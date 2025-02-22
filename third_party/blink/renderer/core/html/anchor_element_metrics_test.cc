// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/html/anchor_element_metrics.h"

#include "base/optional.h"
#include "base/test/scoped_feature_list.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/blink/public/common/features.h"
#include "third_party/blink/renderer/core/html/html_anchor_element.h"
#include "third_party/blink/renderer/core/html/html_iframe_element.h"
#include "third_party/blink/renderer/core/paint/paint_layer_scrollable_area.h"
#include "third_party/blink/renderer/core/testing/sim/sim_request.h"
#include "third_party/blink/renderer/core/testing/sim/sim_test.h"
#include "third_party/blink/renderer/platform/testing/histogram_tester.h"
#include "third_party/blink/renderer/platform/wtf/text/atomic_string.h"

namespace blink {

class AnchorElementMetricsTest : public SimTest {
 public:
  static constexpr int kViewportWidth = 400;
  static constexpr int kViewportHeight = 600;

  // Helper function to test IsUrlIncrementedByOne().
  bool IsIncrementedByOne(const String& source, const String& target) {
    SimRequest main_resource(source, "text/html");
    LoadURL(source);
    main_resource.Complete("<a id='anchor' href=''>example</a>");
    HTMLAnchorElement* anchor_element =
        ToHTMLAnchorElement(GetDocument().getElementById("anchor"));
    anchor_element->SetHref(AtomicString(target));

    return AnchorElementMetrics::MaybeReportClickedMetricsOnClick(
               anchor_element)
        .value()
        .GetIsUrlIncrementedByOne();
  }

 protected:
  AnchorElementMetricsTest() = default;

  void SetUp() override {
    SimTest::SetUp();
    WebView().Resize(WebSize(kViewportWidth, kViewportHeight));
    feature_list_.InitAndEnableFeature(features::kRecordAnchorMetricsClicked);
  }

  base::test::ScopedFeatureList feature_list_;
};

// Test for IsUrlIncrementedByOne().
TEST_F(AnchorElementMetricsTest, IsUrlIncrementedByOne) {
  EXPECT_TRUE(
      IsIncrementedByOne("http://example.com/p1", "http://example.com/p2"));
  EXPECT_TRUE(IsIncrementedByOne("http://example.com/?p=9",
                                 "http://example.com/?p=10"));
  EXPECT_TRUE(IsIncrementedByOne("http://example.com/?p=12",
                                 "http://example.com/?p=13"));
  EXPECT_TRUE(IsIncrementedByOne("http://example.com/p9/cat1",
                                 "http://example.com/p10/cat1"));
  EXPECT_FALSE(
      IsIncrementedByOne("http://example.com/1", "https://example.com/2"));
  EXPECT_FALSE(
      IsIncrementedByOne("http://example.com/1", "http://google.com/2"));
  EXPECT_FALSE(
      IsIncrementedByOne("http://example.com/p1", "http://example.com/p1"));
  EXPECT_FALSE(
      IsIncrementedByOne("http://example.com/p2", "http://example.com/p1"));
  EXPECT_FALSE(IsIncrementedByOne("http://example.com/p9/cat1",
                                  "http://example.com/p10/cat2"));
}

// Test that Finch can control the collection of anchor element metrics.
TEST_F(AnchorElementMetricsTest, FinchControl) {
  HistogramTester histogram_tester;

  SimRequest resource("https://example.com/", "text/html");
  LoadURL("https://example.com/");
  resource.Complete("<a id='anchor' href='https://google.com/'>google</a>");
  HTMLAnchorElement* anchor_element =
      ToHTMLAnchorElement(GetDocument().getElementById("anchor"));

  // With feature kRecordAnchorMetricsClicked disabled, we should not see any
  // count in histograms.
  base::test::ScopedFeatureList disabled_feature_list;
  disabled_feature_list.InitAndDisableFeature(
      features::kRecordAnchorMetricsClicked);
  AnchorElementMetrics::MaybeReportClickedMetricsOnClick(anchor_element);
  histogram_tester.ExpectTotalCount("AnchorElementMetrics.Clicked.RatioArea",
                                    0);

  // If we enable feature kRecordAnchorMetricsClicked, we should see count is 1
  // in histograms.
  base::test::ScopedFeatureList enabled_feature_list;
  enabled_feature_list.InitAndEnableFeature(
      features::kRecordAnchorMetricsClicked);
  AnchorElementMetrics::MaybeReportClickedMetricsOnClick(anchor_element);
  histogram_tester.ExpectTotalCount("AnchorElementMetrics.Clicked.RatioArea",
                                    1);
}

// The main frame contains an anchor element, which contains an image element.
TEST_F(AnchorElementMetricsTest, AnchorFeatureImageLink) {
  SimRequest main_resource("https://example.com/", "text/html");

  LoadURL("https://example.com/");

  main_resource.Complete(String::Format(
      R"HTML(
    <body style='margin: 0px'>
    <div style='height: %dpx;'></div>
    <a id='anchor' href="https://example.com/page2">
      <img height="300" width="200">
    </a>
    <div style='height: %d;'></div>
    </body>)HTML",
      kViewportHeight / 2, 10 * kViewportHeight));

