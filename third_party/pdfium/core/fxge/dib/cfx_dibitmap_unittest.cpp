// Copyright 2018 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if defined(UNSAFE_BUFFERS_BUILD)
// TODO(crbug.com/pdfium/2154): resolve buffer safety issues.
#pragma allow_unsafe_buffers
#endif

#include "core/fxge/dib/cfx_dibitmap.h"

#include <stdint.h>

#include "core/fxcrt/fx_coordinates.h"
#include "core/fxcrt/span.h"
#include "core/fxge/dib/fx_dib.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {

using ::testing::ElementsAre;

}  // namespace

TEST(CFX_DIBitmap, Create) {
  auto pBitmap = pdfium::MakeRetain<CFX_DIBitmap>();
  EXPECT_FALSE(pBitmap->Create(400, 300, FXDIB_Format::kInvalid));

  pBitmap = pdfium::MakeRetain<CFX_DIBitmap>();
  EXPECT_TRUE(pBitmap->Create(400, 300, FXDIB_Format::k1bppRgb));
}

TEST(CFX_DIBitmap, CalculatePitchAndSizeGood) {
  // Simple case with no provided pitch.
  std::optional<CFX_DIBitmap::PitchAndSize> result =
      CFX_DIBitmap::CalculatePitchAndSize(100, 200, FXDIB_Format::kArgb, 0);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(400u, result.value().pitch);
  EXPECT_EQ(80000u, result.value().size);

  // Simple case with no provided pitch and different format.
  result =
      CFX_DIBitmap::CalculatePitchAndSize(100, 200, FXDIB_Format::k8bppRgb, 0);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(100u, result.value().pitch);
  EXPECT_EQ(20000u, result.value().size);

  // Simple case with provided pitch matching width * bpp.
  result =
      CFX_DIBitmap::CalculatePitchAndSize(100, 200, FXDIB_Format::kArgb, 400);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(400u, result.value().pitch);
  EXPECT_EQ(80000u, result.value().size);

  // Simple case with provided pitch, where pitch exceeds width * bpp.
  result =
      CFX_DIBitmap::CalculatePitchAndSize(100, 200, FXDIB_Format::kArgb, 455);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(455u, result.value().pitch);
  EXPECT_EQ(91000u, result.value().size);
}

TEST(CFX_DIBitmap, CalculatePitchAndSizeBad) {
  // Bad width / height.
  static const CFX_Size kBadDimensions[] = {
      {0, 0},   {-1, -1}, {-1, 0},   {0, -1},
      {0, 200}, {100, 0}, {-1, 200}, {100, -1},
  };
  for (const auto& dimension : kBadDimensions) {
    EXPECT_FALSE(CFX_DIBitmap::CalculatePitchAndSize(
        dimension.width, dimension.height, FXDIB_Format::kArgb, 0));
    EXPECT_FALSE(CFX_DIBitmap::CalculatePitchAndSize(
        dimension.width, dimension.height, FXDIB_Format::kArgb, 1));
  }

  // Bad format.
  EXPECT_FALSE(
      CFX_DIBitmap::CalculatePitchAndSize(100, 200, FXDIB_Format::kInvalid, 0));
  EXPECT_FALSE(CFX_DIBitmap::CalculatePitchAndSize(
      100, 200, FXDIB_Format::kInvalid, 800));

  // Width too wide for claimed pitch.
  EXPECT_FALSE(
      CFX_DIBitmap::CalculatePitchAndSize(101, 200, FXDIB_Format::kArgb, 400));

  // Overflow cases with calculated pitch.
  EXPECT_FALSE(CFX_DIBitmap::CalculatePitchAndSize(1073747000, 1,
                                                   FXDIB_Format::kArgb, 0));
  EXPECT_FALSE(CFX_DIBitmap::CalculatePitchAndSize(1048576, 1024,
                                                   FXDIB_Format::kArgb, 0));
  EXPECT_FALSE(CFX_DIBitmap::CalculatePitchAndSize(4194304, 1024,
                                                   FXDIB_Format::k8bppRgb, 0));

  // Overflow cases with provided pitch.
  EXPECT_FALSE(CFX_DIBitmap::CalculatePitchAndSize(
      2147484000u, 1, FXDIB_Format::kArgb, 2147484000u));
  EXPECT_FALSE(CFX_DIBitmap::CalculatePitchAndSize(
      1048576, 1024, FXDIB_Format::kArgb, 4194304));
  EXPECT_FALSE(CFX_DIBitmap::CalculatePitchAndSize(
      4194304, 1024, FXDIB_Format::k8bppRgb, 4194304));
}

TEST(CFX_DIBitmap, CalculatePitchAndSizeBoundary) {
  // Test boundary condition for pitch overflow.
  std::optional<CFX_DIBitmap::PitchAndSize> result =
      CFX_DIBitmap::CalculatePitchAndSize(536870908, 4, FXDIB_Format::k8bppRgb,
                                          0);
  ASSERT_TRUE(result);
  EXPECT_EQ(536870908u, result.value().pitch);
  EXPECT_EQ(2147483632u, result.value().size);
  EXPECT_FALSE(CFX_DIBitmap::CalculatePitchAndSize(536870909, 4,
                                                   FXDIB_Format::k8bppRgb, 0));

  // Test boundary condition for size overflow.
  result = CFX_DIBitmap::CalculatePitchAndSize(68174084, 63,
                                               FXDIB_Format::k8bppRgb, 0);
  ASSERT_TRUE(result);
  EXPECT_EQ(68174084u, result.value().pitch);
  EXPECT_EQ(4294967292u, result.value().size);
  EXPECT_FALSE(CFX_DIBitmap::CalculatePitchAndSize(68174085, 63,
                                                   FXDIB_Format::k8bppRgb, 0));
}

#if defined(PDF_USE_SKIA)
TEST(CFX_DIBitmap, UnPreMultiply_FromCleared) {
  auto bitmap = pdfium::MakeRetain<CFX_DIBitmap>();
  ASSERT_TRUE(bitmap->Create(1, 1, FXDIB_Format::kArgb));
  FXARGB_SetDIB(bitmap->GetWritableBuffer().data(), 0x7f'7f'7f'7f);

  bitmap->UnPreMultiply();

  EXPECT_THAT(bitmap->GetBuffer(), ElementsAre(0xff, 0xff, 0xff, 0x7f));
}

TEST(CFX_DIBitmap, UnPreMultiply_FromPreMultiplied) {
  auto bitmap = pdfium::MakeRetain<CFX_DIBitmap>();
  ASSERT_TRUE(bitmap->Create(1, 1, FXDIB_Format::kArgb));
  bitmap->ForcePreMultiply();
  FXARGB_SetDIB(bitmap->GetWritableBuffer().data(), 0x7f'7f'7f'7f);

  bitmap->UnPreMultiply();

  EXPECT_THAT(bitmap->GetBuffer(), ElementsAre(0xff, 0xff, 0xff, 0x7f));
}

TEST(CFX_DIBitmap, UnPreMultiply_FromUnPreMultiplied) {
  auto bitmap = pdfium::MakeRetain<CFX_DIBitmap>();
  ASSERT_TRUE(bitmap->Create(1, 1, FXDIB_Format::kArgb));
  bitmap->UnPreMultiply();
  FXARGB_SetDIB(bitmap->GetWritableBuffer().data(), 0x7f'ff'ff'ff);

  bitmap->UnPreMultiply();

  EXPECT_THAT(bitmap->GetBuffer(), ElementsAre(0xff, 0xff, 0xff, 0x7f));
}
#endif  // defined(PDF_USE_SKIA)
