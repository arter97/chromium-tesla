// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/js_injection/browser/navigation_web_message_sender.h"

#include "base/json/json_writer.h"
#include "base/strings/utf_string_conversions.h"
#include "components/back_forward_cache/back_forward_cache_disable.h"
#include "components/back_forward_cache/disabled_reason_id.h"
#include "components/js_injection/browser/web_message.h"
#include "components/js_injection/browser/web_message_host.h"
#include "components/js_injection/browser/web_message_host_factory.h"
#include "components/js_injection/browser/web_message_reply_proxy.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/page.h"
#include "content/public/browser/web_contents.h"
#include "net/http/http_response_headers.h"

namespace {

base::Value::Dict CreateBaseMessageFromNavigationHandle(
    content::NavigationHandle* navigation_handle) {
  return base::Value::Dict()
      .Set("id", base::NumberToString(navigation_handle->GetNavigationId()))
      .Set("url", navigation_handle->GetURL().spec())
      .Set("isSameDocument", navigation_handle->IsSameDocument())
      .Set("isPageInitiated", navigation_handle->IsRendererInitiated())
      .Set("isReload",
           navigation_handle->GetReloadType() != content::ReloadType::NONE)
      .Set("isHistory", navigation_handle->IsHistory());
}

}  // namespace

namespace features {
// Temporarily enable this feature while we work on moving the API to AndroidX.
// This should be safe as it is very unlikely for apps to accidentally register
// listeners with the same name as `kNavigationListenerAllowBFCacheObjectName` /
// `kNavigationListenerDisableBFCacheObjectName`.
BASE_FEATURE(kEnableNavigationListener,
             "EnableNavigationListener",
             base::FEATURE_ENABLED_BY_DEFAULT);
}  // namespace features

namespace js_injection {

// An empty WebMessageReplyProxy used as a placeholder as there is no
// connection to the renderer. This object is 1:1 with the Page, so it
// can be used by the app to identify which Page an injected navigation
// message is associated with.
class EmptyReplyProxy : public WebMessageReplyProxy {
 public:
  explicit EmptyReplyProxy(content::Page& page) : page_(&page) {}
  EmptyReplyProxy(const EmptyReplyProxy&) = delete;
  EmptyReplyProxy& operator=(const EmptyReplyProxy&) = delete;
  ~EmptyReplyProxy() override = default;

  // WebMessageReplyProxy:
  void PostWebMessage(blink::WebMessagePayload message) override {
    // Do nothing as there is no connection to the renderer.
  }
  content::Page& GetPage() override { return *page_; }

