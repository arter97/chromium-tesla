// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/side_panel/read_anything/read_anything_coordinator.h"

#include <memory>
#include <string>
#include <vector>

#include "base/functional/bind.h"
#include "base/metrics/field_trial_params.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "build/chromeos_buildflags.h"
#include "chrome/app/vector_icons/vector_icons.h"
#include "chrome/browser/accessibility/embedded_a11y_extension_loader.h"
#include "chrome/browser/language/language_model_manager_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/browser/ui/browser_window/public/browser_window_features.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/side_panel/read_anything/read_anything_container_view.h"
#include "chrome/browser/ui/views/side_panel/read_anything/read_anything_controller.h"
#include "chrome/browser/ui/views/side_panel/read_anything/read_anything_side_panel_controller.h"
#include "chrome/browser/ui/views/side_panel/read_anything/read_anything_side_panel_controller_utils.h"
#include "chrome/browser/ui/views/side_panel/read_anything/read_anything_side_panel_web_view.h"
#include "chrome/browser/ui/views/side_panel/read_anything/read_anything_tab_helper.h"
#include "chrome/browser/ui/views/side_panel/read_anything/read_anything_toolbar_view.h"
#include "chrome/browser/ui/views/side_panel/side_panel_coordinator.h"
#include "chrome/browser/ui/views/side_panel/side_panel_registry.h"
#include "chrome/browser/ui/views/side_panel/side_panel_web_ui_view.h"
#include "chrome/browser/ui/webui/side_panel/read_anything/read_anything_prefs.h"
#include "chrome/browser/ui/webui/side_panel/read_anything/read_anything_untrusted_ui.h"
#include "chrome/common/extensions/extension_constants.h"
#include "chrome/grit/generated_resources.h"
#include "components/feature_engagement/public/feature_constants.h"
#include "components/language/core/browser/language_model.h"
#include "components/language/core/browser/language_model_manager.h"
#include "components/language/core/common/locale_util.h"
#include "ui/accessibility/accessibility_features.h"
#include "ui/base/l10n/l10n_util.h"

#if BUILDFLAG(IS_CHROMEOS_LACROS)
#include "chrome/browser/lacros/embedded_a11y_manager_lacros.h"
#endif  // BUILDFLAG(IS_CHROMEOS_LACROS)

namespace {

// Get the list of distillable URLs defined by the experiment parameter.
// When ReadAnythingCoordinator observes a tab change, page load complete, or
// primary page change, it compares the active url against this list of urls
// to see whether the active page is considered "distillable". This information
// is then passed on to observers which are gated behind the IPH experiment.
// The list of URLs will be associated as a param of the experiment to ensure
// that the variation groups that have the experiment enabled also have the list
// of urls as a param.
std::vector<std::string> GetDistillableURLs() {
  if (base::FeatureList::IsEnabled(
          feature_engagement::kIPHReadingModeSidePanelFeature)) {
    return base::SplitString(
        base::GetFieldTrialParamValueByFeature(
            feature_engagement::kIPHReadingModeSidePanelFeature,
            "distillable_urls"),
        ",", base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL);
  }
  return std::vector<std::string>();
}

base::TimeDelta GetDelaySeconds() {
  // TODO(francisjp): Pull this value from variation.
  return base::Seconds(2);
}

}  // namespace

ReadAnythingCoordinator::ReadAnythingCoordinator(Browser* browser)
    : BrowserUserData<ReadAnythingCoordinator>(*browser),
      distillable_urls_(GetDistillableURLs()),
      delay_timer_(FROM_HERE,
                   GetDelaySeconds(),
                   base::BindRepeating(
                       &ReadAnythingCoordinator::OnTabChangeDelayComplete,
                       base::Unretained(this))) {
  // Create the model and initialize it with user prefs (if present).
  model_ = std::make_unique<ReadAnythingModel>();
  InitModelWithUserPrefs();

  // Create the controller.
  controller_ = std::make_unique<ReadAnythingController>(model_.get(), browser);

  browser->tab_strip_model()->AddObserver(this);
  Observe(GetActiveWebContents());
  if (features::IsReadAnythingLocalSidePanelEnabled()) {
    CreateAndRegisterEntriesForExistingWebContents(browser->tab_strip_model());
  }

  if (features::IsDataCollectionModeForScreen2xEnabled()) {
    BrowserList::GetInstance()->AddObserver(this);
  }

  if (features::IsReadAnythingDocsIntegrationEnabled()) {
    EmbeddedA11yExtensionLoader::GetInstance()->Init();
  }
}

