#ifndef BF_RUNTIME_H
#define BF_RUNTIME_H

#include <stddef.h>
#include <stdint.h>

void bf_runtime_init(int argc, char **argv);
int bf_runtime_getchar(void);
int bf_runtime_putchar(int byte);
void bf_runtime_ffi(uint8_t **tape, size_t *capacity, size_t pointer);

#endif
