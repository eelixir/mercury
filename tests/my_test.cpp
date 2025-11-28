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
