// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/chrome/browser/ui/settings/google_services_settings_mediator.h"

#include "base/auto_reset.h"
#include "base/mac/foundation_util.h"
#include "components/browser_sync/profile_sync_service.h"
#import "components/prefs/ios/pref_observer_bridge.h"
#include "components/prefs/pref_change_registrar.h"
#include "components/prefs/pref_service.h"
#include "components/unified_consent/pref_names.h"
#import "ios/chrome/browser/signin/authentication_service.h"
#import "ios/chrome/browser/signin/authentication_service_factory.h"
#include "ios/chrome/browser/sync/sync_observer_bridge.h"
#include "ios/chrome/browser/sync/sync_setup_service.h"
#import "ios/chrome/browser/ui/collection_view/cells/collection_view_item.h"
#import "ios/chrome/browser/ui/collection_view/cells/collection_view_text_item.h"
#import "ios/chrome/browser/ui/settings/cells/settings_collapsible_item.h"
#import "ios/chrome/browser/ui/settings/cells/sync_switch_item.h"
#include "ios/chrome/grit/ios_chromium_strings.h"
#include "ios/chrome/grit/ios_strings.h"
#import "ios/third_party/material_components_ios/src/components/Palettes/src/MaterialPalettes.h"
#include "ui/base/l10n/l10n_util.h"

#if !defined(__has_feature) || !__has_feature(objc_arc)
#error "This file requires ARC support."
#endif

using l10n_util::GetNSString;
using unified_consent::prefs::kUnifiedConsentGiven;

typedef NSArray<CollectionViewItem*>* ItemArray;

namespace {

// List of sections.
typedef NS_ENUM(NSInteger, SectionIdentifier) {
  SyncEverythingSectionIdentifier = kSectionIdentifierEnumZero,
  PersonalizedSectionIdentifier,
  NonPersonalizedSectionIdentifier,
};

// Keys for ListModel to save collapse/expanded prefences, for each section.
NSString* const kGoogleServicesSettingsPersonalizedSectionKey =
    @"GoogleServicesSettingsPersonalizedSection";
NSString* const kGoogleServicesSettingsNonPersonalizedSectionKey =
    @"GoogleServicesSettingsNonPersonalizedSection";

// List of items.
typedef NS_ENUM(NSInteger, ItemType) {
  // SyncEverythingSectionIdentifier section.
  SyncEverythingItemType = kItemTypeEnumZero,
  // PersonalizedSectionIdentifier section.
  SyncPersonalizationItemType,
  SyncBookmarksItemType,
  SyncHistoryItemType,
  SyncPasswordsItemType,
  SyncOpenTabsItemType,
  SyncAutofillItemType,
  SyncSettingsItemType,
  SyncReadingListItemType,
  SyncActivityAndInteractionsItemType,
  SyncGoogleActivityControlsItemType,
  EncryptionItemType,
  ManageSyncedDataItemType,
  // NonPersonalizedSectionIdentifier section.
  NonPersonalizedServicesItemType,
  AutocompleteSearchesAndURLsItemType,
  PreloadPagesItemType,
  ImproveChromeItemType,
  BetterSearchAndBrowsingItemType,
};

}  // namespace

@interface GoogleServicesSettingsMediator ()<PrefObserverDelegate,
                                             SyncObserverModelBridge> {
  // Bridge to listen to pref changes.
  std::unique_ptr<PrefObserverBridge> prefObserverBridge_;
  // Registrar for pref changes notifications.
  PrefChangeRegistrar prefChangeRegistrar_;
  std::unique_ptr<SyncObserverBridge> _syncObserver;
}

// Returns YES if the user is authenticated.
@property(nonatomic, assign, readonly) BOOL isAuthenticated;
// Returns YES if the user has given his consent to use Google services.
@property(nonatomic, assign, readonly) BOOL isConsentGiven;
// Preference service.
@property(nonatomic, assign, readonly) PrefService* prefService;
// Sync setup service.
@property(nonatomic, assign, readonly) SyncSetupService* syncSetupService;

