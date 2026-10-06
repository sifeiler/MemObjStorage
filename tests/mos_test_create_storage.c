#include "unity.h"
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <inttypes.h>
#include <string.h>

#include "../include/mos.h"
#include "../include/mos_internal.h"
#include "../include/mos_idx.h"
#include "../include/mos_utils.h"
#include "../include/mos_math.h"

typedef struct {
    uint64_t prop1;
    uint64_t prop2;
} TestEntry;

typedef struct {
    mos_t_storage_config config;
    mos_t_storage* storage;
    char dir_path[256];
} CreateTestConfig;

static CreateTestConfig test_config = {0};

void setUp(void) {
    test_config.config.attribute_count = 2;
    test_config.config.index_count = 1;
    test_config.config.max_records = 100;
    test_config.config.padded_record_byte_size = sizeof(TestEntry);

    test_config.config.attributes = calloc(1, sizeof(mos_t_attr) * test_config.config.attribute_count);
    strcpy(test_config.config.attributes[0].name, "prop1");
    test_config.config.attributes[0].type = MOS_ATTR_TYPE_UINT64;
    test_config.config.attributes[0].byte_size = 8;
    test_config.config.attributes[0].field_offset = offsetof(TestEntry, prop1);
    test_config.config.attributes[0].indexed = 1;

    strcpy(test_config.config.attributes[1].name, "prop2");
    test_config.config.attributes[1].type = MOS_ATTR_TYPE_UINT64;
    test_config.config.attributes[1].byte_size = 8;
    test_config.config.attributes[1].field_offset = offsetof(TestEntry, prop2);
    test_config.config.attributes[1].indexed = 0;

    test_config.config.indexes = calloc(1, sizeof(mos_t_idx_descriptor) * test_config.config.index_count);
    strcpy(test_config.config.indexes[0].attribute_name, "prop1");
    test_config.config.indexes[0].type = MOS_IDX_HASH_MAP;
    mos_os_directory_create("tests/tmp");
}

void tearDown(void) {
}

void after_test(mos_t_storage* storage) {
    if(storage != NULL) {
        mos_free_storage(storage);
        storage = NULL;
    }

    if(test_config.config.attributes != NULL) {
        free(test_config.config.attributes);
    }

    if(test_config.config.indexes != NULL) {
        free(test_config.config.indexes);
    }

    // reset static struct to start clean for next test
    memset(&test_config, 0, sizeof(test_config));
}

void mos_test_mos_init_layout__layout_correct(void) {
    //Arrange
    mos_t_layout layout = {0};

    //values in bytes
    uint64_t exp_header_size = 12288;
    uint64_t exp_valid_bitmap_size = 4096;
    uint64_t exp_ready_bitmap_size = 4096;
    uint64_t exp_record_size = MOS_ALIGN_UP(33, 8);
    uint64_t exp_record_data_size = 16;
    uint64_t exp_records_size = MOS_ALIGN_UP(100 * exp_record_size, 4096);
    //no string attributes, but silo will be at least of size MOS_PAGE_SIZE
    uint64_t exp_string_silo_size = 4096;

    //Act
    mos_t_config* internal_config = mos_init_internal_config(&test_config.config);
    mos_init_layout(internal_config, &layout);

    //Assert
    TEST_ASSERT_EQUAL(exp_header_size, layout.header_size);
    TEST_ASSERT_EQUAL(exp_valid_bitmap_size, layout.valid_bitmap_size);
    TEST_ASSERT_EQUAL(exp_ready_bitmap_size, layout.ready_bitmap_size);
    TEST_ASSERT_EQUAL(exp_record_size, layout.record_size);
    TEST_ASSERT_EQUAL(exp_record_data_size, layout.record_data_size);
    TEST_ASSERT_EQUAL(exp_records_size, layout.records_size);
    TEST_ASSERT_EQUAL(exp_string_silo_size, layout.string_silo_size);

    free(internal_config->attributes);
    free(internal_config->indexes);
    free(internal_config);
    after_test(test_config.storage);
}

void mos_test_assert_file_size(uint64_t expected_file_size, int fd) {
    struct stat st;
    if (fstat(fd, &st) == 0) {
        TEST_ASSERT_EQUAL(expected_file_size, st.st_size);
    } else {
        TEST_FAIL_MESSAGE("fstat failed! Cannot check for correct file size.");
    }
}

