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
FS_HEADERS = include/gin_fs.h src/fs_private.h include/gin_naming.h
ACCOUNT_COMPONENT_TARGETS = .build/test_status_projection .build/test_status_projection_fidelity .build/test_status_render .build/test_connect_replan .build/connect_replan_wss_driver .build/account_discovery_driver .build/test_account_naming .build/terminal_password_driver .build/terminal_password_fault_driver .build/account_fs_driver .build/test_account_checkpoint .build/test_account_root .build/test_account_root_fault .build/auth_login_control_driver .build/interactive_login_driver .build/test_account_plan .build/test_account_plan_fault .build/test_account_live_queue .build/test_account_sinks .build/test_account_sinks_fault .build/test_account_network_checkpoint .build/account_output_driver .build/account_history_output_driver .build/test_account_receive_control
ACCOUNT_COMPONENT_TARGETS += .build/account_receive_driver .build/account_dispatch_driver .build/test_account_dispatch_fidelity
ACCOUNT_COMPONENT_TARGETS += .build/account_history_bootstrap_driver
ACCOUNT_COMPONENT_TARGETS += .build/account_owner_driver
ACCOUNT_COMPONENT_TARGETS += .build/gin .build/account_cli_control_driver .build/gin_account_fifo_observer
ACCOUNT_COMPONENT_TARGETS += .build/test_account_registry
ACCOUNT_COMPONENT_TARGETS += .build/test_connect_hot .build/test_connect_hot_fault .build/connect_hot_wss_driver
ACCOUNT_COMPONENT_TARGETS += .build/test_account_dispatch_hot .build/account_dispatch_hot_wss_driver
ACCOUNT_COMPONENT_TARGETS += .build/test_account_receive_hot .build/account_receive_hot_wss_driver
ACCOUNT_RESOURCE_TARGETS = .build/test_credential_identity .build/test_account_root_hot_fault .build/test_account_fs_hot_fault .build/test_account_sinks_hot .build/test_account_sinks_hot_bind .build/test_account_sinks_hot_status .build/test_account_output_hot
ACCOUNT_COMPONENT_TARGETS += $(ACCOUNT_RESOURCE_TARGETS)
RESOURCE_SNAPSHOT_FLAGS =
CURL_CFLAGS = `$(CURL_CONFIG) --cflags`
CURL_LIBS = `$(CURL_CONFIG) --libs`
JANSSON_CFLAGS = $${JANSSON_CFLAGS:-$$(pkg-config --cflags jansson)}
JANSSON_LIBS = $${JANSSON_LIBS:-$$(pkg-config --libs jansson)}

CHECK_MAKE = $(MAKE)
AUTHORITY_JSON_TARGETS = .build/test_authority_json_config .build/test_authority_json_identity .build/test_authority_json_store .build/test_authority_json_connect

all: .build/gin
# Keep standalone filesystem consumers sensitive to the private traversal contract.
.build/auth_driver .build/credential_driver .build/fifo_consumer .build/gin .build/gin_cleanup_fault .build/gin_owner_fault .build/gin_receive_observer .build/history_driver .build/history_http_driver .build/lock_holder .build/query_driver .build/render_driver .build/runtime_boundaries .build/runtime_consumer .build/runtime_stderr .build/send_driver .build/test_authority_json_connect .build/test_authority_json_identity .build/test_authority_json_store .build/test_checkpoint .build/test_render_output .build/test_runtime_api .build/test_session_config: src/fs_private.h


RENDER_SOURCES = src/render.c src/render_escape.c src/render_time.c

HISTORY_PURE_SOURCES = src/history_page.c src/history_evidence.c src/history_path.c src/history_recovery.c
HISTORY_SOURCES = $(HISTORY_PURE_SOURCES) src/history_fetch.c src/history_render.c
HTTP_CONTROL_HEADERS = src/http_control.h include/gin_control.h include/gin_send.h include/gin_http.h include/gin_auth.h include/gin_protocol.h include/gin_topology.h include/gin_credential.h

ACCOUNT_SINKS_SUPPORT = src/account_plan.c src/account_naming.c src/account_fs.c src/account_root.c src/fs.c src/naming.c src/credential.c src/topology.c src/topology_lookup.c src/connect_topic.c src/account_registry.c src/command.c
ACCOUNT_SINKS_HEADERS = include/gin_auth.h include/gin_connect.h include/gin_credential.h include/gin_fs.h include/gin_http.h include/gin_naming.h include/gin_phoenix.h include/gin_protocol.h include/gin_topology.h src/account_fs.h src/account_naming.h src/account_plan.h src/account_root.h src/account_sinks.h src/connect_values.h src/fs_private.h src/json_evidence.h src/topology_internal.h

ACCOUNT_RECEIVE_SUPPORT = src/account_plan.c src/account_naming.c src/session_live_queue.c src/fs.c $(CONNECT_SOURCES)
ACCOUNT_RECEIVE_HEADERS = src/connect_hot.h include/gin_auth.h include/gin_command.h include/gin_connect.h include/gin_control.h include/gin_credential.h include/gin_fs.h include/gin_http.h include/gin_liveness.h include/gin_naming.h include/gin_phoenix.h include/gin_protocol.h include/gin_send.h include/gin_topology.h src/account_naming.h src/account_plan.h src/account_receive.h src/connect_control.h src/connect_values.h src/fs_private.h src/http_control.h src/json_evidence.h src/liveness_values.h src/session_live_queue.h src/topology_internal.h
ACCOUNT_DISPATCH_SUPPORT = src/account_registry.c src/account_receive.c src/account_plan.c src/account_naming.c src/session_live_queue.c src/session_live_projection.c src/history_page.c src/history_evidence.c $(CONNECT_SOURCES) $(RENDER_SOURCES)
ACCOUNT_DISPATCH_HEADERS = src/connect_hot.h include/gin_auth.h include/gin_command.h include/gin_config.h include/gin_connect.h include/gin_control.h include/gin_credential.h include/gin_fs.h include/gin_history.h include/gin_http.h include/gin_liveness.h include/gin_naming.h include/gin_phoenix.h include/gin_protocol.h include/gin_render.h include/gin_send.h include/gin_session.h include/gin_topology.h src/account_dispatch.h src/account_naming.h src/account_plan.h src/account_receive.h src/connect_control.h src/connect_values.h src/history_evidence.h src/history_values.h src/http_control.h src/json_evidence.h src/liveness_values.h src/render_values.h src/session_live_projection.h src/session_live_queue.h src/topology_internal.h

# Cold unit includes connect.c; the WSS driver uses the private API and links it.
ACCOUNT_OUTPUT_SUPPORT = src/account_dispatch.c $(ACCOUNT_DISPATCH_SUPPORT) src/account_sinks.c src/account_fs.c src/account_root.c src/checkpoint.c src/fs.c src/history_render.c
ACCOUNT_OUTPUT_HEADERS = $(ACCOUNT_DISPATCH_HEADERS) src/account_output.h src/account_sinks.h src/account_fs.h src/account_root.h src/checkpoint.h src/fs_private.h
ACCOUNT_HISTORY_BOOTSTRAP_SUPPORT = src/account_output.c $(ACCOUNT_OUTPUT_SUPPORT) src/history_recovery.c src/history_fetch.c src/history_path.c src/control.c src/send_reply.c
ACCOUNT_HISTORY_BOOTSTRAP_HEADERS = src/account_history_bootstrap.h src/send_values.h $(ACCOUNT_OUTPUT_HEADERS)
ACCOUNT_OWNER_SOURCES = src/account_owner.c src/account_history_bootstrap.c $(ACCOUNT_HISTORY_BOOTSTRAP_SUPPORT) src/input.c src/send.c src/send_payload.c
ACCOUNT_OWNER_HEADERS = src/account_owner.h include/gin_input.h include/gin_send.h $(ACCOUNT_HISTORY_BOOTSTRAP_HEADERS)
.build/test_connect_replan: include/gin_auth.h include/gin_command.h include/gin_connect.h include/gin_control.h include/gin_credential.h include/gin_fs.h include/gin_http.h include/gin_liveness.h include/gin_naming.h include/gin_phoenix.h include/gin_protocol.h include/gin_send.h include/gin_topology.h src/auth.c src/command.c src/connect.c src/connect_control.h src/connect_protocols.c src/connect_topic.c src/connect_values.h src/credential.c src/fs.c src/fs_private.h src/http.c src/http_control.h src/json_evidence.h src/liveness_backoff.c src/liveness_values.h src/naming.c src/phoenix.c src/phoenix_refs.c src/phoenix_stream.c src/protocol.c src/protocol_parse.c src/topology.c src/topology_internal.h src/topology_lookup.c tests/test_connect_replan.c src/connect_hot.h
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude -Isrc $(CURL_CFLAGS) $(JANSSON_CFLAGS) $(LDFLAGS) -o $@ tests/test_connect_replan.c src/fs.c src/liveness_backoff.c src/connect_protocols.c src/connect_topic.c src/http.c src/auth.c src/credential.c src/protocol.c src/protocol_parse.c src/phoenix.c src/phoenix_stream.c src/phoenix_refs.c src/command.c src/topology.c src/topology_lookup.c src/naming.c $(CURL_LIBS) $(JANSSON_LIBS) $(LDLIBS)

.build/connect_replan_wss_driver: include/gin_auth.h include/gin_command.h include/gin_connect.h include/gin_control.h include/gin_credential.h include/gin_fs.h include/gin_http.h include/gin_liveness.h include/gin_naming.h include/gin_phoenix.h include/gin_protocol.h include/gin_send.h include/gin_topology.h src/auth.c src/command.c src/connect.c src/connect_control.h src/connect_protocols.c src/connect_topic.c src/connect_values.h src/credential.c src/fs.c src/fs_private.h src/http.c src/http_control.h src/json_evidence.h src/liveness_backoff.c src/liveness_values.h src/naming.c src/phoenix.c src/phoenix_refs.c src/phoenix_stream.c src/protocol.c src/protocol_parse.c src/topology.c src/topology_fetch.c src/topology_internal.h src/topology_lookup.c tests/connect_replan_wss_driver.c src/connect_hot.h
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude -Isrc $(CURL_CFLAGS) $(JANSSON_CFLAGS) $(LDFLAGS) -o $@ tests/connect_replan_wss_driver.c src/topology_fetch.c src/fs.c src/connect.c src/liveness_backoff.c src/connect_protocols.c src/connect_topic.c src/http.c src/auth.c src/credential.c src/protocol.c src/protocol_parse.c src/phoenix.c src/phoenix_stream.c src/phoenix_refs.c src/command.c src/topology.c src/topology_lookup.c src/naming.c $(CURL_LIBS) $(JANSSON_LIBS) $(LDLIBS)


.build/test_account_sinks: tests/test_account_sinks.c src/account_sinks.c $(ACCOUNT_SINKS_SUPPORT) $(ACCOUNT_SINKS_HEADERS)
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude -Isrc $(JANSSON_CFLAGS) $(LDFLAGS) -o $@ tests/test_account_sinks.c src/account_sinks.c $(ACCOUNT_SINKS_SUPPORT) $(JANSSON_LIBS) $(LDLIBS)

# The fault unit includes production and normal-test C; never link either twice.
.build/test_account_sinks_fault: tests/test_account_sinks_fault.c tests/test_account_sinks.c src/account_sinks.c $(ACCOUNT_SINKS_SUPPORT) $(ACCOUNT_SINKS_HEADERS)
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude -Isrc $(JANSSON_CFLAGS) $(LDFLAGS) -o $@ tests/test_account_sinks_fault.c $(ACCOUNT_SINKS_SUPPORT) $(JANSSON_LIBS) $(LDLIBS)

ACCOUNT_ROOT_SUPPORT = src/credential.c src/fs.c src/naming.c
ACCOUNT_ROOT_HEADERS = src/account_root.h include/gin_credential.h $(FS_HEADERS) src/json_evidence.h
ACCOUNT_AUTH_SUPPORT = src/auth.c src/http.c $(ACCOUNT_ROOT_SUPPORT)
ACCOUNT_AUTH_HEADERS = $(HTTP_CONTROL_HEADERS) $(FS_HEADERS) src/json_evidence.h
INTERACTIVE_LOGIN_SUPPORT = src/terminal_password.c src/protocol.c src/protocol_parse.c $(ACCOUNT_AUTH_SUPPORT)
INTERACTIVE_LOGIN_HEADERS = src/interactive_login.h src/terminal_password.h $(ACCOUNT_AUTH_HEADERS)

HISTORY_HEADERS = $(HTTP_CONTROL_HEADERS) include/gin_history.h src/history_values.h src/history_evidence.h include/gin_render.h
HISTORY_PAGE_SUPPORT = src/naming.c src/connect_topic.c src/phoenix.c src/http.c
HISTORY_TARGETS = .build/test_history_page .build/test_history_owned .build/test_history_live .build/test_history_recovery .build/test_history_path .build/test_history_evidence .build/test_render_plain .build/history_http_driver .build/history_driver .build/history_control_driver

CONNECT_HEADERS = src/connect_hot.h src/connect_control.h $(HTTP_CONTROL_HEADERS) include/gin_connect.h include/gin_liveness.h src/liveness_values.h include/gin_command.h include/gin_topology.h src/topology_internal.h include/gin_naming.h src/connect_values.h
CONNECT_QUERY_SOURCES = src/command.c src/topology.c src/topology_lookup.c
CONNECT_SUPPORT_SOURCES = src/liveness_backoff.c src/connect_protocols.c src/connect_topic.c src/http.c src/auth.c src/credential.c src/protocol.c src/protocol_parse.c src/phoenix.c src/phoenix_stream.c src/phoenix_refs.c $(CONNECT_QUERY_SOURCES) src/naming.c
CONNECT_SOURCES = src/connect.c $(CONNECT_SUPPORT_SOURCES)

SESSION_LIVE_SOURCES = src/session_live_queue.c src/session_live_projection.c

.build/test_account_receive_control: tests/test_account_receive_control.c src/account_receive.c $(ACCOUNT_RECEIVE_SUPPORT) $(ACCOUNT_RECEIVE_HEADERS)
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude -Isrc $(CURL_CFLAGS) $(JANSSON_CFLAGS) $(LDFLAGS) -o $@ tests/test_account_receive_control.c $(ACCOUNT_RECEIVE_SUPPORT) $(CURL_LIBS) $(JANSSON_LIBS) $(LDLIBS)

.build/account_receive_driver: tests/account_receive_driver.c src/account_receive.c $(ACCOUNT_RECEIVE_SUPPORT) $(ACCOUNT_RECEIVE_HEADERS)
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude -Isrc $(CURL_CFLAGS) $(JANSSON_CFLAGS) $(LDFLAGS) -o $@ tests/account_receive_driver.c src/account_receive.c $(ACCOUNT_RECEIVE_SUPPORT) $(CURL_LIBS) $(JANSSON_LIBS) $(LDLIBS)

# The owner driver links both production coordinators normally.
.build/account_owner_driver: tests/account_owner_driver.c $(ACCOUNT_OWNER_SOURCES) $(ACCOUNT_OWNER_HEADERS)
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude -Isrc $(CURL_CFLAGS) $(JANSSON_CFLAGS) $(LDFLAGS) -o $@ tests/account_owner_driver.c $(ACCOUNT_OWNER_SOURCES) $(CURL_LIBS) $(JANSSON_LIBS) $(LDLIBS)

# The private bootstrap driver includes the coordinator C; do not link it twice.
.build/account_history_bootstrap_driver: tests/account_history_bootstrap_driver.c src/account_history_bootstrap.c $(ACCOUNT_HISTORY_BOOTSTRAP_SUPPORT) $(ACCOUNT_HISTORY_BOOTSTRAP_HEADERS)
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude -Isrc $(CURL_CFLAGS) $(JANSSON_CFLAGS) $(LDFLAGS) -o $@ tests/account_history_bootstrap_driver.c $(ACCOUNT_HISTORY_BOOTSTRAP_SUPPORT) $(CURL_LIBS) $(JANSSON_LIBS) $(LDLIBS)

.build/account_history_output_driver: tests/account_history_output_driver.c src/account_output.c $(ACCOUNT_OUTPUT_SUPPORT) src/history_recovery.c $(ACCOUNT_OUTPUT_HEADERS)
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude -Isrc $(CURL_CFLAGS) $(JANSSON_CFLAGS) $(LDFLAGS) -o $@ tests/account_history_output_driver.c src/account_output.c $(ACCOUNT_OUTPUT_SUPPORT) src/history_recovery.c $(CURL_LIBS) $(JANSSON_LIBS) $(LDLIBS)

.build/account_output_driver: tests/account_output_driver.c src/account_output.c $(ACCOUNT_OUTPUT_SUPPORT) $(ACCOUNT_OUTPUT_HEADERS)
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude -Isrc $(CURL_CFLAGS) $(JANSSON_CFLAGS) $(LDFLAGS) -o $@ tests/account_output_driver.c src/account_output.c $(ACCOUNT_OUTPUT_SUPPORT) $(CURL_LIBS) $(JANSSON_LIBS) $(LDLIBS)

.build/account_dispatch_driver: tests/account_dispatch_driver.c src/account_dispatch.c $(ACCOUNT_DISPATCH_SUPPORT) $(ACCOUNT_DISPATCH_HEADERS)
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude -Isrc $(CURL_CFLAGS) $(JANSSON_CFLAGS) $(LDFLAGS) -o $@ tests/account_dispatch_driver.c src/account_dispatch.c $(ACCOUNT_DISPATCH_SUPPORT) $(CURL_LIBS) $(JANSSON_LIBS) $(LDLIBS)

# Fidelity includes both production dispatch and its driver; do not link either.
.build/test_account_dispatch_fidelity: tests/test_account_dispatch_fidelity.c tests/account_dispatch_driver.c src/account_dispatch.c $(ACCOUNT_DISPATCH_SUPPORT) $(ACCOUNT_DISPATCH_HEADERS)
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude -Isrc $(CURL_CFLAGS) $(JANSSON_CFLAGS) $(LDFLAGS) -o $@ tests/test_account_dispatch_fidelity.c $(ACCOUNT_DISPATCH_SUPPORT) $(CURL_LIBS) $(JANSSON_LIBS) $(LDLIBS)
SESSION_SOURCES = src/session.c src/session_config.c src/session_operation.c $(SESSION_LIVE_SOURCES) src/checkpoint.c
SESSION_OUTBOUND_SOURCES = src/send.c src/send_payload.c src/send_reply.c src/control.c
SESSION_FAULT_SOURCES = $(SESSION_SOURCES:src/session.c=tests/session_owner_fault_driver.c)
SESSION_HEADERS = src/connect_hot.h $(FS_HEADERS) $(HISTORY_HEADERS) src/checkpoint.h src/connect_control.h $(HTTP_CONTROL_HEADERS) src/liveness_values.h include/gin_liveness.h include/gin_control.h src/session_operation.h src/render_values.h src/send_values.h include/gin_input.h include/gin_command.h include/gin_send.h src/session_live_queue.h src/session_live_projection.h src/json_evidence.h src/history_evidence.h include/gin_render.h include/gin_session.h include/gin_config.h include/gin_http.h include/gin_fs.h include/gin_credential.h include/gin_auth.h include/gin_protocol.h include/gin_topology.h include/gin_connect.h include/gin_phoenix.h
SESSION_OWNER_SUPPORT = src/input.c src/session_operation.c $(SESSION_OUTBOUND_SOURCES) src/session_config.c $(SESSION_LIVE_SOURCES) $(RENDER_SOURCES) $(HISTORY_SOURCES) src/checkpoint.c src/fs.c src/topology_fetch.c $(CONNECT_SOURCES)

.build/test_session_owner: tests/test_session_owner.c $(SESSION_SOURCES) $(SESSION_OWNER_SUPPORT) $(SESSION_HEADERS) $(CONNECT_HEADERS)
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude -Isrc $(CURL_CFLAGS) $(JANSSON_CFLAGS) $(LDFLAGS) -o $@ tests/test_session_owner.c src/session.c $(SESSION_OWNER_SUPPORT) $(CURL_LIBS) $(JANSSON_LIBS) $(LDLIBS)

# The white-box fixture includes the production owner; never link it twice.
.build/test_session_owner_whitebox: tests/test_session_owner.c $(SESSION_SOURCES) $(SESSION_OWNER_SUPPORT) $(SESSION_HEADERS) $(CONNECT_HEADERS)
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude -Isrc $(CURL_CFLAGS) $(JANSSON_CFLAGS) $(LDFLAGS) -DOWNER_WHITEBOX -o $@ tests/test_session_owner.c $(SESSION_OWNER_SUPPORT) $(CURL_LIBS) $(JANSSON_LIBS) $(LDLIBS)

.build/test_session_config: tests/test_session_config.c src/session_config.c src/fs.c src/naming.c src/http.c $(SESSION_HEADERS) $(CONTROL_HEADERS)
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude $(CURL_CFLAGS) $(LDFLAGS) -o $@ tests/test_session_config.c src/session_config.c src/fs.c src/naming.c src/http.c $(CURL_LIBS) $(LDLIBS)


.build/gin: $(SESSION_SOURCES) $(SESSION_HEADERS) $(HISTORY_SOURCES) $(HISTORY_HEADERS) include/gin_command.h include/gin_control.h src/command.c src/control.c include/gin_send.h src/send_values.h include/gin_render.h src/render_values.h src/gin.c src/send.c src/send_payload.c src/send_reply.c src/render.c src/render_escape.c src/render_time.c src/fs.c src/config.c src/naming.c src/input.c src/runtime.c src/http.c src/auth.c src/protocol.c src/protocol_parse.c src/credential.c src/topology.c src/topology_fetch.c src/topology_lookup.c src/connect.c src/liveness_backoff.c src/connect_protocols.c src/connect_topic.c include/gin_connect.h include/gin_liveness.h src/liveness_values.h src/connect_values.h src/phoenix.c src/phoenix_stream.c src/phoenix_refs.c include/gin_phoenix.h src/topology_internal.h include/gin_topology.h include/gin_protocol.h include/gin_auth.h include/gin_credential.h include/gin_http.h include/gin_runtime.h include/gin_input.h include/gin_fs.h include/gin_config.h include/gin_naming.h $(CONTROL_HEADERS) src/connect_hot.h
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude $(CURL_CFLAGS) $(JANSSON_CFLAGS) $(LDFLAGS) -o $@ src/gin.c $(SESSION_SOURCES) $(HISTORY_SOURCES) src/command.c src/control.c src/send.c src/send_payload.c src/send_reply.c src/render.c src/render_escape.c src/render_time.c src/fs.c src/config.c src/naming.c src/input.c src/runtime.c src/http.c src/auth.c src/protocol.c src/protocol_parse.c src/credential.c src/topology.c src/topology_fetch.c src/topology_lookup.c src/phoenix.c src/phoenix_stream.c src/phoenix_refs.c src/connect.c src/liveness_backoff.c src/connect_protocols.c src/connect_topic.c src/account_owner.c src/account_history_bootstrap.c src/account_output.c src/account_dispatch.c src/account_receive.c src/account_plan.c src/account_naming.c src/account_sinks.c src/account_registry.c src/account_fs.c src/account_root.c src/account_cli.c src/account_discovery.c src/interactive_login.c src/terminal_password.c $(CURL_LIBS) $(JANSSON_LIBS) $(LDLIBS)

.build/gin_cleanup_fault: $(SESSION_SOURCES) $(SESSION_HEADERS) $(HISTORY_SOURCES) $(HISTORY_HEADERS) include/gin_command.h include/gin_control.h src/command.c src/control.c include/gin_send.h src/send_values.h include/gin_render.h src/render_values.h src/gin.c src/send.c src/send_payload.c src/send_reply.c src/render.c src/render_escape.c src/render_time.c src/fs.c src/config.c src/naming.c src/input.c src/runtime.c src/http.c src/auth.c src/protocol.c src/protocol_parse.c src/credential.c src/topology.c src/topology_fetch.c src/topology_lookup.c src/connect.c src/liveness_backoff.c src/connect_protocols.c src/connect_topic.c include/gin_connect.h include/gin_liveness.h src/liveness_values.h src/connect_values.h src/phoenix.c src/phoenix_stream.c src/phoenix_refs.c include/gin_phoenix.h src/topology_internal.h include/gin_topology.h include/gin_protocol.h include/gin_auth.h include/gin_credential.h include/gin_http.h include/gin_runtime.h include/gin_input.h include/gin_fs.h include/gin_config.h include/gin_naming.h $(CONTROL_HEADERS) tests/connect_cleanup_fault_driver.c src/connect_hot.h
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude $(CURL_CFLAGS) $(JANSSON_CFLAGS) $(LDFLAGS) -o $@ src/gin.c $(SESSION_SOURCES) $(HISTORY_SOURCES) src/command.c src/control.c src/send.c src/send_payload.c src/send_reply.c src/render.c src/render_escape.c src/render_time.c src/fs.c src/config.c src/naming.c src/input.c src/runtime.c src/http.c src/auth.c src/protocol.c src/protocol_parse.c src/credential.c src/topology.c src/topology_fetch.c src/topology_lookup.c src/phoenix.c src/phoenix_stream.c src/phoenix_refs.c tests/connect_cleanup_fault_driver.c src/liveness_backoff.c src/connect_protocols.c src/connect_topic.c src/account_owner.c src/account_history_bootstrap.c src/account_output.c src/account_dispatch.c src/account_receive.c src/account_plan.c src/account_naming.c src/account_sinks.c src/account_registry.c src/account_fs.c src/account_root.c src/account_cli.c src/account_discovery.c src/interactive_login.c src/terminal_password.c $(CURL_LIBS) $(JANSSON_LIBS) $(LDLIBS)

.build/gin_receive_observer: $(SESSION_SOURCES) $(SESSION_HEADERS) $(HISTORY_SOURCES) $(HISTORY_HEADERS) include/gin_command.h include/gin_control.h src/command.c src/control.c include/gin_send.h src/send_values.h include/gin_render.h src/render_values.h src/gin.c src/send.c src/send_payload.c src/send_reply.c src/render.c src/render_escape.c src/render_time.c src/fs.c src/config.c src/naming.c src/input.c src/runtime.c src/http.c src/auth.c src/protocol.c src/protocol_parse.c src/credential.c src/topology.c src/topology_fetch.c src/topology_lookup.c src/connect.c src/liveness_backoff.c src/connect_protocols.c src/connect_topic.c include/gin_connect.h include/gin_liveness.h src/liveness_values.h src/connect_values.h src/phoenix.c src/phoenix_stream.c src/phoenix_refs.c include/gin_phoenix.h src/topology_internal.h include/gin_topology.h include/gin_protocol.h include/gin_auth.h include/gin_credential.h include/gin_http.h include/gin_runtime.h include/gin_input.h include/gin_fs.h include/gin_config.h include/gin_naming.h tests/connect_receive_observer.c $(CONTROL_HEADERS) src/connect_hot.h
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude $(CURL_CFLAGS) $(JANSSON_CFLAGS) $(LDFLAGS) -o $@ src/gin.c $(SESSION_SOURCES) $(HISTORY_SOURCES) src/command.c src/control.c src/send.c src/send_payload.c src/send_reply.c src/render.c src/render_escape.c src/render_time.c src/fs.c src/config.c src/naming.c src/input.c src/runtime.c src/http.c src/auth.c src/protocol.c src/protocol_parse.c src/credential.c src/topology.c src/topology_fetch.c src/topology_lookup.c src/phoenix.c src/phoenix_stream.c src/phoenix_refs.c tests/connect_receive_observer.c src/liveness_backoff.c src/connect_protocols.c src/connect_topic.c src/account_owner.c src/account_history_bootstrap.c src/account_output.c src/account_dispatch.c src/account_receive.c src/account_plan.c src/account_naming.c src/account_sinks.c src/account_registry.c src/account_fs.c src/account_root.c src/account_cli.c src/account_discovery.c src/interactive_login.c src/terminal_password.c $(CURL_LIBS) $(JANSSON_LIBS) $(LDLIBS)

