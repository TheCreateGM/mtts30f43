# MTT S30 GPU Management Tool Makefile
# Fedora Linux compatible
#
# Copyright © 2024 Moore Threads Inc. All rights reserved.

CC       = gcc
CFLAGS   = -Wall -Wextra -O2 -D_GNU_SOURCE
LDFLAGS  =
LDLIBS   =

TARGET   = mtt-tool
SRCS     = mtt-tool.c
OBJS     = $(SRCS:.c=.o)

PREFIX   ?= /usr/local
BINDIR   ?= $(PREFIX)/bin

.PHONY: all clean install uninstall

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET)

install: $(TARGET)
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 $(TARGET) $(DESTDIR)$(BINDIR)/

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(TARGET)
