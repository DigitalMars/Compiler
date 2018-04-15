/**
 * Implementation of the
 * $(LINK2 http://www.digitalmars.com/download/freecompiler.html, Digital Mars C/C++ Compiler).
 *
 * Copyright:   Copyright (c) 1983-1998 by Symantec, All Rights Reserved
 *              Copyright (c) 2000-2017 by Digital Mars, All Rights Reserved
 * Authors:     $(LINK2 http://www.digitalmars.com, Walter Bright)
 * License:     Distributed under the Boost Software License, Version 1.0.
 *              http://www.boost.org/LICENSE_1_0.txt
 * Source:      https://github.com/DigitalMars/Compiler/blob/master/dm/src/dmc/derr.d
 */

/* Error handler                                        */

import core.stdc.stdarg;
import core.stdc.stdio;
import core.stdc.string;
import core.stdc.stdlib;

import dmd.backend.cdef;
import dmd.backend.cc;
import dmd.backend.global;
import dmd.backend.outbuf;
import dmd.backend.ty;
import dmd.backend.type;

import dmd.backend.dlist;

import dmcdll;
import dtoken;
import msgs2;
import parser;

extern (C++):

alias err_vprintf = vfprintf;
alias dbg_printf = printf;
alias ERRSTREAM = stdout;

__gshared int errcnt = 0;                 // error count

extern (C) void crlf(FILE*);

void prttype(type *);
void errmsgs_init();

void err_GPF()
{
    debug
    {
        __gshared char* p = null;
        *p = 0;       // case GPF on error
    }
}

/**************************************
 * Get filename, line number, and column number.
 * Params:
 *      filename = set to file name
 *      line = set to line number, -1 if none
 *      column = set to column number, -1 if none
 */

void getLocation(out char* filename, out int line, out int column)
{
    Srcpos sp = token_linnum();
    line = sp.Slinnum;

    blklst *b = cstate.CSfilblk;
    if (b)
    {   int col;

        // -2 because scanner is always a bit ahead of the parser
        col = (b == bl ? cast(char*)btextp : b.BLtextp) - b.BLtext - 2;
        if (col < 0)
            col = 0;

        column = col;
        filename = srcpos_name(sp);
    }
    else
    {
        column = -1;
        if (sp.Slinnum)
            filename = srcpos_name(sp);
        else
            filename = finname;              // use original source file name
    }
}

/************************************
 * Do a continuation printf message.
 */

private void err_continue(const(char)* format,...)
{
    va_list ap;
    va_start(ap, format);
    err_vprintf(ERRSTREAM,format,ap);
    crlf(ERRSTREAM);
    fflush(ERRSTREAM);
    err_reportmsgf_continue(format,ap);
    va_end(ap);
}

/**********************************
 * Print error message
 */

void err_message(const(char)* format,...)
{
    va_list ap;
    va_start(ap, format);
    err_vprintf(ERRSTREAM,format,ap);
    crlf(ERRSTREAM);
    fflush(ERRSTREAM);
    err_reportmsgf_fatal(format,ap);
    va_end(ap);
}

/*********************************
 * Print out error message to file stream fp.
 * Input:
 *      format          printf-style format string
 *      args            argument list pointer
 */

