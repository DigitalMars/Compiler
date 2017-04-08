/**
 * Implementation of the
 * $(LINK2 http://www.digitalmars.com/download/freecompiler.html, Digital Mars C/C++ Compiler).
 *
 * Copyright:   Copyright (c) 1996-1998 by Symantec, All Rights Reserved
 *              Copyright (c) 2000-2017 by Digital Mars, All Rights Reserved
 * Authors:     $(LINK2 http://www.digitalmars.com, Walter Bright)
 * License:     Distributed under the Boost Software License, Version 1.0.
 *              http://www.boost.org/LICENSE_1_0.txt
 * Source:      https://github.com/DigitalMars/Compiler/blob/master/dm/src/dmc/errmsgs2.c
 */

#include        <stdio.h>
#include        <string.h>
#include        <stdlib.h>
#include        <time.h>

#include        "cc.h"
#include        "global.h"

static char __file__[] = __FILE__;      /* for tassert.h                */
#include        "tassert.h"

#include        "msgs2.c"

void errmsgs_init();

///////////////////////////////////////////
// Return pointer to string number.

char *dlcmsgs(int n)
{
    assert((unsigned)n < arraysize(msgtbl));
    //errmsgs_init();
    const char *p = msgtbl[n][configv.language];
    if (!p)
        p = msgtbl[n][LANGenglish];

    return (char *)p;
}

/**********************************
 * Initialize error messages.
 */

void errmsgs_init()
{
#if 0
    static int inited;
    int i;

    if (inited)
        return;
    inited++;
    for (i = 0; i < arraysize(msgtbl); i++)
    {
        printf("%d: %s\n",i,msgtbl[i][LANGenglish]);
    }
#endif
}

/**********************************
 */

void errmsgs_term()
{
}
