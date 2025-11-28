#include <iostream>
#include "OrderBook.h" 
#include "Order.h"
#include "CSVParser.h"

int main(int argc, char* argv[]) {
    std::cout << "Initializing Mercury Trading Engine...\n";

    // Create the OrderBook
    Mercury::OrderBook book;

    // Check if a CSV file was provided as argument
    if (argc > 1) {
        std::cout << "\n--- Loading orders from CSV file ---\n";
        Mercury::CSVParser parser;
        auto orders = parser.parseFile(argv[1]);

        std::cout << "Parsed " << orders.size() << " orders from file\n";
        std::cout << "Lines processed: " << parser.getLinesProcessed() << "\n";
        std::cout << "Parse errors: " << parser.getParseErrorCount() << "\n\n";

        // Add all parsed orders to the book
        for (const auto& order : orders) {
            book.addOrder(order);
        }

        book.printBook();
    } else {
        // Fallback to manual order creation for demo
        std::cout << "\n--- Demo mode (no CSV file provided) ---\n";
        std::cout << "Usage: mercury <orders.csv>\n\n";

        // Create some sample orders
        // Buy Order: Bid $100.50 for 10 units (Price in cents: 10050)
        Mercury::Order buyOrder1;
        buyOrder1.id = 1;
        buyOrder1.side = Mercury::Side::Buy;
        buyOrder1.orderType = Mercury::OrderType::Limit;
        buyOrder1.price = 10050;
        buyOrder1.quantity = 10;
        buyOrder1.timestamp = 1000;

        // Buy Order: Bid $100.00 for 5 units (Price in cents: 10000)
        Mercury::Order buyOrder2;
        buyOrder2.id = 2;
        buyOrder2.side = Mercury::Side::Buy;
        buyOrder2.orderType = Mercury::OrderType::Limit;
        buyOrder2.price = 10000;
        buyOrder2.quantity = 5;
        buyOrder2.timestamp = 1001;

        // Sell Order: Ask $101.00 for 20 units (Price in cents: 10100)
        Mercury::Order sellOrder1;
        sellOrder1.id = 3;
        sellOrder1.side = Mercury::Side::Sell;
        sellOrder1.orderType = Mercury::OrderType::Limit;
        sellOrder1.price = 10100;
        sellOrder1.quantity = 20;
        sellOrder1.timestamp = 1002;

        // Add orders to the book
        std::cout << "Adding orders...\n";
        book.addOrder(buyOrder1);
        book.addOrder(buyOrder2);
        book.addOrder(sellOrder1);

        // Print the book state
        book.printBook();

        // Remove an order
        std::cout << "\nRemoving Order #1...\n";
        book.removeOrder(1);
        
        // Print again to verify removal
        book.printBook();
    }

    return 0;
}