// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.signin;

import android.os.Bundle;
import android.support.v4.app.Fragment;
import android.support.v4.app.FragmentManager;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;

import org.chromium.base.ApiCompatibilityUtils;
import org.chromium.chrome.R;
import org.chromium.chrome.browser.widget.RadioButtonWithDescription;

/**
 * This fragment implements the advanced consent bump screen. This screen lets users to enable or
 * customize personalized and non-personalized services.
 */
public class ConsentBumpMoreOptionsFragment extends Fragment {
    private static final String TAG = "ConsentBumpMoreOptionsFragment";

    public ConsentBumpMoreOptionsFragment() {}

    @Override
    public View onCreateView(
            LayoutInflater inflater, ViewGroup container, Bundle savedInstanceState) {
        View view = inflater.inflate(R.layout.consent_bump_more_options_view, container, false);

        RadioButtonWithDescription noChanges = view.findViewById(R.id.consent_bump_no_changes);
        noChanges.setDescriptionText(getText(R.string.consent_bump_no_changes_description));
        RadioButtonWithDescription turnOn = view.findViewById(R.id.consent_bump_turn_on);
        turnOn.setDescriptionText(getText(R.string.consent_bump_turn_on_description));

        Button button = view.findViewById(R.id.back_button);
        button.setOnClickListener(btn -> {
            FragmentManager fragmentManager =
                    ApiCompatibilityUtils.requireNonNull(getFragmentManager());
            fragmentManager.popBackStack();
        });
        return view;
    }
}
