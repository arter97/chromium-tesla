// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/birch/birch_model.h"

#include <optional>

#include "ash/birch/birch_data_provider.h"
#include "ash/birch/birch_item.h"
#include "ash/birch/birch_item_remover.h"
#include "ash/constants/ash_features.h"
#include "ash/constants/ash_pref_names.h"
#include "ash/constants/geolocation_access_level.h"
#include "ash/public/cpp/ambient/ambient_backend_controller.h"
#include "ash/public/cpp/ambient/fake_ambient_backend_controller_impl.h"
#include "ash/public/cpp/test/test_image_downloader.h"
#include "ash/shell.h"
#include "ash/test/ash_test_base.h"
#include "base/files/scoped_temp_dir.h"
#include "base/strings/string_number_conversions.h"
#include "base/test/metrics/histogram_tester.h"
#include "base/test/scoped_feature_list.h"
#include "base/test/simple_test_clock.h"
#include "chromeos/ash/components/geolocation/simple_geolocation_provider.h"
#include "components/prefs/pref_service.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ash {

namespace {

std::vector<BirchFileItem> MakeFileItemList(int item_count) {
  std::vector<BirchFileItem> file_item_list;
  for (int i = 0; i < item_count; i++) {
    file_item_list.emplace_back(
        base::FilePath("test path " + base::NumberToString(i)), u"suggestion",
        base::Time(), "file_id_" + base::NumberToString(i), "icon_url");
  }
  return file_item_list;
}

std::vector<BirchCalendarItem> MakeCalendarItemList(int event_count) {
  std::vector<BirchCalendarItem> calendar_item_list;
  for (int i = 0; i < event_count; i++) {
    calendar_item_list.emplace_back(
        /*title=*/u"Event " + base::NumberToString16(i),
        /*start_time=*/base::Time(),
        /*end_time=*/base::Time(),
        /*calendar_url=*/GURL(),
        /*conference_url=*/GURL(),
        /*event_id=*/"event_id_" + base::NumberToString(i),
        /*all_day_event=*/false);
  }
  return calendar_item_list;
}

std::vector<BirchAttachmentItem> MakeAttachmentItemList(int item_count) {
  std::vector<BirchAttachmentItem> attachment_item_list;
  for (int i = 0; i < item_count; i++) {
    attachment_item_list.emplace_back(
        u"Attachment " + base::NumberToString16(i),
        /*file_url=*/GURL(),
        /*icon_url=*/GURL(),
        /*start_time=*/base::Time(),
        /*end_time=*/base::Time(),
        /*file_id=*/"file_id" + base::NumberToString(i));
  }
  return attachment_item_list;
}

// A data provider that does nothing.
class StubBirchDataProvider : public BirchDataProvider {
 public:
  StubBirchDataProvider() = default;
  ~StubBirchDataProvider() override = default;

  // BirchDataProvider:
  void RequestBirchDataFetch() override {
    did_request_birch_data_fetch_ = true;
  }

  bool did_request_birch_data_fetch_ = false;
};

// A BirchClient that returns data providers that do nothing.
class StubBirchClient : public BirchClient {
 public:
  StubBirchClient() {
    EXPECT_TRUE(test_dir_.CreateUniqueTempDir());
    if (features::IsBirchWeatherV2Enabled()) {
      weather_provider_ = std::make_unique<StubBirchDataProvider>();
    }
  }
  ~StubBirchClient() override = default;

  // BirchClient:
  BirchDataProvider* GetCalendarProvider() override {
    return &calendar_provider_;
  }
  BirchDataProvider* GetFileSuggestProvider() override {
    return &file_suggest_provider_;
  }
  BirchDataProvider* GetRecentTabsProvider() override {
    return &recent_tabs_provider_;
  }
  BirchDataProvider* GetLastActiveProvider() override {
    return &last_active_provider_;
  }
  BirchDataProvider* GetMostVisitedProvider() override {
    return &most_visited_provider_;
  }
  BirchDataProvider* GetSelfShareProvider() override {
    return &self_share_provider_;
  }
  BirchDataProvider* GetReleaseNotesProvider() override {
    return &release_notes_provider_;
  }
  BirchDataProvider* GetWeatherV2Provider() override {
    return weather_provider_.get();
  }
  void WaitForRefreshTokens(base::OnceClosure callback) override {
    std::move(callback).Run();
  }
  base::FilePath GetRemovedItemsFilePath() override {
    return test_dir_.GetPath();
  }

  StubBirchDataProvider calendar_provider_;
  StubBirchDataProvider file_suggest_provider_;
  StubBirchDataProvider recent_tabs_provider_;
  StubBirchDataProvider last_active_provider_;
  StubBirchDataProvider most_visited_provider_;
  StubBirchDataProvider self_share_provider_;
  StubBirchDataProvider release_notes_provider_;
  std::unique_ptr<StubBirchDataProvider> weather_provider_;

  base::ScopedTempDir test_dir_;
};

class TestModelConsumer {
 public:
  void OnItemsReady(const std::string& id) {
    items_ready_responses_.push_back(id);
  }

  const std::vector<std::string> items_ready_responses() const {
    return items_ready_responses_;
  }

 private:
  std::vector<std::string> items_ready_responses_;
};

base::Time TimeFromString(const char* time_string) {
  base::Time time;
  CHECK(base::Time::FromString(time_string, &time));
  return time;
}

class TestModelObserver : public BirchModel::Observer {
 public:
  TestModelObserver() { Shell::Get()->birch_model()->AddObserver(this); }
  ~TestModelObserver() override {
    Shell::Get()->birch_model()->RemoveObserver(this);
  }

  void OnBirchClientSet() override { birch_client_set_ = true; }

  bool birch_client_set() const { return birch_client_set_; }

 private:
  bool birch_client_set_ = false;
};

}  // namespace

class BirchModelTest : public AshTestBase {
 public:
  BirchModelTest()
      : AshTestBase(base::test::TaskEnvironment::TimeSource::MOCK_TIME) {
    feature_list_.InitWithFeatures(
        {features::kForestFeature, features::kBirchWeather}, {});
  }

  void SetUp() override {
    AshTestBase::SetUp();
    // Inject no-op, stub weather provider to prevent real implementation from
    // returning empty weather info.
    Shell::Get()->birch_model()->OverrideWeatherProviderForTest(
        std::make_unique<StubBirchDataProvider>());
    Shell::Get()->birch_model()->SetClientAndInit(&stub_birch_client_);
    base::RunLoop run_loop;
    Shell::Get()
        ->birch_model()
        ->GetItemRemoverForTest()
        ->SetProtoInitCallbackForTest(run_loop.QuitClosure());
    run_loop.Run();

    // Set a test clock so that ranking uses a consistent time across test runs.
    test_clock_.SetNow(TimeFromString("22 Feb 2024 4:00 UTC"));
    Shell::Get()->birch_model()->OverrideClockForTest(&test_clock_);
  }

  void TearDown() override {
    Shell::Get()->birch_model()->SetClientAndInit(nullptr);
    AshTestBase::TearDown();
  }

  void RecordProviderHiddenHistograms() {
    Shell::Get()->birch_model()->RecordProviderHiddenHistograms();
  }

 protected:
  base::test::ScopedFeatureList feature_list_;
  StubBirchClient stub_birch_client_;
  base::SimpleTestClock test_clock_;
};

class BirchModelWithoutWeatherTest : public AshTestBase {
 public:
  BirchModelWithoutWeatherTest()
      : AshTestBase(base::test::TaskEnvironment::TimeSource::MOCK_TIME) {
    feature_list_.InitWithFeatures({features::kForestFeature},
                                   {features::kBirchWeather});
  }
  void SetUp() override {
    AshTestBase::SetUp();
    Shell::Get()->birch_model()->SetClientAndInit(&stub_birch_client_);
    base::RunLoop run_loop;
    Shell::Get()
        ->birch_model()
        ->GetItemRemoverForTest()
        ->SetProtoInitCallbackForTest(run_loop.QuitClosure());
    run_loop.Run();
  }

  void TearDown() override {
    Shell::Get()->birch_model()->SetClientAndInit(nullptr);
    AshTestBase::TearDown();
  }

 protected:
  base::test::ScopedFeatureList feature_list_;
  StubBirchClient stub_birch_client_;
};

// Test that requesting data and adding all fresh items to the model will run
// the callback.
TEST_F(BirchModelTest, AddItemNotifiesCallback) {
  BirchModel* model = Shell::Get()->birch_model();
  TestModelConsumer consumer;
  EXPECT_TRUE(model);

  // Setting items in the model does not notify when no request has occurred.
  model->SetCalendarItems(std::vector<BirchCalendarItem>());
  model->SetAttachmentItems(std::vector<BirchAttachmentItem>());
  model->SetRecentTabItems(std::vector<BirchTabItem>());
  model->SetLastActiveItems(std::vector<BirchLastActiveItem>());
  model->SetMostVisitedItems(std::vector<BirchMostVisitedItem>());
  model->SetSelfShareItems(std::vector<BirchSelfShareItem>());
  model->SetFileSuggestItems(std::vector<BirchFileItem>());
  model->SetReleaseNotesItems(std::vector<BirchReleaseNotesItem>());
  EXPECT_THAT(consumer.items_ready_responses(), testing::IsEmpty());

  // Make a data fetch request and set fresh tab data.
  model->RequestBirchDataFetch(/*is_post_login=*/false,
                               base::BindOnce(&TestModelConsumer::OnItemsReady,
                                              base::Unretained(&consumer),
                                              /*id=*/"0"));
  model->SetRecentTabItems(std::vector<BirchTabItem>());
  model->SetLastActiveItems(std::vector<BirchLastActiveItem>());
  model->SetMostVisitedItems(std::vector<BirchMostVisitedItem>());
  model->SetSelfShareItems(std::vector<BirchSelfShareItem>());

  // Consumer is not notified until all data sources have responded.
  EXPECT_THAT(consumer.items_ready_responses(), testing::IsEmpty());

  model->SetFileSuggestItems(MakeFileItemList(/*item_count=*/1));
  model->SetWeatherItems({});
  model->SetCalendarItems({});
  model->SetAttachmentItems({});
  model->SetReleaseNotesItems({});

  // Adding file items sets all data as fresh, notifying consumers.
  EXPECT_THAT(consumer.items_ready_responses(), testing::ElementsAre("0"));

  // Setting the file suggest items should not trigger items ready again, since
  // no data fetch was requested.
  model->SetFileSuggestItems(MakeFileItemList(/*item_count=*/2));
  EXPECT_THAT(consumer.items_ready_responses(), testing::ElementsAre("0"));

  // Request another data fetch and expect the consumer to be notified once
  // items are set again.
  model->RequestBirchDataFetch(/*is_post_login=*/false,
                               base::BindOnce(&TestModelConsumer::OnItemsReady,
                                              base::Unretained(&consumer),
                                              /*id=*/"1"));
  model->SetRecentTabItems(std::vector<BirchTabItem>());
  model->SetLastActiveItems(std::vector<BirchLastActiveItem>());
  model->SetMostVisitedItems(std::vector<BirchMostVisitedItem>());
  model->SetSelfShareItems(std::vector<BirchSelfShareItem>());
  model->SetFileSuggestItems(MakeFileItemList(/*item_count=*/2));
  model->SetWeatherItems({});
  model->SetCalendarItems({});
  model->SetAttachmentItems({});
  model->SetReleaseNotesItems({});

  EXPECT_THAT(consumer.items_ready_responses(), testing::ElementsAre("0", "1"));
}

