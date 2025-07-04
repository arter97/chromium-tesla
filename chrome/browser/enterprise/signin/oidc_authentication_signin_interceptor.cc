// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/enterprise/signin/oidc_authentication_signin_interceptor.h"

#include <string>

#include "base/check.h"
#include "base/functional/callback.h"
#include "base/logging.h"
#include "base/strings/utf_string_conversions.h"
#include "base/uuid.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/enterprise/identifiers/profile_id_delegate_impl.h"
#include "chrome/browser/enterprise/identifiers/profile_id_service_factory.h"
#include "chrome/browser/enterprise/profile_management/profile_management_features.h"
#include "chrome/browser/enterprise/signin/enterprise_signin_prefs.h"
#include "chrome/browser/enterprise/signin/oidc_authentication_signin_interceptor_factory.h"
#include "chrome/browser/enterprise/signin/oidc_managed_profile_creation_delegate.h"
#include "chrome/browser/enterprise/signin/oidc_metrics_utils.h"
#include "chrome/browser/enterprise/signin/user_policy_oidc_signin_service.h"
#include "chrome/browser/enterprise/signin/user_policy_oidc_signin_service_factory.h"
#include "chrome/browser/enterprise/util/managed_browser_utils.h"
#include "chrome/browser/new_tab_page/chrome_colors/selected_colors_info.h"
#include "chrome/browser/policy/chrome_browser_policy_connector.h"
#include "chrome/browser/profiles/profile_attributes_entry.h"
#include "chrome/browser/profiles/profile_attributes_storage.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/profiles/profiles_state.h"
#include "chrome/browser/signin/identity_manager_factory.h"
#include "chrome/browser/signin/signin_util.h"
#include "chrome/browser/themes/theme_service.h"
#include "chrome/browser/themes/theme_service_factory.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/browser_navigator.h"
#include "chrome/browser/ui/browser_navigator_params.h"
#include "chrome/browser/ui/profiles/profile_colors_util.h"
#include "chrome/browser/ui/signin/signin_view_controller.h"
#include "chrome/common/themes/autogenerated_theme_util.h"
#include "chrome/common/webui_url_constants.h"
#include "components/device_signals/core/browser/pref_names.h"
#include "components/enterprise/browser/controller/browser_dm_token_storage.h"
#include "components/enterprise/browser/identifiers/identifiers_prefs.h"
#include "components/enterprise/browser/identifiers/profile_id_service.h"
#include "components/policy/core/browser/browser_policy_connector.h"
#include "components/policy/core/common/cloud/cloud_policy_client_registration_helper.h"
#include "components/policy/core/common/cloud/user_cloud_policy_manager.h"
#include "components/policy/core/common/policy_logger.h"
#include "components/prefs/pref_service.h"
#include "components/signin/public/identity_manager/primary_account_mutator.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/web_contents.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/mojom/themes.mojom.h"

using profile_management::features::kOidcAuthIsDasherBased;
using profile_management::features::kOidcAuthStubClientId;
using profile_management::features::kOidcAuthStubDmToken;
using profile_management::features::kOidcAuthStubUserEmail;
using profile_management::features::kOidcAuthStubUserName;

using enterprise::ProfileIdServiceFactory;

namespace {

constexpr char kUniqueIdentifierTemplate[] = "iss:%s,sub:%s";

bool IsValidOidcToken(ProfileManagementOicdTokens oidc_tokens) {
  return !oidc_tokens.auth_token.empty() && !oidc_tokens.id_token.empty();
}

}  // namespace

OidcAuthenticationSigninInterceptor::OidcAuthenticationSigninInterceptor(
    Profile* profile,
    std::unique_ptr<WebSigninInterceptor::Delegate> delegate)
    : profile_(profile), delegate_(std::move(delegate)) {
  CHECK(profile_);
  CHECK(delegate_);
}

OidcAuthenticationSigninInterceptor::~OidcAuthenticationSigninInterceptor() =
    default;

