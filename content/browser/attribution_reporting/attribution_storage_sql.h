// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_ATTRIBUTION_REPORTING_ATTRIBUTION_STORAGE_SQL_H_
#define CONTENT_BROWSER_ATTRIBUTION_REPORTING_ATTRIBUTION_STORAGE_SQL_H_

#include <stdint.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/containers/enum_set.h"
#include "base/files/file_path.h"
#include "base/sequence_checker.h"
#include "base/thread_annotations.h"
#include "base/time/time.h"
#include "base/types/expected.h"
#include "content/browser/attribution_reporting/aggregatable_debug_rate_limit_table.h"
#include "content/browser/attribution_reporting/attribution_report.h"
#include "content/browser/attribution_reporting/attribution_resolver.h"
#include "content/browser/attribution_reporting/attribution_trigger.h"
#include "content/browser/attribution_reporting/rate_limit_table.h"
#include "content/browser/attribution_reporting/stored_source.h"
#include "content/common/content_export.h"
#include "content/public/browser/attribution_data_model.h"
#include "content/public/browser/storage_partition.h"
#include "sql/database.h"

namespace sql {
class Statement;
class StatementID;
}  // namespace sql

namespace content {

class AggregatableDebugReport;
class AttributionResolverDelegate;
class StorableSource;
class StoreSourceResult;
struct AttributionInfo;

enum class RateLimitResult : int;

// Provides an implementation of storage that is backed by SQLite.
// This class may be constructed on any sequence but must be accessed and
// destroyed on the same sequence. The sequence must outlive |this|.
class CONTENT_EXPORT AttributionStorageSql {
 public:
  // Version number of the database.
  static constexpr int kCurrentVersionNumber = 62;

  // Earliest version which can use a `kCurrentVersionNumber` database
  // without failing.
  static constexpr int kCompatibleVersionNumber = 62;

  // Latest version of the database that cannot be upgraded to
  // `kCurrentVersionNumber` without razing the database.
  static constexpr int kDeprecatedVersionNumber = 51;

  static_assert(kCompatibleVersionNumber <= kCurrentVersionNumber);
  static_assert(kDeprecatedVersionNumber < kCompatibleVersionNumber);

  // If `user_data_directory` is empty, the DB is created in memory and no data
  // is persisted to disk.
  AttributionStorageSql(const base::FilePath& user_data_directory,
                        AttributionResolverDelegate* delegate);
  AttributionStorageSql(const AttributionStorageSql&) = delete;
  AttributionStorageSql& operator=(const AttributionStorageSql&) = delete;
  AttributionStorageSql(AttributionStorageSql&&) = delete;
  AttributionStorageSql& operator=(AttributionStorageSql&&) = delete;
  ~AttributionStorageSql();

  // These values are persisted to logs. Entries should not be renumbered and
  // numeric values should never be reused.
  enum class InitStatus {
    kSuccess = 0,
    kFailedToOpenDbInMemory = 1,
    kFailedToOpenDbFile = 2,
    kFailedToCreateDir = 3,
    kFailedToInitializeSchema = 4,
    kMaxValue = kFailedToInitializeSchema,
  };
  enum class ReportCorruptionStatus {
    // Tracks total number of corrupted reports for analysis purposes.
    kAnyFieldCorrupted = 0,
    kInvalidFailedSendAttempts = 1,
    kInvalidExternalReportID = 2,
    kInvalidContextOrigin = 3,
    kInvalidReportingOrigin = 4,
    kInvalidReportType = 5,
    kReportingOriginMismatch = 6,
    // Obsolete: kMetadataAsStringFailed = 7,
    kSourceDataMissingEventLevel = 8,
    kSourceDataMissingAggregatable = 9,
    kSourceDataFoundNullAggregatable = 10,
    kInvalidMetadata = 11,
    kSourceNotFound = 12,
    kSourceInvalidSourceOrigin = 13,
    kSourceInvalidReportingOrigin = 14,
    kSourceInvalidSourceType = 15,
    kSourceInvalidAttributionLogic = 16,
    kSourceInvalidNumConversions = 17,
    kSourceInvalidNumAggregatableReports = 18,
    kSourceInvalidAggregationKeys = 19,
    kSourceInvalidFilterData = 20,
    kSourceInvalidActiveState = 21,
    kSourceInvalidReadOnlySourceData = 22,
    // Obsolete: kSourceInvalidEventReportWindows = 23,
    kSourceInvalidMaxEventLevelReports = 24,
    kSourceInvalidEventLevelEpsilon = 25,
    kSourceDestinationSitesQueryFailed = 26,
    kSourceInvalidDestinationSites = 27,
    kStoredSourceConstructionFailed = 28,
    kSourceInvalidTriggerSpecs = 29,
    kMaxValue = kSourceInvalidTriggerSpecs,
  };

