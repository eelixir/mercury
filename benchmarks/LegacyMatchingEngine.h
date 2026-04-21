#pragma once

#include "LegacyOrderBook.h"
#include "MarketData.h"
#include "Order.h"

#include <chrono>
#include <algorithm>
#include <atomic>
#include <functional>
#include <limits>
#include <vector>

namespace MercuryBenchmarks {

class LegacyMatchingEngine {
public:
    using TradeCallback = std::function<void(const Mercury::Trade&)>;
    using ExecutionCallback = std::function<void(const Mercury::ExecutionResult&)>;
    using BookMutationCallback = std::function<void(const Mercury::BookMutation&)>;

    LegacyMatchingEngine() = default;

    Mercury::ExecutionResult submitOrder(Mercury::Order order) {
        if (order.timestamp == 0) {
            order.timestamp = getTimestamp();
        }

        const Mercury::RejectReason rejectReason = order.validate();
        if (rejectReason != Mercury::RejectReason::None) {
            auto result = Mercury::ExecutionResult::makeRejection(order.id, rejectReason);
            notifyExecution(result);
            return result;
        }

        if (order.orderType == Mercury::OrderType::Limit ||
            order.orderType == Mercury::OrderType::Market) {
            if (orderBook_.getOrder(order.id).has_value()) {
                auto result = Mercury::ExecutionResult::makeRejection(
                    order.id, Mercury::RejectReason::DuplicateOrderId);
                notifyExecution(result);
                return result;
            }
        }

        Mercury::ExecutionResult result;
        switch (order.orderType) {
        case Mercury::OrderType::Limit:
            result = processLimitOrder(order);
            break;
        case Mercury::OrderType::Market:
            result = processMarketOrder(order);
            break;
        case Mercury::OrderType::Cancel:
            result = processCancelOrder(order);
            break;
        case Mercury::OrderType::Modify:
            result = processModifyOrder(order);
            break;
        default:
            result = Mercury::ExecutionResult::makeRejection(
                order.id, Mercury::RejectReason::InvalidOrderType);
            break;
        }

        notifyExecution(result);
        return result;
    }

    Mercury::ExecutionResult cancelOrder(uint64_t orderId) {
        Mercury::ExecutionResult result;
        result.orderId = orderId;

        if (orderId == 0) {
            return Mercury::ExecutionResult::makeRejection(
                orderId, Mercury::RejectReason::InvalidOrderId);
        }

        auto existingOrder = orderBook_.getOrder(orderId);
        if (!existingOrder.has_value()) {
            return Mercury::ExecutionResult::makeRejection(
                orderId, Mercury::RejectReason::OrderNotFound);
        }

        const Mercury::Side side = existingOrder->side;
        const int64_t price = existingOrder->price;
        orderBook_.removeOrder(orderId);
        notifyBookMutation(side, price, Mercury::BookDeltaAction::Remove);
        result.status = Mercury::ExecutionStatus::Cancelled;
        result.remainingQuantity = existingOrder->quantity;
        result.message = "Order cancelled successfully";
        return result;
    }

