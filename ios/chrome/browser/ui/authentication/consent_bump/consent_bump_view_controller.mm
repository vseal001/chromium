// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/chrome/browser/ui/authentication/consent_bump/consent_bump_view_controller.h"

#import "ios/chrome/browser/ui/authentication/consent_bump/consent_bump_view_controller_delegate.h"
#include "ios/chrome/browser/ui/uikit_ui_util.h"
#import "ios/chrome/common/ui_util/constraints_ui_util.h"
#include "ios/chrome/grit/ios_strings.h"
#include "ui/base/l10n/l10n_util_mac.h"

#if !defined(__has_feature) || !__has_feature(objc_arc)
#error "This file requires ARC support."
#endif

namespace {
const CGFloat kMargin = 16;
const CGFloat kGradientHeight = 40;
const int kButtonBackgroundColor = 0x1A73E8;
const CGFloat kButtonTitleGreyShade = 0.98;
const CGFloat kButtonCornerRadius = 8;
const CGFloat kButtonPadding = 16;
const CGFloat kButtonVerticalPadding = 8;
const CGFloat kButtonMinimalWidth = 80;
const CGFloat kButtonTitleChangeAnimationDuration = 0.15;
}  // namespace

@interface ConsentBumpViewController ()

@property(nonatomic, strong) UIView* buttonContainer;
// Primary button. Hidden by default.
@property(nonatomic, strong) UIButton* primaryButton;
// Secondary button.
@property(nonatomic, strong) UIButton* secondaryButton;
// More button. Used to scroll the content to the bottom. Only displayed while
// the primary button is hidden.
@property(nonatomic, strong) UIButton* moreButton;
// Constraint between the more button and the secondary button.
@property(nonatomic, strong)
    NSLayoutConstraint* secondaryMoreButtonMarginConstraint;
// Gradient used to show that there is content that can be scrolled.
@property(nonatomic, strong) UIView* gradientView;
@property(nonatomic, strong) CAGradientLayer* gradientLayer;

@end

@implementation ConsentBumpViewController

@synthesize delegate = _delegate;
@synthesize contentViewController = _contentViewController;
@synthesize buttonContainer = _buttonContainer;
@synthesize primaryButton = _primaryButton;
@synthesize secondaryButton = _secondaryButton;
@synthesize moreButton = _moreButton;
@synthesize secondaryMoreButtonMarginConstraint =
    _secondaryMoreButtonMarginConstraint;
@synthesize gradientView = _gradientView;
@synthesize gradientLayer = _gradientLayer;

#pragma mark - Public

- (void)setContentViewController:(UIViewController*)contentViewController {
  if (_contentViewController == contentViewController)
    return;

  // Remove previous VC.
  [_contentViewController willMoveToParentViewController:nil];
  [_contentViewController.view removeFromSuperview];
  [_contentViewController removeFromParentViewController];

  _contentViewController = contentViewController;

  if (!contentViewController)
    return;

  [self addChildViewController:contentViewController];

  contentViewController.view.translatesAutoresizingMaskIntoConstraints = NO;
  [self.view insertSubview:contentViewController.view
              belowSubview:self.gradientView];
  AddSameConstraintsToSides(
      self.view, contentViewController.view,
      LayoutSides::kTop | LayoutSides::kLeading | LayoutSides::kTrailing);
  [contentViewController.view.bottomAnchor
      constraintEqualToAnchor:self.buttonContainer.topAnchor]
      .active = YES;

  [contentViewController didMoveToParentViewController:self];
}

#pragma mark - Property