TEST_F(BirchModelTest, RequestBirchDataFetchRecordsHistograms) {
  base::HistogramTester histograms;
  BirchModel* model = Shell::Get()->birch_model();
  TestModelConsumer consumer;
  EXPECT_TRUE(model);

  // Make a data fetch request.
  model->RequestBirchDataFetch(/*is_post_login=*/false,
                               base::BindOnce(&TestModelConsumer::OnItemsReady,
                                              base::Unretained(&consumer),
                                              /*id=*/"0"));

  // Simulate each data provider replying.
  model->SetCalendarItems({});
  model->SetAttachmentItems({});
  model->SetRecentTabItems({});
  model->SetLastActiveItems({});
  model->SetMostVisitedItems({});
  model->SetSelfShareItems({});
  model->SetFileSuggestItems({});
  model->SetWeatherItems({});
  model->SetReleaseNotesItems({});

  // Callback is called.
  EXPECT_THAT(consumer.items_ready_responses(), testing::ElementsAre("0"));

  // Histograms were recorded for each type.
  histograms.ExpectTotalCount("Ash.Birch.Latency.Calendar", 1);
  histograms.ExpectTotalCount("Ash.Birch.Latency.File", 1);
  histograms.ExpectTotalCount("Ash.Birch.Latency.Tab", 1);
  histograms.ExpectTotalCount("Ash.Birch.Latency.LastActive", 1);
  histograms.ExpectTotalCount("Ash.Birch.Latency.MostVisited", 1);
  histograms.ExpectTotalCount("Ash.Birch.Latency.SelfShare", 1);
  histograms.ExpectTotalCount("Ash.Birch.Latency.Weather", 1);
  histograms.ExpectTotalCount("Ash.Birch.Latency.ReleaseNotes", 1);

  // Total latency was recorded.
  histograms.ExpectTotalCount("Ash.Birch.TotalLatency", 1);

  // Simulate a data provider replying outside of a fetch.
  model->SetFileSuggestItems({});

  // Histograms didn't change.
  histograms.ExpectTotalCount("Ash.Birch.Latency.File", 1);
  histograms.ExpectTotalCount("Ash.Birch.TotalLatency", 1);
}

TEST_F(BirchModelTest, RequestBirchDataFetchRecordsTotalLatencyHistogram) {
  base::HistogramTester histograms;
  BirchModel* model = Shell::Get()->birch_model();

  // Make a data fetch request for post-login.
  model->RequestBirchDataFetch(/*is_post_login=*/true, base::DoNothing());

  // Simulate each data provider replying.
  model->SetCalendarItems({});
  model->SetAttachmentItems({});
  model->SetRecentTabItems({});
  model->SetLastActiveItems({});
  model->SetMostVisitedItems({});
  model->SetSelfShareItems({});
  model->SetFileSuggestItems({});
  model->SetWeatherItems({});
  model->SetReleaseNotesItems({});

  // Total latency post login was recorded.
  histograms.ExpectTotalCount("Ash.Birch.TotalLatencyPostLogin", 1);

  // Make a data fetch request for non-post-login.
  model->RequestBirchDataFetch(/*is_post_login=*/false, base::DoNothing());

  // Simulate each data provider replying.
  model->SetCalendarItems({});
  model->SetAttachmentItems({});
  model->SetRecentTabItems({});
  model->SetLastActiveItems({});
  model->SetMostVisitedItems({});
  model->SetSelfShareItems({});
  model->SetFileSuggestItems({});
  model->SetWeatherItems({});
  model->SetReleaseNotesItems({});

  // Regular latency histogram was recorded.
  histograms.ExpectTotalCount("Ash.Birch.TotalLatency", 1);
}

TEST_F(BirchModelTest, DataFetchForNonPrimaryUserClearsModel) {
  BirchModel* model = Shell::Get()->birch_model();
  TestModelConsumer consumer;

  // Sign in to a secondary user.
  SimulateUserLogin("user2@test.com");
  ASSERT_FALSE(Shell::Get()->session_controller()->IsUserPrimary());

  // Add an item to the model.
  model->SetFileSuggestItems(MakeFileItemList(/*item_count=*/1));

  // Request a data fetch.
  model->RequestBirchDataFetch(/*is_post_login=*/false,
                               base::BindOnce(&TestModelConsumer::OnItemsReady,
                                              base::Unretained(&consumer),
                                              /*id=*/"0"));
  // The fetch callback was called.
  EXPECT_THAT(consumer.items_ready_responses(), testing::ElementsAre("0"));

  // The model is empty.
  EXPECT_TRUE(model->GetAllItems().empty());
}

TEST_F(BirchModelTest, DisablingAllPrefsCausesNoFetch) {
  BirchModel* model = Shell::Get()->birch_model();
  TestModelConsumer consumer;

  // Set all the data types so the data is considered fresh.
  model->SetCalendarItems({});
  model->SetAttachmentItems({});
  model->SetFileSuggestItems({});
  model->SetRecentTabItems({});
  model->SetLastActiveItems({});
  model->SetMostVisitedItems({});
  model->SetSelfShareItems({});
  model->SetWeatherItems({});
  model->SetReleaseNotesItems({});
  ASSERT_TRUE(model->IsDataFresh());

  // Disable all the prefs.
  PrefService* prefs =
      Shell::Get()->session_controller()->GetPrimaryUserPrefService();
  ASSERT_TRUE(prefs);
  prefs->SetBoolean(prefs::kBirchUseCalendar, false);
  prefs->SetBoolean(prefs::kBirchUseFileSuggest, false);
  prefs->SetBoolean(prefs::kBirchUseRecentTabs, false);
  prefs->SetBoolean(prefs::kBirchUseLastActive, false);
  prefs->SetBoolean(prefs::kBirchUseMostVisited, false);
  prefs->SetBoolean(prefs::kBirchUseSelfShare, false);
  prefs->SetBoolean(prefs::kBirchUseReleaseNotes, false);
  prefs->SetBoolean(prefs::kBirchUseWeather, false);

  // Install a stub weather provider.
  auto weather_provider = std::make_unique<StubBirchDataProvider>();
  auto* weather_provider_ptr = weather_provider.get();
  model->OverrideWeatherProviderForTest(std::move(weather_provider));

  // Request a data fetch.
  model->RequestBirchDataFetch(/*is_post_login=*/false,
                               base::BindOnce(&TestModelConsumer::OnItemsReady,
                                              base::Unretained(&consumer),
                                              /*id=*/"0"));

  // The fetch callback was called immediately because nothing was fetched.
  EXPECT_THAT(consumer.items_ready_responses(), testing::ElementsAre("0"));

  // Nothing was fetched and the (empty) data is still fresh.
  auto& client = stub_birch_client_;
  EXPECT_FALSE(client.calendar_provider_.did_request_birch_data_fetch_);
  EXPECT_FALSE(client.file_suggest_provider_.did_request_birch_data_fetch_);
  EXPECT_FALSE(client.recent_tabs_provider_.did_request_birch_data_fetch_);
  EXPECT_FALSE(client.last_active_provider_.did_request_birch_data_fetch_);
  EXPECT_FALSE(client.most_visited_provider_.did_request_birch_data_fetch_);
  EXPECT_FALSE(client.self_share_provider_.did_request_birch_data_fetch_);
  EXPECT_FALSE(client.release_notes_provider_.did_request_birch_data_fetch_);
  EXPECT_FALSE(weather_provider_ptr->did_request_birch_data_fetch_);
  EXPECT_TRUE(model->IsDataFresh());
}

TEST_F(BirchModelTest, EnablingOnePrefsCausesFetch) {
  BirchModel* model = Shell::Get()->birch_model();

  // Disable all the prefs except calendar.
  PrefService* prefs =
      Shell::Get()->session_controller()->GetPrimaryUserPrefService();
  ASSERT_TRUE(prefs);
  prefs->SetBoolean(prefs::kBirchUseCalendar, true);
  prefs->SetBoolean(prefs::kBirchUseFileSuggest, false);
  prefs->SetBoolean(prefs::kBirchUseRecentTabs, false);
  prefs->SetBoolean(prefs::kBirchUseLastActive, false);
  prefs->SetBoolean(prefs::kBirchUseMostVisited, false);
  prefs->SetBoolean(prefs::kBirchUseSelfShare, false);
  prefs->SetBoolean(prefs::kBirchUseReleaseNotes, false);
  prefs->SetBoolean(prefs::kBirchUseWeather, false);

  // Install a stub weather provider.
  auto weather_provider = std::make_unique<StubBirchDataProvider>();
  auto* weather_provider_ptr = weather_provider.get();
  model->OverrideWeatherProviderForTest(std::move(weather_provider));

  // Request a fetch.
  model->RequestBirchDataFetch(/*is_post_login=*/false, base::DoNothing());

  // Only calendar was fetched.
  auto& client = stub_birch_client_;
  EXPECT_TRUE(client.calendar_provider_.did_request_birch_data_fetch_);
  EXPECT_FALSE(client.file_suggest_provider_.did_request_birch_data_fetch_);
  EXPECT_FALSE(client.recent_tabs_provider_.did_request_birch_data_fetch_);
  EXPECT_FALSE(client.last_active_provider_.did_request_birch_data_fetch_);
  EXPECT_FALSE(client.most_visited_provider_.did_request_birch_data_fetch_);
  EXPECT_FALSE(client.self_share_provider_.did_request_birch_data_fetch_);
  EXPECT_FALSE(client.release_notes_provider_.did_request_birch_data_fetch_);
  EXPECT_FALSE(weather_provider_ptr->did_request_birch_data_fetch_);
}

