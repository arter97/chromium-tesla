# Copyright 2023 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from __future__ import annotations

import re
import sys
import unicodedata
from typing import Final

from crossbench.plt.android_adb import AndroidAdbPlatform, adb_devices
from crossbench.plt.arch import MachineArch
from crossbench.plt.base import Platform, SubprocessError, TupleCmdArgsT
from crossbench.plt.ios import ios_devices
from crossbench.plt.linux import LinuxPlatform
from crossbench.plt.linux_ssh import LinuxSshPlatform
from crossbench.plt.chromeos_ssh import ChromeOsSshPlatform
from crossbench.plt.macos import MacOSPlatform
from crossbench.plt.posix import PosixPlatform
from crossbench.plt.win import WinPlatform


def _get_default() -> Platform:
  if sys.platform == "linux":
    return LinuxPlatform()
  if sys.platform == "darwin":
    return MacOSPlatform()
  if sys.platform == "win32":
    return WinPlatform()
  raise NotImplementedError("Unsupported Platform")


PLATFORM: Final[Platform] = _get_default()

_UNSAFE_FILENAME_CHARS_RE = re.compile(r"[^a-zA-Z0-9+\-_.]+")


def safe_filename(name: str) -> str:
  normalized_name = unicodedata.normalize("NFKD", name)
  ascii_name = normalized_name.encode("ascii", "ignore").decode("ascii")
  return _UNSAFE_FILENAME_CHARS_RE.sub("_", ascii_name)
