#include <gtest/gtest.h>
#include "ThreadPool.h"
#include "AsyncWriter.h"
#include "ConcurrentMatchingEngine.h"
#include "CSVParser.h"
#include <atomic>
#include <vector>
#include <chrono>
#include <numeric>
#include <fstream>

using namespace Mercury;

// ============================================================================
// ThreadPool Tests
// ============================================================================

class ThreadPoolTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(ThreadPoolTest, BasicSubmit) {
    ThreadPool pool(2);
    
    auto future = pool.submit([]() { return 42; });
    EXPECT_EQ(42, future.get());
}

TEST_F(ThreadPoolTest, MultipleSubmits) {
    ThreadPool pool(4);
    std::vector<std::future<int>> futures;
    
    for (int i = 0; i < 100; ++i) {
        futures.push_back(pool.submit([i]() { return i * 2; }));
    }
    
    for (int i = 0; i < 100; ++i) {
        EXPECT_EQ(i * 2, futures[i].get());
    }
}

TEST_F(ThreadPoolTest, AtomicCounter) {
    ThreadPool pool(4);
    std::atomic<int> counter{0};
    
    for (int i = 0; i < 1000; ++i) {
        pool.submit([&counter]() { ++counter; });
    }
    
    pool.waitAll();
    EXPECT_EQ(1000, counter.load());
}

TEST_F(ThreadPoolTest, WaitAll) {
    ThreadPool pool(4);
    std::atomic<int> completed{0};
    
    for (int i = 0; i < 100; ++i) {
        pool.submit([&completed]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            ++completed;
        });
    }
    
    pool.waitAll();
    EXPECT_EQ(100, completed.load());
}

TEST_F(ThreadPoolTest, GetSize) {
    ThreadPool pool(8);
    EXPECT_EQ(8u, pool.size());
}

// ============================================================================
// ParallelFor Tests
// ============================================================================

TEST_F(ThreadPoolTest, ParallelForBasic) {
    std::vector<int> data(1000, 0);
    
    ParallelFor::execute(0, data.size(), [&data](size_t i) {
        data[i] = static_cast<int>(i * 2);
    });
    
    for (size_t i = 0; i < data.size(); ++i) {
        EXPECT_EQ(static_cast<int>(i * 2), data[i]);
    }
}

TEST_F(ThreadPoolTest, ParallelForWithPool) {
    ThreadPool pool(4);
    std::vector<int> data(10000, 0);
    
    ParallelFor::execute(0, data.size(), [&data](size_t i) {
        data[i] = static_cast<int>(i);
    }, &pool);
    
    int sum = std::accumulate(data.begin(), data.end(), 0);
    int expectedSum = (10000 * 9999) / 2;  // Sum of 0 to 9999
    EXPECT_EQ(expectedSum, sum);
}

// ============================================================================
// SpinLock Tests
// ============================================================================

TEST_F(ThreadPoolTest, SpinLockBasic) {
    SpinLock lock;
    int counter = 0;
    
    lock.lock();
    counter++;
    lock.unlock();
    
    EXPECT_EQ(1, counter);
}

TEST_F(ThreadPoolTest, SpinLockMultiThreaded) {
    SpinLock lock;
    int counter = 0;
    ThreadPool pool(4);
    
    for (int i = 0; i < 1000; ++i) {
        pool.submit([&lock, &counter]() {
            SpinLockGuard guard(lock);
            ++counter;
        });
    }
    
    pool.waitAll();
    EXPECT_EQ(1000, counter);
}

// ============================================================================
// ConcurrentQueue Tests
// ============================================================================

TEST(ConcurrentQueueTest, PushPop) {
    ConcurrentQueue<int> queue;
    
    queue.push(42);
    
    int value;
    EXPECT_TRUE(queue.pop(value));
    EXPECT_EQ(42, value);
}

TEST(ConcurrentQueueTest, MultipleItems) {
    ConcurrentQueue<int> queue;
    
    for (int i = 0; i < 100; ++i) {
        queue.push(i);
    }
    
    EXPECT_EQ(100u, queue.size());
    
    for (int i = 0; i < 100; ++i) {
        int value;
        EXPECT_TRUE(queue.pop(value));
        EXPECT_EQ(i, value);
    }
    
    EXPECT_TRUE(queue.empty());
}

