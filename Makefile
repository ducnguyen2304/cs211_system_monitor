CC      = gcc
CFLAGS  = -Wall -Wextra -I include
LDFLAGS = -lncurses
TARGET  = sysmon
SRCS    = src/main.c src/cpu.c src/memory.c src/process.c src/display.c
OBJS    = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean
