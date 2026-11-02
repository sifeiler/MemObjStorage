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
    test_config.config.index_count = 3;
    test_config.config.max_records = 4;
    test_config.config.padded_record_byte_size = sizeof(TestEntry);

    test_config.config.attributes = calloc(1, sizeof(mos_t_attr) * test_config.config.attribute_count);
    strcpy(test_config.config.attributes[0].name, "unique_id");
    test_config.config.attributes[0].type = MOS_ATTR_TYPE_UINT64;
    test_config.config.attributes[0].byte_size = EXTERNAL_TYPE_SIZES[MOS_ATTR_TYPE_UINT64];
    test_config.config.attributes[0].field_offset = offsetof(TestEntry, unique_id);
    test_config.config.attributes[0].indexed = 1;
    test_config.config.attributes[0].groupable = 0;

    strcpy(test_config.config.attributes[1].name, "prop1");
    test_config.config.attributes[1].type = MOS_ATTR_TYPE_UINT64;
    test_config.config.attributes[1].byte_size = EXTERNAL_TYPE_SIZES[MOS_ATTR_TYPE_UINT64];
    test_config.config.attributes[1].field_offset = offsetof(TestEntry, prop1);
    test_config.config.attributes[1].indexed = 1;
    test_config.config.attributes[1].groupable = 1;

    strcpy(test_config.config.attributes[2].name, "prop2");
    test_config.config.attributes[2].type = MOS_ATTR_TYPE_STRING;
    test_config.config.attributes[2].byte_size = EXTERNAL_TYPE_SIZES[MOS_ATTR_TYPE_STRING];
    test_config.config.attributes[2].field_offset = offsetof(TestEntry, prop2);
    test_config.config.attributes[2].indexed = 1;
    test_config.config.attributes[2].groupable = 0;

    test_config.config.indexes = calloc(1, sizeof(mos_t_idx_descriptor) * test_config.config.index_count);
    strcpy(test_config.config.indexes[0].attribute_name, "unique_id");
    test_config.config.indexes[0].type = MOS_IDX_HASH_MAP;

    strcpy(test_config.config.indexes[1].attribute_name, "prop1");
    test_config.config.indexes[1].type = MOS_IDX_HASH_MAP;

    strcpy(test_config.config.indexes[2].attribute_name, "prop2");
    test_config.config.indexes[2].type = MOS_IDX_HASH_MAP;

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

