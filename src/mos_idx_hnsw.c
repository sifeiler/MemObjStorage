#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <math.h>
#include <assert.h>
#include <float.h>

#include "../include/mos_idx_hnsw.h"
#include "../include/mos_idx.h"
#include "../include/mos_internal.h"
#include "../include/mos_utils.h"
#include "../include/mos_types_fwd.h"
#include "../include/mos_math.h"
#include "../include/mos_qry.h"

typedef struct mos_t_idx_hnsw_ptrs {
    mos_t_idx_hnsw* hnsw_idx;
    mos_t_idx_hnsw_header* header;
    uint8_t* data;
    float* vectors;     //vectors == data
    mos_t_idx_hnsw_graph_l0_node* l0_nodes;
    uint8_t* upper_layers_arena;
    uint64_t* external_ids;
} mos_t_idx_hnsw_ptrs;

typedef struct mos_t_idx_hnsw_idx_size {
    uint64_t header_size;
    uint64_t vectors_size;
    uint64_t l0_nodes_size;
    uint64_t upper_layer_arena_size;
    uint64_t internal_external_mapping_size;
    uint64_t index_total_size;
    uint64_t l0_node_stride;
    uint64_t upper_arena_chunk_stride;
} mos_t_idx_hnsw_idx_size;

typedef struct mos_t_idx_hnsw_min_heap {
    mos_t_idx_hnsw_neighbor elements[MOS_IDX_HNSW_MAX_EF_CONSTRUCTION];
    uint16_t element_count;
} mos_t_idx_hnsw_min_heap;

//TODO: check if this is the best option to store the reusable visited-bitmap for search/insert
static _Thread_local uint64_t* visited_bitset = NULL;
static _Thread_local uint64_t  visited_bitset_capacity = 0;  // in bits, i.e. node capacity it was sized for

static void mos_idx_hnsw_ensure_visited_bitset(uint64_t node_capacity) {
    if (visited_bitset_capacity < node_capacity) {
        free(visited_bitset);  // safe no-op if NULL
        uint64_t words = (node_capacity + 63) / 64;
        visited_bitset = calloc(words, sizeof(uint64_t));  //pre-zeroed
        visited_bitset_capacity = node_capacity;
    } else {
        // reuse existing buffer, just clear the portion actually in use
        memset(visited_bitset, 0, ((node_capacity + 63) / 64) * sizeof(uint64_t));
    }
}

static bool mos_idx_hnsw_node_visited(uint64_t node_id) {
    return (visited_bitset[node_id / 64] >> (node_id % 64)) & 1ULL;
}

static void mos_idx_hnsw_set_node_visited(uint64_t node_id) {
    uint64_t word = node_id / 64;
    visited_bitset[word] |= (1ULL << (node_id % 64));
}

void mos_idx_hnsw_min_heap_push(mos_t_idx_hnsw_min_heap* heap, uint64_t node_id, float value) {
    if(heap->element_count >= MOS_IDX_HNSW_MAX_EF_CONSTRUCTION) {
        //heap full
        return;
    }

    mos_t_idx_hnsw_neighbor* elements = heap->elements;

    elements[heap->element_count].node_id = node_id;
    elements[heap->element_count].distance = value;
    heap->element_count++;
    
    uint16_t i = heap->element_count - 1;
    while(i > 0 && elements[(i-1) / 2].distance > elements[i].distance) {
        mos_t_idx_hnsw_neighbor temp = elements[i];
        elements[i] = elements[(i-1) / 2];
        elements[(i-1) / 2] = temp;

        i = (i-1) / 2;
    }
}

int mos_idx_hnsw_min_heap_pop(mos_t_idx_hnsw_min_heap* heap, mos_t_idx_hnsw_neighbor* element_out) {
    if(heap->element_count == 0) {
        element_out = NULL;
        return -1;
    }

    mos_t_idx_hnsw_neighbor* elements = heap->elements;

    uint16_t i = 0;
    *element_out = elements[i];
    elements[i] = elements[heap->element_count - 1];
    heap->element_count--;
    
    while(1) {
        uint16_t left_child_index = 2 * i + 1;
        uint16_t right_child_index = 2 * i + 2;

        uint16_t smallest = i;

        if(left_child_index < heap->element_count && elements[left_child_index].distance < elements[smallest].distance) {
            smallest = left_child_index;
        }
        if(right_child_index < heap->element_count && elements[right_child_index].distance < elements[smallest].distance) {
            smallest = right_child_index;
        }

        if(smallest != i) {
            mos_t_idx_hnsw_neighbor temp = elements[smallest];
            elements[smallest] = elements[i];
            elements[i] = temp;
            i = smallest;
        } else {
            break;
        }
    }
    return 0;
}

/**
 * Pops from the min heap and fills elements_out back to front.
 * This results in a sorted array of element ids ending with the smallest and starting with the biggest.
 * 
 * @param min_heap the heap to pop values from
 * @param elements_out the result output parameter. Required to be of size `min_heap->element_count`
 * @param invert_distance to invert e.g. the result_heap distances, which stores negative distances (to reuse min_heap as max_heap)
 */
void mos_idx_hnsw_sort_min_heap_reversed(
    mos_t_idx_hnsw_min_heap* min_heap,
    mos_t_idx_hnsw_neighbor* elements_out,
    bool invert_distance
) {
    mos_t_idx_hnsw_neighbor element;
    uint16_t element_pos = min_heap->element_count - 1;
    while (mos_idx_hnsw_min_heap_pop(min_heap, &element) == 0 && element_pos >= 0) {
        elements_out[element_pos].node_id = element.node_id;
        elements_out[element_pos].distance = invert_distance ? -element.distance : element.distance;
        element_pos--;
    }
}

