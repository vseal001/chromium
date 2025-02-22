// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/dom_distiller/core/task_tracker.h"

#include <stddef.h>
#include <utility>

#include "base/auto_reset.h"
#include "base/location.h"
#include "base/single_thread_task_runner.h"
#include "base/threading/thread_task_runner_handle.h"
#include "components/dom_distiller/core/distilled_content_store.h"
#include "components/dom_distiller/core/proto/distilled_article.pb.h"
#include "components/dom_distiller/core/proto/distilled_page.pb.h"

namespace dom_distiller {

ViewerHandle::ViewerHandle(CancelCallback callback)
    : cancel_callback_(callback) {}

ViewerHandle::~ViewerHandle() {
  if (!cancel_callback_.is_null()) {
    cancel_callback_.Run();
  }
}

TaskTracker::TaskTracker(const ArticleEntry& entry,
                         CancelCallback callback,
                         DistilledContentStore* content_store)
    : cancel_callback_(callback),
      content_store_(content_store),
      blob_fetcher_running_(false),
      entry_(entry),
      distilled_article_(),
      content_ready_(false),
      destruction_allowed_(true),
      weak_ptr_factory_(this) {}

TaskTracker::~TaskTracker() {
  DCHECK(destruction_allowed_);
  DCHECK(viewers_.empty());
}

void TaskTracker::StartDistiller(
    DistillerFactory* factory,
    std::unique_ptr<DistillerPage> distiller_page) {
  if (distiller_) {
    return;
  }
  if (entry_.pages_size() == 0) {
    return;
  }
  GURL url(entry_.pages(0).url());
  DCHECK(url.is_valid());

  distiller_ = factory->CreateDistillerForUrl(url);
  distiller_->DistillPage(url, std::move(distiller_page),
                          base::Bind(&TaskTracker::OnDistillerFinished,
                                     weak_ptr_factory_.GetWeakPtr()),
                          base::Bind(&TaskTracker::OnArticleDistillationUpdated,
                                     weak_ptr_factory_.GetWeakPtr()));
}

void TaskTracker::StartBlobFetcher() {
  if (content_store_) {
    blob_fetcher_running_ = true;
    content_store_->LoadContent(entry_,
                                base::Bind(&TaskTracker::OnBlobFetched,
                                           weak_ptr_factory_.GetWeakPtr()));
  }
}

void TaskTracker::AddSaveCallback(const SaveCallback& callback) {
  DCHECK(!callback.is_null());
  save_callbacks_.push_back(callback);
  if (content_ready_) {
    // Distillation for this task has already completed, and so it can be
    // immediately saved.
    ScheduleSaveCallbacks(true);
  }
}

std::unique_ptr<ViewerHandle> TaskTracker::AddViewer(
    ViewRequestDelegate* delegate) {
  viewers_.push_back(delegate);
  if (content_ready_) {
    // Distillation for this task has already completed, and so the delegate can
    // be immediately told of the result.
    base::ThreadTaskRunnerHandle::Get()->PostTask(
        FROM_HERE, base::BindOnce(&TaskTracker::NotifyViewer,
                                  weak_ptr_factory_.GetWeakPtr(), delegate));
  }
  return std::unique_ptr<ViewerHandle>(new ViewerHandle(base::Bind(
      &TaskTracker::RemoveViewer, weak_ptr_factory_.GetWeakPtr(), delegate)));
}

const std::string& TaskTracker::GetEntryId() const { return entry_.entry_id(); }

bool TaskTracker::HasEntryId(const std::string& entry_id) const {
  return entry_.entry_id() == entry_id;
}

bool TaskTracker::HasUrl(const GURL& url) const {
  for (int i = 0; i < entry_.pages_size(); ++i) {
    if (entry_.pages(i).url() == url.spec()) {
      return true;
    }
  }
  return false;
}

void TaskTracker::RemoveViewer(ViewRequestDelegate* delegate) {
  viewers_.erase(std::remove(viewers_.begin(), viewers_.end(), delegate));
  if (viewers_.empty()) {
    MaybeCancel();
  }
}

void TaskTracker::MaybeCancel() {
  if (!save_callbacks_.empty() || !viewers_.empty()) {
    // There's still work to be done.
    return;
  }

  CancelPendingSources();

  base::AutoReset<bool> dont_delete_this_in_callback(&destruction_allowed_,
                                                     false);
  cancel_callback_.Run(this);
}

void TaskTracker::CancelSaveCallbacks() { ScheduleSaveCallbacks(false); }

void TaskTracker::ScheduleSaveCallbacks(bool distillation_succeeded) {
  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE,
      base::BindOnce(&TaskTracker::DoSaveCallbacks,
                     weak_ptr_factory_.GetWeakPtr(), distillation_succeeded));
}

