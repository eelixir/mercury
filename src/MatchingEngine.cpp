#include "MatchingEngine.h"
#include "BenchTiming.h"
#include "PriceLevel.h"
#include <algorithm>
#include <chrono>
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
            if (orderBook_.hasOrder(order.id)) {
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
            if (!orderBook_.addOrder(order)) {
                // Pool exhausted or internal failure - remainder cannot rest.
                // Preserve any fills we already produced but mark the rest
                // as rejected so the caller sees an accurate picture.
                result.status = ExecutionStatus::Rejected;
                result.rejectReason = RejectReason::InternalError;
                result.message = "Failed to rest order in book (pool exhausted)";
                return result;
            }
            notifyBookMutation(order.side, order.price, BookDeltaAction::Upsert);

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
        OrderNode* existingOrder = orderBook_.getOrderNode(orderId);
        if (!existingOrder) {
            return ExecutionResult::makeRejection(orderId, RejectReason::OrderNotFound);
        }

        const Side side = existingOrder->side;
        const int64_t price = existingOrder->price;
        const uint64_t remainingQuantity = existingOrder->quantity;

        // Remove the order
        orderBook_.removeOrder(existingOrder);
        notifyBookMutation(side, price, BookDeltaAction::Remove);

        result.status = ExecutionStatus::Cancelled;
        result.remainingQuantity = remainingQuantity;
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
        OrderNode* existingOrder = orderBook_.getOrderNode(orderId);
        if (!existingOrder) {
            return ExecutionResult::makeRejection(orderId, RejectReason::OrderNotFound);
        }

        Order modifiedOrder = existingOrder->toOrder();
        const Side originalSide = modifiedOrder.side;
        const int64_t originalPrice = modifiedOrder.price;

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
        orderBook_.removeOrder(existingOrder);
        notifyBookMutation(originalSide, originalPrice, BookDeltaAction::Remove);

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
            if (!orderBook_.addOrder(modifiedOrder)) {
                // Original was already removed above; we cannot re-rest it.
                // Report as a rejected modify so the client sees the loss.
                result.status = ExecutionStatus::Rejected;
                result.rejectReason = RejectReason::InternalError;
                result.remainingQuantity = modifiedOrder.quantity;
                result.message = "Modify failed: could not re-rest order (pool exhausted)";
                return result;
            }
            notifyBookMutation(modifiedOrder.side, modifiedOrder.price, BookDeltaAction::Upsert);
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
                } else {
                    // No fills at this level (e.g., all orders blocked by self-trade prevention)
                    // Must break to avoid infinite loop
                    break;
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
                } else {
                    // No fills at this level (e.g., all orders blocked by self-trade prevention)
                    // Must break to avoid infinite loop
                    break;
                }
            }
        }

        return matched;
    }

    uint64_t MatchingEngine::matchAtPriceLevel(Order& order, int64_t priceLevel, 
                                                std::vector<Trade>& trades) {
        uint64_t totalFilled = 0;

        // Get the price level directly for efficient iteration
        PriceLevel* level = nullptr;
        if (order.side == Side::Buy) {
            level = orderBook_.getAskLevel(priceLevel);
        } else {
            level = orderBook_.getBidLevel(priceLevel);
        }

        // Edge case: empty price level (shouldn't happen but be safe)
        if (!level || level->empty()) {
            return 0;
        }

        // Snapshot identifiers of candidate resting orders up front. We capture
        // {id, clientId} rather than raw OrderNode* because notifyTrade() below
        // invokes external callbacks that may re-enter the engine (cancel/
        // modify/submit) and release nodes back to the object pool. Re-looking
        // up by id on every iteration guarantees we never dereference freed
        // memory.
        struct Candidate {
            uint64_t id;
            uint64_t clientId;
        };
        std::vector<Candidate> ordersToProcess;
        ordersToProcess.reserve(std::min(level->size(), size_t(32)));

        {
            MERCURY_BENCH_SCOPE(Mercury::BenchTiming::Category::PriceLevelIteration);
            for (auto& restingNode : *level) {
                if (order.quantity == 0) break;

                // Self-trade prevention: skip if same client ID
                if (order.clientId != 0 && order.clientId == restingNode.clientId) {
                    continue;
                }

                ordersToProcess.push_back({restingNode.id, restingNode.clientId});
            }
        }

        // Now process the collected orders. Re-resolve by id each iteration so
        // nodes released by a callback earlier in the loop cannot be touched.
        for (const Candidate& candidate : ordersToProcess) {
            if (order.quantity == 0) break;
            if (candidate.id == 0) continue;

            OrderNode* restingOrder = orderBook_.getOrderNode(candidate.id);
            if (!restingOrder || restingOrder->quantity == 0) {
                // Order was cancelled/consumed by a re-entrant callback from a
                // previous fill in this loop - skip safely.
                continue;
            }

            // Calculate fill quantity (with overflow protection)
            const uint64_t restingQuantity = restingOrder->quantity;
            const uint64_t fillQty = std::min(order.quantity, restingQuantity);
            // Cache side up front: notifyTrade() below may trigger a re-entrant
            // cancel that releases this node before we read it again.
            const Side restingSide = restingOrder->side;

            // Edge case: zero fill (shouldn't happen)
            if (fillQty == 0) continue;

            {
                MERCURY_BENCH_SCOPE(Mercury::BenchTiming::Category::CallbackResult);

                // Create trade record
                Trade trade;
                trade.tradeId = generateTradeId();
                trade.price = priceLevel;
                trade.quantity = fillQty;
                trade.timestamp = getTimestamp();

                if (order.side == Side::Buy) {
                    trade.buyOrderId = order.id;
                    trade.sellOrderId = candidate.id;
                    trade.buyClientId = order.clientId;
                    trade.sellClientId = candidate.clientId;
                } else {
                    trade.buyOrderId = candidate.id;
                    trade.sellOrderId = order.id;
                    trade.buyClientId = candidate.clientId;
                    trade.sellClientId = order.clientId;
                }

                trades.push_back(trade);
                notifyTrade(trade);

                // Update statistics (with overflow protection)
                tradeCount_++;
                if (totalVolume_ <= std::numeric_limits<uint64_t>::max() - fillQty) {
                    totalVolume_ += fillQty;
                }
            }

            // Update quantities
            order.quantity -= fillQty;
            totalFilled += fillQty;

            // Update or remove the resting order. Re-resolve by id in case a
            // notifyTrade callback above cancelled this order (which would
            // release `restingOrder` back to the pool).
            OrderNode* liveNode = orderBook_.getOrderNode(candidate.id);
            if (!liveNode) {
                // Callback already removed it - nothing for us to do.
                continue;
            }
            if (fillQty == restingQuantity) {
                // Fully filled - remove from book
                orderBook_.removeOrder(liveNode);
                notifyBookMutation(restingSide, priceLevel, BookDeltaAction::Remove);
            } else {
                // Partially filled - update quantity
                orderBook_.updateOrderQuantity(liveNode, restingQuantity - fillQty);
                notifyBookMutation(restingSide, priceLevel, BookDeltaAction::Upsert);
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
            {
                MERCURY_BENCH_SCOPE(Mercury::BenchTiming::Category::PriceLevelIteration);
                for (const auto& [price, level] : askLevels) {
                    if (!isPriceAcceptable(order, price)) break;

                    for (const auto& node : level) {
                        if (remainingQty <= node.quantity) {
                            return true;  // Can fill completely
                        }
                        remainingQty -= node.quantity;
                    }
                }
            }
        } else {
            // Check bid side
            if (!orderBook_.hasBids()) {
                return false;
            }
            
            const auto& bidLevels = orderBook_.getBidLevels();
            {
                MERCURY_BENCH_SCOPE(Mercury::BenchTiming::Category::PriceLevelIteration);
                for (const auto& [price, level] : bidLevels) {
                    if (!isPriceAcceptable(order, price)) break;

                    for (const auto& node : level) {
                        if (remainingQty <= node.quantity) {
                            return true;  // Can fill completely
                        }
                        remainingQty -= node.quantity;
                    }
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

    void MatchingEngine::notifyBookMutation(Side side, int64_t price, BookDeltaAction action) {
        if (!bookMutationCallback_) {
            return;
        }

        const uint64_t quantity = orderBook_.getQuantityAtPrice(price, side);
        const size_t orderCount = orderBook_.getOrderCountAtPrice(price, side);

        MERCURY_BENCH_SCOPE(Mercury::BenchTiming::Category::CallbackResult);
        BookMutation mutation;
        mutation.side = side;
        mutation.price = price;
        mutation.quantity = quantity;
        mutation.orderCount = orderCount;
        mutation.action = (mutation.quantity == 0 || mutation.orderCount == 0)
            ? BookDeltaAction::Remove
            : action;

        auto now = std::chrono::time_point_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now());
        mutation.timestamp = static_cast<uint64_t>(now.time_since_epoch().count());

        bookMutationCallback_(mutation);
    }

}
