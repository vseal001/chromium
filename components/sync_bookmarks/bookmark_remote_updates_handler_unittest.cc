// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/sync_bookmarks/bookmark_remote_updates_handler.h"

#include <memory>
#include <string>
#include <utility>

#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "components/bookmarks/browser/bookmark_model.h"
#include "components/bookmarks/test/test_bookmark_client.h"
#include "components/sync/base/hash_util.h"
#include "components/sync/base/model_type.h"
#include "components/sync/base/unique_position.h"
#include "components/sync/protocol/unique_position.pb.h"
#include "components/sync_bookmarks/bookmark_model_merger.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using base::ASCIIToUTF16;
using testing::Eq;

namespace sync_bookmarks {

namespace {

// The parent tag for children of the root entity. Entities with this parent are
// referred to as top level enities.
const char kRootParentTag[] = "0";
const char kBookmarkBarId[] = "bookmark_bar_id";
const char kBookmarkBarTag[] = "bookmark_bar";
const char kBookmarksRootId[] = "32904_google_chrome_bookmarks";

syncer::UpdateResponseData CreateUpdateResponseData(
    const std::string& server_id,
    const std::string& parent_id,
    bool is_deletion,
    const syncer::UniquePosition& unique_position) {
  syncer::EntityData data;
  data.id = server_id;
  data.parent_id = parent_id;
  data.unique_position = unique_position.ToProto();

  // EntityData would be considered a deletion if its specifics hasn't been set.
  if (!is_deletion) {
    sync_pb::BookmarkSpecifics* bookmark_specifics =
        data.specifics.mutable_bookmark();
    // Use the server id as the title for simplicity.
    bookmark_specifics->set_title(server_id);
  }
  data.is_folder = true;
  syncer::UpdateResponseData response_data;
  response_data.entity = data.PassToPtr();
  // Similar to what's done in the loopback_server.
  response_data.response_version = 0;
  return response_data;
}

// Overload that assign a random position. Should only be used when position
// doesn't matter.
syncer::UpdateResponseData CreateUpdateResponseData(
    const std::string& server_id,
    const std::string& parent_id,
    bool is_deletion) {
  return CreateUpdateResponseData(server_id, parent_id, is_deletion,
                                  syncer::UniquePosition::InitialPosition(
                                      syncer::UniquePosition::RandomSuffix()));
}

syncer::UpdateResponseData CreateBookmarkRootUpdateData() {
  syncer::EntityData data;
  data.id = syncer::ModelTypeToRootTag(syncer::BOOKMARKS);
  data.parent_id = kRootParentTag;
  data.server_defined_unique_tag =
      syncer::ModelTypeToRootTag(syncer::BOOKMARKS);

  data.specifics.mutable_bookmark();

  syncer::UpdateResponseData response_data;
  response_data.entity = data.PassToPtr();
  // Similar to what's done in the loopback_server.
  response_data.response_version = 0;
  return response_data;
}

syncer::UpdateResponseData CreateBookmarkBarNodeUpdateData() {
  syncer::EntityData data;
  data.id = kBookmarkBarId;
  data.parent_id = kBookmarksRootId;
  data.server_defined_unique_tag = kBookmarkBarTag;

  data.specifics.mutable_bookmark();

  syncer::UpdateResponseData response_data;
  response_data.entity = data.PassToPtr();
  // Similar to what's done in the loopback_server.
  response_data.response_version = 0;
  return response_data;
}

std::unique_ptr<sync_pb::EntityMetadata> CreateEntityMetadata(
    const std::string& server_id,
    const syncer::UniquePosition& unique_position) {
  auto metadata = std::make_unique<sync_pb::EntityMetadata>();
  metadata->set_server_id(server_id);
  *metadata->mutable_unique_position() = unique_position.ToProto();
  metadata->set_is_deleted(false);
  return metadata;
}

std::unique_ptr<sync_pb::EntityMetadata> CreateEntityMetadata(
    const std::string& server_id) {
  return CreateEntityMetadata(server_id,
                              syncer::UniquePosition::InitialPosition(
                                  syncer::UniquePosition::RandomSuffix()));
}

TEST(BookmarkRemoteUpdatesHandlerReorderUpdatesTest, ShouldIgnoreRootNodes) {
  syncer::UpdateResponseDataList updates;
  updates.push_back(CreateBookmarkRootUpdateData());
  std::vector<const syncer::UpdateResponseData*> ordered_updates =
      BookmarkRemoteUpdatesHandler::ReorderUpdatesForTest(&updates);
  // Root node update should be filtered out.
  EXPECT_THAT(ordered_updates.size(), Eq(0U));
}

TEST(BookmarkRemoteUpdatesHandlerReorderUpdatesTest,
     ShouldReorderParentsUpdateBeforeChildrenAndBothBeforeDeletions) {
  // Prepare creation updates to build this structure:
  // bookmark_bar
  //  |- node0
  //    |- node1
  //      |- node2
  // and another sub hierarchy under node3 that won't receive any update.
  // node4
  //  |- node5
  // and a deletion for node6 under node3.

  // Constuct the updates list to have deletion first, and then all creations in
  // reverse shuffled order (from child to parent).

  std::vector<std::string> ids;
  for (int i = 0; i < 7; i++) {
    ids.push_back("node" + base::NumberToString(i));
  }
  // Construct updates list
  syncer::UpdateResponseDataList updates;
  updates.push_back(CreateUpdateResponseData(/*server_id=*/ids[6],
                                             /*parent_id=*/ids[3],
                                             /*is_deletion=*/true));
  updates.push_back(CreateUpdateResponseData(/*server_id=*/ids[5],
                                             /*parent_id=*/ids[4],
                                             /*is_deletion=*/false));
  updates.push_back(CreateUpdateResponseData(/*server_id=*/ids[2],
                                             /*parent_id=*/ids[1],
                                             /*is_deletion=*/false));
  updates.push_back(CreateUpdateResponseData(/*server_id=*/ids[1],
                                             /*parent_id=*/ids[0],
                                             /*is_deletion=*/false));
  updates.push_back(CreateUpdateResponseData(/*server_id=*/ids[4],
                                             /*parent_id=*/ids[3],
                                             /*is_deletion=*/false));
  updates.push_back(CreateUpdateResponseData(/*server_id=*/ids[0],
                                             /*parent_id=*/kBookmarksRootId,
                                             /*is_deletion=*/false));

  std::vector<const syncer::UpdateResponseData*> ordered_updates =
      BookmarkRemoteUpdatesHandler::ReorderUpdatesForTest(&updates);

  // No update should be dropped.
  ASSERT_THAT(ordered_updates.size(), Eq(6U));

  // Updates should be ordered such that parent node update comes first, and
  // deletions come last.
  // node0 --> node1 --> node2 --> node4 --> node5 --> node6.
  // This is test is over verifying since the order requirements are
  // within subtrees only. (e.g it doesn't matter whether node1 comes before or
  // after node4). However, it's implemented this way for simplicity.
  EXPECT_THAT(ordered_updates[0]->entity.value().id, Eq(ids[0]));
  EXPECT_THAT(ordered_updates[1]->entity.value().id, Eq(ids[1]));
  EXPECT_THAT(ordered_updates[2]->entity.value().id, Eq(ids[2]));
  EXPECT_THAT(ordered_updates[3]->entity.value().id, Eq(ids[4]));
  EXPECT_THAT(ordered_updates[4]->entity.value().id, Eq(ids[5]));
  EXPECT_THAT(ordered_updates[5]->entity.value().id, Eq(ids[6]));
}

TEST(BookmarkRemoteUpdatesHandlerReorderUpdatesTest,
     ShouldProcessRandomlyOrderedCreations) {
  // Prepare creation updates to construct this structure:
  // bookmark_bar
  //  |- node0
  //    |- node1
  //      |- node2

  std::unique_ptr<bookmarks::BookmarkModel> bookmark_model =
      bookmarks::TestBookmarkClient::CreateModel();
  SyncedBookmarkTracker tracker(std::vector<NodeMetadataPair>(),
                                std::make_unique<sync_pb::ModelTypeState>());
  const syncer::UpdateResponseDataList bookmark_bar_updates = {
      CreateBookmarkBarNodeUpdateData()};
  // TODO(crbug.com/516866): Create a test fixture that would encapsulate
  // the merge functionality for all relevant tests.
  BookmarkModelMerger(&bookmark_bar_updates, bookmark_model.get(), &tracker)
      .Merge();

  const std::string kId0 = "id0";
  const std::string kId1 = "id1";
  const std::string kId2 = "id2";

  // Constuct the updates list to have creations randomly ordered.
  syncer::UpdateResponseDataList updates;
  updates.push_back(CreateUpdateResponseData(/*server_id=*/kId2,
                                             /*parent_id=*/kId1,
                                             /*is_deletion=*/false));
  updates.push_back(CreateUpdateResponseData(/*server_id=*/kId0,
                                             /*parent_id=*/kBookmarkBarId,
                                             /*is_deletion=*/false));
  updates.push_back(CreateUpdateResponseData(/*server_id=*/kId1,
                                             /*parent_id=*/kId0,
                                             /*is_deletion=*/false));

  BookmarkRemoteUpdatesHandler updates_handler(bookmark_model.get(), &tracker);
  updates_handler.Process(updates);

  // All nodes should be tracked including the bookmark_bar.
  EXPECT_THAT(tracker.TrackedEntitiesCountForTest(), Eq(4U));

  // All nodes should have been added to the model.
  const bookmarks::BookmarkNode* bookmark_bar_node =
      bookmark_model->bookmark_bar_node();
  ASSERT_THAT(bookmark_bar_node->child_count(), Eq(1));
  EXPECT_THAT(bookmark_bar_node->GetChild(0)->GetTitle(),
              Eq(ASCIIToUTF16(kId0)));
  ASSERT_THAT(bookmark_bar_node->GetChild(0)->child_count(), Eq(1));
  EXPECT_THAT(bookmark_bar_node->GetChild(0)->GetChild(0)->GetTitle(),
              Eq(ASCIIToUTF16(kId1)));
  ASSERT_THAT(bookmark_bar_node->GetChild(0)->GetChild(0)->child_count(),
              Eq(1));
  EXPECT_THAT(
      bookmark_bar_node->GetChild(0)->GetChild(0)->GetChild(0)->GetTitle(),
      Eq(ASCIIToUTF16(kId2)));
  EXPECT_THAT(
      bookmark_bar_node->GetChild(0)->GetChild(0)->GetChild(0)->child_count(),
      Eq(0));
}

TEST(BookmarkRemoteUpdatesHandlerReorderUpdatesTest,
     ShouldProcessRandomlyOrderedDeletions) {
  // Prepare deletion updates for this structure:
  // bookmark_bar
  //  |- node0
  //    |- node1
  //      |- node2

  std::unique_ptr<bookmarks::BookmarkModel> bookmark_model =
      bookmarks::TestBookmarkClient::CreateModel();

  const bookmarks::BookmarkNode* bookmark_bar_node =
      bookmark_model->bookmark_bar_node();
  const bookmarks::BookmarkNode* node0 = bookmark_model->AddFolder(
      /*parent=*/bookmark_bar_node, /*index=*/0, base::UTF8ToUTF16("node0"));
  const bookmarks::BookmarkNode* node1 = bookmark_model->AddFolder(
      /*parent=*/node0, /*index=*/0, base::UTF8ToUTF16("node1"));
  const bookmarks::BookmarkNode* node2 = bookmark_model->AddFolder(
      /*parent=*/node1, /*index=*/0, base::UTF8ToUTF16("node2"));

  const std::string kId0 = "id0";
  const std::string kId1 = "id1";
  const std::string kId2 = "id2";

  std::vector<NodeMetadataPair> node_metadata_pairs;
  node_metadata_pairs.emplace_back(node0,
                                   CreateEntityMetadata(/*server_id=*/kId0));
  node_metadata_pairs.emplace_back(node1,
                                   CreateEntityMetadata(/*server_id=*/kId1));
  node_metadata_pairs.emplace_back(node2,
                                   CreateEntityMetadata(/*server_id=*/kId2));

  SyncedBookmarkTracker tracker(std::move(node_metadata_pairs),
                                std::make_unique<sync_pb::ModelTypeState>());

  // Constuct the updates list to have random deletions order.
  syncer::UpdateResponseDataList updates;
  updates.push_back(CreateUpdateResponseData(/*server_id=*/kId1,
                                             /*parent_id=*/kId0,
                                             /*is_deletion=*/true));
  updates.push_back(CreateUpdateResponseData(/*server_id=*/kId0,
                                             /*parent_id=*/kBookmarksRootId,
                                             /*is_deletion=*/true));
  updates.push_back(CreateUpdateResponseData(/*server_id=*/kId2,
                                             /*parent_id=*/kId1,
                                             /*is_deletion=*/true));

  BookmarkRemoteUpdatesHandler updates_handler(bookmark_model.get(), &tracker);
  updates_handler.Process(updates);

  // |tracker| should be empty now.
  EXPECT_THAT(tracker.TrackedEntitiesCountForTest(), Eq(0U));
}

TEST(BookmarkRemoteUpdatesHandlerReorderUpdatesTest,
     ShouldPositionRemoteCreations) {
  // Prepare creation updates to construct this structure:
  // bookmark_bar
  //  |- node0
  //  |- node1
  //  |- node2

  std::unique_ptr<bookmarks::BookmarkModel> bookmark_model =
      bookmarks::TestBookmarkClient::CreateModel();
  SyncedBookmarkTracker tracker(std::vector<NodeMetadataPair>(),
                                std::make_unique<sync_pb::ModelTypeState>());
  const syncer::UpdateResponseDataList bookmark_bar_updates = {
      CreateBookmarkBarNodeUpdateData()};
  BookmarkModelMerger(&bookmark_bar_updates, bookmark_model.get(), &tracker)
      .Merge();

  const std::string kId0 = "id0";
  const std::string kId1 = "id1";
  const std::string kId2 = "id2";

  syncer::UniquePosition pos0 = syncer::UniquePosition::InitialPosition(
      syncer::UniquePosition::RandomSuffix());
  syncer::UniquePosition pos1 = syncer::UniquePosition::After(
      pos0, syncer::UniquePosition::RandomSuffix());
  syncer::UniquePosition pos2 = syncer::UniquePosition::After(
      pos1, syncer::UniquePosition::RandomSuffix());

  // Constuct the updates list to have creations randomly ordered.
  syncer::UpdateResponseDataList updates;
  updates.push_back(CreateBookmarkBarNodeUpdateData());
  updates.push_back(CreateUpdateResponseData(
      /*server_id=*/kId2, /*parent_id=*/kBookmarkBarId,
      /*is_deletion=*/false, /*unique_position=*/pos2));
  updates.push_back(CreateUpdateResponseData(/*server_id=*/kId0,
                                             /*parent_id=*/kBookmarkBarId,
                                             /*is_deletion=*/false,
                                             /*unique_position=*/pos0));
  updates.push_back(CreateUpdateResponseData(
      /*server_id=*/kId1, /*parent_id=*/kBookmarkBarId,
      /*is_deletion=*/false, /*unique_position=*/pos1));

  BookmarkRemoteUpdatesHandler updates_handler(bookmark_model.get(), &tracker);
  updates_handler.Process(updates);

  // All nodes should have been added to the model in the correct order.
  const bookmarks::BookmarkNode* bookmark_bar_node =
      bookmark_model->bookmark_bar_node();
  ASSERT_THAT(bookmark_bar_node->child_count(), Eq(3));
  EXPECT_THAT(bookmark_bar_node->GetChild(0)->GetTitle(),
              Eq(ASCIIToUTF16(kId0)));
  EXPECT_THAT(bookmark_bar_node->GetChild(1)->GetTitle(),
              Eq(ASCIIToUTF16(kId1)));
  EXPECT_THAT(bookmark_bar_node->GetChild(2)->GetTitle(),
              Eq(ASCIIToUTF16(kId2)));
}

TEST(BookmarkRemoteUpdatesHandlerReorderUpdatesTest,
     ShouldPositionRemoteMovesToTheLeft) {
  // Start with structure:
  // bookmark_bar
  //  |- node0
  //  |- node1
  //  |- node2
  //  |- node3
  //  |- node4

  std::unique_ptr<bookmarks::BookmarkModel> bookmark_model =
      bookmarks::TestBookmarkClient::CreateModel();
  const bookmarks::BookmarkNode* bookmark_bar_node =
      bookmark_model->bookmark_bar_node();

  std::vector<std::string> ids;
  std::vector<const bookmarks::BookmarkNode*> nodes;
  std::vector<syncer::UniquePosition> positions;
  std::vector<NodeMetadataPair> node_metadata_pairs;
  // Add the bookmark bar entry.
  node_metadata_pairs.emplace_back(bookmark_bar_node,
                                   CreateEntityMetadata(kBookmarkBarId));
  syncer::UniquePosition position = syncer::UniquePosition::InitialPosition(
      syncer::UniquePosition::RandomSuffix());
  for (int i = 0; i < 5; i++) {
    ids.push_back("node" + base::NumberToString(i));
    // Use ids as node titles for simplcity and to match CreateEntityMetadata()
    // implementation.
    nodes.push_back(bookmark_model->AddFolder(
        /*parent=*/bookmark_bar_node, /*index=*/i, base::UTF8ToUTF16(ids[i])));
    position = syncer::UniquePosition::After(
        position, syncer::UniquePosition::RandomSuffix());
    positions.push_back(position);
    node_metadata_pairs.emplace_back(
        nodes[i], CreateEntityMetadata(ids[i], positions[i]));
  }

  SyncedBookmarkTracker tracker(std::move(node_metadata_pairs),
                                std::make_unique<sync_pb::ModelTypeState>());
  // Change it to this structure by moving node3 after node1.
  // bookmark_bar
  //  |- node0
  //  |- node1
  //  |- node3
  //  |- node2
  //  |- node4

  syncer::UpdateResponseDataList updates;
  updates.push_back(CreateUpdateResponseData(
      /*server_id=*/ids[3],
      /*parent_id=*/kBookmarkBarId,
      /*is_deletion=*/false,
      /*unique_position=*/
      syncer::UniquePosition::Between(positions[1], positions[2],
                                      syncer::UniquePosition::RandomSuffix())));

  BookmarkRemoteUpdatesHandler updates_handler(bookmark_model.get(), &tracker);
  updates_handler.Process(updates);

  // Model should have been updated.
  ASSERT_THAT(bookmark_bar_node->child_count(), Eq(5));
  EXPECT_THAT(bookmark_bar_node->GetChild(2)->GetTitle(),
              Eq(ASCIIToUTF16(ids[3])));
}

TEST(BookmarkRemoteUpdatesHandlerReorderUpdatesTest,
     ShouldPositionRemoteMovesToTheRight) {
  // Start with structure:
  // bookmark_bar
  //  |- node0
  //  |- node1
  //  |- node2
  //  |- node3
  //  |- node4

  std::unique_ptr<bookmarks::BookmarkModel> bookmark_model =
      bookmarks::TestBookmarkClient::CreateModel();
  const bookmarks::BookmarkNode* bookmark_bar_node =
      bookmark_model->bookmark_bar_node();

  std::vector<std::string> ids;
  std::vector<const bookmarks::BookmarkNode*> nodes;
  std::vector<syncer::UniquePosition> positions;
  std::vector<NodeMetadataPair> node_metadata_pairs;
  // Add the bookmark bar entry.
  node_metadata_pairs.emplace_back(bookmark_bar_node,
                                   CreateEntityMetadata(kBookmarkBarId));
  syncer::UniquePosition position = syncer::UniquePosition::InitialPosition(
      syncer::UniquePosition::RandomSuffix());
  for (int i = 0; i < 5; i++) {
    ids.push_back("node" + base::NumberToString(i));
    // Use ids as node titles for simplcity and to match CreateEntityMetadata()
    // implementation.
    nodes.push_back(bookmark_model->AddFolder(
        /*parent=*/bookmark_bar_node, /*index=*/i, base::UTF8ToUTF16(ids[i])));
    position = syncer::UniquePosition::After(
        position, syncer::UniquePosition::RandomSuffix());
    positions.push_back(position);
    node_metadata_pairs.emplace_back(
        nodes[i], CreateEntityMetadata(ids[i], positions[i]));
  }

  SyncedBookmarkTracker tracker(std::move(node_metadata_pairs),
                                std::make_unique<sync_pb::ModelTypeState>());

  // Change it to this structure by moving node1 after node3.
  // bookmark_bar
  //  |- node0
  //  |- node2
  //  |- node3
  //  |- node1
  //  |- node4

  syncer::UpdateResponseDataList updates;
  updates.push_back(CreateUpdateResponseData(
      /*server_id=*/ids[1],
      /*parent_id=*/kBookmarkBarId,
      /*is_deletion=*/false,
      /*unique_position=*/
      syncer::UniquePosition::Between(positions[3], positions[4],
                                      syncer::UniquePosition::RandomSuffix())));

  BookmarkRemoteUpdatesHandler updates_handler(bookmark_model.get(), &tracker);
  updates_handler.Process(updates);

  // Model should have been updated.
  ASSERT_THAT(bookmark_bar_node->child_count(), Eq(5));
  EXPECT_THAT(bookmark_bar_node->GetChild(3)->GetTitle(),
              Eq(ASCIIToUTF16(ids[1])));
}

TEST(BookmarkRemoteUpdatesHandlerReorderUpdatesTest,
     ShouldPositionRemoteReparenting) {
  // Start with structure:
  // bookmark_bar
  //  |- node0
  //  |- node1
  //  |- node2
  //  |- node3
  //  |- node4

  std::unique_ptr<bookmarks::BookmarkModel> bookmark_model =
      bookmarks::TestBookmarkClient::CreateModel();
  const bookmarks::BookmarkNode* bookmark_bar_node =
      bookmark_model->bookmark_bar_node();

  std::vector<std::string> ids;
  std::vector<const bookmarks::BookmarkNode*> nodes;
  std::vector<syncer::UniquePosition> positions;
  std::vector<NodeMetadataPair> node_metadata_pairs;
  // Add the bookmark bar entry.
  node_metadata_pairs.emplace_back(bookmark_bar_node,
                                   CreateEntityMetadata(kBookmarkBarId));
  syncer::UniquePosition position = syncer::UniquePosition::InitialPosition(
      syncer::UniquePosition::RandomSuffix());
  for (int i = 0; i < 5; i++) {
    ids.push_back("node" + base::NumberToString(i));
    // Use ids as node titles for simplcity and to match CreateEntityMetadata()
    // implementation.
    nodes.push_back(bookmark_model->AddFolder(
        /*parent=*/bookmark_bar_node, /*index=*/i, base::UTF8ToUTF16(ids[i])));
    position = syncer::UniquePosition::After(
        position, syncer::UniquePosition::RandomSuffix());
    positions.push_back(position);
    node_metadata_pairs.emplace_back(
        nodes[i], CreateEntityMetadata(ids[i], positions[i]));
  }

  SyncedBookmarkTracker tracker(std::move(node_metadata_pairs),
                                std::make_unique<sync_pb::ModelTypeState>());

  // Change it to this structure by moving node4 under node1.
  // bookmark_bar
  //  |- node0
  //  |- node1
  //    |- node4
  //  |- node2
  //  |- node3

  syncer::UpdateResponseDataList updates;
  updates.push_back(CreateUpdateResponseData(
      /*server_id=*/ids[4],
      /*parent_id=*/ids[1],
      /*is_deletion=*/false,
      /*unique_position=*/
      syncer::UniquePosition::InitialPosition(
          syncer::UniquePosition::RandomSuffix())));

  BookmarkRemoteUpdatesHandler updates_handler(bookmark_model.get(), &tracker);
  updates_handler.Process(updates);

  // Model should have been updated.
  ASSERT_THAT(bookmark_bar_node->child_count(), Eq(4));
  ASSERT_THAT(bookmark_bar_node->GetChild(1)->child_count(), Eq(1));
  EXPECT_THAT(bookmark_bar_node->GetChild(1)->GetChild(0)->GetTitle(),
              Eq(ASCIIToUTF16(ids[4])));
}

}  // namespace

}  // namespace sync_bookmarks
