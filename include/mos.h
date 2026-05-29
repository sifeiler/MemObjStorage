#ifndef MOS_H
#define MOS_H

#include <stdint.h>

/* =========================================================================
   API file of the storage library.
   This file contains only declarations the user should see.

   mos = memoryobjectstorage
         -     -     -
   ========================================================================= */

/* =========================================================================
   CONSTANTS, MACROS, ENUMS
   ========================================================================= */

typedef enum MOS_IDX_TYPE { MOS_IDX_HASH_MAP, MOS_IDX_TRIE } MOS_IDX_TYPE;

typedef enum MOS_ATTR_TYPE {
    MOS_ATTR_TYPE_UINT64,
    MOS_ATTR_TYPE_TIMESTAMP,
    MOS_ATTR_TYPE_STRING
} MOS_ATTR_TYPE;

#pragma pack(push, 1)
typedef struct mos_t_attr_info {
    char name[32];
    MOS_ATTR_TYPE type;
    uint64_t byte_size;
    uint64_t external_offset;            // byte offset in user record
} mos_t_attr_info;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct mos_t_idx {
    char name[32];
    MOS_IDX_TYPE type;
    uint64_t offset_file;   // offset in the storage file
    uint64_t index_size;    // bytes occupied in the storage file
    mos_t_attr_info attribute;
} mos_t_idx;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct mos_t_string {
    char* str;
    uint32_t str_len;
} mos_t_string;
#pragma pack(pop)

typedef struct mos_t_config {
    uint64_t max_records;
    mos_t_attr_info* attributes;
    mos_t_idx* indexes;
    uint64_t index_count;
    uint64_t attribute_count;
    char* storage_path;
} mos_t_config;

typedef struct mos_t_config mos_t_config;
typedef struct mos_t_storage mos_t_storage;
typedef struct mos_t_qry mos_t_qry;
typedef struct mos_t_qry_bmp mos_t_qry_bmp;

/* =========================================================================
   FUNCTION DECLARATIONS
   ========================================================================= */

/**
* Validates a mos_t_config and gives feedback if it is correctly set up to call
* @see mos_create_storage(char*, mos_t_config*)
* @return return 1 -> valid, 0 -> invalid
*/
int mos_validate_config(mos_t_config* mos_config);

/**
 * Creates a mos_t_storage instance.
 * The instance is fully configured based on the passed mos_t_config.
 * Pass the instance to i.e. @see mos_put(storage, id, record) for storing an item.
 * 
 * @param file_path location where the storage file should be created (including file name)
 * @param mos_config the configuration i.e. attributes, indexes etc.
 * @return pointer to a mos_t_storage instance, fully configured and ready to use.
 */
mos_t_storage* mos_create_storage(const char* file_path, mos_t_config* mos_config);

/**
 * Loads the file and initializes a storage instance from it.
 * 
 * @param file_path storage location
 * @return pointer to the initialized mos_t_storage instance
 */
mos_t_storage* mos_load_storage(const char* file_path);

/**
 * Frees all pointers in the passed mos_t_storage instance.
 * Unmaps the file from memory.
 * Closes the file descriptor.
 * The storage can no longer be used via the provided pointer.
 * To use the storage again, call @see mos_load_storage(file_path).
 */
void mos_free_storage(mos_t_storage* storage);

/**
 * Puts a record to the storage.
 * Be aware, that only records, matching the original configuration (attributes, etc.), that was passed on the
 *  initial call of @see mos_create_storage(), are supported.
 * Make sure to remember the id of the record. It's the records unique identifier and needed for deletion.
 * 
 * @param storage     The storage to put the record to
 * @param id        The unique identifier of the record
 * @param record    The record to put
 */
void mos_storage_put(mos_t_storage* storage, uint64_t id, void* record);

const void* mos_storage_get(mos_t_storage* storage, uint64_t id);

/**
 * Remove a record from the storage.
 */
void mos_storage_remove(mos_t_storage* storage, uint64_t id);

/**
 * Searches the storage and returns a bitmap.
 * Every record matching the search query, is represented by a 1 in the bitmap.
 * 
 * Call:
 * @see mos_storage_get_data_for_row_id(storage, row_id)
 *  To get a pointer of the record at row_id. row_id is the index of a 1 bit of the bitmap.
 * 
 * @see mos_storage_get_row_ids(storage, bitmap)
 *  To get indexes of all 1s in the bitmap.
 * 
 * @see mos_storage_get_data(storage, bitmap)
 *  To get pointers to all records having a 1 in the bitmap.
 */
const mos_t_qry_bmp* mos_storage_search(mos_t_storage* storage, mos_t_qry* query);

/**
 * The returned record is copied from memory and allocated on heap.
 * The caller is responsible for freeing it.
*/
const void* mos_storage_get_data_for_row_id(mos_t_storage* storage, uint64_t row_id);
const void* mos_storage_get_row_ids(mos_t_storage* storage, mos_t_qry_bmp* bitmap);
const void* mos_storage_get_data(mos_t_storage* storage, mos_t_qry_bmp* bitmap);

void mos_print_info(mos_t_storage* storage);
void mos_print_state(mos_t_storage* storage);

#endif  // MOS_H