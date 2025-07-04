// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/video/renderable_gpu_memory_buffer_video_frame_pool.h"

#include "base/functional/callback_helpers.h"
#include "base/memory/weak_ptr.h"
#include "base/task/thread_pool.h"
#include "base/test/bind.h"
#include "base/test/scoped_feature_list.h"
#include "base/test/task_environment.h"
#include "components/viz/common/resources/shared_image_format.h"
#include "components/viz/common/resources/shared_image_format_utils.h"
#include "components/viz/test/test_context_provider.h"
#include "gpu/command_buffer/client/client_shared_image.h"
#include "gpu/config/gpu_finch_features.h"
#include "media/base/format_utils.h"
#include "media/base/media_switches.h"
#include "media/base/video_frame.h"
#include "media/video/fake_gpu_memory_buffer.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using ::testing::_;

namespace media {

namespace {

gfx::ColorSpace GetColorSpaceForPixelFormat(media::VideoPixelFormat format) {
  switch (format) {
    case media::PIXEL_FORMAT_NV12:
      return gfx::ColorSpace::CreateREC709();
    case media::PIXEL_FORMAT_ARGB:
    case media::PIXEL_FORMAT_ABGR:
      return gfx::ColorSpace::CreateSRGB();
    default:
      NOTREACHED_NORETURN();
  }
}

class FakeContext : public RenderableGpuMemoryBufferVideoFramePool::Context {
 public:
  FakeContext()
      : context_provider_(viz::TestContextProvider::Create()),
        weak_factory_(this) {}
  ~FakeContext() override = default;

  std::unique_ptr<gfx::GpuMemoryBuffer> CreateGpuMemoryBuffer(
      const gfx::Size& size,
      gfx::BufferFormat format,
      gfx::BufferUsage usage) override {
    DoCreateGpuMemoryBuffer(size, format);
    return std::make_unique<FakeGpuMemoryBuffer>(size, format);
  }
  scoped_refptr<gpu::ClientSharedImage> CreateSharedImage(
      gfx::GpuMemoryBuffer* gpu_memory_buffer,
      const viz::SharedImageFormat& si_format,
      const gfx::ColorSpace& color_space,
      GrSurfaceOrigin surface_origin,
      SkAlphaType alpha_type,
      uint32_t usage,
      gpu::SyncToken& sync_token) override {
    DoCreateSharedImage(si_format, gpu_memory_buffer->GetSize(), color_space,
                        surface_origin, alpha_type, usage,
                        gpu_memory_buffer->CloneHandle());
    return context_provider_->SharedImageInterface()->CreateSharedImage(
        {si_format, gpu_memory_buffer->GetSize(), color_space, surface_origin,
         alpha_type, usage, "RenderableGpuMemoryBufferVideoFramePoolTest"},
        gpu_memory_buffer->CloneHandle());
  }
  scoped_refptr<gpu::ClientSharedImage> CreateSharedImage(
      gfx::GpuMemoryBuffer* gpu_memory_buffer,
      gfx::BufferPlane plane,
      const gfx::ColorSpace& color_space,
      GrSurfaceOrigin surface_origin,
      SkAlphaType alpha_type,
      uint32_t usage,
      gpu::SyncToken& sync_token) override {
    DoCreateSharedImage(gpu_memory_buffer, plane, color_space, surface_origin,
                        alpha_type, usage);
    return context_provider_->SharedImageInterface()->CreateSharedImage(
        gpu_memory_buffer, /*gpu_memory_buffer_manager=*/nullptr, plane,
        {color_space, surface_origin, alpha_type, usage,
         "RenderableGpuMemoryBufferVideoFramePoolTest"});
  }

