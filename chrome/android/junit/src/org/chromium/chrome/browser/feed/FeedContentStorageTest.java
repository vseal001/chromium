// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.feed;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;
import static org.mockito.Mockito.doAnswer;
import static org.mockito.Mockito.doNothing;
import static org.mockito.Mockito.eq;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;

import android.support.test.filters.SmallTest;

import com.google.android.libraries.feed.common.Result;
import com.google.android.libraries.feed.common.functional.Consumer;
import com.google.android.libraries.feed.host.storage.CommitResult;
import com.google.android.libraries.feed.host.storage.ContentMutation;

import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.ArgumentCaptor;
import org.mockito.Captor;
import org.mockito.Mock;
import org.mockito.MockitoAnnotations;
import org.mockito.invocation.InvocationOnMock;
import org.mockito.stubbing.Answer;
import org.robolectric.annotation.Config;

import org.chromium.base.Callback;
import org.chromium.base.test.BaseRobolectricTestRunner;
import org.chromium.chrome.browser.profiles.Profile;

import java.nio.charset.Charset;
import java.util.Arrays;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/**
 * Unit tests for {@link FeedContentStorage}.
 */
@RunWith(BaseRobolectricTestRunner.class)
@Config(manifest = Config.NONE)
public class FeedContentStorageTest {
    public static final String CONTENT_KEY1 = "CONTENT_KEY_1";
    public static final String CONTENT_KEY2 = "CONTENT_KEY_2";
    public static final String CONTENT_KEY3 = "CONTENT_KEY_3";
    public static final byte[] CONTENT_DATA1 = "CONTENT_DATA_1".getBytes(Charset.forName("UTF-8"));
    public static final byte[] CONTENT_DATA2 = "CONTENT_DATA_2".getBytes(Charset.forName("UTF-8"));
    public static final byte[] CONTENT_DATA3 = "CONTENT_DATA_3".getBytes(Charset.forName("UTF-8"));

    @Mock
    private FeedStorageBridge mBridge;
    @Mock
    private Consumer<CommitResult> mBooleanConsumer;
    @Mock
            private Consumer < Result < List<String>>> mListConsumer;
    @Mock
            private Consumer < Result < Map<String, byte[]>>> mMapConsumer;
    @Mock
    private Profile mProfile;
    @Captor
    private ArgumentCaptor<CommitResult> mCommitResultCaptor;
    @Captor
            private ArgumentCaptor < Result < List<String>>> mStringListCaptor;
    @Captor
            private ArgumentCaptor < Result < Map<String, byte[]>>> mMapCaptor;
    @Captor
    private ArgumentCaptor<String> mStringArgument;
    @Captor
    private ArgumentCaptor<List<String>> mStringListArgument;
    @Captor
    private ArgumentCaptor<String[]> mStringArrayArgument;
    @Captor
    private ArgumentCaptor<byte[][]> mByteArrayOfArrayArgument;
    @Captor
    private ArgumentCaptor<Callback<Boolean>> mBooleanCallbackArgument;
    @Captor
            private ArgumentCaptor < Callback < List<String>>> mStringListCallbackArgument;
    @Captor
            private ArgumentCaptor < Callback < Map<String, byte[]>>> mMapCallbackArgument;

    private FeedContentStorage mContentStorage;

    private Answer<Void> createMapAnswer(Map<String, byte[]> map) {
        return new Answer<Void>() {
            @Override
            public Void answer(InvocationOnMock invocation) {
                mMapCallbackArgument.getValue().onResult(map);
                return null;
            }
        };
    }

    private Answer<Void> createStringListAnswer(List<String> stringList) {
        return new Answer<Void>() {
            @Override
            public Void answer(InvocationOnMock invocation) {
                mStringListCallbackArgument.getValue().onResult(stringList);
                return null;
            }
        };
    }

    private Answer<Void> createBooleanAnswer(Boolean bool) {
        return new Answer<Void>() {
            @Override
            public Void answer(InvocationOnMock invocation) {
                mBooleanCallbackArgument.getValue().onResult(bool);
                return null;
            }
        };
    }

    private void verifyMapResult(Map<String, byte[]> expectedMap, boolean expectedBoolean,
            Result<Map<String, byte[]>> actualResult) {
        assertEquals(expectedBoolean, actualResult.isSuccessful());
        if (!expectedBoolean) return;

        Map<String, byte[]> actualMap = actualResult.getValue();
        assertEquals(expectedMap.size(), actualMap.size());
        for (Map.Entry<String, byte[]> entry : expectedMap.entrySet()) {
            assertTrue(actualMap.containsKey(entry.getKey()));
            assertEquals(entry.getValue(), actualMap.get(entry.getKey()));
        }
    }

    private void verifyStringListResult(
            List<String> expectedList, boolean expectedBoolean, Result<List<String>> actualResult) {
        assertEquals(expectedBoolean, actualResult.isSuccessful());
        if (!expectedBoolean) return;

        List<String> actualList = actualResult.getValue();
        assertEquals(expectedList.size(), actualList.size());
        for (String expectedString : expectedList) {
            assertTrue(actualList.contains(expectedString));
        }
    }

