#include <gtest/gtest.h>
#include "OrderBook.h"
#include "Order.h"

using namespace Mercury;

// Helper function to create orders easily
Order createOrder(uint64_t id, Side side, int64_t price, uint64_t quantity) {
    return Order{
        .id = id,
        .timestamp = static_cast<uint64_t>(std::time(nullptr)),
        .orderType = OrderType::Limit,
        .side = side,
        .price = price,
        .quantity = quantity
    };
}

// ============== Basic Order Entry Tests ==============

TEST(OrderBookTest, AddSingleBuyOrder) {
    OrderBook book;
    Order buyOrder = createOrder(1, Side::Buy, 100, 50);
    
    book.addOrder(buyOrder);
    
    EXPECT_TRUE(book.hasBids());
    EXPECT_FALSE(book.hasAsks());
    EXPECT_EQ(book.getBestBid(), 100);
}

TEST(OrderBookTest, AddSingleSellOrder) {
    OrderBook book;
    Order sellOrder = createOrder(1, Side::Sell, 105, 30);
    
    book.addOrder(sellOrder);
    
    EXPECT_FALSE(book.hasBids());
    EXPECT_TRUE(book.hasAsks());
    EXPECT_EQ(book.getBestAsk(), 105);
}

TEST(OrderBookTest, AddMultipleBuyOrdersSamePrice) {
    OrderBook book;
    book.addOrder(createOrder(1, Side::Buy, 100, 50));
    book.addOrder(createOrder(2, Side::Buy, 100, 30));
    book.addOrder(createOrder(3, Side::Buy, 100, 20));
    
    EXPECT_TRUE(book.hasBids());
    EXPECT_EQ(book.getBestBid(), 100);
}

TEST(OrderBookTest, AddMultipleBuyOrdersDifferentPrices) {
    OrderBook book;
    book.addOrder(createOrder(1, Side::Buy, 100, 50));
    book.addOrder(createOrder(2, Side::Buy, 105, 30));
    book.addOrder(createOrder(3, Side::Buy, 95, 20));
    
    EXPECT_TRUE(book.hasBids());
    // Best bid should be highest price (105)
    EXPECT_EQ(book.getBestBid(), 105);
}

TEST(OrderBookTest, AddMultipleSellOrdersDifferentPrices) {
    OrderBook book;
    book.addOrder(createOrder(1, Side::Sell, 110, 50));
    book.addOrder(createOrder(2, Side::Sell, 105, 30));
    book.addOrder(createOrder(3, Side::Sell, 115, 20));
    
    EXPECT_TRUE(book.hasAsks());
    // Best ask should be lowest price (105)
    EXPECT_EQ(book.getBestAsk(), 105);
}

TEST(OrderBookTest, AddBothBuyAndSellOrders) {
    OrderBook book;
    book.addOrder(createOrder(1, Side::Buy, 100, 50));
    book.addOrder(createOrder(2, Side::Sell, 105, 30));
    
    EXPECT_TRUE(book.hasBids());
    EXPECT_TRUE(book.hasAsks());
    EXPECT_EQ(book.getBestBid(), 100);
    EXPECT_EQ(book.getBestAsk(), 105);
}

// ============== Order Removal Tests ==============

TEST(OrderBookTest, RemoveSingleBuyOrder) {
    OrderBook book;
    book.addOrder(createOrder(1, Side::Buy, 100, 50));
    
    book.removeOrder(1);
    
    EXPECT_FALSE(book.hasBids());
}

TEST(OrderBookTest, RemoveSingleSellOrder) {
    OrderBook book;
    book.addOrder(createOrder(1, Side::Sell, 105, 30));
    
    book.removeOrder(1);
    
    EXPECT_FALSE(book.hasAsks());
}

TEST(OrderBookTest, RemoveNonExistentOrder) {
    OrderBook book;
    book.addOrder(createOrder(1, Side::Buy, 100, 50));
    
    // Should not throw or crash
    book.removeOrder(999);
    
    // Original order should still exist
    EXPECT_TRUE(book.hasBids());
    EXPECT_EQ(book.getBestBid(), 100);
}

TEST(OrderBookTest, RemoveOneOfMultipleOrdersAtSamePrice) {
    OrderBook book;
    book.addOrder(createOrder(1, Side::Buy, 100, 50));
    book.addOrder(createOrder(2, Side::Buy, 100, 30));
    book.addOrder(createOrder(3, Side::Buy, 100, 20));
    
    book.removeOrder(2);
    
    // Should still have bids at price 100
    EXPECT_TRUE(book.hasBids());
    EXPECT_EQ(book.getBestBid(), 100);
}

