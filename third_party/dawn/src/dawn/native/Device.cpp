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

#include "dawn/native/Device.h"

#include <algorithm>
#include <array>
#include <mutex>
#include <utility>

#include "absl/container/flat_hash_set.h"
#include "absl/strings/str_format.h"
#include "dawn/common/Log.h"
#include "dawn/common/Ref.h"
#include "dawn/common/Version_autogen.h"
#include "dawn/native/AsyncTask.h"
#include "dawn/native/AttachmentState.h"
#include "dawn/native/BindGroup.h"
#include "dawn/native/BindGroupLayout.h"
#include "dawn/native/BlitBufferToDepthStencil.h"
#include "dawn/native/BlobCache.h"
#include "dawn/native/Buffer.h"
#include "dawn/native/ChainUtils.h"
#include "dawn/native/CommandBuffer.h"
#include "dawn/native/CommandEncoder.h"
#include "dawn/native/CompilationMessages.h"
#include "dawn/native/CreatePipelineAsyncEvent.h"
#include "dawn/native/DawnNative.h"
#include "dawn/native/DynamicUploader.h"
#include "dawn/native/Error.h"
#include "dawn/native/ErrorData.h"
#include "dawn/native/ErrorInjector.h"
#include "dawn/native/ErrorScope.h"
#include "dawn/native/ExternalTexture.h"
#include "dawn/native/Instance.h"
#include "dawn/native/InternalPipelineStore.h"
#include "dawn/native/ObjectType_autogen.h"
#include "dawn/native/PhysicalDevice.h"
#include "dawn/native/PipelineCache.h"
#include "dawn/native/QuerySet.h"
#include "dawn/native/Queue.h"
#include "dawn/native/RenderBundleEncoder.h"
#include "dawn/native/RenderPipeline.h"
#include "dawn/native/Sampler.h"
#include "dawn/native/SharedBufferMemory.h"
#include "dawn/native/SharedFence.h"
#include "dawn/native/SharedTextureMemory.h"
#include "dawn/native/Surface.h"
#include "dawn/native/SwapChain.h"
#include "dawn/native/Texture.h"
#include "dawn/native/ValidationUtils_autogen.h"
#include "dawn/native/utils/WGPUHelpers.h"
#include "dawn/platform/DawnPlatform.h"
#include "dawn/platform/metrics/HistogramMacros.h"
#include "dawn/platform/tracing/TraceEvent.h"
#include "dawn/webgpu.h"
#include "partition_alloc/pointers/raw_ptr.h"

namespace dawn::native {

// DeviceBase sub-structures

struct DeviceBase::Caches {
    ContentLessObjectCache<AttachmentState> attachmentStates;
    ContentLessObjectCache<BindGroupLayoutInternalBase> bindGroupLayouts;
    ContentLessObjectCache<ComputePipelineBase> computePipelines;
    ContentLessObjectCache<PipelineLayoutBase> pipelineLayouts;
    ContentLessObjectCache<RenderPipelineBase> renderPipelines;
    ContentLessObjectCache<SamplerBase> samplers;
    ContentLessObjectCache<ShaderModuleBase> shaderModules;
};

// Tries to find an object in the cache, creating and inserting into the cache if not found.
template <typename RefCountedT, typename CreateFn>
auto GetOrCreate(ContentLessObjectCache<RefCountedT>& cache,
                 RefCountedT* blueprint,
                 CreateFn createFn) {
    using ReturnType = decltype(createFn());

    // If we find the blueprint in the cache we can just return it.
    Ref<RefCountedT> result = cache.Find(blueprint);
    if (result != nullptr) {
        return ReturnType(result);
    }

    using UnwrappedReturnType = typename detail::UnwrapResultOrError<ReturnType>::type;
    static_assert(std::is_same_v<UnwrappedReturnType, Ref<RefCountedT>>,
                  "CreateFn should return an unwrapped type that is the same as Ref<RefCountedT>.");

    // Create the result and try inserting it. Note that inserts can race because the critical
    // sections here is disjoint, hence the checks to verify whether this thread inserted.
    if constexpr (!detail::IsResultOrError<ReturnType>::value) {
        result = createFn();
    } else {
        auto resultOrError = createFn();
        if (DAWN_UNLIKELY(resultOrError.IsError())) {
            return ReturnType(std::move(resultOrError.AcquireError()));
        }
        result = resultOrError.AcquireSuccess();
    }
    DAWN_ASSERT(result.Get() != nullptr);

    bool inserted = false;
    std::tie(result, inserted) = cache.Insert(result.Get());
    return ReturnType(result);
}

namespace {

struct LoggingCallbackTask : CallbackTask {
  public:
    LoggingCallbackTask() = delete;
    LoggingCallbackTask(wgpu::LoggingCallback loggingCallback,
                        WGPULoggingType loggingType,
                        const char* message,
                        void* userdata)
        : mCallback(loggingCallback),
          mLoggingType(loggingType),
          mMessage(message),
          mUserdata(userdata) {
        // Since the FinishImpl() will be called in uncertain future in which time the message
        // may already disposed, we must keep a local copy in the CallbackTask.
    }

  private:
    void FinishImpl() override { mCallback(mLoggingType, mMessage.c_str(), mUserdata); }

    void HandleShutDownImpl() override {
        // Do the logging anyway
        mCallback(mLoggingType, mMessage.c_str(), mUserdata);
    }

    void HandleDeviceLossImpl() override { mCallback(mLoggingType, mMessage.c_str(), mUserdata); }

