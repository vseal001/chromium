// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.media;

import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.net.Uri;
import android.os.Build;
import android.support.test.InstrumentationRegistry;
import android.support.test.filters.SmallTest;

import org.junit.After;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;

import org.chromium.base.test.util.CommandLineFlags;
import org.chromium.chrome.browser.ChromeSwitches;
import org.chromium.chrome.browser.customtabs.SeparateTaskCustomTabActivity;
import org.chromium.chrome.browser.customtabs.SeparateTaskCustomTabActivity0;
import org.chromium.chrome.browser.tab.Tab;
import org.chromium.chrome.test.ChromeJUnit4ClassRunner;
import org.chromium.chrome.test.MultiActivityTestRule;
import org.chromium.chrome.test.TestContentProvider;
import org.chromium.chrome.test.util.ActivityUtils;
import org.chromium.content.browser.test.util.Criteria;
import org.chromium.content.browser.test.util.CriteriaHelper;

import java.util.concurrent.Callable;

/**
 * Integration test suite for the MediaLauncherActivity.
 */
@RunWith(ChromeJUnit4ClassRunner.class)
@CommandLineFlags.Add({ChromeSwitches.DISABLE_FIRST_RUN_EXPERIENCE})
public class MediaLauncherActivityTest {
    @Rule
    public MultiActivityTestRule mTestRule = new MultiActivityTestRule();

    private Context mContext;

    @Before
    public void setUp() {
        mContext = InstrumentationRegistry.getTargetContext();
        MediaViewerUtils.forceEnableMediaLauncherActivityForTest(mContext);
    }

    @After
    public void tearDown() {
        MediaViewerUtils.stopForcingEnableMediaLauncherActivityForTest(mContext);
    }

    @Test
    @SmallTest
    public void testHandleVideoIntent() throws Exception {
        String url = TestContentProvider.createContentUrl("media/test.mp4");
        expectMediaToBeHandled(url, "video/mp4");
    }

    @Test
    @SmallTest
    public void testHandleAudioIntent() throws Exception {
        String url = TestContentProvider.createContentUrl("media/audio.mp3");
        expectMediaToBeHandled(url, "audio/mp3");
    }

    @Test
    @SmallTest
    public void testHandleImageIntent() throws Exception {
        String url = TestContentProvider.createContentUrl("google.png");
        expectMediaToBeHandled(url, "image/png");
    }

    private void expectMediaToBeHandled(String url, String mimeType) throws Exception {
        Uri uri = Uri.parse(url);
        ComponentName componentName = new ComponentName(mContext, MediaLauncherActivity.class);
        Intent intent = new Intent(Intent.ACTION_VIEW, uri);
        intent.setDataAndType(uri, mimeType);
        intent.setComponent(componentName);
        intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        waitForCustomTabActivityToStart(new Callable<Void>() {
            @Override
            public Void call() {
                mContext.startActivity(intent);
                return null;
            }
        }, url);
    }

    private void waitForCustomTabActivityToStart(Callable<Void> trigger, String expectedUrl)
            throws Exception {
        SeparateTaskCustomTabActivity cta;
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP) {
            cta = ActivityUtils.waitForActivity(InstrumentationRegistry.getInstrumentation(),
                    SeparateTaskCustomTabActivity.class, trigger);
        } else {
            cta = ActivityUtils.waitForActivity(InstrumentationRegistry.getInstrumentation(),
                    SeparateTaskCustomTabActivity0.class, trigger);
        }

        CriteriaHelper.pollUiThread(Criteria.equals(expectedUrl, new Callable<String>() {
            @Override
            public String call() throws Exception {
                Tab tab = cta.getActivityTab();
                if (tab == null) return null;

                return tab.getUrl();
            }
        }));
    }
}