void test_storage_search__expand(void) {
    //Arrange
    // we use prop1 as our group_id
    // setup two testentries within the same group but with different unique_ids and prop2
    strcpy(test_config.dir_name, "tests/tmp/test_storage_search__expand");
    strcpy(test_config.config.storage_path, "tests/tmp/test_storage_search__expand");
    mos_os_directory_create("tests/tmp/test_storage_search__expand");

    TestEntry entry = {
        .unique_id = 1,
        .prop1 = 10,
        .prop2 = {
            .str = "page1",
            .str_len = 5
        }
    };

    TestEntry entry2 = {
        .unique_id = 2,
        .prop1 = 10,
        .prop2 = {
            .str = "page2",
            .str_len = 5
        }
    };

    // should not be part of resultset
    TestEntry entry3 = {
        .unique_id = 3,
        .prop1 = 20,
        .prop2 = {
            .str = "page3",
            .str_len = 5
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

    mos_t_qry_search_step step_prop2 = { .op = MOS_QRY_OP_EQ, .attribute_query = { .attribute_name = "prop2", .value = { .type = MOS_ATTR_TYPE_STRING, .char_val = "page1", .byte_length = 5 } } };

    mos_t_qry_search_step* sub_steps[] = { &step_prop2 };

    mos_t_qry_search_step step_expand = {
        .op = MOS_QRY_OP_EXPAND,
        .step_count = 1,
        .sub_steps = sub_steps,
        .attribute_query = { .attribute_name = "prop1" }
    };

    mos_t_qry query = {
        .query = &step_expand
    };

    //Act
    result = (mos_t_qry_bmp*)mos_storage_search(storage, &query);

    //Assert
    // the result should hold all entries that are part of the same group, no matter if their prop2 matches
    TEST_ASSERT_EQUAL_INT(4, result->nBits);
    TEST_ASSERT_EQUAL_INT(1, result->nWords);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(0, result->empty);
    TEST_ASSERT_LESS_OR_EQUAL_INT(254, result->full);

    // we expect the two LSBs to be 1
    TEST_ASSERT_EQUAL_HEX64_MESSAGE(0x3, result->data[0], "Bitmap mismatch in search result.");
}

void test_storage_search__logical_operations_with_expand(void) {
    //Arrange
    // we use prop1 as our group_id
    // setup two testentries within the same group but with different unique_ids and prop2
    strcpy(test_config.dir_name, "tests/tmp/test_storage_search__expand");
    strcpy(test_config.config.storage_path, "tests/tmp/test_storage_search__expand");
    mos_os_directory_create("tests/tmp/test_storage_search__expand");

    TestEntry entry = {
        .unique_id = 1,
        .prop1 = 10,
        .prop2 = {
            .str = "page1",
            .str_len = 5
        }
    };

    TestEntry entry2 = {
        .unique_id = 2,
        .prop1 = 10,
        .prop2 = {
            .str = "page2",
            .str_len = 5
        }
    };

    TestEntry entry3 = {
        .unique_id = 3,
        .prop1 = 20,
        .prop2 = {
            .str = "page3",
            .str_len = 5
        }
    };

    TestEntry entry4 = {
        .unique_id = 4,
        .prop1 = 20,
        .prop2 = {
            .str = "page4",
            .str_len = 5
        }
    };

    mos_t_storage* storage = mos_create_storage(test_config.dir_name, &test_config.config);
    test_config.storage = storage;
    uint64_t id1 = 1;
    uint64_t id2 = 2;
    uint64_t id3 = 3;
    uint64_t id4 = 4;
    mos_storage_put(storage, id1, &entry);
    mos_storage_put(storage, id2, &entry2);
    mos_storage_put(storage, id3, &entry3);
    mos_storage_put(storage, id4, &entry4);

    // will have entry1 as result
    mos_t_qry_search_step eq_page1 = { .op = MOS_QRY_OP_EQ, .attribute_query = { .attribute_name = "prop2", .value = { .type = MOS_ATTR_TYPE_STRING, .char_val = "page1", .byte_length = 5 } } };
    
    // will not have a result
    mos_t_qry_search_step eq_page5 = { .op = MOS_QRY_OP_EQ, .attribute_query = { .attribute_name = "prop2", .value = { .type = MOS_ATTR_TYPE_STRING, .char_val = "page5", .byte_length = 5 } } };

    mos_t_qry_search_step* eq_page1_expand_steps[] = { &eq_page1 };
    mos_t_qry_search_step* eq_page5_expand_steps[] = { &eq_page5 };

    // will have entry3 as result
    mos_t_qry_search_step eq_page3 = { .op = MOS_QRY_OP_EQ, .attribute_query = { .attribute_name = "prop2", .value = { .type = MOS_ATTR_TYPE_STRING, .char_val = "page3", .byte_length = 5 } } };
    
    // will have entry4 as result
    mos_t_qry_search_step eq_page4 = { .op = MOS_QRY_OP_EQ, .attribute_query = { .attribute_name = "prop2", .value = { .type = MOS_ATTR_TYPE_STRING, .char_val = "page4", .byte_length = 5 } } };

    // at this moment eq_page3 AND eq_page4 will be the empty set
    mos_t_qry_search_step* eq_page3_expand_steps[] = { &eq_page3 };
    mos_t_qry_search_step* eq_page4_expand_steps[] = { &eq_page4 };

    // will have a result
    mos_t_qry_search_step eq_page1_expand = {
        .op = MOS_QRY_OP_EXPAND,
        .step_count = 1,
        .sub_steps = eq_page1_expand_steps,
        .attribute_query = { .attribute_name = "prop1" }
    };

    // will not have a result
    mos_t_qry_search_step eq_page5_expand = {
        .op = MOS_QRY_OP_EXPAND,
        .step_count = 1,
        .sub_steps = eq_page5_expand_steps,
        .attribute_query = { .attribute_name = "prop1" }
    };

    // will have a result
    mos_t_qry_search_step eq_page3_expand = {
        .op = MOS_QRY_OP_EXPAND,
        .step_count = 1,
        .sub_steps = eq_page3_expand_steps,
        .attribute_query = { .attribute_name = "prop1" }
    };

    // will have a result
    mos_t_qry_search_step eq_page4_expand = {
        .op = MOS_QRY_OP_EXPAND,
        .step_count = 1,
        .sub_steps = eq_page4_expand_steps,
        .attribute_query = { .attribute_name = "prop1" }
    };

    mos_t_qry_search_step* and_page1_page5_steps[] = { &eq_page1_expand, &eq_page5_expand };

    // will not have a result
    mos_t_qry_search_step and1 = {
        .op = MOS_QRY_OP_AND,
        .step_count = 2,
        .sub_steps = and_page1_page5_steps
    };

    mos_t_qry_search_step* and_page3_page4_steps[] = { &eq_page3_expand, &eq_page4_expand };

    // eq_page3 AND eq_page4 will have entry3 and entry4 as a result
    mos_t_qry_search_step and2 = {
        .op = MOS_QRY_OP_AND,
        .step_count = 2,
        .sub_steps = and_page3_page4_steps
    };

    mos_t_qry_search_step* sub_steps_or[] = { &and1, &and2 };

    mos_t_qry_search_step or = {
        .op = MOS_QRY_OP_OR,
        .step_count = 2,
        .sub_steps = sub_steps_or
    };

    mos_t_qry query = {
        .query = &or
    };

    //Act
    result = (mos_t_qry_bmp*)mos_storage_search(storage, &query);

    //Assert
    // the result should hold all entries that are part of the same group, no matter if their prop2 matches
    TEST_ASSERT_EQUAL_INT(4, result->nBits);
    TEST_ASSERT_EQUAL_INT(1, result->nWords);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(0, result->empty);
    TEST_ASSERT_LESS_OR_EQUAL_INT(254, result->full);

    // we expect the two LSBs to be 1
    TEST_ASSERT_EQUAL_HEX64_MESSAGE(0xC, result->data[0], "Bitmap mismatch in search result.");
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_storage_search__logical_and);
    RUN_TEST(test_storage_search__logical_or);
    RUN_TEST(test_storage_search__logical_not);
    RUN_TEST(test_storage_search__expand);
    RUN_TEST(test_storage_search__logical_operations_with_expand);
    return UNITY_END();
}