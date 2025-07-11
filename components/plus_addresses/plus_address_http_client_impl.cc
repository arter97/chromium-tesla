// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/plus_addresses/plus_address_http_client_impl.h"

#include <concepts>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/json/json_writer.h"
#include "base/sequence_checker.h"
#include "base/strings/strcat.h"
#include "components/plus_addresses/features.h"
#include "components/plus_addresses/metrics/plus_address_metrics.h"
#include "components/plus_addresses/plus_address_parsing_utils.h"
#include "components/plus_addresses/plus_address_types.h"
#include "components/signin/public/base/consent_level.h"
#include "components/signin/public/identity_manager/access_token_info.h"
#include "components/signin/public/identity_manager/identity_manager.h"
#include "components/signin/public/identity_manager/primary_account_access_token_fetcher.h"
#include "components/signin/public/identity_manager/scope_set.h"
#include "google_apis/gaia/google_service_auth_error.h"
#include "net/http/http_status_code.h"
#include "services/data_decoder/public/cpp/data_decoder.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "services/network/public/mojom/url_response_head.mojom.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace plus_addresses {

namespace {

constexpr base::TimeDelta kRequestTimeout = base::Seconds(5);
constexpr auto kSignoutError =
    PlusAddressRequestError(PlusAddressRequestErrorType::kUserSignedOut);

// See docs/network_traffic_annotations.md for reference.
// TODO(b/295556954): Update the description and trigger fields when possible.
//                    Also replace the policy_exception when we have a policy.
constexpr net::NetworkTrafficAnnotationTag kReservePlusAddressAnnotation =
    net::DefineNetworkTrafficAnnotation("plus_address_reservation", R"(
      semantics {
        sender: "Chrome Plus Address Client"
        description: "A plus address is reserved for the user on the "
                      "enterprise-specified server with this request."
        trigger: "User enters the create plus address UX flow."
        internal {
          contacts {
              email: "dc-komics@google.com"
          }
        }
        user_data {
          type: ACCESS_TOKEN,
          type: SENSITIVE_URL
        }
        data: "The origin that the user may use a plus address on is sent."
        destination: GOOGLE_OWNED_SERVICE
        last_reviewed: "2023-09-23"
      }
      policy {
        cookies_allowed: NO
        setting: "Disable the Plus Addresses feature."
        policy_exception_justification: "We don't have an opt-out policy yet"
                                        " as Plus Addresses hasn't launched."
      }
    )");

// TODO(b/277532955): Update the description and trigger fields when possible.
//                    Also replace the policy_exception when we have a policy.
constexpr net::NetworkTrafficAnnotationTag kConfirmPlusAddressAnnotation =
    net::DefineNetworkTrafficAnnotation("plus_address_confirmation", R"(
      semantics {
        sender: "Chrome Plus Address Client"
        description: "A plus address is confirmed for creation on the "
                      "enterprise-specified server with this request."
        trigger: "User confirms to create the displayed plus address."
        internal {
          contacts {
              email: "dc-komics@google.com"
          }
        }
        user_data {
          type: ACCESS_TOKEN,
          type: SENSITIVE_URL,
          type: USERNAME
        }
        data: "The plus address and the origin that the user is using it on "
              "are  both sent."
        destination: GOOGLE_OWNED_SERVICE
        last_reviewed: "2023-09-23"
      }
      policy {
        cookies_allowed: NO
        setting: "Disable the Plus Addresses feature."
        policy_exception_justification: "We don't have an opt-out policy yet"
                                        " as Plus Addresses hasn't launched."
      }
    )");

// TODO(b/295556954): Update the description and trigger fields when possible.
//                    Also replace the policy_exception when we have a policy.
constexpr net::NetworkTrafficAnnotationTag kGetAllPlusAddressesAnnotation =
    net::DefineNetworkTrafficAnnotation("get_all_plus_addresses", R"(
      semantics {
        sender: "Chrome Plus Address Client"
        description: "This request fetches all plus addresses from the "
                      "enterprise-specified server."
        trigger: "n/a. This happens in the background to keep the PlusAddress "
                 "service in sync with the remote server."
        internal {
          contacts {
              email: "dc-komics@google.com"
          }
        }
        user_data {
          type: ACCESS_TOKEN
        }
        data: "n/a"
        destination: GOOGLE_OWNED_SERVICE
        last_reviewed: "2023-09-13"
      }
      policy {
        cookies_allowed: NO
        setting: "Disable the Plus Addresses feature."
        policy_exception_justification: "We don't have an opt-out policy yet"
                                        " as Plus Addresses hasn't launched."
      }
    )");

std::optional<GURL> ValidateAndGetUrl() {
  GURL maybe_url = GURL(features::kEnterprisePlusAddressServerUrl.Get());
  return maybe_url.is_valid() ? std::make_optional(maybe_url) : std::nullopt;
}

// Returns the HTTP response code for the response in the `loader`. If there is
// none, it returns `std::nullopt`.
std::optional<int> GetResponseCode(network::SimpleURLLoader* loader) {
  return loader && loader->ResponseInfo() && loader->ResponseInfo()->headers
             ? std::optional<int>(
                   loader->ResponseInfo()->headers->response_code())
             : std::optional<int>();
}

// Helper wrapper around a `base::OnceCallback` that runs the `callback` with
// argument `arg_on_destroy` if the callback is destroyed and has not been run
// at that point in time.
template <typename T>
class RunOnDestroyHelper final {
 public:
  using Callback = base::OnceCallback<void(const T&)>;

