# Copyright 2023 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import copy
import unittest
from typing import Dict, Tuple, Type
from unittest import mock

import hjson
from immutabledict import immutabledict

from crossbench import path as pth
from crossbench import plt
from crossbench.browsers.chrome.chrome import Chrome
from crossbench.browsers.chrome.webdriver import ChromeWebDriver
from crossbench.browsers.safari.safari import Safari
from crossbench.cli.config.browser import BrowserConfig
from crossbench.cli.config.browser_variants import (BrowserVariantsConfig,
                                                    FlagsConfig,
                                                    FlagsGroupConfig,
                                                    FlagsVariantConfig)
from crossbench.cli.config.driver import (AmbiguousDriverIdentifier,
                                          BrowserDriverType, DriverConfig)
from crossbench.cli.config.network import (NetworkConfig, NetworkSpeedConfig,
                                           NetworkSpeedPreset, NetworkType)
from crossbench.cli.config.probe import ProbeConfig, ProbeListConfig
from crossbench.config import ConfigError
from crossbench.exception import ArgumentTypeMultiException, MultiException
from crossbench.flags import Flags
from crossbench.probes.power_sampler import PowerSamplerProbe
from crossbench.probes.v8.log import V8LogProbe
from crossbench.types import JsonDict
from tests import test_helper
from tests.crossbench import mock_browser
from tests.crossbench.mock_helper import (AndroidAdbMockPlatform,
                                          BaseCrossbenchTestCase,
                                          CrossbenchFakeFsTestCase, MockAdb)

ADB_DEVICES_SINGLE_OUTPUT = """List of devices attached
emulator-5556 device product:sdk_google_phone_x86_64 model:Android_SDK_built_for_x86_64 device:generic_x86_64"""

ADB_DEVICES_OUTPUT = f"""{ADB_DEVICES_SINGLE_OUTPUT}
emulator-5554 device product:sdk_google_phone_x86 model:Android_SDK_built_for_x86 device:generic_x86
0a388e93      device usb:1-1 product:razor model:Nexus_7 device:flo"""

XCTRACE_DEVICES_SINGLE_OUTPUT = """
== Devices ==
a-macbookpro3 (00001234-AAAA-BBBB-0000-11AA22BB33DD)
An iPhone (17.1.2) (00001111-11AA22BB33DD)

== Devices Offline ==
An iPhone Pro (17.1.1) (00002222-11AA22BB33DD)

== Simulators ==
iPad (10th generation) (17.0.1) (00001234-AAAA-BBBB-1111-11AA22BB33DD)
iPad (9th generation) Simulator (15.5) (00001234-AAAA-BBBB-2222-11AA22BB33DD
"""

XCTRACE_DEVICES_OUTPUT = """
== Devices ==
a-macbookpro3 (00001234-AAAA-BBBB-0000-11AA22BB33DD)
An iPhone (17.1.2) (00001111-11AA22BB33DD)
An iPhone Pro (17.1.1) (00002222-11AA22BB33DD)

== Devices Offline ==
An iPhone Pro Max (17.1.0) (00003333-11AA22BB33DD)

== Simulators ==
iPad (10th generation) (17.0.1) (00001234-AAAA-BBBB-1111-11AA22BB33DD)
iPad (9th generation) Simulator (15.5) (00001234-AAAA-BBBB-2222-11AA22BB33DD
"""


class BaseConfigTestCase(BaseCrossbenchTestCase):

  def setUp(self) -> None:
    super().setUp()
    adb_patcher = mock.patch(
        "crossbench.plt.android_adb._find_adb_bin",
        return_value=pth.LocalPath("adb"))
    adb_patcher.start()
    self.addCleanup(adb_patcher.stop)

  def mock_chrome_stable(self, browser_cls: Type[mock_browser.MockBrowser]):
    return mock.patch.object(
        BrowserVariantsConfig, "_get_browser_cls", return_value=browser_cls)

class DriverConfigTestCase(BaseConfigTestCase):

  def test_default(self):
    default = DriverConfig.default()
    self.assertEqual(default.type, BrowserDriverType.WEB_DRIVER)
    self.assertTrue(default.is_local)
    self.assertFalse(default.is_remote)

  def test_parse_invalid(self):
    with self.assertRaises(argparse.ArgumentTypeError):
      _ = DriverConfig.parse("")
    with self.assertRaises(argparse.ArgumentTypeError):
      _ = DriverConfig.parse(":")
    with self.assertRaises(argparse.ArgumentTypeError):
      _ = DriverConfig.parse("{:}")
    with self.assertRaises(argparse.ArgumentTypeError):
      _ = DriverConfig.parse("}")
    with self.assertRaises(argparse.ArgumentTypeError):
      _ = DriverConfig.parse("{")

  def test_parse_path_invalid(self):
    driver_path = self.out_dir / "driver"
    with self.assertRaises(argparse.ArgumentTypeError) as cm:
      _ = DriverConfig.parse(str(driver_path))
    self.assertIn(str(driver_path), str(cm.exception))

    self.fs.create_file(driver_path)
    with self.assertRaises(argparse.ArgumentTypeError) as cm:
      _ = DriverConfig.parse(str(driver_path))
    message = str(cm.exception)
    self.assertIn(str(driver_path), message)
    self.assertIn("empty", message)

  def test_parse_dict_device_id_conflict(self):
    self.platform.sh_results = []
    config_dict = {
        "type": 'adb',
        "device_id": "1234",
        "settings": {
            "device_id": "ABCD"
        }
    }
    with self.assertRaises(argparse.ArgumentTypeError) as cm:
      DriverConfig.parse(config_dict)
    self.assertIn("1234", str(cm.exception))
    self.assertIn("ABCD", str(cm.exception))

  def test_parse_dict_chromeos_ssh(self):
    config_dict = {
        "type": "chromeos-ssh",
        "settings": {
            "host": "chromeos6-row17-rack14-host7",
            "port": "9515",
            "ssh_port": "22",
            "ssh_user": "root"
        }
    }
    self.platform.expect_sh(
        "ssh", "-p", config_dict["settings"]["ssh_port"],
        f"{config_dict['settings']['ssh_user']}@{config_dict['settings']['host']}",
        f"'[' -e {plt.ChromeOsSshPlatform.AUTOLOGIN_PATH} ']'")
    config = DriverConfig.parse(config_dict)
    assert isinstance(config, DriverConfig)
    self.assertEqual(config.type, BrowserDriverType.CHROMEOS_SSH)
    self.assertTrue(config.is_remote)
    self.assertFalse(config.is_local)
    platform = config.get_platform()
    assert isinstance(platform, plt.ChromeOsSshPlatform)
    self.assertEqual(platform.host, "chromeos6-row17-rack14-host7")
    self.assertEqual(platform.port, 9515)
    self.assertEqual(platform._ssh_port, 22)
    self.assertEqual(platform._ssh_user, "root")

  def test_parse_inline_json_adb(self):
    self.platform.sh_results = [ADB_DEVICES_OUTPUT]
    config_dict = {"type": 'adb', "settings": {"device_id": "0a388e93"}}
    config_1 = DriverConfig.parse(hjson.dumps(config_dict))
    assert isinstance(config_1, DriverConfig)
    self.assertEqual(config_1.type, BrowserDriverType.ANDROID)
    self.assertEqual(config_1.device_id, "0a388e93")
    self.assertEqual(config_1.settings["device_id"], "0a388e93")
    self.assertTrue(config_1.is_remote)
    self.assertFalse(config_1.is_local)

    self.platform.sh_results = [ADB_DEVICES_OUTPUT]
    config_2 = DriverConfig.load_dict(config_dict)
    assert isinstance(config_2, DriverConfig)
    self.assertEqual(config_2.type, BrowserDriverType.ANDROID)
    self.assertEqual(config_2.device_id, "0a388e93")
    self.assertEqual(config_2.settings["device_id"], "0a388e93")
    self.assertTrue(config_2.is_remote)
    self.assertFalse(config_2.is_local)
    self.assertEqual(config_1, config_2)

    self.platform.sh_results = [ADB_DEVICES_OUTPUT]
    config_dict = {"type": 'adb', "device_id": "0a388e93"}
    config_3 = DriverConfig.load_dict(config_dict)
    assert isinstance(config_3, DriverConfig)
    self.assertEqual(config_3.type, BrowserDriverType.ANDROID)
    self.assertEqual(config_3.device_id, "0a388e93")
    self.assertTrue(config_3.is_remote)
    self.assertFalse(config_3.is_local)
    self.assertIsNone(config_3.settings)
    self.assertNotEqual(config_1, config_3)
    self.assertNotEqual(config_2, config_3)

  def test_parse_adb_phone_identifier_unknown(self):
    self.platform.sh_results = [ADB_DEVICES_OUTPUT]
    if self.platform.is_macos:
      self.platform.sh_results.append(XCTRACE_DEVICES_SINGLE_OUTPUT)

    with self.assertRaises(argparse.ArgumentTypeError) as cm:
      _ = DriverConfig.parse("Unknown Device X")
    self.assertIn("Unknown Device X", str(cm.exception))

  def test_parse_adb_phone_identifier_multiple(self):
    self.platform.sh_results = [ADB_DEVICES_OUTPUT]
    with self.assertRaises(ArgumentTypeMultiException) as cm:
      _ = DriverConfig.parse("emulator.*")
    message: str = str(cm.exception)
    self.assertIn("emulator-5554", message)
    self.assertIn("emulator-5556", message)
    self.assertTrue(len(cm.exception), 1)
    self.assertTrue(cm.exception.matching(AmbiguousDriverIdentifier))
    self.assertEqual(len(self.platform.sh_cmds), 1)

  def test_parse_adb_phone_identifier(self):
    self.platform.sh_results = [ADB_DEVICES_OUTPUT, ADB_DEVICES_OUTPUT]

    config = DriverConfig.parse("Nexus_7")
    assert isinstance(config, DriverConfig)
    self.assertEqual(len(self.platform.sh_cmds), 2)

    self.assertEqual(config.type, BrowserDriverType.ANDROID)
    self.assertEqual(config.device_id, "0a388e93")
    self.assertTrue(config.is_remote)
    self.assertFalse(config.is_local)

  def test_parse_adb_phone_serial(self):
    self.platform.sh_results = [ADB_DEVICES_OUTPUT, ADB_DEVICES_OUTPUT]

    config = DriverConfig.parse("0a388e93")
    assert isinstance(config, DriverConfig)
    self.assertEqual(len(self.platform.sh_cmds), 2)

    self.assertEqual(config.type, BrowserDriverType.ANDROID)
    self.assertEqual(config.device_id, "0a388e93")

  @unittest.skipIf(not plt.PLATFORM.is_macos, "Incompatible platform")
  def test_parse_ios_phone_serial(self):
    self.platform.sh_results = [
        ADB_DEVICES_OUTPUT, XCTRACE_DEVICES_SINGLE_OUTPUT,
        XCTRACE_DEVICES_SINGLE_OUTPUT
    ]

    config = DriverConfig.parse("00001111-11AA22BB33DD")
    assert isinstance(config, DriverConfig)
    self.assertEqual(len(self.platform.sh_cmds), 3)

    self.assertEqual(config.type, BrowserDriverType.IOS)
    self.assertEqual(config.device_id, "00001111-11AA22BB33DD")


