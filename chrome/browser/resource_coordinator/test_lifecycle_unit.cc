// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/resource_coordinator/test_lifecycle_unit.h"

#include "chrome/browser/resource_coordinator/lifecycle_unit_source.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace resource_coordinator {

TestLifecycleUnit::TestLifecycleUnit(base::TimeTicks last_focused_time,
                                     base::ProcessHandle process_handle,
                                     bool can_discard)
    : LifecycleUnitBase(nullptr, content::Visibility::VISIBLE, nullptr),
      last_focused_time_(last_focused_time),
      process_handle_(process_handle),
      can_discard_(can_discard) {}

TestLifecycleUnit::TestLifecycleUnit(content::Visibility visibility,
                                     UsageClock* usage_clock)
    : LifecycleUnitBase(nullptr, visibility, usage_clock) {}

TestLifecycleUnit::TestLifecycleUnit(LifecycleUnitSourceBase* source)
    : LifecycleUnitBase(source, content::Visibility::VISIBLE, nullptr) {}

TestLifecycleUnit::~TestLifecycleUnit() {
  OnLifecycleUnitDestroyed();
}

TabLifecycleUnitExternal* TestLifecycleUnit::AsTabLifecycleUnitExternal() {
  return nullptr;
}

base::string16 TestLifecycleUnit::GetTitle() const {
  return base::string16();
}

base::TimeTicks TestLifecycleUnit::GetLastFocusedTime() const {
  return last_focused_time_;
}

base::ProcessHandle TestLifecycleUnit::GetProcessHandle() const {
  return process_handle_;
}

LifecycleUnit::SortKey TestLifecycleUnit::GetSortKey() const {
  return SortKey(last_focused_time_);
}

content::Visibility TestLifecycleUnit::GetVisibility() const {
  return content::Visibility::VISIBLE;
}

::mojom::LifecycleUnitLoadingState TestLifecycleUnit::GetLoadingState() const {
  return ::mojom::LifecycleUnitLoadingState::LOADED;
}

bool TestLifecycleUnit::Load() {
  return false;
}

int TestLifecycleUnit::GetEstimatedMemoryFreedOnDiscardKB() const {
  return 0;
}

bool TestLifecycleUnit::CanPurge() const {
  return false;
}

bool TestLifecycleUnit::CanFreeze(DecisionDetails* decision_details) const {
  return false;
}

bool TestLifecycleUnit::CanDiscard(DiscardReason reason,
                                   DecisionDetails* decision_details) const {
  return can_discard_;
}

bool TestLifecycleUnit::Freeze() {
  return false;
}

bool TestLifecycleUnit::Unfreeze() {
  return false;
}

bool TestLifecycleUnit::Discard(DiscardReason discard_reason) {
  return false;
}

void ExpectCanDiscardTrue(const LifecycleUnit* lifecycle_unit,
                          DiscardReason discard_reason) {
  DecisionDetails decision_details;
  EXPECT_TRUE(lifecycle_unit->CanDiscard(discard_reason, &decision_details));
  EXPECT_TRUE(decision_details.IsPositive());
  EXPECT_EQ(1u, decision_details.reasons().size());
  EXPECT_EQ(DecisionSuccessReason::HEURISTIC_OBSERVED_TO_BE_SAFE,
            decision_details.SuccessReason());
}

void ExpectCanDiscardTrueAllReasons(const LifecycleUnit* lifecycle_unit) {
  ExpectCanDiscardTrue(lifecycle_unit, DiscardReason::kExternal);
  ExpectCanDiscardTrue(lifecycle_unit, DiscardReason::kProactive);
  ExpectCanDiscardTrue(lifecycle_unit, DiscardReason::kUrgent);
}

void ExpectCanDiscardFalse(const LifecycleUnit* lifecycle_unit,
                           DecisionFailureReason failure_reason,
                           DiscardReason discard_reason) {
  DecisionDetails decision_details;
  EXPECT_FALSE(lifecycle_unit->CanDiscard(discard_reason, &decision_details));
  EXPECT_FALSE(decision_details.IsPositive());
  EXPECT_EQ(1u, decision_details.reasons().size());
  EXPECT_EQ(failure_reason, decision_details.FailureReason());
}

void ExpectCanDiscardFalseAllReasons(const LifecycleUnit* lifecycle_unit,
                                     DecisionFailureReason failure_reason) {
  ExpectCanDiscardFalse(lifecycle_unit, failure_reason,
                        DiscardReason::kExternal);
  ExpectCanDiscardFalse(lifecycle_unit, failure_reason,
                        DiscardReason::kProactive);
  ExpectCanDiscardFalse(lifecycle_unit, failure_reason, DiscardReason::kUrgent);
}

void ExpectCanDiscardFalseTrivial(const LifecycleUnit* lifecycle_unit,
                                  DiscardReason discard_reason) {
  DecisionDetails decision_details;
  EXPECT_FALSE(lifecycle_unit->CanDiscard(discard_reason, &decision_details));
  EXPECT_FALSE(decision_details.IsPositive());
  // |reasons()| will either contain the status for the 4 local site features
  // heuristics or be empty if the database doesn't track this lifecycle unit.
  EXPECT_TRUE(decision_details.reasons().empty() ||
              (decision_details.reasons().size() == 4));
}

void ExpectCanDiscardFalseTrivialAllReasons(
    const LifecycleUnit* lifecycle_unit) {
  ExpectCanDiscardFalseTrivial(lifecycle_unit, DiscardReason::kExternal);
  ExpectCanDiscardFalseTrivial(lifecycle_unit, DiscardReason::kProactive);
  ExpectCanDiscardFalseTrivial(lifecycle_unit, DiscardReason::kUrgent);
}

}  // namespace resource_coordinator
