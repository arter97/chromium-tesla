// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/system/video_conference/bubble/vc_tile_ui_controller.h"

#include "ash/accessibility/accessibility_controller.h"
#include "ash/shell.h"
#include "ash/strings/grit/ash_strings.h"
#include "ash/system/video_conference/bubble/bubble_view_ids.h"
#include "ash/system/video_conference/effects/video_conference_tray_effects_manager_types.h"
#include "ash/system/video_conference/video_conference_tray_controller.h"
#include "ash/system/video_conference/video_conference_utils.h"
#include "base/barrier_callback.h"
#include "base/containers/flat_set.h"
#include "base/metrics/histogram_functions.h"
#include "chromeos/ash/components/dbus/dlcservice/dlcservice.pb.h"
#include "chromeos/ash/components/dbus/dlcservice/dlcservice_client.h"
#include "chromeos/utils/haptics_util.h"
#include "third_party/cros_system_api/dbus/dlcservice/dbus-constants.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/chromeos/styles/cros_tokens_color_mappings.h"
#include "ui/events/devices/haptic_touchpad_effects.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/label.h"

namespace ash::video_conference {

VcTileUiController::VcTileUiController(const VcHostedEffect* effect)
    : effect_(effect) {
  effect_id_ = effect->id();
  effect_state_ = effect->GetState(/*index=*/0);
  auto* dlc_service_client = DlcserviceClient::Get();
  if (!dlc_service_client) {
    // `dlc_service_client` may not exist in tests.
    return;
  }
  dlc_service_client->AddObserver(this);
}

VcTileUiController::~VcTileUiController() {
  auto* dlc_service_client = DlcserviceClient::Get();
  if (!dlc_service_client) {
    // `dlc_service_client` may not exist in tests.
    return;
  }
  dlc_service_client->RemoveObserver(this);
}

std::unique_ptr<FeatureTile> VcTileUiController::CreateTile() {
  auto tile = std::make_unique<FeatureTile>(
      base::BindRepeating(&VcTileUiController::OnPressed,
                          weak_ptr_factory_.GetWeakPtr()),
      /*is_togglable=*/true, FeatureTile::TileType::kCompact);
  tile_ = tile->GetWeakPtr();

  // Set up view ids for the tile and its children.
  tile->SetID(BubbleViewID::kToggleEffectsButton);
  tile->label()->SetID(BubbleViewID::kToggleEffectLabel);
  tile->icon_button()->SetID(BubbleViewID::kToggleEffectIcon);

  // Set up the initial state of the tile, including elements like label, icon,
  // and colors based on toggle state.
  tile->SetLabel(effect_state_->label_text());
  tile->SetVectorIcon(*effect_state_->icon());
  tile->SetForegroundColorId(cros_tokens::kCrosSysOnSurface);
  std::optional<int> current_state = effect_->get_state_callback().Run();
  CHECK(current_state.has_value());
  tile->SetToggled(current_state.value() != 0);
  UpdateTooltip();

  // Set the initial download state of the tile. Future changes to the tile's
  // download state will occur if/when the tile's associated DLCs update.
  dlc_ids_ = VideoConferenceTrayController::Get()
                 ->GetEffectsManager()
                 .GetDlcIdsForEffectId(effect_id_);
  UpdateDlcDownloadUi();

  return tile;
}

void VcTileUiController::OnDlcStateChanged(
    const dlcservice::DlcState& dlc_state) {
  if (!base::Contains(dlc_ids_, dlc_state.id())) {
    return;
  }

  UpdateDlcDownloadUi();
}

void VcTileUiController::OnPressed(const ui::Event& event) {
  if (!effect_state_ || !tile_) {
    return;
  }

  // Execute the associated tile's callback.
  views::Button::PressedCallback(effect_state_->button_callback()).Run(event);

  // Set the toggled state.
  bool toggled = !tile_->IsToggled();
  tile_->SetToggled(toggled);

  // Track UMA metrics about the toggled state.
  TrackToggleUMA(toggled);

  // Play a "toggled-on" or "toggled-off" haptic effect, depending on the toggle
  // state.
  PlayToggleHaptic(toggled);

  // Update properties about the associated tile that change when the toggle
  // state changes, e.g. colors and tooltip text.
  tile_->UpdateColors();
  UpdateTooltip();
}

void VcTileUiController::TrackToggleUMA(bool target_toggle_state) {
  base::UmaHistogramBoolean(
      video_conference_utils::GetEffectHistogramNameForClick(effect_id_),
      target_toggle_state);
}

void VcTileUiController::PlayToggleHaptic(bool target_toggle_state) {
  chromeos::haptics_util::PlayHapticToggleEffect(
      target_toggle_state, ui::HapticTouchpadEffectStrength::kMedium);
}

VcTileUiController::DlcDownloadStateRequest::DlcDownloadStateRequest(
    const base::flat_set<std::string>& dlc_ids,
    base::OnceCallback<void(FeatureTile::DownloadState download_state,
                            int progress)> set_progress_callback)
    : set_progress_callback_(std::move(set_progress_callback)) {
  if (dlc_ids.empty()) {
    std::move(set_progress_callback_)
        .Run(FeatureTile::DownloadState::kNone, /*progress=*/0);
    return;
  }

  // Multiple DLCs can be managed by one tile, and their states will be
  // delivered individually.
  const auto merge_callback = base::BarrierCallback<DlcDownloadState>(
      dlc_ids.size(),
      base::BindOnce(
          &VcTileUiController::DlcDownloadStateRequest::OnAllDlcStatesRetrieved,
          weak_ptr_factory_.GetWeakPtr()));
  for (const std::string& dlc_id : dlc_ids) {
    DlcserviceClient::Get()->GetDlcState(
        dlc_id,
        base::BindOnce(&DlcDownloadStateRequest::OnDlcStateRetrieved,
                       weak_ptr_factory_.GetWeakPtr(), dlc_id, merge_callback));
  }
}

VcTileUiController::DlcDownloadStateRequest::~DlcDownloadStateRequest() {}

void VcTileUiController::DlcDownloadStateRequest::OnDlcStateRetrieved(
    std::string dlc_id,
    base::OnceCallback<void(DlcDownloadState)> merge_callback,
    std::string_view error,
    const dlcservice::DlcState& dlc_state) {
  std::move(merge_callback)
      .Run({std::move(dlc_id), std::string(error), dlc_state});
}

void VcTileUiController::DlcDownloadStateRequest::OnAllDlcStatesRetrieved(
    std::vector<DlcDownloadState> dlc_download_states) {
  // Check for errors.
  for (const DlcDownloadState& dlc_download_state : dlc_download_states) {
    if (dlc_download_state.error_code != dlcservice::kErrorNone) {
      std::move(set_progress_callback_)
          .Run(FeatureTile::DownloadState::kError, /*progress=*/0);
      return;
    }
  }

  // Check for in progress downloads.
  bool fully_installed = true;
  for (const DlcDownloadState& dlc_download_state : dlc_download_states) {
    if (dlc_download_state.dlc_state.state() !=
        dlcservice::DlcState::State::DlcState_State_INSTALLED) {
      fully_installed = false;
      break;
    }
  }
  if (fully_installed) {
    std::move(set_progress_callback_)
        .Run(FeatureTile::DownloadState::kDownloaded, /*progress=*/0);
    return;
  }

  // One or more DLCs is still downloading. Calculate the overall download
  // progress as the average of each DLC's download progress, weighted evenly.
  double progress = 0;
  for (const DlcDownloadState& dlc_download_state : dlc_download_states) {
    progress += dlc_download_state.dlc_state.progress();
  }
  progress /= dlc_download_states.size();
  std::move(set_progress_callback_)
      .Run(FeatureTile::DownloadState::kDownloading,
           /*progress=*/static_cast<int>(base::ClampFloor(progress * 100)));
}

void VcTileUiController::UpdateDlcDownloadUi() {
  if (!tile_) {
    return;
  }

  dlc_download_state_request_ = std::make_unique<DlcDownloadStateRequest>(
      dlc_ids_,
      // `base::Unretained` is safe because `DlcDownloadStateRequest` is
      // outlived by this, and `DlcDownloadStateRequest` maintains ownership of
      // the callback.
      base::BindOnce(&VcTileUiController::OnDlcDownloadStateFetched,
                     base::Unretained(this)));
}

void VcTileUiController::OnDlcDownloadStateFetched(
    FeatureTile::DownloadState download_state,
    int progress) {
  dlc_download_state_request_.reset();

  if (!tile_) {
    return;
  }

  VideoConferenceTrayController::Get()->OnDlcDownloadStateFetched(
      /*add_warning=*/download_state == FeatureTile::DownloadState::kError,
      effect_state_->label_text());

  tile_->SetDownloadState(download_state, progress);
}

void VcTileUiController::UpdateTooltip() {
  if (!effect_state_ || !tile_) {
    return;
  }
  tile_->SetTooltipText(l10n_util::GetStringFUTF16(
      VIDEO_CONFERENCE_TOGGLE_BUTTON_TOOLTIP,
      l10n_util::GetStringUTF16(effect_state_->accessible_name_id()),
      l10n_util::GetStringUTF16(
          tile_->IsToggled() ? VIDEO_CONFERENCE_TOGGLE_BUTTON_STATE_ON
                             : VIDEO_CONFERENCE_TOGGLE_BUTTON_STATE_OFF)));
}

}  // namespace ash::video_conference
