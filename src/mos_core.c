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

/* =========================================================================
   1. FORWARD DECLARATIONS
   ========================================================================= */
size_t mos_calc_bitmap_size(mos_t_config* cfg);
size_t mos_calc_record_size(mos_t_config* cfg);
size_t mos_calc_record_data_size_internal(mos_t_config* cfg);
size_t mos_calc_attributes_size(mos_t_config* cfg);
size_t mos_calc_indexes_size(mos_t_config* cfg);
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

size_t mos_calc_indexes_size(mos_t_config* cfg) {
    return sizeof(mos_t_idx) * cfg->index_count;
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
    size_t offset = 0;
    size_t header_size = MOS_ALIGN_UP(sizeof(mos_t_header), MOS_PAGE_SIZE);
    size_t attribute_size = MOS_ALIGN_UP(mos_calc_attributes_size(cfg), MOS_PAGE_SIZE);
    size_t indexes_size = MOS_ALIGN_UP(mos_calc_indexes_size(cfg), MOS_PAGE_SIZE);
    size_t bit_map_size = MOS_ALIGN_UP(mos_calc_bitmap_size(cfg), MOS_PAGE_SIZE);
    size_t single_record_size = mos_calc_record_size(cfg);
    size_t record_data_size = cfg->attributes_byte_size_internal;
    size_t record_data_size_external = cfg->attributes_byte_size_external;
    size_t total_records_size = MOS_ALIGN_UP((single_record_size * cfg->max_records), MOS_PAGE_SIZE);
    size_t index_data_size = MOS_ALIGN_UP(mos_calc_indexes_data_size(cfg), MOS_PAGE_SIZE);

    //set sizes
    layout->header_size = header_size;
    layout->attributes_size = attribute_size;
    layout->indexes_size = indexes_size;
    layout->ready_bitmap_size = bit_map_size;
    layout->valid_bitmap_size = bit_map_size;
    layout->record_size = single_record_size;
    layout->record_data_size = record_data_size;
    layout->record_data_size_external = record_data_size_external;
    layout->records_size = total_records_size;
    layout->index_data_size = index_data_size;

    //later implement resizing
    uint64_t string_silo_size = MOS_AVG_STRING_LEN * cfg->string_attribute_count * cfg->max_records;
    // + 20%
    string_silo_size += string_silo_size * 0.2;
    layout->string_silo_size = MOS_ALIGN_UP(string_silo_size, MOS_PAGE_SIZE);

    layout->offset_header = 0;
    //evaluating offsets. Header starts at offset 0
    offset += header_size;
    layout->offset_attributes = offset;

    offset += attribute_size;
    layout->offset_indexes = offset;

    offset += indexes_size;
    layout->offset_valid_bitmap = offset;

    offset += bit_map_size;
    layout->offset_ready_bitmap = offset;
    
    offset += bit_map_size;
    layout->offset_records = offset;

    offset+= total_records_size;
    layout->offset_index_data = offset;

    offset += index_data_size;
    layout->offset_string_silo = offset;

    offset += layout->string_silo_size;

    layout->file_size = offset;
}

uint64_t mos_get_current_time_millis() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    //tv_usec = microseconds
    return (int64_t)tv.tv_sec * 1000LL + (tv.tv_usec / 1000LL);
}
void mos_idx_id_put(mos_t_storage* storage, uint64_t id, uint64_t record_row_id) {
    mos_t_idx* id_idx = storage->indexes;
    mos_t_idx_data* id_idx_data = MOS_GET_PTR(storage->index_data, id_idx->index_offset);
    uint8_t* key_ptr = (uint8_t*)&id;
    mos_idx_put(id_idx->type, id_idx_data, key_ptr, sizeof(id), record_row_id, NULL);
}