// ---- l0 node accessor ----
// l0 nodes are stored as fixed-stride records (stride cached in header, since the
// struct ends in a FAM and sizeof() can't be trusted). Never index this as a plain array.
static inline mos_t_idx_hnsw_graph_l0_node* mos_idx_hnsw_l0_node_at(
    mos_t_idx_hnsw_ptrs* ptrs,
    uint64_t node_id,
    uint64_t l0_node_stride
) {
    uint8_t* base = (uint8_t*)ptrs->l0_nodes;
    return (mos_t_idx_hnsw_graph_l0_node*)(base + node_id * l0_node_stride);
}

// ---- upper-layer arena chunk accessor ----
// Given a node's arena_offset (start of its upper layer block) and a target layer,
// the function returns a live pointer to that layer's chunk.
// Byte arithmetic only -- never add integers directly to a typed chunk pointer,
// since the compiler would scale by sizeof(chunk), which is unreliable for a FAM type.
// The node's first chunk is layer 1.
static inline mos_t_idx_arena_chunk* mos_idx_hnsw_arena_chunk_at(
    mos_t_idx_hnsw_ptrs* ptrs,
    uint64_t node_arena_offset,
    uint8_t layer,
    uint64_t arena_chunk_stride
) {
    assert(layer > 0 && "Upper layer arena chunks are only for layer >= 1");
    uint8_t* base = (uint8_t*)ptrs->upper_layers_arena;
    uint64_t byte_offset = node_arena_offset + (uint64_t)(layer - 1) * arena_chunk_stride;
    return (mos_t_idx_arena_chunk*)(base + byte_offset);
}

// ---- vector accessor ----
static inline const float* mos_idx_hnsw_vector_at(
    mos_t_idx_hnsw_ptrs* ptrs,
    uint64_t node_id,
    uint16_t vector_dim
) {
    return ptrs->vectors + node_id * (uint64_t)vector_dim;
}

//In case this index will support multithreaded reads later, do not use rand() as threads will share similar randomization state.
double mos_idx_hnsw_rand_0_1() {
    return (double)rand() / ((double)RAND_MAX + 1.0);
}

uint8_t mos_idx_hnsw_max_layer(float mL) {
    double rand_0_1 = mos_idx_hnsw_rand_0_1();
    //Making sure rand_0_1 > 0 to avoid undefined ln() behaviour later.
    if(rand_0_1 <= 0.0) {
        rand_0_1 = DBL_MIN;
    }
    //the smaller rand_0_1, the higher max_layer.
    uint64_t layer = floor(-log(rand_0_1) * (double)mL);

    uint8_t max_allowed_layer = MOS_IDX_HNSW_MAX_LAYERS - 1;
    if (layer > max_allowed_layer) {
        return max_allowed_layer;
    }

    return (uint8_t)layer;
}

uint64_t mos_idx_hnsw_upper_layer_arena__layer_chunk_size_per_node(uint32_t m) {
    //the node id + distance per neighbor (m * 16 bytes) + 2 bytes for the current neighbors count + 6 bytes padding
    return m * sizeof(mos_t_idx_hnsw_neighbor) + sizeof(uint64_t);
}

mos_t_idx_hnsw_ptrs mos_idx_hnsw_get_data_ptrs(mos_t_idx_data* index) {
    mos_t_idx_hnsw_ptrs ptrs;
    mos_t_idx_hnsw* hnsw_idx = (mos_t_idx_hnsw*)(((uint8_t*)index) + index->header.index_payload_offset);
    uint8_t* hnsw_idx_base = (uint8_t*)hnsw_idx;
    ptrs.hnsw_idx = hnsw_idx;
    ptrs.header = &hnsw_idx->index_header;
    ptrs.data = (hnsw_idx_base + ptrs.header->vectors_offset);
    ptrs.vectors = (float*)(ptrs.data);
    ptrs.l0_nodes = (mos_t_idx_hnsw_graph_l0_node*)(hnsw_idx_base + hnsw_idx->index_header.l0_nodes_offset);
    ptrs.upper_layers_arena = (uint8_t*)(hnsw_idx_base + hnsw_idx->index_header.upper_layers_arena_offset);
    ptrs.external_ids = (uint64_t*)(hnsw_idx_base + hnsw_idx->index_header.external_ids_offset);
    return ptrs;
}

/**
 * upper layer arena structure:
 *   
 * [arena offset node 0][layer 1: [neighbors_count (max m):neighbors[m]]][layer 2: [neighbors_count (max m):neighbors[m]]][...]
 * [arena offset node 1][layer 1: [neighbors_count (max m):neighbors[m]]][layer 2: [neighbors_count (max m):neighbors[m]]][...]
 * ...
 */
uint64_t mos_idx_hnsw_upper_layer_arena_size(const uint64_t max_node_count, mos_t_idx_hnsw_graph_config* graph_config) {
    assert(graph_config->m >= 2 && "M must be at least 2 to avoid infinite/degenerate layer growth");
    assert(graph_config->mL > 0.0 && "mL must be strictly positive");

    uint8_t current_layer = 1;
    uint64_t arena_byte_size = 0;
    uint64_t layer_node_count = 0;
    uint64_t layer_chunk_size_per_node = mos_idx_hnsw_upper_layer_arena__layer_chunk_size_per_node(graph_config->m);
    while ((layer_node_count = (uint64_t)round((double)max_node_count / exp((double)current_layer / graph_config->mL))) > 0) {
        arena_byte_size += layer_chunk_size_per_node * layer_node_count * MOS_IDX_HNSW_LAYER_MARGIN_MULTIPLICATOR;
        current_layer++;
    }

    uint8_t start_layer = current_layer;
    //3 layers margin
    for (; current_layer < start_layer + 3; current_layer++) {
        arena_byte_size += MOS_IDX_HNSW_LAYER_MARGIN_MULTIPLICATOR * layer_chunk_size_per_node;
    }
    
    return arena_byte_size;
}

