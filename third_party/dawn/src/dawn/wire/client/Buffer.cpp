// Copyright 2019 The Dawn & Tint Authors
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice, this
//    list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution.
//
// 3. Neither the name of the copyright holder nor the names of its
//    contributors may be used to endorse or promote products derived from
//    this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
// FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
// DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
// CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
// OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "dawn/wire/client/Buffer.h"

#include <functional>
#include <limits>
#include <string>
#include <utility>

#include "dawn/wire/BufferConsumer_impl.h"
#include "dawn/wire/WireCmd_autogen.h"
#include "dawn/wire/client/Client.h"
#include "dawn/wire/client/Device.h"
#include "dawn/wire/client/EventManager.h"
#include "partition_alloc/pointers/raw_ptr.h"

namespace dawn::wire::client {
namespace {
WGPUBuffer CreateErrorBufferOOMAtClient(Device* device, const WGPUBufferDescriptor* descriptor) {
    if (descriptor->mappedAtCreation) {
        return nullptr;
    }
    WGPUBufferDescriptor errorBufferDescriptor = *descriptor;
    WGPUDawnBufferDescriptorErrorInfoFromWireClient errorInfo = {};
    errorInfo.chain.sType = WGPUSType_DawnBufferDescriptorErrorInfoFromWireClient;
    errorInfo.outOfMemory = true;
    errorBufferDescriptor.nextInChain = &errorInfo.chain;
    return GetProcs().deviceCreateErrorBuffer(ToAPI(device), &errorBufferDescriptor);
}
}  // anonymous namespace

class Buffer::MapAsyncEvent : public TrackedEvent {
  public:
    static constexpr EventType kType = EventType::MapAsync;

    MapAsyncEvent(const WGPUBufferMapCallbackInfo& callbackInfo, Buffer* buffer)
        : TrackedEvent(callbackInfo.mode),
          mCallback(callbackInfo.callback),
          mUserdata(callbackInfo.userdata),
          mBuffer(buffer) {
        DAWN_ASSERT(buffer != nullptr);
        mBuffer->AddRef();
    }

    ~MapAsyncEvent() override { mBuffer.ExtractAsDangling()->Release(); }

    EventType GetType() override { return kType; }

    bool IsPendingRequest(FutureID futureID) {
        return mBuffer->mPendingMapRequest && mBuffer->mPendingMapRequest->futureID == futureID;
    }

    WireResult ReadyHook(FutureID futureID,
                         WGPUBufferMapAsyncStatus status,
                         uint64_t readDataUpdateInfoLength = 0,
                         const uint8_t* readDataUpdateInfo = nullptr) {
        auto FailRequest = [this]() -> WireResult {
            mStatus = WGPUBufferMapAsyncStatus_Unknown;
            return WireResult::FatalError;
        };

        // Handling for different statuses.
        switch (status) {
            case WGPUBufferMapAsyncStatus_MappingAlreadyPending: {
                DAWN_ASSERT(!IsPendingRequest(futureID));
                mStatus = status;
                break;
            }

            // For client-side rejection errors, we clear the pending request now since they always
            // take precedence.
            case WGPUBufferMapAsyncStatus_DestroyedBeforeCallback:
            case WGPUBufferMapAsyncStatus_UnmappedBeforeCallback: {
                mStatus = status;
                mBuffer->mPendingMapRequest = std::nullopt;
                break;
            }

            case WGPUBufferMapAsyncStatus_Success: {
                if (!IsPendingRequest(futureID)) {
                    // If a success occurs (which must come from the server), but it does not
                    // correspond to the pending request, the pending request must have been
                    // rejected early and hence the status must be set.
                    DAWN_ASSERT(mStatus);
                    break;
                }
                mStatus = status;
                auto& pending = mBuffer->mPendingMapRequest.value();
                if (!pending.type) {
                    return FailRequest();
                }
                switch (*pending.type) {
                    case MapRequestType::Read: {
                        if (readDataUpdateInfoLength > std::numeric_limits<size_t>::max()) {
                            // This is the size of data deserialized from the command stream, which
                            // must be CPU-addressable.
                            return FailRequest();
                        }

                        // Validate to prevent bad map request; buffer destroyed during map request
                        if (mBuffer->mReadHandle == nullptr) {
                            return FailRequest();
                        }
                        // Update user map data with server returned data
                        if (!mBuffer->mReadHandle->DeserializeDataUpdate(
                                readDataUpdateInfo, static_cast<size_t>(readDataUpdateInfoLength),
                                pending.offset, pending.size)) {
                            return FailRequest();
                        }
                        mBuffer->mMappedData = const_cast<void*>(mBuffer->mReadHandle->GetData());
                        break;
                    }
                    case MapRequestType::Write: {
                        if (mBuffer->mWriteHandle == nullptr) {
                            return FailRequest();
                        }
                        mBuffer->mMappedData = mBuffer->mWriteHandle->GetData();
                        break;
                    }
                }
                mBuffer->mMappedOffset = pending.offset;
                mBuffer->mMappedSize = pending.size;
                break;
            }

            // All other statuses are server-side status.
            default: {
                if (!IsPendingRequest(futureID)) {
                    break;
                }
                mStatus = status;
            }
        }
        return WireResult::Success;
    }

