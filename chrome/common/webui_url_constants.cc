// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/common/webui_url_constants.h"

#include <string_view>

#include "build/build_config.h"
#include "build/chromeos_buildflags.h"
#include "components/commerce/core/commerce_constants.h"
#include "components/history_clusters/history_clusters_internals/webui/url_constants.h"
#include "components/lens/buildflags.h"
#include "components/nacl/common/buildflags.h"
#include "components/optimization_guide/optimization_guide_internals/webui/url_constants.h"
#include "components/password_manager/content/common/web_ui_constants.h"
#include "components/safe_browsing/core/common/web_ui_constants.h"
#include "device/vr/buildflags/buildflags.h"
#include "extensions/buildflags/buildflags.h"
#include "third_party/blink/public/common/chrome_debug_urls.h"

namespace chrome {

// Please keep this file in the same order as the header.

// Note: Add hosts to |kChromeHostURLs| at the bottom of this file to be listed
// by chrome://chrome-urls (about:about) and the built-in AutocompleteProvider.

const char kChromeUIAboutHost[] = "about";
const char kChromeUIAboutURL[] = "chrome://about/";
const char kChromeUIActivateSafetyCheckSettingsURL[] =
    "chrome://settings/safetyCheck?activateSafetyCheck";
const char kChromeUIAccessibilityHost[] = "accessibility";
const char kChromeUIAllSitesPath[] = "/content/all";
const char kChromeUIAppIconHost[] = "app-icon";
const char kChromeUIAppIconURL[] = "chrome://app-icon/";
const char kChromeUIAppLauncherPageHost[] = "apps";
const char kChromeUIAppsURL[] = "chrome://apps/";
const char kChromeUIAppsWithDeprecationDialogURL[] =
    "chrome://apps?showDeletionDialog=";
const char kChromeUIAppsWithForceInstalledDeprecationDialogURL[] =
    "chrome://apps?showForceInstallDialog=";
const char kChromeUIAutofillInternalsHost[] = "autofill-internals";
const char kChromeUIBluetoothInternalsHost[] = "bluetooth-internals";
const char kChromeUIBluetoothInternalsURL[] = "chrome://bluetooth-internals";
const char kChromeUIBookmarksHost[] = "bookmarks";
const char kChromeUIBookmarksURL[] = "chrome://bookmarks/";
const char kChromeUIBrowsingTopicsInternalsHost[] = "topics-internals";
const char kChromeUICertificateViewerHost[] = "view-cert";
const char kChromeUICertificateViewerURL[] = "chrome://view-cert/";
const char kChromeUIChromeSigninHost[] = "chrome-signin";
const char kChromeUIChromeSigninURL[] = "chrome://chrome-signin/";
const char kChromeUIChromeURLsHost[] = "chrome-urls";
const char kChromeUIChromeURLsURL[] = "chrome://chrome-urls/";
const char kChromeUIComponentsHost[] = "components";
const char kChromeUIComponentsUrl[] = "chrome://components";
const char kChromeUIConflictsHost[] = "conflicts";
const char kChromeUIConstrainedHTMLTestURL[] = "chrome://constrained-test/";
const char kChromeUIContentSettingsURL[] = "chrome://settings/content";
const char kChromeUICookieSettingsURL[] = "chrome://settings/cookies";
const char kChromeUICrashHost[] = "crash";
const char kChromeUICrashesHost[] = "crashes";
const char kChromeUICrashesUrl[] = "chrome://crashes";
const char kChromeUICreditsHost[] = "credits";
const char kChromeUICreditsURL[] = "chrome://credits/";
const char kChromeUIDefaultHost[] = "version";
const char kChromeUIDelayedHangUIHost[] = "delayeduithreadhang";
const char kChromeUIDevToolsBlankPath[] = "blank";
const char kChromeUIDevToolsBundledPath[] = "bundled";
const char kChromeUIDevToolsCustomPath[] = "custom";
const char kChromeUIDevToolsHost[] = "devtools";
const char kChromeUIDevToolsRemotePath[] = "remote";
const char kChromeUIDevToolsURL[] =
    "devtools://devtools/bundled/inspector.html";
const char kChromeUIDeviceLogHost[] = "device-log";
const char kChromeUIDeviceLogUrl[] = "chrome://device-log";
const char kChromeUIDevUiLoaderURL[] = "chrome://dev-ui-loader/";
const char kChromeUIDiceWebSigninInterceptHost[] =
    "signin-dice-web-intercept.top-chrome";
const char kChromeUIDiceWebSigninInterceptURL[] =
    "chrome://signin-dice-web-intercept.top-chrome/";
const char kChromeUIDiceWebSigninInterceptChromeSigninURL[] =
    "chrome://signin-dice-web-intercept.top-chrome/chrome-signin";
const char kChromeUIDiceWebSigninInterceptChromeSigninSubPage[] =
    "chrome-signin";
const char kChromeUIDownloadInternalsHost[] = "download-internals";
const char kChromeUIDownloadsHost[] = "downloads";
const char kChromeUIDownloadsURL[] = "chrome://downloads/";
const char kChromeUIDriveInternalsHost[] = "drive-internals";
const char kChromeUIDriveInternalsUrl[] = "chrome://drive-internals";
const char kChromeUIEDUCoexistenceLoginURLV2[] =
    "chrome://chrome-signin/edu-coexistence";
const char kChromeUIAccessCodeCastHost[] = "access-code-cast";
const char kChromeUIAccessCodeCastURL[] = "chrome://access-code-cast/";
const char kChromeUIDataSharingInternalsHost[] = "data-sharing-internals";
const char kChromeUIExtensionIconHost[] = "extension-icon";
const char kChromeUIExtensionIconURL[] = "chrome://extension-icon/";
const char kChromeUIExtensionsHost[] = "extensions";
const char kChromeUIExtensionsInternalsHost[] = "extensions-internals";
const char kChromeUIExtensionsInternalsURL[] = "chrome://extensions-internals";
const char kChromeUIExtensionsURL[] = "chrome://extensions/";
const char kChromeUIFamilyLinkUserInternalsHost[] =
    "family-link-user-internals";
const char kChromeUIFaviconHost[] = "favicon";
const char kChromeUIFaviconURL[] = "chrome://favicon/";
const char kChromeUIFavicon2Host[] = "favicon2";
const char kChromeUIFeedbackHost[] = "feedback";
const char kChromeUIFeedbackURL[] = "chrome://feedback/";
const char kChromeUIFileiconURL[] = "chrome://fileicon/";
const char kChromeUIFlagsHost[] = "flags";
const char kChromeUIFlagsURL[] = "chrome://flags/";
const char kChromeUIGCMInternalsHost[] = "gcm-internals";
const char kChromeUIHangUIHost[] = "uithreadhang";
const char kChromeUIHelpHost[] = "help";
const char kChromeUIHelpURL[] = "chrome://help/";
const char kChromeUIHistoryHost[] = "history";
const char kChromeUIHistorySyncedTabs[] = "/syncedTabs";
const char kChromeUIHistoryURL[] = "chrome://history/";
const char kChromeUIHumanPresenceInternalsHost[] = "hps-internals";
const char kChromeUIHumanPresenceInternalsURL[] = "chrome://hps-internals/";
const char kChromeUIIdentityInternalsHost[] = "identity-internals";
const char kChromeUIImageHost[] = "image";
const char kChromeUIImageURL[] = "chrome://image/";
const char kChromeUIInspectHost[] = "inspect";
const char kChromeUIInspectURL[] = "chrome://inspect/";
const char kChromeUIInternalsHost[] = "internals";
const char kChromeUIInterstitialHost[] = "interstitials";
const char kChromeUIInterstitialURL[] = "chrome://interstitials/";
const char kChromeUIKillHost[] = "kill";
const char kChromeUILauncherInternalsHost[] = "launcher-internals";
const char kChromeUILauncherInternalsURL[] = "chrome://launcher-internals";
const char kChromeUILensSearchBubbleHost[] = "lens-search-bubble";
const char kChromeUILensSearchBubbleURL[] = "chrome://lens-search-bubble/";
const char kChromeUISensorInfoHost[] = "sensor-info";
const char kChromeUISensorInfoURL[] = "chrome://sensor-info/";
const char kChromeUILocalStateHost[] = "local-state";
const char kChromeUILocationInternalsHost[] = "location-internals";
const char kChromeUIManagementHost[] = "management";
const char kChromeUIManagementURL[] = "chrome://management";
const char kChromeUIMediaEngagementHost[] = "media-engagement";
const char kChromeUIMediaRouterInternalsHost[] = "media-router-internals";
const char kChromeUIMemoryInternalsHost[] = "memory-internals";
const char kChromeUIMetricsInternalsHost[] = "metrics-internals";
const char kChromeUINTPTilesInternalsHost[] = "ntp-tiles-internals";
const char kChromeUINaClHost[] = "nacl";
const char kChromeUINetExportHost[] = "net-export";
const char kChromeUINetExportURL[] = "chrome://net-export";
const char kChromeUINetInternalsHost[] = "net-internals";
const char kChromeUINetInternalsURL[] = "chrome://net-internals/";
const char kChromeUINewTabHost[] = "newtab";
const char kChromeUINewTabPageHost[] = "new-tab-page";
const char kChromeUINewTabPageURL[] = "chrome://new-tab-page/";
const char kChromeUINewTabPageThirdPartyHost[] = "new-tab-page-third-party";
const char kChromeUINewTabPageThirdPartyURL[] =
    "chrome://new-tab-page-third-party/";
const char kChromeUINewTabURL[] = "chrome://newtab/";
const char kChromeUIProfileInternalsHost[] = "profile-internals";
const char kChromeUIOmniboxHost[] = "omnibox";
const char kChromeUIOmniboxURL[] = "chrome://omnibox/";
#if !BUILDFLAG(IS_ANDROID)
const char kChromeUIOnDeviceInternalsHost[] = "on-device-internals";
const char kChromeUISearchEngineChoiceHost[] = "search-engine-choice";
const char kChromeUISearchEngineChoiceURL[] = "chrome://search-engine-choice";
#endif
const char kChromeUIPasswordManagerInternalsHost[] =
    "password-manager-internals";
const char kChromeUIPasswordManagerURL[] = "chrome://password-manager";
const char kChromeUIPasswordManagerCheckupURL[] =
    "chrome://password-manager/checkup?start=true";
const char kChromeUIPasswordManagerSettingsURL[] =
    "chrome://password-manager/settings";
const char kChromeUIPerformanceSidePanelHost[] =
    "performance-side-panel.top-chrome";
const char kChromeUIPerformanceSidePanelURL[] =
    "chrome://performance-side-panel.top-chrome";
const char kChromeUIPolicyHost[] = "policy";
const char kChromeUIPolicyURL[] = "chrome://policy/";
const char kChromeUIPolicyTestURL[] = "chrome://policy/test";
const char kChromeUIPredictorsHost[] = "predictors";
const char kChromeUIPrefsInternalsHost[] = "prefs-internals";
const char kChromeUIPrefsInternalsURL[] = "chrome://prefs-internals";
const char kChromeUIPrintURL[] = "chrome://print/";
const char kChromeUIPrivacySandboxInternalsHost[] = "privacy-sandbox-internals";
const char kChromeUIPrivacySandboxInternalsURL[] =
    "chrome://privacy-sandbox-internals";
const char kChromeUIPrivacySandboxDialogHost[] = "privacy-sandbox-dialog";
const char kChromeUIPrivacySandboxDialogURL[] =
    "chrome://privacy-sandbox-dialog";
const char kChromeUIPrivacySandboxDialogCombinedPath[] = "combined";
const char kChromeUIPrivacySandboxDialogNoticePath[] = "notice";
const char kChromeUIPrivacySandboxDialogNoticeRestrictedPath[] = "restricted";
const char kChromeUIPrivacySandboxFledgeURL[] =
    "chrome://settings/adPrivacy/sites";
const char kChromeUIPrivacySandboxTopicsURL[] =
    "chrome://settings/adPrivacy/interests";
const char kChromeUIPrivacySandboxManageTopicsLearnMoreURL[] =
    "https://support.google.com/chrome?p=ad_privacy";
const char kChromeUIQuitHost[] = "quit";
const char kChromeUIQuitURL[] = "chrome://quit/";
const char kChromeUIQuotaInternalsHost[] = "quota-internals";
const char kChromeUIResetPasswordHost[] = "reset-password";
const char kChromeUIResetPasswordURL[] = "chrome://reset-password/";
const char kChromeUIRestartHost[] = "restart";
const char kChromeUIRestartURL[] = "chrome://restart/";
const char kChromeUISafetyPixelbookURL[] = "https://g.co/Pixelbook/legal";
const char kChromeUISafetyPixelSlateURL[] = "https://g.co/PixelSlate/legal";
const char kChromeUISegmentationInternalsHost[] = "segmentation-internals";
#if BUILDFLAG(ENABLE_SESSION_SERVICE)
const char kChromeUISessionServiceInternalsPath[] = "session-service";
#endif
const char kChromeUISettingsHost[] = "settings";
const char kChromeUISettingsURL[] = "chrome://settings/";
const char kChromeUISignInInternalsHost[] = "signin-internals";
const char kChromeUISignInInternalsUrl[] = "chrome://signin-internals";
const char kChromeUISigninEmailConfirmationHost[] = "signin-email-confirmation";
const char kChromeUISigninEmailConfirmationURL[] =
    "chrome://signin-email-confirmation";
const char kChromeUISigninErrorHost[] = "signin-error";
const char kChromeUISigninErrorURL[] = "chrome://signin-error/";
const char kChromeUISigninReauthHost[] = "signin-reauth";
const char kChromeUISigninReauthURL[] = "chrome://signin-reauth/";
const char kChromeUISiteDataDeprecatedPath[] = "/siteData";
const char kChromeUISiteEngagementHost[] = "site-engagement";
const char kChromeUISupervisedUserPassphrasePageHost[] =
    "managed-user-passphrase";
const char kChromeUISupportToolHost[] = "support-tool";
const char kChromeUISupportToolURL[] = "chrome://support-tool";
const char kChromeUISyncConfirmationHost[] = "sync-confirmation";
const char kChromeUISyncConfirmationLoadingPath[] = "loading";
const char kChromeUISyncConfirmationURL[] = "chrome://sync-confirmation/";
const char kChromeUISyncFileSystemInternalsHost[] = "syncfs-internals";
const char kChromeUISyncHost[] = "sync";
const char kChromeUISyncInternalsHost[] = "sync-internals";
const char kChromeUISyncInternalsUrl[] = "chrome://sync-internals";
const char kChromeUISystemInfoHost[] = "system";
const char kChromeUITermsHost[] = "terms";
const char kChromeUITermsURL[] = "chrome://terms/";
const char kChromeUIThemeHost[] = "theme";
const char kChromeUIThemeURL[] = "chrome://theme/";
const char kChromeUITranslateInternalsHost[] = "translate-internals";
const char kChromeUITopChromeDomain[] = "top-chrome";
const char kChromeUIUntrustedComposeHost[] = "compose";
const char kChromeUIUntrustedComposeUrl[] = "chrome-untrusted://compose/";
#if !BUILDFLAG(IS_ANDROID)
const char kChromeUIUntrustedHatsHost[] = "hats";
const char kChromeUIUntrustedHatsURL[] = "chrome-untrusted://hats/";
#endif  // !BUILDFLAG(IS_ANDROID)
const char kChromeUIUntrustedImageEditorURL[] =
    "chrome-untrusted://image-editor/";
const char kChromeUIUntrustedPrintURL[] = "chrome-untrusted://print/";
const char kChromeUIUntrustedThemeURL[] = "chrome-untrusted://theme/";
const char kChromeUIUntrustedDataSharingHost[] = "data-sharing";
const char kChromeUIUntrustedDataSharingURL[] =
    "chrome-untrusted://data-sharing/";
const char kChromeUIUsbInternalsHost[] = "usb-internals";
const char kChromeUIUserActionsHost[] = "user-actions";
const char kChromeUIVersionHost[] = "version";
const char kChromeUIVersionURL[] = "chrome://version/";
const char kChromeUIWelcomeHost[] = "welcome";
const char kChromeUIWelcomeURL[] = "chrome://welcome/";

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)
const char kChromeUIWhatsNewHost[] = "whats-new";
const char kChromeUIWhatsNewURL[] = "chrome://whats-new/";
#endif  // BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)

