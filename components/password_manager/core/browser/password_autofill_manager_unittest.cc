// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/password_manager/core/browser/password_autofill_manager.h"

#include <memory>

#include "base/command_line.h"
#include "base/compiler_specific.h"
#include "base/message_loop/message_loop.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/metrics/histogram_tester.h"
#include "base/test/metrics/user_action_tester.h"
#include "build/build_config.h"
#include "components/autofill/core/browser/popup_item_ids.h"
#include "components/autofill/core/browser/suggestion_test_helpers.h"
#include "components/autofill/core/browser/test_autofill_client.h"
#include "components/autofill/core/browser/test_autofill_driver.h"
#include "components/autofill/core/common/autofill_constants.h"
#include "components/autofill/core/common/autofill_switches.h"
#include "components/autofill/core/common/form_field_data.h"
#include "components/autofill/core/common/password_form_fill_data.h"
#include "components/password_manager/core/browser/password_manager.h"
#include "components/password_manager/core/browser/password_manager_metrics_util.h"
#include "components/password_manager/core/browser/stub_password_manager_client.h"
#include "components/password_manager/core/browser/stub_password_manager_driver.h"
#include "components/password_manager/core/common/password_manager_features.h"
#include "components/security_state/core/security_state.h"
#include "components/strings/grit/components_strings.h"
#include "components/sync/driver/fake_sync_service.h"
#include "components/ukm/test_ukm_recorder.h"
#include "services/metrics/public/cpp/ukm_builders.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/gfx/geometry/rect_f.h"

#if defined(OS_ANDROID)
#include "base/android/build_info.h"
#endif

// The name of the username/password element in the form.
const char kUsernameName[] = "username";
const char kInvalidUsername[] = "no-username";
const char kPasswordName[] = "password";

const char kAliceUsername[] = "alice";
const char kAlicePassword[] = "password";

using autofill::Suggestion;
using autofill::SuggestionVectorIdsAre;
using autofill::SuggestionVectorValuesAre;
using autofill::SuggestionVectorLabelsAre;
using testing::_;

using UkmEntry = ukm::builders::PageWithPassword;

namespace autofill {
class AutofillPopupDelegate;
}

namespace password_manager {

namespace {

constexpr char kMainFrameUrl[] = "https://example.com/";

class MockPasswordManagerDriver : public StubPasswordManagerDriver {
 public:
  MOCK_METHOD2(FillSuggestion,
               void(const base::string16&, const base::string16&));
  MOCK_METHOD2(PreviewSuggestion,
               void(const base::string16&, const base::string16&));
  MOCK_METHOD0(GetPasswordManager, PasswordManager*());
};

class TestPasswordManagerClient : public StubPasswordManagerClient {
 public:
  TestPasswordManagerClient() : main_frame_url_(kMainFrameUrl) {}
  ~TestPasswordManagerClient() override = default;

  MockPasswordManagerDriver* mock_driver() { return &driver_; }
  const GURL& GetMainFrameURL() const override { return main_frame_url_; }

  MOCK_METHOD0(GeneratePassword, void());

 private:
  MockPasswordManagerDriver driver_;
  GURL main_frame_url_;
};

class MockSyncService : public syncer::FakeSyncService {
 public:
  MockSyncService() {}
  ~MockSyncService() override {}
  MOCK_CONST_METHOD0(IsFirstSetupComplete, bool());
  MOCK_CONST_METHOD0(IsSyncActive, bool());
  MOCK_CONST_METHOD0(IsUsingSecondaryPassphrase, bool());
  MOCK_CONST_METHOD0(GetActiveDataTypes, syncer::ModelTypeSet());
};

class MockAutofillClient : public autofill::TestAutofillClient {
 public:
  MockAutofillClient() : sync_service_(nullptr) {}
  MockAutofillClient(MockSyncService* sync_service)
      : sync_service_(sync_service) {
    LOG(ERROR) << "init mpck client";
  }
  MOCK_METHOD5(ShowAutofillPopup,
               void(const gfx::RectF& element_bounds,
                    base::i18n::TextDirection text_direction,
                    const std::vector<Suggestion>& suggestions,
                    bool autoselect_first_suggestion,
                    base::WeakPtr<autofill::AutofillPopupDelegate> delegate));
  MOCK_METHOD0(HideAutofillPopup, void());
  MOCK_METHOD1(ExecuteCommand, void(int));

