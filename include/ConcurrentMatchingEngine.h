#pragma once

#include "MatchingEngine.h"
#include "ThreadPool.h"
#include "AsyncWriter.h"
#include <unordered_map>
#include <shared_mutex>
#include <memory>
#include <atomic>

namespace Mercury {

    /**
     * ConcurrentMatchingEngine - Thread-safe matching engine with sharding support
     * 
     * Provides multiple strategies for concurrent order processing:
     * 
     * 1. Single-threaded (default): Traditional single-threaded matching
     *    - Best for: Low volume, sequential order dependencies
     * 
     * 2. Symbol-sharded: Separate order book per symbol, parallel processing
     *    - Best for: Multi-symbol trading with high volume
     *    - Each symbol is processed by its own thread
     * 
     * 3. Async I/O: Single-threaded matching with async callbacks
     *    - Best for: I/O-bound workloads with heavy logging/writing
     *    - Matching is synchronous, but callbacks run async
     * 
     * Usage:
     *   ConcurrentMatchingEngine engine;
     *   engine.setNumSymbolShards(4);  // Optional: enable sharding
     *   
     *   // Submit orders (thread-safe)
     *   auto result = engine.submitOrder(order);
     *   
     *   // Or batch submit for efficiency
     *   engine.submitOrders(orders, results);
     */
    class ConcurrentMatchingEngine {
    public:
        using TradeCallback = std::function<void(const Trade&)>;
        using ExecutionCallback = std::function<void(const ExecutionResult&)>;
        using AsyncTradeCallback = std::function<void(std::vector<Trade>&&)>;

        /**
         * Processing mode for the engine
         */
        enum class Mode {
            SingleThreaded,     // Traditional single-threaded processing
            SymbolSharded,      // Parallel processing by symbol
            AsyncCallbacks      // Single-threaded matching, async callbacks
        };

        ConcurrentMatchingEngine() 
            : mode_(Mode::SingleThreaded)
            , numShards_(1)
            , tradeCount_(0)
            , totalVolume_(0)
            , ordersProcessed_(0) {
            
            engines_.push_back(std::make_unique<MatchingEngine>());
        }

        ~ConcurrentMatchingEngine() {
            shutdown();
        }

        // Disable copying
        ConcurrentMatchingEngine(const ConcurrentMatchingEngine&) = delete;
        ConcurrentMatchingEngine& operator=(const ConcurrentMatchingEngine&) = delete;

        /**
         * Set the processing mode
         * Must be called before any orders are submitted
         */
        void setMode(Mode mode) {
            std::unique_lock<std::shared_mutex> lock(mutex_);
            mode_.store(mode, std::memory_order_release);
            
            if (mode == Mode::AsyncCallbacks && !callbackPool_) {
                callbackPool_ = std::make_unique<ThreadPool>(2);
            }
        }

        /**
         * Set number of symbol shards (enables symbol-sharded mode)
         * @param numShards Number of parallel order books (0 = auto)
         */
        void setNumSymbolShards(size_t numShards) {
            std::unique_lock<std::shared_mutex> lock(mutex_);
            
            if (numShards == 0) {
                numShards = std::thread::hardware_concurrency();
                if (numShards == 0) numShards = 4;
            }
            
            numShards_ = numShards;
            mode_.store(Mode::SymbolSharded, std::memory_order_release);
            
            // Create sharded engines
            engines_.clear();
            engines_.reserve(numShards);
            shardLocks_.clear();
            shardLocks_.reserve(numShards);
            
            for (size_t i = 0; i < numShards; ++i) {
                engines_.push_back(std::make_unique<MatchingEngine>());
                shardLocks_.push_back(std::make_unique<std::mutex>());
            }
        }

        /**
         * Submit a single order (thread-safe)
         */
        ExecutionResult submitOrder(Order order) {
            // Read mode atomically first to determine locking strategy
            Mode currentMode = mode_.load(std::memory_order_acquire);
            
            if (currentMode == Mode::SingleThreaded) {
                std::unique_lock<std::shared_mutex> lock(mutex_);
                return submitOrderInternal(order, 0);
            }
            else if (currentMode == Mode::SymbolSharded) {
                size_t shard = getShardForOrder(order);
                std::lock_guard<std::mutex> lock(*shardLocks_[shard]);
                return submitOrderInternal(order, shard);
            }
            else {  // AsyncCallbacks
                std::unique_lock<std::shared_mutex> lock(mutex_);
                auto result = submitOrderInternal(order, 0);
                
                // Fire async callbacks
                if (!result.trades.empty() && asyncTradeCallback_) {
                    auto trades = std::move(result.trades);
                    callbackPool_->submit([this, trades = std::move(trades)]() mutable {
                        asyncTradeCallback_(std::move(trades));
                    });
                    result.trades.clear();
                }
                
                return result;
            }
        }

