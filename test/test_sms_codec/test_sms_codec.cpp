#include <unity.h>
#include "sms_codec.h"

// Include implementation directly for PlatformIO native test discovery
#include "../../src/sms_codec.cpp"

using namespace SmsCodec;

void setUp(void) {}
void tearDown(void) {}

void test_gsm7_basic_and_euro(void) {
    std::string msg = "Hello Odido! Price: €12.50 {ok} [1]^~";
    std::vector<uint8_t> septets;
    TEST_ASSERT_TRUE(utf8ToGsm7Septets(msg, septets));
    TEST_ASSERT_EQUAL_STRING(msg.c_str(), gsm7SeptetsToUtf8(septets).c_str());
}

void test_encode_decode_single_gsm7_roundtrip(void) {
    std::string msg = "Test SMS from A7670E €5!";
    auto pdus = encodeSubmitPdus("+31612345678", msg, 0x01, 0x10);
    TEST_ASSERT_EQUAL(1, pdus.size());
    TEST_ASSERT_FALSE(pdus[0].isUcs2);

    auto decoded = decodeDeliverPdu(pdus[0].hexPdu);
    TEST_ASSERT_TRUE(decoded.valid);
    TEST_ASSERT_EQUAL_STRING("+31612345678", decoded.sender.c_str());
    TEST_ASSERT_EQUAL_STRING(msg.c_str(), decoded.text.c_str());
}

void test_encode_decode_single_ucs2_emoji(void) {
    std::string msg = "Hoi! Groetjes uit Nederland 🇳🇱🚀 €9,99";
    auto pdus = encodeSubmitPdus("+31698765432", msg, 0x02, 0x20);
    TEST_ASSERT_EQUAL(1, pdus.size());
    TEST_ASSERT_TRUE(pdus[0].isUcs2);

    auto decoded = decodeDeliverPdu(pdus[0].hexPdu);
    TEST_ASSERT_TRUE(decoded.valid);
    TEST_ASSERT_EQUAL_STRING("+31698765432", decoded.sender.c_str());
    TEST_ASSERT_EQUAL_STRING(msg.c_str(), decoded.text.c_str());
}

void test_multipart_gsm7_roundtrip(void) {
    // 200 chars -> 2 parts in GSM-7 (153 + 47)
    std::string msg(200, 'A');
    msg[152] = 'Z';
    msg[153] = 'B';
    auto pdus = encodeSubmitPdus("+31611112222", msg, 0x00, 0x42);
    TEST_ASSERT_EQUAL(2, pdus.size());
    TEST_ASSERT_FALSE(pdus[0].isUcs2);

    std::string combined;
    for (size_t i = 0; i < pdus.size(); ++i) {
        auto d = decodeDeliverPdu(pdus[i].hexPdu);
        TEST_ASSERT_TRUE(d.valid);
        TEST_ASSERT_TRUE(d.isMultipart);
        TEST_ASSERT_EQUAL(0x42, d.refId);
        TEST_ASSERT_EQUAL(2, d.totalParts);
        TEST_ASSERT_EQUAL(i + 1, d.partNumber);
        combined += d.text;
    }
    TEST_ASSERT_EQUAL_STRING(msg.c_str(), combined.c_str());
}

void test_multipart_ucs2_roundtrip(void) {
    // 90 emojis -> 2 parts in UCS-2 (each emoji = 2 UTF-16 code units, <= 67 units/part -> 33 emojis/part)
    std::string msg;
    for (int i = 0; i < 40; ++i) {
        msg += "🚀";
    }
    auto pdus = encodeSubmitPdus("0612345678", msg, 0x00, 0x77);
    TEST_ASSERT_EQUAL(2, pdus.size());
    TEST_ASSERT_TRUE(pdus[0].isUcs2);

    std::string combined;
    for (size_t i = 0; i < pdus.size(); ++i) {
        auto d = decodeDeliverPdu(pdus[i].hexPdu);
        TEST_ASSERT_TRUE(d.valid);
        TEST_ASSERT_TRUE(d.isMultipart);
        TEST_ASSERT_EQUAL(2, d.totalParts);
        TEST_ASSERT_EQUAL(i + 1, d.partNumber);
        combined += d.text;
    }
    TEST_ASSERT_EQUAL_STRING(msg.c_str(), combined.c_str());
}

void test_decode_real_deliver_alphanumeric_sender(void) {
    // Real-world SMS-DELIVER PDU:
    // SMSC: 07 91 1366080000F0 (+31668000000)
    // First octet: 04 (SMS-DELIVER)
    // Sender: 09 D0 4F729AFC06 ("Odido", 5 GSM-7 chars -> 9 semi-octets, type 0xD0)
    // PID: 00, DCS: 00 (GSM-7)
    // SCTS: 62114171500040 (2026-11-14T17:05:00+01:00)
    // UDL: 05 ("hello" = E8 32 9B FD 06)
    const char* pdu = "07911366080000F00409D04F729AFC0600006211417150004005E8329BFD06";
    auto d = decodeDeliverPdu(pdu);
    TEST_ASSERT_TRUE(d.valid);
    TEST_ASSERT_EQUAL_STRING("Odido", d.sender.c_str());
    TEST_ASSERT_EQUAL_STRING("2026-11-14T17:05:00+01:00", d.timestamp.c_str());
    TEST_ASSERT_EQUAL_STRING("hello", d.text.c_str());
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_gsm7_basic_and_euro);
    RUN_TEST(test_encode_decode_single_gsm7_roundtrip);
    RUN_TEST(test_encode_decode_single_ucs2_emoji);
    RUN_TEST(test_multipart_gsm7_roundtrip);
    RUN_TEST(test_multipart_ucs2_roundtrip);
    RUN_TEST(test_decode_real_deliver_alphanumeric_sender);
    return UNITY_END();
}
