/**
 * Implementation of the
 * $(LINK2 http://www.digitalmars.com/download/freecompiler.html, Digital Mars C/C++ Compiler).
 *
 * Copyright:   Copyright (c) 1994-1998 by Symantec, All Rights Reserved
 *              Copyright (c) 2000-2017 by Digital Mars, All Rights Reserved
 * Authors:     $(LINK2 http://www.digitalmars.com, Walter Bright)
 * License:     Distributed under the Boost Software License, Version 1.0.
 *              http://www.boost.org/LICENSE_1_0.txt
 * Source:      https://github.com/DigitalMars/Compiler/blob/master/dm/src/dmc/rtti.c
 */

// C++ RTTI support

#if !SPP

#include        <stdio.h>
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
#include        "exh.h"

static char __file__[] = __FILE__;      /* for tassert.h                */
#include        "tassert.h"

/*******************************
 * Make sure Type_info was defined properly.
 * Returns:
 *      symbol for class Type_info
 *      NULL error
 */

Classsym *rtti_typeinfo()
{
    static const char rtti_name_ti[] = "Type_info";
    static Classsym *s_rtti_ti;

    if (!s_rtti_ti)
    {   s_rtti_ti = (Classsym *)scope_search(rtti_name_ti,SCTglobal);
        if (!s_rtti_ti || s_rtti_ti->Sclass != SCstruct)
        {
            cpperr(EM_typeinfo_h);              // must #include <typeinfo.h>
            s_rtti_ti = NULL;
        }
    }
    if (s_rtti_ti)
        symbol_debug(s_rtti_ti);
    return s_rtti_ti;
}

/***************************
 * Parse the various new style C++ casts.
 *      static_cast < type-name > ( expression )
 *      const_cast < type-name > ( expression )
 *      reinterpret_cast < type-name > ( expression )
 *      dynamic_cast < type-name > ( expression )
 * Input:
 *      tk      token saying which cast type it is
 *      t       type to cast to
 *      e       expression to be cast
 * Returns:
 *      cast expression
 */

elem *rtti_cast(enum_TK tk,elem *e,type *t)
{   type *et;
    tym_t ty,ety;
    type *tn;
    type *etn;
    elem *efunc;
    list_t arglist;
    symbol *s;
    symbol *v;
    elem *ed;
    elem *ep;
    elem *eflag;
    elem *ep1;
    elem *ep2;
    elem *etmp;
    int flag;
    static symbol *sfunc;

    type_debug(t);
    ty = tybasic(t->Tty);
    elem_debug(e);
    et = e->ET;
    type_debug(et);
    ety = tybasic(et->Tty);

    if (errcnt)
        return cast(e,t);

    switch ((int) tk)
    {
        case TKreinterpret_cast:
            /* CPP98 5.2.10-2 "the reinterpret_cast operator shall not
             * cast away constness."
             */
            if (typtr(ty) && typtr(ety) &&
                et->Tnext->Tty & ~t->Tnext->Tty & mTYconst)
                typerr(EM_no_castaway, et, t);

        case TKstatic_cast:
        case TKconst_cast:
        case_cast:
            e = cast(e,t);
            break;

        case TKdynamic_cast:
            if (!(config.flags3 & CFG3rtti))
            {   cpperr(EM_compileRTTI);                 // compile with -ER
                config.flags3 |= CFG3rtti;
            }

            tn = t->Tnext;
            etn = et->Tnext;
            flag = 0;

            // If t is void*, then e must be a pointer and the value is
            // a pointer to the complete object.
            if (typtr(ty) && tybasic(tn->Tty) == TYvoid)
            {
                if (!typtr(ety))
                {   cpperr(EM_not_pointer);             // must be a pointer
                    goto case_cast;
                }

                // If e is not a polymorphic type, then return a pointer
                // to the static type of the object, which is just e.
                if (!type_struct(etn) || !etn->Ttag->Sstruct->Svptr)
                    break;

                // Call RTL function to convert e to a pointer to the
                // complete object
                goto case_dynamic;
            }


            // Type t must be a pointer or a reference to a defined
            // class or void*.
            if ((!tyref(ty) && !typtr(ty)) ||
                !type_struct(tn) ||
                (template_instantiate_forward(tn->Ttag), s = tn->Ttag)->Stype->Tflags & TFforward)
            {   //cpperr(EM_ptr_to_class);              // must be pointer to class
                typerr(EM_ptr_to_class,t,NULL); // must be pointer to class
                goto case_cast;
            }

            if (ety == TYstruct)
                v = et->Ttag;
            else if ((!tyref(ety) && !typtr(ety)) ||
                !type_struct(etn) ||
                (v = etn->Ttag)->Stype->Tflags & TFforward)
            {   //cpperr(EM_ptr_to_class);              // must be pointer to class
                typerr(EM_ptr_to_class,et,NULL);        // must be pointer to class
                goto case_cast;
            }

            if (!v->Sstruct->Svptr)             // if not a polymorphic type
            {
                // If s is an accessible base class of v, then we perform
                // the cast statically.
                // BUG: we are not checking accessibility here
                if (!c1isbaseofc2(NULL,s,v))
                    cpperr(EM_ptr_to_polymorph);        // must be ptr or ref to polymorphic type
                goto case_cast;
            }

            if (c1isbaseofc2(NULL,v,s) & BCFvirtual)
                flag |= 2;              // v is a virtual base of s

        case_dynamic:
            // Call __rtti_cast(ed,e,et,t,flag)
            if (tyref(ty))
                flag |= 1;
            e = reftostar(e);
            if (!typtr(ety))
                e = el_unat(OPaddr,newpointer(et),e);

            if (!sfunc)
                sfunc = scope_search("__rtti_cast",SCTglobal);
            symbol_debug(sfunc);
            efunc = el_var(sfunc);

            // Get pointer to dynamic type of e
            e = poptelem(e);
            e = exp2_copytotemp(e);
            etmp = e->E2;

            ep = el_copytree(etmp);
            ep = el_settype(ep,tspvoid);

            ed = el_copytree(etmp);
            ed = el_unat(OPind,ed->ET->Tnext,ed);
            ed = rtti_typeid(NULL,ed);
            ed = el_unat(OPaddr,newpointer(ed->ET),ed);

            ep1 = el_ptr(init_typeinfo_data(e->ET->Tnext));
            ep2 = el_ptr(init_typeinfo_data(tn));

            arglist = list_build(ed,
                ep,
                ep1,
                ep2,
                el_longt(tsint,flag),
                NULL);
            ep = xfunccall(efunc,NULL,NULL,arglist);

            if (tyref(ty))
            {   ep = reftostart(ep,t);
                // Construct (e,ep)
                e = el_bint(OPcomma,ep->ET,e,ep);
            }
            else
            {   el_settype(ep,t);
                // Construct (e ? ep : NULL)
                e = el_bint(OPcond,t,e,el_bint(OPcolon,t,ep,el_longt(t,0)));
            }
            break;

        default:
            assert(0);
    }
    //elem_print(e);
    return e;
}

