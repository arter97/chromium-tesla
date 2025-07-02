# Copyright 2023 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from __future__ import annotations

import re
from typing import Dict, Final, Optional, Tuple

from crossbench.browsers.version import (BrowserVersion, BrowserVersionChannel,
                                         PartialBrowserVersionError)


class ChromiumVersion(BrowserVersion):
  _PARTS_LEN: Final[int] = 4
  _VERSION_RE = re.compile(
      r"(?P<prefix>[^\d]*)"
      r"((?P<version>\d+(\.[^. ]+){0,3})|"
      r"(?P<channel>extended|stable|beta|dev|canary))"
      r"(?P<suffix>.*)", re.I)
  _VALID_SUFFIX_MATCH = re.compile(r"[^.\d]+", re.I)
  _CHANNEL_LOOKUP: Dict[str, BrowserVersionChannel] = {
      "stable": BrowserVersionChannel.STABLE,
      "beta": BrowserVersionChannel.BETA,
      "dev": BrowserVersionChannel.ALPHA,
      "canary": BrowserVersionChannel.PRE_ALPHA,
      "extended": BrowserVersionChannel.LTS
  }

  def _parse(
      self,
      full_version: str) -> Tuple[Tuple[int, ...], BrowserVersionChannel, str]:
    matches = self._VERSION_RE.search(full_version.strip())
    if not matches:
      raise self.parse_error("Could not extract version number.", full_version)
    channel_str = matches["channel"] or ""
    version_str = matches["version"]
    if not version_str and not channel_str:
      raise self.parse_error("Got empty version match.", full_version)
    prefix = matches["prefix"]
    if not self._validate_prefix(prefix):
      raise self.parse_error(f"Wrong prefix {repr(prefix)}", full_version)
    suffix = matches["suffix"]
    if not self._validate_suffix(suffix):
      raise self.parse_error(f"Wrong suffix {repr(suffix)}", full_version)

    if not version_str:
      return self._channel_version(channel_str, full_version)
    return self._numbered_version(version_str, full_version)

  def _channel_version(
      self, channel_str: str,
      full_version: str) -> Tuple[Tuple[int, ...], BrowserVersionChannel, str]:
    channel = self._parse_exact_channel(channel_str, full_version)
    version_str = ""
    return tuple(), channel, version_str

  def _numbered_version(
      self, version_str: str,
      full_version: str) -> Tuple[Tuple[int, ...], BrowserVersionChannel, str]:
    channel: BrowserVersionChannel = self._parse_default_channel(full_version)
    try:
      parts = tuple(map(int, version_str.split(".")))
    except ValueError as e:
      raise self.parse_error(
          f"Could not parse version parts {repr(version_str)}",
          full_version) from e

    if len(parts) < self._PARTS_LEN:
      padding = ("X",) * (self._PARTS_LEN - len(parts))
      version_str = ".".join(map(str, parts + padding))
    if len(parts) > self._PARTS_LEN:
      raise self.parse_error(f"Too many version parts {parts}", full_version)
    return parts, channel, version_str

  def _validate_prefix(self, prefix: Optional[str]) -> bool:
    if not prefix:
      return True
    prefix = prefix.lower()
    if prefix.strip() == "m":
      return True
    return "chromium " in prefix or "chromium-" in prefix

  def _parse_exact_channel(self, channel_str: str,
                           full_version: str) -> BrowserVersionChannel:
    if channel := self._CHANNEL_LOOKUP.get(channel_str.lower()):
      return channel
    raise self.parse_error(f"Unknown channel {repr(channel_str)}", full_version)

  def _parse_default_channel(self, full_version: str) -> BrowserVersionChannel:
    version_lower: str = full_version.lower()
    for channel_name, channel_obj in self._CHANNEL_LOOKUP.items():
      if channel_name in version_lower:
        return channel_obj
    return BrowserVersionChannel.STABLE

  def _validate_suffix(self, suffix: Optional[str]) -> bool:
    if not suffix:
      return True
    return bool(self._VALID_SUFFIX_MATCH.fullmatch(suffix))

  @property
  def key(self) -> Tuple[Tuple[int, ...], BrowserVersionChannel]:
    return (self.comparable_parts(self._PARTS_LEN), self._channel)

  @property
  def is_complete(self) -> bool:
    return len(self.parts) == 4

  @property
  def build(self) -> int:
    if len(self._parts) <= 2:
      raise PartialBrowserVersionError()
    return self._parts[2]

  @property
  def patch(self) -> int:
    if len(self._parts) <= 3:
      raise PartialBrowserVersionError()
    return self._parts[3]

  @property
  def is_dev(self) -> bool:
    return self.is_alpha

  @property
  def is_canary(self) -> bool:
    return self.is_pre_alpha

  def _channel_name(self, channel: BrowserVersionChannel) -> str:
    for name, lookup_channel in self._CHANNEL_LOOKUP.items():
      if channel == lookup_channel:
        return name
    raise ValueError(f"Unsupported channel: {channel}")


class ChromeDriverVersion(ChromiumVersion):
  _EMPTY_COMMIT_HASH: Final = "0000000000000000000000000000000000000000"

  def _validate_prefix(self, prefix: Optional[str]) -> bool:
    if not prefix:
      return False
    return prefix.lower() in ("chromedriver ", "chromedriver-")

  def _parse_default_channel(self, full_version: str) -> BrowserVersionChannel:
    if self._EMPTY_COMMIT_HASH in full_version:
      return BrowserVersionChannel.PRE_ALPHA
    return BrowserVersionChannel.STABLE

  def _validate_suffix(self, suffix: Optional[str]) -> bool:
    # TODO: extract commit hash / branch info from newer versions
    return True
