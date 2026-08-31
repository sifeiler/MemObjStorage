#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <math.h>

#include "../include/mos_idx.h"
#include "../include/mos_idx_hnsw.h"
#include "../include/mos_math.h"
#include "../include/mos_internal.h"
#include "../include/mos_os.h"
#include "../include/mos_qry.h"

#include "mos_utils_fuzzy.h"

#define MAX_VECT_DIM 512

typedef struct mos_t_fuzzy_hnsw_element {
    uint64_t node_id;
    float dist;
} mos_t_fuzzy_hnsw_element;

int compare_dist_pairs(const void *a, const void *b) {
    float da = ((const mos_t_fuzzy_hnsw_element *)a)->dist;
    float db = ((const mos_t_fuzzy_hnsw_element *)b)->dist;
    
    if (da < db) return -1;
    if (da > db) return 1;
    return 0;
}

static uint8_t bit_is_set(uint64_t* data, uint64_t bit_id) {
    uint64_t word = bit_id / 64;
    return (data[word] >> (bit_id % 64)) & 1ULL;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    size_t min_bytes_needed_per_iteration = 0;

    min_bytes_needed_per_iteration += 1;  //item_count
    min_bytes_needed_per_iteration += 1;  //graph_config.m;
    min_bytes_needed_per_iteration += 1;  //graph_config.ef_construction;
    min_bytes_needed_per_iteration += 1;  //graph_config.ml;
    min_bytes_needed_per_iteration += 2;  //vector_dim;

    min_bytes_needed_per_iteration += 1;  //top_k;
    min_bytes_needed_per_iteration += 1;  //threshold;
    min_bytes_needed_per_iteration += 1;  //ef_search;

    size_t pos = 0;
    while (pos + min_bytes_needed_per_iteration <= size)
    {
        uint8_t item_count = data[pos++];
        if(item_count == 0) {
            item_count = 1;
        }

        uint8_t m = data[pos++];
        if (m < 2) {
            m = 2; // Minimum connections required for small-world connectivity
        } else if (m > 127) {
            m = 127; // Prevents (m * 2) from wrapping around uint8_t
        }

        uint8_t m_max0 = m * 2;

        // ef_construction must be >= m_max0
        uint8_t ef_construction = data[pos++];
        if (ef_construction < m_max0) {
            ef_construction = m_max0; // Candidate search pool must be at least as large as neighbors kept
        }

        float mL = (float)data[pos++] / 255.0f;
        if (mL <= 0.0f) {
            mL = 1.0f / logf((float)m); // Standard HNSW default scaling factor
        }

        // Parse vector_dim (Endian-safe 16-bit read)
        uint16_t vector_dim = (uint16_t)data[pos] | ((uint16_t)data[pos + 1] << 8);
        pos += 2;
        if (vector_dim == 0) {
            vector_dim = 1; // Vectors cannot have 0 dimensions
        }

        uint8_t top_k = data[pos++];
        if (top_k == 0) {
            top_k = 1; // Cannot search for 0 elements
        }

        float threshold = (float)data[pos++] / 255.0f;

        // ef_search >= top_k
        uint8_t ef_search = data[pos++];
        if (ef_search < top_k) {
            ef_search = top_k;
        }

        size_t vector_byte_size = vector_dim * sizeof(float);
        size_t vector_space_size = vector_byte_size * (item_count + 1); //+1 for the query vector
        if(pos + vector_space_size > size) {
            break;
        }

        // Allocations
        float* vectors = malloc(item_count * vector_byte_size);
        mos_t_fuzzy_hnsw_element* ground_truth = malloc(item_count * sizeof(mos_t_fuzzy_hnsw_element));
        if (!vectors || !ground_truth) {
            free(vectors);
            free(ground_truth);
            break;
        }

        mos_t_idx hnsw_idx = {
            .id = 1,
            .index_offset = 0,
            .type = MOS_IDX_HNSW,
            .params.hnsw = {
                .vector_dim = vector_dim,
                .vector_metric = METRIC_COSINE,
                .graph_config = {
                    .m = m,
                    .m_max0 = m_max0,
                    .ef_construction = ef_construction,
                    .mL = mL
                }
            },
            .attribute_name = "vector"
        };

        uint64_t idx_size = mos_idx_hnsw_size(item_count, &hnsw_idx);
        uint64_t idx_data_header_size_page_aligned = MOS_ALIGN_UP(sizeof(mos_t_idx_data_header), MOS_PAGE_SIZE);
        uint64_t idx_data_size = idx_data_header_size_page_aligned + idx_size;

        mos_t_idx_data* idx_data = (mos_t_idx_data*)calloc(1, idx_data_size);
        if (!idx_data) {
            free(vectors);
            free(ground_truth);
            break;
        }
        idx_data->header.index = hnsw_idx;
        idx_data->header.index_payload_offset = idx_data_header_size_page_aligned;
        mos_idx_hnsw_init(item_count, &idx_data->header.index, idx_data);

        float query_vector[vector_dim];
        memcpy(query_vector, &data[pos], vector_byte_size);
        mos_math_sanitize_and_normalize(query_vector, vector_dim);
        pos += vector_byte_size;

        //everything is set up. Now read vectors and put them to hnsw
        for(uint8_t i = 0; i < item_count; i++) {
            float vector[vector_dim];
            memcpy(vector, &data[pos], vector_byte_size);
            mos_math_sanitize_and_normalize(vector, vector_dim);
            pos += vector_byte_size;

            mos_idx_put_result result;
            mos_idx_hnsw_put(idx_data, (uint8_t*)vector, vector_byte_size, i, &result);

            //for assertion
            memcpy(&vectors[i * vector_dim], vector, vector_byte_size);
            assert(result.byte_size <= sizeof(ground_truth[i].node_id));
            memcpy(&ground_truth[i].node_id, result.put_result, result.byte_size);
            //ground_truth[i].node_id = result.put_result;
            ground_truth[i].dist = mos_math_calc_distance(query_vector, vector, vector_dim, METRIC_COSINE);
            free(result.put_result);
        }

        // Sort ascending by distance
        qsort(ground_truth, item_count, sizeof(mos_t_fuzzy_hnsw_element), compare_dist_pairs);

        // Calculate expected_count considering threshold filtering
        uint32_t valid_threshold_count = 0;
        for (uint32_t i = 0; i < item_count; i++) {
            if (ground_truth[i].dist <= threshold) {
                valid_threshold_count++;
            } else {
                break;
            }
        }
        uint32_t expected_count = (valid_threshold_count < top_k) ? valid_threshold_count : top_k;

        mos_t_qry_attr_qry qry = {
            .attribute_name = "vector",
            .value = {
                .byte_length = vector_byte_size,
                .vector_val = {
                    .ef = ef_search,
                    .threshold = threshold,
                    .top_k = top_k,
                    .vector_dim = vector_dim,
                    .vector_val = (float*)query_vector
                }
            }
        };

        uint64_t nWords = (item_count + 63) / 64;
        mos_t_qry_bmp* bmp = NULL;
        mos_os_mem_alloc_aligned((void**)&bmp, sizeof(mos_t_qry_bmp) + nWords * sizeof(uint64_t), 64);

        bmp->empty = 1;
        bmp->full = 0;
        bmp->nBits = item_count;
        bmp->nWords = nWords;
        memset(bmp->data, 0, nWords * sizeof(uint64_t));

        mos_idx_bitmap_search(MOS_IDX_HNSW, idx_data, bmp, &qry);

        uint64_t actual_count = mos_qry_bmp_count_ones(bmp);

        // ASSERTIONS
        assert(actual_count == expected_count && "Bitmap popcount must equal min(top_k, total_vectors)");

        if (expected_count == 0) {
            assert(actual_count == 0 && "Bitmap must be empty when no vectors fall within the distance threshold");
        } else {
            float max_allowed_distance = ground_truth[expected_count - 1].dist;
            uint32_t matches_in_ground_truth = 0;
            float epsilon = 1e-5f; // Handles floating-point precision loss

            for (uint64_t node_id = 0; node_id < item_count; node_id++) {
                if (bit_is_set(bmp->data, node_id)) {
                    float actual_dist = mos_math_calc_distance(query_vector, &vectors[node_id * vector_dim], vector_dim, METRIC_COSINE);

                    // No returned item should be significantly worse than the actual k-th distance.
                    assert(actual_dist <= max_allowed_distance + epsilon && 
                        "HNSW returned a vector strictly worse than the ground-truth k-th distance boundary!");

                    // Count as a match if this node's distance falls within the valid top-k boundary
                    if (actual_dist <= max_allowed_distance + epsilon) {
                        matches_in_ground_truth++;
                    }
                }
            }

            // If ef_search is adequately tuned (e.g., ef_search >= top_k), recall should meet a target threshold.
            float recall = (float)matches_in_ground_truth / (float)expected_count;
            assert(recall >= 0.80f && "HNSW recall dropped below acceptable fuzzing threshold!");
        }
        
        mos_os_mem_free_aligned(bmp);
        free(ground_truth);
        free(idx_data);
        free(vectors);
    }
    return 0;
}