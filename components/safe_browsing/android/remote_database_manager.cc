// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/safe_browsing/android/remote_database_manager.h"

#include <memory>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "base/metrics/field_trial_params.h"
#include "base/metrics/histogram_functions.h"
#include "base/metrics/histogram_macros.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/timer/elapsed_timer.h"
#include "components/safe_browsing/android/real_time_url_checks_allowlist.h"
#include "components/safe_browsing/android/safe_browsing_api_handler_bridge.h"
#include "components/safe_browsing/core/browser/db/v4_get_hash_protocol_manager.h"
#include "components/safe_browsing/core/browser/db/v4_protocol_manager_util.h"
#include "components/safe_browsing/core/common/features.h"
#include "content/public/browser/browser_task_traits.h"
#include "content/public/browser/browser_thread.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"

using content::BrowserThread;
namespace safe_browsing {

using IsInAllowlistResult = RealTimeUrlChecksAllowlist::IsInAllowlistResult;
namespace {

constexpr char kCanCheckUrlBaseHistogramName[] = "SB2.RemoteCall.CanCheckUrl";

void LogCanCheckUrl(bool can_check_url, CheckBrowseUrlType check_type) {
  base::UmaHistogramBoolean(kCanCheckUrlBaseHistogramName, can_check_url);
  std::string metrics_suffix;
  switch (check_type) {
    case CheckBrowseUrlType::kHashDatabase:
      metrics_suffix = ".HashDatabase";
      break;
    case CheckBrowseUrlType::kHashRealTime:
      metrics_suffix = ".HashRealTime";
      break;
  }
  base::UmaHistogramBoolean(kCanCheckUrlBaseHistogramName + metrics_suffix,
                            can_check_url);
}

}  // namespace

//
// RemoteSafeBrowsingDatabaseManager::ClientRequest methods
//
class RemoteSafeBrowsingDatabaseManager::ClientRequest {
 public:
  ClientRequest(Client* client,
                RemoteSafeBrowsingDatabaseManager* db_manager,
                const GURL& url);

  static void OnRequestDoneWeak(const base::WeakPtr<ClientRequest>& req,
                                SBThreatType matched_threat_type,
                                const ThreatMetadata& metadata);
  void OnRequestDone(SBThreatType matched_threat_type,
                     const ThreatMetadata& metadata);

  // Accessors
  Client* client() const { return client_; }
  const GURL& url() const { return url_; }
  base::WeakPtr<ClientRequest> GetWeakPtr() {
    return weak_factory_.GetWeakPtr();
  }

 private:
  raw_ptr<Client, DanglingUntriaged> client_;
  raw_ptr<RemoteSafeBrowsingDatabaseManager, DanglingUntriaged> db_manager_;
  GURL url_;
  base::ElapsedTimer timer_;
  base::WeakPtrFactory<ClientRequest> weak_factory_{this};
};

RemoteSafeBrowsingDatabaseManager::ClientRequest::ClientRequest(
    Client* client,
    RemoteSafeBrowsingDatabaseManager* db_manager,
    const GURL& url)
    : client_(client), db_manager_(db_manager), url_(url) {}

// Static
void RemoteSafeBrowsingDatabaseManager::ClientRequest::OnRequestDoneWeak(
    const base::WeakPtr<ClientRequest>& req,
    SBThreatType matched_threat_type,
    const ThreatMetadata& metadata) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!req) {
    return;  // Previously canceled
  }
  req->OnRequestDone(matched_threat_type, metadata);
}

void RemoteSafeBrowsingDatabaseManager::ClientRequest::OnRequestDone(
    SBThreatType matched_threat_type,
    const ThreatMetadata& metadata) {
  DVLOG(1) << "OnRequestDone took " << timer_.Elapsed().InMilliseconds()
           << " ms for client " << client_ << " and URL " << url_;
  client_->OnCheckBrowseUrlResult(url_, matched_threat_type, metadata);
  UMA_HISTOGRAM_TIMES("SB2.RemoteCall.Elapsed", timer_.Elapsed());
  // CancelCheck() will delete *this.
  db_manager_->CancelCheck(client_);
}

//
// RemoteSafeBrowsingDatabaseManager methods
//

RemoteSafeBrowsingDatabaseManager::RemoteSafeBrowsingDatabaseManager()
    : SafeBrowsingDatabaseManager(content::GetUIThreadTaskRunner({}),
                                  content::GetIOThreadTaskRunner({})),
      enabled_(false) {}

