# Copyright 2022 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from crossbench.benchmarks.jetstream import (jetstream_2_0, jetstream_2_1,
                                             jetstream_2_2)
from tests import test_helper
from tests.crossbench.benchmarks import jetstream_helper


class JetStream20TestCase(jetstream_helper.JetStream2BaseTestCase):

  @property
  def benchmark_cls(self):
    return jetstream_2_0.JetStream20Benchmark

  @property
  def story_cls(self):
    return jetstream_2_0.JetStream20Story

  @property
  def probe_cls(self):
    return jetstream_2_0.JetStream20Probe

  @property
  def name(self):
    return "jetstream_2.0"


class JetStream21TestCase(jetstream_helper.JetStream2BaseTestCase):

  @property
  def benchmark_cls(self):
    return jetstream_2_1.JetStream21Benchmark

  @property
  def story_cls(self):
    return jetstream_2_1.JetStream21Story

  @property
  def probe_cls(self):
    return jetstream_2_1.JetStream21Probe

  @property
  def name(self):
    return "jetstream_2.1"


class JetStream22TestCase(jetstream_helper.JetStream2BaseTestCase):

  @property
  def benchmark_cls(self):
    return jetstream_2_2.JetStream22Benchmark

  @property
  def story_cls(self):
    return jetstream_2_2.JetStream22Story

  @property
  def probe_cls(self):
    return jetstream_2_2.JetStream22Probe

  @property
  def name(self):
    return "jetstream_2.2"


if __name__ == "__main__":
  test_helper.run_pytest(__file__)
