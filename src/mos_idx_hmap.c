#include <stddef.h>

#include "../include/mos_idx_hmap.h"
#include "../include/mos_idx.h"
#include "../include/mos_internal.h"
#include "../include/mos_utils.h"
#include "../include/mos_types_fwd.h"

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

    uint8_t* tail = data + (n * 16);
    
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

/* Implementation of hash map index size. See hash_map_index.h for documentation. */
uint64_t mos_idx_hmap_size(uint64_t item_count) {
    uint64_t index_data_size = sizeof(mos_t_idx_hmap_header);
    uint64_t item_size = sizeof(*((mos_t_idx_hmap*)0)->data);
    uint64_t table_size = mos_utils_next_pow_of_2(2 * item_count);

    //Hash map should only be 50% full, so we double the table size.
    //We add the table size twice: once for the values, once for the verifiers, multiplied by the item size
    index_data_size += 2 * table_size * item_size;
    return MOS_ALIGN_UP(index_data_size, MOS_PAGE_SIZE);
}

/* Implementation of hash map index initialization. See hash_map_index.h for documentation. */
void mos_idx_hmap_init(uint64_t item_count, mos_t_idx* idx, mos_t_idx_data* idx_data) {
    mos_t_idx_hmap* idx_hash_map = (mos_t_idx_hmap*)idx_data->index_payload;
   
    //alignment to next power of 2 is important for fast modulo operations (AND)
    uint64_t table_size = mos_utils_next_pow_of_2(2 * item_count);
    idx->index_size = mos_idx_hmap_size(item_count);
    idx_hash_map->index_header.table_size = table_size;

    //index values come right after the header
    uint64_t index_values_offset = idx->offset_file + offsetof(mos_t_idx_data, index_payload) + sizeof(mos_t_idx_hmap_header);
    //values + verifiers
    uint64_t index_data_size = idx->index_size - sizeof(mos_t_idx_hmap_header);
    idx_hash_map->index_header.offset_values = index_values_offset;
    //index verifiers come right after the index values
    idx_hash_map->index_header.offset_verifiers = index_values_offset + (index_data_size / 2);
}

int64_t mos_idx_hmap_put(const mos_t_idx_data* idx_data, const uint8_t* key, const size_t key_byte_len, const uint64_t value) {
    mos_t_idx_hmap* index = (mos_t_idx_hmap*)idx_data->index_payload;
    mos_t_idx_hmap_header index_header = index->index_header;
    uint64_t table_size = index_header.table_size;
    uint64_t* index_values = index->data;
    uint64_t* index_verifiers = index->data + table_size;

    __uint128_t hash = mos_idx_murmur_hash_3_128(key, MOS_IDX_MURMUR3_SEED, key_byte_len);
    uint64_t index_verifier = (hash >> 64);
    uint64_t mask = table_size - 1;
    //faster modulo to avoid overflow
    uint64_t i = hash & mask;
    int64_t thombstone = -1;
    uint64_t probes = 0;

    while(index_verifiers[i] != MOS_IDX_EMPTY) {
        if(index_verifiers[i] == index_verifier) {
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

    index_values[i] = value;
    index_verifiers[i] = index_verifier;

    return value;
}

int64_t mos_idx_hmap_find_row_id(const mos_t_idx_data* idx_data, const uint8_t* key, const size_t key_byte_len) {
    mos_t_idx_hmap* index = (mos_t_idx_hmap*)idx_data->index_payload;
    mos_t_idx_hmap_header index_header = index->index_header;
    uint64_t table_size = index_header.table_size;
    uint64_t* index_values = index->data;
    uint64_t* index_verifiers = index->data + table_size;

    __uint128_t hash = mos_idx_murmur_hash_3_128(key, MOS_IDX_MURMUR3_SEED, key_byte_len);
    uint64_t mask = index_header.table_size - 1;
    //faster modulo to avoid overflow
    uint64_t i = hash & mask;
    uint64_t index_verifier = (hash >> 64);

    while(index_verifiers[i] != MOS_IDX_EMPTY) {
        if(index_verifiers[i] == index_verifier) {
            return i;
        }
        //apply linear probing to check neighbor
        i = (i + 1) & mask;

        if(i == (hash & mask)) {
            break;
        }
    }

    return VALUE_NOT_FOUND;
}

int64_t mos_idx_hmap_get(const mos_t_idx_data* idx_data, const uint8_t* key, const size_t key_byte_len) {
    mos_t_idx_hmap* index = (mos_t_idx_hmap*)idx_data->index_payload;
    uint64_t* index_values = index->data;

    int64_t i = mos_idx_hmap_find_row_id(idx_data, key, key_byte_len);

    if(i == VALUE_NOT_FOUND) {
        return VALUE_NOT_FOUND;
    }

    return index_values[i];
}

void mos_idx_hmap_remove(const mos_t_idx_data* idx_data, const uint8_t* key, const size_t key_byte_len) {
    mos_t_idx_hmap* index = (mos_t_idx_hmap*)idx_data->index_payload;
    mos_t_idx_hmap_header index_header = index->index_header;
    uint64_t table_size = index_header.table_size;
    uint64_t* index_values = index->data;
    uint64_t* index_verifiers = index->data + table_size;

    int64_t i = mos_idx_hmap_find_row_id(idx_data, key, key_byte_len);

    if(i == VALUE_NOT_FOUND) {
        printf("Key is not within hash_map. Nothing to remove.");
        return;
    }

    index_values[i] = MOS_IDX_EMPTY;
    index_verifiers[i] = MOS_IDX_THOMBSTONE;
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

void mos_idx_hmap_bitmap_search(const mos_t_idx_data* idx_data, mos_t_qry_bmp* bm, const mos_t_qry_attr_qry* query) {
    uint8_t* key_ptr = mos_idx_hmap_value_bytes(&query->value);
    uint64_t len = mos_idx_hmap_value_length(&query->value);
    int64_t record_row_id = mos_idx_hmap_get(idx_data, key_ptr, len);
    if(record_row_id != -1) {
        uint64_t word_index = record_row_id >> 6; // divide by 64
        uint64_t bit_mask = 1ULL << (record_row_id & 63); // % 64
        bm->data[word_index] |= bit_mask;
        bm->empty = 0;
    }
}