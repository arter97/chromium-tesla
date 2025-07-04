// Copyright 2017 The Dawn & Tint Authors
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice, this
//    list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution.
//
// 3. Neither the name of the copyright holder nor the names of its
//    contributors may be used to endorse or promote products derived from
//    this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
// FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
// DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
// CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
// OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#ifndef SRC_DAWN_NATIVE_DEVICE_H_
#define SRC_DAWN_NATIVE_DEVICE_H_

#include <shared_mutex>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "dawn/common/ContentLessObjectCache.h"
#include "dawn/common/Mutex.h"
#include "dawn/common/NonMovable.h"
#include "dawn/common/StackAllocated.h"
#include "dawn/native/CacheKey.h"
#include "dawn/native/Commands.h"
#include "dawn/native/ComputePipeline.h"
#include "dawn/native/CreatePipelineAsyncEvent.h"
#include "dawn/native/Error.h"
#include "dawn/native/ErrorSink.h"
#include "dawn/native/ExecutionQueue.h"
#include "dawn/native/Features.h"
#include "dawn/native/Format.h"
#include "dawn/native/Forward.h"
#include "dawn/native/Limits.h"
#include "dawn/native/ObjectBase.h"
#include "dawn/native/ObjectType_autogen.h"
#include "dawn/native/RefCountedWithExternalCount.h"
#include "dawn/native/Toggles.h"
#include "dawn/native/UsageValidationMode.h"
#include "partition_alloc/pointers/raw_ptr.h"

#include "dawn/native/DawnNative.h"
#include "dawn/native/dawn_platform.h"

namespace dawn::platform {
class WorkerTaskPool;
}  // namespace dawn::platform

namespace dawn::native {
class AsyncTaskManager;
class AttachmentState;
class AttachmentStateBlueprint;
class Blob;
class BlobCache;
class CallbackTaskManager;
class DynamicUploader;
class ErrorScopeStack;
class SharedTextureMemory;
class OwnedCompilationMessages;
struct CallbackTask;
struct InternalPipelineStore;
struct ShaderModuleParseResult;

class DeviceBase : public ErrorSink, public RefCountedWithExternalCount {
  public:
    struct DeviceLostEvent final : public EventManager::TrackedEvent {
        // TODO(https://crbug.com/dawn/2465): Pass just the DeviceLostCallbackInfo when setters are
        // deprecated. Creates and sets the device lost event for the given device if applicable. If
        // the device is nullptr, an event is still created, but the caller owns the last ref of the
        // event. When passing a device, note that device construction can be successful but fail
        // later at initialization, and this should only be called with the device if initialization
        // was successful.
        static Ref<DeviceLostEvent> Create(const DeviceDescriptor* descriptor);

        // Event result fields need to be public so that they can easily be updated prior to
        // completing the event.
        wgpu::DeviceLostReason mReason;
        std::string mMessage;

        wgpu::DeviceLostCallbackNew mCallback = nullptr;
        // TODO(https://crbug.com/dawn/2465): Remove old callback when setters are deprecated, and
        // move userdata into private.
        wgpu::DeviceLostCallback mOldCallback = nullptr;
        raw_ptr<void> mUserdata;
        // Note that the device is set when the event is passed to construct a device.
        Ref<DeviceBase> mDevice = nullptr;

      private:
        explicit DeviceLostEvent(const DeviceLostCallbackInfo& callbackInfo);
        DeviceLostEvent(wgpu::DeviceLostCallback oldCallback, void* userdata);
        ~DeviceLostEvent() override;

        void Complete(EventCompletionType completionType) override;
    };

    DeviceBase(AdapterBase* adapter,
               const UnpackedPtr<DeviceDescriptor>& descriptor,
               const TogglesState& deviceToggles,
               Ref<DeviceLostEvent>&& lostEvent);
    ~DeviceBase() override;