// YES if the switch for |syncEverythingItem| is currently animating from one
// state to another.
@property(nonatomic, assign, readwrite) BOOL syncEverythingSwitchBeingAnimated;
// YES if at least one switch in the personalized section is currently animating
// from one state to another.
@property(nonatomic, assign, readwrite) BOOL personalizedSectionBeingAnimated;
// Item for "Sync Everything" section.
@property(nonatomic, strong, readonly) SyncSwitchItem* syncEverythingItem;
// Collapsible item for the personalized section.
@property(nonatomic, strong, readonly)
    SettingsCollapsibleItem* syncPersonalizationItem;
// All the items for the personalized section.
@property(nonatomic, strong, readonly) ItemArray personalizedItems;
// Collapsible item for the non-personalized section.
@property(nonatomic, strong, readonly)
    SettingsCollapsibleItem* nonPersonalizedServicesItem;
// All the items for the non-personalized section.
@property(nonatomic, strong, readonly) ItemArray nonPersonalizedItems;

@end

@implementation GoogleServicesSettingsMediator

@synthesize consumer = _consumer;
@synthesize authService = _authService;
@synthesize prefService = _prefService;
@synthesize syncSetupService = _syncSetupService;
@synthesize syncEverythingSwitchBeingAnimated =
    _syncEverythingSwitchBeingAnimated;
@synthesize personalizedSectionBeingAnimated =
    _personalizedSectionBeingAnimated;
@synthesize syncEverythingItem = _syncEverythingItem;
@synthesize syncPersonalizationItem = _syncPersonalizationItem;
@synthesize personalizedItems = _personalizedItems;
@synthesize nonPersonalizedServicesItem = _nonPersonalizedServicesItem;
@synthesize nonPersonalizedItems = _nonPersonalizedItems;

#pragma mark - Load model

- (instancetype)initWithPrefService:(PrefService*)prefService
                        syncService:
                            (browser_sync::ProfileSyncService*)syncService
                   syncSetupService:(SyncSetupService*)syncSetupService {
  self = [super init];
  if (self) {
    DCHECK(prefService);
    DCHECK(syncService);
    DCHECK(syncSetupService);
    _prefService = prefService;
    _syncSetupService = syncSetupService;
    _syncObserver.reset(new SyncObserverBridge(self, syncService));
    prefObserverBridge_ = std::make_unique<PrefObserverBridge>(self);
    prefChangeRegistrar_.Init(prefService);
    prefObserverBridge_->ObserveChangesForPreference(kUnifiedConsentGiven,
                                                     &prefChangeRegistrar_);
  }
  return self;
}

// Loads SyncEverythingSectionIdentifier section.
- (void)loadSyncEverythingSection {
  CollectionViewModel* model = self.consumer.collectionViewModel;
  [model addSectionWithIdentifier:SyncEverythingSectionIdentifier];
  [model addItem:self.syncEverythingItem
      toSectionWithIdentifier:SyncEverythingSectionIdentifier];
  self.syncEverythingItem.on = self.isConsentGiven;
}

// Loads PersonalizedSectionIdentifier section.
- (void)loadPersonalizedSection {
  CollectionViewModel* model = self.consumer.collectionViewModel;
  [model addSectionWithIdentifier:PersonalizedSectionIdentifier];
  [model setSectionIdentifier:PersonalizedSectionIdentifier
                 collapsedKey:kGoogleServicesSettingsPersonalizedSectionKey];
  SettingsCollapsibleItem* syncPersonalizationItem =
      self.syncPersonalizationItem;
  [model addItem:syncPersonalizationItem
      toSectionWithIdentifier:PersonalizedSectionIdentifier];
  BOOL collapsed = self.isAuthenticated ? self.isConsentGiven : YES;
  syncPersonalizationItem.collapsed = collapsed;
  [model setSection:PersonalizedSectionIdentifier collapsed:collapsed];
  for (CollectionViewItem* item in self.personalizedItems) {
    [model addItem:item toSectionWithIdentifier:PersonalizedSectionIdentifier];
  }
  [self updatePersonalizedSection];
}

