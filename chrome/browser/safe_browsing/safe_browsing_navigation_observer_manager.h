// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_SAFE_BROWSING_SAFE_BROWSING_NAVIGATION_OBSERVER_MANAGER_H_
#define CHROME_BROWSER_SAFE_BROWSING_SAFE_BROWSING_NAVIGATION_OBSERVER_MANAGER_H_

#include "base/containers/circular_deque.h"
#include "base/feature_list.h"
#include "base/supports_user_data.h"
#include "components/safe_browsing/browser/referrer_chain_provider.h"
#include "components/safe_browsing/proto/csd.pb.h"
#include "components/sessions/core/session_id.h"
#include "content/public/browser/web_contents_observer.h"
#include "third_party/protobuf/src/google/protobuf/repeated_field.h"
#include "url/gurl.h"

class Profile;

namespace safe_browsing {

class SafeBrowsingNavigationObserver;
struct NavigationEvent;
struct ResolvedIPAddress;

// User data stored in DownloadItem for referrer chain information.
class ReferrerChainData : public base::SupportsUserData::Data {
 public:
  ReferrerChainData(std::unique_ptr<ReferrerChain> referrer_chain,
                    size_t referrer_chain_length,
                    size_t recent_navigation_to_collect);
  ~ReferrerChainData() override;
  ReferrerChain* GetReferrerChain();
  size_t referrer_chain_length() { return referrer_chain_length_; }
  size_t recent_navigations_to_collect() {
    return recent_navigations_to_collect_;
  }

  // Unique user data key used to get and set referrer chain data in
  // DownloadItem.
  static const char kDownloadReferrerChainDataKey[];

 private:
  std::unique_ptr<ReferrerChain> referrer_chain_;
  // This is the actual referrer chain length before appending recent navigation
  // events;
  size_t referrer_chain_length_;
  // |recent_navigations_to_collect_| is controlled by finch parameter. If the
  // user is incognito mode or hasn't enabled extended reporting, this value is
  // always 0.
  size_t recent_navigations_to_collect_;
};

// Struct that manages insertion, cleanup, and lookup of NavigationEvent
// objects. Its maximum size is kNavigationRecordMaxSize.
struct NavigationEventList {
 public:
  explicit NavigationEventList(std::size_t size_limit);

  ~NavigationEventList();

  // Finds the most recent navigation event that navigated to |target_url| and
  // its associated |target_main_frame_url| in the tab with ID |target_tab_id|.
  // If navigation happened in the main frame, |target_url| and
  // |target_main_frame_url| are the same.
  // If |target_url| is empty, we use its main frame url (a.k.a.
  // |target_main_frame_url|) to search for navigation events.
  // If |target_tab_id| is invalid, we look for all tabs for the most
  // recent navigation to |target_url| or |target_main_frame_url|.
  // For some cases, the most recent navigation to |target_url| may not be
  // relevant.
  // For example, url1 in window A opens url2 in window B, url1 then opens an
  // about:blank page window C and injects script code in it to trigger a
  // delayed event (e.g. a download) in Window D. Before the event occurs, url2
  // in window B opens a different about:blank page in window C.
  // A ---- C - D
  //   \   /
  //     B
  // In this case, FindNavigationEvent() will think url2 in Window B is the
  // referrer of about::blank in Window C since this navigation is more recent.
  // However, it does not prevent us to attribute url1 in Window A as the cause
  // of all these navigations.
  NavigationEvent* FindNavigationEvent(const base::Time& last_event_timestamp,
                                       const GURL& target_url,
                                       const GURL& target_main_frame_url,
                                       SessionID target_tab_id);

  // Finds the most recent retargeting NavigationEvent that satisfies
  // |target_url|, and |target_tab_id|.
  NavigationEvent* FindRetargetingNavigationEvent(
      const base::Time& last_event_timestamp,
      const GURL& target_url,
      SessionID target_tab_id);

  void RecordNavigationEvent(std::unique_ptr<NavigationEvent> nav_event);

  // Removes stale NavigationEvents and return the number of items removed.
  std::size_t CleanUpNavigationEvents();

  std::size_t Size() { return navigation_events_.size(); }

  NavigationEvent* Get(std::size_t index) {
    return navigation_events_[index].get();
  }

