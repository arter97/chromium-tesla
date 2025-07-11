# See docs/CMake.html for instructions about how to build LLVM with CMake.
cmake_minimum_required(VERSION 3.17.2) # HLSL Change - Require CMake 3.17.2.

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (NOT "${DXC_CMAKE_BEGINS_INCLUDE}" STREQUAL "")
  include(${DXC_CMAKE_BEGINS_INCLUDE})
endif()

if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  message(STATUS "No build type selected, default to Debug")
  set(CMAKE_BUILD_TYPE "Debug")
endif()

if(POLICY CMP0022)
  cmake_policy(SET CMP0022 NEW) # automatic when 2.8.12 is required
endif()

if (POLICY CMP0051)
  # CMake 3.1 and higher include generator expressions of the form
  # $<TARGETLIB:obj> in the SOURCES property.  These need to be
  # stripped everywhere that access the SOURCES property, so we just
  # defer to the OLD behavior of not including generator expressions
  # in the output for now.
  cmake_policy(SET CMP0051 OLD)
endif()

if(CMAKE_VERSION VERSION_LESS 3.1.20141117)
  set(cmake_3_2_USES_TERMINAL)
else()
  set(cmake_3_2_USES_TERMINAL USES_TERMINAL)
endif()

project(LLVM)

# The following only works with the Ninja generator in CMake >= 3.0.
set(LLVM_PARALLEL_COMPILE_JOBS "" CACHE STRING
  "Define the maximum number of concurrent compilation jobs.")
if(LLVM_PARALLEL_COMPILE_JOBS)
  if(CMAKE_VERSION VERSION_LESS 3.0 OR NOT CMAKE_MAKE_PROGRAM MATCHES "ninja$")
    message(WARNING "Job pooling is only available with Ninja generators and CMake 3.0 and later.")
  else()
    set_property(GLOBAL APPEND PROPERTY JOB_POOLS compile_job_pool=${LLVM_PARALLEL_COMPILE_JOBS})
    set(CMAKE_JOB_POOL_COMPILE compile_job_pool)
  endif()
endif()

set(LLVM_PARALLEL_LINK_JOBS "" CACHE STRING
  "Define the maximum number of concurrent link jobs.")
if(LLVM_PARALLEL_LINK_JOBS)
  if(CMAKE_VERSION VERSION_LESS 3.0 OR NOT CMAKE_MAKE_PROGRAM MATCHES "ninja$")
    message(WARNING "Job pooling is only available with Ninja generators and CMake 3.0 and later.")
  else()
    set_property(GLOBAL APPEND PROPERTY JOB_POOLS link_job_pool=${LLVM_PARALLEL_LINK_JOBS})
    set(CMAKE_JOB_POOL_LINK link_job_pool)
  endif()
endif()

# Add path for custom modules
set(CMAKE_MODULE_PATH
  ${CMAKE_MODULE_PATH}
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake"
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules"
  )

set(LLVM_VERSION_MAJOR 3)
set(LLVM_VERSION_MINOR 7)
set(LLVM_VERSION_PATCH 0)
set(LLVM_VERSION_SUFFIX "")

if (NOT PACKAGE_VERSION)
  set(PACKAGE_VERSION
    "${LLVM_VERSION_MAJOR}.${LLVM_VERSION_MINOR}.${LLVM_VERSION_PATCH}${LLVM_VERSION_SUFFIX}")
endif()

option(LLVM_INSTALL_UTILS "Include utility binaries in the 'install' target." OFF)

option(LLVM_INSTALL_TOOLCHAIN_ONLY "Only include toolchain files in the 'install' target." OFF)

option(LLVM_USE_FOLDERS "Enable solution folders in Visual Studio. Disable for Express versions." ON)
if ( LLVM_USE_FOLDERS )
  set_property(GLOBAL PROPERTY USE_FOLDERS ON)
endif()

option(HLSL_EMBED_VERSION "If set, embeds version information in the binaries." ON) # HLSL Change
# HLSL Change - Directory with version.inc file used for the build (generated or copied from HLSL_FIXED_VERSION_LOCATION)
set(HLSL_VERSION_LOCATION ${CMAKE_CURRENT_BINARY_DIR}/utils/version)
option(HLSL_OFFICIAL_BUILD "If set, embeds official version in the binaries (based on the latest release version)" OFF) # HLSL Change
option(HLSL_ENABLE_FIXED_VER "Reads version information from a file." OFF) # HLSL Change

option(HLSL_ENABLE_ANALYZE "Enables compiler analysis during compilation." OFF) # HLSL Change
option(HLSL_OPTIONAL_PROJS_IN_DEFAULT "Include optional projects in default build target." OFF) # HLSL Change
option(HLSL_BUILD_DXILCONV "Include DXBC to DXIL converter and tools." ON) # HLSL Change

option(HLSL_ENABLE_DEBUG_ITERATORS "Disable debug iterators for faster debug and to remove some additional allocations with improper noexcept attribution" OFF) # HLSL Change

# HLSL Change Starts - support commit querying
option(HLSL_SUPPORT_QUERY_GIT_COMMIT_INFO "Supports querying Git commit info." ON)
if ( HLSL_SUPPORT_QUERY_GIT_COMMIT_INFO )
  add_definitions(-DSUPPORT_QUERY_GIT_COMMIT_INFO)
