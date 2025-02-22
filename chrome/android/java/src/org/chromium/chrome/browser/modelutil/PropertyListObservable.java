// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
package org.chromium.chrome.browser.modelutil;

import android.support.annotation.Nullable;

import java.util.Collection;

/**
 * Represents a list of (property-)observable items, and notifies about changes to any of its items.
 *
 * @param <T> The type of item in the list.
 * @param <P> The property key type for {@code T} to be used as payload for partial updates.
 */
public class PropertyListObservable<T extends PropertyObservable<P>, P>
        extends SimpleListObservableBase<T, P> {
    private final PropertyObservable.PropertyObserver<P> mPropertyObserver =
            this::onPropertyChanged;

    @Override
    public void add(T item) {
        super.add(item);
        item.addObserver(mPropertyObserver);
    }

    @Override
    public void remove(T item) {
        item.removeObserver(mPropertyObserver);
        super.remove(item);
    }

    @Override
    public void update(int index, T item) {
        get(index).removeObserver(mPropertyObserver);
        super.update(index, item);
        item.addObserver(mPropertyObserver);
    }

    @Override
    public void set(Collection<T> newItems) {
        for (T item : this) {
            item.removeObserver(mPropertyObserver);
        }
        super.set(newItems);
        for (T item : newItems) {
            item.addObserver(mPropertyObserver);
        }
    }

    private void onPropertyChanged(PropertyObservable<P> source, @Nullable P propertyKey) {
        notifyItemChanged(indexOf(source), propertyKey);
    }
}
