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

.build/gin: include/gin_command.h include/gin_control.h src/command.c src/control.c include/gin_send.h src/send_values.h include/gin_render.h src/render_values.h src/gin.c src/send.c src/send_payload.c src/send_reply.c src/render.c src/render_escape.c src/render_time.c src/fs.c src/config.c src/naming.c src/input.c src/runtime.c src/http.c src/auth.c src/protocol.c src/protocol_parse.c src/credential.c src/topology.c src/topology_fetch.c src/topology_lookup.c src/connect.c src/liveness_backoff.c src/connect_protocols.c src/connect_topic.c include/gin_connect.h include/gin_liveness.h src/liveness_values.h src/connect_values.h src/phoenix.c src/phoenix_stream.c src/phoenix_refs.c include/gin_phoenix.h src/topology_internal.h include/gin_topology.h include/gin_protocol.h include/gin_auth.h include/gin_credential.h include/gin_http.h include/gin_runtime.h include/gin_input.h include/gin_fs.h include/gin_config.h include/gin_naming.h
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude $(CURL_CFLAGS) $(JANSSON_CFLAGS) $(LDFLAGS) -o $@ src/gin.c src/command.c src/control.c src/send.c src/send_payload.c src/send_reply.c src/render.c src/render_escape.c src/render_time.c src/fs.c src/config.c src/naming.c src/input.c src/runtime.c src/http.c src/auth.c src/protocol.c src/protocol_parse.c src/credential.c src/topology.c src/topology_fetch.c src/topology_lookup.c src/phoenix.c src/phoenix_stream.c src/phoenix_refs.c src/connect.c src/liveness_backoff.c src/connect_protocols.c src/connect_topic.c $(CURL_LIBS) $(JANSSON_LIBS) $(LDLIBS)

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

.build/protocol_driver: tests/protocol_driver.c src/protocol.c src/protocol_parse.c src/auth.c src/http.c src/credential.c include/gin_protocol.h include/gin_auth.h include/gin_http.h include/gin_credential.h
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude $(CURL_CFLAGS) $(JANSSON_CFLAGS) $(LDFLAGS) -o $@ tests/protocol_driver.c src/protocol.c src/protocol_parse.c src/auth.c src/http.c src/credential.c $(CURL_LIBS) $(JANSSON_LIBS) $(LDLIBS)

.build/test_protocol_parse: tests/test_protocol_parse.c src/protocol_parse.c include/gin_protocol.h
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude $(JANSSON_CFLAGS) $(LDFLAGS) -o $@ tests/test_protocol_parse.c src/protocol_parse.c $(JANSSON_LIBS) $(LDLIBS)

.build/test_topology: tests/test_topology.c src/topology.c src/topology_lookup.c src/topology_internal.h src/naming.c include/gin_topology.h include/gin_naming.h
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude $(JANSSON_CFLAGS) $(LDFLAGS) -o $@ tests/test_topology.c src/topology_lookup.c src/naming.c $(JANSSON_LIBS) $(LDLIBS)

# The lookup contract unit supplies its own read-only accessor facade.
.build/test_topology_lookup: tests/test_topology_lookup.c src/topology_lookup.c src/naming.c include/gin_topology.h include/gin_naming.h
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude $(LDFLAGS) -o $@ tests/test_topology_lookup.c src/topology_lookup.c src/naming.c $(LDLIBS)

.build/topology_driver: tests/topology_driver.c src/topology.c src/topology_lookup.c src/topology_fetch.c src/topology_internal.h src/protocol.c src/protocol_parse.c src/auth.c src/http.c src/credential.c src/naming.c include/gin_topology.h include/gin_protocol.h include/gin_auth.h include/gin_http.h include/gin_credential.h
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude $(CURL_CFLAGS) $(JANSSON_CFLAGS) $(LDFLAGS) -o $@ tests/topology_driver.c src/topology.c src/topology_lookup.c src/topology_fetch.c src/protocol.c src/protocol_parse.c src/auth.c src/http.c src/credential.c src/naming.c $(CURL_LIBS) $(JANSSON_LIBS) $(LDLIBS)

# Core and real-stream units include one implementation for private fault injection.
.build/test_phoenix: tests/test_phoenix.c src/phoenix.c include/gin_phoenix.h
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude $(JANSSON_CFLAGS) $(LDFLAGS) -o $@ tests/test_phoenix.c $(JANSSON_LIBS) $(LDLIBS)