    // Handles the error, causing a device loss if applicable. Almost always when a device loss
    // occurs because of an error, we want to call the device loss callback with an undefined
    // reason, but the ForceLoss API allows for an injection of the reason, hence the default
    // argument. The `additionalAllowedErrors` mask allows specifying additional errors are allowed
    // (on top of validation and device loss errors). Note that "allowed" is defined as surfacing to
    // users as the respective error rather than causing a device loss instead.
    void HandleError(std::unique_ptr<ErrorData> error,
                     InternalErrorType additionalAllowedErrors = InternalErrorType::None,
                     WGPUDeviceLostReason lost_reason = WGPUDeviceLostReason_Unknown);

    MaybeError ValidateObject(const ApiObjectBase* object) const;

    // TODO(dawn:1702) Remove virtual when we mock the adapter.
    virtual InstanceBase* GetInstance() const;

    AdapterBase* GetAdapter() const;
    PhysicalDeviceBase* GetPhysicalDevice() const;
    virtual dawn::platform::Platform* GetPlatform() const;

    // Returns the Format corresponding to the wgpu::TextureFormat or an error if the format
    // isn't a valid wgpu::TextureFormat or isn't supported by this device.
    // The pointer returned has the same lifetime as the device.
    ResultOrError<const Format*> GetInternalFormat(wgpu::TextureFormat format) const;

    // Returns the Format corresponding to the wgpu::TextureFormat and assumes the format is
    // valid and supported.
    // The reference returned has the same lifetime as the device.
    const Format& GetValidInternalFormat(wgpu::TextureFormat format) const;
    const Format& GetValidInternalFormat(FormatIndex formatIndex) const;
    // Get compatible view formats. The returned span contains all compatible formats not equal to
    // `format`.
    std::vector<const Format*> GetCompatibleViewFormats(const Format& format) const;

    virtual ResultOrError<Ref<CommandBufferBase>> CreateCommandBuffer(
        CommandEncoder* encoder,
        const CommandBufferDescriptor* descriptor) = 0;

    // Many Dawn objects are completely immutable once created which means that if two
    // creations are given the same arguments, they can return the same object. Reusing
    // objects will help make comparisons between objects by a single pointer comparison.
    //
    // Technically no object is immutable as they have a reference count, and an
    // application with reference-counting issues could "see" that objects are reused.
    // This is solved by automatic-reference counting, and also the fact that when using
    // the client-server wire every creation will get a different proxy object, with a
    // different reference count.
    //
    // When trying to create an object, we give both the descriptor and an example of what
    // the created object will be, the "blueprint". The blueprint is just a FooBase object
    // instead of a backend Foo object. If the blueprint doesn't match an object in the
    // cache, then the descriptor is used to make a new object.
    ResultOrError<Ref<BindGroupLayoutBase>> GetOrCreateBindGroupLayout(
        const BindGroupLayoutDescriptor* descriptor,
        PipelineCompatibilityToken pipelineCompatibilityToken = kExplicitPCT);

    BindGroupLayoutBase* GetEmptyBindGroupLayout();
    PipelineLayoutBase* GetEmptyPipelineLayout();

    ResultOrError<Ref<TextureViewBase>> GetOrCreatePlaceholderTextureViewForExternalTexture();

    ResultOrError<Ref<PipelineLayoutBase>> GetOrCreatePipelineLayout(
        const UnpackedPtr<PipelineLayoutDescriptor>& descriptor);

    ResultOrError<Ref<SamplerBase>> GetOrCreateSampler(const SamplerDescriptor* descriptor);

    ResultOrError<Ref<ShaderModuleBase>> GetOrCreateShaderModule(
        const UnpackedPtr<ShaderModuleDescriptor>& descriptor,
        ShaderModuleParseResult* parseResult,
        std::unique_ptr<OwnedCompilationMessages>* compilationMessages);

    Ref<AttachmentState> GetOrCreateAttachmentState(AttachmentState* blueprint);
    Ref<AttachmentState> GetOrCreateAttachmentState(
        const RenderBundleEncoderDescriptor* descriptor);
    Ref<AttachmentState> GetOrCreateAttachmentState(
        const UnpackedPtr<RenderPipelineDescriptor>& descriptor,
        const PipelineLayoutBase* layout);
    Ref<AttachmentState> GetOrCreateAttachmentState(
        const UnpackedPtr<RenderPassDescriptor>& descriptor);

