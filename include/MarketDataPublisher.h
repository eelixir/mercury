#pragma once

#include "MarketData.h"

#include <App.h>           // uWS::Loop, uWS::App
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>

namespace Mercury {

    // Bridges engine-thread MarketDataSink callbacks onto the uWS event-loop
    // thread, broadcasting JSON envelopes to the "market" pub/sub topic.
    //
    // Threading:
    //   - on*Event(...) may be called from any thread (typically the engine
    //     thread). Each call json-serializes synchronously then uses
    //     uWS::Loop::defer to hop onto the networking thread before publishing.
    //   - attach()/detach() must be called from the networking thread.
    class MarketDataPublisher : public MarketDataSink {
    public:
        static constexpr std::string_view TOPIC = "market";

        MarketDataPublisher() = default;
        ~MarketDataPublisher() override = default;

        MarketDataPublisher(const MarketDataPublisher&) = delete;
        MarketDataPublisher& operator=(const MarketDataPublisher&) = delete;

        // Bind to the networking thread's loop and app. Call before any
        // sink callbacks fire, after uWS::App is constructed on its thread.
        void attach(uWS::Loop* loop, uWS::App* app);

        // Release references; subsequent broadcasts become no-ops.
        void detach();

        // Connection counter maintained by the ServerApp WS handlers.
        void incrementConnections();
        void decrementConnections();
        uint64_t connectionCount() const;

        // MarketDataSink — serialize, then defer publish onto the network loop.
        void onBookDelta(const BookDelta& delta) override;
        void onTradeEvent(const TradeEvent& trade) override;
        void onStatsEvent(const StatsEvent& stats) override;
        void onPnLEvent(const PnLEvent& pnl) override;

    private:
        void broadcast(std::string message);

        mutable std::mutex mutex_;
        uWS::Loop* loop_ = nullptr;
        uWS::App* app_ = nullptr;
        std::atomic<uint64_t> connections_{0};
    };

}
