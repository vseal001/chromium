// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/background_fetch/storage/start_next_pending_request_task.h"

#include "base/guid.h"
#include "content/browser/background_fetch/background_fetch_data_manager.h"
#include "content/browser/background_fetch/storage/database_helpers.h"
#include "content/browser/service_worker/service_worker_context_wrapper.h"

namespace content {

namespace background_fetch {

StartNextPendingRequestTask::StartNextPendingRequestTask(
    DatabaseTaskHost* host,
    const BackgroundFetchRegistrationId& registration_id,
    const BackgroundFetchRegistration& registration,
    NextRequestCallback callback)
    : DatabaseTask(host),
      registration_id_(registration_id),
      registration_(registration),
      callback_(std::move(callback)),
      weak_factory_(this) {
  DCHECK(!registration_id_.is_null());
}

StartNextPendingRequestTask::~StartNextPendingRequestTask() = default;

void StartNextPendingRequestTask::Start() {
  GetPendingRequests();
}

void StartNextPendingRequestTask::GetPendingRequests() {
  service_worker_context()->GetRegistrationUserDataByKeyPrefix(
      registration_id_.service_worker_registration_id(),
      PendingRequestKeyPrefix(registration_.unique_id),
      base::BindOnce(&StartNextPendingRequestTask::DidGetPendingRequests,
                     weak_factory_.GetWeakPtr()));
}

void StartNextPendingRequestTask::DidGetPendingRequests(
    const std::vector<std::string>& data,
    blink::ServiceWorkerStatusCode status) {
  switch (ToDatabaseStatus(status)) {
    case DatabaseStatus::kNotFound:
    case DatabaseStatus::kFailed:
      // TODO(crbug.com/780025): Log failures to UMA.
      FinishWithError(blink::mojom::BackgroundFetchError::STORAGE_ERROR);
      return;
    case DatabaseStatus::kOk:
      if (data.empty()) {
        // There are no pending requests.
        FinishWithError(blink::mojom::BackgroundFetchError::NONE);
        return;
      }
  }

  if (!pending_request_.ParseFromString(data.front())) {
    // Service Worker database has been corrupted. Abandon fetches.
    AbandonFetches(registration_id_.service_worker_registration_id());
    FinishWithError(blink::mojom::BackgroundFetchError::STORAGE_ERROR);
    return;
  }

  // Make sure there isn't already an Active Request.
  // This might happen if the browser is killed in-between writes.
  service_worker_context()->GetRegistrationUserData(
      registration_id_.service_worker_registration_id(),
      {ActiveRequestKey(pending_request_.unique_id(),
                        pending_request_.request_index())},
      base::BindOnce(&StartNextPendingRequestTask::DidFindActiveRequest,
                     weak_factory_.GetWeakPtr()));
}

void StartNextPendingRequestTask::DidFindActiveRequest(
    const std::vector<std::string>& data,
    blink::ServiceWorkerStatusCode status) {
  switch (ToDatabaseStatus(status)) {
    case DatabaseStatus::kFailed:
      FinishWithError(blink::mojom::BackgroundFetchError::STORAGE_ERROR);
      return;
    case DatabaseStatus::kNotFound:
      CreateAndStoreActiveRequest();
      return;
    case DatabaseStatus::kOk:
      // We already stored the active request.
      if (!active_request_.ParseFromString(data.front())) {
        // Service worker database has been corrupted. Abandon fetches.
        AbandonFetches(registration_id_.service_worker_registration_id());
        FinishWithError(blink::mojom::BackgroundFetchError::STORAGE_ERROR);
        return;
      }
      StartDownload();
      return;
  }
  NOTREACHED();
}

void StartNextPendingRequestTask::CreateAndStoreActiveRequest() {
  proto::BackgroundFetchActiveRequest active_request;

  active_request_.set_download_guid(base::GenerateGUID());
  active_request_.set_unique_id(pending_request_.unique_id());
  active_request_.set_request_index(pending_request_.request_index());
  // Transfer ownership of the request to avoid a potentially expensive copy.
  active_request_.set_allocated_serialized_request(
      pending_request_.release_serialized_request());

  service_worker_context()->StoreRegistrationUserData(
      registration_id_.service_worker_registration_id(),
      registration_id_.origin().GetURL(),
      {{ActiveRequestKey(active_request_.unique_id(),
                         active_request_.request_index()),
        active_request_.SerializeAsString()}},
      base::BindRepeating(&StartNextPendingRequestTask::DidStoreActiveRequest,
                          weak_factory_.GetWeakPtr()));
}

void StartNextPendingRequestTask::DidStoreActiveRequest(
    blink::ServiceWorkerStatusCode status) {
  switch (ToDatabaseStatus(status)) {
    case DatabaseStatus::kOk:
      break;
    case DatabaseStatus::kFailed:
    case DatabaseStatus::kNotFound:
      FinishWithError(blink::mojom::BackgroundFetchError::NONE);
      return;
  }
  StartDownload();
}

void StartNextPendingRequestTask::StartDownload() {
  DCHECK(!active_request_.download_guid().empty());

  auto next_request = base::MakeRefCounted<BackgroundFetchRequestInfo>(
      active_request_.request_index(),
      ServiceWorkerFetchRequest::ParseFromString(
          active_request_.serialized_request()));
  next_request->SetDownloadGuid(active_request_.download_guid());

  std::move(callback_).Run(next_request);

  // Delete the pending request.
  service_worker_context()->ClearRegistrationUserData(
      registration_id_.service_worker_registration_id(),
      {PendingRequestKey(pending_request_.unique_id(),
                         pending_request_.request_index())},
      base::BindOnce(&StartNextPendingRequestTask::DidDeletePendingRequest,
                     weak_factory_.GetWeakPtr()));
}

void StartNextPendingRequestTask::DidDeletePendingRequest(
    blink::ServiceWorkerStatusCode status) {
  // TODO(crbug.com/780025): Log failures to UMA.
  FinishWithError(blink::mojom::BackgroundFetchError::NONE);
}

void StartNextPendingRequestTask::FinishWithError(
    blink::mojom::BackgroundFetchError error) {
  if (callback_)
    std::move(callback_).Run(nullptr /* request */);
  Finished();  // Destroys |this|.
}

}  // namespace background_fetch

}  // namespace content
