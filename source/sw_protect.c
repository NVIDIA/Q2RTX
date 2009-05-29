#include "config.h"
#include "q_shared.h"

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
void Sys_MakeCodeWriteable (unsigned long startaddr, unsigned long length) {
#ifdef _WIN32
    DWORD  flOldProtect;

    if (!VirtualProtect((LPVOID)startaddr, length, PAGE_READWRITE, &flOldProtect))
        Com_Error(ERR_FATAL, "Protection change failed\n");
#else
    int r;
    unsigned long addr;
    int psize = getpagesize();

    addr = (startaddr & ~(psize-1)) - psize;

    r = mprotect((char*)addr, length + startaddr - addr + psize, 7);
    if (r < 0)
            Com_Error( ERR_FATAL, "Protection change failed\n");
#endif
}

