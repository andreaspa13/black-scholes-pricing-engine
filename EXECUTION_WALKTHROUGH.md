# EXECUTION_WALKTHROUGH.md — Runtime Flow, Iteration 1

This document traces execution from program entry to final output, in the exact order
things happen at runtime. It is not about what code looks like statically — it is about
what the CPU is actually doing, call by call, when you run the binary.

Read this alongside ANNOTATIONS.md. ANNOTATIONS.md explains *what each line means*.
This document explains *when each line runs and why it runs in that order*.

---

## Overview

```
main()
 ├── runSanityCheck()
 │    ├── Construct: MarketData, EuropeanOption x2, BlackScholesModel, MonteCarloModel
 │    ├── bs.price(call)  →  BlackScholesModel::price()  →  N() x4
 │    ├── mc.price(call)  →  MonteCarloModel::price()  →  generatePath() x500,000
 │    ├── bs.price(put), mc.price(put)
 │    └── Put-call parity check
 └── runBenchmarks()
      ├── Construct: MarketData, EuropeanOption, BlackScholesModel
      ├── Benchmark::run(BS, 10,000 iterations)
      └── for paths in {1k, 10k, 100k}:
           ├── Construct MonteCarloModel(paths)
           └── Benchmark::run(MC, 100 iterations)
```

---

## Stage 1 — Program entry: `main()`

**File:** `main.cpp`, line 95

```cpp
int main() {
    runSanityCheck();
    runBenchmarks();
    return 0;
}
```

The OS hands control to `main`. No global objects are constructed before this point
(there are none in the project). The two function calls are sequential: benchmarks do
not start until the sanity check is fully complete.

---

## Stage 2 — `runSanityCheck()` begins

**File:** `main.cpp`, line 28

### 2a. MarketData construction

```cpp
MarketData market { .spot=100.0, .riskFreeRate=0.05, .volatility=0.20 };
```

A `MarketData` struct is placed on the stack frame of `runSanityCheck`. No constructor
is called — this is aggregate initialisation. The compiler writes the three `double`
values directly into the three 8-byte slots on the stack. Total cost: three store
instructions. The object lives until `runSanityCheck` returns.

Stack layout at this point (schematic):
```
[ market.spot        = 100.0 ]  8 bytes
[ market.riskFreeRate = 0.05 ]  8 bytes
[ market.volatility   = 0.20 ]  8 bytes
```

### 2b. EuropeanOption construction (x2)

```cpp
EuropeanOption call(100.0, 1.0, OptionType::Call);
EuropeanOption put (100.0, 1.0, OptionType::Put);
```

Two `EuropeanOption` objects on the stack. For each:

1. `EuropeanOption(double, double, OptionType)` is entered.
2. The member initialiser list immediately delegates to `Option(strike, expiry, type)`.
3. `Option`'s constructor runs: `strike_ = 100.0`, `expiry_ = 1.0`, `type_ = Call/Put`
   are written into the object's memory.
4. `EuropeanOption` adds no data members, so nothing more happens.
5. A **vtable pointer** is also written into the object at offset 0 (before `strike_`).
   This pointer points to `EuropeanOption`'s vtable, which contains the address of
   `EuropeanOption::payoff`. This is what makes virtual dispatch work later.

Memory layout of each `EuropeanOption` (on a 64-bit system):
```
[ vptr     ] → EuropeanOption vtable → { &EuropeanOption::payoff }
[ strike_  ] = 100.0
[ expiry_  ] = 1.0
[ type_    ] = 0 (Call) or 1 (Put)
```

### 2c. BlackScholesModel construction

```cpp
BlackScholesModel bs;
```

`BlackScholesModel` has no user-defined constructor (it uses the compiler-generated
default). The compiler default-constructs all members. `BlackScholesModel` has no data
members, so this is essentially a no-op. A vtable pointer is written pointing to
`BlackScholesModel`'s vtable (containing `&BlackScholesModel::price`).

### 2d. MonteCarloModel construction

```cpp
MonteCarloModel mc(500'000, 1, 42);
```

The user-defined constructor runs. Execution order:

1. **Member initialiser list** (runs before the constructor body):
   - `numPaths_(500000)` — writes 500000 into `numPaths_`
   - `numSteps_(1)` — writes 1 into `numSteps_`
   - `rng_(42)` — constructs the `std::mt19937` from seed 42. Internally this seeds
     the 624-word (2496-byte) Mersenne Twister state array. This is the most expensive
     part of the constructor — it runs the seeding algorithm over the full state.
   - `dist_(0.0, 1.0)` — constructs the normal distribution with mean=0, stddev=1.
     This stores the two parameters and initialises any internal state the Ziggurat
     algorithm needs.

