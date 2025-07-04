// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/fingerprinting_protection_filter/browser/fingerprinting_protection_filter_features.h"

#include "base/feature_list.h"
#include "base/metrics/field_trial_params.h"
#include "components/subresource_filter/core/mojom/subresource_filter.mojom.h"

namespace fingerprinting_protection_filter::features {

// When enabled, loads the Fingerprinting Protection component and evaluates
// resource requests on certain pages against the Fingerprinting Protection
// blocklist, possibly blocks via a subresource filter.
BASE_FEATURE(kEnableFingerprintingProtectionFilter,
             "EnableFingerprintingProtectionFilter",
             base::FEATURE_DISABLED_BY_DEFAULT);

constexpr base::FeatureParam<subresource_filter::mojom::ActivationLevel>::Option
    kActivationLevelOptions[] = {
        {subresource_filter::mojom::ActivationLevel::kDisabled, "disabled"},
        {subresource_filter::mojom::ActivationLevel::kDryRun, "dry_run"},
        {subresource_filter::mojom::ActivationLevel::kEnabled, "enabled"}};

const base::FeatureParam<subresource_filter::mojom::ActivationLevel>
    kActivationLevel{&kEnableFingerprintingProtectionFilter, "activation_level",
                     subresource_filter::mojom::ActivationLevel::kEnabled,
                     &kActivationLevelOptions};

const base::FeatureParam<bool> kEnableOn3pcBlocked{
    &kEnableFingerprintingProtectionFilter, "enable_on_3pc_blocked", false};

}  // namespace fingerprinting_protection_filter::features
