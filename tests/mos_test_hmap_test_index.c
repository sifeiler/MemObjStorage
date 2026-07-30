#include "unity.h"
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <inttypes.h> // Required for PRIu64
#include <string.h>

#include "../include/mos.h"
#include "../include/mos_internal.h"
#include "../include/mos_idx_hmap.h"
#include "../include/mos_idx.h"

void setUp(void) {}

void tearDown(void) {}

void test_mos_idx_hmap_init__even_item_count(void) {
    //Arrange
    uint8_t test_buffer[MOS_PAGE_SIZE];
    memset(test_buffer, 0, MOS_PAGE_SIZE);
    mos_t_idx_data* index_data = (mos_t_idx_data*)test_buffer;
    strncpy(index_data->index.name, "test_index", sizeof(index_data->index.name) - 1);
    index_data->index.offset_file = 8192;
    index_data->index.type = MOS_IDX_HASH_MAP;
    index_data->index.index_size = 0;

    // value will be aligned up to page size and in this case the index fits into a single page
    uint64_t expected_index_size = MOS_PAGE_SIZE;
    //20 * 2 = 40 -> 64 (next power of 2)
    uint64_t expected_table_size = 64;
    uint64_t expected_offset_values = 8192 + offsetof(mos_t_idx_data, index_payload) + sizeof(mos_t_idx_hmap_header);
    uint64_t expected_offset_verifiers = expected_offset_values + 2036;

    //Act
    mos_idx_hmap_init(20, &index_data->index, index_data);

    //Assert
    mos_t_idx_hmap* idx_hash_map = (mos_t_idx_hmap*)index_data->index_payload;
    TEST_ASSERT_EQUAL(expected_index_size, index_data->index.index_size);
    TEST_ASSERT_EQUAL(expected_table_size, idx_hash_map->index_header.table_size);
    TEST_ASSERT_EQUAL(expected_offset_values, idx_hash_map->index_header.offset_values);
    TEST_ASSERT_EQUAL(expected_offset_verifiers, idx_hash_map->index_header.offset_verifiers);
}

void test_mos_idx_hmap_init__odd_item_count(void) {
    //Arrange
    uint8_t test_buffer[MOS_PAGE_SIZE];
    memset(test_buffer, 0, MOS_PAGE_SIZE);
    mos_t_idx_data* index_data = (mos_t_idx_data*)test_buffer;
    strncpy(index_data->index.name, "test_index", sizeof(index_data->index.name) - 1);
    index_data->index.offset_file = 4096;
    index_data->index.type = MOS_IDX_HASH_MAP;
    index_data->index.index_size = 0;

    mos_t_idx_hmap* hash_map_index = (mos_t_idx_hmap*)index_data->index_payload;
    hash_map_index->index_header.table_size = 8;

    // value will be aligned up to page size and in this case the index fits into a single page
    uint64_t expected_index_size = MOS_PAGE_SIZE;
    //11 * 2 = 22 -> 32 (next power of 2)
    uint64_t expected_table_size = 32;
    uint64_t expected_offset_values = 4096 + offsetof(mos_t_idx_data, index_payload) + sizeof(mos_t_idx_hmap_header);
    //2036 due to 4096 Byte page aligning
    uint64_t expected_offset_verifiers = expected_offset_values + 2036;

    //Act
    mos_idx_hmap_init(11, &index_data->index, index_data);

    //Assert
    mos_t_idx_hmap* idx_hash_map = (mos_t_idx_hmap*)index_data->index_payload;
    TEST_ASSERT_EQUAL(expected_index_size, index_data->index.index_size);
    TEST_ASSERT_EQUAL(expected_table_size, idx_hash_map->index_header.table_size);
    TEST_ASSERT_EQUAL(expected_offset_values, idx_hash_map->index_header.offset_values);
    TEST_ASSERT_EQUAL(expected_offset_verifiers, idx_hash_map->index_header.offset_verifiers);
}

void test_mos_idx_hmap_size(void) {
    //Arrange
    uint64_t item_count = 100;

    //Act
    uint64_t actual_index_size = mos_idx_hmap_size(item_count);

    //Assert
    TEST_ASSERT_EQUAL(8192, actual_index_size);
}