const char kChromeUIWebuiGalleryHost[] = "webui-gallery";

#if BUILDFLAG(IS_WIN)
// TODO(crbug.com/40647483): Remove when issue is resolved.
const char kChromeUIWelcomeWin10Host[] = "welcome-win10";
#endif  // BUILDFLAG(IS_WIN)

#if BUILDFLAG(IS_ANDROID)
const char kChromeUIInternalsQueryTilesPath[] = "query-tiles";
const char kChromeUIJavaCrashURL[] = "chrome://java-crash/";
const char kChromeUINativeBookmarksURL[] = "chrome-native://bookmarks/";
const char kChromeUINativeExploreURL[] = "chrome-native://explore";
const char kChromeUINativeNewTabURL[] = "chrome-native://newtab/";
const char kChromeUIOfflineInternalsHost[] = "offline-internals";
const char kChromeUISnippetsInternalsHost[] = "snippets-internals";
const char kChromeUIWebApksHost[] = "webapks";
#else
const char kChromeUIAppServiceInternalsHost[] = "app-service-internals";
const char kChromeUIAppServiceInternalsURL[] = "chrome://app-service-internals";
const char kChromeUINearbyInternalsHost[] = "nearby-internals";
const char kChromeUINearbyInternalsURL[] = "chrome://nearby-internals";
const char kChromeUIBookmarksSidePanelHost[] =
    "bookmarks-side-panel.top-chrome";
