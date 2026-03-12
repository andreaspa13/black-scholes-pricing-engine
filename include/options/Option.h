#pragma once

namespace options {

enum class OptionType { Call, Put };

/**
 * Abstract base class for all option contracts.
 *
 * Responsibilities:
 *   - Hold the contractual parameters (strike, expiry, type)
 *   - Define the payoff interface that pricing models call
 *
 * Deliberately does not know about pricing. Payoff is a property of
 * the contract; how you discount and simulate it is the model's job.
 *
 * The virtual destructor is required whenever you delete through a
 * base pointer — without it, derived destructors won't run and you
 * get UB. Cost is one extra vtable entry; always pay it.
 */
class Option {
public:
    Option(double strike, double expiry, OptionType type)
        : strike_(strike), expiry_(expiry), type_(type) {}

    virtual ~Option() = default;

    // Non-copyable for now — options are not value types in this design.
    // Revisit in iteration 2 when we move to value semantics with templates.
    Option(const Option&)            = delete;
    Option& operator=(const Option&) = delete;

    /**
     * Payoff at expiry given a terminal spot price.
     *
     * Pure virtual: every concrete option type must define its own payoff.
     * For a European call: max(spot - strike, 0)
     * For a European put:  max(strike - spot, 0)
     *
     * Marked const — payoff is a pure function of contract terms and
     * the provided spot; it must not mutate option state.
     */
    virtual double payoff(double spotAtExpiry) const = 0;

    // Accessors — return by value since these are scalars.
    double     strike() const { return strike_; }
    double     expiry() const { return expiry_; }
    OptionType type()   const { return type_;   }

protected:
    double     strike_;
    double     expiry_;
    OptionType type_;
};

} // namespace options
