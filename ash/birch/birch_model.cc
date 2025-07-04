// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/birch/birch_model.h"

#include <algorithm>
#include <limits>
#include <memory>
#include <vector>

#include "ash/birch/birch_data_provider.h"
#include "ash/birch/birch_icon_cache.h"
#include "ash/birch/birch_item.h"
#include "ash/birch/birch_item_remover.h"
#include "ash/birch/birch_ranker.h"
#include "ash/birch/birch_weather_provider.h"
#include "ash/constants/ash_features.h"
#include "ash/constants/ash_pref_names.h"
#include "ash/shell.h"
#include "base/functional/callback_forward.h"
#include "base/metrics/histogram_functions.h"
#include "base/time/time.h"
#include "chromeos/ash/components/geolocation/simple_geolocation_provider.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"

namespace ash {

namespace {

constexpr base::TimeDelta kDataFetchPostLoginTimeoutInMs =
    base::Milliseconds(3000);
constexpr base::TimeDelta kDataFetchTimeoutInMs = base::Milliseconds(1000);

// Returns the pref service to use for Birch prefs.
PrefService* GetPrefService() {
  return Shell::Get()->session_controller()->GetPrimaryUserPrefService();
}

}  // namespace

template <typename T>
BirchModel::DataTypeInfo<T>::DataTypeInfo(const std::string& pref_name,
                                          const std::string& metric_suffix)
    : pref_name(pref_name), metric_suffix(metric_suffix) {}

template <typename T>
BirchModel::DataTypeInfo<T>::~DataTypeInfo() = default;

BirchModel::PendingRequest::PendingRequest() = default;

BirchModel::PendingRequest::~PendingRequest() = default;

BirchModel::BirchModel()
    : calendar_data_(prefs::kBirchUseCalendar, "Calendar"),
      attachment_data_(prefs::kBirchUseCalendar, "Attachment"),
      file_suggest_data_(prefs::kBirchUseFileSuggest, "File"),
      recent_tab_data_(prefs::kBirchUseRecentTabs, "Tab"),
      last_active_data_(prefs::kBirchUseLastActive, "LastActive"),
      most_visited_data_(prefs::kBirchUseMostVisited, "MostVisited"),
      self_share_data_(prefs::kBirchUseSelfShare, "SelfShare"),
      release_notes_data_(prefs::kBirchUseReleaseNotes, "ReleaseNotes"),
      weather_data_(prefs::kBirchUseWeather, "Weather"),
      icon_cache_(std::make_unique<BirchIconCache>()) {
  if (features::IsBirchWeatherEnabled() &&
      !features::IsBirchWeatherV2Enabled()) {
    // If BirchWeatherV2 is enabled, the weather provider is owned by birch
    // client.
    weather_provider_ = std::make_unique<BirchWeatherProvider>(this);
  }
  Shell::Get()->session_controller()->AddObserver(this);
  SimpleGeolocationProvider::GetInstance()->AddObserver(this);
}

BirchModel::~BirchModel() {
  SimpleGeolocationProvider::GetInstance()->RemoveObserver(this);
  Shell::Get()->session_controller()->RemoveObserver(this);
}

void BirchModel::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}

void BirchModel::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

// static
void BirchModel::RegisterProfilePrefs(PrefRegistrySimple* registry) {
  registry->RegisterBooleanPref(prefs::kBirchUseCalendar, true);
  registry->RegisterBooleanPref(prefs::kBirchUseFileSuggest, true);
  registry->RegisterBooleanPref(prefs::kBirchUseRecentTabs, true);
  registry->RegisterBooleanPref(prefs::kBirchUseLastActive, true);
  registry->RegisterBooleanPref(prefs::kBirchUseMostVisited, true);
  registry->RegisterBooleanPref(prefs::kBirchUseSelfShare, true);
  registry->RegisterBooleanPref(prefs::kBirchUseWeather, true);
  registry->RegisterBooleanPref(prefs::kBirchUseReleaseNotes, true);
  // NOTE: If you add a pref here, also update birch_browsertest.cc and
  // birch_model_unittest.cc which have code that disables all prefs.
}

