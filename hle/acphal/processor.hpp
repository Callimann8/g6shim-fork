// SPDX-FileCopyrightText: 2026 gookie and g6shim contributors
// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "queue.hpp"

#include <windows.h>

#include <atomic>
#include <cstdint>

namespace g6shim::acphal {

enum class Command : std::uint32_t {
    RunList = 1,
    Start = 2,
    ResetVoice = 7,
    ReleaseDma = 0x10,
    RefillInput = 0x13,
};

enum class MessageType : std::uint32_t { ListDone = 2, CommandDone = 0x10, ShutdownDone = 0x80 };

#pragma pack(push, 4)
struct Message {
    std::uint32_t type;
    std::uint32_t reserved[3];
    std::uint64_t value;
    std::uint32_t tail;
};
#pragma pack(pop)
static_assert(sizeof(Message) == 0x1C);

constexpr std::size_t kMaxPayloadBytes = 16;

struct CommandRecord {
    Command command;
    std::uint64_t sequence;
    std::uint32_t payload[kMaxPayloadBytes / 4];
};

std::size_t payload_bytes(Command command);
bool known_command(std::uint32_t command);

class Engine {
public:
    virtual void run_list(std::uint32_t apu_address, std::uint32_t entries) = 0;
    virtual void reset_voice(std::uint32_t xma_index) = 0;
    virtual void refill_input(std::uint32_t xma_index, std::uint32_t buffer) = 0;
    virtual void release_dma(std::uint32_t dma_index, std::uint32_t blocks) = 0;

protected:
    ~Engine() = default;
};

constexpr std::uint32_t kMaxQueueEntries = 0x10000;

class Processor {
public:
    explicit Processor(Engine& engine) : engine_(engine) {}

    HRESULT init(std::uint32_t command_entries, std::uint32_t message_entries);
    HRESULT send(const CommandRecord& record);
    HRESULT request_shutdown();
    bool next_message(Message& out);
    std::uint32_t pending_messages() const;
    void run_frame();
    std::uint64_t dropped_messages() const { return dropped_; }

private:
    void execute(const CommandRecord& record);
    void post(MessageType type, std::uint64_t value);

    Engine& engine_;
    BoundedQueue<CommandRecord> commands_;
    BoundedQueue<Message> messages_;
    std::atomic<bool> initialized_{false};
    std::atomic<std::uint64_t> dropped_{0};
};

}  // namespace g6shim::acphal
