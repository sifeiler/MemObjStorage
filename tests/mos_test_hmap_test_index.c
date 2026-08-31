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
    uint16_t buffer_size = MOS_PAGE_SIZE * 3;
    uint8_t test_buffer[buffer_size];
    memset(test_buffer, 0, buffer_size);
    mos_t_idx_data* index_data = (mos_t_idx_data*)test_buffer;
    index_data->header.index.id = 0;
    index_data->header.index.index_offset = 0;
    index_data->header.index.type = MOS_IDX_HASH_MAP;
    index_data->header.index.index_size = buffer_size;
    index_data->header.index_payload_offset = MOS_PAGE_SIZE;

    // value will be aligned up to page size and in this case the index fits into a single page
    uint64_t expected_index_size = MOS_PAGE_SIZE * 2;   //page 1: mos_t_idx_data_header, page 2: hmap header, page 3: hmap data
    //20 * 2 = 40 -> 64 (next power of 2)
    uint64_t expected_table_size = 64;

    //values and verifiers share a page here
    uint64_t expected_offset_values = MOS_PAGE_SIZE;
    uint64_t expected_offset_verifiers = expected_offset_values + (expected_table_size * sizeof(uint64_t));

    //Act
    mos_idx_hmap_init(20, &index_data->header.index, index_data);

    //Assert
    mos_t_idx_hmap* hash_map_index = (mos_t_idx_hmap*)(test_buffer + MOS_PAGE_SIZE);
    TEST_ASSERT_EQUAL(expected_index_size, index_data->header.index.index_size);
    TEST_ASSERT_EQUAL(expected_table_size, hash_map_index->index_header.table_size);
    TEST_ASSERT_EQUAL(expected_offset_values, hash_map_index->index_header.offset_values);
    TEST_ASSERT_EQUAL(expected_offset_verifiers, hash_map_index->index_header.offset_verifiers);
}

void test_mos_idx_hmap_init__odd_item_count(void) {
    //Arrange
    uint16_t buffer_size = MOS_PAGE_SIZE * 3;
    uint8_t test_buffer[buffer_size];
    memset(test_buffer, 0, buffer_size);
    mos_t_idx_data* index_data = (mos_t_idx_data*)test_buffer;
    index_data->header.index.id = 0;
    index_data->header.index.index_offset = 0;
    index_data->header.index.type = MOS_IDX_HASH_MAP;
    index_data->header.index.index_size = buffer_size;
    index_data->header.index_payload_offset = MOS_PAGE_SIZE;

    mos_t_idx_hmap* hash_map_index = (mos_t_idx_hmap*)(test_buffer + MOS_PAGE_SIZE);

    // value will be aligned up to page size and in this case the index fits into a single page
    uint64_t expected_index_size = MOS_PAGE_SIZE * 2;   //page 1: mos_t_idx, page 2: hmap header, page 3: hmap data
    //11 * 2 = 22 -> 32 (next power of 2)
    uint64_t expected_table_size = 32;

    //values and verifiers share a page here
    uint64_t expected_offset_values = MOS_PAGE_SIZE;
    uint64_t expected_offset_verifiers = expected_offset_values + (expected_table_size * sizeof(uint64_t));

    //Act
    mos_idx_hmap_init(11, &index_data->header.index, index_data);

    //Assert
    TEST_ASSERT_EQUAL(expected_index_size, index_data->header.index.index_size);
    TEST_ASSERT_EQUAL(expected_table_size, hash_map_index->index_header.table_size);
    TEST_ASSERT_EQUAL(expected_offset_values, hash_map_index->index_header.offset_values);
    TEST_ASSERT_EQUAL(expected_offset_verifiers, hash_map_index->index_header.offset_verifiers);
}

void test_mos_idx_hmap_size(void) {
    //Arrange
    uint64_t item_count = 100;

    //Act
    uint64_t actual_index_size = mos_idx_hmap_size(item_count, NULL);

    //Assert
    TEST_ASSERT_EQUAL(8192, actual_index_size);
}

void test_mos_idx_hmap_put__first_slot_available(void) {
    //Arrange
    uint16_t buffer_size = MOS_PAGE_SIZE * 3;
    uint8_t test_buffer[buffer_size];
    memset(test_buffer, 0, buffer_size);
    mos_t_idx_data* index_data = (mos_t_idx_data*)test_buffer;
    index_data->header.index.id = 0;
    index_data->header.index.index_offset = 0;
    index_data->header.index.type = MOS_IDX_HASH_MAP;
    index_data->header.index.index_size = buffer_size;
    index_data->header.index_payload_offset = MOS_PAGE_SIZE;

    mos_t_idx_hmap* hash_map_index = (mos_t_idx_hmap*)(test_buffer + MOS_PAGE_SIZE);
    hash_map_index->index_header.table_size = 8;
    hash_map_index->index_header.offset_values = MOS_PAGE_SIZE;
    hash_map_index->index_header.offset_verifiers = hash_map_index->index_header.offset_values + (hash_map_index->index_header.table_size * sizeof(uint64_t));

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
    int64_t result1 = mos_idx_hmap_put(index_data, &key1, 1, val1, NULL);
    // 1 will be hashed to binary ...011 = 3
    int64_t result2 = mos_idx_hmap_put(index_data, &key2, 1, val2, NULL);

    //Assert
    uint64_t* index_values = (uint64_t*)(test_buffer + (MOS_PAGE_SIZE * 2));
    uint64_t* index_verifiers = index_values + hash_map_index->index_header.table_size;
    TEST_ASSERT_NOT_EQUAL(-1, result1);
    TEST_ASSERT_NOT_EQUAL(-1, result2);
    TEST_ASSERT_EQUAL_INT64(val1, index_values[i1]);
    TEST_ASSERT_EQUAL_INT64(val2, index_values[i2]);
    TEST_ASSERT_EQUAL_INT64(verifier1, index_verifiers[i1]);
    TEST_ASSERT_EQUAL_INT64(verifier2, index_verifiers[i2]);
}

