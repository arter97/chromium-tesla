// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/password_manager/android/built_in_backend_to_android_backend_migrator.h"

#include "base/strings/strcat.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/test/gmock_callback_support.h"
#include "base/test/metrics/histogram_tester.h"
#include "base/test/mock_callback.h"
#include "base/test/task_environment.h"
#include "base/time/time.h"
#include "chrome/browser/signin/identity_manager_factory.h"
#include "chrome/browser/signin/identity_test_environment_profile_adaptor.h"
#include "components/password_manager/core/browser/password_manager_metrics_util.h"
#include "components/password_manager/core/browser/password_manager_test_utils.h"
#include "components/password_manager/core/browser/password_store/fake_password_store_backend.h"
#include "components/password_manager/core/browser/password_store/mock_password_store_backend.h"
#include "components/password_manager/core/browser/password_store/password_store_backend.h"
#include "components/password_manager/core/common/password_manager_pref_names.h"
#include "components/prefs/pref_registry.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/testing_pref_service.h"
#include "components/signin/public/base/signin_pref_names.h"
#include "components/sync/base/pref_names.h"
#include "components/sync/service/sync_prefs.h"
#include "components/sync/test/test_sync_service.h"
#include "components/sync_preferences/testing_pref_service_syncable.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using password_manager::prefs::kPasswordsUseUPMLocalAndSeparateStores;
using password_manager::prefs::UseUpmLocalAndSeparateStoresState;
using ::testing::ElementsAre;
using ::testing::ElementsAreArray;
using ::testing::Eq;
using ::testing::Invoke;
using ::testing::IsEmpty;
using ::testing::Pointee;
using ::testing::Return;
using ::testing::UnorderedElementsAreArray;
using ::testing::VariantWith;
using ::testing::WithArg;
using MigrationType =
    password_manager::BuiltInBackendToAndroidBackendMigrator::MigrationType;

namespace password_manager {

namespace {

constexpr base::TimeDelta kLatencyDelta = base::Milliseconds(123u);
const PasswordStoreBackendError kBackendError = PasswordStoreBackendError(
    PasswordStoreBackendErrorType::kUncategorized,
    PasswordStoreBackendErrorRecoveryType::kUnrecoverable);

const char kMigrationProgressStateHistogram[] =
    "PasswordManager.UnifiedPasswordManager.MigrationForLocalUsers."
    "ProgressState";

PasswordForm CreateTestPasswordForm(int index = 0) {
  PasswordForm form;
  form.url = GURL("https://test" + base::NumberToString(index) + ".com");
  form.signon_realm = form.url.spec();
  form.username_value = u"username" + base::NumberToString16(index);
  form.password_value = u"password" + base::NumberToString16(index);
  form.in_store = PasswordForm::Store::kProfileStore;
  return form;
}

}  // namespace

// Checks that initial/rolling migration is started only when all the conditions
// are satisfied. It also check that migration result is properly recorded in
// prefs.
class BuiltInBackendToAndroidBackendMigratorTest : public testing::Test {
 protected:
  BuiltInBackendToAndroidBackendMigratorTest() = default;
  ~BuiltInBackendToAndroidBackendMigratorTest() override = default;

  void Init(int current_migration_version = 0) {
    prefs_.registry()->RegisterIntegerPref(
        prefs::kCurrentMigrationVersionToGoogleMobileServices, 0);
    prefs_.SetInteger(prefs::kCurrentMigrationVersionToGoogleMobileServices,
                      current_migration_version);
    prefs_.registry()->RegisterDoublePref(prefs::kTimeOfLastMigrationAttempt,
                                          0.0);
    prefs_.registry()->RegisterBooleanPref(
        prefs::kRequiresMigrationAfterSyncStatusChange, false);
    prefs_.registry()->RegisterStringPref(
        ::prefs::kGoogleServicesLastSyncingUsername, "testaccount@gmail.com");
    prefs_.registry()->RegisterBooleanPref(
        prefs::kUnenrolledFromGoogleMobileServicesDueToErrors, false);
    prefs_.registry()->RegisterIntegerPref(
        prefs::kUnenrolledFromGoogleMobileServicesAfterApiErrorCode, 0);
    prefs_.registry()->RegisterIntegerPref(
        prefs::kTimesReenrolledToGoogleMobileServices, 0);
    prefs_.registry()->RegisterIntegerPref(
        prefs::kTimesAttemptedToReenrollToGoogleMobileServices, 0);
    prefs_.registry()->RegisterIntegerPref(
        prefs::kPasswordsUseUPMLocalAndSeparateStores,
        static_cast<int>(
            password_manager::prefs::UseUpmLocalAndSeparateStoresState::kOff));
    prefs_.registry()->RegisterBooleanPref(
        prefs::kShouldShowPostPasswordMigrationSheetAtStartup, false);
    prefs_.registry()->RegisterBooleanPref(
        prefs::kEmptyProfileStoreLoginDatabase, false);
    prefs_.registry()->RegisterBooleanPref(
        syncer::prefs::internal::kSyncInitialSyncFeatureSetupComplete, false);
    prefs_.registry()->RegisterBooleanPref(
        syncer::prefs::internal::kSyncKeepEverythingSynced, false);
    prefs_.registry()->RegisterBooleanPref(
        base::StrCat({syncer::prefs::internal::
                          kSyncDataTypeStatusForSyncToSigninMigrationPrefix,
                      ".",
                      syncer::GetModelTypeLowerCaseRootTag(syncer::PASSWORDS)}),
        false);
    CreateMigrator(&built_in_backend_, &android_backend_, &prefs_);
  }

  void InitSyncService(bool is_password_sync_enabled) {
    if (is_password_sync_enabled) {
      sync_service_.GetUserSettings()->SetSelectedTypes(
          /*sync_everything=*/false,
          /*types=*/{syncer::UserSelectableType::kPasswords});
    } else {
      sync_service_.GetUserSettings()->SetSelectedTypes(
          /*sync_everything=*/false, /*types=*/{});
    }
    migrator()->OnSyncServiceInitialized(&sync_service_);
  }

  // BuiltInBackendToAndroidBackendMigrator reads whether password sync is
  // enabled from a pref rather than the SyncService. This helper sets such
  // pref.
  void SetPasswordSyncEnabledPref(bool enabled) {
    if (enabled) {
      prefs_.SetBoolean(
          syncer::prefs::internal::kSyncInitialSyncFeatureSetupComplete, true);
      prefs_.SetBoolean(syncer::prefs::internal::kSyncKeepEverythingSynced,
                        true);
      prefs_.SetBoolean(
          base::StrCat(
              {syncer::prefs::internal::
                   kSyncDataTypeStatusForSyncToSigninMigrationPrefix,
               ".", syncer::GetModelTypeLowerCaseRootTag(syncer::PASSWORDS)}),
          true);
    } else {
      prefs_.SetBoolean(
          syncer::prefs::internal::kSyncInitialSyncFeatureSetupComplete, false);
    }
  }

  void CreateMigrator(PasswordStoreBackend* built_in_backend,
                      PasswordStoreBackend* android_backend,
                      PrefService* prefs) {
    migrator_ = std::make_unique<BuiltInBackendToAndroidBackendMigrator>(
        built_in_backend, android_backend, prefs);
  }

  PasswordStoreBackend& built_in_backend() { return built_in_backend_; }
  PasswordStoreBackend& android_backend() { return android_backend_; }

  TestingPrefServiceSimple* prefs() { return &prefs_; }
  BuiltInBackendToAndroidBackendMigrator* migrator() { return migrator_.get(); }

  void RunUntilIdle() { task_env_.RunUntilIdle(); }
  void FastForwardBy(base::TimeDelta delta) { task_env_.FastForwardBy(delta); }

