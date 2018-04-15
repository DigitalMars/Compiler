/**
 * Implementation of the
 * $(LINK2 http://www.digitalmars.com/download/freecompiler.html, Digital Mars C/C++ Compiler).
 *
 * Copyright:   Copyright (c) 2001-2017 by Digital Mars, All Rights Reserved
 * Authors:     $(LINK2 http://www.digitalmars.com, Walter Bright)
 * License:     Distributed under the Boost Software License, Version 1.0.
 *              http://www.boost.org/LICENSE_1_0.txt
 * Source:      https://github.com/DigitalMars/Compiler/blob/master/dm/src/dmc/adl.d
 */

// CPP98 3.4.2 Argument-dependent name lookup

module adl;

version (SPP)
{
}
else
{

import core.stdc.ctype;
import core.stdc.stdio;
import core.stdc.stdlib;
import core.stdc.string;

import dmd.backend.cc;
import dmd.backend.cdef;
import dmd.backend.cgcv;
import dmd.backend.el;
import dmd.backend.global;
import dmd.backend.oper;
import dmd.backend.ty;
import dmd.backend.type;

import cpp;
import dtoken;
import parser;
import scopeh;

import dmd.backend.dlist;

extern (C++):

//debug = LOG;

void adl_scanType(list_t *pl, type *t);

/*****************************************
 * Add namespace or class to list *pl.
 */

void adl_addSymbol(list_t *pl, Symbol *s)
{
    debug (LOG) printf("\taddSymbol('%s')\n", s ? s.Sident : "");
    if (/*s &&*/ !list_inlist(*pl, s))
    {
        list_prepend(pl, s);
    }
}

/*****************************************
 * Add class namespace and its associated class namespaces.
 */

void adl_addClassNamespace(list_t *pl, Classsym *s)
{
    if (!list_inlist(*pl, s))
    {
        adl_addSymbol(pl, s.Sscope);

        // Add direct and indirect base classes, and the
        // namespaces they are defined in
        for (baseclass_t *b = s.Sstruct.Sbase; b; b = b.BCnext)
            adl_addClassNamespace(pl, b.BCbase);
    }
}

/*****************************************
 * Add class and its associated classes and associated namespaces.
 */

void adl_addClass(list_t *pl, Classsym *s)
{
    if (!list_inlist(*pl, s))
    {
        // IMPROVE: this should be calculated once per class and then cached

        adl_addClassNamespace(pl, s);
        adl_addSymbol(pl, s);

        // If instance of a template
        if (s.Sstruct.Stempsym)
        {
            adl_addSymbol(pl, s.Sstruct.Stempsym.Sscope);

            // BUG: Should we use Spr_arglist instead?
            // BUG: What about types in default template arguments?
            for (param_t *p = s.Sstruct.Sarglist; p; p = p.Pnext)
            {
                // BUG: Review CPP98 rules for template template arguments
                if (p.Ptype)
                    adl_scanType(pl, p.Ptype);
            }
        }
    }
}

/***************************************
 * Given a type, add associated namespaces and associated classes
 * to list *pl.
 */

void adl_scanType(list_t *pl, type *t)
{
    Symbol *s;

    //type_debug(t);
    tym_t ty = tybasic(t.Tty);
    switch (ty)
    {
        case TYvoid:
        case TYident:
            break;

        case TYarray:
        case TYref:
            adl_scanType(pl, t.Tnext);
            break;

        case TYstruct:
            s = t.Ttag;
            if (s.Sstruct.Sflags & STRunion)  // if a union
                adl_addSymbol(pl, s.Sscope);
            else
                adl_addClass(pl, cast(Classsym *)s);
            break;

        case TYenum:
            adl_addSymbol(pl, t.Ttag.Sscope);
            break;

        case TYmemptr:          // pointer to data or function member
            adl_addClass(pl, t.Ttag);
            adl_scanType(pl, t.Tnext);
            break;

        default:
            if (typtr(ty))
            {
                adl_scanType(pl, t.Tnext);
            }
            else if (tyfunc(ty))
            {
                // Do parameters
                for (param_t *p = t.Tparamtypes; p; p = p.Pnext)
                {
                    if (p.Ptype)
                        adl_scanType(pl, p.Ptype);
                }
                adl_scanType(pl, t.Tnext);     // and return type
            }
            else if (tyarithmetic(ty) || tynullptr(ty))
            { }
            else if (ty == TYtemplate)
            {
                // Not sure what to do with this, ignore.
                // Happens with ADL on operator overloads in test\iostream.cpp
            }
            else
            {
                debug type_print(t);
                //printf("ty = x%x\n", ty);
                assert(0);
            }
            break;
    }
}

/*****************************************
 * Scan function argument list and return list of
 * associated namespaces and classes.
 */

list_t adl_scanArglist(list_t arglist)
{
    list_t nclist = null;

    debug (LOG) printf("adl_scanArglist()\n");
    for (list_t la = arglist; la; la = list_next(la))
    {
        elem *e = cast(elem *)list_ptr(la);

        debug (LOG)
        {
            elem_print(e);
            type_print(e.ET);
        }
        if (e.Eoper == OPrelconst && e.PEFflags & PEFtemplate_id)
        {
            for (param_t *ptal = e.EV.Vtal; ptal; ptal = ptal.Pnext)
            {
                if (ptal.Ptype)
                    adl_scanType(&nclist, ptal.Ptype);
                else if (ptal.Pelem)
                    adl_scanType(&nclist, ptal.Pelem.ET);
            }
        }
        else
            adl_scanType(&nclist, e.ET);
    }
    return nclist;
}

/******************************************
 */

Symbol *adl_searchNamespace(const(char)* id, Symbol *sn)
{
    debug (LOG) printf("adl_searchNamespace(id='%s', sn = '%s')\n", id, sn.Sident);
    return findsy(id, sn.Snameroot);
}

/******************************************
 */

Symbol *adl_searchClass(const(char)* id, Classsym *stag)
{
    return findsy(id, stag.Sstruct.Sroot);
}

/********************************************
 * Return list of function symbols via ADL.
 */

Symbol *adl_lookup(char *id, Symbol *so, list_t arglist)
{
    list_t nclist;
    list_t symlist = null;

    debug (LOG) printf("adl_lookup(id='%s')\n", id);
    nclist = adl_scanArglist(arglist);

    if (!nclist)
        return so;

    for (list_t ncl = nclist; ncl; ncl = list_next(ncl))
    {
        Symbol *sa = cast(Symbol *)list_ptr(ncl);
        Symbol *s;

        debug (LOG) printf("\tlooking at associated namespace/class '%s'\n", sa ? sa.Sident : "");
        //symbol_print(sa);

        if (!sa)
        {
            // Search global namespace
            s = scope_search(id, SCTglobal);
        }
        else if (sa.Sclass == SCnamespace)
        {
            s = adl_searchNamespace(id, sa);
        }
        else if (sa.Sclass == SCstruct)
        {
            s = adl_searchClass(id, cast(Classsym *)sa);
        }
        else
        {
            assert(0);
        }

        if (s)
        {
            debug (LOG) printf("\tfound symbol '%s' in scope '%s'\n", s.Sident, s.Sscope ? s.Sscope.Sident : "");
        }
        if (s && tyfunc(s.Stype.Tty) && s.Sclass != SCtypedef &&
            s.Sclass != SCalias && s.Sclass != SCfuncalias)
        {
            debug (LOG) printf("\tadding symbol '%s'\n", s.Sident);
            if (!list_inlist(symlist, s))
                list_prepend(&symlist, s);
        }
    }

    list_free(&nclist, FPNULL);

    if (!symlist)
        return so;

    if (so && !list_inlist(symlist, so))
        list_prepend(&symlist, so);

    so = cast(Symbol *)list_ptr(symlist);
    if (list_nitems(symlist) == 1)
    {
        list_free(&symlist, FPNULL);
        return so;
    }

    Symbol* sadl = symbol_name(&so.Sident[0], SCadl, so.Stype);
    sadl.Spath = symlist;
    return sadl;
}

}
