#pragma once

namespace options {

/**
 * Snapshot of market inputs required to price an option.
 * struct, pure data with no invariants to enforce.
 */
struct MarketData {
    double spot;           
    double riskFreeRate; 
    double volatility; 
};

}
