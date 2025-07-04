// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/wm/overview/overview_focus_cycler_old.h"

#include "ash/accessibility/magnifier/docked_magnifier_controller.h"
#include "ash/accessibility/magnifier/magnifier_utils.h"
#include "ash/accessibility/scoped_a11y_override_window_setter.h"
#include "ash/shell.h"
#include "ash/style/icon_button.h"
#include "ash/wm/desks/desk_action_button.h"
#include "ash/wm/desks/desk_action_view.h"
#include "ash/wm/desks/desk_icon_button.h"
#include "ash/wm/desks/desk_mini_view.h"
#include "ash/wm/desks/desk_name_view.h"
#include "ash/wm/desks/desk_preview_view.h"
#include "ash/wm/desks/desk_profiles_button.h"
#include "ash/wm/desks/desks_controller.h"
#include "ash/wm/desks/overview_desk_bar_view.h"
#include "ash/wm/desks/templates/saved_desk_grid_view.h"
#include "ash/wm/desks/templates/saved_desk_item_view.h"
#include "ash/wm/desks/templates/saved_desk_library_view.h"
#include "ash/wm/desks/templates/saved_desk_name_view.h"
#include "ash/wm/overview/overview_focusable_view.h"
#include "ash/wm/overview/overview_grid.h"
#include "ash/wm/overview/overview_item.h"
#include "ash/wm/overview/overview_item_view.h"
#include "ash/wm/overview/overview_session.h"
#include "ash/wm/overview/overview_utils.h"
#include "ash/wm/splitview/faster_split_view_old.h"
#include "base/containers/contains.h"
#include "base/ranges/algorithm.h"
#include "chromeos/constants/chromeos_features.h"
#include "ui/views/view.h"

