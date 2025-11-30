/**
 * @file order_book_test.cpp
 * @brief Unit tests for OrderBook functionality
 * 
 * Tests organized by category:
 * - Basic Order Entry
 * - Order Removal
 * - Order Book Edge Cases
 * - Order Book Safety
 */

#include <gtest/gtest.h>
#include "OrderBook.h"
#include "Order.h"

using namespace Mercury;

// ============== Test Helpers ==============

namespace {

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

}  // namespace

// ============== Basic Order Entry Tests ==============

TEST(OrderBookTest, AddBothBuyAndSellOrders) {
    OrderBook book;
    book.addOrder(createOrder(1, Side::Buy, 100, 50));
    book.addOrder(createOrder(2, Side::Sell, 105, 30));
    
    EXPECT_TRUE(book.hasBids());
    EXPECT_TRUE(book.hasAsks());
    EXPECT_EQ(book.getBestBid(), 100);
    EXPECT_EQ(book.getBestAsk(), 105);
}

TEST(OrderBookTest, AddMultipleBuyOrdersDifferentPrices) {
    OrderBook book;
    book.addOrder(createOrder(1, Side::Buy, 100, 50));
    book.addOrder(createOrder(2, Side::Buy, 105, 30));
    book.addOrder(createOrder(3, Side::Buy, 95, 20));
    
    EXPECT_TRUE(book.hasBids());
    EXPECT_EQ(book.getBestBid(), 105);  // Highest price
}

TEST(OrderBookTest, AddMultipleBuyOrdersSamePrice) {
    OrderBook book;
    book.addOrder(createOrder(1, Side::Buy, 100, 50));
    book.addOrder(createOrder(2, Side::Buy, 100, 30));
    book.addOrder(createOrder(3, Side::Buy, 100, 20));
    
    EXPECT_TRUE(book.hasBids());
    EXPECT_EQ(book.getBestBid(), 100);
}

TEST(OrderBookTest, AddMultipleSellOrdersDifferentPrices) {
    OrderBook book;
    book.addOrder(createOrder(1, Side::Sell, 110, 50));
    book.addOrder(createOrder(2, Side::Sell, 105, 30));
    book.addOrder(createOrder(3, Side::Sell, 115, 20));
    
    EXPECT_TRUE(book.hasAsks());
    EXPECT_EQ(book.getBestAsk(), 105);  // Lowest price
}

TEST(OrderBookTest, AddOrdersWithSameId) {
    OrderBook book;
    book.addOrder(createOrder(1, Side::Buy, 100, 50));
    book.addOrder(createOrder(1, Side::Buy, 105, 30));  // Same ID
    
    EXPECT_TRUE(book.hasBids());
}

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

TEST(OrderBookTest, EmptyBookHasNoBidsOrAsks) {
    OrderBook book;
    
    EXPECT_FALSE(book.hasBids());
    EXPECT_FALSE(book.hasAsks());
}

TEST(OrderBookTest, LargeNumberOfOrders) {
    OrderBook book;
    
    for (uint64_t i = 0; i < 1000; ++i) {
        book.addOrder(createOrder(i, Side::Buy, 100 + (i % 10), 50));
    }
    
    EXPECT_TRUE(book.hasBids());
    EXPECT_EQ(book.getBestBid(), 109);  // Highest: 100 + 9
    
    for (uint64_t i = 0; i < 1000; ++i) {
        book.removeOrder(i);
    }
    
    EXPECT_FALSE(book.hasBids());
}

// ============== Order Removal Tests ==============

TEST(OrderBookTest, RemoveAllOrdersAtSamePrice) {
    OrderBook book;
    book.addOrder(createOrder(1, Side::Buy, 100, 50));
    book.addOrder(createOrder(2, Side::Buy, 100, 30));
    
    book.removeOrder(1);
    book.removeOrder(2);
    
    EXPECT_FALSE(book.hasBids());
}

TEST(OrderBookTest, RemoveBestAskUpdatesBestPrice) {
    OrderBook book;
    book.addOrder(createOrder(1, Side::Sell, 110, 50));
    book.addOrder(createOrder(2, Side::Sell, 105, 30));
    book.addOrder(createOrder(3, Side::Sell, 115, 20));
    
    EXPECT_EQ(book.getBestAsk(), 105);
    book.removeOrder(2);
    EXPECT_EQ(book.getBestAsk(), 110);
}

TEST(OrderBookTest, RemoveBestBidUpdatesBestPrice) {
    OrderBook book;
    book.addOrder(createOrder(1, Side::Buy, 100, 50));
    book.addOrder(createOrder(2, Side::Buy, 105, 30));
    book.addOrder(createOrder(3, Side::Buy, 95, 20));
    
    EXPECT_EQ(book.getBestBid(), 105);
    book.removeOrder(2);
    EXPECT_EQ(book.getBestBid(), 100);
}

