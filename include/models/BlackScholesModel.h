#pragma once

#include "models/PricingModel.h"

namespace options {

/**
 * Analytical Black-Scholes pricing for European options.
 *
 * Implements the closed-form BS formula under the assumptions:
 *   - Constant volatility
 *   - Continuous risk-free rate and dividend yield
 *   - Log-normally distributed returns
 *   - No transaction costs or early exercise
 *
 * Also computes delta analytically (d1 term), which is a free by-product
 * of the BS formula. Populates PricingResult::delta accordingly.
 *
 * Design note: BS is only valid for European options. In Iteration 1 we
 * enforce this at runtime with a dynamic_cast check and throw. In Iteration
 * 2, templates will push this constraint to compile time — no cast needed.
 */
class BlackScholesModel : public PricingModel {
public:
    PricingResult price(const Option& option,
                        const MarketData& market) const override;

private:
    // Standard normal CDF — used for N(d1) and N(d2) in the BS formula.
    // Implemented via std::erfc for accuracy; avoids table lookups.
    double N(double x) const;
};

} // namespace options
