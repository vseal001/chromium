// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_UI_WS2_WINDOW_SERVICE_OBSERVER_H_
#define SERVICES_UI_WS2_WINDOW_SERVICE_OBSERVER_H_

#include <stdint.h>

#include "base/component_export.h"
#include "services/ui/common/types.h"

namespace ui {
namespace ws2 {

class COMPONENT_EXPORT(WINDOW_SERVICE) WindowServiceObserver {
 public:
  // Called when an event is sent to the client identified by |client_id|.
  // |event_id| is a unique identifier for the event. Once the client responds
  // to the event OnClientAckedEvent() is called.
  virtual void OnWillSendEventToClient(ClientSpecificId client_id,
                                       uint32_t event_id) {}

  // Called when the client identified by |client_id| responds to an event. See
  // OnWillSendEventToClient() for more details.
  virtual void OnClientAckedEvent(ClientSpecificId client_id,
                                  uint32_t event_id) {}

  // Client when the connection to a client is about to be destroyed.
  virtual void OnWillDestroyClient(ClientSpecificId client_id) {}

 protected:
  virtual ~WindowServiceObserver() {}
};

}  // namespace ws2
}  // namespace ui

#endif  // SERVICES_UI_WS2_WINDOW_SERVICE_OBSERVER_H_
