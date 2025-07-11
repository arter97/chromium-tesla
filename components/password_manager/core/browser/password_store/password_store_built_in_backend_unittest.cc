// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/password_manager/core/browser/password_store/password_store_built_in_backend.h"

#include <cstddef>
#include <memory>
#include <utility>

#include "base/check.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/functional/callback_helpers.h"
#include "base/location.h"
#include "base/test/metrics/histogram_tester.h"
#include "base/test/mock_callback.h"
#include "base/test/scoped_feature_list.h"
#include "base/test/task_environment.h"
#include "base/time/time.h"
#include "components/affiliations/core/browser/fake_affiliation_service.h"
#include "components/os_crypt/sync/os_crypt_mocker.h"
#include "components/password_manager/core/browser/affiliation/affiliated_match_helper.h"
#include "components/password_manager/core/browser/affiliation/mock_affiliated_match_helper.h"
#include "components/password_manager/core/browser/features/password_features.h"
#include "components/password_manager/core/browser/password_form.h"
#include "components/password_manager/core/browser/password_manager_buildflags.h"
#include "components/password_manager/core/browser/password_manager_test_utils.h"
#include "components/password_manager/core/browser/password_store/login_database.h"
#include "components/password_manager/core/browser/password_store/password_store_backend.h"
#include "components/password_manager/core/browser/password_store/password_store_change.h"
#include "components/password_manager/core/browser/password_store/password_store_consumer.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/testing_pref_service.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using testing::ElementsAre;
using testing::ElementsAreArray;
using testing::Optional;
using testing::UnorderedElementsAreArray;
using testing::VariantWith;

namespace password_manager {

namespace {

constexpr const char kTestWebRealm1[] = "https://one.example.com/";
constexpr const char kTestWebOrigin1[] = "https://one.example.com/origin";
constexpr const char kTestWebRealm2[] = "https://two.example.com/";
constexpr const char kTestWebOrigin2[] = "https://two.example.com/origin";
constexpr const char kTestWebRealm3[] = "https://three.example.com/";
constexpr const char kTestWebOrigin3[] = "https://three.example.com/origin";
constexpr const char kTestAndroidRealm1[] =
    "android://hash@com.example.android/";
constexpr const char kTestAndroidRealm2[] =
    "android://hash@com.example.two.android/";
constexpr const char kTestAndroidRealm3[] =
    "android://hash@com.example.three.android/";
constexpr const time_t kTestLastUsageTime = 1546300800;  // 00:00 Jan 1 2019 UTC
constexpr PasswordFormData kTestCredentials[] = {
    {PasswordForm::Scheme::kHtml, kTestAndroidRealm1, "", "", u"", u"", u"",
     u"username_value_1", u"", kTestLastUsageTime, 1},
    {PasswordForm::Scheme::kHtml, kTestAndroidRealm2, "", "", u"", u"", u"",
     u"username_value_2", u"", kTestLastUsageTime, 1},
    {PasswordForm::Scheme::kHtml, kTestAndroidRealm3, "", "", u"", u"", u"",
     u"username_value_3", u"", kTestLastUsageTime, 1},
    {PasswordForm::Scheme::kHtml, kTestWebRealm1, kTestWebOrigin1, "", u"", u"",
     u"", u"username_value_4", u"", kTestLastUsageTime, 1},
    // A PasswordFormData with nullptr as the username_value will be converted
    // in a blocklisted PasswordForm in FillPasswordFormWithData().
    {PasswordForm::Scheme::kHtml, kTestWebRealm2, kTestWebOrigin2, "", u"", u"",
     u"", nullptr, u"", kTestLastUsageTime, 1},
    {PasswordForm::Scheme::kHtml, kTestWebRealm3, kTestWebOrigin3, "", u"", u"",
     u"", nullptr, u"", kTestLastUsageTime, 1}};
constexpr auto kLatencyDelta = base::Milliseconds(123u);
constexpr auto kStart = base::Time::FromTimeT(1000);
constexpr auto kEnd = base::Time::FromTimeT(2000);
constexpr const char kTestAndroidName1[] = "Example Android App 1";
constexpr const char kTestAndroidIconURL1[] = "https://example.com/icon_1.png";
constexpr const char kTestAndroidName2[] = "Example Android App 2";
constexpr const char kTestAndroidIconURL2[] = "https://example.com/icon_2.png";

class MockPasswordStoreBackendTester {
 public:
  MOCK_METHOD(void, LoginsReceivedConstRef, (const LoginsResult&));

  void HandleLoginsOrError(LoginsResultOrError results) {
    LoginsReceivedConstRef(std::move(absl::get<LoginsResult>(results)));
  }
};

// A mock LoginDatabase that simulates a failing Init() method.
class BadLoginDatabase : public LoginDatabase {
 public:
  BadLoginDatabase() : LoginDatabase(base::FilePath(), IsAccountStore(false)) {}

  BadLoginDatabase(const BadLoginDatabase&) = delete;
  BadLoginDatabase& operator=(const BadLoginDatabase&) = delete;