namespace ash {

namespace {

void AddDesksBarTraversableViews(
    OverviewGrid* grid,
    std::vector<OverviewFocusableView*>& out_traversable_views) {
  auto* bar_view = grid->desks_bar_view();
  if (!bar_view) {
    return;
  }

  // The desk items are always traversable from left to right, even in RTL
  // languages.
  if (bar_view->IsZeroState()) {
    out_traversable_views.push_back(bar_view->default_desk_button());
  } else {
    for (ash::DeskMiniView* mini_view : bar_view->mini_views()) {
      out_traversable_views.push_back(mini_view->desk_preview());
      if (auto* profiles_button = mini_view->desk_profiles_button()) {
        out_traversable_views.push_back(profiles_button);
      }
      auto* desk_action_view = mini_view->desk_action_view();
      if (desk_action_view->combine_desks_button()->CanShow()) {
        out_traversable_views.push_back(
            desk_action_view->combine_desks_button());
      }
      if (desk_action_view->close_all_button()->CanShow()) {
        out_traversable_views.push_back(desk_action_view->close_all_button());
      }
      out_traversable_views.push_back(mini_view->desk_name_view());
    }
  }
  auto* new_desk_button = bar_view->new_desk_button();
  if (new_desk_button->GetEnabled()) {
    out_traversable_views.push_back(new_desk_button);
  }

  if (auto* library_button = bar_view->library_button()) {
    if (library_button->GetVisible()) {
      out_traversable_views.push_back(library_button);
    }
  }
}

}  // namespace

OverviewFocusCyclerOld::OverviewFocusCyclerOld(OverviewSession* overview_session)
    : overview_session_(overview_session),
      scoped_a11y_overrider_(
          std::make_unique<ScopedA11yOverrideWindowSetter>()) {}

OverviewFocusCyclerOld::~OverviewFocusCyclerOld() = default;

void OverviewFocusCyclerOld::MoveFocus(bool reverse) {
  const std::vector<OverviewFocusableView*> traversable_views =
      GetTraversableViews();
  const int count = static_cast<int>(traversable_views.size());

  // |count| can be zero when there are no overview items and no desk views (eg.
  // "No recent items" or PIP windows are shown but they aren't traversable).
  if (count == 0) {
    return;
  }

  int index = 0;
  bool item_was_deleted = false;
  if (!focused_view_) {
    // Pick up where we left off if |deleted_index_| has a value.
    if (deleted_index_) {
      item_was_deleted = true;
      index = *deleted_index_ >= count ? 0 : *deleted_index_;
      deleted_index_.reset();
    } else if (reverse) {
      index = count - 1;
    }
  } else {
    auto it = base::ranges::find(traversable_views, focused_view_);
    DCHECK(it != traversable_views.end());
    const int current_index = std::distance(traversable_views.begin(), it);
    DCHECK_GE(current_index, 0);
    index = (((reverse ? -1 : 1) + current_index) + count) % count;
  }

  // If we are moving over either end of the list of traversible views and there
  // is an active toast with an undo button for desk removal  that can be
  // focused, then we unfocus any traversible views while the dismiss button
  // is focused.
  if (((index == 0 && !reverse) || (index == count - 1 && reverse)) &&
      !item_was_deleted &&
      DesksController::Get()
          ->MaybeToggleA11yHighlightOnUndoDeskRemovalToast()) {
    SetFocusVisibility(false);
    focused_view_ = nullptr;
    focused_view_tracker_.SetView(nullptr);
    return;
  }

  UpdateFocus(traversable_views[index]);
}

void OverviewFocusCyclerOld::UpdateA11yFocusWindow(
    OverviewFocusableView* name_view) {
  scoped_a11y_overrider_->MaybeUpdateA11yOverrideWindow(
      name_view->GetView()->GetWidget()->GetNativeWindow());
}

void OverviewFocusCyclerOld::MoveFocusToView(
    OverviewFocusableView* target_view,
    bool suppress_accessibility_event) {
  const std::vector<OverviewFocusableView*> traversable_views =
      GetTraversableViews();
  DCHECK(base::Contains(traversable_views, target_view));

  UpdateFocus(target_view, suppress_accessibility_event);
}

void OverviewFocusCyclerOld::OnViewDestroyingOrDisabling(
    OverviewFocusableView* view) {
  DCHECK(view);

  // TODO(afakhry): Refactor this code.
  const std::vector<OverviewFocusableView*> traversable_views =
      GetTraversableViews();
  const auto it = base::ranges::find(traversable_views, view);
  if (it == traversable_views.end()) {
    return;
  }

  const int view_index = std::distance(traversable_views.begin(), it);
  DCHECK_GE(view_index, 0);

  if (view != focused_view_) {
    if (!deleted_index_) {
      return;
    }

    // We need to update the `deleted_index_` in case the destroying view
    // resides before a previously removed focused view in the traversal
    // order.
    if (view_index < *deleted_index_) {
      deleted_index_ = std::max(0, --(*deleted_index_));
    }
    return;
  }

  deleted_index_ = view_index;
  focused_view_->SetFocused(false);
  focused_view_ = nullptr;
  focused_view_tracker_.SetView(nullptr);
}

void OverviewFocusCyclerOld::SetFocusVisibility(bool visible) {
  if (focused_view_) {
    focused_view_->SetFocused(visible);
  }
}

bool OverviewFocusCyclerOld::IsFocusVisible() const {
  return focused_view_ && focused_view_->is_focused();
}

bool OverviewFocusCyclerOld::MaybeActivateFocusedView() {
  if (DesksController::Get()
          ->MaybeActivateDeskRemovalUndoButtonOnHighlightedToast()) {
    return true;
  }

  if (!focused_view_) {
    return false;
  }

  focused_view_->MaybeActivateFocusedView();
  return true;
}

bool OverviewFocusCyclerOld::MaybeCloseFocusedView(bool primary_action) {
  if (!focused_view_) {
    return false;
  }

  focused_view_->MaybeCloseFocusedView(primary_action);
  return true;
}

bool OverviewFocusCyclerOld::MaybeSwapFocusedView(bool right) {
  if (!focused_view_) {
    return false;
  }

  focused_view_->MaybeSwapFocusedView(right);
  return true;
}

bool OverviewFocusCyclerOld::MaybeActivateFocusedViewOnOverviewExit() {
  return focused_view_ && focused_view_->MaybeActivateFocusedViewOnOverviewExit(
                              overview_session_);
}

OverviewItemBase* OverviewFocusCyclerOld::GetFocusedItem() const {
  return focused_view_ ? focused_view_->GetOverviewItem() : nullptr;
}

void OverviewFocusCyclerOld::ResetFocusedView() {
  if (!focused_view_) {
    return;
  }

  deleted_index_.reset();
  focused_view_->SetFocused(false);
  focused_view_ = nullptr;
  focused_view_tracker_.SetView(nullptr);
}

std::vector<OverviewFocusableView*> OverviewFocusCyclerOld::GetTraversableViews()
    const {
  std::vector<OverviewFocusableView*> traversable_views;
  traversable_views.reserve(32);  // Conservative default.

  // Note that this order matches the order of the chromevox cycling in
  // `OverviewSession::UpdateAccessibilityFocus()`.
  for (auto& grid : overview_session_->grid_list()) {
    // If the saved desk library is visible, we shouldn't try to add any
    // overview items.
    if (grid->IsShowingSavedDeskLibrary()) {
      SavedDeskLibraryView* desk_library_view = grid->GetSavedDeskLibraryView();
      DCHECK(desk_library_view);
      for (SavedDeskGridView* saved_desk_grid_view :
           desk_library_view->grid_views()) {
        for (SavedDeskItemView* saved_desk_item :
             saved_desk_grid_view->grid_items()) {
          traversable_views.push_back(saved_desk_item);

          // Admin templates names cannot be edited or focused.
          SavedDeskNameView* name_view = saved_desk_item->name_view();
          if (name_view->IsFocusable()) {
            traversable_views.push_back(name_view);
          }
        }
      }
    } else {
      for (const auto& item : grid->window_list()) {
        for (auto* focusable_view : item->GetFocusableViews()) {
          traversable_views.push_back(focusable_view);
        }
      }
    }

    // UI elements in faster split screen partial overview will be traversed
    // right after the overview items.
    if (auto* faster_split_view = grid->GetFasterSplitViewOld()) {
      traversable_views.push_back(faster_split_view->GetToast());
      traversable_views.push_back(faster_split_view->settings_button());
    }

    AddDesksBarTraversableViews(grid.get(), traversable_views);

    if (grid->IsSaveDeskAsTemplateButtonVisible()) {
      traversable_views.push_back(grid->GetSaveDeskAsTemplateButton());
    }
    if (grid->IsSaveDeskForLaterButtonVisible()) {
      traversable_views.push_back(grid->GetSaveDeskForLaterButton());
    }
  }
  return traversable_views;
}

void OverviewFocusCyclerOld::UpdateFocus(
    OverviewFocusableView* view_to_be_focused,
    bool suppress_accessibility_event) {
  if (focused_view_ == view_to_be_focused) {
    return;
  }

  OverviewFocusableView* previous_view = focused_view_;
  focused_view_ = view_to_be_focused;
  focused_view_tracker_.SetView(view_to_be_focused->GetView());
  focused_view_tracker_.SetIsDeletingCallback(base::BindOnce(
      &OverviewFocusCyclerOld::OnFocusedViewDeleting, base::Unretained(this)));

  // Perform accessibility related tasks.
  if (!suppress_accessibility_event) {
    // Don't emit if focusing since focusing will emit an accessibility event as
    // well.
    scoped_a11y_overrider_->MaybeUpdateA11yOverrideWindow(
        focused_view_->GetView()->GetWidget()->GetNativeWindow());
    focused_view_->GetView()->NotifyAccessibilityEvent(
        ax::mojom::Event::kSelection, true);
  }
  // The overview "focus" works differently from regular focusing so we need to
  // update the magnifier manually here.
  magnifier_utils::MaybeUpdateActiveMagnifierFocus(
      focused_view_->GetMagnifierFocusPointInScreen());

  if (previous_view) {
    previous_view->SetFocused(false);
  }
  focused_view_->SetFocused(true);
}

void OverviewFocusCyclerOld::OnFocusedViewDeleting() {
  focused_view_ = nullptr;
}

}  // namespace ash