 private:
  base::test::SingleThreadTaskEnvironment task_env_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  TestingPrefServiceSimple prefs_;
  syncer::TestSyncService sync_service_;
  FakePasswordStoreBackend built_in_backend_;
  FakePasswordStoreBackend android_backend_{
      IsAccountStore(false),
      FakePasswordStoreBackend::UpdateAlwaysSucceeds(true)};
  std::unique_ptr<BuiltInBackendToAndroidBackendMigrator> migrator_;
};

TEST_F(BuiltInBackendToAndroidBackendMigratorTest,
       CurrentMigrationVersionIsUpdatedWhenMigrationIsNeeded_SyncOn) {
  Init();
  InitSyncService(/*is_password_sync_enabled=*/true);

  migrator()->StartAccountMigrationIfNecessary(
      MigrationType::kInitialForSyncUsers);
  RunUntilIdle();

  EXPECT_EQ(1, prefs()->GetInteger(
                   prefs::kCurrentMigrationVersionToGoogleMobileServices));
  EXPECT_EQ(
      base::Time::Now().InSecondsFSinceUnixEpoch(),
      prefs()->GetDouble(password_manager::prefs::kTimeOfLastMigrationAttempt));
}

TEST_F(BuiltInBackendToAndroidBackendMigratorTest,
       LocalPasswordMigrationRecordsProgressState) {
  base::HistogramTester histogram_tester;
  Init();

  prefs()->SetInteger(
      password_manager::prefs::kPasswordsUseUPMLocalAndSeparateStores,
      static_cast<int>(
          password_manager::prefs::UseUpmLocalAndSeparateStoresState::
              kOffAndMigrationPending));

  InitSyncService(/*is_password_sync_enabled=*/false);

  migrator()->StartMigrationOfLocalPasswords();
  histogram_tester.ExpectBucketCount(
      kMigrationProgressStateHistogram,
      metrics_util::LocalPwdMigrationProgressState::kStarted, 1);

  RunUntilIdle();
  ASSERT_EQ(static_cast<int>(UseUpmLocalAndSeparateStoresState::kOn),
            prefs()->GetInteger(kPasswordsUseUPMLocalAndSeparateStores));
  histogram_tester.ExpectBucketCount(
      kMigrationProgressStateHistogram,
      metrics_util::LocalPwdMigrationProgressState::kFinished, 1);
}

TEST_F(BuiltInBackendToAndroidBackendMigratorTest,
       AllPrefsAreUpdatedAfterLocalPasswordsMigration) {
  Init();

  prefs()->SetInteger(
      password_manager::prefs::kPasswordsUseUPMLocalAndSeparateStores,
      static_cast<int>(
          password_manager::prefs::UseUpmLocalAndSeparateStoresState::
              kOffAndMigrationPending));

  InitSyncService(/*is_password_sync_enabled=*/false);

  migrator()->StartMigrationOfLocalPasswords();

  RunUntilIdle();

  EXPECT_EQ(static_cast<int>(UseUpmLocalAndSeparateStoresState::kOn),
            prefs()->GetInteger(kPasswordsUseUPMLocalAndSeparateStores));
  EXPECT_EQ(
      base::Time::Now().InSecondsFSinceUnixEpoch(),
      prefs()->GetDouble(password_manager::prefs::kTimeOfLastMigrationAttempt));
  EXPECT_TRUE(prefs()->GetBoolean(
      prefs::kShouldShowPostPasswordMigrationSheetAtStartup));
}

TEST_F(BuiltInBackendToAndroidBackendMigratorTest,
       AllPrefsAreUpdatedAfterOnlySettingsMigration) {
  Init();

  prefs()->SetInteger(
      password_manager::prefs::kPasswordsUseUPMLocalAndSeparateStores,
      static_cast<int>(
          password_manager::prefs::UseUpmLocalAndSeparateStoresState::
              kOffAndMigrationPending));
  prefs()->SetBoolean(password_manager::prefs::kEmptyProfileStoreLoginDatabase,
                      true);

  InitSyncService(/*is_password_sync_enabled=*/false);

  migrator()->StartMigrationOfLocalPasswords();
  RunUntilIdle();

  EXPECT_EQ(static_cast<int>(UseUpmLocalAndSeparateStoresState::kOn),
            prefs()->GetInteger(kPasswordsUseUPMLocalAndSeparateStores));
  EXPECT_EQ(
      base::Time::Now().InSecondsFSinceUnixEpoch(),
      prefs()->GetDouble(password_manager::prefs::kTimeOfLastMigrationAttempt));
  EXPECT_FALSE(prefs()->GetBoolean(
      prefs::kShouldShowPostPasswordMigrationSheetAtStartup));
}

// The user was syncing and got unenrolled, but then disabled sync and the
// unenrollment pref wasn't reset.
TEST_F(BuiltInBackendToAndroidBackendMigratorTest,
       PostMigrationSheetIsScheduledForLocalUnenrolledUsers) {
  Init();
  SetPasswordSyncEnabledPref(false);
  prefs()->SetBoolean(prefs::kUnenrolledFromGoogleMobileServicesDueToErrors,
                      true);

  InitSyncService(/*is_password_sync_enabled=*/false);

  migrator()->StartMigrationOfLocalPasswords();
  RunUntilIdle();

  EXPECT_EQ(static_cast<int>(UseUpmLocalAndSeparateStoresState::kOn),
            prefs()->GetInteger(kPasswordsUseUPMLocalAndSeparateStores));
  EXPECT_EQ(
      base::Time::Now().InSecondsFSinceUnixEpoch(),
      prefs()->GetDouble(password_manager::prefs::kTimeOfLastMigrationAttempt));
  EXPECT_TRUE(prefs()->GetBoolean(
      prefs::kShouldShowPostPasswordMigrationSheetAtStartup));
}

TEST_F(BuiltInBackendToAndroidBackendMigratorTest,
       PostMigrationSheetIsNotScheduledForUnenrolledUsers) {
  Init();
  SetPasswordSyncEnabledPref(true);
  prefs()->SetBoolean(prefs::kUnenrolledFromGoogleMobileServicesDueToErrors,
                      true);

  InitSyncService(/*is_password_sync_enabled=*/true);

  migrator()->StartMigrationOfLocalPasswords();
  RunUntilIdle();

  EXPECT_EQ(static_cast<int>(UseUpmLocalAndSeparateStoresState::kOn),
            prefs()->GetInteger(kPasswordsUseUPMLocalAndSeparateStores));
  EXPECT_EQ(
      base::Time::Now().InSecondsFSinceUnixEpoch(),
      prefs()->GetDouble(password_manager::prefs::kTimeOfLastMigrationAttempt));
  EXPECT_FALSE(prefs()->GetBoolean(
      prefs::kShouldShowPostPasswordMigrationSheetAtStartup));
}

TEST_F(BuiltInBackendToAndroidBackendMigratorTest,
       PostMigrationSheetIsNotScheduledForNotMigratedUsers) {
  Init();
  SetPasswordSyncEnabledPref(true);
  prefs()->SetInteger(prefs::kCurrentMigrationVersionToGoogleMobileServices, 0);

  InitSyncService(/*is_password_sync_enabled=*/true);

  migrator()->StartMigrationOfLocalPasswords();
  RunUntilIdle();

  EXPECT_EQ(static_cast<int>(UseUpmLocalAndSeparateStoresState::kOn),
            prefs()->GetInteger(kPasswordsUseUPMLocalAndSeparateStores));
  EXPECT_EQ(
      base::Time::Now().InSecondsFSinceUnixEpoch(),
      prefs()->GetDouble(password_manager::prefs::kTimeOfLastMigrationAttempt));
  EXPECT_FALSE(prefs()->GetBoolean(
      prefs::kShouldShowPostPasswordMigrationSheetAtStartup));
}

TEST_F(BuiltInBackendToAndroidBackendMigratorTest,
       AttemptPrefIsUpdatedAfterInitialMigrationStarted) {
  Init();
  ASSERT_EQ(
      base::Time().InSecondsFSinceUnixEpoch(),
      prefs()->GetDouble(password_manager::prefs::kTimeOfLastMigrationAttempt));

  migrator()->StartAccountMigrationIfNecessary(
      MigrationType::kInitialForSyncUsers);
  RunUntilIdle();

  EXPECT_EQ(
      base::Time::Now().InSecondsFSinceUnixEpoch(),
      prefs()->GetDouble(password_manager::prefs::kTimeOfLastMigrationAttempt));
}

TEST_F(BuiltInBackendToAndroidBackendMigratorTest,
       AttemptPrefIsUpdatedAfterReenrollmentMigrationStarted) {
  Init();
  ASSERT_EQ(
      base::Time().InSecondsFSinceUnixEpoch(),
      prefs()->GetDouble(password_manager::prefs::kTimeOfLastMigrationAttempt));

  migrator()->StartAccountMigrationIfNecessary(
      MigrationType::kReenrollmentAttempt);
  RunUntilIdle();

  EXPECT_EQ(
      base::Time::Now().InSecondsFSinceUnixEpoch(),
      prefs()->GetDouble(password_manager::prefs::kTimeOfLastMigrationAttempt));
}

TEST_F(BuiltInBackendToAndroidBackendMigratorTest,
       AttemptPrefIsUpdatedAfterLocalMigrationStarted) {
  Init();
  ASSERT_EQ(
      base::Time().InSecondsFSinceUnixEpoch(),
      prefs()->GetDouble(password_manager::prefs::kTimeOfLastMigrationAttempt));

  migrator()->StartMigrationOfLocalPasswords();
  RunUntilIdle();

  EXPECT_EQ(
      base::Time::Now().InSecondsFSinceUnixEpoch(),
      prefs()->GetDouble(password_manager::prefs::kTimeOfLastMigrationAttempt));
}

TEST_F(BuiltInBackendToAndroidBackendMigratorTest,
       AttemptPrefIsNotUpdatedAfterNonSyncDataMigrationToAndroidStarted) {
  Init();
  ASSERT_EQ(
      base::Time().InSecondsFSinceUnixEpoch(),
      prefs()->GetDouble(password_manager::prefs::kTimeOfLastMigrationAttempt));

  migrator()->StartAccountMigrationIfNecessary(
      MigrationType::kNonSyncableToAndroidBackend);
  RunUntilIdle();

  EXPECT_EQ(
      base::Time().InSecondsFSinceUnixEpoch(),
      prefs()->GetDouble(password_manager::prefs::kTimeOfLastMigrationAttempt));
}

TEST_F(BuiltInBackendToAndroidBackendMigratorTest,
       AttemptPrefIsNotUpdatedAfterNonSyncDataMigrationToBuiltInStarted) {
  Init();
  ASSERT_EQ(
      base::Time().InSecondsFSinceUnixEpoch(),
      prefs()->GetDouble(password_manager::prefs::kTimeOfLastMigrationAttempt));

  migrator()->StartAccountMigrationIfNecessary(
      MigrationType::kNonSyncableToBuiltInBackend);
  RunUntilIdle();

  EXPECT_EQ(
      base::Time().InSecondsFSinceUnixEpoch(),
      prefs()->GetDouble(password_manager::prefs::kTimeOfLastMigrationAttempt));
}

TEST_F(BuiltInBackendToAndroidBackendMigratorTest,
       PrefsUnchangedWhenAttemptedMigrationEarlierToday) {
  Init();

  prefs()->SetDouble(
      password_manager::prefs::kTimeOfLastMigrationAttempt,
      (base::Time::Now() - base::Hours(2)).InSecondsFSinceUnixEpoch());

  migrator()->StartAccountMigrationIfNecessary(
      MigrationType::kInitialForSyncUsers);
  RunUntilIdle();

  EXPECT_EQ(0, prefs()->GetInteger(
                   prefs::kCurrentMigrationVersionToGoogleMobileServices));
  EXPECT_EQ(
      (base::Time::Now() - base::Hours(2)).InSecondsFSinceUnixEpoch(),
      prefs()->GetDouble(password_manager::prefs::kTimeOfLastMigrationAttempt));
}

TEST_F(BuiltInBackendToAndroidBackendMigratorTest,
       InitialMigrationNeverStartedMetrics) {
  base::HistogramTester histogram_tester;
  const char kMigrationFinishedMetric[] =
      "PasswordManager.UnifiedPasswordManager.WasMigrationDone";
  Init();

  histogram_tester.ExpectTotalCount(kMigrationFinishedMetric, 1);
  histogram_tester.ExpectBucketCount(kMigrationFinishedMetric, false, 1);
}

TEST_F(BuiltInBackendToAndroidBackendMigratorTest,
       InitialMigrationFinishedMetrics) {
  base::HistogramTester histogram_tester;
  const char kMigrationFinishedMetric[] =
      "PasswordManager.UnifiedPasswordManager.WasMigrationDone";

  Init(/*current_migration_version=*/1);

  histogram_tester.ExpectUniqueSample(kMigrationFinishedMetric, true, 1);
}

TEST_F(BuiltInBackendToAndroidBackendMigratorTest,
       MigrationForSyncingUserShouldMoveLocalOnlyDataToAndroidBackend) {
  Init();
  InitSyncService(/*is_password_sync_enabled=*/true);

  PasswordForm form = CreateTestPasswordForm();
  android_backend().AddLoginAsync(form, base::DoNothing());

  // 'skip_zero_click' is a local only field in PasswordForm and hence not
  // available in Android backend before the migration.
  PasswordForm form_with_local_data = form;
  form_with_local_data.skip_zero_click = true;
  built_in_backend().AddLoginAsync(form_with_local_data, base::DoNothing());

  migrator()->StartAccountMigrationIfNecessary(
      MigrationType::kInitialForSyncUsers);
  RunUntilIdle();

  base::MockCallback<LoginsOrErrorReply> mock_reply;
  EXPECT_CALL(
      mock_reply,
      Run(VariantWith<LoginsResult>(ElementsAre(form_with_local_data))));
  android_backend().GetAllLoginsAsync(mock_reply.Get());
  RunUntilIdle();

  EXPECT_FALSE(prefs()->GetBoolean(
      prefs::kShouldShowPostPasswordMigrationSheetAtStartup));
}

TEST_F(BuiltInBackendToAndroidBackendMigratorTest,
       MigrationUserAfterSyncDisablingShouldMoveLocalOnlyDataToBuiltInBackend) {
  Init();

  // Simulate sync being recently disabled.
  prefs()->SetBoolean(prefs::kRequiresMigrationAfterSyncStatusChange, true);
  InitSyncService(/*is_password_sync_enabled=*/false);

  PasswordForm form = CreateTestPasswordForm();
  built_in_backend().AddLoginAsync(form, base::DoNothing());

  // 'skip_zero_click' is a local only field in PasswordForm and hence not
  // available in the built-in backend before the migration.
  PasswordForm form_with_local_data = form;
  form_with_local_data.skip_zero_click = true;
  android_backend().AddLoginAsync(form_with_local_data, base::DoNothing());

  migrator()->StartAccountMigrationIfNecessary(
      MigrationType::kNonSyncableToBuiltInBackend);
  RunUntilIdle();

  base::MockCallback<LoginsOrErrorReply> mock_reply;
  EXPECT_CALL(
      mock_reply,
      Run(VariantWith<LoginsResult>(ElementsAre(form_with_local_data))));
  android_backend().GetAllLoginsAsync(mock_reply.Get());
  RunUntilIdle();

  EXPECT_FALSE(prefs()->GetBoolean(
      prefs::kShouldShowPostPasswordMigrationSheetAtStartup));
  EXPECT_FALSE(
      prefs()->GetBoolean(prefs::kRequiresMigrationAfterSyncStatusChange));
}

// Tests that migration removes blocklisted entries with non-empty username or
// values from the built in backlend before writing to the Android backend.
TEST_F(BuiltInBackendToAndroidBackendMigratorTest,
       MigrationClearsBlocklistedCredentials_SyncOn) {
  Init();
  InitSyncService(/*is_password_sync_enabled=*/true);

  // Add two incorrect entries to the local database to check if they will be
  // removed before writing to the android backend
  PasswordForm form_1 = CreateTestPasswordForm(1);
  form_1.blocked_by_user = true;
  form_1.username_value.clear();
  built_in_backend().AddLoginAsync(form_1, base::DoNothing());

  PasswordForm form_2 = CreateTestPasswordForm(2);
  form_2.blocked_by_user = true;
  form_1.password_value.clear();
  built_in_backend().AddLoginAsync(form_2, base::DoNothing());

  migrator()->StartAccountMigrationIfNecessary(
      MigrationType::kInitialForSyncUsers);
  RunUntilIdle();

  base::MockCallback<LoginsOrErrorReply> mock_reply;
  // Credentials should be cleaned in both android and built in backends.
  EXPECT_CALL(mock_reply, Run(VariantWith<LoginsResult>((IsEmpty())))).Times(2);
  android_backend().GetAllLoginsAsync(mock_reply.Get());
  built_in_backend().GetAllLoginsAsync(mock_reply.Get());
  RunUntilIdle();

  EXPECT_FALSE(prefs()->GetBoolean(
      prefs::kShouldShowPostPasswordMigrationSheetAtStartup));
}

// Tests that migration does not affect username and password for
// non-blocklisted entries.
TEST_F(BuiltInBackendToAndroidBackendMigratorTest,
       MigrationDoesNotClearNonBlocklistedCredentials_SyncOn) {
  Init();
  InitSyncService(/*is_password_sync_enabled=*/true);

  // Add two incorrect entries to the local database to check if they will be
  // fixed before writing to the android backend
  PasswordForm form_1 = CreateTestPasswordForm(1);
  built_in_backend().AddLoginAsync(form_1, base::DoNothing());

  PasswordForm form_2 = CreateTestPasswordForm(2);
  built_in_backend().AddLoginAsync(form_2, base::DoNothing());

  // Add one form to be updated.
  android_backend().AddLoginAsync(form_1, base::DoNothing());
  RunUntilIdle();

  migrator()->StartAccountMigrationIfNecessary(
      MigrationType::kInitialForSyncUsers);
  RunUntilIdle();

  base::MockCallback<LoginsOrErrorReply> mock_reply;
  // Credentials should be cleaned in both android and built in backends.
  EXPECT_CALL(mock_reply,
              Run(VariantWith<LoginsResult>(ElementsAre(form_1, form_2))))
      .Times(2);
  android_backend().GetAllLoginsAsync(mock_reply.Get());
  built_in_backend().GetAllLoginsAsync(mock_reply.Get());
  RunUntilIdle();

  EXPECT_FALSE(prefs()->GetBoolean(
      prefs::kShouldShowPostPasswordMigrationSheetAtStartup));
}

TEST_F(BuiltInBackendToAndroidBackendMigratorTest,
       ReenrollmentAttemptShouldMoveLocalOnlyDataToAndroidBackend) {
  Init();
  InitSyncService(/*is_password_sync_enabled=*/true);

  prefs()->SetBoolean(prefs::kUnenrolledFromGoogleMobileServicesDueToErrors,
                      true);
  const int initial_num_reenrollments =
      prefs()->GetInteger(prefs::kTimesReenrolledToGoogleMobileServices);
  prefs()->SetInteger(prefs::kTimesAttemptedToReenrollToGoogleMobileServices,
                      10);

  PasswordForm form = CreateTestPasswordForm();
  android_backend().AddLoginAsync(form, base::DoNothing());

  // 'skip_zero_click' is a local only field in PasswordForm and hence not
  // available in Android backend before the migration.
  PasswordForm form_with_local_data = form;
  form_with_local_data.skip_zero_click = true;
  built_in_backend().AddLoginAsync(form_with_local_data, base::DoNothing());

  migrator()->StartAccountMigrationIfNecessary(
      MigrationType::kReenrollmentAttempt);
  RunUntilIdle();

  base::MockCallback<LoginsOrErrorReply> mock_reply;
  EXPECT_CALL(
      mock_reply,
      Run(VariantWith<LoginsResult>(ElementsAre(form_with_local_data))));
  android_backend().GetAllLoginsAsync(mock_reply.Get());
  RunUntilIdle();

  // Since the migration has completed successfully, the user should be
  // reenrolled into UPM.
  EXPECT_FALSE(prefs()->GetBoolean(
      prefs::kUnenrolledFromGoogleMobileServicesDueToErrors));
  EXPECT_EQ(prefs()->GetInteger(prefs::kTimesReenrolledToGoogleMobileServices),
            initial_num_reenrollments + 1);
  EXPECT_EQ(prefs()->GetInteger(
                prefs::kTimesAttemptedToReenrollToGoogleMobileServices),
            0);

  EXPECT_FALSE(prefs()->GetBoolean(
      prefs::kShouldShowPostPasswordMigrationSheetAtStartup));
}

// The user was syncing and got unenrolled, but then disabled sync and the
// unenrollment pref wasn't reset.
TEST_F(BuiltInBackendToAndroidBackendMigratorTest,
       UnenrollmentPrefsAreResetOnLocalPasswordMigration) {
  Init();
  prefs()->SetBoolean(prefs::kUnenrolledFromGoogleMobileServicesDueToErrors,
                      true);
  prefs()->SetInteger(
      kPasswordsUseUPMLocalAndSeparateStores,
      static_cast<int>(
          UseUpmLocalAndSeparateStoresState::kOffAndMigrationPending));

  InitSyncService(/*is_password_sync_enabled=*/true);
  migrator()->StartMigrationOfLocalPasswords();
  RunUntilIdle();

  EXPECT_EQ(static_cast<int>(UseUpmLocalAndSeparateStoresState::kOn),
            prefs()->GetInteger(kPasswordsUseUPMLocalAndSeparateStores));
  EXPECT_FALSE(prefs()->GetBoolean(
      prefs::kUnenrolledFromGoogleMobileServicesDueToErrors));
}

class BuiltInBackendToAndroidBackendMigratorTestWithMockedBackends
    : public BuiltInBackendToAndroidBackendMigratorTest {
 protected:
  BuiltInBackendToAndroidBackendMigratorTestWithMockedBackends() = default;
  ~BuiltInBackendToAndroidBackendMigratorTestWithMockedBackends() override =
      default;

  void Init(int current_migration_version = 0) {
    BuiltInBackendToAndroidBackendMigratorTest::Init(current_migration_version);
    CreateMigrator(&built_in_backend_, &android_backend_, prefs());
  }

  MockPasswordStoreBackend built_in_backend_;
  MockPasswordStoreBackend android_backend_;
};

TEST_F(BuiltInBackendToAndroidBackendMigratorTestWithMockedBackends,
       RemoveBlocklistedReturnsWithErrorDoesntCrash) {
  Init();
  InitSyncService(/*is_password_sync_enabled=*/true);

  base::HistogramTester histogram_tester;

  PasswordForm form = CreateTestPasswordForm(1);
  form.blocked_by_user = true;
  std::vector<PasswordForm> built_in_logins = {std::move(form)};
  EXPECT_CALL(built_in_backend_, GetAllLoginsAsync)
      .WillOnce(base::test::RunOnceCallback<0>(std::move(built_in_logins)));
  // Set up `RemoveLoginAsync` to return an error.
  EXPECT_CALL(built_in_backend_, RemoveLoginAsync)
      .WillOnce(base::test::RunOnceCallback<2>(kBackendError));
  migrator()->StartAccountMigrationIfNecessary(
      MigrationType::kInitialForSyncUsers);

  histogram_tester.ExpectUniqueSample(
      "PasswordManager.UnifiedPasswordManager.InitialMigrationForSyncUsers."
      "BuiltInBackend.RemoveLogin.Success",
      false, 1);
}

TEST_F(BuiltInBackendToAndroidBackendMigratorTestWithMockedBackends,
       UpdateLoginMetricReportsSuccess) {
  Init();
  InitSyncService(/*is_password_sync_enabled=*/true);

  base::HistogramTester histogram_tester;

  std::vector<PasswordForm> built_in_logins = {CreateTestPasswordForm(0)};
  EXPECT_CALL(built_in_backend_, GetAllLoginsAsync)
      .WillOnce(base::test::RunOnceCallback<0>(std::move(built_in_logins)));
  EXPECT_CALL(android_backend_, UpdateLoginAsync)
      .WillOnce(base::test::RunOnceCallback<1>(PasswordChangesOrError()));
  migrator()->StartAccountMigrationIfNecessary(
      MigrationType::kInitialForSyncUsers);

  histogram_tester.ExpectUniqueSample(
      "PasswordManager.UnifiedPasswordManager.InitialMigrationForSyncUsers."
      "AndroidBackend."
      "UpdateLogin.Success",
      true, 1);
}

TEST_F(BuiltInBackendToAndroidBackendMigratorTestWithMockedBackends,
       UpdateLoginMetricReportsFailure) {
  Init();
  InitSyncService(/*is_password_sync_enabled=*/true);

  base::HistogramTester histogram_tester;

  std::vector<PasswordForm> built_in_logins = {CreateTestPasswordForm(0)};
  EXPECT_CALL(built_in_backend_, GetAllLoginsAsync)
      .WillOnce(base::test::RunOnceCallback<0>(std::move(built_in_logins)));
  EXPECT_CALL(android_backend_, UpdateLoginAsync)
      .WillOnce(base::test::RunOnceCallback<1>(kBackendError));
  migrator()->StartAccountMigrationIfNecessary(
      MigrationType::kInitialForSyncUsers);

  histogram_tester.ExpectUniqueSample(
      "PasswordManager.UnifiedPasswordManager.InitialMigrationForSyncUsers."
      "AndroidBackend.UpdateLogin.Success",
      false, 1);
}

TEST_F(BuiltInBackendToAndroidBackendMigratorTestWithMockedBackends,
       RemoveLoginMetricReportsSuccess) {
  Init();
  InitSyncService(/*is_password_sync_enabled=*/true);

  base::HistogramTester histogram_tester;

  PasswordForm form_1 = CreateTestPasswordForm(1);
  form_1.blocked_by_user = true;
  std::vector<PasswordForm> built_in_logins = {std::move(form_1)};
  EXPECT_CALL(built_in_backend_, GetAllLoginsAsync)
      .WillOnce(base::test::RunOnceCallback<0>(std::move(built_in_logins)));
  EXPECT_CALL(built_in_backend_, RemoveLoginAsync)
      .WillOnce(base::test::RunOnceCallback<2>(PasswordChangesOrError()));
  migrator()->StartAccountMigrationIfNecessary(
      MigrationType::kInitialForSyncUsers);

  histogram_tester.ExpectUniqueSample(
      "PasswordManager.UnifiedPasswordManager.InitialMigrationForSyncUsers."
      "BuiltInBackend.RemoveLogin.Success",
      true, 1);
}

// Holds the built in and android backend's logins and the expected result after
// the migration.
struct MigrationParam {
  struct Entry {
    Entry(int index,
          std::string password = "",
          base::TimeDelta date_created = base::TimeDelta(),
          base::TimeDelta date_last_used = base::TimeDelta(),
          base::TimeDelta date_password_modified = base::TimeDelta())
        : index(index),
          password(password),
          date_created(date_created),
          date_last_used(date_last_used),
          date_password_modified(date_password_modified) {}

