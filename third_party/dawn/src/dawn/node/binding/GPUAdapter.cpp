// Copyright 2021 The Dawn & Tint Authors
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

#include "src/dawn/node/binding/GPUAdapter.h"

#include <limits>
#include <memory>
#include <unordered_set>
#include <utility>
#include <vector>

#include "src/dawn/node/binding/Converter.h"
#include "src/dawn/node/binding/Errors.h"
#include "src/dawn/node/binding/Flags.h"
#include "src/dawn/node/binding/GPUAdapterInfo.h"
#include "src/dawn/node/binding/GPUDevice.h"
#include "src/dawn/node/binding/GPUSupportedFeatures.h"
#include "src/dawn/node/binding/GPUSupportedLimits.h"
#include "src/dawn/node/binding/TogglesLoader.h"

#define FOR_EACH_LIMIT(X)                        \
    X(maxTextureDimension1D)                     \
    X(maxTextureDimension2D)                     \
    X(maxTextureDimension3D)                     \
    X(maxTextureArrayLayers)                     \
    X(maxBindGroups)                             \
    X(maxBindGroupsPlusVertexBuffers)            \
    X(maxBindingsPerBindGroup)                   \
    X(maxDynamicUniformBuffersPerPipelineLayout) \
    X(maxDynamicStorageBuffersPerPipelineLayout) \
    X(maxSampledTexturesPerShaderStage)          \
    X(maxSamplersPerShaderStage)                 \
    X(maxStorageBuffersPerShaderStage)           \
    X(maxStorageTexturesPerShaderStage)          \
    X(maxUniformBuffersPerShaderStage)           \
    X(maxUniformBufferBindingSize)               \
    X(maxStorageBufferBindingSize)               \
    X(minUniformBufferOffsetAlignment)           \
    X(minStorageBufferOffsetAlignment)           \
    X(maxVertexBuffers)                          \
    X(maxBufferSize)                             \
    X(maxVertexAttributes)                       \
    X(maxVertexBufferArrayStride)                \
    X(maxInterStageShaderComponents)             \
    X(maxInterStageShaderVariables)              \
    X(maxColorAttachments)                       \
    X(maxColorAttachmentBytesPerSample)          \
    X(maxComputeWorkgroupStorageSize)            \
    X(maxComputeInvocationsPerWorkgroup)         \
    X(maxComputeWorkgroupSizeX)                  \
    X(maxComputeWorkgroupSizeY)                  \
    X(maxComputeWorkgroupSizeZ)                  \
    X(maxComputeWorkgroupsPerDimension)

