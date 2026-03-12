#pragma once

namespace options {

/**
 * Snapshot of market inputs required to price an option.
 *
 * Kept as a plain struct — it's pure data with no invariants to enforce.
 * Passed by const ref into pricing models; models do not own it.
 */
struct MarketData {
    double spot;           // Current underlying price (S)
    double riskFreeRate;   // Continuously compounded risk-free rate (r)
    double volatility;     // Implied or historical vol (sigma)
};

} // namespace options
