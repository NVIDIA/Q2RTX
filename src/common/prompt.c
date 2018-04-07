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

//
// prompt.c
//

#include "shared/shared.h"
#include "common/common.h"
#include "common/cvar.h"
#include "common/field.h"
#include "common/files.h"
#include "common/prompt.h"

static cvar_t   *com_completion_mode;
static cvar_t   *com_completion_treshold;

static void Prompt_ShowMatches(commandPrompt_t *prompt, char **matches,
                               int start, int end)
{
    int count = end - start;
    int numCols = 7, numLines;
    int i, j, k;
    size_t maxlen, len, total;
    size_t colwidths[6];
    char *match;

    // determine number of columns needed
    do {
        numCols--;
        numLines = ceil((float)count / numCols);
        total = 0;
        for (i = 0; i < numCols; i++) {
            k = start + numLines * i;
            if (k >= end) {
                break;
            }
            maxlen = 0;
            for (j = k; j < k + numLines && j < end; j++) {
                len = strlen(matches[j]);
                if (maxlen < len) {
                    maxlen = len;
                }
            }
            maxlen += 2; // account for intercolumn spaces
            if (maxlen > prompt->widthInChars) {
                maxlen = prompt->widthInChars;
            }
            colwidths[i] = maxlen;
            total += maxlen;
        }
        if (total < prompt->widthInChars) {
            break; // this number of columns does fit
        }
    } while (numCols > 1);

    for (i = 0; i < numLines; i++) {
        for (j = 0; j < numCols; j++) {
            k = start + j * numLines + i;
            if (k >= end) {
                break;
            }
            match = matches[k];
            prompt->printf("%s", match);
            len = strlen(match);
            if (len < colwidths[j]) {
                // pad with spaces
                for (k = 0; k < colwidths[j] - len; k++) {
                    prompt->printf(" ");
                }
            }
        }
        prompt->printf("\n");
    }
}

static void Prompt_ShowIndividualMatches(
    commandPrompt_t *prompt,
    char            **matches,
    int             numCommands,
    int             numAliases,
    int             numCvars)
{
    int offset = 0;

    if (numCommands) {
        qsort(matches + offset, numCommands, sizeof(matches[0]), SortStrcmp);

        prompt->printf("\n%i possible command%s:\n",
                       numCommands, numCommands != 1 ? "s" : "");

        Prompt_ShowMatches(prompt, matches, offset, offset + numCommands);
        offset += numCommands;
    }

    if (numCvars) {
        qsort(matches + offset, numCvars, sizeof(matches[0]), SortStrcmp);

        prompt->printf("\n%i possible variable%s:\n",
                       numCvars, numCvars != 1 ? "s" : "");

        Prompt_ShowMatches(prompt, matches, offset, offset + numCvars);
        offset += numCvars;
    }

    if (numAliases) {
        qsort(matches + offset, numAliases, sizeof(matches[0]), SortStrcmp);

        prompt->printf("\n%i possible alias%s:\n",
                       numAliases, numAliases != 1 ? "es" : "");

        Prompt_ShowMatches(prompt, matches, offset, offset + numAliases);
        offset += numAliases;
    }
}

static qboolean find_dup(genctx_t *ctx, const char *s)
{
    int i, r;

    for (i = 0; i < ctx->count; i++) {
        if (ctx->ignorecase)
            r = Q_strcasecmp(ctx->matches[i], s);
        else
            r = strcmp(ctx->matches[i], s);

        if (!r)
            return qtrue;
    }

    return qfalse;
}

qboolean Prompt_AddMatch(genctx_t *ctx, const char *s)
{
    int r;

    if (ctx->count >= ctx->size)
        return qfalse;

    if (ctx->ignorecase)
        r = Q_strncasecmp(ctx->partial, s, ctx->length);
    else
        r = strncmp(ctx->partial, s, ctx->length);

    if (r)
        return qtrue;

    if (ctx->ignoredups && find_dup(ctx, s))
        return qtrue;

    ctx->matches = Z_Realloc(ctx->matches, ALIGN(ctx->count + 1, MIN_MATCHES) * sizeof(char *));
    ctx->matches[ctx->count++] = Z_CopyString(s);
    return qtrue;
}