 private:
  raw_ptr<content::Page> page_;
};

const char16_t
    NavigationWebMessageSender::kNavigationListenerAllowBFCacheObjectName[] =
        u"experimentalWebViewNavigationListenerAllowBFCache";
const char16_t
    NavigationWebMessageSender::kNavigationListenerDisableBFCacheObjectName[] =
        u"experimentalWebViewNavigationListenerDisableBFCache";

const char NavigationWebMessageSender::kOptedInMessage[] =
    "NAVIGATION_MESSAGE_OPTED_IN";
const char NavigationWebMessageSender::kNavigationStartedMessage[] =
    "NAVIGATION_STARTED";
const char NavigationWebMessageSender::kNavigationRedirectedMessage[] =
    "NAVIGATION_REDIRECTED";
const char NavigationWebMessageSender::kNavigationCompletedMessage[] =
    "NAVIGATION_COMPLETED";
const char NavigationWebMessageSender::kPageLoadEndMessage[] = "PAGE_LOAD_END";
const char NavigationWebMessageSender::kPageDeletedMessage[] = "PAGE_DELETED";

// static
bool NavigationWebMessageSender::IsNavigationListener(
    const std::u16string& js_object_name) {
  return base::FeatureList::IsEnabled(features::kEnableNavigationListener) &&
         ((js_object_name == kNavigationListenerAllowBFCacheObjectName) ||
          (js_object_name == kNavigationListenerDisableBFCacheObjectName));
}

// static
void NavigationWebMessageSender::CreateForPageIfNeeded(
    content::Page& page,
    const std::u16string& js_object_name,
    WebMessageHostFactory* factory) {
  if (!IsNavigationListener(js_object_name)) {
    return;
  }
  NavigationWebMessageSender::CreateForPage(page, js_object_name, factory);
}

NavigationWebMessageSender::NavigationWebMessageSender(
    content::Page& page,
    const std::u16string& js_object_name,
    WebMessageHostFactory* factory)
    : content::PageUserData<NavigationWebMessageSender>(page),
      content::WebContentsObserver(
          content::WebContents::FromRenderFrameHost(&page.GetMainDocument())) {
  CHECK(base::FeatureList::IsEnabled(features::kEnableNavigationListener));
  CHECK(page.IsPrimary());
  const std::string origin_string =
      page.GetMainDocument().GetLastCommittedOrigin().Serialize();
  reply_proxy_ = std::make_unique<EmptyReplyProxy>(page);
  host_ = factory->CreateHost(origin_string, origin_string,
                              /*is_main_frame=*/true, reply_proxy_.get());
  if (js_object_name == kNavigationListenerDisableBFCacheObjectName) {
    // Prevent this page to enter BFCache. See the comment for the definition of
    // `kNavigationListenerDisableBFCacheObjectName` for why we want to do this.
    content::BackForwardCache::DisableForRenderFrameHost(
        &page.GetMainDocument(),
        back_forward_cache::DisabledReason(
            back_forward_cache::DisabledReasonId::kRequestedByWebViewClient));
  }
}

NavigationWebMessageSender::~NavigationWebMessageSender() {
  CHECK(!page().GetMainDocument().IsInLifecycleState(
      content::RenderFrameHost::LifecycleState::kPendingCommit));
  PostMessageWithType(kPageDeletedMessage);
}

void NavigationWebMessageSender::DispatchOptInMessage() {
  CHECK(page().IsPrimary());

  base::Value::Dict message_dict =
      base::Value::Dict()
          .Set("type", kOptedInMessage)
          .Set("supports_start_and_redirect", true);
  PostMessage(std::move(message_dict));
}

void NavigationWebMessageSender::DidFinishLoad(
    content::RenderFrameHost* render_frame_host,
    const GURL& validated_url) {
  if (!page().IsPrimary() || render_frame_host != &page().GetMainDocument()) {
    // Only send the load notifications for the primary main frame.
    return;
  }
  PostMessageWithType(kPageLoadEndMessage);
}

bool NavigationWebMessageSender::ShouldSendMessageForNavigation(
    content::NavigationHandle* navigation_handle) {
  // Only send navigation notifications for primary pages, and only from the
  // associated NavigationWebMessageSender. Note that since
  // `IsInPrimaryMainFrame()` can also be true when the navigation didn't
  // commit / create a new page, it means that the messages for those
  // navigations will be fired on the sender of the current primary page.
  return page().IsPrimary() && navigation_handle->IsInPrimaryMainFrame();
}

void NavigationWebMessageSender::DidStartNavigation(
    content::NavigationHandle* navigation_handle) {
  if (!ShouldSendMessageForNavigation(navigation_handle)) {
    return;
  }
  base::Value::Dict message_dict =
      CreateBaseMessageFromNavigationHandle(navigation_handle)
          .Set("type", kNavigationStartedMessage);
  PostMessage(std::move(message_dict));
}

void NavigationWebMessageSender::DidRedirectNavigation(
    content::NavigationHandle* navigation_handle) {
  if (!ShouldSendMessageForNavigation(navigation_handle)) {
    return;
  }
  base::Value::Dict message_dict =
      CreateBaseMessageFromNavigationHandle(navigation_handle)
          .Set("type", kNavigationRedirectedMessage);
  PostMessage(std::move(message_dict));
}

void NavigationWebMessageSender::DidFinishNavigation(
    content::NavigationHandle* navigation_handle) {
  if (!ShouldSendMessageForNavigation(navigation_handle)) {
    return;
  }
  base::Value::Dict message_dict =
      CreateBaseMessageFromNavigationHandle(navigation_handle)
          .Set("type", kNavigationCompletedMessage)
          .Set("isErrorPage", navigation_handle->IsErrorPage())
          .Set("committed", navigation_handle->HasCommitted())
          // Some navigations don't have HTTP responses. Default to 200 for
          // those cases.
          .Set("statusCode",
               navigation_handle->GetResponseHeaders()
                   ? navigation_handle->GetResponseHeaders()->response_code()
                   : 200);
  PostMessage(std::move(message_dict));
}

std::unique_ptr<WebMessage> NavigationWebMessageSender::CreateWebMessage(
    base::Value::Dict message_dict) {
  base::Value message(std::move(message_dict));
  std::string json_message;
  base::JSONWriter::Write(message, &json_message);
  std::unique_ptr<WebMessage> web_message = std::make_unique<WebMessage>();
  web_message->message = base::UTF8ToUTF16(json_message);
  return web_message;
}

void NavigationWebMessageSender::PostMessageWithType(std::string_view type) {
  base::Value::Dict message_dict;
  message_dict.Set("type", type);
  PostMessage(std::move(message_dict));
}

void NavigationWebMessageSender::PostMessage(base::Value::Dict message_dict) {
  host_->OnPostMessage(CreateWebMessage(std::move(message_dict)));
}

PAGE_USER_DATA_KEY_IMPL(NavigationWebMessageSender);

}  // namespace js_injection
