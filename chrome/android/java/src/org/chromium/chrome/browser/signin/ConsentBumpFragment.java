// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.signin;

import android.os.Bundle;
import android.support.annotation.IdRes;
import android.support.annotation.StringRes;
import android.support.v4.app.FragmentTransaction;
import android.view.ViewGroup;

import org.chromium.chrome.R;

/**
 * This fragment implements the consent bump screen. This is a variation of the sign-in screen that
 * provides an easy option to enable all features gated by unified consent. This screen is shown to
 * users who are already signed in and have Sync enabled, so there's no need to sign in users.
 */
public class ConsentBumpFragment extends SigninFragmentBase {
    public static Bundle createArguments() {
        return createArgumentsForConsentBumpFlow();
    }

    // Every fragment must have a public default constructor.
    public ConsentBumpFragment() {}

    @Override
    protected Bundle getSigninArguments() {
        return getArguments();
    }

    @Override
    protected void onSigninRefused() {
        FragmentTransaction transaction = getFragmentManager().beginTransaction();
        transaction.setCustomAnimations(R.anim.slide_in_right, R.anim.slide_out_left,
                android.R.anim.slide_in_left, android.R.anim.slide_out_right);
        // Get the id of the view that contains this fragment and replace the fragment.
        @IdRes int containerId = ((ViewGroup) getView().getParent()).getId();
        transaction.replace(containerId, new ConsentBumpMoreOptionsFragment());
        transaction.addToBackStack(null);
        transaction.commit();
    }

    @Override
    protected void onSigninAccepted(String accountName, boolean isDefaultAccount,
            boolean settingsClicked, Runnable callback) {
        // TODO(https://crbug.com/869426): Save the consent state.
        getActivity().finish();
    }

    @Override
    @StringRes
    protected int getNegativeButtonTextId() {
        return R.string.consent_bump_more_options;
    }
}
