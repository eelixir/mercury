#include <iostream>
#include <iomanip>
#include "OrderBook.h" 
#include "Order.h"
#include "CSVParser.h"
#include "MatchingEngine.h"

// Helper function to convert ExecutionStatus to string
std::string statusToString(Mercury::ExecutionStatus status) {
    switch (status) {
        case Mercury::ExecutionStatus::Filled: return "FILLED";
        case Mercury::ExecutionStatus::PartialFill: return "PARTIAL_FILL";
        case Mercury::ExecutionStatus::Resting: return "RESTING";
        case Mercury::ExecutionStatus::Cancelled: return "CANCELLED";
        case Mercury::ExecutionStatus::Modified: return "MODIFIED";
        case Mercury::ExecutionStatus::Rejected: return "REJECTED";
        default: return "UNKNOWN";
    }
}

// Helper function to print execution result
void printResult(const Mercury::ExecutionResult& result) {
    std::cout << "  Status: " << statusToString(result.status) 
              << " | OrderID: " << result.orderId
              << " | Filled: " << result.filledQuantity
              << " | Remaining: " << result.remainingQuantity
              << " | Trades: " << result.trades.size();
    if (!result.message.empty()) {
        std::cout << "\n  Message: " << result.message;
    }
    std::cout << "\n";
}

// Helper to create orders easily
Mercury::Order createOrder(uint64_t id, Mercury::OrderType type, Mercury::Side side, 
                           int64_t price, uint64_t quantity) {
    Mercury::Order order;
    order.id = id;
    order.orderType = type;
    order.side = side;
    order.price = price;
    order.quantity = quantity;
    order.timestamp = 0;  // Will be assigned by engine
    return order;
}

