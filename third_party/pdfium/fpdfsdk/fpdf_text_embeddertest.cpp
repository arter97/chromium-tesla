// Copyright 2015 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if defined(UNSAFE_BUFFERS_BUILD)
// TODO(crbug.com/pdfium/2154): resolve buffer safety issues.
#pragma allow_unsafe_buffers
#endif

#include <algorithm>
#include <utility>
#include <vector>

#include "build/build_config.h"
#include "core/fxcrt/stl_util.h"
#include "core/fxge/fx_font.h"
#include "public/cpp/fpdf_scopers.h"
#include "public/fpdf_doc.h"
#include "public/fpdf_text.h"
#include "public/fpdf_transformpage.h"
#include "public/fpdfview.h"
#include "testing/embedder_test.h"
#include "testing/fx_string_testhelpers.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {

constexpr char kHelloGoodbyeText[] = "Hello, world!\r\nGoodbye, world!";
constexpr int kHelloGoodbyeTextSize = std::size(kHelloGoodbyeText);

bool check_unsigned_shorts(const char* expected,
                           const unsigned short* actual,
                           size_t length) {
  if (length > strlen(expected) + 1)
    return false;

  for (size_t i = 0; i < length; ++i) {
    if (actual[i] != static_cast<unsigned short>(expected[i]))
      return false;
  }
  return true;
}

}  // namespace

class FPDFTextEmbedderTest : public EmbedderTest {};

TEST_F(FPDFTextEmbedderTest, Text) {
  ASSERT_TRUE(OpenDocument("hello_world.pdf"));
  FPDF_PAGE page = LoadPage(0);
  ASSERT_TRUE(page);

  FPDF_TEXTPAGE textpage = FPDFText_LoadPage(page);
  ASSERT_TRUE(textpage);

  unsigned short buffer[128];
  fxcrt::Fill(buffer, 0xbdbd);

  // Check that edge cases are handled gracefully
  EXPECT_EQ(0, FPDFText_GetText(textpage, 0, 128, nullptr));
  EXPECT_EQ(0, FPDFText_GetText(textpage, -1, 128, buffer));
  EXPECT_EQ(0, FPDFText_GetText(textpage, 0, -1, buffer));
  EXPECT_EQ(1, FPDFText_GetText(textpage, 0, 0, buffer));
  EXPECT_EQ(0, buffer[0]);

  // Keep going and check the next case.
  fxcrt::Fill(buffer, 0xbdbd);
  EXPECT_EQ(2, FPDFText_GetText(textpage, 0, 1, buffer));
  EXPECT_EQ(kHelloGoodbyeText[0], buffer[0]);
  EXPECT_EQ(0, buffer[1]);

  // Check includes the terminating NUL that is provided.
  int num_chars = FPDFText_GetText(textpage, 0, 128, buffer);
  ASSERT_EQ(kHelloGoodbyeTextSize, num_chars);
  EXPECT_TRUE(
      check_unsigned_shorts(kHelloGoodbyeText, buffer, kHelloGoodbyeTextSize));

  // Count does not include the terminating NUL in the string literal.
  EXPECT_EQ(kHelloGoodbyeTextSize - 1, FPDFText_CountChars(textpage));
  for (size_t i = 0; i < kHelloGoodbyeTextSize - 1; ++i) {
    EXPECT_EQ(static_cast<unsigned int>(kHelloGoodbyeText[i]),
              FPDFText_GetUnicode(textpage, i))
        << " at " << i;
  }

  // Extracting using a buffer that will be completely filled. Small buffer is
  // 12 elements long, since it will need 2 locations per displayed character in
  // the expected string, plus 2 more for the terminating character.
  static const char kSmallExpected[] = "Hello";
  unsigned short small_buffer[12];
  fxcrt::Fill(buffer, 0xbdbd);
  EXPECT_EQ(6, FPDFText_GetText(textpage, 0, 5, small_buffer));
  EXPECT_TRUE(check_unsigned_shorts(kSmallExpected, small_buffer,
                                    sizeof(kSmallExpected)));

  EXPECT_EQ(12.0, FPDFText_GetFontSize(textpage, 0));
  EXPECT_EQ(16.0, FPDFText_GetFontSize(textpage, 15));

  double left = 1.0;
  double right = 2.0;
  double bottom = 3.0;
  double top = 4.0;
  EXPECT_FALSE(FPDFText_GetCharBox(nullptr, 4, &left, &right, &bottom, &top));
  EXPECT_DOUBLE_EQ(1.0, left);
  EXPECT_DOUBLE_EQ(2.0, right);
  EXPECT_DOUBLE_EQ(3.0, bottom);
  EXPECT_DOUBLE_EQ(4.0, top);
  EXPECT_FALSE(FPDFText_GetCharBox(textpage, -1, &left, &right, &bottom, &top));
  EXPECT_DOUBLE_EQ(1.0, left);
  EXPECT_DOUBLE_EQ(2.0, right);
  EXPECT_DOUBLE_EQ(3.0, bottom);
  EXPECT_DOUBLE_EQ(4.0, top);
  EXPECT_FALSE(FPDFText_GetCharBox(textpage, 55, &left, &right, &bottom, &top));
  EXPECT_DOUBLE_EQ(1.0, left);
  EXPECT_DOUBLE_EQ(2.0, right);
  EXPECT_DOUBLE_EQ(3.0, bottom);
  EXPECT_DOUBLE_EQ(4.0, top);
  EXPECT_FALSE(
      FPDFText_GetCharBox(textpage, 4, nullptr, &right, &bottom, &top));
  EXPECT_FALSE(FPDFText_GetCharBox(textpage, 4, &left, nullptr, &bottom, &top));
  EXPECT_FALSE(FPDFText_GetCharBox(textpage, 4, &left, &right, nullptr, &top));
  EXPECT_FALSE(
      FPDFText_GetCharBox(textpage, 4, &left, &right, &bottom, nullptr));
  EXPECT_FALSE(
      FPDFText_GetCharBox(textpage, 4, nullptr, nullptr, nullptr, nullptr));

  EXPECT_TRUE(FPDFText_GetCharBox(textpage, 4, &left, &right, &bottom, &top));
  EXPECT_NEAR(41.120, left, 0.001);
  EXPECT_NEAR(46.208, right, 0.001);
  EXPECT_NEAR(49.892, bottom, 0.001);
  EXPECT_NEAR(55.652, top, 0.001);

  FS_RECTF rect = {4.0f, 1.0f, 3.0f, 2.0f};
  EXPECT_FALSE(FPDFText_GetLooseCharBox(nullptr, 4, &rect));
  EXPECT_FLOAT_EQ(4.0f, rect.left);
  EXPECT_FLOAT_EQ(3.0f, rect.right);
  EXPECT_FLOAT_EQ(2.0f, rect.bottom);
  EXPECT_FLOAT_EQ(1.0f, rect.top);
  EXPECT_FALSE(FPDFText_GetLooseCharBox(textpage, -1, &rect));
  EXPECT_FLOAT_EQ(4.0f, rect.left);
  EXPECT_FLOAT_EQ(3.0f, rect.right);
  EXPECT_FLOAT_EQ(2.0f, rect.bottom);
  EXPECT_FLOAT_EQ(1.0f, rect.top);
  EXPECT_FALSE(FPDFText_GetLooseCharBox(textpage, 55, &rect));
  EXPECT_FLOAT_EQ(4.0f, rect.left);
  EXPECT_FLOAT_EQ(3.0f, rect.right);
  EXPECT_FLOAT_EQ(2.0f, rect.bottom);
  EXPECT_FLOAT_EQ(1.0f, rect.top);
  EXPECT_FALSE(FPDFText_GetLooseCharBox(textpage, 4, nullptr));

  EXPECT_TRUE(FPDFText_GetLooseCharBox(textpage, 4, &rect));
  EXPECT_FLOAT_EQ(40.664001f, rect.left);
  EXPECT_FLOAT_EQ(46.664001f, rect.right);
  EXPECT_FLOAT_EQ(47.667271f, rect.bottom);
  EXPECT_FLOAT_EQ(59.667271f, rect.top);

  double x = 0.0;
  double y = 0.0;
  EXPECT_TRUE(FPDFText_GetCharOrigin(textpage, 4, &x, &y));
  EXPECT_NEAR(40.664, x, 0.001);
  EXPECT_NEAR(50.000, y, 0.001);

  EXPECT_EQ(4, FPDFText_GetCharIndexAtPos(textpage, 42.0, 50.0, 1.0, 1.0));
  EXPECT_EQ(-1, FPDFText_GetCharIndexAtPos(textpage, 0.0, 0.0, 1.0, 1.0));
  EXPECT_EQ(-1, FPDFText_GetCharIndexAtPos(textpage, 199.0, 199.0, 1.0, 1.0));

  // Test out of range indicies.
  EXPECT_EQ(-1,
            FPDFText_GetCharIndexAtPos(textpage, 42.0, 10000000.0, 1.0, 1.0));
  EXPECT_EQ(-1, FPDFText_GetCharIndexAtPos(textpage, -1.0, 50.0, 1.0, 1.0));

  // Count does not include the terminating NUL in the string literal.
  EXPECT_EQ(2, FPDFText_CountRects(textpage, 0, kHelloGoodbyeTextSize - 1));

  left = 0.0;
  right = 0.0;
  bottom = 0.0;
  top = 0.0;
  EXPECT_TRUE(FPDFText_GetRect(textpage, 1, &left, &top, &right, &bottom));
  EXPECT_NEAR(20.800, left, 0.001);
  EXPECT_NEAR(135.040, right, 0.001);
  EXPECT_NEAR(96.688, bottom, 0.001);
  EXPECT_NEAR(111.600, top, 0.001);

  // Test out of range indicies set outputs to (0.0, 0.0, 0.0, 0.0).
  left = -1.0;
  right = -1.0;
  bottom = -1.0;
  top = -1.0;
  EXPECT_FALSE(FPDFText_GetRect(textpage, -1, &left, &top, &right, &bottom));
  EXPECT_EQ(0.0, left);
  EXPECT_EQ(0.0, right);
  EXPECT_EQ(0.0, bottom);
  EXPECT_EQ(0.0, top);

  left = -2.0;
  right = -2.0;
  bottom = -2.0;
  top = -2.0;
  EXPECT_FALSE(FPDFText_GetRect(textpage, 2, &left, &top, &right, &bottom));
  EXPECT_EQ(0.0, left);
  EXPECT_EQ(0.0, right);
  EXPECT_EQ(0.0, bottom);
  EXPECT_EQ(0.0, top);

  EXPECT_EQ(
      9, FPDFText_GetBoundedText(textpage, 41.0, 56.0, 82.0, 48.0, nullptr, 0));

  // Extract starting at character 4 as above.
  fxcrt::Fill(buffer, 0xbdbd);
  EXPECT_EQ(
      1, FPDFText_GetBoundedText(textpage, 41.0, 56.0, 82.0, 48.0, buffer, 1));
  EXPECT_TRUE(check_unsigned_shorts(kHelloGoodbyeText + 4, buffer, 1));
  EXPECT_EQ(0xbdbd, buffer[1]);

  fxcrt::Fill(buffer, 0xbdbd);
  EXPECT_EQ(
      9, FPDFText_GetBoundedText(textpage, 41.0, 56.0, 82.0, 48.0, buffer, 9));
  EXPECT_TRUE(check_unsigned_shorts(kHelloGoodbyeText + 4, buffer, 8));
  EXPECT_EQ(0xbdbd, buffer[9]);

  fxcrt::Fill(buffer, 0xbdbd);
  EXPECT_EQ(10, FPDFText_GetBoundedText(textpage, 41.0, 56.0, 82.0, 48.0,
                                        buffer, 128));
  EXPECT_TRUE(check_unsigned_shorts(kHelloGoodbyeText + 4, buffer, 9));
  EXPECT_EQ(0u, buffer[9]);
  EXPECT_EQ(0xbdbd, buffer[10]);

  FPDFText_ClosePage(textpage);
  UnloadPage(page);
}

