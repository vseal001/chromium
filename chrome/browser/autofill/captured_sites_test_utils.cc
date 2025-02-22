// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/autofill/captured_sites_test_utils.h"

#include "base/files/file_util.h"
#include "base/json/json_reader.h"
#include "base/json/json_string_value_serializer.h"
#include "base/path_service.h"
#include "base/process/launch.h"
#include "base/strings/string16.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "base/values.h"
#include "build/build_config.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/browsing_data_remover.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/browsing_data_remover_test_util.h"
#include "content/public/test/test_renderer_host.h"
#include "content/public/test/test_utils.h"
#include "ipc/ipc_channel_factory.h"
#include "ipc/ipc_logging.h"
#include "ipc/ipc_message_macros.h"
#include "ipc/ipc_sync_message.h"

namespace {
// The maximum amount of time to wait for Chrome to finish autofilling a form.
const base::TimeDelta kAutofillActionWaitForVisualUpdateTimeout =
    base::TimeDelta::FromSeconds(3);

// The number of tries the TestRecipeReplayer should perform when executing an
// Chrome Autofill action.
// Chrome Autofill can be flaky on some real-world pages. The Captured Site
// Automation Framework will retry an autofill action a couple times before
// concluding that Chrome Autofill does not work.
const int kAutofillActionNumRetries = 5;
}  // namespace

