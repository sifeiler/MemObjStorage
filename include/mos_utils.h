#ifndef MOS_UTILS_H
#define MOS_UTILS_H

#include <stdint.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>

#define VALUE_NOT_FOUND -1

//Use NULL_OFFSET to indicate that something in the storage is not initialized
#define MOS_NULL_OFFSET (uint64_t)-1

//smallest multiple of align where addr fits in
//i.e. align = 64, addr = 100 -> (100 + 64 - 1) & ~0011 1111 = 1000 0000 = 128
#define MOS_ALIGN_UP(addr, align) (((addr) + (align) - 1) & ~((align) - 1))
#define MOS_ALIGN_DOWN(addr, align) ((addr) & ~((align) - 1))

//calculates pointer to location in storage file
#define MOS_GET_PTR(base_ptr, offset) ((void*)((uint8_t*)(base_ptr) + (offset)))  

static inline void mos_utils_report_error(const char* input, ...) {
    va_list args;
    va_start(args, input);

    printf("Error: %s, Message: ", strerror(errno));

    vprintf(input, args);
    printf("\n");

    va_end(args);
}

static inline void mos_utils_exit_program(char* exit_reason, uint64_t exit_code) {
    printf("Program exit: %s\n", exit_reason);
    
    //cleanup, free memory, etc...

    exit(exit_code);
}

/**
 * Evaluates the next power of 2 for the given value.
 * 
 * value = 3 (11)
 * return = 4 (100)
 * 
 * value = 6 (110)
 * return = 8 (1000)
 */
static inline uint64_t mos_utils_next_pow_of_2(uint64_t value) {
    //-1 to avoid jumping to next power when already at a power of 2
    int64_t p = value - 1;
    p |= p >> 1;
    p |= p >> 2;
    p |= p >> 4;
    p |= p >> 8;
    p |= p >> 16;
    p |= p >> 32;
    return p < 0 ? 1 : p + 1;
}

#endif