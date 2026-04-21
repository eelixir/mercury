#pragma once

// Fixed-width binary wire format for high-performance market data streaming.
//
// These structs are designed to be memcpy'd directly into a WebSocket frame
// on /ws/market/bin.  All fields are little-endian (assumes x86/x64 host).
//
// Clients read `header.type` to determine the message kind, then interpret
// the remaining bytes according to the corresponding struct layout.

#include "MarketData.h"
#include <cstdint>
#include <cstring>
#include <string>

namespace Mercury {

#pragma pack(push, 1)

    struct BinaryHeader {
        uint8_t  type;       // 1 = book_delta, 2 = trade
        uint8_t  reserved;
        uint16_t length;     // total message length including header
    };

    static constexpr uint8_t BINARY_TYPE_BOOK_DELTA = 1;
    static constexpr uint8_t BINARY_TYPE_TRADE      = 2;

    struct BinaryBookDelta {
        BinaryHeader header;
        uint64_t sequence;
        uint8_t  side;            // 0 = buy, 1 = sell
        int64_t  price;
        uint64_t quantity;
        uint32_t orderCount;
        uint8_t  action;          // 0 = upsert, 1 = remove
        uint64_t timestamp;
        uint64_t engineLatencyNs;
    };

    struct BinaryTradeEvent {
        BinaryHeader header;
        uint64_t sequence;
        uint64_t tradeId;
        int64_t  price;
        uint64_t quantity;
        uint64_t buyOrderId;
        uint64_t sellOrderId;
        uint64_t buyClientId;
        uint64_t sellClientId;
        uint64_t timestamp;
        uint64_t engineLatencyNs;
    };

#pragma pack(pop)

    // Serialize a BookDelta into a binary message buffer.
    inline std::string serializeBinary(const BookDelta& delta) {
        BinaryBookDelta msg{};
        msg.header.type = BINARY_TYPE_BOOK_DELTA;
        msg.header.reserved = 0;
        msg.header.length = static_cast<uint16_t>(sizeof(BinaryBookDelta));
        msg.sequence = delta.sequence;
        msg.side = delta.side == Side::Buy ? 0 : 1;
        msg.price = delta.price;
        msg.quantity = delta.quantity;
        msg.orderCount = static_cast<uint32_t>(delta.orderCount);
        msg.action = delta.action == BookDeltaAction::Upsert ? 0 : 1;
        msg.timestamp = delta.timestamp;
        msg.engineLatencyNs = delta.engineLatencyNs;

        return std::string(reinterpret_cast<const char*>(&msg), sizeof(msg));
    }

    // Serialize a TradeEvent into a binary message buffer.
    inline std::string serializeBinary(const TradeEvent& trade) {
        BinaryTradeEvent msg{};
        msg.header.type = BINARY_TYPE_TRADE;
        msg.header.reserved = 0;
        msg.header.length = static_cast<uint16_t>(sizeof(BinaryTradeEvent));
        msg.sequence = trade.sequence;
        msg.tradeId = trade.tradeId;
        msg.price = trade.price;
        msg.quantity = trade.quantity;
        msg.buyOrderId = trade.buyOrderId;
        msg.sellOrderId = trade.sellOrderId;
        msg.buyClientId = trade.buyClientId;
        msg.sellClientId = trade.sellClientId;
        msg.timestamp = trade.timestamp;
        msg.engineLatencyNs = trade.engineLatencyNs;

        return std::string(reinterpret_cast<const char*>(&msg), sizeof(msg));
    }

}
