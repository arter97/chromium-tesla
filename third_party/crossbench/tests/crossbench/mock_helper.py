# Copyright 2022 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from __future__ import annotations

import abc
import contextlib
import io
import logging
import pathlib
import shlex

from subprocess import CompletedProcess
from typing import (TYPE_CHECKING, Any, Dict, Final, List, Mapping, Optional,
                    Sequence, Tuple, Union)
from unittest import mock

import psutil
from pyfakefs import fake_filesystem_unittest

import crossbench
from crossbench import cli_helper
from crossbench import path as pth
from crossbench import plt
from crossbench.benchmarks.base import SubStoryBenchmark
from crossbench.browsers.browser import Browser
from crossbench.cli.cli import CrossBenchCLI
from crossbench.cli.config.browser_variants import BrowserVariantsConfig
from crossbench.cli.config.network import NetworkConfig
from crossbench.plt.android_adb import Adb, AndroidAdbPlatform
from crossbench.plt.base import MachineArch, Platform
from crossbench.plt.linux import LinuxPlatform
from crossbench.plt.linux_ssh import LinuxSshPlatform
from crossbench.plt.chromeos_ssh import ChromeOsSshPlatform
from crossbench.plt.macos import MacOSPlatform
from crossbench.plt.win import WinPlatform
from crossbench.runner.run import Run
from crossbench.stories.story import Story
from tests.crossbench import mock_browser

if TYPE_CHECKING:
  from crossbench.runner.runner import Runner


GIB = 1014**3


ShellArgsT = Tuple[Union[str, pathlib.Path]]


class MockPlatformMixin:

  def __init__(self, *args, is_battery_powered=False, **kwargs):
    self._is_battery_powered = is_battery_powered
    # Cache some helper properties that might fail under pyfakefs.
    self.sh_cmds: List[ShellArgsT] = []
    self.expected_sh_cmds: Optional[List[ShellArgsT]] = None
    self.sh_results: List[str] = []
    super().__init__(*args, **kwargs)

  def expect_sh(self,
                *args: Union[str, pathlib.Path],
                result: str = "") -> None:
    if self.expected_sh_cmds is None:
      self.expected_sh_cmds = []
    self.expected_sh_cmds.append(args)
    self.sh_results.append(result)

  @property
  def name(self) -> str:
    return "mock"

  @property
  def machine(self) -> MachineArch:
    return MachineArch.ARM_64

  @property
  def version(self) -> str:
    return "1.2.3.4.5"

  @property
  def device(self) -> str:
    return "TestBook Pro"

  @property
  def cpu(self) -> str:
    return "Mega CPU @ 3.00GHz"

  @property
  def is_battery_powered(self) -> bool:
    return self._is_battery_powered

  def is_thermal_throttled(self) -> bool:
    return False

  def disk_usage(self, path: pathlib.Path):
    del path
    # pylint: disable=protected-access
    return psutil._common.sdiskusage(
        total=GIB * 100, used=20 * GIB, free=80 * GIB, percent=20)

  def cpu_usage(self) -> float:
    return 0.1

  def cpu_details(self) -> Dict[str, Any]:
    return {"physical cores": 2, "logical cores": 4, "info": self.cpu}

  def system_details(self):
    return {"CPU": "20-core 3.1 GHz"}

  def sleep(self, duration):
    del duration

  def processes(self, attrs=()):
    del attrs
    return []

  def process_children(self, parent_pid: int, recursive=False):
    del parent_pid, recursive
    return []

  def foreground_process(self):
    return None

  def sh_stdout(self,
                *args: Union[str, pathlib.Path],
                shell: bool = False,
                quiet: bool = False,
                encoding: str = "utf-8",
                env: Optional[Mapping[str, str]] = None,
                check: bool = True) -> str:
    del shell, quiet, encoding, env, check
    if self.expected_sh_cmds is not None:
      assert self.expected_sh_cmds, f"Missing expected sh_cmds, but got: {args}"
      # Convert all args to str first, sh accepts both str and Paths.
      expected = tuple(map(str, self.expected_sh_cmds.pop(0)))
      str_args = tuple(map(str, args))
      assert expected == str_args, f"Expected sh_cmd: {expected}, got: {args}"
    self.sh_cmds.append(args)
    if not self.sh_results:
      cmd = shlex.join(map(str, args))
      raise ValueError(f"MockPlatform has no more sh outputs for cmd: {cmd}")
    return self.sh_results.pop(0)

  def sh(self,
         *args: Union[str, pathlib.Path],
         shell: bool = False,
         capture_output: bool = False,
         stdout=None,
         stderr=None,
         stdin=None,
         env: Optional[Mapping[str, str]] = None,
         quiet: bool = False,
         check: bool = False):
    del capture_output, stderr, stdin, stdout
    self.sh_stdout(*args, shell=shell, quiet=quiet, env=env, check=check)
    # TODO: Generalize this in the future, to mimic failing `sh` calls.
    return CompletedProcess(args, 0)



