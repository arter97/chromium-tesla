// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/video/gpu_memory_buffer_video_frame_pool.h"

#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <stddef.h>
#include <stdint.h>

#include <atomic>
#include <list>
#include <memory>
#include <utility>

#include "base/barrier_closure.h"
#include "base/bits.h"
#include "base/command_line.h"
#include "base/containers/circular_deque.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/memory/raw_ptr.h"
#include "base/metrics/histogram_functions.h"
#include "base/metrics/histogram_macros.h"
#include "base/ranges/algorithm.h"
#include "base/strings/stringprintf.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/default_tick_clock.h"
#include "base/time/time.h"
#include "base/trace_event/memory_dump_manager.h"
#include "base/trace_event/memory_dump_provider.h"
#include "base/trace_event/trace_event.h"
#include "build/build_config.h"
#include "components/viz/common/resources/shared_image_format.h"
#include "gpu/GLES2/gl2extchromium.h"
#include "gpu/command_buffer/client/client_shared_image.h"
#include "gpu/command_buffer/client/shared_image_interface.h"
#include "gpu/command_buffer/common/shared_image_usage.h"
#include "gpu/config/gpu_switches.h"
#include "media/base/media_switches.h"
#include "media/base/video_types.h"
#include "media/video/gpu_video_accelerator_factories.h"
#include "third_party/libyuv/include/libyuv.h"
#include "ui/gfx/buffer_format_util.h"
#include "ui/gfx/buffer_types.h"
#include "ui/gfx/color_space.h"
#include "ui/gl/trace_util.h"

#if BUILDFLAG(IS_MAC)
#include "base/mac/mac_util.h"
#include "media/base/mac/video_frame_mac.h"
#endif

