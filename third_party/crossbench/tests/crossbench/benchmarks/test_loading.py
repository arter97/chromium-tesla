# Copyright 2022 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# pytype: disable=attribute-error

from __future__ import annotations

import argparse
import datetime as dt
import json
import pathlib
import unittest
from typing import Sequence, cast

import hjson
from pyfakefs import fake_filesystem_unittest

import crossbench
import crossbench.env
import crossbench.path
import crossbench.runner
from crossbench.benchmarks.loading.loading_benchmark import (LoadingPageFilter,
                                                             PageLoadBenchmark)
from crossbench.benchmarks.loading.page import (PAGE_LIST, PAGE_LIST_SMALL,
                                                CombinedPage, LivePage)
from crossbench.benchmarks.loading.page_config import (
    DevToolsRecorderPagesConfig, ListPagesConfig, PageConfig, PagesConfig)
from crossbench.benchmarks.loading.playback_controller import (
    ForeverPlaybackController, PlaybackController, RepeatPlaybackController,
    TimeoutPlaybackController)
from crossbench.runner.runner import Runner
from tests import test_helper
from tests.crossbench.benchmarks import helper
from tests.crossbench.mock_helper import (BaseCliTestCase,
                                          CrossbenchFakeFsTestCase)

cb = crossbench


class PlaybackControllerTest(unittest.TestCase):

  def test_parse_invalid(self):
    for invalid in [
        "11", "something", "1.5x", "4.3.h", "4.5.x", "-1x", "-1.4x", "-2h",
        "-2.1h", "1h30", "infx", "infh", "nanh", "nanx", "0s", "0"
    ]:
      with self.subTest(pattern=invalid):
        with self.assertRaises((argparse.ArgumentTypeError, ValueError)):
          PlaybackController.parse(invalid)

  def test_invalid_repeat(self):
    with self.assertRaises(argparse.ArgumentTypeError):
      PlaybackController.repeat(-1)

  def test_parse_repeat(self):
    playback = PlaybackController.parse("once")
    self.assertIsInstance(playback, RepeatPlaybackController)
    assert isinstance(playback, RepeatPlaybackController)
    self.assertEqual(playback.count, 1)
    self.assertEqual(len(list(playback)), 1)

    playback = PlaybackController.parse("1x")
    self.assertIsInstance(playback, RepeatPlaybackController)
    assert isinstance(playback, RepeatPlaybackController)
    self.assertEqual(playback.count, 1)
    self.assertEqual(len(list(playback)), 1)

    playback = PlaybackController.parse("11x")
    self.assertIsInstance(playback, RepeatPlaybackController)
    assert isinstance(playback, RepeatPlaybackController)
    self.assertEqual(playback.count, 11)
    self.assertEqual(len(list(playback)), 11)

  def test_parse_forever(self):
    playback = PlaybackController.parse("forever")
    self.assertIsInstance(playback, ForeverPlaybackController)
    playback = PlaybackController.parse("inf")
    self.assertIsInstance(playback, ForeverPlaybackController)
    playback = PlaybackController.parse("infinity")
    self.assertIsInstance(playback, ForeverPlaybackController)

  def test_parse_duration(self):
    playback = PlaybackController.parse("5s")
    self.assertIsInstance(playback, TimeoutPlaybackController)
    assert isinstance(playback, TimeoutPlaybackController)
    self.assertEqual(playback.duration, dt.timedelta(seconds=5))

    playback = PlaybackController.parse("5m")
    self.assertIsInstance(playback, TimeoutPlaybackController)
    assert isinstance(playback, TimeoutPlaybackController)
    self.assertEqual(playback.duration, dt.timedelta(minutes=5))

    playback = PlaybackController.parse("5.5m")
    self.assertIsInstance(playback, TimeoutPlaybackController)
    assert isinstance(playback, TimeoutPlaybackController)
    self.assertEqual(playback.duration, dt.timedelta(minutes=5.5))

    playback = PlaybackController.parse("5.5m")
    self.assertIsInstance(playback, TimeoutPlaybackController)
    assert isinstance(playback, TimeoutPlaybackController)
    self.assertEqual(playback.duration, dt.timedelta(minutes=5.5))

  def test_once(self):
    iterations = sum(1 for _ in PlaybackController.once())
    self.assertEqual(iterations, 1)
    iterations = sum(1 for _ in PlaybackController.default())
    self.assertEqual(iterations, 1)

  def test_repeat(self):
    iterations = sum(1 for _ in PlaybackController.repeat(1))
    self.assertEqual(iterations, 1)
    iterations = sum(1 for _ in PlaybackController.repeat(11))
    self.assertEqual(iterations, 11)

  def test_timeout(self):
    # Even 0-duration playback should run once
    iterations = sum(1 for _ in PlaybackController.timeout(dt.timedelta()))
    self.assertEqual(iterations, 1)
    iterations = sum(
        1 for _ in PlaybackController.timeout(dt.timedelta(milliseconds=0.1)))
    self.assertGreaterEqual(iterations, 1)

  def test_forever(self):
    count = 0
    for _ in PlaybackController.forever():
      # Just run for some large-ish amount of iterations to get code coverage.
      count += 1
      if count > 100:
        break


