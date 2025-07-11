// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.test.util.browser.signin;

import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.drawable.Drawable;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.appcompat.content.res.AppCompatResources;

import org.junit.rules.TestRule;
import org.junit.runner.Description;
import org.junit.runners.model.Statement;

import org.chromium.base.ContextUtils;
import org.chromium.chrome.R;
import org.chromium.components.signin.AccountManagerFacadeProvider;
import org.chromium.components.signin.base.AccountCapabilities;
import org.chromium.components.signin.base.AccountInfo;
import org.chromium.components.signin.base.CoreAccountId;
import org.chromium.components.signin.base.CoreAccountInfo;
import org.chromium.components.signin.identitymanager.AccountInfoServiceProvider;
import org.chromium.components.signin.identitymanager.IdentityManager;
import org.chromium.components.signin.test.util.AccountCapabilitiesBuilder;
import org.chromium.components.signin.test.util.FakeAccountInfoService;
import org.chromium.components.signin.test.util.FakeAccountManagerFacade;
import org.chromium.content_public.browser.test.util.TestThreadUtils;

import java.util.HashMap;

/**
 * This test rule mocks AccountManagerFacade.
 *
 * <p>TODO(crbug.com/40228092): Migrate usages that need native to {@link SigninTestRule} and remove
 * the methods that call native from this rule.
 *
 * <p>The rule will not invoke any native code, therefore it is safe to use it in Robolectric tests.
 */
public class AccountManagerTestRule implements TestRule {
    // TODO(crbug.com/40234741): Migrate tests that don't need to create their own accounts to these
    // constants.
    public static final AccountInfo TEST_ACCOUNT_1 =
            new AccountInfo.Builder(
                            "test@gmail.com", FakeAccountManagerFacade.toGaiaId("test@gmail.com"))
                    .fullName("Test1 Full")
                    .givenName("Test1 Given")
                    .accountImage(createAvatar())
                    .build();

    public static final AccountInfo TEST_ACCOUNT_2 =
            new AccountInfo.Builder(
                            "test2@gmail.com", FakeAccountManagerFacade.toGaiaId("test2@gmail.com"))
                    .fullName("Test2 Full")
                    .givenName("Test2 Given")
                    .accountImage(createAvatar())
                    .build();

    public static final AccountInfo TEST_CHILD_ACCOUNT =
            new AccountInfo.Builder(
                            generateChildEmail(TEST_ACCOUNT_1.getEmail()),
                            FakeAccountManagerFacade.toGaiaId(
                                    generateChildEmail(TEST_ACCOUNT_1.getEmail())))
                    .fullName("Test1 Full")
                    .givenName("Test1 Given")
                    .accountImage(createAvatar())
                    .build();

    public static final AccountInfo TEST_NON_GMAIL_ACCOUNT =
            new AccountInfo.Builder(
                            "test@nongmail.com",
                            FakeAccountManagerFacade.toGaiaId("test@nongmail.com"))
                    .fullName("Test Non Gmail Full")
                    .givenName("Test Non Gmail Given")
                    .accountImage(createAvatar())
                    .build();

    public static final AccountInfo TEST_ACCOUNT_NON_DISPLAYABLE_EMAIL =
            new AccountInfo.Builder(
                            generateChildEmail("test@gmail.com"),
                            FakeAccountManagerFacade.toGaiaId("test@gmail.com"))
                    .fullName("Test1 Full")
                    .givenName("Test1 Given")
                    .accountImage(createAvatar())
                    .accountCapabilities(
                            new AccountCapabilitiesBuilder()
                                    .setCanHaveEmailAddressDisplayed(false)
                                    .setIsSubjectToParentalControls(true)
                                    .build())
                    .build();

    public static final AccountInfo TEST_ACCOUNT_NON_DISPLAYABLE_EMAIL_AND_NO_NAME =
            new AccountInfo.Builder(
                            generateChildEmail("test@gmail.com"),
                            FakeAccountManagerFacade.toGaiaId("test@gmail.com"))
                    .accountImage(createAvatar())
                    .accountCapabilities(
                            new AccountCapabilitiesBuilder()
                                    .setCanHaveEmailAddressDisplayed(false)
                                    .setIsSubjectToParentalControls(true)
                                    .build())
                    .build();

    private static final AccountCapabilities MINOR_MODE_NOT_REQUIRED =
            new AccountCapabilitiesBuilder()
                    .setCanShowHistorySyncOptInsWithoutMinorModeRestrictions(true)
                    .build();

    private static final AccountCapabilities MINOR_MODE_REQUIRED =
            new AccountCapabilitiesBuilder()
                    .setCanShowHistorySyncOptInsWithoutMinorModeRestrictions(false)
                    .build();

    public static final AccountInfo AADC_MINOR_ACCOUNT =
            new AccountInfo.Builder(
                            "aadc.minor.account@gmail.com",
                            FakeAccountManagerFacade.toGaiaId("aadc.minor.account@gmail.com"))
                    .fullName("AADC Minor")
                    .givenName("AADC Minor Account")
                    .accountImage(createAvatar())
                    .accountCapabilities(MINOR_MODE_REQUIRED)
                    .build();