    Mercury::ExecutionResult modifyOrder(uint64_t orderId, int64_t newPrice, uint64_t newQuantity) {
        Mercury::ExecutionResult result;
        result.orderId = orderId;

        if (orderId == 0) {
            return Mercury::ExecutionResult::makeRejection(
                orderId, Mercury::RejectReason::InvalidOrderId);
        }

        if (newPrice <= 0 && newQuantity == 0) {
            return Mercury::ExecutionResult::makeRejection(
                orderId, Mercury::RejectReason::ModifyNoChanges);
        }

        if (newPrice < 0) {
            return Mercury::ExecutionResult::makeRejection(
                orderId, Mercury::RejectReason::InvalidPrice);
        }

        auto existingOrder = orderBook_.getOrder(orderId);
        if (!existingOrder.has_value()) {
            return Mercury::ExecutionResult::makeRejection(
                orderId, Mercury::RejectReason::OrderNotFound);
        }

        Mercury::Order modifiedOrder = existingOrder.value();
        const Mercury::Side originalSide = modifiedOrder.side;
        const int64_t originalPrice = modifiedOrder.price;

        bool hasChanges = false;
        if (newPrice > 0 && newPrice != modifiedOrder.price) {
            modifiedOrder.price = newPrice;
            hasChanges = true;
        }

        if (newQuantity > 0 && newQuantity != modifiedOrder.quantity) {
            modifiedOrder.quantity = newQuantity;
            hasChanges = true;
        }

        if (!hasChanges) {
            return Mercury::ExecutionResult::makeRejection(
                orderId, Mercury::RejectReason::ModifyNoChanges);
        }

        orderBook_.removeOrder(orderId);
        notifyBookMutation(originalSide, originalPrice, Mercury::BookDeltaAction::Remove);
        modifiedOrder.timestamp = getTimestamp();

        bool wouldCross = false;
        if (modifiedOrder.side == Mercury::Side::Buy && orderBook_.hasAsks()) {
            wouldCross = modifiedOrder.price >= orderBook_.getBestAsk();
        } else if (modifiedOrder.side == Mercury::Side::Sell && orderBook_.hasBids()) {
            wouldCross = modifiedOrder.price <= orderBook_.getBestBid();
        }

        if (wouldCross) {
            result = processLimitOrder(modifiedOrder);
            if (result.status != Mercury::ExecutionStatus::Rejected) {
                if (result.status == Mercury::ExecutionStatus::Filled) {
                    result.message = "Order modified and fully filled";
                } else {
                    result.status = Mercury::ExecutionStatus::Modified;
                    result.message = "Order modified and partially matched";
                }
            }
        } else {
            orderBook_.addOrder(modifiedOrder);
            notifyBookMutation(
                modifiedOrder.side, modifiedOrder.price, Mercury::BookDeltaAction::Upsert);
            result.status = Mercury::ExecutionStatus::Modified;
            result.remainingQuantity = modifiedOrder.quantity;
            result.message = "Order modified successfully";
        }

        return result;
    }

    uint64_t getTradeCount() const { return tradeCount_; }
    uint64_t getTotalVolume() const { return totalVolume_; }

private:
    Mercury::ExecutionResult processLimitOrder(Mercury::Order& order) {
        Mercury::ExecutionResult result;
        result.orderId = order.id;

        if (order.quantity == 0) {
            return Mercury::ExecutionResult::makeRejection(
                order.id, Mercury::RejectReason::InvalidQuantity);
        }

        if (order.tif == Mercury::TimeInForce::FOK && !canFillCompletely(order)) {
            result = Mercury::ExecutionResult::makeRejection(
                order.id, Mercury::RejectReason::FOKCannotFill);
            result.remainingQuantity = order.quantity;
            return result;
        }

        const uint64_t originalQuantity = order.quantity;
        std::vector<Mercury::Trade> trades;
        matchOrder(order, trades);

        result.trades = std::move(trades);
        result.filledQuantity = originalQuantity - order.quantity;
        result.remainingQuantity = order.quantity;

        if (order.tif == Mercury::TimeInForce::IOC) {
            if (result.filledQuantity == 0) {
                result.status = Mercury::ExecutionStatus::Cancelled;
                result.message = "IOC order not filled - no matching liquidity";
            } else if (order.quantity > 0) {
                result.status = Mercury::ExecutionStatus::PartialFill;
                result.message = "IOC order partially filled, remainder cancelled";
            } else {
                result.status = Mercury::ExecutionStatus::Filled;
                result.message = "IOC order fully filled";
            }
            return result;
        }

        if (order.quantity > 0) {
            orderBook_.addOrder(order);
            notifyBookMutation(order.side, order.price, Mercury::BookDeltaAction::Upsert);
            if (result.hasFills()) {
                result.status = Mercury::ExecutionStatus::PartialFill;
                result.message = "Partially filled, remainder resting in book";
            } else {
                result.status = Mercury::ExecutionStatus::Resting;
                result.message = "Order added to book";
            }
        } else {
            result.status = Mercury::ExecutionStatus::Filled;
            result.message = "Order fully filled";
        }

        return result;
    }