TEST_F(FPDFTextEmbedderTest, TextVertical) {
  ASSERT_TRUE(OpenDocument("vertical_text.pdf"));
  FPDF_PAGE page = LoadPage(0);
  ASSERT_TRUE(page);

  FPDF_TEXTPAGE textpage = FPDFText_LoadPage(page);
  ASSERT_TRUE(textpage);

  EXPECT_EQ(12.0, FPDFText_GetFontSize(textpage, 0));

  double x = 0.0;
  double y = 0.0;
  EXPECT_TRUE(FPDFText_GetCharOrigin(textpage, 1, &x, &y));
  EXPECT_NEAR(6.664, x, 0.001);
  EXPECT_NEAR(171.508, y, 0.001);

  EXPECT_TRUE(FPDFText_GetCharOrigin(textpage, 2, &x, &y));
  EXPECT_NEAR(8.668, x, 0.001);
  EXPECT_NEAR(160.492, y, 0.001);

  FS_RECTF rect;
  EXPECT_TRUE(FPDFText_GetLooseCharBox(textpage, 1, &rect));
  EXPECT_NEAR(4, rect.left, 0.001);
  EXPECT_NEAR(16, rect.right, 0.001);
  EXPECT_NEAR(178.984, rect.bottom, 0.001);
  EXPECT_NEAR(170.308, rect.top, 0.001);

  EXPECT_TRUE(FPDFText_GetLooseCharBox(textpage, 2, &rect));
  EXPECT_NEAR(4, rect.left, 0.001);
  EXPECT_NEAR(16, rect.right, 0.001);
  EXPECT_NEAR(170.308, rect.bottom, 0.001);
  EXPECT_NEAR(159.292, rect.top, 0.001);

  FPDFText_ClosePage(textpage);
  UnloadPage(page);
}

TEST_F(FPDFTextEmbedderTest, TextHebrewMirrored) {
  ASSERT_TRUE(OpenDocument("hebrew_mirrored.pdf"));
  FPDF_PAGE page = LoadPage(0);
  ASSERT_TRUE(page);

  {
    ScopedFPDFTextPage textpage(FPDFText_LoadPage(page));
    ASSERT_TRUE(textpage);

    constexpr int kCharCount = 10;
    ASSERT_EQ(kCharCount, FPDFText_CountChars(textpage.get()));

    unsigned short buffer[kCharCount + 1];
    fxcrt::Fill(buffer, 0x4242);
    EXPECT_EQ(kCharCount + 1,
              FPDFText_GetText(textpage.get(), 0, kCharCount, buffer));
    EXPECT_EQ(0x05d1, buffer[0]);
    EXPECT_EQ(0x05e0, buffer[1]);
    EXPECT_EQ(0x05d9, buffer[2]);
    EXPECT_EQ(0x05de, buffer[3]);
    EXPECT_EQ(0x05d9, buffer[4]);
    EXPECT_EQ(0x05df, buffer[5]);
    EXPECT_EQ(0x000d, buffer[6]);
    EXPECT_EQ(0x000a, buffer[7]);
    EXPECT_EQ(0x05df, buffer[8]);
    EXPECT_EQ(0x05d1, buffer[9]);
  }

  UnloadPage(page);
}

TEST_F(FPDFTextEmbedderTest, TextSearch) {
  ASSERT_TRUE(OpenDocument("hello_world.pdf"));
  FPDF_PAGE page = LoadPage(0);
  ASSERT_TRUE(page);

  FPDF_TEXTPAGE textpage = FPDFText_LoadPage(page);
  ASSERT_TRUE(textpage);

  ScopedFPDFWideString nope = GetFPDFWideString(L"nope");
  ScopedFPDFWideString world = GetFPDFWideString(L"world");
  ScopedFPDFWideString world_caps = GetFPDFWideString(L"WORLD");
  ScopedFPDFWideString world_substr = GetFPDFWideString(L"orld");

  {
    // No occurrences of "nope" in test page.
    ScopedFPDFTextFind search(FPDFText_FindStart(textpage, nope.get(), 0, 0));
    EXPECT_TRUE(search);
    EXPECT_EQ(0, FPDFText_GetSchResultIndex(search.get()));
    EXPECT_EQ(0, FPDFText_GetSchCount(search.get()));

    // Advancing finds nothing.
    EXPECT_FALSE(FPDFText_FindNext(search.get()));
    EXPECT_EQ(0, FPDFText_GetSchResultIndex(search.get()));
    EXPECT_EQ(0, FPDFText_GetSchCount(search.get()));

    // Retreating finds nothing.
    EXPECT_FALSE(FPDFText_FindPrev(search.get()));
    EXPECT_EQ(0, FPDFText_GetSchResultIndex(search.get()));
    EXPECT_EQ(0, FPDFText_GetSchCount(search.get()));
  }

  {
    // Two occurrences of "world" in test page.
    ScopedFPDFTextFind search(FPDFText_FindStart(textpage, world.get(), 0, 2));
    EXPECT_TRUE(search);

    // Remains not found until advanced.
    EXPECT_EQ(0, FPDFText_GetSchResultIndex(search.get()));
    EXPECT_EQ(0, FPDFText_GetSchCount(search.get()));

    // First occurrence of "world" in this test page.
    EXPECT_TRUE(FPDFText_FindNext(search.get()));
    EXPECT_EQ(7, FPDFText_GetSchResultIndex(search.get()));
    EXPECT_EQ(5, FPDFText_GetSchCount(search.get()));

    // Last occurrence of "world" in this test page.
    EXPECT_TRUE(FPDFText_FindNext(search.get()));
    EXPECT_EQ(24, FPDFText_GetSchResultIndex(search.get()));
    EXPECT_EQ(5, FPDFText_GetSchCount(search.get()));

    // Found position unchanged when fails to advance.
    EXPECT_FALSE(FPDFText_FindNext(search.get()));
    EXPECT_EQ(24, FPDFText_GetSchResultIndex(search.get()));
    EXPECT_EQ(5, FPDFText_GetSchCount(search.get()));

    // Back to first occurrence.
    EXPECT_TRUE(FPDFText_FindPrev(search.get()));
    EXPECT_EQ(7, FPDFText_GetSchResultIndex(search.get()));
    EXPECT_EQ(5, FPDFText_GetSchCount(search.get()));

    // Found position unchanged when fails to retreat.
    EXPECT_FALSE(FPDFText_FindPrev(search.get()));
    EXPECT_EQ(7, FPDFText_GetSchResultIndex(search.get()));
    EXPECT_EQ(5, FPDFText_GetSchCount(search.get()));
  }

  {
    // Exact search unaffected by case sensitiity and whole word flags.
    ScopedFPDFTextFind search(FPDFText_FindStart(
        textpage, world.get(), FPDF_MATCHCASE | FPDF_MATCHWHOLEWORD, 0));
    EXPECT_TRUE(search);
    EXPECT_TRUE(FPDFText_FindNext(search.get()));
    EXPECT_EQ(7, FPDFText_GetSchResultIndex(search.get()));
    EXPECT_EQ(5, FPDFText_GetSchCount(search.get()));
  }

  {
    // Default is case-insensitive, so matching agaist caps works.
    ScopedFPDFTextFind search(
        FPDFText_FindStart(textpage, world_caps.get(), 0, 0));
    EXPECT_TRUE(search);
    EXPECT_TRUE(FPDFText_FindNext(search.get()));
    EXPECT_EQ(7, FPDFText_GetSchResultIndex(search.get()));
    EXPECT_EQ(5, FPDFText_GetSchCount(search.get()));
  }

  {
    // But can be made case sensitive, in which case this fails.
    ScopedFPDFTextFind search(
        FPDFText_FindStart(textpage, world_caps.get(), FPDF_MATCHCASE, 0));
    EXPECT_FALSE(FPDFText_FindNext(search.get()));
    EXPECT_EQ(0, FPDFText_GetSchResultIndex(search.get()));
    EXPECT_EQ(0, FPDFText_GetSchCount(search.get()));
  }

  {
    // Default is match anywhere within word, so matching substring works.
    ScopedFPDFTextFind search(
        FPDFText_FindStart(textpage, world_substr.get(), 0, 0));
    EXPECT_TRUE(FPDFText_FindNext(search.get()));
    EXPECT_EQ(8, FPDFText_GetSchResultIndex(search.get()));
    EXPECT_EQ(4, FPDFText_GetSchCount(search.get()));
  }

  {
    // But can be made to mach word boundaries, in which case this fails.
    ScopedFPDFTextFind search(FPDFText_FindStart(textpage, world_substr.get(),
                                                 FPDF_MATCHWHOLEWORD, 0));
    EXPECT_FALSE(FPDFText_FindNext(search.get()));
    // TODO(tsepez): investigate strange index/count values in this state.
  }

  FPDFText_ClosePage(textpage);
  UnloadPage(page);
}

TEST_F(FPDFTextEmbedderTest, TextSearchConsecutive) {
  ASSERT_TRUE(OpenDocument("find_text_consecutive.pdf"));
  FPDF_PAGE page = LoadPage(0);
  ASSERT_TRUE(page);

  FPDF_TEXTPAGE textpage = FPDFText_LoadPage(page);
  ASSERT_TRUE(textpage);

  ScopedFPDFWideString aaaa = GetFPDFWideString(L"aaaa");

  {
    // Search for "aaaa" yields 2 results in "aaaaaaaaaa".
    ScopedFPDFTextFind search(FPDFText_FindStart(textpage, aaaa.get(), 0, 0));
    EXPECT_TRUE(search);

    // Remains not found until advanced.
    EXPECT_EQ(0, FPDFText_GetSchResultIndex(search.get()));
    EXPECT_EQ(0, FPDFText_GetSchCount(search.get()));

    // First occurrence of "aaaa" in this test page.
    EXPECT_TRUE(FPDFText_FindNext(search.get()));
    EXPECT_EQ(0, FPDFText_GetSchResultIndex(search.get()));
    EXPECT_EQ(4, FPDFText_GetSchCount(search.get()));

    // Last occurrence of "aaaa" in this test page.
    EXPECT_TRUE(FPDFText_FindNext(search.get()));
    EXPECT_EQ(4, FPDFText_GetSchResultIndex(search.get()));
    EXPECT_EQ(4, FPDFText_GetSchCount(search.get()));

    // Found position unchanged when fails to advance.
    EXPECT_FALSE(FPDFText_FindNext(search.get()));
    EXPECT_EQ(4, FPDFText_GetSchResultIndex(search.get()));
    EXPECT_EQ(4, FPDFText_GetSchCount(search.get()));

    // Back to first occurrence.
    EXPECT_TRUE(FPDFText_FindPrev(search.get()));
    EXPECT_EQ(0, FPDFText_GetSchResultIndex(search.get()));
    EXPECT_EQ(4, FPDFText_GetSchCount(search.get()));

    // Found position unchanged when fails to retreat.
    EXPECT_FALSE(FPDFText_FindPrev(search.get()));
    EXPECT_EQ(0, FPDFText_GetSchResultIndex(search.get()));
    EXPECT_EQ(4, FPDFText_GetSchCount(search.get()));
  }

  {
    // Search for "aaaa" yields 7 results in "aaaaaaaaaa", when searching with
    // FPDF_CONSECUTIVE.
    ScopedFPDFTextFind search(
        FPDFText_FindStart(textpage, aaaa.get(), FPDF_CONSECUTIVE, 0));
    EXPECT_TRUE(search);

    // Remains not found until advanced.
    EXPECT_EQ(0, FPDFText_GetSchResultIndex(search.get()));
    EXPECT_EQ(0, FPDFText_GetSchCount(search.get()));

    // Find consecutive occurrences of "aaaa" in this test page:
    for (int i = 0; i < 7; ++i) {
      EXPECT_TRUE(FPDFText_FindNext(search.get()));
      EXPECT_EQ(i, FPDFText_GetSchResultIndex(search.get()));
      EXPECT_EQ(4, FPDFText_GetSchCount(search.get()));
    }

    // Found position unchanged when fails to advance.
    EXPECT_FALSE(FPDFText_FindNext(search.get()));
    EXPECT_EQ(6, FPDFText_GetSchResultIndex(search.get()));
    EXPECT_EQ(4, FPDFText_GetSchCount(search.get()));

    for (int i = 5; i >= 0; --i) {
      EXPECT_TRUE(FPDFText_FindPrev(search.get()));
      EXPECT_EQ(i, FPDFText_GetSchResultIndex(search.get()));
      EXPECT_EQ(4, FPDFText_GetSchCount(search.get()));
    }

    // Found position unchanged when fails to retreat.
    EXPECT_FALSE(FPDFText_FindPrev(search.get()));
    EXPECT_EQ(0, FPDFText_GetSchResultIndex(search.get()));
    EXPECT_EQ(4, FPDFText_GetSchCount(search.get()));
  }

  FPDFText_ClosePage(textpage);
  UnloadPage(page);
}