class BrowserConfigTestCase(BaseConfigTestCase):

  def test_equal(self):
    path = Chrome.stable_path(self.platform)
    self.assertEqual(
        BrowserConfig(path, DriverConfig(BrowserDriverType.default())),
        BrowserConfig(path, DriverConfig(BrowserDriverType.default())))
    self.assertNotEqual(
        BrowserConfig(path, DriverConfig(BrowserDriverType.default())),
        BrowserConfig(
            path,
            DriverConfig(
                BrowserDriverType.default(), settings=immutabledict(custom=1))))
    self.assertNotEqual(
        BrowserConfig(path, DriverConfig(BrowserDriverType.default())),
        BrowserConfig(
            pth.RemotePath("foo"), DriverConfig(BrowserDriverType.default())))

  def test_hashable(self):
    _ = hash(BrowserConfig.default())
    _ = hash(
        BrowserConfig(
            pth.RemotePath("foo"),
            DriverConfig(
                BrowserDriverType.default(), settings=immutabledict(custom=1))))

  def test_parse_name_or_path(self):
    path = Chrome.stable_path(self.platform)
    self.assertEqual(
        BrowserConfig.parse("chrome"),
        BrowserConfig(path, DriverConfig(BrowserDriverType.default())))
    self.assertEqual(
        BrowserConfig.parse(str(path)),
        BrowserConfig(path, DriverConfig(BrowserDriverType.default())))

  def test_parse_invalid_name(self):
    with self.assertRaises(argparse.ArgumentTypeError):
      BrowserConfig.parse("")
    with self.assertRaises(argparse.ArgumentTypeError) as cm:
      BrowserConfig.parse("a-random-name")
    self.assertIn("a-random-name", str(cm.exception))

  def test_parse_invalid_path(self):
    path = pth.RemotePath("foo/bar")
    with self.assertRaises(argparse.ArgumentTypeError) as cm:
      BrowserConfig.parse(str(path))
    self.assertIn(str(path), str(cm.exception))
    with self.assertRaises(argparse.ArgumentTypeError) as cm:
      BrowserConfig.parse("selenium/bar")
    self.assertIn("selenium", str(cm.exception))
    self.assertIn("bar", str(cm.exception))

  def test_parse_invalid_windows_path(self):
    with self.assertRaises(argparse.ArgumentTypeError) as cm:
      BrowserConfig.parse("selenium\\bar")
    self.assertIn("selenium\\bar", str(cm.exception))
    with self.assertRaises(argparse.ArgumentTypeError) as cm:
      BrowserConfig.parse("C:\\selenium\\bar")
    self.assertIn("C:\\selenium\\bar", str(cm.exception))

  def test_parse_simple_missing_driver(self):
    with self.assertRaises(argparse.ArgumentTypeError) as cm:
      BrowserConfig.parse(":chrome")
    self.assertIn("driver", str(cm.exception))

  def test_parse_invalid_network_preset(self):
    with self.assertRaises(argparse.ArgumentTypeError) as cm:
      BrowserConfig.parse("selenium:chrome:1xx2")
    self.assertIn("network", str(cm.exception))
    self.assertIn("1xx2", str(cm.exception))

  def test_parse_simple_with_driver(self):
    self.assertEqual(
        BrowserConfig.parse("selenium:chrome"),
        BrowserConfig(
            Chrome.stable_path(self.platform),
            DriverConfig(BrowserDriverType.WEB_DRIVER)))
    self.assertEqual(
        BrowserConfig.parse("webdriver:chrome"),
        BrowserConfig(
            Chrome.stable_path(self.platform),
            DriverConfig(BrowserDriverType.WEB_DRIVER),
            NetworkConfig.default()))
    self.assertEqual(
        BrowserConfig.parse("applescript:chrome"),
        BrowserConfig(
            Chrome.stable_path(self.platform),
            DriverConfig(BrowserDriverType.APPLE_SCRIPT)))
    self.assertEqual(
        BrowserConfig.parse("osa:chrome"),
        BrowserConfig(
            Chrome.stable_path(self.platform),
            DriverConfig(BrowserDriverType.APPLE_SCRIPT)))

  def test_parse_simple_with_driver_with_network(self):
    self.assertEqual(
        BrowserConfig.parse("chrome:4G"),
        BrowserConfig(
            Chrome.stable_path(self.platform),
            DriverConfig(BrowserDriverType.WEB_DRIVER),
            NetworkConfig.load_live(NetworkSpeedPreset.MOBILE_4G)))
    self.assertEqual(
        BrowserConfig.parse("selenium:chrome:4G"),
        BrowserConfig(
            Chrome.stable_path(self.platform),
            DriverConfig(BrowserDriverType.WEB_DRIVER),
            NetworkConfig.load_live(NetworkSpeedPreset.MOBILE_4G)))

  def test_parse_simple_ambiguous_with_driver_ios(self):
    self.platform.sh_results = [XCTRACE_DEVICES_OUTPUT]
    with self.assertRaises(argparse.ArgumentTypeError) as cm:
      _ = BrowserConfig.parse("ios:chrome")
    self.assertIn("devices", str(cm.exception))

  def test_parse_simple_with_driver_ios(self):
    self.platform.sh_results = [
        XCTRACE_DEVICES_SINGLE_OUTPUT, XCTRACE_DEVICES_SINGLE_OUTPUT
    ]
    config = BrowserConfig.parse("ios:chrome")
    self.assertEqual(
        config,
        BrowserConfig(
            Chrome.stable_path(self.platform),
            DriverConfig(BrowserDriverType.IOS)))
    self.assertEqual(config.network, NetworkConfig.default())

  def test_parse_simple_with_driver_ios_with_network(self):
    self.platform.sh_results = [
        XCTRACE_DEVICES_SINGLE_OUTPUT, XCTRACE_DEVICES_SINGLE_OUTPUT
    ]
    config = BrowserConfig.parse("ios:chrome:4G")
    self.assertEqual(
        config,
        BrowserConfig(
            Chrome.stable_path(self.platform),
            DriverConfig(BrowserDriverType.IOS),
            NetworkConfig.load_live(NetworkSpeedPreset.MOBILE_4G)))
    self.assertEqual(config.network,
                     NetworkConfig.load_live(NetworkSpeedPreset.MOBILE_4G))

  def test_parse_simple_ambiguous_with_driver_android(self):
    self.platform.sh_results = [ADB_DEVICES_OUTPUT]
    with self.assertRaises(argparse.ArgumentTypeError) as cm:
      _ = BrowserConfig.parse("adb:chrome")
    self.assertIn("devices", str(cm.exception))

  def test_parse_simple_with_driver_android(self):
    self.platform.sh_results = [
        ADB_DEVICES_SINGLE_OUTPUT, ADB_DEVICES_SINGLE_OUTPUT
    ]
    self.assertEqual(
        BrowserConfig.parse("adb:chrome"),
        BrowserConfig(
            pth.RemotePath("com.android.chrome"),
            DriverConfig(BrowserDriverType.ANDROID)))
    self.assertListEqual(self.platform.sh_results, [])

    self.platform.sh_results = [
        ADB_DEVICES_SINGLE_OUTPUT, ADB_DEVICES_SINGLE_OUTPUT
    ]
    self.assertEqual(
        BrowserConfig.parse("android:com.chrome.beta"),
        BrowserConfig(
            pth.RemotePath("com.chrome.beta"),
            DriverConfig(BrowserDriverType.ANDROID)))
    self.assertListEqual(self.platform.sh_results, [])

    self.platform.sh_results = [
        ADB_DEVICES_SINGLE_OUTPUT, ADB_DEVICES_SINGLE_OUTPUT
    ]
    self.assertEqual(
        BrowserConfig.parse("android:chrome-beta"),
        BrowserConfig(
            pth.RemotePath("com.chrome.beta"),
            DriverConfig(BrowserDriverType.ANDROID)))
    self.assertListEqual(self.platform.sh_results, [])

    self.platform.sh_results = [
        ADB_DEVICES_SINGLE_OUTPUT, ADB_DEVICES_SINGLE_OUTPUT
    ]
    self.assertEqual(
        BrowserConfig.parse("adb:chrome-dev"),
        BrowserConfig(
            pth.RemotePath("com.chrome.dev"),
            DriverConfig(BrowserDriverType.ANDROID)))
    self.assertListEqual(self.platform.sh_results, [])

    self.platform.sh_results = [
        ADB_DEVICES_SINGLE_OUTPUT, ADB_DEVICES_SINGLE_OUTPUT
    ]
    self.assertEqual(
        BrowserConfig.parse("android:chrome-canary"),
        BrowserConfig(
            pth.RemotePath("com.chrome.canary"),
            DriverConfig(BrowserDriverType.ANDROID)))
    self.assertListEqual(self.platform.sh_results, [])

    self.platform.sh_results = [
        ADB_DEVICES_SINGLE_OUTPUT, ADB_DEVICES_SINGLE_OUTPUT
    ]
    self.assertEqual(
        BrowserConfig.parse("android:chromium"),
        BrowserConfig(
            pth.RemotePath("org.chromium.chrome"),
            DriverConfig(BrowserDriverType.ANDROID)))
    self.assertListEqual(self.platform.sh_results, [])

  @unittest.skip("Non-path browser short names are not yet supported "
                 "in complex configs.")
  def test_parse_inline_hjson_android(self):
    self.platform.sh_results = [
        ADB_DEVICES_SINGLE_OUTPUT, ADB_DEVICES_SINGLE_OUTPUT
    ]
    config_dict: JsonDict = {
        "browser": "com.android.chrome",
        "driver": "android",
    }
    self.assertEqual(
        BrowserConfig.parse(config_dict),
        BrowserConfig(
            pth.RemotePath("com.android.chrome"),
            DriverConfig(BrowserDriverType.ANDROID)))
    self.assertListEqual(self.platform.sh_results, [])

  def test_parse_invalid_android_package(self):
    self.platform.sh_results = [ADB_DEVICES_SINGLE_OUTPUT]
    with self.assertRaises(argparse.ArgumentTypeError):
      BrowserConfig.parse("")
    with self.assertRaises(argparse.ArgumentTypeError) as cm:
      BrowserConfig.parse("adb:com.Foo .bar. com")
    self.assertIn("com.Foo .bar. com", str(cm.exception))

  def test_parse_fail_android_browser_string_not_dict(self):
    with self.assertRaises(argparse.ArgumentTypeError) as cm:
      BrowserConfig.parse({"browser": "adb:chrome"})
    self.assertIn("browser", str(cm.exception))
    self.assertIn("short-form", str(cm.exception))

  @unittest.skipIf(plt.PLATFORM.is_win,
                   "Chrome downloading not supported on windows.")
  def test_parse_chrome_version(self):
    self.assertEqual(
        BrowserConfig.parse("applescript:chrome-m100"),
        BrowserConfig("chrome-m100",
                      DriverConfig(BrowserDriverType.APPLE_SCRIPT)))
    self.assertEqual(
        BrowserConfig.parse("selenium:chrome-116.0.5845.4"),
        BrowserConfig("chrome-116.0.5845.4",
                      DriverConfig(BrowserDriverType.WEB_DRIVER)))

  def test_parse_invalid_chrome_version(self):
    with self.assertRaises(argparse.ArgumentTypeError) as cm:
      _ = BrowserConfig.parse("applescript:chrome-m1")
    self.assertIn("m1", str(cm.exception))
    with self.assertRaises(argparse.ArgumentTypeError) as cm:
      _ = BrowserConfig.parse("selenium:chrome-116.845.4")
    self.assertIn("116.845.4", str(cm.exception))

  def test_parse_adb_phone_serial(self):
    self.platform.sh_results = [ADB_DEVICES_OUTPUT, ADB_DEVICES_OUTPUT]
    config = BrowserConfig.parse("0a388e93:chrome")
    assert isinstance(config, BrowserConfig)
    self.assertListEqual(self.platform.sh_results, [])
    self.assertEqual(len(self.platform.sh_cmds), 2)

    self.platform.sh_results = [ADB_DEVICES_OUTPUT]
    expected_driver = DriverConfig(
        BrowserDriverType.ANDROID, device_id="0a388e93")
    self.assertEqual(len(self.platform.sh_results), 0)
    self.assertEqual(len(self.platform.sh_cmds), 3)
    self.assertEqual(
        config,
        BrowserConfig(pth.RemotePath("com.android.chrome"), expected_driver))

  @unittest.skipIf(plt.PLATFORM.is_macos, "Incompatible platform")
  def test_parse_adb_phone_serial_invalid_macos(self):
    self.platform.sh_results = [ADB_DEVICES_OUTPUT]
    with self.assertRaises(argparse.ArgumentTypeError) as cm:
      _ = BrowserConfig.parse("0XXXXXX:chrome")
    self.assertIn("0XXXXXX", str(cm.exception))
    self.assertEqual(len(self.platform.sh_cmds), 1)

  @unittest.skipIf(not plt.PLATFORM.is_macos, "Incompatible platform")
  def test_parse_adb_phone_serial_invalid_non_macos(self):
    self.platform.sh_results = [ADB_DEVICES_OUTPUT, XCTRACE_DEVICES_OUTPUT]
    with self.assertRaises(argparse.ArgumentTypeError) as cm:
      _ = BrowserConfig.parse("0XXXXXX:chrome")
    self.assertIn("0XXXXXX", str(cm.exception))
    self.assertEqual(len(self.platform.sh_cmds), 2)

  def test_parse_invalid_driver(self):
    with self.assertRaises(argparse.ArgumentTypeError):
      BrowserConfig.parse("____:chrome")
    with self.assertRaises(argparse.ArgumentTypeError):
      # This has to be dealt with in users of DriverConfig.parse.
      BrowserConfig.parse("::chrome")

  def test_parse_invalid_hjson(self):
    with self.assertRaises(argparse.ArgumentTypeError):
      BrowserConfig.parse("{:::}")
    with self.assertRaises(argparse.ArgumentTypeError):
      BrowserConfig.parse("{}")
    with self.assertRaises(argparse.ArgumentTypeError):
      BrowserConfig.parse("}")
    with self.assertRaises(argparse.ArgumentTypeError):
      BrowserConfig.parse("{path:something}")

  def test_parse_inline_hjson(self):
    config_dict: JsonDict = {"browser": "chrome", "driver": {"type": 'adb',}}

    self.platform.sh_results = [ADB_DEVICES_OUTPUT]
    with self.assertRaises(MultiException) as cm:
      _ = BrowserConfig.parse(hjson.dumps(config_dict))
    self.assertIn("devices", str(cm.exception))

    self.platform.sh_results = [ADB_DEVICES_SINGLE_OUTPUT]
    config_1 = BrowserConfig.parse(hjson.dumps(config_dict))
    assert isinstance(config_1, BrowserConfig)
    self.assertEqual(config_1.driver.type, BrowserDriverType.ANDROID)

    self.platform.sh_results = [ADB_DEVICES_SINGLE_OUTPUT]
    config_2 = BrowserConfig.load_dict(config_dict)
    assert isinstance(config_2, BrowserConfig)
    self.assertEqual(config_2.driver.type, BrowserDriverType.ANDROID)
    self.assertEqual(config_1, config_2)

    short_config_dict: JsonDict = {
        "browser": "chrome",
        "driver": 'adb',
    }
    self.platform.sh_results = [ADB_DEVICES_OUTPUT]
    with self.assertRaises(MultiException) as cm:
      _ = BrowserConfig.parse(hjson.dumps(short_config_dict))
    self.assertIn("devices", str(cm.exception))

    self.platform.sh_results = [ADB_DEVICES_SINGLE_OUTPUT]
    config_3 = BrowserConfig.load_dict(short_config_dict)
    assert isinstance(config_3, BrowserConfig)
    self.assertEqual(config_3.driver.type, BrowserDriverType.ANDROID)
    self.assertEqual(config_1, config_3)

  def test_parse_inline_hjson_short_string(self):
    # We cannot easily configure the driver property from within the browser
    # config property.
    config_dict: JsonDict = {
        "browser": "adb:chrome",
    }
    with self.assertRaises(argparse.ArgumentTypeError):
      BrowserConfig.load_dict(config_dict)

  def test_parse_inline_driver_browser(self):
    driver_path = pth.LocalPath("custom/chromedriver")
    config_dict: JsonDict = {
        "browser": "chrome",
        "driver": str(driver_path),
    }
    with self.assertRaises(ValueError):
      BrowserConfig.parse(hjson.dumps(config_dict))
    self.fs.create_file(driver_path, st_size=100)
    config = BrowserConfig.parse(hjson.dumps(config_dict))
    assert isinstance(config, BrowserConfig)
    self.assertEqual(config.browser, mock_browser.MockChromeStable.APP_PATH)
    self.assertEqual(config.driver.type, BrowserDriverType.WEB_DRIVER)
    self.assertEqual(config.driver.path, driver_path)

  def test_parse_with_range_simple(self):
    versions = BrowserConfig.parse_with_range("chrome-m100")
    self.assertTupleEqual(versions, (BrowserConfig.parse("chrome-m100"),))

  def test_parse_with_range(self):
    result = (BrowserConfig.parse("chrome-m99"),
              BrowserConfig.parse("chrome-m100"),
              BrowserConfig.parse("chrome-m101"),
              BrowserConfig.parse("chrome-m102"))
    versions = BrowserConfig.parse_with_range("chrome-m99...chrome-m102")
    self.assertTupleEqual(versions, result)
    versions = BrowserConfig.parse_with_range("chrome-m99...m102")
    self.assertTupleEqual(versions, result)
    versions = BrowserConfig.parse_with_range("chrome-m99...102")
    self.assertTupleEqual(versions, result)

  def test_parse_with_range_invalid_empty(self):
    with self.assertRaises(argparse.ArgumentTypeError) as cm:
      BrowserConfig.parse_with_range("")
    self.assertIn("empty", str(cm.exception))

  def test_parse_with_range_invalid_prefix(self):
    with self.assertRaises(argparse.ArgumentTypeError) as cm:
      BrowserConfig.parse_with_range("chr-m100...chrome-m200")
    msg = str(cm.exception)
    self.assertIn("'chr-m'", msg)
    self.assertIn("'chrome-m'", msg)

  def test_parse_with_range_invalid_limit(self):
    with self.assertRaises(argparse.ArgumentTypeError) as cm:
      BrowserConfig.parse_with_range("chr-m100...chr-m90")
    msg = str(cm.exception).lower()
    self.assertIn("larger", msg)
    self.assertIn("chr-m100...chr-m90", msg)

  def test_parse_with_range_missing_digits(self):
    with self.assertRaises(argparse.ArgumentTypeError) as cm:
      BrowserConfig.parse_with_range("chr-m...chrome-m90")
    msg = str(cm.exception).lower()
    self.assertIn("start", msg)
    self.assertIn("'chr-m'", msg)
    with self.assertRaises(argparse.ArgumentTypeError) as cm:
      BrowserConfig.parse_with_range("chr-m100...chr")
    msg = str(cm.exception).lower()
    self.assertIn("limit", msg)
    self.assertIn("'chr'", msg)

