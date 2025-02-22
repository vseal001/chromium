// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_AUTOFILL_CORE_BROWSER_PAYMENTS_PAYMENTS_CLIENT_H_
#define COMPONENTS_AUTOFILL_CORE_BROWSER_PAYMENTS_PAYMENTS_CLIENT_H_

#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "components/autofill/core/browser/autofill_client.h"
#include "components/autofill/core/browser/autofill_profile.h"
#include "components/autofill/core/browser/card_unmask_delegate.h"
#include "components/autofill/core/browser/credit_card.h"
#include "components/prefs/pref_service.h"
#include "google_apis/gaia/google_service_auth_error.h"
#include "services/identity/public/cpp/access_token_info.h"

namespace identity {
class IdentityManager;
class PrimaryAccountAccessTokenFetcher;
}  // namespace identity

namespace network {
struct ResourceRequest;
class SimpleURLLoader;
class SharedURLLoaderFactory;
}  // namespace network

namespace autofill {

namespace payments {

class PaymentsRequest;

// PaymentsClient issues Payments RPCs and manages responses and failure
// conditions. Only one request may be active at a time. Initiating a new
// request will cancel a pending request.
// Tests are located in
// src/components/autofill/content/browser/payments/payments_client_unittest.cc.
class PaymentsClient {
 public:
  // The names of the fields used to send non-location elements as part of an
  // address. Used in the implementation and in tests which verify that these
  // values are set or not at appropriate times.
  static const char kRecipientName[];
  static const char kPhoneNumber[];

  // A collection of the information required to make a credit card unmask
  // request.
  struct UnmaskRequestDetails {
    UnmaskRequestDetails();
    UnmaskRequestDetails(const UnmaskRequestDetails& other);
    ~UnmaskRequestDetails();

    int64_t billing_customer_number = 0;
    CreditCard card;
    std::string risk_data;
    CardUnmaskDelegate::UnmaskResponse user_response;
  };

  // A collection of the information required to make a credit card upload
  // request.
  struct UploadRequestDetails {
    UploadRequestDetails();
    UploadRequestDetails(const UploadRequestDetails& other);
    ~UploadRequestDetails();

    int64_t billing_customer_number = 0;
    CreditCard card;
    base::string16 cvc;
    std::vector<AutofillProfile> profiles;
    base::string16 context_token;
    std::string risk_data;
    std::string app_locale;
    std::vector<const char*> active_experiments;
  };

  // |url_loader_factory| is reference counted so it has no lifetime or
  // ownership requirements. |pref_service| is used to get the registered
  // preference value, |identity_manager|, |unmask_delegate| and |save_delegate|
  // must all outlive |this|. Either delegate might be nullptr.
  // |is_off_the_record| denotes incognito mode.
  PaymentsClient(
      scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory,
      PrefService* pref_service,
      identity::IdentityManager* identity_manager,
      bool is_off_the_record = false);

  virtual ~PaymentsClient();

  // Starts fetching the OAuth2 token in anticipation of future Payments
  // requests. Called as an optimization, but not strictly necessary. Should
  // *not* be called in advance of GetUploadDetails or UploadCard because
  // identifying information should not be sent until the user has explicitly
  // accepted an upload prompt.
  void Prepare();

  PrefService* GetPrefService() const;

  // The user has attempted to unmask a card with the given cvc.
  void UnmaskCard(const UnmaskRequestDetails& request_details,
                  base::OnceCallback<void(AutofillClient::PaymentsRpcResult,
                                          const std::string&)> callback);

  // Determine if the user meets the Payments service's conditions for upload.
  // The service uses |addresses| (from which names and phone numbers are
  // removed) and |app_locale| to determine which legal message to display.
  // |pan_first_six| is the first six digits of the number of the credit card
  // being considered for upload. |detected_values| is a bitmask of
  // CreditCardSaveManager::DetectedValue values that relays what data is
  // actually available for upload in order to make more informed upload
  // decisions. |callback| is the callback function when get response from
  // server. If the conditions are met, the legal message will be returned via
  // |callback|. |active_experiments| is used by Payments server to track
  // requests that were triggered by enabled features.
  virtual void GetUploadDetails(
      const std::vector<AutofillProfile>& addresses,
      const int detected_values,
      const std::string& pan_first_six,
      const std::vector<const char*>& active_experiments,
      const std::string& app_locale,
      base::OnceCallback<void(AutofillClient::PaymentsRpcResult,
                              const base::string16&,
                              std::unique_ptr<base::DictionaryValue>)>
          callback);

  // The user has indicated that they would like to upload a card with the given
  // cvc. This request will fail server-side if a successful call to
  // GetUploadDetails has not already been made.
  virtual void UploadCard(
      const UploadRequestDetails& details,
      base::OnceCallback<void(AutofillClient::PaymentsRpcResult,
                              const std::string&)> callback);

  // Cancels and clears the current |request_|.
  void CancelRequest();

  // Exposed for testing.
  void set_url_loader_factory_for_testing(
      scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory);

 private:
  friend class PaymentsClientTest;

  // Initiates a Payments request using the state in |request|. If
  // |authenticate| is true, ensures that an OAuth token is avialble first.
  // Takes ownership of |request|.
  void IssueRequest(std::unique_ptr<PaymentsRequest> request,
                    bool authenticate);

  // Creates |resource_request_| to be used later in StartRequest().
  void InitializeResourceRequest();

  // Callback from |simple_url_loader_|.
  void OnSimpleLoaderComplete(std::unique_ptr<std::string> response_body);
  void OnSimpleLoaderCompleteInternal(int response_code,
                                      const std::string& data);

  // Callback that handles a completed access token request.
  void AccessTokenFetchFinished(GoogleServiceAuthError error,
                                identity::AccessTokenInfo access_token_info);

  // Handles a completed access token request in the case of failure.
  void AccessTokenError(const GoogleServiceAuthError& error);

  // Initiates a new OAuth2 token request.
  void StartTokenFetch(bool invalidate_old);

  // Adds the token to |simple_url_loader_| and starts the request.
  void SetOAuth2TokenAndStartRequest();

  // Creates |simple_url_loader_| and calls it to start the request.
  void StartRequest();

  // The URL loader factory for the request.
  scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory_;

  // The pref service for this client.
  PrefService* const pref_service_;

  identity::IdentityManager* const identity_manager_;

  // The current request.
  std::unique_ptr<PaymentsRequest> request_;

  // The resource request being used to issue the current request.
  std::unique_ptr<network::ResourceRequest> resource_request_;

  // The URL loader being used to issue the current request.
  std::unique_ptr<network::SimpleURLLoader> simple_url_loader_;

  // The current OAuth2 token fetcher.
  std::unique_ptr<identity::PrimaryAccountAccessTokenFetcher> token_fetcher_;

  // The OAuth2 token, or empty if not fetched.
  std::string access_token_;

  // Denotes incognito mode.
  bool is_off_the_record_;

  // True if |request_| has already retried due to a 401 response from the
  // server.
  bool has_retried_authorization_;

  base::WeakPtrFactory<PaymentsClient> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(PaymentsClient);
};

}  // namespace payments
}  // namespace autofill

#endif  // COMPONENTS_AUTOFILL_CORE_BROWSER_PAYMENTS_PAYMENTS_CLIENT_H_