    Ref<PipelineCacheBase> GetOrCreatePipelineCache(const CacheKey& key);

    // Object creation methods that be used in a reentrant manner.
    ResultOrError<Ref<BindGroupBase>> CreateBindGroup(
        const BindGroupDescriptor* descriptor,
        UsageValidationMode mode = UsageValidationMode::Default);
    ResultOrError<Ref<BindGroupLayoutBase>> CreateBindGroupLayout(
        const BindGroupLayoutDescriptor* descriptor,
        bool allowInternalBinding = false);
    ResultOrError<Ref<BufferBase>> CreateBuffer(const BufferDescriptor* rawDescriptor);
    ResultOrError<Ref<CommandEncoder>> CreateCommandEncoder(
        const CommandEncoderDescriptor* descriptor = nullptr);
    ResultOrError<Ref<ComputePipelineBase>> CreateComputePipeline(
        const ComputePipelineDescriptor* descriptor);
    ResultOrError<Ref<ComputePipelineBase>> CreateUninitializedComputePipeline(
        const ComputePipelineDescriptor* descriptor);
    ResultOrError<Ref<PipelineLayoutBase>> CreatePipelineLayout(
        const PipelineLayoutDescriptor* rawDescriptor,
        PipelineCompatibilityToken pipelineCompatibilityToken = kExplicitPCT);
    ResultOrError<Ref<QuerySetBase>> CreateQuerySet(const QuerySetDescriptor* descriptor);
    ResultOrError<Ref<RenderBundleEncoder>> CreateRenderBundleEncoder(
        const RenderBundleEncoderDescriptor* descriptor);
    ResultOrError<Ref<RenderPipelineBase>> CreateRenderPipeline(
        const RenderPipelineDescriptor* descriptor,
        bool allowInternalBinding = false);
    ResultOrError<Ref<RenderPipelineBase>> CreateUninitializedRenderPipeline(
        const RenderPipelineDescriptor* descriptor,
        bool allowInternalBinding = false);
    ResultOrError<Ref<SamplerBase>> CreateSampler(const SamplerDescriptor* descriptor = nullptr);
    ResultOrError<Ref<ShaderModuleBase>> CreateShaderModule(
        const ShaderModuleDescriptor* descriptor,
        std::unique_ptr<OwnedCompilationMessages>* compilationMessages = nullptr);
    // Deprecated: this was the way to create a SwapChain when it was explicitly manipulated by the
    // end user.
    ResultOrError<Ref<SwapChainBase>> CreateSwapChain(Surface* surface,
                                                      const SwapChainDescriptor* descriptor);
    ResultOrError<Ref<SwapChainBase>> CreateSwapChain(Surface* surface,
                                                      SwapChainBase* previousSwapChain,
                                                      const SurfaceConfiguration* config);
    ResultOrError<Ref<TextureBase>> CreateTexture(const TextureDescriptor* rawDescriptor);
    ResultOrError<Ref<TextureViewBase>> CreateTextureView(TextureBase* texture,
                                                          const TextureViewDescriptor* descriptor);

    ResultOrError<wgpu::TextureUsage> GetSupportedSurfaceUsage(const Surface* surface) const;