namespace media {
namespace {

// Allow MappableSI to be used for GpuMemoryBufferVideoFramePool. Note that
// enabling this flag does not necessarily enables MappableSI usage as it also
// currently needs MultiPlanarSI support. MultiPlanarSI is now enabled by
// default on ToT but the flag is still alive for 1 full release as a
// kill-switch.
BASE_FEATURE(kAlwaysUseMappableSIForGpuMemoryBufferVideoFramePool,
             "AlwaysUseMappableSIForGpuMemoryBufferVideoFramePool",
             base::FEATURE_ENABLED_BY_DEFAULT);

}  // namespace

// Implementation of a pool of GpuMemoryBuffers used to back VideoFrames.
class GpuMemoryBufferVideoFramePool::PoolImpl
    : public base::RefCountedThreadSafe<
          GpuMemoryBufferVideoFramePool::PoolImpl>,
      public base::trace_event::MemoryDumpProvider {
 public:
  // |media_task_runner| is the media task runner associated with the
  // GL context provided by |gpu_factories|
  // |worker_task_runner| is a task runner used to asynchronously copy
  // video frame's planes.
  // |gpu_factories| is an interface to GPU related operation and can be
  // null if a GL context is not available.
  PoolImpl(const scoped_refptr<base::SequencedTaskRunner>& media_task_runner,
           const scoped_refptr<base::TaskRunner>& worker_task_runner,
           GpuVideoAcceleratorFactories* const gpu_factories)
      : media_task_runner_(media_task_runner),
        worker_task_runner_(worker_task_runner),
        gpu_factories_(gpu_factories),
        output_format_(GpuVideoAcceleratorFactories::OutputFormat::UNDEFINED),
        tick_clock_(base::DefaultTickClock::GetInstance()),
        is_mappable_si_enabled_(
            base::FeatureList::IsEnabled(
                kAlwaysUseMappableSIForGpuMemoryBufferVideoFramePool) &&
            IsMultiPlaneFormatForSoftwareVideoEnabled()) {
    DCHECK(media_task_runner_);
    DCHECK(worker_task_runner_);

    // Using a static atomic id generator to generate a unique id for each
    // GpuMemoryBufferVideoFramePool in a thread safe manner.
    static std::atomic_uint32_t id = 0;
    pool_id_ = ++id;

    // Moving the common shared image usage here as a member. This can be moved
    // back to local code where MappableSI is created after the GMB path is
    // full removed.
    si_usage_ = gpu::SHARED_IMAGE_USAGE_GLES2_READ |
                gpu::SHARED_IMAGE_USAGE_RASTER_READ |
                gpu::SHARED_IMAGE_USAGE_DISPLAY_READ |
                gpu::SHARED_IMAGE_USAGE_SCANOUT;
#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_MAC)
    // TODO(crbug.com/40194712): Always add the flag once the
    // OzoneImageBacking is by default turned on.
    if (base::CommandLine::ForCurrentProcess()->HasSwitch(
            switches::kEnableUnsafeWebGPU)) {
      // This SharedImage may be used for zero-copy import into WebGPU.
      si_usage_ |= gpu::SHARED_IMAGE_USAGE_WEBGPU_READ;
    }
#endif
  }

  PoolImpl(const PoolImpl&) = delete;
  PoolImpl& operator=(const PoolImpl&) = delete;

  // Takes a software VideoFrame and calls |frame_ready_cb| with a VideoFrame
  // backed by native textures if possible.
  // The data contained in |video_frame| is copied into the returned frame
  // asynchronously posting tasks to |worker_task_runner_|, while
  // |frame_ready_cb| will be called on |media_task_runner_| once all the data
  // has been copied.
  void CreateHardwareFrame(scoped_refptr<VideoFrame> video_frame,
                           FrameReadyCB cb);

  bool OnMemoryDump(const base::trace_event::MemoryDumpArgs& args,
                    base::trace_event::ProcessMemoryDump* pmd) override;

  // Aborts any pending copies.
  void Abort();

  // Shuts down the frame pool and releases all frames in |frames_|.
  // Once this is called frames will no longer be inserted back into
  // |frames_|.
  void Shutdown();

  void SetTickClockForTesting(const base::TickClock* tick_clock);

  bool IsMappableSIEnabled() const;

 private:
  friend class base::RefCountedThreadSafe<
      GpuMemoryBufferVideoFramePool::PoolImpl>;
  ~PoolImpl() override;

  // Resource to represent a plane.
  struct PlaneResource {
    gfx::Size size;
    int32_t buffer_id = -1;
    std::unique_ptr<gfx::GpuMemoryBuffer> gpu_memory_buffer;
    scoped_refptr<gpu::ClientSharedImage> shared_image;

    // Currently when GpuMemoryBuffers are used to represent a resource plane,
    // Map() and UnMap() happens in different methods and are not scoped in the
    // same method. With MappableSI, keeping the |scoped_mapping| will allow to
    // Map() and reset(UnMap()) it as needed to achieve same behavior.
    std::unique_ptr<gpu::ClientSharedImage::ScopedMapping> scoped_mapping;

    // Tracks whether the SharedImage is created with GpuMemoryBuffer containing
    // multiplanar format and prefers external sampler.
    bool needs_external_sampler = false;
  };

  // All the resources needed to compose a frame.
  // TODO(dalecurtis): The method of use marking used is very brittle
  // and prone to leakage. Switch this to pass around std::unique_ptr
  // such that callers own resources explicitly.
  struct FrameResources {
    explicit FrameResources(const gfx::Size& size, gfx::BufferUsage usage)
        : size(size), usage(usage) {}
    void MarkUsed() {
      is_used_ = true;
      last_use_time_ = base::TimeTicks();
    }
    void MarkUnused(base::TimeTicks last_use_time) {
      is_used_ = false;
      last_use_time_ = last_use_time;
    }
    bool is_used() const { return is_used_; }
    base::TimeTicks last_use_time() const { return last_use_time_; }

    const gfx::Size size;
    const gfx::BufferUsage usage;
    PlaneResource plane_resources[VideoFrame::kMaxPlanes];
    // The sync token used to recycle or destroy the resources. It is set when
    // the resources are returned from the VideoFrame (via
    // MailboxHoldersReleased).
    gpu::SyncToken sync_token;

   private:
    bool is_used_ = true;
    base::TimeTicks last_use_time_;
  };

  // Struct to keep track of requested videoframe copies.
  struct VideoFrameCopyRequest {
    VideoFrameCopyRequest(scoped_refptr<VideoFrame> video_frame,
                          FrameReadyCB frame_ready_cb,
                          bool passthrough)
        : video_frame(std::move(video_frame)),
          frame_ready_cb(std::move(frame_ready_cb)),
          passthrough(passthrough) {}
    scoped_refptr<VideoFrame> video_frame;
    FrameReadyCB frame_ready_cb;
    bool passthrough;
  };

  // Start the copy of a video_frame on the worker_task_runner_.
  // It assumes there are currently no in-flight copies and works on the request
  // in the front of |frame_copy_requests_| queue.
  void StartCopy();

  // Copy |video_frame| data into |frame_resources| and calls |frame_ready_cb|
  // when done.
  void CopyVideoFrameToGpuMemoryBuffers(scoped_refptr<VideoFrame> video_frame,
                                        FrameResources* frame_resources);

  // Called when all the data has been copied.
  void OnCopiesDone(bool copy_failed,
                    scoped_refptr<VideoFrame> video_frame,
                    FrameResources* frame_resources);

  // Called on the media thread when all data has been copied.
  void OnCopiesDoneOnMediaThread(bool copy_failed,
                                 scoped_refptr<VideoFrame> video_frame,
                                 FrameResources* frame_resources);

  static void CopyRowsToBuffer(
      GpuVideoAcceleratorFactories::OutputFormat output_format,
      const size_t plane,
      const size_t row,
      const size_t rows_to_copy,
      const gfx::Size coded_size,
      const VideoFrame* video_frame,
      FrameResources* frame_resources,
      base::OnceClosure done);
  // Prepares GL resources, mailboxes and allocates the new VideoFrame. This has
  // to be run on `media_task_runner_`. On failure, this will release
  // `frame_resources` and return nullptr.
  scoped_refptr<VideoFrame> BindAndCreateMailboxesHardwareFrameResources(
      FrameResources* frame_resources,
      const gfx::Size& coded_size,
      const gfx::Rect& visible_rect,
      const gfx::Size& natural_size,
      const gfx::ColorSpace& color_space,
      base::TimeDelta timestamp,
      bool video_frame_allow_overlay,
      const std::optional<gpu::VulkanYCbCrInfo>& ycbcr_info);

  // Return true if |resources| can be used to represent a frame for
  // specific |format| and |size|.
  static bool AreFrameResourcesCompatible(const FrameResources* resources,
                                          const gfx::Size& size,
                                          gfx::BufferUsage usage) {
    return size == resources->size && usage == resources->usage;
  }

  // Get the resources needed for a frame out of the pool, or create them if
  // necessary.
  // This also drops the LRU resources that can't be reuse for this frame.
  FrameResources* GetOrCreateFrameResources(const gfx::Size& size,
                                            gfx::BufferUsage usage,
                                            const gfx::ColorSpace& color_space);

  // Calls the FrameReadyCB of the first entry in |frame_copy_requests_|, with
  // the provided |video_frame|, then deletes the entry from
  // |frame_copy_requests_| and attempts to start another copy if there are
  // other |frame_copy_requests_| elements.
  void CompleteCopyRequestAndMaybeStartNextCopy(
      scoped_refptr<VideoFrame> video_frame);

  // Callback called when a VideoFrame generated with GetFrameResources is no
  // longer referenced.
  void MailboxHoldersReleased(FrameResources* frame_resources,
                              const gpu::SyncToken& sync_token);

  // Delete resources. This has to be called on the thread where |task_runner|
  // is current.
  static void DeleteFrameResources(
      GpuVideoAcceleratorFactories* const gpu_factories,
      FrameResources* frame_resources);

  // Task runner associated to the GL context provided by |gpu_factories_|.
  const scoped_refptr<base::SequencedTaskRunner> media_task_runner_;
  // Task runner used to asynchronously copy planes.
  const scoped_refptr<base::TaskRunner> worker_task_runner_;

  // Interface to GPU related operations.
  const raw_ptr<GpuVideoAcceleratorFactories> gpu_factories_;

  // Pool of resources.
  std::list<raw_ptr<FrameResources, CtnExperimental>> resources_pool_;

  GpuVideoAcceleratorFactories::OutputFormat output_format_;

  // |tick_clock_| is always a DefaultTickClock outside of testing.
  raw_ptr<const base::TickClock> tick_clock_;

  // Queued up video frames for copies. The front is the currently
  // in-flight copy, new copies are added at the end.
  base::circular_deque<VideoFrameCopyRequest> frame_copy_requests_;
  bool in_shutdown_ = false;

  // Id used in ::OnMemoryDump to identify the GpuMemoryBufferVideoFramePool.
  uint32_t pool_id_ = 0;

  // Unique Id generated each time a GpuMemoryBuffer is created. This is used
  // to identify the GpuMemoryBuffer. This is done in order to stop using
  // GpuMemoryBuffer::GetId() and eventually GpuMemoryBuffer altogether when
  // MappableSI is enabled.
  uint32_t buffer_id_ = 0;

  const bool is_mappable_si_enabled_;
  uint32_t si_usage_ = 0;
};

namespace {

// VideoFrame copies to GpuMemoryBuffers will be split in copies where the
// output size is |kBytesPerCopyTarget| bytes and run in parallel.
constexpr size_t kBytesPerCopyTarget = 1024 * 1024;  // 1MB

// Return the GpuMemoryBuffer format to use for a specific VideoPixelFormat
// and plane.
gfx::BufferFormat GpuMemoryBufferFormat(
    GpuVideoAcceleratorFactories::OutputFormat format,
    size_t plane) {
  switch (format) {
    case GpuVideoAcceleratorFactories::OutputFormat::I420:
      DCHECK_LE(plane, 2u);
      return gfx::BufferFormat::R_8;
    case GpuVideoAcceleratorFactories::OutputFormat::YV12:
      DCHECK_EQ(0u, plane);
      return gfx::BufferFormat::YVU_420;
    case GpuVideoAcceleratorFactories::OutputFormat::P010:
      DCHECK_LE(plane, 1u);
      return gfx::BufferFormat::P010;
    case GpuVideoAcceleratorFactories::OutputFormat::NV12_SINGLE_GMB:
      DCHECK_LE(plane, 1u);
      return gfx::BufferFormat::YUV_420_BIPLANAR;
    case GpuVideoAcceleratorFactories::OutputFormat::NV12_DUAL_GMB:
      DCHECK_LE(plane, 1u);
      return plane == 0 ? gfx::BufferFormat::R_8 : gfx::BufferFormat::RG_88;
    case GpuVideoAcceleratorFactories::OutputFormat::XR30:
      DCHECK_EQ(0u, plane);
      return gfx::BufferFormat::BGRA_1010102;
    case GpuVideoAcceleratorFactories::OutputFormat::XB30:
      DCHECK_EQ(0u, plane);
      return gfx::BufferFormat::RGBA_1010102;
    case GpuVideoAcceleratorFactories::OutputFormat::RGBA:
      DCHECK_EQ(0u, plane);
      return gfx::BufferFormat::RGBA_8888;
    case GpuVideoAcceleratorFactories::OutputFormat::BGRA:
      DCHECK_EQ(0u, plane);
      return gfx::BufferFormat::BGRA_8888;
    case GpuVideoAcceleratorFactories::OutputFormat::UNDEFINED:
      NOTREACHED_IN_MIGRATION();
      break;
  }
  return gfx::BufferFormat::BGRA_8888;
}

// Return the SharedImageFormat format to use for a specific VideoPixelFormat
// and plane.
viz::SharedImageFormat OutputFormatToSharedImageFormat(
    GpuVideoAcceleratorFactories::OutputFormat format,
    size_t plane) {
  switch (format) {
    case GpuVideoAcceleratorFactories::OutputFormat::I420:
      DCHECK_LE(plane, 2u);
      // We have GMBs per plane for I420 so create shared images per plane
      // as well.
      // TODO(hitawala): Create single GMB and shared image.
      return viz::SinglePlaneFormat::kR_8;
    case GpuVideoAcceleratorFactories::OutputFormat::YV12:
      DCHECK_EQ(plane, 0u);
      return viz::MultiPlaneFormat::kYV12;
    case GpuVideoAcceleratorFactories::OutputFormat::P010:
      DCHECK_EQ(plane, 0u);
      return viz::MultiPlaneFormat::kP010;
    case GpuVideoAcceleratorFactories::OutputFormat::NV12_SINGLE_GMB:
      DCHECK_EQ(plane, 0u);
      return viz::MultiPlaneFormat::kNV12;
    case GpuVideoAcceleratorFactories::OutputFormat::NV12_DUAL_GMB:
      DCHECK_LE(plane, 1u);
      // We have GMBs per plane for NV12_DUAL_GMB so create shared images
      // per plane as well.
      // TODO(hitawala): Create single GMB and shared image.
      return plane == 0 ? viz::SinglePlaneFormat::kR_8
                        : viz::SinglePlaneFormat::kRG_88;
    case GpuVideoAcceleratorFactories::OutputFormat::XR30:
      DCHECK_EQ(0u, plane);
      return viz::SinglePlaneFormat::kBGRA_1010102;
    case GpuVideoAcceleratorFactories::OutputFormat::XB30:
      DCHECK_EQ(0u, plane);
      return viz::SinglePlaneFormat::kRGBA_1010102;
    case GpuVideoAcceleratorFactories::OutputFormat::RGBA:
      DCHECK_EQ(0u, plane);
      return viz::SinglePlaneFormat::kRGBA_8888;
    case GpuVideoAcceleratorFactories::OutputFormat::BGRA:
      DCHECK_EQ(0u, plane);
      return viz::SinglePlaneFormat::kBGRA_8888;
    case GpuVideoAcceleratorFactories::OutputFormat::UNDEFINED:
      NOTREACHED_IN_MIGRATION();
      break;
  }
  return viz::SinglePlaneFormat::kBGRA_8888;
}

// The number of output planes to be copied in each iteration.
size_t PlanesPerCopy(GpuVideoAcceleratorFactories::OutputFormat format) {
  switch (format) {
    case GpuVideoAcceleratorFactories::OutputFormat::I420:
    case GpuVideoAcceleratorFactories::OutputFormat::RGBA:
    case GpuVideoAcceleratorFactories::OutputFormat::BGRA:
    case GpuVideoAcceleratorFactories::OutputFormat::XR30:
    case GpuVideoAcceleratorFactories::OutputFormat::XB30:
      return 1;
    case GpuVideoAcceleratorFactories::OutputFormat::NV12_DUAL_GMB:
    case GpuVideoAcceleratorFactories::OutputFormat::NV12_SINGLE_GMB:
    case GpuVideoAcceleratorFactories::OutputFormat::P010:
      return 2;
    case GpuVideoAcceleratorFactories::OutputFormat::YV12:
      return 3;
    case GpuVideoAcceleratorFactories::OutputFormat::UNDEFINED:
      NOTREACHED_IN_MIGRATION();
      break;
  }
  return 0;
}

VideoPixelFormat VideoFormat(
    GpuVideoAcceleratorFactories::OutputFormat format) {
  switch (format) {
    case GpuVideoAcceleratorFactories::OutputFormat::I420:
      return PIXEL_FORMAT_I420;
    case GpuVideoAcceleratorFactories::OutputFormat::YV12:
      return PIXEL_FORMAT_YV12;

    case GpuVideoAcceleratorFactories::OutputFormat::NV12_SINGLE_GMB:
    case GpuVideoAcceleratorFactories::OutputFormat::NV12_DUAL_GMB:
      return PIXEL_FORMAT_NV12;
    case GpuVideoAcceleratorFactories::OutputFormat::P010:
      return PIXEL_FORMAT_P016LE;
    case GpuVideoAcceleratorFactories::OutputFormat::BGRA:
      return PIXEL_FORMAT_ARGB;
    case GpuVideoAcceleratorFactories::OutputFormat::RGBA:
      return PIXEL_FORMAT_ABGR;
    case GpuVideoAcceleratorFactories::OutputFormat::XR30:
      return PIXEL_FORMAT_XR30;
    case GpuVideoAcceleratorFactories::OutputFormat::XB30:
      return PIXEL_FORMAT_XB30;
    case GpuVideoAcceleratorFactories::OutputFormat::UNDEFINED:
      NOTREACHED_IN_MIGRATION();
      break;
  }
  return PIXEL_FORMAT_UNKNOWN;
}

// The number of output planes to be copied in each iteration.
size_t NumGpuMemoryBuffers(GpuVideoAcceleratorFactories::OutputFormat format) {
  switch (format) {
    case GpuVideoAcceleratorFactories::OutputFormat::I420:
      return 3;
    case GpuVideoAcceleratorFactories::OutputFormat::YV12:
    case GpuVideoAcceleratorFactories::OutputFormat::P010:
    case GpuVideoAcceleratorFactories::OutputFormat::NV12_SINGLE_GMB:
      return 1;
    case GpuVideoAcceleratorFactories::OutputFormat::NV12_DUAL_GMB:
      return 2;
    case GpuVideoAcceleratorFactories::OutputFormat::XR30:
    case GpuVideoAcceleratorFactories::OutputFormat::XB30:
      return 1;
    case GpuVideoAcceleratorFactories::OutputFormat::RGBA:
    case GpuVideoAcceleratorFactories::OutputFormat::BGRA:
      return 1;
    case GpuVideoAcceleratorFactories::OutputFormat::UNDEFINED:
      NOTREACHED_NORETURN();
  }
  NOTREACHED_NORETURN();
}

// The number of output rows to be copied in each iteration.
int RowsPerCopy(size_t plane, VideoPixelFormat format, int width) {
  int bytes_per_row = VideoFrame::RowBytes(plane, format, width);
  if (format == PIXEL_FORMAT_NV12) {
    DCHECK_EQ(0u, plane);
    bytes_per_row += VideoFrame::RowBytes(1, format, width);
  }
  // Copy an even number of lines, and at least one.
  return std::max<size_t>((kBytesPerCopyTarget / bytes_per_row) & ~1, 1);
}

void CopyRowsToI420Buffer(int first_row,
                          int rows,
                          int bytes_per_row,
                          size_t bit_depth,
                          const uint8_t* source,
                          int source_stride,
                          uint8_t* output,
                          int dest_stride) {
  TRACE_EVENT2("media", "CopyRowsToI420Buffer", "bytes_per_row", bytes_per_row,
               "rows", rows);

  if (!output)
    return;

  DCHECK_NE(dest_stride, 0);
  DCHECK_LE(bytes_per_row, std::abs(dest_stride));
  DCHECK_LE(bytes_per_row, source_stride);
  DCHECK_GE(bit_depth, 8u);

  if (bit_depth == 8) {
    libyuv::CopyPlane(source + source_stride * first_row, source_stride,
                      output + dest_stride * first_row, dest_stride,
                      bytes_per_row, rows);
  } else {
    const int scale = 0x10000 >> (bit_depth - 8);
    libyuv::Convert16To8Plane(
        reinterpret_cast<const uint16_t*>(source + source_stride * first_row),
        source_stride / 2, output + dest_stride * first_row, dest_stride, scale,
        bytes_per_row, rows);
  }
}

void CopyRowsToP010Buffer(int first_row,
                          int rows,
                          int width,
                          const VideoFrame* source_frame,
                          uint8_t* dest_y,
                          int dest_stride_y,
                          uint8_t* dest_uv,
                          int dest_stride_uv) {
  TRACE_EVENT2("media", "CopyRowsToP010Buffer", "width", width, "rows", rows);

  if (!dest_y || !dest_uv)
    return;

  DCHECK_NE(dest_stride_y, 0);
  DCHECK_NE(dest_stride_uv, 0);
  DCHECK_EQ(0, first_row % 2);
  DCHECK_EQ(source_frame->format(), PIXEL_FORMAT_YUV420P10);
  DCHECK_LE(width * 2, source_frame->stride(VideoFrame::Plane::kY));

  const uint16_t* y_plane = reinterpret_cast<const uint16_t*>(
      source_frame->visible_data(VideoFrame::Plane::kY) +
      first_row * source_frame->stride(VideoFrame::Plane::kY));
  const size_t y_plane_stride = source_frame->stride(VideoFrame::Plane::kY) / 2;
  const uint16_t* u_plane = reinterpret_cast<const uint16_t*>(
      source_frame->visible_data(VideoFrame::Plane::kU) +
      (first_row / 2) * source_frame->stride(VideoFrame::Plane::kU));
  const size_t u_plane_stride = source_frame->stride(VideoFrame::Plane::kU) / 2;
  const uint16_t* v_plane = reinterpret_cast<const uint16_t*>(
      source_frame->visible_data(VideoFrame::Plane::kV) +
      (first_row / 2) * source_frame->stride(VideoFrame::Plane::kV));
  const size_t v_plane_stride = source_frame->stride(VideoFrame::Plane::kV) / 2;

  libyuv::I010ToP010(
      y_plane, y_plane_stride, u_plane, u_plane_stride, v_plane, v_plane_stride,
      reinterpret_cast<uint16_t*>(dest_y + first_row * dest_stride_y),
      dest_stride_y / 2,
      reinterpret_cast<uint16_t*>(dest_uv + (first_row / 2) * dest_stride_uv),
      dest_stride_uv / 2, width, rows);
}

void CopyRowsToNV12Buffer(int first_row,
                          int rows_y,
                          int rows_uv,
                          int bytes_per_row_y,
                          int bytes_per_row_uv,
                          const VideoFrame* source_frame,
                          uint8_t* dest_y,
                          int dest_stride_y,
                          uint8_t* dest_uv,
                          int dest_stride_uv) {
  TRACE_EVENT2("media", "CopyRowsToNV12Buffer", "bytes_per_row",
               bytes_per_row_y, "rows", rows_y);

  if (!dest_y || !dest_uv)
    return;

  DCHECK_NE(dest_stride_y, 0);
  DCHECK_NE(dest_stride_uv, 0);
  DCHECK_LE(bytes_per_row_y, std::abs(dest_stride_y));
  DCHECK_LE(bytes_per_row_uv, std::abs(dest_stride_uv));
  DCHECK_EQ(0, first_row % 2);
  DCHECK(source_frame->format() == PIXEL_FORMAT_I420 ||
         source_frame->format() == PIXEL_FORMAT_YV12 ||
         source_frame->format() == PIXEL_FORMAT_NV12);
  if (source_frame->format() == PIXEL_FORMAT_NV12) {
    libyuv::CopyPlane(
        source_frame->visible_data(VideoFrame::Plane::kY) +
            first_row * source_frame->stride(VideoFrame::Plane::kY),
        source_frame->stride(VideoFrame::Plane::kY),
        dest_y + first_row * dest_stride_y, dest_stride_y, bytes_per_row_y,
        rows_y);
    libyuv::CopyPlane(
        source_frame->visible_data(VideoFrame::Plane::kUV) +
            first_row / 2 * source_frame->stride(VideoFrame::Plane::kUV),
        source_frame->stride(VideoFrame::Plane::kUV),
        dest_uv + first_row / 2 * dest_stride_uv, dest_stride_uv,
        bytes_per_row_uv, rows_uv);

    return;
  }

  libyuv::I420ToNV12(
      source_frame->visible_data(VideoFrame::Plane::kY) +
          first_row * source_frame->stride(VideoFrame::Plane::kY),
      source_frame->stride(VideoFrame::Plane::kY),
      source_frame->visible_data(VideoFrame::Plane::kU) +
          first_row / 2 * source_frame->stride(VideoFrame::Plane::kU),
      source_frame->stride(VideoFrame::Plane::kU),
      source_frame->visible_data(VideoFrame::Plane::kV) +
          first_row / 2 * source_frame->stride(VideoFrame::Plane::kV),
      source_frame->stride(VideoFrame::Plane::kV),
      dest_y + first_row * dest_stride_y, dest_stride_y,
      dest_uv + first_row / 2 * dest_stride_uv, dest_stride_uv, bytes_per_row_y,
      rows_y);
}

void CopyRowsToRGB10Buffer(bool is_argb,
                           int first_row,
                           int rows,
                           int width,
                           const VideoFrame* source_frame,
                           uint8_t* output,
                           int dest_stride) {
  TRACE_EVENT2("media", "CopyRowsToRGB10Buffer", "bytes_per_row", width * 2,
               "rows", rows);
  if (!output)
    return;

  DCHECK_NE(dest_stride, 0);
  DCHECK_LE(width, std::abs(dest_stride / 2));
  DCHECK_EQ(0, first_row % 2);
  DCHECK_EQ(source_frame->format(), PIXEL_FORMAT_YUV420P10);

  const uint16_t* y_plane = reinterpret_cast<const uint16_t*>(
      source_frame->visible_data(VideoFrame::Plane::kY) +
      first_row * source_frame->stride(VideoFrame::Plane::kY));
  const size_t y_plane_stride = source_frame->stride(VideoFrame::Plane::kY) / 2;
  const uint16_t* v_plane = reinterpret_cast<const uint16_t*>(
      source_frame->visible_data(VideoFrame::Plane::kV) +
      first_row / 2 * source_frame->stride(VideoFrame::Plane::kV));
  const size_t v_plane_stride = source_frame->stride(VideoFrame::Plane::kV) / 2;
  const uint16_t* u_plane = reinterpret_cast<const uint16_t*>(
      source_frame->visible_data(VideoFrame::Plane::kU) +
      first_row / 2 * source_frame->stride(VideoFrame::Plane::kU));
  const size_t u_plane_stride = source_frame->stride(VideoFrame::Plane::kU) / 2;
  uint8_t* dest_rgb10 = output + first_row * dest_stride;

  SkYUVColorSpace skyuv = kRec709_SkYUVColorSpace;
  source_frame->ColorSpace().ToSkYUVColorSpace(&skyuv);

  if (skyuv == kRec601_SkYUVColorSpace) {
    if (is_argb) {
      libyuv::I010ToAR30(y_plane, y_plane_stride, u_plane, u_plane_stride,
                         v_plane, v_plane_stride, dest_rgb10, dest_stride,
                         width, rows);
    } else {
      libyuv::I010ToAB30(y_plane, y_plane_stride, u_plane, u_plane_stride,
                         v_plane, v_plane_stride, dest_rgb10, dest_stride,
                         width, rows);
    }
  } else if (skyuv == kBT2020_SkYUVColorSpace) {
    if (is_argb) {
      libyuv::U010ToAR30(y_plane, y_plane_stride, u_plane, u_plane_stride,
                         v_plane, v_plane_stride, dest_rgb10, dest_stride,
                         width, rows);
    } else {
      libyuv::U010ToAB30(y_plane, y_plane_stride, u_plane, u_plane_stride,
                         v_plane, v_plane_stride, dest_rgb10, dest_stride,
                         width, rows);
    }
  } else {  // BT.709
    if (is_argb) {
      libyuv::H010ToAR30(y_plane, y_plane_stride, u_plane, u_plane_stride,
                         v_plane, v_plane_stride, dest_rgb10, dest_stride,
                         width, rows);
    } else {
      libyuv::H010ToAB30(y_plane, y_plane_stride, u_plane, u_plane_stride,
                         v_plane, v_plane_stride, dest_rgb10, dest_stride,
                         width, rows);
    }
  }
}

void CopyRowsToRGBABuffer(bool is_rgba,
                          int first_row,
                          int rows,
                          int width,
                          const VideoFrame* source_frame,
                          uint8_t* output,
                          int dest_stride) {
  TRACE_EVENT2("media", "CopyRowsToRGBABuffer", "bytes_per_row", width * 2,
               "rows", rows);

  if (!output)
    return;

  DCHECK_NE(dest_stride, 0);
  DCHECK_LE(width, std::abs(dest_stride / 2));
  DCHECK_EQ(0, first_row % 2);
  DCHECK_EQ(source_frame->format(), PIXEL_FORMAT_I420A);

  // libyuv uses little-endian for RGBx formats, whereas here we use big endian.
  auto* func_ptr = is_rgba ? libyuv::I420AlphaToABGR : libyuv::I420AlphaToARGB;

  func_ptr(source_frame->visible_data(VideoFrame::Plane::kY) +
               first_row * source_frame->stride(VideoFrame::Plane::kY),
           source_frame->stride(VideoFrame::Plane::kY),
           source_frame->visible_data(VideoFrame::Plane::kU) +
               first_row / 2 * source_frame->stride(VideoFrame::Plane::kU),
           source_frame->stride(VideoFrame::Plane::kU),
           source_frame->visible_data(VideoFrame::Plane::kV) +
               first_row / 2 * source_frame->stride(VideoFrame::Plane::kV),
           source_frame->stride(VideoFrame::Plane::kV),
           source_frame->visible_data(VideoFrame::Plane::kA) +
               first_row * source_frame->stride(VideoFrame::Plane::kA),
           source_frame->stride(VideoFrame::Plane::kA),
           output + first_row * dest_stride, dest_stride, width, rows,
           // Textures are expected to be premultiplied by GL and compositors.
           1 /* attenuate, meaning premultiply */);
}

gfx::Size CodedSize(const VideoFrame* video_frame,
                    GpuVideoAcceleratorFactories::OutputFormat output_format) {
  DCHECK(gfx::Rect(video_frame->coded_size())
             .Contains(video_frame->visible_rect()));

  size_t width = video_frame->visible_rect().width();
  size_t height = video_frame->visible_rect().height();
  gfx::Size output;
  switch (output_format) {
    case GpuVideoAcceleratorFactories::OutputFormat::I420:
    case GpuVideoAcceleratorFactories::OutputFormat::YV12:
    case GpuVideoAcceleratorFactories::OutputFormat::P010:
    case GpuVideoAcceleratorFactories::OutputFormat::NV12_SINGLE_GMB:
    case GpuVideoAcceleratorFactories::OutputFormat::NV12_DUAL_GMB:
      DCHECK_EQ(video_frame->visible_rect().x() % 2, 0);
      DCHECK_EQ(video_frame->visible_rect().y() % 2, 0);
      if (!gfx::IsOddWidthMultiPlanarBuffersAllowed())
        width = base::bits::AlignUp(width, size_t{2});
      if (!gfx::IsOddHeightMultiPlanarBuffersAllowed())
        height = base::bits::AlignUp(height, size_t{2});
      output = gfx::Size(width, height);
      break;
    case GpuVideoAcceleratorFactories::OutputFormat::XR30:
    case GpuVideoAcceleratorFactories::OutputFormat::XB30:
    case GpuVideoAcceleratorFactories::OutputFormat::RGBA:
    case GpuVideoAcceleratorFactories::OutputFormat::BGRA:
      output = gfx::Size(base::bits::AlignUp(width, size_t{2}), height);
      break;
    case GpuVideoAcceleratorFactories::OutputFormat::UNDEFINED:
      NOTREACHED_IN_MIGRATION();
  }
  DCHECK(gfx::Rect(video_frame->coded_size()).Contains(gfx::Rect(output)));
  return output;
}

bool SetPrefersExternalSampler(viz::SharedImageFormat& format) {
  if (format.is_multi_plane()) {
    // Set prefers external sampler only for multiplanar formats on ozone based
    // platforms.
#if BUILDFLAG(IS_OZONE)
    format.SetPrefersExternalSampler();
    return true;
#endif
  }
  return false;
}

}  // unnamed namespace

// Creates a VideoFrame backed by native textures starting from a software
// VideoFrame.
// The data contained in |video_frame| is copied into the VideoFrame passed to
// |frame_ready_cb|.
// This has to be called on the thread where |media_task_runner_| is current.
void GpuMemoryBufferVideoFramePool::PoolImpl::CreateHardwareFrame(
    scoped_refptr<VideoFrame> video_frame,
    FrameReadyCB frame_ready_cb) {
  DCHECK(media_task_runner_->RunsTasksInCurrentSequence());
  // Lazily initialize |output_format_| since VideoFrameOutputFormat() has to be
  // called on the media_thread while this object might be instantiated on any.
  const VideoPixelFormat pixel_format = video_frame->format();
  if (output_format_ == GpuVideoAcceleratorFactories::OutputFormat::UNDEFINED)
    output_format_ = gpu_factories_->VideoFrameOutputFormat(pixel_format);
  // Bail if we have a change of GpuVideoAcceleratorFactories::OutputFormat;
  // such changes should not happen in general (see https://crbug.com/875158).
  if (output_format_ != gpu_factories_->VideoFrameOutputFormat(pixel_format)) {
    std::move(frame_ready_cb).Run(std::move(video_frame));
    return;
  }

  bool is_software_backed_video_frame = !video_frame->HasTextures();
#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)
  is_software_backed_video_frame &= !video_frame->HasDmaBufs();
#endif

  bool passthrough = false;
#if BUILDFLAG(IS_MAC)
  if (!IOSurfaceCanSetColorSpace(video_frame->ColorSpace()))
    passthrough = true;
#endif

  if (!video_frame->IsMappable()) {
    // Already a hardware frame.
    passthrough = true;
  }

  if (output_format_ == GpuVideoAcceleratorFactories::OutputFormat::UNDEFINED)
    passthrough = true;

  switch (pixel_format) {
    // Supported cases.
    case PIXEL_FORMAT_YV12:
    case PIXEL_FORMAT_I420:
    case PIXEL_FORMAT_YUV420P10:
    case PIXEL_FORMAT_I420A:
    case PIXEL_FORMAT_NV12:
    case PIXEL_FORMAT_NV12A:
      break;
    // Unsupported cases.
    case PIXEL_FORMAT_I422:
    case PIXEL_FORMAT_I444:
    case PIXEL_FORMAT_NV21:
    case PIXEL_FORMAT_UYVY:
    case PIXEL_FORMAT_YUY2:
    case PIXEL_FORMAT_ARGB:
    case PIXEL_FORMAT_BGRA:
    case PIXEL_FORMAT_XRGB:
    case PIXEL_FORMAT_RGB24:
    case PIXEL_FORMAT_MJPEG:
    case PIXEL_FORMAT_YUV422P9:
    case PIXEL_FORMAT_YUV420P9:
    case PIXEL_FORMAT_YUV444P9:
    case PIXEL_FORMAT_YUV422P10:
    case PIXEL_FORMAT_YUV444P10:
    case PIXEL_FORMAT_YUV420P12:
    case PIXEL_FORMAT_YUV422P12:
    case PIXEL_FORMAT_YUV444P12:
    case PIXEL_FORMAT_Y16:
    case PIXEL_FORMAT_ABGR:
    case PIXEL_FORMAT_XBGR:
    case PIXEL_FORMAT_NV16:
    case PIXEL_FORMAT_NV24:
    case PIXEL_FORMAT_P016LE:
    case PIXEL_FORMAT_P216LE:
    case PIXEL_FORMAT_P416LE:
    case PIXEL_FORMAT_XR30:
    case PIXEL_FORMAT_XB30:
    case PIXEL_FORMAT_RGBAF16:
    case PIXEL_FORMAT_I422A:
    case PIXEL_FORMAT_I444A:
    case PIXEL_FORMAT_YUV420AP10:
    case PIXEL_FORMAT_YUV422AP10:
    case PIXEL_FORMAT_YUV444AP10:
    case PIXEL_FORMAT_UNKNOWN:
      if (is_software_backed_video_frame) {
        UMA_HISTOGRAM_ENUMERATION(
            "Media.GpuMemoryBufferVideoFramePool.UnsupportedFormat",
            pixel_format, PIXEL_FORMAT_MAX + 1);
      }
      passthrough = true;
  }

  // TODO(crbug.com/40481128): Handle odd positioned video frame input.
  if (video_frame->visible_rect().x() % 2 ||
      video_frame->visible_rect().y() % 2) {
    passthrough = true;
  }

  // TODO(https://crbug.com/webrtc/9033): Eliminate odd size video frame input
  // cases as they are not valid.
  if (video_frame->coded_size().width() % 2 &&
      !gfx::IsOddWidthMultiPlanarBuffersAllowed()) {
    passthrough = true;
  }
  if (video_frame->coded_size().height() % 2 &&
      !gfx::IsOddHeightMultiPlanarBuffersAllowed()) {
    passthrough = true;
  }

  frame_copy_requests_.emplace_back(std::move(video_frame),
                                    std::move(frame_ready_cb), passthrough);
  if (frame_copy_requests_.size() == 1u)
    StartCopy();
}

bool GpuMemoryBufferVideoFramePool::PoolImpl::OnMemoryDump(
    const base::trace_event::MemoryDumpArgs& args,
    base::trace_event::ProcessMemoryDump* pmd) {
  const uint64_t tracing_process_id =
      base::trace_event::MemoryDumpManager::GetInstance()
          ->GetTracingProcessId();
  const int kImportance = 2;
  for (const FrameResources* frame_resources : resources_pool_) {
    for (const PlaneResource& plane_resource :
         frame_resources->plane_resources) {
      if ((is_mappable_si_enabled_ && plane_resource.shared_image) ||
          (!is_mappable_si_enabled_ && plane_resource.gpu_memory_buffer)) {
        std::string dump_name =
            base::StringPrintf("media/video_frame_memory_%d/buffer_%d",
                               pool_id_, plane_resource.buffer_id);
        base::trace_event::MemoryAllocatorDump* dump =
            pmd->CreateAllocatorDump(dump_name);

        size_t buffer_size_in_bytes =
            is_mappable_si_enabled_
                ? plane_resource.shared_image->format().EstimatedSizeInBytes(
                      plane_resource.size)
                : gfx::BufferSizeForBufferFormat(
                      plane_resource.size,
                      plane_resource.gpu_memory_buffer->GetFormat());

        dump->AddScalar(base::trace_event::MemoryAllocatorDump::kNameSize,
                        base::trace_event::MemoryAllocatorDump::kUnitsBytes,
                        buffer_size_in_bytes);
        dump->AddScalar("free_size",
                        base::trace_event::MemoryAllocatorDump::kUnitsBytes,
                        frame_resources->is_used() ? 0 : buffer_size_in_bytes);
        if (is_mappable_si_enabled_) {
          plane_resource.shared_image->OnMemoryDump(pmd, dump->guid(),
                                                    kImportance);
        } else {
          plane_resource.gpu_memory_buffer->OnMemoryDump(
              pmd, dump->guid(), tracing_process_id, kImportance);
        }
      }
    }
  }
  return true;
}

void GpuMemoryBufferVideoFramePool::PoolImpl::Abort() {
  DCHECK(media_task_runner_->RunsTasksInCurrentSequence());
  // Abort any pending copy requests. If one is already in flight, we can't do
  // anything about it.
  if (frame_copy_requests_.size() <= 1u)
    return;
  frame_copy_requests_.erase(frame_copy_requests_.begin() + 1,
                             frame_copy_requests_.end());
}

void GpuMemoryBufferVideoFramePool::PoolImpl::OnCopiesDone(
    bool copy_failed,
    scoped_refptr<VideoFrame> video_frame,
    FrameResources* frame_resources) {
  if (!copy_failed) {
    for (auto& plane_resource : frame_resources->plane_resources) {
      if (plane_resource.gpu_memory_buffer) {
        // Additional checks for sanity and debug until MappableSI is launched.
        CHECK(!is_mappable_si_enabled_);
        plane_resource.gpu_memory_buffer->Unmap();
#if BUILDFLAG(IS_MAC)
        plane_resource.gpu_memory_buffer->SetColorSpace(
            video_frame->ColorSpace());
#endif
      } else if (plane_resource.scoped_mapping) {
        CHECK(is_mappable_si_enabled_);
        plane_resource.scoped_mapping.reset();
#if BUILDFLAG(IS_MAC)
        plane_resource.shared_image->SetColorSpaceOnNativeBuffer(
            video_frame->ColorSpace());
#endif
      }
    }
  }

  TRACE_EVENT_NESTABLE_ASYNC_END0(
      "media", "CopyVideoFrameToGpuMemoryBuffers",
      TRACE_ID_WITH_SCOPE("CopyVideoFrameToGpuMemoryBuffers",
                          video_frame->timestamp().InNanoseconds()));

  media_task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(&PoolImpl::OnCopiesDoneOnMediaThread, this, copy_failed,
                     std::move(video_frame), frame_resources));
}

