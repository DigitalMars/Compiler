/**
 * Implementation of the
 * $(LINK2 http://www.digitalmars.com/download/freecompiler.html, Digital Mars C/C++ Compiler).
 *
 * Copyright:   Copyright (c) 1985-1998 by Symantec, All Rights Reserved
 *              Copyright (c) 2000-2017 by Digital Mars, All Rights Reserved
 * Authors:     $(LINK2 http://www.digitalmars.com, Walter Bright)
 * License:     Distributed under the Boost Software License, Version 1.0.
 *              http://www.boost.org/LICENSE_1_0.txt
 * Source:      https://github.com/DigitalMars/Compiler/blob/master/dm/src/dmc/objrecor.c
 */

// All .OBJ file writing is done here.

#include        <stdio.h>
#include        <string.h>
#include        <stdlib.h>
#include        <ctype.h>
#include        "cc.h"
#include        "token.h"
#include        "global.h"
#include        "parser.h"

#if (SCPP || MARS) && !HTOD

static char __file__[] = __FILE__;      /* for tassert.h                */
#include        "tassert.h"

static char *fobjname;                  // output file name

/*******************************
 */

STATIC void objfile_error()
{
    err_fatal(EM_write_error,fobjname);         // error writing output file
}

/***************************************
 * Open .OBJ file.
 */

void objfile_open(const char *name)
{
    fobjname = (char *)name;
}

/************************************
 * Close .OBJ file.
 */

void objfile_close(void *data, unsigned len)
{
    if (file_write(fobjname, data, len))
        objfile_error();
}

/************************************
 * Close and delete .OBJ file.
 */

void objfile_delete()
{
    file_remove(fobjname);      // delete corrupt output file
}

/**********************************
 * Terminate.
 */

void objfile_term()
{
#if TERMCODE
    mem_free(fobjname);
    fobjname = NULL;
#endif
}

#endif