    // Implementation of API object creation methods. DO NOT use them in a reentrant manner.
    BindGroupBase* APICreateBindGroup(const BindGroupDescriptor* descriptor);
    BindGroupLayoutBase* APICreateBindGroupLayout(const BindGroupLayoutDescriptor* descriptor);
    BufferBase* APICreateBuffer(const BufferDescriptor* descriptor);
    CommandEncoder* APICreateCommandEncoder(const CommandEncoderDescriptor* descriptor);
    ComputePipelineBase* APICreateComputePipeline(const ComputePipelineDescriptor* descriptor);
    PipelineLayoutBase* APICreatePipelineLayout(const PipelineLayoutDescriptor* descriptor);
    QuerySetBase* APICreateQuerySet(const QuerySetDescriptor* descriptor);
    void APICreateComputePipelineAsync(const ComputePipelineDescriptor* descriptor,
                                       WGPUCreateComputePipelineAsyncCallback callback,
                                       void* userdata);
    Future APICreateComputePipelineAsyncF(
        const ComputePipelineDescriptor* descriptor,
        const CreateComputePipelineAsyncCallbackInfo& callbackInfo);
    Future APICreateComputePipelineAsync2(
        const ComputePipelineDescriptor* descriptor,
        const WGPUCreateComputePipelineAsyncCallbackInfo2& callbackInfo);
    void APICreateRenderPipelineAsync(const RenderPipelineDescriptor* descriptor,
                                      WGPUCreateRenderPipelineAsyncCallback callback,
                                      void* userdata);
    Future APICreateRenderPipelineAsyncF(const RenderPipelineDescriptor* descriptor,
                                         const CreateRenderPipelineAsyncCallbackInfo& callbackInfo);
    Future APICreateRenderPipelineAsync2(
        const RenderPipelineDescriptor* descriptor,
        const WGPUCreateRenderPipelineAsyncCallbackInfo2& callbackInfo);
    RenderBundleEncoder* APICreateRenderBundleEncoder(
        const RenderBundleEncoderDescriptor* descriptor);
    RenderPipelineBase* APICreateRenderPipeline(const RenderPipelineDescriptor* descriptor);
    ExternalTextureBase* APICreateExternalTexture(const ExternalTextureDescriptor* descriptor);
    SharedBufferMemoryBase* APIImportSharedBufferMemory(
        const SharedBufferMemoryDescriptor* descriptor);
    SharedTextureMemoryBase* APIImportSharedTextureMemory(
        const SharedTextureMemoryDescriptor* descriptor);
    SharedFenceBase* APIImportSharedFence(const SharedFenceDescriptor* descriptor);
    SamplerBase* APICreateSampler(const SamplerDescriptor* descriptor);
    ShaderModuleBase* APICreateShaderModule(const ShaderModuleDescriptor* descriptor);
    ShaderModuleBase* APICreateErrorShaderModule(const ShaderModuleDescriptor* descriptor,
                                                 const char* errorMessage);
    // TODO(crbug.com/dawn/2320): Remove after deprecation.
    SwapChainBase* APICreateSwapChain(Surface* surface, const SwapChainDescriptor* descriptor);
    TextureBase* APICreateTexture(const TextureDescriptor* descriptor);

    wgpu::TextureUsage APIGetSupportedSurfaceUsage(Surface* surface);

    InternalPipelineStore* GetInternalPipelineStore();

    // For Dawn Wire
    BufferBase* APICreateErrorBuffer(const BufferDescriptor* desc);
    ExternalTextureBase* APICreateErrorExternalTexture();
    TextureBase* APICreateErrorTexture(const TextureDescriptor* desc);

    AdapterBase* APIGetAdapter();
    QueueBase* APIGetQueue();

    wgpu::Status APIGetAHardwareBufferProperties(void* handle,
                                                 AHardwareBufferProperties* properties);
    wgpu::Status APIGetLimits(SupportedLimits* limits) const;
    bool APIHasFeature(wgpu::FeatureName feature) const;
    size_t APIEnumerateFeatures(wgpu::FeatureName* features) const;
    void APIInjectError(wgpu::ErrorType type, const char* message);
    bool APITick();
    void APIValidateTextureDescriptor(const TextureDescriptor* desc);

    void APISetDeviceLostCallback(wgpu::DeviceLostCallback callback, void* userdata);
    void APISetUncapturedErrorCallback(wgpu::ErrorCallback callback, void* userdata);
    void APISetLoggingCallback(wgpu::LoggingCallback callback, void* userdata);
    void APIPushErrorScope(wgpu::ErrorFilter filter);
    void APIPopErrorScope(wgpu::ErrorCallback callback, void* userdata);
    Future APIPopErrorScopeF(const PopErrorScopeCallbackInfo& callbackInfo);
    Future APIPopErrorScope2(const WGPUPopErrorScopeCallbackInfo2& callbackInfo);