void GpuMemoryBufferVideoFramePool::PoolImpl::StartCopy() {
  DCHECK(media_task_runner_->RunsTasksInCurrentSequence());
  DCHECK(!frame_copy_requests_.empty());

  while (!frame_copy_requests_.empty()) {
    VideoFrameCopyRequest& request = frame_copy_requests_.front();
    // Acquire resources. Incompatible ones will be dropped from the pool.
    FrameResources* frame_resources =
        request.passthrough
            ? nullptr
            : GetOrCreateFrameResources(
                  CodedSize(request.video_frame.get(), output_format_),
                  gfx::BufferUsage::SCANOUT_CPU_READ_WRITE,
                  request.video_frame->ColorSpace());
    if (!frame_resources) {
      std::move(request.frame_ready_cb).Run(std::move(request.video_frame));
      frame_copy_requests_.pop_front();
      continue;
    }

    worker_task_runner_->PostTask(
        FROM_HERE, base::BindOnce(&PoolImpl::CopyVideoFrameToGpuMemoryBuffers,
                                  this, request.video_frame, frame_resources));
    break;
  }
}

// Copies |video_frame| into |frame_resources| asynchronously, posting n tasks
// that will be synchronized by a barrier.
// After the barrier is passed OnCopiesDone will be called.
void GpuMemoryBufferVideoFramePool::PoolImpl::CopyVideoFrameToGpuMemoryBuffers(
    scoped_refptr<VideoFrame> video_frame,
    FrameResources* frame_resources) {
  // Compute the number of tasks to post and create the barrier.
  const size_t num_planes = VideoFrame::NumPlanes(VideoFormat(output_format_));
  const size_t planes_per_copy = PlanesPerCopy(output_format_);
  const gfx::Size coded_size = CodedSize(video_frame.get(), output_format_);
  size_t copies = 0;
  for (size_t i = 0; i < num_planes; i += planes_per_copy) {
    const int rows =
        VideoFrame::Rows(i, VideoFormat(output_format_), coded_size.height());
    const int rows_per_copy =
        RowsPerCopy(i, VideoFormat(output_format_), coded_size.width());
    copies += rows / rows_per_copy;
    if (rows % rows_per_copy)
      ++copies;
  }

  for (size_t i = 0; i < NumGpuMemoryBuffers(output_format_); ++i) {
    auto& plane_resource = frame_resources->plane_resources[i];
    bool mapping_succeeded = false;

    // Shared image mapping (if enabled).
    if (is_mappable_si_enabled_) {
      mapping_succeeded = plane_resource.shared_image &&
                          (plane_resource.scoped_mapping =
                               plane_resource.shared_image->Map()) != nullptr;
    } else {
      // Check GPU memory buffer mapping.
      mapping_succeeded = plane_resource.gpu_memory_buffer &&
                          plane_resource.gpu_memory_buffer->Map();
    }

    // Error handling.
    if (!mapping_succeeded) {
      DLOG(ERROR) << "Could not get or map buffer.";

      // Unmap previously mapped buffers.
      for (size_t j = 0; j < i; ++j) {
        if (is_mappable_si_enabled_) {
          frame_resources->plane_resources[j].scoped_mapping.reset();
        } else {
          frame_resources->plane_resources[j].gpu_memory_buffer->Unmap();
        }
      }
      OnCopiesDone(/*copy_failed=*/true, std::move(video_frame),
                   frame_resources);
      return;
    }
  }

  auto on_copies_done =
      base::BindOnce(&PoolImpl::OnCopiesDone, this, /*copy_failed=*/false,
                     video_frame, frame_resources);
  TRACE_EVENT_NESTABLE_ASYNC_BEGIN0(
      "media", "CopyVideoFrameToGpuMemoryBuffers",
      TRACE_ID_WITH_SCOPE("CopyVideoFrameToGpuMemoryBuffers",
                          video_frame->timestamp().InNanoseconds()));
  // If the frame can be copied in one step, do it directly.
  if (copies == 1) {
    DCHECK_LE(num_planes, planes_per_copy);
    const int rows = VideoFrame::Rows(/*plane=*/0, VideoFormat(output_format_),
                                      coded_size.height());
    DCHECK_LE(rows, RowsPerCopy(
                        /*plane=*/0, VideoFormat(output_format_),
                        coded_size.width()));
    CopyRowsToBuffer(output_format_, /*plane=*/0, /*row=*/0, rows, coded_size,
                     video_frame.get(), frame_resources,
                     std::move(on_copies_done));
    return;
  }

  // |barrier| keeps refptr of |video_frame| until all copy tasks are done.
  const base::RepeatingClosure barrier =
      base::BarrierClosure(copies, std::move(on_copies_done));
  // If is more than one copy, post each copy async.
  for (size_t i = 0; i < num_planes; i += planes_per_copy) {
    const int rows =
        VideoFrame::Rows(i, VideoFormat(output_format_), coded_size.height());
    const int rows_per_copy =
        RowsPerCopy(i, VideoFormat(output_format_), coded_size.width());

    for (int row = 0; row < rows; row += rows_per_copy) {
      const int rows_to_copy = std::min(rows_per_copy, rows - row);
      worker_task_runner_->PostTask(
          FROM_HERE, base::BindOnce(&CopyRowsToBuffer, output_format_, i, row,
                                    rows_to_copy, coded_size,
                                    base::Unretained(video_frame.get()),
                                    frame_resources, barrier));
    }
  }
}

