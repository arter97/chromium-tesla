# Copyright 2022 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from __future__ import annotations

import abc
import contextlib
import copy
import pathlib
from typing import (TYPE_CHECKING, Any, Iterator, List, Optional, Tuple, Type,
                    cast)

from crossbench import plt
from crossbench.browsers.all import Chrome, Chromium, Edge, Firefox, Safari
from crossbench.browsers.attributes import BrowserAttributes
from crossbench.browsers.browser import Browser
from crossbench.flags import ChromeFlags, Flags, JSFlags
from crossbench.network.base import Network

if TYPE_CHECKING:
  import datetime as dt

  from crossbench.runner.groups import BrowserSessionRunGroup
  from crossbench.runner.runner import Runner


class MockNetwork(Network):

  @contextlib.contextmanager
  def open(self, session: BrowserSessionRunGroup) -> Iterator[Network]:
    with super().open(session):
      assert session.browser.network is self
      yield self
      assert self.is_running


class MockBrowser(Browser, metaclass=abc.ABCMeta):
  APP_PATH: pathlib.Path = pathlib.Path("/")
  MACOS_BIN_NAME: str = ""
  VERSION: str = "100.22.33.44"

  @classmethod
  def setup_fs(cls, fs) -> None:
    macos_bin_name = cls.APP_PATH.stem
    if cls.MACOS_BIN_NAME:
      macos_bin_name = cls.MACOS_BIN_NAME
    cls.setup_bin(fs, cls.APP_PATH, macos_bin_name)

  @classmethod
  def setup_bin(cls, fs, bin_path: pathlib.Path, macos_bin_name: str) -> None:
    if plt.PLATFORM.is_macos:
      assert bin_path.suffix == ".app"
      bin_path = bin_path / "Contents" / "MacOS" / macos_bin_name
    elif plt.PLATFORM.is_win:
      assert bin_path.suffix == ".exe"
    fs.create_file(bin_path)

  @classmethod
  def default_flags(cls,
                    initial_data: Flags.InitialDataType = None) -> ChromeFlags:
    return ChromeFlags(initial_data)

  def __init__(self,
               label: str,
               *args,
               path: Optional[pathlib.Path] = None,
               **kwargs):
    assert self.APP_PATH
    path = path or pathlib.Path(self.APP_PATH)
    self.app_path = path
    maybe_driver = kwargs.pop("driver_path", None)
    if maybe_driver:
      assert isinstance(maybe_driver, pathlib.Path) and maybe_driver.exists()
    super().__init__(label, path, *args, **kwargs)
    self.url_list: List[str] = []
    self.js_list: List[str] = []
    self.js_side_effects: List[Any] = []
    self.did_run: bool = False
    self.clear_cache_dir: bool = False

  def clear_cache(self, runner: Runner) -> None:
    pass

  def start(self, session: BrowserSessionRunGroup) -> None:
    assert not self._is_running
    self._is_running = True
    self.did_run = True

  def force_quit(self) -> None:
    if not self._is_running:
      return
    self._is_running = False

  def _extract_version(self) -> str:
    return self.VERSION

  def user_agent(self, runner: Runner) -> str:
    return f"Mock Browser {self.type_name}, {self.VERSION}"

  def show_url(self, runner: Runner, url, target: Optional[str] = None) -> None:
    self.url_list.append(url)

  def js(self,
         runner: Runner,
         script,
         timeout: Optional[dt.timedelta] = None,
         arguments=()):
    self.js_list.append(script)
    if timeout:
      assert timeout.total_seconds() > 0
    if self.js_side_effects is None:
      return None
    assert self.js_side_effects, ("Not enough mock js_side_effect available. "
                                  "Please add another js_side_effect entry for "
                                  f"arguments={arguments} \n"
                                  f"Script: {script}")
    result = self.js_side_effects.pop(0)
    # Return copies to avoid leaking data between repetitions.
    return copy.deepcopy(result)


if plt.PLATFORM.is_macos:
  APP_ROOT = pathlib.Path("/Applications")
elif plt.PLATFORM.is_win:
  APP_ROOT = pathlib.Path("C:/Program Files")
else:
  APP_ROOT = pathlib.Path("/usr/bin")