template <typename T>
void BirchModel::SetItems(DataTypeInfo<T>& data_info,
                          const std::vector<T>& items,
                          bool record_latency) {
  if (data_info.fetch_in_progress) {
    base::UmaHistogramCounts100(
        "Ash.Birch.ResultsReturned." + data_info.metric_suffix, items.size());
    if (record_latency) {
      base::UmaHistogramTimes("Ash.Birch.Latency." + data_info.metric_suffix,
                              GetNow() - data_info.fetch_start_time);
    }
    data_info.fetch_in_progress = false;
  }
  data_info.items = std::move(items);
  data_info.is_fresh = true;
  MaybeRespondToDataFetchRequest();
}

void BirchModel::SetCalendarItems(
    const std::vector<BirchCalendarItem>& calendar_items) {
  SetItems(calendar_data_, calendar_items, /*record_latency=*/true);
}

void BirchModel::SetAttachmentItems(
    const std::vector<BirchAttachmentItem>& attachment_items) {
  // There is no separate latency measurement for attachments because they come
  // from the calendar provider.
  SetItems(attachment_data_, attachment_items, /*record_latency=*/false);
}

void BirchModel::SetFileSuggestItems(
    const std::vector<BirchFileItem>& file_suggest_items) {
  SetItems(file_suggest_data_, file_suggest_items,
           /*record_latency=*/true);
}

void BirchModel::SetRecentTabItems(
    const std::vector<BirchTabItem>& recent_tab_items) {
  SetItems(recent_tab_data_, recent_tab_items, /*record_latency=*/true);
}

void BirchModel::SetLastActiveItems(
    const std::vector<BirchLastActiveItem>& items) {
  SetItems(last_active_data_, items, /*record_latency=*/true);
}

void BirchModel::SetMostVisitedItems(
    const std::vector<BirchMostVisitedItem>& items) {
  SetItems(most_visited_data_, items, /*record_latency=*/true);
}

void BirchModel::SetSelfShareItems(
    const std::vector<BirchSelfShareItem>& self_share_items) {
  SetItems(self_share_data_, self_share_items, /*record_latency=*/true);
}

void BirchModel::SetWeatherItems(
    const std::vector<BirchWeatherItem>& weather_items) {
  SetItems(weather_data_, weather_items, /*record_latency=*/true);
}

void BirchModel::SetReleaseNotesItems(
    const std::vector<BirchReleaseNotesItem>& release_notes_items) {
  SetItems(release_notes_data_, release_notes_items,
           /*record_latency=*/true);
}

void BirchModel::SetClientAndInit(BirchClient* client) {
  birch_client_ = client;

  if (birch_client_) {
    // `BirchItemRemover` calls `MaybeRespondToDataFetchRequest` once it
    // has completed initializing, this way any data fetch requests which have
    // completed can be responded to.
    item_remover_ = std::make_unique<BirchItemRemover>(
        birch_client_->GetRemovedItemsFilePath(),
        /*on_init_callback=*/base::BindOnce(
            &BirchModel::MaybeRespondToDataFetchRequest,
            base::Unretained(this)));
    for (auto& observer : observers_) {
      observer.OnBirchClientSet();
    }
  } else {
    item_remover_.reset();
  }
}

template <typename T>
void BirchModel::StartDataFetchIfNeeded(DataTypeInfo<T>& data_info,
                                        BirchDataProvider* data_provider) {
  // If the data type is disabled by pref, no data fetch is expected, so treat
  // the data as fresh.
  if (!GetPrefService()->GetBoolean(data_info.pref_name)) {
    data_info.items.clear();
    data_info.is_fresh = true;
    return;
  }

  // If no fetch is currently in progress, avoid fetching data for this type
  // when the pref was toggled but no items need to be shown.
  const bool model_fetch_in_progress = !pending_requests_.empty();
  if (!model_fetch_in_progress) {
    return;
  }

  if (!data_provider) {
    return;
  }

  // TODO(b/336712820): Return early and avoid requesting a data fetch if a
  // fetch is in progress and it has been ongoing for less than some short
  // amount of time.

  data_info.is_fresh = false;
  data_info.fetch_start_time = GetNow();
  data_info.fetch_in_progress = true;
  data_provider->RequestBirchDataFetch();
}

