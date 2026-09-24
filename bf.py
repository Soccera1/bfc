#!/usr/bin/env python3
"""Reference Brainfuck interpreter for bfc."""

import argparse
import sys


COMMANDS = set(b"><+-.,[]")


def compile_brainfuck(source: bytes) -> tuple[list[tuple[int, int]], dict[int, int]]:
    code: list[tuple[int, int]] = []
    for byte in source:
        if byte not in COMMANDS:
            continue
        if byte in b"><+-" and code and code[-1][0] == byte:
            instruction, count = code[-1]
            code[-1] = (instruction, count + 1)
        else:
            code.append((byte, 1))

    brackets: dict[int, int] = {}
    stack: list[int] = []
    for pc, (instruction, _) in enumerate(code):
        if instruction == ord("["):
            stack.append(pc)
        elif instruction == ord("]"):
            if not stack:
                raise ValueError(f"unmatched ] at command {pc}")
            start = stack.pop()
            brackets[start] = pc
            brackets[pc] = start
    if stack:
        raise ValueError(f"unmatched [ at command {stack[-1]}")
    return code, brackets


def run(code: list[tuple[int, int]], brackets: dict[int, int], data: bytes) -> bytes:
    tape = bytearray(16)
    pointer = 0
    pc = 0
    input_pos = 0
    output = bytearray()
    while pc < len(code):
        op, count = code[pc]
        if op == ord(">"):
            pointer += count
            if pointer >= len(tape):
                tape.extend(bytes(pointer + 1 - len(tape)))
        elif op == ord("<"):
            if count > pointer:
                extra = count - pointer
                tape[:0] = bytes(extra)
                pointer = 0
            else:
                pointer -= count
        elif op == ord("+"):
            tape[pointer] = (tape[pointer] + count) & 0xFF
        elif op == ord("-"):
            tape[pointer] = (tape[pointer] - count) & 0xFF
        elif op == ord("."):
            output.append(tape[pointer])
        elif op == ord(","):
            tape[pointer] = data[input_pos] if input_pos < len(data) else 0
            input_pos += 1
        elif op == ord("[") and tape[pointer] == 0:
            pc = brackets[pc]
        elif op == ord("]") and tape[pointer] != 0:
            pc = brackets[pc]
        pc += 1
    return bytes(output)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("program", help="Brainfuck program file")
    parser.add_argument(
        "--append-nul",
        action="store_true",
        help="append a NUL byte to stdin (used to delimit compiler input)",
    )
    args = parser.parse_args()
    try:
        code, brackets = compile_brainfuck(open(args.program, "rb").read())
        data = sys.stdin.buffer.read()
        if args.append_nul:
            data += b"\0"
        sys.stdout.buffer.write(run(code, brackets, data))
    except (OSError, ValueError) as error:
        print(f"bf.py: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
