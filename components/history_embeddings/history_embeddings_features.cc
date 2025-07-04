// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/history_embeddings/history_embeddings_features.h"

#include "base/feature_list.h"
#include "base/metrics/field_trial_params.h"
#include "build/build_config.h"

#if BUILDFLAG(IS_CHROMEOS)
#include "chromeos/constants/chromeos_features.h"
#endif

namespace history_embeddings {

// Please use `IsFeatureManagementHistoryEmbeddingEnabled()` instead
// of using `kHistoryEmbeddings` directly.
BASE_FEATURE(kHistoryEmbeddings,
             "HistoryEmbeddings",
             base::FEATURE_DISABLED_BY_DEFAULT);

const base::FeatureParam<bool> kShowSourcePassages(&kHistoryEmbeddings,
                                                   "ShowSourcePassages",
                                                   false);

const base::FeatureParam<int> kPassageExtractionDelay(&kHistoryEmbeddings,
                                                      "PassageExtractionDelay",
                                                      5000);

const base::FeatureParam<int> kPassageExtractionMaxWordsPerAggregatePassage(
    &kHistoryEmbeddings,
    "PassageExtractionMaxWordsPerAggregatePassage",
    200);

const base::FeatureParam<int> kSearchQueryMinimumWordCount(
    &kHistoryEmbeddings,
    "SearchQueryMinimumWordCount",
    3);
const base::FeatureParam<int> kSearchPassageMinimumWordCount(
    &kHistoryEmbeddings,
    "SearchPassageMinimumWordCount",
    5);

const base::FeatureParam<int> kSearchResultItemCount(&kHistoryEmbeddings,
                                                     "SearchResultItemCount",
                                                     3);

const base::FeatureParam<bool> kAtKeywordAcceleration(&kHistoryEmbeddings,
                                                      "AtKeywordAcceleration",
                                                      false);

const base::FeatureParam<double> kContentVisibilityThreshold(
    &kHistoryEmbeddings,
    "ContentVisibilityThreshold",
    0.5);

const base::FeatureParam<double> kSearchScoreThreshold(&kHistoryEmbeddings,
                                                       "SearchScoreThreshold",
                                                       0.9);

const base::FeatureParam<bool> kUseMlAnswerer(&kHistoryEmbeddings,
                                              "UseMlAnswerer",
                                              false);

const base::FeatureParam<bool> kUseMlEmbedder(&kHistoryEmbeddings,
                                              "UseMlEmbedder",
                                              true);

const base::FeatureParam<bool> kOmniboxUnscoped(&kHistoryEmbeddings,
                                                "OmniboxUnscoped",
                                                false);

const base::FeatureParam<int> kScheduledEmbeddingsMax(&kHistoryEmbeddings,
                                                      "ScheduledEmbeddingsMax",
                                                      1);

const base::FeatureParam<bool> kSendQualityLog(&kHistoryEmbeddings,
                                               "SendQualityLog",
                                               false);

const base::FeatureParam<int> kEmbedderNumThreads(&kHistoryEmbeddings,
                                                  "EmbeddingsNumThreads",
                                                  4);

const base::FeatureParam<int> kEmbedderCacheSize(&kHistoryEmbeddings,
                                                 "EmbedderCacheSize",
                                                 1000);

const base::FeatureParam<int> kMaxPassagesPerPage(&kHistoryEmbeddings,
                                                  "MaxPassagesPerPage",
                                                  30);

const base::FeatureParam<bool> kDeleteEmbeddings(&kHistoryEmbeddings,
                                                 "DeleteEmbeddings",
                                                 false);
const base::FeatureParam<bool> kRebuildEmbeddings(&kHistoryEmbeddings,
                                                  "RebuildEmbeddings",
                                                  true);

bool IsHistoryEmbeddingEnabled() {
#if BUILDFLAG(IS_CHROMEOS)
  return chromeos::features::IsFeatureManagementHistoryEmbeddingEnabled() &&
         base::FeatureList::IsEnabled(kHistoryEmbeddings);
#else
  return base::FeatureList::IsEnabled(kHistoryEmbeddings);
#endif
}

}  // namespace history_embeddings
