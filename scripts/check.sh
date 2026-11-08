#!/bin/sh
# Reproduce the local Linux quality gates without network access.
set -eu
cd "$(dirname "$0")/.."
sh scripts/bootstrap.sh
python3 scripts/check_docs.py
shellcheck scripts/*.sh
cppcheck --std=c99 --enable=warning,performance,portability --error-exitcode=1 \
    --inline-suppr --suppress=missingIncludeSystem -I include src
for compiler in gcc clang musl-gcc; do
    make clean
    make CC="$compiler" check
 done
make clean
bmake CC=cc check
make clean
make CC=gcc CFLAGS='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie' \
    LDFLAGS='-fsanitize=address,undefined -no-pie' check
make clean
make CC=gcc check
stage="$(pwd)/.build/stage"
make DESTDIR="$stage" PREFIX=/usr install
"$stage/usr/bin/gin" --version
make DESTDIR="$stage" PREFIX=/usr uninstall
test ! -e "$stage/usr/bin/gin"
test ! -e "$stage/usr/share/man/man1/gin.1"
valgrind --quiet --error-exitcode=1 --leak-check=full --errors-for-leak-kinds=all \
    .build/gin init -d "$(pwd)/.build/valgrind-root" -n azzurra -c '#grappa'
git diff --check
printf '%s\n' 'All local development checks passed.'
