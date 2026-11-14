#pragma once

#include <string>

struct ParsedBalance {
    bool valid = false;
    double amount = -1.0;
    std::string currency;   // e.g. "EUR", "USD", "GBP"
    std::string formatted;  // e.g. "€ 4.25"
};

ParsedBalance parseBalanceText(const std::string& text);

bool isBalanceSenderMatch(const std::string& peer,
                          const std::string& configuredTarget,
                          const std::string& text);
