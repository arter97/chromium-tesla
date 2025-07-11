// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/top_chrome/webui_contents_preload_manager.h"

#include <memory>
#include <optional>
#include <string>

#include "base/auto_reset.h"
#include "base/containers/contains.h"
#include "base/feature_list.h"
#include "base/memory/memory_pressure_monitor.h"
#include "base/memory/weak_ptr.h"
#include "base/metrics/histogram_functions.h"
#include "base/no_destructor.h"
#include "base/scoped_observation.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/task_manager/web_contents_tags.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/prefs/prefs_tab_helper.h"
#include "chrome/browser/ui/ui_features.h"
#include "chrome/browser/ui/webui/top_chrome/per_profile_webui_tracker.h"
#include "chrome/browser/ui/webui/top_chrome/preload_context.h"
#include "chrome/browser/ui/webui/top_chrome/profile_preload_candidate_selector.h"
#include "chrome/browser/ui/webui/top_chrome/top_chrome_web_ui_controller.h"
#include "chrome/common/webui_url_constants.h"
#include "chrome/grit/generated_resources.h"
#include "content/public/browser/navigation_controller.h"
#include "ui/base/models/menu_model.h"

namespace {

// Enum class representing the results of attempting to use a preloaded WebUI
// when WebUIContentsPreloadedManager::MakeContents() is called.
// The description of each value is also in tools/metrics/histograms/enums.xml.
enum class WebUIPreloadResult {
  // No preloaded WebUI is available when a WebUI is requested.
  kNoPreload = 0,
  // The preloaded WebUI matches the requested WebUI.
  kHit = 1,
  // The preloaded WebUI is redirected to the requested WebUI.
  kHitRedirected = 2,
  // The preloaded WebUI does not match the requested WebUI and cannot be
  // redirected.
  kMiss = 3,
  kMaxValue = kMiss,
};

// A candidate selector that always preloads a fixed WebUI.
class FixedCandidateSelector : public webui::PreloadCandidateSelector {
 public:
  explicit FixedCandidateSelector(GURL webui_url) : webui_url_(webui_url) {}
  FixedCandidateSelector(const FixedCandidateSelector&) = default;
  FixedCandidateSelector(FixedCandidateSelector&&) = default;
  FixedCandidateSelector& operator=(const FixedCandidateSelector&) = default;
  FixedCandidateSelector& operator=(FixedCandidateSelector&&) = default;

  // webui::PreloadCandidateSelector:
  void Init(const std::vector<GURL>& preloadable_urls) override {
    DCHECK(base::Contains(preloadable_urls, webui_url_));
  }
  std::optional<GURL> GetURLToPreload(
      const webui::PreloadContext& context) const override {
    return webui_url_;
  }

 private:
  GURL webui_url_;
};

bool IsFeatureEnabled() {
  return base::FeatureList::IsEnabled(features::kPreloadTopChromeWebUI);
}

bool IsSmartPreloadEnabled() {
  return IsFeatureEnabled() &&
         features::kPreloadTopChromeWebUISmartPreload.Get();
}

content::WebContents::CreateParams GetWebContentsCreateParams(
    const GURL& webui_url,
    content::BrowserContext* browser_context) {
  content::WebContents::CreateParams create_params(browser_context);
  // Set it to visible so that the resources are immediately loaded.
  create_params.initially_hidden = !IsFeatureEnabled();
  create_params.site_instance =
      content::SiteInstance::CreateForURL(browser_context, webui_url);

  return create_params;
}

content::WebUIController* GetWebUIController(
    content::WebContents* web_contents) {
  if (!web_contents) {
    return nullptr;
  }

  content::WebUI* webui = web_contents->GetWebUI();
  if (!webui) {
    return nullptr;
  }

  return webui->GetController();
}

std::vector<GURL> GetAllPreloadableWebUIURLs() {
  // Top 3 most used Top Chrome WebUIs.
  // TODO(crbug.com/40168622): add more Top Chrome WebUIs.
  static const base::NoDestructor<std::vector<GURL>> s_preloadable_webui_urls(
      {GURL(chrome::kChromeUITabSearchURL),
       GURL(chrome::kChromeUIHistoryClustersSidePanelURL),
       GURL(chrome::kChromeUIBookmarksSidePanelURL)});
  return *s_preloadable_webui_urls;
}

}  // namespace

