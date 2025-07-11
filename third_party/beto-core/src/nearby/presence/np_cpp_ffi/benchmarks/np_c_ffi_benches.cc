// Copyright 2023 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Benchmarks measuring the performance of the pure autogenerated C API.
// This is useful to compare with the C++ benchmarks to understand the overhead
// incurred by going through the C++ wrapper types instead of directly calling
// the C API.

#include "benchmark/benchmark.h"
#include "np_cpp_ffi_functions.h"
#include "np_cpp_ffi_types.h"
#include "shared_test_util.h"

void V0PlaintextCApi(benchmark::State &state) {
  auto num_ciphers = state.range(0);
  auto slab_result = np_ffi::internal::np_ffi_create_credential_slab();
  assert(
      np_ffi::internal::np_ffi_CreateCredentialSlabResult_kind(slab_result) ==
      np_ffi::internal::CreateCredentialSlabResultKind::Success);
  auto slab = np_ffi::internal::np_ffi_CreateCredentialSlabResult_into_SUCCESS(
      slab_result);

  auto book_result =
      np_ffi::internal::np_ffi_create_credential_book_from_slab(slab);
  assert(
      np_ffi::internal::np_ffi_CreateCredentialBookResult_kind(book_result) ==
      np_ffi::internal::CreateCredentialBookResultKind::Success);
  auto book = np_ffi::internal::np_ffi_CreateCredentialBookResult_into_SUCCESS(
      book_result);

  for ([[maybe_unused]] auto _ : state) {
    for (int i = 0; i < num_ciphers; i++) {
      auto result = np_ffi::internal::np_ffi_deserialize_advertisement(
          {V0AdvMultiDeInternals}, book);
      assert(np_ffi::internal::np_ffi_DeserializeAdvertisementResult_kind(
                 result) ==
             np_ffi::internal::DeserializeAdvertisementResultKind::V0);
      [[maybe_unused]] auto deallocate_result =
          np_ffi::internal::np_ffi_deallocate_deserialize_advertisement_result(
              result);
      assert(deallocate_result == np_ffi::internal::DeallocateResult::Success);
    }
  }

  [[maybe_unused]] auto deallocate_result =
      np_ffi::internal::np_ffi_deallocate_credential_book(book);
  assert(deallocate_result == np_ffi::internal::DeallocateResult::Success);
}

BENCHMARK(V0PlaintextCApi)
    ->RangeMultiplier(10)
    ->Range(1, 1000)
    ->Unit(benchmark::kMicrosecond);
