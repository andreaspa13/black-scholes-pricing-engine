#pragma once

#include <string>

namespace options {

/**
 * Output of a pricing calculation.
 *
 * stderr is only meaningful for Monte Carlo results — it carries the
 * standard error of the path-average estimator and gives a sense of
 * simulation convergence. For analytical models it is 0.
 *
 * Greeks are left as NaN by default. Models populate what they can
 * compute; callers should check before using.
 */
struct PricingResult {
    double price   = 0.0;
    double stdErr  = 0.0;   // MC convergence metric; 0 for analytical models
    double delta   = 0.0;   // dV/dS
    double gamma   = 0.0;   // d²V/dS²
    double vega    = 0.0;   // dV/dσ
    double theta   = 0.0;   // dV/dt per calendar day (negative = time decay)
    double rho     = 0.0;   // dV/dr
    std::string modelName;  // for benchmarking output and logging
};

} // namespace options
