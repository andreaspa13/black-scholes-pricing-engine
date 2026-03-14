#include "models/BlackScholesModel.h"
#include "options/EuropeanOption.h"

#include <cmath>
#include <stdexcept>

namespace options {

/**
 * Standard normal CDF via the complementary error function.
 *
 * N(x) = 0.5 * erfc(-x / sqrt(2))
 *
 * std::erfc is in <cmath> and is more numerically stable in the tails
 * than a polynomial approximation, which matters for deep in/out-of-money
 * options. This is the standard implementation choice.
 */
double BlackScholesModel::N(double x) const {
    return 0.5 * std::erfc(-x / std::sqrt(2.0));
}

PricingResult BlackScholesModel::price(const Option& option,
                                        const MarketData& market) const {
    // Runtime enforcement that BS is only used with European options.
    // dynamic_cast returns nullptr for pointer cast on failure, throws for ref.
    // We use pointer cast so we can produce a readable error message.
    const auto* european = dynamic_cast<const EuropeanOption*>(&option);
    if (!european) {
        throw std::invalid_argument(
            "BlackScholesModel: analytical formula only valid for EuropeanOption. "
            "Use MonteCarloModel for other option types.");
    }

    const double S     = market.spot;
    const double K     = option.strike();
    const double r     = market.riskFreeRate;
    const double sigma = market.volatility;
    const double T     = option.expiry();

    // Guard against degenerate inputs
    if (T <= 0.0)     throw std::domain_error("BlackScholesModel: expiry must be positive");
    if (S <= 0.0)     throw std::domain_error("BlackScholesModel: spot must be positive");
    if (sigma <= 0.0) throw std::domain_error("BlackScholesModel: volatility must be positive");

    /**
     * Standard BS formula (no dividends):
     *
     *   d1 = [ln(S/K) + (r + sigma^2/2) * T] / (sigma * sqrt(T))
     *   d2 = d1 - sigma * sqrt(T)
     *
     *   Call = S * N(d1)  - K * e^(-rT) * N(d2)
     *   Put  = K * e^(-rT) * N(-d2) - S * N(-d1)
     *
     * Put-call parity holds: Call - Put = S - K*e^(-rT)
     */
    const double sqrtT = std::sqrt(T);
    const double d1 = (std::log(S / K) + (r + 0.5 * sigma * sigma) * T)
                      / (sigma * sqrtT);
    const double d2 = d1 - sigma * sqrtT;

    const double discountedStrike = K * std::exp(-r * T);

    PricingResult result;
    result.modelName = "BlackScholes";
    result.stdErr    = 0.0;  // analytical — no simulation error

    switch (option.type()) {
        case OptionType::Call:
            result.price = S * N(d1) - discountedStrike * N(d2);
            result.delta = N(d1);  // dCall/dS
            break;
        case OptionType::Put:
            result.price = discountedStrike * N(-d2) - S * N(-d1);
            result.delta = N(d1) - 1.0;  // dPut/dS
            break;
    }

    // ── Greeks ────────────────────────────────────────────────────────────────
    // Normal PDF evaluated at d1: N'(d1) = (1/sqrt(2π)) * exp(-d1²/2)
    static constexpr double kInvSqrt2Pi = 0.3989422804014327;
    const double nd1 = kInvSqrt2Pi * std::exp(-0.5 * d1 * d1);

    // Gamma: d²V/dS² — identical for calls and puts
    result.gamma = nd1 / (S * sigma * sqrtT);

    // Vega: dV/dσ — identical for calls and puts (returned in price-point terms)
    result.vega = S * nd1 * sqrtT;

    // Theta and Rho differ by option type
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

} // namespace options