const char kChromeUIBookmarksSidePanelURL[] =
    "chrome://bookmarks-side-panel.top-chrome/";
const char kChromeUIUntrustedCompanionSidePanelHost[] =
    "companion-side-panel.top-chrome";
const char kChromeUIUntrustedCompanionSidePanelURL[] =
    "chrome-untrusted://companion-side-panel.top-chrome/";
const char kChromeUICustomizeChromeSidePanelHost[] =
    "customize-chrome-side-panel.top-chrome";
const char kChromeUICustomizeChromeSidePanelURL[] =
    "chrome://customize-chrome-side-panel.top-chrome";
const char kChromeUIHistoryClustersSidePanelHost[] =
    "history-clusters-side-panel.top-chrome";
const char kChromeUIHistoryClustersSidePanelURL[] =
    "chrome://history-clusters-side-panel.top-chrome/";
const char kChromeUILensHost[] = "lens";
const char kChromeUILensUntrustedURL[] = "chrome-untrusted://lens/";
const char kChromeUILensUntrustedSidePanelURL[] =
    "chrome-untrusted://lens/side_panel/side_panel.html";
const char kChromeUIUntrustedReadAnythingSidePanelHost[] =
    "read-anything-side-panel.top-chrome";
const char kChromeUIUntrustedReadAnythingSidePanelURL[] =
    "chrome-untrusted://read-anything-side-panel.top-chrome/";
