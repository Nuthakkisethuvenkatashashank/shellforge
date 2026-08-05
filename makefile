CC = gcc

CFLAGS = -Wall -Wextra -std=c11 -Iinclude

SRC = $(wildcard src/*.c)

TARGET = shellforge

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET) -lreadline -lhistory

clean:
	rm -f $(TARGET)

.PHONY: clean
