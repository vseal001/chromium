// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef EXTENSIONS_RENDERER_EXTENSION_URL_LOADER_THROTTLE_H_
#define EXTENSIONS_RENDERER_EXTENSION_URL_LOADER_THROTTLE_H_

#include <string>
#include <vector>

#include "base/macros.h"
#include "content/public/common/url_loader_throttle.h"

namespace extensions {

class ExtensionThrottleManager;

// This class monitors requests issued by extensions and throttles the request
// if there are too many requests made within a short time to urls with the same
// scheme, host, port and path. For the exact criteria for throttling, please
// also see extension_throttle_manager.cc.
class ExtensionURLLoaderThrottle : public content::URLLoaderThrottle {
 public:
  explicit ExtensionURLLoaderThrottle(ExtensionThrottleManager* manager);

  ~ExtensionURLLoaderThrottle() override;

  // content::URLLoaderThrottle:
  void WillStartRequest(network::ResourceRequest* request,
                        bool* defer) override;
  void WillRedirectRequest(
      const net::RedirectInfo& redirect_info,
      const network::ResourceResponseHead& response_head,
      bool* defer,
      std::vector<std::string>* to_be_removed_request_headers) override;
  void WillProcessResponse(const GURL& response_url,
                           const network::ResourceResponseHead& response_head,
                           bool* defer) override;

 private:
  // content::URLLoaderThrottle:
  void DetachFromCurrentSequence() override;

  ExtensionThrottleManager* manager_ = nullptr;
  int request_load_flags_ = 0;
  GURL start_request_url_;

  DISALLOW_COPY_AND_ASSIGN(ExtensionURLLoaderThrottle);
};

}  // namespace extensions

#endif  // EXTENSIONS_RENDERER_EXTENSION_URL_LOADER_THROTTLE_H_
