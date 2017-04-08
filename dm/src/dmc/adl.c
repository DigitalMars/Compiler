/**
 * Implementation of the
 * $(LINK2 http://www.digitalmars.com/download/freecompiler.html, Digital Mars C/C++ Compiler).
 *
 * Copyright:   Copyright (c) 2001-2017 by Digital Mars, All Rights Reserved
 * Authors:     $(LINK2 http://www.digitalmars.com, Walter Bright)
 * License:     Distributed under the Boost Software License, Version 1.0.
 *              http://www.boost.org/LICENSE_1_0.txt
 * Source:      https://github.com/DigitalMars/Compiler/blob/master/dm/src/dmc/adl.c
 */

// CPP98 3.4.2 Argument-dependent name lookup


#if !SPP

#include        <stdio.h>
#include        <ctype.h>
#include        <string.h>
#include        <stdlib.h>
#include        "cc.h"
#include        "parser.h"
#include        "token.h"
#include        "global.h"
#include        "oper.h"
#include        "el.h"
#include        "type.h"
#include        "cpp.h"
#include        "cgcv.h"

static char __file__[] = __FILE__;      /* for tassert.h                */
#include        "tassert.h"

#define LOG     0

void adl_scanType(list_t *pl, type *t);

/*****************************************
 * Add namespace or class to list *pl.
 */

void adl_addSymbol(list_t *pl, symbol *s)
{
#if LOG
    printf("\taddSymbol('%s')\n", s ? s->Sident : "");
#endif
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
        adl_addSymbol(pl, s->Sscope);

        // Add direct and indirect base classes, and the
        // namespaces they are defined in
        for (baseclass_t *b = s->Sstruct->Sbase; b; b = b->BCnext)
            adl_addClassNamespace(pl, b->BCbase);
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
        if (s->Sstruct->Stempsym)
        {
            adl_addSymbol(pl, s->Sstruct->Stempsym->Sscope);

            // BUG: Should we use Spr_arglist instead?
            // BUG: What about types in default template arguments?
            for (param_t *p = s->Sstruct->Sarglist; p; p = p->Pnext)
            {
                // BUG: Review CPP98 rules for template template arguments
                if (p->Ptype)
                    adl_scanType(pl, p->Ptype);
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
    tym_t ty;
    symbol *s;

    type_debug(t);
    ty = tybasic(t->Tty);
    switch (ty)
    {
        case TYvoid:
        case TYident:
            break;

        case TYarray:
        case TYref:
            adl_scanType(pl, t->Tnext);
            break;

        case TYstruct:
            s = t->Ttag;
            if (s->Sstruct->Sflags & STRunion)  // if a union
                adl_addSymbol(pl, s->Sscope);
            else
                adl_addClass(pl, (Classsym *)s);
            break;

        case TYenum:
            adl_addSymbol(pl, t->Ttag->Sscope);
            break;

        case TYmemptr:          // pointer to data or function member
            adl_addClass(pl, t->Ttag);
            adl_scanType(pl, t->Tnext);
            break;

        default:
            if (typtr(ty))
            {
                adl_scanType(pl, t->Tnext);
            }
            else if (tyfunc(ty))
            {
                // Do parameters
                for (param_t *p = t->Tparamtypes; p; p = p->Pnext)
                {
                    if (p->Ptype)
                        adl_scanType(pl, p->Ptype);
                }
                adl_scanType(pl, t->Tnext);     // and return type
            }
            else if (tyarithmetic(ty) || tynullptr(ty))
                ;
            else if (ty == TYtemplate)
            {
                // Not sure what to do with this, ignore.
                // Happens with ADL on operator overloads in test\iostream.cpp
            }
            else
            {
#ifdef DEBUG
                type_print(t);
#endif
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
    list_t nclist = NULL;

#if LOG
    printf("adl_scanArglist()\n");
#endif
    for (list_t la = arglist; la; la = list_next(la))
    {
        elem *e = (elem *)list_ptr(la);

#if LOG
        elem_print(e);
        type_print(e->ET);
#endif
        if (e->Eoper == OPrelconst && e->PEFflags & PEFtemplate_id)
        {
            for (param_t *ptal = e->EV.sp.spu.Vtal; ptal; ptal = ptal->Pnext)
            {
                if (ptal->Ptype)
                    adl_scanType(&nclist, ptal->Ptype);
                else if (ptal->Pelem)
                    adl_scanType(&nclist, ptal->Pelem->ET);
            }
        }
        else
            adl_scanType(&nclist, e->ET);
    }
    return nclist;
}

/******************************************
 */

symbol *adl_searchNamespace(const char *id, symbol *sn)
{
#if LOG
    printf("adl_searchNamespace(id='%s', sn = '%s')\n", id, sn->Sident);
#endif
    return findsy(id, sn->Snameroot);
}

/******************************************
 */

symbol *adl_searchClass(const char *id, Classsym *stag)
{
    return findsy(id, stag->Sstruct->Sroot);
}

/********************************************
 * Return list of function symbols via ADL.
 */

symbol *adl_lookup(char *id, symbol *so, list_t arglist)
{
    list_t nclist;
    list_t symlist = NULL;

#if LOG
    printf("adl_lookup(id='%s')\n", id);
#endif
    nclist = adl_scanArglist(arglist);

    if (!nclist)
        return so;

    for (list_t ncl = nclist; ncl; ncl = list_next(ncl))
    {
        symbol *sa = (symbol *)list_ptr(ncl);
        symbol *s;

#if LOG
        printf("\tlooking at associated namespace/class '%s'\n", sa ? sa->Sident : "");
        //symbol_print(sa);
#endif

        if (!sa)
        {
            // Search global namespace
            s = scope_search(id, SCTglobal);
        }
        else if (sa->Sclass == SCnamespace)
        {
            s = adl_searchNamespace(id, sa);
        }
        else if (sa->Sclass == SCstruct)
        {
            s = adl_searchClass(id, (Classsym *)sa);
        }
        else
        {
            assert(0);
        }

#if LOG
        if (s)
            printf("\tfound symbol '%s' in scope '%s'\n", s->Sident, s->Sscope ? s->Sscope->Sident : "");
#endif
        if (s && tyfunc(s->Stype->Tty) && s->Sclass != SCtypedef &&
            s->Sclass != SCalias && s->Sclass != SCfuncalias)
        {
#if LOG
            printf("\tadding symbol '%s'\n", s->Sident);
#endif
            if (!list_inlist(symlist, s))
                list_prepend(&symlist, s);
        }
    }

    list_free(&nclist, FPNULL);

    if (!symlist)
        return so;

    if (so && !list_inlist(symlist, so))
        list_prepend(&symlist, so);

    so = (symbol *)list_ptr(symlist);
    if (list_nitems(symlist) == 1)
    {
        list_free(&symlist, FPNULL);
        return so;
    }

    symbol *sadl;

    sadl = symbol_name(so->Sident, SCadl, so->Stype);
    sadl->Spath = symlist;
    return sadl;
}

#endif