// static
void GpuMemoryBufferVideoFramePool::PoolImpl::CopyRowsToBuffer(
    GpuVideoAcceleratorFactories::OutputFormat output_format,
    const size_t plane,
    const size_t row,
    const size_t rows_to_copy,
    const gfx::Size coded_size,
    const VideoFrame* video_frame,
    FrameResources* frame_resources,
    base::OnceClosure done) {
  base::ScopedClosureRunner done_runner(std::move(done));
  gfx::GpuMemoryBuffer* buffer =
      frame_resources->plane_resources[plane].gpu_memory_buffer.get();
  auto* scoped_mapping =
      frame_resources->plane_resources[plane].scoped_mapping.get();

  // To handle plane 0 of the underlying buffer.
  uint8_t* memory_ptr0 = static_cast<uint8_t*>(
      scoped_mapping ? scoped_mapping->Memory(0) : buffer->memory(0));
  size_t stride0 =
      scoped_mapping ? scoped_mapping->Stride(0) : buffer->stride(0);

  switch (output_format) {
    case GpuVideoAcceleratorFactories::OutputFormat::I420: {
      const int bytes_per_row = VideoFrame::RowBytes(
          plane, VideoFormat(output_format), coded_size.width());
      CopyRowsToI420Buffer(row, rows_to_copy, bytes_per_row,
                           video_frame->BitDepth(),
                           video_frame->visible_data(plane),
                           video_frame->stride(plane), memory_ptr0, stride0);
      break;
    }

    case GpuVideoAcceleratorFactories::OutputFormat::YV12: {
      DCHECK(video_frame->format() == PIXEL_FORMAT_I420 ||
             video_frame->format() == PIXEL_FORMAT_YUV420P10)
          << VideoPixelFormatToString(video_frame->format());

      VideoPixelFormat pixel_format = VideoFormat(output_format);
      for (int dst_plane = 0; dst_plane < 3; ++dst_plane) {
        static constexpr VideoFrame::Plane kSrcPlanes[3] = {
            VideoFrame::Plane::kY, VideoFrame::Plane::kV,
            VideoFrame::Plane::kU};
        VideoFrame::Plane src_plane = kSrcPlanes[dst_plane];

        const size_t plane_row_start =
            row / VideoFrame::SampleSize(pixel_format, src_plane).height();
        const size_t plane_rows_to_copy =
            VideoFrame::Rows(src_plane, pixel_format, rows_to_copy);
        const size_t plane_bytes_per_row =
            VideoFrame::RowBytes(src_plane, pixel_format, coded_size.width());

        CopyRowsToI420Buffer(
            plane_row_start, plane_rows_to_copy, plane_bytes_per_row,
            video_frame->BitDepth(), video_frame->visible_data(src_plane),
            video_frame->stride(src_plane),
            static_cast<uint8_t*>(scoped_mapping
                                      ? scoped_mapping->Memory(dst_plane)
                                      : buffer->memory(dst_plane)),
            scoped_mapping ? scoped_mapping->Stride(dst_plane)
                           : buffer->stride(dst_plane));
      }
      break;
    }

    case GpuVideoAcceleratorFactories::OutputFormat::P010:
      CopyRowsToP010Buffer(
          row, rows_to_copy, coded_size.width(), video_frame, memory_ptr0,
          stride0,
          static_cast<uint8_t*>(scoped_mapping ? scoped_mapping->Memory(1)
                                               : buffer->memory(1)),
          scoped_mapping ? scoped_mapping->Stride(1) : buffer->stride(1));
      break;

    case GpuVideoAcceleratorFactories::OutputFormat::NV12_SINGLE_GMB: {
      const size_t rows_to_copy_y = VideoFrame::Rows(
          VideoFrame::Plane::kY, VideoFormat(output_format), rows_to_copy);
      const size_t rows_to_copy_uv = VideoFrame::Rows(
          VideoFrame::Plane::kUV, VideoFormat(output_format), rows_to_copy);
      const size_t bytes_per_row_y =
          VideoFrame::RowBytes(VideoFrame::Plane::kY,
                               VideoFormat(output_format), coded_size.width());
      const size_t bytes_per_row_uv =
          VideoFrame::RowBytes(VideoFrame::Plane::kUV,
                               VideoFormat(output_format), coded_size.width());
      CopyRowsToNV12Buffer(
          row, rows_to_copy_y, rows_to_copy_uv, bytes_per_row_y,
          bytes_per_row_uv, video_frame, memory_ptr0, stride0,
          static_cast<uint8_t*>(scoped_mapping ? scoped_mapping->Memory(1)
                                               : buffer->memory(1)),
          scoped_mapping ? scoped_mapping->Stride(1) : buffer->stride(1));
      break;
    }

    case GpuVideoAcceleratorFactories::OutputFormat::NV12_DUAL_GMB: {
      const size_t rows_to_copy_y = VideoFrame::Rows(
          VideoFrame::Plane::kY, VideoFormat(output_format), rows_to_copy);
      const size_t rows_to_copy_uv = VideoFrame::Rows(
          VideoFrame::Plane::kUV, VideoFormat(output_format), rows_to_copy);
      const size_t bytes_per_row_y =
          VideoFrame::RowBytes(VideoFrame::Plane::kY,
                               VideoFormat(output_format), coded_size.width());
      const size_t bytes_per_row_uv =
          VideoFrame::RowBytes(VideoFrame::Plane::kUV,
                               VideoFormat(output_format), coded_size.width());
      gfx::GpuMemoryBuffer* buffer2 =
          frame_resources->plane_resources[1].gpu_memory_buffer.get();
      auto* scoped_mapping2 =
          frame_resources->plane_resources[1].scoped_mapping.get();

      CopyRowsToNV12Buffer(
          row, rows_to_copy_y, rows_to_copy_uv, bytes_per_row_y,
          bytes_per_row_uv, video_frame, memory_ptr0, stride0,
          static_cast<uint8_t*>(scoped_mapping2 ? scoped_mapping2->Memory(0)
                                                : buffer2->memory(0)),
          scoped_mapping2 ? scoped_mapping2->Stride(0) : buffer2->stride(0));
      break;
    }

    case GpuVideoAcceleratorFactories::OutputFormat::XR30:
    case GpuVideoAcceleratorFactories::OutputFormat::XB30: {
      const bool is_argb =
          output_format == GpuVideoAcceleratorFactories::OutputFormat::XR30;
      CopyRowsToRGB10Buffer(is_argb, row, rows_to_copy, coded_size.width(),
                            video_frame, memory_ptr0, stride0);
      break;
    }

    case GpuVideoAcceleratorFactories::OutputFormat::RGBA:
    case GpuVideoAcceleratorFactories::OutputFormat::BGRA: {
      const bool is_rgba =
          output_format == GpuVideoAcceleratorFactories::OutputFormat::RGBA;
      CopyRowsToRGBABuffer(is_rgba, row, rows_to_copy, coded_size.width(),
                           video_frame, memory_ptr0, stride0);
      break;
    }

    case GpuVideoAcceleratorFactories::OutputFormat::UNDEFINED:
      NOTREACHED_IN_MIGRATION();
  }
}

