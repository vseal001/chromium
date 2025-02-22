// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.media;

import android.app.Activity;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.support.annotation.IntDef;

import org.chromium.base.metrics.CachedMetrics;
import org.chromium.chrome.browser.util.IntentUtils;

import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;

/**
 * The MediaLauncherActivity handles media-viewing Intents from other apps. It takes the given
 * content:// URI from the Intent and properly routes it to a media-viewing CustomTabActivity.
 */
public class MediaLauncherActivity extends Activity {
    // UMA histogram values for media types the user can open.
    // Keep in sync with MediaLauncherActivityMediaType enum in enums.xml.
    @IntDef({MediaType.AUDIO, MediaType.IMAGE, MediaType.VIDEO})
    @Retention(RetentionPolicy.SOURCE)
    @interface MediaType {
        int AUDIO = 0;
        int IMAGE = 1;
        int VIDEO = 2;
        int UNKNOWN = 3;
        int NUM_ENTRIES = 4;
    }

    private static CachedMetrics.EnumeratedHistogramSample sMediaTypeHistogram =
            new CachedMetrics.EnumeratedHistogramSample(
                    "MediaLauncherActivity.MediaType", MediaType.NUM_ENTRIES);

    @Override
    public void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        Intent input = IntentUtils.sanitizeIntent(getIntent());
        Uri contentUri = input.getData();
        String mimeType = getContentResolver().getType(contentUri);
        int mediaType = MediaViewerUtils.getMediaTypeFromMIMEType(mimeType);

        sMediaTypeHistogram.record(mediaType);

        if (mediaType == MediaType.UNKNOWN) {
            // With our intent-filter, we should only receive implicit intents with media MIME
            // types. If we receive a non-media MIME type, it is likely a malicious explicit intent,
            // so we should not proceed.
            finish();
            return;
        }

        // TODO(https://crbug.com/800880): Determine file:// URI when possible.
        Intent intent = MediaViewerUtils.getMediaViewerIntent(
                contentUri, contentUri, mimeType, false /* allowExternalAppHandlers */);
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
        startActivity(intent);

        finish();
    }
}
