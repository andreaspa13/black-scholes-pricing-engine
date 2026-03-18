#pragma once

#include "models/PricingModel.h"

namespace options {
class BlackScholesModel : public PricingModel {
public:
    PricingResult price(const Option& option, const MarketData& market) const override;

private:
    double N(double x) const;
};

}
