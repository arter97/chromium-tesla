# Copyright 2024 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import unittest

import hjson

from crossbench.cli.config.probe import ProbeListConfig
from crossbench.probes.js import JSProbe
from tests import test_helper
from tests.crossbench.mock_helper import CrossbenchFakeFsTestCase


class TestJSProbe(CrossbenchFakeFsTestCase):

  @unittest.skipIf(hjson.__name__ != "hjson", "hjson not available")
  def test_parse_example_config(self):
    config_file = (
        test_helper.config_dir() / "probe" / "js.probe.config.example.hjson")
    self.fs.add_real_file(config_file)
    self.assertTrue(config_file.is_file())
    probes = ProbeListConfig.load_path(config_file).probes
    self.assertEqual(len(probes), 1)
    probe = probes[0]
    self.assertIsInstance(probe, JSProbe)
    isinstance(probe, JSProbe)
    self.assertTrue(probe.metric_js)

  def test_parse_config(self):
    config = {
        "setup": "globalThis.metrics = {};",
        "js": "return globalThis.metrics;",
    }
    probe = JSProbe.config_parser().parse(config)
    self.assertIsInstance(probe, JSProbe)
    self.assertEqual(probe.setup_js, "globalThis.metrics = {};")
    self.assertEqual(probe.metric_js, "return globalThis.metrics;")


if __name__ == "__main__":
  test_helper.run_pytest(__file__)