2. **Constructor body** runs:
   - `if (numPaths_ <= 0)` — false, no throw.
   - `if (numSteps_ <= 0)` — false, no throw.
   - Constructor returns.

`mc` now sits on the stack, holding the fully seeded RNG state.

---

## Stage 3 — `bs.price(call, market)` — Black-Scholes pricing

**File:** `BlackScholesModel.cpp`, line 22

Called as `bs.price(call, market)`. Because `price` is declared `virtual` in
`PricingModel`, the call goes through the vtable — but since `bs` is a concrete stack
object of known type `BlackScholesModel`, the compiler can (and with `-O2` will)
devirtualise this call and call `BlackScholesModel::price` directly. The vtable lookup
only reliably occurs when the call is made through a `PricingModel*` or `PricingModel&`.

Execution inside `BlackScholesModel::price`:

### 3a. dynamic_cast

```cpp
const auto* european = dynamic_cast<const EuropeanOption*>(&option);
```

`option` is a `const Option&` that refers to `call`, which is a `EuropeanOption`.
`dynamic_cast` inspects the vtable pointer of `call` and walks the RTTI type tree to
check whether the object is-a `EuropeanOption`. It is, so the cast succeeds and
`european` points to `call`. No throw. Cost: ~20–50 ns of vtable/RTTI traversal.

### 3b. Extract and alias inputs

```cpp
const double S = market.spot;       // 100.0
const double K = option.strike();   // 100.0  (inline accessor, no virtual dispatch)
const double r = market.riskFreeRate; // 0.05
const double sigma = market.volatility; // 0.20
const double T = option.expiry();   // 1.0
```

Six reads from the `market` struct and the `option` object into local registers. The
accessors `option.strike()` and `option.expiry()` are non-virtual inline functions —
they compile to direct memory reads from the option's data members, with no dispatch.

### 3c. Input validation

Three comparisons against zero. All pass. No throw.

### 3d. BS formula computation

```
sqrtT = sqrt(1.0) = 1.0
d1    = (log(100/100) + (0.05 + 0.5*0.04)*1.0) / (0.20*1.0)
      = (0.0 + 0.07) / 0.20
      = 0.35
d2    = 0.35 - 0.20*1.0 = 0.15

discountedStrike = 100 * exp(-0.05*1.0) = 100 * 0.95123 = 95.123
```

Each transcendental function call (`std::sqrt`, `std::log`, `std::exp`) dispatches to
the C standard library, which on x86-64 uses hardware FPU or SSE2 instructions. Each
costs roughly 20–40 ns. With four unique transcendentals here, this block accounts for
most of the ~83 ns minimum BS price time.

### 3e. N(d1) and N(d2) — two calls to `BlackScholesModel::N()`

```
N(0.35)  = 0.5 * erfc(-0.35/sqrt(2)) = 0.5 * erfc(-0.2475) ≈ 0.6368
N(0.15)  = 0.5 * erfc(-0.15/sqrt(2)) = 0.5 * erfc(-0.1061) ≈ 0.5596
```

Each `N()` call invokes `std::erfc`, another transcendental. Two more calls here for
the call pricing, two more for the delta.

### 3f. Price and delta assignment

```
result.price = 100.0 * 0.6368 - 95.123 * 0.5596 ≈ 63.68 - 53.23 ≈ 10.45
result.delta = N(d1) = 0.6368
```

Two multiplications and one subtraction. Negligible cost.

### 3g. Return PricingResult

`PricingResult` is returned by value. NRVO (Named Return Value Optimisation) means the
compiler constructs `result` directly in the caller's stack frame, avoiding a copy.
`std::string modelName = "BlackScholes"` involves a small string — 12 characters fits
within SSO capacity, so no heap allocation occurs on construction or copy.

---

## Stage 4 — `mc.price(call, market)` — Monte Carlo pricing

**File:** `MonteCarloModel.cpp`, line 49

Called with 500,000 paths and 1 step.

### 4a. Extract inputs

```
S=100.0, r=0.05, sigma=0.20, T=1.0
dt = 1.0 / 1 = 1.0
drift = 0.05 - 0.5*0.04 = 0.03
```

### 4b. Allocate payoffs vector

```cpp
std::vector<double> payoffs(numPaths_);
```

One heap allocation of `500,000 * 8 = 4,000,000 bytes = ~3.8 MB`. The allocator
(typically `ptmalloc` on Linux or `mimalloc` on Windows) is called once. The vector's
elements are zero-initialised. This allocation is not in the hot loop — it happens once
per `price()` call.

### 4c. Main simulation loop — 500,000 iterations

