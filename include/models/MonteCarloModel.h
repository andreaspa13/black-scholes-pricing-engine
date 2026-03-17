#pragma once

#include "models/PricingModel.h"

#include <random>
#include <vector>

namespace options {

enum class VarianceReduction {
    None,       // plain Monte Carlo, one path per random draw
    Antithetic  // antithetic variates: each draw Z generates paths for +Z and −Z
};

class MonteCarloModel : public PricingModel {
public:
    explicit MonteCarloModel(int numPaths = 100000, // no. of simulated paths
                             int numSteps = 1, // time steps per path; 1 for European options
                             unsigned seed = 42,    // reproducibility
                             VarianceReduction varReduction = VarianceReduction::None); 

    PricingResult price(const Option& option, const MarketData& market) const override;

private:
    int numPaths_;
    int numSteps_;
    VarianceReduction varReduction_;

    mutable std::mt19937 rng_; // Mersenne Twister pseudo-random generator; mutable for logical constness of price()
    mutable std::normal_distribution<double> dist_;

    std::vector<double> generatePath(double spot, double dt, double drift, double vol) const;
};

} 