TEST(OrderBookTest, RemoveNonExistentOrder) {
    OrderBook book;
    book.addOrder(createOrder(1, Side::Buy, 100, 50));
    
    book.removeOrder(999);
    
    EXPECT_TRUE(book.hasBids());
    EXPECT_EQ(book.getBestBid(), 100);
}

TEST(OrderBookTest, RemoveOneOfMultipleOrdersAtSamePrice) {
    OrderBook book;
    book.addOrder(createOrder(1, Side::Buy, 100, 50));
    book.addOrder(createOrder(2, Side::Buy, 100, 30));
    book.addOrder(createOrder(3, Side::Buy, 100, 20));
    
    book.removeOrder(2);
    
    EXPECT_TRUE(book.hasBids());
    EXPECT_EQ(book.getBestBid(), 100);
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

TEST(OrderBookTest, RemoveOrderTwice) {
    OrderBook book;
    book.addOrder(createOrder(1, Side::Buy, 100, 50));
    
    book.removeOrder(1);
    book.removeOrder(1);  // Should not crash
    
    EXPECT_FALSE(book.hasBids());
}

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

// ============== Order Book Edge Cases ==============

TEST(OrderBookEdgeCasesTest, GetMidPriceOnEmptyBook) {
    OrderBook book;
    
    EXPECT_EQ(book.getMidPrice(), 0);
}

TEST(OrderBookEdgeCasesTest, GetSpreadOnEmptyBook) {
    OrderBook book;
    
    EXPECT_EQ(book.getSpread(), 0);
}

TEST(OrderBookEdgeCasesTest, SpreadWithOnlyAsks) {
    OrderBook book;
    book.addOrder(createOrder(1, Side::Sell, 105, 50));
    
    EXPECT_EQ(book.getSpread(), 0);
}

TEST(OrderBookEdgeCasesTest, SpreadWithOnlyBids) {
    OrderBook book;
    book.addOrder(createOrder(1, Side::Buy, 100, 50));
    
    EXPECT_EQ(book.getSpread(), 0);
}

TEST(OrderBookEdgeCasesTest, TryGetBestAskOnEmptyBook) {
    OrderBook book;
    
    EXPECT_FALSE(book.tryGetBestAsk().has_value());
}

TEST(OrderBookEdgeCasesTest, TryGetBestBidAfterRemoval) {
    OrderBook book;
    book.addOrder(createOrder(1, Side::Buy, 100, 50));
    book.removeOrder(1);
    
    EXPECT_FALSE(book.tryGetBestBid().has_value());
}

TEST(OrderBookEdgeCasesTest, TryGetBestBidOnEmptyBook) {
    OrderBook book;
    
    EXPECT_FALSE(book.tryGetBestBid().has_value());
}

TEST(OrderBookEdgeCasesTest, ValidMidPriceCalculation) {
    OrderBook book;
    book.addOrder(createOrder(1, Side::Buy, 100, 50));
    book.addOrder(createOrder(2, Side::Sell, 110, 50));
    
    EXPECT_EQ(book.getMidPrice(), 105);  // (100 + 110) / 2
}

TEST(OrderBookEdgeCasesTest, ValidSpreadCalculation) {
    OrderBook book;
    book.addOrder(createOrder(1, Side::Buy, 100, 50));
    book.addOrder(createOrder(2, Side::Sell, 105, 50));
    
    EXPECT_EQ(book.getSpread(), 5);
}

// ============== Order Book Safety Tests ==============

TEST(OrderBookSafetyTest, AddDuplicateOrderId) {
    OrderBook book;
    Order order1 = createOrder(1, Side::Buy, 100, 50);
    Order order2 = createOrder(1, Side::Buy, 105, 30);
    
    EXPECT_TRUE(book.addOrder(order1));
    EXPECT_FALSE(book.addOrder(order2));
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

TEST(OrderBookSafetyTest, GetOrderOptional) {
    OrderBook book;
    book.addOrder(createOrder(1, Side::Buy, 100, 50));
    
    auto existing = book.getOrder(1);
    EXPECT_TRUE(existing.has_value());
    EXPECT_EQ(existing->id, 1);
    
    auto nonExisting = book.getOrder(999);
    EXPECT_FALSE(nonExisting.has_value());
}

TEST(OrderBookSafetyTest, HasOrder) {
    OrderBook book;
    book.addOrder(createOrder(1, Side::Buy, 100, 50));
    
    EXPECT_TRUE(book.hasOrder(1));
    EXPECT_FALSE(book.hasOrder(999));
}

TEST(OrderBookSafetyTest, RemoveNonExistentOrder) {
    OrderBook book;
    
    EXPECT_FALSE(book.removeOrder(999));
}

TEST(OrderBookSafetyTest, UpdateQuantityOfNonExistentOrder) {
    OrderBook book;
    
    EXPECT_FALSE(book.updateOrderQuantity(999, 50));
}
