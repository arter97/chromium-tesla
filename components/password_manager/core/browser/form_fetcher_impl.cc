// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/password_manager/core/browser/form_fetcher_impl.h"

#include <iterator>
#include <memory>
#include <utility>
#include <vector>

#include "base/check_op.h"
#include "base/containers/contains.h"
#include "base/memory/raw_ptr.h"
#include "base/notreached.h"
#include "base/observer_list.h"
#include "base/ranges/algorithm.h"
#include "build/build_config.h"
#include "components/autofill/core/common/save_password_progress_logger.h"
#include "components/password_manager/core/browser/browser_save_password_progress_logger.h"
#include "components/password_manager/core/browser/credentials_filter.h"
#include "components/password_manager/core/browser/password_feature_manager.h"
#include "components/password_manager/core/browser/password_form.h"
#include "components/password_manager/core/browser/password_manager_client.h"
#include "components/password_manager/core/browser/password_manager_util.h"
#include "components/password_manager/core/browser/password_store/interactions_stats.h"
#include "components/password_manager/core/browser/password_store/password_store_interface.h"
#include "components/password_manager/core/browser/password_store/password_store_util.h"
#include "components/password_manager/core/browser/password_store/psl_matching_helper.h"
#include "components/password_manager/core/browser/password_store/smart_bubble_stats_store.h"
#include "components/password_manager/core/common/password_manager_features.h"

using Logger = autofill::SavePasswordProgressLogger;
using password_manager_util::GetMatchType;