  syncer::SyncService* GetSyncService() override { return sync_service_; }

 private:
  MockSyncService* sync_service_;
};

bool IsPreLollipopAndroid() {
#if defined(OS_ANDROID)
  return (base::android::BuildInfo::GetInstance()->sdk_int() <
          base::android::SDK_VERSION_LOLLIPOP);
#else
  return false;
#endif
}

std::vector<base::string16> GetSuggestionList(
    std::vector<base::string16> credentials) {
  if (!IsPreLollipopAndroid()) {
    credentials.push_back(
        l10n_util::GetStringUTF16(IDS_PASSWORD_MANAGER_MANAGE_PASSWORDS));
  }
  return credentials;
}

}  // namespace

class PasswordAutofillManagerTest : public testing::Test {
 protected:
  PasswordAutofillManagerTest()
      : test_username_(base::ASCIIToUTF16(kAliceUsername)),
        test_password_(base::ASCIIToUTF16(kAlicePassword)),
        fill_data_id_(0) {}

  void SetUp() override {
    // Add a preferred login and an additional login to the FillData.
    autofill::FormFieldData username_field;
    username_field.name = base::ASCIIToUTF16(kUsernameName);
    username_field.value = test_username_;
    fill_data_.username_field = username_field;

    autofill::FormFieldData password_field;
    password_field.name = base::ASCIIToUTF16(kPasswordName);
    password_field.value = test_password_;
    fill_data_.password_field = password_field;
  }

  void InitializePasswordAutofillManager(
      TestPasswordManagerClient* client,
      autofill::AutofillClient* autofill_client) {
    password_autofill_manager_.reset(new PasswordAutofillManager(
        client->mock_driver(), autofill_client, client));
    password_autofill_manager_->OnAddPasswordFormMapping(fill_data_id_,
                                                         fill_data_);
  }

 protected:
  int fill_data_id() { return fill_data_id_; }
  autofill::PasswordFormFillData& fill_data() { return fill_data_; }

  std::unique_ptr<PasswordAutofillManager> password_autofill_manager_;

  base::string16 test_username_;
  base::string16 test_password_;

 private:
  autofill::PasswordFormFillData fill_data_;
  const int fill_data_id_;

