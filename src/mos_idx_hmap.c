#include <stddef.h>
#include <stdint.h>

#include "../include/mos_idx_hmap.h"
#include "../include/mos_idx.h"
#include "../include/mos_internal.h"
#include "../include/mos_utils.h"
#include "../include/mos_types_fwd.h"
#include "../include/mos_math.h"

typedef struct mos_t_idx_hmap_idx_size {
    uint64_t header_size_page_padded;
    uint64_t table_size_padded;
    uint64_t item_size;
    uint64_t index_data_size_padded;
    uint64_t total_index_size_page_padded;
} mos_t_idx_hmap_idx_size;

typedef struct mos_t_idx_hmap_ptrs {
    mos_t_idx_hmap* hmap;
    mos_t_idx_value_node* index_values;
    uint64_t* index_verifiers;
} mos_t_idx_hmap_ptrs;

__uint128_t mos_idx_murmur_hash_3_128(const uint8_t* data, const uint64_t seed, const size_t key_byte_len);

//rotate val by r bits to the left. The bits that rotate out on the left, rotate back in on the right
static inline uint64_t mos_idx_rotl64(uint64_t val, int8_t r) {
    return (val << r) | (val >> (64 - r));
}

uint64_t mos_idx_murmur_hash_3_64(const uint8_t* data, const uint64_t seed, const size_t key_byte_len) {
    return (uint64_t)mos_idx_murmur_hash_3_128(data, seed, key_byte_len);
}

__uint128_t mos_idx_murmur_hash_3_128(const uint8_t* data, const uint64_t seed, const size_t key_byte_len) {
    const size_t n = key_byte_len / 16;
    const uint8_t remainder = key_byte_len & 15;

    uint64_t hash1 = seed;
    uint64_t hash2 = seed;

    //two 64 bit constants to ensure high entropy for avoidance of clustering
    const uint64_t c1 = 0x87c37b91114253d5ULL;
    const uint64_t c2 = 0x4cf5ad432745937fULL;

    const uint64_t* blocks = (const uint64_t*)data;
    for(size_t i = 0; i < n; i++) {
        uint64_t k1 = blocks[i * 2];
        uint64_t k2 = blocks[i * 2 + 1];

        k1 *= c1; k1 = mos_idx_rotl64(k1, 31); k1 *= c2; hash1 ^= k1;
        hash1 = mos_idx_rotl64(hash1, 27); hash1 += hash2; hash1 = hash1 * 5 + 0x52dce729;

        k2 *= c2; k2 = mos_idx_rotl64(k2, 33); k2 *= c1; hash2 ^= k2;
        hash1 = mos_idx_rotl64(hash2, 31); hash2 += hash1; hash2 = hash2 * 5 + 0x38495ab5;
    }

    const uint8_t* tail = data + (n * 16);
    
    uint64_t k1 = 0;
    uint64_t k2 = 0;

    switch (remainder) {
        case 15: k2 ^= (uint64_t)(tail[14]) << 48;
        case 14: k2 ^= (uint64_t)(tail[13]) << 40;
        case 13: k2 ^= (uint64_t)(tail[12]) << 32;
        case 12: k2 ^= (uint64_t)(tail[11]) << 24;
        case 11: k2 ^= (uint64_t)(tail[10]) << 16;
        case 10: k2 ^= (uint64_t)(tail[9]) << 8;
        case  9: k2 ^= (uint64_t)(tail[8]) << 0;
        
        k2 *= c2; k2 = mos_idx_rotl64(k2, 33); k2 *= c1; hash2 ^= k2;

        case  8: k1 ^= (uint64_t)(tail[7]) << 56;
        case  7: k1 ^= (uint64_t)(tail[6]) << 48;
        case  6: k1 ^= (uint64_t)(tail[5]) << 40;
        case  5: k1 ^= (uint64_t)(tail[4]) << 32;
        case  4: k1 ^= (uint64_t)(tail[3]) << 24;
        case  3: k1 ^= (uint64_t)(tail[2]) << 16;
        case  2: k1 ^= (uint64_t)(tail[1]) << 8;
        case  1: k1 ^= (uint64_t)(tail[0]) << 0;
        
        k1 *= c1; k1 = mos_idx_rotl64(k1, 31); k1 *= c2; hash1 ^= k1;
    };

    hash1 ^= key_byte_len; hash2 ^= key_byte_len;
    hash1 += hash2; hash2 += hash1;

    hash1 ^= hash1 >> 33; hash1 *= 0xff51afd7ed558ccdULL;
    hash1 ^= hash1 >> 33; hash1 *= 0xc4ceb9fe1a85ec53ULL;
    hash1 ^= hash1 >> 33;

    hash2 ^= hash2 >> 33; hash2 *= 0xff51afd7ed558ccdULL;
    hash2 ^= hash2 >> 33; hash2 *= 0xc4ceb9fe1a85ec53ULL;
    hash2 ^= hash2 >> 33;

    hash1 += hash2; hash2 += hash1;

    return ((__uint128_t)hash2 << 64) | hash1;
}

