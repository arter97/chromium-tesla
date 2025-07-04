// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/visited_url_ranking/internal/visited_url_ranking_service_impl.h"

#include <array>
#include <map>
#include <memory>
#include <queue>
#include <string>
#include <variant>
#include <vector>

#include "base/barrier_callback.h"
#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/memory/scoped_refptr.h"
#include "base/metrics/field_trial_params.h"
#include "base/metrics/histogram_functions.h"
#include "base/rand_util.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"
#include "components/history/core/browser/history_service.h"
#include "components/segmentation_platform/public/features.h"
#include "components/segmentation_platform/public/input_context.h"
#include "components/segmentation_platform/public/prediction_options.h"
#include "components/segmentation_platform/public/proto/model_metadata.pb.h"
#include "components/segmentation_platform/public/result.h"
#include "components/segmentation_platform/public/segmentation_platform_service.h"
#include "components/segmentation_platform/public/types/processed_value.h"
#include "components/sync_sessions/session_sync_service.h"
#include "components/visited_url_ranking/internal/history_url_visit_data_fetcher.h"
#include "components/visited_url_ranking/internal/session_url_visit_data_fetcher.h"
#include "components/visited_url_ranking/public/features.h"
#include "components/visited_url_ranking/public/fetch_options.h"
#include "components/visited_url_ranking/public/fetch_result.h"
#include "components/visited_url_ranking/public/url_visit.h"
#include "components/visited_url_ranking/public/url_visit_aggregates_transformer.h"
#include "components/visited_url_ranking/public/url_visit_schema.h"
#include "components/visited_url_ranking/public/url_visit_util.h"
#include "components/visited_url_ranking/public/visited_url_ranking_service.h"

using segmentation_platform::AnnotatedNumericResult;
using segmentation_platform::InputContext;
using segmentation_platform::PredictionOptions;
using segmentation_platform::PredictionStatus;
using segmentation_platform::processing::ProcessedValue;
using visited_url_ranking::URLVisit;

