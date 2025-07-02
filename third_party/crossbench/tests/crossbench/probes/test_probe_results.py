# Copyright 2022 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import pathlib
import unittest

from crossbench.probes.probe import Probe
from crossbench.probes.results import (BrowserProbeResult, EmptyProbeResult,
                                       LocalProbeResult, ProbeResultDict)
from crossbench.runner.run import Run
from tests import test_helper
from tests.crossbench.mock_helper import (BaseCrossbenchTestCase,
                                          CrossbenchFakeFsTestCase)


class ProbeResultTestCase(CrossbenchFakeFsTestCase):

  def test_is_empty(self):
    empty = EmptyProbeResult()
    self.assertTrue(empty.is_empty)
    self.assertFalse(empty)
    combined = empty.merge(EmptyProbeResult())
    self.assertTrue(combined.is_empty)
    url = LocalProbeResult(url=["http://test.com"])
    self.assertFalse(url.is_empty)
    self.assertTrue(url)
    combined = empty.merge(url)
    self.assertFalse(combined.is_empty)
    self.assertTrue(combined)

  def test_equal_empty(self):
    empty = EmptyProbeResult()
    self.assertEqual(empty, empty)
    self.assertEqual(EmptyProbeResult(), EmptyProbeResult())
    local_empty = LocalProbeResult()
    self.assertEqual(local_empty, EmptyProbeResult())
    self.assertEqual(local_empty, local_empty)
    self.assertNotEqual(LocalProbeResult(), None)
    self.assertNotEqual(None, LocalProbeResult())

  def test_is_remote(self):
    empty = EmptyProbeResult()
    self.assertFalse(empty.is_remote)
    local_empty = LocalProbeResult()
    self.assertFalse(local_empty.is_remote)

  def test_equal_single(self):
    url = "http://test.com"
    self.assertEqual(
        LocalProbeResult(url=[
            url,
        ]), LocalProbeResult(url=[
            url,
        ]))
    url_b = "http://foo.test.com"
    self.assertNotEqual(
        LocalProbeResult(url=[
            url,
        ]), LocalProbeResult(url=[
            url_b,
        ]))


  def test_invalid_files(self):
    with self.assertRaises(ValueError):
      LocalProbeResult(file=[pathlib.Path("foo.json")])
    with self.assertRaises(ValueError):
      LocalProbeResult(file=[pathlib.Path("foo.csv")])
    with self.assertRaises(ValueError):
      LocalProbeResult(json=[pathlib.Path("foo.csv")])
    with self.assertRaises(ValueError):
      LocalProbeResult(csv=[pathlib.Path("foo.json")])

  def test_inexistent_files(self):
    with self.assertRaises(ValueError):
      LocalProbeResult(file=[pathlib.Path("not_there.txt")])
    with self.assertRaises(ValueError):
      LocalProbeResult(csv=[pathlib.Path("not_there.csv")])
    with self.assertRaises(ValueError):
      LocalProbeResult(json=[pathlib.Path("not_there.json")])

  def test_result_url(self):
    url = "http://foo.bar.com"
    result = LocalProbeResult(url=[url])
    self.assertFalse(result.is_empty)
    self.assertEqual(result.url, url)
    self.assertListEqual(result.url_list, [url])
    self.assertListEqual(list(result.all_files()), [])
    failed = None
    with self.assertRaises(AssertionError):
      failed = result.file
    with self.assertRaises(AssertionError):
      failed = result.json
    with self.assertRaises(AssertionError):
      failed = result.csv
    self.assertIsNone(failed)
    json_data = result.to_json()
    self.assertDictEqual(json_data, {"url": (url,)})

  def test_result_any_file(self):
    path = self.create_file("result.txt")
    result = LocalProbeResult(file=[path])
    self.assertFalse(result.is_empty)
    self.assertNotEqual(
        result, LocalProbeResult(file=[
            self.create_file("result2.txt"),
        ]))
    self.assertEqual(result.file, path)
    self.assertListEqual(result.file_list, [path])
    self.assertListEqual(list(result.all_files()), [path])
    failed = None
    with self.assertRaises(AssertionError):
      failed = result.url
    with self.assertRaises(AssertionError):
      failed = result.json
    with self.assertRaises(AssertionError):
      failed = result.csv
    self.assertIsNone(failed)

  def test_result_csv(self):
    path = self.create_file("result.csv")
    result = LocalProbeResult(csv=[path])
    self.assertFalse(result.is_empty)
    self.assertNotEqual(
        result, LocalProbeResult(csv=[
            self.create_file("result2.csv"),
        ]))
    self.assertEqual(result.csv, path)
    self.assertListEqual(result.csv_list, [path])
    self.assertListEqual(list(result.all_files()), [path])
    failed = None
    with self.assertRaises(AssertionError):
      failed = result.file
    with self.assertRaises(AssertionError):
      failed = result.file
    with self.assertRaises(AssertionError):
      failed = result.json
    self.assertIsNone(failed)

  def test_result_json(self):
    path = self.create_file("result.json")
    result = LocalProbeResult(json=[path])
    self.assertFalse(result.is_empty)
    self.assertNotEqual(
        result, LocalProbeResult(json=[
            self.create_file("result2.json"),
        ]))
    self.assertEqual(result.json, path)
    self.assertListEqual(result.json_list, [path])
    self.assertListEqual(list(result.all_files()), [path])
    failed = None
    with self.assertRaises(AssertionError):
      failed = result.url
    with self.assertRaises(AssertionError):
      failed = result.file
    with self.assertRaises(AssertionError):
      failed = result.csv
    self.assertIsNone(failed)

  def test_merge(self):
    file = self.create_file("result.custom")
    json = self.create_file("result.json")
    csv = self.create_file("result.csv")
    url = "http://foo.bar.com"

    result = LocalProbeResult(url=[url], file=[file], json=[json], csv=[csv])
    self.assertFalse(result.is_empty)
    self.assertListEqual(list(result.all_files()), [file, json, csv])
    self.assertListEqual(result.url_list, [url])

    merged = result.merge(EmptyProbeResult())
    self.assertFalse(merged.is_empty)
    self.assertListEqual(list(merged.all_files()), [file, json, csv])
    self.assertListEqual(result.url_list, [url])

    file_2 = self.create_file("result.2.custom")
    json_2 = self.create_file("result.2.json")
    csv_2 = self.create_file("result.2.csv")
    url_2 = "http://foo.bar.com/2"
    other = LocalProbeResult(
        url=[url_2], file=[file_2], json=[json_2], csv=[csv_2])
    merged = result.merge(other)
    self.assertFalse(merged.is_empty)
    self.assertListEqual(
        list(merged.all_files()), [file, file_2, json, json_2, csv, csv_2])
    self.assertListEqual(merged.url_list, [url, url_2])
    # result is unchanged:
    self.assertFalse(result.is_empty)
    self.assertListEqual(list(result.all_files()), [file, json, csv])
    self.assertListEqual(result.url_list, [url])
    # other is unchanged:
    self.assertFalse(other.is_empty)
    self.assertListEqual(list(other.all_files()), [file_2, json_2, csv_2])
    self.assertListEqual(other.url_list, [url_2])


