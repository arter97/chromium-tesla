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
//! Common externally-accessible FFI constructs which are needed
//! in order to define the interfaces in this crate's various modules.

use array_view::ArrayView;
use crypto_provider::{CryptoProvider, CryptoRng};
use crypto_provider_default::CryptoProviderImpl;
use handle_map::HandleNotPresentError;
use lock_adapter::std::{RwLock, RwLockWriteGuard};
use lock_adapter::RwLock as _;
use std::string::String;

pub(crate) const DEFAULT_MAX_HANDLES: u32 = u32::MAX - 1;

/// Configuration for top-level constants to be used
/// by the rest of the FFI which are independent of
/// the programming language which we ultimately
/// interface with at a higher level.
#[repr(C)]
pub struct CommonConfig {
    /// The number of shards to employ in all handle-maps,
    /// or zero if we want to use the default.
    ///
    /// The default number of shards will depend on whether
    /// this crate was compiled with the `std` feature or not:
    /// - If compiled with the `std` feature, the default number
    ///   of shards will be set to
    ///   `min(16, std::thread::available_parallelism().unwrap())`,
    ///   assuming that that call completes successfully.
    /// - In all other cases, 16 shards will be used by default.
    num_shards: u8,

    /// The maximum number of credential slabs which may be active
    /// at any one time. By default, this value will be set to
    /// `u32::MAX - 1`, which is the upper-bound on this value.
    max_num_credential_slabs: u32,

    /// The maximum number of credential books which may be active
    /// at any one time. By default, this value will be set to
    /// `u32::MAX - 1`, which is the upper-bound on this value.
    max_num_credential_books: u32,

    /// The maximum number of deserialized v0 advertisements
    /// which may be active at any one time. By default, this
    /// value will be set to `u32::MAX - 1`, which is the upper-bound
    /// on this value.
    max_num_deserialized_v0_advertisements: u32,

    /// The maximum number of deserialized v1 advertisements
    /// which may be active at any one time. By default, this
    /// value will be set to `u32::MAX - 1`, which is the upper-bound
    /// on this value.
    max_num_deserialized_v1_advertisements: u32,

    /// The maximum number of v0 advertisement builders
    /// which may be active at any one time. By default, this
    /// value will be set to `u32::MAX - 1`, which is the upper-bound
    /// on this value.
    max_num_v0_advertisement_builders: u32,

    /// The maximum number of v1 advertisement builders
    /// which may be active at any one time. By default, this
    /// value will be set to `u32::MAX - 1`, which is the upper-bound
    /// on this value.
    max_num_v1_advertisement_builders: u32,
}

impl Default for CommonConfig {
    fn default() -> Self {
        Self::new()
    }
}

impl CommonConfig {
    pub(crate) const fn new() -> Self {
        Self {
            num_shards: 0,
            max_num_credential_slabs: DEFAULT_MAX_HANDLES,
            max_num_credential_books: DEFAULT_MAX_HANDLES,
            max_num_deserialized_v0_advertisements: DEFAULT_MAX_HANDLES,
            max_num_deserialized_v1_advertisements: DEFAULT_MAX_HANDLES,
            max_num_v0_advertisement_builders: DEFAULT_MAX_HANDLES,
            max_num_v1_advertisement_builders: DEFAULT_MAX_HANDLES,
        }
    }
    #[cfg(feature = "std")]
    pub(crate) fn num_shards(&self) -> u8 {
        if self.num_shards == 0 {
            match std::thread::available_parallelism() {
                Ok(parallelism) => 16u8.min(parallelism),
                Err(_) => 16u8,
            }
        } else {
            self.num_shards
        }
    }
    #[cfg(not(feature = "std"))]
    pub(crate) fn num_shards(&self) -> u8 {
        if self.num_shards == 0 {
            16u8
        } else {
            self.num_shards
        }
    }
    pub(crate) fn max_num_credential_slabs(&self) -> u32 {
        self.max_num_credential_slabs
    }
    pub(crate) fn max_num_credential_books(&self) -> u32 {
        self.max_num_credential_books
    }
    pub(crate) fn max_num_deserialized_v0_advertisements(&self) -> u32 {
        self.max_num_deserialized_v0_advertisements
    }
    pub(crate) fn max_num_deserialized_v1_advertisements(&self) -> u32 {
        self.max_num_deserialized_v1_advertisements
    }
    pub(crate) fn max_num_v0_advertisement_builders(&self) -> u32 {
        self.max_num_v0_advertisement_builders
    }
    pub(crate) fn max_num_v1_advertisement_builders(&self) -> u32 {
        self.max_num_v1_advertisement_builders
    }
    pub(crate) fn set_num_shards(&mut self, num_shards: u8) {
        self.num_shards = num_shards
    }

