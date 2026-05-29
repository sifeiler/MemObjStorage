#include "unity.h"
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <inttypes.h>

#include "../include/mos.h"
#include "../include/mos_internal.h"
#include "../include/mos_idx.h"

typedef struct {
    uint64_t prop1;
    uint64_t prop2;
} TestEntry;

typedef struct {
    mos_t_config config;
    //keep attributes seperatly as otherwise the data is not available in test functions
    mos_t_attr_info attribute_info[3];
    mos_t_idx indexes[1];
    mos_t_storage* storage;
    char file_name[256];
} CreateTestConfig;

static CreateTestConfig test_config = {0};

void setUp(void) {
    mos_t_config config = {
        .index_count = 1,
        .attribute_count = 2,
        .max_records = 100
    };
    test_config.config = config;

    mos_t_attr_info prop1 = {
        .name = "prop1",
        .type = MOS_ATTR_TYPE_UINT64,
        .byte_size = 8,
        .external_offset = offsetof(TestEntry, prop1)
    };
    mos_t_attr_info prop2 = {
        .name = "prop2",
        .type = MOS_ATTR_TYPE_UINT64,
        .byte_size = 8,
        .external_offset = offsetof(TestEntry, prop2)
    };
    test_config.attribute_info[0] = prop1;
    test_config.attribute_info[1] = prop2;

    mos_t_idx prop1_idx = {
        .name = "idx_prop1",
        .type = MOS_IDX_HASH_MAP,
        .attribute = {
            .name = "prop1",
            .type = MOS_ATTR_TYPE_UINT64,
            .byte_size = 8,
            .external_offset = offsetof(TestEntry, prop1)
        },
        .index_size = 4096
    };
    test_config.indexes[0] = prop1_idx;

    //point test_config to global static array and not to stack (avoid dangling pointer)
    test_config.config.attributes = test_config.attribute_info;
    test_config.config.indexes = test_config.indexes;
}

void tearDown(void) {
}

void after_test() {
    if(test_config.storage != NULL) {
        mos_free_storage(test_config.storage);
        test_config.storage = NULL;
    }

    // reset static struct to start clean for next test
    memset(&test_config, 0, sizeof(test_config));
}

void mos_test_mos_init_layout__layout_correct(void) {
    //Arrange
    mos_t_layout layout = {0};

    //values in bytes
    uint64_t exp_header_size = 4096;
    uint64_t exp_attributes_size = 4096;
    uint64_t exp_indexes_size = 4096;
    uint64_t exp_valid_bitmap_size = 4096;
    uint64_t exp_ready_bitmap_size = 4096;
    uint64_t exp_record_size = MOS_ALIGN_UP(33, 8);
    uint64_t exp_record_data_size = 16;
    uint64_t exp_records_size = MOS_ALIGN_UP(100 * exp_record_size, 4096);
    uint64_t exp_index_data_size = 2 * 8192;
    //no string attributes, so string_silo is of size 0
    uint64_t exp_string_silo_size = 0;
    uint64_t exp_file_size = 40960;

    //Act
    mos_t_config* internal_config = mos_init_internal_config(&test_config.config);
    mos_init_layout(internal_config, &layout);

    //Assert
    TEST_ASSERT_EQUAL(exp_header_size, layout.header_size);
    TEST_ASSERT_EQUAL(exp_attributes_size, layout.attributes_size);
    TEST_ASSERT_EQUAL(exp_indexes_size, layout.indexes_size);
    TEST_ASSERT_EQUAL(exp_ready_bitmap_size, layout.valid_bitmap_size);
    TEST_ASSERT_EQUAL(exp_ready_bitmap_size, layout.ready_bitmap_size);
    TEST_ASSERT_EQUAL(exp_record_size, layout.record_size);
    TEST_ASSERT_EQUAL(exp_record_data_size, layout.record_data_size);
    TEST_ASSERT_EQUAL(exp_records_size, layout.records_size);
    TEST_ASSERT_EQUAL(exp_index_data_size, layout.index_data_size);
    TEST_ASSERT_EQUAL(exp_string_silo_size, layout.string_silo_size);
    TEST_ASSERT_EQUAL(exp_file_size, layout.file_size);

    uint64_t expected_offset = 0;
    TEST_ASSERT_EQUAL(expected_offset, layout.offset_header);

    expected_offset += exp_header_size;
    TEST_ASSERT_EQUAL(expected_offset, layout.offset_attributes);

    expected_offset += exp_attributes_size;
    TEST_ASSERT_EQUAL(expected_offset, layout.offset_indexes);

    expected_offset += exp_indexes_size;
    TEST_ASSERT_EQUAL(expected_offset, layout.offset_valid_bitmap);

    expected_offset += exp_valid_bitmap_size;
    TEST_ASSERT_EQUAL(expected_offset, layout.offset_ready_bitmap);

    expected_offset += exp_ready_bitmap_size;
    TEST_ASSERT_EQUAL(expected_offset, layout.offset_records);

    expected_offset += exp_records_size;
    TEST_ASSERT_EQUAL(expected_offset, layout.offset_index_data);

    expected_offset += exp_index_data_size;
    expected_offset += exp_string_silo_size;
    TEST_ASSERT_EQUAL(expected_offset, layout.file_size);
    TEST_ASSERT_EQUAL(expected_offset, exp_file_size);

    after_test();
}