  MOCK_METHOD2(DoCreateGpuMemoryBuffer,
               void(const gfx::Size& size, gfx::BufferFormat format));
  MOCK_METHOD7(DoCreateSharedImage,
               void(viz::SharedImageFormat format,
                    const gfx::Size& size,
                    const gfx::ColorSpace& color_space,
                    GrSurfaceOrigin surface_origin,
                    SkAlphaType alpha_type,
                    uint32_t usage,
                    gfx::GpuMemoryBufferHandle buffer_handle));
  MOCK_METHOD6(DoCreateSharedImage,
               void(gfx::GpuMemoryBuffer* gpu_memory_buffer,
                    gfx::BufferPlane plane,
                    const gfx::ColorSpace& color_space,
                    GrSurfaceOrigin surface_origin,
                    SkAlphaType alpha_type,
                    uint32_t usage));
  MOCK_METHOD2(DestroySharedImage,
               void(const gpu::SyncToken& sync_token,
                    scoped_refptr<gpu::ClientSharedImage> shared_image));

  base::WeakPtr<FakeContext> GetWeakPtr() { return weak_factory_.GetWeakPtr(); }

 private:
  scoped_refptr<viz::TestContextProvider> context_provider_;
  base::WeakPtrFactory<FakeContext> weak_factory_;
};

class RenderableGpuMemoryBufferVideoFramePoolTest
    : public testing::TestWithParam<VideoPixelFormat> {
 public:
  RenderableGpuMemoryBufferVideoFramePoolTest() : format_(GetParam()) {}

 protected:
  void VerifySharedImageCreation(FakeContext* context) {
    switch (format_) {
      case PIXEL_FORMAT_NV12: {
        EXPECT_CALL(*context, DoCreateSharedImage(viz::MultiPlaneFormat::kNV12,
                                                  _, _, _, _, _, _));
        break;
      }
      case PIXEL_FORMAT_ARGB: {
        EXPECT_CALL(*context,
                    DoCreateSharedImage(viz::SinglePlaneFormat::kBGRA_8888, _,
                                        _, _, _, _, _));
        break;
      }
      case PIXEL_FORMAT_ABGR: {
        EXPECT_CALL(*context,
                    DoCreateSharedImage(viz::SinglePlaneFormat::kRGBA_8888, _,
                                        _, _, _, _, _));
        break;
      }
      default: {
        NOTREACHED_NORETURN();
      }
    }
  }

  int NumSharedImagesPerFrame() {
    switch (format_) {
      case PIXEL_FORMAT_NV12:
      case PIXEL_FORMAT_ABGR:
      case PIXEL_FORMAT_ARGB: {
        return 1;
      }
      default: {
        NOTREACHED_NORETURN();
      }
    }
  }

  VideoPixelFormat format_;

  base::test::ScopedFeatureList scoped_feature_list_;
};

TEST_P(RenderableGpuMemoryBufferVideoFramePoolTest, SimpleLifetimes) {
  base::test::SingleThreadTaskEnvironment task_environment;
  const gfx::Size size0(128, 256);
  const gfx::BufferFormat format =
      VideoPixelFormatToGfxBufferFormat(format_).value();
  const gfx::ColorSpace color_space0 = GetColorSpaceForPixelFormat(format_);

  base::WeakPtr<FakeContext> context;
  std::unique_ptr<RenderableGpuMemoryBufferVideoFramePool> pool;
  {
    auto context_strong = std::make_unique<FakeContext>();
    context = context_strong->GetWeakPtr();
    pool = RenderableGpuMemoryBufferVideoFramePool::Create(
        std::move(context_strong), format_);
  }

  // Create a new frame.
  EXPECT_CALL(*context, DoCreateGpuMemoryBuffer(size0, format));
  VerifySharedImageCreation(context.get());
  auto video_frame0 = pool->MaybeCreateVideoFrame(size0, color_space0);
  video_frame0 = nullptr;
  task_environment.RunUntilIdle();

  // Expect the frame to be reused.
  EXPECT_CALL(*context, DoCreateGpuMemoryBuffer(size0, format)).Times(0);
  EXPECT_CALL(*context, DoCreateSharedImage(_, _, _, _, _, _)).Times(0);
  EXPECT_CALL(*context, DoCreateSharedImage(_, _, _, _, _, _, _)).Times(0);
  auto video_frame1 = pool->MaybeCreateVideoFrame(size0, color_space0);

  // Expect a new frame to be created.
  EXPECT_CALL(*context, DoCreateGpuMemoryBuffer(size0, format));
  VerifySharedImageCreation(context.get());
  auto video_frame2 = pool->MaybeCreateVideoFrame(size0, color_space0);

  // Expect a new frame to be created.
  EXPECT_CALL(*context, DoCreateGpuMemoryBuffer(size0, format));
  VerifySharedImageCreation(context.get());
  auto video_frame3 = pool->MaybeCreateVideoFrame(size0, color_space0);

  // Freeing two frames will not result in any frames being destroyed, because
  // we allow unused 2 frames to exist.
  video_frame1 = nullptr;
  video_frame2 = nullptr;
  task_environment.RunUntilIdle();

  // Freeing the third frame will result in one of the frames being destroyed.
  EXPECT_CALL(*context, DestroySharedImage(_, _))
      .Times(NumSharedImagesPerFrame());
  video_frame3 = nullptr;
  task_environment.RunUntilIdle();

  // Destroying the pool will result in the remaining two frames being
  // destroyed.
  EXPECT_TRUE(!!context);
  EXPECT_CALL(*context, DestroySharedImage(_, _))
      .Times(NumSharedImagesPerFrame() * 2);
  pool.reset();
  task_environment.RunUntilIdle();
  EXPECT_FALSE(!!context);
}

TEST_P(RenderableGpuMemoryBufferVideoFramePoolTest, FrameFreedAfterPool) {
  base::test::SingleThreadTaskEnvironment task_environment;
  const gfx::Size size0(128, 256);
  const gfx::BufferFormat format =
      VideoPixelFormatToGfxBufferFormat(format_).value();
  const gfx::ColorSpace color_space0 = GetColorSpaceForPixelFormat(format_);

  base::WeakPtr<FakeContext> context;
  std::unique_ptr<RenderableGpuMemoryBufferVideoFramePool> pool;
  {
    auto context_strong = std::make_unique<FakeContext>();
    context = context_strong->GetWeakPtr();
    pool = RenderableGpuMemoryBufferVideoFramePool::Create(
        std::move(context_strong), format_);
  }

  // Create a new frame.
  EXPECT_CALL(*context, DoCreateGpuMemoryBuffer(size0, format));
  VerifySharedImageCreation(context.get());
  auto video_frame0 = pool->MaybeCreateVideoFrame(size0, color_space0);
  task_environment.RunUntilIdle();

  // If the pool is destroyed, but a frame still exists, the context will not
  // be destroyed.
  pool.reset();
  task_environment.RunUntilIdle();
  EXPECT_TRUE(context);

  // Destroy the frame. Still nothing will happen, because its destruction will
  // happen after a posted task is run.
  video_frame0 = nullptr;

  // The shared images will be destroyed once the posted task is run.
  EXPECT_CALL(*context, DestroySharedImage(_, _))
      .Times(NumSharedImagesPerFrame());
  task_environment.RunUntilIdle();
  EXPECT_FALSE(!!context);
}

TEST_P(RenderableGpuMemoryBufferVideoFramePoolTest, CrossThread) {
  base::test::TaskEnvironment task_environment{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  const gfx::Size size0(128, 256);
  const gfx::ColorSpace color_space0 = GetColorSpaceForPixelFormat(format_);

  // Create a pool on the main thread.
  auto pool = RenderableGpuMemoryBufferVideoFramePool::Create(
      std::make_unique<FakeContext>(), format_);

  base::ThreadPool::CreateSequencedTaskRunner({})->PostTaskAndReplyWithResult(
      FROM_HERE,
      // Create a frame on another thread.
      base::BindLambdaForTesting(
          [&]() { return pool->MaybeCreateVideoFrame(size0, color_space0); }),
      // Destroy the video frame on the main thread.
      base::BindLambdaForTesting(
          [&](scoped_refptr<VideoFrame> video_frame0) {}));
  task_environment.RunUntilIdle();

  // Destroy the pool.
  pool = nullptr;
  task_environment.RunUntilIdle();
}

TEST_P(RenderableGpuMemoryBufferVideoFramePoolTest,
       VideoFramesDestroyedConcurrently) {
  base::test::TaskEnvironment task_environment{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  const gfx::Size size0(128, 256);
  const gfx::BufferFormat format =
      VideoPixelFormatToGfxBufferFormat(format_).value();
  const gfx::ColorSpace color_space0 = GetColorSpaceForPixelFormat(format_);

  // Create a pool and several frames on the main thread.
  base::WeakPtr<FakeContext> context;
  std::unique_ptr<RenderableGpuMemoryBufferVideoFramePool> pool;
  {
    auto context_strong = std::make_unique<FakeContext>();
    context = context_strong->GetWeakPtr();
    pool = RenderableGpuMemoryBufferVideoFramePool::Create(
        std::move(context_strong), format_);
  }

  std::vector<scoped_refptr<VideoFrame>> frames;
  static constexpr int kNumFrames = 3;
  for (int i = 0; i < kNumFrames; i++) {
    EXPECT_CALL(*context, DoCreateGpuMemoryBuffer(size0, format));
    VerifySharedImageCreation(context.get());
    frames.emplace_back(pool->MaybeCreateVideoFrame(size0, color_space0));
  }
  task_environment.RunUntilIdle();

  // Expect all frames to be destroyed eventually.
  EXPECT_CALL(*context, DestroySharedImage(_, _))
      .Times(kNumFrames * NumSharedImagesPerFrame());

  // Destroy frames on separate threads. TSAN will tell us if there's a problem.
  for (int i = 0; i < kNumFrames; i++) {
    base::ThreadPool::CreateSequencedTaskRunner({})->PostTask(
        FROM_HERE, base::DoNothingWithBoundArgs(std::move(frames[i])));
  }

  pool.reset();
  task_environment.RunUntilIdle();
  EXPECT_FALSE(!!context);
}

TEST_P(RenderableGpuMemoryBufferVideoFramePoolTest, ConcurrentCreateDestroy) {
  base::test::TaskEnvironment task_environment{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  const gfx::Size size0(128, 256);
  const gfx::ColorSpace color_space0 = GetColorSpaceForPixelFormat(format_);

  // Create a pool on the main thread.
  auto pool = RenderableGpuMemoryBufferVideoFramePool::Create(
      std::make_unique<FakeContext>(), format_);

  // Create a frame on the main thread.
  auto video_frame0 = pool->MaybeCreateVideoFrame(size0, color_space0);
  task_environment.RunUntilIdle();

  // Destroy the frame on another thread. TSAN will tell us if there's a
  // problem.
  base::ThreadPool::CreateSequencedTaskRunner({})->PostTask(
      FROM_HERE, base::DoNothingWithBoundArgs(std::move(video_frame0)));

  // Create another frame on the main thread.
  auto video_frame1 = pool->MaybeCreateVideoFrame(size0, color_space0);
  task_environment.RunUntilIdle();

  video_frame1 = nullptr;
  pool.reset();
  task_environment.RunUntilIdle();
}

TEST_P(RenderableGpuMemoryBufferVideoFramePoolTest, RespectSizeAndColorSpace) {
  base::test::SingleThreadTaskEnvironment task_environment;
  const gfx::BufferFormat format =
      VideoPixelFormatToGfxBufferFormat(format_).value();
  const gfx::Size size0(128, 256);
  const gfx::ColorSpace color_space0 = GetColorSpaceForPixelFormat(format_);
  const gfx::Size size1(256, 256);
  const gfx::ColorSpace color_space1 = gfx::ColorSpace::CreateREC601();

  base::WeakPtr<FakeContext> context;
  std::unique_ptr<RenderableGpuMemoryBufferVideoFramePool> pool;
  {
    auto context_strong = std::make_unique<FakeContext>();
    context = context_strong->GetWeakPtr();
    pool = RenderableGpuMemoryBufferVideoFramePool::Create(
        std::move(context_strong), format_);
  }

  // Create a new frame.
  EXPECT_CALL(*context, DoCreateGpuMemoryBuffer(size0, format)).Times(1);
  VerifySharedImageCreation(context.get());
  auto video_frame0 = pool->MaybeCreateVideoFrame(size0, color_space0);
  video_frame0 = nullptr;
  task_environment.RunUntilIdle();

  // Expect the frame to be reused.
  EXPECT_CALL(*context, DoCreateGpuMemoryBuffer(_, _)).Times(0);
  EXPECT_CALL(*context, DoCreateSharedImage(_, _, _, _, _, _)).Times(0);
  EXPECT_CALL(*context, DoCreateSharedImage(_, _, _, _, _, _, _)).Times(0);
  video_frame0 = pool->MaybeCreateVideoFrame(size0, color_space0);
  video_frame0 = nullptr;
  task_environment.RunUntilIdle();

  // Change the size, expect a new frame to be created (and the previous frame
  // to be destroyed).
  EXPECT_CALL(*context, DestroySharedImage(_, _))
      .Times(NumSharedImagesPerFrame());
  EXPECT_CALL(*context, DoCreateGpuMemoryBuffer(size1, format));
  VerifySharedImageCreation(context.get());
  video_frame0 = pool->MaybeCreateVideoFrame(size1, color_space0);
  video_frame0 = nullptr;
  task_environment.RunUntilIdle();

  // Expect that frame to be reused.
  EXPECT_CALL(*context, DoCreateGpuMemoryBuffer(_, _)).Times(0);
  EXPECT_CALL(*context, DoCreateSharedImage(_, _, _, _, _, _)).Times(0);
  EXPECT_CALL(*context, DoCreateSharedImage(_, _, _, _, _, _, _)).Times(0);
  video_frame0 = pool->MaybeCreateVideoFrame(size1, color_space0);
  video_frame0 = nullptr;
  task_environment.RunUntilIdle();

  // Change the color space, expect a new frame to be created (and the previous
  // frame to be destroyed).
  EXPECT_CALL(*context, DestroySharedImage(_, _))
      .Times(NumSharedImagesPerFrame());
  EXPECT_CALL(*context, DoCreateGpuMemoryBuffer(size1, format));
  VerifySharedImageCreation(context.get());
  video_frame0 = pool->MaybeCreateVideoFrame(size1, color_space1);
  video_frame0 = nullptr;
  task_environment.RunUntilIdle();

  // Expect that frame to be reused.
  EXPECT_CALL(*context, DoCreateGpuMemoryBuffer(_, _)).Times(0);
  EXPECT_CALL(*context, DoCreateSharedImage(_, _, _, _, _, _)).Times(0);
  EXPECT_CALL(*context, DoCreateSharedImage(_, _, _, _, _, _, _)).Times(0);
  video_frame0 = pool->MaybeCreateVideoFrame(size1, color_space1);
  video_frame0 = nullptr;
  task_environment.RunUntilIdle();

  EXPECT_CALL(*context, DestroySharedImage(_, _))
      .Times(NumSharedImagesPerFrame());
  pool.reset();
  task_environment.RunUntilIdle();
  EXPECT_FALSE(!!context);
}

INSTANTIATE_TEST_SUITE_P(
    All,
    RenderableGpuMemoryBufferVideoFramePoolTest,
    testing::Values(media::VideoPixelFormat::PIXEL_FORMAT_NV12,
                    media::VideoPixelFormat::PIXEL_FORMAT_ARGB,
                    media::VideoPixelFormat::PIXEL_FORMAT_ABGR));

}  // namespace

}  // namespace media
