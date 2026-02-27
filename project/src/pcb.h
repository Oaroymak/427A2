#ifndef PCB_H
#define PCB_H

/* Process Control Block */
typedef struct pcb {
    int pid;          /* unique process id                  */
    int mem_start;    /* first frame index in frame store   */
    int mem_end;      /* exclusive end (mem_start + length) */
    int pc;           /* program counter: next frame to run */
    int job_length;   /* total number of instructions       */
    int score;        /* job-length score used by AGING     */
    struct pcb *next; /* intrusive linked-list pointer      */
} PCB;

/* Allocate and initialise a new PCB.
 * mem_start / mem_end refer to the frame store. */
PCB *pcb_create(int mem_start, int mem_end);

/* Deallocate a PCB (does NOT free the frames). */
void pcb_free(PCB *pcb);

#endif
