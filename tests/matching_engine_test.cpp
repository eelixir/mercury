/**
 * @file matching_engine_test.cpp
 * @brief Comprehensive unit tests for MatchingEngine correctness
 * 
 * Tests organized by category:
 * - Limit Order Matching
 * - Market Order Matching  
 * - Cancel & Modify Operations
 * - Time-in-Force (GTC, IOC, FOK)
 * - Edge Cases & Error Handling
 * - Price-Time Priority
 * - Partial Fills
 * - Self-Trade Prevention
 */

#include <gtest/gtest.h>
#include "MatchingEngine.h"
#include "Order.h"
#include <vector>
#include <algorithm>

using namespace Mercury;

// ============== Test Fixture ==============

class MatchingEngineTest : public ::testing::Test {
protected:
    MatchingEngine engine;
    std::vector<Trade> capturedTrades;
    std::vector<ExecutionResult> capturedExecutions;

    void SetUp() override {
        capturedTrades.clear();
        capturedExecutions.clear();

        engine.setTradeCallback([this](const Trade& t) {
            capturedTrades.push_back(t);
        });

        engine.setExecutionCallback([this](const ExecutionResult& r) {
            capturedExecutions.push_back(r);
        });
    }

    Order makeLimitOrder(uint64_t id, Side side, int64_t price, uint64_t qty,
                         TimeInForce tif = TimeInForce::GTC) {
        Order o{};
        o.id = id;
        o.orderType = OrderType::Limit;
        o.side = side;
        o.price = price;
        o.quantity = qty;
        o.tif = tif;
        return o;
    }

    Order makeMarketOrder(uint64_t id, Side side, uint64_t qty,
                          TimeInForce tif = TimeInForce::GTC) {
        Order o{};
        o.id = id;
        o.orderType = OrderType::Market;
        o.side = side;
        o.quantity = qty;
        o.tif = tif;
        return o;
    }

    void seedBook() {
        // Add liquidity: Bids at 99, 98, 97; Asks at 101, 102, 103
        engine.submitOrder(makeLimitOrder(100, Side::Buy, 99, 100));
        engine.submitOrder(makeLimitOrder(101, Side::Buy, 98, 100));
        engine.submitOrder(makeLimitOrder(102, Side::Buy, 97, 100));
        engine.submitOrder(makeLimitOrder(200, Side::Sell, 101, 100));
        engine.submitOrder(makeLimitOrder(201, Side::Sell, 102, 100));
        engine.submitOrder(makeLimitOrder(202, Side::Sell, 103, 100));
        capturedTrades.clear();
        capturedExecutions.clear();
    }
};

// ============== Limit Order Matching Tests ==============

TEST_F(MatchingEngineTest, LimitOrderAddedToEmptyBook) {
    auto result = engine.submitOrder(makeLimitOrder(1, Side::Buy, 100, 50));

    EXPECT_EQ(result.status, ExecutionStatus::Resting);
    EXPECT_EQ(result.orderId, 1);
    EXPECT_EQ(result.filledQuantity, 0);
    EXPECT_EQ(result.remainingQuantity, 50);
    EXPECT_TRUE(engine.getOrderBook().hasBids());
}

TEST_F(MatchingEngineTest, LimitOrderFullMatchSingleOrder) {
    engine.submitOrder(makeLimitOrder(1, Side::Sell, 100, 50));
    auto result = engine.submitOrder(makeLimitOrder(2, Side::Buy, 100, 50));

    EXPECT_EQ(result.status, ExecutionStatus::Filled);
    EXPECT_EQ(result.filledQuantity, 50);
    EXPECT_EQ(result.remainingQuantity, 0);
    EXPECT_EQ(capturedTrades.size(), 1);
    EXPECT_EQ(capturedTrades[0].quantity, 50);
    EXPECT_EQ(capturedTrades[0].price, 100);
}

TEST_F(MatchingEngineTest, LimitOrderPartialMatch) {
    engine.submitOrder(makeLimitOrder(1, Side::Sell, 100, 30));
    auto result = engine.submitOrder(makeLimitOrder(2, Side::Buy, 100, 50));

    EXPECT_EQ(result.status, ExecutionStatus::PartialFill);
    EXPECT_EQ(result.filledQuantity, 30);
    EXPECT_EQ(result.remainingQuantity, 20);
    EXPECT_TRUE(engine.getOrderBook().hasBids());
}