  // LoginDatabase:
  bool Init() override { return false; }
};

PasswordFormData CreateTestPasswordFormData() {
  PasswordFormData data = {PasswordForm::Scheme::kHtml,
                           "http://bar.example.com",
                           "http://bar.example.com/origin",
                           "http://bar.example.com/action",
                           u"submit_element",
                           u"username_element",
                           u"password_element",
                           u"username_value",
                           u"password_value",
                           true,
                           1};
  return data;
}

bool AnyUrl(const GURL& gurl) {
  return true;
}

}  // anonymous namespace

class PasswordStoreBuiltInBackendTest : public testing::Test {
 public:
  PasswordStoreBuiltInBackendTest() = default;

  PasswordStoreBackend* Initialize(
      std::unique_ptr<LoginDatabase> database = nullptr,
      AffiliatedMatchHelper* affiliated_match_helper = nullptr) {
    if (!database) {
      database = std::make_unique<LoginDatabase>(test_login_db_file_path(),
                                                 IsAccountStore(false));
    }

    store_ = std::make_unique<PasswordStoreBuiltInBackend>(
        std::move(database), syncer::WipeModelUponSyncDisabledBehavior::kNever,
        &pref_service_);
    PasswordStoreBackend* backend = store_.get();
    backend->InitBackend(affiliated_match_helper,
                         /*remote_form_changes_received=*/base::DoNothing(),
                         /*sync_enabled_or_disabled_cb=*/base::DoNothing(),
                         /*completion=*/base::DoNothing());
    RunUntilIdle();
    return backend;
  }

  void SetUp() override {
    OSCryptMocker::SetUp();
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
#if !BUILDFLAG(USE_LOGIN_DATABASE_AS_BACKEND)
    pref_service_.registry()->RegisterBooleanPref(
        password_manager::prefs::kEmptyProfileStoreLoginDatabase, false);
#endif
  }

  void TearDown() override {
    PasswordStoreBackend* backend = store_.get();
    backend->Shutdown(base::BindOnce(
        [](std::unique_ptr<PasswordStoreBackend> backend) { backend.reset(); },
        std::move(store_)));
    RunUntilIdle();
    OSCryptMocker::TearDown();
    ASSERT_TRUE(temp_dir_.Delete());
  }

  void RunUntilIdle() { task_environment_.RunUntilIdle(); }

  void AdvanceClock(base::TimeDelta millis) {
    // AdvanceClock is used here because FastForwardBy doesn't work for the
    // intended purpose. FastForwardBy performs the queued actions first and
    // then makes the clock tick and for the tests that follow we want to
    // advance the clock before certain async tasks happen.
    task_environment_.AdvanceClock(millis);
  }

  TestingPrefServiceSimple* pref_service() { return &pref_service_; }

 private:
  void SetupTempDir();

  void ClosePasswordStore();

  base::FilePath test_login_db_file_path() const {
    return temp_dir_.GetPath().Append(FILE_PATH_LITERAL("login_test"));
  }

  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::MainThreadType::UI,
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};

  base::ScopedTempDir temp_dir_;
  std::unique_ptr<PasswordStoreBuiltInBackend> store_;
  TestingPrefServiceSimple pref_service_;
};

TEST_F(PasswordStoreBuiltInBackendTest, NonASCIIData) {
  PasswordStoreBackend* backend = Initialize();

  // Some non-ASCII password form data.
  static const PasswordFormData form_data = {PasswordForm::Scheme::kHtml,
                                             "http://foo.example.com",
                                             "http://foo.example.com/origin",
                                             "http://foo.example.com/action",
                                             u"มีสีสัน",
                                             u"お元気ですか?",
                                             u"盆栽",
                                             u"أحب كرة",
                                             u"£éä국수çà",
                                             true,
                                             1};

  PasswordForm expected_form(*FillPasswordFormWithData(form_data));
  backend->AddLoginAsync(expected_form, base::DoNothing());

  base::MockCallback<LoginsOrErrorReply> mock_reply;
  EXPECT_CALL(mock_reply,
              Run(VariantWith<LoginsResult>(ElementsAre(expected_form))));
  backend->GetAutofillableLoginsAsync(mock_reply.Get());

  RunUntilIdle();
}

TEST_F(PasswordStoreBuiltInBackendTest, TestAddLoginAsync) {
  PasswordStoreBackend* backend = Initialize();
  PasswordForm form = *FillPasswordFormWithData(CreateTestPasswordFormData());

  const PasswordStoreChange add_change =
      PasswordStoreChange(PasswordStoreChange::ADD, form);

  base::MockCallback<PasswordChangesOrErrorReply> mock_reply;
  EXPECT_CALL(
      mock_reply,
      Run(VariantWith<PasswordChanges>(Optional(ElementsAre(add_change)))));
  backend->AddLoginAsync(form, mock_reply.Get());
  RunUntilIdle();
}

TEST_F(PasswordStoreBuiltInBackendTest, TestUpdateLoginAsync) {
  PasswordStoreBackend* backend = Initialize();
  PasswordForm form = *FillPasswordFormWithData(CreateTestPasswordFormData());

  backend->AddLoginAsync(form, base::DoNothing());
  RunUntilIdle();

  form.password_value = u"a different password";
  const PasswordStoreChange update_change =
      PasswordStoreChange(PasswordStoreChange::UPDATE, form);

  base::MockCallback<PasswordChangesOrErrorReply> mock_reply;
  EXPECT_CALL(
      mock_reply,
      Run(VariantWith<PasswordChanges>(Optional(ElementsAre(update_change)))));
  backend->UpdateLoginAsync(form, mock_reply.Get());
  RunUntilIdle();
}