void TaskTracker::OnDistillerFinished(
    std::unique_ptr<DistilledArticleProto> distilled_article) {
  if (content_ready_) {
    return;
  }

  DistilledArticleReady(std::move(distilled_article));
  if (content_ready_) {
    AddDistilledContentToStore(*distilled_article_);
  }

  // 'distiller_ != null' is used as a signal that distillation is in progress,
  // so it needs to be released so that we know distillation is done.
  base::ThreadTaskRunnerHandle::Get()->DeleteSoon(FROM_HERE,
                                                  distiller_.release());

  ContentSourceFinished();
}

void TaskTracker::CancelPendingSources() {
  if (distiller_) {
    base::ThreadTaskRunnerHandle::Get()->DeleteSoon(FROM_HERE,
                                                    distiller_.release());
  }
}

void TaskTracker::OnBlobFetched(
    bool success,
    std::unique_ptr<DistilledArticleProto> distilled_article) {
  blob_fetcher_running_ = false;

  if (content_ready_) {
    return;
  }

  DistilledArticleReady(std::move(distilled_article));

  ContentSourceFinished();
}

bool TaskTracker::IsAnySourceRunning() const {
  return distiller_ || blob_fetcher_running_;
}

void TaskTracker::ContentSourceFinished() {
  if (content_ready_) {
    CancelPendingSources();
  } else if (!IsAnySourceRunning()) {
    distilled_article_.reset(new DistilledArticleProto());
    NotifyViewersAndCallbacks();
  }
}

void TaskTracker::DistilledArticleReady(
    std::unique_ptr<DistilledArticleProto> distilled_article) {
  DCHECK(!content_ready_);

  if (distilled_article->pages_size() == 0) {
    return;
  }

  content_ready_ = true;

  distilled_article_ = std::move(distilled_article);
  entry_.set_title(distilled_article_->title());
  entry_.clear_pages();
  for (int i = 0; i < distilled_article_->pages_size(); ++i) {
    sync_pb::ArticlePage* page = entry_.add_pages();
    page->set_url(distilled_article_->pages(i).url());
  }

  NotifyViewersAndCallbacks();
}

void TaskTracker::NotifyViewersAndCallbacks() {
  for (size_t i = 0; i < viewers_.size(); ++i) {
    NotifyViewer(viewers_[i]);
  }

  // Already inside a callback run SaveCallbacks directly.
  DoSaveCallbacks(content_ready_);
}

void TaskTracker::NotifyViewer(ViewRequestDelegate* delegate) {
  delegate->OnArticleReady(distilled_article_.get());
}

void TaskTracker::DoSaveCallbacks(bool success) {
  if (!save_callbacks_.empty()) {
    for (size_t i = 0; i < save_callbacks_.size(); ++i) {
      DCHECK(!save_callbacks_[i].is_null());
      save_callbacks_[i].Run(
          entry_, distilled_article_.get(), success);
    }

    save_callbacks_.clear();
    MaybeCancel();
  }
}

void TaskTracker::OnArticleDistillationUpdated(
    const ArticleDistillationUpdate& article_update) {
  for (size_t i = 0; i < viewers_.size(); ++i) {
    viewers_[i]->OnArticleUpdated(article_update);
  }
}

void TaskTracker::AddDistilledContentToStore(
    const DistilledArticleProto& content) {
  if (content_store_) {
    content_store_->SaveContent(
        entry_, content, DistilledContentStore::SaveCallback());
  }
}


}  // namespace dom_distiller