  RunOnDestroyHelper(Callback callback, T arg_on_destroy)
      : callback_(std::move(callback)),
        arg_on_destroy_(std::move(arg_on_destroy)) {}

  RunOnDestroyHelper(const RunOnDestroyHelper&) = delete;
  RunOnDestroyHelper(RunOnDestroyHelper&&) = default;
  RunOnDestroyHelper& operator=(const RunOnDestroyHelper&) = delete;
  RunOnDestroyHelper& operator=(RunOnDestroyHelper&&) = default;

  ~RunOnDestroyHelper() {
    if (callback_) {
      std::move(callback_).Run(arg_on_destroy_);
    }
  }

  void Run(const T& arg) && {
    std::move(callback_).Run(arg);
    callback_.Reset();
  }

 private:
  Callback callback_;
  T arg_on_destroy_;
};

// Given a `base::OnceCallback<void(const T&)>` `callback`, it returns another
// `base::OnceCallback` of the same signature with the property that the
// returned callback is run with argument `arg_on_destroy` on its destruction if
// it has not been run before.
template <typename T, typename V>
  requires(std::constructible_from<T, V>)
base::OnceCallback<void(const T&)> WrapAsAutorunCallback(
    base::OnceCallback<void(const T&)> callback,
    V arg_on_destroy) {
  return base::BindOnce(
      [](RunOnDestroyHelper<T> helper, const T& profile) {
        std::move(helper).Run(profile);
      },
      RunOnDestroyHelper<T>(std::move(callback), std::move(arg_on_destroy)));
}

}  // namespace

PlusAddressHttpClientImpl::PlusAddressHttpClientImpl(
    signin::IdentityManager* identity_manager,
    scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory)
    : identity_manager_(identity_manager),
      url_loader_factory_(std::move(url_loader_factory)),
      server_url_(ValidateAndGetUrl()),
      scopes_({features::kEnterprisePlusAddressOAuthScope.Get()}) {}

PlusAddressHttpClientImpl::~PlusAddressHttpClientImpl() = default;

void PlusAddressHttpClientImpl::ReservePlusAddress(
    const url::Origin& origin,
    bool refresh,
    PlusAddressRequestCallback on_completed) {
  if (!server_url_) {
    return;
  }
  GetAuthToken(
      base::BindOnce(&PlusAddressHttpClientImpl::ReservePlusAddressInternal,
                     base::Unretained(this), origin, refresh,
                     WrapAsAutorunCallback(std::move(on_completed),
                                           base::unexpected(kSignoutError))));
}

