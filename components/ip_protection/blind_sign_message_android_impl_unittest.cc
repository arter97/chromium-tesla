// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/ip_protection/blind_sign_message_android_impl.h"

#include <memory>
#include <string>
#include <string_view>

#include "base/memory/raw_ptr.h"
#include "base/test/protobuf_matchers.h"
#include "base/test/task_environment.h"
#include "base/test/test_future.h"
#include "components/ip_protection/android_auth_client_lib/cpp/ip_protection_auth_client.h"
#include "components/ip_protection/android_auth_client_lib/cpp/ip_protection_auth_client_interface.h"
#include "components/ip_protection/blind_sign_message_android_impl.h"
#include "net/base/features.h"
#include "net/third_party/quiche/src/quiche/blind_sign_auth/proto/auth_and_sign.pb.h"
#include "net/third_party/quiche/src/quiche/blind_sign_auth/proto/get_initial_data.pb.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/abseil-cpp/absl/status/status.h"
#include "third_party/abseil-cpp/absl/status/statusor.h"

using ::base::test::EqualsProto;
using ::testing::_;
using ::testing::Not;
using ::testing::Property;

namespace {

class MockIpProtectionAuthClient
    : public ip_protection::android::IpProtectionAuthClientInterface {
 public:
  MOCK_METHOD(
      void,
      GetInitialData,
      (const privacy::ppn::GetInitialDataRequest& request,
       base::OnceCallback<void(
           base::expected<privacy::ppn::GetInitialDataResponse, std::string>)>
           callback),
      (const, override));

  MOCK_METHOD(
      void,
      AuthAndSign,
      (const privacy::ppn::AuthAndSignRequest& request,
       base::OnceCallback<void(base::expected<privacy::ppn::AuthAndSignResponse,
                                              std::string>)> callback),
      (const override));
};
}  // anonymous namespace

class BlindSignMessageAndroidImplTest : public testing::Test {
 protected:
  void SetUp() override {
    fetcher_ = std::make_unique<BlindSignMessageAndroidImpl>();
    auto mock_ip_protection_auth_client =
        std::make_unique<MockIpProtectionAuthClient>();
    mock_ip_protection_auth_client_ = mock_ip_protection_auth_client.get();
    fetcher_->SetIpProtectionAuthClientForTesting(
        std::move(mock_ip_protection_auth_client));
    get_initial_data_request.ParseFromString(kGetInitialDataRequestBody);
    auth_and_sign_request.ParseFromString(kAuthAndSignRequestBody);
  }

 public:
  std::unique_ptr<BlindSignMessageAndroidImpl> fetcher_;
  // `mock_ip_protection_auth_client_` is owned by `fetcher_` through its
  // std::unique_ptr<ip_protection::android::IpProtectionAuthClientInterface>.
  raw_ptr<MockIpProtectionAuthClient> mock_ip_protection_auth_client_;
  const std::string kOAuthToken = "OAuth Token";
  const std::string kGetInitialDataRequestBody = "Get initial data request";
  const std::string kAuthAndSignRequestBody = "Auth and Sign request";
  privacy::ppn::GetInitialDataRequest get_initial_data_request;
  privacy::ppn::AuthAndSignRequest auth_and_sign_request;
  privacy::ppn::GetInitialDataResponse get_initial_data_response;
  privacy::ppn::AuthAndSignResponse auth_and_sign_response;
};

TEST_F(BlindSignMessageAndroidImplTest,
       DoRequestReturnsFailureStatusIfTokenProvided) {
  EXPECT_CALL(*mock_ip_protection_auth_client_,
              GetInitialData(EqualsProto(get_initial_data_request), _))
      .Times(0);
  EXPECT_CALL(*mock_ip_protection_auth_client_,
              AuthAndSign(EqualsProto(auth_and_sign_request), _))
      .Times(0);

  base::test::TestFuture<absl::StatusOr<quiche::BlindSignMessageResponse>>
      result_future;
  auto callback =
      [&result_future](
          absl::StatusOr<quiche::BlindSignMessageResponse> response) {
        result_future.SetValue(std::move(response));
      };

  fetcher_->DoRequest(quiche::BlindSignMessageRequestType::kGetInitialData,
                      kOAuthToken, kGetInitialDataRequestBody,
                      std::move(callback));

  ASSERT_FALSE(result_future.Get().ok());
  EXPECT_EQ(result_future.Get().status().code(), absl::StatusCode::kInternal);
}

TEST_F(BlindSignMessageAndroidImplTest,
       DoRequestSendsCorrectRequestForGetInitialData) {
  EXPECT_CALL(*mock_ip_protection_auth_client_,
              GetInitialData(EqualsProto(get_initial_data_request), _))
      .Times(1)
      .WillOnce([this](const privacy::ppn::GetInitialDataRequest& request,
                       auto&& callback) {
        base::expected<privacy::ppn::GetInitialDataResponse, std::string>
            response(get_initial_data_response);
        std::move(callback).Run(std::move(response));
      });
  EXPECT_CALL(*mock_ip_protection_auth_client_,
              AuthAndSign(EqualsProto(auth_and_sign_request), _))
      .Times(0);

  base::test::TestFuture<absl::StatusOr<quiche::BlindSignMessageResponse>>
      result_future;
  auto callback =
      [&result_future](
          absl::StatusOr<quiche::BlindSignMessageResponse> response) {
        result_future.SetValue(std::move(response));
      };

  fetcher_->DoRequest(quiche::BlindSignMessageRequestType::kGetInitialData,
                      std::nullopt, kGetInitialDataRequestBody,
                      std::move(callback));

  ASSERT_TRUE(result_future.Get().ok());
  EXPECT_EQ(result_future.Get()->status_code(), absl::StatusCode::kOk);
}

