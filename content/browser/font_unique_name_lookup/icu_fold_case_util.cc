// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/font_unique_name_lookup/icu_fold_case_util.h"

using namespace ::icu_62;

namespace content {

std::string IcuFoldCase(const std::string& name_request) {
  UnicodeString name_request_unicode = UnicodeString::fromUTF8(name_request);
  name_request_unicode.foldCase();
  std::string name_request_lower;
  name_request_unicode.toUTF8String(name_request_lower);
  return name_request_lower;
}

}  // namespace content