mos_t_idx_hmap_idx_size mos_idx_hnsw_get_index_size(const uint64_t item_count, mos_t_idx_descriptor* index_desc) {
    UNUSED(index_desc);
    mos_t_idx_hmap_idx_size index_sizes;
    index_sizes.header_size_page_padded = MOS_ALIGN_UP(sizeof(mos_t_idx_hmap_header), MOS_PAGE_SIZE);
    index_sizes.item_size = sizeof(mos_t_idx_value_node);

    //alignment to next power of 2 is important for fast modulo operations (AND)
    index_sizes.table_size_padded = mos_utils_next_pow_of_2(2 * item_count);

    uint64_t values_size = sizeof(mos_t_idx_value_node) * index_sizes.table_size_padded;
    uint64_t verifiers_size = sizeof(uint64_t) * index_sizes.table_size_padded;

    //Hash map should only be 50% full, so we double the table size.
    //We add the table size twice: once for the values, once for the verifiers, multiplied by the item size
    index_sizes.index_data_size_padded = MOS_ALIGN_UP(values_size + verifiers_size, MOS_PAGE_SIZE);
    
    index_sizes.total_index_size_page_padded = index_sizes.header_size_page_padded + index_sizes.index_data_size_padded;
    return index_sizes;
}

static inline mos_t_idx_hmap_ptrs mos_idx_hmap_get_data_ptrs(mos_t_idx_data* idx_data) {
    mos_t_idx_hmap* hmap_index = (mos_t_idx_hmap*)(((uint8_t*)idx_data) + idx_data->header.index_payload_offset);
    mos_t_idx_hmap_ptrs ptrs;
    ptrs.hmap = hmap_index;
    ptrs.index_values = (mos_t_idx_value_node*)(((uint8_t*)hmap_index) + hmap_index->index_header.offset_values);
    ptrs.index_verifiers = (uint64_t*)(((uint8_t*)hmap_index) + hmap_index->index_header.offset_verifiers);
    return ptrs;
}

/* Implementation of hash map index size. See mos_idx_hmap.h for documentation. */
uint64_t mos_idx_hmap_size(uint64_t item_count, mos_t_idx_descriptor* index_desc) {
    mos_t_idx_hmap_idx_size index_size = mos_idx_hnsw_get_index_size(item_count, index_desc);
    return index_size.total_index_size_page_padded;
}

/* Implementation of hash map index initialization. See mos_idx_hmap.h for documentation. */
void mos_idx_hmap_init(uint64_t item_count, mos_t_idx_descriptor* index_desc, mos_t_idx_data* idx_data) {
    mos_t_idx_hmap_ptrs hmap_ptrs = mos_idx_hmap_get_data_ptrs(idx_data);
    mos_t_idx_hmap* idx_hash_map = hmap_ptrs.hmap;
    mos_t_idx_hmap_idx_size index_size = mos_idx_hnsw_get_index_size(item_count, index_desc);
   
    idx_hash_map->index_header.table_size = index_size.table_size_padded;

    //index values come right after the header
    uint64_t index_values_offset = index_size.header_size_page_padded;
    idx_hash_map->index_header.offset_values = index_values_offset;
    //index verifiers come right after the index values
    idx_hash_map->index_header.offset_verifiers = index_values_offset + (index_size.table_size_padded * sizeof(mos_t_idx_value_node));
}