- (UIButton*)primaryButton {
  if (!_primaryButton) {
    _primaryButton = [UIButton buttonWithType:UIButtonTypeSystem];
    _primaryButton.translatesAutoresizingMaskIntoConstraints = NO;
    _primaryButton.backgroundColor = UIColorFromRGB(kButtonBackgroundColor);
    [_primaryButton
        setTitleColor:[UIColor colorWithWhite:kButtonTitleGreyShade alpha:1]
             forState:UIControlStateNormal];
    _primaryButton.layer.cornerRadius = kButtonCornerRadius;
    _primaryButton.contentEdgeInsets =
        UIEdgeInsetsMake(kButtonVerticalPadding, kButtonPadding,
                         kButtonVerticalPadding, kButtonPadding);
    [_primaryButton setContentHuggingPriority:UILayoutPriorityDefaultHigh
                                      forAxis:UILayoutConstraintAxisVertical];
    [_primaryButton addTarget:self
                       action:@selector(primaryButtonCallback)
             forControlEvents:UIControlEventTouchUpInside];
    _primaryButton.hidden = YES;
  }
  return _primaryButton;
}

- (UIButton*)secondaryButton {
  if (!_secondaryButton) {
    _secondaryButton = [UIButton buttonWithType:UIButtonTypeSystem];
    _secondaryButton.translatesAutoresizingMaskIntoConstraints = NO;
    _secondaryButton.contentEdgeInsets =
        UIEdgeInsetsMake(kButtonVerticalPadding, kButtonPadding,
                         kButtonVerticalPadding, kButtonPadding);
    [_secondaryButton setContentHuggingPriority:UILayoutPriorityDefaultHigh
                                        forAxis:UILayoutConstraintAxisVertical];
    [_secondaryButton addTarget:self
                         action:@selector(secondaryButtonCallback)
               forControlEvents:UIControlEventTouchUpInside];
  }
  return _secondaryButton;
}

- (UIButton*)moreButton {
  if (!_moreButton) {
    _moreButton = [UIButton buttonWithType:UIButtonTypeSystem];
    _moreButton.translatesAutoresizingMaskIntoConstraints = NO;
    _moreButton.contentEdgeInsets =
        UIEdgeInsetsMake(kButtonVerticalPadding, kButtonPadding,
                         kButtonVerticalPadding, kButtonPadding);
    [_moreButton setContentHuggingPriority:UILayoutPriorityDefaultHigh
                                   forAxis:UILayoutConstraintAxisVertical];
    [_moreButton addTarget:self
                    action:@selector(moreButtonCallback)
          forControlEvents:UIControlEventTouchUpInside];
    [_moreButton
        setTitle:l10n_util::GetNSString(
                     IDS_IOS_ACCOUNT_CONSISTENCY_CONFIRMATION_SCROLL_BUTTON)
        forState:UIControlStateNormal];
    [_moreButton setImage:[UIImage imageNamed:@"signin_confirmation_more"]
                 forState:UIControlStateNormal];
  }
  return _moreButton;
}

- (UIView*)buttonContainer {
  if (!_buttonContainer) {
    _buttonContainer = [[UIView alloc] init];
    _buttonContainer.translatesAutoresizingMaskIntoConstraints = NO;
  }
  return _buttonContainer;
}

- (UIView*)gradientView {
  if (!_gradientView) {
    _gradientView = [[UIView alloc] initWithFrame:CGRectZero];
    _gradientView.userInteractionEnabled = NO;
    _gradientView.translatesAutoresizingMaskIntoConstraints = NO;
    [_gradientView.layer insertSublayer:self.gradientLayer atIndex:0];
  }
  return _gradientView;
}

- (CAGradientLayer*)gradientLayer {
  if (!_gradientLayer) {
    _gradientLayer = [CAGradientLayer layer];
    _gradientLayer.colors = @[
      (id)[[UIColor colorWithWhite:1 alpha:0] CGColor],
      (id)[self.view.backgroundColor CGColor]
    ];
  }
  return _gradientLayer;
}

#pragma mark - UIViewController

- (void)viewDidLayoutSubviews {
  [super viewDidLayoutSubviews];
  self.gradientLayer.frame = self.gradientView.bounds;
}

