// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/private_aggregation/private_aggregation_host.h"

#include <stddef.h>

#include <bit>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/check.h"
#include "base/check_op.h"
#include "base/command_line.h"
#include "base/containers/flat_map.h"
#include "base/containers/span.h"
#include "base/feature_list.h"
#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/metrics/histogram_functions.h"
#include "base/not_fatal_until.h"
#include "base/rand_util.h"
#include "base/ranges/algorithm.h"
#include "base/time/time.h"
#include "base/timer/elapsed_timer.h"
#include "base/timer/timer.h"
#include "base/uuid.h"
#include "base/values.h"
#include "components/aggregation_service/aggregation_coordinator_utils.h"
#include "content/browser/aggregation_service/aggregatable_report.h"
#include "content/browser/aggregation_service/aggregation_service_features.h"
#include "content/browser/private_aggregation/private_aggregation_budget_key.h"
#include "content/browser/private_aggregation/private_aggregation_budgeter.h"
#include "content/browser/private_aggregation/private_aggregation_features.h"
#include "content/browser/private_aggregation/private_aggregation_utils.h"
#include "content/public/browser/content_browser_client.h"
#include "content/public/common/content_client.h"
#include "content/public/common/content_switches.h"
#include "mojo/public/cpp/bindings/message.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/receiver_set.h"
#include "services/network/public/cpp/is_potentially_trustworthy.h"
#include "third_party/blink/public/common/features.h"
#include "third_party/blink/public/mojom/aggregation_service/aggregatable_report.mojom.h"
#include "third_party/blink/public/mojom/private_aggregation/private_aggregation_host.mojom.h"
#include "url/origin.h"

namespace content {

namespace {

// Helper to add a random delay to reports being sent. This delay is picked
// uniformly at random from the range [10 minutes, 1 hour).
// TODO(alexmt): Consider making this configurable for easier testing.
base::Time GetScheduledReportTime(base::Time report_issued_time) {
  return report_issued_time + base::Minutes(10) +
         base::RandDouble() * base::Minutes(50);
}

void RecordPipeResultHistogram(PrivateAggregationHost::PipeResult result) {
  base::UmaHistogramEnumeration(
      "PrivacySandbox.PrivateAggregation.Host.PipeResult", result);
}

void RecordTimeoutResultHistogram(
    PrivateAggregationHost::TimeoutResult result) {
  base::UmaHistogramEnumeration(
      "PrivacySandbox.PrivateAggregation.Host.TimeoutResult", result);
}

void RecordFilteringIdStatusHistogram(bool has_filtering_id,
                                      bool has_custom_max_bytes) {
  PrivateAggregationHost::FilteringIdStatus status;

  if (has_filtering_id) {
    if (has_custom_max_bytes) {
      status = PrivateAggregationHost::FilteringIdStatus::
          kFilteringIdProvidedWithCustomMaxBytes;
    } else {
      status = PrivateAggregationHost::FilteringIdStatus::
          kFilteringIdProvidedWithDefaultMaxBytes;
    }
  } else {
    if (has_custom_max_bytes) {
      status = PrivateAggregationHost::FilteringIdStatus::
          kNoFilteringIdWithCustomMaxBytes;
    } else {
      status = PrivateAggregationHost::FilteringIdStatus::
          kNoFilteringIdWithDefaultMaxBytes;
    }
  }
  base::UmaHistogramEnumeration(
      "PrivacySandbox.PrivateAggregation.Host.FilteringIdStatus", status);
}

}  // namespace

struct PrivateAggregationHost::ReceiverContext {
  url::Origin worklet_origin;
  url::Origin top_frame_origin;
  PrivateAggregationBudgetKey::Api api_for_budgeting;
  std::optional<std::string> context_id;
  std::optional<url::Origin> aggregation_coordinator_origin;
  size_t filtering_id_max_bytes;

  // If contributions have been truncated, tracks this for triggering the right
  // histogram value.
  bool too_many_contributions = false;

  // Contributions passed to `ContributeToHistogram()` for this receiver.
  std::vector<blink::mojom::AggregatableReportHistogramContribution>
      contributions;

  // The debug mode details to use if a non-null report is sent. Cannot be null.
  blink::mojom::DebugModeDetailsPtr report_debug_details =
      blink::mojom::DebugModeDetails::New();

