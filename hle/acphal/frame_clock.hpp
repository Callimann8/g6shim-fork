// SPDX-FileCopyrightText: 2026 gookie and g6shim contributors
// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <thread>

namespace g6shim::acphal {

constexpr std::uint32_t kOutputRate = 48000;
constexpr std::uint32_t kFrameSamples = 256;
constexpr LONGLONG kFramePeriod100ns = 10'000'000LL * kFrameSamples / kOutputRate;

class FrameClock {
public:
    explicit FrameClock(std::function<void()> on_frame);
    ~FrameClock();
    FrameClock(const FrameClock&) = delete;
    FrameClock& operator=(const FrameClock&) = delete;

    bool start();
    void stop();

private:
    void run();

    std::function<void()> on_frame_;
    HANDLE timer_ = nullptr;
    std::atomic<bool> running_{false};
    std::thread thread_;
};

}  // namespace g6shim::acphal
