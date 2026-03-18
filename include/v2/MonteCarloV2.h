#pragma once

#include "v2/Concepts.h"
#include "models/MonteCarloModel.h"  
#include "utils/MarketData.h"
#include "utils/PricingResult.h"

#include <cmath>
#include <numeric>
#include <random>
#include <stdexcept>
#include <vector>

namespace options::v2 {

/**
 * Iteration 2: template-based Monte Carlo pricer.
 *
 * Improvements over V1 (options::MonteCarloModel):
 *
 * 1. No virtual dispatch on payoff()
 *    V1: price() takes const Option& — the concrete type is erased. Every
 *        call to option.payoff(spot) goes through a vtable pointer, paying
 *        ~3-5 ns of indirection per path. With 100k paths this is ~300-500
 *        µs of pure dispatch overhead per price() call.
 *    V2: price() is a template on Priceable Opt — the concrete type is known
 *        at compile time. option.payoff(spot) is a direct call, inlineable
 *        by the compiler. The virtual dispatch cost disappears entirely.
 *
 * 2. No per-path vector allocation
 *    V1: generatePath() allocated a std::vector<double> on every path —
 *        the dominant cost at ~30-40 ns/path (100k heap allocs per call).
 *        This was the Iteration 3 target, but the template design eliminates
 *        it naturally: since payoff() is inlineable and only the terminal
 *        spot is needed for a European option, the path is accumulated in a
 *        scalar, never materialised as a heap buffer.
 *    V2: path simulation is inlined in price() using a scalar double.
 *        Zero heap allocation per path.
 *
 * 3. No PricingModel base class
 *    V1: inherited from PricingModel, price() was virtual.
 *    V2: standalone class, no inheritance, no vtable.
 *
 */
class MonteCarloModel {
public:

    explicit MonteCarloModel(int numPaths = 100'000,
                             int numSteps = 1,
                             unsigned seed = 42,
                             VarianceReduction varReduction = VarianceReduction::None)
        : numPaths_(numPaths)
        , numSteps_(numSteps)
        , varReduction_(varReduction)
        , rng_(seed)
        , dist_(0.0, 1.0)
    {
        if (numPaths_ <= 0) throw std::invalid_argument("MC-V2: numPaths must be positive");
        if (numSteps_ <= 0) throw std::invalid_argument("MC-V2: numSteps must be positive");
    }

    template<Priceable Opt>
    PricingResult price(const Opt& option, const MarketData& market) const {

        const double S     = market.spot;
        const double r     = market.riskFreeRate;
        const double sigma = market.volatility;
        const double T     = option.expiry();

        if (T <= 0.0)     throw std::domain_error("MC-V2: expiry must be positive");
        if (S <= 0.0)     throw std::domain_error("MC-V2: spot must be positive");
        if (sigma <= 0.0) throw std::domain_error("MC-V2: volatility must be positive");

        const double dt         = T / numSteps_;
        const double drift      = r - 0.5 * sigma * sigma;
        const double volSqrtDt  = sigma * std::sqrt(dt);
        const double discount   = std::exp(-r * T);

        PricingResult result;
        result.delta = 0.0;

        if (varReduction_ == VarianceReduction::Antithetic) {
            const int numPairs = numPaths_ / 2;
            std::vector<double> samples(static_cast<size_t>(numPairs));

            for (int i = 0; i < numPairs; ++i) {
                double Spos = S, Sneg = S;
                for (int step = 0; step < numSteps_; ++step) {
                    const double Z   = dist_(rng_);
                    const double fwd = drift * dt + volSqrtDt * Z;
                    Spos *= std::exp(fwd);
                    Sneg *= std::exp(drift * dt - volSqrtDt * Z);
                }
                // Direct call — concrete type known, no virtual dispatch
                samples[static_cast<size_t>(i)] =
                    0.5 * (option.payoff(Spos) + option.payoff(Sneg));
            }

            const double mean = std::accumulate(samples.begin(), samples.end(), 0.0)
                                / static_cast<double>(numPairs);
            double variance = 0.0;
            for (double y : samples) { const double d = y - mean; variance += d * d; }
            variance /= static_cast<double>(numPairs - 1);

            result.modelName = "MonteCarlo-V2+AV";
            result.price     = discount * mean;
            result.stdErr    = discount * std::sqrt(variance / static_cast<double>(numPairs));

        } else {
            std::vector<double> payoffs(static_cast<size_t>(numPaths_));

            for (int i = 0; i < numPaths_; ++i) {
                // Scalar path accumulation — no per-path vector allocation
                double spot = S;
                for (int step = 0; step < numSteps_; ++step) {
                    spot *= std::exp(drift * dt + volSqrtDt * dist_(rng_));
                }
                // Direct call — concrete type known, no virtual dispatch
                payoffs[static_cast<size_t>(i)] = option.payoff(spot);
            }

            const double mean = std::accumulate(payoffs.begin(), payoffs.end(), 0.0)
                                / static_cast<double>(numPaths_);
            double variance = 0.0;
            for (double p : payoffs) { const double d = p - mean; variance += d * d; }
            variance /= static_cast<double>(numPaths_ - 1);

            result.modelName = "MonteCarlo-V2";
            result.price     = discount * mean;
            result.stdErr    = discount * std::sqrt(variance / static_cast<double>(numPaths_));
        }

        return result;
    }

private:
    int               numPaths_;
    int               numSteps_;
    VarianceReduction varReduction_;

    mutable std::mt19937                     rng_;
    mutable std::normal_distribution<double> dist_;
};

}
