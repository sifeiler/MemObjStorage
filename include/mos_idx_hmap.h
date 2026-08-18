#ifndef MOS_IDX_HMAP
#define MOS_IDX_HMAP

#include <inttypes.h>
#include "mos_types_fwd.h"

/* =========================================================================
   1. CONSTANTS, MACROS, ENUMS
   ========================================================================= */

#define MOS_IDX_HASH_SIZE 64
#define MOS_IDX_EMPTY 0ULL
#define MOS_IDX_THOMBSTONE 0xFFFFFFFFFFFFFFFFULL
#define MOS_IDX_MURMUR3_SEED 0x7FFFFFFF

/* =========================================================================
   2. STRUCTS
   ========================================================================= */

#pragma pack(push, 1)
typedef struct mos_t_idx_hmap_header {
   uint64_t table_size;

   //offset in mos_t_idx_hmap
   uint64_t offset_values;
   //offset in mos_t_idx_hmap
   uint64_t offset_verifiers;
} mos_idx_hmap_header;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct mos_t_idx_hmap {
   mos_idx_hmap_header index_header;
   // [values] [verifiers]
   uint64_t data[];
} mos_idx_hmap;
#pragma pack(pop)

/* =========================================================================
   3. FUNCTION DECLARATIONS
   ========================================================================= */

uint64_t mos_idx_murmur_hash_3_64(const uint8_t* data, const uint64_t seed, const size_t data_len);
__uint128_t mos_idx_murmur_hash_3_128(const uint8_t* data, const uint64_t seed, const size_t data_len);
uint64_t mos_idx_hmap_size(const uint64_t item_count, mos_t_idx* idx);

/**
 * Initializes index and index_data.
 * Values are directly written to the passed arguments.
 *
 * For this function to work, 
 *  sizeof(mos_t_idx_hmap_header) is a multiple of 2
 * 
 * Ensure proper alignment of:
 *  idx->index_offset is a multiple of 2
 * 
 * For performance reasons, the hash table size is doubled
 *  to keep 100% capacity at 50% utilization (linear probing).
 *
 * @param item_count max items in the index
 * @param idx the index
 * @param idx_data the actual index data (values)
 *  - idx_data->index_payload->index_header.table_size
 *    Aligned to next power of 2 for fast modulo
 */
void mos_idx_hmap_init(const uint64_t item_count, mos_t_idx* idx, mos_t_idx_data* idx_data);

/**
 * Creates a hash for the passed key and stores the value together with the created hash.
 * Later, when the user provides the identitical key to function mos_idx_hmap_get, the value can be retrieved again.
 * 
 * @param idx_data the index. Contains index metainformation and the index data itself.
 * @param key the value to create the hash from. Typically a unique value that identifies the indexed item.
 * @param key_len the byte length of the key
 * @param value the value stored in the hashmap. Typically a unique identifier of the indexed item (`record_row_id` in this case)
 * @param result always NULL for hmap. No result is returned by out parameter.
 * 
 * @return -1 if hash map is full. `value` otherwise.
 */
int64_t mos_idx_hmap_put(mos_t_idx_data* idx_data, const uint8_t* key, const size_t key_len, const uint64_t value, mos_idx_put_result* result);

/**
 * Creates a hash for the passed key and stores the value together with the created hash.
 * Later, when the user provides the identitical key to function `mos_idx_hmap_get`, the value can be retrieved again.
 * 
 * @param idx_data the actual index. Contains index metainformation and the index data itself.
 * @param key the value to create the actual hash from, which is used for the hash table lookup. Typically a unique value that identifies the indexed item.
 * @param key_len the byte length of the key
 * 
 * @return the value stored in the hashmap (`record_row_id` in this case) or `VALUE_NOT_FOUND` if not found.
 */
int64_t mos_idx_hmap_get(const mos_t_idx_data* idx_data, const uint8_t* key, const size_t key_len);

/**
 * Removes a value for the passed key. Flags the gap in the hash table in order to be reused on next `mos_idx_hmap_put`.
 * 
 * @param idx_data the actual index. Contains index metainformation and the index data itself.
 * @param key the value to create the actual hash from, which is used for the hash table lookup. Typically a unique value that identifies the indexed item.
 * @param key_len the byte length of the key
 */
void mos_idx_hmap_remove(mos_t_idx_data* idx_data, const uint8_t* key, const size_t key_len);

/**
 * Searches the index data for a value and sets a 1 in the `bitmap` output parameter, for every match in the index.
 * 
 * @param idx_data the actual index. Contains index metainformation and the index data itself.
 * @param bitmap result output parameter
 * @param attribute_query containing the key to be hashed and looked up in the hash table.
 */
void mos_idx_hmap_bitmap_search(const mos_t_idx_data* idx_data, mos_t_qry_bmp* bitmap, const mos_t_qry_attr_qry* attribute_query);

#endif