void OidcAuthenticationSigninInterceptor::MaybeInterceptOidcAuthentication(
    content::WebContents* intercepted_contents,
    ProfileManagementOicdTokens oidc_tokens,
    std::string issuer_id,
    std::string subject_id,
    OidcInterceptionCallback oidc_callback) {
  RecordOidcInterceptionFunnelStep(
      OidcInterceptionFunnelStep::kEnrollmentStarted);

  // The interceptor class should be able to be used if no other interception is
  // in progress. This includes both when a previous interception succeeded and
  // failed: in former case, user may want to re-use the class to register
  // another profile with a different identity; in the latter case, user may
  // want to retry the process or a different identity.
  if (interception_in_progress_) {
    VLOG_POLICY(1, OIDC_ENROLLMENT) << "OIDC Interception already in progress";
    RecordOidcInterceptionResult(
        OidcInterceptionResult::kInterceptionInProgress);
    return;
  }

  interception_in_progress_ = true;
  oidc_callback_ = std::move(oidc_callback);

  if (!IsValidOidcToken(oidc_tokens)) {
    LOG_POLICY(ERROR, OIDC_ENROLLMENT) << "Invalid tokens in the OIDC response";
    return;
  }

  if (!intercepted_contents) {
    LOG_POLICY(ERROR, OIDC_ENROLLMENT)
        << "Web contents no longer available, aborting interception";
    return;
  }

  web_contents_ = intercepted_contents->GetWeakPtr();
  oidc_tokens_ = oidc_tokens;
  unique_user_identifier_ = base::StringPrintf(
      kUniqueIdentifierTemplate, issuer_id.c_str(), subject_id.c_str());

  CHECK(!switch_to_entry_);
  base::FilePath profile_path = profile_->GetPath();
  for (const auto* entry : g_browser_process->profile_manager()
                               ->GetProfileAttributesStorage()
                               .GetAllProfilesAttributes()) {
    if (!entry->GetProfileManagementOidcTokens().auth_token.empty() &&
        entry->GetProfileManagementId() == unique_user_identifier_) {
      switch_to_entry_ = entry;
      break;
    }
  }

  // Same profile
  if (switch_to_entry_ && switch_to_entry_->GetPath() == profile_path) {
    RecordOidcInterceptionResult(
        OidcInterceptionResult::kNoInterceptForCurrentProfile);
    VLOG_POLICY(1, OIDC_ENROLLMENT)
        << "Intercepted info is already in the right profile";
    Reset();
    return;
  }

  switch_to_entry_
      ? VLOG_POLICY(2, OIDC_ENROLLMENT)
            << "Existing OIDC profile with match credential found, displaying "
               "profile switch dialog."
      : VLOG_POLICY(2, OIDC_ENROLLMENT)
            << "Displaying unified consent dialog for profile creation.";

  ProfileAttributesEntry* entry =
      g_browser_process->profile_manager()
          ->GetProfileAttributesStorage()
          .GetProfileAttributesWithPath(profile_->GetPath());
  profile_color_ = GenerateNewProfileColor(entry).color;

  auto* identity_manager = IdentityManagerFactory::GetForProfile(profile_);
  WebSigninInterceptor::SigninInterceptionType interception_type =
      switch_to_entry_
          ? WebSigninInterceptor::SigninInterceptionType::kProfileSwitchForced
          : WebSigninInterceptor::SigninInterceptionType::kEnterpriseOIDC;
  WebSigninInterceptor::Delegate::BubbleParameters bubble_parameters(
      interception_type, AccountInfo(),
      identity_manager->FindExtendedAccountInfoByAccountId(
          identity_manager->GetPrimaryAccountId(signin::ConsentLevel::kSignin)),
      GetAutogeneratedThemeColors(profile_color_).frame_color,
      /*show_link_data_option=*/false, /*show_managed_disclaimer=*/true);
  RecordOidcInterceptionFunnelStep(
      OidcInterceptionFunnelStep::kConsetDialogShown);

  interception_bubble_handle_ = delegate_->ShowSigninInterceptionBubble(
      web_contents_.get(), bubble_parameters,
      base::BindOnce(
          &OidcAuthenticationSigninInterceptor::OnProfileCreationChoice,
          base::Unretained(this)));
}