TEST_F(BlindSignMessageAndroidImplTest,
       DoRequestSendsCorrectRequestForAuthAndSign) {
  EXPECT_CALL(*mock_ip_protection_auth_client_,
              GetInitialData(EqualsProto(get_initial_data_request), _))
      .Times(0);
  EXPECT_CALL(*mock_ip_protection_auth_client_,
              AuthAndSign(EqualsProto(auth_and_sign_request), _))
      .Times(1)
      .WillOnce([this](const privacy::ppn::AuthAndSignRequest& request,
                       auto&& callback) {
        base::expected<privacy::ppn::AuthAndSignResponse, std::string> response(
            auth_and_sign_response);
        std::move(callback).Run(std::move(response));
      });

  base::test::TestFuture<absl::StatusOr<quiche::BlindSignMessageResponse>>
      result_future;
  auto callback =
      [&result_future](
          absl::StatusOr<quiche::BlindSignMessageResponse> response) {
        result_future.SetValue(std::move(response));
      };

  fetcher_->DoRequest(quiche::BlindSignMessageRequestType::kAuthAndSign,
                      std::nullopt, kAuthAndSignRequestBody,
                      std::move(callback));

  ASSERT_TRUE(result_future.Get().ok());
  EXPECT_EQ(result_future.Get()->status_code(), absl::StatusCode::kOk);
}

TEST_F(BlindSignMessageAndroidImplTest,
       RequestsAreQueuedUntilConnectedInstance) {
  // Reset `ip_protection_auth_client_` and skip trying to create a connected
  // instance when making a request.
  fetcher_->SetIpProtectionAuthClientForTesting(nullptr);
  fetcher_->SkipCreateConnectedInstanceForTesting();

  base::test::TestFuture<absl::StatusOr<quiche::BlindSignMessageResponse>>
      get_initial_data_result_future;
  auto get_initial_data_callback =
      [&get_initial_data_result_future](
          absl::StatusOr<quiche::BlindSignMessageResponse> response) {
        get_initial_data_result_future.SetValue(std::move(response));
      };

  fetcher_->DoRequest(quiche::BlindSignMessageRequestType::kGetInitialData,
                      std::nullopt, kGetInitialDataRequestBody,
                      std::move(get_initial_data_callback));
  ASSERT_TRUE(fetcher_->GetPendingRequestsForTesting().size() == 1u);

  base::test::TestFuture<absl::StatusOr<quiche::BlindSignMessageResponse>>
      auth_and_sign_result_future;
  auto auth_and_sign_callback =
      [&auth_and_sign_result_future](
          absl::StatusOr<quiche::BlindSignMessageResponse> response) {
        auth_and_sign_result_future.SetValue(std::move(response));
      };

  fetcher_->DoRequest(quiche::BlindSignMessageRequestType::kAuthAndSign,
                      std::nullopt, kAuthAndSignRequestBody,
                      std::move(auth_and_sign_callback));
  ASSERT_TRUE(fetcher_->GetPendingRequestsForTesting().size() == 2u);

  // Initialize `ip_protection_auth_client_` with new mock client.
  auto mock_ip_protection_auth_client =
      std::make_unique<MockIpProtectionAuthClient>();
  mock_ip_protection_auth_client_ = mock_ip_protection_auth_client.get();

  EXPECT_CALL(*mock_ip_protection_auth_client_,
              GetInitialData(EqualsProto(get_initial_data_request), _))
      .Times(1)
      .WillOnce([this](const privacy::ppn::GetInitialDataRequest& request,
                       auto&& callback) {
        base::expected<privacy::ppn::GetInitialDataResponse, std::string>
            response(get_initial_data_response);
        std::move(callback).Run(std::move(response));
      });
  EXPECT_CALL(*mock_ip_protection_auth_client_,
              AuthAndSign(EqualsProto(auth_and_sign_request), _))
      .Times(1)
      .WillOnce([this](const privacy::ppn::AuthAndSignRequest& request,
                       auto&& callback) {
        base::expected<privacy::ppn::AuthAndSignResponse, std::string> response(
            auth_and_sign_response);
        std::move(callback).Run(std::move(response));
      });

  // Finish create connected instance request and verify we process pending
  // requests.
  fetcher_->OnCreateIpProtectionAuthClientComplete(
      std::move(mock_ip_protection_auth_client));

  ASSERT_TRUE(fetcher_->GetPendingRequestsForTesting().size() == 0u);
  ASSERT_TRUE(get_initial_data_result_future.Get().ok());
  ASSERT_TRUE(auth_and_sign_result_future.Get().ok());
  EXPECT_EQ(get_initial_data_result_future.Get()->status_code(),
            absl::StatusCode::kOk);
  EXPECT_EQ(auth_and_sign_result_future.Get()->status_code(),
            absl::StatusCode::kOk);
}
