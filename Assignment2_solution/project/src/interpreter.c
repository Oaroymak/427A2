#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "interpreter.h"
#include "shell.h"
#include "shellmemory.h"
#include "framestore.h"
#include "pcb.h"
#include "scheduler.h"

/* ================================================================
 * Error helpers
 * ================================================================ */
static int badcommand(void) {
    printf("Unknown Command\n");
    return 1;
}
static int badcommand_filenotfound(void) {
    printf("Bad command: File not found\n");
    return 3;
}
static int badcommand_exec(const char *msg) {
    printf("Bad command: %s\n", msg);
    return 2;
}

/* ================================================================
 * Built-in commands (unchanged from A1)
 * ================================================================ */

static int do_help(void) {
    printf("COMMAND\t\t\tDESCRIPTION\n"
           " help\t\t\tDisplays all the commands\n"
           " quit\t\t\tExits / terminates the shell with \"Bye!\"\n"
           " set VAR STRING\t\tAssigns a value to shell memory\n"
           " print VAR\t\tDisplays the STRING assigned to VAR\n"
           " echo STRING\t\tPrints STRING\n"
           " source SCRIPT\t\tExecutes the file SCRIPT\n"
           " exec P1 [P2 P3] POLICY [#]\tRun programs concurrently\n");
    return 0;
}

static int do_quit(void) {
    printf("Bye!\n");
    exit(0);
}

static int do_set(char *var, char *value) {
    mem_set_value(var, value);
    return 0;
}

static int do_print(char *var) {
    char *v = mem_get_value(var);
    if (v) {
        printf("%s\n", v);
        free(v);
    } else {
        printf("Variable does not exist\n");
    }
    return 0;
}

static int do_echo(char *tok) {
    int must_free = 0;
    if (tok[0] == '$') {
        tok++;
        tok = mem_get_value(tok);
        if (!tok) tok = "";
        else must_free = 1;
    }
    printf("%s\n", tok);
    if (must_free) free(tok);
    return 0;
}

/* ================================================================
 * Load a script file into the frame store.
 * Returns the starting frame index, or -1 on error.
 * *out_count is set to the number of lines loaded.
 * ================================================================ */
static int load_script(const char *filename, int *out_count) {
    FILE *f = fopen(filename, "r");
    if (!f) return -1;

    /* First pass: count lines */
    int count = 0;
    char line[MAX_USER_INPUT];
    while (fgets(line, sizeof(line), f)) {
        /* Strip newline to check if the line has content */
        int len = (int)strlen(line);
        while (len > 0 && (line[len-1]=='\n' || line[len-1]=='\r')) len--;
        line[len] = '\0';
        count++;
    }
    /* Handle files that don't end with a newline: fgets still returns the
       last partial line, so count is already correct. */

    rewind(f);

    if (count == 0) {
        fclose(f);
        *out_count = 0;
        /* Allocate 0 frames — just return 0 as a sentinel start */
        return 0;
    }

    int start = fs_alloc(count);
    if (start < 0) {
        fclose(f);
        fprintf(stderr, "Frame store full.\n");
        return -1;
    }

    int idx = start;
    while (fgets(line, sizeof(line), f)) {
        fs_set(idx++, line);
    }

    fclose(f);
    *out_count = count;
    return start;
}

/* ================================================================
 * source command
 *
 * Loads the script into the frame store, creates a PCB, and runs
 * it through the FCFS scheduler (single-process, so policy doesn't
 * really matter).
 * ================================================================ */
static int do_source(char *script) {
    int count;
    int start = load_script(script, &count);
    if (start < 0) return badcommand_filenotfound();

    if (count == 0) return 0;   /* empty script */

    PCB *pcb = pcb_create(start, start + count);
    if (!pcb) {
        fs_free(start, count);
        fprintf(stderr, "Could not create PCB\n");
        return 1;
    }

    queue_add(pcb, POLICY_FCFS, 0);
    run_scheduler(POLICY_FCFS);
    return 0;
}

/* ================================================================
 * exec command
 *
 * Syntax: exec prog1 [prog2 [prog3]] POLICY [#]
 *
 * Loads up to 3 programs, creates PCBs, and runs them concurrently.
 * If '#' is present, the remaining stdin is treated as a batch-script
 * process (prog0) and is added at the front of the queue.
 * ================================================================ */

static Policy parse_policy(const char *s) {
    if (strcmp(s, "FCFS")  == 0) return POLICY_FCFS;
    if (strcmp(s, "SJF")   == 0) return POLICY_SJF;
    if (strcmp(s, "RR")    == 0) return POLICY_RR;
    if (strcmp(s, "RR30")  == 0) return POLICY_RR30;
    if (strcmp(s, "AGING") == 0) return POLICY_AGING;
    return (Policy)-1;
}

/*
 * Read remaining stdin into the frame store as a "batch script"
 * process (prog0).  Used for background (#) mode.
 * Returns the PCB for prog0, or NULL if stdin is empty.
 */
