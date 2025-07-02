# Copyright 2022 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from __future__ import annotations

import abc
import collections
import logging
import re
from typing import (Any, Dict, Iterable, Iterator, List, Optional, Tuple, Type,
                    TypeVar, Union)

from ordered_set import OrderedSet

from crossbench import path as pth


class FrozenFlagsError(RuntimeError):
  pass


FreezableT = TypeVar("FreezableT", bound="Freezable")


class Freezable:

  def __init__(self, *args, **kwargs) -> None:
    self._frozen = False
    super().__init__(*args, **kwargs)

  def __hash__(self):
    self.freeze()
    return hash(str(self))

  @property
  def is_frozen(self) -> bool:
    return self._frozen

  def freeze(self: FreezableT) -> FreezableT:
    self._frozen = True
    return self

  def assert_not_frozen(self, msg: Optional[str] = None) -> None:
    if not self._frozen:
      return
    if not msg:
      msg = f"Cannot modify frozen {type(self).__name__}"
    raise FrozenFlagsError(msg)


BasicFlagsT = TypeVar("BasicFlagsT", bound="BasicFlags")


class BasicFlags(Freezable, collections.UserDict):
  """Basic implementation for command line flags (similar to Dic[str, str].

  This class is mostly used to make sure command-line flags for browsers
  don't end up having contradicting values.
  """

  InitialDataType = Optional[Union[Dict[str, str], "Flags",
                                   Iterable[Union[Tuple[str, Optional[str]],
                                                  str]]]]

  _WHITE_SPACE_RE = re.compile(r"\s+")
  _BASIC_FLAG_NAME_RE = re.compile(r"(--?)[^\s=-][^\s=]*")
  # Handles space-separated flags: --foo="1" --bar  --baz='2'  --boo=3
  _VALUE_PATTERN = (r"('(?P<value_single_quotes>[^']*)')|"
                    r"(\"(?P<value_double_quotes>[^\"]*)\")|"
                    r"(?P<value_no_quotes>[^'\" ]+)")
  _END_OR_SEPARATOR_PATTERN = r"(\s*\s\s*|$)"
  _PARSE_RE = re.compile(fr"(?P<name>{_BASIC_FLAG_NAME_RE.pattern})"
                         fr"((?P<equal>=)({_VALUE_PATTERN})?)?"
                         fr"{_END_OR_SEPARATOR_PATTERN}")

  @classmethod
  def split(cls, flag_str: str) -> Tuple[str, Optional[str]]:
    if "=" in flag_str:
      flag_name, flag_value = flag_str.split("=", maxsplit=1)
      return (flag_name, flag_value)
    return (flag_str, None)

  @classmethod
  def parse(cls: Type[BasicFlagsT], data: Any) -> BasicFlagsT:
    if isinstance(data, cls):
      return data
    if isinstance(data, str):
      return cls.loads(data)
    return cls(data)

  @classmethod
  def loads(cls: Type[BasicFlagsT], raw_flags: str) -> BasicFlagsT:
    return cls._loads(raw_flags)

  @classmethod
  def _loads(cls: Type[BasicFlagsT],
             raw_flags: str,
             msg: str = "flag") -> BasicFlagsT:
    raw_flags = raw_flags.strip()
    if not raw_flags:
      return cls()
    flag_parts: List[Tuple[str, Optional[str]]] = []
    current_end: Optional[int] = None
    for match in cls._PARSE_RE.finditer(raw_flags):
      if current_end is None:
        if match.start() != 0:
          part = raw_flags[:match.start()]
          raise ValueError(f"Invalid {msg} part at pos=0: {repr(part)}")
      else:
        if current_end != match.start():
          raise ValueError(f"Invalid {msg}: could not consume all data")
      current_end = match.end()

      groups = match.groupdict()
      maybe_flag_name: Optional[str] = groups.get("name")
      if not maybe_flag_name:
        raise ValueError(f"Invalid {msg}: {repr(raw_flags)}")
      # Re-assign since pytype doesn't remove the Optional.
      flag_name: str = maybe_flag_name
      flag_value: Optional[str] = (
          groups.get("value_single_quotes") or
          groups.get("value_double_quotes") or groups.get("value_no_quotes"))
      if groups.get("equal") and not flag_value:
        raise ValueError(f"Invalid {msg}: missing value for {repr(flag_name)}")
      assert flag_name
      flag_parts.append((flag_name, flag_value))

    if current_end != len(raw_flags):
      part = raw_flags[current_end:]
      raise ValueError(
          f"Invalid {msg} part at pos={current_end or 0}: {repr(part)}")
    return cls(flag_parts)

  def __init__(self, initial_data: Flags.InitialDataType = None) -> None:
    super().__init__(initial_data)

  def __setitem__(self, flag_name: str, flag_value: Optional[str]) -> None:
    return self.set(flag_name, flag_value)

  def set(self,
          flag_name: str,
          flag_value: Optional[str] = None,
          override: bool = False) -> None:
    self._set(flag_name, flag_value, override)

  def _set(self,
           flag_name: str,
           flag_value: Optional[str] = None,
           override: bool = False) -> None:
    self.assert_not_frozen()
    self._validate_flag_name(flag_name)
    if flag_value:
      self._validate_flag_value(flag_name, flag_value)
    self._validate_override(flag_name, flag_value, override)
    self.data[flag_name] = flag_value

  def _validate_flag_name(self, flag_name: str) -> None:
    if not flag_name:
      raise ValueError("Cannot set empty flag")
    if self._WHITE_SPACE_RE.search(flag_name):
      raise ValueError(
          f"Flag name cannot contain whitespaces: {repr(flag_name)}")
    if "=" in flag_name:
      raise ValueError(
          f"Flag name contains '=': {repr(flag_name)}, please split")
    if flag_name[0] != "-":
      raise ValueError(
          f"Flag name must begin with a '-', but got {repr(flag_name)}")
    if not self._BASIC_FLAG_NAME_RE.fullmatch(flag_name):
      raise ValueError(
          f"Flag name contains invalid characters: {repr(flag_name)}")

  def _validate_flag_value(self, flag_name: str, flag_value: str) -> None:
    assert flag_value, "Got invalid empty flag_value."
    if not isinstance(flag_value, str):
      raise TypeError(
          f"Expected None or string flag-value for flag {flag_name}, "
          f"but got: {repr(flag_value)}")

  def _validate_override(self, flag_name: str, flag_value: Optional[str],
                         override: bool) -> None:
    if override or flag_name not in self:
      return
    old_value = self[flag_name]
    if flag_value != old_value:
      raise ValueError(f"Flag {flag_name}={repr(flag_value)} was already set "
                       f"with a different previous value: {repr(old_value)}")

  # pylint: disable=arguments-differ
  def update(self,
             initial_data: Flags.InitialDataType = None,
             override: bool = False) -> None:
    # pylint: disable=arguments-differ
    if initial_data is None:
      return
    if isinstance(initial_data, (Flags, dict)):
      for flag_name, flag_value in initial_data.items():
        self.set(flag_name, flag_value, override)
    else:
      for flag_name_or_items in initial_data:
        if isinstance(flag_name_or_items, str):
          self.set(flag_name_or_items, None, override)
        else:
          flag_name, flag_value = flag_name_or_items
          self.set(flag_name, flag_value, override)

  def merge(self, other: Flags.InitialDataType):
    self.update(other)

  def copy(self: BasicFlagsT) -> BasicFlagsT:
    return self.__class__(self)

  def merge_copy(self, other: Flags.InitialDataType):
    ret = self.copy()
    ret.merge(other)
    return ret

  def _describe(self, flag_name: str) -> str:
    value = self.get(flag_name)
    if value is None:
      return flag_name
    return f"{flag_name}={value}"

  def items(self) -> Iterable[Tuple[str, Optional[str]]]:
    return self.data.items()

  def to_dict(self) -> Dict[str, Optional[str]]:
    return dict(self.items())

  def __iter__(self) -> Iterator[str]:
    for k, v in self.items():
      if v is None:
        yield k
      else:
        yield f"{k}={v}"

  def __str__(self) -> str:
    return " ".join(self)


