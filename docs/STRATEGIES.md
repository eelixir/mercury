# Mercury Strategy Development Guide

This guide covers the trading strategy framework in Mercury, including built-in strategies and how to develop custom strategies.

## Overview

Mercury's strategy layer provides:
- **Base strategy framework** with lifecycle hooks
- **Market data feed** via `MarketTick` updates
- **Signal generation** with confidence levels
- **Automatic execution** through the matching engine
- **Position and P&L tracking** per strategy
- **Risk management integration**

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                       StrategyManager                            │
│  ┌──────────────────────────────────────────────────────────┐   │
│  │  onMarketTick(tick) → Strategy::onMarketTick()           │   │
│  │                      → executeSignals()                   │   │
│  │                      → updateMetrics()                    │   │
│  └──────────────────────────────────────────────────────────┘   │
│                              │                                   │
│  ┌──────────────────────────────────────────────────────────┐   │
│  │              Registered Strategies                        │   │
│  │  ┌────────────────┐  ┌────────────────┐  ┌────────────┐  │   │
│  │  │ MarketMaking   │  │ Momentum       │  │ Custom...  │  │   │
│  │  │ Strategy       │  │ Strategy       │  │            │  │   │
│  │  └────────────────┘  └────────────────┘  └────────────┘  │   │
│  └──────────────────────────────────────────────────────────┘   │
└──────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                        MatchingEngine                            │
│  Executes orders generated from strategy signals                 │
└─────────────────────────────────────────────────────────────────┘
```

---

## Quick Start

### Running Built-in Strategies

```bash
# Build Mercury
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build

# Run strategy simulation demo
./build/mercury --strategies
```

### Basic Usage in Code

```cpp
#include "StrategyManager.h"

// Create engine and manager
MatchingEngine engine;
StrategyManager manager(engine);

// Add a market making strategy
MarketMakingConfig mmConfig;
mmConfig.name = "MyMarketMaker";
mmConfig.quoteQuantity = 100;
mmConfig.minSpread = 2;
mmConfig.maxInventory = 500;
manager.addStrategy(std::make_unique<MarketMakingStrategy>(mmConfig));

// Feed market data
MarketTick tick;
tick.bidPrice = 99;
tick.askPrice = 101;
tick.bidQuantity = 500;
tick.askQuantity = 500;
tick.timestamp = 1;

manager.onMarketTick(tick);

// Print performance summary
manager.printSummary();
```

---

## Core Components

### MarketTick

Represents a single market data update:

```cpp
struct MarketTick {
    uint64_t timestamp = 0;
    int64_t bidPrice = 0;          // Best bid price
    int64_t askPrice = 0;          // Best ask price
    uint64_t bidQuantity = 0;      // Quantity at best bid
    uint64_t askQuantity = 0;      // Quantity at best ask
    int64_t lastTradePrice = 0;    // Last traded price
    uint64_t lastTradeQuantity = 0;
    uint64_t totalVolume = 0;      // Total traded volume
    
    // Helper methods
    int64_t midPrice() const;      // (bid + ask) / 2
    int64_t spread() const;        // ask - bid
    bool isValid() const;
};
```

### StrategySignal

Output from strategy indicating desired action:

```cpp
enum class SignalType {
    None,           // No action
    Buy,            // Open/increase long position
    Sell,           // Open/increase short position
    CloseLong,      // Close long position
    CloseShort,     // Close short position
    CancelBids,     // Cancel all bid orders
    CancelAsks,     // Cancel all ask orders
    CancelAll       // Cancel all orders
};

struct StrategySignal {
    SignalType type = SignalType::None;
    int64_t price = 0;            // Target price (0 = market order)
    uint64_t quantity = 0;        // Desired quantity
    double confidence = 0.0;      // Signal confidence (0.0 - 1.0)
    std::string reason;           // Human-readable reason
};
```

### StrategyState

Internal state tracking for each strategy:

```cpp
struct StrategyState {
    // Position tracking
    int64_t netPosition = 0;
    int64_t longPosition = 0;
    int64_t shortPosition = 0;
    
    // P&L tracking
    int64_t realizedPnL = 0;
    int64_t unrealizedPnL = 0;
    int64_t totalPnL = 0;
    
    // Order tracking
    uint64_t activeOrders = 0;
    uint64_t activeBidOrders = 0;
    uint64_t activeAskOrders = 0;
    