endif()

option(DXC_DISABLE_ALLOCATOR_OVERRIDES "Disable usage of allocator overrides" OFF)
mark_as_advanced(DXC_DISABLE_ALLOCATOR_OVERRIDES)

option(DXC_CODEGEN_EXCEPTIONS_TRAP "An exception in code generation generates a trap, ending the compiler process" OFF)
mark_as_advanced(DXC_CODEGEN_EXCEPTIONS_TRAP)

# adjust link option to enable debugging from kernel mode; not compatible with incremental linking
if(NOT CMAKE_VERSION VERSION_LESS "3.13" AND WIN32 AND NOT CMAKE_C_COMPILER_ARCHITECTURE_ID STREQUAL "ARM64EC")
  add_link_options(/DEBUGTYPE:CV,FIXUP,PDATA /INCREMENTAL:NO)
endif()

# enable control flow guard
if(WIN32)
add_compile_options(/guard:cf)
add_link_options(/guard:cf)
endif(WIN32)

# Enable CET Shadow Stack
if(WIN32 AND NOT (CMAKE_C_COMPILER_ARCHITECTURE_ID MATCHES "ARM.*"))
  add_link_options(/CETCOMPAT)
endif()

# HLSL Change Ends

# HLSL Change Starts - set flag for Appveyor CI
if ( "$ENV{CI}" AND "$ENV{APPVEYOR}" )
  add_definitions(-DDXC_ON_APPVEYOR_CI)
endif()
# HLSL Change Ends

# SPIRV change starts
option(ENABLE_SPIRV_CODEGEN "Enables SPIR-V code generation." OFF)
option(SPIRV_BUILD_TESTS "Build targets for the SPIR-V unit tests." OFF)

# Enable SPIR-V CodeGen for Linux by default.
if(NOT WIN32)
  set(ENABLE_SPIRV_CODEGEN ON)
endif()

if (${SPIRV_BUILD_TESTS})
  enable_testing()
  set(ENABLE_SPIRV_CODEGEN ON)
endif()
if (${ENABLE_SPIRV_CODEGEN})
  add_definitions(-DENABLE_SPIRV_CODEGEN)
endif()
# SPIRV change ends

include(VersionFromVCS)

option(LLVM_APPEND_VC_REV
  "Append the version control system revision id to LLVM version" OFF)

if( LLVM_APPEND_VC_REV )
  add_version_info_from_vcs(PACKAGE_VERSION)
endif()

set(PACKAGE_NAME LLVM)
set(PACKAGE_STRING "${PACKAGE_NAME} ${PACKAGE_VERSION}")
set(PACKAGE_BUGREPORT "http://llvm.org/bugs/")

set(BUG_REPORT_URL "${PACKAGE_BUGREPORT}" CACHE STRING
  "Default URL where bug reports are to be submitted.")

# Configure CPack.
if(0) # HLSL Change - disable CPack
set(CPACK_PACKAGE_INSTALL_DIRECTORY "LLVM")
set(CPACK_PACKAGE_VENDOR "LLVM")
set(CPACK_PACKAGE_VERSION_MAJOR ${LLVM_VERSION_MAJOR})
set(CPACK_PACKAGE_VERSION_MINOR ${LLVM_VERSION_MINOR})
set(CPACK_PACKAGE_VERSION_PATCH ${LLVM_VERSION_PATCH})
set(CPACK_PACKAGE_VERSION ${PACKAGE_VERSION})
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE.TXT")
set(CPACK_NSIS_COMPRESSOR "/SOLID lzma \r\n SetCompressorDictSize 32")
if(WIN32 AND NOT UNIX)
  set(CPACK_PACKAGE_INSTALL_REGISTRY_KEY "LLVM")
  set(CPACK_PACKAGE_ICON "${CMAKE_CURRENT_SOURCE_DIR}\\\\cmake\\\\nsis_logo.bmp")
  set(CPACK_NSIS_MUI_ICON "${CMAKE_CURRENT_SOURCE_DIR}\\\\cmake\\\\nsis_icon.ico")
  set(CPACK_NSIS_MUI_UNIICON "${CMAKE_CURRENT_SOURCE_DIR}\\\\cmake\\\\nsis_icon.ico")
  set(CPACK_NSIS_MODIFY_PATH "ON")
  set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL "ON")
  set(CPACK_NSIS_EXTRA_INSTALL_COMMANDS
    "ExecWait '$INSTDIR/tools/msbuild/install.bat'")
  set(CPACK_NSIS_EXTRA_UNINSTALL_COMMANDS
    "ExecWait '$INSTDIR/tools/msbuild/uninstall.bat'")
  if( CMAKE_CL_64 )
    set(CPACK_NSIS_INSTALL_ROOT "$PROGRAMFILES64")
  endif()
endif()
include(CPack)
endif(0) # HLSL Change - disable CPack

