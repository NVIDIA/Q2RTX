#include "shared/shared.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>
#include <sys/mman.h>
#endif

/*
================
Sys_MakeCodeWriteable
================
*/
void Sys_MakeCodeWriteable(uintptr_t start, size_t length)
{
#ifdef _WIN32
    DWORD unused;

    if (!VirtualProtect((LPVOID)start, length, PAGE_EXECUTE_READWRITE, &unused))
        Com_Error(ERR_FATAL, "Protection change failed");
#else
    int psize = getpagesize();
    uintptr_t addr = (start & ~(psize - 1)) - psize;

    if (mprotect((void *)addr, length + start - addr + psize, PROT_READ | PROT_WRITE | PROT_EXEC))
        Com_Error(ERR_FATAL, "Protection change failed");
#endif
}

