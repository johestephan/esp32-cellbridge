#include <unity.h>
#include "mqtt_sms_parser.h"

#include "../../src/mqtt_sms_parser.cpp"

void setUp(void) {}
void tearDown(void) {}

void test_inline_number_colon_message(void) {
    auto r1 = parseMqttPlainSmsPayload("+31612345678: Server room temp high!", "+31600000000");
    TEST_ASSERT_TRUE(r1.valid);
    TEST_ASSERT_EQUAL_STRING("+31612345678", r1.to.c_str());
    TEST_ASSERT_EQUAL_STRING("Server room temp high!", r1.message.c_str());

    auto r2 = parseMqttPlainSmsPayload("06 1234 5678: Hello with spaces", "+31600000000");
    TEST_ASSERT_TRUE(r2.valid);
    TEST_ASSERT_EQUAL_STRING("0612345678", r2.to.c_str());
    TEST_ASSERT_EQUAL_STRING("Hello with spaces", r2.message.c_str());

    auto r3 = parseMqttPlainSmsPayload("1144: SALDO", "");
    TEST_ASSERT_TRUE(r3.valid);
    TEST_ASSERT_EQUAL_STRING("1144", r3.to.c_str());
    TEST_ASSERT_EQUAL_STRING("SALDO", r3.message.c_str());
}

void test_plain_message_uses_fallback_even_with_colons(void) {
    auto r1 = parseMqttPlainSmsPayload("Front door opened at 12:30", "+31699998888");
    TEST_ASSERT_TRUE(r1.valid);
    TEST_ASSERT_EQUAL_STRING("+31699998888", r1.to.c_str());
    TEST_ASSERT_EQUAL_STRING("Front door opened at 12:30", r1.message.c_str());

    // Two-digit time prefix like "12:30 alert" should NOT be mistaken for a phone number
    auto r2 = parseMqttPlainSmsPayload("12:30 alert!", "+31699998888");
    TEST_ASSERT_TRUE(r2.valid);
    TEST_ASSERT_EQUAL_STRING("+31699998888", r2.to.c_str());
    TEST_ASSERT_EQUAL_STRING("12:30 alert!", r2.message.c_str());
}

void test_missing_target_or_empty_message_is_invalid(void) {
    auto r1 = parseMqttPlainSmsPayload("Just a message", "");
    TEST_ASSERT_FALSE(r1.valid);

    auto r2 = parseMqttPlainSmsPayload("+31612345678:", "+31600000000");
    // Prefix with empty message after ':' should fall back safely or reject cleanly; here nothing after ':' so treated as plain text
    TEST_ASSERT_TRUE(r2.valid);
    TEST_ASSERT_EQUAL_STRING("+31600000000", r2.to.c_str());
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_inline_number_colon_message);
    RUN_TEST(test_plain_message_uses_fallback_even_with_colons);
    RUN_TEST(test_missing_target_or_empty_message_is_invalid);
    return UNITY_END();
}