        /**
         * Submit multiple orders (parallel when sharded)
         * @param orders Vector of orders to submit
         * @param results Output vector for execution results
         */
        void submitOrders(const std::vector<Order>& orders, 
                         std::vector<ExecutionResult>& results) {
            results.resize(orders.size());
            
            Mode currentMode = mode_.load(std::memory_order_acquire);
            if (currentMode == Mode::SingleThreaded || currentMode == Mode::AsyncCallbacks) {
                std::unique_lock<std::shared_mutex> lock(mutex_);
                for (size_t i = 0; i < orders.size(); ++i) {
                    results[i] = submitOrderInternal(orders[i], 0);
                }
            }
            else {  // SymbolSharded - process in parallel
                // Group orders by shard
                std::vector<std::vector<size_t>> shardOrders(numShards_);
                for (size_t i = 0; i < orders.size(); ++i) {
                    size_t shard = getShardForOrder(orders[i]);
                    shardOrders[shard].push_back(i);
                }
                
                // Process shards in parallel
                std::vector<std::future<void>> futures;
                futures.reserve(numShards_);
                
                for (size_t shard = 0; shard < numShards_; ++shard) {
                    if (shardOrders[shard].empty()) continue;
                    
                    futures.push_back(std::async(std::launch::async, 
                        [this, shard, &orders, &results, &shardOrders]() {
                            std::lock_guard<std::mutex> lock(*shardLocks_[shard]);
                            for (size_t idx : shardOrders[shard]) {
                                results[idx] = submitOrderInternal(orders[idx], shard);
                            }
                        }));
                }
                
                // Wait for all shards
                for (auto& future : futures) {
                    future.get();
                }
            }
        }

        /**
         * Submit orders with thread pool (for large batches)
         */
        void submitOrdersParallel(const std::vector<Order>& orders,
                                  std::vector<ExecutionResult>& results,
                                  ThreadPool& pool) {
            if (mode_.load(std::memory_order_acquire) != Mode::SymbolSharded) {
                // Fall back to serial processing
                submitOrders(orders, results);
                return;
            }
            
            results.resize(orders.size());
            
            // Group orders by shard
            std::vector<std::vector<size_t>> shardOrders(numShards_);
            for (size_t i = 0; i < orders.size(); ++i) {
                size_t shard = getShardForOrder(orders[i]);
                shardOrders[shard].push_back(i);
            }
            
            // Submit shard processing to thread pool
            std::vector<std::future<void>> futures;
            futures.reserve(numShards_);
            
            for (size_t shard = 0; shard < numShards_; ++shard) {
                if (shardOrders[shard].empty()) continue;
                
                futures.push_back(pool.submit(
                    [this, shard, &orders, &results, &shardOrders]() {
                        std::lock_guard<std::mutex> lock(*shardLocks_[shard]);
                        for (size_t idx : shardOrders[shard]) {
                            results[idx] = submitOrderInternal(orders[idx], shard);
                        }
                    }));
            }
            
            // Wait for all shards
            for (auto& future : futures) {
                future.get();
            }
        }

        /**
         * Cancel an order by ID
         */
        ExecutionResult cancelOrder(uint64_t orderId, uint64_t symbolId = 0) {
            Mode currentMode = mode_.load(std::memory_order_acquire);
            if (currentMode == Mode::SingleThreaded || currentMode == Mode::AsyncCallbacks) {
                std::unique_lock<std::shared_mutex> lock(mutex_);
                return engines_[0]->cancelOrder(orderId);
            }
            else {
                size_t shard = symbolId % numShards_;
                std::lock_guard<std::mutex> lock(*shardLocks_[shard]);
                return engines_[shard]->cancelOrder(orderId);
            }
        }

        // Callbacks
        void setTradeCallback(TradeCallback callback) { 
            tradeCallback_ = std::move(callback); 
            for (auto& engine : engines_) {
                engine->setTradeCallback(tradeCallback_);
            }
        }

        void setExecutionCallback(ExecutionCallback callback) { 
            executionCallback_ = std::move(callback); 
            for (auto& engine : engines_) {
                engine->setExecutionCallback(executionCallback_);
            }
        }

        void setAsyncTradeCallback(AsyncTradeCallback callback) {
            asyncTradeCallback_ = std::move(callback);
        }

        // Statistics (aggregated across shards)
        uint64_t getTradeCount() const { return tradeCount_.load(); }
        uint64_t getTotalVolume() const { return totalVolume_.load(); }
        uint64_t getOrdersProcessed() const { return ordersProcessed_.load(); }

        /**
         * Get the primary order book (for single-threaded mode)
         */
        const OrderBook& getOrderBook() const { 
            return engines_[0]->getOrderBook(); 
        }

        OrderBook& getOrderBook() { 
            return engines_[0]->getOrderBook(); 
        }

