#!/usr/bin/env python3
# Copyright 2018 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""code generator for webgpu command buffers."""

import filecmp
import os
import os.path
import sys
from optparse import OptionParser

import build_cmd_buffer_lib

# Named type info object represents a named type that is used in API call
# arguments. The named types are used in 'webgpu_cmd_buffer_functions.txt'.
#
# Options are documented in build_gles2_cmd_buffer.py/build_raster_cmd_buffer.py
_NAMED_TYPE_INFO = {
  'PowerPreference': {
    'type': 'PowerPreference',
    'valid': [
      'PowerPreference::kDefault',
      'PowerPreference::kHighPerformance',
      'PowerPreference::kLowPower',
    ],
    'invalid': [
      'PowerPreference::kNumPowerPreferences',
    ],
  },
  'MailboxFlags': {
    'type': 'MailboxFlags',
    'valid': [
      'WEBGPU_MAILBOX_NONE',
      'WEBGPU_MAILBOX_DISCARD',
    ],
  },
}

# A function info object specifies the type and other special data for the
# command that will be generated. A base function info object is generated by
# parsing the "webgpu_cmd_buffer_functions.txt", one for each function in the
# file. These function info objects can be augmented and their values can be
# overridden by adding an object to the table below.
#
# Must match function names specified in "webgpu_cmd_buffer_functions.txt".
#
# Options are documented in build_gles2_cmd_buffer.py/build_raster_cmd_buffer.py
# (Note: some options (like decoder_func and unit_test) currently have no
# effect, because WriteServiceImplementation and WriteServiceUnitTests are not
# used below.)
_FUNCTION_INFO = {
  'DawnCommands': {
    'impl_func': False,
    'internal': True,
    'data_transfer_methods': ['shm'],
    'cmd_args': 'uint32_t trace_id_high, uint32_t trace_id_low, '
                'uint32_t commands_shm_id, uint32_t commands_shm_offset, '
                'uint32_t size',
    'size_args': {
      'commands': 'size * sizeof(char)',
    },
  },
  'AssociateMailbox': {
    'impl_func': False,
    'client_test': False,
    'internal': True,
    'cmd_args': 'GLuint device_id, GLuint device_generation, GLuint id, '
                'GLuint generation, GLuint usage, '
                'GLuint internal_usage, MailboxFlags flags, '
                'GLuint view_format_count, GLuint count, '
                'const GLuint* mailbox_and_view_formats',
    'type': 'PUTn',
    'count': 1,
  },
  'DissociateMailbox': {
    'impl_func': False,
    'client_test': False,
  },
  'DissociateMailboxForPresent': {
    'impl_func': False,
    'client_test': False,
  },
  'DestroyServer': {
    'impl_func': False,
    'internal': True,
  },
  'SetWebGPUExecutionContextToken': {
    'impl_func': False,
    'client_test': False,
  },
}

def main(argv):
  """This is the main function."""
  parser = OptionParser()
  parser.add_option(
      "--output-dir",
      help="Output directory for generated files. Defaults to chromium root "
      "directory.")
  parser.add_option(
      "-v", "--verbose", action="store_true", help="Verbose logging output.")
  parser.add_option(
      "-c", "--check", action="store_true",
      help="Check if output files match generated files in chromium root "
      "directory.  Use this in PRESUBMIT scripts with --output-dir.")

  (options, _) = parser.parse_args(args=argv)

  # This script lives under src/gpu/command_buffer.
  script_dir = os.path.dirname(os.path.abspath(__file__))
  assert script_dir.endswith((os.path.normpath("src/gpu/command_buffer"),
                              os.path.normpath("chromium/gpu/command_buffer")))
  # os.path.join doesn't do the right thing with relative paths.
  chromium_root_dir = os.path.abspath(script_dir + "/../..")

  # Support generating files under gen/ and for PRESUBMIT.
  if options.output_dir:
    output_dir = options.output_dir
  else:
    output_dir = chromium_root_dir
  os.chdir(output_dir)

  # This script lives under gpu/command_buffer, cd to base directory.
  build_cmd_buffer_lib.InitializePrefix("WebGPU")
  gen = build_cmd_buffer_lib.GLGenerator(
      options.verbose, "2018", _FUNCTION_INFO, _NAMED_TYPE_INFO,
      chromium_root_dir)
  gen.ParseGLH("gpu/command_buffer/webgpu_cmd_buffer_functions.txt")

  gen.WriteCommandIds("gpu/command_buffer/common/webgpu_cmd_ids_autogen.h")
  gen.WriteFormat("gpu/command_buffer/common/webgpu_cmd_format_autogen.h")
  gen.WriteFormatTest(
    "gpu/command_buffer/common/webgpu_cmd_format_test_autogen.h")
  gen.WriteGLES2InterfaceHeader(
    "gpu/command_buffer/client/webgpu_interface_autogen.h")
  gen.WriteGLES2ImplementationHeader(
    "gpu/command_buffer/client/webgpu_implementation_autogen.h")
  gen.WriteGLES2InterfaceStub(
    "gpu/command_buffer/client/webgpu_interface_stub_autogen.h")
  gen.WriteGLES2InterfaceStubImpl(
      "gpu/command_buffer/client/webgpu_interface_stub_impl_autogen.h")
  gen.WriteGLES2Implementation(
    "gpu/command_buffer/client/webgpu_implementation_impl_autogen.h")
  gen.WriteGLES2ImplementationUnitTests(
    "gpu/command_buffer/client/webgpu_implementation_unittest_autogen.h")
  gen.WriteCmdHelperHeader(
     "gpu/command_buffer/client/webgpu_cmd_helper_autogen.h")
  # Note: No gen.WriteServiceImplementation
  # Note: No gen.WriteServiceUnitTests
  gen.WriteServiceUtilsHeader(
    "gpu/command_buffer/service/webgpu_cmd_validation_autogen.h")
  gen.WriteServiceUtilsImplementation(
    "gpu/command_buffer/service/"
    "webgpu_cmd_validation_implementation_autogen.h")

  build_cmd_buffer_lib.Format(gen.generated_cpp_filenames, output_dir,
                              chromium_root_dir)

  if gen.errors > 0:
    print("build_webgpu_cmd_buffer.py: Failed with %d errors" % gen.errors)
    return 1

  check_failed_filenames = []
  if options.check:
    for filename in gen.generated_cpp_filenames:
      if not filecmp.cmp(os.path.join(output_dir, filename),
                         os.path.join(chromium_root_dir, filename)):
        check_failed_filenames.append(filename)

  if len(check_failed_filenames) > 0:
    print('Please run gpu/command_buffer/build_webgpu_cmd_buffer.py')
    print('Failed check on autogenerated command buffer files:')
    for filename in check_failed_filenames:
      print(filename)
    return 1

  return 0


if __name__ == '__main__':
  sys.exit(main(sys.argv[1:]))