static qboolean needs_quotes(const char *s)
{
    int c;

    while (*s) {
        c = *s++;
        if (c == '$' || c == ';' || !Q_isgraph(c)) {
            return qtrue;
        }
    }

    return qfalse;
}

/*
====================
Prompt_CompleteCommand
====================
*/
void Prompt_CompleteCommand(commandPrompt_t *prompt, qboolean backslash)
{
    inputField_t *inputLine = &prompt->inputLine;
    char *first, *last, *text, **sorted;
    int i, j, c, pos, size, argnum;
    genctx_t ctx;
    int numCommands, numCvars, numAliases;

    if (!inputLine->maxChars)
        return;

    text = inputLine->text;
    size = inputLine->maxChars + 1;
    pos = inputLine->cursorPos;

    // prepend backslash if missing
    if (backslash) {
        if (*text != '\\' && *text != '/') {
            memmove(text + 1, text, size - 1);
            *text = '\\';
        }
        text++;
        size--;
        pos--;
    }

    // skip previous parts if command line is multi-part
    for (i = j = c = 0; i < pos && text[i]; i++) {
        if (text[i] == '"')
            c ^= 1;
        else if (!c && text[i] == ';')
            j = i + 1;
    }
    if (j > 0) {
        text += j;
        size -= j;
        pos -= j;
    }

    // parse the input line into tokens
    Cmd_TokenizeString(text, qfalse);

    // determine argument number to be completed
    argnum = Cmd_FindArgForOffset(pos);

    // generate matches
    memset(&ctx, 0, sizeof(ctx));
    ctx.partial = Cmd_Argv(argnum);
    ctx.length = strlen(ctx.partial);
    ctx.argnum = argnum;
    ctx.size = MAX_MATCHES;

    if (argnum) {
        // complete a command/cvar argument
        Com_Generic_c(&ctx, argnum);
        numCommands = numCvars = numAliases = 0;
    } else {
        // complete a command/cvar/alias name
        Cmd_Command_g(&ctx);
        numCommands = ctx.count;

        Cvar_Variable_g(&ctx);
        numCvars = ctx.count - numCommands;

        Cmd_Alias_g(&ctx);
        numAliases = ctx.count - numCvars - numCommands;
    }

    if (!ctx.count) {
        pos = strlen(inputLine->text);
        prompt->tooMany = qfalse;
        goto finish; // nothing found
    }

    if (ctx.count > Cvar_ClampInteger(com_completion_treshold, 1, MAX_MATCHES) && !prompt->tooMany) {
        prompt->printf("Press TAB again to display all %d possibilities.\n", ctx.count);
        pos = strlen(inputLine->text);
        prompt->tooMany = qtrue;
        goto finish;
    }

    prompt->tooMany = qfalse;

    // truncate at current argument position
    text[Cmd_ArgOffset(argnum)] = 0;

    // append whitespace if completing a new argument
    if (argnum == Cmd_Argc()) {
        Q_strlcat(text, " ", size);
    }

    if (ctx.count == 1) {
        // we have finished completion!
        if (needs_quotes(ctx.matches[0])) {
            Q_strlcat(text, "\"", size);
            Q_strlcat(text, ctx.matches[0], size);
            Q_strlcat(text, "\"", size);
        } else {
            Q_strlcat(text, ctx.matches[0], size);
        }

        pos = strlen(inputLine->text);
        Q_strlcat(text, " ", size);

        // copy trailing arguments
        if (argnum + 1 < Cmd_Argc())
            Q_strlcat(text, Cmd_RawArgsFrom(argnum + 1), size);
        else
            pos++;
        goto finish;
    }

    // sort matches alphabethically
    sorted = Z_Malloc(ctx.count * sizeof(sorted[0]));
    memcpy(sorted, ctx.matches, ctx.count * sizeof(sorted[0]));
    qsort(sorted, ctx.count, sizeof(sorted[0]), ctx.ignorecase ? SortStricmp : SortStrcmp);

    // copy matching part
    first = sorted[0];
    last = sorted[ctx.count - 1];
    do {
        if (*first != *last && (!ctx.ignorecase || Q_tolower(*first) != Q_tolower(*last))) {
            break;
        }
        first++;
        last++;
    } while (*first);

    c = *first;
    *first = 0;
    Q_strlcat(text, sorted[0], size);
    *first = c;

    pos = strlen(inputLine->text);

    // copy trailing arguments
    if (argnum + 1 < Cmd_Argc()) {
        Q_strlcat(text, " ", size);
        Q_strlcat(text, Cmd_RawArgsFrom(argnum + 1), size);
    }

    prompt->printf("]\\%s\n", Cmd_ArgsFrom(0));
    if (argnum) {
        goto multi;
    }

    switch (com_completion_mode->integer) {
    case 0:
        // print in solid list
        for (i = 0; i < ctx.count; i++) {
            prompt->printf("%s\n", sorted[i]);
        }
        break;
    case 1:
    multi:
        // print in multiple columns
        Prompt_ShowMatches(prompt, sorted, 0, ctx.count);
        break;
    case 2:
    default:
        // resort matches by type and print in multiple columns
        Prompt_ShowIndividualMatches(prompt, ctx.matches, numCommands, numAliases, numCvars);
        break;
    }

    Z_Free(sorted);

finish:
    // free matches
    for (i = 0; i < ctx.count; i++) {
        Z_Free(ctx.matches[i]);
    }
    Z_Free(ctx.matches);

    // move cursor
    inputLine->cursorPos = min(pos, inputLine->maxChars - 1);
}

