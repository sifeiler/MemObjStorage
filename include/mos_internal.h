#ifndef MOS_INTERNAL_H
#define MOS_INTERNAL_H

#include <stdio.h>
#include <stdint.h>
#include "mos_types_fwd.h"
#include "mos.h"
#include "mos_string.h"

/* =========================================================================
   1. CONSTANTS, MACROS, ENUMS
   ========================================================================= */

#define MOS_FILE_ID 0x1111CCAC

// in bytes
#define MOS_FILE_LINE_SIZE 64
#define MOS_BIT_MAP_WIDTH 64
#define MOS_PAGE_SIZE 4096
#define MOS_AVG_STRING_LEN 100

#define MOS_MAX_ALLOWED_ATTR 1024

/**
 * Basically the types that replace the user provided attribute types internally.
 * 
 * user provided                    internal mapped
 * -------------                    ---------------
 * MOS_ATTR_TYPE_UINT64     ->      MOS_ATTR_TYPE_INTERNAL_UINT64
 * MOS_ATTR_TYPE_TIMESTAMP  ->      MOS_ATTR_TYPE_INTERNAL_TIMESTAMP
 * MOS_ATTR_TYPE_STRING     ->      MOS_ATTR_TYPE_INTERNAL_STRING_DESC
 * MOS_ATTR_TYPE_VECTOR     ->      MOS_ATTR_TYPE_INTERNAL_HNSW_NODE
 */
typedef enum MOS_ATTR_TYPE_INTERNAL {
    MOS_ATTR_TYPE_INTERNAL_UINT64 = 1 << 0,
    MOS_ATTR_TYPE_INTERNAL_TIMESTAMP = 1 << 1,
    MOS_ATTR_TYPE_INTERNAL_STRING_DESC = 1 << 2,
    MOS_ATTR_TYPE_INTERNAL_HNSW_NODE = 1 << 3
} MOS_ATTR_TYPE_INTERNAL;

static const uint8_t EXTERNAL_TYPE_SIZES[] = {
    [MOS_ATTR_TYPE_UINT64] = sizeof(uint64_t),
    [MOS_ATTR_TYPE_TIMESTAMP] = sizeof(uint64_t),
    //length of StringDescriptor.
    //It's not the length of the string but 8 bytes for the char pointer + 4 bytes for byte_size
    [MOS_ATTR_TYPE_STRING] = 12,
    //It's not the length of the vector but 8 bytes for the float pointer + 4 bytes for the dimension
    [MOS_ATTR_TYPE_VECTOR] = 12
};

static const uint8_t INTERNAL_TYPE_SIZES[] = {
    [MOS_ATTR_TYPE_INTERNAL_UINT64] = sizeof(uint64_t),
    [MOS_ATTR_TYPE_INTERNAL_TIMESTAMP] = sizeof(uint64_t),
    // Length of StringDescriptor.
    // It's not the length of the string but 8 bytes for the silo offset + 4 bytes for byte_size
    [MOS_ATTR_TYPE_INTERNAL_STRING_DESC] = 12,
    // 8 bytes for the node_id in hnsw
    [MOS_ATTR_TYPE_INTERNAL_HNSW_NODE] = 8
};

// 32 indexes max
static const uint32_t attr_index_support[32] = {
    [MOS_ATTR_TYPE_STRING] = MOS_IDX_HASH_MAP,
    [MOS_ATTR_TYPE_UINT64] = MOS_IDX_HASH_MAP,
    [MOS_ATTR_TYPE_VECTOR] = MOS_IDX_HNSW,
};

#define VALID 1
#define INVALID 0

#if defined(__AVX512F__)
    #define MOS_SIMD_REGISTER_BYTES 64
    #define MOS_HAS_SIMD 1
#elif defined(__AVX2__)
    #define MOS_SIMD_REGISTER_BYTES 32
    #define MOS_HAS_SIMD 1
#elif defined(__SSE2__) || defined(_M_X64) || defined(_M_AMD64)
    #define MOS_SIMD_REGISTER_BYTES 16
    #define MOS_HAS_SIMD 1
#elif defined(__ARM_NEON) || defined(__ARM_NEON__)
    #define MOS_SIMD_REGISTER_BYTES 16
    #define MOS_HAS_SIMD 1
#else
    #define MOS_HAS_SIMD 0
#endif

/* =========================================================================
   2. STRUCTS
   ========================================================================= */