class TestProbeConfig(CrossbenchFakeFsTestCase):
  # pylint: disable=expression-not-assigned

  def parse_config(self, config_data) -> ProbeListConfig:
    probe_config_file = pth.LocalPath("/probe.config.hjson")
    with probe_config_file.open("w", encoding="utf-8") as f:
      hjson.dump(config_data, f)
    with probe_config_file.open(encoding="utf-8") as f:
      return ProbeListConfig.load(f)

  def test_invalid_empty(self):
    with self.assertRaises(argparse.ArgumentTypeError):
      self.parse_config({}).probes
    with self.assertRaises(argparse.ArgumentTypeError):
      self.parse_config({"foo": {}}).probes

  def test_invalid_names(self):
    with self.assertRaises(argparse.ArgumentTypeError):
      self.parse_config({"probes": {"invalid probe name": {}}}).probes

  def test_empty(self):
    config = self.parse_config({"probes": {}})
    self.assertListEqual(config.probes, [])

  def test_single_v8_log(self):
    js_flags = ["--log-maps", "--log-function-events"]
    config = self.parse_config(
        {"probes": {
            "v8.log": {
                "prof": True,
                "js_flags": js_flags,
            }
        }})
    self.assertTrue(len(config.probes), 1)
    probe = config.probes[0]
    assert isinstance(probe, V8LogProbe)
    for flag in js_flags + ["--log-all"]:
      self.assertIn(flag, probe.js_flags)

  def test_from_cli_args(self):
    file = pth.LocalPath("probe.config.hjson")
    js_flags = ["--log-maps", "--log-function-events"]
    config_data: JsonDict = {
        "probes": {
            "v8.log": {
                "prof": True,
                "js_flags": js_flags,
            }
        }
    }
    with file.open("w", encoding="utf-8") as f:
      hjson.dump(config_data, f)
    args = mock.Mock(probe_config=file)
    config = ProbeListConfig.from_cli_args(args)
    self.assertTrue(len(config.probes), 1)
    probe = config.probes[0]
    assert isinstance(probe, V8LogProbe)
    for flag in js_flags + ["--log-all"]:
      self.assertIn(flag, probe.js_flags)

  def test_inline_config(self):
    mock_d8_file = pth.LocalPath("out/d8")
    self.fs.create_file(mock_d8_file, st_size=8 * 1024)
    config_data = {"d8_binary": str(mock_d8_file)}
    args = mock.Mock(probe_config=None, throw=True, wraps=False)

    args.probe = [
        ProbeConfig.parse(f"v8.log{hjson.dumps(config_data)}"),
    ]
    config = ProbeListConfig.from_cli_args(args)
    self.assertTrue(len(config.probes), 1)
    probe = config.probes[0]
    self.assertTrue(isinstance(probe, V8LogProbe))

    args.probe = [
        ProbeConfig.parse(f"v8.log:{hjson.dumps(config_data)}"),
    ]
    config = ProbeListConfig.from_cli_args(args)
    self.assertTrue(len(config.probes), 1)
    probe = config.probes[0]
    self.assertTrue(isinstance(probe, V8LogProbe))

  def test_inline_config_invalid(self):
    mock_d8_file = pth.LocalPath("out/d8")
    self.fs.create_file(mock_d8_file)
    config_data = {"d8_binary": str(mock_d8_file)}
    trailing_brace = "}"
    with self.assertRaises(argparse.ArgumentTypeError):
      ProbeConfig.parse(f"v8.log{hjson.dumps(config_data)}{trailing_brace}")
    with self.assertRaises(argparse.ArgumentTypeError):
      ProbeConfig.parse(f"v8.log:{hjson.dumps(config_data)}{trailing_brace}")
    with self.assertRaises(argparse.ArgumentTypeError):
      ProbeConfig.parse("v8.log::")

  def test_inline_config_dir_instead_of_file(self):
    mock_dir = pth.LocalPath("some/dir")
    mock_dir.mkdir(parents=True)
    config_data = {"d8_binary": str(mock_dir)}
    args = mock.Mock(
        probe=[ProbeConfig.parse(f"v8.log{hjson.dumps(config_data)}")],
        probe_config=None,
        throw=True,
        wraps=False)
    with self.assertRaises(argparse.ArgumentTypeError) as cm:
      ProbeListConfig.from_cli_args(args)
    self.assertIn(str(mock_dir), str(cm.exception))

  def test_inline_config_non_existent_file(self):
    config_data = {"d8_binary": "does/not/exist/d8"}
    args = mock.Mock(
        probe=[ProbeConfig.parse(f"v8.log{hjson.dumps(config_data)}")],
        probe_config=None,
        throw=True,
        wraps=False)
    with self.assertRaises(argparse.ArgumentTypeError) as cm:
      ProbeListConfig.from_cli_args(args)
    expected_path = pth.LocalPath("does/not/exist/d8")
    self.assertIn(str(expected_path), str(cm.exception))

  def test_multiple_probes(self):
    powersampler_bin = pth.LocalPath("/powersampler.bin")
    powersampler_bin.touch()
    config = self.parse_config({
        "probes": {
            "v8.log": {
                "log_all": True,
            },
            "powersampler": {
                "bin_path": str(powersampler_bin)
            }
        }
    })
    self.assertTrue(len(config.probes), 2)
    log_probe = config.probes[0]
    assert isinstance(log_probe, V8LogProbe)
    powersampler_probe = config.probes[1]
    assert isinstance(powersampler_probe, PowerSamplerProbe)
    self.assertEqual(powersampler_probe.bin_path, powersampler_bin)


