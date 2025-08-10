/**
 * Copyright (c) 2025, Timothy Stack
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * * Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 * * Neither the name of Timothy Stack nor the names of its contributors
 * may be used to endorse or promote products derived from this software
 * without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ''AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "lnav.console.hh"

#if __has_include("windows.h")
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#define HAVE_WINDOWS_H
#endif

namespace lnav::console {
bool
only_process_attached_to_win32_console()
{
#if defined(HAVE_WINDOWS_H)
    DWORD procIDs[2];
    DWORD count = GetConsoleProcessList((LPDWORD) procIDs, 2);
    return count == 1;
#else
    return false;
#endif
}

void
get_command_line_args(int* argc, char*** argv)
{
#if defined(HAVE_WINDOWS_H)
    // Get the command line arguments as wchar_t strings
    wchar_t** wargv = CommandLineToArgvW(GetCommandLineW(), argc);
    if (!wargv) {
        *argc = 0;
        *argv = NULL;
        return;
    }

    // Count the number of bytes necessary to store the UTF-8 versions of those strings
    int n = 0;
    for (int i = 0; i < *argc; i++)
        n += WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, NULL, 0, NULL, NULL)
            + 1;

    // Allocate the argv[] array + all the UTF-8 strings
    *argv = (char**) malloc((*argc + 1) * sizeof(char*) + n);
    if (!*argv) {
        *argc = 0;
        return;
    }

    // Convert all wargv[] --> argv[]
    char* arg = (char*) &((*argv)[*argc + 1]);
    for (int i = 0; i < *argc; i++) {
        (*argv)[i] = arg;
        arg += WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, arg, n, NULL, NULL)
            + 1;
    }
    (*argv)[*argc] = NULL;
#endif
}

}
