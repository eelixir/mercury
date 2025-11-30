/**
 * @file OrderNode.h
 * @brief Intrusive order node for O(1) list operations
 * 
 * This file defines the OrderNode structure which extends the Order struct
 * with intrusive list pointers, enabling O(1) insertion and removal from
 * price level queues without separate node allocations.
 * 
 * Key benefits:
 * - No separate allocation for list nodes (order IS the node)
 * - O(1) removal from any position in the queue
 * - Better cache locality during iteration
 * - Memory-efficient (no per-node overhead except prev/next pointers)
 */

#pragma once

#include "Order.h"
#include "IntrusiveList.h"
#include <cstdint>

namespace Mercury {

    /**
     * OrderNode - An order that can be stored in an intrusive list
     * 
     * Inherits from IntrusiveListNode to provide prev/next pointers.
     * This is the internal representation used within the order book.
     */
    struct OrderNode : public IntrusiveListNode<OrderNode> {
        // Core order data (same as Order)
        uint64_t id = 0;
        uint64_t timestamp = 0;
        OrderType orderType = OrderType::Limit;
        Side side = Side::Buy;
        int64_t price = 0;
        uint64_t quantity = 0;
        TimeInForce tif = TimeInForce::GTC;
        uint64_t clientId = 0;

        // Pool allocation tracking
        bool inUse = false;     // For object pool management

        // Default constructor
        OrderNode() = default;

        // Construct from Order
        explicit OrderNode(const Order& order)
            : id(order.id)
            , timestamp(order.timestamp)
            , orderType(order.orderType)
            , side(order.side)
            , price(order.price)
            , quantity(order.quantity)
            , tif(order.tif)
            , clientId(order.clientId)
            , inUse(true) {}

        // Convert back to Order (for API compatibility)
        Order toOrder() const {
            Order order;
            order.id = id;
            order.timestamp = timestamp;
            order.orderType = orderType;
            order.side = side;
            order.price = price;
            order.quantity = quantity;
            order.tif = tif;
            order.clientId = clientId;
            return order;
        }

        // Assign from Order
        void fromOrder(const Order& order) {
            id = order.id;
            timestamp = order.timestamp;
            orderType = order.orderType;
            side = order.side;
            price = order.price;
            quantity = order.quantity;
            tif = order.tif;
            clientId = order.clientId;
            inUse = true;
        }

        // Reset for reuse
        void reset() {
            id = 0;
            timestamp = 0;
            orderType = OrderType::Limit;
            side = Side::Buy;
            price = 0;
            quantity = 0;
            tif = TimeInForce::GTC;
            clientId = 0;
            inUse = false;
            // Clear intrusive list pointers
            this->prev = nullptr;
            this->next = nullptr;
        }
    };

    // Type alias for the intrusive list of orders
    using OrderList = IntrusiveList<OrderNode>;

}
