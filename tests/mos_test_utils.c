#include "unity.h"

#include "../include/mos_utils.h"

void setUp(void) {}

void tearDown(void) {}

void test_next_pow_of_2(void) {
    TEST_ASSERT_EQUAL(1, mos_utils_next_pow_of_2(0));
    TEST_ASSERT_EQUAL(2, mos_utils_next_pow_of_2(2));
    TEST_ASSERT_EQUAL(4, mos_utils_next_pow_of_2(3));
    TEST_ASSERT_EQUAL(4, mos_utils_next_pow_of_2(4));
    TEST_ASSERT_EQUAL(8, mos_utils_next_pow_of_2(5));
    TEST_ASSERT_EQUAL(8, mos_utils_next_pow_of_2(8));
    TEST_ASSERT_EQUAL(16, mos_utils_next_pow_of_2(12));
    TEST_ASSERT_EQUAL(16, mos_utils_next_pow_of_2(16));
    TEST_ASSERT_EQUAL(32, mos_utils_next_pow_of_2(20));
    TEST_ASSERT_EQUAL(32, mos_utils_next_pow_of_2(32));
    TEST_ASSERT_EQUAL(64, mos_utils_next_pow_of_2(33));
    TEST_ASSERT_EQUAL(64, mos_utils_next_pow_of_2(64));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_next_pow_of_2);
    return UNITY_END();
}