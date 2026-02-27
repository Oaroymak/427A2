//#define DEBUG 1

#ifdef DEBUG
#   define debug(...) fprintf(stderr, __VA_ARGS__)
#else
#   define debug(...)
// NDEBUG disables asserts
#   define NDEBUG
#endif

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>              // tolower, isdigit
#include <dirent.h>             // scandir
#include <unistd.h>             // chdir
#include <sys/stat.h>           // mkdir
// for run:
#include <sys/types.h>          // pid_t
#include <sys/wait.h>           // waitpid

#include "shellmemory.h"
#include "shell.h"
// A2 additions: frame store, PCB, and scheduler
#include "framestore.h"
#include "pcb.h"
#include "scheduler.h"

int badcommand() {
    printf("Unknown Command\n");
    return 1;
}

// For source command only
int badcommandFileDoesNotExist() {
    printf("Bad command: File not found\n");
    return 3;
}

int badcommandMkdir() {
    printf("Bad command: my_mkdir\n");
    return 4;
}

int badcommandCd() {
    printf("Bad command: my_cd\n");
    return 5;
}

int help();
int quit();
int set(char *var, char *value);
int print(char *var);
int echo(char *tok);
int ls();
int my_mkdir(char *name);
int touch(char *path);
int cd(char *path);
int source(char *script);
int run(char *args[], int args_size);
int badcommandFileDoesNotExist();
// A2: new functions declared here so source() can call load_script() before it's defined
int exec_cmd(char *args[], int args_size);
int load_script(char *filename, int *out_count);
Policy parse_policy(char *s);
PCB *load_remaining_stdin();

// Interpret commands and their arguments
int interpreter(char *command_args[], int args_size) {
    int i;

    // these bits of debug output were very helpful for debugging
    // the changes we made to the parser!
    debug("#args: %d\n", args_size);
#ifdef DEBUG
    for (size_t i = 0; i < args_size; ++i) {
        debug("  %ld: %s\n", i, command_args[i]);
    }
#endif

    if (args_size < 1) {
        // This shouldn't be possible but we are defensive programmers.
        fprintf(stderr, "interpreter called with no words?\n");
        exit(1);
    }

    for (i = 0; i < args_size; i++) {   // terminate args at newlines
        command_args[i][strcspn(command_args[i], "\r\n")] = 0;
    }

    if (strcmp(command_args[0], "help") == 0) {
        //help
        if (args_size != 1)
            return badcommand();
        return help();

    } else if (strcmp(command_args[0], "quit") == 0) {
        //quit
        if (args_size != 1)
            return badcommand();
        return quit();

    } else if (strcmp(command_args[0], "set") == 0) {
        //set
        if (args_size != 3)
            return badcommand();
        return set(command_args[1], command_args[2]);

    } else if (strcmp(command_args[0], "print") == 0) {
        if (args_size != 2)
            return badcommand();
        return print(command_args[1]);

    } else if (strcmp(command_args[0], "echo") == 0) {
        if (args_size != 2)
            return badcommand();
        return echo(command_args[1]);

    } else if (strcmp(command_args[0], "my_ls") == 0) {
        if (args_size != 1)
            return badcommand();
        return ls();

    } else if (strcmp(command_args[0], "my_mkdir") == 0) {
        if (args_size != 2)
            return badcommand();
        return my_mkdir(command_args[1]);

    } else if (strcmp(command_args[0], "my_touch") == 0) {
        if (args_size != 2)
            return badcommand();
        return touch(command_args[1]);

    } else if (strcmp(command_args[0], "my_cd") == 0) {
        if (args_size != 2)
            return badcommand();
        return cd(command_args[1]);

    } else if (strcmp(command_args[0], "source") == 0) {
        if (args_size != 2)
            return badcommand();
        return source(command_args[1]);

    } else if (strcmp(command_args[0], "run") == 0) {
        if (args_size < 2)
            return badcommand();
        return run(&command_args[1], args_size - 1);

    } else if (strcmp(command_args[0], "exec") == 0) {
        // A2: exec takes at least a program name and a policy
        if (args_size < 3)
            return badcommand();
        return exec_cmd(&command_args[1], args_size - 1);

    } else
        return badcommand();
}