TEST_F(FPDFTextEmbedderTest, TextSearchTermAtEnd) {
  ASSERT_TRUE(OpenDocument("hello_world.pdf"));
  FPDF_PAGE page = LoadPage(0);
  ASSERT_TRUE(page);

  {
    ScopedFPDFTextPage textpage(FPDFText_LoadPage(page));
    ASSERT_TRUE(textpage);

    ScopedFPDFWideString search_term = GetFPDFWideString(L"world!");
    ScopedFPDFTextFind search(
        FPDFText_FindStart(textpage.get(), search_term.get(), 0, 0));
    ASSERT_TRUE(search);
    EXPECT_EQ(0, FPDFText_GetSchResultIndex(search.get()));
    EXPECT_EQ(0, FPDFText_GetSchCount(search.get()));

    EXPECT_TRUE(FPDFText_FindNext(search.get()));
    EXPECT_EQ(7, FPDFText_GetSchResultIndex(search.get()));
    EXPECT_EQ(6, FPDFText_GetSchCount(search.get()));

    EXPECT_TRUE(FPDFText_FindNext(search.get()));
    EXPECT_EQ(24, FPDFText_GetSchResultIndex(search.get()));
    EXPECT_EQ(6, FPDFText_GetSchCount(search.get()));
  }

  UnloadPage(page);
}

TEST_F(FPDFTextEmbedderTest, TextSearchLeadingSpace) {
  ASSERT_TRUE(OpenDocument("hello_world.pdf"));
  FPDF_PAGE page = LoadPage(0);
  ASSERT_TRUE(page);

  {
    ScopedFPDFTextPage textpage(FPDFText_LoadPage(page));
    ASSERT_TRUE(textpage);

    ScopedFPDFWideString search_term = GetFPDFWideString(L" Good");
    ScopedFPDFTextFind search(
        FPDFText_FindStart(textpage.get(), search_term.get(), 0, 0));
    ASSERT_TRUE(search);
    EXPECT_EQ(0, FPDFText_GetSchResultIndex(search.get()));
    EXPECT_EQ(0, FPDFText_GetSchCount(search.get()));

    EXPECT_TRUE(FPDFText_FindNext(search.get()));
    EXPECT_EQ(14, FPDFText_GetSchResultIndex(search.get()));
    EXPECT_EQ(5, FPDFText_GetSchCount(search.get()));

    EXPECT_FALSE(FPDFText_FindNext(search.get()));
  }

  UnloadPage(page);
}

TEST_F(FPDFTextEmbedderTest, TextSearchTrailingSpace) {
  ASSERT_TRUE(OpenDocument("hello_world.pdf"));
  FPDF_PAGE page = LoadPage(0);
  ASSERT_TRUE(page);

  {
    ScopedFPDFTextPage textpage(FPDFText_LoadPage(page));
    ASSERT_TRUE(textpage);

    ScopedFPDFWideString search_term = GetFPDFWideString(L"ld! ");
    ScopedFPDFTextFind search(
        FPDFText_FindStart(textpage.get(), search_term.get(), 0, 0));
    ASSERT_TRUE(search);
    EXPECT_EQ(0, FPDFText_GetSchResultIndex(search.get()));
    EXPECT_EQ(0, FPDFText_GetSchCount(search.get()));

    EXPECT_TRUE(FPDFText_FindNext(search.get()));
    EXPECT_EQ(10, FPDFText_GetSchResultIndex(search.get()));
    EXPECT_EQ(4, FPDFText_GetSchCount(search.get()));

    EXPECT_FALSE(FPDFText_FindNext(search.get()));
  }

  UnloadPage(page);
}

TEST_F(FPDFTextEmbedderTest, TextSearchSpaceInSearchTerm) {
  ASSERT_TRUE(OpenDocument("hello_world.pdf"));
  FPDF_PAGE page = LoadPage(0);
  ASSERT_TRUE(page);

  {
    ScopedFPDFTextPage textpage(FPDFText_LoadPage(page));
    ASSERT_TRUE(textpage);

    ScopedFPDFWideString search_term = GetFPDFWideString(L"ld! G");
    ScopedFPDFTextFind search(
        FPDFText_FindStart(textpage.get(), search_term.get(), 0, 0));
    ASSERT_TRUE(search);
    EXPECT_EQ(0, FPDFText_GetSchResultIndex(search.get()));
    EXPECT_EQ(0, FPDFText_GetSchCount(search.get()));

    EXPECT_TRUE(FPDFText_FindNext(search.get()));
    EXPECT_EQ(10, FPDFText_GetSchResultIndex(search.get()));
    // Note: Even though `search_term` contains 5 characters,
    // FPDFText_FindNext() matched "\r\n" in `textpage` against the space in
    // `search_term`.
    EXPECT_EQ(6, FPDFText_GetSchCount(search.get()));

    EXPECT_FALSE(FPDFText_FindNext(search.get()));
  }

  UnloadPage(page);
}

// Fails on Windows. https://crbug.com/pdfium/1370
#if BUILDFLAG(IS_WIN)
#define MAYBE_TextSearchLatinExtended DISABLED_TextSearchLatinExtended
#else
#define MAYBE_TextSearchLatinExtended TextSearchLatinExtended
#endif
TEST_F(FPDFTextEmbedderTest, MAYBE_TextSearchLatinExtended) {
  ASSERT_TRUE(OpenDocument("latin_extended.pdf"));
  FPDF_PAGE page = LoadPage(0);
  ASSERT_TRUE(page);

  FPDF_TEXTPAGE textpage = FPDFText_LoadPage(page);
  ASSERT_TRUE(textpage);

  // Upper/lowercase 'a' with breve.
  constexpr FPDF_WCHAR kNeedleUpper[] = {0x0102, 0x0000};
  constexpr FPDF_WCHAR kNeedleLower[] = {0x0103, 0x0000};

  for (const auto* needle : {kNeedleUpper, kNeedleLower}) {
    ScopedFPDFTextFind search(FPDFText_FindStart(textpage, needle, 0, 0));
    EXPECT_TRUE(search);
    EXPECT_EQ(0, FPDFText_GetSchResultIndex(search.get()));
    EXPECT_EQ(0, FPDFText_GetSchCount(search.get()));

    // Should find 2 results at position 21/22, both with length 1.
    EXPECT_TRUE(FPDFText_FindNext(search.get()));
    EXPECT_EQ(2, FPDFText_GetSchResultIndex(search.get()));
    EXPECT_EQ(1, FPDFText_GetSchCount(search.get()));
    EXPECT_TRUE(FPDFText_FindNext(search.get()));
    EXPECT_EQ(3, FPDFText_GetSchResultIndex(search.get()));
    EXPECT_EQ(1, FPDFText_GetSchCount(search.get()));
    // And no more than 2 results.
    EXPECT_FALSE(FPDFText_FindNext(search.get()));
  }

  FPDFText_ClosePage(textpage);
  UnloadPage(page);
}

// Test that the page has characters despite a bad stream length.
TEST_F(FPDFTextEmbedderTest, StreamLengthPastEndOfFile) {
  ASSERT_TRUE(OpenDocument("bug_57.pdf"));
  FPDF_PAGE page = LoadPage(0);
  ASSERT_TRUE(page);

  FPDF_TEXTPAGE textpage = FPDFText_LoadPage(page);
  ASSERT_TRUE(textpage);
  EXPECT_EQ(13, FPDFText_CountChars(textpage));

  FPDFText_ClosePage(textpage);
  UnloadPage(page);
}

