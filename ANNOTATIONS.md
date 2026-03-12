# ANNOTATIONS.md — Options Pricing Engine

Line-by-line (or block-by-block) annotations for every source file in the project.
Updated with every code change. This is the primary learning artifact.

**Last updated:** Iteration 3 complete — Greeks, implied volatility, antithetic variates, V2 (C++20 concepts), V3 (parallel MC)

---

## Table of Contents

1. [CMakeLists.txt](#cmakeliststxt)
2. [MarketData.h](#marketdatah)
3. [PricingResult.h](#pricingresulth)
4. [Option.h](#optionh)
5. [EuropeanOption.h](#europeanoptionh)
6. [EuropeanOption.cpp](#europeanoptioncpp)
7. [PricingModel.h](#pricingmodelh)
8. [BlackScholesModel.h](#blackscholesmodelh)
9. [BlackScholesModel.cpp](#blackscholesmodelcpp)
10. [MonteCarloModel.h](#montecarlomodelh)
11. [MonteCarloModel.cpp](#montecarlomodelcpp)
12. [Benchmark.h](#benchmarkh)
13. [main.cpp](#maincpp)
14. [server.cpp](#servercpp) ← added
15. [gui.html](#guihtml) ← added
16. [baseline_results.md](#baseline_resultsmd)
17. [ImpliedVol.h](#impliedvolh) ← added
18. [ImpliedVol.cpp](#impliedvolcpp) ← added
19. [v2/Concepts.h](#v2conceptsh) ← added
20. [v2/BlackScholesV2.h](#v2blackscholesv2h) ← added
21. [v2/MonteCarloV2.h](#v2montecarlов2h) ← added
22. [v3/MonteCarloV3.h](#v3montecarlов3h) ← added

---

## CMakeLists.txt

The build system definition. CMake is the de-facto standard for cross-platform C++ projects
and is expected in any professional or portfolio C++ codebase.

```
cmake_minimum_required(VERSION 3.16)
```
Declares the minimum CMake version required. 3.16 was released in 2019 and introduced
`target_precompile_headers` and improved `find_package` behaviour. Specifying a minimum
version prevents the build from silently misinterpreting syntax on older installations.
The alternative — omitting this line — generates a warning and makes the build fragile.

```
project(OptionsEngine VERSION 1.0 LANGUAGES CXX)
```
Names the project and restricts the enabled languages to C++ only. The `VERSION` field
populates CMake variables like `PROJECT_VERSION_MAJOR` for use in generated config files.
`LANGUAGES CXX` prevents CMake from probing for a C compiler, which is irrelevant here
and saves configuration time.

```
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
```
Three lines that always belong together:
- `CMAKE_CXX_STANDARD 20`: compile as C++20. This project uses designated initialisers
  (e.g. `.spot=100.0`) which are a C++20 feature (they exist in C99/C11 but only became
  standard C++ in C++20).
- `CMAKE_CXX_STANDARD_REQUIRED ON`: if the compiler cannot support C++20, fail loudly
  rather than silently downgrading. Without this, CMake may compile in C++17 mode without
  telling you, and subtle C++20 features could be silently accepted by some compilers or
  rejected by others.
- `CMAKE_CXX_EXTENSIONS OFF`: disables compiler-specific extensions (e.g. GCC's
  `-std=gnu++20` becomes `-std=c++20`). Using extensions makes the code non-portable
  to MSVC or Clang without additional effort. Always disable extensions in portable code.

```
add_compile_options(
    -Wall
    -Wextra
    -Wpedantic
    -Wshadow
    -Wconversion
)
```
Global warning flags applied to all targets. Each flag and the reasoning:
- `-Wall`: enables a broad set of commonly agreed-upon warnings. Despite the name it does
  not enable *all* warnings — it enables the ones GCC considers almost always to indicate
  a mistake.
- `-Wextra`: enables additional warnings not covered by `-Wall`, e.g. missing field
  initialisers.
- `-Wpedantic`: enforces strict ISO conformance. Flags extensions that are not standard
  C++. Useful for keeping code portable.
- `-Wshadow`: warns when a local variable or parameter shadows an outer scope variable.
  In financial code this frequently causes bugs (e.g. a local `sigma` shadowing a member
  `sigma_`).
- `-Wconversion`: warns on implicit conversions that may lose precision (e.g.
  `double` → `int` or `int` → `float`). Very useful in numerical code.

Alternative: `-Werror` (treat warnings as errors) is common in CI pipelines to prevent
warning accumulation. Omitted here for development convenience but should be added in CI.

```
include_directories(include)
```
Adds the `include/` subdirectory to the compiler's header search path. With this, a
source file can write `#include "options/Option.h"` and the compiler resolves it to
`include/options/Option.h`. This is the conventional layout: public headers live under
`include/`, source files under `src/`.

The alternative `target_include_directories` (used below for the library target) is
preferred in modern CMake because it scopes the include path to specific targets rather
than applying it globally. Using both here is slightly redundant but harmless.

```
add_library(options_engine
    src/options/EuropeanOption.cpp
    src/models/BlackScholesModel.cpp
    src/models/MonteCarloModel.cpp
    src/utils/ImpliedVol.cpp
)
```
Creates a static library (default when neither STATIC nor SHARED is specified) from the
implementation files. `ImpliedVol.cpp` was added in the Greeks/IV iteration — it contains
the Newton-Raphson and bisection solver that backs the `/iv` HTTP endpoint. Packaging
the pricing engine as a library has several benefits:
- `main.cpp` links against it without recompiling the engine code when only `main.cpp`
  changes.
- In a real project, a test binary would also link against the same library, ensuring
  tests compile the same code that production uses.
- It clearly separates engine logic from the driver binary.

Alternative: a single executable with all `.cpp` files listed directly. Simpler, but
loses the compilation isolation and the test-linking benefit.

```
target_include_directories(options_engine PUBLIC include)
```
Propagates the `include/` directory to any target that links against `options_engine`.
`PUBLIC` means: "use these includes when compiling `options_engine` itself, and also
pass them along to anything that links me." The alternative keywords:
- `PRIVATE`: use only when compiling `options_engine`, do not propagate.
- `INTERFACE`: do not use when compiling `options_engine`, but propagate to linkers.
`PUBLIC` is correct here because consumers of the library need to see the same headers
that the library was built with.

```
add_executable(options_main src/main.cpp)
target_link_libraries(options_main PRIVATE options_engine)
if(WIN32)
    target_link_libraries(options_main PRIVATE -lpthread)
endif()
```
Creates the executable and links the engine library into it. `PRIVATE` on `target_link_libraries`
means: "link `options_engine` into `options_main` but do not propagate this dependency
to anything that links against `options_main`." Since `options_main` is an executable
(not a library), nothing links against it, so `PRIVATE` vs `PUBLIC` makes no practical
difference here — but `PRIVATE` is the correct choice by convention.

The `if(WIN32) -lpthread` block is required for MinGW on Windows: `std::thread` uses
pthreads under the hood via the winpthreads library, and MinGW's linker does not pull it
in automatically. On Linux with GCC, the equivalent is `-pthread` (compile flag, not just
linker flag, which also defines `_REENTRANT`). The Windows-only `if` avoids breaking
Linux builds where `-lpthread` may not be needed or may be handled differently.

```
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
```
Sets the default build type to Release if the user has not specified one. This is
critical for benchmarking: a Debug build disables optimisations (`-O0`) and inserts
debug info, which would make every timing measurement meaningless. The `message(STATUS
...)` line prints the build type during configuration so the user always knows what they
are measuring. Alternative: require the user to always pass `-DCMAKE_BUILD_TYPE=Release`
explicitly; rejected because it makes it too easy to accidentally benchmark a Debug build.

---

## MarketData.h

```cpp
#pragma once
```
Include guard using the non-standard but universally supported `#pragma once`. When a
translation unit includes this header more than once (directly or transitively), the
compiler processes it only once. The alternative is a traditional `#ifndef`/`#define`/
`#endif` guard, which is portable and mandated by the standard but verbose. `#pragma once`
is supported by all modern compilers (GCC, Clang, MSVC) and is preferred for brevity.
It has one edge case: if the same physical file is accessible via two different paths
(e.g. symlinks), some compilers may process it twice. This is not a concern in a
project with a single canonical include directory.

```cpp
namespace options {
```
Opens the project-wide namespace. All project types live in `options::` to avoid
collisions with names from `<cmath>`, `<cstdlib>`, or any future third-party library.
The alternative — a global namespace — is universally considered bad practice in
production C++ as it pollutes the global namespace and makes `using namespace std;`
dangerous.

```cpp
struct MarketData {
    double spot;           // Current underlying price (S)
    double riskFreeRate;   // Continuously compounded risk-free rate (r)
    double volatility;     // Implied or historical vol (sigma)
};
```
A plain aggregate struct (no constructors, no access specifiers). Three observations:

1. **`struct` not `class`**: In C++, the only difference between `struct` and `class`
   is default member access (`public` vs `private`). Using `struct` signals to readers:
   "this is pure data with no invariants to enforce." Using `class` would signal "this
   has logic you should not bypass." The `struct` choice is idiomatic for value objects
   that are just bags of fields.

2. **No constructor**: Because this is an aggregate (no user-declared constructors,
   no private members, no base classes), it can be initialised with aggregate initialisation:
   `MarketData market { .spot=100.0, .riskFreeRate=0.05, .volatility=0.20 }`. The
   `.field=value` syntax (designated initialisers) is C++20. It is readable and catches
   the common bug of swapping arguments that have the same type.

3. **All `double`**: Consistent use of `double` (64-bit IEEE 754) throughout the
   codebase. `float` (32-bit) would halve memory bandwidth pressure in SIMD scenarios
   but its ~7 significant digits are inadequate for financial arithmetic. `long double`
   is 80-bit on x86 but not reliably so on all platforms. `double` is the standard
   choice for financial models.

**`dividendYield` deliberately omitted**: The Merton extension to BS (continuous
dividend yield `q`) is valid for equity and index options, but adds complexity without
benefit for the core portfolio goal. The standard no-dividend BS formula is taught
universally and is what interviewers expect unless dividends are specifically discussed.
It can be reintroduced as a named extension if needed.

---

## PricingResult.h

```cpp
#pragma once
#include <string>
```
`<string>` is needed for `std::string modelName`. Including only what you use is
important in large codebases: unnecessary includes slow compilation and create hidden
transitive dependencies.

```cpp
struct PricingResult {
    double price   = 0.0;
    double stdErr  = 0.0;   // MC convergence metric; 0 for analytical models
    double delta   = 0.0;   // dV/dS
    double gamma   = 0.0;   // d²V/dS²
    double vega    = 0.0;   // dV/dσ
    double theta   = 0.0;   // dV/dt per calendar day (negative = time decay)
    double rho     = 0.0;   // dV/dr
    std::string modelName;  // for benchmarking output and logging
};
```
An aggregate result type with default member initialisers. Design points:

1. **Default member initialisers (`= 0.0`)**: Introduced in C++11. They ensure that a
   default-constructed `PricingResult` has deterministic values, not garbage. Without
   them, `PricingResult r; std::cout << r.price;` would be UB (reading uninitialised
   memory). The alternative is a user-defined default constructor, but default member
   initialisers achieve the same outcome more concisely.

2. **`stdErr` (renamed from `stderr`)**: The original name `stderr` is a macro defined in
   `<cstdio>` that expands to the standard error file stream. If `<cstdio>` is included
   (directly or transitively) in any translation unit that uses `PricingResult`, the name
   would conflict and the struct field would silently become a file pointer. Renamed to
   `stdErr` in Iteration 2 when the conflict was encountered compiling `server.cpp`.

3. **Greeks fields (gamma, vega, theta, rho)**: Added alongside `delta` in the Greeks
   iteration. All five Greeks use the same pattern — they are set by `BlackScholesModel`
   and left as 0.0 by `MonteCarloModel` (which does not compute analytical Greeks). The
   choice of 0.0 as the default rather than `NaN` was deliberate: NaN would propagate
   silently into any downstream arithmetic, making bugs harder to detect. 0.0 is wrong
   but obvious.

4. **`std::string modelName`**: Carrying the model name in the result enables the
   benchmarking output to be self-describing without the caller needing to track which
   model produced which result. The cost is that `std::string` involves a heap allocation
   for strings longer than SSO (Small String Optimisation) capacity, typically 15–22
   characters. "BlackScholes" is 12 characters and "MonteCarlo" is 10, so both fit within
   SSO on typical implementations — no heap allocation occurs.

---

## Option.h

```cpp
#pragma once
namespace options {
enum class OptionType { Call, Put };
```
`enum class` (scoped enumeration, C++11) rather than plain `enum`. Three reasons:
1. **No implicit integer conversion**: With a plain `enum`, `OptionType::Call == 0` is
   `true`. With `enum class`, `OptionType::Call == 0` is a compile error. This prevents
   accidental arithmetic on option types.
2. **No namespace pollution**: Plain enum enumerators are injected into the enclosing
   scope, so `Call` and `Put` would be accessible without qualification. With `enum class`
   you must write `OptionType::Call`, which is unambiguous.
3. **Explicit underlying type possible**: `enum class OptionType : uint8_t { Call, Put }`
   would guarantee one byte of storage. Not done here — the default is `int` and that is
   fine for two enumerators.

```cpp
class Option {
public:
    Option(double strike, double expiry, OptionType type)
        : strike_(strike), expiry_(expiry), type_(type) {}
```
Constructor using a member initialiser list (`: strike_(strike), ...`). This is the
idiomatic C++ way to initialise members, not assignment in the constructor body
(`{ strike_ = strike; }`). The reasons:
- For non-trivial types (e.g. `std::string` members), the initialiser list calls the
  copy constructor once; assignment in the body default-constructs first, then assigns —
  two operations. For `double` it makes no practical difference, but the habit is good.
- `const` and reference members *can only* be initialised in the initialiser list, not
  assigned in the body.
- The initialiser list visually documents all members and their initialisation in one
  place.

The order in the initialiser list matches the order of member declaration
(`strike_`, `expiry_`, `type_`). This matters: C++ initialises members in declaration
order regardless of the initialiser list order. A mismatch (initialise `type_` before
`strike_` in the list when `strike_` is declared first) silently reorders and causes
confusing bugs if any initialiser depends on a previously declared member.

```cpp
    virtual ~Option() = default;
```
A virtual destructor is required on any class that will be deleted through a pointer to
its base type. Without it, `delete basePtr` calls only `~Option()`, not the derived
class destructor — undefined behaviour and a resource leak. The cost: one pointer entry
in the vtable. `= default` tells the compiler to generate the standard implementation
(which does nothing for `Option` but will correctly call derived destructors).

Alternative: a protected non-virtual destructor. If `Option` objects are never deleted
through base pointers (e.g. always used by value or with `std::unique_ptr<EuropeanOption>`),
a protected non-virtual destructor prevents the pattern entirely and costs nothing. That
design is valid but makes the class harder to use in containers of `unique_ptr<Option>`,
which is a common pattern.

```cpp
    Option(const Option&)            = delete;
    Option& operator=(const Option&) = delete;
```
Explicitly deletes copy construction and copy assignment. The comment in the source says
options are not value types in this design — they are polymorphic entities managed by
pointer. Allowing copies of polymorphic objects leads to *object slicing*: copying a
`EuropeanOption` through an `Option&` would copy only the `Option` subobject, silently
losing the derived part. Deleting the operations makes slicing a compile error.

Note: because the copy operations are deleted and no move operations are declared, the
move constructor and move assignment are also implicitly deleted (in C++11 and later, if
you declare/delete any of the five special members, the others may be suppressed). This
means `Option`s are entirely non-transferable, reinforcing the "use through pointer"
design.

```cpp
    virtual double payoff(double spotAtExpiry) const = 0;
```
A pure virtual function (`= 0`). This makes `Option` an abstract class — you cannot
instantiate `Option` directly, only concrete subclasses. `const` is essential: payoff
depends only on the contract terms and the provided spot price, never on mutable state.
A non-const payoff would be suspicious. The `virtual` keyword enables dynamic dispatch:
at runtime the correct derived `payoff()` is called regardless of the static type of
the pointer/reference.

```cpp
    double     strike() const { return strike_; }
    double     expiry() const { return expiry_; }
    OptionType type()   const { return type_;   }
```
Inline accessor functions defined in the class body. The compiler treats inline functions
as candidates for inlining at the call site, eliminating the function call overhead.
For these one-line accessors the compiler will almost certainly inline them regardless
of the `inline` keyword (which is implicit for in-class definitions). Returning by value
(not by const reference) for scalar types is correct: returning a reference to a member
exposes internal state unnecessarily.

```cpp
protected:
    double     strike_;
    double     expiry_;
    OptionType type_;
```
`protected` rather than `private`. This allows `EuropeanOption::payoff()` to access
`strike_` and `type_` directly without going through the accessors. The alternative
(making them `private` and using accessors in derived classes) is arguably safer but
adds indirection. For a small hierarchy of 2–3 classes `protected` is a pragmatic
choice. A counter-argument: `protected` exposes implementation details to all future
derived classes, creating tighter coupling. In Iteration 2 the hierarchy is replaced
by templates, so this is a moot point.

---

## EuropeanOption.h

```cpp
#include "options/Option.h"
```
The header path assumes the include root is the project's `include/` directory (as
configured by `include_directories(include)` and `target_include_directories(...
PUBLIC include)` in CMakeLists.txt). The file lives at `include/options/Option.h`.
This subdirectory convention namespaces headers on the filesystem, preventing collisions
between `options/Option.h` and a hypothetical `models/Option.h`.

```cpp
class EuropeanOption : public Option {
public:
    EuropeanOption(double strike, double expiry, OptionType type)
        : Option(strike, expiry, type) {}
```
Single constructor that delegates entirely to the `Option` base constructor via the
initialiser list. `EuropeanOption` adds no data members of its own — it only provides a
concrete `payoff()` implementation. The `public` inheritance specifier is required here
(unlike in Java or Python, C++ defaults to `private` inheritance for classes). `public`
inheritance means `EuropeanOption` IS-A `Option` in every context — a pointer to
`EuropeanOption` can be passed wherever `const Option&` is expected.

Alternative: non-public inheritance would be used for "implemented-in-terms-of"
relationships, where the base is a private implementation detail. That is not the case
here — the inheritance really does represent a type hierarchy.

```cpp
    double payoff(double spotAtExpiry) const override;
```
`override` (C++11 keyword) explicitly declares that this function overrides a virtual
function in a base class. The compiler will error if no such base virtual function exists.
Without `override`, a typo in the function signature (e.g. `payoff(float)` instead of
`payoff(double)`) would silently create a *new* virtual function that never gets called —
a classic C++ footgun. Always use `override` on derived virtual functions.

---

## EuropeanOption.cpp

```cpp
#include "options/EuropeanOption.h"
#include <algorithm> // std::max
```
`<algorithm>` provides `std::max`. Explicitly commenting why an include is needed is
good practice — it tells future readers (and you six months from now) that removing
`<algorithm>` will break the `std::max` calls below.

```cpp
double EuropeanOption::payoff(double spotAtExpiry) const {
    switch (type_) {
        case OptionType::Call:
            return std::max(spotAtExpiry - strike_, 0.0);
        case OptionType::Put:
            return std::max(strike_ - spotAtExpiry, 0.0);
    }
    return 0.0; // unreachable — silences compiler warning
}
```
Four points about this implementation:

1. **`switch` on `enum class`**: The compiler can warn (with `-Wswitch`) if a case is
   missing from a switch on an enum. This is a useful safety net: if a new `OptionType`
   (e.g. `OptionType::Digital`) is added without updating `payoff()`, the compiler will
   warn. A chain of `if/else` would not provide this guarantee.

2. **`std::max(spotAtExpiry - strike_, 0.0)`**: `std::max` with two `double` arguments
   is preferable to `(spotAtExpiry - strike_ > 0) ? spotAtExpiry - strike_ : 0.0`
   because it is clearer and avoids computing the difference twice. It is also
   preferable to `std::max<double>(...)` explicit template instantiation — the template
   argument is inferred.

3. **`0.0` not `0`**: Passing `0` (int) would cause a type mismatch: `std::max`
   requires both arguments to have the same type. The call `std::max(double, int)` does
   not compile without a cast. `0.0` is a `double` literal — correct type, no cast.

4. **Trailing `return 0.0`**: The `switch` is exhaustive over all `OptionType` values
   (both `Call` and `Put` are handled), so this line is unreachable. However, without
   it, GCC warns about a non-void function that may not return a value. The comment
   makes the intent explicit. An alternative is `__builtin_unreachable()` (GCC/Clang) or
   `std::unreachable()` (C++23), which tells the compiler the path is truly impossible
   and lets it optimise accordingly. Deferred for simplicity.

---

## PricingModel.h

```cpp
class Option;  // forward declaration
```
A forward declaration tells the compiler that `Option` is a class name, without
providing its full definition. This is sufficient when we only use `Option` in a
function signature as a reference (`const Option&`) — the compiler doesn't need to know
the class layout to parse a reference parameter. Using a forward declaration instead of
`#include "options/Option.h"` in this header:
- Reduces compilation time (other files that include `PricingModel.h` don't also pull
  in `Option.h` transitively unless they need it).
- Breaks potential circular include dependency (not an issue here, but the habit is good).

The rule: include what you need, forward-declare what you can.

```cpp
class PricingModel {
public:
    virtual ~PricingModel() = default;

    virtual PricingResult price(const Option& option,
                                const MarketData& market) const = 0;
};
```
`PricingModel` is a pure interface — one pure virtual function and a virtual destructor.
Design observations:

1. **`price()` is `const`**: Pricing models are conceptually stateless calculators.
   Making `price()` const enforces this design: the method cannot modify model state.
   The exception is `MonteCarloModel`, which has `mutable` RNG state. The `mutable`
   keyword on its members restores physical mutability while preserving logical constness
   (i.e. calling `price()` twice with the same arguments produces statistically equivalent
   results even though the RNG state changes).

2. **`const Option&` and `const MarketData&`**: Passed by const reference to avoid
   copies (these could be large objects in a more complex design). `const` signals that
   the model does not modify its inputs. Pass-by-value would be fine for `MarketData`
   (it's small — four `double`s = 32 bytes) but the convention of passing aggregates
   by const reference is consistent and clear.

3. **Separation of concerns**: `PricingModel::price()` takes both an `Option` and
   `MarketData` as separate inputs. An alternative design would embed `MarketData`
   inside the `Option`. That would couple contract terms (strike, expiry) with market
   state (spot, vol), which is wrong — spot and vol change continuously while the
   contract terms are fixed. Keeping them separate allows you to reprice the same option
   against different market scenarios without modifying the option object.

---

## BlackScholesModel.h

```cpp
class BlackScholesModel : public PricingModel {
public:
    PricingResult price(const Option& option,
                        const MarketData& market) const override;
private:
    double N(double x) const;
};
```
`N()` is declared `private` because it is an implementation detail of the BS formula —
no caller outside `BlackScholesModel` needs access to the normal CDF directly. Making it
`private` enforces this boundary. It is declared in the header (not just the .cpp) so
that the function call within `BlackScholesModel.cpp` can be resolved; member functions
declared in `.cpp` only are not part of the class interface and cannot be called as
member functions.

Alternative: a free function in an anonymous namespace in `BlackScholesModel.cpp`.
```cpp
// In BlackScholesModel.cpp, anonymous namespace
namespace { double N(double x) { ... } }
```
This would give the same encapsulation (hidden from all other translation units) without
polluting the class definition. Both approaches are valid; the private member approach
was chosen here, which is idiomatic when the function logically belongs to the class.

---

## BlackScholesModel.cpp

```cpp
double BlackScholesModel::N(double x) const {
    return 0.5 * std::erfc(-x / std::sqrt(2.0));
}
```
Implements the standard normal CDF using `std::erfc` (complementary error function).
The mathematical identity is:

```
N(x) = Φ(x) = (1/2) * erfc(-x / √2)
```

Why `erfc` rather than a polynomial approximation (e.g. Abramowitz & Stegun 26.2.17)?

1. **Numerical stability in the tails**: For large `|x|`, `N(x)` approaches 0 or 1.
   Polynomial approximations lose significant digits in this region. `std::erfc` is
   implemented in hardware-optimised library code (typically using the algorithm from
   Cody 1969 or DLMF) with guaranteed accuracy across the full range.

2. **Standard library**: No code to maintain or test. Any bug in a homegrown
   approximation is yours to find and fix.

3. **Performance**: `std::erfc` is a single transcendental function call. On modern
   x86 with `-O2`, the compiler may use the hardware `FIST` or vectorised variants.
   Polynomial approximations are faster only when they are intentionally less accurate
   (e.g. 4-5 significant digits). For finance, accuracy matters more than the few
   nanoseconds saved.

```cpp
const auto* european = dynamic_cast<const EuropeanOption*>(&option);
if (!european) {
    throw std::invalid_argument("BlackScholesModel: ...");
}
```
`dynamic_cast<T*>` on a pointer returns `nullptr` if the cast fails (the object is not
of type `T`). `dynamic_cast<T&>` on a reference throws `std::bad_cast` on failure.
The pointer form is used here so the error message can be customised (throwing
`std::invalid_argument` rather than `std::bad_cast`).

`dynamic_cast` requires RTTI (Run-Time Type Information), which the compiler generates
automatically. The cost is typically ~20–50 ns per call on a modern CPU due to vtable
traversal. This is the exact cost Iteration 2 eliminates by making the constraint a
compile-time template parameter rather than a runtime check.

Why is this check necessary? `BlackScholesModel::price()` takes `const Option&`, but the
BS formula is only correct for European options. An American option, Asian option, or
barrier option would produce wrong prices silently without this guard. The dynamic_cast
makes the implicit assumption explicit and loud.

```cpp
const double S     = market.spot;
const double K     = option.strike();
const double r     = market.riskFreeRate;
const double sigma = market.volatility;
const double T     = option.expiry();
```
Local aliases using `const double`. This block:
1. Brings variable names in line with mathematical notation (S, K, r, σ, T)
   making the formula below readable without referring back to the struct.
2. Declares them `const` — they are inputs to a pure function and must not change.
3. Uses `const double` not `auto` so that the type is explicit and any accidental
   narrowing conversion from a differently-typed field would be visible.

Alternative: use `market.spot` inline in the formula. This is less readable:
`std::log(market.spot / option.strike())` vs `std::log(S / K)`.

```cpp
if (T <= 0.0)     throw std::domain_error("...");
if (S <= 0.0)     throw std::domain_error("...");
if (sigma <= 0.0) throw std::domain_error("...");
```
Input validation at the model's boundary. `std::domain_error` is the appropriate
exception type for mathematical functions called outside their domain (from `<stdexcept>`).
These guards prevent `std::log(0.0)` (returns `-inf`) and division by zero in the
`d1` formula. In production code, additional checks might include `K <= 0.0` and
`sigma > 10.0` (unrealistic vol that signals a data error).

```cpp
const double sqrtT = std::sqrt(T);
const double d1 = (std::log(S / K) + (r + 0.5 * sigma * sigma) * T)
                  / (sigma * sqrtT);
const double d2 = d1 - sigma * sqrtT;
```
The standard BS d1 and d2 terms (no-dividend form). `sqrtT` is computed once and
reused in both `d1` and `d2` — avoiding calling `std::sqrt(T)` twice. With `-O2` the
compiler may hoist it automatically (Common Subexpression Elimination), but making it
explicit is both faster in debug builds and self-documenting.

The Itô correction term `0.5 * sigma * sigma` (often written `σ²/2`) in the drift of
d1 comes from Itô's lemma applied to the log-price process. Without it you would have
the wrong mean for the risk-neutral distribution and the formula would be incorrect.

```cpp
const double discountedStrike = K * std::exp(-r * T);
```
Pre-computed to avoid calling `std::exp(-r*T)` twice (once in price, once would appear
if we wrote `K*exp(-rT)` inline). `std::exp` is a transcendental function (~20–50 ns);
hoisting it is a valid micro-optimisation. The spot `S` appears undiscounted because
with no dividends the discount factor on the spot term is 1.

```cpp
switch (option.type()) {
    case OptionType::Call:
        result.price = S * N(d1) - discountedStrike * N(d2);
        result.delta = N(d1);  // dCall/dS
        break;
    case OptionType::Put:
        result.price = discountedStrike * N(-d2) - S * N(-d1);
        result.delta = N(d1) - 1.0;  // dPut/dS
        break;
}
```
The standard BS closed-form expressions for price and delta (dV/dS):

- **Call price**: `S*N(d1) - K*e^(-rT)*N(d2)`
- **Put price**: `K*e^(-rT)*N(-d2) - S*N(-d1)` (derived from put-call parity)
- **Call delta**: `N(d1)` — always in (0,1)
- **Put delta**: `N(d1) - 1` — always in (-1,0)

Note that `N(-d1) = 1 - N(d1)` and `N(-d2) = 1 - N(d2)` by symmetry of the normal
distribution. The code calls `N(-d1)` directly rather than `1.0 - N(d1)` because the
`erfc`-based implementation handles negative arguments accurately without cancellation
error.

```cpp
static constexpr double kInvSqrt2Pi = 0.3989422804014327;
const double nd1 = kInvSqrt2Pi * std::exp(-0.5 * d1 * d1);

result.gamma = nd1 / (S * sigma * sqrtT);
result.vega  = S * nd1 * sqrtT;

const double thetaBase = -S * nd1 * sigma / (2.0 * sqrtT);
switch (option.type()) {
    case OptionType::Call:
        result.theta = (thetaBase - r * discountedStrike * N(d2))  / 365.0;
        result.rho   =  K * T * discountedStrike * N(d2);
        break;
    case OptionType::Put:
        result.theta = (thetaBase + r * discountedStrike * N(-d2)) / 365.0;
        result.rho   = -K * T * discountedStrike * N(-d2);
        break;
}
```
The five Greeks following price and delta:

**N'(d1) — the standard normal PDF at d1:**
`nd1 = (1/√(2π)) · exp(−d1²/2)` appears in every second-order Greek. It is computed once
and reused. `kInvSqrt2Pi = 1/√(2π) ≈ 0.3989…` is a `static constexpr` so it is
computed at compile time and placed in read-only memory — no runtime cost.

**Gamma (d²V/dS²):**
`Γ = N'(d1) / (S · σ · √T)` — identical for calls and puts. Gamma is the rate of change
of delta with respect to spot. It is highest at-the-money, near expiry, where a small
spot move causes a large delta change. Gamma is always positive for long options.

**Vega (dV/dσ):**
`ν = S · N'(d1) · √T` — identical for calls and puts. Vega is the sensitivity to a
1-unit change in volatility (e.g. vol going from 20% to 21% changes the price by ~ν·0.01).
Vega increases with time to expiry (more uncertainty) and is maximised ATM.

**Theta (dV/dt, per calendar day):**
```
thetaBase = −S · N'(d1) · σ / (2√T)
call:  Θ = (thetaBase − r · Ke^(−rT) · N(d2))  / 365
put:   Θ = (thetaBase + r · Ke^(−rT) · N(−d2)) / 365
```
`thetaBase` is the volatility component of time decay (common to both). The second term
is the interest rate component: for calls, the holder forgoes the risk-free rate on the
discounted strike — a negative contribution (more decay). For puts, the holder benefits
from interest on the strike, reducing the magnitude of theta.

Division by 365 converts the per-year formula to per-calendar-day. Some practitioners
use 252 (trading days); 365 is the standard for listed equity options. Theta is almost
always negative (long options lose value as time passes). A deep ITM short put can have
slightly positive theta.

**Rho (dV/dr):**
```
call:  ρ =  K · T · Ke^(−rT) · N(d2)
put:   ρ = −K · T · Ke^(−rT) · N(−d2)
```
Rho is positive for calls (higher rates → higher forward, call worth more) and negative
for puts. It is scaled by `K · T · e^(−rT)` — larger for longer-dated, higher-strike
options, small for short-dated or low-rate environments. For FX or index options with a
dividend or foreign rate, the formula would include a second rho term.

**Why compute Greeks alongside price:**
Avoiding two separate BS evaluations when both price and Greeks are needed (the server
always returns all six). Computing them together also avoids re-deriving d1, d2, sqrtT,
and discountedStrike a second time.

---

## MonteCarloModel.h

```cpp
#include <random>
#include <vector>
```
`<random>` provides the Mersenne Twister and the normal distribution. `<vector>` is
required for `std::vector<double>` used in `generatePath`'s return type.

```cpp
enum class VarianceReduction {
    None,       // plain Monte Carlo, one path per random draw
    Antithetic  // antithetic variates: each draw Z generates paths for +Z and −Z
};
```
An `enum class` (scoped, strongly typed) rather than a plain `enum` or a boolean
`useAntithetic` flag. The design rationale:

- **`enum class` over plain `enum`**: scoped enumerators (`VarianceReduction::None`)
  avoid polluting the enclosing namespace and prevent implicit integer conversion.
- **`enum class` over `bool`**: a `bool useAntithetic` parameter would make the call
  site `MonteCarloModel(100000, 1, 42, true)` — opaque. An enum makes it
  `MonteCarloModel(100000, 1, 42, VarianceReduction::Antithetic)` — self-documenting.
- **Extensibility**: the comment explicitly signals that control variates, importance
  sampling, and stratified sampling are future values. Adding a new enumerator does not
  change the class interface, any existing call site, or the switch/if-else structure
  in `price()` beyond adding a new branch.

```cpp
explicit MonteCarloModel(int numPaths = 100'000,
                         int numSteps = 1,
                         unsigned seed = 42,
                         VarianceReduction varReduction = VarianceReduction::None);
```
`explicit` prevents implicit conversion: `MonteCarloModel mc = 100000;` is a compile
error. Without `explicit`, a function taking `MonteCarloModel` by value could silently
accept an integer, constructing a temporary `MonteCarloModel`. The digit separator
`100'000` (C++14) improves readability of large numeric literals. `seed = 42` provides
a default for reproducibility — the same seed produces the same sequence of random
numbers, which is essential for unit testing and debugging. In production, seeds should
come from a hardware entropy source or the current time.

```cpp
mutable std::mt19937                     rng_;
mutable std::normal_distribution<double> dist_;
```
`mutable` allows these members to be modified even when the containing object is
accessed through a `const` reference or when a `const` method is executing. This is
necessary because `price()` is declared `const` (it is logically a pure function: same
contract, same market → same distribution of outcomes) but physically advances the RNG
state with each random variate drawn.

`std::mt19937` is the 32-bit Mersenne Twister. It has a period of 2^19937 - 1, which
is astronomically large — more than sufficient for any Monte Carlo simulation. It is
fast (single clock cycle per output on modern CPUs with compiler intrinsics) and passes
all standard statistical tests. The alternative, `std::mt19937_64`, produces 64-bit
outputs and is better when you need many values from a large state space; for standard
normal sampling via rejection, the 32-bit version is standard.

`std::normal_distribution<double>` uses the Ziggurat algorithm internally (on most
implementations). It draws from the standard Mersenne Twister output and converts to
normally distributed values. Each draw costs roughly 15–20 ns as measured in the
baseline.

Thread safety warning (from the header comment): if two threads call `price()` on
the same `MonteCarloModel` instance simultaneously, they both advance `rng_` and
`dist_` without synchronisation — a data race and undefined behaviour. Solutions
deferred to later iterations: `thread_local` RNG per thread, or pass an RNG by
reference into `price()`.

```cpp
std::vector<double> generatePath(double spot, double dt,
                                 double drift, double vol) const;
```
Returns a `std::vector<double>` by value. This means every call to `generatePath` does
a heap allocation (for the vector's buffer). With 100,000 paths, this is 100,000 heap
allocations per `price()` call. The allocator typically costs 30–40 ns per allocation
as measured in baseline_results.md — the dominant per-path cost. This is the Iteration
3 target. The fix will be to pass in a pre-allocated buffer by reference rather than
returning a new vector.

---

## MonteCarloModel.cpp

```cpp
MonteCarloModel::MonteCarloModel(int numPaths, int numSteps, unsigned seed)
    : numPaths_(numPaths)
    , numSteps_(numSteps)
    , rng_(seed)
    , dist_(0.0, 1.0)
{
    if (numPaths_ <= 0) throw std::invalid_argument("MC: numPaths must be positive");
    if (numSteps_ <= 0) throw std::invalid_argument("MC: numSteps must be positive");
}
```
Validation in the constructor body (after the initialiser list runs) rather than before
it. This is correct here because `rng_` and `dist_` are initialised from seed, 0.0, 1.0
— valid values independent of `numPaths`/`numSteps`. If any initialiser could throw based
on these values, you would need to validate before construction or use a factory. Throwing
from a constructor is well-defined in C++: all fully constructed members are destroyed in
reverse order, so there is no resource leak.

`dist_(0.0, 1.0)` constructs a standard normal distribution (mean=0, stddev=1). The
`std::normal_distribution` constructor takes (mean, stddev), not (mean, variance).
Passing the wrong parameterisation here would silently produce incorrectly scaled random
variables — a subtle bug. The values `0.0, 1.0` are explicit and correct.

```cpp
std::vector<double> MonteCarloModel::generatePath(...) const {
    std::vector<double> path(numSteps_);   // <-- heap allocation, Iteration 3 target
    double S = spot;
    const double volSqrtDt = vol * std::sqrt(dt);

    for (int i = 0; i < numSteps_; ++i) {
        const double Z = dist_(rng_);
        S *= std::exp(drift * dt + volSqrtDt * Z);
        path[i] = S;
    }
    return path;
}
```
Key observations:

1. **`std::vector<double> path(numSteps_)`**: Value-initialises all elements to 0.0
   (C++ guarantees default-initialisation of vector elements). Since we overwrite every
   element in the loop, this zero-fill is redundant work. `std::vector<double>
   path; path.reserve(numSteps_);` followed by `path.push_back(S)` would avoid the
   zero-fill, but the difference is negligible compared to the allocation cost itself.

2. **`const double volSqrtDt = vol * std::sqrt(dt)`**: `std::sqrt(dt)` is invariant
   across loop iterations (dt doesn't change). Hoisting it out of the loop avoids
   `numSteps_` redundant square-root computations. With `numSteps_=1` (the default) this
   makes no difference, but it becomes meaningful for multi-step path simulation.

3. **GBM discretisation**: `S *= std::exp(drift * dt + volSqrtDt * Z)`. This is the
   exact log-normal step, not Euler-Maruyama. The exact step has no discretisation error
   for GBM. Euler-Maruyama would be `S += S * (drift * dt + volSqrtDt * Z)` — it is an
   approximation that accumulates error for small dt. For European options with a single
   step, both give the same result to machine precision; for multi-step paths, the exact
   step is strictly better.

4. **`return path`**: Named Return Value Optimisation (NRVO). The compiler can
   construct `path` directly in the return slot, avoiding a copy. In C++17 and later,
   copy elision is *guaranteed* in some contexts (RVO for temporaries), but NRVO
   (returning a named local) is still only an allowed optimisation. In practice, all
   modern compilers apply it. After Iteration 3 refactoring, this function will take a
   `double*` buffer parameter and this return is eliminated entirely.

```cpp
std::vector<double> payoffs(numPaths_);

for (int i = 0; i < numPaths_; ++i) {
    const auto path          = generatePath(S, dt, drift, sigma);
    const double terminalSpot = path.back();
    payoffs[i]               = option.payoff(terminalSpot);
}
```
The main simulation loop. `option.payoff(terminalSpot)` is a virtual function call —
every iteration incurs the vtable lookup overhead (~3–5 ns per the baseline). In
Iteration 2, templates replace this with a static dispatch. Note `path.back()` returns
the last element — for a single-step path (`numSteps_=1`) this is `path[0]`, i.e. the
only element. This is correct and clear.

The `payoffs` vector stores all individual path payoffs before the mean and variance
are computed. An alternative is an online/streaming algorithm (Welford's method) that
computes mean and variance in a single pass without storing all payoffs, reducing memory
from O(numPaths) to O(1). This is a valid optimisation for large path counts. Deferred
for clarity.

```cpp
const double mean = std::accumulate(payoffs.begin(), payoffs.end(), 0.0)
                    / static_cast<double>(numPaths_);
```
`std::accumulate` (from `<numeric>`) sums the payoffs using the initial value `0.0`
(type `double`). The `0.0` initial value is important: if you wrote `0` (int), the
accumulation would proceed as integer arithmetic until the result exceeded INT_MAX and
overflowed — UB. `0.0` forces double arithmetic throughout.

`static_cast<double>(numPaths_)` is the correct way to convert `int` to `double` for
division. Without the cast, `/ numPaths_` would be integer division (truncation) if
the sum happened to be an integer type. With `Wconversion` enabled, an implicit
conversion without cast may generate a warning.

```cpp
double variance = 0.0;
for (double p : payoffs) {
    const double diff = p - mean;
    variance += diff * diff;
}
variance /= static_cast<double>(numPaths_ - 1);  // Bessel's correction
```
Sample variance using Bessel's correction (dividing by N-1 not N). The unbiased
sample variance uses N-1 in the denominator to correct for the fact that the sample
mean is used as an estimator of the true mean, which slightly underestimates the true
variance when divided by N. For N=100,000 paths the difference is negligible, but
using N-1 is statistically correct and signals awareness of estimation theory.

Note: computing variance with the two-pass formula (mean first, then sum of squared
deviations) is numerically stable. The one-pass formula `E[X^2] - E[X]^2` is prone to
catastrophic cancellation when variance is small relative to the mean — e.g. deep
in-the-money options.

```cpp
result.stdErr    = discount * std::sqrt(variance / static_cast<double>(numPaths_));
```
Standard error of the mean = stddev / sqrt(N) = sqrt(variance / N). This is the MC
convergence metric: at 95% confidence, the true price lies within ±1.96 * stdErr of
the estimated price. Multiplied by `discount` because the payoffs are undiscounted
raw payoffs; the final result is the discounted expected payoff.

```cpp
if (varReduction_ == VarianceReduction::Antithetic) {
    const int numPairs = numPaths_ / 2;
    std::vector<double> samples(static_cast<size_t>(numPairs));
    const double volSqrtDt = sigma * std::sqrt(dt);

    for (int i = 0; i < numPairs; ++i) {
        double Spos = S, Sneg = S;
        for (int step = 0; step < numSteps_; ++step) {
            const double Z   = dist_(rng_);
            const double fwd = drift * dt + volSqrtDt * Z;
            Spos *= std::exp(fwd);
            Sneg *= std::exp(drift * dt - volSqrtDt * Z);
        }
        samples[i] = 0.5 * (option.payoff(Spos) + option.payoff(Sneg));
    }
    // variance over numPairs pair-samples, then stdErr = sqrt(variance/numPairs)
    result.stdErr = discount * std::sqrt(variance / static_cast<double>(numPairs));
}
```
**Antithetic variates — how and why:**

For each draw `Z ~ N(0,1)` two paths are simulated simultaneously: one using `+Z` and
one using `−Z`. For the standard GBM step `S *= exp(drift*dt + σ√dt·Z)`, negating Z
gives a path that drifts in the opposite direction from the same starting point. The
pair-average sample `y_i = (payoff(S_T(+Z)) + payoff(S_T(−Z))) / 2` has lower variance
than a single-path sample because the two payoffs are negatively correlated.

**Why negative correlation reduces variance:**
```
Var(y_i) = Var((f(+Z) + f(−Z)) / 2)
         = (Var(f(+Z)) + Var(f(−Z)) + 2·Cov(f(+Z), f(−Z))) / 4
```
For monotone payoffs (calls and puts), `f(+Z)` and `f(−Z)` move in opposite directions,
so `Cov < 0`. The variance of the pair-average is strictly less than the variance of a
single-path sample, meaning we get a tighter confidence interval for the same number of
random draws.

**Effective path count:** `numPaths_` is treated as the total effective path count.
`numPairs = numPaths_ / 2` pairs are simulated; each pair consumes one RNG draw but
counts as two paths for the x-axis comparison. If `numPaths_` is odd, the last path is
silently dropped (integer truncation). This is documented in the header.

**stdErr denominator:** The standard error is computed over `numPairs` samples (each a
pair-average), not `numPaths_`. This is correct: the pair-average is the elementary
random variable; N/2 of them estimate the mean. Using `numPaths_` in the denominator
would understate the stdErr by a factor of √2 and make the convergence appear better
than it is.

---

## Benchmark.h

```cpp
template<typename Fn>
static BenchmarkResult run(const std::string& name, Fn&& fn, int iterations = 1000) {
```
`Fn&&` is a forwarding reference (also called universal reference), not an rvalue
reference. When `Fn` is deduced as a template parameter and the parameter is `&&`, it
can bind to both lvalues and rvalues and preserves the value category. This allows
`run()` to accept lambdas, function pointers, and functors without unnecessary copies.
`std::forward<Fn>(fn)` would be used inside to perfect-forward into a call, but here
`fn()` is called directly, which is fine for callables.

The function is `static` — it doesn't need `this`, and callers write `Benchmark::run(...)`
without constructing a `Benchmark` object. An alternative design is a free function in
a namespace, but the class with static methods is idiomatic for utility classes in C++.

```cpp
using Clock = std::chrono::high_resolution_clock;
using Ns    = std::chrono::duration<double, std::nano>;
```
Type aliases using `using` (C++11), not `typedef`. `using` is preferred because:
- The syntax is consistent with template aliases (`template<typename T> using Vec = ...`)
- It reads left-to-right like a variable declaration: `Clock` is `std::chrono::...`
- `typedef` reads inside-out, especially for function pointer types.

`high_resolution_clock` is the highest resolution clock available. On Linux/GCC it
typically maps to `CLOCK_REALTIME` or `CLOCK_MONOTONIC` with nanosecond resolution via
`clock_gettime`. On MSVC on Windows, its resolution may be limited to 100ns ticks.

`std::chrono::duration<double, std::nano>` represents a duration in nanoseconds stored
as a `double`. The cast `Ns(t1 - t0).count()` converts the integer nanosecond count
from `clock_gettime` to a `double` for arithmetic.

```cpp
times.reserve(iterations);
```
Reserves capacity for `iterations` elements before the timing loop. Without this,
`push_back` would trigger reallocation (doubling the capacity) potentially several times.
Reallocation during the timing loop would add noise to the measurements. This is a
classic example of `reserve`'s intended use.

```cpp
const auto t0 = Clock::now();
fn();
const auto t1 = Clock::now();
times.push_back(Ns(t1 - t0).count());
```
Wraps `fn()` with clock readings. `Clock::now()` returns a `time_point` object. The
subtraction `t1 - t0` returns a duration in the clock's native units (nanoseconds).
`Ns(...)` converts to `duration<double, std::nano>` and `.count()` extracts the raw
scalar value.

A potential issue: the compiler might reorder `t0 = Clock::now()` relative to the
function call due to the as-if rule. In practice, `Clock::now()` is a system call or
hardware instruction that the compiler cannot reorder across (it has observable side
effects). For rigorous benchmarking, a memory fence (`std::atomic_thread_fence` or
`_mm_lfence`) around the timed region would ensure ordering; this is omitted here for
simplicity and is a known limitation noted in the class comment.

```cpp
const double minT = *std::min_element(times.begin(), times.end());
const double maxT = *std::max_element(times.begin(), times.end());
```
Two separate passes over `times`. `std::minmax_element` would do it in a single pass
with 3/2 N comparisons vs 2N. The difference is negligible for N=10,000 timing
samples in a post-benchmark context (not in the hot path). The two-call form is
slightly more readable; the `minmax_element` version would be appropriate if N were
large.

---

## main.cpp

```cpp
using namespace options;
```
Brings the entire `options` namespace into scope. In a header file this would be a
serious mistake — it would force the namespace import on every file that includes the
header. In a `.cpp` file at translation-unit scope it is acceptable, though contentious.
The alternative (`options::` prefix everywhere) is safer and unambiguous but verbose for
a short driver file. The current choice is fine here; avoid it in any shared header.

```cpp
MarketData market { .spot=100.0, .riskFreeRate=0.05,
                    .volatility=0.20, .dividendYield=0.0 };
```
Designated initialiser syntax (C++20). The field names (`.spot=`, `.riskFreeRate=`, etc.)
are explicit, which:
1. Makes the code self-documenting without needing to look up the struct definition.
2. Prevents argument-order bugs. Without designated initialisers, `{100.0, 0.05, 0.20,
   0.0}` compiles fine even if you accidentally swap the order of fields.
3. Is readable to financial practitioners who recognise the parameter names.

Requirement: C++20 (`CMAKE_CXX_STANDARD 20`). In C++17, you would need to write a
constructor or live with positional initialisation.

```cpp
EuropeanOption call(100.0, 1.0, OptionType::Call);
EuropeanOption put (100.0, 1.0, OptionType::Put);
BlackScholesModel bs;
MonteCarloModel   mc(500'000, 1, 42);
```
Stack-allocated objects (not heap-allocated via `new`). Prefer stack allocation unless
you need heap-allocated lifetime, polymorphic containers, or very large objects.
Stack allocation is:
- Faster: no allocator overhead, guaranteed cache locality with nearby variables.
- Safer: no risk of forgetting to `delete`, no dangling pointers.

500,000 paths for the sanity check MC is deliberately higher than the benchmark
(100,000) to get tighter convergence for the numerical comparison.

```cpp
(void)result.price;
```
Suppresses the "unused variable" warning from `-Wunused-result` or `-Wall`. The
`result.price` access prevents the compiler from optimising away the entire
`bs.price(call, market)` call as dead code (since the result would be unused). This is
the poor-man's version of Google Benchmark's `DoNotOptimize(result)`. It works for this
purpose but is less rigorous: a sufficiently clever compiler could still hoist the call
out of the loop if it determines the result is always the same. For the baseline
measurements this is acceptable; a production benchmark would use a more robust
technique such as `volatile` sinks or explicit memory barriers.

```cpp
for (int paths : { 1'000, 10'000, 100'000 }) {
```
Range-for over a braced initialiser list. The list `{1000, 10000, 100000}` is an
`std::initializer_list<int>`. This idiom avoids a C-style array or a named vector for
a small fixed set of values. The `int` type for the list matches the `paths` variable
type and the `MonteCarloModel` constructor parameter type — no implicit conversion.

```cpp
const std::string label = "MC " + std::to_string(paths) + " paths (100 iters)";
```
`std::to_string` converts `int` to `std::string`. The concatenation `"MC " + ...`
works because `"MC "` is implicitly converted to a `std::string` and `operator+` is
defined for `std::string`. (Plain `char* + char*` does not work as string concatenation
— it would be pointer arithmetic.) For performance-critical code a `std::ostringstream`
or `std::format` (C++20) would be preferable; for a benchmark label that is constructed
once per outer loop iteration this is fine.

```cpp
<< "  implied per-path : "
<< (r.meanNs / paths) << " ns\n\n";
```
Derives the per-path cost by dividing the total mean time by the number of paths.
This metric from the baseline (~70 ns/path) is what the project's iteration targets
are designed to reduce. The division is valid because MC scaling is linear (confirmed
by the baseline measurements showing ~71, 68, 73 ns/path at 1k, 10k, 100k paths).

---

## baseline_results.md

A committed benchmark snapshot serving as the numeric baseline for Iteration 2 comparisons.
Key figures:

- **BS single price**: ~83 ns minimum / ~90 ns mean. After Iteration 2 (template
  dispatch, no `dynamic_cast`), the target is ~60–75 ns — a modest improvement since
  the dominant cost is transcendental functions (`exp`, `erfc`, `sqrt`), not dispatch.

- **MC per-path cost**: ~70 ns. Decomposed as:
  - GBM arithmetic: ~5–10 ns
  - `std::normal_distribution` draw (Ziggurat): ~15–20 ns
  - `std::vector` allocation: **~30–40 ns** (Iteration 3 target)
  - `payoff()` virtual dispatch: ~3–5 ns (Iteration 2 target)

- **Put-call parity error < 1e-12**: confirms the BS implementation is numerically
  correct at machine precision, validating the `erfc`-based N(x) implementation.

- **MC within 0.43 stderr of BS**: expected convergence. At 500k paths, stderr ≈ 0.021,
  so a 0.43-stderr deviation is well within the 2-sigma (~95%) confidence interval.

---

## server.cpp

The HTTP server that bridges the C++ pricing engine to the browser GUI.
Uses two single-header vendor libraries: **cpp-httplib** (HTTP) and **nlohmann/json** (JSON).

```cpp
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _WIN32_WINNT 0x0A00
```
Three Windows-specific guards that must appear before any Windows headers (which httplib.h
pulls in transitively). `WIN32_LEAN_AND_MEAN` strips rarely-used Win32 APIs from the
inclusion, significantly reducing compile time. `NOMINMAX` prevents Windows.h from defining
`min`/`max` macros that would clash with `std::min`/`std::max` and break `<algorithm>` and
`json.hpp`. `_WIN32_WINNT 0x0A00` targets Windows 10 — cpp-httplib v0.37 uses
`CreateFile2()` which requires at least Windows 10 API surface; without this define the
compiler raises an error.

```cpp
void setCors(httplib::Response& res) { ... }
```
Cross-Origin Resource Sharing (CORS) headers added to every response. Browsers enforce the
Same-Origin Policy: a page served from `file://` is a different origin than `localhost:18080`,
so any `fetch()` call would be blocked by default. The three headers (`Allow-Origin: *`,
`Allow-Methods`, `Allow-Headers`) satisfy the browser's preflight check. Without them, the
GUI cannot contact the server even though both run on the same machine.

```cpp
std::vector<double> generateVisPath(double S, double r, double sigma,
                                    double T, int steps, std::mt19937& rng)
```
Generates one GBM price path for display purposes only. This is intentionally separate from
`MonteCarloModel::generatePath` (which is `private`) because the use-case differs: the
display needs a small number of paths with step-level resolution, while the pricer uses
single-step exact discretisation. The function takes the RNG by reference so paths share
state and look visually varied rather than all starting from the same seed position.

```cpp
std::vector<ConvPoint> buildConvergenceSeries(...)
```
Runs a standalone MC simulation with a fixed seed (42) and records the running price
estimate at 20 log-spaced checkpoints between path 50 and `maxN`. Log-spacing is chosen
because MC error decays as 1/√N — equal spacing on a log scale makes the improvement
curve appear linear on a log-x chart, which is the standard way to visualise MC convergence.
The `ConvPoint` struct carries `{n, price, se}` (path count, running estimate, running
standard error).

```cpp
svr.Options("/price", ...)  // CORS preflight
svr.Post("/price", ...)     // main handler
```
`OPTIONS` is the HTTP method browsers use for a CORS preflight request — they send it
automatically before the real `POST` to check if the server allows cross-origin requests.
The `POST` handler is the core of the server: it deserialises the JSON body, validates
inputs, instantiates `BlackScholesModel` and `MonteCarloModel` per-request (ensuring
thread-safe RNG state), runs both pricers, builds the convergence series and visual paths,
then serialises the response as JSON.

**Per-request instantiation pattern:**
```cpp
MonteCarloModel mc(numPaths, 1, 42);
```
`MonteCarloModel` holds a `mutable std::mt19937` which is not thread-safe. Creating a
fresh instance per request ensures each request has its own independent RNG. The fixed
seed 42 makes results reproducible: the same inputs always return the same MC price, which
is useful for debugging and testing.

**Response schema:**
```json
{
  "bs":         { "price", "delta", "stdErr", "d1", "d2" },
  "bsOther":    { "price", "delta", "stdErr", "d1", "d2" },
  "mc":         { "price", "stdErr" },
  "convergence": [{ "n", "price", "se" }, ...],
  "paths":      [[100.0, 101.3, ...], ...]
}
```
`bsOther` carries the BS price for the complementary option type (if the request is for a
call, `bsOther` is the put price). This lets the GUI compute put-call parity without making
a second request. `d1`/`d2` are re-derived inline in `server.cpp` because `PricingResult`
doesn't store them — they are intermediate values, not part of the public pricing interface.

---

## gui.html

A single-file interactive GUI served as a static `file://` page. All layout, styling, and
logic are self-contained — no build step, no framework, no server required for the HTML
itself. The C++ server is only needed when "Run Pricing" is clicked.

**Architecture:**
The GUI is split into three logical layers:
1. **Controls** (left panel) — stepper inputs for S, K, r, σ, T, N, display paths; Call/Put toggle; Run and Explain buttons.
2. **Results** (right panel) — BS result card, MC result card, put-call parity bar, GBM path canvas, MC convergence canvas.
3. **Heatmap section** (below) — 2×2 grid of BS price and P&L surfaces computed in JS.

**Server connection (`runAll`):**
```javascript
async function runAll() {
  const response = await fetch('http://localhost:18080/price', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ S, K, r, sigma, T, type, numPaths, numVisPaths })
  });
  res = await response.json();
  // populate UI from res.bs, res.mc, res.convergence, res.paths
}
```
`runAll` is `async` so the `await fetch(...)` doesn't block the browser's event loop.
The error banner (`#server-error`) is shown if the server is unreachable, giving a clear
actionable message rather than a silent failure.

**JS-only BS calculation (heatmap only):**
```javascript
function bsCalc(S, K, r, sigma, T, type) { ... }
```
A lightweight JS reimplementation of the BS formula used exclusively for the 40×30 heatmap
grid (1,200 evaluations per render). This is acceptable because it's purely for display —
the actual pricing always goes through the C++ server. Using the server for the heatmap
would require 1,200 HTTP round-trips or a batch endpoint, which is unnecessary overhead
for a visual feature.

**Heatmap rendering:**
Each of the four canvases (Call Price, Put Price, Call P&L, Put P&L) is drawn by
`drawHeatmap()`. Key design decisions:
- **Square canvas via JS**: `canvas.style.height = canvas.offsetWidth + 'px'` forces a
  square before reading pixel dimensions. CSS `aspect-ratio` is unreliable in flex containers
  on some browsers because the intrinsic size calculation races with layout.
- **Cell value labels**: `strokeText` (dark outline) followed by `fillText` (white fill)
  gives readable labels on both dark and light cell backgrounds without needing to compute
  per-cell contrast.
- **Vertical colour bar**: rendered by `drawLegend()`, which receives the heatmap's CSS
  pixel height as an explicit argument so both canvases are exactly the same height.
- **Crosshair**: drawn at the current S/σ from `lastResults` so the user can see where
  their active pricing sits on each surface.

**Colour scales:**
- Price heatmaps: dark navy → deep blue → bright blue → gold. Perceptually progressive,
  colourblind-safe, matches the dark theme.
- P&L heatmaps: deep red → white (at PnL=0) → deep green. The white midpoint is the
  breakeven line; red/green carry their natural financial meaning (loss/profit) which is
  unambiguous in this context.

**Antithetic toggle (`.vr-toggle`):**
A simple checkbox that sets `useAntithetic` in the JSON body. When enabled, the
convergence chart title updates to "MC Convergence (antithetic)" and the convergence
series uses `2*total` on the x-axis so both plain and antithetic MC are compared at the
same total computational cost (number of random draws), not the same number of model
evaluations.

**IV panel (`.iv-section`):**
Inputs: market price of the option. Output: solved IV as a percentage, delta between
solved IV and the current σ input, and the convergence method ("Newton-Raphson" or
"bisection"). The `computeIV()` async function calls the `/iv` endpoint separately from
`runAll()` — IV solving is on-demand, not automatic, because it requires a market price
input that the user provides manually.

**MC estimator box (`.mc-estimator-box`):**
A small formula display: `V̂ = e^{−rT} · (1/N) Σ payoff(S_Tⁱ)`. Added for reviewers
who may not immediately recognise what the convergence chart is plotting. It documents
the discount factor, the path average, and the GBM terminal value without requiring the
reader to look at the C++ source.

---

## ImpliedVol.h

```cpp
struct IVResult {
    double      impliedVol = 0.0;
    bool        converged  = false;
    std::string message;
};
```
A result struct rather than throwing an exception on failure. The design choice:

- **`converged` flag**: callers check `result.converged` before trusting `impliedVol`.
  This is the same pattern as `std::from_chars` (C++17) — return a status alongside
  the value rather than either throwing or returning a sentinel like `NaN`. It makes
  the failure path explicit and forces the caller to handle it.
- **`message` field**: carries a human-readable reason for failure ("Price below
  intrinsic value", "max iterations reached"). This is the diagnostic surface — it goes
  directly into the server's JSON error response and into the GUI's error display,
  without any additional error classification logic.
- **Not using `std::optional<double>`**: `optional` would signal "no value" but not
  *why*. The three-field struct carries both the value and the status information.

```cpp
IVResult solveIV(double marketPrice, OptionType type,
                 double S, double K, double r, double T);
```
A free function, not a method on a class. The IV solver does not maintain state between
calls — it is a pure mathematical function of its inputs. A class would add nothing
except constructor syntax. The function lives in the `options` namespace (not `options::v2`)
because it works with V1 primitives (`BlackScholesModel`, `EuropeanOption`) and is used
by the server which predates the V2 template redesign.

---

## ImpliedVol.cpp

```cpp
static constexpr int    kMaxNRIter  = 50;
static constexpr int    kMaxBisIter = 100;
static constexpr double kTol        = 1e-8;
static constexpr double kMinVega    = 1e-10;
static constexpr double kSigmaLo   = 1e-6;
static constexpr double kSigmaHi   = 10.0;
```
`static constexpr` in the anonymous namespace of a `.cpp` file: the `static` prevents
the symbols from being exported (internal linkage); `constexpr` makes them compile-time
constants with no runtime storage. Each constant captures an explicit design decision:

- `kTol = 1e-8`: price convergence in dollars. For a $10 option, this is 0.000001%
  error — well inside any practical need. A tighter tolerance (e.g. 1e-12) would hit
  floating-point noise from the BS formula before converging.
- `kMinVega = 1e-10`: vega below this means the price is nearly insensitive to vol.
  Taking a Newton-Raphson step `σ -= diff/vega` with near-zero vega produces a wildly
  large step that exits the bracket. Switching to bisection is safer.
- `kSigmaLo = 1e-6` / `kSigmaHi = 10.0`: the solver's vol bracket. 1e-6 (~0.0001%)
  is effectively zero vol; 10.0 (1000%) covers any realistic market scenario. No
  option in normal markets should require vol outside this range.

```cpp
struct BSEval { double price; double vega; };

static BSEval evalBS(double sigma, OptionType type,
                     double S, double K, double r, double T) {
    BlackScholesModel bs;
    EuropeanOption opt(K, T, type);
    MarketData mkt{ S, r, sigma };
    const auto res = bs.price(opt, mkt);
    return { res.price, res.vega };
}
```
A local helper that bundles a full BS evaluation (price + vega) into a single call.
Why price and vega together: Newton-Raphson requires both at the same σ point, so it
makes sense to compute them in a single BS pass rather than two separate calls. The
helper is `static` (internal linkage — not visible outside this translation unit),
avoiding any risk of name collision.

**No-arbitrage bounds check:**
```cpp
const double intrinsic  = max(S - K·e^(−rT), 0)   // call
                        = max(K·e^(−rT) - S, 0)   // put
const double upperBound = S                         // call
                        = K·e^(−rT)                // put
```
The lower bound is the discounted intrinsic value (not just `max(S-K, 0)` because the
call is worth at least the forward intrinsic, accounting for time value of money on the
strike). The upper bound for a call is the spot itself — a call can never be worth more
than the underlying because the underlying dominates every payoff. For a put the upper
bound is `K·e^(−rT)` — the present value of the maximum payout if spot goes to zero.
If the market price violates either bound, no vol exists that could produce it under BS.

**Brenner-Subrahmanyam initial guess:**
```cpp
sigma ≈ (P / S) · √(2π / T)
```
Derived by solving the ATM approximation `C_ATM ≈ S · σ · √(T/2π)` for σ. It is exact
at-the-money and provides a reasonable starting point elsewhere, typically within a few
volatility points of the true IV. A good initial guess means NR converges in 3–5
iterations rather than 10–20.

**Newton-Raphson loop:**
```
σ_{n+1} = σ_n − (BS(σ_n) − marketPrice) / vega(σ_n)
```
NR has quadratic convergence near the root: the number of correct decimal places roughly
doubles each iteration. Safeguards:
1. `abs(diff) < kTol` → converged, return immediately.
2. `vega < kMinVega` → break, fall through to bisection.
3. New σ outside `[kSigmaLo, kSigmaHi]` → break (NR overshot; bisection has the bracket).

**Bisection fallback:**
Linear convergence — the bracket width halves each iteration. With 100 iterations and
initial bracket `[1e-6, 10.0]`, the final bracket width is `10.0 / 2^100 ≈ 8e-30`,
which is far below `kTol`. In practice convergence is declared much earlier via the
`abs(fMid) < kTol` check. Bisection requires a valid bracket where `f(lo) < 0 < f(hi)`;
if none exists (e.g. the market price is achievable at a vol outside [lo,hi]), the
function returns `converged=false` with a diagnostic message.

---

## v2/Concepts.h

This file is the core of Iteration 2. It replaces the `Option` abstract base class as
the mechanism for expressing what a model needs from an option type.

```cpp
#include <concepts>
```
`<concepts>` provides `std::convertible_to`, `std::same_as`, and the `requires`
expression syntax. It is a C++20 header — the reason the project requires C++20.

```cpp
template<typename T>
concept Priceable = requires(const T& opt, double spot) {
    { opt.strike()     } -> std::convertible_to<double>;
    { opt.expiry()     } -> std::convertible_to<double>;
    { opt.type()       } -> std::same_as<OptionType>;
    { opt.payoff(spot) } -> std::convertible_to<double>;
};
```
**Reading a concept definition:**
`requires(const T& opt, double spot) { ... }` is a *requires expression*. It introduces
hypothetical variable names (`opt`, `spot`) and then lists *requirements* — expressions
that must be well-formed for the concept to be satisfied. Each requirement has the form
`{ expression } -> constraint;`:

- `{ opt.strike() } -> std::convertible_to<double>` — the expression `opt.strike()`
  must compile, and its return type must be implicitly convertible to `double`. A type
  returning `float` satisfies this; one with no `strike()` method does not.
- `{ opt.type() } -> std::same_as<OptionType>` — stricter: the return type must be
  exactly `OptionType`, not merely convertible. This prevents accidentally satisfying
  the concept with a type whose `type()` returns `int`.

**V1 comparison:**
In V1, `BlackScholesModel::price(const Option& option, ...)` takes the abstract base.
If you pass an `AmericanOption*` through a `const Option&`, it compiles and links; the
error only surfaces at runtime when `dynamic_cast` throws. In V2,
`BlackScholesModel::price<EuropeanPriceable Opt>(...)` will not compile at all if `Opt`
does not satisfy the concept — the error message names the unsatisfied requirement.

```cpp
template<typename T>
concept EuropeanPriceable = Priceable<T>;
```
Currently a synonym for `Priceable`. The purpose is *documented intent*, not current
enforcement. When `AmericanOption` is added, it should include an `earlyExercise()`
method or some other distinguishing feature. `EuropeanPriceable` can then be refined
with a negative constraint (e.g. `requires(!has_early_exercise<T>)`) to reject American
options at the `BlackScholesModel` call site. Keeping the two concepts separate now
means that future refinement requires changing only the concept definition, not all
call sites.

---

## v2/BlackScholesV2.h

```cpp
template<EuropeanPriceable Opt>
PricingResult price(const Opt& option, const MarketData& market) const {
```
A constrained function template. `EuropeanPriceable Opt` is the C++20 shorthand for
`template<typename Opt> requires EuropeanPriceable<Opt>`. The compiler checks the
constraint at the call site — if `Opt` does not satisfy `EuropeanPriceable`, the error
appears at the `bs.price(opt, mkt)` line, not deep inside the function body.

**Why the implementation is header-only:**
Function templates must be visible (definition, not just declaration) at the point of
instantiation. When `main.cpp` writes `bsV2.price(option, market)`, the compiler needs
to generate code for `price<EuropeanOption>`. If the template were defined in a `.cpp`
file, the compiler would only see the declaration and produce a link error ("undefined
reference to `price<EuropeanOption>`"). The solution used everywhere in C++ is to put
template definitions in headers. This is not a stylistic choice — it is a fundamental
constraint of the language.

**Elimination of `dynamic_cast`:**
V1 `BlackScholesModel::price()` contains:
```cpp
const auto* european = dynamic_cast<const EuropeanOption*>(&option);
if (!european) throw std::invalid_argument("...");
```
This check occurs at runtime, every call. In V2 it is absent — the concept constraint
is checked at compile time and there is no code path that can reach a wrong-type
scenario. The `dynamic_cast` overhead (~5–10 ns for a directly-derived type, much more
through deep hierarchies) is gone.

**Formula identity:**
The BS formula (d1, d2, discountedStrike, N(d1), put-call logic, all Greeks) is
identical to V1. Any benchmark difference between V1 and V2 is attributable purely to:
1. Elimination of `dynamic_cast` (~5–10 ns per call)
2. Elimination of vtable dispatch on `price()` when called through a `PricingModel*`
   pointer (not present in the benchmark, which calls on a concrete object directly)

The measured difference on concrete objects is ~1 ns (V1: ~279 ns, V2: ~278 ns) —
consistent with the compiler already devirtualising the `price()` call when the static
type is known. The architectural improvement (compile-time safety) is the real gain.

---

## v2/MonteCarloV2.h

```cpp
template<Priceable Opt>
PricingResult price(const Opt& option, const MarketData& market) const {
    ...
    for (int i = 0; i < numPaths_; ++i) {
        double spot = S;
        for (int step = 0; step < numSteps_; ++step) {
            spot *= std::exp(drift * dt + volSqrtDt * dist_(rng_));
        }
        payoffs[i] = option.payoff(spot);
    }
```
Two simultaneous improvements over V1:

**1. No per-path vector allocation:**
V1 called `generatePath()` which allocated `std::vector<double>(numSteps_)` on every
path. With 100k paths this is 100k heap allocations per `price()` call at ~30–40 ns
each, totalling ~3–4 ms of pure allocation overhead. In V2, the path is accumulated in
a scalar `double spot`. No heap allocation occurs per path. The scalar is directly
incremented in the inner loop. This eliminates the dominant per-path cost measured in
baseline_results.md.

Why is this possible? In V1, `generatePath()` returned the entire path as a vector
because the `payoff()` call was decoupled from path generation — the caller would take
`path.back()` after the function returned. In V2, `payoff(spot)` is a direct inlineable
call on the concrete type, so the compiler can see that only the terminal value is needed
and the path need not be materialised.

**2. No virtual dispatch on `payoff()`:**
V1: `option.payoff(terminalSpot)` goes through a vtable pointer — the concrete
`EuropeanOption::payoff` is looked up at runtime.
V2: `option.payoff(spot)` on a `Priceable Opt` is a direct call to the concrete type's
method, known at compile time. The compiler can inline it into the inner loop. For
`EuropeanOption` this is `max(spot - K, 0.0)` — two arithmetic operations. Inlined,
there is no function call overhead and no branch misprediction on the vtable lookup.

**Measured impact (100k paths):**
- V1: ~200–320 ns/path
- V2: ~117–124 ns/path
- Saving: ~80–100 ns/path

The saving decomposes as:
- ~30–40 ns: vector allocation eliminated
- ~3–5 ns: virtual dispatch eliminated
- Remainder: compiler register allocation and loop structure improvements from knowing
  the payoff at compile time

**Remaining known issue — mutable RNG state:**
V2 still carries `mutable std::mt19937 rng_` and `mutable std::normal_distribution<double>
dist_`. Two concurrent calls to `price()` on the same `MonteCarloV2` instance race on
these members — a data race and undefined behaviour. Addressed in V3 by removing all
mutable state: each worker thread gets its own independently-seeded RNG.

---

## v3/MonteCarloV3.h

V3 adds multi-threading to V2's template-based, allocation-free inner loop. The design
is centred on two problems: RNG safety and variance merging.

**Thread partitioning:**
```cpp
const int totalUnits   = (varReduction_ == VarianceReduction::Antithetic)
                         ? numPaths_ / 2   // units = pairs
                         : numPaths_;      // units = paths
const int unitsPerThread = totalUnits / numThreads_;
const int remainder      = totalUnits % numThreads_;
```
Work is divided into `totalUnits` independent units (either individual paths or antithetic
pairs). The remainder is distributed one extra unit per thread to the first `remainder`
threads, so load is balanced to within one unit. Each thread processes a contiguous slice.

For antithetic mode, the partition is over *pairs*, not individual paths. This ensures
each thread maintains independent (Z, −Z) pairs without inter-thread coordination. The
x-axis in convergence charts uses `2 * totalUnits` (effective paths) for fair comparison.

**Per-thread RNG seeding:**
```cpp
const unsigned thisSeed = seed_ ^ (static_cast<unsigned>(t + 1) * 2654435761u);
```
`2654435761` is Knuth's multiplicative hash constant (the golden ratio scaled to 32
bits). Multiplying the thread index by it and XOR-ing with the base seed spreads nearby
indices far apart in the 32-bit seed space. If instead you used `seed + t`, adjacent
seeds initialise the Mersenne Twister to similar states, potentially producing correlated
random streams that inflate variance in the combined estimate.

The `t + 1` (not `t`) prevents the thread-0 seed from being identical to the base seed
(`seed ^ 0 = seed`), which would make V3 with one thread produce the exact same sequence
as V2 seeded with `seed`. The base seed is still reproducible: the same constructor
arguments always produce the same per-thread seeds and therefore the same result.

**V3 is genuinely const and thread-safe:**
Unlike V1 and V2, V3 has no `mutable` members. Each call to `price()` creates all thread
state locally. `price()` can be called concurrently on the same V3 instance from multiple
threads without any synchronisation — there is nothing shared to race on.

**Chan's parallel variance formula:**
Each thread `t` accumulates `(sum_t, sumSq_t, count_t)`. Merging:

Step 1 — combined mean:
```
μ = Σ(sum_t) / Σ(count_t)
```

Step 2 — combined sum of squared deviations:
```
ssq_t = sumSq_t − count_t · μ_t²          (thread-local SSQ)
ssq   = Σ(ssq_t + count_t · (μ_t − μ)²)   (Chan's correction term)
variance = ssq / (N − 1)                   (Bessel's correction)
```

Why not just concatenate all payoffs and compute variance? That would require each
thread to return a vector of payoff values — O(numPaths) memory per call. Chan's formula
computes the exact same variance from O(1) state per thread. The intermediate value
`sumSq_t − count_t · μ_t²` is the thread-local sum of squared deviations computed from
its own mean, not the global mean. The `count_t · (μ_t − μ)²` term corrects for the
difference between the thread-local and global means. This is numerically stable: it
avoids the catastrophic cancellation of the naive `E[X²] − E[X]²` formula on large sums.

**Crossover point:**
Thread creation (`std::thread` constructor + join) costs approximately 1–2 ms on
Windows/MinGW. For small path counts this dominates computation: at 10k paths, V3 is
0.67× V2 (slower). The crossover to net speedup is around 50k paths. Above 100k paths,
V3 approaches near-linear scaling with thread count. This crossover is intentionally
documented and visible in the benchmark — it demonstrates understanding of parallelism
costs, not just the benefits.

**`numThreads = 0` default → `hardware_concurrency()`:**
```cpp
numThreads_(numThreads > 0
            ? numThreads
            : static_cast<int>(std::max(1u, std::thread::hardware_concurrency())))
```
`std::thread::hardware_concurrency()` returns the number of logical cores, or 0 if the
platform cannot determine it. The `std::max(1u, ...)` fallback ensures at least one
thread on exotic platforms. Passing an explicit thread count is useful for benchmarking
(to measure at 1, 2, 4, 8, 12 threads) without changing the default for normal use.

---

*End of ANNOTATIONS.md*
