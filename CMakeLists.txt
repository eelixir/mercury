cmake_minimum_required(VERSION 3.10)

project(Mercury VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# ============================================================================
# THREADING SUPPORT
# ============================================================================
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

# ============================================================================
# BUILD OPTIONS
# ============================================================================
option(MERCURY_ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(MERCURY_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)
option(MERCURY_ENABLE_PROFILING "Enable built-in profiling instrumentation" OFF)
option(MERCURY_BUILD_BENCHMARKS "Build Google Benchmark micro-benchmarks" OFF)

# ============================================================================
# COMPILER FLAGS
# ============================================================================

# Release with debug info for profiling
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-O2 -g -DNDEBUG")

# AddressSanitizer - detects memory errors (use-after-free, buffer overflow)
if(MERCURY_ENABLE_ASAN)
    message(STATUS "AddressSanitizer enabled")
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address)
endif()

# UndefinedBehaviorSanitizer - detects undefined behavior
if(MERCURY_ENABLE_UBSAN)
    message(STATUS "UndefinedBehaviorSanitizer enabled")
    add_compile_options(-fsanitize=undefined)
    add_link_options(-fsanitize=undefined)
endif()

# Built-in profiling instrumentation
if(MERCURY_ENABLE_PROFILING)
    message(STATUS "Built-in profiling enabled")
    add_compile_definitions(MERCURY_PROFILING_ENABLED=1)
endif()

# Add the main executable
add_executable(mercury
    src/main.cpp
    src/OrderBook.cpp
    src/CSVParser.cpp
    src/MatchingEngine.cpp
    src/TradeWriter.cpp
    src/RiskManager.cpp
    src/PnLTracker.cpp
    include/OrderBook.h
    include/Order.h
    include/CSVParser.h
    include/MatchingEngine.h
    include/TradeWriter.h
    include/RiskManager.h
    include/PnLTracker.h
)

# Include the src and include directories so headers can be found easily
target_include_directories(mercury PUBLIC src include)
target_link_libraries(mercury PRIVATE Threads::Threads)

# Create a library for testable code (excluding main.cpp)
add_library(mercury_lib
    src/OrderBook.cpp
    src/CSVParser.cpp
    src/MatchingEngine.cpp
    src/TradeWriter.cpp
    src/RiskManager.cpp
    src/PnLTracker.cpp
)
target_include_directories(mercury_lib PUBLIC include)
target_link_libraries(mercury_lib PUBLIC Threads::Threads)

# Header-only components (listed for IDE visibility)
set(MERCURY_HEADERS
    include/Order.h
    include/OrderBook.h
    include/OrderNode.h
    include/PriceLevel.h
    include/HashMap.h
    include/IntrusiveList.h
    include/ObjectPool.h
    include/Profiler.h
    include/CSVParser.h
    include/MatchingEngine.h
    include/TradeWriter.h
    include/RiskManager.h
    include/PnLTracker.h
    include/ThreadPool.h
    include/AsyncWriter.h
    include/ConcurrentMatchingEngine.h
    include/Strategy.h
    include/MarketMakingStrategy.h
    include/MomentumStrategy.h
    include/StrategyManager.h
    include/StrategyDemo.h
    include/Backtester.h
    include/BacktestDemo.h
)
target_sources(mercury_lib PRIVATE ${MERCURY_HEADERS})

# Google Test setup - fetch from GitHub
include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.15.2
)
# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()

# Add test executable
add_executable(mercury_tests
    tests/data_structures_test.cpp
    tests/matching_engine_test.cpp
    tests/order_book_test.cpp
    tests/order_validation_test.cpp
    tests/stress_test.cpp
    tests/risk_manager_test.cpp
    tests/pnl_tracker_test.cpp
    tests/concurrency_test.cpp
    tests/strategy_test.cpp
)

target_link_libraries(mercury_tests
    mercury_lib
    GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(mercury_tests)

# ============================================================================
# GOOGLE BENCHMARK (Micro-benchmarking)
# ============================================================================
if(MERCURY_BUILD_BENCHMARKS)
    message(STATUS "Building micro-benchmarks")
    
    FetchContent_Declare(
        googlebenchmark
        GIT_REPOSITORY https://github.com/google/benchmark.git
        GIT_TAG        v1.9.1
    )
    
    set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_WERROR OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googlebenchmark)
    
    add_executable(mercury_benchmarks
        benchmarks/benchmark_main.cpp
    )
    
    target_link_libraries(mercury_benchmarks
        mercury_lib
        benchmark::benchmark
    )
    
    target_include_directories(mercury_benchmarks PRIVATE include)
endif()
