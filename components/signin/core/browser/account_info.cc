// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/signin/core/browser/account_info.h"

namespace {

bool UpdateField(std::string* field, const std::string& new_value) {
  bool should_update = field->empty() && !new_value.empty();
  if (should_update)
    *field = new_value;
  return should_update;
}

bool UpdateField(bool* field, bool new_value) {
  bool should_update = !*field && new_value;
  if (should_update)
    *field = new_value;
  return should_update;
}

}  // namespace

AccountInfo::AccountInfo() = default;

AccountInfo::~AccountInfo() = default;

AccountInfo::AccountInfo(const AccountInfo& other) = default;

AccountInfo::AccountInfo(AccountInfo&& other) noexcept = default;

AccountInfo& AccountInfo::operator=(const AccountInfo& other) = default;

AccountInfo& AccountInfo::operator=(AccountInfo&& other) noexcept = default;

bool AccountInfo::IsEmpty() const {
  return account_id.empty() && email.empty() && gaia.empty() &&
         hosted_domain.empty() && full_name.empty() && given_name.empty() &&
         locale.empty() && picture_url.empty();
}

bool AccountInfo::IsValid() const {
  return !account_id.empty() && !email.empty() && !gaia.empty() &&
         !hosted_domain.empty() && !full_name.empty() && !given_name.empty() &&
         !locale.empty() && !picture_url.empty();
}

bool AccountInfo::UpdateWith(const AccountInfo& other) {
  if (account_id != other.account_id) {
    // Only updates with a compatible AccountInfo.
    return false;
  }

  bool modified = UpdateField(&gaia, other.gaia);
  modified |= UpdateField(&email, other.email);
  modified |= UpdateField(&full_name, other.full_name);
  modified |= UpdateField(&given_name, other.given_name);
  modified |= UpdateField(&hosted_domain, other.hosted_domain);
  modified |= UpdateField(&locale, other.locale);
  modified |= UpdateField(&picture_url, other.picture_url);
  modified |= UpdateField(&is_child_account, other.is_child_account);

  return modified;
}

AccountId AccountIdFromAccountInfo(const AccountInfo& account_info) {
  if (account_info.IsEmpty())
    return EmptyAccountId();

  DCHECK(!account_info.email.empty() && !account_info.gaia.empty());
  return AccountId::FromUserEmailGaiaId(account_info.email, account_info.gaia);
}
