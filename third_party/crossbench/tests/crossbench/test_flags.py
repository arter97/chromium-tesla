# Copyright 2022 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import abc
import unittest

from crossbench.flags import (ChromeBaseFeatures, ChromeBlinkFeatures,
                              ChromeFeatures, ChromeFlags, Flags,
                              FrozenFlagsError, JSFlags)
from tests import test_helper


class TestFlags(unittest.TestCase):

  CLASS = Flags

  def test_construct(self):
    flags = self.CLASS()
    self.assertEqual(len(flags), 0)
    self.assertNotIn("foo", flags)

  def test_construct_dict(self):
    flags = self.CLASS({"--foo": "v1", "--bar": "v2"})
    self.assertIn("--foo", flags)
    self.assertIn("--bar", flags)
    self.assertEqual(flags["--foo"], "v1")
    self.assertEqual(flags["--bar"], "v2")

  def test_construct_list(self):
    flags = self.CLASS(("--foo", "--bar"))
    self.assertIn("--foo", flags)
    self.assertIn("--bar", flags)
    self.assertIsNone(flags["--foo"])
    self.assertIsNone(flags["--bar"])
    with self.assertRaises(ValueError):
      self.CLASS(("--foo=v1", "--bar=v2"))
    flags = self.CLASS((("--foo", "v3"), "--bar"))
    self.assertEqual(flags["--foo"], "v3")
    self.assertIsNone(flags["--bar"])

  def test_construct_flags(self):
    original_flags = self.CLASS({"--foo": "v1", "--bar": "v2"})
    flags = self.CLASS(original_flags)
    self.assertIn("--foo", flags)
    self.assertIn("--bar", flags)
    self.assertEqual(flags["--foo"], "v1")
    self.assertEqual(flags["--bar"], "v2")

  def test_set(self):
    flags = self.CLASS()
    flags["--foo"] = "v1"
    with self.assertRaises(ValueError):
      flags["--foo"] = "v2"
    # setting the same value is ok
    flags["--foo"] = "v1"
    self.assertEqual(flags["--foo"], "v1")
    flags.set("--bar")
    self.assertIn("--foo", flags)
    self.assertIn("--bar", flags)
    self.assertIsNone(flags["--bar"])
    with self.assertRaises(ValueError):
      flags.set("--bar", "v3")
    flags.set("--bar", "v4", override=True)
    self.assertEqual(flags["--foo"], "v1")
    self.assertEqual(flags["--bar"], "v4")

  def test_set_invalid(self):
    flags = self.CLASS()
    with self.assertRaises(TypeError) as cm:
      flags["--foo"] = 123  # pytype: disable=unsupported-operands
    self.assertIn("123", str(cm.exception))
    with self.assertRaises(ValueError) as cm:
      flags["foo"] = 123  # pytype: disable=unsupported-operands
    self.assertIn("-", str(cm.exception))

  def test_set_invalid_flag_name(self):
    flags = self.CLASS()
    for invalid in ("- -foo", "--f oo", "", "-", "--", "--foo\n", "--\nfoo",
                    "--foo,"):
      with self.subTest(invalid_flag=invalid):
        with self.assertRaises(ValueError):
          flags.set(invalid)
        self.assertFalse(invalid in flags)

  def test_get_list(self):
    flags = self.CLASS({"--foo": "v1", "--bar": None})
    self.assertEqual(list(flags), ["--foo=v1", "--bar"])

  def test_copy(self):
    flags = self.CLASS({"--foo": "v1", "--bar": None})
    copy = flags.copy()
    self.assertEqual(list(flags), list(copy))
    self.assertEqual(str(flags), str(copy))

  def test_copy_frozen(self):
    flags = self.CLASS({"--foo": "v1", "--bar": None})
    flags.freeze()
    self.assertTrue(flags.is_frozen)
    copy = flags.copy()
    with self.assertRaises(FrozenFlagsError):
      flags["--custom"] = "123"
    self.assertNotIn("--custom", flags)
    copy["--custom"] = "123"
    self.assertEqual(copy["--custom"], "123")
    copy.freeze()
    with self.assertRaises(FrozenFlagsError):
      copy["--custom-other"] = "123"

  def test_update(self):
    flags = self.CLASS({"--foo": "v1", "--bar": None})
    with self.assertRaises(ValueError):
      flags.update({"--bar": "v2"})
    self.assertEqual(flags["--foo"], "v1")
    self.assertIsNone(flags["--bar"])
    flags.update({"--bar": "v2"}, override=True)
    self.assertEqual(flags["--foo"], "v1")
    self.assertEqual(flags["--bar"], "v2")

  def test_str_basic(self):
    flags = self.CLASS({"--foo": None})
    self.assertEqual(str(flags), "--foo")
    flags = self.CLASS({"--foo": "bar"})
    self.assertEqual(str(flags), "--foo=bar")

  def test_str_multiple(self):
    flags = self.CLASS({
        "--flag1": "value1",
        "--flag2": None,
        "--flag3": "value3"
    })
    self.assertEqual(str(flags), "--flag1=value1 --flag2 --flag3=value3")

  def test_merge(self):
    flags = self.CLASS({"--foo": "v1", "--bar": None})
    with self.assertRaises(ValueError):
      flags.merge({"--bar": "v2"})
    self.assertEqual(flags["--foo"], "v1")
    self.assertIsNone(flags["--bar"])

  def test_parse_single(self):
    flags = self.CLASS.parse("--foo")
    self.assertEqual(len(flags), 1)
    self.assertEqual(flags["--foo"], None)
    self.assertEqual(str(flags), "--foo")

    flags = self.CLASS.parse("--foo=123")
    self.assertEqual(len(flags), 1)
    self.assertEqual(flags["--foo"], "123")
    self.assertEqual(str(flags), "--foo=123")

    flags = self.CLASS.parse("--foo=--bar123")
    self.assertEqual(len(flags), 1)
    self.assertEqual(flags["--foo"], "--bar123")
    self.assertEqual(str(flags), "--foo=--bar123")

  def test_parse_nested(self):
    flags = self.CLASS.parse("--foo=--bar=123")
    self.assertEqual(len(flags), 1)
    self.assertEqual(flags["--foo"], "--bar=123")
    self.assertEqual(str(flags), "--foo=--bar=123")

  def test_parse_multiple(self):
    flags = self.CLASS.parse("--foo --bar")
    self.assertEqual(len(flags), 2)
    self.assertEqual(flags["--foo"], None)
    self.assertEqual(flags["--bar"], None)
    flags = self.CLASS.parse("--foo --bar=1")
    self.assertEqual(len(flags), 2)
    self.assertEqual(flags["--foo"], None)
    self.assertEqual(flags["--bar"], "1")
    flags = self.CLASS.parse("--foo=1 --bar=2")
    self.assertEqual(len(flags), 2)
    self.assertEqual(flags["--foo"], "1")
    self.assertEqual(flags["--bar"], "2")
    flags = self.CLASS.parse("--foo='1' --bar='2'")
    self.assertEqual(len(flags), 2)
    self.assertEqual(flags["--foo"], "1")
    self.assertEqual(flags["--bar"], "2")

  def test_hashable(self):
    flags = self.CLASS.parse("--foo")
    flags["--bar"] = "10"
    test_set = {flags}
    self.assertIn(flags, test_set)
    self.assertIn(self.CLASS.parse("--foo --bar=10"), test_set)
    self.assertNotIn(self.CLASS.parse("--foo --bar=999"), test_set)
    # post-hash modification are not allowed anymore:
    with self.assertRaises(FrozenFlagsError) as cm:
      flags["--bar"] = "0"
    self.assertIn("frozen", str(cm.exception))

  def test_iter(self):
    flags = self.CLASS.parse("--foo --bar=1")
    self.assertListEqual(list(flags), ["--foo", "--bar=1"])
    self.assertListEqual([*flags], ["--foo", "--bar=1"])


