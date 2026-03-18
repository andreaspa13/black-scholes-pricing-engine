#include <gtest/gtest.h>
#include "options/EuropeanOption.h"
#include "models/BlackScholesModel.h"
#include "models/MonteCarloModel.h"
#include "utils/MarketData.h"
#include "v2/BlackScholesV2.h"
#include "v2/MonteCarloV2.h"

using namespace options;

// TC23–TC24: V2 BS must be numerically identical to V1 to machine precision.
// Any divergence would indicate a formula regression, not a performance win.

TEST(V2RegressionTest, BSCallMatchesV1) {
    MarketData m{ 100.0, 0.05, 0.20 };
    EuropeanOption call(100.0, 1.0, OptionType::Call);
    BlackScholesModel    bsV1;
    v2::BlackScholesModel bsV2;
    const auto v1 = bsV1.price(call, m);
    const auto v2 = bsV2.price(call, m);
    EXPECT_NEAR(v2.price, v1.price, 1e-14);
    EXPECT_NEAR(v2.delta, v1.delta, 1e-14);
    EXPECT_NEAR(v2.gamma, v1.gamma, 1e-14);
    EXPECT_NEAR(v2.vega,  v1.vega,  1e-14);
    EXPECT_NEAR(v2.theta, v1.theta, 1e-14);
    EXPECT_NEAR(v2.rho,   v1.rho,   1e-14);
}

TEST(V2RegressionTest, BSPutMatchesV1) {
    MarketData m{ 100.0, 0.05, 0.20 };
    EuropeanOption put(100.0, 1.0, OptionType::Put);
    BlackScholesModel    bsV1;
    v2::BlackScholesModel bsV2;
    const auto v1 = bsV1.price(put, m);
    const auto v2 = bsV2.price(put, m);
    EXPECT_NEAR(v2.price, v1.price, 1e-14);
    EXPECT_NEAR(v2.delta, v1.delta, 1e-14);
}

// TC25: V2 MC call converges to BS within 3 standard errors
TEST(V2RegressionTest, MCCallConvergesToBS) {
    MarketData m{ 100.0, 0.05, 0.20 };
    EuropeanOption call(100.0, 1.0, OptionType::Call);
    BlackScholesModel bsV1;
    v2::MonteCarloModel mcV2(500'000, 1, 42);
    const auto v2MC  = mcV2.price(call, m);
    const double bsPrice = bsV1.price(call, m).price;
    const double devs = std::abs(v2MC.price - bsPrice) / v2MC.stdErr;
    EXPECT_LT(devs, 3.0) << "V2 MC call " << devs << " sigma from BS";
}

// TC26: V2 MC price lies within 5 stdErrs of V1 MC price
// They use the same seed but V2 inlines path simulation, producing a slightly
// different RNG sequence — so exact equality is not expected.
TEST(V2RegressionTest, V2MCWithin5SigmaOfV1MC) {
    MarketData m{ 100.0, 0.05, 0.20 };
    EuropeanOption call(100.0, 1.0, OptionType::Call);
    MonteCarloModel    mcV1(500'000, 1, 42);
    v2::MonteCarloModel mcV2(500'000, 1, 42);
    const auto v1MC = mcV1.price(call, m);
    const auto v2MC = mcV2.price(call, m);
    const double devs = std::abs(v2MC.price - v1MC.price) / v2MC.stdErr;
    EXPECT_LT(devs, 5.0) << "V2 MC price " << devs << " sigma from V1 MC price";
}
