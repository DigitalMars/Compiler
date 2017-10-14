/**
 * Implementation of the
 * $(LINK2 http://www.digitalmars.com/download/freecompiler.html, Digital Mars C/C++ Compiler).
 *
 * Copyright:   Copyright (c) 1992-1998 by Symantec, All Rights Reserved
 *              Copyright (c) 1999-2017 by Digital Mars, All Rights Reserved
 * Authors:     $(LINK2 http://www.digitalmars.com, Walter Bright)
 * License:     Distributed under the Boost Software License, Version 1.0.
 *              http://www.boost.org/LICENSE_1_0.txt
 * Source:      https://github.com/DigitalMars/Compiler/blob/master/dm/src/dmc/page.di
 */

module page;

// D version of the Digital Mars #include file page.h

extern (C):

enum __PGOFF = ushort.sizeof;

struct Pageheader
{
  align (1):
    ushort pagesize;
    ushort maxsize;
    ushort allocp;
    ushort bassize;
    ushort baslnk;
}

enum PAGEOVERHEAD = Pageheader.sizeof;

uint page_calloc(void *baseptr,uint size);
uint page_malloc(void *baseptr,uint size);
uint page_realloc(void *baseptr,uint p, uint nbytes);
int page_free(void *baseptr,uint p);
uint page_maxfree(void *baseptr);
uint page_initialize(void *baseptr,uint pagesize);

ushort page_size(void *baseptr, uint p)
{
    return cast(ushort)(*cast(ushort *)(cast(char *)baseptr + p - __PGOFF) - __PGOFF);
}

void *page_toptr(void *baseptr, uint p)
{
    return cast(void *)(cast(char *)baseptr + p);
}