class LinuxMockPlatform(MockPlatformMixin, LinuxPlatform):
  pass


class LinuxSshMockPlatform(MockPlatformMixin, LinuxSshPlatform):
  pass


class ChromeOsSshMockPlatform(MockPlatformMixin, ChromeOsSshPlatform):
  pass


class MacOsMockPlatform(MockPlatformMixin, MacOSPlatform):
  pass


class WinMockPlatform(MockPlatformMixin, WinPlatform):
  pass


class MockAdb(Adb):

  def start_server(self) -> None:
    pass

  def stop_server(self) -> None:
    pass

  def kill_server(self) -> None:
    pass


class AndroidAdbMockPlatform(MockPlatformMixin, AndroidAdbPlatform):
  pass


class GenericMockPlatform(MockPlatformMixin, Platform):
  pass


if plt.PLATFORM.is_linux:
  MockPlatform = LinuxMockPlatform
elif plt.PLATFORM.is_macos:
  MockPlatform = MacOsMockPlatform
elif plt.PLATFORM.is_win:
  MockPlatform = WinMockPlatform
else:
  raise RuntimeError(f"Unsupported platform: {plt.PLATFORM}")


class MockStory(Story):

  @classmethod
  def all_story_names(cls):
    return ["story_1", "story_2"]

  def run(self, run: Run) -> None:
    pass


class MockBenchmark(SubStoryBenchmark):
  NAME = "mock-benchmark"
  DEFAULT_STORY_CLS = MockStory


class MockCLI(CrossBenchCLI):
  runner: Runner
  platform: Platform

  def __init__(self, *args, **kwargs) -> None:
    self.platform = kwargs.pop("platform")
    super().__init__(*args, **kwargs)

  def _get_runner(self, args, benchmark, env_config, env_validation_mode,
                  timing):
    if not args.out_dir:
      # Use stable mock out dir
      args.out_dir = pathlib.Path("/results")
      assert not args.out_dir.exists()
    runner_kwargs = self.RUNNER_CLS.kwargs_from_cli(args)
    self.runner = self.RUNNER_CLS(
        benchmark=benchmark,
        env_config=env_config,
        env_validation_mode=env_validation_mode,
        timing=timing,
        **runner_kwargs,
        # Use custom platform
        platform=self.platform)
    return self.runner


class CrossbenchFakeFsTestCase(
    fake_filesystem_unittest.TestCase, metaclass=abc.ABCMeta):

  def setUp(self) -> None:
    super().setUp()
    self.setUpPyfakefs(
        modules_to_reload=[crossbench, mock_browser, cli_helper, pth])
    # gettext is used extensively in argparse
    gettext_patcher = mock.patch(
        "gettext.dgettext", side_effect=lambda domain, message: message)
    gettext_patcher.start()
    self.addCleanup(gettext_patcher.stop)
    sleep_patcher = mock.patch('time.sleep', return_value=None)
    self.addCleanup(sleep_patcher.stop)

  def create_file(self,
                  path_str: str,
                  contents: Optional[str] = None) -> pathlib.Path:
    path = pathlib.Path(path_str)
    self.fs.create_file(path, contents=contents)
    return path