TEST_F(BirchModelTest, DisablingPrefsClearsModel) {
  BirchModel* model = Shell::Get()->birch_model();

  // Populate the model with every data type.
  model->SetCalendarItems(MakeCalendarItemList(/*event_count=*/1));
  model->SetAttachmentItems(MakeAttachmentItemList(/*item_count=*/1));
  model->SetFileSuggestItems(MakeFileItemList(/*item_count=*/1));
  std::vector<BirchTabItem> tab_item_list;
  tab_item_list.emplace_back(u"tab", GURL("https://www.example.com/"),
                             base::Time(), GURL("https://www.favicon.com/"),
                             "session",
                             BirchTabItem::DeviceFormFactor::kDesktop);
  model->SetRecentTabItems(std::move(tab_item_list));
  std::vector<BirchLastActiveItem> last_active_list;
  last_active_list.emplace_back(u"active", GURL("https://yahoo.com/"),
                                base::Time(), ui::ImageModel());
  model->SetLastActiveItems(std::move(last_active_list));
  std::vector<BirchMostVisitedItem> most_visited_list;
  most_visited_list.emplace_back(u"visited", GURL("https://google.com/"),
                                 ui::ImageModel());
  model->SetMostVisitedItems(std::move(most_visited_list));
  std::vector<BirchSelfShareItem> self_share_item_list;
  GURL faviconUrl = GURL("https://www.favicon.com/");
  self_share_item_list.emplace_back(
      u"self share guid", u"self share tab", GURL("https://www.example.com/"),
      base::Time(), u"my device", faviconUrl, base::DoNothing());
  model->SetSelfShareItems(std::move(self_share_item_list));
  std::vector<BirchWeatherItem> weather_item_list;
  weather_item_list.emplace_back(u"cloudy", 70.f, ui::ImageModel());
  model->SetWeatherItems(std::move(weather_item_list));
  std::vector<BirchReleaseNotesItem> release_notes_item_list;
  release_notes_item_list.emplace_back(
      u"note", u"explore", GURL("https://www.example.com/"), base::Time());
  model->SetReleaseNotesItems(release_notes_item_list);
  ASSERT_TRUE(model->IsDataFresh());

  // Disable all the prefs for data providers.
  PrefService* prefs =
      Shell::Get()->session_controller()->GetPrimaryUserPrefService();
  ASSERT_TRUE(prefs);
  prefs->SetBoolean(prefs::kBirchUseCalendar, false);
  prefs->SetBoolean(prefs::kBirchUseFileSuggest, false);
  prefs->SetBoolean(prefs::kBirchUseRecentTabs, false);
  prefs->SetBoolean(prefs::kBirchUseLastActive, false);
  prefs->SetBoolean(prefs::kBirchUseMostVisited, false);
  prefs->SetBoolean(prefs::kBirchUseSelfShare, false);
  prefs->SetBoolean(prefs::kBirchUseReleaseNotes, false);
  prefs->SetBoolean(prefs::kBirchUseWeather, false);

  // The model is now empty.
  EXPECT_TRUE(model->GetAllItems().empty());
  EXPECT_TRUE(model->GetCalendarItemsForTest().empty());
  EXPECT_TRUE(model->GetAttachmentItemsForTest().empty());
  EXPECT_TRUE(model->GetFileSuggestItemsForTest().empty());
  EXPECT_TRUE(model->GetTabsForTest().empty());
  EXPECT_TRUE(model->GetLastActiveItemsForTest().empty());
  EXPECT_TRUE(model->GetMostVisitedItemsForTest().empty());
  EXPECT_TRUE(model->GetSelfShareItemsForTest().empty());
  EXPECT_TRUE(model->GetWeatherForTest().empty());
  EXPECT_TRUE(model->GetReleaseNotesItemsForTest().empty());
}

TEST_F(BirchModelTest, DisablingPrefsMarksDataFresh) {
  BirchModel* model = Shell::Get()->birch_model();
  ASSERT_FALSE(model->IsDataFresh());

  // Disable all the prefs for data providers.
  PrefService* prefs =
      Shell::Get()->session_controller()->GetPrimaryUserPrefService();
  ASSERT_TRUE(prefs);
  prefs->SetBoolean(prefs::kBirchUseCalendar, false);
  prefs->SetBoolean(prefs::kBirchUseFileSuggest, false);
  prefs->SetBoolean(prefs::kBirchUseRecentTabs, false);
  prefs->SetBoolean(prefs::kBirchUseLastActive, false);
  prefs->SetBoolean(prefs::kBirchUseMostVisited, false);
  prefs->SetBoolean(prefs::kBirchUseSelfShare, false);
  prefs->SetBoolean(prefs::kBirchUseReleaseNotes, false);
  prefs->SetBoolean(prefs::kBirchUseWeather, false);

  // The data is reported as fresh.
  EXPECT_TRUE(model->IsDataFresh());
}

TEST_F(BirchModelTest, FetchWithOnePrefDisabledMarksDataFresh) {
  BirchModel* model = Shell::Get()->birch_model();
  TestModelConsumer consumer;
  ASSERT_FALSE(model->IsDataFresh());

  // Disable the weather data type via prefs.
  PrefService* prefs =
      Shell::Get()->session_controller()->GetPrimaryUserPrefService();
  ASSERT_TRUE(prefs);
  prefs->SetBoolean(prefs::kBirchUseWeather, false);

  // Request a fetch.
  model->RequestBirchDataFetch(/*is_post_login=*/false,
                               base::BindOnce(&TestModelConsumer::OnItemsReady,
                                              base::Unretained(&consumer),
                                              /*id=*/"0"));
  // Reply with everything but weather.
  model->SetCalendarItems({});
  model->SetAttachmentItems({});
  model->SetFileSuggestItems({});
  model->SetRecentTabItems({});
  model->SetLastActiveItems({});
  model->SetMostVisitedItems({});
  model->SetSelfShareItems({});
  model->SetReleaseNotesItems({});

  // Consumer was notified that fetch was complete.
  EXPECT_THAT(consumer.items_ready_responses(), testing::ElementsAre("0"));

  // Data is fresh.
  EXPECT_TRUE(model->IsDataFresh());
}

TEST_F(BirchModelTest, EnablePrefsDuringFetchCausesDataFetchRequest) {
  BirchModel* model = Shell::Get()->birch_model();

  // Disable all the prefs except weather, so that a data fetch request creates
  // a pending request.
  PrefService* prefs =
      Shell::Get()->session_controller()->GetPrimaryUserPrefService();
  ASSERT_TRUE(prefs);
  prefs->SetBoolean(prefs::kBirchUseCalendar, false);
  prefs->SetBoolean(prefs::kBirchUseFileSuggest, false);
  prefs->SetBoolean(prefs::kBirchUseRecentTabs, false);
  prefs->SetBoolean(prefs::kBirchUseLastActive, false);
  prefs->SetBoolean(prefs::kBirchUseMostVisited, false);
  prefs->SetBoolean(prefs::kBirchUseSelfShare, false);
  prefs->SetBoolean(prefs::kBirchUseReleaseNotes, false);

  // Request a fetch, creating a pending fetch request.
  model->RequestBirchDataFetch(/*is_post_login=*/false, base::DoNothing());

  auto& client = stub_birch_client_;
  EXPECT_FALSE(client.calendar_provider_.did_request_birch_data_fetch_);
  EXPECT_FALSE(client.file_suggest_provider_.did_request_birch_data_fetch_);
  EXPECT_FALSE(client.recent_tabs_provider_.did_request_birch_data_fetch_);
  EXPECT_FALSE(client.last_active_provider_.did_request_birch_data_fetch_);
  EXPECT_FALSE(client.most_visited_provider_.did_request_birch_data_fetch_);
  EXPECT_FALSE(client.self_share_provider_.did_request_birch_data_fetch_);
  EXPECT_FALSE(client.release_notes_provider_.did_request_birch_data_fetch_);

  // Enable prefs and then expect that data fetch requests are called for each
  // enabled data type.
  prefs->SetBoolean(prefs::kBirchUseCalendar, true);
  prefs->SetBoolean(prefs::kBirchUseFileSuggest, true);
  prefs->SetBoolean(prefs::kBirchUseRecentTabs, true);
  prefs->SetBoolean(prefs::kBirchUseLastActive, true);
  prefs->SetBoolean(prefs::kBirchUseMostVisited, true);
  prefs->SetBoolean(prefs::kBirchUseSelfShare, true);
  prefs->SetBoolean(prefs::kBirchUseReleaseNotes, true);
  EXPECT_TRUE(client.calendar_provider_.did_request_birch_data_fetch_);
  EXPECT_TRUE(client.file_suggest_provider_.did_request_birch_data_fetch_);
  EXPECT_TRUE(client.recent_tabs_provider_.did_request_birch_data_fetch_);
  EXPECT_TRUE(client.last_active_provider_.did_request_birch_data_fetch_);
  EXPECT_TRUE(client.most_visited_provider_.did_request_birch_data_fetch_);
  EXPECT_TRUE(client.self_share_provider_.did_request_birch_data_fetch_);
  EXPECT_TRUE(client.release_notes_provider_.did_request_birch_data_fetch_);
}

TEST_F(BirchModelTest, EnableWeatherPrefDuringFetchCausesDataFetchRequest) {
  BirchModel* model = Shell::Get()->birch_model();

  // Install a stub weather provider.
  auto weather_provider = std::make_unique<StubBirchDataProvider>();
  auto* weather_provider_ptr = weather_provider.get();
  model->OverrideWeatherProviderForTest(std::move(weather_provider));

  // Disable the weather pref.
  PrefService* prefs =
      Shell::Get()->session_controller()->GetPrimaryUserPrefService();
  ASSERT_TRUE(prefs);
  prefs->SetBoolean(prefs::kBirchUseWeather, false);

  // Request a fetch, creating a pending fetch request.
  model->RequestBirchDataFetch(/*is_post_login=*/false, base::DoNothing());

  EXPECT_FALSE(weather_provider_ptr->did_request_birch_data_fetch_);

  // Enable the weather pref and expect a weather data fetch.
  prefs->SetBoolean(prefs::kBirchUseWeather, true);
  EXPECT_TRUE(weather_provider_ptr->did_request_birch_data_fetch_);
}

