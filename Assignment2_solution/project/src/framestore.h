#ifndef FRAMESTORE_H
#define FRAMESTORE_H

#define FRAME_STORE_SIZE 1000
#define MAX_LINE_LEN     100

/* Initialize the frame store */
void fs_init(void);

/* Allocate 'count' consecutive frames starting at returned index.
 * Returns -1 if not enough space. */
int fs_alloc(int count);

/* Get the line stored at frame index 'idx'. */
const char *fs_get(int idx);

/* Store a line at frame index 'idx'. */
void fs_set(int idx, const char *line);

/* Free 'count' frames starting at 'start'.
 * (Marks them as available so they can be reused.) */
void fs_free(int start, int count);

/* Return the number of free frames remaining. */
int fs_available(void);

#endif
