cmake_minimum_required(VERSION 3.10)

project(Mercury VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Add the executable
add_executable(mercury
    src/main.cpp
    src/OrderBook.cpp
    include/OrderBook.h
    include/Order.h
)

# Include the src and include directories so headers can be found easily
target_include_directories(mercury PUBLIC src include)
