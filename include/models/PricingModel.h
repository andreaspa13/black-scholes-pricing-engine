#pragma once

#include "utils/MarketData.h"
#include "utils/PricingResult.h"

namespace options {

class Option; 
class PricingModel {
public:
    virtual ~PricingModel() = default;

    virtual PricingResult price(const Option& option, const MarketData& market) const = 0;
};

} 