    MaybeError ValidateIsAlive() const;

    BlobCache* GetBlobCache() const;
    Blob LoadCachedBlob(const CacheKey& key);
    void StoreCachedBlob(const CacheKey& key, const Blob& blob);

    MaybeError CopyFromStagingToBuffer(BufferBase* source,
                                       uint64_t sourceOffset,
                                       BufferBase* destination,
                                       uint64_t destinationOffset,
                                       uint64_t size);
    MaybeError CopyFromStagingToTexture(BufferBase* source,
                                        const TextureDataLayout& src,
                                        const TextureCopy& dst,
                                        const Extent3D& copySizePixels);

    DynamicUploader* GetDynamicUploader() const;

    // The device state which is a combination of creation state and loss state.
    //
    //   - BeingCreated: the device didn't finish creation yet and the frontend cannot be used
    //     (both for the application calling WebGPU, or re-entrant calls). No work exists on
    //     the GPU timeline.
    //   - Alive: the device is usable and might have work happening on the GPU timeline.
    //   - BeingDisconnected: the device is no longer usable because we are waiting for all
    //     work on the GPU timeline to finish. (this is to make validation prevent the
    //     application from adding more work during the transition from Available to
    //     Disconnected)
    //   - Disconnected: there is no longer work happening on the GPU timeline and the CPU data
    //     structures can be safely destroyed without additional synchronization.
    //   - Destroyed: the device is disconnected and resources have been reclaimed.
    enum class State {
        BeingCreated,
        Alive,
        BeingDisconnected,
        Disconnected,
        Destroyed,
    };
    State GetState() const;
    bool IsLost() const;
    ApiObjectList* GetObjectTrackingList(ObjectType type);
    const ApiObjectList* GetObjectTrackingList(ObjectType type) const;

    std::vector<const char*> GetTogglesUsed() const;
    const tint::wgsl::AllowedFeatures& GetWGSLAllowedFeatures() const;
    bool IsToggleEnabled(Toggle toggle) const;
    const TogglesState& GetTogglesState() const;
    bool IsValidationEnabled() const;
    bool IsRobustnessEnabled() const;
    bool IsCompatibilityMode() const;
    bool IsImmediateErrorHandlingEnabled() const;

    size_t GetLazyClearCountForTesting();
    void IncrementLazyClearCountForTesting();
    void EmitWarningOnce(const std::string& message);
    void EmitLog(const char* message);
    void EmitLog(WGPULoggingType loggingType, const char* message);
    void EmitCompilationLog(const ShaderModuleBase* module);
    void APIForceLoss(wgpu::DeviceLostReason reason, const char* message);
    QueueBase* GetQueue() const;

    friend class IgnoreLazyClearCountScope;

    MaybeError Tick();

    // TODO(crbug.com/dawn/839): Organize the below backend-specific parameters into the struct
    // BackendMetadata that we can query from the device.
    virtual uint32_t GetOptimalBytesPerRowAlignment() const = 0;
    virtual uint64_t GetOptimalBufferToTextureCopyOffsetAlignment() const = 0;
    virtual uint64_t GetBufferCopyOffsetAlignmentForDepthStencil() const;

    virtual float GetTimestampPeriodInNS() const = 0;

    virtual bool ShouldDuplicateNumWorkgroupsForDispatchIndirect(
        ComputePipelineBase* computePipeline) const;

    virtual bool MayRequireDuplicationOfIndirectParameters() const;

    virtual bool ShouldDuplicateParametersForDrawIndirect(
        const RenderPipelineBase* renderPipelineBase) const;

    // For OpenGL/OpenGL ES, we must apply the index buffer offset from SetIndexBuffer to the
    // firstIndex parameter in indirect buffers. This happens in the validation since it
    // copies the indirect buffers and updates them while validating.
    // See https://crbug.com/dawn/161
    virtual bool ShouldApplyIndexBufferOffsetToFirstIndex() const;

