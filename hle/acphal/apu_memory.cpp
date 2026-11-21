// SPDX-FileCopyrightText: 2026 gookie and g6shim contributors
// SPDX-License-Identifier: LGPL-3.0-or-later
#include "apu_memory.hpp"

#include <windows.h>

namespace g6shim::acphal {

namespace {

std::uintptr_t align_up(std::uintptr_t value) {
    return (value + kAllocationGranularity - 1) & ~std::uintptr_t(kAllocationGranularity - 1);
}

}  // namespace

void* reserve_below_4gib(std::size_t bytes) {
    if (bytes == 0 || bytes > kMaxApuBlock) return nullptr;
    for (std::uintptr_t at = kApuSearchStart; at + bytes <= kApuAddressLimit; at += kAllocationGranularity) {
        MEMORY_BASIC_INFORMATION mbi{};
        if (!VirtualQuery(reinterpret_cast<void*>(at), &mbi, sizeof(mbi))) break;
        const auto region_end = reinterpret_cast<std::uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
        if (mbi.State != MEM_FREE || region_end - at < bytes) {
            at = align_up(region_end) - kAllocationGranularity;
            continue;
        }
        if (void* p = VirtualAlloc(reinterpret_cast<void*>(at), bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE))
            return p;
    }
    return nullptr;
}

void release_block(void* block) {
    if (block) VirtualFree(block, 0, MEM_RELEASE);
}

void BlockSet::insert(std::uintptr_t base, std::size_t bytes) {
    std::lock_guard<std::mutex> lock(mutex_);
    blocks_[base] = bytes;
}

bool BlockSet::erase(std::uintptr_t base) {
    std::lock_guard<std::mutex> lock(mutex_);
    return blocks_.erase(base) != 0;
}

bool BlockSet::contains(std::uintptr_t address, std::size_t bytes) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = blocks_.upper_bound(address);
    if (it == blocks_.begin()) return false;
    --it;
    const auto offset = address - it->first;
    return offset <= it->second && bytes <= it->second - offset;
}

std::size_t BlockSet::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return blocks_.size();
}

BlockSet& apu_blocks() {
    static BlockSet blocks;
    return blocks;
}

}  // namespace g6shim::acphal
