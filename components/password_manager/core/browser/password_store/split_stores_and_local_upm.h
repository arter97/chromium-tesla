// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_PASSWORD_MANAGER_CORE_BROWSER_PASSWORD_STORE_SPLIT_STORES_AND_LOCAL_UPM_H_
#define COMPONENTS_PASSWORD_MANAGER_CORE_BROWSER_PASSWORD_STORE_SPLIT_STORES_AND_LOCAL_UPM_H_

#include <memory>

class PrefService;

namespace syncer {
class SyncService;
}  // namespace syncer

namespace password_manager {

//  Checks whether the UPM for local users is activated for this client.
//  This also means that the single password store has been split in
//  account and local stores.
bool UsesSplitStoresAndUPMForLocal(const PrefService* pref_service);

// Returns whether it is a requirement to update the GMSCore based on the
// GMSCore version, whether syncing is enabled and whether the user is enrolled
// into the GMSCore.
// - If the GMSCore version is pre-UPM, update is always required.
// - If the GMSCore version supports the account store, but doesn't support the
// local store, the result depends on whether the user is syncing and enrolled
// into UPM.
// - If the GMSCore version supports both the account and local stores, the
// update is never required.
bool IsGmsCoreUpdateRequired(const PrefService* pref_service,
                             const syncer::SyncService* sync_service,
                             const std::string& gms_version_str);

}  // namespace password_manager

#endif  // COMPONENTS_PASSWORD_MANAGER_CORE_BROWSER_PASSWORD_STORE_SPLIT_STORES_AND_LOCAL_UPM_H_
