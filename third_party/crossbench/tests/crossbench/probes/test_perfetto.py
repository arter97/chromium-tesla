# Copyright 2023 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import unittest

import hjson

import crossbench.path as pth
from crossbench.cli.config.probe import ProbeListConfig
from crossbench.probes.all import PerfettoProbe
from tests import test_helper


class TestPerfettoProbe(unittest.TestCase):

  def test_missing_config(self):
    with self.assertRaises(ValueError) as cm:
      PerfettoProbe.from_config({})
    self.assertIn("config", str(cm.exception))

  def test_parse_config(self):
    probe: PerfettoProbe = PerfettoProbe.from_config({"textproto": "TEXTPROTO"})
    self.assertEqual("TEXTPROTO", probe.textproto)
    self.assertEqual(pth.RemotePath("perfetto"), probe.perfetto_bin)

  @unittest.skipIf(hjson.__name__ != "hjson", "hjson not available")
  def test_parse_example_config(self):
    config_file = (
        test_helper.config_dir() / "probe" /
        "perfetto.probe.config.example.hjson")
    self.assertTrue(config_file.is_file())
    probes = ProbeListConfig.load_path(config_file).probes
    self.assertEqual(len(probes), 1)
    probe = probes[0]
    self.assertIsInstance(probe, PerfettoProbe)


if __name__ == "__main__":
  test_helper.run_pytest(__file__)
