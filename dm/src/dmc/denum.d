/**
 * Implementation of the
 * $(LINK2 http://www.digitalmars.com/download/freecompiler.html, Digital Mars C/C++ Compiler).
 *
 * Copyright:   Copyright (c) 1984-1998 by Symantec, All Rights Reserved
 *              Copyright (c) 2000-2017 by Digital Mars, All Rights Reserved
 * Authors:     $(LINK2 http://www.digitalmars.com, Walter Bright)
 * License:     Distributed under the Boost Software License, Version 1.0.
 *              http://www.boost.org/LICENSE_1_0.txt
 * Source:      https://github.com/DigitalMars/Compiler/blob/master/dm/src/dmc/denum.d
 */

// Handle enum declarations

module denum;

version (HTOD) { version = COMPILEIT; }
version (SCPP) { version = COMPILEIT; }


version (COMPILEIT)
{

import core.stdc.stdio;
import core.stdc.string;


import dtoken;
import msgs2;
import parser;
import scopeh;

import ddmd.backend.cdef;
import ddmd.backend.cc;
import ddmd.backend.el;
import ddmd.backend.global;
import ddmd.backend.oper;
import ddmd.backend.ty;
import ddmd.backend.type;

import tk.dlist;

extern (C++):

Symbol *scope_searchinner(const(char)* id, uint sct);
void n2_chkexist(Classsym* stag, char* name);
void n2_addmember(Classsym* stag, Symbol* smember);
Symbol* scope_define(const(char)* id, uint sct, enum_SC sclass);
void *mem_calloc(size_t);
Symbol* cpp_findmember(Classsym* sclass, const(char)* sident, uint flag);


/+
#include        "cc.h"
#include        "token.h"               /* must be before parser.h */
#include        "parser.h"              /* for enum TK             */
#include        "global.h"
#include        "type.h"
#include        "scope.h"
#if TX86
#include        "cgcv.h"
#endif
#include        "cpp.h"
#include        "el.h"
#include        "oper.h"

static char __file__[] = __FILE__;      /* for tassert.h                */
#include        "tassert.h"

STATIC void enumdcllst(Symbol *se);
STATIC Symbol * n2_defineenum(char *enum_tag,int flags);
STATIC int n2_isenum(Symbol **ps);

/****************************
 * Do enum specifier.
 * Forward referencing does not work.
 *      enum_specifier ::=      enum identifier
 *                              enum { enum_decl_list }
 *                              enum identifier { enum_decl_list }
 */

+/

type * enumspec()
{   char *enum_tag;
    Symbol *s;
    type *t;
    type *tenum;
    uint flags = 0;
    auto tserr = tstypes[TYint];

    stoken();                           /* skip over "enum"             */
    if (tok.TKval == TKident)           /* if we found an identifier    */
    {
        enum_tag = parc_strdup(tok.TKid);       // save tag name
        stoken();
    }
    else if (tok.TKval != TKlcur)
    {   synerr(EM_tag);         // ident or left curly bracket expected
        return tserr;
    }
    else
    {
        if (CPP || htod_running())
            enum_tag = n2_genident();   // generate our own tag name
        else
        {   enumdcllst(null);
            return tstypes[TYint];
        }
        flags = SENnotagname;
    }

    if (CPP)
    {
        s = null;
        if (pstate.STclasssym)          // if nested class scope
        {
            if (tok.TKval == TKlcur)
                s = n2_searchmember(pstate.STclasssym,enum_tag);
            else
            {   s = cpp_findmember(pstate.STclasssym,enum_tag,false);
                if (!s && tok.TKval == TKcolcol)
                    s = scope_search(enum_tag,SCTglobal | SCTcover);
            }
            if (s && s.Scover)
                s = s.Scover;
        }
        else if (tok.TKval == TKcolcol)
        {
            version (all)
            {
                token_unget();
                token_setident(enum_tag);
                s = nspace_getqual(2);
                if (!s)
                    return tserr;
                if (s.Scover)
                    s = s.Scover;
                if (s.Sclass != SCenum || tok.TKval == TKsemi)
                {   token_unget();
                    token_setident(s.Sident.ptr);
                    token_unget();
                    tok.TKval = TKcolcol;
                    assert(s.Sscope);
                    return s.Sscope.Stype;
                }
            }
            else
            {
                s = scope_search(enum_tag,SCTglobal | SCTnspace | SCTlocal | SCTcover); // find the struct tag
                // BUG: What if namespace?
                if (s && !n2_isstruct(&s))
                    s = null;
            }
        }
        else
        {
            // Find the enum tag
            Scope *sc;

            sc = scope_find(SCTlocal | SCTnspace | SCTglobal);
            assert(sc);
            s = scope_searchinner(enum_tag, SCTcover | sc.sctype);

            if (s && !n2_isenum(&s))
                s = null;
        }
        while (tok.TKval == TKcolcol)
        {   if (!s || !type_struct(s.Stype))
            {   cpperr(EM_class_colcol,enum_tag);       // must be a class name
                break;
            }
            stoken();
            if (tok.TKval != TKident)
            {   synerr(EM_ident_exp);                   // identifier expected
                break;
            }
            parc_free(enum_tag);
            enum_tag = parc_strdup(tok.TKid);
            s = cpp_findmember(cast(Classsym *)s,enum_tag,false);
            if (s && s.Scover)
                s = s.Scover;
            if (s && !n2_isenum(&s))
                s = null;
            stoken();
        }
    }
    else
        s = scope_searchinner(enum_tag,SCTglobaltag | SCTtag); // find the enum tag
    if (tok.TKval == TKlcur)                    // then it's an enum def
    {
        if (s)                                  // if it exists
        {
            if (s.Sclass == SCenum)
            {
                if (!(s.Senum.SEflags & SENforward))
                {
                    synerr(EM_multiple_def,enum_tag);           // already defined
                    goto Lerr;
                }
            }
            else
            {   synerr(EM_badtag,enum_tag);     // not correct enum tag
             Lerr:
                panic(TKrcur);
                stoken();
                tenum = tserr;
                goto Lret;
            }
        }
        else
        {
            s = n2_defineenum(enum_tag,flags);
        }
        enumdcllst(s);
        s.Senum.SEflags &= ~SENforward;
        htod_decl(s);
    }
    else
    {
            /* Determine if we should look at previous scopes for
                a definition or not.
             */
            if ((tok.TKval != TKcomma && tok.TKval != TKsemi) || pstate.STinexp)
            {
                if (CPP)
                {
                    if (s && n2_isenum(&s))
                        goto Ldone;
                    s = symbol_search(enum_tag);
                    if (s && s.Scover)
                        s = s.Scover;  // try the hidden definition
                }
                else
                    // find the enum tag
                    s = scope_search(enum_tag,SCTglobaltag | SCTtag);
            }

            if (!s)                     // if tag doesn't exist
            {                           // create it
                s = n2_defineenum(enum_tag,flags);
                htod_decl(s);
            }
    }

Ldone:
    debug assert(s == s.Stype.Ttag);
    if (CPP)
        tenum = s.Stype;
    else
    {
        tenum = s.Stype.Tnext;
        if (htod_running())
        {
            tenum = type_copy(tenum);
            tenum.Ttypedef = s;
        }
    }
Lret:
    parc_free(enum_tag);
    return tenum;
}

/**************************
 * Define the enumerator_list.
 * Input:
 *      se      enum tag symbol
 */

//private
 void enumdcllst(Symbol *se)
{   targ_llong enumval = -1L;
    Symbol *s;
    symlist_t el;
    type *tmember;
    type *tbase;
    int negative = 0;

    tmember = tstypes[TYint];
    tbase = tstypes[TYint];
    stoken();                           // skip over '{'
    while (1)                           // '}' ends it
    {
        if (tok.TKval != TKident)
        {
            if (tok.TKval != TKrcur ||
                (config.ansi_c && !CPP) /*||
                // C++98 7-3 'enum { };' is ill-formed
                (CPP && se.Senum.SEflags & SENnotagname)*/)
                synerr(EM_ident_exp);           // identifier expected
            panic(TKrcur);
            stoken();
            break;
        }
        if (CPP)
        {
            if (level == -1)            // if in class scope
            {   struct_t *st = pstate.STclasssym.Sstruct;

                //symbol_debug(pstate.STclasssym);
                if (symbol_searchlist(se.Senum.SEenumlist,tok.TKid))
                    synerr(EM_multiple_def,tok.TKid);
                s = symbol_name(tok.TKid,SCconst,tmember);
                s.Sflags = (s.Sflags & ~SFLpmask) | st.access;

                // Check for already existing member name
                n2_chkexist(pstate.STclasssym,s.Sident.ptr);

                // add member s to list of fields
                n2_addmember(pstate.STclasssym,s);
            }
            else
            {
                s = scope_define(tok.TKid,SCTglobal | SCTnspace | SCTlocal,SCconst);
                s.Stype = tmember;
                tmember.Tcount++;
            }
            list_append(&se.Senum.SEenumlist,s);      // add to member list of enum
        }
        else
        {
            s = scope_define(tok.TKid,SCTglobal | SCTlocal,SCconst);
            s.Stype = tmember;
            tmember.Tcount++;
            if (se && htod_running())
                list_append(&se.Senum.SEenumlist,s);  // add to member list of enum
        }
        s.Sflags |= SFLvalue;
    L1:
        stoken();
        if (tok.TKval == TKeq)          /* if '='                       */
        {
            elem *e;

            stoken();
            s.Svalue = el_longt(s.Stype,enumval + 1);

            char inarglistsave = pstate.STinarglist;
            pstate.STinarglist = 0;
            e = CPP ? assign_exp() : const_exp();
            pstate.STinarglist = inarglistsave;

            if (!tyintegral(e.ET.Tty))
                synerr(EM_integral);            // integral expression expected
            else if (CPP)
            {   if (tybasic(e.ET.Tty) > tbase.Tty)
                    tbase = tstypes[tybasic(e.ET.Tty)];
                if (_tysize[TYint] == 4)
                {
                    // Can represent bits with smaller type
                    if (tbase.Tty == TYulong)
                        tbase = tstypes[TYuint];
                    if (tbase.Tty == TYlong)
                        tbase = tstypes[TYint];
                }
                // CPP98 7.2-4 "Prior to the closing brace, each enumerator
                // is the type of its initializing value."
                type_settype(&s.Stype, e.ET);
            }
            e = poptelem3(e);
            if (e.Eoper != OPconst)
            {   synerr(EM_num);                 // number expected
                enumval = 0;
            }
            else
                enumval = el_tolong(e);
            if (!tyuns(e.ET.Tty) && enumval < 0)
                negative = 1;
            el_free(e);
            el_free(s.Svalue);
        }
        else
        {
            // If overflow, bump up to next larger type
            switch (tbase.Tty)
            {
                case TYint:
                    if (_tysize[TYint] == 4)
                        goto case_TYlong;
                    if (enumval == 0x7FFF)
                    {
                        if (!CPP)
                            synerr(EM_badnumber);       // not representable as int
                        if (negative)
                            tbase = tstypes[TYlong];
                        else
                            tbase = tstypes[TYuint];
                    }
                    break;

                case TYuint:
                    if (_tysize[TYint] == 4)
                        goto case_TYulong;
                    if (enumval == 0xFFFF)
                        tbase = tstypes[TYlong];
                    break;

                case TYlong:
                case_TYlong:
                    if (enumval == 0x7FFFFFFF)
                    {
                        if (!CPP)
                            synerr(EM_badnumber);       // not representable as int
                        if (negative)
                            tbase = tstypes[TYllong];
                        else
                            tbase = tstypes[TYulong];
                    }
                    break;

                case TYulong:
                case_TYulong:
                    if (enumval == 0xFFFFFFFF)
                        tbase = tstypes[TYllong];
                    break;

                case TYllong:
                    if (enumval == 0x7FFFFFFFFFFFFFFFL)
                    {
                        if (negative)
                            synerr(EM_badnumber);       // not representable as int
                        else
                            tbase = tstypes[TYulong];
                    }
                    break;

                case TYullong:
                    if (enumval == -1L)
                        synerr(EM_badnumber);   // not representable as int
                    break;

                default:
                    assert(0);
            }
            enumval++;
        }
        s.Svalue = el_longt(s.Stype,enumval);
        if (!CPP && !se)
        {
            htod_decl(s);       // anonymous enum members are const declarations
        }
        if (tok.TKval == TKcomma)
        {       stoken();
                continue;
        }
        chktok(TKrcur,EM_rcur);
        break;
    }

    if (CPP)
    {
        type_settype(&se.Stype.Tnext, tbase);

        // Go back through enum members, now that we've seen the closing brace,
        // and rewrite the types to be enum types.
        for (el = se.Senum.SEenumlist; el; el = list_next(el))
        {   elem *e;

            s = list_symbol(el);
            type_settype(&s.Stype, se.Stype);
            e = s.Svalue;
            if (e.ET != tbase)
            {   e = _cast(e, tbase);
                e = poptelem(e);
            }
            type_settype(&e.ET, se.Stype);
            s.Svalue = e;
        }
    }
}

/*********************************
 * Define an enum tag symbol.
 */

//private
 Symbol * n2_defineenum(char *enum_tag,int flags)
{   Symbol *s;
    type *t;

    if (CPP)
    {
        // If we should make this a nested enum
        if (pstate.STclasssym /*&& tok.TKval == TKlcur*/)
            s = null;
        else
            s = scope_searchinner(enum_tag,SCTglobal | SCTnspace | SCTlocal);
        if (s)
        {   // Already defined, so create a second 'covered' definition
            s.Scover = cast(Classsym *)symbol_calloc(enum_tag);
            s = s.Scover;
        }
        else
            if (pstate.STclasssym                       // if in class scope and
                /*&& tok.TKval == TKlcur*/)             // sure it's a def
            {
                // Created nested class definition
                n2_chkexist(pstate.STclasssym,enum_tag);
                s = symbol_calloc(enum_tag);
                s.Sclass = SCenum;
                s.Sflags |= pstate.STclasssym.Sstruct.access;
                n2_addmember(pstate.STclasssym,s);
            }
            else
                s = scope_define(enum_tag, SCTglobal | SCTnspace | SCTlocal,SCenum);
    }
    else
        s = scope_define(enum_tag, SCTglobaltag | SCTtag,SCenum);
    s.Senum = cast(enum_t *) mem_calloc(enum_t.sizeof);
    s.Senum.SEflags |= flags;
    s.Senum.SEflags |= SENforward;    // forward reference
    t = type_alloc(TYenum);
    t.Ttag = cast(Classsym *)s;            // enum tag name
    s.Stype = t;
    t.Tcount++;
    s.Stype.Tnext = tstypes[TYint];
    tstypes[TYint].Tcount++;
    return s;
}

/************************
 * Determine if *ps is an enum symbol or not.
 * If it is a typedef'd enum symbol, modify *ps to be the
 * real tag symbol.
 */

//private
 int n2_isenum(Symbol **ps)
{
    int result = 0;

    assert(ps);
    Symbol *s = *ps;
    if (s)
    {   switch (s.Sclass)
        {   case SCenum:
                result = 1;
                break;
            case SCtypedef:
                if (tybasic(s.Stype.Tty) == TYenum)
                {   *ps = s.Stype.Ttag;
                    result = 1;
                }
                break;
            default:
                break;
        }
    }
    return result;
}

}
