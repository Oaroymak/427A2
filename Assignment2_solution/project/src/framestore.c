#include <string.h>
#include <stdio.h>
#include "framestore.h"

/* Each slot: the line text plus a flag indicating if it's in use */
static char  frames[FRAME_STORE_SIZE][MAX_LINE_LEN];
static int   used[FRAME_STORE_SIZE];   /* 1 = occupied, 0 = free */

void fs_init(void) {
    memset(used, 0, sizeof(used));
    memset(frames, 0, sizeof(frames));
}

int fs_alloc(int count) {
    /* Find a run of 'count' consecutive free frames */
    int start = 0;
    while (start + count <= FRAME_STORE_SIZE) {
        int ok = 1;
        for (int i = start; i < start + count; i++) {
            if (used[i]) { ok = 0; start = i + 1; break; }
        }
        if (ok) {
            for (int i = start; i < start + count; i++) used[i] = 1;
            return start;
        }
    }
    return -1;   /* not enough space */
}

const char *fs_get(int idx) {
    if (idx < 0 || idx >= FRAME_STORE_SIZE) return NULL;
    return frames[idx];
}

void fs_set(int idx, const char *line) {
    if (idx < 0 || idx >= FRAME_STORE_SIZE) return;
    strncpy(frames[idx], line, MAX_LINE_LEN - 1);
    frames[idx][MAX_LINE_LEN - 1] = '\0';
    /* Strip trailing newline */
    int len = (int)strlen(frames[idx]);
    while (len > 0 && (frames[idx][len-1] == '\n' || frames[idx][len-1] == '\r'))
        frames[idx][--len] = '\0';
}

void fs_free(int start, int count) {
    for (int i = start; i < start + count; i++) {
        if (i >= 0 && i < FRAME_STORE_SIZE) {
            used[i] = 0;
            frames[i][0] = '\0';
        }
    }
}

int fs_available(void) {
    int n = 0;
    for (int i = 0; i < FRAME_STORE_SIZE; i++)
        if (!used[i]) n++;
    return n;
}
