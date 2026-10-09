#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <errno.h>
#include <unistd.h>
#include <inttypes.h>
#include <sys/time.h>
#include <unistd.h> // Sometimes needed for POSIX definitions
#include <assert.h>
#include <string.h>

#include "../include/mos.h"
#include "../include/mos_utils.h"
#include "../include/mos_internal.h"
#include "../include/mos_idx.h"
#include "../include/mos_os.h"
#include "../include/mos_qry.h"
#include "../include/mos_string.h"
#include "../include/mos_math.h"
#include "../include/mos_arena.h"

/* =========================================================================
   1. FORWARD DECLARATIONS
   ========================================================================= */
size_t mos_calc_bitmap_size(mos_t_config* cfg);
size_t mos_calc_record_size(mos_t_config* cfg);
size_t mos_calc_record_data_size_internal(mos_t_config* cfg);
size_t mos_calc_attributes_size(mos_t_config* cfg);
size_t mos_calc_index_descriptors_size(mos_t_config* cfg);
size_t mos_calc_indexes_data_size(mos_t_config* cfg);
void mos_free_config(mos_t_config* cfg);
void mos_set_bit_to_zero(mos_t_qry_bmp* bitmap, uint64_t row_id);
void mos_set_bit_to_one(mos_t_qry_bmp* bitmap, uint64_t row_id);
void mos_print_layout(mos_t_layout* layout);

/* =========================================================================
   2. Helper Function DEFINITIONS
   ========================================================================= */

/*
* Checks if a record_row_id is within record file bounds.
* return 1 if VALID, 0 if INVALID
*/
int mos_check_record_bounds(mos_t_header* header, uint64_t record_row_id) {
    if(record_row_id >= 0 && (record_row_id < header->max_records)) {
        return VALID;
    }
    return INVALID;
}

size_t mos_calc_attributes_size(mos_t_config* cfg) {
    return sizeof(mos_t_attr) * cfg->attribute_count;
}

size_t mos_calc_index_descriptors_size(mos_t_config* cfg) {
    return sizeof(mos_t_idx_descriptor) * cfg->index_count;
}

size_t mos_calc_indexes_data_size(mos_t_config* cfg) {
    return mos_idx_data_size(cfg);
}

/*
* The record consists of:
*  metadata: flags, timestamp, ...
*  user data: calculated via provided attribute information
* @return size_t: size of a record. The value will be aligned up to nearest multiple of 8.
*/
size_t mos_calc_record_size(mos_t_config* cfg) {
    size_t size = offsetof(mos_t_record, data);
    size += cfg->attributes_byte_size_internal;

    //align up to mutiple of 8 for better CPU handling.
    return MOS_ALIGN_UP(size, 8);
}

/*
* Iterates over all mos_t_attr_info and sums up the size of each attribute type.
* @return size_t: size of the data of a record
*/
size_t mos_calc_record_data_size_internal(mos_t_config* cfg) {
    size_t size = 0;

    for(uint64_t i = 0; i < cfg->attribute_count; i++) {
        mos_t_attr* attribute = &cfg->attributes[i];
        size += attribute->byte_size_internal;
    }

    return size;
}

/**
 * bitmap size in bytes
 */
size_t mos_calc_bitmap_size(mos_t_config* cfg) {
    size_t bit_map_size = sizeof(mos_t_qry_bmp);

    if(cfg->max_records <= MOS_BIT_MAP_WIDTH) {
        bit_map_size += MOS_BIT_MAP_WIDTH;
    } else {
        bit_map_size += MOS_BIT_MAP_WIDTH * ((cfg->max_records) / (MOS_BIT_MAP_WIDTH - 1));
    }

    //in bytes
    return MOS_ALIGN_UP(bit_map_size, 8) / 8;
}

void mos_init_layout(mos_t_config* cfg, mos_t_layout* layout) {
    size_t header_size = MOS_ALIGN_UP(sizeof(mos_t_header), MOS_PAGE_SIZE);
    header_size += MOS_ALIGN_UP(mos_calc_attributes_size(cfg), MOS_PAGE_SIZE);
    header_size += MOS_ALIGN_UP(mos_calc_index_descriptors_size(cfg), MOS_PAGE_SIZE);

    size_t bit_map_size = MOS_ALIGN_UP(mos_calc_bitmap_size(cfg), MOS_PAGE_SIZE);
    size_t single_record_size = mos_calc_record_size(cfg);
    size_t record_data_size = cfg->attributes_byte_size_internal;
    size_t record_data_size_external = cfg->attributes_byte_size_external;
    size_t total_records_size = MOS_ALIGN_UP((single_record_size * cfg->max_records), MOS_PAGE_SIZE);

    //set sizes
    layout->header_size = header_size;
    layout->ready_bitmap_size = bit_map_size;
    layout->valid_bitmap_size = bit_map_size;
    layout->record_size = single_record_size;
    layout->record_data_size = record_data_size;
    layout->record_data_size_external = record_data_size_external;
    layout->records_size = total_records_size;

    //later implement resizing
    uint64_t string_silo_size = MOS_AVG_STRING_LEN * cfg->string_attribute_count * cfg->max_records;
    // + 20%
    string_silo_size += string_silo_size * 0.2;
    layout->string_silo_size = string_silo_size > 0 ? MOS_ALIGN_UP(string_silo_size, MOS_PAGE_SIZE) : MOS_PAGE_SIZE;
}

uint64_t mos_get_current_time_millis() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    //tv_usec = microseconds
    return (int64_t)tv.tv_sec * 1000LL + (tv.tv_usec / 1000LL);
}
void mos_idx_id_put(mos_t_storage* storage, uint64_t id, uint64_t record_row_id) {
    mos_t_header* mmap_header = mos_accessor_header(&storage->header_region);
    //first index descriptor is id index descriptor
    mos_t_idx_descriptor* mmap_id_index_desc = mos_accessor_header_index_descriptors(&storage->header_region);
    mos_t_idx_data* id_idx_data = mos_accessor_idx_data(storage->index_regions, mmap_header->index_count, mmap_id_index_desc->index_region_pos);
    
    const mos_t_idx_context idx_context = {
        .idx_data = id_idx_data,
        .idx_type = mmap_id_index_desc->type,
        .kind.hmap.arena_region = &storage->arena_region
    };
    
    uint8_t* key_ptr = (uint8_t*)&id;
    mos_idx_put(&idx_context, key_ptr, sizeof(id), record_row_id, NULL);
}

