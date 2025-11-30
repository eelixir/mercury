#include <gtest/gtest.h>
#include "RiskManager.h"
#include "Order.h"
#include <vector>
#include <cmath>

using namespace Mercury;

class RiskManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Set up default limits for testing
        limits.maxPositionQuantity = 1000;
        limits.maxGrossExposure = 1000000;
        limits.maxNetExposure = 500000;
        limits.maxDailyLoss = -50000;
        limits.maxOrderValue = 100000;
        limits.maxOrderQuantity = 500;
        limits.maxOpenOrders = 10;
        riskManager = std::make_unique<RiskManager>(limits);
    }

    Order createOrder(uint64_t id, Side side, int64_t price, uint64_t quantity, 
                      uint64_t clientId = 1) {
        Order order;
        order.id = id;
        order.orderType = OrderType::Limit;
        order.side = side;
        order.price = price;
        order.quantity = quantity;
        order.clientId = clientId;
        return order;
    }

    Order createMarketOrder(uint64_t id, Side side, uint64_t quantity, 
                            uint64_t clientId = 1) {
        Order order;
        order.id = id;
        order.orderType = OrderType::Market;
        order.side = side;
        order.price = 0;
        order.quantity = quantity;
        order.clientId = clientId;
        return order;
    }

    RiskLimits limits;
    std::unique_ptr<RiskManager> riskManager;
};

// ==================== Basic Approval Tests ====================

TEST_F(RiskManagerTest, ApprovesValidOrder) {
    auto order = createOrder(1, Side::Buy, 100, 50, 1);
    auto event = riskManager->checkOrder(order);
    
    EXPECT_TRUE(event.isApproved());
    EXPECT_EQ(event.eventType, RiskEventType::Approved);
    EXPECT_EQ(event.orderId, 1);
    EXPECT_EQ(event.clientId, 1);
}

TEST_F(RiskManagerTest, ApprovesCancelOrder) {
    Order cancelOrder;
    cancelOrder.id = 1;
    cancelOrder.orderType = OrderType::Cancel;
    cancelOrder.clientId = 1;
    
    auto event = riskManager->checkOrder(cancelOrder);
    EXPECT_TRUE(event.isApproved());
}

TEST_F(RiskManagerTest, ApprovesModifyOrder) {
    Order modifyOrder;
    modifyOrder.id = 1;
    modifyOrder.orderType = OrderType::Modify;
    modifyOrder.targetOrderId = 100;
    modifyOrder.newPrice = 105;
    modifyOrder.clientId = 1;
    
    auto event = riskManager->checkOrder(modifyOrder);
    EXPECT_TRUE(event.isApproved());
}

// ==================== Order Quantity Limit Tests ====================

TEST_F(RiskManagerTest, RejectsOrderExceedingQuantityLimit) {
    auto order = createOrder(1, Side::Buy, 100, 600, 1);  // limit is 500
    auto event = riskManager->checkOrder(order);
    
    EXPECT_TRUE(event.isRejected());
    EXPECT_EQ(event.eventType, RiskEventType::OrderQuantityLimitBreached);
    EXPECT_EQ(event.limitValue, 500);
    EXPECT_EQ(event.requestedValue, 600);
}

TEST_F(RiskManagerTest, ApprovesOrderAtQuantityLimit) {
    auto order = createOrder(1, Side::Buy, 100, 500, 1);  // exactly at limit
    auto event = riskManager->checkOrder(order);
    
    EXPECT_TRUE(event.isApproved());
}

// ==================== Order Value Limit Tests ====================

TEST_F(RiskManagerTest, RejectsOrderExceedingValueLimit) {
    // Price 300 * Quantity 400 = 120,000 (limit is 100,000)
    auto order = createOrder(1, Side::Buy, 300, 400, 1);
    auto event = riskManager->checkOrder(order);
    
    EXPECT_TRUE(event.isRejected());
    EXPECT_EQ(event.eventType, RiskEventType::OrderValueLimitBreached);
    EXPECT_EQ(event.limitValue, 100000);
    EXPECT_EQ(event.requestedValue, 120000);
}

TEST_F(RiskManagerTest, ApprovesOrderAtValueLimit) {
    // Price 200 * Quantity 500 = 100,000 (exactly at limit)
    auto order = createOrder(1, Side::Buy, 200, 500, 1);
    auto event = riskManager->checkOrder(order);
    
    EXPECT_TRUE(event.isApproved());
}

// ==================== Position Limit Tests ====================