        /**
         * Get order book for a specific shard
         */
        const OrderBook& getOrderBook(size_t shard) const {
            return engines_[shard % engines_.size()]->getOrderBook();
        }

        /**
         * Get aggregate order count across all shards
         */
        size_t getTotalOrderCount() const {
            size_t count = 0;
            for (const auto& engine : engines_) {
                count += engine->getOrderBook().getOrderCount();
            }
            return count;
        }

        /**
         * Get number of active shards
         */
        size_t getNumShards() const { return numShards_; }

        /**
         * Get processing mode
         */
        Mode getMode() const { return mode_.load(std::memory_order_acquire); }

        /**
         * Shutdown the engine (waits for pending async operations)
         */
        void shutdown() {
            if (callbackPool_) {
                callbackPool_->waitAll();
                callbackPool_.reset();
            }
        }

    private:
        std::atomic<Mode> mode_;
        size_t numShards_;
        
        std::vector<std::unique_ptr<MatchingEngine>> engines_;
        std::vector<std::unique_ptr<std::mutex>> shardLocks_;
        mutable std::shared_mutex mutex_;
        
        std::unique_ptr<ThreadPool> callbackPool_;
        
        std::atomic<uint64_t> tradeCount_;
        std::atomic<uint64_t> totalVolume_;
        std::atomic<uint64_t> ordersProcessed_;
        
        TradeCallback tradeCallback_;
        ExecutionCallback executionCallback_;
        AsyncTradeCallback asyncTradeCallback_;

        /**
         * Determine which shard an order belongs to
         * Default: hash by order ID (can be customized to use symbolId)
         */
        size_t getShardForOrder(const Order& order) const {
            // Use clientId as symbol proxy if available, otherwise order ID
            uint64_t key = order.clientId > 0 ? order.clientId : order.id;
            return key % numShards_;
        }

        /**
         * Internal order submission (assumes lock is held)
         */
        ExecutionResult submitOrderInternal(Order order, size_t shard) {
            auto result = engines_[shard]->submitOrder(std::move(order));
            
            // Update aggregate stats
            ++ordersProcessed_;
            if (!result.trades.empty()) {
                tradeCount_ += result.trades.size();
                for (const auto& trade : result.trades) {
                    totalVolume_ += trade.quantity;
                }
            }
            
            return result;
        }
    };

    /**
     * PostTradeProcessor - Handles post-trade processing in parallel
     * 
     * Offloads post-trade work (P&L calculation, risk updates, I/O) 
     * to a thread pool, allowing the matching engine to continue
     * processing orders without waiting.
     */
    class PostTradeProcessor {
    public:
        using TradeHandler = std::function<void(const Trade&, uint64_t buyClientId, uint64_t sellClientId)>;
        using ExecutionHandler = std::function<void(const Order&, const ExecutionResult&)>;

        explicit PostTradeProcessor(size_t numThreads = 0)
            : pool_(numThreads > 0 ? numThreads : 2)
            , tradesProcessed_(0)
            , executionsProcessed_(0) {}

        ~PostTradeProcessor() {
            waitAll();
        }

        /**
         * Queue a trade for processing
         */
        void processTrade(const Trade& trade, uint64_t buyClientId, uint64_t sellClientId) {
            if (tradeHandler_) {
                pool_.submit([this, trade, buyClientId, sellClientId]() {
                    tradeHandler_(trade, buyClientId, sellClientId);
                    ++tradesProcessed_;
                });
            }
        }

        /**
         * Queue trades in batch
         */
        void processTrades(const std::vector<Trade>& trades, 
                          uint64_t buyClientId, uint64_t sellClientId) {
            if (!tradeHandler_) return;
            
            for (const auto& trade : trades) {
                processTrade(trade, buyClientId, sellClientId);
            }
        }

        /**
         * Queue an execution result for processing
         */
        void processExecution(const Order& order, const ExecutionResult& result) {
            if (executionHandler_) {
                pool_.submit([this, order, result]() {
                    executionHandler_(order, result);
                    ++executionsProcessed_;
                });
            }
        }

        /**
         * Set handler for trade processing
         */
        void setTradeHandler(TradeHandler handler) {
            tradeHandler_ = std::move(handler);
        }

        /**
         * Set handler for execution processing
         */
        void setExecutionHandler(ExecutionHandler handler) {
            executionHandler_ = std::move(handler);
        }

        /**
         * Wait for all pending work to complete
         */
        void waitAll() {
            pool_.waitAll();
        }

        /**
         * Get statistics
         */
        size_t getTradesProcessed() const { return tradesProcessed_.load(); }
        size_t getExecutionsProcessed() const { return executionsProcessed_.load(); }

    private:
        ThreadPool pool_;
        TradeHandler tradeHandler_;
        ExecutionHandler executionHandler_;
        std::atomic<size_t> tradesProcessed_;
        std::atomic<size_t> executionsProcessed_;
    };

}