void PlusAddressHttpClientImpl::ConfirmPlusAddress(
    const url::Origin& origin,
    const std::string& plus_address,
    PlusAddressRequestCallback on_completed) {
  if (!server_url_) {
    return;
  }
  GetAuthToken(
      base::BindOnce(&PlusAddressHttpClientImpl::ConfirmPlusAddressInternal,
                     base::Unretained(this), origin, plus_address,
                     WrapAsAutorunCallback(std::move(on_completed),
                                           base::unexpected(kSignoutError))));
}

void PlusAddressHttpClientImpl::GetAllPlusAddresses(
    PlusAddressMapRequestCallback on_completed) {
  if (!server_url_) {
    return;
  }
  GetAuthToken(
      base::BindOnce(&PlusAddressHttpClientImpl::GetAllPlusAddressesInternal,
                     base::Unretained(this),
                     WrapAsAutorunCallback(std::move(on_completed),
                                           base::unexpected(kSignoutError))));
}

void PlusAddressHttpClientImpl::Reset() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  access_token_fetcher_.reset();
  pending_callbacks_ = {};
  loaders_for_creation_.clear();
  loader_for_sync_.reset();
}

void PlusAddressHttpClientImpl::ReservePlusAddressInternal(
    const url::Origin& origin,
    bool refresh,
    PlusAddressRequestCallback on_completed,
    std::optional<std::string> auth_token) {
  if (!auth_token.has_value()) {
    std::move(on_completed)
        .Run(base::unexpected(
            PlusAddressRequestError(PlusAddressRequestErrorType::kOAuthError)));
    return;
  }
  auto resource_request = std::make_unique<network::ResourceRequest>();
  resource_request->method = net::HttpRequestHeaders::kPutMethod;
  resource_request->url =
      server_url_.value().Resolve(kServerReservePlusAddressEndpoint);
  resource_request->headers.SetHeader(
      net::HttpRequestHeaders::kAuthorization,
      base::StrCat({"Bearer ", auth_token.value()}));
  resource_request->credentials_mode = network::mojom::CredentialsMode::kOmit;

  base::Value::Dict payload;
  payload.Set("facet", origin.Serialize());
  payload.Set("refresh_email_address", refresh);
  std::string request_body;
  bool wrote_payload = base::JSONWriter::Write(payload, &request_body);
  DCHECK(wrote_payload);

  std::unique_ptr<network::SimpleURLLoader> loader =
      network::SimpleURLLoader::Create(std::move(resource_request),
                                       kReservePlusAddressAnnotation);
  network::SimpleURLLoader* loader_ptr = loader.get();
  loader_ptr->AttachStringForUpload(request_body, "application/json");
  loader_ptr->SetTimeoutDuration(kRequestTimeout);
  // TODO(b/301984623) - Measure average downloadsize and change this.
  loader_ptr->DownloadToString(
      url_loader_factory_.get(),
      base::BindOnce(
          &PlusAddressHttpClientImpl::OnReserveOrConfirmPlusAddressComplete,
          // Safe since this class owns the list of loaders.
          base::Unretained(this),
          loaders_for_creation_.insert(loaders_for_creation_.begin(),
                                       std::move(loader)),
          PlusAddressNetworkRequestType::kReserve, base::TimeTicks::Now(),
          std::move(on_completed)),
      network::SimpleURLLoader::kMaxBoundedStringDownloadSize);
}