For each iteration `i` from 0 to 499,999:

#### 4c-i. `generatePath(S=100.0, dt=1.0, drift=0.03, sigma=0.20)`

1. **Heap allocation**: `std::vector<double> path(1)` — allocates 1 `double` on the
   heap. This is the dominant per-path cost (~30–40 ns). The allocator is called
   500,000 times across the full loop.

2. **Hoist `volSqrtDt`**: `vol * sqrt(dt) = 0.20 * 1.0 = 0.20`. Computed once before
   the inner loop (numSteps_=1, so the loop runs exactly once).

3. **Inner loop, i=0**:
   - `Z = dist_(rng_)` — draws one standard normal variate. Internally:
     - `rng_()` calls the Mersenne Twister, advancing the state and producing one
       32-bit integer.
     - The Ziggurat algorithm in `std::normal_distribution` converts this (possibly
       drawing more values if it falls in the tail) to a `double` from N(0,1).
       Cost: ~15–20 ns.
   - `S *= exp(drift*dt + volSqrtDt*Z)` — one multiply, one add, one `std::exp`.
     The `exp` call dominates: ~20–40 ns.
   - `path[0] = S` — one store.

4. **Return path by value** — NRVO may apply, but the vector's heap buffer must still
   be handed off. The vector move constructor transfers ownership of the buffer pointer
   without copying the data.

#### 4c-ii. `path.back()`

Returns `path[numSteps_-1] = path[0]`, i.e. the single terminal spot price.

#### 4c-iii. `option.payoff(terminalSpot)` — virtual dispatch

This is a call through a base-class reference (`const Option& option`), so the vtable
is consulted at runtime:

1. Load the vtable pointer from offset 0 of the `call` object.
2. Index into the vtable (payoff is the first virtual function, so index 0).
3. Load the function pointer: `&EuropeanOption::payoff`.
4. Call the function.

Inside `EuropeanOption::payoff`:
```cpp
return std::max(terminalSpot - 100.0, 0.0);  // for a call
```
One subtraction, one `std::max`. Negligible arithmetic, but the vtable lookup costs
~3–5 ns and (crucially) prevents inlining — the compiler cannot fold this computation
into the loop body.

#### 4c-iv. Store payoff

```cpp
payoffs[i] = option.payoff(terminalSpot);
```

One store into the `payoffs` vector. With 500,000 iterations this vector spans ~3.8 MB,
likely spanning multiple cache lines. Sequential access pattern means the hardware
prefetcher handles this well.

#### 4c-v. `path` destructor

At the end of the loop body, `path` (a local `std::vector`) goes out of scope. Its
destructor calls the allocator's `free()` — releasing the 8-byte heap buffer. This
is the second half of the per-path allocation cost.

### 4d. Compute mean

```cpp
const double mean = std::accumulate(payoffs.begin(), payoffs.end(), 0.0) / 500000.0;
```

One pass over 500,000 doubles, summing sequentially. The compiler will likely vectorise
this with SSE2/AVX2 (four or eight doubles per instruction). The 3.8 MB array likely
fits in L3 cache on most desktop CPUs.

### 4e. Compute variance (two-pass)