    // Market state
    MarketTick lastTick;
    uint64_t ticksProcessed = 0;
};
```

---

## Built-in Strategies

### Market Making Strategy

Provides liquidity by continuously quoting bid and ask prices, profiting from the bid-ask spread.

#### Configuration

```cpp
struct MarketMakingConfig {
    std::string name = "MarketMaking";
    
    // Quote parameters
    uint64_t quoteQuantity = 100;     // Size per quote
    int64_t minSpread = 1;            // Minimum bid-ask spread
    int64_t maxSpread = 10;           // Maximum spread
    
    // Risk limits
    int64_t maxInventory = 1000;      // Maximum position (long or short)
    
    // Inventory management
    double fadePerUnit = 0.01;        // Spread adjustment per inventory unit
    double inventorySkewFactor = 1.0; // How aggressively to skew quotes
    
    // Quote refresh
    int64_t minQuoteRefresh = 2;      // Minimum price move to refresh
    bool cancelOnSpreadWiden = true;  // Cancel quotes if spread widens
    
    // Client ID for orders
    uint32_t clientId = 0;
};
```

#### Example

```cpp
MarketMakingConfig config;
config.name = "AggressiveMM";
config.quoteQuantity = 50;
config.minSpread = 1;
config.maxSpread = 5;
config.maxInventory = 300;
config.fadePerUnit = 0.02;  // Widen spread faster as inventory grows

auto mm = std::make_unique<MarketMakingStrategy>(config);
manager.addStrategy(std::move(mm));
```

#### How It Works

1. **Quote Generation**: Places bid and ask orders at configurable distance from mid-price
2. **Inventory Skew**: As position grows, widens spread on the risky side
3. **Fade Mechanism**: Adjusts prices to encourage mean-reversion of inventory
4. **Quote Refresh**: Updates quotes when market moves beyond threshold

```
Initial State:
  Mid = 100, Spread = 2
  Bid Quote: 99, Ask Quote: 101

After accumulating +200 inventory (long):
  Bid Quote: 98 (lower to discourage more buying)
  Ask Quote: 100 (lower to encourage selling)
```

---

### Momentum Strategy

Trend-following strategy using technical indicators to identify and ride price trends.

#### Configuration

```cpp
struct MomentumConfig {
    std::string name = "Momentum";
    
    // Position sizing
    uint64_t baseQuantity = 100;       // Base order size
    uint64_t maxPosition = 1000;       // Maximum position
    
    // Moving average parameters
    size_t shortPeriod = 5;            // Short MA period
    size_t longPeriod = 20;            // Long MA period
    bool useEMA = true;                // Use EMA vs SMA
    
    // Entry/Exit thresholds
    double entryThreshold = 0.02;      // 2% momentum for entry
    double exitThreshold = 0.005;      // Exit threshold
    size_t confirmationBars = 2;       // Bars to confirm signal
    
    // Risk management
    double stopLossPercent = 0.05;     // 5% stop loss
    double takeProfitPercent = 0.10;   // 10% take profit
    bool useTrailingStop = true;
    double trailingStopPercent = 0.03;
    
    // Filters
    bool useTrendFilter = true;        // Only trade with trend
    bool requireVolumeConfirm = false; // Require volume confirmation
    uint64_t minVolume = 1000;
    
    // Order type
    bool useMarketOrders = true;       // Use market vs limit orders
    int64_t limitOffset = 1;           // Offset for limit orders
    
    uint32_t clientId = 0;
};
```

#### Example

```cpp
MomentumConfig config;
config.name = "TrendFollower";
config.baseQuantity = 30;
config.shortPeriod = 5;
config.longPeriod = 12;
config.entryThreshold = 0.01;    // 1% momentum threshold
config.stopLossPercent = 0.03;   // 3% stop loss
config.useTrendFilter = false;   // Trade both directions

