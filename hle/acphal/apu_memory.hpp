// SPDX-FileCopyrightText: 2026 gookie and g6shim contributors
// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>

namespace g6shim::acphal {

constexpr std::uintptr_t kApuAddressLimit = 0x1'0000'0000ull;
constexpr std::uintptr_t kApuSearchStart = 0x1000'0000ull;
constexpr std::size_t kAllocationGranularity = 0x10000;
constexpr std::size_t kMaxApuBlock = 0x1000'0000;

void* reserve_below_4gib(std::size_t bytes);
void release_block(void* block);

class BlockSet {
public:
    void insert(std::uintptr_t base, std::size_t bytes);
    bool erase(std::uintptr_t base);
    bool contains(std::uintptr_t address, std::size_t bytes) const;
    std::size_t size() const;

private:
    mutable std::mutex mutex_;
    std::map<std::uintptr_t, std::size_t> blocks_;
};

BlockSet& apu_blocks();

}  // namespace g6shim::acphal
