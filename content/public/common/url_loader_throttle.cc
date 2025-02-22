// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/public/common/url_loader_throttle.h"

#include "base/logging.h"

namespace content {

void URLLoaderThrottle::Delegate::SetPriority(net::RequestPriority priority) {}
void URLLoaderThrottle::Delegate::UpdateDeferredResponseHead(
    const network::ResourceResponseHead& new_response_head) {}
void URLLoaderThrottle::Delegate::PauseReadingBodyFromNet() {}
void URLLoaderThrottle::Delegate::ResumeReadingBodyFromNet() {}

void URLLoaderThrottle::Delegate::InterceptResponse(
    network::mojom::URLLoaderPtr new_loader,
    network::mojom::URLLoaderClientRequest new_client_request,
    network::mojom::URLLoaderPtr* original_loader,
    network::mojom::URLLoaderClientRequest* original_client_request) {
  NOTIMPLEMENTED();
}

URLLoaderThrottle::Delegate::~Delegate() {}

URLLoaderThrottle::~URLLoaderThrottle() {}

void URLLoaderThrottle::DetachFromCurrentSequence() {
  NOTREACHED();
}

void URLLoaderThrottle::WillStartRequest(network::ResourceRequest* request,
                                         bool* defer) {}

void URLLoaderThrottle::WillRedirectRequest(
    const net::RedirectInfo& redirect_info,
    const network::ResourceResponseHead& response_head,
    bool* defer,
    std::vector<std::string>* to_be_removed_request_headers) {}

void URLLoaderThrottle::WillProcessResponse(
    const GURL& response_url,
    const network::ResourceResponseHead& response_head,
    bool* defer) {}

URLLoaderThrottle::URLLoaderThrottle() {}

}  // namespace content
