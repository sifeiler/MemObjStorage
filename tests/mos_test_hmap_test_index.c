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

#define TEST_HMAP_BUFFER_SIZE (MOS_PAGE_SIZE * 3)
static uint8_t test_buffer[TEST_HMAP_BUFFER_SIZE];

static mos_t_idx_context test_arrange_hmap(uint64_t table_size) {
    memset(test_buffer, 0, TEST_HMAP_BUFFER_SIZE);

    mos_t_idx_data* index_data = (mos_t_idx_data*)test_buffer;
    index_data->header.index_desc.id = 0;
    index_data->header.index_desc.index_region_pos = 0;
    index_data->header.index_desc.type = MOS_IDX_HASH_MAP;
    index_data->header.index_desc.index_size = TEST_HMAP_BUFFER_SIZE;
    index_data->header.index_payload_offset = MOS_PAGE_SIZE;

    mos_t_idx_hmap* hash_map_index = (mos_t_idx_hmap*)(test_buffer + MOS_PAGE_SIZE);
    hash_map_index->index_header.table_size = table_size;
    hash_map_index->index_header.offset_values = MOS_PAGE_SIZE;
    hash_map_index->index_header.offset_verifiers =
        hash_map_index->index_header.offset_values + (table_size * sizeof(mos_t_idx_value_node));

    return (mos_t_idx_context){
        .idx_data = index_data,
        .idx_type = MOS_IDX_HASH_MAP
    };
}

void setUp(void) {}

void tearDown(void) {}

void test_mos_idx_hmap_init__even_item_count(void) {
    //Arrange
    mos_t_idx_context context = test_arrange_hmap(8);

    // value will be aligned up to page size and in this case the index fits into a single page
    uint64_t expected_index_size = MOS_PAGE_SIZE * 3;   //page 1: mos_t_idx_data_header, page 2: hmap header, page 3: hmap data
    //20 * 2 = 40 -> 64 (next power of 2)
    uint64_t expected_table_size = 64;

    //values and verifiers share a page here
    uint64_t expected_offset_values = MOS_PAGE_SIZE;
    uint64_t expected_offset_verifiers = expected_offset_values + (expected_table_size * sizeof(mos_t_idx_value_node));

    //Act
    mos_idx_hmap_init(20, &context.idx_data->header.index_desc, context.idx_data);

    //Assert
    mos_t_idx_data* index_data = context.idx_data;
    mos_t_idx_hmap* hash_map_index = (mos_t_idx_hmap*)(test_buffer + MOS_PAGE_SIZE);
    TEST_ASSERT_EQUAL(expected_index_size, index_data->header.index_desc.index_size);
    TEST_ASSERT_EQUAL(expected_table_size, hash_map_index->index_header.table_size);
    TEST_ASSERT_EQUAL(expected_offset_values, hash_map_index->index_header.offset_values);
    TEST_ASSERT_EQUAL(expected_offset_verifiers, hash_map_index->index_header.offset_verifiers);
}

void test_mos_idx_hmap_init__odd_item_count(void) {
    //Arrange
    mos_t_idx_context context = test_arrange_hmap(8);

    mos_t_idx_hmap* hash_map_index = (mos_t_idx_hmap*)(test_buffer + MOS_PAGE_SIZE);
    // value will be aligned up to page size and in this case the index fits into a single page
    uint64_t expected_index_size = MOS_PAGE_SIZE * 3;   //page 1: mos_t_idx, page 2: hmap header, page 3: hmap data
    //11 * 2 = 22 -> 32 (next power of 2)
    uint64_t expected_table_size = 32;

    //values and verifiers share a page here
    uint64_t expected_offset_values = MOS_PAGE_SIZE;
    uint64_t expected_offset_verifiers = expected_offset_values + (expected_table_size * sizeof(mos_t_idx_value_node));

    //Act
    mos_idx_hmap_init(11, &context.idx_data->header.index_desc, context.idx_data);

    //Assert
    TEST_ASSERT_EQUAL(expected_index_size, context.idx_data->header.index_desc.index_size);
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
    TEST_ASSERT_EQUAL(5 * MOS_PAGE_SIZE, actual_index_size);
}

