#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "../include/mos_qry.h"
#include "../include/mos_internal.h"
#include "../include/mos_utils.h"
#include "../include/mos_idx.h"

#define STACK_SIZE 20

/**
 * Calculates the max exec stack size needed to run a search step. This function can be used prior to stack allocation for a search query execution.
 */
void mos_qry_calc_exec_stack_size(mos_t_qry_search_step* step, uint64_t* exec_stack_count) {
    (*exec_stack_count)++;
    if(step->op & MOS_QRY_LOGICAL_OP) {
        for (uint64_t i = 0; i < step->step_count; i++) {
            mos_qry_calc_exec_stack_size(step->sub_steps[i], exec_stack_count);
        }
    }
}

/**
 * Calculates the amount of bitmaps needed to run the passed QueryExecStack.
 * The amount of bitmaps needed is the maximum of execution steps per level of execution (over all levels).
 */
uint64_t mos_qry_calc_bitmap_stack_size(const mos_t_qry_bmp_exec_stack* exec) {
    uint64_t max_stack_size = 0;
    uint64_t stack_size = 0;
    
    if(exec->stack_size > 0) {
        for (int64_t i = exec->stack_size - 1; i >= 0; i--) {
            mos_t_qry_bmp_exec_step* step = exec->exec_steps[i];

            if(step->sub_step_count > 0) {
                //if the step has substeps, they were already addressed in previous iterations.
                //therefore we reduce the stacksize by sub_step_count because the stack size of a bitmap = max sub steps (per level) over all levels of the execution
                stack_size -= step->sub_step_count;
                stack_size++;
            } else {
                stack_size++;
            }

            if(stack_size > max_stack_size) {
                max_stack_size = stack_size;
            }
        }
    }
    return max_stack_size;
}

const mos_t_idx* mos_qry_get_index_for_search_step(mos_t_idx* indexes, mos_t_qry_search_step* search_step, uint64_t indexes_count) {
    mos_t_qry_attr_qry attribute_query = search_step->attribute_query;
    MOS_QRY_OPERATOR op = search_step->op;
    for (uint64_t i = 0; i < indexes_count; i++) {
        mos_t_idx* index = &indexes[i];
        int index_query_ops = mos_idx_get_supported_index_query_ops(index);
        if((strcmp(index->attribute.name, attribute_query.attribute_name) == 0) && (index_query_ops & op)) {
            return index;
        }
    }
    return NULL;
} 

void mos_qry_build_exec_stack(mos_t_storage* storage, mos_t_qry_bmp_exec_stack* exec_stack, mos_t_qry_search_step* search_step) {
    mos_t_qry_bmp_exec_step* curr_step = exec_stack->exec_steps[exec_stack->top];
    MOS_QRY_OPERATOR op = search_step->op;
    curr_step->op = op;

    if(op & MOS_QRY_RELATIONAL_OP) {
        const mos_t_idx* index = mos_qry_get_index_for_search_step(storage->indexes, search_step, storage->storage_header->index_count);

        if(index == NULL) {
            //report_error("Cannot find index for attribute %s and operator %s.", search_step->attribute_query.attribute_name, search_step->operator);
            return;
        }

        curr_step->attr_query = search_step->attribute_query;
        curr_step->idx_data = MOS_GET_PTR(storage->mmap_ptr, index->offset_file);
        curr_step->idx_type = index->type;
        curr_step->sub_step_count = 0;
    } else {
        curr_step->sub_step_count = search_step->step_count;

        if(search_step->step_count > 0) {
            for (int i = search_step->step_count - 1; i >= 0; i--) {
                exec_stack->top++;
                mos_qry_build_exec_stack(storage, exec_stack, search_step->sub_steps[i]);
            } 
        }
    }
}

/**
 * Traverses the search query recursively in post-order and builds a query execution stack.
 * The query can then be executed in post order, by popping from the stack until the stack is empty.
 */