void GpuMemoryBufferVideoFramePool::PoolImpl::OnCopiesDoneOnMediaThread(
    bool copy_failed,
    scoped_refptr<VideoFrame> video_frame,
    FrameResources* frame_resources) {
  DCHECK(media_task_runner_->RunsTasksInCurrentSequence());
  if (copy_failed) {
    // Drop the resources if there was an error with them. If we're not in
    // shutdown we also need to remove the pool entry for them.
    if (!in_shutdown_) {
      auto it = base::ranges::find(resources_pool_, frame_resources);
      DCHECK(it != resources_pool_.end());
      resources_pool_.erase(it);
    }

    DeleteFrameResources(gpu_factories_, frame_resources);
    delete frame_resources;

    CompleteCopyRequestAndMaybeStartNextCopy(std::move(video_frame));
    return;
  }

  scoped_refptr<VideoFrame> frame =
      BindAndCreateMailboxesHardwareFrameResources(
          frame_resources, CodedSize(video_frame.get(), output_format_),
          gfx::Rect(video_frame->visible_rect().size()),
          video_frame->natural_size(), video_frame->ColorSpace(),
          video_frame->timestamp(), video_frame->metadata().allow_overlay,
          video_frame->ycbcr_info());
  if (!frame) {
    CompleteCopyRequestAndMaybeStartNextCopy(std::move(video_frame));
    return;
  }

  bool new_allow_overlay = frame->metadata().allow_overlay;
  bool new_read_lock_fences_enabled =
      frame->metadata().read_lock_fences_enabled;
  frame->set_hdr_metadata(video_frame->hdr_metadata());
  frame->metadata().MergeMetadataFrom(video_frame->metadata());
  frame->metadata().allow_overlay = new_allow_overlay;
  frame->metadata().read_lock_fences_enabled = new_read_lock_fences_enabled;
  CompleteCopyRequestAndMaybeStartNextCopy(std::move(frame));
}