class MockRun:

  def __init__(self, browser, browser_platform):
    self.brower = browser
    self.browser_platform = browser_platform
    self.is_remote = False


class BrowserProbeResultTestCase(BaseCrossbenchTestCase):

  def setUp(self) -> None:
    super().setUp()
    self.run = MockRun(self.browsers[0], self.platform)

  def test_empty(self):
    result = BrowserProbeResult(self.run)
    self.assertTrue(result.is_empty)
    self.assertFalse(result.is_remote)

  def test_remote_empty(self):
    self.run.is_remote = True
    result = BrowserProbeResult(self.run)
    self.assertTrue(result.is_empty)
    self.assertEqual(result, EmptyProbeResult())
    self.assertEqual(result, LocalProbeResult())

  def test_remote_only_urls(self):
    self.run.is_remote = True
    result = BrowserProbeResult(
        self.run, url=[
            "http://test.com",
        ])
    self.assertFalse(result.is_empty)
    self.assertNotEqual(result, EmptyProbeResult())
    self.assertEqual(result, LocalProbeResult(url=[
        "http://test.com",
    ]))
    self.assertNotEqual(result, LocalProbeResult(url=[
        "http://test.com/bar",
    ]))

  def test_local_browser_equal_local_run(self):
    url = "http://foo.bar.com"
    file = self.create_file("results/file.any")
    json = self.create_file("results/file.json")
    csv = self.create_file("results/file.csv")
    local = LocalProbeResult([url], [file], [json], [csv])
    browser = BrowserProbeResult(self.run, [url], [file], [json], [csv])
    self.assertEqual(local, browser)

  def test_copy_remote_files(self):
    # pylint: disable=attribute-defined-outside-init
    out_dir_local = pathlib.Path("local/results")
    out_dir_remote = pathlib.Path("remote/results")
    out_dir_local.mkdir(parents=True)
    out_dir_remote.mkdir(parents=True)
    self.assertTrue(out_dir_local.is_dir())
    self.assertTrue(out_dir_remote.is_dir())
    remote_file = self.create_file(
        out_dir_remote / "result.any", contents="a1b2c3")
    self.run.is_remote = True
    self.run.out_dir = out_dir_local
    self.run.browser_tmp_dir = out_dir_remote
    result = BrowserProbeResult(
        self.run, file=[
            remote_file,
        ])
    self.assertTrue(remote_file.is_file())
    self.assertNotEqual(remote_file, result.file)
    self.assertTrue(result.file.is_file())
    self.assertEqual(result.file.read_text(), "a1b2c3")


class MockProbe(Probe):
  """
  Probe DOC Text
  """
  NAME = "mock-probe"

  def get_context(self, run: Run):
    pass


class ProbeResultDictTestCase(unittest.TestCase):

  def setUp(self) -> None:
    super().setUp()
    self.result_location = pathlib.Path("test/out/results")
    self.result_dict = ProbeResultDict(self.result_location)
    self.local_result = LocalProbeResult()

  def test_create_empty(self):
    self.assertEqual(len(self.result_dict), 0)
    self.assertFalse(self.result_dict)
    self.assertNotIn(MockProbe(), self.result_dict)

  def test_get_item(self):
    probe = MockProbe()
    with self.assertRaises(KeyError):
      _ = self.result_dict[probe]
    self.result_dict[probe] = self.local_result
    self.assertIs(self.result_dict[probe], self.local_result)

  def test_get(self):
    probe = MockProbe()
    self.assertIsNone(self.result_dict.get(probe))
    self.assertEqual(self.result_dict.get(probe, 1234), 1234)
    self.result_dict[probe] = self.local_result
    self.assertIs(self.result_dict.get(probe), self.local_result)




if __name__ == "__main__":
  test_helper.run_pytest(__file__)
