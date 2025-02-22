// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/public/cpp/frame_border_hit_test.h"

#include "ash/public/cpp/ash_constants.h"
#include "ui/aura/env.h"
#include "ui/base/hit_test.h"
#include "ui/views/view_properties.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_delegate.h"
#include "ui/views/window/non_client_view.h"

namespace ash {

int FrameBorderNonClientHitTest(views::NonClientFrameView* view,
                                const gfx::Point& point_in_widget) {
  gfx::Rect expanded_bounds = view->bounds();
  int outside_bounds = kResizeOutsideBoundsSize;

  if (aura::Env::GetInstance()->is_touch_down())
    outside_bounds *= kResizeOutsideBoundsScaleForTouch;
  expanded_bounds.Inset(-outside_bounds, -outside_bounds);

  if (!expanded_bounds.Contains(point_in_widget))
    return HTNOWHERE;

  // Check the frame first, as we allow a small area overlapping the contents
  // to be used for resize handles.
  views::Widget* widget = view->GetWidget();
  bool can_ever_resize = widget->widget_delegate()->CanResize();
  // Don't allow overlapping resize handles when the window is maximized or
  // fullscreen, as it can't be resized in those states.
  int resize_border = kResizeInsideBoundsSize;
  if (widget->IsMaximized() || widget->IsFullscreen()) {
    resize_border = 0;
    can_ever_resize = false;
  }
  int frame_component = view->GetHTComponentForFrame(
      point_in_widget, resize_border, resize_border, kResizeAreaCornerSize,
      kResizeAreaCornerSize, can_ever_resize);
  if (frame_component != HTNOWHERE)
    return frame_component;

  int client_component =
      widget->client_view()->NonClientHitTest(point_in_widget);
  if (client_component != HTNOWHERE)
    return client_component;

  // Check if it intersects with children (frame caption button, back button,
  // etc.).
  gfx::Point point_in_non_client_view(point_in_widget);
  views::View::ConvertPointFromWidget(widget->non_client_view(),
                                      &point_in_non_client_view);
  for (views::View* target_view =
           widget->non_client_view()->GetEventHandlerForPoint(
               point_in_non_client_view);
       target_view; target_view = target_view->parent()) {
    int target_component =
        target_view->GetProperty(views::kHitTestComponentKey);
    if (target_component != HTNOWHERE)
      return target_component;
    if (target_view == widget->non_client_view())
      break;
  }

  // Caption is a safe default.
  return HTCAPTION;
}

}  // namespace ash
