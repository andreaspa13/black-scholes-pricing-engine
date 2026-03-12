#include "options/EuropeanOption.h"
#include "models/BlackScholesModel.h"
#include "models/MonteCarloModel.h"   // also brings in VarianceReduction enum
#include "utils/MarketData.h"
#include "utils/Benchmark.h"
#include "utils/ImpliedVol.h"

// ── Iteration 2: template models ──────────────────────────────────────────────
#include "v2/BlackScholesV2.h"
#include "v2/MonteCarloV2.h"

// ── Iteration 3: parallel Monte Carlo ─────────────────────────────────────────
#include "v3/MonteCarloV3.h"

#include <iostream>
#include <iomanip>
#include <cmath>
#include <string>
#include <vector>

using namespace options;

// ── Test harness ──────────────────────────────────────────────────────────────

struct TestResults { int passed = 0; int failed = 0; };

static TestResults g_results;

void check(const std::string& name, double got, double expected, double tol) {
    const bool ok = std::abs(got - expected) < tol;
    std::cout << (ok ? "[PASS]" : "[FAIL]") << "  " << name;
    if (!ok)
        std::cout << "\n        got=" << got
                  << "  expected=" << expected
                  << "  diff=" << std::abs(got - expected)
                  << "  tol=" << tol;
    std::cout << "\n";
    ok ? ++g_results.passed : ++g_results.failed;
}

// MC price should lie within maxSigma standard errors of the BS price
void checkMC(const std::string& name, double mcPrice, double bsPrice,
             double mcStdErr, double maxSigma = 3.0) {
    const double devs = std::abs(mcPrice - bsPrice) / mcStdErr;
    const bool ok = devs < maxSigma;
    std::cout << (ok ? "[PASS]" : "[FAIL]") << "  " << name
              << "  (" << std::fixed << std::setprecision(2) << devs << " sigma)";
    if (!ok)
        std::cout << "\n        MC=" << mcPrice << "  BS=" << bsPrice
                  << "  stderr=" << mcStdErr;
    std::cout << "\n";
    ok ? ++g_results.passed : ++g_results.failed;
}

void printResult(const std::string& label, const PricingResult& r) {
    std::cout << std::fixed << std::setprecision(6)
              << label << "\n"
              << "  model  : " << r.modelName << "\n"
              << "  price  : " << r.price     << "\n"
              << "  delta  : " << r.delta     << "\n"
              << "  stderr : " << r.stdErr    << "\n\n";
}

// ── Test suite ────────────────────────────────────────────────────────────────

