// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_TYPED_ARRAYS_NADC_TYPED_ARRAY_VIEW_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_TYPED_ARRAYS_NADC_TYPED_ARRAY_VIEW_H_

#include "third_party/blink/renderer/core/core_export.h"
#include "third_party/blink/renderer/core/typed_arrays/array_buffer_view_helpers.h"
#include "third_party/blink/renderer/core/typed_arrays/dom_typed_array.h"
#include "third_party/blink/renderer/core/typed_arrays/typed_flexible_array_buffer_view.h"
#include "v8/include/v8-fast-api-calls.h"

namespace blink {

// This class is for passing around un-owned bytes as a typed pointer + length
// into functions tagged with extended attribute [NoAllocDirectCall].
// It supports implicit construction from several other MaybeShared typed array
// data types.
//
// IMPORTANT: The data contained by NADCTypedArrayView is NOT OWNED, so caution
// must be taken to ensure it is kept alive.
template <typename T>
class CORE_EXPORT NADCTypedArrayView {
  STACK_ALLOCATED();

 public:
  NADCTypedArrayView(T* data, size_t size) : data_(data), size_(size) {}

  NADCTypedArrayView(const NADCTypedArrayView<T>& rhs)
      : data_(rhs.Data()), size_(rhs.Size()) {}

  // NOLINTNEXTLINE(google-explicit-constructor)
  template <typename U>
  NADCTypedArrayView(const v8::FastApiTypedArray<U>& rhs)
      : small_buffer_(), size_(rhs.length()) {
    U* data = nullptr;
    bool is_aligned = rhs.getStorageIfAligned(&data);
    DCHECK(is_aligned);
    if (size_ < kOnHeapLimit) {
      std::memcpy(
          const_cast<void*>(reinterpret_cast<const void*>(small_buffer_)), data,
          size_ * sizeof(T));
      data_ = small_buffer_;
      return;
    }
    data_ = data;
  }

  // NOLINTNEXTLINE(google-explicit-constructor)
  template <typename U, bool clamped>
  NADCTypedArrayView(const TypedFlexibleArrayBufferView<U, clamped>& rhs)
      : data_(rhs.DataMaybeOnStack()), size_(rhs.length()) {}

  // NOLINTNEXTLINE(google-explicit-constructor)
  template <typename U, typename V8TypedArray, bool clamped>
  NADCTypedArrayView(
      const MaybeShared<DOMTypedArray<U, V8TypedArray, clamped>>& rhs)
      : small_buffer_(),
        data_(rhs.Get() ? rhs->DataMaybeShared() : nullptr),
        size_(rhs.Get() ? rhs->length() : 0) {}

  T* Data() const { return data_; }

  size_t Size() const { return size_; }

  bool IsEmpty() const { return size_ == 0; }

  bool IsNull() const { return data_ == nullptr; }

 private:
  // If the content of the v8::TypedArray is smaller than 64 bytes, then the
  // content gets allocated on the V8 heap and therefore may move upon garbage
  // collection. Therefore the content of small v8::TypedArrays gets copied into
  // the `small_buffer_` of this class, so that the data is accessible beyond
  // garbage collection.
  static constexpr size_t kOnHeapLimit = 64 / sizeof(T);
  T small_buffer_[kOnHeapLimit];
  T* data_;
  size_t size_;
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_TYPED_ARRAYS_NADC_TYPED_ARRAY_VIEW_H_