  Element* anchor = GetDocument().getElementById("anchor");
  HTMLAnchorElement* anchor_element = ToHTMLAnchorElement(anchor);

  auto feature =
      AnchorElementMetrics::MaybeReportClickedMetricsOnClick(anchor_element)
          .value();
  EXPECT_FLOAT_EQ(0.25, feature.GetRatioArea());
  EXPECT_FLOAT_EQ(0.25, feature.GetRatioVisibleArea());
  EXPECT_FLOAT_EQ(0.5, feature.GetRatioDistanceTopToVisibleTop());
  EXPECT_FLOAT_EQ(0.75, feature.GetRatioDistanceCenterToVisibleTop());
  EXPECT_FLOAT_EQ(0.5, feature.GetRatioDistanceRootTop());
  EXPECT_FLOAT_EQ(10, feature.GetRatioDistanceRootBottom());
  EXPECT_FALSE(feature.GetIsInIframe());
  EXPECT_TRUE(feature.GetContainsImage());
  EXPECT_TRUE(feature.GetIsSameHost());
  EXPECT_FALSE(feature.GetIsUrlIncrementedByOne());
}

// The main frame contains an anchor element.
// Features of the element are extracted.
// Then the test scrolls down to check features again.
TEST_F(AnchorElementMetricsTest, AnchorFeatureExtract) {
  SimRequest main_resource("https://example.com/", "text/html");

  LoadURL("https://example.com/");

  main_resource.Complete(String::Format(
      R"HTML(
    <body style='margin: 0px'>
    <div style='height: %dpx;'></div>
    <a id='anchor' href="https://b.example.com">example</a>
    <div style='height: %d;'></div>
    </body>)HTML",
      2 * kViewportHeight, 10 * kViewportHeight));

  Element* anchor = GetDocument().getElementById("anchor");
  HTMLAnchorElement* anchor_element = ToHTMLAnchorElement(anchor);

  auto feature =
      AnchorElementMetrics::MaybeReportClickedMetricsOnClick(anchor_element)
          .value();
  EXPECT_GT(feature.GetRatioArea(), 0);
  EXPECT_FLOAT_EQ(feature.GetRatioDistanceRootTop(), 2);
  EXPECT_FLOAT_EQ(feature.GetRatioDistanceTopToVisibleTop(), 2);
  EXPECT_EQ(feature.GetIsInIframe(), false);

  // Element not in the viewport.
  EXPECT_GT(feature.GetRatioArea(), 0);
  EXPECT_FLOAT_EQ(0, feature.GetRatioVisibleArea());
  EXPECT_FLOAT_EQ(2, feature.GetRatioDistanceTopToVisibleTop());
  EXPECT_LT(2, feature.GetRatioDistanceCenterToVisibleTop());
  EXPECT_FLOAT_EQ(2, feature.GetRatioDistanceRootTop());
  EXPECT_FLOAT_EQ(10, feature.GetRatioDistanceRootBottom());
  EXPECT_FALSE(feature.GetIsInIframe());
  EXPECT_FALSE(feature.GetContainsImage());
  EXPECT_FALSE(feature.GetIsSameHost());
  EXPECT_FALSE(feature.GetIsUrlIncrementedByOne());

  // Scroll down to the anchor element.
  GetDocument().View()->LayoutViewport()->SetScrollOffset(
      ScrollOffset(0, kViewportHeight * 1.5), kProgrammaticScroll);

  auto feature2 =
      AnchorElementMetrics::MaybeReportClickedMetricsOnClick(anchor_element)
          .value();
  EXPECT_LT(0, feature2.GetRatioVisibleArea());
  EXPECT_FLOAT_EQ(0.5, feature2.GetRatioDistanceTopToVisibleTop());
  EXPECT_LT(0.5, feature2.GetRatioDistanceCenterToVisibleTop());
  EXPECT_FLOAT_EQ(2, feature2.GetRatioDistanceRootTop());
  EXPECT_FLOAT_EQ(10, feature2.GetRatioDistanceRootBottom());
}