    PasswordForm ToPasswordForm() const {
      PasswordForm form = CreateTestPasswordForm(index);
      form.password_value = base::ASCIIToUTF16(password);
      form.date_created = base::Time() + date_created;
      form.date_last_used = base::Time() + date_last_used;
      form.date_password_modified = base::Time() + date_password_modified;
      return form;
    }

    int index;
    std::string password;
    base::TimeDelta date_created;
    base::TimeDelta date_last_used;
    base::TimeDelta date_password_modified;
  };

  std::vector<PasswordForm> GetBuiltInLogins() const {
    return EntriesToPasswordForms(built_in_logins);
  }

  std::vector<PasswordForm> GetAndroidLogins() const {
    return EntriesToPasswordForms(android_logins);
  }

  std::vector<PasswordForm> GetMergedLogins() const {
    return EntriesToPasswordForms(merged_logins);
  }

  std::vector<PasswordForm> GetAndroidLoginsAfterInitialSyncMigration() const {
    return EntriesToPasswordForms(initial_sync_migration_android_logins);
  }

  std::vector<PasswordForm> EntriesToPasswordForms(
      const std::vector<Entry>& entries) const {
    std::vector<PasswordForm> v;
    base::ranges::transform(entries, std::back_inserter(v),
                            &Entry::ToPasswordForm);
    return v;
  }

