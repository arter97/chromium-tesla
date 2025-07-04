// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.signin.services;

import androidx.annotation.IntDef;
import androidx.annotation.VisibleForTesting;

import org.jni_zero.NativeMethods;

import org.chromium.base.metrics.RecordHistogram;
import org.chromium.components.signin.metrics.AccountConsistencyPromoAction;
import org.chromium.components.signin.metrics.SigninAccessPoint;

import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;

/** Util methods for signin metrics logging. */
public class SigninMetricsUtils {
    /** Used to record Signin.AddAccountState histogram. Do not change existing values. */
    @Retention(RetentionPolicy.SOURCE)
    @IntDef({
        State.REQUESTED,
        State.STARTED,
        State.SUCCEEDED,
        State.FAILED,
        State.CANCELLED,
        State.NULL_ACCOUNT_NAME,
        State.NUM_STATES
    })
    public @interface State {
        int REQUESTED = 0;
        int STARTED = 1;
        int SUCCEEDED = 2;
        int FAILED = 3;
        int CANCELLED = 4;
        int NULL_ACCOUNT_NAME = 5;
        int NUM_STATES = 6;
    }

    public @interface SyncButtonClicked {
        // These values are persisted to logs. Entries should not be renumbered and
        // numeric values should never be reused.
        int SYNC_OPT_IN_EQUAL_WEIGHTED = 0;
        int SYNC_CANCEL_EQUAL_WEIGHTED = 1;
        int SYNC_SETTINGS_EQUAL_WEIGHTED = 2;
        int SYNC_OPT_IN_NOT_EQUAL_WEIGHTED = 3;
        int SYNC_CANCEL_NOT_EQUAL_WEIGHTED = 4;
        int SYNC_SETTINGS_NOT_EQUAL_WEIGHTED = 5;
        int HISTORY_SYNC_OPT_IN_EQUAL_WEIGHTED = 6;
        int HISTORY_SYNC_CANCEL_EQUAL_WEIGHTED = 7;
        int HISTORY_SYNC_OPT_IN_NOT_EQUAL_WEIGHTED = 8;
        int HISTORY_SYNC_CANCEL_NOT_EQUAL_WEIGHTED = 9;
        int NUM_ENTRIES = 8;
    };

    /**
     * Logs Signin.AccountConsistencyPromoAction.* histograms.
     *
     * @param promoAction {@link AccountConsistencyPromoAction} for this sign-in flow.
     * @param accessPoint {@link SigninAccessPoint} that initiated the sign-in flow.
     */
    public static void logAccountConsistencyPromoAction(
            @AccountConsistencyPromoAction int promoAction, @SigninAccessPoint int accessPoint) {
        SigninMetricsUtilsJni.get().logAccountConsistencyPromoAction(promoAction, accessPoint);
    }

    /**
     * Logs the access point when the user see the view of choosing account to sign in. Sign-in
     * completion histogram is recorded by {@link SigninManager#signinAndEnableSync}.
     *
     * @param accessPoint {@link SigninAccessPoint} that initiated the sign-in flow.
     */
    public static void logSigninStartAccessPoint(@SigninAccessPoint int accessPoint) {
        RecordHistogram.recordEnumeratedHistogram(
                "Signin.SigninStartedAccessPoint", accessPoint, SigninAccessPoint.MAX);
        RecordHistogram.recordEnumeratedHistogram(
                "Signin.SignIn.Started", accessPoint, SigninAccessPoint.MAX);
    }

    /** Logs signin user action for a given {@link SigninAccessPoint}. */
    public static void logSigninUserActionForAccessPoint(@SigninAccessPoint int accessPoint) {
        // TODO(crbug.com/40233859): Remove this check when user action checks are removed
        // from native code.
        if (accessPoint != SigninAccessPoint.SETTINGS_SYNC_OFF_ROW) {
            SigninMetricsUtilsJni.get().logSigninUserActionForAccessPoint(accessPoint);
        }
    }

    /** Logs Signin.AddAccountState histogram. */
    public static void logAddAccountStateHistogram(@State int state) {
        RecordHistogram.recordEnumeratedHistogram(
                "Signin.AddAccountState", state, State.NUM_STATES);
    }

    public static void logHistorySyncAcceptButtonClicked(
            @SigninAccessPoint int accessPoint, @SyncButtonClicked int syncButtonType) {
        RecordHistogram.recordEnumeratedHistogram(
                "Signin.HistorySyncOptIn.Completed", accessPoint, SigninAccessPoint.MAX);
        recordButtonTypeClicked(syncButtonType);
    }

    public static void logHistorySyncDeclineButtonClicked(
            @SigninAccessPoint int accessPoint, @SyncButtonClicked int syncButtonType) {
        RecordHistogram.recordEnumeratedHistogram(
                "Signin.HistorySyncOptIn.Declined", accessPoint, SigninAccessPoint.MAX);
        recordButtonTypeClicked(syncButtonType);
    }

    public static void recordButtonTypeClicked(@SyncButtonClicked int type) {
        RecordHistogram.recordEnumeratedHistogram(
                "Signin.SyncButtons.Clicked", type, SyncButtonClicked.NUM_ENTRIES);
    }

    @VisibleForTesting(otherwise = VisibleForTesting.PACKAGE_PRIVATE)
    @NativeMethods
    public interface Natives {
        void logSigninUserActionForAccessPoint(int accessPoint);

        void logAccountConsistencyPromoAction(int promoAction, int accessPoint);
    }

    private SigninMetricsUtils() {}
}