class TestBrowserVariantsConfig(BaseConfigTestCase):
  # pylint: disable=expression-not-assigned

  EXAMPLE_CONFIG_PATH = (
      test_helper.config_dir() / "browser.config.example.hjson")

  EXAMPLE_REMOTE_CONFIG_PATH = (
      test_helper.config_dir() / "remote.browser.config.example.hjson")

  def setUp(self):
    super().setUp()
    self.browser_lookup: Dict[str, Tuple[
        Type[mock_browser.MockBrowser], BrowserConfig]] = {
            "chr-stable":
                (mock_browser.MockChromeStable,
                 BrowserConfig(mock_browser.MockChromeStable.APP_PATH)),
            "chr-dev": (mock_browser.MockChromeDev,
                        BrowserConfig(mock_browser.MockChromeDev.APP_PATH)),
            "chrome-stable":
                (mock_browser.MockChromeStable,
                 BrowserConfig(mock_browser.MockChromeStable.APP_PATH)),
            "chrome-dev": (mock_browser.MockChromeDev,
                           BrowserConfig(mock_browser.MockChromeDev.APP_PATH)),
        }
    for _, (_, browser_config) in self.browser_lookup.items():
      self.assertTrue(browser_config.path.exists())

  def _expect_linux_ssh(self, cmd, **kwargs):
    return self.platform.expect_sh("ssh", "-p", "22", "user@my-linux-machine",
                                   cmd, **kwargs)

  def _expect_chromeos_ssh(self, cmd, **kwargs):
    return self.platform.expect_sh("ssh", "-p", "22",
                                   "root@my-chromeos-machine", cmd, **kwargs)

  @unittest.skipIf(hjson.__name__ != "hjson", "hjson not available")
  def test_load_browser_config_template(self):
    if not self.EXAMPLE_CONFIG_PATH.exists():
      raise unittest.SkipTest(
          f"Test file {self.EXAMPLE_CONFIG_PATH} does not exist")
    self.fs.add_real_file(self.EXAMPLE_CONFIG_PATH)
    with self.EXAMPLE_CONFIG_PATH.open(encoding="utf-8") as f:
      config = BrowserVariantsConfig(
          browser_lookup_override=self.browser_lookup)
      config.load(f, args=self.mock_args)
    self.assertIn("flag-group-1", config.flags_config)
    self.assertGreaterEqual(len(config.flags_config), 1)
    self.assertGreaterEqual(len(config.variants), 1)

  @unittest.skipIf(hjson.__name__ != "hjson", "hjson not available")
  def test_load_remote_browser_config_template(self):
    if not self.EXAMPLE_REMOTE_CONFIG_PATH.exists():
      raise unittest.SkipTest(
          f"Test file {self.EXAMPLE_REMOTE_CONFIG_PATH} does not exist")
    self.fs.add_real_file(self.EXAMPLE_REMOTE_CONFIG_PATH)

    self._expect_linux_ssh("'[' -e /path/to/google/chrome ']'")
    self._expect_linux_ssh("'[' -f /path/to/google/chrome ']'")
    self._expect_linux_ssh("'[' -e /path/to/google/chrome ']'")
    self._expect_linux_ssh(
        "/path/to/google/chrome --version", result='102.22.33.44')
    self._expect_linux_ssh("env")
    self._expect_linux_ssh("'[' -d /tmp ']'")
    self._expect_linux_ssh("mktemp -d /tmp/chrome.XXXXXXXXXXX")

    self._expect_chromeos_ssh("'[' -e /usr/local/autotest/bin/autologin.py ']'")
    self._expect_chromeos_ssh("'[' -e /opt/google/chrome/chrome ']'")
    self._expect_chromeos_ssh("'[' -f /opt/google/chrome/chrome ']'")
    self._expect_chromeos_ssh("'[' -e /opt/google/chrome/chrome ']'")
    self._expect_chromeos_ssh(
        "/opt/google/chrome/chrome --version", result='125.0.6422.60')
    self._expect_chromeos_ssh("env")
    self._expect_chromeos_ssh("'[' -d /tmp ']'")
    self._expect_chromeos_ssh("mktemp -d /tmp/chrome.XXXXXXXXXXX")

    with self.EXAMPLE_REMOTE_CONFIG_PATH.open(encoding="utf-8") as f:
      config = BrowserVariantsConfig()
      config.load(f, args=self.mock_args)
      self.assertEqual(len(config.variants), 2)
      for variant in config.variants:
        self.assertTrue(variant.platform.is_remote)
        self.assertTrue(variant.platform.is_linux)
      self.assertEqual(config.variants[0].platform.name, 'linux_ssh')
      self.assertEqual(config.variants[1].platform.name, 'chromeos_ssh')
      self.assertEqual(config.variants[0].version, '102.22.33.44')
      self.assertEqual(config.variants[1].version, '125.0.6422.60')

  def test_browser_labels_attributes(self):
    browsers = BrowserVariantsConfig(
        {
            "browsers": {
                "chrome-stable-default": {
                    "path": "chrome-stable",
                },
                "chrome-stable-noopt": {
                    "path": "chrome-stable",
                    "flags": ["--js-flags=--max-opt=0",]
                },
                "chrome-stable-custom": {
                    "label": "custom-label-property",
                    "path": "chrome-stable",
                    "flags": ["--js-flags=--max-opt=0",]
                }
            }
        },
        browser_lookup_override=self.browser_lookup,
        args=self.mock_args).variants
    self.assertEqual(len(browsers), 3)
    self.assertEqual(browsers[0].label, "chrome-stable-default")
    self.assertEqual(browsers[1].label, "chrome-stable-noopt")
    self.assertEqual(browsers[2].label, "custom-label-property")

  def test_browser_label_args(self):
    self.platform.sh_results = [ADB_DEVICES_SINGLE_OUTPUT]
    args = self.mock_args
    adb_config = BrowserConfig.parse("adb:chrome")
    desktop_config = BrowserConfig.parse("chrome")
    args.browser = [
        adb_config,
        desktop_config,
    ]
    self.assertFalse(self.platform.sh_results)
    self.platform.sh_results = [
        ADB_DEVICES_SINGLE_OUTPUT,
        ADB_DEVICES_SINGLE_OUTPUT,
    ]

    def mock_get_browser_cls(browser_config: BrowserConfig):
      if browser_config is adb_config:
        return mock_browser.MockChromeAndroidStable
      if browser_config is desktop_config:
        return mock_browser.MockChromeStable
      raise ValueError("Unknown browser_config")

    with mock.patch.object(
        BrowserVariantsConfig,
        "_get_browser_cls",
        side_effect=mock_get_browser_cls), mock.patch(
            "crossbench.plt.AndroidAdbPlatform.machine",
            new_callable=mock.PropertyMock,
            return_value=plt.MachineArch.ARM_64):
      browsers = BrowserVariantsConfig.from_cli_args(args).variants
    self.assertEqual(len(browsers), 2)
    self.assertEqual(browsers[0].label, "android.arm64.remote_0")
    self.assertEqual(browsers[1].label, "mock.arm64.local_1")

    with self.assertRaises(ConfigError) as cm:
      BrowserVariantsConfig(
          {
              "browsers": {
                  "chrome-stable-label": {
                      "path": "chrome-stable",
                  },
                  "chrome-stable-custom": {
                      "label": "chrome-stable-label",
                      "path": "chrome-stable",
                  }
              }
          },
          browser_lookup_override=self.browser_lookup,
          args=self.mock_args).variants
    message = str(cm.exception)
    self.assertIn("chrome-stable-label", message)
    self.assertIn("chrome-stable-custom", message)

  def test_parse_invalid_browser_type(self):
    for invalid in (None, 1, []):
      with self.assertRaises(ConfigError) as cm:
        _ = BrowserVariantsConfig(
            {
                "browsers": {
                    "chrome-stable-default": invalid
                }
            },
            args=self.mock_args).variants
      self.assertIn("Expected str or dict", str(cm.exception))

  def test_browser_custom_driver_variants(self):
    self.platform.sh_results = [
        ADB_DEVICES_SINGLE_OUTPUT, ADB_DEVICES_SINGLE_OUTPUT,
        ADB_DEVICES_SINGLE_OUTPUT, ADB_DEVICES_SINGLE_OUTPUT
    ]

    def mock_get_browser_platform(
        browser_config: BrowserConfig) -> plt.Platform:
      if browser_config.driver.type == BrowserDriverType.ANDROID:
        return AndroidAdbMockPlatform(self.platform, adb=MockAdb(self.platform))
      return self.platform

    with self.mock_chrome_stable(
        mock_browser.MockChromeAndroidStable), mock.patch.object(
            BrowserVariantsConfig,
            "_get_browser_platform",
            side_effect=mock_get_browser_platform):
      browsers = BrowserVariantsConfig(
          {
              "browsers": {
                  "chrome-stable-default": "chrome-stable",
                  "chrome-stable-adb": "adb:chrome",
                  "chrome-stable-adb2": {
                      "path": "chrome",
                      "driver": "adb"
                  }
              }
          },
          browser_lookup_override=self.browser_lookup,
          args=self.mock_args).variants
    self.assertEqual(len(browsers), 3)
    self.assertEqual(browsers[0].label, "chrome-stable-default")
    self.assertEqual(browsers[1].label, "chrome-stable-adb")
    self.assertEqual(browsers[2].label, "chrome-stable-adb2")
    self.assertIsInstance(browsers[0], mock_browser.MockChromeStable)
    self.assertIsInstance(browsers[1], mock_browser.MockChromeAndroidStable)
    self.assertIsInstance(browsers[2], mock_browser.MockChromeAndroidStable)

  def test_flag_combination_invalid(self):
    with self.assertRaises(ConfigError) as cm:
      BrowserVariantsConfig(
          {
              "flags": {
                  "group1": {
                      "invalid-flag-name": [None, "", "v1"],
                  },
              },
              "browsers": {
                  "chrome-stable": {
                      "path": "chrome-stable",
                      "flags": ["group1",]
                  }
              }
          },
          browser_lookup_override=self.browser_lookup,
          args=self.mock_args).variants
    message = str(cm.exception)
    self.assertIn("group1", message)
    self.assertIn("invalid-flag-name", message)

  def test_flag_combination_none(self):
    with self.assertRaises(ConfigError) as cm:
      BrowserVariantsConfig(
          {
              "flags": {
                  "group1": {
                      "--foo": ["None,", "", "v1"],
                  },
              },
              "browsers": {
                  "chrome-stable": {
                      "path": "chrome-stable",
                      "flags": ["group1"]
                  }
              }
          },
          browser_lookup_override=self.browser_lookup,
          args=self.mock_args).variants
    self.assertIn("None", str(cm.exception))

  def test_flag_combination_duplicate(self):
    with self.assertRaises(ConfigError) as cm:
      BrowserVariantsConfig(
          {
              "flags": {
                  "group1": {
                      "--duplicate-flag": [None, "", "v1"],
                  },
                  "group2": {
                      "--duplicate-flag": [None, "", "v1"],
                  }
              },
              "browsers": {
                  "chrome-stable": {
                      "path": "chrome-stable",
                      "flags": ["group1", "group2"]
                  }
              }
          },
          browser_lookup_override=self.browser_lookup,
          args=self.mock_args).variants
    self.assertIn("--duplicate-flag", str(cm.exception))

  def test_empty(self):
    with self.assertRaises(ConfigError):
      BrowserVariantsConfig({"other": {}}, args=self.mock_args).variants
    with self.assertRaises(ConfigError):
      BrowserVariantsConfig({"browsers": {}}, args=self.mock_args).variants

  def test_unknown_group(self):
    with self.assertRaises(ConfigError) as cm:
      BrowserVariantsConfig(
          {
              "browsers": {
                  "chrome-stable": {
                      "path": "chrome-stable",
                      "flags": ["unknown-flag-group"]
                  }
              }
          },
          args=self.mock_args).variants
    self.assertIn("unknown-flag-group", str(cm.exception))

  def test_duplicate_group(self):
    with self.assertRaises(ConfigError):
      BrowserVariantsConfig(
          {
              "flags": {
                  "group1": {}
              },
              "browsers": {
                  "chrome-stable": {
                      "path": "chrome-stable",
                      "flags": ["group1", "group1"]
                  }
              }
          },
          args=self.mock_args).variants

  def test_non_list_group(self):
    BrowserVariantsConfig(
        {
            "flags": {
                "group1": {}
            },
            "browsers": {
                "chrome-stable": {
                    "path": "chrome-stable",
                    "flags": "group1"
                }
            }
        },
        browser_lookup_override=self.browser_lookup,
        args=self.mock_args).variants
    with self.assertRaises(ConfigError) as cm:
      BrowserVariantsConfig(
          {
              "flags": {
                  "group1": {}
              },
              "browsers": {
                  "chrome-stable": {
                      "path": "chrome-stable",
                      "flags": 1
                  }
              }
          },
          browser_lookup_override=self.browser_lookup,
          args=self.mock_args).variants
    self.assertIn("chrome-stable", str(cm.exception))
    self.assertIn("flags", str(cm.exception))

    with self.assertRaises(ConfigError) as cm:
      BrowserVariantsConfig(
          {
              "flags": {
                  "group1": {}
              },
              "browsers": {
                  "chrome-stable": {
                      "path": "chrome-stable",
                      "flags": {
                          "group1": True
                      }
                  }
              }
          },
          browser_lookup_override=self.browser_lookup,
          args=self.mock_args).variants
    self.assertIn("chrome-stable", str(cm.exception))
    self.assertIn("flags", str(cm.exception))

  def test_duplicate_flag_variant_value(self):
    with self.assertRaises(ConfigError) as cm:
      BrowserVariantsConfig(
          {
              "flags": {
                  "group1": {
                      "--flag": ["repeated", "repeated"]
                  }
              },
              "browsers": {
                  "chrome-stable": {
                      "path": "chrome-stable",
                      "flags": "group1",
                  }
              }
          },
          args=self.mock_args).variants
    self.assertIn("group1", str(cm.exception))
    self.assertIn("--flag", str(cm.exception))

  def test_unknown_path(self):
    with self.assertRaises(Exception):
      BrowserVariantsConfig(
          {
              "browsers": {
                  "chrome-stable": {
                      "path": "path/does/not/exist",
                  }
              }
          },
          args=self.mock_args).variants
    with self.assertRaises(Exception):
      BrowserVariantsConfig(
          {
              "browsers": {
                  "chrome-stable": {
                      "path": "chrome-unknown",
                  }
              }
          },
          args=self.mock_args).variants

  def test_flag_combination_simple(self):
    config = BrowserVariantsConfig(
        {
            "flags": {
                "group1": {
                    "--foo": [None, "", "v1"],
                }
            },
            "browsers": {
                "chrome-stable": {
                    "path": "chrome-stable",
                    "flags": ["group1"]
                }
            }
        },
        browser_lookup_override=self.browser_lookup,
        args=self.mock_args)
    browsers = config.variants
    self.assertEqual(len(browsers), 3)
    for browser in browsers:
      assert isinstance(browser, mock_browser.MockChromeStable)
      self.assertDictEqual(browser.js_flags.to_dict(), {})
    self.assertDictEqual(browsers[0].flags.to_dict(), {})
    self.assertDictEqual(browsers[1].flags.to_dict(), {"--foo": None})
    self.assertDictEqual(browsers[2].flags.to_dict(), {"--foo": "v1"})

  def test_flag_list(self):
    config = BrowserVariantsConfig(
        {
            "flags": {
                "group1": [
                    "",
                    "--foo",
                    "-foo=v1",
                ]
            },
            "browsers": {
                "chrome-stable": {
                    "path": "chrome-stable",
                    "flags": ["group1"]
                }
            }
        },
        browser_lookup_override=self.browser_lookup,
        args=self.mock_args)
    browsers = config.variants
    self.assertEqual(len(browsers), 3)
    for browser in browsers:
      assert isinstance(browser, mock_browser.MockChromeStable)
      self.assertDictEqual(browser.js_flags.to_dict(), {})
    self.assertDictEqual(browsers[0].flags.to_dict(), {})
    self.assertDictEqual(browsers[1].flags.to_dict(), {"--foo": None})
    self.assertDictEqual(browsers[2].flags.to_dict(), {"-foo": "v1"})

  def test_flag_combination(self):
    config = BrowserVariantsConfig(
        {
            "flags": {
                "group1": {
                    "--foo": [None, "", "v1"],
                    "--bar": [None, "", "v1"],
                }
            },
            "browsers": {
                "chrome-stable": {
                    "path": "chrome-stable",
                    "flags": ["group1"]
                }
            }
        },
        browser_lookup_override=self.browser_lookup,
        args=self.mock_args)
    self.assertEqual(len(config.variants), 3 * 3)

  def test_flag_combination_mixed_inline(self):
    config = BrowserVariantsConfig(
        {
            "flags": {
                "compile-hints-experiment": {
                    "--enable-features": [None, "ConsumeCompileHints"]
                }
            },
            "browsers": {
                "chrome-release": {
                    "path": "chrome-stable",
                    "flags": ["--no-sandbox", "compile-hints-experiment"]
                }
            }
        },
        browser_lookup_override=self.browser_lookup,
        args=self.mock_args)
    browsers = config.variants
    self.assertEqual(len(browsers), 2)
    self.assertListEqual(["--no-sandbox"], list(browsers[0].flags))
    self.assertListEqual(
        ["--no-sandbox", "--enable-features=ConsumeCompileHints"],
        list(browsers[1].flags))

  def test_flag_single_inline(self):
    config = BrowserVariantsConfig(
        {
            "browsers": {
                "chrome-release": {
                    "path": "chrome-stable",
                    "flags": "--no-sandbox",
                }
            }
        },
        browser_lookup_override=self.browser_lookup,
        args=self.mock_args)
    browsers = config.variants
    self.assertEqual(len(browsers), 1)
    self.assertListEqual(["--no-sandbox"], list(browsers[0].flags))

  def test_flag_combination_mixed_fixed(self):
    config = BrowserVariantsConfig(
        {
            "flags": {
                "compile-hints-experiment": {
                    "--no-sandbox": "",
                    "--enable-features": [None, "ConsumeCompileHints"]
                }
            },
            "browsers": {
                "chrome-release": {
                    "path": "chrome-stable",
                    "flags": "compile-hints-experiment"
                }
            }
        },
        browser_lookup_override=self.browser_lookup,
        args=self.mock_args)
    browsers = config.variants
    self.assertEqual(len(browsers), 2)
    self.assertListEqual(["--no-sandbox"], list(browsers[0].flags))
    self.assertListEqual(
        ["--no-sandbox", "--enable-features=ConsumeCompileHints"],
        list(browsers[1].flags))

  def test_conflicting_chrome_features(self):
    with self.assertRaises(ConfigError) as cm:
      _ = BrowserVariantsConfig(
          {
              "flags": {
                  "compile-hints-experiment": {
                      "--enable-features": [None, "ConsumeCompileHints"]
                  }
              },
              "browsers": {
                  "chrome-release": {
                      "path":
                          "chrome-stable",
                      "flags": [
                          "--disable-features=ConsumeCompileHints",
                          "compile-hints-experiment"
                      ]
                  }
              }
          },
          browser_lookup_override=self.browser_lookup,
          args=self.mock_args)
    msg = str(cm.exception)
    self.assertIn("ConsumeCompileHints", msg)

  def test_no_flags(self):
    config = BrowserVariantsConfig(
        {
            "browsers": {
                "chrome-stable": {
                    "path": "chrome-stable",
                },
                "chrome-dev": {
                    "path": "chrome-dev",
                }
            }
        },
        browser_lookup_override=self.browser_lookup,
        args=self.mock_args)
    self.assertEqual(len(config.variants), 2)
    browser_0 = config.variants[0]
    assert isinstance(browser_0, mock_browser.MockChromeStable)
    self.assertEqual(browser_0.app_path, mock_browser.MockChromeStable.APP_PATH)
    browser_1 = config.variants[1]
    assert isinstance(browser_1, mock_browser.MockChromeDev)
    self.assertEqual(browser_1.app_path, mock_browser.MockChromeDev.APP_PATH)

  def test_custom_driver(self):
    chromedriver = pth.LocalPath("path/to/chromedriver")
    variants_config = {
        "browsers": {
            "chrome-stable": {
                "browser": "chrome-stable",
                "driver": str(chromedriver),
            }
        }
    }
    with self.assertRaises(argparse.ArgumentTypeError) as cm:
      BrowserVariantsConfig(
          copy.deepcopy(variants_config),
          browser_lookup_override=self.browser_lookup,
          args=self.mock_args)
    self.assertIn(str(chromedriver), str(cm.exception))

    self.fs.create_file(chromedriver, st_size=100)
    with mock.patch.object(
        BrowserVariantsConfig,
        "_get_browser_cls",
        return_value=mock_browser.MockChromeStable):
      config = BrowserVariantsConfig(
          variants_config,
          browser_lookup_override=self.browser_lookup,
          args=self.mock_args)
    self.assertFalse(variants_config["browsers"]["chrome-stable"])
    self.assertEqual(len(config.variants), 1)
    browser_0 = config.variants[0]
    assert isinstance(browser_0, mock_browser.MockChromeStable)
    self.assertEqual(browser_0.app_path, mock_browser.MockChromeStable.APP_PATH)

  def test_inline_flags(self):
    with mock.patch.object(
        ChromeWebDriver, "_extract_version",
        return_value="101.22.333.44"), mock.patch.object(
            Chrome,
            "stable_path",
            return_value=mock_browser.MockChromeStable.APP_PATH):

      config = BrowserVariantsConfig(
          {
              "browsers": {
                  "stable": {
                      "path": "chrome-stable",
                      "flags": ["--foo=bar"]
                  }
              }
          },
          args=self.mock_args)
      self.assertEqual(len(config.variants), 1)
      browser = config.variants[0]
      # TODO: Fix once app lookup is cleaned up
      self.assertEqual(browser.app_path, mock_browser.MockChromeStable.APP_PATH)
      self.assertEqual(browser.version, "101.22.333.44")

  def test_inline_load_safari(self):
    if not plt.PLATFORM.is_macos:
      return
    with mock.patch.object(Safari, "_extract_version", return_value="16.0"):
      config = BrowserVariantsConfig(
          {"browsers": {
              "safari": {
                  "path": "safari",
              }
          }}, args=self.mock_args)
      self.assertEqual(len(config.variants), 1)

  def test_flag_combination_with_fixed(self):
    config = BrowserVariantsConfig(
        {
            "flags": {
                "group1": {
                    "--foo": [None, "", "v1"],
                    "--bar": [None, "", "v1"],
                    "--always_1": "true",
                    "--always_2": "true",
                    "--always_3": "true",
                }
            },
            "browsers": {
                "chrome-stable": {
                    "path": "chrome-stable",
                    "flags": ["group1"]
                }
            }
        },
        browser_lookup_override=self.browser_lookup,
        args=self.mock_args)
    self.assertEqual(len(config.variants), 3 * 3)
    for browser in config.variants:
      assert isinstance(browser, mock_browser.MockChromeStable)
      self.assertEqual(browser.app_path, mock_browser.MockChromeStable.APP_PATH)

  def test_flag_group_combination(self):
    config = BrowserVariantsConfig(
        {
            "flags": {
                "group1": {
                    "--foo": [None, "", "v1"],
                },
                "group2": {
                    "--bar": [None, "", "v1"],
                },
                "group3": {
                    "--other": ["v1", "v2"],
                }
            },
            "browsers": {
                "chrome-stable": {
                    "path": "chrome-stable",
                    "flags": ["group1", "group2", "group3"]
                }
            }
        },
        browser_lookup_override=self.browser_lookup,
        args=self.mock_args)
    self.assertEqual(len(config.variants), 3 * 3 * 2)

  def test_from_cli_args_browser_config(self):
    if self.platform.is_win:
      self.skipTest("No auto-download available on windows")
    browser_cls = mock_browser.MockChromeStable
    # TODO: migrate to with_stem once python 3.9 is available everywhere
    suffix = browser_cls.APP_PATH.suffix
    browser_bin = browser_cls.APP_PATH.with_name(
        f"Custom Google Chrome{suffix}")
    browser_cls.setup_bin(self.fs, browser_bin, "Chrome")
    config_data = {"browsers": {"chrome-stable": {"path": str(browser_bin),}}}
    config_file = pth.LocalPath("config.hjson")
    with config_file.open("w", encoding="utf-8") as f:
      hjson.dump(config_data, f)

    args = mock.Mock(browser=None, browser_config=config_file, driver_path=None)
    with mock.patch.object(
        BrowserVariantsConfig, "_get_browser_cls", return_value=browser_cls):
      config = BrowserVariantsConfig.from_cli_args(args)
    self.assertEqual(len(config.variants), 1)
    browser = config.variants[0]
    self.assertIsInstance(browser, browser_cls)
    self.assertEqual(browser.app_path, browser_bin)

  def test_from_cli_args_browser(self):
    if self.platform.is_win:
      self.skipTest("No auto-download available on windows")
    browser_cls = mock_browser.MockChromeStable
    # TODO: migrate to with_stem once python 3.9 is available everywhere
    suffix = browser_cls.APP_PATH.suffix
    browser_bin = browser_cls.APP_PATH.with_name(
        f"Custom Google Chrome{suffix}")
    browser_cls.setup_bin(self.fs, browser_bin, "Chrome")
    args = mock.Mock(
        browser=[
            BrowserConfig(browser_bin),
        ],
        browser_config=None,
        enable_features=None,
        disable_features=None,
        driver_path=None,
        js_flags=None,
        other_browser_args=[])
    with mock.patch.object(
        BrowserVariantsConfig, "_get_browser_cls", return_value=browser_cls):
      config = BrowserVariantsConfig.from_cli_args(args)
    self.assertEqual(len(config.variants), 1)
    browser = config.variants[0]
    self.assertIsInstance(browser, browser_cls)
    self.assertEqual(browser.app_path, browser_bin)


