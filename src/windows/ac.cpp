/*
Copyright (C) 2006 r1ch.net

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

//
// r1ch.net anticheat support
//

#include "client.h"

typedef PVOID (*FNINIT)(VOID);

STATIC PVOID anticheatApi;
STATIC FNINIT anticheatInit;
STATIC HMODULE anticheatHandle;

bool Sys_GetAntiCheatAPI(void)
{
    bool updated = false;

    //already loaded, just reinit
    if (anticheatInit) {
        anticheatApi = anticheatInit();
        if (!anticheatApi) {
            Com_LPrintf(PRINT_ERROR, "Anticheat failed to reinitialize!\n");
            FreeLibrary(anticheatHandle);
            anticheatHandle = NULL;
            anticheatInit = NULL;
            return false;
        }
        return true;
    }

reInit:
    anticheatHandle = LoadLibrary("anticheat");
    if (!anticheatHandle) {
        Com_LPrintf(PRINT_ERROR, "Anticheat failed to load.\n");
        return false;
    }

    //this should never fail unless the anticheat.dll is bad
    anticheatInit = (FNINIT)GetProcAddress(
                        anticheatHandle, "Initialize");
    if (!anticheatInit) {
        Com_LPrintf(PRINT_ERROR,
                    "Couldn't get API of anticheat.dll!\n"
                    "Please check you are using a valid "
                    "anticheat.dll from http://antiche.at/");
        FreeLibrary(anticheatHandle);
        anticheatHandle = NULL;
        return false;
    }

    anticheatApi = anticheatInit();
    if (anticheatApi) {
        return true; // succeeded
    }

    FreeLibrary(anticheatHandle);
    anticheatHandle = NULL;
    anticheatInit = NULL;
    if (!updated) {
        updated = true;
        goto reInit;
    }

    Com_LPrintf(PRINT_ERROR, "Anticheat failed to initialize.\n");

    return false;
}

