// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.media.router;

import org.chromium.base.annotations.CalledByNative;
import org.chromium.base.annotations.JNINamespace;

/**
 * A wrapper around a FlingingController that allows the native code to use it
 * See chrome/browser/media/android/remote/flinging_controller_bridge.h for the
 * corresponding native code.
 */
@JNINamespace("media_router")
public class FlingingControllerBridge {
    private final FlingingController mFlingingController;

    public FlingingControllerBridge(FlingingController flingingController) {
        mFlingingController = flingingController;
    }

    @CalledByNative
    public void play() {
        mFlingingController.getMediaController().play();
    }

    @CalledByNative
    public void pause() {
        mFlingingController.getMediaController().pause();
    }

    @CalledByNative
    public void setMute(boolean mute) {
        mFlingingController.getMediaController().setMute(mute);
    }

    @CalledByNative
    public void setVolume(float volume) {
        mFlingingController.getMediaController().setVolume(volume);
    }

    @CalledByNative
    public void seek(long positionInMs) {
        mFlingingController.getMediaController().seek(positionInMs);
    }
}
