#include "../include/mos_string.h"
#include "../include/mos_utils.h"
#include "../include/mos_math.h"

//for docs see declaration in mos_string.h
void mos_string_get(void* silo_pointer, mos_t_string_silo* string_silo, mos_t_string_desc* sd, char** result) {
    // pointer directly into the mmap region
    *result = silo_pointer + string_silo->size - sd->str_offset;
}

//for docs see declaration in mos_string.h
int mos_string_put(void* silo_pointer, mos_t_string_silo* string_silo, mos_t_string* string, mos_t_string_desc* result) {
    if(string->str_len == 0) {
        return -1;
    }
    
    uint64_t size = string_silo->size;
    uint64_t current_offset = string_silo->current_offset;
    mos_t_string_desc last_deleted = string_silo->last_deleted;
    uint64_t str_len = string->str_len;

    uint64_t offset = MOS_NULL_OFFSET;

    uint64_t new_len_align_up = MOS_ALIGN_UP(str_len, MOS_MIN_STRING_ALLOC);

    if(last_deleted.str_offset != MOS_NULL_OFFSET) {
        uint64_t deleted_len_align_up = MOS_ALIGN_UP(last_deleted.str_len, MOS_MIN_STRING_ALLOC);

        int64_t align_diff = deleted_len_align_up - new_len_align_up;

        //only the first last deleted string referenced by string_silo is checked. It can be that there is a deleted area somewhere fitting the new string
        //but this would mean a O(n) traversal of the last deleted list (for n deleted strings), which we do not want for when writing strings.

        if(align_diff == 0) {
            //no split needed. New string needs same amount of memory.
            offset = last_deleted.str_offset;
            string_silo->last_deleted = *((mos_t_string_desc*)((uint8_t*)(silo_pointer) + size - last_deleted.str_offset));
        } else if (align_diff > 0) {
            //split needed. Deleted string needed more memory buckets then the new one.
            offset = last_deleted.str_offset - align_diff;
            string_silo->last_deleted = (mos_t_string_desc){
                .str_offset = last_deleted.str_offset,
                .str_len = align_diff
            };
        }
    }
    
    if(offset == MOS_NULL_OFFSET) {
        //strings are a multiple of DELETED_NODE_SIZE
        offset = current_offset + new_len_align_up;
        //at this point, we did not use a last deleted string but want to add the string on top of the silo. So we update our current_offset
        current_offset = offset;
    }

    if(offset <= size) {
        char* str_ptr = ((void*)((uint8_t*)(silo_pointer) + size - offset));
        memcpy(str_ptr, string->str, str_len);
        result->str_offset = offset;
        result->str_len = str_len;
        string_silo->current_offset = current_offset;
        return 0;
    } else {
        mos_utils_report_error("Cannot store string. String silo is full. You need to resize.");
        return -1;
    }
}

//for docs see declaration in mos_string.h
int mos_string_remove(void* silo_pointer, mos_t_string_silo* string_silo, mos_t_string_desc* sd) {
    uint64_t str_offset = sd->str_offset;
    uint64_t size = string_silo->size;
    mos_t_string_desc last_deleted = string_silo->last_deleted;

    if(str_offset != MOS_NULL_OFFSET && str_offset >= sizeof(mos_t_string_desc) && str_offset <= size) {
        mos_t_string_desc* to_delete_ptr = MOS_GET_PTR(silo_pointer, size - str_offset);

        //remember for free list
        *to_delete_ptr = last_deleted;

        string_silo->last_deleted.str_offset = str_offset;
        string_silo->last_deleted.str_len = sd->str_len;
        return 0;
    }
    return -1;
}