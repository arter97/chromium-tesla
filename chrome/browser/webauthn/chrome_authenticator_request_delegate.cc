// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/webauthn/chrome_authenticator_request_delegate.h"

#include <algorithm>
#include <memory>
#include <string_view>
#include <utility>

#include "base/base64.h"
#include "base/command_line.h"
#include "base/containers/contains.h"
#include "base/feature_list.h"
#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/i18n/time_formatting.h"
#include "base/location.h"
#include "base/memory/raw_ptr.h"
#include "base/ranges/algorithm.h"
#include "base/strings/string_split.h"
#include "base/task/thread_pool.h"
#include "base/time/default_clock.h"
#include "base/values.h"
#include "build/build_config.h"
#include "build/chromeos_buildflags.h"
#include "chrome/browser/extensions/api/web_authentication_proxy/web_authentication_proxy_service.h"
#include "chrome/browser/net/system_network_context_manager.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_observer.h"
#include "chrome/browser/signin/identity_manager_factory.h"
#include "chrome/browser/sync/device_info_sync_service_factory.h"
#include "chrome/browser/sync/sync_service_factory.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/browser_navigator.h"
#include "chrome/browser/ui/browser_navigator_params.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/page_action/page_action_icon_type.h"
#include "chrome/browser/webauthn/authenticator_request_dialog_model.h"
#include "chrome/browser/webauthn/cablev2_devices.h"
#include "chrome/browser/webauthn/enclave_manager.h"
#include "chrome/browser/webauthn/gpm_enclave_controller.h"
#include "chrome/browser/webauthn/passkey_model_factory.h"
#include "chrome/browser/webauthn/webauthn_pref_names.h"
#include "chrome/browser/webauthn/webauthn_switches.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/chrome_version.h"
#include "chrome/common/pref_names.h"
#include "chrome/grit/generated_resources.h"
#include "components/device_event_log/device_event_log.h"
#include "components/network_session_configurator/common/network_switches.h"
#include "components/password_manager/core/common/password_manager_pref_names.h"
#include "components/pref_registry/pref_registry_syncable.h"
#include "components/prefs/pref_service.h"
#include "components/signin/public/base/consent_level.h"
#include "components/signin/public/identity_manager/identity_manager.h"
#include "components/signin/public/identity_manager/primary_account_access_token_fetcher.h"
#include "components/sync/base/features.h"
#include "components/sync/base/user_selectable_type.h"
#include "components/sync/protocol/webauthn_credential_specifics.pb.h"
#include "components/sync/service/sync_service.h"
#include "components/sync/service/sync_user_settings.h"
#include "components/trusted_vault/frontend_trusted_vault_connection.h"
#include "components/trusted_vault/trusted_vault_server_constants.h"
#include "components/user_prefs/user_prefs.h"
#include "components/webauthn/core/browser/passkey_model.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/device_service.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/content_client.h"
#include "crypto/random.h"
#include "crypto/unexportable_key.h"
#include "device/fido/cable/v2_handshake.h"
#include "device/fido/discoverable_credential_metadata.h"
#include "device/fido/enclave/constants.h"
#include "device/fido/features.h"
#include "device/fido/fido_authenticator.h"
#include "device/fido/fido_constants.h"
#include "device/fido/fido_discovery_factory.h"
#include "device/fido/fido_request_handler_base.h"
#include "device/fido/fido_types.h"
#include "device/fido/public_key_credential_descriptor.h"
#include "device/fido/public_key_credential_user_entity.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/common/constants.h"
#include "extensions/common/permissions/permissions_data.h"
#include "google_apis/gaia/gaia_constants.h"
#include "net/base/url_util.h"
#include "third_party/icu/source/common/unicode/locid.h"
#include "third_party/icu/source/i18n/unicode/timezone.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/window_open_disposition.h"
#include "ui/views/widget/widget.h"
#include "url/url_util.h"

#if BUILDFLAG(IS_MAC)
#include "chrome/browser/webauthn/chrome_authenticator_request_delegate_mac.h"
#include "device/fido/mac/authenticator.h"
#include "device/fido/mac/credential_metadata.h"
#endif

#if BUILDFLAG(IS_WIN)
#include "chrome/browser/webauthn/local_credential_management_win.h"
#include "device/fido/win/authenticator.h"
#include "device/fido/win/webauthn_api.h"
#endif

#if BUILDFLAG(IS_CHROMEOS)
#include "chrome/browser/webauthn/chromeos/passkey_dialog_controller.h"
#include "chrome/browser/webauthn/chromeos/passkey_discovery.h"
#include "chrome/browser/webauthn/chromeos/passkey_service.h"
#include "chrome/browser/webauthn/chromeos/passkey_service_factory.h"
#include "chromeos/components/webauthn/webauthn_request_registrar.h"
#include "ui/aura/window.h"
#endif