// Regression test for missing attachment type check in IsDataFresh().
TEST_F(BirchModelTest, IsDataFresh_Attachments) {
  BirchModel* model = Shell::Get()->birch_model();
  ASSERT_FALSE(model->IsDataFresh());

  // Provide all data types except attachments. Data should not be fresh.
  model->SetCalendarItems({});
  model->SetFileSuggestItems({});
  model->SetRecentTabItems({});
  model->SetLastActiveItems({});
  model->SetMostVisitedItems({});
  model->SetSelfShareItems({});
  model->SetWeatherItems({});
  model->SetReleaseNotesItems({});
  EXPECT_FALSE(model->IsDataFresh());

  // Providing attachments finishes the set and the data is fresh.
  model->SetAttachmentItems({});
  EXPECT_TRUE(model->IsDataFresh());
}

// TODO(https://crbug.com/324963992): Fix `BirchModel*Test.DataFetchTimeout`
// for debug builds.
#if defined(NDEBUG)
#define MAYBE_DataFetchTimeout DataFetchTimeout
#else
#define MAYBE_DataFetchTimeout DISABLED_DataFetchTimeout
#endif

// Test that consumer is notified when waiting a set amount of time after
// requesting birch data.
TEST_F(BirchModelTest, MAYBE_DataFetchTimeout) {
  BirchModel* model = Shell::Get()->birch_model();
  TestModelConsumer consumer;
  EXPECT_TRUE(model);

  // Passing time and setting data before requesting a birch data fetch will
  // not notify consumer.
  task_environment()->FastForwardBy(base::Milliseconds(1000));

  model->SetFileSuggestItems(MakeFileItemList(/*item_count=*/1));
  model->SetRecentTabItems(std::vector<BirchTabItem>());
  model->SetLastActiveItems({});
  model->SetMostVisitedItems({});
  model->SetSelfShareItems(std::vector<BirchSelfShareItem>());
  model->SetSelfShareItems({});
  std::vector<BirchWeatherItem> weather_items;
  weather_items.emplace_back(u"desc", 70.f, ui::ImageModel());
  model->SetWeatherItems(std::move(weather_items));
  model->SetCalendarItems({});
  model->SetAttachmentItems({});
  model->SetReleaseNotesItems({});

  EXPECT_TRUE(model->IsDataFresh());
  EXPECT_THAT(consumer.items_ready_responses(), testing::IsEmpty());

  model->RequestBirchDataFetch(/*is_post_login=*/false,
                               base::BindOnce(&TestModelConsumer::OnItemsReady,
                                              base::Unretained(&consumer),
                                              /*id=*/"0"));
  EXPECT_FALSE(model->IsDataFresh());
  EXPECT_THAT(consumer.items_ready_responses(), testing::IsEmpty());

  // Test that passing a short amount of time and setting some data does not
  // notify that items are ready.
  task_environment()->FastForwardBy(base::Milliseconds(500));

  std::vector<BirchTabItem> tab_item_list;
  tab_item_list.emplace_back(u"tab title", GURL("example.com"),
                             base::Time::Now(), GURL("example.com/favicon_url"),
                             "session_name",
                             BirchTabItem::DeviceFormFactor::kDesktop);
  model->SetRecentTabItems(tab_item_list);
  EXPECT_THAT(consumer.items_ready_responses(), testing::IsEmpty());

  // Test that passing enough time notifies that items are ready.
  task_environment()->FastForwardBy(base::Milliseconds(500));
  EXPECT_THAT(consumer.items_ready_responses(), testing::ElementsAre("0"));

  std::vector<std::unique_ptr<BirchItem>> all_items = model->GetAllItems();
  EXPECT_EQ(all_items.size(), 3u);
  EXPECT_EQ(all_items[0]->GetType(), BirchItemType::kWeather);
  EXPECT_EQ(all_items[1]->GetType(), BirchItemType::kTab);
  EXPECT_EQ(all_items[2]->GetType(), BirchItemType::kFile);
  EXPECT_FALSE(model->IsDataFresh());
}

TEST_F(BirchModelWithoutWeatherTest, MAYBE_DataFetchTimeout) {
  BirchModel* model = Shell::Get()->birch_model();
  TestModelConsumer consumer;
  EXPECT_TRUE(model);

  // Passing time and setting data before requesting a birch data fetch will
  // not notify consumer.
  task_environment()->FastForwardBy(base::Milliseconds(1000));
  model->SetRecentTabItems(std::vector<BirchTabItem>());
  model->SetLastActiveItems({});
  model->SetMostVisitedItems({});
  model->SetSelfShareItems(std::vector<BirchSelfShareItem>());
  model->SetFileSuggestItems(MakeFileItemList(/*item_count=*/1));
  model->SetCalendarItems({});
  model->SetAttachmentItems({});
  model->SetReleaseNotesItems({});

  EXPECT_TRUE(model->IsDataFresh());
  EXPECT_THAT(consumer.items_ready_responses(), testing::IsEmpty());

  model->RequestBirchDataFetch(/*is_post_login=*/false,
                               base::BindOnce(&TestModelConsumer::OnItemsReady,
                                              base::Unretained(&consumer),
                                              /*id=*/"0"));
  EXPECT_FALSE(model->IsDataFresh());
  EXPECT_THAT(consumer.items_ready_responses(), testing::IsEmpty());

  // Test that passing a short amount of time and setting some data does not
  // notify that items are ready.
  task_environment()->FastForwardBy(base::Milliseconds(500));
  std::vector<BirchTabItem> tab_item_list;
  tab_item_list.emplace_back(u"tab title", GURL("example.com"),
                             base::Time::Now(), GURL("example.com/favicon_url"),
                             "session_name",
                             BirchTabItem::DeviceFormFactor::kDesktop);
  model->SetRecentTabItems(tab_item_list);
  EXPECT_THAT(consumer.items_ready_responses(), testing::IsEmpty());

  // Test that passing enough time notifies that items are ready.
  task_environment()->FastForwardBy(base::Milliseconds(500));
  EXPECT_THAT(consumer.items_ready_responses(), testing::ElementsAre("0"));

  std::vector<std::unique_ptr<BirchItem>> all_items = model->GetAllItems();
  EXPECT_EQ(all_items.size(), 2u);
  EXPECT_EQ(all_items[0]->GetType(), BirchItemType::kTab);
  EXPECT_EQ(all_items[1]->GetType(), BirchItemType::kFile);
  EXPECT_FALSE(model->IsDataFresh());
}

// Test that the data fetch timeout is longer when requesting directly after
// login.
TEST_F(BirchModelTest, PostLoginDataFetchTimeout) {
  BirchModel* model = Shell::Get()->birch_model();
  TestModelConsumer consumer;
  EXPECT_TRUE(model);

  // Passing time and setting data before requesting a birch data fetch will
  // not notify consumer.
  task_environment()->FastForwardBy(base::Milliseconds(1000));

  model->SetFileSuggestItems(MakeFileItemList(/*item_count=*/1));
  model->SetRecentTabItems(std::vector<BirchTabItem>());
  model->SetLastActiveItems({});
  model->SetMostVisitedItems({});
  model->SetSelfShareItems(std::vector<BirchSelfShareItem>());
  model->SetWeatherItems({});
  model->SetCalendarItems({});
  model->SetAttachmentItems({});
  model->SetReleaseNotesItems({});

  EXPECT_TRUE(model->IsDataFresh());
  EXPECT_THAT(consumer.items_ready_responses(), testing::IsEmpty());

  model->RequestBirchDataFetch(/*is_post_login=*/true,
                               base::BindOnce(&TestModelConsumer::OnItemsReady,
                                              base::Unretained(&consumer),
                                              /*id=*/"0"));
  EXPECT_FALSE(model->IsDataFresh());
  EXPECT_THAT(consumer.items_ready_responses(), testing::IsEmpty());

  // Test that passing a short amount of time and setting some data does not
  // notify that items are ready.
  task_environment()->FastForwardBy(base::Milliseconds(2500));

  std::vector<BirchTabItem> tab_item_list;
  tab_item_list.emplace_back(u"tab title", GURL("example.com"),
                             base::Time::Now(), GURL("example.com/favicon_url"),
                             "session_name",
                             BirchTabItem::DeviceFormFactor::kDesktop);
  model->SetRecentTabItems(tab_item_list);
  EXPECT_THAT(consumer.items_ready_responses(), testing::IsEmpty());

  std::vector<BirchSelfShareItem> self_share_item_list;
  GURL faviconUrl = GURL("https://www.favicon.com/");
  self_share_item_list.emplace_back(
      u"self share guid", u"self share tab", GURL("foo.bar.two"), base::Time(),
      u"my device", faviconUrl, base::DoNothing());
  model->SetSelfShareItems(std::move(self_share_item_list));
  EXPECT_THAT(consumer.items_ready_responses(), testing::IsEmpty());

  // Test that passing enough time notifies that items are ready.
  task_environment()->FastForwardBy(base::Milliseconds(500));
  EXPECT_THAT(consumer.items_ready_responses(), testing::ElementsAre("0"));

  std::vector<std::unique_ptr<BirchItem>> all_items = model->GetAllItems();
  EXPECT_EQ(all_items.size(), 2u);
  EXPECT_EQ(all_items[0]->GetType(), BirchItemType::kTab);
  EXPECT_EQ(all_items[1]->GetType(), BirchItemType::kFile);
  EXPECT_FALSE(model->IsDataFresh());
}