class TestChromeFlags(TestFlags):

  CLASS = ChromeFlags

  def test_js_flags(self):
    flags = self.CLASS({
        "--foo": None,
        "--bar": "v1",
    })
    self.assertIsNone(flags["--foo"])
    self.assertEqual(flags["--bar"], "v1")
    with self.assertRaises(ValueError):
      flags["--js-flags"] = "--js-foo, --no-js-foo"
    flags["--js-flags"] = "--js-foo=v3, --no-js-bar"
    with self.assertRaises(ValueError):
      flags["--js-flags"] = "--js-foo=v4, --no-js-bar"
    js_flags = flags.js_flags
    self.assertEqual(js_flags["--js-foo"], "v3")
    self.assertIsNone(js_flags["--no-js-bar"])

  def test_set_empty_js_flags(self):
    flags = self.CLASS({
        "--foo": None,
        "--bar": "v1",
    })
    self.assertNotIn("--js-flags", flags)
    with self.assertRaises(ValueError):
      flags["--js-flags"] = None
    self.assertNotIn("--js-flags", flags)
    flags["--js-flags"] = ""
    self.assertNotIn("--js-flags", flags)
    flags["--js-flags"] = "  "
    self.assertNotIn("--js-flags", flags)

  def test_set_js_flags_invalid(self):
    flags = self.CLASS()
    for invalid in ("-foo", "--bar'f'", "--", "-8", "--8"):
      with self.subTest(js_flag=invalid):
        with self.assertRaises(ValueError) as cm:
          flags["--js-flags"] = invalid
        self.assertNotIn("--js-flags", flags)
        self.assertIn(invalid, str(cm.exception))
    for invalid in ("--foo=", "--foo,--bar=,--baz", "---foo", "--foo,--,--bar",
                    "--foo;;;,;;;--bar"):
      with self.subTest(js_flag=invalid):
        with self.assertRaises(ValueError) as cm:
          flags["--js-flags"] = invalid
        self.assertNotIn("--js-flags", flags)

  def test_js_flags_initial_data(self):
    flags = self.CLASS({
        "--js-flags": "--foo=v1,--no-bar",
    })
    js_flags = flags.js_flags
    self.assertEqual(js_flags["--foo"], "v1")
    self.assertIsNone(js_flags["--no-bar"])

  def test_features(self):
    flags = self.CLASS()
    features = flags.features
    self.assertTrue(features.is_empty)
    flags["--enable-features"] = "F1,F2"
    with self.assertRaises(ValueError):
      flags["--disable-features"] = "F1,F2"
    with self.assertRaises(ValueError):
      flags["--disable-features"] = "F2,F1"
    flags["--disable-features"] = "F3,F4"
    self.assertEqual(features.enabled, {"F1": None, "F2": None})
    self.assertEqual(features.disabled, set(("F3", "F4")))

  def test_blink_features(self):
    flags = self.CLASS()
    features = flags.blink_features
    self.assertTrue(features.is_empty)
    flags["--enable-blink-features"] = "F1,F2"
    with self.assertRaises(ValueError):
      flags["--disable-blink-features"] = "F1,F2"
    with self.assertRaises(ValueError):
      flags["--disable-blink-features"] = "F2,F1"
    flags["--disable-blink-features"] = "F3,F4"
    self.assertEqual(features.enabled, {"F1": None, "F2": None})
    self.assertEqual(features.disabled, set(("F3", "F4")))

  def test_features_invalid_none(self):
    flags = self.CLASS()
    features = flags.features
    self.assertTrue(features.is_empty)
    with self.assertRaises(ValueError):
      flags["--disable-features"] = None
    self.assertTrue(features.is_empty)
    with self.assertRaises(ValueError):
      flags["--enable-features"] = None
    self.assertTrue(features.is_empty)

  def test_blink_features_invalid_none(self):
    flags = self.CLASS()
    features = flags.blink_features
    self.assertTrue(features.is_empty)
    with self.assertRaises(ValueError):
      flags["--disable-blink-features"] = None
    self.assertTrue(features.is_empty)
    with self.assertRaises(ValueError):
      flags["--enable-blink-features"] = None
    self.assertTrue(features.is_empty)

  def test_user_data_dir(self):
    flags = self.CLASS()
    for invalid in (None, "", "  "):
      with self.subTest(user_dat_dir=invalid):
        with self.assertRaises(ValueError) as cm:
          flags["--user-data-dir"] = invalid
        self.assertIn("empty string", str(cm.exception))

  def test_get_list(self):
    flags = self.CLASS()
    flags["--js-flags"] = "--js-foo=v3, --no-js-bar"
    flags["--enable-features"] = "F1,F2"
    flags["--disable-features"] = "F3,F4"
    flags["--enable-blink-features"] = "BLINK_F1,BLINK_F2"
    flags["--disable-blink-features"] = "BLINK_F3,BLINK_F4"
    flags_list = list(flags)
    self.assertListEqual(flags_list, [
        "--js-flags=--js-foo=v3,--no-js-bar",
        "--enable-features=F1,F2",
        "--disable-features=F3,F4",
        "--enable-blink-features=BLINK_F1,BLINK_F2",
        "--disable-blink-features=BLINK_F3,BLINK_F4",
    ])

  def test_initial_data_empty(self):
    flags = self.CLASS()
    flags_copy = self.CLASS(flags)
    self.assertListEqual(list(flags), list(flags_copy))
    flags_copy = self.CLASS()
    flags_copy.update(flags)
    self.assertListEqual(list(flags), list(flags_copy))

  def test_initial_data_simple(self):
    flags = self.CLASS()
    flags["--no-sandbox"] = None
    flags_copy = self.CLASS(flags)
    self.assertListEqual(list(flags), list(flags_copy))
    flags_copy = self.CLASS()
    flags_copy.update(flags)
    self.assertListEqual(list(flags), list(flags_copy))

  def test_initial_data_js_flags(self):
    flags = self.CLASS()
    flags["--js-flags"] = "--js-foo=v3, --no-js-bar"
    flags_copy = self.CLASS(flags)
    self.assertListEqual(list(flags), list(flags_copy))
    flags_copy = self.CLASS()
    flags_copy.update(flags)
    self.assertListEqual(list(flags), list(flags_copy))

  def test_initial_data_features(self):
    flags = self.CLASS()
    flags["--enable-features"] = "F1,F2"
    flags["--disable-features"] = "F3,F4"
    flags_copy = self.CLASS(flags)
    self.assertListEqual(list(flags), list(flags_copy))
    flags_copy = self.CLASS()
    flags_copy.update(flags)
    self.assertListEqual(list(flags), list(flags_copy))

  def test_initial_data_blink_features(self):
    flags = self.CLASS()
    flags["--enable-blink-features"] = "BLINK_F1,BLINK_F2"
    flags["--disable-blink-features"] = "BLINK_F3,BLINK_F4"
    flags_copy = self.CLASS(flags)
    self.assertListEqual(list(flags), list(flags_copy))
    flags_copy = self.CLASS()
    flags_copy.update(flags)
    self.assertListEqual(list(flags), list(flags_copy))

  def test_initial_data_all(self):
    flags = self.CLASS()
    flags["--no-sandbox"] = None
    flags["--js-flags"] = "--js-foo=v3, --no-js-bar"
    flags["--enable-features"] = "F1,F2"
    flags["--disable-features"] = "F3,F4"
    flags["--enable-blink-features"] = "BLINK_F1,BLINK_F2"
    flags["--disable-blink-features"] = "BLINK_F3,BLINK_F4"
    flags_copy = self.CLASS(flags)
    self.assertListEqual(list(flags), list(flags_copy))
    flags_copy = self.CLASS()
    flags_copy.update(flags)
    self.assertListEqual(list(flags), list(flags_copy))

  def test_set_js_flags(self):
    flags = self.CLASS()
    flags["--js-flags"] = "--foo=a/b/c-d-e.log,--bar=a--b/c ,--no-baz"
    self.assertEqual(flags.js_flags["--foo"], "a/b/c-d-e.log")
    self.assertEqual(flags.js_flags["--bar"], "a--b/c")
    self.assertEqual(flags.js_flags["--no-baz"], None)

  def test_js_flags_separators(self):
    flags_1 = self.CLASS()
    flags_1["--js-flags"] = "--f-one=1,--no-f-two,--f-three=3"
    flags_2 = self.CLASS()
    flags_2["--js-flags"] = "--f-one=1 --no-f-two --f-three=3"
    flags_3 = self.CLASS()
    flags_3["--js-flags"] = "--f-one='1',--no-f-two,--f-three=\"3\""
    flags_4 = self.CLASS()
    flags_4["--js-flags"] = "--f-one='1' --no-f-two, --f-three=\"3\""

    list_1 = list(flags_1.js_flags)
    list_2 = list(flags_2.js_flags)
    self.assertListEqual(list_1, list_2)
    list_3 = list(flags_3.js_flags)
    self.assertListEqual(list_1, list_3)
    list_4 = list(flags_4.js_flags)
    self.assertListEqual(list_1, list_4)

    for flags in (flags_1, flags_2, flags_3):
      self.assertEqual(flags.js_flags["--f-one"], "1")
      self.assertEqual(flags.js_flags["--no-f-two"], None)
      self.assertEqual(flags.js_flags["--f-three"], "3")

  def test_set_invalid_js_flags(self):
    flags = self.CLASS()
    flags["--js-flags"] = "--foo=1--bar"
    for invalid in ("--bar,=", "-bar=1", "--bar,,", "--", "-", "a=b",
                    "--bar==1", "--bar==--bar", "--bar='1\", --foo=1",
                    "--foo='1'--bar", "--bar='a b c'"):
      with self.subTest(invalid=invalid):
        with self.assertRaises(ValueError):
          flags["--js-flags"] = invalid

  def test_merge(self):
    flags = self.CLASS({
        "--foo": "v1",
        "--bar": None,
        "--js-flags": "--log-maps,--log-ic",
        "--enable-features": "feature_1,feature_2",
        "--disable-features": "feature_3",
        "--enable-blink-features": "blink_feature_1,blink_feature_2",
        "--disable-blink-features": "blink_feature_3"
    })
    with self.assertRaises(ValueError):
      flags.merge({"--bar": "v2"})
    with self.assertRaises(ValueError):
      flags.merge({"--js-flags": "--no-log-maps"})
    with self.assertRaises(ValueError):
      flags.merge({"--disable-features": "feature_1,"})
    with self.assertRaises(ValueError):
      flags.merge({"--enable-features": "feature_3"})
    with self.assertRaises(ValueError):
      flags.merge({"--enable-blink-features": "blink_feature_3"})
    flags.merge({
        "--js-flags": "--log-all",
        "--enable-features": "feature_x",
        "--disable-features": "feature_y,feature_z",
        "--enable-blink-features": "blink_feature_x",
        "--disable-blink-features": "blink_feature_y,blink_feature_z"
    })
    self.assertListEqual(
        list(flags.js_flags), ["--log-maps", "--log-ic", "--log-all"])
    self.assertListEqual(
        list(flags.features), [
            "--enable-features=feature_1,feature_2,feature_x",
            "--disable-features=feature_3,feature_y,feature_z"
        ])
    self.assertListEqual(
        list(flags.blink_features), [
            "--enable-blink-features=blink_feature_1,blink_feature_2,blink_feature_x",
            "--disable-blink-features=blink_feature_3,blink_feature_y,blink_feature_z"
        ])

  def test_flag_typos_enable_features(self):
    for invalid_flag in ("--enable-feature", "--enabled-feature",
                         "--enabled-features"):
      with self.assertLogs(level="ERROR") as cm:
        self.CLASS({invalid_flag: "feature_1"})
      output = "\n".join(cm.output)
      self.assertIn(invalid_flag, output)
      self.assertIn("--enable-features", output)

    for invalid_flag in ("--disable-feature", "--disabled-feature",
                         "--disabled-features"):
      with self.assertLogs(level="ERROR") as cm:
        self.CLASS({invalid_flag: "feature_1"})
      output = "\n".join(cm.output)
      self.assertIn(invalid_flag, output)
      self.assertIn("--disable-features", output)

  def test_flag_typos_enable_blink_features(self):
    for invalid_flag in ("--enable-blink-feature", "--enabled-blink-feature",
                         "--enabled-blink-features"):
      with self.assertLogs(level="ERROR") as cm:
        self.CLASS({invalid_flag: "feature_1"})
      output = "\n".join(cm.output)
      self.assertIn(invalid_flag, output)
      self.assertIn("--enable-blink-features", output)

    for invalid_flag in ("--disable-blink-feature", "--disabled-blink-feature",
                         "--disabled-blink-features"):
      with self.assertLogs(level="ERROR") as cm:
        self.CLASS({invalid_flag: "feature_1"})
      output = "\n".join(cm.output)
      self.assertIn(invalid_flag, output)
      self.assertIn("--disable-blink-features", output)


