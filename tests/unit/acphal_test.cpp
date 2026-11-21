// SPDX-FileCopyrightText: 2026 gookie and g6shim contributors
// SPDX-License-Identifier: LGPL-3.0-or-later
#include "check.hpp"

#include "apu_memory.hpp"
#include "shape.hpp"

#include <cstring>

using namespace g6shim::acphal;

TEST(acphal_block_set_bounds) {
    BlockSet set;
    set.insert(0x1000, 0x100);
    CHECK(set.contains(0x1000, 0x100));
    CHECK(set.contains(0x10ff, 1));
    CHECK(!set.contains(0x10ff, 2));
    CHECK(!set.contains(0x0fff, 1));
    CHECK(!set.contains(0x1100, 1));
    CHECK(!set.contains(0x1080, ~std::size_t(0)));
    CHECK(set.erase(0x1000));
    CHECK(!set.erase(0x1000));
    CHECK(!set.contains(0x1000, 1));
}

TEST(acphal_blocks_live_below_4gib) {
    void* p = reserve_below_4gib(0x3000);
    CHECK(p != nullptr);
    CHECK(reinterpret_cast<std::uintptr_t>(p) + 0x3000 <= kApuAddressLimit);
    std::memset(p, 0xab, 0x3000);
    release_block(p);
    CHECK(reserve_below_4gib(0) == nullptr);
    CHECK(reserve_below_4gib(kMaxApuBlock + 1) == nullptr);
}

TEST(acphal_shapes_allocate_and_release) {
    ShapeRequest request{};
    request.count[static_cast<std::size_t>(ShapeKind::Src)] = 512;
    request.count[static_cast<std::size_t>(ShapeKind::Dma)] = 1024;
    request.count[static_cast<std::size_t>(ShapeKind::Xma)] = 512;
    CHECK(allocate_shapes(request) == S_OK);
    for (std::size_t k = 0; k < kShapeKinds; ++k) {
        CHECK((request.array[k] != nullptr) == (request.count[k] != 0));
        if (request.array[k]) {
            CHECK(reinterpret_cast<std::uintptr_t>(request.array[k]) < kApuAddressLimit);
            std::memset(request.array[k], 0, std::size_t(request.count[k]) * kShapeStride[k]);
        }
    }
    ShapeRequest again = request;
    CHECK(allocate_shapes(again) == E_UNEXPECTED);
    CHECK(current_shapes().array[static_cast<std::size_t>(ShapeKind::Xma)] ==
          request.array[static_cast<std::size_t>(ShapeKind::Xma)]);
    CHECK(release_shapes() == S_OK);
    CHECK(release_shapes() == E_UNEXPECTED);
}

TEST(acphal_shapes_reject_huge_counts) {
    ShapeRequest request{};
    request.count[0] = kMaxShapeCount + 1;
    CHECK(allocate_shapes(request) == E_INVALIDARG);
    CHECK(request.array[0] == nullptr);
    CHECK(release_shapes() == E_UNEXPECTED);
}