void OidcAuthenticationSigninInterceptor::Shutdown() {
  Reset();
}

void OidcAuthenticationSigninInterceptor::Reset() {
  if (oidc_callback_) {
    std::move(oidc_callback_).Run();
  }

  web_contents_ = nullptr;
  oidc_tokens_ = {};
  dm_token_.clear();
  client_id_.clear();
  user_display_name_.clear();
  user_email_.clear();
  unique_user_identifier_.clear();
  dasher_based_ = true;
  switch_to_entry_ = nullptr;
  profile_creator_.reset();
  profile_color_ = SkColor();
  registration_helper_for_temporary_client_.reset();
  interception_bubble_handle_.reset();
  interception_in_progress_ = false;
  client_for_testing_ = nullptr;
  preset_profile_id_.clear();
}

void OidcAuthenticationSigninInterceptor::StartOidcRegistration() {
  std::string preset_profile_guid =
      base::Uuid::GenerateRandomV4().AsLowercaseString();

  auto device_id = enterprise::ProfileIdDelegateImpl::GetId();
  // We are supplying the GUID and only using current profile's ID service to
  // calculate the new profile ID. Since the two profiles share the same device,
  // the device id should be the same.
  std::optional<std::string> preset_profile_id =
      ProfileIdServiceFactory::GetForProfile(profile_)
          ->GetProfileIdWithGuidAndDeviceId(preset_profile_guid, device_id);

  if (preset_profile_id == std::nullopt || preset_profile_id.value().empty()) {
    LOG_POLICY(ERROR, OIDC_ENROLLMENT)
        << "Failed to create a preset profile ID for the new profile";

    Reset();
    return;
  }
  preset_profile_id_ = preset_profile_id.value();

  VLOG_POLICY(2, OIDC_ENROLLMENT)
      << "Starting OIDC registration process for profile "
      << preset_profile_id_;

  VLOG_POLICY(2, OIDC_ENROLLMENT) << "Starting OIDC registration process";
  RecordOidcInterceptionFunnelStep(
      OidcInterceptionFunnelStep::kProfileRegistrationStarted);
  const base::TimeTicks registration_start_time = base::TimeTicks::Now();

  policy::DeviceManagementService* device_management_service =
      g_browser_process->browser_policy_connector()
          ->device_management_service();

  // If the DeviceManagementService is not yet initialized, start it up now.
  // Skip this for testing client.
  if (!client_for_testing_) {
    device_management_service->ScheduleInitialization(0);
  }

  auto client = client_for_testing_
                    ? std::move(client_for_testing_)
                    : std::make_unique<CloudPolicyClient>(
                          preset_profile_id_, device_management_service,
                          g_browser_process->shared_url_loader_factory(),
                          CloudPolicyClient::DeviceDMTokenCallback());

  registration_helper_for_temporary_client_ =
      std::make_unique<policy::CloudPolicyClientRegistrationHelper>(
          client.get(), enterprise_management::DeviceRegisterRequest::BROWSER);

  // Using a raw pointer to |this| is okay, because the service owns
  // |registration_helper_for_temporary_client_|.
  auto registration_callback =
      base::BindOnce(&OidcAuthenticationSigninInterceptor::OnClientRegistered,
                     base::Unretained(this), std::move(client),
                     preset_profile_guid, registration_start_time);

  registration_helper_for_temporary_client_->StartRegistrationWithOidcTokens(
      oidc_tokens_.auth_token, oidc_tokens_.id_token, std::string(),
      std::move(registration_callback));
}