namespace {

ChromeAuthenticatorRequestDelegate::TestObserver* g_observer = nullptr;

static constexpr char kGoogleRpId[] = "google.com";

// Returns true iff |relying_party_id| is listed in the
// SecurityKeyPermitAttestation policy.
bool IsWebAuthnRPIDListedInSecurityKeyPermitAttestationPolicy(
    content::BrowserContext* browser_context,
    const std::string& relying_party_id) {
  const Profile* profile = Profile::FromBrowserContext(browser_context);
  const PrefService* prefs = profile->GetPrefs();
  const base::Value::List& permit_attestation =
      prefs->GetList(prefs::kSecurityKeyPermitAttestation);
  const std::string& (base::Value::*get_string)() const =
      &base::Value::GetString;
  return base::Contains(permit_attestation, relying_party_id, get_string);
}

bool IsOriginListedInEnterpriseAttestationSwitch(
    const url::Origin& caller_origin) {
  std::string cmdline_origins =
      base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII(
          webauthn::switches::kPermitEnterpriseAttestationOriginList);
  std::vector<std::string_view> origin_strings = base::SplitStringPiece(
      cmdline_origins, ",", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
  return base::ranges::any_of(
      origin_strings, [&caller_origin](std::string_view origin_string) {
        return url::Origin::Create(GURL(origin_string)) == caller_origin;
      });
}

// Returns true iff the credential is reported as being present on the platform
// authenticator (i.e. it is not a phone or icloud credential).
bool IsCredentialFromPlatformAuthenticator(
    device::DiscoverableCredentialMetadata cred) {
  return cred.source != device::AuthenticatorType::kICloudKeychain &&
         cred.source != device::AuthenticatorType::kPhone;
}

// Returns true iff |cred_id| starts with the prefix reserved for passkeys used
// to authenticate to Google services.
bool CredIdHasGooglePasskeyAuthPrefix(const std::vector<uint8_t>& cred_id) {
  constexpr std::string_view kPrefix = "GOOGLE_ACCOUNT:";
  if (cred_id.size() < kPrefix.size()) {
    return false;
  }
  return memcmp(cred_id.data(), kPrefix.data(), kPrefix.size()) == 0;
}

// Filters |passkeys| to only contain credentials that are used to authenticate
// to Google services.
void FilterGoogleAuthPasskeys(
    std::vector<device::DiscoverableCredentialMetadata>* passkeys) {
  std::erase_if(*passkeys, [](const auto& passkey) {
    return IsCredentialFromPlatformAuthenticator(passkey) &&
           !CredIdHasGooglePasskeyAuthPrefix(passkey.user.id);
  });
}

// Returns true if |extension| is allowed to create and assert |rp_id|.
bool ExtensionCanAssertRpId(const extensions::Extension& extension,
                            const std::string& rp_id) {
  // Extensions are always allowed to assert their own extension identifier.
  // This has special handling in
  // ChromeWebAuthenticationDelegate::MaybeGetRelyingPartyIdOverride, the RP ID
  // will be prefixed with the extension scheme to isolate it from web origins.
  if (extension.id() == rp_id) {
    return true;
  }
  if (!base::FeatureList::IsEnabled(
          device::kAllowExtensionsToSetWebAuthnRpIds)) {
    return false;
  }

  // Extensions may not claim eTLDs as RP IDs, even if WebAuthn does not
  // forbid origins from doing so if they are eTLDs themselves.
  if (!net::registry_controlled_domains::HostHasRegistryControlledDomain(
          base::TrimString(rp_id, ".", base::TrimPositions::TRIM_TRAILING),
          net::registry_controlled_domains::INCLUDE_UNKNOWN_REGISTRIES,
          net::registry_controlled_domains::INCLUDE_PRIVATE_REGISTRIES) &&
      !net::HostStringIsLocalhost(rp_id)) {
    return false;
  }

  // An extension should be able to assert a WebAuthn RP ID if it has host
  // permissions over any host that can assert that RP ID. This code duplicates
  // some of the logic on content/public/browser/webauthn_security_utils.h.
  // https://w3c.github.io/webauthn/#relying-party-identifier
  for (const URLPattern& pattern :
       extension.permissions_data()->active_permissions().explicit_hosts()) {
    // Only https hosts and localhost are allowed to assert RP IDs. By design,
    // this means extensions cannot claim RP IDs for other extensions.
    if (!pattern.MatchesScheme(url::kHttpsScheme) &&
        !(pattern.MatchesScheme(url::kHttpScheme) &&
          net::HostStringIsLocalhost(pattern.host()))) {
      continue;
    }
    // IP hosts are not allowed to assert RP IDs.
    if (url::HostIsIPAddress(pattern.host())) {
      continue;
    }
    // If the pattern matches the RP ID, then it is allowed to assert it.
    // Pattern                   RP ID                     Allowed?
    // *.com                     example.com               Yes
    // example.com               example.com               Yes
    // *.example.com             subdomain.example.com     Yes
    if (pattern.MatchesHost(rp_id)) {
      return true;
    }
    // If the pattern matchens any valid subdomain of the RP ID, then it is
    // allowed to assert it, since subdomains can assert parent components up to
    // eTLD+1 on WebAuthn.
    // Pattern                   RP ID                     Allowed?
    // subdomain.example.com     example.com               Yes
    // *.subdomain.example.com   example.com               Yes
    // example.com               subdomain.example.com     No
    if (url::DomainIs(pattern.host(), rp_id)) {
      return true;
    }
  }
  return false;
}

#if BUILDFLAG(IS_MAC)
const char kWebAuthnTouchIdMetadataSecretPrefName[] =
    "webauthn.touchid.metadata_secret";
const char kWebAuthnTouchIdLastUsed[] = "webauthn.touchid.last_used";

// kMacOsRecentlyUsedMaxDays specifies how recently the macOS profile
// authenticator must have been used (for the current profile) to be considered
// "actively" used. Chrome may default to the profile authenticator in more
// cases if it is being actively used.
const int kMacOsRecentlyUsedMaxDays = 31;
#endif

// CableLinkingEventHandler handles linking information sent by caBLEv2
// authenticators. This linking information can come after the WebAuthn
// operation has resolved and thus after the
// `ChromeAuthenticatorRequestDelegate` has been destroyed. Thus this object is
// owned by the callback itself, and can save linking information until the
// point where the `Profile` itself is destroyed.
class CableLinkingEventHandler : public ProfileObserver {
 public:
  explicit CableLinkingEventHandler(Profile* profile) : profile_(profile) {
    profile_->AddObserver(this);
  }

  ~CableLinkingEventHandler() override {
    if (profile_) {
      profile_->RemoveObserver(this);
      profile_ = nullptr;
    }
  }

  void OnNewCablePairing(std::unique_ptr<device::cablev2::Pairing> pairing) {
    if (!profile_) {
      FIDO_LOG(DEBUG) << "Linking event was discarded because it was received "
                         "after the profile was destroyed.";
      return;
    }

    // Drop linking in Incognito sessions. While an argument could be made that
    // it's OK to persist them, this seems like the safe option.
    if (profile_->IsOffTheRecord()) {
      FIDO_LOG(DEBUG) << "Linking event was discarded because the profile is "
                         "Off The Record.";
      return;
    }

    cablev2::AddPairing(profile_, std::move(pairing));
  }

  // ProfileObserver:

  void OnProfileWillBeDestroyed(Profile* profile) override {
    DCHECK_EQ(profile, profile_);
    profile_->RemoveObserver(this);
    profile_ = nullptr;
  }

 private:
  raw_ptr<Profile> profile_;
};

}  // namespace

// ---------------------------------------------------------------------
// ChromeWebAuthenticationDelegate
// ---------------------------------------------------------------------

ChromeWebAuthenticationDelegate::ChromeWebAuthenticationDelegate() = default;
ChromeWebAuthenticationDelegate::~ChromeWebAuthenticationDelegate() = default;

bool ChromeWebAuthenticationDelegate::
    OverrideCallerOriginAndRelyingPartyIdValidation(
        content::BrowserContext* browser_context,
        const url::Origin& caller_origin,
        const std::string& relying_party_id) {
  // Allow chrome-extensions:// origins to make WebAuthn requests.
  // `MaybeGetRelyingPartyId` will override the RP ID if it matches the
  // extension origin.
  if (caller_origin.scheme() != extensions::kExtensionScheme) {
    return false;
  }
  const extensions::Extension* extension =
      extensions::ExtensionRegistry::Get(browser_context)
          ->enabled_extensions()
          .GetByID(caller_origin.host());
  if (!extension) {
    return false;
  }
  return ExtensionCanAssertRpId(*extension, relying_party_id);
}

bool ChromeWebAuthenticationDelegate::OriginMayUseRemoteDesktopClientOverride(
    content::BrowserContext* browser_context,
    const url::Origin& caller_origin) {
  // Allow the Google-internal version of Chrome Remote Desktop to use the
  // RemoteDesktopClientOverride extension and make WebAuthn
  // requests on behalf of other origins, if a corresponding enteprise policy is
  // enabled.
  //
  // The policy explicitly does not cover external instances of CRD. It
  // must not be extended to other origins or be made configurable without going
  // through security review.
  if (!base::FeatureList::IsEnabled(
          device::kWebAuthnGoogleCorpRemoteDesktopClientPrivilege)) {
    return false;
  }

  const Profile* profile = Profile::FromBrowserContext(browser_context);
  const PrefService* prefs = profile->GetPrefs();
  const bool google_corp_remote_proxied_request_allowed =
      prefs->GetBoolean(webauthn::pref_names::kRemoteProxiedRequestsAllowed);
  if (!google_corp_remote_proxied_request_allowed) {
    return false;
  }

  constexpr const char* const kGoogleCorpCrdOrigins[] = {
      "https://remotedesktop.corp.google.com",
      "https://remotedesktop-autopush.corp.google.com/",
      "https://remotedesktop-daily-6.corp.google.com/",
  };
  for (const char* corp_crd_origin : kGoogleCorpCrdOrigins) {
    if (caller_origin == url::Origin::Create(GURL(corp_crd_origin))) {
      return true;
    }
  }

  // An additional origin can be passed on the command line for testing.
  if (!base::CommandLine::ForCurrentProcess()->HasSwitch(
          webauthn::switches::kRemoteProxiedRequestsAllowedAdditionalOrigin)) {
    return false;
  }
  // Note that `cmdline_allowed_origin` will be opaque if the flag is not a
  // valid URL, which won't match `caller_origin`.
  const url::Origin cmdline_allowed_origin = url::Origin::Create(
      GURL(base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII(
          webauthn::switches::kRemoteProxiedRequestsAllowedAdditionalOrigin)));
  return caller_origin == cmdline_allowed_origin;
}

std::optional<std::string>
ChromeWebAuthenticationDelegate::MaybeGetRelyingPartyIdOverride(
    const std::string& claimed_relying_party_id,
    const url::Origin& caller_origin) {
  // Extensions may claim their origin as an RP ID. In that case, we use the
  // whole origin to avoid collisions with the RP ID space for HTTPS origins.
  // Extensions may not claim other extensions RP IDs, even if they have host
  // permissions on them.
  if (caller_origin.scheme() == extensions::kExtensionScheme &&
      claimed_relying_party_id == caller_origin.host()) {
    return caller_origin.Serialize();
  }

  return std::nullopt;
}

bool ChromeWebAuthenticationDelegate::ShouldPermitIndividualAttestation(
    content::BrowserContext* browser_context,
    const url::Origin& caller_origin,
    const std::string& relying_party_id) {
  return IsOriginListedInEnterpriseAttestationSwitch(caller_origin) ||
         IsWebAuthnRPIDListedInSecurityKeyPermitAttestationPolicy(
             browser_context, relying_party_id);
}

bool ChromeWebAuthenticationDelegate::SupportsResidentKeys(
    content::RenderFrameHost* render_frame_host) {
  return true;
}

bool ChromeWebAuthenticationDelegate::SupportsPasskeyMetadataSyncing() {
  return base::FeatureList::IsEnabled(syncer::kSyncWebauthnCredentials);
}

bool ChromeWebAuthenticationDelegate::IsFocused(
    content::WebContents* web_contents) {
  return web_contents->GetVisibility() == content::Visibility::VISIBLE;
}

void ChromeWebAuthenticationDelegate::
    IsUserVerifyingPlatformAuthenticatorAvailableOverride(
        content::RenderFrameHost* render_frame_host,
        base::OnceCallback<void(std::optional<bool>)> callback) {
  const bool is_google =
      render_frame_host->GetLastCommittedOrigin().DomainIs("google.com");
  content::WebAuthenticationDelegate::
      IsUserVerifyingPlatformAuthenticatorAvailableOverride(
          render_frame_host,
          base::BindOnce(
              [](base::WeakPtr<ChromeWebAuthenticationDelegate>
                     web_authentication_delegate,
                 base::WeakPtr<content::BrowserContext> browser_context,
                 const bool is_google,
                 base::OnceCallback<void(std::optional<bool>)> callback,
                 std::optional<bool> testing_api_override) {
                if (!browser_context || !web_authentication_delegate) {
                  return;
                }

                // If the testing API is active, its override takes precedence.
                if (testing_api_override) {
                  std::move(callback).Run(*testing_api_override);
                  return;
                }

                // Chrome disables platform authenticators in Guest sessions.
                // They may be available (behind an additional interstitial) in
                // Incognito mode.
                Profile* profile =
                    Profile::FromBrowserContext(browser_context.get());
                if (profile->IsGuestSession()) {
                  std::move(callback).Run(false);
                  return;
                }

                // Without GPM PIN support the enclave authenticator depends on
                // having an Android LSKF enrolled to the security domain. But
                // we can't check that without querying the security domain
                // service, which we don't want to do just because a site
                // called IsUVPAA. Thus we conservatively ignore the
                // possibility of using the enclave authenticator here.
                if (!device::kWebAuthnGpmPin.Get()) {
                  std::move(callback).Run(std::nullopt);
                  return;
                }

                // The enclave won't allow credentials for an account to be
                // stored in GPM _in_ that account. So we don't want to tell
                // accounts.google.com that there is a platform authenticator
                // if we're later going to reject a create() request for that
                // reason. Thus we have to be conservative with IsUVPAA
                // responses on google.com and ignore the possibility of using
                // the enclave authenticator.
                if (is_google) {
                  std::move(callback).Run(std::nullopt);
                  return;
                }

                web_authentication_delegate->BrowserProvidedPasskeysAvailable(
                    browser_context.get(),
                    base::BindOnce(
                        [](base::OnceCallback<void(std::optional<bool>)>
                               callback,
                           bool available) {
                          if (available) {
                            std::move(callback).Run(true);
                            return;
                          }
                          std::move(callback).Run(std::nullopt);
                        },
                        std::move(callback)));
              },
              weak_ptr_factory_.GetWeakPtr(),
              render_frame_host->GetBrowserContext()->GetWeakPtr(), is_google,
              std::move(callback)));
}

content::WebAuthenticationRequestProxy*
ChromeWebAuthenticationDelegate::MaybeGetRequestProxy(
    content::BrowserContext* browser_context,
    const url::Origin& caller_origin) {
  auto* service = extensions::WebAuthenticationProxyService::GetIfProxyAttached(
      Profile::FromBrowserContext(browser_context));
  return service && service->IsActive(caller_origin) ? service : nullptr;
}

#if BUILDFLAG(IS_MAC)
// static
ChromeWebAuthenticationDelegate::TouchIdAuthenticatorConfig
ChromeWebAuthenticationDelegate::TouchIdAuthenticatorConfigForProfile(
    Profile* profile) {
  constexpr char kKeychainAccessGroup[] =
      MAC_TEAM_IDENTIFIER_STRING "." MAC_BUNDLE_IDENTIFIER_STRING ".webauthn";

  std::string metadata_secret =
      profile->GetPrefs()->GetString(kWebAuthnTouchIdMetadataSecretPrefName);
  if (metadata_secret.empty() ||
      !base::Base64Decode(metadata_secret, &metadata_secret)) {
    metadata_secret = device::fido::mac::GenerateCredentialMetadataSecret();
    profile->GetPrefs()->SetString(
        kWebAuthnTouchIdMetadataSecretPrefName,
        base::Base64Encode(base::as_bytes(base::make_span(metadata_secret))));
  }

  return TouchIdAuthenticatorConfig{
      .keychain_access_group = kKeychainAccessGroup,
      .metadata_secret = std::move(metadata_secret)};
}

std::optional<ChromeWebAuthenticationDelegate::TouchIdAuthenticatorConfig>
ChromeWebAuthenticationDelegate::GetTouchIdAuthenticatorConfig(
    content::BrowserContext* browser_context) {
  return TouchIdAuthenticatorConfigForProfile(
      Profile::FromBrowserContext(browser_context));
}
#endif  // BUILDFLAG(IS_MAC)

#if BUILDFLAG(IS_CHROMEOS)
content::WebAuthenticationDelegate::ChromeOSGenerateRequestIdCallback
ChromeWebAuthenticationDelegate::GetGenerateRequestIdCallback(
    content::RenderFrameHost* render_frame_host) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  aura::Window* window =
      render_frame_host->GetNativeView()->GetToplevelWindow();
  return chromeos::webauthn::WebAuthnRequestRegistrar::Get()
      ->GetRegisterCallback(window);
}
#endif  // BUILDFLAG(IS_CHROMEOS)