mos_t_idx_hnsw_idx_size mos_idx_hnsw_get_index_size_padded(const uint64_t max_node_count, mos_t_idx* idx) {
    assert(idx->type == MOS_IDX_HNSW);
    mos_t_idx_params_hnsw idx_params_hnsw = idx->params.hnsw;
    mos_t_idx_hnsw_graph_config graph_config = idx_params_hnsw.graph_config;

    mos_t_idx_hnsw_idx_size padded_size;

    padded_size.header_size = MOS_ALIGN_UP(sizeof(mos_t_idx_hnsw_header), MOS_PAGE_SIZE);

    //vectors
    uint64_t vector_dim_padded = mos_math_calc_padded_vector_dimension(idx_params_hnsw.vector_dim);
    uint64_t vector_size = sizeof(float) * vector_dim_padded * max_node_count;
    padded_size.vectors_size = MOS_ALIGN_UP(vector_size, MOS_PAGE_SIZE);

    //layer 0 nodes
    padded_size.l0_node_stride = sizeof(mos_t_idx_hnsw_graph_l0_node) + (sizeof(mos_t_idx_hnsw_neighbor) * graph_config.m_max0);
    padded_size.l0_nodes_size = MOS_ALIGN_UP(padded_size.l0_node_stride * max_node_count, MOS_PAGE_SIZE);

    //graph payload upper layers arena
    padded_size.upper_arena_chunk_stride = mos_idx_hnsw_upper_layer_arena__layer_chunk_size_per_node(graph_config.m);
    padded_size.upper_layer_arena_size = MOS_ALIGN_UP(mos_idx_hnsw_upper_layer_arena_size(max_node_count, &graph_config), MOS_PAGE_SIZE);

    //internal_id -> external_id mapping (1:1)
    padded_size.internal_external_mapping_size = MOS_ALIGN_UP(sizeof(uint64_t) * max_node_count, MOS_PAGE_SIZE);

    padded_size.index_total_size = 0;
    padded_size.index_total_size += padded_size.header_size;
    padded_size.index_total_size += padded_size.vectors_size;
    padded_size.index_total_size += padded_size.l0_nodes_size;
    padded_size.index_total_size += padded_size.upper_layer_arena_size;
    padded_size.index_total_size += padded_size.internal_external_mapping_size;

    return padded_size;
}

/**
 * Calculates the byte size of the index.
 * 
 * header: MOS_PAGE_SIZE aligned
 * vectors: MOS_PAGE_SIZE aligned
 * l0_nodes: MOS_PAGE_SIZE aligned
 * upper_layer_arena: MOS_PAGE_SIZE aligned
 * internal_id -> external_id mapping: MOS_PAGE_SIZE aligned
 */
uint64_t mos_idx_hnsw_size(const uint64_t max_node_count, mos_t_idx* idx) {
    assert(idx->type == MOS_IDX_HNSW);
    mos_t_idx_hnsw_idx_size padded_index_size = mos_idx_hnsw_get_index_size_padded(max_node_count, idx);
    return padded_index_size.index_total_size;
}

void mos_idx_hnsw_init(const uint64_t item_count, mos_t_idx* idx, mos_t_idx_data* idx_data) {
    assert(idx_data->header.index.type == MOS_IDX_HNSW);

    mos_t_idx_hnsw_idx_size padded_index_size = mos_idx_hnsw_get_index_size_padded(item_count, idx);

    // Caution! Not all pointers are valid at this point. Offsets needed for hnsw_ptrs are configured in this functions!
    mos_t_idx_hnsw_ptrs hnsw_ptrs = mos_idx_hnsw_get_data_ptrs(idx_data);
    mos_t_idx_hnsw* index = hnsw_ptrs.hnsw_idx;
    mos_t_idx_hnsw_header* index_header = hnsw_ptrs.header;
    mos_t_idx_params_hnsw idx_params_hnsw = idx->params.hnsw;
    mos_t_idx_hnsw_graph_config* graph_config = &idx_params_hnsw.graph_config;

    idx->index_size = padded_index_size.index_total_size;
    index_header->index_empty = true;

    //offsets
    index_header->vectors_offset = padded_index_size.header_size;
    index_header->l0_nodes_offset = index_header->vectors_offset + padded_index_size.vectors_size;
    index_header->upper_layers_arena_offset = index_header->l0_nodes_offset + padded_index_size.l0_nodes_size;
    index_header->external_ids_offset = index_header->upper_layers_arena_offset + padded_index_size.upper_layer_arena_size;
    index->index_header.next_upper_layers_arena_offset = 0;

    //graph
    index_header->current_max_layer = 0;
    memcpy(&index_header->graph_config, graph_config, sizeof(index_header->graph_config));
    index_header->l0_node_stride = padded_index_size.l0_node_stride;
    index_header->upper_layers_arena_chunk_stride = padded_index_size.upper_arena_chunk_stride;
    index_header->upper_layers_arena_size = padded_index_size.upper_layer_arena_size;
    index_header->max_layer_cap = MOS_IDX_HNSW_MAX_LAYERS;
    index_header->node_capacity = item_count;
    index_header->node_count = 0;
    
    //vector
    index_header->vector_dim = idx_params_hnsw.vector_dim;
    index_header->vector_dim_padded = mos_math_calc_padded_vector_dimension(idx_params_hnsw.vector_dim);
    index_header->vector_metric = idx_params_hnsw.vector_metric;

    // now get valid hnsw_ptrs
    mos_t_idx_hnsw_ptrs hnsw_ptrs_valid = mos_idx_hnsw_get_data_ptrs(idx_data);
    memset(hnsw_ptrs_valid.upper_layers_arena, 0, hnsw_ptrs_valid.header->upper_layers_arena_size);
}

