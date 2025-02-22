// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cc/animation/animation_host.h"

#include "base/memory/ptr_util.h"
#include "cc/animation/animation_id_provider.h"
#include "cc/animation/animation_timeline.h"
#include "cc/animation/scroll_timeline.h"
#include "cc/animation/worklet_animation.h"
#include "cc/test/animation_test_common.h"
#include "cc/test/animation_timelines_test_common.h"
#include "cc/test/mock_layer_tree_mutator.h"
#include "cc/trees/scroll_node.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using ::testing::InvokeWithoutArgs;
using ::testing::Mock;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::_;

namespace cc {
namespace {

class AnimationHostTest : public AnimationTimelinesTest {
 public:
  AnimationHostTest() = default;
  ~AnimationHostTest() override = default;

  void AttachWorkletAnimation() {
    client_.RegisterElement(element_id_, ElementListType::ACTIVE);
    client_impl_.RegisterElement(element_id_, ElementListType::PENDING);
    client_impl_.RegisterElement(element_id_, ElementListType::ACTIVE);

    worklet_animation_ = WorkletAnimation::Create(
        worklet_animation_id_, "test_name", nullptr, nullptr);
    int cc_id = worklet_animation_->id();
    worklet_animation_->AttachElement(element_id_);
    host_->AddAnimationTimeline(timeline_);
    timeline_->AttachAnimation(worklet_animation_);

    host_->PushPropertiesTo(host_impl_);
    timeline_impl_ = host_impl_->GetTimelineById(timeline_id_);
    worklet_animation_impl_ =
        ToWorkletAnimation(timeline_impl_->GetAnimationById(cc_id));
  }

  void SetOutputState(base::TimeDelta local_time) {
    worklet_animation_impl_->SetOutputState(
        {worklet_animation_id_, local_time});
  }