// Loads NonPersonalizedSectionIdentifier section.
- (void)loadNonPersonalizedSection {
  CollectionViewModel* model = self.consumer.collectionViewModel;
  [model addSectionWithIdentifier:NonPersonalizedSectionIdentifier];
  [model setSectionIdentifier:NonPersonalizedSectionIdentifier
                 collapsedKey:kGoogleServicesSettingsNonPersonalizedSectionKey];
  SettingsCollapsibleItem* nonPersonalizedServicesItem =
      self.nonPersonalizedServicesItem;
  [model addItem:nonPersonalizedServicesItem
      toSectionWithIdentifier:NonPersonalizedSectionIdentifier];
  BOOL collapsed = self.isAuthenticated ? self.isConsentGiven : NO;
  nonPersonalizedServicesItem.collapsed = collapsed;
  [model setSection:NonPersonalizedSectionIdentifier collapsed:collapsed];
  for (CollectionViewItem* item in self.nonPersonalizedItems) {
    [model addItem:item
        toSectionWithIdentifier:NonPersonalizedSectionIdentifier];
  }
  [self updateNonPersonalizedSection];
}

#pragma mark - Properties

- (BOOL)isAuthenticated {
  return self.authService->IsAuthenticated();
}

- (BOOL)isConsentGiven {
  return self.prefService->GetBoolean(kUnifiedConsentGiven);
}

- (CollectionViewItem*)syncEverythingItem {
  if (!_syncEverythingItem) {
    _syncEverythingItem = [self
        switchItemWithItemType:SyncEverythingItemType
                  textStringID:IDS_IOS_GOOGLE_SERVICES_SETTINGS_SYNC_EVERYTHING
                detailStringID:0
                     commandID:
                         GoogleServicesSettingsCommandIDToggleSyncEverything
                      dataType:0];
  }
  return _syncEverythingItem;
}

- (SettingsCollapsibleItem*)syncPersonalizationItem {
  if (!_syncPersonalizationItem) {
    _syncPersonalizationItem = [self
        collapsibleItemWithItemType:SyncPersonalizationItemType
                       textStringID:
                           IDS_IOS_GOOGLE_SERVICES_SETTINGS_SYNC_PERSONALIZATION_TEXT
                     detailStringID:
                         IDS_IOS_GOOGLE_SERVICES_SETTINGS_SYNC_PERSONALIZATION_DETAIL];
  }
  return _syncPersonalizationItem;
}