uint8_t mos_idx_hnsw_get_neighbors(
    mos_t_idx_hnsw* index,
    mos_t_idx_hnsw_ptrs* ptrs,
    uint64_t node_id,
    uint8_t layer,
    uint64_t layer_chunk_size,
    mos_t_idx_hnsw_neighbor** neighbors_out,
    uint16_t** neighbors_out_count_ptr
) {
    mos_t_idx_hnsw_graph_l0_node* node = mos_idx_hnsw_l0_node_at(ptrs, node_id, index->index_header.l0_node_stride);

    if(layer > node->max_layer) {
        return MOS_IDX_HNSW_NOT_FOUND;
    }

    if(layer == 0) {
        *neighbors_out = node->neighbors;
        *neighbors_out_count_ptr = &node->neighbors_count;
        return MOS_IDX_HNSW_OK;
    }

    mos_t_idx_arena_chunk* node_arena_chunk = mos_idx_hnsw_arena_chunk_at(ptrs, node->arena_offset, layer, layer_chunk_size);

    *neighbors_out = node_arena_chunk->neighbors;
    *neighbors_out_count_ptr = &node_arena_chunk->neighbors_count;

    return MOS_IDX_HNSW_OK;
}

// Returns the single closest node found.
uint64_t mos_idx_hnsw_greedy_search(
    mos_t_idx_hnsw* hnsw,
    mos_t_idx_hnsw_ptrs* ptrs,
    const float* query_vector,
    uint64_t start_node_id,
    uint8_t layer
) {
    const float* start_node_vector = mos_idx_hnsw_vector_at(ptrs, start_node_id, hnsw->index_header.vector_dim);
    float best_distance = mos_math_calc_distance(query_vector, start_node_vector, hnsw->index_header.vector_dim, hnsw->index_header.vector_metric);

    uint64_t closest_node_id = start_node_id;

    while(1) {
        uint64_t prev_closest_node_id = closest_node_id;
        mos_t_idx_hnsw_neighbor* neighbors;
        uint16_t* neighbors_count;
        uint8_t status = mos_idx_hnsw_get_neighbors(hnsw, ptrs, closest_node_id, layer, hnsw->index_header.upper_layers_arena_chunk_stride, &neighbors, &neighbors_count);
        if (status == MOS_IDX_HNSW_OK && neighbors_count != NULL) {
            for(int i = 0; i < *neighbors_count; i++) {
                uint64_t neighbor_node_id = neighbors[i].node_id;
                const float* neighbor_node_vector = mos_idx_hnsw_vector_at(ptrs, neighbor_node_id, hnsw->index_header.vector_dim);

                float new_pot_best_distance = mos_math_calc_distance(query_vector, neighbor_node_vector, hnsw->index_header.vector_dim, hnsw->index_header.vector_metric);
                if(new_pot_best_distance < best_distance) {
                    //found new best
                    best_distance = new_pot_best_distance;
                    closest_node_id = neighbor_node_id;
                }
            }
        }

        if(prev_closest_node_id == closest_node_id) {
            //there was no closer neighbor. closest_node_id is closest node. greedy search done.
            break;
        }
    }

    return closest_node_id;
}

/**
 * Runs ef-sized candidate search at target_layer.
 * Finds a ef-sized set of nearest neighbors to query_vector. Result set is sorted ascending by vector distance.
 * 
 * @param hnsw
 * @param ptrs pre-calculated pointers for easier access
 * @param query_vector must have hnsw->header.vector_dim elements
 * @param start_node_id node to begin the search from (usually the current entry point)
 * @param target_layer layer to run the ef-sized candidate search at
 * @param ef candidate list size (ef_construction at insert, ef at query time)
 * @param neighbors_out caller-owned buffer, must hold at least ef entries
 * @param neighbors_out_capacity size of neighbors_out, for bounds safety
 * @param neighbors_out_count actual number of results written to neighbors_out
 * 
 * @return status about the operations success
 */
