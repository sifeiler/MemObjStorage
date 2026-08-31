#ifndef MOS_IDX_HNSW_H
#define MOS_IDX_HNSW_H

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include "mos_types_fwd.h"
#include "mos_utils.h"
#include "mos.h"

/* =========================================================================
   1. CONSTANTS, MACROS, ENUMS
   ========================================================================= */

#define MOS_IDX_HNSW_MAX_LAYERS 32      //astronomically low probability that we reach layer 32
#define MOS_IDX_HNSW_LAYER_MARGIN_MULTIPLICATOR 5
#define MOS_IDX_HNSW_MAX_EF_CONSTRUCTION 512
#define MOS_IDX_HNSW_UNIFIED_SIMD_WIDTH 16      //current 

typedef enum mos_idx_t_hnsw_status {
    MOS_IDX_HNSW_OK = 0,
    MOS_IDX_HNSW_ERR_INVALID_ARGS = 1,
    MOS_IDX_HNSW_ERR_EMPTY_INDEX = 2,
    MOS_IDX_HNSW_ERR_FULL_INDEX = 3,
    MOS_IDX_HNSW_NOT_FOUND = 4
} mos_idx_t_hnsw_status;

/* =========================================================================
   2. STRUCTS
   ========================================================================= */

typedef struct mos_t_idx_hnsw_neighbor {
    uint64_t node_id;
    float distance;
    uint8_t _pad[4];
} mos_t_idx_hnsw_neighbor;

typedef struct mos_t_idx_arena_chunk {
    uint16_t neighbors_count;
    uint8_t _pad[6];
    mos_t_idx_hnsw_neighbor neighbors[];
} mos_t_idx_arena_chunk;

typedef struct mos_t_idx_hnsw_header {
    //vectors
    uint64_t vectors_offset;
    //vector_dim rounded up to next multiple of SIMD width
    uint64_t vector_dim_padded;

    // layer 0
    uint64_t l0_nodes_offset;
    uint64_t l0_node_stride;

    // upper layers arena & layers in general
    uint64_t upper_layers_arena_offset;
    uint64_t next_upper_layers_arena_offset;
    uint64_t upper_layers_arena_chunk_stride;
    uint64_t upper_layers_arena_size;
    // internal_id -> external_id mapping
    uint64_t external_ids_offset;
    uint64_t node_count;
    uint64_t node_capacity;
    uint64_t entry_node_id;

    uint16_t vector_dim;

    uint8_t vector_metric;  //MOS_T_IDX_HNSW_METRIC
    uint8_t max_layer_cap;
    uint8_t current_max_layer;
    uint8_t index_empty;    // 0 or 1

    // Explicit 2-byte padding to push graph_config to Byte 96 (8-byte aligned)
    uint8_t _pad[2];

    mos_t_idx_hnsw_graph_config graph_config;
} mos_t_idx_hnsw_header;

typedef struct mos_t_idx_hnsw_graph_l0_node {
    uint64_t arena_offset;
    uint16_t neighbors_count;
    uint8_t max_layer;
    uint8_t _pad[5];
    mos_t_idx_hnsw_neighbor neighbors[];    // 8-byte aligned
} mos_t_idx_hnsw_graph_l0_node;

typedef struct mos_t_idx_hnsw {
   mos_t_idx_hnsw_header index_header;

   /**
    * [vectors]
    * [layer 0 nodes]
    * [upper layers (1..?) arena]
    * [internal_id -> external_id mapping; array of type uint64_t of length index_header.node_capacity]
    */
   uint8_t data[];
} mos_t_idx_hnsw;

/* =========================================================================
   3. FUNCTION DECLARATIONS
   ========================================================================= */

uint64_t mos_idx_hnsw_size(const uint64_t item_count, mos_t_idx* idx);
void mos_idx_hnsw_init(const uint64_t item_count, mos_t_idx* idx, mos_t_idx_data* idx_data);

int64_t mos_idx_hnsw_put(mos_t_idx_data* idx_data, const uint8_t* key, const size_t key_len, const uint64_t value, mos_idx_put_result* result);
int64_t mos_idx_hnsw_get(const mos_t_idx_data* idx_data, const uint8_t* key, const size_t key_len);
void mos_idx_hnsw_remove(mos_t_idx_data* idx_data, const uint8_t* key, const size_t key_len);

/**
 * Searches the index data for vectors that are closest to the query vector and sets a 1 in the bitmap for the closest vectors row_ids.
 */
void mos_idx_hnsw_bitmap_search(const mos_t_idx_data* idx_data, mos_t_qry_bmp* bitmap, const mos_t_qry_attr_qry* attribute_query);

#endif