TEST_F(BirchModelWithoutWeatherTest, AddItemNotifiesCallback) {
  BirchModel* model = Shell::Get()->birch_model();
  TestModelConsumer consumer;
  EXPECT_TRUE(model);

  // Setting items in the model does not notify when no request has occurred.
  model->SetRecentTabItems(std::vector<BirchTabItem>());
  model->SetLastActiveItems({});
  model->SetMostVisitedItems(std::vector<BirchMostVisitedItem>());
  model->SetSelfShareItems(std::vector<BirchSelfShareItem>());
  model->SetFileSuggestItems(std::vector<BirchFileItem>());
  EXPECT_THAT(consumer.items_ready_responses(), testing::IsEmpty());

  // Make a data fetch request and set fresh tab data.
  model->RequestBirchDataFetch(/*is_post_login=*/false,
                               base::BindOnce(&TestModelConsumer::OnItemsReady,
                                              base::Unretained(&consumer),
                                              /*id=*/"0"));
  model->SetRecentTabItems(std::vector<BirchTabItem>());
  model->SetLastActiveItems({});
  model->SetMostVisitedItems(std::vector<BirchMostVisitedItem>());
  model->SetSelfShareItems(std::vector<BirchSelfShareItem>());
  // Consumer is not notified until all data sources have responded.
  EXPECT_THAT(consumer.items_ready_responses(), testing::IsEmpty());

  model->SetFileSuggestItems(MakeFileItemList(/*item_count=*/1));
  model->SetWeatherItems({});
  model->SetCalendarItems({});
  model->SetAttachmentItems({});
  model->SetReleaseNotesItems({});

  // Adding file items sets all data as fresh, notifying consumers.
  EXPECT_THAT(consumer.items_ready_responses(), testing::ElementsAre("0"));

  // Setting the file suggest items should not trigger items ready again, since
  // no data fetch was requested.
  model->SetFileSuggestItems(MakeFileItemList(/*item_count=*/2));
  EXPECT_THAT(consumer.items_ready_responses(), testing::ElementsAre("0"));

  // Request another data fetch and expect the consumer to be notified once
  // items are set again.
  model->RequestBirchDataFetch(/*is_post_login=*/false,
                               base::BindOnce(&TestModelConsumer::OnItemsReady,
                                              base::Unretained(&consumer),
                                              /*id=*/"1"));
  model->SetRecentTabItems(std::vector<BirchTabItem>());
  model->SetLastActiveItems({});
  model->SetMostVisitedItems(std::vector<BirchMostVisitedItem>());
  model->SetSelfShareItems(std::vector<BirchSelfShareItem>());
  model->SetFileSuggestItems(MakeFileItemList(/*item_count=*/2));
  model->SetCalendarItems({});
  model->SetAttachmentItems({});
  model->SetReleaseNotesItems({});
  EXPECT_THAT(consumer.items_ready_responses(), testing::ElementsAre("0", "1"));
}

TEST_F(BirchModelTest, MultipleRequestsHaveIndependentTimeouts) {
  BirchModel* model = Shell::Get()->birch_model();
  TestModelConsumer consumer;
  EXPECT_TRUE(model);

  model->RequestBirchDataFetch(/*is_post_login=*/false,
                               base::BindOnce(&TestModelConsumer::OnItemsReady,
                                              base::Unretained(&consumer),
                                              /*id=*/"0"));

  task_environment()->FastForwardBy(base::Milliseconds(500));
  EXPECT_THAT(consumer.items_ready_responses(), testing::IsEmpty());

  model->RequestBirchDataFetch(/*is_post_login=*/false,
                               base::BindOnce(&TestModelConsumer::OnItemsReady,
                                              base::Unretained(&consumer),
                                              /*id=*/"1"));
  task_environment()->FastForwardBy(base::Milliseconds(500));
  EXPECT_THAT(consumer.items_ready_responses(), testing::ElementsAre("0"));

  task_environment()->FastForwardBy(base::Milliseconds(500));
  EXPECT_THAT(consumer.items_ready_responses(), testing::ElementsAre("0", "1"));
  EXPECT_FALSE(model->IsDataFresh());

  model->RequestBirchDataFetch(/*is_post_login=*/false,
                               base::BindOnce(&TestModelConsumer::OnItemsReady,
                                              base::Unretained(&consumer),
                                              /*id=*/"2"));

  EXPECT_THAT(consumer.items_ready_responses(), testing::ElementsAre("0", "1"));

  task_environment()->FastForwardBy(base::Milliseconds(1000));
  EXPECT_THAT(consumer.items_ready_responses(),
              testing::ElementsAre("0", "1", "2"));
  EXPECT_FALSE(model->IsDataFresh());
}

TEST_F(BirchModelTest, ResponseAfterFirstTimeout) {
  BirchModel* model = Shell::Get()->birch_model();
  TestModelConsumer consumer;
  EXPECT_TRUE(model);

  model->RequestBirchDataFetch(/*is_post_login=*/false,
                               base::BindOnce(&TestModelConsumer::OnItemsReady,
                                              base::Unretained(&consumer),
                                              /*id=*/"0"));

  task_environment()->FastForwardBy(base::Milliseconds(500));
  EXPECT_THAT(consumer.items_ready_responses(), testing::IsEmpty());

  model->RequestBirchDataFetch(/*is_post_login=*/false,
                               base::BindOnce(&TestModelConsumer::OnItemsReady,
                                              base::Unretained(&consumer),
                                              /*id=*/"1"));
  task_environment()->FastForwardBy(base::Milliseconds(500));
  EXPECT_THAT(consumer.items_ready_responses(), testing::ElementsAre("0"));

  task_environment()->FastForwardBy(base::Milliseconds(100));
  EXPECT_THAT(consumer.items_ready_responses(), testing::ElementsAre("0"));
  EXPECT_FALSE(model->IsDataFresh());

  model->SetFileSuggestItems(MakeFileItemList(/*item_count=*/1));
  std::vector<BirchWeatherItem> weather_item_list;
  weather_item_list.emplace_back(u"cloudy", 70.f, ui::ImageModel());
  model->SetWeatherItems(std::move(weather_item_list));
  std::vector<BirchTabItem> tab_item_list;
  tab_item_list.emplace_back(u"tab", GURL("https://www.example.com/"),
                             base::Time(), GURL("favicon"), "session",
                             BirchTabItem::DeviceFormFactor::kDesktop);
  model->SetRecentTabItems(std::move(tab_item_list));
  std::vector<BirchLastActiveItem> last_active_list;
  last_active_list.emplace_back(u"active", GURL("https://yahoo.com/"),
                                base::Time(), ui::ImageModel());
  model->SetLastActiveItems(std::move(last_active_list));
  std::vector<BirchMostVisitedItem> most_visited_list;
  most_visited_list.emplace_back(u"visited", GURL("https://google.com/"),
                                 ui::ImageModel());
  model->SetMostVisitedItems(std::move(most_visited_list));
  std::vector<BirchSelfShareItem> self_share_item_list;
  GURL faviconUrl = GURL("favicon");
  self_share_item_list.emplace_back(
      u"self share guid", u"self share tab", GURL("foo.bar.two"), base::Time(),
      u"my device", faviconUrl, base::DoNothing());
  model->SetSelfShareItems(std::move(self_share_item_list));
  model->SetCalendarItems(MakeCalendarItemList(/*event_count=*/1));
  model->SetAttachmentItems(MakeAttachmentItemList(/*item_count=*/1));
  std::vector<BirchReleaseNotesItem> release_notes_item_list;
  release_notes_item_list.emplace_back(
      u"note", u"explore", GURL("https://www.example.com/"), base::Time());
  model->SetReleaseNotesItems(release_notes_item_list);

  EXPECT_TRUE(model->IsDataFresh());

  EXPECT_THAT(consumer.items_ready_responses(), testing::ElementsAre("0", "1"));
  EXPECT_EQ(model->GetAllItems().size(), 9u);

  model->RequestBirchDataFetch(/*is_post_login=*/false,
                               base::BindOnce(&TestModelConsumer::OnItemsReady,
                                              base::Unretained(&consumer),
                                              /*id=*/"2"));
  EXPECT_FALSE(model->IsDataFresh());
  task_environment()->FastForwardBy(base::Milliseconds(100));
  EXPECT_FALSE(model->IsDataFresh());
  EXPECT_THAT(consumer.items_ready_responses(), testing::ElementsAre("0", "1"));

  model->SetFileSuggestItems({});
  model->SetWeatherItems({});
  model->SetRecentTabItems({});
  model->SetLastActiveItems({});
  model->SetMostVisitedItems({});
  model->SetSelfShareItems({});
  model->SetCalendarItems({});
  model->SetAttachmentItems({});
  model->SetReleaseNotesItems({});

  EXPECT_THAT(consumer.items_ready_responses(),
              testing::ElementsAre("0", "1", "2"));
  EXPECT_EQ(model->GetAllItems().size(), 0u);
  EXPECT_TRUE(model->IsDataFresh());
}

TEST_F(BirchModelTest, GetAllItems) {
  BirchModel* model = Shell::Get()->birch_model();

  // Insert one item of each type.
  std::vector<BirchWeatherItem> weather_item_list;
  weather_item_list.emplace_back(u"cloudy", 70.f, ui::ImageModel());
  model->SetWeatherItems(std::move(weather_item_list));
  std::vector<BirchReleaseNotesItem> release_notes_item_list;
  release_notes_item_list.emplace_back(
      u"note", u"explore", GURL("https://www.example.com/"), base::Time());
  model->SetReleaseNotesItems(std::move(release_notes_item_list));
  model->SetCalendarItems(MakeCalendarItemList(/*event_count=*/1));
  model->SetAttachmentItems(MakeAttachmentItemList(/*item_count=*/1));
  std::vector<BirchTabItem> tab_item_list;
  tab_item_list.emplace_back(u"tab", GURL("https://www.example.com/"),
                             base::Time(), GURL("favicon"), "session",
                             BirchTabItem::DeviceFormFactor::kDesktop);
  model->SetRecentTabItems(std::move(tab_item_list));
  std::vector<BirchLastActiveItem> last_active_list;
  last_active_list.emplace_back(u"active", GURL("https://yahoo.com/"),
                                base::Time(), ui::ImageModel());
  model->SetLastActiveItems(std::move(last_active_list));
  std::vector<BirchMostVisitedItem> most_visited_list;
  most_visited_list.emplace_back(u"visited", GURL("https://google.com/"),
                                 ui::ImageModel());
  model->SetMostVisitedItems(std::move(most_visited_list));
  model->SetFileSuggestItems(MakeFileItemList(/*item_count=*/1));

  // Verify that GetAllItems() returns the correct number of items and the
  // code didn't skip a type.
  std::vector<std::unique_ptr<BirchItem>> all_items = model->GetAllItems();
  ASSERT_EQ(all_items.size(), 8u);
  EXPECT_EQ(all_items[0]->GetType(), BirchItemType::kWeather);
  EXPECT_EQ(all_items[1]->GetType(), BirchItemType::kReleaseNotes);
  EXPECT_EQ(all_items[2]->GetType(), BirchItemType::kCalendar);
  EXPECT_EQ(all_items[3]->GetType(), BirchItemType::kAttachment);
  EXPECT_EQ(all_items[4]->GetType(), BirchItemType::kTab);
  EXPECT_EQ(all_items[5]->GetType(), BirchItemType::kLastActive);
  EXPECT_EQ(all_items[6]->GetType(), BirchItemType::kMostVisited);
  EXPECT_EQ(all_items[7]->GetType(), BirchItemType::kFile);
}

