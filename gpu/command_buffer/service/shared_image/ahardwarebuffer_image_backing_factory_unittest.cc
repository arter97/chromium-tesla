// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gpu/command_buffer/service/shared_image/ahardwarebuffer_image_backing_factory.h"

#include "base/android/android_hardware_buffer_compat.h"
#include "base/android/scoped_hardware_buffer_fence_sync.h"
#include "base/functional/callback_helpers.h"
#include "base/memory/raw_ptr.h"
#include "gpu/command_buffer/common/mailbox.h"
#include "gpu/command_buffer/common/shared_image_usage.h"
#include "gpu/command_buffer/service/shared_context_state.h"
#include "gpu/command_buffer/service/shared_image/shared_image_backing.h"
#include "gpu/command_buffer/service/shared_image/shared_image_factory.h"
#include "gpu/command_buffer/service/shared_image/shared_image_manager.h"
#include "gpu/command_buffer/service/shared_image/shared_image_representation.h"
#include "gpu/command_buffer/service/shared_image/shared_image_test_base.h"
#include "gpu/command_buffer/service/shared_image/test_utils.h"
#include "gpu/command_buffer/service/texture_manager.h"
#include "gpu/config/gpu_feature_info.h"
#include "gpu/config/gpu_preferences.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/skia/include/core/SkImage.h"
#include "third_party/skia/include/core/SkSurface.h"
#include "third_party/skia/include/gpu/GrBackendSemaphore.h"
#include "third_party/skia/include/gpu/GrBackendSurface.h"
#include "third_party/skia/include/gpu/GrDirectContext.h"
#include "third_party/skia/include/private/chromium/GrPromiseImageTexture.h"
#include "ui/gfx/color_space.h"
#include "ui/gl/gl_bindings.h"
#include "ui/gl/gl_gl_api_implementation.h"
#include "ui/gl/gl_surface_egl.h"

#if BUILDFLAG(USE_DAWN) && BUILDFLAG(DAWN_ENABLE_BACKEND_OPENGLES)
#include <dawn/native/DawnNative.h>
#include <dawn/native/OpenGLBackend.h>
#include <dawn/webgpu_cpp.h>
#endif

namespace gpu {
namespace {

class AHardwareBufferImageBackingFactoryTest : public SharedImageTestBase {
 public:
  void SetUp() override {
    // AHardwareBuffer is only supported on ANDROID O+. Hence these tests
    // should not be run on android versions less that O.
    if (!base::AndroidHardwareBufferCompat::IsSupportAvailable()) {
      GTEST_SKIP() << "AHardwareBuffer not supported";
    }

    ASSERT_NO_FATAL_FAILURE(InitializeContext(GrContextType::kGL));

    backing_factory_ = std::make_unique<AHardwareBufferImageBackingFactory>(
        context_state_->feature_info(), gpu_preferences_);
  }
};

class GlLegacySharedImage {
 public:
  GlLegacySharedImage(
      SharedImageBackingFactory* backing_factory,
      bool is_thread_safe,
      bool concurrent_read_write,
      SharedImageManager* shared_image_manager,
      MemoryTypeTracker* memory_type_tracker,
      SharedImageRepresentationFactory* shared_image_representation_factory);
  ~GlLegacySharedImage();

  gfx::Size size() { return size_; }
  Mailbox mailbox() { return mailbox_; }

