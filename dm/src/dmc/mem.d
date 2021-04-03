/**
 * Compiler implementation of the
 * $(LINK2 http://www.dlang.org, D programming language).
 *
 * Copyright:   Copyright (C) 1985-1998 by Symantec
 *              Copyright (C) 2000-2021 by The D Language Foundation, All Rights Reserved
 * Authors:     $(LINK2 http://www.digitalmars.com, Walter Bright)
 * License:     $(LINK2 http://www.boost.org/LICENSE_1_0.txt, Boost License 1.0)
 * Source:      $(LINK2 https://github.com/dlang/dmd/blob/master/src/dmd/backend/mem.d, backend/mem.d)
 * Coverage:    https://codecov.io/gh/dlang/dmd/src/master/src/dmd/backend/mem.d
 */


module dmd.backend.mem;


extern (C):

nothrow:
@nogc:
@safe:

@trusted
char *mem_strdup(const(char) *);

@trusted
void *mem_malloc(size_t);

@trusted
void *mem_calloc(size_t);

@trusted
void *mem_realloc(void *,size_t);

@trusted
void mem_free(void *);

@trusted
void mem_init();

@trusted
void mem_term();

extern (C++)
{
    void mem_free_cpp(void *);
    alias mem_freefp = mem_free_cpp;
}

enum MEM_E { MEM_ABORTMSG, MEM_ABORT, MEM_RETNULL, MEM_CALLFP, MEM_RETRY }
void mem_setexception(MEM_E,...);

version (MEM_DEBUG)
{
    alias mem_fstrdup = mem_strdup;
    alias mem_fcalloc = mem_calloc;
    alias mem_fmalloc = mem_malloc;
    alias mem_ffree   = mem_free;
}
else
{
    @trusted
    char *mem_fstrdup(const(char) *);

    @trusted
    void *mem_fcalloc(size_t);

    @trusted
    void *mem_fmalloc(size_t);

    @trusted
    void mem_ffree(void *) { }
}

