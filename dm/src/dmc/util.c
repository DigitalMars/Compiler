/**
 * Implementation of the
 * $(LINK2 http://www.digitalmars.com/download/freecompiler.html, Digital Mars C/C++ Compiler).
 *
 * Copyright:   Copyright (c) 1984-1998 by Symantec, All Rights Reserved
 *              Copyright (c) 2000-2017 by Digital Mars, All Rights Reserved
 * Authors:     $(LINK2 http://www.digitalmars.com, Walter Bright)
 * License:     Distributed under the Boost Software License, Version 1.0.
 *              http://www.boost.org/LICENSE_1_0.txt
 * Source:      https://github.com/DigitalMars/Compiler/blob/master/dm/src/dmc/util.c
 */

// Utility subroutines

#include        <stdio.h>
#include        <ctype.h>
#include        <string.h>
#include        <stdlib.h>
#include        <time.h>

#include        "cc.h"
#include        "global.h"
#include        "mem.h"
#include        "token.h"
#include        "parser.h"

#if _WINDOWS
#include        <dos.h>
#endif

#if __DMC__
#include        <controlc.h>
#endif

static char __file__[] = __FILE__;      /* for tassert.h                */
#include        "tassert.h"

#if TX86

/*******************************
 * Alternative assert failure.
 */

void util_assert(const char *file,int line)
{
    //if (!(configv.verbose == 2))
        //*strchr(file,'.') = 0;
#if USEDLLSHELL
    err_fatal(EM_internal_error,file,line);
#else
    printf("Internal error: %s %d\n",file,line);
#endif

#if defined(DEBUG) && !__GNUC__
    __asm HLT
    line = *(int *)0;           // cause GP fault
#endif
    err_exit();
}

#endif /* TX86 */

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
    static char again;

    //printf("util_exit(%d)\n",exitcode);
    if (!again)
    {   again++;

        if (fdep)
            fclose(fdep);
        if (flst)
            fclose(flst);
#if SPP
        if (fout)
            fclose(fout);               // don't care if fclose fails
        file_remove(foutname);          // delete corrupt output file
        file_term();
#else
        objfile_delete();
        file_remove(fsymname);          // delete corrupt output file
        file_term();
        ph_term();
#endif
#if _WIN32
#if !SPP
        tdb_term();
#endif
        os_term();
#endif
    }
    exit(exitcode);                     /* terminate abnormally         */
}

/****************************
 * Send CRLF to stream.
 */

void crlf(FILE *fstream)
{ err_fputc(LF,fstream);
}

/*************************************
 * Convert unsigned integer to a string and return
 * pointer to that string.
 */

char *unsstr(unsigned i)
{ static char string[sizeof(unsigned) * 3 + 1];

  sprintf(string,"%u",i);
  return string;
}

/*************************
 * Hex digit?
 */

#ifndef __DMC__
int ishex(int c)
{ return isdigit(c) || isxdigit(c); }
#endif

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
 return (!isignore(c) && !isprint(c) && !iseol(c) && !isspace(c));
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
                ;
        return i;
}

#if !(linux || __APPLE__ || __FreeBSD__ || __OpenBSD__)
#if TX86

#if _WIN32

/********************************
 * Control C interrupts go here.
 */

static void __cdecl controlc_handler(void)
{
    //printf("saw controlc\n");
    controlc_saw = 1;
}

/*********************************
 * Trap control C interrupts.
 */

void _STI_controlc()
{
    //printf("_STI_controlc()\n");
    _controlc_handler = controlc_handler;
    controlc_open();                    /* trap control C               */
}

void _STD_controlc()
{
    //printf("_STD_controlc()\n");
    controlc_close();
}


#endif

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

void *util_malloc(unsigned n,unsigned size)
{
#if 0 && MEM_DEBUG
    void *p;

    p = mem_malloc(n * size);
    //dbg_printf("util_calloc(%d) = %p\n",n * size,p);
    return p;
#else
    void *p;
    unsigned long nbytes;

    nbytes = (unsigned long) n * (unsigned long) size;
    p = malloc(nbytes);
    if (!p && (size_t)nbytes)
        err_nomem();
    return p;
#endif
}

/***************************
 */

void *util_calloc(unsigned n,unsigned size)
{
#if 0 && MEM_DEBUG
    void *p;

    p = mem_calloc(n * size);
    //dbg_printf("util_calloc(%d) = %p\n",n * size,p);
    return p;
#else
    void *p;
    unsigned long nbytes;

    nbytes = (unsigned long) n * (unsigned long) size;
    p = calloc(n,size);
    if (!p && (size_t)nbytes)
        err_nomem();
    return p;
#endif
}

/***************************
 */

void util_free(void *p)
{
    //dbg_printf("util_free(%p)\n",p);
#if 0 && MEM_DEBUG
    mem_free(p);
#else
    free(p);
#endif
}

/***************************
 */

void *util_realloc(void *oldp,unsigned n,unsigned size)
{
#if 0 && MEM_DEBUG
    //dbg_printf("util_realloc(%p,%d)\n",oldp,n * size);
    return mem_realloc(oldp,n * size);
#else
    void *p;
    unsigned long nbytes;

    nbytes = (unsigned long) n * (unsigned long) size;
    p = realloc(oldp,nbytes);
    if (!p && (size_t)nbytes)
        err_nomem();
    return p;
#endif
}
#else
#include "TGutil.c"
#endif

/***********************************
 * Storage allocation package to malloc memory outside of PH.
 */

void *parc_malloc(size_t len)
{   void *p;

    p = malloc(len);
    if (!p)
        err_nomem();
#ifdef DEBUG
    assert(((unsigned)p & 3) == 0);
#endif
    return p;
}

void *parc_calloc(size_t len)
{   void *p;

    p = calloc(len,1);
    if (!p)
        err_nomem();
#ifdef DEBUG
    assert(((unsigned)p & 3) == 0);
#endif
    return p;
}

void *parc_realloc(void *oldp,size_t len)
{   void *p;

#ifdef DEBUG
    assert(((unsigned)oldp & 3) == 0);
#endif
    p = realloc(oldp,len);
    if (!p && len)
        err_nomem();
#ifdef DEBUG
    assert(((unsigned)p & 3) == 0);
#endif
    return p;
}

char *parc_strdup(const char *s)
{   char *p;

    p = strdup(s);
    if (!p && s)
        err_nomem();
#ifdef DEBUG
    assert(((unsigned)p & 3) == 0);
#endif
    return p;
}

void parc_free(void *p)
{
#ifdef DEBUG
    assert(((unsigned)p & 3) == 0);
#endif
    free(p);
}
#endif

#if M_UNIX
char *strupr(char *buf)
{
    int i;
    int len = strlen(buf);
    for(i=0; i<len; i++)
        buf[i] = toupper(buf[i]);
    return buf;
}
#endif