RemoteSafeBrowsingDatabaseManager::~RemoteSafeBrowsingDatabaseManager() {
  DCHECK(!enabled_);
}

void RemoteSafeBrowsingDatabaseManager::CancelCheck(Client* client) {
  DCHECK(sb_task_runner()->RunsTasksInCurrentSequence());
  DCHECK(enabled_);
  for (auto itr = current_requests_.begin(); itr != current_requests_.end();
       ++itr) {
    if ((*itr)->client() == client) {
      DVLOG(2) << "Canceling check for URL " << (*itr)->url();
      delete *itr;
      current_requests_.erase(itr);
      return;
    }
  }
}

bool RemoteSafeBrowsingDatabaseManager::CanCheckUrl(const GURL& url) const {
  return url.SchemeIsHTTPOrHTTPS() || url.SchemeIs(url::kFtpScheme) ||
         url.SchemeIsWSOrWSS();
}

bool RemoteSafeBrowsingDatabaseManager::CheckBrowseUrl(
    const GURL& url,
    const SBThreatTypeSet& threat_types,
    Client* client,
    CheckBrowseUrlType check_type) {
  DCHECK(sb_task_runner()->RunsTasksInCurrentSequence());
  DCHECK(!threat_types.empty());
  DCHECK(SBThreatTypeSetIsValidForCheckBrowseUrl(threat_types));
  if (!enabled_) {
    return true;
  }

  bool can_check_url = CanCheckUrl(url);
  LogCanCheckUrl(can_check_url, check_type);
  if (!can_check_url) {
    return true;  // Safe, continue right away.
  }

  std::unique_ptr<ClientRequest> req(new ClientRequest(client, this, url));

  DVLOG(1) << "Checking for client " << client << " and URL " << url;
  auto callback =
      std::make_unique<SafeBrowsingApiHandlerBridge::ResponseCallback>(
          base::BindOnce(&ClientRequest::OnRequestDoneWeak, req->GetWeakPtr()));
  switch (check_type) {
    case CheckBrowseUrlType::kHashDatabase:
      SafeBrowsingApiHandlerBridge::GetInstance().StartHashDatabaseUrlCheck(
          std::move(callback), url, threat_types);
      break;
    case CheckBrowseUrlType::kHashRealTime:
      SafeBrowsingApiHandlerBridge::GetInstance().StartHashRealTimeUrlCheck(
          std::move(callback), url, threat_types);
  }
  current_requests_.push_back(req.release());

  // Defer the resource load.
  return false;
}

bool RemoteSafeBrowsingDatabaseManager::CheckDownloadUrl(
    const std::vector<GURL>& url_chain,
    Client* client) {
  NOTREACHED_IN_MIGRATION();
  return true;
}

bool RemoteSafeBrowsingDatabaseManager::CheckExtensionIDs(
    const std::set<std::string>& extension_ids,
    Client* client) {
  NOTREACHED_IN_MIGRATION();
  return true;
}

bool RemoteSafeBrowsingDatabaseManager::CheckResourceUrl(const GURL& url,
                                                         Client* client) {
  NOTREACHED_IN_MIGRATION();
  return true;
}

std::optional<
    SafeBrowsingDatabaseManager::HighConfidenceAllowlistCheckLoggingDetails>
RemoteSafeBrowsingDatabaseManager::CheckUrlForHighConfidenceAllowlist(
    const GURL& url,
    base::OnceCallback<void(bool)> callback) {
  DCHECK(sb_task_runner()->RunsTasksInCurrentSequence());

  if (!enabled_ || !CanCheckUrl(url)) {
    sb_task_runner()->PostTask(FROM_HERE,
                               base::BindOnce(std::move(callback), false));
    return std::nullopt;
  }

  IsInAllowlistResult match_result =
      RealTimeUrlChecksAllowlist::GetInstance()->IsInAllowlist(url);
  // Note that if the allowlist is unavailable, we say that is a match.
  bool is_match = match_result == IsInAllowlistResult::kInAllowlist ||
                  match_result == IsInAllowlistResult::kAllowlistUnavailable;
  sb_task_runner()->PostTask(FROM_HERE,
                             base::BindOnce(std::move(callback), is_match));
  return std::nullopt;
}