auto mom = std::make_unique<MomentumStrategy>(config);
manager.addStrategy(std::move(mom));
```

#### Technical Indicators

The momentum strategy uses multiple indicators:

**Simple Moving Average (SMA)**
```
SMA = (P1 + P2 + ... + Pn) / n
```

**Exponential Moving Average (EMA)**
```
EMA_today = Price_today * k + EMA_yesterday * (1 - k)
where k = 2 / (period + 1)
```

**MACD (Moving Average Convergence Divergence)**
```
MACD Line = EMA(12) - EMA(26)
Signal Line = EMA(9) of MACD Line
Histogram = MACD Line - Signal Line
```

**RSI (Relative Strength Index)**
```
RSI = 100 - (100 / (1 + RS))
where RS = Average Gain / Average Loss
```

#### Signal Generation

| Condition | Signal |
|-----------|--------|
| Short MA > Long MA + threshold | **BUY** |
| Short MA < Long MA - threshold | **SELL** |
| MACD crosses above signal line | **BUY confirmation** |
| MACD crosses below signal line | **SELL confirmation** |
| RSI < 30 (oversold) | **BUY filter** |
| RSI > 70 (overbought) | **SELL filter** |
| Price hits stop loss | **EXIT** |
| Price hits take profit | **EXIT** |

---

## Custom Strategy Development

### Creating a Custom Strategy

Inherit from the `Strategy` base class:

```cpp
#include "Strategy.h"

class MyCustomStrategy : public Strategy {
public:
    struct Config : public StrategyConfig {
        double myParameter = 0.5;
        int myThreshold = 10;
    };

    explicit MyCustomStrategy(const Config& config)
        : config_(config)
    {
        config_.name = config.name;
    }

    std::vector<StrategySignal> onMarketTick(const MarketTick& tick) override {
        std::vector<StrategySignal> signals;
        
        // Update internal state
        state_.lastTick = tick;
        state_.ticksProcessed++;
        
        // Your trading logic here
        if (shouldBuy(tick)) {
            StrategySignal signal;
            signal.type = SignalType::Buy;
            signal.quantity = 100;
            signal.price = tick.askPrice;  // Limit order at ask
            signal.confidence = 0.8;
            signal.reason = "Custom buy signal";
            signals.push_back(signal);
        }
        
        return signals;
    }

    void onOrderFilled(uint64_t orderId, Side side, 
                       int64_t price, uint64_t quantity) override {
        // Update position tracking
        if (side == Side::Buy) {
            state_.netPosition += quantity;
        } else {
            state_.netPosition -= quantity;
        }
    }

    void onTradeExecuted(const Trade& trade) override {
        // React to market trades (including other participants)
    }

    std::string getName() const override { return config_.name; }
    const StrategyState& getState() const override { return state_; }
    const StrategyConfig& getConfig() const override { return config_; }

private:
    bool shouldBuy(const MarketTick& tick) {
        // Your custom logic
        return false;
    }

    Config config_;
    StrategyState state_;
};
```

### Registering Custom Strategy

```cpp
MyCustomStrategy::Config config;
config.name = "MyStrategy";
config.myParameter = 0.75;

StrategyManager manager(engine);
manager.addStrategy(std::make_unique<MyCustomStrategy>(config));
```

---

## Strategy Manager

The `StrategyManager` orchestrates multiple strategies:

### Features

- Manages strategy lifecycle
- Feeds market data to all strategies
- Executes signals through matching engine
- Tracks orders per strategy
- Updates strategy state on fills
- Collects performance metrics

### Configuration

```cpp
struct StrategyManagerConfig {
    bool enableRiskChecks = true;      // Run risk checks before submitting
    bool enablePnLTracking = true;     // Track P&L per strategy
    bool logSignals = false;           // Log generated signals
    bool logExecutions = true;         // Log order executions
    uint64_t baseOrderId = 1000000;    // Starting order ID
    uint64_t clientIdOffset = 100;     // Client ID offset per strategy
};
```

### API

```cpp
class StrategyManager {
public:
    // Strategy management
    void addStrategy(std::unique_ptr<Strategy> strategy);
    void removeStrategy(const std::string& name);
    size_t getStrategyCount() const;
    
    // Market data
    void onMarketTick(const MarketTick& tick);
    
    // Trade notifications
    void onTradeExecuted(const Trade& trade);
    
    // Metrics
    StrategyMetrics getMetrics(const std::string& name) const;
    void printSummary() const;
    
    // Control
    void pauseStrategy(const std::string& name);
    void resumeStrategy(const std::string& name);
    void cancelAllOrders(const std::string& name);
};
```

### Performance Metrics

```cpp
struct StrategyMetrics {
    std::string strategyName;
    
    // Order statistics
    uint64_t ordersSubmitted = 0;
    uint64_t ordersFilled = 0;
    uint64_t ordersPartialFilled = 0;
    uint64_t ordersCancelled = 0;
    uint64_t ordersRejected = 0;
    
    // Trade statistics
    uint64_t totalTrades = 0;
    uint64_t totalVolume = 0;
    
