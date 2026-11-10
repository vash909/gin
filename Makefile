.POSIX:

PREFIX = /usr/local
DESTDIR =
CC = cc
CPPFLAGS =
CFLAGS = -O2
LDFLAGS =
LDLIBS =
PYTHON = python3
CURL_CONFIG = curl-config
CURL_CFLAGS = `$(CURL_CONFIG) --cflags`
CURL_LIBS = `$(CURL_CONFIG) --libs`
JANSSON_CFLAGS = $${JANSSON_CFLAGS:-$$(pkg-config --cflags jansson)}
JANSSON_LIBS = $${JANSSON_LIBS:-$$(pkg-config --libs jansson)}

all: .build/gin

.build/gin: src/gin.c src/fs.c src/config.c src/naming.c src/input.c src/runtime.c src/http.c src/auth.c src/credential.c include/gin_auth.h include/gin_credential.h include/gin_http.h include/gin_runtime.h include/gin_input.h include/gin_fs.h include/gin_config.h include/gin_naming.h
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude $(CURL_CFLAGS) $(JANSSON_CFLAGS) $(LDFLAGS) -o $@ src/gin.c src/fs.c src/config.c src/naming.c src/input.c src/runtime.c src/http.c src/auth.c src/credential.c $(CURL_LIBS) $(JANSSON_LIBS) $(LDLIBS)

.build/test_naming: tests/test_naming.c src/naming.c include/gin_naming.h
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude $(LDFLAGS) -o $@ tests/test_naming.c src/naming.c $(LDLIBS)

.build/lock_holder: tests/lock_holder.c src/fs.c src/naming.c include/gin_fs.h include/gin_naming.h
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude $(LDFLAGS) -o $@ tests/lock_holder.c src/fs.c src/naming.c $(LDLIBS)

.build/fifo_consumer: tests/fifo_consumer.c src/input.c src/fs.c src/naming.c include/gin_input.h include/gin_fs.h include/gin_naming.h
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude $(LDFLAGS) -o $@ tests/fifo_consumer.c src/input.c src/fs.c src/naming.c $(LDLIBS)

.build/test_input: tests/test_input.c src/input.c include/gin_input.h
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude $(LDFLAGS) -o $@ tests/test_input.c src/input.c $(LDLIBS)

.build/runtime_consumer: tests/runtime_consumer.c src/runtime.c src/input.c src/fs.c src/naming.c include/gin_runtime.h include/gin_input.h include/gin_fs.h include/gin_naming.h
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude $(LDFLAGS) -o $@ tests/runtime_consumer.c src/runtime.c src/input.c src/fs.c src/naming.c $(LDLIBS)

.build/test_runtime_api: tests/test_runtime_api.c src/runtime.c src/input.c src/fs.c src/naming.c include/gin_runtime.h include/gin_input.h include/gin_fs.h include/gin_naming.h
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude $(LDFLAGS) -o $@ tests/test_runtime_api.c src/runtime.c src/input.c src/fs.c src/naming.c $(LDLIBS)

.build/runtime_boundaries.o: src/runtime.c include/gin_runtime.h
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude -Dgin_input_next=fixture_next -Dsigprocmask=fixture_sigprocmask -c src/runtime.c -o $@

.build/runtime_boundaries: tests/runtime_boundaries.c .build/runtime_boundaries.o src/input.c src/fs.c src/naming.c
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude $(LDFLAGS) -o $@ tests/runtime_boundaries.c .build/runtime_boundaries.o src/input.c src/fs.c src/naming.c $(LDLIBS)

.build/runtime_stderr.o: src/gin.c include/gin_runtime.h include/gin_config.h include/gin_fs.h
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude -Dfputs=fixture_fputs -c src/gin.c -o $@

.build/runtime_stderr: tests/runtime_stderr.c .build/runtime_stderr.o src/runtime.c src/input.c src/fs.c src/naming.c src/config.c
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude $(LDFLAGS) -o $@ tests/runtime_stderr.c .build/runtime_stderr.o src/runtime.c src/input.c src/fs.c src/naming.c src/config.c $(LDLIBS)

.build/http_driver: tests/http_driver.c src/http.c include/gin_http.h
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude $(CURL_CFLAGS) $(LDFLAGS) -o $@ tests/http_driver.c src/http.c $(CURL_LIBS) $(LDLIBS)

.build/test_http_bounds: tests/test_http_bounds.c src/http.c include/gin_http.h
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude $(CURL_CFLAGS) $(LDFLAGS) -o $@ tests/test_http_bounds.c $(CURL_LIBS) $(LDLIBS)

.build/auth_driver: tests/auth_driver.c src/auth.c src/http.c src/credential.c src/fs.c src/naming.c include/gin_auth.h include/gin_credential.h include/gin_http.h
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude $(CURL_CFLAGS) $(JANSSON_CFLAGS) $(LDFLAGS) -o $@ tests/auth_driver.c src/auth.c src/http.c src/credential.c src/fs.c src/naming.c $(CURL_LIBS) $(JANSSON_LIBS) $(LDLIBS)

.build/test_http_request: tests/test_http_request.c src/http.c include/gin_http.h
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude $(CURL_CFLAGS) $(LDFLAGS) -o $@ tests/test_http_request.c src/http.c $(CURL_LIBS) $(LDLIBS)

.build/test_auth_bounds: tests/test_auth_bounds.c src/auth.c src/http.c src/credential.c include/gin_auth.h
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude $(CURL_CFLAGS) $(JANSSON_CFLAGS) $(LDFLAGS) -o $@ tests/test_auth_bounds.c src/auth.c src/http.c src/credential.c $(CURL_LIBS) $(JANSSON_LIBS) $(LDLIBS)

.build/credential_driver: tests/credential_driver.c src/credential.c src/fs.c src/naming.c include/gin_credential.h
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude $(JANSSON_CFLAGS) $(LDFLAGS) -o $@ tests/credential_driver.c src/credential.c src/fs.c src/naming.c $(JANSSON_LIBS) $(LDLIBS)

.build/test_credential: tests/test_credential.c src/credential.c include/gin_credential.h
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude $(JANSSON_CFLAGS) $(LDFLAGS) -o $@ tests/test_credential.c src/credential.c $(JANSSON_LIBS) $(LDLIBS)

check: all .build/test_auth_bounds .build/credential_driver .build/test_credential .build/auth_driver .build/test_http_request .build/http_driver .build/test_http_bounds .build/test_naming .build/lock_holder .build/fifo_consumer .build/test_input .build/runtime_consumer .build/test_runtime_api .build/runtime_boundaries .build/runtime_stderr
	.build/test_auth_bounds
	.build/test_credential
	$(PYTHON) tests/test_credentials.py
	.build/test_http_request
	$(PYTHON) tests/test_auth.py AuthTests
	.build/test_http_bounds
	.build/test_naming
	.build/test_input
	$(PYTHON) tests/test_cli.py
	$(PYTHON) tests/test_locking.py
	$(PYTHON) tests/test_input.py
	$(PYTHON) tests/test_runtime.py
	$(PYTHON) tests/test_http.py

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