TEST_F(MatchingEngineTest, LimitOrderMatchAcrossMultipleLevels) {
    engine.submitOrder(makeLimitOrder(1, Side::Sell, 100, 30));
    engine.submitOrder(makeLimitOrder(2, Side::Sell, 101, 30));
    engine.submitOrder(makeLimitOrder(3, Side::Sell, 102, 30));

    auto result = engine.submitOrder(makeLimitOrder(4, Side::Buy, 102, 75));

    // Can match all 3 levels: 30 + 30 + 15 = 75
    EXPECT_EQ(result.status, ExecutionStatus::Filled);
    EXPECT_EQ(result.filledQuantity, 75);
    EXPECT_EQ(result.remainingQuantity, 0);
    EXPECT_EQ(capturedTrades.size(), 3);
}

TEST_F(MatchingEngineTest, LimitOrderNoMatchDifferentPrices) {
    engine.submitOrder(makeLimitOrder(1, Side::Sell, 105, 50));
    auto result = engine.submitOrder(makeLimitOrder(2, Side::Buy, 100, 50));

    EXPECT_EQ(result.status, ExecutionStatus::Resting);
    EXPECT_EQ(result.filledQuantity, 0);
    EXPECT_TRUE(engine.getOrderBook().hasBids());
    EXPECT_TRUE(engine.getOrderBook().hasAsks());
}

TEST_F(MatchingEngineTest, LimitOrderMatchesAtBetterPrice) {
    engine.submitOrder(makeLimitOrder(1, Side::Sell, 95, 50));
    auto result = engine.submitOrder(makeLimitOrder(2, Side::Buy, 100, 50));

    EXPECT_EQ(result.status, ExecutionStatus::Filled);
    EXPECT_EQ(capturedTrades[0].price, 95);  // Matched at resting order price
}

// ============== Market Order Tests ==============

TEST_F(MatchingEngineTest, MarketOrderFullyFilled) {
    seedBook();
    auto result = engine.submitOrder(makeMarketOrder(300, Side::Buy, 50));

    EXPECT_EQ(result.status, ExecutionStatus::Filled);
    EXPECT_EQ(result.filledQuantity, 50);
    EXPECT_EQ(capturedTrades[0].price, 101);  // Best ask
}

TEST_F(MatchingEngineTest, MarketOrderNoLiquidity) {
    auto result = engine.submitOrder(makeMarketOrder(1, Side::Buy, 50));

    EXPECT_EQ(result.status, ExecutionStatus::Rejected);
    EXPECT_EQ(result.rejectReason, RejectReason::NoLiquidity);
}

TEST_F(MatchingEngineTest, MarketOrderPartialFillThenCancel) {
    engine.submitOrder(makeLimitOrder(1, Side::Sell, 100, 30));
    auto result = engine.submitOrder(makeMarketOrder(2, Side::Buy, 50));

    EXPECT_EQ(result.status, ExecutionStatus::PartialFill);
    EXPECT_EQ(result.filledQuantity, 30);
    EXPECT_EQ(result.remainingQuantity, 20);
}

TEST_F(MatchingEngineTest, MarketOrderSweepsMultipleLevels) {
    engine.submitOrder(makeLimitOrder(1, Side::Sell, 100, 30));
    engine.submitOrder(makeLimitOrder(2, Side::Sell, 101, 30));
    engine.submitOrder(makeLimitOrder(3, Side::Sell, 102, 30));

    auto result = engine.submitOrder(makeMarketOrder(4, Side::Buy, 90));

    EXPECT_EQ(result.status, ExecutionStatus::Filled);
    EXPECT_EQ(result.filledQuantity, 90);
    EXPECT_EQ(capturedTrades.size(), 3);
}

// ============== Cancel Order Tests ==============

TEST_F(MatchingEngineTest, CancelExistingOrder) {
    engine.submitOrder(makeLimitOrder(1, Side::Buy, 100, 50));
    auto result = engine.cancelOrder(1);

    EXPECT_EQ(result.status, ExecutionStatus::Cancelled);
    EXPECT_FALSE(engine.getOrderBook().hasBids());
}