namespace captured_sites_test_utils {

constexpr base::TimeDelta PageActivityObserver::kPaintEventCheckInterval;

std::string FilePathToUTF8(const base::FilePath::StringType& str) {
#if defined(OS_WIN)
  return base::WideToUTF8(str);
#else
  return str;
#endif
}

// PageActivityObserver -------------------------------------------------------
PageActivityObserver::PageActivityObserver(content::WebContents* web_contents)
    : content::WebContentsObserver(web_contents) {}

PageActivityObserver::PageActivityObserver(content::RenderFrameHost* frame)
    : content::WebContentsObserver(
          content::WebContents::FromRenderFrameHost(frame)) {}

void PageActivityObserver::WaitTillPageIsIdle(
    base::TimeDelta continuous_paint_timeout) {
  base::TimeTicks finished_load_time = base::TimeTicks::Now();
  bool page_is_loading = false;
  do {
    paint_occurred_during_last_loop_ = false;
    base::RunLoop heart_beat;
    base::ThreadTaskRunnerHandle::Get()->PostDelayedTask(
        FROM_HERE, heart_beat.QuitClosure(), kPaintEventCheckInterval);
    heart_beat.Run();
    page_is_loading =
        web_contents()->IsWaitingForResponse() || web_contents()->IsLoading();
    if (page_is_loading) {
      finished_load_time = base::TimeTicks::Now();
    } else if (base::TimeTicks::Now() - finished_load_time >
               continuous_paint_timeout) {
      // |continuous_paint_timeout| has expired since Chrome loaded the page.
      // During this period of time, Chrome has been continuously painting
      // the page. In this case, the page is probably idle, but a bug, a
      // blinking caret or a persistent animation is making Chrome paint at
      // regular intervals. Exit.
      break;
    }
  } while (page_is_loading || paint_occurred_during_last_loop_);
}

void PageActivityObserver::DidCommitAndDrawCompositorFrame() {
  paint_occurred_during_last_loop_ = true;
}

// TestRecipeReplayer ---------------------------------------------------------
TestRecipeReplayer::TestRecipeReplayer(
    Browser* browser,
    TestRecipeReplayChromeFeatureActionExecutor* feature_action_executor)
    : browser_(browser), feature_action_executor_(feature_action_executor) {}

TestRecipeReplayer::~TestRecipeReplayer(){};

bool TestRecipeReplayer::ReplayTest(const base::FilePath capture_file_path,
                                    const base::FilePath recipe_file_path) {
  if (StartWebPageReplayServer(capture_file_path)) {
    return ReplayRecordedActions(recipe_file_path);
  }
  return false;
}

// static
void TestRecipeReplayer::SetUpCommandLine(base::CommandLine* command_line) {
  // Direct traffic to the Web Page Replay server.
  command_line->AppendSwitchASCII(
      network::switches::kHostResolverRules,
      base::StringPrintf(
          "MAP *:80 127.0.0.1:%d,"
          "MAP *:443 127.0.0.1:%d,"
          // Uncomment to use the live autofill prediction server.
          //"EXCLUDE clients1.google.com,"
          "EXCLUDE localhost",
          kHostHttpPort, kHostHttpsPort));
}

void TestRecipeReplayer::Setup() {
  EXPECT_TRUE(InstallWebPageReplayServerRootCert())
      << "Cannot install the root certificate "
      << "for the local web page replay server.";
  CleanupSiteData();
}

void TestRecipeReplayer::Cleanup() {
  // If there are still cookies at the time the browser test shuts down,
  // Chrome's SQL lite persistent cookie store will crash.
  CleanupSiteData();
  EXPECT_TRUE(StopWebPageReplayServer())
      << "Cannot stop the local Web Page Replay server.";
  EXPECT_TRUE(RemoveWebPageReplayServerRootCert())
      << "Cannot remove the root certificate "
      << "for the local Web Page Replay server.";
}

TestRecipeReplayChromeFeatureActionExecutor*
TestRecipeReplayer::feature_action_executor() {
  return feature_action_executor_;
}

content::WebContents* TestRecipeReplayer::GetWebContents() {
  return browser_->tab_strip_model()->GetActiveWebContents();
}

void TestRecipeReplayer::CleanupSiteData() {
  // Navigate to about:blank, then clear the browser cache.
  // Navigating to about:blank before clearing the cache ensures that
  // the cleanup is thorough and nothing is held.
  ui_test_utils::NavigateToURL(browser_, GURL(url::kAboutBlankURL));
  content::BrowsingDataRemover* remover =
      content::BrowserContext::GetBrowsingDataRemover(browser_->profile());
  content::BrowsingDataRemoverCompletionObserver completion_observer(remover);
  remover->RemoveAndReply(
      base::Time(), base::Time::Max(),
      content::BrowsingDataRemover::DATA_TYPE_COOKIES,
      content::BrowsingDataRemover::ORIGIN_TYPE_UNPROTECTED_WEB,
      &completion_observer);
  completion_observer.BlockUntilCompletion();
}

bool TestRecipeReplayer::StartWebPageReplayServer(
    const base::FilePath& capture_file_path) {
  std::vector<std::string> args;
  base::FilePath src_dir;
  if (!base::PathService::Get(base::DIR_SOURCE_ROOT, &src_dir))
    return false;

  args.push_back(base::StringPrintf("--http_port=%d", kHostHttpPort));
  args.push_back(base::StringPrintf("--https_port=%d", kHostHttpsPort));
  args.push_back(base::StringPrintf(
      "--inject_scripts=%s,%s",
      FilePathToUTF8(
          src_dir.AppendASCII("third_party/catapult/web_page_replay_go")
              .AppendASCII("deterministic.js")
              .value())
          .c_str(),
      FilePathToUTF8(
          src_dir
              .AppendASCII("chrome/test/data/web_page_replay_go_helper_scripts")
              .AppendASCII("automation_helper.js")
              .value())
          .c_str()));

  // Specify the capture file.
  args.push_back(base::StringPrintf(
      "%s", FilePathToUTF8(capture_file_path.value()).c_str()));
  if (!RunWebPageReplayCmd("replay", args, &web_page_replay_server_))
    return false;

  // Sleep 20 seconds to wait for the web page replay server to start.
  // TODO(crbug.com/847910): create a process std stream reader class to use the
  // process output to determine when the server is ready
  base::RunLoop wpr_launch_waiter;
  base::ThreadTaskRunnerHandle::Get()->PostDelayedTask(
      FROM_HERE, wpr_launch_waiter.QuitClosure(),
      base::TimeDelta::FromSeconds(20));
  wpr_launch_waiter.Run();

  return web_page_replay_server_.IsValid();
}

bool TestRecipeReplayer::StopWebPageReplayServer() {
  if (web_page_replay_server_.IsValid())
    return web_page_replay_server_.Terminate(0, true);
  // The test server hasn't started, no op.
  return true;
}

bool TestRecipeReplayer::InstallWebPageReplayServerRootCert() {
  return RunWebPageReplayCmdAndWaitForExit("installroot",
                                           std::vector<std::string>());
}

bool TestRecipeReplayer::RemoveWebPageReplayServerRootCert() {
  return RunWebPageReplayCmdAndWaitForExit("removeroot",
                                           std::vector<std::string>());
}

bool TestRecipeReplayer::RunWebPageReplayCmdAndWaitForExit(
    const std::string& cmd,
    const std::vector<std::string>& args,
    const base::TimeDelta& timeout) {
  base::Process process;
  if (!RunWebPageReplayCmd(cmd, args, &process))
    return false;
  if (process.IsValid()) {
    int exit_code;
    if (process.WaitForExitWithTimeout(timeout, &exit_code))
      return (exit_code == 0);
  }
  return false;
}

bool TestRecipeReplayer::RunWebPageReplayCmd(
    const std::string& cmd,
    const std::vector<std::string>& args,
    base::Process* process) {
  base::LaunchOptions options = base::LaunchOptionsForTest();
  base::FilePath exe_dir;
  if (!base::PathService::Get(base::DIR_SOURCE_ROOT, &exe_dir))
    return false;

  base::FilePath web_page_replay_binary_dir = exe_dir.AppendASCII(
      "third_party/catapult/telemetry/telemetry/internal/bin");
  options.current_directory = web_page_replay_binary_dir;

#if defined(OS_WIN)
  std::string wpr_executable_binary = "win/x86_64/wpr";
#elif defined(OS_MACOSX)
  std::string wpr_executable_binary = "mac/x86_64/wpr";
#elif defined(OS_POSIX)
  std::string wpr_executable_binary = "linux/x86_64/wpr";
#else
#error Plaform is not supported.
#endif
  base::CommandLine full_command(
      web_page_replay_binary_dir.AppendASCII(wpr_executable_binary));
  full_command.AppendArg(cmd);

  // Ask web page replay to use the custom certifcate and key files used to
  // make the web page captures.
  // The capture files used in these browser tests are also used on iOS to
  // test autofill.
  // The custom cert and key files are different from those of the offical
  // WPR releases. The custom files are made to work on iOS.
  base::FilePath src_dir;
  if (!base::PathService::Get(base::DIR_SOURCE_ROOT, &src_dir))
    return false;

  base::FilePath web_page_replay_support_file_dir = src_dir.AppendASCII(
      "components/test/data/autofill/web_page_replay_support_files");
  full_command.AppendArg(base::StringPrintf(
      "--https_cert_file=%s",
      FilePathToUTF8(
          web_page_replay_support_file_dir.AppendASCII("wpr_cert.pem").value())
          .c_str()));
  full_command.AppendArg(base::StringPrintf(
      "--https_key_file=%s",
      FilePathToUTF8(
          web_page_replay_support_file_dir.AppendASCII("wpr_key.pem").value())
          .c_str()));

  for (const auto arg : args)
    full_command.AppendArg(arg);

  *process = base::LaunchProcess(full_command, options);
  return true;
}

bool TestRecipeReplayer::ReplayRecordedActions(
    const base::FilePath recipe_file_path) {
  // Read the text of the recipe file.
  base::ThreadRestrictions::SetIOAllowed(true);
  std::string json_text;
  if (!base::ReadFileToString(recipe_file_path, &json_text))
    return false;

  // Convert the file text into a json object.
  std::unique_ptr<base::DictionaryValue> recipe =
      base::DictionaryValue::From(base::JSONReader().ReadToValue(json_text));
  if (!recipe) {
    ADD_FAILURE() << "Failed to deserialize json text!";
    return false;
  }

  InitializeBrowserToExecuteRecipe(recipe);

  // Iterate through and execute each action in the recipe.
  base::Value* action_list_container = recipe->FindKey("actions");
  if (!action_list_container)
    return false;
  if (base::Value::Type::LIST != action_list_container->type())
    return false;
  base::Value::ListStorage& action_list = action_list_container->GetList();

  for (base::ListValue::iterator it_action = action_list.begin();
       it_action != action_list.end(); ++it_action) {
    base::DictionaryValue* action;
    if (!it_action->GetAsDictionary(&action))
      return false;

    base::Value* type_container = action->FindKey("type");
    if (!type_container)
      return false;
    if (base::Value::Type::STRING != type_container->type())
      return false;
    std::string type = type_container->GetString();

    if (base::CompareCaseInsensitiveASCII(type, "autofill") == 0) {
      ExecuteAutofillAction(action);
    } else if (base::CompareCaseInsensitiveASCII(type, "click") == 0) {
      ExecuteClickAction(action);
    } else if (base::CompareCaseInsensitiveASCII(type, "select") == 0) {
      ExecuteSelectDropdownAction(action);
    } else if (base::CompareCaseInsensitiveASCII(type, "type") == 0) {
      ExecuteTypeAction(action);
    } else if (base::CompareCaseInsensitiveASCII(type, "validateField") == 0) {
      ExecuteValidateFieldValueAction(action);
    } else if (base::CompareCaseInsensitiveASCII(type, "waitFor") == 0) {
      ExecuteWaitForStateAction(action);
    } else {
      ADD_FAILURE() << "Unrecognized action type: " << type;
    }
  }  // end foreach action
  return true;
}

// Functions for deserializing and executing actions from the test recipe
// JSON object.
void TestRecipeReplayer::InitializeBrowserToExecuteRecipe(
    std::unique_ptr<base::DictionaryValue>& recipe) {
  // Extract the starting URL from the test recipe.
  base::Value* starting_url_container = recipe->FindKey("startingURL");
  ASSERT_TRUE(starting_url_container);
  ASSERT_EQ(base::Value::Type::STRING, starting_url_container->type());

  // Navigate to the starting URL, wait for the page to complete loading.
  PageActivityObserver page_activity_observer(GetWebContents());
  ASSERT_TRUE(content::ExecuteScript(
      GetWebContents(),
      base::StringPrintf("window.location.href = '%s';",
                         starting_url_container->GetString().c_str())));
  page_activity_observer.WaitTillPageIsIdle();
}

void TestRecipeReplayer::ExecuteAutofillAction(base::DictionaryValue* action) {
  std::string xpath;
  ASSERT_TRUE(GetTargetHTMLElementXpathFromAction(action, &xpath));
  WaitForElemementToBeReady(xpath);

  VLOG(1) << "Invoking Chrome Autofill on `" << xpath << "`.";
  PageActivityObserver page_activity_observer(GetWebContents());
  // Clear the input box first, in case a previous value is there.
  // If the text input box is not clear, pressing the down key will not
  // bring up the autofill suggestion box.
  // This can happen on sites that requires the user to sign in. After
  // signing in, the site fills the form with the user's profile
  // information.
  ASSERT_TRUE(ExecuteJavaScriptOnElementByXpath(
      xpath, "automation_helper.setInputElementValue(target, ``);"));
  ASSERT_TRUE(feature_action_executor()->AutofillForm(
      GetWebContents(), xpath, kAutofillActionNumRetries));
  page_activity_observer.WaitTillPageIsIdle(
      kAutofillActionWaitForVisualUpdateTimeout);
}

void TestRecipeReplayer::ExecuteClickAction(base::DictionaryValue* action) {
  std::string xpath;
  ASSERT_TRUE(GetTargetHTMLElementXpathFromAction(action, &xpath));
  WaitForElemementToBeReady(xpath);

  VLOG(1) << "Left mouse clicking `" << xpath << "`.";
  PageActivityObserver page_activity_observer(GetWebContents());
  ASSERT_TRUE(ExecuteJavaScriptOnElementByXpath(xpath, "target.click();"));
  page_activity_observer.WaitTillPageIsIdle();
}

void TestRecipeReplayer::ExecuteSelectDropdownAction(
    base::DictionaryValue* action) {
  base::Value* index_container = action->FindKey("index");
  ASSERT_TRUE(index_container);
  ASSERT_EQ(base::Value::Type::INTEGER, index_container->type());
  int index = index_container->GetInt();

  std::string xpath;
  ASSERT_TRUE(GetTargetHTMLElementXpathFromAction(action, &xpath));
  WaitForElemementToBeReady(xpath);

  VLOG(1) << "Select option '" << index << "' from `" << xpath << "`.";
  PageActivityObserver page_activity_observer(GetWebContents());
  ASSERT_TRUE(ExecuteJavaScriptOnElementByXpath(
      xpath, base::StringPrintf(
                 "automation_helper"
                 "  .selectOptionFromDropDownElementByIndex(target, %d);",
                 index_container->GetInt())));
  page_activity_observer.WaitTillPageIsIdle();
}

void TestRecipeReplayer::ExecuteTypeAction(base::DictionaryValue* action) {
  base::Value* value_container = action->FindKey("value");
  ASSERT_TRUE(value_container);
  ASSERT_EQ(base::Value::Type::STRING, value_container->type());
  std::string value = value_container->GetString();

  std::string xpath;
  ASSERT_TRUE(GetTargetHTMLElementXpathFromAction(action, &xpath));
  WaitForElemementToBeReady(xpath);

  VLOG(1) << "Typing '" << value << "' inside `" << xpath << "`.";
  PageActivityObserver page_activity_observer(GetWebContents());
  ASSERT_TRUE(ExecuteJavaScriptOnElementByXpath(
      xpath, base::StringPrintf(
                 "automation_helper.setInputElementValue(target, `%s`);",
                 value.c_str())));
  page_activity_observer.WaitTillPageIsIdle();
}

void TestRecipeReplayer::ExecuteValidateFieldValueAction(
    base::DictionaryValue* action) {
  std::string xpath;
  ASSERT_TRUE(GetTargetHTMLElementXpathFromAction(action, &xpath));
  WaitForElemementToBeReady(xpath);

  base::Value* autofill_prediction_container =
      action->FindKey("expectedAutofillType");
  if (autofill_prediction_container) {
    ASSERT_EQ(base::Value::Type::STRING, autofill_prediction_container->type());
    std::string expected_autofill_prediction_type =
        autofill_prediction_container->GetString();
    VLOG(1) << "Checking the field `" << xpath << "` has the autofill type '"
            << expected_autofill_prediction_type << "'";
    ExpectElementPropertyEquals(
        xpath.c_str(), "return target.getAttribute('autofill-prediction');",
        expected_autofill_prediction_type, true);
  }

  base::Value* expected_value_container = action->FindKey("expectedValue");
  ASSERT_TRUE(expected_value_container);
  ASSERT_EQ(base::Value::Type::STRING, expected_value_container->type());
  std::string expected_value = expected_value_container->GetString();

  VLOG(1) << "Checking the field `" << xpath << "`.";
  ExpectElementPropertyEquals(xpath.c_str(), "return target.value;",
                              expected_value);
}

void TestRecipeReplayer::ExecuteWaitForStateAction(
    base::DictionaryValue* action) {
  // Extract the list of JavaScript assertions into a vector.
  std::vector<std::string> state_assertions;
  base::Value* assertions_list_container = action->FindKey("assertions");
  ASSERT_TRUE(assertions_list_container);
  ASSERT_EQ(base::Value::Type::LIST, assertions_list_container->type());
  base::Value::ListStorage& assertions_list =
      assertions_list_container->GetList();
  for (base::ListValue::iterator it_assertion = assertions_list.begin();
       it_assertion != assertions_list.end(); ++it_assertion) {
    ASSERT_EQ(base::Value::Type::STRING, it_assertion->type());
    state_assertions.push_back(it_assertion->GetString());
  }

  VLOG(1) << "Waiting for page to reach a state.";

  // Wait for all of the assertions to become true on the current page.
  ASSERT_TRUE(WaitForStateChange(state_assertions, default_action_timeout));
}

bool TestRecipeReplayer::GetTargetHTMLElementXpathFromAction(
    base::DictionaryValue* action,
    std::string* xpath) {
  xpath->clear();
  base::Value* xpath_container = action->FindKey("selector");
  if (!xpath_container)
    return false;
  if (base::Value::Type::STRING != xpath_container->type())
    return false;
  *xpath = xpath_container->GetString();
  return true;
}

void TestRecipeReplayer::WaitForElemementToBeReady(std::string xpath) {
  std::vector<std::string> state_assertions;
  state_assertions.push_back(base::StringPrintf(
      "return automation_helper.isElementWithXpathReady(`%s`);",
      xpath.c_str()));
  ASSERT_TRUE(WaitForStateChange(state_assertions, default_action_timeout));
}

bool TestRecipeReplayer::WaitForStateChange(
    const std::vector<std::string>& state_assertions,
    const base::TimeDelta& timeout) {
  const base::TimeTicks start_time = base::TimeTicks::Now();
  PageActivityObserver page_activity_observer(GetWebContents());
  while (!AllAssertionsPassed(state_assertions)) {
    if (base::TimeTicks::Now() - start_time > timeout) {
      ADD_FAILURE() << "State change hasn't completed within timeout.";
      return false;
    }
    page_activity_observer.WaitTillPageIsIdle();
  }
  return true;
}

bool TestRecipeReplayer::AllAssertionsPassed(
    const std::vector<std::string>& assertions) {
  for (const std::string& assertion : assertions) {
    bool assertion_passed = false;
    EXPECT_TRUE(ExecuteScriptAndExtractBool(
        GetWebContents(),
        base::StringPrintf("window.domAutomationController.send("
                           "    (function() {"
                           "      try {"
                           "        %s"
                           "      } catch (ex) {}"
                           "      return false;"
                           "    })());",
                           assertion.c_str()),
        &assertion_passed));
    if (!assertion_passed) {
      VLOG(1) << "'" << assertion << "' failed!";
      return false;
    }
  }
  return true;
}

bool TestRecipeReplayer::ExecuteJavaScriptOnElementByXpath(
    const std::string& element_xpath,
    const std::string& execute_function_body,
    const base::TimeDelta& time_to_wait_for_element) {
  std::string js(base::StringPrintf(
      "try {"
      "  var element = automation_helper.getElementByXpath(`%s`);"
      "  (function(target) { %s })(element);"
      "} catch(ex) {}",
      element_xpath.c_str(), execute_function_body.c_str()));
  return ExecuteScript(GetWebContents(), js);
}

bool TestRecipeReplayer::ExpectElementPropertyEquals(
    const std::string& element_xpath,
    const std::string& get_property_function_body,
    const std::string& expected_value,
    bool ignoreCase) {
  std::string value;
  if (ExecuteScriptAndExtractString(
          GetWebContents(),
          base::StringPrintf(
              "window.domAutomationController.send("
              "    (function() {"
              "      try {"
              "        var element = function() {"
              "          return automation_helper.getElementByXpath(`%s`);"
              "        }();"
              "        return function(target){%s}(element);"
              "      } catch (ex) {}"
              "      return 'Exception encountered';"
              "    })());",
              element_xpath.c_str(), get_property_function_body.c_str()),
          &value)) {
    if (ignoreCase) {
      EXPECT_TRUE(base::EqualsCaseInsensitiveASCII(expected_value, value))
          << "Field xpath: `" << element_xpath << "`, "
          << "Expected: " << expected_value << ", actual: " << value;
    } else {
      EXPECT_EQ(expected_value, value)
          << "Field xpath: `" << element_xpath << "`, ";
    }
    return true;
  }
  VLOG(1) << element_xpath << ", " << get_property_function_body;
  return false;
}

// TestRecipeReplayChromeFeatureActionExecutor --------------------------------
TestRecipeReplayChromeFeatureActionExecutor::
    TestRecipeReplayChromeFeatureActionExecutor() {}
TestRecipeReplayChromeFeatureActionExecutor::
    ~TestRecipeReplayChromeFeatureActionExecutor() {}

bool TestRecipeReplayChromeFeatureActionExecutor::AutofillForm(
    content::WebContents* web_contents,
    const std::string& focus_element_css_selector,
    const int attempts) {
  return false;
}

}  // namespace captured_sites_test_utils
