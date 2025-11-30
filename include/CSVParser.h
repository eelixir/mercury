#pragma once

#include "Order.h"
#include "ThreadPool.h"
#include <string>
#include <vector>
#include <fstream>
#include <optional>
#include <atomic>
#include <mutex>

namespace Mercury {

    /**
     * CSVParser - Parses CSV files containing market order data
     * 
     * Expected CSV format (with header):
     * id,timestamp,type,side,price,quantity[,client_id]
     * 
     * type: "market", "limit", "cancel", "modify"
     * side: "buy", "sell"
     * price: integer (in cents/smallest unit)
     * quantity: positive integer
     * client_id: optional, positive integer for P&L tracking
     * 
     * Supports parallel parsing for large files.
     */
    class CSVParser {
    public:
        CSVParser() = default;

        /**
         * Parse a CSV file and return a vector of orders
         * @param filepath Path to the CSV file
         * @return Vector of parsed orders (invalid orders are skipped)
         */
        std::vector<Order> parseFile(const std::string& filepath);

        /**
         * Parse a CSV file using parallel processing
         * @param filepath Path to the CSV file
         * @param numThreads Number of threads to use (0 = auto)
         * @return Vector of parsed orders (invalid orders are skipped)
         */
        std::vector<Order> parseFileParallel(const std::string& filepath, size_t numThreads = 0);

        /**
         * Parse a single CSV line into an Order
         * @param line The CSV line to parse
         * @return Optional containing the Order if parsing succeeded, empty otherwise
         */
        std::optional<Order> parseLine(const std::string& line);

        /**
         * Parse a single line without error logging (for parallel use)
         * Thread-safe version that doesn't modify instance state
         */
        static std::optional<Order> parseLineSafe(const std::string& line);

        /**
         * Get the number of lines that failed to parse
         */
        size_t getParseErrorCount() const { return parseErrors_.load(); }

        /**
         * Get the total number of lines processed (excluding header)
         */
        size_t getLinesProcessed() const { return linesProcessed_.load(); }

        /**
         * Set minimum file size for parallel parsing (bytes)
         */
        void setParallelThreshold(size_t bytes) { parallelThreshold_ = bytes; }

    private:
        std::atomic<size_t> parseErrors_{0};
        std::atomic<size_t> linesProcessed_{0};
        size_t parallelThreshold_ = 1024 * 1024;  // 1MB default

        /**
         * Split a string by delimiter
         */
        static std::vector<std::string> split(const std::string& str, char delimiter);

        /**
         * Trim whitespace from both ends of a string
         */
        static std::string trim(const std::string& str);

        /**
         * Convert string to OrderType
         */
        static std::optional<OrderType> parseOrderType(const std::string& str);

        /**
         * Convert string to Side
         */
        static std::optional<Side> parseSide(const std::string& str);

        /**
         * Read entire file into memory for parallel parsing
         */
        static std::string readFileContent(const std::string& filepath);

        /**
         * Split file content into line chunks for parallel processing
         */
        static std::vector<std::pair<size_t, size_t>> splitIntoChunks(
            const std::string& content, size_t numChunks);

        /**
         * Parse a chunk of lines
         */
        std::vector<Order> parseChunk(const std::string& content, 
                                      size_t start, size_t end);
    };

}
