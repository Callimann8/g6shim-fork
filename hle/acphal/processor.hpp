// SPDX-FileCopyrightText: 2026 gookie and g6shim contributors
// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "queue.hpp"

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <functional>

namespace g6shim::acphal {

enum class Command : std::uint32_t { SubmitBuffer = 1, Start = 2 };
enum class MessageType : std::uint32_t { BufferDone = 2, Fence = 0x10, ShutdownDone = 0x80 };

#pragma pack(push, 4)
struct Message {
    std::uint32_t type;
    std::uint32_t reserved[3];
    std::uint64_t value;
    std::uint32_t tail;
};
#pragma pack(pop)
static_assert(sizeof(Message) == 0x1C);

struct SubmitBufferPayload {
    std::uint32_t count;
    std::uint32_t apu_address;
    std::uint64_t reserved;
};
static_assert(sizeof(SubmitBufferPayload) == 16);

struct StartPayload {
    std::uint32_t value;
};

constexpr std::uint32_t kMaxQueueEntries = 0x10000;

using BufferSink = std::function<void(const SubmitBufferPayload&)>;
using AddressCheck = std::function<bool(std::uint32_t apu_address)>;

class Processor {
public:
    Processor(BufferSink sink, AddressCheck valid_address);

    HRESULT init(std::uint32_t command_entries, std::uint32_t message_entries);
    HRESULT submit_buffer(const SubmitBufferPayload& payload);
    HRESULT start(const StartPayload& payload);
    HRESULT request_shutdown();
    bool next_message(Message& out);
    std::uint32_t pending_messages() const;
    void run_frame();
    std::uint64_t fence() const { return fence_; }
    std::uint64_t dropped_messages() const { return dropped_; }

private:
    void post(MessageType type, std::uint64_t value);

    BufferSink sink_;
    AddressCheck valid_address_;
    BoundedQueue<SubmitBufferPayload> commands_;
    BoundedQueue<Message> messages_;
    std::atomic<bool> initialized_{false};
    std::atomic<std::uint64_t> fence_{0};
    std::atomic<std::uint64_t> dropped_{0};
};

}  // namespace g6shim::acphal
