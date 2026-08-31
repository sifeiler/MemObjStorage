#ifndef MOS_H
#define MOS_H

#include <stdint.h>
#include "mos_types_fwd.h"

/* =========================================================================
   API file of the storage library.
   This file contains only declarations the user should see.

   mos = memoryobjectstorage
         -     -     -
   ========================================================================= */

/* =========================================================================
   CONSTANTS, MACROS, ENUMS
   ========================================================================= */

#define MOS_ATTR_NAME_LENGTH 32

typedef enum MOS_IDX_TYPE { 
    MOS_IDX_HASH_MAP = 1 << 0, 
    MOS_IDX_HNSW = 1 << 1
} MOS_IDX_TYPE;

// Only store integers anywhere the enum is used.
// Otherwise it might get messy due to different compilers and mmapping.
typedef enum MOS_ATTR_TYPE {
    MOS_ATTR_MIN = 0,

    MOS_ATTR_TYPE_UINT64 = 0,
    MOS_ATTR_TYPE_TIMESTAMP = 1,
    MOS_ATTR_TYPE_STRING = 2,
    MOS_ATTR_TYPE_VECTOR = 3,
    MOS_ATTR_MAX
} MOS_ATTR_TYPE;

typedef struct mos_t_attr_config {
    uint64_t byte_size;
    uint64_t field_offset;                 // byte offset in user provided struct
    char name[MOS_ATTR_NAME_LENGTH];
    uint8_t type;                          // MOS_ATTR_TYPE
    uint8_t indexed;
    uint8_t  _pad[2];
} mos_t_attr_config;

typedef struct mos_t_idx_hnsw_graph_config {
    //graph parameters
    /**
     * Shapes the layer hierarchy; controls how many nodes end up in upper layers
     * Multiplying by mL scales the node distribution in the graph.
     * mL isn't a probability itself - it's a scaling factor that controls how "stretched out" the exponential distribution is,
     *  which in turn controls how many nodes randomly land on higher layers.
     * Smaller mL compresses the distribution toward layer 0 (flatter hierarchy, fewer upper layers used);
     * Larger mL stretches it out (taller hierarchy, more nodes reaching high layers).
     */
    float mL;

    /**
     * Caps neighbor count per node; 
     * controls memory, graph density, and recall/speed tradeoff
     */
    uint16_t m;

    /**
     * Max connections allowed at layer 0.
     * Usually set to 2*m, since layer 0 holds all nodes and benefits from denser connectivity.
     */
    uint16_t m_max0;

    /**
     * Controls how thoroughly candidate neighbors are searched for when wiring up a new node.
     * Higher means a better-quality graph, leading to better recall.
    */
    uint16_t ef_construction;

    uint8_t  _pad[6];
} mos_t_idx_hnsw_graph_config;

typedef struct mos_t_idx_params_hnsw {
    mos_t_idx_hnsw_graph_config graph_config;   // 8 byte aligned
    uint64_t vector_dim;    // the logical vector dimension
    uint8_t vector_metric;  // MOS_T_IDX_HNSW_METRIC
    uint8_t _pad[7];
} mos_t_idx_params_hnsw;

typedef struct mos_t_idx_config {
    char attribute_name[MOS_ATTR_NAME_LENGTH];
    uint8_t type;              // MOS_IDX_TYPE

    //optional parameters that are to be selected based on field `type`
    union {
        mos_t_idx_params_hnsw hnsw;
    } params;
} mos_t_idx_config;

typedef struct mos_t_storage_config {
    uint64_t max_records;
    uint64_t index_count;
    uint64_t attribute_count;
    uint64_t padded_record_byte_size;

    mos_t_attr_config* attributes;
    mos_t_idx_config* indexes;
    char storage_path[256];
} mos_t_storage_config;

typedef struct mos_t_storage mos_t_storage;
typedef struct mos_t_qry mos_t_qry;
typedef struct mos_t_qry_bmp mos_t_qry_bmp;
typedef struct mos_t_idx_hnsw_graph_config mos_t_idx_hnsw_graph_config;
typedef struct mos_t_float_vector mos_t_float_vector;
typedef struct mos_t_string mos_t_string;

/* =========================================================================
   FUNCTION DECLARATIONS
   ========================================================================= */

/**
* Validates a mos_t_config and gives feedback if it is correctly set up to call
* @see mos_create_storage(char*, mos_t_config*)
* @return return 1 -> valid, 0 -> invalid
*/
int mos_validate_config(mos_t_storage_config* mos_config);

/**
 * Creates a mos_t_storage instance.
 * The instance is fully configured based on the passed mos_t_config.
 * Pass the instance to i.e. @see mos_put(storage, id, record) for storing an item.
 * 
 * @param file_path location where the storage file should be created (including file name)
 * @param mos_config the configuration i.e. attributes, indexes etc.
 * @return pointer to a mos_t_storage instance, fully configured and ready to use.
 */
mos_t_storage* mos_create_storage(const char* file_path, mos_t_storage_config* mos_config);

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