// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/tabs/saved_tab_groups/saved_tab_group_utils.h"

#include <numeric>
#include <unordered_set>

#include "base/metrics/user_metrics.h"
#include "base/strings/utf_string_conversions.h"
#include "base/uuid.h"
#include "chrome/app/vector_icons/vector_icons.h"
#include "chrome/browser/favicon/favicon_utils.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_element_identifiers.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/browser/ui/browser_navigator.h"
#include "chrome/browser/ui/tabs/saved_tab_groups/saved_tab_group_keyed_service.h"
#include "chrome/browser/ui/tabs/saved_tab_groups/saved_tab_group_service_factory.h"
#include "chrome/browser/ui/tabs/tab_group_deletion_dialog_controller.h"
#include "chrome/browser/ui/tabs/tab_group_model.h"
#include "chrome/browser/ui/tabs/tab_group_theme.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model_delegate.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/common/pref_names.h"
#include "chrome/grit/generated_resources.h"
#include "components/saved_tab_groups/features.h"
#include "components/saved_tab_groups/pref_names.h"
#include "components/saved_tab_groups/saved_tab_group_tab.h"
#include "components/tab_groups/tab_group_id.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/interaction/element_identifier.h"
#include "ui/base/interaction/element_tracker.h"
#include "ui/base/l10n/l10n_util.h"
#include "url/gurl.h"

namespace {
static constexpr int kOldIconSize = 20;
static constexpr int kUIUpdateIconSize = 16;
}  // namespace