BirchDataProvider* BirchModel::GetWeatherProvider() {
  if (!features::IsBirchWeatherEnabled()) {
    return nullptr;
  }
  if (features::IsBirchWeatherV2Enabled()) {
    return birch_client_ ? birch_client_->GetWeatherV2Provider() : nullptr;
  }
  return weather_provider_.get();
}

void BirchModel::RequestBirchDataFetch(bool is_post_login,
                                       base::OnceClosure callback) {
  if (!Shell::Get()->session_controller()->IsUserPrimary()) {
    // Fetches are only supported for the primary user. Return with empty data.
    ClearAllItems();
    std::move(callback).Run();
    return;
  }

  PrefService* prefs = GetPrefService();
  if (!prefs) {
    std::move(callback).Run();
    return;
  }

  const bool model_fetch_in_progress = !pending_requests_.empty();

  size_t request_id = next_request_id_++;

  PendingRequest& request = pending_requests_[request_id];
  request.callback = std::move(callback);
  request.timer = std::make_unique<base::OneShotTimer>();
  request.timer->Start(
      FROM_HERE,
      is_post_login ? kDataFetchPostLoginTimeoutInMs : kDataFetchTimeoutInMs,
      base::BindOnce(&BirchModel::HandleRequestTimeout, base::Unretained(this),
                     request_id));

  if (model_fetch_in_progress) {
    return;
  }

  // Data for a type can only ever be marked fresh if its pref is disabled, or
  // if its data items are set. Start here by marking all data as not fresh to
  // avoid any preemptive request responses until we determine the initial
  // freshness values after all calls to `StartDataFetchIfNeeded()`.
  MarkDataNotFresh();

  is_post_login_fetch_ = is_post_login;
  fetch_start_time_ = GetNow();

  if (birch_client_) {
    StartDataFetchIfNeeded(calendar_data_,
                           birch_client_->GetCalendarProvider());
    StartDataFetchIfNeeded(attachment_data_,
                           birch_client_->GetCalendarProvider());
    StartDataFetchIfNeeded(file_suggest_data_,
                           birch_client_->GetFileSuggestProvider());
    StartDataFetchIfNeeded(recent_tab_data_,
                           birch_client_->GetRecentTabsProvider());
    StartDataFetchIfNeeded(last_active_data_,
                           birch_client_->GetLastActiveProvider());
    StartDataFetchIfNeeded(most_visited_data_,
                           birch_client_->GetMostVisitedProvider());
    StartDataFetchIfNeeded(self_share_data_,
                           birch_client_->GetSelfShareProvider());
    StartDataFetchIfNeeded(release_notes_data_,
                           birch_client_->GetReleaseNotesProvider());
  }
  StartDataFetchIfNeeded(weather_data_, GetWeatherProvider());
  MaybeRespondToDataFetchRequest();
}