.build/gin_owner_fault: $(SESSION_SOURCES) $(SESSION_HEADERS) $(HISTORY_SOURCES) $(HISTORY_HEADERS) include/gin_command.h include/gin_control.h src/command.c src/control.c include/gin_send.h src/send_values.h include/gin_render.h src/render_values.h src/gin.c src/send.c src/send_payload.c src/send_reply.c src/render.c src/render_escape.c src/render_time.c src/fs.c src/config.c src/naming.c src/input.c src/runtime.c src/http.c src/auth.c src/protocol.c src/protocol_parse.c src/credential.c src/topology.c src/topology_fetch.c src/topology_lookup.c src/connect.c src/liveness_backoff.c src/connect_protocols.c src/connect_topic.c include/gin_connect.h include/gin_liveness.h src/liveness_values.h src/connect_values.h src/phoenix.c src/phoenix_stream.c src/phoenix_refs.c include/gin_phoenix.h src/topology_internal.h include/gin_topology.h include/gin_protocol.h include/gin_auth.h include/gin_credential.h include/gin_http.h include/gin_runtime.h include/gin_input.h include/gin_fs.h include/gin_config.h include/gin_naming.h tests/session_owner_fault_driver.c $(CONTROL_HEADERS) src/connect_hot.h
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude $(CURL_CFLAGS) $(JANSSON_CFLAGS) $(LDFLAGS) -o $@ src/gin.c $(SESSION_FAULT_SOURCES) $(HISTORY_SOURCES) src/command.c src/control.c src/send.c src/send_payload.c src/send_reply.c src/render.c src/render_escape.c src/render_time.c src/fs.c src/config.c src/naming.c src/input.c src/runtime.c src/http.c src/auth.c src/protocol.c src/protocol_parse.c src/credential.c src/topology.c src/topology_fetch.c src/topology_lookup.c src/phoenix.c src/phoenix_stream.c src/phoenix_refs.c src/connect.c src/liveness_backoff.c src/connect_protocols.c src/connect_topic.c src/account_owner.c src/account_history_bootstrap.c src/account_output.c src/account_dispatch.c src/account_receive.c src/account_plan.c src/account_naming.c src/account_sinks.c src/account_registry.c src/account_fs.c src/account_root.c src/account_cli.c src/account_discovery.c src/interactive_login.c src/terminal_password.c $(CURL_LIBS) $(JANSSON_LIBS) $(LDLIBS)

.build/test_account_naming: tests/test_account_naming.c src/account_naming.c src/account_naming.h src/naming.c include/gin_naming.h
	mkdir -p .build
	$(CC) $(CPPFLAGS) $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude -Isrc $(LDFLAGS) -o $@ tests/test_account_naming.c src/account_naming.c src/naming.c $(LDLIBS)

.build/account_fs_driver: tests/account_fs_driver.c src/account_fs.c src/account_fs.h src/account_naming.c src/account_naming.h src/fs.c src/naming.c $(FS_HEADERS)
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude -Isrc $(LDFLAGS) -o $@ tests/account_fs_driver.c src/account_fs.c src/account_naming.c src/fs.c src/naming.c $(LDLIBS)

.build/terminal_password_driver: tests/terminal_password_driver.c src/terminal_password.c src/terminal_password.h
	mkdir -p .build
	$(CC) $(CPPFLAGS) $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Isrc $(LDFLAGS) -o $@ tests/terminal_password_driver.c src/terminal_password.c $(LDLIBS)

# Private fault translation unit includes the native reader; do not link it twice.
.build/terminal_password_fault_driver: tests/terminal_password_fault_driver.c src/terminal_password.c src/terminal_password.h
	mkdir -p .build
	$(CC) $(CPPFLAGS) $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Isrc $(LDFLAGS) -o $@ tests/terminal_password_fault_driver.c $(LDLIBS)

.build/test_account_root: tests/test_account_root.c src/account_root.c $(ACCOUNT_ROOT_SUPPORT) $(ACCOUNT_ROOT_HEADERS)
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude -Isrc $(JANSSON_CFLAGS) $(LDFLAGS) -o $@ tests/test_account_root.c src/account_root.c $(ACCOUNT_ROOT_SUPPORT) $(JANSSON_LIBS) $(LDLIBS)

# The private fault unit includes account_root.c; never link it twice.
.build/test_account_root_fault: tests/test_account_root_fault.c src/account_root.c $(ACCOUNT_ROOT_SUPPORT) $(ACCOUNT_ROOT_HEADERS)
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude -Isrc $(JANSSON_CFLAGS) $(LDFLAGS) -o $@ tests/test_account_root_fault.c $(ACCOUNT_ROOT_SUPPORT) $(JANSSON_LIBS) $(LDLIBS)

.build/auth_login_control_driver: tests/auth_login_control_driver.c $(ACCOUNT_AUTH_SUPPORT) $(ACCOUNT_AUTH_HEADERS)
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude -Isrc $(CURL_CFLAGS) $(JANSSON_CFLAGS) $(LDFLAGS) -o $@ tests/auth_login_control_driver.c $(ACCOUNT_AUTH_SUPPORT) $(CURL_LIBS) $(JANSSON_LIBS) $(LDLIBS)

# The white-box driver includes interactive_login.c exactly once.
.build/interactive_login_driver: tests/interactive_login_driver.c src/interactive_login.c $(INTERACTIVE_LOGIN_SUPPORT) $(INTERACTIVE_LOGIN_HEADERS)
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude -Isrc $(CURL_CFLAGS) $(JANSSON_CFLAGS) $(LDFLAGS) -o $@ tests/interactive_login_driver.c $(INTERACTIVE_LOGIN_SUPPORT) $(CURL_LIBS) $(JANSSON_LIBS) $(LDLIBS)

ACCOUNT_PLAN_SUPPORT = src/account_naming.c $(CONNECT_SOURCES)
ACCOUNT_PLAN_HEADERS = include/gin_auth.h include/gin_command.h include/gin_connect.h include/gin_control.h include/gin_credential.h include/gin_fs.h include/gin_http.h include/gin_liveness.h include/gin_naming.h include/gin_phoenix.h include/gin_protocol.h include/gin_send.h include/gin_topology.h src/account_naming.h src/account_plan.h src/connect_control.h src/connect_values.h src/http_control.h src/json_evidence.h src/liveness_values.h src/topology_internal.h

.build/test_account_plan: tests/test_account_plan.c src/account_plan.c $(ACCOUNT_PLAN_SUPPORT) $(ACCOUNT_PLAN_HEADERS)
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude -Isrc $(CURL_CFLAGS) $(JANSSON_CFLAGS) $(LDFLAGS) -o $@ tests/test_account_plan.c src/account_plan.c $(ACCOUNT_PLAN_SUPPORT) $(CURL_LIBS) $(JANSSON_LIBS) $(LDLIBS)

# The private fault unit includes account_plan.c; never link it twice.
.build/test_account_plan_fault: tests/test_account_plan.c src/account_plan.c $(ACCOUNT_PLAN_SUPPORT) $(ACCOUNT_PLAN_HEADERS)
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude -Isrc $(CURL_CFLAGS) $(JANSSON_CFLAGS) $(LDFLAGS) -DACCOUNT_PLAN_ALLOC_FAULT -o $@ tests/test_account_plan.c $(ACCOUNT_PLAN_SUPPORT) $(CURL_LIBS) $(JANSSON_LIBS) $(LDLIBS)

# The private queue fixture includes session_live_queue.c exactly once.
.build/test_account_live_queue: include/gin_phoenix.h src/session_live_queue.c src/session_live_queue.h tests/test_account_live_queue.c
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude -Isrc $(LDFLAGS) -o $@ tests/test_account_live_queue.c $(LDLIBS)

ACCOUNT_DISCOVERY_SUPPORT = $(CONNECT_SOURCES) src/topology_fetch.c src/session_live_queue.c
ACCOUNT_DISCOVERY_HEADERS = $(CONNECT_HEADERS) src/account_discovery.h src/session_live_queue.h src/json_evidence.h include/gin_fs.h include/gin_phoenix.h

# The private driver includes account_discovery.c; never link it separately.
.build/account_discovery_driver: tests/account_discovery_driver.c src/account_discovery.c $(ACCOUNT_DISCOVERY_SUPPORT) $(ACCOUNT_DISCOVERY_HEADERS)
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude -Isrc $(CURL_CFLAGS) $(JANSSON_CFLAGS) $(LDFLAGS) -o $@ tests/account_discovery_driver.c $(ACCOUNT_DISCOVERY_SUPPORT) $(CURL_LIBS) $(JANSSON_LIBS) $(LDLIBS)

check-account-dispatch-inventory: .build/account_dispatch_driver
	GIN_ACCOUNT_DISPATCH_DRIVER=.build/account_dispatch_driver GIN_DISPATCH_CASE=inventory_add $(PYTHON) tests/test_account_dispatch.py
	GIN_ACCOUNT_DISPATCH_DRIVER=.build/account_dispatch_driver GIN_DISPATCH_CASE=inventory_same $(PYTHON) tests/test_account_dispatch.py
	GIN_ACCOUNT_DISPATCH_DRIVER=.build/account_dispatch_driver GIN_DISPATCH_CASE=inventory_wrong_event $(PYTHON) tests/test_account_dispatch.py
	GIN_ACCOUNT_DISPATCH_DRIVER=.build/account_dispatch_driver GIN_DISPATCH_CASE=inventory_wrong_source $(PYTHON) tests/test_account_dispatch.py
	GIN_ACCOUNT_DISPATCH_DRIVER=.build/account_dispatch_driver GIN_DISPATCH_CASE=inventory_malformed $(PYTHON) tests/test_account_dispatch.py
	GIN_ACCOUNT_DISPATCH_DRIVER=.build/account_dispatch_driver GIN_DISPATCH_CASE=inventory_unknown_network $(PYTHON) tests/test_account_dispatch.py
	GIN_ACCOUNT_DISPATCH_DRIVER=.build/account_dispatch_driver GIN_DISPATCH_CASE=inventory_cap $(PYTHON) tests/test_account_dispatch.py
	GIN_ACCOUNT_DISPATCH_DRIVER=.build/account_dispatch_driver GIN_DISPATCH_CASE=inventory_encrypted $(PYTHON) tests/test_account_dispatch.py

check-account-components: $(ACCOUNT_COMPONENT_TARGETS)
	.build/test_credential_identity
	.build/test_account_root_hot_fault
	.build/test_account_fs_hot_fault
	HOT_FAULT= .build/test_account_sinks_hot
	HOT_FAULT=missing .build/test_account_sinks_hot
	HOT_FAULT=mismatch .build/test_account_sinks_hot
	HOT_FAULT=symlink .build/test_account_sinks_hot
	HOT_FAULT=postpair .build/test_account_sinks_hot
	HOT_FAULT=taint .build/test_account_sinks_hot
	.build/test_account_sinks_hot_bind
	.build/test_account_sinks_hot_status
	.build/test_account_output_hot
	.build/test_account_dispatch_hot
	GIN_ACCOUNT_DISPATCH_HOT_DRIVER=$${PWD}/.build/account_dispatch_hot_wss_driver GIN_ACCOUNT_DISPATCH_HOT_RECEIPT=.build/account_dispatch_hot_receipt.json $(PYTHON) -m unittest discover -s tests -p test_account_dispatch_hot_wss.py -v
	.build/test_account_receive_hot
	GIN_ACCOUNT_HOT_DRIVER=$${PWD}/.build/account_receive_hot_wss_driver $(PYTHON) -m unittest discover -s tests -p test_account_receive_hot_wss.py -v
	.build/test_account_registry
	.build/test_status_projection
	.build/test_status_projection_fidelity
	.build/test_status_render
	.build/test_connect_replan
	GIN_REPLAN_DRIVER=.build/connect_replan_wss_driver $(PYTHON) tests/test_connect_replan_wss.py
	.build/test_account_sinks
	.build/test_account_sinks_fault
	.build/test_account_naming
	.build/test_account_plan
	.build/test_account_plan_fault
	.build/test_account_live_queue
	.build/test_account_root
	.build/test_account_root_fault
	GIN_LOGIN_CONTROL_BINARY=.build/auth_login_control_driver $(PYTHON) tests/test_auth_login_control.py
	INTERACTIVE_DRIVER=.build/interactive_login_driver $(PYTHON) tests/test_interactive_login.py
	GIN_DISCOVERY_DRIVER=.build/account_discovery_driver $(PYTHON) tests/test_account_discovery.py
	.build/test_account_checkpoint
	.build/test_account_network_checkpoint
	TP_DRIVER=.build/terminal_password_driver TP_FAULT_DRIVER=.build/terminal_password_fault_driver $(PYTHON) tests/test_terminal_password.py
	DRIVER=.build/account_fs_driver $(PYTHON) tests/test_account_fs.py
	GIN_ACCOUNT_RECEIVE_DRIVER=.build/account_receive_driver GIN_RECEIVE_CASE=positive $(PYTHON) tests/test_account_receive.py
	GIN_ACCOUNT_RECEIVE_DRIVER=.build/account_receive_driver GIN_RECEIVE_CASE=wrong_uuid $(PYTHON) tests/test_account_receive.py
	GIN_ACCOUNT_RECEIVE_DRIVER=.build/account_receive_driver GIN_RECEIVE_CASE=overflow $(PYTHON) tests/test_account_receive.py
	GIN_ACCOUNT_RECEIVE_DRIVER=.build/account_receive_driver GIN_RECEIVE_CASE=bytes $(PYTHON) tests/test_account_receive.py
	GIN_ACCOUNT_DISPATCH_DRIVER=.build/account_dispatch_driver GIN_DISPATCH_CASE=positive $(PYTHON) tests/test_account_dispatch.py
	GIN_ACCOUNT_DISPATCH_DRIVER=.build/account_dispatch_driver GIN_DISPATCH_CASE=wrong_event_query $(PYTHON) tests/test_account_dispatch.py
	GIN_ACCOUNT_DISPATCH_DRIVER=.build/account_dispatch_driver GIN_DISPATCH_CASE=wrong_event_nick $(PYTHON) tests/test_account_dispatch.py
	GIN_ACCOUNT_DISPATCH_DRIVER=.build/account_dispatch_driver GIN_DISPATCH_CASE=life_joined $(PYTHON) tests/test_account_dispatch.py
	GIN_ACCOUNT_DISPATCH_DRIVER=.build/account_dispatch_driver GIN_DISPATCH_CASE=life_kicked $(PYTHON) tests/test_account_dispatch.py
	GIN_ACCOUNT_DISPATCH_DRIVER=.build/account_dispatch_driver GIN_DISPATCH_CASE=query_same $(PYTHON) tests/test_account_dispatch.py
	GIN_ACCOUNT_DISPATCH_DRIVER=.build/account_dispatch_driver GIN_DISPATCH_CASE=query_changed $(PYTHON) tests/test_account_dispatch.py
	GIN_ACCOUNT_DISPATCH_DRIVER=.build/account_dispatch_driver GIN_DISPATCH_CASE=nick_same $(PYTHON) tests/test_account_dispatch.py
	GIN_ACCOUNT_DISPATCH_DRIVER=.build/account_dispatch_driver GIN_DISPATCH_CASE=nick_changed $(PYTHON) tests/test_account_dispatch.py
	GIN_ACCOUNT_DISPATCH_DRIVER=.build/account_dispatch_driver GIN_DISPATCH_CASE=life_unknown $(PYTHON) tests/test_account_dispatch.py
	GIN_ACCOUNT_DISPATCH_DRIVER=.build/account_dispatch_driver GIN_DISPATCH_CASE=life_offscope $(PYTHON) tests/test_account_dispatch.py
	GIN_ACCOUNT_DISPATCH_DRIVER=.build/account_dispatch_driver GIN_DISPATCH_CASE=life_malformed $(PYTHON) tests/test_account_dispatch.py
	GIN_ACCOUNT_DISPATCH_DRIVER=.build/account_dispatch_driver GIN_DISPATCH_CASE=life_encrypted $(PYTHON) tests/test_account_dispatch.py
	GIN_ACCOUNT_DISPATCH_DRIVER=.build/account_dispatch_driver GIN_DISPATCH_CASE=diagnostic $(PYTHON) tests/test_account_dispatch.py
	GIN_ACCOUNT_DISPATCH_DRIVER=.build/account_dispatch_driver GIN_DISPATCH_CASE=channel_dm $(PYTHON) tests/test_account_dispatch.py
	GIN_ACCOUNT_DISPATCH_DRIVER=.build/account_dispatch_driver GIN_DISPATCH_CASE=wrong_source $(PYTHON) tests/test_account_dispatch.py
	GIN_ACCOUNT_DISPATCH_DRIVER=.build/account_dispatch_driver GIN_DISPATCH_CASE=wrong_network $(PYTHON) tests/test_account_dispatch.py
	GIN_ACCOUNT_DISPATCH_DRIVER=.build/account_dispatch_driver GIN_DISPATCH_CASE=unknown_dm $(PYTHON) tests/test_account_dispatch.py
	GIN_ACCOUNT_DISPATCH_DRIVER=.build/account_dispatch_driver GIN_DISPATCH_CASE=encrypted $(PYTHON) tests/test_account_dispatch.py
	GIN_ACCOUNT_DISPATCH_DRIVER=.build/account_dispatch_driver GIN_DISPATCH_CASE=inventory_add $(PYTHON) tests/test_account_dispatch.py
	GIN_ACCOUNT_DISPATCH_DRIVER=.build/account_dispatch_driver GIN_DISPATCH_CASE=inventory_same $(PYTHON) tests/test_account_dispatch.py
	GIN_ACCOUNT_DISPATCH_DRIVER=.build/account_dispatch_driver GIN_DISPATCH_CASE=inventory_wrong_event $(PYTHON) tests/test_account_dispatch.py
	GIN_ACCOUNT_DISPATCH_DRIVER=.build/account_dispatch_driver GIN_DISPATCH_CASE=inventory_wrong_source $(PYTHON) tests/test_account_dispatch.py
	GIN_ACCOUNT_DISPATCH_DRIVER=.build/account_dispatch_driver GIN_DISPATCH_CASE=inventory_malformed $(PYTHON) tests/test_account_dispatch.py
	GIN_ACCOUNT_DISPATCH_DRIVER=.build/account_dispatch_driver GIN_DISPATCH_CASE=inventory_unknown_network $(PYTHON) tests/test_account_dispatch.py
	GIN_ACCOUNT_DISPATCH_DRIVER=.build/account_dispatch_driver GIN_DISPATCH_CASE=inventory_cap $(PYTHON) tests/test_account_dispatch.py
	GIN_ACCOUNT_DISPATCH_DRIVER=.build/account_dispatch_driver GIN_DISPATCH_CASE=inventory_encrypted $(PYTHON) tests/test_account_dispatch.py
	GIN_ACCOUNT_DISPATCH_DRIVER=.build/test_account_dispatch_fidelity GIN_DISPATCH_CASE=positive $(PYTHON) tests/test_account_dispatch.py
	GIN_ACCOUNT_DISPATCH_DRIVER=.build/test_account_dispatch_fidelity GIN_DISPATCH_CASE=fidelity_kind $(PYTHON) tests/test_account_dispatch.py
	GIN_ACCOUNT_DISPATCH_DRIVER=.build/test_account_dispatch_fidelity GIN_DISPATCH_CASE=fidelity_peer $(PYTHON) tests/test_account_dispatch.py
	GIN_ACCOUNT_DISPATCH_DRIVER=.build/test_account_dispatch_fidelity GIN_DISPATCH_CASE=fidelity_id $(PYTHON) tests/test_account_dispatch.py
	GIN_ACCOUNT_OUTPUT_DRIVER=.build/account_output_driver $(PYTHON) tests/test_account_output.py
	GIN_ACCOUNT_HISTORY_OUTPUT_DRIVER=.build/account_history_output_driver $(PYTHON) tests/test_account_history_output.py
	GIN_ACCOUNT_HISTORY_BOOTSTRAP_DRIVER=.build/account_history_bootstrap_driver $(PYTHON) tests/test_account_history_bootstrap.py
	.build/account_history_bootstrap_driver --null-input
	GIN_ACCOUNT_OWNER_DRIVER=.build/account_owner_driver $(PYTHON) tests/test_account_owner.py
	.build/test_account_receive_control
	$(PYTHON) tests/test_account_cli.py
	GIN_ACCOUNT_CLI_BINARY="$${PWD}/.build/gin" $(PYTHON) -m unittest discover -s tests -p test_account_fifo_send.py -v
	GIN_FIFO_OBSERVER=1 GIN_ACCOUNT_CLI_BINARY="$${PWD}/.build/gin_account_fifo_observer" $(PYTHON) -m unittest discover -s tests -p test_account_fifo_send.py -v
	GIN_ACCOUNT_CLI_BINARY="$${PWD}/.build/gin" $(PYTHON) -m unittest discover -s tests -p test_account_network_fifo.py -v
	GIN_FIFO_OBSERVER=1 GIN_ACCOUNT_CLI_BINARY="$${PWD}/.build/gin_account_fifo_observer" $(PYTHON) -m unittest discover -s tests -p test_account_network_fifo.py -v

	.build/test_connect_hot
	.build/test_connect_hot_fault
	GIN_HOT_DRIVER=$${PWD}/.build/connect_hot_wss_driver $(PYTHON) -m unittest discover -s tests -p test_connect_hot_wss.py -v
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

.build/runtime_stderr.o: src/gin.c include/gin_runtime.h include/gin_config.h include/gin_fs.h $(SESSION_HEADERS)
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude -Dfputs=fixture_fputs -c src/gin.c -o $@

.build/runtime_stderr: tests/runtime_stderr.c .build/runtime_stderr.o src/runtime.c src/input.c src/fs.c src/config.c $(SESSION_SOURCES) $(RENDER_SOURCES) $(HISTORY_SOURCES) $(SESSION_HEADERS) $(CONNECT_SOURCES) $(CONNECT_HEADERS) src/topology_fetch.c $(SESSION_OUTBOUND_SOURCES) $(CONTROL_HEADERS)
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude $(CURL_CFLAGS) $(JANSSON_CFLAGS) $(LDFLAGS) -o $@ tests/runtime_stderr.c .build/runtime_stderr.o src/runtime.c src/input.c src/fs.c src/config.c $(SESSION_SOURCES) $(RENDER_SOURCES) $(HISTORY_SOURCES) $(CONNECT_SOURCES) src/topology_fetch.c $(SESSION_OUTBOUND_SOURCES) src/account_owner.c src/account_history_bootstrap.c src/account_output.c src/account_dispatch.c src/account_receive.c src/account_plan.c src/account_naming.c src/account_sinks.c src/account_registry.c src/account_fs.c src/account_root.c src/account_cli.c src/account_discovery.c src/interactive_login.c src/terminal_password.c $(CURL_LIBS) $(JANSSON_LIBS) $(LDLIBS)

.build/http_driver: tests/http_driver.c src/http.c include/gin_http.h $(CONTROL_HEADERS)
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude $(CURL_CFLAGS) $(LDFLAGS) -o $@ tests/http_driver.c src/http.c $(CURL_LIBS) $(LDLIBS)

.build/test_http_control: tests/test_http_control.c src/http.c $(CONTROL_HEADERS)
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude -Isrc $(CURL_CFLAGS) $(LDFLAGS) -o $@ tests/test_http_control.c src/http.c $(CURL_LIBS) $(LDLIBS)

.build/test_http_bounds: tests/test_http_bounds.c src/http.c include/gin_http.h $(CONTROL_HEADERS)
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude $(CURL_CFLAGS) $(LDFLAGS) -o $@ tests/test_http_bounds.c $(CURL_LIBS) $(LDLIBS)

.build/auth_driver: tests/auth_driver.c src/auth.c src/http.c src/credential.c src/fs.c src/naming.c include/gin_auth.h include/gin_credential.h include/gin_http.h src/json_evidence.h $(CONTROL_HEADERS)
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude $(CURL_CFLAGS) $(JANSSON_CFLAGS) $(LDFLAGS) -o $@ tests/auth_driver.c src/auth.c src/http.c src/credential.c src/fs.c src/naming.c $(CURL_LIBS) $(JANSSON_LIBS) $(LDLIBS)

.build/test_http_request: tests/test_http_request.c src/http.c include/gin_http.h $(CONTROL_HEADERS)
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude $(CURL_CFLAGS) $(LDFLAGS) -o $@ tests/test_http_request.c src/http.c $(CURL_LIBS) $(LDLIBS)

.build/test_auth_bounds: tests/test_auth_bounds.c src/auth.c src/http.c src/credential.c include/gin_auth.h src/json_evidence.h $(CONTROL_HEADERS)
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude $(CURL_CFLAGS) $(JANSSON_CFLAGS) $(LDFLAGS) -o $@ tests/test_auth_bounds.c src/auth.c src/http.c src/credential.c $(CURL_LIBS) $(JANSSON_LIBS) $(LDLIBS)

.build/credential_driver: tests/credential_driver.c src/credential.c src/fs.c src/naming.c include/gin_credential.h src/json_evidence.h
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude $(JANSSON_CFLAGS) $(LDFLAGS) -o $@ tests/credential_driver.c src/credential.c src/fs.c src/naming.c $(JANSSON_LIBS) $(LDLIBS)

.build/test_credential: tests/test_credential.c src/credential.c include/gin_credential.h src/json_evidence.h
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude $(JANSSON_CFLAGS) $(LDFLAGS) -o $@ tests/test_credential.c src/credential.c $(JANSSON_LIBS) $(LDLIBS)

.build/protocol_driver: tests/protocol_driver.c src/protocol.c src/protocol_parse.c src/auth.c src/http.c src/credential.c include/gin_protocol.h include/gin_auth.h include/gin_http.h include/gin_credential.h src/json_evidence.h $(CONTROL_HEADERS)
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude $(CURL_CFLAGS) $(JANSSON_CFLAGS) $(LDFLAGS) -o $@ tests/protocol_driver.c src/protocol.c src/protocol_parse.c src/auth.c src/http.c src/credential.c $(CURL_LIBS) $(JANSSON_LIBS) $(LDLIBS)

.build/test_protocol_parse: tests/test_protocol_parse.c src/protocol_parse.c include/gin_protocol.h src/json_evidence.h
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude $(JANSSON_CFLAGS) $(LDFLAGS) -o $@ tests/test_protocol_parse.c src/protocol_parse.c $(JANSSON_LIBS) $(LDLIBS)

.build/test_topology: src/json_evidence.h tests/test_topology.c src/topology.c src/topology_lookup.c src/topology_internal.h src/naming.c include/gin_topology.h include/gin_naming.h
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude $(JANSSON_CFLAGS) $(LDFLAGS) -o $@ tests/test_topology.c src/topology_lookup.c src/naming.c $(JANSSON_LIBS) $(LDLIBS)

# The lookup contract unit supplies its own read-only accessor facade.
.build/test_topology_lookup: tests/test_topology_lookup.c src/topology_lookup.c src/naming.c include/gin_topology.h include/gin_naming.h
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude $(LDFLAGS) -o $@ tests/test_topology_lookup.c src/topology_lookup.c src/naming.c $(LDLIBS)

.build/topology_driver: tests/topology_driver.c src/topology.c src/topology_lookup.c src/topology_fetch.c src/topology_internal.h src/protocol.c src/protocol_parse.c src/auth.c src/http.c src/credential.c src/naming.c include/gin_topology.h include/gin_protocol.h include/gin_auth.h include/gin_http.h include/gin_credential.h src/json_evidence.h $(CONTROL_HEADERS)
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude $(CURL_CFLAGS) $(JANSSON_CFLAGS) $(LDFLAGS) -o $@ tests/topology_driver.c src/topology.c src/topology_lookup.c src/topology_fetch.c src/protocol.c src/protocol_parse.c src/auth.c src/http.c src/credential.c src/naming.c $(CURL_LIBS) $(JANSSON_LIBS) $(LDLIBS)

