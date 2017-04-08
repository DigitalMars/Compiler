/**
 * Implementation of the
 * $(LINK2 http://www.digitalmars.com/download/freecompiler.html, Digital Mars C/C++ Compiler).
 *
 * Copyright:   Copyright (c) 1994-1998 by Symantec, All Rights Reserved
 *              Copyright (c) 2000-2017 by Digital Mars, All Rights Reserved
 * Authors:     $(LINK2 http://www.digitalmars.com, Walter Bright)
 * License:     Distributed under the Boost Software License, Version 1.0.
 *              http://www.boost.org/LICENSE_1_0.txt
 * Source:      https://github.com/DigitalMars/Compiler/blob/master/dm/src/dmc/tdbx.c
 */

#if !SPP

#include        <stdio.h>
#include        <string.h>
#include        <malloc.h>
#include        <time.h>
#include        "cc.h"
#include        "global.h"
#include        "cgcv.h"
#include        "tdb.h"

static char __file__[] = __FILE__;      /* for tassert.h                */
#include        "tassert.h"

static TDBhandle_t h;

/*************************************
 * Load TDB dll and set function pointers.
 */

STATIC void tdb_loaddll()
{
#if _WIN32
#if !EXPLICIT
    os_loadlibrary("tdb.dll");

#if 1
    GetTDBEngineVersion = (GetTDBEngineVersion_t) os_getprocaddress((char *)GetTDBEngineVersion_o);
    OpenDatabase = (OpenDatabase_t) os_getprocaddress((char *)OpenDatabase_o);
    GetTDBTimeStamp = (GetTDBTimeStamp_t) os_getprocaddress((char *)GetTDBTimeStamp_o);
    SubmitTypes = (SubmitTypes_t) os_getprocaddress((char *)SubmitTypes_o);
    ReceiveTypes = (ReceiveTypes_t) os_getprocaddress((char *)ReceiveTypes_o);
    CloseDatabase = (CloseDatabase_t) os_getprocaddress((char *)CloseDatabase_o);
    GetTypeIndex = (GetTypeIndex_t) os_getprocaddress((char *)GetTypeIndex_o);
#else
    GetTDBEngineVersion = (GetTDBEngineVersion_t) os_getprocaddress("_GetTDBEngineVersion@0");
    OpenDatabase = (OpenDatabase_t) os_getprocaddress("_OpenDatabase@12");
    GetTDBTimeStamp = (GetTDBTimeStamp_t) os_getprocaddress("_GetTDBTimeStamp@8");
    SubmitTypes = (SubmitTypes_t) os_getprocaddress("_SubmitTypes@28");
    ReceiveTypes = (ReceiveTypes_t) os_getprocaddress("_ReceiveTypes@8");
    CloseDatabase = (CloseDatabase_t) os_getprocaddress("_CloseDatabase@8");
    GetTypeIndex = (GetTypeIndex_t) os_getprocaddress("_GetTypeIndex@12");
#endif

#endif
#endif
}

void tdb_error(unsigned line)
{
#if MARS
        printf("Fatal error with file '%s'\n", ftdbname);
        err_exit();
#else
        err_fatal(EM_tdb,ftdbname, line);               // error
#endif
}

/*************************************
 */

void tdb_open()
{
#if _WIN32
    int createflag;

    tdb_loaddll();
    //createflag = 0;                   // open existing
    //if (config.flags2 & CFG2phgen)    // if generate pch
        createflag = 1;                 // create new tdb
    if (OpenDatabase(&h,ftdbname,createflag))
        tdb_error(__LINE__);            // can't open type database
#endif
}

/*************************************************
 * Terminate tdb.
 */

void tdb_term()
{
#if _WIN32
    if (h && CloseDatabase(h,0))
    {
        tdb_error(__LINE__);            // error
    }
#endif
}

/********************************************
 * Get time stamp of tdb file.
 */

unsigned long tdb_gettimestamp()
{
#if _WIN32
    int stamp;

    if (!h)
        tdb_open();

    if (GetTDBTimeStamp(h,&stamp))
        tdb_error(__LINE__);            // error

    return stamp;
#endif
}

/********************************************
 * Write type database buffer to TDB DLL.
 */

void tdb_write(void *buf,unsigned size,unsigned numindices)
{
#if _WIN32
    void *indexhandle;

    if (!h)
        tdb_open();

    if (SubmitTypes(h,buf,size,numindices,&indexhandle,malloc,free))
    {   CloseDatabase(h,0);
        tdb_error(__LINE__);            // error
    }

    tdb_term();
#endif
}

/***********************************************
 * Get type index for type.
 */

#if 1
unsigned long tdb_typidx(void *buf)
{
#if _WIN32
    int Index;

    assert(h);
    //printf("tdb_typidx(%p)\n",buf);
    if (GetTypeIndex(h,buf,&Index))
    {
        //printf("1\n");
        CloseDatabase(h,0);
        tdb_error(__LINE__);            // error
    }
    //printf("2\n");

    return Index;
#endif
}
#else
unsigned long tdb_typidx(unsigned char *buf,unsigned length)
{
    void *indexhandle;
    int *pGlobalIndices;

    //printf("tdb_typidx(%p,%d)\n",buf,length);
    assert(h);
    if (SubmitTypes(h,buf,length,1,&indexhandle,malloc,free))
    {   CloseDatabase(h,0);
        tdb_error(__LINE__);            // error
    }

    if (ReceiveTypes(indexhandle,&pGlobalIndices))
    {   CloseDatabase(h,0);
        tdb_error(__LINE__);            // error
    }

    return pGlobalIndices[0];
}
#endif

#endif
