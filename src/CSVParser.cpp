#include "CSVParser.h"
#include <sstream>
#include <algorithm>
#include <cctype>
#include <iostream>
#include <future>

namespace Mercury {

    std::vector<Order> CSVParser::parseFile(const std::string& filepath) {
        std::vector<Order> orders;
        parseErrors_ = 0;
        linesProcessed_ = 0;

        std::ifstream file(filepath);
        if (!file.is_open()) {
            std::cerr << "CSVParser: Could not open file: " << filepath << "\n";
            return orders;
        }

        // Check file size for parallel parsing
        file.seekg(0, std::ios::end);
        size_t fileSize = static_cast<size_t>(file.tellg());
        file.seekg(0, std::ios::beg);
        
        if (fileSize >= parallelThreshold_) {
            file.close();
            return parseFileParallel(filepath);
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

            ++linesProcessed_;

            auto order = parseLine(line);
            if (order.has_value()) {
                if (order->isValid()) {
                    orders.push_back(order.value());
                } else {
                    ++parseErrors_;
                    std::cerr << "CSVParser: Invalid order data at line " 
                              << linesProcessed_.load() << "\n";
                }
            } else {
                ++parseErrors_;
            }
        }

        file.close();
        return orders;
    }

    std::vector<Order> CSVParser::parseFileParallel(const std::string& filepath, size_t numThreads) {
        parseErrors_ = 0;
        linesProcessed_ = 0;

        // Read entire file into memory
        std::string content = readFileContent(filepath);
        if (content.empty()) {
            return {};
        }

        // Determine number of threads
        if (numThreads == 0) {
            numThreads = std::thread::hardware_concurrency();
            if (numThreads == 0) numThreads = 4;
        }

        // Find and skip header line
        size_t headerEnd = content.find('\n');
        if (headerEnd == std::string::npos) {
            return {};
        }
        size_t dataStart = headerEnd + 1;

        // Split into chunks
        auto chunks = splitIntoChunks(content, numThreads);
        
        // Adjust first chunk to skip header
        if (!chunks.empty() && chunks[0].first < dataStart) {
            chunks[0].first = dataStart;
        }

        // Parse chunks in parallel
        std::vector<std::future<std::vector<Order>>> futures;
        futures.reserve(chunks.size());

        for (const auto& chunk : chunks) {
            futures.push_back(std::async(std::launch::async, [this, &content, chunk]() {
                return parseChunk(content, chunk.first, chunk.second);
            }));
        }

        // Collect results
        std::vector<Order> orders;
        for (auto& future : futures) {
            auto chunkOrders = future.get();
            orders.insert(orders.end(), 
                         std::make_move_iterator(chunkOrders.begin()),
                         std::make_move_iterator(chunkOrders.end()));
        }

        return orders;
    }

    std::string CSVParser::readFileContent(const std::string& filepath) {
        std::ifstream file(filepath, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            std::cerr << "CSVParser: Could not open file: " << filepath << "\n";
            return {};
        }

        size_t fileSize = static_cast<size_t>(file.tellg());
        file.seekg(0, std::ios::beg);

        std::string content(fileSize, '\0');
        file.read(&content[0], static_cast<std::streamsize>(fileSize));
        
        return content;
    }

    std::vector<std::pair<size_t, size_t>> CSVParser::splitIntoChunks(
            const std::string& content, size_t numChunks) {
        std::vector<std::pair<size_t, size_t>> chunks;
        
        if (content.empty() || numChunks == 0) {
            return chunks;
        }

        size_t contentSize = content.size();
        size_t chunkSize = contentSize / numChunks;
        
        if (chunkSize < 1024) {
            // File too small, use single chunk
            chunks.emplace_back(0, contentSize);
            return chunks;
        }

        size_t start = 0;
        for (size_t i = 0; i < numChunks && start < contentSize; ++i) {
            size_t end = (i == numChunks - 1) ? contentSize : start + chunkSize;
            
            // Adjust end to line boundary
            if (end < contentSize) {
                size_t lineEnd = content.find('\n', end);
                if (lineEnd != std::string::npos) {
                    end = lineEnd + 1;
                } else {
                    end = contentSize;
                }
            }

            if (start < end) {
                chunks.emplace_back(start, end);
            }
            start = end;
        }

        return chunks;
    }

    std::vector<Order> CSVParser::parseChunk(const std::string& content, 
                                             size_t start, size_t end) {
        std::vector<Order> orders;
        orders.reserve((end - start) / 50);  // Rough estimate of orders per chunk

        size_t pos = start;
        std::string line;

        while (pos < end) {
            size_t lineEnd = content.find('\n', pos);
            if (lineEnd == std::string::npos || lineEnd >= end) {
                lineEnd = end;
            }

            line = content.substr(pos, lineEnd - pos);
            
            // Remove \r if present
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }

            if (!line.empty() && !trim(line).empty()) {
                ++linesProcessed_;

                auto order = parseLineSafe(line);
                if (order.has_value() && order->isValid()) {
                    orders.push_back(order.value());
                } else {
                    ++parseErrors_;
                }
            }

            pos = lineEnd + 1;
        }

        return orders;
    }

    std::optional<Order> CSVParser::parseLine(const std::string& line) {
        auto result = parseLineSafe(line);
        if (!result.has_value()) {
            std::cerr << "CSVParser: Parse error in line: " << line << "\n";
        }
        return result;
    }

    std::optional<Order> CSVParser::parseLineSafe(const std::string& line) {
        auto fields = split(line, ',');

        // Expected: id, timestamp, type, side, price, quantity
        if (fields.size() < 6) {
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
                return std::nullopt;
            }
            order.orderType = orderType.value();

            // Parse side
            auto side = parseSide(trim(fields[3]));
            if (!side.has_value()) {
                return std::nullopt;
            }
            order.side = side.value();

            // Parse price
            order.price = std::stoll(trim(fields[4]));

            // Parse quantity
            order.quantity = std::stoull(trim(fields[5]));

            // Parse optional clientId (7th field)
            if (fields.size() >= 7) {
                std::string clientIdStr = trim(fields[6]);
                if (!clientIdStr.empty()) {
                    order.clientId = std::stoull(clientIdStr);
                }
            }

        } catch (const std::exception&) {
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
