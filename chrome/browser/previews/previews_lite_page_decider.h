// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_PREVIEWS_PREVIEWS_LITE_PAGE_DECIDER_H_
#define CHROME_BROWSER_PREVIEWS_PREVIEWS_LITE_PAGE_DECIDER_H_

#include <memory>

#include "base/gtest_prod_util.h"
#include "base/macros.h"
#include "base/optional.h"
#include "base/sequence_checker.h"
#include "base/time/time.h"

namespace content {
class NavigationHandle;
class NavigationThrottle;
}  // namespace content

// This class manages the state for triggering the Lite Page Server Preview.
// This class ensures that the feature is enabled and the
// current Profile is not incognito before handing off the real legwork of the
// triggering decision to |PreviewsLitePageNavigationThrottle|.
class PreviewsLitePageDecider {
 public:
  PreviewsLitePageDecider();
  virtual ~PreviewsLitePageDecider();

  // Checks if the feature is enabled and if so, returns a
  // |PreviewsLitePageNavigationThrottle| that handles the rest of the decision
  // making.
  static std::unique_ptr<content::NavigationThrottle> MaybeCreateThrottleFor(
      content::NavigationHandle* handle);

  // Used to notify that the Previews Server should not be sent anymore requests
  // until after the given time.
  void SetRetryAt(base::TimeTicks retry_at);

 protected:
  // Virtual for testing.
  virtual bool IsDataSaverEnabled(content::NavigationHandle* handle) const;

 private:
  FRIEND_TEST_ALL_PREFIXES(PreviewsLitePageDeciderTest, TestServerUnavailable);

  // Returns true if a Preview should not be triggered because the server is
  // unavailable.
  bool IsServerUnavailable(base::TimeTicks now);

  // The time after which it is ok to send the server more preview requests.
  base::Optional<base::TimeTicks> retry_at_;

  SEQUENCE_CHECKER(sequence_checker_);

  DISALLOW_COPY_AND_ASSIGN(PreviewsLitePageDecider);
};

#endif  // CHROME_BROWSER_PREVIEWS_PREVIEWS_LITE_PAGE_DECIDER_H_
