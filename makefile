CC = gcc

CFLAGS = -Wall -Wextra -std=c11 -Iinclude

SRC = $(filter-out src/%_backup.c,$(wildcard src/*.c))

TARGET = shellforge

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET) -lreadline -lhistory

clean:
	rm -f $(TARGET)

.PHONY: clean