class FlagsConfigTestCase(unittest.TestCase):

  def test_invalid_empty(self):
    with self.assertRaises(ArgumentTypeMultiException) as cm:
      FlagsConfig.parse("")
    self.assertIn("empty", str(cm.exception).lower())
    with self.assertRaises(ConfigError) as cm:
      FlagsConfig.loads("")
    self.assertIn("empty", str(cm.exception).lower())

  def test_empty_dict(self):
    config = FlagsConfig.parse({})
    self.assertFalse(config)

  def test_parse_empty_group(self):
    config = FlagsConfig.parse({
        "a": None,
        "b": {},
        "c": tuple(),
    })
    self.assertEqual(len(config), 3)
    for group in config.values():
      self.assertFalse(group)
    self.assertFalse(config["a"])
    self.assertFalse(config["b"])
    self.assertFalse(config["c"])

  def test_parse_str(self):
    config = FlagsConfig.parse("--foo --bar")
    self.assertEqual(len(config), 1)
    self.assertEqual(str(config["default"][0].flags), "--foo --bar")

  def test_parse_single_str_groups(self):
    config = FlagsConfig.parse({
        "a": "--foo=1 --bar",
        "b": "--foo=2 --bar",
    })
    self.assertEqual(len(config), 2)
    self.assertEqual(len(config["a"]), 1)
    self.assertEqual(len(config["b"]), 1)
    flags_a = config["a"][0].flags
    flags_b = config["b"][0].flags
    self.assertEqual(len(flags_a), 2)
    self.assertEqual(len(flags_b), 2)
    self.assertEqual(str(flags_a), "--foo=1 --bar")
    self.assertEqual(str(flags_b), "--foo=2 --bar")

  def test_parse_single_dict_groups(self):
    config = FlagsConfig.parse({
        "a": {
            "--foo": "1",
            "--bar": None,
        },
        "b": {
            "--foo": "2",
            "--bar": None
        }
    })
    self.assertEqual(len(config), 2)
    self.assertEqual(len(config["a"]), 1)
    self.assertEqual(len(config["b"]), 1)
    flags_a = config["a"][0].flags
    flags_b = config["b"][0].flags
    self.assertEqual(len(flags_a), 2)
    self.assertEqual(len(flags_b), 2)
    self.assertEqual(str(flags_a), "--foo=1 --bar")
    self.assertEqual(str(flags_b), "--foo=2 --bar")

  def test_parse_multi_str_groups(self):
    config = FlagsConfig.parse({
        "a": [
            "--foo=1 --bar=1",
            "--foo=1 --bar=2",
        ],
        "b": "--foo=2 --bar",
    })
    self.assertEqual(len(config), 2)
    self.assertEqual(len(config["a"]), 2)
    self.assertEqual(len(config["b"]), 1)
    labels = tuple(v.label for v in config["a"])  # pylint: disable=no-member
    self.assertTupleEqual(labels, ("foo=1_bar=1", "foo=1_bar=2"))
    variants_a = config["a"]
    flags_a_1 = variants_a[0].flags
    flags_a_2 = variants_a[1].flags
    self.assertEqual(str(flags_a_1), "--foo=1 --bar=1")
    self.assertEqual(str(flags_a_2), "--foo=1 --bar=2")

    flags_b = config["b"][0].flags
    self.assertEqual(len(flags_b), 2)
    self.assertEqual(str(flags_b), "--foo=2 --bar")

  def test_parse_multi_dict_str_groups(self):
    config = FlagsConfig.parse({
        "a": {
            "label_a_1": "--foo=1 --bar=1",
            "label_a_2": "--foo=1 --bar=2",
        }
    })
    self.assertEqual(len(config), 1)
    self.assertEqual(len(config["a"]), 2)

    self.assertTupleEqual(
        tuple(v.label for v in config["a"]), ("label_a_1", "label_a_2"))
    variants_a = config["a"]
    flags_a_1 = variants_a[0].flags
    flags_a_2 = variants_a[1].flags
    self.assertEqual(str(flags_a_1), "--foo=1 --bar=1")
    self.assertEqual(str(flags_a_2), "--foo=1 --bar=2")

  def test_parse_multi_dict_dict_groups(self):
    config = FlagsConfig.parse({
        "a": {
            "label_a_1": {
                "--foo": "1",
                "--bar": "1"
            },
            "label_a_2": {
                "--bar": "2",
                "--foo": "1",
            }
        }
    })
    self.assertEqual(len(config), 1)
    self.assertEqual(len(config["a"]), 2)
    self.assertTupleEqual(
        tuple(v.label for v in config["a"]), ("label_a_1", "label_a_2"))
    variants_a = config["a"]
    flags_a_1 = variants_a[0].flags
    flags_a_2 = variants_a[1].flags
    self.assertEqual(str(flags_a_1), "--foo=1 --bar=1")
    self.assertEqual(str(flags_a_2), "--bar=2 --foo=1")

  def test_parse_variants_groups(self):
    config = FlagsConfig.parse(
        {"a": {
            "--foo": [None, "1"],
            "--bar": ["1", "2"],
        }})
    self.assertEqual(len(config), 1)
    self.assertEqual(len(config["a"]), 4)

    self.assertTupleEqual(
        tuple(v.label for v in config["a"]),
        ('bar=1', 'bar=2', 'foo=1_bar=1', 'foo=1_bar=2'))
    variants_a = config["a"]
    self.assertEqual(str(variants_a[0].flags), "--bar=1")
    self.assertEqual(str(variants_a[1].flags), "--bar=2")
    self.assertEqual(str(variants_a[2].flags), "--foo=1 --bar=1")
    self.assertEqual(str(variants_a[3].flags), "--foo=1 --bar=2")


