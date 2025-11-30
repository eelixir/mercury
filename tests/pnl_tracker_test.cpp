#include <gtest/gtest.h>
#include "PnLTracker.h"
#include <filesystem>
#include <fstream>

namespace Mercury {
namespace Testing {

class PnLTrackerTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Use a temp file for testing
        testFile = "test_pnl_output.csv";
    }

    void TearDown() override {
        // Clean up test file
        std::filesystem::remove(testFile);
    }

    std::string testFile;
};

// Test basic construction and file handling
TEST_F(PnLTrackerTest, BasicConstruction) {
    PnLTracker tracker(testFile);
    EXPECT_FALSE(tracker.isOpen());
    EXPECT_EQ(tracker.getFilePath(), testFile);
    EXPECT_EQ(tracker.getClientCount(), 0);
    EXPECT_EQ(tracker.getSnapshotCount(), 0);
}

TEST_F(PnLTrackerTest, OpenAndClose) {
    PnLTracker tracker(testFile);
    EXPECT_TRUE(tracker.open());
    EXPECT_TRUE(tracker.isOpen());
    tracker.close();
    EXPECT_FALSE(tracker.isOpen());
    
    // Verify file was created with header
    std::ifstream file(testFile);
    EXPECT_TRUE(file.is_open());
    std::string header;
    std::getline(file, header);
    EXPECT_TRUE(header.find("snapshot_id") != std::string::npos);
    EXPECT_TRUE(header.find("realized_pnl") != std::string::npos);
    EXPECT_TRUE(header.find("unrealized_pnl") != std::string::npos);
}

// Test single buy trade with P&L tracking
TEST_F(PnLTrackerTest, SingleBuyTrade) {
    PnLTracker tracker(testFile);
    tracker.open();

    Trade trade;
    trade.tradeId = 1;
    trade.buyOrderId = 100;
    trade.sellOrderId = 200;
    trade.price = 1000;
    trade.quantity = 10;
    trade.timestamp = 12345;

    // Client 1 is buying
    tracker.onTradeExecuted(trade, 1, 0, trade.price);

    auto pnl = tracker.getClientPnL(1);
    EXPECT_EQ(pnl.clientId, 1);
    EXPECT_EQ(pnl.longQuantity, 10);
    EXPECT_EQ(pnl.shortQuantity, 0);
    EXPECT_EQ(pnl.netPosition, 10);
    EXPECT_EQ(pnl.totalBuyQuantity, 10);
    EXPECT_EQ(pnl.totalBuyCost, 10000);  // 10 * 1000
    EXPECT_EQ(pnl.realizedPnL, 0);  // No closed positions yet
    EXPECT_EQ(pnl.unrealizedPnL, 0);  // Mark price == entry price

    EXPECT_EQ(tracker.getClientCount(), 1);
    EXPECT_GE(tracker.getSnapshotCount(), 1);
}

// Test single sell trade (opening short)
TEST_F(PnLTrackerTest, SingleSellTrade) {
    PnLTracker tracker(testFile);
    tracker.open();

    Trade trade;
    trade.tradeId = 1;
    trade.buyOrderId = 100;
    trade.sellOrderId = 200;
    trade.price = 1000;
    trade.quantity = 10;
    trade.timestamp = 12345;

    // Client 2 is selling (opening short)
    tracker.onTradeExecuted(trade, 0, 2, trade.price);

    auto pnl = tracker.getClientPnL(2);
    EXPECT_EQ(pnl.clientId, 2);
    EXPECT_EQ(pnl.longQuantity, 0);
    EXPECT_EQ(pnl.shortQuantity, 10);
    EXPECT_EQ(pnl.netPosition, -10);
    EXPECT_EQ(pnl.totalSellQuantity, 10);
    EXPECT_EQ(pnl.totalSellProceeds, 10000);
    EXPECT_EQ(pnl.realizedPnL, 0);
}

