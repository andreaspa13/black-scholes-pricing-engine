# Black-Scholes Pricing Engine

A C++20 options pricing engine built across deliberate iterations, each targeting a specific performance or design constraint. The project prices European call and put options using both Black-Scholes closed-form and Monte Carlo simulation, exposes a REST API, and ships an interactive browser GUI.

---

## What it demonstrates

| Area | Detail |
|---|---|
| Quantitative finance | BS formula, all five Greeks, put-call parity, Monte Carlo GBM, antithetic variates, implied volatility solver |
| C++ design | Iteration 1: runtime polymorphism (virtual dispatch, dynamic_cast). Iteration 2: C++20 concepts replacing the base-class contract, zero-cost abstractions |
| Performance | Measured 1.5× MC speedup from eliminating virtual `payoff()` dispatch and per-path heap allocation |
| Systems | HTTP server (cpp-httplib), JSON API (nlohmann/json), browser GUI with live charts |
| Testing | 38 pass/fail tests covering pricing accuracy, put-call parity, MC convergence, IV round-trips, edge-case rejection |

---

## Iterations

### Iteration 1 — Runtime polymorphism

`include/models/` and `src/models/`

The classic OOP design. `Option` is an abstract base with a pure virtual `payoff()`. `PricingModel` is an abstract base with a pure virtual `price()`. `BlackScholesModel` receives a `const Option&` and uses `dynamic_cast` to verify it is a `EuropeanOption` at runtime, throwing on failure.

**Deliberate design smells (interview talking points):**
- `dynamic_cast` in `BlackScholesModel::price()` — runtime type check that throws; the compiler cannot reject a wrong option type
- Virtual `payoff()` called inside the MC inner loop — vtable dispatch on every path
- `generatePath()` allocates a `std::vector<double>` per path — dominant allocation cost at ~30–40 ns/path

### Iteration 2 — Compile-time polymorphism

`include/v2/`

Replaces the inheritance hierarchy with C++20 concepts. `Priceable` and `EuropeanPriceable` express the same contract as the V1 base classes, but the constraint is checked by the compiler at the call site — a wrong option type is a compile error, not a runtime exception.

`BlackScholesModel::price<Opt>()` and `MonteCarloModel::price<Opt>()` are constrained function templates. The concrete `Opt` type is known at compile time, enabling:
- **No dynamic_cast** — the type check is free
- **No virtual payoff() dispatch** — the compiler inlines the payoff expression directly into the MC loop
- **No per-path vector allocation** — path simulation is accumulated in a scalar double; the heap buffer is gone

---

## Benchmark results

Measured on Windows 11, GCC 15.2 (MinGW), `-O3`. ATM European call, S=100, K=100, r=5%, σ=20%, T=1yr.

### Black-Scholes single price

| | Mean latency |
|---|---|
| V1 (dynamic_cast) | ~300 ns |
| V2 (template) | ~289 ns |

The BS gain is modest — `dynamic_cast` on a directly-derived type is fast, and the compiler already devirtualises `price()` when called on a concrete object. The gap widens when models are called through a `PricingModel*` base pointer.

### Monte Carlo per-path cost

| Paths | V1 ns/path | V2 ns/path | Speedup |
|---|---|---|---|
| 1 000 | 200 | 132 | **1.52×** |
| 10 000 | 190 | 118 | **1.61×** |
| 100 000 | 209 | 152 | **1.38×** |

The ~60–70 ns/path saving decomposes as:
- ~30–40 ns — `std::vector<double>` allocation in `generatePath()` (eliminated by inlining)
- ~3–5 ns — virtual `payoff()` dispatch × 1 call/path (eliminated by template instantiation)

---

## Features

- **Black-Scholes closed-form** — price, Delta, Gamma, Vega, Theta, Rho
- **Monte Carlo GBM simulation** — exact log-normal discretisation, Bessel-corrected standard error, 95% CI
- **Antithetic variates** — variance reduction toggle; pair-average estimator with correct stdErr
- **Implied volatility solver** — Newton-Raphson with bisection fallback, Brenner-Subrahmanyam initial guess, no-arbitrage bounds checking
- **HTTP server** — `POST /price` and `POST /iv` endpoints, CORS-enabled for local file:// GUI
- **Interactive GUI** — BS result card with all Greeks, MC result card, convergence chart, GBM path simulation, BS price/PnL heatmaps, IV input panel

---

## Project structure

```
include/
  options/        Option base class, EuropeanOption, OptionType enum
  models/         V1 BlackScholesModel, MonteCarloModel (virtual dispatch)
  v2/             V2 Concepts.h, BlackScholesV2.h, MonteCarloV2.h (templates)
  utils/          PricingResult, MarketData, ImpliedVol, Benchmark
  vendor/         cpp-httplib v0.37, nlohmann/json v3.11.3 (single headers)
src/
  models/         V1 model implementations
  options/        EuropeanOption payoff
  utils/          ImpliedVol solver implementation
  server.cpp      HTTP server (options_server binary)
  main.cpp        Test suite + V1/V2 benchmark comparison
gui.html          Self-contained browser GUI
```

---

## Build and run

**Requirements:** CMake 3.16+, GCC with C++20 support (tested: MinGW GCC 15.2 on Windows, GCC 13 on Linux)

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build

# Run test suite and V1 vs V2 benchmarks
./build/options_main.exe

# Start the pricing server (required for the GUI)
./build/options_server.exe
```

Then open `gui.html` in a browser. The GUI connects to `http://localhost:18080`.

---

## Test coverage

38 tests across:
- ATM / ITM / OTM call and put prices against known reference values (1e-4 tolerance)
- Put-call parity to machine precision (< 1e-12 error)
- Delta identity: Δ_call − Δ_put = 1
- MC convergence within 3σ of BS price (500k paths)
- Antithetic variates: convergence check + variance reduction verified
- IV round-trips: recover input σ to 1e-6 across ATM, ITM, OTM, high-vol cases
- IV edge-case rejection: sub-intrinsic price, above upper bound
- V2 correctness: BS V2 matches V1 to 1e-14 on all six outputs (price, Δ, Γ, ν, Θ, ρ)
