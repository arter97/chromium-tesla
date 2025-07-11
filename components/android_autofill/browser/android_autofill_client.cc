// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/android_autofill/browser/android_autofill_client.h"

#include <utility>

#include "base/check_op.h"
#include "base/feature_list.h"
#include "base/functional/function_ref.h"
#include "base/notimplemented.h"
#include "base/notreached.h"
#include "base/types/cxx23_to_underlying.h"
#include "components/android_autofill/browser/android_autofill_manager.h"
#include "components/android_autofill/browser/android_autofill_provider.h"
#include "components/autofill/content/browser/content_autofill_client.h"
#include "components/autofill/core/browser/crowdsourcing/autofill_crowdsourcing_manager.h"
#include "components/autofill/core/browser/payments/legal_message_line.h"
#include "components/autofill/core/browser/ui/autofill_suggestion_delegate.h"
#include "components/autofill/core/browser/ui/suggestion.h"
#include "components/autofill/core/browser/ui/suggestion_type.h"
#include "components/autofill/core/browser/webdata/autofill_webdata_service.h"
#include "components/autofill/core/common/aliases.h"
#include "components/autofill/core/common/autofill_features.h"
#include "components/prefs/pref_service.h"
#include "components/user_prefs/user_prefs.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/ssl_status.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/web_contents.h"
#include "ui/android/view_android.h"
#include "ui/gfx/geometry/rect_f.h"