  struct DeletionCounts {
    int sources = 0;
    int reports = 0;
  };

  struct AggregatableDebugSourceData {
    int remaining_budget;
    int num_reports;
  };

  // Deletes corrupt sources/reports if `deletion_counts` is not `nullptr`.
  void VerifyReports(DeletionCounts* deletion_counts);

  StoreSourceResult StoreSource(StorableSource source);
  CreateReportResult MaybeCreateAndStoreReport(AttributionTrigger);
  std::vector<AttributionReport> GetAttributionReports(
      base::Time max_report_time,
      int limit = -1);
  std::optional<base::Time> GetNextReportTime(base::Time time);
  std::optional<AttributionReport> GetReport(AttributionReport::Id);
  std::vector<StoredSource> GetActiveSources(int limit = -1);
  std::set<AttributionDataModel::DataKey> GetAllDataKeys();
  bool DeleteReport(AttributionReport::Id report_id);
  bool UpdateReportForSendFailure(AttributionReport::Id report_id,
                                  base::Time new_report_time);
  std::optional<base::Time> AdjustOfflineReportTimes();
  void ClearAllDataAllTime(bool delete_rate_limit_data);
  void ClearDataWithFilter(base::Time delete_begin,
                           base::Time delete_end,
                           StoragePartition::StorageKeyMatcherFunction filter,
                           bool delete_rate_limit_data);
  [[nodiscard]] std::optional<AggregatableDebugSourceData>
      GetAggregatableDebugSourceData(StoredSource::Id);
  [[nodiscard]] AggregatableDebugRateLimitTable::Result
  AggregatableDebugReportAllowedForRateLimit(const AggregatableDebugReport&);
  [[nodiscard]] bool AdjustForAggregatableDebugReport(
      const AggregatableDebugReport&,
      std::optional<StoredSource::Id>);
  void SetDelegate(AttributionResolverDelegate*);

 private:
  using ReportCorruptionStatusSet =
      base::EnumSet<ReportCorruptionStatus,
                    ReportCorruptionStatus::kAnyFieldCorrupted,
                    ReportCorruptionStatus::kMaxValue>;

  struct ReportCorruptionStatusSetAndIds;
  struct StoredSourceData;

  enum class DbStatus {
    kOpen,
    // The database has never been created, i.e. there is no database file at
    // all.
    kDeferringCreation,
    // The database exists but is not open yet.
    kDeferringOpen,
    // The database initialization failed, or the db suffered from an
    // unrecoverable, but potentially transient, error.
    kClosed,
    // The database initialization failed, or the db suffered from a
    // catastrophic failure.
    kClosedDueToCatastrophicError,
  };

  enum class DbCreationPolicy {
    // Create the db if it does not exist.
    kCreateIfAbsent,
    // Do not create the db if it does not exist.
    kIgnoreIfAbsent,
  };

  // Deactivates the given sources. Returns false on error.
  [[nodiscard]] bool DeactivateSources(
      const std::vector<StoredSource::Id>& sources)
      VALID_CONTEXT_REQUIRED(sequence_checker_);

