#include <gtest/gtest.h>
#include "options/EuropeanOption.h"
#include "models/BlackScholesModel.h"
#include "utils/MarketData.h"
#include "v2/MonteCarloV2.h"
#include "v3/MonteCarloV3.h"

using namespace options;

// TC27: V3 single-thread converges to BS within 3 standard errors
TEST(V3RegressionTest, SingleThreadConvergesToBS) {
    MarketData m{ 100.0, 0.05, 0.20 };
    EuropeanOption call(100.0, 1.0, OptionType::Call);
    BlackScholesModel bs;
    v3::MonteCarloModel mcV3(500'000, 1, 42, VarianceReduction::None, 1);
    const auto res = mcV3.price(call, m);
    const double bsPrice = bs.price(call, m).price;
    const double devs = std::abs(res.price - bsPrice) / res.stdErr;
    EXPECT_LT(devs, 3.0) << "V3 single-thread " << devs << " sigma from BS";
}

// TC28: V3 multi-thread converges to BS within 3 standard errors
TEST(V3RegressionTest, MultiThreadConvergesToBS) {
    MarketData m{ 100.0, 0.05, 0.20 };
    EuropeanOption call(100.0, 1.0, OptionType::Call);
    BlackScholesModel bs;
    v3::MonteCarloModel mcV3(500'000, 1, 42);
    const auto res = mcV3.price(call, m);
    const double bsPrice = bs.price(call, m).price;
    const double devs = std::abs(res.price - bsPrice) / res.stdErr;
    EXPECT_LT(devs, 3.0) << "V3 multi-thread " << devs << " sigma from BS";
}

// TC29: V3 stdErr is within 2x of V2 stdErr
// Different per-thread seeds give a slightly different variance estimate;
// it should be in the same order of magnitude.
TEST(V3RegressionTest, StdErrInSameBallparkAsV2) {
    MarketData m{ 100.0, 0.05, 0.20 };
    EuropeanOption call(100.0, 1.0, OptionType::Call);
    v2::MonteCarloModel mcV2(500'000, 1, 42);
    v3::MonteCarloModel mcV3(500'000, 1, 42);
    const double v2StdErr = mcV2.price(call, m).stdErr;
    const double v3StdErr = mcV3.price(call, m).stdErr;
    EXPECT_LT(v3StdErr, 2.0 * v2StdErr) << "V3 stdErr should be within 2x of V2";
}
