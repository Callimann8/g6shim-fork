// SPDX-FileCopyrightText: 2026 gookie and g6shim contributors
// SPDX-License-Identifier: LGPL-3.0-or-later
#include "shape.hpp"

#include "apu_memory.hpp"

#include <g6shim/log.hpp>

#include <mutex>

namespace g6shim::acphal {

namespace {

std::mutex g_mutex;
ShapeArrays g_shapes;
bool g_allocated = false;

void free_all(ShapeArrays& shapes) {
    for (auto& p : shapes.array) {
        release_block(p);
        p = nullptr;
    }
    shapes.count = {};
}

}  // namespace

HRESULT allocate_shapes(ShapeRequest& request) {
    for (std::size_t k = 0; k < kShapeKinds; ++k) {
        if (request.count[k] > kMaxShapeCount) return E_INVALIDARG;
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_allocated) return E_UNEXPECTED;
    ShapeArrays shapes;
    for (std::size_t k = 0; k < kShapeKinds; ++k) {
        shapes.count[k] = request.count[k];
        if (!request.count[k]) continue;
        shapes.array[k] = reserve_below_4gib(std::size_t(request.count[k]) * kShapeStride[k]);
        if (!shapes.array[k]) {
            free_all(shapes);
            return E_OUTOFMEMORY;
        }
    }
    for (std::size_t k = 0; k < kShapeKinds; ++k) request.array[k] = shapes.array[k];
    g_shapes = shapes;
    g_allocated = true;
    return S_OK;
}

HRESULT release_shapes() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_allocated) return E_UNEXPECTED;
    free_all(g_shapes);
    g_allocated = false;
    return S_OK;
}

ShapeArrays current_shapes() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_shapes;
}

}  // namespace g6shim::acphal