.build/test_phoenix_stream: tests/test_phoenix_stream.c src/phoenix_stream.c src/phoenix.c include/gin_phoenix.h
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude $(JANSSON_CFLAGS) $(LDFLAGS) -o $@ tests/test_phoenix_stream.c src/phoenix.c $(JANSSON_LIBS) $(LDLIBS)

# Collector contract facade supplies decode itself; do not link the real codec.
.build/test_phoenix_collect: tests/test_phoenix_collect.c src/phoenix_stream.c include/gin_phoenix.h
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude $(LDFLAGS) -o $@ tests/test_phoenix_collect.c src/phoenix_stream.c $(LDLIBS)

.build/test_phoenix_refs: tests/test_phoenix_refs.c src/phoenix_refs.c include/gin_phoenix.h
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude $(LDFLAGS) -o $@ tests/test_phoenix_refs.c src/phoenix_refs.c $(LDLIBS)

check: all .build/test_liveness_backoff .build/test_connect_liveness .build/liveness_driver .build/test_command .build/test_control .build/control_driver .build/test_connect_query .build/query_driver .build/test_send .build/test_send_payload .build/test_send_reply .build/send_driver .build/test_render .build/test_render_escape .build/test_render_time .build/test_render_output .build/render_driver .build/connect_driver .build/test_connect .build/test_connect_protocols .build/test_connect_topic .build/test_phoenix .build/test_phoenix_stream .build/test_phoenix_collect .build/test_phoenix_refs .build/test_topology .build/test_topology_lookup .build/topology_driver .build/protocol_driver .build/test_protocol_parse .build/test_auth_bounds .build/credential_driver .build/test_credential .build/auth_driver .build/test_http_request .build/http_driver .build/test_http_bounds .build/test_naming .build/lock_holder .build/fifo_consumer .build/test_input .build/runtime_consumer .build/test_runtime_api .build/runtime_boundaries .build/runtime_stderr
	.build/test_liveness_backoff
	.build/test_connect_liveness
	$(PYTHON) tests/test_liveness.py
	.build/test_command
	.build/test_control
	.build/test_connect_query
	$(PYTHON) tests/test_control.py
	$(PYTHON) tests/test_query.py
	.build/test_send
	.build/test_send_payload
	.build/test_send_reply
	$(PYTHON) tests/test_send.py
	.build/test_render
	.build/test_render_escape
	.build/test_render_time
	.build/test_render_output
	$(PYTHON) tests/test_render.py
	.build/test_connect
	.build/test_connect_protocols
	.build/test_connect_topic
	$(PYTHON) tests/test_ws_fixture_frames.py
	$(PYTHON) tests/test_build_host_curl.py
	$(PYTHON) tests/test_connect.py
	.build/test_phoenix
	.build/test_phoenix_stream
	.build/test_phoenix_collect
	.build/test_phoenix_refs
	.build/test_topology
	.build/test_topology_lookup
	$(PYTHON) tests/test_topology.py
	.build/test_protocol_parse
	$(PYTHON) tests/test_protocol.py
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

CONNECT_HEADERS = include/gin_connect.h include/gin_liveness.h src/liveness_values.h include/gin_command.h include/gin_topology.h src/topology_internal.h include/gin_naming.h src/connect_values.h
CONNECT_QUERY_SOURCES = src/command.c src/topology.c src/topology_lookup.c
CONNECT_SUPPORT_SOURCES = src/liveness_backoff.c src/connect_protocols.c src/connect_topic.c src/http.c src/auth.c src/credential.c src/protocol.c src/protocol_parse.c src/phoenix.c src/phoenix_stream.c src/phoenix_refs.c $(CONNECT_QUERY_SOURCES) src/naming.c
CONNECT_SOURCES = src/connect.c $(CONNECT_SUPPORT_SOURCES)

.build/connect_driver: tests/connect_driver.c $(CONNECT_SOURCES) $(CONNECT_HEADERS)
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude -Isrc $(CURL_CFLAGS) $(JANSSON_CFLAGS) $(LDFLAGS) -o $@ tests/connect_driver.c $(CONNECT_SOURCES) $(CURL_LIBS) $(JANSSON_LIBS) $(LDLIBS)

