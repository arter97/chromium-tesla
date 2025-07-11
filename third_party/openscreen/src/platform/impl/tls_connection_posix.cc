// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "platform/impl/tls_connection_posix.h"

#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <openssl/ssl.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <cstring>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "platform/api/task_runner.h"
#include "platform/base/error.h"
#include "platform/base/span.h"
#include "platform/impl/stream_socket.h"
#include "util/crypto/openssl_util.h"
#include "util/osp_logging.h"

namespace openscreen {

// TODO(jophba): implement write blocking/unblocking
TlsConnectionPosix::TlsConnectionPosix(IPEndpoint local_address,
                                       TaskRunner& task_runner)
    : task_runner_(task_runner),
      socket_(std::make_unique<StreamSocketPosix>(local_address)) {}

TlsConnectionPosix::TlsConnectionPosix(IPAddress::Version version,
                                       TaskRunner& task_runner)
    : task_runner_(task_runner),
      socket_(std::make_unique<StreamSocketPosix>(version)) {}

TlsConnectionPosix::TlsConnectionPosix(std::unique_ptr<StreamSocket> socket,
                                       TaskRunner& task_runner)
    : task_runner_(task_runner), socket_(std::move(socket)) {}

TlsConnectionPosix::~TlsConnectionPosix() {
  if (platform_client_) {
    platform_client_->tls_data_router()->DeregisterConnection(this);
  }
  // TODO(issuetracker.google.com/169966671): This is only tested by CastSocket
  // E2E tests at the moment.
  if (ssl_) {
    SSL_shutdown(ssl_.get());
  }
}

void TlsConnectionPosix::TryReceiveMessage() {
  OSP_CHECK(ssl_);
  constexpr int kMaxApplicationDataBytes = 65536;
  std::vector<uint8_t> block(kMaxApplicationDataBytes);
  ClearOpenSSLERRStack(CURRENT_LOCATION);
  const int bytes_read =
      SSL_read(ssl_.get(), block.data(), kMaxApplicationDataBytes);

  // Read operator was not successful, either due to a closed connection,
  // no application data available, an error occurred, or we have to take an
  // action.
  if (bytes_read <= 0) {
    Error error = GetSSLError(ssl_.get(), bytes_read);
    if (!error.ok() && (error != Error::Code::kAgain)) {
      DispatchError(std::move(error));
    }
    return;
  }

  block.resize(bytes_read);

  task_runner_.PostTask([weak_this = weak_factory_.GetWeakPtr(),
                         moved_block = std::move(block)]() mutable {
    if (auto* self = weak_this.get()) {
      if (auto* client = self->client_) {
        client->OnRead(self, std::move(moved_block));
      }
    }
  });
}

void TlsConnectionPosix::SetClient(Client* client) {
  OSP_CHECK(task_runner_.IsRunningOnTaskRunner());
  client_ = client;
}

bool TlsConnectionPosix::Send(const void* data, size_t len) {
  OSP_CHECK(task_runner_.IsRunningOnTaskRunner());
  return buffer_.Push(data, len);
}

IPEndpoint TlsConnectionPosix::GetRemoteEndpoint() const {
  OSP_CHECK(task_runner_.IsRunningOnTaskRunner());

  std::optional<IPEndpoint> endpoint = socket_->remote_address();
  OSP_CHECK(endpoint.has_value());
  return endpoint.value();
}

void TlsConnectionPosix::RegisterConnectionWithDataRouter(
    PlatformClientPosix* platform_client) {
  OSP_CHECK(!platform_client_);
  platform_client_ = platform_client;
  platform_client_->tls_data_router()->RegisterConnection(this);
}

void TlsConnectionPosix::SendAvailableBytes() {
  ByteView sendable_bytes = buffer_.GetReadableRegion();
  if (sendable_bytes.empty()) {
    return;
  }

  ClearOpenSSLERRStack(CURRENT_LOCATION);
  const int result =
      SSL_write(ssl_.get(), sendable_bytes.data(), sendable_bytes.size());
  if (result <= 0) {
    Error result_error = GetSSLError(ssl_.get(), result);
    if (!result_error.ok() && (result_error.code() != Error::Code::kAgain)) {
      DispatchError(std::move(result_error));
    }
  } else {
    buffer_.Consume(static_cast<size_t>(result));
  }
}

void TlsConnectionPosix::DispatchError(Error error) {
  task_runner_.PostTask([weak_this = weak_factory_.GetWeakPtr(),
                         moved_error = std::move(error)]() mutable {
    if (auto* self = weak_this.get()) {
      if (auto* client = self->client_) {
        client->OnError(self, std::move(moved_error));
      }
    }
  });
}

}  // namespace openscreen