# Core and real-stream units include one implementation for private fault injection.
.build/test_phoenix: tests/test_phoenix.c src/phoenix.c include/gin_phoenix.h src/json_evidence.h
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude $(JANSSON_CFLAGS) $(LDFLAGS) -o $@ tests/test_phoenix.c $(JANSSON_LIBS) $(LDLIBS)

.build/test_phoenix_stream: tests/test_phoenix_stream.c src/phoenix_stream.c src/phoenix.c include/gin_phoenix.h src/json_evidence.h
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude $(JANSSON_CFLAGS) $(LDFLAGS) -o $@ tests/test_phoenix_stream.c src/phoenix.c $(JANSSON_LIBS) $(LDLIBS)

# Collector contract facade supplies decode itself; do not link the real codec.
.build/test_phoenix_collect: tests/test_phoenix_collect.c src/phoenix_stream.c include/gin_phoenix.h
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude $(LDFLAGS) -o $@ tests/test_phoenix_collect.c src/phoenix_stream.c $(LDLIBS)

.build/test_phoenix_refs: tests/test_phoenix_refs.c src/phoenix_refs.c include/gin_phoenix.h
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude $(LDFLAGS) -o $@ tests/test_phoenix_refs.c src/phoenix_refs.c $(LDLIBS)

check: all $(ACCOUNT_COMPONENT_TARGETS) .build/test_checkpoint_fault .build/test_checkpoint .build/gin_cleanup_fault .build/test_http_control .build/gin_receive_observer .build/gin_owner_fault $(AUTHORITY_JSON_TARGETS) .build/test_session_operation .build/test_session_topology_fidelity .build/test_session_live_queue .build/test_session_live_queue_owned .build/test_live_json_fidelity .build/test_session_clock .build/test_connect_drain .build/test_session_config .build/test_session_owner .build/test_session_owner_whitebox $(HISTORY_TARGETS) .build/test_liveness_backoff .build/test_connect_liveness .build/liveness_driver .build/test_command .build/test_control .build/control_driver .build/test_connect_query .build/query_driver .build/test_send .build/test_send_payload .build/test_send_reply .build/send_driver .build/test_render .build/test_render_escape .build/test_render_time .build/test_render_output .build/render_driver .build/connect_driver .build/test_connect .build/test_connect_protocols .build/test_connect_topic .build/test_phoenix .build/test_phoenix_stream .build/test_phoenix_collect .build/test_phoenix_refs .build/test_topology .build/test_topology_lookup .build/topology_driver .build/protocol_driver .build/test_protocol_parse .build/test_auth_bounds .build/credential_driver .build/test_credential .build/auth_driver .build/test_http_request .build/http_driver .build/test_http_bounds .build/test_naming .build/lock_holder .build/fifo_consumer .build/test_input .build/runtime_consumer .build/test_runtime_api .build/runtime_boundaries .build/runtime_stderr
	.build/test_credential_identity
	.build/test_account_root_hot_fault
	.build/test_account_fs_hot_fault
	HOT_FAULT= .build/test_account_sinks_hot
	HOT_FAULT=missing .build/test_account_sinks_hot
	HOT_FAULT=mismatch .build/test_account_sinks_hot
	HOT_FAULT=symlink .build/test_account_sinks_hot
	HOT_FAULT=postpair .build/test_account_sinks_hot
	HOT_FAULT=taint .build/test_account_sinks_hot
	.build/test_account_sinks_hot_bind
	.build/test_account_sinks_hot_status
	.build/test_account_output_hot
	.build/test_account_registry
	.build/test_account_dispatch_hot
	GIN_ACCOUNT_DISPATCH_HOT_DRIVER=$${PWD}/.build/account_dispatch_hot_wss_driver GIN_ACCOUNT_DISPATCH_HOT_RECEIPT=.build/account_dispatch_hot_receipt.json $(PYTHON) -m unittest discover -s tests -p test_account_dispatch_hot_wss.py -v
	.build/test_account_receive_hot
	GIN_ACCOUNT_HOT_DRIVER=$${PWD}/.build/account_receive_hot_wss_driver $(PYTHON) -m unittest discover -s tests -p test_account_receive_hot_wss.py -v
	$(PYTHON) tests/test_make_dependencies.py "$(CHECK_MAKE)"
	.build/test_status_projection
	.build/test_status_projection_fidelity
	.build/test_status_render
	.build/test_connect_replan
	GIN_REPLAN_DRIVER=.build/connect_replan_wss_driver $(PYTHON) tests/test_connect_replan_wss.py
	.build/test_account_sinks
	.build/test_account_sinks_fault
	.build/test_account_naming
	.build/test_account_plan
	.build/test_account_plan_fault
	.build/test_account_live_queue
	.build/test_account_root
	.build/test_account_root_fault
	GIN_LOGIN_CONTROL_BINARY=.build/auth_login_control_driver $(PYTHON) tests/test_auth_login_control.py
	INTERACTIVE_DRIVER=.build/interactive_login_driver $(PYTHON) tests/test_interactive_login.py
	GIN_DISCOVERY_DRIVER=.build/account_discovery_driver $(PYTHON) tests/test_account_discovery.py
	.build/test_account_checkpoint
	.build/test_account_network_checkpoint
	TP_DRIVER=.build/terminal_password_driver TP_FAULT_DRIVER=.build/terminal_password_fault_driver $(PYTHON) tests/test_terminal_password.py
	DRIVER=.build/account_fs_driver $(PYTHON) tests/test_account_fs.py
	GIN_ACCOUNT_RECEIVE_DRIVER=.build/account_receive_driver GIN_RECEIVE_CASE=positive $(PYTHON) tests/test_account_receive.py
	GIN_ACCOUNT_RECEIVE_DRIVER=.build/account_receive_driver GIN_RECEIVE_CASE=wrong_uuid $(PYTHON) tests/test_account_receive.py
	GIN_ACCOUNT_RECEIVE_DRIVER=.build/account_receive_driver GIN_RECEIVE_CASE=overflow $(PYTHON) tests/test_account_receive.py
	GIN_ACCOUNT_RECEIVE_DRIVER=.build/account_receive_driver GIN_RECEIVE_CASE=bytes $(PYTHON) tests/test_account_receive.py
	GIN_ACCOUNT_DISPATCH_DRIVER=.build/account_dispatch_driver GIN_DISPATCH_CASE=positive $(PYTHON) tests/test_account_dispatch.py
	GIN_ACCOUNT_DISPATCH_DRIVER=.build/account_dispatch_driver GIN_DISPATCH_CASE=wrong_event_query $(PYTHON) tests/test_account_dispatch.py
	GIN_ACCOUNT_DISPATCH_DRIVER=.build/account_dispatch_driver GIN_DISPATCH_CASE=wrong_event_nick $(PYTHON) tests/test_account_dispatch.py
	GIN_ACCOUNT_DISPATCH_DRIVER=.build/account_dispatch_driver GIN_DISPATCH_CASE=life_joined $(PYTHON) tests/test_account_dispatch.py
	GIN_ACCOUNT_DISPATCH_DRIVER=.build/account_dispatch_driver GIN_DISPATCH_CASE=life_kicked $(PYTHON) tests/test_account_dispatch.py
	GIN_ACCOUNT_DISPATCH_DRIVER=.build/account_dispatch_driver GIN_DISPATCH_CASE=query_same $(PYTHON) tests/test_account_dispatch.py
	GIN_ACCOUNT_DISPATCH_DRIVER=.build/account_dispatch_driver GIN_DISPATCH_CASE=query_changed $(PYTHON) tests/test_account_dispatch.py
	GIN_ACCOUNT_DISPATCH_DRIVER=.build/account_dispatch_driver GIN_DISPATCH_CASE=nick_same $(PYTHON) tests/test_account_dispatch.py
	GIN_ACCOUNT_DISPATCH_DRIVER=.build/account_dispatch_driver GIN_DISPATCH_CASE=nick_changed $(PYTHON) tests/test_account_dispatch.py
	GIN_ACCOUNT_DISPATCH_DRIVER=.build/account_dispatch_driver GIN_DISPATCH_CASE=life_unknown $(PYTHON) tests/test_account_dispatch.py
	GIN_ACCOUNT_DISPATCH_DRIVER=.build/account_dispatch_driver GIN_DISPATCH_CASE=life_offscope $(PYTHON) tests/test_account_dispatch.py
	GIN_ACCOUNT_DISPATCH_DRIVER=.build/account_dispatch_driver GIN_DISPATCH_CASE=life_malformed $(PYTHON) tests/test_account_dispatch.py
	GIN_ACCOUNT_DISPATCH_DRIVER=.build/account_dispatch_driver GIN_DISPATCH_CASE=life_encrypted $(PYTHON) tests/test_account_dispatch.py
	GIN_ACCOUNT_DISPATCH_DRIVER=.build/account_dispatch_driver GIN_DISPATCH_CASE=diagnostic $(PYTHON) tests/test_account_dispatch.py
	GIN_ACCOUNT_DISPATCH_DRIVER=.build/account_dispatch_driver GIN_DISPATCH_CASE=channel_dm $(PYTHON) tests/test_account_dispatch.py
	GIN_ACCOUNT_DISPATCH_DRIVER=.build/account_dispatch_driver GIN_DISPATCH_CASE=wrong_source $(PYTHON) tests/test_account_dispatch.py
	GIN_ACCOUNT_DISPATCH_DRIVER=.build/account_dispatch_driver GIN_DISPATCH_CASE=wrong_network $(PYTHON) tests/test_account_dispatch.py
	GIN_ACCOUNT_DISPATCH_DRIVER=.build/account_dispatch_driver GIN_DISPATCH_CASE=unknown_dm $(PYTHON) tests/test_account_dispatch.py
	GIN_ACCOUNT_DISPATCH_DRIVER=.build/account_dispatch_driver GIN_DISPATCH_CASE=encrypted $(PYTHON) tests/test_account_dispatch.py
	GIN_ACCOUNT_DISPATCH_DRIVER=.build/account_dispatch_driver GIN_DISPATCH_CASE=inventory_add $(PYTHON) tests/test_account_dispatch.py
	GIN_ACCOUNT_DISPATCH_DRIVER=.build/account_dispatch_driver GIN_DISPATCH_CASE=inventory_same $(PYTHON) tests/test_account_dispatch.py
	GIN_ACCOUNT_DISPATCH_DRIVER=.build/account_dispatch_driver GIN_DISPATCH_CASE=inventory_wrong_event $(PYTHON) tests/test_account_dispatch.py
	GIN_ACCOUNT_DISPATCH_DRIVER=.build/account_dispatch_driver GIN_DISPATCH_CASE=inventory_wrong_source $(PYTHON) tests/test_account_dispatch.py
	GIN_ACCOUNT_DISPATCH_DRIVER=.build/account_dispatch_driver GIN_DISPATCH_CASE=inventory_malformed $(PYTHON) tests/test_account_dispatch.py
	GIN_ACCOUNT_DISPATCH_DRIVER=.build/account_dispatch_driver GIN_DISPATCH_CASE=inventory_unknown_network $(PYTHON) tests/test_account_dispatch.py
	GIN_ACCOUNT_DISPATCH_DRIVER=.build/account_dispatch_driver GIN_DISPATCH_CASE=inventory_cap $(PYTHON) tests/test_account_dispatch.py
	GIN_ACCOUNT_DISPATCH_DRIVER=.build/account_dispatch_driver GIN_DISPATCH_CASE=inventory_encrypted $(PYTHON) tests/test_account_dispatch.py
	GIN_ACCOUNT_DISPATCH_DRIVER=.build/test_account_dispatch_fidelity GIN_DISPATCH_CASE=positive $(PYTHON) tests/test_account_dispatch.py
	GIN_ACCOUNT_DISPATCH_DRIVER=.build/test_account_dispatch_fidelity GIN_DISPATCH_CASE=fidelity_kind $(PYTHON) tests/test_account_dispatch.py
	GIN_ACCOUNT_DISPATCH_DRIVER=.build/test_account_dispatch_fidelity GIN_DISPATCH_CASE=fidelity_peer $(PYTHON) tests/test_account_dispatch.py
	GIN_ACCOUNT_DISPATCH_DRIVER=.build/test_account_dispatch_fidelity GIN_DISPATCH_CASE=fidelity_id $(PYTHON) tests/test_account_dispatch.py
	GIN_ACCOUNT_OUTPUT_DRIVER=.build/account_output_driver $(PYTHON) tests/test_account_output.py
	GIN_ACCOUNT_HISTORY_OUTPUT_DRIVER=.build/account_history_output_driver $(PYTHON) tests/test_account_history_output.py
	GIN_ACCOUNT_HISTORY_BOOTSTRAP_DRIVER=.build/account_history_bootstrap_driver $(PYTHON) tests/test_account_history_bootstrap.py
	.build/account_history_bootstrap_driver --null-input
	GIN_ACCOUNT_OWNER_DRIVER=.build/account_owner_driver $(PYTHON) tests/test_account_owner.py
	.build/test_account_receive_control
	$(PYTHON) tests/test_account_cli.py
	GIN_ACCOUNT_CLI_BINARY="$${PWD}/.build/gin" $(PYTHON) -m unittest discover -s tests -p test_account_fifo_send.py -v
	GIN_FIFO_OBSERVER=1 GIN_ACCOUNT_CLI_BINARY="$${PWD}/.build/gin_account_fifo_observer" $(PYTHON) -m unittest discover -s tests -p test_account_fifo_send.py -v
	GIN_ACCOUNT_CLI_BINARY="$${PWD}/.build/gin" $(PYTHON) -m unittest discover -s tests -p test_account_network_fifo.py -v
	GIN_FIFO_OBSERVER=1 GIN_ACCOUNT_CLI_BINARY="$${PWD}/.build/gin_account_fifo_observer" $(PYTHON) -m unittest discover -s tests -p test_account_network_fifo.py -v
	.build/test_authority_json_config mutation
	.build/test_authority_json_config sweep
	.build/test_authority_json_identity mutation
	.build/test_authority_json_identity sweep
	.build/test_authority_json_store mutation
	.build/test_authority_json_store sweep
	.build/test_authority_json_connect ack-mutation
	.build/test_authority_json_connect ack-sweep
	.build/test_authority_json_connect ack-null
	.build/test_authority_json_connect severance-mutation
	.build/test_authority_json_connect severance-code-mutation
	.build/test_authority_json_connect severance-null
	.build/test_authority_json_connect severance-sweep
	.build/test_session_operation
	.build/test_session_topology_fidelity
	.build/test_session_live_queue
	.build/test_session_live_queue_owned
	.build/test_live_json_fidelity codec
	.build/test_live_json_fidelity render
	.build/test_session_clock
	.build/test_checkpoint
	.build/test_checkpoint_fault pre
	.build/test_checkpoint_fault post
	.build/test_history_page
	.build/test_history_owned
	.build/test_history_live
	.build/test_history_recovery
	.build/test_history_path
	.build/test_history_evidence
	.build/test_render_plain
	$(PYTHON) tests/test_history_http.py
	$(PYTHON) tests/test_history_http_control.py
	$(PYTHON) tests/test_history.py
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
	.build/test_http_control
	$(PYTHON) tests/test_auth.py AuthTests
	.build/test_http_bounds
	.build/test_naming
	.build/test_input
	.build/test_session_config
	.build/test_session_owner
	.build/test_session_owner_whitebox
	.build/test_connect_drain
	$(PYTHON) tests/test_session_liveness_config.py
	$(PYTHON) tests/test_session_liveness.py
	$(PYTHON) tests/test_session_checkpoint.py
	$(PYTHON) tests/test_session_checkpoint_catchup.py
	$(PYTHON) tests/test_session_history_initial.py
	$(PYTHON) tests/test_session_history_new_window.py
	$(PYTHON) tests/test_session_history_epoch.py
	$(PYTHON) tests/test_session_history_retry.py
	$(PYTHON) tests/test_session_cleanup_control.py
	$(PYTHON) tests/test_session_reconnect.py
	$(PYTHON) tests/test_session_start_recovery.py
	$(PYTHON) tests/test_session_start_deadline.py
	$(PYTHON) tests/test_session_http_control.py
	$(PYTHON) tests/test_session_recovery_http_control.py
	$(PYTHON) tests/test_session_backoff_control.py
	$(PYTHON) tests/test_session.py
	$(PYTHON) tests/test_session_failures.py
	$(PYTHON) tests/test_session_permissions.py
	$(PYTHON) tests/test_session_backlog.py
	GIN_SESSION_BINARY=.build/gin $(PYTHON) tests/test_session_reception.py
	GIN_SESSION_BINARY=.build/gin	$(PYTHON) tests/test_session_outbound.py
	$(PYTHON) tests/test_session_live_http.py
	$(PYTHON) tests/test_session_live_control_http.py
	$(PYTHON) tests/test_session_live_http_recovery.py
	$(PYTHON) tests/test_session_live_preflight_stop.py
	GIN_SESSION_BINARY=.build/gin $(PYTHON) tests/test_session_operations.py
	GIN_SESSION_BINARY=.build/gin $(PYTHON) tests/test_session_controls.py
	GIN_SESSION_BINARY=.build/gin	$(PYTHON) tests/test_session_query.py
	$(PYTHON) tests/test_session_query_recovery.py
	GIN_SESSION_BINARY=.build/gin_receive_observer $(PYTHON) tests/test_session_admission.py
	GIN_SESSION_BINARY=.build/gin $(PYTHON) tests/test_session_admission.py Admission.test_complete_text_control
	GIN_SESSION_BINARY=.build/gin_owner_fault $(PYTHON) tests/test_session_owner_faults.py
	$(PYTHON) tests/test_build_prefix.py
	$(PYTHON) tests/test_cli.py
	$(PYTHON) tests/test_live_smoke.py
	$(PYTHON) tests/test_locking.py
	$(PYTHON) tests/test_input.py
	$(PYTHON) tests/test_runtime.py
	$(PYTHON) tests/test_http.py


	.build/test_connect_hot
	.build/test_connect_hot_fault
	GIN_HOT_DRIVER=$${PWD}/.build/connect_hot_wss_driver $(PYTHON) -m unittest discover -s tests -p test_connect_hot_wss.py -v
.build/connect_driver: tests/connect_driver.c $(CONNECT_SOURCES) $(CONNECT_HEADERS) src/json_evidence.h
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude -Isrc $(CURL_CFLAGS) $(JANSSON_CFLAGS) $(LDFLAGS) -o $@ tests/connect_driver.c $(CONNECT_SOURCES) $(CURL_LIBS) $(JANSSON_LIBS) $(LDLIBS)

.build/test_liveness_backoff: tests/test_liveness_backoff.c src/liveness_backoff.c src/liveness_values.h
	mkdir -p .build
	$(CC) $(CPPFLAGS) $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Isrc $(LDFLAGS) -o $@ tests/test_liveness_backoff.c src/liveness_backoff.c $(LDLIBS)

# Private translation-unit clock/I/O faults; do not link connect.c twice.
.build/test_connect_drain: tests/test_connect_drain.c $(CONNECT_SOURCES) $(CONNECT_HEADERS) src/json_evidence.h
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude -Isrc $(CURL_CFLAGS) $(JANSSON_CFLAGS) $(LDFLAGS) -o $@ tests/test_connect_drain.c $(CONNECT_SUPPORT_SOURCES) $(CURL_LIBS) $(JANSSON_LIBS) $(LDLIBS)

.build/test_connect_liveness: tests/test_connect_liveness.c $(CONNECT_SOURCES) $(CONNECT_HEADERS) src/json_evidence.h
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude -Isrc $(CURL_CFLAGS) $(JANSSON_CFLAGS) $(LDFLAGS) -o $@ tests/test_connect_liveness.c $(CONNECT_SUPPORT_SOURCES) $(CURL_LIBS) $(JANSSON_LIBS) $(LDLIBS)

.build/liveness_driver: tests/liveness_driver.c $(CONNECT_SOURCES) $(CONNECT_HEADERS) src/json_evidence.h
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude -Isrc $(CURL_CFLAGS) $(JANSSON_CFLAGS) $(LDFLAGS) -o $@ tests/liveness_driver.c $(CONNECT_SOURCES) $(CURL_LIBS) $(JANSSON_LIBS) $(LDLIBS)

.build/test_connect: tests/test_connect.c $(CONNECT_SOURCES) $(CONNECT_HEADERS) src/json_evidence.h
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
SEND_HEADERS = $(HTTP_CONTROL_HEADERS) include/gin_send.h src/send_values.h include/gin_protocol.h include/gin_auth.h include/gin_http.h include/gin_credential.h include/gin_naming.h include/gin_fs.h

.build/test_send: tests/test_send.c $(SEND_SOURCES) $(SEND_HEADERS) src/json_evidence.h
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude -Isrc $(CURL_CFLAGS) $(JANSSON_CFLAGS) $(LDFLAGS) -o $@ tests/test_send.c $(SEND_SOURCES) $(CURL_LIBS) $(JANSSON_LIBS) $(LDLIBS)

.build/test_send_payload: tests/test_send_payload.c src/send_payload.c src/naming.c $(SEND_HEADERS)
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude -Isrc $(JANSSON_CFLAGS) $(LDFLAGS) -o $@ tests/test_send_payload.c src/send_payload.c src/naming.c $(JANSSON_LIBS) $(LDLIBS)

.build/test_send_reply: tests/test_send_reply.c src/send_reply.c $(SEND_HEADERS)
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude -Isrc $(JANSSON_CFLAGS) $(LDFLAGS) -o $@ tests/test_send_reply.c src/send_reply.c $(JANSSON_LIBS) $(LDLIBS)

.build/send_driver: tests/send_driver.c $(SEND_SOURCES) $(CONNECT_SOURCES) $(RENDER_SOURCES) src/fs.c src/input.c $(SEND_HEADERS) include/gin_input.h $(CONNECT_HEADERS) include/gin_render.h src/render_values.h src/json_evidence.h
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude -Isrc $(CURL_CFLAGS) $(JANSSON_CFLAGS) $(LDFLAGS) -o $@ tests/send_driver.c $(SEND_SOURCES) $(CONNECT_QUERY_SOURCES) src/connect.c src/liveness_backoff.c src/connect_protocols.c src/connect_topic.c src/phoenix.c src/phoenix_stream.c src/phoenix_refs.c $(RENDER_SOURCES) src/fs.c src/input.c $(CURL_LIBS) $(JANSSON_LIBS) $(LDLIBS)

CONTROL_SOURCES = src/control.c src/command.c src/send_reply.c src/naming.c src/fs.c src/input.c src/http.c src/credential.c src/auth.c src/protocol.c src/protocol_parse.c
CONTROL_HEADERS = $(HTTP_CONTROL_HEADERS) include/gin_control.h include/gin_command.h include/gin_send.h src/send_values.h include/gin_naming.h include/gin_fs.h include/gin_input.h include/gin_protocol.h include/gin_auth.h include/gin_http.h include/gin_credential.h

.build/test_command: tests/test_command.c src/command.c src/naming.c include/gin_command.h include/gin_input.h include/gin_naming.h
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude $(LDFLAGS) -o $@ tests/test_command.c src/command.c src/naming.c $(LDLIBS)

.build/test_control: tests/test_control.c $(CONTROL_SOURCES) $(CONTROL_HEADERS) src/json_evidence.h
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude -Isrc $(CURL_CFLAGS) $(JANSSON_CFLAGS) $(LDFLAGS) -o $@ tests/test_control.c $(CONTROL_SOURCES) $(CURL_LIBS) $(JANSSON_LIBS) $(LDLIBS)

.build/control_driver: tests/control_driver.c $(CONTROL_SOURCES) src/send.c src/send_payload.c $(CONTROL_HEADERS) src/json_evidence.h
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude -Isrc $(CURL_CFLAGS) $(JANSSON_CFLAGS) $(LDFLAGS) -o $@ tests/control_driver.c $(CONTROL_SOURCES) src/send.c src/send_payload.c $(CURL_LIBS) $(JANSSON_LIBS) $(LDLIBS)

# This unit includes connect.c privately for deterministic short-write faults.
.build/test_connect_query: tests/test_connect_query.c $(CONNECT_SOURCES) include/gin_connect.h include/gin_command.h include/gin_topology.h src/connect_values.h src/json_evidence.h
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude -Isrc $(CURL_CFLAGS) $(JANSSON_CFLAGS) $(LDFLAGS) -o $@ tests/test_connect_query.c $(CONNECT_SUPPORT_SOURCES) $(CURL_LIBS) $(JANSSON_LIBS) $(LDLIBS)

.build/query_driver: tests/query_driver.c $(CONNECT_SOURCES) src/fs.c src/input.c include/gin_connect.h include/gin_command.h include/gin_topology.h include/gin_fs.h include/gin_input.h src/connect_values.h src/json_evidence.h
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


# History units use the real page implementation except explicit path facades.
.build/test_history_page: tests/test_history_page.c $(HISTORY_PURE_SOURCES) $(HISTORY_PAGE_SUPPORT) $(HISTORY_HEADERS) src/json_evidence.h
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude -Isrc $(CURL_CFLAGS) $(JANSSON_CFLAGS) $(LDFLAGS) -o $@ tests/test_history_page.c $(HISTORY_PURE_SOURCES) $(HISTORY_PAGE_SUPPORT) $(CURL_LIBS) $(JANSSON_LIBS) $(LDLIBS)

.build/test_checkpoint_fault: tests/test_checkpoint.c src/checkpoint.c src/checkpoint.h src/fs.c $(FS_HEADERS) $(HISTORY_PURE_SOURCES) $(HISTORY_PAGE_SUPPORT) $(HISTORY_HEADERS) $(RENDER_SOURCES) src/render_values.h
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -DGIN_CHECKPOINT_FAULT -Iinclude -Isrc $(CURL_CFLAGS) $(JANSSON_CFLAGS) $(LDFLAGS) -o $@ tests/test_checkpoint.c src/fs.c $(HISTORY_PURE_SOURCES) $(HISTORY_PAGE_SUPPORT) $(RENDER_SOURCES) $(CURL_LIBS) $(JANSSON_LIBS) $(LDLIBS)

.build/test_account_checkpoint: tests/test_account_checkpoint.c src/checkpoint.c src/checkpoint.h src/account_naming.c src/account_naming.h src/fs.c $(FS_HEADERS) $(HISTORY_PURE_SOURCES) $(HISTORY_PAGE_SUPPORT) $(HISTORY_HEADERS) $(RENDER_SOURCES) src/render_values.h include/gin_phoenix.h src/connect_values.h src/json_evidence.h
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude -Isrc $(CURL_CFLAGS) $(JANSSON_CFLAGS) $(LDFLAGS) -o $@ tests/test_account_checkpoint.c src/checkpoint.c src/account_naming.c src/fs.c $(HISTORY_PURE_SOURCES) $(HISTORY_PAGE_SUPPORT) $(RENDER_SOURCES) $(CURL_LIBS) $(JANSSON_LIBS) $(LDLIBS)

.build/test_account_network_checkpoint: tests/test_account_network_checkpoint.c src/checkpoint.c src/checkpoint.h src/account_naming.c src/account_naming.h src/fs.c $(FS_HEADERS) $(HISTORY_PURE_SOURCES) $(HISTORY_PAGE_SUPPORT) $(HISTORY_HEADERS) $(RENDER_SOURCES) src/render_values.h include/gin_phoenix.h src/connect_values.h src/json_evidence.h
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude -Isrc $(CURL_CFLAGS) $(JANSSON_CFLAGS) $(LDFLAGS) -o $@ tests/test_account_network_checkpoint.c src/checkpoint.c src/account_naming.c src/fs.c $(HISTORY_PURE_SOURCES) $(HISTORY_PAGE_SUPPORT) $(RENDER_SOURCES) $(CURL_LIBS) $(JANSSON_LIBS) $(LDLIBS)