  // Returns false on failure.
  [[nodiscard]] bool DeleteSources(
      const std::vector<StoredSource::Id>& source_ids)
      VALID_CONTEXT_REQUIRED(sequence_checker_);

  // Deletes all sources that have expired and have no pending
  // reports. Returns false on failure.
  [[nodiscard]] bool DeleteExpiredSources()
      VALID_CONTEXT_REQUIRED(sequence_checker_);

  bool HasCapacityForStoringSource(const std::string& serialized_origin,
                                   base::Time now)
      VALID_CONTEXT_REQUIRED(sequence_checker_);

  // Returns the number of sources in storage.
  std::optional<int64_t> NumberOfSources()
      VALID_CONTEXT_REQUIRED(sequence_checker_);

  enum class ReportAlreadyStoredStatus {
    kNotStored,
    kStored,
    kError,
  };

  void RecordSourcesPerSourceOrigin() VALID_CONTEXT_REQUIRED(sequence_checker_);

  ReportAlreadyStoredStatus ReportAlreadyStored(
      StoredSource::Id source_id,
      std::optional<uint64_t> dedup_key,
      AttributionReport::Type report_type)
      VALID_CONTEXT_REQUIRED(sequence_checker_);

  enum class ConversionCapacityStatus {
    kHasCapacity,
    kNoCapacity,
    kError,
  };

  ConversionCapacityStatus CapacityForStoringReport(
      const url::Origin& context_origin,
      AttributionReport::Type) VALID_CONTEXT_REQUIRED(sequence_checker_);

  enum class ReplaceReportResult {
    kError,
    kAddNewReport,
    kDropNewReport,
    kDropNewReportSourceDeactivated,
    kReplaceOldReport,
  };
  [[nodiscard]] ReplaceReportResult MaybeReplaceLowerPriorityEventLevelReport(
      const AttributionReport& report,
      int num_attributions,
      int64_t conversion_priority,
      std::optional<AttributionReport>& replaced_report)
      VALID_CONTEXT_REQUIRED(sequence_checker_);

  std::optional<AttributionReport> GetReportInternal(AttributionReport::Id)
      VALID_CONTEXT_REQUIRED(sequence_checker_);

  [[nodiscard]] bool ReadDedupKeys(StoredSource&)
      VALID_CONTEXT_REQUIRED(sequence_checker_);

  bool StoreDedupKey(StoredSource::Id source_id,
                     uint64_t dedup_key,
                     AttributionReport::Type report_type)
      VALID_CONTEXT_REQUIRED(sequence_checker_);

  base::expected<AttributionReport, ReportCorruptionStatusSetAndIds>
  ReadReportFromStatement(sql::Statement&)
      VALID_CONTEXT_REQUIRED(sequence_checker_);

  base::expected<StoredSourceData, ReportCorruptionStatusSetAndIds>
  ReadSourceFromStatement(sql::Statement&)
      VALID_CONTEXT_REQUIRED(sequence_checker_);

  std::optional<StoredSourceData> ReadSourceToAttribute(
      StoredSource::Id source_id) VALID_CONTEXT_REQUIRED(sequence_checker_);

  std::vector<AttributionReport> GetReportsInternal(base::Time max_report_time,
                                                    int limit)
      VALID_CONTEXT_REQUIRED(sequence_checker_);

  [[nodiscard]] bool DeleteReportInternal(AttributionReport::Id)
      VALID_CONTEXT_REQUIRED(sequence_checker_);

  std::optional<base::Time> GetNextReportTime(sql::StatementID id,
                                              const char* sql,
                                              base::Time time)
      VALID_CONTEXT_REQUIRED(sequence_checker_);

  [[nodiscard]] bool AdjustOfflineReportTimes(sql::StatementID id,
                                              const char* sql,
                                              base::TimeDelta min_delay,
                                              base::TimeDelta max_delay,
                                              base::Time now)
      VALID_CONTEXT_REQUIRED(sequence_checker_);

