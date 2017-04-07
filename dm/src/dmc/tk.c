/**
 * Compiler implementation of the
 * $(LINK2 http://www.dlang.org, D programming language).
 *
 * Copyright:   Copyright (c) 1999-2017 by Digital Mars, All Rights Reserved
 * Authors:     $(LINK2 http://www.digitalmars.com, Walter Bright)
 * License:     Distributed under the Boost Software License, Version 1.0.
 *              http://www.boost.org/LICENSE_1_0.txt
 * Source:      https://github.com/dlang/dmd/blob/master/src/ddmd/backend/tk.c
 */

#include        <stdio.h>
#include        <stdlib.h>
#include        <string.h>
#include        "cc.h"
#include        "token.h"
#include        "oper.h"
#include        "global.h"
#include        "parser.h"

static char __file__[] = __FILE__;      /* for tassert.h                */
#include        "tassert.h"

#include        "filespec.c"

#if !SPP
#include        "vec.c"

#define malloc          ph_malloc
#define calloc(x,y)     ph_calloc((x) * (y))
#define realloc         ph_realloc
#define free            ph_free
#endif

#if !MEM_DEBUG
#define MEM_NOMEMCOUNT 1
#endif
#include        "mem.c"

/*
#define list_new()              ((list_t) ph_malloc(sizeof(struct LIST)))
#define list_delete(list)       ph_free(list)
#define mem_setnewfileline(a,b,c) 0
*/

//#define list_freelist cstate.CSlist_freelist
#include        "list.c"


