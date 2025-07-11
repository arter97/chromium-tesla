// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/autofill/core/browser/merchant_promo_code_manager.h"

#include "base/functional/callback_helpers.h"
#include "base/test/metrics/histogram_tester.h"
#include "base/test/mock_callback.h"
#include "base/test/scoped_feature_list.h"
#include "base/test/task_environment.h"
#include "components/autofill/core/browser/autofill_test_utils.h"
#include "components/autofill/core/browser/form_structure.h"
#include "components/autofill/core/browser/metrics/payments/offers_metrics.h"
#include "components/autofill/core/browser/payments_data_manager_test_api.h"
#include "components/autofill/core/browser/suggestions_context.h"
#include "components/autofill/core/browser/test_autofill_client.h"
#include "components/autofill/core/browser/test_personal_data_manager.h"
#include "components/autofill/core/common/autofill_features.h"
#include "components/autofill/core/common/autofill_test_utils.h"
#include "components/autofill/core/common/form_data.h"
#include "components/strings/grit/components_strings.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/l10n/l10n_util.h"

namespace autofill {

namespace {

using MockSuggestionsReturnedCallback =
    base::MockCallback<SingleFieldFormFiller::OnSuggestionsReturnedCallback>;
using test::CreateTestFormField;
using ::testing::_;
using ::testing::Field;
using ::testing::Truly;
using ::testing::UnorderedElementsAre;

}  // namespace

class MerchantPromoCodeManagerTest : public testing::Test {
 protected:
  MerchantPromoCodeManagerTest() {
    personal_data_manager_ = std::make_unique<TestPersonalDataManager>();
    merchant_promo_code_manager_ = std::make_unique<MerchantPromoCodeManager>();
    merchant_promo_code_manager_->Init(personal_data_manager_.get(),
                                       /*is_off_the_record=*/false);
    test_field_ =
        CreateTestFormField(/*label=*/"", "Some Field Name", "SomePrefix",
                            FormControlType::kInputText);
  }

  // Sets up the TestPersonalDataManager with a promo code offer for the given
  // |origin|, and sets the offer details url of the offer to
  // |offer_details_url|. Returns the promo code inserted in case the test wants
  // to match it against returned suggestions.
  std::string SetUpPromoCodeOffer(std::string origin,
                                  const GURL& offer_details_url) {
    personal_data_manager_->test_payments_data_manager()
        .SetAutofillWalletImportEnabled(true);
    personal_data_manager_->test_payments_data_manager()
        .SetAutofillPaymentMethodsEnabled(true);
    AutofillOfferData testPromoCodeOfferData =
        test::GetPromoCodeOfferData(GURL(origin));
    testPromoCodeOfferData.SetOfferDetailsUrl(offer_details_url);
    test_api(personal_data_manager_->payments_data_manager())
        .AddOfferData(
            std::make_unique<AutofillOfferData>(testPromoCodeOfferData));
    return testPromoCodeOfferData.GetPromoCode();
  }

  // Adds a promo code focused field to the suggestions context.
  void AddPromoCodeFocusedFieldToSuggestionsContext(SuggestionsContext* out) {
    autofill_field_.set_server_predictions(
        {::autofill::test::CreateFieldPrediction(MERCHANT_PROMO_CODE)});
    out->focused_field = &autofill_field_;
  }