static PCB *load_remaining_stdin(void) {
    /* We read one line at a time and store them in a temporary list
     * before allocating frames, because we don't know the count yet. */
    char  lines[FRAME_STORE_SIZE][MAX_LINE_LEN];
    int   count = 0;
    char  buf[MAX_USER_INPUT];

    while (fgets(buf, sizeof(buf), stdin) && count < FRAME_STORE_SIZE) {
        /* Strip newline */
        int len = (int)strlen(buf);
        while (len > 0 && (buf[len-1]=='\n' || buf[len-1]=='\r')) len--;
        buf[len] = '\0';
        if (len == 0 && feof(stdin)) break;
        strncpy(lines[count], buf, MAX_LINE_LEN - 1);
        lines[count][MAX_LINE_LEN - 1] = '\0';
        count++;
    }

    if (count == 0) return NULL;

    int start = fs_alloc(count);
    if (start < 0) {
        fprintf(stderr, "Frame store full for batch script\n");
        return NULL;
    }

    for (int i = 0; i < count; i++) {
        fs_set(start + i, lines[i]);
    }

    return pcb_create(start, start + count);
}

static int do_exec(char **args, int nargs) {
    /*
     * Parse: exec prog1 [prog2 [prog3]] POLICY [#]
     *
     * args[0..nargs-1] are the arguments (not including "exec" itself).
     * Possible nargs values: 2, 3, 4, 5 (prog(s) + policy [+ #]).
     */

    int background = 0;
    int prog_count = 0;          /* number of program file names */
    const char *filenames[3] = {NULL, NULL, NULL};
    Policy policy;

    /* Check for trailing '#' */
    if (nargs >= 2 && strcmp(args[nargs-1], "#") == 0) {
        background = 1;
        nargs--;                 /* pretend '#' isn't there for parsing */
    }

    /* Now nargs should be 2..4: prog(s) + policy */
    if (nargs < 2 || nargs > 4)
        return badcommand_exec("wrong number of arguments for exec");

    /* Last remaining arg is the policy */
    policy = parse_policy(args[nargs - 1]);
    if ((int)policy < 0)
        return badcommand_exec("invalid scheduling policy");

    prog_count = nargs - 1;
    for (int i = 0; i < prog_count; i++)
        filenames[i] = args[i];

    /* Check for duplicate filenames */
    for (int i = 0; i < prog_count; i++)
        for (int j = i+1; j < prog_count; j++)
            if (strcmp(filenames[i], filenames[j]) == 0)
                return badcommand_exec("duplicate program names");

    /* Validate all files exist before loading any */
    for (int i = 0; i < prog_count; i++) {
        FILE *tmp = fopen(filenames[i], "r");
        if (!tmp) {
            printf("Bad command: File %s not found\n", filenames[i]);
            return 3;
        }
        fclose(tmp);
    }

    /* Load each program into the frame store */
    int starts[3], counts[3];
    int loaded = 0;
    for (int i = 0; i < prog_count; i++) {
        starts[i] = load_script(filenames[i], &counts[i]);
        if (starts[i] < 0) {
            /* Undo already-loaded programs */
            for (int j = 0; j < loaded; j++)
                fs_free(starts[j], counts[j]);
            printf("Bad command: could not load %s\n", filenames[i]);
            return 3;
        }
        loaded++;
    }

    /* Create PCBs and add them to the queue */
    for (int i = 0; i < prog_count; i++) {
        PCB *pcb = pcb_create(starts[i], starts[i] + counts[i]);
        if (!pcb) {
            fprintf(stderr, "Out of memory creating PCB\n");
            return 1;
        }
        queue_add(pcb, policy, 0 /* initial load */);
    }

    /*
     * If we're already inside the scheduler (e.g. exec called from a
     * running batch-script process), just return — the newly created
     * PCBs are now on the ready queue and will be scheduled normally.
     */
    if (scheduler_active) {
        return 0;
    }

    /*
     * Background mode: read the rest of stdin as prog0 and put it at
     * the FRONT of the queue (it must run first to get a chance to
     * call further exec commands before other processes finish).
     */
    if (background) {
        PCB *prog0 = load_remaining_stdin();
        if (prog0) {
            /* Insert at the very front, before the programs just loaded */
            prog0->next = ready_queue;
            ready_queue = prog0;
        }
    }

    /* Start the scheduler */
    run_scheduler(policy);
    return 0;
}

/* ================================================================
 * Main interpreter dispatcher
 * ================================================================ */
int interpreter(char *command_args[], int args_size) {
    int i;
    /* Strip newlines from all tokens */
    for (i = 0; i < args_size; i++)
        command_args[i][strcspn(command_args[i], "\r\n")] = 0;

    if (args_size < 1) return badcommand();

    const char *cmd = command_args[0];

    if (strcmp(cmd, "help") == 0) {
        if (args_size != 1) return badcommand();
        return do_help();

    } else if (strcmp(cmd, "quit") == 0) {
        if (args_size != 1) return badcommand();
        return do_quit();

    } else if (strcmp(cmd, "set") == 0) {
        if (args_size != 3) return badcommand();
        return do_set(command_args[1], command_args[2]);

    } else if (strcmp(cmd, "print") == 0) {
        if (args_size != 2) return badcommand();
        return do_print(command_args[1]);

    } else if (strcmp(cmd, "echo") == 0) {
        if (args_size != 2) return badcommand();
        return do_echo(command_args[1]);

    } else if (strcmp(cmd, "source") == 0) {
        if (args_size != 2) return badcommand();
        return do_source(command_args[1]);

    } else if (strcmp(cmd, "exec") == 0) {
        if (args_size < 3) return badcommand();
        return do_exec(&command_args[1], args_size - 1);

    } else {
        return badcommand();
    }
}