void mos_test_mos_create_storage__file_size(void) {
    //Arrange
    strcpy(test_config.file_name, "tests/mos_test_mos_create_storage__file_size.db");

    //Act
    test_config.storage = mos_create_storage(test_config.file_name, &test_config.config);

    //Assert
    mos_t_storage* storage = test_config.storage;
    TEST_ASSERT_NOT_NULL(&storage->fd);

    struct stat st;
    if (fstat(storage->fd, &st) == 0) {
        TEST_ASSERT_EQUAL(st.st_size, storage->storage_header->layout.file_size);
    } else {
        TEST_FAIL_MESSAGE("fstat failed! Cannot check for correct file size.");
    }

    after_test();
}

void mos_test_mos_create_storage__check_header_area(void) {
    //Arrange
    strcpy(test_config.file_name, "tests/mos_test_mos_create_storage__check_header_area.db");
    uint8_t header[4096] = {0};
    mos_t_header expected_header = {
        .identifier = MOS_FILE_ID,
        .attribute_count = 2,
        .index_count = 2,
        .max_records = 100,
        .layout = {
            .header_size = 4096,
            .attributes_size = 4096,
            .indexes_size = 4096,
            .valid_bitmap_size = 4096,
            .ready_bitmap_size = 4096,
            .record_size = 40,
            .record_data_size = 16,
            .records_size = 4096,
            .index_data_size = 16384,
            .string_silo_size = 0,
            .file_size = 40960,
            .offset_header = 0,
            .offset_attributes = 4096,
            .offset_indexes = 8192,
            .offset_valid_bitmap = 12288,
            .offset_ready_bitmap = 16384,
            .offset_records = 20480,
            .offset_index_data = 24576
        },
        .state = {
            .last_deleted_string.str_offset = MOS_NULL_OFFSET,
            .current_string_offset = 0,
            .last_deleted_row_id = MOS_NULL_OFFSET,
            .next_free_row_id = 0
        }
    };
    //copy expected header to the front of the array and zero out the padding
    memcpy(header, &expected_header, sizeof(mos_t_header));

    //Act
    test_config.storage = mos_create_storage(test_config.file_name, &test_config.config);

    //Assert
    mos_t_layout layout = test_config.storage->storage_header->layout;
    mos_t_header* actual_header = MOS_GET_PTR(test_config.storage->mmap_ptr, layout.offset_header);
    TEST_ASSERT_EQUAL_MEMORY((mos_t_header*)header, actual_header, layout.header_size);

    after_test();
}

void mos_test_mos_create_storage__check_attribute_area(void) {
    //Arrange
    strcpy(test_config.file_name, "tests/mos_test_mos_create_storage__check_attribute_area.db");

    //Act
    test_config.storage = mos_create_storage(test_config.file_name, &test_config.config);

    //Assert
    mos_t_layout layout = test_config.storage->storage_header->layout;
    mos_t_attr_info* actual_attributes = MOS_GET_PTR(test_config.storage->mmap_ptr, layout.offset_attributes);
    size_t expected_total_size = test_config.config.attribute_count * sizeof(mos_t_attr_info);
    //TODO: how to check the padding too?
    TEST_ASSERT_EQUAL_MEMORY(actual_attributes, test_config.attribute_info, expected_total_size);

    after_test();
}

void print_storage_index(const mos_t_idx* idx) {
    if (idx == NULL) {
        printf("idx: NULL\n");
        return;
    }

    printf("--- mos_t_idx Instance ---\n");
    printf("Name:        %s\n", idx->name);
    printf("Type:        %d\n", idx->type);
    printf("Index Size:  %zu bytes\n", (size_t)idx->index_size);
    printf("File Offset: 0x%08lX\n", (unsigned long)idx->offset_file);
    printf("Attr Name: %s\n", idx->attribute.name);
    printf("Attr Type: %d\n", idx->attribute.type);
    printf("Attr External Offset: 0x%08lX\n", (unsigned long)idx->attribute.external_offset);
    printf("---------------------------\n");
}

