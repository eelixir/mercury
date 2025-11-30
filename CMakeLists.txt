cmake_minimum_required(VERSION 3.10)

project(Mercury VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Add the main executable
add_executable(mercury
    src/main.cpp
    src/OrderBook.cpp
    src/CSVParser.cpp
    src/MatchingEngine.cpp
    src/TradeWriter.cpp
    include/OrderBook.h
    include/Order.h
    include/CSVParser.h
    include/MatchingEngine.h
    include/TradeWriter.h
)

# Include the src and include directories so headers can be found easily
target_include_directories(mercury PUBLIC src include)

# Create a library for testable code (excluding main.cpp)
add_library(mercury_lib
    src/OrderBook.cpp
    src/CSVParser.cpp
    src/MatchingEngine.cpp
    src/TradeWriter.cpp
)
target_include_directories(mercury_lib PUBLIC include)

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
)

target_link_libraries(mercury_tests
    mercury_lib
    GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(mercury_tests)