A second sequential pass over `payoffs`, computing squared deviations from the mean.
Also vectorisable. Divided by N-1 = 499,999 (Bessel's correction).

### 4f. Build and return PricingResult

```cpp
result.price  = exp(-0.05*1.0) * mean  = 0.9512 * mean
result.stderr = 0.9512 * sqrt(variance / 500000)
```

Two more transcendental calls (`exp`, `sqrt`), then return.

---

## Stage 5 — Remaining sanity check calls

`bs.price(put, market)` and `mc.price(put, market)` follow the identical paths as
Stages 3 and 4 respectively, with `OptionType::Put` routing to the `Put` branch of
each switch statement. The put payoff branch in `EuropeanOption::payoff` is
`max(100.0 - terminalSpot, 0.0)` instead of `max(terminalSpot - 100.0, 0.0)`.

The put-call parity check then calls `bs.price` twice more and does a few arithmetic
comparisons. No significant computation.

---

## Stage 6 — `runBenchmarks()` begins

**File:** `main.cpp`, line 59

Fresh `MarketData`, `EuropeanOption`, and `BlackScholesModel` are constructed on the
new stack frame — identical process to Stage 2a–2c.

### 6a. Black-Scholes benchmark

```cpp
auto r = Benchmark::run("BS single price (10k iters)", [&]() {
    auto result = bs.price(call, market);
    (void)result.price;
}, 10'000);
```

`Benchmark::run` is a function template. `Fn` is deduced as the lambda type.

Execution inside `Benchmark::run`:

1. `times.reserve(10000)` — one heap allocation of `10000 * 8 = 80,000 bytes`.

2. **Timing loop, 10,000 iterations**:

   Each iteration:
   - `Clock::now()` — a system call or `RDTSC`-based read. On Linux with
     `clock_gettime(CLOCK_REALTIME)` this costs ~20–50 ns itself, which means the
     timing overhead is a significant fraction of what is being measured (~83 ns).
     This is a known limitation of the harness.
   - `fn()` — the lambda executes: calls `bs.price(call, market)` (Stage 3 repeated),
     stores result on the stack, accesses `.price` via `(void)`.
   - `Clock::now()` again.
   - `Ns(t1 - t0).count()` — converts the duration to nanoseconds as `double`.
   - `times.push_back(...)` — no reallocation (capacity reserved).

3. After the loop: compute mean, variance, min, max over the 10,000 timing samples.
   These are sequential passes over an 80 kB array — fits in L2 cache.

4. Return `BenchmarkResult` by value.

### 6b. Monte Carlo benchmark — three iterations

```cpp
for (int paths : { 1'000, 10'000, 100'000 }) {
    MonteCarloModel mc(paths, 1, 42);
    ...
    auto r = Benchmark::run(label, [&]() { mc.price(call, market); }, 100);
```

Each pass through the outer loop:

1. **Construct `MonteCarloModel(paths, 1, 42)`** on the stack — same as Stage 2d but
   with the specified path count. The Mersenne Twister is re-seeded from 42 each time,
   so results are reproducible and comparable across path counts.

2. **`Benchmark::run(label, lambda, 100)`** — the timing loop runs 100 times. Each
   iteration calls `mc.price(call, market)` in full (Stage 4 with `paths` paths instead
   of 500,000). With 100,000 paths and 100 iterations, that is 10,000,000 calls to
   `generatePath` across the whole benchmark.

3. **`r.meanNs / paths`** — divides the mean total pricing time by the number of paths
   to get the per-path cost. This is the primary metric that Iterations 2 and 3 aim to
   reduce.

4. At the closing brace of the `for` loop body, `mc` is destroyed (stack unwind). The
   Mersenne Twister state (2496 bytes) is trivially destructed (no heap involved in
   `std::mt19937` itself).

---

## Stage 7 — Teardown

`runBenchmarks` returns. `runSanityCheck` returned earlier. Back in `main`:

```cpp
return 0;
```

All stack-allocated objects in `main`'s frame are destroyed in reverse declaration order
(C++ guarantees this). There are none directly in `main` — they were in the called
functions' frames and were already destroyed when those functions returned.

The OS reclaims the process's heap and stack. Program exits.

---

## Call Frequency Summary

| Function | Call site | Times called per full run |
|---|---|---|
| `BlackScholesModel::price` | sanity check + parity | 4 |
| `BlackScholesModel::price` | BS benchmark (10k iters) | 10,000 |
| `BlackScholesModel::N` | each `price()` call | 4× per price call |
| `MonteCarloModel::price` | sanity check | 2 (call + put) |
| `MonteCarloModel::price` | MC benchmark (100 iters × 3 path counts) | 300 |
| `MonteCarloModel::generatePath` | each `price()` call | numPaths per call |
| `EuropeanOption::payoff` (virtual) | each `generatePath` result | numPaths per `price()` |
| `std::exp` | BS formula | ~3× per BS price call |
| `std::exp` / `std::log` / `std::sqrt` | MC formula | ~2× per MC price call |
| `std::exp` (inside GBM step) | each path | 1× per path |
| `std::normal_distribution::operator()` | each path step | 1× per path |
| heap `allocate` + `free` (path vector) | each path | 1 alloc + 1 free per path |

---

## Key Bottleneck Visualised

For a single `mc.price()` call with N paths, the per-path work is:

```
generatePath() called
  │
  ├─ malloc(8 bytes)                    ~30–40 ns  ← dominant cost (Iter 3 target)
  ├─ dist_(rng_) — Ziggurat draw        ~15–20 ns
  ├─ std::exp(drift*dt + vol*sqrt*Z)    ~20–40 ns
  └─ free(path buffer)                  (part of the alloc cost above)

Back in price():
  ├─ path.back()                        ~1 ns
  ├─ vtable load + option.payoff()      ~3–5 ns    ← Iter 2 target
  └─ payoffs[i] = ...                   ~1 ns
```

Total: ~70–80 ns per path at baseline.
After Iteration 2 (no vtable): target ~65–75 ns.
After Iteration 3 (no alloc):  target ~30–40 ns.

---

*End of EXECUTION_WALKTHROUGH.md — Iteration 1*