  std::vector<Entry> built_in_logins;
  std::vector<Entry> android_logins;
  std::vector<Entry> merged_logins;

  // The initial sync migration updates built-in backend logins in
  // the android backend, since the data is assumed to be the same apart
  // from non-syncable data. However, this means that no merge logic is
  // applies so these might differ from `merged_logins`.
  std::vector<Entry> initial_sync_migration_android_logins;

  int updated_in_android_backend_credentials_count = 0;

  // The local passwords migration uses a merge algorithm for credentials
  // with the same primary key in both backends,but different passwords.
  // The conflicts are resolved in favor or the most recently created/mdoified
  // entry. The conflict is won by the android backend when the most
  // recent credential is stored there.
  int conflicts_won_by_android = 0;
};

// Tests that initial and rolling migration actually works by comparing
// passwords in built-in/android backend before and after migration.
class BuiltInBackendToAndroidBackendMigratorTestWithMigrationParams
    : public BuiltInBackendToAndroidBackendMigratorTest,
      public testing::WithParamInterface<MigrationParam> {};

// Tests the initial migration result.
TEST_P(BuiltInBackendToAndroidBackendMigratorTestWithMigrationParams,
       InitialMigrationForSyncingUsers) {
  BuiltInBackendToAndroidBackendMigratorTest::Init();

  InitSyncService(/*is_password_sync_enabled=*/true);

  const MigrationParam& p = GetParam();

  for (const auto& login : p.GetBuiltInLogins()) {
    built_in_backend().AddLoginAsync(login, base::DoNothing());
  }
  for (const auto& login : p.GetAndroidLogins()) {
    android_backend().AddLoginAsync(login, base::DoNothing());
  }
  RunUntilIdle();

  migrator()->StartAccountMigrationIfNecessary(
      MigrationType::kInitialForSyncUsers);
  RunUntilIdle();

  // The built-in logins should not be affected.
  base::MockCallback<LoginsOrErrorReply> built_in_reply;
  EXPECT_CALL(
      built_in_reply,
      Run(VariantWith<LoginsResult>(ElementsAreArray(p.GetBuiltInLogins()))));
  built_in_backend().GetAllLoginsAsync(built_in_reply.Get());

  // The android logins are updated. Existing logins are retained.
  base::MockCallback<LoginsOrErrorReply> android_reply;
  EXPECT_CALL(android_reply,
              Run(VariantWith<LoginsResult>(ElementsAreArray(
                  p.GetAndroidLoginsAfterInitialSyncMigration()))));
  android_backend().GetAllLoginsAsync(android_reply.Get());
  RunUntilIdle();

  EXPECT_FALSE(prefs()->GetBoolean(
      prefs::kShouldShowPostPasswordMigrationSheetAtStartup));
}

// Tests the initial migration result.
TEST_P(BuiltInBackendToAndroidBackendMigratorTestWithMigrationParams,
       LocalPwdMigrationAfterEnrollingIntoTheExperiment) {
  base::HistogramTester histogram_tester;
  // Set current_migration_version to 0 to imitate a user enrolling into the
  // experiment.
  BuiltInBackendToAndroidBackendMigratorTest::Init(
      /*current_migration_version=*/0);

  InitSyncService(/*is_password_sync_enabled=*/false);

  prefs()->SetInteger(
      password_manager::prefs::kPasswordsUseUPMLocalAndSeparateStores,
      static_cast<int>(
          password_manager::prefs::UseUpmLocalAndSeparateStoresState::
              kOffAndMigrationPending));

  const MigrationParam& p = GetParam();

  for (const auto& login : p.GetBuiltInLogins()) {
    built_in_backend().AddLoginAsync(login, base::DoNothing());
  }
  for (const auto& login : p.GetAndroidLogins()) {
    android_backend().AddLoginAsync(login, base::DoNothing());
  }
  RunUntilIdle();

  migrator()->StartMigrationOfLocalPasswords();
  RunUntilIdle();

  base::MockCallback<LoginsOrErrorReply> mock_reply;
  EXPECT_CALL(
      mock_reply,
      Run(VariantWith<LoginsResult>(ElementsAreArray(p.GetMergedLogins()))));
  android_backend().GetAllLoginsAsync(mock_reply.Get());
  RunUntilIdle();

  // After local passwords migration, the credentials in the built in backend
  // should stay unchanged.
  EXPECT_CALL(
      mock_reply,
      Run(VariantWith<LoginsResult>(ElementsAreArray(p.GetBuiltInLogins()))));
  built_in_backend().GetAllLoginsAsync(mock_reply.Get());
  RunUntilIdle();

  histogram_tester.ExpectUniqueSample(
      "PasswordManager.UnifiedPasswordManager.MigrationForLocalUsers."
      "MergeWhereAndroidHasMostRecent",
      GetParam().conflicts_won_by_android, 1);
}

TEST_P(BuiltInBackendToAndroidBackendMigratorTestWithMigrationParams,
       LocalPasswordsMigrationMetrics) {
  base::HistogramTester histogram_tester;
  BuiltInBackendToAndroidBackendMigratorTest::Init(
      /*current_migration_version=*/0);

  prefs()->SetInteger(
      password_manager::prefs::kPasswordsUseUPMLocalAndSeparateStores,
      static_cast<int>(
          password_manager::prefs::UseUpmLocalAndSeparateStoresState::
              kOffAndMigrationPending));

  const MigrationParam& p = GetParam();
  for (const auto& login : p.GetBuiltInLogins()) {
    built_in_backend().AddLoginAsync(login, base::DoNothing());
  }
  for (const auto& login : p.GetAndroidLogins()) {
    android_backend().AddLoginAsync(login, base::DoNothing());
  }
  RunUntilIdle();

  migrator()->StartMigrationOfLocalPasswords();
  RunUntilIdle();

  int added_to_android_backend_count =
      p.GetMergedLogins().size() - p.GetAndroidLogins().size();
  histogram_tester.ExpectUniqueSample(
      "PasswordManager.UnifiedPasswordManager.MigrationForLocalUsers."
      "AddLoginCount",
      added_to_android_backend_count, 1);
  histogram_tester.ExpectUniqueSample(
      "PasswordManager.UnifiedPasswordManager.MigrationForLocalUsers."
      "UpdateLoginCount",
      p.updated_in_android_backend_credentials_count, 1);
  histogram_tester.ExpectUniqueSample(
      "PasswordManager.UnifiedPasswordManager.MigrationForLocalUsers."
      "MigratedLoginsTotalCount",
      p.updated_in_android_backend_credentials_count +
          added_to_android_backend_count,
      1);
  histogram_tester.ExpectUniqueSample(
      "PasswordManager.UnifiedPasswordManager.MigrationForLocalUsers."
      "AndroidBackend."
      "AddLogin.Success",
      true, added_to_android_backend_count);
  histogram_tester.ExpectUniqueSample(
      "PasswordManager.UnifiedPasswordManager.MigrationForLocalUsers."
      "AndroidBackend."
      "UpdateLogin.Success",
      true, p.updated_in_android_backend_credentials_count);
}

INSTANTIATE_TEST_SUITE_P(
    BuiltInBackendToAndroidBackendMigratorTest,
    BuiltInBackendToAndroidBackendMigratorTestWithMigrationParams,
    testing::Values(
        MigrationParam{.built_in_logins = {},
                       .android_logins = {},
                       .merged_logins = {},
                       .initial_sync_migration_android_logins = {},
                       .conflicts_won_by_android = 0},
        MigrationParam{.built_in_logins = {{1}, {2}},
                       .android_logins = {},
                       .merged_logins = {{1}, {2}},
                       .initial_sync_migration_android_logins = {{1}, {2}},
                       .updated_in_android_backend_credentials_count = 0,
                       .conflicts_won_by_android = 0},
        MigrationParam{.built_in_logins = {},
                       .android_logins = {{1}, {2}},
                       .merged_logins = {{1}, {2}},
                       .initial_sync_migration_android_logins = {{1}, {2}},
                       .conflicts_won_by_android = 0},
        MigrationParam{.built_in_logins = {{1}, {2}},
                       .android_logins = {{3}},
                       .merged_logins = {{1}, {2}, {3}},
                       .initial_sync_migration_android_logins = {{1}, {2}, {3}},
                       .conflicts_won_by_android = 0},
        MigrationParam{.built_in_logins = {{1}, {2}, {3}},
                       .android_logins = {{1}, {2}, {3}},
                       .merged_logins = {{1}, {2}, {3}},
                       .initial_sync_migration_android_logins = {{1}, {2}, {3}},
                       .conflicts_won_by_android = 0},

        MigrationParam{
            .built_in_logins = {{1, "old_password", base::Days(1)}, {2}},
            .android_logins = {{1, "new_password", base::Days(2)}, {3}},
            .merged_logins = {{1, "new_password", base::Days(2)}, {2}, {3}},
            .initial_sync_migration_android_logins =
                {{1, "old_password", base::Days(1)}, {2}, {3}},
            .conflicts_won_by_android = 1},
        MigrationParam{
            .built_in_logins = {{1, "new_password", base::Days(2)}, {2}},
            .android_logins = {{1, "old_password", base::Days(1)}, {3}},
            .merged_logins = {{1, "new_password", base::Days(2)}, {2}, {3}},
            .initial_sync_migration_android_logins =
                {{1, "new_password", base::Days(2)}, {2}, {3}},
            .updated_in_android_backend_credentials_count = 1,
            .conflicts_won_by_android = 0},
        MigrationParam{.built_in_logins = {{1, "new_password",
                                            /*date_created=*/base::Days(1),
                                            /*date_last_used=*/base::Days(2)}},
                       .android_logins = {{1, "old_password",
                                           /*date_created=*/base::Days(1)}},
                       .merged_logins = {{1, "new_password", base::Days(1),
                                          /*date_last_used=*/base::Days(2)}},
                       .initial_sync_migration_android_logins =
                           {{1, "new_password", base::Days(1),
                             /*date_last_used=*/base::Days(2)}},
                       .updated_in_android_backend_credentials_count = 1,
                       .conflicts_won_by_android = 0},
        MigrationParam{
            .built_in_logins = {{1, "old_password",
                                 /*date_created=*/base::Days(1),
                                 /*date_last_used=*/base::Days(2),
                                 /*date_password_modified=*/base::Days(2)}},
            .android_logins = {{1, "new_password",
                                /*date_created=*/base::Days(1),
                                /*date_last_used=*/base::Days(2),
                                /*date_password_modified=*/base::Days(3)}},
            .merged_logins = {{1, "new_password",
                               /*date_created=*/base::Days(1),
                               /*date_last_used=*/base::Days(2),
                               /*date_password_modified=*/base::Days(3)}},
            .initial_sync_migration_android_logins =
                {{1, "old_password",
                  /*date_created=*/base::Days(1),
                  /*date_last_used=*/base::Days(2),
                  /*date_password_modified=*/base::Days(2)}},
            .conflicts_won_by_android = 1}));

struct MigrationParamForMetrics {
  // Whether migration has already happened.
  bool migration_ran_before;
  // Whether password sync is enabled in settings.
  bool is_sync_enabled;
  // Whether migration is required after a change in sync status.
  bool migration_required_after_sync_state_change;
  // Whether migration should complete successfully or not.
  bool is_successful_migration;
  // Expected migration type for metrics recording.
  MigrationType migration_type;