void mos_idx_id_remove(mos_t_storage* storage, uint64_t id, uint64_t record_row_id) {
    mos_t_idx* id_idx = storage->indexes;
    mos_t_idx_data* id_idx_data = MOS_GET_PTR(storage->index_data, id_idx->index_offset);
    uint8_t* key_ptr = (uint8_t*)&id;
    mos_idx_remove_value(id_idx->type, id_idx_data, key_ptr, sizeof(id));
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
    mos_t_idx* indexes = storage->indexes;
    //skip id index
    for (uint64_t i = 1; i < storage->storage_header->index_count; i++) {
        mos_t_idx* idx = indexes + i;
        mos_t_idx_data* idx_data = MOS_GET_PTR(storage->index_data, idx->index_offset);
        mos_t_attr* attribute = mos_get_attribute_for_attribute_name(storage->attributes, storage->storage_header->attribute_count, idx->attribute_name);

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
            mos_idx_put_result put_result;
            put_result.put_result = NULL;
            mos_idx_put(idx->type, idx_data, attr_base, byte_size, record_row_id, &put_result);

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
    mos_t_header* header = storage->storage_header;
    uint64_t record_size = header->layout.record_size;

    //record is no longer valid for search etc.
    mos_set_bit_to_zero(storage->valid_bitmap, record_row_id);

    uint8_t record_buffer[record_size];
    memset(record_buffer, 0, record_size);
    mos_t_record* record_buffer_ptr = (mos_t_record*)record_buffer;
    record_buffer_ptr->id = id;
    uint8_t* record_buffer_data_ptr = record_buffer_ptr->data;

    //check if any attributes need special treatment
    for(uint64_t i = 0; i < header->attribute_count; i++) {
        mos_t_attr* attribute = &storage->attributes[i];

        //vectors are exclusively handled via mos_indexes_put (HNSW is mandatory for them)
        if(attribute->type == MOS_ATTR_TYPE_INTERNAL_HNSW_NODE) {
            continue;
        }
        
        uint8_t* external_attr = ((uint8_t*)external_record_data) + attribute->field_offset_external;
        uint8_t* internal_attr = record_buffer_data_ptr + attribute->field_offset_internal;

        if(attribute->type == MOS_ATTR_TYPE_INTERNAL_STRING_DESC) {
            mos_t_string* str = (mos_t_string*)external_attr;
            mos_t_string_desc str_out;
            mos_string_put(storage->string_silo_base, &storage->storage_header->string_silo, str, &str_out);
            memcpy(internal_attr, &str_out, attribute->byte_size_internal);
        } else {
            //plain scalar — always a direct copy, indexed or not
            memcpy(internal_attr, external_attr, attribute->byte_size_internal);
        }
    }
    mos_indexes_put(storage, id, (uint8_t*)external_record_data, record_buffer_data_ptr, record_row_id);

    record_buffer_ptr->flags = 0;
    record_buffer_ptr->timestamp = mos_get_current_time_millis();

    mos_t_record* record_storage = (mos_t_record*)MOS_GET_PTR(storage->entries, record_row_id * record_size);
    memcpy(record_storage, record_buffer, record_size);

    //record is valid for search etc.
    mos_set_bit_to_one(storage->valid_bitmap, record_row_id);
}

/* =========================================================================
   3. Header Function DEFINITIONS
   ========================================================================= */

mos_t_config* mos_init_internal_config(mos_t_storage_config* external_cfg) {
    mos_t_config* internal_cfg = calloc(1, sizeof(mos_t_config));

    if(!internal_cfg) {
        mos_utils_report_error("Allocation error. mos_t_storage_config cannot be allocated. Cannot initialize internal config.");
        return NULL;
    }

    mos_t_attr* internal_attributes = calloc(external_cfg->attribute_count, sizeof(mos_t_attr));
    if(!internal_attributes) {
        free(internal_cfg);
        mos_utils_report_error("Allocation error. mos_t_attr_internal cannot be allocated. Cannot initialize internal config.");
        return NULL;
    }

    //+1 for the fixed id index. Every record has an id.
    mos_t_idx* indexes = calloc(external_cfg->index_count + 1, sizeof(mos_t_idx));
    if(!indexes) {
        free(internal_cfg);
        free(internal_attributes);
        mos_utils_report_error("Allocation error. mos_t_idx cannot be allocated. Cannot initialize internal config.");
        return NULL;
    }

    uint64_t actual_attributes_count = 0;
    uint64_t actual_indexes_count = 0;
    uint64_t attribute_offset = 0;
    uint64_t total_attribute_byte_size_internal = 0;

    //id index
    mos_t_idx* id_index = &indexes[0];
    id_index->id = 0;
    id_index->type = MOS_IDX_HASH_MAP;
    strncpy(id_index->attribute_name, "id", MOS_ATTR_NAME_LENGTH - 2);
    id_index->attribute_name[MOS_ATTR_NAME_LENGTH - 1] = '\0';
    id_index->index_offset = 0;
    actual_indexes_count++;

    for(uint64_t i = 0; i < external_cfg->attribute_count; i++) {
        mos_t_attr_config external_attribute = external_cfg->attributes[i];
        mos_t_attr* internal_attribute = &internal_attributes[i];

        if(external_attribute.indexed) {
            if(actual_indexes_count == (external_cfg->index_count + 1)) {
                //too many indexes
                mos_utils_report_error("Invalid mos_t_config. Surpassing expected index_count.");
                return NULL;
            }

            mos_t_idx_config* attr_idx_config = NULL;
            for(uint64_t j = 0; j < external_cfg->index_count; j++) {
                mos_t_idx_config* idx_config = &external_cfg->indexes[j];
                if(strncmp(external_attribute.name, idx_config->attribute_name, strlen(external_attribute.name)) == 0) {
                    attr_idx_config = idx_config;
                    break;
                }
            }
            if(!attr_idx_config) {
                //attribute should be indexed but index configuration is missing -> invalid configuration
                mos_utils_report_error("Invalid mos_t_config. Missing index configuration for attribute %s", external_attribute.name);
                return NULL;
            }
            
            mos_t_idx* index = &indexes[actual_indexes_count];
            strncpy(index->attribute_name, external_attribute.name, MOS_ATTR_NAME_LENGTH - 2);
            index->attribute_name[MOS_ATTR_NAME_LENGTH - 1] = '\0';
            index->type = attr_idx_config->type;
            memcpy(&index->params, &attr_idx_config->params, sizeof(index->params));
            index->id = actual_indexes_count++;
            internal_attribute->indexed = 1;
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
    internal_cfg->indexes = indexes;
    internal_cfg->attribute_count = actual_attributes_count;
    internal_cfg->index_count = actual_indexes_count;
    internal_cfg->max_records = external_cfg->max_records;
    internal_cfg->storage_path = external_cfg->storage_path;

    return internal_cfg;
}

int mos_validate_config(mos_t_storage_config* cfg) {
    if(cfg == NULL) {
        mos_utils_report_error("mos_t_config is NULL. mos_t_config is invalid.");
        return INVALID;
    }

    if(cfg->max_records == 0) {
        mos_utils_report_error("mos_t_config->max_records is 0. mos_t_config is invalid.");
        return INVALID;
    }

    if(cfg->attribute_count == 0) {
        mos_utils_report_error("mos_t_config->attribute_count is 0. mos_t_config is invalid.");
        return INVALID;
    }

    if(*cfg->storage_path == '\0') {
        mos_utils_report_error("mos_t_config->storage_path is 0. mos_t_config is invalid.");
        return INVALID;
    }

    for(uint64_t i = 0; i < cfg->attribute_count; i++) {
        mos_t_attr_config attribute = cfg->attributes[i];

        if(attribute.name[0] == '\0') {
            mos_utils_report_error("attribute name is not set. mos_t_config is invalid.");
            return INVALID;
        }

        if(attribute.byte_size == 0) {
            mos_utils_report_error("attribute byte_size is 0. mos_t_config is invalid.");
            return INVALID;
        }

        if(attribute.type < MOS_ATTR_MIN || attribute.type >= MOS_ATTR_MAX) {
            mos_utils_report_error("attribute type is invalid. mos_t_config is invalid.");
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
                mos_utils_report_error("Invalid mos_t_config. Missing index configuration for attribute %s", attribute.name);
                return INVALID;
            }
            if(!mos_attr_supports_index(attribute.type, attr_idx_config->type)) {
                //attribute should be indexed but provided index is not compatible with attribute type -> invalid configuration
                mos_utils_report_error("Invalid mos_t_config. Index %s not compatible with attribute %s", MOS_IDX_TYPE_NAMES[attr_idx_config->type], attribute.name);
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
                mos_utils_report_error("Invalid mos_t_config. Attributes %s and %s are overlapping", attribute.name, attribute2.name);
                return INVALID;
            }
        }
    }

    return VALID;
}

void mos_map_storage_pointers(mos_t_storage* storage, void* mmap_ptr, mos_t_layout* layout) {
    storage->storage_header = (mos_t_header*)mmap_ptr;
    storage->attributes = MOS_GET_PTR(mmap_ptr, layout->offset_attributes);
    storage->idx_id = MOS_GET_PTR(mmap_ptr, layout->offset_indexes);
    storage->indexes = MOS_GET_PTR(mmap_ptr, layout->offset_indexes);
    storage->idx_id_data = MOS_GET_PTR(mmap_ptr, layout->offset_index_data);
    storage->index_data = MOS_GET_PTR(mmap_ptr, layout->offset_index_data);
    storage->entries = MOS_GET_PTR(mmap_ptr, layout->offset_records);
    storage->ready_bitmap = MOS_GET_PTR(mmap_ptr, layout->offset_ready_bitmap);
    storage->valid_bitmap = MOS_GET_PTR(mmap_ptr, layout->offset_valid_bitmap);
    storage->string_silo_base = MOS_GET_PTR(mmap_ptr, layout->offset_string_silo);
}

mos_t_storage* mos_load_storage(const char* file_path) {
    if(file_path == NULL) {
        mos_utils_report_error("Invalid argument file_path = NULL. Cannot load storage file.");
        return NULL;
    }

    //load file
    FILE* storage_file = fopen(file_path, "r+");

    if(storage_file == NULL) {
        mos_utils_report_error("Cannot load storage file. Call mos_create_storage(...) to create a new file.");
        return NULL;
    }

    int fd = fileno(storage_file);
    int header_page_size = MOS_ALIGN_UP(sizeof(mos_t_header), 4096);
    //map first pages to read header
    void* mmap_file_header_ptr = (void*)mos_os_mmap(fd, header_page_size);

    if(!mmap_file_header_ptr) {
        mos_utils_report_error("Cannot load storage file. Failed to map %d bytes of memory (header pages) of file %s.", header_page_size, file_path);
        return NULL;
    }

    mos_t_header* header_preview = (mos_t_header*)mmap_file_header_ptr;
    
    if(header_preview->identifier != MOS_FILE_ID) {
        mos_utils_report_error("Cannot load storage file %s. File header is invalid.", file_path);
        return NULL;
    }

    uint64_t file_size = header_preview->layout.file_size;

    //now mmap the file
    void* mmap_file_ptr = (void*)mos_os_mmap(fd, file_size);

    if(!mmap_file_ptr) {
        mos_utils_report_error("Cannot load storage file %s.", file_path);
        return NULL;
    }
    mmap_file_header_ptr = NULL;
    //unmap header, no longer needed
    mos_os_munmap(mmap_file_header_ptr, header_page_size);

    mos_t_header* header = (mos_t_header*)mmap_file_ptr;

    mos_t_storage* storage = (mos_t_storage*)calloc(1, sizeof(mos_t_storage));
    if(!storage) {
        return NULL;
    }
    storage->mmap_ptr = mmap_file_ptr;
    storage->fd = fd;

    //set file pointers
    mos_map_storage_pointers(storage, mmap_file_ptr, &header->layout);

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

mos_t_storage* mos_create_storage(const char* file_path, mos_t_storage_config* external_cfg) {
    if(file_path == NULL) {
        mos_utils_report_error("Invalid argument file_path = NULL. Cannot create storage file.");
        return NULL;
    }
    
    if(mos_validate_config(external_cfg) == INVALID) {
        mos_utils_report_error("Invalid mos_t_config. Cannot create storage file.");
        return NULL;
    }
    
    mos_t_config* internal_cfg = mos_init_internal_config(external_cfg);
    if(!internal_cfg) {
        mos_utils_report_error("Cannot create storage: internal config initialization failed.");
        return NULL;
    }

    //create file
    FILE* storage_file = fopen(file_path, "w+");

    if(storage_file == NULL) {
        mos_utils_report_error("Cannot create storage file.");
        return NULL;
    }

    int fd = fileno(storage_file);

    mos_t_layout layout;
    mos_init_layout(internal_cfg, &layout);

    size_t file_size = layout.file_size;

    if(ftruncate(fd, file_size) == -1) {
        mos_utils_report_error("Cannot truncate storage file %s to size %d", file_path, file_size);
        return NULL;
    }

    void* mmap_ptr = (void*)mos_os_mmap(fd, file_size);

    if(!mmap_ptr) {
        mos_utils_report_error("Cannot map memory for %d bytes.", file_size);
        return NULL;
    }

    mos_t_storage* storage = (mos_t_storage*)calloc(1, sizeof(mos_t_storage));
    storage->mmap_ptr = mmap_ptr;
    storage->fd = fd;

    //set file pointers
    mos_map_storage_pointers(storage, mmap_ptr, &layout);

    //writing header to file
    mos_t_header* storage_header = storage->storage_header;

    storage_header->attribute_count = internal_cfg->attribute_count;
    storage_header->index_count = internal_cfg->index_count;
    storage_header->max_records = internal_cfg->max_records;
    memcpy(&(storage_header->layout), &layout, sizeof(mos_t_layout));

    mos_t_string_silo* string_silo = &(storage_header->string_silo);
    string_silo->base_offset = layout.offset_string_silo;
    string_silo->current_offset = 0;
    string_silo->size = layout.string_silo_size;
    string_silo->last_deleted.str_len = 0;
    string_silo->last_deleted.str_offset = MOS_NULL_OFFSET;

    mos_t_state* storage_state = &(storage_header->state);
    //record offsets are calculated from record area start
    storage_state->next_free_row_id = 0;
    storage_state->last_deleted_row_id = MOS_NULL_OFFSET;

    //writing storage attributes to file
    memcpy(storage->attributes, internal_cfg->attributes, sizeof(mos_t_attr) * internal_cfg->attribute_count);

    mos_idx_create(storage, internal_cfg);

    storage_header->identifier = MOS_FILE_ID;

    mos_free_storage_config(internal_cfg);
    return storage;
}

void mos_free_storage(mos_t_storage* storage) {
    if(!storage) {
        return;
    }
    if(storage->mmap_ptr) {
        mos_os_munmap(storage->mmap_ptr, storage->storage_header->layout.file_size);
    }
    if(storage->fd >= 0) {
        if (close(storage->fd) == -1) {
            // Handle error: e.g., print to stderr or log it
            perror("Error closing storage file descriptor");
        }
        storage->fd = -1; // Mark as closed immediately
    }
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
    mos_t_header* header = storage->storage_header;
    uint64_t last_deleted_row_id = header->state.last_deleted_row_id;
    uint64_t next_free_row_id = header->state.next_free_row_id;

    //use free record space (of older record that was deleted/evicted)
    if(last_deleted_row_id != MOS_NULL_OFFSET
        && (mos_check_record_bounds(header, last_deleted_row_id) == VALID)) {
        //remember the value at last_deleted_row_id (Linked Free List) as new last_deleted_row_id
        header->state.last_deleted_row_id = *((uint64_t*)MOS_GET_PTR(storage->entries, last_deleted_row_id * header->layout.record_size));
        mos_put_internal(storage, id, record_data, last_deleted_row_id);
        header->state.last_deleted_row_id = MOS_NULL_OFFSET;
    } 
    //otherwise try to append to bottom of records
    else if (mos_check_record_bounds(header, next_free_row_id) == VALID) {
        mos_put_internal(storage, id, record_data, next_free_row_id);
        header->state.next_free_row_id += 1;
    } else {
        mos_utils_report_error("Cannot put record in storage file. Storage is full.");
    }
}

void mos_storage_get_string(mos_t_storage* storage, mos_t_string_desc* sd, char** result) {
    if (sd->str_offset == MOS_NULL_OFFSET || sd->str_len == 0) {
        result = NULL;
        return;
    }
    mos_string_get(storage->string_silo_base, &storage->storage_header->string_silo, sd, result);
}

const mos_t_record* mos_storage_get_record(mos_t_storage* storage, uint64_t id) {
    mos_t_header* header = storage->storage_header;
    mos_t_idx_data* idx_id_data = storage->idx_id_data;
    uint8_t* key_ptr = (uint8_t*)&id;
    int64_t record_row_id = mos_idx_get(idx_id_data, key_ptr);
    if(record_row_id >= 0) {
        mos_t_record* record = MOS_GET_PTR(storage->entries, record_row_id * header->layout.record_size);
        return record;
    }
    return NULL;
}

/**
 * Reconstructs the user defined record from an internal record.
 * Strings are fetched from the string silo.
 * Vectors are fetched from the hnsw index.
 * ...
 */
const uint8_t* mos_storage_construct_external_record(mos_t_storage* storage, mos_t_record* internal_record) {
    mos_t_header* header = storage->storage_header;
    mos_t_layout layout = header->layout;
    uint8_t* external_record = malloc(layout.record_data_size_external);
    for (size_t i = 0; i < header->attribute_count; i++)
    {
        mos_t_attr attribute = storage->attributes[i];
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
    mos_t_header* header = storage->storage_header;

    //there is no need to get full record if it is not valid
    if(row_id >= 0 && mos_get_bit_at_row_id(storage->valid_bitmap, row_id)) {
        mos_t_record* record = MOS_GET_PTR(storage->entries, row_id * header->layout.record_size);
        return mos_storage_construct_external_record(storage, record);
    }
    return NULL;
}

const void* mos_storage_get(mos_t_storage* storage, uint64_t id) {
    mos_t_idx_data* idx_id_data = storage->idx_id_data;
    uint8_t* key_ptr = (uint8_t*)&id;
    int64_t record_row_id = mos_idx_get(idx_id_data, key_ptr);

    if(record_row_id == VALUE_NOT_FOUND) {
        return NULL;
    }

    return mos_storage_get_data_for_row_id(storage, record_row_id);
}

const mos_t_qry_bmp* mos_storage_search(mos_t_storage* storage, mos_t_qry* query) {
    return mos_qry_process_search(storage, query);
}

void mos_storage_remove(mos_t_storage* storage, uint64_t id) {
    if(!storage) {
        mos_utils_report_error("Cannot remove. mos_t_storage instance is null.");
        return;
    }

    mos_t_header* header = storage->storage_header;
    mos_t_idx_data* idx_id_data = storage->idx_id_data;
    uint8_t* key_ptr = (uint8_t*)&id;
    int64_t record_row_id = mos_idx_get(idx_id_data, key_ptr);
    if(record_row_id < 0) {
        printf("Nothing to remove. Record %" PRId64 " not found.", id);
        return;
    }

    //records are at least 64 bits
    mos_t_record* record = MOS_GET_PTR(storage->entries, record_row_id * header->layout.record_size);
    //invalidate record
    mos_set_bit_to_zero(storage->valid_bitmap, record_row_id);

    mos_t_idx* indexes = storage->indexes;
    //skip id index
    for (uint64_t i = 1; i < header->index_count; i++) {
        mos_t_idx* index = indexes + i;
        mos_t_idx_data* index_data = MOS_GET_PTR(storage->index_data, index->index_offset);

        //TODO: Rework remove. Key might not match for every index.
        mos_t_attr* attribute = mos_get_attribute_for_attribute_name(storage->attributes, header->attribute_count, index->attribute_name);
        uint8_t* key = record->data + attribute->field_offset_internal;
        size_t key_len = attribute->byte_size_internal;
        mos_idx_remove_value(index->type, index_data, key, key_len);
    }
    mos_idx_id_remove(storage, id, record_row_id);
}

void mos_print_header(mos_t_header* header) {
    printf("Storage Header:\n");
    printf("------------------\n");
    printf("identifier %" PRIu64 "\n", header->identifier);
    printf("attribute_count %" PRIu64 "\n", header->attribute_count);
    printf("index_count %" PRIu64 "\n", header->index_count);
    printf("max_records %" PRIu64 "\n", header->max_records);

    mos_print_layout(&header->layout);

    printf("\nStorage Header State:\n");
    printf("------------------\n");
    printf("next_free_row_id %" PRIu64 "\n", header->state.next_free_row_id);
    printf("last_deleted_row_id %" PRIu64 "\n", header->state.last_deleted_row_id);
    printf("last_deleted string length %" PRIu32 "\n", header->string_silo.last_deleted.str_len);
    printf("last_deleted string offset %" PRIu64 "\n", header->string_silo.last_deleted.str_offset);
}

void mos_print_layout(mos_t_layout* layout) {
    printf("Storage Header Sizes:\n");
    printf("------------------\n");
    printf("header_size %" PRIu64 "\n", layout->header_size);
    printf("attributes_size %" PRIu64 "\n", layout->attributes_size);
    printf("indexes_size %" PRIu64 "\n", layout->indexes_size);
    printf("valid_bitmap_size %" PRIu64 "\n", layout->valid_bitmap_size);
    printf("ready_bitmap_size %" PRIu64 "\n", layout->ready_bitmap_size);
    printf("record_size %" PRIu64 "\n", layout->record_size);
    printf("record_data_size %" PRIu64 "\n", layout->record_data_size);
    printf("records_size %" PRIu64 "\n", layout->records_size);
    printf("index_data_size %" PRIu64 "\n", layout->index_data_size);
    printf("string_silo_size %" PRIu64 "\n", layout->string_silo_size);
    printf("file_size %" PRIu64 "\n", layout->file_size);

    printf("\nStorage Header Offsets:\n");
    printf("------------------\n");
    printf("offset_header %" PRIu64 "\n", layout->offset_header);
    printf("offset_attributes %" PRIu64 "\n", layout->offset_attributes);
    printf("offset_indexes %" PRIu64 "\n", layout->offset_indexes);
    printf("offset_valid_bitmap %" PRIu64 "\n", layout->offset_valid_bitmap);
    printf("offset_ready_bitmap %" PRIu64 "\n", layout->offset_ready_bitmap);
    printf("offset_records %" PRIu64 "\n", layout->offset_records);
    printf("offset_index_data %" PRIu64 "\n", layout->offset_index_data);
    printf("offset_string_silo %" PRIu64 "\n", layout->offset_string_silo);
}

void mos_print_info(mos_t_storage* storage) {
    mos_print_header(storage->storage_header);
}

void mos_print_state(mos_t_storage* storage) {
    mos_t_header* header = storage->storage_header;
    mos_t_state state = header->state;

    printf("\nStorage State:\n");
    printf("------------------\n");
    printf("next_free_row_id %" PRIu64 "\n", state.next_free_row_id);
    printf("last_deleted_row_id %" PRIu64 "\n", state.last_deleted_row_id);
}