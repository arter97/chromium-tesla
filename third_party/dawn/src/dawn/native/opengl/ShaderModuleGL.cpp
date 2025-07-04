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

#include "dawn/native/opengl/ShaderModuleGL.h"

#include <sstream>
#include <unordered_map>
#include <utility>

#include "dawn/native/BindGroupLayoutInternal.h"
#include "dawn/native/CacheRequest.h"
#include "dawn/native/Pipeline.h"
#include "dawn/native/TintUtils.h"
#include "dawn/native/opengl/BindGroupLayoutGL.h"
#include "dawn/native/opengl/BindingPoint.h"
#include "dawn/native/opengl/DeviceGL.h"
#include "dawn/native/opengl/PipelineLayoutGL.h"
#include "dawn/platform/DawnPlatform.h"
#include "dawn/platform/tracing/TraceEvent.h"

#include "tint/tint.h"

namespace dawn::native {
namespace {

GLenum GLShaderType(SingleShaderStage stage) {
    switch (stage) {
        case SingleShaderStage::Vertex:
            return GL_VERTEX_SHADER;
        case SingleShaderStage::Fragment:
            return GL_FRAGMENT_SHADER;
        case SingleShaderStage::Compute:
            return GL_COMPUTE_SHADER;
    }
    DAWN_UNREACHABLE();
}

tint::glsl::writer::Version::Standard ToTintGLStandard(opengl::OpenGLVersion::Standard standard) {
    switch (standard) {
        case opengl::OpenGLVersion::Standard::Desktop:
            return tint::glsl::writer::Version::Standard::kDesktop;
        case opengl::OpenGLVersion::Standard::ES:
            return tint::glsl::writer::Version::Standard::kES;
    }
    DAWN_UNREACHABLE();
}

using BindingMap = std::unordered_map<tint::BindingPoint, tint::BindingPoint>;

opengl::CombinedSampler* AppendCombinedSampler(opengl::CombinedSamplerInfo* info,
                                               tint::BindingPoint texture,
                                               tint::BindingPoint sampler,
                                               tint::BindingPoint placeholderBindingPoint) {
    info->emplace_back();
    opengl::CombinedSampler* combinedSampler = &info->back();
    combinedSampler->usePlaceholderSampler = sampler == placeholderBindingPoint;
    combinedSampler->samplerLocation.group = BindGroupIndex(sampler.group);
    combinedSampler->samplerLocation.binding = BindingNumber(sampler.binding);
    combinedSampler->textureLocation.group = BindGroupIndex(texture.group);
    combinedSampler->textureLocation.binding = BindingNumber(texture.binding);
    return combinedSampler;
}

using InterstageLocationAndName = std::pair<uint32_t, std::string>;

#define GLSL_COMPILATION_REQUEST_MEMBERS(X)                                                      \
    X(const tint::Program*, inputProgram)                                                        \
    X(std::string, entryPointName)                                                               \
    X(SingleShaderStage, stage)                                                                  \
    X(std::optional<tint::ast::transform::SubstituteOverride::Config>, substituteOverrideConfig) \
    X(LimitsForCompilationRequest, limits)                                                       \
    X(bool, disableSymbolRenaming)                                                               \
    X(std::vector<InterstageLocationAndName>, interstageVariables)                               \
    X(std::vector<std::string>, bufferBindingVariables)                                          \
    X(tint::glsl::writer::Options, tintOptions)                                                  \
    X(CacheKey::UnsafeUnkeyedValue<dawn::platform::Platform*>, platform)

DAWN_MAKE_CACHE_REQUEST(GLSLCompilationRequest, GLSL_COMPILATION_REQUEST_MEMBERS);
#undef GLSL_COMPILATION_REQUEST_MEMBERS

#define GLSL_COMPILATION_MEMBERS(X) X(std::string, glsl)

DAWN_SERIALIZABLE(struct, GLSLCompilation, GLSL_COMPILATION_MEMBERS){};
#undef GLSL_COMPILATION_MEMBERS

}  // namespace
}  // namespace dawn::native

