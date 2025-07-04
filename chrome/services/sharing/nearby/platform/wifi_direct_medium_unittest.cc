// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/services/sharing/nearby/platform/wifi_direct_medium.h"

#include <netinet/in.h>

#include "base/rand_util.h"
#include "base/task/thread_pool.h"
#include "base/test/task_environment.h"
#include "chrome/services/sharing/nearby/platform/wifi_direct_server_socket.h"
#include "chromeos/ash/services/nearby/public/cpp/fake_firewall_hole_factory.h"
#include "mojo/public/cpp/bindings/self_owned_receiver.h"
#include "mojo/public/cpp/bindings/shared_remote.h"
#include "net/log/net_log_source.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {

constexpr char kTestIPv4Address[] = "127.0.0.1";
constexpr uint16_t kMinPort = 50000;  // Arbitrary port.
constexpr uint16_t kMaxPort = 65535;
constexpr char kTestSSID[] = "DIRECT-xx";
constexpr char kTestPassword[] = "ABCD1234";

// Pick a random port for each test run, otherwise the `Listen` call has a
// chance to return ADDRESS_IN_USE(-147).
int RandomPort() {
  return static_cast<uint16_t>(kMinPort +
                               base::RandGenerator(kMaxPort - kMinPort + 1));
}

class FakeWifiDirectConnection
    : public ash::wifi_direct::mojom::WifiDirectConnection {
 public:
  bool did_associate;
  bool should_associate;
  std::string ipv4_address = kTestIPv4Address;

 private:
  // ash::wifi_direct::mojom::WifiDirectConnection
  void GetProperties(GetPropertiesCallback callback) override {
    auto properties =
        ash::wifi_direct::mojom::WifiDirectConnectionProperties::New();
    properties->ipv4_address = ipv4_address;
    properties->credentials = ash::wifi_direct::mojom::WifiCredentials::New();
    properties->credentials->ssid = kTestSSID;
    properties->credentials->passphrase = kTestPassword;
    std::move(callback).Run(std::move(properties));
  }

  // ash::wifi_direct::mojom::WifiDirectConnection
  void AssociateSocket(::mojo::PlatformHandle handle,
                       AssociateSocketCallback callback) override {
    did_associate = should_associate;
    std::move(callback).Run(should_associate);
  }
};

class FakeWifiDirectManager
    : public ash::wifi_direct::mojom::WifiDirectManager {
 public:
  FakeWifiDirectManager() {}

  ~FakeWifiDirectManager() override {}

  void CreateWifiDirectGroup(
      ash::wifi_direct::mojom::WifiCredentialsPtr credentials,
      CreateWifiDirectGroupCallback callback) override {
    ReturnConnection(std::move(callback));
  }

  void ConnectToWifiDirectGroup(
      ash::wifi_direct::mojom::WifiCredentialsPtr credentials,
      std::optional<uint32_t> frequency,
      ConnectToWifiDirectGroupCallback callback) override {
    EXPECT_EQ(credentials->ssid, expected_ssid_);
    EXPECT_EQ(credentials->passphrase, expected_password_);
    ReturnConnection(std::move(callback));
  }

  void GetWifiP2PCapabilities(
      GetWifiP2PCapabilitiesCallback callback) override {
    auto response = ash::wifi_direct::mojom::WifiP2PCapabilities::New();
    response->is_p2p_supported = is_interface_valid_;
    std::move(callback).Run(std::move(response));
  }

  FakeWifiDirectConnection* SetWifiDirectConnection(
      std::unique_ptr<FakeWifiDirectConnection> connection) {
    connection_ = std::move(connection);
    return connection_.get();
  }

  void SetIsInterfaceValid(bool is_valid) { is_interface_valid_ = is_valid; }

  void SetExpectedCredentials(const std::string& ssid,
                              const std::string& password) {
    expected_ssid_ = ssid;
    expected_password_ = password;
  }

 private:
  void ReturnConnection(ConnectToWifiDirectGroupCallback callback) {
    if (!connection_) {
      std::move(callback).Run(
          ash::wifi_direct::mojom::WifiDirectOperationResult::kNotSupported,
          mojo::NullRemote());
    } else {
      mojo::PendingRemote<ash::wifi_direct::mojom::WifiDirectConnection>
          connection_remote;
      mojo::MakeSelfOwnedReceiver(
          std::move(connection_),
          connection_remote.InitWithNewPipeAndPassReceiver());
      std::move(callback).Run(
          ash::wifi_direct::mojom::WifiDirectOperationResult::kSuccess,
          std::move(connection_remote));
    }
  }

  std::string expected_ssid_ = "";
  std::string expected_password_ = "";
  std::unique_ptr<FakeWifiDirectConnection> connection_;
  bool is_interface_valid_ = true;
};

}  // namespace

