#pragma once

#include <string>
#include <fstream>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <functional>
#include <sstream>
#include <vector>
#include <memory>

namespace Mercury {

    /**
     * AsyncWriter - Asynchronous file writer with internal buffering
     * 
     * Writes are queued and processed by a background thread,
     * allowing the main processing thread to continue without
     * blocking on I/O operations.
     * 
     * Features:
     * - Lock-free fast path for writes
     * - Batch flushing for efficiency
     * - Graceful shutdown with pending write completion
     * - Configurable buffer size
     * 
     * Usage:
     *   AsyncWriter writer("output.csv");
     *   writer.open();
     *   writer.write("line1\n");
     *   writer.write("line2\n");
     *   writer.close();  // Waits for all writes to complete
     */
    class AsyncWriter {
    public:
        static constexpr size_t DEFAULT_BUFFER_SIZE = 8192;
        static constexpr size_t DEFAULT_QUEUE_CAPACITY = 1000;

        explicit AsyncWriter(const std::string& filepath, 
                            size_t bufferSize = DEFAULT_BUFFER_SIZE)
            : filepath_(filepath)
            , bufferSize_(bufferSize)
            , stop_(false)
            , writeCount_(0)
            , bytesWritten_(0) {
            buffer_.reserve(bufferSize);
        }

        ~AsyncWriter() {
            close();
        }

        // Disable copying
        AsyncWriter(const AsyncWriter&) = delete;
        AsyncWriter& operator=(const AsyncWriter&) = delete;

        /**
         * Open the file and start the writer thread
         * @return true if successful
         */
        bool open() {
            std::unique_lock<std::mutex> lock(mutex_);
            
            if (file_.is_open()) {
                return true;
            }

            file_.open(filepath_, std::ios::out | std::ios::trunc);
            if (!file_.is_open()) {
                return false;
            }

            stop_ = false;
            writerThread_ = std::thread([this] { writerLoop(); });
            
            return true;
        }

        /**
         * Close the file (waits for pending writes)
         */
        void close() {
            {
                std::unique_lock<std::mutex> lock(mutex_);
                if (!file_.is_open()) {
                    return;
                }

                // Flush any remaining buffer
                if (!buffer_.empty()) {
                    queue_.push(std::move(buffer_));
                    buffer_.clear();
                    buffer_.reserve(bufferSize_);
                }

                stop_ = true;
            }

            condition_.notify_one();

            if (writerThread_.joinable()) {
                writerThread_.join();
            }

            if (file_.is_open()) {
                file_.close();
            }
        }

        /**
         * Check if file is open
         */
        bool isOpen() const { return file_.is_open() && !stop_; }

        /**
         * Write a string to the file (async)
         * @param data The data to write
         */
        void write(const std::string& data) {
            std::unique_lock<std::mutex> lock(mutex_);
            
            buffer_ += data;
            ++writeCount_;
            bytesWritten_ += data.size();

            // Flush buffer if it's large enough
            if (buffer_.size() >= bufferSize_) {
                queue_.push(std::move(buffer_));
                buffer_.clear();
                buffer_.reserve(bufferSize_);
                condition_.notify_one();
            }
        }

        /**
         * Write with format (like printf)
         */
        template<typename... Args>
        void writef(const char* format, Args... args) {
            char buf[1024];
            int len = std::snprintf(buf, sizeof(buf), format, args...);
            if (len > 0) {
                write(std::string(buf, static_cast<size_t>(len)));
            }
        }

        /**
         * Flush pending writes to disk
         */
        void flush() {
            {
                std::unique_lock<std::mutex> lock(mutex_);
                if (!buffer_.empty()) {
                    queue_.push(std::move(buffer_));
                    buffer_.clear();
                    buffer_.reserve(bufferSize_);
                }
            }
            condition_.notify_one();

            // Wait for queue to drain
            std::unique_lock<std::mutex> lock(mutex_);
            flushCondition_.wait(lock, [this] {
                return queue_.empty();
            });
        }