class TestJSFlags(TestFlags):

  CLASS = JSFlags

  def test_invalid_js_flags(self):
    flags = self.CLASS()
    with self.assertRaises(ValueError) as cm:
      flags.set("-foo")
    self.assertIn("'-foo'", str(cm.exception))
    with self.assertRaises(ValueError) as cm:
      flags.set("--foo,--bar")
    self.assertIn("'--foo,--bar'", str(cm.exception))
    with self.assertRaises(ValueError) as cm:
      flags.set("--v8-log", "foo,bar")
    self.assertIn("comma", str(cm.exception).lower())
    self.assertIn("--v8-log", str(cm.exception))
    self.assertIn("foo,bar", str(cm.exception))
    with self.assertRaises(ValueError) as cm:
      flags["--foo"] = "a b c d"
    self.assertIn("whitespace", str(cm.exception).lower())
    self.assertIn("--foo", str(cm.exception))
    self.assertIn("a b c d", str(cm.exception))

  def test_conflicting_flags(self):
    with self.assertRaises(ValueError):
      flags = self.CLASS(("--foo", "--no-foo"))
    with self.assertRaises(ValueError):
      flags = self.CLASS(("--foo", "--nofoo"))
    flags = self.CLASS(("--foo", "--no-bar"))
    self.assertIsNone(flags["--foo"])
    self.assertIsNone(flags["--no-bar"])
    self.assertIn("--foo", flags)
    self.assertNotIn("--no-foo", flags)
    self.assertNotIn("--bar", flags)
    self.assertIn("--no-bar", flags)

  def test_conflicting_override(self):
    flags = self.CLASS(("--foo", "--no-bar"))
    with self.assertRaises(ValueError):
      flags.set("--no-foo")
    with self.assertRaises(ValueError):
      flags.set("--nofoo")
    flags.set("--nobar")
    with self.assertRaises(ValueError):
      flags.set("--bar")
    with self.assertRaises(ValueError):
      flags.set("--foo", "v2")
    self.assertIsNone(flags["--foo"])
    self.assertIsNone(flags["--no-bar"])

    flags.set("--no-foo", override=True)
    self.assertNotIn("--foo", flags)
    self.assertIn("--no-foo", flags)
    self.assertNotIn("--bar", flags)
    self.assertIn("--no-bar", flags)

    flags.set("--bar", override=True)
    self.assertNotIn("--foo", flags)
    self.assertIn("--no-foo", flags)
    self.assertIn("--bar", flags)
    self.assertNotIn("--no-bar", flags)

  def test_str_multiple(self):
    flags = self.CLASS({
        "--flag-a": "value1",
        "--flag-b": None,
        "--flag-c": "value3"
    })
    self.assertEqual(str(flags), "--flag-a=value1,--flag-b,--flag-c=value3")

  def test_initial_data_empty(self):
    flags = self.CLASS()
    flags_copy = self.CLASS(flags)
    self.assertEqual(str(flags), str(flags_copy))
    flags_copy = self.CLASS()
    flags_copy.update(flags)
    self.assertEqual(str(flags), str(flags_copy))

  def test_initial_data(self):
    flags = self.CLASS({
        "--flag-a": "value1",
        "--flag-b": None,
        "--flag-c": "value3"
    })
    flags_copy = self.CLASS(flags)
    self.assertEqual(str(flags), str(flags_copy))
    flags_copy = self.CLASS()
    flags_copy.update(flags)
    self.assertEqual(str(flags), str(flags_copy))

  def test_parse_nested(self):
    self.skipTest("Not supported for JSFlags")