mos_idx_t_hnsw_status mos_idx_hnsw_get_nearest_neighbors_ascending_by_distance(
    mos_t_idx_hnsw* hnsw,
    mos_t_idx_hnsw_ptrs* ptrs,
    const float* query_vector,
    uint64_t start_node_id,
    uint8_t target_layer,
    uint16_t ef,
    mos_t_idx_hnsw_neighbor* neighbors_out,
    uint16_t neighbors_out_capacity,
    uint16_t* neighbors_out_count
) {
    // candidates min-heap will store vector distances.
    // The more likely two vectors, the smaller their distance.
    mos_t_idx_hnsw_min_heap candidate_heap = {0};

    // result_heap stores negative vector distances.
    // The smaller the negated distance, the dissimilar the vectors.
    mos_t_idx_hnsw_min_heap result_heap = {0};

    uint64_t curr_closest_node_id = start_node_id;
    const float* curr_closest_vector = mos_idx_hnsw_vector_at(ptrs, curr_closest_node_id, hnsw->index_header.vector_dim);
    float curr_best_distance = mos_math_calc_distance(query_vector, curr_closest_vector, hnsw->index_header.vector_dim, hnsw->index_header.vector_metric);
    mos_idx_hnsw_ensure_visited_bitset(hnsw->index_header.node_capacity);

    mos_idx_hnsw_min_heap_push(&candidate_heap, curr_closest_node_id, curr_best_distance);
    mos_idx_hnsw_min_heap_push(&result_heap, curr_closest_node_id, -curr_best_distance);
    mos_idx_hnsw_set_node_visited(curr_closest_node_id);

    mos_t_idx_hnsw_neighbor c;
    while(mos_idx_hnsw_min_heap_pop(&candidate_heap, &c) == 0) {
        const float* c_vector = mos_idx_hnsw_vector_at(ptrs, c.node_id, hnsw->index_header.vector_dim);
        //negate it again as result_heap stores distances negated to reuse min-heap
        float current_worst_distance = result_heap.element_count > 0 ? -result_heap.elements[0].distance : FLT_MAX;
        if(c.distance > current_worst_distance) {
            break;
        }

        mos_t_idx_hnsw_neighbor* neighbors;
        uint16_t* neighbors_count;
        uint8_t status = mos_idx_hnsw_get_neighbors(hnsw, ptrs, c.node_id, target_layer, hnsw->index_header.upper_layers_arena_chunk_stride, &neighbors, &neighbors_count);
        if (status == MOS_IDX_HNSW_OK && neighbors_count != NULL) {
            //add unvisited neighbors of current candidate
            for(uint16_t i = 0; i < *neighbors_count; i++) {
                uint64_t neighbor_node_id = neighbors[i].node_id;
                if (mos_idx_hnsw_node_visited(neighbor_node_id)) {
                    continue;
                }
                mos_idx_hnsw_set_node_visited(neighbor_node_id);

                current_worst_distance = result_heap.element_count > 0 ? -result_heap.elements[0].distance : FLT_MAX;

                const float* neighbor_vector = mos_idx_hnsw_vector_at(ptrs, neighbor_node_id, hnsw->index_header.vector_dim);
                float neighbor_distance = mos_math_calc_distance(query_vector, neighbor_vector, hnsw->index_header.vector_dim, hnsw->index_header.vector_metric);
                
                if(neighbor_distance < current_worst_distance || result_heap.element_count < ef) {
                    mos_idx_hnsw_min_heap_push(&candidate_heap, neighbor_node_id, neighbor_distance);

                    if(result_heap.element_count >= ef) {
                        mos_t_idx_hnsw_neighbor evicted;
                        mos_idx_hnsw_min_heap_pop(&result_heap, &evicted);
                    }
                    mos_idx_hnsw_min_heap_push(&result_heap, neighbor_node_id, -neighbor_distance);
                }
            }
        }
    }

    *neighbors_out_count = result_heap.element_count;
    // write result heap to neighbors_out; sorted by value and asc, so closest neighbors first
    mos_idx_hnsw_sort_min_heap_reversed(&result_heap, neighbors_out, 1);
    return MOS_IDX_HNSW_OK;
}

/**
 * Iterates over candidates. If a candidate c1 at index 1 is closer to a candidate c0 at index 0, than it is to the query vector, c1 is discarded.
 * Heuristics provide a well distributed graph. 
 * For heuristics to work properly, the candidate set has to be sorted ascending by vector distance.
 * 
 * @param hnsw
 * @param ptrs pre-calculated pointers for easier access
 * @param query_vector must have hnsw->header.vector_dim elements
 * @param candidates node to begin the search from (usually the current entry point)
 * @param candidate_count layer to run the ef-sized candidate search at
 * @param selected_out caller-owned buffer, must hold at least candidate_count entries
 * @param selected_out_capacity size of selected_out, for bounds safety
 * @param selected_out_count actual number of results written to selected_out
 * @param discarded_out caller-owned buffer, must hold at least candidate_count entries
 * @param discarded_out_capacity size of discarded_out, for bounds safety
 * @param discarded_out_count actual number of results written to discarded_out
 */
void mos_idx_hnsw_select_neighbors_heuristic(
    mos_t_idx_hnsw* hnsw,
    mos_t_idx_hnsw_ptrs* ptrs,
    const float* query_vector, 
    mos_t_idx_hnsw_neighbor* candidates, 
    uint16_t candidate_count,
    mos_t_idx_hnsw_neighbor* selected_out, uint16_t selected_out_capacity, uint16_t* selected_out_count,
    mos_t_idx_hnsw_neighbor* discarded_out, uint16_t discarded_out_capacity, uint16_t* discarded_out_count
) {
    assert(selected_out != NULL);
    assert(selected_out_count != NULL);
    assert(selected_out_capacity > 0);

    // If discarded_out is requested, ensure its capacity can fit candidate_count
    if (discarded_out != NULL) {
        assert(discarded_out_capacity >= candidate_count);
        assert(discarded_out_count != NULL);
    }

    *selected_out_count = 0;
    *discarded_out_count = 0;

    if (candidate_count == 0) {
        return;
    }

    for(uint16_t i = 0; i < candidate_count; i++) {
        mos_t_idx_hnsw_neighbor c = candidates[i];
        const float* c_vector = mos_idx_hnsw_vector_at(ptrs, c.node_id, hnsw->index_header.vector_dim);
        //float c_q_distance = mos_math_calc_distance(c_vector, query_vector, hnsw->index_header.vector_dim, hnsw->index_header.vector_metric);
        bool valid_candidate = true;
        for (int j = 0; j < *selected_out_count; j++) {
            mos_t_idx_hnsw_neighbor selected_node = selected_out[j];
            const float* s_vector = mos_idx_hnsw_vector_at(ptrs, selected_node.node_id, hnsw->index_header.vector_dim);
            float c_s_distance = mos_math_calc_distance(c_vector, s_vector, hnsw->index_header.vector_dim, hnsw->index_header.vector_metric);
            if(c_s_distance < c.distance) {
                valid_candidate = false;
                break;
            }
        }
        
        if (valid_candidate) {
            if (*selected_out_count < selected_out_capacity) {
                selected_out[(*selected_out_count)++] = c;
            }
        } else {
            if(discarded_out != NULL) {
                if (*discarded_out_count < discarded_out_capacity) {
                    discarded_out[(*discarded_out_count)++] = c;
                }
            }
        }
    }
}

