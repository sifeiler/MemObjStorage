#ifndef MOS_IDX_H
#define MOS_IDX_H

#include <stdint.h>
#include "mos_types_fwd.h"
#include "mos.h"
#include "mos_idx_hmap.h"
#include "mos_idx_hnsw.h"

/* =========================================================================
   1. CONSTANTS, MACROS, ENUMS
   ========================================================================= */

/* =========================================================================
   2. STRUCTS
   ========================================================================= */

#pragma pack(push, 1)
typedef struct mos_t_idx_data_header {
   mos_t_idx index;
   uint64_t index_payload_offset;   //offset of index_payload in mos_t_idx_data
} mos_t_idx_data_header;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct mos_t_idx_data {
   mos_t_idx_data_header header;
   //the actual index: hash_map, hnsw, ...
   uint8_t index_payload[];
} mos_t_idx_data;
#pragma pack(pop)

typedef union mos_idx_put_result {
    uint64_t hnsw_node_id;
    uint8_t raw[8];
} mos_idx_put_result;

typedef struct mos_t_idx_ops {
   uint64_t (*get_index_size)(const uint64_t item_count, mos_t_idx* idx);
   void (*init_index)(const uint64_t item_count, mos_t_idx* idx, mos_t_idx_data* idx_data);
   int64_t (*put)(mos_t_idx_data* idx_data, const uint8_t* key, const size_t key_len, const uint64_t value, mos_idx_put_result* result);
   int64_t (*get)(const mos_t_idx_data* idx_data, const uint8_t* key, const size_t key_len);
   void (*bitmap_search)(const mos_t_idx_data* idx_data, mos_t_qry_bmp* bm, const mos_t_qry_attr_qry* query);
   void (*remove)(mos_t_idx_data* idx_data, const uint8_t* key, const size_t key_len);
} mos_t_idx_ops;

static const mos_t_idx_ops MOS_IDX_OPS_REGISTRY[] = {
    [MOS_IDX_HASH_MAP] = {
        .get_index_size = mos_idx_hmap_size,
        .init_index = mos_idx_hmap_init,
        .put = mos_idx_hmap_put,
        .get = mos_idx_hmap_get,
        .bitmap_search = mos_idx_hmap_bitmap_search,
        .remove = mos_idx_hmap_remove
    },
    [MOS_IDX_HNSW] = {
         .get_index_size = mos_idx_hnsw_size,
         .init_index = mos_idx_hnsw_init,
         .put = mos_idx_hnsw_put,
         .get = mos_idx_hnsw_get,
         .bitmap_search = mos_idx_hnsw_bitmap_search,
         .remove = mos_idx_hnsw_remove
    }
};

/* =========================================================================
   3. FUNCTION DECLARATIONS
   ========================================================================= */

mos_t_idx_ops mos_idx_get_idx_ops(MOS_IDX_TYPE type);
int mos_idx_get_supported_index_query_ops(mos_t_idx* index);
uint64_t mos_idx_data_size(mos_t_config* config);
void mos_idx_create(const mos_t_storage* storage, mos_t_config* mos_config);
void mos_idx_put(const MOS_IDX_TYPE idx_type, mos_t_idx_data* idx_data, const uint8_t* key, const size_t key_len, const uint64_t value, mos_idx_put_result* result);
int64_t mos_idx_get(mos_t_idx_data* id_idx_data, uint8_t* id);

static void mos_idx_bitmap_search(const MOS_IDX_TYPE idx_type, const mos_t_idx_data* idx_data, mos_t_qry_bmp* bm, const mos_t_qry_attr_qry* query) {
    mos_t_idx_ops idx_ops = MOS_IDX_OPS_REGISTRY[idx_type];
    idx_ops.bitmap_search(idx_data, bm, query);
}

static void mos_idx_remove_value(const MOS_IDX_TYPE idx_type, mos_t_idx_data* idx_data, const uint8_t* key, const size_t key_len) {
    mos_t_idx_ops idx_ops = MOS_IDX_OPS_REGISTRY[idx_type];
    idx_ops.remove(idx_data, key, key_len);
}

#endif // MOS_IDX_H
