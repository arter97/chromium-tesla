# Copyright 2023 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import abc
import pathlib
from unittest import mock

from crossbench.browsers.chrome.downloader import (ChromeDownloader,
                                                   ChromeDownloaderLinux,
                                                   ChromeDownloaderMacOS,
                                                   ChromeDownloaderWin)
from tests import test_helper
from tests.crossbench.mock_helper import BaseCrossbenchTestCase


class AbstractChromeDownloaderTestCase(
    BaseCrossbenchTestCase, metaclass=abc.ABCMeta):
  __test__ = False

  def setUp(self) -> None:
    super().setUp()
    self.platform = mock.Mock(
        is_remote=False,
        is_linux=False,
        is_macos=False,
        sh_results=[],
        path=pathlib.Path)
    self.platform.search_app = lambda x: x
    self.platform.which = pathlib.Path
    self.platform.host_platform = self.platform
    self.cache_dir = pathlib.Path("crossbench/binary_cache")
    self.fs.create_dir(self.cache_dir)

  def test_wrong_versions(self) -> None:
    with self.assertRaises(ValueError):
      ChromeDownloader.load("", self.platform, self.cache_dir)
    with self.assertRaises(ValueError):
      ChromeDownloader.load("M", self.platform, self.cache_dir)
    with self.assertRaises(ValueError):
      ChromeDownloader.load("M-100", self.platform, self.cache_dir)
    with self.assertRaises(ValueError):
      ChromeDownloader.load("M100.1.2.3.4.5", self.platform, self.cache_dir)
    with self.assertRaises(ValueError):
      ChromeDownloader.load("100.1.2.3.4.5", self.platform, self.cache_dir)

  def test_empty_path(self) -> None:
    with self.assertRaises(ValueError):
      ChromeDownloader.load(
          pathlib.Path("custom"), self.platform, self.cache_dir)

  def test_load_valid_non_googler(self) -> None:
    self.platform.which = lambda x: None
    with self.assertRaises(ValueError):
      ChromeDownloader.load("chrome-111.0.5563.110", self.platform,
                            self.cache_dir)

  def test_is_valid_strings(self) -> None:
    self.assertFalse(ChromeDownloader.is_valid("", self.platform))
    self.assertFalse(ChromeDownloader.is_valid("mM45", self.platform))
    self.assertFalse(ChromeDownloader.is_valid("M4", self.platform))
    self.assertFalse(ChromeDownloader.is_valid("M1234", self.platform))
    self.assertFalse(ChromeDownloader.is_valid("M123.4", self.platform))
    self.assertFalse(ChromeDownloader.is_valid("M123 ", self.platform))
    self.assertFalse(ChromeDownloader.is_valid("M12 ", self.platform))
    self.assertFalse(ChromeDownloader.is_valid("145", self.platform))
    self.assertFalse(ChromeDownloader.is_valid("45", self.platform))
    self.assertFalse(ChromeDownloader.is_valid("i145", self.platform))
    self.assertFalse(ChromeDownloader.is_valid("i45", self.platform))
    self.assertFalse(ChromeDownloader.is_valid("chr-i145", self.platform))
    self.assertFalse(ChromeDownloader.is_valid("chr-i45", self.platform))

    self.assertTrue(ChromeDownloader.is_valid("M45", self.platform))
    self.assertTrue(ChromeDownloader.is_valid("m45", self.platform))
    self.assertTrue(ChromeDownloader.is_valid("chrome-m45", self.platform))
    self.assertTrue(ChromeDownloader.is_valid("chrome-45", self.platform))
    self.assertTrue(ChromeDownloader.is_valid("chr-m45", self.platform))
    self.assertTrue(ChromeDownloader.is_valid("chr-45", self.platform))

    self.assertTrue(ChromeDownloader.is_valid("M100", self.platform))
    self.assertTrue(ChromeDownloader.is_valid("m100", self.platform))
    self.assertTrue(ChromeDownloader.is_valid("chrome-m100", self.platform))
    self.assertTrue(ChromeDownloader.is_valid("chrome-100", self.platform))
    self.assertTrue(ChromeDownloader.is_valid("chr-m100", self.platform))
    self.assertTrue(ChromeDownloader.is_valid("chr-100", self.platform))
    self.assertFalse(
        ChromeDownloader.is_valid("Google Chrome m100", self.platform))
    self.assertFalse(
        ChromeDownloader.is_valid("Google Chrome 100", self.platform))

    self.assertFalse(
        ChromeDownloader.is_valid("M100.1.2.123.9999", self.platform))
    self.assertTrue(ChromeDownloader.is_valid("M111.0.5563.110", self.platform))
    self.assertTrue(
        ChromeDownloader.is_valid("Google Chrome M111.0.5563.110",
                                  self.platform))
    self.assertTrue(
        ChromeDownloader.is_valid("Google Chrome Canary M111.0.5563.110",
                                  self.platform))
    self.assertTrue(ChromeDownloader.is_valid("111.0.5563.110", self.platform))
    self.assertTrue(
        ChromeDownloader.is_valid("Google Chrome 111.0.5563.110",
                                  self.platform))
    self.assertTrue(
        ChromeDownloader.is_valid("Google Chrome Canary111.0.5563.110",
                                  self.platform))
    self.assertTrue(
        ChromeDownloader.is_valid("chrome-11.0.5563.110", self.platform))
    self.assertTrue(
        ChromeDownloader.is_valid("chrome-M11.0.5563.110", self.platform))
    self.assertTrue(
        ChromeDownloader.is_valid("chr-11.0.5563.110", self.platform))
    self.assertTrue(
        ChromeDownloader.is_valid("chrome-111.0.5563.110", self.platform))
    self.assertTrue(
        ChromeDownloader.is_valid("chr-111.0.5563.110", self.platform))

  def test_is_valid_path(self) -> None:
    self.assertFalse(
        ChromeDownloader.is_valid(pathlib.Path("custom"), self.platform))
    path = pathlib.Path("download/archive.foo")
    self.fs.create_file(path)
    self.assertFalse(ChromeDownloader.is_valid(path, self.platform))


class BasicChromeDownloaderTestCaseLinux(AbstractChromeDownloaderTestCase):
  __test__ = True

  def setUp(self) -> None:
    super().setUp()
    self.platform.is_linux = True

  def test_is_valid_archive(self) -> None:
    path = pathlib.Path("download/archive.rpm")
    self.fs.create_file(path)
    self.assertTrue(ChromeDownloader.is_valid(path, self.platform))
    self.assertTrue(ChromeDownloaderLinux.is_valid(path, self.platform))
    self.assertFalse(ChromeDownloaderMacOS.is_valid(path, self.platform))
    self.assertFalse(ChromeDownloaderWin.is_valid(path, self.platform))

class BasicChromeDownloaderTestCaseMacOS(AbstractChromeDownloaderTestCase):
  __test__ = True

  def setUp(self) -> None:
    super().setUp()
    self.platform.is_macos = True

  def test_is_valid_archive(self) -> None:
    path = pathlib.Path("download/archive.dmg")
    self.fs.create_file(path)
    self.assertTrue(ChromeDownloader.is_valid(path, self.platform))
    self.assertTrue(ChromeDownloaderMacOS.is_valid(path, self.platform))
    self.assertFalse(ChromeDownloaderLinux.is_valid(path, self.platform))
    self.assertFalse(ChromeDownloaderWin.is_valid(path, self.platform))


if __name__ == "__main__":
  test_helper.run_pytest(__file__)
