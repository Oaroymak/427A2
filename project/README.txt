Assignment 2 - Multi-Process Scheduling
========================================

Author: [Your Name] / [McGill ID]

This solution builds directly on the A1 reference code.

Design Notes
------------

New files added:
  framestore.c/h  – shared pool of 1000 program-line slots
  pcb.c/h         – Process Control Block struct and constructor
  scheduler.c/h   – ready queue + scheduling loop

Scheduling infrastructure (Section 1.2.1)
  source now loads the script into the frame store, creates a PCB,
  and runs it through the FCFS scheduler.  Behaviour is identical
  to A1 since there is only ever one process in the queue.

exec and scheduling policies (Sections 1.2.2–1.2.4)
  exec validates that all files exist, loads them into the frame
  store, creates one PCB per file, and calls run_scheduler().

  FCFS / SJF  – non-preemptive; each process runs to completion.
  RR(2)       – time-slice of 2 instructions, FIFO re-enqueue.
  RR30        – time-slice of 30 instructions, FIFO re-enqueue.
  AGING       – time-slice of 1; after each slice all waiting
                processes are aged (score--), then the running
                process is re-inserted using "insert-before-equal"
                semantics so it stays at the head on ties.
                Initial enqueue uses "insert-after-equal" to preserve
                the command-line order for same-length programs.

Background mode (Section 1.2.5)
  When # is present, the remaining stdin is read as prog0 (the
  "batch script process") and placed at the front of the ready
  queue so it runs first.  When exec is called from within a
  running scheduled process, the flag scheduler_active prevents
  starting a nested scheduler; new PCBs are simply added to the
  existing ready queue.

Uses starter code: Official A1 solution provided by the OS team.
