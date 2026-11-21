// SPDX-FileCopyrightText: 2026 gookie and g6shim contributors
// SPDX-License-Identifier: LGPL-3.0-or-later
#include "processor.hpp"

#include <g6shim/log.hpp>

namespace g6shim::acphal {

std::size_t payload_bytes(Command command) {
    switch (command) {
    case Command::RunList: return 16;
    case Command::Start: return 4;
    case Command::ResetVoice: return 4;
    case Command::ReleaseDma: return 8;
    case Command::RefillInput: return 8;
    }
    return 0;
}

bool known_command(std::uint32_t command) {
    return payload_bytes(static_cast<Command>(command)) != 0;
}

HRESULT Processor::init(std::uint32_t command_entries, std::uint32_t message_entries) {
    if (command_entries == 0 || message_entries == 0 || command_entries > kMaxQueueEntries ||
        message_entries > kMaxQueueEntries)
        return E_INVALIDARG;
    if (initialized_) return E_UNEXPECTED;
    commands_.reset(command_entries);
    messages_.reset(message_entries);
    initialized_ = true;
    return S_OK;
}

HRESULT Processor::send(const CommandRecord& record) {
    if (!initialized_) return E_UNEXPECTED;
    return commands_.push(record) ? S_OK : E_OUTOFMEMORY;
}

HRESULT Processor::request_shutdown() {
    if (!initialized_) return E_UNEXPECTED;
    run_frame();
    post(MessageType::ShutdownDone, 0);
    return S_OK;
}

bool Processor::next_message(Message& out) {
    auto message = messages_.pop();
    if (!message) return false;
    out = *message;
    return true;
}

std::uint32_t Processor::pending_messages() const {
    return static_cast<std::uint32_t>(messages_.size());
}

void Processor::run_frame() {
    if (!initialized_) return;
    while (auto record = commands_.pop()) execute(*record);
}

void Processor::execute(const CommandRecord& r) {
    switch (r.command) {
    case Command::RunList: engine_.run_list(r.payload[1], r.payload[0]); break;
    case Command::Start: break;
    case Command::ResetVoice: engine_.reset_voice(r.payload[0]); break;
    case Command::ReleaseDma: engine_.release_dma(r.payload[0], r.payload[1]); break;
    case Command::RefillInput: engine_.refill_input(r.payload[0], r.payload[1]); break;
    }
    if (r.command == Command::RunList) post(MessageType::ListDone, 0);
    if (r.sequence != 0) post(MessageType::CommandDone, r.sequence);
}

void Processor::post(MessageType type, std::uint64_t value) {
    Message m{};
    m.type = static_cast<std::uint32_t>(type);
    m.value = value;
    if (!messages_.push(m) && dropped_++ == 0) G6SHIM_WARN("acphal: message queue full, dropping messages");
}

}  // namespace g6shim::acphal