void mos_idx_id_remove(mos_t_storage* storage, uint64_t id) {
    mos_t_header* mmap_header = mos_accessor_header(&storage->header_region);
    mos_t_idx_descriptor* mmap_id_index_desc = mos_accessor_header_index_descriptors(&storage->header_region);
    mos_t_idx_data* id_idx_data = mos_accessor_idx_data(storage->index_regions, mmap_header->index_count, mmap_id_index_desc->index_region_pos);
    
    const mos_t_idx_context idx_context = {
        .idx_data = id_idx_data,
        .idx_type = mmap_id_index_desc->type,
        .kind.hmap.arena_region = &storage->arena_region
    };
    
    uint8_t* key_ptr = (uint8_t*)&id;
    mos_idx_remove_key(&idx_context, key_ptr, sizeof(id));
}

static inline mos_t_attr* mos_get_attribute_for_attribute_name(mos_t_attr* attributes, uint64_t attributes_count, const char* attribute_name) {
    for(uint64_t i = 0; i < attributes_count; i++) {
        if((strcmp(attribute_name, attributes[i].name) == 0)) {
            return &attributes[i];
        }
    }
    return NULL;
}

void mos_indexes_put(mos_t_storage* storage, uint64_t id, uint8_t* external_record_data, uint8_t* record_data_out, uint64_t record_row_id) {
    mos_t_header* mmap_header = mos_accessor_header(&storage->header_region);
    mos_t_idx_descriptor* mmap_index_descriptors = mos_accessor_header_index_descriptors(&storage->header_region);
    mos_t_attr* mmap_attributes = mos_accessor_header_attributes(&storage->header_region);
    //skip id index
    for (uint64_t i = 1; i < mmap_header->index_count; i++) {
        mos_t_idx_descriptor* index_descriptor = mmap_index_descriptors + i;
        mos_t_idx_data* idx_data = mos_accessor_idx_data(storage->index_regions, mmap_header->index_count, i);
        
        const mos_t_idx_context idx_context = {
            .idx_data = idx_data,
            .idx_type = index_descriptor->type,
            .kind.hmap.arena_region = &storage->arena_region
        };
        
        mos_t_attr* attribute = mos_get_attribute_for_attribute_name(mmap_attributes, mmap_header->attribute_count, index_descriptor->attribute_name);

        uint8_t* attr_base = external_record_data + attribute->field_offset_external;
        uint32_t byte_size = 0;

        //TODO: think about a put pre-/postprocessing step for getting the index data and writing it back to the record.
        if(attribute->type == MOS_ATTR_TYPE_INTERNAL_STRING_DESC) {
            mos_t_string* str = ((mos_t_string*)attr_base);
            byte_size = str->str_len;
            attr_base = (uint8_t*)str->str;
        } else if(attribute->type == MOS_ATTR_TYPE_INTERNAL_HNSW_NODE) {
            mos_t_float_vector* vector = ((mos_t_float_vector*)attr_base);
            byte_size = vector->vector_dim * sizeof(float);
            attr_base = (uint8_t*)vector->vector;
        } else {
            byte_size = attribute->byte_size_external;
        }

        if(attr_base) {
            mos_t_idx_put_result put_result;
            put_result.put_result = NULL;
            mos_idx_put(&idx_context, attr_base, byte_size, record_row_id, &put_result);

            if(put_result.put_result) {
                uint8_t* internal_record_attr = record_data_out + attribute->field_offset_internal;
                assert(put_result.byte_size == attribute->byte_size_internal && "Result of mos_idx_put is incorrectly sized for the record.");
                memcpy(internal_record_attr, put_result.put_result, attribute->byte_size_internal);
                free(put_result.put_result);
                put_result.put_result = NULL;   // defensive — avoid any accidental reuse/double-free further down
            }
        }
    }
    mos_idx_id_put(storage, id, record_row_id);
}

void mos_put_internal(mos_t_storage* storage, uint64_t id, void* external_record_data, uint64_t record_row_id) {
    mos_t_header* mmap_header = mos_accessor_header(&storage->header_region);
    uint64_t record_size = mmap_header->layout.record_size;

    mos_t_qry_bmp* valid_bitmap = mos_accessor_bitmap(&storage->valid_bitmap_region);
    //record is no longer valid for search etc.
    mos_set_bit_to_zero(valid_bitmap, record_row_id);

    uint8_t record_buffer[record_size];
    memset(record_buffer, 0, record_size);
    mos_t_record* record_buffer_ptr = (mos_t_record*)record_buffer;
    record_buffer_ptr->id = id;
    uint8_t* record_buffer_data_ptr = record_buffer_ptr->data;
    mos_t_attr* mmap_attributes = mos_accessor_header_attributes(&storage->header_region);
    //check if any attributes need special treatment
    for(uint64_t i = 0; i < mmap_header->attribute_count; i++) {
        mos_t_attr* attribute = &mmap_attributes[i];

        //vectors are exclusively handled via mos_indexes_put (HNSW is mandatory for them)
        if(attribute->type == MOS_ATTR_TYPE_INTERNAL_HNSW_NODE) {
            continue;
        }
        
        uint8_t* external_attr = ((uint8_t*)external_record_data) + attribute->field_offset_external;
        uint8_t* internal_attr = record_buffer_data_ptr + attribute->field_offset_internal;

        if(attribute->type == MOS_ATTR_TYPE_INTERNAL_STRING_DESC) {
            mos_t_string* str = (mos_t_string*)external_attr;
            mos_t_string_desc str_out;
            mos_string_put(storage->string_silo_region.region_base, &mmap_header->string_silo, str, &str_out);
            memcpy(internal_attr, &str_out, attribute->byte_size_internal);
        } else {
            //plain scalar — always a direct copy, indexed or not
            memcpy(internal_attr, external_attr, attribute->byte_size_internal);
        }
    }
    mos_indexes_put(storage, id, (uint8_t*)external_record_data, record_buffer_data_ptr, record_row_id);

    record_buffer_ptr->flags = 0;
    record_buffer_ptr->timestamp = mos_get_current_time_millis();

    mos_t_record* record_storage = mos_accessor_record(&storage->records_region, record_row_id, record_size);
    memcpy(record_storage, record_buffer, record_size);

    //record is valid for search etc.
    mos_set_bit_to_one(valid_bitmap, record_row_id);
}

