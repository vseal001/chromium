// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/font_unique_name_lookup/icu_fold_case_util.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace content {

TEST(IcuFoldCaseUtilTest, FoldingExamples) {
  ASSERT_EQ(IcuFoldCase("Roboto Condensed Bold Italic"),
            IcuFoldCase("roboto condensed bold italic"));
  ASSERT_EQ(IcuFoldCase("NotoSansDevanagariUI-Bold"),
            IcuFoldCase("notosansdevanagariui-bold"));
  ASSERT_EQ(IcuFoldCase(""), IcuFoldCase(""));
  ASSERT_EQ(IcuFoldCase("12345"), IcuFoldCase("12345"));
  ASSERT_EQ(IcuFoldCase("СКОРБЬ СХОДИТ ЩЕДРОТ"),
            IcuFoldCase("скорбь сходит щедрот"));
}

}  // namespace content