    public static final AccountInfo AADC_ADULT_ACCOUNT =
            new AccountInfo.Builder(
                            "aadc.adult.account@gmail.com",
                            FakeAccountManagerFacade.toGaiaId("aadc.adult.account@gmail.com"))
                    .fullName("AADC Adult")
                    .givenName("AADC Adult Account")
                    .accountImage(createAvatar())
                    .accountCapabilities(MINOR_MODE_NOT_REQUIRED)
                    .build();

    public static final AccountInfo AADC_UNRESOLVED_ACCOUNT = TEST_ACCOUNT_1;

    // TODO(crbug.com/40890215): Use TEST_ACCOUNT_1 instead.
    @Deprecated public static final String TEST_ACCOUNT_EMAIL = "test@gmail.com";

    private final @NonNull FakeAccountManagerFacade mFakeAccountManagerFacade;
    // TODO(crbug.com/40234741): Revise this test rule and make this non-nullable.
    private final @Nullable FakeAccountInfoService mFakeAccountInfoService;

    public AccountManagerTestRule() {
        this(new FakeAccountManagerFacade(), new FakeAccountInfoService());
    }

    public AccountManagerTestRule(@NonNull FakeAccountManagerFacade fakeAccountManagerFacade) {
        this(fakeAccountManagerFacade, new FakeAccountInfoService());
    }

    public AccountManagerTestRule(
            @NonNull FakeAccountManagerFacade fakeAccountManagerFacade,
            @Nullable FakeAccountInfoService fakeAccountInfoService) {
        mFakeAccountManagerFacade = fakeAccountManagerFacade;
        mFakeAccountInfoService = fakeAccountInfoService;
    }

    @Override
    public Statement apply(Statement statement, Description description) {
        return new Statement() {
            @Override
            public void evaluate() throws Throwable {
                setUpRule();
                try {
                    statement.evaluate();
                } finally {
                    tearDownRule();
                }
            }
        };
    }

    /** Sets up the AccountManagerFacade mock. */
    public void setUpRule() {
        TestThreadUtils.runOnUiThreadBlocking(
                () -> {
                    if (mFakeAccountInfoService != null) {
                        AccountInfoServiceProvider.setInstanceForTests(mFakeAccountInfoService);
                    }
                });
        AccountManagerFacadeProvider.setInstanceForTests(mFakeAccountManagerFacade);
    }

    /** Tears down the AccountManagerFacade mock and signs out if user is signed in. */
    public void tearDownRule() {
        AccountManagerFacadeProvider.resetInstanceForTests();
        if (mFakeAccountInfoService != null) AccountInfoServiceProvider.resetForTests();
    }

    /**
     * Adds an observer that detects changes in the account state propagated by the
     * IdentityManager object.
     */
    public void observeIdentityManager(IdentityManager identityManager) {
        identityManager.addObserver(mFakeAccountInfoService);
    }

    // TODO(crbug.com/40890215): Remove deprecated `addAccount` overloads.
    /**
     * Adds an account of the given accountName to the fake AccountManagerFacade.
     *
     * @return The CoreAccountInfo for the account added.
     */
    @Deprecated
    public AccountInfo addAccount(String accountName) {
        return addAccount(accountName, new AccountCapabilities(new HashMap<>()));
    }

    /**
     * Adds an account of the given accountName and capabilities to the fake AccountManagerFacade.
     *
     * @return The CoreAccountInfo for the account added.
     */
    @Deprecated
    public AccountInfo addAccount(String accountName, @NonNull AccountCapabilities capabilities) {
        final String baseName = accountName.split("@", 2)[0];
        return addAccount(
                accountName, baseName + ".full", baseName + ".given", createAvatar(), capabilities);
    }

    /**
     * Adds an account of the given email and name to the fake AccountManagerFacade.
     *
     * @return The CoreAccountInfo for the account added.
     */
    @Deprecated
    public AccountInfo addAccount(String email, String baseName) {
        return addAccount(
                email,
                baseName + ".full",
                baseName + ".given",
                createAvatar(),
                new AccountCapabilities(new HashMap<>()));
    }

    /**
     * Adds an account of the given accountName and capabilities to the fake AccountManagerFacade.
     *
     * @return The CoreAccountInfo for the account added.
     */
    @Deprecated
    public AccountInfo addAccount(
            String accountName, String baseName, @NonNull AccountCapabilities capabilities) {
        return addAccount(
                accountName, baseName + ".full", baseName + ".given", createAvatar(), capabilities);
    }

    /**
     * Adds an account to the fake AccountManagerFacade and {@link AccountInfo} to {@link
     * FakeAccountInfoService}.
     */
    @Deprecated
    public AccountInfo addAccount(
            String email, String fullName, String givenName, @Nullable Bitmap avatar) {
        return addAccount(
                email, fullName, givenName, avatar, new AccountCapabilities(new HashMap<>()));
    }

