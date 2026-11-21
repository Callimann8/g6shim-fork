// SPDX-FileCopyrightText: 2026 gookie and g6shim contributors
// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <vector>

namespace g6shim::acphal {

template <typename T>
class BoundedQueue {
public:
    explicit BoundedQueue(std::size_t capacity = 0) : slots_(capacity) {}

    void reset(std::size_t capacity) {
        std::lock_guard<std::mutex> lock(mutex_);
        slots_.assign(capacity, T{});
        head_ = count_ = 0;
    }

    bool push(const T& item) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (count_ == slots_.size()) return false;
        slots_[(head_ + count_) % slots_.size()] = item;
        ++count_;
        return true;
    }

    std::optional<T> pop() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (count_ == 0) return std::nullopt;
        T item = slots_[head_];
        head_ = (head_ + 1) % slots_.size();
        --count_;
        return item;
    }

    std::size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return count_;
    }

private:
    mutable std::mutex mutex_;
    std::vector<T> slots_;
    std::size_t head_ = 0;
    std::size_t count_ = 0;
};

}  // namespace g6shim::acphal