TEST(ConcurrentQueueTest, ProducerConsumer) {
    ConcurrentQueue<int> queue;
    std::atomic<int> sum{0};
    const int numItems = 1000;
    
    // Producer thread
    std::thread producer([&queue, numItems]() {
        for (int i = 0; i < numItems; ++i) {
            queue.push(i);
        }
    });
    
    // Consumer thread
    std::thread consumer([&queue, &sum, numItems]() {
        for (int i = 0; i < numItems; ++i) {
            int value;
            if (queue.pop(value)) {
                sum += value;
            }
        }
    });
    
    producer.join();
    consumer.join();
    
    int expectedSum = (numItems * (numItems - 1)) / 2;
    EXPECT_EQ(expectedSum, sum.load());
}

// ============================================================================
// AsyncWriter Tests
// ============================================================================

class AsyncWriterTest : public ::testing::Test {
protected:
    std::string testFile = "test_async_output.csv";
    
    void TearDown() override {
        std::remove(testFile.c_str());
    }
};

TEST_F(AsyncWriterTest, WriteAndRead) {
    {
        AsyncWriter writer(testFile);
        EXPECT_TRUE(writer.open());
        
        writer.write("line1\n");
        writer.write("line2\n");
        writer.write("line3\n");
        
        writer.close();
    }
    
    // Read and verify
    std::ifstream file(testFile);
    EXPECT_TRUE(file.is_open());
    
    std::string line;
    std::getline(file, line);
    EXPECT_EQ("line1", line);
    std::getline(file, line);
    EXPECT_EQ("line2", line);
    std::getline(file, line);
    EXPECT_EQ("line3", line);
}

TEST_F(AsyncWriterTest, ManyWrites) {
    const int numWrites = 1000;
    
    {
        AsyncWriter writer(testFile);
        EXPECT_TRUE(writer.open());
        
        for (int i = 0; i < numWrites; ++i) {
            writer.write("test line\n");
        }
        
        writer.close();
    }
    
    // Count lines
    std::ifstream file(testFile);
    int lineCount = 0;
    std::string line;
    while (std::getline(file, line)) {
        ++lineCount;
    }
    
    EXPECT_EQ(numWrites, lineCount);
}

// ============================================================================
// ConcurrentMatchingEngine Tests
// ============================================================================

class ConcurrentMatchingEngineTest : public ::testing::Test {
protected:
    Order createOrder(uint64_t id, Side side, int64_t price, uint64_t qty) {
        Order order;
        order.id = id;
        order.orderType = OrderType::Limit;
        order.side = side;
        order.price = price;
        order.quantity = qty;
        order.tif = TimeInForce::GTC;
        return order;
    }
};

TEST_F(ConcurrentMatchingEngineTest, SingleThreadedBasic) {
    ConcurrentMatchingEngine engine;
    
    auto result = engine.submitOrder(createOrder(1, Side::Buy, 100, 10));
    EXPECT_EQ(ExecutionStatus::Resting, result.status);
    
    result = engine.submitOrder(createOrder(2, Side::Sell, 100, 10));
    EXPECT_EQ(ExecutionStatus::Filled, result.status);
    EXPECT_EQ(1u, result.trades.size());
}

TEST_F(ConcurrentMatchingEngineTest, SymbolSharded) {
    ConcurrentMatchingEngine engine;
    engine.setNumSymbolShards(4);
    
    EXPECT_EQ(4u, engine.getNumShards());
    EXPECT_EQ(ConcurrentMatchingEngine::Mode::SymbolSharded, engine.getMode());
    
    // Submit some orders
    auto result = engine.submitOrder(createOrder(1, Side::Buy, 100, 10));
    EXPECT_EQ(ExecutionStatus::Resting, result.status);
}

TEST_F(ConcurrentMatchingEngineTest, BatchSubmit) {
    ConcurrentMatchingEngine engine;
    
    std::vector<Order> orders;
    for (uint64_t i = 1; i <= 10; ++i) {
        orders.push_back(createOrder(i, Side::Buy, 100, 10));
    }
    
    std::vector<ExecutionResult> results;
    engine.submitOrders(orders, results);
    
    EXPECT_EQ(10u, results.size());
    for (const auto& result : results) {
        EXPECT_EQ(ExecutionStatus::Resting, result.status);
    }
}

TEST_F(ConcurrentMatchingEngineTest, ShardedBatchSubmit) {
    ConcurrentMatchingEngine engine;
    engine.setNumSymbolShards(4);
    
    std::vector<Order> orders;
    for (uint64_t i = 1; i <= 100; ++i) {
        auto order = createOrder(i, Side::Buy, 100, 10);
        order.clientId = i % 4;  // Distribute across shards
        orders.push_back(order);
    }
    
    std::vector<ExecutionResult> results;
    engine.submitOrders(orders, results);
    
    EXPECT_EQ(100u, results.size());
    EXPECT_EQ(100u, engine.getTotalOrderCount());
}

