#include <unity.h>
#include "balance_parser.h"

#include "../../src/balance_parser.cpp"

void setUp(void) {}
void tearDown(void) {}

void test_odido_exact_reply(void) {
    const std::string msg =
        "Je huidige Prepaid tegoed is EUR   4.25 Bekijk je verbruikstatus en geldigheid via je mobiel op odido.nl/verbruik of ga naar Mijn Odido. Tot gauw, Odido";
    auto b = parseBalanceText(msg);
    TEST_ASSERT_TRUE(b.valid);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 4.25f, static_cast<float>(b.amount));
    TEST_ASSERT_EQUAL_STRING("EUR", b.currency.c_str());
    TEST_ASSERT_EQUAL_STRING("€ 4.25", b.formatted.c_str());
    TEST_ASSERT_TRUE(isBalanceSenderMatch("1144", "1144", msg));
    TEST_ASSERT_TRUE(isBalanceSenderMatch("Odido", "1144", msg));
}

void test_euro_comma_and_suffix_variants(void) {
    auto b1 = parseBalanceText("Uw beltegoed bedraagt € 12,50.");
    TEST_ASSERT_TRUE(b1.valid);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 12.50f, static_cast<float>(b1.amount));
    TEST_ASSERT_EQUAL_STRING("€ 12.50", b1.formatted.c_str());

    auto b2 = parseBalanceText("Saldo: 9,95 EUR geldig tot 01-01-2027");
    TEST_ASSERT_TRUE(b2.valid);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 9.95f, static_cast<float>(b2.amount));
    TEST_ASSERT_EQUAL_STRING("€ 9.95", b2.formatted.c_str());
}

void test_fallback_plain_decimal_and_non_balance(void) {
    auto b1 = parseBalanceText("Current balance is 7.50");
    TEST_ASSERT_TRUE(b1.valid);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 7.50f, static_cast<float>(b1.amount));
    TEST_ASSERT_EQUAL_STRING("7.50", b1.formatted.c_str());

    auto b2 = parseBalanceText("Hallo! Hoe gaat het?");
    TEST_ASSERT_FALSE(b2.valid);
    TEST_ASSERT_FALSE(isBalanceSenderMatch("+31612345678", "1144", "Hallo!"));
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_odido_exact_reply);
    RUN_TEST(test_euro_comma_and_suffix_variants);
    RUN_TEST(test_fallback_plain_decimal_and_non_balance);
    return UNITY_END();
}
