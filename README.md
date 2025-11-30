# <img src="https://github.com/user-attachments/assets/7ee41ddf-cf24-42fb-953b-d44c55e9f352" width="400">

> High-performance C++ Trading Engine with order book, matching logic, risk controls, and strategy layer.

## to-do
- [x] prototype order book using std::map and std::multimap 
- [x] basic csv parsing input handler (reading market orders): develop the initial function to ingest order data
- [x] google unit testing for inserts/deletes: write tests to ensure the order book correctly handles basic order entry and removal
- [x] matching engine (limit/market cancel/modify): implement the full engine logic, including limit orders, market orders, cancels, and modifications
- [x] implement custom data structures (linked list, hash map): required for eventual optimization, but practice is done here
- [x] handle edge cases (partial fills, empty book, invalid orders): integrate robust error handling for critical scenarios
- [x] unit tests for correctness & stress testing with sample datasets: validate the entire matching engine with thorough test coverage
- [ ] file i/o: read orders from csv, output trades to file: complete the data flow by ingesting orders and outputting trade results
- [ ] trade logger (csv output): log all executed trades to the required trades.csv file
- [ ] profile code with valgrind/gprof: identify bottlenecks and memory issues before optimization
- [ ] replace stl containers with custom data structures for speed: swap out std::map/std::multimap for custom, optimized structures (e.g., hash map)
- [ ] cache-friendly design (contiguous memory, pre-allocation): implement memory optimizations for improved performance
- [ ] risk manager (limits/checks exposures): add the risk layer to enforce position limits and exposures, outputting to riskevents.csv
- [ ] pnl module (realized + unrealized): implement position and profit/loss tracking, outputting to pnl.csv
- [ ] basic multithreading/concurrency: introduce concurrency where appropriate for further speed improvements
- [ ] implement strategy layer (market making, momentum): develop and integrate simulated strategies
- [ ] run backtests and record pnl: validate the strategies using simulated order flow
- [ ] organize repository and write documentation: (final polish) structure the repository (src/, include/, tests/, docs/) and create the readme.md