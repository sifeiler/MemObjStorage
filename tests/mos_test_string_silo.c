#include "unity.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <inttypes.h>

#include "../include/mos_string.h"
#include "../include/mos_utils.h"
#include "../include/mos_internal.h"
#include "../include/mos_math.h"

#define MAX_STRING_LEN 50

void setUp(void) {
    srand((unsigned int)time(NULL));
}

void tearDown(void) {

}

int random_string(char *out, int max_len) {
    static const char charset[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789";
    const int charset_size = (int)(sizeof(charset) - 1); // -1 for null terminator

    int len = rand() % (max_len + 1); // random length in [0, max_len]

    for (int i = 0; i < len; i++) {
        out[i] = charset[rand() % charset_size];
    }
    out[len] = '\0';

    return len;
}

void test_mos_string_silo_put(int count) {
    uint64_t silo_size = MOS_ALIGN_UP(count * MAX_STRING_LEN, MOS_PAGE_SIZE);

    void* silo_pointer = calloc(1, silo_size);
    mos_t_string_desc* string_descriptors = calloc(count, sizeof(mos_t_string_desc));
    char (*written_strings)[MAX_STRING_LEN] = calloc(count, MAX_STRING_LEN);

    mos_t_string_silo string_silo = {
        .base_offset = 0,
        .size = silo_size,
        .last_deleted.str_offset = MOS_NULL_OFFSET,
        .last_deleted.str_len = 0,
        .current_offset = 0
    };

    //write count strings to silo
    for(int i = 0; i < count; i++) {
        mos_t_string_desc result = {0};

        char string[MAX_STRING_LEN];
        int str_len = random_string(written_strings[i], MAX_STRING_LEN - 1);
        mos_t_string mos_string = {
            .str = written_strings[i],
            .str_len = str_len
        };
        mos_string_put(silo_pointer, &string_silo, &mos_string, &result);
        string_descriptors[i] = result;
    }

    //read count strings from silo
    for(int i = 0; i < count; i++) {
        char* result;
        mos_t_string_desc* string_desc = string_descriptors + i;
        mos_string_get(silo_pointer, &string_silo, string_desc, &result);
        TEST_ASSERT_EQUAL_STRING_LEN(written_strings[i], result, string_desc->str_len);
    }
    
    free(written_strings);
    free(string_descriptors);
    free(silo_pointer);
}

void test_mos_string_silo_put_100(void) {
    test_mos_string_silo_put(100);
}

void test_mos_string_silo_put_1000(void) {
    test_mos_string_silo_put(1000);
}

void test_mos_string_silo_put_5000(void) {
    test_mos_string_silo_put(5000);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_mos_string_silo_put_100);
    RUN_TEST(test_mos_string_silo_put_1000);
    RUN_TEST(test_mos_string_silo_put_5000);
    return UNITY_END();
}