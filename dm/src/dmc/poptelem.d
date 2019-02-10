/**
 * Implementation of the
 * $(LINK2 http://www.digitalmars.com/download/freecompiler.html, Digital Mars C/C++ Compiler).
 *
 * Copyright:   Copyright (C) 1985-1998 by Symantec
 *              Copyright (c) 2000-2017 by Digital Mars, All Rights Reserved
 * Authors:     $(LINK2 http://www.digitalmars.com, Walter Bright)
 * License:     Distributed under the Boost Software License, Version 1.0.
 *              http://www.boost.org/LICENSE_1_0.txt
 * Source:      https://github.com/DigitalMars/Compiler/blob/master/dm/src/dmc/poptelem.d
 */

version (SCPP)
    version = COMPILE;

version (HTOD)
    version = COMPILE;

version (COMPILE)
{
import core.stdc.stdio;

import dmd.backend.cdef;
import dmd.backend.cc;
import dmd.backend.el;
import dmd.backend.global;
import dmd.backend.oper;
import dmd.backend.ty;
import dmd.backend.type;

import cpp;
import parser;

extern (C++):

elem * evalu8(elem *, goal_t);
elem *selecte1(elem *e,type *t);


/******************************
 * Constant fold expression tree.
 * Calculate &symbol and &*e1 if we can.
 */

/* When this !=0, we do constant folding on floating point constants
 * even if they raise overflow, underflow, invalid, etc. exceptions.
 */

__gshared int ignore_exceptions;

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

private elem *poptelemi(elem *e, bool resolve_sizeof, bool ignore_exceptions)
{
    elem *e1;
    elem *e2;

    //printf("poptelemi(e = %p)\n", e); elem_print(e);
debug
{
    assert(PARSER);
    assert(e && e.ET);
//    static int xxx; if (++xxx == 1000) *(char *)0 = 0;
}
    elem_debug(e);
    type_debug(e.ET);

    const OPER op = e.Eoper;

debug
{
    if (OTunary(op))
        assert(!e.EV.E2 || op == OPinfo);
}

    switch (op)
    {
        case OPvar:
            if (CPP && e.EV.Vsym.Sflags & SFLvalue)
                el_toconst(e);
            break;

        case OPsizeof:
            if (resolve_sizeof)
            {
                e.Eoper = OPconst;
                e.EV.Vlong = type_size(e.EV.Vsym.Stype);
            }
            break;

        case OPconst:
        case OPrelconst:
        case OPstring:
            break;

        case OPaddr:
            e1 = e.EV.E1;
            if (e1.Eoper == OPvar)
                goto L3;
            e.EV.E1 = e1 = poptelemi(e1, resolve_sizeof, ignore_exceptions);
            if (e1.Eoper == OPind)     /* if &*exp                     */
            {
            L6:
                type *t = e.ET;
                e1.EV.E1 = _cast(e1.EV.E1,t);
                e = selecte1(selecte1(e,t),t);
            }
            else if (e1.Eoper == OPvar)
            {   /* convert &var to relconst     */
            L3:
                e = selecte1(e,e.ET);
                e.Eoper = OPrelconst;
                // If this is an address of a function template,
                // try to expand the template if it's got an explicit
                // parameter list.
                if (e.PEFflags & PEFtemplate_id)
                {
                    Symbol *s = e.EV.Vsym;
                    s = cpp_lookformatch(s, null, null,null,null,null,
                            e.EV.Vtal, 1|8, null, null);
                    if (s)
                    {
                        e.EV.Vsym = s;
                        param_free(&e.EV.Vtal);
                        e.PEFflags &= ~PEFtemplate_id;
                        type_settype(&e.ET, newpointer(s.Stype));
                    }
                }
            }
            break;
        case OPind:
            e.EV.E1 = e1 = poptelemi(e.EV.E1, resolve_sizeof, ignore_exceptions);
            if (e1.Eoper == OPrelconst)
            {   /* convert *(&var) to var       */

                e = selecte1(e,e.ET);
                e.Eoper = OPvar;
            }
            break;
        case OPnp_fp:
            e.EV.E1 = e1 = poptelemi(e.EV.E1, resolve_sizeof, ignore_exceptions);
            // If casting a non-null constant pointer
            if (e1.Eoper == OPconst && el_tolong(e1) != 0)
                break;
            goto L5;
        case OPoffset:
            e.EV.E1 = e1 = poptelemi(e.EV.E1, resolve_sizeof, ignore_exceptions);
            if (e1.Eoper == OPnp_fp)
                goto L6;
            goto L5;
        case OP32_16:
            e.EV.E1 = e1 = poptelemi(e.EV.E1, resolve_sizeof, ignore_exceptions);
        L5:
            if (e1.Eoper == OPrelconst || e1.Eoper == OPstring)
                e = selecte1(e,e.ET);
            else
                goto eval;
            break;
        case OPandand:
            e.EV.E1 = e1 = poptelemi(e.EV.E1, resolve_sizeof, ignore_exceptions);
            if (iffalse(e1))
                goto L2;
            else
                goto def;
        case OPoror:
            e.EV.E1 = e1 = poptelemi(e.EV.E1, resolve_sizeof, ignore_exceptions);
            if (iftrue(e1))
            {
            L2: el_free(e.EV.E2);
                e.EV.E2 = null;
                e.Eoper = OPbool;
                e = poptelemi(e, resolve_sizeof, ignore_exceptions);
            }
            else
                goto def;
            break;
        case OPcond:
            e.EV.E1 = e1 = poptelemi(e.EV.E1, resolve_sizeof, ignore_exceptions);
            if (e1.Eoper == OPconst)
            {
                e2 = e.EV.E2;
                type_free(e.ET);
                if (boolres(e1))
                {   el_copy(e,e2.EV.E1);
                    e2.EV.E1.Eoper = OPunde;
                    e2.EV.E1.ET = null;
                    e2.EV.E1.EV.E1 = null;
                    e2.EV.E1.EV.E2 = null;
                    el_free(e2.EV.E1);
                    e2.EV.E1 = null;
                }
                else
                {   el_copy(e,e2.EV.E2);
                    e2.EV.E2.Eoper = OPunde;
                    e2.EV.E2.ET = null;
                    e2.EV.E2.EV.E1 = null;
                    e2.EV.E2.EV.E2 = null;
                    el_free(e2.EV.E2);
                    e2.EV.E2 = null;
                }
                el_free(e2);
                el_free(e1);
                e = poptelemi(e, resolve_sizeof, ignore_exceptions);
            }
            else
                goto def;
            break;
        case OPadd:
            e.EV.E1 = e1 = poptelemi(e.EV.E1, resolve_sizeof, ignore_exceptions);
            e.EV.E2 = e2 = poptelemi(e.EV.E2, resolve_sizeof, ignore_exceptions);
            if (e1.Eoper == OPconst)
            {   /* swap leaves */
                e.EV.E1 = e2;
                e2 = e.EV.E2 = e1;
                e1 = e.EV.E1;
            }
            goto L4;
        case OPmin:
            e.EV.E1 = e1 = poptelemi(e.EV.E1, resolve_sizeof, ignore_exceptions);
            e.EV.E2 = e2 = poptelemi(e.EV.E2, resolve_sizeof, ignore_exceptions);
        L4:
            if (e1.Eoper == OPrelconst || e1.Eoper == OPstring)
            {
                if (e2.Eoper == OPconst)
                {   targ_int i = e2.EV.Vint;

                    if (i && e1.Eoper == OPrelconst && e1.EV.Vsym.Sfl == FLgot)
                        break;
                    if (e.Eoper == OPmin)
                        i = -i;
                    e1.EV.Voffset += i;
                    e = selecte1(e,e.ET);
                    break;
                }
            }
            goto eval;
        default:
            if (OTleaf(op))
                goto ret;
            e.EV.E1 = poptelemi(e.EV.E1, resolve_sizeof, ignore_exceptions);
        def:
            if (OTbinary(op))           // if binary node
            {
                e.EV.E2 = poptelemi(e.EV.E2, resolve_sizeof, ignore_exceptions);
            }
        eval:
            goal_t goal = GOALvalue;
            if (ignore_exceptions)
                goal |= GOALignore_exceptions;
            e = evalu8(e, goal);
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
    assert(!OTleaf(e.Eoper));
    elem *e1 = e.EV.E1;
    el_settype(e1,t);
    e.EV.E1 = null;
    el_free(e);
    return e1;
}

}