 private:
  gfx::Size size_;
  Mailbox mailbox_;
  std::unique_ptr<SharedImageBacking> backing_;
  std::unique_ptr<SharedImageRepresentationFactoryRef> shared_image_;
};

// Basic test to check creation and deletion of AHB backed shared image.
TEST_F(AHardwareBufferImageBackingFactoryTest, Basic) {
  GlLegacySharedImage gl_legacy_shared_image{
      backing_factory_.get(),          /*is_thread_safe=*/false,
      /*concurrent_read_write=*/false, &shared_image_manager_,
      &memory_type_tracker_,           &shared_image_representation_factory_};

  // Validate a SkiaImageRepresentation.
  auto skia_representation = shared_image_representation_factory_.ProduceSkia(
      gl_legacy_shared_image.mailbox(), context_state_.get());
  EXPECT_TRUE(skia_representation);
  std::vector<GrBackendSemaphore> begin_semaphores;
  std::vector<GrBackendSemaphore> end_semaphores;
  std::unique_ptr<SkiaImageRepresentation::ScopedWriteAccess>
      scoped_write_access;
  scoped_write_access = skia_representation->BeginScopedWriteAccess(
      &begin_semaphores, &end_semaphores,
      SharedImageRepresentation::AllowUnclearedAccess::kYes);
  EXPECT_TRUE(scoped_write_access);
  auto* surface = scoped_write_access->surface();
  EXPECT_TRUE(surface);
  EXPECT_EQ(gl_legacy_shared_image.size().width(), surface->width());
  EXPECT_EQ(gl_legacy_shared_image.size().height(), surface->height());
  EXPECT_EQ(0u, begin_semaphores.size());
  EXPECT_EQ(0u, end_semaphores.size());
  scoped_write_access.reset();

  std::unique_ptr<SkiaImageRepresentation::ScopedReadAccess> scoped_read_access;
  scoped_read_access = skia_representation->BeginScopedReadAccess(
      &begin_semaphores, &end_semaphores);
  EXPECT_TRUE(scoped_read_access);
  auto* promise_texture = scoped_read_access->promise_image_texture();
  EXPECT_EQ(0u, begin_semaphores.size());
  EXPECT_EQ(0u, end_semaphores.size());
  EXPECT_TRUE(promise_texture);
  GrBackendTexture backend_texture = promise_texture->backendTexture();
  EXPECT_TRUE(backend_texture.isValid());
  EXPECT_EQ(gl_legacy_shared_image.size().width(), backend_texture.width());
  EXPECT_EQ(gl_legacy_shared_image.size().height(), backend_texture.height());
  scoped_read_access.reset();
  skia_representation.reset();
}

// Test to check interaction between Gl and skia GL representations.
// We write to a GL texture using gl representation and then read from skia
// representation.
TEST_F(AHardwareBufferImageBackingFactoryTest, GLSkiaGL) {
  // Create a backing using mailbox.
  auto mailbox = Mailbox::Generate();
  auto format = viz::SinglePlaneFormat::kRGBA_8888;
  gfx::Size size(1, 1);
  auto color_space = gfx::ColorSpace::CreateSRGB();
  GrSurfaceOrigin surface_origin = kTopLeft_GrSurfaceOrigin;
  SkAlphaType alpha_type = kPremul_SkAlphaType;
  gpu::SharedImageUsageSet usage =
      SHARED_IMAGE_USAGE_GLES2_WRITE | SHARED_IMAGE_USAGE_DISPLAY_READ;
  gpu::SurfaceHandle surface_handle = gpu::kNullSurfaceHandle;
  auto backing = backing_factory_->CreateSharedImage(
      mailbox, format, surface_handle, size, color_space, surface_origin,
      alpha_type, usage, "TestLabel", /*is_thread_safe=*/false);
  EXPECT_TRUE(backing);

  GLenum expected_target = GL_TEXTURE_2D;
  std::unique_ptr<SharedImageRepresentationFactoryRef> factory_ref =
      shared_image_manager_.Register(std::move(backing), &memory_type_tracker_);

  // Create a GLTextureImageRepresentation.
  auto gl_representation =
      shared_image_representation_factory_.ProduceGLTexture(mailbox);
  EXPECT_TRUE(gl_representation);
  EXPECT_EQ(expected_target, gl_representation->GetTexture()->target());

  // Create an FBO.
  GLuint fbo = 0;
  gl::GLApi* api = gl::g_current_gl_context;
  api->glGenFramebuffersEXTFn(1, &fbo);
  api->glBindFramebufferEXTFn(GL_FRAMEBUFFER, fbo);

  // Attach the texture to FBO.
  api->glFramebufferTexture2DEXTFn(
      GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
      gl_representation->GetTexture()->target(),
      gl_representation->GetTexture()->service_id(), 0);

  // Set the clear color to green.
  api->glClearColorFn(0.0f, 1.0f, 0.0f, 1.0f);
  api->glClearFn(GL_COLOR_BUFFER_BIT);

  // Mark the representation as cleared.
  gl_representation->SetCleared();
  gl_representation.reset();

  auto dst_pixels = ReadPixels(mailbox, size, context_state_.get(),
                               &shared_image_representation_factory_);

  // Compare the pixel values.
  EXPECT_EQ(dst_pixels[0], 0);
  EXPECT_EQ(dst_pixels[1], 255);
  EXPECT_EQ(dst_pixels[2], 0);
  EXPECT_EQ(dst_pixels[3], 255);

  factory_ref.reset();
}

// Test ProduceDawn via OpenGLES Compat backend
#if BUILDFLAG(USE_DAWN) && BUILDFLAG(DAWN_ENABLE_BACKEND_OPENGLES)
TEST_F(AHardwareBufferImageBackingFactoryTest, ProduceDawnOpenGLES) {
  // Create a backing using mailbox.
  auto mailbox = Mailbox::Generate();
  auto format = viz::SinglePlaneFormat::kRGBA_8888;
  gfx::Size size(1, 1);
  auto color_space = gfx::ColorSpace::CreateSRGB();
  GrSurfaceOrigin surface_origin = kTopLeft_GrSurfaceOrigin;
  SkAlphaType alpha_type = kPremul_SkAlphaType;
  gpu::SharedImageUsageSet usage = SHARED_IMAGE_USAGE_WEBGPU_WRITE |
                                   SHARED_IMAGE_USAGE_DISPLAY_READ |
                                   SHARED_IMAGE_USAGE_SCANOUT;
  gpu::SurfaceHandle surface_handle = gpu::kNullSurfaceHandle;
  auto backing = backing_factory_->CreateSharedImage(
      mailbox, format, surface_handle, size, color_space, surface_origin,
      alpha_type, usage, "TestLabel", /*is_thread_safe=*/false);
  EXPECT_TRUE(backing);

  std::unique_ptr<SharedImageRepresentationFactoryRef> factory_ref =
      shared_image_manager_.Register(std::move(backing), &memory_type_tracker_);

  dawn::native::Instance instance;

  wgpu::RequestAdapterOptions adapter_options;
  adapter_options.backendType = wgpu::BackendType::OpenGLES;
  adapter_options.compatibilityMode = true;

  dawn::native::opengl::RequestAdapterOptionsGetGLProc
      adapter_options_get_gl_proc = {};
  // TODO(343870490): Remove the cast once the type is updated in Dawn.
  adapter_options_get_gl_proc.getProc =
      reinterpret_cast<decltype(adapter_options_get_gl_proc.getProc)>(
          gl::GetGLProcAddress);
  gl::GLDisplayEGL* gl_display = gl::GLSurfaceEGL::GetGLDisplayEGL();
  if (gl_display) {
    adapter_options_get_gl_proc.display = gl_display->GetDisplay();
  } else {
    adapter_options_get_gl_proc.display = EGL_NO_DISPLAY;
  }
  adapter_options.nextInChain = &adapter_options_get_gl_proc;

  std::vector<dawn::native::Adapter> adapters =
      instance.EnumerateAdapters(&adapter_options);
  if (adapters.empty()) {
    GTEST_SKIP() << "Dawn OpenGLES backend not available";
  }
  wgpu::Adapter adapter = wgpu::Adapter(adapters[0].Get());

  wgpu::DeviceDescriptor device_descriptor;
  wgpu::Device device = adapter.CreateDevice(&device_descriptor);

  auto dawn_representation = shared_image_representation_factory_.ProduceDawn(
      mailbox, device, wgpu::BackendType::OpenGLES, {}, context_state_);
  EXPECT_TRUE(dawn_representation);

  wgpu::Color color{0, 255, 0, 255};
  {
    auto scoped_access = dawn_representation->BeginScopedAccess(
        wgpu::TextureUsage::RenderAttachment,
        SharedImageRepresentation::AllowUnclearedAccess::kYes);
    ASSERT_TRUE(scoped_access);

    wgpu::Texture texture(scoped_access->texture());

    wgpu::RenderPassColorAttachment color_desc;
    color_desc.view = texture.CreateView();
    color_desc.resolveTarget = nullptr;
    color_desc.loadOp = wgpu::LoadOp::Clear;
    color_desc.storeOp = wgpu::StoreOp::Store;
    color_desc.clearValue = color;

    wgpu::RenderPassDescriptor renderPassDesc = {};
    renderPassDesc.colorAttachmentCount = 1;
    renderPassDesc.colorAttachments = &color_desc;
    renderPassDesc.depthStencilAttachment = nullptr;

    wgpu::CommandEncoder encoder = device.CreateCommandEncoder();
    wgpu::RenderPassEncoder pass = encoder.BeginRenderPass(&renderPassDesc);
    pass.End();
    wgpu::CommandBuffer commands = encoder.Finish();

    wgpu::Queue queue = device.GetQueue();
    queue.Submit(1, &commands);
  }
  auto dst_pixels = ReadPixels(mailbox, size, context_state_.get(),
                               &shared_image_representation_factory_);
  // Compare the pixel values.
  EXPECT_EQ(dst_pixels[0], color.r);
  EXPECT_EQ(dst_pixels[1], color.g);
  EXPECT_EQ(dst_pixels[2], color.b);
  EXPECT_EQ(dst_pixels[3], color.a);

  factory_ref.reset();
}
#endif  // BUILDFLAG(USE_DAWN) && BUILDFLAG(DAWN_ENABLE_BACKEND_OPENGLES)

TEST_F(AHardwareBufferImageBackingFactoryTest, InitialData) {
  auto mailbox = Mailbox::Generate();
  auto format = viz::SinglePlaneFormat::kRGBA_8888;
  gfx::Size size(4, 4);

  std::vector<uint8_t> initial_data(size.width() * size.height() * 4);

  for (size_t i = 0; i < initial_data.size(); i++) {
    initial_data[i] = static_cast<uint8_t>(i);
  }

  // Create a SharedImage whose contents will be read out by Skia.
  auto color_space = gfx::ColorSpace::CreateSRGB();
  GrSurfaceOrigin surface_origin = kTopLeft_GrSurfaceOrigin;
  SkAlphaType alpha_type = kPremul_SkAlphaType;
  gpu::SharedImageUsageSet usage = SHARED_IMAGE_USAGE_DISPLAY_READ;
  auto backing = backing_factory_->CreateSharedImage(
      mailbox, format, size, color_space, surface_origin, alpha_type, usage,
      "TestLabel", /*is_thread_safe=*/false, initial_data);
  EXPECT_TRUE(backing);

  std::unique_ptr<SharedImageRepresentationFactoryRef> factory_ref =
      shared_image_manager_.Register(std::move(backing), &memory_type_tracker_);

  auto dst_pixels = ReadPixels(mailbox, size, context_state_.get(),
                               &shared_image_representation_factory_);

  // Compare the pixel values.
  DCHECK(dst_pixels.size() == initial_data.size());

  EXPECT_EQ(dst_pixels, initial_data);
  factory_ref.reset();
}

// Test to check invalid format support.
TEST_F(AHardwareBufferImageBackingFactoryTest, InvalidFormat) {
  auto mailbox = Mailbox::Generate();
  auto format = viz::MultiPlaneFormat::kNV12;
  gfx::Size size(256, 256);
  auto color_space = gfx::ColorSpace::CreateSRGB();
  GrSurfaceOrigin surface_origin = kTopLeft_GrSurfaceOrigin;
  SkAlphaType alpha_type = kPremul_SkAlphaType;
  gpu::SurfaceHandle surface_handle = gpu::kNullSurfaceHandle;
  // NOTE: The specific usage here doesn't matter - the only important thing is
  // that it be a usage that the factory supports so that the test is exercising
  // the fact that the passed-in *format* is not supported.
  gpu::SharedImageUsageSet usage = SHARED_IMAGE_USAGE_GLES2_READ;
  auto backing = backing_factory_->CreateSharedImage(
      mailbox, format, surface_handle, size, color_space, surface_origin,
      alpha_type, usage, "TestLabel", /*is_thread_safe=*/false);
  EXPECT_FALSE(backing);
}

// Test to check invalid size support.
TEST_F(AHardwareBufferImageBackingFactoryTest, InvalidSize) {
  auto mailbox = Mailbox::Generate();
  auto format = viz::SinglePlaneFormat::kRGBA_8888;
  gfx::Size size(0, 0);
  auto color_space = gfx::ColorSpace::CreateSRGB();
  GrSurfaceOrigin surface_origin = kTopLeft_GrSurfaceOrigin;
  SkAlphaType alpha_type = kPremul_SkAlphaType;
  gpu::SurfaceHandle surface_handle = gpu::kNullSurfaceHandle;
  // NOTE: The specific usage here doesn't matter - the only important thing is
  // that it be a usage that the factory supports so that the test is exercising
  // the fact that the passed-in *size* is not supported.
  gpu::SharedImageUsageSet usage = SHARED_IMAGE_USAGE_GLES2_READ;
  auto backing = backing_factory_->CreateSharedImage(
      mailbox, format, surface_handle, size, color_space, surface_origin,
      alpha_type, usage, "TestLabel", /*is_thread_safe=*/false);
  EXPECT_FALSE(backing);

  size = gfx::Size(INT_MAX, INT_MAX);
  backing = backing_factory_->CreateSharedImage(
      mailbox, format, surface_handle, size, color_space, surface_origin,
      alpha_type, usage, "TestLabel", /*is_thread_safe=*/false);
  EXPECT_FALSE(backing);
}

TEST_F(AHardwareBufferImageBackingFactoryTest, EstimatedSize) {
  auto mailbox = Mailbox::Generate();
  auto format = viz::SinglePlaneFormat::kRGBA_8888;
  gfx::Size size(256, 256);
  auto color_space = gfx::ColorSpace::CreateSRGB();
  GrSurfaceOrigin surface_origin = kTopLeft_GrSurfaceOrigin;
  SkAlphaType alpha_type = kPremul_SkAlphaType;
  gpu::SurfaceHandle surface_handle = gpu::kNullSurfaceHandle;
  // NOTE: The specific usage does not matter here as long as it is supported by
  // the factory.
  gpu::SharedImageUsageSet usage = SHARED_IMAGE_USAGE_GLES2_READ;
  auto backing = backing_factory_->CreateSharedImage(
      mailbox, format, surface_handle, size, color_space, surface_origin,
      alpha_type, usage, "TestLabel", /*is_thread_safe=*/false);
  EXPECT_TRUE(backing);

  size_t backing_estimated_size = backing->GetEstimatedSize();
  EXPECT_GT(backing_estimated_size, 0u);

  std::unique_ptr<SharedImageRepresentationFactoryRef> shared_image =
      shared_image_manager_.Register(std::move(backing), &memory_type_tracker_);
  EXPECT_EQ(backing_estimated_size, memory_type_tracker_.GetMemRepresented());

  shared_image.reset();
}

// Test to check that only one context can write at a time
TEST_F(AHardwareBufferImageBackingFactoryTest, OnlyOneWriter) {
  GlLegacySharedImage gl_legacy_shared_image{
      backing_factory_.get(),          /*is_thread_safe=*/true,
      /*concurrent_read_write=*/false, &shared_image_manager_,
      &memory_type_tracker_,           &shared_image_representation_factory_};

  auto skia_representation = shared_image_representation_factory_.ProduceSkia(
      gl_legacy_shared_image.mailbox(), context_state_.get());

  std::vector<GrBackendSemaphore> begin_semaphores;
  std::vector<GrBackendSemaphore> end_semaphores;
  std::unique_ptr<SkiaImageRepresentation::ScopedWriteAccess>
      scoped_write_access;
  scoped_write_access = skia_representation->BeginScopedWriteAccess(
      &begin_semaphores, &end_semaphores,
      SharedImageRepresentation::AllowUnclearedAccess::kYes);
  EXPECT_TRUE(scoped_write_access);
  EXPECT_EQ(0u, begin_semaphores.size());
  EXPECT_EQ(0u, end_semaphores.size());

  auto skia_representation2 = shared_image_representation_factory_.ProduceSkia(
      gl_legacy_shared_image.mailbox(), context_state_.get());
  std::vector<GrBackendSemaphore> begin_semaphores2;
  std::vector<GrBackendSemaphore> end_semaphores2;
  std::unique_ptr<SkiaImageRepresentation::ScopedWriteAccess>
      scoped_write_access2;
  scoped_write_access2 = skia_representation2->BeginScopedWriteAccess(
      &begin_semaphores2, &end_semaphores2,
      SharedImageRepresentation::AllowUnclearedAccess::kYes);
  EXPECT_FALSE(scoped_write_access2);
  EXPECT_EQ(0u, begin_semaphores2.size());
  EXPECT_EQ(0u, end_semaphores2.size());
  skia_representation2.reset();

  scoped_write_access.reset();
  skia_representation.reset();
}

// Test to check that multiple readers are allowed
TEST_F(AHardwareBufferImageBackingFactoryTest, CanHaveMultipleReaders) {
  GlLegacySharedImage gl_legacy_shared_image{
      backing_factory_.get(),          /*is_thread_safe=*/true,
      /*concurrent_read_write=*/false, &shared_image_manager_,
      &memory_type_tracker_,           &shared_image_representation_factory_};

  auto skia_representation = shared_image_representation_factory_.ProduceSkia(
      gl_legacy_shared_image.mailbox(), context_state_.get());
  auto skia_representation2 = shared_image_representation_factory_.ProduceSkia(
      gl_legacy_shared_image.mailbox(), context_state_.get());

  std::vector<GrBackendSemaphore> begin_semaphores;
  std::vector<GrBackendSemaphore> end_semaphores;
  std::unique_ptr<SkiaImageRepresentation::ScopedReadAccess> scoped_read_access;
  scoped_read_access = skia_representation->BeginScopedReadAccess(
      &begin_semaphores, &end_semaphores);
  EXPECT_TRUE(scoped_read_access);
  EXPECT_EQ(0u, begin_semaphores.size());
  EXPECT_EQ(0u, end_semaphores.size());

  std::unique_ptr<SkiaImageRepresentation::ScopedReadAccess>
      scoped_read_access2;
  scoped_read_access2 = skia_representation2->BeginScopedReadAccess(
      &begin_semaphores, &end_semaphores);
  EXPECT_TRUE(scoped_read_access2);
  EXPECT_EQ(0u, begin_semaphores.size());
  EXPECT_EQ(0u, end_semaphores.size());

  scoped_read_access2.reset();
  skia_representation2.reset();
  scoped_read_access.reset();
  skia_representation.reset();
}

// Test to check that a context cannot write while another context is reading
TEST_F(AHardwareBufferImageBackingFactoryTest, CannotWriteWhileReading) {
  GlLegacySharedImage gl_legacy_shared_image{
      backing_factory_.get(),          /*is_thread_safe=*/true,
      /*concurrent_read_write=*/false, &shared_image_manager_,
      &memory_type_tracker_,           &shared_image_representation_factory_};

  auto skia_representation = shared_image_representation_factory_.ProduceSkia(
      gl_legacy_shared_image.mailbox(), context_state_.get());

  std::vector<GrBackendSemaphore> begin_semaphores;
  std::vector<GrBackendSemaphore> end_semaphores;

  std::unique_ptr<SkiaImageRepresentation::ScopedReadAccess> scoped_read_access;
  scoped_read_access = skia_representation->BeginScopedReadAccess(
      &begin_semaphores, &end_semaphores);
  EXPECT_TRUE(scoped_read_access);
  EXPECT_EQ(0u, begin_semaphores.size());
  EXPECT_EQ(0u, end_semaphores.size());

  auto skia_representation2 = shared_image_representation_factory_.ProduceSkia(
      gl_legacy_shared_image.mailbox(), context_state_.get());

  std::vector<GrBackendSemaphore> begin_semaphores2;
  std::vector<GrBackendSemaphore> end_semaphores2;

  std::unique_ptr<SkiaImageRepresentation::ScopedWriteAccess>
      scoped_write_access;
  scoped_write_access = skia_representation2->BeginScopedWriteAccess(
      &begin_semaphores2, &end_semaphores2,
      SharedImageRepresentation::AllowUnclearedAccess::kYes);
  EXPECT_FALSE(scoped_write_access);
  EXPECT_EQ(0u, begin_semaphores2.size());
  EXPECT_EQ(0u, end_semaphores2.size());
  skia_representation2.reset();

  scoped_read_access.reset();
  skia_representation.reset();
}

// Test to check that a context cannot read while another context is writing
TEST_F(AHardwareBufferImageBackingFactoryTest, CannotReadWhileWriting) {
  GlLegacySharedImage gl_legacy_shared_image{
      backing_factory_.get(),          /*is_thread_safe=*/true,
      /*concurrent_read_write=*/false, &shared_image_manager_,
      &memory_type_tracker_,           &shared_image_representation_factory_};

  auto skia_representation = shared_image_representation_factory_.ProduceSkia(
      gl_legacy_shared_image.mailbox(), context_state_.get());
  std::vector<GrBackendSemaphore> begin_semaphores;
  std::vector<GrBackendSemaphore> end_semaphores;
  std::unique_ptr<SkiaImageRepresentation::ScopedWriteAccess>
      scoped_write_access;
  scoped_write_access = skia_representation->BeginScopedWriteAccess(
      &begin_semaphores, &end_semaphores,
      SharedImageRepresentation::AllowUnclearedAccess::kYes);
  EXPECT_TRUE(scoped_write_access);
  EXPECT_EQ(0u, begin_semaphores.size());
  EXPECT_EQ(0u, end_semaphores.size());

  auto skia_representation2 = shared_image_representation_factory_.ProduceSkia(
      gl_legacy_shared_image.mailbox(), context_state_.get());
  std::vector<GrBackendSemaphore> begin_semaphores2;
  std::vector<GrBackendSemaphore> end_semaphores2;
  std::unique_ptr<SkiaImageRepresentation::ScopedReadAccess> scoped_read_access;
  scoped_read_access = skia_representation2->BeginScopedReadAccess(
      &begin_semaphores2, &end_semaphores2);
  EXPECT_FALSE(scoped_read_access);
  EXPECT_EQ(0u, begin_semaphores2.size());
  EXPECT_EQ(0u, end_semaphores2.size());
  skia_representation2.reset();

  scoped_write_access.reset();
  skia_representation.reset();
}

TEST_F(AHardwareBufferImageBackingFactoryTest, ConcurrentReadWrite) {
  GlLegacySharedImage gl_legacy_shared_image{
      backing_factory_.get(),         /*is_thread_safe=*/true,
      /*concurrent_read_write=*/true, &shared_image_manager_,
      &memory_type_tracker_,          &shared_image_representation_factory_};

  auto skia_representation = shared_image_representation_factory_.ProduceSkia(
      gl_legacy_shared_image.mailbox(), context_state_.get());
  std::vector<GrBackendSemaphore> begin_semaphores;
  std::vector<GrBackendSemaphore> end_semaphores;
  std::unique_ptr<SkiaImageRepresentation::ScopedReadAccess> scoped_read_access;
  scoped_read_access = skia_representation->BeginScopedReadAccess(
      &begin_semaphores, &end_semaphores);
  EXPECT_TRUE(scoped_read_access);
  EXPECT_EQ(0u, begin_semaphores.size());
  EXPECT_EQ(0u, end_semaphores.size());

  auto skia_representation2 = shared_image_representation_factory_.ProduceSkia(
      gl_legacy_shared_image.mailbox(), context_state_.get());
  std::unique_ptr<SkiaImageRepresentation::ScopedWriteAccess>
      scoped_write_access;
  scoped_write_access = skia_representation2->BeginScopedWriteAccess(
      &begin_semaphores, &end_semaphores,
      SharedImageRepresentation::AllowUnclearedAccess::kYes);
  EXPECT_TRUE(scoped_write_access);
  EXPECT_EQ(0u, begin_semaphores.size());
  EXPECT_EQ(0u, end_semaphores.size());
}

GlLegacySharedImage::GlLegacySharedImage(
    SharedImageBackingFactory* backing_factory,
    bool is_thread_safe,
    bool concurrent_read_write,
    SharedImageManager* shared_image_manager,
    MemoryTypeTracker* memory_type_tracker,
    SharedImageRepresentationFactory* shared_image_representation_factory)
    : size_(256, 256) {
  mailbox_ = Mailbox::Generate();
  auto format = viz::SinglePlaneFormat::kRGBA_8888;
  auto color_space = gfx::ColorSpace::CreateSRGB();
  GrSurfaceOrigin surface_origin = kTopLeft_GrSurfaceOrigin;
  SkAlphaType alpha_type = kPremul_SkAlphaType;
  gpu::SurfaceHandle surface_handle = gpu::kNullSurfaceHandle;
  GLenum expected_target = GL_TEXTURE_2D;

  // Provide usage settings to model an SI that is written via raster and read
  // via GL (e.g., for canvas import into WebGL). Add
  // SHARED_IMAGE_USAGE_DISPLAY_READ if modeling the display compositor being on
  // the same thread as raster.
  gpu::SharedImageUsageSet usage =
      SHARED_IMAGE_USAGE_GLES2_READ | SHARED_IMAGE_USAGE_RASTER_WRITE;
  if (!is_thread_safe) {
    usage |= SHARED_IMAGE_USAGE_DISPLAY_READ;
  }
  if (concurrent_read_write) {
    usage |= SHARED_IMAGE_USAGE_CONCURRENT_READ_WRITE;
  }
  backing_ = backing_factory->CreateSharedImage(
      mailbox_, format, surface_handle, size_, color_space, surface_origin,
      alpha_type, usage, "TestLabel", is_thread_safe);
  EXPECT_TRUE(backing_);

  // Check clearing.
  if (!backing_->IsCleared()) {
    backing_->SetCleared();
    EXPECT_TRUE(backing_->IsCleared());
  }

  shared_image_ =
      shared_image_manager->Register(std::move(backing_), memory_type_tracker);

  auto gl_representation =
      shared_image_representation_factory->ProduceGLTexture(mailbox_);

  EXPECT_TRUE(gl_representation);
  EXPECT_TRUE(gl_representation->GetTexture()->service_id());
  EXPECT_EQ(expected_target, gl_representation->GetTexture()->target());
  EXPECT_EQ(size_, gl_representation->size());
  EXPECT_EQ(format, gl_representation->format());
  EXPECT_EQ(color_space, gl_representation->color_space());
  EXPECT_EQ(usage, gl_representation->usage());
  gl_representation.reset();
}

GlLegacySharedImage::~GlLegacySharedImage() {
  shared_image_.reset();
}

TEST_F(AHardwareBufferImageBackingFactoryTest, Overlay) {
  GlLegacySharedImage gl_legacy_shared_image{
      backing_factory_.get(),          /*is_thread_safe=*/false,
      /*concurrent_read_write=*/false, &shared_image_manager_,
      &memory_type_tracker_,           &shared_image_representation_factory_};

  auto skia_representation = shared_image_representation_factory_.ProduceSkia(
      gl_legacy_shared_image.mailbox(), context_state_.get());

  std::vector<GrBackendSemaphore> begin_semaphores;
  std::vector<GrBackendSemaphore> end_semaphores;
  auto scoped_write_access = skia_representation->BeginScopedWriteAccess(
      &begin_semaphores, &end_semaphores,
      SharedImageRepresentation::AllowUnclearedAccess::kYes);
  EXPECT_TRUE(scoped_write_access);
  EXPECT_EQ(0u, begin_semaphores.size());
  EXPECT_EQ(0u, end_semaphores.size());
  scoped_write_access.reset();

  auto overlay_representation =
      shared_image_representation_factory_.ProduceOverlay(
          gl_legacy_shared_image.mailbox());
  EXPECT_TRUE(overlay_representation);

  auto scoped_read_access = overlay_representation->BeginScopedReadAccess();
  EXPECT_TRUE(scoped_read_access);
  auto buffer = scoped_read_access->GetAHardwareBufferFenceSync();
  DCHECK(buffer);
  scoped_read_access.reset();
  skia_representation.reset();
}

}  // anonymous namespace
}  // namespace gpu
