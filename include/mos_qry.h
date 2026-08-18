#ifndef MOS_QRY_H
#define MOS_QRY_H

#include "mos_internal.h"
#include "mos_os.h"
#include "mos_types_fwd.h"

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