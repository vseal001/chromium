// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "gpu/config/gpu_finch_features.h"

namespace features {

// Enable GPU Rasterization by default. This can still be overridden by
// --force-gpu-rasterization or --disable-gpu-rasterization.
#if defined(OS_MACOSX) || defined(OS_WIN) || defined(OS_CHROMEOS) || \
    defined(OS_ANDROID)
// DefaultEnableGpuRasterization has launched on Mac, Windows, ChromeOS, and
// Android.
const base::Feature kDefaultEnableGpuRasterization{
    "DefaultEnableGpuRasterization", base::FEATURE_ENABLED_BY_DEFAULT};
#else
const base::Feature kDefaultEnableGpuRasterization{
    "DefaultEnableGpuRasterization", base::FEATURE_DISABLED_BY_DEFAULT};
#endif

// Enable out of process rasterization by default.  This can still be overridden
// by --enable-oop-rasterization or --disable-oop-rasterization.
const base::Feature kDefaultEnableOopRasterization{
    "DefaultEnableOopRasterization", base::FEATURE_DISABLED_BY_DEFAULT};

// Use the passthrough command decoder by default.  This can be overridden with
// the --use-cmd-decoder=passthrough or --use-cmd-decoder=validating flags.
const base::Feature kDefaultPassthroughCommandDecoder{
  "DefaultPassthroughCommandDecoder",
#if defined(OS_WIN)
      base::FEATURE_ENABLED_BY_DEFAULT
#else
      base::FEATURE_DISABLED_BY_DEFAULT
#endif
};

// Use DirectComposition layers (overlays) for video if supported. Overridden by
// --enable-direct-composition-layers and --disable-direct-composition-layers.
const base::Feature kDirectCompositionOverlays{
    "DirectCompositionOverlays", base::FEATURE_ENABLED_BY_DEFAULT};

#if defined(OS_ANDROID)
// Use android AImageReader when playing videos with MediaPlayer.
const base::Feature kAImageReaderMediaPlayer{"AImageReaderMediaPlayer",
                                             base::FEATURE_DISABLED_BY_DEFAULT};
#endif

}  // namespace features
