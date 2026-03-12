#pragma once

#include "options/Option.h"

#include <string>

namespace options {

/**
 * Result of an implied volatility solve.
 */
struct IVResult {
    double      impliedVol = 0.0;
    bool        converged  = false;
    std::string message;
};

/**
 * Compute implied volatility from a market option price.
 *
 * Uses Newton-Raphson (quadratic convergence near the root) with automatic
 * fallback to bisection if vega is too small to take reliable NR steps, or
 * if the NR iterate leaves the valid bracket [1e-6, 10.0].
 *
 * Initial guess: Brenner-Subrahmanyam approximation σ ≈ (P/S)·√(2π/T),
 * which is exact for ATM options and a reasonable starting point elsewhere.
 *
 * Edge cases that return converged=false without iterating:
 *   - marketPrice < intrinsic value (arbitrage lower bound violated)
 *   - marketPrice ≥ theoretical upper bound (S for call, K·e^(−rT) for put)
 *   - Non-positive S, K, T or non-positive marketPrice
 *
 * @param marketPrice  Observed market price of the option
 * @param type         OptionType::Call or OptionType::Put
 * @param S            Spot price
 * @param K            Strike price
 * @param r            Continuously compounded risk-free rate
 * @param T            Time to expiry in years
 */
IVResult solveIV(double marketPrice, OptionType type,
                 double S, double K, double r, double T);

} // namespace options