    /// Sets the maximum number of active handles to credential-books
    /// which may be active at any one time.
    /// Max value: `u32::MAX - 1`.
    pub fn set_max_num_credential_books(&mut self, max_num_credential_books: u32) {
        self.max_num_credential_books = DEFAULT_MAX_HANDLES.min(max_num_credential_books)
    }

    /// Sets the maximum number of active handles to credential-slabs
    /// which may be active at any one time.
    /// Max value: `u32::MAX - 1`.
    pub fn set_max_num_credential_slabs(&mut self, max_num_credential_slabs: u32) {
        self.max_num_credential_slabs = DEFAULT_MAX_HANDLES.min(max_num_credential_slabs)
    }

    /// Sets the maximum number of active handles to deserialized v0
    /// advertisements which may be active at any one time.
    /// Max value: `u32::MAX - 1`.
    pub fn set_max_num_deserialized_v0_advertisements(
        &mut self,
        max_num_deserialized_v0_advertisements: u32,
    ) {
        self.max_num_deserialized_v0_advertisements =
            DEFAULT_MAX_HANDLES.min(max_num_deserialized_v0_advertisements)
    }

    /// Sets the maximum number of active handles to deserialized v1
    /// advertisements which may be active at any one time.
    /// Max value: `u32::MAX - 1`.
    pub fn set_max_num_deserialized_v1_advertisements(
        &mut self,
        max_num_deserialized_v1_advertisements: u32,
    ) {
        self.max_num_deserialized_v1_advertisements =
            DEFAULT_MAX_HANDLES.min(max_num_deserialized_v1_advertisements)
    }
    /// Sets the maximum number of active handles to v0 advertisement
    /// builders which may be active at any one time.
    /// Max value: `u32::MAX - 1`.
    pub fn set_max_num_v0_advertisement_builders(
        &mut self,
        max_num_v0_advertisement_builders: u32,
    ) {
        self.max_num_v0_advertisement_builders =
            DEFAULT_MAX_HANDLES.min(max_num_v0_advertisement_builders)
    }
    /// Sets the maximum number of active handles to v1 advertisement
    /// builders which may be active at any one time.
    /// Max value: `u32::MAX - 1`.
    pub fn set_max_num_v1_advertisement_builders(
        &mut self,
        max_num_v1_advertisement_builders: u32,
    ) {
        self.max_num_v1_advertisement_builders =
            DEFAULT_MAX_HANDLES.min(max_num_v1_advertisement_builders)
    }
}

static COMMON_CONFIG: RwLock<CommonConfig> = RwLock::new(CommonConfig::new());

pub(crate) fn global_num_shards() -> u8 {
    COMMON_CONFIG.read().num_shards()
}
pub(crate) fn global_max_num_credential_slabs() -> u32 {
    COMMON_CONFIG.read().max_num_credential_slabs()
}
pub(crate) fn global_max_num_credential_books() -> u32 {
    COMMON_CONFIG.read().max_num_credential_books()
}
pub(crate) fn global_max_num_deserialized_v0_advertisements() -> u32 {
    COMMON_CONFIG.read().max_num_deserialized_v0_advertisements()
}
pub(crate) fn global_max_num_deserialized_v1_advertisements() -> u32 {
    COMMON_CONFIG.read().max_num_deserialized_v1_advertisements()
}
pub(crate) fn global_max_num_v0_advertisement_builders() -> u32 {
    COMMON_CONFIG.read().max_num_v0_advertisement_builders()
}
pub(crate) fn global_max_num_v1_advertisement_builders() -> u32 {
    COMMON_CONFIG.read().max_num_v1_advertisement_builders()
}

/// Sets an override to the number of shards to employ in the NP FFI's
/// internal handle-maps, which places an upper bound on the number
/// of writing threads which may make progress at any one time
/// when concurrently accessing handles of the same type.
///
/// By default, this value will be set to 16, or in `std` environments,
/// the minimum of 16 and the number of available hardware threads.
/// A shard value override of zero will be interpreted the same
/// as this default.
///
/// Setting this value will have no effect if the handle-maps for the
/// API have already begun being used by the client code, and any
/// values set will take effect upon the first usage of _any_ non-`global_config_set`
/// API call.
pub fn global_config_set_num_shards(num_shards: u8) {
    let mut config = COMMON_CONFIG.write();
    config.set_num_shards(num_shards);
}