- (ItemArray)personalizedItems {
  if (!_personalizedItems) {
    SyncSwitchItem* syncBookmarksItem = [self
        switchItemWithItemType:SyncBookmarksItemType
                  textStringID:IDS_IOS_GOOGLE_SERVICES_SETTINGS_BOOKMARKS_TEXT
                detailStringID:0
                     commandID:GoogleServicesSettingsCommandIDToggleDataTypeSync
                      dataType:SyncSetupService::kSyncBookmarks];
    SyncSwitchItem* syncHistoryItem = [self
        switchItemWithItemType:SyncHistoryItemType
                  textStringID:IDS_IOS_GOOGLE_SERVICES_SETTINGS_HISTORY_TEXT
                detailStringID:0
                     commandID:GoogleServicesSettingsCommandIDToggleDataTypeSync
                      dataType:SyncSetupService::kSyncOmniboxHistory];
    SyncSwitchItem* syncPasswordsItem = [self
        switchItemWithItemType:SyncPasswordsItemType
                  textStringID:IDS_IOS_GOOGLE_SERVICES_SETTINGS_PASSWORD_TEXT
                detailStringID:0
                     commandID:GoogleServicesSettingsCommandIDToggleDataTypeSync
                      dataType:SyncSetupService::kSyncPasswords];
    SyncSwitchItem* syncOpenTabsItem = [self
        switchItemWithItemType:SyncOpenTabsItemType
                  textStringID:IDS_IOS_GOOGLE_SERVICES_SETTINGS_OPENTABS_TEXT
                detailStringID:0
                     commandID:GoogleServicesSettingsCommandIDToggleDataTypeSync
                      dataType:SyncSetupService::kSyncOpenTabs];
    SyncSwitchItem* syncAutofillItem = [self
        switchItemWithItemType:SyncAutofillItemType
                  textStringID:IDS_IOS_GOOGLE_SERVICES_SETTINGS_AUTOFILL_TEXT
                detailStringID:0
                     commandID:GoogleServicesSettingsCommandIDToggleDataTypeSync
                      dataType:SyncSetupService::kSyncAutofill];
    SyncSwitchItem* syncReadingListItem = [self
        switchItemWithItemType:SyncReadingListItemType
                  textStringID:
                      IDS_IOS_GOOGLE_SERVICES_SETTINGS_READING_LIST_TEXT
                detailStringID:0
                     commandID:GoogleServicesSettingsCommandIDToggleDataTypeSync
                      dataType:SyncSetupService::kSyncReadingList];
    SyncSwitchItem* syncActivityAndInteractionsItem = [self
        switchItemWithItemType:SyncActivityAndInteractionsItemType
                  textStringID:
                      IDS_IOS_GOOGLE_SERVICES_SETTINGS_ACTIVITY_AND_INTERACTIONS_TEXT
                detailStringID:
                    IDS_IOS_GOOGLE_SERVICES_SETTINGS_ACTIVITY_AND_INTERACTIONS_DETAIL
                     commandID:GoogleServicesSettingsCommandIDToggleDataTypeSync
                      dataType:SyncSetupService::kSyncUserEvent];
    CollectionViewTextItem* syncGoogleActivityControlsItem = [self
        textItemWithItemType:SyncGoogleActivityControlsItemType
                textStringID:
                    IDS_IOS_GOOGLE_SERVICES_SETTINGS_GOOGLE_ACTIVITY_CONTROL_TEXT
              detailStringID:
                  IDS_IOS_GOOGLE_SERVICES_SETTINGS_GOOGLE_ACTIVITY_CONTROL_DETAIL
               accessoryType:MDCCollectionViewCellAccessoryDisclosureIndicator
                   commandID:
                       GoogleServicesSettingsCommandIDOpenGoogleActivityPage];
    CollectionViewTextItem* encryptionItem = [self
        textItemWithItemType:EncryptionItemType
                textStringID:IDS_IOS_GOOGLE_SERVICES_SETTINGS_ENCRYPTION_TEXT
              detailStringID:0
               accessoryType:MDCCollectionViewCellAccessoryDisclosureIndicator
                   commandID:
                       GoogleServicesSettingsCommandIDOpenEncryptionDialog];
    CollectionViewTextItem* manageSyncedDataItem = [self
        textItemWithItemType:ManageSyncedDataItemType
                textStringID:
                    IDS_IOS_GOOGLE_SERVICES_SETTINGS_MANAGED_SYNC_DATA_TEXT
              detailStringID:0
               accessoryType:MDCCollectionViewCellAccessoryNone
                   commandID:
                       GoogleServicesSettingsCommandIDOpenManageSyncedDataPage];
    _personalizedItems = @[
      syncBookmarksItem, syncHistoryItem, syncPasswordsItem, syncOpenTabsItem,
      syncAutofillItem, syncReadingListItem, syncActivityAndInteractionsItem,
      syncGoogleActivityControlsItem, encryptionItem, manageSyncedDataItem
    ];
  }
  return _personalizedItems;
}