    // Whether the backend can use textureLoad() on a resolve target in the same render pass that it
    // will be resolved into.
    virtual bool CanTextureLoadResolveTargetInTheSameRenderpass() const;

    bool HasFeature(Feature feature) const;

    const CombinedLimits& GetLimits() const;

    AsyncTaskManager* GetAsyncTaskManager() const;
    CallbackTaskManager* GetCallbackTaskManager() const;
    dawn::platform::WorkerTaskPool* GetWorkerTaskPool() const;

    // Enqueue a successfully-create async pipeline creation callback.
    // TODO(dawn:2353): Remove.
    void AddRenderPipelineAsyncCallbackTask(Ref<RenderPipelineBase> pipeline,
                                            WGPUCreateRenderPipelineAsyncCallback callback,
                                            void* userdata);
    // Enqueue a failed async pipeline creation callback.
    // If the device is lost, then further errors should not be reported to
    // the application. Instead of an error, a successful callback is enqueued, using
    // an error pipeline created with `label`.
    // TODO(dawn:2353): Remove.
    void AddRenderPipelineAsyncCallbackTask(std::unique_ptr<ErrorData> error,
                                            const char* label,
                                            WGPUCreateRenderPipelineAsyncCallback callback,
                                            void* userdata);

    PipelineCompatibilityToken GetNextPipelineCompatibilityToken();

    const CacheKey& GetCacheKey() const;
    const std::string& GetLabel() const;
    void APISetLabel(const char* label);
    void APIDestroy();

    virtual void AppendDebugLayerMessages(ErrorData* error) {}
    virtual void AppendDeviceLostMessage(ErrorData* error) {}

    // It is guaranteed that the wrapped mutex will outlive the Device (if the Device is deleted
    // before the AutoLockAndHoldRef).
    [[nodiscard]] Mutex::AutoLockAndHoldRef GetScopedLockSafeForDelete();
    // This lock won't guarantee the wrapped mutex will be alive if the Device is deleted before the
    // AutoLock. It would crash if such thing happens.
    [[nodiscard]] Mutex::AutoLock GetScopedLock();

    // This method returns true if Feature::ImplicitDeviceSynchronization is turned on and the
    // device is locked by current thread. This method is only enabled when DAWN_ENABLE_ASSERTS is
    // turned on. Thus it should only be wrapped inside DAWN_ASSERT() macro. i.e.
    // DAWN_ASSERT(device.IsLockedByCurrentThread())
    bool IsLockedByCurrentThreadIfNeeded() const;

    Ref<ComputePipelineBase> AddOrGetCachedComputePipeline(
        Ref<ComputePipelineBase> computePipeline);
    Ref<RenderPipelineBase> AddOrGetCachedRenderPipeline(Ref<RenderPipelineBase> renderPipeline);

    void DumpMemoryStatistics(dawn::native::MemoryDump* dump) const;

    ResultOrError<Ref<BufferBase>> GetOrCreateTemporaryUniformBuffer(size_t size);

  protected:
    // Constructor used only for mocking and testing.
    DeviceBase();

    void ForceSetToggleForTesting(Toggle toggle, bool isEnabled);
    void ForceEnableFeatureForTesting(Feature feature);

    MaybeError Initialize(Ref<QueueBase> defaultQueue);
    void DestroyObjects();
    void Destroy();

    void EnableAdditionalWGSLExtension(tint::wgsl::Extension extension);

    virtual MaybeError GetAHardwareBufferPropertiesImpl(
        void* handle,
        AHardwareBufferProperties* properties) const {
        DAWN_UNREACHABLE();
    }

    // Device lost event needs to be protected for now because mock device needs it.
    // TODO(dawn:1702) Make this private and move the class in the implementation file when we mock
    // the adapter.
    Ref<DeviceLostEvent> mLostEvent = nullptr;

  private:
    void WillDropLastExternalRef() override;

