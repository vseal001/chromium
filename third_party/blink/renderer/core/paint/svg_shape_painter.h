// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_PAINT_SVG_SHAPE_PAINTER_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_PAINT_SVG_SHAPE_PAINTER_H_

#include "third_party/blink/renderer/platform/graphics/paint/paint_flags.h"
#include "third_party/blink/renderer/platform/wtf/allocator.h"
#include "third_party/skia/include/core/SkPath.h"

namespace blink {

class FloatRect;
class GraphicsContext;
class LayoutSVGResourceMarker;
class LayoutSVGShape;
struct MarkerPosition;
struct PaintInfo;

class SVGShapePainter {
  STACK_ALLOCATED();

 public:
  SVGShapePainter(const LayoutSVGShape& layout_svg_shape)
      : layout_svg_shape_(layout_svg_shape) {}

  void Paint(const PaintInfo&);

 private:
  void FillShape(GraphicsContext&, const PaintFlags&, SkPath::FillType);
  void StrokeShape(GraphicsContext&, const PaintFlags&);

  void PaintMarkers(const PaintInfo&, const FloatRect& bounding_box);
  void PaintMarker(const PaintInfo&,
                   const LayoutSVGResourceMarker&,
                   const MarkerPosition&,
                   float stroke_width);
  // Paint a hit test display item and record hit test data. This should be
  // called when painting the background even if there is no other painted
  // content.
  void RecordHitTestData(const PaintInfo&);

  const LayoutSVGShape& layout_svg_shape_;
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_PAINT_SVG_SHAPE_PAINTER_H_