void mos_test_mos_create_storage__file_size(void) {
    //Arrange
    strcpy(test_config.dir_path, "tests/tmp/mos_test_mos_create_storage__file_size");
    strcpy(test_config.config.storage_path, "tests/tmp/mos_test_mos_create_storage__file_size");
    mos_os_directory_create("tests/tmp/mos_test_mos_create_storage__file_size");

    //Act
    test_config.storage = mos_create_storage(test_config.dir_path, &test_config.config);

    //Assert
    mos_t_storage* storage = test_config.storage;
    mos_t_header* mmap_header = mos_accessor_header(&storage->header_region);

    mos_test_assert_file_size(mmap_header->layout.header_size, storage->header_region.fd);
    mos_test_assert_file_size(mmap_header->layout.valid_bitmap_size, storage->valid_bitmap_region.fd);
    mos_test_assert_file_size(mmap_header->layout.ready_bitmap_size, storage->ready_bitmap_region.fd);
    mos_test_assert_file_size(mmap_header->layout.records_size, storage->records_region.fd);
    mos_test_assert_file_size(MOS_PAGE_SIZE, storage->arena_region.fd);

    //no strings, so region was mapped to a single page
    mos_test_assert_file_size(MOS_PAGE_SIZE, storage->string_silo_region.fd);

    mos_t_idx_descriptor* mmap_index_descriptor = mos_accessor_header_index_descriptors(&test_config.storage->header_region);
    for(uint64_t i = 0; i < mmap_header->index_count; i++) {
        mos_t_idx_descriptor* idx_desc = &mmap_index_descriptor[i];
        mos_t_mapped_region*  idx_region = &storage->index_regions[i];

        mos_test_assert_file_size(idx_desc->index_size, idx_region->fd);
        TEST_ASSERT_EQUAL(idx_region->region_byte_size, idx_desc->index_size);
    }

    TEST_ASSERT_EQUAL(storage->header_region.region_byte_size, mmap_header->layout.header_size);
    TEST_ASSERT_EQUAL(storage->valid_bitmap_region.region_byte_size, mmap_header->layout.valid_bitmap_size);
    TEST_ASSERT_EQUAL(storage->ready_bitmap_region.region_byte_size, mmap_header->layout.ready_bitmap_size);
    TEST_ASSERT_EQUAL(storage->records_region.region_byte_size, mmap_header->layout.records_size);
    TEST_ASSERT_EQUAL(storage->string_silo_region.region_byte_size, mmap_header->layout.string_silo_size);

    after_test(test_config.storage);
}

void mos_test_mos_create_storage__check_header_area(void) {
    //Arrange
    strcpy(test_config.dir_path, "tests/tmp/mos_test_mos_create_storage__check_header_area");
    strcpy(test_config.config.storage_path, "tests/tmp/mos_test_mos_create_storage__check_header_area");
    mos_os_directory_create("tests/tmp/mos_test_mos_create_storage__check_header_area");

    uint8_t header[4096] = {0};
    mos_t_header expected_header = {
        .identifier = MOS_FILE_ID,
        .attribute_count = 2,
        .index_count = 2,
        .max_records = 100,
        .attributes_offset = 4096,
        .index_descriptors_offset = 8192,
        .layout = {
            .header_size = 12288,
            .valid_bitmap_size = 4096,
            .ready_bitmap_size = 4096,
            .record_size = 40,
            .record_data_size = 16,
            .record_data_size_external = sizeof(TestEntry),
            .records_size = 4096,
            .string_silo_size = 4096
        },
        .state = {
            .last_deleted_row_id = MOS_NULL_OFFSET,
            .next_free_row_id = 0
        },
        .string_silo = {
            .size = 4096,
            .current_offset = 0,
            .last_deleted = {
                .str_offset = MOS_NULL_OFFSET,
                .str_len = 0
            }
        }
    };
    //copy expected header to the front of the array and zero out the padding
    memcpy(header, &expected_header, sizeof(mos_t_header));

    //Act
    test_config.storage = mos_create_storage(test_config.dir_path, &test_config.config);

    //Assert
    mos_t_header* mmap_header = mos_accessor_header(&test_config.storage->header_region);
    mos_t_layout layout = mmap_header->layout;
    TEST_ASSERT_EQUAL_MEMORY((mos_t_header*)header, mmap_header, 4096);

    after_test(test_config.storage);
}