    // P&L
    int64_t realizedPnL = 0;
    int64_t unrealizedPnL = 0;
    int64_t totalPnL = 0;
    
    // Position
    int64_t netPosition = 0;
    int64_t maxPosition = 0;
    
    // Timing
    uint64_t signalsGenerated = 0;
};
```

---

## Best Practices

### 1. Position Management

Always track and limit position:

```cpp
if (std::abs(state_.netPosition) >= config_.maxPosition) {
    return {};  // No new signals if at limit
}
```

### 2. Risk Controls

Use stop-losses and take-profits:

```cpp
if (state_.netPosition > 0) {
    double pnlPercent = (currentPrice - entryPrice_) / entryPrice_;
    if (pnlPercent <= -config_.stopLossPercent) {
        // Generate exit signal
    }
}
```

### 3. State Validation

Validate market data before using:

```cpp
if (!tick.isValid() || tick.spread() <= 0) {
    return {};
}
```

### 4. Confidence Levels

Use confidence to size positions:

```cpp
signal.quantity = static_cast<uint64_t>(
    config_.baseQuantity * signal.confidence
);
```

### 5. Reason Tracking

Always provide reasons for signals:

```cpp
signal.reason = "MACD bullish crossover, RSI=" + 
                std::to_string(rsi_) + ", Mom=" + 
                std::to_string(momentum * 100) + "%";
```

---

## Testing Strategies

### Unit Testing

```cpp
TEST(MomentumStrategyTest, GeneratesBuySignalOnUptrend) {
    MomentumConfig config;
    config.shortPeriod = 3;
    config.longPeriod = 5;
    config.entryThreshold = 0.01;
    config.useTrendFilter = false;
    
    MomentumStrategy strategy(config);
    
    // Build price history (uptrend)
    std::vector<int64_t> prices = {100, 101, 102, 103, 105, 107, 110};
    
    std::vector<StrategySignal> signals;
    for (size_t i = 0; i < prices.size(); i++) {
        MarketTick tick;
        tick.bidPrice = prices[i] - 1;
        tick.askPrice = prices[i] + 1;
        tick.lastTradePrice = prices[i];
        tick.timestamp = i + 1;
        
        auto newSignals = strategy.onMarketTick(tick);
        signals.insert(signals.end(), newSignals.begin(), newSignals.end());
    }
    
    // Should generate at least one buy signal
    bool hasBuySignal = std::any_of(signals.begin(), signals.end(),
        [](const auto& s) { return s.type == SignalType::Buy; });
    EXPECT_TRUE(hasBuySignal);
}
```

### Running Strategy Tests

```bash
./build/mercury_tests --gtest_filter="*Strategy*"
```

---

## Example: Combined Strategy Session

```cpp
#include "StrategyManager.h"
#include "StrategyDemo.h"

int main() {
    MatchingEngine engine;
    StrategyManager manager(engine);
    
    // Market making strategy
    MarketMakingConfig mmConfig;
    mmConfig.name = "MM";
    mmConfig.quoteQuantity = 50;
    mmConfig.maxInventory = 300;
    manager.addStrategy(std::make_unique<MarketMakingStrategy>(mmConfig));
    
    // Momentum strategy
    MomentumConfig momConfig;
    momConfig.name = "Trend";
    momConfig.baseQuantity = 30;
    momConfig.shortPeriod = 5;
    momConfig.longPeriod = 12;
    manager.addStrategy(std::make_unique<MomentumStrategy>(momConfig));
    
    // Simulate market
    MarketSimulator sim(100);  // Start at price 100
    
    for (int tick = 1; tick <= 100; tick++) {
        MarketTick marketTick = sim.nextTick(/* uptrend */ tick < 50);
        manager.onMarketTick(marketTick);
    }
    
    manager.printSummary();
    return 0;
}
```

---

## Performance Considerations

1. **Minimize allocations** in `onMarketTick()` - called on every tick
2. **Use `reserve()`** for signal vectors if count is predictable
3. **Cache calculations** that don't change every tick
4. **Profile with** `--DMERCURY_ENABLE_PROFILING=ON`

---

## See Also

- [README.md](../README.md) - Project overview
- [PROFILING.md](PROFILING.md) - Performance analysis
- `include/Strategy.h` - Base strategy interface
- `include/MarketMakingStrategy.h` - Market making implementation
- `include/MomentumStrategy.h` - Momentum strategy implementation
- `tests/strategy_test.cpp` - Strategy unit tests