class TestPageLoadBenchmark(helper.SubStoryTestCase):

  @property
  def benchmark_cls(self):
    return PageLoadBenchmark

  def story_filter(  # pylint: disable=arguments-differ
      self,
      patterns: Sequence[str],
      separate: bool = True,
      playback: PlaybackController = PlaybackController.default(),
      about_blank_duration: dt.timedelta = dt.timedelta()) -> LoadingPageFilter:
    args = argparse.Namespace(
        about_blank_duration=about_blank_duration, playback=playback)
    return cast(LoadingPageFilter,
                super().story_filter(patterns, args=args, separate=separate))

  def test_all_stories(self):
    stories = self.story_filter(["all"]).stories
    self.assertGreater(len(stories), 1)
    for story in stories:
      self.assertIsInstance(story, LivePage)
    names = set(story.name for story in stories)
    self.assertEqual(len(names), len(stories))
    self.assertEqual(names, set(page.name for page in PAGE_LIST))

  def test_default_stories(self):
    stories = self.story_filter(["default"]).stories
    self.assertGreater(len(stories), 1)
    for story in stories:
      self.assertIsInstance(story, LivePage)
    names = set(story.name for story in stories)
    self.assertEqual(len(names), len(stories))
    self.assertEqual(names, set(page.name for page in PAGE_LIST_SMALL))

  def test_combined_stories(self):
    stories = self.story_filter(["all"], separate=False).stories
    self.assertEqual(len(stories), 1)
    combined = stories[0]
    self.assertIsInstance(combined, CombinedPage)

  def test_filter_by_name(self):
    for preset_page in PAGE_LIST:
      stories = self.story_filter([preset_page.name]).stories
      self.assertListEqual([p.url for p in stories], [preset_page.url])
    with self.assertRaises(argparse.ArgumentTypeError) as cm:
      self.story_filter([])
    self.assertIn("empty", str(cm.exception).lower())

  def test_filter_by_name_with_duration(self):
    pages = PAGE_LIST
    filtered_pages = self.story_filter([pages[0].name, pages[1].name,
                                        "1001"]).stories
    self.assertListEqual([p.url for p in filtered_pages],
                         [pages[0].url, pages[1].url])
    self.assertEqual(filtered_pages[0].duration, pages[0].duration)
    self.assertEqual(filtered_pages[1].duration, dt.timedelta(seconds=1001))

  def test_page_by_url(self):
    url1 = "http://example.com/test1"
    url2 = "http://example.com/test2"
    stories = self.story_filter([url1, url2]).stories
    self.assertEqual(len(stories), 2)
    self.assertEqual(stories[0].url, url1)
    self.assertEqual(stories[1].url, url2)

  def test_page_by_url_www(self):
    url1 = "www.example.com/test1"
    url2 = "www.example.com/test2"
    stories = self.story_filter([url1, url2]).stories
    self.assertEqual(len(stories), 2)
    self.assertEqual(stories[0].url, f"https://{url1}")
    self.assertEqual(stories[1].url, f"https://{url2}")

  def test_page_by_url_combined(self):
    url1 = "http://example.com/test1"
    url2 = "http://example.com/test2"
    stories = self.story_filter([url1, url2], separate=False).stories
    self.assertEqual(len(stories), 1)
    combined = stories[0]
    self.assertIsInstance(combined, CombinedPage)

  def test_run_combined(self):
    stories = [CombinedPage(PAGE_LIST)]
    self._test_run(stories)
    self._assert_urls_loaded([story.url for story in PAGE_LIST])

  def test_run_default(self):
    stories = PAGE_LIST
    self._test_run(stories)
    self._assert_urls_loaded([story.url for story in stories])

  def test_run_throw(self):
    stories = PAGE_LIST
    self._test_run(stories)
    self._assert_urls_loaded([story.url for story in stories])

  def test_run_repeat_with_about_blank(self):
    url1 = "https://www.example.com/test1"
    url2 = "https://www.example.com/test2"
    stories = self.story_filter(
        [url1, url2],
        separate=False,
        about_blank_duration=dt.timedelta(seconds=1)).stories
    self._test_run(stories)
    urls = [url1, "about:blank", url2, "about:blank"]
    self._assert_urls_loaded(urls)

  def test_run_repeat_with_about_blank_separate(self):
    url1 = "https://www.example.com/test1"
    url2 = "https://www.example.com/test2"
    stories = self.story_filter(
        [url1, url2],
        separate=True,
        about_blank_duration=dt.timedelta(seconds=1)).stories
    self._test_run(stories)
    urls = [url1, "about:blank", url2, "about:blank"]
    self._assert_urls_loaded(urls)

  def test_run_repeat(self):
    url1 = "https://www.example.com/test1"
    url2 = "https://www.example.com/test2"
    stories = self.story_filter([url1, url2],
                                separate=False,
                                playback=PlaybackController.repeat(3)).stories
    self._test_run(stories)
    urls = [url1, url2] * 3
    self._assert_urls_loaded(urls)

  def test_run_repeat_separate(self):
    url1 = "https://www.example.com/test1"
    url2 = "https://www.example.com/test2"
    stories = self.story_filter([url1, url2],
                                separate=True,
                                playback=PlaybackController.repeat(3)).stories
    self._test_run(stories)
    urls = [url1] * 3 + [url2] * 3
    self._assert_urls_loaded(urls)

  def _test_run(self, stories, throw: bool = False):
    benchmark = self.benchmark_cls(stories)
    self.assertTrue(len(benchmark.describe()) > 0)
    runner = Runner(
        self.out_dir,
        self.browsers,
        benchmark,
        env_config=cb.env.HostEnvironmentConfig(),
        env_validation_mode=cb.env.ValidationMode.SKIP,
        platform=self.platform,
        throw=throw)
    runner.run()
    self.assertTrue(runner.is_success)
    self.assertTrue(self.browsers[0].did_run)
    self.assertTrue(self.browsers[1].did_run)

  def _assert_urls_loaded(self, story_urls):
    browser_1_urls = self.filter_splashscreen_urls(self.browsers[0].url_list)
    self.assertEqual(browser_1_urls, story_urls)
    browser_2_urls = self.filter_splashscreen_urls(self.browsers[1].url_list)
    self.assertEqual(browser_2_urls, story_urls)


