#ifndef MOS_QRY_H
#define MOS_QRY_H

#include "mos_internal.h"
#include "mos_os.h"
#include "mos_types_fwd.h"

/* =========================================================================
   FUNCTION DECLARATIONS
   ========================================================================= */
mos_t_qry_bmp* mos_qry_process_search(mos_t_storage* storage, mos_t_qry* query);
uint64_t mos_qry_bmp_count_ones(mos_t_qry_bmp* bm);
uint64_t mos_qry_bmp_get_row_ids(mos_t_qry_bmp* bm, int64_t* row_ids);

#endif