  std::string expected_migration_type() const {
    switch (migration_type) {
      case MigrationType::kInitialForSyncUsers:
        return "InitialMigrationForSyncUsers";
      case MigrationType::kNonSyncableToAndroidBackend:
        return "NonSyncableDataMigrationToAndroidBackend";
      case MigrationType::kNonSyncableToBuiltInBackend:
        return "NonSyncableDataMigrationToBuiltInBackend";
      case MigrationType::kForLocalUsers:
        return "MigrationForLocalUsers";
      case MigrationType::kReenrollmentAttempt:
        return "ReenrollmentAttemptMigration";
      case MigrationType::kNone:
        NOTREACHED_NORETURN();
    }
  }
};

class BuiltInBackendToAndroidBackendMigratorTestMetrics
    : public BuiltInBackendToAndroidBackendMigratorTest,
      public testing::WithParamInterface<MigrationParamForMetrics> {
 protected:
  BuiltInBackendToAndroidBackendMigratorTestMetrics() {
    prefs()->registry()->RegisterIntegerPref(
        prefs::kCurrentMigrationVersionToGoogleMobileServices, 0);
    prefs()->registry()->RegisterDoublePref(prefs::kTimeOfLastMigrationAttempt,
                                            0.0);
    prefs()->registry()->RegisterBooleanPref(
        prefs::kRequiresMigrationAfterSyncStatusChange, false);
    prefs()->registry()->RegisterStringPref(
        ::prefs::kGoogleServicesLastSyncingUsername, "testaccount@gmail.com");
    prefs()->registry()->RegisterBooleanPref(
        prefs::kUnenrolledFromGoogleMobileServicesDueToErrors, false);
    prefs()->registry()->RegisterIntegerPref(
        prefs::kUnenrolledFromGoogleMobileServicesAfterApiErrorCode, 0);
    prefs()->registry()->RegisterIntegerPref(
        prefs::kTimesReenrolledToGoogleMobileServices, 0);
    prefs()->registry()->RegisterIntegerPref(
        prefs::kTimesAttemptedToReenrollToGoogleMobileServices, 0);
    prefs()->registry()->RegisterIntegerPref(
        prefs::kPasswordsUseUPMLocalAndSeparateStores,
        static_cast<int>(
            password_manager::prefs::UseUpmLocalAndSeparateStoresState::kOff));
    prefs()->registry()->RegisterBooleanPref(
        prefs::kShouldShowPostPasswordMigrationSheetAtStartup, false);

    if (GetParam().migration_ran_before) {
      // Setup the pref to indicate that the initial migration has happened
      // already.
      prefs()->SetInteger(prefs::kCurrentMigrationVersionToGoogleMobileServices,
                          1);
    }

    latency_metric_ = "PasswordManager.UnifiedPasswordManager." +
                      GetParam().expected_migration_type() + ".Latency";
    success_metric_ = "PasswordManager.UnifiedPasswordManager." +
                      GetParam().expected_migration_type() + ".Success";

    CreateMigrator(&built_in_backend_, &android_backend_, prefs());

    if (GetParam().migration_required_after_sync_state_change) {
      prefs()->SetBoolean(prefs::kRequiresMigrationAfterSyncStatusChange, true);
    }

    if (GetParam().expected_migration_type() == "ReenrollmentAttempt") {
      prefs()->SetBoolean(prefs::kUnenrolledFromGoogleMobileServicesDueToErrors,
                          true);
    }
  }

  std::string latency_metric_;
  std::string success_metric_;
  ::testing::StrictMock<MockPasswordStoreBackend> built_in_backend_;
  ::testing::StrictMock<MockPasswordStoreBackend> android_backend_;
};

TEST_P(BuiltInBackendToAndroidBackendMigratorTestMetrics,
       MigrationMetricsTest) {
  base::HistogramTester histogram_tester;

  InitSyncService(/*is_password_sync_enabled=*/GetParam().is_sync_enabled);

  auto test_migration_callback = [](LoginsOrErrorReply reply) -> void {
    LoginsResultOrError result = GetParam().is_successful_migration
                                     ? LoginsResultOrError(LoginsResult())
                                     : LoginsResultOrError(kBackendError);
    base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE, base::BindOnce(std::move(reply), std::move(result)),
        kLatencyDelta);
  };

