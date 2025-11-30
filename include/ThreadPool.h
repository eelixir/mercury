#pragma once

#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <atomic>
#include <type_traits>

namespace Mercury {

    /**
     * ThreadPool - A simple, efficient thread pool for parallel task execution
     * 
     * Features:
     * - Fixed number of worker threads
     * - Task queue with condition variable synchronization
     * - Support for returning futures from submitted tasks
     * - Graceful shutdown with task completion
     * 
     * Usage:
     *   ThreadPool pool(4);  // 4 worker threads
     *   
     *   // Submit task with return value
     *   auto future = pool.submit([]() { return 42; });
     *   int result = future.get();
     *   
     *   // Submit task without return value
     *   pool.submit([]() { doWork(); });
     *   
     *   // Wait for all tasks to complete
     *   pool.waitAll();
     */
    class ThreadPool {
    public:
        /**
         * Construct thread pool with specified number of threads
         * @param numThreads Number of worker threads (default: hardware concurrency)
         */
        explicit ThreadPool(size_t numThreads = 0)
            : stop_(false), activeTaskCount_(0) {
            
            if (numThreads == 0) {
                numThreads = std::thread::hardware_concurrency();
                if (numThreads == 0) numThreads = 4;  // Fallback
            }

            workers_.reserve(numThreads);
            for (size_t i = 0; i < numThreads; ++i) {
                workers_.emplace_back([this] { workerLoop(); });
            }
        }

        /**
         * Destructor - waits for all tasks to complete
         */
        ~ThreadPool() {
            shutdown();
        }

        // Disable copying
        ThreadPool(const ThreadPool&) = delete;
        ThreadPool& operator=(const ThreadPool&) = delete;

        /**
         * Submit a task to the pool
         * @param func The callable to execute
         * @param args Arguments to pass to the callable
         * @return std::future for the result
         */
        template<typename F, typename... Args>
        auto submit(F&& func, Args&&... args) 
            -> std::future<typename std::invoke_result<F, Args...>::type> {
            
            using ReturnType = typename std::invoke_result<F, Args...>::type;

            auto task = std::make_shared<std::packaged_task<ReturnType()>>(
                std::bind(std::forward<F>(func), std::forward<Args>(args)...)
            );

            std::future<ReturnType> result = task->get_future();

            {
                std::unique_lock<std::mutex> lock(queueMutex_);
                
                if (stop_) {
                    throw std::runtime_error("Cannot submit to stopped thread pool");
                }

                tasks_.emplace([task]() { (*task)(); });
                ++activeTaskCount_;
            }

            condition_.notify_one();
            return result;
        }

        /**
         * Submit a batch of tasks (more efficient than individual submits)
         * @param tasks Vector of callables to execute
         */
        template<typename F>
        void submitBatch(const std::vector<F>& tasks) {
            {
                std::unique_lock<std::mutex> lock(queueMutex_);
                
                if (stop_) {
                    throw std::runtime_error("Cannot submit to stopped thread pool");
                }

                for (const auto& task : tasks) {
                    tasks_.emplace(task);
                    ++activeTaskCount_;
                }
            }

            condition_.notify_all();
        }

        /**
         * Wait for all submitted tasks to complete
         */
        void waitAll() {
            std::unique_lock<std::mutex> lock(queueMutex_);
            completionCondition_.wait(lock, [this] {
                return tasks_.empty() && activeTaskCount_ == 0;
            });
        }

        /**
         * Get the number of worker threads
         */
        size_t size() const { return workers_.size(); }

        /**
         * Get the number of pending tasks
         */
        size_t pendingTasks() const {
            std::unique_lock<std::mutex> lock(queueMutex_);
            return tasks_.size();
        }

        /**
         * Check if the pool is stopped
         */
        bool isStopped() const { return stop_; }

        /**
         * Shutdown the pool (waits for all tasks to complete)
         */
        void shutdown() {
            {
                std::unique_lock<std::mutex> lock(queueMutex_);
                if (stop_) return;
                stop_ = true;
            }

            condition_.notify_all();

            for (std::thread& worker : workers_) {
                if (worker.joinable()) {
                    worker.join();
                }
            }
        }

    private:
        std::vector<std::thread> workers_;
        std::queue<std::function<void()>> tasks_;
        
        mutable std::mutex queueMutex_;
        std::condition_variable condition_;
        std::condition_variable completionCondition_;
        
        std::atomic<bool> stop_;
        std::atomic<size_t> activeTaskCount_;