namespace visited_url_ranking {

namespace {

// Default sampling rate for kSeen events recording. 1 in
// `kSeenRecordsSamplingRate` events are recorded randomly.
constexpr int kSeenRecordsSamplingRate = 1;

const char* EventNameForAction(ScoredURLUserAction action) {
  switch (action) {
    case kSeen:
      return kURLVisitSeenEventName;
    case kActivated:
      return kURLVisitActivatedEventName;
    case kDismissed:
      return kURLVisitDismissedEventName;
    default:
      NOTREACHED();
  }
}

// Combines `URLVisitVariant` data obtained from various fetchers into
// `URLVisitAggregate` objects. Leverages the `URLMergeKey` in order to
// reconcile what data belongs to the same aggregate object.
std::vector<URLVisitAggregate> ComputeURLVisitAggregates(
    std::vector<FetchResult> fetch_results) {
  std::map<URLMergeKey, URLVisitAggregate> url_visit_map = {};
  for (FetchResult& result : fetch_results) {
    if (result.status != FetchResult::Status::kSuccess) {
      // TODO(romanarora): Capture a metric for how often a specific fetcher
      // failed.
      continue;
    }

    for (std::pair<const URLMergeKey, URLVisitAggregate::URLVisitVariant>&
             url_data : result.data) {
      if (url_visit_map.find(url_data.first) == url_visit_map.end()) {
        url_visit_map.emplace(url_data.first,
                              URLVisitAggregate(url_data.first));
      }

      URLVisitAggregate& aggregate = url_visit_map.at(url_data.first);
      std::visit(
          URLVisitVariantHelper{
              [&aggregate](URLVisitAggregate::TabData& tab_data) {
                aggregate.fetcher_data_map.emplace(
                    tab_data.last_active_tab.session_name.has_value()
                        ? Fetcher::kSession
                        : Fetcher::kTabModel,
                    std::move(tab_data));
              },
              [&aggregate](URLVisitAggregate::HistoryData& history_data) {
                aggregate.fetcher_data_map.emplace(Fetcher::kHistory,
                                                   std::move(history_data));
              }},
          url_data.second);
    }
  }

  std::vector<URLVisitAggregate> url_visits;
  url_visits.reserve(url_visit_map.size());
  for (auto& url_visit_pair : url_visit_map) {
    url_visits.push_back(std::move(url_visit_pair.second));
  }
  url_visit_map.clear();

  return url_visits;
}

void SortScoredAggregatesAndCallback(
    std::vector<URLVisitAggregate> scored_visits,
    VisitedURLRankingService::RankURLVisitAggregatesCallback callback) {
  base::ranges::stable_sort(scored_visits, [](const auto& c1, const auto& c2) {
    // Sort such that higher scored entries precede lower scored entries.
    return c1.score > c2.score;
  });
  VLOG(2) << "visited_url_ranking: result size " << scored_visits.size();
  if (VLOG_IS_ON(2)) {
    for (const auto& visit : scored_visits) {
      VLOG(2) << "visited_url_ranking: Ordered ranked visit: " << visit.url_key
              << " " << *visit.score;
    }
  }
  std::move(callback).Run(ResultStatus::kSuccess, std::move(scored_visits));
}

}  // namespace

VisitedURLRankingServiceImpl::VisitedURLRankingServiceImpl(
    segmentation_platform::SegmentationPlatformService*
        segmentation_platform_service,
    std::map<Fetcher, std::unique_ptr<URLVisitDataFetcher>> data_fetchers,
    std::map<URLVisitAggregatesTransformType,
             std::unique_ptr<URLVisitAggregatesTransformer>> transformers)
    : segmentation_platform_service_(segmentation_platform_service),
      data_fetchers_(std::move(data_fetchers)),
      transformers_(std::move(transformers)),
      seen_record_delay_(base::Seconds(base::GetFieldTrialParamByFeatureAsInt(
          features::kVisitedURLRankingService,
          "seen_record_action_delay_sec",
          kSeenRecordDelaySec))),
      seen_records_sampling_rate_(base::GetFieldTrialParamByFeatureAsInt(
          features::kVisitedURLRankingService,
          "seen_record_action_sampling_rate",
          kSeenRecordsSamplingRate)) {}

VisitedURLRankingServiceImpl::~VisitedURLRankingServiceImpl() = default;

void VisitedURLRankingServiceImpl::FetchURLVisitAggregates(
    const FetchOptions& options,
    GetURLVisitAggregatesCallback callback) {
  auto merge_visits_and_callback =
      base::BindOnce(&VisitedURLRankingServiceImpl::MergeVisitsAndCallback,
                     weak_ptr_factory_.GetWeakPtr(), std::move(callback),
                     options, options.transforms);

  const auto fetch_barrier_callback = base::BarrierCallback<FetchResult>(
      options.fetcher_sources.size(), std::move(merge_visits_and_callback));

  for (const auto& fetcher_entry : options.fetcher_sources) {
    if (!data_fetchers_.count(fetcher_entry.first)) {
      // Some fetchers may not be available (e.g. due to policy) and the client
      // of the service may not know it, so handle the case silently for now.
      // TODO(crbug/346822243): check if there is a better fallback behavior.
      fetch_barrier_callback.Run(
          FetchResult(FetchResult::Status::kSuccess, {}));
      continue;
    }
    const auto& data_fetcher = data_fetchers_.at(fetcher_entry.first);
    data_fetcher->FetchURLVisitData(options, fetch_barrier_callback);
  }
}

void VisitedURLRankingServiceImpl::RankURLVisitAggregates(
    const Config& config,
    std::vector<URLVisitAggregate> visit_aggregates,
    RankURLVisitAggregatesCallback callback) {
  if (visit_aggregates.empty()) {
    std::move(callback).Run(ResultStatus::kSuccess, {});
    return;
  }

  if (!segmentation_platform_service_ ||
      !base::FeatureList::IsEnabled(
          segmentation_platform::features::
              kSegmentationPlatformURLVisitResumptionRanker)) {
    std::move(callback).Run(ResultStatus::kError, {});
    return;
  }

  std::deque<URLVisitAggregate> visits_queue;
  for (auto& visit : visit_aggregates) {
    visits_queue.push_back(std::move(visit));
  }
  visit_aggregates.clear();

  GetNextResult(config.key, std::move(visits_queue), {}, std::move(callback));
}

void VisitedURLRankingServiceImpl::RecordAction(
    ScoredURLUserAction action,
    const std::string& visit_id,
    segmentation_platform::TrainingRequestId visit_request_id) {
  DCHECK(!visit_id.empty());
  VLOG(2) << "visited_url_ranking: RecordAction for " << visit_id << " "
          << static_cast<int>(action);
  base::UmaHistogramEnumeration("VisitedURLRanking.ScoredURLAction", action);

  const char* event_name = EventNameForAction(action);
  segmentation_platform::DatabaseClient::StructuredEvent visit_event = {
      event_name, {{visit_id, 1}}};
  segmentation_platform::DatabaseClient* client =
      segmentation_platform_service_->GetDatabaseClient();
  if (client) {
    client->AddEvent(visit_event);
  }

  base::TimeDelta wait_for_activation = base::TimeDelta();
  // If the action is kSeen, then wait for some time before recording this as
  // result, in case the user clicks on the suggestion. Effectively, this would
  // assume if the user clicks on first 5 mins, then it's a success, otherwise
  // failure.
  if (action == ScoredURLUserAction::kSeen) {
    if (base::RandInt(1, seen_records_sampling_rate_) > 1) {
      return;
    }
    wait_for_activation = seen_record_delay_;
  }
  base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&VisitedURLRankingServiceImpl::TriggerTrainingData,
                     weak_ptr_factory_.GetWeakPtr(), action, visit_id,
                     visit_request_id),
      wait_for_activation);
}

