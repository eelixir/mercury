/**
 * @file stress_test.cpp
 * @brief Stress tests and performance validation for the matching engine
 * 
 * Tests:
 * - High volume order processing
 * - Random order scenarios
 * - Book depth stress
 * - Throughput measurement
 * - Memory stability (many inserts/deletes)
 */

#include <gtest/gtest.h>
#include "MatchingEngine.h"
#include "Order.h"
#include "CSVParser.h"
#include <random>
#include <chrono>
#include <algorithm>
#include <numeric>
#include <fstream>

using namespace Mercury;

// ============== Stress Test Fixture ==============

class StressTest : public ::testing::Test {
protected:
    MatchingEngine engine;
    std::mt19937 rng{42};  // Fixed seed for reproducibility
    uint64_t tradeCount = 0;
    uint64_t orderIdCounter = 1;

    void SetUp() override {
        tradeCount = 0;
        orderIdCounter = 1;
        engine.setTradeCallback([this](const Trade&) {
            tradeCount++;
        });
    }

    Order makeRandomLimitOrder(Side side) {
        std::uniform_int_distribution<int64_t> priceDist(9900, 10100);
        std::uniform_int_distribution<uint64_t> qtyDist(1, 100);

        Order o{};
        o.id = orderIdCounter++;
        o.orderType = OrderType::Limit;
        o.side = side;
        o.price = priceDist(rng);
        o.quantity = qtyDist(rng);
        o.tif = TimeInForce::GTC;
        return o;
    }

    Order makeLimitOrder(uint64_t id, Side side, int64_t price, uint64_t qty) {
        Order o{};
        o.id = id;
        o.orderType = OrderType::Limit;
        o.side = side;
        o.price = price;
        o.quantity = qty;
        o.tif = TimeInForce::GTC;
        return o;
    }

    Order makeMarketOrder(uint64_t id, Side side, uint64_t qty) {
        Order o{};
        o.id = id;
        o.orderType = OrderType::Market;
        o.side = side;
        o.quantity = qty;
        return o;
    }
};

// ============== High Volume Tests ==============

TEST_F(StressTest, ProcessTenThousandOrders) {
    const int N = 10000;
    int successCount = 0;

    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < N; ++i) {
        Side side = (i % 2 == 0) ? Side::Buy : Side::Sell;
        auto result = engine.submitOrder(makeRandomLimitOrder(side));
        if (result.status != ExecutionStatus::Rejected) {
            successCount++;
        }
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    EXPECT_GT(successCount, N * 0.95);  // At least 95% success rate
    std::cout << "Processed " << N << " orders in " << duration.count() << "ms\n";
    std::cout << "Trades generated: " << tradeCount << "\n";
    std::cout << "Orders in book: " << engine.getOrderBook().getOrderCount() << "\n";
}

TEST_F(StressTest, ProcessOneHundredThousandOrders) {
    const int N = 100000;
    int successCount = 0;

    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < N; ++i) {
        Side side = (i % 2 == 0) ? Side::Buy : Side::Sell;
        auto result = engine.submitOrder(makeRandomLimitOrder(side));
        if (result.status != ExecutionStatus::Rejected) {
            successCount++;
        }
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    EXPECT_GT(successCount, N * 0.95);
    std::cout << "Processed " << N << " orders in " << duration.count() << "ms\n";
    std::cout << "Rate: " << (N * 1000.0 / duration.count()) << " orders/sec\n";
    std::cout << "Trades generated: " << tradeCount << "\n";
}

// ============== Book Depth Stress Tests ==============

TEST_F(StressTest, DeepBookOneSide) {
    const int DEPTH = 1000;

    // Create 1000 price levels with 10 orders each
    for (int level = 0; level < DEPTH; ++level) {
        for (int i = 0; i < 10; ++i) {
            auto result = engine.submitOrder(
                makeLimitOrder(orderIdCounter++, Side::Buy, 10000 - level, 100));
            EXPECT_NE(result.status, ExecutionStatus::Rejected);
        }
    }

    EXPECT_EQ(engine.getOrderBook().getBidLevelCount(), DEPTH);
    EXPECT_EQ(engine.getOrderBook().getOrderCount(), DEPTH * 10);
}

TEST_F(StressTest, DeepBookBothSides) {
    const int DEPTH = 500;

    // Bids from 9999 down to 9500
    for (int level = 0; level < DEPTH; ++level) {
        engine.submitOrder(makeLimitOrder(orderIdCounter++, Side::Buy, 9999 - level, 100));
    }

    // Asks from 10001 up to 10500
    for (int level = 0; level < DEPTH; ++level) {
        engine.submitOrder(makeLimitOrder(orderIdCounter++, Side::Sell, 10001 + level, 100));
    }

    EXPECT_EQ(engine.getOrderBook().getBidLevelCount(), DEPTH);
    EXPECT_EQ(engine.getOrderBook().getAskLevelCount(), DEPTH);
    EXPECT_EQ(engine.getOrderBook().getSpread(), 2);  // 10001 - 9999
}

