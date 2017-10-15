/**
 * Implementation of the
 * $(LINK2 http://www.digitalmars.com/download/freecompiler.html, Digital Mars C/C++ Compiler).
 *
 * Copyright:   Copyright (c) 1985-1998 by Symantec, All Rights Reserved
 *              Copyright (c) 2000-2017 by Digital Mars, All Rights Reserved
 * Authors:     $(LINK2 http://www.digitalmars.com, Walter Bright)
 * License:     Distributed under the Boost Software License, Version 1.0.
 *              http://www.boost.org/LICENSE_1_0.txt
 * Source:      https://github.com/DigitalMars/Compiler/blob/master/dm/src/dmc/dmcdll.di
 */

module dmcdll;

extern (C++):

void dmcdll_command_line(int argc, char **argv, const(char)* copyright);
bool dmcdll_first_compile();
void dmcdll_file_term();
char *dmcdll_nettranslate(const(char)* filename, const(char)* mode);
char *dmcdll_TranslateFileName(char *filename, char *mode);
void dmcdll_DisposeFile(char *filename);
void dmcdll_SpawnFile(const(char)* filename, int includelevel);
void dmcdll_SpawnFile(const(char)* filename);
bool dmcdll_Progress(int linnum);
bool dmcdll_build_server();
bool dmcdll_dump_compile_context();

import core.stdc.stdarg;
void dmcdll_html_err(const(char)* srcname, uint linnum, const(char)* format, va_list ap);

void err_reportmsgf_error(const(char)* format, va_list args);
void err_reportmsgf_fatal(const(char)* format, va_list args);
void err_reportmsgf_continue(const(char)* format, va_list args);
void err_reportmsgf_warning(bool warniserr, int warnum, const(char)* format, va_list args);

extern (C) alias HookFp = void function();
void dmcdll_HookDetach(HookFp fp);

void *dmcdll_PersistentAlloc(int size);

