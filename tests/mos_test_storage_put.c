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
    uint8_t pad[4];
} TestEntry;

typedef struct {
    //keep attributes seperatly as otherwise the data is not available in test functions
    mos_t_attr attributes[2];
    mos_t_idx_descriptor indexes[2];
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

    mos_t_idx_descriptor id_idx = {
        .id = 0,
        .index_region_pos = 0,
        .type = MOS_IDX_HASH_MAP,
        .index_size = 12288,     //padded header + padded values & verifiers
        .attribute_name = "id"
    };
    mos_t_idx_descriptor prop1_idx = {
        .id = 1,
        .index_region_pos = 1,
        .type = MOS_IDX_HASH_MAP,
        .index_size = 12288,     //padded header + padded values & verifiers
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
    storage.index_regions = calloc(1, 2 * sizeof(mos_t_mapped_region));

    mos_t_mapped_region* header_region = &storage.header_region;
    mos_t_mapped_region* valid_bitmap_region = &storage.valid_bitmap_region;
    mos_t_mapped_region* ready_bitmap_region = &storage.ready_bitmap_region;
    mos_t_mapped_region* records_region = &storage.records_region;
    mos_t_mapped_region* string_silo_region = &storage.string_silo_region;

    uint8_t* base = (uint8_t*)memory;

    uint64_t offset = 0;
    header_region->region_base = (mos_t_idx_data*)(base);
    header_region->region_byte_size = h->layout.header_size;
    offset += header_region->region_byte_size;

    mos_t_header* header = mos_accessor_header(header_region);
    memcpy(header, h, sizeof(*header));

    valid_bitmap_region->region_base = (mos_t_idx_data*)(base + offset);
    valid_bitmap_region->region_byte_size = h->layout.valid_bitmap_size;
    offset += valid_bitmap_region->region_byte_size;

    ready_bitmap_region->region_base = (mos_t_idx_data*)(base + offset);
    ready_bitmap_region->region_byte_size = h->layout.ready_bitmap_size;
    offset += ready_bitmap_region->region_byte_size;

    records_region->region_base = (mos_t_idx_data*)(base + offset);
    records_region->region_byte_size = h->layout.records_size;
    offset += records_region->region_byte_size;

    string_silo_region->region_base = (mos_t_idx_data*)(base + offset);
    string_silo_region->region_byte_size = h->layout.string_silo_size;
    offset += string_silo_region->region_byte_size;

    storage.index_regions->region_base = (mos_t_idx_data*)(base + offset);
    storage.index_regions->region_byte_size = h->layout.string_silo_size;
    offset += storage.index_regions->region_byte_size;

    mos_t_idx_descriptor* mmap_index_descriptors = mos_accessor_header_index_descriptors(header_region);
    for (uint64_t i = 0; i < h->index_count; i++) {
        mos_t_idx_descriptor* index_desc = &mmap_index_descriptors[i];
        mos_t_mapped_region* index_region = &storage.index_regions[i];
        index_region->region_base = (mos_t_idx_data*)(base + offset);
        index_region->region_byte_size = index_desc->index_size;
        offset += index_region->region_byte_size;

        mos_t_idx_data* idx_data = (mos_t_idx_data*)index_region->region_base;
        idx_data->header.index_payload_offset = MOS_ALIGN_UP(sizeof(mos_t_idx_data_header), MOS_PAGE_SIZE);
        mos_t_idx_hmap* hm = (mos_t_idx_hmap*)(((uint8_t*)idx_data) + idx_data->header.index_payload_offset);
        hm->index_header.table_size = 8;
        hm->index_header.offset_values = MOS_PAGE_SIZE;
        hm->index_header.offset_verifiers = hm->index_header.offset_values + (hm->index_header.table_size * sizeof(uint64_t));
    }

    mos_t_attr* mmap_attributes = mos_accessor_header_attributes(header_region);
    memcpy(mmap_attributes, test_config.attributes, h->attribute_count * sizeof(mos_t_attr));
    memcpy(mmap_index_descriptors, test_config.indexes, h->index_count * sizeof(mos_t_idx_descriptor));

    return storage;
}

void test_mos_storage_put__put_record(void) {
    //Arrange
    uint64_t file_size = 57344;

    mmap_ptr = malloc(file_size);
    memset(mmap_ptr, 0, file_size);

    mos_t_header header = {
        .index_count = 2,
        .attribute_count = 2,
        .max_records = 4,
        .attributes_offset = 4096,
        .index_descriptors_offset = 8192,
        .layout = {
            .header_size = 12288,
            .index_data_size = 24576,
            .valid_bitmap_size = 4096,
            .ready_bitmap_size = 4096,
            .string_silo_size = 4096,
            .records_size = 4096,
            .record_size = 48,
            .record_data_size = 28,
            .record_data_size_external = sizeof(TestEntry)
        },
        .state = {
            .last_deleted_row_id = MOS_NULL_OFFSET,
            .next_free_row_id = 0
        },
        .string_silo = {
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
    TEST_ASSERT_NOT_NULL(result1);
    TEST_ASSERT_EQUAL_INT64(2, result1->prop1);
    TEST_ASSERT_EQUAL_STRING_LEN("entry1", result1->prop2.str, result1->prop2.str_len);
    TEST_ASSERT_EQUAL_INT(6, result1->prop2.str_len);

    free(storage.index_regions);
}

void test_mos_storage_put__put_records(void) {
    //Arrange
    uint64_t file_size = 57344;

    mmap_ptr = malloc(file_size);
    memset(mmap_ptr, 0, file_size);

    mos_t_header header = {
        .index_count = 2,
        .attribute_count = 2,
        .max_records = 4,
        .attributes_offset = 4096,
        .index_descriptors_offset = 8192,
        .layout = {
            .header_size = 12288,
            .index_data_size = 24576,
            .valid_bitmap_size = 4096,
            .ready_bitmap_size = 4096,
            .string_silo_size = 4096,
            .records_size = 4096,
            .record_size = 48,
            .record_data_size = 28,
            .record_data_size_external = sizeof(TestEntry)
        },
        .state = {
            .last_deleted_row_id = MOS_NULL_OFFSET,
            .next_free_row_id = 0
        },
        .string_silo = {
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
    TEST_ASSERT_NOT_NULL(result1);
    TEST_ASSERT_EQUAL_INT64(2, result1->prop1);
    TEST_ASSERT_EQUAL_STRING_LEN("entry1", result1->prop2.str, result1->prop2.str_len);
    TEST_ASSERT_EQUAL_INT(6, result1->prop2.str_len);

    TEST_ASSERT_NOT_NULL(result2);
    TEST_ASSERT_EQUAL_INT64(20, result2->prop1);
    TEST_ASSERT_EQUAL_STRING_LEN("entry11", result2->prop2.str, result2->prop2.str_len);
    TEST_ASSERT_EQUAL_INT(7, result2->prop2.str_len);

    free(storage.index_regions);
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