  // The TestAutofillDriver uses a SequencedWorkerPool which expects the
  // existence of a MessageLoop.
  base::MessageLoop message_loop_;
};

TEST_F(PasswordAutofillManagerTest, FillSuggestion) {
  std::unique_ptr<TestPasswordManagerClient> client(
      new TestPasswordManagerClient);
  InitializePasswordAutofillManager(client.get(), nullptr);

  EXPECT_CALL(*client->mock_driver(),
              FillSuggestion(test_username_, test_password_));
  EXPECT_TRUE(password_autofill_manager_->FillSuggestionForTest(
      fill_data_id(), test_username_));
  testing::Mock::VerifyAndClearExpectations(client->mock_driver());

  EXPECT_CALL(*client->mock_driver(), FillSuggestion(_, _)).Times(0);
  EXPECT_FALSE(password_autofill_manager_->FillSuggestionForTest(
      fill_data_id(), base::ASCIIToUTF16(kInvalidUsername)));

  const int invalid_fill_data_id = fill_data_id() + 1;

  EXPECT_FALSE(password_autofill_manager_->FillSuggestionForTest(
      invalid_fill_data_id, test_username_));

  password_autofill_manager_->DidNavigateMainFrame();
  EXPECT_FALSE(password_autofill_manager_->FillSuggestionForTest(
      fill_data_id(), test_username_));
}

TEST_F(PasswordAutofillManagerTest, PreviewSuggestion) {
  std::unique_ptr<TestPasswordManagerClient> client(
      new TestPasswordManagerClient);
  InitializePasswordAutofillManager(client.get(), nullptr);

  EXPECT_CALL(*client->mock_driver(),
              PreviewSuggestion(test_username_, test_password_));
  EXPECT_TRUE(password_autofill_manager_->PreviewSuggestionForTest(
      fill_data_id(), test_username_));
  testing::Mock::VerifyAndClearExpectations(client->mock_driver());

  EXPECT_CALL(*client->mock_driver(), PreviewSuggestion(_, _)).Times(0);
  EXPECT_FALSE(password_autofill_manager_->PreviewSuggestionForTest(
      fill_data_id(), base::ASCIIToUTF16(kInvalidUsername)));

  const int invalid_fill_data_id = fill_data_id() + 1;

  EXPECT_FALSE(password_autofill_manager_->PreviewSuggestionForTest(
      invalid_fill_data_id, test_username_));

  password_autofill_manager_->DidNavigateMainFrame();
  EXPECT_FALSE(password_autofill_manager_->PreviewSuggestionForTest(
      fill_data_id(), test_username_));
}

// Test that the popup is marked as visible after recieving password
// suggestions.
TEST_F(PasswordAutofillManagerTest, ExternalDelegatePasswordSuggestions) {
  for (bool is_suggestion_on_password_field : {false, true}) {
    SCOPED_TRACE(testing::Message() << "is_suggestion_on_password_field = "
                                    << is_suggestion_on_password_field);
    std::unique_ptr<TestPasswordManagerClient> client(
        new TestPasswordManagerClient);
    std::unique_ptr<MockAutofillClient> autofill_client(new MockAutofillClient);
    InitializePasswordAutofillManager(client.get(), autofill_client.get());

    gfx::RectF element_bounds;
    autofill::PasswordFormFillData data;
    data.username_field.value = test_username_;
    data.password_field.value = test_password_;
    data.preferred_realm = "http://foo.com/";
    int dummy_key = 0;
    password_autofill_manager_->OnAddPasswordFormMapping(dummy_key, data);

    EXPECT_CALL(*client->mock_driver(),
                FillSuggestion(test_username_, test_password_));

    std::vector<autofill::PopupItemId> ids = {
        is_suggestion_on_password_field
            ? autofill::POPUP_ITEM_ID_PASSWORD_ENTRY
            : autofill::POPUP_ITEM_ID_USERNAME_ENTRY};
    if (!IsPreLollipopAndroid()) {
      ids.push_back(autofill::POPUP_ITEM_ID_ALL_SAVED_PASSWORDS_ENTRY);
    }
    EXPECT_CALL(
        *autofill_client,
        ShowAutofillPopup(
            _, _, SuggestionVectorIdsAre(testing::ElementsAreArray(ids)), false,
            _));

    int show_suggestion_options =
        is_suggestion_on_password_field ? autofill::IS_PASSWORD_FIELD : 0;
    password_autofill_manager_->OnShowPasswordSuggestions(
        dummy_key, base::i18n::RIGHT_TO_LEFT, base::string16(),
        show_suggestion_options, element_bounds);

    // Accepting a suggestion should trigger a call to hide the popup.
    EXPECT_CALL(*autofill_client, HideAutofillPopup());
    password_autofill_manager_->DidAcceptSuggestion(
        test_username_, is_suggestion_on_password_field
                            ? autofill::POPUP_ITEM_ID_PASSWORD_ENTRY
                            : autofill::POPUP_ITEM_ID_USERNAME_ENTRY,
        1);
  }
}

// Test that OnShowPasswordSuggestions correctly matches the given FormFieldData
// to the known PasswordFormFillData, and extracts the right suggestions.
TEST_F(PasswordAutofillManagerTest, ExtractSuggestions) {
  std::unique_ptr<TestPasswordManagerClient> client(
      new TestPasswordManagerClient);
  std::unique_ptr<MockAutofillClient> autofill_client(new MockAutofillClient);
  InitializePasswordAutofillManager(client.get(), autofill_client.get());

  gfx::RectF element_bounds;
  autofill::PasswordFormFillData data;
  data.username_field.value = test_username_;
  data.password_field.value = test_password_;
  data.preferred_realm = "http://foo.com/";

  autofill::PasswordAndRealm additional;
  additional.realm = "https://foobarrealm.org";
  base::string16 additional_username(base::ASCIIToUTF16("John Foo"));
  data.additional_logins[additional_username] = additional;

  int dummy_key = 0;
  password_autofill_manager_->OnAddPasswordFormMapping(dummy_key, data);

  // First, simulate displaying suggestions matching an empty prefix. Also
  // verify that both the values and labels are filled correctly. The 'value'
  // should be the user name; the 'label' should be the realm.
  EXPECT_CALL(
      *autofill_client,
      ShowAutofillPopup(
          element_bounds, _,
          testing::AllOf(
              SuggestionVectorValuesAre(testing::UnorderedElementsAreArray(
                  GetSuggestionList({test_username_, additional_username}))),
              SuggestionVectorLabelsAre(testing::AllOf(
                  testing::Contains(base::UTF8ToUTF16(data.preferred_realm)),
                  testing::Contains(base::UTF8ToUTF16(additional.realm))))),
          false, _));
  password_autofill_manager_->OnShowPasswordSuggestions(
      dummy_key, base::i18n::RIGHT_TO_LEFT, base::string16(), 0,
      element_bounds);

  // Now simulate displaying suggestions matching "John".
  EXPECT_CALL(
      *autofill_client,
      ShowAutofillPopup(element_bounds, _,
                        SuggestionVectorValuesAre(testing::ElementsAreArray(
                            GetSuggestionList({additional_username}))),
                        false, _));
  password_autofill_manager_->OnShowPasswordSuggestions(
      dummy_key, base::i18n::RIGHT_TO_LEFT, base::ASCIIToUTF16("John"), 0,
      element_bounds);

  // Finally, simulate displaying all suggestions, without any prefix matching.
  EXPECT_CALL(
      *autofill_client,
      ShowAutofillPopup(
          element_bounds, _,
          SuggestionVectorValuesAre(testing::ElementsAreArray(
              GetSuggestionList({test_username_, additional_username}))),
          false, _));
  password_autofill_manager_->OnShowPasswordSuggestions(
      dummy_key, base::i18n::RIGHT_TO_LEFT, base::ASCIIToUTF16("xyz"),
      autofill::SHOW_ALL, element_bounds);
}

// Verify that, for Android application credentials, the prettified realms of
// applications are displayed as the labels of suggestions on the UI (for
// matches of all levels of preferredness).
TEST_F(PasswordAutofillManagerTest, PrettifiedAndroidRealmsAreShownAsLabels) {
  std::unique_ptr<TestPasswordManagerClient> client(
      new TestPasswordManagerClient);
  std::unique_ptr<MockAutofillClient> autofill_client(new MockAutofillClient);
  InitializePasswordAutofillManager(client.get(), autofill_client.get());

  autofill::PasswordFormFillData data;
  data.username_field.value = test_username_;
  data.preferred_realm = "android://hash@com.example1.android/";

  autofill::PasswordAndRealm additional;
  additional.realm = "android://hash@com.example2.android/";
  base::string16 additional_username(base::ASCIIToUTF16("John Foo"));
  data.additional_logins[additional_username] = additional;

  const int dummy_key = 0;
  password_autofill_manager_->OnAddPasswordFormMapping(dummy_key, data);

  EXPECT_CALL(*autofill_client,
              ShowAutofillPopup(_, _,
                                SuggestionVectorLabelsAre(testing::AllOf(
                                    testing::Contains(base::ASCIIToUTF16(
                                        "android://com.example1.android/")),
                                    testing::Contains(base::ASCIIToUTF16(
                                        "android://com.example2.android/")))),
                                false, _));
  password_autofill_manager_->OnShowPasswordSuggestions(
      dummy_key, base::i18n::RIGHT_TO_LEFT, base::string16(), 0, gfx::RectF());
}

TEST_F(PasswordAutofillManagerTest, FillSuggestionPasswordField) {
  std::unique_ptr<TestPasswordManagerClient> client(
      new TestPasswordManagerClient);
  std::unique_ptr<MockAutofillClient> autofill_client(new MockAutofillClient);
  InitializePasswordAutofillManager(client.get(), autofill_client.get());

  gfx::RectF element_bounds;
  autofill::PasswordFormFillData data;
  data.username_field.value = test_username_;
  data.password_field.value = test_password_;
  data.preferred_realm = "http://foo.com/";

  autofill::PasswordAndRealm additional;
  additional.realm = "https://foobarrealm.org";
  base::string16 additional_username(base::ASCIIToUTF16("John Foo"));
  data.additional_logins[additional_username] = additional;

  autofill::UsernamesCollectionKey usernames_key;
  usernames_key.realm = "http://yetanother.net";

  int dummy_key = 0;
  password_autofill_manager_->OnAddPasswordFormMapping(dummy_key, data);

  EXPECT_CALL(
      *autofill_client,
      ShowAutofillPopup(element_bounds, _,
                        SuggestionVectorValuesAre(testing::ElementsAreArray(
                            GetSuggestionList({test_username_}))),
                        false, _));
  password_autofill_manager_->OnShowPasswordSuggestions(
      dummy_key, base::i18n::RIGHT_TO_LEFT, test_username_,
      autofill::IS_PASSWORD_FIELD, element_bounds);
}

// Verify that typing "foo" into the username field will match usernames
// "foo.bar@example.com", "bar.foo@example.com" and "example@foo.com".
TEST_F(PasswordAutofillManagerTest, DisplaySuggestionsWithMatchingTokens) {
  // Token matching is currently behind a flag.
  base::CommandLine::ForCurrentProcess()->AppendSwitch(
      autofill::switches::kEnableSuggestionsWithSubstringMatch);

  std::unique_ptr<TestPasswordManagerClient> client(
      new TestPasswordManagerClient);
  std::unique_ptr<MockAutofillClient> autofill_client(new MockAutofillClient);
  InitializePasswordAutofillManager(client.get(), autofill_client.get());

  gfx::RectF element_bounds;
  autofill::PasswordFormFillData data;
  base::string16 username = base::ASCIIToUTF16("foo.bar@example.com");
  data.username_field.value = username;
  data.password_field.value = base::ASCIIToUTF16("foobar");
  data.preferred_realm = "http://foo.com/";

  autofill::PasswordAndRealm additional;
  additional.realm = "https://foobarrealm.org";
  base::string16 additional_username(base::ASCIIToUTF16("bar.foo@example.com"));
  data.additional_logins[additional_username] = additional;

  autofill::UsernamesCollectionKey usernames_key;
  usernames_key.realm = "http://yetanother.net";

  int dummy_key = 0;
  password_autofill_manager_->OnAddPasswordFormMapping(dummy_key, data);

  EXPECT_CALL(*autofill_client,
              ShowAutofillPopup(
                  element_bounds, _,
                  SuggestionVectorValuesAre(testing::UnorderedElementsAreArray(
                      GetSuggestionList({username, additional_username}))),
                  false, _));
  password_autofill_manager_->OnShowPasswordSuggestions(
      dummy_key, base::i18n::RIGHT_TO_LEFT, base::ASCIIToUTF16("foo"), 0,
      element_bounds);
}

// Verify that typing "oo" into the username field will not match any usernames
// "foo.bar@example.com", "bar.foo@example.com" or "example@foo.com".
TEST_F(PasswordAutofillManagerTest, NoSuggestionForNonPrefixTokenMatch) {
  // Token matching is currently behind a flag.
  base::CommandLine::ForCurrentProcess()->AppendSwitch(
      autofill::switches::kEnableSuggestionsWithSubstringMatch);

  std::unique_ptr<TestPasswordManagerClient> client(
      new TestPasswordManagerClient);
  std::unique_ptr<MockAutofillClient> autofill_client(new MockAutofillClient);
  InitializePasswordAutofillManager(client.get(), autofill_client.get());

  gfx::RectF element_bounds;
  autofill::PasswordFormFillData data;
  base::string16 username = base::ASCIIToUTF16("foo.bar@example.com");
  data.username_field.value = username;
  data.password_field.value = base::ASCIIToUTF16("foobar");
  data.preferred_realm = "http://foo.com/";

  autofill::PasswordAndRealm additional;
  additional.realm = "https://foobarrealm.org";
  base::string16 additional_username(base::ASCIIToUTF16("bar.foo@example.com"));
  data.additional_logins[additional_username] = additional;

  autofill::UsernamesCollectionKey usernames_key;
  usernames_key.realm = "http://yetanother.net";

  int dummy_key = 0;
  password_autofill_manager_->OnAddPasswordFormMapping(dummy_key, data);

  EXPECT_CALL(*autofill_client, ShowAutofillPopup(_, _, _, _, _)).Times(0);

  password_autofill_manager_->OnShowPasswordSuggestions(
      dummy_key, base::i18n::RIGHT_TO_LEFT, base::ASCIIToUTF16("oo"), 0,
      element_bounds);
}

// Verify that typing "foo@exam" into the username field will match username
// "bar.foo@example.com" even if the field contents span accross multiple
// tokens.
TEST_F(PasswordAutofillManagerTest,
       MatchingContentsWithSuggestionTokenSeparator) {
  // Token matching is currently behind a flag.
  base::CommandLine::ForCurrentProcess()->AppendSwitch(
      autofill::switches::kEnableSuggestionsWithSubstringMatch);

  std::unique_ptr<TestPasswordManagerClient> client(
      new TestPasswordManagerClient);
  std::unique_ptr<MockAutofillClient> autofill_client(new MockAutofillClient);
  InitializePasswordAutofillManager(client.get(), autofill_client.get());

  gfx::RectF element_bounds;
  autofill::PasswordFormFillData data;
  base::string16 username = base::ASCIIToUTF16("foo.bar@example.com");
  data.username_field.value = username;
  data.password_field.value = base::ASCIIToUTF16("foobar");
  data.preferred_realm = "http://foo.com/";

  autofill::PasswordAndRealm additional;
  additional.realm = "https://foobarrealm.org";
  base::string16 additional_username(base::ASCIIToUTF16("bar.foo@example.com"));
  data.additional_logins[additional_username] = additional;

  autofill::UsernamesCollectionKey usernames_key;
  usernames_key.realm = "http://yetanother.net";

  int dummy_key = 0;
  password_autofill_manager_->OnAddPasswordFormMapping(dummy_key, data);

  EXPECT_CALL(
      *autofill_client,
      ShowAutofillPopup(element_bounds, _,
                        SuggestionVectorValuesAre(testing::ElementsAreArray(
                            GetSuggestionList({additional_username}))),
                        false, _));
  password_autofill_manager_->OnShowPasswordSuggestions(
      dummy_key, base::i18n::RIGHT_TO_LEFT, base::ASCIIToUTF16("foo@exam"), 0,
      element_bounds);
}

// Verify that typing "example" into the username field will match and order
// usernames "example@foo.com", "foo.bar@example.com" and "bar.foo@example.com"
// i.e. prefix matched followed by substring matched.
TEST_F(PasswordAutofillManagerTest,
       DisplaySuggestionsWithPrefixesPrecedeSubstringMatched) {
  // Token matching is currently behind a flag.
  base::CommandLine::ForCurrentProcess()->AppendSwitch(
      autofill::switches::kEnableSuggestionsWithSubstringMatch);

  std::unique_ptr<TestPasswordManagerClient> client(
      new TestPasswordManagerClient);
  std::unique_ptr<MockAutofillClient> autofill_client(new MockAutofillClient);
  InitializePasswordAutofillManager(client.get(), autofill_client.get());

  gfx::RectF element_bounds;
  autofill::PasswordFormFillData data;
  base::string16 username = base::ASCIIToUTF16("foo.bar@example.com");
  data.username_field.value = username;
  data.password_field.value = base::ASCIIToUTF16("foobar");
  data.preferred_realm = "http://foo.com/";

  autofill::PasswordAndRealm additional;
  additional.realm = "https://foobarrealm.org";
  base::string16 additional_username(base::ASCIIToUTF16("bar.foo@example.com"));
  data.additional_logins[additional_username] = additional;

  autofill::UsernamesCollectionKey usernames_key;
  usernames_key.realm = "http://yetanother.net";

  int dummy_key = 0;
  password_autofill_manager_->OnAddPasswordFormMapping(dummy_key, data);

  EXPECT_CALL(*autofill_client,
              ShowAutofillPopup(
                  element_bounds, _,
                  SuggestionVectorValuesAre(testing::ElementsAreArray(
                      GetSuggestionList({username, additional_username}))),
                  false, _));
  password_autofill_manager_->OnShowPasswordSuggestions(
      dummy_key, base::i18n::RIGHT_TO_LEFT, base::ASCIIToUTF16("foo"), false,
      element_bounds);
}

TEST_F(PasswordAutofillManagerTest, PreviewAndFillEmptyUsernameSuggestion) {
  // Initialize PasswordAutofillManager with credentials without username.
  std::unique_ptr<TestPasswordManagerClient> client(
      new TestPasswordManagerClient);
  std::unique_ptr<MockAutofillClient> autofill_client(new MockAutofillClient);
  fill_data().username_field.value.clear();
  InitializePasswordAutofillManager(client.get(), autofill_client.get());

  base::string16 no_username_string =
      l10n_util::GetStringUTF16(IDS_PASSWORD_MANAGER_EMPTY_LOGIN);

  // Simulate that the user clicks on a username field.
  EXPECT_CALL(*autofill_client, ShowAutofillPopup(_, _, _, _, _));
  gfx::RectF element_bounds;
  password_autofill_manager_->OnShowPasswordSuggestions(
      fill_data_id(), base::i18n::RIGHT_TO_LEFT, base::string16(), false,
      element_bounds);

  // Check that preview of the empty username works.
  EXPECT_CALL(*client->mock_driver(),
              PreviewSuggestion(base::string16(), test_password_));
  password_autofill_manager_->DidSelectSuggestion(no_username_string,
                                                  0 /*not used*/);
  testing::Mock::VerifyAndClearExpectations(client->mock_driver());

  // Check that fill of the empty username works.
  EXPECT_CALL(*client->mock_driver(),
              FillSuggestion(base::string16(), test_password_));
  EXPECT_CALL(*autofill_client, HideAutofillPopup());
  password_autofill_manager_->DidAcceptSuggestion(
      no_username_string, autofill::POPUP_ITEM_ID_PASSWORD_ENTRY, 1);
  testing::Mock::VerifyAndClearExpectations(client->mock_driver());
}

// Tests that the "Manage passwords" suggestion is shown along with the password
// popup.
TEST_F(PasswordAutofillManagerTest, ShowAllPasswordsOptionOnPasswordField) {
  const char kShownContextHistogram[] =
      "PasswordManager.ShowAllSavedPasswordsShownContext";
  const char kAcceptedContextHistogram[] =
      "PasswordManager.ShowAllSavedPasswordsAcceptedContext";
  base::HistogramTester histograms;
  ukm::TestAutoSetUkmRecorder test_ukm_recorder;

  auto client = std::make_unique<TestPasswordManagerClient>();
  auto autofill_client = std::make_unique<MockAutofillClient>();
  auto manager =
      std::make_unique<password_manager::PasswordManager>(client.get());
  InitializePasswordAutofillManager(client.get(), autofill_client.get());

  ON_CALL(*(client->mock_driver()), GetPasswordManager())
      .WillByDefault(testing::Return(manager.get()));

  gfx::RectF element_bounds;
  autofill::PasswordFormFillData data;
  data.username_field.value = test_username_;
  data.password_field.value = test_password_;
  data.origin = GURL("https://foo.test");

  int dummy_key = 0;
  password_autofill_manager_->OnAddPasswordFormMapping(dummy_key, data);

  EXPECT_CALL(
      *autofill_client,
      ShowAutofillPopup(element_bounds, _,
                        SuggestionVectorValuesAre(testing::ElementsAreArray(
                            GetSuggestionList({test_username_}))),
                        false, _));

  password_autofill_manager_->OnShowPasswordSuggestions(
      dummy_key, base::i18n::RIGHT_TO_LEFT, test_username_,
      autofill::IS_PASSWORD_FIELD, element_bounds);

  if (!IsPreLollipopAndroid()) {
    // Expect a sample only in the shown histogram.
    histograms.ExpectUniqueSample(
        kShownContextHistogram,
        metrics_util::SHOW_ALL_SAVED_PASSWORDS_CONTEXT_PASSWORD, 1);
    // Clicking at the "Show all passwords row" should trigger a call to open
    // the Password Manager settings page and hide the popup.
    EXPECT_CALL(
        *autofill_client,
        ExecuteCommand(autofill::POPUP_ITEM_ID_ALL_SAVED_PASSWORDS_ENTRY));
    EXPECT_CALL(*autofill_client, HideAutofillPopup());
    password_autofill_manager_->DidAcceptSuggestion(
        base::string16(), autofill::POPUP_ITEM_ID_ALL_SAVED_PASSWORDS_ENTRY, 0);
    // Expect a sample in both the shown and accepted histogram.
    histograms.ExpectUniqueSample(
        kShownContextHistogram,
        metrics_util::SHOW_ALL_SAVED_PASSWORDS_CONTEXT_PASSWORD, 1);
    histograms.ExpectUniqueSample(
        kAcceptedContextHistogram,
        metrics_util::SHOW_ALL_SAVED_PASSWORDS_CONTEXT_PASSWORD, 1);
    // Trigger UKM reporting, which happens at destruction time.
    ukm::SourceId expected_source_id = client->GetUkmSourceId();
    manager.reset();
    autofill_client.reset();
    client.reset();

    const auto& entries =
        test_ukm_recorder.GetEntriesByName(UkmEntry::kEntryName);
    EXPECT_EQ(1u, entries.size());
    for (const auto* entry : entries) {
      EXPECT_EQ(expected_source_id, entry->source_id);
      test_ukm_recorder.ExpectEntryMetric(
          entry, UkmEntry::kPageLevelUserActionName,
          static_cast<int64_t>(
              password_manager::PasswordManagerMetricsRecorder::
                  PageLevelUserAction::kShowAllPasswordsWhileSomeAreSuggested));
    }
  } else {
    EXPECT_THAT(histograms.GetAllSamples(kShownContextHistogram),
                testing::IsEmpty());
    EXPECT_THAT(histograms.GetAllSamples(kAcceptedContextHistogram),
                testing::IsEmpty());
  }
}

// Tests that the "Manage passwords" fallback shows up in non-password
// fields of login forms.
TEST_F(PasswordAutofillManagerTest, ShowAllPasswordsOptionOnNonPasswordField) {
  auto client = std::make_unique<TestPasswordManagerClient>();
  auto autofill_client = std::make_unique<MockAutofillClient>();
  InitializePasswordAutofillManager(client.get(), autofill_client.get());

  gfx::RectF element_bounds;
  autofill::PasswordFormFillData data;
  data.username_field.value = test_username_;
  data.password_field.value = test_password_;
  data.origin = GURL("https://foo.test");

  int dummy_key = 0;
  password_autofill_manager_->OnAddPasswordFormMapping(dummy_key, data);

  EXPECT_CALL(
      *autofill_client,
      ShowAutofillPopup(element_bounds, _,
                        SuggestionVectorValuesAre(testing::ElementsAreArray(
                            GetSuggestionList({test_username_}))),
                        false, _));
  password_autofill_manager_->OnShowPasswordSuggestions(
      dummy_key, base::i18n::RIGHT_TO_LEFT, test_username_, 0, element_bounds);
}

TEST_F(PasswordAutofillManagerTest,
       MaybeShowPasswordSuggestionsWithGenerationNoCredentials) {
  auto client = std::make_unique<TestPasswordManagerClient>();
  auto autofill_client = std::make_unique<MockAutofillClient>();
  password_autofill_manager_.reset(new PasswordAutofillManager(
      client->mock_driver(), autofill_client.get(), client.get()));

  EXPECT_CALL(*autofill_client, ShowAutofillPopup(_, _, _, _, _)).Times(0);
  gfx::RectF element_bounds;
  EXPECT_FALSE(
      password_autofill_manager_->MaybeShowPasswordSuggestionsWithGeneration(
          element_bounds, base::i18n::RIGHT_TO_LEFT));
}

TEST_F(PasswordAutofillManagerTest,
       MaybeShowPasswordSuggestionsWithGenerationSomeCredentials) {
  auto client = std::make_unique<TestPasswordManagerClient>();
  auto autofill_client = std::make_unique<MockAutofillClient>();
  InitializePasswordAutofillManager(client.get(), autofill_client.get());

  gfx::RectF element_bounds;
  autofill::PasswordFormFillData data;
  data.username_field.value = test_username_;
  data.password_field.value = test_password_;
  data.origin = GURL("https://foo.test");

  int dummy_key = 0;
  password_autofill_manager_->OnAddPasswordFormMapping(dummy_key, data);

  // Bring up the drop-down with the generaion option.
  base::string16 generation_string =
      l10n_util::GetStringUTF16(IDS_PASSWORD_MANAGER_GENERATE_PASSWORD);
  EXPECT_CALL(*autofill_client,
              ShowAutofillPopup(
                  element_bounds, base::i18n::RIGHT_TO_LEFT,
                  SuggestionVectorValuesAre(testing::ElementsAreArray(
                      GetSuggestionList({test_username_, generation_string}))),
                  false, _));
  EXPECT_TRUE(
      password_autofill_manager_->MaybeShowPasswordSuggestionsWithGeneration(
          element_bounds, base::i18n::RIGHT_TO_LEFT));

  // Click "Generate password".
  EXPECT_CALL(*client, GeneratePassword());
  EXPECT_CALL(*autofill_client, HideAutofillPopup());
  password_autofill_manager_->DidAcceptSuggestion(
      base::string16(), autofill::POPUP_ITEM_ID_GENERATE_PASSWORD_ENTRY, 1);
}

}  // namespace password_manager
