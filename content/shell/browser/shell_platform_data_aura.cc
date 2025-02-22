// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/shell/browser/shell_platform_data_aura.h"

#include "base/macros.h"
#include "build/build_config.h"
#include "content/shell/browser/shell.h"
#include "ui/aura/client/default_capture_client.h"
#include "ui/aura/env.h"
#include "ui/aura/layout_manager.h"
#include "ui/aura/test/test_focus_client.h"
#include "ui/aura/test/test_window_parenting_client.h"
#include "ui/aura/window.h"
#include "ui/aura/window_event_dispatcher.h"
#include "ui/base/ime/input_method.h"
#include "ui/base/ime/input_method_delegate.h"
#include "ui/base/ime/input_method_factory.h"
#include "ui/ozone/public/ozone_platform.h"
#include "ui/platform_window/platform_window_init_properties.h"
#include "ui/wm/core/default_activation_client.h"

#if defined(OS_FUCHSIA)
#include <fuchsia/ui/policy/cpp/fidl.h>
#include "base/fuchsia/component_context.h"
#endif

namespace content {

namespace {

class FillLayout : public aura::LayoutManager {
 public:
  explicit FillLayout(aura::Window* root)
      : root_(root), has_bounds_(!root->bounds().IsEmpty()) {}

  ~FillLayout() override {}

 private:
  // aura::LayoutManager:
  void OnWindowResized() override {
    // If window bounds were not set previously then resize all children to
    // match the size of the parent.
    if (!has_bounds_) {
      has_bounds_ = true;
      for (aura::Window* child : root_->children())
        SetChildBoundsDirect(child, gfx::Rect(root_->bounds().size()));
    }
  }

  void OnWindowAddedToLayout(aura::Window* child) override {
    child->SetBounds(root_->bounds());
  }

  void OnWillRemoveWindowFromLayout(aura::Window* child) override {}

  void OnWindowRemovedFromLayout(aura::Window* child) override {}

  void OnChildWindowVisibilityChanged(aura::Window* child,
                                      bool visible) override {}

  void SetChildBounds(aura::Window* child,
                      const gfx::Rect& requested_bounds) override {
    SetChildBoundsDirect(child, requested_bounds);
  }

  aura::Window* root_;
  bool has_bounds_;

  DISALLOW_COPY_AND_ASSIGN(FillLayout);
};

}

ShellPlatformDataAura* Shell::platform_ = nullptr;

ShellPlatformDataAura::ShellPlatformDataAura(const gfx::Size& initial_size) {
  CHECK(aura::Env::GetInstance());

  ui::PlatformWindowInitProperties properties;
  properties.bounds = gfx::Rect(initial_size);

#if defined(OS_FUCHSIA)
  // When using Scenic Ozone platform we need to supply a ViewOwner request to
  // the window. This is not necessary when using the headless ozone platform.
  if (ui::OzonePlatform::GetInstance()
          ->GetPlatformProperties()
          .needs_view_owner_request) {
    // Initialize view_owner_request for the new instance.
    fidl::InterfaceHandle<fuchsia::ui::viewsv1token::ViewOwner> view_owner;
    properties.view_owner_request = view_owner.NewRequest();

    // Request Presenter to show the view full-screen.
    auto presenter = base::fuchsia::ComponentContext::GetDefault()
                         ->ConnectToService<fuchsia::ui::policy::Presenter>();
    presenter->Present(std::move(view_owner), nullptr);
  }
#endif

  host_ = aura::WindowTreeHost::Create(std::move(properties));
  host_->InitHost();
  host_->window()->Show();
  host_->window()->SetLayoutManager(new FillLayout(host_->window()));

  focus_client_.reset(new aura::test::TestFocusClient());
  aura::client::SetFocusClient(host_->window(), focus_client_.get());

  new wm::DefaultActivationClient(host_->window());
  capture_client_.reset(
      new aura::client::DefaultCaptureClient(host_->window()));
  window_parenting_client_.reset(
      new aura::test::TestWindowParentingClient(host_->window()));
}

ShellPlatformDataAura::~ShellPlatformDataAura() = default;

void ShellPlatformDataAura::ShowWindow() {
  host_->Show();
}

void ShellPlatformDataAura::ResizeWindow(const gfx::Size& size) {
  host_->SetBoundsInPixels(gfx::Rect(size));
}

}  // namespace content
