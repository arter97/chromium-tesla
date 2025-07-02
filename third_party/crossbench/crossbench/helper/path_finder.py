# Copyright 2024 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from __future__ import annotations

import abc
from typing import TYPE_CHECKING, Iterator, Optional, Tuple

from crossbench import path as pth

if TYPE_CHECKING:
  from crossbench.plt.base import Platform


class BaseDirFinder(abc.ABC):

  def __init__(self, platform: Platform, candidates: Tuple[pth.RemotePath,
                                                           ...]) -> None:
    self._platform = platform
    self._candidates = candidates
    self._path: Optional[pth.RemotePath] = self._find_path()
    if self._path:
      assert self._is_valid_path(self._path)

  @property
  def candidates(self) -> Tuple[pth.RemotePath, ...]:
    return self._candidates

  @property
  def platform(self) -> Platform:
    return self._platform

  @property
  def path(self) -> Optional[pth.RemotePath]:
    return self._path

  def _find_path(self) -> Optional[pth.RemotePath]:
    # Try potential build location
    for candidate_dir in self._candidates:
      if self._is_valid_path(candidate_dir):
        return candidate_dir
    return None

  @abc.abstractmethod
  def _is_valid_path(self, candidate: pth.RemotePath) -> bool:
    pass


def default_chromium_candidates(
    platform: Platform) -> Tuple[pth.RemotePath, ...]:
  """Returns a generous list of potential locations of a chromium checkout."""
  candidates = []
  if chromium_src := platform.environ.get("CHROMIUM_SRC"):
    candidates.append(platform.path(chromium_src))
  if platform.is_local:
    # Assume that crossbench is in chrome's third_party dir:
    # Input:   chromium/src/third_party/crossbench/crossbench/probes/helper.py
    # Output:  chromium/src
    candidates.append(pth.LocalPath(__file__).parents[4])
  home_dir = platform.home()
  candidates += [
      # Guessing default locations
      home_dir / "Documents/chromium/src",
      home_dir / "chromium/src",
      platform.path("C:/src/chromium/src"),
      home_dir / "Documents/chrome/src",
      home_dir / "chrome/src",
      platform.path("C:/src/chrome/src"),
  ]
  return tuple(candidates)


def is_chromium_checkout_dir(platform: Platform,
                             dir_path: pth.RemotePath) -> bool:
  return (platform.is_dir(dir_path / "v8") and
          platform.is_dir(dir_path / "chrome") and
          platform.is_dir(dir_path / ".git"))


class ChromiumCheckoutFinder(BaseDirFinder):
  """Finds a chromium src checkout at either given locations or at
  some preset known checkout locations."""

  def __init__(
      self,
      platform: Platform,
      candidates: Tuple[pth.RemotePath, ...] = tuple()) -> None:
    candidates += default_chromium_candidates(platform)
    super().__init__(platform, candidates)

  def _is_valid_path(self, candidate: pth.RemotePath) -> bool:
    return is_chromium_checkout_dir(self.platform, candidate)


class ChromiumBuildBinaryFinder(BaseDirFinder):
  """Finds a custom-built binary in either a given out/BUILD dir or
  tries to find it in build dirs in common known chromium checkout locations."""

  def __init__(
      self,
      platform: Platform,
      binary_name: str,
      candidates: Tuple[pth.RemotePath, ...] = tuple()) -> None:
    self._binary_name = binary_name
    super().__init__(platform, candidates)

  @property
  def binary_name(self) -> str:
    return self._binary_name

  def _iterate_candidate_bin_paths(self) -> Iterator[pth.RemotePath]:
    for candidate_dir in self._candidates:
      yield candidate_dir / self._binary_name

    for candidate in default_chromium_candidates(self.platform):
      candidate_out = candidate / "out"
      if not self.platform.is_dir(candidate_out):
        continue
      # TODO: support remote glob
      for build in ("Release", "release", "rel", "Optdebug", "optdebug", "opt"):
        yield candidate_out / build / self._binary_name

  def _find_path(self) -> Optional[pth.RemotePath]:
    for candidate in self._iterate_candidate_bin_paths():
      if self._is_valid_path(candidate):
        return candidate
    return None

  def _is_valid_path(self, candidate: pth.RemotePath) -> bool:
    assert candidate.name == self._binary_name
    if not self.platform.is_file(candidate):
      return False
    # .../chromium/src/out/Release/BINARY => .../chromium/src/
    # Don't use parents[] access to stop at the root.
    maybe_checkout_dir = candidate.parent.parent.parent
    if not is_chromium_checkout_dir(self._platform, maybe_checkout_dir):
      return False
    return True


class V8CheckoutFinder(BaseDirFinder):

  def __init__(
      self,
      platform: Platform,
      candidates: Tuple[pth.RemotePath, ...] = tuple()) -> None:
    home_dir = platform.home()
    candidates += (
        # V8 Checkouts
        home_dir / "Documents/v8/v8",
        home_dir / "v8/v8",
        platform.path("C:/src/v8/v8"),
        # Raw V8 checkouts
        home_dir / "Documents/v8",
        home_dir / "v8",
        platform.path("C:/src/v8/"),
    )
    super().__init__(platform, candidates)

  def _find_path(self) -> Optional[pth.RemotePath]:
    if v8_checkout := super()._find_path():
      return v8_checkout
    if chromium_checkout := ChromiumCheckoutFinder(self.platform).path:
      return chromium_checkout / "v8"
    maybe_d8_path = self.platform.environ.get("D8_PATH")
    if not maybe_d8_path:
      return None
    for candidate_dir in self.platform.path(maybe_d8_path).parents:
      if self._is_valid_path(candidate_dir):
        return candidate_dir
    return None

  def _is_valid_path(self, candidate: pth.RemotePath) -> bool:
    v8_header_file = candidate / "include" / "v8.h"
    return (self.platform.is_file(v8_header_file) and
            (self.platform.is_dir(candidate / ".git")))


class TraceconvFinder:

  def __init__(self, platform: Platform) -> None:
    self.traceconv: Optional[pth.RemotePath] = None
    if chrome_checkout := ChromiumCheckoutFinder(platform).path:
      candidate = (
          chrome_checkout / "third_party" / "perfetto" / "tools" / "traceconv")
      if platform.is_file(candidate):
        self.traceconv = candidate


class TraceProcessorFinder:

  def __init__(self, platform: Platform) -> None:
    if chrome_checkout := ChromiumCheckoutFinder(platform).path:
      candidate = (
          chrome_checkout / "third_party" / "perfetto" / "tools" / "trace_processor")
      if platform.is_file(candidate):
        self.path = candidate


class WprGoToolFinder:
  _WPR_GO = pth.RemotePath("third_party/catapult/web_page_replay_go/src/wpr.go")

  def __init__(self, platform: Platform) -> None:
    self.platform = platform
    self.path: Optional[pth.RemotePath] = None
    if maybe_chrome := ChromiumCheckoutFinder(platform).path:
      candidate = maybe_chrome / self._WPR_GO
      if self.platform.is_file(candidate):
        self.path = candidate


class TsProxyFinder:
  _TS_PROXY = pth.RemotePath(
      "third_party/catapult/third_party/tsproxy/tsproxy.py")

  def __init__(self, platform: Platform) -> None:
    self.platform = platform
    self.path: Optional[pth.RemotePath] = None
    if maybe_chrome := ChromiumCheckoutFinder(platform).path:
      candidate = maybe_chrome / self._TS_PROXY
      if self.platform.is_file(candidate):
        self.path = candidate