- (SettingsCollapsibleItem*)nonPersonalizedServicesItem {
  if (!_nonPersonalizedServicesItem) {
    _nonPersonalizedServicesItem = [self
        collapsibleItemWithItemType:NonPersonalizedServicesItemType
                       textStringID:
                           IDS_IOS_GOOGLE_SERVICES_SETTINGS_NON_PERSONALIZED_SERVICES_TEXT
                     detailStringID:
                         IDS_IOS_GOOGLE_SERVICES_SETTINGS_NON_PERSONALIZED_SERVICES_DETAIL];
  }
  return _nonPersonalizedServicesItem;
}

- (ItemArray)nonPersonalizedItems {
  if (!_nonPersonalizedItems) {
    SyncSwitchItem* autocompleteSearchesAndURLsItem = [self
        switchItemWithItemType:AutocompleteSearchesAndURLsItemType
                  textStringID:
                      IDS_IOS_GOOGLE_SERVICES_SETTINGS_AUTOCOMPLETE_SEARCHES_AND_URLS_TEXT
                detailStringID:
                    IDS_IOS_GOOGLE_SERVICES_SETTINGS_AUTOCOMPLETE_SEARCHES_AND_URLS_DETAIL
                     commandID:
                         GoogleServicesSettingsCommandIDToggleAutocompleteSearchesService
                      dataType:0];
    SyncSwitchItem* preloadPagesItem = [self
        switchItemWithItemType:PreloadPagesItemType
                  textStringID:
                      IDS_IOS_GOOGLE_SERVICES_SETTINGS_PRELOAD_PAGES_TEXT
                detailStringID:
                    IDS_IOS_GOOGLE_SERVICES_SETTINGS_PRELOAD_PAGES_DETAIL
                     commandID:
                         GoogleServicesSettingsCommandIDTogglePreloadPagesService
                      dataType:0];
    SyncSwitchItem* improveChromeItem = [self
        switchItemWithItemType:ImproveChromeItemType
                  textStringID:
                      IDS_IOS_GOOGLE_SERVICES_SETTINGS_IMPROVE_CHROME_TEXT
                detailStringID:
                    IDS_IOS_GOOGLE_SERVICES_SETTINGS_IMPROVE_CHROME_DETAIL
                     commandID:
                         GoogleServicesSettingsCommandIDToggleImproveChromeService
                      dataType:0];
    SyncSwitchItem* betterSearchAndBrowsingItemType = [self
        switchItemWithItemType:BetterSearchAndBrowsingItemType
                  textStringID:
                      IDS_IOS_GOOGLE_SERVICES_SETTINGS_BETTER_SEARCH_AND_BROWSING_TEXT
                detailStringID:
                    IDS_IOS_GOOGLE_SERVICES_SETTINGS_BETTER_SEARCH_AND_BROWSING_DETAIL
                     commandID:
                         GoogleServicesSettingsCommandIDToggleBetterSearchAndBrowsingService
                      dataType:0];
    _nonPersonalizedItems = @[
      autocompleteSearchesAndURLsItem, preloadPagesItem, improveChromeItem,
      betterSearchAndBrowsingItemType
    ];
  }
  return _nonPersonalizedItems;
}

#pragma mark - Private

// Creates a SettingsCollapsibleItem instance.
- (SettingsCollapsibleItem*)collapsibleItemWithItemType:(NSInteger)itemType
                                           textStringID:(int)textStringID
                                         detailStringID:(int)detailStringID {
  SettingsCollapsibleItem* collapsibleItem =
      [[SettingsCollapsibleItem alloc] initWithType:itemType];
  collapsibleItem.text = GetNSString(textStringID);
  collapsibleItem.numberOfTextLines = 0;
  collapsibleItem.detailText = GetNSString(detailStringID);
  collapsibleItem.numberOfDetailTextLines = 0;
  return collapsibleItem;
}

// Creates a SyncSwitchItem instance.
- (SyncSwitchItem*)switchItemWithItemType:(NSInteger)itemType
                             textStringID:(int)textStringID
                           detailStringID:(int)detailStringID
                                commandID:(NSInteger)commandID
                                 dataType:(NSInteger)dataType {
  SyncSwitchItem* switchItem = [[SyncSwitchItem alloc] initWithType:itemType];
  switchItem.text = GetNSString(textStringID);
  if (detailStringID)
    switchItem.detailText = GetNSString(detailStringID);
  switchItem.commandID = commandID;
  switchItem.dataType = dataType;
  return switchItem;
}

