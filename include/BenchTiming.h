#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>

namespace Mercury::BenchTiming {

#if defined(MERCURY_BENCH_TIMING) && MERCURY_BENCH_TIMING

enum class Category : size_t {
    PriceLevelIteration = 0,
    ObjectPool = 1,
    CallbackResult = 2,
    LadderMap = 3,
    HashLookup = 4,
    IntrusiveList = 5,
    Count = 6
};

struct Snapshot {
    uint64_t priceLevelIterationNs = 0;
    uint64_t objectPoolNs = 0;
    uint64_t callbackResultNs = 0;
    uint64_t ladderMapNs = 0;
    uint64_t hashLookupNs = 0;
    uint64_t intrusiveListNs = 0;
};

struct CounterSet {
    std::atomic<uint64_t> counters[static_cast<size_t>(Category::Count)] = {};
};

inline CounterSet& data() {
    static CounterSet instance;
    return instance;
}

inline void record(Category category, uint64_t nanos) {
    data().counters[static_cast<size_t>(category)].fetch_add(nanos, std::memory_order_relaxed);
}

inline void reset() {
    for (auto& counter : data().counters) {
        counter.store(0, std::memory_order_relaxed);
    }
}

inline Snapshot snapshot() {
    return Snapshot{
        data().counters[static_cast<size_t>(Category::PriceLevelIteration)]
            .load(std::memory_order_relaxed),
        data().counters[static_cast<size_t>(Category::ObjectPool)]
            .load(std::memory_order_relaxed),
        data().counters[static_cast<size_t>(Category::CallbackResult)]
            .load(std::memory_order_relaxed),
        data().counters[static_cast<size_t>(Category::LadderMap)]
            .load(std::memory_order_relaxed),
        data().counters[static_cast<size_t>(Category::HashLookup)]
            .load(std::memory_order_relaxed),
        data().counters[static_cast<size_t>(Category::IntrusiveList)]
            .load(std::memory_order_relaxed),
    };
}

class ScopedTimer {
public:
    explicit ScopedTimer(Category category)
        : category_(category),
          start_(Clock::now()) {}

    ~ScopedTimer() {
        const auto elapsed =
            std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start_).count();
        record(category_, static_cast<uint64_t>(elapsed));
    }

    ScopedTimer(const ScopedTimer&) = delete;
    ScopedTimer& operator=(const ScopedTimer&) = delete;

private:
    using Clock = std::chrono::steady_clock;

    Category category_;
    Clock::time_point start_;
};

#else

enum class Category : size_t {
    PriceLevelIteration = 0,
    ObjectPool = 1,
    CallbackResult = 2,
    LadderMap = 3,
    HashLookup = 4,
    IntrusiveList = 5,
    Count = 6
};

struct Snapshot {
    uint64_t priceLevelIterationNs = 0;
    uint64_t objectPoolNs = 0;
    uint64_t callbackResultNs = 0;
    uint64_t ladderMapNs = 0;
    uint64_t hashLookupNs = 0;
    uint64_t intrusiveListNs = 0;
};

inline void reset() {}
inline Snapshot snapshot() { return {}; }

class ScopedTimer {
public:
    explicit ScopedTimer(Category) {}
};

#endif

}  // namespace Mercury::BenchTiming

#define MERCURY_BENCH_CONCAT_IMPL(x, y) x##y
#define MERCURY_BENCH_CONCAT(x, y) MERCURY_BENCH_CONCAT_IMPL(x, y)

#if defined(MERCURY_BENCH_TIMING) && MERCURY_BENCH_TIMING
#define MERCURY_BENCH_SCOPE(category) \
    Mercury::BenchTiming::ScopedTimer MERCURY_BENCH_CONCAT(_mercury_bench_timer_, __LINE__)(category)
#else
#define MERCURY_BENCH_SCOPE(category) ((void)0)
#endif
