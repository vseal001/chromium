// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.input;

import android.support.test.InstrumentationRegistry;
import android.support.test.filters.LargeTest;

import org.junit.Assert;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;

import org.chromium.base.ThreadUtils;
import org.chromium.base.test.util.CommandLineFlags;
import org.chromium.base.test.util.Feature;
import org.chromium.base.test.util.RetryOnFailure;
import org.chromium.base.test.util.UrlUtils;
import org.chromium.chrome.browser.ChromeActivity;
import org.chromium.chrome.browser.ChromeSwitches;
import org.chromium.chrome.browser.WebContentsFactory;
import org.chromium.chrome.test.ChromeActivityTestRule;
import org.chromium.chrome.test.ChromeJUnit4ClassRunner;
import org.chromium.components.embedder_support.view.ContentView;
import org.chromium.content.browser.test.util.Criteria;
import org.chromium.content.browser.test.util.CriteriaHelper;
import org.chromium.content.browser.test.util.DOMUtils;
import org.chromium.content_public.browser.WebContents;
import org.chromium.ui.base.ActivityWindowAndroid;
import org.chromium.ui.base.ViewAndroidDelegate;

import java.util.concurrent.ExecutionException;

/**
 * Test the select popup and how it interacts with another WebContents.
 */
@RunWith(ChromeJUnit4ClassRunner.class)
@CommandLineFlags.Add({ChromeSwitches.DISABLE_FIRST_RUN_EXPERIENCE})
public class SelectPopupOtherContentViewTest {
    @Rule
    public ChromeActivityTestRule<ChromeActivity> mActivityTestRule =
            new ChromeActivityTestRule<>(ChromeActivity.class);

    private static final String SELECT_URL = UrlUtils.encodeHtmlDataUri(
            "<html><body>"
            + "Which animal is the strongest:<br/>"
            + "<select id=\"select\">"
            + "<option>Black bear</option>"
            + "<option>Polar bear</option>"
            + "<option>Grizzly</option>"
            + "<option>Tiger</option>"
            + "<option>Lion</option>"
            + "<option>Gorilla</option>"
            + "<option>Chipmunk</option>"
            + "</select>"
            + "</body></html>");

    private class PopupShowingCriteria extends Criteria {
        public PopupShowingCriteria() {
            super("The select popup did not show up on click.");
        }

        @Override
        public boolean isSatisfied() {
            return isSelectPopupVisibleOnUiThread();
        }
    }

    private boolean isSelectPopupVisibleOnUiThread() {
        try {
            return ThreadUtils.runOnUiThreadBlocking(
                    () -> mActivityTestRule.getWebContents().isSelectPopupVisibleForTesting());
        } catch (ExecutionException e) {
            throw new RuntimeException(e);
        }
    }

    /**
     * Tests that the showing select popup does not get closed because an unrelated ContentView
     * gets destroyed.
     *
     */
    @Test
    @LargeTest
    @Feature({"Browser"})
    @RetryOnFailure
    public void testPopupNotClosedByOtherContentView()
            throws InterruptedException, Exception, Throwable {
        // Load the test page.
        mActivityTestRule.startMainActivityWithURL(SELECT_URL);

        // Once clicked, the popup should show up.
        DOMUtils.clickNode(mActivityTestRule.getWebContents(), "select");
        CriteriaHelper.pollInstrumentationThread(new PopupShowingCriteria());

        // Now create and destroy a different WebContents.
        ThreadUtils.runOnUiThreadBlocking(new Runnable() {
            @Override
            public void run() {
                WebContents webContents = WebContentsFactory.createWebContents(false, false);
                ChromeActivity activity = mActivityTestRule.getActivity();

                ContentView cv = ContentView.createContentView(activity, webContents);
                webContents.initialize("", ViewAndroidDelegate.createBasicDelegate(cv), cv,
                        new ActivityWindowAndroid(activity),
                        WebContents.createDefaultInternalsHolder());
                webContents.destroy();
            }
        });

        // Process some more events to give a chance to the dialog to hide if it were to.
        InstrumentationRegistry.getInstrumentation().waitForIdleSync();

        // The popup should still be shown.
        Assert.assertTrue("The select popup got hidden by destroying of unrelated ContentViewCore.",
                isSelectPopupVisibleOnUiThread());
    }
}
