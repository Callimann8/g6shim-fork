// SPDX-FileCopyrightText: 2026 gookie and g6shim contributors
// SPDX-License-Identifier: LGPL-3.0-or-later
#include "apu_memory.hpp"
#include "frame_clock.hpp"
#include "processor.hpp"
#include "title_memory.hpp"

#include <g6shim/log.hpp>

#include <atomic>
#include <memory>
#include <new>

namespace g6shim::acphal {

namespace {

constexpr HRESULT kSlotUnsupported = static_cast<HRESULT>(0x8AC80F0E);

bool apu_address_valid(std::uint32_t apu_address) {
    return apu_address != 0 && apu_blocks().contains(apu_address, 1);
}

void discard_buffer(const SubmitBufferPayload&) {}

class AcpHal final {
public:
    AcpHal() : processor_(&discard_buffer, &apu_address_valid), clock_([this] { processor_.run_frame(); }) {}

    virtual HRESULT Init(std::uint32_t command_entries, std::uint32_t message_entries) {
        HRESULT hr = processor_.init(command_entries, message_entries);
        if (SUCCEEDED(hr) && !clock_.start()) hr = E_OUTOFMEMORY;
        G6SHIM_INFO("IAcpHal::Init(%u, %u) -> 0x%08lx", command_entries, message_entries,
                    static_cast<unsigned long>(hr));
        return hr;
    }

    virtual HRESULT Shutdown() {
        clock_.stop();
        return processor_.request_shutdown();
    }

    virtual HRESULT Send(std::uint32_t command, std::uint32_t size, std::uint32_t target, const void* payload,
                         std::uint32_t flags) {
        switch (static_cast<Command>(command)) {
        case Command::SubmitBuffer: {
            SubmitBufferPayload p{};
            if (!copy_from_title(payload, &p, sizeof(p))) return bad_send(command, size, target, payload, flags);
            return processor_.submit_buffer(p);
        }
        case Command::Start: {
            StartPayload p{};
            if (!copy_from_title(payload, &p, sizeof(p))) return bad_send(command, size, target, payload, flags);
            return processor_.start(p);
        }
        }
        if (!unknown_logged_.exchange(true))
            G6SHIM_WARN("IAcpHal::Send(%u, %u, 0x%x, %p, 0x%x): unknown command -> E_NOTIMPL", command, size,
                        target, payload, flags);
        return E_NOTIMPL;
    }

    virtual bool NextMessage(void* out) {
        Message m{};
        if (!processor_.next_message(m)) return false;
        if (!out) return false;
        *static_cast<Message*>(out) = m;
        return true;
    }

    virtual std::uint32_t PendingMessages() { return processor_.pending_messages(); }

    virtual HRESULT Release() {
        delete this;
        return S_OK;
    }

    virtual HRESULT Slot6() { return kSlotUnsupported; }
    virtual HRESULT Slot7() { return kSlotUnsupported; }
    virtual HRESULT Slot8() { return kSlotUnsupported; }
    virtual HRESULT Slot9() { return kSlotUnsupported; }

private:
    HRESULT bad_send(std::uint32_t command, std::uint32_t size, std::uint32_t target, const void* payload,
                     std::uint32_t flags) {
        G6SHIM_WARN("IAcpHal::Send(%u, %u, 0x%x, %p, 0x%x): unreadable payload", command, size, target, payload,
                    flags);
        return E_INVALIDARG;
    }

    Processor processor_;
    FrameClock clock_;
    std::atomic<bool> unknown_logged_{false};
};

}  // namespace

}  // namespace g6shim::acphal

extern "C" HRESULT g6shim_AcpHalCreate(void** out) {
    if (!out) return E_INVALIDARG;
    auto* hal = new (std::nothrow) g6shim::acphal::AcpHal();
    *out = hal;
    G6SHIM_INFO("AcpHalCreate(%p) -> %p", static_cast<void*>(out), static_cast<void*>(hal));
    return hal ? S_OK : E_OUTOFMEMORY;
}