/**
 * Connects node to neighbors and vice versa.
 * If a neighbors list is already full, heuristic search will be applied to neighbors_count + 1 (new neighbor) neighbors selecting best fitting neighbors.
 * 
 * @param index
 * @param hnsw_ptrs
 * @param node_id
 * @param layer
 * @param neighbors
 * @param neighbors_count
 * @param is_back_link      to break recursion on back-edge connection
 */
void mos_idx_hnsw_connect_neighbors(
    mos_t_idx_hnsw* index,
    mos_t_idx_hnsw_ptrs* hnsw_ptrs,
    uint64_t node_id,
    uint8_t layer,
    mos_t_idx_hnsw_neighbor* new_neighbors,
    uint16_t new_neighbors_count,
    bool is_back_link
) {
    uint16_t m = layer == 0 ? index->index_header.graph_config.m_max0 : index->index_header.graph_config.m;

    mos_t_idx_hnsw_neighbor* current_neighbors;
    uint16_t* current_neighbors_count;
    uint8_t status = mos_idx_hnsw_get_neighbors(index, hnsw_ptrs, node_id, layer, index->index_header.upper_layers_arena_chunk_stride, &current_neighbors, &current_neighbors_count);
    
    if (status != MOS_IDX_HNSW_OK || current_neighbors_count == NULL) {
        return;
    }
    
    uint64_t total_neighbors_count = *current_neighbors_count + new_neighbors_count;

    //do the neighbors fit?
    if(total_neighbors_count <= m) {
        memcpy(&current_neighbors[*current_neighbors_count], new_neighbors, sizeof(mos_t_idx_hnsw_neighbor) * new_neighbors_count);

        *current_neighbors_count = (uint16_t)total_neighbors_count;

        // Add back-edges from each new neighbor back to node_id
        if(!is_back_link) {
            for (uint16_t i = 0; i < new_neighbors_count; i++) {
                mos_t_idx_hnsw_neighbor back_ref;
                back_ref.node_id = node_id;
                back_ref.distance = new_neighbors[i].distance;

                // Recursively connect back_ref to the neighbor
                mos_idx_hnsw_connect_neighbors(index, hnsw_ptrs, new_neighbors[i].node_id, layer, &back_ref, 1, true);
            }
        }
    } else {
        //new neighbors will not fit. Let heuristic search do the job.
        mos_t_idx_hnsw_neighbor total_neighbors[total_neighbors_count];
        memcpy(total_neighbors, current_neighbors, sizeof(mos_t_idx_hnsw_neighbor) * *current_neighbors_count);
        memcpy(&total_neighbors[*current_neighbors_count], new_neighbors, sizeof(mos_t_idx_hnsw_neighbor) * new_neighbors_count);

        const float* node_vector = mos_idx_hnsw_vector_at(hnsw_ptrs, node_id, index->index_header.vector_dim);
        uint16_t final_selected_neighbors_count = 0;
        mos_t_idx_hnsw_neighbor final_selected_neighbors[total_neighbors_count];
        uint16_t final_discarded_neighbors_count = 0;
        mos_t_idx_hnsw_neighbor final_discarded_neighbors[total_neighbors_count];
        mos_idx_hnsw_select_neighbors_heuristic(
            index,
            hnsw_ptrs,
            node_vector,
            total_neighbors,
            total_neighbors_count,
            final_selected_neighbors,
            m,
            &final_selected_neighbors_count,
            final_discarded_neighbors,
            total_neighbors_count,
            &final_discarded_neighbors_count
        );
        memcpy(current_neighbors, final_selected_neighbors, sizeof(mos_t_idx_hnsw_neighbor) * final_selected_neighbors_count);
        *current_neighbors_count = final_selected_neighbors_count;

        //handle discarded
        for (uint16_t i = 0; i < final_discarded_neighbors_count; i++) {
            uint64_t discarded_node_id = final_discarded_neighbors[i].node_id;
            uint16_t* current_discarded_neighbors_count;
            mos_t_idx_hnsw_neighbor* current_discarded_neighbors;
            uint8_t status = mos_idx_hnsw_get_neighbors(index, hnsw_ptrs, discarded_node_id, layer, index->index_header.upper_layers_arena_chunk_stride, &current_discarded_neighbors, &current_discarded_neighbors_count);
            if (status == MOS_IDX_HNSW_OK && current_discarded_neighbors_count != NULL) {
                for(uint16_t j = 0; j < *current_discarded_neighbors_count; j++) {
                    if(current_discarded_neighbors[j].node_id == node_id) {
                        current_discarded_neighbors[j] = current_discarded_neighbors[*current_discarded_neighbors_count - 1];
                        (*current_discarded_neighbors_count)--;
                    }
                }
            }
        }

        //handle selected
        for(uint16_t i = 0; i < final_selected_neighbors_count; i++) {
            mos_t_idx_hnsw_neighbor candidate = final_selected_neighbors[i];

            bool is_newly_added = false;
            for (uint16_t j = 0; j < new_neighbors_count; j++) {
                if (candidate.node_id == new_neighbors[j].node_id) {
                    is_newly_added = true;
                    break;   // found it, no need to keep scanning new_neighbors
                }
            }

            if (is_newly_added) {
                mos_t_idx_hnsw_neighbor neighbor_back_ref;
                neighbor_back_ref.node_id = node_id;
                neighbor_back_ref.distance = candidate.distance;
                if(!is_back_link) {
                    mos_idx_hnsw_connect_neighbors(index, hnsw_ptrs, candidate.node_id, layer, &neighbor_back_ref, 1, true);
                }
            }
        }
    }
}

