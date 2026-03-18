#include <gtest/gtest.h>
#include "options/EuropeanOption.h"
#include "models/BlackScholesModel.h"
#include "utils/MarketData.h"
#include "utils/ImpliedVol.h"

#include <cmath>

using namespace options;

// TC17: ATM call round-trip — price with BS at σ=20%, recover via IV solver
TEST(ImpliedVolTest, ATMCallRoundTrip) {
    MarketData m{ 100.0, 0.05, 0.20 };
    BlackScholesModel bs;
    EuropeanOption call(100.0, 1.0, OptionType::Call);
    const double mktPrice = bs.price(call, m).price;
    const auto iv = solveIV(mktPrice, OptionType::Call, 100.0, 100.0, 0.05, 1.0);
    EXPECT_NEAR(iv.impliedVol, 0.20, 1e-6);
}

// TC18: ATM put round-trip
TEST(ImpliedVolTest, ATMPutRoundTrip) {
    MarketData m{ 100.0, 0.05, 0.20 };
    BlackScholesModel bs;
    EuropeanOption put(100.0, 1.0, OptionType::Put);
    const double mktPrice = bs.price(put, m).price;
    const auto iv = solveIV(mktPrice, OptionType::Put, 100.0, 100.0, 0.05, 1.0);
    EXPECT_NEAR(iv.impliedVol, 0.20, 1e-6);
}

// TC19: Deep ITM call (S=120, K=100, σ=30%)
TEST(ImpliedVolTest, DeepITMCallRoundTrip) {
    MarketData m{ 120.0, 0.05, 0.30 };
    BlackScholesModel bs;
    EuropeanOption call(100.0, 1.0, OptionType::Call);
    const double mktPrice = bs.price(call, m).price;
    const auto iv = solveIV(mktPrice, OptionType::Call, 120.0, 100.0, 0.05, 1.0);
    EXPECT_NEAR(iv.impliedVol, 0.30, 1e-6);
}

// TC20: Deep OTM put (S=120, K=100, σ=30%)
TEST(ImpliedVolTest, DeepOTMPutRoundTrip) {
    MarketData m{ 120.0, 0.05, 0.30 };
    BlackScholesModel bs;
    EuropeanOption put(100.0, 1.0, OptionType::Put);
    const double mktPrice = bs.price(put, m).price;
    const auto iv = solveIV(mktPrice, OptionType::Put, 120.0, 100.0, 0.05, 1.0);
    EXPECT_NEAR(iv.impliedVol, 0.30, 1e-6);
}

// TC21: High volatility round-trip (σ=80%)
TEST(ImpliedVolTest, HighVolRoundTrip) {
    MarketData m{ 100.0, 0.05, 0.80 };
    BlackScholesModel bs;
    EuropeanOption call(100.0, 1.0, OptionType::Call);
    const double mktPrice = bs.price(call, m).price;
    const auto iv = solveIV(mktPrice, OptionType::Call, 100.0, 100.0, 0.05, 1.0);
    EXPECT_NEAR(iv.impliedVol, 0.80, 1e-6);
}

// TC22: Price below intrinsic value — solver must not converge
TEST(ImpliedVolTest, RejectsSubIntrinsicPrice) {
    const double intrinsic = std::max(100.0 - 80.0 * std::exp(-0.05 * 1.0), 0.0);
    const auto iv = solveIV(intrinsic - 1.0, OptionType::Call, 100.0, 80.0, 0.05, 1.0);
    EXPECT_FALSE(iv.converged) << "Solver should not converge on a sub-intrinsic price";
}
