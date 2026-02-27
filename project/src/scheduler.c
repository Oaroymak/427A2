#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "scheduler.h"
#include "framestore.h"
#include "shell.h"       /* parseInput */

/* ================================================================
 * Globals
 * ================================================================ */

PCB    *ready_queue     = NULL;
int     scheduler_active = 0;
Policy  active_policy    = POLICY_FCFS;

/* ================================================================
 * Queue helpers
 * ================================================================ */

/*
 * Insert 'pcb' into the queue according to the chosen insertion rule:
 *
 *   FCFS / RR / RR30  →  always append at the tail.
 *
 *   SJF               →  sort by job_length (score == job_length here),
 *                        ties broken AFTER existing peers (FIFO).
 *
 *   AGING             →  sort by score.
 *                        is_reschedule == 0  → tie goes AFTER   (initial load)
 *                        is_reschedule == 1  → tie goes BEFORE  (time-slice done)
 *
 * The "insert before equal" rule for re-scheduling ensures that a
 * running process stays at the head of the queue when its score
 * equals the next process – matching the expected AGING output.
 */
void queue_add(PCB *pcb, Policy policy, int is_reschedule) {
    if (!pcb) return;
    pcb->next = NULL;

    /* For non-sorted policies, just append */
    if (policy == POLICY_FCFS || policy == POLICY_RR || policy == POLICY_RR30) {
        if (!ready_queue) {
            ready_queue = pcb;
            return;
        }
        PCB *tail = ready_queue;
        while (tail->next) tail = tail->next;
        tail->next = pcb;
        return;
    }

    /* Sorted insert (SJF or AGING).
     * The key to compare on:
     *   SJF   → job_length (fixed)
     *   AGING → score      (dynamic)
     */
    int key = (policy == POLICY_SJF) ? pcb->job_length : pcb->score;

    if (!ready_queue) {
        ready_queue = pcb;
        return;
    }

    /* Find insertion point.
     *
     * We want to insert pcb just before the first node whose key
     * is STRICTLY GREATER than pcb's key.
     *
     * Exception for AGING re-scheduling: insert before the first
     * node with key GREATER-THAN-OR-EQUAL-TO pcb's key (i.e. ties
     * go before existing peers, keeping the running process at the
     * front).
     */
    PCB *prev = NULL;
    PCB *cur  = ready_queue;
    while (cur) {
        int cur_key = (policy == POLICY_SJF) ? cur->job_length : cur->score;
        int stop;
        if (is_reschedule && policy == POLICY_AGING)
            stop = (cur_key >= key);   /* stop at first equal-or-greater */
        else
            stop = (cur_key > key);    /* stop at first strictly-greater   */

        if (stop) break;
        prev = cur;
        cur  = cur->next;
    }

    pcb->next = cur;
    if (!prev)
        ready_queue = pcb;
    else
        prev->next = pcb;
}

PCB *queue_pop(void) {
    if (!ready_queue) return NULL;
    PCB *p   = ready_queue;
    ready_queue = p->next;
    p->next  = NULL;
    return p;
}

void queue_age_all(void) {
    for (PCB *p = ready_queue; p; p = p->next) {
        if (p->score > 0) p->score--;
    }
}

/* ================================================================
 * Execute one instruction of a PCB.
 * Returns 1 if the process is now finished, 0 otherwise.
 * ================================================================ */
static int execute_one(PCB *pcb) {
    const char *line = fs_get(pcb->pc);
    if (!line) return 1;

    /* Copy so parseInput can mutate it */
    char buf[MAX_USER_INPUT];
    strncpy(buf, line, MAX_USER_INPUT - 1);
    buf[MAX_USER_INPUT - 1] = '\0';
    /* Append newline so parseInput terminates correctly */
    int len = (int)strlen(buf);
    if (len < MAX_USER_INPUT - 1) { buf[len] = '\n'; buf[len+1] = '\0'; }

    pcb->pc++;

    parseInput(buf);  /* may call quit() and exit */

    return (pcb->pc >= pcb->mem_end);
}

/* Free a PCB's frames and then the PCB itself */
static void cleanup_pcb(PCB *pcb) {
    fs_free(pcb->mem_start, pcb->job_length);
    pcb_free(pcb);
}

/* ================================================================
 * Scheduler loops
 * ================================================================ */

/*
 * Non-preemptive: run each process to completion before moving on.
 * Used for FCFS and SJF.
 */
static void run_nonpreemptive(void) {
    while (ready_queue) {
        PCB *p = queue_pop();
        while (p->pc < p->mem_end) {
            execute_one(p);
        }
        cleanup_pcb(p);
    }
}

/*
 * Round-Robin with a configurable time quantum.
 * Used for RR (quantum=2) and RR30 (quantum=30).
 */
static void run_rr(int quantum) {
    while (ready_queue) {
        PCB *p = queue_pop();
        int done = 0;
        for (int i = 0; i < quantum && !done; i++) {
            done = execute_one(p);
        }
        if (done) {
            cleanup_pcb(p);
        } else {
            /* Re-enqueue at tail (FIFO) */
            queue_add(p, active_policy, 0);
        }
    }
}

/*
 * AGING scheduler.
 * Time-slice = 1 instruction per turn.
 * After each instruction, every waiting process has its score
 * decremented by 1 (floor 0), and the running process is
 * re-inserted with "insert-before-equal" semantics so that it
 * stays at the head when scores are tied.
 */
static void run_aging(void) {
    while (ready_queue) {
        PCB *p = queue_pop();
        int done = execute_one(p);
        queue_age_all();
        if (done) {
            cleanup_pcb(p);
        } else {
            queue_add(p, POLICY_AGING, 1 /* is_reschedule */);
        }
    }
}

/* ================================================================
 * Public entry point
 * ================================================================ */
void run_scheduler(Policy policy) {
    active_policy    = policy;
    scheduler_active = 1;

    switch (policy) {
        case POLICY_FCFS:
            run_nonpreemptive();
            break;
        case POLICY_SJF:
            run_nonpreemptive();
            break;
        case POLICY_RR:
            run_rr(2);
            break;
        case POLICY_RR30:
            run_rr(30);
            break;
        case POLICY_AGING:
            run_aging();
            break;
    }

    scheduler_active = 0;
}
