#include "CSVParser.h"
#include <sstream>
#include <algorithm>
#include <cctype>
#include <iostream>

namespace Mercury {

    std::vector<Order> CSVParser::parseFile(const std::string& filepath) {
        std::vector<Order> orders;
        parseErrors = 0;
        linesProcessed = 0;

        std::ifstream file(filepath);
        if (!file.is_open()) {
            std::cerr << "CSVParser: Could not open file: " << filepath << "\n";
            return orders;
        }

        std::string line;
        bool isFirstLine = true;

        while (std::getline(file, line)) {
            // Skip header row
            if (isFirstLine) {
                isFirstLine = false;
                continue;
            }

            // Skip empty lines
            if (trim(line).empty()) {
                continue;
            }

            linesProcessed++;

            auto order = parseLine(line);
            if (order.has_value()) {
                if (order->isValid()) {
                    orders.push_back(order.value());
                } else {
                    parseErrors++;
                    std::cerr << "CSVParser: Invalid order data at line " 
                              << linesProcessed << "\n";
                }
            } else {
                parseErrors++;
            }
        }

        file.close();
        return orders;
    }

    std::optional<Order> CSVParser::parseLine(const std::string& line) {
        auto fields = split(line, ',');

        // Expected: id, timestamp, type, side, price, quantity
        if (fields.size() < 6) {
            std::cerr << "CSVParser: Insufficient fields in line: " << line << "\n";
            return std::nullopt;
        }

        Order order;

        try {
            // Parse ID
            order.id = std::stoull(trim(fields[0]));

            // Parse timestamp
            order.timestamp = std::stoull(trim(fields[1]));

            // Parse order type
            auto orderType = parseOrderType(trim(fields[2]));
            if (!orderType.has_value()) {
                std::cerr << "CSVParser: Invalid order type: " << fields[2] << "\n";
                return std::nullopt;
            }
            order.orderType = orderType.value();

            // Parse side
            auto side = parseSide(trim(fields[3]));
            if (!side.has_value()) {
                std::cerr << "CSVParser: Invalid side: " << fields[3] << "\n";
                return std::nullopt;
            }
            order.side = side.value();

            // Parse price
            order.price = std::stoll(trim(fields[4]));

            // Parse quantity
            order.quantity = std::stoull(trim(fields[5]));

        } catch (const std::exception& e) {
            std::cerr << "CSVParser: Parse error in line '" << line 
                      << "': " << e.what() << "\n";
            return std::nullopt;
        }

        return order;
    }

    std::vector<std::string> CSVParser::split(const std::string& str, char delimiter) {
        std::vector<std::string> tokens;
        std::stringstream ss(str);
        std::string token;

        while (std::getline(ss, token, delimiter)) {
            tokens.push_back(token);
        }

        return tokens;
    }

    std::string CSVParser::trim(const std::string& str) {
        auto start = std::find_if_not(str.begin(), str.end(), 
            [](unsigned char c) { return std::isspace(c); });
        auto end = std::find_if_not(str.rbegin(), str.rend(), 
            [](unsigned char c) { return std::isspace(c); }).base();

        return (start < end) ? std::string(start, end) : std::string();
    }

    std::optional<OrderType> CSVParser::parseOrderType(const std::string& str) {
        // Convert to lowercase for case-insensitive comparison
        std::string lower = str;
        std::transform(lower.begin(), lower.end(), lower.begin(), 
            [](unsigned char c) { return std::tolower(c); });

        if (lower == "market") return OrderType::Market;
        if (lower == "limit") return OrderType::Limit;
        if (lower == "cancel") return OrderType::Cancel;
        if (lower == "modify") return OrderType::Modify;

        return std::nullopt;
    }

    std::optional<Side> CSVParser::parseSide(const std::string& str) {
        // Convert to lowercase for case-insensitive comparison
        std::string lower = str;
        std::transform(lower.begin(), lower.end(), lower.begin(), 
            [](unsigned char c) { return std::tolower(c); });

        if (lower == "buy" || lower == "b") return Side::Buy;
        if (lower == "sell" || lower == "s") return Side::Sell;

        return std::nullopt;
    }

}