void mos_test_mos_create_storage__check_index_area(void) {
    //Arrange
    strcpy(test_config.file_name, "tests/mos_test_mos_create_storage__check_index_area.db");

    mos_t_idx expected_indexes[2] = {0};

    //Act
    test_config.storage = mos_create_storage(test_config.file_name, &test_config.config);

    //Assert
    mos_t_idx id_index = {
        .name = "idx_id",
        .type = MOS_IDX_HASH_MAP,
        .index_size = 8192,
        .attribute = {
            .name = "id",
            .type = MOS_ATTR_TYPE_UINT64,
            .byte_size = 8,
            .external_offset = MOS_NULL_OFFSET,
        },
        .offset_file = test_config.storage->storage_header->layout.offset_index_data
    };
    mos_t_idx prop1_idx = {
        .name = "idx_prop1",
        .type = MOS_IDX_HASH_MAP,
        .index_size = 8192,
        .attribute = {
            .name = "prop1",
            .type = MOS_ATTR_TYPE_UINT64,
            .byte_size = 8,
            .external_offset = offsetof(TestEntry, prop1)
        },
        .offset_file = test_config.storage->storage_header->layout.offset_index_data + 8192
    };
    expected_indexes[0] = id_index;
    expected_indexes[1] = prop1_idx;

    mos_t_layout layout = test_config.storage->storage_header->layout;
    mos_t_idx* actual_indexes = MOS_GET_PTR(test_config.storage->mmap_ptr, layout.offset_indexes);

    //TODO: how to check the padding too?
    TEST_ASSERT_EQUAL_MEMORY(expected_indexes, actual_indexes, sizeof(expected_indexes));

    after_test();
}

void assert_storage_ptrs(void* mmap_ptr, mos_t_layout* expected_layout, mos_t_storage* actual_storage) {
    mos_t_header* expected_header_ptr = MOS_GET_PTR(mmap_ptr, expected_layout->offset_header);
    mos_t_attr_info* expected_attributes_ptr = MOS_GET_PTR(mmap_ptr, expected_layout->offset_attributes);
    mos_t_idx* expected_idx_id_ptr = MOS_GET_PTR(mmap_ptr, expected_layout->offset_indexes);
    mos_t_idx* expected_indexes_ptr = MOS_GET_PTR(mmap_ptr, expected_layout->offset_indexes);
    mos_t_record* expected_entries_ptr = MOS_GET_PTR(mmap_ptr, expected_layout->offset_records);
    mos_t_idx_data* expected_idx_id_data_ptr = MOS_GET_PTR(mmap_ptr, expected_layout->offset_index_data);
    mos_t_idx_data* expected_index_data_ptr = MOS_GET_PTR(mmap_ptr, expected_layout->offset_index_data);
    mos_t_qry_bmp* expected_valid_bitmap_ptr = MOS_GET_PTR(mmap_ptr, expected_layout->offset_valid_bitmap);
    mos_t_qry_bmp* expected_ready_bitmap_ptr = MOS_GET_PTR(mmap_ptr, expected_layout->offset_ready_bitmap);
    
    mos_t_header* actual_header_ptr = actual_storage->storage_header;
    mos_t_attr_info* actual_attributes_ptr = actual_storage->attributes;
    mos_t_idx* actual_idx_id_ptr = actual_storage->idx_id;
    mos_t_idx* actual_indexes_ptr = actual_storage->indexes;
    mos_t_record* actual_entries_ptr = actual_storage->entries;
    mos_t_idx_data* actual_idx_id_data_ptr = actual_storage->idx_id_data;
    mos_t_idx_data* actual_index_data_ptr = actual_storage->index_data;
    mos_t_qry_bmp* actual_valid_bitmap_ptr = actual_storage->valid_bitmap;
    mos_t_qry_bmp* actual_ready_bitmap_ptr = actual_storage->ready_bitmap;

    TEST_ASSERT_EQUAL_PTR(expected_header_ptr, actual_header_ptr);
    TEST_ASSERT_EQUAL_PTR(expected_attributes_ptr, actual_attributes_ptr);
    TEST_ASSERT_EQUAL_PTR(expected_idx_id_ptr, actual_idx_id_ptr);
    TEST_ASSERT_EQUAL_PTR(expected_indexes_ptr, actual_indexes_ptr);
    TEST_ASSERT_EQUAL_PTR(expected_entries_ptr, actual_entries_ptr);
    TEST_ASSERT_EQUAL_PTR(expected_idx_id_data_ptr, actual_idx_id_data_ptr);
    TEST_ASSERT_EQUAL_PTR(expected_index_data_ptr, actual_index_data_ptr);
    TEST_ASSERT_EQUAL_PTR(expected_valid_bitmap_ptr, actual_valid_bitmap_ptr);
    TEST_ASSERT_EQUAL_PTR(expected_ready_bitmap_ptr, actual_ready_bitmap_ptr);
}