void mos_test_mos_create_storage__check_attribute_area(void) {
    //Arrange
    strcpy(test_config.dir_path, "tests/tmp/mos_test_mos_create_storage__check_attribute_area");
    strcpy(test_config.config.storage_path, "tests/tmp/mos_test_mos_create_storage__check_attribute_area");
    mos_os_directory_create("tests/tmp/mos_test_mos_create_storage__check_attribute_area");
    mos_t_attr expected_attributes[2] = {0};
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
        .type = MOS_ATTR_TYPE_INTERNAL_UINT64,
        .field_offset_external = offsetof(TestEntry, prop2),
        .field_offset_internal = prop1.byte_size_internal,
        .byte_size_external = EXTERNAL_TYPE_SIZES[MOS_ATTR_TYPE_UINT64],
        .byte_size_internal = INTERNAL_TYPE_SIZES[MOS_ATTR_TYPE_INTERNAL_UINT64],
        .indexed = 0
    };
    expected_attributes[0] = prop1;
    expected_attributes[1] = prop2;

    //Act
    test_config.storage = mos_create_storage(test_config.dir_path, &test_config.config);

    //Assert
    mos_t_attr* mmap_attributes = mos_accessor_header_attributes(&test_config.storage->header_region);
    size_t expected_total_size = test_config.config.attribute_count * sizeof(mos_t_attr);
    //TODO: how to check the padding too?
    TEST_ASSERT_EQUAL_MEMORY(expected_attributes, mmap_attributes, expected_total_size);

    after_test(test_config.storage);
}

void print_storage_index(const mos_t_idx_descriptor* idx) {
    if (idx == NULL) {
        printf("idx: NULL\n");
        return;
    }

    printf("--- mos_t_idx Instance ---\n");
    printf("Id:        %d\n", idx->id);
    printf("Type:        %d\n", idx->type);
    printf("Index Size:  %zu bytes\n", (size_t)idx->index_size);
    printf("Index Region Pos: 0x%08lX\n", (unsigned long)idx->index_region_pos);
    printf("Attr Name: %s\n", idx->attribute_name);
    printf("---------------------------\n");
}

void mos_test_mos_create_storage__check_index_area(void) {
    //Arrange
    strcpy(test_config.dir_path, "tests/tmp/mos_test_mos_create_storage__check_index_area");
    strcpy(test_config.config.storage_path, "tests/tmp/mos_test_mos_create_storage__check_index_area");
    mos_os_directory_create("tests/tmp/mos_test_mos_create_storage__check_index_area");

    mos_t_idx_descriptor expected_indexes[2] = {0};

    //Act
    test_config.storage = mos_create_storage(test_config.dir_path, &test_config.config);

    //Assert
    mos_t_idx_descriptor id_index = {
        .id = 0,
        .type = MOS_IDX_HASH_MAP,
        .index_size = 4096 * 6,     // page-padded index data header + page-padded hmap header + page-padded values & verifiers
        .attribute_name = "id",
        .index_region_pos = 0,
        .params = {0}
    };
    mos_t_idx_descriptor prop1_idx = {
        .id = 1,
        .type = MOS_IDX_HASH_MAP,
        .index_size = 4096 * 6,     // page-padded index data header + page-padded hmap header + page-padded values & verifiers
        .attribute_name = "prop1",
        .index_region_pos = 1,
        .params = {0}
    };
    expected_indexes[0] = id_index;
    expected_indexes[1] = prop1_idx;

    mos_t_idx_descriptor* mmap_index_descriptor = mos_accessor_header_index_descriptors(&test_config.storage->header_region);
    //TODO: how to check the padding too?
    TEST_ASSERT_EQUAL_MEMORY(expected_indexes, mmap_index_descriptor, sizeof(expected_indexes));

    after_test(test_config.storage);
}

void mos_test_assert_regions(mos_t_mapped_region* region, const char* expected_file_name) {
    TEST_ASSERT_NOT_NULL(region->region_base);
    TEST_ASSERT_NOT_NULL(region->region_file_path);
    TEST_ASSERT_TRUE(mos_str_ends_with(region->region_file_path, expected_file_name));
}