TEST_F(PasswordStoreBuiltInBackendTest, TestRemoveLoginAsync) {
  PasswordStoreBackend* backend = Initialize();
  PasswordForm form = *FillPasswordFormWithData(CreateTestPasswordFormData());

  backend->AddLoginAsync(form, base::DoNothing());
  RunUntilIdle();

  PasswordStoreChange remove_change =
      PasswordStoreChange(PasswordStoreChange::REMOVE, form);

  base::MockCallback<PasswordChangesOrErrorReply> mock_reply;
  EXPECT_CALL(
      mock_reply,
      Run(VariantWith<PasswordChanges>(Optional(ElementsAre(remove_change)))));
  backend->RemoveLoginAsync(FROM_HERE, form, mock_reply.Get());
  RunUntilIdle();
}

TEST_F(PasswordStoreBuiltInBackendTest, GetAllLoginsAsync) {
  PasswordStoreBackend* backend = Initialize();

  // Populate store with test credentials.
  std::vector<std::unique_ptr<PasswordForm>> all_credentials;
  base::MockCallback<PasswordChangesOrErrorReply> reply;
  EXPECT_CALL(reply, Run).Times(6);
  for (const auto& test_credential : kTestCredentials) {
    all_credentials.push_back(FillPasswordFormWithData(test_credential));
    backend->AddLoginAsync(*all_credentials.back(), reply.Get());
  }
  RunUntilIdle();

  // Verify that the store returns all test credentials.
  std::vector<PasswordForm> expected_results;
  for (const auto& credential : all_credentials) {
    expected_results.push_back(*credential);
  }
  base::MockCallback<LoginsOrErrorReply> mock_reply;
  EXPECT_CALL(
      mock_reply,
      Run(VariantWith<LoginsResult>(ElementsAreArray(expected_results))));
  backend->GetAllLoginsAsync(mock_reply.Get());

  RunUntilIdle();
}

TEST_F(PasswordStoreBuiltInBackendTest, GetAllLoginsAsyncMetrics) {
  const char kDurationMetric[] =
      "PasswordManager.PasswordStoreBuiltInBackend.GetAllLoginsAsync.Latency";
  const char kSuccessMetric[] =
      "PasswordManager.PasswordStoreBuiltInBackend.GetAllLoginsAsync.Success";
  base::HistogramTester histogram_tester;

  PasswordStoreBackend* backend = Initialize();

  // Fill the store
  PasswordForm form = *FillPasswordFormWithData(CreateTestPasswordFormData());

  const PasswordStoreChange add_change =
      PasswordStoreChange(PasswordStoreChange::ADD, form);

  base::MockCallback<PasswordChangesOrErrorReply> mock_reply;
  EXPECT_CALL(
      mock_reply,
      Run(VariantWith<PasswordChanges>(Optional(ElementsAre(add_change)))));
  backend->AddLoginAsync(form, mock_reply.Get());

  // Get the logins
  backend->GetAllLoginsAsync(base::DoNothing());

  AdvanceClock(kLatencyDelta);
  RunUntilIdle();

  histogram_tester.ExpectTotalCount(kDurationMetric, 1);
  histogram_tester.ExpectTimeBucketCount(kDurationMetric, kLatencyDelta, 1);
  histogram_tester.ExpectTotalCount(kSuccessMetric, 1);
  histogram_tester.ExpectBucketCount(kSuccessMetric, true, 1);
}

TEST_F(PasswordStoreBuiltInBackendTest, GetAllLoginsAsyncFailsMetrics) {
  const char kDurationMetric[] =
      "PasswordManager.PasswordStoreBuiltInBackend.GetAllLoginsAsync.Latency";
  const char kSuccessMetric[] =
      "PasswordManager.PasswordStoreBuiltInBackend.GetAllLoginsAsync.Success";
  base::HistogramTester histogram_tester;

  PasswordStoreBackend* bad_backend =
      Initialize(std::make_unique<BadLoginDatabase>());

  bad_backend->GetAllLoginsAsync(base::DoNothing());

  AdvanceClock(kLatencyDelta);
  RunUntilIdle();

  histogram_tester.ExpectTotalCount(kDurationMetric, 1);
  histogram_tester.ExpectTimeBucketCount(kDurationMetric, kLatencyDelta, 1);
  histogram_tester.ExpectTotalCount(kSuccessMetric, 1);
  histogram_tester.ExpectBucketCount(kSuccessMetric, false, 1);
}

