#pragma once

#include "options/Option.h"  // OptionType enum

#include <concepts>

namespace options::v2 {

/**
 * Compile-time contract for any type that can be priced by our models.
 *
 * This replaces the V1 Option abstract base class as the mechanism for
 * expressing what a model needs from an option.
 *
 * V1 (runtime polymorphism):
 *   Models take const Option& and access the interface through a vtable.
 *   The type constraint (is this actually a EuropeanOption?) is checked at
 *   runtime via dynamic_cast and throws std::invalid_argument on failure.
 *
 * V2 (compile-time polymorphism):
 *   Models are function templates constrained by this concept. A type that
 *   does not satisfy Priceable simply does not compile — there is no runtime
 *   path to reach a failed cast or a throw. The constraint is enforced by
 *   the compiler before the program runs.
 *
 * Zero-cost: no vtable, no pointer indirection, no dynamic_cast.
 * Calls to payoff(), strike(), expiry(), type() are direct and inlineable.
 */
template<typename T>
concept Priceable = requires(const T& opt, double spot) {
    { opt.strike()     } -> std::convertible_to<double>;
    { opt.expiry()     } -> std::convertible_to<double>;
    { opt.type()       } -> std::same_as<OptionType>;
    { opt.payoff(spot) } -> std::convertible_to<double>;
};

/**
 * Narrows Priceable to types representing European exercise.
 *
 * BlackScholesModel::price() is constrained to EuropeanPriceable.
 * Attempting to compile bs.price(americanOpt, mkt) will fail with a clear
 * concept constraint error, not a runtime exception.
 *
 * Currently identical to Priceable — the distinction is documentation of
 * intent. When AmericanOption is added, it will NOT satisfy EuropeanPriceable
 * (e.g. by adding a negative constraint on early-exercise methods), giving
 * the compiler the information to reject bs.price(americanOpt, mkt) at the
 * call site.
 */
template<typename T>
concept EuropeanPriceable = Priceable<T>;

} // namespace options::v2
