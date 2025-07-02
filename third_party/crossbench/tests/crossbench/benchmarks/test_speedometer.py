# Copyright 2022 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import datetime as dt
import json
from dataclasses import dataclass

from crossbench.benchmarks.speedometer.speedometer_2_0 import (
    Speedometer20Benchmark, Speedometer20Probe, Speedometer20Story)
from crossbench.benchmarks.speedometer.speedometer_2_1 import (
    Speedometer21Benchmark, Speedometer21Probe, Speedometer21Story)
from crossbench.benchmarks.speedometer.speedometer_3_0 import (
    MeasurementMethod, Speedometer30Benchmark, Speedometer30Probe,
    Speedometer30Story)
from crossbench.browsers.viewport import Viewport
from tests import test_helper
from tests.crossbench.benchmarks.speedometer_helper import (
    Speedometer2BaseTestCase, SpeedometerBaseTestCase)


class Speedometer20TestCase(Speedometer2BaseTestCase):

  @property
  def benchmark_cls(self):
    return Speedometer20Benchmark

  @property
  def story_cls(self):
    return Speedometer20Story

  @property
  def probe_cls(self):
    return Speedometer20Probe

  @property
  def name(self):
    return "speedometer_2.0"

  def test_default_all(self):
    default_story_names = [
        story.name for story in self.story_cls.default(separate=True)
    ]
    all_story_names = [
        story.name for story in self.story_cls.all(separate=True)
    ]
    self.assertListEqual(default_story_names, all_story_names)


class Speedometer21TestCase(Speedometer2BaseTestCase):

  @property
  def benchmark_cls(self):
    return Speedometer21Benchmark

  @property
  def story_cls(self):
    return Speedometer21Story

  @property
  def probe_cls(self):
    return Speedometer21Probe

  @property
  def name(self):
    return "speedometer_2.1"