class BaseCrossbenchTestCase(CrossbenchFakeFsTestCase, metaclass=abc.ABCMeta):

  def filter_splashscreen_urls(self, urls: Sequence[str]) -> List[str]:
    return [url for url in urls if not url.startswith("data:")]

  def setUp(self) -> None:
    # Instantiate MockPlatform before setting up fake_filesystem so we can
    # still interact with the original, real plt.Platform object for extracting
    # basic system information.
    self.platform = MockPlatform()  # pytype: disable=not-instantiable
    super().setUp()
    self._default_log_level = logging.getLogger().getEffectiveLevel()
    logging.getLogger().setLevel(logging.CRITICAL)
    for mock_browser_cls in mock_browser.ALL:
      mock_browser_cls.setup_fs(self.fs)
      self.assertTrue(mock_browser_cls.APP_PATH.exists())
    self.out_dir = pathlib.Path("/tmp/results/test")
    self.out_dir.parent.mkdir(parents=True)
    self.browsers = [
        mock_browser.MockChromeDev("dev", platform=self.platform),
        mock_browser.MockChromeStable("stable", platform=self.platform)
    ]
    mock_platform_patcher = mock.patch.object(plt, "PLATFORM", self.platform)
    mock_platform_patcher.start()
    self.addCleanup(mock_platform_patcher.stop)
    for browser in self.browsers:
      self.assertListEqual(browser.js_side_effects, [])
    self.mock_args = mock.Mock(
        wraps=False,
        driver_path=None,
        browser_config=None,
        viewport=None,
        splash_screen=None,
        cache_dir=pathlib.Path("test_cache_dir"),
        enable_features=None,
        disable_features=None,
        js_flags=None,
        enable_field_trial_config=False,
        network=NetworkConfig.default(),
        probe=[],
        other_browser_args=[])

  def tearDown(self) -> None:
    logging.getLogger().setLevel(self._default_log_level)
    self.assertListEqual(self.platform.sh_results, [])
    super().tearDown()


class SysExitTestException(Exception):

  def __init__(self, exit_code=0):
    super().__init__("sys.exit")
    self.exit_code = exit_code


class BaseCliTestCase(BaseCrossbenchTestCase):

  SPLASH_URLS_LEN: Final[int] = 2

  def run_cli_output(self,
                     *args,
                     raises=None,
                     enable_logging: bool = True) -> Tuple[MockCLI, str, str]:
    with mock.patch(
        "sys.stdout", new_callable=io.StringIO) as mock_stdout, mock.patch(
            "sys.stderr", new_callable=io.StringIO) as mock_stderr:
      cli = self.run_cli(*args, raises=raises, enable_logging=enable_logging)
    stdout = mock_stdout.getvalue()
    stderr = mock_stderr.getvalue()
    # Make sure we don't accidentally reuse the buffers across run_cli calls.
    mock_stdout.close()
    mock_stderr.close()
    return cli, stdout, stderr

  def run_cli(self,
              *args,
              raises=None,
              enable_logging: bool = False) -> MockCLI:
    cli = MockCLI(platform=self.platform, enable_logging=enable_logging)
    with mock.patch(
        "sys.exit", side_effect=SysExitTestException), mock.patch.object(
            plt, "PLATFORM", self.platform):
      if raises:
        with self.assertRaises(raises):
          cli.run(args)
      else:
        cli.run(args)
    return cli

  def mock_chrome_stable(self):
    return mock.patch.object(
        BrowserVariantsConfig,
        "_get_browser_cls",
        return_value=mock_browser.MockChromeStable)

  @contextlib.contextmanager
  def patch_get_browser(self, return_value: Optional[Sequence[Browser]] = None):
    if not return_value:
      return_value = self.browsers
    with mock.patch.object(
        CrossBenchCLI, "_get_browsers", return_value=return_value):
      yield
