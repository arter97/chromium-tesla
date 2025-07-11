// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// A subset of the browser element identifiers are being used in Desktop UI
// benchmark. The name of the identifiers and the string names used by the
// benchmark are expected to be equal.
//
// Please keep the names in this file in sync with
// `tools/perf/page_sets/desktop_ui/browser_element_identifiers.py`

#ifndef CHROME_BROWSER_UI_BROWSER_ELEMENT_IDENTIFIERS_H_
#define CHROME_BROWSER_UI_BROWSER_ELEMENT_IDENTIFIERS_H_

#include "ui/base/interaction/element_identifier.h"
#include "ui/base/interaction/element_tracker.h"

// These should gradually replace values in view_ids.h.
// Please keep this list alphabetized.
DECLARE_ELEMENT_IDENTIFIER_VALUE(kAddCurrentTabToReadingListElementId);
DECLARE_ELEMENT_IDENTIFIER_VALUE(
    kAnonymizedUrlCollectionPersonalizationSettingId);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kAppUninstallDialogOkButtonId);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kAutofillCreditCardBenefitElementId);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kAutofillCreditCardSuggestionEntryElementId);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kAutofillManualFallbackElementId);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kAutofillStandaloneCvcSuggestionElementId);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kAutofillSuggestionElementId);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kBookmarkBarElementId);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kBookmarkSidePanelWebViewElementId);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kBookmarkStarViewElementId);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kBrowserViewElementId);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kConstrainedDialogWebViewElementId);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kCookieControlsIconElementId);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kCustomizeChromeSidePanelWebViewElementId);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kDataSharingBubbleElementId);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kDeviceSignalsConsentCancelButtonElementId);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kDeviceSignalsConsentOkButtonElementId);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kEnhancedProtectionSettingElementId);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kExclusiveAccessBubbleViewElementId);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kExtensionsMenuButtonElementId);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kExtensionsRequestAccessButtonElementId);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kMemorySaverChipElementId);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kInactiveTabSettingElementId);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kInstallPwaElementId);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kIntentChipElementId);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kLensOverlayPageActionIconElementId);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kLensPermissionDialogCancelButtonElementId);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kLensPermissionDialogOkButtonElementId);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kLensPreselectionBubbleElementId);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kLensSearchBubbleElementId);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kLocationIconElementId);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kNewTabButtonElementId);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kOfferNotificationChipElementId);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kOmniboxElementId);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kPasswordsOmniboxKeyIconElementId);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kPerformanceSidePanelWebViewElementId);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kPinnedActionToolbarButtonElementId);
DECLARE_ELEMENT_IDENTIFIER_VALUE(
    kPinnedToolbarActionsContainerDividerElementId);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kPinnedToolbarActionsContainerElementId);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kPriceInsightsChipElementId);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kPriceTrackingBookmarkViewElementId);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kPriceTrackingChipElementId);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kProductSpecificationsChipElementId);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kProductSpecificationsButtonElementId);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kReadLaterSidePanelWebViewElementId);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kReloadButtonElementId);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kSavePasswordComboboxElementId);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kSavedTabGroupBarElementId);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kSavedTabGroupButtonElementId);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kSavedTabGroupOverflowButtonElementId);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kSavedTabGroupOverflowMenuId);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kSearchEngineChoiceDialogId);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kSidePanelCloseButtonElementId);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kSidePanelComboboxElementId);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kSidePanelCompanionToolbarButtonElementId);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kSidePanelElementId);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kSidePanelOpenInNewTabButtonElementId);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kSidePanelPinButtonElementId);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kSidePanelReadingListUnreadElementId);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kSideSearchButtonElementId);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kSideSearchWebViewElementId);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kTabAlertIndicatorButtonElementId);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kTabIconElementId);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kTabElementId);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kTabGroupEditorBubbleCloseGroupButtonId);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kTabGroupEditorBubbleId);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kTabGroupEditorBubbleSaveToggleId);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kTabGroupHeaderElementId);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kTabOrganizationButtonElementId);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kTabSearchBubbleElementId);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kTabSearchButtonElementId);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kTabStripElementId);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kTabStripRegionElementId);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kToolbarAppMenuButtonElementId);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kToolbarAvatarBubbleElementId);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kToolbarAvatarButtonElementId);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kToolbarBackButtonElementId);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kToolbarBackButtonMenuElementId);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kToolbarBatterySaverButtonElementId);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kToolbarChromeLabsBubbleElementId);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kToolbarChromeLabsButtonElementId);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kToolbarDownloadBubbleElementId);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kToolbarDownloadButtonElementId);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kToolbarExtensionsContainerElementId);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kToolbarForwardButtonElementId);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kToolbarForwardButtonMenuElementId);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kToolbarHomeButtonElementId);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kToolbarMediaBubbleElementId);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kToolbarMediaButtonElementId);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kToolbarNewTabButtonElementId);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kToolbarOverflowButtonElementId);
DECLARE_ELEMENT_IDENTIFIER_VALUE(
    kToolbarPerformanceInterventionButtonElementId);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kToolbarSidePanelButtonElementId);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kToolbarSidePanelContainerElementId);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kToolbarTabCounterButtonElementId);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kTopContainerElementId);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kUserNotesSidePanelWebViewElementId);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kWebUIIPHDemoElementIdentifier);

DECLARE_CUSTOM_ELEMENT_EVENT_TYPE(kBrowserThemeChangedEventId);
DECLARE_CUSTOM_ELEMENT_EVENT_TYPE(kSidePanelComboboxChangedCustomEventId);
DECLARE_CUSTOM_ELEMENT_EVENT_TYPE(kSidePanelReadingMarkedAsReadEventId);
DECLARE_CUSTOM_ELEMENT_EVENT_TYPE(kTabGroupedCustomEventId);
DECLARE_CUSTOM_ELEMENT_EVENT_TYPE(kTabGroupSavedCustomEventId);

#endif  // CHROME_BROWSER_UI_BROWSER_ELEMENT_IDENTIFIERS_H_