namespace password_manager {

namespace {

std::vector<std::unique_ptr<PasswordForm>> ConvertToUniquePtr(
    std::vector<PasswordForm> forms) {
  std::vector<std::unique_ptr<PasswordForm>> result;
  result.reserve(forms.size());
  for (auto& form : forms) {
    result.push_back(std::make_unique<PasswordForm>(std::move(form)));
  }
  return result;
}

}  // namespace

FormFetcherImpl::FormFetcherImpl(PasswordFormDigest form_digest,
                                 PasswordManagerClient* client,
                                 bool should_migrate_http_passwords)
    : form_digest_(std::move(form_digest)),
      client_(client),
      should_migrate_http_passwords_(should_migrate_http_passwords &&
                                     form_digest_.scheme ==
                                         PasswordForm::Scheme::kHtml) {}

FormFetcherImpl::~FormFetcherImpl() = default;

void FormFetcherImpl::AddConsumer(FormFetcher::Consumer* consumer) {
  DCHECK(consumer);
  consumers_.AddObserver(consumer);
  if (state_ == State::NOT_WAITING) {
    consumer->OnFetchCompleted();
  }
}

void FormFetcherImpl::RemoveConsumer(FormFetcher::Consumer* consumer) {
  DCHECK(consumers_.HasObserver(consumer));
  consumers_.RemoveObserver(consumer);
}

void FormFetcherImpl::Fetch() {
  std::unique_ptr<BrowserSavePasswordProgressLogger> logger;
  if (password_manager_util::IsLoggingActive(client_)) {
    logger = std::make_unique<BrowserSavePasswordProgressLogger>(
        client_->GetLogManager());
    logger->LogMessage(Logger::STRING_FETCH_METHOD);
    logger->LogNumber(Logger::STRING_FORM_FETCHER_STATE,
                      static_cast<int>(state_));
  }

  if (state_ == State::WAITING) {
    // There is currently a password store query in progress, need to re-fetch
    // store results later.
    need_to_refetch_ = true;
    return;
  }

  state_ = State::WAITING;

  // Issue a fetch from the profile store and, if it exists, also from the
  // account store.
  // Set up |wait_counter_| *before* triggering any of the fetches. This ensures
  // that things work correctly (i.e. we don't notify of completion too early)
  // even if the fetches return synchronously (which is the case in tests).
  wait_counter_++;
  // Clears the flag since it will be outdated after this fetch is finished.
  were_grouped_credentials_availible_ = false;
  PasswordStoreInterface* profile_password_store =
      client_->GetProfilePasswordStore();
  if (!profile_password_store) {
    if (logger) {
      logger->LogMessage(Logger::STRING_NO_STORE);
    }

    std::vector<std::unique_ptr<PasswordForm>> results;
    AggregatePasswordStoreResults(std::move(results));
    return;
  }

  PasswordStoreInterface* account_password_store =
      client_->GetAccountPasswordStore();
  if (account_password_store) {
    wait_counter_++;
  }

  profile_password_store->GetLogins(form_digest_,
                                    weak_ptr_factory_.GetWeakPtr());
  if (account_password_store) {
    account_password_store->GetLogins(form_digest_,
                                      weak_ptr_factory_.GetWeakPtr());
  }

// The statistics isn't needed on mobile, only on desktop. Let's save some
// processor cycles.
#if !BUILDFLAG(IS_IOS) && !BUILDFLAG(IS_ANDROID)
  // The statistics is needed for the "Save password?" bubble.
  password_manager::SmartBubbleStatsStore* stats_store =
      profile_password_store->GetSmartBubbleStatsStore();
  // `stats_store` can be null in tests.
  if (stats_store) {
    stats_store->GetSiteStats(form_digest_.url.DeprecatedGetOriginAsURL(),
                              weak_ptr_factory_.GetWeakPtr());
  }
#endif
}

FormFetcherImpl::State FormFetcherImpl::GetState() const {
  return state_;
}

const std::vector<InteractionsStats>& FormFetcherImpl::GetInteractionsStats()
    const {
  return interactions_stats_;
}

base::span<const PasswordForm> FormFetcherImpl::GetInsecureCredentials() const {
  return insecure_credentials_;
}

base::span<const PasswordForm> FormFetcherImpl::GetNonFederatedMatches() const {
  return non_federated_;
}

base::span<const PasswordForm> FormFetcherImpl::GetFederatedMatches() const {
  return federated_;
}

bool FormFetcherImpl::IsBlocklisted() const {
  if (client_->GetPasswordFeatureManager()->IsOptedInForAccountStorage() &&
      client_->GetPasswordFeatureManager()->GetDefaultPasswordStore() ==
          PasswordForm::Store::kAccountStore) {
    return is_blocklisted_in_account_store_;
  }
  return is_blocklisted_in_profile_store_;
}

bool FormFetcherImpl::IsMovingBlocked(const signin::GaiaIdHash& destination,
                                      const std::u16string& username) const {
  for (const std::vector<PasswordForm>& matches_vector :
       {federated_, non_federated_}) {
    for (const auto& form : matches_vector) {
      // Only local entries can be moved to the account store (though
      // account store matches should never have |moving_blocked_for_list|
      // entries anyway).
      if (form.IsUsingAccountStore()) {
        continue;
      }
      // Ignore non-exact matches for blocking moving. PLS, affiliated and
      // grouped matches are ignored.
      if (GetMatchType(form) !=
          password_manager_util::GetLoginMatchType::kExact) {
        continue;
      }
      if (form.username_value != username) {
        continue;
      }
      if (base::Contains(form.moving_blocked_for_list, destination)) {
        return true;
      }
    }
  }
  return false;
}

const std::vector<raw_ptr<const PasswordForm, VectorExperimental>>&
FormFetcherImpl::GetAllRelevantMatches() const {
  return non_federated_same_scheme_;
}

base::span<const PasswordForm> FormFetcherImpl::GetBestMatches() const {
  return best_matches_;
}

const PasswordForm* FormFetcherImpl::GetPreferredMatch() const {
  if (best_matches_.empty()) {
    return nullptr;
  }
  return &(*best_matches_.begin());
}

std::unique_ptr<FormFetcher> FormFetcherImpl::Clone() {
  // Create the copy without the "HTTPS migration" activated. If it was needed,
  // then it was done by |this| already.
  auto result = std::make_unique<FormFetcherImpl>(form_digest_, client_, false);

  if (state_ != State::NOT_WAITING) {
    // There are no store results to copy, trigger a Fetch on the clone instead.
    result->Fetch();
    return result;
  }

  result->non_federated_ = non_federated_;
  result->federated_ = federated_;
  result->is_blocklisted_in_account_store_ = is_blocklisted_in_account_store_;
  result->is_blocklisted_in_profile_store_ = is_blocklisted_in_profile_store_;
  result->best_matches_ = password_manager_util::FindBestMatches(
      result->non_federated_, form_digest_.scheme,
      result->non_federated_same_scheme_);

  result->interactions_stats_ = interactions_stats_;
  result->insecure_credentials_ = insecure_credentials_;
  result->state_ = state_;
  result->need_to_refetch_ = need_to_refetch_;
  result->profile_store_backend_error_ = profile_store_backend_error_;

  return result;
}

std::optional<PasswordStoreBackendError>
FormFetcherImpl::GetProfileStoreBackendError() const {
  return profile_store_backend_error_;
}

std::optional<PasswordStoreBackendError>
FormFetcherImpl::GetAccountStoreBackendError() const {
  return account_store_backend_error_;
}

bool FormFetcherImpl::WereGroupedCredentialsAvailable() const {
  return were_grouped_credentials_availible_;
}

void FormFetcherImpl::FindMatchesAndNotifyConsumers(
    std::vector<std::unique_ptr<PasswordForm>> results) {
  DCHECK_EQ(State::WAITING, state_);
  SplitResults(std::move(results));

  best_matches_ = password_manager_util::FindBestMatches(
      non_federated_, form_digest_.scheme, non_federated_same_scheme_);

  state_ = State::NOT_WAITING;
  for (auto& consumer : consumers_) {
    consumer.OnFetchCompleted();
  }
}

void FormFetcherImpl::SplitResults(
    std::vector<std::unique_ptr<PasswordForm>> forms) {
  is_blocklisted_in_profile_store_ = false;
  is_blocklisted_in_account_store_ = false;
  non_federated_.clear();
  federated_.clear();
  insecure_credentials_.clear();
  for (auto& form : forms) {
    if (form->blocked_by_user) {
      // Ignore non-exact matches for blocklisted entries. PLS, affiliated and
      // grouped matches are ignored.
      if (password_manager_util::GetMatchType(*form) ==
              password_manager_util::GetLoginMatchType::kExact &&
          form->scheme == form_digest_.scheme) {
        if (form->IsUsingAccountStore()) {
          is_blocklisted_in_account_store_ = true;
        } else {
          is_blocklisted_in_profile_store_ = true;
        }
      }
    } else {
      if (!form->password_issues.empty()) {
        insecure_credentials_.push_back(*form);
      }
      if (form->IsFederatedCredential()) {
        federated_.push_back(*form);
      } else {
        non_federated_.push_back(*form);
      }
    }
  }
}

void FormFetcherImpl::OnGetPasswordStoreResults(
    std::vector<std::unique_ptr<PasswordForm>> results) {
  // This class overrides OnGetPasswordStoreResultsFrom() (the version of this
  // method that also receives the originating store), so the store-less version
  // never gets called.
  NOTREACHED_IN_MIGRATION();
}

void FormFetcherImpl::OnGetPasswordStoreResultsFrom(
    PasswordStoreInterface* store,
    std::vector<std::unique_ptr<PasswordForm>> results) {
  NOTIMPLEMENTED();
}

void FormFetcherImpl::OnGetPasswordStoreResultsOrErrorFrom(
    PasswordStoreInterface* store,
    LoginsResultOrError results_or_error) {
  // TODO(crbug.com/40239372): Handle errors coming from the account
  // store.
  if (store == client_->GetProfilePasswordStore()) {
    profile_store_backend_error_.reset();
    if (absl::holds_alternative<PasswordStoreBackendError>(results_or_error)) {
      profile_store_backend_error_ =
          absl::get<PasswordStoreBackendError>(results_or_error);
    }
  } else if (store == client_->GetAccountPasswordStore()) {
    account_store_backend_error_.reset();
    if (absl::holds_alternative<PasswordStoreBackendError>(results_or_error)) {
      account_store_backend_error_ =
          absl::get<PasswordStoreBackendError>(results_or_error);
    }
  }

  std::vector<PasswordForm> results =
      GetLoginsOrEmptyListOnFailure(std::move(results_or_error));
  if (filter_grouped_credentials_) {
    auto grouped_credentials_count =
        std::erase_if(results, [](const auto& form) {
          return form.match_type == PasswordForm::MatchType::kGrouped;
        });
    // If users is using two password stores this code will executed twice.
    // Meaning that if either one of them had grouped credentials, the value
    // should be maintained.
    were_grouped_credentials_availible_ =
        were_grouped_credentials_availible_ || grouped_credentials_count > 0;
  }

  DCHECK_EQ(State::WAITING, state_);
  DCHECK_GT(wait_counter_, 0);

  if (should_migrate_http_passwords_ && results.empty() &&
      form_digest_.url.SchemeIs(url::kHttpsScheme)) {
    http_migrators_[store] = std::make_unique<HttpPasswordStoreMigrator>(
        url::Origin::Create(form_digest_.url), store,
        client_->GetNetworkContext(), this);
    // The migrator will call us back at ProcessMigratedForms().
    return;
  }

  AggregatePasswordStoreResults(ConvertToUniquePtr(std::move(results)));
}

void FormFetcherImpl::AggregatePasswordStoreResults(
    std::vector<std::unique_ptr<PasswordForm>> results) {
  // Store the results.
  for (auto& form : results) {
    partial_results_.push_back(std::move(form));
  }

  // If we're still awaiting more results, nothing else to do.
  if (--wait_counter_ > 0) {
    return;
  }

  if (need_to_refetch_) {
    // The received results are no longer up to date, need to re-request.
    state_ = State::NOT_WAITING;
    partial_results_.clear();
    Fetch();
    need_to_refetch_ = false;
    return;
  }

  if (password_manager_util::IsLoggingActive(client_)) {
    BrowserSavePasswordProgressLogger logger(client_->GetLogManager());
    logger.LogMessage(Logger::STRING_ON_GET_STORE_RESULTS_METHOD);
    logger.LogNumber(Logger::STRING_NUMBER_RESULTS, partial_results_.size());
  }
  FindMatchesAndNotifyConsumers(std::move(partial_results_));
}

void FormFetcherImpl::OnGetSiteStatistics(
    std::vector<InteractionsStats> stats) {
  interactions_stats_ = std::move(stats);
}

void FormFetcherImpl::ProcessMigratedForms(
    std::vector<std::unique_ptr<PasswordForm>> forms) {
  // The migration from HTTP to HTTPS (within the profile store) was finished.
  // Continue processing with the migrated results.
  AggregatePasswordStoreResults(std::move(forms));
}

}  // namespace password_manager
