#pragma once

#include "utils/MarketData.h"
#include "utils/PricingResult.h"

namespace options {

class Option;  // forward declaration — model headers don't need full Option definition

/**
 * Abstract base for all pricing models.
 *
 * The interface is intentionally minimal: take an option and market data,
 * return a result. Models do not own options or market data — they are
 * stateless calculators (with the deliberate exception of MC's RNG, which
 * is the Iteration 1 design smell we'll address later).
 *
 * Why separate from Option?
 *   Option type and pricing method vary independently. A EuropeanOption
 *   can be priced by BS or MC. MC can (in principle) price any option.
 *   Fusing them would create an N×M class explosion and tighter coupling
 *   than the domain warrants.
 */
class PricingModel {
public:
    virtual ~PricingModel() = default;

    virtual PricingResult price(const Option& option,
                                const MarketData& market) const = 0;
};

} // namespace options