class MockChromiumBrowser(MockBrowser, metaclass=abc.ABCMeta):

  @property
  def js_flags(self) -> JSFlags:
    chrome_flags = cast(ChromeFlags, self.flags)
    assert isinstance(chrome_flags, ChromeFlags)
    return chrome_flags.js_flags  # pylint: disable=no-member

  @property
  def attributes(self) -> BrowserAttributes:
    return BrowserAttributes.CHROMIUM | BrowserAttributes.CHROMIUM_BASED


# Inject MockBrowser into the browser hierarchy for easier testing.
Chromium.register(MockChromiumBrowser)


class MockChromeBrowser(MockChromiumBrowser, metaclass=abc.ABCMeta):

  @property
  def type_name(self) -> str:
    return "chrome"

  @property
  def attributes(self) -> BrowserAttributes:
    return BrowserAttributes.CHROME | BrowserAttributes.CHROMIUM_BASED


Chrome.register(MockChromeBrowser)
if not TYPE_CHECKING:
  assert issubclass(MockChromeBrowser, Chrome)


class MockChromeStable(MockChromeBrowser):
  if plt.PLATFORM.is_macos:
    APP_PATH = APP_ROOT / "Google Chrome.app"
  elif plt.PLATFORM.is_win:
    APP_PATH = APP_ROOT / "Google/Chrome/Application/chrome.exe"
  else:
    APP_PATH = APP_ROOT / "google-chrome"


if not TYPE_CHECKING:
  assert issubclass(MockChromeStable, Chromium)
  assert issubclass(MockChromeStable, Chrome)


class MockChromeAndroidStable(MockChromeStable):

  @property
  def platform(self) -> plt.AndroidAdbPlatform:
    assert isinstance(
        self._platform,
        plt.AndroidAdbPlatform), (f"Invalid platform: {self._platform}")
    return cast(plt.AndroidAdbPlatform, self._platform)

  def _resolve_binary(self, path: pathlib.Path) -> pathlib.Path:
    return path

  @property
  def attributes(self) -> BrowserAttributes:
    return (BrowserAttributes.CHROME | BrowserAttributes.CHROMIUM_BASED
            | BrowserAttributes.MOBILE)


class MockChromeBeta(MockChromeBrowser):
  VERSION = "101.22.33.44"
  if plt.PLATFORM.is_macos:
    APP_PATH = APP_ROOT / "Google Chrome Beta.app"
  elif plt.PLATFORM.is_win:
    APP_PATH = APP_ROOT / "Google/Chrome Beta/Application/chrome.exe"
  else:
    APP_PATH = APP_ROOT / "google-chrome-beta"


class MockChromeDev(MockChromeBrowser):
  VERSION = "102.22.33.44"
  if plt.PLATFORM.is_macos:
    APP_PATH = APP_ROOT / "Google Chrome Dev.app"
  elif plt.PLATFORM.is_win:
    APP_PATH = APP_ROOT / "Google/Chrome Dev/Application/chrome.exe"
  else:
    APP_PATH = APP_ROOT / "google-chrome-unstable"


class MockChromeCanary(MockChromeBrowser):
  VERSION = "103.22.33.44"
  if plt.PLATFORM.is_macos:
    APP_PATH = APP_ROOT / "Google Chrome Canary.app"
  elif plt.PLATFORM.is_win:
    APP_PATH = APP_ROOT / "Google/Chrome SxS/Application/chrome.exe"
  else:
    APP_PATH = APP_ROOT / "google-chrome-canary"


class MockEdgeBrowser(MockChromiumBrowser, metaclass=abc.ABCMeta):

  @property
  def type_name(self) -> str:
    return "edge"

  @property
  def attributes(self) -> BrowserAttributes:
    return BrowserAttributes.EDGE | BrowserAttributes.CHROMIUM_BASED

Edge.register(MockEdgeBrowser)
if not TYPE_CHECKING:
  assert issubclass(MockEdgeBrowser, Chromium)
  assert issubclass(MockEdgeBrowser, Edge)


class MockEdgeStable(MockEdgeBrowser):
  if plt.PLATFORM.is_macos:
    APP_PATH = APP_ROOT / "Microsoft Edge.app"
  elif plt.PLATFORM.is_win:
    APP_PATH = APP_ROOT / "Microsoft/Edge/Application/msedge.exe"
  else:
    APP_PATH = APP_ROOT / "microsoft-edge"