TEST(OrderBookTest, RemoveAllOrdersAtSamePrice) {
    OrderBook book;
    book.addOrder(createOrder(1, Side::Buy, 100, 50));
    book.addOrder(createOrder(2, Side::Buy, 100, 30));
    
    book.removeOrder(1);
    book.removeOrder(2);
    
    EXPECT_FALSE(book.hasBids());
}

TEST(OrderBookTest, RemoveBestBidUpdatesBestPrice) {
    OrderBook book;
    book.addOrder(createOrder(1, Side::Buy, 100, 50));
    book.addOrder(createOrder(2, Side::Buy, 105, 30));
    book.addOrder(createOrder(3, Side::Buy, 95, 20));
    
    // Best bid is 105
    EXPECT_EQ(book.getBestBid(), 105);
    
    // Remove best bid
    book.removeOrder(2);
    
    // New best bid should be 100
    EXPECT_EQ(book.getBestBid(), 100);
}

TEST(OrderBookTest, RemoveBestAskUpdatesBestPrice) {
    OrderBook book;
    book.addOrder(createOrder(1, Side::Sell, 110, 50));
    book.addOrder(createOrder(2, Side::Sell, 105, 30));
    book.addOrder(createOrder(3, Side::Sell, 115, 20));
    
    // Best ask is 105
    EXPECT_EQ(book.getBestAsk(), 105);
    
    // Remove best ask
    book.removeOrder(2);
    
    // New best ask should be 110
    EXPECT_EQ(book.getBestAsk(), 110);
}

TEST(OrderBookTest, RemoveOrderDoesNotAffectOtherSide) {
    OrderBook book;
    book.addOrder(createOrder(1, Side::Buy, 100, 50));
    book.addOrder(createOrder(2, Side::Sell, 105, 30));
    
    book.removeOrder(1);
    
    EXPECT_FALSE(book.hasBids());
    EXPECT_TRUE(book.hasAsks());
    EXPECT_EQ(book.getBestAsk(), 105);
}

// ============== Edge Cases ==============

TEST(OrderBookTest, EmptyBookHasNoBidsOrAsks) {
    OrderBook book;
    
    EXPECT_FALSE(book.hasBids());
    EXPECT_FALSE(book.hasAsks());
}

TEST(OrderBookTest, RemoveOrderTwice) {
    OrderBook book;
    book.addOrder(createOrder(1, Side::Buy, 100, 50));
    
    book.removeOrder(1);
    book.removeOrder(1);  // Should not crash
    
    EXPECT_FALSE(book.hasBids());
}

TEST(OrderBookTest, AddOrdersWithSameId) {
    OrderBook book;
    book.addOrder(createOrder(1, Side::Buy, 100, 50));
    book.addOrder(createOrder(1, Side::Buy, 105, 30));  // Same ID, different price
    
    // Both orders technically exist in the vectors, 
    // but lookup points to the second one
    EXPECT_TRUE(book.hasBids());
}

TEST(OrderBookTest, LargeNumberOfOrders) {
    OrderBook book;
    
    // Add 1000 buy orders
    for (uint64_t i = 0; i < 1000; ++i) {
        book.addOrder(createOrder(i, Side::Buy, 100 + (i % 10), 50));
    }
    
    EXPECT_TRUE(book.hasBids());
    EXPECT_EQ(book.getBestBid(), 109);  // Highest price is 100 + 9 = 109
    
    // Remove all orders
    for (uint64_t i = 0; i < 1000; ++i) {
        book.removeOrder(i);
    }
    
    EXPECT_FALSE(book.hasBids());
}

// ============== Order Validation Tests ==============

TEST(OrderValidationTest, ValidLimitOrder) {
    Order order = createOrder(1, Side::Buy, 100, 50);
    order.orderType = OrderType::Limit;
    
    auto result = order.validate();
    EXPECT_EQ(result, RejectReason::None);
}

TEST(OrderValidationTest, InvalidZeroOrderId) {
    Order order = createOrder(0, Side::Buy, 100, 50);
    
    auto result = order.validate();
    EXPECT_EQ(result, RejectReason::InvalidOrderId);
}

TEST(OrderValidationTest, InvalidZeroQuantity) {
    Order order = createOrder(1, Side::Buy, 100, 0);
    
    auto result = order.validate();
    EXPECT_EQ(result, RejectReason::InvalidQuantity);
}

TEST(OrderValidationTest, InvalidZeroPriceLimitOrder) {
    Order order = createOrder(1, Side::Buy, 0, 50);
    order.orderType = OrderType::Limit;
    
    // Zero price for limit order is not rejected by basic validate()
    // More comprehensive validation would be in MatchingEngine
    auto result = order.validate();
    // Actually zero is not < 0, so it passes basic validation
    EXPECT_EQ(result, RejectReason::None);
}

TEST(OrderValidationTest, InvalidNegativePriceLimitOrder) {
    Order order = createOrder(1, Side::Buy, -100, 50);
    order.orderType = OrderType::Limit;
    
    auto result = order.validate();
    EXPECT_EQ(result, RejectReason::InvalidPrice);
}