  scoped_refptr<WorkletAnimation> worklet_animation_;
  scoped_refptr<WorkletAnimation> worklet_animation_impl_;
  WorkletAnimationId worklet_animation_id_{11, 12};
};

// See Animation tests on layer registration/unregistration in
// animation_unittest.cc.

TEST_F(AnimationHostTest, SyncTimelinesAddRemove) {
  std::unique_ptr<AnimationHost> host(
      AnimationHost::CreateForTesting(ThreadInstance::MAIN));
  std::unique_ptr<AnimationHost> host_impl(
      AnimationHost::CreateForTesting(ThreadInstance::IMPL));

  const int timeline_id = AnimationIdProvider::NextTimelineId();
  scoped_refptr<AnimationTimeline> timeline(
      AnimationTimeline::Create(timeline_id));
  host->AddAnimationTimeline(timeline.get());
  EXPECT_TRUE(timeline->animation_host());

  EXPECT_FALSE(host_impl->GetTimelineById(timeline_id));

  host->PushPropertiesTo(host_impl.get());

  scoped_refptr<AnimationTimeline> timeline_impl =
      host_impl->GetTimelineById(timeline_id);
  EXPECT_TRUE(timeline_impl);
  EXPECT_EQ(timeline_impl->id(), timeline_id);

  host->PushPropertiesTo(host_impl.get());
  EXPECT_EQ(timeline_impl, host_impl->GetTimelineById(timeline_id));

  host->RemoveAnimationTimeline(timeline.get());
  EXPECT_FALSE(timeline->animation_host());

  host->PushPropertiesTo(host_impl.get());
  EXPECT_FALSE(host_impl->GetTimelineById(timeline_id));

  EXPECT_FALSE(timeline_impl->animation_host());
}

TEST_F(AnimationHostTest, ImplOnlyTimeline) {
  std::unique_ptr<AnimationHost> host(
      AnimationHost::CreateForTesting(ThreadInstance::MAIN));
  std::unique_ptr<AnimationHost> host_impl(
      AnimationHost::CreateForTesting(ThreadInstance::IMPL));

  const int timeline_id1 = AnimationIdProvider::NextTimelineId();
  const int timeline_id2 = AnimationIdProvider::NextTimelineId();

  scoped_refptr<AnimationTimeline> timeline(
      AnimationTimeline::Create(timeline_id1));
  scoped_refptr<AnimationTimeline> timeline_impl(
      AnimationTimeline::Create(timeline_id2));
  timeline_impl->set_is_impl_only(true);

  host->AddAnimationTimeline(timeline.get());
  host_impl->AddAnimationTimeline(timeline_impl.get());

  host->PushPropertiesTo(host_impl.get());

  EXPECT_TRUE(host->GetTimelineById(timeline_id1));
  EXPECT_TRUE(host_impl->GetTimelineById(timeline_id2));
}

TEST_F(AnimationHostTest, ImplOnlyScrollAnimationUpdateTargetIfDetached) {
  client_.RegisterElement(element_id_, ElementListType::ACTIVE);
  client_impl_.RegisterElement(element_id_, ElementListType::PENDING);

  gfx::ScrollOffset target_offset(0., 2.);
  gfx::ScrollOffset current_offset(0., 1.);
  host_impl_->ImplOnlyScrollAnimationCreate(element_id_, target_offset,
                                            current_offset, base::TimeDelta(),
                                            base::TimeDelta());

  gfx::Vector2dF scroll_delta(0, 0.5);
  gfx::ScrollOffset max_scroll_offset(0., 3.);

  base::TimeTicks time;

  time += base::TimeDelta::FromSecondsD(0.1);
  EXPECT_TRUE(host_impl_->ImplOnlyScrollAnimationUpdateTarget(
      element_id_, scroll_delta, max_scroll_offset, time, base::TimeDelta()));

  // Detach all animations from layers and timelines.
  host_impl_->ClearMutators();

  time += base::TimeDelta::FromSecondsD(0.1);
  EXPECT_FALSE(host_impl_->ImplOnlyScrollAnimationUpdateTarget(
      element_id_, scroll_delta, max_scroll_offset, time, base::TimeDelta()));
}

// Tests that verify interaction of AnimationHost with LayerTreeMutator.

TEST_F(AnimationHostTest, LayerTreeMutatorUpdateTakesEffectInSameFrame) {
  AttachWorkletAnimation();

  const float start_opacity = .7f;
  const float end_opacity = .3f;
  const double duration = 1.;

  const float expected_opacity =
      start_opacity + (end_opacity - start_opacity) / 2;
  AddOpacityTransitionToAnimation(worklet_animation_.get(), duration,
                                  start_opacity, end_opacity, true);

  base::TimeDelta local_time = base::TimeDelta::FromSecondsD(duration / 2);

  MockLayerTreeMutator* mock_mutator = new NiceMock<MockLayerTreeMutator>();
  host_impl_->SetLayerTreeMutator(
      base::WrapUnique<LayerTreeMutator>(mock_mutator));
  ON_CALL(*mock_mutator, HasAnimators()).WillByDefault(Return(true));
  ON_CALL(*mock_mutator, MutateRef(_))
      .WillByDefault(InvokeWithoutArgs(
          [this, local_time]() { this->SetOutputState(local_time); }));

  // Push the opacity animation to the impl thread.
  host_->PushPropertiesTo(host_impl_);
  host_impl_->ActivateAnimations();

  // Ticking host should cause layer tree mutator to update output state which
  // should take effect in the same animation frame.
  TickAnimationsTransferEvents(base::TimeTicks(), 0u);

  TestLayer* layer =
      client_.FindTestLayer(element_id_, ElementListType::ACTIVE);
  EXPECT_FALSE(layer->is_property_mutated(TargetProperty::OPACITY));
  client_impl_.ExpectOpacityPropertyMutated(
      element_id_, ElementListType::ACTIVE, expected_opacity);
}

TEST_F(AnimationHostTest, LayerTreeMutatorsIsMutatedWithCorrectInputState) {
  AttachWorkletAnimation();

  MockLayerTreeMutator* mock_mutator = new NiceMock<MockLayerTreeMutator>();
  host_impl_->SetLayerTreeMutator(
      base::WrapUnique<LayerTreeMutator>(mock_mutator));
  ON_CALL(*mock_mutator, HasAnimators()).WillByDefault(Return(true));

  const float start_opacity = .7f;
  const float end_opacity = .3f;
  const double duration = 1.;

  AddOpacityTransitionToAnimation(worklet_animation_.get(), duration,
                                  start_opacity, end_opacity, true);

  host_->PushPropertiesTo(host_impl_);
  host_impl_->ActivateAnimations();

  EXPECT_CALL(*mock_mutator, MutateRef(_));

  base::TimeTicks time;
  time += base::TimeDelta::FromSecondsD(0.1);
  TickAnimationsTransferEvents(time, 0u);
}

TEST_F(AnimationHostTest, LayerTreeMutatorsIsMutatedOnlyWhenInputChanges) {
  AttachWorkletAnimation();

  MockLayerTreeMutator* mock_mutator = new NiceMock<MockLayerTreeMutator>();
  host_impl_->SetLayerTreeMutator(
      base::WrapUnique<LayerTreeMutator>(mock_mutator));
  ON_CALL(*mock_mutator, HasAnimators()).WillByDefault(Return(true));

  const float start_opacity = .7f;
  const float end_opacity = .3f;
  const double duration = 1.;

  AddOpacityTransitionToAnimation(worklet_animation_.get(), duration,
                                  start_opacity, end_opacity, true);

  host_->PushPropertiesTo(host_impl_);
  host_impl_->ActivateAnimations();

  EXPECT_CALL(*mock_mutator, MutateRef(_)).Times(1);

  base::TimeTicks time;
  time += base::TimeDelta::FromSecondsD(0.1);
  TickAnimationsTransferEvents(time, 0u);

  // The time has not changed which means worklet animation input is the same.
  // Ticking animations again should not result in mutator being asked to
  // mutate.
  TickAnimationsTransferEvents(time, 0u);
}

class MockAnimation : public Animation {
 public:
  explicit MockAnimation(int id) : Animation(id) {}
  MOCK_METHOD1(Tick, void(base::TimeTicks monotonic_time));

