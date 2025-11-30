#include "MatchingEngine.h"
#include <algorithm>
#include <iostream>
#include <limits>

namespace Mercury {

    MatchingEngine::MatchingEngine() = default;

    ExecutionResult MatchingEngine::submitOrder(Order order) {
        // Assign timestamp if not set
        if (order.timestamp == 0) {
            order.timestamp = getTimestamp();
        }

        // Comprehensive validation with specific reject reason
        RejectReason rejectReason = order.validate();
        if (rejectReason != RejectReason::None) {
            auto result = ExecutionResult::makeRejection(order.id, rejectReason);
            notifyExecution(result);
            return result;
        }

        // Check for duplicate order ID (for new orders, not cancel/modify)
        if (order.orderType == OrderType::Limit || order.orderType == OrderType::Market) {
            if (orderBook_.getOrder(order.id).has_value()) {
                auto result = ExecutionResult::makeRejection(order.id, RejectReason::DuplicateOrderId);
                notifyExecution(result);
                return result;
            }
        }

        ExecutionResult result;

        switch (order.orderType) {
            case OrderType::Limit:
                result = processLimitOrder(order);
                break;
            case OrderType::Market:
                result = processMarketOrder(order);
                break;
            case OrderType::Cancel:
                result = processCancelOrder(order);
                break;
            case OrderType::Modify:
                result = processModifyOrder(order);
                break;
            default:
                result = ExecutionResult::makeRejection(order.id, RejectReason::InvalidOrderType);
                break;
        }

        notifyExecution(result);
        return result;
    }

    ExecutionResult MatchingEngine::processLimitOrder(Order& order) {
        ExecutionResult result;
        result.orderId = order.id;

        // Edge case: zero quantity after previous partial fills
        if (order.quantity == 0) {
            return ExecutionResult::makeRejection(order.id, RejectReason::InvalidQuantity);
        }

        // Handle FOK (Fill-or-Kill): Check if we can fill completely
        if (order.tif == TimeInForce::FOK) {
            if (!canFillCompletely(order)) {
                result = ExecutionResult::makeRejection(order.id, RejectReason::FOKCannotFill);
                result.remainingQuantity = order.quantity;
                return result;
            }
        }

        // Store original quantity for reporting
        uint64_t originalQuantity = order.quantity;

        // Try to match against the opposite side
        std::vector<Trade> trades;
        matchOrder(order, trades);

        result.trades = std::move(trades);
        result.filledQuantity = originalQuantity - order.quantity;
        result.remainingQuantity = order.quantity;

        // Handle IOC (Immediate-or-Cancel): Don't rest in book
        if (order.tif == TimeInForce::IOC) {
            if (result.filledQuantity == 0) {
                result.status = ExecutionStatus::Cancelled;
                result.message = "IOC order not filled - no matching liquidity";
            } else if (order.quantity > 0) {
                result.status = ExecutionStatus::PartialFill;
                result.message = "IOC order partially filled, remainder cancelled";
            } else {
                result.status = ExecutionStatus::Filled;
                result.message = "IOC order fully filled";
            }
            return result;
        }

        // If order still has remaining quantity, add to book (GTC behavior)
        if (order.quantity > 0) {
            orderBook_.addOrder(order);
            
            if (result.hasFills()) {
                result.status = ExecutionStatus::PartialFill;
                result.message = "Partially filled, remainder resting in book";
            } else {
                result.status = ExecutionStatus::Resting;
                result.message = "Order added to book";
            }
        } else {
            result.status = ExecutionStatus::Filled;
            result.message = "Order fully filled";
        }

        return result;
    }

    ExecutionResult MatchingEngine::processMarketOrder(Order& order) {
        ExecutionResult result;
        result.orderId = order.id;

        // Edge case: zero quantity
        if (order.quantity == 0) {
            return ExecutionResult::makeRejection(order.id, RejectReason::InvalidQuantity);
        }

        // Market orders must have matching liquidity on the opposite side
        bool hasLiquidity = (order.side == Side::Buy) ? 
            orderBook_.hasAsks() : orderBook_.hasBids();

        if (!hasLiquidity) {
            result = ExecutionResult::makeRejection(order.id, RejectReason::NoLiquidity);
            result.remainingQuantity = order.quantity;
            return result;
        }

        // Handle FOK: Check if we can fill completely
        if (order.tif == TimeInForce::FOK) {
            if (!canFillCompletely(order)) {
                result = ExecutionResult::makeRejection(order.id, RejectReason::FOKCannotFill);
                result.remainingQuantity = order.quantity;
                return result;
            }
        }

        // Store original quantity
        uint64_t originalQuantity = order.quantity;

        // Match against the book
        std::vector<Trade> trades;
        matchOrder(order, trades);

        result.trades = std::move(trades);
        result.filledQuantity = originalQuantity - order.quantity;
        result.remainingQuantity = order.quantity;

        // Market orders don't rest - any unfilled quantity is cancelled
        if (order.quantity > 0) {
            if (result.filledQuantity > 0) {
                result.status = ExecutionStatus::PartialFill;
                result.message = "Partially filled, remainder cancelled (no more liquidity)";
            } else {
                result.status = ExecutionStatus::Cancelled;
                result.rejectReason = RejectReason::NoLiquidity;
                result.message = "Market order cancelled - insufficient liquidity";
            }
        } else {
            result.status = ExecutionStatus::Filled;
            result.message = "Market order fully filled";
        }

        return result;
    }