TEST_F(MatchingEngineTest, CancelNonExistentOrder) {
    auto result = engine.cancelOrder(999);

    EXPECT_EQ(result.status, ExecutionStatus::Rejected);
    EXPECT_EQ(result.rejectReason, RejectReason::OrderNotFound);
}

TEST_F(MatchingEngineTest, CancelZeroOrderId) {
    auto result = engine.cancelOrder(0);

    EXPECT_EQ(result.status, ExecutionStatus::Rejected);
    EXPECT_EQ(result.rejectReason, RejectReason::InvalidOrderId);
}

// ============== Modify Order Tests ==============

TEST_F(MatchingEngineTest, ModifyOrderPrice) {
    engine.submitOrder(makeLimitOrder(1, Side::Buy, 100, 50));
    auto result = engine.modifyOrder(1, 105, 0);

    EXPECT_EQ(result.status, ExecutionStatus::Modified);
    auto order = engine.getOrderBook().getOrder(1);
    ASSERT_TRUE(order.has_value());
    EXPECT_EQ(order->price, 105);
}

TEST_F(MatchingEngineTest, ModifyOrderQuantity) {
    engine.submitOrder(makeLimitOrder(1, Side::Buy, 100, 50));
    auto result = engine.modifyOrder(1, 0, 75);

    EXPECT_EQ(result.status, ExecutionStatus::Modified);
    auto order = engine.getOrderBook().getOrder(1);
    ASSERT_TRUE(order.has_value());
    EXPECT_EQ(order->quantity, 75);
}

TEST_F(MatchingEngineTest, ModifyNonExistentOrder) {
    auto result = engine.modifyOrder(999, 100, 50);

    EXPECT_EQ(result.status, ExecutionStatus::Rejected);
    EXPECT_EQ(result.rejectReason, RejectReason::OrderNotFound);
}

TEST_F(MatchingEngineTest, ModifyOrderCrossesBook) {
    engine.submitOrder(makeLimitOrder(1, Side::Buy, 100, 50));
    engine.submitOrder(makeLimitOrder(2, Side::Sell, 105, 50));
    
    // Modify buy order to cross the ask
    auto result = engine.modifyOrder(1, 110, 0);

    EXPECT_TRUE(result.status == ExecutionStatus::Filled || 
                result.status == ExecutionStatus::Modified);
    EXPECT_EQ(result.filledQuantity, 50);
}

TEST_F(MatchingEngineTest, ModifyNoChanges) {
    engine.submitOrder(makeLimitOrder(1, Side::Buy, 100, 50));
    auto result = engine.modifyOrder(1, 0, 0);

    EXPECT_EQ(result.status, ExecutionStatus::Rejected);
    EXPECT_EQ(result.rejectReason, RejectReason::ModifyNoChanges);
}

// ============== Time-in-Force Tests ==============

TEST_F(MatchingEngineTest, IOCOrderFullyFilled) {
    engine.submitOrder(makeLimitOrder(1, Side::Sell, 100, 50));
    auto result = engine.submitOrder(makeLimitOrder(2, Side::Buy, 100, 50, TimeInForce::IOC));

    EXPECT_EQ(result.status, ExecutionStatus::Filled);
    EXPECT_EQ(result.filledQuantity, 50);
}

TEST_F(MatchingEngineTest, IOCOrderPartiallyFilledCancelsRemainder) {
    engine.submitOrder(makeLimitOrder(1, Side::Sell, 100, 30));
    auto result = engine.submitOrder(makeLimitOrder(2, Side::Buy, 100, 50, TimeInForce::IOC));

    EXPECT_EQ(result.status, ExecutionStatus::PartialFill);
    EXPECT_EQ(result.filledQuantity, 30);
    EXPECT_FALSE(engine.getOrderBook().hasOrder(2));  // Not resting
}

TEST_F(MatchingEngineTest, IOCOrderNoMatchCancelled) {
    engine.submitOrder(makeLimitOrder(1, Side::Sell, 105, 50));
    auto result = engine.submitOrder(makeLimitOrder(2, Side::Buy, 100, 50, TimeInForce::IOC));

    EXPECT_EQ(result.status, ExecutionStatus::Cancelled);
    EXPECT_EQ(result.filledQuantity, 0);
}