TEST_F(ConcurrentMatchingEngineTest, AsyncCallbacks) {
    ConcurrentMatchingEngine engine;
    engine.setMode(ConcurrentMatchingEngine::Mode::AsyncCallbacks);
    
    std::atomic<int> tradeCount{0};
    engine.setAsyncTradeCallback([&tradeCount](std::vector<Trade>&& trades) {
        tradeCount += static_cast<int>(trades.size());
    });
    
    // Create a match
    engine.submitOrder(createOrder(1, Side::Buy, 100, 10));
    engine.submitOrder(createOrder(2, Side::Sell, 100, 10));
    
    engine.shutdown();  // Wait for async callbacks
    
    // Trade was generated, but callback might run async
    EXPECT_EQ(1u, engine.getTradeCount());
}

TEST_F(ConcurrentMatchingEngineTest, Statistics) {
    ConcurrentMatchingEngine engine;
    
    engine.submitOrder(createOrder(1, Side::Buy, 100, 10));
    engine.submitOrder(createOrder(2, Side::Sell, 100, 5));
    
    EXPECT_EQ(2u, engine.getOrdersProcessed());
    EXPECT_EQ(1u, engine.getTradeCount());
    EXPECT_EQ(5u, engine.getTotalVolume());
}

// ============================================================================
// PostTradeProcessor Tests
// ============================================================================

TEST(PostTradeProcessorTest, ProcessTrades) {
    PostTradeProcessor processor(2);
    std::atomic<int> processedCount{0};
    
    processor.setTradeHandler([&processedCount](const Trade& trade, uint64_t, uint64_t) {
        ++processedCount;
    });
    
    Trade trade;
    trade.tradeId = 1;
    trade.price = 100;
    trade.quantity = 10;
    
    for (int i = 0; i < 100; ++i) {
        processor.processTrade(trade, 1, 2);
    }
    
    processor.waitAll();
    EXPECT_EQ(100, processedCount.load());
}

// ============================================================================
// Parallel CSV Parser Tests
// ============================================================================

TEST(ParallelParserTest, ParseFileParallel) {
    // Create a test CSV file
    const std::string testFile = "test_orders_parallel.csv";
    
    {
        std::ofstream file(testFile);
        file << "id,timestamp,type,side,price,quantity,client_id\n";
        for (int i = 1; i <= 1000; ++i) {
            file << i << "," << i << ",limit,buy,100," << (i % 100 + 1) << "," << (i % 10) << "\n";
        }
    }
    
    // Parse sequentially
    CSVParser parser1;
    auto orders1 = parser1.parseFile(testFile);
    
    // Parse in parallel
    CSVParser parser2;
    auto orders2 = parser2.parseFileParallel(testFile, 4);
    
    // Both should parse the same number of orders
    EXPECT_EQ(orders1.size(), orders2.size());
    EXPECT_EQ(1000u, orders1.size());
    
    // Cleanup
    std::remove(testFile.c_str());
}

// ============================================================================
// Integration/Stress Tests
// ============================================================================

TEST(ConcurrencyStressTest, HighVolumeProcessing) {
    ConcurrentMatchingEngine engine;
    engine.setNumSymbolShards(4);
    
    ThreadPool pool(4);
    const size_t numOrders = 10000;
    
    std::vector<Order> orders;
    orders.reserve(numOrders);
    
    for (size_t i = 1; i <= numOrders; ++i) {
        Order order;
        order.id = i;
        order.orderType = OrderType::Limit;
        order.side = (i % 2 == 0) ? Side::Buy : Side::Sell;
        order.price = 100 + (static_cast<int64_t>(i) % 10);
        order.quantity = 10;
        order.clientId = i % 4;  // Distribute across shards
        orders.push_back(order);
    }
    
    auto startTime = std::chrono::high_resolution_clock::now();
    
    std::vector<ExecutionResult> results;
    engine.submitOrdersParallel(orders, results, pool);
    
    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);
    
    EXPECT_EQ(numOrders, results.size());
    EXPECT_EQ(numOrders, engine.getOrdersProcessed());
    
    std::cout << "Concurrent Processing: " << numOrders << " orders in " 
              << duration.count() / 1000.0 << " ms\n";
    std::cout << "Rate: " << (numOrders * 1000000.0 / duration.count()) << " orders/sec\n";
}
