# Copyright 2023 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from __future__ import annotations

import argparse
import dataclasses
import enum
import json
import pathlib
import unittest
from typing import Any, Dict, List, Optional

from immutabledict import immutabledict

from crossbench import cli_helper, compat
from crossbench.config import ConfigEnum, ConfigObject, ConfigParser
from tests.crossbench.mock_helper import CrossbenchFakeFsTestCase


@enum.unique
class GenericEnum(compat.StrEnumWithHelp):
  A = ("a", "A Help")
  B = ("b", "B Help")
  C = ("c", "C Help")


@enum.unique
class CustomConfigEnum(ConfigEnum):
  A = ("a", "A Help")
  B = ("b", "B Help")
  C = ("c", "C Help")

class CustomValueEnum(enum.Enum):

  @classmethod
  def _missing_(cls, value: Any) -> Optional[CustomValueEnum]:
    if value is True:
      return CustomValueEnum.A_OR_TRUE
    if value is False:
      return CustomValueEnum.B_OR_FALSE
    return super()._missing_(value)

  DEFAULT = "default"
  A_OR_TRUE = "a"
  B_OR_FALSE = "b"


@dataclasses.dataclass(frozen=True)
class CustomNestedConfigObject(ConfigObject):
  name: str

  @classmethod
  def loads(cls, value: str) -> CustomNestedConfigObject:
    if ":" in value:
      raise ValueError("Invalid Config")
    if not value:
      raise ValueError("Got empty input")
    return cls(name=value)

  @classmethod
  def load_dict(cls, config: Dict[str, Any]) -> CustomNestedConfigObject:
    return cls.config_parser().parse(config)

  @classmethod
  def config_parser(cls) -> ConfigParser[CustomNestedConfigObject]:
    parser = ConfigParser("CustomNestedConfigObject parser", cls)
    parser.add_argument("name", type=str, required=True)
    return parser


@dataclasses.dataclass(frozen=True)
class CustomConfigObject(ConfigObject):

  name: str
  array: Optional[List[str]] = None
  integer: Optional[int] = None
  nested: Optional[CustomNestedConfigObject] = None
  choices: str = ""
  generic_enum: GenericEnum = GenericEnum.A
  config_enum: CustomConfigEnum = CustomConfigEnum.A
  custom_value_enum: CustomValueEnum = CustomValueEnum.DEFAULT
  depending_nested: Optional[Dict[str, Any]] = None
  depending_many: Optional[Dict[str, Any]] = None

  @classmethod
  def default(cls) -> CustomConfigObject:
    return cls("default")

  @classmethod
  def loads(cls, value: str) -> CustomConfigObject:
    if ":" in value:
      raise ValueError("Invalid Config")
    if not value:
      raise ValueError("Got empty input")
    return cls(name=value)

  @classmethod
  def parse_depending_nested(
      cls, value: Optional[str],
      nested: CustomNestedConfigObject) -> Optional[Dict]:
    if not value:
      return None
    return {
        "value": cli_helper.parse_non_empty_str(value),
        "nested": cli_helper.parse_not_none(nested, "nested")
    }

  @classmethod
  def parse_depending_many(cls, value: Optional[str], array: List[Any],
                           integer: int,
                           nested: CustomNestedConfigObject) -> Optional[Dict]:
    if not value:
      return None
    return {
        "value": cli_helper.parse_non_empty_str(value),
        "nested": cli_helper.parse_not_none(nested, "nested"),
        "array": cli_helper.parse_not_none(array, "array"),
        "integer": cli_helper.parse_positive_int(integer, "integer"),
    }


  @classmethod
  def load_dict(cls, config: Dict[str, Any]) -> CustomConfigObject:
    return cls.config_parser().parse(config)

  @classmethod
  def config_parser(cls) -> ConfigParser[CustomConfigObject]:
    parser = cls.base_config_parse()
    parser.add_argument(
        "name", aliases=("name_alias", "name_alias2"), type=str, required=True)
    parser.add_argument("array", type=list)
    parser.add_argument("integer", type=cli_helper.parse_positive_int)
    parser.add_argument("nested", type=CustomNestedConfigObject)
    parser.add_argument("generic_enum", type=GenericEnum)
    parser.add_argument("config_enum", type=CustomConfigEnum)
    parser.add_argument(
        "custom_value_enum",
        type=CustomValueEnum,
        default=CustomValueEnum.DEFAULT)
    parser.add_argument("choices", type=str, choices=("x", "y", "z"))
    parser.add_argument(
        "depending_nested",
        type=CustomConfigObject.parse_depending_nested,
        depends_on=("nested",))
    parser.add_argument(
        "depending_many",
        type=CustomConfigObject.parse_depending_many,
        depends_on=("array", "integer", "nested"))
    return parser

  @classmethod
  def base_config_parse(cls) -> ConfigParser[CustomConfigObject]:
    return ConfigParser("CustomConfigObject parser", cls)