/* =========================================================================
   3. Header Function DEFINITIONS
   ========================================================================= */

mos_t_config* mos_init_internal_config(mos_t_storage_config* external_cfg) {
    mos_t_config* internal_cfg = calloc(1, sizeof(mos_t_config));

    if(!internal_cfg) {
        mos_utils_report_error("[mos_core]: Allocation error. mos_t_storage_config cannot be allocated. Cannot initialize internal config.\n");
        return NULL;
    }

    mos_t_attr* internal_attributes = calloc(external_cfg->attribute_count, sizeof(mos_t_attr));
    if(!internal_attributes) {
        free(internal_cfg);
        mos_utils_report_error("[mos_core]: Allocation error. mos_t_attr_internal cannot be allocated. Cannot initialize internal config.\n");
        return NULL;
    }

    //+1 for the fixed id index. Every record has an id.
    mos_t_idx_descriptor* index_descriptors = calloc(external_cfg->index_count + 1, sizeof(mos_t_idx_descriptor));
    if(!index_descriptors) {
        free(internal_cfg);
        free(internal_attributes);
        mos_utils_report_error("[mos_core]: Allocation error. mos_t_idx cannot be allocated. Cannot initialize internal config.\n");
        return NULL;
    }

    uint64_t actual_attributes_count = 0;
    uint64_t actual_indexes_count = 0;
    uint64_t attribute_offset = 0;
    uint64_t total_attribute_byte_size_internal = 0;

    //id index
    mos_t_idx_descriptor* id_index_desc = &index_descriptors[0];
    id_index_desc->id = 0;
    id_index_desc->type = MOS_IDX_HASH_MAP;
    strncpy(id_index_desc->attribute_name, "id", MOS_ATTR_NAME_LENGTH - 2);
    id_index_desc->attribute_name[MOS_ATTR_NAME_LENGTH - 1] = '\0';
    // Very important that index_size is set here!!! This is needed for sizing the mmapped region later.
    mos_idx_set_field_index_size(external_cfg->max_records, id_index_desc);
    actual_indexes_count++;

    //TODO: This should work for 1:n attribute:index mappings
    for(uint64_t i = 0; i < external_cfg->attribute_count; i++) {
        mos_t_attr_config external_attribute = external_cfg->attributes[i];
        mos_t_attr* internal_attribute = &internal_attributes[i];

        if(external_attribute.indexed) {
            int found = 0;
            for(uint64_t j = 0; j < external_cfg->index_count; j++) {
                mos_t_idx_config* idx_config = &external_cfg->indexes[j];
                if(strncmp(external_attribute.name, idx_config->attribute_name, strlen(external_attribute.name)) == 0) {
                    found = 1;
                    if(actual_indexes_count == (external_cfg->index_count + 1)) {
                        //too many indexes
                        mos_utils_report_error("[mos_core]: Invalid mos_t_config. Surpassing expected index_count.\n");
                        return NULL;
                    }
                    
                    mos_t_idx_descriptor* index_desc = &index_descriptors[actual_indexes_count];
                    strncpy(index_desc->attribute_name, external_attribute.name, MOS_ATTR_NAME_LENGTH - 2);
                    index_desc->attribute_name[MOS_ATTR_NAME_LENGTH - 1] = '\0';
                    index_desc->type = idx_config->type;
                    memcpy(&index_desc->params, &idx_config->params, sizeof(index_desc->params));
                    index_desc->id = actual_indexes_count++;
                    mos_idx_set_field_index_size(external_cfg->max_records, index_desc);

                    internal_attribute->flags |= MOS_ATTR_FLAG_INDEXED;
                    if(external_attribute.groupable) {
                        internal_attribute->flags |= MOS_ATTR_FLAG_GROUPABLE;
                    }
                }
            }
            if(found == 0) {
                //attribute should be indexed but index configuration is missing -> invalid configuration
                mos_utils_report_error("[mos_core]: Invalid mos_t_storage_config. Missing index configuration for attribute %s\n", external_attribute.name);
                return NULL;
            }
        }

        MOS_ATTR_TYPE_INTERNAL attr_type_internal = MOS_ATTR_EXTERNAL_INTERNAL_MAPPING[external_attribute.type];

        internal_attribute->byte_size_external = external_attribute.byte_size;
        internal_attribute->byte_size_internal = INTERNAL_TYPE_SIZES[attr_type_internal];
        total_attribute_byte_size_internal += internal_attribute->byte_size_internal;

        if(external_attribute.type == MOS_ATTR_TYPE_STRING) {
            internal_cfg->string_attribute_count++;
        }

        internal_attribute->field_offset_external = external_attribute.field_offset;
        internal_attribute->field_offset_internal = attribute_offset;
        internal_attribute->type = attr_type_internal;
        strncpy(internal_attribute->name, external_attribute.name, MOS_ATTR_NAME_LENGTH - 2);
        internal_attribute->name[MOS_ATTR_NAME_LENGTH - 1] = '\0';
        attribute_offset += internal_attribute->byte_size_internal;
        actual_attributes_count++;
    }

    internal_cfg->attributes_byte_size_external = external_cfg->padded_record_byte_size;
    internal_cfg->attributes_byte_size_internal = total_attribute_byte_size_internal;
    internal_cfg->attributes = internal_attributes;
    internal_cfg->indexes = index_descriptors;
    internal_cfg->attribute_count = actual_attributes_count;
    internal_cfg->index_count = actual_indexes_count;
    internal_cfg->max_records = external_cfg->max_records;
    internal_cfg->storage_path = external_cfg->storage_path;

    for(uint64_t i = 0; i < internal_cfg->index_count; i++) {
        mos_t_idx_descriptor* index_desc = &index_descriptors[i];
        index_desc->index_region_pos = i;
    }

    return internal_cfg;
}

