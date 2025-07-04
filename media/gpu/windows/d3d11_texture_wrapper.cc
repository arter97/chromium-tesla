// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/gpu/windows/d3d11_texture_wrapper.h"

#include <list>
#include <memory>
#include <utility>
#include <vector>

#include "base/task/bind_post_task.h"
#include "base/task/single_thread_task_runner.h"
#include "gpu/command_buffer/common/constants.h"
#include "gpu/command_buffer/common/shared_image_usage.h"
#include "gpu/command_buffer/service/dxgi_shared_handle_manager.h"
#include "gpu/command_buffer/service/shared_image/d3d_image_backing.h"
#include "gpu/command_buffer/service/shared_image/shared_image_format_service_utils.h"
#include "gpu/ipc/service/shared_image_stub.h"
#include "media/base/media_switches.h"
#include "media/base/win/mf_helpers.h"
#include "media/gpu/windows/d3d11_picture_buffer.h"
#include "media/gpu/windows/format_utils.h"
#include "mojo/public/cpp/bindings/callback_helpers.h"

namespace media {

namespace {

bool SupportsFormat(DXGI_FORMAT dxgi_format) {
  switch (dxgi_format) {
    case DXGI_FORMAT_NV12:
    case DXGI_FORMAT_P010:
    case DXGI_FORMAT_Y210:
    case DXGI_FORMAT_Y410:
    case DXGI_FORMAT_P016:
    case DXGI_FORMAT_Y216:
    case DXGI_FORMAT_Y416:
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_R10G10B10A2_UNORM:
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
      return true;
    default:
      return false;
  }
}

viz::SharedImageFormat DXGIFormatToMultiPlanarSharedImageFormat(
    DXGI_FORMAT dxgi_format) {
  switch (dxgi_format) {
    case DXGI_FORMAT_NV12:
      return viz::MultiPlaneFormat::kNV12;
    case DXGI_FORMAT_P010:
      return viz::MultiPlaneFormat::kP010;
    case DXGI_FORMAT_B8G8R8A8_UNORM:
      return viz::SinglePlaneFormat::kBGRA_8888;
    case DXGI_FORMAT_R10G10B10A2_UNORM:
      return viz::SinglePlaneFormat::kRGBA_1010102;
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
      return viz::SinglePlaneFormat::kRGBA_F16;
    default:
      NOTREACHED_IN_MIGRATION();
      return viz::SinglePlaneFormat::kBGRA_8888;
  }
}

}  // anonymous namespace

Texture2DWrapper::Texture2DWrapper() = default;

Texture2DWrapper::~Texture2DWrapper() = default;

DefaultTexture2DWrapper::DefaultTexture2DWrapper(
    const gfx::Size& size,
    const gfx::ColorSpace& color_space,
    DXGI_FORMAT dxgi_format,
    ComD3D11Device device)
    : size_(size),
      color_space_(color_space),
      dxgi_format_(dxgi_format),
      video_device_(std::move(device)) {}

DefaultTexture2DWrapper::~DefaultTexture2DWrapper() = default;

D3D11Status DefaultTexture2DWrapper::BeginSharedImageAccess() {
  if (shared_image_access_) {
    return D3D11Status::Codes::kOk;
  }

  if (shared_image_rep_) {
    TRACE_EVENT0("gpu", "D3D11TextureWrapper::BeginScopedWriteAccess");
    shared_image_access_ = shared_image_rep_->BeginScopedWriteAccess();
    if (!shared_image_access_) {
      return D3D11Status::Codes::
          kVideoDecodeImageRepresentationBeginScopedWriteAccessFailed;
    }
  }

  return D3D11Status::Codes::kOk;
}

D3D11Status DefaultTexture2DWrapper::ProcessTexture(
    const gfx::ColorSpace& input_color_space,
    gpu::MailboxHolder* mailbox_dest,
    gfx::ColorSpace* output_color_space) {
  // If we've received an error, then return it to our caller.  This is probably
  // from some previous operation.
  // TODO(liberato): Return the error.
  if (shared_image_access_) {
    TRACE_EVENT0("gpu", "D3D11TextureWrapper::EndScopedWriteAccess");
    shared_image_access_.reset();
  }

  *mailbox_dest = mailbox_holder_;

  // We're just binding, so the output and output color spaces are the same.
  *output_color_space = input_color_space;

  // TODO(hitawala): Possibly optimize this method as input and stored color
  // spaces should be same.
  CHECK_EQ(input_color_space, color_space_);

  return D3D11Status::Codes::kOk;
}

D3D11Status DefaultTexture2DWrapper::Init(
    scoped_refptr<base::SingleThreadTaskRunner> gpu_task_runner,
    GetCommandBufferHelperCB get_helper_cb,
    ComD3D11Texture2D texture,
    size_t array_slice,
    scoped_refptr<media::D3D11PictureBuffer> picture_buffer,
    Texture2DWrapper::PictureBufferGPUResourceInitDoneCB
        picture_buffer_gpu_resource_init_done_cb) {
  if (!SupportsFormat(dxgi_format_))
    return D3D11Status::Codes::kUnsupportedTextureFormatForBind;

  // Generate mailbox and holder.
  // TODO(liberato): Verify that this is really okay off the GPU main thread.
  // The current implementation is.
  gpu::Mailbox mailbox = gpu::Mailbox::Generate();
  mailbox_holder_ =
      gpu::MailboxHolder(mailbox, gpu::SyncToken(), GL_TEXTURE_EXTERNAL_OES);

  picture_buffer_gpu_resource_init_done_cb_ =
      std::move(picture_buffer_gpu_resource_init_done_cb);

  // Start construction of the GpuResources.
  // We send the texture itself, since we assume that we're using the angle
  // device for decoding.  Sharing seems not to work very well.  Otherwise, we
  // would create the texture with KEYED_MUTEX and NTHANDLE, then send along
  // a handle that we get from |texture| as an IDXGIResource1.
  auto on_error_cb = base::BindPostTaskToCurrentDefault(base::BindOnce(
      &DefaultTexture2DWrapper::OnError, weak_factory_.GetWeakPtr()));

  auto gpu_resource_init_cb = base::BindPostTaskToCurrentDefault(
      base::BindOnce(&DefaultTexture2DWrapper::OnGPUResourceInitDone,
                     weak_factory_.GetWeakPtr()));
  gpu_resources_ = base::SequenceBound<GpuResources>(
      std::move(gpu_task_runner), std::move(on_error_cb),
      std::move(get_helper_cb), std::move(mailbox), size_, color_space_,
      dxgi_format_, video_device_, texture, array_slice,
      std::move(picture_buffer), std::move(gpu_resource_init_cb));
  return D3D11Status::Codes::kOk;
}

void DefaultTexture2DWrapper::OnError(D3D11Status status) {
  if (!received_error_)
    received_error_ = status;
}

void DefaultTexture2DWrapper::SetStreamHDRMetadata(
    const gfx::HDRMetadata& stream_metadata) {}

void DefaultTexture2DWrapper::SetDisplayHDRMetadata(
    const DXGI_HDR_METADATA_HDR10& dxgi_display_metadata) {}

void DefaultTexture2DWrapper::OnGPUResourceInitDone(
    scoped_refptr<media::D3D11PictureBuffer> picture_buffer,
    std::unique_ptr<gpu::VideoDecodeImageRepresentation> shared_image_rep) {
  DCHECK(shared_image_rep);
  shared_image_rep_ = std::move(shared_image_rep);
  std::move(picture_buffer_gpu_resource_init_done_cb_)
      .Run(std::move(picture_buffer));
}

DefaultTexture2DWrapper::GpuResources::GpuResources(
    OnErrorCB on_error_cb,
    GetCommandBufferHelperCB get_helper_cb,
    const gpu::Mailbox& mailbox,
    const gfx::Size& size,
    const gfx::ColorSpace& color_space,
    DXGI_FORMAT dxgi_format,
    ComD3D11Device video_device,
    ComD3D11Texture2D texture,
    size_t array_slice,
    scoped_refptr<media::D3D11PictureBuffer> picture_buffer,
    GPUResourceInitCB gpu_resource_init_cb) {
  CHECK(texture);

  helper_ = get_helper_cb.Run();
  if (!helper_) {
    std::move(on_error_cb)
        .Run(std::move(D3D11Status::Codes::kGetCommandBufferHelperFailed));
    return;
  }

  // Usage flags to allow the display compositor to draw from it, video to
  // decode from it, and webgl/canvas to read from it.
  uint32_t usage =
      gpu::SHARED_IMAGE_USAGE_VIDEO_DECODE |
      gpu::SHARED_IMAGE_USAGE_GLES2_READ | gpu::SHARED_IMAGE_USAGE_RASTER_READ |
      gpu::SHARED_IMAGE_USAGE_DISPLAY_READ | gpu::SHARED_IMAGE_USAGE_SCANOUT;

  scoped_refptr<gpu::DXGISharedHandleState> dxgi_shared_handle_state;
  D3D11_TEXTURE2D_DESC desc = {};
  texture->GetDesc(&desc);
  // Create shared handle for shareable output texture.
  if (desc.MiscFlags & D3D11_RESOURCE_MISC_SHARED_NTHANDLE) {
    ComDXGIResource1 dxgi_resource;
    HRESULT hr = texture.As(&dxgi_resource);
    if (FAILED(hr)) {
      DLOG(ERROR) << "QueryInterface for IDXGIResource failed with error "
                  << std::hex << hr;
      std::move(on_error_cb)
          .Run(std::move(D3D11Status::Codes::kCreateSharedHandleFailed));
      return;
    }

    // WebGPU will potentially read directly from this texture.
    usage |= gpu::SHARED_IMAGE_USAGE_WEBGPU_READ;

    HANDLE shared_handle = nullptr;
    hr = dxgi_resource->CreateSharedHandle(
        nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE,
        nullptr, &shared_handle);
    if (FAILED(hr)) {
      DLOG(ERROR) << "CreateSharedHandle failed with error " << std::hex << hr;
      std::move(on_error_cb)
          .Run(std::move(D3D11Status::Codes::kCreateSharedHandleFailed));
      return;
    }

    dxgi_shared_handle_state =
        helper_->GetDXGISharedHandleManager()->CreateAnonymousSharedHandleState(
            base::win::ScopedHandle(shared_handle), texture);
  }

  auto caps =
      helper_->GetSharedImageStub()->shared_context_state()->GetGLFormatCaps();
  // The target must be GL_TEXTURE_EXTERNAL_OES as the texture is not created
  // with D3D11_BIND_RENDER_TARGET bind flag and so it cannot be bound to the
  // framebuffer. To prevent Skia trying to bind it for read pixels, we need
  // it to be GL_TEXTURE_EXTERNAL_OES.
  std::unique_ptr<gpu::SharedImageBacking> backing =
      gpu::D3DImageBacking::Create(
          mailbox, DXGIFormatToMultiPlanarSharedImageFormat(dxgi_format), size,
          color_space, kTopLeft_GrSurfaceOrigin, kPremul_SkAlphaType, usage,
          "VideoTexture", texture, std::move(dxgi_shared_handle_state), caps,
          GL_TEXTURE_EXTERNAL_OES, array_slice, /*plane_index=*/0u);

  if (!backing) {
    std::move(on_error_cb)
        .Run(std::move(D3D11Status::Codes::kCreateSharedImageFailed));
    return;
  }
  // Need to clear the backing since the D3D11 Video Decoder will initialize
  // the textures.
  backing->SetCleared();

  auto* shared_image_manager = helper_->GetSharedImageManager();
  auto* memory_type_tracker = helper_->GetMemoryTypeTracker();
  shared_image_ =
      shared_image_manager->Register(std::move(backing), memory_type_tracker);

  std::unique_ptr<gpu::VideoDecodeImageRepresentation> shared_image_rep =
      shared_image_manager->ProduceVideoDecode(video_device.Get(), mailbox,
                                               memory_type_tracker);
  if (!shared_image_rep) {
    std::move(on_error_cb)
        .Run(D3D11Status::Codes::kProduceVideoDecodeImageRepresentationFailed);
    shared_image_ = nullptr;
    return;
  }

  std::move(gpu_resource_init_cb)
      .Run(std::move(picture_buffer), std::move(shared_image_rep));
}

DefaultTexture2DWrapper::GpuResources::~GpuResources() = default;

}  // namespace media