    ExecutionResult MatchingEngine::processCancelOrder(const Order& order) {
        return cancelOrder(order.id);
    }

    ExecutionResult MatchingEngine::cancelOrder(uint64_t orderId) {
        ExecutionResult result;
        result.orderId = orderId;

        // Edge case: invalid order ID
        if (orderId == 0) {
            return ExecutionResult::makeRejection(orderId, RejectReason::InvalidOrderId);
        }

        // Try to get the order first to verify it exists
        auto existingOrder = orderBook_.getOrder(orderId);
        if (!existingOrder.has_value()) {
            return ExecutionResult::makeRejection(orderId, RejectReason::OrderNotFound);
        }

        // Remove the order
        orderBook_.removeOrder(orderId);

        result.status = ExecutionStatus::Cancelled;
        result.remainingQuantity = existingOrder->quantity;
        result.message = "Order cancelled successfully";

        return result;
    }

    ExecutionResult MatchingEngine::processModifyOrder(const Order& order) {
        return modifyOrder(order.targetOrderId, order.newPrice, order.newQuantity);
    }

    ExecutionResult MatchingEngine::modifyOrder(uint64_t orderId, int64_t newPrice, uint64_t newQuantity) {
        ExecutionResult result;
        result.orderId = orderId;

        // Edge case: invalid order ID
        if (orderId == 0) {
            return ExecutionResult::makeRejection(orderId, RejectReason::InvalidOrderId);
        }

        // Edge case: no actual changes requested
        if (newPrice <= 0 && newQuantity == 0) {
            return ExecutionResult::makeRejection(orderId, RejectReason::ModifyNoChanges);
        }

        // Edge case: invalid new price
        if (newPrice < 0) {
            return ExecutionResult::makeRejection(orderId, RejectReason::InvalidPrice);
        }

        // Get the existing order
        auto existingOrder = orderBook_.getOrder(orderId);
        if (!existingOrder.has_value()) {
            return ExecutionResult::makeRejection(orderId, RejectReason::OrderNotFound);
        }

        Order modifiedOrder = existingOrder.value();

        // Check if there are actual changes
        bool hasChanges = false;

        // Apply modifications
        if (newPrice > 0 && newPrice != modifiedOrder.price) {
            modifiedOrder.price = newPrice;
            hasChanges = true;
        }

        if (newQuantity > 0 && newQuantity != modifiedOrder.quantity) {
            modifiedOrder.quantity = newQuantity;
            hasChanges = true;
        }

        if (!hasChanges) {
            return ExecutionResult::makeRejection(orderId, RejectReason::ModifyNoChanges);
        }

        // Remove the original order
        orderBook_.removeOrder(orderId);

        // Re-submit with new timestamp (loses time priority)
        modifiedOrder.timestamp = getTimestamp();
        
        // Check if the modified order would cross the book
        bool wouldCross = false;
        if (modifiedOrder.side == Side::Buy && orderBook_.hasAsks()) {
            wouldCross = modifiedOrder.price >= orderBook_.getBestAsk();
        } else if (modifiedOrder.side == Side::Sell && orderBook_.hasBids()) {
            wouldCross = modifiedOrder.price <= orderBook_.getBestBid();
        }

        if (wouldCross) {
            // Process as a new limit order (may result in fills)
            result = processLimitOrder(modifiedOrder);
            // Adjust status to indicate this was a modify
            if (result.status != ExecutionStatus::Rejected) {
                if (result.status == ExecutionStatus::Filled) {
                    result.message = "Order modified and fully filled";
                } else {
                    result.status = ExecutionStatus::Modified;
                    result.message = "Order modified and partially matched";
                }
            }
        } else {
            // Just add to book
            orderBook_.addOrder(modifiedOrder);
            result.status = ExecutionStatus::Modified;
            result.remainingQuantity = modifiedOrder.quantity;
            result.message = "Order modified successfully";
        }

        return result;
    }