int mos_validate_config(mos_t_storage_config* cfg) {
    if(cfg == NULL) {
        mos_utils_report_error("[mos_core]: Invalid mos_t_storage_config. config is NULL.\n");
        return INVALID;
    }

    if(cfg->max_records == 0) {
        mos_utils_report_error("[mos_core]: Invalid mos_t_storage_config. max_records is 0.\n");
        return INVALID;
    }

    if(cfg->attribute_count == 0) {
        mos_utils_report_error("[mos_core]: Invalid mos_t_storage_config. attribute_count is 0.\n");
        return INVALID;
    }

    if(*cfg->storage_path == '\0') {
        mos_utils_report_error("[mos_core]: Invalid mos_t_storage_config. storage_path is 0.\n");
        return INVALID;
    }

    for(uint64_t i = 0; i < cfg->attribute_count; i++) {
        mos_t_attr_config attribute = cfg->attributes[i];

        if(attribute.name[0] == '\0') {
            mos_utils_report_error("[mos_core]: Invalid mos_t_storage_config. Attribute name is not set.\n");
            return INVALID;
        }

        if(attribute.byte_size == 0) {
            mos_utils_report_error("[mos_core]: Invalid mos_t_storage_config. Attribute byte_size is 0.\n");
            return INVALID;
        }

        if(attribute.type < MOS_ATTR_MIN || attribute.type >= MOS_ATTR_MAX) {
            mos_utils_report_error("[mos_core]: Invalid mos_t_storage_config. Attribute type is invalid.\n");
            return INVALID;
        }

        if(attribute.groupable && !attribute.indexed) {
            mos_utils_report_error("[mos_core]: Invalid mos_t_storage_config. Attribute has to be flagged indexed when flagged groupable.\n");
            return INVALID;
        }

        if(attribute.groupable && ((attribute.type & MOS_ATTR_TYPES_GROUPABLE) == 0)) {
            mos_utils_report_error("[mos_core]: Invalid mos_t_storage_config. Attribute is flagged groupable, but attribute type does not support grouping. Use integer types.\n");
            return INVALID;
        }

        if(attribute.indexed) {
            mos_t_idx_config* attr_idx_config = NULL;
            for(uint64_t j = 0; j < cfg->index_count; j++) {
                mos_t_idx_config* idx_config = &cfg->indexes[j];
                if(strncmp(attribute.name, idx_config->attribute_name, strlen(attribute.name)) == 0) {
                    attr_idx_config = idx_config;
                    break;
                }
            }
            if(!attr_idx_config) {
                //attribute should be indexed but index configuration is missing -> invalid configuration
                mos_utils_report_error("[mos_core]: Invalid mos_t_storage_config. Missing index configuration for attribute %s\n", attribute.name);
                return INVALID;
            }
            if(!mos_attr_supports_index(attribute.type, attr_idx_config->type)) {
                //attribute should be indexed but provided index is not compatible with attribute type -> invalid configuration
                mos_utils_report_error("[mos_core]: Invalid mos_t_storage_config. Index %s not compatible with attribute %s\n", MOS_IDX_TYPE_NAMES[attr_idx_config->type], attribute.name);
                return INVALID;
            }
        }

        for(uint64_t j = 0; j < i; j++) {
            mos_t_attr_config attribute2 = cfg->attributes[j];
            uint64_t start_1 = attribute.field_offset;
            uint64_t start_2 = attribute2.field_offset;
            uint64_t end_1 = attribute.field_offset + attribute.byte_size;
            uint64_t end_2 = attribute2.field_offset + attribute2.byte_size;
            if(start_1 < end_2 && start_2 < end_1) {
                mos_utils_report_error("[mos_core]: Invalid mos_t_storage_config. Attributes %s and %s are overlapping", attribute.name, attribute2.name);
                return INVALID;
            }
        }
    }

    return VALID;
}

static void mos_free_index_regions(mos_t_mapped_region** regions, uint64_t count) {
    if (!regions) {
        return;
    }
    for (uint64_t i = 0; i < count; i++) {
        if (regions[i]) {
            mos_memory_region_close(regions[i]);   // assumed to unmap, close the fd and free the struct
        }
    }
    free(regions);
}

int mos_create_index_mmap_regions(const char* dir_path, mos_t_idx_descriptor* idx_descs, uint64_t index_count, mos_t_mapped_region* regions_out) {
    for(uint64_t i = 0; i < index_count; i++) {
        mos_t_idx_descriptor* idx_desc = &idx_descs[i];

        char file_name[64];
        snprintf(file_name, sizeof(file_name), "mos_index_%u.mos", (unsigned)idx_desc->id);

        char mmap_path[MOS_PATH_MAX];
        if(mos_os_path_join(mmap_path, sizeof(mmap_path), dir_path, file_name) != 0) {
            printf("[mos_core]: Couldn't join path for mmapped file of index %u.\n", (unsigned)idx_desc->id);
            return -1;
        }
        if(mos_memory_region_open(mmap_path, idx_desc->index_size, MOS_PAGE_SIZE, &regions_out[i]) != 0) {
            printf("[mos_core]: Couldn't open mmapped region of index %u.\n", (unsigned)idx_desc->id);
            return -1;
        }
    }
    return 0;
}

