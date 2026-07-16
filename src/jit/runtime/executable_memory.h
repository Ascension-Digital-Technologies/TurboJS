#ifndef TURBOJS_EXECUTABLE_MEMORY_H
#define TURBOJS_EXECUTABLE_MEMORY_H

#include <stddef.h>

void *turbojs_executable_memory_allocate(size_t size);
int turbojs_executable_memory_seal(void *memory, size_t size);
void turbojs_executable_memory_free(void *memory, size_t size);

#endif
