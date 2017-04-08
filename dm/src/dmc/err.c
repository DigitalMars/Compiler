/**
 * Implementation of the
 * $(LINK2 http://www.digitalmars.com/download/freecompiler.html, Digital Mars C/C++ Compiler).
 *
 * Copyright:   Copyright (c) 1983-1998 by Symantec, All Rights Reserved
 *              Copyright (c) 2000-2017 by Digital Mars, All Rights Reserved
 * Authors:     $(LINK2 http://www.digitalmars.com, Walter Bright)
 * License:     Distributed under the Boost Software License, Version 1.0.
 *              http://www.boost.org/LICENSE_1_0.txt
 * Source:      https://github.com/DigitalMars/Compiler/blob/master/dm/src/dmc/err.c
 */

/* Error handler                                        */


#include        <stdio.h>
#include        <stdlib.h>
#include        <string.h>
#include        <stdarg.h>
#include        "cc.h"
#include        "parser.h"
#include        "global.h"
#include        "token.h"
#include        "type.h"
#include        "scdll.h"
#include        "outbuf.h"
#include        "allocast.h"

static char __file__[] = __FILE__;      // for tassert.h
#include        "tassert.h"

int errcnt = 0;                 // error count

#ifdef DEBUG
#define ERROR_GPF       1       // 1 if cause GPF on error
#else
#define ERROR_GPF       0       // 1 if cause GPF on error
#endif

void prttype (type *);
void errmsgs_init();


/*********************************
 * Send error message to caller of DLL.
 */

#if USEDLLSHELL

static void err_reportmsgf(tToolMsgType msgtype,int msgnum,const char *format,
                va_list args);

static void err_reportmsg(tToolMsgType msgtype,int msgnum,int errnum,
                va_list args)
{
    err_reportmsgf(msgtype,msgnum,dlcmsgs(errnum),args);
}

static void err_reportmsgf(tToolMsgType msgtype,int msgnum,const char *format,
                va_list args)
{   tToolMsg tm;
    blklst *b;
    char buffer[500];
    int count;
    Srcpos sp;

    sp = token_linnum();
    memset(&tm,0,sizeof(tm));
    tm.version = TOOLMSG_VERSION;
    b = cstate.CSfilblk;
    if (b)
    {   int col;

        // -2 because scanner is always a bit ahead of the parser
        col = (b == bl ? btextp : b->BLtextp) - b->BLtext - 2;
        if (col < 0)
            col = 0;

        tm.colNumber = col;
        tm.fileName = srcpos_name(sp);  //blklst_filename(b);
        //tm.lineNumber = b->Bsrcpos.Slinnum;
    }
    else
    {
        tm.colNumber = kNoColNumber;
        if (sp.Slinnum)
            tm.fileName = srcpos_name(sp);
        else
            tm.fileName = finname;              // use original source file name
        //tm.lineNumber = getlinnum().Slinnum;  // get last known line number
    }
    tm.lineNumber = sp.Slinnum;
    count = _vsnprintf(buffer,sizeof(buffer),format,args);
    tm.msgText = buffer;
    tm.msgType = msgtype;
    tm.msgNumber = msgnum;
    NetSpawnMessage(&tm);
}

/**********************************
 * Printf for DLLs
 */

void dll_printf(const char *format,...)
{   tToolMsg tm;
    char buffer[500];
    int count;

    memset(&tm,0,sizeof(tm));
    tm.version = TOOLMSG_VERSION;
    tm.colNumber = kNoColNumber;
    tm.fileName = NULL;
    tm.lineNumber = kNoLineNumber;
    tm.msgType = eMsgInformational;
    tm.msgNumber = kNoMsgNumber;

    count = _vsnprintf(buffer,sizeof(buffer),format,(va_list)(&format + 1));
    tm.msgText = buffer;
    NetSpawnMessage(&tm);
}

#endif

/************************************
 * Do a continuation printf message.
 */

STATIC void err_continue(const char *format,...)
{
    va_list ap;
    va_start(ap, format);
    err_vprintf(ERRSTREAM,format,ap);
    crlf(ERRSTREAM);
    fflush(ERRSTREAM);
#if USEDLLSHELL
    err_reportmsgf(eMsgContinue,kNoMsgNumber,format,ap);
#endif
    va_end(ap);
}

/**********************************
 * Print error message
 */

void err_message(const char *format,...)
{
    va_list ap;
    va_start(ap, format);
    err_vprintf(ERRSTREAM,format,ap);
    crlf(ERRSTREAM);
    fflush(ERRSTREAM);
#if USEDLLSHELL
    err_reportmsgf(eMsgFatalError,kNoMsgNumber,format,ap);
#endif
    va_end(ap);
}

