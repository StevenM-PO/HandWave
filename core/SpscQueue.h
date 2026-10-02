#pragma once

#include <atomic>
#include <cstddef>

namespace hw {

// Fixed-size, lock-free, single-producer / single-consumer queue.
//
// Exactly one thread may call push() and exactly one (other) thread may call
// pop(). Neither call ever blocks or allocates, so it is safe on the audio
// thread. Holds up to Capacity - 1 items.
template <typename T, size_t Capacity>
class SpscQueue {
    static_assert(Capacity >= 2 && (Capacity & (Capacity - 1)) == 0,
                  "Capacity must be a power of two");

public:
    // Returns false (and drops nothing) if the queue is full.
    bool push(const T& item)
    {
        const size_t head = head_.load(std::memory_order_relaxed);
        const size_t next = (head + 1) & (Capacity - 1);
        if (next == tail_.load(std::memory_order_acquire))
            return false;
        items_[head] = item;
        head_.store(next, std::memory_order_release);
        return true;
    }

    // Returns false if the queue is empty.
    bool pop(T& item)
    {
        const size_t tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire))
            return false;
        item = items_[tail];
        tail_.store((tail + 1) & (Capacity - 1), std::memory_order_release);
        return true;
    }

private:
    T items_[Capacity]{};
    std::atomic<size_t> head_{0}; // written by producer
    std::atomic<size_t> tail_{0}; // written by consumer
};

} // namespace hw