# Sanity check our source directory to make sure that we are not trying to
# generate an in-tree build (unless on MSVC_IDE, where it is ok), and to make
# sure that we don't have any stray generated files lying around in the tree
# (which would end up getting picked up by header search, instead of the correct
# versions).
if( CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR AND NOT MSVC_IDE )
  message(FATAL_ERROR "In-source builds are not allowed.
CMake would overwrite the makefiles distributed with LLVM.
Please create a directory and run cmake from there, passing the path
to this source directory as the last argument.
This process created the file `CMakeCache.txt' and the directory `CMakeFiles'.
Please delete them.")
endif()
if( NOT CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR )
  file(GLOB_RECURSE
    tablegenned_files_on_include_dir
    "${CMAKE_CURRENT_SOURCE_DIR}/include/llvm/*.gen")
  file(GLOB_RECURSE
    tablegenned_files_on_lib_dir
    "${CMAKE_CURRENT_SOURCE_DIR}/lib/Target/*.inc")
  if( tablegenned_files_on_include_dir OR tablegenned_files_on_lib_dir)
    # HLSL Change - change FATAL_ERROR to WARNING to avoid problems with extraneous files
    message(WARNING "Apparently there is a previous in-source build,
probably as the result of running `configure' and `make' on
${CMAKE_CURRENT_SOURCE_DIR}.
This may cause problems. The suspicious files are:
${tablegenned_files_on_lib_dir}
${tablegenned_files_on_include_dir}
Please clean the source directory.")
  endif()
endif()

string(TOUPPER "${CMAKE_BUILD_TYPE}" uppercase_CMAKE_BUILD_TYPE)

set(LLVM_LIBDIR_SUFFIX "" CACHE STRING "Define suffix of library directory name (32/64)" )

# They are used as destination of target generators.
set(LLVM_RUNTIME_OUTPUT_INTDIR ${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_CFG_INTDIR}/bin)
set(LLVM_LIBRARY_OUTPUT_INTDIR ${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_CFG_INTDIR}/lib${LLVM_LIBDIR_SUFFIX})
if(WIN32 OR CYGWIN)
  # DLL platform -- put DLLs into bin.
  set(LLVM_SHLIB_OUTPUT_INTDIR ${LLVM_RUNTIME_OUTPUT_INTDIR})
else()
  set(LLVM_SHLIB_OUTPUT_INTDIR ${LLVM_LIBRARY_OUTPUT_INTDIR})
endif()

# Each of them corresponds to llvm-config's.
set(LLVM_TOOLS_BINARY_DIR ${LLVM_RUNTIME_OUTPUT_INTDIR}) # --bindir
set(LLVM_LIBRARY_DIR      ${LLVM_LIBRARY_OUTPUT_INTDIR}) # --libdir
set(LLVM_MAIN_SRC_DIR     ${CMAKE_CURRENT_SOURCE_DIR}  ) # --src-root
set(LLVM_MAIN_INCLUDE_DIR ${LLVM_MAIN_SRC_DIR}/include ) # --includedir
set(LLVM_BINARY_DIR       ${CMAKE_CURRENT_BINARY_DIR}  ) # --prefix

set(LLVM_EXAMPLES_BINARY_DIR ${LLVM_BINARY_DIR}/examples)
set(LLVM_INCLUDE_DIR ${CMAKE_CURRENT_BINARY_DIR}/include)

# HLSL removed targets AArch64, ARM, BPF, CppBackend, Hexagon, Mips, MSP430, PowerPC, Sparc, SystemZ, X86, XCore
set(LLVM_ALL_TARGETS
  AMDGPU
  NVPTX
  )

# List of targets with JIT support:
set(LLVM_TARGETS_WITH_JIT X86 PowerPC AArch64 ARM Mips SystemZ)

set(LLVM_TARGETS_TO_BUILD "all"
    CACHE STRING "Semicolon-separated list of targets to build, or \"all\".")

set(LLVM_EXPERIMENTAL_TARGETS_TO_BUILD ""
  CACHE STRING "Semicolon-separated list of experimental targets to build.")

option(BUILD_SHARED_LIBS
  "Build all libraries as shared libraries instead of static" OFF)

option(LLVM_ENABLE_TIMESTAMPS "Enable embedding timestamp information in build" ON)
if(LLVM_ENABLE_TIMESTAMPS)
  set(ENABLE_TIMESTAMPS 1)
endif()

option(LLVM_ENABLE_BACKTRACES "Enable embedding backtraces on crash." ON)
if(LLVM_ENABLE_BACKTRACES)
  set(ENABLE_BACKTRACES 1)
endif()

option(LLVM_ENABLE_CRASH_OVERRIDES "Enable crash overrides." ON)
if(LLVM_ENABLE_CRASH_OVERRIDES)
  set(ENABLE_CRASH_OVERRIDES 1)
endif()

option(LLVM_ENABLE_FFI "Use libffi to call external functions from the interpreter" OFF)
set(FFI_LIBRARY_DIR "" CACHE PATH "Additional directory, where CMake should search for libffi.so")
set(FFI_INCLUDE_DIR "" CACHE PATH "Additional directory, where CMake should search for ffi.h or ffi/ffi.h")

set(LLVM_TARGET_ARCH "host"
  CACHE STRING "Set target to use for LLVM JIT or use \"host\" for automatic detection.")

option(LLVM_ENABLE_TERMINFO "Use terminfo database if available." ON)

option(LLVM_ENABLE_THREADS "Use threads if available." ON)

option(LLVM_ENABLE_ZLIB "Use zlib for compression/decompression if available." ON)

if( LLVM_TARGETS_TO_BUILD STREQUAL "all" )
  set( LLVM_TARGETS_TO_BUILD ${LLVM_ALL_TARGETS} )
endif()

set(LLVM_TARGETS_TO_BUILD
   ${LLVM_TARGETS_TO_BUILD}
   ${LLVM_EXPERIMENTAL_TARGETS_TO_BUILD})
list(REMOVE_DUPLICATES LLVM_TARGETS_TO_BUILD)

include(AddLLVMDefinitions)

option(LLVM_ENABLE_PIC "Build Position-Independent Code" ON)
option(LLVM_ENABLE_WARNINGS "Enable compiler warnings." ON)
option(LLVM_ENABLE_MODULES "Compile with C++ modules enabled." OFF)
option(LLVM_ENABLE_CXX1Y "Compile with C++1y enabled." OFF)
option(LLVM_ENABLE_LIBCXX "Use libc++ if available." OFF)
option(LLVM_ENABLE_LIBCXXABI "Use libc++abi when using libc++." OFF)
option(LLVM_ENABLE_PEDANTIC "Compile with pedantic enabled." ON)
option(LLVM_ENABLE_WERROR "Fail and stop if a warning is triggered." OFF)

if( NOT uppercase_CMAKE_BUILD_TYPE STREQUAL "DEBUG" )
  option(LLVM_ENABLE_ASSERTIONS "Enable assertions" OFF)
else()
  option(LLVM_ENABLE_ASSERTIONS "Enable assertions" ON)
endif()

set(LLVM_ABI_BREAKING_CHECKS "WITH_ASSERTS" CACHE STRING
  "Enable abi-breaking checks.  Can be WITH_ASSERTS, FORCE_ON or FORCE_OFF.")

option(LLVM_FORCE_USE_OLD_HOST_TOOLCHAIN
       "Set to ON to force using an old, unsupported host toolchain." OFF)

option(LLVM_USE_INTEL_JITEVENTS
  "Use Intel JIT API to inform Intel(R) VTune(TM) Amplifier XE 2011 about JIT code"
  OFF)

if( LLVM_USE_INTEL_JITEVENTS )
  # Verify we are on a supported platform
  if( NOT CMAKE_SYSTEM_NAME MATCHES "Windows" AND NOT CMAKE_SYSTEM_NAME MATCHES "Linux" )
    message(FATAL_ERROR
      "Intel JIT API support is available on Linux and Windows only.")
  endif()
endif( LLVM_USE_INTEL_JITEVENTS )

option(LLVM_USE_OPROFILE
  "Use opagent JIT interface to inform OProfile about JIT code" OFF)

# If enabled, verify we are on a platform that supports oprofile.
if( LLVM_USE_OPROFILE )
  if( NOT CMAKE_SYSTEM_NAME MATCHES "Linux" )
    message(FATAL_ERROR "OProfile support is available on Linux only.")
  endif( NOT CMAKE_SYSTEM_NAME MATCHES "Linux" )
endif( LLVM_USE_OPROFILE )

set(LLVM_USE_SANITIZER "" CACHE STRING
  "Define the sanitizer used to build binaries and tests.")
option(LLVM_OPTIMIZE_SANITIZED_BUILDS "Enable -O1 on sanitized builds" On)

option(LLVM_USE_SPLIT_DWARF
  "Use -gsplit-dwarf when compiling llvm." OFF)

option(WITH_POLLY "Build LLVM with Polly" ON)
option(LINK_POLLY_INTO_TOOLS "Static link Polly into tools" OFF)

# Define an option controlling whether we should build for 32-bit on 64-bit
# platforms, where supported.
if( CMAKE_SIZEOF_VOID_P EQUAL 8 AND NOT WIN32 )
  # TODO: support other platforms and toolchains.
  option(LLVM_BUILD_32_BITS "Build 32 bits executables and libraries." OFF)
endif()

# Define the default arguments to use with 'lit', and an option for the user to
# override.
set(LIT_ARGS_DEFAULT "-sv")
if (MSVC OR XCODE)
  set(LIT_ARGS_DEFAULT "${LIT_ARGS_DEFAULT} --no-progress-bar")
endif()
set(LLVM_LIT_ARGS "${LIT_ARGS_DEFAULT}" CACHE STRING "Default options for lit")

# On Win32 hosts, provide an option to specify the path to the GnuWin32 tools.
if( WIN32 AND NOT CYGWIN )
  set(LLVM_LIT_TOOLS_DIR "" CACHE PATH "Path to GnuWin32 tools")
endif()

# Define options to control the inclusion and default build behavior for
# components which may not strictly be necessary (tools, examples, and tests).
#
# This is primarily to support building smaller or faster project files.
option(LLVM_INCLUDE_TOOLS "Generate build targets for the LLVM tools." ON)
option(LLVM_BUILD_TOOLS
  "Build the LLVM tools. If OFF, just generate build targets." ON)

option(LLVM_INCLUDE_UTILS "Generate build targets for the LLVM utils." ON)

option(LLVM_BUILD_RUNTIME
  "Build the LLVM runtime libraries." ON)
option(LLVM_BUILD_EXAMPLES
  "Build the LLVM example programs. If OFF, just generate build targets." OFF)
option(LLVM_INCLUDE_EXAMPLES "Generate build targets for the LLVM examples" ON)

option(LLVM_BUILD_TESTS
  "Build LLVM unit tests. If OFF, just generate build targets." OFF)
option(LLVM_INCLUDE_TESTS "Generate build targets for the LLVM unit tests." ON)

option(HLSL_INCLUDE_TESTS  "Generate build targets for the HLSL unit tests." ON)

option (LLVM_BUILD_DOCS "Build the llvm documentation." OFF)
option (LLVM_INCLUDE_DOCS "Generate build targets for llvm documentation." ON)
option (LLVM_ENABLE_DOXYGEN "Use doxygen to generate llvm API documentation." OFF)
option (LLVM_ENABLE_SPHINX "Use Sphinx to generate llvm documentation." OFF)

option (LLVM_BUILD_EXTERNAL_COMPILER_RT
  "Build compiler-rt as an external project." OFF)

option(LLVM_BUILD_LLVM_DYLIB "Build libllvm dynamic library" OFF)
option(LLVM_DYLIB_EXPORT_ALL "Export all symbols from libLLVM.dylib (default is C API only" OFF)
option(LLVM_DISABLE_LLVM_DYLIB_ATEXIT "Disable llvm-shlib's atexit destructors." ON)
if(LLVM_DISABLE_LLVM_DYLIB_ATEXIT)
  set(DISABLE_LLVM_DYLIB_ATEXIT 1)
endif()

option(LLVM_OPTIMIZED_TABLEGEN "Force TableGen to be built with optimization" OFF)
if(CMAKE_CROSSCOMPILING OR (LLVM_OPTIMIZED_TABLEGEN AND LLVM_ENABLE_ASSERTIONS))
  set(LLVM_USE_HOST_TOOLS ON)
endif()

if (LLVM_BUILD_INSTRUMENTED OR LLVM_BUILD_INSTRUMENTED_COVERAGE)
  if(NOT LLVM_PROFILE_MERGE_POOL_SIZE)
    # A pool size of 1-2 is probably sufficient on a SSD. 3-4 should be fine
    # for spining disks. Anything higher may only help on slower mediums.
    set(LLVM_PROFILE_MERGE_POOL_SIZE "4")
  endif()
  if(NOT LLVM_PROFILE_FILE_PATTERN)
    if(NOT LLVM_PROFILE_DATA_DIR)
      set(LLVM_PROFILE_FILE_PATTERN "%${LLVM_PROFILE_MERGE_POOL_SIZE}m.profraw")
    else()
      file(TO_NATIVE_PATH "${LLVM_PROFILE_DATA_DIR}/%${LLVM_PROFILE_MERGE_POOL_SIZE}m.profraw" LLVM_PROFILE_FILE_PATTERN)
    endif()
  endif()
endif()

# All options referred to from HandleLLVMOptions have to be specified
# BEFORE this include, otherwise options will not be correctly set on
# first cmake run
include(config-ix)

# By default, we target the host, but this can be overridden at CMake
# invocation time.
set(LLVM_DEFAULT_TARGET_TRIPLE "${LLVM_HOST_TRIPLE}" CACHE STRING
  "Default target for which LLVM will generate code." )
set(TARGET_TRIPLE "${LLVM_DEFAULT_TARGET_TRIPLE}")

include(HandleLLVMOptions)

# Verify that we can find a Python 3 interpreter and force cmake to use it.
find_package(PythonInterp 3 REQUIRED)

######
# LLVMBuild Integration
#
# We use llvm-build to generate all the data required by the CMake based
# build system in one swoop:
#
#  - We generate a file (a CMake fragment) in the object root which contains
#    all the definitions that are required by CMake.
#
#  - We generate the library table used by llvm-config.
#
#  - We generate the dependencies for the CMake fragment, so that we will
#    automatically reconfigure outselves.

set(LLVMBUILDTOOL "${LLVM_MAIN_SRC_DIR}/utils/llvm-build/llvm-build")
set(LLVMCONFIGLIBRARYDEPENDENCIESINC
  "${LLVM_BINARY_DIR}/tools/llvm-config/LibraryDependencies.inc")
set(LLVMBUILDCMAKEFRAG
  "${LLVM_BINARY_DIR}/LLVMBuild.cmake")

# Create the list of optional components that are enabled
if (LLVM_USE_INTEL_JITEVENTS)
  set(LLVMOPTIONALCOMPONENTS IntelJITEvents)
endif (LLVM_USE_INTEL_JITEVENTS)
if (LLVM_USE_OPROFILE)
  set(LLVMOPTIONALCOMPONENTS ${LLVMOPTIONALCOMPONENTS} OProfileJIT)
endif (LLVM_USE_OPROFILE)

# HLSL Change: use 'Unknown' in place of "${LLVM_NATIVE_ARCH}"
message(STATUS "Constructing LLVMBuild project information")

execute_process(
  COMMAND ${PYTHON_EXECUTABLE} ${LLVMBUILDTOOL}
            --native-target Unknown
            --enable-targets "${LLVM_TARGETS_TO_BUILD}"
            --enable-optional-components "${LLVMOPTIONALCOMPONENTS}"
            --write-library-table ${LLVMCONFIGLIBRARYDEPENDENCIESINC}
            --write-cmake-fragment ${LLVMBUILDCMAKEFRAG}
            OUTPUT_VARIABLE LLVMBUILDOUTPUT
            ERROR_VARIABLE LLVMBUILDERRORS
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_STRIP_TRAILING_WHITESPACE
  RESULT_VARIABLE LLVMBUILDRESULT)

# On Win32, CMake doesn't properly handle piping the default output/error
# streams into the GUI console. So, we explicitly catch and report them.
if( NOT "${LLVMBUILDOUTPUT}" STREQUAL "")
  message(STATUS "llvm-build output: ${LLVMBUILDOUTPUT}")
endif()
if( NOT "${LLVMBUILDRESULT}" STREQUAL "0" )
  message(FATAL_ERROR
    "Unexpected failure executing llvm-build: ${LLVMBUILDERRORS}")
endif()

# Include the generated CMake fragment. This will define properties from the
# LLVMBuild files in a format which is easy to consume from CMake, and will add
# the dependencies so that CMake will reconfigure properly when the LLVMBuild
# files change.
include(${LLVMBUILDCMAKEFRAG})

######

# Configure all of the various header file fragments LLVM uses which depend on
# configuration variables.
set(LLVM_ENUM_TARGETS "")
set(LLVM_ENUM_ASM_PRINTERS "")
set(LLVM_ENUM_ASM_PARSERS "")
set(LLVM_ENUM_DISASSEMBLERS "")
foreach(t ${LLVM_TARGETS_TO_BUILD})
  if(NOT t STREQUAL "None") # HLSL Change - support for no target, but no continue() in CMake 2.8

  set( td ${LLVM_MAIN_SRC_DIR}/lib/Target/${t} )

  list(FIND LLVM_ALL_TARGETS ${t} idx)
  list(FIND LLVM_EXPERIMENTAL_TARGETS_TO_BUILD ${t} idy)
  if( idx LESS 0 AND idy LESS 0 )
    message(FATAL_ERROR "The target `${t}' does not exist.
    It should be one of\n${LLVM_ALL_TARGETS}")
  else()
    set(LLVM_ENUM_TARGETS "${LLVM_ENUM_TARGETS}LLVM_TARGET(${t})\n")
  endif()

  file(GLOB asmp_file "${td}/*AsmPrinter.cpp")
  if( asmp_file )
    set(LLVM_ENUM_ASM_PRINTERS
      "${LLVM_ENUM_ASM_PRINTERS}LLVM_ASM_PRINTER(${t})\n")
  endif()
  if( EXISTS ${td}/AsmParser/CMakeLists.txt )
    set(LLVM_ENUM_ASM_PARSERS
      "${LLVM_ENUM_ASM_PARSERS}LLVM_ASM_PARSER(${t})\n")
  endif()
  if( EXISTS ${td}/Disassembler/CMakeLists.txt )
    set(LLVM_ENUM_DISASSEMBLERS
      "${LLVM_ENUM_DISASSEMBLERS}LLVM_DISASSEMBLER(${t})\n")
  endif()

  endif() # HLSL Change - support for no target
endforeach(t)

# Produce the target definition files, which provide a way for clients to easily
# include various classes of targets.
configure_file(
  ${LLVM_MAIN_INCLUDE_DIR}/llvm/Config/AsmPrinters.def.in
  ${LLVM_INCLUDE_DIR}/llvm/Config/AsmPrinters.def
  )
configure_file(
  ${LLVM_MAIN_INCLUDE_DIR}/llvm/Config/AsmParsers.def.in
  ${LLVM_INCLUDE_DIR}/llvm/Config/AsmParsers.def
  )
configure_file(
  ${LLVM_MAIN_INCLUDE_DIR}/llvm/Config/Disassemblers.def.in
  ${LLVM_INCLUDE_DIR}/llvm/Config/Disassemblers.def
  )
configure_file(
  ${LLVM_MAIN_INCLUDE_DIR}/llvm/Config/Targets.def.in
  ${LLVM_INCLUDE_DIR}/llvm/Config/Targets.def
  )

# Configure the three LLVM configuration header files.
configure_file(
  ${LLVM_MAIN_INCLUDE_DIR}/llvm/Config/config.h.cmake
  ${LLVM_INCLUDE_DIR}/llvm/Config/config.h)
configure_file(
  ${LLVM_MAIN_INCLUDE_DIR}/llvm/Config/llvm-config.h.cmake
  ${LLVM_INCLUDE_DIR}/llvm/Config/llvm-config.h)
configure_file(
  ${LLVM_MAIN_INCLUDE_DIR}/llvm/Config/abi-breaking.h.cmake
  ${LLVM_INCLUDE_DIR}/llvm/Config/abi-breaking.h)
configure_file(
  ${LLVM_MAIN_INCLUDE_DIR}/llvm/Support/DataTypes.h.cmake
  ${LLVM_INCLUDE_DIR}/llvm/Support/DataTypes.h)

# They are not referenced. See set_output_directory().
set( CMAKE_RUNTIME_OUTPUT_DIRECTORY ${LLVM_BINARY_DIR}/bin )
set( CMAKE_LIBRARY_OUTPUT_DIRECTORY ${LLVM_BINARY_DIR}/lib${LLVM_LIBDIR_SUFFIX} )
set( CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${LLVM_BINARY_DIR}/lib${LLVM_LIBDIR_SUFFIX} )

set(CMAKE_BUILD_WITH_INSTALL_RPATH ON)
if (APPLE)
  if(NOT DEFINED CMAKE_INSTALL_RPATH)
    set(CMAKE_INSTALL_NAME_DIR "@rpath")
    set(CMAKE_INSTALL_RPATH "@executable_path/../lib")
    endif(NOT DEFINED CMAKE_INSTALL_RPATH)
else(UNIX)
  if(NOT DEFINED CMAKE_INSTALL_RPATH)
    set(CMAKE_INSTALL_RPATH "\$ORIGIN/../lib${LLVM_LIBDIR_SUFFIX}")
    if(${CMAKE_SYSTEM_NAME} MATCHES "(FreeBSD|DragonFly)")
      set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-z,origin")
      set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,-z,origin")
    endif()
  endif(NOT DEFINED CMAKE_INSTALL_RPATH)
endif()

# Work around a broken bfd ld behavior. When linking a binary with a
# foo.so library, it will try to find any library that foo.so uses and
# check its symbols. This is wasteful (the check was done when foo.so
# was created) and can fail since it is not the dynamic linker and
# doesn't know how to handle search paths correctly.
if (UNIX AND NOT APPLE AND NOT ${CMAKE_SYSTEM_NAME} MATCHES "SunOS")
  set(CMAKE_EXE_LINKER_FLAGS
      "${CMAKE_EXE_LINKER_FLAGS} -Wl,-allow-shlib-undefined")
endif()

set(CMAKE_INCLUDE_CURRENT_DIR ON)

# HLSL Change Starts - override assert to RaiseException instead of abort
# for better test behavior
if(WIN32 AND NOT UNIX)
  include_directories(BEFORE "${LLVM_MAIN_INCLUDE_DIR}/llvm/llvm_assert")
endif()
# HLSL Change Ends

include_directories( ${LLVM_INCLUDE_DIR} ${LLVM_MAIN_INCLUDE_DIR})

include_directories( ${LLVM_INCLUDE_DIR}/dxc/Tracing) # HLSL Change

# when crosscompiling import the executable targets from a file
if(LLVM_USE_HOST_TOOLS)
  include(CrossCompile)
endif(LLVM_USE_HOST_TOOLS)

if(${CMAKE_SYSTEM_NAME} MATCHES "(FreeBSD|DragonFly)")
  # On FreeBSD, /usr/local/* is not used by default. In order to build LLVM
  # with libxml2, iconv.h, etc., we must add /usr/local paths.
  include_directories("/usr/local/include")
  link_directories("/usr/local/lib")
endif(${CMAKE_SYSTEM_NAME} MATCHES "(FreeBSD|DragonFly)")

if( ${CMAKE_SYSTEM_NAME} MATCHES SunOS )
   SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -include llvm/Support/Solaris.h")
endif( ${CMAKE_SYSTEM_NAME} MATCHES SunOS )

# Make sure we don't get -rdynamic in every binary. For those that need it,
# use export_executable_symbols(target).
set(CMAKE_SHARED_LIBRARY_LINK_CXX_FLAGS "")

# enable warnings as errors for debug build
if (WIN32)
  SET(CMAKE_CXX_FLAGS_DEBUG  "${CMAKE_CXX_FLAGS_DEBUG} /WX")
endif (WIN32)

include(AddLLVM)
include(TableGen)

if( MINGW )
  # People report that -O3 is unreliable on MinGW. The traditional
  # build also uses -O2 for that reason:
  llvm_replace_compiler_option(CMAKE_CXX_FLAGS_RELEASE "-O3" "-O2")
endif()

if(LLVM_INCLUDE_TESTS AND WIN32)
  add_definitions(/DMSFT_SUPPORTS_CHILD_PROCESSES)
endif()

# Put this before tblgen. Else we have a circular dependence.
add_subdirectory(lib/Support)
add_subdirectory(lib/MSSupport) # HLSL Change
add_subdirectory(lib/TableGen)

add_subdirectory(utils/TableGen)

add_subdirectory(include/llvm)
# HLSL - Change Start
add_subdirectory(include/dxc)

# This over-specifies the dependency graph, but since generating these doesn't
# really depend on anything else in the build it is safe.
list(APPEND LLVM_COMMON_DEPENDS HCTGen) 

if(EXISTS "${LLVM_MAIN_SRC_DIR}/external")
  add_subdirectory(external) # SPIRV change
endif()
include_directories(AFTER ${DIRECTX_HEADER_INCLUDE_DIR}/directx ${DIRECTX_HEADER_INCLUDE_DIR}/wsl/stubs)

# HLSL - Change End

add_subdirectory(lib)

if (HLSL_EMBED_VERSION) 
  add_subdirectory(utils/version)
endif()

if(LLVM_INCLUDE_UTILS AND LLVM_INCLUDE_TESTS) # HLSL Change - only when testing
  add_subdirectory(utils/FileCheck)
  # add_subdirectory(utils/FileUpdate) # HLSL Change
  add_subdirectory(utils/count)
  add_subdirectory(utils/not)
  add_subdirectory(utils/llvm-lit)
  add_subdirectory(utils/yaml-bench)
else()
  if ( LLVM_INCLUDE_TESTS )
    message(FATAL_ERROR "Including tests when not building utils will not work.
    Either set LLVM_INCLUDE_UTILS to On, or set LLVM_INCLDE_TESTS to Off.")
  endif()
endif()

if(LLVM_INCLUDE_TESTS OR HLSL_INCLUDE_TESTS OR SPIRV_BUILD_TESTS) # HLSL Change
  add_subdirectory(utils/unittest)
endif()

foreach( binding ${LLVM_BINDINGS_LIST} )
  if( EXISTS "${LLVM_MAIN_SRC_DIR}/bindings/${binding}/CMakeLists.txt" )
    add_subdirectory(bindings/${binding})
  endif()
endforeach()

add_subdirectory(projects)

if(WITH_POLLY)
  if(NOT EXISTS ${LLVM_MAIN_SRC_DIR}/tools/polly/CMakeLists.txt)
    set(WITH_POLLY OFF)
  endif()
endif(WITH_POLLY)

if( LLVM_INCLUDE_TOOLS )
  add_subdirectory(tools)
endif()


if( LLVM_INCLUDE_EXAMPLES AND 0 ) # HLSL Change - never build examples
  add_subdirectory(examples)
endif()

if( LLVM_INCLUDE_TESTS )
  add_subdirectory(test)
  add_subdirectory(unittests)
  if (MSVC)
    # This utility is used to prevent crashing tests from calling Dr. Watson on
    # Windows.
    add_subdirectory(utils/KillTheDoctor)
  endif()

  # Add a global check rule now that all subdirectories have been traversed
  # and we know the total set of lit testsuites.
  get_property(LLVM_LIT_TESTSUITES GLOBAL PROPERTY LLVM_LIT_TESTSUITES)
  get_property(LLVM_LIT_PARAMS GLOBAL PROPERTY LLVM_LIT_PARAMS)
  get_property(LLVM_LIT_DEPENDS GLOBAL PROPERTY LLVM_LIT_DEPENDS)
  get_property(LLVM_LIT_EXTRA_ARGS GLOBAL PROPERTY LLVM_LIT_EXTRA_ARGS)
  add_lit_target(check-all
    "Running all regression tests"
    ${LLVM_LIT_TESTSUITES}
    PARAMS ${LLVM_LIT_PARAMS}
    DEPENDS ${LLVM_LIT_DEPENDS}
    ARGS ${LLVM_LIT_EXTRA_ARGS}
    )
  add_custom_target(test-depends DEPENDS ${LLVM_LIT_DEPENDS}) # HLSL Change
endif()

if (LLVM_INCLUDE_DOCS)
  add_subdirectory(docs)
endif()

add_hlsl_hctgen(DxilDocs OUTPUT docs/DXIL.rst CODE_TAG) # HLSL Change

add_subdirectory(cmake/modules)

if (NOT LLVM_INSTALL_TOOLCHAIN_ONLY)
  install(DIRECTORY include/llvm include/llvm-c
    DESTINATION include
    COMPONENT llvm-headers
    FILES_MATCHING
    PATTERN "*.def"
    PATTERN "*.h"
    PATTERN "*.td"
    PATTERN "*.inc"
    PATTERN "LICENSE.TXT"
    PATTERN ".svn" EXCLUDE
    )

  install(DIRECTORY ${LLVM_INCLUDE_DIR}/llvm
    DESTINATION include
    COMPONENT llvm-headers
    FILES_MATCHING
    PATTERN "*.def"
    PATTERN "*.h"
    PATTERN "*.gen"
    PATTERN "*.inc"
    # Exclude include/llvm/CMakeFiles/intrinsics_gen.dir, matched by "*.def"
    PATTERN "CMakeFiles" EXCLUDE
    PATTERN "config.h" EXCLUDE
    PATTERN ".svn" EXCLUDE
    )

  if (NOT CMAKE_CONFIGURATION_TYPES)
    add_custom_target(installhdrs
                      DEPENDS ${name}
                      COMMAND "${CMAKE_COMMAND}"
                              -DCMAKE_INSTALL_COMPONENT=llvm-headers
                              -P "${CMAKE_BINARY_DIR}/cmake_install.cmake")
  endif()
endif()

include(CoverageReport)

# HLSL Change begin - Set default distribution value.
set(LLVM_DISTRIBUTION_COMPONENTS "dxc;dxcompiler;dxc-headers" CACHE STRING "")
# HLSL Change end - Set default distribution value.

# This must be at the end of the LLVM root CMakeLists file because it must run
# after all targets are created.
if(LLVM_DISTRIBUTION_COMPONENTS)
  # HLSL Change - Deleted restriction for non-IDE builds. This works with Visual
  # Studio for DXC because of the limited number of targets.
  
  add_custom_target(install-distribution)
  foreach(target ${LLVM_DISTRIBUTION_COMPONENTS})
    if(TARGET install-${target})
      add_dependencies(install-distribution install-${target})
    else()
      message(FATAL_ERROR "Specified distribution component '${target}' doesn't have an install target")
    endif()
  endforeach()
endif()

# Disable regeneration target for official builds as they always fun a full clean build.
# This should eliminate the race-condition issue with "Cannot restore timestamp".
if (HLSL_OFFICIAL_BUILD)
  set(CMAKE_SUPPRESS_REGENERATION ON)
endif (HLSL_OFFICIAL_BUILD)

if (NOT "${DXC_CMAKE_ENDS_INCLUDE}" STREQUAL "")
  include(${DXC_CMAKE_ENDS_INCLUDE})
endif()
