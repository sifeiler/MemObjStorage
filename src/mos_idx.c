#include <string.h>

#include "../include/mos_types_fwd.h"
#include "../include/mos_idx_hmap.h"
#include "../include/mos_idx.h"
#include "../include/mos_utils.h"
#include "../include/mos_internal.h"
#include "../include/mos_math.h"
#include "../include/mos_qry.h"

void mos_idx_init(const uint64_t record_count, mos_t_idx* idx, mos_t_idx_data* idx_data);

mos_t_idx_ops mos_idx_get_idx_ops(MOS_IDX_TYPE type) {
    return MOS_IDX_OPS_REGISTRY[type];
}

int mos_idx_get_supported_index_query_ops(mos_t_idx* index) {
    switch(index->type) {
        case MOS_IDX_HASH_MAP: return MOS_QRY_OP_EQ;
        case MOS_IDX_HNSW: return MOS_QRY_OP_SIMILAR;
        default: return 0;
    }
}

/**
 * The index data consists of:
 * mos_t_idx: index metadata (page aligned)
 * data of concrete index: hnsw, hmap, ... (page aligned)
 */
uint64_t mos_idx_data_size(mos_t_config* config) {
    mos_t_idx* indexes = config->indexes;
    uint64_t index_count = config->index_count;
    uint64_t total_idx_size = 0;

    //every index_data has a header
    total_idx_size += index_count * MOS_ALIGN_UP(sizeof(mos_t_idx_data_header), MOS_PAGE_SIZE);

    //iterate over all indexes and sum up size
    for (uint64_t i = 0; i < index_count; i++) {
        mos_t_idx index = indexes[i];
        mos_t_idx_ops idx_ops = MOS_IDX_OPS_REGISTRY[index.type];
        total_idx_size += idx_ops.get_index_size(config->max_records, &index);
    }

    return total_idx_size;
}

void mos_idx_create(const mos_t_storage* storage, mos_t_config* mos_config) {
    mos_t_header* header = storage->storage_header;
    mos_t_idx* index = storage->indexes;
    
    if(header->index_count > 0) {
        //copy all indexes to mmap region
        memcpy(index, mos_config->indexes, sizeof(mos_t_idx) * header->index_count);
    }

    uint64_t index_data_header_size_padded = MOS_ALIGN_UP(sizeof(mos_t_idx_data_header), MOS_PAGE_SIZE);

    uint64_t offset_index = 0;
    for (uint64_t i = 0; i < header->index_count; i++) {
        mos_t_idx* curr_index = index + i;
        mos_t_idx_data* index_data = MOS_GET_PTR(storage->index_data, offset_index);
        index_data->header.index_payload_offset = index_data_header_size_padded;
        curr_index->index_offset = offset_index;
        //copy the index metainformation to the index_data_header before initializing the specific index (hmap, hnsw, ...)
        memcpy(&index_data->header.index, curr_index, sizeof(mos_t_idx));
        mos_idx_init(header->max_records, curr_index, index_data);

        //next index offset is old offset + the index metadata + the size of the concrete index
        offset_index += index_data_header_size_padded + curr_index->index_size;
    }
}

void mos_idx_init(const uint64_t item_count, mos_t_idx* idx, mos_t_idx_data* idx_data) {
    mos_t_idx_ops idx_ops = MOS_IDX_OPS_REGISTRY[idx->type];

    // This is where the actual index will be initialized.
    // The index init function only gets passed whats relevant:
    //  - how many items will be indexed at max
    //  - the index meta information
    //  - where to store the actual index data
    idx_ops.init_index(item_count, idx, idx_data);
}

void mos_idx_put(const MOS_IDX_TYPE idx_type, mos_t_idx_data* idx_data, const uint8_t* key, const size_t key_len, const uint64_t value, mos_idx_put_result* result) {
    mos_t_idx_ops idx_ops = MOS_IDX_OPS_REGISTRY[idx_type];
    idx_ops.put(idx_data, key, key_len, value, result);
}

int64_t mos_idx_get(mos_t_idx_data* idx_data, uint8_t* id) {
    return mos_idx_hmap_get(idx_data, id, 8);
}