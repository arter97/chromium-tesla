# Copyright 2022 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import datetime as dt
import json
import pathlib
import unittest
from typing import Dict, List, Optional, Type
from unittest import mock

import hjson

from crossbench import __version__, cli_helper, plt
from crossbench.browsers import splash_screen, viewport
from crossbench.cli.cli import CrossBenchCLI
from crossbench.cli.config import BrowserConfig, BrowserVariantsConfig
from crossbench.cli.config.driver import BrowserDriverType, DriverConfig
from crossbench.env import HostEnvironmentConfig, ValidationMode
from crossbench.path import RemotePath
from crossbench.probes import internal
from crossbench.runner.runner import Runner
from tests import test_helper
from tests.crossbench import mock_browser
from tests.crossbench.mock_helper import BaseCliTestCase, SysExitTestException
from tests.crossbench.test_cli_config import XCTRACE_DEVICES_SINGLE_OUTPUT


class CliTestCase(BaseCliTestCase):

  def test_invalid(self):
    with self.assertRaises(SysExitTestException):
      self.run_cli("unknown subcommand", "--invalid flag")

  def test_describe_invalid_empty(self):
    with self.assertRaises(SysExitTestException) as cm:
      self.run_cli("describe", "")
    self.assertEqual(cm.exception.exit_code, 0)
    with self.assertRaises(SysExitTestException) as cm:
      self.run_cli("describe", "", "--json")
    self.assertEqual(cm.exception.exit_code, 0)

    with self.assertRaises(SysExitTestException) as cm:
      self.run_cli("describe", "--unknown")
    self.assertEqual(cm.exception.exit_code, 0)
    with self.assertRaises(SysExitTestException) as cm:
      self.run_cli("describe", "--unknown", "--json")
    self.assertEqual(cm.exception.exit_code, 0)

  def test_describe_invalid_probe(self):
    with self.assertRaises(SysExitTestException) as cm:
      self.run_cli("describe", "probe", "unknown probe")
    self.assertEqual(cm.exception.exit_code, 0)
    with self.assertRaises(SysExitTestException) as cm:
      self.run_cli("describe", "probe", "unknown probe", "--json")
    self.assertEqual(cm.exception.exit_code, 0)

  def test_describe_invalid_benchmark(self):
    with self.assertRaises(SysExitTestException) as cm:
      self.run_cli("describe", "benchmark", "unknown benchmark")
    self.assertEqual(cm.exception.exit_code, 0)
    with self.assertRaises(SysExitTestException) as cm:
      self.run_cli("describe", "benchmark", "unknown benchmark", "--json")
    self.assertEqual(cm.exception.exit_code, 0)

  def test_describe_invalid_all(self):
    with self.assertRaises(SysExitTestException) as cm:
      self.run_cli("describe", "all", "unknown probe or benchmark")
    self.assertEqual(cm.exception.exit_code, 0)
    with self.assertRaises(SysExitTestException) as cm:
      self.run_cli("describe", "--json", "all", "unknown probe or benchmark")
    self.assertEqual(cm.exception.exit_code, 0)

  def test_describe(self):
    # Non-json output shouldn't fail
    self.run_cli("describe")
    self.run_cli("describe", "all")
    _, stdout, stderr = self.run_cli_output("describe", "--json")
    self.assertFalse(stderr)
    data = json.loads(stdout)
    self.assertIn("benchmarks", data)
    self.assertIn("probes", data)
    self.assertIsInstance(data["benchmarks"], dict)
    self.assertIsInstance(data["probes"], dict)

  def test_describe_benchmarks(self):
    # Non-json output shouldn't fail
    self.run_cli("describe", "benchmarks")
    _, stdout, stderr = self.run_cli_output("describe", "--json", "benchmarks")
    self.assertFalse(stderr)
    data = json.loads(stdout)
    self.assertNotIn("benchmarks", data)
    self.assertNotIn("probes", data)
    self.assertIsInstance(data, dict)
    self.assertIn("loading", data)

  def test_describe_probes(self):
    # Non-json output shouldn't fail
    self.run_cli("describe", "probes")
    _, stdout, stderr = self.run_cli_output("describe", "--json", "probes")
    self.assertFalse(stderr)
    data = json.loads(stdout)
    self.assertNotIn("benchmarks", data)
    self.assertNotIn("probes", data)
    self.assertIsInstance(data, dict)
    self.assertIn("v8.log", data)

  def test_describe_all(self):
    self.run_cli("describe", "probes")
    _, stdout, stderr = self.run_cli_output("describe", "all")
    self.assertFalse(stderr)
    self.assertIn("benchmarks", stdout)
    self.assertIn("v8.log", stdout)
    self.assertIn("speedometer", stdout)

  def test_describe_all_filtered(self):
    self.run_cli("describe", "probes")
    _, stdout, stderr = self.run_cli_output("describe", "all", "v8.log")
    self.assertFalse(stderr)
    self.assertNotIn("benchmarks", stdout)
    self.assertIn("v8.log", stdout)
    self.assertNotIn("speedometer", stdout)

  def test_describe_all_json(self):
    self.run_cli("describe", "probes")
    _, stdout, stderr = self.run_cli_output("describe", "--json", "all")
    self.assertFalse(stderr)
    data = json.loads(stdout)
    self.assertIsInstance(data, dict)
    self.assertIn("benchmarks", data)
    self.assertIn("v8.log", data["probes"])

  def test_describe_all_json_filtered(self):
    self.run_cli("describe", "probes")
    _, stdout, stderr = self.run_cli_output("describe", "--json", "all",
                                            "v8.log")
    self.assertFalse(stderr)
    data = json.loads(stdout)
    self.assertIsInstance(data, dict)
    self.assertEqual(data["benchmarks"], {})
    self.assertEqual(len(data["probes"]), 1)
    self.assertIn("v8.log", data["probes"])

  def test_help(self):
    with self.assertRaises(SysExitTestException) as cm:
      self.run_cli("--help")
    self.assertEqual(cm.exception.exit_code, 0)
    _, stdout, stderr = self.run_cli_output(
        "--help", raises=SysExitTestException)
    self.assertFalse(stderr)
    self.assertIn("usage:", stdout)
    self.assertIn("Subcommands:", stdout)
    # Check for top-level option:
    self.assertIn("--no-color", stdout)
    self.assertIn("Disable colored output", stdout)
    self.assertIn("Available Probes for all Benchmarks:", stdout)

  def test_help_subcommand(self):
    with self.assertRaises(SysExitTestException) as cm:
      self.run_cli("help")
    self.assertEqual(cm.exception.exit_code, 0)
    _, stdout, stderr = self.run_cli_output("help", raises=SysExitTestException)
    self.assertFalse(stderr)
    self.assertIn("usage:", stdout)
    self.assertIn("Subcommands:", stdout)
    # Check for top-level option:
    self.assertIn("--no-color", stdout)
    self.assertIn("Disable colored output", stdout)
    self.assertIn("Available Probes for all Benchmarks:", stdout)

  def test_subcommand_help(self):
    for benchmark_cls in CrossBenchCLI.BENCHMARKS:
      subcommands = (benchmark_cls.NAME,) + benchmark_cls.aliases()
      for subcommand in subcommands:
        with self.assertRaises(SysExitTestException) as cm:
          self.run_cli(subcommand, "--help")
        self.assertEqual(cm.exception.exit_code, 0)
        _, stdout, stderr = self.run_cli_output(
            subcommand, "--help", raises=SysExitTestException)
        self.assertFalse(stderr)
        self.assertIn("--env-validation ENV_VALIDATION", stdout)

  def test_subcommand_help_subcommand(self):
    for benchmark_cls in CrossBenchCLI.BENCHMARKS:
      subcommands = (benchmark_cls.NAME,) + benchmark_cls.aliases()
      for subcommand in subcommands:
        with self.assertRaises(SysExitTestException) as cm:
          self.run_cli(subcommand, "help")
        self.assertEqual(cm.exception.exit_code, 0)
        _, stdout, stderr = self.run_cli_output(
            subcommand, "help", raises=SysExitTestException)
        self.assertFalse(stderr)
        self.assertIn("--env-validation ENV_VALIDATION", stdout)

  def test_subcommand_describe_subcommand(self):
    for benchmark_cls in CrossBenchCLI.BENCHMARKS:
      subcommands = (benchmark_cls.NAME,) + benchmark_cls.aliases()
      for subcommand in subcommands:
        with self.assertRaises(SysExitTestException) as cm:
          self.run_cli(subcommand, "describe")
        self.assertEqual(cm.exception.exit_code, 0)
        _, stdout, stderr = self.run_cli_output(
            subcommand, "describe", raises=SysExitTestException)
        output = stderr + stdout
        self.assertIn("See `describe benchmark ", output)

  def test_version(self):
    with self.assertRaises(SysExitTestException) as cm:
      self.run_cli("--version")
    self.assertEqual(cm.exception.exit_code, 0)
    _, stdout, stderr = self.run_cli_output(
        "--version", raises=SysExitTestException)
    self.assertFalse(stderr)
    self.assertIn(__version__, stdout)

  def test_version_subcommand(self):
    with self.assertRaises(SysExitTestException) as cm:
      self.run_cli("version")
    self.assertEqual(cm.exception.exit_code, 0)
    _, stdout, stderr = self.run_cli_output(
        "version", raises=SysExitTestException)
    self.assertFalse(stderr)
    self.assertIn(__version__, stdout)

  def test_subcommand_run_subcommand(self):
    with self.patch_get_browser():
      url = "http://test.com"
      self.run_cli("loading", "run", f"--urls={url}", "--env-validation=skip",
                   "--throw")
      for browser in self.browsers:
        self.assertListEqual([url], browser.url_list[self.SPLASH_URLS_LEN:])

  def test_invalid_probe(self):
    with self.assertRaises(argparse.ArgumentError), self.patch_get_browser():
      self.run_cli("loading", "--probe=invalid_probe_name", "--throw")

  def test_basic_probe_setting(self):
    with self.patch_get_browser():
      url = "http://test.com"
      self.run_cli("loading", "--probe=v8.log", f"--urls={url}",
                   "--env-validation=skip", "--throw")
      for browser in self.browsers:
        self.assertListEqual([url], browser.url_list[self.SPLASH_URLS_LEN:])
        self.assertIn("--log-all", browser.js_flags)

  def test_invalid_empty_probe_config_file(self):
    config_file = pathlib.Path("/config.hjson")
    config_file.touch()
    with self.patch_get_browser():
      url = "http://test.com"
      with self.assertRaises(argparse.ArgumentError) as cm:
        self.run_cli("loading", f"--probe-config={config_file}",
                     f"--urls={url}", "--env-validation=skip", "--throw")
      message = str(cm.exception)
      self.assertIn("--probe-config", message)
      self.assertIn("empty", message)
      for browser in self.browsers:
        self.assertListEqual([], browser.url_list[self.SPLASH_URLS_LEN:])
        self.assertNotIn("--log", browser.js_flags)

  def test_empty_probe_config_file(self):
    config_file = pathlib.Path("/config.hjson")
    config_data = {"probes": {}}
    with config_file.open("w", encoding="utf-8") as f:
      hjson.dump(config_data, f)

    with self.patch_get_browser():
      url = "http://test.com"
      self.run_cli("loading", f"--probe-config={config_file}", f"--urls={url}",
                   "--env-validation=skip")
      for browser in self.browsers:
        self.assertListEqual([url], browser.url_list[self.SPLASH_URLS_LEN:])
        self.assertNotIn("--log", browser.js_flags)

  def test_invalid_probe_config_file(self):
    config_file = pathlib.Path("/config.hjson")
    config_data = {"probes": {"invalid probe name": {}}}
    with config_file.open("w", encoding="utf-8") as f:
      hjson.dump(config_data, f)
    with self.patch_get_browser():
      url = "http://test.com"
      with self.assertRaises(argparse.ArgumentTypeError):
        self.run_cli("loading", f"--probe-config={config_file}",
                     f"--urls={url}", "--env-validation=skip", "--throw")
      for browser in self.browsers:
        self.assertListEqual([], browser.url_list)
        self.assertEqual(len(browser.js_flags), 0)

  def test_probe_config_file(self):
    config_file = pathlib.Path("/config.hjson")
    js_flags = ["--log-foo", "--log-bar"]
    config_data = {"probes": {"v8.log": {"js_flags": js_flags}}}
    with config_file.open("w", encoding="utf-8") as f:
      hjson.dump(config_data, f)

    with self.patch_get_browser():
      url = "http://test.com"
      self.run_cli("loading", f"--probe-config={config_file}", f"--urls={url}",
                   "--env-validation=skip")
      for browser in self.browsers:
        self.assertListEqual([url], browser.url_list[self.SPLASH_URLS_LEN:])
        for flag in js_flags:
          self.assertIn(flag, browser.js_flags)

  def test_probe_config_file_invalid_probe(self):
    config_file = pathlib.Path("/config.hjson")
    config_data = {"probes": {"invalid probe name": {}}}
    with config_file.open("w", encoding="utf-8") as f:
      hjson.dump(config_data, f)
    with self.assertRaises(
        argparse.ArgumentTypeError) as cm, self.patch_get_browser():
      self.run_cli("loading", f"--probe-config={config_file}",
                   "--urls=http://test.com", "--env-validation=skip", "--throw")
    self.assertIn("invalid probe name", str(cm.exception))

  def test_empty_config_file_properties(self):
    config_file = pathlib.Path("/config.hjson")
    config_data = {"probes": {}, "env": {}, "browsers": {}}
    with config_file.open("w", encoding="utf-8") as f:
      hjson.dump(config_data, f)
    with self.assertRaises(
        argparse.ArgumentTypeError) as cm, self.patch_get_browser():
      url = "http://test.com"
      self.run_cli("loading", f"--config={config_file}", f"--urls={url}",
                   "--env-validation=skip", "--throw")
    self.assertIn("no config properties", str(cm.exception))

  def test_empty_config_files(self):
    config_file = pathlib.Path("/config.hjson")
    config_data = {}
    with config_file.open("w", encoding="utf-8") as f:
      hjson.dump(config_data, f)
    with self.assertRaises(
        argparse.ArgumentTypeError) as cm, self.patch_get_browser():
      url = "http://test.com"
      self.run_cli("loading", f"--config={config_file}", f"--urls={url}",
                   "--env-validation=skip", "--throw")
    self.assertIn("no config properties", str(cm.exception))

  def test_conflicting_config_flags(self):
    config_file = pathlib.Path("/config.hjson")
    config_data = {"probes": {}, "env": {}, "browsers": {}}
    for config_flag in ("--probe-config", "--env-config", "--browser-config"):
      with config_file.open("w", encoding="utf-8") as f:
        hjson.dump(config_data, f)
      with self.assertRaises(argparse.ArgumentTypeError) as cm:
        self.run_cli("sp2", f"--config={config_file}",
                     f"{config_flag}={config_file}", "--env-validation=skip",
                     "--throw")
      message = str(cm.exception)
      self.assertIn("--config", message)
      self.assertIn(config_flag, message)

  def test_config_file_with_probe(self):
    config_file = pathlib.Path("/config.hjson")
    js_flags = ["--log-foo", "--log-bar"]
    config_data = {
        "probes": {
            "v8.log": {
                "js_flags": js_flags
            }
        },
        "env": {},
        "browsers": {}
    }
    with config_file.open("w", encoding="utf-8") as f:
      hjson.dump(config_data, f)

    with self.patch_get_browser():
      url = "http://test.com"
      self.run_cli("loading", f"--config={config_file}", f"--urls={url}",
                   "--env-validation=skip")
      for browser in self.browsers:
        self.assertListEqual([url], browser.url_list[self.SPLASH_URLS_LEN:])
        for flag in js_flags:
          self.assertIn(flag, browser.js_flags)

  def test_config_file_with_env(self):
    config_file = pathlib.Path("/config.hjson")
    config_data = {
        "probes": {},
        "env": {
            "screen_brightness_percent": 66,
            "cpu_max_usage_percent": 77,
        },
        "browsers": {}
    }
    with config_file.open("w", encoding="utf-8") as f:
      hjson.dump(config_data, f)

    with self.patch_get_browser():
      url = "http://test.com"
      cli = self.run_cli("loading", f"--config={config_file}", f"--urls={url}",
                         "--env-validation=skip")
      for browser in self.browsers:
        self.assertListEqual([url], browser.url_list[self.SPLASH_URLS_LEN:])
        self.assertFalse(browser.js_flags)
      config = cli.runner.env.config
      self.assertEqual(config.disk_min_free_space_gib,
                       HostEnvironmentConfig.IGNORE)
      self.assertEqual(config.screen_brightness_percent, 66)
      self.assertEqual(config.cpu_max_usage_percent, 77)

  def test_config_file_with_browser(self):
    config_file = pathlib.Path("/config.hjson")
    config_data = {
        "probes": {},
        "env": {},
        "browsers": {
            "browser_1": {
                "path": "chrome-dev",
            },
            "browser_2": {
                "path": "chrome-stable"
            }
        }
    }
    with config_file.open("w", encoding="utf-8") as f:
      hjson.dump(config_data, f)

    def mock_get_browser_cls(browser_config: BrowserConfig):
      path_str = str(browser_config.path).lower()
      if "dev" in path_str:
        return mock_browser.MockChromeDev
      return mock_browser.MockChromeStable

    with mock.patch.object(
        BrowserVariantsConfig,
        "_get_browser_cls",
        side_effect=mock_get_browser_cls):
      url = "http://test.com"
      cli = self.run_cli("loading", f"--config={config_file}", f"--urls={url}",
                         "--env-validation=skip")
      browsers = cli.runner.browsers
      self.assertEqual(len(browsers), 2)
      self.assertEqual(browsers[0].label, "browser_1")
      self.assertEqual(browsers[1].label, "browser_2")
      for browser in browsers:
        self.assertFalse(browser.js_flags)

  def test_invalid_browser_identifier(self):
    with self.assertRaises(argparse.ArgumentError) as cm:
      self.run_cli("loading", "--browser=unknown_browser_identifier",
                   "--urls=http://test.com", "--env-validation=skip", "--throw")
    self.assertIn("--browser", str(cm.exception))
    self.assertIn("unknown_browser_identifier", str(cm.exception))

  def test_unknown_browser_binary(self):
    browser_bin = pathlib.Path("/foo/custom/browser.bin")
    browser_bin.parent.mkdir(parents=True)
    browser_bin.touch()
    with self.assertRaises(argparse.ArgumentError) as cm:
      self.run_cli("loading", f"--browser={browser_bin}",
                   "--urls=http://test.com", "--env-validation=skip", "--throw")
    self.assertIn("--browser", str(cm.exception))
    self.assertIn(str(browser_bin), str(cm.exception))

  @unittest.skipUnless(plt.PLATFORM.is_win, "Can only run on windows")
  def test_unknown_browser_binary_win(self):
    browser_bin = pathlib.Path("C:\\foo\\custom\\browser.bin")
    browser_bin.parent.mkdir(parents=True)
    browser_bin.touch()
    with self.assertRaises(argparse.ArgumentError) as cm:
      self.run_cli("loading", f"--browser={browser_bin}",
                   "--urls=http://test.com", "--env-validation=skip", "--throw")
    self.assertIn("--browser", str(cm.exception))
    self.assertIn(str(browser_bin), str(cm.exception))

  def test_custom_chrome_browser_binary(self):
    if self.platform.is_win:
      self.skipTest("No auto-download available on windows")
    browser_cls = mock_browser.MockChromeStable
    # TODO: migrate to with_stem once python 3.9 is available everywhere
    suffix = browser_cls.APP_PATH.suffix
    browser_bin = browser_cls.APP_PATH.with_name(
        f"Custom Google Chrome{suffix}")
    browser_cls.setup_bin(self.fs, browser_bin, "Chrome")

    with mock.patch.object(
        BrowserVariantsConfig, "_get_browser_cls",
        return_value=browser_cls) as get_browser_cls:
      self.run_cli("loading", f"--browser={browser_bin}",
                   "--urls=http://test.com", "--env-validation=skip")
    get_browser_cls.assert_called_once_with(
        BrowserConfig(browser_bin, DriverConfig.default()))

  def test_custom_chrome_browser_binary_custom_flags(self):
    if self.platform.is_win:
      self.skipTest("No auto-download available on windows")
    browser_cls = mock_browser.MockChromeStable
    # TODO: migrate to with_stem once python 3.9 is available everywhere
    suffix = browser_cls.APP_PATH.suffix
    browser_bin = browser_cls.APP_PATH.with_name(
        f"Custom Google Chrome{suffix}")
    browser_cls.setup_bin(self.fs, browser_bin, "Chrome")

    with mock.patch.object(
        BrowserVariantsConfig,
        "_get_browser_cls", return_value=browser_cls), mock.patch.object(
            CrossBenchCLI, "_run_benchmark") as run_benchmark:
      self.run_cli("loading", f"--browser={browser_bin}",
                   "--urls=http://test.com", "--env-validation=skip", "--",
                   "--chrome-flag1=value1", "--chrome-flag2")
    run_benchmark.assert_called_once()
    runner = run_benchmark.call_args[0][1]
    self.assertIsInstance(runner, Runner)
    self.assertEqual(len(runner.browsers), 1)
    browser = runner.browsers[0]
    self.assertListEqual(["--chrome-flag1=value1", "--chrome-flag2"],
                         list(browser.flags))

  def test_browser_identifiers(self):
    browsers: Dict[str, Type[mock_browser.MockBrowser]] = {
        "chrome": mock_browser.MockChromeStable,
        "chrome-stable": mock_browser.MockChromeStable,
        "chr-stable": mock_browser.MockChromeStable,
        "chrome-beta": mock_browser.MockChromeBeta,
        "chr-beta": mock_browser.MockChromeBeta,
        "chrome-dev": mock_browser.MockChromeDev,
        "edge": mock_browser.MockEdgeStable,
        "edge-stable": mock_browser.MockEdgeStable,
        "edge-beta": mock_browser.MockEdgeBeta,
        "edge-dev": mock_browser.MockEdgeDev,
        "ff": mock_browser.MockFirefox,
        "firefox": mock_browser.MockFirefox,
        "firefox-dev": mock_browser.MockFirefoxDeveloperEdition,
        "firefox-developer-edition": mock_browser.MockFirefoxDeveloperEdition,
        "ff-dev": mock_browser.MockFirefoxDeveloperEdition,
        "firefox-nightly": mock_browser.MockFirefoxNightly,
        "ff-nightly": mock_browser.MockFirefoxNightly,
        "ff-trunk": mock_browser.MockFirefoxNightly,
    }
    if not self.platform.is_linux:
      browsers["chr-canary"] = mock_browser.MockChromeCanary
      browsers["chrome-canary"] = mock_browser.MockChromeCanary
      browsers["edge-canary"] = mock_browser.MockEdgeCanary
    if self.platform.is_macos:
      browsers.update({
          "safari": mock_browser.MockSafari,
          "sf": mock_browser.MockSafari,
          "safari-technology-preview": mock_browser.MockSafariTechnologyPreview,
          "sf-tp": mock_browser.MockSafariTechnologyPreview,
          "tp": mock_browser.MockSafariTechnologyPreview,
      })

    for identifier, browser_cls in browsers.items():
      out_dir = self.out_dir / identifier
      self.assertFalse(out_dir.exists())
      with mock.patch.object(
          BrowserVariantsConfig, "_get_browser_cls",
          return_value=browser_cls) as get_browser_cls:
        url = "http://test.com"
        self.run_cli("loading", f"--browser={identifier}", f"--urls={url}",
                     "--env-validation=skip", f"--out-dir={out_dir}")
        self.assertTrue(out_dir.exists())
        get_browser_cls.assert_called_once()
        result_files = list(
            out_dir.glob(f"**/{internal.ResultsSummaryProbe.NAME}.json"))
        result_file = result_files[1]
        with result_file.open(encoding="utf-8") as f:
          results = json.load(f)
        self.assertEqual(results["browser"]["version"], browser_cls.VERSION)
        self.assertIn("test.com", results["stories"])

  def test_browser_identifiers_duplicate(self):
    with self.assertRaises(argparse.ArgumentTypeError):
      self.run_cli("loading", "--browser=chrome", "--browser=chrome",
                   "--urls=http://test.com", "--env-validation=skip", "--throw")

  def test_browser_identifiers_multiple(self):
    mock_browsers: List[Type[mock_browser.MockBrowser]] = [
        mock_browser.MockChromeStable,
        mock_browser.MockChromeBeta,
        mock_browser.MockChromeDev,
    ]

    def mock_get_browser_cls(browser_config: BrowserConfig):
      self.assertEqual(browser_config.driver.type, BrowserDriverType.WEB_DRIVER)
      for mock_browser_cls in mock_browsers:
        if mock_browser_cls.APP_PATH == browser_config.path:
          return mock_browser_cls
      raise ValueError("Unknown browser path")

    with mock.patch.object(
        BrowserVariantsConfig,
        "_get_browser_cls",
        side_effect=mock_get_browser_cls) as get_browser_cls:
      url = "http://test.com"
      self.run_cli("loading", "--browser=chrome-beta",
                   "--browser=chrome-stable", "--browser=chrome-dev",
                   f"--urls={url}", "--env-validation=skip",
                   f"--out-dir={self.out_dir}")
      self.assertTrue(self.out_dir.exists())
      get_browser_cls.assert_called()
      # Example:  BROWSER / "cb.results.json"
      result_files = list(
          self.out_dir.glob(f"*/*/{internal.ResultsSummaryProbe.NAME}.json"))
      self.assertEqual(len(result_files), 3)
      versions = []
      for result_file in result_files:
        with result_file.open(encoding="utf-8") as f:
          results = json.load(f)
        versions.append(results["browser"]["version"])
        self.assertIn("test.com", results["stories"])
      self.assertTrue(len(set(versions)), 3)
      for mock_browser_cls in mock_browsers:
        self.assertIn(mock_browser_cls.VERSION, versions)

  def test_browser_identifiers_multiple_same_major_version(self):

    class MockChromeBeta2(mock_browser.MockChromeBeta):
      VERSION = "100.22.33.100"

    class MockChromeDev2(mock_browser.MockChromeDev):
      VERSION = "100.22.33.200"

    mock_browsers: List[Type[mock_browser.MockBrowser]] = [
        MockChromeBeta2,
        MockChromeDev2,
    ]

    def mock_get_browser_cls(browser_config: BrowserConfig):
      self.assertEqual(browser_config.driver.type, BrowserDriverType.WEB_DRIVER)
      for mock_browser_cls in mock_browsers:
        if mock_browser_cls.APP_PATH == browser_config.path:
          return mock_browser_cls
      raise ValueError("Unknown browser path")

    with mock.patch.object(
        BrowserVariantsConfig,
        "_get_browser_cls",
        side_effect=mock_get_browser_cls) as get_browser_cls:
      url = "http://test.com"
      self.run_cli("loading", "--browser=chrome-dev", "--browser=chrome-beta",
                   f"--urls={url}", "--env-validation=skip",
                   f"--out-dir={self.out_dir}")
      self.assertTrue(self.out_dir.exists())
      get_browser_cls.assert_called()
      # Example:  BROWSER / "cb.results.json"
      result_files = list(
          self.out_dir.glob(f"*/*/{internal.ResultsSummaryProbe.NAME}.json"))
      self.assertEqual(len(result_files), 2)
      versions = []
      for result_file in result_files:
        with result_file.open(encoding="utf-8") as f:
          results = json.load(f)
        versions.append(results["browser"]["version"])
        self.assertIn("test.com", results["stories"])
      self.assertTrue(len(set(versions)), 2)
      for mock_browser_cls in mock_browsers:
        self.assertIn(mock_browser_cls.VERSION, versions)

  def test_browser_identifiers_multiple_same_version(self):

    class MockChromeBeta2(mock_browser.MockChromeBeta):
      VERSION = "100.22.33.999"

    class MockChromeDev2(mock_browser.MockChromeDev):
      VERSION = "100.22.33.999"

    mock_browsers: List[Type[mock_browser.MockBrowser]] = [
        MockChromeBeta2,
        MockChromeDev2,
    ]

    def mock_get_browser_cls(browser_config: BrowserConfig):
      self.assertEqual(browser_config.driver.type, BrowserDriverType.WEB_DRIVER)
      for mock_browser_cls in mock_browsers:
        if mock_browser_cls.APP_PATH == browser_config.path:
          return mock_browser_cls
      raise ValueError("Unknown browser path")

    with mock.patch.object(
        BrowserVariantsConfig,
        "_get_browser_cls",
        side_effect=mock_get_browser_cls) as get_browser_cls:
      url = "http://test.com"
      self.run_cli("loading", "--browser=chrome-dev", "--browser=chrome-beta",
                   f"--urls={url}", "--env-validation=skip",
                   f"--out-dir={self.out_dir}")
      self.assertTrue(self.out_dir.exists())
      get_browser_cls.assert_called()
      # Example:  BROWSER / "cb.results.json"
      result_files = list(
          self.out_dir.glob(f"*/*/{internal.ResultsSummaryProbe.NAME}.json"))
      self.assertEqual(len(result_files), 2)
      versions = []
      for result_file in result_files:
        with result_file.open(encoding="utf-8") as f:
          results = json.load(f)
        versions.append(results["browser"]["version"])
        self.assertIn("test.com", results["stories"])
      self.assertTrue(len(set(versions)), 1)
      for mock_browser_cls in mock_browsers:
        self.assertIn(mock_browser_cls.VERSION, versions)

  def test_browser_different_drivers(self):

    def mock_get_browser_cls(browser_config: BrowserConfig):
      if browser_config.driver.type == BrowserDriverType.IOS:
        self.assertEqual(browser_config.path,
                         mock_browser.MockChromeStable.APP_PATH)
        return mock_browser.MockChromeStable
      if browser_config.driver.type == BrowserDriverType.WEB_DRIVER:
        self.assertEqual(browser_config.path,
                         mock_browser.MockChromeBeta.APP_PATH)
        return mock_browser.MockChromeBeta
      self.assertEqual(browser_config.driver.type,
                       BrowserDriverType.APPLE_SCRIPT)
      self.assertEqual(browser_config.path, mock_browser.MockChromeDev.APP_PATH)
      return mock_browser.MockChromeDev

    self.platform.sh_results.append(XCTRACE_DEVICES_SINGLE_OUTPUT)
    with mock.patch.object(
        BrowserVariantsConfig,
        "_get_browser_cls",
        side_effect=mock_get_browser_cls) as get_browser_cls:
      url = "http://test.com"
      self.run_cli("loading", "--browser=ios:chrome-stable",
                   "--browser=selenium:chrome-beta",
                   "--browser=applescript:chrome-dev", f"--urls={url}",
                   "--env-validation=skip", f"--out-dir={self.out_dir}")
      self.assertTrue(self.out_dir.exists())
      get_browser_cls.assert_called()
      # Example:  BROWSER / "cb.results.json"
      result_files = list(
          self.out_dir.glob(f"*/*/{internal.ResultsSummaryProbe.NAME}.json"))
      self.assertEqual(len(result_files), 3)
      versions = []
      for result_file in result_files:
        with result_file.open(encoding="utf-8") as f:
          results = json.load(f)
        versions.append(results["browser"]["version"])
        self.assertIn("test.com", results["stories"])
      self.assertTrue(len(set(versions)), 1)
      self.assertIn(mock_browser.MockChromeStable.VERSION, versions)
      self.assertIn(mock_browser.MockChromeBeta.VERSION, versions)
      self.assertIn(mock_browser.MockChromeDev.VERSION, versions)

  def test_probe_invalid_inline_json_config(self):
    with self.assertRaises(
        argparse.ArgumentError) as cm, self.patch_get_browser():
      self.run_cli("loading", "--probe=v8.log{invalid json: d a t a}",
                   "--urls=cnn", "--env-validation=skip", "--throw")
    message = str(cm.exception)
    self.assertIn("{invalid json: d a t a}", message)

  def test_probe_empty_inline_json_config(self):
    js_flags = ["--log-foo", "--log-bar"]
    with self.patch_get_browser():
      url = "http://test.com"
      self.run_cli("loading", "--probe=v8.log{}", f"--urls={url}",
                   "--env-validation=skip")
      for browser in self.browsers:
        self.assertListEqual([url], browser.url_list[self.SPLASH_URLS_LEN:])
        for flag in js_flags:
          self.assertNotIn(flag, browser.js_flags)

  def test_probe_inline_json_config(self):
    js_flags = ["--log-foo", "--log-bar"]
    json_config = json.dumps({"js_flags": js_flags})
    with self.patch_get_browser():
      url = "http://test.com"
      self.run_cli("loading", f"--probe=v8.log{json_config}", f"--urls={url}",
                   "--env-validation=skip")
      for browser in self.browsers:
        self.assertListEqual([url], browser.url_list[self.SPLASH_URLS_LEN:])
        for flag in js_flags:
          self.assertIn(flag, browser.js_flags)

  def test_env_config_name(self):
    with self.patch_get_browser():
      self.run_cli("loading", "--env=strict", "--urls=http://test.com",
                   "--env-validation=skip", "--throw")

  def test_env_config_inline_hjson(self):
    with self.patch_get_browser():
      self.run_cli("loading", "--env={\"power_use_battery\":false}",
                   "--urls=http://test.com", "--env-validation=skip")

  def test_env_config_inline_invalid(self):
    with self.assertRaises(SysExitTestException):
      self.run_cli("loading", "--env=not a valid name",
                   "--urls=http://test.com", "--env-validation=skip")
    with self.assertRaises(SysExitTestException):
      self.run_cli("loading", "--env={not valid hjson}",
                   "--urls=http://test.com", "--env-validation=skip")
    with self.assertRaises(SysExitTestException):
      self.run_cli("loading", "--env={unknown_property:1}",
                   "--urls=http://test.com", "--env-validation=skip")

  def test_conflicting_driver_path(self):
    mock_browsers: List[Type[mock_browser.MockBrowser]] = [
        mock_browser.MockChromeStable,
        mock_browser.MockFirefox,
    ]

    def mock_get_browser_cls(browser_config: BrowserConfig):
      self.assertEqual(browser_config.driver.type, BrowserDriverType.WEB_DRIVER)
      for mock_browser_cls in mock_browsers:
        if mock_browser_cls.APP_PATH == browser_config.path:
          return mock_browser_cls
      raise ValueError("Unknown browser path")

    driver_path = self.out_dir / "driver"
    self.fs.create_file(driver_path, st_size=1024)
    with self.assertRaises(cli_helper.LateArgumentError) as cm:
      with mock.patch.object(
          BrowserVariantsConfig,
          "_get_browser_cls",
          side_effect=mock_get_browser_cls):
        self.run_cli("loading", "--browser=chrome", "--browser=firefox",
                     f"--driver-path={driver_path}", "--urls=http://test.com",
                     "--env-validation=skip", "--throw")
    self.assertIn("--driver-path", str(cm.exception))

  def test_env_config_invalid_file(self):
    config = pathlib.Path("/test.config.hjson")
    # No "env" property
    with config.open("w", encoding="utf-8") as f:
      hjson.dump({}, f)
    with self.assertRaises(SysExitTestException):
      self.run_cli("loading", f"--env-config={config}",
                   "--urls=http://test.com", "--env-validation=skip")
    # "env" not a dict
    with config.open("w", encoding="utf-8") as f:
      hjson.dump({"env": []}, f)
    with self.assertRaises(SysExitTestException):
      self.run_cli("loading", f"--env-config={config}",
                   "--urls=http://test.com", "--env-validation=skip")
    with config.open("w", encoding="utf-8") as f:
      hjson.dump({"env": {"unknown_property_name": 1}}, f)
    with self.assertRaises(SysExitTestException):
      self.run_cli("loading", f"--env-config={config}",
                   "--urls=http://test.com", "--env-validation=skip")

  def test_multiple_browser_compatible_flags(self):
    mock_browsers: List[Type[mock_browser.MockBrowser]] = [
        mock_browser.MockChromeStable,
        mock_browser.MockFirefox,
        mock_browser.MockChromeDev,
    ]

    def mock_get_browser_cls(browser_config: BrowserConfig):
      self.assertEqual(browser_config.driver.type, BrowserDriverType.WEB_DRIVER)
      for mock_browser_cls in mock_browsers:
        if mock_browser_cls.APP_PATH == browser_config.path:
          return mock_browser_cls
      raise ValueError("Unknown browser path")

    for chrome_flag in ("--js-flags=--no-opt", "--enable-features=Foo",
                        "--disable-features=bar"):
      # Fail for chrome flags for non-chrome browser
      with self.assertRaises(argparse.ArgumentTypeError), mock.patch.object(
          BrowserVariantsConfig,
          "_get_browser_cls",
          side_effect=mock_get_browser_cls):
        self.run_cli("loading", "--urls=http://test.com",
                     "--env-validation=skip", "--throw", "--browser=firefox",
                     chrome_flag)
      # Fail for mixed browsers and chrome flags
      with self.assertRaises(argparse.ArgumentTypeError), mock.patch.object(
          BrowserVariantsConfig,
          "_get_browser_cls",
          side_effect=mock_get_browser_cls):
        self.run_cli("loading", "--urls=http://test.com",
                     "--env-validation=skip", "--throw", "--browser=chrome",
                     "--browser=firefox", chrome_flag)
      with self.assertRaises(argparse.ArgumentTypeError), mock.patch.object(
          BrowserVariantsConfig,
          "_get_browser_cls",
          side_effect=mock_get_browser_cls):
        self.run_cli("loading", "--urls=http://test.com",
                     "--env-validation=skip", "--throw", "--browser=chrome",
                     "--browser=firefox", "--", chrome_flag)
    # Flags for the same type are allowed.
    with self.patch_get_browser():
      self.run_cli("loading", "--urls=http://test.com", "--env-validation=skip",
                   "--throw", "--browser=chrome", "--browser=chrome-dev", "--",
                   "--js-flags=--no-opt")

  def test_parse_env_config_file(self):
    config = pathlib.Path("/test.config.hjson")
    with config.open("w", encoding="utf-8") as f:
      hjson.dump({"env": {}}, f)
    with self.patch_get_browser():
      self.run_cli("loading", f"--env-config={config}",
                   "--urls=http://test.com", "--env-validation=skip")

  def test_env_invalid_inline_and_file(self):
    config = pathlib.Path("/test.config.hjson")
    with config.open("w", encoding="utf-8") as f:
      hjson.dump({"env": {}}, f)
    with self.assertRaises(SysExitTestException):
      self.run_cli("loading", "--env=strict", f"--env-config={config}",
                   "--urls=http://test.com", "--env-validation=skip")

  def test_invalid_splashscreen(self):
    with self.assertRaises(argparse.ArgumentError) as cm:
      self.run_cli("loading", "--browser=chrome", "--urls=http://test.com",
                   "--env-validation=skip", "--splash-screen=unknown-value",
                   "--throw")
    message = str(cm.exception)
    self.assertIn("--splash-screen", message)
    self.assertIn("unknown-value", message)

  def test_splash_screen_none(self):
    with self.mock_chrome_stable():
      url = "http://test.com"
      cli = self.run_cli("loading", f"--urls={url}", "--env-validation=skip",
                         "--throw", "--splash-screen=none")
      for browser in cli.runner.browsers:
        assert isinstance(browser, mock_browser.MockChromeStable)
        self.assertEqual(browser.splash_screen, splash_screen.SplashScreen.NONE)
        self.assertListEqual([url], browser.url_list)
        self.assertEqual(len(browser.js_flags), 0)

  def test_splash_screen_minimal(self):
    with self.mock_chrome_stable():
      url = "http://test.com"
      cli = self.run_cli("loading", f"--urls={url}", "--env-validation=skip",
                         "--throw", "--splash-screen=minimal")
      for browser in cli.runner.browsers:
        assert isinstance(browser, mock_browser.MockChromeStable)
        self.assertEqual(browser.splash_screen,
                         splash_screen.SplashScreen.MINIMAL)
        self.assertEqual(len(browser.url_list), 3)
        self.assertIn(url, browser.url_list)
        self.assertEqual(len(browser.js_flags), 0)

  def test_splash_screen_url(self):
    with self.mock_chrome_stable():
      splash_url = "http://splash.com"
      url = "http://test.com"
      cli = self.run_cli("loading", f"--urls={url}", "--env-validation=skip",
                         "--throw", f"--splash-screen={splash_url}")
      for browser in cli.runner.browsers:
        assert isinstance(browser, mock_browser.MockChromeStable)
        self.assertIsInstance(browser.splash_screen,
                              splash_screen.URLSplashScreen)
        self.assertEqual(len(browser.url_list), 3)
        self.assertEqual(splash_url, browser.url_list[0])
        self.assertEqual(len(browser.js_flags), 0)

  def test_viewport_invalid(self):
    with self.assertRaises(argparse.ArgumentError) as cm:
      self.run_cli("loading", "--browser=chrome", "--urls=http://test.com",
                   "--env-validation=skip", "--viewport=-123", "--throw")
    message = str(cm.exception)
    self.assertIn("--viewport", message)
    self.assertIn("-123", message)

  def test_viewport_maximized(self):
    with self.mock_chrome_stable():
      url = "http://test.com"
      cli = self.run_cli("loading", f"--urls={url}", "--env-validation=skip",
                         "--throw", "--viewport=maximized")
      for browser in cli.runner.browsers:
        assert isinstance(browser, mock_browser.MockChromeStable)
        self.assertEqual(browser.viewport, viewport.Viewport.MAXIMIZED)
        self.assertEqual(len(browser.url_list), 3)
        self.assertEqual(len(browser.js_flags), 0)

  def test_powersampler_invalid_multiple_runs(self):
    powersampler_bin = self.out_dir / "powersampler"
    self.fs.create_file(powersampler_bin, st_size=1024)
    config_str = json.dumps({"bin_path": str(powersampler_bin)})
    with self.mock_chrome_stable():
      with self.assertRaises(argparse.ArgumentTypeError) as cm:
        self.run_cli("loading", "--browser=chrome",
                     f"--probe=powersampler:{config_str}", "--repeat=10",
                     "--urls=http://test.com", "--env-validation=skip",
                     "--throw")
      self.assertIn("powersampler", str(cm.exception))

  def test_fast(self):
    with self.mock_chrome_stable():
      url = "http://test.com"
      cli = self.run_cli("loading", f"--urls={url}", "--throw", "--fast")
      self.assertEqual(cli.args.splash_screen, splash_screen.SplashScreen.NONE)
      self.assertEqual(cli.args.cool_down_time, dt.timedelta(0))
      self.assertEqual(cli.args.env_validation, ValidationMode.SKIP)
      for browser in cli.runner.browsers:
        assert isinstance(browser, mock_browser.MockChromeStable)
        self.assertIs(browser.splash_screen, splash_screen.SplashScreen.NONE)
        self.assertListEqual(browser.url_list, [url])
        self.assertEqual(len(browser.js_flags), 0)

  def test_debug(self):
    with self.mock_chrome_stable():
      url = "http://test.com"
      cli = self.run_cli("loading", f"--urls={url}", "--debug")
      self.assertTrue(cli.args.throw)
      self.assertEqual(cli.args.verbosity, 3)
      for browser in cli.runner.browsers:
        assert isinstance(browser, mock_browser.MockChromeStable)
        self.assertEqual(len(browser.url_list), 3)
        self.assertEqual(len(browser.js_flags), 0)

  def test_debugger_not_found(self):
    for debugger in ("lldb", "gdb", "lldb"):
      searched_binaries = []
      original_search_binary = plt.PLATFORM.search_binary

      def mock_search_binary(binary) -> Optional[RemotePath]:
        searched_binaries.append(binary)
        if "gdb" in str(binary) or "lldb" in str(binary):
          return None
        return original_search_binary(binary)

      with self.mock_chrome_stable(), mock.patch.object(
          plt.PLATFORM, "search_binary", side_effect=mock_search_binary):
        with self.assertRaises(ValueError) as cm:
          self.run_cli("loading", "--urls=cnn", f"--{debugger}", "--throw")
        self.assertIn(debugger, str(cm.exception))
        _, _, stderr = self.run_cli_output(
            "loading",
            "--urls=cnn",
            f"--{debugger}",
            raises=SysExitTestException)
        self.assertIn(f"Unknown binary: {debugger}", stderr)
        self.assertIn(pathlib.Path(debugger), searched_binaries)

if __name__ == "__main__":
  test_helper.run_pytest(__file__)