std::vector<std::unique_ptr<BirchItem>> BirchModel::GetAllItems() {
  if (!IsItemRemoverInitialized()) {
    // With no initialized item remover, return an empty list of items to avoid
    // returning items previously removed by the user.
    return {};
  }

  item_remover_->FilterRemovedCalendarItems(&calendar_data_.items);
  item_remover_->FilterRemovedAttachmentItems(&attachment_data_.items);
  item_remover_->FilterRemovedFileItems(&file_suggest_data_.items);
  item_remover_->FilterRemovedTabs(&recent_tab_data_.items);
  item_remover_->FilterRemovedLastActiveItems(&last_active_data_.items);
  item_remover_->FilterRemovedMostVisitedItems(&most_visited_data_.items);
  item_remover_->FilterRemovedSelfShareItems(&self_share_data_.items);

  BirchRanker ranker(GetNow());
  ranker.RankCalendarItems(&calendar_data_.items);
  ranker.RankAttachmentItems(&attachment_data_.items);
  ranker.RankFileSuggestItems(&file_suggest_data_.items);
  ranker.RankRecentTabItems(&recent_tab_data_.items);
  ranker.RankLastActiveItems(&last_active_data_.items);
  ranker.RankMostVisitedItems(&most_visited_data_.items);
  ranker.RankSelfShareItems(&self_share_data_.items);
  ranker.RankWeatherItems(&weather_data_.items);
  ranker.RankReleaseNotesItems(&release_notes_data_.items);

  // Avoid showing a duplicate last-active tab with a recent tab by removing
  // the item with the higher ranking.
  std::unordered_map<std::string, BirchLastActiveItem> url_to_last_active_item;
  for (auto& item : last_active_data_.items) {
    url_to_last_active_item.emplace(item.url().spec(), item);
  }
  std::erase_if(recent_tab_data_.items, [&url_to_last_active_item](
                                            const auto& recent_tab_item) {
    if (auto iter = url_to_last_active_item.find(recent_tab_item.url().spec());
        iter != url_to_last_active_item.end()) {
      if (recent_tab_item.ranking() > iter->second.ranking()) {
        return true;
      }
      url_to_last_active_item.erase(iter);
    }
    return false;
  });

  // Avoid showing a duplicate most-visited tab with a recent tab by removing
  // the item with the higher ranking.
  std::unordered_map<std::string, BirchMostVisitedItem>
      url_to_most_visited_item;
  for (auto& item : most_visited_data_.items) {
    url_to_most_visited_item.emplace(item.url().spec(), item);
  }
  std::erase_if(recent_tab_data_.items, [&url_to_most_visited_item](
                                            const auto& recent_tab_item) {
    if (auto iter = url_to_most_visited_item.find(recent_tab_item.url().spec());
        iter != url_to_most_visited_item.end()) {
      if (recent_tab_item.ranking() > iter->second.ranking()) {
        return true;
      }
      url_to_most_visited_item.erase(iter);
    }
    return false;
  });

  // Avoid showing a duplicate tab item by removing the item with the higher
  // ranking.
  std::unordered_map<std::string, BirchSelfShareItem> url_to_self_share_item;
  for (auto& item : self_share_data_.items) {
    url_to_self_share_item.emplace(item.url().spec(), item);
  }
  std::erase_if(recent_tab_data_.items, [&url_to_self_share_item](
                                            const auto& recent_tab_item) {
    if (auto iter = url_to_self_share_item.find(recent_tab_item.url().spec());
        iter != url_to_self_share_item.end()) {
      if (recent_tab_item.ranking() > iter->second.ranking()) {
        return true;
      }

      url_to_self_share_item.erase(iter);
    }
    return false;
  });

  // Avoid showing a duplicate file which is both an attachment and file
  // suggestion by erasing the item with the higher ranking.
  std::unordered_map<std::string, BirchAttachmentItem>
      file_id_to_attachment_item;
  for (auto& attachment : attachment_data_.items) {
    file_id_to_attachment_item.emplace(attachment.file_id(), attachment);
  }
  std::erase_if(file_suggest_data_.items, [&file_id_to_attachment_item](
                                              const auto& file_suggest_item) {
    if (file_id_to_attachment_item.contains(file_suggest_item.file_id())) {
      if (file_suggest_item.ranking() >
          file_id_to_attachment_item.at(file_suggest_item.file_id())
              .ranking()) {
        // Duplicate item with a higher ranked file suggest item. Erase the file
        // suggest item.
        return true;
      }
      // Duplicate item with a higher ranked attachment item. Erase the
      // attachment item.
      file_id_to_attachment_item.erase(file_suggest_item.file_id());
    }
    return false;
  });

  std::vector<std::unique_ptr<BirchItem>> all_items;
  for (auto& event : calendar_data_.items) {
    all_items.push_back(std::make_unique<BirchCalendarItem>(event));
  }
  for (auto& event : file_id_to_attachment_item) {
    all_items.push_back(std::make_unique<BirchAttachmentItem>(event.second));
  }
  for (auto& tab : recent_tab_data_.items) {
    all_items.push_back(std::make_unique<BirchTabItem>(tab));
  }
  if (ShouldShowLastActive()) {
    for (auto& item : url_to_last_active_item) {
      all_items.push_back(std::make_unique<BirchLastActiveItem>(item.second));
    }
    last_active_last_shown_ = GetNow();
  }
  if (ShouldShowMostVisited()) {
    for (auto& item : url_to_most_visited_item) {
      all_items.push_back(std::make_unique<BirchMostVisitedItem>(item.second));
    }
    most_visited_last_shown_ = GetNow();
  }
  for (auto& event : url_to_self_share_item) {
    all_items.push_back(std::make_unique<BirchSelfShareItem>(event.second));
  }
  for (auto& file_suggestion : file_suggest_data_.items) {
    all_items.push_back(std::make_unique<BirchFileItem>(file_suggestion));
  }
  for (auto& weather_item : weather_data_.items) {
    all_items.push_back(std::make_unique<BirchWeatherItem>(weather_item));
  }
  for (auto& release_notes_item : release_notes_data_.items) {
    all_items.push_back(
        std::make_unique<BirchReleaseNotesItem>(release_notes_item));
  }
  // Sort items by ranking.
  std::sort(all_items.begin(), all_items.end(),
            [](const auto& item_a, const auto& item_b) {
              return item_a->ranking() < item_b->ranking();
            });

  return all_items;
}

