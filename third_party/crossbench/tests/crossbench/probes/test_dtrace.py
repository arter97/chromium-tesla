# Copyright 2024 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import pathlib
import unittest

import hjson

from crossbench.cli.config.probe import ProbeListConfig
from crossbench.probes.dtrace import DTraceProbe
from tests import test_helper
from tests.crossbench.mock_helper import CrossbenchFakeFsTestCase


class TestDTraceProbe(CrossbenchFakeFsTestCase):

  @unittest.skipIf(hjson.__name__ != "hjson", "hjson not available")
  def test_parse_example_config(self):
    config_file = (
        test_helper.config_dir() / "probe" /
        "dtrace.probe.config.example.hjson")
    self.fs.add_real_file(config_file)
    self.assertTrue(config_file.is_file())
    example_script_file = pathlib.Path("~/dtrace_script.d").expanduser()
    self.fs.create_file(example_script_file, st_size=100)
    self.assertTrue(example_script_file.is_file())
    probes = ProbeListConfig.load_path(config_file).probes
    self.assertEqual(len(probes), 1)
    probe = probes[0]
    self.assertIsInstance(probe, DTraceProbe)
    isinstance(probe, DTraceProbe)
    self.assertEqual(probe.script_path, example_script_file)


if __name__ == "__main__":
  test_helper.run_pytest(__file__)
