// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GPU_IPC_SERVICE_SHARED_IMAGE_STUB_H_
#define GPU_IPC_SERVICE_SHARED_IMAGE_STUB_H_

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "build/build_config.h"
#include "gpu/command_buffer/service/memory_tracking.h"
#include "gpu/command_buffer/service/sequence_id.h"
#include "gpu/command_buffer/service/sync_point_manager.h"
#include "gpu/ipc/common/command_buffer_id.h"
#include "gpu/ipc/common/gpu_channel.mojom.h"
#include "gpu/ipc/service/gpu_ipc_service_export.h"
#include "ui/gfx/gpu_extra_info.h"

namespace gfx {
#if BUILDFLAG(IS_WIN)
class D3DSharedFence;
#endif

struct GpuFenceHandle;
}  // namespace gfx

namespace gpu {
class SharedContextState;
struct Mailbox;
class GpuChannel;
class GpuChannelSharedImageInterface;
class SharedImageFactory;

class GPU_IPC_SERVICE_EXPORT SharedImageStub : public MemoryTracker {
 public:
  ~SharedImageStub() override;

  using SharedImageDestructionCallback =
      base::OnceCallback<void(const gpu::SyncToken&)>;

  static std::unique_ptr<SharedImageStub> Create(GpuChannel* channel,
                                                 int32_t route_id);

  // Executes a DeferredRequest routed to this stub by a GpuChannel.
  void ExecuteDeferredRequest(mojom::DeferredSharedImageRequestPtr request,
                              uint64_t release_count);

  bool GetGpuMemoryBufferHandleInfo(const gpu::Mailbox& mailbox,
                                    gfx::GpuMemoryBufferHandle& handle,
                                    viz::SharedImageFormat& format,
                                    gfx::Size& size,
                                    gfx::BufferUsage& buffer_usage);

  // MemoryTracker implementation:
  void TrackMemoryAllocatedChange(int64_t delta) override;
  uint64_t GetSize() const override;
  uint64_t ClientTracingId() const override;
  int ClientId() const override;
  uint64_t ContextGroupTracingId() const override;

  SequenceId sequence() const { return sequence_; }
  SharedImageFactory* factory() const { return factory_.get(); }
  GpuChannel* channel() const { return channel_; }
  SharedContextState* shared_context_state() { return context_state_.get(); }
  const scoped_refptr<gpu::GpuChannelSharedImageInterface>&
  shared_image_interface();

  SharedImageDestructionCallback GetSharedImageDestructionCallback(
      const Mailbox& mailbox);

  // NOTE: The below method is DEPRECATED for single planar formats eg. RGB
  // BufferFormats. Please use the equivalent method below it taking in single
  // planar SharedImageFormat with GpuMemoryBufferHandle.
  bool CreateSharedImage(const Mailbox& mailbox,
                         gfx::GpuMemoryBufferHandle handle,
                         gfx::BufferFormat format,
                         gfx::BufferPlane plane,
                         const gfx::Size& size,
                         const gfx::ColorSpace& color_space,
                         GrSurfaceOrigin surface_origin,
                         SkAlphaType alpha_type,
                         uint32_t usage,
                         std::string debug_label);
  bool CreateSharedImage(const Mailbox& mailbox,
                         gfx::GpuMemoryBufferHandle handle,
                         viz::SharedImageFormat format,
                         const gfx::Size& size,
                         const gfx::ColorSpace& color_space,
                         GrSurfaceOrigin surface_origin,
                         SkAlphaType alpha_type,
                         uint32_t usage,
                         std::string debug_label);

  bool UpdateSharedImage(const Mailbox& mailbox,
                         gfx::GpuFenceHandle in_fence_handle);

#if BUILDFLAG(IS_WIN)
  void CopyToGpuMemoryBufferAsync(const Mailbox& mailbox,
                                  uint64_t release_count,
                                  base::OnceCallback<void(bool)> callback);
#endif

#if BUILDFLAG(IS_FUCHSIA)
  void RegisterSysmemBufferCollection(zx::eventpair service_handle,
                                      zx::channel sysmem_token,
                                      gfx::BufferFormat format,
                                      gfx::BufferUsage usage,
                                      bool register_with_image_pipe);
#endif  // BUILDFLAG(IS_FUCHSIA)

