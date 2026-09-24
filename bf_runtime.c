#define _POSIX_C_SOURCE 200809L

#include "bf_runtime.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <spawn.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

enum {
    BF_FFI_ARGC = 1,
    BF_FFI_ARGV = 2,
    BF_FFI_OPEN = 3,
    BF_FFI_READ = 4,
    BF_FFI_WRITE = 5,
    BF_FFI_CLOSE = 6,
    BF_FFI_SET_INPUT = 7,
    BF_FFI_SET_OUTPUT = 8,
    BF_FFI_SYSTEM = 9,
    BF_FFI_TEMP = 10,
    BF_FFI_CLANG = 11,
    BF_FFI_EXIT = 12,
    BF_FFI_DRIVER_MODE = 13,
    BF_FFI_DIAGNOSTIC = 14,
    BF_MAX_HANDLES = 256,
    BF_MAX_ARGUMENT = 4096,
};

static int runtime_argc;
static char **runtime_argv;
static FILE *handles[BF_MAX_HANDLES];
static char *temporary_paths[BF_MAX_HANDLES];
static uint8_t last_temporary_handle;
static FILE *runtime_input;
static FILE *runtime_output;
static int runtime_error;
extern char **environ;

void bf_runtime_init(int argc, char **argv) {
    /* argv exposed to Brainfuck excludes the executable name. */
    runtime_argc = argc > 1 ? argc - 1 : 0;
    runtime_argv = argc > 1 ? argv + 1 : NULL;
    memset(handles, 0, sizeof(handles));
    memset(temporary_paths, 0, sizeof(temporary_paths));
    last_temporary_handle = 0;
    runtime_error = 0;
    runtime_input = stdin;
    runtime_output = stdout;
}

int bf_runtime_getchar(void) {
    if (runtime_error) {
        return 0;
    }
    int byte = fgetc(runtime_input == NULL ? stdin : runtime_input);
    return byte == EOF ? 0 : byte;
}

int bf_runtime_putchar(int byte) {
    if (runtime_error) {
        return EOF;
    }
    return fputc(byte, runtime_output == NULL ? stdout : runtime_output);
}

static int ensure_tape(uint8_t **tape, size_t *capacity, size_t required) {
    if (required <= *capacity) {
        return 1;
    }
    size_t next = *capacity == 0 ? 16 : *capacity;
    while (next < required) {
        if (next > SIZE_MAX / 2) {
            return 0;
        }
        next *= 2;
    }
    uint8_t *resized = realloc(*tape, next);
    if (resized == NULL) {
        return 0;
    }
    memset(resized + *capacity, 0, next - *capacity);
    *tape = resized;
    *capacity = next;
    return 1;
}

static FILE *get_handle(uint8_t id) {
    if (id == 0) {
        return NULL;
    }
    return handles[id];
}

static uint8_t allocate_handle(FILE *file) {
    if (file == NULL) {
        return 0;
    }
    for (unsigned int i = 1; i < BF_MAX_HANDLES; ++i) {
        if (handles[i] == NULL) {
            handles[i] = file;
            return (uint8_t)i;
        }
    }
    fclose(file);
    return 0;
}

static const char *tape_string(uint8_t *tape, size_t capacity, size_t start) {
    if (start >= capacity || memchr(tape + start, 0, capacity - start) == NULL) {
        return NULL;
    }
    return (const char *)(tape + start);
}

static void finish_command(uint8_t *tape, size_t pointer, uint8_t result) {
    tape[pointer] = result;
}