void ReadAnythingCoordinator::InitModelWithUserPrefs() {
  Browser* browser = &GetBrowser();
  if (!browser->profile() || !browser->profile()->GetPrefs()) {
    return;
  }

  // Get user's default language to check for compatible fonts.
  language::LanguageModel* language_model =
      LanguageModelManagerFactory::GetForBrowserContext(browser->profile())
          ->GetPrimaryModel();
  std::string prefs_lang = language_model->GetLanguages().front().lang_code;
  prefs_lang = language::ExtractBaseLanguage(prefs_lang);

  std::string prefs_font_name = browser->profile()->GetPrefs()->GetString(
      prefs::kAccessibilityReadAnythingFontName);

  double prefs_font_scale = browser->profile()->GetPrefs()->GetDouble(
      prefs::kAccessibilityReadAnythingFontScale);

  bool prefs_links_enabled = browser->profile()->GetPrefs()->GetBoolean(
      prefs::kAccessibilityReadAnythingLinksEnabled);

  bool prefs_images_enabled = browser->profile()->GetPrefs()->GetBoolean(
      prefs::kAccessibilityReadAnythingImagesEnabled);

  read_anything::mojom::Colors prefs_colors =
      static_cast<read_anything::mojom::Colors>(
          browser->profile()->GetPrefs()->GetInteger(
              prefs::kAccessibilityReadAnythingColorInfo));

  read_anything::mojom::LineSpacing prefs_line_spacing =
      static_cast<read_anything::mojom::LineSpacing>(
          browser->profile()->GetPrefs()->GetInteger(
              prefs::kAccessibilityReadAnythingLineSpacing));

  read_anything::mojom::LetterSpacing prefs_letter_spacing =
      static_cast<read_anything::mojom::LetterSpacing>(
          browser->profile()->GetPrefs()->GetInteger(
              prefs::kAccessibilityReadAnythingLetterSpacing));

  model_->Init(
      /* lang code = */ prefs_lang,
      /* font name= */ prefs_font_name,
      /* font scale = */ prefs_font_scale,
      /* links enabled = */ prefs_links_enabled,
      /* images_enabled = */ prefs_images_enabled,
      /* colors = */ prefs_colors,
      /* line spacing = */ prefs_line_spacing,
      /* letter spacing = */ prefs_letter_spacing);
  default_language_code_ = prefs_lang;
  for (Observer& obs : observers_) {
    obs.SetDefaultLanguageCode(prefs_lang);
  }
}

ReadAnythingCoordinator::~ReadAnythingCoordinator() {
  local_side_panel_switch_delay_timer_.Stop();

  if (features::IsReadAnythingDocsIntegrationEnabled()) {
    RemoveGDocsHelperExtension();
  }

  // Inform observers when |this| is destroyed so they can do their own cleanup.
  for (Observer& obs : observers_) {
    obs.OnCoordinatorDestroyed();
  }

  // Deregister Read Anything from the global side panel registry. This removes
  // Read Anything as a side panel entry observer.
  Browser* browser = &GetBrowser();
  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser);
  if (!browser_view) {
    return;
  }

  // Deregisters the Read Anything side panel if it is not local. When a side
  // panel entry is global, it has the same lifetime as the browser.
  if (!features::IsReadAnythingLocalSidePanelEnabled()) {
    SidePanelRegistry* global_registry =
        SidePanelCoordinator::GetGlobalSidePanelRegistry(browser);
    global_registry->Deregister(
        SidePanelEntry::Key(SidePanelEntry::Id::kReadAnything));
  }

  browser->tab_strip_model()->RemoveObserver(this);
  Observe(nullptr);
}

void ReadAnythingCoordinator::CreateAndRegisterEntry(
    SidePanelRegistry* global_registry) {
  auto side_panel_entry = std::make_unique<SidePanelEntry>(
      SidePanelEntry::Id::kReadAnything,
      base::BindRepeating(&ReadAnythingCoordinator::CreateContainerView,
                          base::Unretained(this)));
  side_panel_entry->AddObserver(this);
  global_registry->Register(std::move(side_panel_entry));
}

void ReadAnythingCoordinator::CreateAndRegisterEntriesForExistingWebContents(
    TabStripModel* tab_strip_model) {
  for (int index = 0; index < tab_strip_model->GetTabCount(); ++index) {
    CreateAndRegisterEntryForWebContents(
        tab_strip_model->GetWebContentsAt(index));
  }
}

void ReadAnythingCoordinator::CreateAndRegisterEntryForWebContents(
    content::WebContents* web_contents) {
  CHECK(web_contents);
  ReadAnythingTabHelper* tab_helper =
      ReadAnythingTabHelper::FromWebContents(web_contents);
  CHECK(tab_helper);
  tab_helper->CreateAndRegisterEntry();
}

ReadAnythingController* ReadAnythingCoordinator::GetController() {
  return controller_.get();
}

ReadAnythingModel* ReadAnythingCoordinator::GetModel() {
  return model_.get();
}

