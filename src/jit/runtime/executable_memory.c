#include "executable_memory.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
void *turbojs_executable_memory_allocate(size_t size)
{
    return VirtualAlloc(NULL, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
}
int turbojs_executable_memory_seal(void *memory, size_t size)
{
    DWORD old_protect;
    if (!VirtualProtect(memory, size, PAGE_EXECUTE_READ, &old_protect))
        return 0;
    FlushInstructionCache(GetCurrentProcess(), memory, size);
    return 1;
}
void turbojs_executable_memory_free(void *memory, size_t size)
{
    (void)size;
    if (memory)
        VirtualFree(memory, 0, MEM_RELEASE);
}
#else
#include <sys/mman.h>
#include <unistd.h>
void *turbojs_executable_memory_allocate(size_t size)
{
    void *memory = mmap(NULL, size, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return memory == MAP_FAILED ? NULL : memory;
}
int turbojs_executable_memory_seal(void *memory, size_t size)
{
    if (mprotect(memory, size, PROT_READ | PROT_EXEC) != 0)
        return 0;
#if defined(__GNUC__) || defined(__clang__)
    __builtin___clear_cache((char *)memory, (char *)memory + size);
#endif
    return 1;
}
void turbojs_executable_memory_free(void *memory, size_t size)
{
    if (memory)
        munmap(memory, size);
}
#endif
