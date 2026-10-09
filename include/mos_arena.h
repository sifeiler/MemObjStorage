#ifndef MOS_ARENA_H
#define MOS_ARENA_H

#include <inttypes.h>
#include "mos_memory.h"
#include "mos_internal.h"
#include "mos_math.h"

#define MOS_MIN_ARENA_ALLOC 16         // must stay a power of two (for uint64_t alignment) AND >= sizeof(mos_t_arena_offset)

typedef struct mos_t_arena_offset {
    uint64_t arena_offset;
    uint64_t arena_size;
} mos_t_arena_offset;

// the header sits on top of the arena mmap region and occupies a single page of memory
typedef struct mos_t_arena_region_header {
    uint64_t identifier;
    uint64_t next_free_offset;                  // after this offset, it is guaranteed that no data is stored so far
    mos_t_arena_offset last_deleted_offset;     // a linked free list
} mos_t_arena_region_header;

static inline mos_t_arena_region_header* mos_arena_accessor_header(mos_t_mapped_region* mmap_arena_region) {
    return (mos_t_arena_region_header*)mmap_arena_region->region_base;
}

static inline uint8_t* mos_arena_accessor(mos_t_mapped_region* mmap_arena_region, mos_t_arena_offset* arena_offset) {
    if(mmap_arena_region->region_byte_size < arena_offset->arena_offset 
        || mmap_arena_region->region_byte_size < (arena_offset->arena_offset + arena_offset->arena_size)) {
        printf("[mos_arena]: Provided mmap region of size %zu is not big enough to access area of size %" PRId64 " at offset %" PRId64 "\n", mmap_arena_region->region_byte_size, arena_offset->arena_size, arena_offset->arena_offset);
        return NULL;
    }
    return ((uint8_t*)mmap_arena_region->region_base) + arena_offset->arena_offset;
}

static inline int mos_arena_init(mos_t_mapped_region* arena_region) {
    mos_t_arena_region_header* mmap_arena_region_header = mos_arena_accessor_header(arena_region);
    mmap_arena_region_header->identifier = MOS_FILE_ID;
    mmap_arena_region_header->last_deleted_offset.arena_offset = MOS_NULL_OFFSET;
    mmap_arena_region_header->last_deleted_offset.arena_size = MOS_NULL_OFFSET;
    mmap_arena_region_header->next_free_offset = MOS_ALIGN_UP(sizeof(mos_t_arena_region_header), MOS_PAGE_SIZE);
    return 0;
}

static inline int mos_arena_allocate(mos_t_mapped_region* mmap_arena_region, uint64_t min_arena_size, mos_t_arena_offset* offset_out) {
    mos_t_arena_region_header* arena_header = mos_arena_accessor_header(mmap_arena_region);
    mos_t_arena_offset* last_deleted_offset = &arena_header->last_deleted_offset;

    if (min_arena_size < MOS_MIN_ARENA_ALLOC) {
        min_arena_size = MOS_MIN_ARENA_ALLOC;
    }

    uint64_t offset = MOS_NULL_OFFSET;
    uint64_t final_arena_size = min_arena_size;
    if(last_deleted_offset->arena_offset != MOS_NULL_OFFSET && last_deleted_offset->arena_size >= min_arena_size) {
        offset = last_deleted_offset->arena_offset;
        final_arena_size = last_deleted_offset->arena_size;

        // use last deleted slot
        mos_t_arena_offset* freed_block_link = (mos_t_arena_offset*)mos_arena_accessor(mmap_arena_region, last_deleted_offset);
        
        if (freed_block_link->arena_offset != MOS_NULL_OFFSET) {
            arena_header->last_deleted_offset = *freed_block_link;
        } else {
            arena_header->last_deleted_offset.arena_offset = MOS_NULL_OFFSET;
            arena_header->last_deleted_offset.arena_size = 0;
        }
    }
    
    if(offset == MOS_NULL_OFFSET) {
        uint64_t needed_capacity = arena_header->next_free_offset + min_arena_size;

        if(mos_memory_region_ensure_capacity(mmap_arena_region, needed_capacity, MOS_PAGE_SIZE) != 0) {
            printf("[mos_arena]: Failed to ensure arena capacity of %" PRIu64 " bytes aligned to %d\n", needed_capacity, MOS_PAGE_SIZE);
            return -1;
        }

        offset = arena_header->next_free_offset;
        arena_header->next_free_offset += min_arena_size;
        final_arena_size = min_arena_size;
    }

    offset_out->arena_offset = offset;
    offset_out->arena_size = final_arena_size;

    return 0;
}

