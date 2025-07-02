# This file is used to manage Vulkan dependencies for several repos. It is
# used by gclient to determine what version of each dependency to check out, and
# where.

# Avoids the need for a custom root variable.
use_relative_paths = True
git_dependencies = 'SYNC'

vars = {
  'chromium_git': 'https://chromium.googlesource.com',

  # Current revision of glslang, the Khronos SPIRV compiler.
  'glslang_revision': '73eccd4b67985d344578cade8958214cee0a3f6e',

  # Current revision of Lunarg VulkanTools
  'lunarg_vulkantools_revision': '27ebab7411bf59f9e9e42a5f6946a03eb9e425b8',

  # Current revision of spirv-cross, the Khronos SPIRV cross compiler.
  'spirv_cross_revision': 'b8fcf307f1f347089e3c46eb4451d27f32ebc8d3',

  # Current revision fo the SPIRV-Headers Vulkan support library.
  'spirv_headers_revision': 'eb49bb7b1136298b77945c52b4bbbc433f7885de',

  # Current revision of SPIRV-Tools for Vulkan.
  'spirv_tools_revision': 'ce46482db7ab3ea9c52fce832d27ca40b14f8e87',

  # Current revision of Khronos Vulkan-Headers.
  'vulkan_headers_revision': 'd192041a2fc9c9fd8ae67d8ae3f32c5511541f04',

  # Current revision of Khronos Vulkan-Loader.
  'vulkan_loader_revision': '1bae807cd919890f7174fa14228b6e672e8d296e',

  # Current revision of Khronos Vulkan-Tools.
  'vulkan_tools_revision': 'a9a1bcd709e185700847268eb4310f6484b027bc',

  # Current revision of Khronos Vulkan-Utility-Libraries.
  'vulkan_utility_libraries_revision': '07759f04791dc3fbb390174f0d24d4a792e0d357',

  # Current revision of Khronos Vulkan-ValidationLayers.
  'vulkan_validation_revision': '28c0a5d84f1a5a03655407f5cf52b98399d369af',
}

deps = {
  'glslang/src': {
    'url': '{chromium_git}/external/github.com/KhronosGroup/glslang@{glslang_revision}',
  },

  'lunarg-vulkantools/src': {
    'url': '{chromium_git}/external/github.com/LunarG/VulkanTools@{lunarg_vulkantools_revision}',
  },

  'spirv-cross/src': {
    'url': '{chromium_git}/external/github.com/KhronosGroup/SPIRV-Cross@{spirv_cross_revision}',
  },

  'spirv-headers/src': {
    'url': '{chromium_git}/external/github.com/KhronosGroup/SPIRV-Headers@{spirv_headers_revision}',
  },

  'spirv-tools/src': {
    'url': '{chromium_git}/external/github.com/KhronosGroup/SPIRV-Tools@{spirv_tools_revision}',
  },

  'vulkan-headers/src': {
    'url': '{chromium_git}/external/github.com/KhronosGroup/Vulkan-Headers@{vulkan_headers_revision}',
  },

  'vulkan-loader/src': {
    'url': '{chromium_git}/external/github.com/KhronosGroup/Vulkan-Loader@{vulkan_loader_revision}',
  },

  'vulkan-tools/src': {
    'url': '{chromium_git}/external/github.com/KhronosGroup/Vulkan-Tools@{vulkan_tools_revision}',
  },

  'vulkan-utility-libraries/src': {
    'url': '{chromium_git}/external/github.com/KhronosGroup/Vulkan-Utility-Libraries@{vulkan_utility_libraries_revision}',
  },

  'vulkan-validation-layers/src': {
    'url': '{chromium_git}/external/github.com/KhronosGroup/Vulkan-ValidationLayers@{vulkan_validation_revision}',
  },
}