  // Returns whether the database execution was successful.
  // `source_id_to_attribute` and `source_ids_to_delete` would be populated if
  // matching sources were found.
  bool FindMatchingSourceForTrigger(
      const AttributionTrigger& trigger,
      base::Time trigger_time,
      std::optional<StoredSource::Id>& source_id_to_attribute,
      std::vector<StoredSource::Id>& source_ids_to_delete,
      std::vector<StoredSource::Id>& source_ids_to_deactivate)
      VALID_CONTEXT_REQUIRED(sequence_checker_);

  AttributionTrigger::EventLevelResult MaybeCreateEventLevelReport(
      const AttributionInfo& attribution_info,
      const StoredSource&,
      const AttributionTrigger& trigger,
      std::optional<AttributionReport>& report,
      std::optional<uint64_t>& dedup_key,
      std::optional<int64_t>& rate_limits_max_attributions)
      VALID_CONTEXT_REQUIRED(sequence_checker_);

  AttributionTrigger::EventLevelResult MaybeStoreEventLevelReport(
      AttributionReport& report,
      std::optional<uint64_t> dedup_key,
      int num_attributions,
      std::optional<AttributionReport>& replaced_report,
      std::optional<AttributionReport>& dropped_report,
      std::optional<int>& max_event_level_reports_per_destination)
      VALID_CONTEXT_REQUIRED(sequence_checker_);

  // Initializes the database if necessary, and returns whether the database is
  // open. |should_create| indicates whether the database should be created if
  // it is not already.
  [[nodiscard]] bool LazyInit(DbCreationPolicy creation_policy)
      VALID_CONTEXT_REQUIRED(sequence_checker_);
  // Returns false on failure.
  [[nodiscard]] bool InitializeSchema(bool db_empty)
      VALID_CONTEXT_REQUIRED(sequence_checker_);
  // Returns false on failure.
  [[nodiscard]] bool CreateSchema() VALID_CONTEXT_REQUIRED(sequence_checker_);
  void HandleInitializationFailure(const InitStatus status)
      VALID_CONTEXT_REQUIRED(sequence_checker_);

  void DatabaseErrorCallback(int extended_error, sql::Statement* stmt);

  // Deletes all event-level and aggregatable reports in storage for storage
  // keys matching `filter`, between `delete_begin` and `delete_end` time. More
  // specifically, this:
  // 1. Deletes all sources within the time range. If any report is attributed
  // to this source it is also deleted.
  // 2. Deletes all reports within the time range. All sources these reports
  // attributed to are also deleted.
  //
  // All sources to be deleted are updated in `source_ids_to_delete`.
  // Returns false on failure.
  [[nodiscard]] bool ClearReportsForOriginsInRange(
      base::Time delete_begin,
      base::Time delete_end,
      StoragePartition::StorageKeyMatcherFunction filter,
      std::vector<StoredSource::Id>& source_ids_to_delete,
      int& num_event_reports_deleted,
      int& num_aggregatable_reports_deleted)
      VALID_CONTEXT_REQUIRED(sequence_checker_);

  [[nodiscard]] bool ClearReportsForSourceIds(
      const std::vector<StoredSource::Id>& source_ids,
      int& num_event_reports_deleted,
      int& num_aggregatable_reports_deleted)
      VALID_CONTEXT_REQUIRED(sequence_checker_);

  // Aggregate Attribution:

  // Checks if the given aggregatable attribution is allowed according to the
  // L1 budget policy specified by the delegate.
  RateLimitResult AggregatableAttributionAllowedForBudgetLimit(
      const AttributionReport::AggregatableAttributionData&
          aggregatable_attribution,
      int remaining_aggregatable_attribution_budget)
      VALID_CONTEXT_REQUIRED(sequence_checker_);

  // Adjusts the aggregatable budget for the source event by
  // `additional_budget_consumed`.
  [[nodiscard]] bool AdjustBudgetConsumedForSource(
      StoredSource::Id source_id,
      int additional_budget_consumed) VALID_CONTEXT_REQUIRED(sequence_checker_);