class TestExamplePageConfig(unittest.TestCase):

  @unittest.skipIf(hjson.__name__ != "hjson", "hjson not available")
  def test_parse_example_page_config_file(self):
    example_config_file = test_helper.config_dir() / "page.config.example.hjson"
    file_config = PagesConfig.parse(example_config_file)
    with example_config_file.open(encoding="utf-8") as f:
      data = hjson.load(f)
    dict_config = PagesConfig.load_dict(data)
    self.assertTrue(dict_config.pages)
    self.assertTrue(file_config.pages)
    for page in dict_config.pages:
      self.assertGreater(len(page.actions), 1)

  @unittest.skipIf(hjson.__name__ != "hjson", "hjson not available")
  def test_parse_android_page_config_file(self):
    example_config_file = (
        test_helper.config_dir() / "woa" / "android_input_page_config.hjson")
    file_config = PagesConfig.parse(example_config_file)
    with example_config_file.open(encoding="utf-8") as f:
      data = hjson.load(f)
    dict_config = PagesConfig.load_dict(data)
    self.assertTrue(dict_config.pages)
    self.assertTrue(file_config.pages)
    for page in dict_config.pages:
      self.assertGreater(len(page.actions), 1)

  @unittest.skipIf(hjson.__name__ != "hjson", "hjson not available")
  def test_parse_loading_page_config_phone(self):
    config_file = (
        test_helper.config_dir() / "woa" / "loading" /
        "page_config_phone.hjson")
    file_config = PagesConfig.parse(config_file)
    with config_file.open(encoding="utf-8") as f:
      data = hjson.load(f)
    dict_config = PagesConfig.load_dict(data)
    self.assertTrue(dict_config.pages)
    self.assertTrue(file_config.pages)
    for page in dict_config.pages:
      self.assertGreater(len(page.actions), 1)

  @unittest.skipIf(hjson.__name__ != "hjson", "hjson not available")
  def test_parse_loading_page_config_tablet(self):
    config_file = (
        test_helper.config_dir() / "woa" / "loading" /
        "page_config_tablet.hjson")
    file_config = PagesConfig.parse(config_file)
    with config_file.open(encoding="utf-8") as f:
      data = hjson.load(f)
    dict_config = PagesConfig.load_dict(data)
    self.assertTrue(dict_config.pages)
    self.assertTrue(file_config.pages)
    for page in dict_config.pages:
      self.assertGreater(len(page.actions), 1)


