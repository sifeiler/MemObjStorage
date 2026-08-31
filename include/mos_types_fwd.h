#ifndef MOS_TYPES_H
#define MOS_TYPES_H

/* =========================================================================
   mos_idx.h, mos_idx_*.h FORWARD DECLARATIONS
   ========================================================================= */
typedef enum MOS_IDX_TYPE MOS_IDX_TYPE;
typedef struct mos_t_idx mos_t_idx;
typedef struct mos_t_idx_data mos_t_idx_data;
typedef struct mos_t_idx_op mos_t_idx_op;
typedef struct mos_t_idx_config mos_t_idx_config;

typedef struct mos_t_idx_hmap_header mos_t_idx_hmap_header;
typedef struct mos_t_idx_hmap mos_t_idx_hmap;

typedef struct mos_t_idx_hnsw_graph_config mos_t_idx_hnsw_graph_config;
typedef struct mos_idx_put_result mos_idx_put_result;

/* =========================================================================
   mos.h FORWARD DECLARATIONS
   ========================================================================= */
typedef enum MOS_ATTR_TYPE MOS_ATTR_TYPE;
typedef struct mos_t_string mos_t_string;
typedef struct mos_t_string_desc mos_t_string_desc;
typedef struct mos_t_string_silo mos_t_string_silo;
typedef struct mos_t_attr mos_t_attr;
typedef struct mos_t_config mos_t_config;
typedef struct mos_t_layout mos_t_layout;
typedef struct mos_t_state mos_t_state;
typedef struct mos_t_header mos_t_header;
typedef struct mos_t_record mos_t_record;
typedef struct mos_t_storage mos_t_storage;
typedef struct mos_t_qry_search_step mos_t_qry_search_step;
typedef struct mos_t_qry mos_t_qry;
typedef struct mos_t_qry_attr_qry mos_t_qry_attr_qry;
typedef struct mos_t_qry_bmp mos_t_qry_bmp;
typedef struct mos_t_attr_value mos_t_attr_value;
typedef struct mos_t_float_vector mos_t_float_vector;
typedef struct mos_t_attr_config mos_t_attr_config;
typedef struct mos_t_index_config mos_t_index_config;

#endif