class Flags(BasicFlags):
  """
  Subclass with slightly stricter flag name checking.
  Most command-line programs adhere to this.
  """
  _FLAG_NAME_RE = re.compile(r"(--?)[a-zA-Z0-9][a-zA-Z0-9_-]*")
  _PARSE_RE = re.compile(fr"(?P<name>{_FLAG_NAME_RE.pattern})"
                         fr"((?P<equal>=)({BasicFlags._VALUE_PATTERN})?)?"
                         fr"{BasicFlags._END_OR_SEPARATOR_PATTERN}")

  def _validate_flag_name(self, flag_name: str) -> None:
    super()._validate_flag_name(flag_name)
    if not self._FLAG_NAME_RE.fullmatch(flag_name):
      raise ValueError(
          f"Flag name contains invalid characters: {repr(flag_name)}")


FlagsT = TypeVar("FlagsT", bound=Flags)

class JSFlags(Flags):
  """Custom flags implementation for V8 flags (--js-flags in chrome)

  Additionally to the base Flag implementation it asserts that bool flags
  with the --no-.../--no... prefix are not contradicting each other.
  """
  _NO_PREFIX = "--no"
  _NAME_RE = re.compile(r"--[a-zA-Z_][a-zA-Z0-9_-]+")

  # We allow two forms:
  # - space separated: --foo="1" --bar  --baz='2'  --boo=3
  # - comma separated: --foo="1",--bar ,--baz='2', --boo=3
  _VALUE_PATTERN = (r"('(?P<value_single_quotes>[^',]+)')|"
                    r"(\"(?P<value_double_quotes>[^\",]+)\")|"
                    r"(?P<value_no_quotes>[^'\", =]+)")
  _END_OR_SEPARATOR_PATTERN = r"(\s*[,\s]\s*|$)"
  _PARSE_RE = re.compile(fr"(?P<name>{_NAME_RE.pattern})"
                         fr"((?P<equal>=)({_VALUE_PATTERN})?)?"
                         fr"{_END_OR_SEPARATOR_PATTERN}")

  @classmethod
  def loads(cls, raw_flags: str) -> JSFlags:
    return cls._loads(raw_flags, "--js-flags")

  def copy(self) -> JSFlags:
    return self.__class__(self)

  def _set(self,
           flag_name: str,
           flag_value: Optional[str] = None,
           override: bool = False) -> None:
    self._validate_js_flag_name(flag_name)
    if flag_value is not None:
      self._validate_js_flag_value(flag_name, flag_value)
    self._check_negated_flag(flag_name, override)
    super()._set(flag_name, flag_value, override)

  def _validate_js_flag_value(self, flag_name: str, flag_value: str) -> None:
    if not isinstance(flag_value, str):
      raise TypeError("JSFlag value must be str, "
                      f"but got {type(flag_value)}: {repr(flag_value)}")
    if "," in flag_value:
      raise ValueError(
          "--js-flags: Comma in V8 flag value, flag escaping for chrome's "
          f"--js-flags might not work: {flag_name}={repr(flag_value)}")
    if self._WHITE_SPACE_RE.search(flag_value):
      raise ValueError("--js-flags: V8 flag-values cannot contain whitespaces:"
                       f"{flag_name}={repr(flag_value)}")

  def _validate_js_flag_name(self, flag_name: str) -> None:
    if not flag_name.startswith("--"):
      raise ValueError("--js-flags: Only long-form flag names allowed, "
                       f"but got {repr(flag_name)}")
    if not self._NAME_RE.fullmatch(flag_name):
      raise ValueError(f"--js-flags: Invalid flag name {repr(flag_name)}. \n"
                       "Check invalid characters in the V8 flag name?")

  def _check_negated_flag(self, flag_name: str, override: bool) -> None:
    if flag_name.startswith(self._NO_PREFIX):
      enabled = flag_name[len(self._NO_PREFIX):]
      # Check for --no-foo form
      if enabled.startswith("-"):
        enabled = enabled[1:]
      enabled = "--" + enabled
      if override:
        del self[enabled]
      elif enabled in self:
        raise ValueError(
            f"Conflicting flag {flag_name}, "
            f"it has already been enabled by {repr(self._describe(enabled))}")
    else:
      # --foo => --no-foo
      disabled = f"--no-{flag_name[2:]}"
      if disabled not in self:
        # Try compact version: --foo => --nofoo
        disabled = f"--no{flag_name[2:]}"
        if disabled not in self:
          return
      if override:
        del self[disabled]
      else:
        raise ValueError(f"Conflicting flag {flag_name}, "
                         "it has previously been disabled by "
                         f"{repr(self._describe(flag_name))}")

  def __str__(self) -> str:
    return ",".join(self)