TEST_F(PasswordStoreBuiltInBackendTest, GetAutofillableLoginsAsyncMetrics) {
  const char kDurationMetricGetLogins[] =
      "PasswordManager.PasswordStoreBuiltInBackend."
      "GetAutofillableLoginsAsync.Latency";
  const char kSuccessMetricGetLogins[] =
      "PasswordManager.PasswordStoreBuiltInBackend."
      "GetAutofillableLoginsAsync.Success";
  const char kDurationMetricAddLogin[] =
      "PasswordManager.PasswordStoreBuiltInBackend.AddLoginAsync.Latency";
  const char kSuccessMetricAddLogin[] =
      "PasswordManager.PasswordStoreBuiltInBackend.AddLoginAsync.Success";
  base::HistogramTester histogram_tester;

  PasswordStoreBackend* backend = Initialize();

  // Fill the store
  PasswordForm form = *FillPasswordFormWithData(CreateTestPasswordFormData());

  const PasswordStoreChange add_change =
      PasswordStoreChange(PasswordStoreChange::ADD, form);

  base::MockCallback<PasswordChangesOrErrorReply> mock_reply;
  EXPECT_CALL(
      mock_reply,
      Run(VariantWith<PasswordChanges>(Optional(ElementsAre(add_change)))));
  backend->AddLoginAsync(form, mock_reply.Get());

  // Get the logins
  backend->GetAutofillableLoginsAsync(base::DoNothing());

  AdvanceClock(kLatencyDelta);
  RunUntilIdle();

  // Metrics for GetAutofillableLoginsAsyncMetrics
  histogram_tester.ExpectTotalCount(kDurationMetricGetLogins, 1);
  histogram_tester.ExpectTimeBucketCount(kDurationMetricGetLogins,
                                         kLatencyDelta, 1);
  histogram_tester.ExpectTotalCount(kSuccessMetricGetLogins, 1);
  histogram_tester.ExpectBucketCount(kSuccessMetricGetLogins, true, 1);
  // Metrics for AddLoginAsync that also gets called in this test
  histogram_tester.ExpectTotalCount(kDurationMetricAddLogin, 1);
  histogram_tester.ExpectTimeBucketCount(kDurationMetricAddLogin, kLatencyDelta,
                                         1);
  histogram_tester.ExpectTotalCount(kSuccessMetricAddLogin, 1);
  histogram_tester.ExpectBucketCount(kSuccessMetricAddLogin, true, 1);
}

TEST_F(PasswordStoreBuiltInBackendTest,
       GetAutofillableLoginsAsyncFailsMetrics) {
  const char kDurationMetricGetLogins[] =
      "PasswordManager.PasswordStoreBuiltInBackend."
      "GetAutofillableLoginsAsync.Latency";
  const char kSuccessMetricGetLogins[] =
      "PasswordManager.PasswordStoreBuiltInBackend."
      "GetAutofillableLoginsAsync.Success";
  const char kDurationMetricAddLogin[] =
      "PasswordManager.PasswordStoreBuiltInBackend.AddLoginAsync.Latency";
  const char kSuccessMetricAddLogin[] =
      "PasswordManager.PasswordStoreBuiltInBackend.AddLoginAsync.Success";
  base::HistogramTester histogram_tester;

  PasswordStoreBackend* bad_backend =
      Initialize(std::make_unique<BadLoginDatabase>());

  // Fill the store
  PasswordForm form = *FillPasswordFormWithData(CreateTestPasswordFormData());
  bad_backend->AddLoginAsync(form, base::DoNothing());

  // Get the logins
  bad_backend->GetAutofillableLoginsAsync(base::DoNothing());

  AdvanceClock(kLatencyDelta);
  RunUntilIdle();

  // Metrics for GetAutofillableLoginsAsyncMetrics
  histogram_tester.ExpectTotalCount(kDurationMetricGetLogins, 1);
  histogram_tester.ExpectTimeBucketCount(kDurationMetricGetLogins,
                                         kLatencyDelta, 1);
  histogram_tester.ExpectTotalCount(kSuccessMetricGetLogins, 1);
  histogram_tester.ExpectBucketCount(kSuccessMetricGetLogins, false, 1);
  // Metrics for AddLoginAsync that also gets called in this test
  histogram_tester.ExpectTotalCount(kDurationMetricAddLogin, 1);
  histogram_tester.ExpectTimeBucketCount(kDurationMetricAddLogin, kLatencyDelta,
                                         1);
  histogram_tester.ExpectTotalCount(kSuccessMetricAddLogin, 1);
  histogram_tester.ExpectBucketCount(kSuccessMetricAddLogin, false, 1);
}

TEST_F(PasswordStoreBuiltInBackendTest, UpdateLoginAsyncMetrics) {
  const char kDurationMetric[] =
      "PasswordManager.PasswordStoreBuiltInBackend.UpdateLoginAsync.Latency";
  const char kSuccessMetric[] =
      "PasswordManager.PasswordStoreBuiltInBackend.UpdateLoginAsync.Success";
  base::HistogramTester histogram_tester;

  PasswordStoreBackend* backend = Initialize();
  PasswordForm form = *FillPasswordFormWithData(CreateTestPasswordFormData());

  backend->AddLoginAsync(form, base::DoNothing());
  RunUntilIdle();

  form.password_value = u"a different password";
  const PasswordStoreChange update_change =
      PasswordStoreChange(PasswordStoreChange::UPDATE, form);

  base::MockCallback<PasswordChangesOrErrorReply> mock_reply;
  EXPECT_CALL(
      mock_reply,
      Run(VariantWith<PasswordChanges>(Optional(ElementsAre(update_change)))));
  backend->UpdateLoginAsync(form, mock_reply.Get());

  AdvanceClock(kLatencyDelta);
  RunUntilIdle();

  histogram_tester.ExpectTotalCount(kDurationMetric, 1);
  histogram_tester.ExpectTimeBucketCount(kDurationMetric, kLatencyDelta, 1);
  histogram_tester.ExpectTotalCount(kSuccessMetric, 1);
  histogram_tester.ExpectBucketCount(kSuccessMetric, true, 1);
}