void Prompt_CompleteHistory(commandPrompt_t *prompt, qboolean forward)
{
    char *s, *m = NULL;
    int i, j;

    if (!prompt->search) {
        s = prompt->inputLine.text;
        if (*s == '/' || *s == '\\') {
            s++;
        }
        if (!*s) {
            return;
        }
        prompt->search = Z_CopyString(s);
    }

    if (forward) {
        for (i = prompt->historyLineNum + 1; i < prompt->inputLineNum; i++) {
            s = prompt->history[i & HISTORY_MASK];
            if (s && strstr(s, prompt->search)) {
                if (strcmp(s, prompt->inputLine.text)) {
                    m = s;
                    break;
                }
            }
        }
    } else {
        j = prompt->inputLineNum - HISTORY_SIZE;
        if (j < 0) {
            j = 0;
        }
        for (i = prompt->historyLineNum - 1; i >= j; i--) {
            s = prompt->history[i & HISTORY_MASK];
            if (s && strstr(s, prompt->search)) {
                if (strcmp(s, prompt->inputLine.text)) {
                    m = s;
                    break;
                }
            }
        }
    }

    if (!m) {
        return;
    }

    prompt->historyLineNum = i;
    IF_Replace(&prompt->inputLine, prompt->history[i & HISTORY_MASK]);
}

void Prompt_ClearState(commandPrompt_t *prompt)
{
    prompt->tooMany = qfalse;
    if (prompt->search) {
        Z_Free(prompt->search);
        prompt->search = NULL;
    }
}

/*
====================
Prompt_Action

User just pressed enter
====================
*/
char *Prompt_Action(commandPrompt_t *prompt)
{
    char *s = prompt->inputLine.text;
    int i, j;

    Prompt_ClearState(prompt);
    if (s[0] == 0 || ((s[0] == '/' || s[0] == '\\') && s[1] == 0)) {
        IF_Clear(&prompt->inputLine);
        return NULL; // empty line
    }

    // save current line in history
    i = prompt->inputLineNum & HISTORY_MASK;
    j = (prompt->inputLineNum - 1) & HISTORY_MASK;
    if (!prompt->history[j] || strcmp(prompt->history[j], s)) {
        if (prompt->history[i]) {
            Z_Free(prompt->history[i]);
        }
        prompt->history[i] = Z_CopyString(s);
        prompt->inputLineNum++;
    } else {
        i = j;
    }

    // stop history search
    prompt->historyLineNum = prompt->inputLineNum;

    IF_Clear(&prompt->inputLine);

    return prompt->history[i];
}

