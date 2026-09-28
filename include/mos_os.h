/* =========================================================================
   All os specific parts for the storage library will be defined here.
   This file provides os independet functions for memory mapping.
   ========================================================================= */
#ifndef MOS_OS_H
#define MOS_OS_H

#ifdef _WIN32
    #include <windows.h>
    #include <io.h>
    #include <intrin.h>
    #include <errno.h>
    #include <fcntl.h>
    #include <sys/stat.h>

    #define ftruncate _chsize_s 

    #define MOS_PATH_MAX 260
    #define FSI_PATH_DELIMITER_STR "\\"
    
    static inline void* mos_os_mmap(int fd, size_t size) {
        HANDLE hFile = (HANDLE)_get_osfhandle(fd);
        HANDLE hMap = CreateFileMapping(hFile, NULL, PAGE_READWRITE, 0, (DWORD)size, NULL);
        if (!hMap) return NULL;
        void* ptr = MapViewOfFile(hMap, FILE_MAP_ALL_ACCESS, 0, 0, size);
        CloseHandle(hMap);
        return ptr;
    }

    static inline void* mos_os_remmap(void* base, size_t old_size, size_t new_size, int fd) {
        if (mos_os_munmap(base, old_size) != 0) {
            return NULL;
        }

        return mos_os_mmap(fd, new_size);
    }

    static inline int mos_os_munmap(void* addr, size_t size) {
        return UnmapViewOfFile(addr) ? 0 : -1;
    }

    static inline int mos_os_msync(void* addr, size_t size) {
        if (!FlushViewOfFile(addr, size)) {
            return -1;
        }
        return 0;
    }

    // mode: 0 = open existing only, 1 = create if missing
    static inline int mos_os_open(const char* path, int create_if_missing) {
        int fd = -1;
        int flags = _O_RDWR | _O_BINARY;
        if (create_if_missing) {
            flags |= _O_CREAT;
        }

        errno_t err = _sopen_s(&fd, path, flags, _SH_DENYNO, _S_IREAD | _S_IWRITE);
        if (err != 0) {
            return -1;
        }
        return fd;
    }

    static inline int mos_os_close_fd(int fd) {
        // fd here is a CRT fd from _open (matches _get_osfhandle usage in mos_os_mmap)
        return _close(fd);
    }

    #ifdef _MSC_VER
        #define fsi_popcount64(x) __popcnt64(x)

        static inline int fsi_ctz64(uint64_t x) {
            unsigned long index;
            _BitScanForward64(&index, x);
            return (int)index;
        }
    #else
        #define fsi_ctz64(x) __builtin_ctzll(x)
        #define fsi_popcount64(x) __builtin_popcountll(x)
    #endif

    static inline int mos_os_mem_alloc_aligned(void** ptr, size_t size, size_t align, int fd) {
        if ((align & (align - 1)) != 0 || align % sizeof(void*) != 0) {
            return EINVAL;
        }

        *ptr =_aligned_malloc(size, align);

        if(*ptr == NULL) {
            return -1;
        }
        return 0;
    }

    static inline void mos_os_mem_free_aligned(void* ptr) {
        _aligned_free(ptr);
    }

    static inline int mos_os_directory_create(const char* path) {
        if (_mkdir(path) != 0) {
            return -1;
        }
        return 0;
    }

    static inline int mos_os_directory_delete(const char* path) {
        if (_rmdir(path) != 0) {
            return -1;
        }
        return 0;
    }
#elif defined(__linux__)
    #include <limits.h>
    #include <sys/mman.h>
    #include <unistd.h>
    #include <fcntl.h>
    #include <sys/stat.h>
    #include "mos_utils.h"

    #ifndef PATH_MAX
        #define MOS_PATH_MAX 4096
    #else
        #define MOS_PATH_MAX PATH_MAX
    #endif

    #define FSI_PATH_DELIMITER_STR "/"

    #define fsi_popcount64(x) __builtin_popcountll(x)
    #define fsi_ctz64(x) __builtin_ctzll(x)

    static inline void* mos_os_mmap(int fd, size_t size) {
        void* ptr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if(ptr == MAP_FAILED) {
            return NULL;
        }
        return ptr;
    }

    static inline void* mos_os_remmap(void* base, size_t old_size, size_t new_size, int fd) {
        UNUSED(fd);
        void* new_base = mremap(base, old_size, new_size, MREMAP_MAYMOVE);
        if (new_base == MAP_FAILED) {
            return NULL;   // remap failed
        }
        return new_base;
    }

    static inline int mos_os_munmap(void* addr, size_t size) {
        return munmap(addr, size);
    }

    static inline int mos_os_msync(void* addr, size_t size) {
        return msync(addr, size, MS_SYNC);
    }

    static inline int mos_os_open(const char* path, int create_if_missing) {
        int flags = O_RDWR;
        if (create_if_missing) {
            flags |= O_CREAT;
        }
        int fd = open(path, flags, 0644);
        return fd;   // -1 on failure
    }

    static inline int mos_os_close_fd(int fd) {
        return close(fd);
    }

    static inline int mos_os_mem_alloc_aligned(void** ptr, size_t size, size_t align) {
        return posix_memalign(ptr, align, size);
    }

    static inline void mos_os_mem_free_aligned(void* ptr) {
        free(ptr);
    }

    static inline int mos_os_directory_create(const char* path) {
        if (mkdir(path, 0755) != 0) {
            return -1;
        }
        return 0;
    }

    static inline int mos_os_directory_delete(const char* path) {
        if (rmdir(path) != 0) {
            return -1;
        }
        return 0;
    }

#endif

static inline int mos_os_path_join(char* out, size_t out_size, const char* dir, const char* file) {
    return snprintf(out, out_size, "%s" FSI_PATH_DELIMITER_STR "%s", dir, file) < (int)out_size ? 0 : -1;
}

#endif