mos_t_storage* mos_load_storage(const char* dir_path) {
    if(dir_path == NULL) {
        mos_utils_report_error("[mos_core]: Invalid argument dir_path is NULL. Cannot load storage files.\n");
        return NULL;
    }

    char mmap_path_header[MOS_PATH_MAX];    
    if(mos_os_path_join(mmap_path_header, sizeof(mmap_path_header), dir_path, "mos_header.mos") != 0) {
        mos_utils_report_error("[mos_core]: Couldn't join path for mmapped file header. Storage load failed.\n");
        return NULL;
    }

    int header_page_size = MOS_ALIGN_UP(sizeof(mos_t_header), MOS_PAGE_SIZE);
    mos_t_mapped_region header_region_first_page;

    //at this point, we do not know the full size of the header. So map the first page, which contains the memory layout.
    if(mos_memory_region_open(mmap_path_header, header_page_size, MOS_PAGE_SIZE, &header_region_first_page) != 0) {
        mos_utils_report_error("[mos_core]: Cannot load storage file. Failed to map header pages of file %s.\n", mmap_path_header);
        return NULL;
    }

    mos_t_header* mmap_header_first_page = mos_accessor_header(&header_region_first_page);
    
    if(mmap_header_first_page->identifier != MOS_FILE_ID) {
        printf("[mos_core]: Cannot load storage file %s. File header is invalid.\n", mmap_path_header);
        return NULL;
    }

    //the layout fits easily within the first memory page of the header
    mos_t_layout layout = mmap_header_first_page->layout;

    if(mos_memory_region_close(&header_region_first_page) != 0) {
        mos_utils_report_error("[mos_core]: Cannot load storage file. Failed to unmap the first memory page (header with layout) of file %s.\n", mmap_path_header);
        return NULL;
    }

    //now map remaining files
    char mmap_path_valid_bitmap[MOS_PATH_MAX];
    char mmap_path_ready_bitmap[MOS_PATH_MAX];
    char mmap_path_records[MOS_PATH_MAX];
    char mmap_path_string_silo[MOS_PATH_MAX];
    char mmap_path_arena[MOS_PATH_MAX];

    int path_join_result = 0;
    path_join_result &= mos_os_path_join(mmap_path_valid_bitmap, sizeof(mmap_path_valid_bitmap), dir_path, "mos_valid_bitmap.mos");
    path_join_result &= mos_os_path_join(mmap_path_ready_bitmap, sizeof(mmap_path_ready_bitmap), dir_path, "mos_ready_bitmap.mos");
    path_join_result &= mos_os_path_join(mmap_path_records, sizeof(mmap_path_records), dir_path, "mos_records.mos");
    path_join_result &= mos_os_path_join(mmap_path_string_silo, sizeof(mmap_path_string_silo), dir_path, "mos_string_silo.mos");
    path_join_result &= mos_os_path_join(mmap_path_arena, sizeof(mmap_path_arena), dir_path, "mos_arena.mos");

    if(path_join_result != 0) {
        printf("[mos_core]: Couldn't join paths for mmapped files. Storage load failed.\n");
        return NULL;
    }

    mos_t_storage* storage = (mos_t_storage*)calloc(1, sizeof(mos_t_storage));
    if(!storage) {
        mos_utils_report_error("[mos_core]: Failed to allocate memory. Storage load failed.\n");
        return NULL;
    }

    int region_open_result = 0;
    //now load the full header
    region_open_result &= mos_memory_region_open(mmap_path_header, layout.header_size, MOS_PAGE_SIZE, &storage->header_region);
    region_open_result &= mos_memory_region_open(mmap_path_valid_bitmap, layout.valid_bitmap_size, MOS_PAGE_SIZE, &storage->valid_bitmap_region);
    region_open_result &= mos_memory_region_open(mmap_path_ready_bitmap, layout.ready_bitmap_size, MOS_PAGE_SIZE, &storage->ready_bitmap_region);
    region_open_result &= mos_memory_region_open(mmap_path_records, layout.records_size, MOS_PAGE_SIZE, &storage->records_region);
    region_open_result &= mos_memory_region_open(mmap_path_string_silo, layout.string_silo_size, MOS_PAGE_SIZE, &storage->string_silo_region);

    //mos_memory_region_open will figure out the real file_size to map, so just pass MOS_PAGE_SIZE
    region_open_result &= mos_memory_region_open(mmap_path_arena, MOS_PAGE_SIZE, MOS_PAGE_SIZE, &storage->arena_region);

    mos_t_header* mmap_header = mos_accessor_header(&storage->header_region);
    storage->index_regions = calloc(1, mmap_header->index_count * sizeof(mos_t_mapped_region));

    if (!storage->index_regions) {
        mos_free_storage(storage);
        return NULL;
    }

    mos_t_idx_descriptor* mmap_index_descriptors = mos_accessor_header_index_descriptors(&storage->header_region);
    region_open_result &= mos_create_index_mmap_regions(dir_path, mmap_index_descriptors, mmap_header->index_count, storage->index_regions);

    if(region_open_result != 0) {
        printf("[mos_core]: Couldn't open a mmapped region. Storage load failed.\n");
        mos_free_storage(storage);
        return NULL;
    }

    return storage;
}

void mos_free_storage_config(mos_t_config* cfg) {
    if(cfg->attributes != NULL) {
        free(cfg->attributes);
        cfg->attributes = NULL;
    }
    if(cfg->indexes != NULL) {
        free(cfg->indexes);
        cfg->indexes = NULL;
    }
    free(cfg);
}