/*
====================
Prompt_HistoryUp
====================
*/
void Prompt_HistoryUp(commandPrompt_t *prompt)
{
    int i;

    Prompt_ClearState(prompt);

    if (prompt->historyLineNum == prompt->inputLineNum) {
        // save current line in history
        i = prompt->inputLineNum & HISTORY_MASK;
        if (prompt->history[i]) {
            Z_Free(prompt->history[i]);
        }
        prompt->history[i] = Z_CopyString(prompt->inputLine.text);
    }

    if (prompt->inputLineNum - prompt->historyLineNum < HISTORY_SIZE &&
        prompt->historyLineNum > 0) {
        prompt->historyLineNum--;
    }

    i = prompt->historyLineNum & HISTORY_MASK;
    IF_Replace(&prompt->inputLine, prompt->history[i]);
}

/*
====================
Prompt_HistoryDown
====================
*/
void Prompt_HistoryDown(commandPrompt_t *prompt)
{
    int i;

    Prompt_ClearState(prompt);

    if (prompt->historyLineNum == prompt->inputLineNum) {
        return;
    }

    prompt->historyLineNum++;

    i = prompt->historyLineNum & HISTORY_MASK;
    IF_Replace(&prompt->inputLine, prompt->history[i]);
}

/*
====================
Prompt_Clear
====================
*/
void Prompt_Clear(commandPrompt_t *prompt)
{
    int i;

    Prompt_ClearState(prompt);

    for (i = 0; i < HISTORY_SIZE; i++) {
        if (prompt->history[i]) {
            Z_Free(prompt->history[i]);
            prompt->history[i] = NULL;
        }
    }

    prompt->historyLineNum = 0;
    prompt->inputLineNum = 0;

    IF_Clear(&prompt->inputLine);
}

void Prompt_SaveHistory(commandPrompt_t *prompt, const char *filename, int lines)
{
    qhandle_t f;
    char *s;
    int i;

    FS_FOpenFile(filename, &f, FS_MODE_WRITE | FS_PATH_BASE);
    if (!f) {
        return;
    }

    if (lines > HISTORY_SIZE) {
        lines = HISTORY_SIZE;
    }

    i = prompt->inputLineNum - lines;
    if (i < 0) {
        i = 0;
    }
    for (; i < prompt->inputLineNum; i++) {
        s = prompt->history[i & HISTORY_MASK];
        if (s) {
            FS_FPrintf(f, "%s\n", s);
        }
    }

    FS_FCloseFile(f);
}

void Prompt_LoadHistory(commandPrompt_t *prompt, const char *filename)
{
    char buffer[MAX_FIELD_TEXT];
    qhandle_t f;
    int i;
    ssize_t len;

    FS_FOpenFile(filename, &f, FS_MODE_READ | FS_TYPE_REAL | FS_PATH_BASE);
    if (!f) {
        return;
    }

    for (i = 0; i < HISTORY_SIZE; i++) {
        if ((len = FS_ReadLine(f, buffer, sizeof(buffer))) < 1) {
            break;
        }
        if (prompt->history[i]) {
            Z_Free(prompt->history[i]);
        }
        prompt->history[i] = memcpy(Z_Malloc(len + 1), buffer, len + 1);
    }

    FS_FCloseFile(f);

    prompt->historyLineNum = i;
    prompt->inputLineNum = i;
}

/*
====================
Prompt_Init
====================
*/
void Prompt_Init(void)
{
    com_completion_mode = Cvar_Get("com_completion_mode", "1", 0);
    com_completion_treshold = Cvar_Get("com_completion_treshold", "50", 0);
}