TEST_F(MatchingEngineTest, FOKOrderFullyFilled) {
    engine.submitOrder(makeLimitOrder(1, Side::Sell, 100, 50));
    auto result = engine.submitOrder(makeLimitOrder(2, Side::Buy, 100, 50, TimeInForce::FOK));

    EXPECT_EQ(result.status, ExecutionStatus::Filled);
    EXPECT_EQ(result.filledQuantity, 50);
}

TEST_F(MatchingEngineTest, FOKOrderRejectedInsufficientLiquidity) {
    engine.submitOrder(makeLimitOrder(1, Side::Sell, 100, 30));
    auto result = engine.submitOrder(makeLimitOrder(2, Side::Buy, 100, 50, TimeInForce::FOK));

    EXPECT_EQ(result.status, ExecutionStatus::Rejected);
    EXPECT_EQ(result.rejectReason, RejectReason::FOKCannotFill);
    EXPECT_EQ(result.filledQuantity, 0);
}

TEST_F(MatchingEngineTest, FOKOrderWithMultipleLevels) {
    engine.submitOrder(makeLimitOrder(1, Side::Sell, 100, 30));
    engine.submitOrder(makeLimitOrder(2, Side::Sell, 101, 30));
    
    auto result = engine.submitOrder(makeLimitOrder(3, Side::Buy, 101, 60, TimeInForce::FOK));

    EXPECT_EQ(result.status, ExecutionStatus::Filled);
    EXPECT_EQ(result.filledQuantity, 60);
}

TEST_F(MatchingEngineTest, GTCOrderRestsInBook) {
    engine.submitOrder(makeLimitOrder(1, Side::Sell, 105, 50));
    auto result = engine.submitOrder(makeLimitOrder(2, Side::Buy, 100, 50, TimeInForce::GTC));

    EXPECT_EQ(result.status, ExecutionStatus::Resting);
    EXPECT_TRUE(engine.getOrderBook().hasOrder(2));
}

// ============== Price-Time Priority Tests ==============

TEST_F(MatchingEngineTest, PriceTimePriorityBetterPriceFirst) {
    engine.submitOrder(makeLimitOrder(1, Side::Sell, 101, 50));
    engine.submitOrder(makeLimitOrder(2, Side::Sell, 100, 50));  // Better price
    
    auto result = engine.submitOrder(makeMarketOrder(3, Side::Buy, 30));

    EXPECT_EQ(capturedTrades[0].price, 100);  // Better price matched first
    EXPECT_EQ(capturedTrades[0].sellOrderId, 2);
}

TEST_F(MatchingEngineTest, PriceTimePrioritySamePriceTimeFirst) {
    engine.submitOrder(makeLimitOrder(1, Side::Sell, 100, 50));  // First
    engine.submitOrder(makeLimitOrder(2, Side::Sell, 100, 50));  // Second
    
    auto result = engine.submitOrder(makeMarketOrder(3, Side::Buy, 30));

    EXPECT_EQ(capturedTrades[0].sellOrderId, 1);  // First order matched
}

TEST_F(MatchingEngineTest, PriceTimePriorityMatchesMultipleAtSameLevel) {
    engine.submitOrder(makeLimitOrder(1, Side::Sell, 100, 30));
    engine.submitOrder(makeLimitOrder(2, Side::Sell, 100, 30));
    
    auto result = engine.submitOrder(makeMarketOrder(3, Side::Buy, 50));

    EXPECT_EQ(capturedTrades.size(), 2);
    EXPECT_EQ(capturedTrades[0].sellOrderId, 1);
    EXPECT_EQ(capturedTrades[0].quantity, 30);
    EXPECT_EQ(capturedTrades[1].sellOrderId, 2);
    EXPECT_EQ(capturedTrades[1].quantity, 20);
}

// ============== Edge Cases & Error Handling ==============

TEST_F(MatchingEngineTest, RejectZeroOrderId) {
    Order o = makeLimitOrder(0, Side::Buy, 100, 50);
    auto result = engine.submitOrder(o);

    EXPECT_EQ(result.status, ExecutionStatus::Rejected);
    EXPECT_EQ(result.rejectReason, RejectReason::InvalidOrderId);
}

