#pragma once

#include "options/Option.h"

namespace options {

/**
 * A standard European option exercisable only at expiry.
 * Payoff:
 *   Call: max(S_T - K, 0)
 *   Put:  max(K - S_T, 0)
 */
class EuropeanOption : public Option {
public:
    EuropeanOption(double strike, double expiry, OptionType type)
        : Option(strike, expiry, type) {}

    double payoff(double spotAtExpiry) const override;
};

} 