class ChromeFlags(Flags):
  """Specialized Flags for Chrome/Chromium-based browser.

  This has special treatment for --js-flags and the feature flags:
  --enable-features/--disable-features
  --enable-blink-features/--disable-blink-features
  """
  _JS_FLAG = "--js-flags"

  def __init__(self, initial_data: Flags.InitialDataType = None) -> None:
    self._features = ChromeFeatures()
    self._blink_features = ChromeBlinkFeatures()
    self._js_flags = JSFlags()
    super().__init__(initial_data)

  def freeze(self) -> ChromeFlags:
    super().freeze()
    self._js_flags.freeze()
    self._features.freeze()
    self._blink_features.freeze()
    return self

  def _set(self,
           flag_name: str,
           flag_value: Optional[str] = None,
           override: bool = False) -> None:
    self.assert_not_frozen()
    # pylint: disable=signature-differs
    if flag_name == ChromeFeatures.ENABLE_FLAG:
      if flag_value is None:
        raise ValueError(f"{ChromeFeatures.ENABLE_FLAG} cannot be None")
      for feature in flag_value.split(","):
        self._features.enable(feature)
    elif flag_name == ChromeFeatures.DISABLE_FLAG:
      if flag_value is None:
        raise ValueError(f"{ChromeFeatures.DISABLE_FLAG} cannot be None")
      for feature in flag_value.split(","):
        self._features.disable(feature)
    elif flag_name == ChromeBlinkFeatures.ENABLE_FLAG:
      if flag_value is None:
        raise ValueError(f"{ChromeBlinkFeatures.ENABLE_FLAG} cannot be None")
      for feature in flag_value.split(","):
        self._blink_features.enable(feature)
    elif flag_name == ChromeBlinkFeatures.DISABLE_FLAG:
      if flag_value is None:
        raise ValueError(f"{ChromeBlinkFeatures.DISABLE_FLAG} cannot be None")
      for feature in flag_value.split(","):
        self._blink_features.disable(feature)
    elif flag_name == self._JS_FLAG:
      if flag_value is None:
        raise ValueError(f"{self._JS_FLAG} cannot be None")
      self._set_js_flag(flag_value, override)
    else:
      flag_value = self._verify_flag(flag_name, flag_value)
      super()._set(flag_name, flag_value, override)

  def _set_js_flag(self, raw_js_flags: str, override: bool) -> None:
    new_js_flags = JSFlags(self._js_flags)
    for js_flag_name, js_flag_value in JSFlags.parse(raw_js_flags).items():
      new_js_flags.set(js_flag_name, js_flag_value, override=override)
    self._js_flags.update(new_js_flags)

  def _verify_flag(self, name: str, value: Optional[str]) -> Optional[str]:
    if candidate := self._find_misspelled_flag(name):
      logging.error(
          "Potentially misspelled flag: '%s'. "
          "Did you mean to use %s ?", name, candidate)
    if name == "--user-data-dir":
      if not value or not value.strip():
        raise ValueError("--user-data-dir cannot be the empty string.")
      # TODO: support remote platforms
      expanded_dir = str(pth.LocalPath(value).expanduser())
      if expanded_dir != value:
        logging.warning(
            "Chrome Flags: auto-expanding --user-data-dir from '%s' to '%s'",
            value, expanded_dir)
      return expanded_dir
    return value

  def _find_misspelled_flag(self, name: str) -> Optional[str]:
    if name in ("--enable-feature", "--enabled-feature", "--enabled-features"):
      return "--enable-features"
    if name in ("--disable-feature", "--disabled-feature",
                "--disabled-features"):
      return "--disable-features"
    if name in ("--enable-blink-feature", "--enabled-blink-feature",
                "--enabled-blink-features"):
      return "--enable-blink-features"
    if name in ("--disable-blink-feature", "--disabled-blink-feature",
                "--disabled-blink-features"):
      return "--disable-blink-features"
    return None

  @property
  def features(self) -> ChromeFeatures:
    return self._features

  @property
  def blink_features(self) -> ChromeBlinkFeatures:
    return self._blink_features

  @property
  def js_flags(self) -> JSFlags:
    return self._js_flags

  def merge(self, other: Flags.InitialDataType) -> None:
    if not isinstance(other, ChromeFlags):
      other = ChromeFlags(other)
    self.features.merge(other.features)
    self.blink_features.merge(other.blink_features)
    self.js_flags.merge(other.js_flags)
    for name, value in other.base_items():
      self.set(name, value)

  def base_items(self) -> Iterable[Tuple[str, Optional[str]]]:
    yield from super().items()

  def items(self) -> Iterable[Tuple[str, Optional[str]]]:
    yield from self.base_items()
    if self._js_flags:
      yield (self._JS_FLAG, str(self.js_flags))
    yield from self.features.items()
    yield from self.blink_features.items()