const char kChromeUIReadLaterHost[] = "read-later.top-chrome";
const char kChromeUIReadLaterURL[] = "chrome://read-later.top-chrome/";
const char kChromeUIUntrustedFeedURL[] = "chrome-untrusted://feed/";
const char kChromeUIOmniboxPopupHost[] = "omnibox-popup.top-chrome";
const char kChromeUIOmniboxPopupURL[] = "chrome://omnibox-popup.top-chrome/";
const char kChromeUISuggestInternalsHost[] = "suggest-internals";
const char kChromeUISuggestInternalsURL[] = "chrome://suggest-internals/";
const char kChromeUIWebAppInternalsHost[] = "web-app-internals";
const char kChromeUIWebAppInternalsURL[] = "chrome://web-app-internals";
const char kChromeUIWebUITestHost[] = "webui-test";
const char kChromeUIUntrustedWebUITestURL[] = "chrome-untrusted://webui-test/";
#endif

#if BUILDFLAG(IS_CHROMEOS_ASH)
// Keep alphabetized.
const char kChromeUIAccountManagerErrorHost[] = "account-manager-error";
const char kChromeUIAccountManagerErrorURL[] = "chrome://account-manager-error";
const char kChromeUIAccountMigrationWelcomeHost[] = "account-migration-welcome";
const char kChromeUIAccountMigrationWelcomeURL[] =
    "chrome://account-migration-welcome";
const char kChromeUIAddSupervisionHost[] = "add-supervision";
const char kChromeUIAddSupervisionURL[] = "chrome://add-supervision/";
const char kChromeUIAppInstallDialogHost[] = "app-install-dialog";
const char kChromeUIAppInstallDialogURL[] = "chrome://app-install-dialog/";
const char kChromeUIArcOverviewTracingHost[] = "arc-overview-tracing";
const char kChromeUIArcOverviewTracingURL[] = "chrome://arc-overview-tracing/";
const char kChromeUIArcPowerControlHost[] = "arc-power-control";
const char kChromeUIArcPowerControlURL[] = "chrome://arc-power-control/";
const char kChromeUIAssistantOptInHost[] = "assistant-optin";
const char kChromeUIAssistantOptInURL[] = "chrome://assistant-optin/";
const char kChromeUIAudioHost[] = "audio";
const char kChromeUIAudioURL[] = "chrome://audio/";
const char kChromeUIBluetoothPairingHost[] = "bluetooth-pairing";
const char kChromeUIBluetoothPairingURL[] = "chrome://bluetooth-pairing/";
const char kChromeUIBorealisInstallerHost[] = "borealis-installer";
const char kChromeUIBorealisInstallerUrl[] = "chrome://borealis-installer";
const char kChromeUICertificateManagerDialogURL[] =
    "chrome://certificate-manager/";
const char kChromeUICertificateManagerHost[] = "certificate-manager";
const char kChromeUICloudUploadHost[] = "cloud-upload";
const char kChromeUICloudUploadURL[] = "chrome://cloud-upload/";
const char kChromeUIConfirmPasswordChangeHost[] = "confirm-password-change";
const char kChromeUIConfirmPasswordChangeUrl[] =
    "chrome://confirm-password-change";
const char kChromeUIConnectivityDiagnosticsAppURL[] =
    "chrome://connectivity-diagnostics";
const char kChromeUICrostiniInstallerHost[] = "crostini-installer";
const char kChromeUICrostiniInstallerUrl[] = "chrome://crostini-installer";
const char kChromeUICrostiniUpgraderHost[] = "crostini-upgrader";
const char kChromeUICrostiniUpgraderUrl[] = "chrome://crostini-upgrader";
const char kChromeUICryptohomeHost[] = "cryptohome";
const char kChromeUICryptohomeURL[] = "chrome://cryptohome";
const char kChromeUIDeviceEmulatorHost[] = "device-emulator";
const char kChromeUIDeviceEmulatorURL[] = "chrome://device-emulator";
const char kChromeUIDiagnosticsAppURL[] = "chrome://diagnostics";
const char kChromeUIEmojiPickerURL[] = "chrome://emoji-picker/";
const char kChromeUIEmojiPickerHost[] = "emoji-picker";
const char kChromeUIEnterpriseReportingHost[] = "enterprise-reporting";
const char kChromeUIEnterpriseReportingURL[] = "chrome://enterprise-reporting";
const char kChromeUIExtendedUpdatesDialogHost[] = "extended-updates-dialog";
const char kChromeUIExtendedUpdatesDialogURL[] =
    "chrome://extended-updates-dialog";
const char kChromeUIFirmwareUpdaterAppURL[] = "chrome://accessory-update";
const char kChromeUIHealthdInternalsHost[] = "healthd-internals";
const char kChromeUIHealthdInternalsURL[] = "chrome://healthd-internals";
const char kChromeUIInternetConfigDialogURL[] =
    "chrome://internet-config-dialog/";
const char kChromeUIInternetDetailDialogURL[] =
    "chrome://internet-detail-dialog/";
