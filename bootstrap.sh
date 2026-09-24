#!/bin/sh
set -eu

cd "$(dirname "$0")"
mkdir -p build

# Run the Brainfuck compiler with the fast stage zero interpreter.
cc -std=c99 -O2 -Wall -Wextra -Werror bf-run.c -o build/bf-run
{ cat bfc.bf; printf '\000'; } | build/bf-run bfc.bf > build/bfc.ll
llvm-as build/bfc.ll -o build/bfc.bc
clang -O0 -x ir build/bfc.ll -o build/bfc

# The compiled compiler must regenerate identical IR from its own source.
{ cat bfc.bf; printf '\000'; } | build/bfc > build/bfc.self.ll
cmp build/bfc.ll build/bfc.self.ll
llvm-as build/bfc.self.ll -o build/bfc.self.bc
clang -O0 -x ir build/bfc.self.ll -o build/bfc.self

printf 'Self-hosting LLVM IR bootstrap complete: build/bfc and build/bfc.self\n'