  if (GetParam().expected_migration_type() == "InitialMigrationForSyncUsers" ||
      GetParam().expected_migration_type() ==
          "NonSyncableDataMigrationToAndroidBackend" ||
      GetParam().expected_migration_type() == "ReenrollmentAttemptMigration") {
    EXPECT_CALL(built_in_backend_, GetAllLoginsAsync)
        .WillOnce(WithArg<0>(Invoke(test_migration_callback)));
  } else if (GetParam().expected_migration_type() ==
             "NonSyncableDataMigrationToBuiltInBackend") {
    EXPECT_CALL(android_backend_, GetAllLoginsForAccountAsync)
        .WillOnce(WithArg<1>(Invoke(test_migration_callback)));
  }

  migrator()->StartAccountMigrationIfNecessary(GetParam().migration_type);
  FastForwardBy(kLatencyDelta);

  histogram_tester.ExpectTotalCount(latency_metric_, 1);
  histogram_tester.ExpectTimeBucketCount(latency_metric_, kLatencyDelta, 1);
  histogram_tester.ExpectUniqueSample(success_metric_,
                                      GetParam().is_successful_migration, 1);
}

// TODO(crbug.com/40827496): Add cases for rolling migration and non-syncing
// users or clean up.
INSTANTIATE_TEST_SUITE_P(
    BuiltInBackendToAndroidBackendMigratorTest,
    BuiltInBackendToAndroidBackendMigratorTestMetrics,
    testing::Values(
        // Successful initial migration.
        MigrationParamForMetrics{
            .migration_ran_before = false,
            .is_sync_enabled = true,
            .migration_required_after_sync_state_change = false,
            .is_successful_migration = true,
            .migration_type = MigrationType::kInitialForSyncUsers},
        // Unsuccessful initial migration.
        MigrationParamForMetrics{
            .migration_ran_before = false,
            .is_sync_enabled = true,
            .migration_required_after_sync_state_change = false,
            .is_successful_migration = false,
            .migration_type = MigrationType::kInitialForSyncUsers},
        // Successful non-syncable data migration to the android backend.
        MigrationParamForMetrics{
            .migration_ran_before = true,
            .is_sync_enabled = true,
            .migration_required_after_sync_state_change = true,
            .is_successful_migration = true,
            .migration_type = MigrationType::kNonSyncableToAndroidBackend},
        // Unsuccessful non-syncable data migration to the android backend.
        MigrationParamForMetrics{
            .migration_ran_before = true,
            .is_sync_enabled = true,
            .migration_required_after_sync_state_change = true,
            .is_successful_migration = false,
            .migration_type = MigrationType::kNonSyncableToAndroidBackend},
        // Successful non-syncable data migration to the built-in backend.
        MigrationParamForMetrics{
            .migration_ran_before = true,
            .is_sync_enabled = false,
            .migration_required_after_sync_state_change = true,
            .is_successful_migration = true,
            .migration_type = MigrationType::kNonSyncableToBuiltInBackend},
        // Unsuccessful non-syncable data migration to the built-in backend.
        MigrationParamForMetrics{
            .migration_ran_before = true,
            .is_sync_enabled = false,
            .migration_required_after_sync_state_change = true,
            .is_successful_migration = false,
            .migration_type = MigrationType::kNonSyncableToBuiltInBackend},
        // Successful reenrollment attempt.
        MigrationParamForMetrics{
            .migration_ran_before = true,
            .is_sync_enabled = true,
            .migration_required_after_sync_state_change = false,
            .is_successful_migration = true,
            .migration_type = MigrationType::kReenrollmentAttempt},
        // Unsuccessful reenrollment attempt.
        MigrationParamForMetrics{
            .migration_ran_before = true,
            .is_sync_enabled = true,
            .migration_required_after_sync_state_change = false,
            .is_successful_migration = false,
            .migration_type = MigrationType::kReenrollmentAttempt}));

