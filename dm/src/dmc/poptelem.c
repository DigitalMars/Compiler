/**
 * Compiler implementation of the
 * $(LINK2 http://www.dlang.org, D programming language).
 *
 * Copyright:   Copyright (C) 1985-1998 by Symantec
 *              Copyright (c) 2000-2017 by Digital Mars, All Rights Reserved
 * Authors:     $(LINK2 http://www.digitalmars.com, Walter Bright)
 * License:     Distributed under the Boost Software License, Version 1.0.
 *              http://www.boost.org/LICENSE_1_0.txt
 */

#if !SPP

#include        <math.h>
#include        <stdlib.h>
#include        <stdio.h>
#include        <string.h>
#include        <float.h>
#include        <time.h>

#include        "cc.h"
#include        "oper.h"                /* OPxxxx definitions           */
#include        "global.h"
#include        "el.h"
#include        "type.h"


static char __file__[] = __FILE__;      /* for tassert.h                */
#include        "tassert.h"

elem * evalu8(elem *, goal_t);
elem *selecte1(elem *e,type *t);


/******************************
 * Constant fold expression tree.
 * Calculate &symbol and &*e1 if we can.
 */

#if SCPP

/* When this !=0, we do constant folding on floating point constants
 * even if they raise overflow, underflow, invalid, etc. exceptions.
 */

extern int ignore_exceptions;

static elem *poptelemi(elem *e, bool resolve_sizeof, bool ignore_exceptions);

elem *poptelem2(elem *e)
{
    // Same as poptelem(), but we ignore floating point exceptions
    return poptelemi(e, false, true);
}

elem *poptelem3(elem *e)
{
    return poptelemi(e, true, false);
}

elem *poptelem4(elem *e)
{
    return poptelemi(e, true, true);
}

elem *poptelem(elem *e)
{
    return poptelemi(e, false, false);
}