bool RemoteSafeBrowsingDatabaseManager::CheckUrlForSubresourceFilter(
    const GURL& url,
    Client* client) {
  DCHECK(sb_task_runner()->RunsTasksInCurrentSequence());

  if (!enabled_ || !CanCheckUrl(url)) {
    return true;
  }

  std::unique_ptr<ClientRequest> req(new ClientRequest(client, this, url));

  DVLOG(1) << "Checking for client " << client << " and URL " << url;
  auto callback =
      std::make_unique<SafeBrowsingApiHandlerBridge::ResponseCallback>(
          base::BindOnce(&ClientRequest::OnRequestDoneWeak, req->GetWeakPtr()));
  SafeBrowsingApiHandlerBridge::GetInstance().StartHashDatabaseUrlCheck(
      std::move(callback), url,
      CreateSBThreatTypeSet({SBThreatType::SB_THREAT_TYPE_SUBRESOURCE_FILTER,
                             SBThreatType::SB_THREAT_TYPE_URL_PHISHING}));

  current_requests_.push_back(req.release());

  // Defer the resource load.
  return false;
}

AsyncMatch RemoteSafeBrowsingDatabaseManager::CheckCsdAllowlistUrl(
    const GURL& url,
    Client* client) {
  DCHECK(sb_task_runner()->RunsTasksInCurrentSequence());

  // If this URL's scheme isn't supported, call is safe.
  if (!CanCheckUrl(url)) {
    return AsyncMatch::MATCH;
  }

  // TODO(crbug.com/41477281): Make this call async.
  bool is_match =
      SafeBrowsingApiHandlerBridge::GetInstance().StartCSDAllowlistCheck(url);
  return is_match ? AsyncMatch::MATCH : AsyncMatch::NO_MATCH;
}

void RemoteSafeBrowsingDatabaseManager::MatchDownloadAllowlistUrl(
    const GURL& url,
    base::OnceCallback<void(bool)> callback) {
  NOTREACHED_IN_MIGRATION();
  sb_task_runner()->PostTask(FROM_HERE,
                             base::BindOnce(std::move(callback), true));
}

safe_browsing::ThreatSource
RemoteSafeBrowsingDatabaseManager::GetBrowseUrlThreatSource(
    CheckBrowseUrlType check_type) const {
  switch (check_type) {
    case CheckBrowseUrlType::kHashDatabase:
      return base::FeatureList::IsEnabled(
                 kSafeBrowsingNewGmsApiForBrowseUrlDatabaseCheck)
                 ? safe_browsing::ThreatSource::ANDROID_SAFEBROWSING
                 : safe_browsing::ThreatSource::REMOTE;
    case CheckBrowseUrlType::kHashRealTime:
      return safe_browsing::ThreatSource::ANDROID_SAFEBROWSING_REAL_TIME;
  }
}

safe_browsing::ThreatSource
RemoteSafeBrowsingDatabaseManager::GetNonBrowseUrlThreatSource() const {
  return safe_browsing::ThreatSource::REMOTE;
}

bool RemoteSafeBrowsingDatabaseManager::IsDownloadProtectionEnabled() const {
  return false;
}

void RemoteSafeBrowsingDatabaseManager::StartOnSBThread(
    scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory,
    const V4ProtocolConfig& config) {
  VLOG(1) << "RemoteSafeBrowsingDatabaseManager starting";
  SafeBrowsingDatabaseManager::StartOnSBThread(url_loader_factory, config);

  enabled_ = true;
}

void RemoteSafeBrowsingDatabaseManager::StopOnSBThread(bool shutdown) {
  // |shutdown| is not used.
  DCHECK(sb_task_runner()->RunsTasksInCurrentSequence());
  DVLOG(1) << "RemoteSafeBrowsingDatabaseManager stopping";

  // Call back and delete any remaining clients. OnRequestDone() modifies
  // |current_requests_|, so we make a copy first.
  std::vector<raw_ptr<ClientRequest, VectorExperimental>> to_callback(
      current_requests_);
  for (safe_browsing::RemoteSafeBrowsingDatabaseManager::ClientRequest* req :
       to_callback) {
    DVLOG(1) << "Stopping: Invoking unfinished req for URL " << req->url();
    req->OnRequestDone(SBThreatType::SB_THREAT_TYPE_SAFE, ThreatMetadata());
  }
  enabled_ = false;

  SafeBrowsingDatabaseManager::StopOnSBThread(shutdown);
}

bool RemoteSafeBrowsingDatabaseManager::IsDatabaseReady() const {
  return enabled_;
}

}  // namespace safe_browsing