/**
 * Takes a vector, creates a node for it and stores the node in the hnsw graph.
 * 
 * @param idx_data the index to store the vector in
 * @param key the float vector of dimension key_len / sizeof(float)
 * @param key_len the byte-length of the key
 * @param value the external id of the key
 * 
 * @return mos_idx_t_hnsw_status
 */
int64_t mos_idx_hnsw_put(mos_t_idx_data* idx_data, const uint8_t* key, const size_t key_len, const uint64_t value, mos_idx_put_result* result) {
    assert(idx_data->header.index.type == MOS_IDX_HNSW);

    mos_t_idx_hnsw_ptrs hnsw_ptrs = mos_idx_hnsw_get_data_ptrs(idx_data);
    mos_t_idx_hnsw* index = hnsw_ptrs.hnsw_idx;
    mos_t_idx_hnsw_header* index_header = hnsw_ptrs.header;
    
    uint16_t dims = index_header->vector_dim;
    assert(key_len == (size_t)dims * sizeof(float));
    assert(index_header->node_count <= index_header->node_capacity);

    if(index_header->node_count == index_header->node_capacity) {
        printf("HNSW index is full. Cannot put any more vectors.");
        return MOS_IDX_HNSW_ERR_FULL_INDEX;
    }

    mos_t_idx_hnsw_graph_config graph_config = index_header->graph_config;

    uint64_t new_node_id = index_header->node_count;
    
    //at this point, there is room for another vector
    float query_vector[dims];
    memcpy(query_vector, key, key_len);

    uint64_t current_entry_node_id;
    uint64_t final_entry_node_id;
    uint8_t active_layer;
    uint8_t new_node_max_layer = mos_idx_hnsw_max_layer(graph_config.mL);
    uint8_t final_max_layer;

    //add new node to layer 0. Node is not connected to any other nodes yet.
    mos_t_idx_hnsw_graph_l0_node* node = mos_idx_hnsw_l0_node_at(&hnsw_ptrs, new_node_id, index_header->l0_node_stride);

    if(new_node_max_layer > 0) {
        uint64_t chunk_size = new_node_max_layer * index_header->upper_layers_arena_chunk_stride;
        uint64_t new_node_arena_offset = index_header->next_upper_layers_arena_offset;
        if (new_node_arena_offset + chunk_size > index_header->upper_layers_arena_size) {
            return MOS_IDX_HNSW_ERR_FULL_INDEX; // or a distinct ARENA_FULL code
        }

        // Clear arena for new node.
        // Neighbor counters for every upper layer of new node are set to 0!
        memset(hnsw_ptrs.upper_layers_arena + new_node_arena_offset, 0, chunk_size);
        index_header->next_upper_layers_arena_offset += chunk_size;
        node->arena_offset = new_node_arena_offset;
    } else {
        node->arena_offset = 0;
    }

    node->max_layer = new_node_max_layer;
    node->neighbors_count = 0;

    // Return early if this is the very first entry.
    if(index_header->index_empty) {
        assert(index_header->node_count == 0);
        assert(index_header->current_max_layer == 0);
        hnsw_ptrs.external_ids[new_node_id] = value;
        float* new_node_id_vector = mos_idx_hnsw_vector_at(&hnsw_ptrs, new_node_id, dims);
        memcpy(new_node_id_vector, query_vector, sizeof(float) * dims);
        index_header->entry_node_id = new_node_id;
        index_header->current_max_layer = new_node_max_layer;
        index_header->index_empty = false;
    } else {
        assert(index_header->node_count > 0);
        assert(index_header->current_max_layer >= 0);

        hnsw_ptrs.external_ids[new_node_id] = value;
        float* new_node_id_vector = mos_idx_hnsw_vector_at(&hnsw_ptrs, new_node_id, dims);
        memcpy(new_node_id_vector, query_vector, sizeof(float) * dims);
        
        // First stage lookup: find best entry node for nearest neighbor search
        if(new_node_max_layer < index_header->current_max_layer) {
            current_entry_node_id = index_header->entry_node_id;
            active_layer = index_header->current_max_layer;
            final_entry_node_id = index_header->entry_node_id;
            final_max_layer = index_header->current_max_layer;

            // Iterate from current_max_layer down to new_node_max_layer + 1 and find nearest node to query_vector.
            // This is a creedy search, so it does not consider a candidate list per layer but just the entry_node_ids neighbors.
            // Start with entry_node_id at current_max_layer.
            for(; active_layer > new_node_max_layer; active_layer--) {
                current_entry_node_id = mos_idx_hnsw_greedy_search(index, &hnsw_ptrs, query_vector, current_entry_node_id, active_layer);
            }
        } else {
            current_entry_node_id = index_header->entry_node_id;
            active_layer = index_header->current_max_layer;
            final_entry_node_id = new_node_id;
            final_max_layer = new_node_max_layer;
        }
        uint16_t m_max = active_layer == 0 ? graph_config.m_max0 : graph_config.m;

        // Second stage lookup: find nearest neighbors to query vector. Start at entry_node_id and active_layer.
        // This is a candidate search down to and including layer 0.
        for(; active_layer >= 0; active_layer--) {
            mos_t_idx_hnsw_neighbor neighbors[graph_config.ef_construction];
            uint16_t neighbors_count = 0;
            if(mos_idx_hnsw_get_nearest_neighbors_ascending_by_distance(
                index,
                &hnsw_ptrs,
                query_vector,
                current_entry_node_id,
                active_layer,
                graph_config.ef_construction,
                neighbors,
                graph_config.ef_construction,
                &neighbors_count) == MOS_IDX_HNSW_OK)
            {
                //apply heuristics
                mos_t_idx_hnsw_neighbor selected_neighbors[neighbors_count];
                uint16_t selected_count = 0;
                mos_t_idx_hnsw_neighbor discarded_neighbors[neighbors_count];
                uint16_t discarded_count = 0;
                mos_idx_hnsw_select_neighbors_heuristic(
                    index,
                    &hnsw_ptrs,
                    query_vector,
                    neighbors,
                    neighbors_count,
                    selected_neighbors,
                    m_max,
                    &selected_count,
                    discarded_neighbors,
                    neighbors_count,
                    &discarded_count
                );

                if(selected_count > 0) {
                    //connect neighbors
                    mos_idx_hnsw_connect_neighbors(index, &hnsw_ptrs, new_node_id, active_layer, selected_neighbors, selected_count, false);
                    // Update entry point for next layer down to the closest neighbor
                    current_entry_node_id = selected_neighbors[0].node_id;
                } else {
                    // Fallback if heuristic rejected all: pick closest candidate
                    current_entry_node_id = neighbors[0].node_id;
                }
            }

            //active_layer is unsigned so break before infinite loop :)
            if(active_layer == 0) {
                break;
            }
        }

        index_header->entry_node_id = final_entry_node_id;
        index_header->current_max_layer = final_max_layer;
        index_header->index_empty = false;
    }

    index_header->node_count++;
    if(result) {
        result->put_result = malloc(sizeof(new_node_id));
        if(!result->put_result) {
            mos_utils_report_error("Allocation failure for HNSW put result.");
            return MOS_IDX_HNSW_ERR_INVALID_ARGS;
        }
        memcpy(result->put_result, &new_node_id, sizeof(new_node_id));
        result->byte_size = sizeof(new_node_id);
    }

    return MOS_IDX_HNSW_OK;
}