const char kChromeUIInternetConfigDialogHost[] = "internet-config-dialog";
const char kChromeUIInternetDetailDialogHost[] = "internet-detail-dialog";
const char kChromeUIBorealisCreditsHost[] = "borealis-credits";
const char kChromeUIBorealisCreditsURL[] = "chrome://borealis-credits/";
const char kChromeUICrostiniCreditsHost[] = "crostini-credits";
const char kChromeUICrostiniCreditsURL[] = "chrome://crostini-credits/";
const char kChromeUILockScreenNetworkHost[] = "lock-network";
const char kChromeUILockScreenNetworkURL[] = "chrome://lock-network";
const char kChromeUILockScreenStartReauthHost[] = "lock-reauth";
const char kChromeUILockScreenStartReauthURL[] = "chrome://lock-reauth";
const char kChromeUIManageMirrorSyncHost[] = "manage-mirrorsync";
const char kChromeUIManageMirrorSyncURL[] = "chrome://manage-mirrorsync";
const char kChromeUIMobileSetupHost[] = "mobilesetup";
const char kChromeUIMobileSetupURL[] = "chrome://mobilesetup/";
const char kChromeUIMultiDeviceInternalsHost[] = "multidevice-internals";
const char kChromeUIMultiDeviceInternalsURL[] =
    "chrome://multidevice-internals";
const char kChromeUIMultiDeviceSetupHost[] = "multidevice-setup";
const char kChromeUIMultiDeviceSetupUrl[] = "chrome://multidevice-setup";
const char kChromeUINetworkHost[] = "network";
const char kChromeUINetworkUrl[] = "chrome://network";
const char kChromeUINotificationTesterHost[] = "notification-tester";
const char kChromeUINotificationTesterURL[] = "chrome://notification-tester";
const char kChromeUIOSCreditsHost[] = "os-credits";
const char kChromeUIOSCreditsURL[] = "chrome://os-credits/";
const char kChromeUIOfficeFallbackHost[] = "office-fallback";
const char kChromeUIOfficeFallbackURL[] = "chrome://office-fallback/";
const char kChromeUIOobeHost[] = "oobe";
const char kChromeUIOobeURL[] = "chrome://oobe/";
const char kChromeUIParentAccessHost[] = "parent-access";
const char kChromeUIParentAccessURL[] = "chrome://parent-access/";
const char kChromeUIPasswordChangeHost[] = "password-change";
const char kChromeUIPasswordChangeUrl[] = "chrome://password-change";
const char kChromeUIPrintManagementUrl[] = "chrome://print-management";
const char kChromeUIPowerHost[] = "power";
const char kChromeUIPowerUrl[] = "chrome://power";
const char kChromeUIRemoteManagementCurtainHost[] = "security-curtain";
const char kChromeUISanitizeAppURL[] = "chrome://sanitize";
const char kChromeUIScanningAppURL[] = "chrome://scanning";
const char kChromeUISetTimeHost[] = "set-time";
const char kChromeUISetTimeURL[] = "chrome://set-time/";
const char kChromeUISlowHost[] = "slow";
const char kChromeUISlowTraceHost[] = "slow_trace";
const char kChromeUISlowURL[] = "chrome://slow/";
const char kChromeUISmbShareHost[] = "smb-share-dialog";
const char kChromeUISmbShareURL[] = "chrome://smb-share-dialog/";
const char kChromeUISmbCredentialsHost[] = "smb-credentials-dialog";
const char kChromeUISmbCredentialsURL[] = "chrome://smb-credentials-dialog/";
const char kChromeUISysInternalsHost[] = "sys-internals";
const char kChromeUISysInternalsUrl[] = "chrome://sys-internals";
const char kChromeUIUntrustedCroshHost[] = "crosh";
const char kChromeUIUntrustedCroshURL[] = "chrome-untrusted://crosh/";
const char kChromeUIUntrustedTerminalHost[] = "terminal";
const char kChromeUIUntrustedTerminalURL[] = "chrome-untrusted://terminal/";
const char kChromeUIUserImageHost[] = "userimage";
const char kChromeUIUserImageURL[] = "chrome://userimage/";
const char kChromeUIVcTrayTesterHost[] = "vc-tray-tester";
const char kChromeUIVcTrayTesterURL[] = "chrome://vc-tray-tester";
const char kChromeUIVmHost[] = "vm";
const char kChromeUIVmUrl[] = "chrome://vm";

const char kChromeUIUrgentPasswordExpiryNotificationHost[] =
    "urgent-password-expiry-notification";
const char kChromeUIUrgentPasswordExpiryNotificationUrl[] =
    "chrome://urgent-password-expiry-notification/";

// Keep alphabetized.

bool IsSystemWebUIHost(std::string_view host) {
  // Compares host instead of full URL for performance (the strings are
  // shorter).
  static const char* const kHosts[] = {
      kChromeUIAccountManagerErrorHost,
      kChromeUIAccountMigrationWelcomeHost,
      kChromeUIAddSupervisionHost,
      kChromeUIAppInstallDialogHost,
      kChromeUIAssistantOptInHost,
      kChromeUIBluetoothPairingHost,
      kChromeUIBorealisCreditsHost,
      kChromeUIBorealisInstallerHost,
      kChromeUICertificateManagerHost,
      kChromeUICloudUploadHost,
      kChromeUICrostiniCreditsHost,
      kChromeUICrostiniInstallerHost,
      kChromeUICryptohomeHost,
      kChromeUIDeviceEmulatorHost,
      kChromeUIEmojiPickerHost,
      kChromeUIExtendedUpdatesDialogHost,
      kChromeUIInternetConfigDialogHost,
      kChromeUIInternetDetailDialogHost,
      kChromeUILockScreenNetworkHost,
      kChromeUILockScreenStartReauthHost,
      kChromeUIMobileSetupHost,
      kChromeUIMultiDeviceSetupHost,
      kChromeUINetworkHost,
      kChromeUINotificationTesterHost,
      kChromeUIOobeHost,
      kChromeUIOSCreditsHost,
      kChromeUIOSSettingsHost,
      kChromeUIPasswordChangeHost,
      kChromeUIPowerHost,
      kChromeUISetTimeHost,
      kChromeUISmbCredentialsHost,
      kChromeUISmbShareHost,
      kChromeUIVcTrayTesterHost,
  };
  for (const char* h : kHosts) {
    if (host == h) {
      return true;
    }
  }
  return false;
}
#endif  // BUILDFLAG(IS_CHROMEOS_ASH)

