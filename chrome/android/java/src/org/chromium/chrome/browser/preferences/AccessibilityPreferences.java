// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.preferences;

import android.os.Bundle;
import android.preference.Preference;
import android.preference.Preference.OnPreferenceChangeListener;
import android.preference.PreferenceFragment;
import android.widget.ListView;

import org.chromium.chrome.R;
import org.chromium.chrome.browser.ChromeFeatureList;
import org.chromium.chrome.browser.accessibility.FontSizePrefs;
import org.chromium.chrome.browser.accessibility.FontSizePrefs.FontSizePrefsObserver;
import org.chromium.chrome.browser.util.AccessibilityUtil;

import java.text.NumberFormat;

/**
 * Fragment to keep track of all the accessibility related preferences.
 */
public class AccessibilityPreferences extends PreferenceFragment
        implements OnPreferenceChangeListener {

    static final String PREF_TEXT_SCALE = "text_scale";
    static final String PREF_FORCE_ENABLE_ZOOM = "force_enable_zoom";
    static final String PREF_READER_FOR_ACCESSIBILITY = "reader_for_accessibility";

    private NumberFormat mFormat;
    private FontSizePrefs mFontSizePrefs;

    private TextScalePreference mTextScalePref;
    private SeekBarLinkedCheckBoxPreference mForceEnableZoomPref;
    private ChromeBaseCheckBoxPreference mReaderForAccessibilityPref;
    private ChromeBaseCheckBoxPreference mAccessibilityTabSwitcherPref;

    private FontSizePrefsObserver mFontSizePrefsObserver = new FontSizePrefsObserver() {
        @Override
        public void onFontScaleFactorChanged(float fontScaleFactor, float userFontScaleFactor) {
            updateTextScaleSummary(userFontScaleFactor);
        }

        @Override
        public void onForceEnableZoomChanged(boolean enabled) {
            mForceEnableZoomPref.setChecked(enabled);
        }
    };

    @Override
    public void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        getActivity().setTitle(R.string.prefs_accessibility);
        PreferenceUtils.addPreferencesFromResource(this, R.xml.accessibility_preferences);

        mFormat = NumberFormat.getPercentInstance();
        mFontSizePrefs = FontSizePrefs.getInstance(getActivity());

        mTextScalePref = (TextScalePreference) findPreference(PREF_TEXT_SCALE);
        mTextScalePref.setOnPreferenceChangeListener(this);

        mForceEnableZoomPref = (SeekBarLinkedCheckBoxPreference) findPreference(
                PREF_FORCE_ENABLE_ZOOM);
        mForceEnableZoomPref.setOnPreferenceChangeListener(this);
        mForceEnableZoomPref.setLinkedSeekBarPreference(mTextScalePref);

        mReaderForAccessibilityPref =
                (ChromeBaseCheckBoxPreference) findPreference(PREF_READER_FOR_ACCESSIBILITY);
        if (ChromeFeatureList.isEnabled(ChromeFeatureList.ALLOW_READER_FOR_ACCESSIBILITY)) {
            mReaderForAccessibilityPref.setChecked(PrefServiceBridge.getInstance().getBoolean(
                    Pref.READER_FOR_ACCESSIBILITY_ENABLED));
            mReaderForAccessibilityPref.setOnPreferenceChangeListener(this);
        } else {
            getPreferenceScreen().removePreference(mReaderForAccessibilityPref);
        }

        mAccessibilityTabSwitcherPref = (ChromeBaseCheckBoxPreference) findPreference(
                ChromePreferenceManager.ACCESSIBILITY_TAB_SWITCHER);
        if (AccessibilityUtil.isAccessibilityEnabled()) {
            mAccessibilityTabSwitcherPref.setChecked(
                    ChromePreferenceManager.getInstance().readBoolean(
                            ChromePreferenceManager.ACCESSIBILITY_TAB_SWITCHER, true));
        } else {
            getPreferenceScreen().removePreference(mAccessibilityTabSwitcherPref);
        }
    }

    @Override
    public void onActivityCreated(Bundle savedInstanceState) {
        super.onActivityCreated(savedInstanceState);

        ((ListView) getView().findViewById(android.R.id.list)).setItemsCanFocus(true);
        ((ListView) getView().findViewById(android.R.id.list)).setDivider(null);
    }

    @Override
    public void onStart() {
        super.onStart();
        updateValues();
        mTextScalePref.startObservingFontPrefs();
        mFontSizePrefs.addObserver(mFontSizePrefsObserver);
    }

    @Override
    public void onStop() {
        mTextScalePref.stopObservingFontPrefs();
        mFontSizePrefs.removeObserver(mFontSizePrefsObserver);
        super.onStop();
    }

    private void updateValues() {
        float userFontScaleFactor = mFontSizePrefs.getUserFontScaleFactor();
        mTextScalePref.setValue(userFontScaleFactor);
        updateTextScaleSummary(userFontScaleFactor);

        mForceEnableZoomPref.setChecked(mFontSizePrefs.getForceEnableZoom());
    }

    private void updateTextScaleSummary(float userFontScaleFactor) {
        mTextScalePref.setSummary(mFormat.format(userFontScaleFactor));
    }

    @Override
    public boolean onPreferenceChange(Preference preference, Object newValue) {
        if (PREF_TEXT_SCALE.equals(preference.getKey())) {
            mFontSizePrefs.setUserFontScaleFactor((Float) newValue);
        } else if (PREF_FORCE_ENABLE_ZOOM.equals(preference.getKey())) {
            mFontSizePrefs.setForceEnableZoomFromUser((Boolean) newValue);
        } else if (PREF_READER_FOR_ACCESSIBILITY.equals(preference.getKey())) {
            PrefServiceBridge.getInstance().setBoolean(
                    Pref.READER_FOR_ACCESSIBILITY_ENABLED, (Boolean) newValue);
        }
        return true;
    }
}
