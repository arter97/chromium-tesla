// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/data_sharing/internal/collaboration_group_sync_bridge.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <utility>

#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/notimplemented.h"
#include "base/sequence_checker.h"
#include "components/sync/base/model_type.h"
#include "components/sync/model/in_memory_metadata_change_list.h"
#include "components/sync/model/model_type_sync_bridge.h"
#include "components/sync/model/mutable_data_batch.h"
#include "components/sync/protocol/collaboration_group_specifics.pb.h"
#include "components/sync/protocol/entity_data.h"
#include "components/sync/protocol/entity_specifics.pb.h"

namespace data_sharing {

namespace {

std::unique_ptr<syncer::EntityData> SpecificsToEntityData(
    const sync_pb::CollaborationGroupSpecifics& specifics) {
  auto entity_data = std::make_unique<syncer::EntityData>();
  *entity_data->specifics.mutable_collaboration_group() = specifics;
  entity_data->name = specifics.collaboration_id();
  return entity_data;
}

}  // namespace

CollaborationGroupSyncBridge::CollaborationGroupSyncBridge(
    std::unique_ptr<syncer::ModelTypeChangeProcessor> change_processor,
    syncer::OnceModelTypeStoreFactory store_factory)
    : syncer::ModelTypeSyncBridge(std::move(change_processor)) {
  std::move(store_factory)
      .Run(
          syncer::COLLABORATION_GROUP,
          base::BindOnce(&CollaborationGroupSyncBridge::OnModelTypeStoreCreated,
                         weak_ptr_factory_.GetWeakPtr()));
}

CollaborationGroupSyncBridge::~CollaborationGroupSyncBridge() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
}

std::unique_ptr<syncer::MetadataChangeList>
CollaborationGroupSyncBridge::CreateMetadataChangeList() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return std::make_unique<syncer::InMemoryMetadataChangeList>();
}

std::optional<syncer::ModelError>
CollaborationGroupSyncBridge::MergeFullSyncData(
    std::unique_ptr<syncer::MetadataChangeList> metadata_change_list,
    syncer::EntityChangeList entity_change_list) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  CHECK(ids_to_specifics_.empty());
  // This is a read-only data type, meaning that no data originates locally,
  // hence there is nothing to merge.
  return ApplyIncrementalSyncChanges(std::move(metadata_change_list),
                                     std::move(entity_change_list));
}

std::optional<syncer::ModelError>
CollaborationGroupSyncBridge::ApplyIncrementalSyncChanges(
    std::unique_ptr<syncer::MetadataChangeList> metadata_change_list,
    syncer::EntityChangeList entity_changes) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  std::unique_ptr<syncer::ModelTypeStore::WriteBatch> batch =
      model_type_store_->CreateWriteBatch();

  std::vector<std::string> added_ids;
  std::vector<std::string> updated_ids;
  std::vector<std::string> deleted_ids;

  for (const std::unique_ptr<syncer::EntityChange>& change : entity_changes) {
    const std::string& collaboration_id = change->storage_key();
    switch (change->type()) {
      case syncer::EntityChange::ACTION_ADD:
      case syncer::EntityChange::ACTION_UPDATE: {
        const sync_pb::EntitySpecifics& entity_specifics =
            change->data().specifics;
        // Guaranteed by ClientTagBasedModelTypeProcessor, based on
        // IsEntityDataValid().
        CHECK(entity_specifics.has_collaboration_group());
        const sync_pb::CollaborationGroupSpecifics collaboration_specifics =
            entity_specifics.collaboration_group();

        ids_to_specifics_[collaboration_id] = collaboration_specifics;
        batch->WriteData(collaboration_id,
                         collaboration_specifics.SerializeAsString());
        break;
      }
      case syncer::EntityChange::ACTION_DELETE:
        ids_to_specifics_.erase(collaboration_id);
        batch->DeleteData(collaboration_id);
        break;
    }
    switch (change->type()) {
      case syncer::EntityChange::ACTION_ADD:
        added_ids.push_back(collaboration_id);
        break;
      case syncer::EntityChange::ACTION_UPDATE:
        updated_ids.push_back(collaboration_id);
        break;
      case syncer::EntityChange::ACTION_DELETE:
        deleted_ids.push_back(collaboration_id);
        break;
    }
  }

  batch->TakeMetadataChangesFrom(std::move(metadata_change_list));
  model_type_store_->CommitWriteBatch(
      std::move(batch),
      base::BindOnce(&CollaborationGroupSyncBridge::OnModelTypeStoreCommit,
                     weak_ptr_factory_.GetWeakPtr()));

  for (auto& observer : observers_) {
    observer.OnGroupsUpdated(added_ids, updated_ids, deleted_ids);
  }

  return std::nullopt;
}

