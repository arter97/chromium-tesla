#!/usr/bin/python3 -i
#
# Copyright (c) 2015-2024 The Khronos Group Inc.
# Copyright (c) 2015-2024 Valve Corporation
# Copyright (c) 2015-2024 LunarG, Inc.
# Copyright (c) 2015-2024 Google Inc.
# Copyright (c) 2023-2024 RasterGrid Kft.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import os
import re
from generators.generator_utils import buildListVUID, PlatformGuardHelper
from generators.vulkan_object import Member, Struct
from generators.base_generator import BaseGenerator

# This class is a container for any source code, data, or other behavior that is necessary to
# customize the generator script for a specific target API variant (e.g. Vulkan SC). As such,
# all of these API-specific interfaces and their use in the generator script are part of the
# contract between this repository and its downstream users. Changing or removing any of these
# interfaces or their use in the generator script will have downstream effects and thus
# should be avoided unless absolutely necessary.
class APISpecific:
    # Generates custom validation for a function parameter or returns None
    @staticmethod
    def genCustomValidation(targetApiName: str, funcName: str, member) -> list[str]:
        match targetApiName:

            # Vulkan specific custom validation (currently none)
            case 'vulkan':
                return None

def isDeviceStruct(struct: Struct):
    for extension in struct.extensions:
        if not extension.device:
            return False
    return True

