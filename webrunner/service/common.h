// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef WEBRUNNER_SERVICE_COMMON_H_
#define WEBRUNNER_SERVICE_COMMON_H_

#include <zircon/processargs.h>

#include "webrunner/common/webrunner_export.h"

namespace base {
class FilePath;
}

namespace webrunner {
// This file contains constants and functions shared between Context and
// ContextProvider processes.

// Handle ID for the Context interface request passed from ContextProvider to
// Context process.
constexpr uint32_t kContextRequestHandleId = PA_HND(PA_USER0, 0);

// Path to the direct used to store persistent data in context process.
extern const char kWebContextDataPath[];

// Switch passed to content process when running in incognito mode, i.e. when
// there is no kWebContextDataPath.
extern const char kIncognitoSwitch[];

// Returns data directory that should be used by this context process. Should
// not be called in ContextProvider. Empty path is returned if the context
// doesn't have storage dir.
WEBRUNNER_EXPORT base::FilePath GetWebContextDataDir();

}  // namespace webrunner

#endif  // WEBRUNNER_SERVICE_COMMON_H_
