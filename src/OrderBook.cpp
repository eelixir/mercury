/**
 * @file OrderBook.cpp
 * @brief OrderBook implementation (now header-only)
 * 
 * The OrderBook is now implemented as a header-only class in OrderBook.h
 * to take advantage of template inlining and reduce function call overhead.
 * 
 * This file is kept for backwards compatibility with the build system.
 */

#include "OrderBook.h"

// All implementation is now in the header file for better inlining
// and template instantiation.

namespace Mercury {
    // Empty - implementation is header-only
}