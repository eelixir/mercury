#include "RiskManager.h"
#include <iostream>
#include <sstream>
#include <cmath>
#include <algorithm>

namespace Mercury {

    // ==================== RiskManager ====================

    RiskManager::RiskManager() : defaultLimits_() {
    }

    RiskManager::RiskManager(const RiskLimits& limits) : defaultLimits_(limits) {
    }

    RiskEvent RiskManager::checkOrder(const Order& order) {
        RiskEvent event;
        event.eventId = generateEventId();
        event.timestamp = getTimestamp();
        event.orderId = order.id;
        event.clientId = order.clientId;

        // Skip risk checks for Cancel and Modify orders
        if (order.orderType == OrderType::Cancel || order.orderType == OrderType::Modify) {
            event.eventType = RiskEventType::Approved;
            event.details = "Cancel/Modify orders bypass risk checks";
            approvedCount_++;
            notifyRiskEvent(event);
            return event;
        }

        // Get client position and applicable limits
        const ClientPosition& position = getOrCreatePosition(order.clientId);
        const RiskLimits& limits = getLimits(order.clientId);

        // Check single order limits first (fastest checks)
        event = checkOrderLimits(order, limits);
        if (event.isRejected()) {
            event.eventId = generateEventId();
            event.timestamp = getTimestamp();
            event.orderId = order.id;
            event.clientId = order.clientId;
            rejectedCount_++;
            notifyRiskEvent(event);
            return event;
        }

        // Check open order limits
        event = checkOpenOrderLimits(order, position, limits);
        if (event.isRejected()) {
            event.eventId = generateEventId();
            event.timestamp = getTimestamp();
            event.orderId = order.id;
            event.clientId = order.clientId;
            rejectedCount_++;
            notifyRiskEvent(event);
            return event;
        }

        // Check position limits
        event = checkPositionLimits(order, position, limits);
        if (event.isRejected()) {
            event.eventId = generateEventId();
            event.timestamp = getTimestamp();
            event.orderId = order.id;
            event.clientId = order.clientId;
            rejectedCount_++;
            notifyRiskEvent(event);
            return event;
        }

        // Check exposure limits
        event = checkExposureLimits(order, position, limits);
        if (event.isRejected()) {
            event.eventId = generateEventId();
            event.timestamp = getTimestamp();
            event.orderId = order.id;
            event.clientId = order.clientId;
            rejectedCount_++;
            notifyRiskEvent(event);
            return event;
        }

        // All checks passed
        event.eventId = generateEventId();
        event.timestamp = getTimestamp();
        event.orderId = order.id;
        event.clientId = order.clientId;
        event.eventType = RiskEventType::Approved;
        event.details = "All risk checks passed";
        approvedCount_++;
        notifyRiskEvent(event);
        return event;
    }

    RiskEvent RiskManager::checkOrderLimits(const Order& order, const RiskLimits& limits) {
        RiskEvent event;
        event.eventType = RiskEventType::Approved;

        // Check order quantity limit
        if (order.quantity > limits.maxOrderQuantity) {
            event.eventType = RiskEventType::OrderQuantityLimitBreached;
            event.currentValue = 0;
            event.limitValue = static_cast<int64_t>(limits.maxOrderQuantity);
            event.requestedValue = static_cast<int64_t>(order.quantity);
            std::ostringstream oss;
            oss << "Order quantity " << order.quantity << " exceeds limit " << limits.maxOrderQuantity;
            event.details = oss.str();
            return event;
        }

        // Check order value limit (for limit orders)
        if (order.orderType == OrderType::Limit && order.price > 0) {
            int64_t orderValue = order.price * static_cast<int64_t>(order.quantity);
            if (orderValue > limits.maxOrderValue) {
                event.eventType = RiskEventType::OrderValueLimitBreached;
                event.currentValue = 0;
                event.limitValue = limits.maxOrderValue;
                event.requestedValue = orderValue;
                std::ostringstream oss;
                oss << "Order value " << orderValue << " exceeds limit " << limits.maxOrderValue;
                event.details = oss.str();
                return event;
            }
        }

        return event;
    }