// A stub WebUI page embdeder that captures the ready-to-show signal.
class WebUIContentsPreloadManager::WebUIControllerEmbedderStub final
    : public TopChromeWebUIController::Embedder {
 public:
  WebUIControllerEmbedderStub() = default;
  ~WebUIControllerEmbedderStub() = default;

  // TopChromeWebUIController::Embedder:
  void CloseUI() override {}
  void ShowContextMenu(gfx::Point point,
                       std::unique_ptr<ui::MenuModel> menu_model) override {}
  void HideContextMenu() override {}
  void ShowUI() override { is_ready_to_show_ = true; }

  // Attach this stub as the embedder of `web_contents`, assuming that the
  // contents is not yet ready to be shown.
  void AttachTo(content::WebContents* web_contents) {
    CHECK_NE(web_contents, nullptr);
    content::WebUIController* webui_controller =
        GetWebUIController(web_contents);
    if (!webui_controller) {
      return;
    }
    // TODO(40168622): Add type check. This is currently not possible because a
    // WebUIController subclass does not retain its parent class' type info.
    auto* bubble_controller =
        static_cast<TopChromeWebUIController*>(webui_controller);
    bubble_controller->set_embedder(this->GetWeakPtr());
    web_contents_ = web_contents;
    is_ready_to_show_ = false;
  }

  // Detach from the previously attached `web_contents`.
  void Detach() {
    if (!web_contents_) {
      return;
    }

    content::WebUIController* webui_controller =
        GetWebUIController(web_contents_);
    if (!webui_controller) {
      return;
    }

    auto* bubble_controller =
        static_cast<TopChromeWebUIController*>(webui_controller);
    bubble_controller->set_embedder(nullptr);
    web_contents_ = nullptr;
  }

  bool is_ready_to_show() const { return is_ready_to_show_; }

  base::WeakPtr<WebUIControllerEmbedderStub> GetWeakPtr() {
    return weak_ptr_factory_.GetWeakPtr();
  }

 private:
  raw_ptr<content::WebContents> web_contents_ = nullptr;
  bool is_ready_to_show_ = false;
  base::WeakPtrFactory<WebUIControllerEmbedderStub> weak_ptr_factory_{this};
};

using MakeContentsResult = WebUIContentsPreloadManager::MakeContentsResult;

MakeContentsResult::MakeContentsResult() = default;
MakeContentsResult::~MakeContentsResult() = default;
MakeContentsResult::MakeContentsResult(MakeContentsResult&&) = default;
MakeContentsResult& MakeContentsResult::operator=(MakeContentsResult&&) =
    default;

WebUIContentsPreloadManager::WebUIContentsPreloadManager() {
  preload_mode_ =
      static_cast<PreloadMode>(features::kPreloadTopChromeWebUIMode.Get());
  webui_controller_embedder_stub_ =
      std::make_unique<WebUIControllerEmbedderStub>();

  webui_tracker_ = PerProfileWebUITracker::Create();
  webui_tracker_observation_.Observe(webui_tracker_.get());
  if (IsSmartPreloadEnabled()) {
    // Use ProfilePreloadCandidateSelector to find the WebUI with the
    // highest engagement score and is not present under the current profile.
    SetPreloadCandidateSelector(
        std::make_unique<webui::ProfilePreloadCandidateSelector>(
            webui_tracker_.get()));
  } else {
    // Old behavior always preloads Tab Search.
    SetPreloadCandidateSelector(std::make_unique<FixedCandidateSelector>(
        GURL(chrome::kChromeUITabSearchURL)));
  }
}

WebUIContentsPreloadManager::~WebUIContentsPreloadManager() = default;

// static
WebUIContentsPreloadManager* WebUIContentsPreloadManager::GetInstance() {
  static base::NoDestructor<WebUIContentsPreloadManager> s_instance;
  return s_instance.get();
}

void WebUIContentsPreloadManager::WarmupForBrowser(Browser* browser) {
  // Most WebUIs, if not all, are hosted by a TYPE_NORMAL browser. This check
  // skips unnecessary preloading for the majority of WebUIs.
  if (!browser->is_type_normal()) {
    return;
  }

  if (preload_mode_ == PreloadMode::kPreloadOnMakeContents) {
    return;
  }

  CHECK_EQ(preload_mode_, PreloadMode::kPreloadOnWarmup);
  MaybePreloadForBrowserContext(browser->profile());
}

