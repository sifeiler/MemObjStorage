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

typedef enum ATTRIBUTE_TYPE ATTRIBUTE_TYPE;

static const uint8_t TYPE_SIZES[] = {
    [MOS_ATTR_TYPE_UINT64] = sizeof(uint64_t),
    [MOS_ATTR_TYPE_TIMESTAMP] = sizeof(uint64_t),
    //length of StringDescriptor.
    //It's not the length of the string but 8 bytes for silo offset + 4 bytes for size
    [MOS_ATTR_TYPE_STRING] = 12
};

#define VALID 1
#define INVALID 0

/* =========================================================================
   2. STRUCTS
   ========================================================================= */

#pragma pack(push, 1)
typedef struct mos_t_layout {
    //sizes
    uint64_t header_size;
    uint64_t attributes_size;
    uint64_t indexes_size;
    uint64_t valid_bitmap_size;
    uint64_t ready_bitmap_size;
    uint64_t record_size;
    uint64_t record_data_size;
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
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct mos_t_state {
    uint64_t next_free_row_id;
    uint64_t last_deleted_row_id;
} mos_t_state;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct mos_t_header {
    uint64_t identifier;            //0x1111CCAC
    uint64_t attribute_count;
    uint64_t index_count;
    uint64_t max_records;

    mos_t_layout layout;
    mos_t_state state;
    mos_t_string_silo string_silo;
} mos_t_header;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct mos_t_record {
    uint64_t id;                    // unique identifier
    uint8_t flags;                  // valid, dirty bits etc.
    uint64_t timestamp;             // eviction
    uint8_t data[];                 // the actual user data
} mos_t_record;
#pragma pack(pop)

typedef struct mos_t_storage {
    mos_t_header* storage_header;   // sizes, offsets, layout, etc.
    mos_t_attr_info* attributes;    // attribute meta information
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

#define MOS_QRY_LOGICAL_OP (MOS_QRY_OP_OR | MOS_QRY_OP_AND | MOS_QRY_OP_NOT)
#define MOS_QRY_RELATIONAL_OP (MOS_QRY_OP_EQ | MOS_QRY_OP_GT | MOS_QRY_OP_LT | MOS_QRY_OP_SIMILAR)

typedef struct mos_t_attr_value_vector {
    uint16_t ef;     // Must be >= k, the number of neighbors requested. Higher ef = better recall, slower search.
    uint64_t top_k;         // Return at most k results,
    float threshold;        // but only results with similarity >= threshold
    uint64_t vector_dim;    // depends on embedding (used model)
    float* vector_val;
} mos_t_attr_value_vector;

typedef struct mos_t_attr_value {
    MOS_ATTR_TYPE type;
    uint64_t byte_length;
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

/**
 * This stack contains bitmaps. It will be used to run a QueryExecStack.
 * It has two pointers:
 *  - result_top: points to the active results of the current AND/OR ExecStep
 *  - free_top: points to a free bitmap that can be popped for the next exec step
 * result_top + free_top = total stack size (total bitmaps)
 * Total stack size was calculated by the max possible exec steps within a single ExecStep evaluation.
 *  Therefore it is not possible for the result and free space to overlap.
 * 
 * Example:
 *  ExecStep: AND with 3 substeps
 *  stack size = 3
 *  free_top = 3
 *  result_top = 0
 *  for every substep: pop free bitmap
 *      ->free_top = 0
 *        result_top = 3 (stack is full with results)
 *  Now the AND operation uses the first result bitmap at data[result_top - 3 (substeps)] and combines all the results there.
 */
typedef struct mos_t_qry_bmp_stack {
    int64_t result_top;
    int64_t free_top;
    uint64_t stack_size;

    // Array of pointers to bitmaps. 
    // Needed because during execution, only pointers should be pushed and popped.
    mos_t_qry_bmp** free_stack;
    mos_t_qry_bmp** result_stack;
} mos_t_qry_bmp_stack;

typedef struct mos_t_qry_bmp_exec_stack {
    uint64_t stack_size;
    uint64_t top;
    mos_t_qry_bmp_exec_step** exec_steps;
} mos_t_qry_bmp_exec_stack;

/* =========================================================================
   FUNCTION DECLARATIONS
   ========================================================================= */

mos_t_config* mos_init_internal_config(mos_t_config* external_cfg);
void mos_init_layout(mos_t_config* cfg, mos_t_layout* layout);

#endif