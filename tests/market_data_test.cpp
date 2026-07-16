#include <gtest/gtest.h>

#include "EngineService.h"
#include "MarketData.h"
#include "OrderBook.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>
#include <vector>

using namespace Mercury;

namespace {

Order makeLimitOrder(uint64_t id, Side side, int64_t price, uint64_t quantity, uint64_t clientId = 0) {
    Order order{};
    order.id = id;
    order.orderType = OrderType::Limit;
    order.side = side;
    order.price = price;
    order.quantity = quantity;
    order.clientId = clientId;
    order.tif = TimeInForce::GTC;
    return order;
}

class RecordingSink : public MarketDataSink {
public:
    void onBookDelta(const BookDelta& delta) override {
        std::lock_guard<std::mutex> lock(mutex);
        deltas.push_back(delta);
        sequences.push_back(delta.sequence);
    }

    void onTradeEvent(const TradeEvent& trade) override {
        std::lock_guard<std::mutex> lock(mutex);
        trades.push_back(trade);
        sequences.push_back(trade.sequence);
    }

    void onStatsEvent(const StatsEvent& stats) override {
        std::lock_guard<std::mutex> lock(mutex);
        statsEvents.push_back(stats);
        sequences.push_back(stats.sequence);
    }

    void onPnLEvent(const PnLEvent& pnl) override {
        std::lock_guard<std::mutex> lock(mutex);
        pnlEvents.push_back(pnl);
        sequences.push_back(pnl.sequence);
    }

    std::mutex mutex;
    std::vector<BookDelta> deltas;
    std::vector<TradeEvent> trades;
    std::vector<StatsEvent> statsEvents;
    std::vector<PnLEvent> pnlEvents;
    std::vector<uint64_t> sequences;
};

}  // namespace

TEST(MarketDataTest, OrderBookTopLevelsReturnsRequestedDepth) {
    OrderBook book;

    book.addOrder(makeLimitOrder(1, Side::Buy, 105, 10));
    book.addOrder(makeLimitOrder(2, Side::Buy, 104, 20));
    book.addOrder(makeLimitOrder(3, Side::Buy, 103, 30));
    book.addOrder(makeLimitOrder(4, Side::Sell, 106, 40));
    book.addOrder(makeLimitOrder(5, Side::Sell, 107, 50));

    const auto bids = book.getTopLevels(Side::Buy, 2);
    const auto asks = book.getTopLevels(Side::Sell, 1);

    ASSERT_EQ(bids.size(), 2u);
    EXPECT_EQ(bids[0].price, 105);
    EXPECT_EQ(bids[0].quantity, 10u);
    EXPECT_EQ(bids[1].price, 104);
    EXPECT_EQ(bids[1].quantity, 20u);

    ASSERT_EQ(asks.size(), 1u);
    EXPECT_EQ(asks[0].price, 106);
    EXPECT_EQ(asks[0].quantity, 40u);
}

TEST(MarketDataTest, EngineServicePublishesSequencedDeltasTradesStatsAndPnL) {
    EngineService service({"SIM"});
    RecordingSink sink;
    service.setMarketDataSink(&sink);
    service.start();

    auto resting = service.submitOrder("SIM", makeLimitOrder(1, Side::Sell, 101, 10, 2));
    auto aggressive = service.submitOrder("SIM", makeLimitOrder(2, Side::Buy, 101, 10, 1));
    auto snapshot = service.getSnapshot("SIM", 5);
    auto state = service.getState();

    service.stop();

    EXPECT_EQ(resting.status, ExecutionStatus::Resting);
    EXPECT_EQ(aggressive.status, ExecutionStatus::Filled);
    EXPECT_TRUE(snapshot.bids.empty());
    EXPECT_TRUE(snapshot.asks.empty());
    EXPECT_EQ(state.tradeCount, 1u);

    ASSERT_FALSE(sink.deltas.empty());
    ASSERT_EQ(sink.trades.size(), 1u);
    ASSERT_FALSE(sink.statsEvents.empty());
    ASSERT_EQ(sink.pnlEvents.size(), 2u);

    EXPECT_TRUE(std::is_sorted(sink.sequences.begin(), sink.sequences.end()));

    const auto& trade = sink.trades.front();
    EXPECT_EQ(trade.price, 101);
    EXPECT_EQ(trade.quantity, 10u);
    EXPECT_EQ(trade.buyClientId, 1u);
    EXPECT_EQ(trade.sellClientId, 2u);
}

TEST(MarketDataTest, EngineServicePublishesMarkToMarketPnLOnStatsUpdates) {
    EngineService service({"SIM"});
    RecordingSink sink;
    service.setMarketDataSink(&sink);
    service.start();

    ASSERT_EQ(service.submitOrder("SIM", makeLimitOrder(1, Side::Sell, 100, 10, 2)).status,
              ExecutionStatus::Resting);
    ASSERT_EQ(service.submitOrder("SIM", makeLimitOrder(2, Side::Buy, 100, 10, 1)).status,
              ExecutionStatus::Filled);

    service.submitOrder("SIM", makeLimitOrder(3, Side::Buy, 118, 5, 3));
    service.submitOrder("SIM", makeLimitOrder(4, Side::Sell, 122, 5, 4));

    service.stop();

    auto clientOne = std::find_if(sink.pnlEvents.rbegin(), sink.pnlEvents.rend(), [](const PnLEvent& event) {
        return event.clientId == 1;
    });

    ASSERT_NE(clientOne, sink.pnlEvents.rend());
    EXPECT_EQ(clientOne->netPosition, 10);
    EXPECT_EQ(clientOne->unrealizedPnL, 200);
    EXPECT_EQ(clientOne->totalPnL, 200);
}

TEST(MarketDataTest, LoopingReplayOffsetsModifyTargetsWithOrderIds) {
    const auto path = std::filesystem::temp_directory_path() /
        ("mercury-replay-modify-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
         ".csv");
    {
        std::ofstream out(path);
        ASSERT_TRUE(out.is_open());
        out << "id,timestamp,type,side,price,quantity,client_id\n";
        out << "1,0,limit,buy,100,10,1\n";
        out << "1,0,modify,buy,101,10,1\n";
    }

    EngineService service({"SIM"});
    service.start();
    ASSERT_TRUE(service.startReplay(path.string(), 1000.0, true, 5));

    bool sawMultipleLoops = false;
    L2Snapshot snapshot;
    for (int i = 0; i < 100; ++i) {
        const auto current = service.getSnapshot("SIM", 20);
        if (current.bids.size() == 1 &&
            current.bids.front().price == 101 &&
            current.bids.front().quantity >= 30) {
            sawMultipleLoops = true;
            snapshot = current;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    service.stopReplay();
    service.stop();
    std::error_code ec;
    std::filesystem::remove(path, ec);

    ASSERT_TRUE(sawMultipleLoops);
    ASSERT_EQ(snapshot.bids.size(), 1u);
    EXPECT_EQ(snapshot.bids.front().price, 101);
}