void WebUIContentsPreloadManager::MaybePreloadForBrowserContextForTesting(
    content::BrowserContext* browser_context) {
  MaybePreloadForBrowserContext(browser_context);
}

std::optional<GURL>
WebUIContentsPreloadManager::GetNextWebUIURLToPreloadForTesting(
    content::BrowserContext* browser_context) const {
  return GetNextWebUIURLToPreload(browser_context);
}

std::optional<GURL> WebUIContentsPreloadManager::GetNextWebUIURLToPreload(
    content::BrowserContext* browser_context) const {
  return preload_candidate_selector_->GetURLToPreload(
      webui::PreloadContext::From(
          Profile::FromBrowserContext(browser_context)));
}

// static
std::vector<GURL>
WebUIContentsPreloadManager::GetAllPreloadableWebUIURLsForTesting() {
  return GetAllPreloadableWebUIURLs();
}

void WebUIContentsPreloadManager::SetPreloadCandidateSelectorForTesting(
    std::unique_ptr<webui::PreloadCandidateSelector>
        preload_candidate_selector) {
  SetPreloadCandidateSelector(std::move(preload_candidate_selector));
}

void WebUIContentsPreloadManager::SetPreloadCandidateSelector(
    std::unique_ptr<webui::PreloadCandidateSelector>
        preload_candidate_selector) {
  preload_candidate_selector_ = std::move(preload_candidate_selector);
  if (preload_candidate_selector_) {
    preload_candidate_selector_->Init(GetAllPreloadableWebUIURLs());
  }
}

void WebUIContentsPreloadManager::MaybePreloadForBrowserContext(
    content::BrowserContext* browser_context) {
  if (!ShouldPreloadForBrowserContext(browser_context)) {
    return;
  }

  // Usually destroying a WebContents may trigger preload, but if the
  // destroy is caused by setting new preload contents, ignore it.
  if (is_setting_preloaded_web_contents_) {
    return;
  }

  std::optional<GURL> preload_url = GetNextWebUIURLToPreload(browser_context);
  if (!preload_url.has_value()) {
    return;
  }

  // Don't preload if already preloaded for this `browser_context`.
  if (preloaded_web_contents_ &&
      preloaded_web_contents_->GetBrowserContext() == browser_context &&
      preloaded_web_contents_->GetVisibleURL().GetWithEmptyPath() ==
          preload_url->GetWithEmptyPath()) {
    return;
  }

  SetPreloadedContents(CreateNewContents(browser_context, *preload_url));
}

void WebUIContentsPreloadManager::SetPreloadedContents(
    std::unique_ptr<content::WebContents> web_contents) {
  webui_controller_embedder_stub_->Detach();
  profile_observation_.Reset();

  base::AutoReset<bool> is_setting_preloaded_web_contents(
      &is_setting_preloaded_web_contents_, true);
  preloaded_web_contents_ = std::move(web_contents);
  if (preloaded_web_contents_) {
    webui_controller_embedder_stub_->AttachTo(preloaded_web_contents_.get());
    profile_observation_.Observe(Profile::FromBrowserContext(
        preloaded_web_contents_->GetBrowserContext()));
  }
}