typedef struct mos_t_layout {
    //sizes
    uint64_t header_size;
    uint64_t attributes_size;
    uint64_t indexes_size;
    uint64_t valid_bitmap_size;
    uint64_t ready_bitmap_size;
    uint64_t record_size;
    uint64_t record_data_size;
    uint64_t record_data_size_external;
    uint64_t records_size;
    uint64_t index_data_size;
    uint64_t string_silo_size;
    uint64_t file_size;

    //offsets
    uint64_t offset_header;
    uint64_t offset_attributes;
    uint64_t offset_indexes;
    uint64_t offset_valid_bitmap;
    uint64_t offset_ready_bitmap;
    uint64_t offset_records;
    uint64_t offset_index_data;
    uint64_t offset_string_silo;
} mos_t_layout;

typedef struct mos_t_state {
    uint64_t next_free_row_id;
    uint64_t last_deleted_row_id;
} mos_t_state;

typedef struct mos_t_header {
    uint64_t identifier;            //0x1111CCAC
    uint64_t attribute_count;
    uint64_t index_count;
    uint64_t max_records;

    mos_t_layout layout;
    mos_t_state state;
    mos_t_string_silo string_silo;
} mos_t_header;

typedef struct mos_t_record {
    uint64_t id;                    // unique identifier
    uint64_t timestamp;             // eviction
    uint8_t flags;                  // valid, dirty bits etc.
    uint8_t _pad[7];
    uint8_t data[];                 // the actual user data
} mos_t_record;

typedef struct mos_t_idx {
    uint64_t index_offset;                      // offset in index_data section (first index has offset 0)
    uint64_t index_size;                        // bytes occupied in the storage file
    char attribute_name[MOS_ATTR_NAME_LENGTH];  // the corresponding attribute
    uint16_t id;                                // a unique identifier among all
    uint8_t type;                               // MOS_IDX_TYPE
    uint8_t _pad[3];

    //optional parameters that are to be selected based on field `type`
    union {
        mos_t_idx_params_hnsw hnsw;
    } params;
} mos_t_idx;

typedef struct mos_t_storage {
    mos_t_header* storage_header;   // sizes, offsets, layout, etc.
    mos_t_attr* attributes;         // attribute meta information
    mos_t_idx* idx_id;              // record id index meta information
    mos_t_idx* indexes;             // array of index meta information
    mos_t_record* entries;          // array of entries
    mos_t_idx_data* idx_id_data;    // record id index
    mos_t_idx_data* index_data;     // attribute index data
    mos_t_qry_bmp* valid_bitmap;    // 1 bit for every record
    mos_t_qry_bmp* ready_bitmap;    // 1 bit for every record
    void* mmap_ptr;                 // pointer to the memory mapped file
    char* file_path;                // path to the memory mapped file
    int fd;                         // memory mapped file (open)
    void* string_silo_base;         // pointer to the string silo area
} mos_t_storage;

typedef struct mos_t_attr {
    uint64_t byte_size_external;    // occupied bytes in user provided data
    uint64_t byte_size_internal;    // occupied bytes in mmapped record

    uint64_t field_offset_external; // byte offset in user provided data
    uint64_t field_offset_internal; // byte offset in internal stored record. mos_t_record.data is base
    char name[MOS_ATTR_NAME_LENGTH];
    uint8_t type;                   // MOS_ATTR_TYPE_INTERNAL
    uint8_t indexed;
    uint8_t  _pad[6];
} mos_t_attr;

typedef struct mos_t_config {
    uint64_t max_records;
    uint64_t index_count;
    uint64_t attribute_count;
    uint64_t attributes_byte_size_internal;
    uint64_t attributes_byte_size_external;
    uint64_t string_attribute_count;

    mos_t_attr* attributes;
    mos_t_idx* indexes;
    char* storage_path;
} mos_t_config;

typedef struct mos_t_float_vector {
    uint16_t vector_dim;
    float* vector;
} mos_t_float_vector;

typedef struct mos_t_attr_value_vector {
    uint64_t top_k;         // Return at most k results,
    uint64_t vector_dim;    // depends on embedding (used model)
    float threshold;        // but only results with similarity >= threshold
    float* vector_val;
    uint16_t ef;            // Must be >= k, the number of neighbors requested. Higher ef = better recall, slower search.
} mos_t_attr_value_vector;

