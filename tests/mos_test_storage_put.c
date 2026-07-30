#include "unity.h"
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include "../include/mos.h"
#include "../include/mos_internal.h"
#include "../include/mos_idx_hmap.h"
#include "../include/mos_idx.h"
#include "../include/mos_utils.h"

typedef struct {
    uint64_t id;
    uint64_t prop1;
    mos_t_string prop2;
} TestEntry;

typedef struct {
    //keep attributes seperatly as otherwise the data is not available in test functions
    mos_t_attr_info attribute_info[3];
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
    mos_t_attr_info id = {
        .name = "id",
        .type = MOS_ATTR_TYPE_UINT64,
        .external_offset = offsetof(TestEntry, id)
    };
    mos_t_attr_info prop1 = {
        .name = "prop1",
        .type = MOS_ATTR_TYPE_UINT64,
        .external_offset = offsetof(TestEntry, prop1)
    };
    mos_t_attr_info prop2 = {
        .name = "prop2",
        .type = MOS_ATTR_TYPE_STRING,
        .external_offset = offsetof(TestEntry, prop2)
    };
    test_config.attribute_info[0] = id;
    test_config.attribute_info[1] = prop1;
    test_config.attribute_info[2] = prop2;

    //fake internal id_idx
    mos_t_idx id_idx = {
        .offset_file = 24576,
        .name = "id_idx",
        .type = MOS_IDX_HASH_MAP,
        .index_size = 4096
    };
    mos_t_idx prop1_idx = {
        .offset_file = 24576 + 4096,
        .name = "idx_prop1",
        .type = MOS_IDX_HASH_MAP,
        .index_size = 4096
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

    storage.attributes = (mos_t_attr_info*)&base[h->layout.offset_attributes];
    memcpy(storage.attributes, test_config.attribute_info, h->attribute_count * sizeof(mos_t_attr_info));
    storage.indexes = (mos_t_idx*)(base + h->layout.offset_indexes);
    memcpy(storage.indexes, test_config.indexes, h->index_count * sizeof(mos_t_idx));

    storage.index_data = (mos_t_idx_data*)(base + h->layout.offset_index_data);
    storage.idx_id_data = (mos_t_idx_data*)(base + h->layout.offset_index_data);
    storage.entries = (mos_t_record*)(base + h->layout.offset_records);
    storage.valid_bitmap = (mos_t_qry_bmp*)(base + h->layout.offset_valid_bitmap);
    storage.ready_bitmap = (mos_t_qry_bmp*)(base + h->layout.offset_ready_bitmap);
    storage.string_silo_base = (void*)(base + h->layout.offset_string_silo);

    for (int i = 0; i < h->index_count; i++) {
        uint8_t* idx_page = ((uint8_t*)storage.index_data) + (i * 4096);
        mos_t_idx_data* idx_data = (mos_t_idx_data*)idx_page;
        mos_t_idx_hmap* hm = (mos_t_idx_hmap*)idx_data->index_payload;
        hm->index_header.table_size = 8;
    }

    return storage;
}

void test_mos_storage_put__put_record(void) {
    //Arrange
    uint64_t file_size = 40960;
    uint64_t offset_records = 20480;
    uint64_t offset_id_idx = 24576;
    uint64_t offset_prop1_idx = offset_id_idx + 4096;
    uint64_t offset_string_silo = offset_prop1_idx + 4096;

    mmap_ptr = malloc(file_size);
    memset(mmap_ptr, 0, file_size);

    mos_t_header header = {
        .index_count = 2,
        .attribute_count = 3,
        .max_records = 4,
        .layout = {
            .header_size = 4096,
            .attributes_size = 4096,
            .indexes_size = 4096,
            .index_data_size = 8192,
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

    TestEntry entry = { .id = 1, .prop1 = 2 };
    entry.prop2.str = "entry1";
    entry.prop2.str_len = 6;

    // Act
    uint64_t id1 = 1;
    mos_storage_put(&storage, id1, &entry);

    result1 = (TestEntry*)mos_storage_get(&storage, id1); // Get first slot
    
    //Assert
    TEST_ASSERT_EQUAL_INT64(1, result1->id);
    TEST_ASSERT_EQUAL_INT64(2, result1->prop1);
    TEST_ASSERT_EQUAL_STRING_LEN("entry1", result1->prop2.str, result1->prop2.str_len);
    TEST_ASSERT_EQUAL_INT(6, result1->prop2.str_len);
}

void test_mos_storage_put__put_records(void) {
    //Arrange
    uint64_t file_size = 40960;
    uint64_t offset_records = 20480;
    uint64_t offset_id_idx = 24576;
    uint64_t offset_prop1_idx = offset_id_idx + 4096;
    uint64_t offset_string_silo = offset_prop1_idx + 4096;

    mmap_ptr = malloc(file_size);
    memset(mmap_ptr, 0, file_size);

    mos_t_header header = {
        .index_count = 2,
        .attribute_count = 3,
        .max_records = 4,
        .layout = {
            .header_size = 4096,
            .attributes_size = 4096,
            .indexes_size = 4096,
            .index_data_size = 8192,
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

    TestEntry entry = { .id = 1, .prop1 = 2 };
    entry.prop2.str = "entry1";
    entry.prop2.str_len = 6;
    TestEntry entry2 = { .id = 11, .prop1 = 20 };
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
    TEST_ASSERT_EQUAL_INT64(1, result1->id);
    TEST_ASSERT_EQUAL_INT64(2, result1->prop1);
    TEST_ASSERT_EQUAL_STRING_LEN("entry1", result1->prop2.str, result1->prop2.str_len);
    TEST_ASSERT_EQUAL_INT(6, result1->prop2.str_len);

    TEST_ASSERT_EQUAL_INT64(11, result2->id);
    TEST_ASSERT_EQUAL_INT64(20, result2->prop1);
    TEST_ASSERT_EQUAL_STRING_LEN("entry11", result2->prop2.str, result2->prop2.str_len);
    TEST_ASSERT_EQUAL_INT(7, result2->prop2.str_len);
}

void print_record(TestEntry* entry) {
    printf("Testentry Id: %" PRId64 "\n", entry->id);
    printf("Testentry Prop1: %" PRId64 "\n", entry->prop1);
    printf("Testentry Prop2: %s\n", entry->prop2.str);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_mos_storage_put__put_record);
    RUN_TEST(test_mos_storage_put__put_records);
    return UNITY_END();
}