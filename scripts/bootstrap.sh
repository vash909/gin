#!/bin/sh
# Install optional development tools only on explicit request.
set -eu
case ${1-} in
    '') ;;
    --install)
        if ! command -v apt-get >/dev/null 2>&1; then
            echo 'Automatic installation supports Debian/Ubuntu only; see docs/DEVELOPMENT.md.' >&2
            exit 1
        fi
        sudo apt-get update
        sudo apt-get install -y --no-install-recommends build-essential clang clang-tools \
            cppcheck valgrind gdb pkg-config libcurl4-openssl-dev libwebsockets-dev \
            libjansson-dev libssl-dev musl-tools bmake shellcheck python3 ca-certificates \
            openssl cmake curl xz-utils bzip2
        ;;
    *) echo 'Usage: sh scripts/bootstrap.sh [--install]' >&2; exit 2 ;;
esac
missing=0
for tool in cc gcc clang make bmake musl-gcc python3 shellcheck cppcheck valgrind gdb pkg-config openssl; do
    if command -v "$tool" >/dev/null 2>&1; then
        printf '%s: %s\n' "$tool" "$(command -v "$tool")"
    else
        printf 'Missing development tool: %s\n' "$tool" >&2
        missing=1
    fi
done
for library in libwebsockets jansson openssl; do
    if pkg-config --exists "$library"; then
        printf '%s: %s\n' "$library" "$(pkg-config --modversion "$library")"
    else
        printf 'Missing transport development dependency: %s\n' "$library" >&2
        missing=1
    fi
done
curl_config=${CURL_CONFIG:-curl-config}
if command -v "$curl_config" >/dev/null 2>&1; then
    printf 'curl-config: %s\n' "$("$curl_config" --version)"
    if ! "$curl_config" --checkfor 7.85.0 >/dev/null 2>&1; then
        echo 'REST/TLS requires curl-config from libcurl >=7.85.0.' >&2
        missing=1
    fi
else
    printf 'Missing curl-config command: %s\n' "$curl_config" >&2
    missing=1
fi
exit "$missing"