extern (D) private void err_print(FILE *fp,const(char)* q,const(char)* format,va_list args)
{
version (Posix)
{
    if (format[0] == '#')
    {
        wrtpos(fp,false);       // don't write line & draw ^ under it
        format++;
    }
    else
    {
        wrtpos(fp,true);        // write line & draw ^ under it
    }
}
else
{
    wrtpos(fp); // write line & draw ^ under it
}
    fputs(q,fp);
    vfprintf(fp,format,args);
    crlf(fp);

version (Win32)
    fflush(fp);
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

private int generr(const(char)* q,int errnum,va_list args)
{ char *msg;

  if (controlc_saw)                     /* poll for ^C                  */
        util_exit(EXIT_BREAK);

  if (errnum < 0)                       /* if warning                   */
        errnum = -errnum;
  else                                  /* else hard error              */
  {
debug
{
}
else
{
        __gshared int lastline = -1;       // last line that got an error
        int curline;

        // Don't print out more than one error per line
        curline = token_linnum().Slinnum;
        if (curline)                    // if line number is valid
        {
            if (curline == lastline)
                return 0;
            lastline = curline;
        }
}
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

void lexerr(uint errnum,...)
{
    va_list ap;
    va_start(ap, errnum);
    if (generr(dlcmsgs(EM_lexical_error),errnum,ap))
    {
        err_reportmsgf_error(dlcmsgs(errnum),ap);
    }
    va_end(ap);
}

void preerr(uint errnum,...)
{
    va_list ap;
    va_start(ap, errnum);
    if (generr(dlcmsgs(EM_preprocessor_error),errnum,ap))
    {
        err_reportmsgf_error(dlcmsgs(errnum),ap);
    }
    va_end(ap);
}

int synerr(uint errnum,...)
{
    va_list ap;
    va_start(ap, errnum);

    int result = generr(dlcmsgs(EM_error),errnum,ap);
    if (result)
        err_reportmsgf_error(dlcmsgs(errnum),ap);
    err_GPF();
    va_end(ap);
    return result;
}

int cpperr(uint errnum,...)
{
    va_list ap;
    va_start(ap, errnum);

    int result = generr(dlcmsgs(EM_error),errnum,ap);
    if (result)
        err_reportmsgf_error(dlcmsgs(errnum),ap);
    err_GPF();
    va_end(ap);
    return result;
}

int tx86err(uint errnum,...)
{
    va_list ap;
    va_start(ap, errnum);

    int result = generr(dlcmsgs(EM_error),errnum,ap);
    if (result)
        err_reportmsgf_error(dlcmsgs(errnum),ap);
    err_GPF();
    va_end(ap);
    return result;
}

/* Translation table from warning number to message number              */
version (SPP)
{
private __gshared short[24] war_to_msg =
[       -1,
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
];
}
else
{
private __gshared short[31] war_to_msg =
[       -1,
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

    // Posix#if TARGET_LINUX || TARGET_OSX || TARGET_FREEBSD || TARGET_OPENBSD || TARGET_SOLARIS
        /*EM_skip_attribute,
        EM_warning_message,
        EM_bad_vastart,
        EM_undefined_inline,*/
];
}

/* Parallel table that controls the enabling of individual warnings     */
private __gshared ubyte[war_to_msg.length] war_to_msg_enable;

/***************************
 * Enable/disable specific warnings.
 * Input:
 *      warnum  warning number, if -1 that means all warnings
 *      on      0 = off, 1 = on
 */

void err_warning_enable(uint warnum,int on)
{
    if (warnum == -1)
        memset(war_to_msg_enable.ptr,on,war_to_msg_enable.sizeof);
    else
    {
        if (warnum < war_to_msg.length)
            war_to_msg_enable[warnum] = cast(ubyte)on;
    }
}

void warerr(uint warnum,...)
{   char[20] msg = void;

    va_list ap;
    va_start(ap, warnum);

    assert(warnum < war_to_msg.length);
    if (!war_to_msg_enable[warnum])     /* if enabled                   */
    {   int errnum;

        if (!(config.flags2 & CFG2warniserr))
            errcnt--;                   /* to offset the increment      */
        sprintf(msg.ptr,dlcmsgs(EM_warning),warnum);

        // Convert from warning number to message number
        errnum = war_to_msg[warnum];

        if (generr(msg.ptr,-errnum,ap))
        {
            err_reportmsgf_warning((config.flags2 & CFG2warniserr) != 0,
                warnum,dlcmsgs(errnum),ap);
        }
    }
    va_end(ap);
}

/**************************
 * A non-recoverable error has occurred.
 */

void err_fatal(uint errnum,...)
{
    va_list ap;
    va_start(ap, errnum);
version (_WINDLL)
{
}
else
{
    fputs(dlcmsgs(EM_fatal_error), ERRSTREAM);          // Fatal error:
    err_vprintf(ERRSTREAM,dlcmsgs(errnum),ap);
    crlf(ERRSTREAM);
}
    err_reportmsgf_fatal(dlcmsgs(errnum),ap);
    err_GPF();
    va_end(ap);
    err_exit();
}

/*************************
 * This is the handler for out-of-memory errors.
 * Called by mem package.
 */

void err_nomem()
{
    //*cast(char*)0=0;
    err_fatal(EM_nomem);
}

/************************************
 * Error in the command line.
 */

void cmderr(uint errnum,...)
{
    va_list ap;
    va_start(ap, errnum);
version (_WINDLL)
{
    dll_printf(dlcmsgs(EM_command_line_error));
}
else
{
    fputs(dlcmsgs(EM_command_line_error), ERRSTREAM);
}
    err_vprintf(ERRSTREAM,dlcmsgs(errnum),ap);
    err_reportmsgf_fatal(dlcmsgs(errnum),ap);
    va_end(ap);
    err_exit();
}

/***************************************
 * Error in HTML source
 */

void html_err(const(char)* srcname, uint linnum, uint errnum, ...)
{
    va_list ap;
    va_start(ap, errnum);
    const(char)* format = dlcmsgs(errnum);
    dmcdll_html_err(srcname, linnum, format, ap);
    va_end(ap);
    errcnt++;
}

version (SPP)
{
}
else
{

/**************
 * Type error.
 */

int typerr(int n,type *t1,type *t2,...)
{
    va_list ap;
    va_start(ap, t2);
    if (generr(dlcmsgs(EM_error),n,ap))
    {   char *p1;
        char *p2;
        Outbuffer buf;
        const(char)* format = "%s: %s";

        err_reportmsgf_error(dlcmsgs(n),ap);
        p1 = type_tostring(&buf,t1);
        err_continue(format,n == EM_explicit_cast ||
                            n == EM_illegal_cast ||
                            n == EM_no_castaway ||
                            n == EM_explicitcast ? cast(char*)"from" : cast(char*)"Had",p1);
        if (t2)
        {
            p2 = type_tostring(&buf,t2);
            err_continue(format,n == EM_explicit_cast ||
                                n == EM_illegal_cast ||
                                n == EM_no_castaway ||
                                n == EM_explicitcast ? cast(char*)"to  " : cast(char*)"and",p2);
        }
        err_GPF();
        va_end(ap);
        return 1;
    }
    va_end(ap);
    return 0;
}

/*****************************
 */

void err_noctor(Classsym *stag,list_t arglist)
{
    char* p;
    {
        auto q = cpp_prettyident(stag);
        const len = strlen(q) + 1;
        auto s = cast(char *)alloca(len);
        p = cast(char *)memcpy(s,q,len);
    }

    Outbuffer buf;
    char *a = arglist_tostring(&buf,arglist);

    synerr(EM_no_ctor,p,p,a);   // can't find constructor matching arglist
}

/*********************************
 * Could not find a function match.
 */

void err_nomatch(const(char)* p,list_t arglist)
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

void err_ambiguous(Symbol *s1,Symbol *s2)
{

    if (synerr(EM_overload_ambig))
    {   char *p1;
        char *p2;
        Outbuffer buf;

        if (s1.Sclass == SCfuncalias)
            s1 = s1.Sfunc.Falias;
        p1 = param_tostring(&buf,s1.Stype);
        err_continue("Had: %s%s",prettyident(s1),p1);
        if (s2)
        {
            if (s2.Sclass == SCfuncalias)
                s2 = s2.Sfunc.Falias;
            p2 = param_tostring(&buf,s2.Stype);
            err_continue("and: %s%s",prettyident(s2),p2);
        }
        else
            // NULL means it's a built-in operator per C++98 13.6
            err_continue("and: Built-in operator");
        err_GPF();
    }
}

/****************************************
 * No instance.
 */

void err_noinstance(Symbol *s1,Symbol *s2)
{
    char* p;
    {
        auto q = cpp_prettyident(s1);
        const len = strlen(q) + 1;
        auto s = cast(char *)alloca(len);
        p = cast(char *)memcpy(s,q,len);
    }

    synerr(EM_no_inst_member,p,cpp_prettyident(s2));    // no this for class
}

/************************************
 */

void err_redeclar(Symbol *s,type *t1,type *t2)
{

    if (synerr(EM_redefined,prettyident(s)))    // types don't match
    {   char *p1;
        char *p2;
        Outbuffer buf;

        p1 = type_tostring(&buf,t1);
        err_continue(dlcmsgs(EM_was_declared),p1);      // It was declared as
        p2 = type_tostring(&buf,t2);
        err_continue(dlcmsgs(EM_now_declared),p2);      // It is now declared
    }
    err_GPF();
}

/************************************
 */

void err_override(Symbol *sfbase,Symbol *sfder)
{
    char* p;
    {
        auto q = cpp_prettyident(sfder);
        const len = strlen(q) + 1;
        auto s = cast(char *)alloca(len);
        p = cast(char *)memcpy(s,q,len);
    }

    if (synerr(EM_diff_ret_type,prettyident(sfbase),p)) // types don't match
    {   char *p1;
        char *p2;
        Outbuffer buf;

        p1 = type_tostring(&buf,sfbase.Stype);
        err_continue(dlcmsgs(EM_was_declared),p1);      // It was declared as
        p2 = type_tostring(&buf,sfder.Stype);
        err_continue(dlcmsgs(EM_now_declared),p2);      // It is now declared
    }

}

/*********************************
 * Not a member.
 */

void err_notamember(const(char)* id, Classsym *s, Symbol *alternate)
{
    symbol_debug(s);
//    assert(type_struct(s.Stype));
    uint em;
    if (alternate)
        em = (s.Stype.Tflags & TFforward) ? EM_not_a_member_alt : EM_notamember_alt;
    else
        em = (s.Stype.Tflags & TFforward) ? EM_not_a_member : EM_notamember;

    synerr(em, id, prettyident(s), alternate ? alternate.Sident.ptr : "".ptr);         // not a member
}

}