int help() {

    // note the literal tab characters here for alignment
    char help_string[] = "COMMAND			DESCRIPTION\n \
help			Displays all the commands\n \
quit			Exits / terminates the shell with \"Bye!\"\n \
set VAR STRING		Assigns a value to shell memory\n \
print VAR		Displays the STRING assigned to VAR\n \
source SCRIPT.TXT		Executes the file SCRIPT.TXT\n ";
    printf("%s\n", help_string);
    return 0;
}

int quit() {
    printf("Bye!\n");
    exit(0);
}

int set(char *var, char *value) {
    mem_set_value(var, value);
    return 0;
}

int print(char *var) {
    char *value = mem_get_value(var);
    if (value) {
        printf("%s\n", value);
        free(value);
    } else {
        printf("Variable does not exist\n");
    }
    return 0;
}

int echo(char *tok) {
    int must_free = 0;
    // is it a var?
    if (tok[0] == '$') {
        tok++;                  // advance pointer, so that tok is now the stuff after '$'
        tok = mem_get_value(tok);
        if (tok == NULL) {
            tok = "";           // must use empty string, can't pass NULL to printf
        } else {
            must_free = 1;
        }
    }

    printf("%s\n", tok);

    // memory management technically optional for this assignment
    if (must_free) free(tok);

    return 0;
}

// We can hide dotfiles in ls using either the filter operand to scandir,
// or by checking the first character ourselves when we go to print
// the names. That would work, and is less code, but this is more robust.
// And this is also better since it won't allocate extra dirents.
int ls_filter(const struct dirent *d) {
    if (d->d_name[0] == '.') return 0;
    return 1;
}

int ls_compare_char(char a, char b) {
    // assumption: a,b are both either digits or letters.
    // If this is not true, the characters will be effectively compared
    // as ASCII when we do the lower_a - lower_b fallback.

    // if both are digits, compare them
    if (isdigit(a) && isdigit(b)) {
        return a - b;
    }
    // if only a is a digit, then b isn't, so a wins.
    if (isdigit(a)) {
        return -1;
    }

    // lowercase both letters so we can compare their alphabetic position.
    char lower_a = tolower(a), lower_b = tolower(b);
    if (lower_a == lower_b) {
        // a and b are the same letter, possibly in different cases.
        // If they are really the same letter, this returns 0.
        // Otherwise, it's negative if A was capital,
        // and positive if B is capital.
        return a - b;
    }

    // Otherwise, compare their alphabetic position by comparing
    // them at a known case.
    return lower_a - lower_b;
}

int ls_compare_str(const char *a, const char *b) {
    // a simple strcmp implementation that uses ls_compare_char.
    // We only check if *a is zero, since if *b is zero earlier,
    // it would've been unequal to *a at that time and we would return.
    // If *b is zero at the same point or later than *a, we'll exit the
    // loop and return the correct value with the last comparison.

    while (*a != '\0') {
        int d = ls_compare_char(*a, *b);
        if (d != 0) return d;
        a++, b++;
    }
    return ls_compare_char(*a, *b);
}

int ls_compare(const struct dirent **a, const struct dirent **b) {
    return ls_compare_str((*a)->d_name, (*b)->d_name);
}

int ls() {
    // straight out of the man page examples for scandir
    // alphasort uses strcoll instead of strcmp,
    // so we have to implement our own comparator to match the ls spec.
    // Note that the test cases weren't very picky about the specified order,
    // so if you just used alphasort with scandir, you should have passed.
    // This was intentional on our part.
    struct dirent **namelist;
    int n;

    n = scandir(".", &namelist, NULL, ls_compare);
    if (n == -1) {
        // something is catastrophically wrong, just give up.
        perror("my_ls couldn't scan the directory");
        return 0;
    }

    for (size_t i = 0; i < n; ++i) {
        printf("%s\n", namelist[i]->d_name);
        free(namelist[i]);
    }
    free(namelist);

    return 0;
}

int str_isalphanum(char *name) {
    for (char c = *name; c != '\0'; c = *++name) {
        if (!(isdigit(c) || isalpha(c))) return 0;
    }
    return 1;
}

