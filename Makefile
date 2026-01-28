CC = clang
CFLAGS = -Wall -Wextra -O2 $(shell pkg-config --cflags raylib libcurl)
LDFLAGS = $(shell pkg-config --libs raylib libcurl) -framework IOKit -framework Cocoa -framework OpenGL

TARGET = fitpower
SRCS = main.c fit_parser.c strava_api.c
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