TEST_F(BirchModelTest, SetItemListRecordsHistogram) {
  base::HistogramTester histograms;
  BirchModel* model = Shell::Get()->birch_model();
  TestModelConsumer consumer;

  // Simulate a data fetch.
  model->RequestBirchDataFetch(/*is_post_login=*/false,
                               base::BindOnce(&TestModelConsumer::OnItemsReady,
                                              base::Unretained(&consumer),
                                              /*id=*/"0"));
  // Insert one item of each type.
  model->SetCalendarItems(MakeCalendarItemList(/*event_count=*/1));
  model->SetAttachmentItems(MakeAttachmentItemList(/*item_count=*/1));
  std::vector<BirchTabItem> tab_item_list;
  tab_item_list.emplace_back(u"tab", GURL("https://www.example.com/"),
                             base::Time(), GURL("favicon"), "session",
                             BirchTabItem::DeviceFormFactor::kDesktop);
  model->SetRecentTabItems(std::move(tab_item_list));
  std::vector<BirchLastActiveItem> last_active_list;
  last_active_list.emplace_back(u"active", GURL("https://yahoo.com/"),
                                base::Time(), ui::ImageModel());
  model->SetLastActiveItems(std::move(last_active_list));
  model->SetFileSuggestItems(MakeFileItemList(/*item_count=*/1));
  std::vector<BirchWeatherItem> weather_item_list;
  weather_item_list.emplace_back(u"cloudy", 70.f, ui::ImageModel());
  model->SetWeatherItems(std::move(weather_item_list));
  std::vector<BirchReleaseNotesItem> release_notes_item_list;
  release_notes_item_list.emplace_back(
      u"note", u"explore", GURL("https://www.example.com/"), base::Time());
  model->SetReleaseNotesItems(std::move(release_notes_item_list));

  // Histograms were recorded for each type.
  histograms.ExpectBucketCount("Ash.Birch.ResultsReturned.Calendar", 1, 1);
  histograms.ExpectBucketCount("Ash.Birch.ResultsReturned.Attachment", 1, 1);
  histograms.ExpectBucketCount("Ash.Birch.ResultsReturned.File", 1, 1);
  histograms.ExpectBucketCount("Ash.Birch.ResultsReturned.Tab", 1, 1);
  histograms.ExpectBucketCount("Ash.Birch.ResultsReturned.LastActive", 1, 1);
  histograms.ExpectBucketCount("Ash.Birch.ResultsReturned.Weather", 1, 1);
  histograms.ExpectBucketCount("Ash.Birch.ResultsReturned.ReleaseNotes", 1, 1);
}

TEST_F(BirchModelTest, GetItemsForDisplay_EnoughTypes) {
  BirchModel* model = Shell::Get()->birch_model();

  // Insert two calendar items.
  std::vector<BirchCalendarItem> calendar_item_list =
      MakeCalendarItemList(/*event_count=*/2);

  // The first event has ranking, the second one has no ranking.
  calendar_item_list.front().set_ranking(4.f);

  model->SetCalendarItems(std::move(calendar_item_list));

  // Insert one item for other types.
  std::vector<BirchAttachmentItem> attachment_item_list =
      MakeAttachmentItemList(/*item_count=*/1);

  attachment_item_list.back().set_ranking(3.f);
  model->SetAttachmentItems(std::move(attachment_item_list));

  std::vector<BirchTabItem> tab_item_list;
  tab_item_list.emplace_back(u"tab", GURL("https://www.example.com/"),
                             base::Time(), GURL("favicon"), "session",
                             BirchTabItem::DeviceFormFactor::kDesktop);
  tab_item_list.back().set_ranking(2.f);
  model->SetRecentTabItems(std::move(tab_item_list));

  std::vector<BirchFileItem> file_item_list =
      MakeFileItemList(/*item_count=*/1);
  file_item_list.back().set_ranking(1.f);
  model->SetFileSuggestItems(std::move(file_item_list));

  std::vector<std::unique_ptr<BirchItem>> items = model->GetItemsForDisplay();

  // We should only get 4 ranked items.
  ASSERT_EQ(items.size(), 4u);

  // The items are in priority order.
  EXPECT_FLOAT_EQ(items[0]->ranking(), 1.f);
  EXPECT_EQ(items[0]->GetType(), BirchItemType::kFile);
  EXPECT_FLOAT_EQ(items[1]->ranking(), 2.f);
  EXPECT_EQ(items[1]->GetType(), BirchItemType::kTab);
  EXPECT_FLOAT_EQ(items[2]->ranking(), 3.f);
  EXPECT_EQ(items[2]->GetType(), BirchItemType::kAttachment);
  EXPECT_FLOAT_EQ(items[3]->ranking(), 4.f);
  EXPECT_EQ(items[3]->GetType(), BirchItemType::kCalendar);
}

TEST_F(BirchModelTest, GetItemsForDisplay_IncludesDuplicateTypes) {
  BirchModel* model = Shell::Get()->birch_model();

  // Insert 2 calendar events with high priority.
  std::vector<BirchCalendarItem> calendar_item_list =
      MakeCalendarItemList(/*event_count=*/2);
  calendar_item_list.front().set_ranking(1.f);
  calendar_item_list.back().set_ranking(2.f);
  model->SetCalendarItems(std::move(calendar_item_list));

  // Then insert 3 other items with lower priority.
  std::vector<BirchAttachmentItem> attachment_item_list =
      MakeAttachmentItemList(/*item_count=*/1);
  attachment_item_list.back().set_ranking(3.f);
  model->SetAttachmentItems(std::move(attachment_item_list));

  std::vector<BirchTabItem> tab_item_list;
  tab_item_list.emplace_back(u"tab", GURL("https://www.example.com/"),
                             base::Time(), GURL("favicon"), "session",
                             BirchTabItem::DeviceFormFactor::kDesktop);
  tab_item_list.back().set_ranking(4.f);
  model->SetRecentTabItems(std::move(tab_item_list));

  std::vector<BirchFileItem> file_item_list =
      MakeFileItemList(/*item_count=*/1);
  file_item_list.back().set_ranking(5.f);
  model->SetFileSuggestItems(std::move(file_item_list));

  std::vector<std::unique_ptr<BirchItem>> items = model->GetItemsForDisplay();

  // Both calendar events are included.
  EXPECT_FLOAT_EQ(items[0]->ranking(), 1.f);
  EXPECT_EQ(items[0]->GetType(), BirchItemType::kCalendar);
  EXPECT_FLOAT_EQ(items[1]->ranking(), 2.f);
  EXPECT_EQ(items[1]->GetType(), BirchItemType::kCalendar);
  EXPECT_FLOAT_EQ(items[2]->ranking(), 3.f);
  EXPECT_EQ(items[2]->GetType(), BirchItemType::kAttachment);
  EXPECT_FLOAT_EQ(items[3]->ranking(), 4.f);
  EXPECT_EQ(items[3]->GetType(), BirchItemType::kTab);
  EXPECT_FLOAT_EQ(items[4]->ranking(), 5.f);
  EXPECT_EQ(items[4]->GetType(), BirchItemType::kFile);
}

TEST_F(BirchModelTest, GetItemsForDisplay_TwoDuplicateTypes) {
  BirchModel* model = Shell::Get()->birch_model();

  // Insert 2 items of the same type.
  std::vector<BirchCalendarItem> calendar_item_list =
      MakeCalendarItemList(/*event_count=*/2);
  calendar_item_list.front().set_ranking(1.f);
  calendar_item_list.back().set_ranking(2.f);
  model->SetCalendarItems(std::move(calendar_item_list));

  // Insert 2 more items of a different type.
  std::vector<BirchAttachmentItem> attachment_item_list =
      MakeAttachmentItemList(/*item_count=*/2);
  attachment_item_list.front().set_ranking(3.f);
  attachment_item_list.back().set_ranking(4.f);
  model->SetAttachmentItems(std::move(attachment_item_list));

  std::vector<std::unique_ptr<BirchItem>> items = model->GetItemsForDisplay();

  ASSERT_EQ(items.size(), 4u);
  EXPECT_FLOAT_EQ(items[0]->ranking(), 1.f);
  EXPECT_EQ(items[0]->GetType(), BirchItemType::kCalendar);
  EXPECT_FLOAT_EQ(items[1]->ranking(), 2.f);
  EXPECT_EQ(items[1]->GetType(), BirchItemType::kCalendar);
  EXPECT_FLOAT_EQ(items[2]->ranking(), 3.f);
  EXPECT_EQ(items[2]->GetType(), BirchItemType::kAttachment);
  EXPECT_FLOAT_EQ(items[3]->ranking(), 4.f);
  EXPECT_EQ(items[3]->GetType(), BirchItemType::kAttachment);
}

TEST_F(BirchModelTest, GetItemsForDisplay_NotEnoughItems) {
  BirchModel* model = Shell::Get()->birch_model();

  // Insert 3 items of the same type.
  std::vector<BirchCalendarItem> calendar_item_list =
      MakeCalendarItemList(/*event_count=*/3);
  calendar_item_list[0].set_ranking(1.f);
  calendar_item_list[1].set_ranking(2.f);
  calendar_item_list[2].set_ranking(3.f);
  model->SetCalendarItems(std::move(calendar_item_list));

  std::vector<std::unique_ptr<BirchItem>> items = model->GetItemsForDisplay();

  // 3 items are returned.
  ASSERT_EQ(items.size(), 3u);
  EXPECT_FLOAT_EQ(items[0]->ranking(), 1.f);
  EXPECT_EQ(items[0]->GetType(), BirchItemType::kCalendar);
  EXPECT_FLOAT_EQ(items[1]->ranking(), 2.f);
  EXPECT_EQ(items[1]->GetType(), BirchItemType::kCalendar);
  EXPECT_FLOAT_EQ(items[2]->ranking(), 3.f);
  EXPECT_EQ(items[2]->GetType(), BirchItemType::kCalendar);
}

