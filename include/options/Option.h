#pragma once

namespace options {

enum class OptionType { Call, Put };

class Option {
public:
    Option(double strike, double expiry, OptionType type)
        : strike_(strike), expiry_(expiry), type_(type) {}

    virtual ~Option() = default;

    // Non-copyable: options are unique contracts; copying doesn't make sense.
    Option(const Option&)            = delete;
    Option& operator=(const Option&) = delete;

    virtual double payoff(double spotAtExpiry) const = 0; // Pure virtual function for payoff calculation

    // Getters for option parameters
    double     strike() const { return strike_; }
    double     expiry() const { return expiry_; }
    OptionType type()   const { return type_;   }

protected:
    double     strike_;
    double     expiry_;
    OptionType type_;
};

}
