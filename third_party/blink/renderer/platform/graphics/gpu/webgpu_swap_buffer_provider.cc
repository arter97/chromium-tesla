// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/platform/graphics/gpu/webgpu_swap_buffer_provider.h"

#include "base/logging.h"
#include "build/build_config.h"
#include "gpu/GLES2/gl2extchromium.h"
#include "gpu/command_buffer/client/client_shared_image.h"
#include "gpu/command_buffer/client/raster_interface.h"
#include "gpu/command_buffer/client/shared_image_interface.h"
#include "gpu/command_buffer/client/webgpu_interface.h"
#include "gpu/command_buffer/common/gpu_memory_buffer_support.h"
#include "gpu/command_buffer/common/shared_image_usage.h"

namespace blink {

namespace {
viz::SharedImageFormat WGPUFormatToViz(wgpu::TextureFormat format) {
  switch (format) {
    case wgpu::TextureFormat::BGRA8Unorm:
      return viz::SinglePlaneFormat::kBGRA_8888;
    case wgpu::TextureFormat::RGBA8Unorm:
      return viz::SinglePlaneFormat::kRGBA_8888;
    case wgpu::TextureFormat::RGBA16Float:
      return viz::SinglePlaneFormat::kRGBA_F16;
    default:
      NOTREACHED_IN_MIGRATION();
      return viz::SinglePlaneFormat::kRGBA_8888;
  }
}

}  // namespace

WebGPUSwapBufferProvider::WebGPUSwapBufferProvider(
    Client* client,
    scoped_refptr<DawnControlClientHolder> dawn_control_client,
    const wgpu::Device& device,
    wgpu::TextureUsage usage,
    wgpu::TextureFormat format,
    PredefinedColorSpace color_space,
    const gfx::HDRMetadata& hdr_metadata)
    : dawn_control_client_(dawn_control_client),
      client_(client),
      device_(device),
      format_(WGPUFormatToViz(format)),
      usage_(usage),
      color_space_(color_space),
      hdr_metadata_(hdr_metadata) {
  wgpu::SupportedLimits limits = {};
  auto get_limits_succeeded = device_.GetLimits(&limits);
  CHECK(get_limits_succeeded);

  max_texture_size_ = limits.limits.maxTextureDimension2D;
}

WebGPUSwapBufferProvider::~WebGPUSwapBufferProvider() {
  Neuter();
}

viz::SharedImageFormat WebGPUSwapBufferProvider::Format() const {
  return format_;
}

const gfx::Size& WebGPUSwapBufferProvider::Size() const {
  if (current_swap_buffer_)
    return current_swap_buffer_->size;

  static constexpr gfx::Size kEmpty;
  return kEmpty;
}

cc::Layer* WebGPUSwapBufferProvider::CcLayer() {
  DCHECK(!neutered_);
  return layer_.get();
}

void WebGPUSwapBufferProvider::SetFilterQuality(
    cc::PaintFlags::FilterQuality filter_quality) {
  if (filter_quality != filter_quality_) {
    filter_quality_ = filter_quality;
    if (layer_) {
      layer_->SetNearestNeighbor(filter_quality ==
                                 cc::PaintFlags::FilterQuality::kNone);
    }
  }
}

void WebGPUSwapBufferProvider::ReleaseWGPUTextureAccessIfNeeded() {
  if (!current_swap_buffer_ || !current_swap_buffer_->mailbox_texture) {
    return;
  }

  // The client's lifetime is independent of the swap buffers that can be kept
  // alive longer due to pending shared image callbacks.
  if (client_) {
    client_->OnTextureTransferred();
  }

  current_swap_buffer_->mailbox_texture->Dissociate();
  current_swap_buffer_->mailbox_texture = nullptr;
}

void WebGPUSwapBufferProvider::DiscardCurrentSwapBuffer() {
  if (current_swap_buffer_ && current_swap_buffer_->mailbox_texture) {
    current_swap_buffer_->mailbox_texture->SetNeedsPresent(false);
  }
  ReleaseWGPUTextureAccessIfNeeded();
  current_swap_buffer_ = nullptr;
}

void WebGPUSwapBufferProvider::Neuter() {
  if (neutered_) {
    return;
  }

  if (layer_) {
    layer_->ClearClient();
    layer_ = nullptr;
  }

  DiscardCurrentSwapBuffer();
  client_ = nullptr;
  neutered_ = true;
}

scoped_refptr<WebGPUSwapBufferProvider::SwapBuffer>
WebGPUSwapBufferProvider::NewOrRecycledSwapBuffer(
    gpu::SharedImageInterface* sii,
    base::WeakPtr<WebGraphicsContext3DProviderWrapper> context_provider,
    const gfx::Size& size,
    SkAlphaType alpha_mode) {
  // Recycled SwapBuffers must be the same size.
  if (!unused_swap_buffers_.empty() &&
      unused_swap_buffers_.back()->size != size) {
    unused_swap_buffers_.clear();
  }

  if (unused_swap_buffers_.empty()) {
    // These SharedImages are read and written by WebGPU clients and can then be
    // sent off to the display compositor.
    uint32_t usage = gpu::SHARED_IMAGE_USAGE_WEBGPU_READ |
                     gpu::SHARED_IMAGE_USAGE_WEBGPU_WRITE |
                     gpu::SHARED_IMAGE_USAGE_WEBGPU_SWAP_CHAIN_TEXTURE |
                     gpu::SHARED_IMAGE_USAGE_DISPLAY_READ;
    if (usage_ & wgpu::TextureUsage::StorageBinding) {
      usage |= gpu::SHARED_IMAGE_USAGE_WEBGPU_STORAGE_TEXTURE;
    }
    auto client_shared_image = sii->CreateSharedImage(
        {Format(), size, PredefinedColorSpaceToGfxColorSpace(color_space_),
         kTopLeft_GrSurfaceOrigin, alpha_mode, usage,
         "WebGPUSwapBufferProvider"},
        gpu::kNullSurfaceHandle);
    CHECK(client_shared_image);
    gpu::SyncToken creation_token = sii->GenUnverifiedSyncToken();

    unused_swap_buffers_.push_back(base::MakeRefCounted<SwapBuffer>(
        std::move(context_provider), std::move(client_shared_image),
        creation_token, size));
    DCHECK_EQ(unused_swap_buffers_.back()->size, size);
  }

  scoped_refptr<SwapBuffer> swap_buffer =
      std::move(unused_swap_buffers_.back());
  unused_swap_buffers_.pop_back();

  DCHECK_EQ(swap_buffer->size, size);
  return swap_buffer;
}

void WebGPUSwapBufferProvider::RecycleSwapBuffer(
    scoped_refptr<SwapBuffer> swap_buffer) {
  // We don't want to keep an arbitrary large number of swap buffers.
  if (unused_swap_buffers_.size() >
      static_cast<unsigned int>(kMaxRecycledSwapBuffers))
    return;

  unused_swap_buffers_.push_back(std::move(swap_buffer));
}

scoped_refptr<WebGPUMailboxTexture> WebGPUSwapBufferProvider::GetNewTexture(
    const wgpu::TextureDescriptor& desc,
    SkAlphaType alpha_mode) {
  DCHECK_EQ(desc.usage, usage_);
  DCHECK_EQ(WGPUFormatToViz(desc.format), format_);
  DCHECK_EQ(desc.dimension, wgpu::TextureDimension::e2D);
  DCHECK_EQ(desc.size.depthOrArrayLayers, 1u);
  DCHECK_EQ(desc.mipLevelCount, 1u);
  DCHECK_EQ(desc.sampleCount, 1u);

  if (desc.nextInChain) {
    // The internal usage descriptor is the only valid struct to chain.
    CHECK_EQ(desc.nextInChain->sType,
             wgpu::SType::DawnTextureInternalUsageDescriptor);
    CHECK_EQ(desc.nextInChain->nextInChain, nullptr);
  }

  auto context_provider = GetContextProviderWeakPtr();
  if (!context_provider) {
    return nullptr;
  }

  gfx::Size size(desc.size.width, desc.size.height);
  if (size.IsEmpty()) {
    return nullptr;
  }

  if (size.width() > max_texture_size_ || size.height() > max_texture_size_) {
    LOG(ERROR) << "GetNewTexture(): invalid size " << size.width() << "x"
               << size.height();
    return nullptr;
  }

  // Create a new swap buffer.
  current_swap_buffer_ = NewOrRecycledSwapBuffer(
      context_provider->ContextProvider()->SharedImageInterface(),
      context_provider, size, alpha_mode);

  // Make a mailbox texture from the swap buffer.
  current_swap_buffer_->mailbox_texture =
      WebGPUMailboxTexture::FromExistingMailbox(
          dawn_control_client_, device_, desc,
          current_swap_buffer_->shared_image->mailbox(),
          // Wait on the last usage of this swap buffer.
          current_swap_buffer_->access_finished_token,
          gpu::webgpu::WEBGPU_MAILBOX_DISCARD,
          // When the mailbox texture is dissociated, set the access finished
          // token back on the swap buffer for the next time it is used.
          base::BindOnce(
              [](scoped_refptr<SwapBuffer> swap_buffer,
                 const gpu::SyncToken& access_finished_token) {
                swap_buffer->access_finished_token = access_finished_token;
              },
              current_swap_buffer_));

  if (!layer_) {
    // Create a layer that will be used by the canvas and will ask for a
    // SharedImage each frame.
    layer_ = cc::TextureLayer::CreateForMailbox(this);
    layer_->SetIsDrawable(true);
    layer_->SetFlipped(false);
    layer_->SetNearestNeighbor(filter_quality_ ==
                               cc::PaintFlags::FilterQuality::kNone);
    // TODO(cwallez@chromium.org): These flags aren't taken into account when
    // the layer is promoted to an overlay. Make sure we have fallback /
    // emulation paths to keep the rendering correct in that cases.
    layer_->SetPremultipliedAlpha(true);

    if (client_) {
      client_->SetNeedsCompositingUpdate();
    }
  }

  // When the page request a texture it means we'll need to present it on the
  // next animation frame.
  layer_->SetNeedsDisplay();
  layer_->SetContentsOpaque(alpha_mode == kOpaque_SkAlphaType);
  layer_->SetBlendBackgroundColor(alpha_mode != kOpaque_SkAlphaType);

  return current_swap_buffer_->mailbox_texture;
}

WebGPUSwapBufferProvider::WebGPUMailboxTextureAndSize
WebGPUSwapBufferProvider::GetLastWebGPUMailboxTextureAndSize() const {
  // It's possible this is called after the canvas context current texture has
  // been destroyed, but `current_swap_buffer_` is still available e.g. when the
  // context is used offscreen only.
  auto latest_swap_buffer =
      current_swap_buffer_ ? current_swap_buffer_ : last_swap_buffer_;
  auto context_provider = GetContextProviderWeakPtr();
  if (!latest_swap_buffer || !context_provider) {
    return WebGPUMailboxTextureAndSize(nullptr, gfx::Size());
  }

  wgpu::TextureDescriptor desc = {
      .usage = usage_,
  };

  return WebGPUMailboxTextureAndSize(
      WebGPUMailboxTexture::FromExistingMailbox(
          dawn_control_client_, device_, desc,
          latest_swap_buffer->shared_image->mailbox(),
          latest_swap_buffer->access_finished_token,
          gpu::webgpu::WEBGPU_MAILBOX_NONE),
      latest_swap_buffer->size);
}

base::WeakPtr<WebGraphicsContext3DProviderWrapper>
WebGPUSwapBufferProvider::GetContextProviderWeakPtr() const {
  return dawn_control_client_->GetContextProviderWeakPtr();
}

bool WebGPUSwapBufferProvider::PrepareTransferableResource(
    cc::SharedBitmapIdRegistrar* bitmap_registrar,
    viz::TransferableResource* out_resource,
    viz::ReleaseCallback* out_release_callback) {
  DCHECK(!neutered_);
  if (!current_swap_buffer_ || neutered_ || !GetContextProviderWeakPtr()) {
    return false;
  }

  ReleaseWGPUTextureAccessIfNeeded();

  // Populate the output resource
  // NOTE: This call is used to match the previous behavior of hardcoding
  // GL_TEXTURE_2D for non-MacOS and using the platform-specific texture target
  // for MacOS.
  // TODO(crbug.com/41494843): Replace this with calling
  // the universal ClientSharedImage::GetTextureTarget() once that rolls out
  // safely.
  uint32_t texture_target =
      current_swap_buffer_->shared_image->GetTextureTargetForOverlays();

  // On macOS, shared images are backed by IOSurfaces, meaning that they are
  // overlay candidates.
#if BUILDFLAG(IS_MAC)
  const bool is_overlay_candidate = true;
#else
  const bool is_overlay_candidate = false;
#endif
  *out_resource = viz::TransferableResource::MakeGpu(
      current_swap_buffer_->shared_image, texture_target,
      current_swap_buffer_->access_finished_token, current_swap_buffer_->size,
      Format(), is_overlay_candidate,
      viz::TransferableResource::ResourceSource::kWebGPUSwapBuffer);
  out_resource->color_space = PredefinedColorSpaceToGfxColorSpace(color_space_);
  out_resource->hdr_metadata = hdr_metadata_;

  // This holds a ref on the SwapBuffers that will keep it alive until the
  // mailbox is released (and while the release callback is running).
  *out_release_callback =
      WTF::BindOnce(&WebGPUSwapBufferProvider::MailboxReleased,
                    scoped_refptr<WebGPUSwapBufferProvider>(this),
                    std::move(current_swap_buffer_));

  return true;
}

bool WebGPUSwapBufferProvider::CopyToVideoFrame(
    WebGraphicsContext3DVideoFramePool* frame_pool,
    SourceDrawingBuffer src_buffer,
    const gfx::ColorSpace& dst_color_space,
    WebGraphicsContext3DVideoFramePool::FrameReadyCallback callback) {
  DCHECK(!neutered_);
  if (!current_swap_buffer_ || neutered_ || !GetContextProviderWeakPtr()) {
    return false;
  }

  DCHECK(frame_pool);

  auto* frame_pool_ri = frame_pool->GetRasterInterface();
  DCHECK(frame_pool_ri);

  // Copy kFrontBuffer to a video frame is not supported
  DCHECK_EQ(src_buffer, kBackBuffer);

  // For a conversion from swap buffer's texture to video frame, we do it
  // using WebGraphicsContext3DVideoFramePool's graphics context. Thus, we
  // need to release WebGPU/Dawn's context's access to the texture.
  ReleaseWGPUTextureAccessIfNeeded();

  // NOTE: This call is used to match the previous behavior of hardcoding
  // GL_TEXTURE_2D for non-MacOS and using the platform-specific texture target
  // for MacOS.
  // TODO(crbug.com/41494843): Replace this with calling
  // the universal ClientSharedImage::GetTextureTarget() once that rolls out
  // safely.
  uint32_t texture_target =
      current_swap_buffer_->shared_image->GetTextureTargetForOverlays();

  gpu::MailboxHolder mailbox_holder(
      current_swap_buffer_->shared_image->mailbox(),
      current_swap_buffer_->access_finished_token, texture_target);

  if (frame_pool->CopyRGBATextureToVideoFrame(
          Format(), current_swap_buffer_->size,
          PredefinedColorSpaceToGfxColorSpace(color_space_),
          kTopLeft_GrSurfaceOrigin, mailbox_holder, dst_color_space,
          std::move(callback))) {
    // Subsequent access to this swap buffer (either webgpu or compositor) must
    // wait for the copy operation to finish.
    frame_pool_ri->GenUnverifiedSyncTokenCHROMIUM(
        current_swap_buffer_->access_finished_token.GetData());
    return true;
  }
  return false;
}

void WebGPUSwapBufferProvider::MailboxReleased(
    scoped_refptr<SwapBuffer> swap_buffer,
    const gpu::SyncToken& sync_token,
    bool lost_resource) {
  // Update the SyncToken to ensure that we will wait for it even if we
  // immediately destroy this buffer.
  swap_buffer->access_finished_token = sync_token;

  if (lost_resource)
    return;

  if (last_swap_buffer_) {
    RecycleSwapBuffer(std::move(last_swap_buffer_));
  }

  last_swap_buffer_ = std::move(swap_buffer);
}

WebGPUSwapBufferProvider::SwapBuffer::SwapBuffer(
    base::WeakPtr<WebGraphicsContext3DProviderWrapper> context_provider,
    scoped_refptr<gpu::ClientSharedImage> shared_image,
    gpu::SyncToken creation_token,
    gfx::Size size)
    : size(size),
      shared_image(std::move(shared_image)),
      context_provider(context_provider),
      access_finished_token(creation_token) {}

WebGPUSwapBufferProvider::SwapBuffer::~SwapBuffer() {
  if (context_provider) {
    gpu::SharedImageInterface* sii =
        context_provider->ContextProvider()->SharedImageInterface();
    sii->DestroySharedImage(access_finished_token, std::move(shared_image));
  }
}

gpu::Mailbox WebGPUSwapBufferProvider::GetCurrentMailboxForTesting() const {
  DCHECK(current_swap_buffer_);
  DCHECK(current_swap_buffer_->shared_image);
  return current_swap_buffer_->shared_image->mailbox();
}
}  // namespace blink