class BuiltInBackendToAndroidBackendMigratorWithMockAndroidBackendTest
    : public BuiltInBackendToAndroidBackendMigratorTest {
 protected:
  BuiltInBackendToAndroidBackendMigratorWithMockAndroidBackendTest() {
    prefs()->registry()->RegisterIntegerPref(
        prefs::kCurrentMigrationVersionToGoogleMobileServices, 0);
    prefs()->registry()->RegisterDoublePref(prefs::kTimeOfLastMigrationAttempt,
                                            0.0);
    prefs()->registry()->RegisterBooleanPref(
        prefs::kRequiresMigrationAfterSyncStatusChange, false);
    prefs()->registry()->RegisterStringPref(
        ::prefs::kGoogleServicesLastSyncingUsername, "testaccount@gmail.com");
    prefs()->registry()->RegisterIntegerPref(
        prefs::kPasswordsUseUPMLocalAndSeparateStores,
        static_cast<int>(
            password_manager::prefs::UseUpmLocalAndSeparateStoresState::kOff));

    CreateMigrator(&built_in_backend_, &android_backend_, prefs());
  }

  PasswordStoreBackend& built_in_backend() { return built_in_backend_; }

  ::testing::NiceMock<MockPasswordStoreBackend> android_backend_;

 private:
  FakePasswordStoreBackend built_in_backend_;
};