TEST_F(FPDFTextEmbedderTest, WebLinks) {
  ASSERT_TRUE(OpenDocument("weblinks.pdf"));
  FPDF_PAGE page = LoadPage(0);
  ASSERT_TRUE(page);

  FPDF_TEXTPAGE textpage = FPDFText_LoadPage(page);
  ASSERT_TRUE(textpage);

  {
    ScopedFPDFPageLink pagelink(FPDFLink_LoadWebLinks(textpage));
    EXPECT_TRUE(pagelink);

    // Page contains two HTTP-style URLs.
    EXPECT_EQ(2, FPDFLink_CountWebLinks(pagelink.get()));

    // Only a terminating NUL required for bogus links.
    EXPECT_EQ(1, FPDFLink_GetURL(pagelink.get(), 2, nullptr, 0));
    EXPECT_EQ(1, FPDFLink_GetURL(pagelink.get(), 1400, nullptr, 0));
    EXPECT_EQ(1, FPDFLink_GetURL(pagelink.get(), -1, nullptr, 0));
  }

  FPDF_PAGELINK pagelink = FPDFLink_LoadWebLinks(textpage);
  EXPECT_TRUE(pagelink);

  // Query the number of characters required for each link (incl NUL).
  EXPECT_EQ(25, FPDFLink_GetURL(pagelink, 0, nullptr, 0));
  EXPECT_EQ(26, FPDFLink_GetURL(pagelink, 1, nullptr, 0));

  static const char expected_url[] = "http://example.com?q=foo";
  static const size_t expected_len = sizeof(expected_url);
  unsigned short buffer[128];

  // Retrieve a link with too small a buffer.  Buffer will not be
  // NUL-terminated, but must not be modified past indicated length,
  // so pre-fill with a pattern to check write bounds.
  fxcrt::Fill(buffer, 0xbdbd);
  EXPECT_EQ(1, FPDFLink_GetURL(pagelink, 0, buffer, 1));
  EXPECT_TRUE(check_unsigned_shorts(expected_url, buffer, 1));
  EXPECT_EQ(0xbdbd, buffer[1]);

  // Check buffer that doesn't have space for a terminating NUL.
  fxcrt::Fill(buffer, 0xbdbd);
  EXPECT_EQ(static_cast<int>(expected_len - 1),
            FPDFLink_GetURL(pagelink, 0, buffer, expected_len - 1));
  EXPECT_TRUE(check_unsigned_shorts(expected_url, buffer, expected_len - 1));
  EXPECT_EQ(0xbdbd, buffer[expected_len - 1]);

  // Retreive link with exactly-sized buffer.
  fxcrt::Fill(buffer, 0xbdbd);
  EXPECT_EQ(static_cast<int>(expected_len),
            FPDFLink_GetURL(pagelink, 0, buffer, expected_len));
  EXPECT_TRUE(check_unsigned_shorts(expected_url, buffer, expected_len));
  EXPECT_EQ(0u, buffer[expected_len - 1]);
  EXPECT_EQ(0xbdbd, buffer[expected_len]);

  // Retreive link with ample-sized-buffer.
  fxcrt::Fill(buffer, 0xbdbd);
  EXPECT_EQ(static_cast<int>(expected_len),
            FPDFLink_GetURL(pagelink, 0, buffer, 128));
  EXPECT_TRUE(check_unsigned_shorts(expected_url, buffer, expected_len));
  EXPECT_EQ(0u, buffer[expected_len - 1]);
  EXPECT_EQ(0xbdbd, buffer[expected_len]);

  // Each link rendered in a single rect in this test page.
  EXPECT_EQ(1, FPDFLink_CountRects(pagelink, 0));
  EXPECT_EQ(1, FPDFLink_CountRects(pagelink, 1));

  // Each link rendered in a single rect in this test page.
  EXPECT_EQ(0, FPDFLink_CountRects(pagelink, -1));
  EXPECT_EQ(0, FPDFLink_CountRects(pagelink, 2));
  EXPECT_EQ(0, FPDFLink_CountRects(pagelink, 10000));

  // Check boundary of valid link index with valid rect index.
  double left = 0.0;
  double right = 0.0;
  double top = 0.0;
  double bottom = 0.0;
  EXPECT_TRUE(FPDFLink_GetRect(pagelink, 0, 0, &left, &top, &right, &bottom));
  EXPECT_NEAR(50.828, left, 0.001);
  EXPECT_NEAR(187.904, right, 0.001);
  EXPECT_NEAR(97.516, bottom, 0.001);
  EXPECT_NEAR(108.700, top, 0.001);

  // Check that valid link with invalid rect index leaves parameters unchanged.
  left = -1.0;
  right = -1.0;
  top = -1.0;
  bottom = -1.0;
  EXPECT_FALSE(FPDFLink_GetRect(pagelink, 0, 1, &left, &top, &right, &bottom));
  EXPECT_EQ(-1.0, left);
  EXPECT_EQ(-1.0, right);
  EXPECT_EQ(-1.0, bottom);
  EXPECT_EQ(-1.0, top);

  // Check that invalid link index leaves parameters unchanged.
  left = -2.0;
  right = -2.0;
  top = -2.0;
  bottom = -2.0;
  EXPECT_FALSE(FPDFLink_GetRect(pagelink, -1, 0, &left, &top, &right, &bottom));
  EXPECT_EQ(-2.0, left);
  EXPECT_EQ(-2.0, right);
  EXPECT_EQ(-2.0, bottom);
  EXPECT_EQ(-2.0, top);

  FPDFLink_CloseWebLinks(pagelink);
  FPDFText_ClosePage(textpage);
  UnloadPage(page);
}

TEST_F(FPDFTextEmbedderTest, WebLinksAcrossLines) {
  ASSERT_TRUE(OpenDocument("weblinks_across_lines.pdf"));
  FPDF_PAGE page = LoadPage(0);
  ASSERT_TRUE(page);

  FPDF_TEXTPAGE textpage = FPDFText_LoadPage(page);
  ASSERT_TRUE(textpage);

  FPDF_PAGELINK pagelink = FPDFLink_LoadWebLinks(textpage);
  EXPECT_TRUE(pagelink);

  static const char* const kExpectedUrls[] = {
      "http://example.com",           // from "http://www.example.com?\r\nfoo"
      "http://example.com/",          // from "http://www.example.com/\r\nfoo"
      "http://example.com/test-foo",  // from "http://example.com/test-\r\nfoo"
      "http://abc.com/test-foo",      // from "http://abc.com/test-\r\n\r\nfoo"
      // Next two links from "http://www.example.com/\r\nhttp://www.abc.com/"
      "http://example.com/",
      "http://www.abc.com",
  };
  static const int kNumLinks = static_cast<int>(std::size(kExpectedUrls));

  EXPECT_EQ(kNumLinks, FPDFLink_CountWebLinks(pagelink));

  for (int i = 0; i < kNumLinks; i++) {
    unsigned short buffer[128] = {};
    const size_t expected_len = strlen(kExpectedUrls[i]) + 1;
    EXPECT_EQ(static_cast<int>(expected_len),
              FPDFLink_GetURL(pagelink, i, nullptr, 0));
    EXPECT_EQ(static_cast<int>(expected_len),
              FPDFLink_GetURL(pagelink, i, buffer, std::size(buffer)));
    EXPECT_TRUE(check_unsigned_shorts(kExpectedUrls[i], buffer, expected_len));
  }

  FPDFLink_CloseWebLinks(pagelink);
  FPDFText_ClosePage(textpage);
  UnloadPage(page);
}

TEST_F(FPDFTextEmbedderTest, WebLinksAcrossLinesBug) {
  ASSERT_TRUE(OpenDocument("bug_650.pdf"));
  FPDF_PAGE page = LoadPage(0);
  ASSERT_TRUE(page);

  FPDF_TEXTPAGE textpage = FPDFText_LoadPage(page);
  ASSERT_TRUE(textpage);

  FPDF_PAGELINK pagelink = FPDFLink_LoadWebLinks(textpage);
  EXPECT_TRUE(pagelink);

  EXPECT_EQ(2, FPDFLink_CountWebLinks(pagelink));
  unsigned short buffer[128] = {0};
  static const char kExpectedUrl[] =
      "http://tutorial45.com/learn-autocad-basics-day-166/";
  static const int kUrlSize = static_cast<int>(sizeof(kExpectedUrl));

  EXPECT_EQ(kUrlSize, FPDFLink_GetURL(pagelink, 1, nullptr, 0));
  EXPECT_EQ(kUrlSize, FPDFLink_GetURL(pagelink, 1, buffer, std::size(buffer)));
  EXPECT_TRUE(check_unsigned_shorts(kExpectedUrl, buffer, kUrlSize));

  FPDFLink_CloseWebLinks(pagelink);
  FPDFText_ClosePage(textpage);
  UnloadPage(page);
}

TEST_F(FPDFTextEmbedderTest, WebLinksCharRanges) {
  ASSERT_TRUE(OpenDocument("weblinks.pdf"));
  FPDF_PAGE page = LoadPage(0);
  ASSERT_TRUE(page);

  FPDF_TEXTPAGE text_page = FPDFText_LoadPage(page);
  ASSERT_TRUE(text_page);

  FPDF_PAGELINK page_link = FPDFLink_LoadWebLinks(text_page);
  EXPECT_TRUE(page_link);

  // Test for char indices of a valid link
  int start_char_index;
  int char_count;
  ASSERT_TRUE(
      FPDFLink_GetTextRange(page_link, 0, &start_char_index, &char_count));
  EXPECT_EQ(35, start_char_index);
  EXPECT_EQ(24, char_count);

  // Test for char indices of an invalid link
  start_char_index = -10;
  char_count = -8;
  ASSERT_FALSE(
      FPDFLink_GetTextRange(page_link, 6, &start_char_index, &char_count));
  EXPECT_EQ(start_char_index, -10);
  EXPECT_EQ(char_count, -8);

  // Test for pagelink = nullptr
  start_char_index = -10;
  char_count = -8;
  ASSERT_FALSE(
      FPDFLink_GetTextRange(nullptr, 0, &start_char_index, &char_count));
  EXPECT_EQ(start_char_index, -10);
  EXPECT_EQ(char_count, -8);

  // Test for link_index < 0
  start_char_index = -10;
  char_count = -8;
  ASSERT_FALSE(
      FPDFLink_GetTextRange(page_link, -4, &start_char_index, &char_count));
  EXPECT_EQ(start_char_index, -10);
  EXPECT_EQ(char_count, -8);

  FPDFLink_CloseWebLinks(page_link);
  FPDFText_ClosePage(text_page);
  UnloadPage(page);
}

TEST_F(FPDFTextEmbedderTest, AnnotLinks) {
  ASSERT_TRUE(OpenDocument("annots.pdf"));
  FPDF_PAGE page = LoadPage(0);
  ASSERT_TRUE(page);

  // Get link count via checking annotation subtype
  int annot_count = FPDFPage_GetAnnotCount(page);
  ASSERT_EQ(9, annot_count);
  int annot_subtype_link_count = 0;
  for (int i = 0; i < annot_count; ++i) {
    ScopedFPDFAnnotation annot(FPDFPage_GetAnnot(page, i));
    if (FPDFAnnot_GetSubtype(annot.get()) == FPDF_ANNOT_LINK) {
      ++annot_subtype_link_count;
    }
  }
  EXPECT_EQ(4, annot_subtype_link_count);

  // Validate that FPDFLink_Enumerate() returns same number of links
  int start_pos = 0;
  FPDF_LINK link_annot;
  int link_count = 0;
  while (FPDFLink_Enumerate(page, &start_pos, &link_annot)) {
    ASSERT_TRUE(link_annot);
    if (start_pos == 1 || start_pos == 2) {
      // First two links point to first and second page within the document
      // respectively
      FPDF_DEST link_dest = FPDFLink_GetDest(document(), link_annot);
      EXPECT_TRUE(link_dest);
      EXPECT_EQ(start_pos - 1,
                FPDFDest_GetDestPageIndex(document(), link_dest));
    } else if (start_pos == 3) {  // points to PDF Spec URL
      FS_RECTF link_rect;
      EXPECT_TRUE(FPDFLink_GetAnnotRect(link_annot, &link_rect));
      EXPECT_NEAR(66.0, link_rect.left, 0.001);
      EXPECT_NEAR(544.0, link_rect.top, 0.001);
      EXPECT_NEAR(196.0, link_rect.right, 0.001);
      EXPECT_NEAR(529.0, link_rect.bottom, 0.001);
    } else if (start_pos == 4) {  // this link has quad points
      int quad_point_count = FPDFLink_CountQuadPoints(link_annot);
      EXPECT_EQ(1, quad_point_count);
      FS_QUADPOINTSF quad_points;
      EXPECT_TRUE(FPDFLink_GetQuadPoints(link_annot, 0, &quad_points));
      EXPECT_NEAR(83.0, quad_points.x1, 0.001);
      EXPECT_NEAR(453.0, quad_points.y1, 0.001);
      EXPECT_NEAR(178.0, quad_points.x2, 0.001);
      EXPECT_NEAR(453.0, quad_points.y2, 0.001);
      EXPECT_NEAR(83.0, quad_points.x3, 0.001);
      EXPECT_NEAR(440.0, quad_points.y3, 0.001);
      EXPECT_NEAR(178.0, quad_points.x4, 0.001);
      EXPECT_NEAR(440.0, quad_points.y4, 0.001);
      // AnnotRect is same as quad points for this link
      FS_RECTF link_rect;
      EXPECT_TRUE(FPDFLink_GetAnnotRect(link_annot, &link_rect));
      EXPECT_NEAR(link_rect.left, quad_points.x1, 0.001);
      EXPECT_NEAR(link_rect.top, quad_points.y1, 0.001);
      EXPECT_NEAR(link_rect.right, quad_points.x4, 0.001);
      EXPECT_NEAR(link_rect.bottom, quad_points.y4, 0.001);
    }
    ++link_count;
  }
  EXPECT_EQ(annot_subtype_link_count, link_count);

  UnloadPage(page);
}

