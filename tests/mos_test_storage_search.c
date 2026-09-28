#include "unity.h"
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include "../include/mos_qry.h"

typedef struct {
    uint64_t unique_id;
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
static mos_t_qry_bmp* result;

void setUp(void) {
    test_config.config.attribute_count = 3;
    test_config.config.index_count = 2;
    test_config.config.max_records = 4;
    test_config.config.padded_record_byte_size = sizeof(TestEntry);

    test_config.config.attributes = calloc(1, sizeof(mos_t_attr) * test_config.config.attribute_count);
    strcpy(test_config.config.attributes[0].name, "unique_id");
    test_config.config.attributes[0].type = MOS_ATTR_TYPE_UINT64;
    test_config.config.attributes[0].byte_size = EXTERNAL_TYPE_SIZES[MOS_ATTR_TYPE_UINT64];
    test_config.config.attributes[0].field_offset = offsetof(TestEntry, unique_id);
    test_config.config.attributes[0].indexed = 0;

    strcpy(test_config.config.attributes[1].name, "prop1");
    test_config.config.attributes[1].type = MOS_ATTR_TYPE_UINT64;
    test_config.config.attributes[1].byte_size = EXTERNAL_TYPE_SIZES[MOS_ATTR_TYPE_UINT64];
    test_config.config.attributes[1].field_offset = offsetof(TestEntry, prop1);
    test_config.config.attributes[1].indexed = 1;

    strcpy(test_config.config.attributes[2].name, "prop2");
    test_config.config.attributes[2].type = MOS_ATTR_TYPE_STRING;
    test_config.config.attributes[2].byte_size = EXTERNAL_TYPE_SIZES[MOS_ATTR_TYPE_STRING];
    test_config.config.attributes[2].field_offset = offsetof(TestEntry, prop2);
    test_config.config.attributes[2].indexed = 1;

    test_config.config.indexes = calloc(1, sizeof(mos_t_idx_descriptor) * test_config.config.index_count);
    strcpy(test_config.config.indexes[0].attribute_name, "prop1");
    test_config.config.indexes[0].type = MOS_IDX_HASH_MAP;

    strcpy(test_config.config.indexes[1].attribute_name, "prop2");
    test_config.config.indexes[1].type = MOS_IDX_HASH_MAP;

    result = NULL;

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

    if(result) {
        free(result);
    }
}

void test_storage_search__logical_and(void) {
    //Arrange
    strcpy(test_config.dir_name, "tests/tmp/test_storage_search__logical_and");
    strcpy(test_config.config.storage_path, "tests/tmp/test_storage_search__logical_and");
    mos_os_directory_create("tests/tmp/test_storage_search__logical_and");

    TestEntry entry = {
        .unique_id = 1,
        .prop1 = 200,
        .prop2 = {
            .str = "entry1",
            .str_len = 6
        }
    };

    TestEntry entry2 = {
        .unique_id = 2,
        .prop1 = 300,
        .prop2 = {
            .str = "entry2",
            .str_len = 6
        }
    };

    mos_t_storage* storage = mos_create_storage(test_config.dir_name, &test_config.config);
    test_config.storage = storage;
    uint64_t id1 = 1;
    uint64_t id2 = 2;
    mos_storage_put(storage, id1, &entry);
    mos_storage_put(storage, id2, &entry2);
    
    mos_t_qry_search_step step_prop1 = { .op = MOS_QRY_OP_EQ, .attribute_query = { .attribute_name = "prop1", .value = { .type = MOS_ATTR_TYPE_UINT64, .int_val = 300, .byte_length = 8 } } };
    mos_t_qry_search_step step_prop2 = { .op = MOS_QRY_OP_EQ, .attribute_query = { .attribute_name = "prop2", .value = { .type = MOS_ATTR_TYPE_STRING, .char_val = "entry2", .byte_length = 6 } } };

    mos_t_qry_search_step* sub_steps[] = { &step_prop1, &step_prop2 };

    mos_t_qry_search_step step = {
        .op = MOS_QRY_OP_AND,
        .step_count = 2,
        .sub_steps = sub_steps
    };

    mos_t_qry query = {
        .query = &step
    };

    //Act
    result = (mos_t_qry_bmp*)mos_storage_search(storage, &query);

    //Assert
    TEST_ASSERT_EQUAL_INT(4, result->nBits);
    TEST_ASSERT_EQUAL_INT(1, result->nWords);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(0, result->empty);
    TEST_ASSERT_LESS_OR_EQUAL_INT(254, result->full);
    TEST_ASSERT_EQUAL_HEX64_MESSAGE(0x2, result->data[0], "Bitmap mismatch in search result.");
}

void test_storage_search__logical_or(void) {
    //Arrange
    strcpy(test_config.dir_name, "tests/tmp/test_storage_search__logical_or");
    strcpy(test_config.config.storage_path, "tests/tmp/test_storage_search__logical_or");
    mos_os_directory_create("tests/tmp/test_storage_search__logical_or");

    TestEntry entry = {
        .unique_id = 1,
        .prop1 = 200,
        .prop2 = {
            .str = "entry1",
            .str_len = 6
        }
    };

    TestEntry entry2 = {
        .unique_id = 2,
        .prop1 = 300,
        .prop2 = {
            .str = "entry2",
            .str_len = 6
        }
    };

    TestEntry entry3 = {
        .unique_id = 3,
        .prop1 = 400,
        .prop2 = {
            .str = "entry3",
            .str_len = 6
        }
    };

    mos_t_storage* storage = mos_create_storage(test_config.dir_name, &test_config.config);
    test_config.storage = storage;
    uint64_t id1 = 1;
    uint64_t id2 = 2;
    uint64_t id3 = 3;
    mos_storage_put(storage, id1, &entry);
    mos_storage_put(storage, id2, &entry2);
    mos_storage_put(storage, id3, &entry3);

    mos_t_qry_search_step step1_prop1 = { .op = MOS_QRY_OP_EQ, .attribute_query = { .attribute_name = "prop1", .value = { .type = MOS_ATTR_TYPE_UINT64, .int_val = 300, .byte_length = 8 } } };
    mos_t_qry_search_step step2_prop1 = { .op = MOS_QRY_OP_EQ, .attribute_query = { .attribute_name = "prop1", .value = { .type = MOS_ATTR_TYPE_UINT64, .int_val = 400, .byte_length = 8 } } };

    mos_t_qry_search_step* sub_steps[] = { &step1_prop1, &step2_prop1 };

    mos_t_qry_search_step step = {
        .op = MOS_QRY_OP_OR,
        .step_count = 2,
        .sub_steps = sub_steps
    };

    mos_t_qry query = {
        .query = &step
    };

    //Act
    result = (mos_t_qry_bmp*)mos_storage_search(storage, &query);

    //Assert
    TEST_ASSERT_EQUAL_INT(4, result->nBits);
    TEST_ASSERT_EQUAL_INT(1, result->nWords);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(0, result->empty);
    TEST_ASSERT_LESS_OR_EQUAL_INT(254, result->full);
    TEST_ASSERT_EQUAL_HEX64_MESSAGE(0x6, result->data[0], "Bitmap mismatch in search result.");
}

/**
 * negation of test_storage_search__logical_and
 */
void test_storage_search__logical_not(void) {
    //Arrange
    strcpy(test_config.dir_name, "tests/tmp/test_storage_search__logical_not");
    strcpy(test_config.config.storage_path, "tests/tmp/test_storage_search__logical_not");
    mos_os_directory_create("tests/tmp/test_storage_search__logical_not");

    TestEntry entry = {
        .unique_id = 1,
        .prop1 = 200,
        .prop2 = {
            .str = "entry1",
            .str_len = 6
        }
    };

    TestEntry entry2 = {
        .unique_id = 2,
        .prop1 = 300,
        .prop2 = {
            .str = "entry2",
            .str_len = 6
        }
    };

    mos_t_storage* storage = mos_create_storage(test_config.dir_name, &test_config.config);
    test_config.storage = storage;
    uint64_t id1 = 1;
    uint64_t id2 = 2;
    mos_storage_put(storage, id1, &entry);
    mos_storage_put(storage, id2, &entry2);
    
    mos_t_qry_search_step step_prop1 = { .op = MOS_QRY_OP_EQ, .attribute_query = { .attribute_name = "prop1", .value = { .type = MOS_ATTR_TYPE_UINT64, .int_val = 300, .byte_length = 8 } } };
    mos_t_qry_search_step step_prop2 = { .op = MOS_QRY_OP_EQ, .attribute_query = { .attribute_name = "prop2", .value = { .type = MOS_ATTR_TYPE_STRING, .char_val = "entry2", .byte_length = 6 } } };

    mos_t_qry_search_step* sub_steps[] = { &step_prop1, &step_prop2 };

    mos_t_qry_search_step step_and = {
        .op = MOS_QRY_OP_AND,
        .step_count = 2,
        .sub_steps = sub_steps
    };

    mos_t_qry_search_step* not_steps[] = { &step_and };

    mos_t_qry_search_step step_not = {
        .op = MOS_QRY_OP_NOT,
        .step_count = 1,
        .sub_steps = not_steps
    };

    mos_t_qry query = {
        .query = &step_not
    };

    //Act
    result = (mos_t_qry_bmp*)mos_storage_search(storage, &query);

    //Assert
    TEST_ASSERT_EQUAL_INT(4, result->nBits);
    TEST_ASSERT_EQUAL_INT(1, result->nWords);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(0, result->empty);
    TEST_ASSERT_LESS_OR_EQUAL_INT(254, result->full);
    TEST_ASSERT_EQUAL_HEX64_MESSAGE(0x5, result->data[0], "Bitmap mismatch in search result.");
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_storage_search__logical_and);
    RUN_TEST(test_storage_search__logical_or);
    RUN_TEST(test_storage_search__logical_not);
    return UNITY_END();
}