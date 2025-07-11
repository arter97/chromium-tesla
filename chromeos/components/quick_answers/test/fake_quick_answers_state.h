// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMEOS_COMPONENTS_QUICK_ANSWERS_TEST_FAKE_QUICK_ANSWERS_STATE_H_
#define CHROMEOS_COMPONENTS_QUICK_ANSWERS_TEST_FAKE_QUICK_ANSWERS_STATE_H_

#include <memory>

#include "chromeos/components/quick_answers/public/cpp/quick_answers_state.h"

class FakeQuickAnswersState : public QuickAnswersState {
 public:
  FakeQuickAnswersState();

  FakeQuickAnswersState(const FakeQuickAnswersState&) = delete;
  FakeQuickAnswersState& operator=(const FakeQuickAnswersState&) = delete;

  ~FakeQuickAnswersState() override;

  void SetSettingsEnabled(bool enabled);
  void SetApplicationLocale(const std::string& locale);
  void SetPreferredLanguages(const std::string& preferred_languages);
  void OnPrefsInitialized();

 protected:
  void AsyncWriteConsentUiImpressionCount(int32_t count) override;
  void AsyncWriteConsentStatus(
      quick_answers::prefs::ConsentStatus consent_status) override;
  void AsyncWriteEnabled(bool enabled) override;
};

#endif  // CHROMEOS_COMPONENTS_QUICK_ANSWERS_TEST_FAKE_QUICK_ANSWERS_STATE_H_
