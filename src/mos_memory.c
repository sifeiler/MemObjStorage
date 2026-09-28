#include <stdio.h>

#include "../include/mos_memory.h"
#include "../include/mos_os.h"
#include "../include/mos_math.h"
#include "../include/mos_utils.h"

// open existing region or, if not exists, create new with min_capacity and aligned up to align_to
int mos_memory_region_open(char file_path[MOS_PATH_MAX], size_t min_capacity, size_t align_to, mos_t_mapped_region* region_out) {
    int fd = mos_os_open(file_path, 0);
    printf("[mos_memory]: Trying to open memory region of size %zu at %s.\n", min_capacity, file_path);
    
    // Never map zero bytes: at least one aligned unit.
    if (min_capacity == 0) {
        min_capacity = align_to;
    }
    
    size_t mmap_capacity = min_capacity;
    if(fd < 0) {
        printf("[mos_memory]: file_path %s\n", file_path);
        printf("[mos_memory]: Memory region does not exist. Creating it.\n");
        fd = mos_os_open(file_path, 1);

        if(fd < 0) {
            mos_utils_report_error("[mos_memory]: Cannot create mmapped region.\n");
            return -1;
        }
        mmap_capacity = MOS_ALIGN_UP(min_capacity, align_to);
        printf("[mos_memory]: Memory region created. Aligned memory from min_capacity %zu to %zu bytes.\n", min_capacity, mmap_capacity);

        if(ftruncate(fd, mmap_capacity) == -1) {
            close(fd);
            mos_utils_report_error("[mos_memory]: Cannot truncate storage file %s to size %zu.\n", file_path, mmap_capacity);
            return -1;
        }
    }
    void* mmap_ptr = (void*)mos_os_mmap(fd, mmap_capacity);
        
    if(!mmap_ptr) {
        close(fd);
        mos_utils_report_error("[mos_memory]: Cannot map %zu bytes of memory for file %s.\n", mmap_capacity, file_path);
        return -1;
    }
    
    printf("[mos_memory]: Mapped memory with aligned capacity of %zu for file %s.\n", mmap_capacity, file_path);
    printf("[mos_memory]: Allocating region struct.\n");

    region_out->fd = fd;
    region_out->region_base = mmap_ptr;
    region_out->region_byte_size = mmap_capacity;
    memcpy(region_out->region_file_path, file_path, sizeof(region_out->region_file_path));

    return 0;
}

// resize existing region to min_capacity, aligned up to align_to
int mos_memory_region_ensure_capacity(mos_t_mapped_region* region, size_t min_capacity, size_t align_to) {
    if(region->region_byte_size >= min_capacity) {
        return 0;
    }

    size_t mmap_capacity = region->region_byte_size;
    while (mmap_capacity < min_capacity) {
        mmap_capacity *= 2;
    }
    mmap_capacity = MOS_ALIGN_UP(mmap_capacity, align_to);

    if (ftruncate(region->fd, mmap_capacity) != 0) {
        mos_utils_report_error("[mos_memory]: Cannot resize mapped file.\n");
        return -1;
    }

    void* new_mmap_ptr = mos_os_remmap(region->region_base, region->region_byte_size, mmap_capacity, region->fd);
    if(new_mmap_ptr == NULL) {
        mos_utils_report_error("[mos_memory]: Cannot remap region from %zu to %zu bytes.\n", region->region_byte_size, mmap_capacity);
        return -1;
    }
    region->region_base = new_mmap_ptr;
    region->region_byte_size = mmap_capacity;
    return 0;
}

// unmap a region
int mos_memory_region_close(mos_t_mapped_region* region) {
    int munmap_result = 0;
    if(region->region_base) {
        munmap_result = mos_os_munmap(region->region_base, region->region_byte_size);
    }
    if (region->fd >= 0) {
        mos_os_close_fd(region->fd);
    }
    memset(region, 0, sizeof(*region));
    return munmap_result;
}