TEST_F(BirchModelTest, GetItemsForDisplay_NotRankedItem) {
  BirchModel* model = Shell::Get()->birch_model();

  // Insert 1 regular item and 1 item with no ranking.
  std::vector<BirchCalendarItem> calendar_item_list =
      MakeCalendarItemList(/*event_count=*/2);
  calendar_item_list.front().set_ranking(1.f);
  model->SetCalendarItems(std::move(calendar_item_list));

  std::vector<std::unique_ptr<BirchItem>> items = model->GetItemsForDisplay();

  // Only 1 item is returned because the unranked item is discarded.
  ASSERT_EQ(items.size(), 1u);
  EXPECT_FLOAT_EQ(items[0]->ranking(), 1.f);
  EXPECT_EQ(items[0]->GetType(), BirchItemType::kCalendar);
}

TEST_F(BirchModelTest, ModelClearedOnMultiProfileUserSwitch) {
  BirchModel* model = Shell::Get()->birch_model();
  TestModelConsumer consumer;

  // Add an item to the model.
  model->SetFileSuggestItems(MakeFileItemList(/*item_count=*/1));

  // Set the other types as empty so the model has fresh data.
  model->SetCalendarItems({});
  model->SetAttachmentItems({});
  model->SetRecentTabItems({});
  model->SetLastActiveItems({});
  model->SetMostVisitedItems({});
  model->SetSelfShareItems({});
  model->SetWeatherItems({});
  model->SetReleaseNotesItems({});
  ASSERT_TRUE(model->IsDataFresh());

  // Sign in to a secondary user.
  SimulateUserLogin("user2@test.com");
  ASSERT_FALSE(Shell::Get()->session_controller()->IsUserPrimary());

  // The model is empty.
  EXPECT_TRUE(model->GetAllItems().empty());

  // The data is not fresh.
  EXPECT_FALSE(model->IsDataFresh());
}

TEST_F(BirchModelTest, WeatherItemsClearedWhenGeolocationDisabled) {
  BirchModel* model = Shell::Get()->birch_model();

  // Geolocation starts as allowed.
  auto* geolocation_provider = SimpleGeolocationProvider::GetInstance();
  ASSERT_EQ(geolocation_provider->GetGeolocationAccessLevel(),
            GeolocationAccessLevel::kAllowed);

  // Add a weather item.
  std::vector<BirchWeatherItem> weather_items;
  weather_items.emplace_back(u"Sunny", 72.f, ui::ImageModel());
  model->SetWeatherItems(std::move(weather_items));
  ASSERT_FALSE(model->GetWeatherForTest().empty());

  // Disable geolocation permission.
  geolocation_provider->SetGeolocationAccessLevel(
      GeolocationAccessLevel::kDisallowed);

  // The weather item is removed.
  EXPECT_TRUE(model->GetWeatherForTest().empty());
}

TEST_F(BirchModelTest, RemoveAndFilterTabItem) {
  BirchModel* model = Shell::Get()->birch_model();

  model->SetCalendarItems({});
  model->SetAttachmentItems({});
  model->SetLastActiveItems({});
  model->SetSelfShareItems({});
  model->SetFileSuggestItems({});
  model->SetWeatherItems({});
  model->SetReleaseNotesItems({});

  BirchTabItem item0(u"item0", GURL("https://example.com/01"), base::Time(),
                     GURL(), "", BirchTabItem::DeviceFormFactor::kDesktop);
  BirchTabItem item1(u"item1", GURL("https://example.com/11"), base::Time(),
                     GURL(), "", BirchTabItem::DeviceFormFactor::kDesktop);
  BirchTabItem item2(u"item2", GURL("https://example.com/21"), base::Time(),
                     GURL(), "", BirchTabItem::DeviceFormFactor::kDesktop);
  std::vector<BirchTabItem> tab_item_list = {item0, item1, item2};
  model->SetRecentTabItems(tab_item_list);

  std::vector<std::unique_ptr<BirchItem>> all_items = model->GetAllItems();
  ASSERT_EQ(all_items.size(), 3u);

  // Remove `item1` and check that it is filtered from `all_items`.
  model->RemoveItem(&item1);

  all_items = model->GetAllItems();
  ASSERT_EQ(all_items.size(), 2u);
}

TEST_F(BirchModelTest, RemoveAndFilterCalendarItem) {
  BirchModel* model = Shell::Get()->birch_model();

  model->SetRecentTabItems({});
  model->SetLastActiveItems({});
  model->SetSelfShareItems({});
  model->SetAttachmentItems({});
  model->SetFileSuggestItems({});
  model->SetWeatherItems({});
  model->SetReleaseNotesItems({});

  std::vector<BirchCalendarItem> calendar_item_list =
      MakeCalendarItemList(/*event_count=*/3);
  model->SetCalendarItems(calendar_item_list);

  std::vector<std::unique_ptr<BirchItem>> all_items = model->GetAllItems();
  ASSERT_EQ(all_items.size(), 3u);

  // Remove the second item and check that it is filtered from `all_items`.
  model->RemoveItem(&calendar_item_list[1]);

  all_items = model->GetAllItems();
  ASSERT_EQ(all_items.size(), 2u);
}

TEST_F(BirchModelTest, RemoveAndFilterAttachmentItem) {
  BirchModel* model = Shell::Get()->birch_model();

  model->SetCalendarItems({});
  model->SetRecentTabItems({});
  model->SetLastActiveItems({});
  model->SetSelfShareItems({});
  model->SetFileSuggestItems({});
  model->SetWeatherItems({});
  model->SetReleaseNotesItems({});

  std::vector<BirchAttachmentItem> attachment_item_list =
      MakeAttachmentItemList(/*item_count=*/3);
  model->SetAttachmentItems(attachment_item_list);

  std::vector<std::unique_ptr<BirchItem>> all_items = model->GetAllItems();
  ASSERT_EQ(all_items.size(), 3u);

  // Remove the second item and check that it is filtered from `all_items`.
  model->RemoveItem(&attachment_item_list[1]);

  all_items = model->GetAllItems();
  ASSERT_EQ(all_items.size(), 2u);
}

TEST_F(BirchModelTest, RemoveAndFilterFileItem) {
  BirchModel* model = Shell::Get()->birch_model();

  model->SetCalendarItems({});
  model->SetSelfShareItems({});
  model->SetAttachmentItems({});
  model->SetRecentTabItems({});
  model->SetLastActiveItems({});
  model->SetWeatherItems({});
  model->SetReleaseNotesItems({});

  std::vector<BirchFileItem> file_item_list =
      MakeFileItemList(/*item_count=*/3);
  model->SetFileSuggestItems(file_item_list);

  std::vector<std::unique_ptr<BirchItem>> all_items = model->GetAllItems();
  ASSERT_EQ(all_items.size(), 3u);

  // Remove the second item and check that it is filtered from `all_items`.
  model->RemoveItem(&file_item_list[1]);

  all_items = model->GetAllItems();
  ASSERT_EQ(all_items.size(), 2u);
}

TEST_F(BirchModelTest, DuplicateFileAndAttachmentItem) {
  BirchModel* model = Shell::Get()->birch_model();

  model->SetCalendarItems({});
  model->SetRecentTabItems({});
  model->SetLastActiveItems({});
  model->SetSelfShareItems({});
  model->SetWeatherItems({});
  model->SetReleaseNotesItems({});

  std::vector<BirchAttachmentItem> attachment_item_list;
  attachment_item_list.emplace_back(
      u"Ongoing Event Attachment 1",
      /*file_url=*/GURL(),
      /*icon_url=*/GURL(),
      /*start_time=*/base::Time(TimeFromString("22 Feb 2024 3:00 UTC")),
      /*end_time=*/base::Time(TimeFromString("22 Feb 2024 5:00 UTC")),
      /*file_id=*/"duplicate_file_id_1");
  attachment_item_list.emplace_back(
      u"Tomorrow Event Attachment 2",
      /*file_url=*/GURL(),
      /*icon_url=*/GURL(),
      /*start_time=*/base::Time(TimeFromString("23 Feb 2024 3:00 UTC")),
      /*end_time=*/base::Time(TimeFromString("23 Feb 2024 5:00 UTC")),
      /*file_id=*/"duplicate_file_id_2");
  model->SetAttachmentItems(attachment_item_list);

  std::vector<BirchFileItem> file_item_list;
  file_item_list.emplace_back(
      base::FilePath("Recently Edited File 1"),
      /*justification=*/u"",
      /*timestamp=*/base::Time(TimeFromString("22 Feb 2024 3:00 UTC")),
      /*file_id=*/"duplicate_file_id_1", "icon_url");
  file_item_list.emplace_back(
      base::FilePath("Recently Edited File 2"),
      /*justification=*/u"",
      /*timestamp=*/base::Time(TimeFromString("22 Feb 2024 3:00 UTC")),
      /*file_id=*/"duplicate_file_id_2", "icon_url");
  model->SetFileSuggestItems(file_item_list);

  // Calling GetAllItems() should return two items, once attachment and one
  // file.
  std::vector<std::unique_ptr<BirchItem>> all_items = model->GetAllItems();
  ASSERT_EQ(all_items.size(), 2u);
  EXPECT_EQ(all_items[0]->GetType(), BirchItemType::kAttachment);
  EXPECT_EQ(all_items[0]->title(), u"Ongoing Event Attachment 1");
  EXPECT_EQ(all_items[1]->GetType(), BirchItemType::kFile);
  EXPECT_EQ(all_items[1]->title(), u"Recently Edited File 2");
}

TEST_F(BirchModelTest, DuplicateSelfShareAndRecentTabItem) {
  BirchModel* model = Shell::Get()->birch_model();

  model->SetCalendarItems({});
  model->SetRecentTabItems({});
  model->SetLastActiveItems({});
  model->SetSelfShareItems({});
  model->SetWeatherItems({});
  model->SetReleaseNotesItems({});

  std::vector<BirchTabItem> tab_item_list;
  tab_item_list.emplace_back(u"tab", GURL("https://www.example.com/"),
                             base::Time(), GURL("https://www.favicon.com/"),
                             "session",
                             BirchTabItem::DeviceFormFactor::kDesktop);
  model->SetRecentTabItems(std::move(tab_item_list));

  std::vector<BirchSelfShareItem> self_share_item_list;
  GURL faviconUrl = GURL("https://www.favicon.com/");
  self_share_item_list.emplace_back(
      u"self share guid", u"self share tab", GURL("https://www.example.com/"),
      base::Time(), u"my device", faviconUrl, base::DoNothing());
  model->SetSelfShareItems(std::move(self_share_item_list));

  std::vector<std::unique_ptr<BirchItem>> all_items = model->GetAllItems();
  ASSERT_EQ(all_items.size(), 1u);
  EXPECT_EQ(all_items[0]->GetType(), BirchItemType::kTab);
  EXPECT_EQ(all_items[0]->title(), u"tab");
}

