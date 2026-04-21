cmake_minimum_required(VERSION 3.10)

if(POLICY CMP0169)
    cmake_policy(SET CMP0169 OLD)
endif()

project(Mercury VERSION 1.0 LANGUAGES C CXX)

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
option(MERCURY_BUILD_SERVER "Build localhost HTTP/WebSocket server mode" ON)

# ============================================================================
# COMPILER FLAGS
# ============================================================================

# Compiler-specific settings
if(MSVC)
    # MSVC-specific flags
    set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "/O2 /Zi /DNDEBUG")
    add_compile_options(/W4)  # High warning level
    
    # AddressSanitizer (MSVC 2019 16.9+ required)
    if(MERCURY_ENABLE_ASAN)
        message(STATUS "AddressSanitizer enabled (MSVC)")
        add_compile_options(/fsanitize=address)
        # Note: MSVC ASAN doesn't support all features of GCC/Clang ASAN
    endif()
    
    # UBSAN not supported on MSVC - warn user
    if(MERCURY_ENABLE_UBSAN)
        message(WARNING "UndefinedBehaviorSanitizer is not supported on MSVC. Ignoring MERCURY_ENABLE_UBSAN.")
    endif()
else()
    # GCC/Clang flags
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
endif()

# Built-in profiling instrumentation
if(MERCURY_ENABLE_PROFILING)
    message(STATUS "Built-in profiling enabled")
    add_compile_definitions(MERCURY_PROFILING_ENABLED=1)
endif()

# Google Test setup - fetch from GitHub
include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.15.2
)
# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.12.0
)

set(LIBUV_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(LIBUV_BUILD_BENCH OFF CACHE BOOL "" FORCE)
set(LIBUV_BUILD_SHARED OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    libuv
    GIT_REPOSITORY https://github.com/libuv/libuv.git
    GIT_TAG        v1.52.1
)

if(MERCURY_BUILD_SERVER)
    FetchContent_Declare(
        uwebsockets
        URL https://github.com/uNetworking/uWebSockets/archive/refs/tags/v20.77.0.zip
        TLS_VERIFY FALSE
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_Declare(
        usockets
        URL https://github.com/uNetworking/uSockets/archive/ae52d1edea0182de655bde4d445858b3b02d6219.zip
        TLS_VERIFY FALSE
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
endif()

FetchContent_MakeAvailable(googletest)
FetchContent_MakeAvailable(nlohmann_json)
if(MERCURY_BUILD_SERVER)
    FetchContent_MakeAvailable(libuv)

    if(WIN32 AND CMAKE_C_COMPILER_ID STREQUAL "GNU")
        set(LIBUV_WIN_UTIL "${libuv_SOURCE_DIR}/src/win/util.c")
        file(READ "${LIBUV_WIN_UTIL}" LIBUV_WIN_UTIL_CONTENT)
        string(REPLACE "&(cpu_info->model)" "(char **) &(cpu_info->model)" LIBUV_WIN_UTIL_CONTENT "${LIBUV_WIN_UTIL_CONTENT}")
        file(WRITE "${LIBUV_WIN_UTIL}" "${LIBUV_WIN_UTIL_CONTENT}")
    endif()
endif()

if(MERCURY_BUILD_SERVER)
    FetchContent_GetProperties(uwebsockets)
    if(NOT uwebsockets_POPULATED)
        FetchContent_Populate(uwebsockets)
    endif()

    FetchContent_GetProperties(usockets)
    if(NOT usockets_POPULATED)
        FetchContent_Populate(usockets)
    endif()

    add_library(usockets STATIC
        ${usockets_SOURCE_DIR}/src/bsd.c
        ${usockets_SOURCE_DIR}/src/context.c
        ${usockets_SOURCE_DIR}/src/loop.c
        ${usockets_SOURCE_DIR}/src/socket.c
        ${usockets_SOURCE_DIR}/src/udp.c
        ${usockets_SOURCE_DIR}/src/eventing/libuv.c
    )
    target_include_directories(usockets PUBLIC ${usockets_SOURCE_DIR}/src)
    target_compile_definitions(usockets PUBLIC LIBUS_USE_LIBUV LIBUS_NO_SSL)
    target_link_libraries(usockets PUBLIC libuv::libuv)
    set_target_properties(usockets PROPERTIES C_STANDARD 11)

    if(WIN32)
        target_compile_definitions(usockets PRIVATE WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00)
    endif()

    add_library(uwebsockets INTERFACE)
    target_include_directories(uwebsockets INTERFACE
        ${uwebsockets_SOURCE_DIR}/src
        ${usockets_SOURCE_DIR}/src
    )
    target_compile_definitions(uwebsockets INTERFACE UWS_NO_ZLIB)
    target_link_libraries(uwebsockets INTERFACE usockets)
endif()

set(MERCURY_CORE_SOURCES
    src/OrderBook.cpp
    src/CSVParser.cpp
    src/MatchingEngine.cpp
    src/TradeWriter.cpp
    src/RiskManager.cpp
    src/PnLTracker.cpp
    src/EngineService.cpp
)

# Add the main executable
add_executable(mercury
    src/main.cpp
    ${MERCURY_CORE_SOURCES}
)

if(MERCURY_BUILD_SERVER)
    target_sources(mercury PRIVATE
        src/ServerApp.cpp
        src/MarketDataPublisher.cpp
        src/OrderEntryGateway.cpp
    )
endif()

# Include the src and include directories so headers can be found easily
target_include_directories(mercury PUBLIC src include)
target_link_libraries(mercury PRIVATE Threads::Threads nlohmann_json::nlohmann_json)
if(MERCURY_BUILD_SERVER)
    target_link_libraries(mercury PRIVATE uwebsockets)
endif()

# Create a library for testable code (excluding main.cpp)
add_library(mercury_lib
    ${MERCURY_CORE_SOURCES}
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
    include/MarketData.h
    include/MarketDataPublisher.h
    include/EngineService.h
    include/ServerApp.h
    include/ServerHelpers.h
    include/OrderEntryGateway.h
    include/BinaryProtocol.h
)
target_sources(mercury_lib PRIVATE ${MERCURY_HEADERS})

enable_testing()

# Add test executable
add_executable(mercury_tests
    tests/data_structures_test.cpp
    tests/market_data_test.cpp
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
