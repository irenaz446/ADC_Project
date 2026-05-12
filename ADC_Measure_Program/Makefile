CC = gcc
CFLAGS = -Wall -Wextra -Wpedantic -Werror -g
# Added -lz for CRC32 calculations
LDFLAGS = -lsqlite3 -lz 
TARGET = uut_cli

SRCS = pc_test_uut.c test_db.c
OBJS = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS) $(LDFLAGS)

%.o: %.c pc_test_uut.h test_db.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(TARGET) $(OBJS) *.db

.PHONY: all clean