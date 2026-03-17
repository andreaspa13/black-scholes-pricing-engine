#include "options/EuropeanOption.h"
#include <algorithm>

namespace options {

double EuropeanOption::payoff(double spotAtExpiry) const {
    switch (type_) {
        case OptionType::Call:
            return std::max(spotAtExpiry - strike_, 0.0);
        case OptionType::Put:
            return std::max(strike_ - spotAtExpiry, 0.0);
    }
    return 0.0; // unreachable (silences compiler warning)
}

} 
