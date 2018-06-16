/**
 * Implementation of the
 * $(LINK2 http://www.digitalmars.com/download/freecompiler.html, Digital Mars C/C++ Compiler).
 *
 * Copyright:   Copyright (c) 1987-1998 by Symantec, All Rights Reserved
 *              Copyright (c) 2000-2017 by Digital Mars, All Rights Reserved
 * Authors:     $(LINK2 http://www.digitalmars.com, Walter Bright)
 * License:     Distributed under the Boost Software License, Version 1.0.
 *              http://www.boost.org/LICENSE_1_0.txt
 * Source:      https://github.com/DigitalMars/Compiler/blob/master/dm/src/dmc/dinline.d
 */

// C++ specific routines

extern (C++):

version (SCPP)
{

import core.stdc.stdio;
import core.stdc.ctype;
import core.stdc.string;
import core.stdc.stdlib;

import scopeh;

import dmd.backend.cdef;
import dmd.backend.cc;
import dmd.backend.el;
import dmd.backend.global;
import dmd.backend.oper;
import dmd.backend.ty;
import dmd.backend.type;

import dmd.backend.dlist;

void n2_instantiate_memfunc(Symbol* s);
void nwc_mustwrite(Symbol*);
void queue_func(Symbol*);
elem* cpp_destructor(type* tclass, elem* eptr, elem* enelems, int dtorflag);

extern __gshared list_t cpp_stidtors;

enum DTORmostderived = 4;


/**********************************
 * Return !=0 if function can be inline'd.
 */

bool inline_possible(Symbol *sfunc)
{
    //dbg_printf("inline_possible(%s)\n",sfunc.Sident);
    auto f = sfunc.Sfunc;
    auto t = sfunc.Stype;
    assert(f && tyfunc(t.Tty) && SymInline(sfunc));

    bool result = false;
    if (!(config.flags & CFGnoinlines) && /* if inlining is turned on   */
        /* Cannot inline varargs or unprototyped functions      */
        (t.Tflags & (TFfixed | TFprototype)) == (TFfixed | TFprototype) &&
        !(t.Tty & mTYimport)           // do not inline imported functions
       )
    {
        auto b = f.Fstartblock;
        assert(b);
        switch (b.BC)
        {   case BCret:
                if (tybasic(t.Tnext.Tty) != TYvoid
                    && !(f.Fflags & (Fctor | Fdtor | Finvariant))
                   )
                {   // Message about no return value
                    // should already have been generated
                    break;
                }
                goto case BCretexp;

            case BCretexp:
                result = true;
                break;

            default:
                break;
        }
    }
    //dbg_printf("returns: %d\n",result);
    return result;
}

/**************************
 * Examine all of the function calls in sfunc, and inline-expand
 * any that can be.
 */

void inline_do(Symbol *sfunc)
{
    //dbg_printf("inline_do(%s)\n",prettyident(sfunc));
    //symbol_debug(sfunc);
    func_t* f = sfunc.Sfunc;
    assert(f && tyfunc(sfunc.Stype.Tty));
    // BUG: flag not set right in CPP
    if (CPP || f.Fflags3 & Fdoinline)  // if any inline functions called
    {
        f.Fflags |= Finlinenest;
        foreach (b; BlockRange(startblock))
            if (b.Belem)
            {
                //elem_print(b.Belem);
                b.Belem = inline_do_walk(b.Belem);
            }
        if (eecontext.EEelem)
        {
            const marksi = globsym.top;
            eecontext.EEelem = inline_do_walk(eecontext.EEelem);
            eecontext_convs(marksi);
        }
        f.Fflags &= ~Finlinenest;
    }
}

private elem* inline_do_walk(elem *e)
{
    //dbg_printf("inline_do_walk(%p)\n",e);
    //elem_debug(e);
    uint op = e.Eoper;
    if (OTbinary(op))
    {
        e.EV.E1 = inline_do_walk(e.EV.E1);
        e.EV.E2 = inline_do_walk(e.EV.E2);
        if (op == OPcall)
            goto L1;
    }
    else if (OTunary(op))
    {
        if (op == OPstrctor)
        {
            elem* e1 = e.EV.E1;
            while (e1.Eoper == OPcomma)
            {   e1.EV.E1 = inline_do_walk(e1.EV.E1);
                e1 = e1.EV.E2;
            }
            if (e1.Eoper == OPcall && e1.EV.E1.Eoper == OPvar)
            {   // Never inline expand this function

                // But do expand templates
                Symbol* s = e1.EV.E1.EV.Vsym;
                if (tyfunc(s.ty()))
                {
                    // This function might be an inline template function that was
                    // never parsed. If so, parse it now.
                    if (s.Sfunc.Fbody)
                    {
                        n2_instantiate_memfunc(s);
                    }
                }
            }
            else
                e1.EV.E1 = inline_do_walk(e1.EV.E1);
            e1.EV.E2 = inline_do_walk(e1.EV.E2);
        }
        else
        {
            e.EV.E1 = inline_do_walk(e.EV.E1);
            if (op == OPucall)
            {
              L1:
                e = inline_ifcan(e);
            }
        }
    }
    else /* leaf */
    {
        // If deferred allocation of variable, allocate it now.
        // The deferred allocations are done by cpp_initctor().
        if (CPP &&
            (op == OPvar || op == OPrelconst))
        {
            Symbol* s = e.EV.Vsym;
            if (s.Sclass == SCauto &&
                s.Ssymnum == -1)
            {   //dbg_printf("Deferred allocation of %p\n",s);
                symbol_add(s);

                if (tybasic(s.Stype.Tty) == TYstruct &&
                    s.Stype.Ttag.Sstruct.Sdtor &&
                    !(s.Sflags & SFLnodtor))
                {
                    elem* eptr = el_ptr(s);
                    elem* edtor = cpp_destructor(s.Stype,eptr,null,DTORmostderived);
                    assert(edtor);
                    edtor = inline_do_walk(edtor);
                    list_append(&cpp_stidtors,edtor);
                }
            }
            if (tyfunc(s.ty()))
            {
                // This function might be an inline template function that was
                // never parsed. If so, parse it now.
                if (s.Sfunc.Fbody)
                {
                    n2_instantiate_memfunc(s);
                }
            }
        }
    }
    return e;
}

/**********************************
 * Inline-expand a function call if it can be.
 * Input:
 *      e       OPcall or OPucall elem
 * Returns:
 *      replacement tree.
 */

private elem* inline_ifcan(elem *e)
{

    //elem_debug(e);
    assert(e && (e.Eoper == OPcall || e.Eoper == OPucall));

    {   if (e.EV.E1.Eoper == OPind && e.EV.E1.EV.E1.Eoper == OPaddr)
            e.EV.E1 = poptelem(e.EV.E1);
        if (e.EV.E1.Eoper != OPvar)
            return e;

        // This is an explicit function call (not through a pointer)
        Symbol* sfunc = e.EV.E1.EV.Vsym;
        //dbg_printf("inline_ifcan: %s, class = %d\n",
        //      prettyident(sfunc),sfunc.Sclass);

        // sfunc may not be a function due to user's clever casting
        if (!tyfunc(sfunc.Stype.Tty))
            return e;

        // This function might be an inline template function that was
        // never parsed. If so, parse it now.
        if (sfunc.Sfunc.Fbody)
        {
            n2_instantiate_memfunc(sfunc);
        }

        /* If forward referencing an inline function, we'll have to     */
        /* write out the function when it eventually is defined */
        if (sfunc.Sfunc.Fstartblock == null)
            nwc_mustwrite(sfunc);
        else if (SymInline(sfunc))
        {   func_t *f = sfunc.Sfunc;

            /* Check to see if we inline expand the function, or queue  */
            /* it to be output.                                         */
            if ((f.Fflags & (Finline | Finlinenest)) == Finline)
                e = inline_expand(e,sfunc);
            else
                queue_func(sfunc);
        }
    }
    return e;
}

/**********************************
 * Inline expand a function.
 * Input:
 *      e       The function call tree
 *      sfunc   Function Symbol
 * Output:
 *      e is free'd
 * Returns:
 *      new tree
 */

private __gshared SYMIDX sistart;

private elem* inline_expand(elem *e,Symbol *sfunc)
{
    //dbg_printf("inline_expand(e = %p, func %p = '%s')\n",
    //  e,sfunc,prettyident(sfunc));
    //elem_debug(e);
    //symbol_debug(sfunc);
    func_t* f = sfunc.Sfunc;

    /* Create duplicate of function elems       */
    assert(f.Fstartblock);
    elem* ec = el_copytree(f.Fstartblock.Belem);

    // Declare all of sfunc's local symbols as symbols in globsym
    sistart = globsym.top;                      // where func's local symbols start
    for (int si = 0; si < f.Flocsym.top; si++)
    {
        Symbol* s = f.Flocsym.tab[si];
        assert(s);
        //if (!s)
        //    continue;
        //symbol_debug(s);
        auto sc = s.Sclass;
        switch (sc)
        {
            case SCparameter:
            case SCfastpar:
                sc = SCauto;
                goto L1;
            case SCregpar:
                sc = SCregister;
                goto L1;
            case SCregister:
            case SCauto:
            case SCpseudo:
            L1:
            {
                Symbol* snew = symbol_copy(s);
                snew.Sclass = sc;
                snew.Sflags |= SFLfree;
                snew.Srange = null;
                s.Sflags |= SFLreplace;
                s.Ssymnum = symbol_add(snew);
                break;
            }
            case SCglobal:
            case SCstatic:
                break;
            default:
                debug symbol_print(s);
                assert(0);
        }
    }

    // Walk the copied tree, to replace the references to the old
    // variables with references to the new.
    if (ec)
    {
        inline_expandwalk(ec);
        if (config.flags3 & CFG3eh &&
            (eecontext.EEin ||
             f.Fflags3 & Fmark ||      // if mark/release around function expansion
             f.Fflags & Fctor))
        {
            elem* em = el_calloc();
            em.Eoper = OPmark;
            el_settype(em,tstypes[TYvoid]);
            ec = el_bint(OPinfo,ec.ET,em,ec);
        }
    }

    // Initialize all the parameter
    // variables with the argument list
    if (e.Eoper == OPcall)
    {
        int n = 0;

        elem* eargs = inline_args(e.EV.E2,&n);
        ec = el_combine(eargs,ec);
    }

    if (ec)
    {
        ec.Esrcpos = e.Esrcpos;         // save line information
        f.Fflags |= Finlinenest;        // prevent recursive inlining
        ec = inline_do_walk(ec);        // look for more cases
        f.Fflags &= ~Finlinenest;
    }
    else
        ec = el_longt(tstypes[TYint],0);
    el_free(e);                         // dump function call
    return ec;
}

/****************************
 * Evaluate the argument list, putting in initialization statements to the
 * local parameters. If there are more arguments than parameters,
 * evaluate the remaining arguments for side effects only.
 * Input:
 *      *pn     this is the nth argument (starts at 0)
 * Output:
 *      *pn     incremented once for each parameter
 */

private elem* inline_args(elem *e,int *pn)
{
    elem* ecopy;

    //elem_debug(e);
    if (e.Eoper == OPparam)
    {
        elem* e1 = inline_args(e.EV.E2,pn);
        elem* e2 = inline_args(e.EV.E1,pn);
        ecopy = el_combine(e1,e2);
    }
    else
    {
        if (e.Eoper == OPstrpar)
            e = e.EV.E1;

        auto n = (*pn)++;
        // Find the nth parameter in the symbol table
        for (auto si = sistart; 1; si++)
        {
            if (si == globsym.top)              // for ... parameters
            {   ecopy = el_copytree(e);
                break;
            }
            Symbol* s = globsym.tab[si];
            // SCregpar was turned into SCregister, SCparameter to SCauto
            if (s.Sclass == SCregister || s.Sclass == SCauto)
            {   if (n == 0)
                {
                    if (e.Eoper == OPstrctor)
                    {
                        ecopy = el_copytree(e.EV.E1);     // skip the OPstrctor
                        e = ecopy;
                        //while (e.Eoper == OPcomma)
                        //    e = e.EV.E2;
                        debug
                        {
                            if (e.Eoper != OPcall && e.Eoper != OPcond)
                                elem_print(e);
                        }
                        assert(e.Eoper == OPcall || e.Eoper == OPcond || e.Eoper == OPinfo);
                        exp2_setstrthis(e,s,0,ecopy.ET);
                    }
                    else
                    {
                        elem* evar = el_var(s);
                        ecopy = el_bint(OPeq,e.ET,evar,el_copytree(e));
                        // If struct copy
                        if (tybasic(ecopy.ET.Tty) == TYstruct)
                        {   ecopy.Eoper = OPstreq;
                        }
                        el_settype(evar,ecopy.ET);
                    }
                    break;
                }
                n--;
            }
        }
    }
    return ecopy;
}

/*********************************
 * Replace references to old symbols with references to copied symbols.
 */

private void inline_expandwalk(elem *e)
{
    while (1)
    {
        assert(e);
        //elem_debug(e);
        //dbg_printf("inline_expandwalk(%p) ",e);WROP(e.Eoper);dbg_printf("\n");
        // the debugger falls over on debugging inlines
        if (configv.addlinenumbers)
            e.Esrcpos.Slinnum = 0;             // suppress debug info for inlines
        if (!OTleaf(e.Eoper))
        {
            if (OTbinary(e.Eoper))
                inline_expandwalk(e.EV.E2);
            else
                assert(!e.EV.E2);
            e = e.EV.E1;
        }
        else
        {
            if (e.Eoper == OPvar || e.Eoper == OPrelconst)
            {
                Symbol *s = e.EV.Vsym;

                if (s.Sflags & SFLreplace)
                    e.EV.Vsym = globsym.tab[s.Ssymnum];
            }
            break;
        }
    }
}


}
version (HTOD)
{

struct Symbol { }
bool inline_possible(Symbol *sfunc) { return false; }
void inline_do(Symbol *sfunc) { }

}

