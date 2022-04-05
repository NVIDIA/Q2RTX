/*
Copyright (C) 2003-2006 Andrey Nazarov

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

#ifndef PROMPT_H
#define PROMPT_H

#include "common/field.h"
#include "common/cmd.h"

#define HISTORY_SIZE    128
#define HISTORY_MASK    (HISTORY_SIZE - 1)

#define MIN_MATCHES     64
#define MAX_MATCHES     250000000

typedef struct commandPrompt_s {
    int     inputLineNum;
    int     historyLineNum;

    inputField_t inputLine;
    char        *history[HISTORY_SIZE];
    char        *search;

    int         widthInChars;
    bool        tooMany;

    void        (* q_printf(1, 2) printf)(const char *fmt, ...);
} commandPrompt_t;

void Prompt_Init(void);
void Prompt_AddMatch(genctx_t *ctx, const char *s);
void Prompt_CompleteCommand(commandPrompt_t *prompt, bool backslash);
void Prompt_CompleteHistory(commandPrompt_t *prompt, bool forward);
void Prompt_ClearState(commandPrompt_t *prompt);
char *Prompt_Action(commandPrompt_t *prompt);
void Prompt_HistoryUp(commandPrompt_t *prompt);
void Prompt_HistoryDown(commandPrompt_t *prompt);
void Prompt_Clear(commandPrompt_t *prompt);
void Prompt_SaveHistory(commandPrompt_t *prompt, const char *filename, int lines);
void Prompt_LoadHistory(commandPrompt_t *prompt, const char *filename);

#endif // PROMPT_H