class PageConfigTestsCase(unittest.TestCase):

  def test_parse_empty(self):
    with self.assertRaises(argparse.ArgumentTypeError) as cm:
      PageConfig.parse("")
    self.assertIn("empty", str(cm.exception).lower())

  def test_parse_unknown_type(self):
    with self.assertRaises(argparse.ArgumentTypeError) as cm:
      PageConfig.parse(self)
    self.assertIn("type", str(cm.exception))

  def test_parse_blank(self):
    config = PageConfig.parse("about:blank")
    self.assertEqual(config.label, "blank")
    self.assertEqual(config.url, "about:blank")

  def test_parse_file(self):
    config = PageConfig.parse("file://foo/bar/custom.html")
    self.assertEqual(config.label, "custom.html")
    self.assertEqual(config.url, "file://foo/bar/custom.html")

  def test_parse_url(self):
    config = PageConfig.parse("http://www.a.com")
    self.assertEqual(config.url, "http://www.a.com")
    self.assertEqual(config.duration, dt.timedelta())
    self.assertEqual(config.label, "a.com")

  def test_parse_url_no_protocol(self):
    config = PageConfig.parse("www.a.com")
    self.assertEqual(config.url, "https://www.a.com")
    self.assertEqual(config.duration, dt.timedelta())
    self.assertEqual(config.label, "a.com")

  def test_parse_url_numbers(self):
    config = PageConfig.parse("123.a.com")
    self.assertEqual(config.url, "https://123.a.com")
    self.assertEqual(config.duration, dt.timedelta())
    self.assertEqual(config.label, "123.a.com")

  def test_parse_with_duration(self):
    config = PageConfig.parse("http://news.b.com,123s")
    self.assertEqual(config.url, "http://news.b.com")
    self.assertEqual(config.duration.total_seconds(), 123)
    self.assertEqual(config.label, "news.b.com")

  def test_parse_invalid_multiple_urls(self):
    with self.assertRaises(argparse.ArgumentTypeError):
      PageConfig.parse("111.a.com,222.b.com")
    with self.assertRaises(argparse.ArgumentTypeError):
      PageConfig.parse("111s,222.b.com")

  def test_parse_multiple_comma(self):
    # duration splitting should happen in the caller
    config = PageConfig.parse("www.b.com/foo?bar=a,b,c,d,123s")
    self.assertEqual(config.url, "https://www.b.com/foo?bar=a,b,c,d")
    self.assertEqual(config.duration.total_seconds(), 123)
    self.assertEqual(config.label, "b.com")


