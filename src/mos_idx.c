#include <string.h>

#include "../include/mos_types_fwd.h"
#include "../include/mos_idx_hmap.h"
#include "../include/mos_idx.h"
#include "../include/mos_utils.h"
#include "../include/mos_internal.h"
#include "../include/mos_math.h"
#include "../include/mos_qry.h"

void mos_idx_init(const uint64_t record_count, mos_t_idx_descriptor* index_desc, mos_t_idx_data* idx_data);

mos_t_idx_ops mos_idx_get_idx_ops(MOS_IDX_TYPE type) {
    return MOS_IDX_OPS_REGISTRY[type];
}

int mos_idx_get_supported_index_query_ops(mos_t_idx_descriptor* index_desc) {
    switch(index_desc->type) {
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
void mos_idx_set_field_index_size(uint64_t max_records, mos_t_idx_descriptor* idx_desc) {
    //every index_data has a header
    uint64_t total_idx_size = MOS_ALIGN_UP(sizeof(mos_t_idx_data_header), MOS_PAGE_SIZE);

    mos_t_idx_ops idx_ops = MOS_IDX_OPS_REGISTRY[idx_desc->type];
    total_idx_size += idx_ops.get_index_size(max_records, idx_desc);

    idx_desc->index_size = total_idx_size;
}

/**
 * The index data consists of:
 * mos_t_idx: index metadata (page aligned)
 * data of concrete index: hnsw, hmap, ... (page aligned)
 */
uint64_t mos_idx_data_size(mos_t_config* config) {
    mos_t_idx_descriptor* index_descriptors = config->indexes;
    uint64_t index_count = config->index_count;
    uint64_t total_idx_size = 0;

    //every index_data has a header
    total_idx_size += index_count * MOS_ALIGN_UP(sizeof(mos_t_idx_data_header), MOS_PAGE_SIZE);

    //iterate over all indexes and sum up size
    for (uint64_t i = 0; i < index_count; i++) {
        mos_t_idx_descriptor index = index_descriptors[i];
        mos_t_idx_ops idx_ops = MOS_IDX_OPS_REGISTRY[index.type];
        total_idx_size += idx_ops.get_index_size(config->max_records, &index);
    }

    return total_idx_size;
}

void mos_idx_create(const mos_t_storage* storage, mos_t_config* mos_config) {
    mos_t_header* mmap_header = mos_accessor_header(&storage->header_region);
    mos_t_idx_descriptor* mmap_index_desc = mos_accessor_header_index_descriptors(&storage->header_region);
    
    if(mmap_header->index_count > 0) {
        //copy all indexes to mmap region
        memcpy(mmap_index_desc, mos_config->indexes, sizeof(mos_t_idx_descriptor) * mmap_header->index_count);
    }

    uint64_t index_data_header_size_padded = MOS_ALIGN_UP(sizeof(mos_t_idx_data_header), MOS_PAGE_SIZE);

    for (uint64_t i = 0; i < mmap_header->index_count; i++) {
        mos_t_idx_descriptor* curr_index_desc = mmap_index_desc + i;
        mos_t_idx_data* index_data = mos_accessor_idx_data(storage->index_regions, mmap_header->index_count, i);
        index_data->header.index_payload_offset = index_data_header_size_padded;
        //copy the index metainformation to the index_data_header before initializing the specific index (hmap, hnsw, ...)
        memcpy(&index_data->header.index_desc, curr_index_desc, sizeof(mos_t_idx_descriptor));
        mos_idx_init(mmap_header->max_records, curr_index_desc, index_data);
    }
}

void mos_idx_init(const uint64_t item_count, mos_t_idx_descriptor* index_desc, mos_t_idx_data* idx_data) {
    mos_t_idx_ops idx_ops = MOS_IDX_OPS_REGISTRY[index_desc->type];

    // This is where the actual index will be initialized.
    // The index init function only gets passed whats relevant:
    //  - how many items will be indexed at max
    //  - the index meta information
    //  - where to store the actual index data
    idx_ops.init_index(item_count, index_desc, idx_data);
}

void mos_idx_put(const MOS_IDX_TYPE idx_type, mos_t_idx_data* idx_data, const uint8_t* key, const size_t key_len, const uint64_t value, mos_idx_put_result* result) {
    mos_t_idx_ops idx_ops = MOS_IDX_OPS_REGISTRY[idx_type];
    idx_ops.put(idx_data, key, key_len, value, result);
}

int64_t mos_idx_get(mos_t_idx_data* idx_data, uint8_t* id) {
    return mos_idx_hmap_get(idx_data, id, 8);
}