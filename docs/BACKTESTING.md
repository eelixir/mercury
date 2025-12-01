# Mercury Backtesting System

## Overview

The Mercury backtesting system provides a comprehensive framework for validating trading strategies using simulated order flow. It includes:

- **Realistic Order Flow Simulation**: Multiple market patterns (trending, mean-reverting, high volatility, etc.)
- **Strategy Validation**: Test market making, momentum, and custom strategies
- **Complete P&L Tracking**: FIFO-based realized P&L and mark-to-market unrealized P&L
- **CSV Output**: Detailed logs for trades, orders, and P&L snapshots
- **Performance Metrics**: Win rate, fill rate, drawdown, Sharpe ratio, and more

## Quick Start

### Running Backtests

Run all backtest demos:
```bash
cd build
./mercury --backtest
```

Run specific backtest:
```bash
./mercury --backtest mm          # Market making strategy
./mercury --backtest momentum    # Momentum strategy
./mercury --backtest multi       # Multiple strategies
./mercury --backtest compare     # Market condition comparison
./mercury --backtest stress      # Stress test
```

### Output Files

Backtests generate several output files in the `build/` directory:

- `*_backtest_report.txt` - Human-readable summary with key metrics
- `backtest_trades.csv` - All executed trades with timestamps
- `backtest_orders.csv` - All submitted orders and their outcomes
- `pnl.csv` - P&L snapshots after each trade

## Architecture

### Core Components

1. **Backtester**: Main backtesting engine that orchestrates the entire process
2. **OrderFlowSimulator**: Generates realistic market orders based on configurable patterns
3. **BacktestConfig**: Configuration for time parameters, order flow, and output
4. **BacktestReport**: Comprehensive results with strategy-level metrics

### Order Flow Patterns

The system supports multiple market regimes:

- **Random**: Random walk with normal distribution
- **Trending**: Directional price movement with drift
- **MeanReverting**: Price oscillates around mean
- **HighVolatility**: Large random price swings
- **LowVolatility**: Tight range-bound movement
- **MomentumBurst**: Sudden strong directional moves
- **Choppy**: Frequent trend reversals

## Usage Examples

### Example 1: Simple Market Making Backtest

```cpp
#include "Backtester.h"
#include "MarketMakingStrategy.h"

// Configure backtest
Mercury::BacktestConfig config;
config.numTicks = 1000;              // 1000 time steps
config.warmupTicks = 50;             // 50 ticks warmup
config.outputDir = "backtest_results";

// Configure order flow
config.orderFlow.pattern = Mercury::OrderFlowPattern::MeanReverting;
config.orderFlow.startPrice = 100;
config.orderFlow.ordersPerTick = 10;
config.orderFlow.volatility = 0.015; // 1.5% volatility

// Create backtester
Mercury::Backtester backtester(config);

// Add strategy
Mercury::MarketMakingConfig mmConfig;
mmConfig.minSpread = 2;
mmConfig.maxSpread = 8;
mmConfig.quoteQuantity = 50;
mmConfig.maxInventory = 500;
backtester.addStrategy(std::make_unique<Mercury::MarketMakingStrategy>(mmConfig));

// Run backtest
auto report = backtester.run();

// Write report
backtester.writeReport(report, "mm_backtest_report.txt");

// Access metrics
for (const auto& metrics : report.strategyMetrics) {
    std::cout << "Strategy: " << metrics.strategyName << "\n";
    std::cout << "Total P&L: " << metrics.totalPnL << "\n";
    std::cout << "Win Rate: " << (metrics.winRate * 100) << "%\n";
}
```

### Example 2: Momentum Strategy Backtest

```cpp
// Configure for trending market
config.orderFlow.pattern = Mercury::OrderFlowPattern::Trending;
config.orderFlow.trendStrength = 0.002; // 0.2% drift per tick

// Create backtester
Mercury::Backtester backtester(config);

// Add momentum strategy
Mercury::MomentumConfig momConfig;
momConfig.shortPeriod = 5;
momConfig.longPeriod = 20;
momConfig.entryThreshold = 0.02;     // Enter on 2% momentum
momConfig.stopLossPct = 0.03;        // 3% stop loss
momConfig.takeProfitPct = 0.06;      // 6% take profit
momConfig.maxPosition = 100;         // Conservative position limit
backtester.addStrategy(std::make_unique<Mercury::MomentumStrategy>(momConfig));

// Run and analyze
auto report = backtester.run();
```

### Example 3: Multi-Strategy Comparison

```cpp
Mercury::Backtester backtester(config);

// Add multiple strategies
backtester.addStrategy(std::make_unique<Mercury::MarketMakingStrategy>(mmConfig));
backtester.addStrategy(std::make_unique<Mercury::MomentumStrategy>(momConfig));

// Run backtest
auto report = backtester.run();

// Compare performance
for (const auto& metrics : report.strategyMetrics) {
    std::cout << metrics.strategyName << ": " 
              << "P&L=" << metrics.totalPnL 
              << ", Trades=" << metrics.totalTrades << "\n";
}
```

## Configuration Options

### BacktestConfig

```cpp
struct BacktestConfig {
    uint64_t numTicks = 1000;           // Number of time steps
    uint64_t tickDurationMs = 100;      // Simulated time per tick
    uint64_t warmupTicks = 50;          // Warmup before tracking
    
    OrderFlowConfig orderFlow;          // Order flow configuration
    RiskLimits riskLimits;              // Risk limits for all strategies
    
    std::string outputDir = "backtest_results";
    bool writeTradeLog = true;
    bool writePnLLog = true;
    bool writeOrderLog = true;
    bool verbose = false;
};
```

