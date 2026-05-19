# Makefile – System Monitor build rules
#
# Targets:
#   all    (default) – compile and link the sysmon binary
#   clean            – remove object files and the binary

CC      = gcc
CFLAGS  = -Wall -Wextra -I include
LDFLAGS = -lncurses -lpthread
TARGET  = sysmon
SRCS    = src/main.c src/cpu.c src/memory.c src/process.c src/display.c
OBJS    = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Compile each .c file to a .o file in the same directory.
%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean
