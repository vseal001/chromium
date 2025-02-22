// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.components.gcm_driver;

import org.chromium.base.AsyncTask;
import org.chromium.base.Log;
import org.chromium.base.ThreadUtils;
import org.chromium.base.VisibleForTesting;
import org.chromium.base.annotations.CalledByNative;
import org.chromium.base.annotations.JNINamespace;

import java.io.IOException;

/**
 * This class is the Java counterpart to the C++ GCMDriverAndroid class.
 * It uses Android's Java GCM APIs to implement GCM registration etc, and
 * sends back GCM messages over JNI.
 *
 * Threading model: all calls to/from C++ happen on the UI thread.
 */
@JNINamespace("gcm")
public class GCMDriver {
    private static final String TAG = "GCMDriver";

    // The instance of GCMDriver currently owned by a C++ GCMDriverAndroid, if any.
    private static GCMDriver sInstance;

    private long mNativeGCMDriverAndroid;
    private GoogleCloudMessagingSubscriber mSubscriber;

    private GCMDriver(long nativeGCMDriverAndroid) {
        mNativeGCMDriverAndroid = nativeGCMDriverAndroid;
        mSubscriber = new GoogleCloudMessagingV2();
    }

    /**
     * Create a GCMDriver object, which is owned by GCMDriverAndroid
     * on the C++ side.
     *  @param nativeGCMDriverAndroid The C++ object that owns us.
     *
     */
    @CalledByNative
    private static GCMDriver create(long nativeGCMDriverAndroid) {
        if (sInstance != null) {
            throw new IllegalStateException("Already instantiated");
        }
        sInstance = new GCMDriver(nativeGCMDriverAndroid);
        return sInstance;
    }

    /**
     * Called when our C++ counterpart is deleted. Clear the handle to our
     * native C++ object, ensuring it's never called.
     */
    @CalledByNative
    private void destroy() {
        assert sInstance == this;
        sInstance = null;
        mNativeGCMDriverAndroid = 0;
    }

    @CalledByNative
    private void register(final String appId, final String senderId) {
        new AsyncTask<String>() {
            @Override
            protected String doInBackground() {
                try {
                    String subtype = appId;
                    String registrationId = mSubscriber.subscribe(senderId, subtype, null);
                    return registrationId;
                } catch (IOException ex) {
                    Log.w(TAG, "GCM subscription failed for " + appId + ", " + senderId, ex);
                    return "";
                }
            }
            @Override
            protected void onPostExecute(String registrationId) {
                nativeOnRegisterFinished(mNativeGCMDriverAndroid, appId, registrationId,
                                         !registrationId.isEmpty());
            }
        }
                .execute();
    }

    @CalledByNative
    private void unregister(final String appId, final String senderId) {
        new AsyncTask<Boolean>() {
            @Override
            protected Boolean doInBackground() {
                try {
                    String subtype = appId;
                    mSubscriber.unsubscribe(senderId, subtype, null);
                    return true;
                } catch (IOException ex) {
                    Log.w(TAG, "GCM unsubscription failed for " + appId + ", " + senderId, ex);
                    return false;
                }
            }

            @Override
            protected void onPostExecute(Boolean success) {
                nativeOnUnregisterFinished(mNativeGCMDriverAndroid, appId, success);
            }
        }
                .execute();
    }

    // The caller of this function is responsible for ensuring the browser process is initialized.
    public static void dispatchMessage(GCMMessage message) {
        ThreadUtils.assertOnUiThread();

        if (sInstance == null) {
            throw new RuntimeException("Failed to instantiate GCMDriver.");
        }

        sInstance.nativeOnMessageReceived(sInstance.mNativeGCMDriverAndroid, message.getAppId(),
                message.getSenderId(), message.getCollapseKey(), message.getRawData(),
                message.getDataKeysAndValuesArray());
    }

    @VisibleForTesting
    public static void overrideSubscriberForTesting(GoogleCloudMessagingSubscriber subscriber) {
        assert sInstance != null;
        assert subscriber != null;
        sInstance.mSubscriber = subscriber;
    }

    private native void nativeOnRegisterFinished(long nativeGCMDriverAndroid, String appId,
            String registrationId, boolean success);
    private native void nativeOnUnregisterFinished(long nativeGCMDriverAndroid, String appId,
            boolean success);
    private native void nativeOnMessageReceived(long nativeGCMDriverAndroid, String appId,
            String senderId, String collapseKey, byte[] rawData, String[] dataKeysAndValues);
}
