// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/picker/views/picker_preview_bubble_controller.h"

#include <memory>
#include <utility>

#include "ash/picker/views/picker_preview_bubble.h"
#include "ash/test/view_drawn_waiter.h"
#include "base/run_loop.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/models/image_model.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/image/image_unittest_util.h"
#include "ui/views/test/views_test_base.h"
#include "ui/views/test/widget_test.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"

namespace ash {
namespace {

using PickerPreviewBubbleControllerTest = views::ViewsTestBase;

// Creates a basic widget and view that acts as the anchor view for the preview
// bubble.
std::unique_ptr<views::Widget> CreateAnchorWidget(gfx::NativeWindow context) {
  views::Widget::InitParams params(
      views::Widget::InitParams::WIDGET_OWNS_NATIVE_WIDGET,
      views::Widget::InitParams::Type::TYPE_WINDOW_FRAMELESS);
  params.bounds = gfx::Rect(0, 0, 400, 400);
  params.context = context;

  auto widget = std::make_unique<views::Widget>(std::move(params));
  widget->SetContentsView(std::make_unique<views::View>());
  widget->Show();
  return widget;
}

ash::HoldingSpaceImage CreateUnresolvedAsyncImage() {
  return ash::HoldingSpaceImage(PickerPreviewBubbleView::kPreviewImageSize,
                                base::FilePath(), base::DoNothing());
}

TEST_F(PickerPreviewBubbleControllerTest, ShowBubbleShowsBubbleWidget) {
  std::unique_ptr<views::Widget> anchor_widget =
      CreateAnchorWidget(GetContext());
  PickerPreviewBubbleController controller;
  ash::HoldingSpaceImage async_preview_image = CreateUnresolvedAsyncImage();
  controller.ShowBubble(&async_preview_image, anchor_widget->GetContentsView());

  views::View* bubble_view = controller.bubble_view_for_testing();
  ASSERT_NE(bubble_view, nullptr);
  ViewDrawnWaiter().Wait(bubble_view);
  ASSERT_NE(bubble_view->GetWidget(), nullptr);
  views::test::WidgetVisibleWaiter(bubble_view->GetWidget()).Wait();
}

TEST_F(PickerPreviewBubbleControllerTest, CloseBubbleClosesBubbleWidget) {
  std::unique_ptr<views::Widget> anchor_widget =
      CreateAnchorWidget(GetContext());
  PickerPreviewBubbleController controller;
  ash::HoldingSpaceImage async_preview_image = CreateUnresolvedAsyncImage();
  controller.ShowBubble(&async_preview_image, anchor_widget->GetContentsView());
  ASSERT_NE(controller.bubble_view_for_testing(), nullptr);
  views::Widget* bubble_widget =
      controller.bubble_view_for_testing()->GetWidget();

  controller.CloseBubble();

  views::test::WidgetDestroyedWaiter(bubble_widget).Wait();
  EXPECT_EQ(controller.bubble_view_for_testing(), nullptr);
}

TEST_F(PickerPreviewBubbleControllerTest,
       DestroyingAnchorWidgetDestroysBubbleWidget) {
  PickerPreviewBubbleController controller;
  std::unique_ptr<views::Widget> anchor_widget =
      CreateAnchorWidget(GetContext());
  ash::HoldingSpaceImage async_preview_image = CreateUnresolvedAsyncImage();
  controller.ShowBubble(&async_preview_image, anchor_widget->GetContentsView());
  ASSERT_NE(controller.bubble_view_for_testing(), nullptr);
  views::Widget* bubble_widget =
      controller.bubble_view_for_testing()->GetWidget();

  anchor_widget->Close();

  views::test::WidgetDestroyedWaiter(bubble_widget).Wait();
  EXPECT_EQ(controller.bubble_view_for_testing(), nullptr);
}

TEST_F(PickerPreviewBubbleControllerTest,
       DestroyingAnchorWidgetImmediatelyDoesNotCrash) {
  PickerPreviewBubbleController controller;
  std::unique_ptr<views::Widget> anchor_widget =
      CreateAnchorWidget(GetContext());
  ash::HoldingSpaceImage async_preview_image = CreateUnresolvedAsyncImage();
  controller.ShowBubble(&async_preview_image, anchor_widget->GetContentsView());

  anchor_widget->CloseNow();

  EXPECT_EQ(controller.bubble_view_for_testing(), nullptr);
}

TEST_F(PickerPreviewBubbleControllerTest, ShowBubbleWhileShownKeepsSameBubble) {
  std::unique_ptr<views::Widget> anchor_widget =
      CreateAnchorWidget(GetContext());
  PickerPreviewBubbleController controller;
  ash::HoldingSpaceImage async_preview_image = CreateUnresolvedAsyncImage();
  controller.ShowBubble(&async_preview_image, anchor_widget->GetContentsView());
  views::View* bubble_view = controller.bubble_view_for_testing();
  ViewDrawnWaiter().Wait(bubble_view);

  controller.ShowBubble(&async_preview_image, anchor_widget->GetContentsView());

  ASSERT_EQ(controller.bubble_view_for_testing(), bubble_view);
  EXPECT_EQ(controller.bubble_view_for_testing()->GetWidget(),
            bubble_view->GetWidget());
}

TEST_F(PickerPreviewBubbleControllerTest, CloseBubbleWithoutShowing) {
  PickerPreviewBubbleController controller;

  controller.CloseBubble();

  EXPECT_EQ(controller.bubble_view_for_testing(), nullptr);
}

TEST_F(PickerPreviewBubbleControllerTest, ShowingBubbleWhileClosingOldBubble) {
  std::unique_ptr<views::Widget> anchor_widget =
      CreateAnchorWidget(GetContext());
  PickerPreviewBubbleController controller;
  ash::HoldingSpaceImage async_preview_image = CreateUnresolvedAsyncImage();
  controller.ShowBubble(&async_preview_image, anchor_widget->GetContentsView());

  // CloseBubble is asynchronous.
  controller.CloseBubble();
  controller.ShowBubble(&async_preview_image, anchor_widget->GetContentsView());
  views::View* bubble_view = controller.bubble_view_for_testing();
  ViewDrawnWaiter().Wait(bubble_view);

  ASSERT_EQ(controller.bubble_view_for_testing(), bubble_view);
  EXPECT_EQ(controller.bubble_view_for_testing()->GetWidget(),
            bubble_view->GetWidget());
}

TEST_F(PickerPreviewBubbleControllerTest,
       ShowBubbleUsesPlaceholderBeforeBitmapResolves) {
  std::unique_ptr<views::Widget> anchor_widget =
      CreateAnchorWidget(GetContext());
  PickerPreviewBubbleController controller;

  ash::HoldingSpaceImage async_preview_image = CreateUnresolvedAsyncImage();
  controller.ShowBubble(&async_preview_image, anchor_widget->GetContentsView());
  PickerPreviewBubbleView* bubble_view = controller.bubble_view_for_testing();
  ViewDrawnWaiter().Wait(bubble_view);

  EXPECT_EQ(bubble_view->GetPreviewImage().GetImage().AsBitmap().getColor(5, 5),
            SK_ColorTRANSPARENT);
}

TEST_F(PickerPreviewBubbleControllerTest,
       ShowBubbleUpdatesPreviewAfterBitmapResolves) {
  std::unique_ptr<views::Widget> anchor_widget =
      CreateAnchorWidget(GetContext());
  base::RunLoop run_loop;
  SkBitmap bitmap = gfx::test::CreateBitmap(100, SK_ColorBLUE);
  ash::HoldingSpaceImage async_preview_image(
      PickerPreviewBubbleView::kPreviewImageSize, base::FilePath(),
      base::BindLambdaForTesting(
          [&](const base::FilePath& file_path, const gfx::Size& size,
              HoldingSpaceImage::BitmapCallback callback) {
            std::move(callback).Run(&bitmap, base::File::Error::FILE_OK);
            run_loop.Quit();
          }));
  PickerPreviewBubbleController controller;

  controller.ShowBubble(&async_preview_image, anchor_widget->GetContentsView());
  PickerPreviewBubbleView* bubble_view = controller.bubble_view_for_testing();
  ViewDrawnWaiter().Wait(bubble_view);

  run_loop.Run();
  EXPECT_EQ(bubble_view->GetPreviewImage().GetImage().AsBitmap().getColor(5, 5),
            SK_ColorBLUE);
}

}  // namespace
}  // namespace ash
