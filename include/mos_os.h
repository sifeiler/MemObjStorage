/* =========================================================================
   All os specific parts for the storage library will be defined here.
   This file provides os independet functions for memory mapping.
   ========================================================================= */
#ifndef MOS_OS_H
#define MOS_OS_H

#ifdef _WIN32
    #include <windows.h>
    #include <io.h>
    #define ftruncate _chsize_s
#else
    #include <sys/mman.h>
    #include <unistd.h>
#endif


#ifdef _WIN32
    #include <intrin.h>
    
    static inline void* mos_os_mmap(int fd, size_t size) {
        HANDLE hFile = (HANDLE)_get_osfhandle(fd);
        HANDLE hMap = CreateFileMapping(hFile, NULL, PAGE_READWRITE, 0, (DWORD)size, NULL);
        if (!hMap) return NULL;
        void* ptr = MapViewOfFile(hMap, FILE_MAP_ALL_ACCESS, 0, 0, size);
        CloseHandle(hMap); 
        return ptr;
    }

    static inline void mos_os_munmap(void* addr, size_t size) {
        UnmapViewOfFile(addr);
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
#else

    #define fsi_popcount64(x) __builtin_popcountll(x)
    #define fsi_ctz64(x) __builtin_ctzll(x)

    static inline void* mos_os_mmap(int fd, size_t size) {
        void* ptr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if(ptr == MAP_FAILED) {
            return NULL;
        }
        return ptr;
    }

    static inline void mos_os_munmap(void* addr, size_t size) {
        munmap(addr, size);
    }

#endif

#endif