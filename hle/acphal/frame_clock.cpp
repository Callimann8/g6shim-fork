// SPDX-FileCopyrightText: 2026 gookie and g6shim contributors
// SPDX-License-Identifier: LGPL-3.0-or-later
#include "frame_clock.hpp"

#include <utility>

namespace g6shim::acphal {

FrameClock::FrameClock(std::function<void()> on_frame) : on_frame_(std::move(on_frame)) {}

FrameClock::~FrameClock() { stop(); }

bool FrameClock::start() {
    if (running_) return true;
    timer_ = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    if (!timer_) return false;
    LARGE_INTEGER due{};
    due.QuadPart = -kFramePeriod100ns;
    if (!SetWaitableTimer(timer_, &due, 0, nullptr, nullptr, FALSE)) {
        CloseHandle(timer_);
        timer_ = nullptr;
        return false;
    }
    running_ = true;
    thread_ = std::thread([this] { run(); });
    return true;
}

void FrameClock::stop() {
    if (!running_.exchange(false)) return;
    if (thread_.joinable()) thread_.join();
    CloseHandle(timer_);
    timer_ = nullptr;
}

void FrameClock::run() {
    LARGE_INTEGER frequency{}, origin{}, now{};
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&origin);
    std::uint64_t frames = 0;
    while (running_) {
        WaitForSingleObject(timer_, INFINITE);
        QueryPerformanceCounter(&now);
        const auto elapsed100ns = (now.QuadPart - origin.QuadPart) * 10'000'000LL / frequency.QuadPart;
        const auto due_frames = static_cast<std::uint64_t>(elapsed100ns / kFramePeriod100ns);
        while (frames < due_frames && running_) {
            on_frame_();
            ++frames;
        }
        LARGE_INTEGER next{};
        next.QuadPart = -(kFramePeriod100ns - elapsed100ns % kFramePeriod100ns);
        SetWaitableTimer(timer_, &next, 0, nullptr, nullptr, FALSE);
    }
}

}  // namespace g6shim::acphal
