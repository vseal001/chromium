// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_BACKGROUND_FETCH_BACKGROUND_FETCH_REQUEST_MATCH_PARAMS_H_
#define CONTENT_BROWSER_BACKGROUND_FETCH_BACKGROUND_FETCH_REQUEST_MATCH_PARAMS_H_

#include "base/optional.h"
#include "content/common/service_worker/service_worker_types.h"
#include "third_party/blink/public/platform/modules/cache_storage/cache_storage.mojom.h"

namespace content {

class CONTENT_EXPORT BackgroundFetchRequestMatchParams {
 public:
  // TODO(crbug.com/863852): Add boolean to differentiate between match vs
  // matchAll.
  BackgroundFetchRequestMatchParams();
  BackgroundFetchRequestMatchParams(
      base::Optional<ServiceWorkerFetchRequest> request_to_match,
      blink::mojom::QueryParamsPtr cache_query_params);
  ~BackgroundFetchRequestMatchParams();

  bool FilterByRequest() const {
    return request_to_match_.has_value();
  }

  // Only call this method if a valid request_to_match was previously provided.
  const ServiceWorkerFetchRequest& request_to_match() const {
    DCHECK(request_to_match_.has_value());
    return request_to_match_.value();
  }

  blink::mojom::QueryParamsPtr cloned_cache_query_params() const {
    if (!cache_query_params_)
      return nullptr;
    return cache_query_params_->Clone();
  }

 private:
  // If |request_to_match| is present, we get response(s) only for this request.
  // If not present, response(s) for all requests (contained in the fetch) will
  // be returned.
  base::Optional<ServiceWorkerFetchRequest> request_to_match_;

  // When nullptr, this has no effect on the response(s) returned.
  blink::mojom::QueryParamsPtr cache_query_params_;

  DISALLOW_COPY_AND_ASSIGN(BackgroundFetchRequestMatchParams);
};

}  // namespace content

#endif  // CONTENT_BROWSER_BACKGROUND_FETCH_BACKGROUND_FETCH_REQUEST_MATCH_PARAMS_H_