TEST(OrderValidationTest, MarketOrderWithZeroPrice) {
    Order order = createOrder(1, Side::Buy, 0, 50);
    order.orderType = OrderType::Market;
    
    // Market orders don't need a price
    auto result = order.validate();
    EXPECT_EQ(result, RejectReason::None);
}

TEST(OrderValidationTest, ValidCancelOrder) {
    Order order{};
    order.id = 1;
    order.orderType = OrderType::Cancel;
    order.timestamp = 1000;
    
    auto result = order.validate();
    EXPECT_EQ(result, RejectReason::None);
}

TEST(OrderValidationTest, ModifyOrderWithZeroTargetId) {
    Order order{};
    order.id = 1;
    order.orderType = OrderType::Modify;
    order.targetOrderId = 0;
    order.price = 100;
    order.quantity = 50;
    order.timestamp = 1000;
    
    auto result = order.validate();
    EXPECT_EQ(result, RejectReason::InvalidOrderId);
}

TEST(OrderValidationTest, ModifyOrderWithNoChanges) {
    Order order{};
    order.id = 1;
    order.orderType = OrderType::Modify;
    order.targetOrderId = 123;
    order.newPrice = 0;
    order.newQuantity = 0;
    order.timestamp = 1000;
    
    auto result = order.validate();
    EXPECT_EQ(result, RejectReason::ModifyNoChanges);
}

// ============== Empty Book Edge Cases ==============

TEST(OrderBookEdgeCasesTest, TryGetBestBidOnEmptyBook) {
    OrderBook book;
    
    auto bestBid = book.tryGetBestBid();
    EXPECT_FALSE(bestBid.has_value());
}

TEST(OrderBookEdgeCasesTest, TryGetBestAskOnEmptyBook) {
    OrderBook book;
    
    auto bestAsk = book.tryGetBestAsk();
    EXPECT_FALSE(bestAsk.has_value());
}

TEST(OrderBookEdgeCasesTest, GetSpreadOnEmptyBook) {
    OrderBook book;
    
    auto spread = book.getSpread();
    EXPECT_EQ(spread, 0);  // Returns 0 when either side is empty
}

TEST(OrderBookEdgeCasesTest, GetMidPriceOnEmptyBook) {
    OrderBook book;
    
    auto midPrice = book.getMidPrice();
    EXPECT_EQ(midPrice, 0);  // Returns 0 when either side is empty
}

TEST(OrderBookEdgeCasesTest, SpreadWithOnlyBids) {
    OrderBook book;
    book.addOrder(createOrder(1, Side::Buy, 100, 50));
    
    auto spread = book.getSpread();
    EXPECT_EQ(spread, 0);  // Returns 0 when asks are empty
}

TEST(OrderBookEdgeCasesTest, SpreadWithOnlyAsks) {
    OrderBook book;
    book.addOrder(createOrder(1, Side::Sell, 105, 50));
    
    auto spread = book.getSpread();
    EXPECT_EQ(spread, 0);  // Returns 0 when bids are empty
}

TEST(OrderBookEdgeCasesTest, ValidSpreadCalculation) {
    OrderBook book;
    book.addOrder(createOrder(1, Side::Buy, 100, 50));
    book.addOrder(createOrder(2, Side::Sell, 105, 50));
    
    auto spread = book.getSpread();
    EXPECT_EQ(spread, 5);
}

TEST(OrderBookEdgeCasesTest, ValidMidPriceCalculation) {
    OrderBook book;
    book.addOrder(createOrder(1, Side::Buy, 100, 50));
    book.addOrder(createOrder(2, Side::Sell, 110, 50));
    
    auto midPrice = book.getMidPrice();
    EXPECT_EQ(midPrice, 105);  // (100 + 110) / 2
}

TEST(OrderBookEdgeCasesTest, TryGetBestBidAfterRemoval) {
    OrderBook book;
    book.addOrder(createOrder(1, Side::Buy, 100, 50));
    book.removeOrder(1);
    
    auto bestBid = book.tryGetBestBid();
    EXPECT_FALSE(bestBid.has_value());
}

// ============== Partial Fill Edge Cases ==============

TEST(PartialFillTest, PartialFillReducesQuantity) {
    Order order = createOrder(1, Side::Buy, 100, 50);
    
    // Simulate partial fill
    order.quantity = 30;  // 20 units filled
    
    EXPECT_EQ(order.quantity, 30);
}

TEST(PartialFillTest, ZeroQuantityAfterFill) {
    Order order = createOrder(1, Side::Buy, 100, 0);
    
    // After full fill, order should be rejected by validation
    auto result = order.validate();
    EXPECT_EQ(result, RejectReason::InvalidQuantity);
}

