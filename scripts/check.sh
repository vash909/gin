#!/bin/sh
# Reproduce the local Linux quality gates without network access.
set -eu
cd "$(dirname "$0")/.."
host_curl_config=${CURL_CONFIG:-curl-config}
sh scripts/bootstrap.sh
python3 scripts/check_docs.py
shellcheck scripts/*.sh
# cppcheck does not inherit the compiler's system-header search paths.
curl_vernum=$("$host_curl_config" --vernum)
jansson_verhex=$(pkg-config --modversion jansson | python3 -c 'import sys; p=[int(x) for x in sys.stdin.read().strip().split(".")]; print(hex((p[0]<<16)|(p[1]<<8)|(p[2] if len(p)>2 else 0)))')
cppcheck --force "-DLIBCURL_VERSION_NUM=0x$curl_vernum" "-DJANSSON_VERSION_HEX=$jansson_verhex" \
    --std=c99 --enable=warning,performance,portability --error-exitcode=1 \
    --inline-suppr --suppress=missingIncludeSystem -I include src
for compiler in gcc clang; do
    make CURL_CONFIG="$host_curl_config" clean
    make CC="$compiler" CURL_CONFIG="$host_curl_config" check
 done
: "${CURL_CONFIG_MUSL:?Set CURL_CONFIG_MUSL to a genuine musl TLS libcurl curl-config}"
make CURL_CONFIG="$host_curl_config" clean
musl_prefix=$(dirname "$(dirname "$CURL_CONFIG_MUSL")")
make CC=musl-gcc CURL_CONFIG="$CURL_CONFIG_MUSL" \
    JANSSON_CFLAGS="-I$musl_prefix/include" JANSSON_LIBS="-L$musl_prefix/lib -ljansson" LDFLAGS=-static check
make CURL_CONFIG="$host_curl_config" clean
bmake CC=cc CURL_CONFIG="$host_curl_config" check
make CURL_CONFIG="$host_curl_config" clean
make CC=gcc CURL_CONFIG="$host_curl_config" CFLAGS='-O2 -DNDEBUG' check
make CURL_CONFIG="$host_curl_config" clean
make CC=gcc CURL_CONFIG="$host_curl_config" CFLAGS='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie' \
    LDFLAGS='-fsanitize=address,undefined -no-pie' check
make CURL_CONFIG="$host_curl_config" clean
make CC=gcc CURL_CONFIG="$host_curl_config" check
stage="$(pwd)/.build/stage"
make CURL_CONFIG="$host_curl_config" DESTDIR="$stage" PREFIX=/usr install
"$stage/usr/bin/gin" --version
make CURL_CONFIG="$host_curl_config" DESTDIR="$stage" PREFIX=/usr uninstall
test ! -e "$stage/usr/bin/gin"
test ! -e "$stage/usr/share/man/man1/gin.1"
valgrind --quiet --error-exitcode=1 --leak-check=full --errors-for-leak-kinds=all \
    .build/gin init -d "$(pwd)/.build/valgrind-root" -n azzurra -c '#grappa'
git diff --check
GIN_RUNTIME_PREFIX='valgrind --quiet --error-exitcode=99 --leak-check=full --errors-for-leak-kinds=all' \
    python3 tests/test_runtime.py
GIN_HTTP_PREFIX='valgrind --quiet --error-exitcode=99 --leak-check=full --errors-for-leak-kinds=all' \
    python3 tests/test_http.py HTTPTests.test_tls_trust_and_hostname
GIN_AUTH_PREFIX='valgrind --quiet --error-exitcode=99 --leak-check=full --errors-for-leak-kinds=all' \
    python3 tests/test_auth.py AuthTests.test_stateful_revocation_and_explicit_persistence
valgrind --quiet --error-exitcode=99 --leak-check=full --errors-for-leak-kinds=all \
    .build/test_protocol_parse
GIN_PROTOCOL_PREFIX='valgrind --quiet --error-exitcode=99 --leak-check=full --errors-for-leak-kinds=all' \
    python3 tests/test_protocol.py ProtocolTests.test_gated_login_order \
    ProtocolTests.test_resume_and_fresh_preflight_each_call \
    ProtocolTests.test_auth_failures_preserve_phase_and_clear_output
valgrind --quiet --error-exitcode=99 --leak-check=full --errors-for-leak-kinds=all \
    .build/test_topology
valgrind --quiet --error-exitcode=99 --leak-check=full --errors-for-leak-kinds=all \
    .build/test_topology_lookup
GIN_TOPOLOGY_PREFIX='valgrind --quiet --error-exitcode=99 --leak-check=full --errors-for-leak-kinds=all' \
    python3 tests/test_topology.py
GIN_CONNECT_PREFIX='valgrind --quiet --error-exitcode=99 --leak-check=full --errors-for-leak-kinds=all' \
    python3 tests/test_connect.py
for unit in test_connect test_connect_protocols test_connect_topic test_phoenix test_phoenix_stream test_phoenix_collect test_phoenix_refs; do
    valgrind --quiet --error-exitcode=99 --leak-check=full --errors-for-leak-kinds=all \
        ".build/$unit"
done
printf '%s\n' 'All local development checks passed.'