TEST_F(PasswordStoreBuiltInBackendTest, UpdateLoginAsyncFailsMetrics) {
  const char kDurationMetric[] =
      "PasswordManager.PasswordStoreBuiltInBackend.UpdateLoginAsync.Latency";
  const char kSuccessMetric[] =
      "PasswordManager.PasswordStoreBuiltInBackend.UpdateLoginAsync.Success";
  base::HistogramTester histogram_tester;

  PasswordStoreBackend* bad_backend =
      Initialize(std::make_unique<BadLoginDatabase>());
  PasswordForm form = *FillPasswordFormWithData(CreateTestPasswordFormData());

  bad_backend->UpdateLoginAsync(form, base::DoNothing());

  AdvanceClock(kLatencyDelta);
  RunUntilIdle();

  histogram_tester.ExpectTotalCount(kDurationMetric, 1);
  histogram_tester.ExpectTimeBucketCount(kDurationMetric, kLatencyDelta, 1);
  histogram_tester.ExpectTotalCount(kSuccessMetric, 1);
  histogram_tester.ExpectBucketCount(kSuccessMetric, false, 1);
}

TEST_F(PasswordStoreBuiltInBackendTest, RemoveLoginAsyncMetrics) {
  const char kDurationMetric[] =
      "PasswordManager.PasswordStoreBuiltInBackend.RemoveLoginAsync.Latency";
  const char kSuccessMetric[] =
      "PasswordManager.PasswordStoreBuiltInBackend.RemoveLoginAsync.Success";
  base::HistogramTester histogram_tester;

  PasswordStoreBackend* backend = Initialize();
  PasswordForm form = *FillPasswordFormWithData(CreateTestPasswordFormData());

  backend->AddLoginAsync(form, base::DoNothing());
  RunUntilIdle();

  PasswordStoreChange remove_change =
      PasswordStoreChange(PasswordStoreChange::REMOVE, form);

  backend->RemoveLoginAsync(FROM_HERE, form, base::DoNothing());

  AdvanceClock(kLatencyDelta);
  RunUntilIdle();

  histogram_tester.ExpectTotalCount(kDurationMetric, 1);
  histogram_tester.ExpectTimeBucketCount(kDurationMetric, kLatencyDelta, 1);
  histogram_tester.ExpectTotalCount(kSuccessMetric, 1);
  histogram_tester.ExpectBucketCount(kSuccessMetric, true, 1);
}

TEST_F(PasswordStoreBuiltInBackendTest, RemoveLoginAsyncFailsMetrics) {
  const char kDurationMetric[] =
      "PasswordManager.PasswordStoreBuiltInBackend.RemoveLoginAsync.Latency";
  base::HistogramTester histogram_tester;

  PasswordStoreBackend* bad_backend =
      Initialize(std::make_unique<BadLoginDatabase>());
  PasswordForm form = *FillPasswordFormWithData(CreateTestPasswordFormData());

  bad_backend->AddLoginAsync(form, base::DoNothing());
  RunUntilIdle();

  PasswordStoreChange remove_change =
      PasswordStoreChange(PasswordStoreChange::REMOVE, form);

  bad_backend->RemoveLoginAsync(FROM_HERE, form, base::DoNothing());

  AdvanceClock(kLatencyDelta);
  RunUntilIdle();

  histogram_tester.ExpectTotalCount(kDurationMetric, 1);
  histogram_tester.ExpectTimeBucketCount(kDurationMetric, kLatencyDelta, 1);
}

TEST_F(PasswordStoreBuiltInBackendTest,
       RemoveLoginsCreatedBetweenAsyncMetrics) {
  const char kDurationMetric[] =
      "PasswordManager.PasswordStoreBuiltInBackend."
      "RemoveLoginsCreatedBetweenAsync."
      "Latency";
  const char kSuccessMetric[] =
      "PasswordManager.PasswordStoreBuiltInBackend."
      "RemoveLoginsCreatedBetweenAsync."
      "Success";
  base::HistogramTester histogram_tester;

  PasswordStoreBackend* backend = Initialize();
  PasswordForm form = *FillPasswordFormWithData(CreateTestPasswordFormData());
  form.date_created = base::Time::FromTimeT(1500);
  backend->AddLoginAsync(form, base::DoNothing());
  RunUntilIdle();

  backend->RemoveLoginsCreatedBetweenAsync(FROM_HERE, kStart, kEnd,
                                           base::DoNothing());

  AdvanceClock(kLatencyDelta);
  RunUntilIdle();

  histogram_tester.ExpectTotalCount(kDurationMetric, 1);
  histogram_tester.ExpectTimeBucketCount(kDurationMetric, kLatencyDelta, 1);
  histogram_tester.ExpectTotalCount(kSuccessMetric, 1);
  histogram_tester.ExpectBucketCount(kSuccessMetric, true, 1);
}