void test_mos_idx_hmap_put__first_slot_occupied(void) {
    //Arrange
    uint16_t buffer_size = MOS_PAGE_SIZE * 3;
    uint8_t test_buffer[buffer_size];
    memset(test_buffer, 0, buffer_size);
    mos_t_idx_data* index_data = (mos_t_idx_data*)test_buffer;
    index_data->header.index.id = 0;
    index_data->header.index.index_offset = 0;
    index_data->header.index.type = MOS_IDX_HASH_MAP;
    index_data->header.index.index_size = buffer_size;
    index_data->header.index_payload_offset = MOS_PAGE_SIZE;

    mos_t_idx_hmap* hash_map_index = (mos_t_idx_hmap*)(test_buffer + MOS_PAGE_SIZE);
    hash_map_index->index_header.table_size = 8;
    hash_map_index->index_header.offset_values = MOS_PAGE_SIZE;
    hash_map_index->index_header.offset_verifiers = hash_map_index->index_header.offset_values + (hash_map_index->index_header.table_size * sizeof(uint64_t));

    uint64_t* index_values = (uint64_t*)(test_buffer + (MOS_PAGE_SIZE * 2));
    uint64_t* index_verifiers = index_values + hash_map_index->index_header.table_size;
    index_values[0] = 10;
    index_verifiers[0] = 7;

    uint8_t key1 = 1;
    __uint128_t hash1 = mos_idx_murmur_hash_3_128(&key1, MOS_IDX_MURMUR3_SEED, 1);
    __uint128_t i1 = hash1 & 7;
    __uint128_t verifier1 = (hash1 >> 64);

    //Act
    // 1 will be hashed to binary ...000 = 0
    mos_idx_hmap_put(index_data, &key1, 1, 5, NULL);

    //Assert
    TEST_ASSERT_EQUAL(10, index_values[0]);
    TEST_ASSERT_EQUAL(5, index_values[i1]);
    TEST_ASSERT_EQUAL(7, index_verifiers[0]);
    TEST_ASSERT_EQUAL(verifier1, index_verifiers[i1]);
}

void test_mos_idx_hmap_put__table_full(void) {
    //Arrange
    uint16_t buffer_size = MOS_PAGE_SIZE * 3;
    uint8_t test_buffer[buffer_size];
    memset(test_buffer, 0, buffer_size);
    mos_t_idx_data* index_data = (mos_t_idx_data*)test_buffer;
    index_data->header.index.id = 0;
    index_data->header.index.index_offset = 0;
    index_data->header.index.type = MOS_IDX_HASH_MAP;
    index_data->header.index.index_size = buffer_size;
    index_data->header.index_payload_offset = MOS_PAGE_SIZE;

    mos_t_idx_hmap* hash_map_index = (mos_t_idx_hmap*)(test_buffer + MOS_PAGE_SIZE);
    hash_map_index->index_header.table_size = 4;
    hash_map_index->index_header.offset_values = MOS_PAGE_SIZE;
    hash_map_index->index_header.offset_verifiers = hash_map_index->index_header.offset_values + (hash_map_index->index_header.table_size * sizeof(uint64_t));

    uint64_t* index_values = (uint64_t*)(test_buffer + MOS_PAGE_SIZE * 2);
    uint64_t* index_verifiers = index_values + hash_map_index->index_header.table_size;

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
    uint64_t result = mos_idx_hmap_put(index_data, &key1, 1, 5, NULL);

    //Assert
    TEST_ASSERT_EQUAL(-1, result);

    //Assert no values were changed
    TEST_ASSERT_EQUAL(10, index_values[0]);
    TEST_ASSERT_EQUAL(11, index_values[1]);
    TEST_ASSERT_EQUAL(12, index_values[2]);
    TEST_ASSERT_EQUAL(13, index_values[3]);
    TEST_ASSERT_EQUAL(20, index_verifiers[0]);
    TEST_ASSERT_EQUAL(21, index_verifiers[1]);
    TEST_ASSERT_EQUAL(22, index_verifiers[2]);
    TEST_ASSERT_EQUAL(23, index_verifiers[3]);
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