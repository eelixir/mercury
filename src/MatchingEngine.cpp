#include "MatchingEngine.h"
#include <algorithm>
#include <iostream>

namespace Mercury {

    MatchingEngine::MatchingEngine() = default;

    ExecutionResult MatchingEngine::submitOrder(Order order) {
        // Assign timestamp if not set
        if (order.timestamp == 0) {
            order.timestamp = getTimestamp();
        }

        // Validate order
        if (!order.isValid()) {
            ExecutionResult result;
            result.status = ExecutionStatus::Rejected;
            result.orderId = order.id;
            result.message = "Invalid order";
            notifyExecution(result);
            return result;
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
                result.status = ExecutionStatus::Rejected;
                result.orderId = order.id;
                result.message = "Unknown order type";
                break;
        }

        notifyExecution(result);
        return result;
    }

    ExecutionResult MatchingEngine::processLimitOrder(Order& order) {
        ExecutionResult result;
        result.orderId = order.id;

        // Handle FOK (Fill-or-Kill): Check if we can fill completely
        if (order.tif == TimeInForce::FOK) {
            if (!canFillCompletely(order)) {
                result.status = ExecutionStatus::Rejected;
                result.remainingQuantity = order.quantity;
                result.message = "FOK order cannot be filled completely";
                return result;
            }
        }

        // Try to match against the opposite side
        std::vector<Trade> trades;
        matchOrder(order, trades);

        result.trades = std::move(trades);
        result.filledQuantity = result.trades.empty() ? 0 : 
            [&]() {
                uint64_t total = 0;
                for (const auto& t : result.trades) total += t.quantity;
                return total;
            }();
        result.remainingQuantity = order.quantity;

        // Handle IOC (Immediate-or-Cancel): Don't rest in book
        if (order.tif == TimeInForce::IOC) {
            if (result.filledQuantity == 0) {
                result.status = ExecutionStatus::Cancelled;
                result.message = "IOC order not filled";
            } else if (order.quantity > 0) {
                result.status = ExecutionStatus::PartialFill;
                result.message = "IOC order partially filled, remainder cancelled";
            } else {
                result.status = ExecutionStatus::Filled;
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

        // Market orders must have matching liquidity on the opposite side
        bool hasLiquidity = (order.side == Side::Buy) ? 
            orderBook_.hasAsks() : orderBook_.hasBids();

        if (!hasLiquidity) {
            result.status = ExecutionStatus::Rejected;
            result.remainingQuantity = order.quantity;
            result.message = "No liquidity available for market order";
            return result;
        }

        // Handle FOK: Check if we can fill completely
        if (order.tif == TimeInForce::FOK) {
            if (!canFillCompletely(order)) {
                result.status = ExecutionStatus::Rejected;
                result.remainingQuantity = order.quantity;
                result.message = "FOK market order cannot be filled completely";
                return result;
            }
        }

        // Match against the book
        std::vector<Trade> trades;
        matchOrder(order, trades);

        result.trades = std::move(trades);
        result.filledQuantity = 0;
        for (const auto& t : result.trades) {
            result.filledQuantity += t.quantity;
        }
        result.remainingQuantity = order.quantity;

        // Market orders don't rest - any unfilled quantity is cancelled
        if (order.quantity > 0) {
            if (result.filledQuantity > 0) {
                result.status = ExecutionStatus::PartialFill;
                result.message = "Partially filled, remainder cancelled (no more liquidity)";
            } else {
                result.status = ExecutionStatus::Cancelled;
                result.message = "Market order cancelled (no liquidity)";
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

        // Try to get the order first to verify it exists
        auto existingOrder = orderBook_.getOrder(orderId);
        if (!existingOrder.has_value()) {
            result.status = ExecutionStatus::Rejected;
            result.message = "Order not found";
            return result;
        }

        // Remove the order
        orderBook_.removeOrder(orderId);

        result.status = ExecutionStatus::Cancelled;
        result.remainingQuantity = existingOrder->quantity;
        result.message = "Order cancelled";

        return result;
    }

    ExecutionResult MatchingEngine::processModifyOrder(const Order& order) {
        return modifyOrder(order.targetOrderId, order.newPrice, order.newQuantity);
    }

    ExecutionResult MatchingEngine::modifyOrder(uint64_t orderId, int64_t newPrice, uint64_t newQuantity) {
        ExecutionResult result;
        result.orderId = orderId;

        // Get the existing order
        auto existingOrder = orderBook_.getOrder(orderId);
        if (!existingOrder.has_value()) {
            result.status = ExecutionStatus::Rejected;
            result.message = "Order not found for modification";
            return result;
        }

        Order modifiedOrder = existingOrder.value();

        // Apply modifications
        bool priceChanged = false;
        if (newPrice > 0 && newPrice != modifiedOrder.price) {
            modifiedOrder.price = newPrice;
            priceChanged = true;
        }

        if (newQuantity > 0) {
            modifiedOrder.quantity = newQuantity;
        }

        // If price changed, we need to remove and re-add (loses time priority)
        // If only quantity decreased, we can keep priority
        // If quantity increased, typically loses priority (implementation choice)
        
        // For simplicity, we always cancel and re-add on any modification
        // This is the safest approach and matches most exchange behavior
        orderBook_.removeOrder(orderId);

        // Re-submit as a new order (keeping the same ID for tracking)
        modifiedOrder.timestamp = getTimestamp();
        
        // Check if the modified order would cross the book
        // If so, we need to match it
        bool wouldCross = false;
        if (modifiedOrder.side == Side::Buy && orderBook_.hasAsks()) {
            wouldCross = modifiedOrder.price >= orderBook_.getBestAsk();
        } else if (modifiedOrder.side == Side::Sell && orderBook_.hasBids()) {
            wouldCross = modifiedOrder.price <= orderBook_.getBestBid();
        }

        if (wouldCross) {
            // Process as a new limit order (may result in fills)
            result = processLimitOrder(modifiedOrder);
            result.status = (result.status == ExecutionStatus::Filled) ? 
                ExecutionStatus::Filled : ExecutionStatus::Modified;
            result.message = "Order modified and matched";
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

        for (const auto& restingOrder : ordersAtLevel) {
            if (order.quantity == 0) break;

            // Calculate fill quantity
            uint64_t fillQty = std::min(order.quantity, restingOrder.quantity);

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

            // Update statistics
            tradeCount_++;
            totalVolume_ += fillQty;

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
        uint64_t remainingQty = order.quantity;

        if (order.side == Side::Buy) {
            // Check ask side
            auto askLevels = orderBook_.getAskLevels();
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
            auto bidLevels = orderBook_.getBidLevels();
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
