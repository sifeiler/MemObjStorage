#include "unity.h"
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include "../include/mos.h"
#include "../include/mos_internal.h"
#include "../include/mos_idx_hmap.h"
#include "../include/mos_idx.h"
#include "../include/mos_utils.h"
#include "../include/mos_math.h"

typedef struct {
    uint64_t prop1;
    mos_t_string prop2;
} TestEntry;

typedef struct {
    //keep attributes seperatly as otherwise the data is not available in test functions
    mos_t_attr attributes[2];
    mos_t_idx indexes[2];
    mos_t_storage* storage;
} CreateTestConfig;

static CreateTestConfig test_config = {0};

static TestEntry* result1;
static TestEntry* result2;
static TestEntry* result3;
static TestEntry* result4;
static void* mmap_ptr;

void setUp(void) {
    mos_t_attr prop1 = {
        .name = "prop1",
        .type = MOS_ATTR_TYPE_INTERNAL_UINT64,
        .field_offset_external = offsetof(TestEntry, prop1),
        .field_offset_internal = 0,
        .byte_size_external = EXTERNAL_TYPE_SIZES[MOS_ATTR_TYPE_UINT64],
        .byte_size_internal = INTERNAL_TYPE_SIZES[MOS_ATTR_TYPE_INTERNAL_UINT64],
        .indexed = 1
    };
    mos_t_attr prop2 = {
        .name = "prop2",
        .type = MOS_ATTR_TYPE_INTERNAL_STRING_DESC,
        .field_offset_external = offsetof(TestEntry, prop2),
        .field_offset_internal = prop1.byte_size_internal,
        .byte_size_external = EXTERNAL_TYPE_SIZES[MOS_ATTR_TYPE_STRING],
        .byte_size_internal = INTERNAL_TYPE_SIZES[MOS_ATTR_TYPE_INTERNAL_STRING_DESC],
        .indexed = 0
    };
    test_config.attributes[0] = prop1;
    test_config.attributes[1] = prop2;

    mos_t_idx id_idx = {
        .id = 0,
        .index_offset = 0,
        .type = MOS_IDX_HASH_MAP,
        .index_size = 8192,     //padded header + padded values & verifiers
        .attribute_name = "id"
    };
    mos_t_idx prop1_idx = {
        .id = 1,
        .index_offset = 12288,     //padded index data header + padded header + padded values & verifiers
        .type = MOS_IDX_HASH_MAP,
        .index_size = 8192,     //padded header + padded values & verifiers
        .attribute_name = "prop1"
    };
    test_config.indexes[0] = id_idx;
    test_config.indexes[1] = prop1_idx;

    result1 = NULL;
    result2 = NULL;
    result3 = NULL;
    result4 = NULL;
    mmap_ptr = NULL;
}

void tearDown(void) {
    if(result1) {
        free(result1);
    }

    if(result2) {
        free(result2);
    }

    if(result3) {
        free(result3);
    }

    if(result4) {
        free(result4);
    }

    if(mmap_ptr) {
        free(mmap_ptr);
    }
}

mos_t_storage setup_test_storage(void* memory, mos_t_header* h) {
    mos_t_storage storage = {0};
    storage.mmap_ptr = memory;
    storage.storage_header = h;

    uint8_t* base = (uint8_t*)memory;

    storage.attributes = (mos_t_attr*)&base[h->layout.offset_attributes];
    memcpy(storage.attributes, test_config.attributes, h->attribute_count * sizeof(mos_t_attr));
    storage.indexes = (mos_t_idx*)(base + h->layout.offset_indexes);
    memcpy(storage.indexes, test_config.indexes, h->index_count * sizeof(mos_t_idx));

    storage.index_data = (mos_t_idx_data*)(base + h->layout.offset_index_data);
    storage.idx_id_data = (mos_t_idx_data*)(base + h->layout.offset_index_data);
    storage.entries = (mos_t_record*)(base + h->layout.offset_records);
    storage.valid_bitmap = (mos_t_qry_bmp*)(base + h->layout.offset_valid_bitmap);
    storage.ready_bitmap = (mos_t_qry_bmp*)(base + h->layout.offset_ready_bitmap);
    storage.string_silo_base = (void*)(base + h->layout.offset_string_silo);

    for (uint64_t i = 0; i < h->index_count; i++) {
        uint8_t* idx_page = ((uint8_t*)storage.index_data) + (i * 12288);     //i * (padded index data header + padded header + padded values & verifiers)
        mos_t_idx_data* idx_data = (mos_t_idx_data*)idx_page;
        idx_data->header.index_payload_offset = MOS_ALIGN_UP(sizeof(mos_t_idx_data_header), MOS_PAGE_SIZE);
        mos_t_idx_hmap* hm = (mos_t_idx_hmap*)(((uint8_t*)idx_data) + idx_data->header.index_payload_offset);
        hm->index_header.table_size = 8;
        hm->index_header.offset_values = MOS_PAGE_SIZE;
        hm->index_header.offset_verifiers = hm->index_header.offset_values + (hm->index_header.table_size * sizeof(uint64_t));
    }

    return storage;
}