TEST_F(RiskManagerTest, RejectsOrderExceedingPositionLimit) {
    // Build up position first by simulating fills
    Trade trade1;
    trade1.tradeId = 1;
    trade1.buyOrderId = 1;
    trade1.sellOrderId = 100;
    trade1.price = 100;
    trade1.quantity = 500;
    riskManager->onTradeExecuted(trade1, 1, 0);
    
    // Verify position is tracked
    auto pos = riskManager->getClientPosition(1);
    EXPECT_EQ(pos.longPosition, 500);
    
    // Now try to add more - this would exceed the 1000 limit
    // Current: 500, adding 600 would make 1100 > 1000 limit
    auto order2 = createOrder(2, Side::Buy, 100, 500, 1);  // Keep under qty/value limits
    auto event = riskManager->checkOrder(order2);
    
    // First order should pass (500 + 500 = 1000, at limit)
    EXPECT_TRUE(event.isApproved());
    
    // Simulate that second order filled too
    Trade trade2;
    trade2.tradeId = 2;
    trade2.buyOrderId = 2;
    trade2.sellOrderId = 101;
    trade2.price = 100;
    trade2.quantity = 500;
    riskManager->onTradeExecuted(trade2, 1, 0);
    
    // Now at position 1000, try to add more - should be rejected
    auto order3 = createOrder(3, Side::Buy, 100, 100, 1);
    event = riskManager->checkOrder(order3);
    
    EXPECT_TRUE(event.isRejected());
    EXPECT_EQ(event.eventType, RiskEventType::PositionLimitBreached);
}

TEST_F(RiskManagerTest, AllowsPositionReductionEvenNearLimit) {
    // Build up a long position
    Trade trade;
    trade.tradeId = 1;
    trade.buyOrderId = 1;
    trade.sellOrderId = 100;
    trade.price = 100;
    trade.quantity = 800;
    riskManager->onTradeExecuted(trade, 1, 0);
    
    // Should allow sell orders that reduce position
    auto sellOrder = createOrder(2, Side::Sell, 100, 500, 1);
    auto event = riskManager->checkOrder(sellOrder);
    
    EXPECT_TRUE(event.isApproved());
}

// ==================== Open Orders Limit Tests ====================

TEST_F(RiskManagerTest, RejectsWhenMaxOpenOrdersExceeded) {
    // Add orders up to the limit
    for (int i = 0; i < 10; i++) {
        auto order = createOrder(i + 1, Side::Buy, 100, 10, 1);
        riskManager->checkOrder(order);
        riskManager->onOrderAdded(order);
    }
    
    // Now try to add one more
    auto order = createOrder(11, Side::Buy, 100, 10, 1);
    auto event = riskManager->checkOrder(order);
    
    EXPECT_TRUE(event.isRejected());
    EXPECT_EQ(event.eventType, RiskEventType::MaxOpenOrdersExceeded);
}

TEST_F(RiskManagerTest, AllowsOrderAfterPreviousRemoved) {
    // Add orders up to the limit
    for (int i = 0; i < 10; i++) {
        auto order = createOrder(i + 1, Side::Buy, 100, 10, 1);
        riskManager->checkOrder(order);
        riskManager->onOrderAdded(order);
    }
    
    // Remove one order
    Order removed;
    removed.id = 5;
    removed.clientId = 1;
    riskManager->onOrderRemoved(removed);
    
    // Now should allow a new order
    auto order = createOrder(11, Side::Buy, 100, 10, 1);
    auto event = riskManager->checkOrder(order);
    
    EXPECT_TRUE(event.isApproved());
}

// ==================== Client Position Tracking Tests ====================

TEST_F(RiskManagerTest, TracksLongPosition) {
    Trade trade;
    trade.tradeId = 1;
    trade.buyOrderId = 1;
    trade.sellOrderId = 100;
    trade.price = 100;
    trade.quantity = 200;
    riskManager->onTradeExecuted(trade, 1, 0);
    
    auto position = riskManager->getClientPosition(1);
    EXPECT_EQ(position.longPosition, 200);
    EXPECT_EQ(position.shortPosition, 0);
    EXPECT_EQ(position.netPosition(), 200);
}

TEST_F(RiskManagerTest, TracksShortPosition) {
    Trade trade;
    trade.tradeId = 1;
    trade.buyOrderId = 100;
    trade.sellOrderId = 1;
    trade.price = 100;
    trade.quantity = 150;
    riskManager->onTradeExecuted(trade, 0, 1);
    
    auto position = riskManager->getClientPosition(1);
    EXPECT_EQ(position.longPosition, 0);
    EXPECT_EQ(position.shortPosition, 150);
    EXPECT_EQ(position.netPosition(), -150);
}

