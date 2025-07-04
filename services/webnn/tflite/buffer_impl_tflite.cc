// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/webnn/tflite/buffer_impl_tflite.h"

#include <climits>

#include "base/memory/ptr_util.h"

namespace webnn::tflite {

std::unique_ptr<WebNNBufferImpl> BufferImplTflite::Create(
    mojo::PendingAssociatedReceiver<mojom::WebNNBuffer> receiver,
    WebNNContextImpl* context,
    mojom::BufferInfoPtr buffer_info,
    const base::UnguessableToken& buffer_handle) {
  // Limit to INT_MAX for security reasons (similar to PartitionAlloc).
  if (buffer_info->size > INT_MAX) {
    DLOG(ERROR) << "Buffer is too large to create.";
    return nullptr;
  }

  return base::WrapUnique(new BufferImplTflite(std::move(receiver), context,
                                         buffer_info->size, buffer_handle));
}

BufferImplTflite::BufferImplTflite(
    mojo::PendingAssociatedReceiver<mojom::WebNNBuffer> receiver,
    WebNNContextImpl* context,
    size_t size,
    const base::UnguessableToken& buffer_handle)
    : WebNNBufferImpl(std::move(receiver), context, size, buffer_handle) {
  buffer_ = base::HeapArray<uint8_t>::WithSize(size);
}

BufferImplTflite::~BufferImplTflite() = default;

void BufferImplTflite::ReadBufferImpl(ReadBufferCallback callback) {
  std::move(callback).Run(
      mojom::ReadBufferResult::NewBuffer(mojo_base::BigBuffer(buffer_)));
}

void BufferImplTflite::WriteBufferImpl(mojo_base::BigBuffer src_buffer) {
  buffer_.first(src_buffer.size()).copy_from(base::span(src_buffer));
}

}  // namespace webnn::tflite
