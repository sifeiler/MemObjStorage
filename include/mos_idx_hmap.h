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
   uint64_t offset_values;
   uint64_t offset_verifiers;
} mos_idx_hmap_header;
#pragma pack(pop)

/**
 * To verify a value at index i, get verifiers[i] and compare to a 64 bit verifier 
 *    calculated based on the value.
 */
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
uint64_t mos_idx_hmap_size(const uint64_t item_count);

/**
 * Initializes index and index_data.
 * Values are directly written to the passed arguments.
 *
 * For this function to work, 
 *  sizeof(mos_t_idx_hmap_header) is a multiple of 2
 * 
 * Ensure proper alignment of:
 *  idx->offset_file is a multiple of 2
 * 
 * For performance reasons, the hash table size is doubled
 *  to keep 100% capacity at 50% utilization (linear probing).
 *
 * @param item_count: max items in the index
 * @param idx: the index
 *  - idx->index_size
 * @param idx_data: the actual index data (values)
 *  - idx_data->index_payload->index_header.table_size
 *    Aligned to next power of 2 for fast modulo
 * 
 *  - idx_data->index_payload->index_header.offset_values
 *  - idx_data->index_payload->index_header.offset_verifiers
 */
void mos_idx_hmap_init(const uint64_t item_count, mos_t_idx* idx, mos_t_idx_data* idx_data);

//key_len in bytes
int64_t mos_idx_hmap_put(const mos_t_idx_data* idx_data, const uint8_t* key, const size_t key_len, const uint64_t value);
int64_t mos_idx_hmap_get(const mos_t_idx_data* idx_data, const uint8_t* key, const size_t key_len);
void mos_idx_hmap_remove(const mos_t_idx_data* idx_data, const uint8_t* key, const size_t key_byte_len);

/**
 * Searches the index data for a value and sets a 1 in the bitmap, for every match in the index.
 */
void mos_idx_hmap_bitmap_search(const mos_t_idx_data* idx_data, mos_t_qry_bmp* bitmap, const mos_t_qry_attr_qry* attribute_query);

#endif