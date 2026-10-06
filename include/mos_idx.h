#ifndef MOS_IDX_H
#define MOS_IDX_H

#include <stdint.h>
#include "mos_types_fwd.h"
#include "mos_internal.h"
#include "mos_idx_hmap.h"
#include "mos_idx_hnsw.h"

/* =========================================================================
   1. CONSTANTS, MACROS, ENUMS
   ========================================================================= */

/* =========================================================================
   2. STRUCTS
   ========================================================================= */

typedef struct mos_t_idx_data_header {
   mos_t_idx_descriptor index_desc;
   uint64_t index_payload_offset;   //offset of index_payload in mos_t_idx_data
} mos_t_idx_data_header;

typedef struct mos_t_idx_data {
   mos_t_idx_data_header header;
   //the actual index: hash_map, hnsw, ...
   uint8_t index_payload[];
} mos_t_idx_data;

typedef struct mos_t_idx_put_result {
   uint64_t byte_size;
   uint8_t* put_result;
} mos_t_idx_put_result;

typedef struct mos_t_idx_ops {
   uint64_t (*get_index_size)(const uint64_t item_count, mos_t_idx_descriptor* index_desc);
   void (*init_index)(const uint64_t item_count, mos_t_idx_descriptor* index_desc, mos_t_idx_data* idx_data);
   int (*put)(const mos_t_idx_context* idx_context, const uint8_t* key, const size_t key_len, const uint64_t value, mos_t_idx_put_result* result);
   int (*get)(const mos_t_idx_context* idx_context, const uint8_t* key, const size_t key_len, mos_t_id_list* result_list_out);
   void (*bitmap_search)(const mos_t_idx_context* idx_context, mos_t_qry_bmp* bm, const mos_t_qry_attr_qry* query);
   int (*remove)(const mos_t_idx_context* idx_context, const uint8_t* key, const size_t key_len);
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
int mos_idx_get_supported_index_query_ops(mos_t_idx_descriptor* index);
uint64_t mos_idx_data_size(mos_t_config* config);
void mos_idx_create(const mos_t_storage* storage, mos_t_config* mos_config);
int mos_idx_put(const mos_t_idx_context* idx_context, const uint8_t* key, const size_t key_len, const uint64_t value, mos_t_idx_put_result* result);
int mos_idx_get(const mos_t_idx_context* idx_context, uint8_t* id, mos_t_id_list* result_list_out);
void mos_idx_set_field_index_size(uint64_t max_records, mos_t_idx_descriptor* idx_desc);

static void mos_idx_bitmap_search(const mos_t_idx_context* idx_context, mos_t_qry_bmp* bm, const mos_t_qry_attr_qry* query) {
   mos_t_idx_ops idx_ops = MOS_IDX_OPS_REGISTRY[idx_context->idx_type];
   idx_ops.bitmap_search(idx_context, bm, query);
}

static int mos_idx_remove_value(const mos_t_idx_context* idx_context, const uint8_t* key, const size_t key_len) {
   mos_t_idx_ops idx_ops = MOS_IDX_OPS_REGISTRY[idx_context->idx_type];
   return idx_ops.remove(idx_context, key, key_len);
}

static inline mos_t_idx_data* mos_accessor_idx_data(mos_t_mapped_region* regions, uint16_t regions_count, uint16_t i) {
   if(i >= regions_count) {
      return NULL;
   }
   return (mos_t_idx_data*) (regions[i]).region_base;
}

#endif // MOS_IDX_H
