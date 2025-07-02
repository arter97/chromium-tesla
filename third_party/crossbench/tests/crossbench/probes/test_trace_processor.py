# Copyright 2024 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import unittest

import hjson

from crossbench.cli.config.probe import ProbeListConfig
from crossbench.probes.all import TraceProcessorProbe
from tests import test_helper


class TestProbe(unittest.TestCase):

  def test_missing_probes(self):
    with self.assertRaises(ValueError) as cm:
      TraceProcessorProbe.from_config({})
    self.assertIn("probes", str(cm.exception))

  @unittest.skip("TODO: work around missing trace processor on bots")
  def test_parse_config(self):
    probe: TraceProcessorProbe = TraceProcessorProbe.from_config(
        {"probes": ["probe1", "probe2"]})
    self.assertEqual(["probe1", "probe2"], probe.probes)

  @unittest.skip("TODO: work around missing trace processor on bots")
  @unittest.skipIf(hjson.__name__ != "hjson", "hjson not available")
  def test_parse_example_config(self):
    config_file = (
        test_helper.config_dir() / "probe" /
        "trace_processor.probe.config.example.hjson")
    self.assertTrue(config_file.is_file())
    probes = ProbeListConfig.load_path(config_file).probes
    self.assertEqual(len(probes), 2)
    probe = probes[0]
    self.assertIsInstance(probe, TraceProcessorProbe)


if __name__ == "__main__":
  test_helper.run_pytest(__file__)