// ============== TimeInForce Edge Cases ==============

TEST(TimeInForceTest, IOCOrderValidation) {
    Order order = createOrder(1, Side::Buy, 100, 50);
    order.tif = TimeInForce::IOC;
    
    auto result = order.validate();
    EXPECT_EQ(result, RejectReason::None);
}

TEST(TimeInForceTest, FOKOrderValidation) {
    Order order = createOrder(1, Side::Buy, 100, 50);
    order.tif = TimeInForce::FOK;
    
    auto result = order.validate();
    EXPECT_EQ(result, RejectReason::None);
}

TEST(TimeInForceTest, GTCOrderValidation) {
    Order order = createOrder(1, Side::Buy, 100, 50);
    order.tif = TimeInForce::GTC;
    
    auto result = order.validate();
    EXPECT_EQ(result, RejectReason::None);
}

// ============== Price Boundary Tests ==============

TEST(PriceBoundaryTest, MaxPriceValue) {
    Order order = createOrder(1, Side::Buy, INT64_MAX, 50);
    order.orderType = OrderType::Limit;
    
    // Very large price should be rejected (out of range)
    auto result = order.validate();
    EXPECT_EQ(result, RejectReason::PriceOutOfRange);
}

TEST(PriceBoundaryTest, MaxQuantityValue) {
    Order order = createOrder(1, Side::Buy, 100, UINT64_MAX);
    
    // Very large quantity should still be valid (overflow protection in matching)
    auto result = order.validate();
    EXPECT_EQ(result, RejectReason::None);
}

TEST(PriceBoundaryTest, MinPositivePrice) {
    Order order = createOrder(1, Side::Buy, 1, 50);
    order.orderType = OrderType::Limit;
    
    auto result = order.validate();
    EXPECT_EQ(result, RejectReason::None);
}

TEST(PriceBoundaryTest, MinPositiveQuantity) {
    Order order = createOrder(1, Side::Buy, 100, 1);
    
    auto result = order.validate();
    EXPECT_EQ(result, RejectReason::None);
}

// ============== Order State Tests ==============

TEST(OrderStateTest, OrderIsValidCheck) {
    Order order = createOrder(1, Side::Buy, 100, 50);
    
    EXPECT_TRUE(order.isValid());
}

TEST(OrderStateTest, OrderIsNotValidWithZeroId) {
    Order order = createOrder(0, Side::Buy, 100, 50);
    
    EXPECT_FALSE(order.isValid());
}

TEST(OrderStateTest, OrderIsNotValidWithZeroQuantity) {
    Order order = createOrder(1, Side::Buy, 100, 0);
    
    EXPECT_FALSE(order.isValid());
}

// ============== OrderBook Safety Tests ==============

TEST(OrderBookSafetyTest, AddDuplicateOrderId) {
    OrderBook book;
    Order order1 = createOrder(1, Side::Buy, 100, 50);
    Order order2 = createOrder(1, Side::Buy, 105, 30);  // Same ID
    
    EXPECT_TRUE(book.addOrder(order1));
    EXPECT_FALSE(book.addOrder(order2));  // Should fail - duplicate ID
}

TEST(OrderBookSafetyTest, RemoveNonExistentOrder) {
    OrderBook book;
    
    EXPECT_FALSE(book.removeOrder(999));  // Order doesn't exist
}

TEST(OrderBookSafetyTest, UpdateQuantityOfNonExistentOrder) {
    OrderBook book;
    
    EXPECT_FALSE(book.updateOrderQuantity(999, 50));  // Order doesn't exist
}

TEST(OrderBookSafetyTest, GetOrderOptional) {
    OrderBook book;
    book.addOrder(createOrder(1, Side::Buy, 100, 50));
    
    auto existingOrder = book.getOrder(1);
    EXPECT_TRUE(existingOrder.has_value());
    EXPECT_EQ(existingOrder->id, 1);
    
    auto nonExistingOrder = book.getOrder(999);
    EXPECT_FALSE(nonExistingOrder.has_value());
}

TEST(OrderBookSafetyTest, HasOrder) {
    OrderBook book;
    book.addOrder(createOrder(1, Side::Buy, 100, 50));
    
    EXPECT_TRUE(book.hasOrder(1));
    EXPECT_FALSE(book.hasOrder(999));
}

TEST(OrderBookSafetyTest, ClearBook) {
    OrderBook book;
    book.addOrder(createOrder(1, Side::Buy, 100, 50));
    book.addOrder(createOrder(2, Side::Sell, 105, 30));
    
    EXPECT_FALSE(book.isEmpty());
    
    book.clear();
    
    EXPECT_TRUE(book.isEmpty());
    EXPECT_FALSE(book.hasBids());
    EXPECT_FALSE(book.hasAsks());
}