std::vector<std::unique_ptr<BirchItem>> BirchModel::GetItemsForDisplay() {
  std::vector<std::unique_ptr<BirchItem>> results = GetAllItems();

  // Remove any items with no ranking, as these should not be shown.
  std::erase_if(results, [](const auto& item) {
    return item->ranking() == std::numeric_limits<float>::max();
  });

  return results;
}

bool BirchModel::IsDataFresh() {
  PrefService* prefs = GetPrefService();
  if (!prefs) {
    return false;
  }

  bool is_birch_client_fresh =
      !birch_client_ ||
      (calendar_data_.is_fresh && attachment_data_.is_fresh &&
       file_suggest_data_.is_fresh && recent_tab_data_.is_fresh &&
       last_active_data_.is_fresh && most_visited_data_.is_fresh &&
       self_share_data_.is_fresh && release_notes_data_.is_fresh);

  // Use the same logic for weather.
  bool is_weather_fresh = !GetWeatherProvider() || weather_data_.is_fresh;
  return is_birch_client_fresh && is_weather_fresh;
}

void BirchModel::RemoveItem(BirchItem* item) {
  if (!IsItemRemoverInitialized()) {
    return;
  }
  // Record that the user hid a chip, with the type of the chip.
  base::UmaHistogramEnumeration("Ash.Birch.Chip.Hidden", item->GetType());

  item_remover_->RemoveItem(item);
}

void BirchModel::OnActiveUserSessionChanged(const AccountId& account_id) {
  if (!has_active_user_session_changed_) {
    // This is the initial notification on signin.
    has_active_user_session_changed_ = true;
    InitPrefChangeRegistrars();
    RecordProviderHiddenHistograms();
    return;
  }

  // On multi-profile switch, first cancel any pending requests.
  pending_requests_.clear();

  // Clear the existing data and mark the data as not fresh.
  ClearAllItems();
  MarkDataNotFresh();
}

void BirchModel::OnGeolocationPermissionChanged(bool enabled) {
  // If geolocation permission is disabled, remove any cached weather data.
  if (!enabled) {
    weather_data_.items.clear();
    weather_data_.is_fresh = false;
  }
}

void BirchModel::OverrideWeatherProviderForTest(
    std::unique_ptr<BirchDataProvider> weather_provider) {
  CHECK(weather_provider_);
  weather_provider_ = std::move(weather_provider);
}

void BirchModel::OverrideClockForTest(base::Clock* clock) {
  clock_override_ = clock;
}

void BirchModel::SetDataFetchCallbackForTest(base::OnceClosure callback) {
  data_fetch_callback_for_test_ = std::move(callback);
}

void BirchModel::HandleRequestTimeout(size_t request_id) {
  auto request = pending_requests_.find(request_id);
  if (request == pending_requests_.end()) {
    return;
  }

  base::OnceClosure callback = std::move(request->second.callback);
  pending_requests_.erase(request);
  std::move(callback).Run();
}

