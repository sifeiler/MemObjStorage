#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifdef _WIN32
    #include <windows.h>
#else
    #include <unistd.h>
    #include <sys/stat.h>
#endif

// Picks the best available test root:
// 1. FSI_TEST_ROOT env var, if set (lets contributors point at a RAM disk manually)
// 2. /dev/shm on Linux, if it exists and is writable (tmpfs, zero SSD wear)
// 3. OS temp dir as fallback (works everywhere, no setup needed)
void mos_get_test_root(char *out, size_t out_size) {
    const char *override = getenv("FSI_TEST_ROOT");
    if (override != NULL) {
        snprintf(out, out_size, "%s", override);
        return;
    }

#ifndef _WIN32
    struct stat st;
    if (stat("/dev/shm", &st) == 0 && (st.st_mode & S_IFDIR)) {
        snprintf(out, out_size, "/dev/shm/fsi_test_%d", getpid());
        return;
    }
#endif

#ifdef _WIN32
    char temp_path[MAX_PATH];
    GetTempPathA(sizeof(temp_path), temp_path);
    snprintf(out, out_size, "%sfsi_test_%lu", temp_path, GetCurrentProcessId());
#else
    const char *tmpdir = getenv("TMPDIR");
    if (tmpdir == NULL) tmpdir = "/tmp";
    snprintf(out, out_size, "%s/fsi_test_%d", tmpdir, getpid());
#endif
}