// Test closing a long position for profit
TEST_F(PnLTrackerTest, CloseLongForProfit) {
    PnLTracker tracker(testFile);
    tracker.open();

    // Trade 1: Buy 10 @ 100
    Trade buyTrade;
    buyTrade.tradeId = 1;
    buyTrade.buyOrderId = 100;
    buyTrade.sellOrderId = 200;
    buyTrade.price = 100;
    buyTrade.quantity = 10;
    tracker.onTradeExecuted(buyTrade, 1, 0, buyTrade.price);

    auto pnl1 = tracker.getClientPnL(1);
    EXPECT_EQ(pnl1.longQuantity, 10);
    EXPECT_EQ(pnl1.realizedPnL, 0);

    // Trade 2: Sell 10 @ 150 (closing for profit)
    Trade sellTrade;
    sellTrade.tradeId = 2;
    sellTrade.buyOrderId = 300;
    sellTrade.sellOrderId = 100;
    sellTrade.price = 150;
    sellTrade.quantity = 10;
    tracker.onTradeExecuted(sellTrade, 0, 1, sellTrade.price);

    auto pnl2 = tracker.getClientPnL(1);
    EXPECT_EQ(pnl2.longQuantity, 0);
    EXPECT_EQ(pnl2.netPosition, 0);
    // Realized P&L = (150 - 100) * 10 = 500
    EXPECT_EQ(pnl2.realizedPnL, 500);
}

// Test closing a long position for loss
TEST_F(PnLTrackerTest, CloseLongForLoss) {
    PnLTracker tracker(testFile);
    tracker.open();

    // Trade 1: Buy 10 @ 100
    Trade buyTrade;
    buyTrade.tradeId = 1;
    buyTrade.price = 100;
    buyTrade.quantity = 10;
    tracker.onTradeExecuted(buyTrade, 1, 0, buyTrade.price);

    // Trade 2: Sell 10 @ 80 (closing for loss)
    Trade sellTrade;
    sellTrade.tradeId = 2;
    sellTrade.price = 80;
    sellTrade.quantity = 10;
    tracker.onTradeExecuted(sellTrade, 0, 1, sellTrade.price);

    auto pnl = tracker.getClientPnL(1);
    EXPECT_EQ(pnl.longQuantity, 0);
    // Realized P&L = (80 - 100) * 10 = -200
    EXPECT_EQ(pnl.realizedPnL, -200);
}

// Test closing a short position for profit
TEST_F(PnLTrackerTest, CloseShortForProfit) {
    PnLTracker tracker(testFile);
    tracker.open();

    // Trade 1: Sell 10 @ 150 (opening short)
    Trade sellTrade;
    sellTrade.tradeId = 1;
    sellTrade.price = 150;
    sellTrade.quantity = 10;
    tracker.onTradeExecuted(sellTrade, 0, 1, sellTrade.price);

    auto pnl1 = tracker.getClientPnL(1);
    EXPECT_EQ(pnl1.shortQuantity, 10);

    // Trade 2: Buy 10 @ 100 (covering short for profit)
    Trade buyTrade;
    buyTrade.tradeId = 2;
    buyTrade.price = 100;
    buyTrade.quantity = 10;
    tracker.onTradeExecuted(buyTrade, 1, 0, buyTrade.price);

    auto pnl2 = tracker.getClientPnL(1);
    EXPECT_EQ(pnl2.shortQuantity, 0);
    EXPECT_EQ(pnl2.netPosition, 0);
    // Short P&L = (sell price - buy price) * qty = (150 - 100) * 10 = 500
    EXPECT_EQ(pnl2.realizedPnL, 500);
}

// Test closing a short position for loss
TEST_F(PnLTrackerTest, CloseShortForLoss) {
    PnLTracker tracker(testFile);
    tracker.open();

    // Trade 1: Sell 10 @ 100 (opening short)
    Trade sellTrade;
    sellTrade.tradeId = 1;
    sellTrade.price = 100;
    sellTrade.quantity = 10;
    tracker.onTradeExecuted(sellTrade, 0, 1, sellTrade.price);

    // Trade 2: Buy 10 @ 130 (covering short for loss)
    Trade buyTrade;
    buyTrade.tradeId = 2;
    buyTrade.price = 130;
    buyTrade.quantity = 10;
    tracker.onTradeExecuted(buyTrade, 1, 0, buyTrade.price);

    auto pnl = tracker.getClientPnL(1);
    // Short P&L = (100 - 130) * 10 = -300
    EXPECT_EQ(pnl.realizedPnL, -300);
}

