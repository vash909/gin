#!/bin/sh
set -eu
umask 077
: "${TMPDIR:?Set TMPDIR to a user-owned private scratch directory}"
: "${GIN_MUSL_PREFIX:?Set GIN_MUSL_PREFIX to an absolute user-owned install prefix}"
case "$GIN_MUSL_PREFIX" in
    /*) ;;
    *) echo 'GIN_MUSL_PREFIX must be absolute.' >&2; exit 1 ;;
esac
missing=0
for tool in curl tar xz bzip2 sha256sum cmake make musl-gcc; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        printf 'Missing required tool: %s\n' "$tool" >&2
        missing=1
    fi
done
if [ "$missing" -ne 0 ]; then
    echo 'Install the missing Linux musl build tools and try again.' >&2
    exit 1
fi
BUILD="$TMPDIR/gin-musl-reproduce"
mkdir -p "$BUILD"
cd "$BUILD"
curl -q --proto '=https' --proto-redir '=https' --fail --location --output curl-8.23.0.tar.xz https://github.com/curl/curl/releases/download/curl-8_23_0/curl-8.23.0.tar.xz
printf '%s  %s\n' 8dadd7d2536a314f982ad05a651b713f426bf7d02cc2c76cffb9796f2218e6c5 curl-8.23.0.tar.xz | sha256sum -c -
tar -xf curl-8.23.0.tar.xz
curl -q --proto '=https' --proto-redir '=https' --fail --location --output mbedtls-3.6.7.tar.bz2 https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-3.6.7/mbedtls-3.6.7.tar.bz2
printf '%s  %s\n' a7e8bcbec0e6f761b4af24f25677626b35f762f68eef79c08677a363212d11f6 mbedtls-3.6.7.tar.bz2 | sha256sum -c -
tar -xf mbedtls-3.6.7.tar.bz2
cd "$BUILD"
cmake -S "$BUILD/mbedtls-3.6.7" -B "$BUILD/mbed-build" -DCMAKE_C_COMPILER=musl-gcc -DCMAKE_INSTALL_PREFIX="$GIN_MUSL_PREFIX" -DCMAKE_BUILD_TYPE=Release -DENABLE_TESTING=OFF -DENABLE_PROGRAMS=OFF -DUSE_SHARED_MBEDTLS_LIBRARY=OFF -DUSE_STATIC_MBEDTLS_LIBRARY=ON
cd "$BUILD"
cmake --build "$BUILD/mbed-build" -j2
cd "$BUILD"
cmake --install "$BUILD/mbed-build"
cd "$BUILD/curl-8.23.0"
"$BUILD/curl-8.23.0/configure" CC=musl-gcc CFLAGS=-O2 LDFLAGS=-static --prefix="$GIN_MUSL_PREFIX" --disable-shared --enable-static --with-mbedtls="$GIN_MUSL_PREFIX" --without-openssl --without-libpsl --without-zlib --without-brotli --without-zstd --without-libidn2 --without-nghttp2 --without-nghttp3 --without-ngtcp2 --without-libssh2 --without-librtmp --without-gssapi --disable-ldap --disable-ldaps --disable-ftp --disable-file --disable-rtsp --disable-dict --disable-telnet --disable-tftp --disable-pop3 --disable-imap --disable-smtp --disable-smb --disable-gopher --disable-mqtt --disable-docs --disable-manual --with-ca-bundle=/etc/ssl/certs/ca-certificates.crt
cd "$BUILD/curl-8.23.0"
make -j2
cd "$BUILD/curl-8.23.0"
make install
cd "$BUILD"
curl -q --proto '=https' --proto-redir '=https' --fail --location --output jansson-2.15.1.tar.bz2 https://github.com/akheron/jansson/releases/download/v2.15.1/jansson-2.15.1.tar.bz2
printf '%s  %s\n' f9aa4b3ec8496fee69db94f06f3d76fd4a26b012ee9bf1e917078c2cd2841881 jansson-2.15.1.tar.bz2 | sha256sum -c -
tar -xf jansson-2.15.1.tar.bz2
cd "$BUILD/jansson-2.15.1"
./configure CC=musl-gcc CFLAGS=-O2 LDFLAGS=-static --prefix="$GIN_MUSL_PREFIX" --disable-shared --enable-static
make -j2
make install