const mos_t_qry_bmp_exec_stack* mos_qry_create_bitmap_exec(mos_t_storage* storage, mos_t_qry* query) {
    uint64_t exec_stack_size = 0;
    mos_qry_calc_exec_stack_size(query->query, &exec_stack_size);

    mos_t_qry_bmp_exec_stack* exec_stack = malloc(sizeof(mos_t_qry_bmp_exec_stack));
    if (!exec_stack) {
        return NULL;
    }

    mos_t_qry_bmp_exec_step** exec_step_ptrs = malloc(exec_stack_size * sizeof(mos_t_qry_bmp_exec_step*));
    if (!exec_step_ptrs) {
        free(exec_stack);
        return NULL;
    }

    exec_stack->stack_size = exec_stack_size;
    exec_stack->top = 0;
    exec_stack->exec_steps = exec_step_ptrs;

    for (uint64_t i = 0; i < exec_stack_size; i++) {
        exec_step_ptrs[i] = malloc(sizeof(mos_t_qry_bmp_exec_step));

        if(!exec_step_ptrs[i]) {
            for (uint64_t j = 0; j < i; j++) {
                free(exec_step_ptrs[j]);
            }
            free(exec_stack);
            return NULL;
        }
    }
    mos_qry_build_exec_stack(storage, exec_stack, query->query);
    return exec_stack;
}

void mos_qry_free_bitmap_exec_stack(const mos_t_qry_bmp_exec_stack* stack) {
    for (uint64_t i = 0; i < stack->stack_size; i++) {
        free(stack->exec_steps[i]);
    }
    free((void*)stack->exec_steps);
    free((void*)stack);
}

mos_t_qry_bmp_stack* mos_qry_create_bitmap_stack(const mos_t_qry_bmp_exec_stack* exec, uint64_t nBits) {
    uint64_t stack_size = mos_qry_calc_bitmap_stack_size(exec);
    uint64_t nWords = (nBits / 64) + 1;
    mos_t_qry_bmp_stack* stack = malloc(sizeof(mos_t_qry_bmp_stack));

    if(!stack) {
        return NULL;
    }
    stack->stack_size = stack_size;

    mos_t_qry_bmp** free_stack = malloc(stack_size * sizeof(mos_t_qry_bmp*));

    if (!free_stack) {
        free(stack);
        return NULL;
    }

    mos_t_qry_bmp** result_stack = malloc(stack_size * sizeof(mos_t_qry_bmp*));
    
    if (!result_stack) {
        free(free_stack);
        free(stack);
        return NULL;
    }

    //start with all bitmaps available (starting at index 0)
    stack->free_top = stack_size;
    stack->result_top = 0;
    stack->free_stack = free_stack;
    stack->result_stack = result_stack;

    //let teh free_stack point to bitmaps
    for (uint64_t i = 0; i < stack_size; i++) {
        mos_os_mem_alloc_aligned((void**)&stack->free_stack[i], sizeof(mos_t_qry_bmp) + nWords * sizeof(uint64_t), 64);
        //if there is a single allocation failure, we free everything
        if(!stack->free_stack[i]) {
            for (uint64_t j = 0; j < i; j++) {
                mos_os_mem_free_aligned(stack->free_stack[j]);
            }
            free(stack->free_stack);
            free(stack);
            return NULL;
        }
        memset(stack->free_stack[i]->data, 0, nWords * sizeof(uint64_t));

        stack->free_stack[i]->nBits = nBits;
        stack->free_stack[i]->nWords = nWords;
        stack->free_stack[i]->full = 0;
        stack->free_stack[i]->empty = 1;
    }

    return stack;
}

void mos_qry_free_bitmap_stack(mos_t_qry_bmp_stack* stack) {
    for (uint64_t i = 0; i < stack->stack_size; i++) {
        if (stack->free_stack[i] != NULL) {
            mos_os_mem_free_aligned(stack->free_stack[i]);
            stack->free_stack[i] = NULL; // prevent double-free
        }
        if (stack->result_stack[i] != NULL) {
            mos_os_mem_free_aligned(stack->result_stack[i]);
            stack->result_stack[i] = NULL; // prevent double-free
        }
    }
    free(stack->free_stack);
    free(stack->result_stack);
    free(stack);
}

static inline mos_t_qry_bmp* mos_qry_bitmap_free_pop(mos_t_qry_bmp_stack* stack) {
    mos_t_qry_bmp* bm = stack->free_stack[--stack->free_top];
    stack->free_stack[stack->free_top] = NULL; // prevent double-free
    return bm;
}

