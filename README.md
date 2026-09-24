# bfc: a self-hosting Brainfuck compiler

`bfc` is written in Brainfuck. It accepts a Brainfuck source file, emits LLVM
IR to a temporary file, then links a native executable with Clang and the small
`bf_runtime` library:

```sh
./bfc hello.bf -o hello
./hello
```

If `-o` is omitted, the executable is named `a.out`. The compiler reads the
source file directly; the generated program reads its own input from standard
input and writes output to standard output. `CLANG` can name a Clang executable
other than `clang`. Running `bfc` without a source file prints usage and exits
with status 2.

The `bfc` shell file is a launcher: it bootstraps `build/bfc` when needed,
sets the runtime library path, and starts the Brainfuck compiler. The bootstrap
uses `bf-run.c` as a stage-zero interpreter, then checks that the generated
native compiler emits the same LLVM module from `bfc.bf`. Building requires a C
compiler, `ar`, `llvm-as`, and Clang. Run `./bootstrap.sh` to build it manually.

## Runtime calls

The compiler adds `!` as a runtime-call extension. Vanilla Brainfuck programs
still use the usual eight commands. With the `bf_runtime` library linked, `!`
uses the current tape cell as an operation number and the following cells as
arguments. File handles are byte values; zero is invalid for file operations.
Operations 7 and 8 use zero to select standard input and output. Strings are
NUL-terminated.

| Operation | Arguments after `!` | Result in current cell |
| --- | --- | --- |
| `1` | — | argument count |
| `2` | argument index | copies that argument starting two cells after the operation; returns its length modulo 256 |
| `3` | mode (`0` read, `1` write, `2` append), path | opens a file; read mode also selects it for `,` |
| `4` | file handle | reads one byte, or zero at EOF |
| `5` | file handle, byte | writes one byte; returns 1 on success |
| `6` | file handle | closes the file; returns 1 on success |
| `7` | file handle (`0` means stdin) | selects input for `,`; returns 1 on success |
| `8` | file handle (`0` means stdout) | selects output for `.`; returns 1 on success |
| `9` | command string | runs a command through the system shell; returns 1 on success |
| `10` | — | creates a temporary file, copies its path two cells after the operation, and selects it for `.` |
| `11` | output path (empty means `a.out`) | links the most recent temporary LLVM module with Clang; 0 on success, 1 on failure |
| `12` | exit status | exits the process |
| `13` | — | returns 0 when `BFC_BOOTSTRAP=1`, otherwise 1; used to select stream bootstrap mode |
| `14` | NUL-terminated message at offset 2 | writes the message to standard error; returns 1 on success |

`bf.py` remains a reference interpreter for standard Brainfuck. `bf-run.c`
implements the runtime extension for bootstrapping and runtime-enabled programs.
The generated module uses a zero-initialized tape that grows as needed in both
directions. It embeds up to 1,000,000 Brainfuck commands; EOF input reads as
zero.
