// Copyright (C) 1995-1998 by Symantec
// Copyright (C) 2000-2009 by Digital Mars
// All Rights Reserved
// http://www.digitalmars.com
/*
 * This source file is made available for personal use
 * only. The license is in /dmd/src/dmd/backendlicense.txt
 * or /dm/src/dmd/backendlicense.txt
 * For any other uses, please contact Digital Mars.
 */

#ifndef _H_dllrun
#define _H_dllrun

#include "netspawn.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef int (WINAPI* tToolEntryPoint)(int, char **, tToolCallbacks *);
typedef int (WINAPI* tToolVersionPoint)();
typedef char *(NETSPAWNAPI *tNetSpawnTranslateCallback)(char *, char *);
typedef void (NETSPAWNAPI *tNetSpawnDisposeCallback)(char *);

typedef struct tToolInfo
{
    char*          makeName;    // name of tool in makefile
    char*          dllName;     // name of dll
    int        activity;    // activity value for this tool
    char*          title;       // title string
    char*          entryName;   // entry point name of tool
        char*          versionName;     // version entry point name
        HINSTANCE          hInstance;
        tToolEntryPoint entry;
        tToolVersionPoint version;
} tToolInfo;

typedef enum
{
   DllSuccess = 0,
   DllLoadError,
   DllVersionError,
   DllJobCancelled,
   DllJobFailed,
   DllEntryError
} DllErrorCode;

#define COMPILING   0
#define LINKING     1
#define DISASSEMBLING   2
#define PREPROCESSING   3
#define SHELLING        4
#define PACKING         5
#define MAKING          6
#define LIBBING         7

extern tToolInfo *FindDll(char *);
extern DllErrorCode RunDll(tToolInfo *, int, char **, tToolCallbacks *); // ret 0 if OK
extern int GetDllReturnCode();
extern void FreeAllDlls();
extern DllErrorCode LoadDll(tToolInfo *); // ret 0 if NOT ok
extern void RegisterTranslateCallback (tNetSpawnTranslateCallback callback);
extern tNetSpawnTranslateCallback RetrieveTranslateCallback ();
extern void RegisterDisposeCallback (tNetSpawnDisposeCallback callback);
extern tNetSpawnDisposeCallback RetrieveDisposeCallback ();
extern unsigned long GetCompilerFlags();
extern void SetCompilerFlags (unsigned long flags);
extern unsigned long GetShellFlags();
extern void SetShellFlags (unsigned long flags);
extern void SetDllMessageFilter (BOOL (*filter)(MSG *));
extern BOOL (*GetDllMessageFilter())(MSG *);

extern void SetRTLEnvironmentVariable (char **environ);
extern char **GetRTLEnvironmentVariable();

#ifdef __cplusplus
}
#endif

#endif