namespace tab_groups {

DEFINE_CLASS_ELEMENT_IDENTIFIER_VALUE(SavedTabGroupUtils, kDeleteGroupMenuItem);
DEFINE_CLASS_ELEMENT_IDENTIFIER_VALUE(SavedTabGroupUtils,
                                      kMoveGroupToNewWindowMenuItem);
DEFINE_CLASS_ELEMENT_IDENTIFIER_VALUE(SavedTabGroupUtils,
                                      kToggleGroupPinStateMenuItem);
DEFINE_CLASS_ELEMENT_IDENTIFIER_VALUE(SavedTabGroupUtils, kTabsTitleItem);
DEFINE_CLASS_ELEMENT_IDENTIFIER_VALUE(SavedTabGroupUtils, kTab);

// static
void SavedTabGroupUtils::RemoveGroupFromTabstrip(
    const Browser* browser,
    const tab_groups::TabGroupId& local_group) {
  const Browser* const browser_with_local_group_id =
      browser ? browser
              : SavedTabGroupUtils::GetBrowserWithTabGroupId(local_group);
  DCHECK(browser_with_local_group_id);
  if (!browser_with_local_group_id) {
    return;
  }

  TabStripModel* const tab_strip_model =
      browser_with_local_group_id->tab_strip_model();

  const int num_tabs_in_group =
      tab_strip_model->group_model()->GetTabGroup(local_group)->tab_count();
  if (tab_strip_model->count() == num_tabs_in_group) {
    // If the group about to be closed has all of the tabs in the browser, add a
    // new tab outside the group to prevent the browser from closing.
    tab_strip_model->delegate()->AddTabAt(GURL(), -1, true);
  }

  tab_strip_model->CloseAllTabsInGroup(local_group);
}

// static
void SavedTabGroupUtils::UngroupSavedGroup(const Browser* browser,
                                           const base::Uuid& saved_group_guid) {
  tab_groups::SavedTabGroupKeyedService* const saved_tab_group_service =
      tab_groups::SavedTabGroupServiceFactory::GetForProfile(
          browser->profile());
  if (!saved_tab_group_service) {
    return;
  }

  // The group must exist and be in the tabstrip for ungrouping.
  const SavedTabGroup* group =
      saved_tab_group_service->model()->Get(saved_group_guid);
  if (!group || !group->local_group_id().has_value()) {
    return;
  }

  base::OnceCallback<void()> ungroup_callback = base::BindOnce(
      [](const Browser* browser, const tab_groups::TabGroupId& local_group) {
        TabStripModel* const model = browser->tab_strip_model();
        const gfx::Range tab_range =
            model->group_model()->GetTabGroup(local_group)->ListTabs();

        std::vector<int> tabs;
        tabs.reserve(tab_range.length());
        for (auto i = tab_range.start(); i < tab_range.end(); ++i) {
          tabs.push_back(i);
        }

        model->RemoveFromGroup(tabs);
      },
      browser, group->local_group_id().value());

  if (tab_groups::IsTabGroupsSaveV2Enabled()) {
    browser->tab_group_deletion_dialog_controller()->MaybeShowDialog(
        tab_groups::DeletionDialogController::DialogType::UngroupSingle,
        std::move(ungroup_callback), group->saved_tabs().size(), 1);
  } else {
    std::move(ungroup_callback).Run();
  }
}

// static
void SavedTabGroupUtils::DeleteSavedGroup(const Browser* browser,
                                          const base::Uuid& saved_group_guid) {
  tab_groups::SavedTabGroupKeyedService* const saved_tab_group_service =
      tab_groups::SavedTabGroupServiceFactory::GetForProfile(
          browser->profile());
  if (!saved_tab_group_service) {
    return;
  }

  const SavedTabGroup* saved_group =
      saved_tab_group_service->model()->Get(saved_group_guid);

  if (!saved_group) {
    return;
  }

  base::OnceCallback<void()> close_callback = base::BindOnce(
      [](const Browser* browser, const base::Uuid& saved_group_guid) {
        tab_groups::SavedTabGroupKeyedService* const saved_tab_group_service =
            tab_groups::SavedTabGroupServiceFactory::GetForProfile(
                browser->profile());
        if (!saved_tab_group_service) {
          return;
        }

        const SavedTabGroup* group =
            saved_tab_group_service->model()->Get(saved_group_guid);
        if (!group) {
          return;
        }

        if (group->local_group_id().has_value()) {
          SavedTabGroupUtils::RemoveGroupFromTabstrip(
              nullptr, group->local_group_id().value());
        }
        saved_tab_group_service->model()->Remove(group->saved_guid());
      },
      browser, saved_group_guid);

  if (tab_groups::IsTabGroupsSaveV2Enabled()) {
    browser->tab_group_deletion_dialog_controller()->MaybeShowDialog(
        tab_groups::DeletionDialogController::DialogType::DeleteSingle,
        std::move(close_callback), saved_group->saved_tabs().size(), 1);
  } else {
    std::move(close_callback).Run();
  }
}

// See comment for TabStripModelDelegate::ConfirmGroupDeletion
bool SavedTabGroupUtils::MaybeShowSavedTabGroupDeletionDialog(
    Browser* browser,
    DeletionDialogController::DialogType type,
    const std::vector<TabGroupId>& group_ids,
    base::OnceCallback<void()> callback) {
  SavedTabGroupKeyedService* saved_tab_group_service =
      SavedTabGroupServiceFactory::GetForProfile(browser->profile());

  // Confirmation is only needed if SavedTabGroups are being deleted. If the
  // service doesnt exist there are no saved tab groups.
  if (!saved_tab_group_service || !IsTabGroupsSaveV2Enabled()) {
    return true;
  }

  // If there's no way to show the group deletion dialog, then fallback to
  // running the callback.
  auto* dialog_controller = browser->tab_group_deletion_dialog_controller();
  if (!dialog_controller || !dialog_controller->CanShowDialog()) {
    return true;
  }

  // Check to see if any of the groups are saved. If so then show the dialog,
  // else, just perform the callback. Also count the number of group and tabs.
  int num_saved_tabs = 0;
  int num_saved_groups = 0;
  for (const auto& group : group_ids) {
    const SavedTabGroup* const saved_group =
        saved_tab_group_service->model()->Get(group);
    if (!saved_group) {
      continue;
    }

    num_saved_tabs += saved_group->saved_tabs().size();
    ++num_saved_groups;
  }

  if (num_saved_groups > 0) {
    return !dialog_controller->MaybeShowDialog(
        type, std::move(callback), num_saved_tabs, num_saved_groups);
  }
  return true;
}

void SavedTabGroupUtils::OpenUrlToBrowser(Browser* browser, const GURL& url) {
  NavigateParams params(browser, url, ui::PAGE_TRANSITION_AUTO_BOOKMARK);
  params.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
  params.started_from_context_menu = true;
  Navigate(&params);
}

void SavedTabGroupUtils::OpenOrMoveSavedGroupToNewWindow(
    Browser* browser,
    const base::Uuid& saved_group_guid) {
  auto* const service =
      SavedTabGroupServiceFactory::GetForProfile(browser->profile());
  const auto* const save_group = service->model()->Get(saved_group_guid);
  // In case the group has been deleted.
  if (!save_group) {
    return;
  }

  const auto& local_group_id = save_group->local_group_id();
  Browser* const browser_with_local_group_id =
      local_group_id.has_value()
          ? SavedTabGroupUtils::GetBrowserWithTabGroupId(local_group_id.value())
          : browser;

  if (!local_group_id.has_value()) {
    // Open the group in the browser the button was pressed.
    // NOTE: This action could cause `this` to be deleted. Make sure lines
    // following this have either copied data by value or hold pointers to the
    // objects it needs.
    service->OpenSavedTabGroupInBrowser(
        browser_with_local_group_id, saved_group_guid,
        tab_groups::OpeningSource::kOpenedFromRevisitUi);
  }

  // Move the open group to a new browser window.
  browser_with_local_group_id->tab_strip_model()
      ->delegate()
      ->MoveGroupToNewWindow(save_group->local_group_id().value());
}

void SavedTabGroupUtils::ToggleGroupPinState(
    Browser* browser,
    const base::Uuid& saved_group_guid) {
  auto* const service =
      SavedTabGroupServiceFactory::GetForProfile(browser->profile());
  service->model()->TogglePinState(saved_group_guid);
}

std::unique_ptr<ui::DialogModel>
SavedTabGroupUtils::CreateSavedTabGroupContextMenuModel(
    Browser* browser,
    const base::Uuid& saved_guid) {
  const auto* const service =
      SavedTabGroupServiceFactory::GetForProfile(browser->profile());
  const auto* const saved_group = service->model()->Get(saved_guid);
  ui::DialogModel::Builder dialog_model = ui::DialogModel::Builder();
  // In case the group has been deleted, return an empty dialog model.
  if (!saved_group) {
    return dialog_model.Build();
  }
  const auto& local_group_id = saved_group->local_group_id();

  const std::u16string move_or_open_group_text =
      local_group_id.has_value()
          ? l10n_util::GetStringUTF16(
                IDS_TAB_GROUP_HEADER_CXMENU_MOVE_GROUP_TO_NEW_WINDOW)
          : l10n_util::GetStringUTF16(
                IDS_TAB_GROUP_HEADER_CXMENU_OPEN_GROUP_IN_NEW_WINDOW);

  bool should_enable_move_menu_item = true;
  if (local_group_id.has_value()) {
    const Browser* const browser_with_local_group_id =
        SavedTabGroupUtils::GetBrowserWithTabGroupId(local_group_id.value());
    const TabStripModel* const tab_strip_model =
        browser_with_local_group_id->tab_strip_model();

    // Show the menu item if there are tabs outside of the saved group.
    should_enable_move_menu_item =
        tab_strip_model->count() != tab_strip_model->group_model()
                                        ->GetTabGroup(local_group_id.value())
                                        ->tab_count();
  }

  bool is_ui_update = tab_groups::IsTabGroupsSaveUIUpdateEnabled();
  dialog_model.AddMenuItem(
      ui::ImageModel::FromVectorIcon(
          kMoveGroupToNewWindowRefreshIcon, ui::kColorMenuIcon,
          is_ui_update ? kUIUpdateIconSize : kOldIconSize),
      move_or_open_group_text,
      base::BindRepeating(
          [](Browser* browser, const base::Uuid& saved_group_guid,
             int event_flags) {
            SavedTabGroupUtils::OpenOrMoveSavedGroupToNewWindow(
                browser, saved_group_guid);
          },
          browser, saved_group->saved_guid()),
      ui::DialogModelMenuItem::Params()
          .SetId(kMoveGroupToNewWindowMenuItem)
          .SetIsEnabled(should_enable_move_menu_item));

  if (is_ui_update) {
    dialog_model.AddMenuItem(
        ui::ImageModel::FromVectorIcon(
            saved_group->is_pinned() ? kKeepPinFilledChromeRefreshIcon
                                     : kKeepPinChromeRefreshIcon,
            ui::kColorMenuIcon,
            is_ui_update ? kUIUpdateIconSize : kOldIconSize),
        l10n_util::GetStringUTF16(saved_group->is_pinned()
                                      ? IDS_TAB_GROUP_HEADER_CXMENU_UNPIN_GROUP
                                      : IDS_TAB_GROUP_HEADER_CXMENU_PIN_GROUP),
        base::BindRepeating(
            [](Browser* browser, const base::Uuid& saved_group_guid,
               int event_flags) {
              SavedTabGroupUtils::ToggleGroupPinState(browser,
                                                      saved_group_guid);
            },
            browser, saved_group->saved_guid()),
        ui::DialogModelMenuItem::Params().SetId(kToggleGroupPinStateMenuItem));
  }

  dialog_model
      .AddMenuItem(
          ui::ImageModel::FromVectorIcon(
              kCloseGroupRefreshIcon, ui::kColorMenuIcon,
              is_ui_update ? kUIUpdateIconSize : kOldIconSize),
          l10n_util::GetStringUTF16(IDS_TAB_GROUP_HEADER_CXMENU_DELETE_GROUP),
          base::BindRepeating(
              [](const Browser* browser, const base::Uuid& saved_group_guid,
                 int event_flags) {
                SavedTabGroupUtils::DeleteSavedGroup(browser, saved_group_guid);
              },
              browser, saved_group->saved_guid()),
          ui::DialogModelMenuItem::Params().SetId(kDeleteGroupMenuItem))
      .AddSeparator();

  if (is_ui_update) {
    dialog_model.AddTitleItem(l10n_util::GetStringUTF16(IDS_TABS_TITLE_CXMENU),
                              kTabsTitleItem);
  }

  for (const SavedTabGroupTab& tab : saved_group->saved_tabs()) {
    const ui::ImageModel& image =
        tab.favicon().has_value()
            ? ui::ImageModel::FromImage(tab.favicon().value())
            : favicon::GetDefaultFaviconModel(
                  GetTabGroupBookmarkColorId(saved_group->color()));
    const std::u16string title =
        tab.title().empty() ? base::UTF8ToUTF16(tab.url().spec()) : tab.title();
    dialog_model.AddMenuItem(
        image, title,

        base::BindRepeating(
            [](Browser* browser, const GURL& url, int event_flags) {
              SavedTabGroupUtils::OpenUrlToBrowser(browser, url);
            },
            browser, tab.url()));
  }

  return dialog_model.Build();
}

SavedTabGroupTab SavedTabGroupUtils::CreateSavedTabGroupTabFromWebContents(
    content::WebContents* contents,
    base::Uuid saved_tab_group_id) {
  // in order to protect from filesystem access or chrome settings page use,
  // replace the URL with the new tab page, when creating from sync or an
  // unsaved group.
  if (!IsURLValidForSavedTabGroups(contents->GetVisibleURL())) {
    return SavedTabGroupTab(GURL(chrome::kChromeUINewTabURL), u"Unsavable tab",
                            saved_tab_group_id,
                            /*position=*/std::nullopt);
  }

  SavedTabGroupTab tab(contents->GetVisibleURL(), contents->GetTitle(),
                       saved_tab_group_id, /*position=*/std::nullopt);
  tab.SetFavicon(favicon::TabFaviconFromWebContents(contents));
  return tab;
}

content::NavigationHandle* SavedTabGroupUtils::OpenTabInBrowser(
    const GURL& url,
    Browser* browser,
    Profile* profile,
    WindowOpenDisposition disposition,
    std::optional<int> tabstrip_index,
    std::optional<tab_groups::TabGroupId> local_group_id) {
  NavigateParams params(profile, url, ui::PAGE_TRANSITION_AUTO_BOOKMARK);
  params.disposition = disposition;
  params.browser = browser;
  params.tabstrip_index = tabstrip_index.value_or(params.tabstrip_index);
  params.group = local_group_id;
  base::WeakPtr<content::NavigationHandle> handle = Navigate(&params);
  return handle.get();
}

// static
Browser* SavedTabGroupUtils::GetBrowserWithTabGroupId(
    tab_groups::TabGroupId group_id) {
  for (Browser* browser : *BrowserList::GetInstance()) {
    const TabStripModel* const tab_strip_model = browser->tab_strip_model();
    if (tab_strip_model && tab_strip_model->SupportsTabGroups() &&
        tab_strip_model->group_model()->ContainsTabGroup(group_id)) {
      return browser;
    }
  }
  return nullptr;
}

// static
TabGroup* SavedTabGroupUtils::GetTabGroupWithId(
    tab_groups::TabGroupId group_id) {
  Browser* browser = GetBrowserWithTabGroupId(group_id);
  if (!browser || !browser->tab_strip_model() ||
      !browser->tab_strip_model()->SupportsTabGroups()) {
    return nullptr;
  }

  TabGroupModel* tab_group_model = browser->tab_strip_model()->group_model();
  CHECK(tab_group_model);

  return tab_group_model->GetTabGroup(group_id);
}

// static
std::vector<content::WebContents*> SavedTabGroupUtils::GetWebContentsesInGroup(
    tab_groups::TabGroupId group_id) {
  Browser* browser = GetBrowserWithTabGroupId(group_id);
  if (!browser || !browser->tab_strip_model() ||
      !browser->tab_strip_model()->SupportsTabGroups()) {
    return {};
  }

  const gfx::Range local_tab_group_indices =
      SavedTabGroupUtils::GetTabGroupWithId(group_id)->ListTabs();
  std::vector<content::WebContents*> contentses;
  for (size_t index = local_tab_group_indices.start();
       index < local_tab_group_indices.end(); index++) {
    contentses.push_back(browser->tab_strip_model()->GetWebContentsAt(index));
  }
  return contentses;
}

std::unordered_set<std::string> SavedTabGroupUtils::GetURLsInSavedTabGroup(
    const tab_groups::SavedTabGroupKeyedService& saved_tab_group_service,
    const base::Uuid& saved_id) {
  const tab_groups::SavedTabGroup* const saved_group =
      saved_tab_group_service.model()->Get(saved_id);
  CHECK(saved_group);

  std::unordered_set<std::string> saved_urls;
  for (const tab_groups::SavedTabGroupTab& saved_tab :
       saved_group->saved_tabs()) {
    saved_urls.emplace(saved_tab.url().spec());
  }

  return saved_urls;
}

void SavedTabGroupUtils::MoveGroupToExistingWindow(
    Browser* source_browser,
    Browser* target_browser,
    const tab_groups::TabGroupId& local_group_id,
    const base::Uuid& saved_group_id) {
  CHECK(source_browser);
  CHECK(target_browser);
  tab_groups::SavedTabGroupKeyedService* const service =
      SavedTabGroupServiceFactory::GetForProfile(source_browser->profile());
  CHECK(service);

  // Find the grouped tabs in `source_browser`.
  gfx::Range tabs_to_move = source_browser->tab_strip_model()
                                ->group_model()
                                ->GetTabGroup(local_group_id)
                                ->ListTabs();
  int num_tabs_to_move = tabs_to_move.length();

  std::vector<int> tab_indicies_to_move(num_tabs_to_move);
  std::iota(tab_indicies_to_move.begin(), tab_indicies_to_move.end(),
            tabs_to_move.start());

  // Disconnect the group and move the tabs to `target_browser`.
  service->DisconnectLocalTabGroup(local_group_id);
  chrome::MoveTabsToExistingWindow(source_browser, target_browser,
                                   tab_indicies_to_move);

  // Tabs should be in `target_browser` now. Regroup them.
  int total_tabs = target_browser->tab_strip_model()->count();
  int first_tab_moved = total_tabs - num_tabs_to_move;
  std::vector<int> tabs_to_add_to_group(num_tabs_to_move);
  std::iota(tabs_to_add_to_group.begin(), tabs_to_add_to_group.end(),
            first_tab_moved);

  // Add group the tabs using the same local id, and reconnect everything.
  target_browser->tab_strip_model()->AddToGroupForRestore(tabs_to_add_to_group,
                                                          local_group_id);
  service->ConnectLocalTabGroup(local_group_id, saved_group_id);
}

void SavedTabGroupUtils::FocusFirstTabOrWindowInOpenGroup(
    tab_groups::TabGroupId local_group_id) {
  Browser* browser_for_activation =
      SavedTabGroupUtils::GetBrowserWithTabGroupId(local_group_id);

  // Only activate the tab group's first tab, if it exists in any browser's
  // tabstrip model and it is not in the active tab in the tab group.
  CHECK(browser_for_activation);
  TabGroup* tab_group =
      browser_for_activation->tab_strip_model()->group_model()->GetTabGroup(
          local_group_id);

  std::optional<int> first_tab = tab_group->GetFirstTab();
  std::optional<int> last_tab = tab_group->GetLastTab();
  int active_index = browser_for_activation->tab_strip_model()->active_index();
  CHECK(first_tab.has_value());
  CHECK(last_tab.has_value());
  CHECK_GE(active_index, 0);

  if (active_index >= first_tab.value() && active_index <= last_tab) {
    browser_for_activation->window()->Activate();
    return;
  }

  browser_for_activation->ActivateContents(
      browser_for_activation->tab_strip_model()->GetWebContentsAt(
          first_tab.value()));

  base::RecordAction(
      base::UserMetricsAction("TabGroups_SavedTabGroups_Focused"));
}

// static
bool SavedTabGroupUtils::IsURLValidForSavedTabGroups(const GURL& gurl) {
  return gurl.SchemeIsHTTPOrHTTPS() || gurl == GURL(chrome::kChromeUINewTabURL);
}

// static
ui::TrackedElement* SavedTabGroupUtils::GetAnchorElementForTabGroupsV2IPH(
    const ui::ElementTracker::ElementList& elements) {
  // If there's no AppMenu with an element ID, we cant find a
  // browser to show the IPH on.
  if (elements.empty()) {
    return nullptr;
  }

  // Get the context from the first element. This is the browser
  // that the IPH will be displayed in.
  ui::ElementContext context = elements[0]->context();

  // Get the OverflowButton from the bookmarks bar. If it exists
  // use it as the anchor.
  ui::TrackedElement* overflow_button_element =
      ui::ElementTracker::GetElementTracker()->GetFirstMatchingElement(
          kSavedTabGroupOverflowButtonElementId, context);
  if (overflow_button_element) {
    return overflow_button_element;
  }

  // Fallback to the AppMenuButton.
  return elements[0];
}

bool SavedTabGroupUtils::ShouldAutoPinNewTabGroups(Profile* profile) {
  return tab_groups::IsTabGroupsSaveUIUpdateEnabled() &&
         profile->GetPrefs()->GetBoolean(
             tab_groups::prefs::kAutoPinNewTabGroups);
}

}  // namespace tab_groups