void CollaborationGroupSyncBridge::GetDataForCommit(StorageKeyList storage_keys,
                                                    DataCallback callback) {
  NOTREACHED();
}

void CollaborationGroupSyncBridge::GetAllDataForDebugging(
    DataCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto batch = std::make_unique<syncer::MutableDataBatch>();
  for (const auto& [id, specifics] : ids_to_specifics_) {
    batch->Put(id, SpecificsToEntityData(specifics));
  }
  std::move(callback).Run(std::move(batch));
}

std::string CollaborationGroupSyncBridge::GetClientTag(
    const syncer::EntityData& entity_data) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return GetStorageKey(entity_data);
}

std::string CollaborationGroupSyncBridge::GetStorageKey(
    const syncer::EntityData& entity_data) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  CHECK(entity_data.specifics.has_collaboration_group());
  return entity_data.specifics.collaboration_group().collaboration_id();
}

void CollaborationGroupSyncBridge::ApplyDisableSyncChanges(
    std::unique_ptr<syncer::MetadataChangeList> delete_metadata_change_list) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  ids_to_specifics_.clear();
  model_type_store_->DeleteAllDataAndMetadata(base::DoNothing());
  weak_ptr_factory_.InvalidateWeakPtrs();
}

bool CollaborationGroupSyncBridge::IsEntityDataValid(
    const syncer::EntityData& entity_data) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return !entity_data.specifics.collaboration_group()
              .collaboration_id()
              .empty();
}

void CollaborationGroupSyncBridge::OnModelTypeStoreCreated(
    const std::optional<syncer::ModelError>& error,
    std::unique_ptr<syncer::ModelTypeStore> store) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (error) {
    change_processor()->ReportError(*error);
    return;
  }

  model_type_store_ = std::move(store);

  model_type_store_->ReadAllData(
      base::BindOnce(&CollaborationGroupSyncBridge::OnReadAllData,
                     weak_ptr_factory_.GetWeakPtr()));
}

void CollaborationGroupSyncBridge::OnReadAllData(
    const std::optional<syncer::ModelError>& error,
    std::unique_ptr<syncer::ModelTypeStore::RecordList> record_list) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (error) {
    change_processor()->ReportError(*error);
    return;
  }

  for (const auto& record : *record_list) {
    sync_pb::CollaborationGroupSpecifics specifics;
    if (!specifics.ParseFromString(record.value)) {
      change_processor()->ReportError(
          {FROM_HERE, "Failed to deserialize database record as specifics."});
      return;
    }
    ids_to_specifics_[specifics.collaboration_id()] = std::move(specifics);
  }

  for (auto& observer : observers_) {
    observer.OnDataLoaded();
  }

  model_type_store_->ReadAllMetadata(
      base::BindOnce(&CollaborationGroupSyncBridge::OnReadAllMetadata,
                     weak_ptr_factory_.GetWeakPtr()));
}

void CollaborationGroupSyncBridge::OnReadAllMetadata(
    const std::optional<syncer::ModelError>& error,
    std::unique_ptr<syncer::MetadataBatch> metadata_batch) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (error) {
    change_processor()->ReportError(*error);
    return;
  }
  change_processor()->ModelReadyToSync(std::move(metadata_batch));
}

void CollaborationGroupSyncBridge::OnModelTypeStoreCommit(
    const std::optional<syncer::ModelError>& error) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (error) {
    change_processor()->ReportError(*error);
  }
}

std::vector<std::string>
CollaborationGroupSyncBridge::GetCollaborationGroupIds() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  std::vector<std::string> ids;
  for (const auto& [id, _] : ids_to_specifics_) {
    ids.push_back(id);
  }
  return ids;
}

void CollaborationGroupSyncBridge::AddObserver(Observer* observer) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  observers_.AddObserver(observer);
}

void CollaborationGroupSyncBridge::RemoveObserver(Observer* observer) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  observers_.RemoveObserver(observer);
}

}  // namespace data_sharing
