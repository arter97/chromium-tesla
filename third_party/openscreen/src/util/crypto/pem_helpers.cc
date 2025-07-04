// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "util/crypto/pem_helpers.h"

#include <openssl/bytestring.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <stdio.h>
#include <string.h>

#include "util/osp_logging.h"
#include "util/stringutil.h"

namespace openscreen {

std::vector<std::string> ReadCertificatesFromPemFile(
    std::string_view filename) {
  FILE* fp = fopen(filename.data(), "r");
  if (!fp) {
    return {};
  }
  std::vector<std::string> certs;
  char* name;
  char* header;
  unsigned char* data;
  long length;  // NOLINT
  while (PEM_read(fp, &name, &header, &data, &length) == 1) {
    if (stringutil::starts_with(name, "CERTIFICATE")) {
      certs.emplace_back(reinterpret_cast<char*>(data), length);
    }
    OPENSSL_free(name);
    OPENSSL_free(header);
    OPENSSL_free(data);
  }
  fclose(fp);
  return certs;
}

bssl::UniquePtr<EVP_PKEY> ReadKeyFromPemFile(std::string_view filename) {
  FILE* fp = fopen(filename.data(), "r");
  if (!fp) {
    return nullptr;
  }
  bssl::UniquePtr<EVP_PKEY> pkey;
  char* name;
  char* header;
  unsigned char* data;
  long length;  // NOLINT
  while (PEM_read(fp, &name, &header, &data, &length) == 1) {
    if (stringutil::starts_with(name, "RSA PRIVATE KEY")) {
      OSP_CHECK(!pkey);
      CBS cbs;
      CBS_init(&cbs, data, length);
      RSA* rsa = RSA_parse_private_key(&cbs);
      if (rsa) {
        pkey.reset(EVP_PKEY_new());
        EVP_PKEY_assign_RSA(pkey.get(), rsa);
      }
    }
    OPENSSL_free(name);
    OPENSSL_free(header);
    OPENSSL_free(data);
  }
  fclose(fp);
  return pkey;
}

}  // namespace openscreen