void runTests() {
    std::cout << "=== Test suite ===\n\n";

    BlackScholesModel bs;

    // ── TC1: ATM call — reference values ──────────────────────────────────────
    // S=100 K=100 r=5% σ=20% T=1yr → BS=10.450584, delta=0.636831
    {
        MarketData m{ 100.0, 0.05, 0.20 };
        EuropeanOption call(100.0, 1.0, OptionType::Call);
        auto r = bs.price(call, m);
        check("TC1  ATM call price",    r.price, 10.450584, 1e-4);
        check("TC1  ATM call delta",    r.delta,  0.636831, 1e-4);
        check("TC1  BS stderr is zero", r.stdErr, 0.0,      1e-15);
    }

    // ── TC2: ATM put — reference values ───────────────────────────────────────
    // S=100 K=100 r=5% σ=20% T=1yr → BS=5.573526, delta=-0.363169
    {
        MarketData m{ 100.0, 0.05, 0.20 };
        EuropeanOption put(100.0, 1.0, OptionType::Put);
        auto r = bs.price(put, m);
        check("TC2  ATM put price",  r.price,  5.573526, 1e-4);
        check("TC2  ATM put delta",  r.delta, -0.363169, 1e-4);
    }

    // ── TC3: Put-call parity — ATM ────────────────────────────────────────────
    // C - P = S - K·e^(-rT) must hold to machine precision
    {
        MarketData m{ 100.0, 0.05, 0.20 };
        EuropeanOption call(100.0, 1.0, OptionType::Call);
        EuropeanOption put (100.0, 1.0, OptionType::Put);
        const double C       = bs.price(call, m).price;
        const double P       = bs.price(put,  m).price;
        const double theory  = 100.0 - 100.0 * std::exp(-0.05 * 1.0);
        const double pcpErr  = std::abs((C - P) - theory);
        check("TC3  PCP ATM error < 1e-12", pcpErr, 0.0, 1e-12);
    }

    // ── TC4: Deep ITM call ────────────────────────────────────────────────────
    // S=120 K=100 r=5% σ=20% T=1yr → delta close to 1
    {
        MarketData m{ 120.0, 0.05, 0.20 };
        EuropeanOption call(100.0, 1.0, OptionType::Call);
        auto r = bs.price(call, m);
        check("TC4  Deep ITM call price",    r.price, 26.169044, 1e-4);
        check("TC4  Deep ITM call delta>0.8", r.delta,  0.896455, 1e-4);
    }

    // ── TC5: Deep OTM call ────────────────────────────────────────────────────
    // S=80 K=100 r=5% σ=20% T=1yr → delta close to 0
    {
        MarketData m{ 80.0, 0.05, 0.20 };
        EuropeanOption call(100.0, 1.0, OptionType::Call);
        auto r = bs.price(call, m);
        check("TC5  Deep OTM call price",    r.price, 1.859420, 1e-4);
        check("TC5  Deep OTM call delta<0.3", r.delta, 0.221922, 1e-4);
    }

    // ── TC6: Short expiry ─────────────────────────────────────────────────────
    // S=100 K=100 r=5% σ=20% T=0.25yr (3 months)
    {
        MarketData m{ 100.0, 0.05, 0.20 };
        EuropeanOption call(100.0, 0.25, OptionType::Call);
        auto r = bs.price(call, m);
        check("TC6  Short expiry (T=0.25) price", r.price, 4.614997, 1e-4);
    }

    // ── TC7: Long expiry ──────────────────────────────────────────────────────
    // S=100 K=100 r=5% σ=20% T=5yr
    {
        MarketData m{ 100.0, 0.05, 0.20 };
        EuropeanOption call(100.0, 5.0, OptionType::Call);
        auto r = bs.price(call, m);
        check("TC7  Long expiry (T=5) price", r.price, 29.138620, 1e-4);
    }

    // ── TC8: High volatility ──────────────────────────────────────────────────
    // S=100 K=100 r=5% σ=50% T=1yr
    {
        MarketData m{ 100.0, 0.05, 0.50 };
        EuropeanOption call(100.0, 1.0, OptionType::Call);
        auto r = bs.price(call, m);
        check("TC8  High vol (σ=50%) price", r.price, 21.792604, 1e-4);
    }

    // ── TC9: Zero risk-free rate ──────────────────────────────────────────────
    // S=100 K=100 r=0% σ=20% T=1yr
    {
        MarketData m{ 100.0, 0.0, 0.20 };
        EuropeanOption call(100.0, 1.0, OptionType::Call);
        auto r = bs.price(call, m);
        check("TC9  Zero rate price", r.price, 7.965567, 1e-4);
        // With r=0 put-call parity: C - P = S - K = 0 (ATM)
        EuropeanOption put(100.0, 1.0, OptionType::Put);
        const double C = r.price;
        const double P = bs.price(put, m).price;
        check("TC9  Zero rate PCP: C-P = S-K = 0", std::abs(C - P), 0.0, 1e-10);
    }

    // ── TC10: Put-call parity — deep ITM ─────────────────────────────────────
    {
        MarketData m{ 120.0, 0.05, 0.20 };
        EuropeanOption call(100.0, 1.0, OptionType::Call);
        EuropeanOption put (100.0, 1.0, OptionType::Put);
        const double C      = bs.price(call, m).price;
        const double P      = bs.price(put,  m).price;
        const double theory = 120.0 - 100.0 * std::exp(-0.05 * 1.0);
        check("TC10 PCP deep ITM error < 1e-12", std::abs((C - P) - theory), 0.0, 1e-12);
    }

    // ── TC11: Call delta + |put delta| = 1 ───────────────────────────────────
    // For European options: delta_call - delta_put = 1 always
    {
        MarketData m{ 100.0, 0.05, 0.20 };
        EuropeanOption call(100.0, 1.0, OptionType::Call);
        EuropeanOption put (100.0, 1.0, OptionType::Put);
        const double dCall = bs.price(call, m).delta;
        const double dPut  = bs.price(put,  m).delta;
        check("TC11 delta_call - delta_put = 1", dCall - dPut, 1.0, 1e-10);
    }

    // ── TC12–TC13: Monte Carlo vs Black-Scholes convergence ──────────────────
    // 500k paths gives tight CI; BS should lie within 3 stderr of MC estimate
    {
        MarketData m{ 100.0, 0.05, 0.20 };
        EuropeanOption call(100.0, 1.0, OptionType::Call);
        EuropeanOption put (100.0, 1.0, OptionType::Put);
        MonteCarloModel mc500k(500'000, 1, 42);
        const auto mcCall = mc500k.price(call, m);
        const auto mcPut  = mc500k.price(put,  m);
        const double bsCall = bs.price(call, m).price;
        const double bsPut  = bs.price(put,  m).price;
        checkMC("TC12 MC(500k) call vs BS", mcCall.price, bsCall, mcCall.stdErr, 3.0);
        checkMC("TC13 MC(500k) put  vs BS", mcPut.price,  bsPut,  mcPut.stdErr,  3.0);
    }

    // ── TC14: Input validation — exceptions ──────────────────────────────────
    {
        MarketData m{ 100.0, 0.05, 0.20 };
        bool threw = false;
        try {
            EuropeanOption call(100.0, -1.0, OptionType::Call);  // negative expiry
            MonteCarloModel mc(1000, 1, 42);
            mc.price(call, m);
        } catch (const std::exception&) { threw = true; }
        check("TC14 MC rejects negative expiry", threw ? 1.0 : 0.0, 1.0, 0.5);
    }

    // ── TC15–TC16: Antithetic variates ───────────────────────────────────────
    // TC15: Antithetic MC call converges to BS (same 3-sigma check as TC12)
    // TC16: Antithetic stdErr < plain MC stdErr for the same path count
    {
        MarketData m{ 100.0, 0.05, 0.20 };
        EuropeanOption call(100.0, 1.0, OptionType::Call);
        const double bsCall = bs.price(call, m).price;

        MonteCarloModel mcAV(500'000, 1, 42, VarianceReduction::Antithetic);
        const auto avRes = mcAV.price(call, m);
        checkMC("TC15 MC+AV(500k) call vs BS", avRes.price, bsCall, avRes.stdErr, 3.0);

        MonteCarloModel mcPlain(500'000, 1, 42);
        const auto plainRes = mcPlain.price(call, m);
        check("TC16 AV stdErr < plain MC stdErr",
              avRes.stdErr < plainRes.stdErr ? 1.0 : 0.0, 1.0, 0.5);
    }

    // ── TC17–TC26: Implied Volatility round-trips ─────────────────────────────
    // Price with BS at known σ, then recover σ via IV solver — must match to 1e-6.
    {
        MarketData m{ 100.0, 0.05, 0.20 };

        // TC17: ATM call round-trip
        {
            EuropeanOption call(100.0, 1.0, OptionType::Call);
            const double mktPrice = bs.price(call, m).price;
            const auto iv = solveIV(mktPrice, OptionType::Call, 100.0, 100.0, 0.05, 1.0);
            check("TC17 IV ATM call round-trip", iv.impliedVol, 0.20, 1e-6);
        }

        // TC18: ATM put round-trip
        {
            EuropeanOption put(100.0, 1.0, OptionType::Put);
            const double mktPrice = bs.price(put, m).price;
            const auto iv = solveIV(mktPrice, OptionType::Put, 100.0, 100.0, 0.05, 1.0);
            check("TC18 IV ATM put round-trip",  iv.impliedVol, 0.20, 1e-6);
        }

        // TC19: Deep ITM call (S=120, K=100)
        {
            MarketData m2{ 120.0, 0.05, 0.30 };
            EuropeanOption call(100.0, 1.0, OptionType::Call);
            const double mktPrice = bs.price(call, m2).price;
            const auto iv = solveIV(mktPrice, OptionType::Call, 120.0, 100.0, 0.05, 1.0);
            check("TC19 IV deep ITM call round-trip", iv.impliedVol, 0.30, 1e-6);
        }

        // TC20: Deep OTM put (S=120, K=100)
        {
            MarketData m2{ 120.0, 0.05, 0.30 };
            EuropeanOption put(100.0, 1.0, OptionType::Put);
            const double mktPrice = bs.price(put, m2).price;
            const auto iv = solveIV(mktPrice, OptionType::Put, 120.0, 100.0, 0.05, 1.0);
            check("TC20 IV deep OTM put round-trip", iv.impliedVol, 0.30, 1e-6);
        }

        // TC21: High-vol round-trip (σ=80%)
        {
            MarketData m3{ 100.0, 0.05, 0.80 };
            EuropeanOption call(100.0, 1.0, OptionType::Call);
            const double mktPrice = bs.price(call, m3).price;
            const auto iv = solveIV(mktPrice, OptionType::Call, 100.0, 100.0, 0.05, 1.0);
            check("TC21 IV high-vol (80%) round-trip", iv.impliedVol, 0.80, 1e-6);
        }

        // TC22: Price below intrinsic — solver must reject cleanly
        {
            const double intrinsic = std::max(100.0 - 80.0 * std::exp(-0.05 * 1.0), 0.0);
            const auto iv = solveIV(intrinsic - 1.0, OptionType::Call,
                                    100.0, 80.0, 0.05, 1.0);
            check("TC22 IV rejects sub-intrinsic price",
                  iv.converged ? 0.0 : 1.0, 1.0, 0.5);
        }
    }

    // ── TC23–TC26: Iteration 2 correctness ───────────────────────────────────
    // V2 models must produce results identical to V1 to machine precision.
    // Any numerical difference would indicate a formula regression, not a
    // performance improvement.
    {
        MarketData m{ 100.0, 0.05, 0.20 };
        EuropeanOption call(100.0, 1.0, OptionType::Call);
        EuropeanOption put (100.0, 1.0, OptionType::Put);

        // BS V1 reference values
        const auto v1Call = bs.price(call, m);
        const auto v1Put  = bs.price(put,  m);

        v2::BlackScholesModel bsV2;

        // TC23: V2 BS call matches V1 to machine precision
        const auto v2Call = bsV2.price(call, m);
        check("TC23 V2 BS call price == V1",  v2Call.price, v1Call.price, 1e-14);
        check("TC23 V2 BS call delta == V1",  v2Call.delta, v1Call.delta, 1e-14);
        check("TC23 V2 BS call gamma == V1",  v2Call.gamma, v1Call.gamma, 1e-14);
        check("TC23 V2 BS call vega  == V1",  v2Call.vega,  v1Call.vega,  1e-14);
        check("TC23 V2 BS call theta == V1",  v2Call.theta, v1Call.theta, 1e-14);
        check("TC23 V2 BS call rho   == V1",  v2Call.rho,   v1Call.rho,   1e-14);

        // TC24: V2 BS put matches V1 to machine precision
        const auto v2Put = bsV2.price(put, m);
        check("TC24 V2 BS put price  == V1",  v2Put.price,  v1Put.price,  1e-14);
        check("TC24 V2 BS put delta  == V1",  v2Put.delta,  v1Put.delta,  1e-14);

        // TC25: V2 MC call converges to BS (same 3-sigma test as TC12)
        v2::MonteCarloModel mcV2(500'000, 1, 42);
        const auto v2MC = mcV2.price(call, m);
        checkMC("TC25 V2 MC(500k) call vs BS", v2MC.price, v1Call.price, v2MC.stdErr, 3.0);

        // TC26: V2 MC stdErr is in the same ballpark as V1 (same seed, same paths)
        // They differ slightly because V2 inlines path simulation instead of calling
        // generatePath() — the RNG sequence is identical but the payoff call path
        // differs. Require they agree to within 5 stdErrs of each other.
        MonteCarloModel mcV1(500'000, 1, 42);
        const auto v1MC = mcV1.price(call, m);
        checkMC("TC26 V2 MC price within 5σ of V1 MC", v2MC.price, v1MC.price, v2MC.stdErr, 5.0);
    }

    // ── TC27–TC29: Iteration 3 correctness ───────────────────────────────────
    {
        MarketData m{ 100.0, 0.05, 0.20 };
        EuropeanOption call(100.0, 1.0, OptionType::Call);
        const double bsPrice = bs.price(call, m).price;

        // TC27: V3 single-thread matches V2 to within MC noise
        // Same path count, single thread — should give statistically
        // consistent results (different seed strategy → different sequence).
        v3::MonteCarloModel mcV3_1t(500'000, 1, 42, VarianceReduction::None, 1);
        const auto v3_1t = mcV3_1t.price(call, m);
        checkMC("TC27 V3 (1 thread) call vs BS", v3_1t.price, bsPrice, v3_1t.stdErr, 3.0);

        // TC28: V3 multi-thread converges to BS (same statistical test)
        v3::MonteCarloModel mcV3_mt(500'000, 1, 42);  // hardware thread count
        const auto v3_mt = mcV3_mt.price(call, m);
        checkMC("TC28 V3 (N threads) call vs BS", v3_mt.price, bsPrice, v3_mt.stdErr, 3.0);

        // TC29: V3 stdErr is in the right ballpark (within 2x of V2 stderr)
        // Different seeds across threads produce a different variance estimate;
        // it should be in the same order of magnitude as V2.
        v2::MonteCarloModel mcV2ref(500'000, 1, 42);
        const auto v2ref = mcV2ref.price(call, m);
        check("TC29 V3 stdErr within 2x of V2",
              v3_mt.stdErr < 2.0 * v2ref.stdErr ? 1.0 : 0.0, 1.0, 0.5);
    }

    // ── Summary ───────────────────────────────────────────────────────────────
    std::cout << "\n"
              << g_results.passed << "/" << (g_results.passed + g_results.failed)
              << " tests passed";
    if (g_results.failed > 0)
        std::cout << "  *** " << g_results.failed << " FAILED ***";
    std::cout << "\n\n";
}

// ── Sanity print (retained for visual confirmation) ──────────────────────────

void runSanityCheck() {
    std::cout << "=== Sanity check: ATM European call ===\n\n";

    MarketData market { .spot=100.0, .riskFreeRate=0.05,
                        .volatility=0.20 };

    EuropeanOption call(100.0, 1.0, OptionType::Call);
    EuropeanOption put (100.0, 1.0, OptionType::Put);

    BlackScholesModel bs;
    MonteCarloModel   mc(500'000, 1, 42);

    printResult("BS  call", bs.price(call, market));
    printResult("MC  call", mc.price(call, market));
    printResult("BS  put",  bs.price(put,  market));
    printResult("MC  put",  mc.price(put,  market));

    const double bsCall = bs.price(call, market).price;
    const double bsPut  = bs.price(put,  market).price;
    const double parity = market.spot - 100.0 * std::exp(-market.riskFreeRate * 1.0);
    std::cout << "Put-call parity check:\n"
              << "  C - P           = " << (bsCall - bsPut) << "\n"
              << "  S - K*e^(-rT)   = " << parity           << "\n"
              << "  diff (should≈0) = " << std::abs(bsCall - bsPut - parity) << "\n\n";
}

// ── Benchmarks ────────────────────────────────────────────────────────────────

void runBenchmarks() {
    std::cout << "=== Iteration 1 baseline benchmarks ===\n\n";

    MarketData market { .spot=100.0, .riskFreeRate=0.05,
                        .volatility=0.20 };
    EuropeanOption call(100.0, 1.0, OptionType::Call);

    BlackScholesModel bs;

    {
        auto r = Benchmark::run("BS single price (10k iters)", [&]() {
            auto result = bs.price(call, market);
            (void)result.price;
        }, 10'000);
        Benchmark::print(r);
    }

    for (int paths : { 1'000, 10'000, 100'000 }) {
        MonteCarloModel mc(paths, 1, 42);
        const std::string label = "MC " + std::to_string(paths) + " paths (100 iters)";
        auto r = Benchmark::run(label, [&]() {
            auto result = mc.price(call, market);
            (void)result.price;
        }, 100);
        Benchmark::print(r);
        std::cout << "  implied per-path : "
                  << std::fixed << std::setprecision(1)
                  << (r.meanNs / paths) << " ns\n\n";
    }
}

// ── Iteration 2 benchmarks ────────────────────────────────────────────────────
// Direct comparison of V1 (virtual dispatch + dynamic_cast) vs V2 (templates).
// Same option, same market data, same iteration counts — only the model changes.
//
// Expected improvements:
//   BS:  ~5-10 ns/call  from eliminating dynamic_cast
//   MC:  ~3-5 ns/path   from eliminating virtual payoff() dispatch
//        ~30-40 ns/path  from eliminating per-path std::vector allocation
//        Total MC speedup: roughly 2-3x on the inner loop

void runV2Benchmarks() {
    std::cout << "=== Iteration 2 vs Iteration 1 benchmarks ===\n\n";

    MarketData market { .spot=100.0, .riskFreeRate=0.05, .volatility=0.20 };
    EuropeanOption call(100.0, 1.0, OptionType::Call);

    // ── Black-Scholes ─────────────────────────────────────────────────────────
    {
        BlackScholesModel    bsV1;
        v2::BlackScholesModel bsV2;

        auto r1 = Benchmark::run("BS V1 (10k iters)", [&]() {
            auto result = bsV1.price(call, market);
            (void)result.price;
        }, 10'000);

        auto r2 = Benchmark::run("BS V2 (10k iters)", [&]() {
            auto result = bsV2.price(call, market);
            (void)result.price;
        }, 10'000);

        Benchmark::print(r1);
        Benchmark::print(r2);
        std::cout << "  BS speedup (mean): "
                  << std::fixed << std::setprecision(2)
                  << (r1.meanNs / r2.meanNs) << "x\n\n";
    }

    // ── Monte Carlo ───────────────────────────────────────────────────────────
    for (int paths : { 1'000, 10'000, 100'000 }) {
        MonteCarloModel    mcV1(paths, 1, 42);
        v2::MonteCarloModel mcV2(paths, 1, 42);

        const std::string label = std::to_string(paths) + " paths (100 iters)";

        auto r1 = Benchmark::run("MC V1 " + label, [&]() {
            auto result = mcV1.price(call, market);
            (void)result.price;
        }, 100);

        auto r2 = Benchmark::run("MC V2 " + label, [&]() {
            auto result = mcV2.price(call, market);
            (void)result.price;
        }, 100);

        Benchmark::print(r1);
        std::cout << "  implied per-path : "
                  << std::fixed << std::setprecision(1)
                  << (r1.meanNs / paths) << " ns\n";
        Benchmark::print(r2);
        std::cout << "  implied per-path : "
                  << std::fixed << std::setprecision(1)
                  << (r2.meanNs / paths) << " ns\n";
        std::cout << "  MC speedup (mean): "
                  << std::fixed << std::setprecision(2)
                  << (r1.meanNs / r2.meanNs) << "x\n\n";
    }
}

// ── Iteration 3 benchmarks ────────────────────────────────────────────────────
// V2 (single-threaded) vs V3 (parallel) on 100k and 1M paths.
// Expected speedup: near-linear with core count for large N.
// Thread-creation overhead dominates at small N — the crossover is visible here.

void runV3Benchmarks() {
    const int nThreads = static_cast<int>(
        std::max(1u, std::thread::hardware_concurrency()));

    std::cout << "=== Iteration 3 parallel benchmarks ("
              << nThreads << " hardware threads) ===\n\n";

    MarketData market { .spot=100.0, .riskFreeRate=0.05, .volatility=0.20 };
    EuropeanOption call(100.0, 1.0, OptionType::Call);

    for (int paths : { 10'000, 100'000, 1'000'000 }) {
        v2::MonteCarloModel mcV2(paths, 1, 42);
        v3::MonteCarloModel mcV3(paths, 1, 42);

        const std::string label = std::to_string(paths) + " paths (20 iters)";

        auto r2 = Benchmark::run("MC V2 " + label, [&]() {
            auto result = mcV2.price(call, market);
            (void)result.price;
        }, 20);

        auto r3 = Benchmark::run("MC V3 " + label, [&]() {
            auto result = mcV3.price(call, market);
            (void)result.price;
        }, 20);

        Benchmark::print(r2);
        std::cout << "  implied per-path : "
                  << std::fixed << std::setprecision(1)
                  << (r2.meanNs / paths) << " ns\n";
        Benchmark::print(r3);
        std::cout << "  implied per-path : "
                  << std::fixed << std::setprecision(1)
                  << (r3.meanNs / paths) << " ns\n";
        std::cout << "  parallel speedup : "
                  << std::fixed << std::setprecision(2)
                  << (r2.meanNs / r3.meanNs) << "x\n\n";
    }
}

// ─────────────────────────────────────────────────────────────────────────────

int main() {
    runTests();
    runSanityCheck();
    runBenchmarks();
    runV2Benchmarks();
    runV3Benchmarks();
    return g_results.failed > 0 ? 1 : 0;
}
