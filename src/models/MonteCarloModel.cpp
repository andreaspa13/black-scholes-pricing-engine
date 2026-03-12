#include "models/MonteCarloModel.h"
#include "options/Option.h"

#include <cmath>
#include <numeric>   // std::accumulate
#include <stdexcept>

namespace options {

MonteCarloModel::MonteCarloModel(int numPaths, int numSteps,
                                 unsigned seed, VarianceReduction varReduction)
    : numPaths_(numPaths)
    , numSteps_(numSteps)
    , varReduction_(varReduction)
    , rng_(seed)
    , dist_(0.0, 1.0)
{
    if (numPaths_ <= 0) throw std::invalid_argument("MC: numPaths must be positive");
    if (numSteps_ <= 0) throw std::invalid_argument("MC: numSteps must be positive");
}

/**
 * Simulate one path under risk-neutral GBM using the exact log-normal
 * discretisation (no Euler approximation error):
 *
 *   S_{i+1} = S_i * exp((drift - 0.5*sigma^2)*dt + sigma*sqrt(dt)*Z_i)
 *
 * The 0.5*sigma^2 correction (Ito term) is essential — without it you
 * are simulating a process with the wrong expected value, which will
 * systematically misprice even simple options.
 *
 * Returns a vector of length numSteps_ containing spot at each step.
 * For plain European options the caller only uses the last element.
 */
std::vector<double> MonteCarloModel::generatePath(double spot,
                                                   double dt,
                                                   double drift,
                                                   double vol) const {
    std::vector<double> path(numSteps_);  // <-- heap allocation, Iteration 3 target
    double S = spot;
    const double volSqrtDt = vol * std::sqrt(dt);

    for (int i = 0; i < numSteps_; ++i) {
        const double Z = dist_(rng_);
        S *= std::exp(drift * dt + volSqrtDt * Z);
        path[i] = S;
    }
    return path;
}

PricingResult MonteCarloModel::price(const Option& option,
                                      const MarketData& market) const {
    const double S     = market.spot;
    const double r     = market.riskFreeRate;
    const double sigma = market.volatility;
    const double T     = option.expiry();

    if (T <= 0.0)     throw std::domain_error("MC: expiry must be positive");
    if (S <= 0.0)     throw std::domain_error("MC: spot must be positive");
    if (sigma <= 0.0) throw std::domain_error("MC: volatility must be positive");

    const double dt       = T / numSteps_;
    const double drift    = r - 0.5 * sigma * sigma;  // risk-neutral drift with Ito correction
    const double discount = std::exp(-r * T);

    PricingResult result;
    result.delta = 0.0;  // bump-and-reprice delta is possible but deferred

    if (varReduction_ == VarianceReduction::Antithetic) {
        /**
         * Antithetic variates: for each draw Z ~ N(0,1) simulate two paths,
         * one using +Z and one using −Z. The payoff sample is their average:
         *
         *   y_i = (payoff(S_T(+Z)) + payoff(S_T(−Z))) / 2
         *
         * For monotone payoffs (calls, puts) f(+Z) and f(−Z) are negatively
         * correlated, so Var(y_i) < Var(f(Z)), giving a tighter price estimate
         * for the same number of RNG draws.
         *
         * numPaths_ is treated as the total effective path count; we draw
         * numPaths_/2 pairs. If numPaths_ is odd the last path is dropped.
         */
        const int numPairs = numPaths_ / 2;
        std::vector<double> samples(static_cast<size_t>(numPairs));

        const double volSqrtDt = sigma * std::sqrt(dt);

        for (int i = 0; i < numPairs; ++i) {
            double Spos = S, Sneg = S;
            for (int step = 0; step < numSteps_; ++step) {
                const double Z   = dist_(rng_);
                const double fwd = drift * dt + volSqrtDt * Z;
                Spos *= std::exp(fwd);
                Sneg *= std::exp(drift * dt - volSqrtDt * Z);  // same |Z|, opposite sign
            }
            samples[static_cast<size_t>(i)] =
                0.5 * (option.payoff(Spos) + option.payoff(Sneg));
        }

        const double mean = std::accumulate(samples.begin(), samples.end(), 0.0)
                            / static_cast<double>(numPairs);

        double variance = 0.0;
        for (double y : samples) {
            const double d = y - mean;
            variance += d * d;
        }
        variance /= static_cast<double>(numPairs - 1);  // Bessel's correction

        result.modelName = "MonteCarlo+AV";
        result.price     = discount * mean;
        // stdErr is over numPairs pair-samples (each already an average of two paths)
        result.stdErr    = discount * std::sqrt(variance / static_cast<double>(numPairs));

    } else {
        // ── Plain Monte Carlo ─────────────────────────────────────────────────
        std::vector<double> payoffs(static_cast<size_t>(numPaths_));

        for (int i = 0; i < numPaths_; ++i) {
            const auto   path         = generatePath(S, dt, drift, sigma);
            payoffs[static_cast<size_t>(i)] = option.payoff(path.back());
        }

        const double mean = std::accumulate(payoffs.begin(), payoffs.end(), 0.0)
                            / static_cast<double>(numPaths_);

        double variance = 0.0;
        for (double p : payoffs) {
            const double d = p - mean;
            variance += d * d;
        }
        variance /= static_cast<double>(numPaths_ - 1);  // Bessel's correction

        result.modelName = "MonteCarlo";
        result.price     = discount * mean;
        result.stdErr    = discount * std::sqrt(variance / static_cast<double>(numPaths_));
    }

    return result;
}

} // namespace options