static inline mos_t_qry_bmp* mos_qry_bitmap_result_pop(mos_t_qry_bmp_stack* stack) {
    mos_t_qry_bmp* bm = stack->result_stack[--stack->result_top];
    stack->result_stack[stack->result_top] = NULL; // prevent double-free
    return bm;
}

static inline mos_t_qry_bmp* mos_qry_get_result_bitmap_at_offset(mos_t_qry_bmp_stack* stack, uint64_t offset) {
    //-1 because stack->result_top always points to next index
    return stack->result_stack[(stack->result_top - 1) - offset];
}

static inline void mos_qry_bitmap_free_push(mos_t_qry_bmp_stack* stack, mos_t_qry_bmp* bitmap) {
    stack->free_stack[stack->free_top++] = bitmap;
}

static inline void mos_qry_bitmap_result_push(mos_t_qry_bmp_stack* stack, mos_t_qry_bmp* bitmap) {
    stack->result_stack[stack->result_top++] = bitmap;
}

static inline void mos_qry_execute_and_inline(mos_t_qry_bmp* dst, mos_t_qry_bmp* bm) {
    uint64_t empty = 0;
    for (uint64_t i = 0; i < dst->nWords; i++) {
        dst->data[i] = dst->data[i] & bm->data[i];
        //if there is a single 1 in any word, the bitmap is not empty
        empty |= dst->data[i];
    }
    if(empty) {
        dst->empty = empty;
    }
}

/**
 * Executes a logical and on the bitmap stack.
 * It takes sub_step_count bitmaps from the result stack and accumulates the result into dst_bm.
 * 
 * Keep dst_bm as the accumulator
 * Pop other bitmaps
 * Merge them into dst_bm
 * Mark the popped ones as free (free_top++)
 */
static inline void mos_qry_execute_and(mos_t_qry_bmp_stack* stack, uint64_t sub_step_count) {
    mos_t_qry_bmp* dst_bm = mos_qry_get_result_bitmap_at_offset(stack, sub_step_count - 1);
    //dst_bm contains our first result already, so only iterate sub_step_count - 1 steps
    for (uint64_t i = 0; i < (sub_step_count - 1); i++) {
        mos_t_qry_bmp* src_bm = mos_qry_bitmap_result_pop(stack);
        mos_qry_execute_and_inline(dst_bm, src_bm);
        mos_qry_bitmap_free_push(stack, src_bm);
    }
}

static inline void mos_qry_execute_or_inline(mos_t_qry_bmp* dst, mos_t_qry_bmp* bm) {
    uint64_t full = ~0ULL;
    for (uint64_t i = 0; i < dst->nWords; i++) {
        dst->data[i] = dst->data[i] | bm->data[i];

        //if there is a single 0 in any word, the bitmap is not full
        full &= dst->data[i];
    }
    if(full == ~0ULL) {
        dst->full = full;
    }
}

static inline void mos_qry_execute_or(mos_t_qry_bmp_stack* stack, uint64_t sub_step_count) {
    mos_t_qry_bmp* dst_bm = mos_qry_get_result_bitmap_at_offset(stack, sub_step_count - 1);
    //dst_bm contains our first result already, so only iterate sub_step_count - 1 steps
    for (uint64_t i = 0; i < (sub_step_count - 1); i++) {
        mos_t_qry_bmp* src_bm = mos_qry_bitmap_result_pop(stack);
        mos_qry_execute_or_inline(dst_bm, src_bm);
        mos_qry_bitmap_free_push(stack, src_bm);
    }
}

static inline void mos_qry_execute_not(mos_t_qry_bmp_stack* stack) {
    mos_t_qry_bmp* dst_bm = mos_qry_bitmap_result_pop(stack);
    for (uint64_t i = 0; i < dst_bm->nWords - 1; i++) {
        dst_bm->data[i] = ~dst_bm->data[i];
    }
    uint64_t remainder = dst_bm->nBits & 63;
    //last byte needs special treatment
    uint64_t remainder_mask = (1ULL << (remainder - 1)) - 1;
    dst_bm->data[dst_bm->nWords - 1] = ~((remainder_mask & dst_bm->data[dst_bm->nWords - 1]) | ~remainder_mask);

    //swap: when bitmap was empty, it is now full and vice versa
    uint8_t tmp = dst_bm->full;
    dst_bm->full = dst_bm->empty;
    dst_bm->empty = tmp;
    mos_qry_bitmap_result_push(stack, dst_bm);
}

