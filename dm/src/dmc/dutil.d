/**
 * Implementation of the
 * $(LINK2 http://www.digitalmars.com/download/freecompiler.html, Digital Mars C/C++ Compiler).
 *
 * Copyright:   Copyright (c) 1984-1998 by Symantec, All Rights Reserved
 *              Copyright (c) 2000-2017 by Digital Mars, All Rights Reserved
 * Authors:     $(LINK2 http://www.digitalmars.com, Walter Bright)
 * License:     Distributed under the Boost Software License, Version 1.0.
 *              http://www.boost.org/LICENSE_1_0.txt
 * Source:      https://github.com/DigitalMars/Compiler/blob/master/dm/src/dmc/dutil.d
 */

// Utility subroutines

module dmsc;

import core.stdc.ctype;
import core.stdc.stdio;
import core.stdc.stdlib;
import core.stdc.string;
import core.stdc.time;

import ddmd.backend.cdef;
import ddmd.backend.cc;
import ddmd.backend.el;
import ddmd.backend.global;
import ddmd.backend.oper;
import ddmd.backend.ty;
import ddmd.backend.type;

import tk.dlist;
import tk.mem;

import dcpp;
import dtoken;
import msgs2;
import parser;
import precomp;

extern (C++):

enum TX86 = 1;

alias err_fputc = fputc;

static if (TX86)
{

/*******************************
 * Alternative assert failure.
 */

void util_assert(const(char)* file,int line)
{
    //if (!(configv.verbose == 2))
        //*strchr(file,'.') = 0;
version (_WINDLL)
{
    err_fatal(EM_internal_error,file,line);
}
else
{
    printf("Internal error: %s %d\n",file,line);
}

    debug line = *cast(int *)0;           // cause GP fault
    err_exit();
}

}

version (none)
{
}
/****************************
 * Clean up and exit program.
 */

void err_exit()
{
    util_exit(EXIT_FAILURE);
}

/****************************
 * Clean up and exit program.
 */

void util_exit(int exitcode)
{
    /* It's possible to recursively come through here if there is some
       failure that keeps calling err_exit() from the cleanup routines
       below.
       So, if we are nesting, then skip.
     */
    __gshared char again;

    //printf("util_exit(%d)\n",exitcode);
    if (!again)
    {   again++;

        if (fdep)
            fclose(fdep);
        if (flst)
            fclose(flst);
version (SPP)
{
        if (fout)
            fclose(fout);               // don't care if fclose fails
        file_remove(foutname);          // delete corrupt output file
        file_term();
}
else
{
        objfile_delete();
        file_remove(fsymname);          // delete corrupt output file
        file_term();
        ph_term();
}
version (Windows)
{
version (SPP)
{
}
else
{
        tdb_term();
}
        os_term();
}
    }
    exit(exitcode);                     /* terminate abnormally         */
}

/****************************
 * Send CRLF to stream.
 */

extern (C) void crlf(FILE *fstream)
{ err_fputc(LF,fstream);
}

/*************************************
 * Convert unsigned integer to a string and return
 * pointer to that string.
 */

char *unsstr(uint i)
{ __gshared char[uint.sizeof * 3 + 1] string;

  sprintf(string.ptr,"%u",i);
  return string.ptr;
}

/*************************
 * Hex digit?
 */

version (DigitalMars)
{
}
else
{
int ishex(int c)
{ return isdigit(c) || isxdigit(c); }
}

/**************************************
 * Ignore nulls, CRs, rubouts
 */

int isignore(int c)
{ return( !c || c == CR || c == '\177' );
}


/*********************
 * Illegal characters
 */

int isillegal(int c)
{
 return (!isignore(c) && !isprint(c) && !iseol(cast(char)c) && !isspace(c));
}

/**********************
 * If c is a power of 2, return that power else -1.
 */

int ispow2(targ_ullong c)
{       int i;

        if (c == 0 || (c & (c - 1)))
            i = -1;
        else
            for (i = 0; c >>= 1; i++)
            { }
        return i;
}


/******************************************************/

/*
        A side effect of our precompiled header system is that
        any single alloc cannot be bigger than a PH buffer. This
        inhibits the creation of large arrays. Since most of those
        arrays never go into a PH, we can allocate them outside
        the PH system, which is what we do here.
 */

/***************************
 */

void *util_malloc(uint n,uint size)
{
static if (0)
{
    void *p;

    p = mem_malloc(n * size);
    //dbg_printf("util_calloc(%d) = %p\n",n * size,p);
    return p;
}
else
{
    void *p;
    ulong nbytes;

    nbytes = cast(ulong) n * cast(ulong) size;
    p = malloc(cast(size_t)nbytes);
    if (!p && cast(size_t)nbytes)
        err_nomem();
    return p;
}
}

/***************************
 */

void *util_calloc(uint n,uint size)
{
static if (0)
{
    void *p;

    p = mem_calloc(n * size);
    //dbg_printf("util_calloc(%d) = %p\n",n * size,p);
    return p;
}
else
{
    void *p;
    ulong nbytes;

    nbytes = cast(ulong) n * cast(ulong) size;
    p = calloc(n,size);
    if (!p && cast(size_t)nbytes)
        err_nomem();
    return p;
}
}

/***************************
 */

void util_free(void *p)
{
    //dbg_printf("util_free(%p)\n",p);
static if (0)
{
    mem_free(p);
}
else
{
    free(p);
}
}

/***************************
 */

void *util_realloc(void *oldp,uint n,uint size)
{
static if (0)
{
    //dbg_printf("util_realloc(%p,%d)\n",oldp,n * size);
    return mem_realloc(oldp,n * size);
}
else
{
    void *p;
    ulong nbytes;

    nbytes = cast(ulong) n * cast(ulong) size;
    p = realloc(oldp,cast(size_t)nbytes);
    if (!p && cast(size_t)nbytes)
        err_nomem();
    return p;
}
}

/***********************************
 * Storage allocation package to malloc memory outside of PH.
 */

void *parc_malloc(size_t len)
{   void *p;

    p = malloc(len);
    if (!p)
        err_nomem();
    debug assert((cast(uint)p & 3) == 0);
    return p;
}

void *parc_calloc(size_t len)
{   void *p;

    p = calloc(len,1);
    if (!p)
        err_nomem();
    debug assert((cast(uint)p & 3) == 0);
    return p;
}

void *parc_realloc(void *oldp,size_t len)
{   void *p;

    debug assert((cast(uint)oldp & 3) == 0);
    p = realloc(oldp,len);
    if (!p && len)
        err_nomem();
    debug assert((cast(uint)p & 3) == 0);
    return p;
}

char *parc_strdup(const(char)* s)
{   char *p;

    p = strdup(s);
    if (!p && s)
        err_nomem();
    debug assert((cast(uint)p & 3) == 0);
    return p;
}

void parc_free(void *p)
{
    debug assert((cast(uint)p & 3) == 0);
    free(p);
}

version (Posix)
{
char *strupr(char *buf)
{
    int i;
    int len = strlen(buf);
    for(i=0; i<len; i++)
        buf[i] = toupper(buf[i]);
    return buf;
}
}