class PagesConfigTestCase(CrossbenchFakeFsTestCase):

  def test_parse_unknown_type(self):
    with self.assertRaises(argparse.ArgumentTypeError) as cm:
      PagesConfig.parse(self)
    self.assertIn("type", str(cm.exception))

  def test_parse_invalid(self):
    with self.assertRaises(argparse.ArgumentTypeError) as cm:
      PagesConfig.parse("123s,")
    self.assertIn("Duration", str(cm.exception))
    with self.assertRaises(argparse.ArgumentTypeError) as cm:
      PagesConfig.parse(",")
    self.assertIn("empty", str(cm.exception))
    with self.assertRaises(argparse.ArgumentTypeError) as cm:
      PagesConfig.parse("http://foo.com,,")
    self.assertIn("empty", str(cm.exception))
    with self.assertRaises(argparse.ArgumentTypeError) as cm:
      PagesConfig.parse("http://foo.com,123s,")
    self.assertIn("empty", str(cm.exception))
    with self.assertRaises(argparse.ArgumentTypeError) as cm:
      PagesConfig.parse("http://foo.com,123s,123s")
    self.assertIn("Duration", str(cm.exception))

  def test_parse_single(self):
    config = PagesConfig.parse("http://a.com")
    self.assertEqual(len(config.pages), 1)
    page_config = config.pages[0]
    self.assertEqual(page_config.url, "http://a.com")

  def test_parse_single_with_duration(self):
    config = PagesConfig.parse("http://a.com,123s")
    self.assertEqual(len(config.pages), 1)
    page_config = config.pages[0]
    self.assertEqual(page_config.url, "http://a.com")
    self.assertEqual(page_config.duration.total_seconds(), 123)

  def test_parse_multiple(self):
    config = PagesConfig.parse("http://a.com,http://b.com")
    self.assertEqual(len(config.pages), 2)
    page_config_0, page_config_1 = config.pages
    self.assertEqual(page_config_0.url, "http://a.com")
    self.assertEqual(page_config_1.url, "http://b.com")

  def test_parse_multiple_short_domain(self):
    config = PagesConfig.parse("a.com,b.com")
    self.assertEqual(len(config.pages), 2)
    page_config_0, page_config_1 = config.pages
    self.assertEqual(page_config_0.url, "https://a.com")
    self.assertEqual(page_config_1.url, "https://b.com")

  def test_parse_multiple_numeric_domain(self):
    config = PagesConfig.parse("111.a.com,222.b.com")
    self.assertEqual(len(config.pages), 2)
    page_config_0, page_config_1 = config.pages
    self.assertEqual(page_config_0.url, "https://111.a.com")
    self.assertEqual(page_config_1.url, "https://222.b.com")

  def test_parse_multiple_numeric_domain_with_duration(self):
    config = PagesConfig.parse("111.a.com,12s,222.b.com,23s")
    self.assertEqual(len(config.pages), 2)
    page_config_0, page_config_1 = config.pages
    self.assertEqual(page_config_0.url, "https://111.a.com")
    self.assertEqual(page_config_1.url, "https://222.b.com")
    self.assertEqual(page_config_0.duration.total_seconds(), 12)
    self.assertEqual(page_config_1.duration.total_seconds(), 23)

  def test_parse_multiple_with_duration(self):
    config = PagesConfig.parse("http://a.com,123s,http://b.com")
    self.assertEqual(len(config.pages), 2)
    page_config_0, page_config_1 = config.pages
    self.assertEqual(page_config_0.url, "http://a.com")
    self.assertEqual(page_config_1.url, "http://b.com")
    self.assertEqual(page_config_0.duration.total_seconds(), 123)
    self.assertEqual(page_config_1.duration, dt.timedelta())

  def test_parse_multiple_with_duration_end(self):
    config = PagesConfig.parse("http://a.com,http://b.com,123s")
    self.assertEqual(len(config.pages), 2)
    page_config_0, page_config_1 = config.pages
    self.assertEqual(page_config_0.url, "http://a.com")
    self.assertEqual(page_config_1.url, "http://b.com")
    self.assertEqual(page_config_0.duration, dt.timedelta())
    self.assertEqual(page_config_1.duration.total_seconds(), 123)

  def test_parse_multiple_with_duration_all(self):
    config = PagesConfig.parse("http://a.com,1s,http://b.com,123s")
    self.assertEqual(len(config.pages), 2)
    page_config_0, page_config_1 = config.pages
    self.assertEqual(page_config_0.url, "http://a.com")
    self.assertEqual(page_config_1.url, "http://b.com")
    self.assertEqual(page_config_0.duration.total_seconds(), 1)
    self.assertEqual(page_config_1.duration.total_seconds(), 123)

  def test_parse_sequence(self):
    config_list = PagesConfig.parse(["http://a.com,1s", "http://b.com,123s"])
    config_str = PagesConfig.parse("http://a.com,1s,http://b.com,123s")
    self.assertEqual(config_list, config_str)

    config_list = PagesConfig.parse(["http://a.com", "http://b.com"])
    config_str = PagesConfig.parse("http://a.com,http://b.com")
    self.assertEqual(config_list, config_str)

  def test_parse_empty_actions(self):
    config_data = {"pages": {"Google Story": []}}
    with self.assertRaises(argparse.ArgumentTypeError) as cm:
      PagesConfig.parse(config_data)
    self.assertIn("action", str(cm.exception).lower())
    config_data = {"pages": {"Google Story": {}}}
    with self.assertRaises(argparse.ArgumentTypeError) as cm:
      PagesConfig.parse(config_data)
    self.assertIn("action", str(cm.exception).lower())

  def test_parse_empty_missing_get_action(self):
    config_data = {
        "pages": {
            "Google Story": [{
                "action": "wait",
                "duration": 5
            }]
        }
    }
    with self.assertRaises(argparse.ArgumentTypeError) as cm:
      PagesConfig.parse(config_data)
    self.assertIn("get", str(cm.exception).lower())

  def test_example(self):
    config_data = {
        "pages": {
            "Google Story": [
                {
                    "action": "get",
                    "url": "https://www.google.com"
                },
                {
                    "action": "wait",
                    "duration": 5
                },
                {
                    "action": "scroll",
                    "direction": "down",
                    "duration": 3
                },
            ],
        }
    }
    config = PagesConfig.parse(config_data)
    self.assert_single_google_story(config.pages)
    # Loading the same config from a file should result in the same actions.
    file = pathlib.Path("page.config.hjson")
    assert not file.exists()
    with file.open("w", encoding="utf-8") as f:
      hjson.dump(config_data, f)
    pages = PagesConfig.parse(str(file)).pages
    self.assert_single_google_story(pages)

  def assert_single_google_story(self, pages: Sequence[PageConfig]):
    self.assertTrue(len(pages), 1)
    page = pages[0]
    self.assertEqual(page.label, "Google Story")
    self.assertListEqual([str(action.TYPE) for action in page.actions],
                         ["get", "wait", "scroll"])

  def test_no_scenarios(self):
    with self.assertRaises(argparse.ArgumentTypeError):
      PagesConfig.load_dict({})
    with self.assertRaises(argparse.ArgumentTypeError):
      PagesConfig.load_dict({"pages": {}})

  def test_scenario_invalid_actions(self):
    invalid_actions = [None, "", [], {}, "invalid string", 12]
    for invalid_action in invalid_actions:
      config_dict = {"pages": {"name": invalid_action}}
      with self.subTest(invalid_action=invalid_action):
        with self.assertRaises(argparse.ArgumentTypeError):
          PagesConfig.load_dict(config_dict)

  def test_missing_action(self):
    with self.assertRaises(argparse.ArgumentTypeError) as cm:
      PagesConfig.load_dict(
          {"pages": {
              "TEST": [{
                  "action___": "wait",
                  "duration": 5.0
              }]
          }})
    self.assertIn("Missing 'action'", str(cm.exception))

  def test_invalid_action(self):
    invalid_actions = [None, "", [], {}, "unknown action name", 12]
    for invalid_action in invalid_actions:
      config_dict = {
          "pages": {
              "TEST": [{
                  "action": invalid_action,
                  "duration": 5.0
              }]
          }
      }
      with self.subTest(invalid_action=invalid_action):
        with self.assertRaises(argparse.ArgumentTypeError):
          PagesConfig.load_dict(config_dict)

  def test_missing_get_action_scenario(self):
    with self.assertRaises(argparse.ArgumentTypeError):
      PagesConfig.load_dict(
          {"pages": {
              "TEST": [{
                  "action": "wait",
                  "duration": 5.0
              }]
          }})

  def test_get_action_durations(self):
    durations = [
        ("5", 5),
        ("5.5", 5.5),
        (6, 6),
        (6.1, 6.1),
        ("5.5", 5.5),
        ("170ms", 0.17),
        ("170milliseconds", 0.17),
        ("170.4ms", 0.1704),
        ("170.4 millis", 0.1704),
        ("8s", 8),
        ("8.1s", 8.1),
        ("8.1seconds", 8.1),
        ("1 second", 1),
        ("1.1 seconds", 1.1),
        ("9m", 9 * 60),
        ("9.5m", 9.5 * 60),
        ("9.5 minutes", 9.5 * 60),
        ("9.5 mins", 9.5 * 60),
        ("1 minute", 60),
        ("1 min", 60),
        ("1h", 3600),
        ("1 h", 3600),
        ("1 hour", 3600),
        ("0.5h", 1800),
        ("0.5 hours", 1800),
    ]
    for input_value, duration in durations:
      with self.subTest(duration=duration):
        page_config = PagesConfig.load_dict({
            "pages": {
                "TEST": [
                    {
                        "action": "get",
                        "url": "google.com"
                    },
                    {
                        "action": "wait",
                        "duration": input_value
                    },
                ]
            }
        })
        self.assertEqual(len(page_config.pages), 1)
        page = page_config.pages[0]
        self.assertEqual(len(page.actions), 2)
        self.assertEqual(page.actions[1].duration,
                         dt.timedelta(seconds=duration))

  def test_action_invalid_duration(self):
    invalid_durations = [
        "1.1.1", None, "", -1, "-1", "-1ms", "1msss", "1ss", "2hh", "asdfasd",
        "---", "1.1.1", "1_123ms", "1'200h", (), [], {}, "-1h"
    ]
    for invalid_duration in invalid_durations:
      with self.subTest(duration=invalid_duration), self.assertRaises(
          (AssertionError, ValueError, argparse.ArgumentTypeError)):
        PagesConfig.load_dict({
            "pages": {
                "TEST": [
                    {
                        "action": "get",
                        "url": "google.com"
                    },
                    {
                        "action": "wait",
                        "duration": invalid_duration
                    },
                ]
            }
        })