void bf_runtime_ffi(uint8_t **tape_address, size_t *capacity_address,
                    size_t pointer) {
    if (tape_address == NULL || capacity_address == NULL ||
        *tape_address == NULL || pointer > SIZE_MAX - BF_MAX_ARGUMENT - 2 ||
        !ensure_tape(tape_address, capacity_address,
                     pointer + BF_MAX_ARGUMENT + 2)) {
        return;
    }

    uint8_t *tape = *tape_address;
    size_t capacity = *capacity_address;
    uint8_t operation = tape[pointer];
    switch (operation) {
    case BF_FFI_ARGC:
        finish_command(tape, pointer,
                      runtime_argc > 255 ? 255 : (uint8_t)runtime_argc);
        break;
    case BF_FFI_ARGV: {
        unsigned int index = tape[pointer + 1];
        const char *argument =
            index < (unsigned int)runtime_argc ? runtime_argv[index] : "";
        size_t size = strnlen(argument, BF_MAX_ARGUMENT - 1);
        memcpy(tape + pointer + 2, argument, size);
        tape[pointer + 2 + size] = 0;
        finish_command(tape, pointer, (uint8_t)size);
        break;
    }
    case BF_FFI_OPEN: {
        const char *path = tape_string(tape, capacity, pointer + 2);
        uint8_t mode = tape[pointer + 1];
        const char *open_mode = mode == 1 ? "wb" : mode == 2 ? "ab" : "rb";
        FILE *file = path == NULL ? NULL : fopen(path, open_mode);
        uint8_t handle = allocate_handle(file);
        if (handle == 0) {
            runtime_error = 1;
            if (path != NULL) {
                perror(path);
            }
        } else if (mode == 0) {
            runtime_input = file;
        }
        finish_command(tape, pointer, handle);
        break;
    }
    case BF_FFI_READ: {
        FILE *file = get_handle(tape[pointer + 1]);
        int byte = file == NULL ? EOF : fgetc(file);
        finish_command(tape, pointer, byte == EOF ? 0 : (uint8_t)byte);
        break;
    }
    case BF_FFI_WRITE: {
        FILE *file = get_handle(tape[pointer + 1]);
        int status = file == NULL ? EOF : fputc(tape[pointer + 2], file);
        finish_command(tape, pointer, status == EOF ? 0 : 1);
        break;
    }
    case BF_FFI_CLOSE: {
        uint8_t id = tape[pointer + 1];
        int status = id == 0 || handles[id] == NULL ? EOF : fclose(handles[id]);
        if (id != 0 && status != EOF) {
            handles[id] = NULL;
        }
        finish_command(tape, pointer, status == EOF ? 0 : 1);
        break;
    }
    case BF_FFI_SET_INPUT: {
        FILE *file = get_handle(tape[pointer + 1]);
        if (tape[pointer + 1] == 0) {
            runtime_input = stdin;
            finish_command(tape, pointer, 1);
        } else {
            runtime_input = file;
            finish_command(tape, pointer, file == NULL ? 0 : 1);
        }
        break;
    }
    case BF_FFI_SET_OUTPUT: {
        FILE *file = get_handle(tape[pointer + 1]);
        fflush(runtime_output == NULL ? stdout : runtime_output);
        if (tape[pointer + 1] == 0) {
            runtime_output = stdout;
            finish_command(tape, pointer, 1);
        } else {
            runtime_output = file;
            finish_command(tape, pointer, file == NULL ? 0 : 1);
        }
        break;
    }
    case BF_FFI_SYSTEM: {
        const char *command = tape_string(tape, capacity, pointer + 2);
        int status = command == NULL ? -1 : system(command);
        if (status >= 0 && WIFEXITED(status)) {
            status = WEXITSTATUS(status);
        }
        finish_command(tape, pointer, status == 0 ? 1 : 0);
        break;
    }
    case BF_FFI_TEMP: {
        static const char pattern[] = "/tmp/bfc-XXXXXX";
        char path[sizeof(pattern)];
        memcpy(path, pattern, sizeof(pattern));
        int descriptor = mkstemp(path);
        FILE *file = descriptor < 0 ? NULL : fdopen(descriptor, "wb+");
        if (descriptor >= 0 && file == NULL) {
            close(descriptor);
            unlink(path);
        }
        uint8_t handle = allocate_handle(file);
        if (handle == 0) {
            runtime_error = 1;
            unlink(path);
            finish_command(tape, pointer, 0);
            break;
        }
        temporary_paths[handle] = strdup(path);
        if (temporary_paths[handle] == NULL) {
            runtime_error = 1;
            fclose(file);
            handles[handle] = NULL;
            unlink(path);
            perror("bfc: storing temporary path");
            finish_command(tape, pointer, 0);
            break;
        }
        last_temporary_handle = handle;
        runtime_output = file;
        size_t length = strlen(path);
        memcpy(tape + pointer + 2, path, length + 1);
        finish_command(tape, pointer, handle);
        break;
    }
    case BF_FFI_CLANG: {
        if (last_temporary_handle == 0) {
            finish_command(tape, pointer, runtime_error ? 1 : 0);
            break;
        }
        uint8_t handle = last_temporary_handle;
        FILE *file = handles[handle];
        if (file != NULL) {
            fflush(file);
            fclose(file);
            handles[handle] = NULL;
        }
        runtime_output = stdout;

        const char *output = tape_string(tape, capacity, pointer + 2);
        if (output == NULL || output[0] == 0) {
            output = "a.out";
        }
        const char *runtime_library = getenv("BFC_RUNTIME_LIB");
        if (runtime_library == NULL || runtime_library[0] == 0) {
            fprintf(stderr, "bfc: BFC_RUNTIME_LIB is not set\n");
            runtime_error = 1;
            finish_command(tape, pointer, 0);
            break;
        }
        const char *clang = getenv("CLANG");
        if (clang == NULL || clang[0] == 0) {
            clang = "clang";
        }
        char *arguments[] = {
            (char *)clang, "-O2", "-x", "ir", temporary_paths[handle],
            "-x", "none", (char *)runtime_library, "-o", (char *)output, NULL
        };
        pid_t child;
        int spawn_status = posix_spawnp(&child, clang, NULL, NULL, arguments,
                                        environ);
        int child_status = 0;
        pid_t waited = -1;
        if (spawn_status == 0) {
            do {
                waited = waitpid(child, &child_status, 0);
            } while (waited < 0 && errno == EINTR);
        } else {
            errno = spawn_status;
            perror("bfc: starting clang");
        }
        if (spawn_status == 0 && waited < 0) {
            perror("bfc: waiting for clang");
        }
        int success = waited == child && WIFEXITED(child_status) &&
                      WEXITSTATUS(child_status) == 0;
        if (!success) {
            runtime_error = 1;
        }
        unlink(temporary_paths[handle]);
        free(temporary_paths[handle]);
        temporary_paths[handle] = NULL;
        last_temporary_handle = 0;
        finish_command(tape, pointer, success ? 0 : 1);
        break;
    }
    case BF_FFI_EXIT:
        exit(tape[pointer + 1]);
    case BF_FFI_DRIVER_MODE: {
        const char *bootstrap = getenv("BFC_BOOTSTRAP");
        finish_command(tape, pointer,
                       bootstrap != NULL && strcmp(bootstrap, "1") == 0 ? 0 : 1);
        break;
    }
    case BF_FFI_DIAGNOSTIC: {
        const char *message = tape_string(tape, capacity, pointer + 2);
        int success = message != NULL && fputs(message, stderr) >= 0 &&
                      fflush(stderr) == 0;
        finish_command(tape, pointer, success ? 1 : 0);
        break;
    }
    default:
        finish_command(tape, pointer, 0);
        break;
    }
}