  void SetGpuExtraInfo(const gfx::GpuExtraInfo& gpu_extra_info);

  bool MakeContextCurrent(bool needs_gl = false);

 private:
  SharedImageStub(GpuChannel* channel, int32_t route_id);

  // TODO(yzshen): Instead of having individual handlers release sync points,
  // change the scheduler to do the release at the end of each task.
  void OnCreateSharedImage(mojom::CreateSharedImageParamsPtr params,
                           uint64_t release_count);
  void OnCreateSharedImageWithData(
      mojom::CreateSharedImageWithDataParamsPtr params,
      uint64_t release_count);
  void OnCreateSharedImageWithBuffer(
      mojom::CreateSharedImageWithBufferParamsPtr params,
      uint64_t release_count);
  void OnCreateGMBSharedImage(mojom::CreateGMBSharedImageParamsPtr params,
                              uint64_t release_count);
  void OnUpdateSharedImage(const Mailbox& mailbox,
                           uint64_t release_count,
                           gfx::GpuFenceHandle in_fence_handle);
  void OnAddReference(const Mailbox& mailbox, uint64_t release_count);

  void OnDestroySharedImage(const Mailbox& mailbox);
  void OnRegisterSharedImageUploadBuffer(base::ReadOnlySharedMemoryRegion shm);
#if BUILDFLAG(IS_WIN)
  void OnCopyToGpuMemoryBuffer(const Mailbox& mailbox, uint64_t release_count);
  void OnCreateSwapChain(mojom::CreateSwapChainParamsPtr params,
                         uint64_t release_count);
  void OnPresentSwapChain(const Mailbox& mailbox, uint64_t release_count);
  void OnRegisterDxgiFence(const Mailbox& mailbox,
                           gfx::DXGIHandleToken dxgi_token,
                           gfx::GpuFenceHandle fence_handle);
  void OnUpdateDxgiFence(const Mailbox& mailbox,
                         gfx::DXGIHandleToken dxgi_token,
                         uint64_t fence_value);
  void OnUnregisterDxgiFence(const Mailbox& mailbox,
                             gfx::DXGIHandleToken dxgi_token);
#endif  // BUILDFLAG(IS_WIN)

  ContextResult Initialize();
  void OnError();

  // Wait on the sync token if any and destroy the shared image.
  void DestroySharedImage(const Mailbox& mailbox, const SyncToken& sync_token);

  std::string GetLabel(const std::string& debug_label) const;

  raw_ptr<GpuChannel> channel_;

  // While this is not a CommandBuffer, this provides a unique identifier for
  // a SharedImageStub, comprised of identifiers which it was already using.
  // TODO(jonross): Look into a rename of CommandBufferId to reflect that it can
  // be a unique identifier for numerous gpu constructs.
  CommandBufferId command_buffer_id_;
  scoped_refptr<GpuChannelSharedImageInterface>
      gpu_channel_shared_image_interface_;
  const SequenceId sequence_;
  scoped_refptr<gpu::SyncPointClientState> sync_point_client_state_;
  scoped_refptr<SharedContextState> context_state_;
  std::unique_ptr<SharedImageFactory> factory_;
  uint64_t size_ = 0;

  // Holds shared memory used in initial data uploads.
  base::ReadOnlySharedMemoryRegion upload_memory_;
  base::ReadOnlySharedMemoryMapping upload_memory_mapping_;

#if BUILDFLAG(IS_WIN)
  // Fences held by external processes. Registered and signaled from ipc
  // channel. Using DXGIHandleToken to identify the fence.
  base::flat_map<Mailbox, base::flat_set<scoped_refptr<gfx::D3DSharedFence>>>
      registered_dxgi_fences_;
#endif

  base::WeakPtrFactory<SharedImageStub> weak_factory_{this};
};

}  // namespace gpu

#endif  // GPU_IPC_SERVICE_SHARED_IMAGE_STUB_H_