void test_mos_idx_hmap_put__first_slot_available(void) {
    //Arrange
    mos_t_idx_context context = test_arrange_hmap(8);

    mos_t_idx_hmap* hash_map_index = (mos_t_idx_hmap*)(test_buffer + MOS_PAGE_SIZE);

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
    mos_t_id_list result1 = {0};
    int result1_ok = mos_idx_hmap_put(&context, &key1, 1, val1, &result1);
    // 1 will be hashed to binary ...011 = 3
    mos_t_id_list result2 = {0};
    int result2_ok = mos_idx_hmap_put(&context, &key2, 1, val2, &result2);

    //Assert
    mos_t_idx_value_node* index_values = (uint64_t*)(test_buffer + (MOS_PAGE_SIZE * 2));
    uint64_t* index_verifiers = index_values + hash_map_index->index_header.table_size;
    TEST_ASSERT_EQUAL(0, result1_ok);
    TEST_ASSERT_EQUAL(0, result2_ok);
    TEST_ASSERT_EQUAL_INT64(1, index_values[i1].values_count);
    TEST_ASSERT_EQUAL_INT64(1, index_values[i2].values_count);
    TEST_ASSERT_EQUAL_INT64(MOS_IDX_VALUES_INLINED, index_values[i1].capacity);
    TEST_ASSERT_EQUAL_INT64(MOS_IDX_VALUES_INLINED, index_values[i2].capacity);
    TEST_ASSERT_EQUAL_INT64(val1, index_values[i1].values.values_inlined[0]);
    TEST_ASSERT_EQUAL_INT64(val2, index_values[i2].values.values_inlined[0]);
    TEST_ASSERT_EQUAL_INT64(verifier1, index_verifiers[i1]);
    TEST_ASSERT_EQUAL_INT64(verifier2, index_verifiers[i2]);
}

void test_mos_idx_hmap_put__first_slot_occupied(void) {
    //Arrange
    mos_t_idx_context context = test_arrange_hmap(8);

    mos_t_idx_hmap* hash_map_index = (mos_t_idx_hmap*)(test_buffer + MOS_PAGE_SIZE);
    mos_t_idx_value_node* index_values = (mos_t_idx_value_node*)(test_buffer + MOS_PAGE_SIZE * 2);
    uint64_t* index_verifiers = index_values + hash_map_index->index_header.table_size;
    index_values[0].values.values_inlined[0] = 10;
    index_values[0].values_count = 1;
    index_values[0].capacity = MOS_IDX_VALUES_INLINED;
    index_verifiers[0] = 7;

    uint8_t key1 = 1;
    __uint128_t hash1 = mos_idx_murmur_hash_3_128(&key1, MOS_IDX_MURMUR3_SEED, 1);
    __uint128_t i1 = hash1 & 7;
    __uint128_t verifier1 = (hash1 >> 64);

    //Act
    // 1 will be hashed to binary ...000 = 0
    mos_idx_hmap_put(&context, &key1, 1, 5, NULL);

    //Assert
    TEST_ASSERT_EQUAL(10, index_values[0].values.values_inlined[0]);
    TEST_ASSERT_EQUAL(5, index_values[i1].values.values_inlined[0]);
    TEST_ASSERT_EQUAL(7, index_verifiers[0]);
    TEST_ASSERT_EQUAL(verifier1, index_verifiers[i1]);
}