TEST_F(FPDFTextEmbedderTest, GetFontSize) {
  ASSERT_TRUE(OpenDocument("hello_world.pdf"));
  FPDF_PAGE page = LoadPage(0);
  ASSERT_TRUE(page);

  FPDF_TEXTPAGE textpage = FPDFText_LoadPage(page);
  ASSERT_TRUE(textpage);

  const double kExpectedFontsSizes[] = {12, 12, 12, 12, 12, 12, 12, 12, 12, 12,
                                        12, 12, 12, 1,  1,  16, 16, 16, 16, 16,
                                        16, 16, 16, 16, 16, 16, 16, 16, 16, 16};

  int count = FPDFText_CountChars(textpage);
  ASSERT_EQ(std::size(kExpectedFontsSizes), static_cast<size_t>(count));
  for (int i = 0; i < count; ++i)
    EXPECT_EQ(kExpectedFontsSizes[i], FPDFText_GetFontSize(textpage, i)) << i;

  FPDFText_ClosePage(textpage);
  UnloadPage(page);
}

TEST_F(FPDFTextEmbedderTest, GetFontInfo) {
  ASSERT_TRUE(OpenDocument("hello_world.pdf"));
  FPDF_PAGE page = LoadPage(0);
  ASSERT_TRUE(page);

  FPDF_TEXTPAGE textpage = FPDFText_LoadPage(page);
  ASSERT_TRUE(textpage);
  std::vector<char> font_name;
  size_t num_chars1 = strlen("Hello, world!");
  const char kExpectedFontName1[] = "Times-Roman";

  for (size_t i = 0; i < num_chars1; i++) {
    int flags = -1;
    unsigned long length =
        FPDFText_GetFontInfo(textpage, i, nullptr, 0, &flags);
    static constexpr unsigned long expected_length = sizeof(kExpectedFontName1);
    ASSERT_EQ(expected_length, length);
    EXPECT_EQ(FXFONT_NONSYMBOLIC, flags);
    font_name.resize(length);
    std::fill(font_name.begin(), font_name.end(), 'a');
    flags = -1;
    EXPECT_EQ(expected_length,
              FPDFText_GetFontInfo(textpage, i, font_name.data(),
                                   font_name.size(), &flags));
    EXPECT_STREQ(kExpectedFontName1, font_name.data());
    EXPECT_EQ(FXFONT_NONSYMBOLIC, flags);
  }
  // If the size of the buffer is not large enough, the buffer should remain
  // unchanged.
  font_name.pop_back();
  std::fill(font_name.begin(), font_name.end(), 'a');
  EXPECT_EQ(sizeof(kExpectedFontName1),
            FPDFText_GetFontInfo(textpage, 0, font_name.data(),
                                 font_name.size(), nullptr));
  for (char a : font_name)
    EXPECT_EQ('a', a);

  // The text is "Hello, world!\r\nGoodbye, world!", so the next two characters
  // do not have any font information.
  EXPECT_EQ(0u, FPDFText_GetFontInfo(textpage, num_chars1, font_name.data(),
                                     font_name.size(), nullptr));
  EXPECT_EQ(0u, FPDFText_GetFontInfo(textpage, num_chars1 + 1, font_name.data(),
                                     font_name.size(), nullptr));

  size_t num_chars2 = strlen("Goodbye, world!");
  const char kExpectedFontName2[] = "Helvetica";
  for (size_t i = num_chars1 + 2; i < num_chars1 + num_chars2 + 2; i++) {
    int flags = -1;
    unsigned long length =
        FPDFText_GetFontInfo(textpage, i, nullptr, 0, &flags);
    static constexpr unsigned long expected_length = sizeof(kExpectedFontName2);
    ASSERT_EQ(expected_length, length);
    EXPECT_EQ(FXFONT_NONSYMBOLIC, flags);
    font_name.resize(length);
    std::fill(font_name.begin(), font_name.end(), 'a');
    flags = -1;
    EXPECT_EQ(expected_length,
              FPDFText_GetFontInfo(textpage, i, font_name.data(),
                                   font_name.size(), &flags));
    EXPECT_STREQ(kExpectedFontName2, font_name.data());
    EXPECT_EQ(FXFONT_NONSYMBOLIC, flags);
  }

  // Now try some out of bounds indices and null pointers to make sure we do not
  // crash.
  // No textpage.
  EXPECT_EQ(0u, FPDFText_GetFontInfo(nullptr, 0, font_name.data(),
                                     font_name.size(), nullptr));
  // No buffer.
  EXPECT_EQ(sizeof(kExpectedFontName1),
            FPDFText_GetFontInfo(textpage, 0, nullptr, 0, nullptr));
  // Negative index.
  EXPECT_EQ(0u, FPDFText_GetFontInfo(textpage, -1, font_name.data(),
                                     font_name.size(), nullptr));
  // Out of bounds index.
  EXPECT_EQ(0u, FPDFText_GetFontInfo(textpage, 1000, font_name.data(),
                                     font_name.size(), nullptr));

  FPDFText_ClosePage(textpage);
  UnloadPage(page);
}

TEST_F(FPDFTextEmbedderTest, ToUnicode) {
  ASSERT_TRUE(OpenDocument("bug_583.pdf"));
  FPDF_PAGE page = LoadPage(0);
  ASSERT_TRUE(page);

  FPDF_TEXTPAGE textpage = FPDFText_LoadPage(page);
  ASSERT_TRUE(textpage);

  ASSERT_EQ(1, FPDFText_CountChars(textpage));
  EXPECT_EQ(0U, FPDFText_GetUnicode(textpage, 0));

  FPDFText_ClosePage(textpage);
  UnloadPage(page);
}

TEST_F(FPDFTextEmbedderTest, IsGenerated) {
  ASSERT_TRUE(OpenDocument("hello_world.pdf"));
  FPDF_PAGE page = LoadPage(0);
  ASSERT_TRUE(page);

  {
    ScopedFPDFTextPage textpage(FPDFText_LoadPage(page));
    ASSERT_TRUE(textpage);

    EXPECT_EQ(static_cast<unsigned int>('H'),
              FPDFText_GetUnicode(textpage.get(), 0));
    EXPECT_EQ(0, FPDFText_IsGenerated(textpage.get(), 0));
    EXPECT_EQ(static_cast<unsigned int>(' '),
              FPDFText_GetUnicode(textpage.get(), 6));
    EXPECT_EQ(0, FPDFText_IsGenerated(textpage.get(), 6));

    EXPECT_EQ(static_cast<unsigned int>('\r'),
              FPDFText_GetUnicode(textpage.get(), 13));
    EXPECT_EQ(1, FPDFText_IsGenerated(textpage.get(), 13));
    EXPECT_EQ(static_cast<unsigned int>('\n'),
              FPDFText_GetUnicode(textpage.get(), 14));
    EXPECT_EQ(1, FPDFText_IsGenerated(textpage.get(), 14));

    EXPECT_EQ(-1, FPDFText_IsGenerated(textpage.get(), -1));
    EXPECT_EQ(-1, FPDFText_IsGenerated(textpage.get(), kHelloGoodbyeTextSize));
    EXPECT_EQ(-1, FPDFText_IsGenerated(nullptr, 6));
  }

  UnloadPage(page);
}

TEST_F(FPDFTextEmbedderTest, IsHyphen) {
  ASSERT_TRUE(OpenDocument("bug_781804.pdf"));
  FPDF_PAGE page = LoadPage(0);
  ASSERT_TRUE(page);

  {
    ScopedFPDFTextPage textpage(FPDFText_LoadPage(page));
    ASSERT_TRUE(textpage);

    EXPECT_EQ(static_cast<unsigned int>('V'),
              FPDFText_GetUnicode(textpage.get(), 0));
    EXPECT_EQ(0, FPDFText_IsHyphen(textpage.get(), 0));
    EXPECT_EQ(static_cast<unsigned int>('\2'),
              FPDFText_GetUnicode(textpage.get(), 6));
    EXPECT_EQ(1, FPDFText_IsHyphen(textpage.get(), 6));

    EXPECT_EQ(static_cast<unsigned int>('U'),
              FPDFText_GetUnicode(textpage.get(), 14));
    EXPECT_EQ(0, FPDFText_IsHyphen(textpage.get(), 14));
    EXPECT_EQ(static_cast<unsigned int>(L'\u2010'),
              FPDFText_GetUnicode(textpage.get(), 18));
    EXPECT_EQ(0, FPDFText_IsHyphen(textpage.get(), 18));

    EXPECT_EQ(-1, FPDFText_IsHyphen(textpage.get(), -1));
    EXPECT_EQ(-1, FPDFText_IsHyphen(textpage.get(), 1000));
    EXPECT_EQ(-1, FPDFText_IsHyphen(nullptr, 6));
  }

  UnloadPage(page);
}

TEST_F(FPDFTextEmbedderTest, IsInvalidUnicode) {
  ASSERT_TRUE(OpenDocument("bug_1388_2.pdf"));
  FPDF_PAGE page = LoadPage(0);
  ASSERT_TRUE(page);

  {
    constexpr int kExpectedCharCount = 5;
    ScopedFPDFTextPage textpage(FPDFText_LoadPage(page));
    ASSERT_TRUE(textpage);
    EXPECT_EQ(kExpectedCharCount, FPDFText_CountChars(textpage.get()));

    EXPECT_EQ(static_cast<unsigned int>('X'),
              FPDFText_GetUnicode(textpage.get(), 0));
    EXPECT_EQ(0, FPDFText_HasUnicodeMapError(textpage.get(), 0));
    EXPECT_EQ(static_cast<unsigned int>(' '),
              FPDFText_GetUnicode(textpage.get(), 1));
    EXPECT_EQ(0, FPDFText_HasUnicodeMapError(textpage.get(), 1));

    EXPECT_EQ(31u, FPDFText_GetUnicode(textpage.get(), 2));
    EXPECT_EQ(1, FPDFText_HasUnicodeMapError(textpage.get(), 2));

    EXPECT_EQ(-1, FPDFText_HasUnicodeMapError(textpage.get(), -1));
    EXPECT_EQ(-1,
              FPDFText_HasUnicodeMapError(textpage.get(), kExpectedCharCount));
    EXPECT_EQ(-1, FPDFText_HasUnicodeMapError(nullptr, 0));
  }

  UnloadPage(page);
}

TEST_F(FPDFTextEmbedderTest, Bug_921) {
  ASSERT_TRUE(OpenDocument("bug_921.pdf"));
  FPDF_PAGE page = LoadPage(0);
  ASSERT_TRUE(page);

  FPDF_TEXTPAGE textpage = FPDFText_LoadPage(page);
  ASSERT_TRUE(textpage);

  static constexpr unsigned int kData[] = {
      1095, 1077, 1083, 1086, 1074, 1077, 1095, 1077, 1089, 1082, 1086, 1077,
      32,   1089, 1090, 1088, 1072, 1076, 1072, 1085, 1080, 1077, 46,   32};
  static constexpr int kStartIndex = 238;

  ASSERT_EQ(268, FPDFText_CountChars(textpage));
  for (size_t i = 0; i < std::size(kData); ++i)
    EXPECT_EQ(kData[i], FPDFText_GetUnicode(textpage, kStartIndex + i));

  unsigned short buffer[std::size(kData) + 1];
  fxcrt::Fill(buffer, 0xbdbd);
  int count = FPDFText_GetText(textpage, kStartIndex, std::size(kData), buffer);
  ASSERT_GT(count, 0);
  ASSERT_EQ(std::size(kData) + 1, static_cast<size_t>(count));
  for (size_t i = 0; i < std::size(kData); ++i)
    EXPECT_EQ(kData[i], buffer[i]);
  EXPECT_EQ(0, buffer[std::size(kData)]);

  FPDFText_ClosePage(textpage);
  UnloadPage(page);
}

