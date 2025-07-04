// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/wm/overview/overview_focus_cycler_old.h"

#include "ash/accessibility/accessibility_controller.h"
#include "ash/constants/ash_features.h"
#include "ash/shell.h"
#include "ash/style/close_button.h"
#include "ash/wm/desks/desk.h"
#include "ash/wm/desks/desk_action_button.h"
#include "ash/wm/desks/desk_action_view.h"
#include "ash/wm/desks/desk_mini_view.h"
#include "ash/wm/desks/desk_name_view.h"
#include "ash/wm/desks/desk_preview_view.h"
#include "ash/wm/desks/desks_test_util.h"
#include "ash/wm/desks/overview_desk_bar_view.h"
#include "ash/wm/desks/templates/saved_desk_util.h"
#include "ash/wm/overview/overview_controller.h"
#include "ash/wm/overview/overview_focusable_view.h"
#include "ash/wm/overview/overview_grid.h"
#include "ash/wm/overview/overview_item.h"
#include "ash/wm/overview/overview_item_view.h"
#include "ash/wm/overview/overview_test_base.h"
#include "ash/wm/overview/overview_test_util.h"
#include "ash/wm/overview/scoped_overview_transform_window.h"
#include "ash/wm/tablet_mode/tablet_mode_controller_test_api.h"
#include "ash/wm/window_util.h"
#include "base/memory/raw_ptr.h"
#include "base/test/scoped_feature_list.h"
#include "ui/aura/window.h"
#include "ui/display/manager/display_manager.h"
#include "ui/display/test/display_manager_test_api.h"
#include "ui/events/keycodes/keyboard_codes_posix.h"
#include "ui/events/test/event_generator.h"
#include "ui/views/widget/widget.h"

