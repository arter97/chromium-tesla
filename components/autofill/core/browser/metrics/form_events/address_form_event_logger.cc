// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/autofill/core/browser/metrics/form_events/address_form_event_logger.h"

#include <algorithm>
#include <iterator>
#include <memory>

#include "base/metrics/histogram_functions.h"
#include "base/metrics/user_metrics.h"
#include "base/metrics/user_metrics_action.h"
#include "components/autofill/core/browser/autofill_data_util.h"
#include "components/autofill/core/browser/autofill_trigger_details.h"
#include "components/autofill/core/browser/logging/log_manager.h"
#include "components/autofill/core/browser/metrics/autofill_metrics_utils.h"
#include "components/autofill/core/common/autofill_features.h"
#include "components/autofill/core/common/autofill_internals/log_message.h"
#include "components/autofill/core/common/autofill_internals/logging_scope.h"

namespace autofill::autofill_metrics {

AddressFormEventLogger::AddressFormEventLogger(
    bool is_in_any_main_frame,
    AutofillMetrics::FormInteractionsUkmLogger* form_interactions_ukm_logger,
    AutofillClient* client)
    : FormEventLoggerBase("Address",
                          is_in_any_main_frame,
                          form_interactions_ukm_logger,
                          client) {}

AddressFormEventLogger::~AddressFormEventLogger() = default;

void AddressFormEventLogger::OnDidFillFormFillingSuggestion(
    const AutofillProfile& profile,
    const FormStructure& form,
    const AutofillField& field,
    AutofillMetrics::PaymentsSigninState signin_state_for_metrics,
    const AutofillTriggerSource trigger_source) {
  signin_state_for_metrics_ = signin_state_for_metrics;

  form_interactions_ukm_logger_->LogDidFillSuggestion(form, field);

  Log(FORM_EVENT_LOCAL_SUGGESTION_FILLED, form);

  if (!has_logged_form_filling_suggestion_filled_) {
    has_logged_form_filling_suggestion_filled_ = true;
    Log(FORM_EVENT_LOCAL_SUGGESTION_FILLED_ONCE, form);
  }

  base::RecordAction(
      base::UserMetricsAction("Autofill_FilledProfileSuggestion"));

  if (trigger_source != AutofillTriggerSource::kFastCheckout) {
    ++form_interaction_counts_.autofill_fills;
  }
  UpdateFlowId();

  profile_categories_filled_.insert(GetCategoryOfProfile(profile));
}

void AddressFormEventLogger::OnDidUndoAutofill() {
  has_logged_undo_after_fill_ = true;
  base::RecordAction(base::UserMetricsAction("Autofill_UndoAddressAutofill"));
}

void AddressFormEventLogger::OnLog(const std::string& name,
                                   FormEvent event,
                                   const FormStructure& form) const {
  uint32_t groups = data_util::DetermineGroups(form);
  base::UmaHistogramEnumeration(
      name + data_util::GetSuffixForProfileFormType(groups), event,
      NUM_FORM_EVENTS);
  if (data_util::ContainsAddress(groups) &&
      (data_util::ContainsPhone(groups) || data_util::ContainsEmail(groups))) {
    base::UmaHistogramEnumeration(name + ".AddressPlusContact", event,
                                  NUM_FORM_EVENTS);
  }
}

void AddressFormEventLogger::RecordPollSuggestions() {
  base::RecordAction(
      base::UserMetricsAction("Autofill_PolledProfileSuggestions"));
}

void AddressFormEventLogger::RecordParseForm() {
  base::RecordAction(base::UserMetricsAction("Autofill_ParsedProfileForm"));
}

void AddressFormEventLogger::RecordShowSuggestions() {
  base::RecordAction(
      base::UserMetricsAction("Autofill_ShowedProfileSuggestions"));
}

void AddressFormEventLogger::RecordFillingAssistance(LogBuffer& logs) const {
  FormEventLoggerBase::RecordFillingAssistance(logs);
  // Log the origin-resolved filling assistance metric by converting the
  // `profile_categories_filled` to an CategoryResolvedFillingAssistanceBucket.
  auto filled_categories_to_bucket = [&] {
    if (profile_categories_filled_.empty()) {
      return CategoryResolvedFillingAssistanceBucket::kNone;
    }
    if (profile_categories_filled_.size() > 1) {
      return CategoryResolvedFillingAssistanceBucket::kMixed;
    }
    switch (*profile_categories_filled_.begin()) {
      case AutofillProfileSourceCategory::kLocalOrSyncable:
        return CategoryResolvedFillingAssistanceBucket::kLocalOrSyncable;
      case AutofillProfileSourceCategory::kAccountChrome:
        return CategoryResolvedFillingAssistanceBucket::kAccountChrome;
      case AutofillProfileSourceCategory::kAccountNonChrome:
        return CategoryResolvedFillingAssistanceBucket::kAccountNonChrome;
    }
  };
  base::UmaHistogramEnumeration("Autofill.Leipzig.FillingAssistanceCategory",
                                filled_categories_to_bucket());
}

void AddressFormEventLogger::RecordFillingCorrectness(LogBuffer& logs) const {
  FormEventLoggerBase::RecordFillingCorrectness(logs);
  // Non-empty because correctness is only logged when an Autofill
  // suggestion was accepted.
  DCHECK(!profile_categories_filled_.empty());
  const std::string kBucket =
      profile_categories_filled_.size() == 1
          ? GetProfileCategorySuffix(*profile_categories_filled_.begin())
          : "Mixed";
  base::UmaHistogramBoolean("Autofill.Leipzig.FillingCorrectness." + kBucket,
                            !has_logged_edited_autofilled_field_);
}

void AddressFormEventLogger::LogUkmInteractedWithForm(
    FormSignature form_signature) {
  // Address Autofill has deprecated the concept of server addresses.
  form_interactions_ukm_logger_->LogInteractedWithForm(
      /*is_for_credit_card=*/false, record_type_count_,
      /*server_record_type_count=*/0, form_signature);
}

bool AddressFormEventLogger::HasLoggedDataToFillAvailable() const {
  return record_type_count_ > 0;
}

DenseSet<FormTypeNameForLogging>
AddressFormEventLogger::GetSupportedFormTypeNamesForLogging() const {
  return {FormTypeNameForLogging::kAddressForm};
}

DenseSet<FormTypeNameForLogging> AddressFormEventLogger::GetFormTypesForLogging(
    const FormStructure& form) const {
  DenseSet<FormTypeNameForLogging> form_types;
  for (FormType form_type : form.GetFormTypes()) {
    switch (form_type) {
      case FormType::kAddressForm:
        form_types.insert(FormTypeNameForLogging::kAddressForm);
        // TODO(crbug.com/339657029): add support for kEmailOnlyForm and
        // kPostalAddressForm.
        break;
      case FormType::kCreditCardForm:
      case FormType::kStandaloneCvcForm:
      case FormType::kPasswordForm:
      case FormType::kUnknownFormType:
        break;
    }
  }
  return form_types;
}

}  // namespace autofill::autofill_metrics
