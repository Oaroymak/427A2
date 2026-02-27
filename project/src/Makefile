CC     = gcc
CFLAGS = -Wall -Wextra -g

SRCS = shell.c interpreter.c shellmemory.c framestore.c pcb.c scheduler.c
OBJS = $(SRCS:.c=.o)

mysh: $(OBJS)
	$(CC) $(CFLAGS) -o mysh $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	$(RM) mysh $(OBJS) *~
