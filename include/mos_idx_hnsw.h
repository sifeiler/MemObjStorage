#ifndef MOS_IDX_HNSW_H
#define MOS_IDX_HNSW_H

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include "mos_types_fwd.h"
#include "mos_utils.h"

/* =========================================================================
   1. CONSTANTS, MACROS, ENUMS
   ========================================================================= */

#define MOS_IDX_HNSW_MAX_LAYERS 32      //astronomically low probability that we reach layer 32
#define MOS_IDX_HNSW_LAYER_MARGIN_MULTIPLICATOR 5
#define MOS_IDX_HNSW_MAX_EF_CONSTRUCTION 512

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

#pragma pack(push, 1)
typedef struct mos_t_idx_hnsw_graph_config {
    //graph parameters
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
    
    /**
     * Shapes the layer hierarchy; controls how many nodes end up in upper layers
     * Multiplying by mL scales the node distribution in the graph.
     * mL isn't a probability itself - it's a scaling factor that controls how "stretched out" the exponential distribution is,
     *  which in turn controls how many nodes randomly land on higher layers.
     * Smaller mL compresses the distribution toward layer 0 (flatter hierarchy, fewer upper layers used);
     * Larger mL stretches it out (taller hierarchy, more nodes reaching high layers).
     */
    float mL;
} mos_t_idx_hnsw_graph_config;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct mos_t_idx_arena_chunk {
    uint16_t neighbors_count;
    uint64_t neighbors[];
} mos_t_idx_arena_chunk;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct mos_t_idx_hnsw_header {
    //vectors
    uint64_t vectors_offset;
    uint16_t vector_dim;
    uint8_t vector_metric;  //MOS_T_IDX_HNSW_METRIC

    //layer 0
    uint64_t l0_nodes_offset;
    uint64_t l0_node_stride;

    //upper layers arena & layers in general
    uint64_t upper_layers_arena_offset;
    uint64_t next_upper_layers_arena_offset;
    uint64_t upper_layers_arena_chunk_stride;
    uint8_t max_layer_cap;
    uint8_t current_max_layer;

    //graph parameters
    mos_t_idx_hnsw_graph_config graph_config;

    uint64_t node_count;
    uint64_t node_capacity;
    uint64_t entry_node_id;

    bool index_empty;
} mos_t_idx_hnsw_header;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct mos_t_idx_hnsw_graph_l0_node {
    uint64_t arena_offset;
    uint8_t max_layer;
    uint16_t neighbors_count;
    uint64_t neighbors[];
} mos_t_idx_hnsw_graph_l0_node;
#pragma pack(pop)

#pragma pack(push, 1)
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
#pragma pack(pop)

/* =========================================================================
   3. FUNCTION DECLARATIONS
   ========================================================================= */

uint64_t mos_idx_hnsw_size(const uint64_t item_count, mos_t_idx* idx);
void mos_idx_hnsw_init(const uint64_t item_count, mos_t_idx* idx, mos_t_idx_data* idx_data);

int64_t mos_idx_hnsw_put(mos_t_idx_data* idx_data, const uint8_t* key, const size_t key_len, const uint64_t value);
int64_t mos_idx_hnsw_get(mos_t_idx_data* idx_data, const uint8_t* key, const size_t key_len);
void mos_idx_hnsw_remove(mos_t_idx_data* idx_data, const uint8_t* key, const size_t key_len);

/**
 * Searches the index data for vectors that are closest to the query vector and sets a 1 in the bitmap for the closest vectors row_ids.
 */
void mos_idx_hnsw_bitmap_search(const mos_t_idx_data* idx_data, mos_t_qry_bmp* bitmap, const mos_t_qry_attr_qry* attribute_query);

#endif