        void workerLoop() {
            while (true) {
                std::function<void()> task;

                {
                    std::unique_lock<std::mutex> lock(queueMutex_);
                    
                    condition_.wait(lock, [this] {
                        return stop_ || !tasks_.empty();
                    });

                    if (stop_ && tasks_.empty()) {
                        return;
                    }

                    task = std::move(tasks_.front());
                    tasks_.pop();
                }

                // Execute task outside of lock
                task();

                // Signal task completion
                {
                    std::unique_lock<std::mutex> lock(queueMutex_);
                    --activeTaskCount_;
                    if (tasks_.empty() && activeTaskCount_ == 0) {
                        completionCondition_.notify_all();
                    }
                }
            }
        }
    };

    /**
     * ParallelFor - Execute a function over a range in parallel
     * 
     * Usage:
     *   ParallelFor::execute(0, 1000, [&](size_t i) {
     *       processItem(items[i]);
     *   });
     */
    class ParallelFor {
    public:
        /**
         * Execute a function over a range [start, end) using thread pool
         * @param start Starting index (inclusive)
         * @param end Ending index (exclusive)
         * @param func Function to execute for each index
         * @param pool Thread pool to use (optional, uses internal pool if not provided)
         * @param chunkSize Size of work chunks per thread (0 = auto)
         */
        template<typename F>
        static void execute(size_t start, size_t end, F&& func, 
                           ThreadPool* pool = nullptr, size_t chunkSize = 0) {
            if (start >= end) return;

            const size_t count = end - start;
            const size_t numThreads = pool ? pool->size() : std::thread::hardware_concurrency();
            
            // For small ranges, run sequentially
            if (count < numThreads * 2) {
                for (size_t i = start; i < end; ++i) {
                    func(i);
                }
                return;
            }

            // Calculate chunk size
            if (chunkSize == 0) {
                chunkSize = (count + numThreads - 1) / numThreads;
            }

            // Create internal pool if none provided
            std::unique_ptr<ThreadPool> internalPool;
            if (!pool) {
                internalPool = std::make_unique<ThreadPool>(numThreads);
                pool = internalPool.get();
            }

            // Submit chunks
            std::vector<std::future<void>> futures;
            futures.reserve((count + chunkSize - 1) / chunkSize);

            for (size_t chunkStart = start; chunkStart < end; chunkStart += chunkSize) {
                size_t chunkEnd = std::min(chunkStart + chunkSize, end);
                
                futures.push_back(pool->submit([=, &func]() {
                    for (size_t i = chunkStart; i < chunkEnd; ++i) {
                        func(i);
                    }
                }));
            }

            // Wait for all chunks
            for (auto& future : futures) {
                future.get();
            }
        }

        /**
         * Execute a function over a vector in parallel
         */
        template<typename T, typename F>
        static void executeVector(std::vector<T>& vec, F&& func, 
                                  ThreadPool* pool = nullptr, size_t chunkSize = 0) {
            execute(0, vec.size(), [&](size_t i) { func(vec[i]); }, pool, chunkSize);
        }

        /**
         * Transform a range in parallel and collect results
         */
        template<typename InputIt, typename F>
        static auto transform(InputIt first, InputIt last, F&& func, ThreadPool* pool = nullptr)
            -> std::vector<typename std::invoke_result<F, decltype(*first)>::type> {
            
            using ResultType = typename std::invoke_result<F, decltype(*first)>::type;
            
            const size_t count = std::distance(first, last);
            std::vector<ResultType> results(count);
            
            std::vector<std::pair<InputIt, ResultType*>> work;
            work.reserve(count);
            
            InputIt it = first;
            for (size_t i = 0; i < count; ++i, ++it) {
                work.emplace_back(it, &results[i]);
            }

            execute(0, count, [&](size_t i) {
                results[i] = func(*work[i].first);
            }, pool);

            return results;
        }
    };

    /**
     * SpinLock - A simple spinlock for short critical sections
     * More efficient than mutex for very short locks
     */
    class SpinLock {
    public:
        void lock() {
            while (flag_.test_and_set(std::memory_order_acquire)) {
                // Spin - optionally add yield or pause
                std::this_thread::yield();
            }
        }

        void unlock() {
            flag_.clear(std::memory_order_release);
        }

        bool try_lock() {
            return !flag_.test_and_set(std::memory_order_acquire);
        }

    private:
        std::atomic_flag flag_ = ATOMIC_FLAG_INIT;
    };

    /**
     * RAII lock guard for SpinLock
     */
    class SpinLockGuard {
    public:
        explicit SpinLockGuard(SpinLock& lock) : lock_(lock) {
            lock_.lock();
        }

        ~SpinLockGuard() {
            lock_.unlock();
        }

        SpinLockGuard(const SpinLockGuard&) = delete;
        SpinLockGuard& operator=(const SpinLockGuard&) = delete;

    private:
        SpinLock& lock_;
    };

}