void test_mos_idx_hmap_put__first_slot_available(void) {
    //Arrange
    uint8_t test_buffer[MOS_PAGE_SIZE];
    memset(test_buffer, 0, MOS_PAGE_SIZE);
    mos_t_idx_data* index_data = (mos_t_idx_data*)test_buffer;
    strncpy(index_data->index.name, "test_index", sizeof(index_data->index.name) - 1);
    index_data->index.offset_file = 8192;
    index_data->index.type = MOS_IDX_HASH_MAP;
    index_data->index.index_size = 0;

    mos_t_idx_hmap* hash_map_index = (mos_t_idx_hmap*)index_data->index_payload;
    hash_map_index->index_header.table_size = 8;

    uint8_t key1 = 1;
    uint8_t key2 = 2;
    uint64_t val1 = 5;
    uint64_t val2 = 6;
    __uint128_t hash1 = mos_idx_murmur_hash_3_128(&key1, MOS_IDX_MURMUR3_SEED, 1);
    __uint128_t hash2 = mos_idx_murmur_hash_3_128(&key2, MOS_IDX_MURMUR3_SEED, 1);
    __uint128_t i1 = hash1 & 7;
    __uint128_t i2 = hash2 & 7;
    __uint128_t verifier1 = (hash1 >> 64);
    __uint128_t verifier2 = (hash2 >> 64);

    //Act
    // 1 will be hashed to binary ...000 = 0
    int64_t result1 = mos_idx_hmap_put(index_data, &key1, 1, val1);
    // 1 will be hashed to binary ...011 = 3
    int64_t result2 = mos_idx_hmap_put(index_data, &key2, 1, val2);

    //Assert
    uint64_t* values = hash_map_index->data;
    uint64_t* verifiers = hash_map_index->data + hash_map_index->index_header.table_size;
    TEST_ASSERT_NOT_EQUAL(-1, result1);
    TEST_ASSERT_NOT_EQUAL(-1, result2);
    TEST_ASSERT_EQUAL_INT64(val1, values[i1]);
    TEST_ASSERT_EQUAL_INT64(val2, values[i2]);
    TEST_ASSERT_EQUAL_INT64(verifier1, verifiers[i1]);
    TEST_ASSERT_EQUAL_INT64(verifier2, verifiers[i2]);
}

void test_mos_idx_hmap_put__first_slot_occupied(void) {
    //Arrange
    uint8_t test_buffer[MOS_PAGE_SIZE];
    memset(test_buffer, 0, MOS_PAGE_SIZE);
    mos_t_idx_data* index_data = (mos_t_idx_data*)test_buffer;
    strncpy(index_data->index.name, "test_index", sizeof(index_data->index.name) - 1);
    index_data->index.offset_file = 8192;
    index_data->index.type = MOS_IDX_HASH_MAP;
    index_data->index.index_size = 0;

    mos_t_idx_hmap* hash_map_index = (mos_t_idx_hmap*)index_data->index_payload;
    hash_map_index->index_header.table_size = 8;
    uint64_t* index_values = hash_map_index->data;
    uint64_t* index_verifiers = hash_map_index->data + hash_map_index->index_header.table_size;
    index_values[0] = 10;
    index_verifiers[0] = 7;

    uint8_t key1 = 1;
    __uint128_t hash1 = mos_idx_murmur_hash_3_128(&key1, MOS_IDX_MURMUR3_SEED, 1);
    __uint128_t i1 = hash1 & 7;
    __uint128_t verifier1 = (hash1 >> 64);

    //Act
    // 1 will be hashed to binary ...000 = 0
    mos_idx_hmap_put(index_data, &key1, 1, 5);

    //Assert
    TEST_ASSERT_EQUAL(index_values[0], 10);
    TEST_ASSERT_EQUAL(index_values[i1], 5);
    TEST_ASSERT_EQUAL(index_verifiers[0], 7);
    TEST_ASSERT_EQUAL(index_verifiers[i1], verifier1);
}

void test_mos_idx_hmap_put__table_full(void) {
    //Arrange
    uint8_t test_buffer[MOS_PAGE_SIZE];
    memset(test_buffer, 0, MOS_PAGE_SIZE);
    mos_t_idx_data* index_data = (mos_t_idx_data*)test_buffer;
    strncpy(index_data->index.name, "test_index", sizeof(index_data->index.name) - 1);
    index_data->index.offset_file = 8192;
    index_data->index.type = MOS_IDX_HASH_MAP;
    index_data->index.index_size = 0;

    mos_t_idx_hmap* hash_map_index = (mos_t_idx_hmap*)index_data->index_payload;
    hash_map_index->index_header.table_size = 4;
    uint64_t* index_values = hash_map_index->data;
    uint64_t* index_verifiers = hash_map_index->data + hash_map_index->index_header.table_size;

    index_values[0] = 10;
    index_values[1] = 11;
    index_values[2] = 12;
    index_values[3] = 13;

    index_verifiers[0] = 20;
    index_verifiers[1] = 21;
    index_verifiers[2] = 22;
    index_verifiers[3] = 23;

    uint8_t key1 = 1;

    //Act
    uint64_t result = mos_idx_hmap_put(index_data, &key1, 1, 5);

    //Assert
    TEST_ASSERT_EQUAL(-1, result);

    //Assert no values were changed
    TEST_ASSERT_EQUAL(index_values[0], 10);
    TEST_ASSERT_EQUAL(index_values[1], 11);
    TEST_ASSERT_EQUAL(index_values[2], 12);
    TEST_ASSERT_EQUAL(index_values[3], 13);
    TEST_ASSERT_EQUAL(index_verifiers[0], 20);
    TEST_ASSERT_EQUAL(index_verifiers[1], 21);
    TEST_ASSERT_EQUAL(index_verifiers[2], 22);
    TEST_ASSERT_EQUAL(index_verifiers[3], 23);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_mos_idx_hmap_init__even_item_count);
    RUN_TEST(test_mos_idx_hmap_init__odd_item_count);
    RUN_TEST(test_mos_idx_hmap_size);
    RUN_TEST(test_mos_idx_hmap_put__first_slot_available);
    RUN_TEST(test_mos_idx_hmap_put__first_slot_occupied);
    RUN_TEST(test_mos_idx_hmap_put__table_full);
    return UNITY_END();
}