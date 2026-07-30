#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <errno.h>
#include <unistd.h>
#include <inttypes.h>
#include <sys/time.h>
#include <unistd.h> // Sometimes needed for POSIX definitions

#include "../include/mos.h"
#include "../include/mos_utils.h"
#include "../include/mos_internal.h"
#include "../include/mos_idx.h"
#include "../include/mos_os.h"
#include "../include/mos_qry.h"
#include "../include/mos_string.h"

/* =========================================================================
   1. FORWARD DECLARATIONS
   ========================================================================= */
size_t mos_calc_bitmap_size(mos_t_config* cfg);
size_t mos_calc_record_size(mos_t_config* cfg);
size_t mos_calc_record_data_size(mos_t_config* cfg);
size_t mos_calc_attributes_size(mos_t_config* cfg);
size_t mos_calc_indexes_size(mos_t_config* cfg);
size_t mos_calc_indexes_data_size(mos_t_config* cfg);
void mos_put_string(mos_t_storage* storage, mos_t_record* record, mos_t_attr_info* attribute, mos_t_string* str);
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
    return sizeof(mos_t_attr_info) * cfg->attribute_count;
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
    size += mos_calc_record_data_size(cfg);

    //align up to mutiple of 8 for better CPU handling.
    return MOS_ALIGN_UP(size, 8);
}