    RiskEvent RiskManager::checkOpenOrderLimits(const Order& order, const ClientPosition& position,
                                                  const RiskLimits& limits) {
        RiskEvent event;
        event.eventType = RiskEventType::Approved;

        // Check max open orders limit
        if (position.openOrderCount >= limits.maxOpenOrders) {
            event.eventType = RiskEventType::MaxOpenOrdersExceeded;
            event.currentValue = static_cast<int64_t>(position.openOrderCount);
            event.limitValue = static_cast<int64_t>(limits.maxOpenOrders);
            event.requestedValue = 1;
            std::ostringstream oss;
            oss << "Open orders " << position.openOrderCount << " would exceed limit " << limits.maxOpenOrders;
            event.details = oss.str();
            return event;
        }

        return event;
    }

    RiskEvent RiskManager::checkPositionLimits(const Order& order, const ClientPosition& position,
                                                 const RiskLimits& limits) {
        RiskEvent event;
        event.eventType = RiskEventType::Approved;

        // Calculate potential new position after this order fills
        int64_t potentialNetPosition = position.netPosition();
        if (order.side == Side::Buy) {
            potentialNetPosition += static_cast<int64_t>(order.quantity);
        } else {
            potentialNetPosition -= static_cast<int64_t>(order.quantity);
        }

        // Check if potential position exceeds limit
        if (std::abs(potentialNetPosition) > limits.maxPositionQuantity) {
            event.eventType = RiskEventType::PositionLimitBreached;
            event.currentValue = position.netPosition();
            event.limitValue = limits.maxPositionQuantity;
            event.requestedValue = static_cast<int64_t>(order.quantity);
            std::ostringstream oss;
            oss << "Net position would be " << potentialNetPosition 
                << ", exceeding limit +/-" << limits.maxPositionQuantity;
            event.details = oss.str();
            return event;
        }

        return event;
    }

    RiskEvent RiskManager::checkExposureLimits(const Order& order, const ClientPosition& position,
                                                 const RiskLimits& limits) {
        RiskEvent event;
        event.eventType = RiskEventType::Approved;

        // For market orders, use a reasonable estimate (worst case scenario)
        // For limit orders, use the order price
        int64_t orderPrice = order.price;
        if (order.orderType == OrderType::Market) {
            // Use a conservative estimate - this should ideally be based on current market prices
            orderPrice = 10000;  // Default market price estimate
        }

        int64_t orderValue = orderPrice * static_cast<int64_t>(order.quantity);

        // Calculate current gross exposure (simplified: using position * avg price)
        // In a real system, this would use mark-to-market prices
        int64_t currentGrossExposure = 0;
        if (position.longPosition > 0 && position.avgBuyPrice > 0) {
            currentGrossExposure += position.longPosition * position.avgBuyPrice;
        }
        if (position.shortPosition > 0 && position.avgSellPrice > 0) {
            currentGrossExposure += position.shortPosition * position.avgSellPrice;
        }

        // Calculate potential gross exposure after order
        int64_t potentialGrossExposure = currentGrossExposure + orderValue;

        // Check gross exposure limit
        if (potentialGrossExposure > limits.maxGrossExposure) {
            event.eventType = RiskEventType::GrossExposureLimitBreached;
            event.currentValue = currentGrossExposure;
            event.limitValue = limits.maxGrossExposure;
            event.requestedValue = orderValue;
            std::ostringstream oss;
            oss << "Gross exposure would be " << potentialGrossExposure 
                << ", exceeding limit " << limits.maxGrossExposure;
            event.details = oss.str();
            return event;
        }

        // Calculate potential net exposure
        int64_t currentNetExposure = 0;
        if (position.longPosition > 0 && position.avgBuyPrice > 0) {
            currentNetExposure += position.longPosition * position.avgBuyPrice;
        }
        if (position.shortPosition > 0 && position.avgSellPrice > 0) {
            currentNetExposure -= position.shortPosition * position.avgSellPrice;
        }

        int64_t potentialNetExposure = currentNetExposure;
        if (order.side == Side::Buy) {
            potentialNetExposure += orderValue;
        } else {
            potentialNetExposure -= orderValue;
        }

        // Check net exposure limit
        if (std::abs(potentialNetExposure) > limits.maxNetExposure) {
            event.eventType = RiskEventType::NetExposureLimitBreached;
            event.currentValue = currentNetExposure;
            event.limitValue = limits.maxNetExposure;
            event.requestedValue = orderValue;
            std::ostringstream oss;
            oss << "Net exposure would be " << potentialNetExposure 
                << ", exceeding limit +/-" << limits.maxNetExposure;
            event.details = oss.str();
            return event;
        }

        // Check daily P&L limit
        if (position.realizedPnL < limits.maxDailyLoss) {
            event.eventType = RiskEventType::DailyLossLimitBreached;
            event.currentValue = position.realizedPnL;
            event.limitValue = limits.maxDailyLoss;
            event.requestedValue = 0;
            std::ostringstream oss;
            oss << "Daily realized loss " << position.realizedPnL 
                << " exceeds limit " << limits.maxDailyLoss;
            event.details = oss.str();
            return event;
        }

        return event;
    }