  private:
    void CompleteImpl(FutureID futureID, EventCompletionType completionType) override {
        WGPUBufferMapAsyncStatus status = completionType == EventCompletionType::Shutdown
                                              ? WGPUBufferMapAsyncStatus_DeviceLost
                                              : WGPUBufferMapAsyncStatus_Success;
        if (mStatus) {
            status = *mStatus;
        }

        auto Callback = [this, &status]() {
            if (mCallback) {
                mCallback(status, mUserdata.ExtractAsDangling());
            }
        };

        if (!IsPendingRequest(futureID)) {
            DAWN_ASSERT(status != WGPUBufferMapAsyncStatus_Success);
            return Callback();
        }

        if (status == WGPUBufferMapAsyncStatus_Success) {
            if (mBuffer->mIsDeviceAlive.expired()) {
                // If the device lost its last ref before this callback was resolved, we want to
                // overwrite the status. This is necessary because otherwise dropping the last
                // device reference could race w.r.t what this callback would see.
                status = WGPUBufferMapAsyncStatus_DestroyedBeforeCallback;
                return Callback();
            }
            DAWN_ASSERT(mBuffer->mPendingMapRequest->type);
            switch (*mBuffer->mPendingMapRequest->type) {
                case MapRequestType::Read:
                    mBuffer->mMappedState = MapState::MappedForRead;
                    break;
                case MapRequestType::Write:
                    mBuffer->mMappedState = MapState::MappedForWrite;
                    break;
            }
        }
        mBuffer->mPendingMapRequest = std::nullopt;
        return Callback();
    }

    WGPUBufferMapCallback mCallback;
    raw_ptr<void> mUserdata;

    std::optional<WGPUBufferMapAsyncStatus> mStatus;

    // Strong reference to the buffer so that when we call the callback we can pass the buffer.
    raw_ptr<Buffer> mBuffer;
};

class Buffer::MapAsyncEvent2 : public TrackedEvent {
  public:
    static constexpr EventType kType = EventType::MapAsync;

    MapAsyncEvent2(const WGPUBufferMapCallbackInfo2& callbackInfo, Buffer* buffer)
        : TrackedEvent(callbackInfo.mode),
          mCallback(callbackInfo.callback),
          mUserdata1(callbackInfo.userdata1),
          mUserdata2(callbackInfo.userdata2),
          mBuffer(buffer) {
        DAWN_ASSERT(buffer != nullptr);
        mBuffer->AddRef();
    }

    ~MapAsyncEvent2() override { mBuffer.ExtractAsDangling()->Release(); }

    EventType GetType() override { return kType; }

    bool IsPendingRequest(FutureID futureID) {
        return mBuffer->mPendingMapRequest && mBuffer->mPendingMapRequest->futureID == futureID;
    }

