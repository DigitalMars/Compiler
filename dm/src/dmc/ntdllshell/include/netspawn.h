/**
 * Implementation of the
 * $(LINK2 http://www.digitalmars.com/download/freecompiler.html, Digital Mars C/C++ Compiler).
 *
 * Copyright:   Copyright (c) 1992-1998 by Symantec, All Rights Reserved
 *              Copyright (c) 2000-2017 by Digital Mars, All Rights Reserved
 * License:     Distributed under the Boost Software License, Version 1.0.
 *              http://www.boost.org/LICENSE_1_0.txt
 * Source:      https://github.com/DigitalMars/Compiler/blob/master/dm/src/dmc/ntllshell/include/netspawn.h
 */

#ifndef __NETSPAWN_H
#define __NETSPAWN_H    1

#if __cplusplus
extern "C" {
#endif

/*---------------------------------------------------------------------------
-- Version numbers ----------------------------------------------------------
---------------------------------------------------------------------------*/

#define TOOLMSG_VERSION 2
#define VERSION_TOOLCALLBACKS   3
#define NETSPAWN_VERSION (TOOLMSG_VERSION + VERSION_TOOLCALLBACKS)

/*---------------------------------------------------------------------------
-- Model dependencies -------------------------------------------------------
---------------------------------------------------------------------------*/

#ifdef __NT__

#define CALLBACK    __stdcall
#define NETSPAWNAPI __stdcall

#else
#ifdef _WINDOWS
#ifdef __LARGE__

#define CALLBACK    __pascal
#define NETSPAWNAPI __pascal

#else
#error 16 bit NETSPAWN applications must be built in the LARGE memory model
#endif
#else
#error NETSPAWN applications must be built for 16 or 32 bit WINDOWS
#endif
#endif

/*---------------------------------------------------------------------------
-- Enums --------------------------------------------------------------------
---------------------------------------------------------------------------*/

typedef enum
{
   NetSpawnError,
   NetSpawnFail,
   NetSpawnOK,
   NetSpawnStarted
} NETSPAWNSTATUS;

typedef enum
{
    eMsgUndefined,          // uncategorized or unknown message type
    eMsgContinue = 0,       // message is a continuation
    eMsgWarning,            // message is a warning
    eMsgError,              // message is an error
    eMsgFatalError,         // message is a fatal error
    eMsgInformational,      // message is informational
    eMsgToolCmdLine = 16,   // message is echo of received command line
    eMsgLinkerExport = 48   // message is a DEF file export
} tToolMsgType;

typedef enum                // ignored in this version (no stack switching)
{
    stkNormal,
    stkDoSwitch,
    stkSwitched
} tStackMode;

typedef enum
{
    yieldOften,
    yieldModerate,
    yieldNever
} tYieldMode;

typedef enum
{
    reportAll,
    reportByFile
} tReportMode;

typedef enum
{
    targetTypeSmake,
    targetTypeJavaTarget
} tTargetType;




/*---------------------------------------------------------------------------
-- Structures ---------------------------------------------------------------
---------------------------------------------------------------------------*/

typedef struct
{
    char    *Activity;
    char    *Title;
    char    *Copyright;
    int      ToolVersion;
} tToolData;

typedef struct
{
    int             version;      // filled in by caller with TOOLMSG_VERSION
    char           *msgText;      // text of message or NULL if none
    tToolMsgType    msgType;      // type of message
    char           *fileName;     // file name of message or NULL if none
    long            lineNumber;   // line number of message or kNoLineNumber
    short           colNumber;    // column number of message or kNoColNumber
    short           msgNumber;    // error/warning number or kNoMsgNumber
    long            fileOffset;   // the offset of the charachter where the error occured
                                  // kNoFileoffset... The file offset has not been set...
} tToolMsg;

typedef struct
{
    int              version; // fill with VERSION_TOOLCALLBACKS
    int  (CALLBACK  *ReportProgress)(long);
    void (CALLBACK  *ReportOutput)(const char *);
    void (CALLBACK  *ReportMessage)(const tToolMsg *);
    void (CALLBACK  *ReportTarget)(const char *, tTargetType);
    void (CALLBACK  *ReportFile)(const char *, int);
    void (CALLBACK  *ReportActivity)(const tToolData *);
    tStackMode       StackMode;
    tYieldMode       YieldMode;
    tReportMode      LineReportMode;
} tToolCallbacks;

typedef void (__cdecl *tHookFP)(void);

/*---------------------------------------------------------------------------
-- Manifests ----------------------------------------------------------------
---------------------------------------------------------------------------*/

#define     kNoLineNumber   -1
#define     kNoColNumber    -1
#define     kNoMsgNumber    0
#define     kCloseLevel     -1
#define     kNoFileOffset   -1

/*---------------------------------------------------------------------------
-- Build Process Status Flags -----------------------------------------------
---------------------------------------------------------------------------*/

#define NETSPAWN_FIRST_COMPILE         0x00000001
#define NETSPAWN_DUMP_COMPILE_CONTEXT  0x00000002
#define NETSPAWN_FIRST_ASSEMBLY        0x00000004
#define NETSPAWN_BUILD_SERVER          0x00000008

/*---------------------------------------------------------------------------
-- Global variables ---------------------------------------------------------
---------------------------------------------------------------------------*/

extern tToolCallbacks __cdecl TaskCallbacks;

/*---------------------------------------------------------------------------
-- Prototypes ---------------------------------------------------------------
---------------------------------------------------------------------------*/

NETSPAWNSTATUS NETSPAWNAPI NetSpawn (char *cmd,
                                     char *args,
                                     tToolCallbacks *ToolCallbacks,
                                     unsigned short thresh);
void           NETSPAWNAPI NetSpawnActivity (const tToolData *);
void           NETSPAWNAPI NetSpawnCancelAllJobs ();
void           NETSPAWNAPI NetSpawnCmdline1 (int argc, char ** argv);
void           NETSPAWNAPI NetSpawnCmdline2 (char *cmdline);
void           NETSPAWNAPI NetSpawnDisposeFile (char *newfilename);
void           NETSPAWNAPI NetSpawnFile (const char *, int);
unsigned long  NETSPAWNAPI NetSpawnGetCompilerFlags();
unsigned long  NETSPAWNAPI NetSpawnGetShellFlags();
int            NETSPAWNAPI NetSpawnGetReturnCode();
void           NETSPAWNAPI NetSpawnHookDetach(tHookFP fp);
void           NETSPAWNAPI NetSpawnInitializeBuild();
NETSPAWNSTATUS NETSPAWNAPI NetSpawnMakeInitialize ();
NETSPAWNSTATUS NETSPAWNAPI NetSpawnMakeUninitialize ();
void           NETSPAWNAPI NetSpawnMessage (const tToolMsg *);
long           NETSPAWNAPI NetSpawnNumberOfServers();
void          *NETSPAWNAPI NetSpawnPersistentAlloc (int size);
NETSPAWNSTATUS NETSPAWNAPI NetSpawnProgress (long line);
void           NETSPAWNAPI NetSpawnReset();
void           NETSPAWNAPI NetSpawnSetStatusCallback (void (*callback)(char *));
void           NETSPAWNAPI NetSpawnTarget (const char *, tTargetType targetType);
char          *NETSPAWNAPI NetSpawnTranslateFileName (char *filename,
                                                      char *mode);
NETSPAWNSTATUS NETSPAWNAPI NetSpawnYield ();

/*---------------------------------------------------------------------------
-- Obsolete prototypes, etc. (do not use in new code) -----------------------
---------------------------------------------------------------------------*/

void _cdecl dllshell_activity (const tToolData *);
void _cdecl dllshell_cmdline1 (int argc, char ** argv);
void _cdecl dllshell_cmdline2 (char *);
void _cdecl dllshell_file (const char *, int);
void _cdecl dllshell_message (const tToolMsg *);
int  _cdecl dllshell_progress (long);
void _cdecl dllshell_target (const char *);
int  _cdecl dllshell_yield (void);
int  _cdecl DllSpawn(char *, char *, tToolCallbacks *);
#define DLLSHELL_YIELD_FLAG_BIT  0
#define EncodeLine(lLine,iYield) (lLine)
#define ToolAskYield(line)       (1)
#define ExtractLineNumber(line)  (line)

#ifdef __cplusplus
}
#endif

#endif

