// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/gpu/chromeos/image_processor.h"

#include <sys/mman.h>
#include <sys/poll.h>

#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include "base/bits.h"
#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/hash/md5.h"
#include "base/rand_util.h"
#include "base/run_loop.h"
#include "base/test/launcher/unit_test_launcher.h"
#include "base/test/test_suite.h"
#include "build/build_config.h"
#include "build/chromeos_buildflags.h"
#include "components/viz/common/resources/shared_image_format.h"
#include "gpu/command_buffer/common/mailbox.h"
#include "gpu/command_buffer/common/shared_image_usage.h"
#include "gpu/command_buffer/service/shared_context_state.h"
#include "gpu/command_buffer/service/shared_image/shared_image_backing.h"
#include "gpu/command_buffer/service/shared_image/shared_image_factory.h"
#include "gpu/command_buffer/service/shared_image/shared_image_manager.h"
#include "gpu/command_buffer/service/shared_image/shared_image_representation.h"
#include "gpu/config/gpu_feature_info.h"
#include "media/base/video_frame.h"
#include "media/base/video_frame_layout.h"
#include "media/base/video_types.h"
#include "media/gpu/chromeos/chromeos_compressed_gpu_memory_buffer_video_frame_utils.h"
#include "media/gpu/chromeos/fourcc.h"
#include "media/gpu/chromeos/gl_image_processor_backend.h"
#include "media/gpu/chromeos/image_processor_backend.h"
#include "media/gpu/chromeos/image_processor_factory.h"
#include "media/gpu/chromeos/libyuv_image_processor_backend.h"
#include "media/gpu/chromeos/platform_video_frame_utils.h"
#include "media/gpu/chromeos/vulkan_image_processor.h"
#include "media/gpu/test/image.h"
#include "media/gpu/test/image_processor/image_processor_client.h"
#include "media/gpu/test/image_quality_metrics.h"
#include "media/gpu/test/video_frame_file_writer.h"
#include "media/gpu/test/video_frame_helpers.h"
#include "media/gpu/test/video_frame_validator.h"
#include "media/gpu/test/video_test_environment.h"
#if BUILDFLAG(USE_V4L2_CODEC)
#include "media/gpu/v4l2/v4l2_device.h"
#include "media/gpu/v4l2/v4l2_image_processor_backend.h"
#endif
#if BUILDFLAG(USE_VAAPI)
#include "media/gpu/vaapi/vaapi_image_processor_backend.h"
#endif
#include "media/gpu/video_frame_mapper.h"
#include "media/gpu/video_frame_mapper_factory.h"
#include "mojo/core/embedder/embedder.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/libyuv/include/libyuv.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/overlay_transform.h"
#include "ui/gl/gl_bindings.h"
#include "ui/gl/gl_context.h"
#include "ui/gl/gl_surface.h"
#include "ui/gl/gl_utils.h"
#include "ui/gl/init/gl_factory.h"
#include "ui/gl/test/gl_surface_test_support.h"

#if BUILDFLAG(IS_OZONE)
#include "ui/ozone/public/ozone_platform.h"
#endif

#define MM21_TILE_WIDTH 32u
#define MM21_TILE_HEIGHT 16u

