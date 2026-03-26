CC      = gcc
CFLAGS  = -Wall -Wextra -std=c11 -O2 -I src -I vendor/cjson \
          $(shell pkg-config --cflags ncursesw)
LDFLAGS = $(shell pkg-config --libs ncursesw) -lm

TARGET  = lepy-procman

SRCS = src/main.c \
       src/proc.c \
       src/oom.c  \
       src/rules.c \
       src/search.c \
       src/modlist.c \
       src/userhome.c \
       src/ui.c   \
       vendor/cjson/cJSON.c

OBJS = $(SRCS:.c=.o)

.PHONY: all clean install

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

install: $(TARGET)
	install -m 755 $(TARGET) /usr/local/bin/$(TARGET)

clean:
	rm -f $(OBJS) $(TARGET)