TEST_F(BuiltInBackendToAndroidBackendMigratorWithMockAndroidBackendTest,
       DoesNotCompleteMigrationWhenWritingToAndroidBackendFails_SyncOn) {
  InitSyncService(/*is_password_sync_enabled=*/true);

  // Add two credentials to the built-in backend.
  built_in_backend().AddLoginAsync(CreateTestPasswordForm(/*index=*/1),
                                   base::DoNothing());
  built_in_backend().AddLoginAsync(CreateTestPasswordForm(/*index=*/2),
                                   base::DoNothing());

  // Simulate an Android backend that fails to write.
  ON_CALL(android_backend_, UpdateLoginAsync)
      .WillByDefault(
          WithArg<1>(Invoke([](PasswordChangesOrErrorReply callback) -> void {
            base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
                FROM_HERE, base::BindOnce(std::move(callback), kBackendError));
          })));

  // Once one UpdateLoginAsync() call fails, all consecutive ones will not be
  // executed. Check that exactly one UpdateLoginAsync() is called.
  EXPECT_CALL(android_backend_, UpdateLoginAsync).Times(1);

  migrator()->StartAccountMigrationIfNecessary(
      MigrationType::kInitialForSyncUsers);

  // Migration version is still 0 since migration didn't complete.
  EXPECT_EQ(0, prefs()->GetInteger(
                   prefs::kCurrentMigrationVersionToGoogleMobileServices));
  RunUntilIdle();
}

TEST_F(BuiltInBackendToAndroidBackendMigratorWithMockAndroidBackendTest,
       DoesNotCompleteMigrationWhenWritingToAndroidBackendFails_SyncOff) {
  prefs()->SetInteger(
      password_manager::prefs::kPasswordsUseUPMLocalAndSeparateStores,
      static_cast<int>(
          password_manager::prefs::UseUpmLocalAndSeparateStoresState::
              kOffAndMigrationPending));

  // Sync state doesn't affect this test, run it arbitrarily for non-sync'ing
  // users.
  InitSyncService(/*is_password_sync_enabled=*/false);

  // Add two credentials to the built-in backend.
  built_in_backend().AddLoginAsync(CreateTestPasswordForm(/*index=*/1),
                                   base::DoNothing());
  built_in_backend().AddLoginAsync(CreateTestPasswordForm(/*index=*/2),
                                   base::DoNothing());

  // Simulate an empty Android backend.
  EXPECT_CALL(android_backend_, GetAllLoginsAsync)
      .WillOnce(WithArg<0>(Invoke([](LoginsOrErrorReply reply) -> void {
        base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
            FROM_HERE, base::BindOnce(std::move(reply), LoginsResult()));
      })));

  // Simulate an Android backend that fails to write.
  ON_CALL(android_backend_, AddLoginAsync)
      .WillByDefault(
          WithArg<1>(Invoke([](PasswordChangesOrErrorReply callback) -> void {
            base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
                FROM_HERE, base::BindOnce(std::move(callback), kBackendError));
          })));

  // Once one AddLoginAsync() call fails, all consecutive ones will not be
  // executed. Check that exactly one AddLoginAsync() is called.
  EXPECT_CALL(android_backend_, AddLoginAsync).Times(1);

  migrator()->StartMigrationOfLocalPasswords();

  // Local migration should still be pending since it didn' t complete
  // successfully.
  EXPECT_EQ(static_cast<int>(
                UseUpmLocalAndSeparateStoresState::kOffAndMigrationPending),
            prefs()->GetInteger(kPasswordsUseUPMLocalAndSeparateStores));

  RunUntilIdle();
}

TEST_F(BuiltInBackendToAndroidBackendMigratorWithMockAndroidBackendTest,
       SecondMigrationCannotStartWhileTheFirstOneHasNotCompleted_SyncOn) {
  InitSyncService(/*is_password_sync_enabled=*/true);

  // Add a form to the built-in backend to have something to migrate.
  PasswordForm form = CreateTestPasswordForm();
  built_in_backend().AddLoginAsync(form, base::DoNothing());

  // Call StartAccountMigrationIfNecessary for the first time.
  migrator()->StartAccountMigrationIfNecessary(
      MigrationType::kInitialForSyncUsers);
  RunUntilIdle();

  // If the user gets evicted from the experiment, migration-related prefs are
  // cleared.
  prefs()->ClearPref(password_manager::prefs::kTimeOfLastMigrationAttempt);

  // Simulate some time passing before the second migration is triggered.
  FastForwardBy(base::Milliseconds(123u));

  // Call StartAccountMigrationIfNecessary for the second time before the first
  // migration finishes in an attempt to reenroll.
  migrator()->StartAccountMigrationIfNecessary(
      MigrationType::kReenrollmentAttempt);
  RunUntilIdle();

  // Check the recorded last migration attempt time. It should not be recorded
  // after the pref was cleared, because the second migration should not be
  // triggered.
  EXPECT_EQ(0, prefs()->GetDouble(
                   password_manager::prefs::kTimeOfLastMigrationAttempt));
}

TEST_F(BuiltInBackendToAndroidBackendMigratorWithMockAndroidBackendTest,
       NonSyncDataToBuiltInBackendDoesNotWriteInitialUPMMigrationPref) {
  InitSyncService(/*is_password_sync_enabled=*/false);

  // Add a form to the built-in backend to have something to migrate.
  PasswordForm form = CreateTestPasswordForm();
  built_in_backend().AddLoginAsync(form, base::DoNothing());
  ON_CALL(android_backend_, GetAllLoginsForAccountAsync)
      .WillByDefault(WithArg<1>(Invoke([&](LoginsOrErrorReply reply) -> void {
        base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
            FROM_HERE,
            base::BindOnce(std::move(reply), std::vector<PasswordForm>()));
      })));

  migrator()->StartAccountMigrationIfNecessary(
      MigrationType::kNonSyncableToBuiltInBackend);
  EXPECT_EQ(MigrationType::kNonSyncableToBuiltInBackend,
            migrator()->migration_in_progress_type());
  RunUntilIdle();

  // Migration finished but initial migration version is still 0.
  EXPECT_EQ(MigrationType::kNone, migrator()->migration_in_progress_type());
  EXPECT_EQ(0, prefs()->GetInteger(
                   prefs::kCurrentMigrationVersionToGoogleMobileServices));
}

TEST_F(BuiltInBackendToAndroidBackendMigratorWithMockAndroidBackendTest,
       LocalPasswordsMigrationMetricsWithErrorDuringMigration) {
  // Sets up the backends mocks in the way:
  // - built in backend has 2 passwords;
  // - android backend has no passwords;
  // - android backend will return an error when trying to add a second built in
  // password;
  std::vector<PasswordForm> built_in_passwords = {CreateTestPasswordForm(0),
                                                  CreateTestPasswordForm(1)};
  for (PasswordForm& form : built_in_passwords) {
    built_in_backend().AddLoginAsync(form, base::DoNothing());
  }
  RunUntilIdle();
  ON_CALL(android_backend_, GetAllLoginsAsync)
      .WillByDefault(WithArg<0>(Invoke([&](LoginsOrErrorReply reply) -> void {
        base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
            FROM_HERE,
            base::BindOnce(std::move(reply), std::vector<PasswordForm>()));
      })));
  ON_CALL(android_backend_, AddLoginAsync(built_in_passwords[1], testing::_))
      .WillByDefault(
          WithArg<1>(Invoke([&](PasswordChangesOrErrorReply reply) -> void {
            base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
                FROM_HERE,
                base::BindOnce(std::move(reply), PasswordChangesOrError()));
          })));
  ON_CALL(android_backend_, AddLoginAsync(built_in_passwords[0], testing::_))
      .WillByDefault(
          WithArg<1>(Invoke([&](PasswordChangesOrErrorReply reply) -> void {
            base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
                FROM_HERE, base::BindOnce(std::move(reply), kBackendError));
          })));

  prefs()->SetInteger(
      password_manager::prefs::kPasswordsUseUPMLocalAndSeparateStores,
      static_cast<int>(
          password_manager::prefs::UseUpmLocalAndSeparateStoresState::
              kOffAndMigrationPending));

  base::HistogramTester histogram_tester;
  migrator()->StartMigrationOfLocalPasswords();
  RunUntilIdle();

  histogram_tester.ExpectUniqueSample(
      "PasswordManager.UnifiedPasswordManager.MigrationForLocalUsers."
      "AddLoginCount",
      1, 1);
  histogram_tester.ExpectUniqueSample(
      "PasswordManager.UnifiedPasswordManager.MigrationForLocalUsers."
      "UpdateLoginCount",
      0, 1);
  histogram_tester.ExpectUniqueSample(
      "PasswordManager.UnifiedPasswordManager.MigrationForLocalUsers."
      "MigratedLoginsTotalCount",
      1, 1);
}

}  // namespace password_manager
