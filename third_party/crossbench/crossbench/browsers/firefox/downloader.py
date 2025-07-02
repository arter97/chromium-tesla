# Copyright 2023 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from __future__ import annotations

import abc
import re
import urllib.parse
from typing import TYPE_CHECKING, Dict, Final, Optional, Tuple, Type, Union

from crossbench.browsers.downloader import DMGArchiveHelper, Downloader

if TYPE_CHECKING:
  from crossbench import plt
  from crossbench.path import LocalPath, RemotePathLike


_PLATFORM_NAME_LOOKUP: Final[Dict[Tuple[str, str], str]] = {
    ("win", "ia32"): "win32",
    ("win", "x64"): "win64",
    ("win", "arm64"): "win-aarch64",
    ("linux", "x64"): "linux-x86-64",
    ("linux", "ia32"): "linux-i686",
    ("macos", "x64"): "mac",
    ("macos", "arm64"): "mac"
}


class FirefoxDownloader(Downloader):
  # TODO: support nightly versions as well
  VERSION_RE: re.Pattern = re.compile(
      r"(firefox-)?(?P<version>([0-9]+(([.ab])[0-9]+){1,3}))", re.I)
  STORAGE_URL: str = "https://ftp.mozilla.org/pub/firefox/releases/"

  @classmethod
  def _get_loader_cls(
      cls, browser_platform: plt.Platform) -> Type[FirefoxDownloader]:
    if browser_platform.is_macos:
      return FirefoxDownloaderMacOS
    if browser_platform.is_linux:
      return FirefoxDownloaderLinux
    if browser_platform.is_win:
      return FirefoxDownloaderWin
    raise ValueError("Downloading Firefox is not supported "
                     f"{browser_platform.name} {browser_platform.machine}")

  @classmethod
  def is_valid_version(cls, path_or_identifier: str) -> bool:
    return bool(cls.VERSION_RE.fullmatch(path_or_identifier))

  @classmethod
  def _is_valid(cls, path_or_identifier: RemotePathLike,
                browser_platform: plt.Platform) -> bool:
    if cls.is_valid_version(str(path_or_identifier)):
      return True
    path = browser_platform.path(path_or_identifier)
    return browser_platform.exists(path) and path.suffix == cls.ARCHIVE_SUFFIX

  def __init__(self,
               version_identifier: Union[str, LocalPath],
               browser_type: str,
               platform_name: str,
               browser_platform: plt.Platform,
               cache_dir: Optional[LocalPath] = None):
    assert not browser_type
    assert not platform_name
    firefox_platform_name = _PLATFORM_NAME_LOOKUP.get(browser_platform.key)
    if not firefox_platform_name:
      raise ValueError(
          "Unsupported macOS architecture for downloading Firefox: "
          f"got={browser_platform.machine}")
    super().__init__(version_identifier, "firefox", firefox_platform_name,
                     browser_platform, cache_dir)

  def _parse_version(
      self, version_identifier: str) -> Tuple[str, Tuple[int, ...], str, bool]:
    match = self.VERSION_RE.search(version_identifier)
    assert match, f"Invalid Firefox version identifier: {version_identifier}"
    version_identifier = version_identifier = match["version"]
    requested_version = tuple(map(int, re.split(r"[.ab]",
                                                version_identifier)))[:4]
    requested_version_str = ".".join(map(str, requested_version))
    requested_exact_version = True
    assert len(requested_version) == 3
    return (version_identifier, requested_version, requested_version_str,
            requested_exact_version)

  MIN_MAC_ARM64_VERSION = (84, 0, 0, 0)

  def _version_check(self) -> None:
    major_version: int = self._requested_version[0]
    if (self._browser_platform.is_macos and self._browser_platform.is_arm64 and
        major_version < self.MIN_MAC_ARM64_VERSION[0]):
      raise ValueError(
          "Native Mac arm64/m1 Firefox version is available with v84, "
          f"but requested {major_version}.")

  def _find_archive_url(self) -> Optional[str]:
    # Quick probe for complete versions
    if self._requested_exact_version:
      return self._find_exact_archive_url(self._version_identifier)
    raise NotImplementedError("Only full-release versions supported.")

  def _find_exact_archive_url(self, version: str) -> Optional[str]:
    folder_url = f"{self.STORAGE_URL}{version}/mac/en-GB"
    return self._archive_urls(folder_url, version)[0]

  @abc.abstractmethod
  def _archive_urls(self, folder_url: str, version_str: str) -> Tuple[str, ...]:
    pass

  def _download_archive(self, archive_url: str, tmp_dir: LocalPath) -> None:
    self._browser_platform.download_to(
        archive_url, tmp_dir / f"archive.{self.ARCHIVE_SUFFIX}")
    archive_candidates = list(tmp_dir.glob("*"))
    assert len(archive_candidates) == 1, (
        f"Download tmp dir contains more than one file: {tmp_dir}"
        f"{archive_candidates}")
    candidate = archive_candidates[0]
    assert not self._archive_path.exists(), (
        f"Archive was already downloaded: {self._archive_path}")
    candidate.replace(self._archive_path)

  @abc.abstractmethod
  def _install_archive(self, archive_path: LocalPath) -> None:
    pass


