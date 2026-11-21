// SPDX-FileCopyrightText: 2026 gookie and g6shim contributors
// SPDX-License-Identifier: LGPL-3.0-or-later
#include "processor.hpp"

#include <g6shim/log.hpp>

#include <utility>

namespace g6shim::acphal {

Processor::Processor(BufferSink sink, AddressCheck valid_address)
    : sink_(std::move(sink)), valid_address_(std::move(valid_address)) {}

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

HRESULT Processor::submit_buffer(const SubmitBufferPayload& payload) {
    if (!initialized_) return E_UNEXPECTED;
    if (!valid_address_(payload.apu_address)) {
        G6SHIM_WARN("acphal: buffer at APU address 0x%08x is not APU memory", payload.apu_address);
        return E_INVALIDARG;
    }
    return commands_.push(payload) ? S_OK : E_OUTOFMEMORY;
}

HRESULT Processor::start(const StartPayload&) {
    return initialized_ ? S_OK : E_UNEXPECTED;
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
    while (auto buffer = commands_.pop()) {
        sink_(*buffer);
        post(MessageType::BufferDone, 0);
    }
    post(MessageType::Fence, ++fence_);
}

void Processor::post(MessageType type, std::uint64_t value) {
    Message m{};
    m.type = static_cast<std::uint32_t>(type);
    m.value = value;
    if (!messages_.push(m) && dropped_++ == 0) G6SHIM_WARN("acphal: message queue full, dropping messages");
}

}  // namespace g6shim::acphal
