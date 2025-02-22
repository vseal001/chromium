// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/policy/user_cloud_policy_store_chromeos.h"

#include <utility>

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/debug/dump_without_crashing.h"
#include "base/logging.h"
#include "base/metrics/histogram_macros.h"
#include "base/sequenced_task_runner.h"
#include "chrome/browser/chromeos/policy/cached_policy_key_loader_chromeos.h"
#include "chrome/browser/lifetime/application_lifetime.h"
#include "chromeos/cryptohome/cryptohome_parameters.h"
#include "google_apis/gaia/gaia_auth_util.h"

using RetrievePolicyResponseType =
    chromeos::SessionManagerClient::RetrievePolicyResponseType;

namespace em = enterprise_management;

namespace policy {

namespace {

// Extracts the domain name from the passed username.
std::string ExtractDomain(const std::string& username) {
  return gaia::ExtractDomainName(gaia::CanonicalizeEmail(username));
}

}  // namespace

UserCloudPolicyStoreChromeOS::UserCloudPolicyStoreChromeOS(
    chromeos::CryptohomeClient* cryptohome_client,
    chromeos::SessionManagerClient* session_manager_client,
    scoped_refptr<base::SequencedTaskRunner> background_task_runner,
    const AccountId& account_id,
    const base::FilePath& user_policy_key_dir,
    bool is_active_directory)
    : UserCloudPolicyStoreBase(background_task_runner,
                               PolicyScope::POLICY_SCOPE_USER),
      session_manager_client_(session_manager_client),
      account_id_(account_id),
      is_active_directory_(is_active_directory),
      cached_policy_key_loader_(std::make_unique<CachedPolicyKeyLoaderChromeOS>(
          cryptohome_client,
          background_task_runner,
          account_id,
          user_policy_key_dir)),
      weak_factory_(this) {}

UserCloudPolicyStoreChromeOS::~UserCloudPolicyStoreChromeOS() {}

void UserCloudPolicyStoreChromeOS::Store(
    const em::PolicyFetchResponse& policy) {
  DCHECK(!is_active_directory_);

  // Cancel all pending requests.
  weak_factory_.InvalidateWeakPtrs();
  std::unique_ptr<em::PolicyFetchResponse> response(
      new em::PolicyFetchResponse(policy));
  cached_policy_key_loader_->EnsurePolicyKeyLoaded(
      base::Bind(&UserCloudPolicyStoreChromeOS::ValidatePolicyForStore,
                 weak_factory_.GetWeakPtr(), base::Passed(&response)));
}

void UserCloudPolicyStoreChromeOS::Load() {
  // Cancel all pending requests.
  weak_factory_.InvalidateWeakPtrs();
  session_manager_client_->RetrievePolicyForUser(
      cryptohome::CreateAccountIdentifierFromAccountId(account_id_),
      base::BindOnce(&UserCloudPolicyStoreChromeOS::OnPolicyRetrieved,
                     weak_factory_.GetWeakPtr()));
}

void UserCloudPolicyStoreChromeOS::LoadImmediately() {
  // This blocking D-Bus call is in the startup path and will block the UI
  // thread. This only happens when the Profile is created synchronously, which
  // on Chrome OS happens whenever the browser is restarted into the same
  // session. That happens when the browser crashes, or right after signin if
  // the user has flags configured in about:flags.
  // However, on those paths we must load policy synchronously so that the
  // Profile initialization never sees unmanaged prefs, which would lead to
  // data loss. http://crbug.com/263061
  std::string policy_blob;
  RetrievePolicyResponseType response_type =
      session_manager_client_->BlockingRetrievePolicyForUser(
          cryptohome::CreateAccountIdentifierFromAccountId(account_id_),
          &policy_blob);

  if (response_type == RetrievePolicyResponseType::GET_SERVICE_FAIL) {
    LOG(ERROR)
        << "Session manager claims that session doesn't exist; signing out";
    base::debug::DumpWithoutCrashing();
    chrome::AttemptUserExit();
    return;
  }

  if (policy_blob.empty()) {
    // The session manager doesn't have policy, or the call failed.
    NotifyStoreLoaded();
    return;
  }

  std::unique_ptr<em::PolicyFetchResponse> policy(
      new em::PolicyFetchResponse());
  if (!policy->ParseFromString(policy_blob)) {
    status_ = STATUS_PARSE_ERROR;
    NotifyStoreError();
    return;
  }

  if (!cached_policy_key_loader_->LoadPolicyKeyImmediately()) {
    status_ = STATUS_LOAD_ERROR;
    NotifyStoreError();
    return;
  }

  std::unique_ptr<UserCloudPolicyValidator> validator =
      CreateValidatorForLoad(std::move(policy));
  validator->RunValidation();
  OnRetrievedPolicyValidated(validator.get());
}

void UserCloudPolicyStoreChromeOS::ValidatePolicyForStore(
    std::unique_ptr<em::PolicyFetchResponse> policy) {
  DCHECK(!is_active_directory_);

  // Create and configure a validator.
  std::unique_ptr<UserCloudPolicyValidator> validator = CreateValidator(
      std::move(policy), CloudPolicyValidatorBase::TIMESTAMP_VALIDATED);
  validator->ValidateUser(account_id_);
  const std::string& cached_policy_key =
      cached_policy_key_loader_->cached_policy_key();
  if (cached_policy_key.empty()) {
    validator->ValidateInitialKey(ExtractDomain(account_id_.GetUserEmail()));
  } else {
    validator->ValidateSignatureAllowingRotation(
        cached_policy_key, ExtractDomain(account_id_.GetUserEmail()));
  }

  // Start validation.
  UserCloudPolicyValidator::StartValidation(
      std::move(validator),
      base::Bind(&UserCloudPolicyStoreChromeOS::OnPolicyToStoreValidated,
                 weak_factory_.GetWeakPtr()));
}

void UserCloudPolicyStoreChromeOS::OnPolicyToStoreValidated(
    UserCloudPolicyValidator* validator) {
  DCHECK(!is_active_directory_);

  validation_status_ = validator->status();

  UMA_HISTOGRAM_ENUMERATION(
      "Enterprise.UserPolicyValidationStoreStatus",
      validation_status_,
      UserCloudPolicyValidator::VALIDATION_STATUS_SIZE);

  if (!validator->success()) {
    status_ = STATUS_VALIDATION_ERROR;
    NotifyStoreError();
    return;
  }

  std::string policy_blob;
  if (!validator->policy()->SerializeToString(&policy_blob)) {
    status_ = STATUS_SERIALIZE_ERROR;
    NotifyStoreError();
    return;
  }

  session_manager_client_->StorePolicyForUser(
      cryptohome::CreateAccountIdentifierFromAccountId(account_id_),
      policy_blob,
      base::Bind(&UserCloudPolicyStoreChromeOS::OnPolicyStored,
                 weak_factory_.GetWeakPtr()));
}

void UserCloudPolicyStoreChromeOS::OnPolicyStored(bool success) {
  DCHECK(!is_active_directory_);

  if (!success) {
    status_ = STATUS_STORE_ERROR;
    NotifyStoreError();
  } else {
    // Load the policy right after storing it, to make sure it was accepted by
    // the session manager. An additional validation is performed after the
    // load; reload the key for that validation too, in case it was rotated.
    cached_policy_key_loader_->ReloadPolicyKey(base::Bind(
        &UserCloudPolicyStoreChromeOS::Load, weak_factory_.GetWeakPtr()));
  }
}

void UserCloudPolicyStoreChromeOS::OnPolicyRetrieved(
    RetrievePolicyResponseType response_type,
    const std::string& policy_blob) {
  // Disallow the sign in when the Chrome OS user session has not started, which
  // should always happen before the profile construction. An attempt to read
  // the policy outside the session will always fail and return an empty policy
  // blob.
  if (response_type == RetrievePolicyResponseType::GET_SERVICE_FAIL) {
    LOG(ERROR)
        << "Session manager claims that session doesn't exist; signing out";
    base::debug::DumpWithoutCrashing();
    chrome::AttemptUserExit();
    return;
  }

  if (policy_blob.empty()) {
    // session_manager doesn't have policy. Adjust internal state and notify
    // the world about the policy update.
    policy_map_.Clear();
    policy_.reset();
    policy_signature_public_key_.clear();
    NotifyStoreLoaded();
    return;
  }

  std::unique_ptr<em::PolicyFetchResponse> policy(
      new em::PolicyFetchResponse());
  if (!policy->ParseFromString(policy_blob)) {
    status_ = STATUS_PARSE_ERROR;
    NotifyStoreError();
    return;
  }

  // Load |cached_policy_key_| to verify the loaded policy.
  if (is_active_directory_) {
    ValidateRetrievedPolicy(std::move(policy));
  } else {
    cached_policy_key_loader_->EnsurePolicyKeyLoaded(
        base::Bind(&UserCloudPolicyStoreChromeOS::ValidateRetrievedPolicy,
                   weak_factory_.GetWeakPtr(), base::Passed(&policy)));
  }
}

void UserCloudPolicyStoreChromeOS::ValidateRetrievedPolicy(
    std::unique_ptr<em::PolicyFetchResponse> policy) {
  UserCloudPolicyValidator::StartValidation(
      CreateValidatorForLoad(std::move(policy)),
      base::Bind(&UserCloudPolicyStoreChromeOS::OnRetrievedPolicyValidated,
                 weak_factory_.GetWeakPtr()));
}

void UserCloudPolicyStoreChromeOS::OnRetrievedPolicyValidated(
    UserCloudPolicyValidator* validator) {
  validation_status_ = validator->status();

  UMA_HISTOGRAM_ENUMERATION(
      "Enterprise.UserPolicyValidationLoadStatus",
      validation_status_,
      UserCloudPolicyValidator::VALIDATION_STATUS_SIZE);

  if (!validator->success()) {
    status_ = STATUS_VALIDATION_ERROR;
    NotifyStoreError();
    return;
  }

  InstallPolicy(std::move(validator->policy_data()),
                std::move(validator->payload()),
                cached_policy_key_loader_->cached_policy_key());
  status_ = STATUS_OK;

  NotifyStoreLoaded();
}

std::unique_ptr<UserCloudPolicyValidator>
UserCloudPolicyStoreChromeOS::CreateValidatorForLoad(
    std::unique_ptr<em::PolicyFetchResponse> policy) {
  std::unique_ptr<UserCloudPolicyValidator> validator = CreateValidator(
      std::move(policy), CloudPolicyValidatorBase::TIMESTAMP_VALIDATED);
  if (is_active_directory_) {
    validator->ValidateTimestamp(
        base::Time(), CloudPolicyValidatorBase::TIMESTAMP_NOT_VALIDATED);
    validator->ValidateDMToken(std::string(),
                               CloudPolicyValidatorBase::DM_TOKEN_NOT_REQUIRED);
    validator->ValidateDeviceId(
        std::string(), CloudPolicyValidatorBase::DEVICE_ID_NOT_REQUIRED);
  } else {
    validator->ValidateUser(account_id_);
    // The policy loaded from session manager need not be validated using the
    // verification key since it is secure, and since there may be legacy policy
    // data that was stored without a verification key.
    validator->ValidateSignature(
        cached_policy_key_loader_->cached_policy_key());
  }
  return validator;
}

}  // namespace policy
