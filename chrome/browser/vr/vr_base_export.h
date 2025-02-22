// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_VR_VR_BASE_EXPORT_H_
#define CHROME_BROWSER_VR_VR_BASE_EXPORT_H_

#if defined(COMPONENT_BUILD)
#if defined(WIN32)

#if defined(VR_BASE_IMPLEMENTATION)
#define VR_BASE_EXPORT __declspec(dllexport)
#else
#define VR_BASE_EXPORT __declspec(dllimport)
#endif  // defined(VR_BASE_IMPLEMENTATION)

#else  // defined(WIN32)
#if defined(VR_BASE_IMPLEMENTATION)
#define VR_BASE_EXPORT __attribute__((visibility("default")))
#else
#define VR_BASE_EXPORT
#endif  // defined(VR_BASE_IMPLEMENTATION)
#endif

#else  // defined(COMPONENT_BBASELD)
#define VR_BASE_EXPORT
#endif

#endif  // CHROME_BROWSER_VR_VR_BASE_EXPORT_H_