void test_mos_idx_hmap_put__table_full(void) {
    //Arrange
    mos_t_idx_context context = test_arrange_hmap(4);

    mos_t_idx_hmap* hash_map_index = (mos_t_idx_hmap*)(test_buffer + MOS_PAGE_SIZE);
    mos_t_idx_value_node* index_values = (mos_t_idx_value_node*)(test_buffer + MOS_PAGE_SIZE * 2);
    uint64_t* index_verifiers = index_values + hash_map_index->index_header.table_size;

    index_values[0].values.values_inlined[0] = 10;
    index_values[1].values.values_inlined[0] = 11;
    index_values[2].values.values_inlined[0] = 12;
    index_values[3].values.values_inlined[0] = 13;

    index_verifiers[0] = 20;
    index_verifiers[1] = 21;
    index_verifiers[2] = 22;
    index_verifiers[3] = 23;

    uint8_t key1 = 1;

    //Act
    int result_ok = mos_idx_hmap_put(&context, &key1, 1, 5, NULL);

    //Assert
    TEST_ASSERT_EQUAL(-1, result_ok);

    //Assert no values were changed
    TEST_ASSERT_EQUAL(10, index_values[0].values.values_inlined[0]);
    TEST_ASSERT_EQUAL(11, index_values[1].values.values_inlined[0]);
    TEST_ASSERT_EQUAL(12, index_values[2].values.values_inlined[0]);
    TEST_ASSERT_EQUAL(13, index_values[3].values.values_inlined[0]);
    TEST_ASSERT_EQUAL(20, index_verifiers[0]);
    TEST_ASSERT_EQUAL(21, index_verifiers[1]);
    TEST_ASSERT_EQUAL(22, index_verifiers[2]);
    TEST_ASSERT_EQUAL(23, index_verifiers[3]);
}

void test_mos_idx_hmap_put__inline_full__no_split(void) {
    // put 3 values under one key — the exact inline boundary
    // find, assert count==3, all three present, capacity still == MOS_IDX_VALUES_INLINED
}

void test_mos_idx_hmap_put__inline_full__next_put_triggers_split(void) {
    // put 4 values — the exact transition bug we found (memcpy sizing, capacity init)
    // find, assert count==4, ALL FOUR present and correct, including the 3 that were copied from inline
}

void test_mos_idx_hmap_put__arena_backed__puts_grow_arena_multiple_times(void) {
    // put e.g. 50 values under one key — forces mos_arena_append to grow more than once
    // find, assert count==50, all 50 present in insertion order
}

void test_mos_idx_hmap_remove__arena_backed__remove_key__frees_arena_and_tombstones(void) {
    // put enough values to spill, then remove_key
    // assert find() on that key now returns not-found
}

void test_mos_idx_hmap_put_remove__arena_backed__slot_reused_by_different_key(void) {
    // put+spill key A, remove_key(A), put key B that happens to land on the same slot
    // (may need to force a collision, or just insert enough other keys that a reuse is likely)
    // assert find(B) returns ONLY B's values, none of A's stale data
    // this is the exact bug from your last message
}

void test_mos_idx_hmap_remove__arena_backed__value_shrinks_but_stays_arena_backed(void) {
    // spill a key (capacity > 3), remove_value down to count <= 3
    // find afterward: assert it STILL reads from arena, not from (stale) inline storage
    // this is the exact bug from a few messages back
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_mos_idx_hmap_init__even_item_count);
    RUN_TEST(test_mos_idx_hmap_init__odd_item_count);
    RUN_TEST(test_mos_idx_hmap_size);
    RUN_TEST(test_mos_idx_hmap_put__first_slot_available);
    RUN_TEST(test_mos_idx_hmap_put__first_slot_occupied);
    RUN_TEST(test_mos_idx_hmap_put__table_full);
    RUN_TEST(test_mos_idx_hmap_put__inline_full__no_split);
    RUN_TEST(test_mos_idx_hmap_put__inline_full__next_put_triggers_split);
    RUN_TEST(test_mos_idx_hmap_put__arena_backed__puts_grow_arena_multiple_times);
    RUN_TEST(test_mos_idx_hmap_remove__arena_backed__remove_key__frees_arena_and_tombstones);
    RUN_TEST(test_mos_idx_hmap_put_remove__arena_backed__slot_reused_by_different_key);
    RUN_TEST(test_mos_idx_hmap_remove__arena_backed__value_shrinks_but_stays_arena_backed);
    return UNITY_END();
}