    Mercury::ExecutionResult processMarketOrder(Mercury::Order& order) {
        Mercury::ExecutionResult result;
        result.orderId = order.id;

        if (order.quantity == 0) {
            return Mercury::ExecutionResult::makeRejection(
                order.id, Mercury::RejectReason::InvalidQuantity);
        }

        const bool hasLiquidity = (order.side == Mercury::Side::Buy)
            ? orderBook_.hasAsks()
            : orderBook_.hasBids();

        if (!hasLiquidity) {
            result = Mercury::ExecutionResult::makeRejection(
                order.id, Mercury::RejectReason::NoLiquidity);
            result.remainingQuantity = order.quantity;
            return result;
        }

        if (order.tif == Mercury::TimeInForce::FOK && !canFillCompletely(order)) {
            result = Mercury::ExecutionResult::makeRejection(
                order.id, Mercury::RejectReason::FOKCannotFill);
            result.remainingQuantity = order.quantity;
            return result;
        }

        const uint64_t originalQuantity = order.quantity;
        std::vector<Mercury::Trade> trades;
        matchOrder(order, trades);

        result.trades = std::move(trades);
        result.filledQuantity = originalQuantity - order.quantity;
        result.remainingQuantity = order.quantity;

        if (order.quantity > 0) {
            if (result.filledQuantity > 0) {
                result.status = Mercury::ExecutionStatus::PartialFill;
                result.message = "Partially filled, remainder cancelled (no more liquidity)";
            } else {
                result.status = Mercury::ExecutionStatus::Cancelled;
                result.rejectReason = Mercury::RejectReason::NoLiquidity;
                result.message = "Market order cancelled - insufficient liquidity";
            }
        } else {
            result.status = Mercury::ExecutionStatus::Filled;
            result.message = "Market order fully filled";
        }

        return result;
    }

    Mercury::ExecutionResult processCancelOrder(const Mercury::Order& order) {
        return cancelOrder(order.id);
    }

    Mercury::ExecutionResult processModifyOrder(const Mercury::Order& order) {
        return modifyOrder(order.targetOrderId, order.newPrice, order.newQuantity);
    }

    bool matchOrder(Mercury::Order& order, std::vector<Mercury::Trade>& trades) {
        bool matched = false;

        if (order.quantity == 0) {
            return false;
        }

        if (order.side == Mercury::Side::Buy) {
            while (order.quantity > 0 && orderBook_.hasAsks()) {
                const int64_t bestAsk = orderBook_.getBestAsk();
                if (!isPriceAcceptable(order, bestAsk)) {
                    break;
                }

                const uint64_t filledQty = matchAtPriceLevel(order, bestAsk, trades);
                if (filledQty > 0) {
                    matched = true;
                } else {
                    break;
                }
            }
        } else {
            while (order.quantity > 0 && orderBook_.hasBids()) {
                const int64_t bestBid = orderBook_.getBestBid();
                if (!isPriceAcceptable(order, bestBid)) {
                    break;
                }

                const uint64_t filledQty = matchAtPriceLevel(order, bestBid, trades);
                if (filledQty > 0) {
                    matched = true;
                } else {
                    break;
                }
            }
        }

        return matched;
    }

    uint64_t matchAtPriceLevel(
        Mercury::Order& order,
        int64_t priceLevel,
        std::vector<Mercury::Trade>& trades) {
        uint64_t totalFilled = 0;

        Mercury::PriceLevel* level = nullptr;
        if (order.side == Mercury::Side::Buy) {
            level = orderBook_.getAskLevel(priceLevel);
        } else {
            level = orderBook_.getBidLevel(priceLevel);
        }

        if (!level || level->empty()) {
            return 0;
        }

        std::vector<uint64_t> ordersToProcess;
        ordersToProcess.reserve(std::min(level->size(), size_t(32)));

        for (const auto& restingNode : *level) {
            if (order.quantity == 0) {
                break;
            }

            if (order.clientId != 0 && order.clientId == restingNode.clientId) {
                continue;
            }

            ordersToProcess.push_back(restingNode.id);
        }

        for (uint64_t restingOrderId : ordersToProcess) {
            if (order.quantity == 0) {
                break;
            }

            auto restingOrderOpt = orderBook_.getOrder(restingOrderId);
            if (!restingOrderOpt) {
                continue;
            }

            const Mercury::Order& restingOrder = *restingOrderOpt;
            const uint64_t fillQty = std::min(order.quantity, restingOrder.quantity);
            if (fillQty == 0) {
                continue;
            }

            Mercury::Trade trade;
            trade.tradeId = generateTradeId();
            trade.price = priceLevel;
            trade.quantity = fillQty;
            trade.timestamp = getTimestamp();

            if (order.side == Mercury::Side::Buy) {
                trade.buyOrderId = order.id;
                trade.sellOrderId = restingOrder.id;
                trade.buyClientId = order.clientId;
                trade.sellClientId = restingOrder.clientId;
            } else {
                trade.buyOrderId = restingOrder.id;
                trade.sellOrderId = order.id;
                trade.buyClientId = restingOrder.clientId;
                trade.sellClientId = order.clientId;
            }

            trades.push_back(trade);
            notifyTrade(trade);
            ++tradeCount_;
            if (totalVolume_ <= std::numeric_limits<uint64_t>::max() - fillQty) {
                totalVolume_ += fillQty;
            }

            order.quantity -= fillQty;
            totalFilled += fillQty;

            if (fillQty == restingOrder.quantity) {
                orderBook_.removeOrder(restingOrder.id);
                notifyBookMutation(restingOrder.side, priceLevel, Mercury::BookDeltaAction::Remove);
            } else {
                orderBook_.updateOrderQuantity(restingOrder.id, restingOrder.quantity - fillQty);
                notifyBookMutation(restingOrder.side, priceLevel, Mercury::BookDeltaAction::Upsert);
            }
        }

        return totalFilled;
    }