TEST_F(PasswordStoreBuiltInBackendTest,
       RemoveLoginsCreatedBetweenAsyncNothingToDeleteMetrics) {
  const char kDurationMetric[] =
      "PasswordManager.PasswordStoreBuiltInBackend."
      "RemoveLoginsCreatedBetweenAsync."
      "Latency";
  const char kSuccessMetric[] =
      "PasswordManager.PasswordStoreBuiltInBackend."
      "RemoveLoginsCreatedBetweenAsync."
      "Success";
  base::HistogramTester histogram_tester;

  PasswordStoreBackend* backend = Initialize();
  PasswordForm form = *FillPasswordFormWithData(CreateTestPasswordFormData());
  form.date_created = base::Time::FromTimeT(300);
  backend->AddLoginAsync(form, base::DoNothing());
  RunUntilIdle();

  backend->RemoveLoginsCreatedBetweenAsync(FROM_HERE, kStart, kEnd,
                                           base::DoNothing());

  AdvanceClock(kLatencyDelta);
  RunUntilIdle();

  histogram_tester.ExpectTotalCount(kDurationMetric, 1);
  histogram_tester.ExpectTimeBucketCount(kDurationMetric, kLatencyDelta, 1);
  histogram_tester.ExpectTotalCount(kSuccessMetric, 1);
  histogram_tester.ExpectBucketCount(kSuccessMetric, true, 1);
}

TEST_F(PasswordStoreBuiltInBackendTest,
       RemoveLoginsCreatedBetweenAsyncFailsMetrics) {
  const char kDurationMetric[] =
      "PasswordManager.PasswordStoreBuiltInBackend."
      "RemoveLoginsCreatedBetweenAsync."
      "Latency";
  const char kSuccessMetric[] =
      "PasswordManager.PasswordStoreBuiltInBackend."
      "RemoveLoginsCreatedBetweenAsync."
      "Success";
  base::HistogramTester histogram_tester;

  PasswordStoreBackend* bad_backend =
      Initialize(std::make_unique<BadLoginDatabase>());

  bad_backend->RemoveLoginsCreatedBetweenAsync(FROM_HERE, kStart, kEnd,
                                               base::DoNothing());

  AdvanceClock(kLatencyDelta);
  RunUntilIdle();

  histogram_tester.ExpectTotalCount(kDurationMetric, 1);
  histogram_tester.ExpectTimeBucketCount(kDurationMetric, kLatencyDelta, 1);
  histogram_tester.ExpectTotalCount(kSuccessMetric, 1);
  histogram_tester.ExpectBucketCount(kSuccessMetric, false, 1);
}

TEST_F(PasswordStoreBuiltInBackendTest, RemoveLoginsByURLAndTimeAsyncMetrics) {
  const char kDurationMetric[] =
      "PasswordManager.PasswordStoreBuiltInBackend."
      "RemoveLoginsByURLAndTimeAsync."
      "Latency";
  const char kSuccessMetric[] =
      "PasswordManager.PasswordStoreBuiltInBackend."
      "RemoveLoginsByURLAndTimeAsync."
      "Success";
  base::HistogramTester histogram_tester;

  PasswordStoreBackend* backend = Initialize();
  PasswordForm form = *FillPasswordFormWithData(CreateTestPasswordFormData());
  form.date_created = kStart + base::Milliseconds(500);
  DCHECK(form.date_created < kEnd);
  backend->AddLoginAsync(form, base::DoNothing());
  RunUntilIdle();

  backend->RemoveLoginsByURLAndTimeAsync(
      FROM_HERE, base::BindRepeating(&AnyUrl), kStart, kEnd, base::DoNothing(),
      base::DoNothing());

  AdvanceClock(kLatencyDelta);
  RunUntilIdle();

  histogram_tester.ExpectTotalCount(kDurationMetric, 1);
  histogram_tester.ExpectTimeBucketCount(kDurationMetric, kLatencyDelta, 1);
  histogram_tester.ExpectTotalCount(kSuccessMetric, 1);
  histogram_tester.ExpectBucketCount(kSuccessMetric, true, 1);
}

