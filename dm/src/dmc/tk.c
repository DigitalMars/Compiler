// Copyright (C) 1984-1998 by Symantec
// Copyright (C) 2000-2009 by Digital Mars
// All Rights Reserved
// http://www.digitalmars.com
// Written by Walter Bright
/*
 * This source file is made available for personal use
 * only. The license is in /dmd/src/dmd/backendlicense.txt
 * or /dm/src/dmd/backendlicense.txt
 * For any other uses, please contact Digital Mars.
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