    virtual ResultOrError<Ref<BindGroupBase>> CreateBindGroupImpl(
        const BindGroupDescriptor* descriptor) = 0;
    virtual ResultOrError<Ref<BindGroupLayoutInternalBase>> CreateBindGroupLayoutImpl(
        const BindGroupLayoutDescriptor* descriptor) = 0;
    virtual ResultOrError<Ref<BufferBase>> CreateBufferImpl(
        const UnpackedPtr<BufferDescriptor>& descriptor) = 0;
    virtual ResultOrError<Ref<ExternalTextureBase>> CreateExternalTextureImpl(
        const ExternalTextureDescriptor* descriptor);
    virtual ResultOrError<Ref<PipelineLayoutBase>> CreatePipelineLayoutImpl(
        const UnpackedPtr<PipelineLayoutDescriptor>& descriptor) = 0;
    virtual ResultOrError<Ref<QuerySetBase>> CreateQuerySetImpl(
        const QuerySetDescriptor* descriptor) = 0;
    virtual ResultOrError<Ref<SamplerBase>> CreateSamplerImpl(
        const SamplerDescriptor* descriptor) = 0;
    virtual ResultOrError<Ref<ShaderModuleBase>> CreateShaderModuleImpl(
        const UnpackedPtr<ShaderModuleDescriptor>& descriptor,
        ShaderModuleParseResult* parseResult,
        OwnedCompilationMessages* compilationMessages) = 0;
    // Note that previousSwapChain may be nullptr, or come from a different backend.
    virtual ResultOrError<Ref<SwapChainBase>> CreateSwapChainImpl(
        Surface* surface,
        SwapChainBase* previousSwapChain,
        const SurfaceConfiguration* config) = 0;
    virtual ResultOrError<Ref<TextureBase>> CreateTextureImpl(
        const UnpackedPtr<TextureDescriptor>& descriptor) = 0;
    virtual ResultOrError<Ref<TextureViewBase>> CreateTextureViewImpl(
        TextureBase* texture,
        const UnpackedPtr<TextureViewDescriptor>& descriptor) = 0;
    virtual Ref<ComputePipelineBase> CreateUninitializedComputePipelineImpl(
        const UnpackedPtr<ComputePipelineDescriptor>& descriptor) = 0;
    virtual Ref<RenderPipelineBase> CreateUninitializedRenderPipelineImpl(
        const UnpackedPtr<RenderPipelineDescriptor>& descriptor) = 0;
    virtual ResultOrError<Ref<SharedTextureMemoryBase>> ImportSharedTextureMemoryImpl(
        const SharedTextureMemoryDescriptor* descriptor);
    virtual ResultOrError<Ref<SharedBufferMemoryBase>> ImportSharedBufferMemoryImpl(
        const SharedBufferMemoryDescriptor* descriptor);
    virtual ResultOrError<Ref<SharedFenceBase>> ImportSharedFenceImpl(
        const SharedFenceDescriptor* descriptor);
    virtual void SetLabelImpl();

    virtual MaybeError TickImpl() = 0;
    void FlushCallbackTaskQueue();

    ResultOrError<Ref<BindGroupLayoutBase>> CreateEmptyBindGroupLayout();
    ResultOrError<Ref<PipelineLayoutBase>> CreateEmptyPipelineLayout();

    Ref<ComputePipelineBase> GetCachedComputePipeline(
        ComputePipelineBase* uninitializedComputePipeline);
    Ref<RenderPipelineBase> GetCachedRenderPipeline(
        RenderPipelineBase* uninitializedRenderPipeline);
    virtual Ref<PipelineCacheBase> GetOrCreatePipelineCacheImpl(const CacheKey& key);
    virtual void InitializeComputePipelineAsyncImpl(Ref<CreateComputePipelineAsyncEvent> event);
    virtual void InitializeRenderPipelineAsyncImpl(Ref<CreateRenderPipelineAsyncEvent> event);

    void ApplyFeatures(const UnpackedPtr<DeviceDescriptor>& deviceDescriptor);

    void SetWGSLExtensionAllowList();

    // ErrorSink implementation
    void ConsumeError(std::unique_ptr<ErrorData> error,
                      InternalErrorType additionalAllowedErrors = InternalErrorType::None) override;