void ReadAnythingCoordinator::AddObserver(
    ReadAnythingCoordinator::Observer* observer) {
  observers_.AddObserver(observer);

  // InitModelWithUserPrefs where default_language_code_ is set may be called
  // before all observerers have been added, so ensure that observers are
  // updated with the correct language code as they're added.
  observer->SetDefaultLanguageCode(default_language_code_);
}
void ReadAnythingCoordinator::RemoveObserver(
    ReadAnythingCoordinator::Observer* observer) {
  observers_.RemoveObserver(observer);
}
void ReadAnythingCoordinator::AddModelObserver(
    ReadAnythingModel::Observer* observer) {
  DCHECK(model_);
  model_->AddObserver(observer);
}
void ReadAnythingCoordinator::RemoveModelObserver(
    ReadAnythingModel::Observer* observer) {
  DCHECK(model_);
  model_->RemoveObserver(observer);
}

void ReadAnythingCoordinator::OnEntryShown(SidePanelEntry* entry) {
  DCHECK(entry->key().id() == SidePanelEntry::Id::kReadAnything);
  OnReadAnythingSidePanelEntryShown();
}

void ReadAnythingCoordinator::OnEntryHidden(SidePanelEntry* entry) {
  DCHECK(entry->key().id() == SidePanelEntry::Id::kReadAnything);
  OnReadAnythingSidePanelEntryHidden();
}

void ReadAnythingCoordinator::OnReadAnythingSidePanelEntryShown() {
  for (Observer& obs : observers_) {
    obs.Activate(true);
  }

  if (!features::IsReadAnythingDocsIntegrationEnabled()) {
    return;
  }

  if (!features::IsReadAnythingLocalSidePanelEnabled()) {
    InstallGDocsHelperExtension();
    return;
  }

  active_local_side_panel_count_++;
  InstallGDocsHelperExtension();
}

void ReadAnythingCoordinator::OnReadAnythingSidePanelEntryHidden() {
  for (Observer& obs : observers_) {
    obs.Activate(false);
  }

  if (!features::IsReadAnythingDocsIntegrationEnabled()) {
    return;
  }

  if (!features::IsReadAnythingLocalSidePanelEnabled()) {
    RemoveGDocsHelperExtension();
    return;
  }

  active_local_side_panel_count_--;
  local_side_panel_switch_delay_timer_.Stop();
  local_side_panel_switch_delay_timer_.Start(
      FROM_HERE, base::Seconds(30),
      base::BindRepeating(
          &ReadAnythingCoordinator::OnLocalSidePanelSwitchDelayTimeout,
          weak_ptr_factory_.GetWeakPtr()));
}

std::unique_ptr<views::View> ReadAnythingCoordinator::CreateContainerView() {
  Browser* browser = &GetBrowser();
  auto web_view =
      std::make_unique<ReadAnythingSidePanelWebView>(browser->profile());

  if (features::IsReadAnythingWebUIToolbarEnabled()) {
    return std::move(web_view);
  }

  // Create the views.
  auto toolbar = std::make_unique<ReadAnythingToolbarView>(
      this,
      /* ReadAnythingToolbarView::Delegate* = */ controller_.get(),
      /* ReadAnythingFontCombobox::Delegate* = */ controller_.get());

  // Create the component.
  // Note that a coordinator would normally maintain ownership of these objects,
  // but objects extending {ui/views/view.h} prefer ownership over raw pointers.
  auto container_view = std::make_unique<ReadAnythingContainerView>(
      this, std::move(toolbar), std::move(web_view));

  return std::move(container_view);
}

void ReadAnythingCoordinator::StartPageChangeDelay() {
  // Reset the delay status.
  post_tab_change_delay_complete_ = false;
  // Cancel any existing page change delay and start again.
  delay_timer_.Reset();
}

void ReadAnythingCoordinator::OnTabChangeDelayComplete() {
  CHECK(!post_tab_change_delay_complete_);
  post_tab_change_delay_complete_ = true;
  auto* web_contents = GetActiveWebContents();
  // Web contents should be checked before starting the delay, and the timer
  // will be canceled if the user navigates or leaves the tab.
  CHECK(web_contents);
  if (!web_contents->IsLoading()) {
    // Ability to show was already checked before timer was started.
    ActivePageDistillable();
  }
}

