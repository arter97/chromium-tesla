// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_TOP_CHROME_WEBUI_CONTENTS_PRELOAD_MANAGER_H_
#define CHROME_BROWSER_UI_WEBUI_TOP_CHROME_WEBUI_CONTENTS_PRELOAD_MANAGER_H_

#include <optional>

#include "base/callback_list.h"
#include "base/scoped_observation.h"
#include "chrome/browser/profiles/profile_observer.h"
#include "chrome/browser/ui/webui/top_chrome/per_profile_webui_tracker.h"
#include "chrome/browser/ui/webui/top_chrome/preload_candidate_selector.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/web_contents.h"
#include "url/gurl.h"

class Browser;
class PerProfileWebUITracker;

// WebUIContentsPreloadManager is a singleton class that preloads top Chrome
// WebUIs. At anytime, at most one WebContents is preloaded across all profiles.
// If under heavy memory pressure, no preloaded contents will be created.
//
// To make a WebUI preloadable, update GetAllPreloadableWebUIURLs() and
// ensure that tests pass.
class WebUIContentsPreloadManager : public ProfileObserver,
                                    public PerProfileWebUITracker::Observer {
 public:
  enum class PreloadMode {
    // Preloads on calling `WarmupForBrowser()` and after every WebUI
    // creation.
    // TODO(326505383): preloading on browser startup causes test failures
    // primarily because they expect a certain number of WebContents are
    // created.
    kPreloadOnWarmup = 0,
    // Preloads only after every WebUI creation.
    // After the preloaded contents is taken, perloads a new contents.
    kPreloadOnMakeContents = 1,
  };

  struct MakeContentsResult {
    MakeContentsResult();
    MakeContentsResult(MakeContentsResult&&);
    MakeContentsResult& operator=(MakeContentsResult&&);
    MakeContentsResult(const MakeContentsResult&) = delete;
    MakeContentsResult& operator=(const MakeContentsResult&) = delete;
    ~MakeContentsResult();

    std::unique_ptr<content::WebContents> web_contents;
    // True if `web_contents` is ready to be shown on screen. This boolean only
    // reflects the state when this struct is constructed. The `web_contents`
    // will cease to be ready to show, for example, if it reloads.
    bool is_ready_to_show;
  };

  WebUIContentsPreloadManager();
  ~WebUIContentsPreloadManager() override;

  WebUIContentsPreloadManager(const WebUIContentsPreloadManager&) = delete;
  WebUIContentsPreloadManager& operator=(const WebUIContentsPreloadManager&) =
      delete;

  static WebUIContentsPreloadManager* GetInstance();

  // Warms up the preload manager. Depending on PreloadMode this may or may not
  // make a preloaded contents.
  void WarmupForBrowser(Browser* browser);

  // Make a WebContents that shows `webui_url` under `browser_context`.
  // Reuses the preloaded contents if it is under the same `browser_context`.
  // A new preloaded contents will be created, unless we are under heavy
  // memory pressure.
  MakeContentsResult MakeContents(const GURL& webui_url,
                                  content::BrowserContext* browser_context);

  content::WebContents* preloaded_web_contents() {
    return preloaded_web_contents_.get();
  }

  // Disable navigations for tests that don't have //content properly
  // initialized.
  void DisableNavigationForTesting();

  // Returns the GURL of all preloadable WebUIs.
  static std::vector<GURL> GetAllPreloadableWebUIURLsForTesting();

  // Returns the currently preloaded WebUI URL. Returns nullopt if no content is
  // preloaded. This is a mirror of GetPreloadedURL().
  std::optional<GURL> GetPreloadedURLForTesting() const;

  // Returns the next WebUI URL to preload. This can return nullopt indicating
  // that no new WebUI will be preloaded. This is a mirror of
  // GetNextWebUIURLToPreload().
  std::optional<GURL> GetNextWebUIURLToPreloadForTesting(
      content::BrowserContext* browser_context) const;

  void MaybePreloadForBrowserContextForTesting(
      content::BrowserContext* browser_context);

  void SetPreloadCandidateSelectorForTesting(
      std::unique_ptr<webui::PreloadCandidateSelector>
          preload_candidate_selector);

 private:
  class WebUIControllerEmbedderStub;

  // Returns the currently preloaded WebUI URL. Returns nullopt if no content is
  // preloaded.
  void SetPreloadCandidateSelector(
      std::unique_ptr<webui::PreloadCandidateSelector>
          preload_candidate_selector);

  // Returns the next WebUI URL to preload. This can return nullopt indicating
  // that no new WebUI will be preloaded.
  std::optional<GURL> GetNextWebUIURLToPreload(
      content::BrowserContext* browser_context) const;

  // Preload a WebContents for `browser_context`.
  // There is at most one preloaded contents at any time.
  // If the preloaded contents has a different browser context, replace it
  // with a new contents under the given `browser_context`.
  // If under heavy memory pressure, no preloaded contents will be created.
  void MaybePreloadForBrowserContext(content::BrowserContext* browser_context);

  // Sets the current preloaded WebContents and performs necessary bookkepping.
  // The bookkeeping includes monitoring for the shutdown of the browser context
  // and handling the "ready-to-show" event emitted by the WebContents.
  void SetPreloadedContents(std::unique_ptr<content::WebContents> web_contents);

  std::unique_ptr<content::WebContents> CreateNewContents(
      content::BrowserContext* browser_context,
      GURL url);

  void LoadURLForContents(content::WebContents* web_contents, GURL url);

  // Returns true if a new preloaded contents should be created for
  // `browser_context`.
  bool ShouldPreloadForBrowserContext(
      content::BrowserContext* browser_context) const;

  // Cleans up preloaded contents on browser context shutdown.
  void OnBrowserContextShutdown(content::BrowserContext* browser_context);

  // ProfileObserver:
  void OnProfileWillBeDestroyed(Profile* profile) override;

  // PerProfileWebUITracker::Observer:
  void OnWebContentsDestroyed(content::WebContents* web_contents) override;

  PreloadMode preload_mode_ = PreloadMode::kPreloadOnMakeContents;

  // Disable navigations for views unittests because they don't initialize
  // //content properly.
  bool is_navigation_disabled_for_test_ = false;

  // Used to prevent the preload re-entrance due to destroying the old preload
  // contents.
  bool is_setting_preloaded_web_contents_ = false;

  std::unique_ptr<content::WebContents> preloaded_web_contents_;

  // Tracks the WebUI presence state under a profile.
  std::unique_ptr<PerProfileWebUITracker> webui_tracker_;

  // Observes the tracker for WebContents destroy.
  base::ScopedObservation<PerProfileWebUITracker,
                          PerProfileWebUITracker::Observer>
      webui_tracker_observation_{this};

  // PreloadCandidateSelector selects the next WebUI to preload.
  std::unique_ptr<webui::PreloadCandidateSelector> preload_candidate_selector_;

  // A stub WebUI page embdeder that captures the ready-to-show signal.
  std::unique_ptr<WebUIControllerEmbedderStub> webui_controller_embedder_stub_;

  // Observation of destroy of preload content's profile.
  base::ScopedObservation<Profile, ProfileObserver> profile_observation_{this};
};

#endif  // CHROME_BROWSER_UI_WEBUI_TOP_CHROME_WEBUI_CONTENTS_PRELOAD_MANAGER_H_
