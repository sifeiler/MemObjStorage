#include "unity.h"
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include "../include/mos_qry.h"

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
static mos_t_qry_bmp* result;

void setUp(void) {
    mos_t_config storage_config = {
        .index_count = 2,
        .attribute_count = 3,
        .max_records = 4
    };
    test_config.storage_config = storage_config;

    mos_t_attr_info id = {
        .name = "id",
        .type = MOS_ATTR_TYPE_UINT64,
        .byte_size = sizeof(uint64_t),
        .external_offset = offsetof(TestEntry, id)
    };
    mos_t_attr_info prop1 = {
        .name = "prop1",
        .type = MOS_ATTR_TYPE_UINT64,
        .byte_size = sizeof(uint64_t),
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

    mos_t_idx prop1_idx = {
        .name = "idx_prop1",
        .type = MOS_IDX_HASH_MAP,
        .attribute = {
            .name = "prop1",
            .type = MOS_ATTR_TYPE_UINT64,
            .byte_size = sizeof(uint64_t),
            .external_offset = offsetof(TestEntry, prop1)
        }
    };
    mos_t_idx prop2_idx = {
        .name = "idx_prop2",
        .type = MOS_IDX_HASH_MAP,
        .attribute = {
            .name = "prop2",
            .type = MOS_ATTR_TYPE_STRING,
            .external_offset = offsetof(TestEntry, prop2)
        }
    };
    
    test_config.indexes[0] = prop1_idx;
    test_config.indexes[1] = prop2_idx;

    //point test_config to global static array and not to stack (avoid dangling pointer)
    test_config.storage_config.attributes = test_config.attribute_info;
    test_config.storage_config.indexes = test_config.indexes;

    result = NULL;
}

void tearDown(void) {
    if(test_config.storage != NULL) {
        mos_free_storage(test_config.storage);
        test_config.storage = NULL;
    }

    // reset static struct to start clean for next test
    memset(&test_config, 0, sizeof(test_config));

    if(result) {
        free(result);
    }
}

void test_storage_search__logical_and(void) {
    //Arrange
    strcpy(test_config.file_name, "tests/test_storage_search__logical_and.db");

    TestEntry entry = {
        .id = 1,
        .prop1 = 200,
        .prop2 = {
            .str = "entry1",
            .str_len = 6
        }
    };

    TestEntry entry2 = {
        .id = 2,
        .prop1 = 300,
        .prop2 = {
            .str = "entry2",
            .str_len = 6
        }
    };

    mos_t_storage* storage = mos_create_storage(test_config.file_name, &test_config.storage_config);
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
    strcpy(test_config.file_name, "tests/test_storage_search__logical_or.db");

    TestEntry entry = {
        .id = 1,
        .prop1 = 200,
        .prop2 = {
            .str = "entry1",
            .str_len = 6
        }
    };

    TestEntry entry2 = {
        .id = 2,
        .prop1 = 300,
        .prop2 = {
            .str = "entry2",
            .str_len = 6
        }
    };

    TestEntry entry3 = {
        .id = 3,
        .prop1 = 400,
        .prop2 = {
            .str = "entry3",
            .str_len = 6
        }
    };

    mos_t_storage* storage = mos_create_storage(test_config.file_name, &test_config.storage_config);
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
    strcpy(test_config.file_name, "tests/test_storage_search__logical_not.db");

    TestEntry entry = {
        .id = 1,
        .prop1 = 200,
        .prop2 = {
            .str = "entry1",
            .str_len = 6
        }
    };

    TestEntry entry2 = {
        .id = 2,
        .prop1 = 300,
        .prop2 = {
            .str = "entry2",
            .str_len = 6
        }
    };

    mos_t_storage* storage = mos_create_storage(test_config.file_name, &test_config.storage_config);
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