#if BUILDFLAG(IS_CHROMEOS)
const char kChromeUIAppDisabledHost[] = "app-disabled";
const char kChromeUIAppDisabledURL[] = "chrome://app-disabled";
const char kChromeUIDlpInternalsHost[] = "dlp-internals";
const char kChromeUIDlpInternalsURL[] = "chrome://dlp-internals";
const char kChromeUIGpuURL[] = "chrome://gpu";
const char kChromeUIHistogramsURL[] = "chrome://histograms";
const char kChromeUIKerberosInBrowserHost[] = "kerberos-in-browser";
const char kChromeUIKerberosInBrowserURL[] = "chrome://kerberos-in-browser";
const char kChromeUILocationInternalsURL[] = "chrome://location-internals";
const char kChromeUIOsFlagsAppURL[] = "chrome://flags/";
const char kChromeUIOSSettingsHost[] = "os-settings";
const char kChromeUIOsUrlAppURL[] = "chrome://internal/";
const char kChromeUIOSSettingsURL[] = "chrome://os-settings/";
const char kChromeUISystemURL[] = "chrome://system";
const char kOsUIComponentsURL[] = "os://components";
const char kOsUIConnectivityDiagnosticsAppURL[] =
    "os://connectivity-diagnostics";
const char kOsUIDeviceLogURL[] = "os://device-log";
const char kOsUIDiagnosticsAppURL[] = "os://diagnostics";
const char kOsUIFirmwareUpdaterAppURL[] = "os://accessory-update";
const char kOsUIFlagsURL[] = "os://flags";
const char kOsUIHelpAppURL[] = "os://help-app";
const char kOsUIPrintManagementAppURL[] = "os://print-management";
const char kOsUIScanningAppURL[] = "os://scanning";
const char kOsUIShortcutCustomizationAppURL[] = "os://shortcut-customization";
const char kOsUISystemURL[] = "os://system";
const char kOsUIVersionURL[] = "os://version";
#endif

#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)
const char kChromeUIWebUIJsErrorHost[] = "webuijserror";
const char kChromeUIWebUIJsErrorURL[] = "chrome://webuijserror/";
#endif

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX) || \
    BUILDFLAG(IS_CHROMEOS_ASH)
const char kChromeUIConnectorsInternalsHost[] = "connectors-internals";
#endif

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX) || \
    BUILDFLAG(IS_CHROMEOS)
const char kChromeUIDiscardsHost[] = "discards";
const char kChromeUIDiscardsURL[] = "chrome://discards/";
#endif

#if !BUILDFLAG(IS_ANDROID)
const char kChromeUINearbyShareHost[] = "nearby";
const char kChromeUINearbyShareURL[] = "chrome://nearby/";
#endif  // !BUILDFLAG(IS_ANDROID)

#if BUILDFLAG(IS_POSIX) && !BUILDFLAG(IS_MAC) && !BUILDFLAG(IS_ANDROID)
const char kChromeUILinuxProxyConfigHost[] = "linux-proxy-config";
#endif

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || \
    BUILDFLAG(IS_ANDROID)
const char kChromeUISandboxHost[] = "sandbox";
#endif

// TODO(crbug.com/40118868): Revisit the macro expression once build flag switch
// of lacros-chrome is complete.
#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || \
    (BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS_LACROS))
const char kChromeUIBrowserSwitchHost[] = "browser-switch";
const char kChromeUIBrowserSwitchURL[] = "chrome://browser-switch/";
const char kChromeUIIntroHost[] = "intro";
const char kChromeUIIntroURL[] = "chrome://intro";
const char kChromeUIIntroDefaultBrowserSubPage[] = "default-browser";
const char kChromeUIIntroDefaultBrowserURL[] = "chrome://intro/default-browser";
const char kChromeUIManagedUserProfileNoticeHost[] =
    "managed-user-profile-notice";
const char kChromeUIManagedUserProfileNoticeUrl[] =
    "chrome://managed-user-profile-notice/";
const char kChromeUIProfileCustomizationHost[] = "profile-customization";
const char kChromeUIProfileCustomizationURL[] =
    "chrome://profile-customization";
const char kChromeUIProfilePickerHost[] = "profile-picker";
const char kChromeUIProfilePickerUrl[] = "chrome://profile-picker/";
const char kChromeUIProfilePickerStartupQuery[] = "startup";
#endif

#if ((BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)) && \
     defined(TOOLKIT_VIEWS)) ||                         \
    defined(USE_AURA)
const char kChromeUITabModalConfirmDialogHost[] = "tab-modal-confirm-dialog";
#endif

#if BUILDFLAG(ENABLE_PRINT_PREVIEW)
const char kChromeUIPrintHost[] = "print";
#endif

#if BUILDFLAG(ENABLE_WEBUI_TAB_STRIP)
const char kChromeUITabStripHost[] = "tab-strip.top-chrome";
const char kChromeUITabStripURL[] = "chrome://tab-strip.top-chrome";
#endif

#if !BUILDFLAG(IS_ANDROID)
const char kChromeUITabSearchHost[] = "tab-search.top-chrome";
const char kChromeUITabSearchURL[] = "chrome://tab-search.top-chrome/";
#endif

const char kChromeUIWebRtcLogsHost[] = "webrtc-logs";

// Settings sub pages.

// NOTE: Add sub page paths to |kChromeSettingsSubPages| in
// chrome_autocomplete_provider_client.cc to be listed by the built-in
// AutocompleteProvider.

