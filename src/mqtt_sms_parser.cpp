#include "mqtt_sms_parser.h"

#include <cctype>

std::string trimSmsToken(const std::string& s) {
    size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) {
        ++start;
    }
    size_t end = s.size();
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) {
        --end;
    }
    return s.substr(start, end - start);
}

namespace {

bool looksLikePhoneTarget(const std::string& candidate) {
    std::string t = trimSmsToken(candidate);
    if (t.size() < 3 || t.size() > 22) return false;
    if (t[0] != '+' && !std::isdigit(static_cast<unsigned char>(t[0]))) {
        return false;
    }
    int digits = 0;
    for (size_t i = 0; i < t.size(); ++i) {
        char c = t[i];
        if (std::isdigit(static_cast<unsigned char>(c))) {
            ++digits;
        } else if (c == '+' && i == 0) {
            continue;
        } else if (c == ' ' || c == '-' || c == '(' || c == ')') {
            continue;
        } else {
            return false;
        }
    }
    return digits >= 3;
}

std::string compactPhoneSpaces(const std::string& raw) {
    std::string out;
    out.reserve(raw.size());
    for (char c : raw) {
        if (c != ' ') out.push_back(c);
    }
    return out;
}

} // namespace

MqttSmsCommand parseMqttPlainSmsPayload(const std::string& payload,
                                        const std::string& fallbackTo) {
    MqttSmsCommand cmd;
    std::string text = trimSmsToken(payload);
    if (text.empty()) return cmd;

    size_t colon = text.find(':');
    if (colon != std::string::npos && colon > 0 && colon <= 22) {
        std::string prefix = text.substr(0, colon);
        if (looksLikePhoneTarget(prefix)) {
            std::string rest = trimSmsToken(text.substr(colon + 1));
            if (!rest.empty()) {
                cmd.valid = true;
                cmd.to = compactPhoneSpaces(trimSmsToken(prefix));
                cmd.message = rest;
                return cmd;
            }
        }
    }

    std::string fb = trimSmsToken(fallbackTo);
    if (!fb.empty()) {
        cmd.valid = true;
        cmd.to = fb;
        cmd.message = text;
    }
    return cmd;
}
