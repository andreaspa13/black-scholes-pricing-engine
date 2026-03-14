#pragma once

#include "v2/Concepts.h"
#include "utils/MarketData.h"
#include "utils/PricingResult.h"

#include <cmath>
#include <stdexcept>

namespace options::v2 {

/**
 * Iteration 2: template-based Black-Scholes pricer.
 *
 * Improvements over V1 (options::BlackScholesModel):
 *
 * 1. No dynamic_cast
 *    V1: dynamic_cast<const EuropeanOption*>(&option) — runtime type check,
 *        throws std::invalid_argument on failure.
 *    V2: template constraint EuropeanPriceable<Opt> — compile-time rejection.
 *        A non-European type is a compiler error, not a runtime exception.
 *
 * 2. No PricingModel base class
 *    V1: inherited from PricingModel, price() was virtual.
 *    V2: standalone class, price() is a constrained function template.
 *        No vtable, no pointer-to-base indirection needed.
 *
 * 3. Options become value types
 *    V1: Option was non-copyable (deleted copy constructor), always passed
 *        by pointer/reference through the base class.
 *    V2: Opt is a template parameter — options can be passed by value,
 *        enabling stack allocation and trivial copies for cheap types.
 *
 * The formula is identical to V1. Any benchmark difference is attributable
 * purely to the elimination of dynamic_cast (~5-10 ns/call on x86-64).
 *
 * Implementation in the header because function templates must be visible
 * at the point of instantiation. This is a fundamental constraint of C++
 * templates, not a stylistic choice.
 */
class BlackScholesModel {
public:

    template<EuropeanPriceable Opt>
    PricingResult price(const Opt& option, const MarketData& market) const {

        const double S     = market.spot;
        const double K     = option.strike();
        const double r     = market.riskFreeRate;
        const double sigma = market.volatility;
        const double T     = option.expiry();

        if (T <= 0.0)     throw std::domain_error("BS-V2: expiry must be positive");
        if (S <= 0.0)     throw std::domain_error("BS-V2: spot must be positive");
        if (sigma <= 0.0) throw std::domain_error("BS-V2: volatility must be positive");

        const double sqrtT            = std::sqrt(T);
        const double d1               = (std::log(S / K) + (r + 0.5 * sigma * sigma) * T)
                                        / (sigma * sqrtT);
        const double d2               = d1 - sigma * sqrtT;
        const double discountedStrike = K * std::exp(-r * T);

        PricingResult result;
        result.modelName = "BlackScholes-V2";
        result.stdErr    = 0.0;

        switch (option.type()) {
            case OptionType::Call:
                result.price = S * N(d1) - discountedStrike * N(d2);
                result.delta = N(d1);
                break;
            case OptionType::Put:
                result.price = discountedStrike * N(-d2) - S * N(-d1);
                result.delta = N(d1) - 1.0;
                break;
        }

        static constexpr double kInvSqrt2Pi = 0.3989422804014327;
        const double nd1 = kInvSqrt2Pi * std::exp(-0.5 * d1 * d1);

        result.gamma = nd1 / (S * sigma * sqrtT);
        result.vega  = S * nd1 * sqrtT;

        const double thetaBase = -S * nd1 * sigma / (2.0 * sqrtT);
        switch (option.type()) {
            case OptionType::Call:
                result.theta = (thetaBase - r * discountedStrike * N(d2))  / 365.0;
                result.rho   =  T * discountedStrike * N(d2);
                break;
            case OptionType::Put:
                result.theta = (thetaBase + r * discountedStrike * N(-d2)) / 365.0;
                result.rho   = -T * discountedStrike * N(-d2);
                break;
        }

        return result;
    }

private:
    // Identical to V1 — N(x) = standard normal CDF via erfc for tail accuracy.
    double N(double x) const {
        return 0.5 * std::erfc(-x / std::sqrt(2.0));
    }
};

} // namespace options::v2