void ChromeWebAuthenticationDelegate::BrowserProvidedPasskeysAvailable(
    content::BrowserContext* browser_context,
    base::OnceCallback<void(bool)> callback) {
#if BUILDFLAG(IS_CHROMEOS)
  chromeos::PasskeyService* passkey_service =
      chromeos::PasskeyServiceFactory::GetForProfile(
          Profile::FromBrowserContext(browser_context));
  std::move(callback).Run(passkey_service &&
                          passkey_service->GpmPasskeysAvailable());
#else
  if (!base::FeatureList::IsEnabled(device::kWebAuthnEnclaveAuthenticator)) {
    FIDO_LOG(EVENT) << "Enclave authenticator disabled because flag not set";
    std::move(callback).Run(false);
    return;
  }

  auto* profile =
      Profile::FromBrowserContext(browser_context)->GetOriginalProfile();
  auto* const identity_manager = IdentityManagerFactory::GetForProfile(profile);
  if (!identity_manager ||
      !identity_manager->HasPrimaryAccount(signin::ConsentLevel::kSignin)) {
    FIDO_LOG(EVENT)
        << "Enclave authenticator disabled because no suitable account";
    std::move(callback).Run(false);
    return;
  }

  auto* const sync_service = SyncServiceFactory::GetForProfile(profile);

  // TODO(crbug.com/40066949): Remove this call once IsSyncFeatureEnabled()
  // is fully deprecated, see ConsentLevel::kSync documentation for details,
  // in components/signin/public/base/consent_level.h.
  if (!sync_service || !sync_service->IsSyncFeatureEnabled() ||
      !sync_service->GetUserSettings()->GetSelectedTypes().Has(
          syncer::UserSelectableType::kPasswords)) {
    FIDO_LOG(EVENT)
        << "Enclave authenticator disabled because password sync not active";
    std::move(callback).Run(false);
    return;
  }
  // Check for TPM availability.
  if (tpm_available_.has_value()) {
    std::move(callback).Run(*tpm_available_);
    return;
  }
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::TaskPriority::BEST_EFFORT, base::MayBlock()},
      base::BindOnce([]() -> bool {
        crypto::UnexportableKeyProvider::Config config;
#if BUILDFLAG(IS_MAC)
        config.keychain_access_group =
            EnclaveManager::kEnclaveKeysKeychainAccessGroup;
#endif  // BUILDFLAG(IS_MAC)
        std::unique_ptr<crypto::UnexportableKeyProvider> provider =
            crypto::GetUnexportableKeyProvider(std::move(config));
        if (!provider) {
          FIDO_LOG(EVENT)
              << "Enclave authenticator disabled because no key provider";
          return false;
        }
        return provider->SelectAlgorithm(device::enclave::kSigningAlgorithms) !=
               std::nullopt;
      }),
      base::BindOnce(
          [](base::OnceCallback<void(bool)> callback,
             base::WeakPtr<ChromeWebAuthenticationDelegate> webauthn_delegate,
             bool available) {
            if (webauthn_delegate) {
              webauthn_delegate->tpm_available_ = available;
            }
            if (!available) {
              FIDO_LOG(EVENT)
                  << "Enclave authenticator disabled because of lack of TPM";
            }
            std::move(callback).Run(available);
          },
          std::move(callback), weak_ptr_factory_.GetWeakPtr()));