.build/test_liveness_backoff: tests/test_liveness_backoff.c src/liveness_backoff.c src/liveness_values.h
	mkdir -p .build
	$(CC) $(CPPFLAGS) $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Isrc $(LDFLAGS) -o $@ tests/test_liveness_backoff.c src/liveness_backoff.c $(LDLIBS)

# Private translation-unit clock/I/O faults; do not link connect.c twice.
.build/test_connect_liveness: tests/test_connect_liveness.c $(CONNECT_SOURCES) $(CONNECT_HEADERS)
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude -Isrc $(CURL_CFLAGS) $(JANSSON_CFLAGS) $(LDFLAGS) -o $@ tests/test_connect_liveness.c $(CONNECT_SUPPORT_SOURCES) $(CURL_LIBS) $(JANSSON_LIBS) $(LDLIBS)

.build/liveness_driver: tests/liveness_driver.c $(CONNECT_SOURCES) $(CONNECT_HEADERS)
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude -Isrc $(CURL_CFLAGS) $(JANSSON_CFLAGS) $(LDFLAGS) -o $@ tests/liveness_driver.c $(CONNECT_SOURCES) $(CURL_LIBS) $(JANSSON_LIBS) $(LDLIBS)

.build/test_connect: tests/test_connect.c $(CONNECT_SOURCES) $(CONNECT_HEADERS)
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude -Isrc $(CURL_CFLAGS) $(JANSSON_CFLAGS) $(LDFLAGS) -o $@ tests/test_connect.c $(CONNECT_SUPPORT_SOURCES) $(CURL_LIBS) $(JANSSON_LIBS) $(LDLIBS)

.build/test_connect_protocols: tests/test_connect_protocols.c src/connect_protocols.c src/connect_values.h
	mkdir -p .build
	$(CC) $(CPPFLAGS) $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude -Isrc $(LDFLAGS) -o $@ tests/test_connect_protocols.c src/connect_protocols.c $(LDLIBS)

.build/test_connect_topic: tests/test_connect_topic.c src/connect_topic.c src/connect_values.h
	mkdir -p .build
	$(CC) $(CPPFLAGS) $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude -Isrc $(LDFLAGS) -o $@ tests/test_connect_topic.c src/connect_topic.c $(LDLIBS)


SEND_VALUES = src/send_payload.c src/send_reply.c
SEND_SOURCES = src/send.c $(SEND_VALUES) src/protocol.c src/protocol_parse.c src/auth.c src/http.c src/credential.c src/naming.c
SEND_HEADERS = include/gin_send.h src/send_values.h include/gin_protocol.h include/gin_auth.h include/gin_http.h include/gin_credential.h include/gin_naming.h include/gin_fs.h

.build/test_send: tests/test_send.c $(SEND_SOURCES) $(SEND_HEADERS)
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude -Isrc $(CURL_CFLAGS) $(JANSSON_CFLAGS) $(LDFLAGS) -o $@ tests/test_send.c $(SEND_SOURCES) $(CURL_LIBS) $(JANSSON_LIBS) $(LDLIBS)

.build/test_send_payload: tests/test_send_payload.c src/send_payload.c src/naming.c $(SEND_HEADERS)
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude -Isrc $(JANSSON_CFLAGS) $(LDFLAGS) -o $@ tests/test_send_payload.c src/send_payload.c src/naming.c $(JANSSON_LIBS) $(LDLIBS)

.build/test_send_reply: tests/test_send_reply.c src/send_reply.c $(SEND_HEADERS)
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude -Isrc $(JANSSON_CFLAGS) $(LDFLAGS) -o $@ tests/test_send_reply.c src/send_reply.c $(JANSSON_LIBS) $(LDLIBS)

.build/send_driver: tests/send_driver.c $(SEND_SOURCES) $(CONNECT_SOURCES) $(RENDER_SOURCES) src/fs.c src/input.c $(SEND_HEADERS) include/gin_input.h $(CONNECT_HEADERS) include/gin_render.h src/render_values.h
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude -Isrc $(CURL_CFLAGS) $(JANSSON_CFLAGS) $(LDFLAGS) -o $@ tests/send_driver.c $(SEND_SOURCES) $(CONNECT_QUERY_SOURCES) src/connect.c src/liveness_backoff.c src/connect_protocols.c src/connect_topic.c src/phoenix.c src/phoenix_stream.c src/phoenix_refs.c $(RENDER_SOURCES) src/fs.c src/input.c $(CURL_LIBS) $(JANSSON_LIBS) $(LDLIBS)