TEST_F(FPDFTextEmbedderTest, GetTextWithHyphen) {
  ASSERT_TRUE(OpenDocument("bug_781804.pdf"));
  FPDF_PAGE page = LoadPage(0);
  ASSERT_TRUE(page);

  FPDF_TEXTPAGE textpage = FPDFText_LoadPage(page);
  ASSERT_TRUE(textpage);

  // Check that soft hyphens are not included
  // Expecting 'Veritaserum', except there is a \uFFFE where the hyphen was in
  // the original text. This is a weird thing that Adobe does, which we
  // replicate.
  constexpr unsigned short soft_expected[] = {
      0x0056, 0x0065, 0x0072, 0x0069, 0x0074, 0x0061, 0xfffe,
      0x0073, 0x0065, 0x0072, 0x0075, 0x006D, 0x0000};
  {
    constexpr int count = std::size(soft_expected) - 1;
    unsigned short buffer[std::size(soft_expected)] = {};
    EXPECT_EQ(count + 1, FPDFText_GetText(textpage, 0, count, buffer));
    for (int i = 0; i < count; i++)
      EXPECT_EQ(soft_expected[i], buffer[i]);
  }

  // Check that hard hyphens are included
  {
    // There isn't the \0 in the actual doc, but there is a \r\n, so need to
    // add 1 to get aligned.
    constexpr size_t offset = std::size(soft_expected) + 1;
    // Expecting 'User-\r\ngenerated', the - is a unicode character, so cannot
    // store in a char[].
    constexpr unsigned short hard_expected[] = {
        0x0055, 0x0073, 0x0065, 0x0072, 0x2010, 0x000d, 0x000a, 0x0067, 0x0065,
        0x006e, 0x0065, 0x0072, 0x0061, 0x0074, 0x0065, 0x0064, 0x0000};
    constexpr int count = std::size(hard_expected) - 1;
    unsigned short buffer[std::size(hard_expected)];

    EXPECT_EQ(count + 1, FPDFText_GetText(textpage, offset, count, buffer));
    for (int i = 0; i < count; i++)
      EXPECT_EQ(hard_expected[i], buffer[i]);
  }

  FPDFText_ClosePage(textpage);
  UnloadPage(page);
}

TEST_F(FPDFTextEmbedderTest, bug_782596) {
  // If there is a regression in this test, it will only fail under ASAN
  ASSERT_TRUE(OpenDocument("bug_782596.pdf"));
  FPDF_PAGE page = LoadPage(0);
  ASSERT_TRUE(page);
  FPDF_TEXTPAGE textpage = FPDFText_LoadPage(page);
  ASSERT_TRUE(textpage);
  FPDFText_ClosePage(textpage);
  UnloadPage(page);
}

TEST_F(FPDFTextEmbedderTest, ControlCharacters) {
  ASSERT_TRUE(OpenDocument("control_characters.pdf"));
  FPDF_PAGE page = LoadPage(0);
  ASSERT_TRUE(page);

  FPDF_TEXTPAGE textpage = FPDFText_LoadPage(page);
  ASSERT_TRUE(textpage);

  // Should not include the control characters in the output
  unsigned short buffer[128];
  fxcrt::Fill(buffer, 0xbdbd);
  int num_chars = FPDFText_GetText(textpage, 0, 128, buffer);
  ASSERT_EQ(kHelloGoodbyeTextSize, num_chars);
  EXPECT_TRUE(
      check_unsigned_shorts(kHelloGoodbyeText, buffer, kHelloGoodbyeTextSize));

  // Attempting to get a chunk of text after the control characters
  static const char expected_substring[] = "Goodbye, world!";
  // Offset is the length of 'Hello, world!\r\n' + 2 control characters in the
  // original stream
  static const int offset = 17;
  fxcrt::Fill(buffer, 0xbdbd);
  num_chars = FPDFText_GetText(textpage, offset, 128, buffer);

  ASSERT_GE(num_chars, 0);
  EXPECT_EQ(sizeof(expected_substring), static_cast<size_t>(num_chars));
  EXPECT_TRUE(check_unsigned_shorts(expected_substring, buffer,
                                    sizeof(expected_substring)));

  FPDFText_ClosePage(textpage);
  UnloadPage(page);
}

// Testing that hyphen makers (0x0002) are replacing hard hyphens when
// the word contains non-ASCII characters.
TEST_F(FPDFTextEmbedderTest, bug_1029) {
  ASSERT_TRUE(OpenDocument("bug_1029.pdf"));
  FPDF_PAGE page = LoadPage(0);
  ASSERT_TRUE(page);

  FPDF_TEXTPAGE textpage = FPDFText_LoadPage(page);
  ASSERT_TRUE(textpage);

  constexpr int page_range_offset = 171;
  constexpr int page_range_length = 56;

  // This text is:
  // 'METADATA table. When the split has committed, it noti' followed
  // by a 'soft hyphen' (0x0002) and then 'fi'.
  //
  // The original text has a fi ligature, but that is broken up into
  // two characters when the PDF is processed.
  constexpr unsigned int expected[] = {
      0x004d, 0x0045, 0x0054, 0x0041, 0x0044, 0x0041, 0x0054, 0x0041,
      0x0020, 0x0074, 0x0061, 0x0062, 0x006c, 0x0065, 0x002e, 0x0020,
      0x0057, 0x0068, 0x0065, 0x006e, 0x0020, 0x0074, 0x0068, 0x0065,
      0x0020, 0x0073, 0x0070, 0x006c, 0x0069, 0x0074, 0x0020, 0x0068,
      0x0061, 0x0073, 0x0020, 0x0063, 0x006f, 0x006d, 0x006d, 0x0069,
      0x0074, 0x0074, 0x0065, 0x0064, 0x002c, 0x0020, 0x0069, 0x0074,
      0x0020, 0x006e, 0x006f, 0x0074, 0x0069, 0x0002, 0x0066, 0x0069};
  static_assert(page_range_length == std::size(expected),
                "Expected should be the same size as the range being "
                "extracted from page.");
  EXPECT_LT(page_range_offset + page_range_length,
            FPDFText_CountChars(textpage));

  for (int i = 0; i < page_range_length; ++i) {
    EXPECT_EQ(expected[i],
              FPDFText_GetUnicode(textpage, page_range_offset + i));
  }

  FPDFText_ClosePage(textpage);
  UnloadPage(page);
}

TEST_F(FPDFTextEmbedderTest, CountRects) {
  ASSERT_TRUE(OpenDocument("hello_world.pdf"));
  FPDF_PAGE page = LoadPage(0);
  ASSERT_TRUE(page);

  FPDF_TEXTPAGE textpage = FPDFText_LoadPage(page);
  ASSERT_TRUE(textpage);

  // Sanity check hello_world.pdf.
  // |num_chars| check includes the terminating NUL that is provided.
  {
    unsigned short buffer[128];
    int num_chars = FPDFText_GetText(textpage, 0, 128, buffer);
    ASSERT_EQ(kHelloGoodbyeTextSize, num_chars);
    EXPECT_TRUE(check_unsigned_shorts(kHelloGoodbyeText, buffer,
                                      kHelloGoodbyeTextSize));
  }

  // Now test FPDFText_CountRects().
  static const int kHelloWorldEnd = strlen("Hello, world!");
  static const int kGoodbyeWorldStart = kHelloWorldEnd + 2;  // "\r\n"
  for (int start = 0; start < kHelloWorldEnd; ++start) {
    // Always grab some part of "hello world" and some part of "goodbye world"
    // Since -1 means "all".
    EXPECT_EQ(2, FPDFText_CountRects(textpage, start, -1));

    // No characters always means 0 rects.
    EXPECT_EQ(0, FPDFText_CountRects(textpage, start, 0));

    // 1 character stays within "hello world"
    EXPECT_EQ(1, FPDFText_CountRects(textpage, start, 1));

    // When |start| is 0, Having |kGoodbyeWorldStart| char count does not reach
    // "goodbye world".
    int expected_value = start ? 2 : 1;
    EXPECT_EQ(expected_value,
              FPDFText_CountRects(textpage, start, kGoodbyeWorldStart));

    // Extremely large character count will always return 2 rects because
    // |start| starts inside "hello world".
    EXPECT_EQ(2, FPDFText_CountRects(textpage, start, 500));
  }

  // Now test negative counts.
  for (int start = 0; start < kHelloWorldEnd; ++start) {
    EXPECT_EQ(2, FPDFText_CountRects(textpage, start, -100));
    EXPECT_EQ(2, FPDFText_CountRects(textpage, start, -2));
  }

  // Now test larger start values.
  const int kExpectedLength = strlen(kHelloGoodbyeText);
  for (int start = kGoodbyeWorldStart + 1; start < kExpectedLength; ++start) {
    EXPECT_EQ(1, FPDFText_CountRects(textpage, start, -1));
    EXPECT_EQ(0, FPDFText_CountRects(textpage, start, 0));
    EXPECT_EQ(1, FPDFText_CountRects(textpage, start, 1));
    EXPECT_EQ(1, FPDFText_CountRects(textpage, start, 2));
    EXPECT_EQ(1, FPDFText_CountRects(textpage, start, 500));
  }

  // Now test start values that starts beyond the end of the text.
  for (int start = kExpectedLength; start < 100; ++start) {
    EXPECT_EQ(0, FPDFText_CountRects(textpage, start, -1));
    EXPECT_EQ(0, FPDFText_CountRects(textpage, start, 0));
    EXPECT_EQ(0, FPDFText_CountRects(textpage, start, 1));
    EXPECT_EQ(0, FPDFText_CountRects(textpage, start, 2));
    EXPECT_EQ(0, FPDFText_CountRects(textpage, start, 500));
  }

  FPDFText_ClosePage(textpage);
  UnloadPage(page);
}

TEST_F(FPDFTextEmbedderTest, GetText) {
  ASSERT_TRUE(OpenDocument("hello_world.pdf"));
  FPDF_PAGE page = LoadPage(0);
  ASSERT_TRUE(page);

  FPDF_TEXTPAGE text_page = FPDFText_LoadPage(page);
  ASSERT_TRUE(text_page);

  EXPECT_EQ(2, FPDFPage_CountObjects(page));
  FPDF_PAGEOBJECT text_object = FPDFPage_GetObject(page, 0);
  ASSERT_TRUE(text_object);

  // Positive testing.
  constexpr char kHelloText[] = "Hello, world!";
  // Return value includes the terminating NUL that is provided.
  constexpr unsigned long kHelloUTF16Size = std::size(kHelloText) * 2;
  constexpr wchar_t kHelloWideText[] = L"Hello, world!";
  unsigned long size = FPDFTextObj_GetText(text_object, text_page, nullptr, 0);
  ASSERT_EQ(kHelloUTF16Size, size);

  std::vector<unsigned short> buffer(size);
  ASSERT_EQ(size,
            FPDFTextObj_GetText(text_object, text_page, buffer.data(), size));
  ASSERT_EQ(kHelloWideText, GetPlatformWString(buffer.data()));

  // Negative testing.
  ASSERT_EQ(0U, FPDFTextObj_GetText(nullptr, text_page, nullptr, 0));
  ASSERT_EQ(0U, FPDFTextObj_GetText(text_object, nullptr, nullptr, 0));
  ASSERT_EQ(0U, FPDFTextObj_GetText(nullptr, nullptr, nullptr, 0));

  // Buffer is too small, ensure it's not modified.
  buffer.resize(2);
  buffer[0] = 'x';
  buffer[1] = '\0';
  size =
      FPDFTextObj_GetText(text_object, text_page, buffer.data(), buffer.size());
  ASSERT_EQ(kHelloUTF16Size, size);
  ASSERT_EQ('x', buffer[0]);
  ASSERT_EQ('\0', buffer[1]);

  FPDFText_ClosePage(text_page);
  UnloadPage(page);
}

