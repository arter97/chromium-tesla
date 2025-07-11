// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.components.data_sharing;

import org.chromium.base.Callback;
import org.chromium.base.ObserverList;
import org.chromium.base.UserDataHost;

/** Data sharing service impl for testing. */
public class TestDataSharingService implements DataSharingService {

    ObserverList<DataSharingService.Observer> mJavaObservers = new ObserverList<>();

    public TestDataSharingService() {}

    @Override
    public void addObserver(Observer observer) {
        mJavaObservers.addObserver(observer);
    }

    @Override
    public void removeObserver(Observer observer) {
        mJavaObservers.removeObserver(observer);
    }

    @Override
    public void readAllGroups(Callback<GroupsDataSetOrFailureOutcome> callback) {
        Callback.runNullSafe(
                callback,
                new DataSharingService.GroupsDataSetOrFailureOutcome(
                        null, PeopleGroupActionFailure.PERSISTENT_FAILURE));
    }

    @Override
    public void readGroup(String groupId, Callback<GroupDataOrFailureOutcome> callback) {
        Callback.runNullSafe(
                callback,
                new DataSharingService.GroupDataOrFailureOutcome(
                        null, PeopleGroupActionFailure.PERSISTENT_FAILURE));
    }

    @Override
    public void createGroup(String groupName, Callback<GroupDataOrFailureOutcome> callback) {
        Callback.runNullSafe(
                callback,
                new DataSharingService.GroupDataOrFailureOutcome(
                        null, PeopleGroupActionFailure.PERSISTENT_FAILURE));
    }

    @Override
    public void deleteGroup(String groupId, Callback<Integer> callback) {
        Callback.runNullSafe(callback, PeopleGroupActionOutcome.PERSISTENT_FAILURE);
    }

    @Override
    public void inviteMember(String groupId, String inviteeEmail, Callback<Integer> callback) {
        Callback.runNullSafe(callback, PeopleGroupActionOutcome.PERSISTENT_FAILURE);
    }

    @Override
    public void removeMember(String groupId, String memberEmail, Callback<Integer> callback) {
        Callback.runNullSafe(callback, PeopleGroupActionOutcome.PERSISTENT_FAILURE);
    }

    @Override
    public boolean isEmptyService() {
        return true;
    }

    @Override
    public DataSharingNetworkLoader getNetworkLoader() {
        return null;
    }

    @Override
    public UserDataHost getUserDataHost() {
        return null;
    }
}