#endif
}

// ---------------------------------------------------------------------
// ChromeAuthenticatorRequestDelegate
// ---------------------------------------------------------------------

std::vector<std::unique_ptr<device::cablev2::Pairing>>
ChromeAuthenticatorRequestDelegate::TestObserver::
    GetCablePairingsFromSyncedDevices() {
  return {};
}

// static
void ChromeAuthenticatorRequestDelegate::RegisterProfilePrefs(
    user_prefs::PrefRegistrySyncable* registry) {
  registry->RegisterListPref(prefs::kSecurityKeyPermitAttestation);
  registry->RegisterIntegerPref(
      webauthn::pref_names::kEnclaveDeclinedGPMCredentialCreationCount, 0);
  registry->RegisterIntegerPref(
      webauthn::pref_names::kEnclaveDeclinedGPMBootstrappingCount, 0);
#if BUILDFLAG(IS_WIN)
  LocalCredentialManagementWin::RegisterProfilePrefs(registry);
#endif
#if BUILDFLAG(IS_MAC)
  registry->RegisterStringPref(kWebAuthnTouchIdMetadataSecretPrefName,
                               std::string());
  registry->RegisterStringPref(kWebAuthnTouchIdLastUsed, std::string());
  // This boolean preference is used as a tristate. If unset, whether or not to
  // default to iCloud is determined based on several factors.
  // (See `ShouldCreateInICloudKeychain`.) If set, then this preference is
  // controlling.
  //
  // The default value of this preference only determines whether the toggle
  // in settings will show as set or not when the preference hasn't been
  // explicitly set. Since the behaviour is actually more complex than can be
  // expressed in a boolean, this is always an approximation.
  registry->RegisterBooleanPref(
      prefs::kCreatePasskeysInICloudKeychain,
      ShouldCreateInICloudKeychain(
          RequestSource::kWebAuthentication,
          // Whether or not the user is actively using the profile authenticator
          // is stored in preferences, which aren't available at this time while
          // we're still registering them. Thus we assume that they are not.
          /*is_active_profile_authenticator_user=*/false,
          IsICloudDriveEnabled(),
          /*request_is_for_google_com=*/false, /*preference=*/std::nullopt));
#endif
  cablev2::RegisterProfilePrefs(registry);
}

ChromeAuthenticatorRequestDelegate::ChromeAuthenticatorRequestDelegate(
    content::RenderFrameHost* render_frame_host)
    : render_frame_host_id_(render_frame_host->GetGlobalId()),
      dialog_model_(std::make_unique<AuthenticatorRequestDialogModel>(
          GetRenderFrameHost())),
      dialog_controller_(std::make_unique<AuthenticatorRequestDialogController>(
          dialog_model_.get())) {
  dialog_model_->observers.AddObserver(this);
  if (g_observer) {
    g_observer->Created(this);
  }
}

ChromeAuthenticatorRequestDelegate::~ChromeAuthenticatorRequestDelegate() {
  // Currently, completion of the request is indicated by //content destroying
  // this delegate.
  dialog_model_->OnRequestComplete();
  dialog_model_->observers.RemoveObserver(this);

  if (g_observer) {
    g_observer->OnDestroy(this);
  }
}

// static
void ChromeAuthenticatorRequestDelegate::SetGlobalObserverForTesting(
    TestObserver* observer) {
  CHECK(!observer || !g_observer);
  g_observer = observer;
}

base::WeakPtr<ChromeAuthenticatorRequestDelegate>
ChromeAuthenticatorRequestDelegate::AsWeakPtr() {
  return weak_ptr_factory_.GetWeakPtr();
}

GPMEnclaveController*
ChromeAuthenticatorRequestDelegate::enclave_controller_for_testing() const {
  return enclave_controller_.get();
}

#if BUILDFLAG(IS_CHROMEOS)
chromeos::PasskeyDialogController&
ChromeAuthenticatorRequestDelegate::chromeos_passkey_controller_for_testing()
    const {
  CHECK(chromeos_passkey_controller_);
  return *chromeos_passkey_controller_;
}
#endif

void ChromeAuthenticatorRequestDelegate::SetRelyingPartyId(
    const std::string& rp_id) {
  dialog_model_->relying_party_id = rp_id;
}

bool ChromeAuthenticatorRequestDelegate::DoesBlockRequestOnFailure(
    InterestingFailureReason reason) {
  if (!IsWebAuthnUIEnabled()) {
    return false;
  }

  // If the UI was already in the state where we asked the user to complete the
  // transaction on the other device then any errors are immediately resolved.
  // Very likely the user canceled on the phone and doesn't want to see another
  // error UI on the desktop.
  if (cable_device_ready_) {
    return false;
  }

  switch (reason) {
    case InterestingFailureReason::kTimeout:
      dialog_controller_->OnRequestTimeout();
      break;
    case InterestingFailureReason::kKeyNotRegistered:
      dialog_controller_->OnActivatedKeyNotRegistered();
      break;
    case InterestingFailureReason::kKeyAlreadyRegistered:
      dialog_controller_->OnActivatedKeyAlreadyRegistered();
      break;
    case InterestingFailureReason::kSoftPINBlock:
      dialog_controller_->OnSoftPINBlock();
      break;
    case InterestingFailureReason::kHardPINBlock:
      dialog_controller_->OnHardPINBlock();
      break;
    case InterestingFailureReason::kAuthenticatorRemovedDuringPINEntry:
      dialog_controller_->OnAuthenticatorRemovedDuringPINEntry();
      break;
    case InterestingFailureReason::kAuthenticatorMissingResidentKeys:
      dialog_controller_->OnAuthenticatorMissingResidentKeys();
      break;
    case InterestingFailureReason::kAuthenticatorMissingUserVerification:
      dialog_controller_->OnAuthenticatorMissingUserVerification();
      break;
    case InterestingFailureReason::kAuthenticatorMissingLargeBlob:
      dialog_controller_->OnAuthenticatorMissingLargeBlob();
      break;
    case InterestingFailureReason::kNoCommonAlgorithms:
      dialog_controller_->OnNoCommonAlgorithms();
      break;
    case InterestingFailureReason::kStorageFull:
      dialog_controller_->OnAuthenticatorStorageFull();
      break;
    case InterestingFailureReason::kUserConsentDenied:
      dialog_controller_->OnUserConsentDenied();
      break;
    case InterestingFailureReason::kWinUserCancelled:
      return dialog_controller_->OnWinUserCancelled();
    case InterestingFailureReason::kHybridTransportError:
      return dialog_controller_->OnHybridTransportError();
    case InterestingFailureReason::kNoPasskeys:
      return dialog_controller_->OnNoPasskeys();
    case InterestingFailureReason::kEnclaveError:
      return dialog_controller_->OnEnclaveError();
    case InterestingFailureReason::kEnclaveCancel:
      dialog_model_->CancelAuthenticatorRequest();
      break;
  }
  return true;
}

