// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/data_sharing/internal/android/data_sharing_service_android.h"

#include <memory>
#include <string>

#include "base/android/callback_android.h"
#include "base/android/jni_android.h"
#include "base/android/jni_string.h"
#include "base/android/scoped_java_ref.h"
#include "base/scoped_observation.h"
#include "components/data_sharing/internal/android/data_sharing_conversion_bridge.h"
#include "components/data_sharing/internal/android/data_sharing_network_loader_android.h"
#include "components/data_sharing/public/data_sharing_service.h"

// Must come after all headers that specialize FromJniType() / ToJniType().
#include "components/data_sharing/internal/jni_headers/DataSharingServiceImpl_jni.h"
#include "components/data_sharing/internal/jni_headers/ObserverBridge_jni.h"

using base::android::AttachCurrentThread;
using base::android::ConvertJavaStringToUTF8;
using base::android::ConvertUTF8ToJavaString;
using base::android::JavaParamRef;
using base::android::RunObjectCallbackAndroid;
using base::android::ScopedJavaGlobalRef;
using base::android::ScopedJavaLocalRef;

namespace data_sharing {
namespace {

const char kDataSharingServiceBridgeKey[] = "data_sharing_service_bridge";

void RunGroupsDataSetOrFailureOutcomeCallback(
    const JavaRef<jobject>& j_callback,
    const DataSharingService::GroupsDataSetOrFailureOutcome& result) {
  JNIEnv* env = AttachCurrentThread();
  ScopedJavaLocalRef<jobject> j_result =
      DataSharingConversionBridge::CreateGroupDataSetOrFailureOutcome(env,
                                                                      result);
  RunObjectCallbackAndroid(j_callback, j_result);
}

void RunGroupDataOrFailureOutcomeCallback(
    const JavaRef<jobject>& j_callback,
    const DataSharingService::GroupDataOrFailureOutcome& result) {
  JNIEnv* env = AttachCurrentThread();
  ScopedJavaLocalRef<jobject> j_result =
      DataSharingConversionBridge::CreateGroupDataOrFailureOutcome(env, result);
  RunObjectCallbackAndroid(j_callback, j_result);
}

void RunPeopleGroupActionOutcomeCallback(
    const JavaRef<jobject>& j_callback,
    DataSharingService::PeopleGroupActionOutcome result) {
  ScopedJavaLocalRef<jobject> j_result =
      DataSharingConversionBridge::CreatePeopleGroupActionOutcome(
          AttachCurrentThread(), static_cast<int>(result));
  RunObjectCallbackAndroid(j_callback, j_result);
}

}  // namespace

// Native counterpart of Java ObserverBridge. Observes the native service and
// sends notifications to the Java bridge.
class DataSharingServiceAndroid::GroupDataObserverBridge
    : public DataSharingService::Observer {
 public:
  GroupDataObserverBridge(
      DataSharingService* data_sharing_service,
      DataSharingServiceAndroid* data_sharing_service_android);
  ~GroupDataObserverBridge() override;

  GroupDataObserverBridge(const GroupDataObserverBridge&) = delete;
  GroupDataObserverBridge& operator=(const GroupDataObserverBridge&) = delete;

  // DataSharingService::Observer impl:
  void OnGroupChanged(const GroupData& group_data) override;
  void OnGroupAdded(const GroupData& group_data) override;
  void OnGroupRemoved(const std::string& group_id) override;

 private:
  ScopedJavaLocalRef<jobject> java_obj_;
  base::ScopedObservation<DataSharingService, DataSharingService::Observer>
      scoped_obs_{this};
};

DataSharingServiceAndroid::GroupDataObserverBridge::GroupDataObserverBridge(
    DataSharingService* data_sharing_service,
    DataSharingServiceAndroid* data_sharing_service_android) {
  java_obj_ = data_sharing_service_android->GetJavaObserverBridge();
  scoped_obs_.Observe(data_sharing_service);
}

DataSharingServiceAndroid::GroupDataObserverBridge::~GroupDataObserverBridge() =
    default;

void DataSharingServiceAndroid::GroupDataObserverBridge::OnGroupChanged(
    const GroupData& group_data) {
  JNIEnv* env = AttachCurrentThread();
  ScopedJavaLocalRef<jobject> j_group =
      DataSharingConversionBridge::CreateJavaGroupData(env, group_data);
  Java_ObserverBridge_onGroupChanged(env, java_obj_, j_group);
}

void DataSharingServiceAndroid::GroupDataObserverBridge::OnGroupAdded(
    const GroupData& group_data) {
  JNIEnv* env = AttachCurrentThread();
  ScopedJavaLocalRef<jobject> j_group =
      DataSharingConversionBridge::CreateJavaGroupData(env, group_data);
  Java_ObserverBridge_onGroupAdded(env, java_obj_, j_group);
}

void DataSharingServiceAndroid::GroupDataObserverBridge::OnGroupRemoved(
    const std::string& group_id) {
  JNIEnv* env = AttachCurrentThread();
  Java_ObserverBridge_onGroupRemoved(env, java_obj_,
                                     ConvertUTF8ToJavaString(env, group_id));
}

// This function is declared in data_sharing_service.h and
// should be linked in to any binary using
// DataSharingService::GetJavaObject.
// static
ScopedJavaLocalRef<jobject> DataSharingService::GetJavaObject(
    DataSharingService* service) {
  if (!service->GetUserData(kDataSharingServiceBridgeKey)) {
    service->SetUserData(kDataSharingServiceBridgeKey,
                         std::make_unique<DataSharingServiceAndroid>(service));
  }

  DataSharingServiceAndroid* bridge = static_cast<DataSharingServiceAndroid*>(
      service->GetUserData(kDataSharingServiceBridgeKey));

  return bridge->GetJavaObject();
}

DataSharingServiceAndroid::DataSharingServiceAndroid(
    DataSharingService* data_sharing_service)
    : data_sharing_service_(data_sharing_service),
      network_loader_(std::make_unique<DataSharingNetworkLoaderAndroid>(
          data_sharing_service->GetDataSharingNetworkLoader())) {
  DCHECK(data_sharing_service_);
  JNIEnv* env = base::android::AttachCurrentThread();
  java_obj_.Reset(env, Java_DataSharingServiceImpl_create(
                           env, reinterpret_cast<int64_t>(this))
                           .obj());
  observer_bridge_ =
      std::make_unique<GroupDataObserverBridge>(data_sharing_service, this);
}

DataSharingServiceAndroid::~DataSharingServiceAndroid() {
  JNIEnv* env = base::android::AttachCurrentThread();
  Java_DataSharingServiceImpl_clearNativePtr(env, java_obj_);
}

void DataSharingServiceAndroid::ReadAllGroups(
    JNIEnv* env,
    const JavaParamRef<jobject>& j_callback) {
  data_sharing_service_->ReadAllGroups(
      base::BindOnce(&RunGroupsDataSetOrFailureOutcomeCallback,
                     ScopedJavaGlobalRef<jobject>(j_callback)));
}

void DataSharingServiceAndroid::ReadGroup(
    JNIEnv* env,
    const JavaParamRef<jstring>& group_id,
    const JavaParamRef<jobject>& j_callback) {
  data_sharing_service_->ReadGroup(
      ConvertJavaStringToUTF8(env, group_id),
      base::BindOnce(&RunGroupDataOrFailureOutcomeCallback,
                     ScopedJavaGlobalRef<jobject>(j_callback)));
}

void DataSharingServiceAndroid::CreateGroup(
    JNIEnv* env,
    const JavaParamRef<jstring>& group_name,
    const JavaParamRef<jobject>& j_callback) {
  data_sharing_service_->CreateGroup(
      ConvertJavaStringToUTF8(env, group_name),
      base::BindOnce(&RunGroupDataOrFailureOutcomeCallback,
                     ScopedJavaGlobalRef<jobject>(j_callback)));
}

void DataSharingServiceAndroid::DeleteGroup(
    JNIEnv* env,
    const JavaParamRef<jstring>& group_id,
    const JavaParamRef<jobject>& j_callback) {
  data_sharing_service_->DeleteGroup(
      ConvertJavaStringToUTF8(env, group_id),
      base::BindOnce(&RunPeopleGroupActionOutcomeCallback,
                     ScopedJavaGlobalRef<jobject>(j_callback)));
}

void DataSharingServiceAndroid::InviteMember(
    JNIEnv* env,
    const JavaParamRef<jstring>& group_id,
    const JavaParamRef<jstring>& invitee_email,
    const JavaParamRef<jobject>& j_callback) {
  data_sharing_service_->InviteMember(
      ConvertJavaStringToUTF8(env, group_id),
      ConvertJavaStringToUTF8(env, invitee_email),
      base::BindOnce(&RunPeopleGroupActionOutcomeCallback,
                     ScopedJavaGlobalRef<jobject>(j_callback)));
}

void DataSharingServiceAndroid::RemoveMember(
    JNIEnv* env,
    const JavaParamRef<jstring>& group_id,
    const JavaParamRef<jstring>& member_email,
    const JavaParamRef<jobject>& j_callback) {
  data_sharing_service_->RemoveMember(
      ConvertJavaStringToUTF8(env, group_id),
      ConvertJavaStringToUTF8(env, member_email),
      base::BindOnce(&RunPeopleGroupActionOutcomeCallback,
                     ScopedJavaGlobalRef<jobject>(j_callback)));
}

bool DataSharingServiceAndroid::IsEmptyService(
    JNIEnv* env,
    const JavaParamRef<jobject>& jcaller) {
  return data_sharing_service_->IsEmptyService();
}

ScopedJavaLocalRef<jobject> DataSharingServiceAndroid::GetNetworkLoader(
    JNIEnv* env) {
  return network_loader_->GetJavaObject();
}

ScopedJavaLocalRef<jobject> DataSharingServiceAndroid::GetJavaObject() {
  return ScopedJavaLocalRef<jobject>(java_obj_);
}

ScopedJavaLocalRef<jobject> DataSharingServiceAndroid::GetJavaObserverBridge() {
  JNIEnv* env = base::android::AttachCurrentThread();
  return Java_DataSharingServiceImpl_getObserverBridge(env, GetJavaObject());
}

}  // namespace data_sharing