int mos_idx_hmap_put(const mos_t_idx_context* idx_context, const uint8_t* key, const size_t key_byte_len, const uint64_t value, mos_t_idx_put_result* result) {
    UNUSED(result);
    mos_t_idx_hmap_ptrs hmap_ptrs = mos_idx_hmap_get_data_ptrs(idx_context->idx_data);
    mos_t_idx_hmap* index = hmap_ptrs.hmap;
    mos_t_idx_hmap_header index_header = index->index_header;
    uint64_t table_size = index_header.table_size;
    mos_t_idx_value_node* index_values = hmap_ptrs.index_values;
    uint64_t* index_verifiers = hmap_ptrs.index_verifiers;

    __uint128_t hash = mos_idx_murmur_hash_3_128(key, MOS_IDX_MURMUR3_SEED, key_byte_len);
    uint64_t index_verifier = (hash >> 64);
    uint64_t mask = table_size - 1;
    //faster modulo to avoid overflow
    uint64_t i = hash & mask;
    int64_t thombstone = -1;
    uint64_t probes = 0;
    int key_existed = 0;

    while(index_verifiers[i] != MOS_IDX_EMPTY) {
        if(index_verifiers[i] == index_verifier) {
            key_existed = 1;
            break; //found key, i is index
        }

        if(index_verifiers[i] == MOS_IDX_THOMBSTONE && thombstone == -1) {
            thombstone = i;
        }

        //linear probing
        i = (i + 1) & mask;
        probes++;

        //check if hash map is full
        if(probes >= table_size) {
            if(thombstone == -1) {
                return -1;
            }
            i = thombstone;
            break;
        }
    }

    //prefer put at thombstone (priorize deleted item) over empty value
    if (index_verifiers[i] == MOS_IDX_EMPTY && thombstone != -1) {
        i = thombstone;
    }

    mos_t_idx_value_node* index_value = &index_values[i];

    if (!key_existed) {
        // fresh slot -> initialize
        index_value->values_count = 0;
        index_value->capacity = MOS_IDX_VALUES_INLINED;
        memset(&index_value->values, 0, sizeof(index_value->values));
    }

    if(index_value->values_count < MOS_IDX_VALUES_INLINED && index_value->capacity <= MOS_IDX_VALUES_INLINED) {
        index_value->values.values_inlined[index_value->values_count] = value;
    } else {
        if(index_value->capacity <= MOS_IDX_VALUES_INLINED) {
            mos_t_arena_offset arena_offset = {0};
            uint64_t arena_capacity = MOS_IDX_VALUES_INLINED * 4 * sizeof(uint64_t);
            if(mos_arena_allocate(idx_context->kind.hmap.arena_region, arena_capacity, &arena_offset) != 0) {
                printf("Hmap put failed. Cannot allocate arena.\n");
                return -1;
            }

            uint8_t* arena_block = mos_arena_accessor(idx_context->kind.hmap.arena_region, &arena_offset);

            // copy over from inlined to arena
            memcpy(arena_block, index_value->values.values_inlined, MOS_IDX_VALUES_INLINED * sizeof(uint64_t));
            index_value->capacity = arena_capacity / sizeof(uint64_t);
            index_value->values.arena_offset = arena_offset;
        }

        uint64_t bytes_to_skip = sizeof(uint64_t) * index_value->values_count;
        if (mos_arena_append(idx_context->kind.hmap.arena_region, &index_value->values.arena_offset,
                            &value, sizeof(uint64_t), bytes_to_skip) != 0) {
            printf("Arena write failed. Cannot put value to hashmap.\n");
            return -1;
        }

        // sync capacity as arena might have been resized during append
        index_value->capacity = index_value->values.arena_offset.arena_size / sizeof(uint64_t);
    }
    index_value->values_count++;
    index_verifiers[i] = index_verifier;

    return 0;
}

int mos_idx_hmap_find_table_position(const mos_t_idx_context* idx_context, const uint8_t* key, const size_t key_byte_len, uint64_t* table_pos_out) {
    mos_t_idx_hmap_ptrs hmap_ptrs = mos_idx_hmap_get_data_ptrs(idx_context->idx_data);
    mos_t_idx_hmap* index = hmap_ptrs.hmap;
    mos_t_idx_hmap_header index_header = index->index_header;
    uint64_t* index_verifiers = hmap_ptrs.index_verifiers;

    __uint128_t hash = mos_idx_murmur_hash_3_128(key, MOS_IDX_MURMUR3_SEED, key_byte_len);
    uint64_t mask = index_header.table_size - 1;
    //faster modulo to avoid overflow
    uint64_t i = hash & mask;
    uint64_t index_verifier = (hash >> 64);

    while(index_verifiers[i] != MOS_IDX_EMPTY) {
        if(index_verifiers[i] == index_verifier) {
            *table_pos_out = i;
            return 0;
        }
        //apply linear probing to check neighbor
        i = (i + 1) & mask;

        if(i == (hash & mask)) {
            break;
        }
    }

    return VALUE_NOT_FOUND;
}

