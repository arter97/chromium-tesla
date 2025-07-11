// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/messages/android/messages_feature.h"

#include "base/android/feature_map.h"
#include "base/feature_list.h"
#include "base/metrics/field_trial_params.h"
#include "base/no_destructor.h"

// Must come after all headers that specialize FromJniType() / ToJniType().
#include "components/messages/android/feature_flags_jni_headers/MessageFeatureMap_jni.h"

namespace messages {

namespace {

const base::Feature* kFeaturesExposedToJava[] = {
    &kMessagesForAndroidStackingAnimation,
    &kMessagesForAndroidFullyVisibleCallback,
    &kMessagesAndroidExtraHistograms,
};

// static
base::android::FeatureMap* GetFeatureMap() {
  static base::NoDestructor<base::android::FeatureMap> kFeatureMap(std::vector(
      std::begin(kFeaturesExposedToJava), std::end(kFeaturesExposedToJava)));
  return kFeatureMap.get();
}

}  // namespace

BASE_FEATURE(kMessagesForAndroidAdsBlocked,
             "MessagesForAndroidAdsBlocked",
             base::FEATURE_ENABLED_BY_DEFAULT);

BASE_FEATURE(kMessagesForAndroidPopupBlocked,
             "MessagesForAndroidPopupBlocked",
             base::FEATURE_ENABLED_BY_DEFAULT);

BASE_FEATURE(kMessagesForAndroidSaveCard,
             "MessagesForAndroidSaveCard",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kMessagesForAndroidStackingAnimation,
             "MessagesForAndroidStackingAnimation",
             base::FEATURE_ENABLED_BY_DEFAULT);

BASE_FEATURE(kMessagesForAndroidFullyVisibleCallback,
             "MessagesForAndroidFullyVisibleCallback",
             base::FEATURE_ENABLED_BY_DEFAULT);

// Feature that enables extra histogram recordings.
BASE_FEATURE(kMessagesAndroidExtraHistograms,
             "MessagesAndroidExtraHistograms",
             base::FEATURE_ENABLED_BY_DEFAULT);

bool IsAdsBlockedMessagesUiEnabled() {
  return base::FeatureList::IsEnabled(kMessagesForAndroidAdsBlocked);
}

bool IsPopupBlockedMessagesUiEnabled() {
  return base::FeatureList::IsEnabled(kMessagesForAndroidPopupBlocked);
}

bool IsSaveCardMessagesUiEnabled() {
  return base::FeatureList::IsEnabled(kMessagesForAndroidSaveCard);
}

bool IsStackingAnimationEnabled() {
  return base::FeatureList::IsEnabled(kMessagesForAndroidStackingAnimation);
}

bool ISdFullyVisibleCallbackEnabled() {
  return base::FeatureList::IsEnabled(kMessagesForAndroidFullyVisibleCallback);
}

static jlong JNI_MessageFeatureMap_GetNativeMap(JNIEnv* env) {
  return reinterpret_cast<jlong>(GetFeatureMap());
}

}  // namespace messages
