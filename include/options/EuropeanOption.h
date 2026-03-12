#pragma once

#include "options/Option.h"

namespace options {

/**
 * A standard European option — exercisable only at expiry.
 *
 * Payoff:
 *   Call: max(S_T - K, 0)
 *   Put:  max(K - S_T, 0)
 *
 * This is the only option type in Iteration 1. Keeping it as a concrete
 * subclass of Option (rather than just using Option directly) preserves
 * the inheritance structure that Iteration 2 will refactor away with
 * templates, making the performance contrast explicit and meaningful.
 */
class EuropeanOption : public Option {
public:
    EuropeanOption(double strike, double expiry, OptionType type)
        : Option(strike, expiry, type) {}

    /**
     * Terminal payoff. Called by both BS (analytically derived, but
     * the payoff function informs the formula) and MC (called once
     * per simulated path at the terminal node).
     */
    double payoff(double spotAtExpiry) const override;
};

} // namespace options