typedef struct mos_t_attr_value {
    uint64_t byte_length;
    MOS_ATTR_TYPE type;
    union {
        uint64_t int_val;
        char* char_val;
        mos_t_attr_value_vector vector_val;
    };
} mos_t_attr_value;

typedef struct mos_t_qry_attr_qry {
    char attribute_name[32];
    mos_t_attr_value value;
} mos_t_qry_attr_qry;

//Search engine structs
//bitmask only for easier comparision
typedef enum MOS_QRY_OPERATOR {
    MOS_QRY_OP_OR   = 1 << 0,
    MOS_QRY_OP_AND  = 1 << 1,
    MOS_QRY_OP_NOT  = 1 << 2,
    MOS_QRY_OP_EQ   = 1 << 3,
    MOS_QRY_OP_GT   = 1 << 4,
    MOS_QRY_OP_LT   = 1 << 5,
    MOS_QRY_OP_SIMILAR     = 1 << 6
} MOS_QRY_OPERATOR;

typedef struct mos_t_qry_search_step {
    MOS_QRY_OPERATOR op;
    uint64_t step_count;
    //point to an array of step pointers
    mos_t_qry_search_step** sub_steps;
    //is only present for relational operators (eq, gt, lt, ...)
    mos_t_qry_attr_qry attribute_query;
} mos_t_qry_search_step;

/**
 * query:
 *  operator: or
 *  steps:
 *      step1:
 *          operator: equal
 *          attribute: name
 *          value: "xyz"
 *      step2:
 *          operator: equal
 *          attribute: crated_at
 *          value 123456
 *      step3:
 *          operator: and
 *          steps:
 *              step1:
 *                  operator: equal
 *                  attribute: attribute1
 *                  value: "123"
 *              step2:
 *                  ...
 */
typedef struct mos_t_qry {
    mos_t_qry_search_step* query;
} mos_t_qry;

//created out of SearchQueryStep
typedef struct mos_t_qry_bmp_exec_step {
    uint64_t sub_step_count;
    MOS_QRY_OPERATOR op;
    // OPTIONAL. Only available for relational exec steps (eq, gt, st, ...)
    MOS_IDX_TYPE idx_type;
    mos_t_idx_data* idx_data;
    mos_t_qry_attr_qry attr_query;
    // OPTIONAL END
} mos_t_qry_bmp_exec_step;

typedef struct mos_t_qry_bmp_exec {
    uint64_t step_count;
    //array of steps sufficient, as steps will be executed sequentially
    mos_t_qry_bmp_exec_step* steps;
} mos_t_qry_bmp_exec;

/**
 * Bitmap with wordsize = 64
 */
typedef struct mos_t_qry_bmp {
    uint64_t nBits;
    uint64_t nWords;

    //for shortcuts in evaluation
    uint8_t full;
    uint8_t empty;

    //data is flexible, so we keep it at the end of the struct for easier allocation later.
    uint64_t data[];
} mos_t_qry_bmp;

/* =========================================================================
   FUNCTION DECLARATIONS
   ========================================================================= */

mos_t_config* mos_init_internal_config(mos_t_storage_config* external_cfg);
void mos_init_layout(mos_t_config* cfg, mos_t_layout* layout);

/* =========================================================================
   FUNCTION DEFINITIONS
   ========================================================================= */

static uint8_t mos_attr_supports_index(MOS_ATTR_TYPE attr_type, MOS_IDX_TYPE idx) {
    return (attr_index_support[attr_type] & idx) != 0;
}

static const char* const MOS_IDX_TYPE_NAMES[] = {
    [MOS_IDX_HASH_MAP] = "MOS_IDX_HASH_MAP",
    [MOS_IDX_HNSW] = "MOS_IDX_HNSW"
};

static MOS_ATTR_TYPE_INTERNAL MOS_ATTR_EXTERNAL_INTERNAL_MAPPING[] = {
    [MOS_ATTR_TYPE_UINT64] = MOS_ATTR_TYPE_INTERNAL_UINT64,
    [MOS_ATTR_TYPE_TIMESTAMP] = MOS_ATTR_TYPE_INTERNAL_TIMESTAMP,
    [MOS_ATTR_TYPE_STRING] = MOS_ATTR_TYPE_INTERNAL_STRING_DESC,
    [MOS_ATTR_TYPE_VECTOR] = MOS_ATTR_TYPE_INTERNAL_HNSW_NODE
};

#endif