- (void)viewDidLoad {
  self.view.backgroundColor = [UIColor whiteColor];

  // Add subviews.
  [self.buttonContainer addSubview:self.primaryButton];
  [self.buttonContainer addSubview:self.moreButton];
  [self.buttonContainer addSubview:self.secondaryButton];
  [self.view addSubview:self.buttonContainer];
  [self.view addSubview:self.gradientView];

  // Constraints.
  self.secondaryMoreButtonMarginConstraint = [self.moreButton.leadingAnchor
      constraintGreaterThanOrEqualToAnchor:self.secondaryButton.trailingAnchor
                                  constant:kMargin];
  id<LayoutGuideProvider> safeArea = SafeAreaLayoutGuideForView(self.view);
  AddSameConstraintsToSides(self.view, self.gradientView,
                            LayoutSides::kLeading | LayoutSides::kTrailing);
  AddSameConstraintsToSides(
      safeArea, self.buttonContainer,
      LayoutSides::kBottom | LayoutSides::kLeading | LayoutSides::kTrailing);
  AddSameConstraintsToSidesWithInsets(
      self.secondaryButton, self.buttonContainer,
      LayoutSides::kLeading | LayoutSides::kTop | LayoutSides::kBottom,
      ChromeDirectionalEdgeInsetsMake(kMargin, kMargin, kMargin, 0));
  AddSameConstraintsToSidesWithInsets(
      self.primaryButton, self.buttonContainer,
      LayoutSides::kTrailing | LayoutSides::kTop | LayoutSides::kBottom,
      ChromeDirectionalEdgeInsetsMake(kMargin, 0, kMargin, kMargin));
  AddSameConstraintsToSidesWithInsets(
      self.moreButton, self.buttonContainer,
      LayoutSides::kTrailing | LayoutSides::kTop | LayoutSides::kBottom,
      ChromeDirectionalEdgeInsetsMake(kMargin, 0, kMargin, kMargin));
  [NSLayoutConstraint activateConstraints:@[
    self.secondaryMoreButtonMarginConstraint,
    [self.gradientView.heightAnchor constraintEqualToConstant:kGradientHeight],
    [self.gradientView.bottomAnchor
        constraintEqualToAnchor:self.buttonContainer.topAnchor],
    [self.primaryButton.leadingAnchor
        constraintGreaterThanOrEqualToAnchor:self.secondaryButton.trailingAnchor
                                    constant:kMargin],

    // Add minimum width to the buttons to have a better looking animation when
    // changing the label.
    [self.primaryButton.widthAnchor
        constraintGreaterThanOrEqualToConstant:kButtonMinimalWidth],
    [self.secondaryButton.widthAnchor
        constraintGreaterThanOrEqualToConstant:kButtonMinimalWidth],
  ]];
}

#pragma mark - ConsentBumpConsumer

- (void)setPrimaryButtonTitle:(NSString*)secondaryButtonTitle {
  // Use CrossDisolve to avoid issue where the button is changing size before
  // changing its label, leading to an inconsistent animation.
  [UIView transitionWithView:self.primaryButton
                    duration:kButtonTitleChangeAnimationDuration
                     options:UIViewAnimationOptionTransitionCrossDissolve
                  animations:^{
                    [self.primaryButton setTitle:secondaryButtonTitle
                                        forState:UIControlStateNormal];
                  }
                  completion:nil];
}

- (void)setSecondaryButtonTitle:(NSString*)secondaryButtonTitle {
  // Use CrossDisolve to avoid issue where the button is changing size before
  // changing its label, leading to an inconsistent animation.
  [UIView transitionWithView:self.primaryButton
                    duration:kButtonTitleChangeAnimationDuration
                     options:UIViewAnimationOptionTransitionCrossDissolve
                  animations:^{
                    [self.secondaryButton setTitle:secondaryButtonTitle
                                          forState:UIControlStateNormal];
                  }
                  completion:nil];
}

- (void)showPrimaryButton {
  self.moreButton.hidden = YES;
  self.primaryButton.hidden = NO;
  self.secondaryMoreButtonMarginConstraint.active = NO;
}

#pragma mark - Private

- (void)primaryButtonCallback {
  [self.delegate consentBumpViewControllerDidTapPrimaryButton:self];
}

- (void)secondaryButtonCallback {
  [self.delegate consentBumpViewControllerDidTapSecondaryButton:self];
}

- (void)moreButtonCallback {
  [self.delegate consentBumpViewControllerDidTapMoreButton:self];
}

@end
