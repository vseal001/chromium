// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/background_fetch/background_fetch_scheduler.h"

#include "base/guid.h"
#include "content/browser/background_fetch/background_fetch_job_controller.h"

namespace content {

BackgroundFetchScheduler::Controller::Controller(
    const BackgroundFetchRegistrationId& registration_id,
    FinishedCallback finished_callback)
    : registration_id_(registration_id),
      finished_callback_(std::move(finished_callback)) {
  DCHECK(finished_callback_);
}

BackgroundFetchScheduler::Controller::~Controller() = default;

void BackgroundFetchScheduler::Controller::Finish(
    BackgroundFetchReasonToAbort reason_to_abort) {
  DCHECK(reason_to_abort != BackgroundFetchReasonToAbort::NONE ||
         !HasMoreRequests());

  if (reason_to_abort != BackgroundFetchReasonToAbort::NONE)
    aborted_ = true;

  DCHECK(finished_callback_);
  std::move(finished_callback_).Run(registration_id_, reason_to_abort);
}

BackgroundFetchScheduler::BackgroundFetchScheduler(
    BackgroundFetchScheduler::RequestProvider* request_provider)
    : request_provider_(request_provider) {}

BackgroundFetchScheduler::~BackgroundFetchScheduler() = default;

void BackgroundFetchScheduler::RemoveJobController(
    const BackgroundFetchRegistrationId& registration_id) {
  for (auto iter = controller_queue_.begin(); iter != controller_queue_.end();
       /* no increment */) {
    if ((**iter).registration_id() == registration_id)
      iter = controller_queue_.erase(iter);
    else
      iter++;
  }
}

void BackgroundFetchScheduler::AddJobController(
    BackgroundFetchScheduler::Controller* controller) {
  controller_queue_.push_back(controller);

  if (!controller_queue_.empty() &&
      download_controller_map_.size() < max_concurrent_downloads_) {
    ScheduleDownload();
  }
}

void BackgroundFetchScheduler::ScheduleDownload() {
  DCHECK(download_controller_map_.size() < max_concurrent_downloads_);

  if (lock_scheduler_ || controller_queue_.empty())
    return;

  auto* controller = controller_queue_.front();
  controller_queue_.pop_front();

  // Making an async call, `ScheduleDownload` shouldn't be called anymore.
  lock_scheduler_ = true;
  request_provider_->PopNextRequest(
      controller->registration_id(),
      base::BindOnce(&BackgroundFetchScheduler::DidPopNextRequest,
                     base::Unretained(this), controller));
}

void BackgroundFetchScheduler::DidPopNextRequest(
    BackgroundFetchScheduler::Controller* controller,
    scoped_refptr<BackgroundFetchRequestInfo> request_info) {
  DCHECK(controller);

  lock_scheduler_ = false;  // Can schedule downloads again.

  // Storage error, fetch might have been aborted.
  if (!request_info) {
    ScheduleDownload();
    return;
  }

  // Database tasks issued by the DataManager cannot be recalled, which means
  // that it's possible to have a race condition where a request will have been
  // retrieved for a controller that's otherwise been aborted.
  if (controller->aborted())
    return;

  download_controller_map_[request_info->download_guid()] = controller;
  controller->StartRequest(request_info);

  if (download_controller_map_.size() < max_concurrent_downloads_)
    ScheduleDownload();
}

void BackgroundFetchScheduler::MarkRequestAsComplete(
    const BackgroundFetchRegistrationId& registration_id,
    scoped_refptr<BackgroundFetchRequestInfo> request) {
  DCHECK(download_controller_map_.count(request->download_guid()));
  auto* controller = download_controller_map_[request->download_guid()];
  download_controller_map_.erase(request->download_guid());

  request_provider_->MarkRequestAsComplete(
      controller->registration_id(), request.get(),
      base::BindOnce(&BackgroundFetchScheduler::DidMarkRequestAsComplete,
                     base::Unretained(this), controller));
}

void BackgroundFetchScheduler::DidMarkRequestAsComplete(
    BackgroundFetchScheduler::Controller* controller) {
  // Database tasks issued by the DataManager cannot be recalled, which means
  // that it's possible to have a race condition where a request will have been
  // marked as complete for a controller that's otherwise been aborted.
  if (controller->aborted())
    return;

  if (controller->HasMoreRequests())
    controller_queue_.push_back(controller);
  else
    controller->Finish(BackgroundFetchReasonToAbort::NONE);

  ScheduleDownload();
}

void BackgroundFetchScheduler::OnJobAborted(
    const BackgroundFetchRegistrationId& registration_id,
    std::vector<std::string> aborted_guids) {
  // For every active download that was aborted, remove it and schedule a new
  // download.
  for (const auto& guid : aborted_guids) {
    download_controller_map_.erase(guid);
    ScheduleDownload();
  }
}

}  // namespace content
