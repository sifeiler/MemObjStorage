#ifndef MOS_STRING_H
#define MOS_STRING_H

#include <stdint.h>

/*
The string silo can be used to store and retrieve strings from a memory area.
The silo grows bottom up towards base, starting at base pointer + size.
The silo will never write outside of [base;base+size)
The silo remembers offsets of deleted strings and reuses them for new strings smaller or equal the length of the deleted string.
Strings will be stored as a multiple of 16 bytes. This is neccessary to store a free list for deletion and not produce many small silo holes.

String offsets behaviour:
Offset when there are no strings        = 0  -> address base + size
Offset of a single string with length 5 = 16 -> address base + size - 16
So an offset already points to the beginning of the string.

The silo has the following structure:

base (inclusive)
|
################
################
################
################
################
################
################
################
################ ^ growth-direction
################ |
string2.........
string1.........OUTOFSILO
^               ^
|               |base+size (exclusive)
|       
|offset of string1 = 16

*/

typedef struct mos_t_string {
    char* str;
    uint32_t str_len;
} mos_t_string;

typedef struct mos_t_string_desc {
    uint64_t str_offset;               //the offset of the string start (so str_len included) from base + size 
    uint32_t str_len;
} mos_t_string_desc;

typedef struct mos_t_string_silo {
    uint64_t base_offset;              //the silo base offset from file top. Writing will start at base + size and grow towards base.
    uint64_t size;
    uint64_t current_offset;
    mos_t_string_desc last_deleted;    // 4-byte aligned
    uint8_t _pad[4];
} mos_t_string_silo;

#define MOS_MIN_STRING_ALLOC 16         // must stay a power of two AND >= sizeof(mos_t_string_desc)
#define DELETED_NODE_SIZE sizeof(mos_t_string_desc)

_Static_assert(MOS_MIN_STRING_ALLOC >= sizeof(mos_t_string_desc),
               "MOS_MIN_STRING_ALLOC must fit a free-list node");
_Static_assert((MOS_MIN_STRING_ALLOC & (MOS_MIN_STRING_ALLOC - 1)) == 0,
               "MOS_MIN_STRING_ALLOC must be a power of two");

/**
 * Reads a string at the given offset and provides a pointer to it.
 * 
 * @param silo_pointer  Pointer to the silo base
 * @param string_silo   The actual silo to get the string from.
 * @param sd            The offset and length of the string to get.
 * @param result        The pointer of the string in the silo.
 *                      result is an output parameter.
 */
void mos_string_get(void* silo_pointer, mos_t_string_silo* string_silo, mos_t_string_desc* sd, char** result);

/**
 * Writes a string to the silo and provides the offset the string was stored at.
 * 
 * @param silo_pointer  Pointer to the silo base
 * @param string_silo   The actual silo to put the string to.
 * @param string        The string and its length to be put to the silo.
 * @param result        Pointer to the parameter holding the offset where the string was stored at.
 *                      result is an output parameter.
 * 
 * @return   0 when put was successful
 *          -1 when put failed (on empty string of length 0 etc.)
 */
int mos_string_put(void* silo_pointer, mos_t_string_silo* string_silo, mos_t_string* string, mos_t_string_desc* result);

/**
 * Removes a string from the silo and updates string_silo->last_deleted.
 * 
 * @param silo_pointer  Pointer to the silo base
 * @param string_silo   The actual silo to remove the string from.
 * @param sd            The offset and length of the string to remove.
 * 
 * @return   0 when remove was successful
 *          -1 when remove failed
 */
int mos_string_remove(void* silo_pointer, mos_t_string_silo* string_silo, mos_t_string_desc* sd);

#endif