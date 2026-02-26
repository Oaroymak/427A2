#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>

#include "shell.h"
#include "interpreter.h"
#include "shellmemory.h"
#include "framestore.h"

int parseInput(char ui[]);

int main(int argc, char *argv[]) {
    printf("Shell version 1.4 created December 2024\n\n");

    char userInput[MAX_USER_INPUT];
    int  batch_mode = !isatty(STDIN_FILENO);

    memset(userInput, 0, sizeof(userInput));

    mem_init();
    fs_init();

    while (1) {
        if (!batch_mode)
            printf("$ ");

        if (!fgets(userInput, MAX_USER_INPUT - 1, stdin))
            break;

        int errorCode = parseInput(userInput);
        if (errorCode == -1)
            exit(99);

        if (feof(stdin))
            break;

        memset(userInput, 0, sizeof(userInput));
    }

    return 0;
}

/* ----------------------------------------------------------------
 * Tokenise one line and dispatch to the interpreter.
 * Handles semicolons for command chaining.
 * ---------------------------------------------------------------- */

static int wordEnding(char c) {
    return c == '\0' || c == '\n' || isspace(c) || c == ';';
}

int parseInput(char inp[]) {
    char  tmp[200];
    char *words[100];
    int   ix = 0, w = 0;
    int   wordlen;
    int   errorCode = 0;

    while (inp[ix] != '\n' && inp[ix] != '\0' && ix < 1000) {
        /* skip whitespace */
        for (; isspace(inp[ix]) && inp[ix] != '\n' && ix < 1000; ix++);
        if (inp[ix] == ';') break;

        for (wordlen = 0; !wordEnding(inp[ix]) && ix < 1000; ix++, wordlen++)
            tmp[wordlen] = inp[ix];

        if (wordlen > 0) {
            tmp[wordlen] = '\0';
            words[w++] = strdup(tmp);
            if (inp[ix] == '\0') break;
        } else {
            break;
        }
    }

    if (w > 0) {
        errorCode = interpreter(words, w);
        for (int i = 0; i < w; i++) free(words[i]);
    }

    if (inp[ix] == ';')
        return parseInput(&inp[ix + 1]);

    return errorCode;
}