MakeContentsResult WebUIContentsPreloadManager::MakeContents(
    const GURL& webui_url,
    content::BrowserContext* browser_context) {
  std::unique_ptr<content::WebContents> web_contents_ret;
  bool is_ready_to_show = false;
  WebUIPreloadResult preload_result = preloaded_web_contents_
                                          ? WebUIPreloadResult::kMiss
                                          : WebUIPreloadResult::kNoPreload;

  // Use preloaded contents if requested the same WebUI under the same browser
  // context. Navigating to or from a blank page is also allowed.
  // TODO(325836830): allow navigations between WebUIs.
  if (preloaded_web_contents_ &&
      preloaded_web_contents_->GetBrowserContext() == browser_context &&
      (preloaded_web_contents_->GetURL().host() == webui_url.host() ||
       preloaded_web_contents_->GetURL().IsAboutBlank() ||
       webui_url.IsAboutBlank())) {
    preload_result = WebUIPreloadResult::kHit;
    // Redirect if requested a different URL.
    if (!url::IsSameOriginWith(preloaded_web_contents_->GetURL(), webui_url)) {
      preload_result = WebUIPreloadResult::kHitRedirected;
      LoadURLForContents(preloaded_web_contents_.get(), webui_url);
    }
    web_contents_ret = std::move(preloaded_web_contents_);
    is_ready_to_show = webui_controller_embedder_stub_->is_ready_to_show();
    SetPreloadedContents(nullptr);
  } else {
    web_contents_ret = CreateNewContents(browser_context, webui_url);
    is_ready_to_show = false;
  }

  // Navigate to path if the request URL has a different path.
  if (!is_navigation_disabled_for_test_ &&
      webui_url.path() != web_contents_ret->GetURL().path()) {
    CHECK(url::IsSameOriginWith(webui_url, web_contents_ret->GetURL()));
    LoadURLForContents(web_contents_ret.get(), webui_url);
  }

  base::UmaHistogramEnumeration("WebUI.TopChrome.Preload.Result",
                                preload_result);

  // Preload a new contents.
  MaybePreloadForBrowserContext(browser_context);

  task_manager::WebContentsTags::ClearTag(web_contents_ret.get());

  MakeContentsResult result;
  result.web_contents = std::move(web_contents_ret);
  result.is_ready_to_show = is_ready_to_show;
  return result;
}

std::optional<GURL> WebUIContentsPreloadManager::GetPreloadedURLForTesting()
    const {
  if (!preloaded_web_contents_) {
    return std::nullopt;
  }
  return preloaded_web_contents_->GetVisibleURL();
}

void WebUIContentsPreloadManager::DisableNavigationForTesting() {
  is_navigation_disabled_for_test_ = true;
}

std::unique_ptr<content::WebContents>
WebUIContentsPreloadManager::CreateNewContents(
    content::BrowserContext* browser_context,
    GURL url) {
  std::unique_ptr<content::WebContents> web_contents =
      content::WebContents::Create(
          GetWebContentsCreateParams(url, browser_context));

  // Propagates user prefs to web contents.
  // This is needed by, for example, text selection color on ChromeOS.
  PrefsTabHelper::CreateForWebContents(web_contents.get());

  task_manager::WebContentsTags::CreateForToolContents(
      web_contents.get(), IDS_TASK_MANAGER_PRELOADED_RENDERER_FOR_UI);

  LoadURLForContents(web_contents.get(), url);

  webui_tracker_->AddWebContents(web_contents.get());

  return web_contents;
}

void WebUIContentsPreloadManager::LoadURLForContents(
    content::WebContents* web_contents,
    GURL url) {
  if (is_navigation_disabled_for_test_) {
    return;
  }

  web_contents->GetController().LoadURL(url, content::Referrer(),
                                        ui::PAGE_TRANSITION_AUTO_TOPLEVEL,
                                        std::string());
}

bool WebUIContentsPreloadManager::ShouldPreloadForBrowserContext(
    content::BrowserContext* browser_context) const {
  // Don't preload if the feature is disabled.
  if (!IsFeatureEnabled()) {
    return false;
  }

  if (browser_context->ShutdownStarted()) {
    return false;
  }

  // Don't preload if under heavy memory pressure.
  const auto* memory_monitor = base::MemoryPressureMonitor::Get();
  if (memory_monitor && memory_monitor->GetCurrentPressureLevel() >=
                            base::MemoryPressureMonitor::MemoryPressureLevel::
                                MEMORY_PRESSURE_LEVEL_MODERATE) {
    return false;
  }

  return true;
}

void WebUIContentsPreloadManager::OnProfileWillBeDestroyed(Profile* profile) {
  profile_observation_.Reset();
  if (!preloaded_web_contents_) {
    return;
  }

  webui_controller_embedder_stub_->Detach();
  CHECK_EQ(preloaded_web_contents_->GetBrowserContext(), profile);
  preloaded_web_contents_.reset();
}

void WebUIContentsPreloadManager::OnWebContentsDestroyed(
    content::WebContents* web_contents) {
  // Triggers preloading when a WebUI is destroyed. Without this step, the
  // preloaded content would only be the second highest engaged WebUI for
  // the most time.
  MaybePreloadForBrowserContext(web_contents->GetBrowserContext());
}
