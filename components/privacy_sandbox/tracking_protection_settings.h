// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_PRIVACY_SANDBOX_TRACKING_PROTECTION_SETTINGS_H_
#define COMPONENTS_PRIVACY_SANDBOX_TRACKING_PROTECTION_SETTINGS_H_

#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "base/scoped_observation.h"
#include "components/content_settings/core/browser/host_content_settings_map.h"
#include "components/keyed_service/core/keyed_service.h"
#include "components/privacy_sandbox/tracking_protection_onboarding.h"
#include "components/privacy_sandbox/tracking_protection_prefs.h"

class HostContentSettingsMap;
class PrefService;

namespace privacy_sandbox {

class TrackingProtectionSettingsObserver;

// A service which provides an interface for observing and reading tracking
// protection settings.
class TrackingProtectionSettings
    : public TrackingProtectionOnboarding::Observer,
      public KeyedService {
 public:
  explicit TrackingProtectionSettings(
      PrefService* pref_service,
      HostContentSettingsMap* host_content_settings_map,
      TrackingProtectionOnboarding* onboarding_service,
      bool is_incognito);
  ~TrackingProtectionSettings() override;

  // KeyedService:
  void Shutdown() override;

  virtual void AddObserver(TrackingProtectionSettingsObserver* observer);
  virtual void RemoveObserver(TrackingProtectionSettingsObserver* observer);

  // Returns whether "do not track" is enabled.
  bool IsDoNotTrackEnabled() const;

  // Returns whether tracking protection for 3PCD (prefs + UX) is enabled.
  bool IsTrackingProtection3pcdEnabled() const;

  // Returns whether tracking protection 3PCD is enabled and all 3PC are blocked
  // (i.e. without mitigations).
  bool AreAllThirdPartyCookiesBlocked() const;

  // Returns whether 3PCs are allowed in spite of 3PCD due to an enterprise
  // policy.
  bool AreThirdPartyCookiesAllowedByEnterprise() const;

  // Returns whether anti-fingerprinting is enabled.
  bool IsFingerprintingProtectionEnabled() const;

  // Returns whether IP protection is enabled.
  bool IsIpProtectionEnabled() const;

  // From TrackingProtectionOnboarding::Observer
  void OnTrackingProtectionOnboardingUpdated(
      TrackingProtectionOnboarding::OnboardingStatus onboarding_status)
      override;

  // Adds a Tracking Protection site-scoped (wildcarded) exception for a given
  // url. `is_user_bypass_exception` should be true if the exception was set via
  // user bypass and will therefore be temporary.
  void AddTrackingProtectionException(const GURL& first_party_url,
                                      bool is_user_bypass_exception = false);

  // Removes a Tracking Protection exception for a given url.
  // This removes both site-scoped (wildcarded) and origin-scoped exceptions.
  void RemoveTrackingProtectionException(const GURL& first_party_url);

  // Returns the tracking protection setting for `first_party_url`. This will be
  // BLOCK unless the user has made an explicit exception for `first_party_url`.
  ContentSetting GetTrackingProtectionSetting(
      const GURL& first_party_url,
      content_settings::SettingInfo* info = nullptr) const;

 private:
  void OnEnterpriseControlForPrefsChanged();
  void MaybeInitializeIppPref();

  // Callbacks for pref observation.
  void OnDoNotTrackEnabledPrefChanged();
  void OnBlockAllThirdPartyCookiesPrefChanged();
  void OnTrackingProtection3pcdPrefChanged();
  void OnIpProtectionPrefChanged();
  void OnFingerprintingProtectionPrefChanged();

  base::ObserverList<TrackingProtectionSettingsObserver>::Unchecked observers_;
  PrefChangeRegistrar pref_change_registrar_;
  raw_ptr<PrefService> pref_service_;
  raw_ptr<HostContentSettingsMap> host_content_settings_map_;
  raw_ptr<TrackingProtectionOnboarding> onboarding_service_;
  bool is_incognito_;

  base::ScopedObservation<TrackingProtectionOnboarding,
                          TrackingProtectionOnboarding::Observer>
      onboarding_observation_{this};
};

}  // namespace privacy_sandbox

#endif  // COMPONENTS_PRIVACY_SANDBOX_TRACKING_PROTECTION_SETTINGS_H_