int mos_idx_hmap_get(const mos_t_idx_context* idx_context, const uint8_t* key, const size_t key_byte_len, mos_t_id_list* result_list_out) {
    mos_t_idx_hmap_ptrs hmap_ptrs = mos_idx_hmap_get_data_ptrs(idx_context->idx_data);
    mos_t_idx_value_node* index_values = hmap_ptrs.index_values;

    uint64_t table_pos = UINT64_MAX;
    if(mos_idx_hmap_find_table_position(idx_context, key, key_byte_len, &table_pos) != 0) {
        printf("Key is not within hashmap.\n");
        result_list_out->count = 0;
        result_list_out->ids = NULL;
        return 0;
    }

    mos_t_idx_value_node* index_value = &index_values[table_pos];

    if(index_value->capacity <= MOS_IDX_VALUES_INLINED) {
        result_list_out->ids = index_value->values.values_inlined;
    } else {
        result_list_out->ids = (uint64_t*)mos_arena_accessor(idx_context->kind.hmap.arena_region, &index_value->values.arena_offset);
    }
    result_list_out->count = index_value->values_count;
    return 0;
}

int mos_idx_hmap_remove_key(const mos_t_idx_context* idx_context, const uint8_t* key, const size_t key_byte_len) {
    mos_t_idx_hmap_ptrs hmap_ptrs = mos_idx_hmap_get_data_ptrs(idx_context->idx_data);
    mos_t_idx_value_node* index_values = hmap_ptrs.index_values;
    uint64_t* index_verifiers = hmap_ptrs.index_verifiers;

    uint64_t table_pos = 0;
    if(mos_idx_hmap_find_table_position(idx_context, key, key_byte_len, &table_pos) != 0) {
        printf("Key is not within hashmap. Nothing to remove.\n");
        return VALUE_NOT_FOUND;
    }

    mos_t_idx_value_node* index_value = &index_values[table_pos];

    if(index_value->values_count <= MOS_IDX_VALUES_INLINED) {
        memset(index_value, 0, sizeof(*index_value));
    } else {
        if (mos_arena_free(idx_context->kind.hmap.arena_region, &index_value->values.arena_offset) != 0) {
            printf("Arena free failed. Cannot remove value from hashmap.\n");
            return -1;
        }
    }

    index_verifiers[table_pos] = MOS_IDX_THOMBSTONE;
    return 0;
}

int mos_idx_hmap_remove_value(const mos_t_idx_context* idx_context, const uint8_t* key, const size_t key_len, uint64_t value) {
    UNUSED(idx_context);
    UNUSED(key);
    UNUSED(key_len);
    UNUSED(value);
    return 0;
}

static inline uint8_t* mos_idx_hmap_value_bytes(const mos_t_attr_value* v) {
    switch (v->type) {
        case MOS_ATTR_TYPE_UINT64:
            return (uint8_t*)&v->int_val;
        case MOS_ATTR_TYPE_STRING:
            return (uint8_t*)v->char_val;
        default:
            return NULL;
    }
}

static inline uint64_t mos_idx_hmap_value_length(const mos_t_attr_value* v) {
    switch (v->type) {
        case MOS_ATTR_TYPE_UINT64:
            return sizeof(uint64_t);
        case MOS_ATTR_TYPE_STRING:
            return v->byte_length;
        default:
            return 0;
    }
}

void mos_idx_hmap_bitmap_search(const mos_t_idx_context* idx_context, mos_t_qry_bmp* bm, const mos_t_qry_attr_qry* query) {
    uint8_t* key_ptr = mos_idx_hmap_value_bytes(&query->value);
    uint64_t len = mos_idx_hmap_value_length(&query->value);
    mos_t_id_list result_list = {0};
    if(mos_idx_hmap_get(idx_context, key_ptr, len, &result_list) == 0) {
        for(uint64_t i = 0; i < result_list.count; i++) {
            uint64_t record_row_id = result_list.ids[i];
            uint64_t word_index = record_row_id >> 6; // divide by 64
            uint64_t bit_mask = 1ULL << (record_row_id & 63); // % 64
            bm->data[word_index] |= bit_mask;
            bm->empty = 0;
        }
    }
}