.build/test_checkpoint: tests/test_checkpoint.c src/checkpoint.c src/checkpoint.h src/fs.c $(FS_HEADERS) $(HISTORY_PURE_SOURCES) $(HISTORY_PAGE_SUPPORT) $(HISTORY_HEADERS) $(RENDER_SOURCES) src/render_values.h
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude -Isrc $(CURL_CFLAGS) $(JANSSON_CFLAGS) $(LDFLAGS) -o $@ tests/test_checkpoint.c src/checkpoint.c src/fs.c $(HISTORY_PURE_SOURCES) $(HISTORY_PAGE_SUPPORT) $(RENDER_SOURCES) $(CURL_LIBS) $(JANSSON_LIBS) $(LDLIBS)

.build/test_history_live: tests/test_history_live.c $(HISTORY_PURE_SOURCES) $(HISTORY_PAGE_SUPPORT) $(HISTORY_HEADERS) src/json_evidence.h
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude -Isrc $(CURL_CFLAGS) $(JANSSON_CFLAGS) $(LDFLAGS) -o $@ tests/test_history_live.c $(HISTORY_PURE_SOURCES) $(HISTORY_PAGE_SUPPORT) $(CURL_LIBS) $(JANSSON_LIBS) $(LDLIBS)

.build/test_history_owned: tests/test_history_page.c $(HISTORY_PURE_SOURCES) $(HISTORY_PAGE_SUPPORT) $(HISTORY_HEADERS) src/json_evidence.h
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude -Isrc $(CURL_CFLAGS) $(JANSSON_CFLAGS) $(LDFLAGS) -o $@ -DTEST_OWNED_FAULT tests/test_history_page.c src/history_evidence.c $(HISTORY_PAGE_SUPPORT) $(CURL_LIBS) $(JANSSON_LIBS) $(LDLIBS)

.build/test_history_recovery: tests/test_history_recovery.c $(HISTORY_PURE_SOURCES) $(HISTORY_PAGE_SUPPORT) $(HISTORY_HEADERS) src/json_evidence.h
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude -Isrc $(CURL_CFLAGS) $(JANSSON_CFLAGS) $(LDFLAGS) -o $@ -DGIN_HISTORY_RECOVERY_REAL_PAGE -DGIN_HISTORY_RECOVERY_WHITEBOX tests/test_history_recovery.c src/history_page.c src/history_evidence.c $(HISTORY_PAGE_SUPPORT) $(CURL_LIBS) $(JANSSON_LIBS) $(LDLIBS)

.build/test_history_path: tests/test_history_path.c src/history_path.c $(HISTORY_HEADERS)
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude -Isrc $(CURL_CFLAGS) $(JANSSON_CFLAGS) $(LDFLAGS) -o $@ tests/test_history_path.c src/history_path.c $(LDLIBS)

# Evidence unit includes its implementation for a private work counter.
.build/test_history_evidence: tests/test_history_evidence.c src/history_evidence.c src/history_evidence.h src/json_evidence.h
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude -Isrc $(CURL_CFLAGS) $(JANSSON_CFLAGS) $(LDFLAGS) -o $@ tests/test_history_evidence.c $(JANSSON_LIBS) $(LDLIBS)

.build/test_render_plain: tests/test_render_plain.c $(RENDER_SOURCES) src/phoenix.c src/naming.c src/connect_topic.c include/gin_render.h src/render_values.h src/json_evidence.h
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude -Isrc $(CURL_CFLAGS) $(JANSSON_CFLAGS) $(LDFLAGS) -o $@ tests/test_render_plain.c $(RENDER_SOURCES) src/phoenix.c src/naming.c src/connect_topic.c $(JANSSON_LIBS) $(LDLIBS)

.build/history_control_driver: tests/history_control_driver.c tests/history_http_driver.c $(HISTORY_SOURCES) $(CONNECT_SOURCES) $(RENDER_SOURCES) src/fs.c $(FS_HEADERS) $(HISTORY_HEADERS) $(CONNECT_HEADERS) src/json_evidence.h
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude -Isrc $(CURL_CFLAGS) $(JANSSON_CFLAGS) $(LDFLAGS) -o $@ tests/history_control_driver.c $(HISTORY_SOURCES) $(CONNECT_SOURCES) $(RENDER_SOURCES) src/fs.c $(CURL_LIBS) $(JANSSON_LIBS) $(LDLIBS)

.build/history_http_driver: tests/history_http_driver.c $(HISTORY_SOURCES) $(CONNECT_SOURCES) $(RENDER_SOURCES) src/fs.c $(HISTORY_HEADERS) $(CONNECT_HEADERS) src/json_evidence.h
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude -Isrc $(CURL_CFLAGS) $(JANSSON_CFLAGS) $(LDFLAGS) -o $@ tests/history_http_driver.c $(HISTORY_SOURCES) $(CONNECT_SOURCES) $(RENDER_SOURCES) src/fs.c $(CURL_LIBS) $(JANSSON_LIBS) $(LDLIBS)

.build/history_driver: tests/history_driver.c $(HISTORY_SOURCES) $(CONNECT_SOURCES) $(RENDER_SOURCES) src/fs.c $(HISTORY_HEADERS) $(CONNECT_HEADERS) src/json_evidence.h
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude -Isrc $(CURL_CFLAGS) $(JANSSON_CFLAGS) $(LDFLAGS) -o $@ tests/history_driver.c $(HISTORY_SOURCES) $(CONNECT_SOURCES) $(RENDER_SOURCES) src/fs.c $(CURL_LIBS) $(JANSSON_LIBS) $(LDLIBS)


.build/test_render: tests/test_render.c $(RENDER_SOURCES) src/phoenix.c src/naming.c src/connect_topic.c include/gin_render.h src/render_values.h src/json_evidence.h
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

.build/render_driver: tests/render_driver.c $(RENDER_SOURCES) $(CONNECT_SOURCES) src/fs.c src/naming.c $(CONNECT_HEADERS) include/gin_render.h src/render_values.h src/json_evidence.h
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude -Isrc $(CURL_CFLAGS) $(JANSSON_CFLAGS) $(LDFLAGS) -o $@ tests/render_driver.c $(RENDER_SOURCES) $(CONNECT_SOURCES) src/fs.c $(CURL_LIBS) $(JANSSON_LIBS) $(LDLIBS)

# Copy queue units: only the owned-fault unit includes the implementation.
.build/test_session_live_queue: tests/test_session_live_queue.c src/session_live_queue.c src/session_live_queue.h include/gin_phoenix.h
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude -Isrc $(LDFLAGS) -o $@ tests/test_session_live_queue.c src/session_live_queue.c $(LDLIBS)

.build/test_session_live_queue_owned: tests/test_session_live_queue_owned.c src/session_live_queue.c src/session_live_queue.h include/gin_phoenix.h
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude -Isrc $(LDFLAGS) -o $@ tests/test_session_live_queue_owned.c $(LDLIBS)

.build/test_live_json_fidelity: tests/test_live_json_fidelity.c $(RENDER_SOURCES) src/phoenix.c src/naming.c src/connect_topic.c src/json_evidence.h include/gin_render.h include/gin_phoenix.h
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude -Isrc $(JANSSON_CFLAGS) $(LDFLAGS) -o $@ tests/test_live_json_fidelity.c $(RENDER_SOURCES) src/phoenix.c src/naming.c src/connect_topic.c $(JANSSON_LIBS) $(LDLIBS)


.build/test_authority_json_config: tests/test_authority_json_config.c src/protocol_parse.c  src/json_evidence.h $(CONNECT_HEADERS)
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude -Isrc $(CURL_CFLAGS) $(JANSSON_CFLAGS) $(LDFLAGS) -o $@ tests/test_authority_json_config.c  $(CURL_LIBS) $(JANSSON_LIBS) $(LDLIBS)

.build/test_authority_json_identity: tests/test_authority_json_identity.c src/auth.c src/credential.c src/fs.c src/naming.c src/http.c src/json_evidence.h $(CONNECT_HEADERS) $(CONTROL_HEADERS)
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude -Isrc $(CURL_CFLAGS) $(JANSSON_CFLAGS) $(LDFLAGS) -o $@ tests/test_authority_json_identity.c src/credential.c src/fs.c src/naming.c src/http.c $(CURL_LIBS) $(JANSSON_LIBS) $(LDLIBS)

.build/test_authority_json_store: tests/test_authority_json_store.c src/credential.c src/fs.c src/naming.c src/http.c src/json_evidence.h $(CONNECT_HEADERS) $(CONTROL_HEADERS)
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude -Isrc $(CURL_CFLAGS) $(JANSSON_CFLAGS) $(LDFLAGS) -o $@ tests/test_authority_json_store.c src/fs.c src/naming.c src/http.c $(CURL_LIBS) $(JANSSON_LIBS) $(LDLIBS)

.build/test_authority_json_connect: tests/test_authority_json_connect.c src/connect.c src/fs.c $(CONNECT_SUPPORT_SOURCES) src/json_evidence.h $(CONNECT_HEADERS) src/connect_hot.h
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude -Isrc $(CURL_CFLAGS) $(JANSSON_CFLAGS) $(LDFLAGS) -o $@ tests/test_authority_json_connect.c src/fs.c $(CONNECT_SUPPORT_SOURCES) $(CURL_LIBS) $(JANSSON_LIBS) $(LDLIBS)

.build/test_session_operation: tests/test_session_operation.c src/session_operation.c src/session_operation.h
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude -Isrc $(LDFLAGS) -o $@ tests/test_session_operation.c src/session_operation.c $(LDLIBS)

SESSION_PROJECTION_SUPPORT = src/session_live_projection.c src/history_page.c src/history_evidence.c src/http.c src/topology.c src/topology_lookup.c $(RENDER_SOURCES) src/phoenix.c src/connect_topic.c src/naming.c

.build/test_session_topology_fidelity: tests/test_session_topology_fidelity.c $(SESSION_PROJECTION_SUPPORT) $(SESSION_HEADERS) src/topology_internal.h
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude -Isrc $(CURL_CFLAGS) $(JANSSON_CFLAGS) $(LDFLAGS) -o $@ tests/test_session_topology_fidelity.c $(SESSION_PROJECTION_SUPPORT) $(CURL_LIBS) $(JANSSON_LIBS) $(LDLIBS)
.build/test_session_clock: tests/test_session_clock.c $(SESSION_PROJECTION_SUPPORT) $(SESSION_HEADERS) src/render_values.h
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude -Isrc $(CURL_CFLAGS) $(JANSSON_CFLAGS) $(LDFLAGS) -o $@ tests/test_session_clock.c $(SESSION_PROJECTION_SUPPORT) $(CURL_LIBS) $(JANSSON_LIBS) $(LDLIBS)

STATUS_RENDER_SOURCES = $(RENDER_SOURCES) src/history_render.c $(HISTORY_PURE_SOURCES) $(HISTORY_PAGE_SUPPORT)
STATUS_RENDER_HEADERS = include/gin_auth.h include/gin_control.h include/gin_credential.h include/gin_fs.h include/gin_history.h include/gin_http.h include/gin_naming.h include/gin_phoenix.h include/gin_protocol.h include/gin_render.h include/gin_send.h include/gin_topology.h src/connect_values.h src/history_evidence.h src/history_values.h src/http_control.h src/json_evidence.h src/render_values.h

.build/test_status_render: tests/test_status_render.c $(STATUS_RENDER_SOURCES) $(STATUS_RENDER_HEADERS)
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude -Isrc $(CURL_CFLAGS) $(JANSSON_CFLAGS) $(LDFLAGS) -o $@ tests/test_status_render.c $(STATUS_RENDER_SOURCES) $(CURL_LIBS) $(JANSSON_LIBS) $(LDLIBS)

# STATUS projection units: fidelity privately includes the real implementation.
STATUS_PROJECTION_SUPPORT = src/history_page.c src/history_evidence.c src/http.c src/topology.c src/topology_lookup.c src/render.c src/render_escape.c src/render_time.c src/phoenix.c src/connect_topic.c src/naming.c
STATUS_PROJECTION_HEADERS = include/gin_auth.h include/gin_config.h include/gin_control.h include/gin_credential.h include/gin_fs.h include/gin_history.h include/gin_http.h include/gin_naming.h include/gin_phoenix.h include/gin_protocol.h include/gin_render.h include/gin_send.h include/gin_session.h include/gin_topology.h src/connect_values.h src/history_evidence.h src/history_values.h src/http_control.h src/json_evidence.h src/render_values.h src/session_live_projection.h src/session_live_queue.h src/topology_internal.h

.build/test_status_projection: tests/test_status_projection.c src/session_live_projection.c $(STATUS_PROJECTION_SUPPORT) $(STATUS_PROJECTION_HEADERS)
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude -Isrc $(CURL_CFLAGS) $(JANSSON_CFLAGS) $(LDFLAGS) -o $@ tests/test_status_projection.c $(STATUS_PROJECTION_SUPPORT) src/session_live_projection.c $(CURL_LIBS) $(JANSSON_LIBS) $(LDLIBS)

.build/test_status_projection_fidelity: tests/test_status_projection_fidelity.c src/session_live_projection.c $(STATUS_PROJECTION_SUPPORT) $(STATUS_PROJECTION_HEADERS)
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude -Isrc $(CURL_CFLAGS) $(JANSSON_CFLAGS) $(LDFLAGS) -o $@ tests/test_status_projection_fidelity.c $(STATUS_PROJECTION_SUPPORT) $(CURL_LIBS) $(JANSSON_LIBS) $(LDLIBS)

# Complete private front-door closure for every main translation-unit role.
ACCOUNT_CLI_HEADERS = include/gin_auth.h include/gin_command.h include/gin_config.h include/gin_connect.h include/gin_control.h include/gin_credential.h include/gin_fs.h include/gin_history.h include/gin_http.h include/gin_input.h include/gin_liveness.h include/gin_naming.h include/gin_phoenix.h include/gin_protocol.h include/gin_render.h include/gin_runtime.h include/gin_send.h include/gin_session.h include/gin_topology.h src/account_cli.h src/account_discovery.h src/account_dispatch.h src/account_fs.h src/account_history_bootstrap.h src/account_naming.h src/account_output.h src/account_owner.h src/account_plan.h src/account_receive.h src/account_root.h src/account_sinks.h src/checkpoint.h src/connect_control.h src/connect_values.h src/fs_private.h src/history_evidence.h src/history_values.h src/http_control.h src/interactive_login.h src/json_evidence.h src/liveness_values.h src/render_values.h src/send_values.h src/session_live_projection.h src/session_live_queue.h src/terminal_password.h src/topology_internal.h
ACCOUNT_CLI_SOURCES = src/account_owner.c src/account_history_bootstrap.c src/account_output.c src/account_dispatch.c src/account_receive.c src/account_plan.c src/account_naming.c src/session_live_queue.c src/session_live_projection.c src/history_page.c src/history_evidence.c src/connect.c src/liveness_backoff.c src/connect_protocols.c src/connect_topic.c src/http.c src/auth.c src/credential.c src/protocol.c src/protocol_parse.c src/phoenix.c src/phoenix_stream.c src/phoenix_refs.c src/command.c src/topology.c src/topology_lookup.c src/naming.c src/render.c src/render_escape.c src/render_time.c src/account_sinks.c src/account_registry.c src/account_fs.c src/account_root.c src/checkpoint.c src/fs.c src/history_render.c src/history_recovery.c src/history_fetch.c src/history_path.c src/control.c src/send_reply.c src/account_cli.c src/account_discovery.c src/topology_fetch.c src/interactive_login.c src/terminal_password.c
.build/gin .build/gin_cleanup_fault .build/gin_owner_fault .build/gin_receive_observer .build/runtime_stderr .build/runtime_stderr.o: $(ACCOUNT_CLI_HEADERS)
.build/gin .build/gin_cleanup_fault .build/gin_owner_fault .build/gin_receive_observer .build/runtime_stderr: $(ACCOUNT_CLI_SOURCES)

# This private driver includes account_cli.c and replaces interactive login.
ACCOUNT_CLI_CONTROL_SUPPORT = $(ACCOUNT_CLI_SOURCES:src/account_cli.c=)
ACCOUNT_CLI_CONTROL_LINK = $(ACCOUNT_CLI_CONTROL_SUPPORT:src/interactive_login.c=) src/input.c src/send.c src/send_payload.c
.build/account_cli_control_driver: src/input.c src/send.c src/send_payload.c include/gin_input.h
.build/account_cli_control_driver: tests/account_cli_control_driver.c $(ACCOUNT_CLI_SOURCES) $(ACCOUNT_CLI_HEADERS)
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude -Isrc $(CURL_CFLAGS) $(JANSSON_CFLAGS) $(LDFLAGS) -o $@ tests/account_cli_control_driver.c $(ACCOUNT_CLI_CONTROL_LINK) $(CURL_LIBS) $(JANSSON_LIBS) $(LDLIBS)

check-account-cli: .build/gin .build/account_cli_control_driver
	$(PYTHON) tests/test_account_cli.py

# Private observer includes account_owner.c; never link the owner twice.
FIFO_PRODUCTION_SOURCES = src/gin.c $(SESSION_SOURCES) $(HISTORY_SOURCES) src/command.c src/control.c src/send.c src/send_payload.c src/send_reply.c src/render.c src/render_escape.c src/render_time.c src/fs.c src/config.c src/naming.c src/input.c src/runtime.c src/http.c src/auth.c src/protocol.c src/protocol_parse.c src/credential.c src/topology.c src/topology_fetch.c src/topology_lookup.c src/phoenix.c src/phoenix_stream.c src/phoenix_refs.c src/connect.c src/liveness_backoff.c src/connect_protocols.c src/connect_topic.c src/account_owner.c src/account_history_bootstrap.c src/account_output.c src/account_dispatch.c src/account_receive.c src/account_plan.c src/account_naming.c src/account_sinks.c src/account_registry.c src/account_fs.c src/account_root.c src/account_cli.c src/account_discovery.c src/interactive_login.c src/terminal_password.c
FIFO_OBSERVER_SOURCES = $(FIFO_PRODUCTION_SOURCES:src/account_owner.c=tests/account_fifo_send_observer.c)
.build/gin_account_fifo_observer: $(FIFO_OBSERVER_SOURCES) src/account_owner.c $(ACCOUNT_CLI_HEADERS) $(SESSION_HEADERS) $(HISTORY_HEADERS)
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude $(CURL_CFLAGS) $(JANSSON_CFLAGS) $(LDFLAGS) -o $@ $(FIFO_OBSERVER_SOURCES) $(CURL_LIBS) $(JANSSON_LIBS) $(LDLIBS)

check-account-fifo-send: .build/gin .build/gin_account_fifo_observer
	GIN_ACCOUNT_CLI_BINARY="$${PWD}/.build/gin" $(PYTHON) -m unittest discover -s tests -p test_account_fifo_send.py -v
	GIN_FIFO_OBSERVER=1 GIN_ACCOUNT_CLI_BINARY="$${PWD}/.build/gin_account_fifo_observer" $(PYTHON) -m unittest discover -s tests -p test_account_fifo_send.py -v

check-account-network-fifo: .build/gin .build/gin_account_fifo_observer
	GIN_ACCOUNT_CLI_BINARY="$${PWD}/.build/gin" $(PYTHON) -m unittest discover -s tests -p test_account_network_fifo.py -v
	GIN_FIFO_OBSERVER=1 GIN_ACCOUNT_CLI_BINARY="$${PWD}/.build/gin_account_fifo_observer" $(PYTHON) -m unittest discover -s tests -p test_account_network_fifo.py -v


# Pure registry unit: SDK headers only, no curl runtime or production wiring.
ACCOUNT_REGISTRY_SOURCES = tests/test_account_registry.c src/account_plan.c src/account_naming.c src/naming.c src/topology.c src/connect_topic.c src/command.c src/account_registry.c
ACCOUNT_REGISTRY_HEADERS = include/gin_auth.h include/gin_command.h include/gin_connect.h include/gin_credential.h include/gin_fs.h include/gin_http.h include/gin_naming.h include/gin_phoenix.h include/gin_protocol.h include/gin_topology.h src/account_naming.h src/account_plan.h src/account_registry.h src/connect_values.h src/json_evidence.h src/topology_internal.h
.build/test_account_registry: $(ACCOUNT_REGISTRY_SOURCES) $(ACCOUNT_REGISTRY_HEADERS)
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude -Isrc $(CURL_CFLAGS) $(JANSSON_CFLAGS) $(LDFLAGS) -o $@ $(ACCOUNT_REGISTRY_SOURCES) $(JANSSON_LIBS) $(LDLIBS)

.PHONY: check-account-registry
check-account-registry: .build/test_account_registry
	.build/test_account_registry


# Native-only hot-query characterization; units include the production TU.
.build/test_connect_hot: tests/test_connect_hot.c src/connect.c $(CONNECT_SUPPORT_SOURCES) $(CONNECT_HEADERS) src/json_evidence.h
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude -Isrc $(CURL_CFLAGS) $(JANSSON_CFLAGS) $(LDFLAGS) -o $@ tests/test_connect_hot.c $(CONNECT_SUPPORT_SOURCES) $(CURL_LIBS) $(JANSSON_LIBS) $(LDLIBS)

.build/test_connect_hot_fault: tests/test_connect_hot_fault.c src/connect.c $(CONNECT_SUPPORT_SOURCES) $(CONNECT_HEADERS) src/json_evidence.h
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude -Isrc $(CURL_CFLAGS) $(JANSSON_CFLAGS) $(LDFLAGS) -o $@ tests/test_connect_hot_fault.c $(CONNECT_SUPPORT_SOURCES) $(CURL_LIBS) $(JANSSON_LIBS) $(LDLIBS)

.build/connect_hot_wss_driver: tests/connect_hot_wss_driver.c src/connect.c $(CONNECT_SOURCES) $(CONNECT_HEADERS) src/json_evidence.h
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude -Isrc $(CURL_CFLAGS) $(JANSSON_CFLAGS) $(LDFLAGS) -o $@ tests/connect_hot_wss_driver.c $(CONNECT_SOURCES) $(CURL_LIBS) $(JANSSON_LIBS) $(LDLIBS)

check-connect-hot: .build/test_connect_hot .build/test_connect_hot_fault .build/connect_hot_wss_driver
	.build/test_connect_hot
	.build/test_connect_hot_fault
	GIN_HOT_DRIVER=$${PWD}/.build/connect_hot_wss_driver $(PYTHON) -m unittest discover -s tests -p test_connect_hot_wss.py -v

.PHONY: check-connect-hot

# Owned hot receiver: the unit textually includes both production TUs.
ACCOUNT_RECEIVE_HOT_SUPPORT = src/account_plan.c src/account_naming.c src/session_live_queue.c src/fs.c $(CONNECT_SUPPORT_SOURCES)
.build/test_account_receive_hot: tests/test_account_receive_hot.c tests/test_connect_hot.c src/account_receive.c src/connect.c $(ACCOUNT_RECEIVE_HOT_SUPPORT) $(ACCOUNT_RECEIVE_HEADERS) src/account_receive_hot.h
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude -Isrc $(CURL_CFLAGS) $(JANSSON_CFLAGS) $(LDFLAGS) -o $@ tests/test_account_receive_hot.c $(ACCOUNT_RECEIVE_HOT_SUPPORT) $(CURL_LIBS) $(JANSSON_LIBS) $(LDLIBS)

.build/account_receive_hot_wss_driver: tests/account_receive_hot_wss_driver.c src/account_receive.c src/connect.c $(ACCOUNT_RECEIVE_HOT_SUPPORT) $(ACCOUNT_RECEIVE_HEADERS) src/account_receive_hot.h
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude -Isrc $(CURL_CFLAGS) $(JANSSON_CFLAGS) $(LDFLAGS) -o $@ tests/account_receive_hot_wss_driver.c src/account_receive.c src/connect.c $(ACCOUNT_RECEIVE_HOT_SUPPORT) $(CURL_LIBS) $(JANSSON_LIBS) $(LDLIBS)

check-account-receive-hot: .build/test_account_receive_hot .build/account_receive_hot_wss_driver
	.build/test_account_receive_hot
	GIN_ACCOUNT_HOT_DRIVER=$${PWD}/.build/account_receive_hot_wss_driver $(PYTHON) -m unittest discover -s tests -p test_account_receive_hot_wss.py -v

.PHONY: check-account-receive-hot

# Every actual consumer, including legacy roles, includes this opt-in header.
.build/account_cli_control_driver .build/account_dispatch_driver .build/account_history_bootstrap_driver .build/account_history_output_driver .build/account_output_driver .build/account_owner_driver .build/account_receive_driver .build/gin .build/gin_account_fifo_observer .build/gin_cleanup_fault .build/gin_owner_fault .build/gin_receive_observer .build/runtime_stderr .build/test_account_dispatch_fidelity .build/test_account_receive_control: src/account_receive_hot.h

