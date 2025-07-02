# Copyright 2023 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from __future__ import annotations

import pathlib
import sys
from typing import Union

import pytest


def root_dir() -> pathlib.Path:
  # Input:  /foo/bar/crossbench/tests/test_helper.py
  # Output: /foo/bar/crossbench/
  return pathlib.Path(__file__).parents[1]


def config_dir() -> pathlib.Path:
  return root_dir() / "config"


def run_pytest(path: Union[str, pathlib.Path], *args):
  extra_args = [*args, *sys.argv[1:]]
  # Run tests single-threaded by default when running the test file directly.
  if "-n" not in extra_args:
    extra_args.extend(["-n", "1"])
  sys.exit(pytest.main([str(path), *extra_args]))