int my_mkdir(char *name) {
    int must_free = 0;

    debug("my_mkdir: ->%s<-\n", name);

    if (name[0] == '$') {
        ++name;
        // lookup name
        name = mem_get_value(name);
        debug("  lookup: %s\n", name ? name : "(NULL)");
        if (name) {
            // name exists, should free whatever we got
            must_free = 1;
        }
    }
    if (!name || !str_isalphanum(name)) {
        // either name doesn't exist, or isn't valid, error.
        if (must_free) free(name);
        return badcommandMkdir();
    }
    // at this point name is definitely OK

    // 0777 means "777 in octal," aka 511. This value means
    // "give the new folder all permissions that we can."
    int result = mkdir(name, 0777);

    if (result) {
        // description doesn't specify what to do in this case,
        // (including if the directory already exists)
        // so we just give an error message on stderr and ignore it.
        perror("Something went wrong in my_mkdir");
    }

    if (must_free) free(name);
    return 0;
}

int touch(char *path) {
    // we're told we can assume this.
    assert(str_isalphanum(path));
    // if things go wrong, just ignore it.
    FILE *f = fopen(path, "a");
    fclose(f);
    return 0;
}

int cd(char *path) {
    // we're told we can assume this.
    assert(str_isalphanum(path));

    int result = chdir(path);
    if (result) {
        // chdir can fail for several reasons, but the only one we need
        // to handle here for the spec is the ENOENT reason,
        // aka Error NO ENTry -- the directory doesn't exist.
        // Since that's the only one we have to handle, we'll just assume
        // that that's what happened.
        // Alternatively, you can check if the directory exists
        // explicitly first using `stat`. However it is often better to
        // simply try to use a filesystem resource and then recover when
        // you can't, rather than trying to validate first. If you validate
        // first while two users are on the system, there's a race condition!
        return badcommandCd();
    }
    return 0;
}

