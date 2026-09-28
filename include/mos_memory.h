#ifndef MOS_MEMORY_H
#define MOS_MEMORY_H
#include <stdlib.h>
#include "mos_os.h"

typedef struct mos_t_mapped_region {
    int     fd;
    size_t  region_byte_size;
    void*   region_base;
    char    region_file_path[MOS_PATH_MAX];
} mos_t_mapped_region;

// open existing region or, if not exists, create new with min_capacity and aligned up to align_to
int mos_memory_region_open(char file_path[MOS_PATH_MAX], size_t min_capacity, size_t align_to, mos_t_mapped_region* region_ptr_out);

// resize existing region to min_capacity, aligned up to align_to
int mos_memory_region_ensure_capacity(mos_t_mapped_region* region, size_t min_capacity, size_t align_to);

// unmap a region
int mos_memory_region_close(mos_t_mapped_region* region);

#endif