static inline int mos_arena_free(mos_t_mapped_region* mmap_arena_region, mos_t_arena_offset* arena_offset) {
    assert(arena_offset->arena_size >= sizeof(mos_t_arena_offset));

    mos_t_arena_region_header* arena_header = mos_arena_accessor_header(mmap_arena_region);
    mos_t_arena_offset* last_deleted_offset = &arena_header->last_deleted_offset;

    uint8_t* to_delete_block = mos_arena_accessor(mmap_arena_region, arena_offset);

    if(to_delete_block == NULL) {
        return -1;
    }
    memset(to_delete_block, 0, arena_offset->arena_size);

    mos_t_arena_offset* block_link = (mos_t_arena_offset*)to_delete_block;

    if(last_deleted_offset->arena_offset == MOS_NULL_OFFSET) {
        block_link->arena_offset = MOS_NULL_OFFSET;
        block_link->arena_size = 0;
    } else {
        mos_t_arena_offset* last_deleted_offset_block = (mos_t_arena_offset*)mos_arena_accessor(mmap_arena_region, last_deleted_offset);
        *block_link = *last_deleted_offset_block;
    }

    last_deleted_offset->arena_offset = arena_offset->arena_offset;
    last_deleted_offset->arena_size = arena_offset->arena_size;

    return 0;
}

// no growth, caller guarantees the block is already big enough.
static inline int mos_arena_write(mos_t_mapped_region* arena, mos_t_arena_offset* offset,
                                  const void* value, uint64_t value_byte_len, uint64_t byte_offset) {
    if (offset->arena_size < byte_offset + value_byte_len) {
        return -1;
    }
    uint8_t* block = mos_arena_accessor(arena, offset);
    memcpy(block + byte_offset, value, value_byte_len);
    return 0;
}

// grows if needed, preserves existing bytes, then writes the new value at the end.
static inline int mos_arena_append(mos_t_mapped_region* arena, mos_t_arena_offset* offset,
                                   const void* value, uint64_t value_byte_len, uint64_t current_used_bytes) {
    uint64_t needed = current_used_bytes + value_byte_len;

    if (offset->arena_size < needed) {
        mos_t_arena_offset old = *offset;
        mos_t_arena_offset fresh;

        uint64_t new_capacity = old.arena_size > 0 ? old.arena_size * 2 : needed;
        if (new_capacity < needed) new_capacity = needed;

        if (mos_arena_allocate(arena, new_capacity, &fresh) != 0) {
            return -1;
        }

        // TODO: Try to append to old_arena, though difficult to figure out if area next to old arena is free and big enough.
        if (old.arena_offset != MOS_NULL_OFFSET) {
            uint8_t* old_block = mos_arena_accessor(arena, &old);
            uint8_t* new_block = mos_arena_accessor(arena, &fresh);
            memcpy(new_block, old_block, current_used_bytes);
            mos_arena_free(arena, &old);
        }

        *offset = fresh;
    }

    return mos_arena_write(arena, offset, value, value_byte_len, current_used_bytes);
}

static_assert((MOS_MIN_ARENA_ALLOC & (MOS_MIN_ARENA_ALLOC - 1)) == 0 && MOS_MIN_ARENA_ALLOC >= sizeof(mos_t_arena_offset), "MOS_MIN_ARENA_ALLOC must stay a power of two (for uint64_t alignment) AND >= sizeof(mos_t_arena_offset)");

#endif