class FirefoxDownloaderLinux(FirefoxDownloader):
  ARCHIVE_SUFFIX: str = ".tar.bz2"

  @classmethod
  def is_valid(cls, path_or_identifier: RemotePathLike,
               browser_platform: plt.Platform) -> bool:
    return cls._is_valid(path_or_identifier, browser_platform)

  def _extracted_path(self) -> LocalPath:
    return self._out_dir / self._requested_version_str

  def _installed_app_path(self) -> LocalPath:
    # TODO: support local vs remote
    return self._extracted_path() / "firefox-bin"

  def _archive_urls(self, folder_url: str, version_str: str) -> Tuple[str, ...]:
    return (f"{folder_url}/firefox-{version_str}.tar.bz2",)

  def _install_archive(self, archive_path: LocalPath) -> None:
    raise NotImplementedError("Missing linux support")


class FirefoxDownloaderMacOS(FirefoxDownloader):
  ARCHIVE_SUFFIX: str = ".dmg"

  @classmethod
  def is_valid(cls, path_or_identifier: RemotePathLike,
               browser_platform: plt.Platform) -> bool:
    return cls._is_valid(path_or_identifier, browser_platform)

  def _download_archive(self, archive_url: str, tmp_dir: LocalPath) -> None:
    assert self._browser_platform.is_macos
    if self._browser_platform.is_arm64 and (self._requested_version
                                            < self.MIN_MAC_ARM64_VERSION):
      raise ValueError(
          "Firefox Arm64 Apple Silicon is only available starting with "
          f"{self.MIN_MAC_ARM64_VERSION[0]}, "
          f"but requested {self._requested_version_str} is too old.")
    super()._download_archive(archive_url, tmp_dir)

  def _archive_urls(self, folder_url: str, version_str: str) -> Tuple[str, ...]:
    return (f"{folder_url}/" +
            urllib.parse.quote(f"Firefox {version_str}.dmg"),)

  def _extracted_path(self) -> LocalPath:
    # TODO: support local vs remote
    return self._installed_app_path()

  def _installed_app_path(self) -> LocalPath:
    return self._out_dir / f"Firefox {self._requested_version_str}.app"

  def _install_archive(self, archive_path: LocalPath) -> None:
    extracted_path = self._extracted_path()
    DMGArchiveHelper.extract(self.host_platform, archive_path, extracted_path)
    assert extracted_path.exists()


class FirefoxDownloaderWin(FirefoxDownloader):

  @classmethod
  def is_valid(cls, path_or_identifier: RemotePathLike,
               browser_platform: plt.Platform) -> bool:
    return False

  def _install_archive(self, archive_path: LocalPath) -> None:
    raise NotImplementedError("Missing windows support")