DEVTOOLS_RECORDER_EXAMPLE = {
    "title":
        "cnn load",
    "steps": [
        {
            "type": "setViewport",
            "width": 1628,
            "height": 397,
            "deviceScaleFactor": 1,
            "isMobile": False,
            "hasTouch": False,
            "isLandscape": False
        },
        {
            "type":
                "navigate",
            "url":
                "https://edition.cnn.com/",
            "assertedEvents": [{
                "type": "navigation",
                "url": "https://edition.cnn.com/",
                "title": ""
            }]
        },
        {
            "type": "click",
            "target": "main",
            "selectors": [
                ["aria/Opinion"],
                [
                    "#pageHeader > div > div > div.header__container div:nth-of-type(5) > a"
                ],
                [
                    "xpath///*[@id=\"pageHeader\"]/div/div/div[1]/div[1]/nav/div/div[5]/a"
                ],
                [
                    "pierce/#pageHeader > div > div > div.header__container div:nth-of-type(5) > a"
                ]
            ],
            "offsetY": 17,
            "offsetX": 22.515625
        },
    ]
}


class _ConfigBaseTestCase(fake_filesystem_unittest.TestCase):

  def setUp(self) -> None:
    super().setUp()
    self.setUpPyfakefs(modules_to_reload=[crossbench.path])


class DevToolsRecorderPageConfigTestCase(_ConfigBaseTestCase):

  def test_invalid(self):
    with self.assertRaises(argparse.ArgumentTypeError) as cm:
      DevToolsRecorderPagesConfig.parse({})
    self.assertIn("empty", str(cm.exception))

  def test_missing_title(self):
    with self.assertRaises(argparse.ArgumentTypeError) as cm:
      DevToolsRecorderPagesConfig.parse({"foo": {}})
    self.assertIn("title", str(cm.exception))

  def test_basic_config(self):
    config = DevToolsRecorderPagesConfig.parse(DEVTOOLS_RECORDER_EXAMPLE)
    self.assertEqual(len(config.pages), 1)
    page = config.pages[0]
    self.assertEqual(page.label, "cnn load")
    self.assertEqual(page.url, "https://edition.cnn.com/")
    self.assertGreater(len(page.actions), 1)

  def test_basic_config_from_file(self):
    config_path = pathlib.Path("devtools.config.json")
    with config_path.open("w") as f:
      json.dump(DEVTOOLS_RECORDER_EXAMPLE, f)
    config_file = DevToolsRecorderPagesConfig.parse(config_path)
    config_dict = DevToolsRecorderPagesConfig.parse(DEVTOOLS_RECORDER_EXAMPLE)
    self.assertEqual(config_file, config_dict)


