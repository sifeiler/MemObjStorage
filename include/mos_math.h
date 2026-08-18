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

//smallest multiple of align where addr fits in
//i.e. align = 64, addr = 100 -> (100 + 64 - 1) & ~0011 1111 = 1000 0000 = 128
#define MOS_ALIGN_UP(addr, align) (((addr) + (align) - 1) & ~((align) - 1))
#define MOS_ALIGN_DOWN(addr, align) ((addr) & ~((align) - 1))

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
float mos_math_dot_product_avx2(const float* v1, const float* v2, const uint64_t dimension);

/**
 * Squared eucledian distance (l2). Be aware, ranking is preserved no matter if l2 is squared or not.
 * For performace reasons, squared l2 is used.
 */
float mos_math_euclidian_distance_squared_avx2(const float* v1, const float* v2, const uint64_t dimension);

float mos_math_calc_distance(const float* v1, const float* v2, const uint16_t dimension, const MOS_T_IDX_HNSW_METRIC metric);

uint16_t mos_math_calc_padded_vector_dimension(const uint16_t logical_dims);

/* =========================================================================
   4. FUNCTION DEFINITIONS
   ========================================================================= */

/**
 * Evaluates the next power of 2 for the given value.
 * 
 * value = 3 (11)
 * return = 4 (100)
 * 
 * value = 6 (110)
 * return = 8 (1000)
 */
static inline uint64_t mos_utils_next_pow_of_2(uint64_t value) {
    //-1 to avoid jumping to next power when already at a power of 2
    int64_t p = value - 1;
    p |= p >> 1;
    p |= p >> 2;
    p |= p >> 4;
    p |= p >> 8;
    p |= p >> 16;
    p |= p >> 32;
    return p < 0 ? 1 : p + 1;
}

#endif