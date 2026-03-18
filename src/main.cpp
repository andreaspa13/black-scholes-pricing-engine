#include "options/EuropeanOption.h"
#include "models/BlackScholesModel.h"
#include "models/MonteCarloModel.h"
#include "utils/MarketData.h"
#include "utils/Benchmark.h"

// Iteration 2: template models
#include "v2/BlackScholesV2.h"
#include "v2/MonteCarloV2.h"

// Iteration 3: parallel Monte Carlo
#include "v3/MonteCarloV3.h"

#include <iostream>
#include <iomanip>
#include <cmath>
#include <string>

using namespace options;

void printResult(const std::string& label, const PricingResult& r) {
    std::cout << std::fixed << std::setprecision(6)
              << label << "\n"
              << "  model  : " << r.modelName << "\n"
              << "  price  : " << r.price     << "\n"
              << "  delta  : " << r.delta     << "\n"
              << "  stderr : " << r.stdErr    << "\n\n";
}

// Sanity print (visual confirmation of ATM European call/put)
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

// Iteration 1 baseline benchmarks
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

// Iteration 2 vs Iteration 1 benchmarks
// Direct comparison of V1 (virtual dispatch + dynamic_cast) vs V2 (templates).
// Expected improvements:
//   BS:  ~5-10 ns/call  from eliminating dynamic_cast
//   MC:  ~3-5 ns/path   from eliminating virtual payoff() dispatch
//        ~30-40 ns/path  from eliminating per-path std::vector allocation
void runV2Benchmarks() {
    std::cout << "=== Iteration 2 vs Iteration 1 benchmarks ===\n\n";

    MarketData market { .spot=100.0, .riskFreeRate=0.05, .volatility=0.20 };
    EuropeanOption call(100.0, 1.0, OptionType::Call);

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

// Iteration 3 benchmarks
// V2 (single-threaded) vs V3 (parallel) on 100k and 1M paths.
// Expected speedup: near-linear with core count for large N.
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

int main() {
    runSanityCheck();
    runBenchmarks();
    runV2Benchmarks();
    runV3Benchmarks();
    return 0;
}
