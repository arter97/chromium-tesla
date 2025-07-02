# Copyright 2023 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from __future__ import annotations

import argparse
import datetime as dt
import json
import pathlib
import unittest
from typing import Any

from crossbench.cli_helper import (
    Duration, parse_bool, parse_dict, parse_dir_path, parse_existing_file_path,
    parse_float, parse_hjson_file_path, parse_httpx_url_str, parse_inline_hjson,
    parse_int, parse_json_file, parse_json_file_path, parse_non_empty_dict,
    parse_non_empty_dir_path, parse_non_empty_file_path, parse_non_empty_str,
    parse_path, parse_port, parse_positive_int, parse_positive_zero_float,
    parse_positive_zero_int, parse_sh_cmd, parse_unique_sequence)
from tests.crossbench.mock_helper import CrossbenchFakeFsTestCase


class DurationTestCase(unittest.TestCase):

  def test_parse_negative(self):
    with self.assertRaises(argparse.ArgumentTypeError):
      Duration.parse(-1)
    with self.assertRaises(argparse.ArgumentTypeError) as cm:
      Duration.parse("-1")
    with self.assertRaises(argparse.ArgumentTypeError) as cm:
      Duration.parse_zero("-1")
    with self.assertRaises(argparse.ArgumentTypeError) as cm:
      Duration.parse_zero(dt.timedelta(seconds=-1))
    with self.assertRaises(argparse.ArgumentTypeError) as cm:
      Duration.parse_non_zero("-1")
    with self.assertRaises(argparse.ArgumentTypeError) as cm:
      Duration.parse_non_zero(dt.timedelta(seconds=-1))
    self.assertIn("-1", str(cm.exception))
    self.assertEqual(Duration.parse_any("-1.5").total_seconds(), -1.5)

  def test_parse_zero(self):
    self.assertEqual(Duration.parse_any("0").total_seconds(), 0)
    self.assertEqual(Duration.parse_any("0s").total_seconds(), 0)
    self.assertEqual(Duration.parse_any("0.0").total_seconds(), 0)
    self.assertEqual(Duration.parse_zero("0.0").total_seconds(), 0)
    for invalid in (-1, 0, "-1", "0", "invalid", dt.timedelta(0),
                    dt.timedelta(seconds=-1)):
      with self.assertRaises(argparse.ArgumentTypeError) as cm:
        Duration.parse(invalid)
      self.assertIn(str(invalid), str(cm.exception))
      with self.assertRaises(argparse.ArgumentTypeError) as cm:
        Duration.parse_non_zero(invalid)
      self.assertIn(str(invalid), str(cm.exception))

  def test_parse_empty(self):
    with self.assertRaises(argparse.ArgumentTypeError):
      Duration.parse("")
    with self.assertRaises(argparse.ArgumentTypeError):
      Duration.parse_any("")
    with self.assertRaises(argparse.ArgumentTypeError):
      Duration.parse_zero("")
    with self.assertRaises(argparse.ArgumentTypeError):
      Duration.parse_non_zero("")

  def test_invalid_suffix(self):
    with self.assertRaises(argparse.ArgumentTypeError) as cm:
      Duration.parse("100XXX")
    self.assertIn("Unknown duration format", str(cm.exception))
    with self.assertRaises(argparse.ArgumentTypeError):
      Duration.parse("X0XX")
    with self.assertRaises(argparse.ArgumentTypeError):
      Duration.parse("100X0XX")

  def test_no_unit(self):
    self.assertEqual(Duration.parse("200"), dt.timedelta(seconds=200))
    self.assertEqual(Duration.parse(200), dt.timedelta(seconds=200))

  def test_milliseconds(self):
    self.assertEqual(Duration.parse("27.5ms"), dt.timedelta(milliseconds=27.5))
    self.assertEqual(
        Duration.parse(dt.timedelta(milliseconds=27.5)),
        dt.timedelta(milliseconds=27.5))
    self.assertEqual(
        Duration.parse("27.5 millis"), dt.timedelta(milliseconds=27.5))
    self.assertEqual(
        Duration.parse("27.5 milliseconds"), dt.timedelta(milliseconds=27.5))

  def test_seconds(self):
    self.assertEqual(Duration.parse("27.5s"), dt.timedelta(seconds=27.5))
    self.assertEqual(Duration.parse("1 sec"), dt.timedelta(seconds=1))
    self.assertEqual(Duration.parse("27.5 secs"), dt.timedelta(seconds=27.5))
    self.assertEqual(Duration.parse("1 second"), dt.timedelta(seconds=1))
    self.assertEqual(Duration.parse("27.5 seconds"), dt.timedelta(seconds=27.5))

  def test_minutes(self):
    self.assertEqual(Duration.parse("27.5m"), dt.timedelta(minutes=27.5))
    self.assertEqual(Duration.parse("1 min"), dt.timedelta(minutes=1))
    self.assertEqual(Duration.parse("27.5 mins"), dt.timedelta(minutes=27.5))
    self.assertEqual(Duration.parse("1 minute"), dt.timedelta(minutes=1))
    self.assertEqual(Duration.parse("27.5 minutes"), dt.timedelta(minutes=27.5))

  def test_hours(self):
    self.assertEqual(Duration.parse("27.5h"), dt.timedelta(hours=27.5))
    self.assertEqual(Duration.parse("0.1 h"), dt.timedelta(hours=0.1))
    self.assertEqual(Duration.parse("27.5 hrs"), dt.timedelta(hours=27.5))
    self.assertEqual(Duration.parse("1 hour"), dt.timedelta(hours=1))
    self.assertEqual(Duration.parse("27.5 hours"), dt.timedelta(hours=27.5))