// Test partial position close
TEST_F(PnLTrackerTest, PartialClose) {
    PnLTracker tracker(testFile);
    tracker.open();

    // Trade 1: Buy 100 @ 50
    Trade buyTrade;
    buyTrade.tradeId = 1;
    buyTrade.price = 50;
    buyTrade.quantity = 100;
    tracker.onTradeExecuted(buyTrade, 1, 0, buyTrade.price);

    // Trade 2: Sell 40 @ 60 (partial close)
    Trade sellTrade;
    sellTrade.tradeId = 2;
    sellTrade.price = 60;
    sellTrade.quantity = 40;
    tracker.onTradeExecuted(sellTrade, 0, 1, sellTrade.price);

    auto pnl = tracker.getClientPnL(1);
    EXPECT_EQ(pnl.longQuantity, 60);  // 100 - 40
    EXPECT_EQ(pnl.netPosition, 60);
    // Realized P&L on 40 units = (60 - 50) * 40 = 400
    EXPECT_EQ(pnl.realizedPnL, 400);
}

// Test unrealized P&L calculation
TEST_F(PnLTrackerTest, UnrealizedPnL) {
    PnLTracker tracker(testFile);
    tracker.open();

    // Buy 10 @ 100
    Trade buyTrade;
    buyTrade.tradeId = 1;
    buyTrade.price = 100;
    buyTrade.quantity = 10;
    tracker.onTradeExecuted(buyTrade, 1, 0, buyTrade.price);

    // Update mark-to-market at price 120
    int64_t unrealized = tracker.updateMarkToMarket(1, 120);
    // Unrealized = (120 - 100) * 10 = 200
    EXPECT_EQ(unrealized, 200);

    auto pnl = tracker.getClientPnL(1);
    EXPECT_EQ(pnl.unrealizedPnL, 200);
    EXPECT_EQ(pnl.totalPnL, 200);  // realized (0) + unrealized (200)
}

// Test unrealized P&L for short position
TEST_F(PnLTrackerTest, UnrealizedPnLShort) {
    PnLTracker tracker(testFile);
    tracker.open();

    // Sell 10 @ 100 (open short)
    Trade sellTrade;
    sellTrade.tradeId = 1;
    sellTrade.price = 100;
    sellTrade.quantity = 10;
    tracker.onTradeExecuted(sellTrade, 0, 1, sellTrade.price);

    // Mark-to-market at 80 (profit on short)
    int64_t unrealized = tracker.updateMarkToMarket(1, 80);
    // Unrealized = (100 - 80) * 10 = 200
    EXPECT_EQ(unrealized, 200);

    // Mark-to-market at 120 (loss on short)
    unrealized = tracker.updateMarkToMarket(1, 120);
    // Unrealized = (100 - 120) * 10 = -200
    EXPECT_EQ(unrealized, -200);
}

// Test FIFO ordering for multiple trades
TEST_F(PnLTrackerTest, FIFOOrdering) {
    PnLTracker tracker(testFile);
    tracker.open();

    // Buy 10 @ 100, then buy 10 @ 110
    Trade buy1;
    buy1.tradeId = 1;
    buy1.price = 100;
    buy1.quantity = 10;
    tracker.onTradeExecuted(buy1, 1, 0, buy1.price);

    Trade buy2;
    buy2.tradeId = 2;
    buy2.price = 110;
    buy2.quantity = 10;
    tracker.onTradeExecuted(buy2, 1, 0, buy2.price);

    auto pnl1 = tracker.getClientPnL(1);
    EXPECT_EQ(pnl1.longQuantity, 20);

    // Sell 10 @ 120 - should close FIRST buy at 100
    Trade sell;
    sell.tradeId = 3;
    sell.price = 120;
    sell.quantity = 10;
    tracker.onTradeExecuted(sell, 0, 1, sell.price);

    auto pnl2 = tracker.getClientPnL(1);
    EXPECT_EQ(pnl2.longQuantity, 10);
    // FIFO: closes first lot @ 100, realized = (120 - 100) * 10 = 200
    EXPECT_EQ(pnl2.realizedPnL, 200);
}