/*********************************
 * Print out error message to file stream fp.
 * Input:
 *      format          printf-style format string
 *      args            argument list pointer
 */

static void err_print(FILE *fp,const char *q,const char *format,va_list args)
{
#if TARGET_LINUX || TARGET_OSX || TARGET_FREEBSD || TARGET_OPENBSD || TARGET_SOLARIS
    if (format[0] == '#')
    {
        wrtpos(fp,FALSE);       // don't write line & draw ^ under it
        format++;
    }
    else
    {
        wrtpos(fp,TRUE);        // write line & draw ^ under it
    }
#else
    wrtpos(fp); // write line & draw ^ under it
#endif
    fputs(q,fp);
    vfprintf(fp,format,args);
    crlf(fp);
#if _WIN32
    fflush(fp);
#endif
}

/********************************************
 * There has been a generic error. Show where the error
 * occurred, putting a ^ under it. Then type the error
 * message.
 * Input:
 *      errnum =        error number as defined by this subroutine
 * Returns:
 *      1       error was printed
 *      0       error was not printed
 */

static int generr(const char *q,int errnum,va_list args)
{ char *msg;

  if (controlc_saw)                     /* poll for ^C                  */
        util_exit(EXIT_BREAK);

  if (errnum < 0)                       /* if warning                   */
        errnum = -errnum;
  else                                  /* else hard error              */
  {
#ifndef DEBUG
        static int lastline = -1;       // last line that got an error
        int curline;

        // Don't print out more than one error per line
        curline = token_linnum().Slinnum;
        if (curline)                    // if line number is valid
        {
            if (curline == lastline)
                return 0;
            lastline = curline;
        }
#endif
  }

    msg = dlcmsgs(errnum);
    err_print(ERRSTREAM,q,msg,args);
    errcnt++;                           /* keep track of # of errors    */
    if (flst)                           /* if we have a .LST file       */
    {
        err_print(flst,q,msg,args);
        crlf(flst);
    }
    if (errcnt >= configv.errmax)
        err_fatal(EM_2manyerrors);
    return 1;
}

void lexerr(unsigned errnum,...)
{
    va_list ap;
    va_start(ap, errnum);
#if USEDLLSHELL
    if (generr(dlcmsgs(EM_lexical_error),errnum,ap))
    {
        err_reportmsg(eMsgError,kNoMsgNumber,errnum,ap);
    }
#else
    generr(dlcmsgs(EM_lexical_error),errnum,ap);
#endif
    va_end(ap);
}

void preerr(unsigned errnum,...)
{
    va_list ap;
    va_start(ap, errnum);
#if USEDLLSHELL
    if (generr(dlcmsgs(EM_preprocessor_error),errnum,ap))
    {
        err_reportmsg(eMsgError,kNoMsgNumber,errnum,ap);
    }
#else
    generr(dlcmsgs(EM_preprocessor_error),errnum,ap);
#endif
    va_end(ap);
}

int synerr(unsigned errnum,...)
{
    va_list ap;
    va_start(ap, errnum);

    int result = generr(dlcmsgs(EM_error),errnum,ap);
#if USEDLLSHELL
    if (result)
        err_reportmsg(eMsgError,kNoMsgNumber,errnum,ap);
#endif
#if ERROR_GPF
    *(char *)0=0;
#endif
    va_end(ap);
    return result;
}

int cpperr(unsigned errnum,...)
{
    va_list ap;
    va_start(ap, errnum);

    int result = generr(dlcmsgs(EM_error),errnum,ap);
#if USEDLLSHELL
    if (result)
        err_reportmsg(eMsgError,kNoMsgNumber,errnum,ap);
#endif
#if ERROR_GPF
    *(char *)0=0;
#endif
    va_end(ap);
    return result;
}

#if TX86
int tx86err(unsigned errnum,...)
{
    va_list ap;
    va_start(ap, errnum);

    int result = generr(dlcmsgs(EM_error),errnum,ap);
#if USEDLLSHELL
    if (result)
        err_reportmsg(eMsgError,kNoMsgNumber,errnum,ap);
#endif
#if ERROR_GPF
    *(char *)0=0;
#endif
    va_end(ap);
    return result;
}
#endif

