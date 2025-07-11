// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/visited_url_ranking/public/url_visit_util.h"

#include <math.h>

#include <array>
#include <optional>
#include <string>

#include "base/memory/scoped_refptr.h"
#include "build/build_config.h"
#include "components/segmentation_platform/public/input_context.h"
#include "components/segmentation_platform/public/types/processed_value.h"
#include "components/visited_url_ranking/public/fetch_result.h"
#include "components/visited_url_ranking/public/url_visit_schema.h"
#include "url/gurl.h"

using segmentation_platform::InputContext;
using segmentation_platform::processing::ProcessedValue;

namespace visited_url_ranking {

namespace {

// Bucketize the value to exponential buckets. Returns lower bound of the bucket.
float BucketizeExp(int64_t value, int max_buckets) {
  if (value <= 0) {
    return 0;
  }
  int log_val = floor(log2(value));
  if (log_val >= max_buckets) {
    log_val = max_buckets;
  }
  return pow(2, log_val);
}

// Used as input to the model, it is ok to group some platforms together. Only
// lists platforms that use the service.
enum class PlatformType {
  kUnknown = 0,
  kWindows = 1,
  kLinux = 2,
  kMac = 3,
  kIos = 4,
  kAndroid = 5,
  kOther = 6
};

PlatformType GetPlatformInput() {
#if BUILDFLAG(IS_WIN)
  return PlatformType::kWindows;
#elif BUILDFLAG(IS_MAC)
  return PlatformType::kMac;
#elif BUILDFLAG(IS_LINUX)
  return PlatformType::kLinux;
#elif BUILDFLAG(IS_IOS)
  return PlatformType::kIos;
#elif BUILDFLAG(IS_ANDROID)
  return PlatformType::kAndroid;
#else
  return PlatformType::kOther;
#endif
}

}  // namespace

// TODO(crbug.com/335200723): Integrate client configurable merging and
// deduplication logic to produce "merge" keys for provided URLs.
URLMergeKey ComputeURLMergeKey(const GURL& url) {
  return url.spec();
}

scoped_refptr<InputContext> AsInputContext(
    const std::array<FieldSchema, kNumInputs>& fields_schema,
    const URLVisitAggregate& url_visit_aggregate) {
  base::flat_map<std::string, ProcessedValue> signal_value_map;

  auto* local_tab_data =
      (url_visit_aggregate.fetcher_data_map.find(Fetcher::kTabModel) !=
       url_visit_aggregate.fetcher_data_map.end())
          ? std::get_if<URLVisitAggregate::TabData>(
                &url_visit_aggregate.fetcher_data_map.at(Fetcher::kTabModel))
          : nullptr;

  auto* session_tab_data =
      (url_visit_aggregate.fetcher_data_map.find(Fetcher::kSession) !=
       url_visit_aggregate.fetcher_data_map.end())
          ? std::get_if<URLVisitAggregate::TabData>(
                &url_visit_aggregate.fetcher_data_map.at(Fetcher::kSession))
          : nullptr;

  // For cases where distinguishing between local and session tab data is not
  // relevant, prioritize using local tab data when constructing signals.
  auto* tab_data = local_tab_data ? local_tab_data : session_tab_data;

  auto* history_data =
      (url_visit_aggregate.fetcher_data_map.find(Fetcher::kHistory) !=
       url_visit_aggregate.fetcher_data_map.end())
          ? std::get_if<URLVisitAggregate::HistoryData>(
                &url_visit_aggregate.fetcher_data_map.at(Fetcher::kHistory))
          : nullptr;

  for (const auto& field_schema : fields_schema) {
    // Initialized to a sentinel value of -1 which represents an undefined
    // value.
    auto value = ProcessedValue::FromFloat(-1);
    switch (field_schema.signal) {
      case kTimeSinceLastModifiedSec: {
        base::TimeDelta time_since_last_modified =
            base::Time::Now() - url_visit_aggregate.GetLastVisitTime();
        value = ProcessedValue::FromFloat(time_since_last_modified.InSeconds());
      } break;
      case kTimeSinceLastActiveSec:
        if (tab_data && !tab_data->last_active.is_null()) {
          base::TimeDelta time_since_last_active =
              base::Time::Now() - tab_data->last_active;
          value = ProcessedValue::FromFloat(time_since_last_active.InSeconds());
        }
        break;
      case kTimeActiveForTimePeriodSec:
        if (history_data) {
          value = ProcessedValue::FromFloat(
              history_data->total_foreground_duration.InSeconds());
        }
        break;
      case kNumTimesActive:
        value = ProcessedValue::FromFloat(url_visit_aggregate.num_times_active);
        break;
      case kLocalTabCount:
        if (local_tab_data) {
          value = ProcessedValue::FromFloat(
              BucketizeExp(local_tab_data->tab_count, 50));
        }
        break;
      case kSessionTabCount:
        if (session_tab_data) {
          value = ProcessedValue::FromFloat(
              BucketizeExp(session_tab_data->tab_count, 50));
        }
        break;
      case kVisitCount:
        if (history_data) {
          value = ProcessedValue::FromFloat(history_data->visit_count);
        }
        break;
      case kIsBookmarked:
        value = ProcessedValue::FromFloat(url_visit_aggregate.bookmarked);
        break;
      case kIsPinned:
        if (tab_data) {
          value = ProcessedValue::FromFloat(tab_data->pinned);
        }
        break;
      case kIsInTabGroup:
        if (tab_data) {
          value = ProcessedValue::FromFloat(tab_data->in_group);
        }
        break;
      case kIsInCluster:
        if (history_data) {
          value = ProcessedValue::FromFloat(history_data->in_cluster);
        }
        break;
      case kHasUrlKeyedImage:
        if (history_data) {
          value = ProcessedValue::FromFloat(
              history_data->last_visited.content_annotations
                  .has_url_keyed_image);
        }
        break;
      case kHasAppId:
        if (history_data) {
          value =
              ProcessedValue::FromFloat(history_data->last_app_id.has_value());
        }
        break;
      case kPlatform:
        value =
            ProcessedValue::FromFloat(static_cast<float>(GetPlatformInput()));
        break;
      case kSeenCountLastDay:
      case kActivatedCountLastDay:
      case kDismissedCountLastDay:
      case kSeenCountLast7Days:
      case kActivatedCountLast7Days:
      case kDismissedCountLast7Days:
      case kSeenCountLast30Days:
      case kActivatedCountLast30Days:
      case kDismissedCountLast30Days:
        if (url_visit_aggregate.metrics_signals.find(field_schema.name) !=
            url_visit_aggregate.metrics_signals.end()) {
          value = ProcessedValue::FromFloat(
              url_visit_aggregate.metrics_signals.at(field_schema.name));
        }
        break;
    }

    signal_value_map.emplace(field_schema.name, std::move(value));
  }

  scoped_refptr<InputContext> input_context =
      base::MakeRefCounted<InputContext>();
  input_context->metadata_args = std::move(signal_value_map);
  return input_context;
}

const URLVisitAggregate::Tab* GetTabIfExists(
    const URLVisitAggregate& url_visit_aggregate) {
  const auto& fetcher_data_map = url_visit_aggregate.fetcher_data_map;
  if (fetcher_data_map.find(Fetcher::kSession) != fetcher_data_map.end()) {
    const URLVisitAggregate::TabData* tab_data =
        std::get_if<URLVisitAggregate::TabData>(
            &fetcher_data_map.at(Fetcher::kSession));
    if (tab_data) {
      return &tab_data->last_active_tab;
    }
  }

  if (fetcher_data_map.find(Fetcher::kTabModel) != fetcher_data_map.end()) {
    const URLVisitAggregate::TabData* tab_data =
        std::get_if<URLVisitAggregate::TabData>(
            &fetcher_data_map.at(Fetcher::kTabModel));
    if (tab_data) {
      return &tab_data->last_active_tab;
    }
  }

  return nullptr;
}

const history::AnnotatedVisit* GetHistoryEntryVisitIfExists(
    const URLVisitAggregate& url_visit_aggregate) {
  const auto& fetcher_data_map = url_visit_aggregate.fetcher_data_map;
  if (fetcher_data_map.find(Fetcher::kHistory) != fetcher_data_map.end()) {
    const URLVisitAggregate::HistoryData* history_data =
        std::get_if<URLVisitAggregate::HistoryData>(
            &fetcher_data_map.at(Fetcher::kHistory));
    if (history_data) {
      return &history_data->last_visited;
    }
  }

  return nullptr;
}

}  // namespace visited_url_ranking