class StatelessValidationHelperOutputGenerator(BaseGenerator):
    def __init__(self,
                 valid_usage_file):
        BaseGenerator.__init__(self)
        self.valid_vuids = buildListVUID(valid_usage_file)

        # These functions have additional, custom-written checks in the utils cpp file. CodeGen will automatically add a call
        # to those functions of the form 'bool manual_PreCallValidateAPIName', where the 'vk' is dropped.
        # see 'manual_PreCallValidateCreateGraphicsPipelines' as an example.
        self.functions_with_manual_checks = [
            'vkCreateInstance',
            'vkCreateDevice',
            'vkCreateQueryPool',
            'vkCreateRenderPass',
            'vkCreateRenderPass2',
            'vkCreateBuffer',
            'vkCreateImage',
            'vkCreatePipelineLayout',
            'vkCreateGraphicsPipelines',
            'vkCreateComputePipelines',
            'vkCreateRayTracingPipelinesNV',
            'vkCreateRayTracingPipelinesKHR',
            'vkCreateSampler',
            'vkCreateDescriptorSetLayout',
            'vkGetDescriptorSetLayoutSupport',
            'vkCreateBufferView',
            'vkCreateSemaphore',
            'vkCreateEvent',
            'vkFreeDescriptorSets',
            'vkUpdateDescriptorSets',
            'vkBeginCommandBuffer',
            'vkFreeCommandBuffers',
            'vkCmdSetViewport',
            'vkCmdSetScissor',
            'vkCmdSetLineWidth',
            'vkCmdClearAttachments',
            'vkCmdBindIndexBuffer',
            'vkCmdBindIndexBuffer2KHR',
            'vkCmdCopyBuffer',
            'vkCmdUpdateBuffer',
            'vkCmdFillBuffer',
            'vkCreateSwapchainKHR',
            'vkCreateSharedSwapchainsKHR',
            'vkQueuePresentKHR',
            'vkCreateDescriptorPool',
            'vkCmdPushDescriptorSetKHR',
            'vkCmdPushDescriptorSet2KHR',
            'vkGetDescriptorEXT',
            'vkCmdSetExclusiveScissorNV',
            'vkCmdSetViewportShadingRatePaletteNV',
            'vkCmdSetCoarseSampleOrderNV',
            'vkAllocateMemory',
            'vkCreateAccelerationStructureNV',
            'vkCreateAccelerationStructureKHR',
            'vkDestroyAccelerationStructureKHR',
            'vkGetAccelerationStructureHandleNV',
            'vkGetPhysicalDeviceImageFormatProperties',
            'vkGetPhysicalDeviceImageFormatProperties2',
            'vkCmdBuildAccelerationStructureNV',
            'vkCreateFramebuffer',
            'vkCmdSetLineStippleKHR',
            'vkSetDebugUtilsObjectNameEXT',
            'vkSetDebugUtilsObjectTagEXT',
            'vkCmdSetViewportWScalingNV',
            'vkAcquireNextImageKHR',
            'vkAcquireNextImage2KHR',
            'vkCmdBindTransformFeedbackBuffersEXT',
            'vkCmdBeginTransformFeedbackEXT',
            'vkCmdEndTransformFeedbackEXT',
            'vkCreateSamplerYcbcrConversion',
            'vkGetMemoryFdKHR',
            'vkImportSemaphoreFdKHR',
            'vkGetSemaphoreFdKHR',
            'vkImportFenceFdKHR',
            'vkGetFenceFdKHR',
            'vkGetMemoryWin32HandleKHR',
            'vkImportFenceWin32HandleKHR',
            'vkGetFenceWin32HandleKHR',
            'vkImportSemaphoreWin32HandleKHR',
            'vkGetSemaphoreWin32HandleKHR',
            'vkGetMemoryHostPointerPropertiesEXT',
            'vkCmdBindVertexBuffers',
            'vkCreateImageView',
            'vkCopyAccelerationStructureToMemoryKHR',
            'vkCmdCopyAccelerationStructureToMemoryKHR',
            'vkCopyAccelerationStructureKHR',
            'vkCmdCopyAccelerationStructureKHR',
            'vkCopyMemoryToAccelerationStructureKHR',
            'vkCmdCopyMemoryToAccelerationStructureKHR',
            'vkCmdWriteAccelerationStructuresPropertiesKHR',
            'vkWriteAccelerationStructuresPropertiesKHR',
            'vkGetRayTracingCaptureReplayShaderGroupHandlesKHR',
            'vkCmdBuildAccelerationStructureIndirectKHR',
            'vkGetDeviceAccelerationStructureCompatibilityKHR',
            'vkCmdSetViewportWithCount',
            'vkCmdSetScissorWithCount',
            'vkCmdBindVertexBuffers2',
            'vkCmdCopyBuffer2',
            'vkCmdBuildAccelerationStructuresKHR',
            'vkCmdBuildAccelerationStructuresIndirectKHR',
            'vkBuildAccelerationStructuresKHR',
            'vkGetAccelerationStructureBuildSizesKHR',
            'vkCmdWriteAccelerationStructuresPropertiesNV',
            'vkCreateDisplayModeKHR',
            'vkCmdSetVertexInputEXT',
            'vkCmdPushConstants',
            'vkCmdPushConstants2KHR',
            'vkMergePipelineCaches',
            'vkCmdClearColorImage',
            'vkCmdBeginRenderPass',
            'vkCmdBeginRenderPass2',
            'vkCmdBeginRendering',
            'vkCmdSetDiscardRectangleEXT',
            'vkGetQueryPoolResults',
            'vkCmdBeginConditionalRenderingEXT',
            'vkGetDeviceImageMemoryRequirements',
            'vkGetDeviceImageSparseMemoryRequirements',
            'vkCreateAndroidSurfaceKHR',
            'vkCreateWin32SurfaceKHR',
            'vkCreateWaylandSurfaceKHR',
            'vkCreateXcbSurfaceKHR',
            'vkCreateXlibSurfaceKHR',
            'vkGetPhysicalDeviceSurfaceFormatsKHR',
            'vkGetPhysicalDeviceSurfacePresentModesKHR',
            'vkGetPhysicalDeviceSurfaceCapabilities2KHR',
            'vkGetPhysicalDeviceSurfaceFormats2KHR',
            'vkGetPhysicalDeviceSurfacePresentModes2EXT',
            'vkExportMetalObjectsEXT',
            'vkCmdSetDiscardRectangleEnableEXT',
            'vkCmdSetDiscardRectangleModeEXT',
            'vkCmdSetExclusiveScissorEnableNV',
            'vkGetMemoryWin32HandlePropertiesKHR',
            'vkGetMemoryFdPropertiesKHR',
            'vkCreateShadersEXT',
            'vkGetShaderBinaryDataEXT',
            'vkSetDeviceMemoryPriorityEXT',
            'vkGetDeviceImageSubresourceLayoutKHR',
            'vkQueueBindSparse',
            'vkCmdBindDescriptorBuffersEXT',
            'vkGetPhysicalDeviceExternalBufferProperties',
            'vkGetPipelinePropertiesEXT',
            ]

        # Commands to ignore
        self.blacklist = [
            'vkGetInstanceProcAddr',
            'vkGetDeviceProcAddr',
            'vkEnumerateInstanceVersion',
            'vkEnumerateInstanceLayerProperties',
            'vkEnumerateInstanceExtensionProperties',
            'vkEnumerateDeviceLayerProperties',
            'vkEnumerateDeviceExtensionProperties',
            'vkGetDeviceGroupSurfacePresentModes2EXT'
            ]

        # Validation conditions for some special case struct members that are conditionally validated
        self.structMemberValidationConditions = [
            {
                'struct' : 'VkSubpassDependency2',
                'field' :  'VkPipelineStageFlagBits',
                'condition' : '!vku::FindStructInPNextChain<VkMemoryBarrier2>(pCreateInfo->pDependencies[dependencyIndex].pNext)'
            },
            {
                'struct' : 'VkSubpassDependency2',
                'field' :  'VkAccessFlagBits',
                'condition' : '!vku::FindStructInPNextChain<VkMemoryBarrier2>(pCreateInfo->pDependencies[dependencyIndex].pNext)'
            }
        ]

        # Will create a validate function for the struct to be called by non-generated code.
        # There are cases where `noautovalidity` and `optional` are both true, but really are just
        # guarded by other conditions.
        #
        # Example: pViewportState should always be validated, when not ignored. The logic of when it
        # isn't ignored gets complex and best done by hand in sl_pipeline.cpp
        self.generateStructHelper = [
            'VkPipelineViewportStateCreateInfo',
            'VkPipelineTessellationStateCreateInfo',
            'VkPipelineVertexInputStateCreateInfo',
            'VkPipelineMultisampleStateCreateInfo',
            'VkPipelineColorBlendStateCreateInfo',
            'VkPipelineDepthStencilStateCreateInfo',
            'VkPipelineInputAssemblyStateCreateInfo',
            'VkPipelineRasterizationStateCreateInfo',
            'VkDescriptorAddressInfoEXT',
        ]

        # Map of structs type names to generated validation code for that struct type
        self.validatedStructs = dict()
        # Map of flags typenames
        self.flags = set()
        # Map of flag bits typename to list of values
        self.flagBits = dict()

        self.stype_version_dict = dict()

    def generate(self):
        self.write(f'''// *** THIS FILE IS GENERATED - DO NOT EDIT ***
            // See {os.path.basename(__file__)} for modifications

            /***************************************************************************
            *
            * Copyright (c) 2015-2024 The Khronos Group Inc.
            * Copyright (c) 2015-2024 Valve Corporation
            * Copyright (c) 2015-2024 LunarG, Inc.
            *
            * Licensed under the Apache License, Version 2.0 (the "License");
            * you may not use this file except in compliance with the License.
            * You may obtain a copy of the License at
            *
            *     http://www.apache.org/licenses/LICENSE-2.0
            *
            * Unless required by applicable law or agreed to in writing, software
            * distributed under the License is distributed on an "AS IS" BASIS,
            * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
            * See the License for the specific language governing permissions and
            * limitations under the License.
            ****************************************************************************/\n''')
        self.write('// NOLINTBEGIN') # Wrap for clang-tidy to ignore

        if self.filename == 'stateless_validation_helper.h':
            self.generateHeader()
        elif self.filename == 'stateless_validation_helper.cpp':
            self.generateSource()
        else:
            self.write(f'\nFile name {self.filename} has no code to generate\n')

        self.write('// NOLINTEND') # Wrap for clang-tidy to ignore

    def generateHeader(self):
        out = []
        out.append('#pragma once\n')

        out.append('\nstatic inline bool IsDuplicatePnext(VkStructureType input_value) {\n')
        out.append('    switch (input_value) {\n')
        for struct in [x for x in self.vk.structs.values() if x.allowDuplicate and x.sType is not None]:
            # The sType will always be first member of struct
            out.append(f'        case {struct.sType}:\n')
        out.append('            return true;\n')
        out.append('        default:\n')
        out.append('            return false;\n')
        out.append('    }\n')
        out.append('}\n')
        out.append('\n')

        guard_helper = PlatformGuardHelper()
        for command in [x for x in self.vk.commands.values() if x.name not in self.blacklist]:
            out.extend(guard_helper.add_guard(command.protect))
            prototype = command.cPrototype.split('VKAPI_CALL ')[1]
            prototype = f'bool PreCallValidate{prototype[2:]}'
            prototype = prototype.replace(');', ') const override;\n')
            if 'ValidationCache' in command.name:
                prototype = prototype.replace('const override', 'const')
            prototype = prototype.replace(')', ',\n    const ErrorObject&                          error_obj)')
            out.append(prototype)
        out.extend(guard_helper.add_guard(None))

        for struct_name in self.generateStructHelper:
            out.append(f'bool Validate{struct_name[2:]}(const {struct_name} &info, const Location &loc) const;')

        self.write("".join(out))

    def generateSource(self):
        # Structure fields to ignore
        structMemberBlacklist = {
            'VkWriteDescriptorSet' : ['dstSet'],
            'VkAccelerationStructureGeometryKHR' :['geometry'],
            'VkDescriptorDataEXT' :['pSampler']
        }
        for struct in [x for x in self.vk.structs.values() if x.name in structMemberBlacklist]:
            for member in [x for x in struct.members if x.name in structMemberBlacklist[struct.name]]:
                member.noAutoValidity = True

        # TODO - We should not need this with VulkanObject, but the following are casuing issues
        # being "promoted"
        #  VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TEXEL_BUFFER_ALIGNMENT_FEATURES_EXT
        #  VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_YCBCR_2_PLANE_444_FORMATS_FEATURES_EXT
        #  VK_STRUCTURE_TYPE_QUEUE_FAMILY_CHECKPOINT_PROPERTIES_2_NV
        #  VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT
        #  VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_2_FEATURES_EXT
        #  VK_STRUCTURE_TYPE_CHECKPOINT_DATA_2_NV
        #  VK_STRUCTURE_TYPE_MULTIVIEW_PER_VIEW_ATTRIBUTES_INFO_NVX
        #  VK_STRUCTURE_TYPE_RENDERING_FRAGMENT_SHADING_RATE_ATTACHMENT_INFO_KHR
        #  VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_4444_FORMATS_FEATURES_EXT
        #  VK_STRUCTURE_TYPE_RENDERING_FRAGMENT_DENSITY_MAP_ATTACHMENT_INFO_EXT
        root = self.registry.reg
        for extensions in root.findall('extensions'):
            for extension in extensions.findall('extension'):
                extensionName = extension.get('name')
                promotedTo = extension.get('promotedto')
                # TODO Issue 5103 - this is being used to remove false positive currently
                promotedToCore = promotedTo is not None and 'VK_VERSION' in promotedTo

                for entry in extension.iterfind('require/enum[@extends="VkStructureType"]'):
                    if (entry.get('comment') is None or 'typo' not in entry.get('comment')):
                        alias = entry.get('alias')
                        if (alias is not None and promotedToCore):
                            if (alias not in self.stype_version_dict.keys()):
                                self.stype_version_dict[alias] = set()
                            self.stype_version_dict[alias].add(extensionName)

        # Generate the struct member checking code from the captured data
        for struct in self.vk.structs.values():
            # The string returned will be nested in an if check for a NULL pointer, so needs its indent incremented
            lines = self.genFuncBody(self.vk.structs[struct.name].members, '{funcName}', '{errorLoc}', '{valuePrefix}', '{displayNamePrefix}', struct.name, False)
            if lines:
                self.validatedStructs[struct.name] = lines

        out = []
        out.append('''
            #include "chassis.h"

            #include "stateless/stateless_validation.h"
            #include "generated/enum_flag_bits.h"
            #include "generated/layer_chassis_dispatch.h"
            ''')

        # The reason we split this up into Feature and Properties struct is before be had a 450 case, 10k line function that broke MSVC
        # reference: https://www.asawicki.info/news_1617_how_code_refactoring_can_fix_stack_overflow_error
        extended_structs = [x for x in self.vk.structs.values() if x.extends]
        feature_structs = [x for x in extended_structs if x.extends == ["VkPhysicalDeviceFeatures2", "VkDeviceCreateInfo"]]
        property_structs = [x for x in extended_structs if x.extends == ["VkPhysicalDeviceProperties2"]]
        other_structs = [x for x in extended_structs if x not in feature_structs and x not in property_structs]

        out.append('''
            bool StatelessValidation::ValidatePnextFeatureStructContents(const Location& loc,
                                                                const VkBaseOutStructure* header, const char *pnext_vuid,
                                                                VkPhysicalDevice caller_physical_device, bool is_const_param) const {
                bool skip = false;
                const bool is_physdev_api = caller_physical_device != VK_NULL_HANDLE;
                switch(header->sType) {
            ''')
        guard_helper = PlatformGuardHelper()
        for struct in feature_structs:
            out.extend(guard_helper.add_guard(struct.protect))
            out.extend(self.genStructBody(struct, False))
        out.extend(guard_helper.add_guard(None))
        out.append('''
                    default:
                        skip = false;
                }
                return skip;
            }

            ''')

        out.append('''
            bool StatelessValidation::ValidatePnextPropertyStructContents(const Location& loc,
                                                                const VkBaseOutStructure* header, const char *pnext_vuid,
                                                                VkPhysicalDevice caller_physical_device, bool is_const_param) const {
                bool skip = false;
                const bool is_physdev_api = caller_physical_device != VK_NULL_HANDLE;
                switch(header->sType) {
            ''')
        guard_helper = PlatformGuardHelper()
        for struct in property_structs:
            out.extend(guard_helper.add_guard(struct.protect))
            out.extend(self.genStructBody(struct, False))
        out.extend(guard_helper.add_guard(None))
        out.append('''
                    default:
                        skip = false;
                }
                return skip;
            }

            ''')

        out.append('''
            // All structs that are not a Feature or Property struct
            bool StatelessValidation::ValidatePnextStructContents(const Location& loc,
                                                                const VkBaseOutStructure* header, const char *pnext_vuid,
                                                                VkPhysicalDevice caller_physical_device, bool is_const_param) const {
                bool skip = false;
                const bool is_physdev_api = caller_physical_device != VK_NULL_HANDLE;
                switch(header->sType) {
            ''')
        guard_helper = PlatformGuardHelper()
        for struct in other_structs:
            out.extend(guard_helper.add_guard(struct.protect))
            out.extend(self.genStructBody(struct, True))
        out.extend(guard_helper.add_guard(None))
        out.append('''
                    default:
                        skip = false;
                }
                return skip;
            }

            ''')

        # Some extensions are alias from EXT->KHR but are not promoted, example
        #   vkGetImageSubresourceLayout2EXT (VK_EXT_host_image_copy)
        #   vkGetImageSubresourceLayout2KHR (VK_KHR_maintenance5)
        alias_but_not_core = []
        for command in [x for x in self.vk.commands.values() if x.name not in self.blacklist and x.alias and x.alias in self.vk.commands]:
            aliasCommand = self.vk.commands[command.alias]
            if aliasCommand.version is None:
                alias_but_not_core.append(aliasCommand.name)

        # Generate the command parameter checking code from the captured data
        for command in [x for x in self.vk.commands.values() if x.name not in self.blacklist]:
            out.extend(guard_helper.add_guard(command.protect, extra_newline=True))

            prototype = (command.cPrototype.split('VKAPI_CALL ')[1])[2:-1]
            prototype = prototype.replace(')', ', const ErrorObject& error_obj)')
            out.append(f'bool StatelessValidation::PreCallValidate{prototype} const {{\n')
            out.append('    bool skip = false;\n')

            # Create a copy here to make the logic simpler passing into ValidatePnextStructContents
            out.append('    [[maybe_unused]] const Location loc = error_obj.location;\n')

            # Cannot validate extension dependencies for device extension APIs having a physical device as their dispatchable object
            if command.extensions and (not any(x.device for x in command.extensions) or command.params[0].type != 'VkPhysicalDevice'):
                cExpression =  []
                outExpression =  []
                for extension in command.extensions:
                    outExpression.append(f'vvl::Extension::_{extension.name}')
                    if extension.instance:
                        cExpression.append(f'IsExtEnabled(instance_extensions.{extension.name.lower()})')
                    else:
                        cExpression.append(f'IsExtEnabled(device_extensions.{extension.name.lower()})')

                cExpression = " || ".join(cExpression)
                if len(outExpression) > 1:
                    cExpression = f'({cExpression})'

                if command.name in alias_but_not_core:
                    cExpression += f' && loc.function == vvl::Func::{command.name}'
                out.append(f'if (!{cExpression}) skip |= OutputExtensionError(loc, {{{", ".join(outExpression)}}});\n')

            if command.alias and command.alias in self.vk.commands:
                # For alias that are promoted, just point to new function, ErrorObject will allow us to distinguish the caller
                # Note that we can only do this if the promoted version is part of the target API
                paramList = [param.name for param in command.params]
                paramList.append('error_obj')
                params = ', '.join(paramList)
                out.append(f'skip |= PreCallValidate{command.alias[2:]}({params});')
            else:
                # Skip first parameter if it is a dispatch handle (everything except vkCreateInstance)
                startIndex = 0 if command.name == 'vkCreateInstance' else 1
                lines = self.genFuncBody(command.params[startIndex:], command.name, 'loc', '', '', None, isPhysDevice = command.params[0].type == 'VkPhysicalDevice')

                if command.instance and command.version:
                    # check function name so KHR version doesn't trigger flase positive
                    out.append(f'if (loc.function == vvl::Func::{command.name} && CheckPromotedApiAgainstVulkanVersion({command.params[0].name}, loc, {command.version.nameApi})) return true;\n')

                for line in lines:
                    if isinstance(line, list):
                        for sub in line:
                            out.append(sub)
                    else:
                        out.append(line)
                # Insert call to custom-written function if present
                if command.name in self.functions_with_manual_checks:
                    manualCheckCmd = command.name
                # We also have to consider aliases here as the promoted version may not be part of the target API
                elif command.alias in self.functions_with_manual_checks:
                    manualCheckCmd = command.alias
                else:
                    manualCheckCmd = None
                if manualCheckCmd:
                    # Generate parameter list for manual fcn and down-chain calls
                    params_text = ', '.join([x.name for x in command.params]) + ', error_obj'
                    out.append(f'    if (!skip) skip |= manual_PreCallValidate{manualCheckCmd[2:]}({params_text});\n')
            out.append('return skip;\n')
            out.append('}\n')
        out.extend(guard_helper.add_guard(None, extra_newline=True))

        for struct_name in self.generateStructHelper:
            out.append(f'bool StatelessValidation::Validate{struct_name[2:]}(const {struct_name} &info, const Location &loc) const {{\n')
            out.append('    bool skip = false;\n')
            # Only generate validation code if the structure actually exists in the target API
            if struct_name in self.vk.structs:
                out.extend(self.expandStructCode(struct_name, struct_name, 'loc', 'info.', '', []))
            out.append('    return skip;\n')
            out.append('}\n')

        self.write("".join(out))

    def genType(self, typeinfo, name, alias):
        BaseGenerator.genType(self, typeinfo, name, alias)
        if (typeinfo.elem.get('category') == 'bitmask' and not alias):
            self.flags.add(name)

    def genGroup(self, groupinfo, groupName, alias):
        BaseGenerator.genGroup(self, groupinfo, groupName, alias)
        if 'FlagBits' in groupName and groupName != 'VkStructureType':
            bits = []
            for elem in groupinfo.elem.findall('enum'):
                if elem.get('supported') != 'disabled' and elem.get('alias') is None:
                    bits.append(elem.get('name'))
            if bits:
                self.flagBits[groupName] = bits

    def isHandleOptional(self, member: Member, lengthMember: Member) -> bool :
        # Simple, if it's optional, return true
        if member.optional or member.optionalPointer:
            return True
        # If no validity is being generated, it usually means that validity is complex and not absolute, so let's say yes.
        if member.noAutoValidity:
            return True
        # If the parameter is an array and we haven't already returned, find out if any of the len parameters are optional
        if lengthMember and lengthMember.optional:
            return True
        return

    # Get VUID identifier from implicit VUID tag
    def GetVuid(self, name, suffix):
        vuid_string = f'VUID-{name}-{suffix}'
        vuid = "kVUIDUndefined"
        if '->' in vuid_string:
           return vuid
        if vuid_string in self.valid_vuids:
            vuid = f'"{vuid_string}"'
        elif name in self.vk.commands:
            # Only commands have alias to worry about
            alias_string = f'VUID-{self.vk.commands[name].alias}-{suffix}'
            if alias_string in self.valid_vuids:
                vuid = f'"{alias_string}"'
        return vuid

    # Generate the pointer check string
    def makePointerCheck(self, valuePrefix, member: Member, lengthMember: Member, errorLoc, valueRequired, lenValueRequired, lenPtrRequired, funcName, structTypeName):
        checkExpr = []
        callerName = structTypeName if structTypeName else funcName
        if lengthMember:
            length_deref = '->' in member.length
            count_required_vuid = self.GetVuid(callerName, f"{member.length}-arraylength")
            array_required_vuid = self.GetVuid(callerName, f"{member.name}-parameter")
            count_ptr_required_vuid = self.GetVuid(callerName, f"{member.length}-parameter")
            # TODO: Remove workaround for missing optional tag in vk.xml
            if array_required_vuid == '"VUID-VkFramebufferCreateInfo-pAttachments-parameter"':
                return []
            # This is an array with a pointer to a count value
            if lengthMember.pointer and not length_deref:
                # If count and array parameters are optional, there will be no validation
                if valueRequired == 'true' or lenPtrRequired == 'true' or lenValueRequired == 'true':
                    # When the length parameter is a pointer, there is an extra Boolean parameter in the function call to indicate if it is required
                    checkExpr.append(f'skip |= ValidatePointerArray({errorLoc}.dot(Field::{member.length}), {errorLoc}.dot(Field::{member.name}), {valuePrefix}{member.length}, &{valuePrefix}{member.name}, {lenPtrRequired}, {lenValueRequired}, {valueRequired},{count_ptr_required_vuid}, {count_required_vuid}, {array_required_vuid});\n')
            # This is an array with an integer count value
            else:
                # If count and array parameters are optional, there will be no validation
                if valueRequired == 'true' or lenValueRequired == 'true':
                    if member.type != 'char':
                        # A valid VU can't use '->' in the middle so the generated VUID from the spec uses '::' instead
                        count_required_vuid = self.GetVuid(callerName, f"{member.length.replace('->', '::')}-arraylength")
                        if structTypeName == 'VkShaderModuleCreateInfo' and member.name == 'pCode':
                            count_required_vuid = '"VUID-VkShaderModuleCreateInfo-codeSize-01085"' # exception due to unique lenValue

                        # TODO - some length have unhandled symbols
                        count_loc = f'{errorLoc}.dot(Field::{member.length})'
                        if '->' in member.length:
                            count_loc = f'{errorLoc}.dot(Field::{member.length.split("->")[0]}).dot(Field::{member.length.split("->")[1]})'
                        elif ' + ' in member.length:
                            # hardcoded only instance for now
                            if 'samples' in member.length: # "(samples + 31) / 32"
                                count_loc = f'{errorLoc}.dot(Field::samples)'
                            elif 'rasterizationSamples' in member.length: # "(rasterizationSamples + 31) / 32"
                                count_loc = f'{errorLoc}.dot(Field::rasterizationSamples)'
                                member.length = 'rasterizationSamples'
                        elif ' / ' in member.length:
                            count_loc = f'{errorLoc}.dot(Field::{member.length.split(" / ")[0]})'
                        checkExpr.append(f'skip |= ValidateArray({count_loc}, {errorLoc}.dot(Field::{member.name}), {valuePrefix}{member.length}, &{valuePrefix}{member.name}, {lenValueRequired}, {valueRequired}, {count_required_vuid}, {array_required_vuid});\n')
                    else:
                        # Arrays of strings receive special processing
                        checkExpr.append(f'skip |= ValidateStringArray({errorLoc}.dot(Field::{member.length}), {errorLoc}.dot(Field::{member.name}), {valuePrefix}{member.length}, {valuePrefix}{member.name}, {lenValueRequired}, {valueRequired}, {count_required_vuid}, {array_required_vuid});\n')
            if checkExpr and lengthMember and length_deref and member.length.count('->'):
                # Add checks to ensure the validation call does not dereference a NULL pointer to obtain the count
                count = member.length.count('->')
                checkedExpr = []
                elements = member.length.split('->')
                # Open the if expression blocks
                for i in range(0, count):
                    checkedExpr.append(f'if ({"->".join(elements[0:i+1])} != nullptr) {{\n')
                # Add the validation expression
                for expr in checkExpr:
                    checkedExpr.append(expr)
                # Close the if blocks
                for i in range(0, count):
                    checkedExpr.append('}\n')
                checkExpr = [checkedExpr]
        # This is an individual struct that is not allowed to be NULL
        elif not (member.optional or member.fixedSizeArray):
            # Function pointers need a reinterpret_cast to void*
            ptr_required_vuid = self.GetVuid(callerName, f"{member.name}-parameter")
            if member.type.startswith('PFN_'):
                allocator_dict = {'pfnAllocation': '"VUID-VkAllocationCallbacks-pfnAllocation-00632"',
                                  'pfnReallocation': '"VUID-VkAllocationCallbacks-pfnReallocation-00633"',
                                  'pfnFree': '"VUID-VkAllocationCallbacks-pfnFree-00634"',
                                 }
                vuid = allocator_dict.get(member.name)
                if vuid is not None:
                    ptr_required_vuid = vuid
                checkExpr.append(f'skip |= ValidateRequiredPointer({errorLoc}.dot(Field::{member.name}), reinterpret_cast<const void*>({valuePrefix}{member.name}), {ptr_required_vuid});\n')
            else:
                checkExpr.append(f'skip |= ValidateRequiredPointer({errorLoc}.dot(Field::{member.name}), {valuePrefix}{member.name}, {ptr_required_vuid});\n')
        else:
            # Special case for optional internal allocation function pointers.
            if (member.type, member.name) == ('PFN_vkInternalAllocationNotification', 'pfnInternalAllocation'):
                checkExpr.extend(self.internalAllocationCheck(valuePrefix, member.name, errorLoc, 'pfnInternalFree'))
            elif (member.type, member.name) == ('PFN_vkInternalFreeNotification', 'pfnInternalFree'):
                checkExpr.extend(self.internalAllocationCheck(valuePrefix, member.name, errorLoc, 'pfnInternalAllocation'))
        return checkExpr

    # Generate internal allocation function pointer check.
    def internalAllocationCheck(self, valuePrefix, name, errorLoc, complementaryName):
        checkExpr = []
        vuid = '"VUID-VkAllocationCallbacks-pfnInternalAllocation-00635"'
        checkExpr.append(f'if ({valuePrefix}{name} != nullptr)')
        checkExpr.append('{')
        # Function pointers need a reinterpret_cast to void*
        checkExpr.append(f'skip |= ValidateRequiredPointer({errorLoc}.dot(Field::{name}), reinterpret_cast<const void*>({valuePrefix}{complementaryName}), {vuid});\n')
        checkExpr.append('}\n')
        return checkExpr

    # Process struct member validation code, performing name substitution if required
    def processStructMemberCode(self, line, funcName, errorLoc, memberNamePrefix, memberDisplayNamePrefix):
        # Build format specifier list
        kwargs = {}
        if '{funcName}' in line:
            kwargs['funcName'] = funcName
        if '{errorLoc}' in line:
            kwargs['errorLoc'] = errorLoc
        if '{valuePrefix}' in line:
            kwargs['valuePrefix'] = memberNamePrefix
        if '{displayNamePrefix}' in line:
            # Check for a tuple that includes a format string and format parameters to be used with the ParameterName class
            if type(memberDisplayNamePrefix) is tuple:
                kwargs['displayNamePrefix'] = memberDisplayNamePrefix[0]
            else:
                kwargs['displayNamePrefix'] = memberDisplayNamePrefix

        if kwargs:
            # Need to escape the C++ curly braces
            return line.format(**kwargs)
        return line

    # Process struct member validation code, stripping metadata
    def ScrubStructCode(self, code):
        scrubbed_lines = ''
        for line in code:
            if 'ValidateStructPnext(' in line:
                continue
            if 'allowed_structs' in line:
                continue
            if 'xml-driven validation' in line:
                continue
            line = line.replace('{funcName}', '')
            line = line.replace('{errorLoc}', '')
            line = line.replace('{valuePrefix}', '')
            line = line.replace('{displayNamePrefix}', '')
            scrubbed_lines += line
        return scrubbed_lines

    # Process struct validation code for inclusion in function or parent struct validation code
    def expandStructCode(self, item_type, funcName, errorLoc, memberNamePrefix, memberDisplayNamePrefix, output):
        lines = self.validatedStructs[item_type]
        for line in lines:
            if output:
                output[-1] += '\n'
            if isinstance(line, list):
                for sub in line:
                    output.append(self.processStructMemberCode(sub, funcName, errorLoc, memberNamePrefix, memberDisplayNamePrefix))
            else:
                output.append(self.processStructMemberCode(line, funcName, errorLoc, memberNamePrefix, memberDisplayNamePrefix))
        return output

    # Generate the parameter checking code
    def genFuncBody(self, members: list[Member], funcName, errorLoc, valuePrefix, displayNamePrefix, structTypeName, isPhysDevice):
        struct = self.vk.structs[structTypeName] if structTypeName in self.vk.structs else None
        callerName = structTypeName if structTypeName else funcName
        lines = []    # Generated lines of code
        duplicateCountVuid = [] # prevent duplicate VUs being generated

        # TODO Using a regex in this context is not ideal. Would be nicer if usedLines were a list of objects with "settings" (such as "isPhysDevice")
        validate_pnext_rx = re.compile(r'(.*ValidateStructPnext\(.*)(\).*\n*)', re.M)

        # Returnedonly structs should have most of their members ignored -- on entry, we only care about validating the sType and
        # pNext members. Everything else will be overwritten by the callee.
        for member in [x for x in members if not struct or not struct.returnedOnly or (x.name in ('sType', 'pNext'))]:
            usedLines = []
            lengthMember = None
            condition = None
            #
            # Generate the full name of the value, which will be printed in the error message, by adding the variable prefix to the value name
            valueDisplayName = f'{displayNamePrefix}{member.name}'
            #
            # Check for NULL pointers, ignore the in-out count parameters that
            # will be validated with their associated array
            if (member.pointer or member.fixedSizeArray) and not [x for x in members if x.length and member.name == x.length]:
                # Parameters for function argument generation
                valueRequired = 'true'    # Parameter cannot be NULL
                lenPtrRequired = 'true'  # Count pointer cannot be NULL
                lenValueRequired = 'true'  # Count value cannot be 0
                countRequiredVuid = None # If there is a count required VUID to check
                # Generate required/optional parameter strings for the pointer and count values
                if member.optional or member.optionalPointer:
                    valueRequired = 'false'
                if member.length:
                    # The parameter is an array with an explicit count parameter
                    # Find a named parameter in a parameter list
                    lengthMember = next((x for x in members if x.name == member.length), None)

                    # First check if any element of params matches length exactly
                    if not lengthMember:
                        # Otherwise, look for any elements of params that appear within length
                        candidates = [p for p in members if re.search(r'\b{}\b'.format(p.name), member.length)]
                        # 0 or 1 matches are expected, >1 would require a special case and/or explicit validation
                        if len(candidates) == 0:
                            lengthMember = None
                        elif len(candidates) == 1:
                            lengthMember = candidates[0]

                    if lengthMember:
                        if lengthMember.pointer:
                            lenPtrRequired = 'false' if lengthMember.optional else lenPtrRequired
                            lenValueRequired = 'false' if lengthMember.optionalPointer else lenValueRequired
                            # In case of count as field in another struct, look up field to see if count is optional.
                            len_deref = member.length.split('->')
                            if len(len_deref) == 2:
                                lenMembers = next((x.members for x in self.vk.structs.values() if x.name == lengthMember.type), None)
                                if lenMembers and next((x for x in lenMembers if x.name == len_deref[1] and x.optional), None):
                                    lenValueRequired = 'false'
                        else:
                            vuidName = self.GetVuid(callerName, f"{lengthMember.name}-arraylength")
                            # This VUID is considered special, as it is the only one whose names ends in "-arraylength" but has special conditions allowing bindingCount to be 0.
                            arrayVuidExceptions = ["\"VUID-vkCmdBindVertexBuffers2-bindingCount-arraylength\""]
                            if vuidName in arrayVuidExceptions:
                                continue
                            if lengthMember.optional:
                                lenValueRequired = 'false'
                            elif member.noAutoValidity:
                                # Handle edge case where XML expresses a non-optional non-pointer value length with noautovalidity
                                # ex: <param noautovalidity="true"len="commandBufferCount">
                                countRequiredVuid = self.GetVuid(callerName, f"{lengthMember.name}-arraylength")
                                if countRequiredVuid in duplicateCountVuid:
                                    countRequiredVuid = None
                                else:
                                    duplicateCountVuid.append(countRequiredVuid)
                    else:
                        # Do not generate length checks for constant sized arrays
                        lenPtrRequired = 'false'
                        lenValueRequired = 'false'

                #
                # The parameter will not be processed when tagged as 'noautovalidity'
                # For the pointer to struct case, the struct pointer will not be validated, but any
                # members not tagged as 'noautovalidity' will be validated
                # We special-case the custom allocator checks, as they are explicit but can be auto-generated.
                AllocatorFunctions = ['PFN_vkAllocationFunction', 'PFN_vkReallocationFunction', 'PFN_vkFreeFunction', 'PFN_vkInternalAllocationNotification', 'PFN_vkInternalFreeNotification']
                api_specific_custom_validation = APISpecific.genCustomValidation(self.targetApiName, funcName, member)
                if api_specific_custom_validation is not None:
                    usedLines.extend(api_specific_custom_validation)
                elif member.noAutoValidity and member.type not in AllocatorFunctions and not countRequiredVuid:
                    # Log a diagnostic message when validation cannot be automatically generated and must be implemented manually
                    self.logMsg('diag', f'ParameterValidation: No validation for {callerName} {member.name}')
                elif countRequiredVuid:
                    usedLines.append(f'skip |= ValidateArray({errorLoc}.dot(Field::{member.length}), loc, {valuePrefix}{member.length}, &{valuePrefix}{member.name}, true, false, {countRequiredVuid}, kVUIDUndefined);\n')
                else:
                    if member.type in self.vk.structs and self.vk.structs[member.type].sType:
                        # If this is a pointer to a struct with an sType field, verify the type
                        struct = self.vk.structs[member.type]
                        stypeVUID = self.GetVuid(member.type, "sType-sType")
                        paramVUID = self.GetVuid(callerName, f"{member.name}-parameter")
                        if lengthMember:
                            count_required_vuid = self.GetVuid(callerName, f"{member.length}-arraylength")
                            # This is an array of struct pointers
                            if member.cDeclaration.count('*') == 2:
                                usedLines.append(f'skip |= ValidateStructPointerTypeArray({errorLoc}.dot(Field::{lengthMember.name}), {errorLoc}.dot(Field::{member.name}), "{struct.sType}", {valuePrefix}{lengthMember.name}, {valuePrefix}{member.name}, {struct.sType}, {lenValueRequired}, {valueRequired}, {stypeVUID}, {paramVUID}, {count_required_vuid});\n')
                            # This is an array with a pointer to a count value
                            elif lengthMember.pointer:
                                # When the length parameter is a pointer, there is an extra Boolean parameter in the function call to indicate if it is required
                                count_ptr_required_vuid = self.GetVuid(callerName, f"{member.length}-parameter")
                                usedLines.append(f'skip |= ValidateStructTypeArray({errorLoc}.dot(Field::{member.length}), {errorLoc}.dot(Field::{member.name}), "{struct.sType}", {valuePrefix}{member.length}, {valuePrefix}{member.name}, {struct.sType}, {lenPtrRequired}, {lenValueRequired}, {valueRequired}, {stypeVUID}, {paramVUID}, {count_ptr_required_vuid}, {count_required_vuid});\n')
                            # This is an array with an integer count value
                            else:
                                usedLines.append(f'skip |= ValidateStructTypeArray({errorLoc}.dot(Field::{member.length}), {errorLoc}.dot(Field::{member.name}), "{struct.sType}", {valuePrefix}{member.length}, {valuePrefix}{member.name}, {struct.sType}, {lenValueRequired}, {valueRequired}, {stypeVUID}, {paramVUID}, {count_required_vuid});\n')
                        # This is an individual struct
                        else:
                            usedLines.append(f'skip |= ValidateStructType({errorLoc}.dot(Field::{member.name}), "{struct.sType}", {valuePrefix}{member.name}, {struct.sType}, {valueRequired}, {paramVUID}, {stypeVUID});\n')
                    # If this is an input handle array that is not allowed to contain NULL handles, verify that none of the handles are VK_NULL_HANDLE
                    elif member.type in self.vk.handles and member.const and not self.isHandleOptional(member, lengthMember):
                        if not lengthMember:
                            # This is assumed to be an output handle pointer
                            raise Exception('Unsupported parameter validation case: Output handles are not NULL checked')
                        elif lengthMember.pointer:
                            # This is assumed to be an output array with a pointer to a count value
                            raise Exception('Unsupported parameter validation case: Output handle array elements are not NULL checked')
                        count_required_vuid = self.GetVuid(callerName, f"{member.length}-arraylength")
                        # This is an array with an integer count value
                        usedLines.append(f'skip |= ValidateHandleArray({errorLoc}.dot(Field::{member.length}), {errorLoc}.dot(Field::{member.name}), {valuePrefix}{member.length}, {valuePrefix}{member.name}, {lenValueRequired}, {valueRequired}, {count_required_vuid});\n')
                    elif member.type in self.flags and member.const:
                        # Generate check string for an array of VkFlags values
                        flagBitsName = member.type.replace('Flags', 'FlagBits')
                        if flagBitsName not in self.vk.bitmasks:
                            raise Exception('Unsupported parameter validation case: array of reserved VkFlags')
                        allFlags = 'All' + flagBitsName
                        count_required_vuid = self.GetVuid(callerName, f"{member.length}-arraylength")
                        array_required_vuid = self.GetVuid(callerName, f"{member.name}-parameter")
                        usedLines.append(f'skip |= ValidateFlagsArray({errorLoc}.dot(Field::{member.length}), {errorLoc}.dot(Field::{member.name}), vvl::FlagBitmask::{flagBitsName}, {allFlags}, {valuePrefix}{member.length}, {valuePrefix}{member.name}, {lenValueRequired}, {count_required_vuid}, {array_required_vuid});\n')
                    elif member.type == 'VkBool32' and member.const:
                        count_required_vuid = self.GetVuid(callerName, f"{member.length}-arraylength")
                        array_required_vuid = self.GetVuid(callerName, f"{member.name}-parameter")
                        usedLines.append(f'skip |= ValidateBool32Array({errorLoc}.dot(Field::{member.length}), {errorLoc}.dot(Field::{member.name}), {valuePrefix}{member.length}, {valuePrefix}{member.name}, {lenValueRequired}, {valueRequired}, {count_required_vuid}, {array_required_vuid});\n')
                    elif member.type in self.vk.enums and member.const:
                        lenLoc = 'loc' if member.fixedSizeArray else f'{errorLoc}.dot(Field::{member.length})'
                        count_required_vuid = self.GetVuid(callerName, f"{member.length}-arraylength")
                        array_required_vuid = self.GetVuid(callerName, f"{member.name}-parameter")
                        usedLines.append(f'skip |= ValidateRangedEnumArray({lenLoc}, {errorLoc}.dot(Field::{member.name}), vvl::Enum::{member.type}, {valuePrefix}{member.length}, {valuePrefix}{member.name}, {lenValueRequired}, {valueRequired}, {count_required_vuid}, {array_required_vuid});\n')
                    elif member.name == 'pNext':
                        # Generate an array of acceptable VkStructureType values for pNext
                        extStructCount = 0
                        extStructData = 'nullptr'
                        pNextVuid = self.GetVuid(structTypeName, "pNext-pNext")
                        sTypeVuid = self.GetVuid(structTypeName, "sType-unique")
                        struct = self.vk.structs[structTypeName]
                        if struct.extendedBy:
                            extStructVar = f'allowed_structs_{structTypeName}'
                            extStructCount = f'{extStructVar}.size()'
                            extStructData = f'{extStructVar}.data()'
                            extendedBy = ", ".join([self.vk.structs[x].sType for x in struct.extendedBy])
                            usedLines.append(f'constexpr std::array {extStructVar} = {{ {extendedBy} }};\n')
                        usedLines.append(f'skip |= ValidateStructPnext({errorLoc}, {valuePrefix}{member.name}, {extStructCount}, {extStructData}, GeneratedVulkanHeaderVersion, {pNextVuid}, {sTypeVuid});\n')
                    else:
                        usedLines += self.makePointerCheck(valuePrefix, member, lengthMember, errorLoc, valueRequired, lenValueRequired, lenPtrRequired, funcName, structTypeName)
                    # If this is a pointer to a struct (input), see if it contains members that need to be checked
                    if member.type in self.validatedStructs and (member.const or self.vk.structs[member.type].returnedOnly):
                        # Process struct pointer/array validation code, performing name substitution if required
                        expr = []
                        expr.append(f'if ({valuePrefix}{member.name} != nullptr)\n')
                        expr.append('{\n')
                        newErrorLoc = f'{member.name}_loc'
                        if lengthMember:
                            # Need to process all elements in the array
                            length = member.length.split(',')[0]
                            indexName = length.replace('Count', 'Index')
                            # If the length value is a pointer, de-reference it for the count.
                            deref = '*' if lengthMember.pointer else ''
                            expr.append(f'for (uint32_t {indexName} = 0; {indexName} < {deref}{valuePrefix}{length}; ++{indexName})\n')
                            expr.append('{\n')
                            expr.append(f'[[maybe_unused]] const Location {newErrorLoc} = {errorLoc}.dot(Field::{member.name}, {indexName});')
                            # Prefix for value name to display in error message
                            connector = '->' if member.cDeclaration.count('*') == 2 else '.'
                            memberNamePrefix = f'{valuePrefix}{member.name}[{indexName}]{connector}'
                            memberDisplayNamePrefix = (f'{valueDisplayName}[%i]{connector}', indexName)
                        else:
                            expr.append(f'[[maybe_unused]] const Location {newErrorLoc} = {errorLoc}.dot(Field::{member.name});')
                            memberNamePrefix = f'{valuePrefix}{member.name}->'
                            memberDisplayNamePrefix = f'{valueDisplayName}->'
                        # Expand the struct validation lines
                        expr = self.expandStructCode(member.type, funcName, newErrorLoc, memberNamePrefix, memberDisplayNamePrefix, expr)
                        if lengthMember:
                            expr.append('}\n')
                        expr.append('}\n')
                        usedLines.append(expr)

                    is_const_str = 'true' if member.const else 'false'
                    isPhysDevice_str = 'physicalDevice' if isPhysDevice else 'VK_NULL_HANDLE'
                    for setter, _, elem in multi_string_iter(usedLines):
                        elem = re.sub(r', (true|false|physicalDevice|VK_NULL_HANDLE)', '', elem)
                        m = validate_pnext_rx.match(elem)
                        if m is not None:
                            setter(f'{m.group(1)}, {isPhysDevice_str}, {is_const_str}{m.group(2)}')

            # Non-pointer types
            else:
                # The parameter will not be processes when tagged as 'noautovalidity'
                # For the struct case, the struct type will not be validated, but any
                # members not tagged as 'noautovalidity' will be validated
                if member.noAutoValidity:
                    # Log a diagnostic message when validation cannot be automatically generated and must be implemented manually
                    self.logMsg('diag', f'ParameterValidation: No validation for {callerName} {member.name}')
                else:
                    if member.type in self.vk.structs and self.vk.structs[member.type].sType:
                        vuid = self.GetVuid(member.type, "sType-sType")
                        sType = self.vk.structs[member.type].sType
                        usedLines.append(f'skip |= ValidateStructType({errorLoc}.dot(Field::{member.name}), "{sType}", &({valuePrefix}{member.name}), {sType}, false, kVUIDUndefined, {vuid});\n')
                    elif member.name == 'sType' and structTypeName in self.generateStructHelper:
                        # special case when dealing with isolated struct helper functions
                        vuid = self.GetVuid(struct.name, "sType-sType")
                        usedLines.append(f'skip |= ValidateStructType(loc, "{struct.sType}", &info, {struct.sType}, false, kVUIDUndefined, {vuid});\n')
                    elif member.type in self.vk.handles:
                        if not member.optional:
                            usedLines.append(f'skip |= ValidateRequiredHandle({errorLoc}.dot(Field::{member.name}), {valuePrefix}{member.name});\n')
                    elif member.type in self.flags and member.type.replace('Flags', 'FlagBits') not in self.flagBits:
                        vuid = self.GetVuid(callerName, f"{member.name}-zerobitmask")
                        usedLines.append(f'skip |= ValidateReservedFlags({errorLoc}.dot(Field::{member.name}), {valuePrefix}{member.name}, {vuid});\n')
                    elif member.type in self.flags or member.type in self.flagBits:
                        if member.type in self.flags:
                            flagBitsName = member.type.replace('Flags', 'FlagBits')
                            flagsType = 'kOptionalFlags' if member.optional else 'kRequiredFlags'
                            invalidVuid = self.GetVuid(callerName, f"{member.name}-parameter")
                            zeroVuid = self.GetVuid(callerName, f"{member.name}-requiredbitmask")
                        elif member.type in self.flagBits:
                            flagBitsName = member.type
                            flagsType = 'kOptionalSingleBit' if member.optional else 'kRequiredSingleBit'
                            invalidVuid = self.GetVuid(callerName, f"{member.name}-parameter")
                            zeroVuid = invalidVuid
                        # Bad workaround, but this whole file will be refactored soon
                        if flagBitsName == 'VkBuildAccelerationStructureFlagBitsNV':
                            flagBitsName = 'VkBuildAccelerationStructureFlagBitsKHR'
                        allFlagsName = 'All' + flagBitsName
                        zeroVuidArg = '' if member.optional else ', ' + zeroVuid
                        condition = [item for item in self.structMemberValidationConditions if (item['struct'] == structTypeName and item['field'] == flagBitsName)]
                        usedLines.append(f'skip |= ValidateFlags({errorLoc}.dot(Field::{member.name}), vvl::FlagBitmask::{flagBitsName}, {allFlagsName}, {valuePrefix}{member.name}, {flagsType}, {invalidVuid}{zeroVuidArg});\n')
                    elif member.type == 'VkBool32':
                        usedLines.append(f'skip |= ValidateBool32({errorLoc}.dot(Field::{member.name}), {valuePrefix}{member.name});\n')
                    elif member.type in self.vk.enums and member.type != 'VkStructureType':
                        vuid = self.GetVuid(callerName, f"{member.name}-parameter")
                        usedLines.append(f'skip |= ValidateRangedEnum({errorLoc}.dot(Field::{member.name}), vvl::Enum::{member.type}, {valuePrefix}{member.name}, {vuid});\n')
                    # If this is a struct, see if it contains members that need to be checked
                    if member.type in self.validatedStructs:
                        memberNamePrefix = f'{valuePrefix}{member.name}.'
                        memberDisplayNamePrefix = f'{valueDisplayName}.'
                        usedLines.append(self.expandStructCode(member.type, funcName, errorLoc, memberNamePrefix, memberDisplayNamePrefix, []))
            # Append the parameter check to the function body for the current command
            if usedLines:
                # Apply special conditional checks
                if condition:
                    # Generate code to check for a specific condition before executing validation code
                    checkedExpr = []
                    checkedExpr.append(f'if ({condition[0]["condition"]}) {{\n')
                    for expr in usedLines:
                        checkedExpr.append(expr)
                    checkedExpr.append('}\n')
                    usedLines = [checkedExpr]

                lines += usedLines
        if not lines:
            lines.append('// No xml-driven validation\n')
        return lines

    # Joins strings in English fashion
    # TODO: move to some utility library
    def englishJoin(self, strings, conjunction: str):
        strings_list = list(strings)
        if len(strings_list) <= 1:
            return strings_list[0]
        else: # len > 1
            return f'{", ".join(strings_list[:-1])}, {conjunction} {strings_list[-1]}'


    # This logic was broken into its own function because we need to fill multiple functions with these structs
    def genStructBody(self, struct: Struct, non_prop_feature: bool):
        pnext_case = '\n'
        pnext_check = ''

        pnext_case += f'        // Validation code for {struct.name} structure members\n'
        pnext_case += f'        case {struct.sType}: {{ // Covers VUID-{struct.name}-sType-sType\n'

        if struct.sType and struct.version and all(not x.promotedTo for x in struct.extensions):
            pnext_check += f'''
                if (is_physdev_api) {{
                    VkPhysicalDeviceProperties device_properties = {{}};
                    DispatchGetPhysicalDeviceProperties(caller_physical_device, &device_properties);
                    if (device_properties.apiVersion < {struct.version.nameApi}) {{
                        APIVersion device_api_version(static_cast<uint32_t>(device_properties.apiVersion));
                        skip |= LogError(
                                pnext_vuid, instance, loc.dot(Field::pNext),
                                "includes a pointer to a VkStructureType ({struct.sType}) which was added in {struct.version.nameApi} but the "
                                "current effective API version is %s.", StringAPIVersion(device_api_version).c_str());
                    }}
                }}
                '''

        elif struct.sType in self.stype_version_dict.keys():
            ext_names = self.stype_version_dict[struct.sType]

            # Skip extensions that are not in the target API
            # This check is needed because parts of the base generator code bypass the
            # dependency resolution logic in the registry tooling and thus the generator
            # may attempt to generate code for extensions which are not supported in the
            # target API variant, thus this check needs to happen even if any specific
            # target API variant may not specifically need it
            ext_names.intersection(self.vk.extensions.keys())
            if len(ext_names) == 0:
                return ""

            ext_names = sorted(list(ext_names)) # make the order deterministic

            # Dependent on enabled extension
            extension_conditinals = list()
            for ext_name in ext_names:
                extension = self.vk.extensions[ext_name]
                if extension.device:
                    extension_conditinals.append( f'((is_physdev_api && !SupportedByPdev(physical_device, vvl::Extension::_{extension.name})) || (!is_physdev_api && !IsExtEnabled(device_extensions.{extension.name.lower()})))' )
                else:
                    extension_conditinals.append( f'!IsExtEnabled(instance_extensions.{extension.name.lower()})' )
            if len(extension_conditinals) == 1 and extension_conditinals[0][0] == '(' and extension_conditinals[0][-1] == ')':
                extension_conditinals[0] = extension_conditinals[0][1:-1]# strip extraneous parentheses

            extension_check = f'if ({" && ".join(extension_conditinals)}) {{'
            pnext_check += f'''
                    {extension_check}
                        skip |= LogError(
                            pnext_vuid, instance, loc.dot(Field::pNext),
                            "includes a pointer to a VkStructureType ({struct.sType}), but its parent extension "
                            "{self.englishJoin(ext_names, "or")} has not been enabled.");
                    }}
                '''

        expr = self.expandStructCode(struct.name, struct.name, 'pNext_loc', 'structure->', '', [])
        struct_validation_source = self.ScrubStructCode(expr)
        if struct_validation_source != '':
            # Only reasonable to validate content of structs if const as otherwise the date inside has not been writen to yet
            # https://github.com/KhronosGroup/Vulkan-ValidationLayers/issues/3122
            pnext_check += 'if (is_const_param) {\n'
            pnext_check += f'[[maybe_unused]] const Location pNext_loc = loc.pNext(Struct::{struct.name});\n'

            # Can have a struct from a device extension be extended by an instance extension struct
            # https://github.com/KhronosGroup/Vulkan-ValidationLayers/issues/7803
            # This is true already for all Properties/Features so exclude them here
            check_for_instance = False
            if non_prop_feature and isDeviceStruct(struct):
                for extend in struct.extends:
                    if not isDeviceStruct(self.vk.structs[extend]):
                        check_for_instance = True

            # We do the extension checking here at the pNext chaining because if the struct is only used in a new extended command,
            # using that command will always trigger a "missing extension" VU
            checkExpression = []
            resultExpression = []
            for extension in [x.name for x in struct.extensions if x.device]:
                if check_for_instance:
                    checkExpression.append(f'(!is_physdev_api && !IsExtEnabled(device_extensions.{extension.lower()}))')
                else:
                    checkExpression.append(f'!IsExtEnabled(device_extensions.{extension.lower()})')
                resultExpression.append(extension)
            # TODO - Video session creation checks will fail tests if no extensions are found (need to fix test logic)
            if len(checkExpression) > 0 and 'Video' not in struct.name:
                # Special message for device features
                if 'VkPhysicalDeviceFeatures2' in struct.extends and 'VkDeviceCreateInfo' in struct.extends:
                    pnext_check += f'''if ({" && ".join(checkExpression)}){{
                        skip |= LogError(pnext_vuid, instance, pNext_loc,
                            "includes a pointer to a {struct.name}, but when creating VkDevice, the parent extension "
                            "({" or ".join(resultExpression)}) was not included in ppEnabledExtensionNames.");
                        }}
                    '''
                else:
                    pnext_check += f'''if ({" && ".join(checkExpression)}){{
                            skip |= LogError(pnext_vuid, instance, pNext_loc, "extended struct requires the extensions {" or ".join(resultExpression)}");
                        }}
                        '''

            struct_validation_source = f'{struct.name} *structure = ({struct.name} *) header;\n{struct_validation_source}'
            struct_validation_source += '}\n'
        pnext_case += f'{pnext_check}{struct_validation_source}'
        pnext_case += '} break;\n'
        # Skip functions containing no validation
        if struct_validation_source or pnext_check != '':
            return pnext_case
        else:
            return f'\n        // No Validation code for {struct.name} structure members  -- Covers VUID-{struct.name}-sType-sType\n'

# Helper for iterating over a list where each element is possibly a single element or another 1-dimensional list
# Generates (setter, deleter, element) for each element where:
#  - element = the next element in the list
#  - setter(x) = a function that will set the entry in `lines` corresponding to `element` to `x`
#  - deleter() = a function that will delete the entry corresponding to `element` in `lines`
def multi_string_iter(lines):
    for i, ul in enumerate(lines):
        if not isinstance(ul, list):
            def setter(x): lines[i] = x
            def deleter(): del(lines[i])
            yield (setter, deleter, ul)
        else:
            for j, k in enumerate(lines[i]):
                def setter(x): lines[i][j] = x
                def deleter(): del(lines[i][j])
                yield (setter, deleter, k)