/* Translation table from warning number to message number              */
static short war_to_msg[] =
{       -1,
#if SPP
        -1,
        -1,
        EM_nestcomment,
        -1,
        -1,
        EM_valuenotused,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        EM_unknown_pragma,
        -1,
        EM_num_args,
        -1,
        -1,
        -1,
        EM_divby0,
#else
        EM_no_inline,
        EM_assignment,
        EM_nestcomment,
        EM_assignthis,
        EM_notagname,
        EM_valuenotused,
        EM_extra_semi,
        EM_large_auto,
        EM_obsolete_del,
        EM_obsolete_inc,
        EM_init2tmp,
        EM_used_b4_set,
        EM_bad_op,
        EM_386_op,
        EM_ret_auto,
        EM_ds_ne_dgroup,
        EM_unknown_pragma,
        EM_implied_ret,
        EM_num_args,
        /*0x2000+*/EM_before_pch,
        /*0x2000+*/EM_pch_first,
        EM_pch_config,
        EM_divby0,
        EM_badnumber,
        EM_ccast,
        EM_obsolete,
    #if TARGET_LINUX || TARGET_OSX || TARGET_FREEBSD || TARGET_OPENBSD || TARGET_SOLARIS
        EM_skip_attribute,
        EM_warning_message,
        EM_bad_vastart,
        EM_undefined_inline,
    #endif
#endif
};

/* Parallel table that controls the enabling of individual warnings     */
static unsigned char war_to_msg_enable[arraysize(war_to_msg)];

/***************************
 * Enable/disable specific warnings.
 * Input:
 *      warnum  warning number, if -1 that means all warnings
 *      on      0 = off, 1 = on
 */

void err_warning_enable(unsigned warnum,int on)
{
    if (warnum == -1)
        memset(war_to_msg_enable,on,arraysize(war_to_msg_enable));
    else
    {
        if (warnum < arraysize(war_to_msg))
            war_to_msg_enable[warnum] = on;
    }
}

void warerr(unsigned warnum,...)
{   char msg[20];

    va_list ap;
    va_start(ap, warnum);

    assert(warnum < arraysize(war_to_msg));
    if (!war_to_msg_enable[warnum])     /* if enabled                   */
    {   int errnum;

        if (!(config.flags2 & CFG2warniserr))
            errcnt--;                   /* to offset the increment      */
        sprintf(msg,dlcmsgs(EM_warning),warnum);

        // Convert from warning number to message number
        errnum = war_to_msg[warnum];

#if USEDLLSHELL
        {   int result;

            result = generr(msg,-errnum,ap);
            if (result)
                err_reportmsg(
                    (config.flags2 & CFG2warniserr) ? eMsgError : eMsgWarning,
                    warnum,errnum,ap);
        }
#else
        generr(msg,-errnum,ap);
#endif
    }
    va_end(ap);
}

/**************************
 * A non-recoverable error has occurred.
 */

void err_fatal(unsigned errnum,...)
{
    va_list ap;
    va_start(ap, errnum);
#if !_WINDLL
    fputs(dlcmsgs(EM_fatal_error), ERRSTREAM);          // Fatal error:
    err_vprintf(ERRSTREAM,dlcmsgs(errnum),ap);
    crlf(ERRSTREAM);
#endif
#if USEDLLSHELL
    err_reportmsg(eMsgFatalError,kNoMsgNumber,errnum,ap);
#endif
#if ERROR_GPF
    *(char *)0=0;
#endif
    va_end(ap);
    err_exit();
}

/*************************
 * This is the handler for out-of-memory errors.
 * Called by mem package.
 */

void err_nomem()
{
    //*(char*)0=0;
    err_fatal(EM_nomem);
}

/************************************
 * Error in the command line.
 */

void cmderr(unsigned errnum,...)
{
    va_list ap;
    va_start(ap, errnum);
#if _WINDLL
    dll_printf(dlcmsgs(EM_command_line_error));
#else
    fputs(dlcmsgs(EM_command_line_error), ERRSTREAM);
#endif
    err_vprintf(ERRSTREAM,dlcmsgs(errnum),ap);
#if USEDLLSHELL
    err_reportmsg(eMsgFatalError,kNoMsgNumber,errnum,ap);
#endif
    va_end(ap);
    err_exit();
}

/***************************************
 * Error in HTML source
 */

void html_err(const char *srcname, unsigned linnum, unsigned errnum, ...)
{
    va_list ap;
    va_start(ap, errnum);
#if USEDLLSHELL
    tToolMsg tm;
    char buffer[500];
    int count;

    memset(&tm,0,sizeof(tm));
    tm.version = TOOLMSG_VERSION;
    tm.colNumber = kNoColNumber;
    tm.fileName = (char *)srcname;      // use original source file name
    tm.lineNumber = linnum;
    count = _vsnprintf(buffer,sizeof(buffer),dlcmsgs(errnum),ap);
    tm.msgText = buffer;
    tm.msgType = eMsgError;
    tm.msgNumber = kNoMsgNumber;
    NetSpawnMessage(&tm);
#else
    printf("%s(%d) : HTML error: ", srcname, linnum);
    vprintf(dlcmsgs(errnum),ap);
    crlf(stdout);
    fflush(stdout);
#endif
    va_end(ap);
    errcnt++;
}