namespace ash {

class OverviewFocusCyclerOldTest : public OverviewTestBase,
                                   public testing::WithParamInterface<bool> {
 public:
  OverviewFocusCyclerOldTest() = default;
  OverviewFocusCyclerOldTest(const OverviewFocusCyclerOldTest&) = delete;
  OverviewFocusCyclerOldTest& operator=(const OverviewFocusCyclerOldTest&) = delete;
  ~OverviewFocusCyclerOldTest() override = default;

  OverviewFocusCyclerOld* GetFocusCycler() {
    return GetOverviewSession()->focus_cycler_old();
  }

  // Helper to make tests more readable.
  bool AreDeskTemplatesEnabled() const { return GetParam(); }

  // OverviewTestBase:
  void SetUp() override {
    scoped_feature_list_.InitWithFeatureStates(
        {{features::kDesksTemplates, AreDeskTemplatesEnabled()},
         {features::kFasterSplitScreenSetup, true},
         {features::kOsSettingsRevampWayfinding, true},
         {features::kOverviewNewFocus, false}});
    OverviewTestBase::SetUp();
    ScopedOverviewTransformWindow::SetImmediateCloseForTests(true);
  }

 private:
  base::test::ScopedFeatureList scoped_feature_list_;
};

// Tests traversing some windows in overview mode with the tab key.
TEST_P(OverviewFocusCyclerOldTest, BasicTabKeyNavigation) {
  std::unique_ptr<aura::Window> window2(CreateTestWindow());
  std::unique_ptr<aura::Window> window1(CreateTestWindow());

  ToggleOverview();
  const std::vector<std::unique_ptr<OverviewItemBase>>& overview_windows =
      GetOverviewItemsForRoot(0);
  auto* event_generator = GetEventGenerator();
  SendKeyUntilOverviewItemIsFocused(ui::VKEY_TAB, event_generator);
  EXPECT_EQ(overview_windows[0]->GetWindow(), GetOverviewFocusedWindow());
  SendKeyUntilOverviewItemIsFocused(ui::VKEY_TAB, event_generator);
  EXPECT_EQ(overview_windows[1]->GetWindow(), GetOverviewFocusedWindow());
  SendKeyUntilOverviewItemIsFocused(ui::VKEY_TAB, event_generator);
  EXPECT_EQ(overview_windows[0]->GetWindow(), GetOverviewFocusedWindow());
  SendKeyUntilOverviewItemIsFocused(ui::VKEY_RIGHT, event_generator);
  EXPECT_EQ(overview_windows[1]->GetWindow(), GetOverviewFocusedWindow());
  SendKeyUntilOverviewItemIsFocused(ui::VKEY_LEFT, event_generator);
  EXPECT_EQ(overview_windows[0]->GetWindow(), GetOverviewFocusedWindow());
}

// Same as above but for tablet mode. Regression test for crbug.com/1036140.
TEST_P(OverviewFocusCyclerOldTest, BasicTabKeyNavigationTablet) {
  std::unique_ptr<aura::Window> window1(CreateTestWindow());
  std::unique_ptr<aura::Window> window2(CreateTestWindow());
  std::unique_ptr<aura::Window> window3(CreateTestWindow());

  TabletModeControllerTestApi().EnterTabletMode();
  ToggleOverview();
  const std::vector<std::unique_ptr<OverviewItemBase>>& overview_windows =
      GetOverviewItemsForRoot(0);
  auto* event_generator = GetEventGenerator();
  SendKeyUntilOverviewItemIsFocused(ui::VKEY_TAB, event_generator);
  EXPECT_EQ(overview_windows[0]->GetWindow(), GetOverviewFocusedWindow());
  SendKeyUntilOverviewItemIsFocused(ui::VKEY_TAB, event_generator);
  EXPECT_EQ(overview_windows[1]->GetWindow(), GetOverviewFocusedWindow());
  SendKeyUntilOverviewItemIsFocused(ui::VKEY_RIGHT, event_generator);
  EXPECT_EQ(overview_windows[2]->GetWindow(), GetOverviewFocusedWindow());
  SendKeyUntilOverviewItemIsFocused(ui::VKEY_LEFT, event_generator);
  EXPECT_EQ(overview_windows[1]->GetWindow(), GetOverviewFocusedWindow());
}

// Tests that pressing Ctrl+W while a window is selected in overview closes it.
TEST_P(OverviewFocusCyclerOldTest, CloseWindowWithKey) {
  std::unique_ptr<views::Widget> widget(
      CreateTestWidget(views::Widget::InitParams::WIDGET_OWNS_NATIVE_WIDGET));
  ToggleOverview();

  SendKeyUntilOverviewItemIsFocused(ui::VKEY_RIGHT, GetEventGenerator());
  EXPECT_EQ(widget->GetNativeWindow(), GetOverviewFocusedWindow());
  PressAndReleaseKey(ui::VKEY_W, ui::EF_CONTROL_DOWN);
  EXPECT_TRUE(widget->IsClosed());
}

// Tests traversing some windows in overview mode with the arrow keys in every
// possible direction.
TEST_P(OverviewFocusCyclerOldTest, BasicArrowKeyNavigation) {
  const size_t test_windows = 9;
  UpdateDisplay("800x600");
  std::vector<std::unique_ptr<aura::Window>> windows;
  for (size_t i = test_windows; i > 0; --i) {
    windows.push_back(
        std::unique_ptr<aura::Window>(CreateTestWindowInShellWithId(i)));
  }

  ui::KeyboardCode arrow_keys[] = {ui::VKEY_RIGHT, ui::VKEY_DOWN, ui::VKEY_LEFT,
                                   ui::VKEY_UP};
  // The rows contain variable number of items making vertical navigation not
  // feasible. [Down] is equivalent to [Right] and [Up] is equivalent to [Left].
  int index_path_for_direction[][test_windows + 1] = {
      {1, 2, 3, 4, 5, 6, 7, 8, 9, 1},  // Right
      {1, 2, 3, 4, 5, 6, 7, 8, 9, 1},  // Down (same as Right)
      {9, 8, 7, 6, 5, 4, 3, 2, 1, 9},  // Left
      {9, 8, 7, 6, 5, 4, 3, 2, 1, 9}   // Up (same as Left)
  };

  auto* event_generator = GetEventGenerator();
  for (size_t key_index = 0; key_index < std::size(arrow_keys); ++key_index) {
    ToggleOverview();
    const std::vector<std::unique_ptr<OverviewItemBase>>& overview_windows =
        GetOverviewItemsForRoot(0);
    for (size_t i = 0; i < test_windows + 1; ++i) {
      SendKeyUntilOverviewItemIsFocused(arrow_keys[key_index], event_generator);

      const int index = index_path_for_direction[key_index][i];
      EXPECT_EQ(GetOverviewFocusedWindow()->GetId(),
                overview_windows[index - 1]->GetWindow()->GetId())
          << "Focused window id(" +
                 base::NumberToString(GetOverviewFocusedWindow()->GetId()) +
                 ") did not match overview item window at index " +
                 base::NumberToString(index) + "'s id(" +
                 base::NumberToString(
                     overview_windows[index - 1]->GetWindow()->GetId()) +
                 ")";
    }
    ToggleOverview();
  }
}

// Tests that when an item is removed while focused, the focus ring disappears,
// and when we tab again we pick up where we left off.
TEST_P(OverviewFocusCyclerOldTest, ItemClosed) {
  auto widget1 =
      CreateTestWidget(views::Widget::InitParams::WIDGET_OWNS_NATIVE_WIDGET);
  auto widget2 =
      CreateTestWidget(views::Widget::InitParams::WIDGET_OWNS_NATIVE_WIDGET);
  auto widget3 =
      CreateTestWidget(views::Widget::InitParams::WIDGET_OWNS_NATIVE_WIDGET);
  ToggleOverview();

  auto* event_generator = GetEventGenerator();
  SendKeyUntilOverviewItemIsFocused(ui::VKEY_TAB, event_generator);
  SendKeyUntilOverviewItemIsFocused(ui::VKEY_TAB, event_generator);
  EXPECT_EQ(widget2->GetNativeWindow(), GetOverviewFocusedWindow());

  // Remove `widget2` by closing it with ctrl + W. Test that the focus ring
  // becomes invisible (neither widget is focused).
  PressAndReleaseKey(ui::VKEY_W, ui::EF_CONTROL_DOWN);
  EXPECT_TRUE(widget2->IsClosed());
  widget2.reset();
  EXPECT_FALSE(GetOverviewFocusedWindow());

  // Tests that on pressing tab, the focus ring becomes visible and we focus
  // the window that comes after the deleted one.
  SendKeyUntilOverviewItemIsFocused(ui::VKEY_TAB, event_generator);
  EXPECT_EQ(widget1->GetNativeWindow(), GetOverviewFocusedWindow());
}

// Tests basic selection across multiple monitors.
// TODO(http://b/325335020): Port this test to `OverviewFocusCyclerTest`.
TEST_P(OverviewFocusCyclerOldTest, BasicMultiMonitorArrowKeyNavigation) {
  UpdateDisplay("500x400,500x400");
  const gfx::Rect bounds1(100, 100);
  const gfx::Rect bounds2(550, 0, 100, 100);
  std::unique_ptr<aura::Window> window4(CreateTestWindow(bounds2));
  std::unique_ptr<aura::Window> window3(CreateTestWindow(bounds2));
  std::unique_ptr<aura::Window> window2(CreateTestWindow(bounds1));
  std::unique_ptr<aura::Window> window1(CreateTestWindow(bounds1));

  ToggleOverview();

  auto* event_generator = GetEventGenerator();
  const std::vector<std::unique_ptr<OverviewItemBase>>& overview_root1 =
      GetOverviewItemsForRoot(0);
  const std::vector<std::unique_ptr<OverviewItemBase>>& overview_root2 =
      GetOverviewItemsForRoot(1);
  SendKeyUntilOverviewItemIsFocused(ui::VKEY_RIGHT, event_generator);
  EXPECT_EQ(GetOverviewFocusedWindow(), overview_root1[0]->GetWindow());
  SendKeyUntilOverviewItemIsFocused(ui::VKEY_RIGHT, event_generator);
  EXPECT_EQ(GetOverviewFocusedWindow(), overview_root1[1]->GetWindow());
  SendKeyUntilOverviewItemIsFocused(ui::VKEY_RIGHT, event_generator);
  EXPECT_EQ(GetOverviewFocusedWindow(), overview_root2[0]->GetWindow());
  SendKeyUntilOverviewItemIsFocused(ui::VKEY_RIGHT, event_generator);
  EXPECT_EQ(GetOverviewFocusedWindow(), overview_root2[1]->GetWindow());
}

// Tests first monitor when display order doesn't match left to right screen
// positions.
// TODO(http://b/325335020): Port this test to `OverviewFocusCyclerTest`.
TEST_P(OverviewFocusCyclerOldTest, MultiMonitorReversedOrder) {
  UpdateDisplay("500x400,500x400");
  Shell::Get()->display_manager()->SetLayoutForCurrentDisplays(
      display::test::CreateDisplayLayout(display_manager(),
                                         display::DisplayPlacement::LEFT, 0));
  aura::Window::Windows root_windows = Shell::GetAllRootWindows();
  std::unique_ptr<aura::Window> window2(CreateTestWindow(gfx::Rect(100, 100)));
  std::unique_ptr<aura::Window> window1(
      CreateTestWindow(gfx::Rect(-450, 0, 100, 100)));
  EXPECT_EQ(root_windows[1], window1->GetRootWindow());
  EXPECT_EQ(root_windows[0], window2->GetRootWindow());

  ToggleOverview();

  // Coming from the left to right, we should select window1 first being on the
  // display to the left.
  auto* event_generator = GetEventGenerator();
  SendKeyUntilOverviewItemIsFocused(ui::VKEY_RIGHT, event_generator);
  EXPECT_EQ(GetOverviewFocusedWindow(), window1.get());

  // Exit and reenter overview.
  ToggleOverview();
  ToggleOverview();

  // Coming from right to left, we should select window2 first being on the
  // display on the right.
  SendKeyUntilOverviewItemIsFocused(ui::VKEY_LEFT, event_generator);
  EXPECT_EQ(GetOverviewFocusedWindow(), window2.get());
}

// Tests three monitors where the grid becomes empty on one of the monitors.
// TODO(http://b/325335020): Port this test to `OverviewFocusCyclerTest`.
TEST_P(OverviewFocusCyclerOldTest, ThreeMonitors) {
  UpdateDisplay("500x400,500x400,500x400");
  aura::Window::Windows root_windows = Shell::GetAllRootWindows();
  std::unique_ptr<aura::Window> window3(
      CreateTestWindow(gfx::Rect(1000, 0, 100, 100)));
  std::unique_ptr<aura::Window> window2(
      CreateTestWindow(gfx::Rect(500, 0, 100, 100)));
  std::unique_ptr<aura::Window> window1(CreateTestWindow(gfx::Rect(100, 100)));
  EXPECT_EQ(root_windows[0], window1->GetRootWindow());
  EXPECT_EQ(root_windows[1], window2->GetRootWindow());
  EXPECT_EQ(root_windows[2], window3->GetRootWindow());

  ToggleOverview();

  auto* event_generator = GetEventGenerator();
  SendKeyUntilOverviewItemIsFocused(ui::VKEY_RIGHT, event_generator);
  SendKeyUntilOverviewItemIsFocused(ui::VKEY_RIGHT, event_generator);
  SendKeyUntilOverviewItemIsFocused(ui::VKEY_RIGHT, event_generator);
  EXPECT_EQ(window3.get(), GetOverviewFocusedWindow());

  // If the selected window is closed, then nothing should be selected.
  window3.reset();
  EXPECT_EQ(nullptr, GetOverviewFocusedWindow());
  ToggleOverview();

  window3 = CreateTestWindow(gfx::Rect(1000, 0, 100, 100));
  ToggleOverview();
  SendKeyUntilOverviewItemIsFocused(ui::VKEY_RIGHT, event_generator);
  SendKeyUntilOverviewItemIsFocused(ui::VKEY_RIGHT, event_generator);
  SendKeyUntilOverviewItemIsFocused(ui::VKEY_RIGHT, event_generator);

  // If the window on the second display is removed, the selected window should
  // remain window3.
  EXPECT_EQ(window3.get(), GetOverviewFocusedWindow());
  window2.reset();
  EXPECT_EQ(window3.get(), GetOverviewFocusedWindow());
}

// Tests selecting a window in overview mode with the return key.
TEST_P(OverviewFocusCyclerOldTest, FocusOverviewWindowWithReturnKey) {
  std::unique_ptr<aura::Window> window2(CreateTestWindow());
  std::unique_ptr<aura::Window> window1(CreateTestWindow());
  ToggleOverview();

  // Pressing the return key on an item that is not focused should not do
  // anything.
  PressAndReleaseKey(ui::VKEY_RETURN);
  OverviewController* overview_controller = OverviewController::Get();
  EXPECT_TRUE(overview_controller->InOverviewSession());

  // Highlight the first window.
  ASSERT_TRUE(FocusOverviewWindow(window1.get(), GetEventGenerator()));
  PressAndReleaseKey(ui::VKEY_RETURN);
  EXPECT_FALSE(overview_controller->InOverviewSession());
  EXPECT_TRUE(wm::IsActiveWindow(window1.get()));

  // Highlight the second window.
  ToggleOverview();
  ASSERT_TRUE(FocusOverviewWindow(window2.get(), GetEventGenerator()));
  PressAndReleaseKey(ui::VKEY_RETURN);
  EXPECT_FALSE(OverviewController::Get()->InOverviewSession());
  EXPECT_TRUE(wm::IsActiveWindow(window2.get()));
}

// Tests that the location of the overview focus ring is as expected while
// dragging an overview item.
TEST_P(OverviewFocusCyclerOldTest, FocusLocationWhileDragging) {
  std::unique_ptr<aura::Window> window1(CreateTestWindow(gfx::Rect(200, 200)));
  std::unique_ptr<aura::Window> window2(CreateTestWindow(gfx::Rect(200, 200)));
  std::unique_ptr<aura::Window> window3(CreateTestWindow(gfx::Rect(200, 200)));

  ToggleOverview();

  // Tab once to show the focus ring.
  auto* event_generator = GetEventGenerator();
  SendKeyUntilOverviewItemIsFocused(ui::VKEY_TAB, event_generator);
  EXPECT_EQ(window3.get(), GetOverviewFocusedWindow());
  OverviewItemBase* item = GetOverviewItemForWindow(window3.get());

  // Tests that while dragging an item, tabbing does not change which item the
  // focus ring is shown on, but the focus ring is hidden. Drag the item in a
  // way which does not enter splitview, or close overview.
  const gfx::PointF start_point = item->target_bounds().CenterPoint();
  const gfx::PointF end_point(20.f, 20.f);
  GetOverviewSession()->InitiateDrag(item, start_point,
                                     /*is_touch_dragging=*/true,
                                     /*event_source_item=*/item);
  GetOverviewSession()->Drag(item, end_point);
  SendKeyUntilOverviewItemIsFocused(ui::VKEY_TAB, event_generator);
  EXPECT_EQ(window3.get(), GetOverviewFocusedWindow());
  EXPECT_FALSE(GetFocusCycler()->IsFocusVisible());

  // Tests that on releasing the item, the focused window remains the same.
  GetOverviewSession()->Drag(item, start_point);
  GetOverviewSession()->CompleteDrag(item, start_point);
  EXPECT_EQ(window3.get(), GetOverviewFocusedWindow());
  EXPECT_TRUE(GetFocusCycler()->IsFocusVisible());

  // Tests that on tabbing after releasing, the focused window is the next one.
  SendKeyUntilOverviewItemIsFocused(ui::VKEY_TAB, event_generator);
  EXPECT_EQ(window2.get(), GetOverviewFocusedWindow());
}

// ----------------------------------------------------------------------------
// DesksOverviewFocusCyclerOldTest:

class DesksOverviewFocusCyclerOldTest : public OverviewFocusCyclerOldTest {
 public:
  DesksOverviewFocusCyclerOldTest() = default;

