# Copyright 2023 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from __future__ import annotations

import logging
import pathlib
import shutil
from typing import Union

import pytest

from crossbench import compat, plt
from crossbench.browsers.chrome.downloader import ChromeDownloader
from crossbench.browsers.chrome.webdriver import ChromeWebDriver
from crossbench.browsers.chromium.webdriver import (ChromeDriverFinder,
                                                    DriverNotFoundError)
from tests import test_helper

if plt.PLATFORM.which("gsutil") is None and plt.PLATFORM.is_macos:
  pytest.skip(
      "Missing required 'gsutil', skipping test.", allow_module_level=True)

try:
  plt.PLATFORM.sh_stdout(
      "gsutil", "ls", "gs://chrome-signed/desktop-5c0tCh/111.0.5563.19/linux64")
except plt.SubprocessError as e:
  logging.info("Could not access chrome bucket with gsutil: %s", e)
  if "does not have storage.objects.list access" in str(e):
    pytest.skip(
        "gsutil likely has no access to gs://chrome-signed/desktop-5c0tCh",
        allow_module_level=True)
  raise e


def _load_and_check_version(output_dir: pathlib.Path,
                            archive_dir: pathlib.Path,
                            version_or_archive: Union[str, pathlib.Path],
                            version_str: str,
                            expect_archive: bool = True) -> pathlib.Path:
  app_path: pathlib.Path = ChromeDownloader.load(version_or_archive,
                                                 plt.PLATFORM, output_dir)
  assert compat.is_relative_to(app_path, output_dir)
  assert archive_dir.exists()
  assert app_path.exists()
  if plt.PLATFORM.is_macos:
    assert set(output_dir.iterdir()) == {app_path, archive_dir}
  assert version_str in plt.PLATFORM.app_version(app_path)
  archives = list(archive_dir.iterdir())
  if expect_archive:
    assert len(archives) == 1
  else:
    assert not archives
  assert app_path.exists()
  chrome = ChromeWebDriver("test-chrome", app_path, platform=plt.PLATFORM)
  assert version_str in chrome.version
  _load_and_check_chromedriver(output_dir, chrome)
  return app_path


def _load_and_check_chromedriver(output_dir, chrome: ChromeWebDriver) -> None:
  driver_dir = output_dir / "chromedriver-binaries"
  driver_dir.mkdir()
  finder = ChromeDriverFinder(chrome, cache_dir=driver_dir)
  assert not list(driver_dir.iterdir())
  with pytest.raises(DriverNotFoundError):
    finder.find_local_build()
  driver_path: pathlib.Path = finder.download()
  assert list(driver_dir.iterdir()) == [driver_path]
  assert driver_path.is_file()
  # Downloading again should use the cache-version
  driver_path: pathlib.Path = finder.download()
  assert list(driver_dir.iterdir()) == [driver_path]
  assert driver_path.is_file()
  # Restore output dir state.
  driver_path.unlink()
  driver_dir.rmdir()


@pytest.mark.skipif(
    not plt.PLATFORM.is_linux, reason="No canary versions on linux.")
def test_download_pre_115_canary(output_dir, archive_dir) -> None:
  assert not list(output_dir.iterdir())
  _load_and_check_version(
      output_dir,
      archive_dir,
      "chrome-114.0.5735.2",
      "114.0.5735.2",
      expect_archive=False)


def test_download_major_version_milestone(output_dir, archive_dir) -> None:
  assert not list(output_dir.iterdir())
  _load_and_check_version(
      output_dir, archive_dir, "chrome-M111", "111", expect_archive=False)

  # Re-downloading should reuse the extracted app.
  app_path = _load_and_check_version(
      output_dir, archive_dir, "chrome-M111", "111", expect_archive=False)

  # Delete the extracted app and reload, can't reuse the cached archive since
  # we're requesting only a milestone that could have been updated
  # in the meantime.
  if plt.PLATFORM.is_macos:
    shutil.rmtree(app_path)
  else:
    shutil.rmtree(output_dir / "M111")
  assert not app_path.exists()
  _load_and_check_version(
      output_dir, archive_dir, "chrome-M111", "111", expect_archive=False)


def test_download_major_version_chrome_for_testing(output_dir,
                                                   archive_dir) -> None:
  # Post M114 we're relying on the new chrome-for-testing download
  assert not list(output_dir.iterdir())
  _load_and_check_version(
      output_dir, archive_dir, "chrome-M115", "115", expect_archive=False)

  # Re-downloading should reuse the extracted app.
  app_path = _load_and_check_version(
      output_dir, archive_dir, "chrome-M115", "115", expect_archive=False)

  # Delete the extracted app and reload, can't reuse the cached archive since
  # we're requesting only a milestone that could have been updated
  # in the meantime.
  if plt.PLATFORM.is_macos:
    shutil.rmtree(app_path)
  else:
    shutil.rmtree(output_dir / "M115")
  assert not app_path.exists()
  _load_and_check_version(
      output_dir, archive_dir, "chrome-M115", "115", expect_archive=False)


def test_download_specific_version_pre_115_stable(output_dir,
                                                  archive_dir) -> None:
  assert not list(output_dir.iterdir())
  version_str = "111.0.5563.146"
  _load_and_check_version(output_dir, archive_dir, f"chrome-{version_str}",
                          version_str)

  # Re-downloading should work as well and hit the extracted app.
  app_path = _load_and_check_version(output_dir, archive_dir,
                                     f"chrome-{version_str}", version_str)

  # Delete the extracted app and reload, should reuse the cached archive.
  if plt.PLATFORM.is_macos:
    shutil.rmtree(app_path)
  else:
    shutil.rmtree(output_dir / version_str)
  assert not app_path.exists()
  app_path = _load_and_check_version(output_dir, archive_dir,
                                     f"chrome-{version_str}", version_str)

  # Delete app and install from archive.
  if plt.PLATFORM.is_macos:
    shutil.rmtree(app_path)
  else:
    shutil.rmtree(output_dir / version_str)
  assert not app_path.exists()
  archives = list(archive_dir.iterdir())
  assert len(archives) == 1
  archive = archives[0]
  app_path = _load_and_check_version(output_dir, archive_dir, archive,
                                     version_str)
  assert list(archive_dir.iterdir()) == [archive]


@pytest.mark.skipif(
    plt.PLATFORM.is_macos and plt.PLATFORM.is_arm64,
    reason="Old versions only supported on intel machines.")
def test_download_old_major_version(output_dir, archive_dir) -> None:
  assert not list(output_dir.iterdir())
  _load_and_check_version(
      output_dir, archive_dir, "chrome-M68", "68", expect_archive=False)


if __name__ == "__main__":
  test_helper.run_pytest(__file__)
