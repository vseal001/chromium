// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chromecast.base;

import static org.hamcrest.Matchers.contains;
import static org.hamcrest.Matchers.emptyIterable;
import static org.junit.Assert.assertThat;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.junit.runners.BlockJUnit4ClassRunner;

import java.util.ArrayList;
import java.util.List;

/**
 * Tests for Observable#andThen().
 */
@RunWith(BlockJUnit4ClassRunner.class)
public class ObservableAndThenTest {
    @Test
    public void testAndThenNotActivatedInitially() {
        Controller<String> aState = new Controller<>();
        Controller<String> bState = new Controller<>();
        List<String> result = new ArrayList<>();
        aState.andThen(bState).watch(
                Observers.onEnter((String a, String b) -> { result.add("a=" + a + ", b=" + b); }));
        assertThat(result, emptyIterable());
    }

    @Test
    public void testAndThenNotActivatedIfSecondBeforeFirst() {
        Controller<String> aState = new Controller<>();
        Controller<String> bState = new Controller<>();
        List<String> result = new ArrayList<>();
        aState.andThen(bState).watch(
                Observers.onEnter((String a, String b) -> { result.add("a=" + a + ", b=" + b); }));
        bState.set("b");
        aState.set("a");
        assertThat(result, emptyIterable());
    }

    @Test
    public void testAndThenActivatedIfFirstThenSecond() {
        Controller<String> aState = new Controller<>();
        Controller<String> bState = new Controller<>();
        List<String> result = new ArrayList<>();
        aState.andThen(bState).watch(
                Observers.onEnter((String a, String b) -> { result.add("a=" + a + ", b=" + b); }));
        aState.set("a");
        bState.set("b");
        assertThat(result, contains("a=a, b=b"));
    }

    @Test
    public void testAndThenActivated_plusBplusAminusBplusB() {
        Controller<String> aState = new Controller<>();
        Controller<String> bState = new Controller<>();
        List<String> result = new ArrayList<>();
        aState.andThen(bState).watch(
                Observers.onEnter((String a, String b) -> { result.add("a=" + a + ", b=" + b); }));
        bState.set("b");
        aState.set("a");
        bState.reset();
        bState.set("B");
        assertThat(result, contains("a=a, b=B"));
    }

    @Test
    public void testAndThenDeactivated_plusAplusBminusA() {
        Controller<String> aState = new Controller<>();
        Controller<String> bState = new Controller<>();
        List<String> result = new ArrayList<>();
        aState.andThen(bState).watch(
                Observers.onExit((String a, String b) -> { result.add("a=" + a + ", b=" + b); }));
        aState.set("A");
        bState.set("B");
        aState.reset();
        assertThat(result, contains("a=A, b=B"));
    }

    @Test
    public void testAndThenDeactivated_plusAplusBminusB() {
        Controller<String> aState = new Controller<>();
        Controller<String> bState = new Controller<>();
        List<String> result = new ArrayList<>();
        aState.andThen(bState).watch(
                Observers.onExit((String a, String b) -> { result.add("a=" + a + ", b=" + b); }));
        aState.set("A");
        bState.set("B");
        bState.reset();
        assertThat(result, contains("a=A, b=B"));
    }

    @Test
    public void testComposeAndThen() {
        Controller<Unit> aState = new Controller<>();
        Controller<Unit> bState = new Controller<>();
        Controller<Unit> cState = new Controller<>();
        Controller<Unit> dState = new Controller<>();
        Observable<Both<Unit, Unit>> aThenB = aState.andThen(bState);
        Observable<Both<Both<Unit, Unit>, Unit>> aThenBThenC = aThenB.andThen(cState);
        Observable<Both<Both<Both<Unit, Unit>, Unit>, Unit>> aThenBThenCThenD =
                aThenBThenC.andThen(dState);
        List<String> result = new ArrayList<>();
        aState.watch(Observers.onEnter(() -> result.add("A")));
        aThenB.watch(Observers.onEnter(() -> result.add("B")));
        aThenBThenC.watch(Observers.onEnter(() -> result.add("C")));
        aThenBThenCThenD.watch(Observers.onEnter(() -> result.add("D")));
        aState.set(Unit.unit());
        bState.set(Unit.unit());
        cState.set(Unit.unit());
        dState.set(Unit.unit());
        aState.reset();
        assertThat(result, contains("A", "B", "C", "D"));
    }
}