void mos_test_mos_create_storage__check_regions(void) {
    //Arrange
    strcpy(test_config.dir_path, "tests/tmp/mos_test_mos_create_storage__check_storage_ptrs");
    strcpy(test_config.config.storage_path, "tests/tmp/mos_test_mos_create_storage__check_storage_ptrs");
    mos_os_directory_create("tests/tmp/mos_test_mos_create_storage__check_storage_ptrs");

    //Act
    test_config.storage = mos_create_storage(test_config.dir_path, &test_config.config);

    //Assert
    mos_t_header* mmap_header = mos_accessor_header(&test_config.storage->header_region);
    TEST_ASSERT_NOT_NULL(test_config.storage->index_regions);
    mos_test_assert_regions(&test_config.storage->header_region, "mos_header.mos");
    mos_test_assert_regions(&test_config.storage->valid_bitmap_region, "mos_valid_bitmap.mos");
    mos_test_assert_regions(&test_config.storage->ready_bitmap_region, "mos_ready_bitmap.mos");
    mos_test_assert_regions(&test_config.storage->records_region, "mos_records.mos");
    mos_test_assert_regions(&test_config.storage->string_silo_region, "mos_string_silo.mos");

    for(uint64_t i = 0; i < mmap_header->index_count; i++) {
        char expected_file_name[64];
        snprintf(expected_file_name, sizeof(expected_file_name), "mos_index_%u.mos", (unsigned)i);
        mos_test_assert_regions(&test_config.storage->index_regions[i], expected_file_name);
    }

    after_test(test_config.storage);
}

void mos_test_mos_create_storage__check_record_area_empty(void) {
    //Arrange
    strcpy(test_config.dir_path, "tests/tmp/mos_test_mos_create_storage__check_record_area_empty");
    strcpy(test_config.config.storage_path, "tests/tmp/mos_test_mos_create_storage__check_record_area_empty");
    mos_os_directory_create("tests/tmp/mos_test_mos_create_storage__check_record_area_empty");

    //Act
    test_config.storage = mos_create_storage(test_config.dir_path, &test_config.config);

    //Assert
    mos_t_header* mmap_header = mos_accessor_header(&test_config.storage->header_region);
    mos_t_layout layout = mmap_header->layout;
    uint8_t expected_zeros[layout.record_data_size];
    memset(expected_zeros, 0, layout.record_data_size);

    mos_t_record* records_ptr = test_config.storage->records_region.region_base;
    TEST_ASSERT_EQUAL_MEMORY(expected_zeros, records_ptr, layout.record_data_size);

    after_test(test_config.storage);
}

void mos_test_mos_create_storage__check_valid_bitmap_area_empty(void) {
    //Arrange
    strcpy(test_config.dir_path, "tests/tmp/mos_test_mos_create_storage__check_valid_bitmap_area_empty");
    strcpy(test_config.config.storage_path, "tests/tmp/mos_test_mos_create_storage__check_valid_bitmap_area_empty");
    mos_os_directory_create("tests/tmp/mos_test_mos_create_storage__check_valid_bitmap_area_empty");

    //Act
    test_config.storage = mos_create_storage(test_config.dir_path, &test_config.config);

    //Assert
    mos_t_header* mmap_header = mos_accessor_header(&test_config.storage->header_region);
    mos_t_layout layout = mmap_header->layout;
    uint8_t expected_zeros[layout.valid_bitmap_size];
    memset(expected_zeros, 0, layout.valid_bitmap_size);

    mos_t_qry_bmp* valid_bitmap_ptr = test_config.storage->valid_bitmap_region.region_base;
    TEST_ASSERT_EQUAL_MEMORY(expected_zeros, valid_bitmap_ptr, layout.valid_bitmap_size);

    after_test(test_config.storage);
}

void mos_test_mos_create_storage__check_ready_bitmap_area_empty(void) {
    //Arrange
    strcpy(test_config.dir_path, "tests/tmp/mos_test_mos_create_storage__check_ready_bitmap_area_empty");
    strcpy(test_config.config.storage_path, "tests/tmp/mos_test_mos_create_storage__check_ready_bitmap_area_empty");
    mos_os_directory_create("tests/tmp/mos_test_mos_create_storage__check_ready_bitmap_area_empty");

    //Act
    test_config.storage = mos_create_storage(test_config.dir_path, &test_config.config);

    //Assert
    mos_t_header* mmap_header = mos_accessor_header(&test_config.storage->header_region);
    mos_t_layout layout = mmap_header->layout;
    uint8_t expected_zeros[layout.ready_bitmap_size];
    memset(expected_zeros, 0, layout.ready_bitmap_size);

    mos_t_qry_bmp* ready_bitmap_ptr = test_config.storage->ready_bitmap_region.region_base;
    TEST_ASSERT_EQUAL_MEMORY(expected_zeros, ready_bitmap_ptr, layout.ready_bitmap_size);

    after_test(test_config.storage);
}

