#include <gtest/gtest.h>
#include <cmath>
#include "options/EuropeanOption.h"
#include "models/BlackScholesModel.h"
#include "utils/MarketData.h"

using namespace options;

class BlackScholesTest : public ::testing::Test {
protected:
    BlackScholesModel bs;
};

// TC1: ATM call — reference values
// S=100 K=100 r=5% σ=20% T=1yr → price=10.450584, delta=0.636831
TEST_F(BlackScholesTest, ATMCall) {
    MarketData m{ 100.0, 0.05, 0.20 };
    EuropeanOption call(100.0, 1.0, OptionType::Call);
    auto r = bs.price(call, m);
    EXPECT_NEAR(r.price,  10.450584, 1e-4);
    EXPECT_NEAR(r.delta,   0.636831, 1e-4);
    EXPECT_NEAR(r.stdErr,  0.0,      1e-15);
}

// TC2: ATM put — reference values
// S=100 K=100 r=5% σ=20% T=1yr → price=5.573526, delta=-0.363169
TEST_F(BlackScholesTest, ATMPut) {
    MarketData m{ 100.0, 0.05, 0.20 };
    EuropeanOption put(100.0, 1.0, OptionType::Put);
    auto r = bs.price(put, m);
    EXPECT_NEAR(r.price,  5.573526, 1e-4);
    EXPECT_NEAR(r.delta, -0.363169, 1e-4);
}

// TC3: Put-call parity — ATM
// C - P = S - K·e^(-rT) must hold to near machine precision
TEST_F(BlackScholesTest, PutCallParityATM) {
    MarketData m{ 100.0, 0.05, 0.20 };
    EuropeanOption call(100.0, 1.0, OptionType::Call);
    EuropeanOption put (100.0, 1.0, OptionType::Put);
    const double C      = bs.price(call, m).price;
    const double P      = bs.price(put,  m).price;
    const double theory = 100.0 - 100.0 * std::exp(-0.05 * 1.0);
    EXPECT_NEAR(C - P, theory, 1e-12);
}

// TC4: Deep ITM call — delta close to 1
// S=120 K=100 r=5% σ=20% T=1yr
TEST_F(BlackScholesTest, DeepITMCall) {
    MarketData m{ 120.0, 0.05, 0.20 };
    EuropeanOption call(100.0, 1.0, OptionType::Call);
    auto r = bs.price(call, m);
    EXPECT_NEAR(r.price, 26.169044, 1e-4);
    EXPECT_NEAR(r.delta,  0.896455, 1e-4);
}

// TC5: Deep OTM call — delta close to 0
// S=80 K=100 r=5% σ=20% T=1yr
TEST_F(BlackScholesTest, DeepOTMCall) {
    MarketData m{ 80.0, 0.05, 0.20 };
    EuropeanOption call(100.0, 1.0, OptionType::Call);
    auto r = bs.price(call, m);
    EXPECT_NEAR(r.price, 1.859420, 1e-4);
    EXPECT_NEAR(r.delta, 0.221922, 1e-4);
}

// TC6: Short expiry T=0.25yr
TEST_F(BlackScholesTest, ShortExpiry) {
    MarketData m{ 100.0, 0.05, 0.20 };
    EuropeanOption call(100.0, 0.25, OptionType::Call);
    EXPECT_NEAR(bs.price(call, m).price, 4.614997, 1e-4);
}

// TC7: Long expiry T=5yr
TEST_F(BlackScholesTest, LongExpiry) {
    MarketData m{ 100.0, 0.05, 0.20 };
    EuropeanOption call(100.0, 5.0, OptionType::Call);
    EXPECT_NEAR(bs.price(call, m).price, 29.138620, 1e-4);
}

// TC8: High volatility σ=50%
TEST_F(BlackScholesTest, HighVol) {
    MarketData m{ 100.0, 0.05, 0.50 };
    EuropeanOption call(100.0, 1.0, OptionType::Call);
    EXPECT_NEAR(bs.price(call, m).price, 21.792604, 1e-4);
}

// TC9: Zero risk-free rate — ATM put-call parity: C - P = S - K = 0
TEST_F(BlackScholesTest, ZeroRate) {
    MarketData m{ 100.0, 0.0, 0.20 };
    EuropeanOption call(100.0, 1.0, OptionType::Call);
    EuropeanOption put (100.0, 1.0, OptionType::Put);
    const double C = bs.price(call, m).price;
    const double P = bs.price(put,  m).price;
    EXPECT_NEAR(C,     7.965567, 1e-4);
    EXPECT_NEAR(C - P, 0.0,      1e-10);
}

// TC10: Put-call parity — deep ITM
TEST_F(BlackScholesTest, PutCallParityDeepITM) {
    MarketData m{ 120.0, 0.05, 0.20 };
    EuropeanOption call(100.0, 1.0, OptionType::Call);
    EuropeanOption put (100.0, 1.0, OptionType::Put);
    const double C      = bs.price(call, m).price;
    const double P      = bs.price(put,  m).price;
    const double theory = 120.0 - 100.0 * std::exp(-0.05 * 1.0);
    EXPECT_NEAR(C - P, theory, 1e-12);
}

// TC11: delta_call - delta_put = 1 (put-call parity differentiated w.r.t. S)
TEST_F(BlackScholesTest, DeltaRelationship) {
    MarketData m{ 100.0, 0.05, 0.20 };
    EuropeanOption call(100.0, 1.0, OptionType::Call);
    EuropeanOption put (100.0, 1.0, OptionType::Put);
    const double dCall = bs.price(call, m).delta;
    const double dPut  = bs.price(put,  m).delta;
    EXPECT_NEAR(dCall - dPut, 1.0, 1e-10);
}
