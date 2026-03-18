#include <gtest/gtest.h>
#include "options/EuropeanOption.h"
#include "models/BlackScholesModel.h"
#include "models/MonteCarloModel.h"
#include "utils/MarketData.h"

using namespace options;

// Helper: asserts MC price lies within maxSigma standard errors of a reference price.
static void ExpectWithinSigma(double mcPrice, double refPrice, double mcStdErr,
                               double maxSigma, const std::string& label) {
    const double devs = std::abs(mcPrice - refPrice) / mcStdErr;
    EXPECT_LT(devs, maxSigma) << label << " — " << devs << " sigma from reference";
}

// TC12–TC13: Plain MC (500k paths) converges to BS for call and put
TEST(MonteCarloTest, CallConvergesToBS) {
    MarketData m{ 100.0, 0.05, 0.20 };
    EuropeanOption call(100.0, 1.0, OptionType::Call);
    BlackScholesModel bs;
    MonteCarloModel mc(500'000, 1, 42);
    const auto mcRes = mc.price(call, m);
    const double bsPrice = bs.price(call, m).price;
    ExpectWithinSigma(mcRes.price, bsPrice, mcRes.stdErr, 3.0, "MC call vs BS");
}

TEST(MonteCarloTest, PutConvergesToBS) {
    MarketData m{ 100.0, 0.05, 0.20 };
    EuropeanOption put(100.0, 1.0, OptionType::Put);
    BlackScholesModel bs;
    MonteCarloModel mc(500'000, 1, 42);
    const auto mcRes = mc.price(put, m);
    const double bsPrice = bs.price(put, m).price;
    ExpectWithinSigma(mcRes.price, bsPrice, mcRes.stdErr, 3.0, "MC put vs BS");
}

// TC14: Negative expiry must throw
TEST(MonteCarloTest, RejectsNegativeExpiry) {
    MarketData m{ 100.0, 0.05, 0.20 };
    EuropeanOption call(100.0, -1.0, OptionType::Call);
    MonteCarloModel mc(1000, 1, 42);
    EXPECT_THROW(mc.price(call, m), std::exception);
}

// TC15: Antithetic MC (500k paths) converges to BS
TEST(MonteCarloTest, AntitheticConvergesToBS) {
    MarketData m{ 100.0, 0.05, 0.20 };
    EuropeanOption call(100.0, 1.0, OptionType::Call);
    BlackScholesModel bs;
    MonteCarloModel mcAV(500'000, 1, 42, VarianceReduction::Antithetic);
    const auto avRes = mcAV.price(call, m);
    const double bsPrice = bs.price(call, m).price;
    ExpectWithinSigma(avRes.price, bsPrice, avRes.stdErr, 3.0, "MC+AV call vs BS");
}

// TC16: Antithetic stdErr < plain MC stdErr for the same path count
TEST(MonteCarloTest, AntitheticStdErrLowerThanPlain) {
    MarketData m{ 100.0, 0.05, 0.20 };
    EuropeanOption call(100.0, 1.0, OptionType::Call);
    MonteCarloModel mcAV   (500'000, 1, 42, VarianceReduction::Antithetic);
    MonteCarloModel mcPlain(500'000, 1, 42);
    const double avStdErr    = mcAV   .price(call, m).stdErr;
    const double plainStdErr = mcPlain.price(call, m).stdErr;
    EXPECT_LT(avStdErr, plainStdErr) << "Antithetic variance reduction should lower stdErr";
}