    void RiskManager::onTradeExecuted(const Trade& trade, uint64_t buyClientId, uint64_t sellClientId) {
        // Update buyer's position
        if (buyClientId != 0) {
            ClientPosition& buyerPos = getOrCreatePosition(buyClientId);
            
            // If buyer has short position, this trade closes some of it
            if (buyerPos.shortPosition >= static_cast<int64_t>(trade.quantity)) {
                // Fully or partially closing short position
                int64_t pnl = (buyerPos.avgSellPrice - trade.price) * static_cast<int64_t>(trade.quantity);
                buyerPos.realizedPnL += pnl;
                buyerPos.shortPosition -= static_cast<int64_t>(trade.quantity);
            } else if (buyerPos.shortPosition > 0) {
                // Partial close and new long
                int64_t closeQty = buyerPos.shortPosition;
                int64_t newLongQty = static_cast<int64_t>(trade.quantity) - closeQty;
                int64_t pnl = (buyerPos.avgSellPrice - trade.price) * closeQty;
                buyerPos.realizedPnL += pnl;
                buyerPos.shortPosition = 0;
                buyerPos.longPosition += newLongQty;
                // Update average buy price
                if (buyerPos.longPosition > 0) {
                    buyerPos.avgBuyPrice = ((buyerPos.avgBuyPrice * (buyerPos.longPosition - newLongQty)) + 
                                            (trade.price * newLongQty)) / buyerPos.longPosition;
                }
            } else {
                // Adding to long position
                int64_t oldLongValue = buyerPos.avgBuyPrice * buyerPos.longPosition;
                int64_t newValue = trade.price * static_cast<int64_t>(trade.quantity);
                buyerPos.longPosition += static_cast<int64_t>(trade.quantity);
                if (buyerPos.longPosition > 0) {
                    buyerPos.avgBuyPrice = (oldLongValue + newValue) / buyerPos.longPosition;
                }
            }
        }

        // Update seller's position
        if (sellClientId != 0) {
            ClientPosition& sellerPos = getOrCreatePosition(sellClientId);
            
            // If seller has long position, this trade closes some of it
            if (sellerPos.longPosition >= static_cast<int64_t>(trade.quantity)) {
                // Fully or partially closing long position
                int64_t pnl = (trade.price - sellerPos.avgBuyPrice) * static_cast<int64_t>(trade.quantity);
                sellerPos.realizedPnL += pnl;
                sellerPos.longPosition -= static_cast<int64_t>(trade.quantity);
            } else if (sellerPos.longPosition > 0) {
                // Partial close and new short
                int64_t closeQty = sellerPos.longPosition;
                int64_t newShortQty = static_cast<int64_t>(trade.quantity) - closeQty;
                int64_t pnl = (trade.price - sellerPos.avgBuyPrice) * closeQty;
                sellerPos.realizedPnL += pnl;
                sellerPos.longPosition = 0;
                sellerPos.shortPosition += newShortQty;
                // Update average sell price
                if (sellerPos.shortPosition > 0) {
                    sellerPos.avgSellPrice = ((sellerPos.avgSellPrice * (sellerPos.shortPosition - newShortQty)) + 
                                               (trade.price * newShortQty)) / sellerPos.shortPosition;
                }
            } else {
                // Adding to short position
                int64_t oldShortValue = sellerPos.avgSellPrice * sellerPos.shortPosition;
                int64_t newValue = trade.price * static_cast<int64_t>(trade.quantity);
                sellerPos.shortPosition += static_cast<int64_t>(trade.quantity);
                if (sellerPos.shortPosition > 0) {
                    sellerPos.avgSellPrice = (oldShortValue + newValue) / sellerPos.shortPosition;
                }
            }
        }
    }