void ReadAnythingCoordinator::OnTabStripModelChanged(
    TabStripModel* tab_strip_model,
    const TabStripModelChange& change,
    const TabStripSelectionChange& selection) {
  // If the Read Anything side panel is local, creates and registers a side
  // panel entry for each tab.
  if (features::IsReadAnythingLocalSidePanelEnabled()) {
    if (change.type() == TabStripModelChange::Type::kInserted) {
      for (const auto& inserted_tab : change.GetInsert()->contents) {
        CreateAndRegisterEntryForWebContents(inserted_tab.contents);
      }
    }
    if (change.type() == TabStripModelChange::Type::kReplaced) {
      content::WebContents* new_contents = change.GetReplace()->new_contents;
      if (new_contents) {
        CreateAndRegisterEntryForWebContents(new_contents);
      }
    }
  }
  if (!selection.active_tab_changed()) {
    return;
  }
  Observe(GetActiveWebContents());
  if (IsActivePageDistillable()) {
    StartPageChangeDelay();
  } else {
    ActivePageNotDistillable();
  }
}

void ReadAnythingCoordinator::DidStopLoading() {
  if (!post_tab_change_delay_complete_) {
    return;
  }
  if (IsActivePageDistillable()) {
    ActivePageDistillable();
  } else {
    ActivePageNotDistillable();
  }
}

void ReadAnythingCoordinator::PrimaryPageChanged(content::Page& page) {
  // On navigation, cancel any running delays.
  delay_timer_.Stop();

  if (!IsActivePageDistillable()) {
    // On navigation, if we shouldn't show the IPH hide it. Otherwise continue
    // to show it.
    ActivePageNotDistillable();
  }
}

content::WebContents* ReadAnythingCoordinator::GetActiveWebContents() const {
  return GetBrowser().tab_strip_model()->GetActiveWebContents();
}

bool ReadAnythingCoordinator::IsActivePageDistillable() const {
  auto* web_contents = GetActiveWebContents();
  if (!web_contents) {
    return false;
  }

  auto url = web_contents->GetLastCommittedURL();

  for (auto distillable : distillable_urls_) {
    // If the url's domain is found in distillable urls AND the url has a
    // filename (i.e. it is not a home page or sub-home page), show the promo.
    if (url.DomainIs(distillable) && !url.ExtractFileName().empty()) {
      return true;
    }
  }
  return false;
}

void ReadAnythingCoordinator::ActivePageNotDistillable() {
  GetBrowser().window()->CloseFeaturePromo(
      feature_engagement::kIPHReadingModeSidePanelFeature);
  for (Observer& obs : observers_) {
    obs.OnActivePageDistillable(false);
  }
}

void ReadAnythingCoordinator::ActivePageDistillable() {
  GetBrowser().window()->MaybeShowFeaturePromo(
      feature_engagement::kIPHReadingModeSidePanelFeature);
  for (Observer& obs : observers_) {
    obs.OnActivePageDistillable(true);
  }
}

void ReadAnythingCoordinator::InstallGDocsHelperExtension() {
#if BUILDFLAG(IS_CHROMEOS_LACROS)
  EmbeddedA11yManagerLacros::GetInstance()->SetReadingModeEnabled(true);
#else
  EmbeddedA11yExtensionLoader::GetInstance()->InstallExtensionWithId(
      extension_misc::kReadingModeGDocsHelperExtensionId,
      extension_misc::kReadingModeGDocsHelperExtensionPath,
      extension_misc::kReadingModeGDocsHelperManifestFilename,
      /*should_localize=*/false);
#endif  // BUILDFLAG(IS_CHROMEOS_LACROS)
}

void ReadAnythingCoordinator::RemoveGDocsHelperExtension() {
#if BUILDFLAG(IS_CHROMEOS_LACROS)
  EmbeddedA11yManagerLacros::GetInstance()->SetReadingModeEnabled(false);
#else
  EmbeddedA11yExtensionLoader::GetInstance()->RemoveExtensionWithId(
      extension_misc::kReadingModeGDocsHelperExtensionId);
#endif  // BUILDFLAG(IS_CHROMEOS_LACROS)
}

void ReadAnythingCoordinator::ActivePageNotDistillableForTesting() {
  ActivePageNotDistillable();
}

void ReadAnythingCoordinator::ActivePageDistillableForTesting() {
  ActivePageDistillable();
}

void ReadAnythingCoordinator::OnBrowserSetLastActive(Browser* browser) {
  if (!features::IsDataCollectionModeForScreen2xEnabled() ||
      browser != &GetBrowser()) {
    return;
  }
  // This code is called as part of a screen2x data generation workflow, where
  // the browser is opened by a CLI and the read-anything side panel is
  // automatically opened.
  auto* side_panel_ui = browser->GetFeatures().side_panel_ui();
  if (side_panel_ui->GetCurrentEntryId() != SidePanelEntryId::kReadAnything) {
    side_panel_ui->Show(SidePanelEntryId::kReadAnything);
  }
}

void ReadAnythingCoordinator::OnLocalSidePanelSwitchDelayTimeout() {
  if (active_local_side_panel_count_ > 0) {
    return;
  }

  RemoveGDocsHelperExtension();
}

BROWSER_USER_DATA_KEY_IMPL(ReadAnythingCoordinator);