// A2: source now loads the script into the frame store and runs it
// through the scheduler as a single FCFS process, so that the scheduling
// infrastructure is exercised even for single-script execution.
// Behaviour is identical to A1 from the user's perspective.
int source(char *script) {
    int count;
    int start = load_script(script, &count);
    if (start < 0)
        return badcommandFileDoesNotExist();
    if (count == 0)
        return 0;   // empty script, nothing to do

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

int run(char *args[], int arg_size) {
    // copy the args into a new NULL-terminated array.
    char **adj_args = calloc(arg_size + 1, sizeof(char *));
    for (int i = 0; i < arg_size; ++i) {
        adj_args[i] = args[i];
    }

    // always flush output streams before forking.
    fflush(stdout);
    // attempt to fork the shell
    pid_t pid = fork();
    if (pid < 0) {
        // fork failed. Report the error and move on.
        perror("fork() failed");
        return 1;
    } else if (pid == 0) {
        // we are the new child process.
        execvp(adj_args[0], adj_args);
        perror("exec failed");
        // The parent and child are sharing stdin, and according to
        // a part of the glibc documentation that you are **not**
        // expected to know for this course, a shared input handle
        // should be fflushed (if it is needed) or closed
        // (if it is not). Handling this exec error case is not even
        // necessary, but let's do it right.
        // (Failure to do this can result in the parent process
        // reading the remaining input twice in batch mode.)
        fclose(stdin);
        exit(1);
    } else {
        // we are the parent process.
        waitpid(pid, NULL, 0);
    }

    return 0;
}

// ================================================================
// A2 additions: load_script, parse_policy, load_remaining_stdin,
// and exec_cmd.  Everything above this line is unchanged from A1.
// ================================================================

// Open a script file and load each line into the frame store.
// Returns the starting frame index on success, -1 on error.
// *out_count is set to the number of lines loaded.
int load_script(char *filename, int *out_count) {
    FILE *f = fopen(filename, "r");
    if (!f) return -1;

    // First pass: count lines so we know how many frames to allocate.
    int count = 0;
    char line[MAX_USER_INPUT];
    while (fgets(line, sizeof(line), f))
        count++;

    rewind(f);

    if (count == 0) {
        fclose(f);
        *out_count = 0;
        return 0;
    }

    int start = fs_alloc(count);
    if (start < 0) {
        fclose(f);
        fprintf(stderr, "Frame store full.\n");
        return -1;
    }

    int idx = start;
    while (fgets(line, sizeof(line), f))
        fs_set(idx++, line);

    fclose(f);
    *out_count = count;
    return start;
}

// Convert a policy name string to the Policy enum.
// Returns -1 if the string is not a recognized policy.
Policy parse_policy(char *s) {
    if (strcmp(s, "FCFS")  == 0) return POLICY_FCFS;
    if (strcmp(s, "SJF")   == 0) return POLICY_SJF;
    if (strcmp(s, "RR")    == 0) return POLICY_RR;
    if (strcmp(s, "RR30")  == 0) return POLICY_RR30;
    if (strcmp(s, "AGING") == 0) return POLICY_AGING;
    return (Policy)-1;
}

// Read the remaining stdin into the frame store as a new process (prog0).
// Used for background (#) mode: the rest of the batch script becomes a
// scheduled process so it interleaves with the programs given to exec.
// Returns the new PCB, or NULL if stdin is empty.
PCB *load_remaining_stdin() {
    char  lines[FRAME_STORE_SIZE][MAX_LINE_LEN];
    int   count = 0;
    char  buf[MAX_USER_INPUT];

    while (fgets(buf, sizeof(buf), stdin) && count < FRAME_STORE_SIZE) {
        // strip newline
        int len = (int)strlen(buf);
        while (len > 0 && (buf[len-1] == '\n' || buf[len-1] == '\r')) len--;
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

    for (int i = 0; i < count; i++)
        fs_set(start + i, lines[i]);

    return pcb_create(start, start + count);
}

// exec_cmd: run up to 3 programs concurrently under a given scheduling policy.
// Syntax: exec prog1 [prog2 [prog3]] POLICY [#]
// args[] and nargs cover everything after the "exec" keyword.
int exec_cmd(char *args[], int nargs) {
    int background = 0;

    // Check for optional trailing '#' (background mode)
    if (nargs >= 2 && strcmp(args[nargs - 1], "#") == 0) {
        background = 1;
        nargs--;
    }

    // After stripping '#', we expect 2..4 args: up to 3 filenames + policy
    if (nargs < 2 || nargs > 4) {
        printf("Bad command: wrong number of arguments for exec\n");
        return 2;
    }

    // Last remaining arg is the scheduling policy
    Policy policy = parse_policy(args[nargs - 1]);
    if ((int)policy < 0) {
        printf("Bad command: invalid scheduling policy\n");
        return 2;
    }

    int prog_count = nargs - 1;
    char **filenames = args;   // first prog_count args are filenames

    // Reject duplicate filenames
    for (int i = 0; i < prog_count; i++)
        for (int j = i + 1; j < prog_count; j++)
            if (strcmp(filenames[i], filenames[j]) == 0) {
                printf("Bad command: duplicate program names\n");
                return 2;
            }

    // Validate all files exist before loading any of them,
    // so we never end up in a half-loaded state.
    for (int i = 0; i < prog_count; i++) {
        FILE *tmp = fopen(filenames[i], "r");
        if (!tmp) {
            printf("Bad command: File %s not found\n", filenames[i]);
            return 3;
        }
        fclose(tmp);
    }

    // Load each program into the frame store and create its PCB
    int starts[3], counts[3];
    int loaded = 0;
    for (int i = 0; i < prog_count; i++) {
        starts[i] = load_script(filenames[i], &counts[i]);
        if (starts[i] < 0) {
            // Undo already-loaded programs before returning
            for (int j = 0; j < loaded; j++)
                fs_free(starts[j], counts[j]);
            printf("Bad command: could not load %s\n", filenames[i]);
            return 3;
        }
        loaded++;
    }

    for (int i = 0; i < prog_count; i++) {
        PCB *pcb = pcb_create(starts[i], starts[i] + counts[i]);
        if (!pcb) {
            fprintf(stderr, "Out of memory creating PCB\n");
            return 1;
        }
        queue_add(pcb, policy, 0 /* initial load, not a reschedule */);
    }

    // If we are already inside a running scheduler (i.e. this exec was
    // called from within a scheduled process via background mode), just
    // return.  The newly enqueued PCBs will be picked up by the existing
    // scheduler loop automatically.
    if (scheduler_active)
        return 0;

    // Background mode: consume the rest of stdin as prog0 (the "batch
    // script process") and place it at the front of the queue so it gets
    // a time slice before any of the just-loaded programs run.
    if (background) {
        PCB *prog0 = load_remaining_stdin();
        if (prog0) {
            prog0->next = ready_queue;
            ready_queue = prog0;
        }
    }

    run_scheduler(policy);
    return 0;
}
