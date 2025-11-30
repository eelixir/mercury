/**
 * @file Profiler.h
 * @brief Lightweight instrumentation profiler for latency measurement
 * 
 * Provides high-resolution timing utilities for measuring critical path latency.
 * Essential for understanding tick-to-trade times in trading systems.
 * 
 * Features:
 * - Nanosecond-precision timestamps using std::chrono
 * - RAII-based scoped timers for automatic measurement
 * - Statistics collection (min, max, mean, percentiles)
 * - Zero-overhead in release builds when disabled
 * 
 * Usage:
 *   MERCURY_PROFILE_SCOPE("order_submission");  // Scoped measurement
 *   
 *   // Or manual timing:
 *   auto start = Mercury::Profiler::now();
 *   // ... operation ...
 *   auto elapsed = Mercury::Profiler::elapsedNanos(start);
 */

#pragma once

#include <chrono>
#include <string>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <cmath>
#include <mutex>
#include <iostream>
#include <iomanip>

// Enable/disable profiling at compile time
#ifndef MERCURY_PROFILING_ENABLED
    #ifdef NDEBUG
        #define MERCURY_PROFILING_ENABLED 0  // Disabled in release by default
    #else
        #define MERCURY_PROFILING_ENABLED 1  // Enabled in debug
    #endif
#endif

#if MERCURY_PROFILING_ENABLED
    #define MERCURY_PROFILE_SCOPE(name) \
        Mercury::ScopedTimer _mercury_timer_##__LINE__(name)
    #define MERCURY_PROFILE_FUNCTION() \
        MERCURY_PROFILE_SCOPE(__func__)
#else
    #define MERCURY_PROFILE_SCOPE(name) ((void)0)
    #define MERCURY_PROFILE_FUNCTION() ((void)0)
#endif

namespace Mercury {

    using Clock = std::chrono::high_resolution_clock;
    using TimePoint = Clock::time_point;
    using Nanoseconds = std::chrono::nanoseconds;
    using Microseconds = std::chrono::microseconds;

    /**
     * LatencyStats - Statistical summary of latency measurements
     */
    struct LatencyStats {
        size_t count = 0;
        int64_t minNanos = 0;
        int64_t maxNanos = 0;
        double meanNanos = 0.0;
        double stddevNanos = 0.0;
        int64_t p50Nanos = 0;   // Median
        int64_t p90Nanos = 0;
        int64_t p99Nanos = 0;
        int64_t p999Nanos = 0;  // Important for tail latency

        void print(const std::string& name) const {
            std::cout << std::fixed << std::setprecision(2);
            std::cout << "╔═══════════════════════════════════════════════════════════╗\n";
            std::cout << "║ " << std::left << std::setw(57) << name << " ║\n";
            std::cout << "╠═══════════════════════════════════════════════════════════╣\n";
            std::cout << "║ Samples: " << std::setw(49) << count << " ║\n";
            std::cout << "╠═══════════════════════════════════════════════════════════╣\n";
            std::cout << "║ Min:     " << std::setw(10) << minNanos << " ns  │  Mean:  " 
                      << std::setw(12) << meanNanos << " ns     ║\n";
            std::cout << "║ Max:     " << std::setw(10) << maxNanos << " ns  │  Stdev: " 
                      << std::setw(12) << stddevNanos << " ns     ║\n";
            std::cout << "╠═══════════════════════════════════════════════════════════╣\n";
            std::cout << "║ Percentiles:                                              ║\n";
            std::cout << "║   p50:   " << std::setw(10) << p50Nanos << " ns  (" 
                      << std::setw(8) << (p50Nanos / 1000.0) << " µs)              ║\n";
            std::cout << "║   p90:   " << std::setw(10) << p90Nanos << " ns  (" 
                      << std::setw(8) << (p90Nanos / 1000.0) << " µs)              ║\n";
            std::cout << "║   p99:   " << std::setw(10) << p99Nanos << " ns  (" 
                      << std::setw(8) << (p99Nanos / 1000.0) << " µs)              ║\n";
            std::cout << "║   p999:  " << std::setw(10) << p999Nanos << " ns  (" 
                      << std::setw(8) << (p999Nanos / 1000.0) << " µs)              ║\n";
            std::cout << "╚═══════════════════════════════════════════════════════════╝\n";
        }
    };

