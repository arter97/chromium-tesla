// Copyright 2023 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef THIRD_PARTY_NEARBY_CONNECTIONS_IMPLEMENTATION_FLAGS_NEARBY_CONNECTIONS_FEATURE_FLAGS_H_
#define THIRD_PARTY_NEARBY_CONNECTIONS_IMPLEMENTATION_FLAGS_NEARBY_CONNECTIONS_FEATURE_FLAGS_H_

#include <cstdint>

#include "absl/strings/string_view.h"
#include "internal/flags/flag.h"

namespace nearby {
namespace connections {
namespace config_package_nearby {

constexpr absl::string_view kConfigPackage = "nearby";

// The Nearby Connections features.
namespace nearby_connections_feature {

// Disable/Enable BLE v2 in Nearby Connections SDK.
constexpr auto kEnableBleV2 =
    flags::Flag<bool>(kConfigPackage, "45401515", false);

// The timeout in millis to report peripheral device lost.
constexpr auto kBlePeripheralLostTimeoutMillis =
    flags::Flag<int64_t>(kConfigPackage, "45411439", 12000);

// Enable/Disable GATT query during scanning.
constexpr auto kEnableGattQueryInThread =
    flags::Flag<bool>(kConfigPackage, "45415261", false);

// Enable/Disable payload manager to skip chunk update.
constexpr auto kEnablePayloadManagerToSkipChunkUpdate =
    flags::Flag<bool>(kConfigPackage, "45415729", false);

// Enable/Disable safe-to-disconnect feature.
constexpr auto kEnableSafeToDisconnect =
    flags::Flag<bool>(kConfigPackage, "45425789", false);

// Enable/Disable auto_reconnect feature.
constexpr auto kEnableAutoReconnect =
    flags::Flag<bool>(kConfigPackage, "45427690", false);

// When true, allows to enable payload-received-ack protocol.
constexpr auto kEnablePayloadReceivedAck =
    flags::Flag<bool>(kConfigPackage, "45425840", false);

// Support 0. disabled all. 1. safe-to-disconnect 2. reserved 3. auto-reconnect
// 4. auto-resume for dev device 5. payload_ack
constexpr auto kSafeToDisconnectVersion =
    flags::Flag<int64_t>(kConfigPackage, "45425841", 0);

// When true, skip the BWU frame if the pcp is not connected.
constexpr auto kProcessBwuFrameAfterPcpConnected =
    flags::Flag<bool>(kConfigPackage, "45627835", false);

// When true, check illegal characters in the file name and parent folder.
constexpr auto kCheckIllegalCharacters =
    flags::Flag<bool>(kConfigPackage, "45632028", false);

// When true, allows to enable Multiplex feature.
constexpr auto kEnableMultiplex =
    flags::Flag<bool>(kConfigPackage, "45627836", false);

// When true, allows to ignore the upgrade path available frame for advertiser.
constexpr auto kIgnoreUpgradePathAvailableFrameForAdvertiser =
    flags::Flag<bool>(kConfigPackage, "45633895", false);

// When true, use the medium in connection info.
constexpr auto kUseMediumInConnectionInfo =
    flags::Flag<bool>(kConfigPackage, "45635058", false);

}  // namespace nearby_connections_feature
}  // namespace config_package_nearby
}  // namespace connections
}  // namespace nearby

#endif  // THIRD_PARTY_NEARBY_CONNECTIONS_IMPLEMENTATION_FLAGS_NEARBY_CONNECTIONS_FEATURE_FLAGS_H_