class ChromeBaseFeatures(Freezable, abc.ABC):
  ENABLE_FLAG: str = ""
  DISABLE_FLAG: str = ""

  def __init__(self) -> None:
    super().__init__()
    self._enabled: Dict[str, Optional[str]] = {}
    self._disabled: OrderedSet[str] = OrderedSet()

  @property
  def is_empty(self) -> bool:
    return len(self._enabled) == 0 and len(self._disabled) == 0

  @property
  def enabled(self) -> Dict[str, Optional[str]]:
    return dict(self._enabled)

  @property
  def disabled(self) -> OrderedSet[str]:
    return OrderedSet(self._disabled)

  def _parse_feature(self, feature: str) -> Tuple[str, Optional[str]]:
    if not feature:
      raise ValueError("Cannot parse empty feature")
    if "," in feature:
      raise ValueError(f"{repr(feature)} contains multiple features. "
                       "Please split them first.")
    return self._parse_feature_parts(feature)

  @abc.abstractmethod
  def _parse_feature_parts(self, feature: str) -> Tuple[str, Optional[str]]:
    pass

  def enable(self, feature: str) -> None:
    name, value = self._parse_feature(feature)
    self._enable(name, value)

  def _enable(self, name: str, value: Optional[str]) -> None:
    self.assert_not_frozen()
    if name in self._disabled:
      raise ValueError(
          f"Cannot enable previously disabled feature={repr(name)}")
    if name in self._enabled:
      prev_value = self._enabled[name]
      if value != prev_value:
        raise ValueError("Cannot set conflicting values "
                         f"({repr(prev_value)}, vs. {repr(value)}) "
                         f"for the same feature={repr(name)}")
    else:
      self._enabled[name] = value

  def disable(self, feature: str) -> None:
    self.assert_not_frozen()
    name, _ = self._parse_feature(feature)
    if name in self._enabled:
      raise ValueError(
          f"Cannot disable previously enabled feature={repr(name)}")
    self._disabled.add(name)

  def update(self, other: ChromeBaseFeatures) -> None:
    if not isinstance(other, type(self)):
      raise TypeError(f"Cannot merge {type(self)} with {type(other)}")
    for disabled in other.disabled:
      self.disable(disabled)
    for name, value in other.enabled.items():
      self._enable(name, value)

  def merge(self, other: ChromeBaseFeatures) -> None:
    self.update(other)

  def items(self) -> Iterable[Tuple[str, str]]:
    if self._enabled:
      joined = ",".join(
          k if v is None else f"{k}{v}" for k, v in self._enabled.items())
      yield (self.ENABLE_FLAG, joined)
    if self._disabled:
      joined = ",".join(self._disabled)
      yield (self.DISABLE_FLAG, joined)

  def __iter__(self) -> Iterator[str]:
    for flag_name, features_str in self.items():
      yield f"{flag_name}={features_str}"

  def __str__(self) -> str:
    return " ".join(self)