    // As all deferred callback tasks will be triggered before modifying the registered
    // callback or shutting down, we are ensured that callback function and userdata pointer
    // stored in tasks is valid when triggered.
    wgpu::LoggingCallback mCallback;
    WGPULoggingType mLoggingType;
    std::string mMessage;
    raw_ptr<void> mUserdata;
};

static constexpr UncapturedErrorCallbackInfo kEmptyUncapturedErrorCallbackInfo = {nullptr, nullptr,
                                                                                  nullptr};

}  // anonymous namespace

DeviceBase::DeviceLostEvent::DeviceLostEvent(const DeviceLostCallbackInfo& callbackInfo)
    : TrackedEvent(callbackInfo.mode, SystemEvent::CreateNonProgressingEvent()),
      mCallback(callbackInfo.callback),
      mUserdata(callbackInfo.userdata) {}

DeviceBase::DeviceLostEvent::DeviceLostEvent(wgpu::DeviceLostCallback oldCallback, void* userdata)
    : TrackedEvent(wgpu::CallbackMode::AllowProcessEvents,
                   SystemEvent::CreateNonProgressingEvent()),
      mOldCallback(oldCallback),
      mUserdata(userdata) {}

DeviceBase::DeviceLostEvent::~DeviceLostEvent() {
    EnsureComplete(EventCompletionType::Shutdown);
}

// static
Ref<DeviceBase::DeviceLostEvent> DeviceBase::DeviceLostEvent::Create(
    const DeviceDescriptor* descriptor) {
    DAWN_ASSERT(descriptor != nullptr);

#if defined(DAWN_ENABLE_ASSERTS)
    // TODO(crbug.com/dawn/2465) Make default AllowSpontaneous once SetDeviceLostCallback is gone.
    static constexpr DeviceLostCallbackInfo kDefaultDeviceLostCallbackInfo = {
        nullptr, wgpu::CallbackMode::AllowProcessEvents,
        [](WGPUDevice const*, WGPUDeviceLostReason, char const*, void*) {
            static bool calledOnce = false;
            if (!calledOnce) {
                calledOnce = true;
                dawn::WarningLog() << "No Dawn device lost callback was set. This is probably not "
                                      "intended. If you really want to ignore device lost and "
                                      "suppress this message, set the callback explicitly.";
            }
        },
        nullptr};
#else
    static constexpr DeviceLostCallbackInfo kDefaultDeviceLostCallbackInfo = {
        nullptr, wgpu::CallbackMode::AllowProcessEvents, nullptr, nullptr};
#endif  // DAWN_ENABLE_ASSERTS

    Ref<DeviceBase::DeviceLostEvent> lostEvent;
    if (descriptor->deviceLostCallback != nullptr) {
        dawn::WarningLog()
            << "DeviceDescriptor.deviceLostCallback and DeviceDescriptor.deviceLostUserdata are "
               "deprecated. Use DeviceDescriptor.deviceLostCallbackInfo instead.";
        lostEvent = AcquireRef(new DeviceBase::DeviceLostEvent(descriptor->deviceLostCallback,
                                                               descriptor->deviceLostUserdata));
    } else {
        DeviceLostCallbackInfo deviceLostCallbackInfo = kDefaultDeviceLostCallbackInfo;
        if (descriptor->deviceLostCallbackInfo.callback != nullptr ||
            descriptor->deviceLostCallbackInfo.mode != wgpu::CallbackMode::WaitAnyOnly) {
            deviceLostCallbackInfo = descriptor->deviceLostCallbackInfo;
            if (deviceLostCallbackInfo.mode != wgpu::CallbackMode::AllowSpontaneous) {
                // TODO(dawn:2458) Currently we default the callback mode to ProcessEvents if not
                // passed for backwards compatibility. We should add warning logging for it though
                // when available.
                deviceLostCallbackInfo.mode = wgpu::CallbackMode::AllowProcessEvents;
            }
        }
        lostEvent = AcquireRef(new DeviceBase::DeviceLostEvent(deviceLostCallbackInfo));
    }

    return lostEvent;
}

void DeviceBase::DeviceLostEvent::Complete(EventCompletionType completionType) {
    if (completionType == EventCompletionType::Shutdown) {
        mReason = wgpu::DeviceLostReason::InstanceDropped;
        mMessage = "A valid external Instance reference no longer exists.";
    }
    if (mReason == wgpu::DeviceLostReason::InstanceDropped ||
        mReason == wgpu::DeviceLostReason::FailedCreation) {
        mDevice = nullptr;
    }

    if (mOldCallback) {
        mOldCallback(ToAPI(mReason), mMessage.c_str(), mUserdata.ExtractAsDangling());
    } else if (mCallback) {
        auto device = ToAPI(mDevice.Get());
        mCallback(&device, ToAPI(mReason), mMessage.c_str(), mUserdata.ExtractAsDangling());
    }
    mDevice = nullptr;
}

ResultOrError<Ref<PipelineLayoutBase>> ValidateLayoutAndGetComputePipelineDescriptorWithDefaults(
    DeviceBase* device,
    const ComputePipelineDescriptor& descriptor,
    ComputePipelineDescriptor* outDescriptor) {
    Ref<PipelineLayoutBase> layoutRef;
    *outDescriptor = descriptor;

    if (outDescriptor->layout == nullptr) {
        DAWN_TRY_ASSIGN(layoutRef,
                        PipelineLayoutBase::CreateDefault(device,
                                                          {{
                                                              SingleShaderStage::Compute,
                                                              outDescriptor->compute.module,
                                                              outDescriptor->compute.entryPoint,
                                                              outDescriptor->compute.constantCount,
                                                              outDescriptor->compute.constants,
                                                          }},
                                                          /*allowInternalBinding=*/false));
        outDescriptor->layout = layoutRef.Get();
    }

    return layoutRef;
}

ResultOrError<Ref<PipelineLayoutBase>> ValidateLayoutAndGetRenderPipelineDescriptorWithDefaults(
    DeviceBase* device,
    const RenderPipelineDescriptor& descriptor,
    RenderPipelineDescriptor* outDescriptor,
    bool allowInternalBinding) {
    Ref<PipelineLayoutBase> layoutRef;
    *outDescriptor = descriptor;

    if (descriptor.layout == nullptr) {
        // Ref will keep the pipeline layout alive until the end of the function where
        // the pipeline will take another reference.
        DAWN_TRY_ASSIGN(layoutRef,
                        PipelineLayoutBase::CreateDefault(
                            device, GetRenderStagesAndSetPlaceholderShader(device, &descriptor),
                            allowInternalBinding));
        outDescriptor->layout = layoutRef.Get();
    }

    return layoutRef;
}

// DeviceBase

DeviceBase::DeviceBase(AdapterBase* adapter,
                       const UnpackedPtr<DeviceDescriptor>& descriptor,
                       const TogglesState& deviceToggles,
                       Ref<DeviceLostEvent>&& lostEvent)
    : mLostEvent(std::move(lostEvent)),
      mAdapter(adapter),
      mToggles(deviceToggles),
      mNextPipelineCompatibilityToken(1) {
    DAWN_ASSERT(descriptor);

    mLostEvent->mDevice = this;

#if defined(DAWN_ENABLE_ASSERTS)
    static constexpr UncapturedErrorCallbackInfo kDefaultUncapturedErrorCallbackInfo = {
        nullptr,
        [](WGPUErrorType, char const*, void*) {
            static bool calledOnce = false;
            if (!calledOnce) {
                calledOnce = true;
                dawn::WarningLog() << "No Dawn device uncaptured error callback was set. This is "
                                      "probably not intended. If you really want to ignore errors "
                                      "and suppress this message, set the callback explicitly.";
            }
        },
        nullptr};
#else
    static constexpr UncapturedErrorCallbackInfo kDefaultUncapturedErrorCallbackInfo =
        kEmptyUncapturedErrorCallbackInfo;
#endif  // DAWN_ENABLE_ASSERTS
    mUncapturedErrorCallbackInfo = kDefaultUncapturedErrorCallbackInfo;
    if (descriptor->uncapturedErrorCallbackInfo.callback != nullptr) {
        mUncapturedErrorCallbackInfo = descriptor->uncapturedErrorCallbackInfo;
    }

    AdapterProperties adapterProperties;
    adapter->APIGetProperties(&adapterProperties);

    ApplyFeatures(descriptor);

    DawnCacheDeviceDescriptor cacheDesc = {};
    const auto* cacheDescIn = descriptor.Get<DawnCacheDeviceDescriptor>();
    if (cacheDescIn != nullptr) {
        cacheDesc = *cacheDescIn;
    }

    if (cacheDesc.loadDataFunction == nullptr && cacheDesc.storeDataFunction == nullptr &&
        cacheDesc.functionUserdata == nullptr && GetPlatform()->GetCachingInterface() != nullptr) {
        // Populate cache functions and userdata from legacy cachingInterface.
        cacheDesc.loadDataFunction = [](const void* key, size_t keySize, void* value,
                                        size_t valueSize, void* userdata) {
            auto* cachingInterface = static_cast<dawn::platform::CachingInterface*>(userdata);
            return cachingInterface->LoadData(key, keySize, value, valueSize);
        };
        cacheDesc.storeDataFunction = [](const void* key, size_t keySize, const void* value,
                                         size_t valueSize, void* userdata) {
            auto* cachingInterface = static_cast<dawn::platform::CachingInterface*>(userdata);
            return cachingInterface->StoreData(key, keySize, value, valueSize);
        };
        cacheDesc.functionUserdata = GetPlatform()->GetCachingInterface();
    }

    // Disable caching if the toggle is passed, or the WGSL writer is not enabled.
    // TODO(crbug.com/dawn/1481): Shader caching currently has a dependency on the WGSL writer to
    // generate cache keys. We can lift the dependency once we also cache frontend parsing,
    // transformations, and reflection.
#if TINT_BUILD_WGSL_WRITER
    if (IsToggleEnabled(Toggle::DisableBlobCache)) {
#else
    {
#endif
        cacheDesc.loadDataFunction = nullptr;
        cacheDesc.storeDataFunction = nullptr;
        cacheDesc.functionUserdata = nullptr;
    }
    mBlobCache = std::make_unique<BlobCache>(cacheDesc);

    if (descriptor->requiredLimits != nullptr) {
        mLimits.v1 =
            ReifyDefaultLimits(descriptor->requiredLimits->limits, adapter->GetFeatureLevel());
    } else {
        GetDefaultLimits(&mLimits.v1, adapter->GetFeatureLevel());
    }
    // Get experimentalSubgroupLimits from physical device
    mLimits.experimentalSubgroupLimits =
        GetPhysicalDevice()->GetLimits().experimentalSubgroupLimits;

    mFormatTable = BuildFormatTable(this);

    if (descriptor->label != nullptr && strlen(descriptor->label) != 0) {
        mLabel = descriptor->label;
    }

    mIsImmediateErrorHandlingEnabled = IsToggleEnabled(Toggle::EnableImmediateErrorHandling);

    // Record the cache key from the properties. Note that currently, if a new extension
    // descriptor is added (and probably handled here), the cache key recording needs to be
    // updated.
    StreamIn(&mDeviceCacheKey, kDawnVersion, adapterProperties, mEnabledFeatures.featuresBitSet,
             mToggles, cacheDesc);
}

DeviceBase::DeviceBase() : mState(State::Alive), mToggles(ToggleStage::Device) {
    GetDefaultLimits(&mLimits.v1, FeatureLevel::Core);
    mFormatTable = BuildFormatTable(this);

    DeviceDescriptor desc = {};
    desc.deviceLostCallbackInfo = {nullptr, wgpu::CallbackMode::AllowSpontaneous};
    mLostEvent = DeviceLostEvent::Create(&desc);
    mLostEvent->mDevice = this;
}

DeviceBase::~DeviceBase() {
    // We need to explicitly release the Queue before we complete the destructor so that the
    // Queue does not get destroyed after the Device.
    mQueue = nullptr;
    mLostEvent = nullptr;
}

MaybeError DeviceBase::Initialize(Ref<QueueBase> defaultQueue) {
    mQueue = std::move(defaultQueue);

    SetWGSLExtensionAllowList();

    mCaches = std::make_unique<DeviceBase::Caches>();
    mErrorScopeStack = std::make_unique<ErrorScopeStack>();
    mDynamicUploader = std::make_unique<DynamicUploader>(this);
    mCallbackTaskManager = AcquireRef(new CallbackTaskManager());
    mInternalPipelineStore = std::make_unique<InternalPipelineStore>(this);

    DAWN_ASSERT(GetPlatform() != nullptr);
    mWorkerTaskPool = GetPlatform()->CreateWorkerTaskPool();
    mAsyncTaskManager = std::make_unique<AsyncTaskManager>(mWorkerTaskPool.get());

    // Starting from now the backend can start doing reentrant calls so the device is marked as
    // alive.
    mState = State::Alive;

    DAWN_TRY_ASSIGN(mEmptyBindGroupLayout, CreateEmptyBindGroupLayout());
    DAWN_TRY_ASSIGN(mEmptyPipelineLayout, CreateEmptyPipelineLayout());

    // If placeholder fragment shader module is needed, initialize it
    if (IsToggleEnabled(Toggle::UsePlaceholderFragmentInVertexOnlyPipeline)) {
        // The empty fragment shader, used as a work around for vertex-only render pipeline
        constexpr char kEmptyFragmentShader[] = R"(
                @fragment fn fs_empty_main() {}
            )";
        ShaderModuleDescriptor descriptor;
        ShaderModuleWGSLDescriptor wgslDesc;
        wgslDesc.code = kEmptyFragmentShader;
        descriptor.nextInChain = &wgslDesc;

        DAWN_TRY_ASSIGN(mInternalPipelineStore->placeholderFragmentShader,
                        CreateShaderModule(&descriptor));
    }

    if (HasFeature(Feature::ImplicitDeviceSynchronization)) {
        mMutex = AcquireRef(new Mutex);
    } else {
        mMutex = nullptr;
    }

    // mAdapter is not set for mock test devices.
    // TODO(crbug.com/dawn/1702): using a mock adapter could avoid the null checking.
    if (mAdapter != nullptr) {
        mAdapter->GetInstance()->AddDevice(this);
    }

    return {};
}

void DeviceBase::WillDropLastExternalRef() {
    {
        // This will be invoked by API side, so we need to lock.
        // Note: we cannot hold the lock when flushing the callbacks so have to limit the scope of
        // the lock.
        auto deviceLock(GetScopedLock());

        // DeviceBase uses RefCountedWithExternalCount to break refcycles.
        //
        // DeviceBase holds multiple Refs to various API objects (pipelines, buffers, etc.) which
        // are used to implement various device-level facilities. These objects are cached on the
        // device, so we want to keep them around instead of making transient allocations. However,
        // many of the objects also hold a Ref<Device> back to their parent device.
        //
        // In order to break this cycle and prevent leaks, when the application drops the last
        // external ref and WillDropLastExternalRef is called, the device clears out any member refs
        // to API objects that hold back-refs to the device - thus breaking any reference cycles.
        //
        // Currently, this is done by calling Destroy on the device to cease all in-flight work and
        // drop references to internal objects. We may want to lift this in the future, but it would
        // make things more complex because there might be pending tasks which hold a ref back to
        // the device - either directly or indirectly. We would need to ensure those tasks don't
        // create new reference cycles, and we would need to continuously try draining the pending
        // tasks to clear out all remaining refs.
        Destroy();
    }

    // Flush last remaining callback tasks.
    do {
        FlushCallbackTaskQueue();
    } while (!mCallbackTaskManager->IsEmpty());

    auto deviceLock(GetScopedLock());
    // Drop the device's reference to the queue. Because the application dropped the last external
    // references, they can no longer get the queue from APIGetQueue().
    mQueue = nullptr;

    // Reset callbacks since after dropping the last external reference, the application may have
    // freed any device-scope memory needed to run the callback.
    mUncapturedErrorCallbackInfo = kEmptyUncapturedErrorCallbackInfo;

    // mAdapter is not set for mock test devices.
    // TODO(crbug.com/dawn/1702): using a mock adapter could avoid the null checking.
    if (mAdapter != nullptr) {
        mAdapter->GetInstance()->RemoveDevice(this);

        // Once last external ref dropped, all callbacks should be forwarded to Instance's callback
        // queue instead.
        mCallbackTaskManager = mAdapter->GetInstance()->GetCallbackTaskManager();
    }
}

void DeviceBase::DestroyObjects() {
    // List of object types in reverse "dependency" order so we can iterate and delete the
    // objects safely. We define dependent here such that if B has a ref to A, then B depends on
    // A. We therefore try to destroy B before destroying A. Note that this only considers the
    // immediate frontend dependencies, while backend objects could add complications and extra
    // dependencies.
    //
    // Note that AttachmentState is not an ApiObject so it cannot be eagerly destroyed. However,
    // since AttachmentStates are cached by the device, objects that hold references to
    // AttachmentStates should make sure to un-ref them in their Destroy operation so that we
    // can destroy the frontend cache.

    // clang-format off
        static constexpr std::array<ObjectType, 20> kObjectTypeDependencyOrder = {
            ObjectType::ComputePassEncoder,
            ObjectType::RenderPassEncoder,
            ObjectType::RenderBundleEncoder,
            ObjectType::RenderBundle,
            ObjectType::CommandEncoder,
            ObjectType::CommandBuffer,
            ObjectType::RenderPipeline,
            ObjectType::ComputePipeline,
            ObjectType::PipelineLayout,
            ObjectType::SwapChain,
            ObjectType::BindGroup,
            ObjectType::BindGroupLayout,
            ObjectType::ShaderModule,
            ObjectType::SharedTextureMemory,
            ObjectType::SharedFence,
            ObjectType::ExternalTexture,
            ObjectType::Texture,  // Note that Textures own the TextureViews.
            ObjectType::QuerySet,
            ObjectType::Sampler,
            ObjectType::Buffer,
        };
    // clang-format on

    for (ObjectType type : kObjectTypeDependencyOrder) {
        mObjectLists[type].Destroy();
    }
}

void DeviceBase::Destroy() {
    // Skip if we are already destroyed.
    if (mState == State::Destroyed) {
        return;
    }

    // This function may be called re-entrantly inside APITick(). Tick triggers callbacks
    // inside which the application may destroy the device. Thus, we should be careful not
    // to delete objects that are needed inside Tick after callbacks have been called.
    //  - mCallbackTaskManager is not deleted since we flush the callback queue at the end
    // of Tick(). Note: that flush should always be empty since all callbacks are drained
    // inside Destroy() so there should be no outstanding tasks holding objects alive.
    //  - Similiarly, mAsyncTaskManager is not deleted since we use it to return a status
    // from Tick() whether or not there is any more pending work.

    // Skip handling device facilities if they haven't even been created (or failed doing so)
    if (mState != State::BeingCreated) {
        // The device is being destroyed so it will be lost, call the application callback.
        if (mLostEvent != nullptr) {
            mLostEvent->mReason = wgpu::DeviceLostReason::Destroyed;
            mLostEvent->mMessage = "Device was destroyed.";
            GetInstance()->GetEventManager()->SetFutureReady(mLostEvent.Get());
            mLostEvent = nullptr;
        }

        // Call all the callbacks immediately as the device is about to shut down.
        // TODO(crbug.com/dawn/826): Cancel the tasks that are in flight if possible.
        mAsyncTaskManager->WaitAllPendingTasks();
        mCallbackTaskManager->HandleShutDown();
    }

    // Disconnect the device, depending on which state we are currently in.
    switch (mState) {
        case State::BeingCreated:
            // The GPU timeline was never started so we don't have to wait.
            break;

        case State::Alive:
            // Alive is the only state which can have GPU work happening. Wait for all of it to
            // complete before proceeding with destruction.
            // Ignore errors so that we can continue with destruction
            IgnoreErrors(mQueue->WaitForIdleForDestruction());
            mQueue->AssumeCommandsComplete();
            break;

        case State::BeingDisconnected:
            // Getting disconnected is a transient state happening in a single API call so there
            // is always an external reference keeping the Device alive, which means the
            // destructor cannot run while BeingDisconnected.
            DAWN_UNREACHABLE();
            break;

        case State::Disconnected:
            break;

        case State::Destroyed:
            // If we are already destroyed we should've skipped this work entirely.
            DAWN_UNREACHABLE();
            break;
    }

    if (mState != State::BeingCreated) {
        // The GPU timeline is finished.
        DAWN_ASSERT(mQueue->GetCompletedCommandSerial() == mQueue->GetLastSubmittedCommandSerial());

        // Finish destroying all objects owned by the device and tick the queue-related tasks
        // since they should be complete. This must be done before DestroyImpl() it may
        // relinquish resources that will be freed by backends in the DestroyImpl() call.
        DestroyObjects();
        mQueue->Tick(mQueue->GetCompletedCommandSerial());
        // Call TickImpl once last time to clean up resources
        // Ignore errors so that we can continue with destruction
        IgnoreErrors(TickImpl());
    }

    // At this point GPU operations are always finished, so we are in the disconnected state.
    // Note that currently this state change is required because some of the backend
    // implementations of DestroyImpl checks that we are disconnected before doing work.
    mState = State::Disconnected;

    mDynamicUploader = nullptr;
    mEmptyBindGroupLayout = nullptr;
    mEmptyPipelineLayout = nullptr;
    mInternalPipelineStore = nullptr;
    mExternalTexturePlaceholderView = nullptr;
    mTemporaryUniformBuffer = nullptr;

    // Note: mQueue is not released here since the application may still get it after calling
    // Destroy() via APIGetQueue.
    if (mQueue != nullptr) {
        mQueue->AssumeCommandsComplete();
        mQueue->Destroy();
    }

    // Now that the GPU timeline is empty, destroy the backend device.
    DestroyImpl();

    mCaches = nullptr;
    mState = State::Destroyed;
}

void DeviceBase::APIDestroy() {
    Destroy();
}

void DeviceBase::HandleError(std::unique_ptr<ErrorData> error,
                             InternalErrorType additionalAllowedErrors,
                             WGPUDeviceLostReason lostReason) {
    AppendDebugLayerMessages(error.get());

    InternalErrorType type = error->GetType();
    if (type != InternalErrorType::Validation) {
        // D3D device can provide additional device removed reason. We would
        // like to query and log the the device removed reason if the error is
        // not validation error.
        AppendDeviceLostMessage(error.get());
    }

    InternalErrorType allowedErrors =
        InternalErrorType::Validation | InternalErrorType::DeviceLost | additionalAllowedErrors;

    if (type == InternalErrorType::DeviceLost) {
        mState = State::Disconnected;

        // If the ErrorInjector is enabled, then the device loss might be fake and the device
        // still be executing commands. Force a wait for idle in this case, with State being
        // Disconnected so we can detect this case in WaitForIdleForDestruction.
        if (ErrorInjectorEnabled()) {
            IgnoreErrors(mQueue->WaitForIdleForDestruction());
        }

        // A real device lost happened. Set the state to disconnected as the device cannot be
        // used. Also tags all commands as completed since the device stopped running.
        mQueue->AssumeCommandsComplete();
    } else if (!(allowedErrors & type)) {
        // If we receive an error which we did not explicitly allow, assume the backend can't
        // recover and proceed with device destruction. We first wait for all previous commands to
        // be completed so that backend objects can be freed immediately, before handling the loss.
        error->AppendContext("handling unexpected error type %s when allowed errors are %s.", type,
                             allowedErrors);

        // Move away from the Alive state so that the application cannot use this device
        // anymore.
        // TODO(crbug.com/dawn/831): Do we need atomics for this to become visible to other
        // threads in a multithreaded scenario?
        mState = State::BeingDisconnected;

        // Ignore errors so that we can continue with destruction
        // Assume all commands are complete after WaitForIdleForDestruction (because they were)
        IgnoreErrors(mQueue->WaitForIdleForDestruction());
        IgnoreErrors(TickImpl());
        mQueue->AssumeCommandsComplete();
        mState = State::Disconnected;

        // Now everything is as if the device was lost.
        type = InternalErrorType::DeviceLost;
    }

    const std::string messageStr = error->GetFormattedMessage();
    if (type == InternalErrorType::DeviceLost) {
        // The device was lost, schedule the application callback's execution.
        // Note: we don't invoke the callbacks directly here because it could cause re-entrances ->
        // possible deadlock.
        if (mLostEvent != nullptr) {
            mLostEvent->mReason = FromAPI(lostReason);
            mLostEvent->mMessage = messageStr;
            GetInstance()->GetEventManager()->SetFutureReady(mLostEvent.Get());
            mLostEvent = nullptr;
        }

        mQueue->HandleDeviceLoss();

        // TODO(crbug.com/dawn/826): Cancel the tasks that are in flight if possible.
        mAsyncTaskManager->WaitAllPendingTasks();
        mCallbackTaskManager->HandleDeviceLoss();

        // Still forward device loss errors to the error scopes so they all reject.
        mErrorScopeStack->HandleError(ToWGPUErrorType(type), messageStr);
    } else {
        // Pass the error to the error scope stack and call the uncaptured error callback
        // if it isn't handled. DeviceLost is not handled here because it should be
        // handled by the lost callback.
        bool captured = mErrorScopeStack->HandleError(ToWGPUErrorType(type), messageStr);
        if (!captured) {
            // Only call the uncaptured error callback if the device is alive. After the
            // device is lost, the uncaptured error callback should cease firing.
            if (mUncapturedErrorCallbackInfo.callback != nullptr && mState == State::Alive) {
                mUncapturedErrorCallbackInfo.callback(
                    static_cast<WGPUErrorType>(ToWGPUErrorType(type)), messageStr.c_str(),
                    mUncapturedErrorCallbackInfo.userdata);
            }
        }
    }
}

void DeviceBase::ConsumeError(std::unique_ptr<ErrorData> error,
                              InternalErrorType additionalAllowedErrors) {
    DAWN_ASSERT(error != nullptr);
    HandleError(std::move(error), additionalAllowedErrors);
}

void DeviceBase::APISetLoggingCallback(wgpu::LoggingCallback callback, void* userdata) {
    std::lock_guard<std::shared_mutex> lock(mLoggingMutex);
    mLoggingCallback = callback;
    mLoggingUserdata = userdata;
}

void DeviceBase::APISetUncapturedErrorCallback(wgpu::ErrorCallback callback, void* userdata) {
    // The registered callback function and userdata pointer are stored and used by deferred
    // callback tasks, and after setting a different callback (especially in the case of
    // resetting) the resources pointed by such pointer may be freed. Flush all deferred
    // callback tasks to guarantee we are never going to use the previous callback after
    // this call.
    FlushCallbackTaskQueue();
    auto deviceLock(GetScopedLock());
    // Clearing the callback and userdata is allowed because in Chromium they should be cleared
    // after Dawn device is destroyed and before Dawn wire server is destroyed.
    if (callback == nullptr) {
        mUncapturedErrorCallbackInfo = kEmptyUncapturedErrorCallbackInfo;
        return;
    }
    if (IsLost()) {
        return;
    }
    mUncapturedErrorCallbackInfo = {nullptr, callback, userdata};
}

void DeviceBase::APISetDeviceLostCallback(wgpu::DeviceLostCallback callback, void* userdata) {
    GetInstance()->EmitDeprecationWarning(
        "SetDeviceLostCallback is deprecated. Pass the callback in the device descriptor instead.");

    // The registered callback function and userdata pointer are stored and used by deferred
    // callback tasks, and after setting a different callback (especially in the case of
    // resetting) the resources pointed by such pointer may be freed. Flush all deferred
    // callback tasks to guarantee we are never going to use the previous callback after
    // this call.
    FlushCallbackTaskQueue();
    auto deviceLock(GetScopedLock());
    // Clearing the callback and userdata is allowed because in Chromium they should be cleared
    // after Dawn device is destroyed and before Dawn wire server is destroyed.
    if (callback == nullptr) {
        mLostEvent->mCallback = nullptr;
        mLostEvent->mOldCallback = nullptr;
        mLostEvent->mUserdata = nullptr;
        return;
    }
    if (IsLost()) {
        return;
    }
    mLostEvent->mOldCallback = callback;
    mLostEvent->mUserdata = userdata;
}

void DeviceBase::APIPushErrorScope(wgpu::ErrorFilter filter) {
    if (ConsumedError(ValidateErrorFilter(filter))) {
        return;
    }
    mErrorScopeStack->Push(filter);
}

void DeviceBase::APIPopErrorScope(wgpu::ErrorCallback callback, void* userdata) {
    static wgpu::ErrorCallback kDefaultCallback = [](WGPUErrorType, char const*, void*) {};

    APIPopErrorScope2({nullptr, WGPUCallbackMode_AllowProcessEvents,
                       [](WGPUPopErrorScopeStatus, WGPUErrorType type, char const* message,
                          void* callback, void* userdata) {
                           auto cb = reinterpret_cast<wgpu::ErrorCallback>(callback);
                           cb(type, message, userdata);
                       },
                       reinterpret_cast<void*>(callback != nullptr ? callback : kDefaultCallback),
                       userdata});
}

Future DeviceBase::APIPopErrorScopeF(const PopErrorScopeCallbackInfo& callbackInfo) {
    return APIPopErrorScope2({ToAPI(callbackInfo.nextInChain), ToAPI(callbackInfo.mode),
                              [](WGPUPopErrorScopeStatus status, WGPUErrorType type,
                                 char const* message, void* callback, void* userdata) {
                                  auto cb = reinterpret_cast<WGPUPopErrorScopeCallback>(callback);
                                  cb(status, type, message, userdata);
                              },
                              reinterpret_cast<void*>(callbackInfo.callback),
                              callbackInfo.userdata});
}

Future DeviceBase::APIPopErrorScope2(const WGPUPopErrorScopeCallbackInfo2& callbackInfo) {
    struct PopErrorScopeEvent final : public EventManager::TrackedEvent {
        WGPUPopErrorScopeCallback2 mCallback;
        raw_ptr<void> mUserdata1;
        raw_ptr<void> mUserdata2;
        std::optional<ErrorScope> mScope;

        PopErrorScopeEvent(const WGPUPopErrorScopeCallbackInfo2& callbackInfo,
                           std::optional<ErrorScope>&& scope)
            : TrackedEvent(static_cast<wgpu::CallbackMode>(callbackInfo.mode),
                           TrackedEvent::Completed{}),
              mCallback(callbackInfo.callback),
              mUserdata1(callbackInfo.userdata1),
              mUserdata2(callbackInfo.userdata2),
              mScope(scope) {}

        ~PopErrorScopeEvent() override { EnsureComplete(EventCompletionType::Shutdown); }

        void Complete(EventCompletionType completionType) override {
            WGPUPopErrorScopeStatus status = completionType == EventCompletionType::Ready
                                                 ? WGPUPopErrorScopeStatus_Success
                                                 : WGPUPopErrorScopeStatus_InstanceDropped;
            WGPUErrorType type;
            const char* message;
            if (mScope) {
                type = static_cast<WGPUErrorType>(mScope->GetErrorType());
                message = mScope->GetErrorMessage().c_str();
            } else {
                type = WGPUErrorType_Unknown;
                message = "No error scopes to pop";
            }

            mCallback(status, type, message, mUserdata1.ExtractAsDangling(),
                      mUserdata2.ExtractAsDangling());
        }
    };

    std::optional<ErrorScope> scope;
    {
        // TODO(crbug.com/dawn/831) Manually acquire device lock instead of relying on code-gen for
        // re-entrancy.
        auto deviceLock(GetScopedLock());

        if (IsLost()) {
            scope = ErrorScope(wgpu::ErrorType::DeviceLost, "GPU device disconnected");
        } else if (!mErrorScopeStack->Empty()) {
            scope = mErrorScopeStack->Pop();
        }
    }

    FutureID futureID = GetInstance()->GetEventManager()->TrackEvent(
        AcquireRef(new PopErrorScopeEvent(callbackInfo, std::move(scope))));
    return {futureID};
}

BlobCache* DeviceBase::GetBlobCache() const {
    return mBlobCache.get();
}

Blob DeviceBase::LoadCachedBlob(const CacheKey& key) {
    return GetBlobCache()->Load(key);
}

void DeviceBase::StoreCachedBlob(const CacheKey& key, const Blob& blob) {
    if (!blob.Empty()) {
        GetBlobCache()->Store(key, blob);
    }
}

MaybeError DeviceBase::ValidateObject(const ApiObjectBase* object) const {
    DAWN_ASSERT(object != nullptr);
    DAWN_INVALID_IF(object->GetDevice() != this,
                    "%s is associated with %s, and cannot be used with %s.", object,
                    object->GetDevice(), this);

    // TODO(dawn:563): Preserve labels for error objects.
    DAWN_INVALID_IF(object->IsError(), "%s is invalid.", object);

    return {};
}

MaybeError DeviceBase::ValidateIsAlive() const {
    DAWN_INVALID_IF(mState != State::Alive, "%s is lost.", this);
    return {};
}

void DeviceBase::APIForceLoss(wgpu::DeviceLostReason reason, const char* message) {
    if (mState != State::Alive) {
        return;
    }
    // Note that since we are passing None as the allowedErrors, an additional message will be
    // appended noting that the error was unexpected. Since this call is for testing only it is not
    // too important, but useful for users to understand where the extra message is coming from.
    HandleError(DAWN_INTERNAL_ERROR(message), InternalErrorType::None, ToAPI(reason));
}

DeviceBase::State DeviceBase::GetState() const {
    return mState;
}

bool DeviceBase::IsLost() const {
    DAWN_ASSERT(mState != State::BeingCreated);
    return mState != State::Alive;
}

ApiObjectList* DeviceBase::GetObjectTrackingList(ObjectType type) {
    return &mObjectLists[type];
}

const ApiObjectList* DeviceBase::GetObjectTrackingList(ObjectType type) const {
    return &mObjectLists[type];
}

InstanceBase* DeviceBase::GetInstance() const {
    return mAdapter->GetInstance();
}

AdapterBase* DeviceBase::GetAdapter() const {
    return mAdapter.Get();
}

PhysicalDeviceBase* DeviceBase::GetPhysicalDevice() const {
    return mAdapter->GetPhysicalDevice();
}

dawn::platform::Platform* DeviceBase::GetPlatform() const {
    return GetAdapter()->GetInstance()->GetPlatform();
}

InternalPipelineStore* DeviceBase::GetInternalPipelineStore() {
    return mInternalPipelineStore.get();
}

bool DeviceBase::HasPendingTasks() {
    return mAsyncTaskManager->HasPendingTasks() || !mCallbackTaskManager->IsEmpty();
}

bool DeviceBase::IsDeviceIdle() {
    if (HasPendingTasks()) {
        return false;
    }
    return !mQueue->HasScheduledCommands();
}

ResultOrError<const Format*> DeviceBase::GetInternalFormat(wgpu::TextureFormat format) const {
    FormatIndex index = ComputeFormatIndex(format);
    DAWN_INVALID_IF(index >= mFormatTable.size(), "Unknown texture format %s.", format);

    const Format* internalFormat = &mFormatTable[index];
    DAWN_INVALID_IF(!internalFormat->IsSupported(), "Unsupported texture format %s, reason: %s.",
                    format, internalFormat->unsupportedReason);

    return internalFormat;
}

const Format& DeviceBase::GetValidInternalFormat(wgpu::TextureFormat format) const {
    FormatIndex index = ComputeFormatIndex(format);
    DAWN_ASSERT(index < mFormatTable.size());
    DAWN_ASSERT(mFormatTable[index].IsSupported());
    return mFormatTable[index];
}

const Format& DeviceBase::GetValidInternalFormat(FormatIndex index) const {
    DAWN_ASSERT(index < mFormatTable.size());
    DAWN_ASSERT(mFormatTable[index].IsSupported());
    return mFormatTable[index];
}

std::vector<const Format*> DeviceBase::GetCompatibleViewFormats(const Format& format) const {
    wgpu::TextureFormat viewFormat =
        format.format == format.baseFormat ? format.baseViewFormat : format.baseFormat;
    if (viewFormat == wgpu::TextureFormat::Undefined) {
        return {};
    }
    const Format& f = mFormatTable[ComputeFormatIndex(viewFormat)];
    if (!f.IsSupported()) {
        return {};
    }
    return {&f};
}

ResultOrError<Ref<BindGroupLayoutBase>> DeviceBase::GetOrCreateBindGroupLayout(
    const BindGroupLayoutDescriptor* descriptor,
    PipelineCompatibilityToken pipelineCompatibilityToken) {
    BindGroupLayoutInternalBase blueprint(this, descriptor, ApiObjectBase::kUntrackedByDevice);

    const size_t blueprintHash = blueprint.ComputeContentHash();
    blueprint.SetContentHash(blueprintHash);

    Ref<BindGroupLayoutInternalBase> internal;
    DAWN_TRY_ASSIGN(internal, GetOrCreate(mCaches->bindGroupLayouts, &blueprint,
                                          [&]() -> ResultOrError<Ref<BindGroupLayoutInternalBase>> {
                                              Ref<BindGroupLayoutInternalBase> result;
                                              DAWN_TRY_ASSIGN(
                                                  result, CreateBindGroupLayoutImpl(descriptor));
                                              result->SetContentHash(blueprintHash);
                                              return result;
                                          }));
    return AcquireRef(
        new BindGroupLayoutBase(this, descriptor->label, internal, pipelineCompatibilityToken));
}

// Private function used at initialization
ResultOrError<Ref<BindGroupLayoutBase>> DeviceBase::CreateEmptyBindGroupLayout() {
    BindGroupLayoutDescriptor desc = {};
    desc.entryCount = 0;
    desc.entries = nullptr;

    return GetOrCreateBindGroupLayout(&desc);
}

ResultOrError<Ref<PipelineLayoutBase>> DeviceBase::CreateEmptyPipelineLayout() {
    PipelineLayoutDescriptor desc = {};
    desc.bindGroupLayoutCount = 0;
    desc.bindGroupLayouts = nullptr;

    return GetOrCreatePipelineLayout(Unpack(&desc));
}

BindGroupLayoutBase* DeviceBase::GetEmptyBindGroupLayout() {
    DAWN_ASSERT(mEmptyBindGroupLayout != nullptr);
    return mEmptyBindGroupLayout.Get();
}

PipelineLayoutBase* DeviceBase::GetEmptyPipelineLayout() {
    DAWN_ASSERT(mEmptyPipelineLayout != nullptr);
    return mEmptyPipelineLayout.Get();
}

Ref<ComputePipelineBase> DeviceBase::GetCachedComputePipeline(
    ComputePipelineBase* uninitializedComputePipeline) {
    return mCaches->computePipelines.Find(uninitializedComputePipeline);
}

Ref<RenderPipelineBase> DeviceBase::GetCachedRenderPipeline(
    RenderPipelineBase* uninitializedRenderPipeline) {
    return mCaches->renderPipelines.Find(uninitializedRenderPipeline);
}

Ref<ComputePipelineBase> DeviceBase::AddOrGetCachedComputePipeline(
    Ref<ComputePipelineBase> computePipeline) {
    auto [pipeline, _] = mCaches->computePipelines.Insert(computePipeline.Get());
    return std::move(pipeline);
}

Ref<RenderPipelineBase> DeviceBase::AddOrGetCachedRenderPipeline(
    Ref<RenderPipelineBase> renderPipeline) {
    auto [pipeline, _] = mCaches->renderPipelines.Insert(renderPipeline.Get());
    return std::move(pipeline);
}

ResultOrError<Ref<TextureViewBase>>
DeviceBase::GetOrCreatePlaceholderTextureViewForExternalTexture() {
    if (!mExternalTexturePlaceholderView.Get()) {
        Ref<TextureBase> externalTexturePlaceholder;
        TextureDescriptor textureDesc;
        textureDesc.dimension = wgpu::TextureDimension::e2D;
        textureDesc.format = wgpu::TextureFormat::RGBA8Unorm;
        textureDesc.label = "Dawn_External_Texture_Placeholder_Texture";
        textureDesc.size = {1, 1, 1};
        textureDesc.usage = wgpu::TextureUsage::TextureBinding;

        DAWN_TRY_ASSIGN(externalTexturePlaceholder, CreateTexture(&textureDesc));

        TextureViewDescriptor textureViewDesc;
        textureViewDesc.arrayLayerCount = 1;
        textureViewDesc.aspect = wgpu::TextureAspect::All;
        textureViewDesc.baseArrayLayer = 0;
        textureViewDesc.dimension = wgpu::TextureViewDimension::e2D;
        textureViewDesc.format = wgpu::TextureFormat::RGBA8Unorm;
        textureViewDesc.label = "Dawn_External_Texture_Placeholder_Texture_View";
        textureViewDesc.mipLevelCount = 1;

        DAWN_TRY_ASSIGN(mExternalTexturePlaceholderView,
                        CreateTextureView(externalTexturePlaceholder.Get(), &textureViewDesc));
    }

    return mExternalTexturePlaceholderView;
}

ResultOrError<Ref<PipelineLayoutBase>> DeviceBase::GetOrCreatePipelineLayout(
    const UnpackedPtr<PipelineLayoutDescriptor>& descriptor) {
    PipelineLayoutBase blueprint(this, descriptor, ApiObjectBase::kUntrackedByDevice);

    const size_t blueprintHash = blueprint.ComputeContentHash();
    blueprint.SetContentHash(blueprintHash);

    return GetOrCreate(mCaches->pipelineLayouts, &blueprint,
                       [&]() -> ResultOrError<Ref<PipelineLayoutBase>> {
                           Ref<PipelineLayoutBase> result;
                           DAWN_TRY_ASSIGN(result, CreatePipelineLayoutImpl(descriptor));
                           result->SetContentHash(blueprintHash);
                           return result;
                       });
}

ResultOrError<Ref<SamplerBase>> DeviceBase::GetOrCreateSampler(
    const SamplerDescriptor* descriptor) {
    SamplerBase blueprint(this, descriptor, ApiObjectBase::kUntrackedByDevice);

    const size_t blueprintHash = blueprint.ComputeContentHash();
    blueprint.SetContentHash(blueprintHash);

    return GetOrCreate(mCaches->samplers, &blueprint, [&]() -> ResultOrError<Ref<SamplerBase>> {
        Ref<SamplerBase> result;
        DAWN_TRY_ASSIGN(result, CreateSamplerImpl(descriptor));
        result->SetContentHash(blueprintHash);
        return result;
    });
}

ResultOrError<Ref<ShaderModuleBase>> DeviceBase::GetOrCreateShaderModule(
    const UnpackedPtr<ShaderModuleDescriptor>& descriptor,
    ShaderModuleParseResult* parseResult,
    std::unique_ptr<OwnedCompilationMessages>* compilationMessages) {
    DAWN_ASSERT(parseResult != nullptr);

    ShaderModuleBase blueprint(this, descriptor, ApiObjectBase::kUntrackedByDevice);

    const size_t blueprintHash = blueprint.ComputeContentHash();
    blueprint.SetContentHash(blueprintHash);

    return GetOrCreate(
        mCaches->shaderModules, &blueprint, [&]() -> ResultOrError<Ref<ShaderModuleBase>> {
            auto* unownedMessages = compilationMessages ? compilationMessages->get() : nullptr;
            if (!parseResult->HasParsedShader()) {
                // We skip the parse on creation if validation isn't enabled which let's us quickly
                // lookup in the cache without validating and parsing. We need the parsed module
                // now.
                DAWN_ASSERT(!IsValidationEnabled());
                DAWN_TRY(
                    ValidateAndParseShaderModule(this, descriptor, parseResult, unownedMessages));
            }

            auto resultOrError = [&]() -> ResultOrError<Ref<ShaderModuleBase>> {
                SCOPED_DAWN_HISTOGRAM_TIMER_MICROS(GetPlatform(), "CreateShaderModuleUS");
                return CreateShaderModuleImpl(descriptor, parseResult, unownedMessages);
            }();
            DAWN_HISTOGRAM_BOOLEAN(GetPlatform(), "CreateShaderModuleSuccess",
                                   resultOrError.IsSuccess());

            Ref<ShaderModuleBase> result;
            DAWN_TRY_ASSIGN(result, std::move(resultOrError));
            result->SetContentHash(blueprintHash);
            // Inject compilation messages now, as another thread may get a cache hit and query them
            // immediately after insert into the cache.
            if (compilationMessages) {
                result->InjectCompilationMessages(std::move(*compilationMessages));
            }
            return result;
        });
}

Ref<AttachmentState> DeviceBase::GetOrCreateAttachmentState(AttachmentState* blueprint) {
    return GetOrCreate(mCaches->attachmentStates, blueprint, [&]() -> Ref<AttachmentState> {
        return AcquireRef(new AttachmentState(*blueprint));
    });
}

Ref<AttachmentState> DeviceBase::GetOrCreateAttachmentState(
    const RenderBundleEncoderDescriptor* descriptor) {
    AttachmentState blueprint(this, descriptor);
    return GetOrCreateAttachmentState(&blueprint);
}

Ref<AttachmentState> DeviceBase::GetOrCreateAttachmentState(
    const UnpackedPtr<RenderPipelineDescriptor>& descriptor,
    const PipelineLayoutBase* layout) {
    AttachmentState blueprint(this, descriptor, layout);
    return GetOrCreateAttachmentState(&blueprint);
}

Ref<AttachmentState> DeviceBase::GetOrCreateAttachmentState(
    const UnpackedPtr<RenderPassDescriptor>& descriptor) {
    AttachmentState blueprint(this, descriptor);
    return GetOrCreateAttachmentState(&blueprint);
}

Ref<PipelineCacheBase> DeviceBase::GetOrCreatePipelineCache(const CacheKey& key) {
    return GetOrCreatePipelineCacheImpl(key);
}

// Object creation API methods

BindGroupBase* DeviceBase::APICreateBindGroup(const BindGroupDescriptor* descriptor) {
    Ref<BindGroupBase> result;
    if (ConsumedError(CreateBindGroup(descriptor), &result, "calling %s.CreateBindGroup(%s).", this,
                      descriptor)) {
        return ReturnToAPI(
            BindGroupBase::MakeError(this, descriptor ? descriptor->label : nullptr));
    }
    return ReturnToAPI(std::move(result));
}
BindGroupLayoutBase* DeviceBase::APICreateBindGroupLayout(
    const BindGroupLayoutDescriptor* descriptor) {
    Ref<BindGroupLayoutBase> result;
    if (ConsumedError(CreateBindGroupLayout(descriptor), &result,
                      "calling %s.CreateBindGroupLayout(%s).", this, descriptor)) {
        return ReturnToAPI(
            BindGroupLayoutBase::MakeError(this, descriptor ? descriptor->label : nullptr));
    }
    return ReturnToAPI(std::move(result));
}
BufferBase* DeviceBase::APICreateBuffer(const BufferDescriptor* descriptor) {
    // Search for the host mapped pointer extension struct. If it is present, we will
    // try to create the buffer without taking the global device-lock. If creation fails,
    // we'll acquire the device lock and do normal error handling.
    bool hasHostMapped = false;
    for (const auto* chain = descriptor->nextInChain; chain != nullptr;
         chain = chain->nextInChain) {
        if (chain->sType == wgpu::SType::BufferHostMappedPointer) {
            hasHostMapped = true;
            break;
        }
    }

    std::optional<ResultOrError<Ref<BufferBase>>> resultOrError;
    if (hasHostMapped) {
        // Buffer creation from host-mapped pointer does not need the device lock.
        resultOrError = CreateBuffer(descriptor);
        if (resultOrError->IsSuccess()) {
            return ReturnToAPI(resultOrError->AcquireSuccess());
        }
        // Error case continues below, and will acquire the device lock for
        // thread-safe error handling.
        // TODO(dawn:1662): Make error handling thread-safe.
    }

    auto deviceLock(GetScopedLock());
    if (!hasHostMapped) {
        resultOrError = CreateBuffer(descriptor);
    }
    Ref<BufferBase> result;
    if (ConsumedError(std::move(*resultOrError), &result, InternalErrorType::OutOfMemory,
                      "calling %s.CreateBuffer(%s).", this, descriptor)) {
        DAWN_ASSERT(result == nullptr);
        result = BufferBase::MakeError(this, descriptor);
    }
    return ReturnToAPI(std::move(result));
}

CommandEncoder* DeviceBase::APICreateCommandEncoder(const CommandEncoderDescriptor* descriptor) {
    Ref<CommandEncoder> result;
    if (ConsumedError(CreateCommandEncoder(descriptor), &result,
                      "calling %s.CreateCommandEncoder(%s).", this, descriptor)) {
        result = CommandEncoder::MakeError(this, descriptor ? descriptor->label : nullptr);
    }
    return ReturnToAPI(std::move(result));
}
ComputePipelineBase* DeviceBase::APICreateComputePipeline(
    const ComputePipelineDescriptor* descriptor) {
    TRACE_EVENT1(GetPlatform(), General, "DeviceBase::APICreateComputePipeline", "label",
                 utils::GetLabelForTrace(descriptor->label));

    auto resultOrError = CreateComputePipeline(descriptor);
    if (resultOrError.IsSuccess()) {
        return ReturnToAPI(resultOrError.AcquireSuccess());
    }

    // Acquire the device lock for error handling.
    // TODO(dawn:1662): Make error handling thread-safe.
    auto deviceLock(GetScopedLock());
    Ref<ComputePipelineBase> result;
    if (ConsumedError(std::move(resultOrError), &result, InternalErrorType::Internal,
                      "calling %s.CreateComputePipeline(%s).", this, descriptor)) {
        result = ComputePipelineBase::MakeError(this, descriptor ? descriptor->label : nullptr);
    }
    return ReturnToAPI(std::move(result));
}
void DeviceBase::APICreateComputePipelineAsync(const ComputePipelineDescriptor* descriptor,
                                               WGPUCreateComputePipelineAsyncCallback callback,
                                               void* userdata) {
    GetInstance()->EmitDeprecationWarning(
        "Old CreateComputePipelineAsync APIs are deprecated. If using C please pass a CallbackInfo "
        "struct that has two userdatas. Otherwise, if using C++, please use templated helpers.");

    APICreateComputePipelineAsync2(
        descriptor, {nullptr, WGPUCallbackMode_AllowProcessEvents,
                     [](WGPUCreatePipelineAsyncStatus status, WGPUComputePipeline pipeline,
                        char const* message, void* callback, void* userdata) {
                         auto cb =
                             reinterpret_cast<WGPUCreateComputePipelineAsyncCallback>(callback);
                         cb(status, pipeline, message, userdata);
                     },
                     reinterpret_cast<void*>(callback), userdata});
}
Future DeviceBase::APICreateComputePipelineAsyncF(
    const ComputePipelineDescriptor* descriptor,
    const CreateComputePipelineAsyncCallbackInfo& callbackInfo) {
    GetInstance()->EmitDeprecationWarning(
        "Old CreateComputePipelineAsync APIs are deprecated. If using C please pass a CallbackInfo "
        "struct that has two userdatas. Otherwise, if using C++, please use templated helpers.");
    return APICreateComputePipelineAsync2(
        descriptor, {ToAPI(callbackInfo.nextInChain), ToAPI(callbackInfo.mode),
                     [](WGPUCreatePipelineAsyncStatus status, WGPUComputePipeline pipeline,
                        char const* message, void* callback, void* userdata) {
                         auto cb =
                             reinterpret_cast<WGPUCreateComputePipelineAsyncCallback>(callback);
                         cb(status, pipeline, message, userdata);
                     },
                     reinterpret_cast<void*>(callbackInfo.callback), callbackInfo.userdata});
}
Future DeviceBase::APICreateComputePipelineAsync2(
    const ComputePipelineDescriptor* descriptor,
    const WGPUCreateComputePipelineAsyncCallbackInfo2& callbackInfo) {
    TRACE_EVENT1(GetPlatform(), General, "DeviceBase::APICreateComputePipelineAsync", "label",
                 utils::GetLabelForTrace(descriptor->label));

    EventManager* manager = GetInstance()->GetEventManager();

    auto GetFuture = [&](Ref<EventManager::TrackedEvent>&& event) {
        FutureID futureID = manager->TrackEvent(std::move(event));
        return Future{futureID};
    };

    if (IsLost()) {
        // Device lost error: create an async event that completes when created.
        return GetFuture(AcquireRef(new CreateComputePipelineAsyncEvent(
            this, callbackInfo, DAWN_DEVICE_LOST_ERROR("Device lost"), descriptor->label)));
    }

    auto resultOrError = CreateUninitializedComputePipeline(descriptor);
    if (resultOrError.IsError()) {
        // Validation error: create an async event that completes when created.
        return GetFuture(AcquireRef(new CreateComputePipelineAsyncEvent(
            this, callbackInfo, resultOrError.AcquireError(), descriptor->label)));
    }

    Ref<ComputePipelineBase> uninitializedComputePipeline = resultOrError.AcquireSuccess();
    Ref<ComputePipelineBase> cachedComputePipeline =
        GetCachedComputePipeline(uninitializedComputePipeline.Get());
    if (cachedComputePipeline.Get() != nullptr) {
        // Cached pipeline: create an async event that completes when created.
        return GetFuture(AcquireRef(new CreateComputePipelineAsyncEvent(
            this, callbackInfo, std::move(cachedComputePipeline))));
    }

    // New pipeline: create an event backed by system event that is really async.
    Ref<CreateComputePipelineAsyncEvent> event = AcquireRef(new CreateComputePipelineAsyncEvent(
        this, callbackInfo, std::move(uninitializedComputePipeline),
        AcquireRef(new SystemEvent())));
    Future future = GetFuture(event);
    InitializeComputePipelineAsyncImpl(std::move(event));
    return future;
}
PipelineLayoutBase* DeviceBase::APICreatePipelineLayout(
    const PipelineLayoutDescriptor* descriptor) {
    Ref<PipelineLayoutBase> result;
    if (ConsumedError(CreatePipelineLayout(descriptor), &result,
                      "calling %s.CreatePipelineLayout(%s).", this, descriptor)) {
        result = PipelineLayoutBase::MakeError(this, descriptor ? descriptor->label : nullptr);
    }
    return ReturnToAPI(std::move(result));
}
QuerySetBase* DeviceBase::APICreateQuerySet(const QuerySetDescriptor* descriptor) {
    Ref<QuerySetBase> result;
    if (ConsumedError(CreateQuerySet(descriptor), &result, InternalErrorType::OutOfMemory,
                      "calling %s.CreateQuerySet(%s).", this, descriptor)) {
        result = QuerySetBase::MakeError(this, descriptor);
    }
    return ReturnToAPI(std::move(result));
}
SamplerBase* DeviceBase::APICreateSampler(const SamplerDescriptor* descriptor) {
    Ref<SamplerBase> result;
    if (ConsumedError(CreateSampler(descriptor), &result, "calling %s.CreateSampler(%s).", this,
                      descriptor)) {
        result = SamplerBase::MakeError(this, descriptor ? descriptor->label : nullptr);
    }
    return ReturnToAPI(std::move(result));
}
void DeviceBase::APICreateRenderPipelineAsync(const RenderPipelineDescriptor* descriptor,
                                              WGPUCreateRenderPipelineAsyncCallback callback,
                                              void* userdata) {
    GetInstance()->EmitDeprecationWarning(
        "Old CreateRenderPipelineAsync APIs are deprecated. If using C please pass a CallbackInfo "
        "struct that has two userdatas. Otherwise, if using C++, please use templated helpers.");

    APICreateRenderPipelineAsync2(
        descriptor, {nullptr, WGPUCallbackMode_AllowProcessEvents,
                     [](WGPUCreatePipelineAsyncStatus status, WGPURenderPipeline pipeline,
                        char const* message, void* callback, void* userdata) {
                         auto cb =
                             reinterpret_cast<WGPUCreateRenderPipelineAsyncCallback>(callback);
                         cb(status, pipeline, message, userdata);
                     },
                     reinterpret_cast<void*>(callback), userdata});
}
Future DeviceBase::APICreateRenderPipelineAsyncF(
    const RenderPipelineDescriptor* descriptor,
    const CreateRenderPipelineAsyncCallbackInfo& callbackInfo) {
    GetInstance()->EmitDeprecationWarning(
        "Old CreateRenderPipelineAsync APIs are deprecated. If using C please pass a CallbackInfo "
        "struct that has two userdatas. Otherwise, if using C++, please use templated helpers.");
    return APICreateRenderPipelineAsync2(
        descriptor, {ToAPI(callbackInfo.nextInChain), ToAPI(callbackInfo.mode),
                     [](WGPUCreatePipelineAsyncStatus status, WGPURenderPipeline pipeline,
                        char const* message, void* callback, void* userdata) {
                         auto cb =
                             reinterpret_cast<WGPUCreateRenderPipelineAsyncCallback>(callback);
                         cb(status, pipeline, message, userdata);
                     },
                     reinterpret_cast<void*>(callbackInfo.callback), callbackInfo.userdata});
}
Future DeviceBase::APICreateRenderPipelineAsync2(
    const RenderPipelineDescriptor* descriptor,
    const WGPUCreateRenderPipelineAsyncCallbackInfo2& callbackInfo) {
    TRACE_EVENT1(GetPlatform(), General, "DeviceBase::APICreateRenderPipelineAsync", "label",
                 utils::GetLabelForTrace(descriptor->label));

    EventManager* manager = GetInstance()->GetEventManager();

    auto GetFuture = [&](Ref<EventManager::TrackedEvent>&& event) {
        FutureID futureID = manager->TrackEvent(std::move(event));
        return Future{futureID};
    };

    if (IsLost()) {
        // Device lost error: create an async event that completes when created.
        return GetFuture(AcquireRef(new CreateRenderPipelineAsyncEvent(
            this, callbackInfo, DAWN_DEVICE_LOST_ERROR("Device lost"), descriptor->label)));
    }

    auto resultOrError = CreateUninitializedRenderPipeline(descriptor);
    if (resultOrError.IsError()) {
        // Validation error: create an async event that completes when created.
        return GetFuture(AcquireRef(new CreateRenderPipelineAsyncEvent(
            this, callbackInfo, resultOrError.AcquireError(), descriptor->label)));
    }

    Ref<RenderPipelineBase> uninitializedRenderPipeline = resultOrError.AcquireSuccess();
    Ref<RenderPipelineBase> cachedRenderPipeline =
        GetCachedRenderPipeline(uninitializedRenderPipeline.Get());
    if (cachedRenderPipeline.Get() != nullptr) {
        // Cached pipeline: create an async event that completes when created.
        return GetFuture(AcquireRef(new CreateRenderPipelineAsyncEvent(
            this, callbackInfo, std::move(cachedRenderPipeline))));
    }

    // New pipeline: create an event backed by system event that is really async.
    Ref<CreateRenderPipelineAsyncEvent> event = AcquireRef(new CreateRenderPipelineAsyncEvent(
        this, callbackInfo, std::move(uninitializedRenderPipeline), AcquireRef(new SystemEvent())));
    Future future = GetFuture(event);
    InitializeRenderPipelineAsyncImpl(std::move(event));
    return future;
}
RenderBundleEncoder* DeviceBase::APICreateRenderBundleEncoder(
    const RenderBundleEncoderDescriptor* descriptor) {
    Ref<RenderBundleEncoder> result;
    if (ConsumedError(CreateRenderBundleEncoder(descriptor), &result,
                      "calling %s.CreateRenderBundleEncoder(%s).", this, descriptor)) {
        result = RenderBundleEncoder::MakeError(this, descriptor ? descriptor->label : nullptr);
    }
    return ReturnToAPI(std::move(result));
}
RenderPipelineBase* DeviceBase::APICreateRenderPipeline(
    const RenderPipelineDescriptor* descriptor) {
    TRACE_EVENT1(GetPlatform(), General, "DeviceBase::APICreateRenderPipeline", "label",
                 utils::GetLabelForTrace(descriptor->label));

    auto resultOrError = CreateRenderPipeline(descriptor);
    if (resultOrError.IsSuccess()) {
        return ReturnToAPI(resultOrError.AcquireSuccess());
    }

    // Acquire the device lock for error handling.
    // TODO(dawn:1662): Make error handling thread-safe.
    auto deviceLock(GetScopedLock());
    Ref<RenderPipelineBase> result;
    if (ConsumedError(std::move(resultOrError), &result, InternalErrorType::Internal,
                      "calling %s.CreateRenderPipeline(%s).", this, descriptor)) {
        result = RenderPipelineBase::MakeError(this, descriptor ? descriptor->label : nullptr);
    }
    return ReturnToAPI(std::move(result));
}
ShaderModuleBase* DeviceBase::APICreateShaderModule(const ShaderModuleDescriptor* descriptor) {
    TRACE_EVENT1(GetPlatform(), General, "DeviceBase::APICreateShaderModule", "label",
                 utils::GetLabelForTrace(descriptor->label));

    std::unique_ptr<OwnedCompilationMessages> compilationMessages(
        std::make_unique<OwnedCompilationMessages>());
    auto resultOrError = CreateShaderModule(descriptor, &compilationMessages);
    if (resultOrError.IsSuccess()) {
        Ref<ShaderModuleBase> result = resultOrError.AcquireSuccess();
        EmitCompilationLog(result.Get());
        return ReturnToAPI(std::move(result));
    }

    // Acquire the device lock for error handling.
    auto deviceLock(GetScopedLock());
    Ref<ShaderModuleBase> result;
    if (ConsumedError(std::move(resultOrError), &result, "calling %s.CreateShaderModule(%s).", this,
                      descriptor)) {
        DAWN_ASSERT(result == nullptr);
        result = ShaderModuleBase::MakeError(this, descriptor ? descriptor->label : nullptr);
        // Emit Tint errors and warnings for the error shader module.
        // Also move the compilation messages to the shader module so the application can later
        // retrieve it with GetCompilationInfo.
        result->InjectCompilationMessages(std::move(compilationMessages));
    }
    EmitCompilationLog(result.Get());
    return ReturnToAPI(std::move(result));
}

ShaderModuleBase* DeviceBase::APICreateErrorShaderModule(const ShaderModuleDescriptor* descriptor,
                                                         const char* errorMessage) {
    Ref<ShaderModuleBase> result =
        ShaderModuleBase::MakeError(this, descriptor ? descriptor->label : nullptr);
    std::unique_ptr<OwnedCompilationMessages> compilationMessages(
        std::make_unique<OwnedCompilationMessages>());
    compilationMessages->AddUnanchoredMessage(errorMessage, wgpu::CompilationMessageType::Error);
    result->InjectCompilationMessages(std::move(compilationMessages));
    EmitCompilationLog(result.Get());

    std::unique_ptr<ErrorData> errorData =
        DAWN_VALIDATION_ERROR("Error in calling %s.CreateShaderModule(%s).", this, descriptor);
    ConsumeError(std::move(errorData));

    return ReturnToAPI(std::move(result));
}
SwapChainBase* DeviceBase::APICreateSwapChain(Surface* surface,
                                              const SwapChainDescriptor* descriptor) {
    Ref<SwapChainBase> result;
    if (ConsumedError(CreateSwapChain(surface, descriptor), &result,
                      "calling %s.CreateSwapChain(%s).", this, descriptor)) {
        SurfaceConfiguration config;
        config.nextInChain = descriptor->nextInChain;
        config.device = this;
        config.width = descriptor->width;
        config.height = descriptor->height;
        config.format = descriptor->format;
        config.usage = descriptor->usage;
        config.presentMode = descriptor->presentMode;
        config.viewFormatCount = 0;
        config.viewFormats = nullptr;
        config.alphaMode = wgpu::CompositeAlphaMode::Opaque;
        result = SwapChainBase::MakeError(this, &config);
    }
    return ReturnToAPI(std::move(result));
}
TextureBase* DeviceBase::APICreateTexture(const TextureDescriptor* descriptor) {
    Ref<TextureBase> result;
    if (ConsumedError(CreateTexture(descriptor), &result, InternalErrorType::OutOfMemory,
                      "calling %s.CreateTexture(%s).", this, descriptor)) {
        result = TextureBase::MakeError(this, descriptor);
    }
    return ReturnToAPI(std::move(result));
}

wgpu::TextureUsage DeviceBase::APIGetSupportedSurfaceUsage(Surface* surface) {
    wgpu::TextureUsage result;
    if (ConsumedError(GetSupportedSurfaceUsage(surface), &result,
                      "calling %s.GetSupportedSurfaceUsage().", this)) {
        return wgpu::TextureUsage::None;
    }
    return result;
}

// For Dawn Wire

BufferBase* DeviceBase::APICreateErrorBuffer(const BufferDescriptor* desc) {
    UnpackedPtr<BufferDescriptor> unpacked;
    if (!ConsumedError(ValidateBufferDescriptor(this, desc), &unpacked,
                       InternalErrorType::OutOfMemory, "calling %s.CreateBuffer(%s).", this,
                       desc)) {
        auto* clientErrorInfo = unpacked.Get<DawnBufferDescriptorErrorInfoFromWireClient>();
        if (clientErrorInfo != nullptr && clientErrorInfo->outOfMemory) {
            HandleError(DAWN_OUT_OF_MEMORY_ERROR("Failed to allocate memory for buffer mapping"),
                        InternalErrorType::OutOfMemory);
        }
    }

    // Set the size of the error buffer to 0 as this function is called only when an OOM happens at
    // the client side.
    BufferDescriptor fakeDescriptor = *desc;
    fakeDescriptor.size = 0;
    return ReturnToAPI(BufferBase::MakeError(this, &fakeDescriptor));
}

ExternalTextureBase* DeviceBase::APICreateErrorExternalTexture() {
    return ReturnToAPI(ExternalTextureBase::MakeError(this));
}

TextureBase* DeviceBase::APICreateErrorTexture(const TextureDescriptor* desc) {
    return ReturnToAPI(TextureBase::MakeError(this, desc));
}

// Other Device API methods

// Returns true if future ticking is needed.
bool DeviceBase::APITick() {
    // TODO(dawn:1987) Add deprecation warning when Instance.ProcessEvents no longer calls this.

    // Tick may trigger callbacks which drop a ref to the device itself. Hold a Ref to ourselves
    // to avoid deleting |this| in the middle of this function call.
    Ref<DeviceBase> self(this);
    bool tickError;
    {
        // Note: we cannot hold the lock when flushing the callbacks so have to limit the scope of
        // the lock here.
        auto deviceLock(GetScopedLock());
        tickError = ConsumedError(Tick());
    }

    // We have to check callback tasks in every APITick because it is not related to any global
    // serials.
    FlushCallbackTaskQueue();

    if (tickError) {
        return false;
    }

    auto deviceLock(GetScopedLock());
    // We don't throw an error when device is lost. This allows pending callbacks to be
    // executed even after the Device is lost/destroyed.
    if (IsLost()) {
        return HasPendingTasks();
    }

    TRACE_EVENT1(GetPlatform(), General, "DeviceBase::APITick::IsDeviceIdle", "isDeviceIdle",
                 IsDeviceIdle());

    return !IsDeviceIdle();
}

MaybeError DeviceBase::Tick() {
    if (IsLost() || !mQueue->HasScheduledCommands()) {
        return {};
    }

    // To avoid overly ticking, we only want to tick when:
    // 1. the last submitted serial has moved beyond the completed serial
    // 2. or the backend still has pending commands to submit.
    DAWN_TRY(mQueue->CheckPassedSerials());
    DAWN_TRY(TickImpl());

    // TODO(crbug.com/dawn/833): decouple TickImpl from updating the serial so that we can
    // tick the dynamic uploader before the backend resource allocators. This would allow
    // reclaiming resources one tick earlier.
    mDynamicUploader->Deallocate(mQueue->GetCompletedCommandSerial());
    mQueue->Tick(mQueue->GetCompletedCommandSerial());

    return {};
}

AdapterBase* DeviceBase::APIGetAdapter() {
    mAdapter->APIAddRef();
    return mAdapter.Get();
}

QueueBase* DeviceBase::APIGetQueue() {
    // Backends gave the primary queue during initialization.
    DAWN_ASSERT(mQueue != nullptr);
    auto queue = mQueue;
    return ReturnToAPI(std::move(queue));
}

ExternalTextureBase* DeviceBase::APICreateExternalTexture(
    const ExternalTextureDescriptor* descriptor) {
    Ref<ExternalTextureBase> result;
    if (ConsumedError(CreateExternalTextureImpl(descriptor), &result,
                      "calling %s.CreateExternalTexture(%s).", this, descriptor)) {
        result = ExternalTextureBase::MakeError(this);
    }

    return ReturnToAPI(std::move(result));
}

SharedBufferMemoryBase* DeviceBase::APIImportSharedBufferMemory(
    const SharedBufferMemoryDescriptor* descriptor) {
    Ref<SharedBufferMemoryBase> result = nullptr;
    if (ConsumedError(
            [&]() -> ResultOrError<Ref<SharedBufferMemoryBase>> {
                DAWN_TRY(ValidateIsAlive());
                return ImportSharedBufferMemoryImpl(descriptor);
            }(),
            &result, "calling %s.ImportSharedBufferMemory(%s).", this, descriptor)) {
        return SharedBufferMemoryBase::MakeError(this, descriptor);
    }
    return result.Detach();
}

ResultOrError<Ref<SharedBufferMemoryBase>> DeviceBase::ImportSharedBufferMemoryImpl(
    const SharedBufferMemoryDescriptor* descriptor) {
    return DAWN_UNIMPLEMENTED_ERROR("Not implemented");
}

SharedTextureMemoryBase* DeviceBase::APIImportSharedTextureMemory(
    const SharedTextureMemoryDescriptor* descriptor) {
    Ref<SharedTextureMemoryBase> result;
    if (ConsumedError(
            [&]() -> ResultOrError<Ref<SharedTextureMemoryBase>> {
                DAWN_TRY(ValidateIsAlive());
                return ImportSharedTextureMemoryImpl(descriptor);
            }(),
            &result, "calling %s.ImportSharedTextureMemory(%s).", this, descriptor)) {
        result = SharedTextureMemoryBase::MakeError(this, descriptor);
    }
    return ReturnToAPI(std::move(result));
}

ResultOrError<Ref<SharedTextureMemoryBase>> DeviceBase::ImportSharedTextureMemoryImpl(
    const SharedTextureMemoryDescriptor* descriptor) {
    return DAWN_UNIMPLEMENTED_ERROR("Not implemented");
}

SharedFenceBase* DeviceBase::APIImportSharedFence(const SharedFenceDescriptor* descriptor) {
    Ref<SharedFenceBase> result;
    if (ConsumedError(
            [&]() -> ResultOrError<Ref<SharedFenceBase>> {
                DAWN_TRY(ValidateIsAlive());
                return ImportSharedFenceImpl(descriptor);
            }(),
            &result, "calling %s.ImportSharedFence(%s).", this, descriptor)) {
        result = SharedFenceBase::MakeError(this, descriptor);
    }
    return ReturnToAPI(std::move(result));
}

ResultOrError<Ref<SharedFenceBase>> DeviceBase::ImportSharedFenceImpl(
    const SharedFenceDescriptor* descriptor) {
    return DAWN_UNIMPLEMENTED_ERROR("Not implemented");
}

void DeviceBase::ApplyFeatures(const UnpackedPtr<DeviceDescriptor>& deviceDescriptor) {
    DAWN_ASSERT(deviceDescriptor);
    // Validate all required features with device toggles.
    DAWN_ASSERT(GetPhysicalDevice()->SupportsAllRequiredFeatures(
        {deviceDescriptor->requiredFeatures, deviceDescriptor->requiredFeatureCount}, mToggles));

    for (uint32_t i = 0; i < deviceDescriptor->requiredFeatureCount; ++i) {
        mEnabledFeatures.EnableFeature(deviceDescriptor->requiredFeatures[i]);
    }
}

bool DeviceBase::HasFeature(Feature feature) const {
    return mEnabledFeatures.IsEnabled(feature);
}

void DeviceBase::SetWGSLExtensionAllowList() {
    // Set the WGSL extensions and language features allow list based on device's enabled features
    // and other properties.
    if (mEnabledFeatures.IsEnabled(Feature::ShaderF16)) {
        mWGSLAllowedFeatures.extensions.insert(tint::wgsl::Extension::kF16);
    }
    if (mEnabledFeatures.IsEnabled(Feature::ChromiumExperimentalSubgroups)) {
        mWGSLAllowedFeatures.extensions.insert(
            tint::wgsl::Extension::kChromiumExperimentalSubgroups);
    }
    if (IsToggleEnabled(Toggle::AllowUnsafeAPIs)) {
        mWGSLAllowedFeatures.extensions.insert(
            tint::wgsl::Extension::kChromiumDisableUniformityAnalysis);
        mWGSLAllowedFeatures.extensions.insert(tint::wgsl::Extension::kChromiumInternalGraphite);
    }
    if (mEnabledFeatures.IsEnabled(Feature::DualSourceBlending)) {
        mWGSLAllowedFeatures.extensions.insert(tint::wgsl::Extension::kDualSourceBlending);
    }
    if (mEnabledFeatures.IsEnabled(Feature::PixelLocalStorageNonCoherent) ||
        mEnabledFeatures.IsEnabled(Feature::PixelLocalStorageCoherent)) {
        mWGSLAllowedFeatures.extensions.insert(
            tint::wgsl::Extension::kChromiumExperimentalPixelLocal);
    }
    if (mEnabledFeatures.IsEnabled(Feature::FramebufferFetch)) {
        mWGSLAllowedFeatures.extensions.insert(
            tint::wgsl::Extension::kChromiumExperimentalFramebufferFetch);
    }

    // Language features are enabled instance-wide.
    // mAdapter is not set for mock test devices.
    // TODO(crbug.com/dawn/1702): using a mock adapter and instance could avoid the null checking.
    if (mAdapter != nullptr) {
        const auto& allowedFeatures = GetInstance()->GetAllowedWGSLLanguageFeatures();
        mWGSLAllowedFeatures.features = {allowedFeatures.begin(), allowedFeatures.end()};
    }
}

const tint::wgsl::AllowedFeatures& DeviceBase::GetWGSLAllowedFeatures() const {
    return mWGSLAllowedFeatures;
}

void DeviceBase::EnableAdditionalWGSLExtension(tint::wgsl::Extension extension) {
    mWGSLAllowedFeatures.extensions.insert(extension);
}

bool DeviceBase::IsValidationEnabled() const {
    return !IsToggleEnabled(Toggle::SkipValidation);
}

bool DeviceBase::IsRobustnessEnabled() const {
    return !IsToggleEnabled(Toggle::DisableRobustness);
}

bool DeviceBase::IsCompatibilityMode() const {
    return mAdapter != nullptr && mAdapter->GetFeatureLevel() == FeatureLevel::Compatibility;
}

bool DeviceBase::IsImmediateErrorHandlingEnabled() const {
    return mIsImmediateErrorHandlingEnabled;
}

size_t DeviceBase::GetLazyClearCountForTesting() {
    return mLazyClearCountForTesting;
}

void DeviceBase::IncrementLazyClearCountForTesting() {
    ++mLazyClearCountForTesting;
}

void DeviceBase::EmitWarningOnce(const std::string& message) {
    if (mWarnings.insert(message).second) {
        this->EmitLog(WGPULoggingType_Warning, message.c_str());
    }
}

void DeviceBase::EmitCompilationLog(const ShaderModuleBase* module) {
    const OwnedCompilationMessages* messages = module->GetCompilationMessages();
    if (!messages->HasWarningsOrErrors()) {
        return;
    }

    // Limit the number of compilation error emitted to avoid spamming the devtools console hard.
    constexpr uint32_t kCompilationLogSpamLimit = 20;
    if (mEmittedCompilationLogCount.load(std::memory_order_acquire) > kCompilationLogSpamLimit) {
        return;
    }

    if (mEmittedCompilationLogCount.fetch_add(1, std::memory_order_acq_rel) ==
        kCompilationLogSpamLimit - 1) {
        // Note: if there are multiple threads emitting logs, this may not actually be the exact
        // last message. This is probably not a huge problem since this message will be emitted
        // somewhere near the end.
        return EmitLog(WGPULoggingType_Warning,
                       "Reached the WGSL compilation log warning limit. To see all the compilation "
                       "logs, query them directly on the ShaderModule objects.");
    }

    // Emit the formatted Tint errors and warnings.
    std::ostringstream t;
    t << absl::StrFormat("Compilation log for %s:", module);
    for (const auto& pMessage : messages->GetFormattedTintMessages()) {
        t << "\n" << pMessage;
    }

    EmitLog(WGPULoggingType_Warning, t.str().c_str());
}

void DeviceBase::EmitLog(const char* message) {
    this->EmitLog(WGPULoggingType_Info, message);
}

void DeviceBase::EmitLog(WGPULoggingType loggingType, const char* message) {
    // Acquire a shared lock. This allows multiple threads to emit logs,
    // or even logs to be emitted re-entrantly. It will block if there is a call
    // to SetLoggingCallback. Applications should not call SetLoggingCallback inside
    // the logging callback or they will deadlock.
    std::shared_lock<std::shared_mutex> lock(mLoggingMutex);
    if (mLoggingCallback) {
        mLoggingCallback(loggingType, message, mLoggingUserdata);
    }
}

wgpu::Status DeviceBase::APIGetAHardwareBufferProperties(void* handle,
                                                         AHardwareBufferProperties* properties) {
    if (!HasFeature(Feature::SharedTextureMemoryAHardwareBuffer)) {
        ConsumeError(
            DAWN_VALIDATION_ERROR("Queried APIGetAHardwareBufferProperties() on %s "
                                  "without the %s feature being set.",
                                  this, ToAPI(Feature::SharedTextureMemoryAHardwareBuffer)));
        return wgpu::Status::Error;
    }

    if (ConsumedError(GetAHardwareBufferPropertiesImpl(handle, properties))) {
        return wgpu::Status::Error;
    }

    return wgpu::Status::Success;
}

wgpu::Status DeviceBase::APIGetLimits(SupportedLimits* limits) const {
    DAWN_ASSERT(limits != nullptr);
    InstanceBase* instance = GetAdapter()->GetInstance();

    UnpackedPtr<SupportedLimits> unpacked;
    if (instance->ConsumedError(ValidateAndUnpack(limits), &unpacked)) {
        return wgpu::Status::Error;
    }

    limits->limits = mLimits.v1;

    if (auto* subgroupLimits = unpacked.Get<DawnExperimentalSubgroupLimits>()) {
        if (!mToggles.IsEnabled(Toggle::AllowUnsafeAPIs)) {
            // If AllowUnsafeAPIs is not enabled, return the default-initialized
            // DawnExperimentalSubgroupLimits object, where minSubgroupSize and
            // maxSubgroupSize are WGPU_LIMIT_U32_UNDEFINED.
            *subgroupLimits = DawnExperimentalSubgroupLimits{};
        } else {
            *subgroupLimits = mLimits.experimentalSubgroupLimits;
        }
    }

    return wgpu::Status::Success;
}

bool DeviceBase::APIHasFeature(wgpu::FeatureName feature) const {
    return mEnabledFeatures.IsEnabled(feature);
}

size_t DeviceBase::APIEnumerateFeatures(wgpu::FeatureName* features) const {
    return mEnabledFeatures.EnumerateFeatures(features);
}

void DeviceBase::APIInjectError(wgpu::ErrorType type, const char* message) {
    if (ConsumedError(ValidateErrorType(type))) {
        return;
    }

    // This method should only be used to make error scope reject. For DeviceLost there is the
    // LoseForTesting function that can be used instead.
    if (type != wgpu::ErrorType::Validation && type != wgpu::ErrorType::OutOfMemory) {
        HandleError(
            DAWN_VALIDATION_ERROR("Invalid injected error, must be Validation or OutOfMemory"));
        return;
    }

    HandleError(DAWN_MAKE_ERROR(FromWGPUErrorType(type), message), InternalErrorType::OutOfMemory);
}

void DeviceBase::APIValidateTextureDescriptor(const TextureDescriptor* descriptorOrig) {
    AllowMultiPlanarTextureFormat allowMultiPlanar;
    if (HasFeature(Feature::MultiPlanarFormatExtendedUsages)) {
        allowMultiPlanar = AllowMultiPlanarTextureFormat::Yes;
    } else {
        allowMultiPlanar = AllowMultiPlanarTextureFormat::No;
    }

    TextureDescriptor rawDescriptor = descriptorOrig->WithTrivialFrontendDefaults();

    UnpackedPtr<TextureDescriptor> unpacked;
    if (!ConsumedError(ValidateAndUnpack(&rawDescriptor), &unpacked)) {
        [[maybe_unused]] bool hadError =
            ConsumedError(ValidateTextureDescriptor(this, unpacked, allowMultiPlanar));
    }
}

QueueBase* DeviceBase::GetQueue() const {
    DAWN_ASSERT(mQueue != nullptr);
    return mQueue.Get();
}

// Implementation details of object creation

ResultOrError<Ref<BindGroupBase>> DeviceBase::CreateBindGroup(const BindGroupDescriptor* descriptor,
                                                              UsageValidationMode mode) {
    DAWN_TRY(ValidateIsAlive());
    if (IsValidationEnabled()) {
        DAWN_TRY_CONTEXT(ValidateBindGroupDescriptor(this, descriptor, mode),
                         "validating %s against %s", descriptor, descriptor->layout);
    }
    return CreateBindGroupImpl(descriptor);
}

ResultOrError<Ref<BindGroupLayoutBase>> DeviceBase::CreateBindGroupLayout(
    const BindGroupLayoutDescriptor* descriptor,
    bool allowInternalBinding) {
    DAWN_TRY(ValidateIsAlive());
    if (IsValidationEnabled()) {
        DAWN_TRY_CONTEXT(ValidateBindGroupLayoutDescriptor(this, descriptor, allowInternalBinding),
                         "validating %s", descriptor);
    }
    return GetOrCreateBindGroupLayout(descriptor);
}

ResultOrError<Ref<BufferBase>> DeviceBase::CreateBuffer(const BufferDescriptor* rawDescriptor) {
    DAWN_TRY(ValidateIsAlive());
    UnpackedPtr<BufferDescriptor> descriptor;
    if (IsValidationEnabled()) {
        DAWN_TRY_ASSIGN(descriptor, ValidateBufferDescriptor(this, rawDescriptor));
    } else {
        descriptor = Unpack(rawDescriptor);
    }

    Ref<BufferBase> buffer;
    DAWN_TRY_ASSIGN(buffer, CreateBufferImpl(descriptor));

    if (descriptor->mappedAtCreation) {
        DAWN_TRY(buffer->MapAtCreation());
    }

    return std::move(buffer);
}

ResultOrError<Ref<ComputePipelineBase>> DeviceBase::CreateComputePipeline(
    const ComputePipelineDescriptor* descriptor) {
    // If a pipeline layout is not specified, we cannot use cached pipelines.
    bool useCache = descriptor->layout != nullptr;

    Ref<ComputePipelineBase> uninitializedComputePipeline;
    DAWN_TRY_ASSIGN(uninitializedComputePipeline, CreateUninitializedComputePipeline(descriptor));

    if (useCache) {
        Ref<ComputePipelineBase> cachedComputePipeline =
            GetCachedComputePipeline(uninitializedComputePipeline.Get());
        if (cachedComputePipeline.Get() != nullptr) {
            return cachedComputePipeline;
        }
    }

    MaybeError maybeError;
    {
        SCOPED_DAWN_HISTOGRAM_TIMER_MICROS(GetPlatform(), "CreateComputePipelineUS");
        maybeError = uninitializedComputePipeline->Initialize();
    }
    DAWN_HISTOGRAM_BOOLEAN(GetPlatform(), "CreateComputePipelineSuccess", maybeError.IsSuccess());

    DAWN_TRY(std::move(maybeError));
    return useCache ? AddOrGetCachedComputePipeline(std::move(uninitializedComputePipeline))
                    : std::move(uninitializedComputePipeline);
}

ResultOrError<Ref<CommandEncoder>> DeviceBase::CreateCommandEncoder(
    const CommandEncoderDescriptor* descriptor) {
    const CommandEncoderDescriptor defaultDescriptor = {};
    if (descriptor == nullptr) {
        descriptor = &defaultDescriptor;
    }

    DAWN_TRY(ValidateIsAlive());
    UnpackedPtr<CommandEncoderDescriptor> unpacked;
    if (IsValidationEnabled()) {
        DAWN_TRY_ASSIGN(unpacked, ValidateCommandEncoderDescriptor(this, descriptor));
    } else {
        unpacked = Unpack(descriptor);
    }
    return CommandEncoder::Create(this, unpacked);
}

// Overwritten on the backends to return pipeline caches if supported.
Ref<PipelineCacheBase> DeviceBase::GetOrCreatePipelineCacheImpl(const CacheKey& key) {
    DAWN_UNREACHABLE();
}

ResultOrError<Ref<ComputePipelineBase>> DeviceBase::CreateUninitializedComputePipeline(
    const ComputePipelineDescriptor* descriptor) {
    DAWN_TRY(ValidateIsAlive());
    if (IsValidationEnabled()) {
        DAWN_TRY(ValidateComputePipelineDescriptor(this, descriptor));
    }

    Ref<PipelineLayoutBase> layoutRef;
    ComputePipelineDescriptor appliedDescriptor;
    DAWN_TRY_ASSIGN(layoutRef, ValidateLayoutAndGetComputePipelineDescriptorWithDefaults(
                                   this, *descriptor, &appliedDescriptor));

    return CreateUninitializedComputePipelineImpl(Unpack(&appliedDescriptor));
}

// This base version is creating the pipeline synchronously,
// and is overwritten on the backends that actually support asynchronous pipeline creation.
void DeviceBase::InitializeComputePipelineAsyncImpl(Ref<CreateComputePipelineAsyncEvent> event) {
    event->InitializeSync();
}

// This base version is creating the pipeline synchronously,
// and is overwritten on the backends that actually support asynchronous pipeline creation.
void DeviceBase::InitializeRenderPipelineAsyncImpl(Ref<CreateRenderPipelineAsyncEvent> event) {
    event->InitializeSync();
}

ResultOrError<Ref<PipelineLayoutBase>> DeviceBase::CreatePipelineLayout(
    const PipelineLayoutDescriptor* descriptor,
    PipelineCompatibilityToken pipelineCompatibilityToken) {
    DAWN_TRY(ValidateIsAlive());
    UnpackedPtr<PipelineLayoutDescriptor> unpacked;
    if (IsValidationEnabled()) {
        DAWN_TRY_ASSIGN(unpacked, ValidatePipelineLayoutDescriptor(this, descriptor,
                                                                   pipelineCompatibilityToken));
    } else {
        unpacked = Unpack(descriptor);
    }

    // When we are not creating explicit pipeline layouts, i.e. we are using 'auto', don't use the
    // cache.
    if (pipelineCompatibilityToken != kExplicitPCT) {
        Ref<PipelineLayoutBase> result;
        DAWN_TRY_ASSIGN(result, CreatePipelineLayoutImpl(unpacked));
        result->SetContentHash(result->ComputeContentHash());
        return result;
    }
    return GetOrCreatePipelineLayout(unpacked);
}

ResultOrError<Ref<ExternalTextureBase>> DeviceBase::CreateExternalTextureImpl(
    const ExternalTextureDescriptor* descriptor) {
    DAWN_TRY(ValidateIsAlive());
    if (IsValidationEnabled()) {
        DAWN_TRY_CONTEXT(ValidateExternalTextureDescriptor(this, descriptor), "validating %s",
                         descriptor);
    }

    return ExternalTextureBase::Create(this, descriptor);
}

ResultOrError<Ref<QuerySetBase>> DeviceBase::CreateQuerySet(const QuerySetDescriptor* descriptor) {
    DAWN_TRY(ValidateIsAlive());
    if (IsValidationEnabled()) {
        DAWN_TRY_CONTEXT(ValidateQuerySetDescriptor(this, descriptor), "validating %s", descriptor);
    }
    return CreateQuerySetImpl(descriptor);
}

ResultOrError<Ref<RenderBundleEncoder>> DeviceBase::CreateRenderBundleEncoder(
    const RenderBundleEncoderDescriptor* descriptor) {
    DAWN_TRY(ValidateIsAlive());
    if (IsValidationEnabled()) {
        DAWN_TRY_CONTEXT(ValidateRenderBundleEncoderDescriptor(this, descriptor),
                         "validating render bundle encoder descriptor.");
    }
    return RenderBundleEncoder::Create(this, descriptor);
}

ResultOrError<Ref<RenderPipelineBase>> DeviceBase::CreateRenderPipeline(
    const RenderPipelineDescriptor* descriptor,
    bool allowInternalBinding) {
    // If a pipeline layout is not specified, we cannot use cached pipelines.
    bool useCache = descriptor->layout != nullptr;

    Ref<RenderPipelineBase> uninitializedRenderPipeline;
    DAWN_TRY_ASSIGN(uninitializedRenderPipeline,
                    CreateUninitializedRenderPipeline(descriptor, allowInternalBinding));

    if (useCache) {
        Ref<RenderPipelineBase> cachedRenderPipeline =
            GetCachedRenderPipeline(uninitializedRenderPipeline.Get());
        if (cachedRenderPipeline != nullptr) {
            return cachedRenderPipeline;
        }
    }

    MaybeError maybeError;
    {
        SCOPED_DAWN_HISTOGRAM_TIMER_MICROS(GetPlatform(), "CreateRenderPipelineUS");
        maybeError = uninitializedRenderPipeline->Initialize();
    }
    DAWN_HISTOGRAM_BOOLEAN(GetPlatform(), "CreateRenderPipelineSuccess", maybeError.IsSuccess());

    DAWN_TRY(std::move(maybeError));
    return useCache ? AddOrGetCachedRenderPipeline(std::move(uninitializedRenderPipeline))
                    : std::move(uninitializedRenderPipeline);
}

ResultOrError<Ref<RenderPipelineBase>> DeviceBase::CreateUninitializedRenderPipeline(
    const RenderPipelineDescriptor* descriptor,
    bool allowInternalBinding) {
    DAWN_TRY(ValidateIsAlive());
    if (IsValidationEnabled()) {
        DAWN_TRY(ValidateRenderPipelineDescriptor(this, descriptor));

        // Validation for kMaxBindGroupsPlusVertexBuffers is skipped because it is not necessary so
        // far.
        static_assert(kMaxBindGroups + kMaxVertexBuffers <= kMaxBindGroupsPlusVertexBuffers);
    }

    // Ref will keep the pipeline layout alive until the end of the function where
    // the pipeline will take another reference.
    Ref<PipelineLayoutBase> layoutRef;
    RenderPipelineDescriptor appliedDescriptor;
    DAWN_TRY_ASSIGN(layoutRef, ValidateLayoutAndGetRenderPipelineDescriptorWithDefaults(
                                   this, *descriptor, &appliedDescriptor, allowInternalBinding));

    return CreateUninitializedRenderPipelineImpl(Unpack(&appliedDescriptor));
}

ResultOrError<Ref<SamplerBase>> DeviceBase::CreateSampler(const SamplerDescriptor* descriptorOrig) {
    DAWN_TRY(ValidateIsAlive());

    SamplerDescriptor descriptor = {};
    if (descriptorOrig) {
        descriptor = descriptorOrig->WithTrivialFrontendDefaults();
    }

    if (IsValidationEnabled()) {
        DAWN_TRY_CONTEXT(ValidateSamplerDescriptor(this, &descriptor), "validating %s",
                         &descriptor);
    }

    return GetOrCreateSampler(&descriptor);
}

ResultOrError<Ref<ShaderModuleBase>> DeviceBase::CreateShaderModule(
    const ShaderModuleDescriptor* descriptor,
    std::unique_ptr<OwnedCompilationMessages>* compilationMessages) {
    DAWN_TRY(ValidateIsAlive());

    // CreateShaderModule can be called from inside dawn_native. If that's the case handle the
    // error directly in Dawn and no compilationMessages held in the shader module. It is ok as
    // long as dawn_native don't use the compilationMessages of these internal shader modules.
    ShaderModuleParseResult parseResult;

    UnpackedPtr<ShaderModuleDescriptor> unpacked;
    if (IsValidationEnabled()) {
        DAWN_TRY_ASSIGN_CONTEXT(unpacked, ValidateAndUnpack(descriptor),
                                "validating and unpacking %s", descriptor);
        DAWN_TRY_CONTEXT(ValidateAndParseShaderModule(
                             this, unpacked, &parseResult,
                             compilationMessages ? compilationMessages->get() : nullptr),
                         "validating %s", descriptor);
    } else {
        unpacked = Unpack(descriptor);
    }

    return GetOrCreateShaderModule(unpacked, &parseResult, compilationMessages);
}

ResultOrError<Ref<SwapChainBase>> DeviceBase::CreateSwapChain(
    Surface* surface,
    const SwapChainDescriptor* descriptor) {
    GetInstance()->EmitDeprecationWarning(
        "The explicit creation of a SwapChain object is deprecated and should be replaced by "
        "Surface configuration.");

    DAWN_TRY(ValidateIsAlive());
    if (IsValidationEnabled()) {
        DAWN_TRY_CONTEXT(ValidateSwapChainDescriptor(this, surface, descriptor), "validating %s",
                         descriptor);
    }

    SurfaceConfiguration config;
    config.nextInChain = descriptor->nextInChain;
    config.device = this;
    config.width = descriptor->width;
    config.height = descriptor->height;
    config.format = descriptor->format;
    config.usage = descriptor->usage;
    config.presentMode = descriptor->presentMode;
    config.viewFormatCount = 0;
    config.viewFormats = nullptr;
    config.alphaMode = wgpu::CompositeAlphaMode::Opaque;

    SwapChainBase* previousSwapChain = surface->GetAttachedSwapChain();
    ResultOrError<Ref<SwapChainBase>> maybeNewSwapChain =
        CreateSwapChainImpl(surface, previousSwapChain, &config);

    if (previousSwapChain != nullptr) {
        previousSwapChain->DetachFromSurface();
    }

    Ref<SwapChainBase> newSwapChain;
    DAWN_TRY_ASSIGN(newSwapChain, std::move(maybeNewSwapChain));

    newSwapChain->SetIsAttached();
    surface->SetAttachedSwapChain(newSwapChain.Get());
    return newSwapChain;
}

ResultOrError<Ref<SwapChainBase>> DeviceBase::CreateSwapChain(Surface* surface,
                                                              SwapChainBase* previousSwapChain,
                                                              const SurfaceConfiguration* config) {
    // Nothing to validate here as it is done in Surface::Configure
    return CreateSwapChainImpl(surface, previousSwapChain, config);
}

ResultOrError<Ref<TextureBase>> DeviceBase::CreateTexture(const TextureDescriptor* descriptorOrig) {
    DAWN_TRY(ValidateIsAlive());

    TextureDescriptor rawDescriptor = descriptorOrig->WithTrivialFrontendDefaults();

    UnpackedPtr<TextureDescriptor> descriptor;
    if (IsValidationEnabled()) {
        AllowMultiPlanarTextureFormat allowMultiPlanar;
        if (HasFeature(Feature::MultiPlanarFormatExtendedUsages)) {
            allowMultiPlanar = AllowMultiPlanarTextureFormat::SingleLayerOnly;
        } else {
            allowMultiPlanar = AllowMultiPlanarTextureFormat::No;
        }
        DAWN_TRY_ASSIGN_CONTEXT(descriptor, ValidateAndUnpack(&rawDescriptor), "validating %s.",
                                &rawDescriptor);
        DAWN_TRY_CONTEXT(ValidateTextureDescriptor(this, descriptor, allowMultiPlanar),
                         "validating %s.", descriptor);
    } else {
        descriptor = Unpack(&rawDescriptor);
    }

    return CreateTextureImpl(descriptor);
}

ResultOrError<Ref<TextureViewBase>> DeviceBase::CreateTextureView(
    TextureBase* texture,
    const TextureViewDescriptor* descriptorOrig) {
    DAWN_TRY(ValidateIsAlive());
    DAWN_TRY(ValidateObject(texture));

    TextureViewDescriptor desc;
    DAWN_TRY_ASSIGN(desc, GetTextureViewDescriptorWithDefaults(texture, descriptorOrig));

    UnpackedPtr<TextureViewDescriptor> descriptor;
    if (IsValidationEnabled()) {
        DAWN_TRY_ASSIGN_CONTEXT(descriptor, ValidateAndUnpack(&desc), "validating %s.", &desc);
        DAWN_TRY_CONTEXT(ValidateTextureViewDescriptor(this, texture, descriptor),
                         "validating %s against %s.", descriptor, texture);
    } else {
        descriptor = Unpack(&desc);
    }
    return CreateTextureViewImpl(texture, descriptor);
}

ResultOrError<wgpu::TextureUsage> DeviceBase::GetSupportedSurfaceUsage(
    const Surface* surface) const {
    GetInstance()->EmitDeprecationWarning(
        "GetSupportedSurfaceUsage is deprecated, use surface.GetCapabilities(adapter).usages.");

    DAWN_TRY(ValidateIsAlive());

    if (IsValidationEnabled()) {
        DAWN_INVALID_IF(!HasFeature(Feature::SurfaceCapabilities), "%s is not enabled.",
                        wgpu::FeatureName::SurfaceCapabilities);
    }

    PhysicalDeviceSurfaceCapabilities caps;
    DAWN_TRY_ASSIGN(caps, GetPhysicalDevice()->GetSurfaceCapabilities(GetInstance(), surface));
    return caps.usages;
}

// Other implementation details

DynamicUploader* DeviceBase::GetDynamicUploader() const {
    return mDynamicUploader.get();
}

// The Toggle device facility

std::vector<const char*> DeviceBase::GetTogglesUsed() const {
    return mToggles.GetEnabledToggleNames();
}

bool DeviceBase::IsToggleEnabled(Toggle toggle) const {
    return mToggles.IsEnabled(toggle);
}

const TogglesState& DeviceBase::GetTogglesState() const {
    return mToggles;
}

void DeviceBase::ForceSetToggleForTesting(Toggle toggle, bool isEnabled) {
    mToggles.ForceSet(toggle, isEnabled);
}

void DeviceBase::ForceEnableFeatureForTesting(Feature feature) {
    mEnabledFeatures.EnableFeature(feature);
    mFormatTable = BuildFormatTable(this);
}

void DeviceBase::FlushCallbackTaskQueue() {
    // Callbacks might cause re-entrances. Mutex shouldn't be locked. So we expect there is no
    // locked mutex before entering this method.
    DAWN_ASSERT(mMutex == nullptr || !mMutex->IsLockedByCurrentThread());

    Ref<CallbackTaskManager> callbackTaskManager;

    {
        // This is a data race with the assignment to InstanceBase's callback queue manager in
        // WillDropLastExternalRef(). Need to protect with a lock and keep the old
        // mCallbackTaskManager alive.
        // TODO(crbug.com/dawn/752): In future, all devices should use InstanceBase's callback queue
        // manager from the start. So we won't need to care about data race at that point.
        auto deviceLock(GetScopedLock());
        callbackTaskManager = mCallbackTaskManager;
    }

    callbackTaskManager->Flush();
}

const CombinedLimits& DeviceBase::GetLimits() const {
    return mLimits;
}

AsyncTaskManager* DeviceBase::GetAsyncTaskManager() const {
    return mAsyncTaskManager.get();
}

CallbackTaskManager* DeviceBase::GetCallbackTaskManager() const {
    return mCallbackTaskManager.Get();
}

dawn::platform::WorkerTaskPool* DeviceBase::GetWorkerTaskPool() const {
    return mWorkerTaskPool.get();
}

PipelineCompatibilityToken DeviceBase::GetNextPipelineCompatibilityToken() {
    return PipelineCompatibilityToken(mNextPipelineCompatibilityToken++);
}

const CacheKey& DeviceBase::GetCacheKey() const {
    return mDeviceCacheKey;
}

const std::string& DeviceBase::GetLabel() const {
    return mLabel;
}

void DeviceBase::APISetLabel(const char* label) {
    mLabel = label;
    SetLabelImpl();
}

void DeviceBase::SetLabelImpl() {}

bool DeviceBase::ShouldDuplicateNumWorkgroupsForDispatchIndirect(
    ComputePipelineBase* computePipeline) const {
    return false;
}

bool DeviceBase::MayRequireDuplicationOfIndirectParameters() const {
    return false;
}

bool DeviceBase::ShouldDuplicateParametersForDrawIndirect(
    const RenderPipelineBase* renderPipelineBase) const {
    return false;
}

bool DeviceBase::ShouldApplyIndexBufferOffsetToFirstIndex() const {
    return false;
}

bool DeviceBase::CanTextureLoadResolveTargetInTheSameRenderpass() const {
    return false;
}

uint64_t DeviceBase::GetBufferCopyOffsetAlignmentForDepthStencil() const {
    // For depth-stencil texture, buffer offset must be a multiple of 4, which is required
    // by WebGPU and Vulkan SPEC.
    return 4u;
}

MaybeError DeviceBase::CopyFromStagingToBuffer(BufferBase* source,
                                               uint64_t sourceOffset,
                                               BufferBase* destination,
                                               uint64_t destinationOffset,
                                               uint64_t size) {
    DAWN_TRY(
        CopyFromStagingToBufferImpl(source, sourceOffset, destination, destinationOffset, size));
    if (GetDynamicUploader()->ShouldFlush()) {
        mQueue->ForceEventualFlushOfCommands();
    }
    return {};
}

MaybeError DeviceBase::CopyFromStagingToTexture(BufferBase* source,
                                                const TextureDataLayout& src,
                                                const TextureCopy& dst,
                                                const Extent3D& copySizePixels) {
    if (dst.aspect == Aspect::Depth &&
        IsToggleEnabled(Toggle::UseBlitForBufferToDepthTextureCopy)) {
        DAWN_TRY_CONTEXT(BlitStagingBufferToDepth(this, source, src, dst, copySizePixels),
                         "copying from staging buffer to depth aspect of %s using blit workaround.",
                         dst.texture.Get());
    } else if (dst.aspect == Aspect::Stencil &&
               IsToggleEnabled(Toggle::UseBlitForBufferToStencilTextureCopy)) {
        DAWN_TRY_CONTEXT(
            BlitStagingBufferToStencil(this, source, src, dst, copySizePixels),
            "copying from staging buffer to stencil aspect of %s using blit workaround.",
            dst.texture.Get());
    } else {
        DAWN_TRY(CopyFromStagingToTextureImpl(source, src, dst, copySizePixels));
    }

    if (GetDynamicUploader()->ShouldFlush()) {
        mQueue->ForceEventualFlushOfCommands();
    }
    return {};
}

Mutex::AutoLockAndHoldRef DeviceBase::GetScopedLockSafeForDelete() {
    return Mutex::AutoLockAndHoldRef(mMutex);
}

Mutex::AutoLock DeviceBase::GetScopedLock() {
    return Mutex::AutoLock(mMutex.Get());
}

bool DeviceBase::IsLockedByCurrentThreadIfNeeded() const {
    return mMutex == nullptr || mMutex->IsLockedByCurrentThread();
}

void DeviceBase::DumpMemoryStatistics(dawn::native::MemoryDump* dump) const {
    std::string prefix = absl::StrFormat("device_%p", static_cast<const void*>(this));
    GetObjectTrackingList(ObjectType::Texture)->ForEach([&](const ApiObjectBase* texture) {
        static_cast<const TextureBase*>(texture)->DumpMemoryStatistics(dump, prefix.c_str());
    });
    GetObjectTrackingList(ObjectType::Buffer)->ForEach([&](const ApiObjectBase* buffer) {
        static_cast<const BufferBase*>(buffer)->DumpMemoryStatistics(dump, prefix.c_str());
    });
}

ResultOrError<Ref<BufferBase>> DeviceBase::GetOrCreateTemporaryUniformBuffer(size_t size) {
    if (!mTemporaryUniformBuffer || mTemporaryUniformBuffer->GetSize() != size) {
        BufferDescriptor desc;
        desc.label = "Internal_TemporaryUniform";
        desc.usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::Uniform;
        desc.size = size;
        DAWN_TRY_ASSIGN(mTemporaryUniformBuffer, CreateBuffer(&desc));
    }

    return mTemporaryUniformBuffer;
}

IgnoreLazyClearCountScope::IgnoreLazyClearCountScope(DeviceBase* device)
    : mDevice(device), mLazyClearCountForTesting(device->mLazyClearCountForTesting) {}

IgnoreLazyClearCountScope::~IgnoreLazyClearCountScope() {
    mDevice->mLazyClearCountForTesting = mLazyClearCountForTesting;
}

}  // namespace dawn::native
