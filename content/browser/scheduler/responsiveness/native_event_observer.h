// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_SCHEDULER_RESPONSIVENESS_NATIVE_EVENT_OBSERVER_H_
#define CONTENT_BROWSER_SCHEDULER_RESPONSIVENESS_NATIVE_EVENT_OBSERVER_H_

#include "base/callback.h"
#include "base/macros.h"
#include "build/build_config.h"
#include "content/common/content_export.h"

#if defined(OS_MACOSX)
#include "content/public/browser/native_event_processor_observer_mac.h"
#endif

namespace content {
namespace responsiveness {

// This class must only be used from the UI thread.
//
// This class hooks itself into the native event processor for the platform and
// forwards will_run/did_run callbacks to Watcher. Native events are processed
// at different layers for each platform, so the interface for this class is
// necessarily messy.
//
// On macOS, the hook should be in -[BrowserCrApplication sendEvent:].
// On Linux, the hook should be in ui::PlatformEventSource::DispatchEvent.
// On Windows, the hook should be in MessagePumpForUI::ProcessMessageHelper.
// On Android, the hook should be in <TBD>.
class CONTENT_EXPORT NativeEventObserver
#if defined(OS_MACOSX)
    : public NativeEventProcessorObserver
#endif
{
 public:
  using WillRunEventCallback =
      base::RepeatingCallback<void(const void* opaque_identifier)>;

  // |creation_time| refers to the time at which the native event was created.
  using DidRunEventCallback =
      base::RepeatingCallback<void(const void* opaque_identifier,
                                   base::TimeTicks creation_time)>;

  // The constructor will register the object as an observer of the native event
  // processor. The destructor will unregister the object.
  NativeEventObserver(WillRunEventCallback will_run_event_callback,
                      DidRunEventCallback did_run_event_callback);
  virtual ~NativeEventObserver();

 protected:
#if defined(OS_MACOSX)
  // NativeEventProcessorObserver overrides:
  // Exposed for tests.
  void WillRunNativeEvent(const void* opaque_identifier) override;
  void DidRunNativeEvent(const void* opaque_identifier,
                         base::TimeTicks creation_time) override;
#endif

 private:
  void RegisterObserver();
  void DeregisterObserver();

  WillRunEventCallback will_run_event_callback_;
  DidRunEventCallback did_run_event_callback_;

  DISALLOW_COPY_AND_ASSIGN(NativeEventObserver);
};

}  // namespace responsiveness
}  // namespace content

#endif  // CONTENT_BROWSER_SCHEDULER_RESPONSIVENESS_NATIVE_EVENT_OBSERVER_H_
