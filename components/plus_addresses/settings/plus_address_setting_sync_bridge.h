// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_PLUS_ADDRESSES_SETTINGS_PLUS_ADDRESS_SETTING_SYNC_BRIDGE_H_
#define COMPONENTS_PLUS_ADDRESSES_SETTINGS_PLUS_ADDRESS_SETTING_SYNC_BRIDGE_H_

#include <memory>

#include "base/containers/flat_map.h"
#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"
#include "components/sync/model/model_type_store.h"
#include "components/sync/model/model_type_sync_bridge.h"
#include "components/sync/protocol/plus_address_setting_specifics.pb.h"

namespace plus_addresses {

// Bridge for PLUS_ADDRESS_SETTING. Lives on the UI thread and is owned by
// `PlusAddressSettingService`.
class PlusAddressSettingSyncBridge : public syncer::ModelTypeSyncBridge {
 public:
  explicit PlusAddressSettingSyncBridge(
      std::unique_ptr<syncer::ModelTypeChangeProcessor> change_processor,
      syncer::OnceModelTypeStoreFactory store_factory);
  ~PlusAddressSettingSyncBridge() override;

  // Returns the specifics for the setting of the given `name` if the bridge
  // is aware of any such setting. Otherwise, nullopt is returned.
  std::optional<sync_pb::PlusAddressSettingSpecifics> GetSetting(
      std::string_view name) const;

  // syncer::ModelTypeSyncBridge:
  std::unique_ptr<syncer::MetadataChangeList> CreateMetadataChangeList()
      override;
  std::optional<syncer::ModelError> MergeFullSyncData(
      std::unique_ptr<syncer::MetadataChangeList> metadata_change_list,
      syncer::EntityChangeList entity_data) override;
  std::optional<syncer::ModelError> ApplyIncrementalSyncChanges(
      std::unique_ptr<syncer::MetadataChangeList> metadata_change_list,
      syncer::EntityChangeList entity_changes) override;
  void ApplyDisableSyncChanges(std::unique_ptr<syncer::MetadataChangeList>
                                   delete_metadata_change_list) override;
  void GetData(StorageKeyList storage_keys, DataCallback callback) override;
  void GetAllDataForDebugging(DataCallback callback) override;
  bool IsEntityDataValid(const syncer::EntityData& entity_data) const override;
  std::string GetClientTag(const syncer::EntityData& entity_data) override;
  std::string GetStorageKey(const syncer::EntityData& entity_data) override;

 private:
  // Callbacks for various asynchronous operations of the `store_`.
  void OnStoreCreated(const std::optional<syncer::ModelError>& error,
                      std::unique_ptr<syncer::ModelTypeStore> store);
  void StartSyncingWithDataAndMetadata(
      const std::optional<syncer::ModelError>& error,
      std::unique_ptr<syncer::ModelTypeStore::RecordList> data,
      std::unique_ptr<syncer::MetadataBatch> metadata_batch);
  void ReportErrorIfSet(const std::optional<syncer::ModelError>& error);

  // Storage layer used by this sync bridge. Asynchronously created through the
  // `store_factory` injected through the constructor. Non-null if creation
  // finished without an error.
  std::unique_ptr<syncer::ModelTypeStore> store_;

  // A copy of the settings from the `store_`, used for synchronous access.
  // Keyed by `PlusAddressSettingSpecifics::name`.
  base::flat_map<std::string, sync_pb::PlusAddressSettingSpecifics> settings_;

  // Sequence checker ensuring that callbacks from the `store_` happen on the
  // bridge's thread.
  SEQUENCE_CHECKER(sequence_checker_);

  base::WeakPtrFactory<PlusAddressSettingSyncBridge> weak_factory_{this};
};

}  // namespace plus_addresses

#endif  // COMPONENTS_PLUS_ADDRESSES_SETTINGS_PLUS_ADDRESS_SETTING_SYNC_BRIDGE_H_