class CustomConfigObjectWithDefault(CustomConfigObject):

  @classmethod
  def base_config_parse(cls) -> ConfigParser[CustomConfigObjectWithDefault]:
    return ConfigParser("CustomConfigObject parser", cls, default=cls.default())


class ConfigParserTestCase(unittest.TestCase):

  def setUp(self):
    super().setUp()
    self.parser = ConfigParser("ConfigParserTestCase parser",
                               CustomConfigObject)

  def test_invalid_type(self):
    with self.assertRaises(TypeError):
      self.parser.add_argument("foo", type="something")  # pytype: disable=wrong-arg-types

  def test_invalid_alias(self):
    with self.assertRaises(ValueError):
      self.parser.add_argument("foo", aliases=("foo",), type=str)
    with self.assertRaises(ValueError):
      self.parser.add_argument(
          "foo", aliases=("foo_alias", "foo_alias"), type=str)

  def test_duplicate(self):
    self.parser.add_argument("foo", type=str)
    with self.assertRaises(ValueError):
      self.parser.add_argument("foo", type=str)
    with self.assertRaises(ValueError):
      self.parser.add_argument("foo2", aliases=("foo",), type=str)

  def test_invalid_string_depends_on(self):
    with self.assertRaises(TypeError):
      self.parser.add_argument(
          "custom",
          type=CustomConfigObject.parse_depending_nested,
          depends_on="other")  # pytype: disable=wrong-arg-types

  def test_invalid_depends_on_nof_arguments(self):
    with self.assertRaises(TypeError) as cm:
      self.parser.add_argument("any", type=lambda x: x, depends_on=("other",))
    self.assertIn("arguments", str(cm.exception))

  def test_invalid_depends_on(self):
    with self.assertRaises(ValueError):
      self.parser.add_argument("any", type=None, depends_on=("other",))

    with self.assertRaises(ValueError):
      self.parser.add_argument("enum", type=GenericEnum, depends_on=("other",))
    with self.assertRaises(ValueError):
      self.parser.add_argument("enum", type=ConfigEnum, depends_on=("other",))

    for primitive_type in (bool, float, int, str):
      with self.assertRaises(TypeError):
        self.parser.add_argument(
            "param", type=primitive_type, depends_on=("other",))

  def test_recursive_depends_on(self):
    self.parser.add_argument(
        "x", type=lambda value, y: value + y, depends_on=("y",))
    self.parser.add_argument(
        "y", type=lambda value, x: value + x, depends_on=("x",))
    with self.assertRaises(argparse.ArgumentTypeError) as cm:
      self.parser.parse({"x": 1, "y": 100})
    self.assertIn("Recursive", str(cm.exception))

  def test_default(self):
    self.parser.add_argument("name", type=str, required=True)
    with self.assertRaises(argparse.ArgumentTypeError) as cm:
      self.parser.parse({})
    self.assertIn("no value", str(cm.exception).lower())
    parser = ConfigParser(
        "ConfigParserTestCase parser",
        CustomConfigObject,
        default=CustomConfigObject.default())
    config = parser.parse({})
    self.assertEqual(config, CustomConfigObject.default())

  def test_invalid_default(self):
    with self.assertRaises(TypeError) as cm:
      ConfigParser(  # pytype: disable=wrong-arg-types
          "ConfigParserTestCase parser",
          CustomConfigObject,
          default="something else")
    self.assertIn("instance", str(cm.exception))