TEST_F(BirchModelTest, DuplicateLastActiveAndRecentTabItem) {
  BirchModel* model = Shell::Get()->birch_model();

  // Set the time to morning.
  test_clock_.SetNow(TimeFromString("22 Feb 2024 7:00 UTC"));

  // Create a recent tab from more than an hour ago.
  std::vector<BirchTabItem> tab_item_list;
  tab_item_list.emplace_back(u"tab", GURL("https://www.example.com/"),
                             test_clock_.Now() - base::Hours(2), GURL(),
                             "session",
                             BirchTabItem::DeviceFormFactor::kDesktop);
  model->SetRecentTabItems(std::move(tab_item_list));

  std::vector<BirchLastActiveItem> last_active_item_list;
  last_active_item_list.emplace_back(u"last active",
                                     GURL("https://www.example.com/"),
                                     base::Time(), ui::ImageModel());
  model->SetLastActiveItems(std::move(last_active_item_list));

  // The last active item has the higher priority and hence is shown.
  std::vector<std::unique_ptr<BirchItem>> all_items = model->GetAllItems();
  ASSERT_EQ(all_items.size(), 1u);
  EXPECT_EQ(all_items[0]->GetType(), BirchItemType::kLastActive);
  EXPECT_EQ(all_items[0]->title(), u"last active");
}

TEST_F(BirchModelTest, DuplicateMostVisitedAndRecentTabItem) {
  BirchModel* model = Shell::Get()->birch_model();

  // Set the time to morning so that most visited items will be ranked.
  test_clock_.SetNow(TimeFromString("22 Feb 2024 7:00 UTC"));

  // Create a recent tab from more than an hour ago.
  std::vector<BirchTabItem> tab_item_list;
  tab_item_list.emplace_back(u"tab", GURL("https://www.example.com/"),
                             test_clock_.Now() - base::Hours(2), GURL(),
                             "session",
                             BirchTabItem::DeviceFormFactor::kDesktop);
  model->SetRecentTabItems(std::move(tab_item_list));

  std::vector<BirchMostVisitedItem> most_visited_item_list;
  most_visited_item_list.emplace_back(
      u"most visited", GURL("https://www.example.com/"), ui::ImageModel());
  model->SetMostVisitedItems(std::move(most_visited_item_list));

  // The most visited item has the higher priority and hence is shown.
  std::vector<std::unique_ptr<BirchItem>> all_items = model->GetAllItems();
  ASSERT_EQ(all_items.size(), 1u);
  EXPECT_EQ(all_items[0]->GetType(), BirchItemType::kMostVisited);
  EXPECT_EQ(all_items[0]->title(), u"most visited");
}

TEST_F(BirchModelTest, DifferentSelfShareAndRecentTabItem) {
  BirchModel* model = Shell::Get()->birch_model();

  model->SetCalendarItems({});
  model->SetRecentTabItems({});
  model->SetLastActiveItems({});
  model->SetSelfShareItems({});
  model->SetWeatherItems({});
  model->SetReleaseNotesItems({});

  std::vector<BirchTabItem> tab_item_list;
  tab_item_list.emplace_back(u"tab", GURL("https://www.example.com/"),
                             base::Time(), GURL("https://www.favicon.com/"),
                             "session",
                             BirchTabItem::DeviceFormFactor::kDesktop);
  model->SetRecentTabItems(std::move(tab_item_list));

  std::vector<BirchSelfShareItem> self_share_item_list;
  GURL faviconUrl = GURL("https://www.favicon.com/");
  self_share_item_list.emplace_back(u"self share guid", u"self share tab",
                                    GURL("https://www.exampletwo.com/"),
                                    base::Time(), u"my device", faviconUrl,
                                    base::DoNothing());
  model->SetSelfShareItems(std::move(self_share_item_list));

  std::vector<std::unique_ptr<BirchItem>> all_items = model->GetAllItems();
  ASSERT_EQ(all_items.size(), 2u);
  EXPECT_EQ(all_items[0]->GetType(), BirchItemType::kTab);
  EXPECT_EQ(all_items[0]->title(), u"tab");
  EXPECT_EQ(all_items[1]->GetType(), BirchItemType::kSelfShare);
  EXPECT_EQ(all_items[1]->title(), u"self share tab");
}

TEST_F(BirchModelTest, SetClientObservation) {
  BirchModel* model = Shell::Get()->birch_model();
  TestModelObserver test_observer;

  // BirchClient has not been set since observation has started.
  EXPECT_FALSE(test_observer.birch_client_set());

  // Set the client and expect model observer to be notified.
  model->SetClientAndInit(&stub_birch_client_);
  EXPECT_TRUE(test_observer.birch_client_set());
}

TEST_F(BirchModelTest, RemoveItemRecordsHistogram) {
  base::HistogramTester histograms;
  BirchModel* model = Shell::Get()->birch_model();

  // Add a calendar item to the model.
  std::vector<BirchCalendarItem> calendar_item_list =
      MakeCalendarItemList(/*event_count=*/1);
  model->SetCalendarItems(calendar_item_list);

  // Remove the calendar item, as if a user hid the suggestion chip.
  model->RemoveItem(&calendar_item_list[0]);

  // Histogram was recorded.
  histograms.ExpectBucketCount("Ash.Birch.Chip.Hidden",
                               BirchItemType::kCalendar, 1);
}

TEST_F(BirchModelTest, RecordProviderHiddenHistograms) {
  base::HistogramTester histograms;

  // Disable all the prefs, as if the user had hidden each data type.
  PrefService* prefs =
      Shell::Get()->session_controller()->GetPrimaryUserPrefService();
  ASSERT_TRUE(prefs);
  prefs->SetBoolean(prefs::kBirchUseCalendar, false);
  prefs->SetBoolean(prefs::kBirchUseFileSuggest, false);
  prefs->SetBoolean(prefs::kBirchUseRecentTabs, false);
  prefs->SetBoolean(prefs::kBirchUseLastActive, false);
  prefs->SetBoolean(prefs::kBirchUseSelfShare, false);
  prefs->SetBoolean(prefs::kBirchUseReleaseNotes, false);
  prefs->SetBoolean(prefs::kBirchUseWeather, false);

  // Record histograms.
  RecordProviderHiddenHistograms();

  // Histograms are recorded. All types are hidden.
  histograms.ExpectBucketCount("Ash.Birch.ProviderHidden.Calendar", true, 1);
  histograms.ExpectBucketCount("Ash.Birch.ProviderHidden.FileSuggest", true, 1);
  histograms.ExpectBucketCount("Ash.Birch.ProviderHidden.RecentTabs", true, 1);
  histograms.ExpectBucketCount("Ash.Birch.ProviderHidden.LastActive", true, 1);
  histograms.ExpectBucketCount("Ash.Birch.ProviderHidden.SelfShare", true, 1);
  histograms.ExpectBucketCount("Ash.Birch.ProviderHidden.Weather", true, 1);
  histograms.ExpectBucketCount("Ash.Birch.ProviderHidden.ReleaseNotes", true,
                               1);
}

TEST_F(BirchModelTest, LastActiveItemShownByTime) {
  BirchModel* model = Shell::Get()->birch_model();

  // Set the time to morning so that last active items will be ranked.
  test_clock_.SetNow(TimeFromString("22 Feb 2024 7:00 UTC"));

  // Create a last active item.
  std::vector<BirchLastActiveItem> last_active_item_list;
  last_active_item_list.emplace_back(u"last active",
                                     GURL("https://www.example.com/"),
                                     base::Time(), ui::ImageModel());
  model->SetLastActiveItems(std::move(last_active_item_list));

  // The first time we query for items, it is shown.
  std::vector<std::unique_ptr<BirchItem>> all_items = model->GetAllItems();
  ASSERT_EQ(all_items.size(), 1u);
  EXPECT_EQ(all_items[0]->GetType(), BirchItemType::kLastActive);

  // Advance the time by 1 minute.
  test_clock_.Advance(base::Minutes(1));

  // The item is still shown.
  all_items = model->GetAllItems();
  ASSERT_EQ(all_items.size(), 1u);
  EXPECT_EQ(all_items[0]->GetType(), BirchItemType::kLastActive);

  // Advance the time by 2 minutes (for a total of 3, past the threshold for
  // showing most visited items).
  test_clock_.Advance(base::Minutes(2));

  // The item is not shown.
  all_items = model->GetAllItems();
  EXPECT_TRUE(all_items.empty());
}

TEST_F(BirchModelTest, MostVisitedItemShownByTime) {
  BirchModel* model = Shell::Get()->birch_model();

  // Set the time to morning so that most visited items will be ranked.
  test_clock_.SetNow(TimeFromString("22 Feb 2024 7:00 UTC"));

  // Create a most visited item.
  std::vector<BirchMostVisitedItem> most_visited_item_list;
  most_visited_item_list.emplace_back(
      u"most visited", GURL("https://www.example.com/"), ui::ImageModel());
  model->SetMostVisitedItems(std::move(most_visited_item_list));

  // The first time we query for items, it is shown.
  std::vector<std::unique_ptr<BirchItem>> all_items = model->GetAllItems();
  ASSERT_EQ(all_items.size(), 1u);
  EXPECT_EQ(all_items[0]->GetType(), BirchItemType::kMostVisited);

  // Advance the time by 1 minute.
  test_clock_.Advance(base::Minutes(1));

  // The item is still shown.
  all_items = model->GetAllItems();
  ASSERT_EQ(all_items.size(), 1u);
  EXPECT_EQ(all_items[0]->GetType(), BirchItemType::kMostVisited);

  // Advance the time by 2 minutes (for a total of 3, past the threshold for
  // showing most visited items).
  test_clock_.Advance(base::Minutes(2));

  // The item is not shown.
  all_items = model->GetAllItems();
  EXPECT_TRUE(all_items.empty());
}

}  // namespace ash
