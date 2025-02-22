/*
 * Copyright (C) 2007 Apple Inc.
 * Copyright (C) 2007 Alp Toker <alp@atoker.com>
 * Copyright (C) 2008 Collabora Ltd.
 * Copyright (C) 2008, 2009 Google Inc.
 * Copyright (C) 2009 Kenneth Rohde Christiansen
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 */

#include "third_party/blink/renderer/core/paint/theme_painter_default.h"

#include "third_party/blink/public/platform/platform.h"
#include "third_party/blink/public/platform/web_rect.h"
#include "third_party/blink/public/platform/web_theme_engine.h"
#include "third_party/blink/renderer/core/frame/local_frame_view.h"
#include "third_party/blink/renderer/core/layout/layout_object.h"
#include "third_party/blink/renderer/core/layout/layout_progress.h"
#include "third_party/blink/renderer/core/layout/layout_theme_default.h"
#include "third_party/blink/renderer/core/paint/paint_info.h"
#include "third_party/blink/renderer/platform/graphics/color.h"
#include "third_party/blink/renderer/platform/graphics/graphics_context.h"
#include "third_party/blink/renderer/platform/graphics/graphics_context_state_saver.h"
#include "third_party/blink/renderer/platform/layout_test_support.h"

namespace blink {

namespace {

const unsigned kDefaultButtonBackgroundColor = 0xffdddddd;

bool UseMockTheme() {
  return LayoutTestSupport::IsMockThemeEnabledForTest();
}

WebThemeEngine::State GetWebThemeState(const Node* node) {
  if (!LayoutTheme::IsEnabled(node))
    return WebThemeEngine::kStateDisabled;
  if (UseMockTheme() && LayoutTheme::IsReadOnlyControl(node))
    return WebThemeEngine::kStateReadonly;
  if (LayoutTheme::IsPressed(node))
    return WebThemeEngine::kStatePressed;
  if (UseMockTheme() && LayoutTheme::IsFocused(node))
    return WebThemeEngine::kStateFocused;
  if (LayoutTheme::IsHovered(node))
    return WebThemeEngine::kStateHover;

  return WebThemeEngine::kStateNormal;
}

class DirectionFlippingScope {
 public:
  DirectionFlippingScope(const LayoutObject&, const PaintInfo&, const IntRect&);
  ~DirectionFlippingScope();