// Test multiple clients
TEST_F(PnLTrackerTest, MultipleClients) {
    PnLTracker tracker(testFile);
    tracker.open();

    // Client 1 buys, Client 2 sells
    Trade trade;
    trade.tradeId = 1;
    trade.price = 100;
    trade.quantity = 50;
    tracker.onTradeExecuted(trade, 1, 2, trade.price);

    auto pnl1 = tracker.getClientPnL(1);
    auto pnl2 = tracker.getClientPnL(2);

    EXPECT_EQ(pnl1.longQuantity, 50);
    EXPECT_EQ(pnl1.shortQuantity, 0);
    EXPECT_EQ(pnl2.longQuantity, 0);
    EXPECT_EQ(pnl2.shortQuantity, 50);

    EXPECT_EQ(tracker.getClientCount(), 2);
}

// Test reset functionality
TEST_F(PnLTrackerTest, Reset) {
    PnLTracker tracker(testFile);
    tracker.open();

    Trade trade;
    trade.tradeId = 1;
    trade.price = 100;
    trade.quantity = 10;
    tracker.onTradeExecuted(trade, 1, 0, trade.price);

    EXPECT_EQ(tracker.getClientCount(), 1);
    EXPECT_GT(tracker.getSnapshotCount(), 0);

    tracker.reset();

    EXPECT_EQ(tracker.getClientCount(), 0);
    EXPECT_EQ(tracker.getSnapshotCount(), 0);
}

// Test PnL callback
TEST_F(PnLTrackerTest, PnLCallback) {
    PnLTracker tracker(testFile);
    tracker.open();

    int callbackCount = 0;
    PnLSnapshot lastSnapshot;
    
    tracker.setPnLCallback([&callbackCount, &lastSnapshot](const PnLSnapshot& snapshot) {
        callbackCount++;
        lastSnapshot = snapshot;
    });

    Trade trade;
    trade.tradeId = 1;
    trade.price = 100;
    trade.quantity = 10;
    tracker.onTradeExecuted(trade, 1, 0, trade.price);

    EXPECT_EQ(callbackCount, 1);
    EXPECT_EQ(lastSnapshot.clientId, 1);
    EXPECT_EQ(lastSnapshot.netPosition, 10);
}

// ==================== PnLWriter Tests ====================

class PnLWriterTest : public ::testing::Test {
protected:
    void SetUp() override {
        testFile = "test_pnl_writer.csv";
    }

    void TearDown() override {
        std::filesystem::remove(testFile);
    }

    std::string testFile;
};

TEST_F(PnLWriterTest, WriteSnapshot) {
    PnLWriter writer(testFile);
    EXPECT_TRUE(writer.open());

    PnLSnapshot snapshot;
    snapshot.snapshotId = 1;
    snapshot.timestamp = 12345;
    snapshot.clientId = 100;
    snapshot.netPosition = 50;
    snapshot.longQuantity = 100;
    snapshot.shortQuantity = 50;
    snapshot.realizedPnL = 1000;
    snapshot.unrealizedPnL = 500;
    snapshot.totalPnL = 1500;
    snapshot.markPrice = 110;
    snapshot.costBasis = 10000;
    snapshot.avgEntryPrice = 100;
    snapshot.tradeId = 42;

    EXPECT_TRUE(writer.writeSnapshot(snapshot));
    EXPECT_EQ(writer.getSnapshotCount(), 1);

    writer.close();

    // Verify file content
    std::ifstream file(testFile);
    std::string line;
    std::getline(file, line);  // header
    std::getline(file, line);  // data
    
    EXPECT_TRUE(line.find("1,12345,100,50,100,50,1000,500,1500,110,10000,100,42") != std::string::npos);
}

}  // namespace Testing
}  // namespace Mercury
