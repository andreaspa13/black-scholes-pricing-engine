#pragma once

#include <string>

namespace options {
 // Output of a pricing calculation.
 // struct used for pure data buckets.
struct PricingResult {
    double price   = 0.0;
    double stdErr  = 0.0;   // MC convergence metric; 0 for analytical models
    double delta   = 0.0;   // dV/dS
    double gamma   = 0.0;   // d²V/dS²
    double vega    = 0.0;   // dV/dσ
    double theta   = 0.0;   // dV/dt per calendar day (negative = time decay)
    double rho     = 0.0;   // dV/dr
    std::string modelName;  // Identifies the pricing model used, e.g. "Black-Scholes", "Monte Carlo"
};

} 
