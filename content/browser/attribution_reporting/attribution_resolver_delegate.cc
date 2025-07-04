// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/attribution_reporting/attribution_resolver_delegate.h"

#include "base/check.h"
#include "base/notreached.h"
#include "components/attribution_reporting/source_type.mojom.h"
#include "content/browser/attribution_reporting/attribution_config.h"
#include "content/browser/attribution_reporting/attribution_reporting.mojom.h"

namespace content {

namespace {
using ::attribution_reporting::mojom::SourceType;

}  // namespace

AttributionResolverDelegate::AttributionResolverDelegate(
    const AttributionConfig& config)
    : config_(config) {
  DCHECK(config_.Validate());
}

AttributionResolverDelegate::~AttributionResolverDelegate() = default;

int AttributionResolverDelegate::GetMaxSourcesPerOrigin() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return config_.max_sources_per_origin;
}

int AttributionResolverDelegate::GetMaxReportsPerDestination(
    attribution_reporting::mojom::ReportType report_type) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  switch (report_type) {
    case attribution_reporting::mojom::ReportType::kEventLevel:
      return config_.event_level_limit.max_reports_per_destination;
    case attribution_reporting::mojom::ReportType::kAggregatableAttribution:
      return config_.aggregate_limit.max_reports_per_destination;
    case attribution_reporting::mojom::ReportType::kNullAggregatable:
      NOTREACHED_NORETURN();
  }
}

int AttributionResolverDelegate::GetMaxDestinationsPerSourceSiteReportingSite()
    const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return config_.max_destinations_per_source_site_reporting_site;
}

const AttributionConfig::RateLimitConfig&
AttributionResolverDelegate::GetRateLimits() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return config_.rate_limit;
}

double AttributionResolverDelegate::GetMaxChannelCapacity(
    SourceType source_type) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  switch (source_type) {
    case SourceType::kNavigation:
      return config_.event_level_limit.max_navigation_info_gain;
    case SourceType::kEvent:
      return config_.event_level_limit.max_event_info_gain;
  }
}

absl::uint128 AttributionResolverDelegate::GetMaxTriggerStateCardinality()
    const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return config_.event_level_limit.max_trigger_state_cardinality;
}

int AttributionResolverDelegate::GetMaxAggregatableReportsPerSource() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return config_.aggregate_limit.max_aggregatable_reports_per_source;
}

AttributionConfig::DestinationRateLimit
AttributionResolverDelegate::GetDestinationRateLimit() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return config_.destination_rate_limit;
}

AttributionConfig::AggregatableDebugRateLimit
AttributionResolverDelegate::GetAggregatableDebugRateLimit() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return config_.aggregatable_debug_rate_limit;
}

}  // namespace content
