// Copyright 2014 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if defined(UNSAFE_BUFFERS_BUILD)
// TODO(crbug.com/pdfium/2154): resolve buffer safety issues.
#pragma allow_unsafe_buffers
#endif

#include "xfa/fxfa/parser/xfa_utils.h"

#include <iterator>

#include "testing/gtest/include/gtest/gtest.h"

TEST(XfaUtilsImpTest, XFA_MapRotation) {
  struct TestCase {
    int input;
    int expected_output;
  } TestCases[] = {{-1000000, 80}, {-361, 359}, {-360, 0},  {-359, 1},
                   {-91, 269},     {-90, 270},  {-89, 271}, {-1, 359},
                   {0, 0},         {1, 1},      {89, 89},   {90, 90},
                   {91, 91},       {359, 359},  {360, 0},   {361, 1},
                   {100000, 280}};

  for (size_t i = 0; i < std::size(TestCases); ++i) {
    EXPECT_EQ(TestCases[i].expected_output,
              XFA_MapRotation(TestCases[i].input));
  }
}