class _ChromeBaseFeaturesTestCase(unittest.TestCase, metaclass=abc.ABCMeta):

  @abc.abstractmethod
  def instance(self) -> ChromeBaseFeatures:
    pass

  def test_empty(self):
    features = self.instance()
    self.assertEqual(str(features), "")
    features_list = list(features)
    self.assertEqual(len(features_list), 0)
    self.assertDictEqual(features.enabled, {})
    self.assertSetEqual(features.disabled, set())

  def test_enable_simple(self):
    features = self.instance()
    features.enable("feature1")
    features.enable("feature2")
    features_list = list(features)
    self.assertEqual(len(features_list), 1)
    features_str = str(features)
    self.assertIn("=feature1,feature2", features_str)

  def test_disable_simple(self):
    features = self.instance()
    features.disable("feature1")
    features.disable("feature2")
    features_list = list(features)
    self.assertEqual(len(features_list), 1)
    features_str = str(features)
    self.assertIn("=feature1,feature2", features_str)

  def test_enable_disable(self):
    features = self.instance()
    features.enable("feature1")
    features.disable("feature2")
    features_list = list(features)
    self.assertEqual(len(features_list), 2)
    features_str = str(features)
    self.assertIn("feature1", features_str)
    self.assertIn("feature2", features_str)
    self.assertDictEqual(features.enabled, {"feature1": None})
    self.assertSetEqual(features.disabled, {"feature2"})

  def test_update_same(self):
    features_1 = self.instance()
    features_1.disable("feature1")
    features_2 = self.instance()
    features_2.disable("feature1")
    features_1.update(features_2)
    self.assertEqual(str(features_1), str(features_2))

  def test_update_add(self):
    features_1 = self.instance()
    features_1.disable("feature1")
    features_1.enable("feature2")
    features_2 = self.instance()
    features_2.disable("featureX")
    features_2.enable("featureY")
    features_1.update(features_2)
    self.assertSetEqual(features_1.disabled, {"feature1", "featureX"})
    self.assertSetEqual(
        set(features_1.enabled.keys()), {"feature2", "featureY"})

  def test_update_conflict(self):
    features_1 = self.instance()
    features_1.enable("feature1")
    features_2 = self.instance()
    features_2.disable("feature1")
    with self.assertRaises(ValueError):
      features_1.update(features_2)

  def test_enable_disable_frozen(self):
    features = self.instance()
    features.freeze()
    with self.assertRaises(FrozenFlagsError):
      features.disable("feature1")
    with self.assertRaises(FrozenFlagsError):
      features.enable("feature2")