namespace dawn::native::opengl {

std::string GetBindingName(BindGroupIndex group, BindingNumber bindingNumber) {
    std::ostringstream o;
    o << "dawn_binding_" << static_cast<uint32_t>(group) << "_"
      << static_cast<uint32_t>(bindingNumber);
    return o.str();
}

bool operator<(const BindingLocation& a, const BindingLocation& b) {
    return std::tie(a.group, a.binding) < std::tie(b.group, b.binding);
}

bool operator<(const CombinedSampler& a, const CombinedSampler& b) {
    return std::tie(a.usePlaceholderSampler, a.samplerLocation, a.textureLocation) <
           std::tie(b.usePlaceholderSampler, a.samplerLocation, b.textureLocation);
}

std::string CombinedSampler::GetName() const {
    std::ostringstream o;
    o << "dawn_combined";
    if (usePlaceholderSampler) {
        o << "_placeholder_sampler";
    } else {
        o << "_" << static_cast<uint32_t>(samplerLocation.group) << "_"
          << static_cast<uint32_t>(samplerLocation.binding);
    }
    o << "_with_" << static_cast<uint32_t>(textureLocation.group) << "_"
      << static_cast<uint32_t>(textureLocation.binding);
    return o.str();
}

// static
ResultOrError<Ref<ShaderModule>> ShaderModule::Create(
    Device* device,
    const UnpackedPtr<ShaderModuleDescriptor>& descriptor,
    ShaderModuleParseResult* parseResult,
    OwnedCompilationMessages* compilationMessages) {
    Ref<ShaderModule> module = AcquireRef(new ShaderModule(device, descriptor));
    DAWN_TRY(module->Initialize(parseResult, compilationMessages));
    return module;
}

ShaderModule::ShaderModule(Device* device, const UnpackedPtr<ShaderModuleDescriptor>& descriptor)
    : ShaderModuleBase(device, descriptor) {}

MaybeError ShaderModule::Initialize(ShaderModuleParseResult* parseResult,
                                    OwnedCompilationMessages* compilationMessages) {
    ScopedTintICEHandler scopedICEHandler(GetDevice());

    DAWN_TRY(InitializeBase(parseResult, compilationMessages));

    return {};
}

ResultOrError<GLuint> ShaderModule::CompileShader(
    const OpenGLFunctions& gl,
    const ProgrammableStage& programmableStage,
    SingleShaderStage stage,
    bool usesVertexIndex,
    bool usesInstanceIndex,
    bool usesFragDepth,
    CombinedSamplerInfo* combinedSamplers,
    const PipelineLayout* layout,
    bool* needsPlaceholderSampler,
    bool* needsTextureBuiltinUniformBuffer,
    BindingPointToFunctionAndOffset* bindingPointToData) const {
    TRACE_EVENT0(GetDevice()->GetPlatform(), General, "TranslateToGLSL");

    const OpenGLVersion& version = ToBackend(GetDevice())->GetGL().GetVersion();

    GLSLCompilationRequest req = {};

    auto tintProgram = GetTintProgram();
    req.inputProgram = &(tintProgram->program);

    tint::inspector::Inspector inspector(*req.inputProgram);
    tint::glsl::writer::Bindings bindings;

    // Since (non-Vulkan) GLSL does not support descriptor sets, generate a
    // mapping from the original group/binding pair to a binding-only
    // value. This mapping will be used by Tint to remap all global
    // variables to the 1D space.
    const EntryPointMetadata& entryPointMetaData = GetEntryPoint(programmableStage.entryPoint);
    const BindingInfoArray& moduleBindingInfo = entryPointMetaData.bindings;

    // Because of the way the rest of the backend uses the binding information, we need to pass
    // through the original WGSL values in the combined shader map. That means, we need to store
    // that data for the external texture, otherwise it ends up getting lost.
    BindingMap externalTextureExpansionMap;

    for (BindGroupIndex group : IterateBitSet(layout->GetBindGroupLayoutsMask())) {
        const BindGroupLayout* bgl = ToBackend(layout->GetBindGroupLayout(group));

        for (const auto& [binding, shaderBindingInfo] : moduleBindingInfo[group]) {
            tint::BindingPoint srcBindingPoint{static_cast<uint32_t>(group),
                                               static_cast<uint32_t>(binding)};

            // For buffer bindings that can be sharable across stages, we need to rename them to
            // avoid GL program link failures due to block naming issues.
            if (std::holds_alternative<BufferBindingInfo>(shaderBindingInfo.bindingInfo) &&
                stage != SingleShaderStage::Compute) {
                req.bufferBindingVariables.emplace_back(shaderBindingInfo.name);
            }

            BindingIndex bindingIndex = bgl->GetBindingIndex(binding);
            auto& bindingIndexInfo = layout->GetBindingIndexInfo()[group];
            uint32_t shaderIndex = bindingIndexInfo[bindingIndex];
            tint::BindingPoint dstBindingPoint{0, shaderIndex};

            auto* const bufferBindingInfo =
                std::get_if<BufferBindingInfo>(&shaderBindingInfo.bindingInfo);

            if (bufferBindingInfo) {
                switch (bufferBindingInfo->type) {
                    case wgpu::BufferBindingType::Uniform:
                        bindings.uniform.emplace(
                            srcBindingPoint,
                            tint::glsl::writer::binding::Uniform{dstBindingPoint.binding});
                        break;
                    case kInternalStorageBufferBinding:
                    case wgpu::BufferBindingType::Storage:
                    case wgpu::BufferBindingType::ReadOnlyStorage:
                        bindings.storage.emplace(
                            srcBindingPoint,
                            tint::glsl::writer::binding::Storage{dstBindingPoint.binding});
                        break;
                    case wgpu::BufferBindingType::Undefined:
                        DAWN_UNREACHABLE();
                        break;
                }
            } else if (std::holds_alternative<SamplerBindingInfo>(shaderBindingInfo.bindingInfo)) {
                bindings.sampler.emplace(
                    srcBindingPoint, tint::glsl::writer::binding::Sampler{dstBindingPoint.binding});
            } else if (std::holds_alternative<TextureBindingInfo>(shaderBindingInfo.bindingInfo)) {
                bindings.texture.emplace(
                    srcBindingPoint, tint::glsl::writer::binding::Texture{dstBindingPoint.binding});
            } else if (std::holds_alternative<StorageTextureBindingInfo>(
                           shaderBindingInfo.bindingInfo)) {
                bindings.storage_texture.emplace(
                    srcBindingPoint,
                    tint::glsl::writer::binding::StorageTexture{dstBindingPoint.binding});
            } else if (std::holds_alternative<ExternalTextureBindingInfo>(
                           shaderBindingInfo.bindingInfo)) {
                const auto& etBindingMap = bgl->GetExternalTextureBindingExpansionMap();
                const auto& expansion = etBindingMap.find(binding);
                DAWN_ASSERT(expansion != etBindingMap.end());

                const auto& bindingExpansion = expansion->second;
                tint::glsl::writer::binding::BindingInfo plane0{
                    bindingIndexInfo[bgl->GetBindingIndex(bindingExpansion.plane0)]};
                tint::glsl::writer::binding::BindingInfo plane1{
                    bindingIndexInfo[bgl->GetBindingIndex(bindingExpansion.plane1)]};
                tint::glsl::writer::binding::BindingInfo metadata{
                    bindingIndexInfo[bgl->GetBindingIndex(bindingExpansion.params)]};

                tint::BindingPoint plane1WGSLBindingPoint{
                    static_cast<uint32_t>(group), static_cast<uint32_t>(bindingExpansion.plane1)};
                externalTextureExpansionMap[srcBindingPoint] = plane1WGSLBindingPoint;

                bindings.external_texture.emplace(
                    srcBindingPoint,
                    tint::glsl::writer::binding::ExternalTexture{metadata, plane0, plane1});
            }
        }
    }

    // When textures are accessed without a sampler (e.g., textureLoad()),
    // GetSamplerTextureUses() will return this sentinel value.
    bindings.placeholder_sampler_bind_point = {static_cast<uint32_t>(kMaxBindGroupsTyped), 0};

    *needsPlaceholderSampler = false;
    // Find all the sampler/texture pairs for this entry point, and create
    // CombinedSamplers for them. CombinedSampler records the binding points
    // of the original texture and sampler, and generates a unique name. The
    // corresponding uniforms will be retrieved by these generated names
    // in PipelineGL. Any texture-only references will have
    // "usePlaceholderSampler" set to true, and only the texture binding point
    // will be used in naming them. In addition, Dawn will bind a
    // non-filtering sampler for them (see PipelineGL).
    auto uses = inspector.GetSamplerTextureUses(programmableStage.entryPoint,
                                                bindings.placeholder_sampler_bind_point);
    CombinedSamplerInfo combinedSamplerInfo;
    for (const auto& use : uses) {
        tint::BindingPoint samplerBindPoint = use.sampler_binding_point;
        tint::BindingPoint texBindPoint = use.texture_binding_point;

        CombinedSampler* info = AppendCombinedSampler(
            &combinedSamplerInfo, use.texture_binding_point, use.sampler_binding_point,
            bindings.placeholder_sampler_bind_point);

        if (info->usePlaceholderSampler) {
            *needsPlaceholderSampler = true;
        }

        // Note, the rest of Dawn is expecting to use the un-modified WGSL binding points when
        // looking up information on the combined samplers. Tint is expecting Dawn to provide
        // the final expected values for those entry points. So, we end up using the origina
        // values for the AppendCombinedSampler calls and the remapped binding points when we
        // put things in the tint bindings structure.

        {
            auto texIt = bindings.texture.find(texBindPoint);
            if (texIt != bindings.texture.end()) {
                texBindPoint.group = 0;
                texBindPoint.binding = texIt->second.binding;
            } else {
                // The plane0 texture will be in external_textures, not textures, so we have to set
                // the `sampler_texture_name` based on the external_texture value.
                auto exIt = bindings.external_texture.find(texBindPoint);
                if (exIt != bindings.external_texture.end()) {
                    texBindPoint.group = 0;
                    texBindPoint.binding = exIt->second.plane0.binding;
                }
            }
        }
        {
            auto it = bindings.sampler.find(samplerBindPoint);
            if (it != bindings.sampler.end()) {
                samplerBindPoint.group = 0;
                samplerBindPoint.binding = it->second.binding;
            }
        }

        bindings.sampler_texture_to_name.emplace(
            tint::glsl::writer::binding::CombinedTextureSamplerPair{texBindPoint, samplerBindPoint,
                                                                    false},
            info->GetName());

        // If the texture has an associated plane1 texture (ie., it's an external texture),
        // append a new combined sampler with the same sampler and the plane1 texture.
        auto it = externalTextureExpansionMap.find(use.texture_binding_point);
        if (it != externalTextureExpansionMap.end()) {
            CombinedSampler* plane1Info =
                AppendCombinedSampler(&combinedSamplerInfo, it->second, use.sampler_binding_point,
                                      bindings.placeholder_sampler_bind_point);

            tint::BindingPoint plane1TexBindPoint = it->second;
            auto dstIt = bindings.external_texture.find(use.texture_binding_point);
            if (dstIt != bindings.external_texture.end()) {
                plane1TexBindPoint.group = 0;
                plane1TexBindPoint.binding = dstIt->second.plane1.binding;
            }

            bindings.sampler_texture_to_name.emplace(
                tint::glsl::writer::binding::CombinedTextureSamplerPair{plane1TexBindPoint,
                                                                        samplerBindPoint, true},
                plane1Info->GetName());
        }
    }

    // Some texture builtin functions are unsupported on GLSL ES. These are emulated with internal
    // uniforms.
    bindings.texture_builtins_from_uniform.ubo_binding = {kMaxBindGroups + 1, 0};

    // Remap the internal ubo binding as well.
    bindings.uniform.emplace(
        bindings.texture_builtins_from_uniform.ubo_binding,
        tint::glsl::writer::binding::Uniform{layout->GetInternalUniformBinding()});

    auto textureBuiltinsFromUniformData = inspector.GetTextureQueries(programmableStage.entryPoint);
    bool needsInternalUBO = false;
    if (!textureBuiltinsFromUniformData.empty()) {
        needsInternalUBO = true;
        for (size_t i = 0; i < textureBuiltinsFromUniformData.size(); ++i) {
            const auto& info = textureBuiltinsFromUniformData[i];

            // This is the unmodified binding point from the WGSL shader.
            tint::BindingPoint srcBindingPoint{info.group, info.binding};
            bindings.texture_builtins_from_uniform.ubo_bindingpoint_ordering.emplace_back(
                srcBindingPoint);

            // The remapped binding point is inserted into the Dawn data structure.
            const BindGroupLayoutInternalBase* bgl =
                layout->GetBindGroupLayout(BindGroupIndex{info.group});
            tint::BindingPoint dstBindingPoint = tint::BindingPoint{
                info.group,
                static_cast<uint32_t>(bgl->GetBindingIndex(BindingNumber{info.binding}))};

            BindPointFunction type = BindPointFunction::kTextureNumLevels;
            switch (info.type) {
                case tint::inspector::Inspector::TextureQueryType::kTextureNumLevels:
                    type = BindPointFunction::kTextureNumLevels;
                    break;
                case tint::inspector::Inspector::TextureQueryType::kTextureNumSamples:
                    type = BindPointFunction::kTextureNumSamples;
                    break;
            }

            // Note, the `sizeof(uint32_t)` has to match up with the data type created by the
            // `TextureBuiltinsFromUniform` when it creates the UBO structure.
            bindingPointToData->emplace(
                dstBindingPoint, std::pair{type, static_cast<uint32_t>(i * sizeof(uint32_t))});
        }
    }

    std::optional<tint::ast::transform::SubstituteOverride::Config> substituteOverrideConfig;
    if (!programmableStage.metadata->overrides.empty()) {
        substituteOverrideConfig = BuildSubstituteOverridesTransformConfig(programmableStage);
    }

    const CombinedLimits& limits = GetDevice()->GetLimits();

    req.stage = stage;
    req.entryPointName = programmableStage.entryPoint;
    req.substituteOverrideConfig = std::move(substituteOverrideConfig);
    req.limits = LimitsForCompilationRequest::Create(limits.v1);
    req.platform = UnsafeUnkeyedValue(GetDevice()->GetPlatform());

    req.tintOptions.version = tint::glsl::writer::Version(ToTintGLStandard(version.GetStandard()),
                                                          version.GetMajor(), version.GetMinor());

    req.tintOptions.disable_robustness = false;

    if (usesVertexIndex) {
        req.tintOptions.first_vertex_offset = 4 * PipelineLayout::PushConstantLocation::FirstVertex;
    }

    if (usesInstanceIndex) {
        req.tintOptions.first_instance_offset =
            4 * PipelineLayout::PushConstantLocation::FirstInstance;
    }

    if (usesFragDepth) {
        req.tintOptions.depth_range_offsets = {4 * PipelineLayout::PushConstantLocation::MinDepth,
                                               4 * PipelineLayout::PushConstantLocation::MaxDepth};
    }

    req.disableSymbolRenaming = GetDevice()->IsToggleEnabled(Toggle::DisableSymbolRenaming);

    req.interstageVariables = {};
    for (size_t i = 0; i < entryPointMetaData.interStageVariables.size(); i++) {
        if (entryPointMetaData.usedInterStageVariables[i]) {
            req.interstageVariables.emplace_back(static_cast<uint32_t>(i),
                                                 entryPointMetaData.interStageVariables[i].name);
        }
    }

    req.tintOptions.bindings = std::move(bindings);
    req.tintOptions.disable_polyfill_integer_div_mod =
        GetDevice()->IsToggleEnabled(Toggle::DisablePolyfillsOnIntegerDivisonAndModulo);

    CacheResult<GLSLCompilation> compilationResult;
    DAWN_TRY_LOAD_OR_RUN(
        compilationResult, GetDevice(), std::move(req), GLSLCompilation::FromBlob,
        [](GLSLCompilationRequest r) -> ResultOrError<GLSLCompilation> {
            tint::ast::transform::Manager transformManager;
            tint::ast::transform::DataMap transformInputs;

            transformManager.Add<tint::ast::transform::SingleEntryPoint>();
            transformInputs.Add<tint::ast::transform::SingleEntryPoint::Config>(r.entryPointName);

            {
                tint::ast::transform::Renamer::Remappings assignedRenamings = {};

                // Give explicit renaming mappings for interstage variables
                // Because GLSL requires interstage IO names to match.
                for (const auto& it : r.interstageVariables) {
                    assignedRenamings.emplace(
                        it.second, "dawn_interstage_location_" + std::to_string(it.first));
                }

                // Prepend v_ or f_ to buffer binding variable names in order to avoid collisions in
                // renamed interface blocks. The AddBlockAttribute transform in the Tint GLSL
                // printer will always generate wrapper structs from such bindings.
                for (const auto& variableName : r.bufferBindingVariables) {
                    assignedRenamings.emplace(
                        variableName,
                        (r.stage == SingleShaderStage::Vertex ? "v_" : "f_") + variableName);
                }

                // Needs to run early so that they can use builtin names safely.
                // TODO(dawn:2180): move this transform into Tint.
                transformManager.Add<tint::ast::transform::Renamer>();
                transformInputs.Add<tint::ast::transform::Renamer::Config>(
                    r.disableSymbolRenaming ? tint::ast::transform::Renamer::Target::kGlslKeywords
                                            : tint::ast::transform::Renamer::Target::kAll,
                    false, std::move(assignedRenamings));
            }

            if (r.substituteOverrideConfig) {
                // This needs to run after SingleEntryPoint transform which removes unused overrides
                // for current entry point.
                transformManager.Add<tint::ast::transform::SubstituteOverride>();
                transformInputs.Add<tint::ast::transform::SubstituteOverride::Config>(
                    std::move(r.substituteOverrideConfig).value());
            }

            tint::Program program;
            tint::ast::transform::DataMap transformOutputs;
            DAWN_TRY_ASSIGN(program, RunTransforms(&transformManager, r.inputProgram,
                                                   transformInputs, &transformOutputs, nullptr));

            // TODO(dawn:2180): refactor out.
            // Get the entry point name after the renamer pass.
            // In the case of the entry-point name being a reserved GLSL keyword
            // (including `main`) the entry-point would have been renamed
            // regardless of the `disableSymbolRenaming` flag. Always check the
            // rename map, and if the name was changed, get the new one.
            auto* data = transformOutputs.Get<tint::ast::transform::Renamer::Data>();
            DAWN_ASSERT(data != nullptr);
            auto it = data->remappings.find(r.entryPointName.data());
            std::string remappedEntryPoint;
            if (it != data->remappings.end()) {
                remappedEntryPoint = it->second;
            } else {
                remappedEntryPoint = r.entryPointName;
            }
            DAWN_ASSERT(remappedEntryPoint != "");

            if (r.stage == SingleShaderStage::Compute) {
                // Validate workgroup size after program runs transforms.
                Extent3D _;
                DAWN_TRY_ASSIGN(_, ValidateComputeStageWorkgroupSize(
                                       program, remappedEntryPoint.c_str(), r.limits,
                                       /* fullSubgroups */ {}));
            }

            auto result = tint::glsl::writer::Generate(program, r.tintOptions, remappedEntryPoint);
            DAWN_INVALID_IF(result != tint::Success, "An error occurred while generating GLSL:\n%s",
                            result.Failure().reason.Str());

            return GLSLCompilation{{std::move(result->glsl)}};
        },
        "OpenGL.CompileShaderToGLSL");

    if (GetDevice()->IsToggleEnabled(Toggle::DumpShaders)) {
        std::ostringstream dumpedMsg;
        dumpedMsg << "/* Dumped generated GLSL */\n" << compilationResult->glsl;

        GetDevice()->EmitLog(WGPULoggingType_Info, dumpedMsg.str().c_str());
    }

    GLuint shader = gl.CreateShader(GLShaderType(stage));
    const char* source = compilationResult->glsl.c_str();
    gl.ShaderSource(shader, 1, &source, nullptr);
    gl.CompileShader(shader);

    GLint compileStatus = GL_FALSE;
    gl.GetShaderiv(shader, GL_COMPILE_STATUS, &compileStatus);
    if (compileStatus == GL_FALSE) {
        GLint infoLogLength = 0;
        gl.GetShaderiv(shader, GL_INFO_LOG_LENGTH, &infoLogLength);

        if (infoLogLength > 1) {
            std::vector<char> buffer(infoLogLength);
            gl.GetShaderInfoLog(shader, infoLogLength, nullptr, &buffer[0]);
            gl.DeleteShader(shader);
            return DAWN_VALIDATION_ERROR("%s\nProgram compilation failed:\n%s", source,
                                         buffer.data());
        }
    }

    GetDevice()->GetBlobCache()->EnsureStored(compilationResult);
    *needsTextureBuiltinUniformBuffer = needsInternalUBO;

    *combinedSamplers = std::move(combinedSamplerInfo);
    return shader;
}

}  // namespace dawn::native::opengl
