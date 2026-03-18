#pragma once

#include "options/Option.h" 

#include <concepts>

namespace options::v2 {

/**
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
 */
template<typename T>
concept EuropeanPriceable = Priceable<T>;

}