void ChromeAuthenticatorRequestDelegate::OnTransactionSuccessful(
    RequestSource request_source,
    device::FidoRequestType request_type,
    device::AuthenticatorType authenticator_type) {
#if BUILDFLAG(IS_MAC)
  if (request_source != RequestSource::kWebAuthentication) {
    return;
  }

  if (authenticator_type == device::AuthenticatorType::kTouchID) {
    Profile::FromBrowserContext(GetBrowserContext())
        ->GetPrefs()
        ->SetString(
            kWebAuthnTouchIdLastUsed,
            base::UnlocalizedTimeFormatWithPattern(
                base::Time::Now(), "yyyy-MM-dd", icu::TimeZone::getGMT()));
  }

  dialog_controller_->RecordMacOsSuccessHistogram(request_type,
                                                  authenticator_type);
#endif
}

void ChromeAuthenticatorRequestDelegate::RegisterActionCallbacks(
    base::OnceClosure cancel_callback,
    base::RepeatingClosure start_over_callback,
    AccountPreselectedCallback account_preselected_callback,
    device::FidoRequestHandlerBase::RequestCallback request_callback,
    base::RepeatingClosure bluetooth_adapter_power_on_callback) {
  request_callback_ = request_callback;
  cancel_callback_ = std::move(cancel_callback);
  start_over_callback_ = std::move(start_over_callback);
  account_preselected_callback_ = std::move(account_preselected_callback);

  dialog_controller_->SetRequestCallback(request_callback);
  dialog_controller_->SetAccountPreselectedCallback(
      account_preselected_callback_);
  dialog_controller_->SetBluetoothAdapterPowerOnCallback(
      bluetooth_adapter_power_on_callback);
}

void ChromeAuthenticatorRequestDelegate::ShouldReturnAttestation(
    const std::string& relying_party_id,
    const device::FidoAuthenticator* authenticator,
    bool is_enterprise_attestation,
    base::OnceCallback<void(bool)> callback) {
  if (disable_ui_ && IsVirtualEnvironmentEnabled()) {
    std::move(callback).Run(true);
    return;
  }
  if (IsWebAuthnRPIDListedInSecurityKeyPermitAttestationPolicy(
          GetBrowserContext(), relying_party_id)) {
    // Enterprise attestations should have been approved already and not reach
    // this point.
    DCHECK(!is_enterprise_attestation);
    std::move(callback).Run(true);
    return;
  }

  // AuthenticatorCommon can't evaluate attestation decisions with the UI
  // disabled.
  if (disable_ui_) {
    NOTREACHED_IN_MIGRATION();
    std::move(callback).Run(false);
    return;
  }

#if BUILDFLAG(IS_WIN)
  if (authenticator->GetType() == device::AuthenticatorType::kWinNative &&
      static_cast<const device::WinWebAuthnApiAuthenticator*>(authenticator)
          ->ShowsPrivacyNotice()) {
    // The OS' native API includes an attestation prompt.
    std::move(callback).Run(true);
    return;
  }
#endif  // BUILDFLAG(IS_WIN)

  dialog_controller_->RequestAttestationPermission(is_enterprise_attestation,
                                                   std::move(callback));
}

std::vector<std::unique_ptr<device::FidoDiscoveryBase>>
ChromeAuthenticatorRequestDelegate::CreatePlatformDiscoveries() {
  std::vector<std::unique_ptr<device::FidoDiscoveryBase>> discoveries;
#if BUILDFLAG(IS_CHROMEOS)
  if (base::FeatureList::IsEnabled(device::kChromeOsPasskeys)) {
    discoveries.push_back(
        std::make_unique<chromeos::PasskeyDiscovery>(GetRenderFrameHost()));
  }
#endif
  return discoveries;
}

