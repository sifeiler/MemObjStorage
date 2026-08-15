#ifndef MOS_MATH_H
#define MOS_MATH_H

#include <stdint.h>
#include <immintrin.h>

/* =========================================================================
   1. CONSTANTS, MACROS, ENUMS
   ========================================================================= */

typedef enum MOS_T_IDX_HNSW_METRIC {
    /**
     * Eucledian distance
     * Measures straight-line distance between two points.
     * Ideal when vector magnitude matters (e.g., image features)
     */
    METRIC_L2 = 0,

    /**
     * Measures the angle between vectors while ignoring magnitude.
     * Standard choice for text embeddings where direction defines semantic meaning.
     */
    METRIC_COSINE = 1,

    /**
     * Dot Product.
     * Measures the projection of one vector onto another.
     * Frequently used for normalized vectors.
     */
    METRIC_DOT_PRODUCT = 2
} MOS_T_IDX_HNSW_METRIC;

/* =========================================================================
   2. STRUCTS
   ========================================================================= */

/* =========================================================================
   3. FUNCTION DECLARATIONS
   ========================================================================= */

float mos_math_reduce___m256_add_ps(__m256 v);

/**
 * Calculates the dot product of vector `v1` and vector `v2`. Both vectors have `dimension` float values.
 */
float mos_math_dot_product_avx2(float* v1, float* v2, uint64_t dimension);

/**
 * Squared eucledian distance (l2). Be aware, ranking is preserved no matter if l2 is squared or not.
 * For performace reasons, squared l2 is used.
 */
float mos_math_euclidian_distance_squared_avx2(float* v1, float* v2, uint64_t dimension);

float mos_math_calc_distance(float* v1, float* v2, uint16_t dimension, MOS_T_IDX_HNSW_METRIC metric);

uint16_t mos_math_calc_padded_vector_dimension(uint16_t logical_dims);

#endif