const char kAccessibilitySubPage[] = "accessibility";
const char kAdPrivacySubPage[] = "adPrivacy";
const char kPrivacySandboxMeasurementSubpage[] = "adPrivacy/measurement";
const char kAddressesSubPage[] = "addresses";
const char kAppearanceSubPage[] = "appearance";
const char kAutofillSubPage[] = "autofill";
const char kClearBrowserDataSubPage[] = "clearBrowserData";
const char kContentSettingsSubPage[] = "content";
const char kAllSitesSettingsSubpage[] = "content/all";
const char kFileSystemSubpage[] = "content/filesystem";
const char kFileSystemSettingsSubpage[] = "content/filesystem/siteDetails";
const char kOnDeviceSiteDataSubpage[] = "content/siteData";
const char kOfferWritingHelpSubpage[] = "content/offerWritingHelp";
const char kCookieSettingsSubPage[] = "cookies";
const char kExperimentalAISettingsSubPage[] = "ai";
const char kDefaultBrowserSubPage[] = "defaultBrowser";
const char kDownloadsSubPage[] = "downloads";
const char kHandlerSettingsSubPage[] = "handlers";
const char kImportDataSubPage[] = "importData";
const char kLanguagesSubPage[] = "languages/details";
const char kLanguageOptionsSubPage[] = "languages";
const char kOnStartupSubPage[] = "onStartup";
const char kPasskeysSubPage[] = "passkeys";
const char kPasswordCheckSubPage[] = "passwords/check?start=true";
const char kPasswordManagerSubPage[] = "passwords";
const char kPaymentsSubPage[] = "payments";
const char kPerformanceSubPage[] = "performance";
const char kPrintingSettingsSubPage[] = "printing";
const char kPrivacyGuideSubPage[] = "privacy/guide";
const char kPrivacySubPage[] = "privacy";
const char kResetSubPage[] = "reset";
const char kResetProfileSettingsSubPage[] = "resetProfileSettings";
const char kSafeBrowsingEnhancedProtectionSubPage[] = "security?q=enhanced";
const char kSafetyCheckSubPage[] = "safetyCheck";
const char kSafetyHubSubPage[] = "safetyCheck";
const char kSearchSubPage[] = "search";
const char kSearchEnginesSubPage[] = "searchEngines";
const char kSignOutSubPage[] = "signOut";
const char kSiteDetailsSubpage[] = "content/siteDetails";
const char kSyncSetupSubPage[] = "syncSetup";
const char kTriggeredResetProfileSettingsSubPage[] =
    "triggeredResetProfileSettings";
const char kManageProfileSubPage[] = "manageProfile";
const char kPeopleSubPage[] = "people";

#if !BUILDFLAG(IS_ANDROID)
const char kAdPrivacySubPagePath[] = "/adPrivacy";
const char kTrackingProtectionSubPagePath[] = "/trackingProtection";
const char kCookiesSubPagePath[] = "/cookies";
#endif

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)
const char kChromeUIWebAppSettingsURL[] = "chrome://app-settings/";
const char kChromeUIWebAppSettingsHost[] = "app-settings";
#endif

#if BUILDFLAG(IS_WIN)
const char kCleanupSubPage[] = "cleanup";
#endif  // BUILDFLAG(IS_WIN)

#if !BUILDFLAG(IS_ANDROID) && BUILDFLAG(GOOGLE_CHROME_BRANDING)
const char kChromeUICastFeedbackHost[] = "cast-feedback";
#endif

#if BUILDFLAG(ENABLE_LENS_DESKTOP_GOOGLE_BRANDED_FEATURES)
const char kChromeUILensURL[] = "chrome://lens/";
#endif

// Extension sub pages.
const char kExtensionConfigureCommandsSubPage[] = "configureCommands";

