#pragma once

#include "Order.h"
#include <string>
#include <vector>
#include <fstream>
#include <chrono>

namespace Mercury {

    /**
     * TradeWriter - Writes trade execution results to CSV files
     * 
     * Output CSV format (with header):
     * trade_id,timestamp,buy_order_id,sell_order_id,price,quantity
     */
    class TradeWriter {
    public:
        explicit TradeWriter(const std::string& filepath);
        ~TradeWriter();

        // Prevent copying
        TradeWriter(const TradeWriter&) = delete;
        TradeWriter& operator=(const TradeWriter&) = delete;

        // Allow moving
        TradeWriter(TradeWriter&& other) noexcept;
        TradeWriter& operator=(TradeWriter&& other) noexcept;

        /**
         * Open the file for writing. Writes header if file is new.
         * @return true if file was opened successfully
         */
        bool open();

        /**
         * Close the file
         */
        void close();

        /**
         * Check if file is open and ready for writing
         */
        bool isOpen() const { return file.is_open(); }

        /**
         * Write a single trade to the file
         * @param trade The trade to write
         * @return true if write was successful
         */
        bool writeTrade(const Trade& trade);

        /**
         * Write multiple trades to the file
         * @param trades Vector of trades to write
         * @return Number of trades successfully written
         */
        size_t writeTrades(const std::vector<Trade>& trades);

        /**
         * Flush buffered writes to disk
         */
        void flush();

        /**
         * Get the number of trades written
         */
        size_t getTradeCount() const { return tradesWritten; }

        /**
         * Get the file path
         */
        const std::string& getFilePath() const { return filepath; }

    private:
        std::string filepath;
        std::ofstream file;
        size_t tradesWritten = 0;

        /**
         * Write the CSV header row
         */
        void writeHeader();
    };

    /**
     * ExecutionReportWriter - Writes order execution reports to CSV
     * 
     * Output CSV format (with header):
     * order_id,timestamp,type,side,status,reject_reason,filled_qty,remaining_qty,trade_count,avg_price
     */
    class ExecutionReportWriter {
    public:
        explicit ExecutionReportWriter(const std::string& filepath);
        ~ExecutionReportWriter();

        // Prevent copying
        ExecutionReportWriter(const ExecutionReportWriter&) = delete;
        ExecutionReportWriter& operator=(const ExecutionReportWriter&) = delete;

        /**
         * Open the file for writing
         */
        bool open();

        /**
         * Close the file
         */
        void close();

        /**
         * Check if file is open
         */
        bool isOpen() const { return file.is_open(); }

        /**
         * Write an execution report
         * @param order The original order
         * @param result The execution result
         * @return true if write was successful
         */
        bool writeReport(const Order& order, const ExecutionResult& result);

        /**
         * Flush buffered writes
         */
        void flush();

        /**
         * Get count of reports written
         */
        size_t getReportCount() const { return reportsWritten; }

    private:
        std::string filepath;
        std::ofstream file;
        size_t reportsWritten = 0;

        void writeHeader();
        
        static const char* orderTypeToString(OrderType type);
        static const char* sideToString(Side side);
        static const char* statusToString(ExecutionStatus status);
    };

}