static elem *poptelemi(elem *e, bool resolve_sizeof, bool ignore_exceptions)
{
    elem *e1,*e2;

    //dbg_printf("poptelemi(e = %p)\n", e); elem_print(e);
#ifdef DEBUG
    assert(PARSER);
    assert(e && e->ET);
//    static int xxx; if (++xxx == 1000) *(char *)0 = 0;
#endif
    elem_debug(e);
    type_debug(e->ET);

    unsigned op = e->Eoper;

#ifdef DEBUG
    if (OTunary(op))
        assert(!e->E2 || op == OPinfo);
#endif

    switch (op)
    {
        case OPvar:
            if (CPP && e->EV.sp.Vsym->Sflags & SFLvalue)
                el_toconst(e);
            break;

        case OPsizeof:
            if (resolve_sizeof)
            {
                e->Eoper = OPconst;
                e->EV.Vlong = type_size(e->EV.sp.Vsym->Stype);
            }
            break;

        case OPconst:
        case OPrelconst:
        case OPstring:
            break;

        case OPaddr:
            e1 = e->E1;
            if (e1->Eoper == OPvar)
                goto L3;
            e->E1 = e1 = poptelemi(e1, resolve_sizeof, ignore_exceptions);
            if (e1->Eoper == OPind)     /* if &*exp                     */
            {
            L6:
                type *t = e->ET;
                e1->E1 = cast(e1->E1,t);
                e = selecte1(selecte1(e,t),t);
            }
            else if (e1->Eoper == OPvar)
            {   /* convert &var to relconst     */
            L3:
                e = selecte1(e,e->ET);
                e->Eoper = OPrelconst;
                // If this is an address of a function template,
                // try to expand the template if it's got an explicit
                // parameter list.
                if (e->PEFflags & PEFtemplate_id)
                {
                    Symbol *s = e->EV.sp.Vsym;
                    s = cpp_lookformatch(s, NULL, NULL,NULL,NULL,NULL,
                            e->EV.sp.spu.Vtal, 1|8, NULL, NULL);
                    if (s)
                    {
                        e->EV.sp.Vsym = s;
                        param_free(&e->EV.sp.spu.Vtal);
                        e->PEFflags &= ~PEFtemplate_id;
                        type_settype(&e->ET, newpointer(s->Stype));
                    }
                }
            }
            break;
        case OPind:
            e->E1 = e1 = poptelemi(e->E1, resolve_sizeof, ignore_exceptions);
            if (e1->Eoper == OPrelconst)
            {   /* convert *(&var) to var       */

                e = selecte1(e,e->ET);
                e->Eoper = OPvar;
            }
            break;
        case OPnp_fp:
            e->E1 = e1 = poptelemi(e->E1, resolve_sizeof, ignore_exceptions);
            // If casting a non-NULL constant pointer
            if (e1->Eoper == OPconst && el_tolong(e1) != 0)
                break;
            goto L5;
        case OPoffset:
            e->E1 = e1 = poptelemi(e->E1, resolve_sizeof, ignore_exceptions);
            if (e1->Eoper == OPnp_fp)
                goto L6;
            goto L5;
        case OP32_16:
            e->E1 = e1 = poptelemi(e->E1, resolve_sizeof, ignore_exceptions);
        L5:
            if (e1->Eoper == OPrelconst || e1->Eoper == OPstring)
                e = selecte1(e,e->ET);
            else
                goto eval;
            break;
        case OPandand:
            e->E1 = e1 = poptelemi(e->E1, resolve_sizeof, ignore_exceptions);
            if (iffalse(e1))
                goto L2;
            else
                goto def;
        case OPoror:
            e->E1 = e1 = poptelemi(e->E1, resolve_sizeof, ignore_exceptions);
            if (iftrue(e1))
            {
            L2: el_free(e->E2);
                e->E2 = NULL;
                e->Eoper = OPbool;
                e = poptelemi(e, resolve_sizeof, ignore_exceptions);
            }
            else
                goto def;
            break;
        case OPcond:
            e->E1 = e1 = poptelemi(e->E1, resolve_sizeof, ignore_exceptions);
            if (e1->Eoper == OPconst)
            {
                e2 = e->E2;
                type_free(e->ET);
                if (boolres(e1))
                {   el_copy(e,e2->E1);
                    e2->E1->Eoper = OPunde;
                    e2->E1->ET = NULL;
                    e2->E1->E1 = NULL;
                    e2->E1->E2 = NULL;
                    el_free(e2->E1);
                    e2->E1 = NULL;
                }
                else
                {   el_copy(e,e2->E2);
                    e2->E2->Eoper = OPunde;
                    e2->E2->ET = NULL;
                    e2->E2->E1 = NULL;
                    e2->E2->E2 = NULL;
                    el_free(e2->E2);
                    e2->E2 = NULL;
                }
                el_free(e2);
                el_free(e1);
                e = poptelemi(e, resolve_sizeof, ignore_exceptions);
            }
            else
                goto def;
            break;
        case OPadd:
            e->E1 = e1 = poptelemi(e->E1, resolve_sizeof, ignore_exceptions);
            e->E2 = e2 = poptelemi(e->E2, resolve_sizeof, ignore_exceptions);
            if (e1->Eoper == OPconst)
            {   /* swap leaves */
                e->E1 = e2;
                e2 = e->E2 = e1;
                e1 = e->E1;
            }
            goto L4;
        case OPmin:
            e->E1 = e1 = poptelemi(e->E1, resolve_sizeof, ignore_exceptions);
            e->E2 = e2 = poptelemi(e->E2, resolve_sizeof, ignore_exceptions);
        L4:
            if (e1->Eoper == OPrelconst || e1->Eoper == OPstring)
            {
                if (e2->Eoper == OPconst)
                {   targ_int i = e2->EV.Vint;

                    if (i && e1->Eoper == OPrelconst && e1->EV.sp.Vsym->Sfl == FLgot)
                        break;
                    if (e->Eoper == OPmin)
                        i = -i;
                    e1->EV.sp.Voffset += i;
                    e = selecte1(e,e->ET);
                    break;
                }
            }
            goto eval;
        default:
            if (OTleaf(op))
                goto ret;
            e->E1 = poptelemi(e->E1, resolve_sizeof, ignore_exceptions);
        def:
            if (OTbinary(op))           // if binary node
            {
                e->E2 = poptelemi(e->E2, resolve_sizeof, ignore_exceptions);
            }
        eval:
            ::ignore_exceptions = ignore_exceptions;
            e = evalu8(e, GOALvalue);
            break;
    }
ret:
    return e;
}

/********************
 * Select E1 leaf of e, and give it type t.
 */

elem *selecte1(elem *e,type *t)
{
    elem_debug(e);
    assert(EOP(e));
    elem *e1 = e->E1;
    el_settype(e1,t);
    e->E1 = NULL;
    el_free(e);
    return e1;
}

#endif

#endif /* !SPP */