/*************************
 * A typeid(expression) or typeid(type-name) has been parsed.
 * Input:
 *      t       type-name, NULL if there wasn't one
 *      e       expression, NULL if there wasn't one
 * Output:
 *      t & e are free'd
 * Returns:
 *      elem consisting of the type_info object
 */

elem *rtti_typeid(type *t,elem *e)
{   symbol *s;
    symbol *svptr;
    type *tref;
    Classsym *srtti;
    Classsym *stag;
    struct_t *st;

    //dbg_printf("rtti_typeid(t = %p, e = %p)\n",t,e);
    //if (t) type_print(t);
    //if (e) elem_print(e);

    if (t) type_debug(t);
    if (e) elem_debug(e);
    assert(!t || !e);

    srtti = rtti_typeinfo();
    if (!srtti)                         // if error
    {   el_free(e);
        type_free(t);
        e = el_calloc();
        e->Eoper = OPconst;
        el_settype(e,tserr);
        return e;
    }

    // Create type that is a "const Type_info&"
    tref = newref(srtti->Stype);
    tref->Tty |= mTYconst;

    if (e)
    {   e = reftostar(e);
        e = poptelem(e);
    }

    // If e is a polymorphic type, return a reference to the complete
    // object represented by that type.
    if (e &&
        e->Eoper != OPvar &&
        type_struct(e->ET) &&
        (svptr = (stag = e->ET->Ttag)->Sstruct->Svptr) != 0)
    {   elem *emos;

        if (!(config.flags3 & CFG3rtti))
        {   cpperr(EM_compileRTTI);                     // compile with -ER
            config.flags3 |= CFG3rtti;
        }

        // *(e->vptr - 1)

        symbol_debug(svptr);
        emos = el_longt(tsint,svptr->Smemoff);
        // Account for offset due to reuse of primary base class vtbl
        st = stag->Sstruct;
        if (st->Sprimary && st->Sprimary->BCbase->Sstruct->Svptr == st->Svptr)
            emos->EV.sp.Voffset = st->Sprimary->BCoffset;
        e = el_unat(OPaddr,newpointer(e->ET),e);
        t = type_allocn(st->ptrtype,svptr->Stype); // match pointer type of ethis
        e = el_bint(OPadd,t,e,emos);               // ethis + mos
        e = el_unat(OPind,svptr->Stype,e);      // *(ethis + mos)

        e = el_bint(OPadd,e->ET,e,el_longt(tsint,-tysize(st->ptrtype)));
        e = el_unat(OPind,tref,e);
    }
    else
    {
        type *ti;

        ti = t ? t : e->ET;

        // Create an instance of type Type_info
        s = init_typeinfo(ti);
        assert(s);
        symbol_debug(s);

        type_free(t);
        el_free(e);

        // e will be a const reference to s
        e = el_ptr(s);
        el_settype(e,tref);
        elem_debug(e);
    }

    return reftostar(e);
}

#endif
