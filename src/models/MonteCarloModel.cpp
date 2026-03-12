#include "models/MonteCarloModel.h"
#include "options/Option.h"

#include <cmath>
#include <numeric>   // std::accumulate
#include <stdexcept>

namespace options {

MonteCarloModel::MonteCarloModel(int numPaths, int numSteps, unsigned seed)
    : numPaths_(numPaths)
    , numSteps_(numSteps)
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

    const double dt    = T / numSteps_;
    const double drift = r - 0.5 * sigma * sigma;  // risk-neutral drift with Ito correction

    // Collect discounted payoffs across all paths
    std::vector<double> payoffs(numPaths_);

    for (int i = 0; i < numPaths_; ++i) {
        const auto path          = generatePath(S, dt, drift, sigma);
        const double terminalSpot = path.back();
        payoffs[i]               = option.payoff(terminalSpot);
    }

    // Price = e^(-rT) * E[payoff]  under risk-neutral measure
    const double mean = std::accumulate(payoffs.begin(), payoffs.end(), 0.0)
                        / static_cast<double>(numPaths_);

    // Standard error of the mean estimator: stddev / sqrt(N)
    // Gives a confidence interval on the MC price estimate.
    double variance = 0.0;
    for (double p : payoffs) {
        const double diff = p - mean;
        variance += diff * diff;
    }
    variance /= static_cast<double>(numPaths_ - 1);  // Bessel's correction

    const double discount = std::exp(-r * T);

    PricingResult result;
    result.modelName = "MonteCarlo";
    result.price     = discount * mean;
    result.stdErr    = discount * std::sqrt(variance / numPaths_);
    result.delta     = 0.0;  // bump-and-reprice delta is possible but deferred

    return result;
}

} // namespace options
