#ifndef MOS_QRY_H
#define MOS_QRY_H

#include "mos_internal.h"
#include "mos_os.h"
#include "mos_types_fwd.h"

/* =========================================================================
   1. CONSTANTS, MACROS, ENUMS
   ========================================================================= */

/* =========================================================================
   2. STRUCTS
   ========================================================================= */

#define MOS_QRY_LOGICAL_OP (MOS_QRY_OP_OR | MOS_QRY_OP_AND | MOS_QRY_OP_NOT)
#define MOS_QRY_RELATIONAL_OP (MOS_QRY_OP_EQ | MOS_QRY_OP_GT | MOS_QRY_OP_LT | MOS_QRY_OP_SIMILAR)

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

static inline void mos_qry_bmp_set_row_id_bit(mos_t_qry_bmp* bm, uint64_t row_id) {
   uint64_t word_index = row_id >> 6; // divide by 64
   uint64_t bit_mask = 1ULL << (row_id & 63); // % 64
   bm->data[word_index] |= bit_mask;
   bm->empty = 0;
}

/* =========================================================================
   FUNCTION DECLARATIONS
   ========================================================================= */
mos_t_qry_bmp* mos_qry_process_search(mos_t_storage* storage, mos_t_qry* query);
uint64_t mos_qry_bmp_count_ones(mos_t_qry_bmp* bm);
uint64_t mos_qry_bmp_get_row_ids(const mos_t_qry_bmp* bm, int64_t* row_ids);

#endif