  const base::circular_deque<std::unique_ptr<NavigationEvent>>&
  navigation_events() {
    return navigation_events_;
  }

 private:
  base::circular_deque<std::unique_ptr<NavigationEvent>> navigation_events_;
  const std::size_t size_limit_;
};

// Manager class for SafeBrowsingNavigationObserver, which is in charge of
// cleaning up stale navigation events, and identifying landing page/landing
// referrer for a specific Safe Browsing event.
class SafeBrowsingNavigationObserverManager
    : public base::RefCountedThreadSafe<SafeBrowsingNavigationObserverManager>,
      public ReferrerChainProvider {
 public:
  // Helper function to check if user gesture is older than
  // kUserGestureTTLInSecond.
  static bool IsUserGestureExpired(const base::Time& timestamp);

  // Helper function to strip ref fragment from a URL. Many pages end up with a
  // fragment (e.g. http://bar.com/index.html#foo) at the end due to in-page
  // navigation or a single "#" at the end due to navigation triggered by
  // href="#" and javascript onclick function. We don't want to have separate
  // entries for these cases in the maps.
  static GURL ClearURLRef(const GURL& url);

  // Checks if we should enable observing navigations for safe browsing purpose.
  // Return true if the safe browsing safe browsing service is enabled and
  // initialized.
  static bool IsEnabledAndReady(Profile* profile);

  // Sanitize referrer chain by only keeping origin information of all URLs.
  static void SanitizeReferrerChain(ReferrerChain* referrer_chain);

  SafeBrowsingNavigationObserverManager();

  // Adds |nav_event| to |navigation_event_list_|. Object pointed to by
  // |nav_event| will be no longer accessible after this function.
  void RecordNavigationEvent(std::unique_ptr<NavigationEvent> nav_event);
  void RecordUserGestureForWebContents(content::WebContents* web_contents,
                                       const base::Time& timestamp);
  void OnUserGestureConsumed(content::WebContents* web_contents,
                             const base::Time& timestamp);
  bool HasUserGesture(content::WebContents* web_contents);
  void RecordHostToIpMapping(const std::string& host, const std::string& ip);

  // Clean-ups need to be done when a WebContents gets destroyed.
  void OnWebContentDestroyed(content::WebContents* web_contents);

  // Removes all the observed NavigationEvents, user gestures, and resolved IP
  // addresses that are older than kNavigationFootprintTTLInSecond.
  void CleanUpStaleNavigationFootprints();

  // Based on the |target_url| and |target_tab_id|, traces back the observed
  // NavigationEvents in navigation_event_list_ to identify the sequence of
  // navigations leading to the target, with the coverage limited to
  // |user_gesture_count_limit| number of user gestures. Then converts these
  // identified NavigationEvents into ReferrerChainEntrys and append them to
  // |out_referrer_chain|.
  AttributionResult IdentifyReferrerChainByEventURL(
      const GURL& event_url,
      SessionID event_tab_id,  // Invalid if tab id is unknown or not available.
      int user_gesture_count_limit,
      ReferrerChain* out_referrer_chain) override;

  // Based on the |web_contents| associated with an event, traces back the
  // observed NavigationEvents in |navigation_event_list_| to identify the
  // sequence of navigations leading to the event hosting page, with the
  // coverage limited to |user_gesture_count_limit| number of user gestures.
  // Then converts these identified NavigationEvents into ReferrerChainEntrys
  // and append them to |out_referrer_chain|.
  AttributionResult IdentifyReferrerChainByWebContents(
      content::WebContents* web_contents,
      int user_gesture_count_limit,
      ReferrerChain* out_referrer_chain) override;

  // Based on the |initiating_frame_url| and its associated |tab_id|, traces
  // back the observed NavigationEvents in navigation_event_list_ to identify
  // those navigations leading to this |initiating_frame_url|. If this
  // initiating frame has a user gesture, we trace back with the coverage
  // limited to |user_gesture_count_limit|-1 number of user gestures, otherwise
  // we trace back |user_gesture_count_limit| number of user gestures. We then
  // converts these identified NavigationEvents into ReferrerChainEntrys and
  // appends them to |out_referrer_chain|.
  AttributionResult IdentifyReferrerChainByHostingPage(
      const GURL& initiating_frame_url,
      const GURL& initiating_main_frame_url,
      SessionID tab_id,
      bool has_user_gesture,
      int user_gesture_count_limit,
      ReferrerChain* out_referrer_chain);

  // Records the creation of a new WebContents by |source_web_contents|. This is
  // used to detect cross-frame and cross-tab navigations.
  void RecordNewWebContents(content::WebContents* source_web_contents,
                            int source_render_process_id,
                            int source_render_frame_id,
                            GURL target_url,
                            ui::PageTransition page_transition,
                            content::WebContents* target_web_contents,
                            bool renderer_initiated);

  // Based on user state, attribution result and finch parameter, calculates the
  // number of recent navigations we want to append to the referrer chain.
  static size_t CountOfRecentNavigationsToAppend(const Profile& profile,
                                                 AttributionResult result);

  // Appends |recent_navigation_count| number of recent navigation events to
  // referrer chain in reverse chronological order.
  void AppendRecentNavigations(size_t recent_navigation_count,
                               ReferrerChain* out_referrer_chain);

 private:
  friend class base::RefCountedThreadSafe<
      SafeBrowsingNavigationObserverManager>;
  friend class TestNavigationObserverManager;
  friend class SBNavigationObserverBrowserTest;
  friend class SBNavigationObserverTest;

  struct GurlHash {
    std::size_t operator()(const GURL& url) const {
      return std::hash<std::string>()(url.spec());
    }
  };

  typedef std::unordered_map<content::WebContents*, base::Time> UserGestureMap;
  typedef std::unordered_map<std::string, std::vector<ResolvedIPAddress>>
      HostToIpMap;

  virtual ~SafeBrowsingNavigationObserverManager();

  NavigationEventList* navigation_event_list() {
    return &navigation_event_list_;
  }

  HostToIpMap* host_to_ip_map() { return &host_to_ip_map_; }

  // Remove stale entries from navigation_event_list_ if they are older than
  // kNavigationFootprintTTLInSecond (2 minutes).
  void CleanUpNavigationEvents();

  // Remove stale entries from user_gesture_map_ if they are older than
  // kNavigationFootprintTTLInSecond (2 minutes).
  void CleanUpUserGestures();

  // Remove stale entries from host_to_ip_map_ if they are older than
  // kNavigationFootprintTTLInSecond (2 minutes).
  void CleanUpIpAddresses();

  bool IsCleanUpScheduled() const;

  void ScheduleNextCleanUpAfterInterval(base::TimeDelta interval);

  void AddToReferrerChain(ReferrerChain* referrer_chain,
                          NavigationEvent* nav_event,
                          const GURL& destination_main_frame_url,
                          ReferrerChainEntry::URLType type);

  // Helper function to get the remaining referrer chain when we've already
  // traced back |current_user_gesture_count| number of user gestures.
  // This function modifies the |out_referrer_chain| and |out_result|.
  void GetRemainingReferrerChain(NavigationEvent* last_nav_event_traced,
                                 int current_user_gesture_count,
                                 int user_gesture_count_limit,
                                 ReferrerChain* out_referrer_chain,
                                 AttributionResult* out_result);

  // navigation_event_list_ keeps track of all the observed navigations. Since
  // the same url can be requested multiple times across different tabs and
  // frames, this list of NavigationEvents are ordered by navigation finish
  // time. Entries in navigation_event_list_ will be removed if they are older
  // than 2 minutes since their corresponding navigations finish or there are
  // more than kNavigationRecordMaxSize entries.
  NavigationEventList navigation_event_list_;

  // user_gesture_map_ keeps track of the timestamp of last user gesture in
  // in each WebContents. We assume for majority of cases, a navigation
  // shortly after a user gesture indicate this navigation is user initiated.
  UserGestureMap user_gesture_map_;

  // Host to timestamped IP addresses map that covers all the main frame and
  // subframe URLs' hosts. Since it is possible for a host to resolve to more
  // than one IP in even a short period of time, we map a single host to a
  // vector of ResolvedIPAddresss.
  HostToIpMap host_to_ip_map_;

  base::OneShotTimer cleanup_timer_;

  DISALLOW_COPY_AND_ASSIGN(SafeBrowsingNavigationObserverManager);
};
}  // namespace safe_browsing

#endif  // CHROME_BROWSER_SAFE_BROWSING_SAFE_BROWSING_NAVIGATION_OBSERVER_MANAGER_H_
