// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/wm/window_restore/pine_controller.h"

#include "ash/birch/birch_model.h"
#include "ash/constants/ash_pref_names.h"
#include "ash/constants/ash_switches.h"
#include "ash/constants/notifier_catalogs.h"
#include "ash/display/screen_ash.h"
#include "ash/public/cpp/image_util.h"
#include "ash/public/cpp/resources/grit/ash_public_unscaled_resources.h"
#include "ash/public/cpp/system/anchored_nudge_data.h"
#include "ash/public/cpp/system/anchored_nudge_manager.h"
#include "ash/public/cpp/window_properties.h"
#include "ash/screen_util.h"
#include "ash/shell.h"
#include "ash/strings/grit/ash_strings.h"
#include "ash/style/ash_color_id.h"
#include "ash/style/dark_light_mode_controller_impl.h"
#include "ash/style/system_dialog_delegate_view.h"
#include "ash/system/toast/toast_manager_impl.h"
#include "ash/utility/forest_util.h"
#include "ash/wm/desks/desks_util.h"
#include "ash/wm/overview/overview_controller.h"
#include "ash/wm/overview/overview_grid.h"
#include "ash/wm/overview/overview_session.h"
#include "ash/wm/window_restore/informed_restore_contents_data.h"
#include "ash/wm/window_restore/pine_constants.h"
#include "ash/wm/window_restore/window_restore_metrics.h"
#include "ash/wm/window_restore/window_restore_util.h"
#include "ash/wm/window_util.h"
#include "base/command_line.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/task/task_traits.h"
#include "base/task/thread_pool.h"
#include "chromeos/ui/base/app_types.h"
#include "chromeos/ui/base/display_util.h"
#include "chromeos/ui/base/window_properties.h"
#include "components/prefs/pref_service.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/ui_base_types.h"
#include "ui/compositor/layer.h"
#include "ui/views/background.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/layout/flex_layout.h"
#include "ui/views/view_class_properties.h"