CONTROL_SOURCES = src/control.c src/command.c src/send_reply.c src/naming.c src/fs.c src/input.c src/http.c src/credential.c src/auth.c src/protocol.c src/protocol_parse.c
CONTROL_HEADERS = include/gin_control.h include/gin_command.h include/gin_send.h src/send_values.h include/gin_naming.h include/gin_fs.h include/gin_input.h include/gin_protocol.h include/gin_auth.h include/gin_http.h include/gin_credential.h

.build/test_command: tests/test_command.c src/command.c src/naming.c include/gin_command.h include/gin_input.h include/gin_naming.h
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude $(LDFLAGS) -o $@ tests/test_command.c src/command.c src/naming.c $(LDLIBS)

.build/test_control: tests/test_control.c $(CONTROL_SOURCES) $(CONTROL_HEADERS)
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude -Isrc $(CURL_CFLAGS) $(JANSSON_CFLAGS) $(LDFLAGS) -o $@ tests/test_control.c $(CONTROL_SOURCES) $(CURL_LIBS) $(JANSSON_LIBS) $(LDLIBS)

.build/control_driver: tests/control_driver.c $(CONTROL_SOURCES) src/send.c src/send_payload.c $(CONTROL_HEADERS)
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude -Isrc $(CURL_CFLAGS) $(JANSSON_CFLAGS) $(LDFLAGS) -o $@ tests/control_driver.c $(CONTROL_SOURCES) src/send.c src/send_payload.c $(CURL_LIBS) $(JANSSON_LIBS) $(LDLIBS)

# This unit includes connect.c privately for deterministic short-write faults.
.build/test_connect_query: tests/test_connect_query.c $(CONNECT_SOURCES) include/gin_connect.h include/gin_command.h include/gin_topology.h src/connect_values.h
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude -Isrc $(CURL_CFLAGS) $(JANSSON_CFLAGS) $(LDFLAGS) -o $@ tests/test_connect_query.c $(CONNECT_SUPPORT_SOURCES) $(CURL_LIBS) $(JANSSON_LIBS) $(LDLIBS)

.build/query_driver: tests/query_driver.c $(CONNECT_SOURCES) src/fs.c src/input.c include/gin_connect.h include/gin_command.h include/gin_topology.h include/gin_fs.h include/gin_input.h src/connect_values.h
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude -Isrc $(CURL_CFLAGS) $(JANSSON_CFLAGS) $(LDFLAGS) -o $@ tests/query_driver.c $(CONNECT_SOURCES) src/fs.c src/input.c $(CURL_LIBS) $(JANSSON_LIBS) $(LDLIBS)

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

RENDER_SOURCES = src/render.c src/render_escape.c src/render_time.c

.build/test_render: tests/test_render.c $(RENDER_SOURCES) src/phoenix.c src/naming.c src/connect_topic.c include/gin_render.h src/render_values.h
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude -Isrc $(JANSSON_CFLAGS) $(LDFLAGS) -o $@ tests/test_render.c src/render_escape.c src/render_time.c src/phoenix.c src/naming.c src/connect_topic.c $(JANSSON_LIBS) $(LDLIBS)

.build/test_render_escape: tests/test_render_escape.c src/render_escape.c include/gin_render.h src/render_values.h
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude -Isrc $(LDFLAGS) -o $@ tests/test_render_escape.c src/render_escape.c $(LDLIBS)

.build/test_render_time: tests/test_render_time.c src/render_time.c include/gin_render.h src/render_values.h
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude -Isrc $(LDFLAGS) -o $@ tests/test_render_time.c src/render_time.c $(LDLIBS)

.build/test_render_output: tests/test_render_output.c src/fs.c src/naming.c include/gin_render.h src/render_values.h
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude -Isrc $(LDFLAGS) -o $@ tests/test_render_output.c src/fs.c src/naming.c $(LDLIBS)

.build/render_driver: tests/render_driver.c $(RENDER_SOURCES) $(CONNECT_SOURCES) src/fs.c src/naming.c $(CONNECT_HEADERS) include/gin_render.h src/render_values.h
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude -Isrc $(CURL_CFLAGS) $(JANSSON_CFLAGS) $(LDFLAGS) -o $@ tests/render_driver.c $(RENDER_SOURCES) $(CONNECT_SOURCES) src/fs.c $(CURL_LIBS) $(JANSSON_LIBS) $(LDLIBS)
