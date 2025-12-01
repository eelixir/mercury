#include <iostream>
#include <iomanip>
#include <chrono>
#include <thread>
#include "OrderBook.h" 
#include "Order.h"
#include "CSVParser.h"
#include "MatchingEngine.h"
#include "ConcurrentMatchingEngine.h"
#include "TradeWriter.h"
#include "RiskManager.h"
#include "PnLTracker.h"
#include "ThreadPool.h"
#include "AsyncWriter.h"
#include "StrategyDemo.h"
#include "BacktestDemo.h"

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
    std::cout << "Hardware concurrency: " << std::thread::hardware_concurrency() << " threads\n";

    // Create the matching engine
    Mercury::MatchingEngine engine;

    // Check if a CSV file was provided as argument
    if (argc > 1) {
        // First, collect all non-flag arguments and flags separately
        std::vector<std::string> positionalArgs;
        bool useConcurrency = false;
        bool useAsyncWriters = false;
        
        bool runStrategies = false;
        bool runBacktest = false;
        
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--concurrent" || arg == "-c") {
                useConcurrency = true;
            } else if (arg == "--async-io" || arg == "-a") {
                useAsyncWriters = true;
            } else if (arg == "--strategies" || arg == "-s") {
                runStrategies = true;
            } else if (arg == "--backtest" || arg == "-b") {
                runBacktest = true;
            } else if (arg[0] != '-') {
                positionalArgs.push_back(arg);
            }
        }
        
        // If strategies flag is set, run strategy demos
        if (runStrategies) {
            Mercury::runAllStrategyDemos();
            return 0;
        }
        
        // If backtest flag is set, run backtest demos
        if (runBacktest) {
            // Check if specific backtest requested
            if (positionalArgs.empty()) {
                Mercury::runAllBacktestDemos();
            } else {
                std::string backtestType = positionalArgs[0];
                if (backtestType == "mm" || backtestType == "marketmaking") {
                    Mercury::runMarketMakingBacktest();
                } else if (backtestType == "momentum" || backtestType == "mom") {
                    Mercury::runMomentumBacktest();
                } else if (backtestType == "multi") {
                    Mercury::runMultiStrategyBacktest();
                } else if (backtestType == "compare" || backtestType == "comparison") {
                    Mercury::runMarketConditionComparison();
                } else if (backtestType == "stress") {
                    Mercury::runStressBacktest();
                } else {
                    Mercury::runAllBacktestDemos();
                }
            }
            return 0;
        }
        
        if (positionalArgs.empty()) {
            std::cerr << "Error: No input file specified\n";
            return 1;
        }
        
        std::string inputFile = positionalArgs[0];
        
        // Determine output file names from positional args
        std::string tradesFile = (positionalArgs.size() > 1) ? positionalArgs[1] : "trades.csv";
        std::string reportsFile = (positionalArgs.size() > 2) ? positionalArgs[2] : "executions.csv";
        std::string riskEventsFile = (positionalArgs.size() > 3) ? positionalArgs[3] : "riskevents.csv";
        std::string pnlFile = (positionalArgs.size() > 4) ? positionalArgs[4] : "pnl.csv";

        std::cout << "\n========================================\n";
        std::cout << "   Mercury File I/O Mode\n";
        std::cout << "========================================\n";
        std::cout << "Input:       " << inputFile << "\n";
        std::cout << "Trades:      " << tradesFile << "\n";
        std::cout << "Executions:  " << reportsFile << "\n";
        std::cout << "Risk Events: " << riskEventsFile << "\n";
        std::cout << "P&L:         " << pnlFile << "\n";
        std::cout << "Concurrency: " << (useConcurrency ? "Enabled" : "Disabled") << "\n";
        std::cout << "Async I/O:   " << (useAsyncWriters ? "Enabled" : "Disabled") << "\n";
        std::cout << "========================================\n\n";

        // Parse input orders (with parallel parsing for large files)
        std::cout << "--- Parsing Orders ---\n";
        auto parseStartTime = std::chrono::high_resolution_clock::now();
        
        Mercury::CSVParser parser;
        std::vector<Mercury::Order> orders;
        
        if (useConcurrency) {
            orders = parser.parseFileParallel(inputFile);
        } else {
            orders = parser.parseFile(inputFile);
        }
        
        auto parseEndTime = std::chrono::high_resolution_clock::now();
        auto parseDuration = std::chrono::duration_cast<std::chrono::microseconds>(parseEndTime - parseStartTime);

        std::cout << "Orders parsed: " << orders.size() << "\n";
        std::cout << "Lines processed: " << parser.getLinesProcessed() << "\n";
        std::cout << "Parse time: " << parseDuration.count() / 1000.0 << " ms\n";
        if (parser.getParseErrorCount() > 0) {
            std::cout << "Parse errors: " << parser.getParseErrorCount() << "\n";
        }
        std::cout << "\n";

        if (orders.empty()) {
            std::cerr << "Error: No valid orders to process\n";
            return 1;
        }

        // Set up output writers (async or sync based on flag)
        std::unique_ptr<Mercury::AsyncWriter> asyncTradeWriter;
        std::unique_ptr<Mercury::AsyncWriter> asyncReportWriter;
        std::unique_ptr<Mercury::AsyncWriter> asyncRiskWriter;
        std::unique_ptr<Mercury::AsyncWriter> asyncPnlWriter;
        
        Mercury::TradeWriter tradeWriter(tradesFile);
        Mercury::ExecutionReportWriter reportWriter(reportsFile);
        Mercury::RiskEventWriter riskEventWriter(riskEventsFile);
        Mercury::PnLTracker pnlTracker(pnlFile);

        if (useAsyncWriters) {
            asyncTradeWriter = std::make_unique<Mercury::AsyncWriter>(tradesFile);
            asyncReportWriter = std::make_unique<Mercury::AsyncWriter>(reportsFile);
            asyncRiskWriter = std::make_unique<Mercury::AsyncWriter>(riskEventsFile);
            asyncPnlWriter = std::make_unique<Mercury::AsyncWriter>(pnlFile);
            
            if (!asyncTradeWriter->open() || !asyncReportWriter->open() ||
                !asyncRiskWriter->open() || !asyncPnlWriter->open()) {
                std::cerr << "Error: Could not open async output files\n";
                return 1;
            }
            
            // Write headers
            asyncTradeWriter->write("trade_id,timestamp,buy_order_id,sell_order_id,price,quantity\n");
            asyncReportWriter->write("order_id,timestamp,type,side,status,reject_reason,filled_qty,remaining_qty,trade_count,avg_price\n");
            asyncRiskWriter->write("event_id,timestamp,order_id,client_id,event_type,current_value,limit_value,requested_value,details\n");
            asyncPnlWriter->write("snapshot_id,timestamp,client_id,net_position,long_qty,short_qty,realized_pnl,unrealized_pnl,total_pnl,mark_price,cost_basis,avg_entry_price,trade_id\n");
        } else {
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

        // Set up post-trade processor for async processing
        std::unique_ptr<Mercury::PostTradeProcessor> postTradeProcessor;
        std::mutex pnlMutex;  // Protect PnLTracker in concurrent mode
        if (useConcurrency) {
            postTradeProcessor = std::make_unique<Mercury::PostTradeProcessor>(2);
            
            // Set up async trade handler (thread-safe)
            postTradeProcessor->setTradeHandler(
                [&pnlTracker, &pnlMutex](const Mercury::Trade& trade, uint64_t buyClientId, uint64_t sellClientId) {
                    std::lock_guard<std::mutex> lock(pnlMutex);
                    pnlTracker.onTradeExecuted(trade, buyClientId, sellClientId, trade.price);
                });
        }

        // Set up risk event callback
        if (useAsyncWriters) {
            riskManager.setRiskCallback([&asyncRiskWriter](const Mercury::RiskEvent& event) {
                std::ostringstream oss;
                oss << event.eventId << "," << event.timestamp << ","
                    << event.orderId << "," << event.clientId << ","
                    << Mercury::riskEventTypeToString(event.eventType) << ","
                    << event.currentValue << "," << event.limitValue << ","
                    << event.requestedValue << "," << event.details << "\n";
                asyncRiskWriter->write(oss.str());
            });
        } else {
            riskManager.setRiskCallback([&riskEventWriter](const Mercury::RiskEvent& event) {
                riskEventWriter.writeEvent(event);
            });
        }

        // Set up trade callback
        std::atomic<size_t> asyncTradeCount{0};
        if (useAsyncWriters) {
            engine.setTradeCallback([&asyncTradeWriter, &asyncTradeCount](const Mercury::Trade& trade) {
                std::ostringstream oss;
                oss << trade.tradeId << "," << trade.timestamp << ","
                    << trade.buyOrderId << "," << trade.sellOrderId << ","
                    << trade.price << "," << trade.quantity << "\n";
                asyncTradeWriter->write(oss.str());
                ++asyncTradeCount;
            });
        } else {
            engine.setTradeCallback([&tradeWriter, &pnlTracker](const Mercury::Trade& trade) {
                tradeWriter.writeTrade(trade);
            });
        }

        // Process all orders
        std::cout << "--- Processing Orders ---\n";
        auto startTime = std::chrono::high_resolution_clock::now();

        std::atomic<size_t> filled{0}, partialFill{0}, resting{0};
        std::atomic<size_t> cancelled{0}, modified{0}, rejected{0};
        std::atomic<size_t> riskRejected{0};
        std::atomic<size_t> asyncReportCount{0};

        // Helper lambda for processing execution results
        auto processResult = [&](const Mercury::Order& order, const Mercury::ExecutionResult& result) {
            // Track order in risk manager if it was added to book
            if (result.status == Mercury::ExecutionStatus::Resting ||
                result.status == Mercury::ExecutionStatus::PartialFill) {
                riskManager.onOrderAdded(order);
            }
            
            // Track fills in risk manager
            if (result.hasFills()) {
                for (const auto& trade : result.trades) {
                    riskManager.onTradeExecuted(trade, 
                        order.side == Mercury::Side::Buy ? order.clientId : 0,
                        order.side == Mercury::Side::Sell ? order.clientId : 0);
                    
                    // Handle P&L tracking
                    if (postTradeProcessor) {
                        uint64_t buyClientId = order.side == Mercury::Side::Buy ? order.clientId : 0;
                        uint64_t sellClientId = order.side == Mercury::Side::Sell ? order.clientId : 0;
                        postTradeProcessor->processTrade(trade, buyClientId, sellClientId);
                    } else if (!useAsyncWriters) {
                        uint64_t buyClientId = order.side == Mercury::Side::Buy ? order.clientId : 0;
                        uint64_t sellClientId = order.side == Mercury::Side::Sell ? order.clientId : 0;
                        pnlTracker.onTradeExecuted(trade, buyClientId, sellClientId, trade.price);
                    }
                }
            }
            
            // Track cancelled orders
            if (result.status == Mercury::ExecutionStatus::Cancelled) {
                riskManager.onOrderRemoved(order);
            }
            
            // Write execution report
            if (useAsyncWriters) {
                std::ostringstream oss;
                double avgPrice = 0.0;
                if (result.filledQuantity > 0 && !result.trades.empty()) {
                    int64_t totalValue = 0;
                    for (const auto& trade : result.trades) {
                        totalValue += trade.price * static_cast<int64_t>(trade.quantity);
                    }
                    avgPrice = static_cast<double>(totalValue) / static_cast<double>(result.filledQuantity);
                }
                
                const char* typeStr = order.orderType == Mercury::OrderType::Market ? "market" :
                                     order.orderType == Mercury::OrderType::Limit ? "limit" :
                                     order.orderType == Mercury::OrderType::Cancel ? "cancel" : "modify";
                const char* sideStr = order.side == Mercury::Side::Buy ? "buy" : "sell";
                const char* statusStr = result.status == Mercury::ExecutionStatus::Filled ? "filled" :
                                       result.status == Mercury::ExecutionStatus::PartialFill ? "partial_fill" :
                                       result.status == Mercury::ExecutionStatus::Resting ? "resting" :
                                       result.status == Mercury::ExecutionStatus::Cancelled ? "cancelled" :
                                       result.status == Mercury::ExecutionStatus::Modified ? "modified" : "rejected";
                
                oss << order.id << "," << order.timestamp << ","
                    << typeStr << "," << sideStr << ","
                    << statusStr << "," << Mercury::rejectReasonToString(result.rejectReason) << ","
                    << result.filledQuantity << "," << result.remainingQuantity << ","
                    << result.trades.size() << "," << std::fixed << std::setprecision(2) << avgPrice << "\n";
                asyncReportWriter->write(oss.str());
                ++asyncReportCount;
            } else {
                reportWriter.writeReport(order, result);
            }

            // Track statistics
            switch (result.status) {
                case Mercury::ExecutionStatus::Filled: ++filled; break;
                case Mercury::ExecutionStatus::PartialFill: ++partialFill; break;
                case Mercury::ExecutionStatus::Resting: ++resting; break;
                case Mercury::ExecutionStatus::Cancelled: ++cancelled; break;
                case Mercury::ExecutionStatus::Modified: ++modified; break;
                case Mercury::ExecutionStatus::Rejected: ++rejected; break;
            }
        };

        // Process orders (sequentially - order book requires sequential access)
        for (const auto& order : orders) {
            // First perform risk check
            auto riskEvent = riskManager.checkOrder(order);
            
            if (riskEvent.isRejected()) {
                // Order rejected by risk manager
                ++riskRejected;
                
                // Create a rejection result for the execution report
                Mercury::ExecutionResult result;
                result.status = Mercury::ExecutionStatus::Rejected;
                result.rejectReason = Mercury::RejectReason::InternalError;
                result.orderId = order.id;
                result.remainingQuantity = order.quantity;
                result.message = "Risk check failed: " + riskEvent.details;
                
                if (useAsyncWriters) {
                    std::ostringstream oss;
                    const char* typeStr = order.orderType == Mercury::OrderType::Market ? "market" :
                                         order.orderType == Mercury::OrderType::Limit ? "limit" :
                                         order.orderType == Mercury::OrderType::Cancel ? "cancel" : "modify";
                    const char* sideStr = order.side == Mercury::Side::Buy ? "buy" : "sell";
                    
                    oss << order.id << "," << order.timestamp << ","
                        << typeStr << "," << sideStr << ","
                        << "rejected," << Mercury::rejectReasonToString(result.rejectReason) << ","
                        << "0," << result.remainingQuantity << ","
                        << "0,0.00\n";
                    asyncReportWriter->write(oss.str());
                    ++asyncReportCount;
                } else {
                    reportWriter.writeReport(order, result);
                }
                ++rejected;
                continue;
            }
            
            // Risk check passed, submit to matching engine
            auto result = engine.submitOrder(order);
            processResult(order, result);
        }

        auto endTime = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);

        // Wait for async operations to complete
        if (postTradeProcessor) {
            postTradeProcessor->waitAll();
        }

        // Flush and close output files
        if (useAsyncWriters) {
            asyncTradeWriter->close();
            asyncReportWriter->close();
            asyncRiskWriter->close();
            asyncPnlWriter->close();
        } else {
            tradeWriter.close();
            reportWriter.close();
            riskEventWriter.close();
            pnlTracker.close();
        }

        // Print summary
        std::cout << "\n========================================\n";
        std::cout << "           Processing Complete\n";
        std::cout << "========================================\n";
        std::cout << "Parse time:    " << parseDuration.count() / 1000.0 << " ms\n";
        std::cout << "Process time:  " << duration.count() / 1000.0 << " ms\n";
        std::cout << "Total time:    " << (parseDuration.count() + duration.count()) / 1000.0 << " ms\n";
        std::cout << "Throughput:    " << (orders.size() * 1000000.0 / duration.count()) << " orders/sec\n";
        std::cout << "\n--- Order Status Summary ---\n";
        std::cout << "  Filled:       " << filled.load() << "\n";
        std::cout << "  Partial Fill: " << partialFill.load() << "\n";
        std::cout << "  Resting:      " << resting.load() << "\n";
        std::cout << "  Cancelled:    " << cancelled.load() << "\n";
        std::cout << "  Modified:     " << modified.load() << "\n";
        std::cout << "  Rejected:     " << rejected.load() << "\n";
        std::cout << "\n--- Risk Manager Statistics ---\n";
        std::cout << "  Risk Checks:  " << riskManager.getTotalChecks() << "\n";
        std::cout << "  Approved:     " << riskManager.getApprovedCount() << "\n";
        std::cout << "  Risk Rejected:" << riskRejected.load() << "\n";
        std::cout << "  Clients:      " << riskManager.getClientCount() << "\n";
        std::cout << "\n--- Trading Statistics ---\n";
        std::cout << "  Total Trades: " << engine.getTradeCount() << "\n";
        std::cout << "  Total Volume: " << engine.getTotalVolume() << " units\n";
        std::cout << "\n--- Order Book State ---\n";
        std::cout << "  Orders in Book: " << engine.getOrderBook().getOrderCount() << "\n";
        std::cout << "  Bid Levels: " << engine.getOrderBook().getBidLevelCount() << "\n";
        std::cout << "  Ask Levels: " << engine.getOrderBook().getAskLevelCount() << "\n";
        std::cout << "\n--- Output Files ---\n";
        if (useAsyncWriters) {
            std::cout << "  Trades written: " << asyncTradeCount.load() << " -> " << tradesFile << "\n";
            std::cout << "  Reports written: " << asyncReportCount.load() << " -> " << reportsFile << "\n";
        } else {
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
        std::cout << "\nUsage: mercury <orders.csv> [trades.csv] [executions.csv] [riskevents.csv] [pnl.csv] [options]\n";
        std::cout << "  orders.csv     - Input file with orders to process\n";
        std::cout << "  trades.csv     - Output file for trade results (default: trades.csv)\n";
        std::cout << "  executions.csv - Output file for execution reports (default: executions.csv)\n";
        std::cout << "  riskevents.csv - Output file for risk events (default: riskevents.csv)\n";
        std::cout << "  pnl.csv        - Output file for P&L snapshots (default: pnl.csv)\n\n";
        std::cout << "Options:\n";
        std::cout << "  --concurrent, -c   Enable concurrent parsing and post-trade processing\n";
        std::cout << "  --async-io, -a     Enable asynchronous I/O writers\n";
        std::cout << "  --strategies, -s   Run trading strategy demos\n";
        std::cout << "  --backtest, -b     Run backtesting demos\n\n";
        std::cout << "Backtest modes (use with --backtest):\n";
        std::cout << "  mercury --backtest              Run all backtest demos\n";
        std::cout << "  mercury --backtest mm           Market making backtest\n";
        std::cout << "  mercury --backtest momentum     Momentum strategy backtest\n";
        std::cout << "  mercury --backtest multi        Multi-strategy backtest\n";
        std::cout << "  mercury --backtest compare      Market condition comparison\n";
        std::cout << "  mercury --backtest stress       Stress test backtest\n\n";
        runDemo();
    }

    return 0;
}
