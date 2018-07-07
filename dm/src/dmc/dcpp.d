/**
 * Implementation of the
 * $(LINK2 http://www.digitalmars.com/download/freecompiler.html, Digital Mars C/C++ Compiler).
 *
 * Copyright:   Copyright (c) 1987-1998 by Symantec, All Rights Reserved
 *              Copyright (c) 2000-2017 by Digital Mars, All Rights Reserved
 * Authors:     $(LINK2 http://www.digitalmars.com, Walter Bright)
 * License:     Distributed under the Boost Software License, Version 1.0.
 *              http://www.boost.org/LICENSE_1_0.txt
 * Source:      https://github.com/DigitalMars/Compiler/blob/master/dm/src/dmc/dcpp.d
 */

// C++ specific routines

module dcpp;

version (SPP)
{
}
else
{
import core.stdc.stdio;
import core.stdc.string;
import core.stdc.stdlib;

import dmd.backend.cdef;
import dmd.backend.cc;
import dmd.backend.cgcv;
import dmd.backend.dt;
import dmd.backend.el;
import dmd.backend.global;
import dmd.backend.iasm;
import dmd.backend.obj;
import dmd.backend.oper;
import dmd.backend.outbuf;
import dmd.backend.ty;
import dmd.backend.type;

import dmd.backend.dlist;
import dmd.backend.memh;

import cpp;
import dtoken;
import eh;
import msgs2;
import parser;
import scopeh;
import dspeller;


extern (C++):

alias dbg_printf = printf;
alias MEM_PH_MALLOC = mem_malloc;
alias MEM_PH_FREE = mem_free;
alias MEM_PARF_MALLOC = mem_malloc;
alias MEM_PARF_CALLOC = mem_calloc;
alias MEM_PARF_REALLOC = mem_realloc;
alias MEM_PARF_FREE = mem_free;
alias MEM_PARF_STRDUP = mem_strdup;


__gshared
{
/* List of elems which are the constructor and destructor calls to make */
list_t constructor_list;         // for _STIxxxx
list_t destructor_list;          // for _STDxxxx

list_t cpp_stidtors;            /* auto destructors that go in _STIxxxx */

/* Special predefined functions */
private Symbol* s_vec_new,s_vec_ctor,s_vec_cpct,s_vec_delete;
Symbol *s_vec_dtor;
Symbol *s_vec_invariant;
private Symbol *s_fatexit;
private type *t_pctor;           /* type for pointer to constructor      */
private type *t_pdtor;           // type for pointer to destructor (and invariant)
Symbol *s_mptr;
Symbol *s_genthunk;

// From mangle.c
extern OPTABLE[57] oparray;

/* Names for special variables  */

char[7] cpp_name_free        = "__free";
char[5] cpp_name_this        = "this";
char[10] cpp_name_none       = "__unnamed";
char[12] cpp_name_initvbases = "$initVBases";
char[12] cpp_name_invariant  = "__invariant";

/***********************************
 * Array of linked lists of function symbols. Each function
 * is an overloaded version of that operator.
 */

Symbol*[OPMAX] cpp_operfuncs;

/* Bit array for if there are namespace operator overloads */
uint[(OPMAX + 31) / 32] cpp_operfuncs_nspace;
}

private __gshared int cpp_usertypecmp_nest;


/****************************************
 * Check to see if s is 'visible' at this point or not.
 * Returns:
 *      0       not visible
 *      1       visible
 */

private int checkSequence(Symbol *s)
{
    if (s.Ssequence > pstate.STmaxsequence &&
        config.flags4 & CFG4dependent &&
        (!s.Sscope || s.Sscope.Sclass == SCnamespace))
        return 0;
    return 1;
}


/***********************************
 * Match levels.
 * Non-zero means some sort of match.
 * A higher number means a better match.
 */

int Match__cmp(ref Match m1, ref Match m2)
{   int result;

version (none)
{
    printf("Match::cmp()\n");
    printf("m1.m   = %x, m2.m   = %x\n", m1.m, m2.m);
    printf("m1.m2  = %x, m2.m2  = %x\n", m1.m2, m2.m2);
    printf("m1.s   = %p, m2.s   = %p\n", m1.s, m2.s);
    printf("m1._ref = %d, m2._ref = %d\n", m1._ref, m2._ref);
    printf("m1.cv  = %x, m2.cv  = %x\n", m1.toplevelcv, m2.toplevelcv);
}
version (none)
{
    if (m1.s)
    {
        assert(m1.m > TMATCHellipsis && m1.m <= TMATCHuserdef);
        symbol_debug(m1.s);
    }
    if (m2.s)
    {
        //printf("m2.m = %x\n", m2.m);
        //if (!(m2.m > TMATCHellipsis && m2.m <= TMATCHuserdef)) *(char*)0=0;
        assert(m2.m > TMATCHellipsis && m2.m <= TMATCHuserdef);
        symbol_debug(m2.s);
    }
}

    /* CPP98 13.3.3.2-3 User-defined conversion sequence U1 is a better
     * conversion sequence than another user-defined conversion sequence U2 if
     * they contain the same user-defined conversion function or constructor and
     * if the second standard conversion sequence of U1 is better than the
     * second standard conversion sequence of U2.
     */
    if (m1.s != m2.s && m1.s && m2.s)
    {   result= 0;              // user-defined sequences with different functions
                                // are the same
        goto Lret;
    }

    result = m1.m - m2.m;

    if (result == 0 && m1.m == TMATCHuserdef)
    {   result = m1.m2 - m2.m2;
        //printf("result2 = %d\n", result);
    }
    else

    /* CPP98 13.3.3.2-3 if S1 and S2 are reference bindings (8.5.3),
     * and the types to which the references refer are the same
     * type except for toplevel cvqualifiers,
     * and the type to which the reference initialized by S2
     * refers is more cvqualified than the type to which the
     * reference initialized by S1 refers,
     * then the types are distinquishable.
     */

    if (result == -1 || result == 1)
    {
        //printf("result = %d, ref1 = %d, ref2 = %d, %x, %x\n", result, m1._ref, m2._ref, m1.toplevelcv, m2.toplevelcv);
        if (m1.toplevelcv ^ m2.toplevelcv)
        {
            if (m1._ref && m2._ref && m1.toplevelcv ^ m2.toplevelcv)
            {
                if ((m1.toplevelcv & ~m2.toplevelcv) == 0)
                    // m2 is more cv-qualified
                    goto Lret;
                if ((m2.toplevelcv & ~m1.toplevelcv) == 0)
                    // m1 is more cv-qualified
                    goto Lret;
            }
            result = 0;
        }
    }
Lret:
    //printf("result = %d\n", result);
    return result;
}


/***************************************
 * Compare two match arrays.
 * Returns:
 *      < 0     1 is a worse match than 2
 *      == 0    neither is better than the other
 *      > 0     1 is a better match than 2
 */

private int match_cmp(Match *m1, Match *m2, int nargs)
{   int result;

version (none)
{
    printf("match_cmp(m1 = %p, m2 = %p, nargs = %d)\n", m1, m2, nargs);
    for (int i = 0; i <= nargs; i++)
    {
        printf("\tm1[%d] = x%02x,%p m2[%d] = x%02x,%p\n", i, m1[i].m, m1[i].s, i, m2[i].m, m2[i].s);
    }
}
    //{ static int xx; if (++xx == 20) *(char*)0=0; }

    // The first entry in the array is the worst match of each array
    result = Match.cmp(m1[0], m2[0]);
    //printf("\t[0]: %d\n", result);
    if (1)
    {   /* C++98 13.3.3
         * One function is a better match than another if
         * for each argument in the call, the corresponding parameter
         * of the first function is at least as good a match as the
         * corresponding parameter of the second function, and for some
         * argument the corresponding parameter of the first function is
         * a better match.
         */
        int i;

        for (i = 1; i <= nargs; i++)
        {   int r;

            r = Match.cmp(m1[i], m2[i]);
            //printf("\t[%d]: %d\n", i, r);
            if (r)
            {
                if ((r > 0 && result < 0) ||
                    (r < 0 && result > 0))
                {   result = 0;
                    break;
                }
                if (result == 0)
                    result = r;
            }
        }
    }
    //printf("match_cmp: result = %d\n", result);
    return result;
}


/***************************************
 * Translate identifier for symbol to pretty-printed string.
 */

private __gshared
{
    char *cpp_pi;
    size_t cpp_pi_max;
}

void pi_ensure(size_t len)
{
    if (cpp_pi_max < len)
    {
        cpp_pi_max += len * 2;
        cpp_pi = cast(char *) mem_realloc(cpp_pi,cpp_pi_max);
    }
}

private char *pi_cpy(const(char)* s2)
{   size_t len;

    len = strlen(s2) + 1;
    pi_ensure(len);
    return cast(char *)memcpy(cpp_pi,s2,len);
}

private char *pi_cat(const(char)* s2)
{   size_t len1,len2;

    if (!cpp_pi)
        return pi_cpy(s2);
    len1 = strlen(cpp_pi);
    len2 = strlen(s2) + 1;
    pi_ensure(len1 + len2);
    memcpy(cpp_pi + len1,s2,len2);
    return cpp_pi;
}

char *cpp_prettyident(Symbol *s)
{   int func;
    uint fflags;
    const(char)* p;

    //printf("cpp_prettyident('%s')\n", s.Sident);
    symbol_debug(s);
    if (s.Sscope)
    {   cpp_prettyident(s.Sscope);
        pi_cat("::");
    }
    else
    {   pi_ensure(1);
        cpp_pi[0] = 0;
    }
    p = &s.Sident[0];
    func = tyfunc(s.Stype.Tty);
    if (func && s.Sfunc)
    {   int i;

        fflags = s.Sfunc.Fflags;
        if (strcmp(&s.Sident[0],cpp_name_ct.ptr) == 0 && isclassmember(s))
            fflags |= Fctor;
        switch (fflags & (Fctor | Fdtor | Foperator | Fcast))
        {
            case Fdtor:
                pi_cat("~");
                goto case Fctor;
            case Fctor:
                p = &s.Sfunc.Fclass.Sident[0];
                if (s.Sfunc.Fclass.Sstruct.Sflags & STRnotagname)
                    p = cpp_name_none.ptr;
                break;
            case Foperator:
            case Fcast:
                pi_cat("operator ");
                if (fflags & Fcast)
                {
                    type *tret = s.Stype.Tnext;       // function return type

                    type_debug(tret);
                    if (!tret.Tnext)
                    {   tym_t ty = tybasic(tret.Tty);

                        if (ty == TYstruct || ty == TYenum)
                            p = &tret.Ttag.Sident[0];
                        else
                            p = tystring[ty];
                    }
                }
                else
                {
                    if (s.Sfunc.Foper == OPanew)
                        p = "new[]";
                    else if (s.Sfunc.Foper == OPadelete)
                        p = "delete[]";
                    else
                    {
                        i = cpp_opidx(s.Sfunc.Foper);
                        assert(i >= 0);
                        p = oparray[i].pretty.ptr;
                    }
                }
                break;
            case 0:
                break;
            default:
                assert(0);
        }
    }
    else
        p = symbol_ident(s);

{   char* n,o;

    n = strdup(cpp_pi);
    o = strdup(cpp_unmangleident(p));
    assert(n && o);
    pi_cpy(n);
    pi_cat(o);
    free(n);
    free(o);
}

    return cpp_pi;
}

/***************************
 */

private block * block_new(int bc)
{   block *b;

    b = block_calloc();
    b.BC = cast(ubyte)bc;
    return b;
}


/**********************************
 * Make sure predefined symbols are defined.
 */

void cpp_getpredefined()
{
    __gshared const(char)*
        vecnew = "__vec_new",
        vecdel = "__vec_delete",
        vecctor = "__vec_ctor",
        veccpct = "__vec_cpct",
        vecdtor = "__vec_dtor",
        vecinvariant = "__vec_invariant";

    static Symbol* lookupsym(const(char)* p) { return scope_search(p, SCTglobal); }

    if (!s_mptr)
        s_mptr = lookupsym("__mptr");
    symbol_debug(s_mptr);

    if (!s_genthunk)
        s_genthunk = lookupsym("__genthunk");
    symbol_debug(s_genthunk);

    if (s_vec_new == null)
        s_vec_new = lookupsym(vecnew);
    symbol_debug(s_vec_new);
    t_pctor = s_vec_new.Stype.Tparamtypes.Pnext.Pnext.Pnext.Ptype;
    type_debug(t_pctor);

    if (s_vec_delete == null)
        s_vec_delete = lookupsym(vecdel);
    symbol_debug(s_vec_delete);
    t_pdtor = s_vec_delete.Stype.Tparamtypes.Pnext.Pnext.Pnext.Ptype;
    type_debug(t_pdtor);

    if (s_vec_ctor == null)
        s_vec_ctor = lookupsym(vecctor);
    symbol_debug(s_vec_ctor);

    if (s_vec_cpct == null)
        s_vec_cpct = lookupsym(veccpct);
    symbol_debug(s_vec_cpct);

    if (s_vec_dtor == null)
        s_vec_dtor = lookupsym(vecdtor);
    symbol_debug(s_vec_dtor);

    if (s_vec_invariant == null)
        s_vec_invariant = lookupsym(vecinvariant);
    symbol_debug(s_vec_invariant);

static if (TARGET_WINDOS)
{
    if (s_fatexit == null)
        s_fatexit = lookupsym("_fatexit");
    symbol_debug(s_fatexit);
    //if (_tysize[TYint] == 2)
    //  s_fatexit.Stype.Tty = TYffunc;        // always far function
    //s_fatexit.Stype.Tflags &= ~TFfixed;
}

}


/************************************
 * Call operator new(size_t size,...)
 * Input:
 *      global  1       if ::new
 *              2       if new[]
 *      sfunc   function that we're in
 *      tret    type returned from new()
 *      arglist placement list
 * Output:
 *      arglist is free'd
 */

elem *cpp_new(int global,Symbol *sfunc,elem *esize,list_t arglist,type *tret)
{   Symbol *snew;
    elem *enew;
    elem *e;
    type *t = tret.Tnext;
    Classsym *stag;
    const(char)* id;

    list_prepend(&arglist,esize);
    if (global & 2 && !(config.flags4 & CFG4anew))
        global = 1;
    id = (global & 2) ? cpp_name_anew.ptr : cpp_name_new.ptr;
    snew = null;
    if (!(global & 1) && tybasic(t.Tty) == TYstruct)
    {
        /* Look for T::operator new()   */
        snew = cpp_findmember(t.Ttag,id,false);
        stag = t.Ttag;

        //if (snew)
            //cpp_memberaccess(snew,sfunc,t.Ttag);
    }
    if (!snew)
        {                               /* Try global table     */
        stag = null;
        snew = scope_search(id,SCTglobal);
        }
    assert(snew);
    snew = cpp_overload(snew,null,arglist,stag,null,0);

    version (none)
    {   Outbuffer buf;
        char *p1;
        p1 = param_tostring(&buf,snew.Stype);
        dbg_printf("cpp_new(snew='%s%s')\n",cpp_prettyident(snew),p1);
        free(p1);
    }

    enew = el_var(snew);
    e = xfunccall(enew,null,null,arglist);
    el_settype(e,tret);
    return e;
}


/************************************
 * Call operator delete(void *p, size_t size)
 * Input:
 *      global  1       if ::delete (the global delete operator)
 *              2       if delete[]
 *      sfunc   function that we're in
 */

elem *cpp_delete(int global,Symbol *sfunc,elem *eptr,elem *esize)
{   Symbol *sdelete;
    Symbol *s;
    elem *e;
    match_t match;
    list_t arglist;
    const(char)* id;
    type *t = eptr.ET.Tnext;

    type *tptr = eptr.ET;
    if (tptr.Tnext.Tty & (mTYconst | mTYvolatile))
    {
        // Remove cv qualifiers so overloading
        // of operator delete(void * ...) works
        tptr = type_copy(tptr);
        type_setcv(&tptr.Tnext, 0);
        type_settype(&eptr.ET, tptr);
    }

    arglist = list_build(eptr,null);

    if (global & 2 && !(config.flags4 & CFG4anew))
        global = 1;
    id = (global & 2) ? cpp_name_adelete.ptr : cpp_name_delete.ptr;
    sdelete = null;
    if (!(global & 1) && tybasic(t.Tty) == TYstruct)
        sdelete = cpp_findmember(t.Ttag,id,false);

    list_append(&arglist,esize);
    if (!sdelete)
    {                           /* Try global table     */
        sdelete = scope_search(id,SCTglobal);
    }
    assert(sdelete);

    s = cpp_overloadfunc(sdelete,null,arglist,&match,null,null,0);
    if (match == TMATCHnomatch)
    {   list_free(&arglist.next,cast(list_free_fp)&el_free);   // dump size arg
        //type_print((cast(elem *)list_ptr(arglist)).ET);
        s = cpp_overloadfunc(sdelete,null,arglist,&match,null,null,0);
        if (match == TMATCHnomatch)
        {   err_nomatch((global & 2) ? "operator delete[]" : "operator delete",arglist);
            s = sdelete;
        }
    }
    cpp_memberaccess(s,sfunc,isclassmember(s) ? t.Ttag : null);

    e = xfunccall(el_var(s),null,null,arglist);
    return e;
}


/*********************************
 * Determine if two types match for overloading purposes.
 * Ignore refs, const and volatile modifiers, and regard arrays and pointers
 * as equivalent.
 * Input:
 *      flags   2: ignore const,volatile
 * Returns:
 *      !=0     match
 *      0       don't match
 */

int cpp_typecmp(type *t1,type *t2,int flags, param_t *p1 = null, param_t *p2 = null)
{   tym_t t1ty,t2ty;

    type_debug(t1);
    type_debug(t2);
    //dbg_printf("cpp_typecmp(%s,",cpp_typetostring(t1,0)); dbg_printf("%s)\n",cpp_typetostring(t2,0));
version (none)   // the old way, retain for the moment till the other way is tested
{
    if (tyref(t1.Tty))
        t1 = t1.Tnext;
    if (tyref(t2.Tty))
        t2 = t2.Tnext;

  return
    (t1 == t2 ||
    ((t1ty = tybasic(t1.Tty)) == (t2ty = tybasic(t2.Tty))
                ||
        /* Arrays and pointer types are compatible      */
        (t1ty == TYarray || t2ty == TYarray) &&
        (LARGEDATA && (typtr(t1ty) || typtr(t2ty)) ||
         t1ty == pointertype || t2ty == pointertype)
                ||
        (exp2_ptrconv(t1,t2) == 1 && (LARGEDATA || _tysize[t1ty] == _tysize[t2ty]) &&
         t1ty != TYhptr && t2ty != TYhptr)
    )
        &&
    /* Array dimensions must match or be unknown        */
    (t1ty != TYarray || t2ty != TYarray || t1.Tdim == t2.Tdim ||
     t1.Tflags & TFsizeunknown || t2.Tflags & TFsizeunknown
    )
        &&
    /* If structs, then the members must match  */
    (t1ty != TYstruct && t1ty != TYenum && t1ty != TYmemptr || t1.Ttag == t2.Ttag)
        &&
    /* If subsequent types, they must match (ignore const, volatile)    */
    (typematch(t1.Tnext,t2.Tnext,2 | 1))
        &&
    /* If function, and both prototypes exist, then prototypes must match */
    (!tyfunc(t1ty) ||
     !(t1.Tflags & TFprototype) ||
     !(t2.Tflags & TFprototype) ||
     ((t1.Tflags & (TFprototype | TFfixed)) ==
         (t2.Tflags & (TFprototype | TFfixed))) &&
        paramlstmatch(t1.Tparamtypes,t2.Tparamtypes))
    /* If template, must refer to same template */
        &&
    (tybasic(t1ty) != TYtemplate ||
         template_paramlstmatch(t1, t2))
    )
   ;
}
else
{
    if (tyref(t2.Tty))
        t2 = t2.Tnext;

  Lagain:
    if (t1 == t2)
        goto Lmatch;

    t2ty = tybasic(t2.Tty);
    t1ty = tybasic(t1.Tty);

    switch (t1ty)
    {
        case TYarray:                   // Array dimensions must match or be unknown
            if (t2ty == TYarray)
            {   if (t1.Tdim != t2.Tdim &&
                    !((t1.Tflags | t2.Tflags) & TFsizeunknown)
                   )
                    goto Lnomatch;
            }
            else if (typtr(t2ty))
            {
                if (!LARGEDATA && t2ty != pointertype)
                    goto Lnomatch;
            }
            else
                goto Lnomatch;
            goto Lnext;

        case TYstruct:
        case TYenum:
            if (t1ty != t2ty)
                goto Lnomatch;
            if (t1.Ttag != t2.Ttag)
                goto Lnomatch;
            break;

        case TYmemptr:
            if (t1ty != t2ty)
                goto Lnomatch;
            if (t1.Ttag != t2.Ttag)
                goto Lnomatch;
            if (!typematch(t1.Tnext,t2.Tnext,0))
                goto Lnomatch;
            break;

        case TYtemplate:
version (none)
{
            // Exact match if t2 is an instance of template t1
            if (t2ty == TYstruct &&
                (cast(typetemp_t *)t1).Tsym == t2.Ttag.Sstruct.Stempsym)
                break;
}
            if (t2ty != TYtemplate)
                goto Lnomatch;
            if (!template_paramlstmatch(t1, t2))
                goto Lnomatch;
            assert(!t1.Tnext);
            break;

        default:
            // If function, and both prototypes exist, then prototypes must match
            if (tyfunc(t1ty))
            {
        case TYnfunc:
        case TYnpfunc:
        case TYnsfunc:
                if (t1ty != t2ty)
                    goto Lnomatch;
                if (!(
                     !(t1.Tflags & t2.Tflags & TFprototype) ||
                     ((t1.Tflags ^ t2.Tflags) & TFfixed) == 0 &&
                        paramlstmatch(t1.Tparamtypes,t2.Tparamtypes)
                   ))
                    goto Lnomatch;

            }
            else if (typtr(t1ty))
            {
        case TYnptr:
                if (t1ty == t2ty)
                { }
                else if (t2ty == TYarray)
                {
                    if (!LARGEDATA && t1ty != pointertype)
                        goto Lmatch;
                    goto Lnext;
                }
                else if (!(
                        (exp2_ptrconv(t1,t2) == 1 && (LARGEDATA || _tysize[t1ty] == _tysize[t2ty]) &&
                         t1ty != TYhptr && t2ty != TYhptr)
                        ))
                    goto Lnomatch;
            }
            else if (tyref(t1ty))
            {
        case TYref:
                t1 = t1.Tnext;
                assert(!tyref(t1.Tty));
                goto Lagain;
            }
            else if (t1ty != t2ty)
                goto Lnomatch;

        Lnext:
            if (!typematch(t1.Tnext,t2.Tnext,flags | 1))
                goto Lnomatch;
            break;

        case TYident:
            if (t2ty != TYident)
                goto Lnomatch;
            if (p1 && p2)
            {
                int n1 = p1.searchn(t1.Tident);
                int n2 = p2.searchn(t2.Tident);

                if (n1 != -1 && n1 == n2)
                    break;
            }
            if (strcmp(t1.Tident, t2.Tident))
                goto Lnomatch;
            break;
    }
  Lmatch:
    return 1;

  Lnomatch:
    return 0;
}
}


/*********************************
 * Determine if two types match. Try to get them to match using
 * user-defined conversions.
 * Returns:
 *      TMATCHxxxxx match level after user-defined conversion
 */

private Match cpp_usertypecmp(elem *e1,type *t2)
{   type *t1 = e1.ET;
    type *t2class;
    Match result;
    Symbol *scast;

    //printf("cpp_usertypecmp()\n");
    assert(t1 && t2);
    type_debug(t1);
    type_debug(t2);
    cpp_usertypecmp_nest++;

    /* If e1 is a class, look for a user-defined conversion     */
    scast = cpp_typecast(t1,t2,&result);

    /* If t2 is a class or a ref to a class, look for a constructor     */
    /* to convert e1 to t2                                              */
    if (tyref(t2.Tty))
    {   result._ref = 1;
        t2class = t2.Tnext;
    }
    else
        t2class = t2;
    result.toplevelcv = t2class.Tty & (mTYconst | mTYvolatile);
    if (type_struct(t2class))
    {   list_t arglist;
        Symbol *sctor;
        Symbol *s;
        Symbol *ambig;
        Match matchctor;

        arglist = list_build(e1,null);

        symbol_debug(t2class.Ttag);
        template_instantiate_forward(t2class.Ttag);
        sctor = t2class.Ttag.Sstruct.Sctor;
        s = cpp_lookformatch(sctor,null,arglist,&matchctor,&ambig,null,null,4u,null,null);
        list_free(&arglist,FPNULL);
        if (s && !(s.Sfunc.Fflags & Fexplicit))
        {   /* We matched a constructor */
            //printf("cpp_usertypecmp: matched a constructor\n");
            int i = Match.cmp(result, matchctor);
            if (i == 0)         // ambiguous match
                goto Lnomatch;
            else if (i < 0)                     // if matchctor is better
            {
                if (ambig)                      /* ambiguous match      */
                    goto Lnomatch;
                result.m = matchctor.m;         // pick better match
                result.s = s;
            }
            else
                result.s = scast;
        }
    }
    else
        result.s = scast;
    goto Lret;

Lnomatch:
    result.m = TMATCHnomatch;
    result.s = null;
    result._ref = 0;
    result.toplevelcv = 0;
Lret:
    cpp_usertypecmp_nest--;
    return result;
}


/*********************************
 * Determine matching level of elem e1 to type t2.
 * Returns:
 *      match level (as a byte)
 */

match_t cpp_matchtypes(elem *e1,type *t2, Match *pm = null)
{
    match_t match;
    Match m;
    type *t1;
    tym_t tym1,tym2;
    int adjustment;

version (none)
{
/* Integral promotions table
NOTE: Does not include TYdchar, TYchar16, TYnullptr but should
                        TO
                ld  da   d f ull ll  ul  l ui  i   e us  w  s  uc sc  c  b
        bool     0   0 | 0  0  1  1 | 1  1  1  1 | 0  1  1  1 | 0  0  0  0
   F    char     0   0   0  0  1  1   1  1  1  1   0  1  1  1   0  0  0  0
   R    schar    0   0   0  0  1  1   1  1  1  1   0  1  1  1   0  0  0  0
   O    uchar    0   0   0  0  1  1   1  1  1  1   0  1  1  1   0  0  0  0
   M    short    0   0   0  0  1  1   1  1  1  1   0  1  1  0   0  0  0  0
        wchar_t  0   0   0  0  1  1   1  1  1  1   0  0
        ushort   0   0   0  0  1  1   1  1  1  1   0  0
        enum     0   0   0  0  1  1   1  1  1  1   0  0
        int      0   0   0  0  1  1   1  1  1  0   0  0
        uint     0   0   0  0  1  1   1  1
        long     0   0   0  0  1  1   1  0
        ulong    0   0   0  0  1  1   0  0
        llong    0   0   0  0  1  0
        ullong   0   0   0  0  0  0
        float    1   1   1  0
        double   1   0   0
        real64   1   0
        ldouble  0
 */
    immutable uint[TYldouble + 1] typromo =
    [
        0x03F70,
        0x03F70,
        0x03F70,
        0x03F70,
        0x03F60,
        0x03F00,
        0x03F00,
        0x03F00,
        0x03E00,
        0x03C00,
        0x03800,
        0x03000,
        0x02000,
        0x00000,
        0x38000,
        0x20000,
        0x20000,
        0x00000,
    ];
}
else
{
    __gshared uint[TYldouble + 1] typromo;
    __gshared int inited;

    // Use rules from ANSI C++ 4.5 & 4.6
    if (!inited)
    {   inited++;

        typromo[TYchar]  = 1 << TYint;
        typromo[TYschar] = 1 << TYint;
        typromo[TYuchar] = 1 << TYint;
        typromo[TYshort] = 1 << TYint;
        typromo[TYbool]  = 1 << TYint;
        typromo[TYfloat] = 1 << TYdouble;      // ANSI C++ 4.6

        if (I16)
        {
            typromo[TYwchar_t] = 1 << TYuint;
            typromo[TYushort]  = 1 << TYuint;
        }
        else
        {
            typromo[TYwchar_t] = 1 << TYint;
            typromo[TYushort]  = 1 << TYint;
        }
    }
}

    enum LOG_MATCHTYPES = 0;

static if (LOG_MATCHTYPES)
{
    printf("cpp_matchtypes(e1=%p,t2=%p)\n",e1,t2);
    type_print(e1.ET);
    type_print(t2);
}
    t1 = e1.ET;
    assert(t1 && t2);

    type_debug(t1);
    type_debug(t2);

    m.s = null;
    adjustment = 0;
    tym1 = tybasic(t1.Tty);
    if (tyref(tym1))
    {   t1 = t1.Tnext;
        tym1 = tybasic(t1.Tty);
    }
    tym2 = tybasic(t2.Tty);
    if (tyref(tym2))
    {   t2 = t2.Tnext;
        m._ref = 1;
        tym2 = tybasic(t2.Tty);

        /* ARM pg. 318  */
        /* Cannot 'subtract' const or volatile modifiers        */
        if ((t1.Tty & (mTYconst | mTYvolatile)) & ~t2.Tty)
            goto nostandmatch;          /* try user-defined conversion  */

version (none)   // this crashes cpp2.cpp, don't know why.
{
        // Cannot bind a temporary to a non-const reference
        if (e1.Eoper == OPvar &&
            e1.EV.sp.Vsym.Sflags & SFLtmp &&
            !(t2.Tty & mTYconst))
            goto nostandmatch;
}
        if (!(t2.Tty & mTYconst) &&
            !typematch(t2, t1, 0) &&
            !t1isbaseoft2(t2,t1))
        {
            goto nomatch;
        }
    }
    m.toplevelcv = t2.Tty & (mTYconst | mTYvolatile);

    // Changing const or volatile is a tie-breaker,
    // not just for references anymore
    if ((t2.Tty & (mTYconst | mTYvolatile)) ^
        (t1.Tty & (mTYconst | mTYvolatile)))
        adjustment = 1;

    if ((typtr(tym1) || tym1 == TYarray) &&
        (typtr(tym2) || tym2 == TYarray)
       )
    {
        tym_t t1n = t1.Tnext.Tty & (mTYconst | mTYvolatile);
        tym_t t2n = t2.Tnext.Tty & (mTYconst | mTYvolatile);

        // C++98 4.2-2 Allow subtracting const from string literal
        if (e1.Eoper == OPstring && !t2n && t1n == mTYconst)
            adjustment += 1;
        else
        {
            // Subtracting const or volatile to pointers is not allowed
            if (t1n & ~t2n)
                goto nostandmatch;

            // Adding const or volatile to pointers makes the match worse
            if (t1n ^ t2n)
                adjustment += 1;
        }
    }

    /* Look for exact match     */
    match = TMATCHexact;
    /*dbg_printf("result is %d\n",cpp_typecmp(t1,t2,2));*/
    if (cpp_typecmp(t1,t2,2,null,null))
    {
static if (LOG_MATCHTYPES)
{
        printf("exact match\n");
}
        goto yesmatch;
    }

    /* Look for match with promotions   */
    match = TMATCHpromotions;
    if (tym1 >= typromo.length || tym2 >= typromo.length)
        goto nopromomatch;
    if (typromo[tym1] & (1 << tym2))
        goto yesmatch;
    if (tym1 == TYenum)
    {
        /* C++98 4.5 Integral promotions [conv.prom], paragraph 2.
         * "An rvalue of type wchar_t (3.9.1) or an enumeration type (7.2) can
         * be converted to an rvalue of the first of the following types that
         * can represent all the values of its underlying type: int,
         * unsigned int, long, or unsigned long."
         */

        if (t1.Tnext.Tty == tym2)
            goto yesmatch;
    }

nopromomatch:
    /* Look for match with standard conversions */
    match = TMATCHstandard;
    // 0 can be converted to any scalar type
    if (e1.Eoper == OPconst && !boolres(e1) &&
        tyintegral(tym1) &&
        tyscalar(tym2) && tym2 != TYenum &&
        // and not pointer to struct
        !(typtr(tym1) && type_struct(t1.Tnext) && typtr(tym2) && type_struct(t2.Tnext)))
        goto yesmatch;

    /* An arithmetic type can be converted to any other arithmetic type */
    if (tyarithmetic(tym1) && tyarithmetic(tym2) && tym2 != TYenum)
        goto yesmatch;

    /* C++98 4.12: an arithmetic, enumeration, pointer or pointer to member
     * can be converted to bool.
     */
    if (tym2 == TYbool && (tym1 == TYenum || tymptr(tym1)))
    {
static if (LOG_MATCHTYPES)
{
        printf("\tboolean conversion\n");
}
        /* C++98 13.3.3.2-4: "A conversion that is not a conversion of a
         * pointer, or pointer to member, to bool is better than another
         * conversion that is such a conversion.
         */
        match = TMATCHboolean;
        goto yesmatch;
    }

    /* This stuff should match typechk()        */
    if (tymptr(tym1) && tymptr(tym2))
    {   int ptrconv;

        // Check for pointer to function
        if (tyfunc(t1.Tnext.Tty) && tyfunc(t2.Tnext.Tty))
        {   elem *etmp;
            Symbol *s;

            // Try overloaded function
            // (typechk() will eventually do the actual replacement in
            // e1 of the overloaded function)
            ptrconv = 0;
            etmp = el_copytree(e1);
            if (!OTleaf(etmp.Eoper))
                etmp = poptelem(etmp);
            if (etmp.Eoper == OPrelconst)
            {   s = etmp.EV.Vsym;
                s = cpp_findfunc(t2.Tnext,null,s,1);
                if (s && s != etmp.EV.Vsym)
                {   type *t;

                    etmp.EV.Vsym = s;
                    type_free(etmp.ET);

                    // Different for member pointer and function pointer.
                    // Analogous to code in exp2_paramchk().
                    if ((!s.Sfunc.Fthunk || s.Sfunc.Fflags & Finstance) &&
                        (s.Sfunc.Fflags & Fstatic || !isclassmember(s)))
                    {
                        t = newpointer(s.Stype);
                    }
                    else
                    {
                        t = type_allocn(TYmemptr,s.Stype);
                        t.Ttag = cast(Classsym *)s.Sscope;
                    }

                    t.Tcount++;
                    etmp.ET = t;
                    ptrconv = cpp_matchtypes(etmp,t2);
                }
            }
            el_free(etmp);
            if (ptrconv)
            {   match = cast(match_t)ptrconv;
                adjustment = 0;
                goto yesmatch;
            }
        }

        ptrconv = exp2_ptrconv(t1,t2);
        if (!ptrconv)
            goto nostandmatch;
        adjustment += ptrconv - 1;
        goto yesmatch;
    }

    /* If classes, look down base classes for a match   */
    if (tym1 == TYstruct && tym2 == TYstruct)
    {   int conv;

        conv = t1isbaseoft2(t2,t1);
        if (!conv)
            goto nostandmatch;
        adjustment += (conv >> 8);
        goto yesmatch;

        if (t1isbaseoft2(t2,t1))
            goto yesmatch;
    }

    if (e1.Eoper == OPnullptr && tynullptr(tym1) && typtr(tym2))
    {
        goto yesmatch;
    }

    /* Look for match with user-defined conversions     */
nostandmatch:
    if (cpp_usertypecmp_nest)           /* don't allow nesting of user-def */
        goto nomatch;

    m = cpp_usertypecmp(e1,t2);
    match = m.m;
    if (match != TMATCHnomatch)
    {
        //printf("test1 match = x%x, adjustment = %d, s = %p\n", match, adjustment, m.s);
        assert((adjustment & ~1) == 0);
version (all)
{
        m.m2 = cast(match_t)(match - adjustment);
        adjustment = 0;
}
else
{
        adjustment *= 2;                /* make triv conv more significant */
        if (match != TMATCHexact)
        {   adjustment++;               /* slightly worse than exact    */
            if (match <= TMATCHpromotions)
            {   adjustment++;
                if (match <= TMATCHstandard)
                {   adjustment++;
                    if (match <= TMATCHboolean)
                        adjustment++;
                }
            }

        }
}
        match = TMATCHuserdef;
        goto yesmatch;
    }

    /* Couldn't find any match  */
nomatch:
static if (LOG_MATCHTYPES)
{
    printf("nomatch\n");
}
    m.m = TMATCHnomatch;
    if (pm)
        *pm = m;
    return TMATCHnomatch;

yesmatch:
static if (LOG_MATCHTYPES)
{
    printf("match = x%x, adjustment = x%x\n",match,adjustment);
}
    if (pm)
    {   m.m = cast(match_t)(match - adjustment);
        *pm = m;
    }
    return cast(match_t)(match - adjustment);
}


/**********************************
 * Determine if there is a user-defined conversion to convert from e1 to t2.
 * If there is, return !=0 and a revised e1.
 * Input:
 *      doit    bit 1:  revise e1 to be the converted expression
 *                  2:  skip final cast to t2
 *                  4:  don't check for constructor conversions
 *                  8:  implicit conversion
 * Returns:
 *      0       no conversion
 *      !=0     match level
 */

int cpp_cast(elem **pe1,type *t2,int doit)
{   elem *e1;
    type *t1;
    type *t2cast;
    match_t matchconv;
    match_t matchctor;
    Symbol *sconv;
    Symbol *sctor;
    Symbol *sctorambig;

enum LOG_CAST = 0;

    e1 = *pe1;
    t1 = e1.ET;
    assert(pe1 && e1 && t1 && t2);
    type_debug(t1);
    type_debug(t2);
static if (LOG_CAST)
{
    dbg_printf("cpp_cast(doit = x%x)\n", doit);
    elem_print(e1);
    type_print(t1);
    type_print(t2);
}

    /* Look for conversion operator     */
    matchconv = TMATCHnomatch;
    if (tybasic(t1.Tty) == TYstruct)
    {
        template_instantiate_forward(t1.Ttag);
        Match m;
        sconv = cpp_typecast(t1,t2,&m);
        matchconv = m.m;
    }

    /* Look for constructor     */
    matchctor = TMATCHnomatch;
    t2cast = tyref(t2.Tty) ? t2.Tnext : t2;
    if (!(doit & 4) && tybasic(t2cast.Tty) == TYstruct)
    {
        template_instantiate_forward(t2cast.Ttag);

        /* If convert to same type      */
        if (tybasic(t1.Tty) == TYstruct &&
            t1.Ttag == t2cast.Ttag)
        {
static if (LOG_CAST)
{
            printf("\tcpp_cast(): convert to same type, nomatch\n");
}
            return TMATCHnomatch;
        }

        n2_createcopyctor(t2cast.Ttag,1);

        if ((sctor = t2cast.Ttag.Sstruct.Sctor) != null)
        {   list_t arglist;

            /* Look for constructor to convert e1 to t2 */
            arglist = list_build(e1,null);
            sctorambig = null;
            sctor = cpp_overloadfunc(sctor,t1,arglist,&matchctor,&sctorambig,null,0);
            list_free(&arglist,FPNULL);

            if (doit & 1 && doit & 8 && sctor && sctor.Sfunc.Fflags & Fexplicit)
            {   // BUG: does this check occur before overloading or after?
                cpperr(EM_explicit_ctor);
                //sctor = null;
                //matchctor = TMATCHnomatch;
            }
        }
    }

    /* If no matches, we're done        */
    if (matchconv == matchctor)
    {
        if (matchconv != TMATCHnomatch)
            cpperr(EM_ambig_type_conv);         // ambiguous conversion
static if (LOG_CAST)
{
        printf("\tcpp_cast(): matchconv == matchctor == x%x, nomatch\n", matchconv);
}
        return TMATCHnomatch;
    }

static if (LOG_CAST)
{
    printf("\tConversion exists, doit = x%x\n",doit);
}
    if (doit & 1)
    {   /* The conversion exists. So implement it.      */

        /* If conversion function is a better fit       */
        if (matchconv > matchctor)
        {   elem* eptr,econv;
            type *t;

static if (LOG_CAST)
{
            printf("\tuse conversion function\n");
}
            symbol_debug(sconv);
            cpp_memberaccess(sconv,funcsym_p,t1.Ttag); /* check access rights  */

            /* Construct a function call to do the conversion           */
            eptr = exp2_addr(e1);
            eptr = _cast(eptr,newpointer(sconv.Sscope.Stype)); /* to correct pointer type */
            econv = cpp_getfunc(t1,sconv,&eptr);
            econv = el_unat(OPind,econv.ET.Tnext,econv);

            e1 = xfunccall(econv,eptr,null,null);
            if (!(doit & 2))
                //e1 = exp2_cast(e1,t2);        // any final built-in conversion
                e1 = _cast(e1,t2);       // any final built-in conversion
            *pe1 = e1;
        }
        else
        {   /* Use a constructor for the conversion     */

            list_t arglist;
            elem *e;

            /* Look for constructor to convert e1 to t2 */
            symbol_debug(sctor);
            if (sctorambig)
                err_ambiguous(sctor,sctorambig);
            arglist = list_build(e1,null);
            e = cpp_initctor(t2cast,arglist);
            e = typechk(e,t2);          /* if t2 is a ref, convert e to a ref */
            *pe1 = e;
        }
    }
static if (LOG_CAST)
{
    printf("\tcpp_cast(): matchconv = x%x, matchctor = x%x\n", matchconv, matchctor);
}
    return matchconv > matchctor ? matchconv : matchctor;
}


/***************************
 * Create a new variable of type tclass and call constructor on it.
 */

elem *cpp_initctor(type *tclass,list_t arglist)
{
    elem *ec;
    Symbol *s;

    type_debug(tclass);
    debug assert(tclass && tybasic(tclass.Tty) == TYstruct);
    //dbg_printf("cpp_initctor(%s)\n",tclass.Ttag.Sident);
    if (0 && !init_staticctor &&        /* if variable doesn't go into _STI_xxxx */
        (!funcsym_p || pstate.STinparamlist))
    {
        cpperr(EM_ctor_context);        // can't handle constructor in this context
        ec = el_longt(newpointer(tclass),0);
    }
    else
    {
        template_instantiate_forward(tclass.Ttag);
        s = symbol_genauto(tclass);
        s.Sflags |= SFLtmp;
        //dbg_printf("init_staticctor = %d, inparamlist = %d, s.Ssymnum = %d\n",
        //    init_staticctor,pstate.STinparamlist,s.Ssymnum);

assert(s.Stype);
type_debug(s.Stype);
        ec = cpp_constructor(el_ptr(s),tclass,arglist,null,null,0);
        if (!ec)
            ec = el_ptr(s);
        else if (ec.Eoper == OPctor)
            ec = el_bint(OPinfo,ec.ET,ec,el_ptr(s));
    }
    return el_unat(OPind,tclass,ec);
}


/*********************************
 * Run through elem tree, and allocate temps for deferred allocations.
 */

private void cpp_alloctmp_walk(elem *e);
private __gshared elem *cpp_alloctmp_e;

void cpp_alloctmps(elem *e)
{
    cpp_alloctmp_e = e;
    cpp_alloctmp_walk(e);
}

private void cpp_alloctmp_walk(elem *e)
{   Symbol *s;

    while (1)
    {   elem_debug(e);
        if (!OTleaf(e.Eoper))
        {   if (OTbinary(e.Eoper))
                cpp_alloctmp_walk(e.EV.E2);
            e = e.EV.E1;
        }
        else
        {
            switch (e.Eoper)
            {
                case OPvar:
                case OPrelconst:
                    /* If deferred allocation of variable, allocate it now.     */
                    /* The deferred allocations are done by cpp_initctor().     */
                    if ((s = e.EV.Vsym).Sclass == SCauto &&
                        s.Ssymnum == -1)
                    {   Symbol *s2;

                        //printf("Deferred allocation of %p\n",s);
                        s2 = symbol_genauto(s.Stype);
                        s2.Sflags |= SFLnodtor;
                        el_replace_sym(cpp_alloctmp_e,s,s2);
                    }
                    break;

                default:
                    break;
            }
            break;
        }
    }

}


/*****************************
 * Determine if *pe is a struct that can be cast to a pointer.
 * If so, do it.
 * Returns:
 *      0       can't be cast to a pointer
 *      !=0     *pe is cast to a pointer
 */

int cpp_casttoptr(elem **pe)
{
    elem *e = *pe;
    type *t = e.ET;
    Symbol *s;
    int result = 0;

    if (tybasic(t.Tty) == TYstruct)
    {
        s = cpp_typecast(t,tspvoid,null);       // search for conversion function
        if (!s)
        {
            s = cpp_typecast(t,tspcvoid,null);  // search for conversion function
        }
        if (s)
        {   result = 1;                         // we can do the cast
            cpp_cast(pe,s.Stype.Tnext,3);     // and do the cast
        }
    }
    return result;
}


/*****************************
 * If e is a struct, determine if there is a user-defined cast
 * to a value which can be tested. If there is, cast it and
 * return the resulting elem.
 * Input:
 *      flags   1       to bool
 *              0       to any scalar
 */

private __gshared type * *[16] cpp_typ =
[   &tspcvoid,&tstypes[TYbool],&tstypes[TYchar],&tstypes[TYschar],&tstypes[TYuchar],
    &tstypes[TYshort],&tstypes[TYushort],&tstypes[TYwchar_t],&tstypes[TYint],&tstypes[TYuint],&tstypes[TYlong],&tstypes[TYulong],
    &tstypes[TYdchar],
//  &tsllong,&tsullong,
    &tstypes[TYfloat],&tstypes[TYdouble],
    &tstypes[TYdouble_alias],
//  &tsldouble,
];

elem *cpp_bool(elem *e, int flags)
{
    if (!CPP)
        return e;

    //printf("cpp_bool(flags = %d)\n", flags);
    if (type_struct(e.ET))
    {   int i,imax;
        match_t[cpp_typ.length] match = void;

        if (flags == 1 && cpp_cast(&e, tstypes[TYbool], 0) == TMATCHexact)
        {
            cpp_cast(&e, tstypes[TYbool], 3);    // do the conversion
        }
        else
        {
            imax = 0;
            match[imax] = TMATCHnomatch;
            for (i = 0; i < match.length; i++)
            {   match[i] = cast(match_t)cpp_cast(&e,*cpp_typ[i],0);
                //printf("match[%d] = x%x\n", i, match[i]);
                if (match[i] > match[imax])             // if better match
                    imax = i;
            }
            if (match[imax] > TMATCHnomatch)
            {   for (i = imax + 1; i < match.length; i++)
                    if (match[i] == match[imax])
                    {
                        cpperr(EM_ambig_type_conv);     // ambiguous conversion
                        break;
                    }
                if (imax == 0)
                    cpp_casttoptr(&e);
                else
                    cpp_cast(&e,*cpp_typ[imax],3);      // do the conversion
            }
        }
    }
    return e;
}

private match_t cpp_bool_match(elem *e)
{
    match_t m = TMATCHexact;

    //printf("cpp_bool_match()\n");
    if (type_struct(e.ET))
    {   int i;
        match_t mbest = TMATCHnomatch;

        mbest = TMATCHnomatch;
        for (i = 0; i < cpp_typ.length; i++)
        {   m = cast(match_t)cpp_cast(&e,*cpp_typ[i],0);
            if (m > mbest)              // if better match
                mbest = m;
        }
        m = mbest;
    }
    return m;
}


/*************************************
 * Determine if there is a cast function to convert from tclass to t2.
 * Output:
 *      *pmatch         TMATCHxxxx of how function matched
 * Returns:
 *      conversion function symbol
 *      null if no unique cast function found
 */

Symbol * cpp_typecast(type *tclass,type *t2,Match *pmatch)
{
    list_t cl;
    Symbol *s;
    Symbol *sexact = null;
    int cexact = 0;
    int aexact = 0;
    int adjustment;
    elem e = void;
    struct_t *st;
    Match match,m;

enum LOG_TYPECAST = 0;

    cpp_usertypecmp_nest++;     /* do not allow more than one level of
                                   user-defined conversions             */
    type_debug(tclass);
    type_debug(t2);
    if (type_struct(tclass))
    {
        e.Eoper = OPvar;        /* a kludge for parameter to promotionstypecmp() */
        e.Ecount = 0;
        st = tclass.Ttag.Sstruct;

static if (LOG_TYPECAST)
{
        printf("cpp_typecast(stag = '%s')\n", tclass.Ttag.Sident);
        type_print(tclass);
        type_print(t2);
        printf("\n");
}

        /* Look down list of user-defined conversion functions  */
        for (cl = st.Scastoverload; cl; cl = list_next(cl))
        {
            s = list_symbol(cl);
            symbol_debug(s);
            if (s.Sclass == SCfunctempl)
            {
                param_t *ptpl = s.Sfunc.Farglist;
                param_t *ptal;

                //printf("\tfound SCfunctempl\n");
                ptal = ptpl.createTal(null);

                // There are no function arguments to a cast operator,
                // so pick up the template parameters from the cast
                // operator return type.
                m = template_matchtype(s.Stype.Tnext, t2, null, ptpl, ptal, 1);
                if (m.m < TMATCHexact)
                {
                    continue;
                }
                s = template_matchfunc(s, null, 1, TMATCHexact, ptal);
                memset(&m, 0, m.sizeof);
                m.m = s ? TMATCHexact : TMATCHnomatch;
            }
            else
            {
                if (s.Sfunc.Fflags & Finstance)
                    continue;
                if (s.Sclass == SCftexpspec)
                    continue;
                //printf("\tfound %p %s\n", s, s.Sident);
                debug e.id = elem.IDelem;
                e.ET = s.Stype.Tnext;
                cpp_matchtypes(&e,t2,&m);
            }
static if (LOG_TYPECAST)
{
            printf("cpp_typecast: m = x%x\n", m.m);
}
            if (m.m != TMATCHnomatch)
            {
                adjustment = 0;
                if ((s.Stype.Tty & (mTYconst | mTYvolatile)) ^
                    (tclass.Tty & (mTYconst | mTYvolatile)) &&
                    m.m == TMATCHexact)
                {
static if (LOG_TYPECAST)
{
                    printf("adjustment\n");
}
                    m.m--;
                    adjustment = -1;
                }

                //printf("m = %x, match = %x, adjustment = %d, aexact = %d\n", m, match, adjustment, aexact);
                if (m.m > match.m ||
                    m.m == match.m && adjustment > aexact)
                {   // Better match
                    match = m;
                    sexact = s;
                    cexact = 0;
                    aexact = adjustment;
                }
                else if (m.m == match.m && adjustment == aexact && s != sexact)
                    cexact++;
            }
        }
//      match += aexact;                        // fold in adjustment

        if (match.m != TMATCHexact)             // if inexact match
        {   baseclass_t *b;
            Symbol *sexact2 = sexact;

            /* Try looking at base classes      */
            for (b = st.Sbase; b; b = b.BCnext)
            {
                s = cpp_typecast(b.BCbase.Stype,t2,&m);
                if (m.m)
                {
                    if (m.m > match.m)          /* if better match      */
                    {   match = m;
                        sexact = s;
                        cexact = 0;
                    }
                    else if (m.m == match.m && s != sexact &&
                        sexact != sexact2)      // match with derived is better
                                                // than match with base
                        cexact++;
                }
            }
        }

        if (cexact)                             /* if multiple matches  */
        {   match.m = TMATCHnomatch;
            sexact = null;
        }
    }

nomatch:
    cpp_usertypecmp_nest--;
    if (pmatch)
        *pmatch = match;
static if (LOG_TYPECAST)
{
    printf("\tcpp_typecast: match = %x, ref=%d, sexact = '%s'\n", match.m, match._ref, sexact ? sexact.Sident : "null");
}
    return sexact;
}


/**************************
 * Determine matching of arglist to function sfunc.
 * Input:
 *      ma      array of match levels to be filled in:
 *              ma[0] worst match
 *              ma[1] for 'this' match
 *              ma[2..2+nargs] match for each parameter
 *      tthis   type of 'this' pointer, null if no 'this'
 * Returns:
 *      0       don't match
 *      != 0    worst match level
 */

private Match cpp_matchfuncs(type *tthis,list_t arglist,Symbol *sfunc,Match *ma, int *puseDefault)
{   list_t al;
    param_t *p;
    type *functype;
    Match wmatch;                       /* worst match                  */
    Match m;
    int i;
    int any;

    //printf("cpp_matchfuncs()\n");
version (none)
{
    Outbuffer buf;
    char *p1;
    p1 = param_tostring(&buf,sfunc.Stype);
    dbg_printf("cpp_matchfuncs(arglist=%p,sfunc='%s%s')\n",
        arglist,cpp_prettyident(sfunc),p1);
    free(p1);
}

    functype = sfunc.Stype;
    type_debug(functype);
    assert(tyfunc(functype.Tty));
    wmatch.m = TMATCHexact;
    any = 0;
    *puseDefault = 0;           // assume not using default parameters

    // First do a quick check to see if the number of arguments is compatible
    al = arglist;
    p = functype.Tparamtypes;
    while (1)
    {
        if (al)
        {
            if (p)
            {
                p = p.Pnext;
            }
            else
            {
                if (functype.Tflags & TFfixed)
                    goto nomatch;
                break;
            }
            al = list_next(al);
        }
        else if (p)                     /* function must have more types */
        {
            if (!p.Pelem)
                goto nomatch;           /* remaining parameters are not defaults */
            break;
        }
        else
            break;
    }

    // Start by regarding any 'this' parameter as the first argument
    // to a non-static member function
    if (tthis && isclassmember(sfunc) &&
        !(sfunc.Sfunc.Fflags & (Fstatic | Fctor | Fdtor | Finvariant)))
    {   type *tfunc;
        tym_t cv;
        elem ethis = void;
        type *te;

        if (sfunc.Sfunc.Fflags & Fsurrogate)
        {
            // Type of parameter is 'conversion-type-id'
            tfunc = sfunc.Sfunc.Fsurrogatesym.Stype.Tnext;
            tfunc.Tcount++;
            te = tthis;
        }
        else
        {
            tfunc = newpointer(sfunc.Sscope.Stype);
            tfunc.Tcount++;

            // Add in cv-qualifiers from member function
            cv = sfunc.Stype.Tty & (mTYconst | mTYvolatile);
            if (cv)
                type_setty(&tfunc.Tnext,tfunc.Tnext.Tty | cv);
            te = newpointer(tthis);
        }

        debug ethis.id = elem.IDelem;
        ethis.Eoper = OPvar;
        ethis.Ecount = 0;
        ethis.ET = te;
        cpp_matchtypes(&ethis,tfunc,&m);
        //printf("1m: %x, ref=%x, toplevelcv=%x\n", m.m, m._ref, m.toplevelcv);
        any = 1;
        type_free(tfunc);
        te.Tcount++;
        type_free(te);
//      if (m.m <= TMATCHuserdef)       // can't use user-def conv on 'this'
        if (m.m == TMATCHnomatch)
            goto nomatch;
        wmatch = m;
    }
    ma[1] = wmatch;
    i = 2;

    al = arglist;
    p = functype.Tparamtypes;
    while (1)
    {
        if (al)
        {
            if (p)
            {   elem *e = list_elem(al);
                type_debug(p.Ptype);
version (none)
{
printf("-----\n");
//elem_print(e);
type_print(e.ET);
printf("-----\n");
type_print(p.Ptype);
printf("-----\n");
}
                cpp_matchtypes(e, p.Ptype, &m);
                //printf("2m: %x, ref=%x, toplevelcv=%x\n", m.m, m._ref, m.toplevelcv);
                //Outbuffer buf1,buf2;
                //printf("cpp_matchtypes:\t%s\n\t%s\n\t=x%x\n",type_tostring(&buf1,e.ET),type_tostring(&buf2,p.Ptype),m);
                if (m.m == TMATCHnomatch)
                {
                    // See if it could be a pointer to a template function,
                    // and can that template be instantiated for p.Ptype.Tnext?
                    if (e.Eoper == OPaddr &&
                        e.EV.E1.Eoper == OPvar &&
                        typtr(p.Ptype.Tty) && tyfunc(p.Ptype.Tnext.Tty))
                    {
                        Symbol *s;
                        Funcsym *so;

                        s = e.EV.E1.EV.Vsym;
                        if (s.Sclass == SCfuncalias &&
                            s.Sfunc.Falias.Sclass == SCfunctempl)
                            s = s.Sfunc.Falias;
                        if (s.Sclass != SCfunctempl)
                            goto nomatch;

                        for (so = s; so; so = so.Sfunc.Foversym)
                        {   Symbol *sf;

                            // BUG: need to check arg list for dependent types
                            //if (!checkSequence(so))
                                //continue;

                            sf = so;
                            if (sf.Sclass == SCfuncalias &&
                                sf.Sfunc.Falias.Sclass == SCfunctempl)
                                sf = sf.Sfunc.Falias;
                            if (sf.Sclass != SCfunctempl)
                                continue;
                            sf = template_matchfunc(sf,p.Ptype.Tnext.Tparamtypes,2,TMATCHexact,null);
                            if (sf)
                                break;
                        }
                        //so = cpp_findfunc(p.Ptype.Tnext, null, s, 3);
                        if (!so)
                            goto nomatch;
                        m.m = TMATCHexact;
                    }
                    else
                        goto nomatch;
                }
                if (!any || Match.cmp(m, wmatch) < 0)
                    wmatch = m;         /* retain worst match level     */
                ma[i++] = m;
                p = p.Pnext;
            }
            else
            {
                if (functype.Tflags & TFfixed)
                    goto nomatch;
                wmatch.m = TMATCHellipsis;
                wmatch.s = null;
                while (al)
                {
                    ma[i++] = wmatch;
                    al = list_next(al);
                }
                goto ret;
            }
            al = list_next(al);
        }
        else if (p)                     /* function must have more types */
        {
            if (!p.Pelem)
                goto nomatch;           /* remaining parameters are not defaults */
            *puseDefault = 1;           // used default parameters
            break;
        }
        else
            break;
        any = 1;
    }

ret:
    ma[0] = wmatch;
    //dbg_printf("cpp_matchfuncs: match = x%x\n",wmatch);
version (none)
{
    for (int j = 0; j < i; j++)
        printf("ma[%d] = x%x ", j, ma[j]);
    printf("\n");
}
    return wmatch;

nomatch:
    //dbg_printf("cpp_matchfuncs: nomatch\n");
    wmatch.m = TMATCHnomatch;
    wmatch.s = null;
    goto ret;
}


/*************************
 * Look for function symbol matching arglist.
 * The matched function must be unique, or a syntax error is generated.
 * Input:
 *      sfunc           the overloaded function symbol
 *      tthis           if member func, type of what 'this' points to
 *      arglist         list of arguments to function
 *      pmatch          where to store resulting match level
 *      pma[3]          if (pma) then where to store match levels of
 *                      first two arguments
 *      ptali           explicit template-argument-list for function templates
 *      flags           1: only match function templates
 *                      2: operator overload with only enums
 *                      |4: do not expand if match with template
 *                      |8: we don't have arglist
 *                      |0x10: this is a copy constructor
 *
 *      sfunc2
 *      tthis2          used for when looking for member functions too with
 *                      same arglist (without first argument)
 * Returns:
 *      s       symbol of matching function
 *      null    no match
 */

Symbol * cpp_lookformatch(Symbol *sfunc,type *tthis,
        list_t arglist,Match *pmatch,Symbol **pambig,match_t *pma,
        param_t *ptali,uint flags,
        Symbol *sfunc2, type *tthis2, Symbol *stagfriend = null)
{   Symbol *s;
    Match match,m;
    Symbol *ambig;
    Symbol *stemp;
    Match* ma,matmp;

debug
    Match[6] buffer = void;            // small to exercise bugs
else
    Match[40] buffer = void;           // room for 18 parameters

    int useDefault;
    int nargs;
    int dependentArglist = 0;   // 0: undetermined
                                // 1: is dependent
                                // 2: is not dependent
    param_t *pl = null;
    param_t *pl2 = null;
    param_t *plmatch = null;

enum LOG_LOOKFORMATCH = 0;

static if (LOG_LOOKFORMATCH)
{
    dbg_printf("cpp_lookformatch(sfunc = '%s' %p, flags = x%x)\n",sfunc ? cpp_prettyident(sfunc) : "null", sfunc, flags);
    printf("------------arglist:\n");
    for (list_t li = arglist; li; li = list_next(li))
        type_print(cast(type *)(list_elem(li).ET));
    printf("------------ptali:\n");
    for (param_t *p = ptali; p; p = p.Pnext)
        p.print();
    printf("------------\n\n");
}

version (none)
{
    for (s = sfunc; s; s = s.Sfunc.Foversym)
    {
        printf("\ts = %p\n", s);
    }
}

    s = null;
    stemp = null;
    ambig = null;
    match.m = TMATCHnomatch;
    match.s = null;

    // If some variable was cast into being a function, skip overload
    // checking.
    if (sfunc && !tyfunc(sfunc.Stype.Tty))
    {
        if (pmatch)
        {   pmatch.m = TMATCHexact;
            pmatch.s = null;
        }
        if (pambig)
            *pambig = null;
        return sfunc;
    }

    nargs = list_nitems(arglist) + 1;
    if ((nargs + 1) * 2 <= buffer.length)
        ma = &buffer[0];            // use existing buffer for speed
    else
        ma = cast(Match *) MEM_PARF_MALLOC((*ma).sizeof * (nargs + 1) * 2);
    matmp = ma + nargs + 1;

    for (int i = 0; i < 2; i++)
    {
static if (LOG_LOOKFORMATCH)
{
     printf("\tpass %d\n", i);
}
     LIST list;
     list_t sl;

     if (!sfunc)
        sl = null;
     else if (sfunc.Sclass == SCadl)
        sl = sfunc.Spath;
     else
     {  sl = &list;
        list.next = null;
        list.ptr = sfunc;
     }

     for (; sl; sl = list_next(sl))
     {
      Symbol *snext;

      sfunc = cast(Symbol *)list_ptr(sl);
      for ( ; sfunc; sfunc = snext)
      {
        symbol_debug(sfunc);
        snext = sfunc.Sfunc.Foversym;

static if (LOG_LOOKFORMATCH)
{
        printf("\tsfunc = %p:\n", sfunc);
}
        if (sfunc.Sfunc.Fflags3 & Foverridden)        // if overridden
            continue;                                   // skip it
        if (sfunc.Sfunc.Fflags & Finstance)           // if template instance
            continue;                                   // skip it
        if (sfunc.Sclass == SCfuncalias &&
            sfunc.Sfunc.Falias.Sclass == SCfunctempl)
            sfunc = sfunc.Sfunc.Falias;
        if (sfunc == s)                         // can happen with using-declarations
            continue;
        if (!checkSequence(sfunc))
        {
//printf("sfunc = '%s'\n", sfunc.Sident);
            if (dependentArglist == 0)
            {
                dependentArglist = 2;           // assume not dependent
                for (list_t al = arglist; al; al = list_next(al))
                {   elem *e = list_elem(al);

//type_print(e.ET);
                    int d = el_isdependent(e);
//printf("\tel_isdependent: %d\n", d);
                    if (d)
                    {
                        dependentArglist = 1;
                        break;
                    }
                }
            }
            if (dependentArglist == 2)
                continue;
        }
        if (sfunc.Sclass == SCfunctempl)       // if a function template
        {
            if (!pl)
            {   // Convert arglist into parameter list
                list_t al = arglist;
                for (pl = null; al; al = list_next(al))
                {   elem *e = list_elem(al);
                    param_t *p = param_append_type(&pl,e.ET);
                    //if (e.Eoper == OPconst)
                        p.Pelem = el_copytree(e);
                }
            }

            pl2 = pl;
            if (i)
                pl2 = pl.Pnext;

            param_t *ptal;
            m = template_deduce_ptal(tthis, sfunc, ptali, matmp, flags & 8, pl2, &ptal);

            // Make sure ptal was filled in
            for (param_t *p = ptal; p; p = p.Pnext)
            {
                if (!p.Ptype && !p.Pelem && !p.Psym)
                {
static if (LOG_LOOKFORMATCH)
{
                    printf("\tptal '%s' not filled in\n", p.Pident);
}
                    m.m = TMATCHnomatch;
                    m.s = null;
                    break;
                }
            }
            param_free(&ptal);

            /* This is so a copy constructor that exactly matches its
             * argument doesn't infinitely recurse.
             * For non-templates,
             *  X::X(X)
             * is already diagnosted as illegal.
             */
            if (flags & 0x10 && m.m == TMATCHexact)
            {   m.m = TMATCHnomatch;
                m.s = null;
            }

static if (LOG_LOOKFORMATCH)
{
            printf("\tt m = x%x\n", m.m);
}
        }
        else if (flags & 1)             // if function templates only
            continue;
        else
        {   list_t arglist2 = arglist;
            if (i)
                arglist2 = list_next(arglist);
            assert(tyfunc(sfunc.Stype.Tty));

            // CPP98 13.3.1.2 says that one operand must be an enum if neither
            // operand is a class and this is operator overloading.
            if (flags & 2 && i == 0)
            {
                param_t *p;

                for (p = sfunc.Stype.Tparamtypes; p; p = p.Pnext)
                {
                    type *t = p.Ptype;
                    if (tyref(t.Tty))
                        t = t.Tnext;
                    if (tybasic(t.Tty) == TYenum)
                        goto L5;
                }
                continue;
            L5: ;
            }

            m = cpp_matchfuncs(tthis,arglist2,sfunc,matmp, &useDefault);
static if (LOG_LOOKFORMATCH)
{
            printf("\tc m = x%x\n", m.m);
}
        }
        if (m.m != TMATCHnomatch)                       // if some sort of match
        {   int r;

            if (s)
            {
                // Two matches s and sfunc, figure out which is better
                if (match.m == TMATCHnomatch /*||
                    match.m == TMATCHexact ||
                    m.m == TMATCHexact*/)
                    r = m.m - match.m;
                else
                    r = match_cmp(matmp,ma,nargs);
                if (r == 0)
                {
                    if (1)
                    {   // Non-template functions are better than templates
                        if (s.Sclass == SCfunctempl && sfunc.Sclass != SCfunctempl)
                            goto Lsfunc;
                        if (s.Sclass != SCfunctempl && sfunc.Sclass == SCfunctempl)
                            goto Ls;

                        // If both are templates, use partial ordering rules
                        if (s.Sclass == SCfunctempl)
                        {
                            int c1 = template_function_leastAsSpecialized(s, sfunc, ptali);
                            int c2 = template_function_leastAsSpecialized(sfunc, s, ptali);

static if (LOG_LOOKFORMATCH)
{
                            printf("c1 = %d, c2 = %d\n", c1, c2);
}
                            if (c1 && !c2)      // s is more specialized
                                goto Ls;
                            else if (!c1 && c2) // sfunc is more specialized
                                goto Lsfunc;
                            else                // equally specialized
                                goto Lambig;    // which is ambiguous
                        }

                    Lambig:
static if (LOG_LOOKFORMATCH)
{
                        printf("\tambiguous\n");
}
                        if (!nspace_isSame(s, sfunc) || useDefault)
                            ambig = sfunc;      // ambiguous
                        continue;
                    }
                }
                if (r > 0)              // if sfunc is a better match than s
                    goto Lsfunc;

             Ls: // s is better
static if (LOG_LOOKFORMATCH)
{
                printf("\ts is better\n");
}
                continue;
            }
            else
            {
             Lsfunc: // sfunc is better
static if (LOG_LOOKFORMATCH)
{
                printf("\tsfunc is better\n");
}
                ambig = null;
                match = m;
                s = sfunc;
                plmatch = pl2;
                memcpy(ma,matmp,(nargs + 1) * (*ma).sizeof);
                continue;
            }
        }
      }
     }
     sfunc = sfunc2;
     tthis = tthis2;
     if (nargs)
     {  nargs--;
        memmove(ma + 1, ma + 2, nargs * (*ma).sizeof);
     }
    }

    if (ma != &buffer[0])                   // if we allocated our own buffer
        MEM_PARF_FREE(ma);

static if (LOG_LOOKFORMATCH)
{
    if (ambig)
        printf("\tambiguous, pambig = %p, ambig = %p, s = %p\n", pambig, ambig, s);
}

    if (pmatch)
        *pmatch = match;

    if (pambig)
        *pambig = ambig;
    else if (ambig)
        err_ambiguous(s,ambig);

    if (s && s.Sclass == SCfunctempl && !ambig && !(flags & 4))
    {   s = template_matchfunc(s, plmatch, 1 | (flags & 8), TMATCHexact, ptali, stagfriend);
        assert(s || errcnt);
    }
    param_free(&pl);

static if (LOG_LOOKFORMATCH)
{
    dbg_printf("cpp_lookformatch(%s) return %p\n",s ? cpp_prettyident(s) : "null",s);
}
    return s;
}


/********************************
 * For a class X, find any X::operator=() that takes an argument
 * of class X.
 */

Symbol *cpp_findopeq(Classsym *stag)
{
    elem e = void;
    list_t arglist;
    match_t match;
    Symbol *ambig;
    Symbol *sopeq;

    //printf("cpp_findopeq(%s)\n", cpp_prettyident(stag));
    template_instantiate_forward(stag);
    sopeq = n2_searchmember(stag,cpp_name_as.ptr);
    if (sopeq)
    {
        debug e.id = elem.IDelem;
        e.Eoper = OPvar;
        e.Ecount = 0;
        e.ET = stag.Stype;
        arglist = list_build(&e,null);

        cpp_overloadfunc(sopeq,null,arglist,&match,&ambig,null,4);
        list_free(&arglist,FPNULL);
        //printf("\tmatch = x%x\n", match);
        if (match <= TMATCHpromotions)
            sopeq = null;
    }
    return sopeq;
}


/************************************
 * Overload function, issue errors, check access rights.
 * Returns:
 *      function symbol (never null)
 */

Symbol *cpp_overload(
        Symbol *sf,             // function to be overloaded
        type *tthis,
        list_t arglist,         // arguments to function
        Classsym *sclass,       // type of object through which we are accessing smember
        param_t *ptal,          // template-argument-list for SCfuncdecl
        uint flags)         // 1: only look at SCfuncdecl
{
    Symbol *sfunc;
    Symbol *ambig;

    assert(sf);
    symbol_debug(sf);
    sfunc = cpp_overloadfunc(sf,tthis,arglist,null,&ambig,ptal,flags);
    if (!sfunc)
    {   sfunc = sf;
        if (sf.Sclass == SCadl)
            sfunc = cast(Symbol *)list_ptr(sf.Spath);
        err_nomatch(&sfunc.Sident[0],arglist);
    }
    else if (ambig)
        err_ambiguous(sfunc,ambig);     /* ambiguous match              */
    else if (isclassmember(sfunc) &&    // if sfunc is a member of a class
        sclass)                         // workaround BUG where sfunc is
                                        // a static member function, but no
                                        // sclass. See description in dofunc()
    {
        cpp_memberaccess(sfunc,funcsym_p,sclass);
    }
    return sfunc;
}


/************************************
 * Determine which function sfunc really is.
 *      1. If the symbol is not overloaded, then that symbol is it
 *      2. Look for symbol that is an exact match
 *      3. Look for match with a template
 *      4. Look for a symbol using built-in conversions
 *      5. Look for a symbol using user-defined conversions, if found
 *         it must be unique
 * Returns:
 *      if (pambig)
 *              if unique match
 *                      function symbol
 *                      *pambig = null
 *              if ambiguous match
 *                      function symbol and *pambig are the symbols
 *              if no match
 *                      null
 *      else
 *              if unique match
 *                      function symbol
 *              if ambiguous match
 *                      null
 *              if no match
 *                      null
 */

private Symbol * cpp_overloadfunc(Symbol *sfunc,type *tthis,
        list_t arglist,match_t *pmatch,Symbol **pambig,param_t *ptal, uint flags)
{   Symbol *s;
    match_t match;

    if (!pmatch)
        pmatch = &match;
    *pmatch = TMATCHnomatch;
    s = sfunc;
    if (s)
    {
        //dbg_printf("cpp_overloadfunc(%s)\n",sfunc.Sident);
        /* Look for exact match */
        Match m;
        s = cpp_lookformatch(sfunc,tthis,arglist,&m,pambig,null,ptal,flags,null,null);
        *pmatch = m.m;
        if (s)
        {
            //dbg_printf("\tFound match, %x\n", s.Sfunc.Fflags & Finstance);
            template_function_verify(s, arglist, ptal, TMATCHuserdef);
        }
        else if (!sfunc.Sfunc.Foversym  /* if symbol is not overloaded */
            && sfunc.Sclass != SCfunctempl     /* and it isn't a template */
            && sfunc.Sclass != SCadl
            )
        {
            s = sfunc;                  /* match it anyway              */
            debug assert(!pambig || !*pambig);
        }
    }
    return s;
}


/*********************************
 * Determine which overloaded function starting with sfunc matches
 * type t.
 * Input:
 *      td      ==0 look at typedefs only
 *              ==1 look at real functions only
 *              ==2 look at real functions and templates
 *              ==3 look at templates only; don't instantiate
 * Returns:
 *      s       matching func
 *      null    no matching func
 */


Funcsym *cpp_findfunc(type *t,param_t *ptpl, Symbol *s,int td)
{   Funcsym *sf;

    //printf("cpp_findfunc('%s',%d)\n",s.Sident,td);
    type_debug(t);
    if (s && !tyfunc(s.Stype.Tty))    // could happen from syntax errors
        s = null;
    for (sf = s; s; s = s.Sfunc.Foversym)
    {   symbol_debug(s);
        assert(s.Sfunc);
        if (s.Sfunc.Fflags3 & Foverridden)
            continue;
        switch (s.Sclass)
        {
            case SCtypedef:
                if (td != 0)
                    continue;
                break;
            case SCfunctempl:
                if (td == 0 || td == 1)
                    continue;
                break;
            default:
                if (td == 0)
                    continue;
                break;
        }
        if (cpp_funccmp(t, ptpl, s))
            goto L1;
    }

    if ((td == 2) && tyfunc(t.Tty))
    {
        int parsebody = (td == 2) ? 1 : 2;
        for (; sf; sf = sf.Sfunc.Foversym)
        {
            Funcsym *so = sf;

            if (sf.Sclass == SCfuncalias)
                so = sf.Sfunc.Falias;
            if (so.Sclass == SCfunctempl)
            {
                s = template_matchfunc(so,t.Tparamtypes,parsebody,TMATCHexact,null);
                if (s)
                {
                    //printf("cpp_findfunc() found %p '%s'\n", s, s.Sident);
                    goto L1;
                }
            }
        }
    }
L1:
    //printf("-cpp_findfunc()\n");
    return s;
}


/*********************************
 * Determine if two function types are exactly the same, as far
 * as overloading goes.
 * Input:
 *      t1      proposed new type (can be unprototyped)
 *      ptpl1   proposed new template-parameter-list
 *      s2      existing function symbol
 * Returns:
 *      0       not the same
 *      !=0     same
 */

int cpp_funccmp(Symbol *s1, Symbol *s2)
{
    return cpp_funccmp(s1.Stype,
                (s1.Sclass == SCfunctempl) ? s1.Sfunc.Farglist : s1.Sfunc.Fptal,
                s2);
}

int cpp_funccmp(type *t1, param_t *ptpl1, Symbol *s2)
{   param_t *p1;
    param_t *p2;
    type *t2 = s2.Stype;

version (none)
{
    printf("cpp_funccmp(t1 = %p, t2 = %p, s2 = '%s')\n", t1, t2, s2.Sident);
    type_print(t1); type_print(t2);
    if (ptpl1)
    {
        printf("ptpl1:\n");
        ptpl1.print_list();
    }
    if (s2.Sfunc.Farglist)
    {
        printf("ptpl2:\n");
        s2.Sfunc.Farglist.print_list();
    }
}

    type_debug(t1);
    type_debug(t2);
    assert(tyfunc(t1.Tty) && tyfunc(t2.Tty));

    // Check template-parameter-list's
    if (s2.Sclass == SCfunctempl)
    {
        p2 = s2.Sfunc.Farglist;

        // Use template_arglst_match() instead?
        for (p1 = ptpl1; p1; p1 = p1.Pnext)
        {
            if (!p2)
            {
                //printf("notsame1\n");
                goto notsame;
            }
            if (p1.Ptype)
            {
                if (!p2.Ptype)
                {
                    //printf("notsame2\n");
                    goto notsame;
                }
                if (!typematch(p1.Ptype, p2.Ptype, 0))
                {
                    //printf("notsame3\n");
                    goto notsame;
                }
            }
            else if (p2.Ptype)
            {
                //printf("notsame4\n");
                goto notsame;
            }
            p2 = p2.Pnext;
        }
        if (p2)
            goto notsame;
    }
    else if (ptpl1)
        goto notsame;

    if (s2.Sclass != SCfunctempl && ptpl1 && s2.Sfunc.Fptal)
    {
        if (!template_arglst_match(ptpl1, s2.Sfunc.Fptal))
            goto notsame;
    }

    /* Not the same if const and volatile differ        */
    if ((t1.Tty & (mTYconst | mTYvolatile)) !=
        (t2.Tty & (mTYconst | mTYvolatile)))
        goto notsame;

    if (!(t1.Tflags & TFprototype))
        goto same;

    if (t1.Tflags & TFfuncret && !typematch(t1.Tnext,t2.Tnext,0))
        goto notsame;

    /* Whether arg lists are variadic or not must be the same   */
    if ((t1.Tflags & TFfixed) != (t2.Tflags & TFfixed))
        goto notsame;

    /* Parameter lists must match */
    p2 = t2.Tparamtypes;
    for (p1 = t1.Tparamtypes; p1; p1 = p1.Pnext)
    {
        if (!p2)
            goto notsame;

version (all)
{
        type *t1x = p1.Ptype;
        type *t2x = p2.Ptype;
        int first = 1;

        if (tyref(t1x.Tty) != tyref(t2x.Tty))
            goto notsame;

        if (tyref(t1x.Tty))
        {   t1x = t1x.Tnext;
            first = 0;
        }
        if (tyref(t2x.Tty))
        {   t2x = t2x.Tnext;
            first = 0;
        }
        if (!first && (t1x.Tty ^ t2x.Tty) & (mTYconst | mTYvolatile))
        {   //printf("notsame5\n");
            goto notsame;
        }
        if (!cpp_typecmp(t1x, t2x, 0, ptpl1, s2.Sfunc.Farglist))
        {   //printf("notsame6\n");
            goto notsame;
        }
        if ((typtr(t1x.Tty) || tybasic(t1x.Tty) == TYarray) &&
            (t1x.Tnext.Tty ^ t2x.Tnext.Tty) & (mTYconst | mTYvolatile))
            goto notsame;

}
else
{
        elem e = void;
        e.ET = p1.Ptype;
        e.Eoper = OPvar;                /* kludge for parameter         */
        e.Ecount = 0;
        debug e.id = elem.IDelem;
        type_debug(p2.Ptype);
        if (cpp_matchtypes(&e,p2.Ptype) != TMATCHexact)
            goto notsame;
}
        p2 = p2.Pnext;
    }
    if (p2)
        goto notsame;
same:
    //dbg_printf("-cpp_funccmp(): same\n");
    return 1;

notsame:
    //dbg_printf("-cpp_funccmp(): notsame\n");
    return 0;
}


/********************************
 * Look to see if we should replace this operator elem with a
 * function call to an overloaded operator function.
 * Returns:
 *      if replaced with function call
 *              the new elem (e is free'd)
 *      else
 *              null
 */

elem *cpp_opfunc(elem *e)
{   list_t arglist;
    list_t al;
    Symbol *s;                          /* operator function symbol     */
    Symbol *ambig;
    Symbol *sm;
    Symbol *ambigm;
    elem *eret;
    elem *ethis;
    elem *efunc;
    type *t;
    elem *e2;
    type *tclass;
    match_t matchm;
    match_t[3] ma = void;
    match_t[3] mam = void;
    char *opident;
    uint op;
    uint flags;

    enum DBG = 0;

    op = e.Eoper;
    if (DBG) { WROP(op); dbg_printf(" cpp_opfunc() start\n"); }
    assert(e && !OTleaf(op));

    // At least one leaf must be a struct or a ref to a struct
    flags = 2;          // set to 0 if one argument is a struct
    {
        int i;

        type* tx = e.EV.E1.ET;
        for (i = 0; i < 2; i++)
        {
        Lagain:
            type_debug(tx);
            switch (tybasic(tx.Tty))
            {
                case TYstruct:
                    template_instantiate_forward(tx.Ttag);
                    flags = 0;
                    goto L3;

                case TYenum:
                    if (config.flags4 & CFG4enumoverload)
                        goto L3;
                    break;

                case TYref:
                    tx = tx.Tnext;
                    goto Lagain;

                default:
                    break;
            }
            if (!OTbinary(e.Eoper))
                break;
            tx = e.EV.E2.ET;
        }

        if (DBG) dbg_printf(" not found 1\n");
        return null;
    L3: ;
    }

    opident = cpp_opident(op);
    if (!opident)
    {   if (DBG) dbg_printf(" not found 2\n");
        return null;
    }

    if (op != OPaddr)           /* if not already taking address */
        e.EV.E1 = arraytoptr(e.EV.E1);

    // Also search for a member function
    ethis = e.EV.E1;
    tclass = ethis.ET;
    assert(!tyref(tclass.Tty));
    sm = null;
    if (type_struct(tclass))            // lvalue is a struct or ref to a struct
    {   Classsym *stag = tclass.Ttag;

        // Search for compatible operator function
        sm = cpp_findmember(stag,opident,false);
    }

    // Search for compatible operator function
    s = cpp_operfuncs[op];

    /* Construct list of arguments      */
    arglist = list_build(e.EV.E1,null);
    if (OTbinary(e.Eoper))
    {   e.EV.E2 = arraytoptr(e.EV.E2);
        type_debug(e.EV.E2.ET);
        if (op == OPcomma)
            el_settype(e,e.EV.E2.ET);
        list_append(&arglist,e.EV.E2);
    }

    // If there are namespaces, we must do ADL
    if (cpp_operfuncs_nspace[cast(uint)op / 32] & (1 << (op & 31)))
    {
        //printf("doing ADL lookup\n");
        s = scope_search(opident, SCTglobal /*| SCTnspace*/);
        s = adl_lookup(opident, s, arglist);
    }


    eret = null;
    Match m;
    s = cpp_lookformatch(s,null,arglist,&m,null,null,null,flags,sm,tclass);
    if (s)
    {   Match m2;

        m2 = cpp_builtinoperator(e);
        //printf("cpp_opfunc: match = x%x, match2 = x%x\n", m.m, m2.m);
        int result = Match.cmp(m, m2);
        if (result == 0)
        {
            // Built-in operators are better than templates
            if (!(s.Sfunc.Fflags & Finstance)) // if not template instance
            {
                err_ambiguous(s, null);
            }
            goto rete;
        }
        else if (result < 0)
            goto rete;          // m2 is a better match

        if (isclassmember(s))
        {   Symbol *sowner;
            Classsym *stag = tclass.Ttag;

            // Access check for stag::s
            if (!pstate.STinopeq)
                cpp_memberaccess(s,funcsym_p,stag);

            // Construct pointer to this
            list_subtract(&arglist,ethis);
            ethis = exp2_addr(ethis);
            sowner = s.Sscope;
            c1isbaseofc2(&ethis,sowner,stag);

            // Member functions could be virtual
            efunc = cpp_getfunc(sowner.Stype,s,&ethis);
            efunc = el_unat(OPind,efunc.ET.Tnext,efunc);

            /* Check for non-const function and const ethis     */
            if (ethis.ET.Tnext.Tty & (mTYconst | mTYvolatile) & ~s.Stype.Tty)
                typerr(EM_cv_arg,ethis.ET,s.Stype);   // type mismatch
        }
        else
        {
            efunc = el_var(s);
            ethis = null;
        }

        if (DBG) dbg_printf("found an operator overload function\n");

        e2 = xfunccall(efunc,ethis,null,arglist);
        arglist = null;
        e.EV.E1 = e.EV.E2 = null;
        el_free(e);
        eret = e2;
    }

rete:
    list_free(&arglist,FPNULL);         /* and dump the list            */
    if (DBG && !eret) dbg_printf(" not found 3\n");
    return eret;
}


/**********************************
 * Look for a unique user-defined conversion of e to a non-void*.
 */

elem *cpp_ind(elem *e)
{   Classsym *stag;
    Symbol *sm;
    list_t cl;

    if (type_struct(e.ET))
    {
        sm = null;
        stag = e.ET.Ttag;
        for (cl = stag.Sstruct.Scastoverload; cl; cl = list_next(cl))
        {   Symbol *sc;
            type *tc;

            sc = list_symbol(cl);
            symbol_debug(sc);
            tc = sc.Stype.Tnext;              // function return type
            if (tyref(tc.Tty))
                tc = tc.Tnext;
            if (!typtr(tc.Tty) || tybasic(tc.Tnext.Tty) == TYvoid)
                continue;
            if (sm)
            {
                err_ambiguous(sm,sc);
                break;
            }
            sm = sc;
        }

        if (sm)
        {
            Symbol *sowner;
            elem *ethis;
            elem *efunc;

            /* Construct pointer to this        */
            ethis = exp2_addr(e);
            sowner = sm.Sscope;
            c1isbaseofc2(&ethis,sowner,stag);

            /* Member functions could be virtual        */
            efunc = cpp_getfunc(sowner.Stype,sm,&ethis);
            efunc = el_unat(OPind,efunc.ET.Tnext,efunc);

            /* Check for non-const function and const ethis     */
            if (ethis.ET.Tnext.Tty & (mTYconst | mTYvolatile) & ~sm.Stype.Tty)
                typerr(EM_cv_arg,ethis.ET,sm.Stype);  /* type mismatch */
            e = xfunccall(efunc,ethis,null,null);
        }
    }
    return e;
}


/*********************************
 * Determine if function sfunc is a friend or a member of class sclass.
 * Returns:
 *      !=0     is a friend or member
 *      0       is not a friend
 */

enum LOG_FUNCISFRIEND = 0;

int cpp_funcisfriend(Symbol *sfunc,Classsym *sclass)
{
    if (sfunc)
    {   Classsym *fclass = sfunc.Sfunc.Fclass;

        symbol_debug(sfunc);
        symbol_debug(sclass);

        if (LOG_FUNCISFRIEND)
            printf("cpp_funcisfriend(func %s,class %s)\n",&sfunc.Sident[0],&sclass.Sident[0]);

        /* If sfunc is a member of sclass, we get private access        */
        if (sclass == fclass)
        {
            if (LOG_FUNCISFRIEND)
                printf("\tcpp_funcisfriend() sclass == fclass\n");

            return 1;
        }

        /* If sfunc is a friend of sowner, we get private access        */
        else
        {
            while (1)
            {
                for (list_t tl = sfunc.Sfunc.Fclassfriends; tl; tl = list_next(tl))
                {
                    if (LOG_FUNCISFRIEND)
                        printf("\tFclassfriends = '%s'\n", &list_symbol(tl).Sident[0]);

                    if (sclass == list_symbol(tl))
                        return 1;
                }
                // If sfunc was generated from a function template, check
                // the friends of that function template
                if (sfunc.Sfunc.Fflags & Finstance)
                {
                    if (LOG_FUNCISFRIEND)
                        printf("\tsfunc is an instance of a function template\n");
                    sfunc = sfunc.Sfunc.Ftempl;
                }
                else
                    break;
            }
        }
        if (LOG_FUNCISFRIEND)
            printf("\tcpp_funcisfriend() = 0\n");
    }
    return 0;
}


/**********************************
 * Determine if class s is the same or a friend of sclass.
 * Returns:
 *      !=0     is a friend
 *      0       is not a friend
 */

int cpp_classisfriend(Classsym *s,Classsym *sclass)
{   list_t tl;

    //printf("cpp_classisfriend('%s', '%s')\n", s.Sident, sclass.Sident);
    if (s)
        symbol_debug(s);
    symbol_debug(sclass);
    if (s == sclass)
        return 1;
    for (tl = sclass.Sstruct.Sfriendclass; tl; tl = list_next(tl))
        if (s == list_symbol(tl))
            return 1;

    // If both are instances of the same template, then it is a friend
    if (s && s.Sstruct.Stempsym && s.Sstruct.Stempsym == sclass.Sstruct.Stempsym)
        return 1;

    return 0;
}


/**********************************
 * Find member of class. Do not worry about access.
 * Worry about ambiguities.
 * Input:
 *      sclass  class to search for member in
 *      ident   identifier of member
 *      flag    1 if issue error message for not found
 * Returns:
 *      pointer to member symbol if found
 *      null    member not found
 */

private Symbol * cpp_findmemberx (Classsym *sclass,const(char)* sident,uint flag,Symbol **psvirtual);

Symbol *cpp_findmember(Classsym *sclass,const(char)* sident,uint flag)
{   Symbol *svirtual;

    symbol_debug(sclass);
    //dbg_printf("cpp_findmember(%s::%s)\n",sclass.Sident,sident);
    return cpp_findmemberx(sclass,sident,flag,&svirtual);
}


/************************************************
 * Same as cpp_findmember(), but look at enclosing class scopes.
 * Input:
 *      flag    1 issue error message for not found
 *              2 do not look in dependent base classes
 * Output:
 *      if found, *psclass is set to the enclosing most derived class name.
 * Returns:
 *      pointer to member symbol if found
 *      null    member not found
 */

Symbol *cpp_findmember_nest(Classsym **psclass,const(char)* sident,uint flag)
{   Symbol *svirtual;
    Classsym *sclass;
    Symbol *smember = null;

    //printf("cpp_findmember_nest('%s','%s', flag=%x)\n",(*psclass).Sident,sident,flag);
    for (sclass = *psclass; sclass; sclass = cast(Classsym *)sclass.Sscope)
    {
        symbol_debug(sclass);
        if (sclass.Sclass != SCstruct)
            break;
        /*dbg_printf("cpp_findmember_nest(%s::%s)\n",sclass.Sident,sident);*/
        smember = cpp_findmemberx(sclass,sident,flag & 2,&svirtual);
        if (smember)
        {   *psclass = sclass;
            break;
        }
    }
    if (flag & 1 && !smember && *psclass)
    {

        extern (D) void *cpp_findmember_nest_fp(const(char)* id, ref int cost)
        {
            return cpp_findmember_nest(psclass, id, 0);
        }

        Symbol *s = cast(Symbol *)speller(sident, &cpp_findmember_nest_fp, &idchars[0]);
        err_notamember(sident, *psclass, s);    // not a member of sclass
    }
    return smember;
}

private Symbol * cpp_findmemberx(Classsym *sclass,const(char)* sident,
        uint flag,Symbol **psvirtual)
{   Symbol *s;
    struct_t *st;

    assert(sclass);
    symbol_debug(sclass);

    //dbg_printf("cpp_findmemberx(%s::%s, flag = x%x)\n",sclass.Sident, sident, flag);
    template_instantiate_forward(sclass);
    st = sclass.Sstruct;
    assert(st);
    *psvirtual = null;
    s = n2_searchmember(sclass,sident);
    if (!s)
    {   baseclass_t *b;
        Symbol *stmp;
        Symbol *svirtual;

        //if (flag & 2) printf("test3\n");
        //printf("\tsearching base classes\n");
        for (b = st.Sbase; b; b = b.BCnext)
        {
            // Do not search dependent base classes
//printf("\ttest1: %x, %p == %p\n", b.BCflags & BCFdependent, sclass, pstate.STclasssym);
            if (b.BCflags & BCFdependent &&
                (/*sclass == pstate.STclasssym ||*/ flag & 2) &&
                config.flags4 & CFG4dependent)
                continue;

            stmp = cpp_findmemberx(b.BCbase,sident,0,&svirtual);
            if (stmp)
            {
                if (b.BCflags & BCFvirtual && !svirtual)
                    svirtual = b.BCbase;
                if (stmp == s)
                {
                    //printf("\tfound same symbol\n");
                    /* Found same symbol by two different paths.
                     * Must be accessed through same virtual base.
                     */
                    if (s.needThis() && (!svirtual || svirtual != *psvirtual))
                    {
                        err_ambiguous(s,stmp);  /* ambiguous reference  */
                    }
                }
                else if (s)
                {
                    Symbol *sc = s.Sscope;
                    Symbol *sctmp = stmp.Sscope;

//printf("sc = '%s', sctmp = '%s'\n", sc.Sident, sctmp.Sident);
//if (svirtual) printf("svirtual = '%s'\n", svirtual.Sident);
//if (*psvirtual) printf("*psvirtual = '%s'\n", (*psvirtual).Sident);

                    if (svirtual && !*psvirtual)
                    {
                        if (c1isbaseofc2(null,svirtual,sc))
                            continue;           // pick s
                    }

                    if (*psvirtual && !svirtual)
                    {
                        if (c1isbaseofc2(null,*psvirtual,sctmp))
                            goto L1;            // pick stmp
                    }

                    if (*psvirtual && svirtual)
                    {
                        if (c1isbaseofc2(null,sc,sctmp))
                        {   /* stmp dominates s */
                            //printf("\t1: %s dominates %s\n", sctmp.Sident, sc.Sident);
                            goto L1;
                        }
                        else if (c1isbaseofc2(null,sctmp,sc))
                        {   /* s dominates stmp */
                            //printf("\t2: %s dominates %s\n", sc.Sident, sctmp.Sident);
                            continue;
                        }
                    }

                    //printf("\tcheck if ambiguous\n");
                    /* Determine if either s dominates stmp or
                        stmp dominates s. Otherwise, the match
                        is ambiguous.
                     */
                    //if (c1isbaseofc2(null,sc,sctmp))
                    if (c1dominatesc2(sclass,sctmp,sc))
                    {   /* stmp dominates s     */
                        //printf("\t1: %s dominates %s\n", sctmp.Sident, sc.Sident);
                        goto L1;
                    }
                    //else if (c1isbaseofc2(null,sctmp,sc))
                    else if (c1dominatesc2(sclass,sc,sctmp))
                    {   /* s dominates stmp     */
                        //printf("\t2: %s dominates %s\n", sc.Sident, sctmp.Sident);
                    }
                    else
                        err_ambiguous(s,stmp);  /* ambiguous reference to s */
                }
                else
                {
                L1: s = stmp;
                    *psvirtual = svirtual;
                }
            }
        }
    }
    if (flag & 1 && !s)
        err_notamember(sident,sclass);  // not a member of sclass
    //dbg_printf("\tdone, s.Sident = '%s'\n",s ? s.Sident : "null");
    return s;
}


/**********************************
 * Determine access level to member smember from class sclass.
 * Assume presence of smember and ambiguity checking are already done.
 * Input:
 *      smember member we are checking access to
 *      sclass  type of object through which we are accessing smember
 * Returns:
 *      SFLxxxx
 */

int cpp_findaccess(Symbol *smember,Classsym *sclass)
{
    struct_t *st;
    uint access_ret;

    assert(sclass);
    symbol_debug(sclass);
    symbol_debug(smember);

    st = sclass.Sstruct;
    assert(st);
    /*dbg_printf("cpp_findaccess %s::%s\n",sclass.Sident,smember.Sident);*/
    if (smember.Sscope == sclass)
    {
        access_ret = smember.Sflags & SFLpmask;
    }
    else
    {   baseclass_t *b;
        uint access;

        access_ret = SFLnone;
        for (b = st.Sbase; b; b = b.BCnext)
        {
            access = cpp_findaccess(smember,b.BCbase);
            if (access == SFLprivate)
                access = SFLnone;       /* private members of base class not accessible */

            if (access != SFLnone)
            {
                /* If access is not to be left unchanged        */
                if (!list_inlist(b.BCpublics,smember))
                {   /* Modify access based on derivation access. ARM 11.2 */
                    switch (b.BCflags & BCFpmask)
                    {   case BCFprivate:
                            access = SFLprivate;
                            break;
                        case BCFprotected:
                            access = SFLprotected;
                            break;
                        default:
                            break;
                    }
                }

                /* Pick path with loosest access        */
                if (access_ret == SFLnone || access < access_ret)
                    access_ret = access;
            }

        }
    }
    /*dbg_printf("cpp_findaccess = x%x\n",access_ret);*/
    return access_ret;
}


/**********************************
 * Determine if we have access to member smember.
 * Assume presence of smember and ambiguity checking are already done.
 * Input:
 *      smember member we are checking access to
 *      sfunc   function we're in (null if not in a function)
 *      sclass  type of object through which we are accessing smember
 */

enum LOG_MEMBERACCESS = 0;

/* Helper function for cpp_memberaccess()       */
private int cpp_memberaccessx(Symbol *smember,Symbol *sfunc,Classsym *sclass)
{
    struct_t *st;

    assert(sclass);
    symbol_debug(sclass);
    symbol_debug(smember);

static if (LOG_MEMBERACCESS)
{
    dbg_printf("cpp_memberaccessx for %s::%s in function %s() in scope %s\n",
        sclass.Sident,smember.Sident,
        sfunc ? sfunc.Sident : null,
        pstate.STclasssym ? pstate.STclasssym.Sident : null);
}
    st = sclass.Sstruct;
    assert(st);
    if (cpp_funcisfriend(sfunc,sclass) ||
        cpp_classisfriend(pstate.STclasssym,sclass))
    {
        if (smember.Sscope == sclass)
            return 1;
        else
        {   baseclass_t *b;
            uint access;

            for (b = st.Sbase; b; b = b.BCnext)
            {
                access = cpp_findaccess(smember,b.BCbase);
                if (access == SFLpublic || access == SFLprotected ||
                    cpp_memberaccessx(smember,sfunc,b.BCbase)
                   )
                    return 1;

            }
        }
    }
    else
    {
        if (smember.Sscope != sclass)
        {   baseclass_t *b;

            for (b = st.Sbase; b; b = b.BCnext)
            {
                if (cpp_memberaccessx(smember,sfunc,b.BCbase))
                    return 1;

            }
        }
    }
    return 0;
}


/*************************
 * Returns:
 *      !=0     accessible
 *      0       not accessible
 */

int cpp_memberaccesst(Symbol *smember,Symbol *sfunc,Classsym *sclass)
{
    struct_t *st;
    int result;

    if (!smember)
        return 1;
    symbol_debug(smember);
    if (!isclassmember(smember))        // if not a member of a class
        return 1;                       // then it is accessible
    assert(sclass);
    symbol_debug(sclass);

static if (LOG_MEMBERACCESS)
{
    dbg_printf("cpp_memberaccess for %s::%s in function %s() in scope %s\n",
        sclass.Sident,smember.Sident,
        sfunc ? sfunc.Sident : null,
        pstate.STclasssym ? &pstate.STclasssym.Sident[0] : null);
}

    assert(tybasic(sclass.Stype.Tty) == TYstruct);
    assert(!sfunc || tyfunc(sfunc.Stype.Tty));
    assert(c1isbaseofc2(null,smember.Sscope,sclass));
    st = sclass.Sstruct;
    assert(st);
    if (smember.Sscope == sclass)
    {
        result = (smember.Sflags & SFLpmask) == SFLpublic ||
                cpp_funcisfriend(sfunc,sclass) ||
                cpp_classisfriend(pstate.STclasssym,sclass);
static if (LOG_MEMBERACCESS)
{
        printf("\tcpp_memberaccess1: result = %d\n", result);
        if (1)
        {
            printf("\t\tSflags == SFLpublic = %d\n", (smember.Sflags & SFLpmask) == SFLpublic);
            printf("\t\tcpp_funcisfriend() = %d\n", cpp_funcisfriend(sfunc,sclass));
            printf("\t\tcpp_classisfriend() = %d\n", cpp_classisfriend(pstate.STclasssym,sclass));
        }
}
        if (!result)
        {
            /* If we're a static in a function that is a friend of class X that has
             * protected access to sclass, allow it.
             */
            if (sfunc && !smember.needThis())
            {
                for (list_t tl = sfunc.Sfunc.Fclassfriends; tl; tl = list_next(tl))
                {   Classsym *sx = cast(Classsym *)list_symbol(tl);

                    if (sx != sclass)
                    {
                        result = cpp_memberaccesst(smember, sfunc, sx);
                        if (result)
                            break;
                    }
                }
            }
        }

    }
    else if (cpp_findaccess(smember,sclass) == SFLpublic)
    {
        result = 1;
    }
    else
    {
static if (LOG_MEMBERACCESS)
{
        printf("\tcpp_memberaccess2\n");
}
        result = cpp_memberaccessx(smember,sfunc,sclass);
    }
    return result;
}


void cpp_memberaccess(Symbol *smember,Symbol *sfunc,Classsym *sclass)
{
    if (!cpp_memberaccesst(smember, sfunc, sclass))
    {
        char *p = cast(char *) MEM_PARF_STRDUP(cpp_prettyident(smember));

        cpperr(EM_not_accessible,p,cpp_prettyident(sclass));        // no access to member
        MEM_PARF_FREE(p);
    }
}



/************************************
 * Determine type of 'this' for member function.
 * Type is void if static member function.
 */

type *cpp_thistype(type *tfunc,Classsym *stag)
{   type *t;
    tym_t tym,modifiers;

    type_debug(tfunc);
    t = newpointer(stag.Stype);
    /* Pull in const and volatile from function type    */
    modifiers = (tfunc.Tty & (mTYconst | mTYvolatile));
    type_setty(&t.Tnext,stag.Stype.Tty | modifiers);
    tym = stag.Sstruct.ptrtype;
    assert(typtr(tym));
    t.Tty = tym;
    t.Tcount++;
    return t;
}

/**********************************
 * Define 'this' as the first parameter to member function sfunc.
 */

Symbol *cpp_declarthis(Symbol *sfunc,Classsym *stag)
{   Symbol *s;
    type *t;
    tym_t tym,modifiers;

    //dbg_printf("cpp_declarthis(%p, '%s')\n",sfunc, sfunc.Sident);
    assert(level == 1);                 /* must be at parameter level   */
version (all)
    s = scope_define(cpp_name_this.ptr,SCTlocal,SCparameter);
else
{
    s = scope_define(cpp_name_this.ptr,SCTlocal,
        (tybasic(sfunc.Stype.Tty) == TYmfunc)
        ? SCfastpar : SCparameter);
}
    s.Stype = cpp_thistype(sfunc.Stype,stag);
    return s;
}


/***********************************
 * Given a pointer to a class, adjust the type of the pointer to
 * be a pointer to another class.
 * Do not do any user-defined conversions or offset fiddling.
 */

elem *cpp_fixptrtype(elem *e,type *tclass)
{   tym_t ptr;

    ptr = tclass.Ttag.Sstruct.ptrtype;
    if (ptr != tybasic(e.ET.Tty))     // if we need to convert
    {   type *t;

        t = type_allocn(ptr | (e.ET.Tty & ~mTYbasic),e.ET.Tnext);
        e = _cast(e,t);
    }
    el_settype(e,newpointer(tclass));
    return e;
}


/************************************
 * Return address of vtable.
 */

elem *cpp_addr_vtable(Classsym *stag)
{   Symbol *svptr;
    struct_t *st;
    elem *ev;
    SC scvtbl;

    st = stag.Sstruct;
    svptr = st.Svptr;
    assert(svptr);
    scvtbl = cast(SC) (config.flags2 & CFG2comdat) ? SCcomdat :
             (st.Sflags & STRvtblext) ? SCextern : SCstatic;
    n2_genvtbl(stag,scvtbl,0);          // make sure vtbl[]s exist

    /* ev = &_vtbl+offset       */
    ev = el_var(st.Svtbl);
    // Account for offset due to RTTI
    if (config.flags3 & CFG3rtti)
        ev.EV.Voffset += _tysize[st.ptrtype];

    ev = el_unat(OPaddr,svptr.Stype,ev);
    return ev;
}


/***********************************
 * Return boolean expression if e is of type t.
 * Currently works only if e is an instance of a class, and t is that
 * class, and both have virtual functions.
 * No checking is done for other cases.
 */

elem *cpp_istype(elem *e, type *t)
{   Symbol *svptr;
    Classsym *stag;
    elem* emos,ev;
    elem *ec;
    type *tclass;
    tym_t tym;
    struct_t *st;
    baseclass_t *b;
    SC scvtbl;

    e = el_unat(OPaddr, type_ptr(e, e.ET), e);
    tym = e.ET.Tty;
    tclass = t;
    assert(tybasic(tclass.Tty) == TYstruct);
    stag = tclass.Ttag;
    st = stag.Sstruct;
    svptr = st.Svptr;

    /*  If any of the virtual functions are pure, then
        can't be an instance.
     */
    if (!svptr || n2_anypure(st.Svirtual) == 2)
    {
        return el_combine(e,el_longt(tslogical, 0));
    }

    symbol_debug(svptr);
    emos = el_longt(tstypes[TYint],svptr.Smemoff);
    /* Account for offset due to reuse of primary base class vtbl */
    if (st.Sprimary && st.Sprimary.BCbase.Sstruct.Svptr == st.Svptr)
        emos.EV.Voffset = st.Sprimary.BCoffset;
    t = type_allocn(tym,svptr.Stype);  // match pointer type of ethis
    e = el_bint(OPadd,t,e,emos);        // ethis + mos
    e = el_unat(OPind,svptr.Stype,e);  /* *(ethis + mos)               */

    ev = cpp_addr_vtable(stag);
    ec = el_bint(OPeqeq,tslogical,e,ev);
    return ec;
}


/***********************************
 * Given the symbol for the this pointer, construct an elem
 *      *(this + _vptr) = &_vtbl
 * and return it. This is the initialization of the virtual array pointer.
 * Input:
 *      ctor    !=0 if constructor
 *              0 if destructor
 * Returns:
 *      e       vptr assignment expression
 *      null    no vptr assignment
 */

private elem * cpp_assignvptr(Symbol *s_this,int ctor)
{   Symbol *svptr;
    Classsym *stag;
    elem* emos,ev;
    elem *e;
    elem *ec;
    type *tclass;
    type *t;
    tym_t tym;
    struct_t *st;
    baseclass_t *b;
    char genvtbl;
    SC scvtbl;

    symbol_debug(s_this);
    tym = s_this.Stype.Tty;
    assert(typtr(tym));
    tclass = s_this.Stype.Tnext;
    assert(tybasic(tclass.Tty) == TYstruct);
    stag = tclass.Ttag;
    st = stag.Sstruct;
    svptr = st.Svptr;
    scvtbl = cast(SC) (config.flags2 & CFG2comdat) ? SCcomdat :
             (st.Sflags & STRvtblext) ? SCextern : SCstatic;

    /* If any of the virtual functions are pure, then optimize
       by not assigning vptr.
     */
    if (!svptr || n2_anypure(st.Svirtual) == 2)
    {   ec = null;
        genvtbl = false;
        goto L2;
    }

    symbol_debug(svptr);
    emos = el_longt(tstypes[TYint],svptr.Smemoff);
    /* Account for offset due to reuse of primary base class vtbl */
    if (st.Sprimary && st.Sprimary.BCbase.Sstruct.Svptr == st.Svptr)
        emos.EV.Voffset = st.Sprimary.BCoffset;
    t = type_allocn(tym,svptr.Stype);  // match pointer type of ethis
    e = el_bint(OPadd,t,el_var(s_this),emos);   // ethis + mos
    e = el_unat(OPind,svptr.Stype,e);  /* *(ethis + mos)               */

    genvtbl = true;

    ev = cpp_addr_vtable(stag);
    ec = el_bint(OPeq,e.ET,e,ev);

L2:
    /* Do vptrs for base classes        */
    for (b = st.Smptrbase; b; b = b.BCnext)
    {   baseclass_t *vb;
        Classsym *sbase;
        targ_int vptroffset;

        if (!(b.BCflags & BCFnewvtbl))
        {
            if (!(config.flags3 & CFG3rtti) || !b.BCbase.Sstruct.Svptr)
                continue;
        }

        /* If any of the virtual functions are pure, then optimize
           by not assigning vptr.
         */
        sbase = b.BCbase;
        symbol_debug(sbase);
        svptr = sbase.Sstruct.Svptr;
        assert(svptr);
        if (!svptr)
            continue;
        symbol_debug(svptr);

        if (n2_anypure(b.BCmptrlist) == 2)
            continue;

        // If base doesn't need a vtbl, don't generate one

        if (!genvtbl)                   /* if vtbls not generated yet   */
        {   n2_genvtbl(stag,scvtbl,0);  // make sure vtbl[]s exist
            genvtbl = true;
        }

        t = type_allocn(tym,svptr.Stype);
        vptroffset = svptr.Smemoff;

        if (b.BCflags & BCFvirtual)            /* if base class is virtual */
        {
            e = el_var(s_this);
            e = exp2_ptrvbaseclass(e,stag,sbase);
        }
        else
        {
            baseclass_t *bn;
            symlist_t sl;
            Symbol *s2;

            e = el_var(s_this);
            sl = null;
            for (bn = b; bn; bn = bn.BCpbase)
            {
                list_prepend(&sl,bn.BCbase);
            }
            s2 = stag;
            while (sl)
            {   Classsym *s;

                s = list_Classsym(sl);
                c1isbaseofc2(&e,s,s2);
                s2 = s;
                list_pop(&sl);
            }
        }

        emos = el_longt(tstypes[TYint],vptroffset);
        e = el_bint(OPadd,t,e,emos);
        e = el_unat(OPind,svptr.Stype,e);      /* *(&e + mos)          */

        /* ev = &_vtbl  */
        ev = el_var(b.BCvtbl);
        // Account for offset due to RTTI
        if (config.flags3 & CFG3rtti)
            ev.EV.Voffset += _tysize[st.ptrtype];

        ev = el_unat(OPaddr,newpointer(sbase.Stype),ev);

        e = el_bint(OPeq,e.ET,e,ev);

        ec = el_combine(e,ec);
    }

    return ec;
}


/***********************************
 * Given the symbol for the this pointer, construct an elem
 *      *(this + _vbptr) = &_vbtbl
 * and return it. This is the initialization of the virtual base array pointer.
 * Returns:
 *      e       vbptr assignment expression
 *      null    no vbptr assignment
 */

private elem * cpp_assignvbptr(Symbol *s_this)
{   Symbol *svptr;
    Classsym *stag;
    elem* emos,ev;
    elem *e;
    elem *ec;
    type *tclass;
    type *t;
    tym_t tym;
    struct_t *st;
    baseclass_t *b;
    SC scvtbl;

    symbol_debug(s_this);
    tym = s_this.Stype.Tty;
    assert(typtr(tym));
    tclass = s_this.Stype.Tnext;
    assert(tybasic(tclass.Tty) == TYstruct);
    stag = tclass.Ttag;
    //dbg_printf("cpp_assignvbptr for '%s'\n",stag.Sident);
    st = stag.Sstruct;
    svptr = st.Svbptr;
    scvtbl = cast(SC) (config.flags2 & CFG2comdat) ? SCcomdat :
             (st.Sflags & STRvtblext) ? SCextern : SCstatic;

    symbol_debug(svptr);
    emos = el_longt(tstypes[TYint],st.Svbptr_off);
    t = type_allocn(tym,svptr.Stype);  // match pointer type of ethis
    e = el_bint(OPadd,t,el_var(s_this),emos);   // ethis + mos
    e = el_unat(OPind,svptr.Stype,e);  /* *(ethis + mos)               */

    n2_genvbtbl(stag,scvtbl,0);         // make sure vtbl[]s exist

    /* ev = &_vtbl+offset       */
    ev = el_var(st.Svbtbl);
    ev = el_unat(OPaddr,e.ET,ev);

    ec = el_bint(OPeq,e.ET,e,ev);

    // Do vbptrs for base classes
    for (b = st.Svbptrbase; b; b = b.BCnext)
    {   baseclass_t *vb;
        Classsym *sbase;
        targ_int vptroffset;

        sbase = b.BCbase;
        symbol_debug(sbase);
        svptr = sbase.Sstruct.Svbptr;
        assert(svptr);
        symbol_debug(svptr);

        t = type_allocn(tym,svptr.Stype);
        //dbg_printf("b.BCoffset = x%lx, sbase('%s').Svbptr_off = x%lx\n",b.BCoffset,sbase.Sident,sbase.Sstruct.Svbptr_off);
        vptroffset = b.BCoffset + sbase.Sstruct.Svbptr_off;
        e = el_var(s_this);
        emos = el_longt(tstypes[TYint],vptroffset);
        e = el_bint(OPadd,t,e,emos);
        e = el_unat(OPind,svptr.Stype,e);      /* *(&e + mos)          */

        /* ev = &_vtbl  */
        ev = el_var(b.BCvtbl);
        ev = el_unat(OPaddr,newpointer(tstypes[TYint]),ev);

        e = el_bint(OPeq,e.ET,e,ev);

        ec = el_combine(ec,e);
    }

    return ec;
}


/***********************************
 * Determine offset of virtual function sfunc from the start of the
 * vtbl[] for class sclass.
 */

int cpp_vtbloffset(Classsym *sclass,Symbol *sfunc)
{   int i;
    int mptrsize;
    list_t vl;

    symbol_debug(sclass);
    symbol_debug(sfunc);

    //dbg_printf("cpp_vtbloffset('%s','%s')\n",sclass.Sident,cpp_prettyident(sfunc));
    cpp_getpredefined();                        /* define s_mptr        */
    mptrsize = type_size(s_mptr.Stype);

    assert(isclassmember(sfunc));
    if (sfunc.Sscope.Sstruct.Sscaldeldtor == sfunc)
    {   sfunc = sfunc.Sscope.Sstruct.Sdtor;
        //dbg_printf("dtor '%s','%s'\n",sfunc.Sident,cpp_prettyident(sfunc));
    }

    /* Compute offset from start of virtual table for function sfunc */
    i = -mptrsize;      // no null at start of vtbl[]
    for (vl = sclass.Sstruct.Svirtual; ; vl = list_next(vl))
    {   mptr_t *m;

        i += mptrsize;
        assert(vl);
        m = list_mptr(vl);
        //dbg_printf("vl = x%lx, sym = x%lx, '%s' ty = x%x\n",
        //    vl,m.MPf,m.MPf.Sident,m.MPf.Stype.Tty);
        symbol_debug(m.MPf);
        assert(tyfunc(m.MPf.Stype.Tty));
        if (sfunc == m.MPf)
            break;
    }
    return i;
}


/*********************************
 * Get pointer to a function given:
 *      tclass  Class function is a member of (not the same as sfunc.Sscope
 *              in the case where a derived class uses the same virtual
 *              function as its base class)
 *      sfunc   The function symbol
 *      ethis   Pointer to object that is an instance of tclass
 * Returns:
 *      Expression tree that is a function pointer to the real function or
 *      is a lookup of the virtual function pointer.
 *      *pethis is modified to reflect adjusted ethis for virtual function
 *      call.
 */

elem * cpp_getfunc(type *tclass,Symbol *sfunc,elem **pethis)
{   elem *pfunc;
    elem *ethis = *pethis;
    Classsym *stag;

    //dbg_printf("cpp_getfunc()\n");
    type_debug(tclass);
    stag = tclass.Ttag;
    assert(stag);
    symbol_debug(sfunc);
    assert(sfunc);

    if (sfunc.Sfunc.Fflags & Fvirtual)
    {   int i;
        Symbol *svptr;
        elem *e;
        struct_t *st = stag.Sstruct;
        tym_t tym;
        type *tsy;

        if (!ethis)
        {
            cpperr(EM_no_instance,&stag.Sident[0]);        // no this for class
            goto L1;
        }
        elem_debug(ethis);
        tym = ethis.ET.Tty;
        assert(typtr(tym) &&
               tybasic(ethis.ET.Tnext.Tty) == TYstruct);

        /* See if we can call function directly */
        ethis = poptelem(ethis);
        if (ethis.Eoper == OPrelconst && ethis.EV.Voffset == 0 &&
            tybasic((tsy = ethis.EV.Vsym.Stype).Tty) == TYstruct &&
            tsy.Ttag == stag)
        {   *pethis = ethis;
            goto L1;
        }

        /* We can call function directly if we are in a ctor or dtor
           and ethis is "this"
         */
        if (funcsym_p && funcsym_p.Sfunc.Fflags & (Fctor | Fdtor) &&
            ethis.Eoper == OPvar &&
            ethis.ET.Tnext == funcsym_p.Sscope.Stype &&
            !strcmp(&ethis.EV.Vsym.Sident[0],cpp_name_this.ptr))
        {   *pethis = ethis;
            goto L1;
        }

        // Note that ethis is already adjusted to be the ethis for sfunc.Sscope
        // c1isbaseofc2(&ethis,sfunc.Sscope,stag);
        i = cpp_vtbloffset(cast(Classsym *)sfunc.Sscope,sfunc);
        st = sfunc.Sscope.Sstruct;

        /* Construct pointer to function, pdtor                         */
        /* pdtor:       *(*(ethis + offset(vptr)) + i)                  */
        svptr = st.Svptr;
        symbol_debug(svptr);

        /* e = *(ethis + offset(vptr)); ethis might be a handle pointer */
        e = el_bint(OPadd,newpointer(svptr.Stype),
                el_same(&ethis),el_longt(tstypes[TYint],cast(targ_int) svptr.Smemoff));
        e.ET.Tty = tym;
        e = el_unat(OPind,svptr.Stype,e);

        *pethis = ethis;
        e = el_bint(OPadd,e.ET,e,el_longt(tstypes[TYint],i));
        pfunc = el_unat(OPind,newpointer(sfunc.Stype),e);
    }
    else
    {
    L1: /* Watch out for pure functions */
        if (sfunc.Sfunc.Fflags & Fpure)
            cpperr(EM_pure_virtual,cpp_prettyident(sfunc));
        pfunc = el_ptr(sfunc);
    }
    return pfunc;
}


/*******************************
 * Call constructor for ethis.
 * Input:
 *      funcsym_p       Which function we're in (null if none)
 *      arglist         List of parameters to constructor
 *      enelems         If not null, then use vector constructor, enelems
 *                      evaluates to number of elems in vector
 *      pvirtbase       If not null, then list of pointers to virtual base
 *                      classes
 *      flags
 *              1       this is a dynamic array initialization
 *              2       call __vec_new even if no ctor
 *              4       the class for access checking purposes is the
 *                      class for which funcsym_p is a member
 *              8       do not generate EH information
 *              0x10    do not call primary dtor for EH
 *              0x20    do not call constructors marked 'explicit'
 * Output:
 *      ethis           Is free'd unless (flags & 2)
 *      arglist         Is free'd
 *      enelems         Is free'd
 *      epvirtbase      Is free'd
 * Returns:
 *      null if constructor not found
 */

elem *cpp_constructor(elem *ethis,type *tclass,list_t arglist,elem *enelems,
        list_t pvirtbase,int flags)
{   Symbol *sctor;                      /* constructor function         */
    elem *e;
    Classsym *stag;
    Symbol *sconv;
    struct_t *st;
    elem *e2;
    match_t matchconv;
    match_t matchctor;
    int doeh;
    int nargs;
    int veccopy;
    int ctorflags = 0;

    //dbg_printf("cpp_constructor(tclass = '%s', flags = x%x)\n",tclass.Ttag.Sident, flags);
    assert(tclass && tybasic(tclass.Tty) == TYstruct && tclass.Ttag);
    stag = tclass.Ttag;
    symbol_debug(stag);
    template_instantiate_forward(stag);
    st = stag.Sstruct;
    ethis = cpp_fixptrtype(ethis,tclass);
    ethis = poptelem(ethis);
    veccopy = 0;

    doeh = !(flags & 8) && (config.flags3 & CFG3eh) && !eecontext.EEin;
    if (pointertype != st.ptrtype ||
        !st.Sdtor ||
        (tyfarfunc(st.Sdtor.Stype.Tty) ? !LARGECODE : LARGECODE))
        doeh = 0;                       // not ambient memory model

    /* Look for conversion operator     */
    e2 = null;
    sconv = null;
    matchconv = TMATCHnomatch;
    nargs = list_nitems(arglist);
    if (nargs == 1)
    {   type *t2;

        e2 = list_elem(arglist);
        t2 = type_arrayroot(e2.ET);

        if (tybasic(t2.Tty) == TYstruct)
        {
            if (enelems && tybasic(e2.ET.Tty) == TYarray && t2.Ttag == stag)
            {   // Must be vector copy constructor initialization.
                // Change e2 to be just a var.
                e2 = el_unat(OPaddr,newpointer(e2.ET.Tnext),e2);
                e2 = el_unat(OPind,t2,e2);
                e2 = poptelem(e2);
                arglist.ptr = e2;
                veccopy = 1;
            }

            if (c1isbaseofc2(null,stag,t2.Ttag))
                /* Generate default copy constructor X::X(X&)   */
                /* if one doesn't already exist                 */
                n2_createcopyctor(stag,1);
            if (stag == t2.Ttag)
            {   matchconv = 0;
                sconv = null;
                ctorflags |= 0x10;
            }
            else
            {   Match m;
                sconv = cpp_typecast(t2,tclass,&m);
                matchconv = m.m;
            }
        }
    }

    /* Look for constructor     */
    if (st.Sflags & STRgenctor0 && !arglist)
    {
        n2_creatector(tclass);                  // Generate X::X()
    }
    sctor = st.Sctor;
    sctor = cpp_overloadfunc(sctor,ethis.ET.Tnext,arglist,&matchctor,null,null,ctorflags);

version (none)
{
    if (sctor)
    {   Outbuffer buf;
        char *p1;
        p1 = param_tostring(&buf,sctor.Stype);
        dbg_printf("cpp_constructor(matchctor=x%x,sctor='%s%s')\n",
            matchctor,cpp_prettyident(sctor),p1);
        free(p1);
    }
    //elem_print(list_elem(arglist));
    //type_print(list_elem(arglist).ET);
}

    /* See if conversion function is a better fit       */
    //printf("matchconv = x%x, matchctor = x%x\n", matchconv, matchctor);
    if (matchconv > matchctor)
    {   /* Build ((*ethis = sconv(e2)),ethis)           */
        elem *ep;
        elem *econv;
        type *t;

        if (enelems)
            cpperr(EM_vector_init);     // no initializer for vector ctor
        /* This section matches code in cpp_cast()      */
        /*ep = cast(exp2_addr(e2),newpointer(e2.ET));*/
        ep = _cast(exp2_addr(e2),newpointer(sconv.Sscope.Stype)); /* to correct pointer type */
        econv = cpp_getfunc(e2.ET,sconv,&ep);
        econv = el_unat(OPind,econv.ET.Tnext,econv);

        e2 = xfunccall(econv,ep,null,null);
        assert(!pvirtbase);

        list_free(&arglist,FPNULL);
        arglist = list_build(e2,null);

        if (ethis.Eoper == OPrelconst && !pvirtbase)
        {
            e = init_constructor(ethis.EV.Vsym,tclass,arglist,ethis.EV.Voffset,4,null);
            el_free(ethis);
            if (e.Eoper == OPind)
                e = selecte1(e,e.EV.E1.ET);      // dump extra *
        }
        else
            e = cpp_constructor(ethis,tclass,arglist,enelems,pvirtbase,flags);
        goto ret;
    }

    if (!matchctor)
    {
        /*dbg_printf("constructor not found\n");*/
        if (arglist || st.Sctor)
        {
            /* Look for special case where we can simply copy structs.  */
            if (list_nitems(arglist) == 1 &&
                !enelems &&
                tybasic(e2.ET.Tty) == TYstruct)
            {
                Classsym *stag2 = e2.ET.Ttag;

                template_instantiate_forward(stag2);
                if (stag == stag2 || c1isbaseofc2(null, stag, stag2))
                {
                    // Generate default copy constructor X::X(X&)
                    n2_createcopyctor(stag,1);
                    sctor = st.Scpct;
                    if (sctor)
                        goto L2;

                    // Construct:  ((*ethis = e2),ethis)
                    assert(errcnt);             // should only happen on error
                    list_free(&arglist,FPNULL);
                    e = el_bint(OPstreq,tclass,el_unat(OPind,tclass,ethis),e2);
                    e = el_bint(OPcomma,ethis.ET,e,el_copytree(ethis));
                    goto ret;
                }
            }

            if (arglist || st.Sflags & STRanyctor)
            {
                err_noctor(stag,arglist);       // can't find constructor
                list_free(&pvirtbase,cast(list_free_fp)&el_free);
            }
            list_free(&arglist,cast(list_free_fp)&el_free);
        }
        assert(!pvirtbase || errcnt);
        if ((flags & 3) == 3)
        {
            /*  (ptr) __vec_new(&s,sizelem,nelems,null) */
            elem *evec;
            Symbol *sd;
            elem *ed;

            cpp_getpredefined();
            sd = doeh ? n2_createprimdtor(stag) : null;
            ed = sd ? _cast(el_ptr(sd),t_pdtor) : el_longt(t_pdtor,0);
            arglist = list_build(ethis,
                        el_typesize(tclass),
                        enelems,
                        el_longt(t_pctor,0),
                        ed,
                        null);

            e = xfunccall(el_var(s_vec_new),null,null,arglist);
            el_settype(e,newpointer(tclass));
        }
        else
        {
            if (doeh && !(flags & 1))
            {   Symbol *sd;

                sd = n2_createprimdtor(stag);
                if (enelems && enelems.Eoper == OPconst)
                    sd = n2_vecdtor(stag,enelems);
                e = el_ctor(ethis,null,sd);
            }
            else
            {   el_free(ethis);
                e = null;
            }
            el_free(enelems);
        }
    }
    else
    {
        assert(tyfunc(sctor.Stype.Tty));
        if (matchctor == matchconv)
            err_nomatch(&stag.Sident[0],arglist);  /* dunno which function to call */
    L2:
        elem *ector;
        Classsym *sftag;

        /* Determine access of function funcsym_p to member function sctor */
        sftag = (flags & 4) ? cast(Classsym *)funcsym_p.Sscope : stag;
        cpp_memberaccess(sctor,funcsym_p,sftag);

        // If no explicit constructors
        if (flags & 0x20 && sctor.Sfunc.Fflags & Fexplicit && list_nitems(arglist) == 1)
            // BUG: does this check occur before overloading or after?
            cpperr(EM_explicit_ctor);

        if (enelems)
        {
            /* if (dynamic)
                        (ptr) __vec_new(&s,sizelem,nelems,sctor)
               else
                        (ptr) __vec_ctor(&s,sizelem,nelems,sctor)
             */
            elem *evec;
            Symbol *sd;
            elem *ed;

            cpp_getpredefined();
            assert(!pvirtbase);

            if (arglist && (!veccopy || flags & 1))
            {   cpperr(EM_vector_init);         // no initializer for vector ctor
                list_free(&arglist,cast(list_free_fp)&el_free);
                veccopy = 0;
            }
            else
                list_free(&arglist,FPNULL);

            if (veccopy)
                sctor = n2_veccpct(stag);
            else
                sctor = n2_vecctor(stag);       /* get vector constructor       */
            assert(sctor);
            sd = doeh ? n2_createprimdtor(stag) : null;
            ed = sd ? _cast(el_ptr(sd),t_pdtor) : el_longt(t_pdtor,0);
            assert(ethis.ET.Tnext);
            if (ethis.ET.Tnext.Tty & (mTYconst | mTYvolatile))
                el_settype(ethis,tspvoid);
            if (e2)
                e2 = el_unat(OPaddr,newpointer(e2.ET),e2);
            arglist = list_build(ethis,
                        el_typesize(tclass),
                        enelems,
                        _cast(el_ptr(sctor),t_pctor),
                        ed,
                        e2,
                        null);

            evec = el_var((flags & 1) ? s_vec_new :
                          (veccopy ? s_vec_cpct : s_vec_ctor));
            {
            elem *ectorx;

            if (!(flags & 1) &&
                doeh && ethis.Eoper != OPstrthis)
                ectorx = el_copytree(ethis);
            else
                ectorx = null;
            e = xfunccall(evec,null,null,arglist);
            el_settype(e,sctor.Stype.Tnext);
            if (ectorx)
                e = el_ctor(ectorx,e,n2_vecdtor(stag,enelems));
            }
        }
        else
        {   /* Construct call to constructor.                           */
            /* If no pvirtbase, add nulls as values for virtual base    */
            /* class pointer parameters                                 */
            if (!pvirtbase)
            {
                if (st.Svirtbase)
                    // Set $initVBases to 1 to indicate ctor should
                    // construct vbases
                    list_append(&pvirtbase,el_longt(tstypes[TYint],1));
            }

            {
            elem *ectorx;

            if (doeh && ethis.Eoper != OPstrthis)
                ectorx = el_copytree(ethis);
            else
                ectorx = null;
            if (sctor.Sfunc.Fflags & Fbitcopy &&
                list_nitems(arglist) == 1 &&
                tybasic(e2.ET.Tty) == TYstruct &&
                e2.ET.Ttag == stag &&
                stag.Sstruct.Sflags & STRbitcopy
               )
            {
                list_free(&arglist,FPNULL);
                if (stag.Sstruct.Sflags & STR0size)
                {
                    e = el_bint(OPcomma,ethis.ET,e2,ethis);
                }
                else
                {   // Construct:  ((*ethis = e2),ethis)
                    e = el_bint(OPstreq,tclass,el_unat(OPind,tclass,ethis),e2);
                    e = el_bint(OPcomma,ethis.ET,e,el_copytree(ethis));
                }
            }
            else
                e = xfunccall(el_var(sctor),ethis,pvirtbase,arglist);
            if (flags & 0x10)
                e = el_ctor(ectorx,e,st.Sdtor);
            else
                e = el_ctor(ectorx,e,n2_createprimdtor(stag));
            }
        }
    }
ret:
    return e;
}


/*****************************
 * Generate a function call to destructor.
 * Input:
 *      tclass  class type that has the destructor
 *      eptr    elem that is a pointer to the object to be destroyed
 *      enelems if vector destructor, this is the number of elems
 *              else null
 *      dtorflag        DTORxxxx, value for second argument of the destructor
 * Return elem created.
 */

elem *cpp_destructor(type *tclass,elem *eptr,elem *enelems,int dtorflag)
{   Symbol *sdtor;
    Classsym *stag;
    elem *e;
    elem *edtor;
    elem *efunc;
    list_t arglist;
    struct_t *st;
    int noeh;

    //printf("cpp_destructor called enelems %p, dtorflag x%x\n", enelems, dtorflag);
    cpp_getpredefined();

    assert(tclass && tybasic(tclass.Tty) == TYstruct && tclass.Ttag);
    stag = tclass.Ttag;
    st = stag.Sstruct;
    noeh = dtorflag & DTORnoeh;
    if (!(config.flags3 & CFG3eh) || pointertype != st.ptrtype || eecontext.EEin)
        noeh = 1;
    dtorflag &= ~DTORnoeh;
    if (enelems)
        dtorflag |= DTORvector;
    assert((dtorflag & (DTORvecdel | DTORvector)) != (DTORvecdel | DTORvector));
    eptr = cpp_fixptrtype(eptr,tclass);
    sdtor = st.Sdtor;
    if (sdtor)
    {   Classsym *sftag;
        Symbol *sd;

        if (tyfarfunc(sdtor.Stype.Tty) ? !LARGECODE : LARGECODE)
            noeh = 1;                   // not ambient memory model

        // Determine access of function funcsym_p to member function sdtor
        if (!(dtorflag & DTORnoaccess))
        {
            sftag = (dtorflag & DTORmostderived)
                    ? stag
                    : cast(Classsym *) funcsym_p.Sscope;
            cpp_memberaccess(sdtor,funcsym_p,sftag);    // access checking
        }
        dtorflag &= ~DTORnoaccess;
        if (!(sdtor.Sfunc.Fflags & Fvirtual))
            dtorflag &= ~DTORvirtual;
        switch (dtorflag)
        {
            case 0:
                sd = sdtor;
                break;
            case DTORmostderived:
                sd = n2_createprimdtor(stag);
                break;
            default:
                sd = n2_createscaldeldtor(stag);
                break;
        }
        if (dtorflag & DTORvirtual)
            edtor = cpp_getfunc(tclass,sd,&eptr);
        else
            edtor = el_ptr(sd);
    }
    else
    {
        if (dtorflag & DTORvecdel)
        {
            edtor = el_longt(t_pdtor,0);        // null for pointer to dtor
        }
        else
        {
         L1:
            /* BUG: What if eptr or enelems have side effects?  */
            el_free(eptr);
            el_free(enelems);
            return null;
        }
    }

    arglist = null;

    if (dtorflag & DTORvector)          // it's a static vector
    {
        /* call __vec_dtor(ptr,sizelem,nelems,edtor)            */
        elem *ed;

        assert(!(dtorflag & DTORvirtual));
        el_free(edtor);
        edtor = el_var(n2_vecdtor(stag,enelems));
        el_free(enelems);
        goto L6;

        edtor = _cast(edtor,s_vec_dtor.Stype.Tparamtypes.Pnext.Pnext.Pnext.Ptype);
        efunc = el_var(s_vec_dtor);
        eptr = poptelem(eptr);
        arglist = list_build(eptr,el_typesize(tclass),enelems,edtor,null);
        if (!noeh)
            ed = el_copytree(eptr);
        e = xfunccall(efunc,null,null,arglist);
        if (!noeh)
            e = el_dtor(ed,e);
    }
    else if (dtorflag & DTORvecdel)
    {
        /* call __vec_delete(void *Parray,int Free,size_t Sizelem,
                int (*Dtor)(void))
         */
        edtor = _cast(edtor,s_vec_delete.Stype.Tparamtypes.Pnext.Pnext.Pnext.Ptype);
        efunc = el_var(s_vec_delete);
        arglist = list_build(
                eptr,
                el_longt(tstypes[TYint],dtorflag & (DTORfree | DTORvecdel)),
                el_typesize(tclass),
                edtor,
                null);

        e = xfunccall(efunc,null,null,arglist);
        //if (sdtor)
        //    nwc_mustwrite(sdtor);
    }
    else
    {
        /* Generate:  edtor(eptr,dtorflag)      */

        if (dtorflag & ~DTORmostderived)
            list_append(&arglist,el_longt(tstypes[TYint],dtorflag & DTORfree));
        edtor = el_unat(OPind,edtor.ET.Tnext,edtor);
    L6:
        elem *ed;
        eptr = poptelem(eptr);
        if (!noeh)
            ed = el_copytree(eptr);
        e = xfunccall(edtor,eptr,null,arglist);
        if (!noeh)
            e = el_dtor(ed,e);
    }
    return e;
}


/****************************
 * Given the list of static constructors and destructors, build
 * two special functions that call them.
 */

void cpp_build_STI_STD()
{   char *p;
    char *name;
    int dtor;

    /*dbg_printf("cpp_build_STI_STD()\n");*/
    if (!CPP || errcnt)                         // if any syntax errors
    {
if (TERMCODE)
{
        list_free(&constructor_list,cast(list_free_fp)&el_free);
        list_free(&destructor_list,cast(list_free_fp)&el_free);
}
        return;                         /* don't invite disaster        */
    }
    p = file_unique();                  // get name unique to this module
    name = cast(char *) alloca(strlen(p) + 6);
    sprintf(name,"__SD%s_",p);

    // Do destructors (_STD)
    dtor = 0;
    if (destructor_list)
    {   Symbol *sdtor;

        sdtor = cpp_build_STX(name,destructor_list);
static if (TARGET_LINUX || TARGET_OSX || TARGET_FREEBSD || TARGET_OPENBSD || TARGET_SOLARIS)
{
        Obj.staticctor(sdtor,1,pstate.STinitseg);
        dtor = 0;
}
else
{
        // Append call to __fatexit(sdtor) to constructor list
        {   elem *e;
            list_t arglist;

            arglist = list_build(el_ptr(sdtor),null);
            e = xfunccall(el_var(s_fatexit),null,null,arglist);
            list_append(&constructor_list,e);
            dtor = 1;
        }
}
    }

version (SCPP)
{
    // Do constructors (_STI)
    if (constructor_list)
    {   Symbol *sctor;

        name[3] = 'I';                  // convert name to _STIxxxx
        sctor = cpp_build_STX(name,constructor_list);
        Obj.staticctor(sctor,dtor,pstate.STinitseg);
    }
}
}


/***************************
 * Build a function that executes all the elems in tor_list.
 * Input:
 *      *name           what to name the function
 */

private Symbol * cpp_build_STX(char *name,list_t tor_list)
{
    Symbol *s;
    elem *e;
    list_t cl;
    block *b;
    func_t *f;
    type *t;

static if (MEMMODELS == 1)
    t = type_alloc(functypetab[cast(int) linkage]);
else
{
    // All are far functions for 16 bit models.
    // Bummer for .COM programs.
    t = type_alloc(functypetab[cast(int) LINK_C][(_tysize[TYint] == 4) ? Smodel : Mmodel]);
}
    t.Tmangle = funcmangletab[cast(int) LINK_C];
    t.Tnext = tstypes[TYvoid];
    tstypes[TYvoid].Tcount++;
    s = symbol_name(name,SCglobal,t);
    s.Sflags |= SFLimplem;             /* seen implementation          */
    f = s.Sfunc;
    e = null;
    for (cl = tor_list; cl; cl = list_next(cl))
        e = el_combine(e,list_elem(cl));
    list_free(&tor_list,FPNULL);
    b = block_new(BCret);
    b.Belem = e;
    f.Fstartblock = b;
    assert(globsym.top == 0);           // no local symbols
    queue_func(s);                      /* queue for output             */
    symbol_keep(s);
static if (TARGET_LINUX || TARGET_OSX || TARGET_FREEBSD || TARGET_OPENBSD || TARGET_SOLARIS)
{
    output_func();                      // output now for relocation ref */
}
    return s;
}


/***************************
 * Get pointer to local symbol with name for function.
 * Ignore scoping.
 * Only look for stack variables.
 * Returns:
 *      null if not found
 */
Symbol *cpp_getlocalsym(Symbol *sfunc,char *name)
{   func_t *f;
    SYMIDX i;
    symtab_t *ps;

    //dbg_printf("cpp_getlocalsym(%s,%s)\n",sfunc.Sident,name);
    symbol_debug(sfunc);
    f = sfunc.Sfunc;
    assert(f);
    ps = &f.Flocsym;
    if (!ps.tab)               // it's in global table if function is not finished
    {   //printf("looking at globsym\n");
        ps = &globsym;
    }
    //dbg_printf("Flocsym.top = %d\n",ps.top);
    for (i = 0; i < ps.top; i++)
    {
        //dbg_printf("ps.tab[%d].Sident = '%s'\n",i,&ps.tab[i].Sident[0]);
        if (strcmp(&ps.tab[i].Sident[0],name) == 0)
            return ps.tab[i];
    }
    return null;
}


/********************************
 * Get pointer to "this" variable for function.
 */

Symbol *cpp_getthis(Symbol *sfunc)
{
    //dbg_printf("cpp_getthis(%p, '%s')\n",sfunc, sfunc.Sident);
    return cpp_getlocalsym(sfunc,cast(char*)cpp_name_this);
}


/**********************************
 * Find constructor X::X() for class stag.
 * Returns:
 *      constructor function
 *      null if none
 */

Symbol *cpp_findctor0(Classsym *stag)
{   Symbol *sctor;

    symbol_debug(stag);

    if (stag.Sstruct.Sflags & STRgenctor0)
        n2_creatector(stag.Stype);     // generate X::X()

    for (sctor = stag.Sstruct.Sctor; sctor; sctor = sctor.Sfunc.Foversym)
    {   symbol_debug(sctor);

        /* If no arguments and not variadic     */
        if (!sctor.Stype.Tparamtypes && sctor.Stype.Tflags & TFfixed)
            break;
    }
    return sctor;
}


/***************************************
 * Construct list of pointers to virtual base classes to pass to the
 * constructor for the base class sbase.
 */

private list_t cpp_pvirtbase(Classsym *stag,Classsym *sbase)
{
    list_t pvirtbase = null;
    if (sbase.Sstruct.Svirtbase)
        list_append(&pvirtbase,el_longt(tstypes[TYint],0));
    return pvirtbase;
}


/*********************************
 * Find initializer expression for symbol s.
 * Return null if none found.
 */

private list_t cpp_meminitializer(list_t bl,Symbol *s)
{
    meminit_t *m;
    list_t arglist;

    /* Look for explicit initializer    */
    arglist = null;             /* if no explicit initializer   */
    for (; bl; bl = list_next(bl))
    {   m = cast(meminit_t *) list_ptr(bl);
        if (m.MIsym == s)
        {
            arglist = m.MIelemlist;
            m.MIelemlist = null;
            if (!arglist)
            {
                // See if we should create one
                if (tyscalar(s.Stype.Tty))
                {
                    elem *e;

                    e = el_longt(s.Stype, 0);
                    arglist = list_build(e, null);
                }
            }
            break;
        }
    }
    return arglist;
}


/*********************************
 * Take initializer list and build initialization expression for
 * constructor s_ctor.
 * Input:
 *      flag    1       is internally generated copy constructor
 */

void cpp_buildinitializer(Symbol *s_ctor,list_t baseinit,int flag)
{   elem *e;
    func_t *f;
    Classsym *stag;
    struct_t *st;
    type *tclass;
    symtab_t *psymtabsave;
    Symbol *funcsym_save;
    int flags = 4;

    symbol_debug(s_ctor);
    assert(tyfunc(s_ctor.Stype.Tty));
    f = s_ctor.Sfunc;
    assert(f);
    tclass = s_ctor.Stype.Tnext.Tnext;
    assert(tybasic(tclass.Tty) == TYstruct);
    stag = tclass.Ttag;
    st = stag.Sstruct;
    if (pointertype != st.ptrtype)     // if not ambient memory model
        flags |= 8;                     // no eh information
    //dbg_printf("cpp_buildinitializer(%s) %p, flag = %d\n",prettyident(s_ctor),s_ctor, flag);

    // Switch to local symtab, so if temporary variables are generated,
    // they are added to the local symbol table rather than the global
    psymtabsave = cstate.CSpsymtab;
    if (f.Flocsym.tab)
        cstate.CSpsymtab = &f.Flocsym;

    funcsym_save = funcsym_p;
    funcsym_p = s_ctor;

    /* Now that we have the list of initializers in baseinit, produce   */
    /* the expression e which does all the initialization for the       */
    /* class. This used to be in cpp_fixconstructor(), but we had to    */
    /* move it here because if any temporaries were generated, the      */
    /* destructor needs to get called.                                  */

    e = null;

    /* Skip base and member initialization if constructor is a bitcopy  */
    if (!(f.Fflags & Fbitcopy))
    {   list_t arglist;
        symlist_t sl;
        Symbol *s_this;
        int iscpct;                     /* !=0 if copy constructor      */

        s_this = f.Flocsym.tab ? cpp_getthis(s_ctor)
                               : scope_search(cpp_name_this.ptr,SCTlocal);
        symbol_debug(s_this);

        /*iscpct = n2_iscopyctor(s_ctor);*/

    /* Call constructor for each virtual base class     */
    {   baseclass_t *b;

        // Find initvbases flag
        Symbol *s_initvbases;
        elem *evc;

        // Call constructors
        evc = null;
        for (b = st.Svirtbase; b; b = b.BCnext)
        {   elem *ethis;
            elem *ector;
            Classsym *sbase;
            SYMIDX marksi;

            sbase = b.BCbase;
            ethis = el_var(s_this);
            //ethis = exp2_ptrvbaseclass(ethis,stag,sbase);
            ethis = el_bint(OPadd,type_allocn(s_this.Stype.Tty,sbase.Stype),ethis,el_longt(tstypes[TYint],b.BCoffset));
            arglist = cpp_meminitializer(baseinit,sbase);
            /*dbg_printf("Virtual base '%s', arglist = %p\n",sbase.Sident,arglist);*/
            if (sbase.Sstruct.Sflags & STRgenctor0 && !arglist)
                n2_creatector(sbase.Stype);
            if (sbase.Sstruct.Sctor)          /* if constructor       */
            {   list_t pvirtbase;

                pvirtbase = cpp_pvirtbase(stag,sbase);
                /* Construct call to virtual base constructor   */
                marksi = globsym.top;
                ector = cpp_constructor(ethis,sbase.Stype,arglist,null,pvirtbase,flags | 0x10);
                func_expadddtors(&ethis, marksi, globsym.top, true, true);
            }
            else
                ector = ethis;
            evc = el_combine(evc,ector);
        }

        // Initialize vbptr
        if (st.Svirtbase)
            evc = el_combine(cpp_assignvbptr(s_this),evc);

        // Build: (s_initvbases && evc)
        if (evc)
        {
            s_initvbases = f.Flocsym.tab ? cpp_getlocalsym(s_ctor,cast(char*)cpp_name_initvbases)
                                         : scope_search(cpp_name_initvbases.ptr,SCTlocal);
            e = el_bint(OPandand,tstypes[TYint],el_var(s_initvbases),evc);
        }
    }

    /* Do constructors for non-virtual base classes in the order declared */
    {   baseclass_t *b;
        elem *ethis;
        Classsym *sbase;

        /* Do non-virtual base classes  */
        for (b = st.Sbase; b; b = b.BCnext)
        {   elem *ector;
            list_t pvirtbase;

            if (b.BCflags & BCFvirtual)
                continue;
            sbase = b.BCbase;
            ethis = el_var(s_this);
            c1isbaseofc2(&ethis,sbase,stag);

            arglist = cpp_meminitializer(baseinit,sbase);
            pvirtbase = cpp_pvirtbase(stag,sbase);
            ector = cpp_constructor(ethis,sbase.Stype,arglist,null,pvirtbase,flags | 0x10);
            e = el_combine(e,ector);
        }
    }

    /* Do constructors for members      */
//    if (!flag)
    for (sl = st.Sfldlst; sl; sl = list_next(sl))
    {   Symbol *s;
        type *t;
        type *tclassx;

        s = list_symbol(sl);
        /* Do not initialize things like static members or typedefs     */
        if (s.Sclass != SCmember && s.Sclass != SCfield)
            continue;
        t = s.Stype;
        if (!t)
            continue;
        tclassx = type_arrayroot(t);

        /* If constructor for member    */
        if (tybasic(tclassx.Tty) == TYstruct /*&& tclassx.Ttag.Sstruct.Sctor*/)
        {   elem *et;
            elem *enelems;

            /* Look for member initializer      */
            arglist = cpp_meminitializer(baseinit,s);
            enelems = el_nelems(t);
            et = el_var(s_this);
            et = el_bint(OPadd,et.ET,et,el_longt(tstypes[TYuint],s.Smemoff));
            et = cpp_constructor(et,tclassx,arglist,enelems,null,flags & 8);
            e = el_combine(e,et);
        }
        /* else if a member initializer */
        else
        {   elem *et;

            /* Look for member initializer      */
            arglist = cpp_meminitializer(baseinit,s);
            if (arglist)
            {
                /* BUG: This doesn't pick up attempts to initialize arrays */
                if (list_nitems(arglist) == 1)
                {   elem *ei;

                    /* Create et <== *(this + offset)   */
                    et = el_var(s_this);
                    et = el_bint(OPadd,newpointer(t),et,el_longt(tstypes[TYuint],s.Smemoff));
                    et = el_unat(OPind,
                        (tyref(t.Tty) ?
                            newpointer(t.Tnext) : t),et);
                    if (s.Sclass == SCfield)
                    {   elem *mos;

                        // Take care of bit fields
                        mos = el_longt(tstypes[TYuint],s.Swidth * 256 + s.Sbit);
                        et = el_bint(OPbit,t,et,mos);
                    }

                    /* Get initializer and convert it to type of et     */
                    ei = list_elem(arglist);
//printf("arg:\n"); type_print(ei.ET);
//printf("\nparameter:\n"); type_print(t);
                    ei = typechk(ei,t);

                    et = el_bint(OPeq,et.ET,et,ei); /* create (et = ei) */
                    if (tybasic(t.Tty) == TYstruct)
                    {   et.Eoper = OPstreq;
                        /*assert(t.Ttag.Sstruct.Sflags & STRbitcopy);*/
                    }
                    e = el_combine(e,et);
                    list_free(&arglist,FPNULL);
                }
                else
                    cpperr(EM_one_arg,&s.Sident[0]);       // 1 arg req'd
            }
        }
    }
    } /* if not bitcopy */

    list_free(&baseinit,cast(list_free_fp)&meminit_free);
    f.Fbaseinit = e;

    funcsym_p = funcsym_save;
    cstate.CSpsymtab = psymtabsave;
}


/**************************
 * Add code to constructor function.
 */

void cpp_fixconstructor(Symbol *s_ctor)
{   elem *e;
    Symbol *s_this;
    func_t *f;
    type *tclass;
    symlist_t sl;
    bool sawthis;
    list_t arglist;
    Classsym *sbase;
    Classsym *stag;
    symtab_t *psymtabsave;
    Symbol *funcsymsave;
    int _abstract;
    block *baseblock;

    //dbg_printf("cpp_fixconstructor(%s) %p\n",prettyident(s_ctor),s_ctor);
    assert(s_ctor);
    symbol_debug(s_ctor);
    f = s_ctor.Sfunc;
    assert(f);
    if (!(s_ctor.Sflags & SFLimplem)   // if haven't seen function body yet
        || f.Fflags & Ffixed           // already been "fixed"
       )
        return;

    baseblock = f.Fbaseblock ? f.Fbaseblock : f.Fstartblock;

    f.Fflags |= Ffixed;

    /* Switch to local symtab, so if temporary variables are generated, */
    /* they are added to the local symbol table rather than the global  */
    psymtabsave = cstate.CSpsymtab;
    cstate.CSpsymtab = &f.Flocsym;
    assert(cstate.CSpsymtab.tab);      // the local symbol table must exist

    funcsymsave = funcsym_p;
    funcsym_p = s_ctor;

    s_this = cpp_getthis(s_ctor);
    symbol_debug(s_this);
    if (errcnt)                         /* if syntax errors occurred    */
        goto fixret;                    /* don't attempt to continue    */

    /* Type of ctor is <func ret><ref to><class>        */
    tclass = s_ctor.Stype.Tnext.Tnext;
    assert(tybasic(tclass.Tty) == TYstruct);
    stag = tclass.Ttag;

    /* Build a constructor, e, which first calls the constructor for    */
    /* for the base class and then for each member. Next, assign        */
    /* pointer to virtual function table.                               */
    e = f.Fbaseinit;
    f.Fbaseinit = null;

    /* If there are virtual functions, assign virtual pointer   */
    _abstract = stag.Sstruct.Sflags & STRabstract;
    e = el_combine(e,cpp_assignvptr(s_this,1));

    /* Find every occurrence of an assignment to this, and append a     */
    /* copy of e to it.                                                 */
    sawthis = false;                    /* assume no assignments to this */
    foreach (b; BlockRange(baseblock))
        if (b.Belem)
            sawthis |= !!fixctorwalk(b.Belem,e,s_this);

    if (sawthis)
    {
        synerr(EM_assignthis);
        el_free(e);
    }
    else
    {   /* Didn't see any assignments to this. Therefore, create one at */
        /* entry to the function                                        */

        baseblock.Belem = el_combine(e,baseblock.Belem);
    }

fixret:
    /* Make sure 'this' is returned from every return block             */
    foreach (b; BlockRange(s_ctor.Sfunc.Fstartblock))
        if (b.BC == BCret || b.BC == BCretexp)
        {   elem *ex;

            b.BC = BCretexp;
            ex = b.Belem;
            if (ex)
            {   Symbol *s;

                while (ex.Eoper == OPcomma)
                    ex = ex.EV.E2;
                if (ex.Eoper == OPcall &&
                    ex.EV.E1.Eoper == OPvar &&
                    (s = ex.EV.E1.EV.Vsym).Sfunc.Fflags & Fctor)
                {
                    do
                        ex = ex.EV.E2;
                    while (ex.Eoper == OPparam);
                    if (ex.Eoper == OPvar && ex.EV.Vsym == s_this)
                        continue;       /* s_this is being returned     */
                }
            }
            b.Belem = el_combine(b.Belem,el_var(s_this));
        }

    funcsym_p = funcsymsave;
    cstate.CSpsymtab = psymtabsave;
    /*dbg_printf("cpp_fixconstructor ret\n");*/
}


private int fixctorwalk(
        elem *e,        // the tree down which assignments to this are
        elem *ec,       // elem which is appended to assignments to this
        Symbol *s_this)
{   char sawthis = false;

    while (1)
    {
        assert(e);
        if (OTbinary(e.Eoper))
        {   if (e.Eoper == OPeq &&
                e.EV.E1.Eoper == OPvar &&
                e.EV.E1.EV.Vsym == s_this)
            {   elem *e1;
                elem tmp = void;

                if (!ec)
                    return true;
                /* Create (((this=x),ec),this) from (this=x)            */
                e1 = el_combine(e,el_copytree(ec));
                e1 = el_bint(OPcomma,e.ET,e1,el_var(s_this));
                e1.EV.E1.EV.E1 = e1;
                tmp = *e;
                *e = *e1;       /* jam into tree by swapping contents   */
                *e1 = tmp;      /*  of e and e1                         */
                e = e1;
                sawthis |= true;
            }
            sawthis |= fixctorwalk(e.EV.E2,ec,s_this);
            goto L1;
        }
        else if (OTunary(e.Eoper))
        L1:
            e = e.EV.E1;
        else
            return sawthis;
    }
    assert(0);
}


/********************************
 * Generate constructor for tclass if we don't have one but need one.
 * Returns:
 *      0       no constructor needed
 *      1       generate constructor X::X()
 *      2       constructor only needed if no { } initializers
 */

int cpp_ctor(Classsym *stag)
{
    struct_t *st;

    if (errcnt)                         /* don't attempt if errors      */
        return 0;
    symbol_debug(stag);
    assert(type_struct(stag.Stype));

    st = stag.Sstruct;
    if (!st.Sctor)
    {
        /* If any base class has a constructor or there is a virtual    */
        /* table or there are virtual base classes                      */
        if (st.Svirtual || st.Svirtbase)
        {
        L1:
            return 1;
        }
        else
        {   baseclass_t *b;
            symlist_t sl;

            /* See if constructor for any base          */
            for (b = st.Sbase; b; b = b.BCnext)
                if (b.BCbase.Sstruct.Sflags & STRanyctor)
                    goto L1;

            /* See if constructor for any non-static data member        */
            for (sl = st.Sfldlst; sl; sl = list_next(sl))
            {   Symbol *s;
                type *t;

                s = list_symbol(sl);
                symbol_debug(s);
                if (s.Sclass == SCmember)
                {
                    t = s.Stype;
                    /* If constructor for member        */
                    if (t)
                    {   t = type_arrayroot(t);
                        if (tybasic(t.Tty) == TYstruct &&
                            t.Ttag.Sstruct.Sflags & STRanyctor)
                            return 2;
                    }
                }
            }
        }
    }
    return 0;
}


/********************************
 * Determine if inline destructor needs to be created for tclass.
 */

int cpp_dtor(type *tclass)
{   struct_t *st;

    if (errcnt)                         /* don't attempt if errors      */
        goto L2;
    assert(tclass && tybasic(tclass.Tty) == TYstruct);

    st = tclass.Ttag.Sstruct;
    if (!st.Sdtor)
    {   baseclass_t *b;
        symlist_t sl;

        /* If any base class has a destructor   */
        for (b = st.Sbase; b; b = b.BCnext)
            if (b.BCbase.Sstruct.Sdtor)
            {
            L1:
                return true;
            }


        /* See if destructor for any member     */
        for (sl = st.Sfldlst; sl; sl = list_next(sl))
        {   Symbol *s;
            type *t;

            s = list_symbol(sl);
            /* Don't worry about dtors for static members */
            if (s.Sclass == SCmember)
            {
                t = s.Stype;
                /* If destructor for member     */
                if (t)
                {   t = type_arrayroot(t);
                    if (tybasic(t.Tty) == TYstruct && t.Ttag.Sstruct.Sdtor)
                        goto L1;
                }
            }
        }
    }
L2: return false;
}


/******************************************
 * Build an expression that destructs all the base classes
 * and members.
 */

elem *cpp_buildterminator(Classsym *stag, Symbol *s_this, elem **ped)
{
    elem *edm;
    elem *edb;
    elem *e;
    elem *e1;
    Classsym *sbase;
    int doeh;

    /* Construct e, the combination of member destructors and base
     * class destructors
     */

    edm = null;
    edb = null;
    e = null;

    doeh = 1;
    if (!(config.flags3 & CFG3eh) || pointertype != stag.Sstruct.ptrtype || eecontext.EEin)
        doeh = 0;

    /* Do destructor for each member    */
    for (symlist_t sl = stag.Sstruct.Sfldlst; sl; sl = list_next(sl))
    {   Symbol *s;
        type *t;

        s = list_symbol(sl);
        /* Do not destroy things like static members    */
        if (s.Sclass != SCmember && s.Sclass != SCfield)
            continue;
        t = s.Stype;
        if (t)
        {   type *tclass;

            tclass = type_arrayroot(t);

            /* If destructor for member */
            if (tybasic(tclass.Tty) == TYstruct && tclass.Ttag.Sstruct.Sdtor)
            {   elem *et;
                elem *enelems;
                elem *ector;
                elem *e2;
                Symbol *sdtor;

                enelems = el_nelems(t);

                if (doeh)
                {
                    // Figure out which destructor to call
                    if (enelems)
                        sdtor = n2_vecdtor(tclass.Ttag, enelems);
                    else
                    {
                        //sdtor = tclass.Ttag.Sstruct.Sdtor;
                        sdtor = n2_createprimdtor(tclass.Ttag);
                    }
                }

                et = el_var(s_this);
                et = el_bint(OPadd,et.ET,et,el_longt(tstypes[TYuint],s.Smemoff));
                if (doeh)
                    ector = el_copytree(et);
                e2 = cpp_destructor(tclass,et,enelems,DTORmostderived | DTORnoeh);
                if (doeh)
                    e2 = el_dtor(el_copytree(ector), e2);
                e = el_combine(e2,e);

                /* Insert a dummy 'constructor' call to hang
                 * an OPctor on, so that if the destructor throws an
                 * exception the subobjects will get destructed
                 * upon stack unwind.
                 */

                if (doeh)
                {
                    e2 = el_ctor(ector, el_longt(tstypes[TYint], 0), sdtor);
                    edm = el_combine(edm,e2);
                }
            }
        }
    }

    /* Do destructors for non-virtual base classes */
    {
        e1 = null;
        for (baseclass_t *b = stag.Sstruct.Sbase; b; b = b.BCnext)
        {
            if (b.BCflags & BCFvirtual)
                continue;               /* do virtual base classes later */
            sbase = b.BCbase;
            symbol_debug(sbase);
            if (sbase.Sstruct.Sdtor)
            {   elem *et;
                elem *ector;
                elem *e2;

                et = el_var(s_this);
                c1isbaseofc2(&et,sbase,stag);
                if (doeh)
                    ector = el_copytree(et);
                e2 = cpp_destructor(sbase.Stype,et,null,DTORnoeh);
                if (doeh)
                    e2 = el_dtor(el_copytree(et), e2);
                e1 = el_combine(e2,e1);

                if (doeh)
                {
                    e2 = el_ctor(ector, el_longt(tstypes[TYint], 0), sbase.Sstruct.Sdtor);
                    edb = el_combine(edb,e2);
                }
            }
        }
        e = el_combine(e,e1);
    }

    *ped = el_combine(edb, edm);
    return e;
}


/**************************
 * Add code to destructor function.
 */

void cpp_fixdestructor(Symbol *s_dtor)
{   elem* e,e1,e2;
    elem *ed;
    Symbol *s_this;
    Symbol *s__free;
    Symbol *funcsym_save;
    type *tclass;
    Classsym *stag;
    int _abstract;                       /* !=0 if dtor for abstract class */
    int sepnewdel;                      // !=0 if delete is not to be used
    block *baseblock;
    func_t *f = s_dtor.Sfunc;

    if (errcnt)                         /* don't attempt if errors      */
        return;
    /*dbg_printf("cpp_fixdestructor(%s)\n",prettyident(s_dtor));*/
    assert(s_dtor);
    if (!(s_dtor.Sflags & SFLimplem)   /* if haven't seen function body yet */
        //|| f.Fflags & Fpure  // ignore abstract virtual dtors
        || f.Fflags & Ffixed   // already been "fixed"
       )
        return;

    sepnewdel = !(s_dtor.Sfunc.Fflags & Fvirtual);
    f.Fflags |= Ffixed;

    /* Adjust which function we are in  */
    funcsym_save = funcsym_p;
    funcsym_p = s_dtor;

    s_this = cpp_getthis(s_dtor);
    symbol_debug(s_this);

    assert(s_this.Stype);
    tclass = s_this.Stype.Tnext;      /* this is <pointer to><class>  */
    assert(tclass && tybasic(tclass.Tty) == TYstruct);
    stag = tclass.Ttag;

    baseblock = f.Fbaseblock ? f.Fbaseblock : f.Fstartblock;

    e = cpp_buildterminator(stag, s_this, &ed);


    {
    char sawthis;

    /* Search for any assignments to this       */
    sawthis = false;                    /* assume no assignments to this */
    foreach (b; BlockRange(baseblock))
        if (b.Belem && fixctorwalk(b.Belem,null,s_this))
        {   sawthis = true;
            synerr(EM_assignthis);
            break;
        }

    _abstract = stag.Sstruct.Sflags & STRabstract;

    foreach (b; BlockRange(baseblock))
    {
        if (b.BC == BCret || b.BC == BCretexp || b == f.Fbaseendblock)
            b.Belem = el_combine(b.Belem,el_copytree(e));

        if (b == f.Fbaseendblock)
            break;
    }
    el_free(e);

    if (1 || !_abstract)
    {   block *bstart = baseblock;

        /* Add expression that resets the virtual function table
           pointers to the start of the destructor.
         */
        bstart.Belem = el_combine(cpp_assignvptr(s_this,0),bstart.Belem);

        // Add code that enables EH for member and base destructors
        bstart.Belem = el_combine(ed, bstart.Belem);
    }
    }

    funcsym_p = funcsym_save;
    /*dbg_printf("cpp_fixdestructor() done\n");*/
}


/*************************************
 * Do structure copy.
 */

elem *cpp_structcopy(elem *e)
{   elem *e1;
    type *t1;
    type *t2;

    elem_debug(e);
    e1 = e.EV.E1;
    t1 = e1.ET;
    if (type_struct(t1))
    {   Classsym *stag = t1.Ttag;
        Symbol *sopeq;
        int flag;

version (none)
{
        printf("cpp_structcopy()\n");
        elem_print(e);
        type_print(e.EV.E1.ET);
        type_print(e.EV.E2.ET);
}
        flag = (type_struct(t2 = e.EV.E2.ET) && t2.Ttag == stag);
        n2_createopeq(stag,flag);       // make sure operator=() exists
        sopeq = stag.Sstruct.Sopeq;

        if (t1.Tty & mTYconst && sopeq && sopeq.Sfunc.Fflags & Fgen)
        {   const(char)* p;

            p = (e1.Eoper == OPvar) ? prettyident(e1.EV.Vsym) : "";
            synerr(EM_const_assign,p);  // can't assign to const variable
        }

        /* If the struct is a bit copy, just do an OPstreq, because if
           we're trying to copy structs from different segments, that's
           the only way it'll work (like copy far to near).
         */

        if (sopeq && sopeq.Sfunc.Fflags & Fbitcopy && flag)
        {
            e.Eoper = OPstreq;
        }
        else if ((e1 = cpp_opfunc(e)) != null)
            return e1;
        else
            typerr(EM_explicit_cast,t2,t1);     // can't implicitly convert from t2 to t1
    }
    return e;
}


/********************************
 * Determine if inline invariant needs to be created for tclass.
 * This routine is parallel to cpp_dtor().
 */

int cpp_needInvariant(type *tclass)
{   struct_t *st;

    if (!errcnt)                        // don't attempt if errors
    {
        assert(tclass && tybasic(tclass.Tty) == TYstruct);

        st = tclass.Ttag.Sstruct;
        if (!st.Sinvariant)
        {   baseclass_t *b;
            symlist_t sl;

            // If any base class has a destructor
            for (b = st.Sbase; b; b = b.BCnext)
                if (b.BCbase.Sstruct.Sinvariant)
                {
                L1:
                    return true;
                }


            // See if invariant for any member
            for (sl = st.Sfldlst; sl; sl = list_next(sl))
            {   Symbol *s;
                type *t;

                s = list_symbol(sl);
                // Don't worry about invariants for static members
                if (s.Sclass == SCmember)
                {
                    t = s.Stype;
                    // If destructor for member
                    if (t)
                    {   t = type_arrayroot(t);
                        if (tybasic(t.Tty) == TYstruct && t.Ttag.Sstruct.Sinvariant)
                            goto L1;
                    }
                }
            }
        }
    }
    return false;
}


/**************************
 * Add code to invariant function.
 * This parallels cpp_fixdestructor().
 */

void cpp_fixinvariant(Symbol *s_inv)
{
    elem* e,e1,e2;
    Symbol *s_this;
    Symbol *s__free;
    Symbol *funcsym_save;
    type *tclass;
    symlist_t sl;
    Classsym *stag;
    Classsym *sbase;
    baseclass_t *b;

    if (errcnt ||                               // don't attempt if errors
        !(config.flags5 & CFG5invariant)
       )
        return;
    //dbg_printf("cpp_fixinvariant(%s)\n",prettyident(s_inv));
    assert(s_inv);
    if (!(s_inv.Sflags & SFLimplem)            // if haven't seen function body yet
        //|| s_inv.Sfunc.Fflags & Fpure       // ignore abstract virtual dtors
        || s_inv.Sfunc.Fflags & Ffixed        // already been "fixed"
       )
        return;

    s_inv.Sfunc.Fflags |= Ffixed;

    /* Adjust which function we are in  */
    funcsym_save = funcsym_p;
    funcsym_p = s_inv;

    s_this = cpp_getthis(s_inv);
    symbol_debug(s_this);

    assert(s_this.Stype);
    tclass = s_this.Stype.Tnext;      // this is <pointer to><class>
    assert(tclass && tybasic(tclass.Tty) == TYstruct);
    stag = tclass.Ttag;

    // Construct e, the combination of member invariants and base
    // class invariants

    e = null;

    // Do invariant for each member
    for (sl = stag.Sstruct.Sfldlst; sl; sl = list_next(sl))
    {   Symbol *s;
        type *t;

        s = list_symbol(sl);
        // Do not destroy things like static members
        if (s.Sclass != SCmember && s.Sclass != SCfield)
            continue;
        t = s.Stype;
        if (t)
        {   type *tclassx;

            tclassx = type_arrayroot(t);

            // If invariant for member
            if (tybasic(tclassx.Tty) == TYstruct && tclassx.Ttag.Sstruct.Sinvariant)
            {   elem *et;
                elem *enelems;

                enelems = el_nelems(t);
                et = el_var(s_this);
                et = el_bint(OPadd,et.ET,et,el_longt(tstypes[TYuint],s.Smemoff));
                et = cpp_invariant(tclassx,et,enelems,DTORmostderived | DTORnoeh);
                e = el_combine(et,e);
            }
        }
    }

    // Do invariants for non-virtual base classes
    // (virtual base invariants are done by the DTORmostderived (the primary)
    // invariant)
    {
        e1 = null;
        for (b = stag.Sstruct.Sbase; b; b = b.BCnext)
        {
            if (b.BCflags & BCFvirtual)
                continue;               // do virtual base classes later
            sbase = b.BCbase;
            symbol_debug(sbase);
            if (sbase.Sstruct.Sinvariant)
            {   elem *et;

                et = el_var(s_this);
                c1isbaseofc2(&et,sbase,stag);
                et = cpp_invariant(sbase.Stype,et,null,DTORnoeh);
                e1 = el_combine(et,e1);
            }
        }
        e = el_combine(e,e1);
    }

    // Put at start of function
    func_t *f;
    f = s_inv.Sfunc;
    assert(f);
    f.Fstartblock.Belem = el_combine(e, f.Fstartblock.Belem);

    funcsym_p = funcsym_save;
    //dbg_printf("cpp_fixinvariant() done\n");
}


/*****************************
 * Generate a function call to invariant().
 * This code is very analogous to cpp_destructor()
 * Input:
 *      tclass  class type that has the invariant
 *      eptr    elem that is a pointer to the object to be checked
 *      enelems if vector invariant, this is the number of elems
 *              else null
 *      invariantflag   DTORxxxx, value for second argument of the invariant()
 * Return elem created.
 */

elem *cpp_invariant(type *tclass,elem *eptr,elem *enelems,int invariantflag)
{   Symbol *sinvariant;
    Classsym *stag;
    elem *e;
    elem *einvariant;
    elem *efunc;
    list_t arglist;
    struct_t *st;

    //dbg_printf("cpp_invariant() called enelems %p, invariantflag x%x\n",
    //enelems,invariantflag);
    cpp_getpredefined();

    assert(tclass && tybasic(tclass.Tty) == TYstruct && tclass.Ttag);
    stag = tclass.Ttag;
    st = stag.Sstruct;
    invariantflag &= ~DTORnoeh;
    if (enelems)
        invariantflag |= DTORvector;
    assert((invariantflag & (DTORvecdel | DTORvector)) != (DTORvecdel | DTORvector));
    eptr = cpp_fixptrtype(eptr,tclass);
    sinvariant = st.Sinvariant;
    if (sinvariant)
    {   Classsym *sftag;
        Symbol *sd;

        // Determine access of function funcsym_p to member function sinvariant
        if (!(invariantflag & DTORnoaccess))
        {
            sftag = (invariantflag & DTORmostderived)
                    ? stag
                    : cast(Classsym *) funcsym_p.Sscope;
            cpp_memberaccess(sinvariant,funcsym_p,sftag);       // access checking
        }
        invariantflag &= ~DTORnoaccess;
        if (!(sinvariant.Sfunc.Fflags & Fvirtual))
            invariantflag &= ~DTORvirtual;
        switch (invariantflag)
        {
            case 0:
                sd = sinvariant;
                break;
            case DTORmostderived:
                sd = n2_createpriminv(stag);
                break;
            default:
                sd = sinvariant;        // don't need scaled invariant call
                //sd = n2_createscaldelinvariant(stag);
                break;
        }
        if (invariantflag & DTORvirtual)
            einvariant = cpp_getfunc(tclass,sd,&eptr);
        else
            einvariant = el_ptr(sd);
    }
    else
    {
        if (invariantflag & DTORvecdel)
        {
            einvariant = el_longt(t_pdtor,0);   // null for pointer to invariant
        }
        else
        {
         L1:
            /* BUG: What if eptr or enelems have side effects?  */
            el_free(eptr);
            el_free(enelems);
            return null;
        }
    }

    if (invariantflag & DTORvector)             // it's a static vector
    {
        // call __vec_invariant(ptr,sizelem,nelems,einvariant)

        einvariant = _cast(einvariant,s_vec_invariant.Stype.Tparamtypes.Pnext.Pnext.Pnext.Ptype);
        efunc = el_var(s_vec_invariant);
        assert(eptr);
        eptr = poptelem(eptr);
        arglist = list_build(eptr,el_typesize(tclass),enelems,einvariant,null);
        eptr = null;
    }
    else if (invariantflag & DTORvecdel)        // it's allocated by new()
    {
        assert(0);                              // shouldn't ever need this
version (none)
{
        // call __vec_delete(void *Parray,int Free,size_t Sizelem,
        //      int (*Dtor)(void))

        einvariant = _cast(einvariant,s_vec_delete.Stype.Tparamtypes.Pnext.Pnext.Pnext.Ptype);
        efunc = el_var(s_vec_delete);
        arglist = list_build(
                eptr,
                el_longt(tstypes[TYint],invariantflag & (DTORfree | DTORvecdel)),
                el_typesize(tclass),
                einvariant,
                null);
        eptr = null;
}
    }
    else
    {
        // Generate:  eptr.einvariant()
        elem *ed;

        arglist = null;
        efunc = el_unat(OPind,einvariant.ET.Tnext,einvariant);
        eptr = poptelem(eptr);
    }
    e = xfunccall(efunc,eptr,null,arglist);
    return e;
}


/**********************************
 * Decide if we need to add call to __invariant() to __in block.
 *      Fflag   Fxxx functions to exclude
 */

elem *Funcsym_invariant(Funcsym *s, int Fflag)
{
    //printf("Funcsym_invariant('%s')\n", s.Sident);

    // Add to public non-static constructors or functions
    if (isclassmember(s) &&                     // is a member function
        ((s.Sfunc.Fflags & (Fflag | Finvariant | Fstatic)) == 0) &&
        (s.Sflags & SFLpmask) == SFLpublic
       )
    {
        elem *e;
        type *tclass = s.Sscope.Stype;
        Symbol *s_this = cpp_getthis(s);
        assert(s_this);
        elem *eptr = el_var(s_this);

        e = cpp_invariant(tclass, eptr, null, DTORvirtual);
        return e;
    }
    return null;
}


//extern __gshared char* cpp_pi;

/******************************************
 * C++98 13.6
 * Return match level of built-in operator.
 */

private Match cpp_builtinoperator(elem *e)
{
    Match m;
    match_t m1 = TMATCHnomatch;
    match_t m2;

    switch (e.Eoper)
    {
        case OPeqeq:
        case OPne:
            m1 = cpp_bool_match(e.EV.E1);
            m2 = cpp_bool_match(e.EV.E2);
            //printf("m1 = x%x, m2 = x%x\n", m1, m2);
            if (m2 < m1)
                m1 = m2;
            break;

        default:
            break;
    }
    if (m1 != TMATCHnomatch)
    {
        m.m = TMATCHuserdef;
        m.m2 = cast(match_t)(m1 - 1);          // -1 is a kludge
        //m1 = (m1 == TMATCHexact) ? TMATCHuserdef : TMATCHuserdef - 3;
    }
    //printf("cpp_builtinoperator() = x%x\n", m1);
    return m;
}

/**********************************
 */

void cpp_init()
{
}

/***************************
 * Perform optional clean-up step.
 */

void cpp_term()
{
    if (TERMCODE)
        mem_free(cpp_pi);
}

}