TEST_F(MatchingEngineTest, RejectZeroQuantity) {
    Order o = makeLimitOrder(1, Side::Buy, 100, 0);
    auto result = engine.submitOrder(o);

    EXPECT_EQ(result.status, ExecutionStatus::Rejected);
    EXPECT_EQ(result.rejectReason, RejectReason::InvalidQuantity);
}

TEST_F(MatchingEngineTest, RejectNegativePrice) {
    Order o = makeLimitOrder(1, Side::Buy, -100, 50);
    auto result = engine.submitOrder(o);

    EXPECT_EQ(result.status, ExecutionStatus::Rejected);
    EXPECT_EQ(result.rejectReason, RejectReason::InvalidPrice);
}

TEST_F(MatchingEngineTest, RejectDuplicateOrderId) {
    engine.submitOrder(makeLimitOrder(1, Side::Buy, 100, 50));
    auto result = engine.submitOrder(makeLimitOrder(1, Side::Sell, 105, 50));

    EXPECT_EQ(result.status, ExecutionStatus::Rejected);
    EXPECT_EQ(result.rejectReason, RejectReason::DuplicateOrderId);
}

TEST_F(MatchingEngineTest, ExecutionCallbackInvoked) {
    engine.submitOrder(makeLimitOrder(1, Side::Buy, 100, 50));

    EXPECT_EQ(capturedExecutions.size(), 1);
    EXPECT_EQ(capturedExecutions[0].orderId, 1);
}

TEST_F(MatchingEngineTest, TradeCallbackInvoked) {
    engine.submitOrder(makeLimitOrder(1, Side::Sell, 100, 50));
    engine.submitOrder(makeLimitOrder(2, Side::Buy, 100, 50));

    EXPECT_EQ(capturedTrades.size(), 1);
    EXPECT_EQ(capturedTrades[0].buyOrderId, 2);
    EXPECT_EQ(capturedTrades[0].sellOrderId, 1);
}

// ============== Self-Trade Prevention Tests ==============

TEST_F(MatchingEngineTest, SelfTradePreventionSkipsOwnOrders) {
    Order sell = makeLimitOrder(1, Side::Sell, 100, 50);
    sell.clientId = 42;
    engine.submitOrder(sell);

    Order buy = makeLimitOrder(2, Side::Buy, 100, 50);
    buy.clientId = 42;  // Same client
    auto result = engine.submitOrder(buy);

    EXPECT_EQ(result.status, ExecutionStatus::Resting);  // No match due to STP
    EXPECT_EQ(result.filledQuantity, 0);
}

TEST_F(MatchingEngineTest, SelfTradePreventionMatchesDifferentClients) {
    Order sell = makeLimitOrder(1, Side::Sell, 100, 50);
    sell.clientId = 42;
    engine.submitOrder(sell);

    Order buy = makeLimitOrder(2, Side::Buy, 100, 50);
    buy.clientId = 43;  // Different client
    auto result = engine.submitOrder(buy);

    EXPECT_EQ(result.status, ExecutionStatus::Filled);
    EXPECT_EQ(result.filledQuantity, 50);
}

// ============== Statistics Tests ==============

TEST_F(MatchingEngineTest, TradeCountIncremented) {
    engine.submitOrder(makeLimitOrder(1, Side::Sell, 100, 50));
    engine.submitOrder(makeLimitOrder(2, Side::Buy, 100, 50));

    EXPECT_EQ(engine.getTradeCount(), 1);
}

TEST_F(MatchingEngineTest, TotalVolumeAccumulated) {
    engine.submitOrder(makeLimitOrder(1, Side::Sell, 100, 50));
    engine.submitOrder(makeLimitOrder(2, Side::Buy, 100, 50));

    EXPECT_EQ(engine.getTotalVolume(), 50);
}

TEST_F(MatchingEngineTest, MultipleTradesVolumeAccumulated) {
    engine.submitOrder(makeLimitOrder(1, Side::Sell, 100, 30));
    engine.submitOrder(makeLimitOrder(2, Side::Sell, 101, 40));
    engine.submitOrder(makeLimitOrder(3, Side::Buy, 101, 70));

    EXPECT_EQ(engine.getTradeCount(), 2);
    EXPECT_EQ(engine.getTotalVolume(), 70);
}
