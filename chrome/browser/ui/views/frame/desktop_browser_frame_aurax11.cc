// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/frame/desktop_browser_frame_aurax11.h"

#include "base/command_line.h"
#include "chrome/browser/shell_integration_linux.h"
#include "chrome/browser/ui/views/frame/browser_frame.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/pref_names.h"
#include "ui/views/widget/widget.h"

DesktopBrowserFrameAuraX11::DesktopBrowserFrameAuraX11(
    BrowserFrame* browser_frame,
    BrowserView* browser_view)
    : DesktopBrowserFrameAura(browser_frame, browser_view) {
  use_custom_frame_pref_.Init(
      prefs::kUseCustomChromeFrame,
      browser_view->browser()->profile()->GetPrefs(),
      base::Bind(&DesktopBrowserFrameAuraX11::OnUseCustomChromeFrameChanged,
                 base::Unretained(this)));
}

DesktopBrowserFrameAuraX11::~DesktopBrowserFrameAuraX11() {
}

views::Widget::InitParams DesktopBrowserFrameAuraX11::GetWidgetParams() {
  views::Widget::InitParams params;
  params.native_widget = this;

  // Set up a custom WM_CLASS for some sorts of window types. This allows
  // task switchers in X11 environments to distinguish between main browser
  // windows and e.g app windows.
  const Browser& browser = *browser_view()->browser();
  params.wm_class_name =
      browser.is_app() && !browser.is_devtools()
          ? shell_integration_linux::GetWMClassFromAppName(browser.app_name())
          // This window is a hosted app or v1 packaged app.
          // NOTE: v2 packaged app windows are created by
          // ChromeNativeAppWindowViews.
          : shell_integration_linux::GetProgramClassName();
  params.wm_class_class = shell_integration_linux::GetProgramClassClass();
  const char kX11WindowRoleBrowser[] = "browser";
  const char kX11WindowRolePopup[] = "pop-up";
  params.wm_role_name = browser_view()->browser()->is_type_tabbed()
                            ? std::string(kX11WindowRoleBrowser)
                            : std::string(kX11WindowRolePopup);
  params.remove_standard_frame = UseCustomFrame();

  return params;
}

bool DesktopBrowserFrameAuraX11::UseCustomFrame() const {
  return use_custom_frame_pref_.GetValue() &&
      browser_view()->IsBrowserTypeNormal();
}

void DesktopBrowserFrameAuraX11::OnUseCustomChromeFrameChanged() {
  // Tell the window manager to add or remove system borders.
  browser_frame()->set_frame_type(
      UseCustomFrame() ? views::Widget::FRAME_TYPE_FORCE_CUSTOM
                       : views::Widget::FRAME_TYPE_FORCE_NATIVE);
  browser_frame()->FrameTypeChanged();
}