void BirchModel::MaybeRespondToDataFetchRequest() {
  if (!IsDataFresh() || !IsItemRemoverInitialized()) {
    return;
  }

  // Was this a real fetch being completed (rather than a provider supplying
  // data outside of a fetch)?
  bool was_model_fetch = !pending_requests_.empty();
  if (was_model_fetch) {
    // All data providers have replied, so compute total latency.
    base::TimeDelta latency = GetNow() - fetch_start_time_;
    if (is_post_login_fetch_) {
      base::UmaHistogramTimes("Ash.Birch.TotalLatencyPostLogin", latency);
    } else {
      base::UmaHistogramTimes("Ash.Birch.TotalLatency", latency);
    }
  }

  std::vector<base::OnceClosure> callbacks;
  for (auto& request : pending_requests_) {
    callbacks.push_back(std::move(request.second.callback));
  }
  pending_requests_.clear();

  for (auto& callback : callbacks) {
    std::move(callback).Run();
  }

  if (data_fetch_callback_for_test_) {
    std::move(data_fetch_callback_for_test_).Run();
    data_fetch_callback_for_test_.Reset();
  }
}

base::Time BirchModel::GetNow() const {
  if (clock_override_) {
    return clock_override_->Now();
  }
  return base::Time::Now();
}

void BirchModel::ClearAllItems() {
  calendar_data_.items.clear();
  attachment_data_.items.clear();
  file_suggest_data_.items.clear();
  recent_tab_data_.items.clear();
  last_active_data_.items.clear();
  most_visited_data_.items.clear();
  weather_data_.items.clear();
  release_notes_data_.items.clear();
}

void BirchModel::MarkDataNotFresh() {
  calendar_data_.is_fresh = false;
  attachment_data_.is_fresh = false;
  file_suggest_data_.is_fresh = false;
  recent_tab_data_.is_fresh = false;
  last_active_data_.is_fresh = false;
  most_visited_data_.is_fresh = false;
  weather_data_.is_fresh = false;
  release_notes_data_.is_fresh = false;
}

void BirchModel::InitPrefChangeRegistrars() {
  PrefService* prefs = GetPrefService();
  if (!prefs) {
    return;
  }

  calendar_pref_registrar_.Init(prefs);
  calendar_pref_registrar_.Add(
      prefs::kBirchUseCalendar,
      base::BindRepeating(&BirchModel::OnCalendarPrefChanged,
                          base::Unretained(this)));
  file_suggest_pref_registrar_.Init(prefs);
  file_suggest_pref_registrar_.Add(
      prefs::kBirchUseFileSuggest,
      base::BindRepeating(&BirchModel::OnFileSuggestPrefChanged,
                          base::Unretained(this)));
  recent_tab_pref_registrar_.Init(prefs);
  recent_tab_pref_registrar_.Add(
      prefs::kBirchUseRecentTabs,
      base::BindRepeating(&BirchModel::OnRecentTabPrefChanged,
                          base::Unretained(this)));
  last_active_pref_registrar_.Init(prefs);
  last_active_pref_registrar_.Add(
      prefs::kBirchUseLastActive,
      base::BindRepeating(&BirchModel::OnLastActivePrefChanged,
                          base::Unretained(this)));
  most_visited_pref_registrar_.Init(prefs);
  most_visited_pref_registrar_.Add(
      prefs::kBirchUseMostVisited,
      base::BindRepeating(&BirchModel::OnMostVisitedPrefChanged,
                          base::Unretained(this)));
  self_share_pref_registrar_.Init(prefs);
  self_share_pref_registrar_.Add(
      prefs::kBirchUseSelfShare,
      base::BindRepeating(&BirchModel::OnSelfSharePrefChanged,
                          base::Unretained(this)));
  weather_pref_registrar_.Init(prefs);
  weather_pref_registrar_.Add(
      prefs::kBirchUseWeather,
      base::BindRepeating(&BirchModel::OnWeatherPrefChanged,
                          base::Unretained(this)));
  release_notes_pref_registrar_.Init(prefs);
  release_notes_pref_registrar_.Add(
      prefs::kBirchUseReleaseNotes,
      base::BindRepeating(&BirchModel::OnReleaseNotesPrefChanged,
                          base::Unretained(this)));
}

