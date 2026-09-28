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
    uint8_t pad[4];
} TestEntry;

typedef struct {
    mos_t_storage_config config;
    mos_t_idx_descriptor indexes[2];
    mos_t_storage* storage;
    char dir_name[256];
} CreateTestConfig;

static CreateTestConfig test_config = {0};
static TestEntry* result1;

void setUp(void) {
    test_config.config.attribute_count = 3;
    test_config.config.index_count = 2;
    test_config.config.max_records = 100;
    test_config.config.padded_record_byte_size = sizeof(TestEntry);

    test_config.config.attributes = calloc(1, sizeof(mos_t_attr) * test_config.config.attribute_count);
    strcpy(test_config.config.attributes[0].name, "id");
    test_config.config.attributes[0].type = MOS_ATTR_TYPE_UINT64;
    test_config.config.attributes[0].byte_size = EXTERNAL_TYPE_SIZES[MOS_ATTR_TYPE_UINT64];
    test_config.config.attributes[0].field_offset = offsetof(TestEntry, id);
    test_config.config.attributes[0].indexed = 1;

    strcpy(test_config.config.attributes[1].name, "prop1");
    test_config.config.attributes[1].type = MOS_ATTR_TYPE_UINT64;
    test_config.config.attributes[1].byte_size = EXTERNAL_TYPE_SIZES[MOS_ATTR_TYPE_UINT64];
    test_config.config.attributes[1].field_offset = offsetof(TestEntry, prop1);
    test_config.config.attributes[1].indexed = 1;

    strcpy(test_config.config.attributes[2].name, "prop2");
    test_config.config.attributes[2].type = MOS_ATTR_TYPE_STRING;
    test_config.config.attributes[2].byte_size = EXTERNAL_TYPE_SIZES[MOS_ATTR_TYPE_STRING];
    test_config.config.attributes[2].field_offset = offsetof(TestEntry, prop2);
    test_config.config.attributes[2].indexed = 0;

    test_config.config.indexes = calloc(1, sizeof(mos_t_idx_descriptor) * 2);
    strcpy(test_config.config.indexes[0].attribute_name, "id");
    test_config.config.indexes[0].type = MOS_IDX_HASH_MAP;

    strcpy(test_config.config.indexes[1].attribute_name, "prop1");
    test_config.config.indexes[1].type = MOS_IDX_HASH_MAP;

    mos_os_directory_create("tests/tmp");
}

void tearDown(void) {
    if(test_config.storage != NULL) {
        mos_free_storage(test_config.storage);
        test_config.storage = NULL;
    }

    if(test_config.config.attributes != NULL) {
        free(test_config.config.attributes);
    }

    if(test_config.config.indexes != NULL) {
        free(test_config.config.indexes);
    }

    // reset static struct to start clean for next test
    memset(&test_config, 0, sizeof(test_config));

    if(result1) {
        free(result1);
    }
}

void mos_storage_remove__remove_record(void) {
    //Arrange
    strcpy(test_config.dir_name, "tests/tmp/mos_storage_remove__remove_record");
    strcpy(test_config.config.storage_path, "tests/tmp/mos_storage_remove__remove_record");
    mos_os_directory_create("tests/tmp/mos_storage_remove__remove_record");

    test_config.storage = mos_create_storage(test_config.dir_name, &test_config.config);

    TestEntry entry = { .id = 1, .prop1 = 2 };
    entry.prop2.str = "entry1";
    entry.prop2.str_len = 6;

    uint64_t id1 = 1;
    mos_storage_put(test_config.storage, id1, &entry);

    result1 = (TestEntry*)mos_storage_get(test_config.storage, id1); // Get first slot
    TEST_ASSERT_NOT_NULL(result1);
    TEST_ASSERT_EQUAL_INT64(1, result1->id);

    //Act
    mos_storage_remove(test_config.storage, id1);
    TestEntry* result_after_remove = (TestEntry*)mos_storage_get(test_config.storage, id1); // Get first slot

    //Assert
    TEST_ASSERT_NULL(result_after_remove);

    //TODO: further assert bitmaps, indexes etc.
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(mos_storage_remove__remove_record);
    return UNITY_END();
}