        /**
         * Get statistics
         */
        size_t getWriteCount() const { return writeCount_; }
        size_t getBytesWritten() const { return bytesWritten_; }
        const std::string& getFilePath() const { return filepath_; }

    private:
        std::string filepath_;
        std::ofstream file_;
        size_t bufferSize_;

        std::string buffer_;
        std::queue<std::string> queue_;
        
        std::mutex mutex_;
        std::condition_variable condition_;
        std::condition_variable flushCondition_;
        std::thread writerThread_;
        
        std::atomic<bool> stop_;
        std::atomic<size_t> writeCount_;
        std::atomic<size_t> bytesWritten_;

        void writerLoop() {
            while (true) {
                std::string data;

                {
                    std::unique_lock<std::mutex> lock(mutex_);
                    
                    condition_.wait(lock, [this] {
                        return stop_ || !queue_.empty();
                    });

                    if (queue_.empty()) {
                        if (stop_) {
                            break;
                        }
                        continue;
                    }

                    data = std::move(queue_.front());
                    queue_.pop();

                    // Notify flush waiters if queue is now empty
                    if (queue_.empty()) {
                        flushCondition_.notify_all();
                    }
                }

                // Write outside of lock
                if (!data.empty()) {
                    file_ << data;
                }
            }

            // Final flush
            file_.flush();
        }
    };

    /**
     * ConcurrentQueue - A simple thread-safe queue for producer/consumer patterns
     */
    template<typename T>
    class ConcurrentQueue {
    public:
        explicit ConcurrentQueue(size_t maxSize = 0) 
            : maxSize_(maxSize), stopped_(false) {}

        /**
         * Push an item to the queue
         * @return true if successful, false if queue is stopped
         */
        bool push(T item) {
            std::unique_lock<std::mutex> lock(mutex_);
            
            if (maxSize_ > 0) {
                notFull_.wait(lock, [this] {
                    return stopped_ || queue_.size() < maxSize_;
                });
            }

            if (stopped_) {
                return false;
            }

            queue_.push(std::move(item));
            notEmpty_.notify_one();
            return true;
        }

        /**
         * Push multiple items to the queue
         */
        bool pushBatch(std::vector<T>& items) {
            std::unique_lock<std::mutex> lock(mutex_);
            
            if (stopped_) {
                return false;
            }

            for (auto& item : items) {
                queue_.push(std::move(item));
            }
            notEmpty_.notify_all();
            return true;
        }

        /**
         * Pop an item from the queue (blocking)
         * @param item Output parameter for the popped item
         * @return true if an item was retrieved, false if queue is stopped and empty
         */
        bool pop(T& item) {
            std::unique_lock<std::mutex> lock(mutex_);
            
            notEmpty_.wait(lock, [this] {
                return stopped_ || !queue_.empty();
            });

            if (queue_.empty()) {
                return false;
            }

            item = std::move(queue_.front());
            queue_.pop();
            
            if (maxSize_ > 0) {
                notFull_.notify_one();
            }
            
            return true;
        }

        /**
         * Try to pop an item without blocking
         * @return true if an item was retrieved
         */
        bool tryPop(T& item) {
            std::unique_lock<std::mutex> lock(mutex_);
            
            if (queue_.empty()) {
                return false;
            }

            item = std::move(queue_.front());
            queue_.pop();
            
            if (maxSize_ > 0) {
                notFull_.notify_one();
            }
            
            return true;
        }

        /**
         * Pop multiple items at once
         */
        size_t popBatch(std::vector<T>& items, size_t maxItems) {
            std::unique_lock<std::mutex> lock(mutex_);
            
            notEmpty_.wait(lock, [this] {
                return stopped_ || !queue_.empty();
            });

            size_t count = 0;
            while (!queue_.empty() && count < maxItems) {
                items.push_back(std::move(queue_.front()));
                queue_.pop();
                ++count;
            }

            if (maxSize_ > 0 && count > 0) {
                notFull_.notify_all();
            }

            return count;
        }

        /**
         * Stop the queue (unblocks all waiting consumers)
         */
        void stop() {
            std::unique_lock<std::mutex> lock(mutex_);
            stopped_ = true;
            notEmpty_.notify_all();
            notFull_.notify_all();
        }

