// SPDX-FileCopyrightText: 2026 gookie and g6shim contributors
// SPDX-License-Identifier: LGPL-3.0-or-later
#include "apu_memory.hpp"
#include "engine.hpp"
#include "frame_clock.hpp"
#include "processor.hpp"
#include "shape.hpp"
#include "title_memory.hpp"

#include <g6shim/log.hpp>

#include <atomic>
#include <memory>
#include <new>

namespace g6shim::acphal {

namespace {

constexpr HRESULT kSlotUnsupported = static_cast<HRESULT>(0x8AC80F0E);

ContextArrays shape_contexts() {
    const ShapeArrays s = current_shapes();
    auto index = [](ShapeKind k) { return static_cast<std::size_t>(k); };
    ContextArrays a;
    a.xma = static_cast<XmaContext*>(s.array[index(ShapeKind::Xma)]);
    a.xma_count = a.xma ? s.count[index(ShapeKind::Xma)] : 0;
    a.src = static_cast<SrcContext*>(s.array[index(ShapeKind::Src)]);
    a.src_count = a.src ? s.count[index(ShapeKind::Src)] : 0;
    a.dma = static_cast<DmaContext*>(s.array[index(ShapeKind::Dma)]);
    a.dma_count = a.dma ? s.count[index(ShapeKind::Dma)] : 0;
    return a;
}

class AcpHal final {
public:
    AcpHal()
        : engine_(memory_, &shape_contexts, &make_xma_decoder), processor_(engine_),
          clock_([this] { processor_.run_frame(); }) {}

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

    virtual HRESULT Send(std::uint32_t command, std::uint64_t sequence, std::uint32_t target, const void* payload,
                         std::uint32_t flags) {
        if (!known_command(command)) {
            if (!unknown_logged_.exchange(true))
                G6SHIM_WARN("IAcpHal::Send(0x%x, %llu, 0x%x, %p, 0x%x): unknown command -> E_NOTIMPL", command,
                            static_cast<unsigned long long>(sequence), target, payload, flags);
            return E_NOTIMPL;
        }
        CommandRecord r{};
        r.command = static_cast<Command>(command);
        r.sequence = sequence;
        if (!copy_from_title(payload, r.payload, payload_bytes(r.command))) {
            G6SHIM_WARN("IAcpHal::Send(0x%x, %llu, 0x%x, %p, 0x%x): unreadable payload", command,
                        static_cast<unsigned long long>(sequence), target, payload, flags);
            return E_INVALIDARG;
        }
        return processor_.send(r);
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
    TitleApuMemory memory_;
    AudioEngine engine_;
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
