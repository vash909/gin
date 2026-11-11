#!/bin/sh
# User-prefix native TLS/WebSocket curl; never changes the system installation.
set -eu
umask 077
: "${TMPDIR:?Set a private build directory}"
: "${GIN_CURL_PREFIX:?Set an absolute user-owned native curl prefix}"
case "$GIN_CURL_PREFIX" in
  /*) ;;
  *) echo 'GIN_CURL_PREFIX must be absolute.' >&2; exit 1 ;;
esac
for tool in curl tar make cc python3 mktemp; do
  command -v "$tool" >/dev/null
done
python3 - "$GIN_CURL_PREFIX" "$TMPDIR" <<'PY'
import os
import stat
import sys

prefix, tmpdir = sys.argv[1], sys.argv[2]

def fail(message):
    sys.stderr.write(message + '\n')
    sys.exit(1)

def nearest_existing(path):
    current = path
    while not os.path.exists(current):
        parent = os.path.dirname(current)
        if parent == current:
            break
        current = parent
    return current

def check_owned_not_group_other_writable(path, label):
    info = os.stat(path)
    if not stat.S_ISDIR(info.st_mode):
        fail(label + ' must be a directory.')
    if info.st_uid != os.geteuid():
        fail(label + ' is not owned by the caller.')
    if stat.S_IMODE(info.st_mode) & 0o022:
        fail(label + ' must not be group- or other-writable.')

if os.path.islink(prefix):
    fail('GIN_CURL_PREFIX must not be a symlink.')
if os.path.lexists(prefix) and not os.path.isdir(prefix):
    fail('GIN_CURL_PREFIX must be a directory.')
canonical_prefix = os.path.realpath(prefix)
system_prefixes = {
    '/', '/usr', '/usr/local', '/bin', '/sbin', '/lib', '/lib64',
    '/etc', '/opt', '/opt/homebrew', '/var', '/tmp',
}
if canonical_prefix in {os.path.realpath(p) for p in system_prefixes}:
    fail('GIN_CURL_PREFIX must not be a system prefix.')
check_owned_not_group_other_writable(
    nearest_existing(canonical_prefix), 'GIN_CURL_PREFIX or its nearest existing parent')
resolved_tmpdir = os.path.realpath(tmpdir)
if not os.path.isdir(resolved_tmpdir):
    fail('TMPDIR must be an existing directory.')
check_owned_not_group_other_writable(resolved_tmpdir, 'TMPDIR')
PY
build=$(mktemp -d "$TMPDIR/gin-host-curl.XXXXXX")
cd "$build"
archive=curl-8.23.0.tar.xz
curl -q --proto '=https' --proto-redir '=https' --fail --location --output "$archive" \
    https://github.com/curl/curl/releases/download/curl-8_23_0/curl-8.23.0.tar.xz
expected=8dadd7d2536a314f982ad05a651b713f426bf7d02cc2c76cffb9796f2218e6c5
if command -v sha256sum >/dev/null; then
  actual=$(sha256sum "$archive")
  actual=${actual%% *}
elif command -v shasum >/dev/null; then
  actual=$(shasum -a 256 "$archive")
  actual=${actual%% *}
else
  actual=$(sha256 -q "$archive")
fi
[ "$actual" = "$expected" ] || { echo 'curl archive checksum mismatch.' >&2; exit 1; }
tar -xf "$archive"
cd curl-8.23.0
./configure --prefix="$GIN_CURL_PREFIX" --disable-shared --enable-static --with-openssl \
    --enable-websockets --without-libpsl --without-zlib --without-brotli --without-zstd \
    --without-libidn2 --without-nghttp2 --without-nghttp3 --without-ngtcp2 \
    --without-libssh2 --without-librtmp --without-gssapi --disable-ldap --disable-ldaps \
    --disable-docs --disable-manual
nice -n 10 make -j1
make install
"$GIN_CURL_PREFIX/bin/curl-config" --checkfor 8.23.0
python3 - "$GIN_CURL_PREFIX/bin/curl-config" <<'PY'
import subprocess
import sys

config = sys.argv[1]
try:
    features = set(subprocess.check_output([config, '--features'], text=True).split())
    protocols = set(subprocess.check_output([config, '--protocols'], text=True).split())
except Exception:
    features, protocols = set(), set()
if not {'SSL', 'AsynchDNS'} <= features or not {'WS', 'WSS'} <= protocols:
    sys.stderr.write('Installed curl does not provide required SSL, AsynchDNS, WS and WSS support.\n')
    sys.exit(1)
PY
