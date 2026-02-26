#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "shellmemory.h"

struct memory_struct {
    char *var;
    char *value;
};

static struct memory_struct shellmemory[MEM_SIZE];

void mem_init(void) {
    for (int i = 0; i < MEM_SIZE; i++) {
        shellmemory[i].var   = NULL;
        shellmemory[i].value = NULL;
    }
}

void mem_set_value(char *var_in, char *value_in) {
    /* Update existing binding */
    for (int i = 0; i < MEM_SIZE; i++) {
        if (shellmemory[i].var && strcmp(shellmemory[i].var, var_in) == 0) {
            free(shellmemory[i].value);
            shellmemory[i].value = strdup(value_in);
            return;
        }
    }
    /* Insert new binding */
    for (int i = 0; i < MEM_SIZE; i++) {
        if (shellmemory[i].var == NULL) {
            shellmemory[i].var   = strdup(var_in);
            shellmemory[i].value = strdup(value_in);
            return;
        }
    }
    fprintf(stderr, "Shell memory full!\n");
}

char *mem_get_value(char *var_in) {
    for (int i = 0; i < MEM_SIZE; i++) {
        if (shellmemory[i].var && strcmp(shellmemory[i].var, var_in) == 0) {
            return strdup(shellmemory[i].value);
        }
    }
    return NULL;
}
