.POSIX:

PREFIX = /usr/local
DESTDIR =
CC = cc
CPPFLAGS =
CFLAGS = -O2
LDFLAGS =
LDLIBS =
PYTHON = python3

all: .build/gin

.build/gin: src/gin.c src/fs.c src/config.c src/naming.c include/gin_fs.h include/gin_config.h include/gin_naming.h
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude $(LDFLAGS) -o $@ src/gin.c src/fs.c src/config.c src/naming.c $(LDLIBS)

.build/test_naming: tests/test_naming.c src/naming.c include/gin_naming.h
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude $(LDFLAGS) -o $@ tests/test_naming.c src/naming.c $(LDLIBS)

check: all .build/test_naming
	.build/test_naming
	$(PYTHON) tests/test_cli.py

clean:
	rm -rf .build

install: all
	mkdir -p "$(DESTDIR)$(PREFIX)/bin" "$(DESTDIR)$(PREFIX)/share/man/man1"
	cp .build/gin "$(DESTDIR)$(PREFIX)/bin/gin"
	chmod 755 "$(DESTDIR)$(PREFIX)/bin/gin"
	cp man/gin.1 "$(DESTDIR)$(PREFIX)/share/man/man1/gin.1"
	chmod 644 "$(DESTDIR)$(PREFIX)/share/man/man1/gin.1"

uninstall:
	rm -f "$(DESTDIR)$(PREFIX)/bin/gin" "$(DESTDIR)$(PREFIX)/share/man/man1/gin.1"