    bool HasPendingTasks();
    bool IsDeviceIdle();

    // DestroyImpl is used to clean up and release resources used by device, does not wait for
    // GPU or check errors.
    virtual void DestroyImpl() = 0;

    virtual MaybeError CopyFromStagingToBufferImpl(BufferBase* source,
                                                   uint64_t sourceOffset,
                                                   BufferBase* destination,
                                                   uint64_t destinationOffset,
                                                   uint64_t size) = 0;
    virtual MaybeError CopyFromStagingToTextureImpl(const BufferBase* source,
                                                    const TextureDataLayout& src,
                                                    const TextureCopy& dst,
                                                    const Extent3D& copySizePixels) = 0;

    UncapturedErrorCallbackInfo mUncapturedErrorCallbackInfo;

    std::shared_mutex mLoggingMutex;
    wgpu::LoggingCallback mLoggingCallback = nullptr;
    raw_ptr<void> mLoggingUserdata = nullptr;

    std::unique_ptr<ErrorScopeStack> mErrorScopeStack;

    Ref<AdapterBase> mAdapter;

    // The object caches aren't exposed in the header as they would require a lot of
    // additional includes.
    struct Caches;
    std::unique_ptr<Caches> mCaches;

    Ref<BindGroupLayoutBase> mEmptyBindGroupLayout;
    Ref<PipelineLayoutBase> mEmptyPipelineLayout;

    Ref<TextureViewBase> mExternalTexturePlaceholderView;

    std::unique_ptr<DynamicUploader> mDynamicUploader;
    Ref<QueueBase> mQueue;

    std::atomic<uint32_t> mEmittedCompilationLogCount = 0;

    absl::flat_hash_set<std::string> mWarnings;

    State mState = State::BeingCreated;

    PerObjectType<ApiObjectList> mObjectLists;

    FormatTable mFormatTable;

    TogglesState mToggles;

    size_t mLazyClearCountForTesting = 0;
    std::atomic_uint64_t mNextPipelineCompatibilityToken;

    CombinedLimits mLimits;
    FeaturesSet mEnabledFeatures;
    tint::wgsl::AllowedFeatures mWGSLAllowedFeatures;

    std::unique_ptr<InternalPipelineStore> mInternalPipelineStore;
    Ref<BufferBase> mTemporaryUniformBuffer;

    Ref<CallbackTaskManager> mCallbackTaskManager;
    std::unique_ptr<dawn::platform::WorkerTaskPool> mWorkerTaskPool;

    // Ensure `mAsyncTaskManager` is always destroyed before mWorkerTaskPool
    std::unique_ptr<AsyncTaskManager> mAsyncTaskManager;
    std::string mLabel;

    CacheKey mDeviceCacheKey;
    std::unique_ptr<BlobCache> mBlobCache;

    // We cache this toggle so that we can check it without locking the device.
    bool mIsImmediateErrorHandlingEnabled = false;

    // This pointer is non-null if Feature::ImplicitDeviceSynchronization is turned on.
    Ref<Mutex> mMutex = nullptr;
};

ResultOrError<Ref<PipelineLayoutBase>> ValidateLayoutAndGetComputePipelineDescriptorWithDefaults(
    DeviceBase* device,
    const ComputePipelineDescriptor& descriptor,
    ComputePipelineDescriptor* outDescriptor);

ResultOrError<Ref<PipelineLayoutBase>> ValidateLayoutAndGetRenderPipelineDescriptorWithDefaults(
    DeviceBase* device,
    const RenderPipelineDescriptor& descriptor,
    RenderPipelineDescriptor* outDescriptor,
    bool allowInternalBinding = false);

class IgnoreLazyClearCountScope : public NonMovable, public StackAllocated {
  public:
    explicit IgnoreLazyClearCountScope(DeviceBase* device);
    ~IgnoreLazyClearCountScope();

  private:
    raw_ptr<DeviceBase> mDevice;
    size_t mLazyClearCountForTesting;
};

}  // namespace dawn::native

#endif  // SRC_DAWN_NATIVE_DEVICE_H_