void ChromeAuthenticatorRequestDelegate::ConfigureDiscoveries(
    const url::Origin& origin,
    const std::string& rp_id,
    RequestSource request_source,
    device::FidoRequestType request_type,
    std::optional<device::ResidentKeyRequirement> resident_key_requirement,
    device::UserVerificationRequirement user_verification_requirement,
    std::optional<std::string_view> user_name,
    base::span<const device::CableDiscoveryData> pairings_from_extension,
    bool browser_provided_passkeys_available,
    device::FidoDiscoveryFactory* discovery_factory) {
  DCHECK(request_type == device::FidoRequestType::kGetAssertion ||
         resident_key_requirement.has_value());
  request_type_ = request_type;
  user_verification_requirement_ = user_verification_requirement;
  Profile* const profile = Profile::FromBrowserContext(GetBrowserContext());

  // Without the UI enabled, discoveries like caBLE, Android AOA, iCloud
  // keychain, and the enclave, don't make sense.
  if (disable_ui_) {
    return;
  }

  if (browser_provided_passkeys_available && !IsVirtualEnvironmentEnabled()) {
    // Creating credentials in GPM can be disabled by policy, but get() is
    // always allowed.
    if (request_type == device::FidoRequestType::kGetAssertion ||
        profile->GetPrefs()->GetBoolean(
            password_manager::prefs::kCredentialsEnableService)) {
#if BUILDFLAG(IS_CHROMEOS)
      chromeos::PasskeyService* passkey_service =
          chromeos::PasskeyServiceFactory::GetForProfile(profile);
      CHECK(passkey_service && passkey_service->GpmPasskeysAvailable());
      chromeos_passkey_controller_ =
          std::make_unique<chromeos::PasskeyDialogController>(
              dialog_model_.get(), passkey_service,
              PasskeyModelFactory::GetInstance()->GetForProfile(profile), rp_id,
              request_type, user_verification_requirement);
#else
      auto* const identity_manager =
          IdentityManagerFactory::GetForProfile(profile->GetOriginalProfile());
      const auto consent = signin::ConsentLevel::kSignin;
      if (identity_manager->HasPrimaryAccount(consent)) {
        std::string account_name =
            identity_manager->GetPrimaryAccountInfo(consent).email;
        // The enclave should not allow a credential to be created within it for
        // the same Google account.
        if (rp_id != kGoogleRpId ||
            request_type != device::FidoRequestType::kMakeCredential ||
            user_name.value_or("") != account_name) {
          dialog_model_->account_name = std::move(account_name);
          enclave_controller_ = std::make_unique<GPMEnclaveController>(
              GetRenderFrameHost(), dialog_model_.get(), rp_id, request_type,
              user_verification_requirement,
              clock_ ? clock_ : base::DefaultClock::GetInstance(),
              std::move(pending_trusted_vault_connection_));
        }
      }
#endif
    } else {
      FIDO_LOG(EVENT)
          << "Enclave unavailable for creating passkeys due to policy.";
    }
  }

  const bool cable_extension_permitted = ShouldPermitCableExtension(origin);
  const bool cable_extension_provided =
      cable_extension_permitted && !pairings_from_extension.empty();

  if (g_observer) {
    for (const auto& pairing : pairings_from_extension) {
      if (pairing.version == device::CableDiscoveryData::Version::V2) {
        g_observer->CableV2ExtensionSeen(pairing.v2->server_link_data);
      }
    }

    g_observer->ConfiguringCable(request_type);
  }

#if BUILDFLAG(IS_LINUX)
  // No caBLEv1 on Linux. It tends to crash bluez.
  if (base::Contains(pairings_from_extension,
                     device::CableDiscoveryData::Version::V1,
                     &device::CableDiscoveryData::version)) {
    pairings_from_extension = base::span<const device::CableDiscoveryData>();
  }
#endif

  std::vector<device::CableDiscoveryData> pairings;
  if (cable_extension_permitted) {
    pairings.insert(pairings.end(), pairings_from_extension.begin(),
                    pairings_from_extension.end());
  }
  const bool cable_extension_accepted = !pairings.empty();
  const bool cablev2_extension_provided =
      base::Contains(pairings, device::CableDiscoveryData::Version::V2,
                     &device::CableDiscoveryData::version);
  const bool ignore_linked_cable_devices =
      origin.DomainIs("google.com") && discovery_factory->no_cable_linking;

  std::vector<std::unique_ptr<device::cablev2::Pairing>> paired_phones;
  base::RepeatingCallback<void(std::unique_ptr<device::cablev2::Pairing>)>
      contact_phone_callback;
  if (!ignore_linked_cable_devices &&
      (!cable_extension_provided ||
       base::FeatureList::IsEnabled(device::kWebAuthCableExtensionAnywhere))) {
    std::unique_ptr<cablev2::KnownDevices> known_devices =
        cablev2::KnownDevices::FromProfile(profile);
    if (g_observer) {
      known_devices->synced_devices =
          g_observer->GetCablePairingsFromSyncedDevices();
    }
    can_use_synced_phone_passkeys_ = !known_devices->synced_devices.empty();
    paired_phones = cablev2::MergeDevices(std::move(known_devices),
                                          &icu::Locale::getDefault());

    // The debug log displays in reverse order, so the headline is emitted after
    // the names.
    for (const auto& pairing : paired_phones) {
      FIDO_LOG(DEBUG) << "• " << pairing->name << " " << pairing->last_updated
                      << " priority:" << pairing->channel_priority;
    }
    FIDO_LOG(DEBUG) << "Found " << paired_phones.size() << " caBLEv2 devices";

    if (!paired_phones.empty()) {
      contact_phone_callback = discovery_factory->get_cable_contact_callback();
    }
  }

#if BUILDFLAG(IS_WIN)
  device::WinWebAuthnApi* const webauthn_api =
      device::WinWebAuthnApi::GetDefault();
  const bool system_handles_cable =
      webauthn_api && webauthn_api->SupportsHybrid() &&
      // For now, Chrome handles hybrid even if Windows supports it for synced
      // GPM passkeys.
      !base::FeatureList::IsEnabled(syncer::kSyncWebauthnCredentials);
#else
  constexpr bool system_handles_cable = false;
#endif

  const bool non_extension_cablev2_enabled =
      !system_handles_cable &&
      (!cable_extension_permitted ||
       (!cable_extension_provided &&
        request_type == device::FidoRequestType::kGetAssertion) ||
       (request_type == device::FidoRequestType::kMakeCredential &&
        resident_key_requirement.has_value() &&
        resident_key_requirement.value() !=
            device::ResidentKeyRequirement::kDiscouraged) ||
       base::FeatureList::IsEnabled(device::kWebAuthCableExtensionAnywhere));

  std::optional<std::array<uint8_t, device::cablev2::kQRKeySize>>
      qr_generator_key;
  std::optional<std::string> qr_string;
  if (non_extension_cablev2_enabled || cablev2_extension_provided) {
    // A QR key is generated for all caBLEv2 cases but whether the QR code is
    // displayed is up to the UI.
    qr_generator_key.emplace();
    crypto::RandBytes(*qr_generator_key);
    qr_string = device::cablev2::qr::Encode(*qr_generator_key, request_type);

    auto linking_handler = std::make_unique<CableLinkingEventHandler>(profile);
    discovery_factory->set_cable_pairing_callback(
        base::BindRepeating(&CableLinkingEventHandler::OnNewCablePairing,
                            std::move(linking_handler)));
    discovery_factory->set_cable_invalidated_pairing_callback(
        base::BindRepeating(
            &ChromeAuthenticatorRequestDelegate::OnInvalidatedCablePairing,
            weak_ptr_factory_.GetWeakPtr()));
    discovery_factory->set_cable_event_callback(
        base::BindRepeating(&ChromeAuthenticatorRequestDelegate::OnCableEvent,
                            weak_ptr_factory_.GetWeakPtr()));
  }

  if (non_extension_cablev2_enabled || cablev2_extension_provided ||
      enclave_controller_) {
    if (SystemNetworkContextManager::GetInstance()) {
      // TODO(nsatragno): this should probably use a storage partition network
      // context instead. See the SystemNetworkContextManager class comments.
      discovery_factory->set_network_context_factory(base::BindRepeating([]() {
        return SystemNetworkContextManager::GetInstance()->GetContext();
      }));
    }
  }

  mojo::Remote<device::mojom::UsbDeviceManager> usb_device_manager;
  if (!pass_empty_usb_device_manager_) {
    content::GetDeviceService().BindUsbDeviceManager(
        usb_device_manager.BindNewPipeAndPassReceiver());
  }
  discovery_factory->set_android_accessory_params(
      std::move(usb_device_manager),
      l10n_util::GetStringUTF8(IDS_WEBAUTHN_CABLEV2_AOA_REQUEST_DESCRIPTION));

  if (cable_extension_accepted || non_extension_cablev2_enabled) {
    std::optional<bool> extension_is_v2;
    if (cable_extension_provided) {
      extension_is_v2 = cablev2_extension_provided;
    }
    dialog_controller_->set_cable_transport_info(
        extension_is_v2, std::move(paired_phones),
        std::move(contact_phone_callback), qr_string);
    discovery_factory->set_cable_data(request_type, std::move(pairings),
                                      qr_generator_key);
  }

#if BUILDFLAG(IS_MAC)
  {
    content::WebContents* web_contents =
        content::WebContents::FromRenderFrameHost(GetRenderFrameHost());
    // Not all contexts in which this code runs have a BrowserWindow.
    // Notably the dialog containing a WebContents that is used for signing
    // into a new profile does not. Thus the NSWindow is fetched more directly.
    const views::Widget* widget = views::Widget::GetTopLevelWidgetForNativeView(
        web_contents->GetNativeView());
    if (widget) {
      const gfx::NativeWindow window = widget->GetNativeWindow();
      if (window) {
        discovery_factory->set_nswindow(
            reinterpret_cast<uintptr_t>(window.GetNativeNSWindow()));
      }
    }
  }
#endif

  if (enclave_controller_) {
    enclave_controller_->ConfigureDiscoveries(discovery_factory);
  }

  dialog_controller_->set_is_non_webauthn_request(
      request_source != RequestSource::kWebAuthentication);

#if BUILDFLAG(IS_MAC)
  ConfigureICloudKeychain(request_source, rp_id);
#endif
}

void ChromeAuthenticatorRequestDelegate::SetHints(
    const AuthenticatorRequestClientDelegate::Hints& hints) {
  if (g_observer) {
    g_observer->HintsSet(hints);
  }
  dialog_controller_->SetHints(hints);
}

void ChromeAuthenticatorRequestDelegate::SelectAccount(
    std::vector<device::AuthenticatorGetAssertionResponse> responses,
    base::OnceCallback<void(device::AuthenticatorGetAssertionResponse)>
        callback) {
  if (disable_ui_) {
    // Requests with UI disabled should never reach account selection.
    DCHECK(IsVirtualEnvironmentEnabled());

    // The browser is being automated. Select the first credential to support
    // automation of discoverable credentials.
    // TODO(crbug.com/40639383): Provide a way to determine which account gets
    // picked.
    std::move(callback).Run(std::move(responses.at(0)));
    return;
  }

  if (g_observer) {
    g_observer->AccountSelectorShown(responses);
    std::move(callback).Run(std::move(responses.at(0)));
    return;
  }

  dialog_controller_->SelectAccount(std::move(responses), std::move(callback));
}

void ChromeAuthenticatorRequestDelegate::DisableUI() {
  disable_ui_ = true;
}

bool ChromeAuthenticatorRequestDelegate::IsWebAuthnUIEnabled() {
  // The UI is fully disabled for the entire request duration if either:
  // 1) The UI was temporarily hidden, e.g. while showing the native Windows
  // WebAuthn UI. But in those cases the UI is still enabled and can be shown
  // e.g. for an attestation consent prompt.
  // 2) A specialized UI is replacing the default WebAuthn UI, such as Secure
  // Payment Confirmation or Autofill.
  return !disable_ui_;
}

void ChromeAuthenticatorRequestDelegate::SetConditionalRequest(
    bool is_conditional) {
  is_conditional_ = is_conditional;
}

void ChromeAuthenticatorRequestDelegate::SetCredentialIdFilter(
    std::vector<device::PublicKeyCredentialDescriptor> credential_list) {
  credential_filter_ = std::move(credential_list);
}

void ChromeAuthenticatorRequestDelegate::SetUserEntityForMakeCredentialRequest(
    const device::PublicKeyCredentialUserEntity& user_entity) {
  dialog_model_->user_entity = user_entity;
}