# Recursive quoted-header closure of actual Native consumers.
.build/account_cli_control_driver: include/gin_auth.h include/gin_command.h include/gin_config.h include/gin_connect.h include/gin_control.h include/gin_credential.h include/gin_fs.h include/gin_history.h include/gin_http.h include/gin_input.h include/gin_liveness.h include/gin_naming.h include/gin_phoenix.h include/gin_protocol.h include/gin_render.h include/gin_send.h include/gin_session.h include/gin_topology.h src/account_cli.h src/account_discovery.h src/account_dispatch.h src/account_fs.h src/account_history_bootstrap.h src/account_naming.h src/account_output.h src/account_owner.h src/account_plan.h src/account_receive.h src/account_root.h src/account_sinks.h src/checkpoint.h src/connect_control.h src/connect_hot.h src/connect_values.h src/fs_private.h src/history_evidence.h src/history_values.h src/http_control.h src/interactive_login.h src/json_evidence.h src/liveness_values.h src/render_values.h src/send_values.h src/session_live_projection.h src/session_live_queue.h src/terminal_password.h src/topology_internal.h
.build/account_discovery_driver: include/gin_auth.h include/gin_command.h include/gin_connect.h include/gin_control.h include/gin_credential.h include/gin_fs.h include/gin_http.h include/gin_liveness.h include/gin_naming.h include/gin_phoenix.h include/gin_protocol.h include/gin_send.h include/gin_topology.h src/account_discovery.h src/connect_control.h src/connect_hot.h src/connect_values.h src/http_control.h src/json_evidence.h src/liveness_values.h src/session_live_queue.h src/topology_internal.h
.build/account_dispatch_driver: include/gin_auth.h include/gin_command.h include/gin_config.h include/gin_connect.h include/gin_control.h include/gin_credential.h include/gin_fs.h include/gin_history.h include/gin_http.h include/gin_liveness.h include/gin_naming.h include/gin_phoenix.h include/gin_protocol.h include/gin_render.h include/gin_send.h include/gin_session.h include/gin_topology.h src/account_dispatch.h src/account_naming.h src/account_plan.h src/account_receive.h src/connect_control.h src/connect_hot.h src/connect_values.h src/history_evidence.h src/history_values.h src/http_control.h src/json_evidence.h src/liveness_values.h src/render_values.h src/session_live_projection.h src/session_live_queue.h src/topology_internal.h
.build/account_history_bootstrap_driver: include/gin_auth.h include/gin_command.h include/gin_config.h include/gin_connect.h include/gin_control.h include/gin_credential.h include/gin_fs.h include/gin_history.h include/gin_http.h include/gin_liveness.h include/gin_naming.h include/gin_phoenix.h include/gin_protocol.h include/gin_render.h include/gin_send.h include/gin_session.h include/gin_topology.h src/account_dispatch.h src/account_fs.h src/account_history_bootstrap.h src/account_naming.h src/account_output.h src/account_plan.h src/account_receive.h src/account_root.h src/account_sinks.h src/checkpoint.h src/connect_control.h src/connect_hot.h src/connect_values.h src/fs_private.h src/history_evidence.h src/history_values.h src/http_control.h src/json_evidence.h src/liveness_values.h src/render_values.h src/send_values.h src/session_live_projection.h src/session_live_queue.h src/topology_internal.h
.build/account_history_output_driver: include/gin_auth.h include/gin_command.h include/gin_config.h include/gin_connect.h include/gin_control.h include/gin_credential.h include/gin_fs.h include/gin_history.h include/gin_http.h include/gin_liveness.h include/gin_naming.h include/gin_phoenix.h include/gin_protocol.h include/gin_render.h include/gin_send.h include/gin_session.h include/gin_topology.h src/account_dispatch.h src/account_fs.h src/account_naming.h src/account_output.h src/account_plan.h src/account_receive.h src/account_root.h src/account_sinks.h src/checkpoint.h src/connect_control.h src/connect_hot.h src/connect_values.h src/fs_private.h src/history_evidence.h src/history_values.h src/http_control.h src/json_evidence.h src/liveness_values.h src/render_values.h src/session_live_projection.h src/session_live_queue.h src/topology_internal.h
.build/account_output_driver: include/gin_auth.h include/gin_command.h include/gin_config.h include/gin_connect.h include/gin_control.h include/gin_credential.h include/gin_fs.h include/gin_history.h include/gin_http.h include/gin_liveness.h include/gin_naming.h include/gin_phoenix.h include/gin_protocol.h include/gin_render.h include/gin_send.h include/gin_session.h include/gin_topology.h src/account_dispatch.h src/account_fs.h src/account_naming.h src/account_output.h src/account_plan.h src/account_receive.h src/account_root.h src/account_sinks.h src/checkpoint.h src/connect_control.h src/connect_hot.h src/connect_values.h src/fs_private.h src/history_evidence.h src/history_values.h src/http_control.h src/json_evidence.h src/liveness_values.h src/render_values.h src/session_live_projection.h src/session_live_queue.h src/topology_internal.h
.build/account_owner_driver: include/gin_auth.h include/gin_command.h include/gin_config.h include/gin_connect.h include/gin_control.h include/gin_credential.h include/gin_fs.h include/gin_history.h include/gin_http.h include/gin_input.h include/gin_liveness.h include/gin_naming.h include/gin_phoenix.h include/gin_protocol.h include/gin_render.h include/gin_send.h include/gin_session.h include/gin_topology.h src/account_dispatch.h src/account_fs.h src/account_history_bootstrap.h src/account_naming.h src/account_output.h src/account_owner.h src/account_plan.h src/account_receive.h src/account_root.h src/account_sinks.h src/checkpoint.h src/connect_control.h src/connect_hot.h src/connect_values.h src/fs_private.h src/history_evidence.h src/history_values.h src/http_control.h src/json_evidence.h src/liveness_values.h src/render_values.h src/send_values.h src/session_live_projection.h src/session_live_queue.h src/topology_internal.h
.build/account_receive_driver: include/gin_auth.h include/gin_command.h include/gin_connect.h include/gin_control.h include/gin_credential.h include/gin_fs.h include/gin_http.h include/gin_liveness.h include/gin_naming.h include/gin_phoenix.h include/gin_protocol.h include/gin_send.h include/gin_topology.h src/account_naming.h src/account_plan.h src/account_receive.h src/connect_control.h src/connect_hot.h src/connect_values.h src/fs_private.h src/http_control.h src/json_evidence.h src/liveness_values.h src/session_live_queue.h src/topology_internal.h
.build/connect_driver: include/gin_auth.h include/gin_command.h include/gin_connect.h include/gin_control.h include/gin_credential.h include/gin_fs.h include/gin_http.h include/gin_liveness.h include/gin_naming.h include/gin_phoenix.h include/gin_protocol.h include/gin_send.h include/gin_topology.h src/connect_control.h src/connect_hot.h src/connect_values.h src/http_control.h src/json_evidence.h src/liveness_values.h src/topology_internal.h
.build/connect_hot_wss_driver: include/gin_auth.h include/gin_command.h include/gin_connect.h include/gin_control.h include/gin_credential.h include/gin_fs.h include/gin_http.h include/gin_liveness.h include/gin_naming.h include/gin_phoenix.h include/gin_protocol.h include/gin_send.h include/gin_topology.h src/connect_control.h src/connect_hot.h src/connect_values.h src/http_control.h src/json_evidence.h src/liveness_values.h src/topology_internal.h
.build/connect_replan_wss_driver: include/gin_auth.h include/gin_command.h include/gin_connect.h include/gin_control.h include/gin_credential.h include/gin_fs.h include/gin_http.h include/gin_liveness.h include/gin_naming.h include/gin_phoenix.h include/gin_protocol.h include/gin_send.h include/gin_topology.h src/connect_control.h src/connect_hot.h src/connect_values.h src/fs_private.h src/http_control.h src/json_evidence.h src/liveness_values.h src/topology_internal.h
.build/gin: include/gin_auth.h include/gin_command.h include/gin_config.h include/gin_connect.h include/gin_control.h include/gin_credential.h include/gin_fs.h include/gin_history.h include/gin_http.h include/gin_input.h include/gin_liveness.h include/gin_naming.h include/gin_phoenix.h include/gin_protocol.h include/gin_render.h include/gin_runtime.h include/gin_send.h include/gin_session.h include/gin_topology.h src/account_cli.h src/account_discovery.h src/account_dispatch.h src/account_fs.h src/account_history_bootstrap.h src/account_naming.h src/account_output.h src/account_owner.h src/account_plan.h src/account_receive.h src/account_root.h src/account_sinks.h src/checkpoint.h src/connect_control.h src/connect_hot.h src/connect_values.h src/fs_private.h src/history_evidence.h src/history_values.h src/http_control.h src/interactive_login.h src/json_evidence.h src/liveness_values.h src/render_values.h src/send_values.h src/session_live_projection.h src/session_live_queue.h src/session_operation.h src/terminal_password.h src/topology_internal.h
.build/gin_account_fifo_observer: include/gin_auth.h include/gin_command.h include/gin_config.h include/gin_connect.h include/gin_control.h include/gin_credential.h include/gin_fs.h include/gin_history.h include/gin_http.h include/gin_input.h include/gin_liveness.h include/gin_naming.h include/gin_phoenix.h include/gin_protocol.h include/gin_render.h include/gin_runtime.h include/gin_send.h include/gin_session.h include/gin_topology.h src/account_cli.h src/account_discovery.h src/account_dispatch.h src/account_fs.h src/account_history_bootstrap.h src/account_naming.h src/account_output.h src/account_owner.h src/account_plan.h src/account_receive.h src/account_root.h src/account_sinks.h src/checkpoint.h src/connect_control.h src/connect_hot.h src/connect_values.h src/fs_private.h src/history_evidence.h src/history_values.h src/http_control.h src/interactive_login.h src/json_evidence.h src/liveness_values.h src/render_values.h src/send_values.h src/session_live_projection.h src/session_live_queue.h src/session_operation.h src/terminal_password.h src/topology_internal.h
.build/gin_cleanup_fault: include/gin_auth.h include/gin_command.h include/gin_config.h include/gin_connect.h include/gin_control.h include/gin_credential.h include/gin_fs.h include/gin_history.h include/gin_http.h include/gin_input.h include/gin_liveness.h include/gin_naming.h include/gin_phoenix.h include/gin_protocol.h include/gin_render.h include/gin_runtime.h include/gin_send.h include/gin_session.h include/gin_topology.h src/account_cli.h src/account_discovery.h src/account_dispatch.h src/account_fs.h src/account_history_bootstrap.h src/account_naming.h src/account_output.h src/account_owner.h src/account_plan.h src/account_receive.h src/account_root.h src/account_sinks.h src/checkpoint.h src/connect_control.h src/connect_hot.h src/connect_values.h src/fs_private.h src/history_evidence.h src/history_values.h src/http_control.h src/interactive_login.h src/json_evidence.h src/liveness_values.h src/render_values.h src/send_values.h src/session_live_projection.h src/session_live_queue.h src/session_operation.h src/terminal_password.h src/topology_internal.h
.build/gin_owner_fault: include/gin_auth.h include/gin_command.h include/gin_config.h include/gin_connect.h include/gin_control.h include/gin_credential.h include/gin_fs.h include/gin_history.h include/gin_http.h include/gin_input.h include/gin_liveness.h include/gin_naming.h include/gin_phoenix.h include/gin_protocol.h include/gin_render.h include/gin_runtime.h include/gin_send.h include/gin_session.h include/gin_topology.h src/account_cli.h src/account_discovery.h src/account_dispatch.h src/account_fs.h src/account_history_bootstrap.h src/account_naming.h src/account_output.h src/account_owner.h src/account_plan.h src/account_receive.h src/account_root.h src/account_sinks.h src/checkpoint.h src/connect_control.h src/connect_hot.h src/connect_values.h src/fs_private.h src/history_evidence.h src/history_values.h src/http_control.h src/interactive_login.h src/json_evidence.h src/liveness_values.h src/render_values.h src/send_values.h src/session_live_projection.h src/session_live_queue.h src/session_operation.h src/terminal_password.h src/topology_internal.h
.build/gin_receive_observer: include/gin_auth.h include/gin_command.h include/gin_config.h include/gin_connect.h include/gin_control.h include/gin_credential.h include/gin_fs.h include/gin_history.h include/gin_http.h include/gin_input.h include/gin_liveness.h include/gin_naming.h include/gin_phoenix.h include/gin_protocol.h include/gin_render.h include/gin_runtime.h include/gin_send.h include/gin_session.h include/gin_topology.h src/account_cli.h src/account_discovery.h src/account_dispatch.h src/account_fs.h src/account_history_bootstrap.h src/account_naming.h src/account_output.h src/account_owner.h src/account_plan.h src/account_receive.h src/account_root.h src/account_sinks.h src/checkpoint.h src/connect_control.h src/connect_hot.h src/connect_values.h src/fs_private.h src/history_evidence.h src/history_values.h src/http_control.h src/interactive_login.h src/json_evidence.h src/liveness_values.h src/render_values.h src/send_values.h src/session_live_projection.h src/session_live_queue.h src/session_operation.h src/terminal_password.h src/topology_internal.h
.build/history_control_driver: include/gin_auth.h include/gin_command.h include/gin_connect.h include/gin_control.h include/gin_credential.h include/gin_fs.h include/gin_history.h include/gin_http.h include/gin_liveness.h include/gin_naming.h include/gin_phoenix.h include/gin_protocol.h include/gin_render.h include/gin_send.h include/gin_topology.h src/connect_control.h src/connect_hot.h src/connect_values.h src/fs_private.h src/history_evidence.h src/history_values.h src/http_control.h src/json_evidence.h src/liveness_values.h src/render_values.h src/topology_internal.h
.build/history_driver: include/gin_auth.h include/gin_command.h include/gin_connect.h include/gin_control.h include/gin_credential.h include/gin_fs.h include/gin_history.h include/gin_http.h include/gin_liveness.h include/gin_naming.h include/gin_phoenix.h include/gin_protocol.h include/gin_render.h include/gin_send.h include/gin_topology.h src/connect_control.h src/connect_hot.h src/connect_values.h src/fs_private.h src/history_evidence.h src/history_values.h src/http_control.h src/json_evidence.h src/liveness_values.h src/render_values.h src/topology_internal.h
.build/history_http_driver: include/gin_auth.h include/gin_command.h include/gin_connect.h include/gin_control.h include/gin_credential.h include/gin_fs.h include/gin_history.h include/gin_http.h include/gin_liveness.h include/gin_naming.h include/gin_phoenix.h include/gin_protocol.h include/gin_render.h include/gin_send.h include/gin_topology.h src/connect_control.h src/connect_hot.h src/connect_values.h src/fs_private.h src/history_evidence.h src/history_values.h src/http_control.h src/json_evidence.h src/liveness_values.h src/render_values.h src/topology_internal.h
.build/liveness_driver: include/gin_auth.h include/gin_command.h include/gin_connect.h include/gin_control.h include/gin_credential.h include/gin_fs.h include/gin_http.h include/gin_liveness.h include/gin_naming.h include/gin_phoenix.h include/gin_protocol.h include/gin_send.h include/gin_topology.h src/connect_control.h src/connect_hot.h src/connect_values.h src/http_control.h src/json_evidence.h src/liveness_values.h src/topology_internal.h
.build/query_driver: include/gin_auth.h include/gin_command.h include/gin_connect.h include/gin_control.h include/gin_credential.h include/gin_fs.h include/gin_http.h include/gin_input.h include/gin_liveness.h include/gin_naming.h include/gin_phoenix.h include/gin_protocol.h include/gin_send.h include/gin_topology.h src/connect_control.h src/connect_hot.h src/connect_values.h src/fs_private.h src/http_control.h src/json_evidence.h src/liveness_values.h src/topology_internal.h
.build/render_driver: include/gin_auth.h include/gin_command.h include/gin_connect.h include/gin_control.h include/gin_credential.h include/gin_fs.h include/gin_http.h include/gin_liveness.h include/gin_naming.h include/gin_phoenix.h include/gin_protocol.h include/gin_render.h include/gin_send.h include/gin_topology.h src/connect_control.h src/connect_hot.h src/connect_values.h src/fs_private.h src/http_control.h src/json_evidence.h src/liveness_values.h src/render_values.h src/topology_internal.h
.build/runtime_stderr: include/gin_auth.h include/gin_command.h include/gin_config.h include/gin_connect.h include/gin_control.h include/gin_credential.h include/gin_fs.h include/gin_history.h include/gin_http.h include/gin_input.h include/gin_liveness.h include/gin_naming.h include/gin_phoenix.h include/gin_protocol.h include/gin_render.h include/gin_runtime.h include/gin_send.h include/gin_session.h include/gin_topology.h src/account_cli.h src/account_discovery.h src/account_dispatch.h src/account_fs.h src/account_history_bootstrap.h src/account_naming.h src/account_output.h src/account_owner.h src/account_plan.h src/account_receive.h src/account_root.h src/account_sinks.h src/checkpoint.h src/connect_control.h src/connect_hot.h src/connect_values.h src/fs_private.h src/history_evidence.h src/history_values.h src/http_control.h src/interactive_login.h src/json_evidence.h src/liveness_values.h src/render_values.h src/send_values.h src/session_live_projection.h src/session_live_queue.h src/session_operation.h src/terminal_password.h src/topology_internal.h
.build/send_driver: include/gin_auth.h include/gin_command.h include/gin_connect.h include/gin_control.h include/gin_credential.h include/gin_fs.h include/gin_http.h include/gin_input.h include/gin_liveness.h include/gin_naming.h include/gin_phoenix.h include/gin_protocol.h include/gin_render.h include/gin_send.h include/gin_topology.h src/connect_control.h src/connect_hot.h src/connect_values.h src/fs_private.h src/http_control.h src/json_evidence.h src/liveness_values.h src/render_values.h src/send_values.h src/topology_internal.h
.build/test_account_dispatch_fidelity: include/gin_auth.h include/gin_command.h include/gin_config.h include/gin_connect.h include/gin_control.h include/gin_credential.h include/gin_fs.h include/gin_history.h include/gin_http.h include/gin_liveness.h include/gin_naming.h include/gin_phoenix.h include/gin_protocol.h include/gin_render.h include/gin_send.h include/gin_session.h include/gin_topology.h src/account_dispatch.h src/account_naming.h src/account_plan.h src/account_receive.h src/connect_control.h src/connect_hot.h src/connect_values.h src/history_evidence.h src/history_values.h src/http_control.h src/json_evidence.h src/liveness_values.h src/render_values.h src/session_live_projection.h src/session_live_queue.h src/topology_internal.h
.build/test_account_plan: include/gin_auth.h include/gin_command.h include/gin_connect.h include/gin_control.h include/gin_credential.h include/gin_fs.h include/gin_http.h include/gin_liveness.h include/gin_naming.h include/gin_phoenix.h include/gin_protocol.h include/gin_send.h include/gin_topology.h src/account_naming.h src/account_plan.h src/connect_control.h src/connect_hot.h src/connect_values.h src/http_control.h src/json_evidence.h src/liveness_values.h src/topology_internal.h
.build/test_account_plan_fault: include/gin_auth.h include/gin_command.h include/gin_connect.h include/gin_control.h include/gin_credential.h include/gin_fs.h include/gin_http.h include/gin_liveness.h include/gin_naming.h include/gin_phoenix.h include/gin_protocol.h include/gin_send.h include/gin_topology.h src/account_naming.h src/account_plan.h src/connect_control.h src/connect_hot.h src/connect_values.h src/http_control.h src/json_evidence.h src/liveness_values.h src/topology_internal.h
.build/test_account_receive_control: include/gin_auth.h include/gin_command.h include/gin_connect.h include/gin_control.h include/gin_credential.h include/gin_fs.h include/gin_http.h include/gin_liveness.h include/gin_naming.h include/gin_phoenix.h include/gin_protocol.h include/gin_send.h include/gin_topology.h src/account_naming.h src/account_plan.h src/account_receive.h src/connect_control.h src/connect_hot.h src/connect_values.h src/fs_private.h src/http_control.h src/json_evidence.h src/liveness_values.h src/session_live_queue.h src/topology_internal.h
.build/test_authority_json_connect: include/gin_auth.h include/gin_command.h include/gin_connect.h include/gin_control.h include/gin_credential.h include/gin_fs.h include/gin_http.h include/gin_liveness.h include/gin_naming.h include/gin_phoenix.h include/gin_protocol.h include/gin_send.h include/gin_topology.h src/connect_control.h src/connect_hot.h src/connect_values.h src/fs_private.h src/http_control.h src/json_evidence.h src/liveness_values.h src/topology_internal.h
.build/test_connect: include/gin_auth.h include/gin_command.h include/gin_connect.h include/gin_control.h include/gin_credential.h include/gin_fs.h include/gin_http.h include/gin_liveness.h include/gin_naming.h include/gin_phoenix.h include/gin_protocol.h include/gin_send.h include/gin_topology.h src/connect_control.h src/connect_hot.h src/connect_values.h src/http_control.h src/json_evidence.h src/liveness_values.h src/topology_internal.h
.build/test_connect_drain: include/gin_auth.h include/gin_command.h include/gin_connect.h include/gin_control.h include/gin_credential.h include/gin_fs.h include/gin_http.h include/gin_liveness.h include/gin_naming.h include/gin_phoenix.h include/gin_protocol.h include/gin_send.h include/gin_topology.h src/connect_control.h src/connect_hot.h src/connect_values.h src/http_control.h src/json_evidence.h src/liveness_values.h src/topology_internal.h
.build/test_connect_hot: include/gin_auth.h include/gin_command.h include/gin_connect.h include/gin_control.h include/gin_credential.h include/gin_fs.h include/gin_http.h include/gin_liveness.h include/gin_naming.h include/gin_phoenix.h include/gin_protocol.h include/gin_send.h include/gin_topology.h src/connect_control.h src/connect_hot.h src/connect_values.h src/http_control.h src/json_evidence.h src/liveness_values.h src/topology_internal.h
.build/test_connect_hot_fault: include/gin_auth.h include/gin_command.h include/gin_connect.h include/gin_control.h include/gin_credential.h include/gin_fs.h include/gin_http.h include/gin_liveness.h include/gin_naming.h include/gin_phoenix.h include/gin_protocol.h include/gin_send.h include/gin_topology.h src/connect_control.h src/connect_hot.h src/connect_values.h src/http_control.h src/json_evidence.h src/liveness_values.h src/topology_internal.h
.build/test_connect_liveness: include/gin_auth.h include/gin_command.h include/gin_connect.h include/gin_control.h include/gin_credential.h include/gin_fs.h include/gin_http.h include/gin_liveness.h include/gin_naming.h include/gin_phoenix.h include/gin_protocol.h include/gin_send.h include/gin_topology.h src/connect_control.h src/connect_hot.h src/connect_values.h src/http_control.h src/json_evidence.h src/liveness_values.h src/topology_internal.h
.build/test_connect_query: include/gin_auth.h include/gin_command.h include/gin_connect.h include/gin_control.h include/gin_credential.h include/gin_fs.h include/gin_http.h include/gin_liveness.h include/gin_naming.h include/gin_phoenix.h include/gin_protocol.h include/gin_send.h include/gin_topology.h src/connect_control.h src/connect_hot.h src/connect_values.h src/http_control.h src/json_evidence.h src/liveness_values.h src/topology_internal.h
.build/test_connect_replan: include/gin_auth.h include/gin_command.h include/gin_connect.h include/gin_control.h include/gin_credential.h include/gin_fs.h include/gin_http.h include/gin_liveness.h include/gin_naming.h include/gin_phoenix.h include/gin_protocol.h include/gin_send.h include/gin_topology.h src/connect_control.h src/connect_hot.h src/connect_values.h src/fs_private.h src/http_control.h src/json_evidence.h src/liveness_values.h src/topology_internal.h
.build/test_session_owner: include/gin_auth.h include/gin_command.h include/gin_config.h include/gin_connect.h include/gin_control.h include/gin_credential.h include/gin_fs.h include/gin_history.h include/gin_http.h include/gin_input.h include/gin_liveness.h include/gin_naming.h include/gin_phoenix.h include/gin_protocol.h include/gin_render.h include/gin_send.h include/gin_session.h include/gin_topology.h src/checkpoint.h src/connect_control.h src/connect_hot.h src/connect_values.h src/fs_private.h src/history_evidence.h src/history_values.h src/http_control.h src/json_evidence.h src/liveness_values.h src/render_values.h src/send_values.h src/session_live_projection.h src/session_live_queue.h src/session_operation.h src/topology_internal.h
.build/test_session_owner_whitebox: include/gin_auth.h include/gin_command.h include/gin_config.h include/gin_connect.h include/gin_control.h include/gin_credential.h include/gin_fs.h include/gin_history.h include/gin_http.h include/gin_input.h include/gin_liveness.h include/gin_naming.h include/gin_phoenix.h include/gin_protocol.h include/gin_render.h include/gin_send.h include/gin_session.h include/gin_topology.h src/checkpoint.h src/connect_control.h src/connect_hot.h src/connect_values.h src/fs_private.h src/history_evidence.h src/history_values.h src/http_control.h src/json_evidence.h src/liveness_values.h src/render_values.h src/send_values.h src/session_live_projection.h src/session_live_queue.h src/session_operation.h src/topology_internal.h

ACCOUNT_HOT_OUTPUT_SUPPORT = src/phoenix.c $(ACCOUNT_SINKS_SUPPORT) src/account_sinks.c src/checkpoint.c src/history_page.c src/history_evidence.c src/history_render.c $(RENDER_SOURCES) src/http.c

# Isolated resource units; no account frontend runtime integration.

.build/test_credential_identity: tests/test_credential_identity.c src/credential.c
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude -Isrc $(CURL_CFLAGS) $(JANSSON_CFLAGS) $(LDFLAGS) -o $@ tests/test_credential_identity.c src/credential.c $(CURL_LIBS) $(JANSSON_LIBS) $(LDLIBS)

.build/test_account_root_hot_fault: tests/test_account_root_hot_fault.c $(ACCOUNT_ROOT_SUPPORT)
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude -Isrc $(CURL_CFLAGS) $(JANSSON_CFLAGS) $(LDFLAGS) -o $@ tests/test_account_root_hot_fault.c $(ACCOUNT_ROOT_SUPPORT) $(CURL_LIBS) $(JANSSON_LIBS) $(LDLIBS)

.build/test_account_fs_hot_fault: tests/test_account_fs_hot_fault.c src/account_naming.c src/naming.c
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude -Isrc $(CURL_CFLAGS) $(JANSSON_CFLAGS) $(LDFLAGS) -o $@ tests/test_account_fs_hot_fault.c src/account_naming.c src/naming.c $(CURL_LIBS) $(JANSSON_LIBS) $(LDLIBS)

.build/test_account_sinks_hot: tests/test_account_sinks_hot.c $(ACCOUNT_SINKS_SUPPORT)
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude -Isrc $(CURL_CFLAGS) $(JANSSON_CFLAGS) $(LDFLAGS) -o $@ tests/test_account_sinks_hot.c $(ACCOUNT_SINKS_SUPPORT) $(CURL_LIBS) $(JANSSON_LIBS) $(LDLIBS)

.build/test_account_sinks_hot_bind: tests/test_account_sinks_hot_bind.c $(ACCOUNT_SINKS_SUPPORT)
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude -Isrc $(CURL_CFLAGS) $(JANSSON_CFLAGS) $(LDFLAGS) -o $@ tests/test_account_sinks_hot_bind.c $(ACCOUNT_SINKS_SUPPORT) $(CURL_LIBS) $(JANSSON_LIBS) $(LDLIBS)

.build/test_account_sinks_hot_status: tests/test_account_sinks_hot_status.c src/account_plan.c src/account_naming.c src/account_fs.c src/account_root.c src/fs.c src/naming.c src/credential.c src/topology.c src/topology_lookup.c src/connect_topic.c src/command.c
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude -Isrc $(CURL_CFLAGS) $(JANSSON_CFLAGS) $(LDFLAGS) $(RESOURCE_SNAPSHOT_FLAGS) -o $@ tests/test_account_sinks_hot_status.c src/account_plan.c src/account_naming.c src/account_fs.c src/account_root.c src/fs.c src/naming.c src/credential.c src/topology.c src/topology_lookup.c src/connect_topic.c src/command.c $(CURL_LIBS) $(JANSSON_LIBS) $(LDLIBS)

.build/test_account_output_hot: tests/test_account_output_hot.c $(ACCOUNT_HOT_OUTPUT_SUPPORT)
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude -Isrc $(CURL_CFLAGS) $(JANSSON_CFLAGS) $(LDFLAGS) $(RESOURCE_SNAPSHOT_FLAGS) -o $@ tests/test_account_output_hot.c $(ACCOUNT_HOT_OUTPUT_SUPPORT) $(CURL_LIBS) $(JANSSON_LIBS) $(LDLIBS)

check-account-resources: $(ACCOUNT_RESOURCE_TARGETS)
	.build/test_credential_identity
	.build/test_account_root_hot_fault
	.build/test_account_fs_hot_fault
	HOT_FAULT= .build/test_account_sinks_hot
	HOT_FAULT=missing .build/test_account_sinks_hot
	HOT_FAULT=mismatch .build/test_account_sinks_hot
	HOT_FAULT=symlink .build/test_account_sinks_hot
	HOT_FAULT=postpair .build/test_account_sinks_hot
	HOT_FAULT=taint .build/test_account_sinks_hot
	.build/test_account_sinks_hot_bind
	.build/test_account_sinks_hot_status
	.build/test_account_output_hot

.PHONY: check-account-resources check-account-resources-valgrind

.build/test_account_sinks_hot_status_vg: tests/test_account_sinks_hot_status.c src/account_plan.c src/account_naming.c src/account_fs.c src/account_root.c src/fs.c src/naming.c src/credential.c src/topology.c src/topology_lookup.c src/connect_topic.c src/command.c
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude -Isrc $(CURL_CFLAGS) $(JANSSON_CFLAGS) $(LDFLAGS) -DGIN_STATUS_VALGRIND -o $@ tests/test_account_sinks_hot_status.c src/account_plan.c src/account_naming.c src/account_fs.c src/account_root.c src/fs.c src/naming.c src/credential.c src/topology.c src/topology_lookup.c src/connect_topic.c src/command.c $(CURL_LIBS) $(JANSSON_LIBS) $(LDLIBS)

.build/test_account_output_hot_vg: tests/test_account_output_hot.c $(ACCOUNT_HOT_OUTPUT_SUPPORT)
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude -Isrc $(CURL_CFLAGS) $(JANSSON_CFLAGS) $(LDFLAGS) -DGIN_OUTPUT_SNAPSHOT_VG -o $@ tests/test_account_output_hot.c $(ACCOUNT_HOT_OUTPUT_SUPPORT) $(CURL_LIBS) $(JANSSON_LIBS) $(LDLIBS)