TEST_F(RiskManagerTest, TracksNetPositionAfterMultipleTrades) {
    // Buy 200
    Trade trade1;
    trade1.tradeId = 1;
    trade1.buyOrderId = 1;
    trade1.sellOrderId = 100;
    trade1.price = 100;
    trade1.quantity = 200;
    riskManager->onTradeExecuted(trade1, 1, 0);
    
    // Sell 50 (closing part of long)
    Trade trade2;
    trade2.tradeId = 2;
    trade2.buyOrderId = 200;
    trade2.sellOrderId = 2;
    trade2.price = 105;
    trade2.quantity = 50;
    riskManager->onTradeExecuted(trade2, 0, 1);
    
    auto position = riskManager->getClientPosition(1);
    EXPECT_EQ(position.longPosition, 150);
    EXPECT_EQ(position.shortPosition, 0);
    EXPECT_EQ(position.netPosition(), 150);
}

// ==================== Client-Specific Limits Tests ====================

TEST_F(RiskManagerTest, UsesClientSpecificLimits) {
    // Set special limits for client 2
    RiskLimits client2Limits;
    client2Limits.maxOrderQuantity = 100;  // Much lower than default
    client2Limits.maxOrderValue = 50000;
    client2Limits.maxPositionQuantity = 500;
    client2Limits.maxOpenOrders = 5;
    client2Limits.maxGrossExposure = 500000;
    client2Limits.maxNetExposure = 250000;
    client2Limits.maxDailyLoss = -25000;
    riskManager->setClientLimits(2, client2Limits);
    
    // Order that's OK for client 1
    auto order1 = createOrder(1, Side::Buy, 100, 200, 1);
    EXPECT_TRUE(riskManager->checkOrder(order1).isApproved());
    
    // Same order rejected for client 2
    auto order2 = createOrder(2, Side::Buy, 100, 200, 2);
    auto event = riskManager->checkOrder(order2);
    EXPECT_TRUE(event.isRejected());
    EXPECT_EQ(event.eventType, RiskEventType::OrderQuantityLimitBreached);
}

// ==================== Statistics Tests ====================

TEST_F(RiskManagerTest, TracksApprovedAndRejectedCounts) {
    // Submit some valid orders
    for (int i = 0; i < 5; i++) {
        auto order = createOrder(i + 1, Side::Buy, 100, 10, 1);
        riskManager->checkOrder(order);
    }
    
    // Submit some invalid orders
    for (int i = 0; i < 3; i++) {
        auto order = createOrder(i + 100, Side::Buy, 100, 600, 1);  // Exceeds qty limit
        riskManager->checkOrder(order);
    }
    
    EXPECT_EQ(riskManager->getApprovedCount(), 5);
    EXPECT_EQ(riskManager->getRejectedCount(), 3);
    EXPECT_EQ(riskManager->getTotalChecks(), 8);
}

// ==================== Reset Tests ====================

TEST_F(RiskManagerTest, ResetPositionsClearsAllPositions) {
    // Add some positions
    Trade trade;
    trade.tradeId = 1;
    trade.buyOrderId = 1;
    trade.sellOrderId = 100;
    trade.price = 100;
    trade.quantity = 200;
    riskManager->onTradeExecuted(trade, 1, 0);
    
    // Verify position exists
    EXPECT_EQ(riskManager->getClientPosition(1).longPosition, 200);
    
    // Reset
    riskManager->resetPositions();
    
    // Position should be cleared
    EXPECT_EQ(riskManager->getClientPosition(1).longPosition, 0);
}

// ==================== Callback Tests ====================

TEST_F(RiskManagerTest, CallsRiskCallbackOnCheck) {
    std::vector<RiskEvent> events;
    riskManager->setRiskCallback([&events](const RiskEvent& event) {
        events.push_back(event);
    });
    
    auto order = createOrder(1, Side::Buy, 100, 50, 1);
    riskManager->checkOrder(order);
    
    ASSERT_EQ(events.size(), 1);
    EXPECT_EQ(events[0].orderId, 1);
    EXPECT_TRUE(events[0].isApproved());
}

TEST_F(RiskManagerTest, CallsRiskCallbackOnRejection) {
    std::vector<RiskEvent> events;
    riskManager->setRiskCallback([&events](const RiskEvent& event) {
        events.push_back(event);
    });
    
    auto order = createOrder(1, Side::Buy, 100, 600, 1);  // Exceeds limit
    riskManager->checkOrder(order);
    
    ASSERT_EQ(events.size(), 1);
    EXPECT_TRUE(events[0].isRejected());
    EXPECT_EQ(events[0].eventType, RiskEventType::OrderQuantityLimitBreached);
}

