# Copyright 2024 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from __future__ import annotations

from crossbench import plt

from tests import test_helper
from tests.crossbench.plt.test_linux_ssh import LinuxSshPlatformTest


class ChromeOsSshPlatformTest(LinuxSshPlatformTest):
  SSH_USER = "chronos"
  platform: plt.ChromeOsSshPlatform

  def setUp(self) -> None:
    super().setUp()
    self.platform = plt.ChromeOsSshPlatform(
        self.mock_platform,
        host=self.HOST,
        port=self.PORT,
        ssh_port=self.SSH_PORT,
        ssh_user=self.SSH_USER)

  def test_name(self):
    self.assertEqual(self.platform.name, "chromeos_ssh")


if __name__ == "__main__":
  test_helper.run_pytest(__file__)
