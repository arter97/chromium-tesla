// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.test.transit.hub;

import static androidx.test.espresso.Espresso.onView;
import static androidx.test.espresso.action.ViewActions.click;
import static androidx.test.espresso.action.ViewActions.replaceText;
import static androidx.test.espresso.matcher.ViewMatchers.isAssignableFrom;
import static androidx.test.espresso.matcher.ViewMatchers.withContentDescription;
import static androidx.test.espresso.matcher.ViewMatchers.withId;
import static androidx.test.espresso.matcher.ViewMatchers.withText;

import static org.hamcrest.CoreMatchers.allOf;
import static org.hamcrest.CoreMatchers.startsWith;

import static org.chromium.base.test.transit.ViewElement.unscopedViewElement;
import static org.chromium.base.test.transit.ViewElement.viewElement;

import android.content.Context;
import android.view.View;
import android.widget.EditText;

import androidx.annotation.Nullable;

import org.hamcrest.Matcher;

import org.chromium.base.test.transit.Elements;
import org.chromium.base.test.transit.Facility;
import org.chromium.base.test.transit.ViewElement;
import org.chromium.chrome.browser.tasks.tab_management.ColorPickerUtils;
import org.chromium.chrome.test.R;
import org.chromium.chrome.test.transit.HubTabSwitcherBaseStation;
import org.chromium.chrome.test.transit.tab_groups.TabGroupUtil;
import org.chromium.components.tab_groups.TabGroupColorId;

import java.util.List;

/** Dialog that appears when a new tab group is created to name the group and pick a color. */
public class HubNewTabGroupDialogFacility extends Facility<HubTabSwitcherBaseStation> {

    public static final ViewElement DIALOG = viewElement(withId(R.id.visual_data_dialog_layout));
    public static final ViewElement DIALOG_TITLE =
            viewElement(allOf(withId(R.id.visual_data_dialog_title), withText("New tab group")));
    public static final Matcher<View> TITLE_INPUT_MATCHER =
            allOf(withId(R.id.title_input_text), isAssignableFrom(EditText.class));
    // TODO(crbug.com/345489175): Partially cut off in android_30_google_apis_x86.textpb
    public static final ViewElement COLOR_PICKER_CONTAINER =
            viewElement(
                    withId(R.id.color_picker_container),
                    ViewElement.newOptions().displayingAtLeast(50).build());
    public static final ViewElement DONE_BUTTON = viewElement(withId(R.id.positive_button));

    private final List<Integer> mTabIdsToGroup;
    private final String mTitle;
    private final @Nullable @TabGroupColorId Integer mSelectedColor;
    private ViewElement mTitleInputElement;

    /** Constructor. Expects no particular title or selected color. */
    public HubNewTabGroupDialogFacility(
            HubTabSwitcherBaseStation hostStation, List<Integer> tabIdsToGroup) {
        this(
                hostStation,
                tabIdsToGroup,
                TabGroupUtil.getNumberOfTabsString(tabIdsToGroup.size()),
                /* selectedColor= */ null);
    }

    /** Constructor. Expects a specific title and selected color. */
    public HubNewTabGroupDialogFacility(
            HubTabSwitcherBaseStation hostStation,
            List<Integer> tabIdsToGroup,
            String title,
            @Nullable @TabGroupColorId Integer selectedColor) {
        super(hostStation);
        mTabIdsToGroup = tabIdsToGroup;
        mTitle = title;
        mSelectedColor = selectedColor;
    }

    @Override
    public void declareElements(Elements.Builder elements) {
        elements.declareView(DIALOG);
        elements.declareView(DIALOG_TITLE);

        String inputElementId = "Tab group title input showing " + mTitle;
        mTitleInputElement =
                viewElement(
                        allOf(TITLE_INPUT_MATCHER, withText(mTitle)),
                        ViewElement.newOptions().elementId(inputElementId).build());
        elements.declareView(mTitleInputElement);

        elements.declareView(COLOR_PICKER_CONTAINER);
        @TabGroupColorId List<Integer> colors = ColorPickerUtils.getTabGroupColorIdList();
        for (@TabGroupColorId Integer color : colors) {
            if (mSelectedColor != null) {
                elements.declareView(
                        unscopedViewElement(
                                colorPickerIconMatcher(color, color.equals(mSelectedColor))));
            } else {
                elements.declareView(
                        unscopedViewElement(colorPickerIconMatcher(color, /* selected= */ null)));
            }
        }

        elements.declareView(DONE_BUTTON);
    }

    private Matcher<View> colorPickerIconMatcher(
            @TabGroupColorId Integer color, @Nullable Boolean selected) {
        Context context = mHostStation.getActivity();
        String colorName =
                context.getString(
                        ColorPickerUtils.getTabGroupColorPickerItemColorAccessibilityString(color));
        Matcher<View> contentDescriptionMatcher;
        if (selected != null) {
            contentDescriptionMatcher =
                    withContentDescription(
                            colorName + " " + (selected ? "Selected" : "Not selected"));
        } else {
            contentDescriptionMatcher = withContentDescription(startsWith(colorName));
        }
        return allOf(withId(R.id.color_picker_icon), contentDescriptionMatcher);
    }

    /** Input a new tab group name. */
    public HubNewTabGroupDialogFacility inputName(String newTabGroupName) {
        return mHostStation.swapFacilitySync(
                this,
                new HubNewTabGroupDialogFacility(
                        mHostStation, mTabIdsToGroup, newTabGroupName, mSelectedColor),
                () -> mTitleInputElement.perform(replaceText(newTabGroupName)));
    }

    /** Select a color. */
    public HubNewTabGroupDialogFacility pickColor(@TabGroupColorId int newColor) {
        return mHostStation.swapFacilitySync(
                this,
                new HubNewTabGroupDialogFacility(mHostStation, mTabIdsToGroup, mTitle, newColor),
                () ->
                        onView(colorPickerIconMatcher(newColor, /* selected= */ false))
                                .perform(click()));
    }

    /** Press "Done" to confirm the tab group name and color. */
    public HubTabSwitcherGroupCardFacility pressDone() {
        return mHostStation.swapFacilitySync(
                this,
                new HubTabSwitcherGroupCardFacility(mHostStation, mTabIdsToGroup, mTitle),
                () -> DONE_BUTTON.perform(click()));
    }
}