#if !SPP

/**************
 * Type error.
 */

int typerr(int n,type *t1,type *t2,...)
{
    va_list ap;
    va_start(ap, t2);
    if (generr(dlcmsgs(EM_error),n,ap))
    {   char *p1,*p2;
        Outbuffer buf;
        static char format[] = "%s: %s";

#if USEDLLSHELL
        err_reportmsg(eMsgError,kNoMsgNumber,n,ap);
#endif
        p1 = type_tostring(&buf,t1);
        err_continue(format,n == EM_explicit_cast ||
                            n == EM_illegal_cast ||
                            n == EM_no_castaway ||
                            n == EM_explicitcast ? "from" : "Had",p1);
        if (t2)
        {
            p2 = type_tostring(&buf,t2);
            err_continue(format,n == EM_explicit_cast ||
                                n == EM_illegal_cast ||
                                n == EM_no_castaway ||
                                n == EM_explicitcast ? "to  " : "and",p2);
        }
#if ERROR_GPF
        *(char *)0=0;
#endif
        va_end(ap);
        return 1;
    }
    va_end(ap);
    return 0;
}

/*****************************
 */

void err_noctor(Classsym *stag,list_t arglist)
{   char *p = alloca_strdup(cpp_prettyident(stag));
    Outbuffer buf;
    char *a = arglist_tostring(&buf,arglist);

    synerr(EM_no_ctor,p,p,a);   // can't find constructor matching arglist
}

/*********************************
 * Could not find a function match.
 */

void err_nomatch(const char *p,list_t arglist)
{   Outbuffer buf;
    char *a = arglist_tostring(&buf,arglist);

    synerr(EM_nomatch,p,a);             // no match
}

/***********************
 * Pretty-print a type.
 */

void prttype(type *t)
{   Outbuffer buf;

    dbg_printf(type_tostring(&buf,t));
}

/****************************************
 * Ambiguous functions. Print out both.
 */

void err_ambiguous(symbol *s1,symbol *s2)
{

    if (synerr(EM_overload_ambig))
    {   char *p1,*p2;
        Outbuffer buf;

        if (s1->Sclass == SCfuncalias)
            s1 = s1->Sfunc->Falias;
        p1 = param_tostring(&buf,s1->Stype);
        err_continue("Had: %s%s",prettyident(s1),p1);
        if (s2)
        {
            if (s2->Sclass == SCfuncalias)
                s2 = s2->Sfunc->Falias;
            p2 = param_tostring(&buf,s2->Stype);
            err_continue("and: %s%s",prettyident(s2),p2);
        }
        else
            // NULL means it's a built-in operator per C++98 13.6
            err_continue("and: Built-in operator");
#if ERROR_GPF
        *(char *)0=0;
#endif
    }
}

/****************************************
 * No instance.
 */

void err_noinstance(symbol *s1,symbol *s2)
{
    char *p = alloca_strdup(cpp_prettyident(s1));

    synerr(EM_no_inst_member,p,cpp_prettyident(s2));    // no this for class
}

/************************************
 */

void err_redeclar(symbol *s,type *t1,type *t2)
{

    if (synerr(EM_redefined,prettyident(s)))    // types don't match
    {   char *p1,*p2;
        Outbuffer buf;

        p1 = type_tostring(&buf,t1);
        err_continue(dlcmsgs(EM_was_declared),p1);      // It was declared as
        p2 = type_tostring(&buf,t2);
        err_continue(dlcmsgs(EM_now_declared),p2);      // It is now declared
    }
#if ERROR_GPF
    *(char *)0=0;
#endif
}

/************************************
 */

void err_override(symbol *sfbase,symbol *sfder)
{   char *p;


    p = alloca_strdup(prettyident(sfder));
    if (synerr(EM_diff_ret_type,prettyident(sfbase),p)) // types don't match
    {   char *p1,*p2;
        Outbuffer buf;

        p1 = type_tostring(&buf,sfbase->Stype);
        err_continue(dlcmsgs(EM_was_declared),p1);      // It was declared as
        p2 = type_tostring(&buf,sfder->Stype);
        err_continue(dlcmsgs(EM_now_declared),p2);      // It is now declared
    }

}

/*********************************
 * Not a member.
 */

void err_notamember(const char *id, Classsym *s, symbol *alternate)
{
    symbol_debug(s);
//    assert(type_struct(s->Stype));
    unsigned em;
    if (alternate)
        em = (s->Stype->Tflags & TFforward) ? EM_not_a_member_alt : EM_notamember_alt;
    else
        em = (s->Stype->Tflags & TFforward) ? EM_not_a_member : EM_notamember;

    synerr(em, id, prettyident(s), alternate ? alternate->Sident : "");         // not a member
}

#endif