void ChromeAuthenticatorRequestDelegate::OnTransportAvailabilityEnumerated(
    device::FidoRequestHandlerBase::TransportAvailabilityInfo data) {
  if (disable_ui_) {
    return;
  }

  const bool delay_ui_for_gpm =
#if BUILDFLAG(IS_CHROMEOS)
      chromeos_passkey_controller_ &&
      !chromeos_passkey_controller_->ready_for_ui();
#else
      enclave_controller_ && !enclave_controller_->ready_for_ui();
#endif
  if (delay_ui_for_gpm) {
    // Delay showing UI until GPM state is loaded. It's only after this
    // point that we know whether GPM will be active for this request or not.
    pending_transport_availability_info_ = std::make_unique<
        device::FidoRequestHandlerBase::TransportAvailabilityInfo>(
        std::move(data));
    return;
  }

  ShowUI(std::move(data));
}

bool ChromeAuthenticatorRequestDelegate::EmbedderControlsAuthenticatorDispatch(
    const device::FidoAuthenticator& authenticator) {
  // Decide whether the //device/fido code should dispatch the current
  // request to an authenticator immediately after it has been
  // discovered, or whether the embedder/UI takes charge of that by
  // invoking its RequestCallback.
  if (!IsWebAuthnUIEnabled()) {
    // There is no UI to handle request dispatch.
    return false;
  }
  if (authenticator.GetType() == device::AuthenticatorType::kEnclave) {
    return false;
  }

  if (is_conditional_ &&
      (dialog_model_->step() ==
           AuthenticatorRequestDialogModel::Step::kConditionalMediation ||
       dialog_model_->step() ==
           AuthenticatorRequestDialogModel::Step::kNotStarted)) {
    // There is an active conditional request that is not showing any UI. The UI
    // will dispatch to any plugged in authenticators after the user selects an
    // option.
    return true;
  }
  auto transport = authenticator.AuthenticatorTransport();
  return !transport ||  // Windows
         *transport == device::FidoTransportProtocol::kInternal;
}

void ChromeAuthenticatorRequestDelegate::FidoAuthenticatorAdded(
    const device::FidoAuthenticator& authenticator) {
  if (!IsWebAuthnUIEnabled()) {
    return;
  }

  dialog_controller_->AddAuthenticator(authenticator);
}

void ChromeAuthenticatorRequestDelegate::FidoAuthenticatorRemoved(
    std::string_view authenticator_id) {
  if (!IsWebAuthnUIEnabled()) {
    return;
  }

  dialog_controller_->RemoveAuthenticator(authenticator_id);
}

void ChromeAuthenticatorRequestDelegate::BluetoothAdapterPowerChanged(
    bool is_powered_on) {
  dialog_controller_->BluetoothAdapterPowerChanged(is_powered_on);
}

bool ChromeAuthenticatorRequestDelegate::SupportsPIN() const {
  return true;
}

void ChromeAuthenticatorRequestDelegate::CollectPIN(
    CollectPINOptions options,
    base::OnceCallback<void(std::u16string)> provide_pin_cb) {
  dialog_controller_->CollectPIN(options.reason, options.error,
                                 options.min_pin_length, options.attempts,
                                 std::move(provide_pin_cb));
}

void ChromeAuthenticatorRequestDelegate::StartBioEnrollment(
    base::OnceClosure next_callback) {
  dialog_controller_->StartInlineBioEnrollment(std::move(next_callback));
}

void ChromeAuthenticatorRequestDelegate::OnSampleCollected(
    int bio_samples_remaining) {
  dialog_controller_->OnSampleCollected(bio_samples_remaining);
}

void ChromeAuthenticatorRequestDelegate::FinishCollectToken() {
  dialog_controller_->FinishCollectToken();
}

void ChromeAuthenticatorRequestDelegate::OnRetryUserVerification(int attempts) {
  dialog_controller_->OnRetryUserVerification(attempts);
}

void ChromeAuthenticatorRequestDelegate::OnStartOver() {
  DCHECK(start_over_callback_);
  start_over_callback_.Run();
}

void ChromeAuthenticatorRequestDelegate::OnModelDestroyed(
    AuthenticatorRequestDialogModel* model) {
  DCHECK_EQ(model, dialog_model_.get());
}

void ChromeAuthenticatorRequestDelegate::OnCancelRequest() {
  // |cancel_callback_| must be invoked at most once as invocation of
  // |cancel_callback_| will destroy |this|.
  DCHECK(cancel_callback_);
  std::move(cancel_callback_).Run();
}

void ChromeAuthenticatorRequestDelegate::OnManageDevicesClicked() {
  content::WebContents* web_contents =
      content::WebContents::FromRenderFrameHost(GetRenderFrameHost());
  Browser* browser = chrome::FindBrowserWithTab(web_contents);
  if (browser) {
    NavigateParams params(browser,
                          GURL("chrome://settings/securityKeys/phones"),
                          ui::PageTransition::PAGE_TRANSITION_AUTO_TOPLEVEL);
    params.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
    Navigate(&params);
  }
}

void ChromeAuthenticatorRequestDelegate::SetPassEmptyUsbDeviceManagerForTesting(
    bool value) {
  pass_empty_usb_device_manager_ = value;
}

void ChromeAuthenticatorRequestDelegate::SetTrustedVaultConnectionForTesting(
    std::unique_ptr<trusted_vault::TrustedVaultConnection> connection) {
  pending_trusted_vault_connection_ = std::move(connection);
}

void ChromeAuthenticatorRequestDelegate::SetClockForTesting(
    base::Clock* clock) {
  CHECK(!enclave_controller_);
  clock_ = clock;
}

content::RenderFrameHost*
ChromeAuthenticatorRequestDelegate::GetRenderFrameHost() const {
  content::RenderFrameHost* ret =
      content::RenderFrameHost::FromID(render_frame_host_id_);
  DCHECK(ret);
  return ret;
}

content::BrowserContext* ChromeAuthenticatorRequestDelegate::GetBrowserContext()
    const {
  return GetRenderFrameHost()->GetBrowserContext();
}

void ChromeAuthenticatorRequestDelegate::ShowUI(
    device::FidoRequestHandlerBase::TransportAvailabilityInfo tai) {
  if (base::FeatureList::IsEnabled(syncer::kSyncWebauthnCredentials) &&
      (can_use_synced_phone_passkeys_ ||
       (enclave_controller_ && enclave_controller_->is_active())
#if BUILDFLAG(IS_CHROMEOS)
       || chromeos_passkey_controller_
#endif
       )) {
    GetPhoneContactableGpmPasskeysForRpId(&tai.recognized_credentials);
  }
  FilterRecognizedCredentials(&tai);

  if (g_observer) {
    g_observer->OnTransportAvailabilityEnumerated(this, &tai);
  }

  if (dialog_model_->step() !=
      AuthenticatorRequestDialogModel::Step::kNotStarted) {
    dialog_controller_->OnTransportAvailabilityChanged(std::move(tai));
    return;
  }

  dialog_controller_->StartFlow(std::move(tai), is_conditional_);

  if (g_observer) {
    g_observer->UIShown(this);
  }
}

void ChromeAuthenticatorRequestDelegate::OnReadyForUI() {
  if (!pending_transport_availability_info_) {
    return;
  }

  auto pending_transport_availability_info =
      std::move(pending_transport_availability_info_);
  pending_transport_availability_info_.reset();

  ShowUI(std::move(*pending_transport_availability_info));
}

bool ChromeAuthenticatorRequestDelegate::ShouldPermitCableExtension(
    const url::Origin& origin) {
  if (base::FeatureList::IsEnabled(device::kWebAuthCableExtensionAnywhere)) {
    return true;
  }

  // Because the future of the caBLE extension might be that we transition
  // everything to QR-code or sync-based pairing, we don't want use of the
  // extension to spread without consideration. Therefore it's limited to
  // origins that are already depending on it and test sites.
  if (origin.DomainIs("google.com")) {
    return true;
  }

  const GURL test_site("https://webauthndemo.appspot.com");
  DCHECK(test_site.is_valid());
  return origin.IsSameOriginWith(test_site);
}

void ChromeAuthenticatorRequestDelegate::OnInvalidatedCablePairing(
    std::unique_ptr<device::cablev2::Pairing> failed_pairing) {
  PrefService* const prefs =
      Profile::FromBrowserContext(GetBrowserContext())->GetPrefs();

  // A pairing was reported to be invalid. Delete it unless it came from Sync,
  // in which case there's nothing to be done.
  cablev2::DeletePairingByPublicKey(prefs,
                                    failed_pairing->peer_public_key_x962);

  // Contact the next phone with the same name, if any, given that no
  // notification has been sent.
  dialog_controller_->OnPhoneContactFailed(failed_pairing->name);
}

