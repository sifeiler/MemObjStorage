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

#endif