void OidcAuthenticationSigninInterceptor::OnClientRegistered(
    std::unique_ptr<CloudPolicyClient> client,
    std::string preset_profile_guid,
    base::TimeTicks registration_start_time) {
  if (client->last_dm_status() != policy::DM_STATUS_SUCCESS) {
    RecordOidcInterceptionResult(
        OidcInterceptionResult::kFailedToRegisterProfile);
    RecordOidcEnrollmentRegistrationLatency(
        std::nullopt, /*success=*/false,
        base::TimeTicks::Now() - registration_start_time);
    LOG_POLICY(ERROR, OIDC_ENROLLMENT)
        << "OIDC client registration failed with DM Status: "
        << client->last_dm_status() << ". Enrollment process interrupted.";

    Reset();
    return;
  }

  if (kOidcAuthStubDmToken.Get().empty()) {
    CHECK(client);
  }

  dm_token_ = kOidcAuthStubDmToken.Get().empty() ? client->dm_token()
                                                 : kOidcAuthStubDmToken.Get();
  client_id_ = kOidcAuthStubClientId.Get().empty()
                   ? client->client_id()
                   : kOidcAuthStubClientId.Get();
  user_display_name_ = kOidcAuthStubUserName.Get().empty()
                           ? client->oidc_user_display_name()
                           : kOidcAuthStubUserName.Get();
  user_email_ = kOidcAuthStubUserEmail.Get().empty()
                    ? client->oidc_user_email()
                    : kOidcAuthStubUserEmail.Get();

  bool is_dasherless_client =
      client->third_party_identity_type() ==
      policy::ThirdPartyIdentityType::OIDC_MANAGEMENT_DASHERLESS;

  VLOG_POLICY(2, OIDC_ENROLLMENT)
      << "OIDC client registration succeeded for third party identity type: "
      << client->third_party_identity_type()
      << ". Starting profile creation process.";

  // TODO(b/328055055): Replace this somewhat confusing check when bool
  // IsDasherlessManagement is replaced with an Enum.
  dasher_based_ = !kOidcAuthIsDasherBased.Get() ? kOidcAuthIsDasherBased.Get()
                                                : !is_dasherless_client;

  RecordOidcEnrollmentRegistrationLatency(
      dasher_based_, /*success=*/true,
      base::TimeTicks::Now() - registration_start_time);
  CHECK(!dm_token_.empty());

  // Unretained is fine because the profile creator is owned by this.
  profile_creator_ = std::make_unique<ManagedProfileCreator>(
      profile_, unique_user_identifier_,
      (user_display_name_.empty())
          ? profiles::GetDefaultNameForNewEnterpriseProfile()
          : base::UTF8ToUTF16(user_display_name_),
      std::make_unique<OidcManagedProfileCreationDelegate>(
          oidc_tokens_.auth_token, oidc_tokens_.id_token, dasher_based_,
          user_display_name_, user_email_),
      base::BindOnce(
          &OidcAuthenticationSigninInterceptor::OnNewSignedInProfileCreated,
          base::Unretained(this)),
      preset_profile_guid);
}

void OidcAuthenticationSigninInterceptor::OnProfileCreationChoice(
    SigninInterceptionResult create) {
  if (create != SigninInterceptionResult::kAccepted) {
    RecordOidcInterceptionResult(OidcInterceptionResult::kConsetDialogRejected);
    if (switch_to_entry_) {
      VLOG_POLICY(2, OIDC_ENROLLMENT) << "Profile switch refused by the user";
    } else {
      VLOG_POLICY(2, OIDC_ENROLLMENT) << "Profile creation refused by the user";
    }

    Reset();
    return;
  }

  CHECK(!profile_creator_);
  if (switch_to_entry_) {
    // Unretained is fine because the profile creator is owned by this.
    profile_creator_ = std::make_unique<ManagedProfileCreator>(
        profile_, switch_to_entry_->GetPath(),
        std::make_unique<OidcManagedProfileCreationDelegate>(),
        base::BindOnce(
            &OidcAuthenticationSigninInterceptor::OnNewSignedInProfileCreated,
            base::Unretained(this)));
  } else {
    kOidcAuthStubDmToken.Get().empty()
        ? StartOidcRegistration()
        : OnClientRegistered(nullptr, std::string(), base::TimeTicks::Now());
  }
}