void BirchModel::OnCalendarPrefChanged() {
  if (birch_client_) {
    StartDataFetchIfNeeded(calendar_data_,
                           birch_client_->GetCalendarProvider());
    StartDataFetchIfNeeded(attachment_data_,
                           birch_client_->GetCalendarProvider());
  }
}

void BirchModel::OnFileSuggestPrefChanged() {
  if (birch_client_) {
    StartDataFetchIfNeeded(file_suggest_data_,
                           birch_client_->GetFileSuggestProvider());
  }
}

void BirchModel::OnRecentTabPrefChanged() {
  if (birch_client_) {
    StartDataFetchIfNeeded(recent_tab_data_,
                           birch_client_->GetRecentTabsProvider());
  }
}

void BirchModel::OnLastActivePrefChanged() {
  if (birch_client_) {
    StartDataFetchIfNeeded(last_active_data_,
                           birch_client_->GetLastActiveProvider());
  }
}

void BirchModel::OnMostVisitedPrefChanged() {
  if (birch_client_) {
    StartDataFetchIfNeeded(most_visited_data_,
                           birch_client_->GetMostVisitedProvider());
  }
}

void BirchModel::OnSelfSharePrefChanged() {
  if (birch_client_) {
    StartDataFetchIfNeeded(self_share_data_,
                           birch_client_->GetSelfShareProvider());
  }
}

void BirchModel::OnWeatherPrefChanged() {
  StartDataFetchIfNeeded(weather_data_, GetWeatherProvider());
}

void BirchModel::OnReleaseNotesPrefChanged() {
  if (birch_client_) {
    StartDataFetchIfNeeded(release_notes_data_,
                           birch_client_->GetReleaseNotesProvider());
  }
}

void BirchModel::RecordProviderHiddenHistograms() {
  PrefService* prefs = GetPrefService();
  if (!prefs) {
    return;
  }

  base::UmaHistogramBoolean("Ash.Birch.ProviderHidden.Calendar",
                            !prefs->GetBoolean(prefs::kBirchUseCalendar));
  base::UmaHistogramBoolean("Ash.Birch.ProviderHidden.FileSuggest",
                            !prefs->GetBoolean(prefs::kBirchUseFileSuggest));
  base::UmaHistogramBoolean("Ash.Birch.ProviderHidden.RecentTabs",
                            !prefs->GetBoolean(prefs::kBirchUseRecentTabs));
  base::UmaHistogramBoolean("Ash.Birch.ProviderHidden.LastActive",
                            !prefs->GetBoolean(prefs::kBirchUseLastActive));
  base::UmaHistogramBoolean("Ash.Birch.ProviderHidden.MostVisited",
                            !prefs->GetBoolean(prefs::kBirchUseMostVisited));
  base::UmaHistogramBoolean("Ash.Birch.ProviderHidden.SelfShare",
                            !prefs->GetBoolean(prefs::kBirchUseSelfShare));
  base::UmaHistogramBoolean("Ash.Birch.ProviderHidden.Weather",
                            !prefs->GetBoolean(prefs::kBirchUseWeather));
  base::UmaHistogramBoolean("Ash.Birch.ProviderHidden.ReleaseNotes",
                            !prefs->GetBoolean(prefs::kBirchUseReleaseNotes));
}

bool BirchModel::IsItemRemoverInitialized() {
  return item_remover_ && item_remover_->Initialized();
}

bool BirchModel::ShouldShowLastActive() {
  if (last_active_last_shown_.is_null()) {
    return true;  // Never been shown.
  }
  // Re-show for up to 2 minutes.
  return GetNow() - last_active_last_shown_ < base::Minutes(2);
}

bool BirchModel::ShouldShowMostVisited() {
  if (most_visited_last_shown_.is_null()) {
    return true;  // Never been shown.
  }
  // Re-show for up to 2 minutes.
  return GetNow() - most_visited_last_shown_ < base::Minutes(2);
}

}  // namespace ash