scoped_refptr<VideoFrame> GpuMemoryBufferVideoFramePool::PoolImpl::
    BindAndCreateMailboxesHardwareFrameResources(
        FrameResources* frame_resources,
        const gfx::Size& coded_size,
        const gfx::Rect& visible_rect,
        const gfx::Size& natural_size,
        const gfx::ColorSpace& color_space,
        base::TimeDelta timestamp,
        bool video_frame_allow_overlay,
        const std::optional<gpu::VulkanYCbCrInfo>& ycbcr_info) {
  DCHECK(media_task_runner_->RunsTasksInCurrentSequence());
  gpu::SharedImageInterface* sii = gpu_factories_->SharedImageInterface();
  if (!sii) {
    frame_resources->MarkUnused(tick_clock_->NowTicks());
    return nullptr;
  }

  scoped_refptr<gpu::ClientSharedImage> shared_images[VideoFrame::kMaxPlanes];
  bool is_webgpu_compatible = false;
  // Set up the planes creating the mailboxes needed to refer to the textures.
  for (size_t plane = 0; plane < NumGpuMemoryBuffers(output_format_); plane++) {
    PlaneResource& plane_resource = frame_resources->plane_resources[plane];
    gfx::GpuMemoryBuffer* gpu_memory_buffer =
        frame_resources->plane_resources[plane].gpu_memory_buffer.get();

    // This method is only expected to be called when there is a GMB or
    // MappableSI and copy to it after mapping didn't fail.
    CHECK((is_mappable_si_enabled_ && plane_resource.shared_image) ||
          (!is_mappable_si_enabled_ && gpu_memory_buffer));

    auto handle =
        is_mappable_si_enabled_
            ? plane_resource.shared_image->CloneGpuMemoryBufferHandle()
            : gpu_memory_buffer->CloneHandle();

    // Log software/hardware backed GpuMemoryBuffer's `output_format_` used to
    // create the shared image.
    if (handle.type == gfx::GpuMemoryBufferType::SHARED_MEMORY_BUFFER) {
      UMA_HISTOGRAM_ENUMERATION("Media.GPU.OutputFormatSoftwareGmb",
                                output_format_);
    } else {
      UMA_HISTOGRAM_ENUMERATION("Media.GPU.OutputFormatHardwareGmb",
                                output_format_);
    }

#if BUILDFLAG(IS_MAC)
    // Shared image uses iosurface as native resource which is compatible to
    // WebGPU always.
    is_webgpu_compatible =
        media::IOSurfaceIsWebGPUCompatible(handle.io_surface.get());
#endif

#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)
    is_webgpu_compatible =
        handle.native_pixmap_handle.supports_zero_copy_webgpu_import;
#endif

    // Bind the texture and create or rebind the image. This image may be read
    // via the raster interface for import into canvas and/or 2-copy import into
    // WebGL as well as potentially being read via the GLES interface for 1-copy
    // import into WebGL.
    // Note that when MappableSI is enabled via |is_mappable_si_enabled_|, we
    // don't need to create a shared image here as we have already created a
    // MappableSI instead of creating GpuMemoryBuffer in
    // ::GetOrCreateFrameResource().
    if (!is_mappable_si_enabled_ && !plane_resource.shared_image) {
      constexpr char kDebugLabel[] = "MediaGmbVideoFramePool";
      if (IsMultiPlaneFormatForSoftwareVideoEnabled()) {
        viz::SharedImageFormat si_format =
            OutputFormatToSharedImageFormat(output_format_, plane);
        if (handle.type != gfx::GpuMemoryBufferType::SHARED_MEMORY_BUFFER) {
          if (SetPrefersExternalSampler(si_format)) {
            plane_resource.needs_external_sampler = true;
          }
        }
        plane_resource.shared_image =
            sii->CreateSharedImage({si_format, gpu_memory_buffer->GetSize(),
                                    color_space, si_usage_, kDebugLabel},
                                   std::move(handle));
      } else {
        plane_resource.shared_image = sii->CreateSharedImage(
            gpu_memory_buffer, gpu_factories_->GpuMemoryBufferManager(),
            gfx::BufferPlane::DEFAULT,
            {color_space, kTopLeft_GrSurfaceOrigin, kPremul_SkAlphaType,
             si_usage_, kDebugLabel});
      }
      CHECK(plane_resource.shared_image);
    } else {
      sii->UpdateSharedImage(frame_resources->sync_token,
                             plane_resource.shared_image->mailbox());
    }
    shared_images[plane] = plane_resource.shared_image;
  }

  // Insert a sync_token, this is needed to make sure that the textures the
  // mailboxes refer to will be used only after all the previous commands posted
  // in the SharedImageInterface have been processed.
  gpu::SyncToken sync_token = sii->GenUnverifiedSyncToken();
  const gfx::BufferFormat buffer_format =
      GpuMemoryBufferFormat(output_format_, 0);
  auto texture_target = shared_images[0]->GetTextureTarget(
      gfx::BufferUsage::SCANOUT_CPU_READ_WRITE, buffer_format);

  VideoPixelFormat frame_format = VideoFormat(output_format_);

  // Create the VideoFrame backed by native textures.
  scoped_refptr<VideoFrame> frame = VideoFrame::WrapSharedImages(
      frame_format, shared_images, sync_token, texture_target,
      VideoFrame::ReleaseMailboxCB(), coded_size, visible_rect, natural_size,
      timestamp);

  if (!frame) {
    frame_resources->MarkUnused(tick_clock_->NowTicks());
    MailboxHoldersReleased(frame_resources, sync_token);
    return nullptr;
  }
  frame->SetReleaseMailboxCB(
      base::BindOnce(&PoolImpl::MailboxHoldersReleased, this, frame_resources));

  frame->set_color_space(color_space);

  if (ycbcr_info) {
    frame->set_ycbcr_info(ycbcr_info);
  }

  if (NumGpuMemoryBuffers(output_format_) == 1 &&
      IsMultiPlaneFormatForSoftwareVideoEnabled()) {
    // Set type only for NV12_SINGLE_GMB and P010 cases. For NV12_DUAL_GMB and
    // I420 cases there are still multiple GMBs with one for each plane so we
    // have multiple shared images and it still goes through the legacy
    // multiplanar path.
    frame->set_shared_image_format_type(
        SharedImageFormatType::kSharedImageFormat);
    PlaneResource& resource = frame_resources->plane_resources[0];
    if (resource.shared_image->format().PrefersExternalSampler()) {
      frame->set_shared_image_format_type(
          SharedImageFormatType::kSharedImageFormatExternalSampler);
    }
  }

  bool allow_overlay = false;
#if BUILDFLAG(IS_WIN)
  // Windows direct composition path only supports NV12 video overlays. We use
  // separate shared images for the planes for both single and dual NV12 GMBs.
  allow_overlay = (output_format_ ==
                   GpuVideoAcceleratorFactories::OutputFormat::NV12_DUAL_GMB) ||
                  (output_format_ ==
                   GpuVideoAcceleratorFactories::OutputFormat::NV12_SINGLE_GMB);
#else
  switch (output_format_) {
    case GpuVideoAcceleratorFactories::OutputFormat::I420:
    case GpuVideoAcceleratorFactories::OutputFormat::YV12:
      allow_overlay = video_frame_allow_overlay;
      break;
    case GpuVideoAcceleratorFactories::OutputFormat::P010:
    case GpuVideoAcceleratorFactories::OutputFormat::NV12_SINGLE_GMB:
      allow_overlay = true;
      break;
    case GpuVideoAcceleratorFactories::OutputFormat::NV12_DUAL_GMB:
      // Only used on configurations where we can't support overlays.
      break;
    case GpuVideoAcceleratorFactories::OutputFormat::XR30:
    case GpuVideoAcceleratorFactories::OutputFormat::XB30:
      // TODO(mcasas): Enable this for ChromeOS https://crbug.com/776093.
      allow_overlay = false;
#if BUILDFLAG(IS_MAC)
      allow_overlay = IOSurfaceCanSetColorSpace(color_space);
#endif
      // We've converted the YUV to RGB, fix the color space.
      // TODO(hubbe): The libyuv YUV to RGB conversion may not have
      // honored the color space conversion 100%. We should either fix
      // libyuv or find a way for later passes to make up the difference.
      frame->set_color_space(color_space.GetAsRGB());
      break;
    case GpuVideoAcceleratorFactories::OutputFormat::RGBA:
    case GpuVideoAcceleratorFactories::OutputFormat::BGRA:
      allow_overlay = true;
      break;
    case GpuVideoAcceleratorFactories::OutputFormat::UNDEFINED:
      break;
  }
#endif  // BUILDFLAG(IS_WIN)
  frame->metadata().allow_overlay = allow_overlay;
  frame->metadata().read_lock_fences_enabled = true;
  frame->metadata().is_webgpu_compatible = is_webgpu_compatible;
  return frame;
}