  base::test::TaskEnvironment task_environment_;
  test::AutofillUnitTestEnvironment autofill_test_environment_;
  TestAutofillClient autofill_client_;
  std::unique_ptr<TestPersonalDataManager> personal_data_manager_;
  std::unique_ptr<MerchantPromoCodeManager> merchant_promo_code_manager_;
  FormFieldData test_field_;
  AutofillField autofill_field_;
};

TEST_F(MerchantPromoCodeManagerTest, ShowsPromoCodeSuggestions) {
  base::HistogramTester histogram_tester;
  std::string last_committed_origin_url = "https://www.example.com";
  FormData form_data;
  form_data.set_main_frame_origin(
      url::Origin::Create(GURL(last_committed_origin_url)));
  FormStructure form_structure{form_data};
  SuggestionsContext context;
  context.form_structure = &form_structure;
  AddPromoCodeFocusedFieldToSuggestionsContext(&context);
  std::string promo_code = SetUpPromoCodeOffer(
      last_committed_origin_url, GURL("https://offer-details-url.com/"));
  Suggestion promo_code_suggestion = Suggestion(base::ASCIIToUTF16(promo_code));
  Suggestion footer_suggestion = Suggestion(l10n_util::GetStringUTF16(
      IDS_AUTOFILL_PROMO_CODE_SUGGESTIONS_FOOTER_TEXT));

  // Setting up mock to verify that the handler is returned a list of
  // promo-code-based suggestions and the promo code details line.
  MockSuggestionsReturnedCallback mock_callback;
  EXPECT_CALL(
      mock_callback,
      Run(_, UnorderedElementsAre(
                 Field(&Suggestion::main_text, promo_code_suggestion.main_text),
                 Field(&Suggestion::type, SuggestionType::kSeparator),
                 Field(&Suggestion::main_text, footer_suggestion.main_text))))
      .Times(3);

  // Simulate request for suggestions.
  // Because all criteria are met, active promo code suggestions for the given
  // merchant site will be displayed instead of requesting Autocomplete
  // suggestions.
  EXPECT_TRUE(merchant_promo_code_manager_->OnGetSingleFieldSuggestions(
      test_field_, autofill_client_, mock_callback.Get(),
      /*context=*/context));

  // Trigger offers suggestions popup again to be able to test that we do not
  // log metrics twice for the same field.
  EXPECT_TRUE(merchant_promo_code_manager_->OnGetSingleFieldSuggestions(
      test_field_, autofill_client_, mock_callback.Get(),
      /*context=*/context));

  // Trigger offers suggestions popup again to be able to test that we log
  // metrics more than once if it is a different field.
  FormFieldData other_field =
      CreateTestFormField(/*label=*/"", "Some Other Name", "SomePrefix",
                          FormControlType::kInputTelephone);
  EXPECT_TRUE(merchant_promo_code_manager_->OnGetSingleFieldSuggestions(
      other_field, autofill_client_, mock_callback.Get(),
      /*context=*/context));

  histogram_tester.ExpectBucketCount(
      "Autofill.Offer.SuggestionsPopupShown2",
      autofill_metrics::OffersSuggestionsPopupEvent::
          kOffersSuggestionsPopupShownOnce,
      2);
  histogram_tester.ExpectBucketCount(
      "Autofill.Offer.SuggestionsPopupShown2",
      autofill_metrics::OffersSuggestionsPopupEvent::
          kOffersSuggestionsPopupShown,
      3);
  histogram_tester.ExpectBucketCount(
      "Autofill.Offer.Suggestion2.GPayPromoCodeOffer",
      autofill_metrics::OffersSuggestionsEvent::kOfferSuggestionShownOnce, 2);
  histogram_tester.ExpectBucketCount(
      "Autofill.Offer.Suggestion2.GPayPromoCodeOffer",
      autofill_metrics::OffersSuggestionsEvent::kOfferSuggestionShown, 3);
}

TEST_F(MerchantPromoCodeManagerTest,
       DoesNotShowPromoCodeOffersIfFieldIsNotAPromoCodeField) {
  base::HistogramTester histogram_tester;
  // Setting up mock to verify that suggestions returning is not triggered if
  // the field is not a promo code field.
  MockSuggestionsReturnedCallback mock_callback;
  EXPECT_CALL(mock_callback, Run).Times(0);

  // Simulate request for suggestions.
  EXPECT_FALSE(merchant_promo_code_manager_->OnGetSingleFieldSuggestions(
      test_field_, autofill_client_, mock_callback.Get(),
      /*context=*/SuggestionsContext()));

  // Ensure that no metrics were logged.
  histogram_tester.ExpectBucketCount(
      "Autofill.Offer.SuggestionsPopupShown2",
      autofill_metrics::OffersSuggestionsPopupEvent::
          kOffersSuggestionsPopupShownOnce,
      0);
  histogram_tester.ExpectBucketCount(
      "Autofill.Offer.SuggestionsPopupShown2",
      autofill_metrics::OffersSuggestionsPopupEvent::
          kOffersSuggestionsPopupShown,
      0);
  histogram_tester.ExpectBucketCount(
      "Autofill.Offer.Suggestion2.GPayPromoCodeOffer",
      autofill_metrics::OffersSuggestionsEvent::kOfferSuggestionShownOnce, 0);
  histogram_tester.ExpectBucketCount(
      "Autofill.Offer.Suggestion2.GPayPromoCodeOffer",
      autofill_metrics::OffersSuggestionsEvent::kOfferSuggestionShown, 0);
}

TEST_F(MerchantPromoCodeManagerTest,
       DoesNotShowPromoCodeOffersForOffTheRecord) {
  base::HistogramTester histogram_tester;
  std::string last_committed_origin_url = "https://www.example.com";
  std::string promo_code = SetUpPromoCodeOffer(
      last_committed_origin_url, GURL("https://offer-details-url.com/"));
  FormData form_data;
  form_data.set_main_frame_origin(
      url::Origin::Create(GURL(last_committed_origin_url)));
  FormStructure form_structure{form_data};
  SuggestionsContext context;
  context.form_structure = &form_structure;
  AddPromoCodeFocusedFieldToSuggestionsContext(&context);
  merchant_promo_code_manager_->is_off_the_record_ = true;

  // Setting up mock to verify that suggestions returning is not triggered if
  // the user is off the record.
  MockSuggestionsReturnedCallback mock_callback;
  EXPECT_CALL(mock_callback, Run).Times(0);

  // Simulate request for suggestions.
  EXPECT_FALSE(merchant_promo_code_manager_->OnGetSingleFieldSuggestions(
      test_field_, autofill_client_, mock_callback.Get(),
      /*context=*/context));

  // Ensure that no metrics were logged.
  histogram_tester.ExpectBucketCount(
      "Autofill.Offer.SuggestionsPopupShown2",
      autofill_metrics::OffersSuggestionsPopupEvent::
          kOffersSuggestionsPopupShownOnce,
      0);
  histogram_tester.ExpectBucketCount(
      "Autofill.Offer.SuggestionsPopupShown2",
      autofill_metrics::OffersSuggestionsPopupEvent::
          kOffersSuggestionsPopupShown,
      0);
  histogram_tester.ExpectBucketCount(
      "Autofill.Offer.Suggestion2.GPayPromoCodeOffer",
      autofill_metrics::OffersSuggestionsEvent::kOfferSuggestionShownOnce, 0);
  histogram_tester.ExpectBucketCount(
      "Autofill.Offer.Suggestion2.GPayPromoCodeOffer",
      autofill_metrics::OffersSuggestionsEvent::kOfferSuggestionShown, 0);
}

TEST_F(MerchantPromoCodeManagerTest,
       DoesNotShowPromoCodeOffersIfPersonalDataManagerDoesNotExist) {
  base::HistogramTester histogram_tester;
  std::string last_committed_origin_url = "https://www.example.com";
  FormData form_data;
  form_data.set_main_frame_origin(
      url::Origin::Create(GURL(last_committed_origin_url)));
  FormStructure form_structure{form_data};
  SuggestionsContext context;
  context.form_structure = &form_structure;
  AddPromoCodeFocusedFieldToSuggestionsContext(&context);
  merchant_promo_code_manager_->personal_data_manager_ = nullptr;

  // Setting up mock to verify that suggestions returning is not triggered if
  // personal data manager does not exist.
  MockSuggestionsReturnedCallback mock_callback;
  EXPECT_CALL(mock_callback, Run).Times(0);

  // Simulate request for suggestions.
  EXPECT_FALSE(merchant_promo_code_manager_->OnGetSingleFieldSuggestions(
      test_field_, autofill_client_, mock_callback.Get(),
      /*context=*/context));

  // Ensure that no metrics were logged.
  histogram_tester.ExpectBucketCount(
      "Autofill.Offer.SuggestionsPopupShown2",
      autofill_metrics::OffersSuggestionsPopupEvent::
          kOffersSuggestionsPopupShownOnce,
      0);
  histogram_tester.ExpectBucketCount(
      "Autofill.Offer.SuggestionsPopupShown2",
      autofill_metrics::OffersSuggestionsPopupEvent::
          kOffersSuggestionsPopupShown,
      0);
  histogram_tester.ExpectBucketCount(
      "Autofill.Offer.Suggestion2.GPayPromoCodeOffer",
      autofill_metrics::OffersSuggestionsEvent::kOfferSuggestionShownOnce, 0);
  histogram_tester.ExpectBucketCount(
      "Autofill.Offer.Suggestion2.GPayPromoCodeOffer",
      autofill_metrics::OffersSuggestionsEvent::kOfferSuggestionShown, 0);
}

TEST_F(MerchantPromoCodeManagerTest, NoPromoCodeOffers) {
  base::HistogramTester histogram_tester;
  std::string last_committed_origin_url = "https://www.example.com";
  personal_data_manager_->test_payments_data_manager()
      .SetAutofillWalletImportEnabled(true);
  personal_data_manager_->test_payments_data_manager()
      .SetAutofillPaymentMethodsEnabled(true);
  FormData form_data;
  form_data.set_main_frame_origin(
      url::Origin::Create(GURL(last_committed_origin_url)));
  FormStructure form_structure{form_data};
  SuggestionsContext context;
  context.form_structure = &form_structure;
  AddPromoCodeFocusedFieldToSuggestionsContext(&context);

  // Setting up mock to verify that suggestions returning is not triggered if
  // there are no promo code offers to suggest.
  MockSuggestionsReturnedCallback mock_callback;
  EXPECT_CALL(mock_callback, Run).Times(0);

  // Simulate request for suggestions.
  EXPECT_FALSE(merchant_promo_code_manager_->OnGetSingleFieldSuggestions(
      test_field_, autofill_client_, mock_callback.Get(),
      /*context=*/context));

  // Ensure that no metrics were logged.
  histogram_tester.ExpectBucketCount(
      "Autofill.Offer.SuggestionsPopupShown2",
      autofill_metrics::OffersSuggestionsPopupEvent::
          kOffersSuggestionsPopupShownOnce,
      0);
  histogram_tester.ExpectBucketCount(
      "Autofill.Offer.SuggestionsPopupShown2",
      autofill_metrics::OffersSuggestionsPopupEvent::
          kOffersSuggestionsPopupShown,
      0);
  histogram_tester.ExpectBucketCount(
      "Autofill.Offer.Suggestion2.GPayPromoCodeOffer",
      autofill_metrics::OffersSuggestionsEvent::kOfferSuggestionShownOnce, 0);
  histogram_tester.ExpectBucketCount(
      "Autofill.Offer.Suggestion2.GPayPromoCodeOffer",
      autofill_metrics::OffersSuggestionsEvent::kOfferSuggestionShown, 0);
}

// This test case exists to ensure that disabling autofill wallet import (by
// turning off the "Payment methods, offers, and addresses using Google Pay"
// toggle) disables offering suggestions and autofilling for promo codes.
TEST_F(MerchantPromoCodeManagerTest, AutofillWalletImportDisabled) {
  base::HistogramTester histogram_tester;
  std::string last_committed_origin_url = "https://www.example.com";
  FormData form_data;
  form_data.set_main_frame_origin(
      url::Origin::Create(GURL(last_committed_origin_url)));
  FormStructure form_structure{form_data};
  SuggestionsContext context;
  context.form_structure = &form_structure;
  AddPromoCodeFocusedFieldToSuggestionsContext(&context);
  SetUpPromoCodeOffer(last_committed_origin_url,
                      GURL("https://offer-details-url.com/"));
  personal_data_manager_->test_payments_data_manager()
      .SetAutofillWalletImportEnabled(false);

  // Autofill wallet import is disabled, so check that we do not return
  // suggestions to the handler.
  MockSuggestionsReturnedCallback mock_callback;
  EXPECT_CALL(mock_callback, Run).Times(0);

  // Simulate request for suggestions.
  EXPECT_FALSE(merchant_promo_code_manager_->OnGetSingleFieldSuggestions(
      test_field_, autofill_client_, mock_callback.Get(),
      /*context=*/context));

  // Ensure that no metrics were logged.
  histogram_tester.ExpectBucketCount(
      "Autofill.Offer.SuggestionsPopupShown2",
      autofill_metrics::OffersSuggestionsPopupEvent::
          kOffersSuggestionsPopupShownOnce,
      0);
  histogram_tester.ExpectBucketCount(
      "Autofill.Offer.SuggestionsPopupShown2",
      autofill_metrics::OffersSuggestionsPopupEvent::
          kOffersSuggestionsPopupShown,
      0);
  histogram_tester.ExpectBucketCount(
      "Autofill.Offer.Suggestion2.GPayPromoCodeOffer",
      autofill_metrics::OffersSuggestionsEvent::kOfferSuggestionShownOnce, 0);
  histogram_tester.ExpectBucketCount(
      "Autofill.Offer.Suggestion2.GPayPromoCodeOffer",
      autofill_metrics::OffersSuggestionsEvent::kOfferSuggestionShown, 0);
}

// This test case exists to ensure that disabling autofill credit card (by
// turning off the "Save and fill payment methods" toggle) disables offering
// suggestions and autofilling for promo codes.
TEST_F(MerchantPromoCodeManagerTest, AutofillCreditCardDisabled) {
  base::HistogramTester histogram_tester;
  std::string last_committed_origin_url = "https://www.example.com";
  FormData form_data;
  form_data.set_main_frame_origin(
      url::Origin::Create(GURL(last_committed_origin_url)));
  FormStructure form_structure{form_data};
  SuggestionsContext context;
  context.form_structure = &form_structure;
  AddPromoCodeFocusedFieldToSuggestionsContext(&context);
  SetUpPromoCodeOffer(last_committed_origin_url,
                      GURL("https://offer-details-url.com/"));
  personal_data_manager_->test_payments_data_manager()
      .SetAutofillPaymentMethodsEnabled(false);

  // Autofill credit card is disabled, so check that we do not return
  // suggestions to the handler.
  MockSuggestionsReturnedCallback mock_callback;
  EXPECT_CALL(mock_callback, Run).Times(0);

  // Simulate request for suggestions.
  EXPECT_FALSE(merchant_promo_code_manager_->OnGetSingleFieldSuggestions(
      test_field_, autofill_client_, mock_callback.Get(),
      /*context=*/context));

  // Ensure that no metrics were logged.
  histogram_tester.ExpectBucketCount(
      "Autofill.Offer.SuggestionsPopupShown2",
      autofill_metrics::OffersSuggestionsPopupEvent::
          kOffersSuggestionsPopupShownOnce,
      0);
  histogram_tester.ExpectBucketCount(
      "Autofill.Offer.SuggestionsPopupShown2",
      autofill_metrics::OffersSuggestionsPopupEvent::
          kOffersSuggestionsPopupShown,
      0);
  histogram_tester.ExpectBucketCount(
      "Autofill.Offer.Suggestion2.GPayPromoCodeOffer",
      autofill_metrics::OffersSuggestionsEvent::kOfferSuggestionShownOnce, 0);
  histogram_tester.ExpectBucketCount(
      "Autofill.Offer.Suggestion2.GPayPromoCodeOffer",
      autofill_metrics::OffersSuggestionsEvent::kOfferSuggestionShown, 0);
}

// This test case exists to ensure that we do not offer promo code offer
// suggestions if the field already contains a promo code.
TEST_F(MerchantPromoCodeManagerTest, PrefixMatched) {
  base::HistogramTester histogram_tester;
  std::string last_committed_origin_url = "https://www.example.com";
  FormData form_data;
  form_data.set_main_frame_origin(
      url::Origin::Create(GURL(last_committed_origin_url)));
  FormStructure form_structure{form_data};
  SuggestionsContext context;
  context.form_structure = &form_structure;
  AddPromoCodeFocusedFieldToSuggestionsContext(&context);
  test_field_.set_value(base::ASCIIToUTF16(SetUpPromoCodeOffer(
      last_committed_origin_url, GURL("https://offer-details-url.com/"))));

  // The field contains the promo code already, so check that we do not return
  // suggestions to the handler.
  MockSuggestionsReturnedCallback mock_callback;
  EXPECT_CALL(
      mock_callback,
      Run(_, testing::Truly(
                 [](const std::vector<Suggestion>& returned_suggestions) {
                   return returned_suggestions.empty();
                 })));

  // Simulate request for suggestions.
  EXPECT_TRUE(merchant_promo_code_manager_->OnGetSingleFieldSuggestions(
      test_field_, autofill_client_, mock_callback.Get(),
      /*context=*/context));

  // No metrics should be logged because no suggestions were shown.
  histogram_tester.ExpectBucketCount(
      "Autofill.Offer.SuggestionsPopupShown2",
      autofill_metrics::OffersSuggestionsPopupEvent::
          kOffersSuggestionsPopupShownOnce,
      0);
  histogram_tester.ExpectBucketCount(
      "Autofill.Offer.SuggestionsPopupShown2",
      autofill_metrics::OffersSuggestionsPopupEvent::
          kOffersSuggestionsPopupShown,
      0);
  histogram_tester.ExpectBucketCount(
      "Autofill.Offer.Suggestion2.GPayPromoCodeOffer",
      autofill_metrics::OffersSuggestionsEvent::kOfferSuggestionShownOnce, 0);
  histogram_tester.ExpectBucketCount(
      "Autofill.Offer.Suggestion2.GPayPromoCodeOffer",
      autofill_metrics::OffersSuggestionsEvent::kOfferSuggestionShown, 0);
}

TEST_F(MerchantPromoCodeManagerTest,
       OnSingleFieldSuggestion_GPayPromoCodeOfferSuggestion) {
  // Set up the test.
  base::HistogramTester histogram_tester;
  std::u16string test_promo_code = u"test_promo_code";
  std::string last_committed_origin_url = "https://www.example.com";
  FormData form_data;
  form_data.set_main_frame_origin(
      url::Origin::Create(GURL(last_committed_origin_url)));
  FormStructure form_structure{form_data};
  SuggestionsContext context;
  context.form_structure = &form_structure;
  AddPromoCodeFocusedFieldToSuggestionsContext(&context);
  SetUpPromoCodeOffer(last_committed_origin_url,
                      GURL("https://offer-details-url.com/"));

  // Check that non promo code popup item id's do not log as offer suggestion
  // selected.
  merchant_promo_code_manager_->OnSingleFieldSuggestionSelected(
      test_promo_code, SuggestionType::kAutocompleteEntry);
  histogram_tester.ExpectBucketCount(
      "Autofill.Offer.Suggestion2.GPayPromoCodeOffer",
      autofill_metrics::OffersSuggestionsEvent::kOfferSuggestionSelected, 0);

  // Simulate showing the promo code offers suggestions popup.
  EXPECT_TRUE(merchant_promo_code_manager_->OnGetSingleFieldSuggestions(
      test_field_, autofill_client_, base::DoNothing(),
      /*context=*/context));

  // Simulate selecting a promo code offer suggestion.
  merchant_promo_code_manager_->OnSingleFieldSuggestionSelected(
      test_promo_code, SuggestionType::kMerchantPromoCodeEntry);

  // Check that the histograms logged correctly.
  histogram_tester.ExpectBucketCount(
      "Autofill.Offer.Suggestion2.GPayPromoCodeOffer",
      autofill_metrics::OffersSuggestionsEvent::kOfferSuggestionSelected, 1);
  histogram_tester.ExpectBucketCount(
      "Autofill.Offer.Suggestion2.GPayPromoCodeOffer",
      autofill_metrics::OffersSuggestionsEvent::kOfferSuggestionSelectedOnce,
      1);

  // Simulate showing the promo code offers suggestions popup.
  EXPECT_TRUE(merchant_promo_code_manager_->OnGetSingleFieldSuggestions(
      test_field_, autofill_client_, base::DoNothing(),
      /*context=*/context));

  // Simulate selecting a promo code offer suggestion.
  merchant_promo_code_manager_->OnSingleFieldSuggestionSelected(
      test_promo_code, SuggestionType::kMerchantPromoCodeEntry);

  // Check that the histograms logged correctly.
  histogram_tester.ExpectBucketCount(
      "Autofill.Offer.Suggestion2.GPayPromoCodeOffer",
      autofill_metrics::OffersSuggestionsEvent::kOfferSuggestionSelected, 2);
  histogram_tester.ExpectBucketCount(
      "Autofill.Offer.Suggestion2.GPayPromoCodeOffer",
      autofill_metrics::OffersSuggestionsEvent::kOfferSuggestionSelectedOnce,
      1);
}

TEST_F(MerchantPromoCodeManagerTest,
       OnSingleFieldSuggestion_GPayPromoCodeOfferFooter) {
  // Set up the test.
  base::HistogramTester histogram_tester;
  std::u16string test_promo_code = u"test_promo_code";
  std::string last_committed_origin_url = "https://www.example.com";
  FormData form_data;
  form_data.set_main_frame_origin(
      url::Origin::Create(GURL(last_committed_origin_url)));
  FormStructure form_structure{form_data};
  SuggestionsContext context;
  context.form_structure = &form_structure;
  AddPromoCodeFocusedFieldToSuggestionsContext(&context);
  SetUpPromoCodeOffer(last_committed_origin_url,
                      GURL("https://offer-details-url.com/"));

  // Check that non promo code footer popup item id's do not log as offer
  // suggestions footer selected.
  merchant_promo_code_manager_->OnSingleFieldSuggestionSelected(
      test_promo_code, SuggestionType::kAutocompleteEntry);
  histogram_tester.ExpectBucketCount(
      "Autofill.Offer.Suggestion2.GPayPromoCodeOffer",
      autofill_metrics::OffersSuggestionsEvent::
          kOfferSuggestionSeeOfferDetailsSelected,
      0);

  // Simulate showing the promo code offers suggestions popup.
  EXPECT_TRUE(merchant_promo_code_manager_->OnGetSingleFieldSuggestions(
      test_field_, autofill_client_, base::DoNothing(),
      /*context=*/context));

  // Simulate selecting a promo code offer suggestion.
  merchant_promo_code_manager_->OnSingleFieldSuggestionSelected(
      test_promo_code, SuggestionType::kSeePromoCodeDetails);

  // Check that the histograms logged correctly.
  histogram_tester.ExpectBucketCount(
      "Autofill.Offer.Suggestion2.GPayPromoCodeOffer",
      autofill_metrics::OffersSuggestionsEvent::
          kOfferSuggestionSeeOfferDetailsSelected,
      1);
  histogram_tester.ExpectBucketCount(
      "Autofill.Offer.Suggestion2.GPayPromoCodeOffer",
      autofill_metrics::OffersSuggestionsEvent::
          kOfferSuggestionSeeOfferDetailsSelectedOnce,
      1);

  // Simulate showing the promo code offers suggestions popup.
  EXPECT_TRUE(merchant_promo_code_manager_->OnGetSingleFieldSuggestions(
      test_field_, autofill_client_, base::DoNothing(),
      /*context=*/context));

  // Simulate selecting a promo code offer suggestion.
  merchant_promo_code_manager_->OnSingleFieldSuggestionSelected(
      test_promo_code, SuggestionType::kSeePromoCodeDetails);

  // Check that the histograms logged correctly.
  histogram_tester.ExpectBucketCount(
      "Autofill.Offer.Suggestion2.GPayPromoCodeOffer",
      autofill_metrics::OffersSuggestionsEvent::
          kOfferSuggestionSeeOfferDetailsSelected,
      2);
  histogram_tester.ExpectBucketCount(
      "Autofill.Offer.Suggestion2.GPayPromoCodeOffer",
      autofill_metrics::OffersSuggestionsEvent::
          kOfferSuggestionSeeOfferDetailsSelectedOnce,
      1);
}

}  // namespace autofill