namespace nearby::chrome {

class WifiDirectMediumTest : public ::testing::Test {
 public:
  // ::testing::Test
  void SetUp() override {
    // Set up WiFi Direct mojo service.
    auto wifi_direct_manager = std::make_unique<FakeWifiDirectManager>();
    wifi_direct_manager_ = wifi_direct_manager.get();
    mojo::MakeSelfOwnedReceiver(
        std::move(wifi_direct_manager),
        wifi_direct_manager_remote_.BindNewPipeAndPassReceiver());

    // Set up firewall hole factory mojo service.
    auto firewall_hole_factory =
        std::make_unique<ash::nearby::FakeFirewallHoleFactory>();
    firewall_hole_factory_ = firewall_hole_factory.get();
    mojo::MakeSelfOwnedReceiver(
        std::move(firewall_hole_factory),
        firewall_hole_factory_remote_.BindNewPipeAndPassReceiver());

    // Create the subject under test.
    medium_ = std::make_unique<WifiDirectMedium>(wifi_direct_manager_remote_,
                                                 firewall_hole_factory_remote_);
  }

  void AcceptSocket(int port) {
    auto ip_endpoint = net::IPEndPoint(
        net::IPAddress::FromIPLiteral(kTestIPv4Address).value(), port);
    server_socket_ =
        std::make_unique<net::TCPServerSocket>(nullptr, net::NetLogSource());

    auto fd = base::ScopedFD(
        net::CreatePlatformSocket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
    CHECK(fd.get() > 0);
    server_socket_->AdoptSocket(fd.release());

    std::optional<net::IPAddress> address =
        net::IPAddress::FromIPLiteral(kTestIPv4Address);
    CHECK(address);
    int listen_result =
        server_socket_->Listen(net::IPEndPoint(*address, port), 4,
                               /*ipv6_only=*/std::nullopt);
    CHECK(listen_result == net::OK);

    int result = server_socket_->Accept(
        &accepted_socket_, base::BindOnce(&WifiDirectMediumTest::OnAccept,
                                          base::Unretained(this)));
    if (result != net::ERR_IO_PENDING) {
      OnAccept(result);
    }
  }

  void OnAccept(int result) {
    EXPECT_EQ(result, net::OK);
    accepted_socket_->Disconnect();
  }

  WifiDirectMedium* medium() { return medium_.get(); }
  FakeWifiDirectManager* manager() { return wifi_direct_manager_; }
  ash::nearby::FakeFirewallHoleFactory* firewall_hole_factory() {
    return firewall_hole_factory_;
  }

  void RunOnTaskRunner(base::OnceClosure task) {
    base::RunLoop run_loop;
    base::ThreadPool::CreateSequencedTaskRunner({base::MayBlock()})
        ->PostTaskAndReply(FROM_HERE, std::move(task), run_loop.QuitClosure());
    run_loop.Run();
  }

 private:
  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::MainThreadType::IO};
  raw_ptr<FakeWifiDirectManager> wifi_direct_manager_;
  mojo::SharedRemote<ash::wifi_direct::mojom::WifiDirectManager>
      wifi_direct_manager_remote_;
  raw_ptr<ash::nearby::FakeFirewallHoleFactory> firewall_hole_factory_;
  mojo::SharedRemote<::sharing::mojom::FirewallHoleFactory>
      firewall_hole_factory_remote_;
  std::unique_ptr<WifiDirectMedium> medium_;
  std::unique_ptr<net::TCPServerSocket> server_socket_;
  std::unique_ptr<net::StreamSocket> accepted_socket_;
};

TEST_F(WifiDirectMediumTest, IsInterfaceValid_Valid) {
  manager()->SetIsInterfaceValid(true);
  RunOnTaskRunner(base::BindOnce(
      [](WifiDirectMedium* medium) {
        base::ScopedAllowBaseSyncPrimitivesForTesting allow;
        EXPECT_TRUE(medium->IsInterfaceValid());
      },
      medium()));
}

TEST_F(WifiDirectMediumTest, IsInterfaceValid_Invalid) {
  manager()->SetIsInterfaceValid(false);
  RunOnTaskRunner(base::BindOnce(
      [](WifiDirectMedium* medium) {
        base::ScopedAllowBaseSyncPrimitivesForTesting allow;
        EXPECT_FALSE(medium->IsInterfaceValid());
      },
      medium()));
}

TEST_F(WifiDirectMediumTest, StartWifiDirect_MissingConnection) {
  manager()->SetWifiDirectConnection(nullptr);
  RunOnTaskRunner(base::BindOnce(
      [](WifiDirectMedium* medium) {
        base::ScopedAllowBaseSyncPrimitivesForTesting allow;
        WifiDirectCredentials credentials;
        EXPECT_FALSE(medium->StartWifiDirect(&credentials));
      },
      medium()));
}

TEST_F(WifiDirectMediumTest, StartWifiDirect_ValidConnection) {
  manager()->SetWifiDirectConnection(
      std::make_unique<FakeWifiDirectConnection>());
  RunOnTaskRunner(base::BindOnce(
      [](WifiDirectMedium* medium) {
        base::ScopedAllowBaseSyncPrimitivesForTesting allow;
        WifiDirectCredentials credentials;
        EXPECT_TRUE(medium->StartWifiDirect(&credentials));
        EXPECT_EQ(credentials.GetIPAddress(), kTestIPv4Address);
        EXPECT_EQ(credentials.GetGateway(), kTestIPv4Address);
        EXPECT_EQ(credentials.GetSSID(), kTestSSID);
        EXPECT_EQ(credentials.GetPassword(), kTestPassword);
      },
      medium()));
}

TEST_F(WifiDirectMediumTest, StopWifiDirect_MissingConnection) {
  manager()->SetWifiDirectConnection(nullptr);

  RunOnTaskRunner(base::BindOnce(
      [](WifiDirectMedium* medium) {
        base::ScopedAllowBaseSyncPrimitivesForTesting allow;
        WifiDirectCredentials credentials;
        EXPECT_FALSE(medium->StartWifiDirect(&credentials));
      },
      medium()));

  RunOnTaskRunner(base::BindOnce(
      [](WifiDirectMedium* medium) {
        base::ScopedAllowBaseSyncPrimitivesForTesting allow;
        EXPECT_FALSE(medium->StopWifiDirect());
      },
      medium()));
}

TEST_F(WifiDirectMediumTest, StopWifiDirect_ExistingConnection) {
  manager()->SetWifiDirectConnection(
      std::make_unique<FakeWifiDirectConnection>());

  RunOnTaskRunner(base::BindOnce(
      [](WifiDirectMedium* medium) {
        base::ScopedAllowBaseSyncPrimitivesForTesting allow;
        WifiDirectCredentials credentials;
        EXPECT_TRUE(medium->StartWifiDirect(&credentials));
      },
      medium()));

  RunOnTaskRunner(base::BindOnce(
      [](WifiDirectMedium* medium) {
        base::ScopedAllowBaseSyncPrimitivesForTesting allow;
        EXPECT_TRUE(medium->StopWifiDirect());
      },
      medium()));
}

TEST_F(WifiDirectMediumTest, ConnectWifiDirect_MissingConnection) {
  manager()->SetWifiDirectConnection(nullptr);
  manager()->SetExpectedCredentials(kTestSSID, kTestPassword);
  RunOnTaskRunner(base::BindOnce(
      [](WifiDirectMedium* medium) {
        base::ScopedAllowBaseSyncPrimitivesForTesting allow;
        WifiDirectCredentials credentials;
        credentials.SetSSID(kTestSSID);
        credentials.SetPassword(kTestPassword);
        EXPECT_FALSE(medium->ConnectWifiDirect(&credentials));
      },
      medium()));
}

TEST_F(WifiDirectMediumTest, ConnectWifiDirect_ValidConnection) {
  manager()->SetWifiDirectConnection(
      std::make_unique<FakeWifiDirectConnection>());
  manager()->SetExpectedCredentials(kTestSSID, kTestPassword);
  RunOnTaskRunner(base::BindOnce(
      [](WifiDirectMedium* medium) {
        base::ScopedAllowBaseSyncPrimitivesForTesting allow;
        WifiDirectCredentials credentials;
        credentials.SetSSID(kTestSSID);
        credentials.SetPassword(kTestPassword);
        EXPECT_TRUE(medium->ConnectWifiDirect(&credentials));
      },
      medium()));
}

TEST_F(WifiDirectMediumTest, DisconnectWifiDirect_MissingConnection) {
  manager()->SetWifiDirectConnection(nullptr);

  RunOnTaskRunner(base::BindOnce(
      [](WifiDirectMedium* medium) {
        base::ScopedAllowBaseSyncPrimitivesForTesting allow;
        WifiDirectCredentials credentials;
        EXPECT_FALSE(medium->ConnectWifiDirect(&credentials));
      },
      medium()));

  RunOnTaskRunner(base::BindOnce(
      [](WifiDirectMedium* medium) {
        base::ScopedAllowBaseSyncPrimitivesForTesting allow;
        EXPECT_FALSE(medium->DisconnectWifiDirect());
      },
      medium()));
}

TEST_F(WifiDirectMediumTest, DisconnectWifiDirect_ExistingConnection) {
  manager()->SetWifiDirectConnection(
      std::make_unique<FakeWifiDirectConnection>());
  manager()->SetExpectedCredentials(kTestSSID, kTestPassword);

  RunOnTaskRunner(base::BindOnce(
      [](WifiDirectMedium* medium) {
        base::ScopedAllowBaseSyncPrimitivesForTesting allow;
        WifiDirectCredentials credentials;
        credentials.SetSSID(kTestSSID);
        credentials.SetPassword(kTestPassword);
        EXPECT_TRUE(medium->ConnectWifiDirect(&credentials));
      },
      medium()));

  RunOnTaskRunner(base::BindOnce(
      [](WifiDirectMedium* medium) {
        base::ScopedAllowBaseSyncPrimitivesForTesting allow;
        EXPECT_TRUE(medium->DisconnectWifiDirect());
      },
      medium()));
}

TEST_F(WifiDirectMediumTest, ConnectToService_Success) {
  auto* connection = manager()->SetWifiDirectConnection(
      std::make_unique<FakeWifiDirectConnection>());
  connection->should_associate = true;
  manager()->SetExpectedCredentials(kTestSSID, kTestPassword);

  RunOnTaskRunner(base::BindOnce(
      [](WifiDirectMedium* medium) {
        base::ScopedAllowBaseSyncPrimitivesForTesting allow;
        WifiDirectCredentials credentials;
        credentials.SetSSID(kTestSSID);
        credentials.SetPassword(kTestPassword);
        EXPECT_TRUE(medium->ConnectWifiDirect(&credentials));
      },
      medium()));

  int port = RandomPort();
  AcceptSocket(port);
  RunOnTaskRunner(base::BindOnce(
      [](WifiDirectMedium* medium, int port) {
        base::ScopedAllowBaseSyncPrimitivesForTesting allow;
        CancellationFlag cancellation_flag;
        EXPECT_TRUE(medium->ConnectToService(kTestIPv4Address, port,
                                             &cancellation_flag));
      },
      medium(), port));
}

TEST_F(WifiDirectMediumTest, ConnectToService_MissingConnection) {
  manager()->SetWifiDirectConnection(nullptr);
  manager()->SetExpectedCredentials(kTestSSID, kTestPassword);

  RunOnTaskRunner(base::BindOnce(
      [](WifiDirectMedium* medium) {
        base::ScopedAllowBaseSyncPrimitivesForTesting allow;
        WifiDirectCredentials credentials;
        credentials.SetSSID(kTestSSID);
        credentials.SetPassword(kTestPassword);
        EXPECT_FALSE(medium->ConnectWifiDirect(&credentials));
      },
      medium()));

  int port = RandomPort();
  AcceptSocket(port);
  RunOnTaskRunner(base::BindOnce(
      [](WifiDirectMedium* medium, int port) {
        base::ScopedAllowBaseSyncPrimitivesForTesting allow;
        CancellationFlag cancellation_flag;
        EXPECT_FALSE(medium->ConnectToService(kTestIPv4Address, port,
                                              &cancellation_flag));
      },
      medium(), port));
}

TEST_F(WifiDirectMediumTest, ConnectToService_FailToAssociatesSocket) {
  auto* connection = manager()->SetWifiDirectConnection(
      std::make_unique<FakeWifiDirectConnection>());
  connection->should_associate = false;
  manager()->SetExpectedCredentials(kTestSSID, kTestPassword);

  RunOnTaskRunner(base::BindOnce(
      [](WifiDirectMedium* medium) {
        base::ScopedAllowBaseSyncPrimitivesForTesting allow;
        WifiDirectCredentials credentials;
        credentials.SetSSID(kTestSSID);
        credentials.SetPassword(kTestPassword);
        EXPECT_TRUE(medium->ConnectWifiDirect(&credentials));
      },
      medium()));

  int port = RandomPort();
  AcceptSocket(port);
  RunOnTaskRunner(base::BindOnce(
      [](WifiDirectMedium* medium, int port) {
        base::ScopedAllowBaseSyncPrimitivesForTesting allow;
        CancellationFlag cancellation_flag;
        EXPECT_FALSE(medium->ConnectToService(kTestIPv4Address, port,
                                              &cancellation_flag));
      },
      medium(), port));
}

TEST_F(WifiDirectMediumTest, ListenForService_Success) {
  auto* connection = manager()->SetWifiDirectConnection(
      std::make_unique<FakeWifiDirectConnection>());
  connection->should_associate = true;
  firewall_hole_factory()->should_succeed_ = true;

  RunOnTaskRunner(base::BindOnce(
      [](WifiDirectMedium* medium) {
        base::ScopedAllowBaseSyncPrimitivesForTesting allow;
        WifiDirectCredentials credentials;
        EXPECT_TRUE(medium->StartWifiDirect(&credentials));
      },
      medium()));

  RunOnTaskRunner(base::BindOnce(
      [](WifiDirectMedium* medium) {
        base::ScopedAllowBaseSyncPrimitivesForTesting allow;
        EXPECT_TRUE(medium->ListenForService(RandomPort()));
      },
      medium()));

  EXPECT_TRUE(connection->did_associate);
}

TEST_F(WifiDirectMediumTest, ListenForService_MissingConnection) {
  manager()->SetWifiDirectConnection(nullptr);
  firewall_hole_factory()->should_succeed_ = true;

  RunOnTaskRunner(base::BindOnce(
      [](WifiDirectMedium* medium) {
        base::ScopedAllowBaseSyncPrimitivesForTesting allow;
        WifiDirectCredentials credentials;
        EXPECT_FALSE(medium->StartWifiDirect(&credentials));
      },
      medium()));

  RunOnTaskRunner(base::BindOnce(
      [](WifiDirectMedium* medium) {
        base::ScopedAllowBaseSyncPrimitivesForTesting allow;
        EXPECT_FALSE(medium->ListenForService(RandomPort()));
      },
      medium()));
}

TEST_F(WifiDirectMediumTest, ListenForService_FailToAssociatesSocket) {
  auto* connection = manager()->SetWifiDirectConnection(
      std::make_unique<FakeWifiDirectConnection>());
  connection->should_associate = false;
  firewall_hole_factory()->should_succeed_ = true;

  RunOnTaskRunner(base::BindOnce(
      [](WifiDirectMedium* medium) {
        base::ScopedAllowBaseSyncPrimitivesForTesting allow;
        WifiDirectCredentials credentials;
        EXPECT_TRUE(medium->StartWifiDirect(&credentials));
      },
      medium()));

  RunOnTaskRunner(base::BindOnce(
      [](WifiDirectMedium* medium) {
        base::ScopedAllowBaseSyncPrimitivesForTesting allow;
        EXPECT_FALSE(medium->ListenForService(RandomPort()));
      },
      medium()));
}

TEST_F(WifiDirectMediumTest, ListenForService_FailToOpenFirewallHole) {
  auto* connection = manager()->SetWifiDirectConnection(
      std::make_unique<FakeWifiDirectConnection>());
  connection->should_associate = true;
  firewall_hole_factory()->should_succeed_ = false;

  RunOnTaskRunner(base::BindOnce(
      [](WifiDirectMedium* medium) {
        base::ScopedAllowBaseSyncPrimitivesForTesting allow;
        WifiDirectCredentials credentials;
        EXPECT_TRUE(medium->StartWifiDirect(&credentials));
      },
      medium()));

  RunOnTaskRunner(base::BindOnce(
      [](WifiDirectMedium* medium) {
        base::ScopedAllowBaseSyncPrimitivesForTesting allow;
        EXPECT_FALSE(medium->ListenForService(RandomPort()));
      },
      medium()));
}

TEST_F(WifiDirectMediumTest, ListenForService_InvalidAddress) {
  auto* connection = manager()->SetWifiDirectConnection(
      std::make_unique<FakeWifiDirectConnection>());
  connection->should_associate = true;
  connection->ipv4_address = "nope";
  firewall_hole_factory()->should_succeed_ = true;

  RunOnTaskRunner(base::BindOnce(
      [](WifiDirectMedium* medium) {
        base::ScopedAllowBaseSyncPrimitivesForTesting allow;
        WifiDirectCredentials credentials;
        EXPECT_TRUE(medium->StartWifiDirect(&credentials));
      },
      medium()));

  RunOnTaskRunner(base::BindOnce(
      [](WifiDirectMedium* medium) {
        base::ScopedAllowBaseSyncPrimitivesForTesting allow;
        EXPECT_FALSE(medium->ListenForService(RandomPort()));
      },
      medium()));
}

}  // namespace nearby::chrome