TEST_F(PasswordStoreBuiltInBackendTest,
       RemoveLoginsByURLAndTimeAsyncNothingToDeleteMetrics) {
  const char kDurationMetric[] =
      "PasswordManager.PasswordStoreBuiltInBackend."
      "RemoveLoginsByURLAndTimeAsync."
      "Latency";
  const char kSuccessMetric[] =
      "PasswordManager.PasswordStoreBuiltInBackend."
      "RemoveLoginsByURLAndTimeAsync."
      "Success";
  base::HistogramTester histogram_tester;

  PasswordStoreBackend* backend = Initialize();
  PasswordForm form = *FillPasswordFormWithData(CreateTestPasswordFormData());
  form.date_created = kStart - base::Milliseconds(500);
  backend->AddLoginAsync(form, base::DoNothing());
  RunUntilIdle();

  backend->RemoveLoginsByURLAndTimeAsync(
      FROM_HERE, base::BindRepeating(&AnyUrl), kStart, kEnd, base::DoNothing(),
      base::DoNothing());

  AdvanceClock(kLatencyDelta);
  RunUntilIdle();

  histogram_tester.ExpectTotalCount(kDurationMetric, 1);
  histogram_tester.ExpectTimeBucketCount(kDurationMetric, kLatencyDelta, 1);
  histogram_tester.ExpectTotalCount(kSuccessMetric, 1);
  histogram_tester.ExpectBucketCount(kSuccessMetric, true, 1);
}

TEST_F(PasswordStoreBuiltInBackendTest, FillMatchingLoginsAsyncMetrics) {
  const char kDurationMetric[] =
      "PasswordManager.PasswordStoreBuiltInBackend."
      "FillMatchingLoginsAsync."
      "Latency";
  const char kSuccessMetric[] =
      "PasswordManager.PasswordStoreBuiltInBackend."
      "FillMatchingLoginsAsync."
      "Success";
  base::HistogramTester histogram_tester;

  PasswordStoreBackend* backend = Initialize();
  PasswordForm form = *FillPasswordFormWithData(CreateTestPasswordFormData());
  const std::string kTestPasswordFormURL = form.signon_realm;
  backend->AddLoginAsync(std::move(form), base::DoNothing());
  RunUntilIdle();

  std::vector<PasswordFormDigest> forms;
  forms.emplace_back(PasswordFormDigest(PasswordForm::Scheme::kHtml,
                                        kTestPasswordFormURL,
                                        GURL(kTestPasswordFormURL)));

  backend->FillMatchingLoginsAsync(base::DoNothing(), /*include_psl=*/false,
                                   std::move(forms));
  AdvanceClock(kLatencyDelta);
  RunUntilIdle();

  histogram_tester.ExpectTotalCount(kDurationMetric, 1);
  histogram_tester.ExpectTimeBucketCount(kDurationMetric, kLatencyDelta, 1);
  histogram_tester.ExpectTotalCount(kSuccessMetric, 1);
  histogram_tester.ExpectBucketCount(kSuccessMetric, true, 1);
}

TEST_F(PasswordStoreBuiltInBackendTest,
       FillMatchingLoginsAsyncNothingToFillMetrics) {
  const char kDurationMetric[] =
      "PasswordManager.PasswordStoreBuiltInBackend."
      "FillMatchingLoginsAsync."
      "Latency";
  const char kSuccessMetric[] =
      "PasswordManager.PasswordStoreBuiltInBackend."
      "FillMatchingLoginsAsync."
      "Success";
  base::HistogramTester histogram_tester;
  std::string kTestPasswordFormURL("http://foo.example.com");

  PasswordStoreBackend* backend = Initialize();

  std::vector<PasswordFormDigest> forms;
  forms.emplace_back(PasswordFormDigest(PasswordForm::Scheme::kHtml,
                                        kTestPasswordFormURL,
                                        GURL(kTestPasswordFormURL)));

  backend->FillMatchingLoginsAsync(base::DoNothing(), /*include_psl=*/false,
                                   std::move(forms));
  AdvanceClock(kLatencyDelta);
  RunUntilIdle();

  histogram_tester.ExpectTotalCount(kDurationMetric, 1);
  histogram_tester.ExpectTimeBucketCount(kDurationMetric, kLatencyDelta, 1);
  histogram_tester.ExpectTotalCount(kSuccessMetric, 1);
  histogram_tester.ExpectBucketCount(kSuccessMetric, true, 1);
}

TEST_F(PasswordStoreBuiltInBackendTest, GetLoginsWithAffiliations) {
  affiliations::FakeAffiliationService fake_affiliation_service;
  MockAffiliatedMatchHelper mock_affiliated_match_helper(
      &fake_affiliation_service);
  PasswordStoreBackend* backend =
      Initialize(nullptr, &mock_affiliated_match_helper);

  std::vector<std::unique_ptr<PasswordForm>> all_credentials;
  for (const auto& test_credential : kTestCredentials) {
    all_credentials.push_back(FillPasswordFormWithData(test_credential));
    backend->AddLoginAsync(*all_credentials.back(), base::DoNothing());
    RunUntilIdle();
  }

  std::vector<PasswordForm> expected_results;
  expected_results.push_back(*all_credentials[0]);
  expected_results.back().match_type = PasswordForm::MatchType::kAffiliated;
  expected_results.push_back(*all_credentials[3]);
  expected_results.back().match_type = PasswordForm::MatchType::kExact;
  expected_results.push_back(*all_credentials[4]);
  expected_results.back().match_type = PasswordForm::MatchType::kPSL;
  expected_results.push_back(*all_credentials[5]);
  expected_results.back().match_type =
      PasswordForm::MatchType::kPSL | PasswordForm::MatchType::kGrouped;

  PasswordFormDigest observed_form = {PasswordForm::Scheme::kHtml,
                                      kTestWebRealm1, GURL(kTestWebOrigin1)};

  std::vector<std::string> affiliated_android_realms;
  affiliated_android_realms.push_back(kTestAndroidRealm1);
  std::vector<std::string> grouped_realms;
  grouped_realms.push_back(kTestWebRealm3);

  mock_affiliated_match_helper.ExpectCallToGetAffiliatedAndGrouped(
      observed_form, affiliated_android_realms, grouped_realms);
  mock_affiliated_match_helper
      .ExpectCallToInjectAffiliationAndBrandingInformation({});
  base::MockCallback<LoginsOrErrorReply> mock_reply;
  EXPECT_CALL(mock_reply, Run(VariantWith<LoginsResult>(
                              UnorderedElementsAreArray(expected_results))));

  backend->GetGroupedMatchingLoginsAsync(observed_form, mock_reply.Get());
  RunUntilIdle();
}