void ChromeAuthenticatorRequestDelegate::OnCableEvent(
    device::cablev2::Event event) {
  if (event == device::cablev2::Event::kReady) {
    cable_device_ready_ = true;
  }

  dialog_controller_->OnCableEvent(event);
}

void ChromeAuthenticatorRequestDelegate::GetPhoneContactableGpmPasskeysForRpId(
    std::vector<device::DiscoverableCredentialMetadata>* passkeys) {
  device::AuthenticatorType type;
  std::vector<sync_pb::WebauthnCredentialSpecifics> credentials;

#if BUILDFLAG(IS_CHROMEOS)
  if (chromeos_passkey_controller_) {
    credentials = chromeos_passkey_controller_->credentials();
    type = device::AuthenticatorType::kChromeOSPasskeys;
#else
  int enclave_bootstrap_limit_reached =
      Profile::FromBrowserContext(GetBrowserContext())
          ->GetOriginalProfile()
          ->GetPrefs()
          ->GetInteger(
              webauthn::pref_names::kEnclaveDeclinedGPMBootstrappingCount) >=
      device::enclave::kMaxGPMBootstrapPrompts;
  FIDO_LOG(EVENT) << "b/342399396: considering whether GPM credentials are "
                     "enclave; have controller: "
                  << static_cast<bool>(enclave_controller_)
                  << " bootstrap limit: " << enclave_bootstrap_limit_reached;
  if (enclave_controller_ && !enclave_bootstrap_limit_reached &&
      enclave_controller_->is_active()) {
    credentials = enclave_controller_->creds();
    type = device::AuthenticatorType::kEnclave;
#endif
  } else {
    webauthn::PasskeyModel* passkey_model =
        PasskeyModelFactory::GetInstance()->GetForProfile(
            Profile::FromBrowserContext(GetBrowserContext()));
    CHECK(passkey_model);
    credentials = passkey_model->GetPasskeysForRelyingPartyId(
        dialog_model_->relying_party_id);
    type = device::AuthenticatorType::kPhone;
  }

  for (const sync_pb::WebauthnCredentialSpecifics& passkey : credentials) {
    passkeys->emplace_back(
        type, passkey.rp_id(),
        std::vector<uint8_t>(passkey.credential_id().begin(),
                             passkey.credential_id().end()),
        device::PublicKeyCredentialUserEntity(
            std::vector<uint8_t>(passkey.user_id().begin(),
                                 passkey.user_id().end()),
            passkey.user_name(), passkey.user_display_name()));
  }
}

void ChromeAuthenticatorRequestDelegate::FilterRecognizedCredentials(
    device::FidoRequestHandlerBase::TransportAvailabilityInfo* tai) {
  if (base::FeatureList::IsEnabled(device::kWebAuthnFilterGooglePasskeys) &&
      dialog_model()->relying_party_id == kGoogleRpId &&
      base::ranges::any_of(tai->recognized_credentials,
                           IsCredentialFromPlatformAuthenticator)) {
    // Regrettably, Chrome will create webauthn credentials for things other
    // than authentication (e.g. credit card autofill auth) under the rp id
    // "google.com". To differentiate those credentials from actual passkeys you
    // can use to sign in, Google adds a prefix to the user id.
    // This code filter passkeys that do not match that prefix.
    FilterGoogleAuthPasskeys(&tai->recognized_credentials);
    if (tai->has_platform_authenticator_credential ==
            device::FidoRequestHandlerBase::RecognizedCredential::
                kHasRecognizedCredential &&
        base::ranges::none_of(tai->recognized_credentials,
                              IsCredentialFromPlatformAuthenticator)) {
      tai->has_platform_authenticator_credential = device::
          FidoRequestHandlerBase::RecognizedCredential::kNoRecognizedCredential;
    }
  }

  if (!credential_filter_.empty()) {
    std::vector<device::DiscoverableCredentialMetadata> filtered_list;
    for (auto& platform_credential : tai->recognized_credentials) {
      for (auto& filter_credential : credential_filter_) {
        if (platform_credential.cred_id == filter_credential.id) {
          filtered_list.push_back(platform_credential);
          break;
        }
      }
    }
    tai->recognized_credentials = std::move(filtered_list);
  }
}

#if BUILDFLAG(IS_MAC)
// static
std::optional<int> ChromeAuthenticatorRequestDelegate::DaysSinceDate(
    const std::string& formatted_date,
    const base::Time now) {
  int year, month, day_of_month;
  // sscanf will ignore trailing garbage, but we don't need to be strict here.
  if (sscanf(formatted_date.c_str(), "%u-%u-%u", &year, &month,
             &day_of_month) != 3) {
    return std::nullopt;
  }

  const base::Time::Exploded exploded = {
      .year = year, .month = month, .day_of_month = day_of_month};

  base::Time t;
  if (!base::Time::FromUTCExploded(exploded, &t) || now < t) {
    return std::nullopt;
  }

  const base::TimeDelta difference = now - t;
  return difference.InDays();
}

// static
std::optional<bool> ChromeAuthenticatorRequestDelegate::GetICloudKeychainPref(
    const PrefService* prefs) {
  const PrefService::Preference* pref =
      prefs->FindPreference(prefs::kCreatePasskeysInICloudKeychain);
  if (pref->IsDefaultValue()) {
    return std::nullopt;
  }
  return pref->GetValue()->GetBool();
}

// static
bool ChromeAuthenticatorRequestDelegate::IsActiveProfileAuthenticatorUser(
    const PrefService* prefs) {
  const std::string& last_used = prefs->GetString(kWebAuthnTouchIdLastUsed);
  if (last_used.empty()) {
    return false;
  }
  const std::optional<int> days = DaysSinceDate(last_used, base::Time::Now());
  return days.has_value() && days.value() <= kMacOsRecentlyUsedMaxDays;
}

// static
bool ChromeAuthenticatorRequestDelegate::ShouldCreateInICloudKeychain(
    RequestSource request_source,
    bool is_active_profile_authenticator_user,
    bool has_icloud_drive_enabled,
    bool request_is_for_google_com,
    std::optional<bool> preference) {
  // Secure Payment Confirmation and credit-card autofill continue to use
  // the profile authenticator.
  if (request_source != RequestSource::kWebAuthentication) {
    return false;
  }
  if (preference.has_value()) {
    return *preference;
  }
  const base::Feature* feature;
  if (request_is_for_google_com) {
    feature = &device::kWebAuthnICloudKeychainForGoogle;
  } else {
    if (is_active_profile_authenticator_user) {
      if (has_icloud_drive_enabled) {
        feature = &device::kWebAuthnICloudKeychainForActiveWithDrive;
      } else {
        feature = &device::kWebAuthnICloudKeychainForActiveWithoutDrive;
      }
    } else {
      if (has_icloud_drive_enabled) {
        feature = &device::kWebAuthnICloudKeychainForInactiveWithDrive;
      } else {
        feature = &device::kWebAuthnICloudKeychainForInactiveWithoutDrive;
      }
    }
  }

  return base::FeatureList::IsEnabled(*feature);
}

void ChromeAuthenticatorRequestDelegate::ConfigureICloudKeychain(
    RequestSource request_source,
    const std::string& rp_id) {
  const PrefService* prefs =
      Profile::FromBrowserContext(GetBrowserContext())->GetPrefs();
  const bool is_icloud_drive_enabled = IsICloudDriveEnabled();
  const bool is_active_profile_authenticator_user =
      IsActiveProfileAuthenticatorUser(prefs);
  dialog_controller_->set_allow_icloud_keychain(
      request_source == RequestSource::kWebAuthentication);
  dialog_controller_->set_has_icloud_drive_enabled(is_icloud_drive_enabled);
  dialog_controller_->set_is_active_profile_authenticator_user(
      is_active_profile_authenticator_user);
  dialog_controller_->set_should_create_in_icloud_keychain(
      ShouldCreateInICloudKeychain(
          request_source, is_active_profile_authenticator_user,
          is_icloud_drive_enabled, rp_id == "google.com",
          GetICloudKeychainPref(prefs)));
}

#endif
