// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_RESOURCE_COORDINATOR_TAB_ACTIVITY_WATCHER_H_
#define CHROME_BROWSER_RESOURCE_COORDINATOR_TAB_ACTIVITY_WATCHER_H_

#include <memory>

#include "base/containers/flat_set.h"
#include "base/macros.h"
#include "base/optional.h"
#include "chrome/browser/resource_coordinator/tab_ranker/tab_score_predictor.h"
#include "chrome/browser/ui/browser_list_observer.h"
#include "chrome/browser/ui/browser_tab_strip_tracker.h"
#include "chrome/browser/ui/browser_tab_strip_tracker_delegate.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"

class TabMetricsLogger;

namespace resource_coordinator {

// Observes background tab activity in order to log UKMs for tabs and score tabs
// using the Tab Ranker. Metrics will be compared against tab reactivation/close
// events to determine the end state of each background tab.
class TabActivityWatcher : public BrowserListObserver,
                           public TabStripModelObserver,
                           public BrowserTabStripTrackerDelegate {
 public:
  TabActivityWatcher();
  ~TabActivityWatcher() override;

  // Uses the Tab Ranker model to predict a score for the tab, where a higher
  // value indicates a higher likelihood of being reactivated.
  // Returns the score if the tab could be scored.
  base::Optional<float> CalculateReactivationScore(
      content::WebContents* web_contents);

  // Returns the single instance, creating it if necessary.
  static TabActivityWatcher* GetInstance();

 private:
  friend class TabActivityWatcherTest;

  // Helper class to observe WebContents.
  // TODO(michaelpg): Merge this into TabLifecycleUnit.
  class WebContentsData;

  // Called When A Tab is closed, log necessary metrics and erase the
  // |web_contents_data| pointer in |all_closing_tabs_|.
  void OnTabClosed(WebContentsData* web_contents_data);

  // BrowserListObserver:
  void OnBrowserSetLastActive(Browser* browser) override;

  // TabStripModelObserver:
  void TabInsertedAt(TabStripModel* tab_strip_model,
                     content::WebContents* contents,
                     int index,
                     bool foreground) override;
  void TabDetachedAt(content::WebContents* contents,
                     int index,
                     bool was_active) override;
  void TabReplacedAt(TabStripModel* tab_strip_model,
                     content::WebContents* old_contents,
                     content::WebContents* new_contents,
                     int index) override;
  void TabPinnedStateChanged(TabStripModel* tab_strip_model,
                             content::WebContents* contents,
                             int index) override;
  void WillCloseAllTabs(TabStripModel* tab_strip_model) override;
  void CloseAllTabsStopped(TabStripModel* tab_strip_model,
                           CloseAllStoppedReason reason) override;

  // BrowserTabStripTrackerDelegate:
  bool ShouldTrackBrowser(Browser* browser) override;

  // Resets internal state.
  void ResetForTesting();

  std::unique_ptr<TabMetricsLogger> tab_metrics_logger_;

  // Manages registration of this class as an observer of all TabStripModels as
  // browsers are created and destroyed.
  BrowserTabStripTracker browser_tab_strip_tracker_;

  // Loads the Tab Ranker model on first use and calculates tab scores.
  tab_ranker::TabScorePredictor predictor_;

  // All WebContentsData of the browser that is currently in closing_all mode.
  base::flat_set<WebContentsData*> all_closing_tabs_;

  DISALLOW_COPY_AND_ASSIGN(TabActivityWatcher);
};

}  // namespace resource_coordinator

#endif  // CHROME_BROWSER_RESOURCE_COORDINATOR_TAB_ACTIVITY_WATCHER_H_