void test_mos_storage_put__put_record(void) {
    //Arrange
    uint64_t file_size = 57344;
    uint64_t offset_records = 20480;
    uint64_t offset_id_idx = 24576;
    uint64_t offset_prop1_idx = offset_id_idx + 12288;     //padded index data header + padded header + padded values & verifiers
    uint64_t offset_string_silo = offset_prop1_idx + 12288;     //padded index data header + padded header + padded values & verifiers

    mmap_ptr = malloc(file_size);
    memset(mmap_ptr, 0, file_size);

    mos_t_header header = {
        .index_count = 2,
        .attribute_count = 2,
        .max_records = 4,
        .layout = {
            .header_size = 4096,
            .attributes_size = 4096,
            .indexes_size = 4096,
            .index_data_size = 24576,
            .valid_bitmap_size = 4096,
            .ready_bitmap_size = 4096,
            .string_silo_size = 4096,
            .record_size = 48,
            .record_data_size = 28,
            .offset_header = 0,
            .offset_attributes = 4096,
            .offset_indexes = 8192,
            .offset_valid_bitmap = 12288,
            .offset_ready_bitmap = 16384,
            .offset_records = offset_records,
            .offset_index_data = offset_id_idx,
            .offset_string_silo = offset_string_silo,
            .file_size = file_size
        },
        .state = {
            .last_deleted_row_id = MOS_NULL_OFFSET,
            .next_free_row_id = 0
        },
        .string_silo = {
            .base_offset = offset_string_silo,
            .current_offset = 0,
            .size = 4096,
            .last_deleted = {
                .str_offset = MOS_NULL_OFFSET
            }
        }
    };    
    mos_t_storage storage = setup_test_storage(mmap_ptr, &header);

    TestEntry entry = { .prop1 = 2 };
    entry.prop2.str = "entry1";
    entry.prop2.str_len = 6;

    // Act
    uint64_t id1 = 1;
    mos_storage_put(&storage, id1, &entry);

    result1 = (TestEntry*)mos_storage_get(&storage, id1); // Get first slot
    
    //Assert
    TEST_ASSERT_EQUAL_INT64(2, result1->prop1);
    TEST_ASSERT_EQUAL_STRING_LEN("entry1", result1->prop2.str, result1->prop2.str_len);
    TEST_ASSERT_EQUAL_INT(6, result1->prop2.str_len);
}

void test_mos_storage_put__put_records(void) {
    //Arrange
    uint64_t file_size = 57344;
    uint64_t offset_records = 20480;
    uint64_t offset_id_idx = 24576;
    uint64_t offset_prop1_idx = offset_id_idx + 12288;
    uint64_t offset_string_silo = offset_prop1_idx + 12288;

    mmap_ptr = malloc(file_size);
    memset(mmap_ptr, 0, file_size);

    mos_t_header header = {
        .index_count = 2,
        .attribute_count = 2,
        .max_records = 4,
        .layout = {
            .header_size = 4096,
            .attributes_size = 4096,
            .indexes_size = 4096,
            .index_data_size = 24576,
            .valid_bitmap_size = 4096,
            .ready_bitmap_size = 4096,
            .string_silo_size = 4096,
            .record_size = 48,
            .record_data_size = 28,
            .offset_header = 0,
            .offset_attributes = 4096,
            .offset_indexes = 8192,
            .offset_valid_bitmap = 12288,
            .offset_ready_bitmap = 16384,
            .offset_records = offset_records,
            .offset_index_data = offset_id_idx,
            .file_size = file_size
        },
        .state = {
            .last_deleted_row_id = MOS_NULL_OFFSET,
            .next_free_row_id = 0
        },
        .string_silo = {
            .base_offset = offset_string_silo,
            .current_offset = 0,
            .size = 4096,
            .last_deleted = {
                .str_offset = MOS_NULL_OFFSET
            }
        }
    };    
    mos_t_storage storage = setup_test_storage(mmap_ptr, &header);

    TestEntry entry = { .prop1 = 2 };
    entry.prop2.str = "entry1";
    entry.prop2.str_len = 6;
    TestEntry entry2 = { .prop1 = 20 };
    entry2.prop2.str = "entry11";
    entry2.prop2.str_len = 7;

    // Act
    uint64_t id1 = 1;
    uint64_t id11 = 11;
    mos_storage_put(&storage, id1, &entry);
    mos_storage_put(&storage, id11, &entry2);

    result1 = (TestEntry*)mos_storage_get(&storage, id1);
    result2 = (TestEntry*)mos_storage_get(&storage, id11);

    //Assert
    TEST_ASSERT_EQUAL_INT64(2, result1->prop1);
    TEST_ASSERT_EQUAL_STRING_LEN("entry1", result1->prop2.str, result1->prop2.str_len);
    TEST_ASSERT_EQUAL_INT(6, result1->prop2.str_len);

    TEST_ASSERT_EQUAL_INT64(20, result2->prop1);
    TEST_ASSERT_EQUAL_STRING_LEN("entry11", result2->prop2.str, result2->prop2.str_len);
    TEST_ASSERT_EQUAL_INT(7, result2->prop2.str_len);
}

void print_record(TestEntry* entry) {
    printf("Testentry Prop1: %" PRId64 "\n", entry->prop1);
    printf("Testentry Prop2: %s\n", entry->prop2.str);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_mos_storage_put__put_record);
    RUN_TEST(test_mos_storage_put__put_records);
    return UNITY_END();
}