namespace android_autofill {

void AndroidAutofillClient::CreateForWebContents(
    content::WebContents* contents) {
  DCHECK(contents);
  if (!FromWebContents(contents)) {
    contents->SetUserData(
        UserDataKey(), base::WrapUnique(new AndroidAutofillClient(contents)));
  }
}

AndroidAutofillClient::AndroidAutofillClient(content::WebContents* web_contents)
    : autofill::ContentAutofillClient(web_contents) {}

AndroidAutofillClient::~AndroidAutofillClient() {
  HideAutofillSuggestions(autofill::SuggestionHidingReason::kTabGone);
}

bool AndroidAutofillClient::IsOffTheRecord() const {
  auto* mutable_this = const_cast<AndroidAutofillClient*>(this);
  return mutable_this->GetWebContents().GetBrowserContext()->IsOffTheRecord();
}

scoped_refptr<network::SharedURLLoaderFactory>
AndroidAutofillClient::GetURLLoaderFactory() {
  return GetWebContents()
      .GetBrowserContext()
      ->GetDefaultStoragePartition()
      ->GetURLLoaderFactoryForBrowserProcess();
}

autofill::AutofillCrowdsourcingManager*
AndroidAutofillClient::GetCrowdsourcingManager() {
  if (autofill::AutofillProvider::
          is_crowdsourcing_manager_disabled_for_testing()) {
    return nullptr;
  }
  if (!crowdsourcing_manager_) {
    // Lazy initialization to avoid virtual function calls in the constructor.
    crowdsourcing_manager_ =
        std::make_unique<autofill::AutofillCrowdsourcingManager>(
            this, GetChannel(), GetLogManager());
  }
  return crowdsourcing_manager_.get();
}

autofill::PersonalDataManager* AndroidAutofillClient::GetPersonalDataManager() {
  return nullptr;
}

autofill::AutocompleteHistoryManager*
AndroidAutofillClient::GetAutocompleteHistoryManager() {
  NOTREACHED_NORETURN();
}

PrefService* AndroidAutofillClient::GetPrefs() {
  return const_cast<PrefService*>(std::as_const(*this).GetPrefs());
}

const PrefService* AndroidAutofillClient::GetPrefs() const {
  return user_prefs::UserPrefs::Get(GetWebContents().GetBrowserContext());
}

syncer::SyncService* AndroidAutofillClient::GetSyncService() {
  // TODO(b/321949351): Move this and other stubs into AutofillClient.
  return nullptr;
}

signin::IdentityManager* AndroidAutofillClient::GetIdentityManager() {
  return nullptr;
}

autofill::FormDataImporter* AndroidAutofillClient::GetFormDataImporter() {
  return nullptr;
}

autofill::StrikeDatabase* AndroidAutofillClient::GetStrikeDatabase() {
  return nullptr;
}

ukm::UkmRecorder* AndroidAutofillClient::GetUkmRecorder() {
  return nullptr;
}

ukm::SourceId AndroidAutofillClient::GetUkmSourceId() {
  // TODO(b/321677608): Consider UKM recording via delegate (non-WebView only).
  return ukm::kInvalidSourceId;
}

autofill::AddressNormalizer* AndroidAutofillClient::GetAddressNormalizer() {
  return nullptr;
}

const GURL& AndroidAutofillClient::GetLastCommittedPrimaryMainFrameURL() const {
  return GetWebContents().GetPrimaryMainFrame()->GetLastCommittedURL();
}

url::Origin AndroidAutofillClient::GetLastCommittedPrimaryMainFrameOrigin()
    const {
  return GetWebContents().GetPrimaryMainFrame()->GetLastCommittedOrigin();
}

security_state::SecurityLevel
AndroidAutofillClient::GetSecurityLevelForUmaHistograms() {
  // TODO(b/321677908): Consider recording for non-webview.
  // Return the count value which will not be recorded.
  return security_state::SecurityLevel::SECURITY_LEVEL_COUNT;
}

const translate::LanguageState* AndroidAutofillClient::GetLanguageState() {
  return nullptr;
}

translate::TranslateDriver* AndroidAutofillClient::GetTranslateDriver() {
  return nullptr;
}

void AndroidAutofillClient::ShowAutofillSettings(
    autofill::SuggestionType suggestion_type) {
  NOTIMPLEMENTED();
}

void AndroidAutofillClient::ShowEditAddressProfileDialog(
    const autofill::AutofillProfile& profile,
    AddressProfileSavePromptCallback on_user_decision_callback) {
  NOTREACHED_IN_MIGRATION();
}

void AndroidAutofillClient::ShowDeleteAddressProfileDialog(
    const autofill::AutofillProfile& profile,
    AddressProfileDeleteDialogCallback delete_dialog_callback) {
  NOTREACHED_IN_MIGRATION();
}

void AndroidAutofillClient::ConfirmSaveAddressProfile(
    const autofill::AutofillProfile& profile,
    const autofill::AutofillProfile* original_profile,
    SaveAddressProfilePromptOptions options,
    AddressProfileSavePromptCallback callback) {
  NOTIMPLEMENTED();
}

bool AndroidAutofillClient::HasCreditCardScanFeature() const {
  return false;
}

void AndroidAutofillClient::ScanCreditCard(CreditCardScanCallback callback) {
  NOTIMPLEMENTED();
}

bool AndroidAutofillClient::ShowTouchToFillCreditCard(
    base::WeakPtr<autofill::TouchToFillDelegate> delegate,
    base::span<const autofill::CreditCard> cards_to_suggest,
    const std::vector<bool>& card_acceptabilities) {
  return false;
}

void AndroidAutofillClient::HideTouchToFillCreditCard() {}

void AndroidAutofillClient::ShowAutofillSuggestions(
    const autofill::AutofillClient::PopupOpenArgs& open_args,
    base::WeakPtr<autofill::AutofillSuggestionDelegate> delegate) {
  NOTIMPLEMENTED();
}

void AndroidAutofillClient::UpdateAutofillDataListValues(
    base::span<const autofill::SelectOption> datalist) {
  // Leaving as an empty method since updating autofill popup window
  // dynamically does not seem to be a useful feature when delegating to Android
  // APIs.
}

void AndroidAutofillClient::PinAutofillSuggestions() {
  NOTIMPLEMENTED();
}

void AndroidAutofillClient::UpdatePopup(
    const std::vector<autofill::Suggestion>& suggestions,
    autofill::FillingProduct main_filling_product,
    autofill::AutofillSuggestionTriggerSource trigger_source) {
  NOTIMPLEMENTED();
}

void AndroidAutofillClient::HideAutofillSuggestions(
    autofill::SuggestionHidingReason reason) {
  // TODO(321950502): Analyze hiding the datalist popup here.
}

bool AndroidAutofillClient::IsAutocompleteEnabled() const {
  return false;
}

bool AndroidAutofillClient::IsPasswordManagerEnabled() {
  // Android 3P mode and WebView rely on the AndroidAutofillManager which
  // doesn't call this function. If it ever does, the function needs to
  // be implemented in a meaningful way.
  NOTREACHED_NORETURN();
}

void AndroidAutofillClient::DidFillOrPreviewForm(
    autofill::mojom::ActionPersistence action_persistence,
    autofill::AutofillTriggerSource trigger_source,
    bool is_refill) {}

void AndroidAutofillClient::DidFillOrPreviewField(
    const std::u16string& autofilled_value,
    const std::u16string& profile_full_name) {}

bool AndroidAutofillClient::IsContextSecure() const {
  content::SSLStatus ssl_status;
  content::NavigationEntry* navigation_entry =
      GetWebContents().GetController().GetLastCommittedEntry();
  if (!navigation_entry) {
    return false;
  }

  ssl_status = navigation_entry->GetSSL();
  // Note: As of crbug.com/701018, Chrome relies on SecurityStateTabHelper to
  // determine whether the page is secure, but WebView has no equivalent class.
  // TODO(b/321679324): Consider injecting SecurityStateTabHelper for 3P chrome.

  return navigation_entry->GetURL().SchemeIsCryptographic() &&
         ssl_status.certificate &&
         !net::IsCertStatusError(ssl_status.cert_status) &&
         !(ssl_status.content_status &
           content::SSLStatus::RAN_INSECURE_CONTENT);
}

autofill::FormInteractionsFlowId
AndroidAutofillClient::GetCurrentFormInteractionsFlowId() {
  // Currently not in use here. See `ChromeAutofillClient` for a proper
  // implementation.
  return {};
}

content::WebContents& AndroidAutofillClient::GetWebContents() const {
  // While a const_cast is not ideal. The Autofill API uses const in various
  // spots and the content public API doesn't have const accessors. So the const
  // cast is the lesser of two evils.
  return const_cast<content::WebContents&>(
      ContentAutofillClient::GetWebContents());
}

std::unique_ptr<autofill::AutofillManager> AndroidAutofillClient::CreateManager(
    base::PassKey<autofill::ContentAutofillDriver> pass_key,
    autofill::ContentAutofillDriver& driver) {
  return base::WrapUnique(new autofill::AndroidAutofillManager(&driver));
}

}  // namespace android_autofill