/// Sets the maximum number of active handles to credential slabs
/// which may be active at any one time. Max value: `u32::MAX - 1`.
///
/// Setting this value will have no effect if the handle-maps for the
/// API have already begun being used by the client code, and any
/// values set will take effect upon the first usage of any API
/// call utilizing credential slabs.
pub fn global_config_set_max_num_credential_slabs(max_num_credential_slabs: u32) {
    let mut config = COMMON_CONFIG.write();
    config.set_max_num_credential_slabs(max_num_credential_slabs);
}
/// Sets the maximum number of active handles to credential books
/// which may be active at any one time. Max value: `u32::MAX - 1`.
///
/// Setting this value will have no effect if the handle-maps for the
/// API have already begun being used by the client code, and any
/// values set will take effect upon the first usage of any API
/// call utilizing credential books.
pub fn global_config_set_max_num_credential_books(max_num_credential_books: u32) {
    let mut config = COMMON_CONFIG.write();
    config.set_max_num_credential_books(max_num_credential_books);
}

/// Sets the maximum number of active handles to deserialized v0
/// advertisements which may be active at any one time.
/// Max value: `u32::MAX - 1`.
///
/// Setting this value will have no effect if the handle-maps for the
/// API have already begun being used by the client code, and any
/// values set will take effect upon the first usage of any API
/// call which references or returns a deserialized V0 advertisement.
pub fn global_config_set_max_num_deserialized_v0_advertisements(
    max_num_deserialized_v0_advertisements: u32,
) {
    let mut config = COMMON_CONFIG.write();
    config.set_max_num_deserialized_v0_advertisements(max_num_deserialized_v0_advertisements);
}

/// Sets the maximum number of active handles to deserialized v1
/// advertisements which may be active at any one time.
/// Max value: `u32::MAX - 1`.
///
/// Setting this value will have no effect if the handle-maps for the
/// API have already begun being used by the client code, and any
/// values set will take effect upon the first usage of any API
/// call which references or returns a deserialized V1 advertisement.
pub fn global_config_set_max_num_deserialized_v1_advertisements(
    max_num_deserialized_v1_advertisements: u32,
) {
    let mut config = COMMON_CONFIG.write();
    config.set_max_num_deserialized_v1_advertisements(max_num_deserialized_v1_advertisements);
}

/// Sets the maximum number of active handles to v0 advertisement
/// builders which may be active at any one time.
/// Max value: `u32::MAX - 1`.
///
/// Setting this value will have no effect if the handle-maps for the
/// API have already begun being used by the client code, and any
/// values set will take effect upon the first usage of any API
/// call which references or returns a v0 advertisement builder.
pub fn global_config_set_max_num_v0_advertisement_builders(max_num_v0_advertisement_builders: u32) {
    let mut config = COMMON_CONFIG.write();
    config.set_max_num_v0_advertisement_builders(max_num_v0_advertisement_builders);
}

/// Sets the maximum number of active handles to v1 advertisement
/// builders which may be active at any one time.
/// Max value: `u32::MAX - 1`.
///
/// Setting this value will have no effect if the handle-maps for the
/// API have already begun being used by the client code, and any
/// values set will take effect upon the first usage of any API
/// call which references or returns a v1 advertisement builder.
pub fn global_config_set_max_num_v1_advertisement_builders(max_num_v1_advertisement_builders: u32) {
    let mut config = COMMON_CONFIG.write();
    config.set_max_num_v1_advertisement_builders(max_num_v1_advertisement_builders);
}
// API surfaces:

/// A result-type enum which tells the caller whether/not a deallocation
/// succeeded or failed due to the requested handle not being present.
#[repr(C)]
pub enum DeallocateResult {
    /// The requested handle to deallocate was not present in the map
    NotPresent = 0,
    /// The object behind the handle was successfully deallocated
    Success = 1,
}

impl From<Result<(), HandleNotPresentError>> for DeallocateResult {
    fn from(result: Result<(), HandleNotPresentError>) -> Self {
        match result {
            Ok(_) => DeallocateResult::Success,
            Err(_) => DeallocateResult::NotPresent,
        }
    }
}

/// Represents the raw contents of the service payload data
/// under the Nearby Presence service UUID
#[repr(C)]
pub struct RawAdvertisementPayload {
    bytes: ByteBuffer<255>,
}

impl RawAdvertisementPayload {
    /// Yields a slice of the bytes in this raw advertisement payload.
    #[allow(clippy::unwrap_used)]
    pub fn as_slice(&self) -> &[u8] {
        // The unwrapping here will never trigger a panic,
        // because the byte-buffer is 255 bytes, the byte-length
        // of which is the maximum value storable in a u8.
        self.bytes.as_slice().unwrap()
    }
}

/// A byte-string with a maximum size of N,
/// where only the first `len` bytes are considered
/// to contain the actual payload. N is only
/// permitted to be between 0 and 255.
#[derive(Clone)]
#[repr(C)]
// TODO: Once generic const exprs are stabilized,
// we could instead make N into a compile-time u8.
pub struct ByteBuffer<const N: usize> {
    len: u8,
    bytes: [u8; N],
}

/// A FFI safe wrapper of a fixed size array
#[derive(Clone)]
#[repr(C)]
pub struct FixedSizeArray<const N: usize>([u8; N]);