class ListPageConfigTestCase(_ConfigBaseTestCase):

  def test_invalid(self):
    with self.assertRaises(argparse.ArgumentTypeError) as cm:
      ListPagesConfig.parse({})
    self.assertIn("empty", str(cm.exception))
    with self.assertRaises(argparse.ArgumentTypeError) as cm:
      ListPagesConfig.parse({"foo": {}})
    self.assertIn("pages", str(cm.exception))

    with self.assertRaises(argparse.ArgumentTypeError) as cm:
      ListPagesConfig.load_dict({"pages": None})
    self.assertIn("None", str(cm.exception))

    with self.assertRaises(argparse.ArgumentTypeError) as cm:
      ListPagesConfig.load_dict({"pages": []})
    self.assertIn("empty", str(cm.exception))

  def test_direct_string_single(self):
    with self.assertRaises(argparse.ArgumentTypeError) as cm:
      ListPagesConfig.parse("http://foo.bar.com,23s")
    self.assertIn("http://foo.bar.com,23s", str(cm.exception))

  def test_direct_string_single_dict(self):
    config_dict = ListPagesConfig.parse({"pages": "http://foo.bar.com,23s"})
    config_str = PagesConfig(
        pages=(PageConfig.parse("http://foo.bar.com,23s"),))
    self.assertEqual(config_dict, config_str)

  @unittest.skip("Combined pages per line not supported yet")
  def test_direct_string_multiple(self):
    config = ListPagesConfig.load_dict(
        {"pages": "http://a.com,12s,http://b.com,13s"})
    self.assertEqual(len(config.pages), 2)
    story_1, story_2 = config.pages
    self.assertEqual(story_1.url, "http://a.com")
    self.assertEqual(story_2.url, "http://b.com")
    self.assertEqual(story_1.duration.total_seconds(), 12)
    self.assertEqual(story_2.duration.total_seconds(), 13)

  def test_list(self):
    page_configs = ["http://a.com,12s", "http://b.com,13s"]
    config_str = PagesConfig.parse("http://a.com,12s,http://b.com,13s")
    config_dict_list = ListPagesConfig.parse({"pages": page_configs})
    config_list = ListPagesConfig.parse(page_configs)
    self.assertEqual(config_str, config_dict_list)
    self.assertEqual(config_str, config_list)

  def test_load_file(self):
    page_configs = ["http://a.com,12s", "http://b.com,13s"]
    config_file = pathlib.Path("page_list.txt")
    with config_file.open("w") as f:
      f.write("\n".join(page_configs))
    config_file = ListPagesConfig.parse(config_file)
    config_list = ListPagesConfig.parse(page_configs)
    self.assertEqual(config_file, config_list)

  def test_load_file_empty_liens(self):
    page_configs = ["http://a.com,12s", "http://b.com,13s"]
    config_file = pathlib.Path("page_list.txt")
    with config_file.open("w") as f:
      f.write("\n")
      f.write(page_configs[0])
      f.write("\n\n")
      f.write(page_configs[1])
      f.write("\n\n")
    config_file = ListPagesConfig.parse(config_file)
    config_list = ListPagesConfig.parse(page_configs)
    self.assertEqual(config_file, config_list)