// Creates a CollectionViewTextItem instance.
- (CollectionViewTextItem*)
textItemWithItemType:(NSInteger)itemType
        textStringID:(int)textStringID
      detailStringID:(int)detailStringID
       accessoryType:(MDCCollectionViewCellAccessoryType)accessoryType
           commandID:(NSInteger)commandID {
  CollectionViewTextItem* textItem =
      [[CollectionViewTextItem alloc] initWithType:itemType];
  textItem.text = GetNSString(textStringID);
  textItem.accessoryType = accessoryType;
  if (detailStringID)
    textItem.detailText = GetNSString(detailStringID);
  textItem.commandID = commandID;
  return textItem;
}

// Updates the personalized section according to the user consent.
- (void)updatePersonalizedSection {
  BOOL enabled = self.isAuthenticated && !self.isConsentGiven;
  [self updateSectionWithCollapsibleItem:self.syncPersonalizationItem
                                   items:self.personalizedItems
                                 enabled:enabled];
}

// Updates |item.on| and |item.enabled| according to its data type.
- (void)updateSwitchValueWithItem:(SyncSwitchItem*)item enabled:(BOOL)enabled {
  SyncSetupService::SyncableDatatype dataType =
      static_cast<SyncSetupService::SyncableDatatype>(item.dataType);
  syncer::ModelType modelType = self.syncSetupService->GetModelType(dataType);
  item.on = self.syncSetupService->IsDataTypePreferred(modelType);
  item.enabled = enabled;
}

// Updates the non-personalized section according to the user consent.
- (void)updateNonPersonalizedSection {
  BOOL enabled = !self.isAuthenticated || !self.isConsentGiven;
  [self updateSectionWithCollapsibleItem:self.nonPersonalizedServicesItem
                                   items:self.nonPersonalizedItems
                                 enabled:enabled];
}

// Set a section (collapsible item, with all the items inside) to be enabled
// or disabled.
- (void)updateSectionWithCollapsibleItem:
            (SettingsCollapsibleItem*)collapsibleItem
                                   items:(ItemArray)items
                                 enabled:(BOOL)enabled {
  UIColor* textColor = enabled ? nil : [[MDCPalette greyPalette] tint500];
  collapsibleItem.textColor = textColor;
  for (CollectionViewItem* item in items) {
    if ([item isKindOfClass:[SyncSwitchItem class]]) {
      SyncSwitchItem* switchItem = base::mac::ObjCCast<SyncSwitchItem>(item);
      if (switchItem.commandID ==
          GoogleServicesSettingsCommandIDToggleDataTypeSync) {
        SyncSetupService::SyncableDatatype dataType =
            static_cast<SyncSetupService::SyncableDatatype>(
                switchItem.dataType);
        syncer::ModelType modelType =
            self.syncSetupService->GetModelType(dataType);
        switchItem.on = self.syncSetupService->IsDataTypePreferred(modelType);
      }
      switchItem.enabled = enabled;
    } else if ([item isKindOfClass:[CollectionViewTextItem class]]) {
      CollectionViewTextItem* textItem =
          base::mac::ObjCCast<CollectionViewTextItem>(item);
      textItem.textColor = textColor;
    } else {
      NOTREACHED();
    }
  }
}

#pragma mark - GoogleServicesSettingsViewControllerModelDelegate

- (void)googleServicesSettingsViewControllerLoadModel:
    (GoogleServicesSettingsViewController*)controller {
  DCHECK_EQ(self.consumer, controller);
  self.consumer.collectionViewModel.collapsableMode =
      ListModelCollapsableModeFirstCell;
  if (self.isAuthenticated)
    [self loadSyncEverythingSection];
  [self loadPersonalizedSection];
  [self loadNonPersonalizedSection];
}