    WireResult ReadyHook(FutureID futureID,
                         WGPUMapAsyncStatus status,
                         const char* message,
                         uint64_t readDataUpdateInfoLength = 0,
                         const uint8_t* readDataUpdateInfo = nullptr) {
        if (status != WGPUMapAsyncStatus_Success) {
            mStatus = status;
            mMessage = message;
            return WireResult::Success;
        }

        // If the request was already aborted via the client side, we don't need to actually do
        // anything, so just return success.
        if (!IsPendingRequest(futureID)) {
            return WireResult::Success;
        }

        auto FailRequest = [this](const char* message) -> WireResult {
            mStatus = WGPUMapAsyncStatus_Unknown;
            mMessage = message;
            return WireResult::FatalError;
        };

        mStatus = status;
        DAWN_ASSERT(message == nullptr);
        const auto& pending = mBuffer->mPendingMapRequest.value();
        if (!pending.type) {
            return FailRequest("Invalid map call without a specified mapping type.");
        }
        switch (*pending.type) {
            case MapRequestType::Read: {
                if (readDataUpdateInfoLength > std::numeric_limits<size_t>::max()) {
                    // This is the size of data deserialized from the command stream, which must be
                    // CPU-addressable.
                    return FailRequest("Invalid data size returned from the server.");
                }

                // Update user map data with server returned data
                if (!mBuffer->mReadHandle->DeserializeDataUpdate(
                        readDataUpdateInfo, static_cast<size_t>(readDataUpdateInfoLength),
                        pending.offset, pending.size)) {
                    return FailRequest("Failed to deserialize data returned from the server.");
                }
                mBuffer->mMappedData = const_cast<void*>(mBuffer->mReadHandle->GetData());
                break;
            }
            case MapRequestType::Write: {
                mBuffer->mMappedData = mBuffer->mWriteHandle->GetData();
                break;
            }
        }
        mBuffer->mMappedOffset = pending.offset;
        mBuffer->mMappedSize = pending.size;

        return WireResult::Success;
    }

  private:
    void CompleteImpl(FutureID futureID, EventCompletionType completionType) override {
        if (completionType == EventCompletionType::Shutdown) {
            mStatus = WGPUMapAsyncStatus_InstanceDropped;
            mMessage = "A valid external Instance reference no longer exists.";
        }

        auto Callback = [this]() {
            if (mCallback) {
                mCallback(mStatus, mMessage ? mMessage->c_str() : nullptr,
                          mUserdata1.ExtractAsDangling(), mUserdata2.ExtractAsDangling());
            }
        };

        if (!IsPendingRequest(futureID)) {
            DAWN_ASSERT(mStatus != WGPUMapAsyncStatus_Success);
            return Callback();
        }

        if (mStatus == WGPUMapAsyncStatus_Success) {
            if (mBuffer->mIsDeviceAlive.expired()) {
                // If the device lost its last ref before this callback was resolved, we want to
                // overwrite the status. This is necessary because otherwise dropping the last
                // device reference could race w.r.t what this callback would see.
                mStatus = WGPUMapAsyncStatus_Aborted;
                mMessage = "Buffer was destroyed before mapping was resolved.";
                return Callback();
            }
            DAWN_ASSERT(mBuffer->mPendingMapRequest->type);
            switch (*mBuffer->mPendingMapRequest->type) {
                case MapRequestType::Read:
                    mBuffer->mMappedState = MapState::MappedForRead;
                    break;
                case MapRequestType::Write:
                    mBuffer->mMappedState = MapState::MappedForWrite;
                    break;
            }
        }
        mBuffer->mPendingMapRequest = std::nullopt;
        return Callback();
    }

    WGPUBufferMapCallback2 mCallback;
    raw_ptr<void> mUserdata1;
    raw_ptr<void> mUserdata2;

    WGPUMapAsyncStatus mStatus;
    std::optional<std::string> mMessage;

