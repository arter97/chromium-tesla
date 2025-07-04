// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/optimization_guide/core/model_execution/model_execution_features.h"

#include "base/containers/contains.h"
#include "base/feature_list.h"
#include "base/metrics/field_trial_params.h"
#include "base/notreached.h"
#include "components/optimization_guide/core/model_execution/feature_keys.h"
#include "components/optimization_guide/core/optimization_guide_features.h"

namespace optimization_guide {
namespace features {
namespace internal {

// Settings visibility features.
BASE_FEATURE(kComposeSettingsVisibility,
             "ComposeSettingsVisibility",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kTabOrganizationSettingsVisibility,
             "TabOrganizationSettingsVisibility",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kWallpaperSearchSettingsVisibility,
             "WallpaperSearchSettingsVisibility",
             base::FEATURE_DISABLED_BY_DEFAULT);

// Graduation features.

// Note: ComposeGraduated is enabled by default because the feature is
// country-restricted at runtime.
BASE_FEATURE(kComposeGraduated,
             "ComposeGraduated",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kTabOrganizationGraduated,
             "TabOrganizationGraduated",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kWallpaperSearchGraduated,
             "WallpaperSearchGraduated",
             base::FEATURE_ENABLED_BY_DEFAULT);

BASE_FEATURE(kExperimentalAIIPHPromoRampUp,
             "ExperimentalAIIPHPromoRampUp",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kModelExecutionCapabilityDisable,
             "ModelExecutionCapabilityDisable",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kModelAdaptationCompose,
             "ModelAdaptationCompose",
             base::FEATURE_ENABLED_BY_DEFAULT);

BASE_FEATURE(kOnDeviceModelTestFeature,
             "OnDeviceModelTestFeature",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kOnDeviceModelPromptApiFeature,
             "OnDeviceModelPromptApiFeature",
             base::FEATURE_DISABLED_BY_DEFAULT);

bool IsGraduatedFeature(UserVisibleFeatureKey feature) {
  bool is_graduated = false;
  switch (feature) {
    // Actual features.
    case UserVisibleFeatureKey::kCompose:
      is_graduated = base::FeatureList::IsEnabled(kComposeGraduated);
      break;
    case UserVisibleFeatureKey::kTabOrganization:
      is_graduated = base::FeatureList::IsEnabled(kTabOrganizationGraduated);
      break;
    case UserVisibleFeatureKey::kWallpaperSearch:
      is_graduated = base::FeatureList::IsEnabled(kWallpaperSearchGraduated);
      break;
  }
  DCHECK(!is_graduated ||
         !base::FeatureList::IsEnabled(
             *GetFeatureToUseToCheckSettingsVisibility(feature)))
      << "Feature should not be both graduated and visible in settings: "
      << GetFeatureToUseToCheckSettingsVisibility(feature)->name;
  return is_graduated;
}

const base::Feature* GetFeatureToUseToCheckSettingsVisibility(
    UserVisibleFeatureKey feature) {
  switch (feature) {
    case UserVisibleFeatureKey::kCompose:
      return &kComposeSettingsVisibility;
    case UserVisibleFeatureKey::kTabOrganization:
      return &kTabOrganizationSettingsVisibility;
    case UserVisibleFeatureKey::kWallpaperSearch:
      return &kWallpaperSearchSettingsVisibility;
  }
}

base::flat_set<UserVisibleFeatureKey> GetAllowedFeaturesForUnsignedUser() {
  std::vector<UserVisibleFeatureKey> allowed_features;
  for (auto key : kAllUserVisibleFeatureKeys) {
    const auto* feature = GetFeatureToUseToCheckSettingsVisibility(key);
    if (GetFieldTrialParamByFeatureAsBool(*feature, "allow_unsigned_user",
                                          false)) {
      allowed_features.push_back(key);
    }
  }
  return allowed_features;
}

// LINT.IfChange(IsOnDeviceModelEnabled)
//
// On-device supported features should return true.
// `GetOnDeviceFeatureRecentlyUsedPref` should return a valid pref for each
// on-device feature.
bool IsOnDeviceModelEnabled(ModelBasedCapabilityKey feature) {
  switch (feature) {
    case ModelBasedCapabilityKey::kCompose:
      return base::FeatureList::IsEnabled(
          optimization_guide::features::kOptimizationGuideComposeOnDeviceEval);
    case ModelBasedCapabilityKey::kTest:
      return base::FeatureList::IsEnabled(kOnDeviceModelTestFeature);
    case ModelBasedCapabilityKey::kTabOrganization:
    case ModelBasedCapabilityKey::kWallpaperSearch:
    case ModelBasedCapabilityKey::kTextSafety:
      return false;
    case ModelBasedCapabilityKey::kPromptApi:
      return true;
  }
}
// LINT.ThenChange(model_execution_prefs.cc:GetOnDeviceFeatureRecentlyUsedPref,
//                 IsOnDeviceModelAdaptationEnabled,
//                 GetOptimizationTargetForModelAdaptation)

// LINT.IfChange(IsOnDeviceModelAdaptationEnabled)
//
// On-device model adaptation features should return true.
// `GetOptimizationTargetForModelAdaptation` should return a valid optimization
// target for each on-device model adaptation feature, that will be used to
// download the adaptation model.
bool IsOnDeviceModelAdaptationEnabled(ModelBasedCapabilityKey feature) {
  switch (feature) {
    case ModelBasedCapabilityKey::kCompose:
      return base::FeatureList::IsEnabled(kModelAdaptationCompose);
    case ModelBasedCapabilityKey::kTest:
      return base::GetFieldTrialParamByFeatureAsBool(
          kOnDeviceModelTestFeature, "enable_adaptation", false);
    case ModelBasedCapabilityKey::kPromptApi:
      return base::GetFieldTrialParamByFeatureAsBool(
          kOnDeviceModelPromptApiFeature, "enable_adaptation", false);
    case ModelBasedCapabilityKey::kTabOrganization:
    case ModelBasedCapabilityKey::kWallpaperSearch:
    case ModelBasedCapabilityKey::kTextSafety:
      return false;
  }
}
// LINT.ThenChange(IsOnDeviceModelEnabled)

// LINT.IfChange(GetOptimizationTargetForModelAdaptation)
proto::OptimizationTarget GetOptimizationTargetForModelAdaptation(
    ModelBasedCapabilityKey feature) {
  switch (feature) {
    case ModelBasedCapabilityKey::kCompose:
      return proto::OPTIMIZATION_TARGET_COMPOSE;
    case ModelBasedCapabilityKey::kTest:
      return proto::OPTIMIZATION_TARGET_MODEL_VALIDATION;
    case ModelBasedCapabilityKey::kPromptApi:
    case ModelBasedCapabilityKey::kTabOrganization:
    case ModelBasedCapabilityKey::kWallpaperSearch:
    case ModelBasedCapabilityKey::kTextSafety:
      NOTREACHED_IN_MIGRATION();
  }
  return proto::OPTIMIZATION_TARGET_UNKNOWN;
}
// LINT.ThenChange(IsOnDeviceModelEnabled)

}  // namespace internal

}  // namespace features

}  // namespace optimization_guide
