/**
 * Implementation of the
 * $(LINK2 http://www.digitalmars.com/download/freecompiler.html, Digital Mars C/C++ Compiler).
 *
 * Copyright:   Copyright (c) 1985-1998 by Symantec, All Rights Reserved
 *              Copyright (c) 2000-2017 by Digital Mars, All Rights Reserved
 * Authors:     $(LINK2 http://www.digitalmars.com, Walter Bright)
 * License:     Distributed under the Boost Software License, Version 1.0.
 *              http://www.boost.org/LICENSE_1_0.txt
 * Source:      https://github.com/DigitalMars/Compiler/blob/master/dm/src/dmc/dmcdll.c
 */

#include        <stdio.h>
#include        <time.h>
#include        <ctype.h>
#include        <string.h>
#include        <stdlib.h>

#include        "cc.h"
#include        "parser.h"
#include        "type.h"
#include        "filespec.h"
#include        "global.h"
#include        "token.h"
#include        "scdll.h"

static char __file__[] = __FILE__;      /* for tassert.h                */
#include        "tassert.h"

unsigned long netspawn_flags = 0;

#if _WIN32 && _WINDLL
static list_t file_list;
#endif

/*********************************
 */
void dmcdll_command_line(int argc,char **argv, const char *copyright)
{
#if USEDLLSHELL
    {   static tToolData tooldata;

        // Set these at runtime to avoid DGROUP segment fixups
        // (DGROUP fixups screw us up because DS is switched for DLLs)
        tooldata.Activity = ACTIVITY;
        tooldata.Title = "Digital Mars " COMPILER " Version " VERSION;
        tooldata.Copyright = (char *)copyright;
        tooldata.ToolVersion = VERSIONINT;

        NetSpawnCmdline1(argc,argv);    // report command line
        NetSpawnActivity(&tooldata);    // report activity
    }
#endif
}


/*******************************************
 * Returns:
 *      true if this is the first compile
 */

bool dmcdll_first_compile()
{
#if _WIN32 && _WINDLL
    netspawn_flags = NetSpawnGetCompilerFlags();
    return (netspawn_flags & NETSPAWN_FIRST_COMPILE) != 0;
#else
    return true;
#endif
}


/******************************************
 */

void dmcdll_file_term()
{
#if _WIN32 && _WINDLL
    for (list_t fl = file_list; fl; fl = list_next(fl))
        NetSpawnDisposeFile((char *)list_ptr(fl));
    list_free(&file_list,FPNULL);
#endif
}

/***********************************
 * Net translate filename.
 */

char *dmcdll_nettranslate(const char *filename,const char *mode)
{
#if _WIN32 && _WINDLL
    char *newname;
    static int nest;

    nest++;
    newname = NetSpawnTranslateFileName((char *)filename,(char *)mode);
    if (!newname)
    {   if (nest == 1)
            err_exit();                 // abort without message
    }
    else
        list_append(&file_list,newname);
    nest--;
    return newname;
#else
    return (char *)filename;
#endif
}


char *dmcdll_TranslateFileName(char *filename, char *mode)
{
    return NetSpawnTranslateFileName(filename, mode);
}

void dmcdll_DisposeFile(char *filename)
{
    NetSpawnDisposeFile(filename);
}


void dmcdll_SpawnFile(const char *filename, int includelevel)
{
    NetSpawnFile(filename, includelevel);
}

void dmcdll_SpawnFile(const char *filename)
{
    NetSpawnFile(filename, kCloseLevel);
}

/*****************************************
 * Indicate progress.
 * Params:
 *      linnum = increasing value indicating progress, -1 means no indication
 * Returns:
 *      TRUE means exit program
 */
bool dmcdll_Progress(int linnum)
{
#if _WIN32 && _WINDLL
    return NetSpawnProgress(linnum == -1 ? kNoLineNumber : linnum) != NetSpawnOK;
#else
    return FALSE;
#endif
}

/**********************************
 * Printf for DLLs
 */

void dll_printf(const char *format,...)
{
#if _WIN32 && _WINDLL
    char buffer[500];
    int count = _vsnprintf(buffer,sizeof(buffer),format,(va_list)(&format + 1));

    tToolMsg tm;
    memset(&tm,0,sizeof(tm));
    tm.version = TOOLMSG_VERSION;
    tm.colNumber = kNoColNumber;
    tm.fileName = NULL;
    tm.lineNumber = kNoLineNumber;
    tm.msgType = eMsgInformational;
    tm.msgNumber = kNoMsgNumber;
    tm.msgText = buffer;

    NetSpawnMessage(&tm);
#endif
}

/************************************
 * Error in HTML source
 */

void dmcdll_html_err(const char *srcname, unsigned linnum, const char *format, va_list ap)
{
#if USEDLLSHELL
    char buffer[500];

    int count = _vsnprintf(buffer,sizeof(buffer),format,ap);

    tToolMsg tm;
    memset(&tm,0,sizeof(tm));
    tm.version = TOOLMSG_VERSION;
    tm.colNumber = kNoColNumber;
    tm.fileName = (char *)srcname;      // use original source file name
    tm.lineNumber = linnum;
    tm.msgText = buffer;
    tm.msgType = eMsgError;
    tm.msgNumber = kNoMsgNumber;

    NetSpawnMessage(&tm);
#else
    printf("%s(%d) : HTML error: ", srcname, linnum);
    vprintf(format,ap);
    fputc('\n', stdout);
    fflush(stdout);
#endif
}

/*********************************
 * Send error message to caller of DLL.
 */

#if USEDLLSHELL

void getLocation(char*& filename, int& line, int& column);

static void err_reportmsgf(tToolMsgType msgtype,int msgnum,const char *format,
                va_list args)
{
    char buffer[500];

    int count = _vsnprintf(buffer,sizeof(buffer),format,args);

    char* filename;
    int line;
    int column;
    getLocation(filename, line, column);

    tToolMsg tm;
    memset(&tm,0,sizeof(tm));
    tm.version = TOOLMSG_VERSION;
    tm.fileName = filename;
    tm.lineNumber = line;
    tm.colNumber = column;
    tm.msgText = buffer;
    tm.msgType = msgtype;
    tm.msgNumber = msgnum;

    NetSpawnMessage(&tm);
}

void err_reportmsgf_error(const char *format, va_list args)
{
    err_reportmsgf(eMsgError,kNoMsgNumber,format,args);
}

void err_reportmsgf_fatal(const char *format, va_list args)
{
    err_reportmsgf(eMsgFatalError,kNoMsgNumber,format,args);
}

void err_reportmsgf_continue(const char *format, va_list args)
{
    err_reportmsgf(eMsgContinue,kNoMsgNumber,format,args);
}

void err_reportmsgf_warning(bool warniserr, int warnum, const char *format, va_list args)
{
    err_reportmsgf(warniserr ? eMsgError : eMsgWarning,warnum,format,args);
}

#else

void err_reportmsgf_error(const char *format, va_list args)
{
}

void err_reportmsgf_fatal(const char *format, va_list args)
{
}

void err_reportmsgf_continue(const char *format, va_list args)
{
}

void err_reportmsgf_warning(bool warniserr, int warnum, const char *format, va_list args)
{
}

#endif