  DesksOverviewFocusCyclerOldTest(const DesksOverviewFocusCyclerOldTest&) = delete;
  DesksOverviewFocusCyclerOldTest& operator=(const DesksOverviewFocusCyclerOldTest&) =
      delete;

  ~DesksOverviewFocusCyclerOldTest() override = default;

  // OverviewFocusCyclerOldTest:
  void SetUp() override {
    OverviewFocusCyclerOldTest::SetUp();

    // All tests in this suite require the desks bar to be visible in overview,
    // which requires at least two desks.
    auto* desk_controller = DesksController::Get();
    desk_controller->NewDesk(DesksCreationRemovalSource::kButton);
    ASSERT_EQ(2u, desk_controller->desks().size());

    // Give the second desk a name. The desk name gets exposed as the accessible
    // name. And the focusable views that are painted in these tests will fail
    // the accessibility paint checker checks if they lack an accessible name.
    desk_controller->GetDeskAtIndex(1)->SetName(u"Desk 2", false);
  }

  OverviewFocusableView* GetHighlightedView() {
    return GetFocusCycler()->focused_view();
  }

  const OverviewDeskBarView* GetDesksBarViewForRoot(aura::Window* root_window) {
    OverviewGrid* grid =
        GetOverviewSession()->GetGridWithRootWindow(root_window);
    const OverviewDeskBarView* bar_view = grid->desks_bar_view();
    DCHECK(bar_view->IsZeroState() ^ grid->IsDesksBarViewActive());
    return bar_view;
  }