    bool MatchingEngine::matchOrder(Order& order, std::vector<Trade>& trades) {
        bool matched = false;

        // Safety check: order must have quantity
        if (order.quantity == 0) {
            return false;
        }

        // Determine which side to match against
        if (order.side == Side::Buy) {
            // Buy orders match against asks (sellers)
            while (order.quantity > 0 && orderBook_.hasAsks()) {
                int64_t bestAsk = orderBook_.getBestAsk();
                
                // Check if price is acceptable
                if (!isPriceAcceptable(order, bestAsk)) {
                    break;  // No more matchable prices
                }

                uint64_t filledQty = matchAtPriceLevel(order, bestAsk, trades);
                if (filledQty > 0) {
                    matched = true;
                }
            }
        } else {
            // Sell orders match against bids (buyers)
            while (order.quantity > 0 && orderBook_.hasBids()) {
                int64_t bestBid = orderBook_.getBestBid();
                
                // Check if price is acceptable
                if (!isPriceAcceptable(order, bestBid)) {
                    break;  // No more matchable prices
                }

                uint64_t filledQty = matchAtPriceLevel(order, bestBid, trades);
                if (filledQty > 0) {
                    matched = true;
                }
            }
        }

        return matched;
    }

    uint64_t MatchingEngine::matchAtPriceLevel(Order& order, int64_t priceLevel, 
                                                std::vector<Trade>& trades) {
        uint64_t totalFilled = 0;

        // Get orders at this price level
        auto ordersAtLevel = orderBook_.getOrdersAtPrice(priceLevel, 
            order.side == Side::Buy ? Side::Sell : Side::Buy);

        // Edge case: empty price level (shouldn't happen but be safe)
        if (ordersAtLevel.empty()) {
            return 0;
        }

        for (const auto& restingOrder : ordersAtLevel) {
            if (order.quantity == 0) break;

            // Self-trade prevention: skip if same client ID
            if (order.clientId != 0 && order.clientId == restingOrder.clientId) {
                continue;  // Skip this order to prevent self-trade
            }

            // Calculate fill quantity (with overflow protection)
            uint64_t fillQty = std::min(order.quantity, restingOrder.quantity);
            
            // Edge case: zero fill (shouldn't happen)
            if (fillQty == 0) continue;

            // Create trade record
            Trade trade;
            trade.tradeId = generateTradeId();
            trade.price = priceLevel;
            trade.quantity = fillQty;
            trade.timestamp = getTimestamp();

            if (order.side == Side::Buy) {
                trade.buyOrderId = order.id;
                trade.sellOrderId = restingOrder.id;
            } else {
                trade.buyOrderId = restingOrder.id;
                trade.sellOrderId = order.id;
            }

            trades.push_back(trade);
            notifyTrade(trade);

            // Update statistics (with overflow protection)
            tradeCount_++;
            if (totalVolume_ <= std::numeric_limits<uint64_t>::max() - fillQty) {
                totalVolume_ += fillQty;
            }

            // Update quantities
            order.quantity -= fillQty;
            totalFilled += fillQty;

            // Update or remove the resting order
            if (fillQty == restingOrder.quantity) {
                // Fully filled - remove from book
                orderBook_.removeOrder(restingOrder.id);
            } else {
                // Partially filled - update quantity
                orderBook_.updateOrderQuantity(restingOrder.id, 
                    restingOrder.quantity - fillQty);
            }
        }

        return totalFilled;
    }

    bool MatchingEngine::isPriceAcceptable(const Order& order, int64_t priceLevel) const {
        // Market orders accept any price
        if (order.orderType == OrderType::Market) {
            return true;
        }

        // Edge case: negative price level (shouldn't happen)
        if (priceLevel < 0) {
            return false;
        }

        // Limit order price check
        if (order.side == Side::Buy) {
            // Buyers want to buy at or below their limit price
            return priceLevel <= order.price;
        } else {
            // Sellers want to sell at or above their limit price
            return priceLevel >= order.price;
        }
    }

    bool MatchingEngine::canFillCompletely(const Order& order) const {
        // Edge case: zero quantity can always be "filled"
        if (order.quantity == 0) {
            return true;
        }

        uint64_t remainingQty = order.quantity;

        if (order.side == Side::Buy) {
            // Check ask side
            if (!orderBook_.hasAsks()) {
                return false;
            }
            
            const auto& askLevels = orderBook_.getAskLevels();
            for (const auto& [price, orders] : askLevels) {
                if (!isPriceAcceptable(order, price)) break;
                
                for (const auto& o : orders) {
                    if (remainingQty <= o.quantity) {
                        return true;  // Can fill completely
                    }
                    remainingQty -= o.quantity;
                }
            }
        } else {
            // Check bid side
            if (!orderBook_.hasBids()) {
                return false;
            }
            
            const auto& bidLevels = orderBook_.getBidLevels();
            for (const auto& [price, orders] : bidLevels) {
                if (!isPriceAcceptable(order, price)) break;
                
                for (const auto& o : orders) {
                    if (remainingQty <= o.quantity) {
                        return true;  // Can fill completely
                    }
                    remainingQty -= o.quantity;
                }
            }
        }

        return remainingQty == 0;
    }

    void MatchingEngine::notifyTrade(const Trade& trade) {
        if (tradeCallback_) {
            tradeCallback_(trade);
        }
    }

    void MatchingEngine::notifyExecution(const ExecutionResult& result) {
        if (executionCallback_) {
            executionCallback_(result);
        }
    }

}
