// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_TEST_SHELL_DELEGATE_H_
#define ASH_TEST_SHELL_DELEGATE_H_

#include <memory>

#include "ash/shell_delegate.h"
#include "base/macros.h"

namespace ash {

class TestShellDelegate : public ShellDelegate {
 public:
  TestShellDelegate();
  ~TestShellDelegate() override;

  // Overridden from ShellDelegate:
  bool CanShowWindowForUser(aura::Window* window) const override;
  void PreInit() override;
  std::unique_ptr<keyboard::KeyboardUI> CreateKeyboardUI() override;
  std::unique_ptr<ScreenshotDelegate> CreateScreenshotDelegate() override;
  AccessibilityDelegate* CreateAccessibilityDelegate() override;
  ui::InputDeviceControllerClient* GetInputDeviceControllerClient() override;

 private:
  DISALLOW_COPY_AND_ASSIGN(TestShellDelegate);
};

}  // namespace ash

#endif  // ASH_TEST_SHELL_DELEGATE_H_