class ChromeFeaturesTestCase(_ChromeBaseFeaturesTestCase):

  def instance(self) -> ChromeFeatures:
    return ChromeFeatures()

  def test_enable_complex_features(self):
    features = self.instance()
    features.enable("feature1")
    features.enable("feature2:k1")
    features.enable("feature3:k1/v1/k2/v2")
    features.enable("feature4<Trial1:k1/v1/k2/v2")
    features.enable("feature5<Trial1.Group1:k1/v1/k2/v2")
    features_list = list(features)
    self.assertEqual(len(features_list), 1)

  def test_disable_complex_features(self):
    features = self.instance()
    features.disable("feature1")
    features.disable("feature2:k1")
    features.disable("feature3:k1/v1/k2/v2")
    features.disable("feature4<Trial1:k1/v1/k2/v2")
    features.disable("feature5<Trial1.Group1:k1/v1/k2/v2")
    features_list = list(features)
    self.assertEqual(len(features_list), 1)
    features_str = str(features)
    self.assertIn("feature1", features_str)
    self.assertIn("feature2", features_str)
    self.assertIn("feature3", features_str)
    self.assertIn("feature4", features_str)

  def test_enable_simple_chrome(self):
    features = self.instance()
    features.enable("feature1")
    features.enable("feature2")
    self.assertEqual(str(features), "--enable-features=feature1,feature2")

  def test_disable_simple_chrome(self):
    features = self.instance()
    features.disable("feature1")
    features.disable("feature2")
    self.assertEqual(str(features), "--disable-features=feature1,feature2")

  def test_enable_disable_chrome(self):
    features = self.instance()
    features.enable("feature1")
    features.disable("feature2")
    self.assertEqual(
        str(features), "--enable-features=feature1 --disable-features=feature2")

  def test_enable_disable_complex(self):
    features = self.instance()
    features.enable("feature0")
    features.enable("feature1:k1/v1")
    features.enable("feature2<Trial.Group:k2/v2")
    features.disable("feature3:k3/v3")
    self.assertDictEqual(features.enabled, {
        "feature0": None,
        "feature1": ":k1/v1",
        "feature2": "<Trial.Group:k2/v2"
    })
    self.assertSetEqual(features.disabled, {"feature3"})

  def test_conflicting_values_enabled(self):
    features = self.instance()
    features.enable("feature1")
    features.enable("feature1")
    with self.assertRaises(ValueError):
      features.disable("feature1")
    with self.assertRaises(ValueError):
      features.enable("feature1:k1/v1")
    features_str = str(features)
    self.assertEqual(features_str, "--enable-features=feature1")

  def test_conflicting_values_disabled(self):
    features = self.instance()
    features.disable("feature1")
    features.disable("feature1")
    with self.assertRaises(ValueError):
      features.enable("feature1")
    features.disable("feature1:k1/v1")
    features_str = str(features)
    self.assertEqual(features_str, "--disable-features=feature1")