class LoadingBenchmarkCliTestCase(BaseCliTestCase):

  def test_invalid_duplicate_urls_stories(self):
    with self.assertRaises(argparse.ArgumentTypeError) as cm:
      with self.patch_get_browser():
        url = "http://test.com"
        self.run_cli("loading", "run", f"--urls={url}", f"--stories={url}",
                     "--env-validation=skip", "--throw")
    self.assertIn("--urls", str(cm.exception))
    self.assertIn("--stories", str(cm.exception))

  def test_invalid_duplicate_urls_config(self):
    with self.assertRaises(argparse.ArgumentError) as cm:
      with self.patch_get_browser():
        self.run_cli("loading", "run", "--urls=https://test.com",
                     "--page-config=config.hjson", "--env-validation=skip",
                     "--throw")
    self.assertIn("--urls", str(cm.exception))
    self.assertIn("--page-config", str(cm.exception))

  def test_invalid_duplicate_stories_config(self):
    with self.assertRaises(argparse.ArgumentTypeError) as cm:
      with self.patch_get_browser():
        self.run_cli("loading", "run", "--stories=https://test.com",
                     "--page-config=config.hjson", "--env-validation=skip",
                     "--throw")
    self.assertIn("--stories", str(cm.exception))
    self.assertIn("page config", str(cm.exception).lower())

  def test_page_list_file(self):
    config = pathlib.Path("test/pages.txt")
    self.fs.create_file(config)
    url_1 = "http://one.test.com"
    url_2 = "http://two.test.com"
    with config.open("w") as f:
      f.write("\n".join((url_1, url_2)))
    with self.patch_get_browser():
      self.run_cli("loading", "run", f"--urls-file={config}",
                   "--env-validation=skip", "--throw")
      for browser in self.browsers:
        self.assertListEqual([url_1, url_2],
                             browser.url_list[self.SPLASH_URLS_LEN:])

  def test_page_list_file_separate(self):
    config = pathlib.Path("test/pages.txt")
    self.fs.create_file(config)
    url_1 = "http://one.test.com"
    url_2 = "http://two.test.com"
    with config.open("w") as f:
      f.write("\n".join((url_1, url_2)))
    with self.patch_get_browser():
      self.run_cli("loading", "run", f"--urls-file={config}",
                   "--env-validation=skip", "--separate", "--throw")
      for browser in self.browsers:
        self.assertEqual(len(browser.url_list), (self.SPLASH_URLS_LEN + 1) * 2)
        self.assertEqual(url_1, browser.url_list[self.SPLASH_URLS_LEN])
        self.assertEqual(url_2, browser.url_list[self.SPLASH_URLS_LEN * 2 + 1])

  def test_urls_single(self):
    with self.patch_get_browser():
      url = "http://test.com"
      self.run_cli("loading", "run", f"--urls={url}", "--env-validation=skip",
                   "--throw")
      for browser in self.browsers:
        self.assertListEqual([url], browser.url_list[self.SPLASH_URLS_LEN:])

  def test_urls_multiple(self):
    with self.patch_get_browser():
      url_1 = "http://one.test.com"
      url_2 = "http://two.test.com"
      self.run_cli("loading", "run", f"--urls={url_1},{url_2}",
                   "--env-validation=skip", "--throw")
      for browser in self.browsers:
        self.assertListEqual([url_1, url_2],
                             browser.url_list[self.SPLASH_URLS_LEN:])

  def test_urls_multiple_separate(self):
    with self.patch_get_browser():
      url_1 = "http://one.test.com"
      url_2 = "http://two.test.com"
      self.run_cli("loading", "run", f"--urls={url_1},{url_2}",
                   "--env-validation=skip", "--separate", "--throw")
      for browser in self.browsers:
        self.assertEqual(len(browser.url_list), (self.SPLASH_URLS_LEN + 1) * 2)
        self.assertEqual(url_1, browser.url_list[self.SPLASH_URLS_LEN])
        self.assertEqual(url_2, browser.url_list[self.SPLASH_URLS_LEN * 2 + 1])

  def test_repeat_playback(self):
    with self.patch_get_browser():
      url_1 = "http://one.test.com"
      url_2 = "http://two.test.com"
      self.run_cli("loading", "run", f"--urls={url_1},{url_2}", "--playback=2x",
                   "--env-validation=skip", "--throw")
      for browser in self.browsers:
        self.assertListEqual([url_1, url_2, url_1, url_2],
                             browser.url_list[self.SPLASH_URLS_LEN:])

  def test_repeat_playback_separate(self):
    with self.patch_get_browser():
      url_1 = "http://one.test.com"
      url_2 = "http://two.test.com"
      self.run_cli("loading", "run", f"--urls={url_1},{url_2}", "--playback=2x",
                   "--separate", "--env-validation=skip", "--throw")
      for browser in self.browsers:
        self.assertEqual(len(browser.url_list), (self.SPLASH_URLS_LEN + 2) * 2)
        self.assertListEqual(
            [url_1, url_1],
            browser.url_list[self.SPLASH_URLS_LEN:self.SPLASH_URLS_LEN + 2])
        self.assertListEqual([url_2, url_2],
                             browser.url_list[self.SPLASH_URLS_LEN * 2 + 2:])

  def test_actions_config(self):
    config_file = pathlib.Path("test/page_config.json")
    self.fs.create_file(config_file)
    url_1 = "http://one.test.com"
    url_2 = "http://two.test.com"
    config = {
        "pages": {
            "test_one": [{
                "action": "get",
                "url": url_1
            }, {
                "action": "get",
                "url": url_2
            }]
        }
    }
    with config_file.open("w") as f:
      json.dump(config, f)
    with self.patch_get_browser():
      self.run_cli("loading", "run", f"--page-config={config_file}",
                   "--env-validation=skip", "--throw")
      for browser in self.browsers:
        self.assertListEqual([url_1, url_2],
                             browser.url_list[self.SPLASH_URLS_LEN:])

if __name__ == "__main__":
  test_helper.run_pytest(__file__)