 private:
  bool needs_flipping_;
  const PaintInfo& paint_info_;
};

DirectionFlippingScope::DirectionFlippingScope(
    const LayoutObject& layout_object,
    const PaintInfo& paint_info,
    const IntRect& rect)
    : needs_flipping_(!layout_object.StyleRef().IsLeftToRightDirection()),
      paint_info_(paint_info) {
  if (!needs_flipping_)
    return;
  paint_info_.context.Save();
  paint_info_.context.Translate(2 * rect.X() + rect.Width(), 0);
  paint_info_.context.Scale(-1, 1);
}

DirectionFlippingScope::~DirectionFlippingScope() {
  if (!needs_flipping_)
    return;
  paint_info_.context.Restore();
}

IntRect DeterminateProgressValueRectFor(const LayoutProgress& layout_progress,
                                        const IntRect& rect) {
  int dx = rect.Width() * layout_progress.GetPosition();
  return IntRect(rect.X(), rect.Y(), dx, rect.Height());
}

IntRect IndeterminateProgressValueRectFor(const LayoutProgress& layout_progress,
                                          const IntRect& rect) {
  // Value comes from default of GTK+.
  static const int kProgressActivityBlocks = 5;

  int value_width = rect.Width() / kProgressActivityBlocks;
  int movable_width = rect.Width() - value_width;
  if (movable_width <= 0)
    return IntRect();

  double progress = layout_progress.AnimationProgress();
  if (progress < 0.5)
    return IntRect(rect.X() + progress * 2 * movable_width, rect.Y(),
                   value_width, rect.Height());
  return IntRect(rect.X() + (1.0 - progress) * 2 * movable_width, rect.Y(),
                 value_width, rect.Height());
}

IntRect ProgressValueRectFor(const LayoutProgress& layout_progress,
                             const IntRect& rect) {
  return layout_progress.IsDeterminate()
             ? DeterminateProgressValueRectFor(layout_progress, rect)
             : IndeterminateProgressValueRectFor(layout_progress, rect);
}

IntRect ConvertToPaintingRect(const LayoutObject& input_layout_object,
                              const LayoutObject& part_layout_object,
                              LayoutRect part_rect,
                              const IntRect& local_offset) {
  // Compute an offset between the partLayoutObject and the inputLayoutObject.
  LayoutSize offset_from_input_layout_object =
      -part_layout_object.OffsetFromAncestor(&input_layout_object);
  // Move the rect into partLayoutObject's coords.
  part_rect.Move(offset_from_input_layout_object);
  // Account for the local drawing offset.
  part_rect.Move(local_offset.X(), local_offset.Y());

  return PixelSnappedIntRect(part_rect);
}

}  // namespace

ThemePainterDefault::ThemePainterDefault(LayoutThemeDefault& theme)
    : ThemePainter(), theme_(theme) {}

bool ThemePainterDefault::PaintCheckbox(const Node* node,
                                        const Document&,
                                        const ComputedStyle& style,
                                        const PaintInfo& paint_info,
                                        const IntRect& rect) {
  WebThemeEngine::ExtraParams extra_params;
  cc::PaintCanvas* canvas = paint_info.context.Canvas();
  extra_params.button.checked = LayoutTheme::IsChecked(node);
  extra_params.button.indeterminate = LayoutTheme::IsIndeterminate(node);

  float zoom_level = style.EffectiveZoom();
  GraphicsContextStateSaver state_saver(paint_info.context, false);
  IntRect unzoomed_rect = rect;
  if (zoom_level != 1) {
    state_saver.Save();
    unzoomed_rect.SetWidth(unzoomed_rect.Width() / zoom_level);
    unzoomed_rect.SetHeight(unzoomed_rect.Height() / zoom_level);
    paint_info.context.Translate(unzoomed_rect.X(), unzoomed_rect.Y());
    paint_info.context.Scale(zoom_level, zoom_level);
    paint_info.context.Translate(-unzoomed_rect.X(), -unzoomed_rect.Y());
  }

  Platform::Current()->ThemeEngine()->Paint(
      canvas, WebThemeEngine::kPartCheckbox, GetWebThemeState(node),
      WebRect(unzoomed_rect), &extra_params);
  return false;
}

bool ThemePainterDefault::PaintRadio(const Node* node,
                                     const Document&,
                                     const ComputedStyle&,
                                     const PaintInfo& paint_info,
                                     const IntRect& rect) {
  WebThemeEngine::ExtraParams extra_params;
  cc::PaintCanvas* canvas = paint_info.context.Canvas();
  extra_params.button.checked = LayoutTheme::IsChecked(node);

  Platform::Current()->ThemeEngine()->Paint(canvas, WebThemeEngine::kPartRadio,
                                            GetWebThemeState(node),
                                            WebRect(rect), &extra_params);
  return false;
}

bool ThemePainterDefault::PaintButton(const Node* node,
                                      const Document&,
                                      const ComputedStyle& style,
                                      const PaintInfo& paint_info,
                                      const IntRect& rect) {
  WebThemeEngine::ExtraParams extra_params;
  cc::PaintCanvas* canvas = paint_info.context.Canvas();
  extra_params.button.has_border = true;
  extra_params.button.background_color =
      UseMockTheme() ? 0xffc0c0c0 : kDefaultButtonBackgroundColor;
  if (style.HasBackground()) {
    extra_params.button.background_color =
        style.VisitedDependentColor(GetCSSPropertyBackgroundColor()).Rgb();
  }
  Platform::Current()->ThemeEngine()->Paint(canvas, WebThemeEngine::kPartButton,
                                            GetWebThemeState(node),
                                            WebRect(rect), &extra_params);
  return false;
}

bool ThemePainterDefault::PaintTextField(const Node* node,
                                         const ComputedStyle& style,
                                         const PaintInfo& paint_info,
                                         const IntRect& rect) {
  // WebThemeEngine does not handle border rounded corner and background image
  // so return true to draw CSS border and background.
  if (style.HasBorderRadius() || style.HasBackgroundImage())
    return true;

  ControlPart part = style.Appearance();

  WebThemeEngine::ExtraParams extra_params;
  extra_params.text_field.is_text_area = part == kTextAreaPart;
  extra_params.text_field.is_listbox = part == kListboxPart;

  cc::PaintCanvas* canvas = paint_info.context.Canvas();

  Color background_color =
      style.VisitedDependentColor(GetCSSPropertyBackgroundColor());
  extra_params.text_field.background_color = background_color.Rgb();

  Platform::Current()->ThemeEngine()->Paint(
      canvas, WebThemeEngine::kPartTextField, GetWebThemeState(node),
      WebRect(rect), &extra_params);
  return false;
}

bool ThemePainterDefault::PaintMenuList(const Node* node,
                                        const Document& document,
                                        const ComputedStyle& style,
                                        const PaintInfo& i,
                                        const IntRect& rect) {
  WebThemeEngine::ExtraParams extra_params;
  // Match Chromium Win behaviour of showing all borders if any are shown.
  extra_params.menu_list.has_border = style.HasBorder();
  extra_params.menu_list.has_border_radius = style.HasBorderRadius();
  // Fallback to transparent if the specified color object is invalid.
  Color background_color(Color::kTransparent);
  if (style.HasBackground()) {
    background_color =
        style.VisitedDependentColor(GetCSSPropertyBackgroundColor());
  }
  extra_params.menu_list.background_color = background_color.Rgb();

  // If we have a background image, don't fill the content area to expose the
  // parent's background. Also, we shouldn't fill the content area if the
  // alpha of the color is 0. The API of Windows GDI ignores the alpha.
  // FIXME: the normal Aura theme doesn't care about this, so we should
  // investigate if we really need fillContentArea.
  extra_params.menu_list.fill_content_area =
      !style.HasBackgroundImage() && background_color.Alpha();

  SetupMenuListArrow(document, style, rect, extra_params);

  cc::PaintCanvas* canvas = i.context.Canvas();
  Platform::Current()->ThemeEngine()->Paint(
      canvas, WebThemeEngine::kPartMenuList, GetWebThemeState(node),
      WebRect(rect), &extra_params);
  return false;
}

bool ThemePainterDefault::PaintMenuListButton(const Node* node,
                                              const Document& document,
                                              const ComputedStyle& style,
                                              const PaintInfo& paint_info,
                                              const IntRect& rect) {
  WebThemeEngine::ExtraParams extra_params;
  extra_params.menu_list.has_border = false;
  extra_params.menu_list.has_border_radius = style.HasBorderRadius();
  extra_params.menu_list.background_color = Color::kTransparent;
  extra_params.menu_list.fill_content_area = false;
  SetupMenuListArrow(document, style, rect, extra_params);

  cc::PaintCanvas* canvas = paint_info.context.Canvas();
  Platform::Current()->ThemeEngine()->Paint(
      canvas, WebThemeEngine::kPartMenuList, GetWebThemeState(node),
      WebRect(rect), &extra_params);
  return false;
}

void ThemePainterDefault::SetupMenuListArrow(
    const Document& document,
    const ComputedStyle& style,
    const IntRect& rect,
    WebThemeEngine::ExtraParams& extra_params) {
  const int left = rect.X() + floorf(style.BorderLeftWidth());
  const int right = rect.X() + rect.Width() - floorf(style.BorderRightWidth());
  const int middle = rect.Y() + rect.Height() / 2;

  extra_params.menu_list.arrow_y = middle;
  float arrow_box_width = theme_.ClampedMenuListArrowPaddingSize(
      document.View()->GetChromeClient(), style);
  float arrow_scale_factor = arrow_box_width / theme_.MenuListArrowWidthInDIP();
  if (UseMockTheme()) {
    // The size and position of the drop-down button is different between
    // the mock theme and the regular aura theme.

    // Padding inside the arrowBox.
    float extra_padding = 2 * arrow_scale_factor;
    float arrow_size =
        std::min(arrow_box_width,
                 static_cast<float>(rect.Height() - style.BorderTopWidth() -
                                    style.BorderBottomWidth())) -
        2 * extra_padding;
    // |arrowX| is the middle position for mock theme engine.
    extra_params.menu_list.arrow_x =
        (style.Direction() == TextDirection::kRtl)
            ? rect.X() + extra_padding + (arrow_size / 2)
            : right - (arrow_size / 2) - extra_padding;
    extra_params.menu_list.arrow_size = arrow_size;
  } else {
    // TODO(tkent): This should be 7.0 to match scroll bar buttons.
    float arrow_size = 6.0 * arrow_scale_factor;
    // Put the 6px arrow at the center of paddingForArrow area.
    // |arrowX| is the left position for Aura theme engine.
    extra_params.menu_list.arrow_x =
        (style.Direction() == TextDirection::kRtl)
            ? left + (arrow_box_width - arrow_size) / 2
            : right - (arrow_box_width + arrow_size) / 2;
    extra_params.menu_list.arrow_size = arrow_size;
  }
  extra_params.menu_list.arrow_color =
      style.VisitedDependentColor(GetCSSPropertyColor()).Rgb();
}

bool ThemePainterDefault::PaintSliderTrack(const LayoutObject& o,
                                           const PaintInfo& i,
                                           const IntRect& rect) {
  WebThemeEngine::ExtraParams extra_params;
  cc::PaintCanvas* canvas = i.context.Canvas();
  extra_params.slider.vertical =
      o.StyleRef().Appearance() == kSliderVerticalPart;

  PaintSliderTicks(o, i, rect);

  // FIXME: Mock theme doesn't handle zoomed sliders.
  float zoom_level = UseMockTheme() ? 1 : o.StyleRef().EffectiveZoom();
  GraphicsContextStateSaver state_saver(i.context, false);
  IntRect unzoomed_rect = rect;
  if (zoom_level != 1) {
    state_saver.Save();
    unzoomed_rect.SetWidth(unzoomed_rect.Width() / zoom_level);
    unzoomed_rect.SetHeight(unzoomed_rect.Height() / zoom_level);
    i.context.Translate(unzoomed_rect.X(), unzoomed_rect.Y());
    i.context.Scale(zoom_level, zoom_level);
    i.context.Translate(-unzoomed_rect.X(), -unzoomed_rect.Y());
  }

  Platform::Current()->ThemeEngine()->Paint(
      canvas, WebThemeEngine::kPartSliderTrack, GetWebThemeState(o.GetNode()),
      WebRect(unzoomed_rect), &extra_params);
  return false;
}

bool ThemePainterDefault::PaintSliderThumb(const Node* node,
                                           const ComputedStyle& style,
                                           const PaintInfo& paint_info,
                                           const IntRect& rect) {
  WebThemeEngine::ExtraParams extra_params;
  cc::PaintCanvas* canvas = paint_info.context.Canvas();
  extra_params.slider.vertical = style.Appearance() == kSliderThumbVerticalPart;
  extra_params.slider.in_drag = LayoutTheme::IsPressed(node);

  // FIXME: Mock theme doesn't handle zoomed sliders.
  float zoom_level = UseMockTheme() ? 1 : style.EffectiveZoom();
  GraphicsContextStateSaver state_saver(paint_info.context, false);
  IntRect unzoomed_rect = rect;
  if (zoom_level != 1) {
    state_saver.Save();
    unzoomed_rect.SetWidth(unzoomed_rect.Width() / zoom_level);
    unzoomed_rect.SetHeight(unzoomed_rect.Height() / zoom_level);
    paint_info.context.Translate(unzoomed_rect.X(), unzoomed_rect.Y());
    paint_info.context.Scale(zoom_level, zoom_level);
    paint_info.context.Translate(-unzoomed_rect.X(), -unzoomed_rect.Y());
  }

  Platform::Current()->ThemeEngine()->Paint(
      canvas, WebThemeEngine::kPartSliderThumb, GetWebThemeState(node),
      WebRect(unzoomed_rect), &extra_params);
  return false;
}

bool ThemePainterDefault::PaintInnerSpinButton(const Node* node,
                                               const ComputedStyle& style,
                                               const PaintInfo& paint_info,
                                               const IntRect& rect) {
  WebThemeEngine::ExtraParams extra_params;
  cc::PaintCanvas* canvas = paint_info.context.Canvas();
  extra_params.inner_spin.spin_up =
      (LayoutTheme::ControlStatesForNode(node, style) & kSpinUpControlState);
  extra_params.inner_spin.read_only = LayoutTheme::IsReadOnlyControl(node);

  Platform::Current()->ThemeEngine()->Paint(
      canvas, WebThemeEngine::kPartInnerSpinButton, GetWebThemeState(node),
      WebRect(rect), &extra_params);
  return false;
}

bool ThemePainterDefault::PaintProgressBar(const LayoutObject& o,
                                           const PaintInfo& i,
                                           const IntRect& rect) {
  if (!o.IsProgress())
    return true;

  const LayoutProgress& layout_progress = ToLayoutProgress(o);
  IntRect value_rect = ProgressValueRectFor(layout_progress, rect);

  WebThemeEngine::ExtraParams extra_params;
  extra_params.progress_bar.determinate = layout_progress.IsDeterminate();
  extra_params.progress_bar.value_rect_x = value_rect.X();
  extra_params.progress_bar.value_rect_y = value_rect.Y();
  extra_params.progress_bar.value_rect_width = value_rect.Width();
  extra_params.progress_bar.value_rect_height = value_rect.Height();

  DirectionFlippingScope scope(o, i, rect);
  cc::PaintCanvas* canvas = i.context.Canvas();
  Platform::Current()->ThemeEngine()->Paint(
      canvas, WebThemeEngine::kPartProgressBar, GetWebThemeState(o.GetNode()),
      WebRect(rect), &extra_params);
  return false;
}

bool ThemePainterDefault::PaintTextArea(const Node* node,
                                        const ComputedStyle& style,
                                        const PaintInfo& paint_info,
                                        const IntRect& rect) {
  return PaintTextField(node, style, paint_info, rect);
}

bool ThemePainterDefault::PaintSearchField(const Node* node,
                                           const ComputedStyle& style,
                                           const PaintInfo& paint_info,
                                           const IntRect& rect) {
  return PaintTextField(node, style, paint_info, rect);
}

bool ThemePainterDefault::PaintSearchFieldCancelButton(
    const LayoutObject& cancel_button_object,
    const PaintInfo& paint_info,
    const IntRect& r) {
  // Get the layoutObject of <input> element.
  if (!cancel_button_object.GetNode())
    return false;
  Node* input = cancel_button_object.GetNode()->OwnerShadowHost();
  const LayoutObject& base_layout_object = input && input->GetLayoutObject()
                                               ? *input->GetLayoutObject()
                                               : cancel_button_object;
  if (!base_layout_object.IsBox())
    return false;
  const LayoutBox& input_layout_box = ToLayoutBox(base_layout_object);
  LayoutRect input_content_box = input_layout_box.ContentBoxRect();

  // Make sure the scaled button stays square and will fit in its parent's box.
  LayoutUnit cancel_button_size =
      std::min(input_content_box.Width(),
               std::min(input_content_box.Height(), LayoutUnit(r.Height())));
  // Calculate cancel button's coordinates relative to the input element.
  // Center the button vertically.  Round up though, so if it has to be one
  // pixel off-center, it will be one pixel closer to the bottom of the field.
  // This tends to look better with the text.
  LayoutRect cancel_button_rect(
      cancel_button_object.OffsetFromAncestor(&input_layout_box)
          .Width(),
      input_content_box.Y() +
          (input_content_box.Height() - cancel_button_size + 1) / 2,
      cancel_button_size, cancel_button_size);
  IntRect painting_rect = ConvertToPaintingRect(
      input_layout_box, cancel_button_object, cancel_button_rect, r);

  DEFINE_STATIC_REF(Image, cancel_image,
                    (Image::LoadPlatformResource("searchCancel")));
  DEFINE_STATIC_REF(Image, cancel_pressed_image,
                    (Image::LoadPlatformResource("searchCancelPressed")));
  paint_info.context.DrawImage(
      LayoutTheme::IsPressed(cancel_button_object.GetNode())
          ? cancel_pressed_image
          : cancel_image,
      Image::kSyncDecode, FloatRect(painting_rect));
  return false;
}

}  // namespace blink
