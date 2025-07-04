// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/safe_browsing/content/browser/web_api_handshake_checker.h"

#include "base/memory/weak_ptr.h"
#include "base/metrics/histogram_functions.h"
#include "components/safe_browsing/content/browser/web_ui/safe_browsing_ui.h"
#include "components/safe_browsing/core/browser/safe_browsing_url_checker_impl.h"
#include "components/safe_browsing/core/browser/url_checker_delegate.h"
#include "components/safe_browsing/core/common/features.h"
#include "components/safe_browsing/core/common/scheme_logger.h"
#include "content/public/browser/browser_task_traits.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/web_contents.h"
#include "net/http/http_request_headers.h"

namespace safe_browsing {

class WebApiHandshakeChecker::CheckerOnSB {
 public:
  CheckerOnSB(base::WeakPtr<WebApiHandshakeChecker> handshake_checker,
              GetDelegateCallback delegate_getter,
              const GetWebContentsCallback& web_contents_getter,
              int frame_tree_node_id)
      : handshake_checker_(std::move(handshake_checker)),
        delegate_getter_(std::move(delegate_getter)),
        web_contents_getter_(web_contents_getter),
        frame_tree_node_id_(frame_tree_node_id) {
    DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
    DCHECK(handshake_checker_);
    DCHECK(delegate_getter_);
    DCHECK(web_contents_getter_);
  }

  void Check(const GURL& url) {
    DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
    DCHECK(delegate_getter_);
    DCHECK(web_contents_getter_);

    scoped_refptr<UrlCheckerDelegate> url_checker_delegate =
        std::move(delegate_getter_).Run();
    bool skip_checks =
        !url_checker_delegate ||
        url_checker_delegate->ShouldSkipRequestCheck(
            url, frame_tree_node_id_,
            /*render_process_id=*/content::ChildProcessHost::kInvalidUniqueID,
            /*render_frame_token=*/std::nullopt,
            /*originated_from_service_worker=*/false);
    if (skip_checks) {
      OnCompleteCheckInternal(/*proceed=*/true);
      return;
    }

    scheme_logger::LogScheme(url,
                             "SafeBrowsing.WebApiHandshakeCheck.UrlScheme");
    // If |kSafeBrowsingSkipSubresources2| is enabled, skip Safe Browsing checks
    // for WebTransport.
    if (base::FeatureList::IsEnabled(kSafeBrowsingSkipSubresources2)) {
      base::UmaHistogramBoolean("SafeBrowsing.WebApiHandshakeCheck.Skipped",
                                true);
      OnCompleteCheckInternal(/*proceed=*/true);
      return;
    }

    base::UmaHistogramBoolean("SafeBrowsing.WebApiHandshakeCheck.Skipped",
                              false);
    url_checker_ = std::make_unique<SafeBrowsingUrlCheckerImpl>(
        net::HttpRequestHeaders(), /*load_flags=*/0, /*has_user_gesture=*/false,
        url_checker_delegate, web_contents_getter_, /*weak_web_state=*/nullptr,
        /*render_process_id=*/content::ChildProcessHost::kInvalidUniqueID,
        /*render_frame_token=*/std::nullopt, frame_tree_node_id_,
        /*navigation_id=*/std::nullopt,
        /*url_real_time_lookup_enabled=*/false,
        /*can_check_db=*/true, /*can_check_high_confidence_allowlist=*/true,
        /*url_lookup_service_metric_suffix=*/".None",
        content::GetUIThreadTaskRunner({}),
        /*url_lookup_service=*/nullptr,
        /*hash_realtime_service_on_ui=*/nullptr,
        /*hash_realtime_selection=*/
        hash_realtime_utils::HashRealTimeSelection::kNone,
        /*is_async_check=*/false, SessionID::InvalidValue());
    url_checker_->CheckUrl(
        url, "GET",
        base::BindOnce(&WebApiHandshakeChecker::CheckerOnSB::OnCheckUrlResult,
                       base::Unretained(this)));
  }

  base::WeakPtr<CheckerOnSB> AsWeakPtr() { return weak_factory_.GetWeakPtr(); }

 private:
  // See comments in BrowserUrlLoaderThrottle::OnCheckUrlResult().
  void OnCheckUrlResult(
      SafeBrowsingUrlCheckerImpl::NativeUrlCheckNotifier* slow_check_notifier,
      bool proceed,
      bool showed_interstitial,
      bool has_post_commit_interstitial_skipped,
      SafeBrowsingUrlCheckerImpl::PerformedCheck performed_check) {
    DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
    if (!slow_check_notifier) {
      OnCompleteCheckInternal(proceed);
      return;
    }

    *slow_check_notifier =
        base::BindOnce(&WebApiHandshakeChecker::CheckerOnSB::OnCompleteCheck,
                       base::Unretained(this));
  }

  void OnCompleteCheck(
      bool proceed,
      bool showed_interstitial,
      bool has_post_commit_interstitial_skipped,
      SafeBrowsingUrlCheckerImpl::PerformedCheck performed_check) {
    OnCompleteCheckInternal(proceed);
  }

  void OnCompleteCheckInternal(bool proceed) {
    DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
    handshake_checker_->OnCompleteCheck(proceed);
  }

  base::WeakPtr<WebApiHandshakeChecker> handshake_checker_;
  GetDelegateCallback delegate_getter_;
  GetWebContentsCallback web_contents_getter_;
  const int frame_tree_node_id_;
  std::unique_ptr<SafeBrowsingUrlCheckerImpl> url_checker_;
  base::WeakPtrFactory<CheckerOnSB> weak_factory_{this};
};

WebApiHandshakeChecker::WebApiHandshakeChecker(
    GetDelegateCallback delegate_getter,
    const GetWebContentsCallback& web_contents_getter,
    int frame_tree_node_id) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  sb_checker_ = std::make_unique<CheckerOnSB>(
      weak_factory_.GetWeakPtr(), std::move(delegate_getter),
      web_contents_getter, frame_tree_node_id);
}

WebApiHandshakeChecker::~WebApiHandshakeChecker() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
}

void WebApiHandshakeChecker::Check(const GURL& url, CheckCallback callback) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  DCHECK(!check_callback_);
  check_callback_ = std::move(callback);
  sb_checker_->Check(url);
}

void WebApiHandshakeChecker::OnCompleteCheck(bool proceed) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  DCHECK(check_callback_);

  CheckResult result = proceed ? CheckResult::kProceed : CheckResult::kBlocked;
  std::move(check_callback_).Run(result);
}

}  // namespace safe_browsing
