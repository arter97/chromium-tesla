// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/preloading/prerender/prerender_no_vary_search_hint_commit_deferring_condition.h"

#include <memory>
#include <optional>

#include "base/check.h"
#include "base/feature_list.h"
#include "base/functional/callback_forward.h"
#include "base/memory/ptr_util.h"
#include "base/no_destructor.h"
#include "base/task/single_thread_task_runner.h"
#include "base/time/time.h"
#include "content/browser/preloading/prerender/prerender_host.h"
#include "content/browser/renderer_host/frame_tree_node.h"
#include "content/browser/renderer_host/navigation_request.h"
#include "content/public/browser/commit_deferring_condition.h"

namespace content {

namespace {

// Returns the root prerender frame tree node associated with navigation_request
// of ongoing prerender activation.
FrameTreeNode* GetRootPrerenderFrameTreeNode(int prerender_frame_tree_node_id) {
  FrameTreeNode* root =
      FrameTreeNode::GloballyFindByID(prerender_frame_tree_node_id);
  if (root) {
    CHECK(root->IsOutermostMainFrame());
  }
  return root;
}

}  // namespace

// static
std::unique_ptr<CommitDeferringCondition>
PrerenderNoVarySearchHintCommitDeferringCondition::MaybeCreate(
    NavigationRequest& navigation_request,
    NavigationType navigation_type,
    std::optional<int> candidate_prerender_frame_tree_node_id) {
  // Don't create if No-Vary-Search support for prerender is not enabled.
  if (!base::FeatureList::IsEnabled(blink::features::kPrerender2NoVarySearch)) {
    return nullptr;
  }

  // Don't create if this navigation is not for prerender page activation.
  if (navigation_type != NavigationType::kPrerenderedPageActivation) {
    return nullptr;
  }

  // For `navigation_type` == `NavigationType::kPrerenderedPageActivation`
  // `candidate_prerender_frame_tree_node_id` has always a value as we are
  // trying to activate a prerender.
  CHECK(candidate_prerender_frame_tree_node_id.has_value());

  // Don't create if associated PrerenderHost has already received headers.
  FrameTreeNode* prerender_frame_tree_node = GetRootPrerenderFrameTreeNode(
      candidate_prerender_frame_tree_node_id.value());
  // If there is no prerender frame tree node stop here.
  if (!prerender_frame_tree_node) {
    return nullptr;
  }
  PrerenderHost& prerender_host =
      PrerenderHost::GetFromFrameTreeNode(*prerender_frame_tree_node);
  if (prerender_host.were_headers_received()) {
    return nullptr;
  }
  if (!prerender_host.no_vary_search_expected().has_value()) {
    return nullptr;
  }

  return base::WrapUnique(new PrerenderNoVarySearchHintCommitDeferringCondition(
      navigation_request, candidate_prerender_frame_tree_node_id.value()));
}

PrerenderNoVarySearchHintCommitDeferringCondition::
    ~PrerenderNoVarySearchHintCommitDeferringCondition() {
  block_until_head_timer_.reset();
  // Stop observing the associated PrerenderHost to avoid use-after-free.
  FrameTreeNode* prerender_frame_tree_node =
      GetRootPrerenderFrameTreeNode(candidate_prerender_frame_tree_node_id_);
  if (!prerender_frame_tree_node) {
    // In this case the commit deferring condition was removed as an
    // observer by the PrerenderHost destructor.
    return;
  }
  PrerenderHost& prerender_host =
      PrerenderHost::GetFromFrameTreeNode(*prerender_frame_tree_node);
  prerender_host.RemoveObserver(this);

  if (waiting_on_headers_) {
    waiting_on_headers_ = false;
    prerender_host.OnWaitingForHeadersFinished(
        GetNavigationHandle(), PrerenderHost::WaitingForHeadersFinishedReason::
                                   kMaybeNavigationCancelled);
  }
}

PrerenderNoVarySearchHintCommitDeferringCondition::
    PrerenderNoVarySearchHintCommitDeferringCondition(
        NavigationRequest& navigation_request,
        int candidate_prerender_frame_tree_node_id)
    : CommitDeferringCondition(navigation_request),
      candidate_prerender_frame_tree_node_id_(
          candidate_prerender_frame_tree_node_id) {
  CHECK(base::FeatureList::IsEnabled(blink::features::kPrerender2NoVarySearch));
  CHECK_NE(candidate_prerender_frame_tree_node_id_,
           RenderFrameHost::kNoFrameTreeNodeId);
  FrameTreeNode* prerender_frame_tree_node =
      GetRootPrerenderFrameTreeNode(candidate_prerender_frame_tree_node_id_);
  PrerenderHost& prerender_host =
      PrerenderHost::GetFromFrameTreeNode(*prerender_frame_tree_node);
  // We only create `this` instance if headers were not received by the
  // associated prerender.
  CHECK(!prerender_host.were_headers_received());
  // Add this commit deferring condition as an observer of the associated
  // PrerenderHost.
  prerender_host.AddObserver(this);
}

CommitDeferringCondition::Result
PrerenderNoVarySearchHintCommitDeferringCondition::WillCommitNavigation(
    base::OnceClosure resume) {
  FrameTreeNode* prerender_frame_tree_node =
      GetRootPrerenderFrameTreeNode(candidate_prerender_frame_tree_node_id_);
  // If the prerender FrameTreeNode is gone, the prerender activation is allowed
  // to continue here but will fail soon.
  if (!prerender_frame_tree_node) {
    return Result::kProceed;
  }
  // If headers were already received, proceed.
  PrerenderHost& prerender_host =
      PrerenderHost::GetFromFrameTreeNode(*prerender_frame_tree_node);
  if (prerender_host.were_headers_received()) {
    return Result::kProceed;
  }

  // `resume` callback is always set by
  // `CommitDeferringConditionRunner::ProcessConditions`.
  CHECK(resume);
  // We now need to wait for headers.
  resume_ = std::move(resume);

  base::TimeDelta block_until_head_timeout =
      prerender_host.WaitUntilHeadTimeout();
  PrerenderHost::WaitingForHeadersStartedReason reason =
      PrerenderHost::WaitingForHeadersStartedReason::kWithoutTimeout;
  if (block_until_head_timeout.is_positive()) {
    CHECK(!block_until_head_timer_);
    block_until_head_timer_ = std::make_unique<base::OneShotTimer>();
    block_until_head_timer_->SetTaskRunner(GetTimerTaskRunner());
    block_until_head_timer_->Start(
        FROM_HERE, block_until_head_timeout,
        base::BindOnce(&PrerenderNoVarySearchHintCommitDeferringCondition::
                           OnBlockUntilHeadTimerElapsed,
                       Unretained(this)));
    reason = PrerenderHost::WaitingForHeadersStartedReason::kWithTimeout;
  }

  // Let the PrerenderHost know that this navigation is waiting on the
  // associated prerender's headers.
  waiting_on_headers_ = true;
  prerender_host.OnWaitingForHeadersStarted(GetNavigationHandle(), reason);

  return Result::kDefer;
}

void PrerenderNoVarySearchHintCommitDeferringCondition::OnHeadersReceived() {
  // Verify all conditions are met:
  // * headers should have been received and
  // * the prerender_frame_tree_node is still alive.
  FrameTreeNode* prerender_frame_tree_node =
      GetRootPrerenderFrameTreeNode(candidate_prerender_frame_tree_node_id_);
  CHECK(prerender_frame_tree_node);
  PrerenderHost& prerender_host =
      PrerenderHost::GetFromFrameTreeNode(*prerender_frame_tree_node);
  CHECK(prerender_host.were_headers_received());

  // Remove the observer from the prerender host.
  prerender_host.RemoveObserver(this);

  // Let the Prerender_Host know that this navigation is done waiting on the
  // associated prerender's headers.
  if (waiting_on_headers_) {
    waiting_on_headers_ = false;
    prerender_host.OnWaitingForHeadersFinished(
        GetNavigationHandle(),
        PrerenderHost::WaitingForHeadersFinishedReason::kHeadersReceived);
  }

  // We don't need the timer anymore.
  block_until_head_timer_.reset();

  // Resume the navigation once the prerender main page has received the
  // headers.
  CHECK(resume_);
  std::move(resume_).Run();

  // Don't add any more code as "this" is destroyed by calling the
  // resume_ callback.
}

void PrerenderNoVarySearchHintCommitDeferringCondition::OnHostDestroyed(
    PrerenderFinalStatus status) {
  FrameTreeNode* prerender_frame_tree_node =
      GetRootPrerenderFrameTreeNode(candidate_prerender_frame_tree_node_id_);
  CHECK(prerender_frame_tree_node);
  PrerenderHost& prerender_host =
      PrerenderHost::GetFromFrameTreeNode(*prerender_frame_tree_node);

  // Remove the observer from the prerender host.
  prerender_host.RemoveObserver(this);

  // We might need to hold more state to know.
  if (waiting_on_headers_) {
    // Let the Prerender_Host know that this navigation is done waiting on the
    // associated prerender's headers.
    waiting_on_headers_ = false;
    prerender_host.OnWaitingForHeadersFinished(
        GetNavigationHandle(),
        PrerenderHost::WaitingForHeadersFinishedReason::kHostDestroyed);
  }

  // We don't need the timer anymore.
  block_until_head_timer_.reset();

  // If we have not resumed the navigation, do it now. This could happen if the
  // prerender is cancelled before receiving headers.
  if (resume_) {
    std::move(resume_).Run();
  }

  // Don't add any more code as "this" is destroyed by calling the
  // resume_ callback.
}

void PrerenderNoVarySearchHintCommitDeferringCondition::
    OnBlockUntilHeadTimerElapsed() {
  FrameTreeNode* prerender_frame_tree_node =
      GetRootPrerenderFrameTreeNode(candidate_prerender_frame_tree_node_id_);
  CHECK(prerender_frame_tree_node);
  PrerenderHost& prerender_host =
      PrerenderHost::GetFromFrameTreeNode(*prerender_frame_tree_node);
  // Let the Prerender_Host know that this navigation is done waiting on the
  // associated prerender's headers.
  waiting_on_headers_ = false;
  prerender_host.OnWaitingForHeadersFinished(
      GetNavigationHandle(),
      PrerenderHost::WaitingForHeadersFinishedReason::kTimeoutElapsed);

  if (resume_) {
    std::move(resume_).Run();
  }

  // Don't add any more code as "this" is destroyed by calling the
  // resume_ callback.
}

scoped_refptr<base::SingleThreadTaskRunner>
PrerenderNoVarySearchHintCommitDeferringCondition::GetTimerTaskRunner() {
  return GetTimerTaskRunnerForTesting()        // IN-TEST
             ? GetTimerTaskRunnerForTesting()  // IN-TEST
             : base::SingleThreadTaskRunner::GetCurrentDefault();
}

// static
scoped_refptr<base::SingleThreadTaskRunner>&
PrerenderNoVarySearchHintCommitDeferringCondition::
    GetTimerTaskRunnerForTesting() {
  static base::NoDestructor<scoped_refptr<base::SingleThreadTaskRunner>>
      timer_task_runner_for_testing;
  return *timer_task_runner_for_testing;
}

// static
void PrerenderNoVarySearchHintCommitDeferringCondition::
    SetTimerTaskRunnerForTesting(
        scoped_refptr<base::SingleThreadTaskRunner> task_runner) {
  GetTimerTaskRunnerForTesting() = std::move(task_runner);  // IN-TEST
}

}  // namespace content
