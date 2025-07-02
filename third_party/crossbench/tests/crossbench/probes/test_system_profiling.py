# Copyright 2024 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import pathlib
import unittest

from crossbench.probes.profiling.system_profiling import (
    CallGraphMode, TargetMode, generate_simpleperf_command_line)
from tests import test_helper


class TestSystemProfilingProbe(unittest.TestCase):

  def test_simpleperf_command_line_with_tid(self):
    output_path = pathlib.Path("simpleperf.perf.data")
    self.assertListEqual(
        generate_simpleperf_command_line(
            target=TargetMode.RENDERER_MAIN_ONLY,
            app_name="com.android.chrome",
            renderer_pid=1234,
            renderer_main_tid=5678,
            call_graph_mode=CallGraphMode.DWARF,
            frequency=None,
            count=None,
            cpus=None,
            events=None,
            grouped_events=None,
            add_counters=None,
            output_path=output_path), [
                "simpleperf", "record", "-t", "5678", "--post-unwind=yes", "-o",
                output_path
            ])

  def test_simpleperf_command_line_with_pid(self):
    output_path = pathlib.Path("simpleperf.perf.data")
    self.assertListEqual(
        generate_simpleperf_command_line(
            target=TargetMode.RENDERER_PROCESS_ONLY,
            app_name="com.android.chrome",
            renderer_pid=1234,
            renderer_main_tid=5678,
            call_graph_mode=CallGraphMode.DWARF,
            frequency=None,
            count=None,
            cpus=None,
            events=None,
            grouped_events=None,
            add_counters=None,
            output_path=output_path), [
                "simpleperf", "record", "-p", "1234", "--post-unwind=yes", "-o",
                output_path
            ])

  def test_simpleperf_command_line_with_app(self):
    output_path = pathlib.Path("simpleperf.perf.data")
    self.assertListEqual(
        generate_simpleperf_command_line(
            target=TargetMode.BROWSER_APP_ONLY,
            app_name="com.chrome.beta",
            renderer_pid=None,
            renderer_main_tid=None,
            call_graph_mode=CallGraphMode.DWARF,
            frequency=None,
            count=None,
            cpus=None,
            events=None,
            grouped_events=None,
            add_counters=None,
            output_path=output_path), [
                "simpleperf", "record", "--app", "com.chrome.beta",
                "--post-unwind=yes", "-o", output_path
            ])

  def test_simpleperf_command_line_systemwide(self):
    output_path = pathlib.Path("simpleperf.perf.data")
    self.assertListEqual(
        generate_simpleperf_command_line(
            target=TargetMode.SYSTEM_WIDE,
            app_name="org.chromium.chrome",
            renderer_pid=None,
            renderer_main_tid=None,
            call_graph_mode=CallGraphMode.DWARF,
            frequency=None,
            count=None,
            cpus=None,
            events=None,
            grouped_events=None,
            add_counters=None,
            output_path=output_path),
        ["simpleperf", "record", "-a", "--post-unwind=yes", "-o", output_path])

  def test_simpleperf_command_line_with_frequency(self):
    output_path = pathlib.Path("simpleperf.perf.data")
    self.assertListEqual(
        generate_simpleperf_command_line(
            target=TargetMode.SYSTEM_WIDE,
            app_name="org.chromium.chrome",
            renderer_pid=None,
            renderer_main_tid=None,
            call_graph_mode=CallGraphMode.FRAME_POINTER,
            frequency=1234,
            count=None,
            cpus=None,
            events=None,
            grouped_events=None,
            add_counters=None,
            output_path=output_path), [
                "simpleperf", "record", "-a", "--call-graph", "fp", "-f",
                "1234", "-o", output_path
            ])

  def test_simpleperf_command_line_with_count(self):
    output_path = pathlib.Path("simpleperf.perf.data")
    self.assertListEqual(
        generate_simpleperf_command_line(
            target=TargetMode.SYSTEM_WIDE,
            app_name="org.chromium.chrome",
            renderer_pid=None,
            renderer_main_tid=None,
            call_graph_mode=CallGraphMode.FRAME_POINTER,
            frequency=None,
            count=5,
            cpus=None,
            events=None,
            grouped_events=None,
            add_counters=None,
            output_path=output_path), [
                "simpleperf", "record", "-a", "--call-graph", "fp", "-c", "5",
                "-o", output_path
            ])

  def test_simpleperf_command_line_with_cpu(self):
    output_path = pathlib.Path("simpleperf.perf.data")
    self.assertListEqual(
        generate_simpleperf_command_line(
            target=TargetMode.SYSTEM_WIDE,
            app_name="org.chromium.chrome",
            renderer_pid=None,
            renderer_main_tid=None,
            call_graph_mode=CallGraphMode.FRAME_POINTER,
            frequency=None,
            count=None,
            cpus=[0, 1, 2],
            events=None,
            grouped_events=None,
            add_counters=None,
            output_path=output_path), [
                "simpleperf", "record", "-a", "--call-graph", "fp", "--cpu",
                "0,1,2", "-o", output_path
            ])

  def test_simpleperf_command_line_with_events(self):
    output_path = pathlib.Path("simpleperf.perf.data")
    self.assertListEqual(
        generate_simpleperf_command_line(
            target=TargetMode.SYSTEM_WIDE,
            app_name="org.chromium.chrome",
            renderer_pid=None,
            renderer_main_tid=None,
            call_graph_mode=CallGraphMode.NO_CALL_GRAPH,
            frequency=1234,
            count=5,
            cpus=None,
            events=["cpu-cycles", "instructions"],
            grouped_events=None,
            add_counters=None,
            output_path=output_path), [
                "simpleperf", "record", "-a", "-f", "1234", "-c", "5", "-e",
                "cpu-cycles,instructions", "-o", output_path
            ])

  def test_simpleperf_command_line_with_grouped_events(self):
    output_path = pathlib.Path("simpleperf.perf.data")
    self.assertListEqual(
        generate_simpleperf_command_line(
            target=TargetMode.SYSTEM_WIDE,
            app_name="org.chromium.chrome",
            renderer_pid=None,
            renderer_main_tid=None,
            call_graph_mode=CallGraphMode.NO_CALL_GRAPH,
            frequency=1234,
            count=5,
            cpus=None,
            events=None,
            grouped_events=["cpu-cycles", "instructions"],
            add_counters=None,
            output_path=output_path), [
                "simpleperf", "record", "-a", "-f", "1234", "-c", "5",
                "--group", "cpu-cycles,instructions", "-o", output_path
            ])

  def test_simpleperf_command_line_with_add_counters(self):
    output_path = pathlib.Path("simpleperf.perf.data")
    self.assertListEqual(
        generate_simpleperf_command_line(
            target=TargetMode.SYSTEM_WIDE,
            app_name="org.chromium.chrome",
            renderer_pid=None,
            renderer_main_tid=None,
            call_graph_mode=CallGraphMode.NO_CALL_GRAPH,
            frequency=1234,
            count=5,
            cpus=None,
            events=["sched:sched_switch"],
            grouped_events=None,
            add_counters=["cpu-cycles", "instructions"],
            output_path=output_path), [
                "simpleperf", "record", "-a", "-f", "1234", "-c", "5", "-e",
                "sched:sched_switch", "--add-counter",
                "cpu-cycles,instructions", "--no-inherit", "-o", output_path
            ])


if __name__ == "__main__":
  test_helper.run_pytest(__file__)
