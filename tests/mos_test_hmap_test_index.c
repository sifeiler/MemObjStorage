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

#define TEST_HMAP_BUFFER_SIZE (MOS_PAGE_SIZE * 6)
static uint8_t test_buffer[TEST_HMAP_BUFFER_SIZE];

static mos_t_idx_context test_arrange_hmap(uint64_t table_size) {
    memset(test_buffer, 0, TEST_HMAP_BUFFER_SIZE);

    mos_t_idx_data* index_data = (mos_t_idx_data*)test_buffer;
    index_data->header.index_desc.id = 0;
    index_data->header.index_desc.index_region_pos = 0;
    index_data->header.index_desc.type = MOS_IDX_HASH_MAP;
    index_data->header.index_desc.index_size = MOS_PAGE_SIZE * 3;
    index_data->header.index_payload_offset = MOS_PAGE_SIZE;

    mos_t_idx_hmap* hash_map_index = (mos_t_idx_hmap*)(test_buffer + MOS_PAGE_SIZE);
    hash_map_index->index_header.table_size = table_size;
    hash_map_index->index_header.offset_values = MOS_PAGE_SIZE;
    hash_map_index->index_header.offset_verifiers = hash_map_index->index_header.offset_values + (table_size * sizeof(mos_t_idx_value_node));

    //arena_region_header, arena_header and arena cover the last three pages
    mos_t_mapped_region* arena_region = (mos_t_mapped_region*)(&test_buffer[MOS_PAGE_SIZE * 3]);
    arena_region->region_base = &test_buffer[MOS_PAGE_SIZE * 4];
    arena_region->region_byte_size = MOS_PAGE_SIZE * 2;
    mos_arena_init(arena_region);

    return (mos_t_idx_context){
        .idx_data = index_data,
        .idx_type = MOS_IDX_HASH_MAP,
        .kind.hmap = {
            .arena_region = arena_region
        }
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
    TEST_ASSERT_EQUAL_UINT64(expected_index_size, index_data->header.index_desc.index_size);
    TEST_ASSERT_EQUAL_UINT64(expected_table_size, hash_map_index->index_header.table_size);
    TEST_ASSERT_EQUAL_UINT64(expected_offset_values, hash_map_index->index_header.offset_values);
    TEST_ASSERT_EQUAL_UINT64(expected_offset_verifiers, hash_map_index->index_header.offset_verifiers);
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
    TEST_ASSERT_EQUAL_UINT64(5 * MOS_PAGE_SIZE, actual_index_size);
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
    TEST_ASSERT_EQUAL_UINT64(1, index_values[i1].values_count);
    TEST_ASSERT_EQUAL_UINT64(1, index_values[i2].values_count);
    TEST_ASSERT_EQUAL_UINT64(MOS_IDX_VALUES_INLINED, index_values[i1].capacity);
    TEST_ASSERT_EQUAL_UINT64(MOS_IDX_VALUES_INLINED, index_values[i2].capacity);
    TEST_ASSERT_EQUAL_UINT64(val1, index_values[i1].values.values_inlined[0]);
    TEST_ASSERT_EQUAL_UINT64(val2, index_values[i2].values.values_inlined[0]);
    TEST_ASSERT_EQUAL_UINT64(verifier1, index_verifiers[i1]);
    TEST_ASSERT_EQUAL_UINT64(verifier2, index_verifiers[i2]);
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
    TEST_ASSERT_EQUAL_UINT64(10, index_values[0].values.values_inlined[0]);
    TEST_ASSERT_EQUAL_UINT64(5, index_values[i1].values.values_inlined[0]);
    TEST_ASSERT_EQUAL_UINT64(7, index_verifiers[0]);
    TEST_ASSERT_EQUAL_UINT64(verifier1, index_verifiers[i1]);
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
    TEST_ASSERT_EQUAL_UINT64(10, index_values[0].values.values_inlined[0]);
    TEST_ASSERT_EQUAL_UINT64(11, index_values[1].values.values_inlined[0]);
    TEST_ASSERT_EQUAL_UINT64(12, index_values[2].values.values_inlined[0]);
    TEST_ASSERT_EQUAL_UINT64(13, index_values[3].values.values_inlined[0]);
    TEST_ASSERT_EQUAL_UINT64(20, index_verifiers[0]);
    TEST_ASSERT_EQUAL_UINT64(21, index_verifiers[1]);
    TEST_ASSERT_EQUAL_UINT64(22, index_verifiers[2]);
    TEST_ASSERT_EQUAL_UINT64(23, index_verifiers[3]);
}

void test_mos_idx_hmap_put__inline_full__no_split(void) {
    // Arrange
    mos_t_idx_context context = test_arrange_hmap(8);

    mos_t_idx_hmap* hash_map_index = (mos_t_idx_hmap*)(test_buffer + MOS_PAGE_SIZE);
    mos_t_idx_value_node* index_values = (mos_t_idx_value_node*)(test_buffer + MOS_PAGE_SIZE * 2);
    uint64_t* index_verifiers = index_values + hash_map_index->index_header.table_size;

    uint8_t key1 = 1;
    __uint128_t hash1 = mos_idx_murmur_hash_3_128(&key1, MOS_IDX_MURMUR3_SEED, 1);
    __uint128_t i1 = hash1 & 7;
    __uint128_t verifier1 = (hash1 >> 64);

    // Act
    for(uint64_t i = 0; i < MOS_IDX_VALUES_INLINED; i++) {
        mos_idx_hmap_put(&context, &key1, sizeof(uint8_t), i, NULL);
    }

    mos_t_id_list result_list = {0};
    mos_idx_hmap_get(&context, &key1, sizeof(key1), &result_list);

    // Assert
    TEST_ASSERT_EQUAL_UINT64(MOS_IDX_VALUES_INLINED, index_values[i1].capacity);
    TEST_ASSERT_EQUAL_UINT64(5, index_values[i1].values_count);

    for(int i = 0; i < MOS_IDX_VALUES_INLINED; i++) {
        TEST_ASSERT_EQUAL_UINT64(i, index_values[i1].values.values_inlined[i]);
    }

    TEST_ASSERT_EQUAL_UINT64(5, result_list.count);
    for(int i = 0; i < MOS_IDX_VALUES_INLINED; i++) {
        TEST_ASSERT_EQUAL_UINT64(i, result_list.ids[i]);
    }
}

void test_mos_idx_hmap_put__inline_full__next_put_triggers_split(void) {
    // Arrange
    mos_t_idx_context context = test_arrange_hmap(8);

    mos_t_idx_hmap* hash_map_index = (mos_t_idx_hmap*)(test_buffer + MOS_PAGE_SIZE);
    mos_t_idx_value_node* index_values = (mos_t_idx_value_node*)(test_buffer + MOS_PAGE_SIZE * 2);
    uint64_t* index_verifiers = index_values + hash_map_index->index_header.table_size;

    uint8_t key1 = 1;
    __uint128_t hash1 = mos_idx_murmur_hash_3_128(&key1, MOS_IDX_MURMUR3_SEED, 1);
    __uint128_t i1 = hash1 & 7;
    __uint128_t verifier1 = (hash1 >> 64);

    uint64_t items_to_put = MOS_IDX_VALUES_INLINED + 1;

    // Act
    for(uint64_t i = 0; i < items_to_put; i++) {
        mos_idx_hmap_put(&context, &key1, sizeof(uint8_t), i, NULL);
    }

    mos_t_id_list result_list = {0};
    mos_idx_hmap_get(&context, &key1, sizeof(key1), &result_list);

    // Assert
    TEST_ASSERT_EQUAL_UINT64(MOS_IDX_VALUES_INLINED * 4, index_values[i1].capacity);
    TEST_ASSERT_EQUAL_UINT64(items_to_put, index_values[i1].values_count);
    // Arenas actually start at MOS_PAGE_SIZE. First page is used by arena header.
    TEST_ASSERT_EQUAL_UINT64(MOS_PAGE_SIZE, index_values[i1].values.arena_offset.arena_offset);
    TEST_ASSERT_EQUAL_UINT64(MOS_IDX_VALUES_INLINED * 4 * sizeof(uint64_t), index_values[i1].values.arena_offset.arena_size);

    // now we look into the arena
    uint64_t* arena_block = mos_arena_accessor(context.kind.hmap.arena_region, &index_values[i1].values.arena_offset);
    for(int i = 0; i < items_to_put; i++) {
        TEST_ASSERT_EQUAL_UINT64(i, arena_block[i]);
    }

    // check if the result holds the values of the arena
    TEST_ASSERT_EQUAL_UINT64(6, result_list.count);
    for(int i = 0; i < items_to_put; i++) {
        TEST_ASSERT_EQUAL_UINT64(i, result_list.ids[i]);
    }

    // compare arena and result addresses
    for(int i = 0; i < items_to_put; i++) {
        TEST_ASSERT_EQUAL_PTR(&arena_block[i], &result_list.ids[i]);
    }
}

void test_mos_idx_hmap_put__arena_backed__puts_grow_arena_multiple_times(void) {
    // Arrange
    mos_t_idx_context context = test_arrange_hmap(8);

    mos_t_idx_hmap* hash_map_index = (mos_t_idx_hmap*)(test_buffer + MOS_PAGE_SIZE);
    mos_t_idx_value_node* index_values = (mos_t_idx_value_node*)(test_buffer + MOS_PAGE_SIZE * 2);
    uint64_t* index_verifiers = index_values + hash_map_index->index_header.table_size;

    uint8_t key1 = 1;
    __uint128_t hash1 = mos_idx_murmur_hash_3_128(&key1, MOS_IDX_MURMUR3_SEED, 1);
    __uint128_t i1 = hash1 & 7;
    __uint128_t verifier1 = (hash1 >> 64);

    int items_to_put_first_split = MOS_IDX_VALUES_INLINED + 1;
    int items_to_put_first_arena_resize = MOS_IDX_VALUES_INLINED * 4 + 1;
    int items_to_put_second_arena_resize = MOS_IDX_VALUES_INLINED * 4 * 2 + 1;

    // Act - from inlined to arena
    uint64_t i = 0;
    for(i; i < items_to_put_first_split; i++) {
        mos_idx_hmap_put(&context, &key1, sizeof(uint8_t), i, NULL);
    }

    mos_t_id_list result_list = {0};
    mos_idx_hmap_get(&context, &key1, sizeof(key1), &result_list);

    // Assert - from inlined to arena
    uint64_t expected_capacity_first_arena = MOS_IDX_VALUES_INLINED * 4;
    uint64_t expected_arena_size_first_arena = MOS_IDX_VALUES_INLINED * 4 * sizeof(uint64_t);
    TEST_ASSERT_EQUAL_UINT64(MOS_IDX_VALUES_INLINED * 4, index_values[i1].capacity);
    TEST_ASSERT_EQUAL_UINT64(items_to_put_first_split, index_values[i1].values_count);
    TEST_ASSERT_EQUAL_UINT64(MOS_PAGE_SIZE, index_values[i1].values.arena_offset.arena_offset);
    TEST_ASSERT_EQUAL_UINT64(expected_arena_size_first_arena, index_values[i1].values.arena_offset.arena_size);

    // now we look into the arena
    uint64_t* arena_block = mos_arena_accessor(context.kind.hmap.arena_region, &index_values[i1].values.arena_offset);
    for(uint64_t i = 0; i < items_to_put_first_split; i++) {
        TEST_ASSERT_EQUAL_UINT64(i, arena_block[i]);
    }

    // check if the result holds the values of the arena
    TEST_ASSERT_EQUAL_UINT64(items_to_put_first_split, result_list.count);
    for(uint64_t i = 0; i < items_to_put_first_split; i++) {
        TEST_ASSERT_EQUAL_UINT64(i, result_list.ids[i]);
    }

    // compare arena and result addresses
    for(uint64_t i = 0; i < items_to_put_first_split; i++) {
        TEST_ASSERT_EQUAL_PTR(&arena_block[i], &result_list.ids[i]);
    }

    // Act - first arena realloc
    for(i; i < items_to_put_first_arena_resize; i++) {
        mos_idx_hmap_put(&context, &key1, sizeof(uint8_t), i, NULL);
    }

    mos_idx_hmap_get(&context, &key1, sizeof(key1), &result_list);

    // Assert - first arena realloc
    uint64_t expected_capacity_second_arena = expected_capacity_first_arena * 2;
    uint64_t expected_arena_size_second_arena = expected_arena_size_first_arena * 2;
    TEST_ASSERT_EQUAL_UINT64(expected_capacity_second_arena, index_values[i1].capacity);
    TEST_ASSERT_EQUAL_UINT64(items_to_put_first_arena_resize, index_values[i1].values_count);
    TEST_ASSERT_EQUAL_UINT64(MOS_PAGE_SIZE + expected_arena_size_first_arena, index_values[i1].values.arena_offset.arena_offset);
    TEST_ASSERT_EQUAL_UINT64(expected_arena_size_second_arena, index_values[i1].values.arena_offset.arena_size);

    // now we look into the arena
    arena_block = mos_arena_accessor(context.kind.hmap.arena_region, &index_values[i1].values.arena_offset);
    for(uint64_t i = 0; i < items_to_put_first_arena_resize; i++) {
        TEST_ASSERT_EQUAL_UINT64(i, arena_block[i]);
    }

    // check if the result holds the values of the arena
    TEST_ASSERT_EQUAL_UINT64(items_to_put_first_arena_resize, result_list.count);
    for(uint64_t i = 0; i < items_to_put_first_arena_resize; i++) {
        TEST_ASSERT_EQUAL_UINT64(i, result_list.ids[i]);
    }

    // compare arena and result addresses
    for(uint64_t i = 0; i < items_to_put_first_arena_resize; i++) {
        TEST_ASSERT_EQUAL_UINT64(&arena_block[i], &result_list.ids[i]);
    }

    // Act - second arena realloc
    for(i; i < items_to_put_second_arena_resize; i++) {
        mos_idx_hmap_put(&context, &key1, sizeof(uint8_t), i, NULL);
    }

    mos_idx_hmap_get(&context, &key1, sizeof(key1), &result_list);

    // Assert - second arena realloc
    TEST_ASSERT_EQUAL_UINT64(expected_capacity_second_arena * 2, index_values[i1].capacity);
    TEST_ASSERT_EQUAL_UINT64(items_to_put_second_arena_resize, index_values[i1].values_count);
    TEST_ASSERT_EQUAL_UINT64(MOS_PAGE_SIZE + expected_arena_size_first_arena + expected_arena_size_second_arena, index_values[i1].values.arena_offset.arena_offset);
    TEST_ASSERT_EQUAL_UINT64(expected_arena_size_second_arena * 2, index_values[i1].values.arena_offset.arena_size);

    // now we look into the arena
    arena_block = mos_arena_accessor(context.kind.hmap.arena_region, &index_values[i1].values.arena_offset);
    for(uint64_t i = 0; i < items_to_put_second_arena_resize; i++) {
        TEST_ASSERT_EQUAL_UINT64(i, arena_block[i]);
    }

    // check if the result holds the values of the arena
    TEST_ASSERT_EQUAL_UINT64(items_to_put_second_arena_resize, result_list.count);
    for(uint64_t i = 0; i < items_to_put_second_arena_resize; i++) {
        TEST_ASSERT_EQUAL_UINT64(i, result_list.ids[i]);
    }

    // compare arena and result addresses
    for(uint64_t i = 0; i < items_to_put_second_arena_resize; i++) {
        TEST_ASSERT_EQUAL_PTR(&arena_block[i], &result_list.ids[i]);
    }
}

void test_mos_idx_hmap_remove__arena_backed__remove_key__frees_arena_and_tombstones(void) {
    // Arrange
    mos_t_idx_context context = test_arrange_hmap(8);

    mos_t_idx_hmap* hash_map_index = (mos_t_idx_hmap*)(test_buffer + MOS_PAGE_SIZE);
    mos_t_idx_value_node* index_values = (mos_t_idx_value_node*)(test_buffer + MOS_PAGE_SIZE * 2);
    uint64_t* index_verifiers = index_values + hash_map_index->index_header.table_size;

    uint8_t key1 = 1;
    __uint128_t hash1 = mos_idx_murmur_hash_3_128(&key1, MOS_IDX_MURMUR3_SEED, 1);
    __uint128_t i1 = hash1 & 7;
    __uint128_t verifier1 = (hash1 >> 64);

    uint64_t items_to_put = MOS_IDX_VALUES_INLINED + 1;

    for(uint64_t i = 0; i < items_to_put; i++) {
        mos_idx_hmap_put(&context, &key1, sizeof(uint8_t), i, NULL);
    }

    mos_t_id_list result_list = {0};
    mos_idx_hmap_get(&context, &key1, sizeof(key1), &result_list);

    TEST_ASSERT_EQUAL_UINT64(MOS_IDX_VALUES_INLINED * 4, index_values[i1].capacity);
    TEST_ASSERT_EQUAL_UINT64(items_to_put, index_values[i1].values_count);
    // Arenas actually start at MOS_PAGE_SIZE. First page is used by arena header.
    TEST_ASSERT_EQUAL_UINT64(MOS_PAGE_SIZE, index_values[i1].values.arena_offset.arena_offset);
    TEST_ASSERT_EQUAL_UINT64(MOS_IDX_VALUES_INLINED * 4 * sizeof(uint64_t), index_values[i1].values.arena_offset.arena_size);

    // now we look into the arena
    uint64_t* arena_block = mos_arena_accessor(context.kind.hmap.arena_region, &index_values[i1].values.arena_offset);
    for(int i = 0; i < items_to_put; i++) {
        TEST_ASSERT_EQUAL_UINT64(i, arena_block[i]);
    }

    // check if the result holds the values of the arena
    TEST_ASSERT_EQUAL_UINT64(items_to_put, result_list.count);
    for(uint64_t i = 0; i < items_to_put; i++) {
        TEST_ASSERT_EQUAL_UINT64(i, result_list.ids[i]);
    }

    // compare arena and result addresses
    for(int i = 0; i < items_to_put; i++) {
        TEST_ASSERT_EQUAL_PTR(&arena_block[i], &result_list.ids[i]);
    }

    // now remove
    int remove_ok = mos_idx_hmap_remove_key(&context, &key1, sizeof(key1));

    TEST_ASSERT_EQUAL(0, remove_ok);

    mos_t_id_list result_list_after_remove = {0};
    mos_idx_hmap_get(&context, &key1, sizeof(key1), &result_list_after_remove);
    TEST_ASSERT_EQUAL_UINT64(0, result_list_after_remove.count);
    TEST_ASSERT_NULL(result_list_after_remove.ids);
}

void test_mos_idx_hmap_put_remove__arena_backed__slot_reused_but_inlined(void) {
    // Arrange
    mos_t_idx_context context = test_arrange_hmap(8);
    mos_t_arena_region_header* arena_header = mos_arena_accessor_header(context.kind.hmap.arena_region);

    mos_t_idx_hmap* hash_map_index = (mos_t_idx_hmap*)(test_buffer + MOS_PAGE_SIZE);
    mos_t_idx_value_node* index_values = (mos_t_idx_value_node*)(test_buffer + MOS_PAGE_SIZE * 2);
    uint64_t* index_verifiers = index_values + hash_map_index->index_header.table_size;

    uint8_t key1 = 1;
    __uint128_t hash1 = mos_idx_murmur_hash_3_128(&key1, MOS_IDX_MURMUR3_SEED, 1);
    __uint128_t i1 = hash1 & 7;
    __uint128_t verifier1 = (hash1 >> 64);

    int items_to_put_first_split = MOS_IDX_VALUES_INLINED + 1;

    // Act
    uint64_t i = 0;
    for(i; i < items_to_put_first_split; i++) {
        mos_idx_hmap_put(&context, &key1, sizeof(uint8_t), i, NULL);
    }

    mos_t_arena_offset* old_arena_block = (mos_t_arena_offset*)mos_arena_accessor(context.kind.hmap.arena_region, &index_values[i1].values.arena_offset);

    // remove key1
    mos_idx_hmap_remove_key(&context, &key1, sizeof(uint8_t));

    // put key1 again with a new value i
    i++;
    mos_idx_hmap_put(&context, &key1, sizeof(uint8_t), i, NULL);

    mos_t_id_list result_list = {0};
    mos_idx_hmap_get(&context, &key1, sizeof(uint8_t), &result_list);

    // check if the result holds the new value
    TEST_ASSERT_EQUAL_UINT64(1, result_list.count);
    TEST_ASSERT_EQUAL_UINT64(i, result_list.ids[0]);
    TEST_ASSERT_EQUAL_UINT64(i, index_values[i1].values.values_inlined[0]);
    TEST_ASSERT_EQUAL_UINT64(MOS_NULL_OFFSET, old_arena_block->arena_offset);
    TEST_ASSERT_EQUAL_UINT64(0, old_arena_block->arena_size);

    TEST_ASSERT_EQUAL_UINT64(MOS_PAGE_SIZE, arena_header->last_deleted_offset.arena_offset);
    TEST_ASSERT_EQUAL_UINT64(MOS_IDX_VALUES_INLINED * 4 * sizeof(uint64_t), arena_header->last_deleted_offset.arena_size);
}

void test_mos_idx_hmap_put_remove__arena_backed__arena_reused(void) {
    // Arrange
    mos_t_idx_context context = test_arrange_hmap(8);
    mos_t_arena_region_header* arena_header = mos_arena_accessor_header(context.kind.hmap.arena_region);

    mos_t_idx_hmap* hash_map_index = (mos_t_idx_hmap*)(test_buffer + MOS_PAGE_SIZE);
    mos_t_idx_value_node* index_values = (mos_t_idx_value_node*)(test_buffer + MOS_PAGE_SIZE * 2);
    uint64_t* index_verifiers = index_values + hash_map_index->index_header.table_size;

    uint8_t key1 = 1;
    __uint128_t hash1 = mos_idx_murmur_hash_3_128(&key1, MOS_IDX_MURMUR3_SEED, 1);
    __uint128_t i1 = hash1 & 7;
    __uint128_t verifier1 = (hash1 >> 64);

    int items_to_put_first_split = MOS_IDX_VALUES_INLINED + 1;

    // Act
    uint64_t i = 0;
    for(i; i < items_to_put_first_split; i++) {
        mos_idx_hmap_put(&context, &key1, sizeof(uint8_t), i, NULL);
    }

    mos_t_arena_offset* old_arena_block = (mos_t_arena_offset*)mos_arena_accessor(context.kind.hmap.arena_region, &index_values[i1].values.arena_offset);

    // remove key1
    mos_idx_hmap_remove_key(&context, &key1, sizeof(uint8_t));

    // put key1 again with a new value i
    for(int i = items_to_put_first_split; i < items_to_put_first_split * 2; i++) {
        mos_idx_hmap_put(&context, &key1, sizeof(uint8_t), i, NULL);
    }

    mos_t_id_list result_list = {0};
    mos_idx_hmap_get(&context, &key1, sizeof(key1), &result_list);

    mos_t_arena_offset* new_arena_block = (mos_t_arena_offset*)mos_arena_accessor(context.kind.hmap.arena_region, &index_values[i1].values.arena_offset);

    // check if the result holds the new values
    TEST_ASSERT_EQUAL_UINT64(items_to_put_first_split, result_list.count);

    for(int i = 0; i < items_to_put_first_split; i++) {
        TEST_ASSERT_EQUAL_UINT64(i + items_to_put_first_split, result_list.ids[i]);
    }

    TEST_ASSERT_EQUAL_PTR(old_arena_block, new_arena_block);

    TEST_ASSERT_EQUAL_UINT64(MOS_NULL_OFFSET, arena_header->last_deleted_offset.arena_offset);
    TEST_ASSERT_EQUAL_UINT64(0, arena_header->last_deleted_offset.arena_size);
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
    RUN_TEST(test_mos_idx_hmap_put_remove__arena_backed__slot_reused_but_inlined);
    RUN_TEST(test_mos_idx_hmap_put_remove__arena_backed__arena_reused);
    return UNITY_END();
}