class FlagsVariantConfigTestCase(unittest.TestCase):

  def test_empty(self):
    empty = FlagsVariantConfig("default")
    self.assertEqual(empty.label, "default")
    self.assertFalse(empty.flags)
    self.assertEqual(empty.index, 0)

  def test_merge_copy(self):
    flags_a = Flags.parse("--foo-a")
    flags_b = Flags.parse("--bar-b=1")
    variant_a = FlagsVariantConfig("label_a", 0, flags_a)
    variant_b = FlagsVariantConfig("label_b", 1, flags_b)
    variant = variant_a.merge_copy(variant_b)
    self.assertEqual(variant.label, "label_a_label_b")
    self.assertEqual(str(variant.flags), "--foo-a --bar-b=1")
    self.assertEqual(variant.index, 0)

    variant = variant_a.merge_copy(variant_b, index=11, label="custom_label")
    self.assertEqual(variant.label, "custom_label")
    self.assertEqual(str(variant.flags), "--foo-a --bar-b=1")
    self.assertEqual(variant.index, 11)

  def test_equal(self):
    variant_a = FlagsVariantConfig.parse("label_a", 0, "--foo=a")
    variant_b = FlagsVariantConfig.parse("label_b", 1, "--foo=a")
    variant_c = FlagsVariantConfig.parse("label_b", 1, "--foo=b")
    self.assertEqual(variant_a, variant_b)
    self.assertEqual(variant_b, variant_a)
    self.assertNotEqual(variant_a, variant_c)
    self.assertNotEqual(variant_b, variant_c)
    variants = set((variant_a,))
    self.assertIn(variant_a, variants)
    self.assertIn(variant_b, variants)
    self.assertNotIn(variant_c, variants)


