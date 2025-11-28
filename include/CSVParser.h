#pragma once

#include "Order.h"
#include <string>
#include <vector>
#include <fstream>
#include <optional>

namespace Mercury {

    /**
     * CSVParser - Parses CSV files containing market order data
     * 
     * Expected CSV format (with header):
     * id,timestamp,type,side,price,quantity
     * 
     * type: "market", "limit", "cancel", "modify"
     * side: "buy", "sell"
     * price: integer (in cents/smallest unit)
     * quantity: positive integer
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
         * Parse a single CSV line into an Order
         * @param line The CSV line to parse
         * @return Optional containing the Order if parsing succeeded, empty otherwise
         */
        std::optional<Order> parseLine(const std::string& line);

        /**
         * Get the number of lines that failed to parse
         */
        size_t getParseErrorCount() const { return parseErrors; }

        /**
         * Get the total number of lines processed (excluding header)
         */
        size_t getLinesProcessed() const { return linesProcessed; }

    private:
        size_t parseErrors = 0;
        size_t linesProcessed = 0;

        /**
         * Split a string by delimiter
         */
        std::vector<std::string> split(const std::string& str, char delimiter);

        /**
         * Trim whitespace from both ends of a string
         */
        std::string trim(const std::string& str);

        /**
         * Convert string to OrderType
         */
        std::optional<OrderType> parseOrderType(const std::string& str);

        /**
         * Convert string to Side
         */
        std::optional<Side> parseSide(const std::string& str);
    };

}