    // Strong reference to the buffer so that when we call the callback we can pass the buffer.
    raw_ptr<Buffer> mBuffer;
};

// static
WGPUBuffer Buffer::Create(Device* device, const WGPUBufferDescriptor* descriptor) {
    Client* wireClient = device->GetClient();

    bool mappable =
        (descriptor->usage & (WGPUBufferUsage_MapRead | WGPUBufferUsage_MapWrite)) != 0 ||
        descriptor->mappedAtCreation;
    if (mappable && descriptor->size >= std::numeric_limits<size_t>::max()) {
        return CreateErrorBufferOOMAtClient(device, descriptor);
    }

    std::unique_ptr<MemoryTransferService::ReadHandle> readHandle = nullptr;
    std::unique_ptr<MemoryTransferService::WriteHandle> writeHandle = nullptr;

    DeviceCreateBufferCmd cmd;
    cmd.deviceId = device->GetWireId();
    cmd.descriptor = descriptor;
    cmd.readHandleCreateInfoLength = 0;
    cmd.readHandleCreateInfo = nullptr;
    cmd.writeHandleCreateInfoLength = 0;
    cmd.writeHandleCreateInfo = nullptr;

    size_t readHandleCreateInfoLength = 0;
    size_t writeHandleCreateInfoLength = 0;
    if (mappable) {
        if ((descriptor->usage & WGPUBufferUsage_MapRead) != 0) {
            // Create the read handle on buffer creation.
            readHandle.reset(
                wireClient->GetMemoryTransferService()->CreateReadHandle(descriptor->size));
            if (readHandle == nullptr) {
                return CreateErrorBufferOOMAtClient(device, descriptor);
            }
            readHandleCreateInfoLength = readHandle->SerializeCreateSize();
            cmd.readHandleCreateInfoLength = readHandleCreateInfoLength;
        }

        if ((descriptor->usage & WGPUBufferUsage_MapWrite) != 0 || descriptor->mappedAtCreation) {
            // Create the write handle on buffer creation.
            writeHandle.reset(
                wireClient->GetMemoryTransferService()->CreateWriteHandle(descriptor->size));
            if (writeHandle == nullptr) {
                return CreateErrorBufferOOMAtClient(device, descriptor);
            }
            writeHandleCreateInfoLength = writeHandle->SerializeCreateSize();
            cmd.writeHandleCreateInfoLength = writeHandleCreateInfoLength;
        }
    }

    // Create the buffer and send the creation command.
    // This must happen after any potential error buffer creation
    // as server expects allocating ids to be monotonically increasing
    Buffer* buffer = wireClient->Make<Buffer>(device->GetEventManagerHandle(), descriptor);
    buffer->mIsDeviceAlive = device->GetAliveWeakPtr();

    if (descriptor->mappedAtCreation) {
        // If the buffer is mapped at creation, a write handle is created and will be
        // destructed on unmap if the buffer doesn't have MapWrite usage
        // The buffer is mapped right now.
        buffer->mMappedState = MapState::MappedAtCreation;
        buffer->mMappedOffset = 0;
        buffer->mMappedSize = buffer->mSize;
        DAWN_ASSERT(writeHandle != nullptr);
        buffer->mMappedData = writeHandle->GetData();
    }

    cmd.result = buffer->GetWireHandle();

    // clang-format off
    // Turning off clang format here because for some reason it does not format the
    // CommandExtensions consistently, making it harder to read.
    wireClient->SerializeCommand(
        cmd,
        CommandExtension{readHandleCreateInfoLength,
                         [&](char* readHandleBuffer) {
                             if (readHandle != nullptr) {
                                 // Serialize the ReadHandle into the space after the command.
                                 readHandle->SerializeCreate(readHandleBuffer);
                                 buffer->mReadHandle = std::move(readHandle);
                             }
                         }},
        CommandExtension{writeHandleCreateInfoLength,
                         [&](char* writeHandleBuffer) {
                             if (writeHandle != nullptr) {
                                 // Serialize the WriteHandle into the space after the command.
                                 writeHandle->SerializeCreate(writeHandleBuffer);
                                 buffer->mWriteHandle = std::move(writeHandle);
                             }
                         }});
    // clang-format on
    return ToAPI(buffer);
}

Buffer::Buffer(const ObjectBaseParams& params,
               const ObjectHandle& eventManagerHandle,
               const WGPUBufferDescriptor* descriptor)
    : ObjectWithEventsBase(params, eventManagerHandle),
      mSize(descriptor->size),
      mUsage(static_cast<WGPUBufferUsage>(descriptor->usage)),
      // This flag is for the write handle created by mappedAtCreation
      // instead of MapWrite usage. We don't have such a case for read handle.
      mDestructWriteHandleOnUnmap(descriptor->mappedAtCreation &&
                                  ((descriptor->usage & WGPUBufferUsage_MapWrite) == 0)) {}

Buffer::~Buffer() {
    FreeMappedData();
}

ObjectType Buffer::GetObjectType() const {
    return ObjectType::Buffer;
}

void Buffer::SetFutureStatus(WGPUBufferMapAsyncStatus status) {
    if (!mPendingMapRequest) {
        return;
    }

    FutureID futureID = mPendingMapRequest->futureID;
    bool isNewEntryPoint = mPendingMapRequest->isNewEntryPoint;
    mPendingMapRequest = std::nullopt;

    if (isNewEntryPoint) {
        auto [newStatus, message] =
            [](WGPUBufferMapAsyncStatus status) -> std::pair<WGPUMapAsyncStatus, const char*> {
            switch (status) {
                case WGPUBufferMapAsyncStatus_DestroyedBeforeCallback:
                    return {WGPUMapAsyncStatus_Aborted,
                            "Buffer was destroyed before mapping was resolved."};
                case WGPUBufferMapAsyncStatus_UnmappedBeforeCallback:
                    return {WGPUMapAsyncStatus_Aborted,
                            "Buffer was unmapped before mapping was resolved."};
                default:
                    DAWN_UNREACHABLE();
            }
        }(status);

        DAWN_CHECK(GetEventManager().SetFutureReady<MapAsyncEvent2>(futureID, newStatus, message) ==
                   WireResult::Success);
    } else {
        DAWN_CHECK(GetEventManager().SetFutureReady<MapAsyncEvent>(futureID, status) ==
                   WireResult::Success);
    }
}

void Buffer::MapAsync(WGPUMapModeFlags mode,
                      size_t offset,
                      size_t size,
                      WGPUBufferMapCallback callback,
                      void* userdata) {
    WGPUBufferMapCallbackInfo callbackInfo = {};
    callbackInfo.mode = WGPUCallbackMode_AllowSpontaneous;
    callbackInfo.callback = callback;
    callbackInfo.userdata = userdata;
    MapAsyncF(mode, offset, size, callbackInfo);
}

WGPUFuture Buffer::MapAsyncF(WGPUMapModeFlags mode,
                             size_t offset,
                             size_t size,
                             const WGPUBufferMapCallbackInfo& callbackInfo) {
    DAWN_ASSERT(GetRefcount() != 0);

    Client* client = GetClient();
    auto [futureIDInternal, tracked] =
        GetEventManager().TrackEvent(std::make_unique<MapAsyncEvent>(callbackInfo, this));
    if (!tracked) {
        return {futureIDInternal};
    }

    if (mPendingMapRequest) {
        [[maybe_unused]] auto id = GetEventManager().SetFutureReady<MapAsyncEvent>(
            futureIDInternal, WGPUBufferMapAsyncStatus_MappingAlreadyPending);
        return {futureIDInternal};
    }

    // Handle the defaulting of size required by WebGPU.
    if ((size == WGPU_WHOLE_MAP_SIZE) && (offset <= mSize)) {
        size = mSize - offset;
    }

    // Set up the request structure that will hold information while this mapping is in flight.
    std::optional<MapRequestType> mapMode;
    if (mode & WGPUMapMode_Read) {
        mapMode = MapRequestType::Read;
    } else if (mode & WGPUMapMode_Write) {
        mapMode = MapRequestType::Write;
    }

    mPendingMapRequest = {futureIDInternal, offset, size, mapMode, false};

    // Serialize the command to send to the server.
    BufferMapAsyncCmd cmd;
    cmd.bufferId = GetWireId();
    cmd.eventManagerHandle = GetEventManagerHandle();
    cmd.future = {futureIDInternal};
    cmd.mode = mode;
    cmd.offset = offset;
    cmd.size = size;
    cmd.userdataCount = 1;

    client->SerializeCommand(cmd);
    return {futureIDInternal};
}

WGPUFuture Buffer::MapAsync2(WGPUMapModeFlags mode,
                             size_t offset,
                             size_t size,
                             const WGPUBufferMapCallbackInfo2& callbackInfo) {
    DAWN_ASSERT(GetRefcount() != 0);

    Client* client = GetClient();
    auto [futureIDInternal, tracked] =
        GetEventManager().TrackEvent(std::make_unique<MapAsyncEvent2>(callbackInfo, this));
    if (!tracked) {
        return {futureIDInternal};
    }

    if (mPendingMapRequest) {
        [[maybe_unused]] auto id = GetEventManager().SetFutureReady<MapAsyncEvent2>(
            futureIDInternal, WGPUMapAsyncStatus_Error,
            "Buffer already has an outstanding map pending.");
        return {futureIDInternal};
    }

    // Handle the defaulting of size required by WebGPU.
    if ((size == WGPU_WHOLE_MAP_SIZE) && (offset <= mSize)) {
        size = mSize - offset;
    }

    // Set up the request structure that will hold information while this mapping is in flight.
    std::optional<MapRequestType> mapMode;
    if (mode & WGPUMapMode_Read) {
        mapMode = MapRequestType::Read;
    } else if (mode & WGPUMapMode_Write) {
        mapMode = MapRequestType::Write;
    }

    mPendingMapRequest = {futureIDInternal, offset, size, mapMode, true};

    // Serialize the command to send to the server.
    BufferMapAsyncCmd cmd;
    cmd.bufferId = GetWireId();
    cmd.eventManagerHandle = GetEventManagerHandle();
    cmd.future = {futureIDInternal};
    cmd.mode = mode;
    cmd.offset = offset;
    cmd.size = size;
    cmd.userdataCount = 2;

    client->SerializeCommand(cmd);
    return {futureIDInternal};
}

WireResult Client::DoBufferMapAsyncCallback(ObjectHandle eventManager,
                                            WGPUFuture future,
                                            WGPUBufferMapAsyncStatus status,
                                            WGPUMapAsyncStatus status2,
                                            const char* message,
                                            uint8_t userdataCount,
                                            uint64_t readDataUpdateInfoLength,
                                            const uint8_t* readDataUpdateInfo) {
    if (userdataCount == 1) {
        return GetEventManager(eventManager)
            .SetFutureReady<Buffer::MapAsyncEvent>(future.id, status, readDataUpdateInfoLength,
                                                   readDataUpdateInfo);
    } else {
        return GetEventManager(eventManager)
            .SetFutureReady<Buffer::MapAsyncEvent2>(future.id, status2, message,
                                                    readDataUpdateInfoLength, readDataUpdateInfo);
    }
}

void* Buffer::GetMappedRange(size_t offset, size_t size) {
    if (!IsMappedForWriting() || !CheckGetMappedRangeOffsetSize(offset, size)) {
        return nullptr;
    }
    return static_cast<uint8_t*>(mMappedData) + offset;
}

const void* Buffer::GetConstMappedRange(size_t offset, size_t size) {
    if (!(IsMappedForWriting() || IsMappedForReading()) ||
        !CheckGetMappedRangeOffsetSize(offset, size)) {
        return nullptr;
    }
    return static_cast<uint8_t*>(mMappedData) + offset;
}

void Buffer::Unmap() {
    // Invalidate the local pointer, and cancel all other in-flight requests that would
    // turn into errors anyway (you can't double map). This prevents race when the following
    // happens, where the application code would have unmapped a buffer but still receive a
    // callback:
    //   - Client -> Server: MapRequest1, Unmap, MapRequest2
    //   - Server -> Client: Result of MapRequest1
    //   - Unmap locally on the client
    //   - Server -> Client: Result of MapRequest2
    Client* client = GetClient();

    if (IsMappedForWriting()) {
        // Writes need to be flushed before Unmap is sent. Unmap calls all associated
        // in-flight callbacks which may read the updated data.

        // Get the serialization size of data update writes.
        size_t writeDataUpdateInfoLength =
            mWriteHandle->SizeOfSerializeDataUpdate(mMappedOffset, mMappedSize);

        BufferUpdateMappedDataCmd cmd;
        cmd.bufferId = GetWireId();
        cmd.writeDataUpdateInfoLength = writeDataUpdateInfoLength;
        cmd.writeDataUpdateInfo = nullptr;
        cmd.offset = mMappedOffset;
        cmd.size = mMappedSize;

        client->SerializeCommand(
            cmd, CommandExtension{writeDataUpdateInfoLength, [&](char* writeHandleBuffer) {
                                      // Serialize flush metadata into the space after the command.
                                      // This closes the handle for writing.
                                      mWriteHandle->SerializeDataUpdate(writeHandleBuffer,
                                                                        cmd.offset, cmd.size);
                                  }});

        // If mDestructWriteHandleOnUnmap is true, that means the write handle is merely
        // for mappedAtCreation usage. It is destroyed on unmap after flush to server
        // instead of at buffer destruction.
        if (mDestructWriteHandleOnUnmap) {
            mMappedData = nullptr;
            mWriteHandle = nullptr;
            if (mReadHandle) {
                // If it's both mappedAtCreation and MapRead we need to reset
                // mData to readHandle's GetData(). This could be changed to
                // merging read/write handle in future
                mMappedData = const_cast<void*>(mReadHandle->GetData());
            }
        }
    }

    // Free map access tokens
    mMappedState = MapState::Unmapped;
    mMappedOffset = 0;
    mMappedSize = 0;

    BufferUnmapCmd cmd;
    cmd.self = ToAPI(this);
    client->SerializeCommand(cmd);

    SetFutureStatus(WGPUBufferMapAsyncStatus_UnmappedBeforeCallback);
}

void Buffer::Destroy() {
    Client* client = GetClient();

    // Remove the current mapping and destroy Read/WriteHandles.
    FreeMappedData();

    BufferDestroyCmd cmd;
    cmd.self = ToAPI(this);
    client->SerializeCommand(cmd);

    SetFutureStatus(WGPUBufferMapAsyncStatus_DestroyedBeforeCallback);
}

WGPUBufferUsage Buffer::GetUsage() const {
    return mUsage;
}

uint64_t Buffer::GetSize() const {
    return mSize;
}

WGPUBufferMapState Buffer::GetMapState() const {
    switch (mMappedState) {
        case MapState::MappedForRead:
        case MapState::MappedForWrite:
        case MapState::MappedAtCreation:
            return WGPUBufferMapState_Mapped;
        case MapState::Unmapped:
            if (mPendingMapRequest) {
                return WGPUBufferMapState_Pending;
            } else {
                return WGPUBufferMapState_Unmapped;
            }
    }
    DAWN_UNREACHABLE();
}

bool Buffer::IsMappedForReading() const {
    return mMappedState == MapState::MappedForRead;
}

bool Buffer::IsMappedForWriting() const {
    return mMappedState == MapState::MappedForWrite || mMappedState == MapState::MappedAtCreation;
}

bool Buffer::CheckGetMappedRangeOffsetSize(size_t offset, size_t size) const {
    if (offset % 8 != 0 || offset < mMappedOffset || offset > mSize) {
        return false;
    }

    size_t rangeSize = size == WGPU_WHOLE_MAP_SIZE ? mSize - offset : size;

    if (rangeSize % 4 != 0 || rangeSize > mMappedSize) {
        return false;
    }

    size_t offsetInMappedRange = offset - mMappedOffset;
    return offsetInMappedRange <= mMappedSize - rangeSize;
}

void Buffer::FreeMappedData() {
#if defined(DAWN_ENABLE_ASSERTS)
    // When in "debug" mode, 0xCA-out the mapped data when we free it so that in we can detect
    // use-after-free of the mapped data. This is particularly useful for WebGPU test about the
    // interaction of mapping and GC.
    if (mMappedData) {
        memset(static_cast<uint8_t*>(mMappedData) + mMappedOffset, 0xCA, mMappedSize);
    }
#endif  // defined(DAWN_ENABLE_ASSERTS)

    mMappedOffset = 0;
    mMappedSize = 0;
    mMappedData = nullptr;
    mReadHandle = nullptr;
    mWriteHandle = nullptr;
    mMappedState = MapState::Unmapped;
}

}  // namespace dawn::wire::client
