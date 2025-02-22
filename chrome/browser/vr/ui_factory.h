// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_VR_UI_FACTORY_H_
#define CHROME_BROWSER_VR_UI_FACTORY_H_

#include <memory>

#include "chrome/browser/vr/ui_interface.h"
#include "chrome/browser/vr/vr_export.h"

namespace vr {

class AudioDelegate;
class KeyboardDelegate;
class PlatformInputHandler;
class TextInputDelegate;
class UiBrowserInterface;
struct UiInitialState;

class VR_EXPORT UiFactory {
 public:
  static std::unique_ptr<UiInterface> Create(
      UiBrowserInterface* browser,
      PlatformInputHandler* content_input_forwarder,
      KeyboardDelegate* keyboard_delegate,
      TextInputDelegate* text_input_delegate,
      AudioDelegate* audio_delegate,
      const UiInitialState& ui_initial_state);
};

}  // namespace vr

#endif  // CHROME_BROWSER_VR_UI_FACTORY_H_