TEST_F(FPDFTextEmbedderTest, CroppedText) {
  static constexpr int kPageCount = 4;
  static constexpr FS_RECTF kBoxes[kPageCount] = {
      {50.0f, 150.0f, 150.0f, 50.0f},
      {50.0f, 150.0f, 150.0f, 50.0f},
      {60.0f, 150.0f, 150.0f, 60.0f},
      {60.0f, 150.0f, 150.0f, 60.0f},
  };
  static constexpr const char* kExpectedText[kPageCount] = {
      " world!\r\ndbye, world!",
      " world!\r\ndbye, world!",
      "bye, world!",
      "bye, world!",
  };

  ASSERT_TRUE(OpenDocument("cropped_text.pdf"));
  ASSERT_EQ(kPageCount, FPDF_GetPageCount(document()));

  for (int i = 0; i < kPageCount; ++i) {
    FPDF_PAGE page = LoadPage(i);
    ASSERT_TRUE(page);

    FS_RECTF box;
    EXPECT_TRUE(FPDF_GetPageBoundingBox(page, &box));
    EXPECT_EQ(kBoxes[i].left, box.left);
    EXPECT_EQ(kBoxes[i].top, box.top);
    EXPECT_EQ(kBoxes[i].right, box.right);
    EXPECT_EQ(kBoxes[i].bottom, box.bottom);

    {
      ScopedFPDFTextPage textpage(FPDFText_LoadPage(page));
      ASSERT_TRUE(textpage);

      unsigned short buffer[128];
      fxcrt::Fill(buffer, 0xbdbd);
      int num_chars = FPDFText_GetText(textpage.get(), 0, 128, buffer);
      ASSERT_EQ(kHelloGoodbyeTextSize, num_chars);
      EXPECT_TRUE(check_unsigned_shorts(kHelloGoodbyeText, buffer,
                                        kHelloGoodbyeTextSize));

      int expected_char_count = strlen(kExpectedText[i]);
      ASSERT_EQ(expected_char_count,
                FPDFText_GetBoundedText(textpage.get(), box.left, box.top,
                                        box.right, box.bottom, nullptr, 0));

      fxcrt::Fill(buffer, 0xbdbd);
      ASSERT_EQ(expected_char_count + 1,
                FPDFText_GetBoundedText(textpage.get(), box.left, box.top,
                                        box.right, box.bottom, buffer, 128));
      EXPECT_TRUE(
          check_unsigned_shorts(kExpectedText[i], buffer, expected_char_count));
    }

    UnloadPage(page);
  }
}

TEST_F(FPDFTextEmbedderTest, Bug_1139) {
  ASSERT_TRUE(OpenDocument("bug_1139.pdf"));
  FPDF_PAGE page = LoadPage(0);
  ASSERT_TRUE(page);

  FPDF_TEXTPAGE text_page = FPDFText_LoadPage(page);
  ASSERT_TRUE(text_page);

  // -1 for CountChars not including the \0, but +1 for the extra control
  // character.
  EXPECT_EQ(kHelloGoodbyeTextSize, FPDFText_CountChars(text_page));

  // There is an extra control character at the beginning of the string, but it
  // should not appear in the output nor prevent extracting the text.
  unsigned short buffer[128];
  int num_chars = FPDFText_GetText(text_page, 0, 128, buffer);
  ASSERT_EQ(kHelloGoodbyeTextSize, num_chars);
  EXPECT_TRUE(
      check_unsigned_shorts(kHelloGoodbyeText, buffer, kHelloGoodbyeTextSize));
  FPDFText_ClosePage(text_page);
  UnloadPage(page);
}

TEST_F(FPDFTextEmbedderTest, Bug_642) {
  ASSERT_TRUE(OpenDocument("bug_642.pdf"));
  FPDF_PAGE page = LoadPage(0);
  ASSERT_TRUE(page);
  {
    ScopedFPDFTextPage text_page(FPDFText_LoadPage(page));
    ASSERT_TRUE(text_page);

    constexpr char kText[] = "ABCD";
    constexpr size_t kTextSize = std::size(kText);
    // -1 for CountChars not including the \0
    EXPECT_EQ(static_cast<int>(kTextSize) - 1,
              FPDFText_CountChars(text_page.get()));

    unsigned short buffer[kTextSize];
    int num_chars =
        FPDFText_GetText(text_page.get(), 0, std::size(buffer) - 1, buffer);
    ASSERT_EQ(static_cast<int>(kTextSize), num_chars);
    EXPECT_TRUE(check_unsigned_shorts(kText, buffer, kTextSize));
  }

  UnloadPage(page);
}

TEST_F(FPDFTextEmbedderTest, GetCharAngle) {
  ASSERT_TRUE(OpenDocument("rotated_text.pdf"));
  FPDF_PAGE page = LoadPage(0);
  ASSERT_TRUE(page);

  FPDF_TEXTPAGE text_page = FPDFText_LoadPage(page);
  ASSERT_TRUE(text_page);

  static constexpr int kSubstringsSize[] = {
      std::size("Hello,"), std::size(" world!\r\n"), std::size("Goodbye,")};

  // -1 for CountChars not including the \0, but +1 for the extra control
  // character.
  EXPECT_EQ(kHelloGoodbyeTextSize, FPDFText_CountChars(text_page));

  EXPECT_FLOAT_EQ(-1.0f, FPDFText_GetCharAngle(nullptr, 0));
  EXPECT_FLOAT_EQ(-1.0f, FPDFText_GetCharAngle(text_page, -1));
  EXPECT_FLOAT_EQ(-1.0f,
                  FPDFText_GetCharAngle(text_page, kHelloGoodbyeTextSize + 1));

  // Test GetCharAngle for every quadrant
  EXPECT_NEAR(FXSYS_PI / 4.0, FPDFText_GetCharAngle(text_page, 0), 0.001);
  EXPECT_NEAR(3 * FXSYS_PI / 4.0,
              FPDFText_GetCharAngle(text_page, kSubstringsSize[0]), 0.001);
  EXPECT_NEAR(
      5 * FXSYS_PI / 4.0,
      FPDFText_GetCharAngle(text_page, kSubstringsSize[0] + kSubstringsSize[1]),
      0.001);
  EXPECT_NEAR(
      7 * FXSYS_PI / 4.0,
      FPDFText_GetCharAngle(text_page, kSubstringsSize[0] + kSubstringsSize[1] +
                                           kSubstringsSize[2]),
      0.001);

  FPDFText_ClosePage(text_page);
  UnloadPage(page);
}

TEST_F(FPDFTextEmbedderTest, GetFontWeight) {
  ASSERT_TRUE(OpenDocument("font_weight.pdf"));
  FPDF_PAGE page = LoadPage(0);
  ASSERT_TRUE(page);

  FPDF_TEXTPAGE text_page = FPDFText_LoadPage(page);
  ASSERT_TRUE(text_page);

  EXPECT_EQ(2, FPDFText_CountChars(text_page));

  EXPECT_EQ(-1, FPDFText_GetFontWeight(nullptr, 0));
  EXPECT_EQ(-1, FPDFText_GetFontWeight(text_page, -1));
  EXPECT_EQ(-1, FPDFText_GetFontWeight(text_page, 314));

  // The font used for this text only specifies /StemV (80); the weight value
  // that is returned should be calculated from that (80*5 == 400).
  EXPECT_EQ(400, FPDFText_GetFontWeight(text_page, 0));

  // Using a /StemV value of 82, the estimate comes out to 410, even though
  // /FontWeight is 400.
  // TODO(crbug.com/pdfium/1420): Fix this the return value here.
  EXPECT_EQ(410, FPDFText_GetFontWeight(text_page, 1));

  FPDFText_ClosePage(text_page);
  UnloadPage(page);
}

TEST_F(FPDFTextEmbedderTest, GetTextRenderMode) {
  ASSERT_TRUE(OpenDocument("text_render_mode.pdf"));
  FPDF_PAGE page = LoadPage(0);
  ASSERT_TRUE(page);

  FPDF_TEXTPAGE text_page = FPDFText_LoadPage(page);
  ASSERT_TRUE(text_page);

  ASSERT_EQ(12, FPDFText_CountChars(text_page));

  ASSERT_EQ(FPDF_TEXTRENDERMODE_UNKNOWN,
            FPDFText_GetTextRenderMode(nullptr, 0));
  ASSERT_EQ(FPDF_TEXTRENDERMODE_UNKNOWN,
            FPDFText_GetTextRenderMode(text_page, -1));
  ASSERT_EQ(FPDF_TEXTRENDERMODE_UNKNOWN,
            FPDFText_GetTextRenderMode(text_page, 314));

  ASSERT_EQ(FPDF_TEXTRENDERMODE_FILL, FPDFText_GetTextRenderMode(text_page, 0));

  ASSERT_EQ(FPDF_TEXTRENDERMODE_STROKE,
            FPDFText_GetTextRenderMode(text_page, 7));

  FPDFText_ClosePage(text_page);
  UnloadPage(page);
}

TEST_F(FPDFTextEmbedderTest, GetFillColor) {
  ASSERT_TRUE(OpenDocument("text_color.pdf"));
  FPDF_PAGE page = LoadPage(0);
  ASSERT_TRUE(page);

  FPDF_TEXTPAGE text_page = FPDFText_LoadPage(page);
  ASSERT_TRUE(text_page);

  ASSERT_EQ(1, FPDFText_CountChars(text_page));

  ASSERT_FALSE(
      FPDFText_GetFillColor(nullptr, 0, nullptr, nullptr, nullptr, nullptr));
  ASSERT_FALSE(
      FPDFText_GetFillColor(text_page, -1, nullptr, nullptr, nullptr, nullptr));
  ASSERT_FALSE(FPDFText_GetFillColor(text_page, 314, nullptr, nullptr, nullptr,
                                     nullptr));
  ASSERT_FALSE(
      FPDFText_GetFillColor(text_page, 0, nullptr, nullptr, nullptr, nullptr));

  unsigned int r;
  unsigned int g;
  unsigned int b;
  unsigned int a;
  ASSERT_TRUE(FPDFText_GetFillColor(text_page, 0, &r, &g, &b, &a));
  ASSERT_EQ(0xffu, r);
  ASSERT_EQ(0u, g);
  ASSERT_EQ(0u, b);
  ASSERT_EQ(0xffu, a);

  FPDFText_ClosePage(text_page);
  UnloadPage(page);
}