namespace media {
namespace {

const char* usage_msg =
    "usage: image_processor_test\n"
    "[--gtest_help] [--help] [-v=<level>] [--vmodule=<config>] "
    "[--save_images] [--source_directory=<dir>]\n";

const char* help_msg =
    "Run the image processor tests.\n\n"
    "The following arguments are supported:\n"
    "  --gtest_help          display the gtest help and exit.\n"
    "  --help                display this help and exit.\n"
    "   -v                   enable verbose mode, e.g. -v=2.\n"
    "  --vmodule             enable verbose mode for the specified module.\n"
    "  --save_images         write images processed by a image processor to\n"
    "                        the \"<testname>\" folder.\n"
    "  --source_directory    specify the directory that contains test source\n"
    "                        files. Defaults to the current directory.\n"
#if defined(ARCH_CPU_ARM_FAMILY)
    "  --force_gl            use the GL image processor backend.\n"
#endif  // defined(ARCH_CPU_ARM_FAMILY)
    "  --force_libyuv        use the LibYUV image processor backend.\n"
#if BUILDFLAG(USE_V4L2_CODEC)
    "  --force_v4l2          use the V4L2 image processor backend.\n"
#endif  // BUILDFLAG(USE_V4L2_CODEC)
#if BUILDFLAG(USE_VAAPI)
    "  --force_vaapi         use the VA-API image processor backend.\n"
#endif  // BUILDFLAG(USE_VAAPI)
    ;

bool g_save_images = false;
base::FilePath g_source_directory =
    base::FilePath(base::FilePath::kCurrentDirectory);

// BackendType defines an enum for specifying a particular backend.
enum class BackendType {
#if defined(ARCH_CPU_ARM_FAMILY)
  kGL,
#endif  // defined(ARCH_CPU_ARM_FAMILY)
  kLibYUV,
#if BUILDFLAG(USE_V4L2_CODEC)
  kV4L2,
#endif  // BUILDFLAG(USE_V4L2_CODEC)
#if BUILDFLAG(USE_VAAPI)
  kVAAPI,
#endif  // BUILDFLAG(USE_VAAPI)
};

const char* ToString(BackendType backend) {
  switch (backend) {
#if defined(ARCH_CPU_ARM_FAMILY)
    case BackendType::kGL:
      return "GL";
#endif  // defined(ARCH_CPU_ARM_FAMILY)
    case BackendType::kLibYUV:
      return "LibYUV";
#if BUILDFLAG(USE_V4L2_CODEC)
    case BackendType::kV4L2:
      return "V4L2";
#endif  // BUILDFLAG(USE_V4L2_CODEC)
#if BUILDFLAG(USE_VAAPI)
    case BackendType::kVAAPI:
      return "VAAPI";
#endif  // BUILDFLAG(USE_VAAPI)
  }
}

// Creates a CreateBackendCB for the specified BackendType. If backend is not
// set, then returns std::nullopt.
std::optional<ImageProcessor::CreateBackendCB> GetCreateBackendCB(
    std::optional<BackendType> backend) {
  if (!backend) {
    return std::nullopt;
  }

  switch (*backend) {
#if defined(ARCH_CPU_ARM_FAMILY)
    case BackendType::kGL:
      return base::BindRepeating(&media::GLImageProcessorBackend::Create);
#endif  // defined(ARCH_CPU_ARM_FAMILY)
    case BackendType::kLibYUV:
      return base::BindRepeating(&media::LibYUVImageProcessorBackend::Create);
#if BUILDFLAG(USE_V4L2_CODEC)
    case BackendType::kV4L2:
      return base::BindRepeating(&media::V4L2ImageProcessorBackend::Create,
                                 base::MakeRefCounted<media::V4L2Device>(),
                                 /*num_buffers=*/1);
#endif  // BUILDFLAG(USE_V4L2_CODEC)
#if BUILDFLAG(USE_VAAPI)
    case BackendType::kVAAPI:
      return base::BindRepeating(&VaapiImageProcessorBackend::Create);
#endif  // BUILDFLAG(USE_VAAPI)
  }
}

std::optional<BackendType> g_backend_type;

base::FilePath BuildSourceFilePath(const base::FilePath& filename) {
  return media::g_source_directory.Append(filename);
}

media::test::VideoTestEnvironment* g_env;

// Files for pixel format conversion test.
const base::FilePath::CharType* kNV12Image =
    FILE_PATH_LITERAL("bear_320x192.nv12.yuv");
const base::FilePath::CharType* kYV12Image =
    FILE_PATH_LITERAL("bear_320x192.yv12.yuv");
const base::FilePath::CharType* kI420Image =
    FILE_PATH_LITERAL("bear_320x192.i420.yuv");
const base::FilePath::CharType* kI422Image =
    FILE_PATH_LITERAL("bear_320x192.i422.yuv");
const base::FilePath::CharType* kYUYVImage =
    FILE_PATH_LITERAL("bear_320x192.yuyv.yuv");

// Files for scaling test.
const base::FilePath::CharType* kNV12Image720P =
    FILE_PATH_LITERAL("puppets-1280x720.nv12.yuv");
const base::FilePath::CharType* kNV12Image360P =
    FILE_PATH_LITERAL("puppets-640x360.nv12.yuv");
const base::FilePath::CharType* kNV12Image270P =
    FILE_PATH_LITERAL("puppets-480x270.nv12.yuv");
const base::FilePath::CharType* kNV12Image180P =
    FILE_PATH_LITERAL("puppets-320x180.nv12.yuv");
const base::FilePath::CharType* kNV12Image360PIn480P =
    FILE_PATH_LITERAL("puppets-640x360_in_640x480.nv12.yuv");
const base::FilePath::CharType* kI422Image360P =
    FILE_PATH_LITERAL("puppets-640x360.i422.yuv");
const base::FilePath::CharType* kYUYVImage360P =
    FILE_PATH_LITERAL("puppets-640x360.yuyv.yuv");
const base::FilePath::CharType* kI420Image360P =
    FILE_PATH_LITERAL("puppets-640x360.i420.yuv");
const base::FilePath::CharType* kI420Image270P =
    FILE_PATH_LITERAL("puppets-480x270.i420.yuv");

#if BUILDFLAG(USE_V4L2_CODEC)
// File for MM21 detile and scaling test.
const base::FilePath::CharType* kMM21Image270P =
    FILE_PATH_LITERAL("puppets-480x270.mm21.yuv");

#if BUILDFLAG(ENABLE_VULKAN)
// Files for MT2T Vulkan detile test.
const base::FilePath::CharType* kMT2TImage =
    FILE_PATH_LITERAL("crowd_run_1080x512.mt2t");
const base::FilePath::CharType* kP010Image =
    FILE_PATH_LITERAL("crowd_run_1080x512.p010");
#endif
#endif

enum class YuvSubsampling {
  kYuv420,
  kYuv422,
  kYuv444,
};

YuvSubsampling ToYuvSubsampling(VideoPixelFormat format) {
  switch (format) {
    case PIXEL_FORMAT_I420:
    case PIXEL_FORMAT_NV12:
    case PIXEL_FORMAT_YV12:
      return YuvSubsampling::kYuv420;
    case PIXEL_FORMAT_I422:
    case PIXEL_FORMAT_YUY2:
      return YuvSubsampling::kYuv422;
    default:
      NOTREACHED_IN_MIGRATION() << "Invalid format " << format;
      return YuvSubsampling::kYuv444;
  }
}

#if BUILDFLAG(IS_CHROMEOS_ASH)
bool IsFormatTestedForDmabufAndGbm(VideoPixelFormat format) {
  switch (format) {
    case PIXEL_FORMAT_NV12:
    case PIXEL_FORMAT_YV12:
      return true;
    default:
      return false;
  }
}
#endif  // BUILDFLAG(IS_CHROMEOS_ASH)

#if BUILDFLAG(USE_V4L2_CODEC)
bool SupportsNecessaryGLExtension() {
  bool ret;

  scoped_refptr<gl::GLSurface> gl_surface =
      gl::init::CreateOffscreenGLSurface(gl::GetDefaultDisplay(), gfx::Size());
  if (!gl_surface) {
    LOG(ERROR) << "Error creating GL surface";
    return false;
  }
  scoped_refptr<gl::GLContext> gl_context = gl::init::CreateGLContext(
      nullptr, gl_surface.get(), gl::GLContextAttribs());
  if (!gl_context) {
    LOG(ERROR) << "Error creating GL context";
    return false;
  }
  if (!gl_context->MakeCurrent(gl_surface.get())) {
    LOG(ERROR) << "Error making GL context current";
    return false;
  }
  ret = gl_context->HasExtension("GL_EXT_YUV_target");
  gl_context->ReleaseCurrent(gl_surface.get());

  return ret;
}

scoped_refptr<VideoFrame> CreateNV12Frame(const gfx::Size& size,
                                          VideoFrame::StorageType type) {
  const gfx::Rect visible_rect(size);
  constexpr base::TimeDelta kNullTimestamp;
  if (type == VideoFrame::STORAGE_GPU_MEMORY_BUFFER) {
    return CreateGpuMemoryBufferVideoFrame(
        VideoPixelFormat::PIXEL_FORMAT_NV12, size, visible_rect, size,
        kNullTimestamp, gfx::BufferUsage::SCANOUT_CPU_READ_WRITE);
  } else {
    DCHECK(type == VideoFrame::STORAGE_DMABUFS);
    return CreatePlatformVideoFrame(VideoPixelFormat::PIXEL_FORMAT_NV12, size,
                                    visible_rect, size, kNullTimestamp,
                                    gfx::BufferUsage::SCANOUT_CPU_READ_WRITE);
  }
}

scoped_refptr<VideoFrame> CreateRandomMM21Frame(const gfx::Size& size,
                                                VideoFrame::StorageType type) {
  DCHECK_EQ(static_cast<unsigned int>(size.width()),
            base::bits::AlignUp(static_cast<unsigned int>(size.width()),
                                MM21_TILE_WIDTH));
  DCHECK_EQ(static_cast<unsigned int>(size.height()),
            base::bits::AlignUp(static_cast<unsigned int>(size.height()),
                                MM21_TILE_HEIGHT));

  scoped_refptr<VideoFrame> ret = CreateNV12Frame(size, type);
  if (!ret) {
    LOG(ERROR) << "Failed to create MM21 frame";
    return nullptr;
  }

  // The MM21 path only makes sense for V4L2, so we should never get an Intel
  // media compressed buffer here.
  CHECK(!IsIntelMediaCompressedModifier(ret->layout().modifier()));
  std::unique_ptr<VideoFrameMapper> frame_mapper =
      VideoFrameMapperFactory::CreateMapper(
          VideoPixelFormat::PIXEL_FORMAT_NV12, type,
          /*force_linear_buffer_mapper=*/true,
          /*must_support_intel_media_compressed_buffers=*/false);
  if (!frame_mapper) {
    LOG(ERROR) << "Unable to create a VideoFrameMapper";
    return nullptr;
  }
  scoped_refptr<VideoFrame> mapped_ret =
      frame_mapper->Map(ret, PROT_READ | PROT_WRITE);
  if (!mapped_ret) {
    LOG(ERROR) << "Unable to map MM21 frame";
    return nullptr;
  }

  uint8_t* y_plane = mapped_ret->GetWritableVisibleData(VideoFrame::Plane::kY);
  uint8_t* uv_plane =
      mapped_ret->GetWritableVisibleData(VideoFrame::Plane::kUV);
  for (int row = 0; row < size.height(); row++) {
    for (int col = 0; col < size.width(); col++) {
      y_plane[col] = base::RandInt(/*min=*/0, /*max=*/255);
      if (row % 2 == 0) {
        uv_plane[col] = base::RandInt(/*min=*/0, /*max=*/255);
      }
    }
    y_plane += mapped_ret->stride(VideoFrame::Plane::kY);
    if (row % 2 == 0) {
      uv_plane += mapped_ret->stride(VideoFrame::Plane::kUV);
    }
  }

  return ret;
}

bool CompareNV12VideoFrames(scoped_refptr<VideoFrame> test_frame,
                            scoped_refptr<VideoFrame> golden_frame) {
  if (test_frame->coded_size() != golden_frame->coded_size() ||
      test_frame->visible_rect() != golden_frame->visible_rect() ||
      test_frame->format() != VideoPixelFormat::PIXEL_FORMAT_NV12 ||
      golden_frame->format() != VideoPixelFormat::PIXEL_FORMAT_NV12) {
    return false;
  }

  // We run this test for the V4L2 path only, so we should never get Intel media
  // compressed frames here.
  CHECK(!IsIntelMediaCompressedModifier(test_frame->layout().modifier()));
  CHECK(!IsIntelMediaCompressedModifier(golden_frame->layout().modifier()));

  std::unique_ptr<VideoFrameMapper> test_frame_mapper =
      VideoFrameMapperFactory::CreateMapper(
          VideoPixelFormat::PIXEL_FORMAT_NV12, test_frame->storage_type(),
          /*force_linear_buffer_mapper=*/true,
          /*must_support_intel_media_compressed_buffers=*/false);
  if (!test_frame_mapper) {
    return false;
  }
  std::unique_ptr<VideoFrameMapper> golden_frame_mapper =
      VideoFrameMapperFactory::CreateMapper(
          VideoPixelFormat::PIXEL_FORMAT_NV12, golden_frame->storage_type(),
          /*force_linear_buffer_mapper=*/true,
          /*must_support_intel_media_compressed_buffers=*/false);
  if (!golden_frame_mapper) {
    return false;
  }
  scoped_refptr<VideoFrame> mapped_test_frame =
      test_frame_mapper->Map(test_frame, PROT_READ | PROT_WRITE);
  if (!mapped_test_frame) {
    LOG(ERROR) << "Unable to map test frame";
    return false;
  }
  scoped_refptr<VideoFrame> mapped_golden_frame =
      golden_frame_mapper->Map(golden_frame, PROT_READ | PROT_WRITE);
  if (!mapped_golden_frame) {
    LOG(ERROR) << "Unable to map golden frame";
    return false;
  }

  const uint8_t* test_y_plane =
      mapped_test_frame->visible_data(VideoFrame::Plane::kY);
  const uint8_t* test_uv_plane =
      mapped_test_frame->visible_data(VideoFrame::Plane::kUV);
  const uint8_t* golden_y_plane =
      mapped_golden_frame->visible_data(VideoFrame::Plane::kY);
  const uint8_t* golden_uv_plane =
      mapped_golden_frame->visible_data(VideoFrame::Plane::kUV);
  for (int y = 0; y < test_frame->coded_size().height(); y++) {
    for (int x = 0; x < test_frame->coded_size().width(); x++) {
      if (test_y_plane[x] != golden_y_plane[x]) {
        return false;
      }

      if (y % 2 == 0) {
        if (test_uv_plane[x] != golden_uv_plane[x]) {
          return false;
        }
      }
    }
    test_y_plane += mapped_test_frame->stride(VideoFrame::Plane::kY);
    golden_y_plane += mapped_golden_frame->stride(VideoFrame::Plane::kY);
    if (y % 2 == 0) {
      test_uv_plane += mapped_test_frame->stride(VideoFrame::Plane::kUV);
      golden_uv_plane += mapped_golden_frame->stride(VideoFrame::Plane::kUV);
    }
  }

  return true;
}
#endif

class ImageProcessorParamTest
    : public ::testing::Test,
      public ::testing::WithParamInterface<
          std::tuple<base::FilePath, base::FilePath>> {
 public:
  void SetUp() override {}
  void TearDown() override { model_frames_.clear(); }

  std::unique_ptr<test::ImageProcessorClient> CreateImageProcessorClient(
      const test::Image& input_image,
      VideoFrame::StorageType input_storage_type,
      test::Image* const output_image,
      VideoFrame::StorageType output_storage_type) {
    bool is_single_planar_input = true;
    bool is_single_planar_output = true;
#if defined(ARCH_CPU_ARM_FAMILY)
    // [Ugly Hack] In the use scenario of V4L2ImageProcessor in
    // V4L2VideoEncodeAccelerator. ImageProcessor needs to read and write
    // contents with arbitrary offsets. The format needs to be multi planar.
    is_single_planar_input = !IsYuvPlanar(input_image.PixelFormat());
    is_single_planar_output = !IsYuvPlanar(output_image->PixelFormat());
#endif

    Fourcc input_fourcc = *Fourcc::FromVideoPixelFormat(
        input_image.PixelFormat(), is_single_planar_input);
    Fourcc output_fourcc = *Fourcc::FromVideoPixelFormat(
        output_image->PixelFormat(), is_single_planar_output);

    auto input_layout = test::CreateVideoFrameLayout(input_image.PixelFormat(),
                                                     input_image.Size());
    auto output_layout = test::CreateVideoFrameLayout(
        output_image->PixelFormat(), output_image->Size());
    LOG_ASSERT(input_layout && output_layout);
    ImageProcessor::PortConfig input_config(
        input_fourcc, input_image.Size(), input_layout->planes(),
        input_image.VisibleRect(), input_storage_type);
    ImageProcessor::PortConfig output_config(
        output_fourcc, output_image->Size(), output_layout->planes(),
        output_image->VisibleRect(), output_storage_type);

    // TODO(crbug.com/917951): Select more appropriate number of buffers.
    constexpr size_t kNumBuffers = 1;
    LOG_ASSERT(output_image->IsMetadataLoaded());
    std::vector<std::unique_ptr<test::VideoFrameProcessor>> frame_processors;
    // TODO(crbug.com/944823): Use VideoFrameValidator for RGB formats.
    // Validating processed frames is currently not supported when a format is
    // not YUV or when scaling images.
    if (IsYuvPlanar(input_fourcc.ToVideoPixelFormat()) &&
        IsYuvPlanar(output_fourcc.ToVideoPixelFormat()) &&
        ToYuvSubsampling(input_fourcc.ToVideoPixelFormat()) ==
            ToYuvSubsampling(output_fourcc.ToVideoPixelFormat())) {
      if (input_image.Size() == output_image->Size()) {
        auto vf_validator = test::MD5VideoFrameValidator::Create(
            {output_image->Checksum()}, output_image->PixelFormat());
        LOG_ASSERT(vf_validator);
        frame_processors.push_back(std::move(vf_validator));
      } else if (input_fourcc == output_fourcc) {
        // Scaling case.
        LOG_ASSERT(output_image->Load());
        scoped_refptr<const VideoFrame> model_frame =
            CreateVideoFrameFromImage(*output_image);
        LOG_ASSERT(model_frame) << "Failed to create from image";
        model_frames_ = {model_frame};
        auto vf_validator = test::SSIMVideoFrameValidator::Create(
            base::BindRepeating(&ImageProcessorParamTest::GetModelFrame,
                                base::Unretained(this)));
        frame_processors.push_back(std::move(vf_validator));
      }
    }

    if (g_save_images) {
      base::FilePath output_dir =
          base::FilePath(base::FilePath::kCurrentDirectory)
              .Append(g_env->GetTestOutputFilePath());
      test::VideoFrameFileWriter::OutputFormat saved_file_format =
          IsYuvPlanar(output_fourcc.ToVideoPixelFormat())
              ? test::VideoFrameFileWriter::OutputFormat::kYUV
              : test::VideoFrameFileWriter::OutputFormat::kPNG;
      frame_processors.push_back(
          test::VideoFrameFileWriter::Create(output_dir, saved_file_format));
    }

    auto ip_client = test::ImageProcessorClient::Create(
        GetCreateBackendCB(g_backend_type), input_config, output_config,
        kNumBuffers, std::move(frame_processors));
    return ip_client;
  }

 private:
  scoped_refptr<const VideoFrame> GetModelFrame(size_t frame_index) const {
    if (frame_index >= model_frames_.size()) {
      LOG(ERROR) << "Failed to get model frame with index=" << frame_index;
      ADD_FAILURE();
      return nullptr;
    }
    return model_frames_[frame_index];
  }

  std::vector<scoped_refptr<const VideoFrame>> model_frames_;
};

TEST_P(ImageProcessorParamTest, ConvertOneTime_MemToMem) {
  // Load the test input image. We only need the output image's metadata so we
  // can compare checksums.
  test::Image input_image(BuildSourceFilePath(std::get<0>(GetParam())));
  test::Image output_image(BuildSourceFilePath(std::get<1>(GetParam())));
  ASSERT_TRUE(input_image.Load());
  ASSERT_TRUE(output_image.LoadMetadata());

  const bool is_scaling = (input_image.PixelFormat() == PIXEL_FORMAT_NV12 &&
                           output_image.PixelFormat() == PIXEL_FORMAT_NV12);
  const auto storage = is_scaling ? VideoFrame::STORAGE_GPU_MEMORY_BUFFER
                                  : VideoFrame::STORAGE_OWNED_MEMORY;
  auto ip_client =
      CreateImageProcessorClient(input_image, storage, &output_image, storage);
  if (!ip_client && g_backend_type.has_value()) {
    GTEST_SKIP() << "Forced backend " << ToString(*g_backend_type)
                 << " does not support this test";
  }
  ASSERT_TRUE(ip_client);

  ip_client->Process(input_image, output_image);

  EXPECT_TRUE(ip_client->WaitUntilNumImageProcessed(1u));
  EXPECT_EQ(ip_client->GetErrorCount(), 0u);
  EXPECT_EQ(ip_client->GetNumOfProcessedImages(), 1u);
  EXPECT_TRUE(ip_client->WaitForFrameProcessors());
}

#if BUILDFLAG(IS_CHROMEOS_ASH)
// We don't yet have the function to create Dmabuf-backed VideoFrame on
// platforms except ChromeOS. So MemToDmabuf test is limited on ChromeOS.
TEST_P(ImageProcessorParamTest, ConvertOneTime_DmabufToMem) {
  // Load the test input image. We only need the output image's metadata so we
  // can compare checksums.
  test::Image input_image(BuildSourceFilePath(std::get<0>(GetParam())));
  test::Image output_image(BuildSourceFilePath(std::get<1>(GetParam())));
  ASSERT_TRUE(input_image.Load());
  ASSERT_TRUE(output_image.LoadMetadata());
  if (!IsFormatTestedForDmabufAndGbm(input_image.PixelFormat()))
    GTEST_SKIP() << "Skipping Dmabuf format " << input_image.PixelFormat();
  const bool is_scaling = (input_image.PixelFormat() == PIXEL_FORMAT_NV12 &&
                           output_image.PixelFormat() == PIXEL_FORMAT_NV12);
  const auto storage = is_scaling ? VideoFrame::STORAGE_GPU_MEMORY_BUFFER
                                  : VideoFrame::STORAGE_OWNED_MEMORY;
  auto ip_client =
      CreateImageProcessorClient(input_image, storage, &output_image, storage);
  if (!ip_client && g_backend_type.has_value()) {
    GTEST_SKIP() << "Forced backend " << ToString(*g_backend_type)
                 << " does not support this test";
  }
  ASSERT_TRUE(ip_client);

  ip_client->Process(input_image, output_image);

  EXPECT_TRUE(ip_client->WaitUntilNumImageProcessed(1u));
  EXPECT_EQ(ip_client->GetErrorCount(), 0u);
  EXPECT_EQ(ip_client->GetNumOfProcessedImages(), 1u);
  EXPECT_TRUE(ip_client->WaitForFrameProcessors());
}

TEST_P(ImageProcessorParamTest, ConvertOneTime_DmabufToDmabuf) {
  // Load the test input image. We only need the output image's metadata so we
  // can compare checksums.
  test::Image input_image(BuildSourceFilePath(std::get<0>(GetParam())));
  test::Image output_image(BuildSourceFilePath(std::get<1>(GetParam())));
  ASSERT_TRUE(input_image.Load());
  ASSERT_TRUE(output_image.LoadMetadata());
  if (!IsFormatTestedForDmabufAndGbm(input_image.PixelFormat()))
    GTEST_SKIP() << "Skipping Dmabuf format " << input_image.PixelFormat();
  if (!IsFormatTestedForDmabufAndGbm(output_image.PixelFormat()))
    GTEST_SKIP() << "Skipping Dmabuf format " << output_image.PixelFormat();

  auto ip_client =
      CreateImageProcessorClient(input_image, VideoFrame::STORAGE_DMABUFS,
                                 &output_image, VideoFrame::STORAGE_DMABUFS);
  if (!ip_client && g_backend_type.has_value()) {
    GTEST_SKIP() << "Forced backend " << ToString(*g_backend_type)
                 << " does not support this test";
  }
  ASSERT_TRUE(ip_client);
  ip_client->Process(input_image, output_image);

  EXPECT_TRUE(ip_client->WaitUntilNumImageProcessed(1u));
  EXPECT_EQ(ip_client->GetErrorCount(), 0u);
  EXPECT_EQ(ip_client->GetNumOfProcessedImages(), 1u);
  EXPECT_TRUE(ip_client->WaitForFrameProcessors());
}

// Although GpuMemoryBuffer is a cross platform class, code for image processor
// test is designed only for ChromeOS. So this test runs on ChromeOS only.
TEST_P(ImageProcessorParamTest, ConvertOneTime_GmbToGmb) {
  // Load the test input image. We only need the output image's metadata so we
  // can compare checksums.
  test::Image input_image(BuildSourceFilePath(std::get<0>(GetParam())));
  test::Image output_image(BuildSourceFilePath(std::get<1>(GetParam())));
  ASSERT_TRUE(input_image.Load());
  ASSERT_TRUE(output_image.LoadMetadata());
  if (!IsFormatTestedForDmabufAndGbm(input_image.PixelFormat())) {
    GTEST_SKIP() << "Skipping GpuMemoryBuffer format "
                 << input_image.PixelFormat();
  }
  if (!IsFormatTestedForDmabufAndGbm(output_image.PixelFormat())) {
    GTEST_SKIP() << "Skipping GpuMemoryBuffer format "
                 << output_image.PixelFormat();
  }

  auto ip_client = CreateImageProcessorClient(
      input_image, VideoFrame::STORAGE_GPU_MEMORY_BUFFER, &output_image,
      VideoFrame::STORAGE_GPU_MEMORY_BUFFER);
  if (!ip_client && g_backend_type.has_value()) {
    GTEST_SKIP() << "Forced backend " << ToString(*g_backend_type)
                 << " does not support this test";
  }
  ASSERT_TRUE(ip_client);
  ip_client->Process(input_image, output_image);

  EXPECT_TRUE(ip_client->WaitUntilNumImageProcessed(1u));
  EXPECT_EQ(ip_client->GetErrorCount(), 0u);
  EXPECT_EQ(ip_client->GetNumOfProcessedImages(), 1u);
  EXPECT_TRUE(ip_client->WaitForFrameProcessors());
}
#endif  // BUILDFLAG(IS_CHROMEOS_ASH)

INSTANTIATE_TEST_SUITE_P(
    PixelFormatConversionToNV12,
    ImageProcessorParamTest,
    ::testing::Values(std::make_tuple(kI420Image, kNV12Image),
                      std::make_tuple(kYV12Image, kNV12Image),
                      std::make_tuple(kI422Image, kNV12Image),
                      std::make_tuple(kYUYVImage, kNV12Image)));

INSTANTIATE_TEST_SUITE_P(
    PixelFormatConversionToI420,
    ImageProcessorParamTest,
    ::testing::Values(std::make_tuple(kI420Image, kI420Image),
                      std::make_tuple(kI422Image, kI420Image),
                      std::make_tuple(kYUYVImage, kI420Image)));

INSTANTIATE_TEST_SUITE_P(
    NV12DownScaling,
    ImageProcessorParamTest,
    ::testing::Values(std::make_tuple(kNV12Image720P, kNV12Image360P),
                      std::make_tuple(kNV12Image720P, kNV12Image270P),
                      std::make_tuple(kNV12Image720P, kNV12Image180P),
                      std::make_tuple(kNV12Image360P, kNV12Image270P),
                      std::make_tuple(kNV12Image360P, kNV12Image180P)));

INSTANTIATE_TEST_SUITE_P(I420DownScaling,
                         ImageProcessorParamTest,
                         ::testing::Values(std::make_tuple(kI420Image360P,
                                                           kI420Image270P)));

INSTANTIATE_TEST_SUITE_P(
    DownScalingConversionToNV12,
    ImageProcessorParamTest,
    ::testing::Values(std::make_tuple(kI422Image360P, kNV12Image270P),
                      std::make_tuple(kYUYVImage360P, kNV12Image270P)));

INSTANTIATE_TEST_SUITE_P(
    DownScalingConversionToI420,
    ImageProcessorParamTest,
    ::testing::Values(std::make_tuple(kI420Image360P, kI420Image270P),
                      std::make_tuple(kI422Image360P, kI420Image270P),
                      std::make_tuple(kYUYVImage360P, kI420Image270P)));

// Crop 360P frame from 480P.
INSTANTIATE_TEST_SUITE_P(NV12Cropping,
                         ImageProcessorParamTest,
                         ::testing::Values(std::make_tuple(kNV12Image360PIn480P,
                                                           kNV12Image360P)));
// Crop 360p frame from 480P and scale the area to 270P.
INSTANTIATE_TEST_SUITE_P(NV12CroppingAndScaling,
                         ImageProcessorParamTest,
                         ::testing::Values(std::make_tuple(kNV12Image360PIn480P,
                                                           kNV12Image270P)));

#if BUILDFLAG(IS_CHROMEOS_ASH)
// TODO(hiroh): Add more tests.
// MEM->DMABUF (V4L2VideoEncodeAccelerator),
#endif

#if BUILDFLAG(USE_V4L2_CODEC)
TEST(ImageProcessorBackendTest, CompareLibYUVAndGLBackendsForMM21Image) {
  if (!SupportsNecessaryGLExtension()) {
    GTEST_SKIP() << "Skipping GL Backend test, unsupported platform.";
  }
  if (g_backend_type.has_value()) {
    GTEST_SKIP() << "Skipping test since a particular backend was specified in "
                    "the command line arguments.";
  }

  constexpr gfx::Size kTestImageSize(1920, 1088);
  constexpr gfx::Rect kTestImageVisibleRect(kTestImageSize);
  const ImageProcessor::PixelLayoutCandidate candidate = {Fourcc(Fourcc::MM21),
                                                          kTestImageSize};
  std::vector<ImageProcessor::PixelLayoutCandidate> candidates = {candidate};

  auto client_task_runner = base::SequencedTaskRunner::GetCurrentDefault();
  base::RunLoop run_loop;
  base::RepeatingClosure quit_closure = run_loop.QuitClosure();
  bool image_processor_error = false;
  ImageProcessor::ErrorCB error_cb = base::BindRepeating(
      [](scoped_refptr<base::SequencedTaskRunner> client_task_runner,
         base::RepeatingClosure quit_closure, bool* image_processor_error) {
        CHECK(client_task_runner->RunsTasksInCurrentSequence());
        *image_processor_error = true;
        quit_closure.Run();
      },
      client_task_runner, quit_closure, &image_processor_error);
  ImageProcessorFactory::PickFormatCB pick_format_cb = base::BindRepeating(
      [](const std::vector<Fourcc>&, std::optional<Fourcc>) {
        return std::make_optional<Fourcc>(Fourcc::NV12);
      });

  std::unique_ptr<ImageProcessor> libyuv_image_processor =
      ImageProcessorFactory::
          CreateLibYUVImageProcessorWithInputCandidatesForTesting(
              candidates, kTestImageVisibleRect, kTestImageSize,
              /*num_buffers=*/1, client_task_runner, pick_format_cb, error_cb);
  ASSERT_TRUE(libyuv_image_processor)
      << "Error creating LibYUV image processor";
  std::unique_ptr<ImageProcessor> gl_image_processor = ImageProcessorFactory::
      CreateGLImageProcessorWithInputCandidatesForTesting(
          candidates, kTestImageVisibleRect, kTestImageSize, /*num_buffers=*/1,
          client_task_runner, pick_format_cb, error_cb);
  ASSERT_TRUE(gl_image_processor) << "Error creating GLImageProcessor";

  scoped_refptr<VideoFrame> input_frame =
      CreateRandomMM21Frame(kTestImageSize, VideoFrame::STORAGE_DMABUFS);
  ASSERT_TRUE(input_frame) << "Error creating input frame";
  scoped_refptr<VideoFrame> gl_output_frame =
      CreateNV12Frame(kTestImageSize, VideoFrame::STORAGE_GPU_MEMORY_BUFFER);
  ASSERT_TRUE(gl_output_frame) << "Error creating GL output frame";
  scoped_refptr<VideoFrame> libyuv_output_frame =
      CreateNV12Frame(kTestImageSize, VideoFrame::STORAGE_GPU_MEMORY_BUFFER);
  ASSERT_TRUE(libyuv_output_frame) << "Error creating LibYUV output frame";

  int outstanding_processors = 2;
  ImageProcessor::FrameReadyCB libyuv_callback = base::BindOnce(
      [](scoped_refptr<base::SequencedTaskRunner> client_task_runner,
         base::RepeatingClosure quit_closure, int* outstanding_processors,
         scoped_refptr<VideoFrame>* libyuv_output_frame,
         scoped_refptr<VideoFrame> frame) {
        CHECK(client_task_runner->RunsTasksInCurrentSequence());
        *libyuv_output_frame = std::move(frame);
        if (!(--*outstanding_processors)) {
          quit_closure.Run();
        }
      },
      client_task_runner, quit_closure, &outstanding_processors,
      &libyuv_output_frame);

  ImageProcessor::FrameReadyCB gl_callback = base::BindOnce(
      [](scoped_refptr<base::SequencedTaskRunner> client_task_runner,
         base::RepeatingClosure quit_closure, int* outstanding_processors,
         scoped_refptr<VideoFrame>* gl_output_frame,
         scoped_refptr<VideoFrame> frame) {
        CHECK(client_task_runner->RunsTasksInCurrentSequence());
        *gl_output_frame = std::move(frame);
        if (!(--*outstanding_processors)) {
          quit_closure.Run();
        }
      },
      client_task_runner, quit_closure, &outstanding_processors,
      &gl_output_frame);

  libyuv_image_processor->Process(input_frame, libyuv_output_frame,
                                  std::move(libyuv_callback));
  gl_image_processor->Process(input_frame, gl_output_frame,
                              std::move(gl_callback));

  run_loop.Run();

  ASSERT_FALSE(image_processor_error);
  ASSERT_TRUE(libyuv_output_frame);
  ASSERT_TRUE(gl_output_frame);
  ASSERT_TRUE(CompareNV12VideoFrames(gl_output_frame, libyuv_output_frame));
}

#if BUILDFLAG(ENABLE_VULKAN)
TEST(ImageProcessorBackendTest, VulkanDetileScaleTest) {
  test::Image input_image(BuildSourceFilePath(base::FilePath(kMM21Image270P)));
  ASSERT_TRUE(input_image.Load());
  gfx::Size coded_size = input_image.Size();
  gfx::Rect visible_rect = input_image.VisibleRect();
  scoped_refptr<VideoFrame> mm21_frame = CreateNV12Frame(
      input_image.Size(), VideoFrame::STORAGE_GPU_MEMORY_BUFFER);

  // The MM21 path only makes sense for V4L2, so we should never get an Intel
  // media compressed buffer here.
  ASSERT_FALSE(IsIntelMediaCompressedModifier(mm21_frame->layout().modifier()));
  std::unique_ptr<VideoFrameMapper> frame_mapper =
      VideoFrameMapperFactory::CreateMapper(
          VideoPixelFormat::PIXEL_FORMAT_NV12,
          VideoFrame::STORAGE_GPU_MEMORY_BUFFER,
          /*force_linear_buffer_mapper=*/true,
          /*must_support_intel_media_compressed_buffers=*/false);
  scoped_refptr<VideoFrame> mapped_mm21_frame =
      frame_mapper->Map(mm21_frame, PROT_READ | PROT_WRITE);
  ASSERT_TRUE(mapped_mm21_frame);
  uint8_t* input_y_plane = (uint8_t*)mapped_mm21_frame->GetWritableVisibleData(
      VideoFrame::Plane::kY);
  uint8_t* input_uv_plane = (uint8_t*)mapped_mm21_frame->GetWritableVisibleData(
      VideoFrame::Plane::kUV);
  libyuv::NV12Copy(
      input_image.Data(), coded_size.width(),
      input_image.Data() + coded_size.GetArea(), coded_size.width(),
      input_y_plane, mapped_mm21_frame->stride(VideoFrame::Plane::kY),
      input_uv_plane, mapped_mm21_frame->stride(VideoFrame::Plane::kUV),
      coded_size.width(), coded_size.height());

  gfx::Size output_size(1000, 1000);
  gfx::Rect output_visible_rect(output_size);
  constexpr base::TimeDelta kNullTimestamp;
  Fourcc output_fourcc(Fourcc::AR24);
  scoped_refptr<VideoFrame> vulkan_output_frame =
      CreateGpuMemoryBufferVideoFrame(VideoPixelFormat::PIXEL_FORMAT_ARGB,
                                      output_size, output_visible_rect,
                                      output_size, kNullTimestamp,
                                      gfx::BufferUsage::SCANOUT_CPU_READ_WRITE);

  auto input_layout =
      test::CreateVideoFrameLayout(input_image.PixelFormat(), coded_size);
  auto output_layout = test::CreateVideoFrameLayout(
      output_fourcc.ToVideoPixelFormat(), output_size);
  auto in_gmb = CreateGpuMemoryBufferHandle(mm21_frame.get());
  auto out_gmb = CreateGpuMemoryBufferHandle(vulkan_output_frame.get());

  // Initialize shared image infrastructure.
  auto share_group = base::MakeRefCounted<gl::GLShareGroup>();
  auto surface =
      gl::init::CreateOffscreenGLSurface(gl::GetDefaultDisplay(), gfx::Size());
  auto context = gl::init::CreateGLContext(share_group.get(), surface.get(),
                                           gl::GLContextAttribs());
  context->MakeCurrent(surface.get());
  auto context_state = base::MakeRefCounted<gpu::SharedContextState>(
      share_group, surface, context, false, base::DoNothing(),
      gpu::GpuPreferences().gr_context_type);
  gpu::SharedImageManager shared_image_manager;
  gpu::GpuPreferences gpu_preferences;
  gpu::GpuDriverBugWorkarounds gpu_workarounds;
  gpu::GpuFeatureInfo gpu_info;
  gpu::SharedImageFactory shared_image_factory(
      gpu_preferences, gpu_workarounds, gpu_info, context_state.get(),
      &shared_image_manager, nullptr, false);

  // Wrap input and output frames in shared images.
  auto input_mailbox = gpu::Mailbox::Generate();
  auto output_mailbox = gpu::Mailbox::Generate();
  viz::SharedImageFormat format_nv12 = viz::SharedImageFormat::MultiPlane(
      viz::SharedImageFormat::PlaneConfig::kY_UV,
      viz::SharedImageFormat::Subsampling::k420,
      viz::SharedImageFormat::ChannelFormat::k8);
  format_nv12.SetPrefersExternalSampler();
  shared_image_factory.CreateSharedImage(
      input_mailbox, format_nv12, coded_size, gfx::ColorSpace::CreateSRGB(),
      kTopLeft_GrSurfaceOrigin, kOpaque_SkAlphaType,
      gpu::SharedImageUsage::SHARED_IMAGE_USAGE_DISPLAY_READ, "TestLabel",
      std::move(in_gmb));
  shared_image_factory.CreateSharedImage(
      output_mailbox, viz::SinglePlaneFormat::kBGRA_8888, coded_size,
      gfx::ColorSpace::CreateSRGB(), kTopLeft_GrSurfaceOrigin,
      kUnpremul_SkAlphaType,
      gpu::SharedImageUsage::SHARED_IMAGE_USAGE_DISPLAY_WRITE |
          gpu::SharedImageUsage::SHARED_IMAGE_USAGE_SCANOUT,
      "TestLabel", std::move(out_gmb));

  auto vulkan_image_processor =
      VulkanImageProcessor::Create(/*is_protected=*/false, kMM21);
  ASSERT_TRUE(vulkan_image_processor);

  auto input_vulkan_representation = shared_image_manager.ProduceVulkan(
      input_mailbox, nullptr, vulkan_image_processor->GetVulkanDeviceQueue(),
      vulkan_image_processor->GetVulkanImplementation(),
      /*needs_detiling=*/true);
  auto output_vulkan_representation = shared_image_manager.ProduceVulkan(
      output_mailbox, nullptr, vulkan_image_processor->GetVulkanDeviceQueue(),
      vulkan_image_processor->GetVulkanImplementation(),
      /*needs_detiling=*/true);
  {
    std::vector<VkSemaphore> begin_semaphores;
    std::vector<VkSemaphore> end_semaphores;
    auto input_access = input_vulkan_representation->BeginScopedAccess(
        gpu::RepresentationAccessMode::kRead, begin_semaphores, end_semaphores);
    auto output_access = output_vulkan_representation->BeginScopedAccess(
        gpu::RepresentationAccessMode::kWrite, begin_semaphores,
        end_semaphores);

    // TODO(b/251458823): Add tests for more interesting crop and rotation
    // parameters.
    vulkan_image_processor->Process(
        input_access->GetVulkanImage(), visible_rect.size(),
        output_access->GetVulkanImage(),
        gfx::RectF(static_cast<float>(output_size.width()),
                   static_cast<float>(output_size.height())),
        gfx::RectF(1.0f, 1.0f), gfx::OVERLAY_TRANSFORM_NONE, begin_semaphores,
        end_semaphores);
  }

  // This implicitly waits for all semaphores to signal.
  vulkan_image_processor->GetVulkanDeviceQueue()
      ->GetFenceHelper()
      ->PerformImmediateCleanup();

  // Replicate this operation using LibYUV. Note that we don't use the image
  // processor since we need to do a very specific conversion and scale
  // operation that the LibYUV image processor backend doesn't support.
  size_t i420_y_size = visible_rect.size().GetArea();
  size_t i420_u_v_size =
      ((visible_rect.width() + 1) / 2) * ((visible_rect.height() + 1) / 2);
  uint8_t* i420_y = (uint8_t*)mmap(nullptr, i420_y_size, PROT_READ | PROT_WRITE,
                                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  uint8_t* i420_u =
      (uint8_t*)mmap(nullptr, i420_u_v_size, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  uint8_t* i420_v =
      (uint8_t*)mmap(nullptr, i420_u_v_size, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  libyuv::MM21ToI420(
      input_image.Data(), coded_size.width(),
      input_image.Data() + coded_size.GetArea(),
      ((coded_size.width() + 1) & (~1)), i420_y, visible_rect.width(), i420_u,
      (visible_rect.width() + 1) / 2, i420_v, (visible_rect.width() + 1) / 2,
      visible_rect.width(), visible_rect.height());
  size_t i420_scaled_y_size = output_size.GetArea();
  size_t i420_scaled_u_v_size =
      ((output_size.width() + 1) / 2) * ((output_size.width() + 1) / 2);
  uint8_t* i420_scaled_y =
      (uint8_t*)mmap(nullptr, i420_scaled_y_size, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  uint8_t* i420_scaled_u =
      (uint8_t*)mmap(nullptr, i420_scaled_u_v_size, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  uint8_t* i420_scaled_v =
      (uint8_t*)mmap(nullptr, i420_scaled_u_v_size, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  libyuv::I420Scale(i420_y, visible_rect.width(), i420_u,
                    (visible_rect.width() + 1) / 2, i420_v,
                    (visible_rect.width() + 1) / 2, visible_rect.width(),
                    visible_rect.height(), i420_scaled_y, output_size.width(),
                    i420_scaled_u, (output_size.width() + 1) / 2, i420_scaled_v,
                    (output_size.height() + 1) / 2, output_size.width(),
                    output_size.height(), libyuv::kFilterBilinear);
  munmap(i420_y, i420_y_size);
  munmap(i420_u, i420_u_v_size);
  munmap(i420_v, i420_u_v_size);

  // Convert the vulkan frame into I420. We really want to scan this out as
  // ARGB since that's what the display controller supports, but the LibYUV
  // PSNR calculator only takes I420.
  uint8_t* vulkan_output_y =
      (uint8_t*)mmap(nullptr, i420_scaled_y_size, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  uint8_t* vulkan_output_u =
      (uint8_t*)mmap(nullptr, i420_scaled_u_v_size, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  uint8_t* vulkan_output_v =
      (uint8_t*)mmap(nullptr, i420_scaled_u_v_size, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

  // The MM21 de-tiling path only makes sense for V4L2, so we should never get
  // an Intel media compressed buffer here.
  ASSERT_FALSE(
      IsIntelMediaCompressedModifier(vulkan_output_frame->layout().modifier()));
  std::unique_ptr<VideoFrameMapper> output_frame_mapper =
      VideoFrameMapperFactory::CreateMapper(
          VideoPixelFormat::PIXEL_FORMAT_ARGB,
          VideoFrame::STORAGE_GPU_MEMORY_BUFFER,
          /*force_linear_buffer_mapper=*/true,
          /*must_support_intel_media_compressed_buffers=*/false);
  scoped_refptr<VideoFrame> mapped_output_frame =
      output_frame_mapper->Map(vulkan_output_frame, PROT_READ | PROT_WRITE);
  const uint8_t* argb_plane =
      mapped_output_frame->visible_data(VideoFrame::Plane::kARGB);
  libyuv::ARGBToI420(
      argb_plane, mapped_output_frame->stride(VideoFrame::Plane::kARGB),
      vulkan_output_y, output_size.width(), vulkan_output_u,
      (output_size.width() + 1) / 2, vulkan_output_v,
      (output_size.width() + 1) / 2, output_size.width(), output_size.height());

  // Calculate PSNR. Note that we can't just do a byte by byte comparison
  // because floating point inacuracies in both the color conversion and the
  // bilinear filtering will result in small differences between these two
  // methods.
  double psnr = libyuv::I420Psnr(
      i420_scaled_y, output_size.width(), i420_scaled_u,
      (output_size.width() + 1) / 2, i420_scaled_v,
      (output_size.width() + 1) / 2, vulkan_output_y, output_size.width(),
      vulkan_output_u, (output_size.width() + 1) / 2, vulkan_output_v,
      (output_size.width() + 1) / 2, output_size.width(), output_size.height());
  constexpr double kPsnrThreshold = 35.0;
  ASSERT_TRUE(psnr >= kPsnrThreshold);

  munmap(i420_scaled_y, i420_scaled_y_size);
  munmap(i420_scaled_u, i420_scaled_u_v_size);
  munmap(i420_scaled_v, i420_scaled_u_v_size);
  munmap(vulkan_output_y, i420_scaled_y_size);
  munmap(vulkan_output_u, i420_scaled_u_v_size);
  munmap(vulkan_output_v, i420_scaled_u_v_size);
}

TEST(ImageProcessorBackendTest, VulkanMT2TDetileScaleTest) {
  constexpr size_t kBppNumerator = 5;
  constexpr size_t kBppDenom = 4;

  test::Image input_image(BuildSourceFilePath(base::FilePath(kMT2TImage)));
  ASSERT_TRUE(input_image.Load());
  test::Image golden_image(BuildSourceFilePath(base::FilePath(kP010Image)));
  ASSERT_TRUE(golden_image.Load());

  gfx::Rect visible_rect = input_image.VisibleRect();
  gfx::Size output_size = gfx::Size(1000, 1000);
  gfx::Size coded_size = gfx::Size(
      base::bits::AlignUp(static_cast<unsigned int>(visible_rect.width()),
                          MM21_TILE_WIDTH),
      base::bits::AlignUp(static_cast<unsigned int>(visible_rect.height()),
                          MM21_TILE_HEIGHT));

  scoped_refptr<VideoFrame> mt2t_frame = CreateNV12Frame(
      gfx::Size(coded_size.width(),
                coded_size.height() * kBppNumerator / kBppDenom),
      VideoFrame::STORAGE_GPU_MEMORY_BUFFER);
  ASSERT_TRUE(mt2t_frame);

  ASSERT_FALSE(IsIntelMediaCompressedModifier(mt2t_frame->layout().modifier()));
  std::unique_ptr<VideoFrameMapper> frame_mapper =
      VideoFrameMapperFactory::CreateMapper(
          VideoPixelFormat::PIXEL_FORMAT_NV12,
          VideoFrame::STORAGE_GPU_MEMORY_BUFFER,
          /*force_linear_buffer_mapper=*/true,
          /*must_support_intel_media_compressed_buffers=*/false);
  scoped_refptr<VideoFrame> mapped_mt2t_frame =
      frame_mapper->Map(mt2t_frame, PROT_READ | PROT_WRITE);
  ASSERT_TRUE(mapped_mt2t_frame);
  memcpy(mapped_mt2t_frame->GetWritableVisibleData(VideoFrame::Plane::kY),
         input_image.Data(), mt2t_frame->coded_size().GetArea());
  memcpy(mapped_mt2t_frame->GetWritableVisibleData(VideoFrame::Plane::kUV),
         input_image.Data() + mt2t_frame->coded_size().GetArea(),
         mt2t_frame->coded_size().GetArea() / 2);

  constexpr base::TimeDelta kNullTimestamp;
  scoped_refptr<VideoFrame> vulkan_output_frame =
      CreateGpuMemoryBufferVideoFrame(
          VideoPixelFormat::PIXEL_FORMAT_XR30, output_size,
          gfx::Rect(output_size.width(), output_size.height()), output_size,
          kNullTimestamp, gfx::BufferUsage::SCANOUT_CPU_READ_WRITE);
  ASSERT_TRUE(vulkan_output_frame);

  auto in_gmb = CreateGpuMemoryBufferHandle(mt2t_frame.get());
  auto out_gmb = CreateGpuMemoryBufferHandle(vulkan_output_frame.get());

  // Initialize shared image infrastructure.
  auto share_group = base::MakeRefCounted<gl::GLShareGroup>();
  auto surface =
      gl::init::CreateOffscreenGLSurface(gl::GetDefaultDisplay(), gfx::Size());
  auto context = gl::init::CreateGLContext(share_group.get(), surface.get(),
                                           gl::GLContextAttribs());
  context->MakeCurrent(surface.get());
  auto context_state = base::MakeRefCounted<gpu::SharedContextState>(
      share_group, surface, context, false, base::DoNothing(),
      gpu::GpuPreferences().gr_context_type);
  gpu::SharedImageManager shared_image_manager;
  gpu::GpuPreferences gpu_preferences;
  gpu::GpuDriverBugWorkarounds gpu_workarounds;
  gpu::GpuFeatureInfo gpu_info;
  gpu::SharedImageFactory shared_image_factory(
      gpu_preferences, gpu_workarounds, gpu_info, context_state.get(),
      &shared_image_manager, nullptr, false);

  // Wrap input and output frames in shared images.
  auto input_mailbox = gpu::Mailbox::Generate();
  auto output_mailbox = gpu::Mailbox::Generate();
  viz::SharedImageFormat format_nv12 = viz::SharedImageFormat::MultiPlane(
      viz::SharedImageFormat::PlaneConfig::kY_UV,
      viz::SharedImageFormat::Subsampling::k420,
      viz::SharedImageFormat::ChannelFormat::k8);
  format_nv12.SetPrefersExternalSampler();
  shared_image_factory.CreateSharedImage(
      input_mailbox, format_nv12, mt2t_frame->coded_size(),
      gfx::ColorSpace::CreateSRGB(), kTopLeft_GrSurfaceOrigin,
      kOpaque_SkAlphaType,
      gpu::SharedImageUsage::SHARED_IMAGE_USAGE_DISPLAY_READ, "TestLabel",
      std::move(in_gmb));
  shared_image_factory.CreateSharedImage(
      output_mailbox, viz::SinglePlaneFormat::kBGRA_1010102, output_size,
      gfx::ColorSpace::CreateSRGB(), kTopLeft_GrSurfaceOrigin,
      kUnpremul_SkAlphaType,
      gpu::SharedImageUsage::SHARED_IMAGE_USAGE_DISPLAY_WRITE |
          gpu::SharedImageUsage::SHARED_IMAGE_USAGE_SCANOUT,
      "TestLabel", std::move(out_gmb));

  auto vulkan_image_processor =
      VulkanImageProcessor::Create(/*is_protected=*/false, kMT2T);
  ASSERT_TRUE(vulkan_image_processor);

  auto input_vulkan_representation = shared_image_manager.ProduceVulkan(
      input_mailbox, nullptr, vulkan_image_processor->GetVulkanDeviceQueue(),
      vulkan_image_processor->GetVulkanImplementation(),
      /*needs_detiling=*/true);
  auto output_vulkan_representation = shared_image_manager.ProduceVulkan(
      output_mailbox, nullptr, vulkan_image_processor->GetVulkanDeviceQueue(),
      vulkan_image_processor->GetVulkanImplementation(),
      /*needs_detiling=*/true);
  {
    std::vector<VkSemaphore> begin_semaphores;
    std::vector<VkSemaphore> end_semaphores;
    auto input_access = input_vulkan_representation->BeginScopedAccess(
        gpu::RepresentationAccessMode::kRead, begin_semaphores, end_semaphores);
    auto output_access = output_vulkan_representation->BeginScopedAccess(
        gpu::RepresentationAccessMode::kWrite, begin_semaphores,
        end_semaphores);

    // TODO(b/251458823): Add tests for more interesting crop and rotation
    // parameters.
    vulkan_image_processor->Process(
        input_access->GetVulkanImage(), visible_rect.size(),
        output_access->GetVulkanImage(),
        gfx::RectF(static_cast<float>(output_size.width()),
                   static_cast<float>(output_size.height())),
        gfx::RectF(1.0f, 1.0f), gfx::OVERLAY_TRANSFORM_NONE, begin_semaphores,
        end_semaphores);
  }

  // This implicitly waits for all semaphores to signal.
  vulkan_image_processor->GetVulkanDeviceQueue()
      ->GetFenceHelper()
      ->PerformImmediateCleanup();

  // Map output frame.
  std::unique_ptr<VideoFrameMapper> output_frame_mapper =
      VideoFrameMapperFactory::CreateMapper(
          VideoPixelFormat::PIXEL_FORMAT_XR30,
          VideoFrame::STORAGE_GPU_MEMORY_BUFFER,
          /*force_linear_buffer_mapper=*/true,
          /*must_support_intel_media_compressed_buffers=*/false);
  scoped_refptr<VideoFrame> mapped_output_frame =
      output_frame_mapper->Map(vulkan_output_frame, PROT_READ | PROT_WRITE);

  // Replicate this operation with LibYUV.
  uint16_t* i010_y = reinterpret_cast<uint16_t*>(
      mmap(nullptr, visible_rect.size().GetArea() * 2, PROT_READ | PROT_WRITE,
           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
  uint16_t* i010_u = reinterpret_cast<uint16_t*>(
      mmap(nullptr, visible_rect.size().GetArea() / 4 * 2,
           PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
  uint16_t* i010_v = reinterpret_cast<uint16_t*>(
      mmap(nullptr, visible_rect.size().GetArea() / 4 * 2,
           PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
  libyuv::P010ToI010(reinterpret_cast<uint16_t*>(golden_image.Data()),
                     visible_rect.width(),
                     reinterpret_cast<uint16_t*>(golden_image.Data()) +
                         visible_rect.size().GetArea(),
                     visible_rect.width(), i010_y, visible_rect.width(), i010_u,
                     visible_rect.width() / 2, i010_v, visible_rect.width() / 2,
                     visible_rect.width(), visible_rect.height());
  uint16_t* scaled_i010_y = reinterpret_cast<uint16_t*>(
      mmap(nullptr, output_size.GetArea() * 2, PROT_READ | PROT_WRITE,
           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
  uint16_t* scaled_i010_u = reinterpret_cast<uint16_t*>(
      mmap(nullptr, output_size.GetArea() / 4 * 2, PROT_READ | PROT_WRITE,
           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
  uint16_t* scaled_i010_v = reinterpret_cast<uint16_t*>(
      mmap(nullptr, output_size.GetArea() / 4 * 2, PROT_READ | PROT_WRITE,
           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
  libyuv::I420Scale_16(
      i010_y, visible_rect.width(), i010_u, visible_rect.width() / 2, i010_v,
      visible_rect.width() / 2, visible_rect.width(), visible_rect.height(),
      scaled_i010_y, output_size.width(), scaled_i010_u,
      output_size.width() / 2, scaled_i010_v, output_size.width() / 2,
      output_size.width(), output_size.height(), libyuv::kFilterBilinear);
  uint32_t* libyuv_output = reinterpret_cast<uint32_t*>(
      mmap(nullptr, output_size.GetArea() * 4, PROT_READ | PROT_WRITE,
           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
  libyuv::I010ToAR30(
      scaled_i010_y, output_size.width(), scaled_i010_u,
      output_size.width() / 2, scaled_i010_v, output_size.width() / 2,
      reinterpret_cast<uint8_t*>(libyuv_output), output_size.width() * 4,
      output_size.width(), output_size.height());

  double psnr = test::ComputeAR30PSNR(
      reinterpret_cast<const uint32_t*>(
          mapped_output_frame->visible_data(VideoFrame::Plane::kARGB)),
      mapped_output_frame->stride(VideoFrame::Plane::kARGB) / 4, libyuv_output,
      output_size.width(), output_size.width(), output_size.height());

  // TODO(b/328227651): We have to keep this PSNR threshold pretty low because
  // LibYUV produces inaccurate results in the 10-bit YUV->ARGB conversion. We
  // should try to fix this discrepancy though.
  constexpr double kPsnrThreshold = 25.0;
  ASSERT_TRUE(psnr >= kPsnrThreshold);

  munmap(libyuv_output, output_size.GetArea() * 4);
  munmap(i010_y, visible_rect.size().GetArea() * 2);
  munmap(i010_u, visible_rect.size().GetArea() / 4 * 2);
  munmap(i010_v, visible_rect.size().GetArea() / 4 * 2);
  munmap(scaled_i010_y, output_size.GetArea() * 2);
  munmap(scaled_i010_u, output_size.GetArea() / 4 * 2);
  munmap(scaled_i010_v, output_size.GetArea() / 4 * 2);
}
#endif
#endif

}  // namespace
}  // namespace media

// Argument handler for setting a forced ImageProcessor backend
static int HandleForcedBackendArgument(const std::string& arg,
                                       media::BackendType type) {
  if (media::g_backend_type.has_value() && *media::g_backend_type != type) {
    std::cout << "error argument --" << arg
              << " is invalid. ImageProcessor backend was already set to "
              << media::ToString(*media::g_backend_type) << std::endl;
    return EXIT_FAILURE;
  }
  media::g_backend_type = type;
  return 0;
}

int main(int argc, char** argv) {
  base::CommandLine::Init(argc, argv);

  // Print the help message if requested. This needs to be done before
  // initializing gtest, to overwrite the default gtest help message.
  const base::CommandLine* cmd_line = base::CommandLine::ForCurrentProcess();
  LOG_ASSERT(cmd_line);
  if (cmd_line->HasSwitch("help")) {
    std::cout << media::usage_msg << "\n" << media::help_msg;
    return 0;
  }

  base::CommandLine::SwitchMap switches = cmd_line->GetSwitches();
  for (base::CommandLine::SwitchMap::const_iterator it = switches.begin();
       it != switches.end(); ++it) {
    if (it->first.find("gtest_") == 0 ||               // Handled by GoogleTest
        it->first == "v" || it->first == "vmodule") {  // Handled by Chrome
      continue;
    }

    if (it->first == "save_images") {
      media::g_save_images = true;
    } else if (it->first == "source_directory") {
      media::g_source_directory = base::FilePath(it->second);
#if defined(ARCH_CPU_ARM_FAMILY)
    } else if (it->first == "force_gl") {
      if (int ret =
              HandleForcedBackendArgument(it->first, media::BackendType::kGL)) {
        return ret;
      }
#endif  // defined(ARCH_CPU_ARM_FAMILY)
    } else if (it->first == "force_libyuv") {
      if (int ret = HandleForcedBackendArgument(it->first,
                                                media::BackendType::kLibYUV)) {
        return ret;
      }
#if BUILDFLAG(USE_V4L2_CODEC)
    } else if (it->first == "force_v4l2") {
      if (int ret = HandleForcedBackendArgument(it->first,
                                                media::BackendType::kV4L2)) {
        return ret;
      }
#endif  // BUILDFLAG(USE_V4L2_CODEC)
#if BUILDFLAG(USE_VAAPI)
    } else if (it->first == "force_vaapi") {
      if (int ret = HandleForcedBackendArgument(it->first,
                                                media::BackendType::kVAAPI)) {
        return ret;
      }
#endif  // BUILDFLAG(USE_VAAPI)
    } else {
      std::cout << "unknown option: --" << it->first << "\n"
                << media::usage_msg;
      return EXIT_FAILURE;
    }
  }

  testing::InitGoogleTest(&argc, argv);

  auto* const test_environment = new media::test::VideoTestEnvironment;
  media::g_env = reinterpret_cast<media::test::VideoTestEnvironment*>(
      testing::AddGlobalTestEnvironment(test_environment));

// TODO(b/316374371) Try to remove Ozone and replace with EGL and GL.
#if BUILDFLAG(IS_OZONE)
  ui::OzonePlatform::InitParams ozone_param;
  ozone_param.single_process = true;
#if BUILDFLAG(ENABLE_VULKAN) && BUILDFLAG(USE_V4L2_CODEC)
  ui::OzonePlatform::InitializeForUI(ozone_param);
#endif
  ui::OzonePlatform::InitializeForGPU(ozone_param);
#endif

#if BUILDFLAG(USE_V4L2_CODEC)
  gl::GLSurfaceTestSupport::InitializeOneOffImplementation(
      gl::GLImplementationParts(gl::kGLImplementationEGLGLES2),
      /*fallback_to_software_gl=*/false);
#endif

  return RUN_ALL_TESTS();
}
