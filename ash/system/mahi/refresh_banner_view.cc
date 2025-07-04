// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/system/mahi/refresh_banner_view.h"

#include <string>

#include "ash/strings/grit/ash_strings.h"
#include "ash/style/icon_button.h"
#include "ash/style/typography.h"
#include "ash/system/mahi/mahi_constants.h"
#include "base/check_is_test.h"
#include "base/metrics/histogram_functions.h"
#include "base/strings/string_util.h"
#include "base/time/time.h"
#include "chromeos/components/mahi/public/cpp/mahi_manager.h"
#include "components/vector_icons/vector_icons.h"
#include "third_party/skia/include/core/SkPath.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/chromeos/styles/cros_tokens_color_mappings.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/layer_animator.h"
#include "ui/gfx/text_constants.h"
#include "ui/views/animation/animation_builder.h"
#include "ui/views/background.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/flex_layout_types.h"
#include "ui/views/layout/flex_layout_view.h"
#include "ui/views/layout/layout_types.h"
#include "ui/views/view_class_properties.h"

namespace ash {

namespace {

constexpr int kRefreshBannerCornerRadius = 20;
constexpr base::TimeDelta kRefreshBannerAnimationDurationMs =
    base::Milliseconds(150);
constexpr gfx::Insets kRefreshBannerInteriorMargin =
    gfx::Insets::TLBR(4, 20, mahi_constants::kRefreshBannerStackDepth + 4, 20);
constexpr gfx::Insets kTitleLabelMargin = gfx::Insets::TLBR(0, 0, 0, 8);

SkPath GetClipPath(gfx::Size size) {
  int width = size.width();
  int height = size.height();

  auto top_left = SkPoint::Make(0, 0);
  auto top_right = SkPoint::Make(width, 0);
  auto bottom_left = SkPoint::Make(0, height);
  auto bottom_right = SkPoint::Make(width, height);
  int radius = kRefreshBannerCornerRadius;
  int bottom_radius = mahi_constants::kPanelCornerRadius;

  const auto horizontal_offset = SkPoint::Make(radius, 0.f);
  const auto vertical_offset = SkPoint::Make(0.f, radius);
  const auto bottom_vertical_offset =
      SkPoint::Make(0.f, mahi_constants::kRefreshBannerStackDepth - 1);
  const auto bottom_horizontal_offset = SkPoint::Make(bottom_radius, 0.f);

  return SkPath()
      // Start just before the curve of the top-left corner.
      .moveTo(radius, 0.f)
      // Draw the top-left rounded corner.
      .arcTo(top_left, top_left + vertical_offset, radius)
      // Draw the bottom-left rounded corner and the vertical line
      // connecting it to the top-left corner.
      .lineTo(bottom_left)
      .arcTo(bottom_left - bottom_vertical_offset,
             bottom_left - bottom_vertical_offset + bottom_horizontal_offset,
             radius)
      // Draw the bottom-right rounded corner and the horizontal line
      // connecting it to the bottom-left corner.
      .arcTo(bottom_right - bottom_vertical_offset, bottom_right, bottom_radius)
      .lineTo(bottom_right)
      .arcTo(top_right, top_right - horizontal_offset, bottom_radius)
      .lineTo(radius, 0.f)
      .close();
}

}  // namespace

RefreshBannerView::RefreshBannerView(MahiUiController* ui_controller)
    : MahiUiController::Delegate(ui_controller), ui_controller_(ui_controller) {
  CHECK(ui_controller_);

  SetOrientation(views::LayoutOrientation::kHorizontal);
  SetInteriorMargin(kRefreshBannerInteriorMargin);
  SetID(mahi_constants::ViewId::kRefreshView);
  SetBackground(views::CreateThemedRoundedRectBackground(
      cros_tokens::kCrosSysSystemPrimaryContainer, /*radius=*/0));
  SetVisible(false);

  // We need to paint this view to a layer for animations.
  SetPaintToLayer();

  auto* const manager = chromeos::MahiManager::Get();

  AddChildView(
      views::Builder<views::Label>()
          .CopyAddressTo(&title_label_)
          .SetID(mahi_constants::ViewId::kBannerTitleLabel)
          .SetText(l10n_util::GetStringFUTF16(
              IDS_ASH_MAHI_REFRESH_BANNER_LABEL_TEXT,
              manager ? manager->GetContentTitle() : base::EmptyString16()))
          .SetAutoColorReadabilityEnabled(false)
          .SetEnabledColorId(cros_tokens::kCrosSysSystemOnPrimaryContainer)
          .SetFontList(TypographyProvider::Get()->ResolveTypographyToken(
              TypographyToken::kCrosAnnotation2))
          .SetHorizontalAlignment(gfx::ALIGN_LEFT)
          .SetProperty(
              views::kFlexBehaviorKey,
              views::FlexSpecification(views::MinimumFlexSizeRule::kScaleToZero,
                                       views::MaximumFlexSizeRule::kUnbounded))
          .SetProperty(views::kMarginsKey, kTitleLabelMargin)
          .Build());

  auto* icon_button =
      AddChildView(IconButton::Builder()
                       .SetViewId(mahi_constants::ViewId::kRefreshButton)
                       .SetCallback(base::BindRepeating(
                           [](MahiUiController* ui_controller) {
                             ui_controller->RefreshContents();
                             base::UmaHistogramEnumeration(
                                 mahi_constants::kMahiButtonClickHistogramName,
                                 mahi_constants::PanelButton::kRefreshButton);
                           },
                           // Using `base::Unretained()` is safe here since
                           // `ui_controller` outlives this `RefreshBannerView`.
                           base::Unretained(ui_controller)))
                       .SetVectorIcon(&vector_icons::kReloadChromeRefreshIcon)
                       .SetType(IconButton::Type::kSmallProminentFloating)
                       // TODO(b/319264190): Replace the a11y string.
                       .SetAccessibleName(u"Refresh contents")
                       .Build());
  icon_button->SetIconColor(cros_tokens::kCrosSysSystemOnPrimaryContainer);
  views::FocusRing::Get(icon_button)
      ->SetColorId(cros_tokens::kCrosSysSystemOnPrimaryContainer);
}

RefreshBannerView::~RefreshBannerView() = default;

void RefreshBannerView::Show() {
  auto* manager = chromeos::MahiManager::Get();
  if (!manager) {
    CHECK_IS_TEST();
    return;
  }

  title_label_->SetText(l10n_util::GetStringFUTF16(
      IDS_ASH_MAHI_REFRESH_BANNER_LABEL_TEXT, manager->GetContentTitle()));

  // Abort all running animations before showing the banner, to prevent fade
  // out animations from hiding the banner again right after it is shown.
  CHECK(layer());
  layer()->GetAnimator()->AbortAllAnimations();

  // TODO(b/331109652): Determine whether the banner should still animate if it
  // was already visible.
  SetVisible(true);
  layer()->SetOpacity(0.0);
  views::AnimationBuilder()
      .Once()
      .SetDuration(kRefreshBannerAnimationDurationMs)
      .SetOpacity(this, 1);
}

void RefreshBannerView::Hide() {
  if (GetVisible()) {
    views::AnimationBuilder()
        .OnEnded(base::BindOnce(
            [](const base::WeakPtr<views::View>& view) {
              if (view) {
                view->SetVisible(false);
              }
            },
            weak_ptr_factory_.GetWeakPtr()))
        .Once()
        .SetDuration(kRefreshBannerAnimationDurationMs)
        .SetOpacity(this, 0.0);
  }
}

void RefreshBannerView::OnBoundsChanged(const gfx::Rect& old_bounds) {
  SetClipPath(GetClipPath(GetContentsBounds().size()));
}

void RefreshBannerView::ViewHierarchyChanged(
    const views::ViewHierarchyChangedDetails& details) {
  // Make sure the refresh banner is always shown on top.
  if (layer() && layer()->parent()) {
    layer()->parent()->StackAtTop(layer());
  }
}

void RefreshBannerView::VisibilityChanged(View* starting_from,
                                          bool is_visible) {
  if (!is_visible || GetContentsBounds().size().IsZero()) {
    return;
  }
  SetClipPath(GetClipPath(GetContentsBounds().size()));
}

views::View* RefreshBannerView::GetView() {
  return this;
}

bool RefreshBannerView::GetViewVisibility(VisibilityState state) const {
  // Do not change visibility because visibility depends on the refresh
  // availability instead of `state`.
  return GetVisible();
}

void RefreshBannerView::OnUpdated(const MahiUiUpdate& update) {
  switch (update.type()) {
    case MahiUiUpdateType::kRefreshAvailabilityUpdated:
      if (update.GetRefreshAvailability()) {
        Show();
      } else {
        Hide();
      }
      return;
    case MahiUiUpdateType::kContentsRefreshInitiated:
      Hide();
      return;
    case MahiUiUpdateType::kErrorReceived:
    case MahiUiUpdateType::kAnswerLoaded:
    case MahiUiUpdateType::kOutlinesLoaded:
    case MahiUiUpdateType::kQuestionAndAnswerViewNavigated:
    case MahiUiUpdateType::kQuestionPosted:
    case MahiUiUpdateType::kQuestionReAsked:
    case MahiUiUpdateType::kSummaryLoaded:
    case MahiUiUpdateType::kSummaryAndOutlinesSectionNavigated:
    case MahiUiUpdateType::kSummaryAndOutlinesReloaded:
      return;
  }
}

BEGIN_METADATA(RefreshBannerView)
END_METADATA

}  // namespace ash