void PlusAddressHttpClientImpl::ConfirmPlusAddressInternal(
    const url::Origin& origin,
    const std::string& plus_address,
    PlusAddressRequestCallback on_completed,
    std::optional<std::string> auth_token) {
  if (!auth_token.has_value()) {
    std::move(on_completed)
        .Run(base::unexpected(
            PlusAddressRequestError(PlusAddressRequestErrorType::kOAuthError)));
    return;
  }
  auto resource_request = std::make_unique<network::ResourceRequest>();
  resource_request->method = net::HttpRequestHeaders::kPutMethod;
  resource_request->url =
      server_url_.value().Resolve(kServerCreatePlusAddressEndpoint);
  resource_request->headers.SetHeader(
      net::HttpRequestHeaders::kAuthorization,
      base::StrCat({"Bearer ", auth_token.value()}));
  resource_request->credentials_mode = network::mojom::CredentialsMode::kOmit;

  base::Value::Dict payload;
  payload.Set("facet", origin.Serialize());
  payload.Set("reserved_email_address", plus_address);
  std::string request_body;
  bool wrote_payload = base::JSONWriter::Write(payload, &request_body);
  DCHECK(wrote_payload);

  std::unique_ptr<network::SimpleURLLoader> loader =
      network::SimpleURLLoader::Create(std::move(resource_request),
                                       kConfirmPlusAddressAnnotation);
  network::SimpleURLLoader* loader_ptr = loader.get();
  loader_ptr->AttachStringForUpload(request_body, "application/json");
  loader_ptr->SetTimeoutDuration(kRequestTimeout);
  // TODO(b/301984623) - Measure average downloadsize and change this.
  loader_ptr->DownloadToString(
      url_loader_factory_.get(),
      base::BindOnce(
          &PlusAddressHttpClientImpl::OnReserveOrConfirmPlusAddressComplete,
          // Safe since this class owns the list of loaders.
          base::Unretained(this),
          loaders_for_creation_.insert(loaders_for_creation_.begin(),
                                       std::move(loader)),
          PlusAddressNetworkRequestType::kCreate, base::TimeTicks::Now(),
          std::move(on_completed)),
      network::SimpleURLLoader::kMaxBoundedStringDownloadSize);
}

void PlusAddressHttpClientImpl::GetAllPlusAddressesInternal(
    PlusAddressMapRequestCallback on_completed,
    std::optional<std::string> auth_token) {
  if (!auth_token.has_value()) {
    return;
  }

  auto resource_request = std::make_unique<network::ResourceRequest>();
  resource_request->method = net::HttpRequestHeaders::kGetMethod;
  resource_request->url =
      server_url_.value().Resolve(kServerPlusProfileEndpoint);
  resource_request->headers.SetHeader(
      net::HttpRequestHeaders::kAuthorization,
      base::StrCat({"Bearer ", auth_token.value()}));
  resource_request->credentials_mode = network::mojom::CredentialsMode::kOmit;

  loader_for_sync_ = network::SimpleURLLoader::Create(
      std::move(resource_request), kGetAllPlusAddressesAnnotation);
  loader_for_sync_->SetTimeoutDuration(kRequestTimeout);
  loader_for_sync_->DownloadToString(
      url_loader_factory_.get(),
      base::BindOnce(&PlusAddressHttpClientImpl::OnGetAllPlusAddressesComplete,
                     // Safe since this class owns the loader_for_sync_.
                     base::Unretained(this), base::TimeTicks::Now(),
                     std::move(on_completed)),
      // TODO(b/301984623) - Measure average download size and change this.
      network::SimpleURLLoader::kMaxBoundedStringDownloadSize);
}

void PlusAddressHttpClientImpl::OnReserveOrConfirmPlusAddressComplete(
    UrlLoaderList::iterator it,
    PlusAddressNetworkRequestType type,
    base::TimeTicks request_start,
    PlusAddressRequestCallback on_completed,
    std::unique_ptr<std::string> response) {
  // Record relevant metrics.
  std::unique_ptr<network::SimpleURLLoader> loader = std::move(*it);
  metrics::RecordNetworkRequestLatency(type,
                                       base::TimeTicks::Now() - request_start);
  std::optional<int> response_code = GetResponseCode(loader.get());
  if (response_code) {
    metrics::RecordNetworkRequestResponseCode(type, *response_code);
  }
  // Destroy the loader before returning.
  loaders_for_creation_.erase(it);
  if (!response) {
    std::move(on_completed)
        .Run(base::unexpected(
            PlusAddressRequestError::AsNetworkError(response_code)));
    return;
  }
  metrics::RecordNetworkRequestResponseSize(type, response->size());
  // Parse the response & return it via callback.
  data_decoder::DataDecoder::ParseJsonIsolated(
      *response,
      base::BindOnce(&ParsePlusProfileFromV1Create)
          .Then(base::BindOnce(
              [](PlusAddressRequestCallback callback,
                 std::optional<PlusProfile> result) {
                if (!result.has_value()) {
                  std::move(callback).Run(
                      base::unexpected(PlusAddressRequestError(
                          PlusAddressRequestErrorType::kParsingError)));
                  return;
                }
                std::move(callback).Run(result.value());
              },
              std::move(on_completed))));
}

