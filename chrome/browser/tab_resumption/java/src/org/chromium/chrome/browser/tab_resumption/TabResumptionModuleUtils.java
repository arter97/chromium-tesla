// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.tab_resumption;

import android.text.TextUtils;

import org.chromium.base.cached_flags.BooleanCachedFieldTrialParameter;
import org.chromium.base.cached_flags.IntCachedFieldTrialParameter;
import org.chromium.chrome.browser.flags.ChromeFeatureList;
import org.chromium.components.embedder_support.util.UrlUtilities;
import org.chromium.url.GURL;

import java.util.Set;

/** Utilities for the tab resumption module. */
public class TabResumptionModuleUtils {
    private static final int DEFAULT_MAX_TILES_NUMBER = 2;

    /** Callback to handle click on suggestion tiles. */
    public interface SuggestionClickCallback {
        void onSuggestionClicked(SuggestionEntry entry);
    }

    /** Overrides getCurrentTime() return value, for testing. */
    public interface FakeGetCurrentTimeMs {
        long get();
    }

    /**
     * A set of the host URLs of default native apps on Android. This set should be keep consistent
     * with kDefaultAppBlocklist in {@link
     * components/visited_url_ranking/internal/url_visit_util.h}.
     */
    static final Set<String> sDefaultAppBlocklist =
            Set.of(
                    "assistant.google.com",
                    "calendar.google.com",
                    "docs.google.com",
                    "drive.google.com",
                    "mail.google.com",
                    "music.youtube.com",
                    "m.youtube.com",
                    "photos.google.com",
                    "www.youtube.com");

    private static final String TAB_RESUMPTION_V2_PARAM = "enable_v2";
    public static final BooleanCachedFieldTrialParameter TAB_RESUMPTION_V2 =
            ChromeFeatureList.newBooleanCachedFieldTrialParameter(
                    ChromeFeatureList.TAB_RESUMPTION_MODULE_ANDROID,
                    TAB_RESUMPTION_V2_PARAM,
                    false);

    private static final String TAB_RESUMPTION_MAX_TILES_NUMBER_PARAM = "max_tiles_number";
    public static final IntCachedFieldTrialParameter TAB_RESUMPTION_MAX_TILES_NUMBER =
            ChromeFeatureList.newIntCachedFieldTrialParameter(
                    ChromeFeatureList.TAB_RESUMPTION_MODULE_ANDROID,
                    TAB_RESUMPTION_MAX_TILES_NUMBER_PARAM,
                    DEFAULT_MAX_TILES_NUMBER);

    private static final String TAB_RESUMPTION_USE_SALIENT_IMAGE_PARAM = "use_salient_image";
    public static final BooleanCachedFieldTrialParameter TAB_RESUMPTION_USE_SALIENT_IMAGE =
            ChromeFeatureList.newBooleanCachedFieldTrialParameter(
                    ChromeFeatureList.TAB_RESUMPTION_MODULE_ANDROID,
                    TAB_RESUMPTION_USE_SALIENT_IMAGE_PARAM,
                    false);

    private static final String TAB_RESUMPTION_SHOW_SEE_MORE_PARAM = "show_see_more";
    public static final BooleanCachedFieldTrialParameter TAB_RESUMPTION_SHOW_SEE_MORE =
            ChromeFeatureList.newBooleanCachedFieldTrialParameter(
                    ChromeFeatureList.TAB_RESUMPTION_MODULE_ANDROID,
                    TAB_RESUMPTION_SHOW_SEE_MORE_PARAM,
                    false);

    private static final String TAB_RESUMPTION_USE_DEFAULT_APP_FILTER_PARAM =
            "use_default_app_filter";
    public static final BooleanCachedFieldTrialParameter TAB_RESUMPTION_USE_DEFAULT_APP_FILTER =
            ChromeFeatureList.newBooleanCachedFieldTrialParameter(
                    ChromeFeatureList.TAB_RESUMPTION_MODULE_ANDROID,
                    TAB_RESUMPTION_USE_DEFAULT_APP_FILTER_PARAM,
                    false);

    private static final String TAB_RESUMPTION_DISABLE_BLEND_PARAM = "disable_blend";
    public static final BooleanCachedFieldTrialParameter TAB_RESUMPTION_DISABLE_BLEND =
            ChromeFeatureList.newBooleanCachedFieldTrialParameter(
                    ChromeFeatureList.TAB_RESUMPTION_MODULE_ANDROID,
                    TAB_RESUMPTION_DISABLE_BLEND_PARAM,
                    false);

    private static final String TAB_RESUMPTION_FETCH_HISTORY_BACKEND_PARAM =
            "fetch_history_backend";
    public static final BooleanCachedFieldTrialParameter TAB_RESUMPTION_FETCH_HISTORY_BACKEND =
            ChromeFeatureList.newBooleanCachedFieldTrialParameter(
                    ChromeFeatureList.TAB_RESUMPTION_MODULE_ANDROID,
                    TAB_RESUMPTION_FETCH_HISTORY_BACKEND_PARAM,
                    false);

    private static final String TAB_RESUMPTION_FETCH_LOCAL_TABS_BACKEND_PARAM =
            "fetch_local_tabs_backend";
    public static final BooleanCachedFieldTrialParameter TAB_RESUMPTION_FETCH_LOCAL_TABS_BACKEND =
            ChromeFeatureList.newBooleanCachedFieldTrialParameter(
                    ChromeFeatureList.TAB_RESUMPTION_MODULE_ANDROID,
                    TAB_RESUMPTION_FETCH_LOCAL_TABS_BACKEND_PARAM,
                    false);

    private static FakeGetCurrentTimeMs sFakeGetCurrentTimeMs;

    /**
     * Overrides the getCurrentTimeMs() results. Using a function instead of static value for
     * flexibility, which is useful for simulating an advancing clock. Passing null disables this.
     */
    static void setFakeCurrentTimeMsForTesting(FakeGetCurrentTimeMs fakeGetCurrentTimeMs) {
        sFakeGetCurrentTimeMs = fakeGetCurrentTimeMs;
    }

    /** Returns the current time in ms since the epock. Mockable. */
    static long getCurrentTimeMs() {
        return (sFakeGetCurrentTimeMs == null)
                ? System.currentTimeMillis()
                : sFakeGetCurrentTimeMs.get();
    }

    /**
     * Extracts the registered, organization-identifying host and all its registry information, but
     * no subdomains, from a given URL. In particular, removes the "www." prefix.
     */
    static String getDomainUrl(GURL url) {
        String domainUrl = UrlUtilities.getDomainAndRegistry(url.getSpec(), false);
        return TextUtils.isEmpty(domainUrl) ? url.getHost() : domainUrl;
    }

    /**
     * Returns whether to exclude the given URL. It returns true if the host of the URL matches one
     * of the default native apps.
     *
     * @param url The URL of the suggestion.
     */
    static boolean shouldExcludeUrl(GURL url) {
        return TabResumptionModuleUtils.TAB_RESUMPTION_USE_DEFAULT_APP_FILTER.getValue()
                && sDefaultAppBlocklist.contains(url.getHost());
    }
}