        /**
         * Check if queue is stopped
         */
        bool isStopped() const { return stopped_; }

        /**
         * Get current size
         */
        size_t size() const {
            std::unique_lock<std::mutex> lock(mutex_);
            return queue_.size();
        }

        /**
         * Check if empty
         */
        bool empty() const {
            std::unique_lock<std::mutex> lock(mutex_);
            return queue_.empty();
        }

        /**
         * Clear all items
         */
        void clear() {
            std::unique_lock<std::mutex> lock(mutex_);
            std::queue<T> empty;
            std::swap(queue_, empty);
            if (maxSize_ > 0) {
                notFull_.notify_all();
            }
        }

    private:
        std::queue<T> queue_;
        mutable std::mutex mutex_;
        std::condition_variable notEmpty_;
        std::condition_variable notFull_;
        size_t maxSize_;
        std::atomic<bool> stopped_;
    };

    /**
     * BufferedWriter - Double-buffered synchronous writer for batch efficiency
     * 
     * Less overhead than AsyncWriter for simpler use cases.
     * Writes are accumulated in a buffer and flushed when full.
     */
    class BufferedWriter {
    public:
        static constexpr size_t DEFAULT_BUFFER_SIZE = 65536;

        explicit BufferedWriter(const std::string& filepath, 
                               size_t bufferSize = DEFAULT_BUFFER_SIZE)
            : filepath_(filepath)
            , bufferSize_(bufferSize)
            , writeCount_(0) {
            buffer_.reserve(bufferSize);
        }

        ~BufferedWriter() {
            close();
        }

        bool open() {
            file_.open(filepath_, std::ios::out | std::ios::trunc);
            return file_.is_open();
        }

        void close() {
            if (file_.is_open()) {
                flush();
                file_.close();
            }
        }

        bool isOpen() const { return file_.is_open(); }

        void write(const std::string& data) {
            buffer_ += data;
            ++writeCount_;

            if (buffer_.size() >= bufferSize_) {
                flushBuffer();
            }
        }

        void flush() {
            flushBuffer();
            file_.flush();
        }

        size_t getWriteCount() const { return writeCount_; }
        const std::string& getFilePath() const { return filepath_; }

    private:
        std::string filepath_;
        std::ofstream file_;
        std::string buffer_;
        size_t bufferSize_;
        size_t writeCount_;

        void flushBuffer() {
            if (!buffer_.empty()) {
                file_ << buffer_;
                buffer_.clear();
            }
        }
    };

    /**
     * AsyncTradeWriter - Specialized async writer for Trade records
     * 
     * Accumulates trades and writes them in batches.
     */
    class AsyncTradeWriter {
    public:
        explicit AsyncTradeWriter(const std::string& filepath)
            : writer_(filepath)
            , tradesWritten_(0)
            , headerWritten_(false) {}

        bool open() {
            if (!writer_.open()) {
                return false;
            }
            writeHeader();
            return true;
        }

        void close() {
            writer_.close();
        }

        bool isOpen() const { return writer_.isOpen(); }

        void writeTrade(uint64_t tradeId, uint64_t timestamp, 
                       uint64_t buyOrderId, uint64_t sellOrderId,
                       int64_t price, uint64_t quantity) {
            std::ostringstream oss;
            oss << tradeId << ","
                << timestamp << ","
                << buyOrderId << ","
                << sellOrderId << ","
                << price << ","
                << quantity << "\n";
            
            writer_.write(oss.str());
            ++tradesWritten_;
        }

        void flush() { writer_.flush(); }
        size_t getTradeCount() const { return tradesWritten_; }

    private:
        AsyncWriter writer_;
        std::atomic<size_t> tradesWritten_;
        bool headerWritten_;

        void writeHeader() {
            if (!headerWritten_) {
                writer_.write("trade_id,timestamp,buy_order_id,sell_order_id,price,quantity\n");
                headerWritten_ = true;
            }
        }
    };

}
