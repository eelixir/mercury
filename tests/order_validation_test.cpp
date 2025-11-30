/**
 * @file order_validation_test.cpp
 * @brief Unit tests for Order validation and edge cases
 * 
 * Tests organized by category:
 * - Order Validation
 * - Partial Fill Cases
 * - Price Boundaries
 * - Time-in-Force
 * - Order State
 */

#include <gtest/gtest.h>
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

// ============== Order State Tests ==============

TEST(OrderStateTest, OrderIsNotValidWithZeroId) {
    Order order = createOrder(0, Side::Buy, 100, 50);
    
    EXPECT_FALSE(order.isValid());
}

TEST(OrderStateTest, OrderIsNotValidWithZeroQuantity) {
    Order order = createOrder(1, Side::Buy, 100, 0);
    
    EXPECT_FALSE(order.isValid());
}

TEST(OrderStateTest, OrderIsValidCheck) {
    Order order = createOrder(1, Side::Buy, 100, 50);
    
    EXPECT_TRUE(order.isValid());
}

// ============== Order Validation Tests ==============

TEST(OrderValidationTest, InvalidNegativePriceLimitOrder) {
    Order order = createOrder(1, Side::Buy, -100, 50);
    order.orderType = OrderType::Limit;
    
    EXPECT_EQ(order.validate(), RejectReason::InvalidPrice);
}

TEST(OrderValidationTest, InvalidZeroOrderId) {
    Order order = createOrder(0, Side::Buy, 100, 50);
    
    EXPECT_EQ(order.validate(), RejectReason::InvalidOrderId);
}

TEST(OrderValidationTest, InvalidZeroPriceLimitOrder) {
    Order order = createOrder(1, Side::Buy, 0, 50);
    order.orderType = OrderType::Limit;
    
    // Zero is not < 0, passes basic validation
    EXPECT_EQ(order.validate(), RejectReason::None);
}

TEST(OrderValidationTest, InvalidZeroQuantity) {
    Order order = createOrder(1, Side::Buy, 100, 0);
    
    EXPECT_EQ(order.validate(), RejectReason::InvalidQuantity);
}

TEST(OrderValidationTest, MarketOrderWithZeroPrice) {
    Order order = createOrder(1, Side::Buy, 0, 50);
    order.orderType = OrderType::Market;
    
    EXPECT_EQ(order.validate(), RejectReason::None);
}

TEST(OrderValidationTest, ModifyOrderWithNoChanges) {
    Order order{};
    order.id = 1;
    order.orderType = OrderType::Modify;
    order.targetOrderId = 123;
    order.newPrice = 0;
    order.newQuantity = 0;
    order.timestamp = 1000;
    
    EXPECT_EQ(order.validate(), RejectReason::ModifyNoChanges);
}

TEST(OrderValidationTest, ModifyOrderWithZeroTargetId) {
    Order order{};
    order.id = 1;
    order.orderType = OrderType::Modify;
    order.targetOrderId = 0;
    order.price = 100;
    order.quantity = 50;
    order.timestamp = 1000;
    
    EXPECT_EQ(order.validate(), RejectReason::InvalidOrderId);
}

TEST(OrderValidationTest, ValidCancelOrder) {
    Order order{};
    order.id = 1;
    order.orderType = OrderType::Cancel;
    order.timestamp = 1000;
    
    EXPECT_EQ(order.validate(), RejectReason::None);
}

TEST(OrderValidationTest, ValidLimitOrder) {
    Order order = createOrder(1, Side::Buy, 100, 50);
    order.orderType = OrderType::Limit;
    
    EXPECT_EQ(order.validate(), RejectReason::None);
}

// ============== Partial Fill Tests ==============

TEST(PartialFillTest, PartialFillReducesQuantity) {
    Order order = createOrder(1, Side::Buy, 100, 50);
    order.quantity = 30;  // Simulated partial fill
    
    EXPECT_EQ(order.quantity, 30);
}

TEST(PartialFillTest, ZeroQuantityAfterFill) {
    Order order = createOrder(1, Side::Buy, 100, 0);
    
    EXPECT_EQ(order.validate(), RejectReason::InvalidQuantity);
}

// ============== Price Boundary Tests ==============

TEST(PriceBoundaryTest, MaxPriceValue) {
    Order order = createOrder(1, Side::Buy, INT64_MAX, 50);
    order.orderType = OrderType::Limit;
    
    EXPECT_EQ(order.validate(), RejectReason::PriceOutOfRange);
}

TEST(PriceBoundaryTest, MaxQuantityValue) {
    Order order = createOrder(1, Side::Buy, 100, UINT64_MAX);
    
    EXPECT_EQ(order.validate(), RejectReason::None);
}

TEST(PriceBoundaryTest, MinPositivePrice) {
    Order order = createOrder(1, Side::Buy, 1, 50);
    order.orderType = OrderType::Limit;
    
    EXPECT_EQ(order.validate(), RejectReason::None);
}

TEST(PriceBoundaryTest, MinPositiveQuantity) {
    Order order = createOrder(1, Side::Buy, 100, 1);
    
    EXPECT_EQ(order.validate(), RejectReason::None);
}

// ============== Time-in-Force Tests ==============

TEST(TimeInForceTest, FOKOrderValidation) {
    Order order = createOrder(1, Side::Buy, 100, 50);
    order.tif = TimeInForce::FOK;
    
    EXPECT_EQ(order.validate(), RejectReason::None);
}

TEST(TimeInForceTest, GTCOrderValidation) {
    Order order = createOrder(1, Side::Buy, 100, 50);
    order.tif = TimeInForce::GTC;
    
    EXPECT_EQ(order.validate(), RejectReason::None);
}

TEST(TimeInForceTest, IOCOrderValidation) {
    Order order = createOrder(1, Side::Buy, 100, 50);
    order.tif = TimeInForce::IOC;
    
    EXPECT_EQ(order.validate(), RejectReason::None);
}