    /**
     * Profiler - Singleton class for collecting timing statistics
     */
    class Profiler {
    public:
        static Profiler& instance() {
            static Profiler profiler;
            return profiler;
        }

        // Get current timestamp
        static TimePoint now() {
            return Clock::now();
        }

        // Calculate elapsed time in nanoseconds
        static int64_t elapsedNanos(TimePoint start) {
            return std::chrono::duration_cast<Nanoseconds>(now() - start).count();
        }

        static int64_t elapsedMicros(TimePoint start) {
            return std::chrono::duration_cast<Microseconds>(now() - start).count();
        }

        // Record a timing sample
        void record(const std::string& name, int64_t nanos) {
            std::lock_guard<std::mutex> lock(mutex_);
            samples_[name].push_back(nanos);
        }

        // Calculate statistics for a named timer
        LatencyStats getStats(const std::string& name) const {
            std::lock_guard<std::mutex> lock(mutex_);
            
            auto it = samples_.find(name);
            if (it == samples_.end() || it->second.empty()) {
                return {};
            }

            std::vector<int64_t> sorted = it->second;
            std::sort(sorted.begin(), sorted.end());
            
            LatencyStats stats;
            stats.count = sorted.size();
            stats.minNanos = sorted.front();
            stats.maxNanos = sorted.back();
            
            // Calculate mean
            double sum = 0;
            for (int64_t val : sorted) {
                sum += val;
            }
            stats.meanNanos = sum / stats.count;
            
            // Calculate stddev
            double variance = 0;
            for (int64_t val : sorted) {
                double diff = val - stats.meanNanos;
                variance += diff * diff;
            }
            stats.stddevNanos = std::sqrt(variance / stats.count);
            
            // Calculate percentiles
            auto percentile = [&sorted](double p) {
                size_t idx = static_cast<size_t>((p / 100.0) * (sorted.size() - 1));
                return sorted[idx];
            };
            
            stats.p50Nanos = percentile(50);
            stats.p90Nanos = percentile(90);
            stats.p99Nanos = percentile(99);
            stats.p999Nanos = percentile(99.9);
            
            return stats;
        }

        // Print all statistics
        void printAll() const {
            std::lock_guard<std::mutex> lock(mutex_);
            std::cout << "\n╔═══════════════════════════════════════════════════════════╗\n";
            std::cout << "║              MERCURY PROFILER SUMMARY                     ║\n";
            std::cout << "╚═══════════════════════════════════════════════════════════╝\n\n";
            
            for (const auto& [name, _] : samples_) {
                getStats(name).print(name);
                std::cout << "\n";
            }
        }

        // Reset all samples
        void reset() {
            std::lock_guard<std::mutex> lock(mutex_);
            samples_.clear();
        }

        // Get sample count for a timer
        size_t getSampleCount(const std::string& name) const {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = samples_.find(name);
            return (it != samples_.end()) ? it->second.size() : 0;
        }

    private:
        Profiler() = default;
        mutable std::mutex mutex_;
        std::unordered_map<std::string, std::vector<int64_t>> samples_;
    };

    /**
     * ScopedTimer - RAII timer for automatic measurement
     */
    class ScopedTimer {
    public:
        explicit ScopedTimer(const char* name) 
            : name_(name), start_(Profiler::now()) {}

        ~ScopedTimer() {
            auto elapsed = Profiler::elapsedNanos(start_);
            Profiler::instance().record(name_, elapsed);
        }

        // Non-copyable
        ScopedTimer(const ScopedTimer&) = delete;
        ScopedTimer& operator=(const ScopedTimer&) = delete;

    private:
        const char* name_;
        TimePoint start_;
    };

    /**
     * ManualTimer - For more control over timing
     */
    class ManualTimer {
    public:
        void start() {
            start_ = Profiler::now();
        }

        int64_t stopNanos() const {
            return Profiler::elapsedNanos(start_);
        }

        int64_t stopMicros() const {
            return Profiler::elapsedMicros(start_);
        }

        int64_t stopAndRecord(const std::string& name) {
            auto elapsed = stopNanos();
            Profiler::instance().record(name, elapsed);
            return elapsed;
        }

    private:
        TimePoint start_;
    };

} // namespace Mercury