void VisitedURLRankingServiceImpl::TriggerTrainingData(
    ScoredURLUserAction action,
    const std::string& visit_id,
    segmentation_platform::TrainingRequestId visit_request_id) {
  // Trigger UKM data collection on action.
  auto labels = segmentation_platform::TrainingLabels();
  labels.output_metric = std::make_pair("action", static_cast<int>(action));
  segmentation_platform_service_->CollectTrainingData(
      segmentation_platform::proto::SegmentId::
          OPTIMIZATION_TARGET_URL_VISIT_RESUMPTION_RANKER,
      visit_request_id, labels, base::DoNothing());
}

void VisitedURLRankingServiceImpl::MergeVisitsAndCallback(
    GetURLVisitAggregatesCallback callback,
    const FetchOptions& options,
    const std::vector<URLVisitAggregatesTransformType>& ordered_transforms,
    std::vector<FetchResult> fetcher_results) {
  std::queue<URLVisitAggregatesTransformType> transform_type_queue;
  for (const auto& transform_type : ordered_transforms) {
    transform_type_queue.push(transform_type);
  }

  TransformVisitsAndCallback(
      std::move(callback), options, std::move(transform_type_queue),
      URLVisitAggregatesTransformer::Status::kSuccess,
      ComputeURLVisitAggregates(std::move(fetcher_results)));
}

void VisitedURLRankingServiceImpl::TransformVisitsAndCallback(
    GetURLVisitAggregatesCallback callback,
    const FetchOptions& options,
    std::queue<URLVisitAggregatesTransformType> transform_type_queue,
    URLVisitAggregatesTransformer::Status status,
    std::vector<URLVisitAggregate> aggregates) {
  if (status == URLVisitAggregatesTransformer::Status::kError) {
    std::move(callback).Run(ResultStatus::kError, {});
    return;
  }

  if (transform_type_queue.empty() || aggregates.empty()) {
    std::move(callback).Run(ResultStatus::kSuccess, std::move(aggregates));
    return;
  }

  auto transform_type = transform_type_queue.front();
  transform_type_queue.pop();
  const auto it = transformers_.find(transform_type);
  if (it == transformers_.end()) {
    std::move(callback).Run(ResultStatus::kError, {});
    return;
  }
  it->second->Transform(
      std::move(aggregates), options,
      base::BindOnce(&VisitedURLRankingServiceImpl::TransformVisitsAndCallback,
                     weak_ptr_factory_.GetWeakPtr(), std::move(callback),
                     options, std::move(transform_type_queue)));
}

void VisitedURLRankingServiceImpl::GetNextResult(
    const std::string& segmentation_key,
    std::deque<URLVisitAggregate> visit_aggregates,
    std::vector<URLVisitAggregate> scored_visits,
    RankURLVisitAggregatesCallback callback) {
  if (visit_aggregates.empty()) {
    SortScoredAggregatesAndCallback(std::move(scored_visits),
                                    std::move(callback));
    return;
  }

  PredictionOptions options;
  options.on_demand_execution = true;
  scoped_refptr<InputContext> input_context =
      AsInputContext(kURLVisitAggregateSchema, visit_aggregates.front());
  segmentation_platform_service_->GetAnnotatedNumericResult(
      segmentation_key, options, input_context,
      base::BindOnce(&VisitedURLRankingServiceImpl::OnGetResult,
                     weak_ptr_factory_.GetWeakPtr(), segmentation_key,
                     std::move(visit_aggregates), std::move(scored_visits),
                     std::move(callback)));
}

void VisitedURLRankingServiceImpl::OnGetResult(
    const std::string& segmentation_key,
    std::deque<URLVisitAggregate> visit_aggregates,
    std::vector<URLVisitAggregate> scored_visits,
    RankURLVisitAggregatesCallback callback,
    const AnnotatedNumericResult& result) {
  float model_score = -1;
  if (result.status == PredictionStatus::kSucceeded) {
    model_score = *result.GetResultForLabel(segmentation_key);
  }
  auto visit = std::move(visit_aggregates.front());
  visit.request_id = result.request_id;
  visit.score = model_score;
  visit_aggregates.pop_front();
  scored_visits.emplace_back(std::move(visit));

  GetNextResult(segmentation_key, std::move(visit_aggregates),
                std::move(scored_visits), std::move(callback));
}

}  // namespace visited_url_ranking