/*
* Iterates over all mos_t_attr_info and sums up the size of each attribute type.
* @return size_t: size of the data of a record
*/
size_t mos_calc_record_data_size(mos_t_config* cfg) {
    size_t size = 0;

    for(uint64_t i = 0; i < cfg->attribute_count; i++) {
        mos_t_attr_info* attribute = &cfg->attributes[i];
        //at this point, all attribute types should be valid due to prior validation
        size += TYPE_SIZES[attribute->type];
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
    size_t record_data_size = mos_calc_record_data_size(cfg);
    size_t total_records_size = MOS_ALIGN_UP((single_record_size * cfg->max_records), MOS_PAGE_SIZE);
    size_t index_data_size = MOS_ALIGN_UP(mos_calc_indexes_data_size(cfg), MOS_PAGE_SIZE);

    size_t string_attr_count = 0;
    for(uint64_t i = 0; i < cfg->attribute_count; i++) {
        if(cfg->attributes[i].type == MOS_ATTR_TYPE_STRING) {
            string_attr_count++;
        }
    }

    //set sizes
    layout->header_size = header_size;
    layout->attributes_size = attribute_size;
    layout->indexes_size = indexes_size;
    layout->ready_bitmap_size = bit_map_size;
    layout->valid_bitmap_size = bit_map_size;
    layout->record_size = single_record_size;
    layout->record_data_size = record_data_size;
    layout->records_size = total_records_size;
    layout->index_data_size = index_data_size;

    //later implement resizing
    uint64_t string_silo_size = MOS_AVG_STRING_LEN * string_attr_count * cfg->max_records;
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
    mos_t_idx_data* id_idx_data = MOS_GET_PTR(storage->mmap_ptr, id_idx->offset_file);
    uint8_t* key_ptr = (uint8_t*)&id;
    mos_idx_put(id_idx->type, id_idx_data, key_ptr, sizeof(id), record_row_id);
}

void mos_idx_id_remove(mos_t_storage* storage, uint64_t id, uint64_t record_row_id) {
    mos_t_idx* id_idx = storage->indexes;
    mos_t_idx_data* id_idx_data = MOS_GET_PTR(storage->mmap_ptr, id_idx->offset_file);
    uint8_t* key_ptr = (uint8_t*)&id;
    mos_idx_put(id_idx->type, id_idx_data, key_ptr, sizeof(id), record_row_id);
}

void mos_indexes_put(mos_t_storage* storage, uint64_t id, void* record_data, uint64_t record_row_id) {
    mos_t_idx* indexes = storage->indexes;
    //skip id index
    for (uint64_t i = 1; i < storage->storage_header->index_count; i++) {
        mos_t_idx* idx = indexes + i;
        mos_t_idx_data* idx_data = MOS_GET_PTR(storage->mmap_ptr, idx->offset_file);
        mos_t_attr_info attribute = idx->attribute;

        uint8_t* attr_base = (uint8_t*)record_data + attribute.external_offset;

        uint32_t byte_size = 0;
        //strings once again need special treatment.
        //At this point, the string value should still be in the record.
        //Later it will be replaced by the pointer to the string silo.
        if(attribute.type == MOS_ATTR_TYPE_STRING) {
            mos_t_string* str = ((mos_t_string*)attr_base);
            byte_size = str->str_len;
            attr_base = (uint8_t*)str->str;
        } else {
            byte_size = attribute.byte_size;
        }

        //get the attribute value from the record. This value will be used for creating the hash.
        mos_idx_put(idx->type, idx_data, attr_base, byte_size, record_row_id);
    }
    mos_idx_id_put(storage, id, record_row_id);
}

void mos_put_internal(mos_t_storage* storage, uint64_t id, void* record_data, uint64_t record_row_id) {
    mos_t_record* record = (mos_t_record*)MOS_GET_PTR(storage->entries, record_row_id * storage->storage_header->layout.record_size);
    mos_t_header* header = storage->storage_header;

    mos_set_bit_to_zero(storage->valid_bitmap, record_row_id);
    record->id = id;

    //record not yet valid, but ok to copy already because valid_bitmap informs about it
    memcpy(record->data, record_data, header->layout.record_data_size);

    mos_indexes_put(storage, id, record->data, record_row_id);

    //check if any attributes need special treatment
    for(uint64_t i = 0; i < header->attribute_count; i++) {
        mos_t_attr_info attribute = storage->attributes[i];
        //put_string has to happen after mos_indexes_put
        if(attribute.type == MOS_ATTR_TYPE_STRING) {
            mos_t_string* str = (mos_t_string*)(record->data + attribute.external_offset);
            mos_put_string(storage, record, &attribute, str);
        }
    }

    record->flags = 0;
    record->timestamp = mos_get_current_time_millis();

    mos_set_bit_to_one(storage->valid_bitmap, record_row_id);
}

/*
------------    |
------------    |
------------    |
------------    |
index1index2    v
------------    ^
--------str4    |
str3str2str1    |
*/
void mos_put_string(mos_t_storage* storage, mos_t_record* record, mos_t_attr_info* attribute, mos_t_string* str) {
    //mos_t_string_desc* str_desc = (mos_t_string_desc*)(record->data + attribute->external_offset);
    // Put the string descriptor at the exact same position where the actual string was stored in the record. 
    //  This is neccessary to get fixed size records. The actual string is moved into the string silo by mos_string_put.
    //  TODO: Do not use the same pointer for both, the actual string and the string descriptor, 
    //        as this can go horribly wrong if mos_string_put writes at the descriptor address before coping the actual string from the address.
    mos_string_put(storage->string_silo_base, &storage->storage_header->string_silo, str, str);
}

/* =========================================================================
   3. Header Function DEFINITIONS
   ========================================================================= */

mos_t_config* mos_init_internal_config(mos_t_config* external_cfg) {
    mos_t_config* internal_cfg = malloc(sizeof(mos_t_config));

    mos_t_attr_info* attributes = malloc(sizeof(mos_t_attr_info) * external_cfg->attribute_count);
    //+1 for the fixed id index. Every record has an id.
    mos_t_idx* indexes = malloc(sizeof(mos_t_idx) * (external_cfg->index_count + 1));
    *internal_cfg = *external_cfg;

    internal_cfg->attributes = attributes;
    internal_cfg->indexes = indexes;
    internal_cfg->index_count = external_cfg->index_count + 1;

    strncpy(indexes->name, "idx_id", 31);
    indexes->name[31] = '\0';
    indexes->type = MOS_IDX_HASH_MAP;
    strncpy(indexes->attribute.name, "id", 31);
    indexes->attribute.name[31] = '\0';

    indexes->attribute.type = MOS_ATTR_TYPE_UINT64;
    indexes->attribute.byte_size = 8;
    indexes->attribute.external_offset = MOS_NULL_OFFSET;

    memcpy(internal_cfg->attributes, external_cfg->attributes, sizeof(mos_t_attr_info) * external_cfg->attribute_count);
    memcpy(internal_cfg->indexes + 1, external_cfg->indexes, sizeof(mos_t_idx) * external_cfg->index_count);

    return internal_cfg;
}

int mos_validate_config(mos_t_config* cfg) {
    if(cfg == NULL) {
        mos_utils_report_error("mos_t_config is NULL. mos_t_config is invalid.");
        return INVALID;
    }

    if(cfg->max_records <= 0) {
        mos_utils_report_error("mos_t_config->max_records is <= 0. mos_t_config is invalid.");
        return INVALID;
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

mos_t_storage* mos_create_storage(const char* file_path, mos_t_config* external_cfg) {
    if(file_path == NULL) {
        mos_utils_report_error("Invalid argument file_path = NULL. Cannot create storage file.");
        return NULL;
    }
    
    mos_t_config* internal_cfg = mos_init_internal_config(external_cfg);

    if(mos_validate_config(internal_cfg) == INVALID) {
        mos_utils_report_error("Invalid mos_t_config. Cannot create storage file.");
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
    memcpy(storage->attributes, internal_cfg->attributes, sizeof(mos_t_attr_info) * internal_cfg->attribute_count);

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

const void* mos_storage_get_data_for_row_id(mos_t_storage* storage, uint64_t row_id) {
    mos_t_header* header = storage->storage_header;

    //there is no need to get full record if it is not valid
    if(row_id >= 0 && mos_get_bit_at_row_id(storage->valid_bitmap, row_id)) {
        uint8_t* record_payload = malloc(header->layout.record_data_size);
        mos_t_record* record = MOS_GET_PTR(storage->entries, row_id * header->layout.record_size);
        memcpy(record_payload, record->data, header->layout.record_data_size);
        for (size_t i = 0; i < header->attribute_count; i++)
        {
            mos_t_attr_info attribute = storage->attributes[i];
            //strings need special treatment
            if(attribute.type == MOS_ATTR_TYPE_STRING) {
                mos_t_string_desc sd = *(mos_t_string_desc*)MOS_GET_PTR(record->data, attribute.external_offset);
                mos_t_string* dest_string = (mos_t_string*)(record_payload + attribute.external_offset);
                mos_storage_get_string(storage, &sd, &dest_string->str);
                dest_string->str_len = sd.str_len;
            }
        }
        return record_payload;
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
    uint8_t* record = MOS_GET_PTR(storage->entries, record_row_id * header->layout.record_size);
    //invalidate record
    mos_set_bit_to_zero(storage->valid_bitmap, record_row_id);

    mos_t_idx* indexes = storage->indexes;
    //skip id index
    for (uint64_t i = 1; i < header->index_count; i++) {
        mos_t_idx* index = indexes + i;
        mos_t_idx_data* index_data = MOS_GET_PTR(storage->mmap_ptr, index->offset_file);

        uint8_t* key = record + offsetof(mos_t_record, data) + index->attribute.external_offset;
        size_t key_len = index->attribute.byte_size;
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