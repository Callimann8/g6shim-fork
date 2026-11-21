// SPDX-FileCopyrightText: 2026 gookie and g6shim contributors
// SPDX-License-Identifier: LGPL-3.0-or-later
#include "title_memory.hpp"

#include <windows.h>

#include <cstdint>
#include <cstring>

namespace g6shim::acphal {

namespace {

constexpr std::uintptr_t kLowestUserAddress = 0x10000;

bool readable_protection(DWORD protect) {
    if (protect & (PAGE_GUARD | PAGE_NOACCESS)) return false;
    return (protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE |
                       PAGE_EXECUTE_WRITECOPY)) != 0;
}

}  // namespace

bool readable(const void* address, std::size_t bytes) {
    auto at = reinterpret_cast<std::uintptr_t>(address);
    const auto end = at + bytes;
    if (at < kLowestUserAddress || end < at) return false;
    while (at < end) {
        MEMORY_BASIC_INFORMATION mbi{};
        if (!VirtualQuery(reinterpret_cast<const void*>(at), &mbi, sizeof(mbi))) return false;
        if (mbi.State != MEM_COMMIT || !readable_protection(mbi.Protect)) return false;
        at = reinterpret_cast<std::uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
    }
    return true;
}

bool copy_from_title(const void* source, void* destination, std::size_t bytes) {
    if (!readable(source, bytes)) return false;
    std::memcpy(destination, source, bytes);
    return true;
}

}  // namespace g6shim::acphal