namespace wgpu::binding {

////////////////////////////////////////////////////////////////////////////////
// wgpu::bindings::GPUAdapter
// TODO(crbug.com/dawn/1133): This is a stub implementation. Properly implement.
////////////////////////////////////////////////////////////////////////////////
GPUAdapter::GPUAdapter(dawn::native::Adapter a,
                       const Flags& flags,
                       std::shared_ptr<AsyncRunner> async)
    : adapter_(a), flags_(flags), async_(async) {}

// TODO(dawn:1133): Avoid the extra copy by making the generator make a virtual method with const
// std::string&
interop::Interface<interop::GPUSupportedFeatures> GPUAdapter::getFeatures(Napi::Env env) {
    wgpu::Adapter adapter(adapter_.Get());
    size_t count = adapter.EnumerateFeatures(nullptr);
    std::vector<wgpu::FeatureName> features(count);
    adapter.EnumerateFeatures(&features[0]);
    return interop::GPUSupportedFeatures::Create<GPUSupportedFeatures>(env, env,
                                                                       std::move(features));
}

interop::Interface<interop::GPUSupportedLimits> GPUAdapter::getLimits(Napi::Env env) {
    WGPUSupportedLimits limits{};
    if (!adapter_.GetLimits(&limits)) {
        Napi::Error::New(env, "failed to get adapter limits").ThrowAsJavaScriptException();
    }

    wgpu::SupportedLimits wgpuLimits{};

#define COPY_LIMIT(LIMIT) wgpuLimits.limits.LIMIT = limits.limits.LIMIT;
    FOR_EACH_LIMIT(COPY_LIMIT)
#undef COPY_LIMIT

    return interop::GPUSupportedLimits::Create<GPUSupportedLimits>(env, wgpuLimits);
}

interop::Interface<interop::GPUAdapterInfo> GPUAdapter::getInfo(Napi::Env env) {
    WGPUAdapterInfo info = {};
    adapter_.GetInfo(&info);

    return interop::GPUAdapterInfo::Create<GPUAdapterInfo>(env, info);
}

bool GPUAdapter::getIsFallbackAdapter(Napi::Env) {
    WGPUAdapterProperties adapterProperties = {};
    adapter_.GetProperties(&adapterProperties);
    return adapterProperties.adapterType == WGPUAdapterType_CPU;
}

bool GPUAdapter::getIsCompatibilityMode(Napi::Env) {
    WGPUAdapterProperties adapterProperties = {};
    adapter_.GetProperties(&adapterProperties);
    return adapterProperties.compatibilityMode;
}

interop::Promise<interop::Interface<interop::GPUDevice>> GPUAdapter::requestDevice(
    Napi::Env env,
    interop::GPUDeviceDescriptor descriptor) {
    wgpu::DeviceDescriptor desc{};  // TODO(crbug.com/dawn/1133): Fill in.

    Converter conv(env);
    std::vector<wgpu::FeatureName> requiredFeatures;
    for (auto required : descriptor.requiredFeatures) {
        wgpu::FeatureName feature = wgpu::FeatureName::Undefined;

        // requiredFeatures is a "sequence<GPUFeatureName>" so a Javascript exception should be
        // thrown if one of the strings isn't one of the known features.
        if (!conv(feature, required)) {
            return {env, interop::kUnusedPromise};
        }

        requiredFeatures.emplace_back(feature);
    }
    if (!conv(desc.label, descriptor.label)) {
        return {env, interop::kUnusedPromise};
    }

    interop::Promise<interop::Interface<interop::GPUDevice>> promise(env, PROMISE_INFO);

    wgpu::RequiredLimits limits;
#define COPY_LIMIT(LIMIT)                                                                    \
    if (descriptor.requiredLimits.count(#LIMIT)) {                                           \
        using DawnLimitType = decltype(WGPULimits::LIMIT);                                   \
        DawnLimitType* dawnLimit = &limits.limits.LIMIT;                                     \
        uint64_t jsLimit = descriptor.requiredLimits[#LIMIT];                                \
        if (jsLimit > std::numeric_limits<DawnLimitType>::max() - 1) {                       \
            promise.Reject(                                                                  \
                binding::Errors::OperationError(env, "Limit \"" #LIMIT "\" out of range.")); \
            return promise;                                                                  \
        }                                                                                    \
        *dawnLimit = jsLimit;                                                                \
        descriptor.requiredLimits.erase(#LIMIT);                                             \
    }
    FOR_EACH_LIMIT(COPY_LIMIT)
#undef COPY_LIMIT

    for (auto [key, _] : descriptor.requiredLimits) {
        promise.Reject(binding::Errors::OperationError(env, "Unknown limit \"" + key + "\""));
        return promise;
    }

    desc.requiredFeatureCount = requiredFeatures.size();
    desc.requiredFeatures = requiredFeatures.data();
    desc.requiredLimits = &limits;

    // Propagate enabled/disabled dawn features
    TogglesLoader togglesLoader(flags_);
    DawnTogglesDescriptor deviceTogglesDesc = togglesLoader.GetDescriptor();
    desc.nextInChain = &deviceTogglesDesc;

    auto wgpu_device = adapter_.CreateDevice(&desc);
    if (wgpu_device == nullptr) {
        promise.Reject(binding::Errors::OperationError(env, "failed to create device"));
        return promise;
    }

    auto gpu_device = std::make_unique<GPUDevice>(env, desc, wgpu_device, async_);
    if (!valid_) {
        gpu_device->ForceLoss(interop::GPUDeviceLostReason::kUnknown,
                              "Device was marked as lost due to a stale adapter.");
    }
    valid_ = false;

    promise.Resolve(interop::GPUDevice::Bind(env, std::move(gpu_device)));
    return promise;
}

}  // namespace wgpu::binding
