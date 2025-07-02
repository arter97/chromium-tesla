# Copyright 2023 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from __future__ import annotations

import abc
import dataclasses
import enum
import functools
from typing import Any, Final, Tuple


@dataclasses.dataclass
class _BrowserVersionChannelMixin:
  label: str
  index: int


@functools.total_ordering
class BrowserVersionChannel(_BrowserVersionChannelMixin, enum.Enum):
  LTS = ("lts", 0)
  STABLE = ("stable", 1)
  BETA = ("beta", 2)
  ALPHA = ("alpha", 3)
  PRE_ALPHA = ("pre-alpha", 4)

  def __str__(self) -> str:
    return self.label

  def __lt__(self, other: Any) -> bool:
    if not isinstance(other, BrowserVersionChannel):
      raise TypeError("BrowserVersionChannel can not be compared to {other}")
    return self.index < other.index


class BrowserVersionParseError(ValueError):

  def __init__(self, name: str, msg: str, version: str):
    self._version = version
    super().__init__(f"Invalid {name} {repr(version)}: {msg}")


class PartialBrowserVersionError(ValueError):
  pass


@functools.total_ordering
class BrowserVersion(abc.ABC):

  _MAX_PART_VALUE: Final[int] = 0xFFFF

  _parts: Tuple[int, ...]
  _channel: BrowserVersionChannel
  _version_str: str

  def __init__(self, version: str) -> None:
    (self._parts, self._channel, self._version_str) = self._parse(version)
    if self._parts is None:
      raise self.parse_error("Invalid version format", version)
    for part in self._parts:
      if part < 0:
        raise self.parse_error("Version parts must be positive", version)

  def parse_error(self, msg: str, version: str) -> BrowserVersionParseError:
    return BrowserVersionParseError(type(self).__name__, msg, version)

  @abc.abstractmethod
  def _parse(
      self,
      full_version: str) -> Tuple[Tuple[int, ...], BrowserVersionChannel, str]:
    pass

  @property
  def parts(self) -> Tuple[int, ...]:
    return self._parts

  def comparable_parts(self, padded_len) -> Tuple[int, ...]:
    if self.is_complete:
      return self._parts
    padding = (self._MAX_PART_VALUE,) * (padded_len - len(self._parts))
    return self._parts + padding

  @property
  @abc.abstractmethod
  def is_complete(self) -> bool:
    pass

  @property
  def is_channel_version(self) -> bool:
    return not self._parts

  @property
  def major(self) -> int:
    if not self._parts:
      raise PartialBrowserVersionError()
    return self._parts[0]

  @property
  def minor(self) -> int:
    if len(self._parts) <= 1:
      raise PartialBrowserVersionError()
    return self._parts[1]

  @property
  def channel(self) -> BrowserVersionChannel:
    return self._channel

  @property
  def is_lts(self) -> bool:
    return self.channel == BrowserVersionChannel.LTS

  @property
  def is_stable(self) -> bool:
    return self.channel == BrowserVersionChannel.STABLE

  @property
  def is_beta(self) -> bool:
    return self.channel == BrowserVersionChannel.BETA

  @property
  def is_alpha(self) -> bool:
    return self.channel == BrowserVersionChannel.ALPHA

  @property
  def is_pre_alpha(self) -> bool:
    return self.channel == BrowserVersionChannel.PRE_ALPHA

  @property
  def channel_name(self) -> str:
    return self._channel_name(self.channel)

  @abc.abstractmethod
  def _channel_name(self, channel: BrowserVersionChannel) -> str:
    pass

  @property
  def key(self) -> Tuple[Tuple[int, ...], BrowserVersionChannel]:
    return (self._parts, self._channel)

  def __str__(self) -> str:
    if not self._version_str:
      return self.channel_name
    return f"{self._version_str} {self.channel_name}"

  def __eq__(self, other: Any) -> bool:
    if not isinstance(other, type(self)):
      return False
    return self.key == other.key

  def __le__(self, other: Any) -> bool:
    if not isinstance(other, type(self)):
      raise TypeError("Cannot compare versions from different browsers: "
                      f"{self} vs. {other}.")
    if self.is_channel_version and other.is_channel_version:
      return self._channel <= other._channel
    if self.is_channel_version:
      raise ValueError(f"Cannot compare channel {self} against {other}")
    if other.is_channel_version:
      raise ValueError(f"Cannot compare {self} against channel {other}")
    return self.key <= other.key


class UnknownBrowserVersion(BrowserVersion):
  """Sentinel helper object for initializing version variables before
  knowing which exact browser/version is used."""

  def __init__(self) -> None:
    super().__init__("unknown")

  def _parse(
      self,
      full_version: str) -> Tuple[Tuple[int, ...], BrowserVersionChannel, str]:
    del full_version
    return (tuple(), BrowserVersionChannel.STABLE, "unknown")

  def _channel_name(self, channel: BrowserVersionChannel) -> str:
    return "unknown"

  @property
  def is_stable(self) -> bool:
    return False

  @property
  def is_complete(self) -> bool:
    return False
