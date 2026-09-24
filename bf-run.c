#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bf_runtime.h"

enum { INITIAL_TAPE_SIZE = 16 };

typedef struct {
    unsigned char op;
    size_t count;
    size_t jump;
} Instruction;

static int is_command(int byte) {
    return byte == '>' || byte == '<' || byte == '+' || byte == '-' ||
           byte == '.' || byte == ',' || byte == '[' || byte == ']' ||
           byte == '!';
}

static void free_program(Instruction *program, size_t *stack,
                         unsigned char *source) {
    free(source);
    free(stack);
    free(program);
}

static int grow_right(unsigned char **tape, size_t *capacity,
                      size_t required) {
    size_t new_capacity = *capacity;
    while (new_capacity < required) {
        if (new_capacity > SIZE_MAX / 2) {
            return 0;
        }
        new_capacity *= 2;
    }
    unsigned char *new_tape = realloc(*tape, new_capacity);
    if (new_tape == NULL) {
        return 0;
    }
    memset(new_tape + *capacity, 0, new_capacity - *capacity);
    *tape = new_tape;
    *capacity = new_capacity;
    return 1;
}

static int grow_left(unsigned char **tape, size_t *capacity, size_t *pointer,
                     size_t distance) {
    size_t new_capacity = *capacity;
    while (new_capacity - *capacity < distance) {
        if (new_capacity > SIZE_MAX / 2) {
            return 0;
        }
        new_capacity *= 2;
    }
    size_t shift = new_capacity - *capacity;
    unsigned char *new_tape = realloc(*tape, new_capacity);
    if (new_tape == NULL) {
        return 0;
    }
    memmove(new_tape + shift, new_tape, *capacity);
    memset(new_tape, 0, shift);
    *pointer += shift - distance;
    *tape = new_tape;
    *capacity = new_capacity;
    return 1;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: bf-run PROGRAM.bf [ARG ...]\n");
        return 2;
    }
    bf_runtime_init(argc - 1, argv + 1);

    FILE *file = fopen(argv[1], "rb");
    if (file == NULL) {
        perror(argv[1]);
        return 1;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        perror("fseek");
        fclose(file);
        return 1;
    }
    long file_size = ftell(file);
    if (file_size < 0 || fseek(file, 0, SEEK_SET) != 0) {
        perror("reading program size");
        fclose(file);
        return 1;
    }

    size_t source_size = (size_t)file_size;
    unsigned char *source = malloc(source_size == 0 ? 1 : source_size);
    Instruction *program = calloc(source_size + 1, sizeof(*program));
    size_t *stack = malloc((source_size + 1) * sizeof(*stack));
    if (source == NULL || program == NULL || stack == NULL) {
        fprintf(stderr, "bf-run: out of memory\n");
        free_program(program, stack, source);
        fclose(file);
        return 1;
    }
    if (fread(source, 1, source_size, file) != source_size) {
        perror("reading program");
        free_program(program, stack, source);
        fclose(file);
        return 1;
    }
    fclose(file);

    size_t length = 0;
    for (size_t i = 0; i < source_size; ++i) {
        unsigned char op = source[i];
        if (!is_command(op)) {
            continue;
        }
        if ((op == '>' || op == '<' || op == '+' || op == '-') &&
            length > 0 && program[length - 1].op == op) {
            ++program[length - 1].count;
        } else {
            program[length].op = op;
            program[length].count = 1;
            program[length].jump = 0;
            ++length;
        }
    }

    size_t stack_size = 0;
    for (size_t pc = 0; pc < length; ++pc) {
        if (program[pc].op == '[') {
            stack[stack_size++] = pc;
        } else if (program[pc].op == ']') {
            if (stack_size == 0) {
                fprintf(stderr, "bf-run: unmatched ] at command %zu\n", pc);
                free_program(program, stack, source);
                return 1;
            }
            size_t open = stack[--stack_size];
            program[open].jump = pc;
            program[pc].jump = open;
        }
    }
    if (stack_size != 0) {
        fprintf(stderr, "bf-run: unmatched [ at command %zu\n",
                stack[stack_size - 1]);
        free_program(program, stack, source);
        return 1;
    }

    size_t tape_capacity = INITIAL_TAPE_SIZE;
    unsigned char *tape = calloc(tape_capacity, sizeof(*tape));
    if (tape == NULL) {
        fprintf(stderr, "bf-run: unable to allocate tape\n");
        free_program(program, stack, source);
        return 1;
    }
    size_t pointer = 0;
    size_t pc = 0;
    while (pc < length) {
        Instruction instruction = program[pc];
        switch (instruction.op) {
        case '>':
            if (instruction.count >= SIZE_MAX - pointer ||
                !grow_right(&tape, &tape_capacity,
                            pointer + instruction.count + 1)) {
                fprintf(stderr, "bf-run: unable to grow tape\n");
                free(tape);
                free_program(program, stack, source);
                return 1;
            }
            pointer += instruction.count;
            break;
        case '<':
            if (instruction.count > pointer) {
                if (!grow_left(&tape, &tape_capacity, &pointer,
                               instruction.count - pointer)) {
                    fprintf(stderr, "bf-run: unable to grow tape\n");
                    free(tape);
                    free_program(program, stack, source);
                    return 1;
                }
            } else {
                pointer -= instruction.count;
            }
            break;
        case '+':
            tape[pointer] = (unsigned char)(tape[pointer] + instruction.count);
            break;
        case '-':
            tape[pointer] = (unsigned char)(tape[pointer] - instruction.count);
            break;
        case '.':
            if (bf_runtime_putchar(tape[pointer]) == EOF) {
                free(tape);
                free_program(program, stack, source);
                return 1;
            }
            break;
        case ',': {
            int byte = bf_runtime_getchar();
            tape[pointer] = byte == EOF ? 0 : (unsigned char)byte;
            break;
        }
        case '[':
            if (tape[pointer] == 0) {
                pc = instruction.jump;
            }
            break;
        case ']':
            if (tape[pointer] != 0) {
                pc = instruction.jump;
            }
            break;
        case '!':
            bf_runtime_ffi(&tape, &tape_capacity, pointer);
            break;
        default:
            break;
        }
        ++pc;
    }

    free(tape);
    free_program(program, stack, source);
    return 0;
}