check-account-resources-valgrind: $(ACCOUNT_RESOURCE_TARGETS) .build/test_account_sinks_hot_status_vg .build/test_account_output_hot_vg
	valgrind --quiet --trace-children=yes --error-exitcode=99 --leak-check=full --show-leak-kinds=all --errors-for-leak-kinds=all .build/test_credential_identity
	valgrind --quiet --trace-children=yes --error-exitcode=99 --leak-check=full --show-leak-kinds=all --errors-for-leak-kinds=all .build/test_account_root_hot_fault
	valgrind --quiet --trace-children=yes --error-exitcode=99 --leak-check=full --show-leak-kinds=all --errors-for-leak-kinds=all .build/test_account_fs_hot_fault
	HOT_FAULT= valgrind --quiet --trace-children=yes --error-exitcode=99 --leak-check=full --show-leak-kinds=all --errors-for-leak-kinds=all .build/test_account_sinks_hot
	HOT_FAULT=missing valgrind --quiet --trace-children=yes --error-exitcode=99 --leak-check=full --show-leak-kinds=all --errors-for-leak-kinds=all .build/test_account_sinks_hot
	HOT_FAULT=mismatch valgrind --quiet --trace-children=yes --error-exitcode=99 --leak-check=full --show-leak-kinds=all --errors-for-leak-kinds=all .build/test_account_sinks_hot
	HOT_FAULT=symlink valgrind --quiet --trace-children=yes --error-exitcode=99 --leak-check=full --show-leak-kinds=all --errors-for-leak-kinds=all .build/test_account_sinks_hot
	HOT_FAULT=postpair valgrind --quiet --trace-children=yes --error-exitcode=99 --leak-check=full --show-leak-kinds=all --errors-for-leak-kinds=all .build/test_account_sinks_hot
	HOT_FAULT=taint valgrind --quiet --trace-children=yes --error-exitcode=99 --leak-check=full --show-leak-kinds=all --errors-for-leak-kinds=all .build/test_account_sinks_hot
	valgrind --quiet --trace-children=yes --error-exitcode=99 --leak-check=full --show-leak-kinds=all --errors-for-leak-kinds=all .build/test_account_sinks_hot_bind
	valgrind --quiet --trace-children=yes --error-exitcode=99 --leak-check=full --show-leak-kinds=all --errors-for-leak-kinds=all .build/test_account_sinks_hot_status_vg
	valgrind --quiet --trace-children=yes --error-exitcode=99 --leak-check=full --show-leak-kinds=all --errors-for-leak-kinds=all .build/test_account_output_hot_vg

.build/account_cli_control_driver: include/gin_auth.h include/gin_command.h include/gin_config.h include/gin_connect.h include/gin_control.h include/gin_credential.h include/gin_fs.h include/gin_history.h include/gin_http.h include/gin_input.h include/gin_liveness.h include/gin_naming.h include/gin_phoenix.h include/gin_protocol.h include/gin_render.h include/gin_send.h include/gin_session.h include/gin_topology.h src/account_cli.c src/account_cli.h src/account_discovery.c src/account_discovery.h src/account_dispatch.c src/account_dispatch.h src/account_fs.c src/account_fs.h src/account_history_bootstrap.c src/account_history_bootstrap.h src/account_naming.c src/account_naming.h src/account_output.c src/account_output.h src/account_output_hot.h src/account_owner.c src/account_owner.h src/account_plan.c src/account_plan.h src/account_receive.c src/account_receive.h src/account_receive_hot.h src/account_registry.c src/account_registry.h src/account_root.c src/account_root.h src/account_sinks.c src/account_sinks.h src/account_sinks_hot.h src/auth.c src/checkpoint.c src/checkpoint.h src/command.c src/connect.c src/connect_control.h src/connect_hot.h src/connect_protocols.c src/connect_topic.c src/connect_values.h src/control.c src/credential.c src/credential_identity.h src/fs.c src/fs_private.h src/history_evidence.c src/history_evidence.h src/history_fetch.c src/history_page.c src/history_path.c src/history_recovery.c src/history_render.c src/history_values.h src/http.c src/http_control.h src/input.c src/interactive_login.h src/json_evidence.h src/liveness_backoff.c src/liveness_values.h src/naming.c src/phoenix.c src/phoenix_refs.c src/phoenix_stream.c src/protocol.c src/protocol_parse.c src/render.c src/render_escape.c src/render_time.c src/render_values.h src/send.c src/send_payload.c src/send_reply.c src/send_values.h src/session_live_projection.c src/session_live_projection.h src/session_live_queue.c src/session_live_queue.h src/terminal_password.c src/terminal_password.h src/topology.c src/topology_fetch.c src/topology_internal.h src/topology_lookup.c tests/account_cli_control_driver.c

.build/account_discovery_driver: include/gin_auth.h include/gin_command.h include/gin_connect.h include/gin_control.h include/gin_credential.h include/gin_fs.h include/gin_http.h include/gin_liveness.h include/gin_naming.h include/gin_phoenix.h include/gin_protocol.h include/gin_send.h include/gin_topology.h src/account_discovery.c src/account_discovery.h src/auth.c src/command.c src/connect.c src/connect_control.h src/connect_hot.h src/connect_protocols.c src/connect_topic.c src/connect_values.h src/credential.c src/credential_identity.h src/http.c src/http_control.h src/json_evidence.h src/liveness_backoff.c src/liveness_values.h src/naming.c src/phoenix.c src/phoenix_refs.c src/phoenix_stream.c src/protocol.c src/protocol_parse.c src/session_live_queue.c src/session_live_queue.h src/topology.c src/topology_fetch.c src/topology_internal.h src/topology_lookup.c tests/account_discovery_driver.c

.build/account_dispatch_driver: include/gin_auth.h include/gin_command.h include/gin_config.h include/gin_connect.h include/gin_control.h include/gin_credential.h include/gin_fs.h include/gin_history.h include/gin_http.h include/gin_liveness.h include/gin_naming.h include/gin_phoenix.h include/gin_protocol.h include/gin_render.h include/gin_send.h include/gin_session.h include/gin_topology.h src/account_dispatch.c src/account_dispatch.h src/account_naming.c src/account_naming.h src/account_plan.c src/account_plan.h src/account_receive.c src/account_receive.h src/account_receive_hot.h src/auth.c src/command.c src/connect.c src/connect_control.h src/connect_hot.h src/connect_protocols.c src/connect_topic.c src/connect_values.h src/credential.c src/credential_identity.h src/history_evidence.c src/history_evidence.h src/history_page.c src/history_values.h src/http.c src/http_control.h src/json_evidence.h src/liveness_backoff.c src/liveness_values.h src/naming.c src/phoenix.c src/phoenix_refs.c src/phoenix_stream.c src/protocol.c src/protocol_parse.c src/render.c src/render_escape.c src/render_time.c src/render_values.h src/session_live_projection.c src/session_live_projection.h src/session_live_queue.c src/session_live_queue.h src/topology.c src/topology_internal.h src/topology_lookup.c tests/account_dispatch_driver.c

.build/account_history_bootstrap_driver: include/gin_auth.h include/gin_command.h include/gin_config.h include/gin_connect.h include/gin_control.h include/gin_credential.h include/gin_fs.h include/gin_history.h include/gin_http.h include/gin_liveness.h include/gin_naming.h include/gin_phoenix.h include/gin_protocol.h include/gin_render.h include/gin_send.h include/gin_session.h include/gin_topology.h src/account_dispatch.c src/account_dispatch.h src/account_fs.c src/account_fs.h src/account_history_bootstrap.c src/account_history_bootstrap.h src/account_naming.c src/account_naming.h src/account_output.c src/account_output.h src/account_output_hot.h src/account_plan.c src/account_plan.h src/account_receive.c src/account_receive.h src/account_receive_hot.h src/account_registry.c src/account_registry.h src/account_root.c src/account_root.h src/account_sinks.c src/account_sinks.h src/account_sinks_hot.h src/auth.c src/checkpoint.c src/checkpoint.h src/command.c src/connect.c src/connect_control.h src/connect_hot.h src/connect_protocols.c src/connect_topic.c src/connect_values.h src/control.c src/credential.c src/credential_identity.h src/fs_private.h src/history_evidence.c src/history_evidence.h src/history_fetch.c src/history_page.c src/history_path.c src/history_recovery.c src/history_render.c src/history_values.h src/http.c src/http_control.h src/json_evidence.h src/liveness_backoff.c src/liveness_values.h src/naming.c src/phoenix.c src/phoenix_refs.c src/phoenix_stream.c src/protocol.c src/protocol_parse.c src/render.c src/render_escape.c src/render_time.c src/render_values.h src/send_reply.c src/send_values.h src/session_live_projection.c src/session_live_projection.h src/session_live_queue.c src/session_live_queue.h src/topology.c src/topology_internal.h src/topology_lookup.c tests/account_history_bootstrap_driver.c

.build/account_history_output_driver: include/gin_auth.h include/gin_command.h include/gin_config.h include/gin_connect.h include/gin_control.h include/gin_credential.h include/gin_fs.h include/gin_history.h include/gin_http.h include/gin_liveness.h include/gin_naming.h include/gin_phoenix.h include/gin_protocol.h include/gin_render.h include/gin_send.h include/gin_session.h include/gin_topology.h src/account_dispatch.c src/account_dispatch.h src/account_fs.c src/account_fs.h src/account_naming.c src/account_naming.h src/account_output.c src/account_output.h src/account_output_hot.h src/account_plan.c src/account_plan.h src/account_receive.c src/account_receive.h src/account_receive_hot.h src/account_registry.c src/account_registry.h src/account_root.c src/account_root.h src/account_sinks.c src/account_sinks.h src/account_sinks_hot.h src/auth.c src/checkpoint.c src/checkpoint.h src/command.c src/connect.c src/connect_control.h src/connect_hot.h src/connect_protocols.c src/connect_topic.c src/connect_values.h src/credential.c src/credential_identity.h src/fs_private.h src/history_evidence.c src/history_evidence.h src/history_page.c src/history_recovery.c src/history_render.c src/history_values.h src/http.c src/http_control.h src/json_evidence.h src/liveness_backoff.c src/liveness_values.h src/naming.c src/phoenix.c src/phoenix_refs.c src/phoenix_stream.c src/protocol.c src/protocol_parse.c src/render.c src/render_escape.c src/render_time.c src/render_values.h src/session_live_projection.c src/session_live_projection.h src/session_live_queue.c src/session_live_queue.h src/topology.c src/topology_internal.h src/topology_lookup.c tests/account_history_output_driver.c

.build/account_output_driver: include/gin_auth.h include/gin_command.h include/gin_config.h include/gin_connect.h include/gin_control.h include/gin_credential.h include/gin_fs.h include/gin_history.h include/gin_http.h include/gin_liveness.h include/gin_naming.h include/gin_phoenix.h include/gin_protocol.h include/gin_render.h include/gin_send.h include/gin_session.h include/gin_topology.h src/account_dispatch.c src/account_dispatch.h src/account_fs.c src/account_fs.h src/account_naming.c src/account_naming.h src/account_output.c src/account_output.h src/account_output_hot.h src/account_plan.c src/account_plan.h src/account_receive.c src/account_receive.h src/account_receive_hot.h src/account_registry.c src/account_registry.h src/account_root.c src/account_root.h src/account_sinks.c src/account_sinks.h src/account_sinks_hot.h src/auth.c src/checkpoint.c src/checkpoint.h src/command.c src/connect.c src/connect_control.h src/connect_hot.h src/connect_protocols.c src/connect_topic.c src/connect_values.h src/credential.c src/credential_identity.h src/fs_private.h src/history_evidence.c src/history_evidence.h src/history_page.c src/history_render.c src/history_values.h src/http.c src/http_control.h src/json_evidence.h src/liveness_backoff.c src/liveness_values.h src/naming.c src/phoenix.c src/phoenix_refs.c src/phoenix_stream.c src/protocol.c src/protocol_parse.c src/render.c src/render_escape.c src/render_time.c src/render_values.h src/session_live_projection.c src/session_live_projection.h src/session_live_queue.c src/session_live_queue.h src/topology.c src/topology_internal.h src/topology_lookup.c tests/account_output_driver.c

.build/account_owner_driver: include/gin_auth.h include/gin_command.h include/gin_config.h include/gin_connect.h include/gin_control.h include/gin_credential.h include/gin_fs.h include/gin_history.h include/gin_http.h include/gin_input.h include/gin_liveness.h include/gin_naming.h include/gin_phoenix.h include/gin_protocol.h include/gin_render.h include/gin_send.h include/gin_session.h include/gin_topology.h src/account_dispatch.c src/account_dispatch.h src/account_fs.c src/account_fs.h src/account_history_bootstrap.c src/account_history_bootstrap.h src/account_naming.c src/account_naming.h src/account_output.c src/account_output.h src/account_output_hot.h src/account_owner.c src/account_owner.h src/account_plan.c src/account_plan.h src/account_receive.c src/account_receive.h src/account_receive_hot.h src/account_registry.c src/account_registry.h src/account_root.c src/account_root.h src/account_sinks.c src/account_sinks.h src/account_sinks_hot.h src/auth.c src/checkpoint.c src/checkpoint.h src/command.c src/connect.c src/connect_control.h src/connect_hot.h src/connect_protocols.c src/connect_topic.c src/connect_values.h src/control.c src/credential.c src/credential_identity.h src/fs_private.h src/history_evidence.c src/history_evidence.h src/history_fetch.c src/history_page.c src/history_path.c src/history_recovery.c src/history_render.c src/history_values.h src/http.c src/http_control.h src/input.c src/json_evidence.h src/liveness_backoff.c src/liveness_values.h src/naming.c src/phoenix.c src/phoenix_refs.c src/phoenix_stream.c src/protocol.c src/protocol_parse.c src/render.c src/render_escape.c src/render_time.c src/render_values.h src/send.c src/send_payload.c src/send_reply.c src/send_values.h src/session_live_projection.c src/session_live_projection.h src/session_live_queue.c src/session_live_queue.h src/topology.c src/topology_internal.h src/topology_lookup.c tests/account_owner_driver.c

.build/account_receive_driver: include/gin_auth.h include/gin_command.h include/gin_connect.h include/gin_control.h include/gin_credential.h include/gin_fs.h include/gin_http.h include/gin_liveness.h include/gin_naming.h include/gin_phoenix.h include/gin_protocol.h include/gin_send.h include/gin_topology.h src/account_naming.c src/account_naming.h src/account_plan.c src/account_plan.h src/account_receive.c src/account_receive.h src/account_receive_hot.h src/auth.c src/command.c src/connect.c src/connect_control.h src/connect_hot.h src/connect_protocols.c src/connect_topic.c src/connect_values.h src/credential.c src/credential_identity.h src/fs.c src/fs_private.h src/http.c src/http_control.h src/json_evidence.h src/liveness_backoff.c src/liveness_values.h src/naming.c src/phoenix.c src/phoenix_refs.c src/phoenix_stream.c src/protocol.c src/protocol_parse.c src/session_live_queue.c src/session_live_queue.h src/topology.c src/topology_internal.h src/topology_lookup.c tests/account_receive_driver.c

.build/account_receive_hot_wss_driver: include/gin_auth.h include/gin_command.h include/gin_connect.h include/gin_control.h include/gin_credential.h include/gin_fs.h include/gin_http.h include/gin_liveness.h include/gin_naming.h include/gin_phoenix.h include/gin_protocol.h include/gin_send.h include/gin_topology.h src/account_naming.c src/account_naming.h src/account_plan.c src/account_plan.h src/account_receive.c src/account_receive.h src/account_receive_hot.h src/auth.c src/command.c src/connect.c src/connect_control.h src/connect_hot.h src/connect_protocols.c src/connect_topic.c src/connect_values.h src/credential.c src/credential_identity.h src/fs.c src/fs_private.h src/http.c src/http_control.h src/json_evidence.h src/liveness_backoff.c src/liveness_values.h src/naming.c src/phoenix.c src/phoenix_refs.c src/phoenix_stream.c src/protocol.c src/protocol_parse.c src/session_live_queue.c src/session_live_queue.h src/topology.c src/topology_internal.h src/topology_lookup.c tests/account_receive_hot_wss_driver.c

.build/auth_driver: include/gin_auth.h include/gin_control.h include/gin_credential.h include/gin_fs.h include/gin_http.h include/gin_naming.h include/gin_protocol.h include/gin_send.h include/gin_topology.h src/auth.c src/credential.c src/credential_identity.h src/fs.c src/fs_private.h src/http.c src/http_control.h src/json_evidence.h src/naming.c tests/auth_driver.c

.build/auth_login_control_driver: include/gin_auth.h include/gin_control.h include/gin_credential.h include/gin_fs.h include/gin_http.h include/gin_naming.h include/gin_protocol.h include/gin_send.h include/gin_topology.h src/auth.c src/credential.c src/credential_identity.h src/fs.c src/fs_private.h src/http.c src/http_control.h src/json_evidence.h src/naming.c tests/auth_login_control_driver.c

.build/connect_driver: include/gin_auth.h include/gin_command.h include/gin_connect.h include/gin_control.h include/gin_credential.h include/gin_fs.h include/gin_http.h include/gin_liveness.h include/gin_naming.h include/gin_phoenix.h include/gin_protocol.h include/gin_send.h include/gin_topology.h src/auth.c src/command.c src/connect.c src/connect_control.h src/connect_hot.h src/connect_protocols.c src/connect_topic.c src/connect_values.h src/credential.c src/credential_identity.h src/http.c src/http_control.h src/json_evidence.h src/liveness_backoff.c src/liveness_values.h src/naming.c src/phoenix.c src/phoenix_refs.c src/phoenix_stream.c src/protocol.c src/protocol_parse.c src/topology.c src/topology_internal.h src/topology_lookup.c tests/connect_driver.c

.build/connect_hot_wss_driver: include/gin_auth.h include/gin_command.h include/gin_connect.h include/gin_control.h include/gin_credential.h include/gin_fs.h include/gin_http.h include/gin_liveness.h include/gin_naming.h include/gin_phoenix.h include/gin_protocol.h include/gin_send.h include/gin_topology.h src/auth.c src/command.c src/connect.c src/connect_control.h src/connect_hot.h src/connect_protocols.c src/connect_topic.c src/connect_values.h src/credential.c src/credential_identity.h src/http.c src/http_control.h src/json_evidence.h src/liveness_backoff.c src/liveness_values.h src/naming.c src/phoenix.c src/phoenix_refs.c src/phoenix_stream.c src/protocol.c src/protocol_parse.c src/topology.c src/topology_internal.h src/topology_lookup.c tests/connect_hot_wss_driver.c

.build/connect_replan_wss_driver: include/gin_auth.h include/gin_command.h include/gin_connect.h include/gin_control.h include/gin_credential.h include/gin_fs.h include/gin_http.h include/gin_liveness.h include/gin_naming.h include/gin_phoenix.h include/gin_protocol.h include/gin_send.h include/gin_topology.h src/auth.c src/command.c src/connect.c src/connect_control.h src/connect_hot.h src/connect_protocols.c src/connect_topic.c src/connect_values.h src/credential.c src/credential_identity.h src/fs.c src/fs_private.h src/http.c src/http_control.h src/json_evidence.h src/liveness_backoff.c src/liveness_values.h src/naming.c src/phoenix.c src/phoenix_refs.c src/phoenix_stream.c src/protocol.c src/protocol_parse.c src/topology.c src/topology_fetch.c src/topology_internal.h src/topology_lookup.c tests/connect_replan_wss_driver.c

.build/control_driver: include/gin_auth.h include/gin_command.h include/gin_control.h include/gin_credential.h include/gin_fs.h include/gin_http.h include/gin_input.h include/gin_naming.h include/gin_protocol.h include/gin_send.h include/gin_topology.h src/auth.c src/command.c src/control.c src/credential.c src/credential_identity.h src/fs.c src/fs_private.h src/http.c src/http_control.h src/input.c src/json_evidence.h src/naming.c src/protocol.c src/protocol_parse.c src/send.c src/send_payload.c src/send_reply.c src/send_values.h tests/control_driver.c

.build/credential_driver: include/gin_credential.h include/gin_fs.h include/gin_naming.h src/credential.c src/credential_identity.h src/fs.c src/fs_private.h src/json_evidence.h src/naming.c tests/credential_driver.c

.build/gin: include/gin_auth.h include/gin_command.h include/gin_config.h include/gin_connect.h include/gin_control.h include/gin_credential.h include/gin_fs.h include/gin_history.h include/gin_http.h include/gin_input.h include/gin_liveness.h include/gin_naming.h include/gin_phoenix.h include/gin_protocol.h include/gin_render.h include/gin_runtime.h include/gin_send.h include/gin_session.h include/gin_topology.h src/account_cli.c src/account_cli.h src/account_discovery.c src/account_discovery.h src/account_dispatch.c src/account_dispatch.h src/account_fs.c src/account_fs.h src/account_history_bootstrap.c src/account_history_bootstrap.h src/account_naming.c src/account_naming.h src/account_output.c src/account_output.h src/account_output_hot.h src/account_owner.c src/account_owner.h src/account_plan.c src/account_plan.h src/account_receive.c src/account_receive.h src/account_receive_hot.h src/account_registry.c src/account_registry.h src/account_root.c src/account_root.h src/account_sinks.c src/account_sinks.h src/account_sinks_hot.h src/auth.c src/checkpoint.c src/checkpoint.h src/command.c src/config.c src/connect.c src/connect_control.h src/connect_hot.h src/connect_protocols.c src/connect_topic.c src/connect_values.h src/control.c src/credential.c src/credential_identity.h src/fs.c src/fs_private.h src/gin.c src/history_evidence.c src/history_evidence.h src/history_fetch.c src/history_page.c src/history_path.c src/history_recovery.c src/history_render.c src/history_values.h src/http.c src/http_control.h src/input.c src/interactive_login.c src/interactive_login.h src/json_evidence.h src/liveness_backoff.c src/liveness_values.h src/naming.c src/phoenix.c src/phoenix_refs.c src/phoenix_stream.c src/protocol.c src/protocol_parse.c src/render.c src/render_escape.c src/render_time.c src/render_values.h src/runtime.c src/send.c src/send_payload.c src/send_reply.c src/send_values.h src/session.c src/session_config.c src/session_live_projection.c src/session_live_projection.h src/session_live_queue.c src/session_live_queue.h src/session_operation.c src/session_operation.h src/terminal_password.c src/terminal_password.h src/topology.c src/topology_fetch.c src/topology_internal.h src/topology_lookup.c

.build/gin_account_fifo_observer: include/gin_auth.h include/gin_command.h include/gin_config.h include/gin_connect.h include/gin_control.h include/gin_credential.h include/gin_fs.h include/gin_history.h include/gin_http.h include/gin_input.h include/gin_liveness.h include/gin_naming.h include/gin_phoenix.h include/gin_protocol.h include/gin_render.h include/gin_runtime.h include/gin_send.h include/gin_session.h include/gin_topology.h src/account_cli.c src/account_cli.h src/account_discovery.c src/account_discovery.h src/account_dispatch.c src/account_dispatch.h src/account_fs.c src/account_fs.h src/account_history_bootstrap.c src/account_history_bootstrap.h src/account_naming.c src/account_naming.h src/account_output.c src/account_output.h src/account_output_hot.h src/account_owner.c src/account_owner.h src/account_plan.c src/account_plan.h src/account_receive.c src/account_receive.h src/account_receive_hot.h src/account_registry.c src/account_registry.h src/account_root.c src/account_root.h src/account_sinks.c src/account_sinks.h src/account_sinks_hot.h src/auth.c src/checkpoint.c src/checkpoint.h src/command.c src/config.c src/connect.c src/connect_control.h src/connect_hot.h src/connect_protocols.c src/connect_topic.c src/connect_values.h src/control.c src/credential.c src/credential_identity.h src/fs.c src/fs_private.h src/gin.c src/history_evidence.c src/history_evidence.h src/history_fetch.c src/history_page.c src/history_path.c src/history_recovery.c src/history_render.c src/history_values.h src/http.c src/http_control.h src/input.c src/interactive_login.c src/interactive_login.h src/json_evidence.h src/liveness_backoff.c src/liveness_values.h src/naming.c src/phoenix.c src/phoenix_refs.c src/phoenix_stream.c src/protocol.c src/protocol_parse.c src/render.c src/render_escape.c src/render_time.c src/render_values.h src/runtime.c src/send.c src/send_payload.c src/send_reply.c src/send_values.h src/session.c src/session_config.c src/session_live_projection.c src/session_live_projection.h src/session_live_queue.c src/session_live_queue.h src/session_operation.c src/session_operation.h src/terminal_password.c src/terminal_password.h src/topology.c src/topology_fetch.c src/topology_internal.h src/topology_lookup.c tests/account_fifo_send_observer.c

.build/gin_cleanup_fault: include/gin_auth.h include/gin_command.h include/gin_config.h include/gin_connect.h include/gin_control.h include/gin_credential.h include/gin_fs.h include/gin_history.h include/gin_http.h include/gin_input.h include/gin_liveness.h include/gin_naming.h include/gin_phoenix.h include/gin_protocol.h include/gin_render.h include/gin_runtime.h include/gin_send.h include/gin_session.h include/gin_topology.h src/account_cli.c src/account_cli.h src/account_discovery.c src/account_discovery.h src/account_dispatch.c src/account_dispatch.h src/account_fs.c src/account_fs.h src/account_history_bootstrap.c src/account_history_bootstrap.h src/account_naming.c src/account_naming.h src/account_output.c src/account_output.h src/account_output_hot.h src/account_owner.c src/account_owner.h src/account_plan.c src/account_plan.h src/account_receive.c src/account_receive.h src/account_receive_hot.h src/account_registry.c src/account_registry.h src/account_root.c src/account_root.h src/account_sinks.c src/account_sinks.h src/account_sinks_hot.h src/auth.c src/checkpoint.c src/checkpoint.h src/command.c src/config.c src/connect.c src/connect_control.h src/connect_hot.h src/connect_protocols.c src/connect_topic.c src/connect_values.h src/control.c src/credential.c src/credential_identity.h src/fs.c src/fs_private.h src/gin.c src/history_evidence.c src/history_evidence.h src/history_fetch.c src/history_page.c src/history_path.c src/history_recovery.c src/history_render.c src/history_values.h src/http.c src/http_control.h src/input.c src/interactive_login.c src/interactive_login.h src/json_evidence.h src/liveness_backoff.c src/liveness_values.h src/naming.c src/phoenix.c src/phoenix_refs.c src/phoenix_stream.c src/protocol.c src/protocol_parse.c src/render.c src/render_escape.c src/render_time.c src/render_values.h src/runtime.c src/send.c src/send_payload.c src/send_reply.c src/send_values.h src/session.c src/session_config.c src/session_live_projection.c src/session_live_projection.h src/session_live_queue.c src/session_live_queue.h src/session_operation.c src/session_operation.h src/terminal_password.c src/terminal_password.h src/topology.c src/topology_fetch.c src/topology_internal.h src/topology_lookup.c tests/connect_cleanup_fault_driver.c

.build/gin_owner_fault: include/gin_auth.h include/gin_command.h include/gin_config.h include/gin_connect.h include/gin_control.h include/gin_credential.h include/gin_fs.h include/gin_history.h include/gin_http.h include/gin_input.h include/gin_liveness.h include/gin_naming.h include/gin_phoenix.h include/gin_protocol.h include/gin_render.h include/gin_runtime.h include/gin_send.h include/gin_session.h include/gin_topology.h src/account_cli.c src/account_cli.h src/account_discovery.c src/account_discovery.h src/account_dispatch.c src/account_dispatch.h src/account_fs.c src/account_fs.h src/account_history_bootstrap.c src/account_history_bootstrap.h src/account_naming.c src/account_naming.h src/account_output.c src/account_output.h src/account_output_hot.h src/account_owner.c src/account_owner.h src/account_plan.c src/account_plan.h src/account_receive.c src/account_receive.h src/account_receive_hot.h src/account_registry.c src/account_registry.h src/account_root.c src/account_root.h src/account_sinks.c src/account_sinks.h src/account_sinks_hot.h src/auth.c src/checkpoint.c src/checkpoint.h src/command.c src/config.c src/connect.c src/connect_control.h src/connect_hot.h src/connect_protocols.c src/connect_topic.c src/connect_values.h src/control.c src/credential.c src/credential_identity.h src/fs.c src/fs_private.h src/gin.c src/history_evidence.c src/history_evidence.h src/history_fetch.c src/history_page.c src/history_path.c src/history_recovery.c src/history_render.c src/history_values.h src/http.c src/http_control.h src/input.c src/interactive_login.c src/interactive_login.h src/json_evidence.h src/liveness_backoff.c src/liveness_values.h src/naming.c src/phoenix.c src/phoenix_refs.c src/phoenix_stream.c src/protocol.c src/protocol_parse.c src/render.c src/render_escape.c src/render_time.c src/render_values.h src/runtime.c src/send.c src/send_payload.c src/send_reply.c src/send_values.h src/session.c src/session_config.c src/session_live_projection.c src/session_live_projection.h src/session_live_queue.c src/session_live_queue.h src/session_operation.c src/session_operation.h src/terminal_password.c src/terminal_password.h src/topology.c src/topology_fetch.c src/topology_internal.h src/topology_lookup.c tests/session_owner_fault_driver.c