class Speedometer30TestCase(SpeedometerBaseTestCase):

  @property
  def benchmark_cls(self):
    return Speedometer30Benchmark

  @property
  def story_cls(self):
    return Speedometer30Story

  @property
  def probe_cls(self):
    return Speedometer30Probe

  @property
  def name(self):
    return "speedometer_3.0"

  @property
  def name_all(self):
    return "speedometer_3.0_all"

  @dataclass
  class Namespace(SpeedometerBaseTestCase.Namespace):
    sync_wait = dt.timedelta(0)
    sync_warmup = dt.timedelta(0)
    measurement_method = MeasurementMethod.RAF
    story_viewport = None
    shuffle_seed = None
    detailed_metrics = False

  EXAMPLE_STORY_DATA = {}

  def _generate_s3_metrics(self, name, values):
    return {
        "children": [],
        "delta": 0,
        "geomean": 39.20000000298023,
        "max": 39.20000000298023,
        "mean": 39.20000000298023,
        "min": 39.20000000298023,
        "name": name,
        "percentDelta": 0,
        "sum": 39.20000000298023,
        "unit": "ms",
        "values": values
    }

  def _generate_test_probe_results(self, iterations, story):
    values = [21.3] * iterations
    probe_result = {
        "Geomean": self._generate_s3_metrics("Geomean", values),
        "Score": self._generate_s3_metrics("Score", values),
    }
    for iteration in range(iterations):
      key = f"Iteration-{iteration}-Total"
      probe_result[key] = self._generate_s3_metrics(key, values)

    for substory_name in story.substories:
      probe_result[substory_name] = self._generate_s3_metrics(
          substory_name, values)
    return probe_result

  def test_run_combined(self):
    self._run_combined(["TodoMVC-JavaScript-ES5", "TodoMVC-Backbone"])

  def test_run_separate(self):
    self._run_separate(["TodoMVC-JavaScript-ES5", "TodoMVC-Backbone"])

  def test_s3_probe_results(self):
    story_names = ("TodoMVC-JavaScript-ES5", "TodoMVC-Backbone")
    self.browsers = [self.browsers[0]]
    runner = self._test_run(
        story_names=story_names, separate=False, repetitions=2)
    self.assertEqual(len(runner.runs), 2)
    run_1 = runner.runs[0]
    run_2 = runner.runs[1]
    probe_file = f"{self.probe_cls.NAME}.json"
    with (run_1.out_dir / probe_file).open() as f:
      data_1 = json.load(f)
    with (run_2.out_dir / probe_file).open() as f:
      data_2 = json.load(f)
    keys_1 = tuple(data_1.keys())
    keys_2 = tuple(data_2.keys())
    self.assertTupleEqual(keys_1, keys_2)
    # Make sure the aggregate metrics are at the end
    expected_keys = story_names + ("Iteration-0-Total", "Iteration-1-Total",
                                   "Geomean", "Score")
    self.assertTupleEqual(keys_1, expected_keys)

    with (runner.story_groups[0].path / probe_file).open() as f:
      stories_data = json.load(f)
    self.assertTupleEqual(tuple(stories_data.keys()), expected_keys)

  def test_measurement_method_kwargs(self):
    args = self.Namespace()
    benchmark = self.benchmark_cls.from_cli_args(args)
    for story in benchmark.stories:
      assert isinstance(story, self.story_cls)
      self.assertEqual(story.measurement_method, MeasurementMethod.RAF)

    args.measurement_method = MeasurementMethod.TIMER
    benchmark = self.benchmark_cls.from_cli_args(args)
    for story in benchmark.stories:
      assert isinstance(story, self.story_cls)
      self.assertEqual(story.measurement_method, MeasurementMethod.TIMER)
      self.assertDictEqual(story.url_params, {"measurementMethod": "timer"})

  def test_sync_wait_kwargs(self):
    args = self.Namespace()
    benchmark = self.benchmark_cls.from_cli_args(args)
    for story in benchmark.stories:
      assert isinstance(story, self.story_cls)
      self.assertEqual(story.sync_wait, dt.timedelta(0))

    with self.assertRaises(argparse.ArgumentTypeError):
      args.sync_wait = dt.timedelta(seconds=-123.4)
      self.benchmark_cls.from_cli_args(args)

    args.sync_wait = dt.timedelta(seconds=123.4)
    benchmark = self.benchmark_cls.from_cli_args(args)
    for story in benchmark.stories:
      assert isinstance(story, self.story_cls)
      self.assertEqual(story.sync_wait, dt.timedelta(seconds=123.4))
      self.assertDictEqual(story.url_params, {"waitBeforeSync": "123400"})

  def test_sync_warmup_kwargs(self):
    args = self.Namespace()
    benchmark = self.benchmark_cls.from_cli_args(args)
    for story in benchmark.stories:
      assert isinstance(story, self.story_cls)
      self.assertEqual(story.sync_warmup, dt.timedelta(0))

    with self.assertRaises(argparse.ArgumentTypeError):
      args.sync_warmup = dt.timedelta(seconds=-123.4)
      self.benchmark_cls.from_cli_args(args)

    args.sync_warmup = dt.timedelta(seconds=123.4)
    benchmark = self.benchmark_cls.from_cli_args(args)
    for story in benchmark.stories:
      assert isinstance(story, self.story_cls)
      self.assertEqual(story.sync_warmup, dt.timedelta(seconds=123.4))
      self.assertDictEqual(story.url_params, {"warmupBeforeSync": "123400"})

  def test_viewport_kwargs(self):
    args = self.Namespace()
    benchmark = self.benchmark_cls.from_cli_args(args)
    for story in benchmark.stories:
      assert isinstance(story, self.story_cls)
      self.assertEqual(story.viewport, None)

    with self.assertRaises(argparse.ArgumentTypeError):
      args.story_viewport = Viewport.FULLSCREEN
      self.benchmark_cls.from_cli_args(args)

    args.story_viewport = Viewport(999, 888)
    benchmark = self.benchmark_cls.from_cli_args(args)
    for story in benchmark.stories:
      assert isinstance(story, self.story_cls)
      self.assertEqual(story.viewport, Viewport(999, 888))
      self.assertDictEqual(story.url_params, {"viewport": "999x888"})

  def test_shuffle_seed_kwargs(self):
    args = self.Namespace()
    benchmark = self.benchmark_cls.from_cli_args(args)
    for story in benchmark.stories:
      assert isinstance(story, self.story_cls)
      self.assertEqual(story.shuffle_seed, None)

    with self.assertRaises(argparse.ArgumentTypeError):
      args.shuffle_seed = "some invalid value"
      self.benchmark_cls.from_cli_args(args)

    args.shuffle_seed = 1234
    benchmark = self.benchmark_cls.from_cli_args(args)
    for story in benchmark.stories:
      assert isinstance(story, self.story_cls)
      self.assertEqual(story.shuffle_seed, 1234)
      self.assertDictEqual(story.url_params, {"shuffleSeed": "1234"})

#  Don't expose abstract BaseTestCase to test runner
del SpeedometerBaseTestCase
del Speedometer2BaseTestCase

if __name__ == "__main__":
  test_helper.run_pytest(__file__)