TEST_F(StressTest, MarketOrderSweepsDeepBook) {
    const int LEVELS = 100;

    // Create deep ask book
    for (int level = 0; level < LEVELS; ++level) {
        engine.submitOrder(
            makeLimitOrder(orderIdCounter++, Side::Sell, 10000 + level, 50));
    }

    uint64_t totalQty = LEVELS * 50;
    
    auto result = engine.submitOrder(makeMarketOrder(orderIdCounter++, Side::Buy, totalQty));

    EXPECT_EQ(result.status, ExecutionStatus::Filled);
    EXPECT_EQ(result.filledQuantity, totalQty);
    EXPECT_EQ(tradeCount, LEVELS);
    EXPECT_FALSE(engine.getOrderBook().hasAsks());
}

// ============== Insert/Delete Cycle Tests ==============

TEST_F(StressTest, InsertDeleteCycle) {
    const int CYCLES = 5000;
    std::vector<uint64_t> activeOrders;

    for (int cycle = 0; cycle < CYCLES; ++cycle) {
        // Add a batch of orders
        for (int i = 0; i < 10; ++i) {
            Side side = (i % 2 == 0) ? Side::Buy : Side::Sell;
            auto result = engine.submitOrder(makeRandomLimitOrder(side));
            if (result.status == ExecutionStatus::Resting) {
                activeOrders.push_back(result.orderId);
            }
        }

        // Cancel half of them
        if (activeOrders.size() > 5) {
            std::shuffle(activeOrders.begin(), activeOrders.end(), rng);
            for (int i = 0; i < 5 && !activeOrders.empty(); ++i) {
                engine.cancelOrder(activeOrders.back());
                activeOrders.pop_back();
            }
        }
    }

    // Verify book is consistent
    auto& book = engine.getOrderBook();
    EXPECT_LE(book.getOrderCount(), activeOrders.size() + 100);
    std::cout << "After " << CYCLES << " insert/delete cycles:\n";
    std::cout << "  Orders in book: " << book.getOrderCount() << "\n";
    std::cout << "  Bid levels: " << book.getBidLevelCount() << "\n";
    std::cout << "  Ask levels: " << book.getAskLevelCount() << "\n";
}

TEST_F(StressTest, ModifyCycle) {
    const int ORDERS = 1000;
    const int MODIFIES = 5000;

    // Seed book
    for (int i = 0; i < ORDERS; ++i) {
        engine.submitOrder(makeLimitOrder(orderIdCounter++, Side::Buy, 9900 + (i % 100), 50));
    }

    std::uniform_int_distribution<uint64_t> idDist(1, ORDERS);
    std::uniform_int_distribution<int64_t> priceDist(9850, 9950);
    std::uniform_int_distribution<uint64_t> qtyDist(10, 100);

    int successfulModifies = 0;
    for (int i = 0; i < MODIFIES; ++i) {
        uint64_t targetId = idDist(rng);
        int64_t newPrice = priceDist(rng);
        uint64_t newQty = qtyDist(rng);

        auto result = engine.modifyOrder(targetId, newPrice, newQty);
        if (result.status == ExecutionStatus::Modified) {
            successfulModifies++;
        }
    }

    std::cout << "Successful modifies: " << successfulModifies << "/" << MODIFIES << "\n";
}

// ============== Matching Stress Tests ==============

TEST_F(StressTest, AggressiveMatchingStress) {
    const int ROUNDS = 100;

    for (int round = 0; round < ROUNDS; ++round) {
        // Build up liquidity
        for (int i = 0; i < 50; ++i) {
            engine.submitOrder(makeLimitOrder(orderIdCounter++, Side::Sell, 10050 - i, 100));
        }

        // Aggressive buys sweep the book
        for (int i = 0; i < 10; ++i) {
            engine.submitOrder(makeMarketOrder(orderIdCounter++, Side::Buy, 500));
        }
    }

    std::cout << "After aggressive matching stress:\n";
    std::cout << "  Total trades: " << tradeCount << "\n";
    std::cout << "  Total volume: " << engine.getTotalVolume() << "\n";
}

TEST_F(StressTest, AlternatingBuySellOrders) {
    const int N = 10000;

    for (int i = 0; i < N; ++i) {
        Side side = (i % 2 == 0) ? Side::Buy : Side::Sell;
        int64_t price = (side == Side::Buy) ? 10000 : 10000;  // Cross at same price
        engine.submitOrder(makeLimitOrder(orderIdCounter++, side, price, 10));
    }

    // Every pair should match
    EXPECT_EQ(tradeCount, N / 2);
    EXPECT_EQ(engine.getOrderBook().getOrderCount(), 0);
}

