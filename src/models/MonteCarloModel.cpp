#include "models/MonteCarloModel.h"
#include "options/Option.h"

#include <cmath>
#include <numeric> 
#include <stdexcept>

namespace options {

MonteCarloModel::MonteCarloModel(int numPaths, int numSteps,unsigned seed, VarianceReduction varReduction)
    : numPaths_(numPaths)
    , numSteps_(numSteps)
    , varReduction_(varReduction)
    , rng_(seed)
    , dist_(0.0, 1.0)
{
    if (numPaths_ <= 0) throw std::invalid_argument("MC: numPaths must be positive");
    if (numSteps_ <= 0) throw std::invalid_argument("MC: numSteps must be positive");
}

std::vector<double> MonteCarloModel::generatePath(double spot, double dt, double drift, double vol) const { // returns vector of spot at each step along the path (one path)
    std::vector<double> path(numSteps_); 
    double S = spot;
    const double volSqrtDt = vol * std::sqrt(dt);

    for (int i = 0; i < numSteps_; ++i) {
        const double Z = dist_(rng_);
        S *= std::exp(drift * dt + volSqrtDt * Z);
        path[i] = S;
    }
    return path;
}

PricingResult MonteCarloModel::price(const Option& option, const MarketData& market) const {
    const double S     = market.spot;
    const double r     = market.riskFreeRate;
    const double sigma = market.volatility;
    const double T     = option.expiry();

    if (T <= 0.0)     throw std::domain_error("MC: expiry must be positive");
    if (S <= 0.0)     throw std::domain_error("MC: spot must be positive");
    if (sigma <= 0.0) throw std::domain_error("MC: volatility must be positive");

    const double dt       = T / numSteps_;
    const double drift    = r - 0.5 * sigma * sigma;  // risk-neutral drift with Ito correction
    const double discount = std::exp(-r * T); // discount factor for present value

    PricingResult result;
    result.delta = 0.0;

    if (varReduction_ == VarianceReduction::Antithetic) { // Antithetic variates
        const int numPairs = numPaths_ / 2;
        std::vector<double> samples(static_cast<size_t>(numPairs));

        const double volSqrtDt = sigma * std::sqrt(dt);

        for (int i = 0; i < numPairs; ++i) {
            double Spos = S, Sneg = S;
            for (int step = 0; step < numSteps_; ++step) {
                const double Z   = dist_(rng_);
                const double fwd = drift * dt + volSqrtDt * Z;
                Spos *= std::exp(fwd);
                Sneg *= std::exp(drift * dt - volSqrtDt * Z);  
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

    } else { // Classic Monte Carlo
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

} 
