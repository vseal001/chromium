// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/message_center/views/message_popup_view.h"

#include "base/feature_list.h"
#include "build/build_config.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/accessibility/ax_node_data.h"
#include "ui/aura/window.h"
#include "ui/aura/window_targeter.h"
#include "ui/display/display.h"
#include "ui/display/screen.h"
#include "ui/message_center/public/cpp/features.h"
#include "ui/message_center/public/cpp/message_center_constants.h"
#include "ui/message_center/views/message_popup_collection.h"
#include "ui/message_center/views/message_view.h"
#include "ui/message_center/views/message_view_context_menu_controller.h"
#include "ui/message_center/views/message_view_factory.h"
#include "ui/message_center/views/popup_alignment_delegate.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/widget/widget.h"

#if defined(OS_WIN)
#include "ui/views/widget/desktop_aura/desktop_native_widget_aura.h"
#endif

namespace message_center {

MessagePopupView::MessagePopupView(const Notification& notification,
                                   PopupAlignmentDelegate* alignment_delegate,
                                   MessagePopupCollection* popup_collection)
    : message_view_(MessageViewFactory::Create(notification, true)),
      alignment_delegate_(alignment_delegate),
      popup_collection_(popup_collection),
      a11y_feedback_on_init_(
          notification.rich_notification_data()
              .should_make_spoken_feedback_for_popup_updates) {
#if !defined(OS_CHROMEOS)
  if (!base::FeatureList::IsEnabled(message_center::kNewStyleNotifications)) {
    context_menu_controller_ =
        std::make_unique<MessageViewContextMenuController>();
    message_view_->set_context_menu_controller(context_menu_controller_.get());
  }
#endif

  SetLayoutManager(std::make_unique<views::FillLayout>());

  if (!message_view_->IsManuallyExpandedOrCollapsed())
    message_view_->SetExpanded(message_view_->IsAutoExpandingAllowed());
  AddChildView(message_view_);
  set_notify_enter_exit_on_child(true);
}

MessagePopupView::MessagePopupView(PopupAlignmentDelegate* alignment_delegate,
                                   MessagePopupCollection* popup_collection)
    : message_view_(nullptr),
      alignment_delegate_(alignment_delegate),
      popup_collection_(popup_collection),
      a11y_feedback_on_init_(false) {
  SetLayoutManager(std::make_unique<views::FillLayout>());
}

MessagePopupView::~MessagePopupView() = default;

void MessagePopupView::UpdateContents(const Notification& notification) {
  message_view_->UpdateWithNotification(notification);
  popup_collection_->NotifyPopupResized();
  if (notification.rich_notification_data()
          .should_make_spoken_feedback_for_popup_updates) {
    NotifyAccessibilityEvent(ax::mojom::Event::kAlert, true);
  }
}

float MessagePopupView::GetOpacity() const {
  return GetWidget()->GetLayer()->opacity();
}

void MessagePopupView::SetPopupBounds(const gfx::Rect& bounds) {
  GetWidget()->SetBounds(bounds);
}

void MessagePopupView::SetOpacity(float opacity) {
  GetWidget()->SetOpacity(opacity);
}

void MessagePopupView::AutoCollapse() {
  if (is_hovered_ || message_view_->IsManuallyExpandedOrCollapsed())
    return;
  message_view_->SetExpanded(false);
}

void MessagePopupView::Show() {
  views::Widget::InitParams params(views::Widget::InitParams::TYPE_POPUP);
  params.keep_on_top = true;
#if defined(OS_LINUX) && !defined(OS_CHROMEOS)
  params.opacity = views::Widget::InitParams::OPAQUE_WINDOW;
#else
  params.opacity = views::Widget::InitParams::TRANSLUCENT_WINDOW;
#endif
  params.delegate = this;
  views::Widget* widget = new views::Widget();
  alignment_delegate_->ConfigureWidgetInitParamsForContainer(widget, &params);
  widget->set_focus_on_creation(false);
  widget->AddObserver(this);

#if defined(OS_WIN)
  // We want to ensure that this toast always goes to the native desktop,
  // not the Ash desktop (since there is already another toast contents view
  // there.
  if (!params.parent)
    params.native_widget = new views::DesktopNativeWidgetAura(widget);
#endif

  widget->Init(params);

#if defined(OS_CHROMEOS)
  // On Chrome OS, this widget is shown in the shelf container. It means this
  // widget would inherit the parent's window targeter (ShelfWindowTarget) by
  // default. But it is not good for popup. So we override it with the normal
  // WindowTargeter.
  gfx::NativeWindow native_window = widget->GetNativeWindow();
  native_window->SetEventTargeter(std::make_unique<aura::WindowTargeter>());
#endif

  widget->SetOpacity(0.0);
  widget->ShowInactive();

  if (a11y_feedback_on_init_)
    NotifyAccessibilityEvent(ax::mojom::Event::kAlert, true);
}

void MessagePopupView::Close() {
  if (!GetWidget()) {
    DeleteDelegate();
    return;
  }

  if (!GetWidget()->IsClosed())
    GetWidget()->CloseNow();
}

void MessagePopupView::OnMouseEntered(const ui::MouseEvent& event) {
  is_hovered_ = true;
  popup_collection_->Update();
}

void MessagePopupView::OnMouseExited(const ui::MouseEvent& event) {
  is_hovered_ = false;
  popup_collection_->Update();
}

void MessagePopupView::ChildPreferredSizeChanged(views::View* child) {
  popup_collection_->NotifyPopupResized();
}

void MessagePopupView::GetAccessibleNodeData(ui::AXNodeData* node_data) {
  message_view_->GetAccessibleNodeData(node_data);
  node_data->role = ax::mojom::Role::kAlertDialog;
}

const char* MessagePopupView::GetClassName() const {
  return "MessagePopupView";
}

void MessagePopupView::OnDisplayChanged() {
  OnWorkAreaChanged();
}

void MessagePopupView::OnWorkAreaChanged() {
  views::Widget* widget = GetWidget();
  if (!widget)
    return;

  gfx::NativeView native_view = widget->GetNativeView();
  if (!native_view)
    return;

  if (alignment_delegate_->RecomputeAlignment(
          display::Screen::GetScreen()->GetDisplayNearestView(native_view))) {
    popup_collection_->ResetBounds();
  }
}

void MessagePopupView::OnWidgetActivationChanged(views::Widget* widget,
                                                 bool active) {
  is_active_ = active;
  popup_collection_->Update();
}

}  // namespace message_center
