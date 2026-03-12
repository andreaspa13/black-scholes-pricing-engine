#pragma once

#include <chrono>
#include <string>
#include <vector>
#include <functional>
#include <cmath>
#include <numeric>
#include <iostream>
#include <iomanip>

namespace options {

struct BenchmarkResult {
    std::string name;
    double meanNs;
    double stddevNs;
    double minNs;
    double maxNs;
    int    iterations;
};

/**
 * Minimal benchmarking harness.
 *
 * Usage:
 *   auto result = Benchmark::run("BS call", [&]() {
 *       bs.price(option, market);
 *   }, 10000);
 *   Benchmark::print(result);
 *
 * Design notes:
 *   - Uses high_resolution_clock. On Linux this is typically clock_gettime
 *     with nanosecond resolution; on MSVC it may be lower resolution.
 *   - Does NOT do warm-up runs or attempt to prevent compiler optimisation
 *     of the result. For Iteration 1 baseline measurements this is fine.
 *     A production benchmark would use Google Benchmark or similar.
 *   - The volatile sink trick (DoNotOptimize) is intentionally absent here
 *     to keep the code simple. If you see suspiciously fast results, add it.
 */
class Benchmark {
public:
    template<typename Fn>
    static BenchmarkResult run(const std::string& name, Fn&& fn, int iterations = 1000) {
        using Clock = std::chrono::high_resolution_clock;
        using Ns    = std::chrono::duration<double, std::nano>;

        std::vector<double> times;
        times.reserve(iterations);

        for (int i = 0; i < iterations; ++i) {
            const auto t0 = Clock::now();
            fn();
            const auto t1 = Clock::now();
            times.push_back(Ns(t1 - t0).count());
        }

        const double mean = std::accumulate(times.begin(), times.end(), 0.0)
                            / static_cast<double>(iterations);

        double variance = 0.0;
        for (double t : times) {
            const double d = t - mean;
            variance += d * d;
        }
        variance /= static_cast<double>(iterations - 1);

        const double minT = *std::min_element(times.begin(), times.end());
        const double maxT = *std::max_element(times.begin(), times.end());

        return { name, mean, std::sqrt(variance), minT, maxT, iterations };
    }

    static void print(const BenchmarkResult& r) {
        std::cout << std::fixed << std::setprecision(1)
                  << "[" << r.name << "]\n"
                  << "  iterations : " << r.iterations    << "\n"
                  << "  mean       : " << r.meanNs        << " ns\n"
                  << "  stddev     : " << r.stddevNs      << " ns\n"
                  << "  min        : " << r.minNs         << " ns\n"
                  << "  max        : " << r.maxNs         << " ns\n";
    }
};

} // namespace options