void PlusAddressHttpClientImpl::OnGetAllPlusAddressesComplete(
    base::TimeTicks request_start,
    PlusAddressMapRequestCallback on_completed,
    std::unique_ptr<std::string> response) {
  // Record relevant metrics.
  metrics::RecordNetworkRequestLatency(PlusAddressNetworkRequestType::kList,
                                       base::TimeTicks::Now() - request_start);
  std::optional<int> response_code = GetResponseCode(loader_for_sync_.get());
  if (response_code) {
    metrics::RecordNetworkRequestResponseCode(
        PlusAddressNetworkRequestType::kList, *response_code);
  }
  // Destroy the loader before returning.
  loader_for_sync_.reset();
  if (!response) {
    std::move(on_completed)
        .Run(base::unexpected(
            PlusAddressRequestError::AsNetworkError(response_code)));
    return;
  }
  metrics::RecordNetworkRequestResponseSize(
      PlusAddressNetworkRequestType::kList, response->size());
  // Parse the response & return it via callback.
  data_decoder::DataDecoder::ParseJsonIsolated(
      *response,
      base::BindOnce(&ParsePlusAddressMapFromV1List)
          .Then(base::BindOnce(
              [](PlusAddressMapRequestCallback callback,
                 std::optional<PlusAddressMap> result) {
                if (!result.has_value()) {
                  std::move(callback).Run(
                      base::unexpected(PlusAddressRequestError(
                          PlusAddressRequestErrorType::kParsingError)));
                  return;
                }
                std::move(callback).Run(result.value());
              },
              std::move(on_completed))));
}

void PlusAddressHttpClientImpl::GetAuthToken(TokenReadyCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (access_token_fetcher_) {
    pending_callbacks_.emplace(std::move(callback));
    return;
  }
  access_token_fetcher_ =
      std::make_unique<signin::PrimaryAccountAccessTokenFetcher>(
          /*consumer_name=*/"PlusAddressHttpClientImpl", identity_manager_, scopes_,
          base::BindOnce(&PlusAddressHttpClientImpl::OnTokenFetched,
                         // It is safe to use base::Unretained as
                         // |this| owns |access_token_fetcher_|.
                         base::Unretained(this), std::move(callback)),
          // Use WaitUntilAvailable to defer getting an OAuth token until
          // the user is signed in. We can switch to kImmediate once we
          // have a sign in observer that guarantees we're already signed in
          // by this point.
          signin::PrimaryAccountAccessTokenFetcher::Mode::kWaitUntilAvailable,
          // Sync doesn't need to be enabled for us to use PlusAddresses.
          signin::ConsentLevel::kSignin);
}

void PlusAddressHttpClientImpl::OnTokenFetched(
    TokenReadyCallback callback,
    GoogleServiceAuthError error,
    signin::AccessTokenInfo access_token_info) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  access_token_fetcher_.reset();
  metrics::RecordNetworkRequestOauthError(error);
  std::optional<std::string> access_token;
  if (error.state() == GoogleServiceAuthError::NONE) {
    access_token = access_token_info.token;
  }
  std::move(callback).Run(access_token);
  while (!pending_callbacks_.empty()) {
    std::move(pending_callbacks_.front()).Run(access_token);
    pending_callbacks_.pop();
  }
}

}  // namespace plus_addresses