class MockEdgeBeta(MockEdgeBrowser):
  VERSION = "101.22.33.44"
  if plt.PLATFORM.is_macos:
    APP_PATH = APP_ROOT / "Microsoft Edge Beta.app"
  elif plt.PLATFORM.is_win:
    APP_PATH = APP_ROOT / "Microsoft/Edge Beta/Application/msedge.exe"
  else:
    APP_PATH = APP_ROOT / "microsoft-edge-beta"


class MockEdgeDev(MockEdgeBrowser):
  VERSION = "102.22.33.44"
  if plt.PLATFORM.is_macos:
    APP_PATH = APP_ROOT / "Microsoft Edge Dev.app"
  elif plt.PLATFORM.is_win:
    APP_PATH = APP_ROOT / "Microsoft/Edge Dev/Application/msedge.exe"
  else:
    APP_PATH = APP_ROOT / "microsoft-edge-dev"


class MockEdgeCanary(MockEdgeBrowser):
  VERSION = "103.22.33.44"
  if plt.PLATFORM.is_macos:
    APP_PATH = APP_ROOT / "Microsoft Edge Canary.app"
  elif plt.PLATFORM.is_win:
    APP_PATH = APP_ROOT / "Microsoft/Edge SxS/Application/msedge.exe"
  else:
    APP_PATH = APP_ROOT / "unsupported/msedge-canary"


class MockSafariBrowser(MockBrowser, metaclass=abc.ABCMeta):

  @property
  def type_name(self) -> str:
    return "safari"

  @property
  def attributes(self) -> BrowserAttributes:
    return BrowserAttributes.SAFARI



Safari.register(MockSafariBrowser)
if not TYPE_CHECKING:
  assert issubclass(MockSafariBrowser, Safari)


class MockSafari(MockSafariBrowser):
  if plt.PLATFORM.is_macos:
    APP_PATH = APP_ROOT / "Safari.app"
  elif plt.PLATFORM.is_win:
    APP_PATH = APP_ROOT / "Unsupported/Safari.exe"
  else:
    APP_PATH = pathlib.Path("/unsupported-platform/Safari")


class MockSafariTechnologyPreview(MockSafariBrowser):
  if plt.PLATFORM.is_macos:
    APP_PATH = APP_ROOT / "Safari Technology Preview.app"
  elif plt.PLATFORM.is_win:
    APP_PATH = APP_ROOT / "Unsupported/Safari Technology Preview.exe"
  else:
    APP_PATH = pathlib.Path("/unsupported-platform/Safari Technology Preview")


class MockFirefoxBrowser(MockBrowser, metaclass=abc.ABCMeta):

  @property
  def type_name(self) -> str:
    return "firefox"

  @property
  def attributes(self) -> BrowserAttributes:
    return BrowserAttributes.FIREFOX


Firefox.register(MockFirefoxBrowser)
if not TYPE_CHECKING:
  assert issubclass(MockFirefoxBrowser, Firefox)


class MockFirefox(MockFirefoxBrowser):
  if plt.PLATFORM.is_macos:
    APP_PATH = APP_ROOT / "Firefox.app"
  elif plt.PLATFORM.is_win:
    APP_PATH = APP_ROOT / "Mozilla Firefox/firefox.exe"
  else:
    APP_PATH = APP_ROOT / "firefox"


class MockFirefoxDeveloperEdition(MockFirefoxBrowser):
  if plt.PLATFORM.is_macos:
    APP_PATH = APP_ROOT / "Firefox Developer Edition.app"
  elif plt.PLATFORM.is_win:
    APP_PATH = APP_ROOT / "Firefox Developer Edition/firefox.exe"
  else:
    APP_PATH = APP_ROOT / "firefox-developer-edition"


class MockFirefoxNightly(MockFirefoxBrowser):
  if plt.PLATFORM.is_macos:
    APP_PATH = APP_ROOT / "Firefox Nightly.app"
  elif plt.PLATFORM.is_win:
    APP_PATH = APP_ROOT / "Firefox Nightly/firefox.exe"
  else:
    APP_PATH = APP_ROOT / "firefox-trunk"


ALL: Tuple[Type[MockBrowser], ...] = (
    MockChromeCanary,
    MockChromeDev,
    MockChromeBeta,
    MockChromeStable,
    MockEdgeCanary,
    MockEdgeDev,
    MockEdgeBeta,
    MockEdgeStable,
    MockSafari,
    MockSafariTechnologyPreview,
    MockFirefox,
    MockFirefoxDeveloperEdition,
    MockFirefoxNightly,
)