mos_t_storage* mos_create_storage(const char* dir_path, mos_t_storage_config* external_cfg) {
    if(dir_path == NULL) {
        mos_utils_report_error("[mos_core]: Invalid argument dir_path is NULL. Cannot create storage files.\n");
        return NULL;
    }
    
    if(mos_validate_config(external_cfg) == INVALID) {
        mos_utils_report_error("[mos_core]: Invalid mos_t_config. Cannot create storage file.\n");
        return NULL;
    }
    
    mos_t_config* internal_cfg = mos_init_internal_config(external_cfg);
    if(!internal_cfg) {
        mos_utils_report_error("[mos_core]: Cannot create storage: internal config initialization failed.\n");
        return NULL;
    }
    
    mos_t_layout layout;
    mos_init_layout(internal_cfg, &layout);

    // create all mmapped files in dir_path
    char mmap_path_header[MOS_PATH_MAX];
    char mmap_path_valid_bitmap[MOS_PATH_MAX];
    char mmap_path_ready_bitmap[MOS_PATH_MAX];
    char mmap_path_records[MOS_PATH_MAX];
    char mmap_path_string_silo[MOS_PATH_MAX];
    char mmap_path_arena[MOS_PATH_MAX];

    int path_join_result = 0;
    path_join_result &= mos_os_path_join(mmap_path_header, sizeof(mmap_path_header), dir_path, "mos_header.mos");
    path_join_result &= mos_os_path_join(mmap_path_valid_bitmap, sizeof(mmap_path_valid_bitmap), dir_path, "mos_valid_bitmap.mos");
    path_join_result &= mos_os_path_join(mmap_path_ready_bitmap, sizeof(mmap_path_ready_bitmap), dir_path, "mos_ready_bitmap.mos");
    path_join_result &= mos_os_path_join(mmap_path_records, sizeof(mmap_path_records), dir_path, "mos_records.mos");
    path_join_result &= mos_os_path_join(mmap_path_string_silo, sizeof(mmap_path_string_silo), dir_path, "mos_string_silo.mos");
    path_join_result &= mos_os_path_join(mmap_path_arena, sizeof(mmap_path_arena), dir_path, "mos_arena.mos");

    if(path_join_result != 0) {
        printf("[mos_core]: Couldn't join paths for mmapped files. Storage creation failed.\n");
        mos_free_storage_config(internal_cfg);
        return NULL;
    }

    mos_t_storage* storage = (mos_t_storage*)calloc(1, sizeof(mos_t_storage));
    if(!storage) {
        mos_utils_report_error("[mos_core]: Failed to allocate memory. Storage load failed.\n");
        return NULL;
    }

    storage->index_regions = calloc(1, internal_cfg->index_count * sizeof(mos_t_mapped_region));

    if (!storage->index_regions) {
        mos_free_storage_config(internal_cfg);
        mos_free_storage(storage);
        return NULL;
    }

    int region_open_result = 0;
    region_open_result &= mos_memory_region_open(mmap_path_header, layout.header_size, MOS_PAGE_SIZE, &storage->header_region);
    region_open_result &= mos_memory_region_open(mmap_path_valid_bitmap, layout.valid_bitmap_size, MOS_PAGE_SIZE, &storage->valid_bitmap_region);
    region_open_result &= mos_memory_region_open(mmap_path_ready_bitmap, layout.ready_bitmap_size, MOS_PAGE_SIZE, &storage->ready_bitmap_region);
    region_open_result &= mos_memory_region_open(mmap_path_records, layout.records_size, MOS_PAGE_SIZE, &storage->records_region);
    region_open_result &= mos_memory_region_open(mmap_path_string_silo, layout.string_silo_size, MOS_PAGE_SIZE, &storage->string_silo_region);

    //arena starts with MOS_PAGE_SIZE and is resized automatically if needed
    region_open_result &= mos_memory_region_open(mmap_path_arena, MOS_PAGE_SIZE, MOS_PAGE_SIZE, &storage->arena_region);
    mos_arena_init(&storage->arena_region);

    region_open_result &= mos_create_index_mmap_regions(dir_path, internal_cfg->indexes, internal_cfg->index_count, storage->index_regions);

    if(region_open_result != 0) {
        printf("[mos_core]: Couldn't open a mmapped region. Storage creation failed.\n");
        mos_free_storage_config(internal_cfg);
        mos_free_storage(storage);
        return NULL;
    }

    //writing header to file
    mos_t_header* mmap_storage_header = mos_accessor_header(&storage->header_region);
    mmap_storage_header->attribute_count = internal_cfg->attribute_count;
    mmap_storage_header->index_count = internal_cfg->index_count;
    mmap_storage_header->attributes_offset = MOS_ALIGN_UP(sizeof(mos_t_header), MOS_PAGE_SIZE);
    mmap_storage_header->index_descriptors_offset = MOS_ALIGN_UP(mmap_storage_header->attributes_offset + mos_calc_attributes_size(internal_cfg), MOS_PAGE_SIZE);
    mmap_storage_header->max_records = internal_cfg->max_records;
    memcpy(&(mmap_storage_header->layout), &layout, sizeof(mos_t_layout));

    //TODO: String silo grows bottom up. This is a leftover from earlier single mmapped-file design. This will lead to issues on resize. Fix that!!!
    mos_t_string_silo* mmap_string_silo = &mmap_storage_header->string_silo;
    mmap_string_silo->current_offset = 0;
    mmap_string_silo->size = layout.string_silo_size;
    mmap_string_silo->last_deleted.str_len = 0;
    mmap_string_silo->last_deleted.str_offset = MOS_NULL_OFFSET;

    mos_t_state* mmap_storage_state = &(mmap_storage_header->state);
    //record offsets are calculated from record area start
    mmap_storage_state->next_free_row_id = 0;
    mmap_storage_state->last_deleted_row_id = MOS_NULL_OFFSET;

    //writing storage attributes to file
    mos_t_attr* mmap_attributes = mos_accessor_header_attributes(&storage->header_region);
    memcpy(mmap_attributes, internal_cfg->attributes, sizeof(mos_t_attr) * internal_cfg->attribute_count);

    mos_t_idx_descriptor* mmap_index_desc = mos_accessor_header_index_descriptors(&storage->header_region);
    memcpy(mmap_index_desc, internal_cfg->indexes, sizeof(mos_t_idx_descriptor) * internal_cfg->index_count);

    mos_idx_create(storage, internal_cfg);

    mmap_storage_header->identifier = MOS_FILE_ID;

    mos_free_storage_config(internal_cfg);
    return storage;
}

void mos_free_storage(mos_t_storage* storage) {
    if(!storage) {
        return;
    }

    mos_t_header* mmap_header = mos_accessor_header(&storage->header_region);
    for(uint64_t i = 0; i < mmap_header->index_count; i++) {
        mos_memory_region_close(&storage->index_regions[i]);
    }
    mos_memory_region_close(&storage->header_region);
    mos_memory_region_close(&storage->valid_bitmap_region);
    mos_memory_region_close(&storage->ready_bitmap_region);
    mos_memory_region_close(&storage->records_region);
    mos_memory_region_close(&storage->string_silo_region);

    free(storage->index_regions);
    free(storage);
}

/**
 * Sets the bit at index row_id to 0 in bitmap.
 */
