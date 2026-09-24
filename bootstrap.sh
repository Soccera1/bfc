#!/bin/sh
set -eu

cd "$(dirname "$0")"
mkdir -p build

# Stage zero interprets the Brainfuck compiler.  Its linked runtime supplies
# the same ! calls used by native bfc programs.
cc -std=c99 -O2 -Wall -Wextra -Werror -c bf_runtime.c -o build/bf_runtime.o
ar rcs build/libbfruntime.a build/bf_runtime.o
cc -std=c99 -O2 -Wall -Wextra -Werror bf-run.c bf_runtime.c -o build/bf-run

{ cat bfc.bf; printf '\000'; } | BFC_BOOTSTRAP=1 build/bf-run bfc.bf > build/bfc-stage0.ll
llvm-as build/bfc-stage0.ll -o build/bfc-stage0.bc
clang -O0 -x ir build/bfc-stage0.ll -x none build/libbfruntime.a -o build/bfc-stage1

# The generated compiler must emit the same LLVM module from its own source.
{ cat bfc.bf; printf '\000'; } | BFC_BOOTSTRAP=1 build/bfc-stage1 > build/bfc-stage1.ll
cmp build/bfc-stage0.ll build/bfc-stage1.ll
llvm-as build/bfc-stage1.ll -o build/bfc-stage1.bc
clang -O0 -x ir build/bfc-stage1.ll -x none build/libbfruntime.a -o build/bfc

printf 'Self-hosting Brainfuck compiler built: build/bfc\n'