// Destroy all the resources posting one task per FrameResources
// to the |media_task_runner_|.
GpuMemoryBufferVideoFramePool::PoolImpl::~PoolImpl() {
  DCHECK(in_shutdown_);
}

void GpuMemoryBufferVideoFramePool::PoolImpl::Shutdown() {
  DCHECK(media_task_runner_->RunsTasksInCurrentSequence());
  // Clients don't care about copies once shutdown has started, so abort them.
  Abort();

  // Delete all the resources on the media thread.
  in_shutdown_ = true;
  for (FrameResources* frame_resources : resources_pool_) {
    // Will be deleted later upon return to pool.
    if (frame_resources->is_used())
      continue;

    media_task_runner_->PostTask(
        FROM_HERE,
        base::BindOnce(&PoolImpl::DeleteFrameResources, gpu_factories_,
                       base::Owned(frame_resources)));
  }
  resources_pool_.clear();
}

void GpuMemoryBufferVideoFramePool::PoolImpl::SetTickClockForTesting(
    const base::TickClock* tick_clock) {
  tick_clock_ = tick_clock;
}

bool GpuMemoryBufferVideoFramePool::PoolImpl::IsMappableSIEnabled() const {
  return is_mappable_si_enabled_;
}

// Tries to find the resources in the pool or create them.
// Incompatible resources will be dropped.
GpuMemoryBufferVideoFramePool::PoolImpl::FrameResources*
GpuMemoryBufferVideoFramePool::PoolImpl::GetOrCreateFrameResources(
    const gfx::Size& size,
    gfx::BufferUsage usage,
    const gfx::ColorSpace& color_space) {
  DCHECK(media_task_runner_->RunsTasksInCurrentSequence());

  auto it = resources_pool_.begin();
  while (it != resources_pool_.end()) {
    FrameResources* frame_resources = *it;
    if (!frame_resources->is_used()) {
      if (AreFrameResourcesCompatible(frame_resources, size, usage)) {
        frame_resources->MarkUsed();
        return frame_resources;
      } else {
        resources_pool_.erase(it++);
        DeleteFrameResources(gpu_factories_, frame_resources);
        delete frame_resources;
      }
    } else {
      it++;
    }
  }

  // Create the resources.
  FrameResources* frame_resources = new FrameResources(size, usage);
  resources_pool_.push_back(frame_resources);
  for (size_t i = 0; i < NumGpuMemoryBuffers(output_format_); i++) {
    PlaneResource& plane_resource = frame_resources->plane_resources[i];
    const size_t width =
        VideoFrame::Columns(i, VideoFormat(output_format_), size.width());
    const size_t height =
        VideoFrame::Rows(i, VideoFormat(output_format_), size.height());
    plane_resource.size = gfx::Size(width, height);

    if (is_mappable_si_enabled_) {
      if (auto* sii = gpu_factories_->SharedImageInterface()) {
        viz::SharedImageFormat si_format =
            OutputFormatToSharedImageFormat(output_format_, i);

        // This needs to be called before creating the mappable shared image
        // here. |si_format| could be modified internally later based on the
        // type of buffer (shared memory or native gpu buffer) backing the
        // shared image. https://issues.chromium.org/339546249.
        SetPrefersExternalSampler(si_format);

        // Create a Mappable shared image.
        plane_resource.shared_image = sii->CreateSharedImage(
            {si_format, plane_resource.size, color_space, si_usage_,
             "MediaGmbVideoFramePoolMappableSI"},
            gpu::kNullSurfaceHandle, usage);
      } else {
        return nullptr;
      }
    } else {
      auto buffer_format = GpuMemoryBufferFormat(output_format_, i);
      plane_resource.gpu_memory_buffer = gpu_factories_->CreateGpuMemoryBuffer(
          plane_resource.size, buffer_format, usage);
    }
  }
  return frame_resources;
}

