// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMEOS_ASH_COMPONENTS_GROWTH_CAMPAIGNS_MANAGER_H_
#define CHROMEOS_ASH_COMPONENTS_GROWTH_CAMPAIGNS_MANAGER_H_

#include "base/component_export.h"
#include "base/functional/callback_forward.h"
#include "base/memory/raw_ptr.h"
#include "base/observer_list.h"
#include "base/observer_list_types.h"
#include "chromeos/ash/components/growth/action_performer.h"
#include "chromeos/ash/components/growth/campaigns_manager_client.h"
#include "chromeos/ash/components/growth/campaigns_matcher.h"
#include "chromeos/ash/components/growth/campaigns_model.h"

class PrefService;

namespace growth {

// A class that manages growth campaigns.
class COMPONENT_EXPORT(CHROMEOS_ASH_COMPONENTS_GROWTH) CampaignsManager {
 public:
  using GetCampaignCallback =
      base::OnceCallback<void(const Campaign* campaign)>;

  // Interface for observing the CampaignsManager.
  class Observer : public base::CheckedObserver {
   public:
    ~Observer() override = default;

    // Trigger when complete loading growth campaigns. CampaignsManager is ready
    // for serving campaigns at this point.
    virtual void OnCampaignsLoadCompleted() = 0;
  };

  CampaignsManager(CampaignsManagerClient* client, PrefService* local_state);
  CampaignsManager(const CampaignsManager&) = delete;
  CampaignsManager& operator=(const CampaignsManager&) = delete;
  ~CampaignsManager();

  // Static.
  static CampaignsManager* Get();

  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

  void SetPrefs(PrefService* prefs);

  // Download and install campaigns. Once installed, trigger the
  // `OnCampaignsLoaded` to install campaigns and notifier observers when
  // complete loading campaigns.
  void LoadCampaigns(base::OnceClosure load_callback, bool in_oobe = false);

  // Get campaigns by slot and register sythetical trial for current session.
  // This is used by reactive slots to query campaign that targets the given
  // `slot`. It a heavy operation which should be called when's necessary.
  // TODO(b/308684443): Rename this to `GetCampaignBySlotAndRegisterTrial`.
  const Campaign* GetCampaignBySlot(Slot slot) const;

  // Get latest opened URL.
  const GURL& GetActiveUrl() const;

  // Set the current active URL. Used in `CampaignsMatcher` for matching
  // URL targeting
  void SetActiveUrl(const GURL& url);

  // Get latest opened app id.
  const std::string& GetOpenedAppId() const;

  // Set the current opened app. Used in `CampaignsMatcher` for matching
  // opened app targeting.
  void SetOpenedApp(const std::string& app_id);

  // Get latest trigger.
  const Trigger& GetTrigger() const;

  // Set the current trigger type and event. Used in `CampaignsMatcher` for
  // matching trigger targeting.
  void SetTrigger(const Trigger&& trigger_type);

  // Set whether the current user is device owner.
  void SetIsUserOwner(bool is_user_owner);

  // Select action performer based on the given `action`. Action includes the
  // action type and action params for performing action.
  void PerformAction(int campaign_id, const Action* action);

  // Select action performer based on the action type and perform action with
  // action params.
  void PerformAction(int campaign_id,
                     const ActionType action_type,
                     const base::Value::Dict* params);

  // Clear event stored in the Feature Engagement framework.
  void ClearEvent(CampaignEvent event, const std::string& id);
  void ClearEvent(const std::string& event);

  // Record event to the Feature Engagement framework. Event will be stored and
  // could be used for targeting.
  // TODO: b/342283711 - Refactor this into two functions with
  // `RecordSurfaceUiEvent` and `RecordEventAppOpened`.
  void RecordEventForTargeting(growth::CampaignEvent event,
                               const std::string& id);

  void SetOobeCompleteTimeForTesting(base::Time time);
  const Campaigns* GetCampaignsBySlotForTesting(Slot slot) const;

 private:
  // Triggred when campaigns component loaded.
  void OnCampaignsComponentLoaded(
      base::OnceClosure load_callback,
      bool in_oobe,
      const std::optional<const base::FilePath>& file_path);

  // Triggered when campaigns are loaded from the campaigns component mounted
  // path.
  void OnCampaignsLoaded(base::OnceClosure load_callback,
                         std::optional<base::Value::Dict> campaigns);

  // Triggered when loading OOBE timestamp completed.
  void OnOobeTimestampLoaded(base::OnceClosure load_callback,
                             const std::optional<const base::FilePath>& path,
                             base::Time oobe_time);

  // Notify observers that campaigns are loaded and CampaignsManager is ready
  // to query.
  void NotifyCampaignsLoaded();

  // Register synthetic trial for growth. It will not work if campaign is
  // incomplete, i.e. missing id.
  void RegisterTrialForCampaign(const Campaign* campaign) const;

  void RecordEvent(const std::string& event);

  raw_ptr<CampaignsManagerClient> client_ = nullptr;

  // True if campaigns are loaded.
  bool campaigns_loaded_ = false;

  // Campaigns store owns all campaigns, including proactive and reactive
  // campaigns.
  CampaignsPerSlot campaigns_;
  // Campaigns matcher for selecting campaigns based on criteria.
  CampaignsMatcher matcher_;

  // Maps action type to the action.
  ActionMap actions_map_;

  // Keeps track of when downloading campaigns begins.
  base::TimeTicks campaigns_download_start_time_;

  base::Time oobe_complete_time_for_test_;

  base::ObserverList<Observer> observers_;

  base::WeakPtrFactory<CampaignsManager> weak_factory_{this};
};

}  // namespace growth

#endif  // CHROMEOS_ASH_COMPONENTS_GROWTH_CAMPAIGNS_MANAGER_H_
