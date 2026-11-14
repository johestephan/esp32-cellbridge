#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace SmsCodec {

struct EncodedPdu {
    std::string hexPdu;     // Full hex PDU starting with "00" (use SIM default SMSC)
    int tpduLength;         // Length in bytes excluding SMSC header (for AT+CMGS=<len>)
    bool isUcs2;
    int partIndex;          // 1-based
    int totalParts;
};

struct DecodedSms {
    bool valid = false;
    std::string smsc;
    std::string sender;
    std::string timestamp;  // ISO-8601 e.g. "2026-11-14T17:05:00+01:00"
    std::string text;       // UTF-8 decoded text
    bool isMultipart = false;
    uint16_t refId = 0;
    uint8_t totalParts = 1;
    uint8_t partNumber = 1;
    bool isUcs2 = false;
};

/**
 * Encode a UTF-8 message to one or more 3GPP SMS-SUBMIT PDUs.
 * Automatically selects GSM-7 (up to 160 chars, or 153/part) when possible,
 * and falls back to UCS-2 / UTF-16BE (up to 70 code units, or 67/part)
 * when emojis or non-GSM characters are present.
 */
std::vector<EncodedPdu> encodeSubmitPdus(const std::string& recipient,
                                         const std::string& utf8Text,
                                         uint8_t msgRef = 0,
                                         uint8_t concatRef = 1);

/**
 * Decode a 3GPP SMS-DELIVER (or SMS-SUBMIT) hex PDU into UTF-8 text & metadata.
 */
DecodedSms decodeDeliverPdu(const std::string& hexPdu);

/**
 * Helpers exposed for testing and UCS-2 text/number utilities.
 */
bool utf8ToGsm7Septets(const std::string& utf8, std::vector<uint8_t>& septets);
std::string gsm7SeptetsToUtf8(const std::vector<uint8_t>& septets);
std::vector<uint16_t> utf8ToUtf16(const std::string& utf8);
std::string utf16BeBytesToUtf8(const std::vector<uint8_t>& bytes);
std::string bytesToHex(const std::vector<uint8_t>& data);
bool hexToBytes(const std::string& hex, std::vector<uint8_t>& out);

} // namespace SmsCodec