namespace ash {

namespace {

// The nudge will not be shown if it already been shown 3 times, or if 24 hours
// have not yet passed since it was last shown.
constexpr int kNudgeMaxShownCount = 3;
constexpr base::TimeDelta kNudgeTimeBetweenShown = base::Hours(24);

bool ShouldShowPineImage(const gfx::ImageSkia& pine_image) {
  if (pine_image.isNull()) {
    return false;
  }

  const gfx::Size image_size = pine_image.size();
  const bool is_image_landscape = image_size.width() > image_size.height();

  // TODO(minch|sammiequon): The pine dialog will only be shown inside the
  // primary display for now. Change the logic here if it changes.
  const display::Display display_with_pine =
      display::Screen::GetScreen()->GetPrimaryDisplay();
  const bool is_display_landscape = chromeos::IsLandscapeOrientation(
      chromeos::GetDisplayCurrentOrientation(display_with_pine));

  // Show the image only if the pine image and the display showing it both have
  // the same orientation.
  return is_image_landscape == is_display_landscape;
}

PrefService* GetActivePrefService() {
  return Shell::Get()->session_controller()->GetActivePrefService();
}

// Returns true if this is the first time login and we should show the informed
// restore onboarding message.
bool ShouldStartInformedRestoreOnboarding() {
  PrefService* prefs = GetActivePrefService();
  return prefs && prefs->GetBoolean(prefs::kShowInformedRestoreOnboarding);
}

}  // namespace

PineController::PineController() {
  Shell::Get()->overview_controller()->AddObserver(this);

  activation_change_observation_.Observe(Shell::Get()->activation_client());
}

PineController::~PineController() {
  Shell::Get()->overview_controller()->RemoveObserver(this);
}

void PineController::MaybeShowInformedRestoreOnboarding(bool restore_on) {
  if (onboarding_widget_ || !ShouldStartInformedRestoreOnboarding()) {
    return;
  }
  GetActivePrefService()->SetBoolean(prefs::kShowInformedRestoreOnboarding,
                                     false);

  auto dialog =
      views::Builder<SystemDialogDelegateView>()
          .SetTitleText(l10n_util::GetStringUTF16(
              restore_on
                  ? IDS_ASH_INFORMED_RESTORE_ONBOARDING_RESTORE_ON_TITLE
                  : IDS_ASH_INFORMED_RESTORE_ONBOARDING_RESTORE_OFF_TITLE))
          .SetDescription(l10n_util::GetStringUTF16(
              restore_on
                  ? IDS_ASH_INFORMED_RESTORE_ONBOARDING_RESTORE_ON_DESCRIPTION
                  : IDS_ASH_INFORMED_RESTORE_ONBOARDING_RESTORE_OFF_DESCRIPTION))
          .SetAcceptButtonText(l10n_util::GetStringUTF16(
              restore_on
                  ? IDS_ASH_INFORMED_RESTORE_ONBOARDING_RESTORE_ON_ACCEPT
                  : IDS_ASH_INFORMED_RESTORE_ONBOARDING_RESTORE_OFF_ACCEPT))
          .SetAcceptCallback(
              base::BindOnce(&PineController::OnOnboardingAcceptPressed,
                             base::Unretained(this), restore_on))
          .Build();

  // Since no additional view was set, the buttons will be center aligned.
  dialog->SetButtonContainerAlignment(views::LayoutAlignment::kCenter);
  dialog->SetLayoutManager(std::make_unique<views::FlexLayout>())
      ->SetOrientation(views::LayoutOrientation::kVertical)
      .SetMainAxisAlignment(views::LayoutAlignment::kCenter)
      .SetCrossAxisAlignment(views::LayoutAlignment::kCenter)
      .SetCollapseMargins(true);
  dialog->SetModalType(ui::MODAL_TYPE_SYSTEM);
  dialog->SetTopContentView(
      views::Builder<views::ImageView>()
          .SetImage(
              ui::ResourceBundle::GetSharedInstance().GetThemedLottieImageNamed(
                  IDR_INFORMED_RESTORE_ONBOARDING_IMAGE))
          .Build());
  dialog->SetProperty(
      views::kFlexBehaviorKey,
      views::FlexSpecification(views::MinimumFlexSizeRule::kPreferred,
                               views::MaximumFlexSizeRule::kUnbounded));
  if (restore_on) {
    // If the user had the restore pref set as "Ask every time", don't show the
    // Cancel button.
    dialog->SetCancelButtonVisible(false);
  } else {
    dialog->SetCancelButtonText(l10n_util::GetStringUTF16(
        IDS_ASH_INFORMED_RESTORE_ONBOARDING_RESTORE_OFF_CANCEL));
    // `this` is guaranteed to outlive the dialog.
    dialog->SetCancelCallback(base::BindOnce(
        &PineController::OnOnboardingCancelPressed, base::Unretained(this)));
  }

  views::Widget::InitParams params(
      views::Widget::InitParams::WIDGET_OWNS_NATIVE_WIDGET,
      views::Widget::InitParams::TYPE_WINDOW_FRAMELESS);
  params.name = "PineOnboardingWidget";
  params.delegate = dialog.release();

  onboarding_widget_ = std::make_unique<views::Widget>(std::move(params));
  onboarding_widget_->Show();
}

void PineController::MaybeStartPineOverviewSessionDevAccelerator() {
  auto data = std::make_unique<InformedRestoreContentsData>();
  data->last_session_crashed = false;
  std::pair<base::OnceClosure, base::OnceClosure> split =
      base::SplitOnceCallback(
          base::BindOnce(&PineController::MaybeEndPineOverviewSession,
                         weak_ptr_factory_.GetWeakPtr()));
  data->restore_callback = std::move(split.first);
  data->cancel_callback = std::move(split.second);

  // NOTE: Comment/uncomment the following apps locally, but avoid changes as to
  // reduce merge conflicts.
  // Chrome.
  data->apps_infos.emplace_back(
      "mgndgikekgjfcpckkfioiadnlibdjbkf", /*tab_title=*/"Reddit",
      std::vector<GURL>{
          GURL("https://www.cnn.com/"), GURL("https://www.reddit.com/"),
          GURL("https://www.youtube.com/"), GURL("https://www.waymo.com/"),
          GURL("https://www.google.com/")},
      /*tab_count=*/10u, /*lacros_profile_id=*/0);
  // PWA.
  data->apps_infos.emplace_back("kjgfgldnnfoeklkmfkjfagphfepbbdan", "Meet");

  // SWA.
  data->apps_infos.emplace_back("njfbnohfdkmbmnjapinfcopialeghnmh", "Camera");
  data->apps_infos.emplace_back("odknhmnlageboeamepcngndbggdpaobj", "Settings");
  data->apps_infos.emplace_back("fkiggjmkendpmbegkagpmagjepfkpmeb", "Files");
  data->apps_infos.emplace_back("oabkinaljpjeilageghcdlnekhphhphl",
                                "Calculator");

  data->apps_infos.emplace_back(
      "mgndgikekgjfcpckkfioiadnlibdjbkf", /*tab_title=*/"Maps",
      std::vector<GURL>{GURL("https://www.google.com/maps/")},
      /*tab_count=*/1, /*lacros_profile_id=*/0);
  data->apps_infos.emplace_back("fkiggjmkendpmbegkagpmagjepfkpmeb", "Files");
  data->apps_infos.emplace_back(
      "mgndgikekgjfcpckkfioiadnlibdjbkf", /*tab_title=*/"Twitter",
      std::vector<GURL>{GURL("https://www.twitter.com/"),
                        GURL("https://www.youtube.com/"),
                        GURL("https://www.google.com/")},
      /*tab_count=*/3u, /*lacros_profile_id=*/0);

  MaybeStartPineOverviewSession(std::move(data));
}

void PineController::MaybeStartPineOverviewSession(
    std::unique_ptr<InformedRestoreContentsData> contents_data) {
  CHECK(IsForestFeatureEnabled());

  if (OverviewController::Get()->InOverviewSession()) {
    return;
  }

  // TODO(hewer|sammiequon): This function should only be called once in
  // production code when `contents_data_` is null. It can be called multiple
  // times currently via dev accelerator. Remove this block when
  // `MaybeStartPineOverviewSessionDevAccelerator()` is removed.
  if (contents_data_) {
    StartPineOverviewSession();
    return;
  }

  contents_data_ = std::move(contents_data);

  // If this is the first time starting informed restore, show the onboarding
  // dialog instead. Informed restore session will be started if the user hits
  // 'Accept'.
  if (ShouldStartInformedRestoreOnboarding()) {
    MaybeShowInformedRestoreOnboarding(/*restore_on=*/true);
    return;
  }

  if (!contents_data_) {
    return;
  }

  RecordPineScreenshotDurations(Shell::Get()->local_state());
  image_util::DecodeImageFile(
      base::BindOnce(&PineController::OnPineImageDecoded,
                     weak_ptr_factory_.GetWeakPtr(), base::TimeTicks::Now()),
      GetShutdownPineImagePath(), data_decoder::mojom::ImageCodec::kPng);
}

void PineController::MaybeEndPineOverviewSession() {
  contents_data_.reset();
  OverviewController::Get()->EndOverview(OverviewEndAction::kAccelerator,
                                         OverviewEnterExitType::kNormal);
}

void PineController::OnOverviewModeEnding(OverviewSession* overview_session) {
  in_informed_restore_ = false;
  for (const auto& grid : overview_session->grid_list()) {
    if (grid->pine_widget()) {
      in_informed_restore_ = true;
      break;
    }
  }
}

void PineController::OnOverviewModeEndingAnimationComplete(bool canceled) {
  // If `canceled` is true, overview was reentered before the exit animations
  // were finished. `in_informed_restore_` will be reset the next time overview
  // ends.
  if (canceled || !in_informed_restore_) {
    return;
  }

  in_informed_restore_ = false;

  // In multi-user scenario, forest may have been available for the user that
  // started overview, but not for the current user. (Switching users ends
  // overview.)
  if (!IsForestFeatureEnabled()) {
    return;
  }

  PrefService* prefs = GetActivePrefService();
  if (!prefs) {
    return;
  }

  // Nudge has already been shown three times. No need to educate anymore.
  const int shown_count =
      prefs->GetInteger(prefs::kInformedRestoreNudgeShownCount);
  if (shown_count >= kNudgeMaxShownCount) {
    return;
  }

  // Nudge has been shown within the last 24 hours already.
  base::Time now = base::Time::Now();
  if (now - prefs->GetTime(prefs::kInformedRestoreNudgeLastShown) <
      kNudgeTimeBetweenShown) {
    return;
  }

  AnchoredNudgeData nudge_data(
      pine::kSuggestionsNudgeId,
      NudgeCatalogName::kInformedRestoreEducationNudge,
      l10n_util::GetStringUTF16(IDS_ASH_INFORMED_RESTORE_EDUCATION_NUDGE));
  nudge_data.image_model =
      ui::ResourceBundle::GetSharedInstance().GetThemedLottieImageNamed(
          DarkLightModeControllerImpl::Get()->IsDarkModeEnabled()
              ? IDR_INFORMED_RESTORE_NUDGE_IMAGE_DM
              : IDR_INFORMED_RESTORE_NUDGE_IMAGE_LM);
  nudge_data.fill_image_size = true;
  AnchoredNudgeManager::Get()->Show(nudge_data);

  prefs->SetInteger(prefs::kInformedRestoreNudgeShownCount, shown_count + 1);
  prefs->SetTime(prefs::kInformedRestoreNudgeLastShown, now);
}

void PineController::OnWindowActivated(ActivationReason reason,
                                       aura::Window* gained_active,
                                       aura::Window* lost_active) {
  if (gained_active && window_util::IsWindowUserPositionable(gained_active) &&
      gained_active->GetProperty(chromeos::kAppTypeKey) !=
          chromeos::AppType::NON_APP) {
    contents_data_.reset();
  }
}

void PineController::OnPineImageDecoded(base::TimeTicks start_time,
                                        const gfx::ImageSkia& pine_image) {
  CHECK(contents_data_);
  RecordScreenshotDecodeDuration(base::TimeTicks::Now() - start_time);

  if (ShouldShowPineImage(pine_image)) {
    contents_data_->image = pine_image;
  } else {
    RecordScreenshotOnShutdownStatus(
        ScreenshotOnShutdownStatus::kFailedOnDifferentOrientations);
  }
  // Delete the pine image from the disk to avoid stale screenshot on next
  // time showing the dialog.
  base::ThreadPool::PostTask(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::HIGHEST},
      base::BindOnce(base::IgnoreResult(&base::DeleteFile),
                     GetShutdownPineImagePath()));

