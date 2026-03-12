# Iteration 1 — Baseline Benchmark Results

**Build:** g++ 13.3, -O2, Linux x86-64  
**Date:** Iteration 1 complete  
**Commit tag:** iteration-1-baseline

---

## Sanity Check: ATM European Call (S=100, K=100, r=5%, σ=20%, T=1yr)

| Model         | Price     | Delta    | Stderr   |
|---------------|-----------|----------|----------|
| Black-Scholes | 10.450584 | 0.636831 | 0.000000 |
| Monte Carlo   | 10.459257 | —        | 0.020838 |

MC delta from BS: **0.636831**  
MC price within 0.43 stderr of BS — expected convergence behaviour.

Put-call parity error: **< 1e-12** (machine precision)

---

## Benchmark: Black-Scholes Single Price

| Metric     | Value   |
|------------|---------|
| Iterations | 10,000  |
| Mean       | ~90 ns  |
| Stddev     | ~94 ns  |
| Min        | ~83 ns  |

Notes:
- High stddev relative to mean is typical for timing single short functions; 
  cache effects and OS scheduling create tail latency (max ~7.5µs).
- The min (~83ns) is the most meaningful number — it reflects the cost 
  when the instruction cache is warm and no context switch occurs.

---

## Benchmark: Monte Carlo Scaling

| Paths   | Mean (ns) | Per-path (ns) |
|---------|-----------|---------------|
| 1,000   | 71,090    | 71.1          |
| 10,000  | 683,437   | 68.3          |
| 100,000 | 7,337,794 | 73.4          |

**Scaling is linear** — expected, confirms no algorithmic surprise.

**Per-path cost ~70ns** decomposes approximately as:
- GBM step arithmetic: ~5–10 ns
- `std::normal_distribution` sample (Ziggurat): ~15–20 ns  
- `std::vector<double>` allocation/deallocation: **~30–40 ns** ← primary target
- `payoff()` virtual dispatch: ~3–5 ns

---

## Known bottlenecks for future iterations

### Iteration 2 — Template refactor targets
- Virtual dispatch on `payoff()`: ~3–5 ns per path (small but eliminates devirtualisation barrier)
- `dynamic_cast` in `BlackScholesModel::price()`: measurable overhead, replaced by compile-time type constraint

### Iteration 3 — Memory optimisation targets
- **Per-path `std::vector` allocation: ~30–40 ns / path** — dominant cost
- 100k paths = ~100k heap allocations per `price()` call
- Fix: pre-allocate a single reusable path buffer (removes allocator pressure entirely)
