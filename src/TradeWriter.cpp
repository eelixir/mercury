#include "TradeWriter.h"
#include <iostream>
#include <iomanip>

namespace Mercury {

    // ==================== TradeWriter ====================

    TradeWriter::TradeWriter(const std::string& filepath)
        : filepath(filepath), tradesWritten(0) {
    }

    TradeWriter::~TradeWriter() {
        close();
    }

    TradeWriter::TradeWriter(TradeWriter&& other) noexcept
        : filepath(std::move(other.filepath)),
          file(std::move(other.file)),
          tradesWritten(other.tradesWritten) {
        other.tradesWritten = 0;
    }

    TradeWriter& TradeWriter::operator=(TradeWriter&& other) noexcept {
        if (this != &other) {
            close();
            filepath = std::move(other.filepath);
            file = std::move(other.file);
            tradesWritten = other.tradesWritten;
            other.tradesWritten = 0;
        }
        return *this;
    }

    bool TradeWriter::open() {
        if (file.is_open()) {
            return true;
        }

        file.open(filepath, std::ios::out | std::ios::trunc);
        if (!file.is_open()) {
            std::cerr << "TradeWriter: Could not open file: " << filepath << "\n";
            return false;
        }

        writeHeader();
        return true;
    }

    void TradeWriter::close() {
        if (file.is_open()) {
            file.flush();
            file.close();
        }
    }

    void TradeWriter::writeHeader() {
        file << "trade_id,timestamp,buy_order_id,sell_order_id,price,quantity\n";
    }

    bool TradeWriter::writeTrade(const Trade& trade) {
        if (!file.is_open()) {
            std::cerr << "TradeWriter: File not open\n";
            return false;
        }

        file << trade.tradeId << ","
             << trade.timestamp << ","
             << trade.buyOrderId << ","
             << trade.sellOrderId << ","
             << trade.price << ","
             << trade.quantity << "\n";

        if (file.fail()) {
            std::cerr << "TradeWriter: Write failed for trade " << trade.tradeId << "\n";
            return false;
        }

        tradesWritten++;
        return true;
    }

    size_t TradeWriter::writeTrades(const std::vector<Trade>& trades) {
        size_t written = 0;
        for (const auto& trade : trades) {
            if (writeTrade(trade)) {
                written++;
            }
        }
        return written;
    }

    void TradeWriter::flush() {
        if (file.is_open()) {
            file.flush();
        }
    }

    // ==================== ExecutionReportWriter ====================

    ExecutionReportWriter::ExecutionReportWriter(const std::string& filepath)
        : filepath(filepath), reportsWritten(0) {
    }

    ExecutionReportWriter::~ExecutionReportWriter() {
        close();
    }

    bool ExecutionReportWriter::open() {
        if (file.is_open()) {
            return true;
        }

        file.open(filepath, std::ios::out | std::ios::trunc);
        if (!file.is_open()) {
            std::cerr << "ExecutionReportWriter: Could not open file: " << filepath << "\n";
            return false;
        }

        writeHeader();
        return true;
    }

    void ExecutionReportWriter::close() {
        if (file.is_open()) {
            file.flush();
            file.close();
        }
    }

    void ExecutionReportWriter::writeHeader() {
        file << "order_id,timestamp,type,side,status,reject_reason,filled_qty,remaining_qty,trade_count,avg_price\n";
    }

    bool ExecutionReportWriter::writeReport(const Order& order, const ExecutionResult& result) {
        if (!file.is_open()) {
            std::cerr << "ExecutionReportWriter: File not open\n";
            return false;
        }

        // Calculate average fill price
        double avgPrice = 0.0;
        if (result.filledQuantity > 0 && !result.trades.empty()) {
            int64_t totalValue = 0;
            for (const auto& trade : result.trades) {
                totalValue += trade.price * static_cast<int64_t>(trade.quantity);
            }
            avgPrice = static_cast<double>(totalValue) / static_cast<double>(result.filledQuantity);
        }

        file << order.id << ","
             << order.timestamp << ","
             << orderTypeToString(order.orderType) << ","
             << sideToString(order.side) << ","
             << statusToString(result.status) << ","
             << rejectReasonToString(result.rejectReason) << ","
             << result.filledQuantity << ","
             << result.remainingQuantity << ","
             << result.trades.size() << ","
             << std::fixed << std::setprecision(2) << avgPrice << "\n";

        if (file.fail()) {
            std::cerr << "ExecutionReportWriter: Write failed for order " << order.id << "\n";
            return false;
        }

        reportsWritten++;
        return true;
    }

    void ExecutionReportWriter::flush() {
        if (file.is_open()) {
            file.flush();
        }
    }

    const char* ExecutionReportWriter::orderTypeToString(OrderType type) {
        switch (type) {
            case OrderType::Market: return "market";
            case OrderType::Limit: return "limit";
            case OrderType::Cancel: return "cancel";
            case OrderType::Modify: return "modify";
            default: return "unknown";
        }
    }

    const char* ExecutionReportWriter::sideToString(Side side) {
        switch (side) {
            case Side::Buy: return "buy";
            case Side::Sell: return "sell";
            default: return "unknown";
        }
    }

    const char* ExecutionReportWriter::statusToString(ExecutionStatus status) {
        switch (status) {
            case ExecutionStatus::Filled: return "filled";
            case ExecutionStatus::PartialFill: return "partial_fill";
            case ExecutionStatus::Resting: return "resting";
            case ExecutionStatus::Cancelled: return "cancelled";
            case ExecutionStatus::Modified: return "modified";
            case ExecutionStatus::Rejected: return "rejected";
            default: return "unknown";
        }
    }

}