 private:
  ~MockAnimation() {}
};

bool Animation1TimeEquals20(MutatorInputState* input) {
  std::unique_ptr<AnimationWorkletInput> in = input->TakeWorkletState(333);
  return in && in->added_and_updated_animations.size() == 1 &&
         in->added_and_updated_animations[0]
                 .worklet_animation_id.animation_id == 22 &&
         in->added_and_updated_animations[0].current_time == 20;
}

void CreateScrollingNodeForElement(ElementId element_id,
                                   PropertyTrees* property_trees) {
  ScrollNode node;
  node.scrollable = true;
  // Setup scroll dimention to be 100x100.
  node.bounds = gfx::Size(200, 200);
  node.container_bounds = gfx::Size(100, 100);

  int node_id = property_trees->scroll_tree.Insert(node, 0);
  property_trees->element_id_to_scroll_node_index[element_id] = node_id;
}

TEST_F(AnimationHostTest, LayerTreeMutatorUpdateReflectsScrollAnimations) {
  ElementId element_id = element_id_;
  int animation_id1 = 11;
  int animation_id2 = 12;
  WorkletAnimationId worklet_animation_id{333, 22};

  client_.RegisterElement(element_id, ElementListType::ACTIVE);
  client_impl_.RegisterElement(element_id, ElementListType::PENDING);
  client_impl_.RegisterElement(element_id, ElementListType::ACTIVE);
  host_impl_->AddAnimationTimeline(timeline_);

  PropertyTrees property_trees;
  property_trees.is_main_thread = false;
  CreateScrollingNodeForElement(element_id, &property_trees);

  ScrollTree& scroll_tree = property_trees.scroll_tree;
  // Set an initial scroll value.
  scroll_tree.SetScrollOffsetDeltaForTesting(element_id,
                                             gfx::Vector2dF(10, 10));

  scoped_refptr<MockAnimation> mock_scroll_animation(
      new MockAnimation(animation_id1));
  EXPECT_CALL(*mock_scroll_animation, Tick(_))
      .WillOnce(InvokeWithoutArgs([&]() {
        // Scroll to 20% of the max value.
        scroll_tree.SetScrollOffsetDeltaForTesting(element_id,
                                                   gfx::Vector2dF(20, 20));
      }));

  // Ensure scroll animation is ticking.
  timeline_->AttachAnimation(mock_scroll_animation);
  host_impl_->AddToTicking(mock_scroll_animation);

  // Create scroll timeline that links scroll animation and worklet animation
  // together. Use timerange so that we have 1:1 time & scroll mapping.
  auto scroll_timeline = std::make_unique<ScrollTimeline>(
      element_id, ScrollTimeline::Vertical, 100);

  // Create a worklet animation that is bound to the scroll timeline.
  scoped_refptr<WorkletAnimation> worklet_animation(
      new WorkletAnimation(animation_id2, worklet_animation_id, "test_name",
                           std::move(scroll_timeline), nullptr, true));
  worklet_animation->AttachElement(element_id);
  timeline_->AttachAnimation(worklet_animation);

  AddOpacityTransitionToAnimation(worklet_animation.get(), 1, .7f, .3f, true);

  MockLayerTreeMutator* mock_mutator = new NiceMock<MockLayerTreeMutator>();
  host_impl_->SetLayerTreeMutator(
      base::WrapUnique<LayerTreeMutator>(mock_mutator));
  ON_CALL(*mock_mutator, HasAnimators()).WillByDefault(Return(true));
  EXPECT_CALL(*mock_mutator,
              MutateRef(::testing::Truly(Animation1TimeEquals20)))
      .Times(1);

  // Ticking host should cause scroll animation to scroll which should also be
  // reflected in the input of the layer tree mutator in the same animation
  // frame.
  host_impl_->TickAnimations(base::TimeTicks(), scroll_tree, false);
}

}  // namespace
}  // namespace cc
