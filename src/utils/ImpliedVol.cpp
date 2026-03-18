#include "utils/ImpliedVol.h"
#include "models/BlackScholesModel.h"
#include "options/EuropeanOption.h"
#include "utils/MarketData.h"

#include <cmath>

namespace options {

// Constants
// constexpr used to evaluate at compile time (no runtime cost)
//static at file scope (private to this unit)
static constexpr int    kMaxNRIter  = 50;
static constexpr int    kMaxBisIter = 100;
static constexpr double kTol        = 1e-8;   // price convergence tolerance
static constexpr double kMinVega    = 1e-10;  // vega below this → switch to bisection
static constexpr double kSigmaLo   = 1e-6;   // lower bracket bound (0.0001%)
static constexpr double kSigmaHi   = 10.0;   // upper bracket bound (1000%)

// Helper: price + vega at a given σ
struct BSEval { double price; double vega; };

// Prices the option at a given trial σ and returns price and vega.
static BSEval evalBS(double sigma, OptionType type, double S, double K, double r, double T) { 
    BlackScholesModel bs;
    EuropeanOption opt(K, T, type);
    MarketData mkt{ S, r, sigma };
    const auto res = bs.price(opt, mkt);
    return { res.price, res.vega };
}


IVResult solveIV(double marketPrice, OptionType type,
                 double S, double K, double r, double T) {

    // Input validation - guard clause
    if (T <= 0.0 || S <= 0.0 || K <= 0.0)
        return { 0.0, false, "S, K and T must all be positive" };
    if (marketPrice <= 0.0)
        return { 0.0, false, "Market price must be positive" };

    // No-arbitrage bounds: minimum and maximum an option can be worth
    const double disc       = std::exp(-r * T);
    const double intrinsic  = (type == OptionType::Call) // intrinsic value: S-K for call; K*e^(-rT)-S for put
                              ? std::max(S - K * disc, 0.0)
                              : std::max(K * disc - S, 0.0);
    const double upperBound = (type == OptionType::Call) ? S : K * disc; // cannot be worth more than the discounted strike (put) or spot (call)

    if (marketPrice < intrinsic - kTol) // riskless profit from immediate exercise exceeds market price → no valid IV exists
        return { 0.0, false, "Price below intrinsic value — no valid IV exists" };
    if (marketPrice >= upperBound - kTol) // worth more than the stock (call) or discounted strike (put) → no valid IV exists
        return { 0.0, false, "Price at or above theoretical upper bound — no valid IV exists" };

    // Initial guess: Brenner-Subrahmanyam ATM approximation
    // σ ≈ (P / S) · √(2π / T),  exact for ATM, reasonable elsewhere
    static constexpr double kSqrt2Pi = 2.5066282746310002;
    double sigma = (marketPrice / S) * kSqrt2Pi / std::sqrt(T);
    sigma = std::max(kSigmaLo, std::min(sigma, kSigmaHi));

    // Newton-Raphson
    for (int i = 0; i < kMaxNRIter; ++i) {
        BSEval ev;
        try { ev = evalBS(sigma, type, S, K, r, T); } // If BS throws break to bisection fallback.
        catch (...) { break; }

        const double diff = ev.price - marketPrice; // f(σ) = BS_price(σ) - marketPrice; we want f(σ)~=0
        if (std::abs(diff) < kTol)
            return { sigma, true, "converged (Newton-Raphson)" };

        if (ev.vega < kMinVega) break;  // vega vanishes → bisection is safer

        const double newSigma = sigma - diff / ev.vega; // Newton-Raphson update
        if (newSigma <= kSigmaLo || newSigma >= kSigmaHi) break;  // Divergence → bisection is safer
        sigma = newSigma;
    }

    // Bisection fallback
    // Guarantee a bracket [lo, hi] where f(lo) < 0 < f(hi).
    double lo = kSigmaLo, hi = kSigmaHi;
    double fLo, fHi;
    try {
        fLo = evalBS(lo, type, S, K, r, T).price - marketPrice;
        fHi = evalBS(hi, type, S, K, r, T).price - marketPrice;
    } catch (...) {
        return { 0.0, false, "Pricing error during bracket check" };
    }

    if (fLo * fHi > 0.0) // Sign check
        return { 0.0, false, "No solution in vol range [0.0001%, 1000%]" };

    for (int i = 0; i < kMaxBisIter; ++i) {
        const double mid = 0.5 * (lo + hi);
        double fMid;
        try { fMid = evalBS(mid, type, S, K, r, T).price - marketPrice; }
        catch (...) { return { 0.0, false, "Pricing error during bisection" }; }

        if (std::abs(fMid) < kTol || (hi - lo) < kTol)
            return { mid, true, "converged (bisection)" };

        if (fLo * fMid < 0.0) { hi = mid; fHi = fMid; }
        else                   { lo = mid; fLo = fMid; }
    }

    // Bisection ran out of iterations — return best estimate with warning
    return { 0.5 * (lo + hi), false, "max iterations reached — result approximate" };
}

} 