void GpuMemoryBufferVideoFramePool::PoolImpl::
    CompleteCopyRequestAndMaybeStartNextCopy(
        scoped_refptr<VideoFrame> video_frame) {
  DCHECK(!frame_copy_requests_.empty());

  std::move(frame_copy_requests_.front().frame_ready_cb)
      .Run(std::move(video_frame));
  frame_copy_requests_.pop_front();
  if (!frame_copy_requests_.empty())
    StartCopy();
}

// static
void GpuMemoryBufferVideoFramePool::PoolImpl::DeleteFrameResources(
    GpuVideoAcceleratorFactories* const gpu_factories,
    FrameResources* frame_resources) {
  // TODO(dcastagna): As soon as the context lost is dealt with in media,
  // make sure that we won't execute this callback (use a weak pointer to
  // the old context).
  gpu::SharedImageInterface* sii = gpu_factories->SharedImageInterface();
  if (!sii)
    return;

  for (PlaneResource& plane_resource : frame_resources->plane_resources) {
    if (plane_resource.shared_image) {
      sii->DestroySharedImage(frame_resources->sync_token,
                              std::move(plane_resource.shared_image));
    }
  }
}

// Called when a VideoFrame is no longer referenced.
// Put back the resources in the pool.
void GpuMemoryBufferVideoFramePool::PoolImpl::MailboxHoldersReleased(
    FrameResources* frame_resources,
    const gpu::SyncToken& release_sync_token) {
  if (!media_task_runner_->RunsTasksInCurrentSequence()) {
    media_task_runner_->PostTask(
        FROM_HERE, base::BindOnce(&PoolImpl::MailboxHoldersReleased, this,
                                  frame_resources, release_sync_token));
    return;
  }
  frame_resources->sync_token = release_sync_token;

  if (in_shutdown_) {
    DeleteFrameResources(gpu_factories_, frame_resources);
    delete frame_resources;
    return;
  }

  const base::TimeTicks now = tick_clock_->NowTicks();
  frame_resources->MarkUnused(now);
  auto it = resources_pool_.begin();
  while (it != resources_pool_.end()) {
    FrameResources* resources = *it;

    constexpr base::TimeDelta kStaleFrameLimit = base::Seconds(10);
    if (!resources->is_used() &&
        now - resources->last_use_time() > kStaleFrameLimit) {
      resources_pool_.erase(it++);
      DeleteFrameResources(gpu_factories_, resources);
      delete resources;
    } else {
      it++;
    }
  }
}

GpuMemoryBufferVideoFramePool::GpuMemoryBufferVideoFramePool() = default;

GpuMemoryBufferVideoFramePool::GpuMemoryBufferVideoFramePool(
    const scoped_refptr<base::SequencedTaskRunner>& media_task_runner,
    const scoped_refptr<base::TaskRunner>& worker_task_runner,
    GpuVideoAcceleratorFactories* gpu_factories)
    : pool_impl_(
          new PoolImpl(media_task_runner, worker_task_runner, gpu_factories)) {
  base::trace_event::MemoryDumpManager::GetInstance()
      ->RegisterDumpProviderWithSequencedTaskRunner(
          pool_impl_.get(), "GpuMemoryBufferVideoFramePool", media_task_runner,
          base::trace_event::MemoryDumpProvider::Options());
}

GpuMemoryBufferVideoFramePool::~GpuMemoryBufferVideoFramePool() {
  // May be nullptr in tests.
  if (!pool_impl_)
    return;

  pool_impl_->Shutdown();
  base::trace_event::MemoryDumpManager::GetInstance()->UnregisterDumpProvider(
      pool_impl_.get());
}

void GpuMemoryBufferVideoFramePool::MaybeCreateHardwareFrame(
    scoped_refptr<VideoFrame> video_frame,
    FrameReadyCB frame_ready_cb) {
  DCHECK(video_frame);
  pool_impl_->CreateHardwareFrame(std::move(video_frame),
                                  std::move(frame_ready_cb));
}

void GpuMemoryBufferVideoFramePool::Abort() {
  pool_impl_->Abort();
}

void GpuMemoryBufferVideoFramePool::SetTickClockForTesting(
    const base::TickClock* tick_clock) {
  pool_impl_->SetTickClockForTesting(tick_clock);
}

bool GpuMemoryBufferVideoFramePool::IsMappableSIEnabledForTesting() const {
  return pool_impl_->IsMappableSIEnabled();
}

}  // namespace media