// The main frame contains an iframe. The iframe contains an anchor element.
// Features of the element are extracted.
// Then the test scrolls down in the main frame to check features again.
// Then the test scrolls down in the iframe to check features again.
TEST_F(AnchorElementMetricsTest, AnchorFeatureInIframe) {
  SimRequest main_resource("https://example.com/page1", "text/html");
  SimRequest iframe_resource("https://example.com/iframe.html", "text/html");
  SimRequest image_resource("https://example.com/cat.png", "image/png");

  LoadURL("https://example.com/page1");

  main_resource.Complete(String::Format(
      R"HTML(
        <body style='margin: 0px'>
        <div style='height: %dpx;'></div>
        <iframe id='iframe' src='https://example.com/iframe.html'
            style='width: 300px; height: %dpx;
            border-style: none; padding: 0px; margin: 0px;'></iframe>
        <div style='height: %dpx;'></div>
        </body>)HTML",
      2 * kViewportHeight, kViewportHeight / 2, 10 * kViewportHeight));

  iframe_resource.Complete(String::Format(
      R"HTML(
    <body style='margin: 0px'>
    <div style='height: %dpx;'></div>
    <a id='anchor' href="https://example.com/page2">example</a>
    <div style='height: %dpx;'></div>
    </body>)HTML",
      kViewportHeight / 2, 5 * kViewportHeight));

  Element* iframe = GetDocument().getElementById("iframe");
  HTMLIFrameElement* iframe_element = ToHTMLIFrameElement(iframe);
  Frame* sub = iframe_element->ContentFrame();
  LocalFrame* subframe = ToLocalFrame(sub);

  Element* anchor = subframe->GetDocument()->getElementById("anchor");
  HTMLAnchorElement* anchor_element = ToHTMLAnchorElement(anchor);

  auto feature =
      AnchorElementMetrics::MaybeReportClickedMetricsOnClick(anchor_element)
          .value();
  EXPECT_LT(0, feature.GetRatioArea());
  EXPECT_FLOAT_EQ(0, feature.GetRatioVisibleArea());
  EXPECT_FLOAT_EQ(2.5, feature.GetRatioDistanceTopToVisibleTop());
  EXPECT_LT(2.5, feature.GetRatioDistanceCenterToVisibleTop());
  EXPECT_FLOAT_EQ(2.5, feature.GetRatioDistanceRootTop());
  EXPECT_TRUE(feature.GetIsInIframe());
  EXPECT_FALSE(feature.GetContainsImage());
  EXPECT_TRUE(feature.GetIsSameHost());
  EXPECT_TRUE(feature.GetIsUrlIncrementedByOne());

  // Scroll down the main frame.
  GetDocument().View()->LayoutViewport()->SetScrollOffset(
      ScrollOffset(0, kViewportHeight * 1.8), kProgrammaticScroll);

  auto feature2 =
      AnchorElementMetrics::MaybeReportClickedMetricsOnClick(anchor_element)
          .value();
  EXPECT_LT(0, feature2.GetRatioVisibleArea());
  EXPECT_FLOAT_EQ(0.7, feature2.GetRatioDistanceTopToVisibleTop());
  EXPECT_FLOAT_EQ(2.5, feature2.GetRatioDistanceRootTop());

  // Scroll down inside iframe. Now the anchor element is visible.
  subframe->View()->LayoutViewport()->SetScrollOffset(
      ScrollOffset(0, kViewportHeight * 0.2), kProgrammaticScroll);

  auto feature3 =
      AnchorElementMetrics::MaybeReportClickedMetricsOnClick(anchor_element)
          .value();
  EXPECT_LT(0, feature3.GetRatioVisibleArea());
  EXPECT_FLOAT_EQ(0.5, feature3.GetRatioDistanceTopToVisibleTop());
  EXPECT_FLOAT_EQ(2.5, feature3.GetRatioDistanceRootTop());
  // The distance is expected to be 10.2 - height of the anchor element.
  EXPECT_GT(10.2, feature3.GetRatioDistanceRootBottom());
}

}  // namespace blink
