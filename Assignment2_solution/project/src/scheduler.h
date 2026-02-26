#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "pcb.h"

typedef enum {
    POLICY_FCFS  = 0,
    POLICY_SJF   = 1,
    POLICY_RR    = 2,
    POLICY_RR30  = 3,
    POLICY_AGING = 4
} Policy;

/* Global ready queue (head pointer).
 * Exposed so the interpreter can enqueue processes from within
 * the scheduler (e.g. exec called from a running batch-script). */
extern PCB *ready_queue;

/* Set to 1 while the scheduler loop is active.
 * The interpreter uses this flag to decide whether to start
 * a new scheduler or to just enqueue onto the existing one. */
extern int  scheduler_active;

/* The policy currently in effect (for in-scheduler exec calls). */
extern Policy active_policy;

/* -----------------------------------------------------------
 * Queue operations
 * ----------------------------------------------------------- */

/* Add a PCB to the ready queue.
 * - For FCFS / RR / RR30: always appends at the tail (FIFO).
 * - For SJF / AGING: uses insertion sort by score.
 *   'is_reschedule' == 1 means we are re-inserting the process
 *   that just ran its time-slice.  In that case ties are broken
 *   by inserting BEFORE other processes with the same score, so
 *   the running process stays at the head when scores are equal.
 *   When 'is_reschedule' == 0 (initial load) ties are broken by
 *   inserting AFTER other processes with the same score, so the
 *   order in which files were given on the command line is
 *   preserved. */
void queue_add(PCB *pcb, Policy policy, int is_reschedule);

/* Remove and return the head of the ready queue. */
PCB *queue_pop(void);

/* Age all processes currently in the ready queue:
 * decrease each score by 1 (floor at 0). */
void queue_age_all(void);

/* -----------------------------------------------------------
 * Scheduler entry point
 * ----------------------------------------------------------- */

/* Run all PCBs currently in ready_queue using 'policy'.
 * Returns when the queue is empty (or quit() is called). */
void run_scheduler(Policy policy);

#endif
