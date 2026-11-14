#include "balance_parser.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>

namespace {

std::string toLowerAscii(std::string s) {
    for (char& ch : s) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return s;
}

std::string trimPlusAndSpace(const std::string& s) {
    size_t start = 0;
    while (start < s.size() && (s[start] == ' ' || s[start] == '+')) {
        ++start;
    }
    size_t end = s.size();
    while (end > start && s[end - 1] == ' ') {
        --end;
    }
    return toLowerAscii(s.substr(start, end - start));
}

bool parseNumberToken(const std::string& raw, double& outVal) {
    if (raw.empty()) return false;
    std::string norm;
    norm.reserve(raw.size());

    size_t lastDot = raw.rfind('.');
    size_t lastComma = raw.rfind(',');
    char decSep = 0;
    if (lastDot != std::string::npos && lastComma != std::string::npos) {
        decSep = (lastDot > lastComma) ? '.' : ',';
    } else if (lastComma != std::string::npos) {
        decSep = ',';
    } else if (lastDot != std::string::npos) {
        decSep = '.';
    }

    for (char ch : raw) {
        if (std::isdigit(static_cast<unsigned char>(ch))) {
            norm.push_back(ch);
        } else if (ch == decSep) {
            norm.push_back('.');
        }
    }
    if (norm.empty()) return false;

    char* endPtr = nullptr;
    double val = std::strtod(norm.c_str(), &endPtr);
    if (endPtr == norm.c_str() || val < 0.0 || val > 100000.0) {
        return false;
    }
    outVal = val;
    return true;
}

std::string formatAmount(double amount, const std::string& currency) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.2f", amount);
    if (currency == "EUR") return std::string("€ ") + buf;
    if (currency == "USD") return std::string("$ ") + buf;
    if (currency == "GBP") return std::string("£ ") + buf;
    if (!currency.empty()) return currency + " " + buf;
    return std::string(buf);
}

// Reads a numeric token starting after whitespace/colon/equals from pos
bool extractNumberAfter(const std::string& text, size_t pos, double& outVal) {
    size_t i = pos;
    while (i < text.size() && (text[i] == ' ' || text[i] == '\t' || text[i] == ':' || text[i] == '=')) {
        ++i;
    }
    if (i >= text.size() || !std::isdigit(static_cast<unsigned char>(text[i]))) {
        return false;
    }
    size_t j = i;
    while (j < text.size()) {
        char c = text[j];
        if (std::isdigit(static_cast<unsigned char>(c)) || c == '.' || c == ',') {
            ++j;
        } else {
            break;
        }
    }
    while (j > i && (text[j - 1] == '.' || text[j - 1] == ',')) {
        --j;
    }
    return parseNumberToken(text.substr(i, j - i), outVal);
}

// Reads a numeric token immediately preceding pos (suffix currency like "4.25 EUR")
bool extractNumberBefore(const std::string& text, size_t pos, double& outVal) {
    size_t j = pos;
    while (j > 0 && text[j - 1] == ' ') {
        --j;
    }
    if (j == 0 || !std::isdigit(static_cast<unsigned char>(text[j - 1]))) {
        return false;
    }
    size_t i = j;
    while (i > 0) {
        char c = text[i - 1];
        if (std::isdigit(static_cast<unsigned char>(c)) || c == '.' || c == ',') {
            --i;
        } else {
            break;
        }
    }
    return parseNumberToken(text.substr(i, j - i), outVal);
}

} // namespace

ParsedBalance parseBalanceText(const std::string& text) {
    ParsedBalance res;
    if (text.empty()) return res;

    struct Marker {
        const char* token;
        const char* currency;
    };
    static const Marker kMarkers[] = {
        {"eur", "EUR"},
        {"\xE2\x82\xAC", "EUR"}, // UTF-8 '€'
        {"euro", "EUR"},
        {"usd", "USD"},
        {"$", "USD"},
        {"gbp", "GBP"},
        {"\xC2\xA3", "GBP"},     // UTF-8 '£'
    };

    std::string lower = toLowerAscii(text);

    // First look for currency prefix ("EUR   4.25", "€ 4,25", "$5.00")
    for (const auto& m : kMarkers) {
        std::string tok(m.token);
        size_t pos = 0;
        while ((pos = lower.find(tok, pos)) != std::string::npos) {
            size_t tokLen = tok.size();
            bool wordLeft = (tokLen == 1 || tok[0] < 0) ||
                            (pos == 0 || !std::isalpha(static_cast<unsigned char>(lower[pos - 1])));
            bool wordRight = (tokLen == 1 || tok[0] < 0) ||
                             (pos + tokLen >= lower.size() || !std::isalpha(static_cast<unsigned char>(lower[pos + tokLen])));
            if (wordLeft && wordRight) {
                double val = 0.0;
                if (extractNumberAfter(text, pos + tokLen, val) || extractNumberBefore(text, pos, val)) {
                    res.valid = true;
                    res.amount = val;
                    res.currency = m.currency;
                    res.formatted = formatAmount(val, res.currency);
                    return res;
                }
            }
            pos += tokLen;
        }
    }

    // Fallback: look for first decimal number (e.g. USSD "Your balance is 4.25")
    for (size_t i = 0; i < text.size(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(text[i]))) continue;
        size_t j = i;
        bool hasSep = false;
        while (j < text.size()) {
            char c = text[j];
            if (std::isdigit(static_cast<unsigned char>(c))) {
                ++j;
            } else if ((c == '.' || c == ',') && j + 1 < text.size() &&
                       std::isdigit(static_cast<unsigned char>(text[j + 1]))) {
                hasSep = true;
                ++j;
            } else {
                break;
            }
        }
        if (hasSep) {
            double val = 0.0;
            if (parseNumberToken(text.substr(i, j - i), val)) {
                res.valid = true;
                res.amount = val;
                res.currency = "";
                res.formatted = formatAmount(val, "");
                return res;
            }
        }
        i = j;
    }

    return res;
}

bool isBalanceSenderMatch(const std::string& peer,
                          const std::string& configuredTarget,
                          const std::string& text) {
    std::string p = trimPlusAndSpace(peer);
    std::string t = trimPlusAndSpace(configuredTarget);
    if (!t.empty() && p == t) {
        return true;
    }
    std::string lower = toLowerAscii(text);
    if (lower.find("prepaid tegoed") != std::string::npos ||
        lower.find("beltegoed") != std::string::npos ||
        (lower.find("odido") != std::string::npos && lower.find("tegoed") != std::string::npos)) {
        return parseBalanceText(text).valid;
    }
    return false;
}