// ==================== Market Order Tests ====================

TEST_F(RiskManagerTest, ChecksMarketOrderQuantityLimit) {
    auto order = createMarketOrder(1, Side::Buy, 600, 1);  // Exceeds 500 limit
    auto event = riskManager->checkOrder(order);
    
    EXPECT_TRUE(event.isRejected());
    EXPECT_EQ(event.eventType, RiskEventType::OrderQuantityLimitBreached);
}

TEST_F(RiskManagerTest, ApprovesValidMarketOrder) {
    // Use quantity that won't exceed limits when multiplied by default market price
    auto order = createMarketOrder(1, Side::Buy, 5, 1);  // Small quantity
    auto event = riskManager->checkOrder(order);
    
    EXPECT_TRUE(event.isApproved());
}

// ==================== Exposure Limit Tests ====================

TEST_F(RiskManagerTest, RejectsOrderExceedingGrossExposure) {
    // Set up higher limits to focus on gross exposure
    RiskLimits exposureLimits;
    exposureLimits.maxPositionQuantity = 100000;
    exposureLimits.maxGrossExposure = 100000;  // Low gross exposure limit
    exposureLimits.maxNetExposure = 500000;
    exposureLimits.maxDailyLoss = -50000;
    exposureLimits.maxOrderValue = 1000000;  // High order value limit
    exposureLimits.maxOrderQuantity = 10000;
    exposureLimits.maxOpenOrders = 100;
    auto rm = std::make_unique<RiskManager>(exposureLimits);
    
    // Build up significant exposure first
    Trade trade;
    trade.tradeId = 1;
    trade.buyOrderId = 1;
    trade.sellOrderId = 100;
    trade.price = 100;
    trade.quantity = 800;
    rm->onTradeExecuted(trade, 1, 0);
    
    // Current gross exposure: 800 * 100 = 80,000
    // New order: 500 * 100 = 50,000
    // Total: 130,000 > 100,000 limit
    auto order = createOrder(2, Side::Buy, 100, 500, 1);
    auto event = rm->checkOrder(order);
    
    EXPECT_TRUE(event.isRejected());
    EXPECT_EQ(event.eventType, RiskEventType::GrossExposureLimitBreached);
}

// ==================== Multiple Client Tests ====================

TEST_F(RiskManagerTest, TracksMultipleClientsIndependently) {
    // Client 1 buys
    Trade trade1;
    trade1.tradeId = 1;
    trade1.buyOrderId = 1;
    trade1.sellOrderId = 100;
    trade1.price = 100;
    trade1.quantity = 200;
    riskManager->onTradeExecuted(trade1, 1, 0);
    
    // Client 2 sells
    Trade trade2;
    trade2.tradeId = 2;
    trade2.buyOrderId = 200;
    trade2.sellOrderId = 2;
    trade2.price = 100;
    trade2.quantity = 150;
    riskManager->onTradeExecuted(trade2, 0, 2);
    
    // Check positions are independent
    auto pos1 = riskManager->getClientPosition(1);
    auto pos2 = riskManager->getClientPosition(2);
    
    EXPECT_EQ(pos1.longPosition, 200);
    EXPECT_EQ(pos1.shortPosition, 0);
    EXPECT_EQ(pos2.longPosition, 0);
    EXPECT_EQ(pos2.shortPosition, 150);
}

// ==================== Edge Cases ====================

TEST_F(RiskManagerTest, HandlesZeroClientId) {
    auto order = createOrder(1, Side::Buy, 100, 50, 0);  // No client ID
    auto event = riskManager->checkOrder(order);
    
    // Should still work, using default position
    EXPECT_TRUE(event.isApproved());
}

TEST_F(RiskManagerTest, HandlesVeryLargeQuantityWithinLimits) {
    // Set higher limits
    RiskLimits highLimits;
    highLimits.maxOrderQuantity = UINT64_MAX;
    highLimits.maxOrderValue = INT64_MAX;
    highLimits.maxPositionQuantity = INT64_MAX;
    highLimits.maxGrossExposure = INT64_MAX;
    highLimits.maxNetExposure = INT64_MAX;
    highLimits.maxDailyLoss = INT64_MIN;
    highLimits.maxOpenOrders = UINT64_MAX;
    
    auto rm = RiskManager(highLimits);
    auto order = createOrder(1, Side::Buy, 1000000, 1000000, 1);
    auto event = rm.checkOrder(order);
    
    EXPECT_TRUE(event.isApproved());
}

int main(int argc, char **argv) {
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