TEST_F(FPDFTextEmbedderTest, GetStrokeColor) {
  ASSERT_TRUE(OpenDocument("text_color.pdf"));
  FPDF_PAGE page = LoadPage(0);
  ASSERT_TRUE(page);

  FPDF_TEXTPAGE text_page = FPDFText_LoadPage(page);
  ASSERT_TRUE(text_page);

  ASSERT_EQ(1, FPDFText_CountChars(text_page));

  ASSERT_FALSE(
      FPDFText_GetStrokeColor(nullptr, 0, nullptr, nullptr, nullptr, nullptr));
  ASSERT_FALSE(FPDFText_GetStrokeColor(text_page, -1, nullptr, nullptr, nullptr,
                                       nullptr));
  ASSERT_FALSE(FPDFText_GetStrokeColor(text_page, 314, nullptr, nullptr,
                                       nullptr, nullptr));
  ASSERT_FALSE(FPDFText_GetStrokeColor(text_page, 0, nullptr, nullptr, nullptr,
                                       nullptr));

  unsigned int r;
  unsigned int g;
  unsigned int b;
  unsigned int a;
  ASSERT_TRUE(FPDFText_GetStrokeColor(text_page, 0, &r, &g, &b, &a));
  ASSERT_EQ(0u, r);
  ASSERT_EQ(0xffu, g);
  ASSERT_EQ(0u, b);
  ASSERT_EQ(0xffu, a);

  FPDFText_ClosePage(text_page);
  UnloadPage(page);
}

TEST_F(FPDFTextEmbedderTest, GetMatrix) {
  constexpr char kExpectedText[] = "A1\r\nA2\r\nA3";
  constexpr size_t kExpectedTextSize = std::size(kExpectedText);
  constexpr FS_MATRIX kExpectedMatrices[] = {
      {12.0f, 0.0f, 0.0f, 10.0f, 66.0f, 90.0f},
      {12.0f, 0.0f, 0.0f, 10.0f, 66.0f, 90.0f},
      {1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f},
      {1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f},
      {12.0f, 0.0f, 0.0f, 10.0f, 38.0f, 60.0f},
      {12.0f, 0.0f, 0.0f, 10.0f, 38.0f, 60.0f},
      {1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f},
      {1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f},
      {1.0f, 0.0f, 0.0f, 0.833333, 60.0f, 130.0f},
      {1.0f, 0.0f, 0.0f, 0.833333, 60.0f, 130.0f},
  };
  constexpr size_t kExpectedCount = std::size(kExpectedMatrices);
  static_assert(kExpectedCount + 1 == kExpectedTextSize,
                "Bad expected matrix size");

  ASSERT_TRUE(OpenDocument("font_matrix.pdf"));
  FPDF_PAGE page = LoadPage(0);
  ASSERT_TRUE(page);

  {
    ScopedFPDFTextPage text_page(FPDFText_LoadPage(page));
    ASSERT_TRUE(text_page);
    ASSERT_EQ(static_cast<int>(kExpectedCount),
              FPDFText_CountChars(text_page.get()));

    {
      // Check the characters.
      unsigned short buffer[kExpectedTextSize];
      ASSERT_EQ(static_cast<int>(kExpectedTextSize),
                FPDFText_GetText(text_page.get(), 0, kExpectedCount, buffer));
      EXPECT_TRUE(
          check_unsigned_shorts(kExpectedText, buffer, kExpectedTextSize));
    }

    // Check the character matrix.
    FS_MATRIX matrix;
    for (size_t i = 0; i < kExpectedCount; ++i) {
      ASSERT_TRUE(FPDFText_GetMatrix(text_page.get(), i, &matrix)) << i;
      EXPECT_FLOAT_EQ(kExpectedMatrices[i].a, matrix.a) << i;
      EXPECT_FLOAT_EQ(kExpectedMatrices[i].b, matrix.b) << i;
      EXPECT_FLOAT_EQ(kExpectedMatrices[i].c, matrix.c) << i;
      EXPECT_FLOAT_EQ(kExpectedMatrices[i].d, matrix.d) << i;
      EXPECT_FLOAT_EQ(kExpectedMatrices[i].e, matrix.e) << i;
      EXPECT_FLOAT_EQ(kExpectedMatrices[i].f, matrix.f) << i;
    }

    // Check bad parameters.
    EXPECT_FALSE(FPDFText_GetMatrix(nullptr, 0, &matrix));
    EXPECT_FALSE(FPDFText_GetMatrix(text_page.get(), 10, &matrix));
    EXPECT_FALSE(FPDFText_GetMatrix(text_page.get(), -1, &matrix));
    EXPECT_FALSE(FPDFText_GetMatrix(text_page.get(), 0, nullptr));
  }

  UnloadPage(page);
}

TEST_F(FPDFTextEmbedderTest, CharBox) {
  // For a size 12 letter 'A'.
  constexpr double kExpectedCharWidth = 8.460;
  constexpr double kExpectedCharHeight = 6.600;
  constexpr float kExpectedLooseCharWidth = 8.664f;
  constexpr float kExpectedLooseCharHeight = 12.0f;

  ASSERT_TRUE(OpenDocument("font_matrix.pdf"));
  FPDF_PAGE page = LoadPage(0);
  ASSERT_TRUE(page);

  {
    ScopedFPDFTextPage text_page(FPDFText_LoadPage(page));
    ASSERT_TRUE(text_page);

    // Check the character box size.
    double left;
    double right;
    double bottom;
    double top;
    ASSERT_TRUE(
        FPDFText_GetCharBox(text_page.get(), 0, &left, &right, &bottom, &top));
    EXPECT_NEAR(kExpectedCharWidth, right - left, 0.001);
    EXPECT_NEAR(kExpectedCharHeight, top - bottom, 0.001);
    ASSERT_TRUE(
        FPDFText_GetCharBox(text_page.get(), 4, &left, &right, &bottom, &top));
    EXPECT_NEAR(kExpectedCharWidth, right - left, 0.001);
    EXPECT_NEAR(kExpectedCharHeight, top - bottom, 0.001);
    ASSERT_TRUE(
        FPDFText_GetCharBox(text_page.get(), 8, &left, &right, &bottom, &top));
    EXPECT_NEAR(kExpectedCharWidth, right - left, 0.001);
    EXPECT_NEAR(kExpectedCharHeight, top - bottom, 0.001);

    // Check the loose character box size.
    FS_RECTF rect;
    ASSERT_TRUE(FPDFText_GetLooseCharBox(text_page.get(), 0, &rect));
    EXPECT_FLOAT_EQ(kExpectedLooseCharWidth, rect.right - rect.left);
    EXPECT_FLOAT_EQ(kExpectedLooseCharHeight, rect.top - rect.bottom);
    ASSERT_TRUE(FPDFText_GetLooseCharBox(text_page.get(), 4, &rect));
    EXPECT_FLOAT_EQ(kExpectedLooseCharWidth, rect.right - rect.left);
    EXPECT_FLOAT_EQ(kExpectedLooseCharHeight, rect.top - rect.bottom);
    ASSERT_TRUE(FPDFText_GetLooseCharBox(text_page.get(), 8, &rect));
    EXPECT_FLOAT_EQ(kExpectedLooseCharWidth, rect.right - rect.left);
    EXPECT_NEAR(kExpectedLooseCharHeight, rect.top - rect.bottom, 0.00001);
  }

  UnloadPage(page);
}

TEST_F(FPDFTextEmbedderTest, SmallType3Glyph) {
  ASSERT_TRUE(OpenDocument("bug_1591.pdf"));
  FPDF_PAGE page = LoadPage(0);
  ASSERT_TRUE(page);

  {
    ScopedFPDFTextPage text_page(FPDFText_LoadPage(page));
    ASSERT_TRUE(text_page);
    ASSERT_EQ(5, FPDFText_CountChars(text_page.get()));

    EXPECT_EQ(49u, FPDFText_GetUnicode(text_page.get(), 0));
    EXPECT_EQ(32u, FPDFText_GetUnicode(text_page.get(), 1));
    EXPECT_EQ(50u, FPDFText_GetUnicode(text_page.get(), 2));
    EXPECT_EQ(32u, FPDFText_GetUnicode(text_page.get(), 3));
    EXPECT_EQ(49u, FPDFText_GetUnicode(text_page.get(), 4));

    // Check the character box size.
    double left;
    double right;
    double bottom;
    double top;
    ASSERT_TRUE(
        FPDFText_GetCharBox(text_page.get(), 0, &left, &right, &bottom, &top));
    EXPECT_DOUBLE_EQ(63.439998626708984, left);
    EXPECT_DOUBLE_EQ(65.360000610351562, right);
    EXPECT_DOUBLE_EQ(50.0, bottom);
    EXPECT_DOUBLE_EQ(61.520000457763672, top);
    ASSERT_TRUE(
        FPDFText_GetCharBox(text_page.get(), 1, &left, &right, &bottom, &top));
    EXPECT_DOUBLE_EQ(62.007999420166016, left);
    EXPECT_DOUBLE_EQ(62.007999420166016, right);
    EXPECT_DOUBLE_EQ(50.0, bottom);
    EXPECT_DOUBLE_EQ(50.0, top);
    ASSERT_TRUE(
        FPDFText_GetCharBox(text_page.get(), 2, &left, &right, &bottom, &top));
    EXPECT_DOUBLE_EQ(86.0, left);
    EXPECT_DOUBLE_EQ(88.400001525878906, right);
    EXPECT_DOUBLE_EQ(50.0, bottom);
    EXPECT_DOUBLE_EQ(50.240001678466797, top);
    ASSERT_TRUE(
        FPDFText_GetCharBox(text_page.get(), 3, &left, &right, &bottom, &top));
    EXPECT_DOUBLE_EQ(86.010002136230469, left);
    EXPECT_DOUBLE_EQ(86.010002136230469, right);
    EXPECT_DOUBLE_EQ(50.0, bottom);
    EXPECT_DOUBLE_EQ(50.0, top);
    ASSERT_TRUE(
        FPDFText_GetCharBox(text_page.get(), 4, &left, &right, &bottom, &top));
    EXPECT_DOUBLE_EQ(99.44000244140625, left);
    EXPECT_DOUBLE_EQ(101.36000061035156, right);
    EXPECT_DOUBLE_EQ(50.0, bottom);
    EXPECT_DOUBLE_EQ(61.520000457763672, top);
  }

  UnloadPage(page);
}

TEST_F(FPDFTextEmbedderTest, BigtableTextExtraction) {
  constexpr char kExpectedText[] =
      "{fay,jeff,sanjay,wilsonh,kerr,m3b,tushar,\x02k es,gruber}@google.com";
  constexpr int kExpectedTextCount = std::size(kExpectedText) - 1;

  ASSERT_TRUE(OpenDocument("bigtable_mini.pdf"));
  FPDF_PAGE page = LoadPage(0);
  ASSERT_TRUE(page);

  {
    ScopedFPDFTextPage text_page(FPDFText_LoadPage(page));
    ASSERT_TRUE(text_page);
    int char_count = FPDFText_CountChars(text_page.get());
    ASSERT_GE(char_count, 0);
    ASSERT_EQ(kExpectedTextCount, char_count);

    for (int i = 0; i < kExpectedTextCount; ++i) {
      EXPECT_EQ(static_cast<uint32_t>(kExpectedText[i]),
                FPDFText_GetUnicode(text_page.get(), i));
    }
  }

  UnloadPage(page);
}

TEST_F(FPDFTextEmbedderTest, Bug1769) {
  ASSERT_TRUE(OpenDocument("bug_1769.pdf"));
  FPDF_PAGE page = LoadPage(0);
  ASSERT_TRUE(page);

  {
    ScopedFPDFTextPage textpage(FPDFText_LoadPage(page));
    ASSERT_TRUE(textpage);

    unsigned short buffer[128] = {};
    // TODO(crbug.com/pdfium/1769): Improve text extraction.
    // The first instance of "world" is visible to the human eye and should be
    // extracted as is. The second instance is not, so how it should be
    // extracted is debatable.
    ASSERT_EQ(10, FPDFText_GetText(textpage.get(), 0, 128, buffer));
    EXPECT_TRUE(check_unsigned_shorts("wo d wo d", buffer, 10));
  }

  UnloadPage(page);
}
