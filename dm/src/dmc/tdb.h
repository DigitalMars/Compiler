/**
 * Implementation of the
 * $(LINK2 http://www.digitalmars.com/download/freecompiler.html, Digital Mars C/C++ Compiler).
 *
 * Copyright:   Copyright (c) 1989-1998 by Symantec, All Rights Reserved
 *              Copyright (c) 1999-2017 by Digital Mars, All Rights Reserved
 * Authors:     $(LINK2 http://www.digitalmars.com, Walter Bright)
 * License:     Distributed under the Boost Software License, Version 1.0.
 *              http://www.boost.org/LICENSE_1_0.txt
 * Source:      https://github.com/DigitalMars/Compiler/blob/master/dm/src/dmc/tdb.h
 */

//#pragma once

extern "C"
{

#define TDBVERSION      0x0000

#define TDBFUNC __stdcall

typedef void *TDBhandle_t;

#if EXPLICIT

int TDBFUNC GetTDBEngineVersion(void);
int TDBFUNC OpenDatabase(TDBhandle_t *ph, const char *Asciz, int CreateFlag);
int TDBFUNC GetTDBTimeStamp(TDBhandle_t h, int *TimeStamp);
int TDBFUNC SubmitTypes(TDBhandle_t h, void *pTypes, unsigned SizeTypes,
        unsigned Count, void **IndexHandle,
        void * __cdecl (*pmalloc)(unsigned),
        void __cdecl (*pfree)(void *));
int TDBFUNC ReceiveTypes(void *IndexHandle, int **pGlobalIndexes);
int TDBFUNC CloseDatabase(TDBhandle_t h, int DeleteFlag);
int TDBFUNC GetTypeIndex(TDBHandle_t h, void *pType, int *Index);

#else

// Do it with function pointers, so that the DLL is only loaded if
// and when it is required.

typedef int TDBFUNC (*GetTDBEngineVersion_t)(void);
typedef int TDBFUNC (*OpenDatabase_t)(TDBhandle_t *ph, const char *Asciz, int CreateFlag);
typedef int TDBFUNC (*GetTDBTimeStamp_t)(TDBhandle_t h, int *TimeStamp);
typedef int TDBFUNC (*SubmitTypes_t)(TDBhandle_t h, void *pTypes, unsigned SizeTypes,
        unsigned Count, void **IndexHandle,
#if __GNUC__
        void * (*pmalloc)(unsigned),
        void (*pfree)(void *));
#else
        void * __cdecl (*pmalloc)(unsigned),
        void __cdecl (*pfree)(void *));
#endif
typedef int TDBFUNC (*ReceiveTypes_t)(void *IndexHandle, int **pGlobalIndexes);
typedef int TDBFUNC (*CloseDatabase_t)(TDBhandle_t h, int DeleteFlag);
typedef int TDBFUNC (*GetTypeIndex_t)(TDBhandle_t h, void *pType, int *Index);

GetTDBEngineVersion_t GetTDBEngineVersion;
OpenDatabase_t OpenDatabase;
GetTDBTimeStamp_t GetTDBTimeStamp;
SubmitTypes_t SubmitTypes;
ReceiveTypes_t ReceiveTypes;
CloseDatabase_t CloseDatabase;
GetTypeIndex_t GetTypeIndex;

// Ordinal numbers
enum TDB_Ordinal
{
        OpenDatabase_o = 1,
        CloseDatabase_o = 2,
        GetTDBEngineVersion_o = 3,
        GetTDBTimeStamp_o = 4,
        SubmitTypes_o = 5,
        ReceiveTypes_o = 6,
        GetTypeIndex_o = 9
};

#endif

}
