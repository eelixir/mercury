#pragma once

#include "LegacyHashMap.h"
#include "MarketData.h"
#include "ObjectPool.h"
#include "Order.h"
#include "OrderNode.h"
#include "PriceLevel.h"

#include <functional>
#include <limits>
#include <map>
#include <optional>

namespace MercuryBenchmarks {

class LegacyOrderBook {
public:
    static constexpr size_t DEFAULT_ORDER_POOL_SIZE = 10000;

    explicit LegacyOrderBook(size_t initialPoolSize = DEFAULT_ORDER_POOL_SIZE)
        : orderPool_(initialPoolSize, true),
          orderLookup_(initialPoolSize) {}

    LegacyOrderBook(const LegacyOrderBook&) = delete;
    LegacyOrderBook& operator=(const LegacyOrderBook&) = delete;
    LegacyOrderBook(LegacyOrderBook&&) = default;
    LegacyOrderBook& operator=(LegacyOrderBook&&) = default;

    bool addOrder(const Mercury::Order& order) {
        if (orderLookup_.find(order.id) != nullptr) {
            return false;
        }

        if (order.id == 0 || order.quantity == 0) {
            return false;
        }

        Mercury::OrderNode* node = orderPool_.acquire();
        if (!node) {
            return false;
        }

        node->fromOrder(order);

        Mercury::PriceLevel* level = nullptr;
        if (order.side == Mercury::Side::Buy) {
            auto& levelRef = bids_[order.price];
            levelRef.price = order.price;
            level = &levelRef;
        } else {
            auto& levelRef = asks_[order.price];
            levelRef.price = order.price;
            level = &levelRef;
        }

        level->addOrder(node);
        orderLookup_.insert(order.id, OrderLocation{node, order.price, order.side});
        return true;
    }

    bool removeOrder(uint64_t orderId) {
        if (orderId == 0) {
            return false;
        }

        OrderLocation* loc = orderLookup_.find(orderId);
        if (!loc) {
            return false;
        }

        Mercury::OrderNode* node = loc->node;
        const int64_t price = loc->price;
        const Mercury::Side side = loc->side;

        if (side == Mercury::Side::Buy) {
            auto it = bids_.find(price);
            if (it != bids_.end()) {
                it->second.removeOrder(node);
                if (it->second.empty()) {
                    bids_.erase(it);
                }
            }
        } else {
            auto it = asks_.find(price);
            if (it != asks_.end()) {
                it->second.removeOrder(node);
                if (it->second.empty()) {
                    asks_.erase(it);
                }
            }
        }

        orderLookup_.erase(orderId);
        node->reset();
        orderPool_.release(node);
        return true;
    }

    std::optional<Mercury::Order> getOrder(uint64_t orderId) const {
        if (orderId == 0) {
            return std::nullopt;
        }

        const OrderLocation* loc = orderLookup_.find(orderId);
        if (!loc) {
            return std::nullopt;
        }

        return loc->node->toOrder();
    }

    bool updateOrderQuantity(uint64_t orderId, uint64_t newQuantity) {
        if (orderId == 0) {
            return false;
        }

        OrderLocation* loc = orderLookup_.find(orderId);
        if (!loc) {
            return false;
        }

        if (newQuantity == 0) {
            return removeOrder(orderId);
        }

        Mercury::OrderNode* node = loc->node;
        const int64_t price = loc->price;
        const Mercury::Side side = loc->side;

        if (side == Mercury::Side::Buy) {
            auto it = bids_.find(price);
            if (it != bids_.end()) {
                it->second.updateOrderQuantity(node, newQuantity);
            }
        } else {
            auto it = asks_.find(price);
            if (it != asks_.end()) {
                it->second.updateOrderQuantity(node, newQuantity);
            }
        }

        return true;
    }

    Mercury::PriceLevel* getBidLevel(int64_t price) {
        auto it = bids_.find(price);
        return it != bids_.end() ? &it->second : nullptr;
    }

    Mercury::PriceLevel* getAskLevel(int64_t price) {
        auto it = asks_.find(price);
        return it != asks_.end() ? &it->second : nullptr;
    }

    const std::map<int64_t, Mercury::PriceLevel, std::greater<int64_t>>& getBidLevels() const {
        return bids_;
    }

    const std::map<int64_t, Mercury::PriceLevel>& getAskLevels() const {
        return asks_;
    }

    [[nodiscard]] bool hasBids() const noexcept { return !bids_.empty(); }
    [[nodiscard]] bool hasAsks() const noexcept { return !asks_.empty(); }

    [[nodiscard]] int64_t getBestBid() const noexcept {
        return bids_.empty() ? std::numeric_limits<int64_t>::min() : bids_.begin()->first;
    }

    [[nodiscard]] int64_t getBestAsk() const noexcept {
        return asks_.empty() ? std::numeric_limits<int64_t>::max() : asks_.begin()->first;
    }

    [[nodiscard]] uint64_t getQuantityAtPrice(int64_t price, Mercury::Side side) const {
        if (side == Mercury::Side::Buy) {
            auto it = bids_.find(price);
            return it != bids_.end() ? it->second.quantity() : 0;
        }

        auto it = asks_.find(price);
        return it != asks_.end() ? it->second.quantity() : 0;
    }

    [[nodiscard]] size_t getOrderCountAtPrice(int64_t price, Mercury::Side side) const {
        if (side == Mercury::Side::Buy) {
            auto it = bids_.find(price);
            return it != bids_.end() ? it->second.size() : 0;
        }

        auto it = asks_.find(price);
        return it != asks_.end() ? it->second.size() : 0;
    }

private:
    struct OrderLocation {
        Mercury::OrderNode* node;
        int64_t price;
        Mercury::Side side;
    };

    std::map<int64_t, Mercury::PriceLevel, std::greater<int64_t>> bids_;
    std::map<int64_t, Mercury::PriceLevel> asks_;
    LegacyHashMap<uint64_t, OrderLocation, LegacyOrderIdHash> orderLookup_;
    mutable Mercury::ObjectPool<Mercury::OrderNode> orderPool_;
};

}  // namespace MercuryBenchmarks
