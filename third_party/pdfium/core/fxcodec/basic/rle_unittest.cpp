// Copyright 2016 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if defined(UNSAFE_BUFFERS_BUILD)
// TODO(crbug.com/pdfium/2154): resolve buffer safety issues.
#pragma allow_unsafe_buffers
#endif

#include <stdint.h>

#include <limits>
#include <memory>

#include "core/fpdfapi/parser/fpdf_parser_decode.h"
#include "core/fxcodec/basic/basicmodule.h"
#include "core/fxcodec/data_and_bytes_consumed.h"
#include "core/fxcrt/data_vector.h"
#include "testing/gtest/include/gtest/gtest.h"

TEST(fxcodec, RLEEmptyInput) {
  EXPECT_TRUE(BasicModule::RunLengthEncode({}).empty());
}

// Check length 1 input works. Check terminating character is applied.
TEST(fxcodec, RLEShortInput) {
  const uint8_t src_buf[] = {1};
  DataVector<uint8_t> dest_buf = BasicModule::RunLengthEncode(src_buf);
  ASSERT_EQ(3u, dest_buf.size());
  EXPECT_EQ(0, dest_buf[0]);
  EXPECT_EQ(1, dest_buf[1]);
  EXPECT_EQ(128, dest_buf[2]);
}

// Check a few basic cases (2 matching runs in a row, matching run followed
// by a non-matching run, and non-matching run followed by a matching run).
TEST(fxcodec, RLENormalInputs) {
  {
    // Case 1: Match, match
    const uint8_t src_buf_1[] = {2, 2, 2, 2, 4, 4, 4, 4, 4, 4};
    DataVector<uint8_t> dest_buf = BasicModule::RunLengthEncode(src_buf_1);
    DataAndBytesConsumed result = RunLengthDecode(dest_buf);
    ASSERT_EQ(sizeof(src_buf_1), result.data.size());
    auto decoded_buf_span = pdfium::make_span(result.data);
    for (uint32_t i = 0; i < decoded_buf_span.size(); i++) {
      EXPECT_EQ(src_buf_1[i], decoded_buf_span[i]) << " at " << i;
    }
  }

  {
    // Case 2: Match, non-match
    const uint8_t src_buf_2[] = {2, 2, 2, 2, 1, 2, 3, 4, 5, 6};
    DataVector<uint8_t> dest_buf = BasicModule::RunLengthEncode(src_buf_2);
    DataAndBytesConsumed result = RunLengthDecode(dest_buf);
    ASSERT_EQ(sizeof(src_buf_2), result.data.size());
    auto decoded_buf_span = pdfium::make_span(result.data);
    for (uint32_t i = 0; i < decoded_buf_span.size(); i++) {
      EXPECT_EQ(src_buf_2[i], decoded_buf_span[i]) << " at " << i;
    }
  }

  {
    // Case 3: Non-match, match
    const uint8_t src_buf_3[] = {1, 2, 3, 4, 5, 3, 3, 3, 3, 3};
    DataVector<uint8_t> dest_buf = BasicModule::RunLengthEncode(src_buf_3);
    DataAndBytesConsumed result = RunLengthDecode(dest_buf);
    ASSERT_EQ(sizeof(src_buf_3), result.data.size());
    auto decoded_buf_span = pdfium::make_span(result.data);
    for (uint32_t i = 0; i < decoded_buf_span.size(); i++) {
      EXPECT_EQ(src_buf_3[i], decoded_buf_span[i]) << " at " << i;
    }
  }
}

// Check that runs longer than 128 are broken up properly, both matched and
// non-matched.
TEST(fxcodec, RLEFullLengthInputs) {
  {
    // Case 1: Match, match
    const uint8_t src_buf_1[260] = {1};
    DataVector<uint8_t> dest_buf = BasicModule::RunLengthEncode(src_buf_1);
    DataAndBytesConsumed result = RunLengthDecode(dest_buf);
    ASSERT_EQ(sizeof(src_buf_1), result.data.size());
    auto decoded_buf_span = pdfium::make_span(result.data);
    for (uint32_t i = 0; i < decoded_buf_span.size(); i++) {
      EXPECT_EQ(src_buf_1[i], decoded_buf_span[i]) << " at " << i;
    }
  }

  {
    // Case 2: Match, non-match
    uint8_t src_buf_2[260] = {2};
    for (uint16_t i = 128; i < 260; i++)
      src_buf_2[i] = static_cast<uint8_t>(i - 125);
    DataVector<uint8_t> dest_buf = BasicModule::RunLengthEncode(src_buf_2);
    DataAndBytesConsumed result = RunLengthDecode(dest_buf);
    ASSERT_EQ(sizeof(src_buf_2), result.data.size());
    auto decoded_buf_span = pdfium::make_span(result.data);
    for (uint32_t i = 0; i < decoded_buf_span.size(); i++) {
      EXPECT_EQ(src_buf_2[i], decoded_buf_span[i]) << " at " << i;
    }
  }

  {
    // Case 3: Non-match, match
    uint8_t src_buf_3[260] = {3};
    for (uint8_t i = 0; i < 128; i++)
      src_buf_3[i] = i;
    DataVector<uint8_t> dest_buf = BasicModule::RunLengthEncode(src_buf_3);
    DataAndBytesConsumed result = RunLengthDecode(dest_buf);
    ASSERT_EQ(sizeof(src_buf_3), result.data.size());
    auto decoded_buf_span = pdfium::make_span(result.data);
    for (uint32_t i = 0; i < decoded_buf_span.size(); i++) {
      EXPECT_EQ(src_buf_3[i], decoded_buf_span[i]) << " at " << i;
    }
  }

  {
    // Case 4: Non-match, non-match
    uint8_t src_buf_4[260];
    for (uint16_t i = 0; i < 260; i++)
      src_buf_4[i] = static_cast<uint8_t>(i);
    DataVector<uint8_t> dest_buf = BasicModule::RunLengthEncode(src_buf_4);
    DataAndBytesConsumed result = RunLengthDecode(dest_buf);
    ASSERT_EQ(sizeof(src_buf_4), result.data.size());
    auto decoded_buf_span = pdfium::make_span(result.data);
    for (uint32_t i = 0; i < decoded_buf_span.size(); i++) {
      EXPECT_EQ(src_buf_4[i], decoded_buf_span[i]) << " at " << i;
    }
  }
}