TEST_F(PasswordStoreBuiltInBackendTest,
       GetAllLoginsWithAffiliationAndBrandingInformation) {
  affiliations::FakeAffiliationService fake_affiliation_service;
  MockAffiliatedMatchHelper mock_affiliated_match_helper(
      &fake_affiliation_service);
  PasswordStoreBackend* backend =
      Initialize(nullptr, &mock_affiliated_match_helper);

  std::vector<std::unique_ptr<PasswordForm>> all_credentials;
  for (const auto& test_credential : kTestCredentials) {
    all_credentials.push_back(FillPasswordFormWithData(test_credential));
    backend->AddLoginAsync(*all_credentials.back(), base::DoNothing());
    RunUntilIdle();
  }

  std::vector<PasswordForm> expected_results;
  for (const auto& credential : all_credentials) {
    expected_results.push_back(*credential);
  }

  std::vector<MockAffiliatedMatchHelper::AffiliationAndBrandingInformation>
      affiliation_info_for_results = {
          {kTestWebRealm1, kTestAndroidName1, GURL(kTestAndroidIconURL1)},
          {/* Pretend affiliation or branding info is unavailable. */},
          {kTestWebRealm2, kTestAndroidName2, GURL(kTestAndroidIconURL2)},
          {/* Pretend affiliation or branding info is unavailable. */},
          {/* Pretend affiliation or branding info is unavailable. */},
          {/* Pretend affiliation or branding info is unavailable. */}};

  mock_affiliated_match_helper
      .ExpectCallToInjectAffiliationAndBrandingInformation(
          affiliation_info_for_results);

  for (size_t i = 0; i < expected_results.size(); ++i) {
    expected_results[i].affiliated_web_realm =
        affiliation_info_for_results[i].affiliated_web_realm;
    expected_results[i].app_display_name =
        affiliation_info_for_results[i].app_display_name;
    expected_results[i].app_icon_url =
        affiliation_info_for_results[i].app_icon_url;
  }

  base::MockCallback<LoginsOrErrorReply> mock_reply;
  EXPECT_CALL(mock_reply, Run(VariantWith<LoginsResult>(
                              UnorderedElementsAreArray(expected_results))));

  backend->GetAllLoginsWithAffiliationAndBrandingAsync(mock_reply.Get());
  RunUntilIdle();
}

#if !BUILDFLAG(USE_LOGIN_DATABASE_AS_BACKEND)
TEST_F(PasswordStoreBuiltInBackendTest, NotAbleToSavePasswordsEmptyDB) {
  base::test::ScopedFeatureList features(
      password_manager::features::kUnifiedPasswordManagerSyncOnlyInGMSCore);
  pref_service()->SetBoolean(
      password_manager::prefs::kEmptyProfileStoreLoginDatabase, true);
  PasswordStoreBackend* backend = Initialize();
  EXPECT_FALSE(backend->IsAbleToSavePasswords());
}

TEST_F(PasswordStoreBuiltInBackendTest, IsAbleToSavePasswords) {
  base::test::ScopedFeatureList features(
      password_manager::features::kUnifiedPasswordManagerSyncOnlyInGMSCore);
  pref_service()->SetBoolean(
      password_manager::prefs::kEmptyProfileStoreLoginDatabase, false);
  PasswordStoreBackend* backend = Initialize();
  EXPECT_TRUE(backend->IsAbleToSavePasswords());
}

TEST_F(PasswordStoreBuiltInBackendTest, AbleToSavePasswordsFeatureDisabled) {
  base::test::ScopedFeatureList features;
  features.InitAndDisableFeature(
      password_manager::features::kUnifiedPasswordManagerSyncOnlyInGMSCore);
  pref_service()->SetBoolean(
      password_manager::prefs::kEmptyProfileStoreLoginDatabase, false);
  PasswordStoreBackend* backend = Initialize();
  EXPECT_TRUE(backend->IsAbleToSavePasswords());
}
#endif

TEST_F(PasswordStoreBuiltInBackendTest, NotAbleSavePasswordsWhenDatabaseIsBad) {
  PasswordStoreBackend* bad_backend =
      Initialize(std::make_unique<BadLoginDatabase>());

  EXPECT_FALSE(bad_backend->IsAbleToSavePasswords());
}

}  // namespace password_manager
