// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chromecast.base;

import static org.hamcrest.Matchers.contains;
import static org.junit.Assert.assertThat;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.junit.runners.BlockJUnit4ClassRunner;

import org.chromium.chromecast.base.Inheritance.Base;
import org.chromium.chromecast.base.Inheritance.Derived;

import java.util.ArrayList;
import java.util.List;

/**
 * Miscellaneous tests for Observable.
 *
 * This includes advanced behaviors like watch-currying and correct use of generics.
 */
@RunWith(BlockJUnit4ClassRunner.class)
public class ObservableMiscellaneousTest {
    @Test
    public void testBeingTooCleverWithObserversAndInheritance() {
        Controller<Base> baseController = new Controller<>();
        Controller<Derived> derivedController = new Controller<>();
        List<String> result = new ArrayList<>();
        // Test that the same Observer object can observe Observables of different types, as
        // long as the Observer type is a superclass of both Observable types.
        Observer<Base> observer = (Base value) -> {
            result.add("enter: " + value.toString());
            return () -> result.add("exit: " + value.toString());
        };
        baseController.watch(observer);
        // Compile error if generics are wrong.
        derivedController.watch(observer);
        baseController.set(new Base());
        // The scope from the previous set() call will not be overridden because this is activating
        // a different Controller.
        derivedController.set(new Derived());
        // The Controller<Base> can be activated with an object that extends Base.
        baseController.set(new Derived());
        assertThat(
                result, contains("enter: Base", "enter: Derived", "exit: Base", "enter: Derived"));
    }

    @Test
    public void testWatchCurrying() {
        Controller<String> aState = new Controller<>();
        Controller<String> bState = new Controller<>();
        Controller<String> result = new Controller<>();
        ReactiveRecorder recorder = ReactiveRecorder.record(result);
        // I guess this makes .and() obsolete?
        aState.watch(a -> bState.watch(b -> {
            result.set("" + a + ", " + b);
            return () -> result.reset();
        }));
        aState.set("A");
        bState.set("B");
        recorder.verify().opened("A, B").end();
        aState.reset();
        recorder.verify().closed("A, B").end();
        aState.set("AA");
        recorder.verify().opened("AA, B").end();
        bState.reset();
        recorder.verify().closed("AA, B").end();
    }

    @Test
    public void testPowerUnlimitedPower() {
        Controller<Unit> aState = new Controller<>();
        Controller<Unit> bState = new Controller<>();
        Controller<Unit> cState = new Controller<>();
        Controller<Unit> dState = new Controller<>();
        List<String> result = new ArrayList<>();
        // Praise be to Haskell Curry.
        aState.watch(a -> bState.watch(b -> cState.watch(c -> dState.watch(d -> {
            result.add("it worked!");
            return () -> result.add("exit");
        }))));
        aState.set(Unit.unit());
        bState.set(Unit.unit());
        cState.set(Unit.unit());
        dState.set(Unit.unit());
        assertThat(result, contains("it worked!"));
        result.clear();
        aState.reset();
        assertThat(result, contains("exit"));
        result.clear();
        aState.set(Unit.unit());
        assertThat(result, contains("it worked!"));
        result.clear();
        bState.reset();
        assertThat(result, contains("exit"));
        result.clear();
        bState.set(Unit.unit());
        assertThat(result, contains("it worked!"));
        result.clear();
        cState.reset();
        assertThat(result, contains("exit"));
        result.clear();
        cState.set(Unit.unit());
        assertThat(result, contains("it worked!"));
        result.clear();
        dState.reset();
        assertThat(result, contains("exit"));
        result.clear();
        dState.set(Unit.unit());
        assertThat(result, contains("it worked!"));
    }

    // Any Scope implementation with a constructor of one argument can use a method reference to its
    // constructor as an Observer.
    private static class TransitionLogger implements Scope {
        public static final List<String> sResult = new ArrayList<>();
        private final String mData;

        public TransitionLogger(String data) {
            mData = data;
            sResult.add("enter: " + mData);
        }

        @Override
        public void close() {
            sResult.add("exit: " + mData);
        }
    }

    @Test
    public void testObserverWithAutoCloseableConstructor() {
        Controller<String> controller = new Controller<>();
        // You can use a constructor method reference in a watch() call.
        controller.watch(TransitionLogger::new);
        controller.set("a");
        controller.reset();
        assertThat(TransitionLogger.sResult, contains("enter: a", "exit: a"));
    }
}