#pragma mark - GoogleServicesSettingsCommandHandler

- (void)toggleSyncEverythingWithValue:(BOOL)value {
  if (value == self.isConsentGiven)
    return;
  // Mark the switch has being animated to avoid being reloaded.
  base::AutoReset<BOOL> autoReset(&_syncEverythingSwitchBeingAnimated, YES);
  self.prefService->SetBoolean(kUnifiedConsentGiven, value);
}

- (void)toggleSyncDataSync:(NSInteger)dataTypeInt WithValue:(BOOL)on {
  base::AutoReset<BOOL> autoReset(&_personalizedSectionBeingAnimated, YES);
  SyncSetupService::SyncableDatatype dataType =
      static_cast<SyncSetupService::SyncableDatatype>(dataTypeInt);
  syncer::ModelType modelType = self.syncSetupService->GetModelType(dataType);
  self.syncSetupService->SetDataTypeEnabled(modelType, on);
}

- (void)toggleAutocompleteSearchesServiceWithValue:(BOOL)on {
  // Needs to be implemented.
}

- (void)togglePreloadPagesServiceWithValue:(BOOL)on {
  // Needs to be implemented.
}

- (void)toggleImproveChromeServiceWithValue:(BOOL)on {
  // Needs to be implemented.
}

- (void)toggleBetterSearchAndBrowsingServiceWithValue:(BOOL)on {
  // Needs to be implemented.
}

- (void)openGoogleActivityPage {
  // Needs to be implemented.
}

- (void)openEncryptionDialog {
  // Needs to be implemented.
}

- (void)openManageSyncedDataPage {
  // Needs to be implemented.
}

#pragma mark - PrefObserverDelegate

- (void)onPreferenceChanged:(const std::string&)preferenceName {
  DCHECK_EQ(kUnifiedConsentGiven, preferenceName);
  self.syncEverythingItem.on = self.isConsentGiven;
  [self updatePersonalizedSection];
  [self updateNonPersonalizedSection];
  CollectionViewModel* model = self.consumer.collectionViewModel;
  if (!self.isConsentGiven) {
    // If the consent is removed, both collapsible sections should be expanded.
    [model setSection:PersonalizedSectionIdentifier collapsed:NO];
    [self syncPersonalizationItem].collapsed = NO;
    [model setSection:NonPersonalizedSectionIdentifier collapsed:NO];
    [self nonPersonalizedServicesItem].collapsed = NO;
  }
  // Reload sections.
  NSMutableIndexSet* sectionIndexToReload = [NSMutableIndexSet indexSet];
  if (!self.syncEverythingSwitchBeingAnimated) {
    // The sync everything section can be reloaded only if the switch for
    // syncEverythingItem is not currently animated. Otherwise the animation
    // would be stopped before the end.
    [sectionIndexToReload addIndex:[model sectionForSectionIdentifier:
                                              SyncEverythingSectionIdentifier]];
  }
  if (!self.personalizedSectionBeingAnimated) {
    // The sync everything section can be reloaded only if none of the switches
    // in the personalized section are not currently animated. Otherwise the
    // animation would be stopped before the end.
    [sectionIndexToReload addIndex:[model sectionForSectionIdentifier:
                                              PersonalizedSectionIdentifier]];
  }
  [sectionIndexToReload addIndex:[model sectionForSectionIdentifier:
                                            NonPersonalizedSectionIdentifier]];
  [self.consumer reloadSections:sectionIndexToReload];
}

#pragma mark - SyncObserverModelBridge

- (void)onSyncStateChanged {
  [self updatePersonalizedSection];
  if (!self.personalizedSectionBeingAnimated) {
    NSMutableIndexSet* sectionIndexToReload = [NSMutableIndexSet indexSet];
    [sectionIndexToReload
        addIndex:PersonalizedSectionIdentifier - kSectionIdentifierEnumZero];
    [self.consumer reloadSections:sectionIndexToReload];
  }
}

@end