// ============== Sample Dataset Tests ==============

TEST_F(StressTest, ProcessSampleDataset) {
    CSVParser parser;
    auto orders = parser.parseFile("../data/sample_orders.csv");

    size_t rejected = 0;
    size_t cancelRejected = 0;
    
    for (auto& order : orders) {
        auto result = engine.submitOrder(order);
        if (result.status == ExecutionStatus::Rejected) {
            rejected++;
            // Cancel/Modify rejections are expected if target doesn't exist
            if (order.orderType == OrderType::Cancel || order.orderType == OrderType::Modify) {
                cancelRejected++;
            } else {
                // Non-cancel/modify rejections are failures
                EXPECT_NE(result.status, ExecutionStatus::Rejected)
                    << "Order " << order.id << " was unexpectedly rejected: " 
                    << rejectReasonToString(result.rejectReason);
            }
        }
    }

    std::cout << "Sample dataset results:\n";
    std::cout << "  Orders processed: " << orders.size() << "\n";
    std::cout << "  Trades: " << tradeCount << "\n";
    std::cout << "  Rejected: " << rejected << " (Cancel/Modify: " << cancelRejected << ")\n";
    std::cout << "  Bids in book: " << engine.getOrderBook().getBidLevelCount() << "\n";
    std::cout << "  Asks in book: " << engine.getOrderBook().getAskLevelCount() << "\n";
}

// ============== Consistency & Invariant Tests ==============

TEST_F(StressTest, BookInvariantsAfterRandomOperations) {
    const int OPS = 10000;
    std::uniform_int_distribution<int> opDist(0, 3);

    for (int i = 0; i < OPS; ++i) {
        int op = opDist(rng);

        switch (op) {
            case 0:  // Limit buy
                engine.submitOrder(makeRandomLimitOrder(Side::Buy));
                break;
            case 1:  // Limit sell
                engine.submitOrder(makeRandomLimitOrder(Side::Sell));
                break;
            case 2:  // Market order
                if (engine.getOrderBook().hasAsks()) {
                    engine.submitOrder(makeMarketOrder(orderIdCounter++, Side::Buy, 50));
                }
                break;
            case 3:  // Cancel random
                if (orderIdCounter > 10) {
                    std::uniform_int_distribution<uint64_t> cancelDist(1, orderIdCounter - 1);
                    engine.cancelOrder(cancelDist(rng));
                }
                break;
        }

        // Check invariants
        auto& book = engine.getOrderBook();
        if (book.hasBids() && book.hasAsks()) {
            // Best bid should always be less than best ask (no crossed book)
            EXPECT_LT(book.getBestBid(), book.getBestAsk())
                << "Book crossed at operation " << i;
        }
    }
}

TEST_F(StressTest, VolumeConsistency) {
    const int N = 1000;
    uint64_t expectedVolume = 0;

    // All orders at same price = guaranteed matches
    for (int i = 0; i < N; ++i) {
        engine.submitOrder(makeLimitOrder(orderIdCounter++, Side::Sell, 100, 10));
    }

    for (int i = 0; i < N; ++i) {
        auto result = engine.submitOrder(makeLimitOrder(orderIdCounter++, Side::Buy, 100, 10));
        expectedVolume += result.filledQuantity;
    }

    EXPECT_EQ(engine.getTotalVolume(), expectedVolume);
    EXPECT_EQ(tradeCount, N);
}

// ============== Throughput Benchmark ==============

TEST_F(StressTest, ThroughputBenchmark) {
    const int WARMUP = 5000;
    const int MEASURE = 50000;

    // Warmup
    for (int i = 0; i < WARMUP; ++i) {
        Side side = (i % 2 == 0) ? Side::Buy : Side::Sell;
        engine.submitOrder(makeRandomLimitOrder(side));
    }

    // Measure
    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < MEASURE; ++i) {
        Side side = (i % 2 == 0) ? Side::Buy : Side::Sell;
        engine.submitOrder(makeRandomLimitOrder(side));
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    double ordersPerSecond = (MEASURE * 1000000.0) / duration.count();
    double microsecondsPerOrder = static_cast<double>(duration.count()) / MEASURE;

    std::cout << "\n=== Throughput Benchmark ===\n";
    std::cout << "Orders: " << MEASURE << "\n";
    std::cout << "Time: " << duration.count() / 1000.0 << " ms\n";
    std::cout << "Rate: " << ordersPerSecond << " orders/sec\n";
    std::cout << "Latency: " << microsecondsPerOrder << " µs/order\n";

    // Expect at least 10,000 orders/second on reasonable hardware
    EXPECT_GT(ordersPerSecond, 10000);
}
