
CC = $(CROSS_COMPILE)gcc
CFLAGS = -Wall -Werror -g

TARGET = aesdsocket

SRC = $(wildcard *.c)
OBJ = $(SRC:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $^ -o $@

$(OBJ): $(SRC)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(TARGET) *.o
