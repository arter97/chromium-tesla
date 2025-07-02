# Copyright 2024 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from __future__ import annotations

import unittest

from crossbench.browsers.attributes import BrowserAttributes


class BrowserAttributesTestCase(unittest.TestCase):

  def test_is_chromium_based(self):
    custom_chrome = (
        BrowserAttributes.CHROMIUM | BrowserAttributes.CHROMIUM_BASED)
    self.assertTrue(custom_chrome)
    self.assertFalse(custom_chrome & BrowserAttributes.SAFARI)
    self.assertTrue(custom_chrome & BrowserAttributes.CHROMIUM_BASED)
    self.assertIs(custom_chrome & BrowserAttributes.CHROMIUM_BASED,
                  BrowserAttributes.CHROMIUM_BASED)
    self.assertTrue(custom_chrome.is_chromium_based)

  def test_is_chrome(self):
    for value in BrowserAttributes:
      self.assertEqual(value.is_chrome, value is BrowserAttributes.CHROME)
    custom = BrowserAttributes.CHROME | BrowserAttributes.WEBDRIVER
    self.assertTrue(custom.is_chrome)
    self.assertTrue(BrowserAttributes.CHROME.is_chrome)
    self.assertFalse(BrowserAttributes.SAFARI.is_chrome)

  def test_is_safari(self):
    for value in BrowserAttributes:
      self.assertEqual(value.is_safari, value is BrowserAttributes.SAFARI)
    custom = BrowserAttributes.SAFARI | BrowserAttributes.WEBDRIVER
    self.assertTrue(custom.is_safari)
    self.assertTrue(BrowserAttributes.SAFARI.is_safari)
    self.assertFalse(BrowserAttributes.CHROME.is_safari)

  def test_is_edge(self):
    for value in BrowserAttributes:
      self.assertEqual(value.is_edge, value is BrowserAttributes.EDGE)
    custom = BrowserAttributes.EDGE | BrowserAttributes.WEBDRIVER
    self.assertTrue(custom.is_edge)
    self.assertTrue(BrowserAttributes.EDGE.is_edge)
    self.assertFalse(BrowserAttributes.CHROME.is_edge)

  def test_is_firefox(self):
    for value in BrowserAttributes:
      self.assertEqual(value.is_firefox, value is BrowserAttributes.FIREFOX)
    custom = BrowserAttributes.FIREFOX | BrowserAttributes.WEBDRIVER
    self.assertTrue(custom.is_firefox)
    self.assertTrue(BrowserAttributes.FIREFOX.is_firefox)
    self.assertFalse(BrowserAttributes.CHROME.is_firefox)