.build/gin_receive_observer: include/gin_auth.h include/gin_command.h include/gin_config.h include/gin_connect.h include/gin_control.h include/gin_credential.h include/gin_fs.h include/gin_history.h include/gin_http.h include/gin_input.h include/gin_liveness.h include/gin_naming.h include/gin_phoenix.h include/gin_protocol.h include/gin_render.h include/gin_runtime.h include/gin_send.h include/gin_session.h include/gin_topology.h src/account_cli.c src/account_cli.h src/account_discovery.c src/account_discovery.h src/account_dispatch.c src/account_dispatch.h src/account_fs.c src/account_fs.h src/account_history_bootstrap.c src/account_history_bootstrap.h src/account_naming.c src/account_naming.h src/account_output.c src/account_output.h src/account_output_hot.h src/account_owner.c src/account_owner.h src/account_plan.c src/account_plan.h src/account_receive.c src/account_receive.h src/account_receive_hot.h src/account_registry.c src/account_registry.h src/account_root.c src/account_root.h src/account_sinks.c src/account_sinks.h src/account_sinks_hot.h src/auth.c src/checkpoint.c src/checkpoint.h src/command.c src/config.c src/connect.c src/connect_control.h src/connect_hot.h src/connect_protocols.c src/connect_topic.c src/connect_values.h src/control.c src/credential.c src/credential_identity.h src/fs.c src/fs_private.h src/gin.c src/history_evidence.c src/history_evidence.h src/history_fetch.c src/history_page.c src/history_path.c src/history_recovery.c src/history_render.c src/history_values.h src/http.c src/http_control.h src/input.c src/interactive_login.c src/interactive_login.h src/json_evidence.h src/liveness_backoff.c src/liveness_values.h src/naming.c src/phoenix.c src/phoenix_refs.c src/phoenix_stream.c src/protocol.c src/protocol_parse.c src/render.c src/render_escape.c src/render_time.c src/render_values.h src/runtime.c src/send.c src/send_payload.c src/send_reply.c src/send_values.h src/session.c src/session_config.c src/session_live_projection.c src/session_live_projection.h src/session_live_queue.c src/session_live_queue.h src/session_operation.c src/session_operation.h src/terminal_password.c src/terminal_password.h src/topology.c src/topology_fetch.c src/topology_internal.h src/topology_lookup.c tests/connect_receive_observer.c

.build/history_control_driver: include/gin_auth.h include/gin_command.h include/gin_connect.h include/gin_control.h include/gin_credential.h include/gin_fs.h include/gin_history.h include/gin_http.h include/gin_liveness.h include/gin_naming.h include/gin_phoenix.h include/gin_protocol.h include/gin_render.h include/gin_send.h include/gin_topology.h src/auth.c src/command.c src/connect.c src/connect_control.h src/connect_hot.h src/connect_protocols.c src/connect_topic.c src/connect_values.h src/credential.c src/credential_identity.h src/fs.c src/fs_private.h src/history_evidence.c src/history_evidence.h src/history_fetch.c src/history_page.c src/history_path.c src/history_recovery.c src/history_render.c src/history_values.h src/http.c src/http_control.h src/json_evidence.h src/liveness_backoff.c src/liveness_values.h src/naming.c src/phoenix.c src/phoenix_refs.c src/phoenix_stream.c src/protocol.c src/protocol_parse.c src/render.c src/render_escape.c src/render_time.c src/render_values.h src/topology.c src/topology_internal.h src/topology_lookup.c tests/history_control_driver.c tests/history_http_driver.c

.build/history_driver: include/gin_auth.h include/gin_command.h include/gin_connect.h include/gin_control.h include/gin_credential.h include/gin_fs.h include/gin_history.h include/gin_http.h include/gin_liveness.h include/gin_naming.h include/gin_phoenix.h include/gin_protocol.h include/gin_render.h include/gin_send.h include/gin_topology.h src/auth.c src/command.c src/connect.c src/connect_control.h src/connect_hot.h src/connect_protocols.c src/connect_topic.c src/connect_values.h src/credential.c src/credential_identity.h src/fs.c src/fs_private.h src/history_evidence.c src/history_evidence.h src/history_fetch.c src/history_page.c src/history_path.c src/history_recovery.c src/history_render.c src/history_values.h src/http.c src/http_control.h src/json_evidence.h src/liveness_backoff.c src/liveness_values.h src/naming.c src/phoenix.c src/phoenix_refs.c src/phoenix_stream.c src/protocol.c src/protocol_parse.c src/render.c src/render_escape.c src/render_time.c src/render_values.h src/topology.c src/topology_internal.h src/topology_lookup.c tests/history_driver.c

.build/history_http_driver: include/gin_auth.h include/gin_command.h include/gin_connect.h include/gin_control.h include/gin_credential.h include/gin_fs.h include/gin_history.h include/gin_http.h include/gin_liveness.h include/gin_naming.h include/gin_phoenix.h include/gin_protocol.h include/gin_render.h include/gin_send.h include/gin_topology.h src/auth.c src/command.c src/connect.c src/connect_control.h src/connect_hot.h src/connect_protocols.c src/connect_topic.c src/connect_values.h src/credential.c src/credential_identity.h src/fs.c src/fs_private.h src/history_evidence.c src/history_evidence.h src/history_fetch.c src/history_page.c src/history_path.c src/history_recovery.c src/history_render.c src/history_values.h src/http.c src/http_control.h src/json_evidence.h src/liveness_backoff.c src/liveness_values.h src/naming.c src/phoenix.c src/phoenix_refs.c src/phoenix_stream.c src/protocol.c src/protocol_parse.c src/render.c src/render_escape.c src/render_time.c src/render_values.h src/topology.c src/topology_internal.h src/topology_lookup.c tests/history_http_driver.c

.build/interactive_login_driver: include/gin_auth.h include/gin_control.h include/gin_credential.h include/gin_fs.h include/gin_http.h include/gin_naming.h include/gin_protocol.h include/gin_send.h include/gin_topology.h src/auth.c src/credential.c src/credential_identity.h src/fs.c src/fs_private.h src/http.c src/http_control.h src/interactive_login.c src/interactive_login.h src/json_evidence.h src/naming.c src/protocol.c src/protocol_parse.c src/terminal_password.c src/terminal_password.h tests/interactive_login_driver.c

.build/liveness_driver: include/gin_auth.h include/gin_command.h include/gin_connect.h include/gin_control.h include/gin_credential.h include/gin_fs.h include/gin_http.h include/gin_liveness.h include/gin_naming.h include/gin_phoenix.h include/gin_protocol.h include/gin_send.h include/gin_topology.h src/auth.c src/command.c src/connect.c src/connect_control.h src/connect_hot.h src/connect_protocols.c src/connect_topic.c src/connect_values.h src/credential.c src/credential_identity.h src/http.c src/http_control.h src/json_evidence.h src/liveness_backoff.c src/liveness_values.h src/naming.c src/phoenix.c src/phoenix_refs.c src/phoenix_stream.c src/protocol.c src/protocol_parse.c src/topology.c src/topology_internal.h src/topology_lookup.c tests/liveness_driver.c

.build/protocol_driver: include/gin_auth.h include/gin_control.h include/gin_credential.h include/gin_fs.h include/gin_http.h include/gin_naming.h include/gin_protocol.h include/gin_send.h include/gin_topology.h src/auth.c src/credential.c src/credential_identity.h src/http.c src/http_control.h src/json_evidence.h src/protocol.c src/protocol_parse.c tests/protocol_driver.c

.build/query_driver: include/gin_auth.h include/gin_command.h include/gin_connect.h include/gin_control.h include/gin_credential.h include/gin_fs.h include/gin_http.h include/gin_input.h include/gin_liveness.h include/gin_naming.h include/gin_phoenix.h include/gin_protocol.h include/gin_send.h include/gin_topology.h src/auth.c src/command.c src/connect.c src/connect_control.h src/connect_hot.h src/connect_protocols.c src/connect_topic.c src/connect_values.h src/credential.c src/credential_identity.h src/fs.c src/fs_private.h src/http.c src/http_control.h src/input.c src/json_evidence.h src/liveness_backoff.c src/liveness_values.h src/naming.c src/phoenix.c src/phoenix_refs.c src/phoenix_stream.c src/protocol.c src/protocol_parse.c src/topology.c src/topology_internal.h src/topology_lookup.c tests/query_driver.c

.build/render_driver: include/gin_auth.h include/gin_command.h include/gin_connect.h include/gin_control.h include/gin_credential.h include/gin_fs.h include/gin_http.h include/gin_liveness.h include/gin_naming.h include/gin_phoenix.h include/gin_protocol.h include/gin_render.h include/gin_send.h include/gin_topology.h src/auth.c src/command.c src/connect.c src/connect_control.h src/connect_hot.h src/connect_protocols.c src/connect_topic.c src/connect_values.h src/credential.c src/credential_identity.h src/fs.c src/fs_private.h src/http.c src/http_control.h src/json_evidence.h src/liveness_backoff.c src/liveness_values.h src/naming.c src/phoenix.c src/phoenix_refs.c src/phoenix_stream.c src/protocol.c src/protocol_parse.c src/render.c src/render_escape.c src/render_time.c src/render_values.h src/topology.c src/topology_internal.h src/topology_lookup.c tests/render_driver.c

.build/runtime_stderr: include/gin_auth.h include/gin_command.h include/gin_config.h include/gin_connect.h include/gin_control.h include/gin_credential.h include/gin_fs.h include/gin_history.h include/gin_http.h include/gin_input.h include/gin_liveness.h include/gin_naming.h include/gin_phoenix.h include/gin_protocol.h include/gin_render.h include/gin_runtime.h include/gin_send.h include/gin_session.h include/gin_topology.h src/account_cli.c src/account_cli.h src/account_discovery.c src/account_discovery.h src/account_dispatch.c src/account_dispatch.h src/account_fs.c src/account_fs.h src/account_history_bootstrap.c src/account_history_bootstrap.h src/account_naming.c src/account_naming.h src/account_output.c src/account_output.h src/account_output_hot.h src/account_owner.c src/account_owner.h src/account_plan.c src/account_plan.h src/account_receive.c src/account_receive.h src/account_receive_hot.h src/account_registry.c src/account_registry.h src/account_root.c src/account_root.h src/account_sinks.c src/account_sinks.h src/account_sinks_hot.h src/auth.c src/checkpoint.c src/checkpoint.h src/command.c src/config.c src/connect.c src/connect_control.h src/connect_hot.h src/connect_protocols.c src/connect_topic.c src/connect_values.h src/control.c src/credential.c src/credential_identity.h src/fs.c src/fs_private.h src/history_evidence.c src/history_evidence.h src/history_fetch.c src/history_page.c src/history_path.c src/history_recovery.c src/history_render.c src/history_values.h src/http.c src/http_control.h src/input.c src/interactive_login.c src/interactive_login.h src/json_evidence.h src/liveness_backoff.c src/liveness_values.h src/naming.c src/phoenix.c src/phoenix_refs.c src/phoenix_stream.c src/protocol.c src/protocol_parse.c src/render.c src/render_escape.c src/render_time.c src/render_values.h src/runtime.c src/send.c src/send_payload.c src/send_reply.c src/send_values.h src/session.c src/session_config.c src/session_live_projection.c src/session_live_projection.h src/session_live_queue.c src/session_live_queue.h src/session_operation.c src/session_operation.h src/terminal_password.c src/terminal_password.h src/topology.c src/topology_fetch.c src/topology_internal.h src/topology_lookup.c tests/runtime_stderr.c

.build/send_driver: include/gin_auth.h include/gin_command.h include/gin_connect.h include/gin_control.h include/gin_credential.h include/gin_fs.h include/gin_http.h include/gin_input.h include/gin_liveness.h include/gin_naming.h include/gin_phoenix.h include/gin_protocol.h include/gin_render.h include/gin_send.h include/gin_topology.h src/auth.c src/command.c src/connect.c src/connect_control.h src/connect_hot.h src/connect_protocols.c src/connect_topic.c src/connect_values.h src/credential.c src/credential_identity.h src/fs.c src/fs_private.h src/http.c src/http_control.h src/input.c src/json_evidence.h src/liveness_backoff.c src/liveness_values.h src/naming.c src/phoenix.c src/phoenix_refs.c src/phoenix_stream.c src/protocol.c src/protocol_parse.c src/render.c src/render_escape.c src/render_time.c src/render_values.h src/send.c src/send_payload.c src/send_reply.c src/send_values.h src/topology.c src/topology_internal.h src/topology_lookup.c tests/send_driver.c

.build/test_account_dispatch_fidelity: include/gin_auth.h include/gin_command.h include/gin_config.h include/gin_connect.h include/gin_control.h include/gin_credential.h include/gin_fs.h include/gin_history.h include/gin_http.h include/gin_liveness.h include/gin_naming.h include/gin_phoenix.h include/gin_protocol.h include/gin_render.h include/gin_send.h include/gin_session.h include/gin_topology.h src/account_dispatch.c src/account_dispatch.h src/account_naming.c src/account_naming.h src/account_plan.c src/account_plan.h src/account_receive.c src/account_receive.h src/account_receive_hot.h src/auth.c src/command.c src/connect.c src/connect_control.h src/connect_hot.h src/connect_protocols.c src/connect_topic.c src/connect_values.h src/credential.c src/credential_identity.h src/history_evidence.c src/history_evidence.h src/history_page.c src/history_values.h src/http.c src/http_control.h src/json_evidence.h src/liveness_backoff.c src/liveness_values.h src/naming.c src/phoenix.c src/phoenix_refs.c src/phoenix_stream.c src/protocol.c src/protocol_parse.c src/render.c src/render_escape.c src/render_time.c src/render_values.h src/session_live_projection.c src/session_live_projection.h src/session_live_queue.c src/session_live_queue.h src/topology.c src/topology_internal.h src/topology_lookup.c tests/account_dispatch_driver.c tests/test_account_dispatch_fidelity.c

.build/test_account_output_hot: include/gin_auth.h include/gin_command.h include/gin_config.h include/gin_connect.h include/gin_control.h include/gin_credential.h include/gin_fs.h include/gin_history.h include/gin_http.h include/gin_liveness.h include/gin_naming.h include/gin_phoenix.h include/gin_protocol.h include/gin_render.h include/gin_send.h include/gin_session.h include/gin_topology.h src/account_dispatch.c src/account_dispatch.h src/account_fs.c src/account_fs.h src/account_naming.c src/account_naming.h src/account_output.c src/account_output.h src/account_output_hot.h src/account_plan.c src/account_plan.h src/account_receive.c src/account_receive.h src/account_receive_hot.h src/account_registry.c src/account_registry.h src/account_root.c src/account_root.h src/account_sinks.c src/account_sinks.h src/account_sinks_hot.h src/auth.c src/checkpoint.c src/checkpoint.h src/command.c src/connect.c src/connect_control.h src/connect_hot.h src/connect_protocols.c src/connect_topic.c src/connect_values.h src/credential.c src/credential_identity.h src/fs_private.h src/history_evidence.c src/history_evidence.h src/history_page.c src/history_render.c src/history_values.h src/http.c src/http_control.h src/json_evidence.h src/liveness_backoff.c src/liveness_values.h src/naming.c src/phoenix.c src/phoenix_refs.c src/phoenix_stream.c src/protocol.c src/protocol_parse.c src/render.c src/render_escape.c src/render_time.c src/render_values.h src/session_live_projection.c src/session_live_projection.h src/session_live_queue.c src/session_live_queue.h src/topology.c src/topology_internal.h src/topology_lookup.c tests/test_account_output_hot.c

.build/test_account_output_hot_vg: include/gin_auth.h include/gin_command.h include/gin_config.h include/gin_connect.h include/gin_control.h include/gin_credential.h include/gin_fs.h include/gin_history.h include/gin_http.h include/gin_liveness.h include/gin_naming.h include/gin_phoenix.h include/gin_protocol.h include/gin_render.h include/gin_send.h include/gin_session.h include/gin_topology.h src/account_dispatch.c src/account_dispatch.h src/account_fs.c src/account_fs.h src/account_naming.c src/account_naming.h src/account_output.c src/account_output.h src/account_output_hot.h src/account_plan.c src/account_plan.h src/account_receive.c src/account_receive.h src/account_receive_hot.h src/account_registry.c src/account_registry.h src/account_root.c src/account_root.h src/account_sinks.c src/account_sinks.h src/account_sinks_hot.h src/auth.c src/checkpoint.c src/checkpoint.h src/command.c src/connect.c src/connect_control.h src/connect_hot.h src/connect_protocols.c src/connect_topic.c src/connect_values.h src/credential.c src/credential_identity.h src/fs_private.h src/history_evidence.c src/history_evidence.h src/history_page.c src/history_render.c src/history_values.h src/http.c src/http_control.h src/json_evidence.h src/liveness_backoff.c src/liveness_values.h src/naming.c src/phoenix.c src/phoenix_refs.c src/phoenix_stream.c src/protocol.c src/protocol_parse.c src/render.c src/render_escape.c src/render_time.c src/render_values.h src/session_live_projection.c src/session_live_projection.h src/session_live_queue.c src/session_live_queue.h src/topology.c src/topology_internal.h src/topology_lookup.c tests/test_account_output_hot.c

.build/test_account_plan: include/gin_auth.h include/gin_command.h include/gin_connect.h include/gin_control.h include/gin_credential.h include/gin_fs.h include/gin_http.h include/gin_liveness.h include/gin_naming.h include/gin_phoenix.h include/gin_protocol.h include/gin_send.h include/gin_topology.h src/account_naming.c src/account_naming.h src/account_plan.c src/account_plan.h src/auth.c src/command.c src/connect.c src/connect_control.h src/connect_hot.h src/connect_protocols.c src/connect_topic.c src/connect_values.h src/credential.c src/credential_identity.h src/http.c src/http_control.h src/json_evidence.h src/liveness_backoff.c src/liveness_values.h src/naming.c src/phoenix.c src/phoenix_refs.c src/phoenix_stream.c src/protocol.c src/protocol_parse.c src/topology.c src/topology_internal.h src/topology_lookup.c tests/test_account_plan.c

.build/test_account_plan_fault: include/gin_auth.h include/gin_command.h include/gin_connect.h include/gin_control.h include/gin_credential.h include/gin_fs.h include/gin_http.h include/gin_liveness.h include/gin_naming.h include/gin_phoenix.h include/gin_protocol.h include/gin_send.h include/gin_topology.h src/account_naming.c src/account_naming.h src/account_plan.c src/account_plan.h src/auth.c src/command.c src/connect.c src/connect_control.h src/connect_hot.h src/connect_protocols.c src/connect_topic.c src/connect_values.h src/credential.c src/credential_identity.h src/http.c src/http_control.h src/json_evidence.h src/liveness_backoff.c src/liveness_values.h src/naming.c src/phoenix.c src/phoenix_refs.c src/phoenix_stream.c src/protocol.c src/protocol_parse.c src/topology.c src/topology_internal.h src/topology_lookup.c tests/test_account_plan.c

.build/test_account_receive_control: include/gin_auth.h include/gin_command.h include/gin_connect.h include/gin_control.h include/gin_credential.h include/gin_fs.h include/gin_http.h include/gin_liveness.h include/gin_naming.h include/gin_phoenix.h include/gin_protocol.h include/gin_send.h include/gin_topology.h src/account_naming.c src/account_naming.h src/account_plan.c src/account_plan.h src/account_receive.c src/account_receive.h src/account_receive_hot.h src/auth.c src/command.c src/connect.c src/connect_control.h src/connect_hot.h src/connect_protocols.c src/connect_topic.c src/connect_values.h src/credential.c src/credential_identity.h src/fs.c src/fs_private.h src/http.c src/http_control.h src/json_evidence.h src/liveness_backoff.c src/liveness_values.h src/naming.c src/phoenix.c src/phoenix_refs.c src/phoenix_stream.c src/protocol.c src/protocol_parse.c src/session_live_queue.c src/session_live_queue.h src/topology.c src/topology_internal.h src/topology_lookup.c tests/test_account_receive_control.c

.build/test_account_receive_hot: include/gin_auth.h include/gin_command.h include/gin_connect.h include/gin_control.h include/gin_credential.h include/gin_fs.h include/gin_http.h include/gin_liveness.h include/gin_naming.h include/gin_phoenix.h include/gin_protocol.h include/gin_send.h include/gin_topology.h src/account_naming.c src/account_naming.h src/account_plan.c src/account_plan.h src/account_receive.c src/account_receive.h src/account_receive_hot.h src/auth.c src/command.c src/connect.c src/connect_control.h src/connect_hot.h src/connect_protocols.c src/connect_topic.c src/connect_values.h src/credential.c src/credential_identity.h src/fs.c src/fs_private.h src/http.c src/http_control.h src/json_evidence.h src/liveness_backoff.c src/liveness_values.h src/naming.c src/phoenix.c src/phoenix_refs.c src/phoenix_stream.c src/protocol.c src/protocol_parse.c src/session_live_queue.c src/session_live_queue.h src/topology.c src/topology_internal.h src/topology_lookup.c tests/test_account_receive_hot.c

.build/test_account_root: include/gin_credential.h include/gin_fs.h include/gin_naming.h src/account_root.c src/account_root.h src/credential.c src/credential_identity.h src/fs.c src/fs_private.h src/json_evidence.h src/naming.c tests/test_account_root.c

.build/test_account_root_fault: include/gin_credential.h include/gin_fs.h include/gin_naming.h src/account_root.c src/account_root.h src/credential.c src/credential_identity.h src/fs.c src/fs_private.h src/json_evidence.h src/naming.c tests/test_account_root_fault.c

.build/test_account_root_hot_fault: include/gin_credential.h include/gin_fs.h include/gin_naming.h src/account_root.c src/account_root.h src/credential.c src/credential_identity.h src/fs.c src/fs_private.h src/json_evidence.h src/naming.c tests/test_account_root_hot_fault.c

.build/test_account_sinks: include/gin_auth.h include/gin_command.h include/gin_connect.h include/gin_credential.h include/gin_fs.h include/gin_http.h include/gin_naming.h include/gin_phoenix.h include/gin_protocol.h include/gin_topology.h src/account_fs.c src/account_fs.h src/account_naming.c src/account_naming.h src/account_plan.c src/account_plan.h src/account_registry.c src/account_registry.h src/account_root.c src/account_root.h src/account_sinks.c src/account_sinks.h src/account_sinks_hot.h src/command.c src/connect_topic.c src/connect_values.h src/credential.c src/credential_identity.h src/fs.c src/fs_private.h src/json_evidence.h src/naming.c src/topology.c src/topology_internal.h src/topology_lookup.c tests/sinks_exec_probe.h tests/test_account_sinks.c

.build/test_account_sinks_fault: include/gin_auth.h include/gin_command.h include/gin_connect.h include/gin_credential.h include/gin_fs.h include/gin_http.h include/gin_naming.h include/gin_phoenix.h include/gin_protocol.h include/gin_topology.h src/account_fs.c src/account_fs.h src/account_naming.c src/account_naming.h src/account_plan.c src/account_plan.h src/account_registry.c src/account_registry.h src/account_root.c src/account_root.h src/account_sinks.c src/account_sinks.h src/account_sinks_hot.h src/command.c src/connect_topic.c src/connect_values.h src/credential.c src/credential_identity.h src/fs.c src/fs_private.h src/json_evidence.h src/naming.c src/topology.c src/topology_internal.h src/topology_lookup.c tests/sinks_exec_probe.h tests/test_account_sinks.c tests/test_account_sinks_fault.c

.build/test_account_sinks_hot: include/gin_auth.h include/gin_command.h include/gin_connect.h include/gin_credential.h include/gin_fs.h include/gin_http.h include/gin_naming.h include/gin_phoenix.h include/gin_protocol.h include/gin_topology.h src/account_fs.c src/account_fs.h src/account_naming.c src/account_naming.h src/account_plan.c src/account_plan.h src/account_registry.c src/account_registry.h src/account_root.c src/account_root.h src/account_sinks.c src/account_sinks.h src/account_sinks_hot.h src/command.c src/connect_topic.c src/connect_values.h src/credential.c src/credential_identity.h src/fs.c src/fs_private.h src/json_evidence.h src/naming.c src/topology.c src/topology_internal.h src/topology_lookup.c tests/sinks_exec_probe.h tests/test_account_sinks.c tests/test_account_sinks_hot.c

.build/test_account_sinks_hot_bind: include/gin_auth.h include/gin_command.h include/gin_connect.h include/gin_credential.h include/gin_fs.h include/gin_http.h include/gin_naming.h include/gin_phoenix.h include/gin_protocol.h include/gin_topology.h src/account_fs.c src/account_fs.h src/account_naming.c src/account_naming.h src/account_plan.c src/account_plan.h src/account_registry.c src/account_registry.h src/account_root.c src/account_root.h src/account_sinks.c src/account_sinks.h src/account_sinks_hot.h src/command.c src/connect_topic.c src/connect_values.h src/credential.c src/credential_identity.h src/fs.c src/fs_private.h src/json_evidence.h src/naming.c src/topology.c src/topology_internal.h src/topology_lookup.c tests/sinks_exec_probe.h tests/test_account_sinks.c tests/test_account_sinks_hot_bind.c

.build/test_account_sinks_hot_status: include/gin_auth.h include/gin_command.h include/gin_connect.h include/gin_credential.h include/gin_fs.h include/gin_http.h include/gin_naming.h include/gin_phoenix.h include/gin_protocol.h include/gin_topology.h src/account_fs.c src/account_fs.h src/account_naming.c src/account_naming.h src/account_plan.c src/account_plan.h src/account_registry.c src/account_registry.h src/account_root.c src/account_root.h src/account_sinks.c src/account_sinks.h src/account_sinks_hot.h src/command.c src/connect_topic.c src/connect_values.h src/credential.c src/credential_identity.h src/fs.c src/fs_private.h src/json_evidence.h src/naming.c src/topology.c src/topology_internal.h src/topology_lookup.c tests/sinks_exec_probe.h tests/test_account_sinks.c tests/test_account_sinks_hot_status.c

.build/test_account_sinks_hot_status_vg: include/gin_auth.h include/gin_command.h include/gin_connect.h include/gin_credential.h include/gin_fs.h include/gin_http.h include/gin_naming.h include/gin_phoenix.h include/gin_protocol.h include/gin_topology.h src/account_fs.c src/account_fs.h src/account_naming.c src/account_naming.h src/account_plan.c src/account_plan.h src/account_registry.c src/account_registry.h src/account_root.c src/account_root.h src/account_sinks.c src/account_sinks.h src/account_sinks_hot.h src/command.c src/connect_topic.c src/connect_values.h src/credential.c src/credential_identity.h src/fs.c src/fs_private.h src/json_evidence.h src/naming.c src/topology.c src/topology_internal.h src/topology_lookup.c tests/sinks_exec_probe.h tests/test_account_sinks.c tests/test_account_sinks_hot_status.c

