#include <stdint.h>
#include <immintrin.h>
#include <stddef.h>
#include <math.h>

#include "../include/mos_math.h"

#define MOS_MATH_SIMD_WIDTH 16

float mos_math_reduce___m256_add_ps(__m256 v) {
    // [A, B, C, D | E, F, G, H]
    __m128 low  = _mm256_castps256_ps128(v);        // [A, B, C, D]
    __m128 high = _mm256_extractf128_ps(v, 1);      // [E, F, G, H]
    __m128 sum4 = _mm_add_ps(low, high);            // [A+E, B+F, C+G, D+H]
    
    __m128 shuf = _mm_movehdup_ps(sum4);            // [B+F, B+F, D+H, D+H]
    __m128 sum2 = _mm_add_ps(sum4, shuf);           // [A+B+E+F, ..., C+D+G+H, ...]
    __m128 shuf2 = _mm_movehl_ps(shuf, sum2);       // [C+D+G+H, ...]
    __m128 sum1 = _mm_add_ss(sum2, shuf2);          // Total sum in lowest float lane
    
    return _mm_cvtss_f32(sum1);
}

/**
 * Calculates the dot product of vector `v1` and vector `v2`. Both vectors have `dimension` float values.
 */
float mos_math_dot_product_avx2(float* v1, float* v2, uint64_t dimension) {
    //we use 256 bit instructions. One float has 32 bits. Per instruction we can process 256 / 32 = 8 flaots.

    __m256 result_reg = _mm256_setzero_ps();
    int64_t dim = (int64_t)dimension;
    int i = 0;
    for(; i <= dim - 8; i += 8) {
        //load vectors into registers
        __m256 v1_reg = _mm256_loadu_ps(&v1[i]);    //use _mm256_load_ps if vectors are always 32 byte aligned
        __m256 v2_reg = _mm256_loadu_ps(&v2[i]);
        result_reg = _mm256_fmadd_ps(v1_reg, v2_reg, result_reg);
    }

    float result_float = mos_math_reduce___m256_add_ps(result_reg);

    //in case vectors are not a multiple of 8
    for(; i < dim; i++) {
        result_float += v1[i] * v2[i];
    }

    return result_float;
}

float mos_math_euclidian_distance_squared_avx2(float* v1, float* v2, uint64_t dimension) {
    __m256 result_reg = _mm256_setzero_ps();

    int64_t dim = (int64_t)dimension;
    int i = 0;
    for(; i <= dim - 8; i += 8) {
        //load vectors into registers
        __m256 v1_reg = _mm256_loadu_ps(&v1[i]);
        __m256 v2_reg = _mm256_loadu_ps(&v2[i]);
        __m256 sub_reg = _mm256_sub_ps(v1_reg, v2_reg);
        result_reg = _mm256_fmadd_ps(sub_reg, sub_reg, result_reg);
    }

    float result_float = mos_math_reduce___m256_add_ps(result_reg);

    for(; i < dim; i++) {
        float sub = v1[i] - v2[i];
        result_float += (sub * sub);
    }

    return result_float;
}

float mos_idx_hnsw_calc_distance(float* v1, float* v2, uint16_t dimension, MOS_T_IDX_HNSW_METRIC metric) {
    switch (metric) {
        case METRIC_COSINE:     //vectors are assumed to be pre-normalized
            float dp = mos_math_dot_product_avx2(v1, v2, dimension);
            return 1 - dp;
        case METRIC_L2:
            return mos_math_euclidian_distance_squared_avx2(v1, v2, dimension);
        case METRIC_DOT_PRODUCT:
            float dp = mos_math_dot_product_avx2(v1, v2, dimension);
            //negate: the higher the dp, the more v1 and v2 point in the same direction.
            // we want the higher the dp, the smaller the distance
            return -dp;
    }
}

uint16_t mos_math_calc_padded_vector_dimension(uint16_t logical_dims) {
    
}