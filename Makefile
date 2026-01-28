CC = clang
CFLAGS = -Wall -Wextra -O2 $(shell pkg-config --cflags raylib)
LDFLAGS = $(shell pkg-config --libs raylib) -framework IOKit -framework Cocoa -framework OpenGL

TARGET = fitpower
SRCS = main.c fit_parser.c
OBJS = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(TARGET) $(OBJS)

run: $(TARGET)
	./$(TARGET)

.PHONY: all clean run