class FlagsGroupConfigTestCase(unittest.TestCase):

  def test_parse_empty(self):
    for empty in (None, [], (), {}, "", "  "):
      with self.subTest(flags=empty):
        self.assertFalse(FlagsGroupConfig.parse(empty))

  def test_parse_invalid(self):
    for invalid in (-1, 0, 1):
      with self.subTest(invalid=invalid):
        with self.assertRaises(ConfigError):
          FlagsGroupConfig.parse(invalid)

  def test_parse_str_single(self):
    group = FlagsGroupConfig.parse("--foo-a=1")
    self.assertEqual(len(group), 1)
    self.assertEqual(str(group[0].flags), "--foo-a=1")
    self.assertEqual(group[0].label, "default")

  def test_parse_str_multiple(self):
    group = FlagsGroupConfig.parse(("--foo-a=1 --bar", "--foo-a=2"))
    self.assertEqual(len(group), 2)
    self.assertEqual(str(group[0].flags), "--foo-a=1 --bar")
    self.assertEqual(str(group[1].flags), "--foo-a=2")

  def test_parse_str_multiple_empty(self):
    group = FlagsGroupConfig.parse(("", "--foo", "-foo=v1"))
    self.assertEqual(len(group), 3)
    self.assertEqual(str(group[0].flags), "")
    self.assertEqual(str(group[1].flags), "--foo")
    self.assertEqual(str(group[2].flags), "-foo=v1")

  def test_parse_dict_simple(self):
    group = FlagsGroupConfig.parse({
        "--foo": "1",
        "--bar": "2",
    })
    self.assertEqual(len(group), 1)
    self.assertEqual(str(group[0].flags), "--foo=1 --bar=2")
    self.assertEqual(group[0].label, "default")

  def test_parse_dict_invalid_variant(self):
    for invalid in (
        -1,
        0,
    ):
      with self.subTest(invalid=invalid):
        with self.assertRaises(ValueError):
          FlagsGroupConfig.parse({
              "--foo": "1",
              "--invalid": invalid,
              "--bar": "2",
          })

  def test_parse_duplicate_variant_value(self):
    for duplicate in (None, "", "value"):
      with self.subTest(duplicate=duplicate):
        with self.assertRaises(ValueError) as cm:
          FlagsGroupConfig.parse({"--duplicate": [duplicate, duplicate]})
        self.assertIn("duplicate", str(cm.exception))
    with self.assertRaises(ConfigError) as cm:
      FlagsGroupConfig.parse(
          ["--foo --duplicate='foo'", "--foo --duplicate='foo'"])
    self.assertIn("duplicate", str(cm.exception))

  def test_parse_dict_single_with_labels(self):
    group = FlagsGroupConfig.parse({
        "config_1": "--foo=1 --bar",
        "config_2": "",
    })
    self.assertEqual(len(group), 2)
    self.assertEqual(str(group[0].flags), "--foo=1 --bar")
    self.assertEqual(str(group[1].flags), "")
    self.assertEqual(group[0].label, "config_1")
    self.assertEqual(group[1].label, "config_2")
    for index, group in enumerate(group):
      self.assertEqual(group.index, index)

  def test_parse_dict_with_labels_duplicate_flags(self):
    with self.assertRaises(argparse.ArgumentTypeError) as cm:
      _ = FlagsGroupConfig.parse({
          "config_1": "--foo=1 --bar",
          "config_2": "--foo=1 --bar",
      })
    self.assertIn("duplicate", str(cm.exception).lower())
    self.assertIn("--foo=1 --bar", str(cm.exception).lower())

  def test_parse_dict_single(self):
    group = FlagsGroupConfig.parse({
        "--foo": "1",
        "--bar": None,
    })
    self.assertEqual(len(group), 1)
    self.assertEqual(str(group[0].flags), "--foo=1 --bar")

  def test_parse_dict_multiple_3_x_1(self):
    group = FlagsGroupConfig.parse({
        "--foo": [None, "1", "2"],
        "--bar": None,
    })
    self.assertEqual(len(group), 3)
    self.assertEqual(str(group[0].flags), "--bar")
    self.assertEqual(str(group[1].flags), "--foo=1 --bar")
    self.assertEqual(str(group[2].flags), "--foo=2 --bar")
    for index, group in enumerate(group):
      self.assertEqual(group.index, index)

  def test_parse_dict_multiple_2_x_2(self):
    group = FlagsGroupConfig.parse({
        "--foo": [None, "a"],
        "--bar": [None, "b"],
    })
    self.assertEqual(len(group), 4)
    self.assertEqual(str(group[0].flags), "")
    self.assertEqual(str(group[1].flags), "--bar=b")
    self.assertEqual(str(group[2].flags), "--foo=a")
    self.assertEqual(str(group[3].flags), "--foo=a --bar=b")
    self.assertEqual(group[0].label, "default")
    self.assertEqual(group[1].label, "bar=b")
    self.assertEqual(group[2].label, "foo=a")
    self.assertEqual(group[3].label, "foo=a_bar=b")
    for index, group in enumerate(group):
      self.assertEqual(group.index, index)

  def test_product_single(self):
    group_a = FlagsGroupConfig.parse("--foo-a=1")
    group_b = FlagsGroupConfig.parse("--foo-b=1")
    self.assertEqual(group_a[0].label, "default")
    self.assertEqual(group_b[0].label, "default")
    group = group_a.product(group_b)
    self.assertEqual(len(group), 1)
    self.assertEqual(str(group[0].flags), "--foo-a=1 --foo-b=1")
    self.assertEqual(group[0].label, "default")

  def test_product_empty_empty(self):
    group_a = FlagsGroupConfig()
    group_b = FlagsGroupConfig()
    group = group_a.product(group_b)
    self.assertFalse(group)
    group = group_a.product(group_b, group_b, group_b)
    self.assertFalse(group)

  def test_product_same(self):
    group_a = FlagsGroupConfig.parse("--foo-b=1")
    self.assertEqual(group_a[0].label, "default")
    group = group_a.product(group_a)
    self.assertEqual(len(group), 1)
    self.assertEqual(str(group[0].flags), "--foo-b=1")
    self.assertEqual(group[0].label, "default")
    group = group_a.product(group_a, group_a, group_a)
    self.assertEqual(len(group), 1)
    self.assertEqual(str(group[0].flags), "--foo-b=1")
    self.assertEqual(group[0].label, "default")

  def test_product_same_values(self):
    group_a = FlagsGroupConfig.parse("--foo-b=1")
    group_b = FlagsGroupConfig.parse("--foo-b=1")
    group = group_a.product(group_b)
    self.assertEqual(len(group), 1)
    self.assertEqual(str(group[0].flags), "--foo-b=1")
    group = group_a.product(group_a, group_a, group_a)
    self.assertEqual(len(group), 1)
    self.assertEqual(str(group[0].flags), "--foo-b=1")

  def test_product_empty(self):
    group_a = FlagsGroupConfig.parse("")
    group_b = FlagsGroupConfig.parse("--foo-b=1")
    group = group_a.product(group_b)
    self.assertEqual(len(group), 1)
    self.assertEqual(str(group[0].flags), "--foo-b=1")
    group = group_b.product(group_a)
    self.assertEqual(len(group), 1)
    self.assertEqual(str(group[0].flags), "--foo-b=1")

  def test_product_2_x_1(self):
    group_a = FlagsGroupConfig.parse((
        None,
        "--foo-a=1",
    ))
    group_b = FlagsGroupConfig.parse("--foo-b=1")
    group = group_a.product(group_b)
    self.assertEqual(len(group), 2)
    self.assertEqual(str(group[0].flags), "--foo-b=1")
    self.assertEqual(str(group[1].flags), "--foo-a=1 --foo-b=1")
    self.assertEqual(group[0].label, "default")
    self.assertEqual(group[1].label, "foo_a=1")

  def test_product_2_x_2(self):
    group_a = FlagsGroupConfig.parse((
        None,
        "--foo-a=1",
    ))
    group_b = FlagsGroupConfig.parse((None, "--foo-b=1"))
    group = group_a.product(group_b)
    self.assertEqual(len(group), 4)
    self.assertEqual(str(group[0].flags), "")
    self.assertEqual(str(group[1].flags), "--foo-b=1")
    self.assertEqual(str(group[2].flags), "--foo-a=1")
    self.assertEqual(str(group[3].flags), "--foo-a=1 --foo-b=1")
    self.assertEqual(group[0].label, "default")
    self.assertEqual(group[1].label, "foo_b=1")
    self.assertEqual(group[2].label, "foo_a=1")
    self.assertEqual(group[3].label, "foo_a=1_foo_b=1")
    for index, group in enumerate(group):
      self.assertEqual(group.index, index)

  def test_product_conflicting(self):
    group_a = FlagsGroupConfig.parse(("--foo=1"))
    group_b = FlagsGroupConfig.parse(("--foo=2"))
    with self.assertRaises(ValueError) as cm:
      group_a.product(group_b)
    self.assertIn("different previous value", str(cm.exception))


