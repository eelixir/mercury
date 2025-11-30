/**
 * @file PriceLevel.h
 * @brief Price level structure with intrusive order list
 * 
 * Encapsulates a single price level in the order book, containing:
 * - The price
 * - A doubly-linked list of orders at this price (FIFO queue)
 * - Aggregate quantity for quick depth queries
 * 
 * Key optimizations:
 * - IntrusiveList for O(1) order insertion/removal
 * - Cached aggregate quantity (updated on insert/remove)
 * - No dynamic allocation per order
 */

#pragma once

#include "OrderNode.h"
#include "IntrusiveList.h"
#include <cstdint>

namespace Mercury {

    /**
     * PriceLevel - Represents all orders at a single price
     * 
     * Orders are maintained in FIFO order (time priority).
     * First order added is first to be matched.
     */
    struct PriceLevel {
        int64_t price = 0;
        OrderList orders;           // Intrusive list of OrderNodes
        uint64_t totalQuantity = 0; // Cached sum of all order quantities
        size_t orderCount = 0;      // Number of orders at this level

        PriceLevel() = default;
        explicit PriceLevel(int64_t p) : price(p) {}

        // Disable copy (orders can't be in multiple lists)
        PriceLevel(const PriceLevel&) = delete;
        PriceLevel& operator=(const PriceLevel&) = delete;

        // Enable move
        PriceLevel(PriceLevel&& other) noexcept
            : price(other.price)
            , orders(std::move(other.orders))
            , totalQuantity(other.totalQuantity)
            , orderCount(other.orderCount) {
            other.totalQuantity = 0;
            other.orderCount = 0;
        }

        PriceLevel& operator=(PriceLevel&& other) noexcept {
            if (this != &other) {
                price = other.price;
                orders = std::move(other.orders);
                totalQuantity = other.totalQuantity;
                orderCount = other.orderCount;
                other.totalQuantity = 0;
                other.orderCount = 0;
            }
            return *this;
        }

        /**
         * Add an order to the back of the queue (FIFO)
         * @param node Pointer to OrderNode (must not be in another list)
         */
        void addOrder(OrderNode* node) {
            orders.push_back(node);
            totalQuantity += node->quantity;
            ++orderCount;
        }

        /**
         * Remove a specific order from this level
         * @param node Pointer to OrderNode to remove
         */
        void removeOrder(OrderNode* node) {
            if (node->quantity <= totalQuantity) {
                totalQuantity -= node->quantity;
            } else {
                totalQuantity = 0;  // Shouldn't happen, but be safe
            }
            orders.remove(node);
            --orderCount;
        }

        /**
         * Get the first (oldest) order at this level
         * @return Pointer to front order, or nullptr if empty
         */
        OrderNode* front() {
            return orders.empty() ? nullptr : &orders.front();
        }

        const OrderNode* front() const {
            return orders.empty() ? nullptr : &orders.front();
        }

        /**
         * Remove and return the first order
         * @return Pointer to removed order, or nullptr if empty
         */
        OrderNode* popFront() {
            if (orders.empty()) return nullptr;
            
            OrderNode* node = &orders.front();
            if (node->quantity <= totalQuantity) {
                totalQuantity -= node->quantity;
            } else {
                totalQuantity = 0;
            }
            orders.pop_front();
            --orderCount;
            return node;
        }

        /**
         * Update the quantity of an order at this level
         * @param node Order to update
         * @param newQuantity New quantity value
         */
        void updateOrderQuantity(OrderNode* node, uint64_t newQuantity) {
            if (node->quantity <= totalQuantity) {
                totalQuantity -= node->quantity;
            }
            node->quantity = newQuantity;
            totalQuantity += newQuantity;
        }

        /**
         * Check if this price level is empty
         */
        bool empty() const { return orders.empty(); }

        /**
         * Get the number of orders at this level
         */
        size_t size() const { return orderCount; }

        /**
         * Get the total quantity at this level
         */
        uint64_t quantity() const { return totalQuantity; }

        // Iterator access for the order list
        auto begin() { return orders.begin(); }
        auto end() { return orders.end(); }
        auto begin() const { return orders.begin(); }
        auto end() const { return orders.end(); }
    };

}