void mos_set_bit_to_zero(mos_t_qry_bmp* bitmap, uint64_t row_id) {
    uint64_t word = row_id / 64;
    uint64_t bitmask = ~(1ULL << (row_id & 63)); // find remainder (%64) and set the bit to 0
    bitmap->data[word] = bitmap->data[word] & bitmask;
}

/**
 * Sets the bit at index row_id to 1 in bitmap.
 */
void mos_set_bit_to_one(mos_t_qry_bmp* bitmap, uint64_t row_id) {
    uint64_t word = row_id / 64;
    uint64_t bitmask = (1ULL << (row_id & 63)); // find remainder (%64) and set the bit to 1
    bitmap->data[word] = bitmap->data[word] |= bitmask;
}

/**
 * Returns the bit at the given row_id
 */
int mos_get_bit_at_row_id(mos_t_qry_bmp* bitmap, uint64_t row_id) {
    uint64_t word = row_id / 64;
    return (bitmap->data[word] >> (row_id & 63)) & 1;
}

void mos_storage_put(mos_t_storage* storage, uint64_t id, void* record_data) {
    mos_t_header* mmap_header = mos_accessor_header(&storage->header_region);
    uint64_t last_deleted_row_id = mmap_header->state.last_deleted_row_id;
    uint64_t next_free_row_id = mmap_header->state.next_free_row_id;

    //use free record space (of older record that was deleted/evicted)
    if(last_deleted_row_id != MOS_NULL_OFFSET
        && (mos_check_record_bounds(mmap_header, last_deleted_row_id) == VALID)) {
        mos_t_record* mmap_record = mos_accessor_record(&storage->records_region, last_deleted_row_id, mmap_header->layout.record_size);
        //remember the value at last_deleted_row_id (Linked Free List) as new last_deleted_row_id
        mmap_header->state.last_deleted_row_id = *((uint64_t*)mmap_record);
        mos_put_internal(storage, id, record_data, last_deleted_row_id);
        mmap_header->state.last_deleted_row_id = MOS_NULL_OFFSET;
    } 
    //otherwise try to append to bottom of records
    else if (mos_check_record_bounds(mmap_header, next_free_row_id) == VALID) {
        mos_put_internal(storage, id, record_data, next_free_row_id);
        mmap_header->state.next_free_row_id += 1;
    } else {
        mos_utils_report_error("[mos_core]: Cannot put record in storage file. Storage is full.");
    }
}

void mos_storage_get_string(mos_t_storage* storage, mos_t_string_desc* sd, char** result) {
    if (sd->str_offset == MOS_NULL_OFFSET || sd->str_len == 0) {
        result = NULL;
        return;
    }
    mos_t_header* mmap_header = mos_accessor_header(&storage->header_region);
    mos_string_get(storage->string_silo_region.region_base, &mmap_header->string_silo, sd, result);
}

/**
 * Reconstructs the user defined record from an internal record.
 * Strings are fetched from the string silo.
 * Vectors are fetched from the hnsw index.
 * ...
 */
const uint8_t* mos_storage_construct_external_record(mos_t_storage* storage, mos_t_record* internal_record) {
    mos_t_header* mmap_header = mos_accessor_header(&storage->header_region);
    mos_t_layout layout = mmap_header->layout;
    uint8_t* external_record = malloc(layout.record_data_size_external);
    mos_t_attr* mmap_attributes = mos_accessor_header_attributes(&storage->header_region);
    for (size_t i = 0; i < mmap_header->attribute_count; i++)
    {
        mos_t_attr attribute = mmap_attributes[i];
        uint8_t* internal_attr_data = MOS_GET_PTR(internal_record->data, attribute.field_offset_internal);
        
        uint8_t data_buffer[attribute.byte_size_external];
        switch (attribute.type) {
            case MOS_ATTR_TYPE_INTERNAL_STRING_DESC: {
                mos_t_string_desc* sd = (mos_t_string_desc*)internal_attr_data;
                mos_t_string* dest_string = (mos_t_string*)data_buffer;
                mos_storage_get_string(storage, sd, &dest_string->str);
                dest_string->str_len = sd->str_len;
                break;
            }
            //TODO: return vector for hnsw node id in record
            case MOS_ATTR_TYPE_INTERNAL_HNSW_NODE: {
                uint64_t hnsw_node_id;
                memcpy(&hnsw_node_id, internal_attr_data, sizeof(hnsw_node_id));
                //TODO: get vector and put it into external_record
                //mos_t_float_vector* dest_vector = (mos_t_float_vector*)data_buffer;
                //mos_idx_hnsw_get(idx, &attribute, node_desc, &dest_vector->vector);
                //dest_vector->vector_dim = header->/* vector_dim wherever stored */;
                break;
            }
            default:
                memcpy(data_buffer, internal_attr_data, sizeof(data_buffer));
        }
        memcpy(external_record + attribute.field_offset_external, data_buffer, sizeof(data_buffer));
    }
    return external_record;
}

const void* mos_storage_get_data_for_row_id(mos_t_storage* storage, uint64_t row_id) {
    mos_t_header* mmap_header = mos_accessor_header(&storage->header_region);
    mos_t_qry_bmp* mmap_valid_bitmap = mos_accessor_bitmap(&storage->valid_bitmap_region);
    //there is no need to get full record if it is not valid
    if(row_id >= 0 && mos_get_bit_at_row_id(mmap_valid_bitmap, row_id)) {
        mos_t_record* record = mos_accessor_record(&storage->records_region, row_id, mmap_header->layout.record_size);
        return mos_storage_construct_external_record(storage, record);
    }
    return NULL;
}

const void* mos_storage_get(mos_t_storage* storage, uint64_t id) {
    mos_t_header* mmap_header = mos_accessor_header(&storage->header_region);
    mos_t_idx_data* idx_id_data = mos_accessor_idx_data(storage->index_regions, mmap_header->index_count, MOS_ID_INDEX_POS);

    mos_t_idx_context idx_context = {
        .idx_data = idx_id_data,
        .idx_type = MOS_IDX_HASH_MAP,   // id index is always of type hashmap
        .kind = &storage->arena_region
    };

    uint8_t* key_ptr = (uint8_t*)&id;

    mos_t_id_list result_ids = {0};
    if(mos_idx_get(&idx_context, key_ptr, &result_ids) != 0) {
        printf("[mos_core]: Record %" PRId64 " not found.\n", id);
        return NULL;
    }

    if(result_ids.count != 1) {
        printf("[mos_core]: Cannot get record with unique id %" PRId64 ". More than one record found for this id.\n", id);
        return NULL;
    }

    return mos_storage_get_data_for_row_id(storage, result_ids.ids[0]);
}