void OidcAuthenticationSigninInterceptor::OnNewSignedInProfileCreated(
    base::WeakPtr<Profile> new_profile) {
  CHECK(profile_creator_);

  if (!new_profile) {
    RecordOidcProfileCreationResult(
        OidcProfileCreationResult::kFailedToCreateProfile, dasher_based_);
    LOG_POLICY(ERROR, OIDC_ENROLLMENT) << "Failed to create new profile";
    Reset();
    return;
  }

  if (switch_to_entry_) {
    ProfileAttributesEntry* new_profile_entry =
        g_browser_process->profile_manager()
            ->GetProfileAttributesStorage()
            .GetProfileAttributesWithPath(new_profile->GetPath());

    CHECK(new_profile_entry);

    // TODO(b/328055055): Replace this confusing check when bool
    // IsDasherlessManagement is replaced with an Enum.
    dasher_based_ = !new_profile_entry->IsDasherlessManagement();
  }

  RecordOidcProfileCreationFunnelStep(
      OidcProfileCreationFunnelStep::kProfileCreated, dasher_based_);
  new_profile->GetPrefs()->SetBoolean(
      device_signals::prefs::kDeviceSignalsPermanentConsentReceived, true);

  if (!switch_to_entry_) {
    VLOG_POLICY(2, OIDC_ENROLLMENT)
        << "Created new OIDC-managed profile with preset profile ID: "
        << preset_profile_id_;
    std::optional<std::string> new_profile_id =
        ProfileIdServiceFactory::GetForProfile(new_profile.get())
            ->GetProfileId();
    if (new_profile_id == std::nullopt) {
      LOG_POLICY(ERROR, OIDC_ENROLLMENT)
          << "Failed to retrieve profile ID for the new OIDC-managed profile";
      RecordOidcProfileCreationResult(
          OidcProfileCreationResult::kMismatchingProfileId, dasher_based_);
    } else if (new_profile_id.value() != preset_profile_id_) {
      LOG_POLICY(ERROR, OIDC_ENROLLMENT)
          << "New profile's ID " << new_profile_id.value()
          << " mismatches with the preset value";
      RecordOidcProfileCreationResult(
          OidcProfileCreationResult::kMismatchingProfileId, dasher_based_);
    }

    // Generate a color theme for new profiles
    ThemeServiceFactory::GetForProfile(new_profile.get())
        ->SetUserColorAndBrowserColorVariant(
            profile_color_, ui::mojom::BrowserColorVariant::kTonalSpot);

    // Create a backup copy of the dm token/client ID in case of losing cached
    // policies.
    new_profile->GetPrefs()->SetString(
        enterprise_signin::prefs::kPolicyRecoveryToken, dm_token_);
    new_profile->GetPrefs()->SetString(
        enterprise_signin::prefs::kPolicyRecoveryClientId, client_id_);
  } else {
    VLOG_POLICY(2, OIDC_ENROLLMENT) << "Profile switched sucessfully";
  }

  policy::UserPolicyOidcSigninService* policy_service =
      policy::UserPolicyOidcSigninServiceFactory::GetForProfile(
          new_profile.get());

  VLOG_POLICY(2, OIDC_ENROLLMENT)
      << "Starting policy fetch process for OIDC-managed profile";
  RecordOidcProfileCreationFunnelStep(
      OidcProfileCreationFunnelStep::kPolicyFetchStarted, dasher_based_);

  policy_service->FetchPolicyForSignedInUser(
      AccountId(), dm_token_, client_id_,
      /*user_affiliation_ids=*/std::vector<std::string>(),
      new_profile.get()
          ->GetDefaultStoragePartition()
          ->GetURLLoaderFactoryForBrowserProcess(),
      base::BindOnce(&OidcAuthenticationSigninInterceptor::
                         OnPolicyFetchCompleteInNewProfile,
                     weak_factory_.GetWeakPtr(), new_profile.get(),
                     base::TimeTicks::Now()));
}