void mos_test_mos_load_storage(void) {
    //Arrange
    strcpy(test_config.dir_path, "tests/tmp/mos_test_mos_load_storage");
    strcpy(test_config.config.storage_path, "tests/tmp/mos_test_mos_load_storage");
    mos_os_directory_create("tests/tmp/mos_test_mos_load_storage");

    test_config.storage = mos_create_storage(test_config.dir_path, &test_config.config);
    
    mos_t_mapped_region* expected_header_region = &test_config.storage->header_region;
    mos_t_mapped_region* expected_valid_bitmap_region = &test_config.storage->valid_bitmap_region;
    mos_t_mapped_region* expected_ready_bitmap_region = &test_config.storage->ready_bitmap_region;
    mos_t_mapped_region* expected_records_region = &test_config.storage->records_region;
    mos_t_mapped_region* expected_string_silo_region = &test_config.storage->string_silo_region;
    mos_t_mapped_region* expected_indexes_regions = test_config.storage->index_regions;

    mos_t_header* expected_mmap_header = mos_accessor_header(&test_config.storage->header_region);

    //Act
    mos_t_storage* loaded_storage = mos_load_storage("tests/tmp/mos_test_mos_load_storage");

    mos_t_mapped_region* loaded_header_region = &loaded_storage->header_region;
    mos_t_mapped_region* loaded_valid_bitmap_region = &loaded_storage->valid_bitmap_region;
    mos_t_mapped_region* loaded_ready_bitmap_region = &loaded_storage->ready_bitmap_region;
    mos_t_mapped_region* loaded_records_region = &loaded_storage->records_region;
    mos_t_mapped_region* loaded_string_silo_region = &loaded_storage->string_silo_region;
    mos_t_mapped_region* loaded_indexes_regions = loaded_storage->index_regions;

    mos_t_header* loaded_mmap_header = mos_accessor_header(&loaded_storage->header_region);

    //Assert
    TEST_ASSERT_NOT_NULL(loaded_storage);
    TEST_ASSERT_EQUAL(MOS_FILE_ID, expected_mmap_header->identifier);
    TEST_ASSERT_EQUAL(MOS_FILE_ID, loaded_mmap_header->identifier);

    TEST_ASSERT_EQUAL_MEMORY(expected_header_region->region_base, loaded_header_region->region_base, expected_header_region->region_byte_size);
    TEST_ASSERT_EQUAL_MEMORY(expected_valid_bitmap_region->region_base, loaded_valid_bitmap_region->region_base, expected_valid_bitmap_region->region_byte_size);
    TEST_ASSERT_EQUAL_MEMORY(expected_ready_bitmap_region->region_base, loaded_ready_bitmap_region->region_base, expected_ready_bitmap_region->region_byte_size);
    TEST_ASSERT_EQUAL_MEMORY(expected_records_region->region_base, loaded_records_region->region_base, expected_records_region->region_byte_size);
    TEST_ASSERT_EQUAL_MEMORY(expected_string_silo_region->region_base, loaded_string_silo_region->region_base, expected_string_silo_region->region_byte_size);

    for(uint64_t i = 0; i < expected_mmap_header->index_count; i++) {
        TEST_ASSERT_EQUAL_MEMORY(expected_indexes_regions[i].region_base, loaded_indexes_regions[i].region_base, expected_indexes_regions[i].region_byte_size);
    }
    
    after_test(test_config.storage);
    mos_free_storage(loaded_storage);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(mos_test_mos_init_layout__layout_correct);
    RUN_TEST(mos_test_mos_create_storage__file_size);
    RUN_TEST(mos_test_mos_create_storage__check_header_area);
    RUN_TEST(mos_test_mos_create_storage__check_attribute_area);
    RUN_TEST(mos_test_mos_create_storage__check_index_area);
    RUN_TEST(mos_test_mos_create_storage__check_regions);
    RUN_TEST(mos_test_mos_create_storage__check_record_area_empty);
    RUN_TEST(mos_test_mos_create_storage__check_valid_bitmap_area_empty);
    RUN_TEST(mos_test_mos_create_storage__check_ready_bitmap_area_empty);
    RUN_TEST(mos_test_mos_load_storage);
    return UNITY_END();
}