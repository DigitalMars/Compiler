// Copyright (C) 1996-1998 by Symantec
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
#include        <string.h>
#include        <stdlib.h>
#include        <time.h>
#include        "cc.h"
#include        "global.h"
#if TARGET_MAC
#include        "TGvers.h"
#endif

static char __file__[] = __FILE__;      /* for tassert.h                */
#include        "tassert.h"

#include        "msgs2.c"

void errmsgs_init();

///////////////////////////////////////////
// Return pointer to string number.

char *dlcmsgs(int n)
{
    char *p;

    assert((unsigned)n < arraysize(msgtbl));
    //errmsgs_init();
    p = msgtbl[n][configv.language];
    if (!p)
        p = msgtbl[n][LANGenglish];

    return p;
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