void OidcAuthenticationSigninInterceptor::
    CreateBrowserAfterSigninInterception() {
  GURL url_to_open = GURL(chrome::kChromeUINewTabURL);

  // Open a new browser.
  NavigateParams params(profile_, url_to_open,
                        ui::PAGE_TRANSITION_AUTO_BOOKMARK);
  Navigate(&params);
  VLOG_POLICY(2, OIDC_ENROLLMENT) << "New browser created";
}

void OidcAuthenticationSigninInterceptor::OnPolicyFetchCompleteInNewProfile(
    Profile* new_profile,
    base::TimeTicks policy_fetch_start_time,
    bool success) {
  RecordOidcEnrollmentPolicyFetchLatency(
      dasher_based_, success, base::TimeTicks::Now() - policy_fetch_start_time);
  if (success) {
    VLOG_POLICY(2, OIDC_ENROLLMENT) << "Policy fetched for OIDC profile.";
  }

  if (success && dasher_based_ && !switch_to_entry_) {
    VLOG_POLICY(2, OIDC_ENROLLMENT)
        << "Policy fetched for Dasher-based OIDC profile, adding the user as "
           "the primary account.";
    RecordOidcProfileCreationFunnelStep(
        OidcProfileCreationFunnelStep::kAddingPrimaryAccount, dasher_based_);

    // User account management would be included in unified consent dialog.
    chrome::enterprise_util::SetUserAcceptedAccountManagement(new_profile,
                                                              true);

    policy::CloudPolicyManager* user_policy_manager =
        new_profile->GetUserCloudPolicyManager();

    std::string gaia_id =
        user_policy_manager->core()->store()->policy()->gaia_id();

    VLOG_POLICY(2, OIDC_ENROLLMENT) << "GAIA ID retrieved from user policy for "
                                    << user_email_ << ": " << gaia_id << ".";
    auto set_primary_account_result =
        signin_util::SetPrimaryAccountWithInvalidToken(
            new_profile, user_email_, gaia_id,
            /*is_under_advanced_protection=*/false,
            signin_metrics::AccessPoint::
                ACCESS_POINT_OIDC_REDIRECTION_INTERCEPTION,
            signin_metrics::SourceForRefreshTokenOperation::
                kMachineLogon_CredentialProvider);

    VLOG_POLICY(2, OIDC_ENROLLMENT)
        << "Operation of setting account id " << gaia_id
        << " received the following result: "
        << static_cast<int>(set_primary_account_result);

    RecordOidcProfileCreationResult(
        (set_primary_account_result ==
         signin::PrimaryAccountMutator::PrimaryAccountError::kNoError)
            ? OidcProfileCreationResult::kEnrollmentSucceeded
            : OidcProfileCreationResult::kFailedToAddPrimaryAccount,
        dasher_based_);

  } else {
    RecordOidcProfileCreationResult(
        (success) ? ((switch_to_entry_)
                         ? OidcProfileCreationResult::kSwitchedToExistingProfile
                         : OidcProfileCreationResult::kEnrollmentSucceeded)
                  : OidcProfileCreationResult::kFailedToFetchPolicy,
        dasher_based_);
  }

  // Work is done in this profile, creating a new browser window/tab for the
  // new/existing profile with chrome://newtab/, using the new profile's
  // interceptor. We can create the window regardless of policy fetch and
  // primary account setting succeeds or not.
  OidcAuthenticationSigninInterceptorFactory::GetForProfile(new_profile)
      ->CreateBrowserAfterSigninInterception();

  Reset();
}
