// fuzz_string_silo.c
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include "../include/mos_string.h"
#include "../include/mos_utils.h"
#include "mos_utils_fuzzy.h"

#define SILO_SIZE (64 * 1024)
#define MAX_LIVE 256

typedef struct {
    mos_t_string_desc desc;
    char content[512];
    int len;
    int alive;
} shadow_entry;

static int step = 0;

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 2) return 0;
    int local_step = 0;

    void *silo_pointer = calloc(1, SILO_SIZE);
    mos_t_string_silo silo = {
        .base_offset = 0,
        .size = SILO_SIZE,
        .current_offset = 0,
        .last_deleted = { .str_offset = MOS_NULL_OFFSET, .str_len = 0 }
    };

    shadow_entry shadow[MAX_LIVE] = {0};
    int live_count = 0;

    size_t pos = 0;
    while (pos + 2 <= size) {
        uint8_t op = data[pos++] & 1;   // 0 = put, 1 = remove
        step++;
        local_step++;

        if (op == 0 && live_count < MAX_LIVE) {
            uint8_t len = data[pos++] % 200;   // bounded string length
            if (pos + len > size) break;

            mos_t_string s = { .str = (char*)&data[pos], .str_len = len };
            mos_t_string_desc result = {0};
            if(mos_string_put(silo_pointer, &silo, &s, &result) != 0) {
                continue;
            }

            shadow[live_count].desc = result;
            memcpy(shadow[live_count].content, &data[pos], len);
            shadow[live_count].len = len;
            shadow[live_count].alive = 1;
            live_count++;
            pos += len;
            printf("[Step G %d][Step L %d] PUT: len=%d, offset=%lu, capacity=%d\n", step, local_step, len, (unsigned long)result.str_offset, result.str_len);
        } else if (live_count > 0) {
            int idx = data[pos++] % live_count;
            if (shadow[idx].alive) {
                if(mos_string_remove(silo_pointer, &silo, &shadow[idx].desc) != 0) {
                    continue;
                }
                shadow[idx].alive = 0;
            }
            printf("[Step G %d][Step L %d] REMOVE: index=%d, offset=%lu\n", step, local_step, idx, (unsigned long)shadow[idx].desc.str_offset);
        }

        // invariant check: every live string still reads back correctly
        for (int i = 0; i < live_count; i++) {
            if (!shadow[i].alive) continue;
            char *result;
            mos_string_get(silo_pointer, &silo, &shadow[i].desc, &result);
            if (memcmp(result, shadow[i].content, shadow[i].len) != 0) {
                abort();   // libFuzzer will catch this and save the crashing input
            }
        }
    }

    free(silo_pointer);
    return 0;
}