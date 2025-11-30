#include <iostream>
#include <iomanip>
#include <chrono>
#include "OrderBook.h" 
#include "Order.h"
#include "CSVParser.h"
#include "MatchingEngine.h"
#include "TradeWriter.h"
#include "RiskManager.h"
#include "PnLTracker.h"

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
        std::string inputFile = argv[1];
        
        // Determine output file names
        std::string tradesFile = (argc > 2) ? argv[2] : "trades.csv";
        std::string reportsFile = (argc > 3) ? argv[3] : "executions.csv";
        std::string riskEventsFile = (argc > 4) ? argv[4] : "riskevents.csv";
        std::string pnlFile = (argc > 5) ? argv[5] : "pnl.csv";

        std::cout << "\n========================================\n";
        std::cout << "   Mercury File I/O Mode\n";
        std::cout << "========================================\n";
        std::cout << "Input:       " << inputFile << "\n";
        std::cout << "Trades:      " << tradesFile << "\n";
        std::cout << "Executions:  " << reportsFile << "\n";
        std::cout << "Risk Events: " << riskEventsFile << "\n";
        std::cout << "P&L:         " << pnlFile << "\n";
        std::cout << "========================================\n\n";

        // Parse input orders
        std::cout << "--- Parsing Orders ---\n";
        Mercury::CSVParser parser;
        auto orders = parser.parseFile(inputFile);

        std::cout << "Orders parsed: " << orders.size() << "\n";
        std::cout << "Lines processed: " << parser.getLinesProcessed() << "\n";
        if (parser.getParseErrorCount() > 0) {
            std::cout << "Parse errors: " << parser.getParseErrorCount() << "\n";
        }
        std::cout << "\n";

        if (orders.empty()) {
            std::cerr << "Error: No valid orders to process\n";
            return 1;
        }

        // Set up output writers
        Mercury::TradeWriter tradeWriter(tradesFile);
        Mercury::ExecutionReportWriter reportWriter(reportsFile);
        Mercury::RiskEventWriter riskEventWriter(riskEventsFile);
        Mercury::PnLTracker pnlTracker(pnlFile);

        if (!tradeWriter.open()) {
            std::cerr << "Error: Could not open trades output file\n";
            return 1;
        }

        if (!reportWriter.open()) {
            std::cerr << "Error: Could not open executions output file\n";
            return 1;
        }

        if (!riskEventWriter.open()) {
            std::cerr << "Error: Could not open risk events output file\n";
            return 1;
        }

        if (!pnlTracker.open()) {
            std::cerr << "Error: Could not open P&L output file\n";
            return 1;
        }

        // Set up risk manager with default limits
        Mercury::RiskLimits limits;
        limits.maxPositionQuantity = 100000;       // Max 100K net position
        limits.maxGrossExposure = 1000000000;      // Max 1B gross exposure
        limits.maxNetExposure = 500000000;         // Max 500M net exposure
        limits.maxDailyLoss = -100000000;          // Max 100M daily loss
        limits.maxOrderValue = 10000000;           // Max 10M per order
        limits.maxOrderQuantity = 10000;           // Max 10K quantity per order
        limits.maxOpenOrders = 1000;               // Max 1000 open orders
        Mercury::RiskManager riskManager(limits);

        // Set up risk event callback to write events as they occur
        riskManager.setRiskCallback([&riskEventWriter](const Mercury::RiskEvent& event) {
            riskEventWriter.writeEvent(event);
        });

        // Set up trade callback to write trades as they occur
        engine.setTradeCallback([&tradeWriter, &pnlTracker](const Mercury::Trade& trade) {
            tradeWriter.writeTrade(trade);
        });

        // Process all orders
        std::cout << "--- Processing Orders ---\n";
        auto startTime = std::chrono::high_resolution_clock::now();

        size_t filled = 0, partialFill = 0, resting = 0;
        size_t cancelled = 0, modified = 0, rejected = 0;
        size_t riskRejected = 0;

        for (const auto& order : orders) {
            // First perform risk check
            auto riskEvent = riskManager.checkOrder(order);
            
            if (riskEvent.isRejected()) {
                // Order rejected by risk manager
                riskRejected++;
                
                // Create a rejection result for the execution report
                Mercury::ExecutionResult result;
                result.status = Mercury::ExecutionStatus::Rejected;
                result.rejectReason = Mercury::RejectReason::InternalError;  // Could add RiskReject
                result.orderId = order.id;
                result.remainingQuantity = order.quantity;
                result.message = "Risk check failed: " + riskEvent.details;
                
                reportWriter.writeReport(order, result);
                rejected++;
                continue;
            }
            
            // Risk check passed, submit to matching engine
            auto result = engine.submitOrder(order);
            
            // Track order in risk manager if it was added to book
            if (result.status == Mercury::ExecutionStatus::Resting ||
                result.status == Mercury::ExecutionStatus::PartialFill) {
                riskManager.onOrderAdded(order);
            }
            
            // Track fills in risk manager
            if (result.hasFills()) {
                for (const auto& trade : result.trades) {
                    // For simplicity, use order.clientId for both sides
                    // In a real system, you'd track the resting order's clientId
                    riskManager.onTradeExecuted(trade, 
                        order.side == Mercury::Side::Buy ? order.clientId : 0,
                        order.side == Mercury::Side::Sell ? order.clientId : 0);
                    
                    // Track P&L for both sides of the trade
                    // The aggressive order's client is order.clientId
                    // For simplicity, we use clientId for the aggressive side
                    uint64_t buyClientId = order.side == Mercury::Side::Buy ? order.clientId : 0;
                    uint64_t sellClientId = order.side == Mercury::Side::Sell ? order.clientId : 0;
                    pnlTracker.onTradeExecuted(trade, buyClientId, sellClientId, trade.price);
                }
            }
            
            // Track cancelled orders
            if (result.status == Mercury::ExecutionStatus::Cancelled) {
                riskManager.onOrderRemoved(order);
            }
            
            // Write execution report
            reportWriter.writeReport(order, result);

            // Track statistics
            switch (result.status) {
                case Mercury::ExecutionStatus::Filled: filled++; break;
                case Mercury::ExecutionStatus::PartialFill: partialFill++; break;
                case Mercury::ExecutionStatus::Resting: resting++; break;
                case Mercury::ExecutionStatus::Cancelled: cancelled++; break;
                case Mercury::ExecutionStatus::Modified: modified++; break;
                case Mercury::ExecutionStatus::Rejected: rejected++; break;
            }
        }

        auto endTime = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);

        // Flush and close output files
        tradeWriter.close();
        reportWriter.close();
        riskEventWriter.close();
        pnlTracker.close();

        // Print summary
        std::cout << "\n========================================\n";
        std::cout << "           Processing Complete\n";
        std::cout << "========================================\n";
        std::cout << "Time elapsed: " << duration.count() / 1000.0 << " ms\n";
        std::cout << "Throughput: " << (orders.size() * 1000000.0 / duration.count()) << " orders/sec\n";
        std::cout << "\n--- Order Status Summary ---\n";
        std::cout << "  Filled:       " << filled << "\n";
        std::cout << "  Partial Fill: " << partialFill << "\n";
        std::cout << "  Resting:      " << resting << "\n";
        std::cout << "  Cancelled:    " << cancelled << "\n";
        std::cout << "  Modified:     " << modified << "\n";
        std::cout << "  Rejected:     " << rejected << "\n";
        std::cout << "\n--- Risk Manager Statistics ---\n";
        std::cout << "  Risk Checks:  " << riskManager.getTotalChecks() << "\n";
        std::cout << "  Approved:     " << riskManager.getApprovedCount() << "\n";
        std::cout << "  Risk Rejected:" << riskRejected << "\n";
        std::cout << "  Clients:      " << riskManager.getClientCount() << "\n";
        std::cout << "\n--- Trading Statistics ---\n";
        std::cout << "  Total Trades: " << engine.getTradeCount() << "\n";
        std::cout << "  Total Volume: " << engine.getTotalVolume() << " units\n";
        std::cout << "\n--- Order Book State ---\n";
        std::cout << "  Orders in Book: " << engine.getOrderBook().getOrderCount() << "\n";
        std::cout << "  Bid Levels: " << engine.getOrderBook().getBidLevelCount() << "\n";
        std::cout << "  Ask Levels: " << engine.getOrderBook().getAskLevelCount() << "\n";
        std::cout << "\n--- Output Files ---\n";
        std::cout << "  Trades written: " << tradeWriter.getTradeCount() << " -> " << tradesFile << "\n";
        std::cout << "  Reports written: " << reportWriter.getReportCount() << " -> " << reportsFile << "\n";
        std::cout << "  Risk events:    " << riskEventWriter.getEventCount() << " -> " << riskEventsFile << "\n";
        std::cout << "  P&L snapshots:  " << pnlTracker.getSnapshotCount() << " -> " << pnlFile << "\n";
        std::cout << "\n--- P&L Summary ---\n";
        std::cout << "  Clients tracked: " << pnlTracker.getClientCount() << "\n";
        for (const auto& [clientId, pnl] : pnlTracker.getAllClientPnL()) {
            if (clientId > 0) {
                std::cout << "  Client " << clientId << ": "
                          << "Net Pos=" << pnl.netPosition 
                          << ", Realized=" << pnl.realizedPnL 
                          << ", Unrealized=" << pnl.unrealizedPnL 
                          << ", Total=" << pnl.totalPnL << "\n";
            }
        }
        std::cout << "========================================\n";

        // Optionally print final order book
        if (engine.getOrderBook().getOrderCount() > 0 && 
            engine.getOrderBook().getOrderCount() <= 20) {
            std::cout << "\nFinal Order Book:\n";
            engine.getOrderBook().printBook();
        }
    } else {
        // Run interactive demo
        std::cout << "\nUsage: mercury <orders.csv> [trades.csv] [executions.csv] [riskevents.csv] [pnl.csv]\n";
        std::cout << "  orders.csv     - Input file with orders to process\n";
        std::cout << "  trades.csv     - Output file for trade results (default: trades.csv)\n";
        std::cout << "  executions.csv - Output file for execution reports (default: executions.csv)\n";
        std::cout << "  riskevents.csv - Output file for risk events (default: riskevents.csv)\n";
        std::cout << "  pnl.csv        - Output file for P&L snapshots (default: pnl.csv)\n\n";
        runDemo();
    }

    return 0;
}
