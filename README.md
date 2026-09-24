# bfc: a self-hosting Brainfuck compiler

bfc.bf is a Brainfuck program that reads one Brainfuck source stream up to a
NUL byte and writes a standalone LLVM IR module. It ignores every input byte
other than the eight Brainfuck commands. The module embeds those commands as
bytecode and includes an LLVM IR interpreter. Its zero-initialized tape grows
as needed in either direction, preserving cells when it reallocates. Programs
use standard input and output.

The compiler itself is written in Brainfuck. bf.py is a Python reference
interpreter; the bootstrap script uses bf-run.c as its stage zero interpreter.
Run ./bootstrap.sh.

The script creates build/bfc.ll, verifies it with llvm-as, compiles it, then
asks the compiled compiler to compile bfc.bf again. It checks that both LLVM IR
files are byte-for-byte identical and builds the second-stage compiler too.
The build needs a C compiler, llvm-as, and clang.

The compiler consumes source through stdin and requires a NUL byte terminator.
The bootstrap script adds that byte. The source may contain newlines and
comments; they are ignored like any other non-command character. Generated
modules accept Brainfuck program input through stdin; EOF reads as zero. The
embedded command buffer supports up to 1,000,000 Brainfuck commands.
