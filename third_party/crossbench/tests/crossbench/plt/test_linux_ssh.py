# Copyright 2024 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from __future__ import annotations

from crossbench import plt
from tests import test_helper
from tests.crossbench.plt.helper import PosixPlatformTestCase


class LinuxSshPlatformTest(PosixPlatformTestCase):
  __test__ = True
  HOST = "host"
  PORT = 9515
  SSH_PORT = 22
  SSH_USER = "user"
  platform: plt.LinuxSshPlatform

  def setUp(self) -> None:
    super().setUp()
    self.platform = plt.LinuxSshPlatform(
        self.mock_platform,
        host=self.HOST,
        port=self.PORT,
        ssh_port=self.SSH_PORT,
        ssh_user=self.SSH_USER)

  def test_is_remote(self):
    self.assertTrue(self.platform.is_remote)

  def test_is_linux(self):
    self.assertTrue(self.platform.is_linux)

  def test_name(self):
    self.assertEqual(self.platform.name, "linux_ssh")

  def test_host(self):
    self.assertEqual(self.platform.host, self.HOST)

  def test_port(self):
    self.assertEqual(self.platform.port, self.PORT)

  def test_host_platform(self):
    self.assertIs(self.platform.host_platform, self.mock_platform)

  def test_version(self):
    self.expect_sh(
        "ssh",
        "-p",
        f"{self.SSH_PORT}",
        f"{self.SSH_USER}@{self.HOST}",
        "uname -r",
        result="999")
    self.assertEqual(self.platform.version, "999")
    # Subsequent calls are cached.
    self.assertEqual(self.platform.version, "999")


if __name__ == "__main__":
  test_helper.run_pytest(__file__)
