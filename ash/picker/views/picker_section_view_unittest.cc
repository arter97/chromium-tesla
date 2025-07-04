// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/picker/views/picker_section_view.h"

#include <memory>
#include <string>
#include <utility>

#include "ash/picker/mock_picker_asset_fetcher.h"
#include "ash/picker/views/picker_emoji_item_view.h"
#include "ash/picker/views/picker_emoticon_item_view.h"
#include "ash/picker/views/picker_gif_view.h"
#include "ash/picker/views/picker_image_item_view.h"
#include "ash/picker/views/picker_item_view.h"
#include "ash/picker/views/picker_list_item_view.h"
#include "ash/picker/views/picker_preview_bubble_controller.h"
#include "ash/picker/views/picker_symbol_item_view.h"
#include "ash/public/cpp/picker/picker_search_result.h"
#include "base/containers/span.h"
#include "base/functional/callback_helpers.h"
#include "base/memory/raw_ptr.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/test/views_test_base.h"
#include "ui/views/view.h"
#include "ui/views/view_utils.h"

namespace ash {
namespace {

using ::testing::IsEmpty;
using ::testing::Property;
using ::testing::SizeIs;

constexpr int kDefaultSectionWidth = 320;

std::unique_ptr<PickerImageItemView> CreateGifItem(
    const gfx::Size& gif_dimensions) {
  return std::make_unique<PickerImageItemView>(
      base::DoNothing(),
      std::make_unique<PickerGifView>(
          /*frames_fetcher=*/base::DoNothing(),
          /*preview_image_fetcher=*/base::DoNothing(), gif_dimensions,
          /*accessible_name=*/u""));
}

using PickerSectionViewTest = views::ViewsTestBase;

TEST_F(PickerSectionViewTest, CreatesTitleLabel) {
  MockPickerAssetFetcher asset_fetcher;
  PickerSectionView section_view(kDefaultSectionWidth, &asset_fetcher);

  const std::u16string kSectionTitleText = u"Section";
  section_view.AddTitleLabel(kSectionTitleText);

  EXPECT_THAT(section_view.title_label_for_testing(),
              Property(&views::Label::GetText, kSectionTitleText));
}

TEST_F(PickerSectionViewTest, AddsListItem) {
  MockPickerAssetFetcher asset_fetcher;
  PickerSectionView section_view(kDefaultSectionWidth, &asset_fetcher);

  section_view.AddListItem(
      std::make_unique<PickerListItemView>(base::DoNothing()));

  base::span<const raw_ptr<PickerItemView>> items =
      section_view.item_views_for_testing();
  ASSERT_THAT(items, SizeIs(1));
  EXPECT_TRUE(views::IsViewClass<PickerListItemView>(items[0]));
}

TEST_F(PickerSectionViewTest, AddsTwoListItems) {
  MockPickerAssetFetcher asset_fetcher;
  PickerSectionView section_view(kDefaultSectionWidth, &asset_fetcher);

  section_view.AddListItem(
      std::make_unique<PickerListItemView>(base::DoNothing()));
  section_view.AddListItem(
      std::make_unique<PickerListItemView>(base::DoNothing()));

  base::span<const raw_ptr<PickerItemView>> items =
      section_view.item_views_for_testing();
  ASSERT_THAT(items, SizeIs(2));
  EXPECT_TRUE(views::IsViewClass<PickerListItemView>(items[0]));
  EXPECT_TRUE(views::IsViewClass<PickerListItemView>(items[1]));
}

TEST_F(PickerSectionViewTest, AddsEmojiItem) {
  MockPickerAssetFetcher asset_fetcher;
  PickerSectionView section_view(kDefaultSectionWidth, &asset_fetcher);

  section_view.AddEmojiItem(
      std::make_unique<PickerEmojiItemView>(base::DoNothing(), u"😊"));

  base::span<const raw_ptr<PickerItemView>> items =
      section_view.item_views_for_testing();
  ASSERT_THAT(items, SizeIs(1));
  EXPECT_TRUE(views::IsViewClass<PickerEmojiItemView>(items[0]));
}

TEST_F(PickerSectionViewTest, AddsSymbolItem) {
  MockPickerAssetFetcher asset_fetcher;
  PickerSectionView section_view(kDefaultSectionWidth, &asset_fetcher);

  section_view.AddSymbolItem(
      std::make_unique<PickerSymbolItemView>(base::DoNothing(), u"♬"));

  base::span<const raw_ptr<PickerItemView>> items =
      section_view.item_views_for_testing();
  ASSERT_THAT(items, SizeIs(1));
  EXPECT_TRUE(views::IsViewClass<PickerSymbolItemView>(items[0]));
}

TEST_F(PickerSectionViewTest, AddsEmoticonItem) {
  MockPickerAssetFetcher asset_fetcher;
  PickerSectionView section_view(kDefaultSectionWidth, &asset_fetcher);

  section_view.AddEmoticonItem(std::make_unique<PickerEmoticonItemView>(
      base::DoNothing(), u"¯\\_(ツ)_/¯"));

  base::span<const raw_ptr<PickerItemView>> items =
      section_view.item_views_for_testing();
  ASSERT_THAT(items, SizeIs(1));
  EXPECT_TRUE(views::IsViewClass<PickerEmoticonItemView>(items[0]));
}

TEST_F(PickerSectionViewTest, AddsGifItem) {
  MockPickerAssetFetcher asset_fetcher;
  PickerSectionView section_view(kDefaultSectionWidth, &asset_fetcher);

  section_view.AddImageItem(CreateGifItem(gfx::Size(100, 100)));

  base::span<const raw_ptr<PickerItemView>> items =
      section_view.item_views_for_testing();
  ASSERT_THAT(items, SizeIs(1));
  EXPECT_TRUE(views::IsViewClass<PickerImageItemView>(items[0]));
}

TEST_F(PickerSectionViewTest, EmojiItemsAndGifItems) {
  MockPickerAssetFetcher asset_fetcher;
  PickerSectionView section_view(kDefaultSectionWidth, &asset_fetcher);

  section_view.AddEmojiItem(
      std::make_unique<PickerEmojiItemView>(base::DoNothing(), u"😊"));
  section_view.AddImageItem(CreateGifItem(gfx::Size(100, 100)));

  base::span<const raw_ptr<PickerItemView>> items =
      section_view.item_views_for_testing();
  ASSERT_THAT(items, SizeIs(2));
  EXPECT_TRUE(views::IsViewClass<PickerEmojiItemView>(items[0]));
  EXPECT_TRUE(views::IsViewClass<PickerImageItemView>(items[1]));
}

TEST_F(PickerSectionViewTest, AddsResults) {
  MockPickerAssetFetcher asset_fetcher;
  PickerSectionView section_view(kDefaultSectionWidth, &asset_fetcher);
  PickerPreviewBubbleController preview_controller;

  section_view.AddResult(PickerSearchResult::Text(u"Result"),
                         &preview_controller, base::DoNothing());

  section_view.AddResult(PickerSearchResult::Emoji(u"😊"), &preview_controller,
                         base::DoNothing());

  base::span<const raw_ptr<PickerItemView>> items =
      section_view.item_views_for_testing();
  ASSERT_THAT(items, SizeIs(2));
  EXPECT_TRUE(views::IsViewClass<PickerListItemView>(items[0]));
  EXPECT_TRUE(views::IsViewClass<PickerEmojiItemView>(items[1]));
}

TEST_F(PickerSectionViewTest, ClearsItems) {
  MockPickerAssetFetcher asset_fetcher;
  PickerSectionView section_view(kDefaultSectionWidth, &asset_fetcher);
  section_view.AddListItem(
      std::make_unique<PickerListItemView>(base::DoNothing()));

  section_view.ClearItems();

  EXPECT_THAT(section_view.item_views_for_testing(), IsEmpty());
}

}  // namespace
}  // namespace ash
