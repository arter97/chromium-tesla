// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/commerce/core/product_specifications/mock_product_specifications_service.h"

#include "base/functional/callback_helpers.h"
#include "components/sync/test/mock_model_type_change_processor.h"

namespace commerce {

MockProductSpecificationsService::MockProductSpecificationsService()
    : ProductSpecificationsService(
          base::DoNothing(),
          std::make_unique<syncer::MockModelTypeChangeProcessor>()) {}

MockProductSpecificationsService::~MockProductSpecificationsService() = default;

}  // namespace commerce
