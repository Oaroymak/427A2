#include <stdlib.h>
#include "pcb.h"

static int next_pid = 1;

PCB *pcb_create(int mem_start, int mem_end) {
    PCB *p = (PCB *)malloc(sizeof(PCB));
    if (!p) return NULL;
    p->pid        = next_pid++;
    p->mem_start  = mem_start;
    p->mem_end    = mem_end;
    p->pc         = mem_start;
    p->job_length = mem_end - mem_start;
    p->score      = p->job_length;
    p->next       = NULL;
    return p;
}

void pcb_free(PCB *pcb) {
    free(pcb);
}