static inline void mos_qry_execute_leaf(mos_t_qry_bmp_exec_step* exec, mos_t_qry_bmp_stack* stack) {
    mos_t_qry_bmp* bm = mos_qry_bitmap_free_pop(stack);
    mos_idx_bitmap_search(exec->idx_type, exec->idx_data, bm, &exec->attr_query);
    mos_qry_bitmap_result_push(stack, bm);
}

mos_t_qry_bmp* mos_qry_execute(const mos_t_qry_bmp_exec_stack* query_exec, mos_t_qry_bmp_stack* stack) {
    mos_t_qry_bmp_exec_step** steps = query_exec->exec_steps;
    for (int i = query_exec->top; i >= 0; i--) {
        mos_t_qry_bmp_exec_step* step = steps[i];
        switch (step->op) {
            case MOS_QRY_OP_OR:
                mos_qry_execute_or(stack, step->sub_step_count);
                break;
            case MOS_QRY_OP_AND:
                mos_qry_execute_and(stack, step->sub_step_count);
                break;
            case MOS_QRY_OP_NOT:
                mos_qry_execute_not(stack);
                break;
            case MOS_QRY_OP_EQ:
            case MOS_QRY_OP_GT:
            case MOS_QRY_OP_LT:
                mos_qry_execute_leaf(step, stack);
                break;
            default:
                //always false, but should print the string
                assert(0 && "Unknown operation");
                break;
        }
    }
    assert(stack->result_top == 1);
    return mos_qry_bitmap_result_pop(stack);
}

mos_t_qry_bmp* mos_qry_bitmap_clone(mos_t_qry_bmp* bm) {
    uint64_t bm_size = sizeof(mos_t_qry_bmp) + bm->nWords * sizeof(uint64_t);
    mos_t_qry_bmp* bm_clone = NULL;
    mos_os_mem_alloc_aligned((void**)&bm_clone, bm_size, 64);

    if(!bm_clone) {
        return NULL;
    }

    memcpy(bm_clone, bm, bm_size);
    return bm_clone;
}

/**
 * Caller is responsible of freeing the result bitmap.
 */
mos_t_qry_bmp* mos_qry_process_search(mos_t_storage* storage, mos_t_qry* query) {
    const mos_t_qry_bmp_exec_stack* exec_stack = mos_qry_create_bitmap_exec(storage, query);
    // Create a stack of bitmaps that is big enough to execute exec_stack
    // Every bitmap on the stack will have a single bit per record.
    mos_t_qry_bmp_stack* bitmap_stack = mos_qry_create_bitmap_stack(exec_stack, storage->storage_header->max_records);
    mos_t_qry_bmp* result = mos_qry_execute(exec_stack, bitmap_stack);

    mos_t_qry_bmp* result_clone = mos_qry_bitmap_clone(result);

    mos_qry_free_bitmap_exec_stack(exec_stack);
    mos_qry_free_bitmap_stack(bitmap_stack);
    free(result);

    return result_clone;
}

uint64_t mos_qry_bmp_count_ones(mos_t_qry_bmp* bm) {
    uint64_t ones = 0;
    for(size_t i = 0; i < bm->nWords; i++) {
        ones += fsi_popcount64(bm->data[i]);
    }
    return ones;
}

/**
 * Get the index for every active bit (=1) in the bitmap.
 * @param bm the bitmap
 * @param row_ids
 *  Buffer for row_ids. Each row_id is 64 Bits.
 *  Make sure the buffer is of size int64_t * mos_qry_bmp_count_ones(bm).
 * @return row_ids count
 */
uint64_t mos_qry_bmp_get_row_ids(const mos_t_qry_bmp* bm, int64_t* row_ids) {
    uint64_t ones_count = 0;
    for (size_t i = 0; i < bm->nWords; i++) {
        uint64_t word = bm->data[i];
        //we skip zero words
        while (word)
        {
            int bit = fsi_ctz64(word);
            row_ids[ones_count++] = i * 64 + bit;
            // word     = ...0100
            // word - 1 = ...0011
            // AND      = ...0000  ← clears the lowest set bit
            word &= word - 1;
        }
    }
    return ones_count;
}