 protected:
  static void CheckDeskBarViewSize(const OverviewDeskBarView* view,
                                   const std::string& scope) {
    SCOPED_TRACE(scope);
    EXPECT_EQ(view->bounds().height(),
              view->GetWidget()->GetWindowBoundsInScreen().height());
  }
};

// Tests that we can tab through the desk mini views, new desk button and
// overview items in the correct order. Overview items will have the overview
// focus ring shown when focused, but desks items will not.
TEST_P(DesksOverviewFocusCyclerOldTest, TabbingBasic) {
  std::unique_ptr<aura::Window> window1(CreateTestWindow(gfx::Rect(200, 200)));
  std::unique_ptr<aura::Window> window2(CreateTestWindow(gfx::Rect(200, 200)));

  ToggleOverview();
  const auto* desk_bar_view =
      GetDesksBarViewForRoot(Shell::GetPrimaryRootWindow());

  CheckDeskBarViewSize(desk_bar_view, "initial");
  EXPECT_EQ(2u, desk_bar_view->mini_views().size());

  // Tests that the overview item gets focused first.
  PressAndReleaseKey(ui::VKEY_TAB);
  auto* item2 = GetOverviewItemForWindow(window2.get())
                    ->GetLeafItemForWindow(window2.get());
  EXPECT_EQ(item2->overview_item_view(), GetHighlightedView());
  CheckDeskBarViewSize(desk_bar_view, "overview item");

  // Tests that the first focused desk item is the first desk preview view.
  PressAndReleaseKey(ui::VKEY_TAB);
  PressAndReleaseKey(ui::VKEY_TAB);
  EXPECT_EQ(desk_bar_view->mini_views()[0]->desk_preview(),
            GetHighlightedView());
  CheckDeskBarViewSize(desk_bar_view, "first mini view");

  // Tests that the close all button of the first desk preview is focused next.
  PressAndReleaseKey(ui::VKEY_TAB);
  EXPECT_EQ(
      desk_bar_view->mini_views()[0]->desk_action_view()->close_all_button(),
      GetHighlightedView());

  // Test that one more tab focuses the desks name view.
  PressAndReleaseKey(ui::VKEY_TAB);
  EXPECT_EQ(desk_bar_view->mini_views()[0]->desk_name_view(),
            GetHighlightedView());

  // Tests that after tabbing through the mini views, we focus the new desk
  // button.
  PressAndReleaseKey(ui::VKEY_TAB);
  PressAndReleaseKey(ui::VKEY_TAB);
  PressAndReleaseKey(ui::VKEY_TAB);
  PressAndReleaseKey(ui::VKEY_TAB);
  EXPECT_EQ(desk_bar_view->new_desk_button(), GetHighlightedView());
  CheckDeskBarViewSize(desk_bar_view, "new desk button");

  // Tests that tabbing past the new desk button, we focus the save to a new
  // desk template. The templates button is not in the tab traversal since it is
  // hidden when we have no templates.
  if (AreDeskTemplatesEnabled()) {
    PressAndReleaseKey(ui::VKEY_TAB);
    EXPECT_EQ(desk_bar_view->overview_grid()->GetSaveDeskAsTemplateButton(),
              GetHighlightedView());
  }

  // Tests that after the save desk as template button (if the feature was
  // enabled), wo get the save desk for later button.
  PressAndReleaseKey(ui::VKEY_TAB);
  EXPECT_EQ(desk_bar_view->overview_grid()->GetSaveDeskForLaterButton(),
            GetHighlightedView());
  // Tests that after tabbing through the overview items, we go back to the
  // first overview item.
  PressAndReleaseKey(ui::VKEY_TAB);
  EXPECT_EQ(item2->overview_item_view(), GetHighlightedView());
  CheckDeskBarViewSize(desk_bar_view, "go back to first");
}

// Tests that we can reverse tab through the desk mini views, new desk button
// and overview items in the correct order.
TEST_P(DesksOverviewFocusCyclerOldTest, TabbingReverse) {
  std::unique_ptr<aura::Window> window1(CreateAppWindow(gfx::Rect(200, 200)));
  std::unique_ptr<aura::Window> window2(CreateAppWindow(gfx::Rect(200, 200)));

  ToggleOverview();
  const auto* desk_bar_view =
      GetDesksBarViewForRoot(Shell::GetPrimaryRootWindow());
  EXPECT_EQ(2u, desk_bar_view->mini_views().size());

  // Tests that the first focused item when reversing is the save desk for
  // later button.
  PressAndReleaseKey(ui::VKEY_TAB, ui::EF_SHIFT_DOWN);
  EXPECT_EQ(desk_bar_view->overview_grid()->GetSaveDeskForLaterButton(),
            GetHighlightedView());

  // Tests that after the save desk for later button, we get the save desk as
  // template button, if the feature is enabled.
  if (AreDeskTemplatesEnabled()) {
    PressAndReleaseKey(ui::VKEY_TAB, ui::EF_SHIFT_DOWN);
    EXPECT_EQ(desk_bar_view->overview_grid()->GetSaveDeskAsTemplateButton(),
              GetHighlightedView());
  }

  // Tests that after the desks templates button (if the feature was enabled),
  // we get to the new desk button.
  PressAndReleaseKey(ui::VKEY_TAB, ui::EF_SHIFT_DOWN);
  EXPECT_EQ(desk_bar_view->new_desk_button(), GetHighlightedView());

  // Tests that after the new desk button comes the preview views, desk action
  // buttons, and the desk name views in reverse order.
  PressAndReleaseKey(ui::VKEY_TAB, ui::EF_SHIFT_DOWN);
  EXPECT_EQ(desk_bar_view->mini_views()[1]->desk_name_view(),
            GetHighlightedView());
  PressAndReleaseKey(ui::VKEY_TAB, ui::EF_SHIFT_DOWN);
  EXPECT_EQ(
      desk_bar_view->mini_views()[1]->desk_action_view()->close_all_button(),
      GetHighlightedView());
  PressAndReleaseKey(ui::VKEY_TAB, ui::EF_SHIFT_DOWN);
  EXPECT_EQ(desk_bar_view->mini_views()[1]->desk_preview(),
            GetHighlightedView());

  PressAndReleaseKey(ui::VKEY_TAB, ui::EF_SHIFT_DOWN);
  EXPECT_EQ(desk_bar_view->mini_views()[0]->desk_name_view(),
            GetHighlightedView());
  PressAndReleaseKey(ui::VKEY_TAB, ui::EF_SHIFT_DOWN);
  EXPECT_EQ(
      desk_bar_view->mini_views()[0]->desk_action_view()->close_all_button(),
      GetHighlightedView());
  PressAndReleaseKey(ui::VKEY_TAB, ui::EF_SHIFT_DOWN);
  EXPECT_EQ(desk_bar_view->mini_views()[0]
                ->desk_action_view()
                ->combine_desks_button(),
            GetHighlightedView());
  PressAndReleaseKey(ui::VKEY_TAB, ui::EF_SHIFT_DOWN);
  EXPECT_EQ(desk_bar_view->mini_views()[0]->desk_preview(),
            GetHighlightedView());

  // Tests that the next focused item when reversing is the last overview item.
  PressAndReleaseKey(ui::VKEY_TAB, ui::EF_SHIFT_DOWN);
  auto* item1 = GetOverviewItemForWindow(window1.get())
                    ->GetLeafItemForWindow(window1.get());
  EXPECT_EQ(item1->overview_item_view(), GetHighlightedView());

  // Tests that the next focused item when reversing is the save desk for later
  // button.
  PressAndReleaseKey(ui::VKEY_TAB, ui::EF_SHIFT_DOWN);
  PressAndReleaseKey(ui::VKEY_TAB, ui::EF_SHIFT_DOWN);
  EXPECT_EQ(desk_bar_view->overview_grid()->GetSaveDeskForLaterButton(),
            GetHighlightedView());

  // Tests that we return to the save desk as template button after reverse
  // tabbing through the save desk for later button if the feature is enabled.
  if (AreDeskTemplatesEnabled()) {
    PressAndReleaseKey(ui::VKEY_TAB, ui::EF_SHIFT_DOWN);
    EXPECT_EQ(desk_bar_view->overview_grid()->GetSaveDeskAsTemplateButton(),
              GetHighlightedView());
  }
}

// Tests that we can tab and chromevox interchangeably through the desk mini
// views and new desk button in the correct order.
TEST_P(DesksOverviewFocusCyclerOldTest, TabbingChromevox) {
  Shell::Get()->accessibility_controller()->spoken_feedback().SetEnabled(true);
  ToggleOverview();
  const auto* desk_bar_view =
      GetDesksBarViewForRoot(Shell::GetPrimaryRootWindow());
  EXPECT_EQ(2u, desk_bar_view->mini_views().size());

  // Alternate between tabbing and chromevoxing through the 2 desk preview views
  // and name views.
  PressAndReleaseKey(ui::VKEY_TAB);
  EXPECT_EQ(desk_bar_view->mini_views()[0]->desk_preview(),
            GetHighlightedView());
  PressAndReleaseKey(ui::VKEY_RIGHT, ui::EF_COMMAND_DOWN);
  EXPECT_EQ(
      desk_bar_view->mini_views()[0]->desk_action_view()->close_all_button(),
      GetHighlightedView());
  PressAndReleaseKey(ui::VKEY_RIGHT, ui::EF_COMMAND_DOWN);
  EXPECT_EQ(desk_bar_view->mini_views()[0]->desk_name_view(),
            GetHighlightedView());

  PressAndReleaseKey(ui::VKEY_RIGHT, ui::EF_COMMAND_DOWN);
  EXPECT_EQ(desk_bar_view->mini_views()[1]->desk_preview(),
            GetHighlightedView());
  PressAndReleaseKey(ui::VKEY_RIGHT, ui::EF_COMMAND_DOWN);
  EXPECT_EQ(
      desk_bar_view->mini_views()[1]->desk_action_view()->close_all_button(),
      GetHighlightedView());
  PressAndReleaseKey(ui::VKEY_TAB);
  EXPECT_EQ(desk_bar_view->mini_views()[1]->desk_name_view(),
            GetHighlightedView());

  // Check for the new desk button.
  PressAndReleaseKey(ui::VKEY_RIGHT, ui::EF_COMMAND_DOWN);
  EXPECT_EQ(desk_bar_view->new_desk_button(), GetHighlightedView());
}

// Tests that tabbing with desk items and multiple displays works as expected.
// TODO(http://b/325335020): Port this test to `DesksOverviewFocusCyclerTest`.
TEST_P(DesksOverviewFocusCyclerOldTest, TabbingMultiDisplay) {
  UpdateDisplay("600x400,600x400,600x400");
  std::vector<raw_ptr<aura::Window, VectorExperimental>> roots =
      Shell::GetAllRootWindows();
  ASSERT_EQ(3u, roots.size());

  // Create two windows on the first display, and one each on the second and
  // third displays.
  std::unique_ptr<aura::Window> window1(CreateAppWindow(gfx::Rect(200, 200)));
  std::unique_ptr<aura::Window> window2(CreateAppWindow(gfx::Rect(200, 200)));
  std::unique_ptr<aura::Window> window3(
      CreateAppWindow(gfx::Rect(600, 0, 200, 200)));
  std::unique_ptr<aura::Window> window4(
      CreateAppWindow(gfx::Rect(1200, 0, 200, 200)));
  ASSERT_EQ(roots[0], window1->GetRootWindow());
  ASSERT_EQ(roots[0], window2->GetRootWindow());
  ASSERT_EQ(roots[1], window3->GetRootWindow());
  ASSERT_EQ(roots[2], window4->GetRootWindow());

  ToggleOverview();
  const auto* desk_bar_view1 = GetDesksBarViewForRoot(roots[0]);
  EXPECT_EQ(2u, desk_bar_view1->mini_views().size());

  // Tests that tabbing initially will go through the two overview items on the
  // first display.
  PressAndReleaseKey(ui::VKEY_TAB);
  auto* item2 = GetOverviewItemForWindow(window2.get())
                    ->GetLeafItemForWindow(window2.get());
  EXPECT_EQ(item2->overview_item_view(), GetHighlightedView());
  PressAndReleaseKey(ui::VKEY_TAB);
  auto* item1 = GetOverviewItemForWindow(window1.get())
                    ->GetLeafItemForWindow(window1.get());
  EXPECT_EQ(item1->overview_item_view(), GetHighlightedView());

  // Tests that further tabbing will go through the desk preview views,  desk
  // name views, the new desk button, and finally the desks templates button on
  // the first display.
  PressAndReleaseKey(ui::VKEY_TAB);
  EXPECT_EQ(desk_bar_view1->mini_views()[0]->desk_preview(),
            GetHighlightedView());
  PressAndReleaseKey(ui::VKEY_TAB);
  EXPECT_EQ(desk_bar_view1->mini_views()[0]
                ->desk_action_view()
                ->combine_desks_button(),
            GetHighlightedView());
  PressAndReleaseKey(ui::VKEY_TAB);
  EXPECT_EQ(
      desk_bar_view1->mini_views()[0]->desk_action_view()->close_all_button(),
      GetHighlightedView());
  PressAndReleaseKey(ui::VKEY_TAB);
  EXPECT_EQ(desk_bar_view1->mini_views()[0]->desk_name_view(),
            GetHighlightedView());

  PressAndReleaseKey(ui::VKEY_TAB);
  EXPECT_EQ(desk_bar_view1->mini_views()[1]->desk_preview(),
            GetHighlightedView());
  PressAndReleaseKey(ui::VKEY_TAB);
  EXPECT_EQ(
      desk_bar_view1->mini_views()[1]->desk_action_view()->close_all_button(),
      GetHighlightedView());
  PressAndReleaseKey(ui::VKEY_TAB);
  EXPECT_EQ(desk_bar_view1->mini_views()[1]->desk_name_view(),
            GetHighlightedView());
  PressAndReleaseKey(ui::VKEY_TAB);

  EXPECT_EQ(desk_bar_view1->new_desk_button(), GetHighlightedView());
  if (AreDeskTemplatesEnabled()) {
    PressAndReleaseKey(ui::VKEY_TAB);
    EXPECT_EQ(desk_bar_view1->overview_grid()->GetSaveDeskAsTemplateButton(),
              GetHighlightedView());
  }
  PressAndReleaseKey(ui::VKEY_TAB);
  EXPECT_EQ(desk_bar_view1->overview_grid()->GetSaveDeskForLaterButton(),
            GetHighlightedView());

  // Tests that the next tab will bring us to the first overview item on the
  // second display.
  PressAndReleaseKey(ui::VKEY_TAB);
  auto* item3 = GetOverviewItemForWindow(window3.get())
                    ->GetLeafItemForWindow(window3.get());
  EXPECT_EQ(item3->overview_item_view(), GetHighlightedView());

  PressAndReleaseKey(ui::VKEY_TAB);
  const auto* desk_bar_view2 = GetDesksBarViewForRoot(roots[1]);
  EXPECT_EQ(desk_bar_view2->mini_views()[0]->desk_preview(),
            GetHighlightedView());

  // Tab through all items on the second display.
  PressAndReleaseKey(ui::VKEY_TAB);
  PressAndReleaseKey(ui::VKEY_TAB);
  PressAndReleaseKey(ui::VKEY_TAB);
  PressAndReleaseKey(ui::VKEY_TAB);
  PressAndReleaseKey(ui::VKEY_TAB);
  PressAndReleaseKey(ui::VKEY_TAB);
  PressAndReleaseKey(ui::VKEY_TAB);
  EXPECT_EQ(desk_bar_view2->new_desk_button(), GetHighlightedView());
  if (AreDeskTemplatesEnabled()) {
    PressAndReleaseKey(ui::VKEY_TAB);
    EXPECT_EQ(desk_bar_view2->overview_grid()->GetSaveDeskAsTemplateButton(),
              GetHighlightedView());
  }
  PressAndReleaseKey(ui::VKEY_TAB);
  EXPECT_EQ(desk_bar_view2->overview_grid()->GetSaveDeskForLaterButton(),
            GetHighlightedView());

  // Tests that after tabbing through the items on the second display, the
  // next tab will bring us to the first overview item on the third display.
  PressAndReleaseKey(ui::VKEY_TAB);
  auto* item4 = GetOverviewItemForWindow(window4.get())
                    ->GetLeafItemForWindow(window4.get());
  EXPECT_EQ(item4->overview_item_view(), GetHighlightedView());

  PressAndReleaseKey(ui::VKEY_TAB);
  const auto* desk_bar_view3 = GetDesksBarViewForRoot(roots[2]);
  EXPECT_EQ(desk_bar_view3->mini_views()[0]->desk_preview(),
            GetHighlightedView());

  // Tab through all items on the third display.
  PressAndReleaseKey(ui::VKEY_TAB);
  PressAndReleaseKey(ui::VKEY_TAB);
  PressAndReleaseKey(ui::VKEY_TAB);
  PressAndReleaseKey(ui::VKEY_TAB);
  PressAndReleaseKey(ui::VKEY_TAB);
  PressAndReleaseKey(ui::VKEY_TAB);
  PressAndReleaseKey(ui::VKEY_TAB);
  EXPECT_EQ(desk_bar_view3->new_desk_button(), GetHighlightedView());
  if (AreDeskTemplatesEnabled()) {
    PressAndReleaseKey(ui::VKEY_TAB);
    EXPECT_EQ(desk_bar_view3->overview_grid()->GetSaveDeskAsTemplateButton(),
              GetHighlightedView());
  }
  PressAndReleaseKey(ui::VKEY_TAB);
  EXPECT_EQ(desk_bar_view3->overview_grid()->GetSaveDeskForLaterButton(),
            GetHighlightedView());

  // Tests that after tabbing through the items on the third display, the next
  // tab will bring us to the first overview item on the first display.
  PressAndReleaseKey(ui::VKEY_TAB);
  EXPECT_EQ(item2->overview_item_view(), GetHighlightedView());
}

TEST_P(DesksOverviewFocusCyclerOldTest, ActivateHighlightOnMiniView) {
  // We are initially on desk 1.
  const auto* desks_controller = DesksController::Get();
  auto& desks = desks_controller->desks();
  ASSERT_EQ(desks_controller->active_desk(), desks[0].get());

  ToggleOverview();
  const auto* desk_bar_view =
      GetDesksBarViewForRoot(Shell::GetPrimaryRootWindow());

  // Use keyboard to navigate to the preview view associated with desk 2.
  PressAndReleaseKey(ui::VKEY_TAB);
  PressAndReleaseKey(ui::VKEY_TAB);
  PressAndReleaseKey(ui::VKEY_TAB);
  PressAndReleaseKey(ui::VKEY_TAB);
  ASSERT_EQ(desk_bar_view->mini_views()[1]->desk_preview(),
            GetHighlightedView());

  // Tests that after hitting the return key on the focused preview view
  // associated with desk 2, we switch to desk 2.
  DeskSwitchAnimationWaiter waiter;
  PressAndReleaseKey(ui::VKEY_RETURN);
  waiter.Wait();
  EXPECT_EQ(desks_controller->active_desk(), desks[1].get());
}

TEST_P(DesksOverviewFocusCyclerOldTest, CloseHighlightOnMiniView) {
  const auto* desks_controller = DesksController::Get();
  ASSERT_EQ(2u, desks_controller->desks().size());
  auto* desk1 = desks_controller->GetDeskAtIndex(0);
  auto* desk2 = desks_controller->GetDeskAtIndex(1);
  ASSERT_EQ(desk1, desks_controller->active_desk());

  ToggleOverview();
  const auto* desk_bar_view =
      GetDesksBarViewForRoot(Shell::GetPrimaryRootWindow());
  auto* mini_view2 = desk_bar_view->mini_views()[1].get();

  // Use keyboard to navigate to the miniview associated with desk 2.
  PressAndReleaseKey(ui::VKEY_TAB);
  PressAndReleaseKey(ui::VKEY_TAB);
  PressAndReleaseKey(ui::VKEY_TAB);
  PressAndReleaseKey(ui::VKEY_TAB);
  ASSERT_EQ(mini_view2->desk_preview(), GetHighlightedView());

  // Tests that after hitting ctrl-w on the focused preview view associated with
  // `desk2`, `desk2` is destroyed.
  PressAndReleaseKey(ui::VKEY_W, ui::EF_CONTROL_DOWN);
  EXPECT_EQ(1u, desks_controller->desks().size());
  EXPECT_NE(desk2, desks_controller->GetDeskAtIndex(0));

  // Desks bar never goes back to zero state after it's initialized.
  EXPECT_FALSE(desk_bar_view->IsZeroState());
  EXPECT_FALSE(desk_bar_view->mini_views().empty());
}

TEST_P(DesksOverviewFocusCyclerOldTest, ActivateDeskNameView) {
  ToggleOverview();
  const auto* desk_bar_view =
      GetDesksBarViewForRoot(Shell::GetPrimaryRootWindow());
  auto* desk_name_view_1 = desk_bar_view->mini_views()[0]->desk_name_view();

  // Tab until the desk name view of the first desk is focused.
  PressAndReleaseKey(ui::VKEY_TAB);
  PressAndReleaseKey(ui::VKEY_TAB);
  PressAndReleaseKey(ui::VKEY_TAB);
  EXPECT_EQ(desk_name_view_1, GetHighlightedView());

  // Press enter and expect that the desk name is being edited.
  PressAndReleaseKey(ui::VKEY_RETURN);
  EXPECT_TRUE(desk_name_view_1->HasFocus());
  EXPECT_TRUE(desk_bar_view->IsDeskNameBeingModified());

  // All should be selected.
  EXPECT_TRUE(desk_name_view_1->HasSelection());
  const auto* desks_controller = DesksController::Get();
  auto* desk_1 = desks_controller->GetDeskAtIndex(0);
  EXPECT_EQ(desk_1->name(), desk_name_view_1->GetSelectedText());

  // Arrow keys should not change neither the focus nor the focus ring.
  PressAndReleaseKey(ui::VKEY_RIGHT);
  PressAndReleaseKey(ui::VKEY_RIGHT);
  PressAndReleaseKey(ui::VKEY_RIGHT);
  PressAndReleaseKey(ui::VKEY_LEFT);
  EXPECT_EQ(desk_name_view_1, GetHighlightedView());
  EXPECT_TRUE(desk_name_view_1->HasFocus());

  // Select all and delete.
  PressAndReleaseKey(ui::VKEY_A, ui::EF_CONTROL_DOWN);
  PressAndReleaseKey(ui::VKEY_BACK);
  // Type "code" and hit Tab, this should commit the changes and move the
  // focus ring to the next item.
  PressAndReleaseKey(ui::VKEY_C);
  PressAndReleaseKey(ui::VKEY_O);
  PressAndReleaseKey(ui::VKEY_D);
  PressAndReleaseKey(ui::VKEY_E);
  PressAndReleaseKey(ui::VKEY_TAB);

  EXPECT_FALSE(desk_name_view_1->HasFocus());
  EXPECT_EQ(desk_bar_view->mini_views()[1]->desk_preview(),
            GetHighlightedView());
  EXPECT_EQ(u"code", desk_1->name());
  EXPECT_TRUE(desk_1->is_name_set_by_user());
}

TEST_P(DesksOverviewFocusCyclerOldTest, RemoveDeskWhileNameIsHighlighted) {
  ToggleOverview();
  const auto* desk_bar_view =
      GetDesksBarViewForRoot(Shell::GetPrimaryRootWindow());
  auto* desk_name_view_1 = desk_bar_view->mini_views()[0]->desk_name_view();

  // Tab until the desk name view of the first desk is focused.
  PressAndReleaseKey(ui::VKEY_TAB);
  PressAndReleaseKey(ui::VKEY_TAB);
  PressAndReleaseKey(ui::VKEY_TAB);
  EXPECT_EQ(desk_name_view_1, GetHighlightedView());

  // Desks bar never goes back to zero state after it's initialized.
  const auto* desks_controller = DesksController::Get();
  auto* desk_1 = desks_controller->GetDeskAtIndex(0);
  RemoveDesk(desk_1);
  EXPECT_EQ(nullptr, GetHighlightedView());
  EXPECT_FALSE(desk_bar_view->IsZeroState());

  // Tabbing again should cause no crashes.
  PressAndReleaseKey(ui::VKEY_TAB);
  EXPECT_EQ(desk_bar_view->mini_views()[0]->desk_preview(),
            GetHighlightedView());
}

// Tests the overview focus cycler behavior when a user uses the new desk
// button.
TEST_P(DesksOverviewFocusCyclerOldTest, ActivateCloseHighlightOnNewDeskButton) {
  // Make sure the display is large enough to hold the max number of desks.
  UpdateDisplay("1200x800");
  ToggleOverview();
  const auto* desk_bar_view =
      GetDesksBarViewForRoot(Shell::GetPrimaryRootWindow());
  ASSERT_FALSE(desk_bar_view->IsZeroState());
  const views::LabelButton* new_desk_button = desk_bar_view->new_desk_button();
  const auto* desks_controller = DesksController::Get();

  auto check_name_view_at_index = [this, desks_controller](
                                      const auto* desk_bar_view, int index) {
    const auto* desk_name_view =
        desk_bar_view->mini_views()[index]->desk_name_view();
    EXPECT_TRUE(desk_name_view->HasFocus());
    if (desks_controller->CanCreateDesks()) {
      EXPECT_EQ(GetHighlightedView(), desk_name_view);
    }
    EXPECT_EQ(std::u16string(), desk_name_view->GetText());
  };

  // Use the keyboard to navigate to the new desk button.
  PressAndReleaseKey(ui::VKEY_TAB);
  PressAndReleaseKey(ui::VKEY_TAB);
  PressAndReleaseKey(ui::VKEY_TAB);
  PressAndReleaseKey(ui::VKEY_TAB);
  PressAndReleaseKey(ui::VKEY_TAB);
  PressAndReleaseKey(ui::VKEY_TAB);
  PressAndReleaseKey(ui::VKEY_TAB);
  ASSERT_EQ(new_desk_button, GetHighlightedView()->GetView());

  // Keep adding new desks until we reach the maximum allowed amount. Verify the
  // amount of desks is indeed the maximum allowed and that the new desk button
  // is disabled.
  while (desks_controller->CanCreateDesks()) {
    PressAndReleaseKey(ui::VKEY_RETURN);
    check_name_view_at_index(desk_bar_view,
                             desks_controller->desks().size() - 1);
    PressAndReleaseKey(ui::VKEY_TAB);
  }
  EXPECT_FALSE(new_desk_button->GetEnabled());
  EXPECT_EQ(desks_util::GetMaxNumberOfDesks(),
            desks_controller->desks().size());
}

TEST_P(DesksOverviewFocusCyclerOldTest, ZeroStateOfDesksBar) {
  ToggleOverview();
  auto* desks_bar_view = GetDesksBarViewForRoot(Shell::GetPrimaryRootWindow());
  ASSERT_FALSE(desks_bar_view->IsZeroState());
  ASSERT_EQ(2u, desks_bar_view->mini_views().size());

  // Remove one desk to enter zero state desks bar.
  auto* mini_view = desks_bar_view->mini_views()[1].get();
  GetEventGenerator()->MoveMouseTo(
      mini_view->GetBoundsInScreen().CenterPoint());
  EXPECT_TRUE(GetDeskActionVisibilityForMiniView(mini_view));
  LeftClickOn(GetCloseDeskButtonForMiniView(mini_view));

  // Desks bar never goes back to zero state after it's initialized.
  ASSERT_FALSE(desks_bar_view->IsZeroState());

  // Both zero state default desk button and zero state new desk button can be
  // focused in overview mode.
  PressAndReleaseKey(ui::VKEY_TAB);
  EXPECT_EQ(desks_bar_view->mini_views()[0]->desk_preview(),
            GetHighlightedView());
  PressAndReleaseKey(ui::VKEY_TAB);
  EXPECT_EQ(desks_bar_view->mini_views()[0]->desk_name_view(),
            GetHighlightedView());

  EXPECT_EQ(desks_bar_view->mini_views()[0]->desk_name_view(),
            GetHighlightedView());
  ToggleOverview();

  // Trigger the zero state new desk button will focus on the new created desk's
  // name view.
  ToggleOverview();
  EXPECT_TRUE(OverviewController::Get()->InOverviewSession());
  desks_bar_view = GetOverviewSession()
                       ->GetGridWithRootWindow(Shell::GetPrimaryRootWindow())
                       ->desks_bar_view();
  EXPECT_TRUE(desks_bar_view->IsZeroState());
  PressAndReleaseKey(ui::VKEY_TAB);
  PressAndReleaseKey(ui::VKEY_TAB);
  EXPECT_EQ(desks_bar_view->new_desk_button(), GetHighlightedView());
  PressAndReleaseKey(ui::VKEY_RETURN);
  EXPECT_EQ(desks_bar_view->mini_views()[1]->desk_name_view(),
            GetHighlightedView());
}

TEST_P(DesksOverviewFocusCyclerOldTest, ActivateHighlightOnViewFocused) {
  // Set up an overview with 2 mini desk items.
  ToggleOverview();
  const auto* desk_bar_view =
      GetDesksBarViewForRoot(Shell::GetPrimaryRootWindow());
  CheckDeskBarViewSize(desk_bar_view, "initial");
  EXPECT_EQ(2u, desk_bar_view->mini_views().size());

  // Tab to first mini desk view's preview view.
  PressAndReleaseKey(ui::VKEY_TAB);
  ASSERT_EQ(desk_bar_view->mini_views()[0]->desk_preview(),
            GetHighlightedView());
  CheckDeskBarViewSize(desk_bar_view, "overview item");

  // Click on the second mini desk item's name view.
  auto* event_generator = GetEventGenerator();
  auto* desk_name_view_1 = desk_bar_view->mini_views()[1]->desk_name_view();
  event_generator->MoveMouseTo(
      desk_name_view_1->GetBoundsInScreen().CenterPoint());
  event_generator->ClickLeftButton();
  EXPECT_FALSE(desk_bar_view->IsZeroState());

  // Verify that focus has moved to the clicked desk item.
  EXPECT_EQ(desk_name_view_1, GetHighlightedView());
  EXPECT_TRUE(desk_name_view_1->HasFocus());
}

// Tests that there is no crash when tabbing after we switch to the zero state
// desks bar. Regression test for https://crbug.com/1301134.
TEST_P(DesksOverviewFocusCyclerOldTest, SwitchingToZeroStateWhileTabbing) {
  ToggleOverview();
  auto* desks_bar_view = GetDesksBarViewForRoot(Shell::GetPrimaryRootWindow());
  ASSERT_FALSE(desks_bar_view->IsZeroState());
  ASSERT_EQ(2u, desks_bar_view->mini_views().size());

  // Tab to first mini desk view's preview view.
  PressAndReleaseKey(ui::VKEY_TAB);
  ASSERT_EQ(desks_bar_view->mini_views()[0]->desk_preview(),
            GetHighlightedView());

  // Remove one desk to have only one desk left.
  auto* mini_view = desks_bar_view->mini_views()[1].get();
  GetEventGenerator()->MoveMouseTo(
      mini_view->GetBoundsInScreen().CenterPoint());
  ASSERT_TRUE(GetDeskActionVisibilityForMiniView(mini_view));
  LeftClickOn(GetCloseDeskButtonForMiniView(mini_view));

  // Desks bar never goes back to zero state after it's initialized.
  ASSERT_FALSE(desks_bar_view->IsZeroState());

  // Try tabbing after removing the second desk triggers us to transition to
  // zero state desks bar. There should not be a crash.
  PressAndReleaseKey(ui::VKEY_TAB);
}

INSTANTIATE_TEST_SUITE_P(All, OverviewFocusCyclerOldTest, testing::Bool());
INSTANTIATE_TEST_SUITE_P(All, DesksOverviewFocusCyclerOldTest, testing::Bool());

}  // namespace ash