int64_t mos_idx_hnsw_get(const mos_t_idx_data* idx_data, const uint8_t* key, const size_t key_len) {
    assert(idx_data->header.index.type == MOS_IDX_HNSW);

    return -1;
}

void mos_idx_hnsw_remove(mos_t_idx_data* idx_data, const uint8_t* key, const size_t key_len) {
    assert(idx_data->header.index.type == MOS_IDX_HNSW);

}

/**
 * Searches the index data for vectors that match the query and sets a 1 in the bitmap for the matching vectors row_id.
 */
void mos_idx_hnsw_bitmap_search(const mos_t_idx_data* idx_data, mos_t_qry_bmp* bitmap, const mos_t_qry_attr_qry* attribute_query) {
    assert(idx_data->header.index.type == MOS_IDX_HNSW);

    mos_t_idx_hnsw_ptrs hnsw_ptrs = mos_idx_hnsw_get_data_ptrs(idx_data);
    mos_t_idx_hnsw* index = hnsw_ptrs.hnsw_idx;

    mos_t_idx_hnsw_header* index_header = hnsw_ptrs.header;
    mos_t_attr_value_vector search_vector = attribute_query->value.vector_val;
    uint16_t ef = search_vector.ef;
    uint16_t k = search_vector.top_k;

    uint16_t dims = index_header->vector_dim;
    assert(search_vector.vector_dim == dims);
    assert(strcmp(idx_data->header.index.attribute_name, attribute_query->attribute_name) == 0);

    float query_vector[dims];
    memcpy(query_vector, search_vector.vector_val, sizeof(float) * search_vector.vector_dim);

    uint64_t nearest_node = index_header->entry_node_id;
    for(uint8_t i = index_header->current_max_layer; i > 0; i--) {
        nearest_node = mos_idx_hnsw_greedy_search(index, &hnsw_ptrs, query_vector, nearest_node, i);
    }

    //ef based search on layer 0, starting with nearest_node
    mos_t_idx_hnsw_neighbor nearest_neighbors[ef];
    uint16_t nearest_neighbors_count = 0;
    mos_idx_hnsw_get_nearest_neighbors_ascending_by_distance(index, &hnsw_ptrs, query_vector, nearest_node, 0, ef, nearest_neighbors, ef, &nearest_neighbors_count);

    //select top k
    uint16_t result_count = k < nearest_neighbors_count ? k : nearest_neighbors_count;

    //set bitmap values
    for(uint16_t i = 0; i < result_count; i++) {
        if(nearest_neighbors[i].distance <= search_vector.threshold) {
            uint64_t node_id = nearest_neighbors[i].node_id;
            uint64_t row_id = hnsw_ptrs.external_ids[node_id];
            mos_qry_bmp_set_row_id_bit(bitmap, row_id);
        }
    }
}