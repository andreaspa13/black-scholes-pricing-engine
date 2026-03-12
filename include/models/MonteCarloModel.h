#pragma once

#include "models/PricingModel.h"

#include <random>
#include <vector>

namespace options {

/**
 * Variance reduction technique applied during Monte Carlo simulation.
 *
 * Kept as a separate enum so new techniques (control variates, importance
 * sampling, stratified sampling) can be added without changing the class
 * interface — just extend the enum and add a branch in price().
 */
enum class VarianceReduction {
    None,       // plain Monte Carlo, one path per random draw
    Antithetic  // antithetic variates: each draw Z generates paths for +Z and −Z
};

/**
 * Monte Carlo pricer using Geometric Brownian Motion path simulation.
 *
 * Simulates N paths of the underlying under the risk-neutral measure:
 *
 *   S_{t+dt} = S_t * exp((r - q - sigma^2/2)*dt + sigma*sqrt(dt)*Z)
 *   where Z ~ N(0,1)
 *
 * Price estimate = e^(-rT) * mean(payoff over all paths)
 *
 * --- Deliberate Iteration 1 design choices (known issues) ---
 *
 * 1. mutable RNG state
 *    price() is logically const (same parameters → same distribution of
 *    outcomes) but physically mutates the RNG. This breaks thread safety:
 *    two threads calling price() on the same model instance will race on
 *    rng_. Fixed in a later iteration via thread_local or per-call RNG.
 *
 * 2. Per-path vector allocation
 *    generatePath() allocates a std::vector<double> on every path.
 *    With 100k paths this is 100k heap allocations per price() call.
 *    This is the primary allocation bottleneck Iteration 3 will address
 *    with a pre-allocated buffer or memory pool.
 *
 * Both are documented here intentionally — being able to explain why
 * a naive implementation makes these choices, and what the fix is,
 * is more valuable in an interview than pretending they don't exist.
 */
class MonteCarloModel : public PricingModel {
public:
    /**
     * @param numPaths     Number of simulated paths. More paths → lower stderr,
     *                     roughly stderr ∝ 1/sqrt(numPaths).
     * @param numSteps     Time steps per path. For plain European options a
     *                     single step suffices (only terminal value matters).
     *                     numSteps > 1 is scaffolding for path-dependent options.
     * @param seed         RNG seed for reproducibility.
     * @param varReduction Variance reduction technique to apply.
     *                     Antithetic: generates N/2 pairs (Z, −Z), halving the
     *                     number of RNG draws while typically reducing variance
     *                     more than proportionally for monotone payoffs.
     */
    explicit MonteCarloModel(int numPaths = 100'000,
                             int numSteps = 1,
                             unsigned seed = 42,
                             VarianceReduction varReduction = VarianceReduction::None);

    PricingResult price(const Option& option,
                        const MarketData& market) const override;

private:
    int              numPaths_;
    int              numSteps_;
    VarianceReduction varReduction_;

    // mutable because price() is const but RNG advances on each draw.
    // See class-level note above.
    mutable std::mt19937                     rng_;
    mutable std::normal_distribution<double> dist_;

    /**
     * Simulate one GBM path and return the spot price at each step.
     * Allocates a new vector per call — the Iteration 3 bottleneck.
     */
    std::vector<double> generatePath(double spot,
                                     double dt,
                                     double drift,
                                     double vol) const;
};

} // namespace options
