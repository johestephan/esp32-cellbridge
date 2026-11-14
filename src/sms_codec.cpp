#include "sms_codec.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace SmsCodec {

namespace {

// 3GPP TS 23.038 GSM 7-bit default alphabet -> Unicode code point
static const uint16_t kGsm7Basic[128] = {
    0x0040, 0x00A3, 0x0024, 0x00A5, 0x00E8, 0x00E9, 0x00F9, 0x00EC, // 0x00..0x07
    0x00F2, 0x00C7, 0x000A, 0x00D8, 0x00F8, 0x000D, 0x00C5, 0x00E5, // 0x08..0x0F
    0x0394, 0x005F, 0x03A6, 0x0393, 0x039B, 0x03A9, 0x03A0, 0x03A8, // 0x10..0x17
    0x03A3, 0x0398, 0x039E, 0x001B, 0x00C6, 0x00E6, 0x00DF, 0x00C9, // 0x18..0x1F
    0x0020, 0x0021, 0x0022, 0x0023, 0x00A4, 0x0025, 0x0026, 0x0027, // 0x20..0x27
    0x0028, 0x0029, 0x002A, 0x002B, 0x002C, 0x002D, 0x002E, 0x002F, // 0x28..0x2F
    0x0030, 0x0031, 0x0032, 0x0033, 0x0034, 0x0035, 0x0036, 0x0037, // 0x30..0x37
    0x0038, 0x0039, 0x003A, 0x003B, 0x003C, 0x003D, 0x003E, 0x003F, // 0x38..0x3F
    0x00A1, 0x0041, 0x0042, 0x0043, 0x0044, 0x0045, 0x0046, 0x0047, // 0x40..0x47
    0x0048, 0x0049, 0x004A, 0x004B, 0x004C, 0x004D, 0x004E, 0x004F, // 0x48..0x4F
    0x0050, 0x0051, 0x0052, 0x0053, 0x0054, 0x0055, 0x0056, 0x0057, // 0x50..0x57
    0x0058, 0x0059, 0x005A, 0x00C4, 0x00D6, 0x00D1, 0x00DC, 0x00A7, // 0x58..0x5F
    0x00BF, 0x0061, 0x0062, 0x0063, 0x0064, 0x0065, 0x0066, 0x0067, // 0x60..0x67
    0x0068, 0x0069, 0x006A, 0x006B, 0x006C, 0x006D, 0x006E, 0x006F, // 0x68..0x6F
    0x0070, 0x0071, 0x0072, 0x0073, 0x0074, 0x0075, 0x0076, 0x0077, // 0x70..0x77
    0x0078, 0x0079, 0x007A, 0x00E4, 0x00F6, 0x00F1, 0x00FC, 0x00E0  // 0x78..0x7F
};

uint16_t gsm7ExtToUnicode(uint8_t code) {
    switch (code) {
        case 0x0A: return 0x000C; // Form feed
        case 0x14: return 0x005E; // ^
        case 0x28: return 0x007B; // {
        case 0x29: return 0x007D; // }
        case 0x2F: return 0x005C; // '\'
        case 0x3C: return 0x005B; // [
        case 0x3D: return 0x007E; // ~
        case 0x3E: return 0x005D; // ]
        case 0x40: return 0x007C; // |
        case 0x65: return 0x20AC; // €
        default:   return 0x0020;
    }
}

int unicodeToGsm7(uint32_t cp, bool& isExt) {
    isExt = false;
    if (cp > 0xFFFF) return -1;
    // Check extension characters first
    switch (cp) {
        case 0x000C: isExt = true; return 0x0A;
        case 0x005E: isExt = true; return 0x14;
        case 0x007B: isExt = true; return 0x28;
        case 0x007D: isExt = true; return 0x29;
        case 0x005C: isExt = true; return 0x2F;
        case 0x005B: isExt = true; return 0x3C;
        case 0x007E: isExt = true; return 0x3D;
        case 0x005D: isExt = true; return 0x3E;
        case 0x007C: isExt = true; return 0x40;
        case 0x20AC: isExt = true; return 0x65;
        default: break;
    }
    for (int i = 0; i < 128; ++i) {
        if (i == 0x1B) continue; // Escape
        if (kGsm7Basic[i] == cp) {
            return i;
        }
    }
    return -1;
}

void appendUtf8(std::string& out, uint32_t cp) {
    if (cp <= 0x7F) {
        out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | ((cp >> 6) & 0x1F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | ((cp >> 12) & 0x0F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0x10FFFF) {
        out.push_back(static_cast<char>(0xF0 | ((cp >> 18) & 0x07)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

std::vector<uint32_t> utf8ToCodePoints(const std::string& s) {
    std::vector<uint32_t> cps;
    const size_t n = s.size();
    size_t i = 0;
    while (i < n) {
        uint8_t c = static_cast<uint8_t>(s[i]);
        uint32_t cp = 0xFFFD;
        if (c < 0x80) {
            cp = c;
            i += 1;
        } else if ((c & 0xE0) == 0xC0 && i + 1 < n) {
            cp = ((c & 0x1F) << 6) | (static_cast<uint8_t>(s[i + 1]) & 0x3F);
            i += 2;
        } else if ((c & 0xF0) == 0xE0 && i + 2 < n) {
            cp = ((c & 0x0F) << 12) |
                 ((static_cast<uint8_t>(s[i + 1]) & 0x3F) << 6) |
                 (static_cast<uint8_t>(s[i + 2]) & 0x3F);
            i += 3;
        } else if ((c & 0xF8) == 0xF0 && i + 3 < n) {
            cp = ((c & 0x07) << 18) |
                 ((static_cast<uint8_t>(s[i + 1]) & 0x3F) << 12) |
                 ((static_cast<uint8_t>(s[i + 2]) & 0x3F) << 6) |
                 (static_cast<uint8_t>(s[i + 3]) & 0x3F);
            i += 4;
        } else {
            i += 1;
        }
        cps.push_back(cp);
    }
    return cps;
}

int hexVal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

// Pack 7-bit septets into octets, with optional leading bit offset (e.g. 1 fill bit after 6-byte UDH)
std::vector<uint8_t> packSeptets(const std::vector<uint8_t>& septets, uint8_t fillBits) {
    const size_t totalBits = fillBits + septets.size() * 7;
    const size_t totalBytes = (totalBits + 7) / 8;
    std::vector<uint8_t> out(totalBytes, 0);

    size_t bitPos = fillBits;
    for (uint8_t s : septets) {
        uint8_t v = s & 0x7F;
        for (int b = 0; b < 7; ++b) {
            if ((v >> b) & 1) {
                out[bitPos / 8] |= static_cast<uint8_t>(1U << (bitPos % 8));
            }
            ++bitPos;
        }
    }
    return out;
}

// Unpack `count` 7-bit septets from `bytes` starting at `startByte` and `fillBits` offset
std::vector<uint8_t> unpackSeptets(const std::vector<uint8_t>& bytes,
                                   size_t startByte,
                                   uint8_t fillBits,
                                   size_t count) {
    std::vector<uint8_t> out;
    out.reserve(count);
    size_t bitPos = startByte * 8 + fillBits;
    const size_t maxBits = bytes.size() * 8;
    for (size_t i = 0; i < count; ++i) {
        if (bitPos + 7 > maxBits) break;
        uint8_t val = 0;
        for (int b = 0; b < 7; ++b) {
            size_t cur = bitPos + b;
            if ((bytes[cur / 8] >> (cur % 8)) & 1) {
                val |= static_cast<uint8_t>(1U << b);
            }
        }
        out.push_back(val);
        bitPos += 7;
    }
    return out;
}

std::string decodeBcdNumber(const std::vector<uint8_t>& bytes,
                            size_t offset,
                            size_t numOctets,
                            uint8_t addrType) {
    std::string res;
    if ((addrType & 0x70) == 0x10) {
        res.push_back('+');
    }
    for (size_t i = 0; i < numOctets && (offset + i) < bytes.size(); ++i) {
        uint8_t b = bytes[offset + i];
        uint8_t lo = b & 0x0F;
        uint8_t hi = (b >> 4) & 0x0F;
        auto nibbleChar = [](uint8_t n) -> char {
            if (n <= 9) return static_cast<char>('0' + n);
            if (n == 0x0A) return '*';
            if (n == 0x0B) return '#';
            if (n == 0x0C) return 'a';
            if (n == 0x0D) return 'b';
            if (n == 0x0E) return 'c';
            return '\0';
        };
        char c1 = nibbleChar(lo);
        if (c1) res.push_back(c1);
        char c2 = nibbleChar(hi);
        if (c2) res.push_back(c2);
    }
    return res;
}

std::vector<uint8_t> encodeAddress(const std::string& rawNumber) {
    std::string clean;
    bool isIntl = false;
    for (char c : rawNumber) {
        if (c == '+' && clean.empty() && !isIntl) {
            isIntl = true;
        } else if ((c >= '0' && c <= '9') || c == '*' || c == '#') {
            clean.push_back(c);
        }
    }
    if (!isIntl && clean.size() > 2 && clean[0] == '0' && clean[1] == '0') {
        isIntl = true;
        clean = clean.substr(2);
    }

    auto charNibble = [](char c) -> uint8_t {
        if (c >= '0' && c <= '9') return static_cast<uint8_t>(c - '0');
        if (c == '*') return 0x0A;
        if (c == '#') return 0x0B;
        return 0x0F;
    };

    std::vector<uint8_t> out;
    out.push_back(static_cast<uint8_t>(clean.size()));
    out.push_back(isIntl ? 0x91 : 0x81);
    for (size_t i = 0; i < clean.size(); i += 2) {
        uint8_t lo = charNibble(clean[i]);
        uint8_t hi = (i + 1 < clean.size()) ? charNibble(clean[i + 1]) : 0x0F;
        out.push_back(static_cast<uint8_t>((hi << 4) | lo));
    }
    return out;
}

std::string decodeTimestamp(const std::vector<uint8_t>& bytes, size_t offset) {
    if (offset + 7 > bytes.size()) return "";
    auto bcd = [](uint8_t b) -> int {
        return (b & 0x0F) * 10 + ((b >> 4) & 0x0F);
    };
    int year = 2000 + bcd(bytes[offset + 0]);
    int month = bcd(bytes[offset + 1]);
    int day = bcd(bytes[offset + 2]);
    int hour = bcd(bytes[offset + 3]);
    int minute = bcd(bytes[offset + 4]);
    int second = bcd(bytes[offset + 5]);

    uint8_t tzRaw = bytes[offset + 6];
    bool neg = (tzRaw & 0x08) != 0;
    int tzQuarters = ((tzRaw & 0x07) * 10) + ((tzRaw >> 4) & 0x0F);
    int tzMinutes = tzQuarters * 15;
    int tzH = tzMinutes / 60;
    int tzM = tzMinutes % 60;

    char buf[64];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d%c%02d:%02d",
                  year, month, day, hour, minute, second,
                  neg ? '-' : '+', tzH, tzM);
    return std::string(buf);
}

} // namespace

std::string bytesToHex(const std::vector<uint8_t>& data) {
    static const char kHex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(data.size() * 2);
    for (uint8_t b : data) {
        out.push_back(kHex[(b >> 4) & 0x0F]);
        out.push_back(kHex[b & 0x0F]);
    }
    return out;
}

bool hexToBytes(const std::string& hex, std::vector<uint8_t>& out) {
    out.clear();
    if (hex.size() % 2 != 0) return false;
    out.reserve(hex.size() / 2);
    for (size_t i = 0; i < hex.size(); i += 2) {
        int hi = hexVal(hex[i]);
        int lo = hexVal(hex[i + 1]);
        if (hi < 0 || lo < 0) {
            out.clear();
            return false;
        }
        out.push_back(static_cast<uint8_t>((hi << 4) | lo));
    }
    return true;
}

bool utf8ToGsm7Septets(const std::string& utf8, std::vector<uint8_t>& septets) {
    septets.clear();
    std::vector<uint32_t> cps = utf8ToCodePoints(utf8);
    septets.reserve(cps.size());
    for (uint32_t cp : cps) {
        bool isExt = false;
        int code = unicodeToGsm7(cp, isExt);
        if (code < 0) {
            septets.clear();
            return false;
        }
        if (isExt) {
            septets.push_back(0x1B);
        }
        septets.push_back(static_cast<uint8_t>(code));
    }
    return true;
}

std::string gsm7SeptetsToUtf8(const std::vector<uint8_t>& septets) {
    std::string out;
    out.reserve(septets.size());
    bool escape = false;
    for (uint8_t s : septets) {
        uint8_t c = s & 0x7F;
        if (escape) {
            appendUtf8(out, gsm7ExtToUnicode(c));
            escape = false;
        } else if (c == 0x1B) {
            escape = true;
        } else {
            appendUtf8(out, kGsm7Basic[c]);
        }
    }
    return out;
}

std::vector<uint16_t> utf8ToUtf16(const std::string& utf8) {
    std::vector<uint16_t> out;
    for (uint32_t cp : utf8ToCodePoints(utf8)) {
        if (cp <= 0xFFFF) {
            out.push_back(static_cast<uint16_t>(cp));
        } else if (cp <= 0x10FFFF) {
            uint32_t v = cp - 0x10000;
            out.push_back(static_cast<uint16_t>(0xD800 + (v >> 10)));
            out.push_back(static_cast<uint16_t>(0xDC00 + (v & 0x3FF)));
        }
    }
    return out;
}

std::string utf16BeBytesToUtf8(const std::vector<uint8_t>& bytes) {
    std::string out;
    for (size_t i = 0; i + 1 < bytes.size(); i += 2) {
        uint16_t u1 = static_cast<uint16_t>((bytes[i] << 8) | bytes[i + 1]);
        if (u1 >= 0xD800 && u1 <= 0xDBFF && i + 3 < bytes.size()) {
            uint16_t u2 = static_cast<uint16_t>((bytes[i + 2] << 8) | bytes[i + 3]);
            if (u2 >= 0xDC00 && u2 <= 0xDFFF) {
                uint32_t cp = 0x10000 + (((static_cast<uint32_t>(u1) - 0xD800) << 10) |
                                         (static_cast<uint32_t>(u2) - 0xDC00));
                appendUtf8(out, cp);
                i += 2;
                continue;
            }
        }
        appendUtf8(out, u1);
    }
    return out;
}

std::vector<EncodedPdu> encodeSubmitPdus(const std::string& recipient,
                                         const std::string& utf8Text,
                                         uint8_t msgRef,
                                         uint8_t concatRef) {
    std::vector<EncodedPdu> result;
    std::vector<uint8_t> da = encodeAddress(recipient);

    std::vector<uint8_t> septets;
    const bool useGsm7 = utf8ToGsm7Septets(utf8Text, septets);

    if (useGsm7) {
        if (septets.size() <= 160) {
            std::vector<uint8_t> tpdu;
            tpdu.push_back(0x11); // SMS-SUBMIT, VPF=relative, UDHI=0
            tpdu.push_back(msgRef);
            tpdu.insert(tpdu.end(), da.begin(), da.end());
            tpdu.push_back(0x00); // PID
            tpdu.push_back(0x00); // DCS: GSM-7
            tpdu.push_back(0xAA); // VP: 4 days
            tpdu.push_back(static_cast<uint8_t>(septets.size()));
            std::vector<uint8_t> packed = packSeptets(septets, 0);
            tpdu.insert(tpdu.end(), packed.begin(), packed.end());

            EncodedPdu p;
            p.tpduLength = static_cast<int>(tpdu.size());
            p.hexPdu = "00" + bytesToHex(tpdu);
            p.isUcs2 = false;
            p.partIndex = 1;
            p.totalParts = 1;
            result.push_back(p);
            return result;
        }

        // Split into <= 153 septet chunks without breaking 0x1B escape pairs
        std::vector<std::vector<uint8_t>> chunks;
        size_t pos = 0;
        while (pos < septets.size()) {
            size_t len = std::min<size_t>(153, septets.size() - pos);
            if (len == 153 && septets[pos + len - 1] == 0x1B) {
                len -= 1; // Do not split escape byte from its payload
            }
            chunks.emplace_back(septets.begin() + pos, septets.begin() + pos + len);
            pos += len;
        }

        const int total = static_cast<int>(chunks.size());
        for (int i = 0; i < total; ++i) {
            std::vector<uint8_t> tpdu;
            tpdu.push_back(0x51); // SMS-SUBMIT, VPF=relative, UDHI=1 (0x40 | 0x11)
            tpdu.push_back(static_cast<uint8_t>(msgRef + i));
            tpdu.insert(tpdu.end(), da.begin(), da.end());
            tpdu.push_back(0x00); // PID
            tpdu.push_back(0x00); // DCS: GSM-7
            tpdu.push_back(0xAA); // VP: 4 days

            // 6-byte UDH = 48 bits -> 7 septets = 49 bits -> 1 fill bit
            const size_t udl = 7 + chunks[i].size();
            tpdu.push_back(static_cast<uint8_t>(udl));
            tpdu.push_back(0x05); // UDHL
            tpdu.push_back(0x00); // IEI: 8-bit concat ref
            tpdu.push_back(0x03); // IEDL
            tpdu.push_back(concatRef);
            tpdu.push_back(static_cast<uint8_t>(total));
            tpdu.push_back(static_cast<uint8_t>(i + 1));

            std::vector<uint8_t> packed = packSeptets(chunks[i], 1);
            tpdu.insert(tpdu.end(), packed.begin(), packed.end());

            EncodedPdu p;
            p.tpduLength = static_cast<int>(tpdu.size());
            p.hexPdu = "00" + bytesToHex(tpdu);
            p.isUcs2 = false;
            p.partIndex = i + 1;
            p.totalParts = total;
            result.push_back(p);
        }
        return result;
    }

    // UCS-2 (UTF-16BE)
    std::vector<uint16_t> u16 = utf8ToUtf16(utf8Text);
    if (u16.size() <= 70) {
        std::vector<uint8_t> tpdu;
        tpdu.push_back(0x11);
        tpdu.push_back(msgRef);
        tpdu.insert(tpdu.end(), da.begin(), da.end());
        tpdu.push_back(0x00); // PID
        tpdu.push_back(0x08); // DCS: UCS-2
        tpdu.push_back(0xAA);
        tpdu.push_back(static_cast<uint8_t>(u16.size() * 2));
        for (uint16_t cu : u16) {
            tpdu.push_back(static_cast<uint8_t>((cu >> 8) & 0xFF));
            tpdu.push_back(static_cast<uint8_t>(cu & 0xFF));
        }

        EncodedPdu p;
        p.tpduLength = static_cast<int>(tpdu.size());
        p.hexPdu = "00" + bytesToHex(tpdu);
        p.isUcs2 = true;
        p.partIndex = 1;
        p.totalParts = 1;
        result.push_back(p);
        return result;
    }

    // Multipart UCS-2: <= 67 UTF-16 code units per part, preserving surrogate pairs
    std::vector<std::vector<uint16_t>> chunks;
    size_t pos = 0;
    while (pos < u16.size()) {
        size_t len = std::min<size_t>(67, u16.size() - pos);
        if (len == 67 && u16[pos + len - 1] >= 0xD800 && u16[pos + len - 1] <= 0xDBFF) {
            len -= 1; // Keep surrogate pair intact
        }
        chunks.emplace_back(u16.begin() + pos, u16.begin() + pos + len);
        pos += len;
    }

    const int total = static_cast<int>(chunks.size());
    for (int i = 0; i < total; ++i) {
        std::vector<uint8_t> tpdu;
        tpdu.push_back(0x51); // UDHI=1
        tpdu.push_back(static_cast<uint8_t>(msgRef + i));
        tpdu.insert(tpdu.end(), da.begin(), da.end());
        tpdu.push_back(0x00); // PID
        tpdu.push_back(0x08); // DCS: UCS-2
        tpdu.push_back(0xAA);
        tpdu.push_back(static_cast<uint8_t>(6 + chunks[i].size() * 2));
        tpdu.push_back(0x05);
        tpdu.push_back(0x00);
        tpdu.push_back(0x03);
        tpdu.push_back(concatRef);
        tpdu.push_back(static_cast<uint8_t>(total));
        tpdu.push_back(static_cast<uint8_t>(i + 1));
        for (uint16_t cu : chunks[i]) {
            tpdu.push_back(static_cast<uint8_t>((cu >> 8) & 0xFF));
            tpdu.push_back(static_cast<uint8_t>(cu & 0xFF));
        }

        EncodedPdu p;
        p.tpduLength = static_cast<int>(tpdu.size());
        p.hexPdu = "00" + bytesToHex(tpdu);
        p.isUcs2 = true;
        p.partIndex = i + 1;
        p.totalParts = total;
        result.push_back(p);
    }
    return result;
}

DecodedSms decodeDeliverPdu(const std::string& hexPdu) {
    DecodedSms sms;
    std::vector<uint8_t> bytes;
    if (!hexToBytes(hexPdu, bytes) || bytes.size() < 4) {
        return sms;
    }

    size_t idx = 0;
    // 1. SMSC header
    uint8_t smscLen = bytes[idx++];
    if (idx + smscLen >= bytes.size()) return sms;
    if (smscLen > 0) {
        uint8_t smscType = bytes[idx];
        sms.smsc = decodeBcdNumber(bytes, idx + 1, smscLen - 1, smscType);
        idx += smscLen;
    }

    if (idx >= bytes.size()) return sms;
    uint8_t firstOctet = bytes[idx++];
    uint8_t mti = firstOctet & 0x03;
    bool udhi = (firstOctet & 0x40) != 0;

    if (mti != 0x00 && mti != 0x01) {
        return sms; // Only SMS-DELIVER (0) and SMS-SUBMIT (1)
    }
    if (mti == 0x01) {
        // Skip TP-MR for SMS-SUBMIT
        if (idx >= bytes.size()) return sms;
        idx++;
    }

    // 2. Originating/Destination Address
    if (idx + 2 > bytes.size()) return sms;
    uint8_t addrLenNibbles = bytes[idx++];
    uint8_t addrType = bytes[idx++];
    size_t addrBytes = (addrLenNibbles + 1) / 2;
    if (idx + addrBytes > bytes.size()) return sms;

    if ((addrType & 0x70) == 0x50) {
        // Alphanumeric sender ID (GSM-7 packed)
        size_t charCount = (static_cast<size_t>(addrLenNibbles) * 4) / 7;
        std::vector<uint8_t> sep = unpackSeptets(bytes, idx, 0, charCount);
        sms.sender = gsm7SeptetsToUtf8(sep);
    } else {
        sms.sender = decodeBcdNumber(bytes, idx, addrBytes, addrType);
    }
    idx += addrBytes;

    // 3. PID & DCS
    if (idx + 2 > bytes.size()) return sms;
    idx++; // skip TP-PID
    uint8_t dcs = bytes[idx++];

    // 4. Timestamp (SMS-DELIVER) or Validity Period (SMS-SUBMIT)
    if (mti == 0x00) {
        if (idx + 7 > bytes.size()) return sms;
        sms.timestamp = decodeTimestamp(bytes, idx);
        idx += 7;
    } else {
        uint8_t vpf = (firstOctet >> 3) & 0x03;
        size_t vpBytes = (vpf == 2) ? 1 : ((vpf == 1 || vpf == 3) ? 7 : 0);
        if (idx + vpBytes > bytes.size()) return sms;
        idx += vpBytes;
    }

    if (idx >= bytes.size()) return sms;
    uint8_t udl = bytes[idx++];

    // Determine encoding from DCS (3GPP TS 23.038)
    enum class Alphabet { Gsm7, EightBit, Ucs2 };
    Alphabet alpha = Alphabet::Gsm7;
    if ((dcs & 0xC0) == 0x00) {
        uint8_t bits = (dcs >> 2) & 0x03;
        if (bits == 0x01) alpha = Alphabet::EightBit;
        else if (bits == 0x02) alpha = Alphabet::Ucs2;
    } else if ((dcs & 0xF0) == 0xF0) {
        alpha = (dcs & 0x04) ? Alphabet::EightBit : Alphabet::Gsm7;
    } else if ((dcs & 0xF0) == 0xE0) {
        alpha = Alphabet::Ucs2;
    }
    sms.isUcs2 = (alpha == Alphabet::Ucs2);

    // 5. Optional UDH
    size_t udhBytes = 0;
    if (udhi && idx < bytes.size()) {
        uint8_t udhl = bytes[idx];
        udhBytes = 1 + udhl;
        if (idx + udhBytes <= bytes.size()) {
            size_t p = idx + 1;
            size_t endUdh = idx + udhBytes;
            while (p + 1 < endUdh) {
                uint8_t iei = bytes[p];
                uint8_t iedl = bytes[p + 1];
                if (p + 2 + iedl > endUdh) break;
                if (iei == 0x00 && iedl == 3) {
                    sms.isMultipart = true;
                    sms.refId = bytes[p + 2];
                    sms.totalParts = bytes[p + 3];
                    sms.partNumber = bytes[p + 4];
                } else if (iei == 0x08 && iedl == 4) {
                    sms.isMultipart = true;
                    sms.refId = static_cast<uint16_t>((bytes[p + 2] << 8) | bytes[p + 3]);
                    sms.totalParts = bytes[p + 4];
                    sms.partNumber = bytes[p + 5];
                }
                p += 2 + iedl;
            }
        }
    }

    // 6. Decode User Data
    if (alpha == Alphabet::Gsm7) {
        size_t udhSeptets = (udhBytes * 8 + 6) / 7;
        uint8_t fillBits = static_cast<uint8_t>(udhSeptets * 7 - udhBytes * 8);
        size_t textSeptets = (udl > udhSeptets) ? (udl - udhSeptets) : 0;
        std::vector<uint8_t> sep = unpackSeptets(bytes, idx + udhBytes, fillBits, textSeptets);
        sms.text = gsm7SeptetsToUtf8(sep);
    } else if (alpha == Alphabet::Ucs2) {
        size_t start = idx + udhBytes;
        size_t avail = (idx + udl <= bytes.size()) ? (idx + udl) : bytes.size();
        if (start < avail) {
            std::vector<uint8_t> u16Bytes(bytes.begin() + start, bytes.begin() + avail);
            sms.text = utf16BeBytesToUtf8(u16Bytes);
        }
    } else {
        size_t start = idx + udhBytes;
        size_t avail = (idx + udl <= bytes.size()) ? (idx + udl) : bytes.size();
        for (size_t i = start; i < avail; ++i) {
            sms.text.push_back(static_cast<char>(bytes[i]));
        }
    }

    sms.valid = true;
    return sms;
}

} // namespace SmsCodec
