// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GPU_COMMAND_BUFFER_SERVICE_SHARED_IMAGE_WRAPPED_SK_IMAGE_BACKING_FACTORY_H_
#define GPU_COMMAND_BUFFER_SERVICE_SHARED_IMAGE_WRAPPED_SK_IMAGE_BACKING_FACTORY_H_

#include <memory>

#include "gpu/command_buffer/common/shared_image_usage.h"
#include "gpu/command_buffer/service/shared_image/shared_image_backing_factory.h"
#include "gpu/command_buffer/service/texture_base.h"
#include "gpu/gpu_gles2_export.h"
#include "third_party/skia/include/core/SkImage.h"
#include "ui/gfx/geometry/size.h"

namespace gpu {

class SharedContextState;

class GPU_GLES2_EXPORT WrappedSkImageBackingFactory
    : public gpu::SharedImageBackingFactory {
 public:
  explicit WrappedSkImageBackingFactory(
      scoped_refptr<SharedContextState> context_state);

  WrappedSkImageBackingFactory(const WrappedSkImageBackingFactory&) = delete;
  WrappedSkImageBackingFactory& operator=(const WrappedSkImageBackingFactory&) =
      delete;

  ~WrappedSkImageBackingFactory() override;

  // SharedImageBackingFactory implementation:
  std::unique_ptr<SharedImageBacking> CreateSharedImage(
      const Mailbox& mailbox,
      viz::SharedImageFormat format,
      SurfaceHandle surface_handle,
      const gfx::Size& size,
      const gfx::ColorSpace& color_space,
      GrSurfaceOrigin surface_origin,
      SkAlphaType alpha_type,
      SharedImageUsageSet usage,
      std::string debug_label,
      bool is_thread_safe) override;
  std::unique_ptr<SharedImageBacking> CreateSharedImage(
      const Mailbox& mailbox,
      viz::SharedImageFormat format,
      const gfx::Size& size,
      const gfx::ColorSpace& color_space,
      GrSurfaceOrigin surface_origin,
      SkAlphaType alpha_type,
      SharedImageUsageSet usage,
      std::string debug_label,
      bool is_thread_safe,
      base::span<const uint8_t> pixel_data) override;
  std::unique_ptr<SharedImageBacking> CreateSharedImage(
      const Mailbox& mailbox,
      viz::SharedImageFormat format,
      const gfx::Size& size,
      const gfx::ColorSpace& color_space,
      GrSurfaceOrigin surface_origin,
      SkAlphaType alpha_type,
      SharedImageUsageSet usage,
      std::string debug_label,
      gfx::GpuMemoryBufferHandle handle) override;
  std::unique_ptr<SharedImageBacking> CreateSharedImage(
      const Mailbox& mailbox,
      gfx::GpuMemoryBufferHandle handle,
      gfx::BufferFormat format,
      gfx::BufferPlane plane,
      const gfx::Size& size,
      const gfx::ColorSpace& color_space,
      GrSurfaceOrigin surface_origin,
      SkAlphaType alpha_type,
      SharedImageUsageSet usage,
      std::string debug_label) override;
  bool IsSupported(uint32_t usage,
                   viz::SharedImageFormat format,
                   const gfx::Size& size,
                   bool thread_safe,
                   gfx::GpuMemoryBufferType gmb_type,
                   GrContextType gr_context_type,
                   base::span<const uint8_t> pixel_data) override;
  SharedImageBackingType GetBackingType() override;

 private:
  scoped_refptr<SharedContextState> context_state_;
  const bool use_graphite_;
  const bool is_drdc_enabled_;
  const bool graphite_supports_compressed_textures_;
};

}  // namespace gpu

#endif  // GPU_COMMAND_BUFFER_SERVICE_SHARED_IMAGE_WRAPPED_SK_IMAGE_BACKING_FACTORY_H_