    /**
     * Adds an account to the fake AccountManagerFacade and {@link AccountInfo} to {@link
     * FakeAccountInfoService}.
     */
    @Deprecated
    public AccountInfo addAccount(
            String email,
            String fullName,
            String givenName,
            @Nullable Bitmap avatar,
            @NonNull AccountCapabilities capabilities) {
        String gaiaId = FakeAccountManagerFacade.toGaiaId(email);
        AccountInfo accountInfo =
                new AccountInfo(
                        new CoreAccountId(gaiaId),
                        email,
                        gaiaId,
                        fullName,
                        givenName,
                        avatar,
                        capabilities);
        addAccount(accountInfo);
        return accountInfo;
    }

    /**
     * Adds an account to the fake AccountManagerFacade and {@link AccountInfo} to {@link
     * FakeAccountInfoService}.
     */
    public void addAccount(AccountInfo accountInfo) {
        mFakeAccountManagerFacade.addAccount(accountInfo);
        // TODO(crbug.com/40234741): Revise this test rule and remove the condition here.
        if (mFakeAccountInfoService != null) mFakeAccountInfoService.addAccountInfo(accountInfo);
    }

    /**
     * Sets the result for the next add account flow.
     *
     * @param result The activity result to return when the intent is launched
     * @param newAccountName The account name to return when the intent is launched
     */
    public void setResultForNextAddAccountFlow(int result, @Nullable String newAccountName) {
        setResultForNextAddAccountFlow(result, newAccountName, false);
    }

    /**
     * Sets the result for the next add account flow.
     *
     * @param result The activity result to return when the intent is launched
     * @param newAccountName The account name to return when the intent is launched
     * @param isMinorModeEnabled The account be subject to minor mode restrictions
     */
    public void setResultForNextAddAccountFlow(
            int result, @Nullable String newAccountName, boolean isMinorModeEnabled) {
        // TODO(crbug.com/343872217) To be replaced with a single method that takes {@link
        // AccountInfo}
        mFakeAccountManagerFacade.setResultForNextAddAccountFlow(
                result, newAccountName, isMinorModeEnabled);
    }

    /** Removes an account with the given {@link CoreAccountId}. */
    public void removeAccount(CoreAccountId accountId) {
        mFakeAccountManagerFacade.removeAccount(accountId);
    }

    /** See {@link FakeAccountManagerFacade#blockGetCoreAccountInfos(boolean)}. */
    public FakeAccountManagerFacade.UpdateBlocker blockGetCoreAccountInfosUpdate(
            boolean populateCache) {
        return mFakeAccountManagerFacade.blockGetCoreAccountInfos(populateCache);
    }

    /** Converts an account email to its corresponding CoreAccountInfo object. */
    public CoreAccountInfo toCoreAccountInfo(String accountEmail) {
        String accountGaiaId = mFakeAccountManagerFacade.getAccountGaiaId(accountEmail);
        return CoreAccountInfo.createFromEmailAndGaiaId(accountEmail, accountGaiaId);
    }

    /**
     * Creates an email used to identify child accounts in tests.
     * A child-specific prefix will be appended to the base name so that the created account
     * will be considered as {@link ChildAccountStatus#REGULAR_CHILD} in
     * {@link FakeAccountManagerFacade}.
     */
    public static String generateChildEmail(String baseName) {
        return FakeAccountManagerFacade.generateChildEmail(baseName);
    }

    /** Returns an avatar image created from test resource. */
    protected static Bitmap createAvatar() {
        Drawable drawable =
                AppCompatResources.getDrawable(
                        ContextUtils.getApplicationContext(), R.drawable.test_profile_picture);
        Bitmap bitmap =
                Bitmap.createBitmap(
                        drawable.getIntrinsicWidth(),
                        drawable.getIntrinsicHeight(),
                        Bitmap.Config.ARGB_8888);
        Canvas canvas = new Canvas(bitmap);
        drawable.setBounds(0, 0, canvas.getWidth(), canvas.getHeight());
        drawable.draw(canvas);
        return bitmap;
    }

    /**
     * Resolves the minor mode of {@param accountInfo} to restricted, so that the UI will be safe to
     * show to minors.
     */
    public void resolveMinorModeToRestricted(CoreAccountId accountId) {
        // TODO(b/343384614): append instead of overriding
        overrideCapabilities(accountId, MINOR_MODE_REQUIRED);
    }

    /**
     * Resolves the minor mode of {@param accountInfo} to unrestricted, so that the UI will not have
     * any minor restrictions.
     */
    public void resolveMinorModeToUnrestricted(CoreAccountId accountId) {
        // TODO(b/343384614): append instead of overriding
        overrideCapabilities(accountId, MINOR_MODE_NOT_REQUIRED);
    }

    private void overrideCapabilities(CoreAccountId accountId, AccountCapabilities capabilities) {
        mFakeAccountManagerFacade.setAccountCapabilities(accountId, capabilities);
    }
}