class ChromeFeatures(ChromeBaseFeatures):
  """
  Chrome Features set, throws if features are enabled and disabled at the same
  time.
  Examples:
    --disable-features="MyFeature1"
    --enable-features="MyFeature1,MyFeature2"
    --enable-features="MyFeature1:k1/v1/k2/v2,MyFeature2"
    --enable-features="MyFeature3<Trial2:k1/v1/k2/v2"
  """

  ENABLE_FLAG: str = "--enable-features"
  DISABLE_FLAG: str = "--disable-features"

  def _parse_feature_parts(self, feature: str) -> Tuple[str, Optional[str]]:
    parts = feature.split("<")
    if len(parts) == 2:
      return (parts[0], "<" + parts[1])
    if len(parts) != 1:
      raise ValueError(f"Invalid number of feature parts: {repr(parts)}")
    parts = feature.split(":")
    if len(parts) == 2:
      return (parts[0], ":" + parts[1])
    if len(parts) != 1:
      raise ValueError(f"Invalid number of feature parts: {repr(parts)}")
    return (feature, None)


class ChromeBlinkFeatures(ChromeBaseFeatures):
  """
  Chrome Features set, throws if features are enabled and disabled at the same
  time.
  Examples:
    --disable-blink-features="MyFeature1"
    --enable-blink-features="MyFeature1,MyFeature2"
  """

  ENABLE_FLAG: str = "--enable-blink-features"
  DISABLE_FLAG: str = "--disable-blink-features"

  def _parse_feature_parts(self, feature: str) -> Tuple[str, Optional[str]]:
    if "<" in feature or ":" in feature:
      raise ValueError("blink feature do not have params, "
                       f"but found param separator in {repr(feature)}")
    return (feature, None)