    void RiskManager::onOrderAdded(const Order& order) {
        if (order.clientId == 0) return;
        ClientPosition& pos = getOrCreatePosition(order.clientId);
        pos.openOrderCount++;
        pos.dailyOrderCount++;
    }

    void RiskManager::onOrderRemoved(const Order& order) {
        if (order.clientId == 0) return;
        ClientPosition& pos = getOrCreatePosition(order.clientId);
        if (pos.openOrderCount > 0) {
            pos.openOrderCount--;
        }
    }

    void RiskManager::onOrderFilled(const Order& order, uint64_t filledQuantity) {
        // Position updates happen in onTradeExecuted
        // This is for tracking order state
        (void)order;
        (void)filledQuantity;
    }

    ClientPosition RiskManager::getClientPosition(uint64_t clientId) const {
        const ClientPosition* ptr = clientPositions_.find(clientId);
        if (ptr != nullptr) {
            return *ptr;
        }
        return ClientPosition();
    }

    ClientPosition& RiskManager::getOrCreatePosition(uint64_t clientId) {
        ClientPosition* ptr = clientPositions_.find(clientId);
        if (ptr != nullptr) {
            return *ptr;
        }
        // Insert default position
        clientPositions_.insert(clientId, ClientPosition());
        return *clientPositions_.find(clientId);
    }

    void RiskManager::setClientLimits(uint64_t clientId, const RiskLimits& limits) {
        clientLimits_.insert(clientId, limits);
    }

    const RiskLimits& RiskManager::getLimits(uint64_t clientId) const {
        if (clientId != 0) {
            const RiskLimits* ptr = clientLimits_.find(clientId);
            if (ptr != nullptr) {
                return *ptr;
            }
        }
        return defaultLimits_;
    }

    void RiskManager::setDefaultLimits(const RiskLimits& limits) {
        defaultLimits_ = limits;
    }

    void RiskManager::resetPositions() {
        clientPositions_.clear();
    }

    void RiskManager::resetDailyCounters() {
        for (auto it = clientPositions_.begin(); it != clientPositions_.end(); ++it) {
            it.value().dailyOrderCount = 0;
            it.value().realizedPnL = 0;
        }
    }

    void RiskManager::notifyRiskEvent(const RiskEvent& event) {
        if (riskCallback_) {
            riskCallback_(event);
        }
    }

    // ==================== RiskEventWriter ====================

    RiskEventWriter::RiskEventWriter(const std::string& filepath)
        : filepath_(filepath), eventsWritten_(0) {
    }

    RiskEventWriter::~RiskEventWriter() {
        close();
    }

    bool RiskEventWriter::open() {
        if (file_.is_open()) {
            return true;
        }

        file_.open(filepath_, std::ios::out | std::ios::trunc);
        if (!file_.is_open()) {
            std::cerr << "RiskEventWriter: Could not open file: " << filepath_ << "\n";
            return false;
        }

        writeHeader();
        return true;
    }

    void RiskEventWriter::close() {
        if (file_.is_open()) {
            file_.flush();
            file_.close();
        }
    }

    void RiskEventWriter::writeHeader() {
        file_ << "event_id,timestamp,order_id,client_id,event_type,current_value,limit_value,requested_value,details\n";
    }

    bool RiskEventWriter::writeEvent(const RiskEvent& event) {
        if (!file_.is_open()) {
            std::cerr << "RiskEventWriter: File not open\n";
            return false;
        }

        // Escape details string for CSV (replace commas and newlines)
        std::string safeDetails = event.details;
        for (char& c : safeDetails) {
            if (c == ',' || c == '\n' || c == '\r') {
                c = ' ';
            }
        }

        file_ << event.eventId << ","
              << event.timestamp << ","
              << event.orderId << ","
              << event.clientId << ","
              << riskEventTypeToString(event.eventType) << ","
              << event.currentValue << ","
              << event.limitValue << ","
              << event.requestedValue << ","
              << safeDetails << "\n";

        if (file_.fail()) {
            std::cerr << "RiskEventWriter: Write failed for event " << event.eventId << "\n";
            return false;
        }

        eventsWritten_++;
        return true;
    }

    void RiskEventWriter::flush() {
        if (file_.is_open()) {
            file_.flush();
        }
    }

}