class ChromeBlinkFeaturesTestCase(_ChromeBaseFeaturesTestCase):

  def instance(self) -> ChromeBlinkFeatures:
    return ChromeBlinkFeatures()

  def test_empty(self):
    features = self.instance()
    self.assertEqual(str(features), "")
    features_list = list(features)
    self.assertEqual(len(features_list), 0)
    self.assertDictEqual(features.enabled, {})
    self.assertSetEqual(features.disabled, set())

  def test_enable_basic_features(self):
    features = self.instance()
    features.enable("feature1")

  def test_enable_invalid(self):
    features = self.instance()
    for invalid in ("feature2:k1", "feature3:k1/v1/k2/v2",
                    "feature4<Trial1:k1/v1/k2/v2",
                    "feature5<Trial1.Group1:k1/v1/k2/v2"):
      with self.assertRaises(ValueError):
        features.enable(invalid)
    self.assertTrue(features.is_empty)

  def test_enable_simple_chrome_blink(self):
    features = self.instance()
    features.enable("feature1")
    features.enable("feature2")
    self.assertEqual(str(features), "--enable-blink-features=feature1,feature2")

  def test_disable_simple_chrome_blink(self):
    features = self.instance()
    features.disable("feature1")
    features.disable("feature2")
    self.assertEqual(
        str(features), "--disable-blink-features=feature1,feature2")

  def test_enable_disable_chrome_blink(self):
    features = self.instance()
    features.enable("feature1")
    features.disable("feature2")
    self.assertEqual(
        str(features),
        "--enable-blink-features=feature1 --disable-blink-features=feature2")


del _ChromeBaseFeaturesTestCase

if __name__ == "__main__":
  test_helper.run_pytest(__file__)
