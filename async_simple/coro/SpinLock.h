/*
 * Copyright (c) 2022, Alibaba Group Holding Limited;
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#ifndef ASYNC_SIMPLE_CORO_SPIN_LOCK_H
#define ASYNC_SIMPLE_CORO_SPIN_LOCK_H

#include <async_simple/coro/Lazy.h>
#include <thread>

namespace async_simple {
namespace coro {

class SpinLock {
public:
    explicit SpinLock(std::int32_t count = 1024) noexcept
        : _spinCount(count), _locked(false) {}

    bool tryLock() noexcept {
        return !_locked.exchange(true, std::memory_order_acquire);
    }

    Lazy<> coLock() noexcept {
        auto counter = _spinCount;
        while (!tryLock()) {
            while (_locked.load(std::memory_order_relaxed)) {
                if (counter-- <= 0) {
                    co_await Yield{};
                    counter = _spinCount;
                }
            }
        }
        co_return;
    }

    void lock() noexcept {
        auto counter = _spinCount;
        while (!tryLock()) {
            while (_locked.load(std::memory_order_relaxed)) {
                if (counter-- <= 0) {
                    //  是将当前线程所抢到的CPU”时间片A”让渡给其他线程
                    // (其他线程会争抢”时间片A”, 注意: 此时”当前线程”不参与争抢).
                    // 等到其他线程使用完”时间片A”后, 
                    // 再由操作系统调度, 当前线程再和其他线程一起开始抢CPU时间片.
                    std::this_thread::yield(); 
                    counter = _spinCount;
                }
            }
        }
    }

    void unlock() noexcept { _locked.store(false, std::memory_order_release); }

    Lazy<std::unique_lock<SpinLock>> coScopedLock() noexcept {
        co_await coLock();
        co_return std::unique_lock<SpinLock>{*this, std::adopt_lock};
    }

private:
    // use spin count to avoid current coroutine occupy current thread too long, 
    // and starve the other coroutines.
    std::int32_t _spinCount;
    std::atomic<bool> _locked;
};

class ScopedSpinLock {
public:
    explicit ScopedSpinLock(SpinLock &lock) : _lock(lock) { _lock.lock(); }
    ~ScopedSpinLock() { _lock.unlock(); }

private:
    ScopedSpinLock(const ScopedSpinLock &) = delete;
    ScopedSpinLock &operator=(const ScopedSpinLock &) = delete;
    SpinLock &_lock;
};

}  // namespace coro
}  // namespace async_simple

#endif  // ASYNC_SIMPLE_CORO_SPIN_LOCK_H
