/**
 * Compiler implementation of the
 * $(LINK2 http://www.dlang.org, D programming language).
 *
 * Copyright:   Copyright (c) 1999-2017 by Digital Mars, All Rights Reserved
 * Authors:     $(LINK2 http://www.digitalmars.com, Walter Bright)
 * License:     Distributed under the Boost Software License, Version 1.0.
 *              http://www.boost.org/LICENSE_1_0.txt
 * Source:      https://github.com/dlang/dmd/blob/master/src/dmd/backend/tk.c
 */

#include <stdlib.h>

#if !SPP
    void *ph_malloc(size_t nbytes);
    void *ph_calloc(size_t nbytes);
    void ph_free(void *p);
    void *ph_realloc(void *p , size_t nbytes);

    #define malloc          ph_malloc
    #define calloc(x,y)     ph_calloc((x) * (y))
    #define realloc         ph_realloc
    #define free            ph_free
#endif

#if !MEM_DEBUG
    #define MEM_NOMEMCOUNT 1
#endif

#include        "mem.c"