    bool isPriceAcceptable(const Mercury::Order& order, int64_t priceLevel) const {
        if (order.orderType == Mercury::OrderType::Market) {
            return true;
        }

        if (priceLevel < 0) {
            return false;
        }

        if (order.side == Mercury::Side::Buy) {
            return priceLevel <= order.price;
        }
        return priceLevel >= order.price;
    }

    bool canFillCompletely(const Mercury::Order& order) const {
        if (order.quantity == 0) {
            return true;
        }

        uint64_t remainingQty = order.quantity;

        if (order.side == Mercury::Side::Buy) {
            if (!orderBook_.hasAsks()) {
                return false;
            }

            const auto& askLevels = orderBook_.getAskLevels();
            for (const auto& [price, level] : askLevels) {
                if (!isPriceAcceptable(order, price)) {
                    break;
                }

                for (const auto& node : level) {
                    if (remainingQty <= node.quantity) {
                        return true;
                    }
                    remainingQty -= node.quantity;
                }
            }
        } else {
            if (!orderBook_.hasBids()) {
                return false;
            }

            const auto& bidLevels = orderBook_.getBidLevels();
            for (const auto& [price, level] : bidLevels) {
                if (!isPriceAcceptable(order, price)) {
                    break;
                }

                for (const auto& node : level) {
                    if (remainingQty <= node.quantity) {
                        return true;
                    }
                    remainingQty -= node.quantity;
                }
            }
        }

        return remainingQty == 0;
    }

    uint64_t getTimestamp() { return ++currentTimestamp_; }
    uint64_t generateTradeId() { return ++tradeIdCounter_; }

    void notifyTrade(const Mercury::Trade& trade) {
        if (tradeCallback_) {
            tradeCallback_(trade);
        }
    }

    void notifyExecution(const Mercury::ExecutionResult& result) {
        if (executionCallback_) {
            executionCallback_(result);
        }
    }

    void notifyBookMutation(
        Mercury::Side side,
        int64_t price,
        Mercury::BookDeltaAction action) {
        if (!bookMutationCallback_) {
            return;
        }

        Mercury::BookMutation mutation;
        mutation.side = side;
        mutation.price = price;
        mutation.quantity = orderBook_.getQuantityAtPrice(price, side);
        mutation.orderCount = orderBook_.getOrderCountAtPrice(price, side);
        mutation.action = (mutation.quantity == 0 || mutation.orderCount == 0)
            ? Mercury::BookDeltaAction::Remove
            : action;

        auto now = std::chrono::time_point_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now());
        mutation.timestamp = static_cast<uint64_t>(now.time_since_epoch().count());

        bookMutationCallback_(mutation);
    }

    LegacyOrderBook orderBook_;
    std::atomic<uint64_t> tradeIdCounter_{0};
    std::atomic<uint64_t> currentTimestamp_{0};
    uint64_t tradeCount_ = 0;
    uint64_t totalVolume_ = 0;
    TradeCallback tradeCallback_;
    ExecutionCallback executionCallback_;
    BookMutationCallback bookMutationCallback_;
};

}  // namespace MercuryBenchmarks
