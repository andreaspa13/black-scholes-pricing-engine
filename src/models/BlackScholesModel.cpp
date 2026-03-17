#include "models/BlackScholesModel.h"
#include "options/EuropeanOption.h"

#include <cmath>
#include <stdexcept>

namespace options {
double BlackScholesModel::N(double x) const { // Standard normal CDF using complementary error function (cmath) for accuracy.
    return 0.5 * std::erfc(-x / std::sqrt(2.0));
}

PricingResult BlackScholesModel::price(const Option& option, const MarketData& market) const {
    // BS is only used with European options.
    // dynamic_cast returns nullptr for pointer cast on failure, throws for ref.
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

    // Guard against invalid inputs
    if (T <= 0.0)     throw std::domain_error("BlackScholesModel: expiry must be positive");
    if (S <= 0.0)     throw std::domain_error("BlackScholesModel: spot must be positive");
    if (sigma <= 0.0) throw std::domain_error("BlackScholesModel: volatility must be positive");

    const double sqrtT = std::sqrt(T);
    const double d1 = (std::log(S / K) + (r + 0.5 * sigma * sigma) * T)
                      / (sigma * sqrtT);
    const double d2 = d1 - sigma * sqrtT;

    const double discountedStrike = K * std::exp(-r * T);

    PricingResult result;
    result.modelName = "BlackScholes";
    result.stdErr    = 0.0;  // analytical, no simulation error

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

    // Greeks
    // Normal PDF evaluated at d1: N'(d1) = (1/sqrt(2π)) * exp(-d1²/2)
    static constexpr double kInvSqrt2Pi = 0.3989422804014327;
    const double nd1 = kInvSqrt2Pi * std::exp(-0.5 * d1 * d1);

    // Gamma: d²V/dS² 
    result.gamma = nd1 / (S * sigma * sqrtT);

    // Vega: dV/dσ 
    result.vega = S * nd1 * sqrtT;

    // Theta and Rho
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

} 