impl<const N: usize> FixedSizeArray<N> {
    /// Constructs a byte-buffer from a Rust-side-derived owned array
    pub(crate) fn from_array(bytes: [u8; N]) -> Self {
        Self(bytes)
    }
    /// Yields a slice of the bytes
    pub fn as_slice(&self) -> &[u8] {
        self.0.as_slice()
    }
    /// De-structures this FFI-compatible fixed-size array
    /// into a bare Rust fixed size array.
    pub fn into_array(self) -> [u8; N] {
        self.0
    }
}

impl<const N: usize> ByteBuffer<N> {
    /// Constructs a byte-buffer from a Rust-side-derived
    /// ArrayView, which is assumed to be trusted to be
    /// properly initialized, and with a size-bound
    /// under 255 bytes.
    pub(crate) fn from_array_view(array_view: ArrayView<u8, N>) -> Self {
        let (len, bytes) = array_view.into_raw_parts();
        let len = len as u8;
        Self { len, bytes }
    }
    /// Yields a slice of the first `self.len` bytes of `self.bytes`.
    pub fn as_slice(&self) -> Option<&[u8]> {
        if self.len as usize <= N {
            Some(&self.bytes[..(self.len as usize)])
        } else {
            None
        }
    }
}

pub(crate) type CryptoRngImpl = <CryptoProviderImpl as CryptoProvider>::CryptoRng;

pub(crate) struct LazyInitCryptoRng {
    maybe_rng: Option<CryptoRngImpl>,
}

impl LazyInitCryptoRng {
    const fn new() -> Self {
        Self { maybe_rng: None }
    }
    pub(crate) fn get_rng(&mut self) -> &mut CryptoRngImpl {
        self.maybe_rng.get_or_insert_with(CryptoRngImpl::new)
    }
}

/// Shared, lazily-initialized cryptographically-secure
/// RNG for all operations in the FFI core.
static CRYPTO_RNG: RwLock<LazyInitCryptoRng> = RwLock::new(LazyInitCryptoRng::new());

/// Gets a write guard to the (lazily-init) library-global crypto rng.
pub(crate) fn get_global_crypto_rng() -> RwLockWriteGuard<'static, LazyInitCryptoRng> {
    CRYPTO_RNG.write()
}

/// The DE type for an encrypted identity
#[derive(Clone, Copy)]
#[repr(u8)]
pub enum EncryptedIdentityType {
    /// Identity for broadcasts to nearby devices with the same
    /// logged-in-account (for some account).
    Private = 1,
    /// Identity for broadcasts to nearby devices which this
    /// device has declared to trust.
    Trusted = 2,
    /// Identity for broadcasts to devices which have been provisioned
    /// offline with this device.
    Provisioned = 4,
}

impl From<EncryptedIdentityType> for np_adv::de_type::EncryptedIdentityDataElementType {
    fn from(val: EncryptedIdentityType) -> np_adv::de_type::EncryptedIdentityDataElementType {
        use np_adv::de_type::EncryptedIdentityDataElementType;
        match val {
            EncryptedIdentityType::Private => EncryptedIdentityDataElementType::Private,
            EncryptedIdentityType::Trusted => EncryptedIdentityDataElementType::Trusted,
            EncryptedIdentityType::Provisioned => EncryptedIdentityDataElementType::Provisioned,
        }
    }
}

impl From<np_adv::de_type::EncryptedIdentityDataElementType> for EncryptedIdentityType {
    fn from(value: np_adv::de_type::EncryptedIdentityDataElementType) -> Self {
        use np_adv::de_type::EncryptedIdentityDataElementType;
        match value {
            EncryptedIdentityDataElementType::Private => Self::Private,
            EncryptedIdentityDataElementType::Trusted => Self::Trusted,
            EncryptedIdentityDataElementType::Provisioned => Self::Provisioned,
        }
    }
}

/// Error returned if the bit representation of a supposedly-Rust-constructed
/// -and-validated type actually doesn't correspond to the format of the
/// data structure expected on the Rust side of the boundary, and performing
/// further operations on the structure would yield unintended behavior.
/// If this kind of error is being raised, the foreign lang code must
/// be messing with stack-allocated data structures for this library
/// in an entirely unexpected way.
#[derive(Debug)]
pub struct InvalidStackDataStructure;

/// Error raised when attempting to cast an enum to
/// one of its variants, but the value is actually
/// of a different variant than the requested one.
pub struct EnumCastError {
    pub(crate) projection_method_name: String,
    pub(crate) variant_enum_name: String,
    pub(crate) variant_type_name: String,
}

impl core::fmt::Debug for EnumCastError {
    fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        write!(
            f,
            "Attempted to cast a non-{} to a {} via {}",
            &self.variant_enum_name, &self.variant_type_name, &self.projection_method_name
        )
    }
}
