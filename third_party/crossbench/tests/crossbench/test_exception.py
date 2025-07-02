# Copyright 2022 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import unittest
from contextlib import contextmanager
from unittest import mock

from crossbench.exception import (ArgumentTypeMultiException, Entry,
                                  ExceptionAnnotator, MultiException, annotate,
                                  annotate_argparsing)
from tests import test_helper


class CustomException(Exception):
  pass


class CustomException2(Exception):
  pass


class CustomValueError(ValueError):
  pass

class ExceptionHandlerTestCase(unittest.TestCase):

  def test_invalid_get_item(self):
    annotator = ExceptionAnnotator()
    with self.assertRaises(TypeError):
      _ = annotator["invalid key"]
    with self.assertRaises(IndexError):
      _ = annotator[1]

  def test_getitem(self):
    annotator = ExceptionAnnotator()
    with annotator.capture("exception"):
      raise Exception("AAA")
    with annotator.capture("exception"):
      raise Exception("BBB")
    self.assertEqual(len(annotator), 2)
    entry_0 = annotator[0]
    entry_1 = annotator[1]
    self.assertIsNot(entry_0, entry_1)
    self.assertIs(annotator[-1], entry_1)
    with self.assertRaises(IndexError):
      _ = annotator[2]

  def test_annotate(self):
    with self.assertRaises(MultiException) as cm:
      with annotate("BBB"):
        with annotate("AAA"):
          raise ValueError("an exception")
    exception: MultiException = cm.exception
    annotator: ExceptionAnnotator = exception.annotator
    self.assertTrue(len(annotator), 1)
    entry: Entry = annotator[0]
    self.assertTupleEqual(entry.info_stack, ("BBB", "AAA"))
    self.assertIsInstance(entry.exception, ValueError)

  def test_annotate_argparse(self):
    with self.assertRaises(ArgumentTypeMultiException) as cm:
      with annotate_argparsing("BBB"):
        with annotate("AAA"):
          with annotate("000"):
            raise ValueError("an exception")
    exception: MultiException = cm.exception
    self.assertIsInstance(exception, argparse.ArgumentTypeError)
    annotator: ExceptionAnnotator = exception.annotator
    self.assertTrue(len(annotator), 1)
    entry: Entry = annotator[0]
    self.assertTupleEqual(entry.info_stack, ("BBB", "AAA", "000"))
    self.assertIsInstance(entry.exception, ValueError)

  def test_annotate_argparse_nested(self):
    with self.assertRaises(ArgumentTypeMultiException) as cm:
      with annotate_argparsing("BBB"):
        with annotate_argparsing("AAA"):
          with annotate_argparsing("000"):
            raise CustomValueError("an exception")
    exception: MultiException = cm.exception
    self.assertIsInstance(exception, argparse.ArgumentTypeError)
    annotator: ExceptionAnnotator = exception.annotator
    self.assertTrue(len(annotator), 1)
    entry: Entry = annotator[0]
    self.assertListEqual(
        annotator.matching(CustomValueError), [
            entry.exception,
        ])
    self.assertTupleEqual(entry.info_stack, ("BBB", "AAA", "000"))
    self.assertIsInstance(entry.exception, ValueError)

  def test_annotate_argparse_pass_through(self):
    with self.assertRaises(ArgumentTypeMultiException) as cm:
      with annotate_argparsing("BBB"):
        with annotate_argparsing("AAA"):
          with annotate_argparsing("000"):
            raise argparse.ArgumentTypeError("some arg type error")
    self.assertEqual(len(cm.exception), 1)
    exception: argparse.ArgumentTypeError = cm.exception.matching(
        argparse.ArgumentTypeError)[0]
    self.assertIsInstance(exception, argparse.ArgumentTypeError)
    self.assertEqual(str(exception), "some arg type error")

  def test_annotate_collecting(self):
    annotator = ExceptionAnnotator()
    with self.assertRaises(MultiException) as cm:
      with annotator.annotate("AAA"):
        with annotator.annotate("000"):
          raise ValueError("an exception")
    exception: MultiException = cm.exception
    self.assertIsInstance(exception, MultiException)
    self.assertFalse(annotator.is_success)
    self.assertTrue(len(annotator), 1)
    entry: Entry = annotator[0]
    self.assertTupleEqual(entry.info_stack, ("AAA", "000"))
    self.assertIsInstance(entry.exception, ValueError)

  def test_empty(self):
    annotator = ExceptionAnnotator()
    self.assertTrue(annotator.is_success)
    self.assertEqual(len(annotator), 0)
    self.assertListEqual(annotator.to_json(), [])
    with mock.patch("logging.error") as logging_mock:
      annotator.log()
    # No exceptions => no error output
    logging_mock.assert_not_called()

  def test_handle_exception(self):
    annotator = ExceptionAnnotator()
    exception = ValueError("custom message")
    try:
      raise exception
    except ValueError as e:
      annotator.append(e)
    self.assertFalse(annotator.is_success)
    serialized = annotator.to_json()
    self.assertEqual(len(serialized), 1)
    self.assertEqual(serialized[0]["title"], str(exception))
    with mock.patch("logging.debug") as logging_mock:
      annotator.log()
    logging_mock.assert_has_calls([mock.call(exception)])

  def test_handle_rethrow(self):
    annotator = ExceptionAnnotator(throw=True)
    exception = ValueError("custom message")
    with self.assertRaises(ValueError) as cm:
      try:
        raise exception
      except ValueError as e:
        annotator.append(e)
    self.assertEqual(cm.exception, exception)
    self.assertFalse(annotator.is_success)
    serialized = annotator.to_json()
    self.assertEqual(len(serialized), 1)
    self.assertEqual(serialized[0]["title"], str(exception))

  def test_info_stack(self):
    annotator = ExceptionAnnotator(throw=True)
    exception = ValueError("custom message")
    with self.assertRaises(ValueError) as cm, annotator.info(
        "info 1", "info 2"):
      self.assertTupleEqual(annotator.info_stack, ("info 1", "info 2"))
      try:
        raise exception
      except ValueError as e:
        annotator.append(e)
    self.assertEqual(cm.exception, exception)
    self.assertFalse(annotator.is_success)
    self.assertEqual(len(annotator), 1)
    entry = annotator[0]
    self.assertTupleEqual(entry.info_stack, ("info 1", "info 2"))
    serialized = annotator.to_json()
    self.assertEqual(len(serialized), 1)
    self.assertEqual(serialized[0]["title"], str(exception))
    self.assertEqual(serialized[0]["info_stack"], ("info 1", "info 2"))

  def test_info_stack_logging(self):
    annotator = ExceptionAnnotator()
    try:
      with annotator.info("info 1", "info 2"):
        raise ValueError("custom message")
    except ValueError as e:
      annotator.append(e)
    with self.assertLogs(level="ERROR") as cm:
      annotator.log()
    output = "\n".join(cm.output)
    self.assertIn("info 1", output)
    self.assertIn("info 2", output)
    self.assertIn("custom message", output)

  def test_handle_keyboard_interrupt(self):
    annotator = ExceptionAnnotator()
    keyboard_interrupt = KeyboardInterrupt()
    with mock.patch("sys.exit", side_effect=ValueError) as exit_mock:
      with self.assertRaises(ValueError) as cm:
        try:
          raise keyboard_interrupt
        except KeyboardInterrupt as e:
          annotator.append(e)
      self.assertNotEqual(cm.exception, keyboard_interrupt)
    exit_mock.assert_called_once_with(0)

  def test_extend(self):
    annotator_1 = ExceptionAnnotator()
    try:
      raise ValueError("error_1")
    except ValueError as e:
      annotator_1.append(e)
    annotator_2 = ExceptionAnnotator()
    try:
      raise ValueError("error_2")
    except ValueError as e:
      annotator_2.append(e)
    annotator_3 = ExceptionAnnotator()
    annotator_4 = ExceptionAnnotator()
    self.assertFalse(annotator_1.is_success)
    self.assertFalse(annotator_2.is_success)
    self.assertTrue(annotator_3.is_success)
    self.assertTrue(annotator_4.is_success)

    self.assertEqual(len(annotator_1), 1)
    self.assertEqual(len(annotator_2), 1)
    annotator_2.extend(annotator_1)
    self.assertEqual(len(annotator_2), 2)
    self.assertFalse(annotator_1.is_success)
    self.assertFalse(annotator_2.is_success)

    self.assertEqual(len(annotator_1), 1)
    self.assertEqual(len(annotator_3), 0)
    self.assertEqual(len(annotator_4), 0)
    annotator_3.extend(annotator_1)
    annotator_3.extend(annotator_4)
    self.assertEqual(len(annotator_3), 1)
    self.assertFalse(annotator_3.is_success)
    self.assertTrue(annotator_4.is_success)

  def test_extend_nested(self):
    annotator_1 = ExceptionAnnotator()
    annotator_2 = ExceptionAnnotator()
    exception_1 = ValueError("error_1")
    exception_2 = ValueError("error_2")
    with annotator_1.capture("info 1", "info 2", exceptions=(ValueError,)):
      raise exception_1
    self.assertEqual(len(annotator_1), 1)
    self.assertEqual(len(annotator_2), 0)
    with annotator_1.info("info 1", "info 2"):
      with annotator_2.capture("info 3", "info 4", exceptions=(ValueError,)):
        raise exception_2
      annotator_1.extend(annotator_2, is_nested=True)
    self.assertEqual(len(annotator_1), 2)
    self.assertEqual(len(annotator_2), 1)
    self.assertTupleEqual(annotator_1[0].info_stack, ("info 1", "info 2"))
    self.assertTupleEqual(annotator_1[1].info_stack,
                          ("info 1", "info 2", "info 3", "info 4"))
    self.assertTupleEqual(annotator_2[0].info_stack, ("info 3", "info 4"))

  def test_contextmanager(self):
    annotator = ExceptionAnnotator()

    @contextmanager
    def context(value):
      with annotator.capture("entry"):
        yield value

    with context("custom value") as context_value:
      raise Exception("custom exception")

    self.assertEqual(context_value, "custom value")
    self.assertFalse(annotator.is_success)
    self.assertEqual(len(annotator), 1)
    self.assertIn("custom exception", str(annotator[0]))

  def test_contextmanager_raise_before_yield_capture(self):

    @contextmanager
    def context_simple(value):
      raise RuntimeError("exception before yield")

    did_run = False
    with self.assertRaises(RuntimeError) as cm:
      with context_simple("custom value 1"):
        did_run = True
        raise Exception("custom exception 1")
    self.assertFalse(did_run)
    self.assertIn("exception before yield", str(cm.exception))

    annotator = ExceptionAnnotator()

    @contextmanager
    def context_capture(value):
      with annotator.capture("entry"):
        raise RuntimeError("exception before yield")
      yield value

    did_run = False
    with self.assertRaises(CustomException) as cm:
      with context_capture("custom value 2"):
        did_run = True
        raise CustomException("custom exception 2")
    self.assertTrue(did_run)
    self.assertIn("custom exception 2", str(cm.exception))
    self.assertFalse(annotator.is_success)
    self.assertEqual(len(annotator), 1)
    self.assertIsInstance(annotator[0].exception, RuntimeError)

  def test_contextmanager_raise_before_yield_annotate(self):
    annotator = ExceptionAnnotator()

    @contextmanager
    def context_annotate(value):
      with annotator.annotate("entry"):
        raise RuntimeError("exception before yield")
      yield value

    did_run = False
    with self.assertRaises(MultiException) as cm:
      with context_annotate("custom value 3"):
        did_run = True
        raise CustomException("custom exception 3")
    self.assertFalse(did_run)
    self.assertIn("exception before yield", str(cm.exception))
    self.assertFalse(annotator.is_success)
    self.assertEqual(len(annotator), 1)
    self.assertIsInstance(annotator[0].exception, RuntimeError)



if __name__ == "__main__":
  test_helper.run_pytest(__file__)
