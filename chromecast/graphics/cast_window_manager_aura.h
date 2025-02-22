// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMECAST_GRAPHICS_CAST_WINDOW_MANAGER_AURA_H_
#define CHROMECAST_GRAPHICS_CAST_WINDOW_MANAGER_AURA_H_

#include <memory>

#include "base/macros.h"
#include "chromecast/graphics/cast_window_manager.h"
#include "ui/aura/client/window_parenting_client.h"
#include "ui/aura/window_tree_host_platform.h"

namespace aura {
namespace client {
class DefaultCaptureClient;
class ScreenPositionClient;
}  // namespace client
}  // namespace aura

namespace chromecast {

class CastFocusClientAura;
class CastSystemGestureEventHandler;
class CastSystemGestureDispatcher;
class SideSwipeDetector;

// An aura::WindowTreeHost that correctly converts input events.
class CastWindowTreeHost : public aura::WindowTreeHostPlatform {
 public:
  CastWindowTreeHost(bool enable_input, const gfx::Rect& bounds);
  ~CastWindowTreeHost() override;

  // aura::WindowTreeHostPlatform implementation:
  void DispatchEvent(ui::Event* event) override;

  // aura::WindowTreeHost implementation
  gfx::Rect GetTransformedRootWindowBoundsInPixels(
      const gfx::Size& size_in_pixels) const override;

 private:
  const bool enable_input_;

  DISALLOW_COPY_AND_ASSIGN(CastWindowTreeHost);
};

class CastWindowManagerAura : public CastWindowManager,
                              public aura::client::WindowParentingClient {
 public:
  explicit CastWindowManagerAura(bool enable_input);
  ~CastWindowManagerAura() override;

  void Setup();

  // CastWindowManager implementation:
  void TearDown() override;
  void AddWindow(gfx::NativeView window) override;
  gfx::NativeView GetRootWindow() override;
  void SetWindowId(gfx::NativeView window, WindowId window_id) override;
  void InjectEvent(ui::Event* event) override;

  // aura::client::WindowParentingClient implementation:
  aura::Window* GetDefaultParent(aura::Window* window,
                                 const gfx::Rect& bounds) override;

  void AddGestureHandler(CastGestureHandler* handler) override;

  void RemoveGestureHandler(CastGestureHandler* handler) override;

  void SetColorInversion(bool enable) override;

  CastWindowTreeHost* window_tree_host() const;

 private:
  const bool enable_input_;
  std::unique_ptr<CastWindowTreeHost> window_tree_host_;
  std::unique_ptr<aura::client::DefaultCaptureClient> capture_client_;
  std::unique_ptr<CastFocusClientAura> focus_client_;
  std::unique_ptr<aura::client::ScreenPositionClient> screen_position_client_;
  std::unique_ptr<CastSystemGestureDispatcher> system_gesture_dispatcher_;
  std::unique_ptr<CastSystemGestureEventHandler> system_gesture_event_handler_;
  std::unique_ptr<SideSwipeDetector> side_swipe_detector_;

  DISALLOW_COPY_AND_ASSIGN(CastWindowManagerAura);
};

}  // namespace chromecast

#endif  // CHROMECAST_GRAPHICS_CAST_WINDOW_MANAGER_AURA_H_
