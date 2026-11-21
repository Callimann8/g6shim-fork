// SPDX-FileCopyrightText: 2026 gookie and g6shim contributors
// SPDX-License-Identifier: LGPL-3.0-or-later
#include "apu_memory.hpp"
#include "shape.hpp"

#include <g6shim/log.hpp>

namespace {

using namespace g6shim::acphal;

constexpr std::uint32_t kMaxApuAlignment = kAllocationGranularity;

}  // namespace

extern "C" HRESULT g6shim_AcpHalAllocateShapeContexts(ShapeRequest* request, void* reserved) {
    if (!request) return E_INVALIDARG;
    const HRESULT hr = allocate_shapes(*request);
    G6SHIM_INFO("AcpHalAllocateShapeContexts(%p, %p) counts %u %u %u %u %u %u -> 0x%08lx", static_cast<void*>(request),
                reserved, request->count[0], request->count[1], request->count[2], request->count[3],
                request->count[4], request->count[5], static_cast<unsigned long>(hr));
    return hr;
}

extern "C" HRESULT g6shim_AcpHalReleaseShapeContexts() {
    const HRESULT hr = release_shapes();
    G6SHIM_INFO("AcpHalReleaseShapeContexts() -> 0x%08lx", static_cast<unsigned long>(hr));
    return hr;
}

extern "C" HRESULT g6shim_ApuAlloc(void** address, std::uint32_t* apu_address, std::uint32_t bytes,
                                   std::uint32_t alignment, std::uint32_t flags) {
    if (!address || bytes == 0 || bytes > kMaxApuBlock || alignment > kMaxApuAlignment ||
        (alignment & (alignment - 1)) != 0) {
        G6SHIM_WARN("ApuAlloc(%p, %p, 0x%x, %u, 0x%x): bad arguments", static_cast<void*>(address),
                    static_cast<void*>(apu_address), bytes, alignment, flags);
        return E_INVALIDARG;
    }
    void* block = reserve_below_4gib(bytes);
    if (!block) return E_OUTOFMEMORY;
    const auto base = reinterpret_cast<std::uintptr_t>(block);
    apu_blocks().insert(base, bytes);
    *address = block;
    if (apu_address) *apu_address = static_cast<std::uint32_t>(base);
    G6SHIM_INFO("ApuAlloc(0x%x, %u, 0x%x) -> %p", bytes, alignment, flags, block);
    return S_OK;
}

extern "C" HRESULT g6shim_ApuFree(void* address) {
    if (!apu_blocks().erase(reinterpret_cast<std::uintptr_t>(address))) {
        G6SHIM_WARN("ApuFree(%p): not an APU block", address);
        return E_INVALIDARG;
    }
    release_block(address);
    return S_OK;
}

extern "C" HRESULT g6shim_AcpHalCreate(void** out) {
    if (out) *out = nullptr;
    G6SHIM_INFO("AcpHalCreate(%p): no processor -> E_NOTIMPL", static_cast<void*>(out));
    return E_NOTIMPL;
}
