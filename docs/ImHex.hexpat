import std.io;
#include <std/mem.pat>

struct mos_t_stringdesc {
    u64 str_offset;
    u64 str_len;
};

struct mos_t_header {
    u64 identifier;        //0x1111CCAC
    u64 attribute_count;
    u64 index_count;
    u64 max_records;

    //sizes
    u64 header_size;
    u64 attributes_size;
    u64 indexes_size;
    u64 valid_bitmap_size;
    u64 ready_bitmap_size;
    u64 record_size;
    u64 record_data_size;
    u64 records_size;
    u64 index_data_size;
    u64 string_silo_size;
    u64 file_size;

    //offsets
    u64 offset_header;
    u64 offset_attributes;
    u64 offset_indexes;
    u64 offset_valid_bitmap;
    u64 offset_ready_bitmap;
    u64 offset_records;
    u64 offset_index_data;
    
    u64 offset_next_free_record;
    u64 offset_last_deleted_record;
    //growing from bottom towards index data
    u64 current_string_offset;
    //to reuse empty spaces within the silo
    mos_t_stringdesc last_deleted_string;
};

enum MOS_ATTRIBUTE_TYPE : u32 {
    U_64 = 0,
    TIMESTAMP = 1,
    STRING = 2
};

enum MOS_INDEX_TYPE : u32 {
    HASHMAP = 0,
    TRIE = 1
};

struct mos_t_attr_info {
    char name[32];
    u32 type;
    u64 record_offset;            // byte offset in user record
};

struct mos_t_idx {
    char name[32];
    u32 type;          //C enum entry typically translates to 32 bits
    u64 index_offset;
    u64 index_size;    // bytes occupied in the storage file 
};

struct mos_t_record {
    u8 flags;              // for valid, dirty bits etc.
    u64 timestamp;         // for eviction
    u8 data[28];             // the actual entry
    u8 align[3];
};

mos_t_header header @ 0x00;
u8 header_align_up[8] @ 0x78;
mos_t_attr_info attributes[3] @ 0x80;
u8 attributes_align_up[36] @ 0x11C;
mos_t_idx index[0] @ 0x140;
u64 valid_bit_map[2] @ 0x140;
u64 valid_bit_map_align_up[6] @ 0x150;
u64 ready_bit_map[2] @ 0x180;
u64 ready_bit_map_align_up[6] @ 0x190;
mos_t_record entries[100] @ 0x1C0;