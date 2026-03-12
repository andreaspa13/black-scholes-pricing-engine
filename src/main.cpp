#include "options/EuropeanOption.h"
#include "models/BlackScholesModel.h"
#include "models/MonteCarloModel.h"
#include "utils/MarketData.h"
#include "utils/Benchmark.h"

#include <iostream>
#include <iomanip>
#include <cmath>

using namespace options;

// ── Helpers ──────────────────────────────────────────────────────────────────

void printResult(const std::string& label, const PricingResult& r) {
    std::cout << std::fixed << std::setprecision(6)
              << label << "\n"
              << "  model  : " << r.modelName << "\n"
              << "  price  : " << r.price     << "\n"
              << "  delta  : " << r.delta     << "\n"
              << "  stderr : " << r.stdErr    << "\n\n";
}

/**
 * Sanity check: compare BS vs MC price for a standard ATM call.
 * We expect MC to converge within a few stderr of the BS price.
 */
void runSanityCheck() {
    std::cout << "=== Sanity check: ATM European call ===\n\n";

    // Textbook parameters — easy to verify by hand or against online calculators.
    // S=100, K=100, r=5%, sigma=20%, T=1yr, q=0 → BS call ≈ 10.45
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

    // Verify put-call parity: C - P = S - K*e^(-rT)
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

    // --- Black-Scholes: tight loop, many iterations ---
    {
        auto r = Benchmark::run("BS single price (10k iters)", [&]() {
            auto result = bs.price(call, market);
            // prevent the compiler from optimising away the call entirely
            (void)result.price;
        }, 10'000);
        Benchmark::print(r);
    }

    // --- Monte Carlo: vary path count to show scaling ---
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

// ─────────────────────────────────────────────────────────────────────────────

int main() {
    runSanityCheck();
    runBenchmarks();
    return 0;
}