  AttributionTrigger::AggregatableResult
  MaybeCreateAggregatableAttributionReport(
      const AttributionInfo& attribution_info,
      const StoredSource&,
      const AttributionTrigger& trigger,
      std::optional<AttributionReport>& report,
      std::optional<uint64_t>& dedup_key,
      std::optional<int>& max_aggregatable_reports_per_destination,
      std::optional<int64_t>& rate_limits_max_attributions)
      VALID_CONTEXT_REQUIRED(sequence_checker_);

  // Stores the data associated with the aggregatable report, e.g. budget
  // consumed and dedup keys. The report itself will be stored in
  // `GenerateNullAggregatableReportsAndStoreReports()`.
  AttributionTrigger::AggregatableResult
  MaybeStoreAggregatableAttributionReportData(
      AttributionReport& report,
      int remaining_aggregatable_attribution_budget,
      int num_aggregatable_attribution_reports,
      std::optional<uint64_t> dedup_key,
      std::optional<int>& max_aggregatable_reports_per_source)
      VALID_CONTEXT_REQUIRED(sequence_checker_);

  [[nodiscard]] bool StoreAttributionReport(AttributionReport& report)
      VALID_CONTEXT_REQUIRED(sequence_checker_);

  // Generates null aggregatable reports for the given trigger, assigns
  // verification data to null aggregatable reports and the real aggregatable
  // report if created, and stores all those reports.
  [[nodiscard]] bool GenerateNullAggregatableReportsAndStoreReports(
      const AttributionTrigger&,
      const AttributionInfo&,
      std::optional<AttributionReport>& new_aggregatable_report,
      std::optional<base::Time>& min_null_aggregatable_report_time)
      VALID_CONTEXT_REQUIRED(sequence_checker_);

  // Randomly assigns trigger verification data to the given reports.
  void AssignTriggerVerificationData(std::vector<AttributionReport>&,
                                     const AttributionTrigger&)
      VALID_CONTEXT_REQUIRED(sequence_checker_);

  base::Time GetAggregatableReportTime(const AttributionTrigger&,
                                       base::Time trigger_time) const
      VALID_CONTEXT_REQUIRED(sequence_checker_);

  [[nodiscard]] bool AdjustAggregatableDebugSourceData(
      StoredSource::Id,
      int additional_budget_consumed) VALID_CONTEXT_REQUIRED(sequence_checker_);

  const base::FilePath path_to_database_;

  // Current status of the database initialization. Tracks what stage |this| is
  // at for lazy initialization, and used as a signal for if the database is
  // closed. This is initialized in the first call to LazyInit() to avoid doing
  // additional work in the constructor, see https://crbug.com/1121307.
  std::optional<DbStatus> db_status_ GUARDED_BY_CONTEXT(sequence_checker_);

  sql::Database db_ GUARDED_BY_CONTEXT(sequence_checker_);

  raw_ptr<AttributionResolverDelegate> delegate_
      GUARDED_BY_CONTEXT(sequence_checker_);

  // Table which stores timestamps of sent reports, and checks if new reports
  // can be created given API rate limits. The underlying table is created in
  // |db_|, but only accessed within |RateLimitTable|.
  // `rate_limit_table_` references `delegate_` So it must be declared after it.
  RateLimitTable rate_limit_table_ GUARDED_BY_CONTEXT(sequence_checker_);

  // `aggregatable_rate_limit_table_` references `delegate_` So it must be
  // declared after it.
  AggregatableDebugRateLimitTable aggregatable_debug_rate_limit_table_
      GUARDED_BY_CONTEXT(sequence_checker_);

  // Time at which `DeleteExpiredSources()` was last called. Initialized to
  // the NULL time.
  base::Time last_deleted_expired_sources_
      GUARDED_BY_CONTEXT(sequence_checker_);

  SEQUENCE_CHECKER(sequence_checker_);
};

}  // namespace content

#endif  // CONTENT_BROWSER_ATTRIBUTION_REPORTING_ATTRIBUTION_STORAGE_SQL_H_
