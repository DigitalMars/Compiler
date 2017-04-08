/**
 * Implementation of the
 * $(LINK2 http://www.digitalmars.com/download/freecompiler.html, Digital Mars C/C++ Compiler).
 *
 * Copyright:   Copyright (c) 1995-1998 by Symantec, All Rights Reserved
 *              Copyright (c) 2000-2017 by Digital Mars, All Rights Reserved
 * License:     Distributed under the Boost Software License, Version 1.0.
 *              http://www.boost.org/LICENSE_1_0.txt
 * Source:      https://github.com/DigitalMars/Compiler/blob/master/dm/src/dmc/ntllshell/include/nsidde.h
 */

#ifndef _NSIDDE_H_
#define _NSIDDE_H_

#include "netspawn.h"
#include "network.h"

#ifdef __cplusplus
extern "C"
{
#endif

/*
typedef struct
{
   int version;
#define VERSION_TOOLCALLBACKS 2
   int  (CALLBACK _far *ReportProgress) (const NetCommAddress __far *server,
                                          int jobSequence,
                                          long);
   void (CALLBACK _far *ReportOutput)   (const NetCommAddress __far *server,
                                          int jobSequence,
                                          const char __far *output);
   void (CALLBACK _far *ReportMessage)  (const NetCommAddress __far *server,
                                          int jobSequence,
                                          const tToolMsg __far *message);
   void (CALLBACK _far *ReportTarget)   (const NetCommAddress __far *server,
                                          int jobSequence,
                                          const char __far *target);
   void (CALLBACK _far *ReportFile)      (const NetCommAddress __far *server,
                                          int jobSequence,
                                          const char __far *file,
                                          int level);
   void (CALLBACK _far *ReportActivity) (const NetCommAddress __far *server,
                                          int jobSequence,
                                          const tToolData __far *activity);
   tStackMode StackMode;
   tYieldMode YieldMode;
   tReportMode LineReportMode;
} tServerCallbacks;
*/

typedef enum
{
  NetSpawnInstallNoError = 0,
  NetSpawnInstallCantFindDll,
  NetSpawnInstallMissingEntryPoint
} NetSpawnInstallErrors;


typedef struct
{
    int  version; // fill with VERSION_TOOLCALLBACKS
    int  (CALLBACK _far *ReportProgress)(NetCommAddress *address, int ,long);
    void (CALLBACK _far *ReportOutput)(NetCommAddress *address, int , const char _far *);
    void (CALLBACK _far *ReportMessage)(NetCommAddress *address, int , const tToolMsg _far *);
    void (CALLBACK _far *ReportTarget)(NetCommAddress *address, int , const char _far *, tTargetType);
    void (CALLBACK _far *ReportFile)(NetCommAddress *address, int , const char _far *, int);
    void (CALLBACK _far *ReportActivity)(NetCommAddress *address, int , const tToolData _far *);
    tStackMode  StackMode;
    tYieldMode  YieldMode;
    tReportMode LineReportMode;
} tServerCallbacks;

NETSPAWNSTATUS NetSpawnInstall (char *clientID, char *controlDirectory,
                                tServerCallbacks *callbacks, tToolCallbacks *clientcallback);
void NetSpawnSetControlDirectory (char *controlDirectory);
NETSPAWNSTATUS NetSpawnUninstall();

void NetSpawnSetDistribute (BOOL dist); // cd be done in NetSpawnInstall

BOOL NetSpawnOpenServerList(char *control_directory);
BOOL NetSpawnGetFirstServer(char *name, NetCommAddress *node);
BOOL NetSpawnGetNextServer(char *name, NetCommAddress *node);
void NetSpawnCloseServerList();
void NetSpawnImprovedSleep(short time);
void NetSpawnSaveEnvironment(char *CurrentDirectory);
void NetSpawnDumpCompileContext();
void NetSpawnDumpCompileContextForBuildservers();
void NETSPAWNAPI NetSpawnSetNetworkPassword(char *password);
void NETSPAWNAPI NetSpawnSetReleaseDirectory(char *releaseDirectory);
void NETSPAWNAPI NetSpawnSetMessageFilter (BOOL (*messageFilterFn)(MSG *));
void NETSPAWNAPI NetSpawnSetDependencyTracking(BOOL depTrack);
void NETSPAWNAPI NetSpawnSetDontStopOnErrorFlag();
NetSpawnInstallErrors NetSpawnGetError();

#ifdef __cplusplus
}
#endif

#endif
