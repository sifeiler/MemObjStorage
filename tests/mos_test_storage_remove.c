#include "unity.h"
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include "../include/mos.h"
#include "../include/mos_internal.h"
#include "../include/mos_idx_hmap.h"
#include "../include/mos_idx.h"

typedef struct {
    uint64_t id;
    uint64_t prop1;
    mos_t_string prop2;
} TestEntry;

typedef struct {
    mos_t_config storage_config;
    //keep attributes seperatly as otherwise the data is not available in test functions
    mos_t_attr_info attribute_info[3];
    mos_t_idx indexes[2];
    mos_t_storage* storage;
    char file_name[256];
} CreateTestConfig;

static CreateTestConfig test_config = {0};

void setUp(void) {
    mos_t_config storage_config = {
        .index_count = 1,
        .attribute_count = 2,
        .max_records = 100
    };
    test_config.storage_config = storage_config;

    mos_t_attr_info id = {
        .name = "id",
        .type = MOS_ATTR_TYPE_UINT64,
        .external_offset = offsetof(TestEntry, id),
        .byte_size = 8
    };
    mos_t_attr_info prop1 = {
        .name = "prop1",
        .type = MOS_ATTR_TYPE_UINT64,
        .external_offset = offsetof(TestEntry, prop1),
        .byte_size = 8
    };
    mos_t_attr_info prop2 = {
        .name = "prop2",
        .type = MOS_ATTR_TYPE_STRING,
        .external_offset = offsetof(TestEntry, prop2),
    };
    test_config.attribute_info[0] = id;
    test_config.attribute_info[1] = prop1;
    test_config.attribute_info[2] = prop2;

    //fake internal id_idx
    mos_t_idx id_idx = {
        .offset_file = 24576,
        .name = "id_idx",
        .type = MOS_IDX_HASH_MAP,
        .index_size = 4096,
        .attribute = id
    };
    mos_t_idx prop1_idx = {
        .offset_file = 24576 + 4096,
        .name = "idx_prop1",
        .type = MOS_IDX_HASH_MAP,
        .index_size = 4096,
        .attribute = prop1
    };
    test_config.indexes[0] = id_idx;
    test_config.indexes[1] = prop1_idx;

    //point test_config to global static array and not to stack (avoid dangling pointer)
    test_config.storage_config.attributes = test_config.attribute_info;
    test_config.storage_config.indexes = test_config.indexes;
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

void mos_storage_remove__remove_record(void) {
    //Arrange
    strcpy(test_config.file_name, "tests/mos_storage_remove__remove_record.db");

    test_config.storage = mos_create_storage(test_config.file_name, &test_config.storage_config);

    TestEntry entry = { .id = 1, .prop1 = 2 };
    entry.prop2.str = "entry1";
    entry.prop2.str_len = 6;

    uint64_t id1 = 1;
    mos_storage_put(test_config.storage, id1, &entry);

    TestEntry* result = (TestEntry*)mos_storage_get(test_config.storage, id1); // Get first slot
    TEST_ASSERT_EQUAL_INT64(1, result->id);

    //Act
    mos_storage_remove(test_config.storage, id1);
    TestEntry* result_after_remove = (TestEntry*)mos_storage_get(test_config.storage, id1); // Get first slot

    //Assert
    TEST_ASSERT_NULL(result_after_remove);

    //TODO: further assert bitmaps, indexes etc.
    mos_t_qry_bmp* valid_bitmap = test_config.storage->valid_bitmap;
    
    after_test();
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(mos_storage_remove__remove_record);
    return UNITY_END();
}