// Add hosts here to be included in chrome://chrome-urls (about:about).
// These hosts will also be suggested by BuiltinProvider.
const char* const kChromeHostURLs[] = {
    kChromeUIAboutHost,
    kChromeUIAccessibilityHost,
#if !BUILDFLAG(IS_ANDROID)
    kChromeUIAppServiceInternalsHost,
#endif
    kChromeUIAutofillInternalsHost,
    kChromeUIBluetoothInternalsHost,
    kChromeUIBrowsingTopicsInternalsHost,
    kChromeUIChromeURLsHost,
    kChromeUIComponentsHost,
    commerce::kChromeUICommerceInternalsHost,
    kChromeUICrashesHost,
    kChromeUICreditsHost,
#if BUILDFLAG(IS_CHROMEOS_ASH) && !defined(OFFICIAL_BUILD)
    kChromeUIDeviceEmulatorHost,
#endif
    kChromeUIDeviceLogHost,
    kChromeUIDownloadInternalsHost,
    kChromeUIFamilyLinkUserInternalsHost,
    kChromeUIFlagsHost,
    kChromeUIGCMInternalsHost,
    kChromeUIHistoryHost,
    history_clusters_internals::kChromeUIHistoryClustersInternalsHost,
#if BUILDFLAG(IS_CHROMEOS_ASH)
    kChromeUIHumanPresenceInternalsHost,
#endif
    kChromeUIInterstitialHost,
    kChromeUILocalStateHost,
#if !BUILDFLAG(IS_ANDROID)
    kChromeUIManagementHost,
#endif
    kChromeUIMediaEngagementHost,
    kChromeUIMetricsInternalsHost,
    kChromeUINetExportHost,
    kChromeUINetInternalsHost,
    kChromeUINewTabHost,
    kChromeUIOmniboxHost,
#if !BUILDFLAG(IS_ANDROID)
    kChromeUIOnDeviceInternalsHost,
#endif
    optimization_guide_internals::kChromeUIOptimizationGuideInternalsHost,
    kChromeUIPasswordManagerInternalsHost,
    password_manager::kChromeUIPasswordManagerHost,
    kChromeUIPolicyHost,
    kChromeUIPredictorsHost,
    kChromeUIPrefsInternalsHost,
    kChromeUIProfileInternalsHost,
    kChromeUIQuotaInternalsHost,
    kChromeUISignInInternalsHost,
    kChromeUISiteEngagementHost,
#if !BUILDFLAG(IS_ANDROID)
    kChromeUISuggestInternalsHost,
#endif
    kChromeUINTPTilesInternalsHost,
    safe_browsing::kChromeUISafeBrowsingHost,
    kChromeUISyncInternalsHost,
#if !BUILDFLAG(IS_ANDROID)
    kChromeUITabSearchHost,
    kChromeUITermsHost,
#endif
    kChromeUITranslateInternalsHost,
    kChromeUIUsbInternalsHost,
    kChromeUIUserActionsHost,
    kChromeUIVersionHost,
#if !BUILDFLAG(IS_ANDROID)
    kChromeUIWebAppInternalsHost,
#endif
    content::kChromeUIPrivateAggregationInternalsHost,
    content::kChromeUIAttributionInternalsHost,
    content::kChromeUIBlobInternalsHost,
    content::kChromeUIDinoHost,
    content::kChromeUIGpuHost,
    content::kChromeUIHistogramHost,
    content::kChromeUIIndexedDBInternalsHost,
    content::kChromeUIMediaInternalsHost,
    content::kChromeUINetworkErrorsListingHost,
    content::kChromeUIProcessInternalsHost,
    content::kChromeUIServiceWorkerInternalsHost,
#if !BUILDFLAG(IS_ANDROID)
    content::kChromeUITracingHost,
#endif
    content::kChromeUIUkmHost,
    content::kChromeUIWebRTCInternalsHost,
#if BUILDFLAG(ENABLE_VR)
    content::kChromeUIWebXrInternalsHost,
#endif
#if !BUILDFLAG(IS_ANDROID)
#if !BUILDFLAG(IS_CHROMEOS)
    kChromeUIAppLauncherPageHost,
#endif
    kChromeUIBookmarksHost,
    kChromeUIDownloadsHost,
    kChromeUIHelpHost,
    kChromeUIInspectHost,
    kChromeUINewTabPageHost,
    kChromeUINewTabPageThirdPartyHost,
    kChromeUISettingsHost,
    kChromeUISystemInfoHost,
#if !BUILDFLAG(IS_CHROMEOS)
    kChromeUIWhatsNewHost,
#endif
#endif
#if BUILDFLAG(IS_ANDROID)
    kChromeUIOfflineInternalsHost,
    kChromeUISnippetsInternalsHost,
    kChromeUIWebApksHost,
#endif
#if BUILDFLAG(IS_CHROMEOS_ASH)
    kChromeUIBorealisCreditsHost,
    kChromeUICertificateManagerHost,
    kChromeUICrostiniCreditsHost,
    kChromeUICryptohomeHost,
    kChromeUIDriveInternalsHost,
    kChromeUINetworkHost,
    kChromeUILockScreenNetworkHost,
    kChromeUIOobeHost,
    kChromeUIOSCreditsHost,
    kChromeUIOSSettingsHost,
    kChromeUIPowerHost,
    kChromeUISysInternalsHost,
    kChromeUIInternetConfigDialogHost,
    kChromeUIInternetDetailDialogHost,
    kChromeUIAssistantOptInHost,
#endif
#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX) || \
    BUILDFLAG(IS_CHROMEOS_ASH)
    kChromeUIConnectorsInternalsHost,
#endif
#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX) || \
    BUILDFLAG(IS_CHROMEOS)
    kChromeUIDiscardsHost,
#endif
#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)
    kChromeUIWebAppSettingsHost,
#endif
#if BUILDFLAG(IS_POSIX) && !BUILDFLAG(IS_MAC) && !BUILDFLAG(IS_ANDROID)
    kChromeUILinuxProxyConfigHost,
#endif
#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || \
    BUILDFLAG(IS_ANDROID)
    kChromeUISandboxHost,
#endif
#if BUILDFLAG(IS_WIN)
    kChromeUIConflictsHost,
#endif
#if BUILDFLAG(ENABLE_NACL)
    kChromeUINaClHost,
#endif
#if BUILDFLAG(ENABLE_EXTENSIONS)
    kChromeUIExtensionsHost,
    kChromeUIExtensionsInternalsHost,
#endif
#if BUILDFLAG(ENABLE_PRINT_PREVIEW)
    kChromeUIPrintHost,
#endif
    kChromeUIWebRtcLogsHost,
#if BUILDFLAG(IS_CHROMEOS)
    kChromeUIDlpInternalsHost,
#endif  // BUILDFLAG(IS_CHROMEOS)
};
const size_t kNumberOfChromeHostURLs = std::size(kChromeHostURLs);

// Add chrome://internals/* subpages here to be included in chrome://chrome-urls
// (about:about).
const char* const kChromeInternalsPathURLs[] = {
#if BUILDFLAG(IS_ANDROID)
    kChromeUIInternalsQueryTilesPath,
#endif  // BUILDFLAG(IS_ANDROID)
#if BUILDFLAG(ENABLE_SESSION_SERVICE)
    kChromeUISessionServiceInternalsPath,
#endif
};
const size_t kNumberOfChromeInternalsPathURLs =
    std::size(kChromeInternalsPathURLs);

const char* const kChromeDebugURLs[] = {
    // TODO(crbug.com/40253037): make this list comprehensive
    blink::kChromeUIBadCastCrashURL,
    blink::kChromeUIBrowserCrashURL,
    blink::kChromeUIBrowserDcheckURL,
    blink::kChromeUICrashURL,
#if BUILDFLAG(ENABLE_RUST_CRASH)
    blink::kChromeUICrashRustURL,
#if defined(ADDRESS_SANITIZER)
    blink::kChromeUICrashRustOverflowURL,
#endif
#endif  // BUILDFLAG(ENABLE_RUST_CRASH)
    blink::kChromeUIDumpURL,
    blink::kChromeUIKillURL,
    blink::kChromeUIHangURL,
    blink::kChromeUIShorthangURL,
    blink::kChromeUIGpuCleanURL,
    blink::kChromeUIGpuCrashURL,
    blink::kChromeUIGpuHangURL,
    blink::kChromeUIMemoryExhaustURL,
    blink::kChromeUIMemoryPressureCriticalURL,
    blink::kChromeUIMemoryPressureModerateURL,
#if BUILDFLAG(IS_WIN)
    blink::kChromeUIBrowserHeapCorruptionURL,
    blink::kChromeUICfgViolationCrashURL,
    blink::kChromeUIHeapCorruptionCrashURL,
#endif
#if BUILDFLAG(IS_ANDROID)
    blink::kChromeUIGpuJavaCrashURL,
    kChromeUIJavaCrashURL,
#endif
#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)
    kChromeUIWebUIJsErrorURL,
#endif
    kChromeUIQuitURL,
    kChromeUIRestartURL};
const size_t kNumberOfChromeDebugURLs = std::size(kChromeDebugURLs);

}  // namespace chrome