void runDemo() {
    std::cout << "\n========================================\n";
    std::cout << "   Mercury Matching Engine Demo\n";
    std::cout << "========================================\n\n";

    Mercury::MatchingEngine engine;

    // Set up trade callback
    engine.setTradeCallback([](const Mercury::Trade& trade) {
        std::cout << "  >> TRADE: ID=" << trade.tradeId 
                  << " Price=" << trade.price 
                  << " Qty=" << trade.quantity
                  << " (Buy #" << trade.buyOrderId 
                  << " <-> Sell #" << trade.sellOrderId << ")\n";
    });

    // ======== DEMO 1: Limit Orders with No Match ========
    std::cout << "--- Step 1: Add Limit Orders (No Match) ---\n";
    
    std::cout << "Adding Buy Limit @100 for 50 units (Order #1)\n";
    auto result = engine.submitOrder(createOrder(1, Mercury::OrderType::Limit, 
                                                  Mercury::Side::Buy, 100, 50));
    printResult(result);

    std::cout << "Adding Buy Limit @98 for 30 units (Order #2)\n";
    result = engine.submitOrder(createOrder(2, Mercury::OrderType::Limit, 
                                             Mercury::Side::Buy, 98, 30));
    printResult(result);

    std::cout << "Adding Sell Limit @105 for 40 units (Order #3)\n";
    result = engine.submitOrder(createOrder(3, Mercury::OrderType::Limit, 
                                             Mercury::Side::Sell, 105, 40));
    printResult(result);

    std::cout << "Adding Sell Limit @110 for 25 units (Order #4)\n";
    result = engine.submitOrder(createOrder(4, Mercury::OrderType::Limit, 
                                             Mercury::Side::Sell, 110, 25));
    printResult(result);

    std::cout << "\nOrder Book State:\n";
    engine.getOrderBook().printBook();

    // ======== DEMO 2: Limit Order with Match ========
    std::cout << "--- Step 2: Crossing Limit Order (Partial Fill) ---\n";
    std::cout << "Adding Buy Limit @107 for 60 units (Order #5)\n";
    std::cout << "This should match against Sell @105 (40 units) and rest 20 @107\n";
    result = engine.submitOrder(createOrder(5, Mercury::OrderType::Limit, 
                                             Mercury::Side::Buy, 107, 60));
    printResult(result);

    std::cout << "\nOrder Book State:\n";
    engine.getOrderBook().printBook();

    // ======== DEMO 3: Market Order ========
    std::cout << "--- Step 3: Market Order ---\n";
    std::cout << "Sending Sell Market Order for 70 units (Order #6)\n";
    std::cout << "This should sweep bids: 20@107, then 50@100\n";
    result = engine.submitOrder(createOrder(6, Mercury::OrderType::Market, 
                                             Mercury::Side::Sell, 0, 70));
    printResult(result);

    std::cout << "\nOrder Book State:\n";
    engine.getOrderBook().printBook();

    // ======== DEMO 4: Cancel Order ========
    std::cout << "--- Step 4: Cancel Order ---\n";
    std::cout << "Cancelling Order #4 (Sell @110 for 25)\n";
    result = engine.cancelOrder(4);
    printResult(result);

    std::cout << "\nOrder Book State:\n";
    engine.getOrderBook().printBook();

    // ======== DEMO 5: Modify Order ========
    std::cout << "--- Step 5: Modify Order ---\n";
    std::cout << "First, add a new order to modify\n";
    std::cout << "Adding Buy Limit @95 for 100 units (Order #7)\n";
    result = engine.submitOrder(createOrder(7, Mercury::OrderType::Limit, 
                                             Mercury::Side::Buy, 95, 100));
    printResult(result);

    std::cout << "\nModifying Order #7: Price 95->99, Quantity 100->75\n";
    result = engine.modifyOrder(7, 99, 75);
    printResult(result);

    std::cout << "\nOrder Book State:\n";
    engine.getOrderBook().printBook();

    // ======== DEMO 6: IOC Order (Immediate-or-Cancel) ========
    std::cout << "--- Step 6: IOC (Immediate-or-Cancel) Order ---\n";
    std::cout << "Adding Sell IOC @90 for 100 units (Order #8)\n";
    Mercury::Order iocOrder = createOrder(8, Mercury::OrderType::Limit, 
                                           Mercury::Side::Sell, 90, 100);
    iocOrder.tif = Mercury::TimeInForce::IOC;
    result = engine.submitOrder(iocOrder);
    printResult(result);

    std::cout << "\nOrder Book State:\n";
    engine.getOrderBook().printBook();

    // ======== Statistics ========
    std::cout << "========================================\n";
    std::cout << "           Trading Statistics\n";
    std::cout << "========================================\n";
    std::cout << "Total Trades: " << engine.getTradeCount() << "\n";
    std::cout << "Total Volume: " << engine.getTotalVolume() << " units\n";
    std::cout << "Orders in Book: " << engine.getOrderBook().getOrderCount() << "\n";
    std::cout << "Bid Levels: " << engine.getOrderBook().getBidLevelCount() << "\n";
    std::cout << "Ask Levels: " << engine.getOrderBook().getAskLevelCount() << "\n";
}

int main(int argc, char* argv[]) {
    std::cout << "Initializing Mercury Trading Engine...\n";

    // Create the matching engine
    Mercury::MatchingEngine engine;

    // Check if a CSV file was provided as argument
    if (argc > 1) {
        std::cout << "\n--- Loading orders from CSV file ---\n";
        Mercury::CSVParser parser;
        auto orders = parser.parseFile(argv[1]);

        std::cout << "Parsed " << orders.size() << " orders from file\n";
        std::cout << "Lines processed: " << parser.getLinesProcessed() << "\n";
        std::cout << "Parse errors: " << parser.getParseErrorCount() << "\n\n";

        // Set up trade callback
        engine.setTradeCallback([](const Mercury::Trade& trade) {
            std::cout << "TRADE: ID=" << trade.tradeId 
                      << " Price=" << trade.price 
                      << " Qty=" << trade.quantity << "\n";
        });

        // Process all orders through the matching engine
        for (const auto& order : orders) {
            auto result = engine.submitOrder(order);
            std::cout << "Order #" << order.id << ": " 
                      << statusToString(result.status) << "\n";
        }

        std::cout << "\nFinal Order Book State:\n";
        engine.getOrderBook().printBook();

        std::cout << "\nStatistics:\n";
        std::cout << "Total Trades: " << engine.getTradeCount() << "\n";
        std::cout << "Total Volume: " << engine.getTotalVolume() << "\n";
    } else {
        // Run interactive demo
        std::cout << "\nUsage: mercury <orders.csv>\n";
        runDemo();
    }

    return 0;
}