  // If a timeout is specified by the client, this timer will be used to
  // schedule the timeout task. This should be nullptr iff no timeout is
  // specified by the client.
  std::unique_ptr<base::OneShotTimer> timeout_timer;
};

PrivateAggregationHost::PrivateAggregationHost(
    base::RepeatingCallback<
        void(ReportRequestGenerator,
             std::vector<blink::mojom::AggregatableReportHistogramContribution>,
             PrivateAggregationBudgetKey,
             PrivateAggregationBudgeter::BudgetDeniedBehavior)>
        on_report_request_details_received,
    BrowserContext* browser_context)
    : should_not_delay_reports_(
          base::CommandLine::ForCurrentProcess()->HasSwitch(
              switches::kPrivateAggregationDeveloperMode)),
      on_report_request_details_received_(
          std::move(on_report_request_details_received)),
      browser_context_(*browser_context) {
  CHECK(browser_context);
  CHECK(!on_report_request_details_received_.is_null());

  // `base::Unretained()` is safe as `receiver_set_` is owned by `this`.
  receiver_set_.set_disconnect_handler(base::BindRepeating(
      &PrivateAggregationHost::OnReceiverDisconnected, base::Unretained(this)));
}

PrivateAggregationHost::~PrivateAggregationHost() {
  CHECK_GE(pipes_with_timeout_count_, 0);
  for (int i = 0; i < pipes_with_timeout_count_; ++i) {
    RecordTimeoutResultHistogram(TimeoutResult::kStillScheduledOnShutdown);
  }

  if (pipe_duration_timers_.empty()) {
    return;
  }
  for (auto& [id, elapsed_timer] : pipe_duration_timers_) {
    base::UmaHistogramLongTimes(
        "PrivacySandbox.PrivateAggregation.Host.PipeOpenDurationOnShutdown",
        elapsed_timer.Elapsed());
  }
}

bool PrivateAggregationHost::BindNewReceiver(
    url::Origin worklet_origin,
    url::Origin top_frame_origin,
    PrivateAggregationBudgetKey::Api api_for_budgeting,
    std::optional<std::string> context_id,
    std::optional<base::TimeDelta> timeout,
    std::optional<url::Origin> aggregation_coordinator_origin,
    size_t filtering_id_max_bytes,
    mojo::PendingReceiver<blink::mojom::PrivateAggregationHost>
        pending_receiver) {
  // If rejected, let the pending receiver be destroyed as it goes out of scope
  // so none of its requests are processed.
  if (!network::IsOriginPotentiallyTrustworthy(worklet_origin)) {
    return false;
  }
  if (context_id.has_value() &&
      context_id.value().size() > kMaxContextIdLength) {
    return false;
  }

  if (aggregation_coordinator_origin.has_value() &&
      !aggregation_service::IsAggregationCoordinatorOriginAllowed(
          aggregation_coordinator_origin.value())) {
    return false;
  }

  if (!base::FeatureList::IsEnabled(
          blink::features::kPrivateAggregationApiFilteringIds)) {
    filtering_id_max_bytes = kDefaultFilteringIdMaxBytes;
  }
  if (filtering_id_max_bytes < 1 ||
      filtering_id_max_bytes >
          AggregationServicePayloadContents::kMaximumFilteringIdMaxBytes) {
    return false;
  }

  // Timeouts should only be set for deterministic reports.
  // TODO(alexmt): Consider requiring timeouts for deterministic reports.
  if (timeout.has_value() && !context_id.has_value() &&
      filtering_id_max_bytes == kDefaultFilteringIdMaxBytes) {
    return false;
  }
  mojo::ReceiverId id = receiver_set_.Add(
      this, std::move(pending_receiver),
      ReceiverContext{.worklet_origin = std::move(worklet_origin),
                      .top_frame_origin = std::move(top_frame_origin),
                      .api_for_budgeting = api_for_budgeting,
                      .context_id = std::move(context_id),
                      .aggregation_coordinator_origin =
                          std::move(aggregation_coordinator_origin),
                      .filtering_id_max_bytes = filtering_id_max_bytes});

  ReceiverContext* receiver_context_raw_ptr = receiver_set_.GetContext(id);

  if (timeout) {
    CHECK(timeout->is_positive());

    pipes_with_timeout_count_++;
    receiver_context_raw_ptr->timeout_timer =
        std::make_unique<base::OneShotTimer>();

    // Passing `base::Unretained(this)` is safe as `this` owns the receiver
    // context and the receiver context owns the timer.
    receiver_context_raw_ptr->timeout_timer->Start(
        FROM_HERE, *timeout,
        base::BindOnce(&PrivateAggregationHost::OnTimeoutBeforeDisconnect,
                       base::Unretained(this), id));
  }

  auto emplace_result = pipe_duration_timers_.emplace(id, base::ElapsedTimer());
  CHECK(emplace_result.second);  // The ID should not already be present.

  return true;
}

bool PrivateAggregationHost::IsDebugModeAllowed(
    const url::Origin& top_frame_origin,
    const url::Origin& reporting_origin) {
  if (!blink::features::kPrivateAggregationApiDebugModeEnabledAtAll.Get()) {
    return false;
  }

  if (!base::FeatureList::IsEnabled(
          kPrivateAggregationApiDebugModeRequires3pcEligibility)) {
    return true;
  }

  return GetContentClient()->browser()->IsPrivateAggregationDebugModeAllowed(
      &*browser_context_, top_frame_origin, reporting_origin);
}

void PrivateAggregationHost::ContributeToHistogram(
    std::vector<blink::mojom::AggregatableReportHistogramContributionPtr>
        contribution_ptrs) {
  const url::Origin& reporting_origin =
      receiver_set_.current_context().worklet_origin;
  CHECK(network::IsOriginPotentiallyTrustworthy(reporting_origin),
        base::NotFatalUntil::M128);

  if (!GetContentClient()->browser()->IsPrivateAggregationAllowed(
          &*browser_context_, receiver_set_.current_context().top_frame_origin,
          reporting_origin, /*out_block_is_site_setting_specific=*/nullptr)) {
    CloseCurrentPipe(PipeResult::kApiDisabledInSettings);
    return;
  }

  using Contribution = blink::mojom::AggregatableReportHistogramContribution;
  using ContributionPtr =
      blink::mojom::AggregatableReportHistogramContributionPtr;

  base::span<ContributionPtr> incoming_ptrs{contribution_ptrs};
  std::vector<Contribution>& accepted =
      receiver_set_.current_context().contributions;

  // Null pointers should fail mojo validation.
  CHECK(base::ranges::none_of(incoming_ptrs, &ContributionPtr::is_null),
        base::NotFatalUntil::M128);

  if (base::ranges::any_of(incoming_ptrs,
                           [](const ContributionPtr& contribution) {
                             return contribution->value < 0;
                           })) {
    mojo::ReportBadMessage("Negative value encountered");
    CloseCurrentPipe(PipeResult::kNegativeValue);
    return;
  }

  if (base::FeatureList::IsEnabled(
          blink::features::kPrivateAggregationApiFilteringIds) &&
      base::ranges::any_of(
          incoming_ptrs, [&](const ContributionPtr& contribution) {
            return static_cast<size_t>(
                       std::bit_width(contribution->filtering_id.value_or(0))) >
                   8 * receiver_set_.current_context().filtering_id_max_bytes;
          })) {
    mojo::ReportBadMessage("Filtering ID too big for max bytes");
    CloseCurrentPipe(PipeResult::kFilteringIdInvalid);
    return;
  }

  // TODO(alexmt): Consider eliding contributions with values of zero as well as
  // potentially merging contributions with the same bucket (although that
  // should probably be done after budgeting).

  CHECK_LE(accepted.size(), kMaxNumberOfContributions);
  const size_t num_remaining = kMaxNumberOfContributions - accepted.size();

  if (incoming_ptrs.size() > num_remaining) {
    receiver_set_.current_context().too_many_contributions = true;
    incoming_ptrs = incoming_ptrs.first(num_remaining);
  }

  base::ranges::transform(incoming_ptrs, std::back_inserter(accepted),
                          &ContributionPtr::operator*);
}

AggregatableReportRequest PrivateAggregationHost::GenerateReportRequest(
    base::ElapsedTimer timeout_or_disconnect_timer,
    blink::mojom::DebugModeDetailsPtr debug_mode_details,
    base::Time scheduled_report_time,
    base::Uuid report_id,
    const url::Origin& reporting_origin,
    PrivateAggregationBudgetKey::Api api_for_budgeting,
    std::optional<std::string> context_id,
    std::optional<url::Origin> aggregation_coordinator_origin,
    size_t specified_filtering_id_max_bytes,
    std::vector<blink::mojom::AggregatableReportHistogramContribution>
        contributions) {
  CHECK(context_id.has_value() || !contributions.empty());
  CHECK(debug_mode_details);

  bool use_new_report_version =
      base::FeatureList::IsEnabled(
          blink::features::kPrivateAggregationApiFilteringIds) &&
      base::FeatureList::IsEnabled(
          kPrivacySandboxAggregationServiceFilteringIds);

  std::optional<size_t> applied_filtering_id_max_bytes =
      specified_filtering_id_max_bytes;
  if (use_new_report_version) {
    RecordFilteringIdStatusHistogram(
        /*has_filtering_id=*/base::ranges::any_of(
            contributions,
            [](blink::mojom::AggregatableReportHistogramContribution&
                   contribution) {
              return contribution.filtering_id.has_value();
            }),
        /*has_custom_max_bytes=*/specified_filtering_id_max_bytes !=
            kDefaultFilteringIdMaxBytes);
  } else {
    applied_filtering_id_max_bytes.reset();
    base::ranges::for_each(
        contributions,
        [](blink::mojom::AggregatableReportHistogramContribution&
               contribution) { contribution.filtering_id.reset(); });
  }

  AggregationServicePayloadContents payload_contents(
      AggregationServicePayloadContents::Operation::kHistogram,
      std::move(contributions),
      // TODO(alexmt): Consider allowing this to be set.
      blink::mojom::AggregationServiceMode::kDefault,
      std::move(aggregation_coordinator_origin), kMaxNumberOfContributions,
      applied_filtering_id_max_bytes);

  AggregatableReportSharedInfo shared_info(
      scheduled_report_time, std::move(report_id), reporting_origin,
      debug_mode_details->is_enabled
          ? AggregatableReportSharedInfo::DebugMode::kEnabled
          : AggregatableReportSharedInfo::DebugMode::kDisabled,
      /*additional_fields=*/base::Value::Dict(),
      /*api_version=*/
      use_new_report_version ? kApiReportVersionWithFilteringId
                             : kApiReportVersionWithoutFilteringId,
      /*api_identifier=*/
      private_aggregation::GetApiIdentifier(api_for_budgeting));

  std::string reporting_path = private_aggregation::GetReportingPath(
      api_for_budgeting,
      /*is_immediate_debug_report=*/false);

  std::optional<uint64_t> debug_key;
  if (!debug_mode_details->debug_key.is_null()) {
    CHECK(debug_mode_details->is_enabled);
    debug_key = debug_mode_details->debug_key->value;
  }

  base::flat_map<std::string, std::string> additional_fields;
  if (context_id.has_value()) {
    additional_fields["context_id"] = context_id.value();
  }

  std::optional<AggregatableReportRequest> report_request =
      AggregatableReportRequest::Create(
          std::move(payload_contents), std::move(shared_info),
          std::move(reporting_path), debug_key, std::move(additional_fields));

  // All failure cases should've been handled by earlier validation code.
  CHECK(report_request.has_value());

  if (context_id.has_value()) {
    base::UmaHistogramTimes(
        "PrivacySandbox.PrivateAggregation.Host."
        "TimeToGenerateReportRequestWithContextId",
        timeout_or_disconnect_timer.Elapsed());
  }

  return std::move(report_request).value();
}

void PrivateAggregationHost::EnableDebugMode(
    blink::mojom::DebugKeyPtr debug_key) {
  if (receiver_set_.current_context().report_debug_details->is_enabled) {
    mojo::ReportBadMessage("EnableDebugMode() called multiple times");
    CloseCurrentPipe(PipeResult::kEnableDebugModeCalledMultipleTimes);
    return;
  }

  receiver_set_.current_context().report_debug_details->is_enabled = true;
  receiver_set_.current_context().report_debug_details->debug_key =
      std::move(debug_key);
}

void PrivateAggregationHost::CloseCurrentPipe(PipeResult pipe_result) {
  // We should only reach here after an error.
  CHECK_NE(pipe_result, PipeResult::kReportSuccess);
  CHECK_NE(pipe_result,
           PipeResult::kReportSuccessButTruncatedDueToTooManyContributions);
  CHECK_NE(pipe_result, PipeResult::kNoReportButNoError);

  RecordPipeResultHistogram(pipe_result);

  if (receiver_set_.current_context().timeout_timer) {
    CHECK(receiver_set_.current_context().timeout_timer->IsRunning());
    pipes_with_timeout_count_--;
    RecordTimeoutResultHistogram(TimeoutResult::kCanceledDueToError);
  }

  mojo::ReceiverId current_receiver = receiver_set_.current_receiver();
  receiver_set_.Remove(current_receiver);
  pipe_duration_timers_.erase(current_receiver);
}

void PrivateAggregationHost::OnTimeoutBeforeDisconnect(mojo::ReceiverId id) {
  ReceiverContext* receiver_context = receiver_set_.GetContext(id);

  SendReportOnTimeoutOrDisconnect(*receiver_context,
                                  /*remaining_timeout=*/base::TimeDelta());

  pipes_with_timeout_count_--;
  RecordTimeoutResultHistogram(
      TimeoutResult::kOccurredBeforeRemoteDisconnection);

  receiver_set_.Remove(id);
  pipe_duration_timers_.erase(id);
}

void PrivateAggregationHost::OnReceiverDisconnected() {
  pipe_duration_timers_.erase(receiver_set_.current_receiver());

  ReceiverContext& current_context = receiver_set_.current_context();
  if (!current_context.timeout_timer) {
    SendReportOnTimeoutOrDisconnect(current_context,
                                    /*remaining_timeout=*/base::TimeDelta());
    return;
  }

  // The timeout hasn't been reached.
  CHECK(current_context.timeout_timer->IsRunning());
  pipes_with_timeout_count_--;

  RecordTimeoutResultHistogram(
      TimeoutResult::kOccurredAfterRemoteDisconnection);

  base::TimeDelta remaining_timeout =
      current_context.timeout_timer->desired_run_time() -
      base::TimeTicks::Now();

  SendReportOnTimeoutOrDisconnect(current_context, remaining_timeout);
}

void PrivateAggregationHost::SendReportOnTimeoutOrDisconnect(
    ReceiverContext& receiver_context,
    base::TimeDelta remaining_timeout) {
  base::ElapsedTimer timeout_or_disconnect_timer;

  const url::Origin& reporting_origin = receiver_context.worklet_origin;
  CHECK(network::IsOriginPotentiallyTrustworthy(reporting_origin),
        base::NotFatalUntil::M128);

  if (!GetContentClient()->browser()->IsPrivateAggregationAllowed(
          &*browser_context_, receiver_context.top_frame_origin,
          reporting_origin, /*out_block_is_site_setting_specific=*/nullptr)) {
    // No need to remove the pipe from `receiver_set_` as it's already
    // disconnected or will get disconnected synchronously.
    RecordPipeResultHistogram(PipeResult::kApiDisabledInSettings);
    return;
  }

  if (receiver_context.report_debug_details->is_enabled &&
      !IsDebugModeAllowed(receiver_context.top_frame_origin,
                          reporting_origin)) {
    receiver_context.report_debug_details =
        blink::mojom::DebugModeDetails::New();
  }

  PrivateAggregationBudgeter::BudgetDeniedBehavior budget_denied_behavior =
      receiver_context.context_id.has_value()
          ? PrivateAggregationBudgeter::BudgetDeniedBehavior::kSendNullReport
          : PrivateAggregationBudgeter::BudgetDeniedBehavior::kDontSendReport;

  if (receiver_context.contributions.empty()) {
    if (!receiver_context.context_id.has_value()) {
      RecordPipeResultHistogram(PipeResult::kNoReportButNoError);
      return;
    }

    // Null reports caused by no contributions never have debug mode enabled.
    // TODO(crbug.com/40276453): Consider permitting this.
    receiver_context.report_debug_details =
        blink::mojom::DebugModeDetails::New();
  }

  base::Time now = base::Time::Now();

  // If the timeout hasn't been reached, use a modified report issued time.
  base::Time report_issued_time = now + remaining_timeout;

  bool should_not_delay_this_report =
      should_not_delay_reports_ || receiver_context.timeout_timer;

  ReportRequestGenerator report_request_generator = base::BindOnce(
      GenerateReportRequest, std::move(timeout_or_disconnect_timer),
      std::move(receiver_context.report_debug_details),
      /*scheduled_report_time=*/
      should_not_delay_this_report ? report_issued_time
                                   : GetScheduledReportTime(report_issued_time),
      /*report_id=*/base::Uuid::GenerateRandomV4(), reporting_origin,
      receiver_context.api_for_budgeting,
      std::move(receiver_context.context_id),
      std::move(receiver_context.aggregation_coordinator_origin),
      receiver_context.filtering_id_max_bytes);

  RecordPipeResultHistogram(
      receiver_context.too_many_contributions
          ? PipeResult::kReportSuccessButTruncatedDueToTooManyContributions
          : PipeResult::kReportSuccess);

  std::optional<PrivateAggregationBudgetKey> budget_key =
      PrivateAggregationBudgetKey::Create(
          /*origin=*/reporting_origin, /*api_invocation_time=*/now,
          /*api=*/receiver_context.api_for_budgeting);

  // The origin should be potentially trustworthy.
  CHECK(budget_key.has_value(), base::NotFatalUntil::M128);

  on_report_request_details_received_.Run(
      std::move(report_request_generator),
      std::move(receiver_context.contributions), std::move(budget_key.value()),
      budget_denied_behavior);
}

}  // namespace content