  StartPineOverviewSession();
}

void PineController::StartPineOverviewSession() {
  if (base::CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kForceBirchFetch)) {
    LOG(WARNING) << "Forcing Birch data fetch";
    Shell::Get()->birch_model()->RequestBirchDataFetch(
        /*is_post_login=*/false, base::BindOnce([]() {
          // Dump the items that were fetched.
          LOG(WARNING) << "All items:";
          auto all_items = Shell::Get()->birch_model()->GetAllItems();
          for (const auto& item : all_items) {
            LOG(WARNING) << item->ToString();
          }
          // Dump the items for display.
          LOG(WARNING) << "Items for display:";
          auto display_items =
              Shell::Get()->birch_model()->GetItemsForDisplay();
          for (const auto& item : display_items) {
            LOG(WARNING) << item->ToString();
          }
        }));
  }
  // TODO(sammiequon): Add a new start action for this type of overview session.
  OverviewController::Get()->StartOverview(OverviewStartAction::kAccelerator,
                                           OverviewEnterExitType::kPine);
}

void PineController::OnOnboardingAcceptPressed(bool restore_on) {
  // Wait until the onboarding widget is destroyed before starting overview,
  // since we disallow entering overview while system modal windows are open.
  // Use a weak ptr since `this` can be deleted before we close all windows.
  // Only do this if we have contents data.
  if (contents_data_) {
    onboarding_widget_->widget_delegate()->RegisterDeleteDelegateCallback(
        base::BindOnce(
            [](const base::WeakPtr<PineController>& weak_this) {
              if (weak_this) {
                weak_this->StartPineOverviewSession();
              }
            },
            weak_ptr_factory_.GetWeakPtr()));
  }

  if (restore_on) {
    return;
  }

  // The onboarding dialog would only be shown if `GetActivePrefService()` is
  // not null.
  GetActivePrefService()->SetInteger(
      prefs::kRestoreAppsAndPagesPrefName,
      static_cast<int>(full_restore::RestoreOption::kAskEveryTime));

  // Show toast letting users know the pref change will affect them next
  // session.
  Shell::Get()->toast_manager()->Show(ToastData(
      pine::kOnboardingToastId, ToastCatalogName::kInformedRestoreOnboarding,
      l10n_util::GetStringUTF16(IDS_ASH_INFORMED_RESTORE_ONBOARDING_TOAST)));

  // We only record the action taken if the user had Restore off.
  RecordOnboardingAction(/*restore=*/true);
}

void PineController::OnOnboardingCancelPressed() {
  // The cancel button would only exist if the user had Restore off.
  RecordOnboardingAction(/*restore=*/false);
}

}  // namespace ash