class ConfigObjectTestCase(CrossbenchFakeFsTestCase):

  def test_help(self):
    help_text = CustomConfigObject.config_parser().help
    self.assertIn("name", help_text)
    self.assertIn("array", help_text)
    self.assertIn("integer", help_text)
    self.assertIn("nested", help_text)
    self.assertIn("generic_enum", help_text)
    self.assertIn("config_enum", help_text)
    self.assertIn("custom_value_enum", help_text)
    self.assertIn("choices", help_text)
    self.assertIn("depending_nested", help_text)
    self.assertIn("depending_many", help_text)

  def test_value_has_path_prefix(self):
    for value in ("/foo/bar", "~/foo/bar", "../foo/bar", "./foo/bar",
                  "C:\\foo\\bar", "C:/foo/bar"):
      with self.subTest(value=value):
        self.assertTrue(CustomConfigObject.value_has_path_prefix(value))
    for value in ("foo/bar", "foo:bar", "foo", "{foo:'/foo/bar'}"):
      with self.subTest(value=value):
        self.assertFalse(CustomConfigObject.value_has_path_prefix(value))

  def test_load_invalid_str(self):
    for invalid in ("", None, 1, []):
      with self.assertRaises(argparse.ArgumentTypeError):
        CustomConfigObject.parse(invalid)

  def test_load_dict_invalid(self):
    with self.assertRaises(argparse.ArgumentTypeError):
      CustomConfigObject.parse({})
    with self.assertRaises(argparse.ArgumentTypeError):
      CustomConfigObject.parse({"name": "foo", "array": 1})
    with self.assertRaises(argparse.ArgumentTypeError):
      CustomConfigObject.parse({"name": "foo", "name_alias": "foo"})
    with self.assertRaises(argparse.ArgumentTypeError):
      CustomConfigObject.parse({"name": "foo", "array": [], "integer": "a"})
    with self.assertRaises(argparse.ArgumentTypeError):
      CustomConfigObject.load_dict({"name": "foo", "array": [], "integer": "a"})

  def test_load_dict(self):
    config = CustomConfigObject.parse({"name": "foo"})
    assert isinstance(config, CustomConfigObject)
    self.assertEqual(config.name, "foo")
    config = CustomConfigObject.parse({"name": "foo", "array": []})
    self.assertEqual(config.name, "foo")
    self.assertListEqual(config.array, [])
    data = {"name": "foo", "array": [1, 2, 3], "integer": 153}
    config = CustomConfigObject.parse(dict(data))
    assert isinstance(config, CustomConfigObject)
    self.assertEqual(config.name, "foo")
    self.assertListEqual(config.array, [1, 2, 3])
    self.assertEqual(config.integer, 153)
    config_2 = CustomConfigObject.load_dict(dict(data))
    assert isinstance(config, CustomConfigObject)
    self.assertEqual(config, config_2)

  def test_load_dict_default(self):
    self.assertIsNone(CustomConfigObject.config_parser().default)
    with self.assertRaises(argparse.ArgumentTypeError):
      CustomConfigObject.parse({})
    self.assertIsNone(CustomConfigObject.config_parser().default,
                      CustomConfigObjectWithDefault.default())
    config = CustomConfigObjectWithDefault.parse({})
    self.assertEqual(config, CustomConfigObjectWithDefault.default())

  def test_load_dict_alias(self):
    config = CustomConfigObject.parse({"name_alias": "foo"})
    assert isinstance(config, CustomConfigObject)
    self.assertEqual(config.name, "foo")

  def test_load_dict_custom_value_enum(self):
    config = CustomConfigObject.parse({"name_alias": "foo"})
    assert isinstance(config, CustomConfigObject)
    self.assertIs(config.custom_value_enum, CustomValueEnum.DEFAULT)
    for config_value, result in ((CustomValueEnum.A_OR_TRUE,
                                  CustomValueEnum.A_OR_TRUE),
                                 ("a", CustomValueEnum.A_OR_TRUE),
                                 (True, CustomValueEnum.A_OR_TRUE),
                                 (CustomValueEnum.B_OR_FALSE,
                                  CustomValueEnum.B_OR_FALSE),
                                 ("b", CustomValueEnum.B_OR_FALSE),
                                 (False, CustomValueEnum.B_OR_FALSE),
                                 ("default", CustomValueEnum.DEFAULT)):
      config = CustomConfigObject.parse({
          "name_alias": "foo",
          "custom_value_enum": config_value
      })
      self.assertIs(config.custom_value_enum, result)

  def test_load_dict_custom_value_enum_invalid(self):
    for invalid in (1, 2, {}, "A", "B"):
      with self.assertRaises(argparse.ArgumentTypeError) as cm:
        CustomConfigObject.parse({
            "name_alias": "foo",
            "custom_value_enum": invalid
        })
      self.assertIn(f"{invalid}", str(cm.exception))

  def test_loads(self):
    config = CustomConfigObject.parse("a name")
    assert isinstance(config, CustomConfigObject)
    self.assertEqual(config.name, "a name")

  def test_load_path_missing_file(self):
    path = pathlib.Path("invalid.file")
    self.assertFalse(path.exists())
    with self.assertRaises(argparse.ArgumentTypeError):
      CustomConfigObject.parse(path)
    with self.assertRaises(argparse.ArgumentTypeError):
      CustomConfigObject.load_path(path)

  def test_load_path_empty_file(self):
    path = pathlib.Path("test_file.json")
    self.assertFalse(path.exists())
    path.touch()
    with self.assertRaises(argparse.ArgumentTypeError):
      CustomConfigObject.parse(path)
    with self.assertRaises(argparse.ArgumentTypeError):
      CustomConfigObject.load_path(path)

  def test_load_path_invalid_json_file(self):
    path = pathlib.Path("test_file.json")
    path.write_text("{{", encoding="utf-8")
    with self.assertRaises(argparse.ArgumentTypeError):
      CustomConfigObject.parse(path)
    with self.assertRaises(argparse.ArgumentTypeError):
      CustomConfigObject.load_path(path)

  def test_load_path_empty_json_object(self):
    path = pathlib.Path("test_file.json")
    with path.open("w", encoding="utf-8") as f:
      json.dump({}, f)
    with self.assertRaises(argparse.ArgumentTypeError) as cm:
      CustomConfigObject.parse(path)
    self.assertIn("non-empty data", str(cm.exception))

  def test_load_path_invalid_json_array(self):
    path = pathlib.Path("test_file.json")
    with path.open("w", encoding="utf-8") as f:
      json.dump([], f)
    with self.assertRaises(argparse.ArgumentTypeError) as cm:
      CustomConfigObject.parse(path)
    self.assertIn("non-empty data", str(cm.exception))

  def test_load_path_minimal(self):
    path = pathlib.Path("test_file.json")
    with path.open("w", encoding="utf-8") as f:
      json.dump({"name": "Config Name"}, f)
    config = CustomConfigObject.load_path(path)
    assert isinstance(config, CustomConfigObject)
    self.assertEqual(config.name, "Config Name")
    self.assertIsNone(config.array)
    self.assertIsNone(config.integer)
    self.assertIsNone(config.nested)
    config_2 = CustomConfigObject.parse(str(path))
    self.assertEqual(config, config_2)

  TEST_DICT = immutabledict({
      "name": "Config Name",
      "array": [1, 3],
      "integer": 166
  })

  def test_load_path_full(self):
    path = pathlib.Path("test_file.json")
    with path.open("w", encoding="utf-8") as f:
      json.dump(dict(self.TEST_DICT), f)
    config = CustomConfigObject.load_path(path)
    assert isinstance(config, CustomConfigObject)
    self.assertEqual(config.name, "Config Name")
    self.assertListEqual(config.array, [1, 3])
    self.assertEqual(config.integer, 166)
    self.assertIsNone(config.nested)
    config_2 = CustomConfigObject.parse(str(path))
    self.assertEqual(config, config_2)

  def test_load_dict_full(self):
    config = CustomConfigObject.load_dict(dict(self.TEST_DICT))
    assert isinstance(config, CustomConfigObject)
    self.assertEqual(config.name, "Config Name")
    self.assertListEqual(config.array, [1, 3])
    self.assertEqual(config.integer, 166)
    self.assertIsNone(config.nested)

  TEST_DICT_NESTED = immutabledict({"name": "a nested name"})

  def test_load_dict_nested(self):
    test_dict = dict(self.TEST_DICT)
    test_dict["nested"] = dict(self.TEST_DICT_NESTED)
    config = CustomConfigObject.load_dict(test_dict)
    assert isinstance(config, CustomConfigObject)
    self.assertEqual(config.name, "Config Name")
    self.assertListEqual(config.array, [1, 3])
    self.assertEqual(config.integer, 166)
    self.assertEqual(config.nested,
                     CustomNestedConfigObject(name="a nested name"))

  def test_load_dict_nested_file(self):
    path = pathlib.Path("nested.json")
    self.assertFalse(path.exists())
    with path.open("w", encoding="utf-8") as f:
      json.dump(dict(self.TEST_DICT_NESTED), f)
    test_dict = dict(self.TEST_DICT)
    test_dict["nested"] = str(path)
    config = CustomConfigObject.load_dict(test_dict)
    assert isinstance(config, CustomConfigObject)
    self.assertEqual(config.nested,
                     CustomNestedConfigObject(name="a nested name"))

  def test_load_missing_depending(self):
    with self.assertRaises(argparse.ArgumentTypeError) as cm:
      CustomConfigObject.parse({"name": "foo", "depending_nested": "a value"})
    self.assertIn("depending_nested", str(cm.exception))
    self.assertIn("Expected nested", str(cm.exception))
    with self.assertRaises(argparse.ArgumentTypeError) as cm:
      CustomConfigObject.parse({
          "name": "foo",
          "depending_nested": "a value",
          "nested": None
      })
    self.assertIn("depending_nested", str(cm.exception))
    self.assertIn("Expected nested", str(cm.exception))

  def test_load_depending_simple(self):
    config = CustomConfigObject.parse({
        "name": "foo",
        "nested": "nested string value",
        "depending_nested": "a value"
    })
    self.assertDictEqual(config.depending_nested, {
        "value": "a value",
        "nested": config.nested
    })

  def test_load_generic_enum(self):
    test_dict = dict(self.TEST_DICT)
    test_dict["generic_enum"] = "b"
    config = CustomConfigObject.load_dict(test_dict)
    self.assertIs(config.generic_enum, GenericEnum.B)
    test_dict = dict(self.TEST_DICT)
    test_dict["generic_enum"] = "c"
    config = CustomConfigObject.load_dict(test_dict)
    self.assertIs(config.generic_enum, GenericEnum.C)

  def test_load_generic_enum_invalid(self):
    test_dict = dict(self.TEST_DICT)
    test_dict["generic_enum"] = "unknown value"
    with self.assertRaises(argparse.ArgumentTypeError) as cm:
      CustomConfigObject.load_dict(test_dict)
    error_message = str(cm.exception).lower()
    self.assertIn("choices are", error_message)
    self.assertIn("generic_enum", error_message)

  def test_load_config_enum(self):
    test_dict = dict(self.TEST_DICT)
    test_dict["config_enum"] = "b"
    config = CustomConfigObject.load_dict(test_dict)
    self.assertIs(config.config_enum, CustomConfigEnum.B)
    test_dict = dict(self.TEST_DICT)
    test_dict["config_enum"] = "c"
    config = CustomConfigObject.load_dict(test_dict)
    self.assertIs(config.config_enum, CustomConfigEnum.C)

  def test_load_custom_enum_invalid(self):
    test_dict = dict(self.TEST_DICT)
    test_dict["config_enum"] = "unknown value"
    with self.assertRaises(argparse.ArgumentTypeError) as cm:
      CustomConfigObject.load_dict(test_dict)
    error_message = str(cm.exception).lower()
    self.assertIn("choices are", error_message)
    self.assertIn("config_enum", error_message)


class ConfigEnumTestCase(unittest.TestCase):

  def test_load_invalid(self):
    for invalid in ("", None):
      with self.assertRaises(argparse.ArgumentTypeError) as cm:
        CustomConfigEnum.parse(invalid)
      error_message = str(cm.exception)
      self.assertIn("Choices are", error_message)
      self.assertIn("CustomConfigEnum", error_message)

  def test_parse(self):
    for value, result in ((CustomConfigEnum.A,
                           CustomConfigEnum.A), ("a", CustomConfigEnum.A),
                          (CustomConfigEnum.B,
                           CustomConfigEnum.B), ("c", CustomConfigEnum.C)):
      self.assertIs(CustomConfigEnum.parse(value), result)
