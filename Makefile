
VERSION = `cat VERSION`
PREFIX ?= /usr/local
GTK_CONFIG = pkg-config gtk+-2.0
PLUGIN_DIR ?= $(PREFIX)/lib/gkrellm2/plugins
GKRELLM_INCLUDE = -I$(PREFIX)/include
GTK_CFLAGS = `$(GTK_CONFIG) --cflags` 
GTK_LIB = `$(GTK_CONFIG) --libs`
FLAGS = -Wall -fPIC $(GTK_CFLAGS) $(GKRELLM_INCLUDE)
CFLAGS ?= -O -g
CFLAGS += $(FLAGS)
CFLAGS += -DVERSION=\"$(VERSION)\"
LIBS = $(GTK_LIB)
LFLAGS = -shared
CC ?= gcc
INSTALL = install -c
INSTALL_PROGRAM = $(INSTALL) -s
OBJS = trayicons.o

trayicons.so: $(OBJS)
	$(CC) $(FLAGS) $(OBJS) -o trayicons.so $(LIBS) $(LFLAGS)

clean:
	rm -f *.o core *.so* *.bak *~

install: 
	$(INSTALL_PROGRAM) trayicons.so $(PLUGIN_DIR)	

%.c.o: %.c