class NetworkSpeedConfigTestCase(BaseConfigTestCase):

  def test_parse_invalid(self):
    for invalid in ("", None, "---"):
      with self.subTest(invalid=invalid):
        with self.assertRaises(argparse.ArgumentTypeError):
          NetworkSpeedConfig.parse(invalid)
        with self.assertRaises(argparse.ArgumentTypeError):
          NetworkSpeedConfig.loads(str(invalid))
      with self.assertRaises(argparse.ArgumentTypeError) as cm:
        NetworkSpeedConfig.parse("not a speed preset value")
      self.assertIn("choices are", str(cm.exception).lower())

  def test_parse_default(self):
    config = NetworkSpeedConfig.parse("default")
    self.assertEqual(config, NetworkSpeedConfig.default())

  def test_default(self):
    config = NetworkSpeedConfig.default()
    self.assertIsNone(config.rtt_ms)
    self.assertIsNone(config.in_kbps)
    self.assertIsNone(config.out_kbps)
    self.assertIsNone(config.window)

  def test_parse_speed_preset(self):
    config = NetworkSpeedConfig.parse("4G")
    self.assertNotEqual(config, NetworkSpeedConfig.default())

    for preset in NetworkSpeedPreset:  # pytype: disable=missing-parameter
      config = NetworkSpeedConfig.parse(str(preset))
      self.assertEqual(config, NetworkSpeedConfig.load_preset(preset))

  def test_parse_invalid_preset(self):
    with self.assertRaises(argparse.ArgumentTypeError) as cm:
      NetworkSpeedConfig.parse("1xx4")
    self.assertIn("1xx4", str(cm.exception))
    self.assertIn("Choices are", str(cm.exception))

  def test_parse_dict(self):
    config = NetworkSpeedConfig.parse({
        "rtt_ms": 100,
        "in_kbps": 200,
        "out_kbps": 300,
        "window": 400
    })
    self.assertIsNone(config.ts_proxy)
    self.assertEqual(config.rtt_ms, 100)
    self.assertEqual(config.in_kbps, 200)
    self.assertEqual(config.out_kbps, 300)
    self.assertEqual(config.window, 400)

  def test_parse_invalid_dict(self):
    for int_property in ("rtt_ms", "in_kbps", "out_kbps", "window"):
      with self.subTest(config_property=int_property):
        with self.assertRaises(argparse.ArgumentTypeError) as cm:
          _ = NetworkSpeedConfig.parse({int_property: -100})
        self.assertIn(int_property, str(cm.exception))

  def test_parse_ts_proxy_path(self):
    with self.assertRaises(argparse.ArgumentTypeError) as cm:
      _ = NetworkSpeedConfig.parse({"ts_proxy": "/some/random/path"})
    self.assertIn("ts_proxy", str(cm.exception))
    ts_proxy = pth.LocalPath("/python/ts_proxy.py")
    self.fs.create_file(ts_proxy, st_size=100)
    config = NetworkSpeedConfig.parse({"ts_proxy": str(ts_proxy)})
    self.assertEqual(config.ts_proxy, ts_proxy)

class NetworkConfigTestCase(BaseConfigTestCase):

  def test_parse_invalid(self):
    for invalid in ("", None, "---", "something"):
      with self.assertRaises(argparse.ArgumentTypeError):
        NetworkConfig.parse(invalid)
      with self.assertRaises(argparse.ArgumentTypeError):
        NetworkConfig.loads(str(invalid))

  def test_parse_default(self):
    config = NetworkConfig.parse("default")
    self.assertEqual(config, NetworkConfig.default())

  def test_default(self):
    config = NetworkConfig.default()
    self.assertEqual(config.type, NetworkType.LIVE)
    self.assertEqual(config.speed, NetworkSpeedConfig.default())

  def test_parse_replay_archive_invalid(self):
    path = pth.LocalPath("/foo/bar/wprgo.archive")
    with self.assertRaises(argparse.ArgumentTypeError) as cm:
      NetworkConfig.parse(str(path))
    message = str(cm.exception)
    self.assertIn("wpr.go archive", message)
    self.assertIn("exist", message)

    self.fs.create_file(path)
    with self.assertRaises(argparse.ArgumentTypeError) as cm:
      NetworkConfig.parse(str(path))
    message = str(cm.exception)
    self.assertIn("wpr.go archive", message)
    self.assertIn("empty", message)

  def test_parse_wprgo_archive(self):
    path = pth.LocalPath("/foo/bar/wprgo.archive")
    self.fs.create_file(path, st_size=1024)
    config = NetworkConfig.parse(str(path))
    assert isinstance(config, NetworkConfig)
    self.assertEqual(config.type, NetworkType.WPR)
    self.assertEqual(config.path, path)
    self.assertEqual(config.speed, NetworkSpeedConfig.default())

  def test_invalid_constructor_params(self):
    with self.assertRaises(argparse.ArgumentTypeError):
      _ = NetworkConfig(path=pth.LocalPath("foo/bar"))
    with self.assertRaises(argparse.ArgumentTypeError):
      _ = NetworkConfig(type=NetworkType.LOCAL, path=None)
    with self.assertRaises(argparse.ArgumentTypeError):
      _ = NetworkConfig(type=NetworkType.WPR, path=None)

  def test_parse_speed_preset(self):
    for preset in NetworkSpeedPreset:  # pytype: disable=missing-parameter
      config = NetworkConfig.loads(preset.value)
      self.assertEqual(config.speed, NetworkSpeedConfig.load_preset(preset))

  def test_parse_live_preset(self):
    live_a = NetworkConfig.load_live("4G")
    live_b = NetworkConfig.load_live(NetworkSpeedConfig.parse("4G"))
    live_c = NetworkConfig.load_live(
        NetworkSpeedConfig.parse(NetworkSpeedPreset.MOBILE_4G))
    live_d = NetworkConfig.load_live(NetworkSpeedPreset.MOBILE_4G)
    self.assertEqual(live_a, live_b)
    self.assertEqual(live_a, live_c)
    self.assertEqual(live_a, live_d)

  def test_parse_wpr_invalid(self):
    dir_path = pth.LocalPath("test/dir")
    dir_path.mkdir(parents=True)
    for invalid in ("", "default", "4G", dir_path, str(dir_path)):
      with self.assertRaises(argparse.ArgumentTypeError):
        NetworkConfig.parse_wpr(invalid)

  def test_parse_wpr(self):
    archive_path = pth.LocalPath("test/archive.wprgo")
    with self.assertRaises(argparse.ArgumentTypeError) as cm:
      NetworkConfig.parse_wpr(archive_path)
    self.assertIn(str(archive_path), str(cm.exception))
    self.fs.create_file(archive_path, st_size=100)
    config = NetworkConfig.parse_wpr(archive_path)
    self.assertEqual(config.type, NetworkType.WPR)
    self.assertEqual(config.speed, NetworkSpeedConfig.default())
    self.assertEqual(config.path, archive_path)
    self.assertEqual(config, NetworkConfig.parse(archive_path))

  def test_parse_dict_default(self):
    config = NetworkConfig.parse({})
    self.assertEqual(config, NetworkConfig.default())

  def test_parse_dict_speed(self):
    config: NetworkConfig = NetworkConfig.parse({"speed": "4G"})
    self.assertNotEqual(config, NetworkConfig.default())
    self.assertEqual(config.type, NetworkType.LIVE)
    self.assertEqual(
        config.speed,
        NetworkSpeedConfig.load_preset(NetworkSpeedPreset.MOBILE_4G))
    self.assertIsNone(config.path)

  def test_parse_dict_wpr(self):
    archive_path = pth.LocalPath("test/archive.wprgo")
    with self.assertRaises(argparse.ArgumentTypeError) as cm:
      NetworkConfig.parse({"type": "wpr", "path": archive_path})
    self.assertIn(str(archive_path), str(cm.exception))
    self.fs.create_file(archive_path, st_size=100)
    config = NetworkConfig.parse({"type": "wpr", "path": archive_path})
    self.assertEqual(config, NetworkConfig.parse_wpr(archive_path))


if __name__ == "__main__":
  test_helper.run_pytest(__file__)