const mos_t_qry_bmp* mos_storage_search(mos_t_storage* storage, mos_t_qry* query) {
    return mos_qry_process_search(storage, query);
}

void mos_storage_remove(mos_t_storage* storage, uint64_t id) {
    if(!storage) {
        mos_utils_report_error("[mos_core]: Cannot remove. mos_t_storage instance is null.");
        return;
    }

    mos_t_header* mmap_header = mos_accessor_header(&storage->header_region);
    mos_t_idx_data* idx_id_data = mos_accessor_idx_data(storage->index_regions, mmap_header->index_count, MOS_ID_INDEX_POS);

    mos_t_idx_context idx_context = {
        .idx_data = idx_id_data,
        .idx_type = MOS_IDX_HASH_MAP,   // id index is always of type hashmap
        .kind = &storage->arena_region
    };

    uint8_t* key_ptr = (uint8_t*)&id;
    mos_t_id_list result_ids = {0};
    if(mos_idx_get(&idx_context, key_ptr, &result_ids) != 0) {
        printf("[mos_core]: Nothing to remove. Record %" PRId64 " not found.\n", id);
        return;
    }

    if(result_ids.count != 1) {
        printf("[mos_core]: Cannot remove record with id %" PRId64 ". More than one record found for this id.\n", id);
        return;
    }

    uint64_t record_row_id = result_ids.ids[0];

    //records are at least 64 bits
    mos_t_record* record = mos_accessor_record(&storage->records_region, record_row_id, mmap_header->layout.record_size);
    //invalidate record
    mos_t_qry_bmp* mmap_valid_bitmap = mos_accessor_bitmap(&storage->valid_bitmap_region);
    mos_set_bit_to_zero(mmap_valid_bitmap, record_row_id);

    mos_t_idx_descriptor* mmap_index_descriptors = mos_accessor_header_index_descriptors(&storage->header_region);
    mos_t_attr* mmap_attributes = mos_accessor_header_attributes(&storage->header_region);
    //skip id index
    for (uint64_t i = 1; i < mmap_header->index_count; i++) {
        mos_t_idx_descriptor* index_descriptor = mmap_index_descriptors + i;
        mos_t_idx_data* index_data = mos_accessor_idx_data(storage->index_regions, mmap_header->index_count, index_descriptor->index_region_pos);

        const mos_t_idx_context idx_context = {
            .idx_data = index_data,
            .idx_type = index_descriptor->type,
            .kind.hmap.arena_region = &storage->arena_region
        };

        //TODO: Rework remove. Key might not match for every index.
        mos_t_attr* attribute = mos_get_attribute_for_attribute_name(mmap_attributes, mmap_header->attribute_count, index_descriptor->attribute_name);
        uint8_t* key = record->data + attribute->field_offset_internal;
        size_t key_len = attribute->byte_size_internal;
        mos_idx_remove_value(&idx_context, key, key_len, record_row_id);
    }
    mos_idx_id_remove(storage, id);
}

void mos_print_header(mos_t_header* header) {
    printf("[mos_core]: Storage Header:\n");
    printf("[mos_core]: ------------------\n");
    printf("[mos_core]: identifier %" PRIu64 "\n", header->identifier);
    printf("[mos_core]: attribute_count %" PRIu64 "\n", header->attribute_count);
    printf("[mos_core]: index_count %" PRIu64 "\n", header->index_count);
    printf("[mos_core]: max_records %" PRIu64 "\n", header->max_records);

    mos_print_layout(&header->layout);
    printf("\n");
    printf("[mos_core]: Storage Header State:\n");
    printf("[mos_core]: ------------------\n");
    printf("[mos_core]: next_free_row_id %" PRIu64 "\n", header->state.next_free_row_id);
    printf("[mos_core]: last_deleted_row_id %" PRIu64 "\n", header->state.last_deleted_row_id);
    printf("[mos_core]: last_deleted string length %" PRIu32 "\n", header->string_silo.last_deleted.str_len);
    printf("[mos_core]: last_deleted string offset %" PRIu64 "\n", header->string_silo.last_deleted.str_offset);
}

void mos_print_layout(mos_t_layout* layout) {
    printf("[mos_core]: Storage Header Sizes:\n");
    printf("[mos_core]: ------------------\n");
    printf("[mos_core]: header_size %" PRIu64 "\n", layout->header_size);
    printf("[mos_core]: valid_bitmap_size %" PRIu64 "\n", layout->valid_bitmap_size);
    printf("[mos_core]: ready_bitmap_size %" PRIu64 "\n", layout->ready_bitmap_size);
    printf("[mos_core]: record_size %" PRIu64 "\n", layout->record_size);
    printf("[mos_core]: record_data_size %" PRIu64 "\n", layout->record_data_size);
    printf("[mos_core]: records_size %" PRIu64 "\n", layout->records_size);
    printf("[mos_core]: string_silo_size %" PRIu64 "\n", layout->string_silo_size);
}

void mos_print_info(mos_t_storage* storage) {
    mos_t_header* mmap_header = mos_accessor_header(&storage->header_region);
    mos_print_header(mmap_header);
}

void mos_print_state(mos_t_storage* storage) {
    mos_t_header* mmap_header = mos_accessor_header(&storage->header_region);
    mos_t_state state = mmap_header->state;

    printf("\n");
    printf("[mos_core]: Storage State:\n");
    printf("[mos_core]: ------------------\n");
    printf("[mos_core]: next_free_row_id %" PRIu64 "\n", state.next_free_row_id);
    printf("[mos_core]: last_deleted_row_id %" PRIu64 "\n", state.last_deleted_row_id);
}