CC = clang
CFLAGS = -Wall -Wextra -O2 $(shell pkg-config --cflags raylib libcurl)

UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
    LDFLAGS = $(shell pkg-config --libs raylib libcurl) -framework IOKit -framework Cocoa -framework OpenGL
else
    LDFLAGS = $(shell pkg-config --libs raylib libcurl) -lm
endif

TARGET = fitpower
SRCS = main.c fit_parser.c strava_api.c file_organizer.c activity_tree.c tile_map.c
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