.build/test_auth_bounds: include/gin_auth.h include/gin_control.h include/gin_credential.h include/gin_fs.h include/gin_http.h include/gin_naming.h include/gin_protocol.h include/gin_send.h include/gin_topology.h src/auth.c src/credential.c src/credential_identity.h src/http.c src/http_control.h src/json_evidence.h tests/test_auth_bounds.c

.build/test_authority_json_connect: include/gin_auth.h include/gin_command.h include/gin_connect.h include/gin_control.h include/gin_credential.h include/gin_fs.h include/gin_http.h include/gin_liveness.h include/gin_naming.h include/gin_phoenix.h include/gin_protocol.h include/gin_send.h include/gin_topology.h src/auth.c src/command.c src/connect.c src/connect_control.h src/connect_hot.h src/connect_protocols.c src/connect_topic.c src/connect_values.h src/credential.c src/credential_identity.h src/fs.c src/fs_private.h src/http.c src/http_control.h src/json_evidence.h src/liveness_backoff.c src/liveness_values.h src/naming.c src/phoenix.c src/phoenix_refs.c src/phoenix_stream.c src/protocol.c src/protocol_parse.c src/topology.c src/topology_internal.h src/topology_lookup.c tests/test_authority_json_connect.c

.build/test_authority_json_identity: include/gin_auth.h include/gin_control.h include/gin_credential.h include/gin_fs.h include/gin_http.h include/gin_naming.h include/gin_protocol.h include/gin_send.h include/gin_topology.h src/auth.c src/credential.c src/credential_identity.h src/fs.c src/fs_private.h src/http.c src/http_control.h src/json_evidence.h src/naming.c tests/test_authority_json_identity.c

.build/test_authority_json_store: include/gin_auth.h include/gin_control.h include/gin_credential.h include/gin_fs.h include/gin_http.h include/gin_naming.h include/gin_protocol.h include/gin_send.h include/gin_topology.h src/credential.c src/credential_identity.h src/fs.c src/fs_private.h src/http.c src/http_control.h src/json_evidence.h src/naming.c tests/test_authority_json_store.c

.build/test_connect: include/gin_auth.h include/gin_command.h include/gin_connect.h include/gin_control.h include/gin_credential.h include/gin_fs.h include/gin_http.h include/gin_liveness.h include/gin_naming.h include/gin_phoenix.h include/gin_protocol.h include/gin_send.h include/gin_topology.h src/auth.c src/command.c src/connect.c src/connect_control.h src/connect_hot.h src/connect_protocols.c src/connect_topic.c src/connect_values.h src/credential.c src/credential_identity.h src/http.c src/http_control.h src/json_evidence.h src/liveness_backoff.c src/liveness_values.h src/naming.c src/phoenix.c src/phoenix_refs.c src/phoenix_stream.c src/protocol.c src/protocol_parse.c src/topology.c src/topology_internal.h src/topology_lookup.c tests/test_connect.c

.build/test_connect_drain: include/gin_auth.h include/gin_command.h include/gin_connect.h include/gin_control.h include/gin_credential.h include/gin_fs.h include/gin_http.h include/gin_liveness.h include/gin_naming.h include/gin_phoenix.h include/gin_protocol.h include/gin_send.h include/gin_topology.h src/auth.c src/command.c src/connect.c src/connect_control.h src/connect_hot.h src/connect_protocols.c src/connect_topic.c src/connect_values.h src/credential.c src/credential_identity.h src/http.c src/http_control.h src/json_evidence.h src/liveness_backoff.c src/liveness_values.h src/naming.c src/phoenix.c src/phoenix_refs.c src/phoenix_stream.c src/protocol.c src/protocol_parse.c src/topology.c src/topology_internal.h src/topology_lookup.c tests/test_connect_drain.c

.build/test_connect_hot: include/gin_auth.h include/gin_command.h include/gin_connect.h include/gin_control.h include/gin_credential.h include/gin_fs.h include/gin_http.h include/gin_liveness.h include/gin_naming.h include/gin_phoenix.h include/gin_protocol.h include/gin_send.h include/gin_topology.h src/auth.c src/command.c src/connect.c src/connect_control.h src/connect_hot.h src/connect_protocols.c src/connect_topic.c src/connect_values.h src/credential.c src/credential_identity.h src/http.c src/http_control.h src/json_evidence.h src/liveness_backoff.c src/liveness_values.h src/naming.c src/phoenix.c src/phoenix_refs.c src/phoenix_stream.c src/protocol.c src/protocol_parse.c src/topology.c src/topology_internal.h src/topology_lookup.c tests/test_connect_hot.c

.build/test_connect_hot_fault: include/gin_auth.h include/gin_command.h include/gin_connect.h include/gin_control.h include/gin_credential.h include/gin_fs.h include/gin_http.h include/gin_liveness.h include/gin_naming.h include/gin_phoenix.h include/gin_protocol.h include/gin_send.h include/gin_topology.h src/auth.c src/command.c src/connect.c src/connect_control.h src/connect_hot.h src/connect_protocols.c src/connect_topic.c src/connect_values.h src/credential.c src/credential_identity.h src/http.c src/http_control.h src/json_evidence.h src/liveness_backoff.c src/liveness_values.h src/naming.c src/phoenix.c src/phoenix_refs.c src/phoenix_stream.c src/protocol.c src/protocol_parse.c src/topology.c src/topology_internal.h src/topology_lookup.c tests/test_connect_hot_fault.c

.build/test_connect_liveness: include/gin_auth.h include/gin_command.h include/gin_connect.h include/gin_control.h include/gin_credential.h include/gin_fs.h include/gin_http.h include/gin_liveness.h include/gin_naming.h include/gin_phoenix.h include/gin_protocol.h include/gin_send.h include/gin_topology.h src/auth.c src/command.c src/connect.c src/connect_control.h src/connect_hot.h src/connect_protocols.c src/connect_topic.c src/connect_values.h src/credential.c src/credential_identity.h src/http.c src/http_control.h src/json_evidence.h src/liveness_backoff.c src/liveness_values.h src/naming.c src/phoenix.c src/phoenix_refs.c src/phoenix_stream.c src/protocol.c src/protocol_parse.c src/topology.c src/topology_internal.h src/topology_lookup.c tests/test_connect_liveness.c

.build/test_connect_query: include/gin_auth.h include/gin_command.h include/gin_connect.h include/gin_control.h include/gin_credential.h include/gin_fs.h include/gin_http.h include/gin_liveness.h include/gin_naming.h include/gin_phoenix.h include/gin_protocol.h include/gin_send.h include/gin_topology.h src/auth.c src/command.c src/connect.c src/connect_control.h src/connect_hot.h src/connect_protocols.c src/connect_topic.c src/connect_values.h src/credential.c src/credential_identity.h src/http.c src/http_control.h src/json_evidence.h src/liveness_backoff.c src/liveness_values.h src/naming.c src/phoenix.c src/phoenix_refs.c src/phoenix_stream.c src/protocol.c src/protocol_parse.c src/topology.c src/topology_internal.h src/topology_lookup.c tests/test_connect_query.c

.build/test_connect_replan: include/gin_auth.h include/gin_command.h include/gin_connect.h include/gin_control.h include/gin_credential.h include/gin_fs.h include/gin_http.h include/gin_liveness.h include/gin_naming.h include/gin_phoenix.h include/gin_protocol.h include/gin_send.h include/gin_topology.h src/auth.c src/command.c src/connect.c src/connect_control.h src/connect_hot.h src/connect_protocols.c src/connect_topic.c src/connect_values.h src/credential.c src/credential_identity.h src/fs.c src/fs_private.h src/http.c src/http_control.h src/json_evidence.h src/liveness_backoff.c src/liveness_values.h src/naming.c src/phoenix.c src/phoenix_refs.c src/phoenix_stream.c src/protocol.c src/protocol_parse.c src/topology.c src/topology_internal.h src/topology_lookup.c tests/test_connect_replan.c

.build/test_control: include/gin_auth.h include/gin_command.h include/gin_control.h include/gin_credential.h include/gin_fs.h include/gin_http.h include/gin_input.h include/gin_naming.h include/gin_protocol.h include/gin_send.h include/gin_topology.h src/auth.c src/command.c src/control.c src/credential.c src/credential_identity.h src/fs.c src/fs_private.h src/http.c src/http_control.h src/input.c src/json_evidence.h src/naming.c src/protocol.c src/protocol_parse.c src/send_reply.c src/send_values.h tests/test_control.c

.build/test_credential: include/gin_credential.h include/gin_fs.h src/credential.c src/credential_identity.h src/json_evidence.h tests/test_credential.c

.build/test_credential_identity: include/gin_credential.h include/gin_fs.h src/credential.c src/credential_identity.h src/json_evidence.h tests/test_credential_identity.c

.build/test_send: include/gin_auth.h include/gin_control.h include/gin_credential.h include/gin_fs.h include/gin_http.h include/gin_naming.h include/gin_protocol.h include/gin_send.h include/gin_topology.h src/auth.c src/credential.c src/credential_identity.h src/http.c src/http_control.h src/json_evidence.h src/naming.c src/protocol.c src/protocol_parse.c src/send.c src/send_payload.c src/send_reply.c src/send_values.h tests/test_send.c

.build/test_session_owner: include/gin_auth.h include/gin_command.h include/gin_config.h include/gin_connect.h include/gin_control.h include/gin_credential.h include/gin_fs.h include/gin_history.h include/gin_http.h include/gin_input.h include/gin_liveness.h include/gin_naming.h include/gin_phoenix.h include/gin_protocol.h include/gin_render.h include/gin_send.h include/gin_session.h include/gin_topology.h src/auth.c src/checkpoint.c src/checkpoint.h src/command.c src/connect.c src/connect_control.h src/connect_hot.h src/connect_protocols.c src/connect_topic.c src/connect_values.h src/control.c src/credential.c src/credential_identity.h src/fs.c src/fs_private.h src/history_evidence.c src/history_evidence.h src/history_fetch.c src/history_page.c src/history_path.c src/history_recovery.c src/history_render.c src/history_values.h src/http.c src/http_control.h src/input.c src/json_evidence.h src/liveness_backoff.c src/liveness_values.h src/naming.c src/phoenix.c src/phoenix_refs.c src/phoenix_stream.c src/protocol.c src/protocol_parse.c src/render.c src/render_escape.c src/render_time.c src/render_values.h src/send.c src/send_payload.c src/send_reply.c src/send_values.h src/session.c src/session_config.c src/session_live_projection.c src/session_live_projection.h src/session_live_queue.c src/session_live_queue.h src/session_operation.c src/session_operation.h src/topology.c src/topology_fetch.c src/topology_internal.h src/topology_lookup.c tests/test_session_owner.c

.build/test_session_owner_whitebox: include/gin_auth.h include/gin_command.h include/gin_config.h include/gin_connect.h include/gin_control.h include/gin_credential.h include/gin_fs.h include/gin_history.h include/gin_http.h include/gin_input.h include/gin_liveness.h include/gin_naming.h include/gin_phoenix.h include/gin_protocol.h include/gin_render.h include/gin_send.h include/gin_session.h include/gin_topology.h src/auth.c src/checkpoint.c src/checkpoint.h src/command.c src/connect.c src/connect_control.h src/connect_hot.h src/connect_protocols.c src/connect_topic.c src/connect_values.h src/control.c src/credential.c src/credential_identity.h src/fs.c src/fs_private.h src/history_evidence.c src/history_evidence.h src/history_fetch.c src/history_page.c src/history_path.c src/history_recovery.c src/history_render.c src/history_values.h src/http.c src/http_control.h src/input.c src/json_evidence.h src/liveness_backoff.c src/liveness_values.h src/naming.c src/phoenix.c src/phoenix_refs.c src/phoenix_stream.c src/protocol.c src/protocol_parse.c src/render.c src/render_escape.c src/render_time.c src/render_values.h src/send.c src/send_payload.c src/send_reply.c src/send_values.h src/session.c src/session_config.c src/session_live_projection.c src/session_live_projection.h src/session_live_queue.c src/session_live_queue.h src/session_operation.c src/session_operation.h src/topology.c src/topology_fetch.c src/topology_internal.h src/topology_lookup.c tests/test_session_owner.c

.build/topology_driver: include/gin_auth.h include/gin_control.h include/gin_credential.h include/gin_fs.h include/gin_http.h include/gin_naming.h include/gin_protocol.h include/gin_send.h include/gin_topology.h src/auth.c src/credential.c src/credential_identity.h src/http.c src/http_control.h src/json_evidence.h src/naming.c src/protocol.c src/protocol_parse.c src/topology.c src/topology_fetch.c src/topology_internal.h src/topology_lookup.c tests/topology_driver.c

.build/test_account_fs_hot_fault: include/gin_fs.h include/gin_naming.h src/account_naming.c src/account_naming.h src/fs.c src/fs_private.h src/naming.c tests/test_account_fs_hot_fault.c




.build/account_history_bootstrap_driver: include/gin_auth.h include/gin_command.h include/gin_config.h include/gin_connect.h include/gin_control.h include/gin_credential.h include/gin_fs.h include/gin_history.h include/gin_http.h include/gin_liveness.h include/gin_naming.h include/gin_phoenix.h include/gin_protocol.h include/gin_render.h include/gin_send.h include/gin_session.h include/gin_topology.h src/account_dispatch.c src/account_dispatch.h src/account_fs.c src/account_fs.h src/account_history_bootstrap.c src/account_history_bootstrap.h src/account_naming.c src/account_naming.h src/account_output.c src/account_output.h src/account_output_hot.h src/account_plan.c src/account_plan.h src/account_receive.c src/account_receive.h src/account_receive_hot.h src/account_registry.c src/account_registry.h src/account_root.c src/account_root.h src/account_sinks.c src/account_sinks.h src/account_sinks_hot.h src/auth.c src/checkpoint.c src/checkpoint.h src/command.c src/connect.c src/connect_control.h src/connect_hot.h src/connect_protocols.c src/connect_topic.c src/connect_values.h src/control.c src/credential.c src/credential_identity.h src/fs.c src/fs_private.h src/history_evidence.c src/history_evidence.h src/history_fetch.c src/history_page.c src/history_path.c src/history_recovery.c src/history_render.c src/history_values.h src/http.c src/http_control.h src/json_evidence.h src/liveness_backoff.c src/liveness_values.h src/naming.c src/phoenix.c src/phoenix_refs.c src/phoenix_stream.c src/protocol.c src/protocol_parse.c src/render.c src/render_escape.c src/render_time.c src/render_values.h src/send_reply.c src/send_values.h src/session_live_projection.c src/session_live_projection.h src/session_live_queue.c src/session_live_queue.h src/topology.c src/topology_internal.h src/topology_lookup.c tests/account_history_bootstrap_driver.c

.build/account_history_output_driver: include/gin_auth.h include/gin_command.h include/gin_config.h include/gin_connect.h include/gin_control.h include/gin_credential.h include/gin_fs.h include/gin_history.h include/gin_http.h include/gin_liveness.h include/gin_naming.h include/gin_phoenix.h include/gin_protocol.h include/gin_render.h include/gin_send.h include/gin_session.h include/gin_topology.h src/account_dispatch.c src/account_dispatch.h src/account_fs.c src/account_fs.h src/account_naming.c src/account_naming.h src/account_output.c src/account_output.h src/account_output_hot.h src/account_plan.c src/account_plan.h src/account_receive.c src/account_receive.h src/account_receive_hot.h src/account_registry.c src/account_registry.h src/account_root.c src/account_root.h src/account_sinks.c src/account_sinks.h src/account_sinks_hot.h src/auth.c src/checkpoint.c src/checkpoint.h src/command.c src/connect.c src/connect_control.h src/connect_hot.h src/connect_protocols.c src/connect_topic.c src/connect_values.h src/credential.c src/credential_identity.h src/fs.c src/fs_private.h src/history_evidence.c src/history_evidence.h src/history_page.c src/history_recovery.c src/history_render.c src/history_values.h src/http.c src/http_control.h src/json_evidence.h src/liveness_backoff.c src/liveness_values.h src/naming.c src/phoenix.c src/phoenix_refs.c src/phoenix_stream.c src/protocol.c src/protocol_parse.c src/render.c src/render_escape.c src/render_time.c src/render_values.h src/session_live_projection.c src/session_live_projection.h src/session_live_queue.c src/session_live_queue.h src/topology.c src/topology_internal.h src/topology_lookup.c tests/account_history_output_driver.c

.build/account_output_driver: include/gin_auth.h include/gin_command.h include/gin_config.h include/gin_connect.h include/gin_control.h include/gin_credential.h include/gin_fs.h include/gin_history.h include/gin_http.h include/gin_liveness.h include/gin_naming.h include/gin_phoenix.h include/gin_protocol.h include/gin_render.h include/gin_send.h include/gin_session.h include/gin_topology.h src/account_dispatch.c src/account_dispatch.h src/account_fs.c src/account_fs.h src/account_naming.c src/account_naming.h src/account_output.c src/account_output.h src/account_output_hot.h src/account_plan.c src/account_plan.h src/account_receive.c src/account_receive.h src/account_receive_hot.h src/account_registry.c src/account_registry.h src/account_root.c src/account_root.h src/account_sinks.c src/account_sinks.h src/account_sinks_hot.h src/auth.c src/checkpoint.c src/checkpoint.h src/command.c src/connect.c src/connect_control.h src/connect_hot.h src/connect_protocols.c src/connect_topic.c src/connect_values.h src/credential.c src/credential_identity.h src/fs.c src/fs_private.h src/history_evidence.c src/history_evidence.h src/history_page.c src/history_render.c src/history_values.h src/http.c src/http_control.h src/json_evidence.h src/liveness_backoff.c src/liveness_values.h src/naming.c src/phoenix.c src/phoenix_refs.c src/phoenix_stream.c src/protocol.c src/protocol_parse.c src/render.c src/render_escape.c src/render_time.c src/render_values.h src/session_live_projection.c src/session_live_projection.h src/session_live_queue.c src/session_live_queue.h src/topology.c src/topology_internal.h src/topology_lookup.c tests/account_output_driver.c

.build/account_owner_driver: include/gin_auth.h include/gin_command.h include/gin_config.h include/gin_connect.h include/gin_control.h include/gin_credential.h include/gin_fs.h include/gin_history.h include/gin_http.h include/gin_input.h include/gin_liveness.h include/gin_naming.h include/gin_phoenix.h include/gin_protocol.h include/gin_render.h include/gin_send.h include/gin_session.h include/gin_topology.h src/account_dispatch.c src/account_dispatch.h src/account_fs.c src/account_fs.h src/account_history_bootstrap.c src/account_history_bootstrap.h src/account_naming.c src/account_naming.h src/account_output.c src/account_output.h src/account_output_hot.h src/account_owner.c src/account_owner.h src/account_plan.c src/account_plan.h src/account_receive.c src/account_receive.h src/account_receive_hot.h src/account_registry.c src/account_registry.h src/account_root.c src/account_root.h src/account_sinks.c src/account_sinks.h src/account_sinks_hot.h src/auth.c src/checkpoint.c src/checkpoint.h src/command.c src/connect.c src/connect_control.h src/connect_hot.h src/connect_protocols.c src/connect_topic.c src/connect_values.h src/control.c src/credential.c src/credential_identity.h src/fs.c src/fs_private.h src/history_evidence.c src/history_evidence.h src/history_fetch.c src/history_page.c src/history_path.c src/history_recovery.c src/history_render.c src/history_values.h src/http.c src/http_control.h src/input.c src/json_evidence.h src/liveness_backoff.c src/liveness_values.h src/naming.c src/phoenix.c src/phoenix_refs.c src/phoenix_stream.c src/protocol.c src/protocol_parse.c src/render.c src/render_escape.c src/render_time.c src/render_values.h src/send.c src/send_payload.c src/send_reply.c src/send_values.h src/session_live_projection.c src/session_live_projection.h src/session_live_queue.c src/session_live_queue.h src/topology.c src/topology_internal.h src/topology_lookup.c tests/account_owner_driver.c


























.build/test_account_output_hot: include/gin_auth.h include/gin_command.h include/gin_config.h include/gin_connect.h include/gin_control.h include/gin_credential.h include/gin_fs.h include/gin_history.h include/gin_http.h include/gin_naming.h include/gin_phoenix.h include/gin_protocol.h include/gin_render.h include/gin_send.h include/gin_session.h include/gin_topology.h src/account_dispatch.h src/account_fs.c src/account_fs.h src/account_naming.c src/account_naming.h src/account_output.c src/account_output.h src/account_output_hot.h src/account_plan.c src/account_plan.h src/account_receive.h src/account_registry.c src/account_registry.h src/account_root.c src/account_root.h src/account_sinks.c src/account_sinks.h src/account_sinks_hot.h src/checkpoint.c src/checkpoint.h src/command.c src/connect_topic.c src/connect_values.h src/credential.c src/credential_identity.h src/fs.c src/fs_private.h src/history_evidence.c src/history_evidence.h src/history_page.c src/history_render.c src/history_values.h src/http.c src/http_control.h src/json_evidence.h src/naming.c src/render.c src/render_escape.c src/render_time.c src/render_values.h src/session_live_projection.h src/session_live_queue.h src/topology.c src/topology_internal.h src/topology_lookup.c tests/test_account_output_hot.c

.build/test_account_output_hot_vg: include/gin_auth.h include/gin_command.h include/gin_config.h include/gin_connect.h include/gin_control.h include/gin_credential.h include/gin_fs.h include/gin_history.h include/gin_http.h include/gin_naming.h include/gin_phoenix.h include/gin_protocol.h include/gin_render.h include/gin_send.h include/gin_session.h include/gin_topology.h src/account_dispatch.h src/account_fs.c src/account_fs.h src/account_naming.c src/account_naming.h src/account_output.c src/account_output.h src/account_output_hot.h src/account_plan.c src/account_plan.h src/account_receive.h src/account_registry.c src/account_registry.h src/account_root.c src/account_root.h src/account_sinks.c src/account_sinks.h src/account_sinks_hot.h src/checkpoint.c src/checkpoint.h src/command.c src/connect_topic.c src/connect_values.h src/credential.c src/credential_identity.h src/fs.c src/fs_private.h src/history_evidence.c src/history_evidence.h src/history_page.c src/history_render.c src/history_values.h src/http.c src/http_control.h src/json_evidence.h src/naming.c src/render.c src/render_escape.c src/render_time.c src/render_values.h src/session_live_projection.h src/session_live_queue.h src/topology.c src/topology_internal.h src/topology_lookup.c tests/test_account_output_hot.c



























.build/test_account_output_hot .build/test_account_output_hot_vg: src/phoenix.c include/gin_phoenix.h

# The private unit includes dispatch.c and interposes receiver getters.
ACCOUNT_DISPATCH_HOT_UNIT_SUPPORT = src/account_registry.c src/account_plan.c src/account_naming.c src/session_live_projection.c src/history_page.c src/history_evidence.c $(CONNECT_SOURCES) $(RENDER_SOURCES)
.build/test_account_dispatch_hot: tests/test_account_dispatch_hot.c src/account_dispatch.c $(ACCOUNT_DISPATCH_HOT_UNIT_SUPPORT) $(ACCOUNT_DISPATCH_HEADERS)
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude -Isrc $(CURL_CFLAGS) $(JANSSON_CFLAGS) $(LDFLAGS) -o $@ tests/test_account_dispatch_hot.c $(ACCOUNT_DISPATCH_HOT_UNIT_SUPPORT) $(CURL_LIBS) $(JANSSON_LIBS) $(LDLIBS)
.build/account_dispatch_hot_wss_driver: tests/account_dispatch_hot_wss_driver.c src/account_dispatch.c $(ACCOUNT_DISPATCH_SUPPORT) $(ACCOUNT_DISPATCH_HEADERS)
	mkdir -p .build
	$(CC) $(CPPFLAGS) -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude -Isrc $(CURL_CFLAGS) $(JANSSON_CFLAGS) $(LDFLAGS) -o $@ tests/account_dispatch_hot_wss_driver.c src/account_dispatch.c $(ACCOUNT_DISPATCH_SUPPORT) $(CURL_LIBS) $(JANSSON_LIBS) $(LDLIBS)
check-account-dispatch-hot: .build/test_account_dispatch_hot .build/account_dispatch_hot_wss_driver
	.build/test_account_dispatch_hot
	GIN_ACCOUNT_DISPATCH_HOT_DRIVER=$${PWD}/.build/account_dispatch_hot_wss_driver GIN_ACCOUNT_DISPATCH_HOT_RECEIPT=.build/account_dispatch_hot_receipt.json $(PYTHON) -m unittest discover -s tests -p test_account_dispatch_hot_wss.py -v
















# Dedicated instrumentation role; ordinary drivers never require Valgrind headers.
VALGRIND_HEADERS = /usr/include/valgrind/memcheck.h /usr/include/valgrind/valgrind.h
.build/account_dispatch_hot_wss_valgrind_driver: tests/account_dispatch_hot_wss_driver.c src/account_dispatch.c $(ACCOUNT_DISPATCH_SUPPORT) $(ACCOUNT_DISPATCH_HEADERS) src/account_dispatch_hot.h src/account_receive_hot.h src/account_registry.h src/credential_identity.h $(VALGRIND_HEADERS)
	mkdir -p .build
	$(CC) $(CPPFLAGS) -DGIN_DISPATCH_VALGRIND -D_POSIX_C_SOURCE=200809L $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude -Isrc $(CURL_CFLAGS) $(JANSSON_CFLAGS) $(LDFLAGS) -o $@ tests/account_dispatch_hot_wss_driver.c src/account_dispatch.c $(ACCOUNT_DISPATCH_SUPPORT) $(CURL_LIBS) $(JANSSON_LIBS) $(LDLIBS)
check-account-dispatch-hot-valgrind: .build/test_account_dispatch_hot .build/account_dispatch_hot_wss_valgrind_driver
	printf '%s\n' '#!/bin/sh' 'exec valgrind --trace-children=yes --error-exitcode=99 --leak-check=full --show-leak-kinds=all --errors-for-leak-kinds=all "$${0%/*}/account_dispatch_hot_wss_valgrind_driver" "$$@" --instrumented-setup' > .build/account_dispatch_hot_wss_vg
	chmod +x .build/account_dispatch_hot_wss_vg
	valgrind --trace-children=yes --error-exitcode=99 --leak-check=full --show-leak-kinds=all --errors-for-leak-kinds=all .build/test_account_dispatch_hot
	GIN_ACCOUNT_DISPATCH_HOT_DRIVER=$${PWD}/.build/account_dispatch_hot_wss_vg GIN_ACCOUNT_DISPATCH_HOT_RECEIPT=.build/account_dispatch_hot_valgrind_receipt.json $(PYTHON) -m unittest discover -s tests -p test_account_dispatch_hot_wss.py -v

.build/account_cli_control_driver: src/account_dispatch_hot.h

.build/account_dispatch_driver: src/account_dispatch_hot.h src/account_registry.h

.build/account_dispatch_hot_wss_driver: src/account_dispatch_hot.h src/account_receive_hot.h src/account_registry.h src/credential_identity.h

.build/account_history_bootstrap_driver: src/account_dispatch_hot.h

.build/account_history_output_driver: src/account_dispatch_hot.h

.build/account_output_driver: src/account_dispatch_hot.h

.build/account_owner_driver: src/account_dispatch_hot.h

.build/gin: src/account_dispatch_hot.h

.build/gin_account_fifo_observer: src/account_dispatch_hot.h

.build/gin_cleanup_fault: src/account_dispatch_hot.h

.build/gin_owner_fault: src/account_dispatch_hot.h

.build/gin_receive_observer: src/account_dispatch_hot.h

.build/runtime_stderr: src/account_dispatch_hot.h

.build/test_account_dispatch_fidelity: src/account_dispatch_hot.h src/account_registry.h

.build/test_account_dispatch_hot: src/account_dispatch_hot.h src/account_registry.h src/credential_identity.h
