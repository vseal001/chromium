// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/shell_state.h"

#include <memory>
#include <utility>

#include "ash/shell.h"
#include "ash/ws/window_service_owner.h"
#include "services/ui/ws2/window_service.h"
#include "ui/display/display.h"
#include "ui/display/screen.h"

namespace ash {

ShellState::ShellState() = default;

ShellState::~ShellState() = default;

void ShellState::BindRequest(mojom::ShellStateRequest request) {
  bindings_.AddBinding(this, std::move(request));
}

aura::Window* ShellState::GetRootWindowForNewWindows() const {
  if (scoped_root_window_for_new_windows_)
    return scoped_root_window_for_new_windows_;
  return root_window_for_new_windows_;
}

void ShellState::SetRootWindowForNewWindows(aura::Window* root) {
  if (root == root_window_for_new_windows_)
    return;
  root_window_for_new_windows_ = root;
  NotifyAllClients();
}

void ShellState::AddClient(mojom::ShellStateClientPtr client) {
  mojom::ShellStateClient* client_impl = client.get();
  clients_.AddPtr(std::move(client));
  client_impl->SetDisplayIdForNewWindows(GetDisplayIdForNewWindows());
}

void ShellState::FlushMojoForTest() {
  clients_.FlushForTesting();
}

void ShellState::NotifyAllClients() {
  const int64_t display_id = GetDisplayIdForNewWindows();
  clients_.ForAllPtrs([display_id](mojom::ShellStateClient* client) {
    client->SetDisplayIdForNewWindows(display_id);
  });

  // WindowService broadcasts the display id over mojo to all remote apps.
  // TODO(jamescook): Move this into Shell when ShellState is removed.
  WindowServiceOwner* ws_owner = Shell::Get()->window_service_owner();
  // |ws_owner| is null during shutdown and tests. |window_service()| is null
  // during early startup.
  if (ws_owner && ws_owner->window_service())
    ws_owner->window_service()->SetDisplayForNewWindows(display_id);
}

int64_t ShellState::GetDisplayIdForNewWindows() const {
  // GetDisplayNearestWindow() handles null.
  return display::Screen::GetScreen()
      ->GetDisplayNearestWindow(GetRootWindowForNewWindows())
      .id();
}

void ShellState::SetScopedRootWindowForNewWindows(aura::Window* root) {
  if (root == scoped_root_window_for_new_windows_)
    return;
  // Only allow set and clear, not switch.
  DCHECK(!scoped_root_window_for_new_windows_ || !root);
  scoped_root_window_for_new_windows_ = root;
  NotifyAllClients();
}

}  // namespace ash