void mos_test_mos_create_storage__check_storage_ptrs(void) {
    //Arrange
    strcpy(test_config.file_name, "tests/mos_test_mos_create_storage__check_storage_ptrs.db");

    //Act
    test_config.storage = mos_create_storage(test_config.file_name, &test_config.config);

    //Assert
    assert_storage_ptrs(test_config.storage->mmap_ptr, &test_config.storage->storage_header->layout, test_config.storage);

    after_test();
}

void mos_test_mos_create_storage__check_record_area_empty(void) {
    //Arrange
    strcpy(test_config.file_name, "tests/mos_test_mos_create_storage__check_record_area_empty.db");

    //Act
    test_config.storage = mos_create_storage(test_config.file_name, &test_config.config);

    //Assert
    mos_t_layout layout = test_config.storage->storage_header->layout;
    uint8_t expected_zeros[layout.record_data_size];
    memset(expected_zeros, 0, layout.record_data_size);

    mos_t_record* records_ptr = test_config.storage->entries;
    TEST_ASSERT_EQUAL_MEMORY(expected_zeros, records_ptr, layout.record_data_size);

    after_test();
}

void mos_test_mos_create_storage__check_valid_bitmap_area_empty(void) {
    //Arrange
    strcpy(test_config.file_name, "tests/mos_test_mos_create_storage__check_valid_bitmap_area_empty.db");

    //Act
    test_config.storage = mos_create_storage(test_config.file_name, &test_config.config);

    //Assert
    mos_t_layout layout = test_config.storage->storage_header->layout;
    uint8_t expected_zeros[layout.valid_bitmap_size];
    memset(expected_zeros, 0, layout.valid_bitmap_size);

    mos_t_qry_bmp* valid_bitmap_ptr = test_config.storage->valid_bitmap;
    TEST_ASSERT_EQUAL_MEMORY(expected_zeros, valid_bitmap_ptr, layout.valid_bitmap_size);

    after_test();  
}

void mos_test_mos_create_storage__check_ready_bitmap_area_empty(void) {
    //Arrange
    strcpy(test_config.file_name, "tests/mos_test_mos_create_storage__check_ready_bitmap_area_empty.db");

    //Act
    test_config.storage = mos_create_storage(test_config.file_name, &test_config.config);

    //Assert
    mos_t_layout layout = test_config.storage->storage_header->layout;
    uint8_t expected_zeros[layout.ready_bitmap_size];
    memset(expected_zeros, 0, layout.ready_bitmap_size);

    mos_t_qry_bmp* ready_bitmap_ptr = test_config.storage->ready_bitmap;
    TEST_ASSERT_EQUAL_MEMORY(expected_zeros, ready_bitmap_ptr, layout.ready_bitmap_size);

    after_test();  
}

void mos_test_mos_load_storage(void) {
    //Arrange
    strcpy(test_config.file_name, "tests/mos_test_mos_load_storage.db");

    test_config.storage = mos_create_storage(test_config.file_name, &test_config.config);
    
    uint64_t file_size = test_config.storage->storage_header->layout.file_size;
    TEST_ASSERT_GREATER_OR_EQUAL(1, file_size);

    uint8_t storage_file[file_size];
    memcpy(storage_file, test_config.storage->mmap_ptr, file_size);
    mos_free_storage(test_config.storage);

    //Act
    mos_t_storage* loaded_storage = mos_load_storage("tests/mos_test_mos_load_storage.db");

    //Assert
    TEST_ASSERT_NOT_NULL(loaded_storage);
    TEST_ASSERT_EQUAL(loaded_storage->storage_header->identifier, MOS_FILE_ID);
    TEST_ASSERT_EQUAL_MEMORY(storage_file, loaded_storage->mmap_ptr, file_size);

    assert_storage_ptrs(loaded_storage->mmap_ptr, &loaded_storage->storage_header->layout, loaded_storage);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(mos_test_mos_init_layout__layout_correct);
    RUN_TEST(mos_test_mos_create_storage__file_size);
    RUN_TEST(mos_test_mos_create_storage__check_header_area);
    RUN_TEST(mos_test_mos_create_storage__check_attribute_area);
    RUN_TEST(mos_test_mos_create_storage__check_index_area);
    RUN_TEST(mos_test_mos_create_storage__check_storage_ptrs);
    RUN_TEST(mos_test_mos_create_storage__check_record_area_empty);
    RUN_TEST(mos_test_mos_create_storage__check_valid_bitmap_area_empty);
    RUN_TEST(mos_test_mos_create_storage__check_ready_bitmap_area_empty);

    RUN_TEST(mos_test_mos_load_storage);
    return UNITY_END();
}
