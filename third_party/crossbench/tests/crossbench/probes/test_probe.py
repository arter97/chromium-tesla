# Copyright 2024 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import inspect

import crossbench.path as pth
from crossbench.probes.all import GENERAL_PURPOSE_PROBES, INTERNAL_PROBES
from crossbench.probes.debugger import DebuggerProbe
from crossbench.probes.dtrace import DTraceProbe
from crossbench.probes.perfetto import PerfettoProbe
from crossbench.probes.performance_entries import PerformanceEntriesProbe
from crossbench.probes.polling import ShellPollingProbe
from crossbench.probes.power_sampler import PowerSamplerProbe
from crossbench.probes.powermetrics import PowerMetricsProbe
from crossbench.probes.probe import Probe
from crossbench.probes.profiling.browser_profiling import BrowserProfilingProbe
from crossbench.probes.profiling.system_profiling import ProfilingProbe
from crossbench.probes.system_stats import SystemStatsProbe
from crossbench.probes.tracing import TracingProbe
from crossbench.probes.v8.builtins_pgo import V8BuiltinsPGOProbe
from crossbench.probes.v8.log import V8LogProbe
from crossbench.probes.v8.rcs import V8RCSProbe
from crossbench.probes.v8.turbolizer import V8TurbolizerProbe
from crossbench.probes.video import VideoProbe
from crossbench.probes.web_page_replay.recorder import WebPageReplayProbe
from tests import test_helper
from tests.crossbench.mock_helper import CrossbenchFakeFsTestCase


class ProbeTestCase(CrossbenchFakeFsTestCase):

  def probe_instances(self):
    yield from self.internal_probe_instances()
    yield from self.general_purpose_probe_instances()

  def internal_probe_instances(self):
    for probe_cls in self.internal_probe_classes():
      with self.subTest(probe_cls=probe_cls):
        try:
          yield probe_cls()
        except GeneratorExit:
          break

  def general_purpose_probe_instances(self):
    yield BrowserProfilingProbe()
    yield DebuggerProbe(pth.LocalPath("debugger.bin"))
    yield DTraceProbe(pth.LocalPath("script.dtrace"))
    yield PerfettoProbe("textproto", pth.LocalPath("perfetto.bin"))
    yield PerformanceEntriesProbe()
    yield PowerMetricsProbe()
    yield PowerSamplerProbe()
    yield ProfilingProbe()
    yield ShellPollingProbe(cmd=["ls"])
    yield SystemStatsProbe()
    yield TracingProbe()
    yield V8BuiltinsPGOProbe()
    yield V8LogProbe()
    yield V8RCSProbe()
    yield V8TurbolizerProbe()
    yield VideoProbe()
    yield WebPageReplayProbe(wpr_go_bin=self.create_file("wpr.go"))

  def probe_classes(self):
    yield from self.internal_probe_classes()
    yield from self.general_purpose_probe_classes()

  def all_probe_subclasses(self, probe_cls=Probe):
    for probe_sub_cls in probe_cls.__subclasses__():
      if "Mock" in str(probe_sub_cls):
        continue
      if not inspect.isabstract(probe_sub_cls):
        yield probe_sub_cls
      yield from self.all_probe_subclasses(probe_sub_cls)

  def internal_probe_classes(self):
    for probe_cls in INTERNAL_PROBES:
      with self.subTest(probe_cls=probe_cls):
        try:
          yield probe_cls
        except GeneratorExit:
          break

  def general_purpose_probe_classes(self):
    for probe_cls in GENERAL_PURPOSE_PROBES:
      with self.subTest(probe_cls=probe_cls):
        try:
          yield probe_cls
        except GeneratorExit:
          break

  def test_properties(self):
    for probe_cls in self.internal_probe_classes():
      self.assertFalse(probe_cls.IS_GENERAL_PURPOSE)
    for probe_cls in self.general_purpose_probe_classes():
      self.assertTrue(probe_cls.IS_GENERAL_PURPOSE)
    for probe_cls in self.probe_classes():
      self.assertTrue(probe_cls.NAME)

  def test_default_lists(self):
    count = 0
    for probe_cls in self.all_probe_subclasses():
      count += 1
      if probe_cls.IS_GENERAL_PURPOSE:
        self.assertIn(probe_cls, GENERAL_PURPOSE_PROBES)
    self.assertGreater(count,
                       len(GENERAL_PURPOSE_PROBES) + len(INTERNAL_PROBES))

  def test_help(self):
    for probe_cls in self.probe_classes():
      help_text = probe_cls.help_text()
      self.assertTrue(help)
      summary = probe_cls.summary_text()
      self.assertTrue(summary)
      self.assertIn(summary, help_text)

  def test_config_parser(self):
    for probe_cls in self.probe_classes():
      config_parser = probe_cls.config_parser()
      self.assertEqual(config_parser.probe_cls, probe_cls)

  def test_basic_probe_instances(self):
    keys = set()
    names = set()
    for probe_instance in self.probe_instances():
      _ = hash(probe_instance)
      key = probe_instance.key
      self.assertIsInstance(key, tuple)
      self.assertNotIn(key, keys)
      keys.add(key)
      self.assertTrue(probe_instance.name)
      self.assertNotIn(probe_instance.name, names)
      names.add(probe_instance.name)

  def test_is_internal(self):
    for probe_instance in self.internal_probe_instances():
      self.assertTrue(probe_instance.is_internal)
    for probe_instance in self.general_purpose_probe_instances():
      self.assertFalse(probe_instance.is_internal)

  def test_is_attached(self):
    for probe_instance in self.general_purpose_probe_instances():
      self.assertFalse(probe_instance.is_attached)


if __name__ == "__main__":
  test_helper.run_pytest(__file__)