### OrderFlowConfig

```cpp
struct OrderFlowConfig {
    OrderFlowPattern pattern = OrderFlowPattern::Random;
    
    int64_t startPrice = 100;           // Starting price
    uint64_t ordersPerTick = 5;         // Orders per tick
    double volatility = 0.02;           // Price volatility (2%)
    
    uint64_t minOrderSize = 10;         // Min order quantity
    uint64_t maxOrderSize = 200;        // Max order quantity
    double marketOrderRatio = 0.3;      // % market orders
    
    // Pattern-specific
    double trendStrength = 0.001;       // Trend drift per tick
    double meanReversionSpeed = 0.05;   // Mean reversion force
    double burstProbability = 0.05;     // Momentum burst probability
    double reversalProbability = 0.1;   // Reversal probability
    
    int64_t minSpread = 2;              // Min bid-ask spread
    int64_t maxSpread = 10;             // Max spread
    uint32_t seed = 42;                 // RNG seed (0 = random)
};
```

## Performance Metrics

The backtester calculates comprehensive metrics for each strategy:

### P&L Metrics
- Total P&L (realized + unrealized)
- Realized P&L from closed positions
- Unrealized P&L on open positions
- Maximum drawdown
- Peak P&L

### Trade Metrics
- Total trades
- Winning trades
- Losing trades
- Total volume
- Average trade size
- Win rate

### Position Metrics
- Maximum position size
- Final position
- Average position

### Order Metrics
- Orders submitted
- Orders filled
- Orders rejected
- Fill rate

### Risk Metrics
- Maximum loss
- Sharpe ratio
- Sortino ratio
- Profit factor

## CSV Output Format

### backtest_trades.csv
```
trade_id,timestamp,buy_order_id,sell_order_id,price,quantity
1,42,100001,100005,100,50
2,43,100002,100006,101,75
```

### backtest_orders.csv
```
order_id,timestamp,type,side,price,quantity,status,filled_qty
100001,42,limit,buy,100,100,filled,100
100002,42,market,sell,0,50,filled,50
```

### pnl.csv
```
snapshot_id,timestamp,client_id,net_position,long_qty,short_qty,realized_pnl,unrealized_pnl,total_pnl,mark_price,cost_basis,avg_entry_price,trade_id
1,42,100,50,50,0,0,25,25,101,5000,100,1
2,43,100,0,0,0,50,0,50,102,0,0,2
```

## Best Practices

1. **Warmup Period**: Use warmup ticks to let strategies build price history
2. **Realistic Parameters**: Match volatility and order flow to real markets
3. **Multiple Runs**: Run with different seeds to test robustness
4. **Market Regimes**: Test strategies across different market conditions
5. **Risk Limits**: Set appropriate position and loss limits

## Advanced Usage

### Custom Order Flow Pattern

Create a custom order flow by extending `OrderFlowSimulator` or by configuring existing patterns:

```cpp
config.orderFlow.pattern = OrderFlowPattern::Trending;
config.orderFlow.volatility = 0.03;        // 3% daily vol
config.orderFlow.trendStrength = 0.0015;   // Slight upward drift
config.orderFlow.ordersPerTick = 20;       // High liquidity
config.orderFlow.marketOrderRatio = 0.25;  // 25% takers
```

### Strategy Parameter Optimization

Run multiple backtests with different strategy parameters to find optimal settings:

```cpp
for (double threshold = 0.01; threshold <= 0.03; threshold += 0.005) {
    Mercury::MomentumConfig config;
    config.entryThreshold = threshold;
    
    Mercury::Backtester backtester(backtestConfig);
    backtester.addStrategy(std::make_unique<Mercury::MomentumStrategy>(config));
    
    auto report = backtester.run();
    // Analyze report.strategyMetrics[0].totalPnL
}
```

### Market Condition Analysis

Compare strategy performance across different market regimes:

```cpp
std::vector<OrderFlowPattern> patterns = {
    OrderFlowPattern::Trending,
    OrderFlowPattern::MeanReverting,
    OrderFlowPattern::Choppy,
    OrderFlowPattern::HighVolatility
};

for (const auto& pattern : patterns) {
    config.orderFlow.pattern = pattern;
    Mercury::Backtester backtester(config);
    // Add strategy and run...
}
```

## Troubleshooting

### Issue: Strategy not generating signals
- Check warmup period is sufficient for indicator calculation
- Verify thresholds are appropriate for volatility level
- Enable verbose mode to see signal generation

### Issue: Poor P&L performance
- Review strategy parameters vs market conditions
- Check risk limits aren't too restrictive
- Analyze trade log to understand entry/exit quality

### Issue: High fill rejection rate
- Review risk limits (may be too tight)
- Check position limits vs order sizes
- Verify price limits for limit orders

## Performance Considerations

The backtester is optimized for speed:

- Single-threaded order book (required for deterministic execution)
- Efficient order matching using price-time priority
- Minimal memory allocation during simulation
- Async I/O for CSV output (optional)

Typical throughput: **50,000+ ticks/second** on modern hardware

## Future Enhancements

Potential improvements:

- [ ] Multi-asset backtesting
- [ ] Transaction cost modeling
- [ ] Slippage simulation
- [ ] Market impact modeling
- [ ] Portfolio-level risk management
- [ ] Walk-forward optimization
- [ ] Monte Carlo simulation
- [ ] Custom performance metrics

## See Also

- [STRATEGIES.md](STRATEGIES.md) - Strategy implementation guide
- [PROFILING.md](PROFILING.md) - Performance profiling guide
- Strategy header files for configuration options
