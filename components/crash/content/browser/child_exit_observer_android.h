// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_CRASH_CONTENT_BROWSER_CHILD_EXIT_OBSERVER_ANDROID_H_
#define COMPONENTS_CRASH_CONTENT_BROWSER_CHILD_EXIT_OBSERVER_ANDROID_H_

#include <map>
#include <memory>
#include <vector>

#include "base/android/application_status_listener.h"
#include "base/android/child_process_binding_types.h"
#include "base/lazy_instance.h"
#include "base/memory/ref_counted.h"
#include "base/process/process.h"
#include "base/scoped_observer.h"
#include "base/synchronization/lock.h"
#include "components/crash/content/browser/crash_handler_host_linux.h"
#include "content/public/browser/browser_child_process_observer.h"
#include "content/public/browser/notification_observer.h"
#include "content/public/browser/notification_registrar.h"
#include "content/public/browser/posix_file_descriptor_info.h"
#include "content/public/common/child_process_host.h"
#include "content/public/common/process_type.h"

namespace content {
struct ChildProcessTerminationInfo;
}

namespace crash_reporter {

// This class centralises the observation of child processes for the
// purpose of reacting to child process crashes.
// The ChildExitObserver instance exists on the browser main thread.
class ChildExitObserver : public content::BrowserChildProcessObserver,
                          public content::NotificationObserver,
                          public crashpad::CrashHandlerHost::Observer {
 public:
  struct TerminationInfo {
    // Used to indicate the child did not receive a crash signal.
    static constexpr int kInvalidSigno = -1;

    TerminationInfo();
    TerminationInfo(const TerminationInfo& other);
    TerminationInfo& operator=(const TerminationInfo& other);

    // TODO(jperaza): Not valid until Crashpad is enabled.
    bool is_crashed() const { return crash_signo != kInvalidSigno; }

    int process_host_id = content::ChildProcessHost::kInvalidUniqueID;
    base::ProcessHandle pid = base::kNullProcessHandle;
    content::ProcessType process_type = content::PROCESS_TYPE_UNKNOWN;
    base::android::ApplicationState app_state =
        base::android::APPLICATION_STATE_UNKNOWN;

    // TODO(jperaza): Not valid until Crashpad is enabled.
    // The crash signal the child process received before it exited.
    int crash_signo = kInvalidSigno;

    // True if this is intentional shutdown of the child process, e.g. when a
    // tab is closed. Some fields below may not be populated if this is true.
    bool normal_termination = false;

    // Values from ChildProcessTerminationInfo.
    // Note base::TerminationStatus and exit_code are missing intentionally
    // because those fields hold no useful information on Android.
    base::android::ChildBindingState binding_state =
        base::android::ChildBindingState::UNBOUND;
    bool was_killed_intentionally_by_browser = false;
    int remaining_process_with_strong_binding = 0;
    int remaining_process_with_moderate_binding = 0;
    int remaining_process_with_waived_binding = 0;

    // Note this is slightly different |has_oom_protection_bindings|.
    // This is equivalent to status == TERMINATION_STATUS_NORMAL_TERMINATION,
    // which historically also checked whether app is in foreground, using
    // a slightly different implementation than
    // ApplicationStatusListener::GetState.
    bool was_oom_protected_status = false;

    // Applies to renderer process only. Generally means renderer is hosting
    // one or more visible tabs.
    bool renderer_has_visible_clients = false;

    // Applies to renderer process only. Generally true indicates that there
    // is no main frame being hosted in this renderer process. Note there are
    // edge cases, eg if an invisible main frame and a visible sub frame from
    // different tabs are sharing the same renderer, then this is false.
    bool renderer_was_subframe = false;
  };

  // ChildExitObserver client interface.
  // Client methods will be called synchronously in the order in which
  // clients were registered. It is the implementer's responsibility
  // to post tasks to the appropriate threads if required (and be
  // aware that this may break ordering guarantees).
  //
  // Note, callbacks are generated for both "child processes" which are hosted
  // by BrowserChildProcessHosts, and "render processes" which are hosted by
  // RenderProcessHosts. The unique ids correspond to either the
  // ChildProcessData::id, or the RenderProcessHost::ID, depending on the
  // process type.
  class Client {
   public:
    // OnChildStart is called on the launcher thread.
    virtual void OnChildStart(int process_host_id,
                              content::PosixFileDescriptorInfo* mappings) = 0;
    // OnChildExit is called on the UI thread.
    // OnChildExit may be called twice for the same process.
    virtual void OnChildExit(const TerminationInfo& info) = 0;

    virtual ~Client() {}
  };

  // The global ChildExitObserver instance is created by calling
  // Create (on the UI thread), and lives until process exit. Tests
  // making use of this class should register an AtExitManager.
  static void Create();

  // Fetch a pointer to the global ChildExitObserver instance. The
  // global instance must have been created by the time GetInstance is
  // called.
  static ChildExitObserver* GetInstance();

  void RegisterClient(std::unique_ptr<Client> client);

  // crashpad::CrashHandlerHost::Observer
  void ChildReceivedCrashSignal(base::ProcessId pid, int signo) override;

  // BrowserChildProcessStarted must be called from
  // ContentBrowserClient::GetAdditionalMappedFilesForChildProcess
  // overrides, to notify the ChildExitObserver of child process
  // creation, and to allow clients to register any fd mappings they
  // need.
  void BrowserChildProcessStarted(int process_host_id,
                                  content::PosixFileDescriptorInfo* mappings);

 private:
  friend struct base::LazyInstanceTraitsBase<ChildExitObserver>;

  ChildExitObserver();
  ~ChildExitObserver() override;

  // content::BrowserChildProcessObserver implementation:
  void BrowserChildProcessHostDisconnected(
      const content::ChildProcessData& data) override;
  void BrowserChildProcessKilled(
      const content::ChildProcessData& data,
      const content::ChildProcessTerminationInfo& info) override;

  // NotificationObserver implementation:
  void Observe(int type,
               const content::NotificationSource& source,
               const content::NotificationDetails& details) override;

  // Called on child process exit (including crash).
  void OnChildExit(TerminationInfo* info);

  content::NotificationRegistrar notification_registrar_;

  base::Lock registered_clients_lock_;
  std::vector<std::unique_ptr<Client>> registered_clients_;

  // process_host_id to process id. Only accessed on the UI thread.
  std::map<int, base::ProcessHandle> process_host_id_to_pid_;

  // Key is process_host_id. Only used for BrowserChildProcessHost. Only
  // accessed on the UI thread.
  std::map<int, TerminationInfo> browser_child_process_info_;

  base::Lock crash_signals_lock_;
  std::map<base::ProcessId, int> child_pid_to_crash_signal_;
  ScopedObserver<crashpad::CrashHandlerHost,
                 crashpad::CrashHandlerHost::Observer>
      scoped_observer_;

  DISALLOW_COPY_AND_ASSIGN(ChildExitObserver);
};

}  // namespace crash_reporter

#endif  // COMPONENTS_CRASH_CONTENT_BROWSER_CHILD_EXIT_OBSERVER_ANDROID_H_
