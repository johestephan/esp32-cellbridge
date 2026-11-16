#pragma once

#include <string>

struct MqttSmsCommand {
    bool valid = false;
    std::string to;
    std::string message;
};

// Parses plain text ("+31612345678: Hello!" or "Hello!" using fallbackTo)
// or extracts recipient/message when pre-parsed fields aren't from JSON.
MqttSmsCommand parseMqttPlainSmsPayload(const std::string& payload,
                                        const std::string& fallbackTo);

// Normalizes phone/short-code target strings (trims whitespace).
std::string trimSmsToken(const std::string& s);