    @Before
    public void setUp() throws Exception {
        MockitoAnnotations.initMocks(this);
        doNothing().when(mBridge).init(eq(mProfile));
        mBridge.init(mProfile);
        mContentStorage = new FeedContentStorage(mBridge);
    }

    @Test
    @SmallTest
    public void getTest() {
        Map<String, byte[]> answerMap = new HashMap<>();
        answerMap.put(CONTENT_KEY1, CONTENT_DATA1);
        Answer<Void> answer = createMapAnswer(answerMap);
        doAnswer(answer).when(mBridge).loadContent(
                mStringListArgument.capture(), mMapCallbackArgument.capture());
        List<String> keys = Arrays.asList(CONTENT_KEY1, CONTENT_KEY2);

        mContentStorage.get(keys, mMapConsumer);
        verify(mBridge, times(1)).loadContent(eq(keys), mMapCallbackArgument.capture());
        verify(mMapConsumer, times(1)).accept(mMapCaptor.capture());
        verifyMapResult(answerMap, true, mMapCaptor.getValue());
    }

    @Test
    @SmallTest
    public void getAllTest() {
        Map<String, byte[]> answerMap = new HashMap<>();
        answerMap.put(CONTENT_KEY1, CONTENT_DATA1);
        answerMap.put(CONTENT_KEY2, CONTENT_DATA2);
        answerMap.put(CONTENT_KEY3, CONTENT_DATA3);
        Answer<Void> answer = createMapAnswer(answerMap);
        doAnswer(answer).when(mBridge).loadContentByPrefix(
                mStringArgument.capture(), mMapCallbackArgument.capture());

        mContentStorage.getAll(CONTENT_KEY1, mMapConsumer);
        verify(mBridge, times(1))
                .loadContentByPrefix(eq(CONTENT_KEY1), mMapCallbackArgument.capture());
        verify(mMapConsumer, times(1)).accept(mMapCaptor.capture());
        verifyMapResult(answerMap, true, mMapCaptor.getValue());
    }

    @Test
    @SmallTest
    public void getAllKeysTest() {
        List<String> answerStrings = Arrays.asList(CONTENT_KEY1, CONTENT_KEY2, CONTENT_KEY3);
        Answer<Void> answer = createStringListAnswer(answerStrings);
        doAnswer(answer).when(mBridge).loadAllContentKeys(mStringListCallbackArgument.capture());

        mContentStorage.getAllKeys(mListConsumer);
        verify(mBridge, times(1)).loadAllContentKeys(mStringListCallbackArgument.capture());
        verify(mListConsumer, times(1)).accept(mStringListCaptor.capture());
        verifyStringListResult(answerStrings, true, mStringListCaptor.getValue());
    }

    @Test
    @SmallTest
    public void commitTest() {
        Answer<Void> answerSaveContent = createBooleanAnswer(true);
        doAnswer(answerSaveContent)
                .when(mBridge)
                .saveContent(mStringArrayArgument.capture(), mByteArrayOfArrayArgument.capture(),
                        mBooleanCallbackArgument.capture());

        Answer<Void> answerDeleteContent = createBooleanAnswer(true);
        doAnswer(answerDeleteContent)
                .when(mBridge)
                .deleteContent(mStringListArgument.capture(), mBooleanCallbackArgument.capture());

        Answer<Void> answerDeleteContentByPrefix = createBooleanAnswer(true);
        doAnswer(answerDeleteContentByPrefix)
                .when(mBridge)
                .deleteContentByPrefix(
                        mStringArgument.capture(), mBooleanCallbackArgument.capture());

        Answer<Void> answerDeleteAllContent = createBooleanAnswer(true);
        doAnswer(answerDeleteAllContent)
                .when(mBridge)
                .deleteAllContent(mBooleanCallbackArgument.capture());

        mContentStorage.commit(new ContentMutation.Builder()
                                       .upsert(CONTENT_KEY1, CONTENT_DATA1)
                                       .delete(CONTENT_KEY2)
                                       .deleteByPrefix(CONTENT_KEY3)
                                       .deleteAll()
                                       .build(),
                mBooleanConsumer);
        verify(mBridge, times(1))
                .saveContent(mStringArrayArgument.capture(), mByteArrayOfArrayArgument.capture(),
                        mBooleanCallbackArgument.capture());
        verify(mBridge, times(1))
                .deleteContent(mStringListArgument.capture(), mBooleanCallbackArgument.capture());
        verify(mBridge, times(1))
                .deleteContentByPrefix(
                        mStringArgument.capture(), mBooleanCallbackArgument.capture());
        verify(mBridge, times(1)).deleteAllContent(mBooleanCallbackArgument.capture());

        verify(mBooleanConsumer, times(1)).accept(mCommitResultCaptor.capture());
        CommitResult commitResult = mCommitResultCaptor.getValue();
        assertEquals(CommitResult.SUCCESS, commitResult);
    }
}
