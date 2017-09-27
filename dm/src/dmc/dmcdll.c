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
extern /*static*/ list_t file_list;
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