class ArgParserHelperTestCase(CrossbenchFakeFsTestCase):

  def setUp(self):
    super().setUp()
    self._json_test_data = {"int": 1, "array": [1, "2"]}

  def test_parse_non_empty_str(self):
    self.assertEqual(parse_non_empty_str("a string"), "a string")
    with self.assertRaises(argparse.ArgumentTypeError) as cm:
      parse_non_empty_str("")
    self.assertIn("empty", str(cm.exception))

  def test_parse_httpx_url_str(self):
    for valid in ("http://foo.com", "https://foo.com", "http://localhost:800"):
      self.assertEqual(parse_httpx_url_str(valid), valid)
    for invalid in ("", "ftp://localhost:32", "http://///"):
      with self.assertRaises(argparse.ArgumentTypeError) as cm:
        _ = parse_httpx_url_str(invalid)
      self.assertIn(invalid, str(cm.exception))

  def test_parse_int(self):
    self.assertEqual(parse_int("-123456"), -123456)
    self.assertEqual(parse_int(-123456), -123456)
    self.assertEqual(parse_int("-1"), -1)
    self.assertEqual(parse_int(-1), -1)
    self.assertEqual(parse_int("0"), 0)
    self.assertEqual(parse_int(0), 0)
    self.assertEqual(parse_int("1"), 1)
    self.assertEqual(parse_int(1), 1)
    self.assertEqual(parse_int("123456"), 123456)
    self.assertEqual(parse_int(123456), 123456)

  def test_parse_int_invalid(self):
    for invalid in ("", "-1.2", "1.2", "100.001", "Nan", "inf", "-inf",
                    "invalid"):
      with self.assertRaises(argparse.ArgumentTypeError):
        _ = parse_int(invalid)

  def test_parse_positive_int(self):
    self.assertEqual(parse_positive_int("1"), 1)
    self.assertEqual(parse_positive_int("123"), 123)

  def test_parse_positive_int_ivalid(self):
    for invalid in ("", "0", "-1", "-1.2", "1.2", "Nan", "inf", "-inf",
                    "invalid"):
      with self.assertRaises(argparse.ArgumentTypeError):
        _ = parse_positive_int(invalid)

  def test_parse_positive_zero_int(self):
    self.assertEqual(parse_positive_zero_int("1"), 1)
    self.assertEqual(parse_positive_zero_int("0"), 0)

  def test_parse_positive_zero_int_invalid(self):
    for invalid in ("", "-1", "-1.2", "1.2", "NaN", "inf", "-inf", "invalid"):
      with self.assertRaises(argparse.ArgumentTypeError):
        _ = parse_positive_zero_int(invalid)

  def test_parse_float(self):
    self.assertEqual(parse_float("-1.2"), -1.2)
    self.assertEqual(parse_float(-1.2), -1.2)
    self.assertEqual(parse_float("-1"), -1.0)
    self.assertEqual(parse_float(-1), -1.0)
    self.assertEqual(parse_float("0"), 0.0)
    self.assertEqual(parse_float(0), 0.0)
    self.assertEqual(parse_float("0.0"), 0.0)
    self.assertEqual(parse_float(0.0), 0.0)
    self.assertEqual(parse_float("0.1"), 0.1)
    self.assertEqual(parse_float(0.1), 0.1)

  def test_parse_float_invalid(self):
    for invalid in ("", "abc", "NaN", "inf", "-inf", "invalid"):
      with self.assertRaises(argparse.ArgumentTypeError):
        _ = parse_positive_zero_float(invalid)

  def test_parse_positive_zero_float(self):
    self.assertEqual(parse_positive_zero_float("1"), 1.0)
    self.assertEqual(parse_positive_zero_float("0"), 0.0)
    self.assertEqual(parse_positive_zero_float("0.0"), 0.0)
    self.assertEqual(parse_positive_zero_float("1.23"), 1.23)

  def test_parse_positive_zero_float_invlid(self):
    for invalid in ("", "-1", "-1.2", "NaN", "inf", "-inf", "invalid"):
      with self.assertRaises(argparse.ArgumentTypeError):
        _ = parse_positive_zero_float(invalid)

  def test_parse_port(self):
    self.assertEqual(parse_port(1), 1)
    self.assertEqual(parse_port("1"), 1)
    self.assertEqual(parse_port(440), 440)
    self.assertEqual(parse_port("440"), 440)
    self.assertEqual(parse_port(65535), 65535)
    self.assertEqual(parse_port("65535"), 65535)

  def test_parse_port_invalid(self):
    for invalid in ("", "-1", "-1.2", "6553500", "inf", "-inf", "invalid"):
      with self.assertRaises(argparse.ArgumentTypeError):
        _ = parse_port(invalid)

  def _json_file_test_helper(self, parser) -> Any:
    with self.assertRaises(argparse.ArgumentTypeError):
      parser("file")

    path = pathlib.Path("file.json")
    self.assertFalse(path.exists())
    with self.assertRaises(argparse.ArgumentTypeError):
      parser(path)

    path.touch()
    with self.assertRaises(argparse.ArgumentTypeError):
      parser(path)

    with path.open("w", encoding="utf-8") as f:
      f.write("{invalid json data")
    with self.assertRaises(argparse.ArgumentTypeError):
      parser(path)
    # Test very long lines too.
    with path.open("w", encoding="utf-8") as f:
      f.write("{\n invalid json data" + "." * 100)
    with self.assertRaises(argparse.ArgumentTypeError):
      parser(path)

    with path.open("w", encoding="utf-8") as f:
      f.write("""{
              'a': {},
              'c': }}
              """)
    with self.assertRaises(argparse.ArgumentTypeError):
      parser(path)

    with path.open("w", encoding="utf-8") as f:
      json.dump(self._json_test_data, f)
    str_result = parser(str(path))
    path_result = parser(path)
    self.assertEqual(str_result, path_result)
    return str_result

  def test_parse_json_file(self):
    result = self._json_file_test_helper(parse_json_file)
    self.assertDictEqual(self._json_test_data, result)

  def test_parse_json_file_path(self):
    result = self._json_file_test_helper(parse_json_file_path)
    self.assertEqual(pathlib.Path("file.json"), result)

  def test_parse_hjson_file_path(self):
    result = self._json_file_test_helper(parse_hjson_file_path)
    self.assertEqual(pathlib.Path("file.json"), result)

  def test_parse_inline_hjson(self):
    with self.assertRaises(argparse.ArgumentTypeError):
      parse_inline_hjson("")
    with self.assertRaises(argparse.ArgumentTypeError):
      parse_inline_hjson("{invalid json}")
    with self.assertRaises(argparse.ArgumentTypeError):
      parse_inline_hjson("{'asdfas':'asdf}")
    self.assertDictEqual(self._json_test_data,
                         parse_inline_hjson(json.dumps(self._json_test_data)))

  def test_parse_dir_path(self):
    with self.assertRaises(argparse.ArgumentTypeError):
      parse_dir_path("")
    file = pathlib.Path("file")
    with self.assertRaises(argparse.ArgumentTypeError) as cm:
      parse_dir_path(file)
    self.assertIn("does not exist", str(cm.exception))
    file.touch()
    with self.assertRaises(argparse.ArgumentTypeError) as cm:
      parse_dir_path(file)
    self.assertIn("not a folder", str(cm.exception))
    folder = pathlib.Path("folder")
    folder.mkdir()
    self.assertEqual(folder, parse_dir_path(folder))
    self.assertEqual(folder, parse_dir_path(str(folder)))

  def test_parse_non_empty_dir_path(self):
    folder = pathlib.Path("folder")
    folder.mkdir()
    with self.assertRaises(argparse.ArgumentTypeError) as cm:
      parse_non_empty_dir_path(folder)
    self.assertIn("empty", str(cm.exception))
    (folder / "foo").touch()
    self.assertEqual(folder, parse_non_empty_dir_path(folder))
    self.assertEqual(folder, parse_non_empty_dir_path(str(folder)))

  def test_parse_non_empty_file_path(self):
    with self.assertRaises(argparse.ArgumentTypeError):
      parse_non_empty_file_path("")
    folder = pathlib.Path("folder")
    with self.assertRaises(argparse.ArgumentTypeError) as cm:
      parse_non_empty_file_path(folder)
    self.assertIn("does not exist", str(cm.exception))
    folder.mkdir()
    with self.assertRaises(argparse.ArgumentTypeError) as cm:
      parse_non_empty_file_path(folder)
    self.assertIn("not a file", str(cm.exception))
    file = pathlib.Path("file")
    file.touch()
    with self.assertRaises(argparse.ArgumentTypeError) as cm:
      self.assertEqual(file, parse_non_empty_file_path(file))
    self.assertIn("is an empty file", str(cm.exception))

    with file.open("w") as f:
      f.write("fooo")
    self.assertEqual(file, parse_non_empty_file_path(file))

  def test_parse_existing_file_path(self):
    with self.assertRaises(argparse.ArgumentTypeError):
      parse_existing_file_path("")
    folder = pathlib.Path("folder")
    with self.assertRaises(argparse.ArgumentTypeError) as cm:
      parse_existing_file_path(folder)
    self.assertIn("does not exist", str(cm.exception))
    folder.mkdir()
    with self.assertRaises(argparse.ArgumentTypeError) as cm:
      parse_existing_file_path(folder)
    self.assertIn("not a file", str(cm.exception))
    file = pathlib.Path("file")
    file.touch()
    self.assertEqual(file, parse_existing_file_path(file))

  def test_parse_path(self):
    with self.assertRaises(argparse.ArgumentTypeError):
      parse_path("")
    folder = pathlib.Path("folder")
    folder.mkdir()
    self.assertEqual(folder, parse_path(folder))
    file = pathlib.Path("file")
    file.touch()
    self.assertEqual(file, parse_path(file))

  def test_bool_success(self):
    self.assertIs(parse_bool("true"), True)
    self.assertIs(parse_bool("True"), True)
    self.assertIs(parse_bool(True), True)
    self.assertIs(parse_bool("false"), False)
    self.assertIs(parse_bool("False"), False)
    self.assertIs(parse_bool(False), False)

  def test_bool_invalid(self):
    for invalid in (1, 0, "1", "0", "", None, [], tuple()):
      with self.assertRaises(argparse.ArgumentTypeError):
        parse_bool(invalid)

  def test_parse_sh_cmd(self):
    self.assertListEqual(parse_sh_cmd("ls -al ."), ["ls", "-al", "."])
    self.assertListEqual(parse_sh_cmd("ls -al '.'"), ["ls", "-al", "."])
    self.assertListEqual(parse_sh_cmd(";ls -al '.'"), [";ls", "-al", "."])
    self.assertListEqual(parse_sh_cmd(("ls", "-al", ".")), ["ls", "-al", "."])

  def test_parse_sh_cmd_invalid(self):
    for invalid in (1, "", None, [], "ls -al \"."):
      with self.assertRaises(argparse.ArgumentTypeError):
        parse_sh_cmd(invalid)

  def test_parse_dict_invalid(self):
    for invalid in (1, 0, "1", "0", "", None, [], tuple()):
      with self.assertRaises(argparse.ArgumentTypeError):
        parse_dict(invalid)

  def test_parse_dict(self):
    self.assertDictEqual(parse_dict({}), {})
    self.assertDictEqual(parse_dict({"A": 2}), {"A": 2})

  def test_parse_non_empty_dict_invalid(self):
    for invalid in (1, 0, "1", "0", "", None, [], tuple(), dict()):
      with self.assertRaises(argparse.ArgumentTypeError):
        parse_non_empty_dict(invalid)

  def test_parse_non_empty_dict(self):
    result = parse_non_empty_dict({"a": 1})
    self.assertDictEqual(result, {"a": 1})

  def test_parse_unique_sequence(self):
    self.assertListEqual(parse_unique_sequence([]), [])
    self.assertTupleEqual(parse_unique_sequence(tuple()), tuple())
    self.assertListEqual(parse_unique_sequence([1, 2, 3]), [1, 2, 3])
    self.assertTupleEqual(parse_unique_sequence((1, 2, 3)), (1, 2, 3))

  def test_parse_unique_sequence_invalid(self):
    with self.assertRaises(argparse.ArgumentTypeError) as cm:
      parse_unique_sequence([1, 1, 2, 2, 2, 3, 5, 5])
    self.assertIn("duplicates", str(cm.exception))
    self.assertIn("1, 2, 5", str(cm.exception))

  def test_parse_unique_sequence_custom_exception(self):

    class CustomException(Exception):
      pass

    with self.assertRaises(CustomException):
      parse_unique_sequence([1, 1], error_cls=CustomException)

  def test_parse_unique_sequence_custom_name(self):
    with self.assertRaises(argparse.ArgumentTypeError) as cm:
      parse_unique_sequence([1, 1], name="custom test name")
    self.assertIn("custom test name", str(cm.exception))
