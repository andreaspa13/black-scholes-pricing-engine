#pragma once

#include "v2/Concepts.h"
#include "models/MonteCarloModel.h"  // VarianceReduction enum
#include "utils/MarketData.h"
#include "utils/PricingResult.h"

#include <cmath>
#include <numeric>
#include <random>
#include <stdexcept>
#include <thread>
#include <vector>

namespace options::v3 {

/**
 * Iteration 3: parallel Monte Carlo pricer.
 *
 * Builds on V2 (template payoff, no per-path allocation) and adds
 * multi-threaded path simulation using std::thread.
 *
 * Thread safety design:
 * V1/V2 stored a mutable std::mt19937 as a member. This is not thread-safe:
 * two concurrent calls to price() on the same instance race on the RNG state.
 * The V2 header documented this as the remaining known issue.
 * V3 fixes this by giving each worker thread its own independent RNG, seeded
 * deterministically from the base seed.
 * V3 holds no mutable state. price() is genuinely const and thread-safe.
 *
 * Variance merging
 *
 * Each thread produces a partial (sum, sumSq, count). These are combined
 * using the parallel variance formula (Chan et al., 1979).
 *
 * Thread count 
 *
 * numThreads = 0 (default) uses std::thread::hardware_concurrency(), falling
 * back to 1 if the platform cannot determine core count.
 *
 * Speedup expectation
 *
 * Near-linear with core count for large N (the inner loop is embarrassingly
 * parallel — no shared mutable state, no synchronisation during simulation).
 * Overhead: thread creation + join. For small N this dominates; the crossover
 * is typically around 10k–50k paths depending on the hardware.
 */
class MonteCarloModel {
public:
    explicit MonteCarloModel(int numPaths  = 100'000,
                             int numSteps  = 1,
                             unsigned seed = 42,
                             VarianceReduction varReduction = VarianceReduction::None,
                             int numThreads = 0) 
        : numPaths_(numPaths)
        , numSteps_(numSteps)
        , seed_(seed)
        , varReduction_(varReduction)
        , numThreads_(numThreads > 0
                      ? numThreads
                      : static_cast<int>(
                            std::max(1u, std::thread::hardware_concurrency()))) // use hardware concurrency by default
    {
        if (numPaths_ <= 0)  throw std::invalid_argument("MC-V3: numPaths must be positive");
        if (numSteps_ <= 0)  throw std::invalid_argument("MC-V3: numSteps must be positive");
        if (numThreads_ <= 0) throw std::invalid_argument("MC-V3: numThreads must be positive");
    }

    template<v2::Priceable Opt>
    PricingResult price(const Opt& option, const MarketData& market) const {

        const double S     = market.spot;
        const double r     = market.riskFreeRate;
        const double sigma = market.volatility;
        const double T     = option.expiry();

        if (T <= 0.0)     throw std::domain_error("MC-V3: expiry must be positive");
        if (S <= 0.0)     throw std::domain_error("MC-V3: spot must be positive");
        if (sigma <= 0.0) throw std::domain_error("MC-V3: volatility must be positive");

        // Precompute constants for the geometric Brownian motion simulation
        const double dt        = T / numSteps_;
        const double drift     = r - 0.5 * sigma * sigma;
        const double volSqrtDt = sigma * std::sqrt(dt);
        const double discount  = std::exp(-r * T);

        // Partition work across threads
        // For antithetic, we partition pairs rather than individual paths so
        // each thread maintains paired (Z, -Z) draws without coordination.
        const int totalUnits   = (varReduction_ == VarianceReduction::Antithetic)
                                 ? numPaths_ / 2   // units = pairs
                                 : numPaths_;       // units = paths
        const int unitsPerThread = totalUnits / numThreads_;
        const int remainder      = totalUnits % numThreads_;

        // Per-thread partial statistics: sum, sum-of-squares, count
        struct ThreadStats { double sum; double sumSq; int count; };
        std::vector<ThreadStats> stats(static_cast<size_t>(numThreads_));
        std::vector<std::thread> threads;
        threads.reserve(static_cast<size_t>(numThreads_));

        for (int t = 0; t < numThreads_; ++t) {
            const int thisUnits = unitsPerThread + (t < remainder ? 1 : 0);
            // Per-thread seed: XOR with Knuth's multiplicative constant scaled
            // by thread index to spread seeds far apart in the seed space.
            const unsigned thisSeed = seed_ ^ (static_cast<unsigned>(t + 1) * 2654435761u);

            threads.emplace_back([&, t, thisUnits, thisSeed]() {
                std::mt19937                     rng(thisSeed);
                std::normal_distribution<double> dist(0.0, 1.0);

                double sum = 0.0, sumSq = 0.0;

                if (varReduction_ == VarianceReduction::Antithetic) {
                    for (int i = 0; i < thisUnits; ++i) {
                        double Spos = S, Sneg = S;
                        for (int step = 0; step < numSteps_; ++step) {
                            const double Z   = dist(rng);
                            const double fwd = drift * dt + volSqrtDt * Z;
                            Spos *= std::exp(fwd);
                            Sneg *= std::exp(drift * dt - volSqrtDt * Z);
                        }
                        const double y = 0.5 * (option.payoff(Spos) + option.payoff(Sneg));
                        sum   += y;
                        sumSq += y * y;
                    }
                } else {
                    for (int i = 0; i < thisUnits; ++i) {
                        double spot = S;
                        for (int step = 0; step < numSteps_; ++step) {
                            spot *= std::exp(drift * dt + volSqrtDt * dist(rng));
                        }
                        const double y = option.payoff(spot);
                        sum   += y;
                        sumSq += y * y;
                    }
                }

                stats[static_cast<size_t>(t)] = { sum, sumSq, thisUnits };
            });
        }

        for (auto& th : threads) th.join();

        // ── Merge using Chan's parallel variance formula ───────────────────────
        // Step 1: combined mean
        double totalSum = 0.0;
        int    totalN   = 0;
        for (const auto& s : stats) {
            totalSum += s.sum;
            totalN   += s.count;
        }
        const double mean = totalSum / static_cast<double>(totalN);

        // Step 2: combine per-thread sums of squared deviations
        // ssq_i = sumSq_i − n_i * μ_i²  (thread-local sum of squared deviations)
        // ssq   = Σ(ssq_i + n_i * (μ_i − μ)²)
        double ssq = 0.0;
        for (const auto& s : stats) {
            const double mu_i  = s.sum / static_cast<double>(s.count);
            const double ssq_i = s.sumSq
                                 - static_cast<double>(s.count) * mu_i * mu_i;
            const double bias  = static_cast<double>(s.count) * (mu_i - mean) * (mu_i - mean);
            ssq += ssq_i + bias;
        }
        const double variance = ssq / static_cast<double>(totalN - 1);  // Bessel's correction

        const bool antithetic = (varReduction_ == VarianceReduction::Antithetic);

        PricingResult result;
        result.modelName = antithetic ? "MonteCarlo-V3+AV" : "MonteCarlo-V3";
        result.delta     = 0.0;
        result.price     = discount * mean;
        result.stdErr    = discount * std::sqrt(variance / static_cast<double>(totalN));

        return result;
    }

private:
    int               numPaths_;
    int               numSteps_;
    unsigned          seed_;
    VarianceReduction varReduction_;
    int               numThreads_;
    // No mutable RNG member — V3 is genuinely const and thread-safe.
};

} 
