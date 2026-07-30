#include "../include/mos.h"
#include "../include/mos_string.h"
#include <stdio.h>
#include <inttypes.h>
#include <string.h>
#include <stdlib.h>
#include <stddef.h>

// 28 bytes in storage:
// file_path   12 bytes for the string descriptor (actual string is stored in silo)
// created_at   8 bytes
// modified_at  8 bytes
//             --------
//             28 bytes
typedef struct {
    mos_t_string file_path;
    uint64_t created_at;
    uint64_t modified_at;
} File;

int main() {
    //simple storage test
    mos_t_config mos_config = {
        .index_count = 0,
        .attribute_count = 3,
        .max_records = 100
    };

    mos_t_attr_info attribute_info[3];

    mos_t_attr_info file_path = {
        .name = "file_path",
        .type = MOS_ATTR_TYPE_STRING,
        .external_offset = offsetof(File, file_path)
    };
    mos_t_attr_info created_at = {
        .name = "created_at",
        .type = MOS_ATTR_TYPE_TIMESTAMP,
        .external_offset = offsetof(File, created_at)
    };
    mos_t_attr_info modified_at = {
        .name = "modified_at",
        .type = MOS_ATTR_TYPE_TIMESTAMP,
        .external_offset = offsetof(File, modified_at)
    };

    attribute_info[0] = file_path;
    attribute_info[1] = created_at;
    attribute_info[2] = modified_at;

    mos_config.attributes = attribute_info;

    // At this point, mos_config contains all information needed to physically create the storage file.
    // It is not possible to add attributes or indexes afterwards.
    mos_t_storage* storage = mos_create_storage("mos.db", &mos_config);

    File file = {
        .file_path = {
            .str = "File1",
            .str_len = 5
        },
        .created_at = 1,
        .modified_at = 2
    };

    File file2 = {
        .file_path = {
            .str = "File2",
            .str_len = 5
        },
        .created_at = 3,
        .modified_at = 4
    };
    
    mos_print_info(storage);
    mos_print_state(storage);

    mos_storage_put(storage, 1, &file);
    mos_storage_put(storage, 2, &file2);

    mos_print_state(storage);
    mos_free_storage(storage);

    return 0;
}