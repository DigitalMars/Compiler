/**
 * Implementation of the
 * $(LINK2 http://www.digitalmars.com/download/freecompiler.html, Digital Mars C/C++ Compiler).
 *
 * Copyright:   Copyright (c) 1991-1998 by Symantec, All Rights Reserved
 *              Copyright (c) 2000-2017 by Digital Mars, All Rights Reserved
 * Authors:     $(LINK2 http://www.digitalmars.com, Walter Bright)
 * License:     Distributed under the Boost Software License, Version 1.0.
 *              http://www.boost.org/LICENSE_1_0.txt
 * Source:      https://github.com/DigitalMars/Compiler/blob/master/dm/src/dmc/dexp2.d
 */

// More of expression parser

module dexp2;

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
import dmd.backend.mem;

import cpp;
import dtoken;
import eh;
import msgs2;
import parser;
import precomp;
import scopeh;

extern (C++):

alias dbg_printf = printf;
alias MEM_PH_MALLOC = mem_malloc;
alias MEM_PH_CALLOC = mem_calloc;
alias MEM_PH_FREE = mem_free;
alias MEM_PH_STRDUP = mem_strdup;
alias MEM_PARF_MALLOC = mem_malloc;
alias MEM_PARF_CALLOC = mem_calloc;
alias MEM_PARF_REALLOC = mem_realloc;
alias MEM_PARF_FREE = mem_free;
alias MEM_PARF_STRDUP = mem_strdup;


type* tserr(); // { return tstypes[TYint]; }


__gshared list_t symlist;                  // for C

/* Array to give the 'relaxed' type for relaxed type checking   */
extern __gshared ubyte[TYMAX] _tyrelax;
uint type_relax() { return config.flags3 & CFG3relax; }     // !=0 if relaxed type checking

// !=0 if semi-relaxed type checking
uint type_semirelax()
{
    return config.exe & EX_posix ? (config.flags3 & CFG3semirelax) : type_relax();
}

int REGSIZE() { return _tysize[TYnptr]; }


/*******************************
 * Read list of comma-separated arguments into *parglist.
 * Input:
 *      tok.TKval       Token past (
 * Output:
 *      tok.TKval       )
 */

void getarglist(list_t *parglist)
{
    //printf("+getarglist()\n");
    *parglist = null;
    if (tok.TKval != TKrpar)            /* if parameters                */
        while (true)
        {   elem *e;

            e = assign_exp();
//elem_print(e);
//type_print(e.ET);
            if (e.Eoper != OPconst)
                e = poptelem(arraytoptr(e));
            list_append(parglist,e);
            if (tok.TKval != TKcomma)
                break;
            parglist = &(*parglist).next;
            stoken();
        }
    //printf("-getarglist()\n");
}

/*******************************
 * Replace (e) with ((stmp = e),stmp)
 * The destructor will eventually be called for stmp
 */

elem *exp2_copytotemp(elem *e)
{
    type *t;
    Symbol *stmp;
    elem *eeq;

    //dbg_printf("exp2_copytotemp(%p)\n",e);
    elem_debug(e);
    assert(CPP);
    t = e.ET;
    type_debug(t);
    stmp = symbol_genauto(t);
    if (type_struct(t) && !errcnt)
    {   list_t arglist;

        arglist = list_build(e,null);
        eeq = cpp_constructor(el_ptr(stmp),t,arglist,null,null,0);
    }
    else
        eeq = el_bint((tyaggregate(t.Tty) ? OPstreq : OPeq),t,el_var(stmp),e);
    return el_bint(OPcomma,t,eeq,el_var(stmp));
}

/************************************
 * Determine function return method.
 * Input:
 *      tfunc   type of function
 * Returns:
 *      RET_XXXX
 */

int exp2_retmethod(type *tfunc)
{
    /* For C++ and Pascal, structs and reals are on the stack.
       For C, reals and 1,2,4 byte structs are return in registers,
       others are returned in a static.
     */
    /* BUG: For TARGET_LINUX, and a RET_STACK return, change TYnfunc to TYhfunc.
     */
    tym_t ty;
    int mangle;
    targ_size_t sz;
    type *tn = tfunc.Tnext;

    if (errcnt)
        return RET_REGS;

    ty = tybasic(tn.Tty);
    assert(ty != TYtemplate);
    mangle = type_mangle(tfunc);

    if (ty == TYstruct)
    {
        template_instantiate_forward(tn.Ttag);
        if (I32)
            goto L1;


        if (mangle == mTYman_pas)
            return RET_PSTACK;
        if (mangle == mTYman_cpp)
            return RET_STACK;
    L1:
        if (CPP)
        {   struct_t *st;

            st = tn.Ttag.Sstruct;

            /* Set STRbitcopy (should probably do this elsewhere)       */
            if (!(st.Sflags & STRbitcopy))
            {
                //n2_createopeq(tn.Ttag,1);
                n2_createcopyctor(tn.Ttag,1);
            }

            /* Return struct on stack if it has any constructors
               or destructors. Since C structs cannot have ctors or
               dtors, we are compatible, while avoiding the problems
               of constructing/destructing an object that is in
               registers or a static!
             */
            if ((st.Sflags & (STRanyctor | STRbitcopy)) != STRbitcopy ||
                st.Sdtor)
                return RET_STACK;
        }

static if (0)
{
        if (!OPT_IS_SET(OPTfreg_struct_return))
            return RET_STACK;
}
        switch (type_size(tn))
        {   case CHARSIZE:
            case SHORTSIZE:
            case LONGSIZE:
                return RET_REGS;        /* return small structs in regs */
                                        /* (not 3 byte structs!)        */
            case LLONGSIZE:
                if (I32)
                    return RET_REGS;
                break;

            default:
                break;
        }
        /* For 32 bit code generation, don't repeat the old mistake
           of returning structs in a static.
         */
        return (I32) ? RET_STACK : RET_STATIC;
    }
    else if (tybasic(tfunc.Tty) == TYjfunc)
        return RET_REGS;
    else if (typfunc(tfunc.Tty) && tyfloating(ty))
        return RET_STACK;
    else
        return RET_REGS;
}

/************************************
 * Determine type of hidden argument when return type is RET_STACK.
 * Input:
 *      tfunc   type of function
 * Returns:
 *      type of hidden argument
 */

type *exp2_hiddentype(type *tfunc)
{
    type *thidden = newpointer(tfunc.Tnext);

    debug assert(exp2_retmethod(tfunc) & (RET_STACK | RET_PSTACK));
    if (!type_struct(tfunc.Tnext))
        thidden.Tty = I16 ? TYsptr : TYnptr;
    else if (thidden.Tty == TYnptr && I16)
        thidden.Tty = TYsptr;  /* always stack relative        */
    return thidden;
}

/***********************************
 * Take the address of an expression.
 */

elem *exp2_addr(elem *e)
{
    //dbg_printf("exp2_addr(%p)\n",e);
    /* Convert &*(far *)handle to handle        */
    if (e.Eoper == OPind &&
        (e.EV.E1.Eoper == OPvp_fp || e.EV.E1.Eoper == OPcvp_fp))
    {   elem *eh;

        eh = e;
        e = e.EV.E1.EV.E1;
        eh.EV.E1.EV.E1 = null;
        el_free(eh);
    }
    else
    {
        if (CPP)
        {
            // JTM: Moved this code from dodot.  It was causing a problem
            // for implicit calls to operator methods when a method result
            // was being used for an operator on a small object mac bug #5410
            elem *eTemp;

            eTemp = e;
            while (1)
            {
                switch (eTemp.Eoper)
                {
                    case OPcomma:
                        eTemp = eTemp.EV.E2;
                        continue;

                    case OPcond:
                    case OPvar:
                    case OPind:
                    case OPstreq:
                    case OPeq:
                        break;
                    default:
                        e = exp2_copytotemp(e);
                        break;
                }
                break;
            }
        }
        e = el_unat(OPaddr,type_ptr(e,e.ET),e);
    }
    return e;
}

/******************************************
 * Do a qualified name lookup.
 * Input:
 *      sclass          if !=null, starting context
 *      flags   1       don't look in local scopes
 * Output:
 *      *pflags 1       dependent type
 * Returns:
 *      null    Ending token is not an identifier symbol,
 *              like X::Y::operator*(...)
 *              or X::Y::~Y()
 *              Current token is TKoperator or TKcom
 *      !=null  Symbol of Z in X::Y::Z
 *              Current token is token following Z identifier
 */

Symbol *exp2_qualified_lookup(Classsym *sclass, int flags, int *pflags)
{
    int global;
    char *vident;
    uint sct = SCTglobal | SCTnspace | SCTtempsym | SCTclass |
                   SCTcover | SCTlocal;
    Symbol *s = null;
    if (pflags)
        *pflags = 0;

    global = 0;
    if (tok.TKval == TKcolcol)
    {   global = 1;
        sct = SCTglobal /*| SCTnspace*/ | SCTtempsym | SCTcover;
        sclass = null;
        stoken();
    }

    if (tok.TKval == TKtemplate)
        stoken();       // BUG: check following ident really is a template

    if (tok.TKval == TKident)
    {
        //vident = alloca_strdup(tok.TKid);
        {
        size_t len = strlen(tok.TKid) + 1;
        auto _s = alloca(len);
        vident = cast(char *) memcpy(_s,tok.TKid,len);
        }
        stoken();
        if (tok.TKval == TKcolcol)
            sct |= SCTcolcol;
        token_unget();
        if (flags & 1)
            sct &= ~SCTlocal;

        if (!sclass)
            s = scope_search(vident, sct);
        else
            s = cpp_findmember_nest(&sclass, vident, 1);
    }
    else if (tok.TKval == TKsymbol)
    {
        s = tok.TKsym;
    }

    while (s)
    {
        switch (s.Sclass)
        {
            case SCstruct:
                template_instantiate_forward(cast(Classsym *)s);
                if (stoken() == TKcolcol)
                {   Symbol *smem;

                    stoken();
                    if (tok.TKval == TKtemplate)
                        stoken();       // BUG: check following ident really is a template
                    if (tok.TKval == TKident)
                    {
                        smem = cpp_findmember(cast(Classsym *)s, tok.TKid, 1);
                        if (smem)
                        {
                            cpp_memberaccess(smem,global ? null : funcsym_p,cast(Classsym *)s);
                            s = smem;
                            continue;
                        }
                    }
                    else
                    {   token_unget();
                        tok.TKval = TKcolcol;
                    }
                }
                if (type_isdependent(s.Stype) && pflags)
                    *pflags |= 1;
                return s;

            case SCnamespace:
                s = nspace_qualify(cast(Nspacesym *)s);
                continue;

            case SCtemplate:
                s = template_expand(s, 1);
                continue;

            case SCalias:
                s = s.Smemalias;
                continue;

            case SCtypedef:
                if (type_isdependent(s.Stype) && pflags)
                    *pflags |= 1;
                if (n2_isstruct(&s))
                    continue;
                break;

            default:
                if (s.Scover)
                {
                    stoken();
                    if (tok.TKval == TKcolcol)
                    {
                        token_unget();
                        s = s.Scover;
                        continue;
                    }
                    return s;
                }
                break;
        }
        break;
    }

    stoken();
    return s;
}


/************************************
 * Determine if token is a simple type name.
 * If it is, return the type, else null.
 */

/*private*/ type * exp2_issimpletypename()
{   type *t = null;
    Symbol *s;

    switch (tok.TKval)
    {
        case TKbool:            t = tstypes[TYbool];     break;
        case TKchar:            t = tstypes[TYchar];     break;
        case TKchar16_t:        t = tstypes[TYchar16];   break;
        case TKchar32_t:        t = tstypes[TYdchar];    break;
        case TKwchar_t:         t = (config.flags4 & CFG4wchar_is_long)
                                        ? tstypes[TYdchar] : tstypes[TYwchar_t];
                                                break;
        case TKshort:           t = tstypes[TYshort];    break;
        case TKsigned:
        case TKint:             t = tstypes[TYint];      break;
        case TKlong:            t = tstypes[TYlong];     break;
        case TKunsigned:        t = tstypes[TYulong];    break;
        case TKfloat:           t = tstypes[TYfloat];    break;
        case TKdouble:          t = tstypes[TYdouble];   break;
        case TKvoid:            t = tstypes[TYvoid];     break;
        case TKsymbol:
            s = tok.TKsym;
            goto L1;

        case TKident:
            /* It's a simple-type-name if it's a complete-class-name or qualified-type-name */
            s = symbol_search(tok.TKid);
        L1:
            if (s)
            {   switch (s.Sclass)
                {
                    case SCtypedef:
                        if (type_struct(s.Stype))
                            break;
                        goto case SCenum;

                    case SCenum:
                    //case SCtemplate:
                        t = s.Stype;
                        break;

                    default:
                        break;
                }
            }
            break;

        default:
            break;
    }
    return t;
}

/************************************
 * Parse explicit call of a destructor for a simple type name, as in:
 *      int e;
 *      e.int::~int();
 *      e.~int();
 * Input:
 *      current token is token following dot
 * Returns:
 *      (void) e;
 */

/*private*/ elem * exp2_simpledtor(elem *e,type *t)
{   tym_t ty;
    Symbol *s;

    //printf("exp2_simpledtor()\n");
    if (!e)
        goto err;
    elem_debug(e);
    ty = tybasic(t.Tty);
    if (tok.TKval == TKcom)
        goto L1;                        /* e.~int()             */
    t = exp2_issimpletypename();
    if (t)
    {
        /* e.int::~int()
         */
        if (ty != tybasic(t.Tty) ||
            stoken() != TKcolcol ||
            stoken() != TKcom)
            goto err;
    L1:
        stoken();
        t = exp2_issimpletypename();
    L2:
        if (t &&
            ty == tybasic(t.Tty) &&
            stoken() == TKlpar &&
            stoken() == TKrpar)
        {
            stoken();
            return _cast(e, tstypes[TYvoid]);
        }
        else
            goto err;
    }

    s = exp2_qualified_lookup(null, 0, null);
    if (!s)
        goto err;
    if (s.Sclass != SCtypedef)
        goto err;
    t = s.Stype;
    if (ty != tybasic(t.Tty) ||
        tok.TKval != TKcolcol ||
        stoken() != TKcom)
        goto err;

    stoken();
    if (tok.TKval != TKident)
        goto err;
    if (s.Sscope)
    {
        if (s.Sscope.Sclass == SCstruct)
            s = cpp_findmember(cast(Classsym *)s.Sscope, tok.TKid, 1);
        else if (s.Sscope.Sclass == SCnamespace)
            s = nspace_search(tok.TKid, cast(Nspacesym *)s.Sscope);
        if (!s || (s.Sclass != SCtypedef && s.Sclass != SCenum))
            goto err;
        t = s.Stype;
    }
    else
        t = exp2_issimpletypename();
    goto L2;

err:
    synerr(EM_simple_dtor);             // invalid simple destructor
    return e;
}


/***********************
 * E1.MOS is converted to *(&E1 + offset(MOS))
 * If bit field, create
 *      (*(&E1 + offset(MOS))) OPbit (width * 256 + bit)
 * We can't do more than this, since we don't know if it's an
 * lvalue or rvalue.
 * Special handling if MOS is an array.
 * Input:
 *      e1      instance of struct (possibly null if static member)
 *      tclass  type of that struct
 */

elem *dodot(elem *e1,type *tclass, bool bColcol)
{ type *at;
  Classsym *sclass;
  Symbol *s;
  elem* e,eplus,mos,ethis;
  tym_t modifiers;
  char lvalue = 1;              // assume result will be an lvalue
  elem **pe;
  Classsym *sowner;
  Classsym *sclass0;
  Classsym *sclass0addr;
  Classsym *stmp;
  char *vident;
  char direct;
  char destructor;
  char thisptr;
  Symbol *sTempl = null;
  int result;

    if (!tclass)                        // could happen if int.member
        return el_settype(e1,tserr);    // patch things up

static if (0)
{
    dbg_printf("dodot(e1 = %p, tclass = %p)\n",e1,tclass);
    elem_print(e1);
    type_print(tclass);
    tok.print();
}

    type_debug(tclass);
    if (!type_struct(tclass))
    {
        if (CPP)
        {
            return exp2_simpledtor(e1,tclass);
        }
        else
        {
            synerr(EM_not_struct);              // not a struct or union type
            ethis = e1;
            goto err;
        }
    }
    else
        sclass = tclass.Ttag;

  if (CPP)
  {
    sclass0 = sclass;                   /* remember starting point      */
    sclass0addr = null;
    if (e1)
    {
        /* If expression is not something we can take the address of, we   */
        /* copy the expression to a temporary and take the address of that */
        modifiers = e1.ET.Tty & (mTYconst | mTYvolatile | mTYLINK);
        thisptr = 1;
        ethis = exp2_addr(e1);
    }
    else
    {   modifiers = 0;
        thisptr = 0;
        ethis = el_longt(newpointer(tclass),0);
    }
    pe = &ethis;
    direct = false;                     /* do not call function directly */
    destructor = false;
L2:
    if (tok.TKval == TKident || tok.TKval == TKsymbol)
    {
Lid3:
        Symbol *sc = null;

        stmp = sclass;
        if (tok.TKval == TKident)
        {
            //vident = alloca_strdup(tok.TKid);
            {
            size_t len = strlen(tok.TKid) + 1;
            auto _s = alloca(len);
            vident = cast(char *) memcpy(_s,tok.TKid,len);
            }

            sTempl = null;
            stoken();
            if (tok.TKval == TKlt)
            {
                sc = scope_search(vident,SCTglobal | SCTnspace);
                if (sc && sc.Sclass == SCtemplate)
                {
                    sTempl = sc;
                    sc = template_expand(sc,2);
                    if (sc && stoken() == TKcolcol)
                        goto Lid2;
                }
            }
        }
        else // TKsymbol
        {
            sc = tok.TKsym;
            stoken();
            vident = &sc.Sident[0];
            if (sc.Sclass == SCstruct)
            {   sTempl = sc.Sstruct.Stempsym;
                if (sTempl)
                    goto Lid2;
                if (tok.TKval == TKcolcol)
                    goto Lid2;
            }
        }
        if (tok.TKval == TKcolcol)          /* p.class::member */
        {
            if (stmp)
                sc = cpp_findmember_nest(&stmp, vident, 0);
            else
                sc = symbol_search(vident);
        Lid:
            if (sc)
            {
                switch (sc.Sclass)
                {
                    case SCtypedef:
                        if (tybasic(sc.Stype.Tty) == TYstruct)
                        {   sc = cast(Symbol *)sc.Stype.Ttag;
                            goto Lid2;
                        }
                        break;

                    case SCenum:
                        if (stoken() != TKident)
                            synerr(EM_ident_exp);               // identifier expected
                        else
                        {   Symbol *se;

                            se = symbol_searchlist(sc.Senum.SEenumlist,tok.TKid);
                            if (!se)
                                cpperr(EM_not_enum_member,tok.TKid,cpp_prettyident(sc));        // not a member
                            goto L2;
                        }
                        break;

                    case SCstruct:
                        tclass = sc.Stype;
                        sclass = tclass.Ttag;
                        sclass0 = sclass;
                        /* "this" pointer is now null   */
                        el_free(ethis);
                        modifiers = 0;
                        thisptr = 0;
                        ethis = el_longt(newpointer(tclass),0);
                        stoken();
                        goto L2;

                    default:
                        if (sc.Scover)
                        {   sc = sc.Scover;
                            goto Lid;
                        }
                        break;
                }
            }
            sc = scope_search(vident,SCTglobal | SCTnspace | SCTtempsym | SCTtemparg | SCTlocal);
            if (!sc && sclass.Sscope && sclass.Sscope.Sclass == SCnamespace)
            {
                sc = sclass;
            }
            if (sc && type_struct(sc.Stype))
                sc = sc.Stype.Ttag;

            /* Class sc must be the same as sclass or a base class of it */
        Lid2:
            if (!sc)
            {
                cpperr(EM_public_base,vident,&sclass.Sident[0]);   // vident must be a public base class
                goto err;
            }

            stoken();

            if (tok.TKval == TKtemplate)                // ::template
            {   stoken();
                if (tok.TKval != TKident)
                    goto Lident_err;
                // BUG: should verify that identifier really does name a template
            }

            //printf("sc = '%s', sclass = '%s'\n", &sc.Sident[0], &sclass.Sident[0]);
            if (!c1isbaseofc2(pe,cast(Classsym *)sc,sclass))
            {
                if (tok.TKval == TKident)
                {
                    //char *p = alloca_strdup(tok.TKid);
                    char* p;
                    {
                    size_t len = strlen(tok.TKid) + 1;
                    auto _s = alloca(len);
                    p = cast(char *) memcpy(_s,tok.TKid,len);
                    }

                    Symbol* sx = sc;
                    sx = cpp_findmember_nest(cast(Classsym **)&sx,tok.TKid,0);
                    if (sx && type_struct(sx.Stype))
                        sx = sx.Stype.Ttag;
                    if (stoken() == TKcolcol)
                    {
                        sc = sx;
                        goto Lid2;
                    }
                    sclass = cast(Classsym *)sc;
                    sclass0 = sclass;
                    tclass = sclass.Stype;
                    token_unget();
                    token_setident(p);
                    goto L2;
                }
                cpperr(EM_public_base,vident,&sclass.Sident[0]);   // vident must be a public base class
                goto err;
            }

            sclass = cast(Classsym *)sc;
            tclass = sclass.Stype;
            if (!sclass0addr)
                sclass0addr = sclass;
            direct = true;      /* if virtual function, call it directly */
            if (tok.TKval == TKcom)     /* if p.X::~X()        */
            {
                stoken();
                if (tok.TKval == TKident)
                {
                    Symbol *sx;
                    Classsym *stmp2 = cast(Classsym *)sc;
                    int r;

                    sx = cpp_findmember_nest(&stmp2,tok.TKid,0);
                    if (!sx && !template_classname(tok.TKid, sclass))
                        // Necessary if X is a typedef that is not
                        // nested in the scope of sc.
                        sx = scope_search(tok.TKid, SCTglobal | SCTnspace | SCTtempsym | SCTtemparg | SCTlocal);
                    if (sx && type_struct(sx.Stype))
                        sx = sx.Stype.Ttag;
                    r = template_classname(tok.TKid, sclass);
                    if ( (!sx || (sTempl != sx && sclass != sx)) &&
                        !r
                       )
                        cpperr(EM_tilde_class,&sclass.Sident[0]);  // class name expected
                    stoken();
                    if (sTempl && r && tok.TKval == TKlt)
                    {
                        // Could be p.X<T>::~X<T>()
                        Symbol *sy = template_expand(sTempl, 2);
                        if (sy != sclass)
                            cpperr(EM_tilde_class,&sclass.Sident[0]);      // class name expected
                        stoken();
                    }
                    vident = cpp_name_dt.ptr;
                    destructor = true;
                }
                else
                    synerr(EM_ident_exp);                       // identifier expected
                goto L3;
            }
            else if (tok.TKval == TKident)
            {   // Look for casts of the form p.X::X()
                Symbol *sx;
                Classsym *stmp2 = cast(Classsym *)sc;

                sx = cpp_findmember_nest(&stmp2,tok.TKid,0);
                //sx = scope_search2(tok.TKid, SCTglobal | SCTnspace | SCTtempsym | SCTtemparg | SCTlocal);
                if (sx && type_struct(sx.Stype) && !(sytab[sx.Sclass] & SCEXP))
                    sx = sx.Stype.Ttag;
                if (sx && (sx == sclass || sx == sTempl))
                {   // It's a cast of the form p.X::X()
                    el_free(ethis);
                    stoken();
                    return exp_simplecast(tclass);
                }
            }
            goto L2;
        }
  }
  else if (tok.TKval == TKoperator)
  {     type *t;
        int oper;
        char *v;

        if (stoken() == TKeq)           // if operator=
            n2_createopeq(sclass,0);
        token_unget();
        v = cpp_operator(&oper,&t);

        //vident = alloca_strdup(v);
        {
        size_t len = strlen(v) + 1;
        auto _s = alloca(len);
        vident = cast(char *) memcpy(_s,v,len);
        }

        stmp = sclass;
        s = cpp_findmember_nest(&stmp, vident, 0);
        if (!s)
        {
            // Look for conversion operator template
            Match m;
            s = cpp_typecast(sclass.Stype, t, &m);
            if (!s)
                err_notamember(vident, sclass);
        }
        type_free(t);
        goto Lsymfound;
  }
  else if (tok.TKval == TKcom)          /* if p.~X()                   */
  {
        stoken();
        if (tok.TKval == TKident)
        {
            Symbol *sx;

            sx = scope_search(tok.TKid, SCTglobal | SCTnspace | SCTtempsym | SCTtemparg | SCTlocal);
            if (sx && type_struct(sx.Stype))
                sx = sx.Stype.Ttag;
            if ( (!sx || (sTempl != sx && sclass != sx)) &&
                !template_classname(tok.TKid,sclass)
               )
                cpperr(EM_tilde_class,&sclass.Sident[0]);  // class name expected
            vident = cpp_name_dt.ptr;
            destructor = true;
            stoken();
        }
        else
            synerr(EM_ident_exp);                       // identifier expected
L3:
    ;
  }
  else if (tok.TKval == TKtemplate)     // if x.template foo<..>
  {
        stoken();
        if (tok.TKval == TKident)
        {   // BUG: check that tok.TKident is a template followed by <...>
            goto Lid3;
        }
        if (tok.TKval == TKsymbol)
        {   // BUG: check that tok.TKsym is an implementation of a struct
            goto Lid3;
        }
        goto Lident_err;
  }
  else
  {
Lident_err:
        synerr(EM_ident_exp);                   // identifier expected
        goto err;
  }

  /* try to find the Symbol */
    {
        stmp = sclass;
        s = cpp_findmember_nest(&stmp,vident,destructor ^ 1);
    }
Lsymfound:
    if (s)
    {
        if (stmp != sclass)             // if class was in enclosing scope
        {
            if (destructor)
                goto Ldtor;

            // We have found a member of a nested class, which
            // doesn't have a 'this' pointer
            sclass0 = stmp;
            sclass = stmp;
            tclass = sclass.Stype;

            // "this" pointer is now null
            el_free(ethis);
            modifiers = 0;
            thisptr = 0;
            ethis = el_longt(newpointer(tclass),0);
        }

        while (s.Sclass == SCmemalias)
        {
            if (thisptr)
            {   symlist_t ul;

                for (ul = s.Spath; ul; ul = list_next(ul))
                {   Classsym *sc;
                    int i;

                    sc = cast(Classsym *)list_symbol(ul);
                    i = c1isbaseofc2(&ethis,sc,sclass);
                    assert(i);
                    sclass = sc;
                }
            }
            s = s.Smemalias;
        }
    }
    else
    {   if (destructor)
        {   /* No effect if non-existent destructor is called   */
        Ldtor:
            chktok(TKlpar,EM_lpar);
            chktok(TKrpar,EM_rpar);
            return _cast(ethis,tstypes[TYvoid]);  /* evaluate any side effects    */
        }
        goto err;
    }
    sowner = cast(Classsym *)s.Sscope;
    // Need to check if function because if overloaded function, and
    // s is a static function, but we'll select a virtual one.
    if (s.needThis() || tyfunc(s.Stype.Tty))
    {
        result = c1isbaseofc2(pe,sowner,sclass);
        assert(result);
    }
  }
  else  // !CPP
  {
    // If expression is not something we can take the address of, we
    // copy the expression to a temporary and take the address of that
    e = e1;
    while (e.Eoper == OPcomma)
        e = e.EV.E2;
    if (e.Eoper != OPvar && e.Eoper != OPind)
    {   /* Generate e1 <== (tmp = e1),tmp       */
        Symbol *stmpx;

        stmpx = symbol_genauto(tclass);
        e = el_bint(OPstreq,tclass,el_var(stmpx),e1);
        block_appendexp(curblock, e);
        e1 = el_var(stmpx);
        lvalue = 0;                     // result won't be an lvalue
    }

    modifiers = e1.ET.Tty & (mTYconst | mTYvolatile | mTYLINK);
    ethis = exp2_addr(e1);
    if (tok.TKval != TKident)
    {   synerr(EM_ident_exp);                   /* identifier expected          */
        goto err;
    }

    /* try to find the Symbol */
    s = n2_searchmember(sclass,tok.TKid);
    if (!s)
    {   // forward reference / not a member
        err_notamember(tok.TKid,sclass);
        goto err;
    }
    stoken();
  }

    at = newpointer(s.Stype);  /* type is pointer to MOS       */
    if (!tyfunc(s.Stype.Tty))
        type_setty(&at.Tnext,at.Tnext.Tty | modifiers);

    if (!CPP || thisptr)
    {
        at.Tty = ethis.ET.Tty;       /* pointer type should match this */
        if (ethis.Eoper == OPaddr && ethis.EV.E1.Eoper == OPind)
        {   type *t = ethis.EV.E1.EV.E1.ET;

            /* if insizeof, this may not be a pointer (it may be an array) */
            if (typtr(t.Tty))
                at.Tty = t.Tty;
        }
    }

  if (CPP)
  {
    if (!tyfunc(s.Stype.Tty))
    {
        if (!pstate.STdeferaccesscheck)
        {   /* BUG: what we should do here for STdeferaccesscheck
             * is collect all the parameters to cpp_memberaccess()
             * and call them later.
             */
            if (pstate.STisaddr && sclass0addr && s.needThis())
                cpp_memberaccess(s,bColcol ? null : funcsym_p,sclass0addr);
            else
                cpp_memberaccess(s,bColcol ? null : funcsym_p,sclass0);
        }
    }

    switch (s.Sclass)
    {
        case SCmember:
        case SCfield:
        case SCinline:
        case SCfuncalias:
        case SCfunctempl:
            break;
        case SCglobal:
        case SCextern:
        case SCstatic:
        case SCcomdat:
        case SCcomdef:
        case SCftexpspec:
        case SCsinline:
            /* static member of class   */
            if (!tyfunc(s.Stype.Tty))
            {
                e = el_var(s);
                if (thisptr)
                {
                    e.PEFflags |= PEFaddrmem;
                }
                e = reftostar(e);
                goto L10;
            }
            break;
        case SCconst:
            e = el_copytree(s.Svalue);
        L10:
            at.Tcount++;                       // so it will free properly
            type_free(at);
            el_free(ethis);
            return e;
        case SCenum:
        case SCtypedef:
        case SCstruct:
            if (tok.TKval == TKlpar)
            {
                e = exp_simplecast(s.Stype);
                goto L10;
            }
            else
                synerr(EM_not_variable,prettyident(s),"".ptr);      // s is not a variable
            break;
        case SCauto:
            synerr(EM_member_auto, prettyident(s));
            e = el_var(s);
            break;

        default:
            symbol_print(s);
            assert(0);
    }
    if (tyfunc(s.Stype.Tty))
    {
        Funcsym *sfunc;
        Funcsym *ambig;
        list_t arglist;
        elem* e2,ec;
        type *tthis;
        param_t *ptal = null;
        ubyte flags = 0;

        at.Tcount++;                   /* so it will free properly     */
        type_free(at);

        if (tok.TKval == TKlt || tok.TKval == TKlg)
        {
            Symbol *s2;

            for (s2 = s; s2; s2 = s2.Sfunc.Foversym)
            {
                if (s2.Sclass == SCfunctempl)
                {
                    s = s2;
                    ptal = template_gargs(s);
                    stoken();
                    flags = PEFtemplate_id;
                    break;
                }
            }
        }

        if (tok.TKval != TKlpar)        // construct pointer to member
        {
            targ_size_t d;
            elem *ecx;

            assert(!ptal);      // BUG: don't know how to deal with pointer to template function
            ethis = poptelem(ethis);
            if (thisptr)
            {
                switch (ethis.Eoper)
                {   case OPvar:
                    case OPind:
                    case OPrelconst:
                        d = 0;
                        goto L9;
                    case OPadd:
                        ecx = ethis.EV.E2;
                        break;
                    default:
                        synerr(EM_lpar2, "function".ptr);   // '(' expected
                        goto L9;
                }
            }
            else
                ecx = ethis;
            if (ecx.Eoper != OPconst)
                /* must not be a virtual base */
                cpperr(EM_vbase,&sowner.Sident[0],&sclass.Sident[0]);
            else
                d = ecx.EV.Vint;                /* offset to start of sowner */

        L9:
            e = poptelem(el_ptr(s));
            e.EV.Voffset = d;
            if (!(s.Sfunc.Fflags & Fstatic))
            {   el_settype(e,type_allocmemptr(sclass,e.ET.Tnext));
                e.EV.ethis = ethis;
            }
            else
                el_free(ethis);
            cpp_memberaccess(s,funcsym_p,sclass0);
            return e;
        }

        chktok(TKlpar,EM_lpar);
        getarglist(&arglist);           /* read list of parameters      */
        if (thisptr)
            tthis = ethis.ET.Tnext;
        else
            tthis = null;
        sfunc = cpp_overload(s,tthis,arglist,sclass0,ptal,(flags != 0));

        while (sfunc.Sclass == SCfuncalias)
        {   // Adjust 'this' pointer
            if (thisptr)
            {   symlist_t ul;

                for (ul = sfunc.Spath; ul; ul = list_next(ul))
                {   Classsym *sc;
                    int i;

                    sc = cast(Classsym *)list_symbol(ul);
                    i = c1isbaseofc2(&ethis,sc,sclass);
                    assert(i);
                    sclass = sc;
                }
            }
            sfunc = sfunc.Sfunc.Falias;
            sowner = cast(Classsym *)sfunc.Sscope;
        }

        /* Convert ethis to default pointer type        */
        if (thisptr)
            ethis = _cast(ethis,newpointer(ethis.ET.Tnext));

        if (destructor && thisptr)
            // Explicitly called destructors are not part of the EH mechanism
            ec = cpp_destructor(sowner.Stype,ethis,null,
                DTORnoaccess |                  // access check already done
                DTORmostderived | DTORnoeh | (direct ? 0 : DTORvirtual));
        else
        {
            if (direct)
                e = el_ptr(sfunc);
            else
                e = cpp_getfunc(sowner.Stype,sfunc,&ethis);

            e = el_unat(OPind,e.ET.Tnext,e);
            e = poptelem(e);            /* collapse out *& and &*       */

            if (sfunc.Sfunc.Fflags & Fstatic)
            {   el_free(ethis);
                ethis = null;
            }
            else if (!thisptr)
            {
                err_noinstance(sclass,s);
                el_free(ethis);
                ethis = null;
            }
            else
            {
                tym_t tym = ethis.ET.Tnext.Tty & (mTYconst | mTYvolatile);
                /* if this modifier const or volatile need same modifier on func ?? */
                if (tym & ~sfunc.Stype.Tty)
                    typerr(EM_cv_arg,ethis.ET,sfunc.Stype);   /* type mismatch */
            }
            ec = xfunccall(e,ethis,null,arglist);
        }
        chktok(TKrpar,EM_rpar);
        return ec;
  }

    if (!thisptr)
    {
        if (s.Sclass == SCmember && (pstate.STisaddr || pstate.STinsizeof))
        {
            /* Will be turned into a pointer to member by una_exp()     */
            targ_size_t d;

            // Disallow if current class is nested inside sclass
            if (funcsym_p && !pstate.STisaddr)
            {
                Symbol *sc = cast(Symbol *)funcsym_p.Sscope;
                while (sc)
                {
                    if (sc == sclass)
                    {   err_noinstance(sclass, s);
                        break;
                    }
                    sc = sc.Sscope;
                }
            }

            ethis = poptelem(ethis);
            if (ethis.Eoper != OPconst)
                /* must not be a virtual base */
                cpperr(EM_vbase,&sowner.Sident[0],&sclass.Sident[0]);
            else
                d = ethis.EV.Vint;             /* offset to start of sowner */

            /* Construct a special elem. The type is the type of the    */
            /* member, but the fields contain the info so we can later  */
            /* construct a memptr out of it with una_exp()              */
            e = el_var(s);
            e.EV.Vsym = sclass;
            e.EV.Voffset = d + s.Smemoff;  /* offset from this     */
            if (tybasic(s.Stype.Tty) != TYarray) /* result is pointer to array        */
                el_settype(e,at.Tnext);
        }
        else
        {
            err_noinstance(sclass,s);
            e = el_longt(tserr,1);
        }
        goto L10;
    }
  }

  // Make huge pointers far. We operate on the assumption that a
  // struct does *NOT* cross segment boundaries, so we wish to avoid
  // the heavy penalty of huge arithmetic.
  if (tybasic(ethis.ET.Tty) == TYhptr)
        type_setty(&ethis.ET,(ethis.ET.Tty & ~mTYbasic) | TYfptr);

  mos = el_longt(tstypes[TYint],s.Smemoff);             // offset of member
  eplus = el_bint(OPadd,at,ethis,mos);          // &e1 + mos

  e = el_unat(OPind,s.Stype,eplus);    /* *(&e1 + mos)                 */
  if (s.Sflags & SFLmutable)
        modifiers &= ~mTYconst;
  type_setty(&e.ET,e.ET.Tty | modifiers);
  handleaccess(e);
  if (tyref(e.ET.Tty))
        e = reftostar(e);
  if (s.Sclass == SCfield)             /* if a bit field               */
  {
        /* Take care of bit fields */
        mos = el_longt(tstypes[TYuint],s.Swidth * 256 + s.Sbit);
        e = el_bint(OPbit,s.Stype,e,mos);
  }
  if (!lvalue)
        e.PEFflags |= PEFnotlvalue;
  e.PEFflags |= PEFmember;
  e.Emember = s;
  return e;

err:
    /* ethis could be null      */
    return ethis ? ethis : el_longt(tserr,0);
}


/*************************
 * C11 6.5.2.1-2
 * E1[E2] is converted to *(E1 + E2*s1)
 * No error checking is done.
 */

elem *doarray(elem *e1)
{   elem* e2,eplus;
    type* t1,t2;

    elem_debug(e1);
    stoken();
    e1.PEFflags |= PEFaddrmem;
    e1 = arraytoptr(e1);
    e2 = expression();
    chktok(TKrbra,EM_rbra);
    if (CPP)
    {
        // postpone convertchk() so overloaded OPbrack works correctly
        eplus = el_bint(OPbrack,null,e1,e2);
        {   elem *e;

            /* See if [] operator is overloaded */
            if ((e = cpp_opfunc(eplus)) != null)
                return e;
        }
        eplus.EV.E2 = e2 = convertchk(e2);
        eplus.Eoper = OPadd;
    L1: ;
    }
    else
    {
        e2 = arraytoptr(e2);
        eplus = el_bint(OPadd,null,e1,e2);
    }
    t2 = e2.ET;
    t1 = e1.ET;
    if (typtr(t1.Tty))
    {   el_settype(eplus,t1);
        if (CPP && tybasic(t2.Tty) == TYstruct)
        {
            if (cpp_cast(&e2, tstypes[TYint], 1))
            {   eplus.EV.E2 = e2;
                goto L1;
            }
        }
    }
    else if (typtr(t2.Tty))
    {   el_settype(eplus,t2);
        if (!CPP)
        {   // Swap operands
            eplus.EV.E1 = e2;
            eplus.EV.E1 = e1;
        }
    }
    // Look for conversion of e1 or e2 to a pointer
    else if (CPP && tybasic(t1.Tty) == TYstruct && tyintegral(t2.Tty) &&
        cpp_casttoptr(&e1))
    {
        eplus.EV.E1 = e1;
        goto L1;
    }
    else if (CPP && tybasic(t2.Tty) == TYstruct && tyintegral(t1.Tty) &&
        cpp_casttoptr(&e2))
    {
        eplus.EV.E2 = e2;
        goto L1;
    }
    else
    {
        typerr(EM_array,t1,cast(type *) null);
        return el_settype(eplus,tserr);
    }
    scale(eplus);
    e1 = el_unat(OPind,eplus.ET.Tnext,eplus);
    if (tyref(e1.ET.Tty))             // if <reference to>
    {   // convert (e) to (*e)
        e1 = reftostar(e1);
    }
    handleaccess(e1);
    return e1;
}


/***************************
 * Convert E1.MOS to (*E1).MOS, let dodot() do the rest.
 */

elem *doarrow(elem *e1)
{   type *t;

    elem_debug(e1);
    e1 = arraytoptr(e1);
    if (CPP)
    {
        // Recursively expand ., as x.m is interpreted as (x.operator.()).m
        while (1)
        {   elem *eo;

            e1 = el_unat(OParrow,e1.ET,e1);
            if ((eo = cpp_opfunc(e1)) == null)
                break;
            e1 = eo;
        }
        e1 = selecte1(e1,e1.ET);               // get rid of the OParrow
    }
    t = e1.ET;
    if (t)
    {
        if (!typtr(t.Tty))
            typerr(EM_pointer, t, cast(type *) null);       // pointer req'd before .
        else
            t = t.Tnext;
    }
    e1 = el_unat(OPind, t, e1);
    handleaccess(e1);
    if (CPP && tok.TKval == TK_istype)
    {   type *tx;
        type *typ_spec;

        stoken();
        chktok(TKlpar, EM_lpar);

        type_specifier(&typ_spec);
        tx = declar_abstract(typ_spec);
        fixdeclar(tx);

        e1 = cpp_istype(e1, tx);

        type_free(tx);
        type_free(typ_spec);
        chktok(TKrpar,EM_rpar);
    }
    else
        e1 = dodot(e1, e1.ET, false);
    return e1;
}


/****************************
 * Resolve (ei .* em).
 */

elem *dodotstar(elem *ei,elem *em)
{   type *tclass;
    type *tm;
    type *ti;
    int result;
    elem *ethis;

    /* em must be of type <pointer to member of class T>        */
    tm = em.ET;
    if (tybasic(tm.Tty) != TYmemptr)
    {   cpperr(EM_ptr_member);  // pointer to member expected to right of .* or .*
        goto err;
    }
    tclass = tm.Ttag.Stype;
    type_debug(tclass);

    /* ei must be of type <instance of T> or <instance of       */
    /* class publicly derived from T>.                          */
    ti = ei.ET;
    result = t1isbaseoft2(tclass,ti);
    if (result == 0)
    {
        typerr(EM_type_mismatch,ti,tclass);     // type mismatch
        goto err;
    }
    if ((result & BCFpmask) != BCFpublic)
    {
        cpperr(EM_public_base,&tclass.Ttag.Sident[0],&ti.Ttag.Sident[0]);   // must be a public base class
    }

    /* Convert em to a <pointer to member of class TI>  */
    tm = type_allocmemptr(ti.Ttag,tm.Tnext);

    ethis = exp2_addr(ei);
    if (tyfunc(tm.Tnext.Tty))
    {
        /* Pointer to function member. Call the function:       */
        /*      (*em)(&ei,args...)                              */

        list_t arglist;
        elem *e2;
        tym_t tym;

        em = exp2_castx(em,tm,&ethis,0);

        chktok(TKrpar,EM_rpar);
        chktok(TKlpar,EM_lpar);
        getarglist(&arglist);           /* read list of parameters      */

        /* em is actually a regular pointer to func. So let's make it one */
        el_settype(em,newpointer(tm.Tnext));

        em = el_unat(OPind,em.ET.Tnext,em);
        em = poptelem(em);              /* collapse out *& and &*       */

        tym = ethis.ET.Tnext.Tty & (mTYconst | mTYvolatile);
        /* if this modifier const or volatile need same modifier on func ?? */
        if (tym & ~em.ET.Tty)
            typerr(EM_cv_arg,ethis.ET,em.ET); /* type mismatch */

        ei = xfunccall(em,ethis,null,arglist);
        return ei;
    }
    else
    {
        /* Pointer to data member. Construct the expression:
         *      *(&ei + (em + 1))
         */

        type *at;

        em = _cast(em,tm);

        at = type_allocn(ethis.ET.Tty,tm.Tnext);     // retain pointer type of ethis
        el_settype(em,tstypes[TYint]);
        // Subtract 1 to compensate for 1 added by una_exp()
        em = el_bint(OPmin,tstypes[TYint],em,el_longt(tstypes[TYint],1));
        ei = el_bint(OPadd,at,ethis,em);
        ei = el_unat(OPind,ei.ET.Tnext,ei);
        handleaccess(ei);
        if (tyref(ei.ET.Tty))
            ei = reftostar(ei);
        return ei;
    }

err:
    el_free(em);
    return ei;
}


/********************************
 * Create:
 *               OPcall                         OPucall
 *               /    \                 or         |
 *              e     parameters                   e
 */

elem *dofunc(elem *e)
{   elem *ec;
    type *t;
    tym_t ty;
    list_t arglist;

    //printf("dofunc()\n");
    //elem_print(e);
    //type_print(e.ET);

    assert(e && e.ET);
    elem_debug(e);
    ty = e.ET.Tty;

    if (tybasic(ty) == TYmemptr && e.Eoper == OPrelconst && e.EV.ethis)
    {   elem *ethis = e.EV.ethis;
        e.EV.ethis = null;
        token_unget();
        tok.TKval = TKrpar;
        return dodotstar(el_unat(OPind, ethis.ET.Tnext, ethis), e);
    }

    // Provide implicit "this" pointer for ptr-to-member function calls
    if (tybasic(ty) == TYmemptr && funcsym_p && isclassmember(funcsym_p))
    {   Symbol *sthis;

        sthis = scope_search(cpp_name_this.ptr,SCTlocal);
        assert(sthis);
        token_unget();
        tok.TKval = TKrpar;
        return dodotstar(el_unat(OPind,sthis.Stype.Tnext,el_var(sthis)),e);
    }

    stoken();                           // skip over (
    getarglist(&arglist);               // read list of parameters into arglist
    if (tok.TKval != TKrpar)
        synerr(EM_rpar);

L1:
    if (typtr(ty))                      // gets a dereference for free
    {
        e = el_unat(OPind,e.ET.Tnext,e);
        if (CPP)
            e = poptelem(e);            // collapse out *& and &*
    }

  if (CPP)
  {
    // See if this is really an overloaded operator()
    if (type_struct(e.ET))
    {   type *tclass = e.ET;
        char *opident;
        Classsym *stag;
        Symbol *sfunc;
        Symbol *ambig;
        elem *e1;

        stag = tclass.Ttag;
        opident = cpp_opident(OPcall);
        sfunc = cpp_findmember(stag,opident,false);
        if (sfunc)
        {   elem *ethis;

            sfunc = cpp_overload(sfunc,tclass,arglist,stag,null,0);
            assert(tyfunc(sfunc.Stype.Tty));

            // If surrogate function call
            if (sfunc.Sfunc.Fflags & Fsurrogate)
            {   int m;

                sfunc = sfunc.Sfunc.Fsurrogatesym;

                // Convert e to pointer to function (by calling surrogate)
                m = cpp_cast(&e, sfunc.Stype.Tnext, 1);
                assert(m);
                ty = e.ET.Tty;
                goto L1;
            }

            ethis = el_calloc();
            el_copy(ethis,e);
            ethis = exp2_addr(ethis);
            c1isbaseofc2(&ethis,sfunc.Sscope,stag);

            /* Member functions could be virtual        */
            e1 = cpp_getfunc(tclass,sfunc,&ethis);
            e1 = el_unat(OPind,e1.ET.Tnext,e1);
            el_copy(e,e1);
            e1.ET = null;
            e1.EV.E1 = null;
            e1.EV.E2 = null;
            el_free(e1);

            e = poptelem(e);
            ec = xfunccall(e,ethis,null,arglist);
        }
        else
        {   err_nomatch("operator()",arglist); /* no match for function */
            list_free(&arglist,cast(list_free_fp)&el_free);
            return e;
        }
    }
    else
    {
        if (e.Eoper == OPvar && tyfunc(e.ET.Tty))
        {
            Symbol *sfunc;

            symbol_debug(e.EV.Vsym);

            // BUG: if e.EV.Vsym is a static member function, the sclass
            // argument should not be null. This bug can happen with:
            //   (a.f)();
            // where a is a class instance, and f is a static member function.
            sfunc = cpp_overload(e.EV.Vsym,null,arglist,null,e.EV.Vtal,(e.PEFflags & PEFtemplate_id) != 0);
            if (sfunc != e.EV.Vsym) /* if changed the Symbol */
            {
                el_settype(e,sfunc.Stype);
                e.EV.Vsym = sfunc;
            }
            if (sfunc.Stype.Tty & mTYimport)
            {
                if (config.exe & EX_windos)
                    Obj._import(e);              // do deferred import
            }

            // No longer needed
            param_free(&e.EV.Vtal);
            e.PEFflags &= ~PEFtemplate_id;
        }
        t = e.ET;
        if (!tyfunc(t.Tty))
        {   synerr(EM_function);                // function expected
            return e;
        }
        ec = xfunccall(e,null,null,arglist);
    }
  }
  else
  {
    t = e.ET;
    if (!tyfunc(t.Tty))
    {   synerr(EM_function);            // function expected
        return e;
    }
    ec = xfunccall(e,null,null,arglist);
  }

    ec = builtinFunc(ec);

ret:
    elem_debug(ec);
    return ec;
}

/*******************************************************
 * If function is a builtin, replace call with special operator.
 */

elem *builtinFunc(elem *ec)
{
    elem *e = ec.EV.E1;
    elem *e2;

    if (e.Eoper == OPvar &&
//#if linux || __APPLE__ || __FreeBSD__ || __OpenBSD__
        // In linux this is controlled by an option instead of adding defines to
        // change xxxxx to _inline_xxxx.
        //OPT_IS_SET(OPTfinline_functions)
//#else
        // Look for _inline_xxxx() functions and replace with appropriate opcode
        (CPP || !(e.ET.Tflags & TFgenerated)) // function must be prototyped
//#endif
     )
    {   Symbol *s;
        int i;
        enum TABLEN = 51;
        __gshared const char*[TABLEN] inlinefunc = /* names of inline functions  */
        [
                "bsf",
                "bsr",
                "bt",
                "btc",
                "btr",
                "bts",
                "cmpxchg",
                "cos",
                "cosf",
                "cosl",
                "fabs",
                "fabsf",
                "fabsl",
                "fmemcmp",
                "fmemcpy",
                "fmemset",
                "fstrcmp",
                "fstrcpy",
                "fstrlen",
                "inp",
                "inpl",
                "inpw",
                "ldexp",
                "ldexpf",
                "ldexpl",
                "memcmp",
                "memcpy",
                "memset",
                "outp",
                "outpl",
                "outpw",
                "rint",
                "rintf",
                "rintl",
                "rndtol",
                "rndtoll",
                "rndtos",
                "rol",
                "ror",
                "setjmp",
                "sin",
                "sinf",
                "sinl",
                "sqrt",
                "sqrtf",
                "sqrtl",
                "strcmp",
                "strcpy",
                "strlen",
                "yl2x",
                "yl2xp1",
        ];
        /* Parallel table of corresponding opcodes      */
        __gshared ubyte[TABLEN] opcode =
        [
                  OPbsf,OPbsr,OPbt,OPbtc,OPbtr,OPbts,
                  OPcmpxchg,
                  OPcos,OPcos,OPcos,
                  OPabs,OPabs,OPabs,OPmemcmp,OPmemcpy,OPmemset,
                  OPstrcmp,OPstrcpy,OPstrlen,
                  OPinp,OPinp,OPinp,
                  OPscale,OPscale,OPscale,
                  OPmemcmp,OPmemcpy,OPmemset,
                  OPoutp,OPoutp,OPoutp,
                  OPrint,OPrint,OPrint,OPrndtol,OPrndtol,OPrndtol,
                  OProl,OPror,
                  OPsetjmp,OPsin,OPsin,OPsin,OPsqrt,OPsqrt,OPsqrt,
                  OPstrcmp,OPstrcpy,OPstrlen,
                  OPyl2x,OPyl2xp1,
        ];
        /* Types of the operands. We check against these to make sure
           we are not inlining an overloaded function.
         */
        __gshared const tym_t[18] ty1 =
                [ TYptr,TYptr,TYptr,TYptr,TYptr,TYptr,
                  TYptr,
                  TYdouble,TYfloat,TYuint,TYuint,TYptr,TYptr,TYuint,
                  TYuint,TYdouble,TYfloat,TYptr ];
        __gshared const tym_t[18] ty2 =
                [ ~0,~0,~0,~0,~0,~0,
                  ~0,
                  ~0,~0,TYuint,TYuint,~0,~0,TYchar,TYuint,~0,~0,~0 ];

        s = e.EV.Vsym;
        symbol_debug(s);
        enum linux = 0; // linux || __APPLE__ || __FreeBSD__ || __OpenBSD__
        if ((linux || s.Sident[0] == '_' && memcmp(&s.Sident[0] + 1,"inline_".ptr,7) == 0)
            && ec.Eoper == OPcall      /* forget about OPucall for now */
           )
        {
            // If not C mangling, don't recognize it
            if (CPP && type_mangle(s.Stype) != mTYman_c)
                goto ret;
            i = binary(&s.Sident[0] + (linux ? 0 : 8),cast(const(char)**)&inlinefunc[0],inlinefunc.length);
            if (i != -1)
            {   int op = opcode[i];

                if (op == OPsetjmp && config.exe != EX_WIN32)
                    goto ret;
                if (OTunary(op))                /* if unary operator            */
                {
static if (0)
{
                    type *t = ec.EV.E2.ET;

                    if (op == OPstrlen)
                    {   if (!typtr(t.Tty) ||
                            !(t.Tty & mTYconst) ||
                            tybasic(t.Tnext.Tty) != TYchar
                           )
                        goto ret;
                    }
                    else if (tybasic(t.Tty) != ty1[i])
                        goto ret;
}
                    if (ec.Eoper == OPind)     // if stdcall with real return
                    {
                        e = ec.EV.E1;
                        ec.EV.E1 = e.EV.E2.EV.E1;
                        e.EV.E2.EV.E1 = null;
                    }
                    else
                    {
                        ec.EV.E1 = ec.EV.E2;
                        ec.EV.E2 = null;
                    }
                }
                else if (op == OPmemcpy)
                {   // Build (s1 memcpy (s2 param n))
                    // from  (memcpy call (n param (s2 param s1)))
                    if (ec.EV.E2.Eoper != OPparam)
                        goto ret;
                    e2 = ec.EV.E2.EV.E2;
                    if (e2.Eoper != OPparam)
                        goto ret;
                    ec.EV.E1 = e2.EV.E2;            // s1
                    ec.EV.E2.EV.E2 = ec.EV.E2.EV.E1;    // n
                    ec.EV.E2.EV.E1 = e2.EV.E1;        // s2
                    goto Le2;
                }
                else if (op == OPmemcmp)
                {   // Build ((s1 param s2) memcmp n)
                    // from  (memcmp call (n param (s2 param s1)))
                    if (ec.EV.E2.Eoper != OPparam)
                        goto ret;
                    e2 = ec.EV.E2.EV.E2;
                    if (e2.Eoper != OPparam)
                        goto ret;
                    ec.EV.E1 = ec.EV.E2;
                    ec.EV.E2 = ec.EV.E2.EV.E1;        // n
                    ec.EV.E1.EV.E1 = e2.EV.E2;        // s1
                    ec.EV.E1.EV.E2 = e2.EV.E1;        // s2
                    goto Le2;
                }
                else if (op == OPmemset)
                {   // Build (s memset (n param val))
                    // from  (memset call (n param (val param s)))
                    elem* en, ev;
                    if (ec.EV.E2.Eoper != OPparam)
                        goto ret;
                    e2 = ec.EV.E2.EV.E2;
                    if (e2.Eoper != OPparam)
                        goto ret;
                    ec.EV.E1 = e2.EV.E2;            // s
static if (0) // linux || __APPLE__ || __FreeBSD__ || __OpenBSD__
{
                    en = ec.EV.E2.EV.E1;            // n
                    ev = e2.EV.E1;
                    if ((en.Eoper == OPconst))
                    {
                        int cnt = en.EV.Vint;
                        elem *eo = ev;
                        while(OTconv(eo.Eoper))
                            eo = eo.EV.E1;
                        if (eo.Eoper == OPconst)
                        {
                            uint val;
                            while(OTconv(ev.Eoper))
                            {
                                eo = ev;
                                ev = ev.EV.E1;
                                eo.EV.E1 = null;
                                el_free(eo);
                            }
                            val = el_tolong(ev) & 0xff;
                            val = val | (val << 8) | (val << 16) | (val << 24);
                            ev.EV.Vuns = val;
                            ev.ET = tstypes[TYint];
                            ec.EV.E2.EV.E2 = ev;
                            goto Le2;
                        }
                    }
}
                    ec.EV.E2.EV.E2 = _cast(e2.EV.E1, tstypes[TYchar]);  // val
                    goto Le2;
                }
                else if (op == OPscale || op == OPyl2x || op == OPyl2xp1)
                {   // Build ((long double)exp scale x)
                    // from  (ldexp call (exp param x))
                    if (ec.EV.E2.Eoper != OPparam)
                        goto ret;
                    e2 = ec.EV.E2.EV.E2;            // x
                    ec.EV.E1 = ec.EV.E2.EV.E1;        // exp
                    ec.EV.E2.EV.E1 = null;
                    ec.EV.E2.EV.E2 = null;
                    el_free(ec.EV.E2);            // param
                    ec.EV.E2 = e2;
                    ec.EV.E1 = exp2_cast(ec.EV.E1, e2.ET);
                }
                else if (op == OPcmpxchg)
                {   // Build (e1 OPcmpxchg (old param new))
                    // from  (cmpxchg call (new param (old param e1)))
                    if (ec.EV.E2.Eoper != OPparam)
                        goto ret;
                    e2 = ec.EV.E2.EV.E2;
                    if (e2.Eoper != OPparam)
                        goto ret;
                    ec.EV.E1 = el_unat(OPind,e2.EV.E2.ET.Tnext,e2.EV.E2);            // e1
                    ec.EV.E2.EV.E2 = ec.EV.E2.EV.E1;    // new
                    ec.EV.E2.EV.E1 = e2.EV.E1;        // old
                    goto Le2;
                }
                else if (OTbinary(op))
                {       /* Convert the 2 parameters to the 2 operands           */
                    e2 = ec.EV.E2;
                    if (e2.Eoper != OPparam || e2.EV.E1.Eoper == OPparam ||
                        e2.EV.E2.Eoper == OPparam)
                        goto ret;
static if (0)
{
                    /* Make sure this is not an overloaded function             */
                    if (tybasic(e2.EV.E2.ET.Tty) != ty1[i])
                        goto ret;
                    if (tybasic(e2.EV.E1.ET.Tty) != ty2[i])
                        goto ret;
}
                    ec.EV.E1 = e2.EV.E2;
                    ec.EV.E2 = e2.EV.E1;
                Le2:
                    e2.EV.E1 = e2.EV.E2 = null;
                    el_free(e2);                // free the OPparam elem
                }
                else
                {
static if (0) /* compile in when we add inline functions with no parameters     */
{
                    assert(ec.EV.E2 == null);
                    ec.EV.E1 = null;
}
                }
                ec.Eoper = cast(ubyte)op;
                el_free(e);
            }
        }
        else if (NTEXCEPTIONS && s.Sident[0] == '_' && ec.Eoper == OPucall)
        {
            if (strcmp(&s.Sident[0] + 1,"exception_info") == 0)
            {
                if (!pstate.STinfilter)
                    tx86err(EM_needs_filter);           // only valid in filter-expression
                else
                {
                    el_free(ec);
                    ec = el_var(nteh_contextsym());
                    ec.EV.Voffset = (NTEXCEPTIONS == 1 ) ? 20 : 4;
                    el_settype(ec,tspvoid);
                }
            }
            else if (strcmp(&s.Sident[0] + 1,"exception_code") == 0)
            {
                if (!pstate.STinfilter && !pstate.STinexcept)
                    tx86err(EM_needs_handler);          // only valid in except_handler or filter-expression
                else
                {
                    pstate.STbfilter.Bflags |= BFLehcode;
                    el_free(ec);
                    ec = el_var(nteh_ecodesym());
                }
            }
        }
    }

ret:
    elem_debug(ec);
    return ec;
}


/**************************
 * Do the default promotions.
 */

/*private*/ elem * defaultpromotions(elem *e)
{       type *t;

        /* this should be combined with paramtypadj()   */
        switch (tybasic(e.ET.Tty))
        {
            case TYfloat:
                t = tstypes[TYdouble];
                break;
            default:
                return convertchk(e);
        }
        return _cast(e,t);
}

/************************
 * Do the integral promotions.
 * Convert <array of> to <pointer to>.
 */

elem *convertchk(elem *e)
{       tym_t tym;
        type *t;

        tym = e.ET.Tty;
        switch (tybasic(tym))
        {
            case TYvoid:
                synerr(EM_void_novalue);        // void has no value
                return e;
            case TYbool:
            case TYschar:
            case TYuchar:
            case TYchar:
            case TYshort:
                t = tstypes[TYint];
                break;
            case TYushort:
            case TYchar16:
            case TYwchar_t:             // BUG: 4 byte wchar_t's?
                if (_tysize[TYint] > SHORTSIZE)
                    t = tstypes[TYint];          /* value-preserving rules       */
                else
                    t = tstypes[TYuint];
                break;

            case TYsptr:
                if (!(config.wflags & WFssneds))
                {   t = type_allocn(TYnptr,e.ET.Tnext);
                    break;
                }
                goto default;
            default:
                return e;
            case TYnfunc:
            case TYnpfunc:
            case TYnsfunc:
            case TYfsfunc:
            case TYnsysfunc:
            case TYfsysfunc:
            case TYf16func:
            case TYifunc:
            case TYjfunc:
            case TYffunc:
            case TYfpfunc:      /* convert to pointer to func   */
            case TYarray:
                return arraytoptr(e);
        }
        return _cast(e,t);
}

/**************************
 * C11 6.3.2.1-3
 * If e is <array of>, convert it to <pointer to>.
 * C11 6.3.2.1-4
 * If e is <function>, convert it to <pointer to><function>.
 */

elem *arraytoptr(elem *e)
{   type *t;

    elem_debug(e);
    t = e.ET;
    if (t)
    {
        type_debug(t);
        switch (tybasic(t.Tty))
        {   case TYarray:
                if (e.Eoper == OPvar && type_isvla(t))
                {
                    // It already is a pointer, so just change the type
                    type_settype(&e.ET, newpointer(t.Tnext));
                }
                else if (CPP || !(e.PEFflags & PEFnotlvalue))  // ANSI C 3.2.2.1
                {   type *tp = type_ptr(e,t.Tnext);

                    tp.Talternate = t;
                    t.Tcount++;
                    e = el_unat(OPaddr,tp,e);
                }
                break;

            case TYnfunc:
            case TYnpfunc:
            case TYnsfunc:
            case TYnsysfunc:
            case TYfsysfunc:
            case TYf16func:
            case TYfsfunc:
            case TYifunc:
            case TYjfunc:
            case TYffunc:
            case TYfpfunc:
                e = exp2_addr(e);
                break;

            default:
                break;
        }
    }
    return e;
}

/*******************************
 * Convert a reference to a star.
 */

elem *reftostar(elem *e)
{
    return reftostart(e,e.ET);
}

elem *reftostart(elem *e,type *t)
{
    type_debug(t);
    if (tyref(t.Tty))                  /* if <reference to>    */
    {   /* convert (e) to (*e)  */

        type_debug(t.Tnext);
        el_settype(e,reftoptr(t));
        type_debug(e.ET.Tnext);
        e = el_unat(OPind,e.ET.Tnext,e);
    }
    return e;
}

/*****************************
 * Convert a pointer reference to a handle access.
 * Also handle _far16 pointer conversions.
 */

void handleaccess(elem *e)
{   /* Rewrite *e to *vptrfptr(e)       */
    type *t;
    tym_t ty;

    assert(e.Eoper == OPind);
    switch (tybasic(e.EV.E1.ET.Tty))
    {
        case TYvptr:
            ty = TYfptr;
        L1: t = newpointer(e.ET);
            t.Tty = (t.Tty & ~mTYbasic) | ty;
            e.EV.E1 = _cast(e.EV.E1,t);      /* cast handle to far pointer   */
            break;

        case TYf16ptr:
            ty = TYnptr;        /* convert far16 pointer to near pointer */
            goto L1;

        default:
            break;
    }
}


/**************************
 * Convert a fptr to an offset to that fptr.
 */

elem *lptrtooffset(elem *e)
{
    if (tyfv(e.ET.Tty))
        e = el_unat(OPoffset,tstypes[TYuint],e);
    return e;
}


/**********************
 * Take care of structure parameters.
 * Replace (e) with (OPstrpar e).
 */

/*private*/ elem * strarg(elem *e)
{
    //printf("strarg()\n");
    if (CPP)
    {
        /* Note: Need to improve code generation for case:
                struct DECIMAL
                {   DECIMAL();
                    DECIMAL(const DECIMAL &i);
                };

                extern DECIMAL _Dnull, _DLAST;
                int   IsBlah();
                void  Select(DECIMAL);

                void Test() { Select(IsBlah()? _Dnull : _DLAST); }
         */

        /* If there is a constructor for e, rewrite e to        */
        /*      ctor(&tmp,&e),tmp                               */
        type *tclass = e.ET;
        Classsym *sclass = tclass.Ttag;

        template_instantiate_forward(sclass);

        /* Set STRbitcopy (should probably do this elsewhere)   */
        if (!(sclass.Sstruct.Sflags & STRbitcopy))
        {
            //n2_createopeq(sclass,1);  // don't create unless we need it
            n2_createcopyctor(sclass,1);
        }
        if (sclass.Sstruct.Sflags & STRanyctor &&
            !(sclass.Sstruct.Sflags & STRbitcopy))
        {
            list_t arglist;
            elem *ector;

            // Remove possible OPinfo
            if (e.Eoper == OPind && e.EV.E1.Eoper == OPinfo)
            {   elem *ex = e.EV.E1;

                e.EV.E1 = ex.EV.E2;
                ex.EV.E2 = null;
                el_free(ex);
            }

            arglist = list_build(e,null);
            ector = init_constructor(null,tclass,arglist,0,3,null);
            assert(ector);
            return el_unat(OPstrctor,tclass,ector);
        }
    }
    if (e.Eoper != OPstrpar)
        e = el_unat(OPstrpar,e.ET,e);
    return e;
}


/*****************************
 * Generate call to a function.
 * Input:
 *      efunc           expression giving function to call
 *      ethis           'this' pointer
 *      pvirtbase       any virtual base stuff, free'd
 *      arglist         argument list, free'd
 * Returns:
 *      call expression tree
 */

elem *xfunccall(elem *efunc,elem *ethis,list_t pvirtbase,list_t arglist)
{   type *tfunc;
    const(char)* funcid;
    Symbol *sfunc;
    Symbol *stmp;
    list_t al;
    int reverse;
    elem *e;
    param_t *p;
    int retmethod;
    type *t;
    tym_t ty;

    tfunc = efunc.ET;
    if (!tyfunc(tfunc.Tty))
    {   list_free(&arglist,cast(list_free_fp)&el_free);
        return null;
    }

    // funcid is used for semi-lucid error messages
    if (efunc.Eoper == OPvar)
    {
        sfunc = efunc.EV.Vsym;
        funcid = prettyident(efunc.EV.Vsym);
        if (funcsym_p && SymInline(sfunc))
            funcsym_p.Sfunc.Fflags3 |= Fdoinline;     // check for inline expansions
    }
    else
    {
        sfunc = null;
        funcid = "function";
    }

static if (0)
{
    if (strcmp(funcid,"__builtin_next_arg") == 0)
        return lnx_builtin_next_arg(efunc,arglist);
}
    // Construct function prototype based on types of parameters
    if (!CPP && !(tfunc.Tflags & TFprototype)) // if no prototype
    {   // Generate a prototype based on the types of the parameters
        param_t **pp;

        tfunc.Tflags |= TFgenerated;
        pp = null;
        if (config.flags3 & CFG3autoproto && efunc.Eoper == OPvar)
        {   tfunc.Tflags |= TFprototype | TFfixed;
            pp = &tfunc.Tparamtypes;
        }
        for (al = arglist; al; al = list_next(al))
        {   type *tx;
            elem *ex = cast(elem *) list_ptr(al);

            ex = defaultpromotions(ex);   /* default promotions           */
            /* Adjust pointer types     */
            tx = ex.ET;
            tx.Tcount++;
            paramtypadj(&tx);
            ex = _cast(ex,tx);
            type_free(tx);

            al.ptr = cast(void *) ex;
            if (pp)
            {
//printf("tfunc\n%p: ",tfunc); type_print(tfunc);
//printf("e.ET\n%p: ",ex.ET); type_print(ex.ET);
                if (type_embed(ex.ET,tfunc))
                {   synerr(EM_recursive_proto);         // recursive prototype
                    goto L1;
                }
                param_append_type(pp,ex.ET);
            }
        }
    L1: ;
    }

    {   uint nparam;
        uint param;
        int fixed;

        nparam = tfunc.Tparamtypes.length();

        fixed = tfunc.Tflags & TFfixed;
        reverse = tyrevfunc(tfunc.Tty);
        p = tfunc.Tparamtypes;
        al = arglist;
        param = 0;
        while (al || p)
        {   elem *e1;

            param++;
            if (al == null)             /* if end of actual parameters  */
            {
                // See if there are default parameters
                if (CPP && p)
                {
                    if (p.PelemToken)
                    {   // C++98 14.7.1-11 default argument instantiation
                        assert(sfunc && sfunc.Sfunc.Fflags & Finstance);
                        Symbol *stemp = sfunc.Sfunc.Ftempl;

                        template_createsymtab(stemp.Sfunc.Farglist, sfunc.Sfunc.Fptal);
                        token_unget();
                        token_setlist(p.PelemToken);
                        stoken();
                        e1 = arraytoptr(assign_exp());
                        if (tok.TKval != TKcomma && tok.TKval != TKrpar)
                            token_poplist();    // try to recover from parse error
                        stoken();
                        cpp_alloctmps(e1);
                        scope_unwind(1);
                    }
                    else if (p.Pelem)
                    {
                        e1 = el_copytree(p.Pelem);
                        cpp_alloctmps(e1);
                    }
                    else
                        break;
                }
                else
                    break;
            }
            else
                e1 = cast(elem *) list_ptr(al);
            if (p)
            {
                /* Convert argument to type of parameter        */
                e1 = exp2_paramchk(e1,p.Ptype,param);
                p = p.Pnext;
            }
            else
            {   type *t1;

                e1 = defaultpromotions(e1);     /* promotions           */
                /* Adjust pointer types */
                t1 = e1.ET;
                t1.Tcount++;
                paramtypadj(&t1);
                e1 = _cast(e1,t1);
                type_free(t1);

                if (fixed)              /* too many parameters          */
                {
                    synerr(EM_num_args,nparam,funcid,list_nitems(arglist));
                    tfunc.Tflags &= ~TFfixed;
                    fixed = false;
                }
            }
            if (al)
            {   al.ptr = e1;
                al = list_next(al);
            }
            else if (CPP)
                list_append(&arglist,e1);
        }
        if (p)                          /* need more parameters         */
            synerr(EM_num_args,nparam,funcid,list_nitems(arglist));
    }

    if (pvirtbase)
    {
        if (tybasic(tfunc.Tty) == TYmfunc)
            list_cat(&arglist,pvirtbase);
        else
            arglist = list_cat(&pvirtbase,arglist);
    }

    /* Convert list of arguments into a tree    */
    e = null;
    for (al = arglist; al; al = list_next(al))
    {   elem *e1;

        e1 = list_elem(al);
        elem_debug(e1);
        if (type_struct(e1.ET))
            e1 = strarg(e1);
        e = (e) ? el_bint(OPparam,tstypes[TYint],reverse ? e : e1,reverse ? e1 : e) : e1;
    }
    list_free(&arglist,FPNULL);

    /* If pascal function type that is returning a struct, add a hidden */
    /* first argument that is a pointer to a temporary struct (for the  */
    /* return value).                                           */
    retmethod = exp2_retmethod(tfunc);
    if (retmethod == RET_STACK)
    {   elem *ehidden;

        stmp = symbol_genauto(tfunc.Tnext);
        ehidden = el_ptr(stmp);
        ehidden = _cast(ehidden,exp2_hiddentype(tfunc));
        if (e)
        {
            if (reverse && type_mangle(tfunc) == mTYman_cpp)
                e = el_bint(OPparam,tstypes[TYint],ehidden,e);
            else
                e = el_bint(OPparam,tstypes[TYint],e,ehidden);
        }
        else
            e = ehidden;
    }

    if (CPP && ethis)
    {   elem_debug(ethis);
        // Convert ethis into correct pointer type for this class
        ethis = typechk(ethis,newpointer(ethis.ET.Tnext));
        // Use tstypes[TYuint] instead of tstypes[TYint], see exp2_gethidden() for reason
        e = (e) ? el_bint(OPparam,tstypes[TYuint],e,ethis) : ethis;
    }

    if (e)
        e = el_bint(OPcall,tfunc.Tnext,efunc,e);
    else
        e = el_unat(OPucall,tfunc.Tnext,efunc);

    // Modify function return elem (e) based on types of result.

    elem_debug(e);
    t = e.ET;
    type_debug(t);
    ty = tybasic(t.Tty);

    /* Turn structure returns into pointers to structures                       */
    if (retmethod != RET_REGS)
    {
        /* What's actually returned is a pointer to the value,  */
        /* so we need to tack a * onto the front.               */
        el_settype(e,newpointer(t));
        if (CPP &&
            retmethod == RET_STACK && ty == TYstruct &&
            !eecontext.EEin &&
            t.Ttag.Sstruct.Sdtor && config.flags3 & CFG3eh)
            e = el_ctor(el_ptr(stmp),e,t.Ttag.Sstruct.Sdtor);
        if (retmethod & (RET_STACK | RET_PSTACK))
        {   tym_t tym = e.ET.Tty;

            if (tym == TYsptr || (tym == TYnptr && !(config.wflags & WFssneds)))
                e = _cast(e,newpointer(t));
            else
                e = typechk(e,newpointer(t));
        }
        e = el_unat(OPind,t,e);
  }

  // For functions returning references, put a * in front
  else if (tyref(t.Tty))
  {     // Convert e from <ref to> to <ptr to>
        e = reftostar(e);
  }
  if (!CPP)
      e.PEFflags |= PEFnotlvalue;              // function returns are never lvalues
  return e;
}

////////////////////////////////////////
// Find OPstrthis in parameter list

void exp2_setstrthis(elem *e,Symbol *s,targ_size_t offset,type *t)
{
    assert(CPP);
    while (1)
    {
        switch (e.Eoper)
        {
            case OPstrthis:
                if (s)
                {
                    e.Eoper = OPrelconst;
                    e.EV.Vsym = s;
                    el_settype(e,t);
                }
                e.EV.Voffset = offset;
                break;

            case OPcond:
                e = e.EV.E2;
                goto case OPparam;

            case OPparam:
            case OPcomma:
            case OPinfo:
                exp2_setstrthis(e.EV.E1,s,offset,t);
                goto case OPcall;

            case OPcall:
            case OPeq:
                e = e.EV.E2;
                continue;

            case OPctor:
                e = e.EV.E1;
                continue;

            default:
                break;
        }
        break;
    }
}

/*************************************
 * Given a function call expression tree that takes an argument
 * of a hidden pointer to the return value, return that argument.
 */

elem *exp2_gethidden(elem *e)
{
    type *tf = e.EV.E1.ET;

    debug assert(e.Eoper == OPcall);
    type_debug(tf);
    debug assert(exp2_retmethod(tf) == RET_STACK);

    e = e.EV.E2;
    if (type_mangle(tf) == mTYman_cpp && tyrevfunc(tf.Tty))
    {   // Hidden is first parameter pushed
        while (e.Eoper == OPparam)
            e = e.EV.E1;
    }
    else
    {   // Be careful not to grab the 'this' parameter.
        // We do a nasty trick of detecting the 'this'
        // parameter by checking the type of OPparam.
        if (e.Eoper == OPparam && e.ET.Tty == TYuint)
            e = e.EV.E1;  // skip 'this'
        // Hidden parameter is now last parameter pushed
        while (e.Eoper == OPparam)
            e = e.EV.E2;
    }
    return e;
}


/******************************
 * Check to see that operands of an operator are arithmetic.
 */

void chkarithmetic(elem *e)
{   type *t2;

    elem_debug(e);
    t2 = null;
    if (!tyarithmetic(e.EV.E1.ET.Tty) ||
        OTbinary(e.Eoper) && !tyarithmetic((t2 = e.EV.E2.ET).Tty))
            typerr(EM_illegal_op_types,e.EV.E1.ET,t2); // illegal operand types
}

/******************************
 * Check to see that operands of an operator are of integral type.
 */

void chkintegral(elem *e)
{   type *t2;

    elem_debug(e);
    t2 = null;
    if (!tyintegral(e.EV.E1.ET.Tty) ||
        OTbinary(e.Eoper) && !tyintegral((t2 = e.EV.E2.ET).Tty))
    {
            typerr(EM_illegal_op_types,e.EV.E1.ET,t2); // illegal operand types
    }
}


/******************************
 * Do type checking. That is, convert e to type t. Detect pointer
 * mismatches.
 */

elem *typechk(elem *e,type *t)
{
    return exp2_paramchk(e,t,0);
}

/*******************************************
 * Check type of function parameter.
 * Input:
 *      param   function parameter number (0 means not a parameter)
 */

/*private*/ elem * exp2_paramchk(elem *e,type *t,int param)
{ type *et;
  tym_t ety,ty;

  assert(e);
  elem_debug(e);
  type_debug(t);
  et = e.ET;
  type_debug(et);
  ety = tybasic(et.Tty);
  if (et == t && ety != TYmemptr)
        return e;                       // short cut if no cast necessary

static if (0)
{
  static int nest;
  if (++nest == 5)
  {
        assert(0);
  }
}
static if (0)
{
  dbg_printf("exp2_paramchk(e,t,%d)\n",param);
  printf("parentheses = x%x\n", e.PEFflags & PEFparentheses);
  elem_print(e);
  type_print(et);
  type_print(t);
}
  ty = tybasic(t.Tty);

if (config.exe & EX_posix)
{
  if ((ty == TYstruct) && (t.Ttag.Sstruct.Sflags & STRunion) &&
          t.Tty & mTYtransu)
  {
      //dbg_printf("found transparent union formal parameter\n");
      t = list_symbol(t.Ttag.Sstruct.Sfldlst).Stype;
      ty = tybasic(t.Tty);
  }
}

  // If t is a reference type, and e is what t refers to, add a
  // reference operator in front of e.
  if (tyref(ty))
  {     elem *e1;
        type *tn = t.Tnext;
        tym_t tnty = tn.Tty;
        int result;
        int flags;

        // Detect invalid reference initializations. See ARM 8.4.3
        if (tnty & mTYvolatile && ety & mTYconst ||
            tnty & mTYconst && ety & mTYvolatile ||
            !(tnty & (mTYconst | mTYvolatile)) && ety & (mTYconst | mTYvolatile)
           )
            cpperr(EM_bad_ref_init);    // invalid reference initialization
        flags = 0x10;
        if (et.Tty & mTYfar &&
            ((ty == TYref && LARGEDATA) || ty == TYfref))
            flags |= 0x20;
        if (typematch(tn,et,flags))
        {
        }
        else if (tybasic(tnty) == TYarray)
        {
            if (typtr(tybasic(et.Tty)) && typematch(tn.Tnext,et.Tnext,0))
            {   // Initializing a reference to an array with a pointer to the array contents
                e = exp2_paramchk(e,newpointer(tn.Tnext),param);       // to default pointer type
                return e;
            }
        }
        else if (tyfunc(tnty) && typtr(tybasic(et.Tty)) &&
                 tyfunc(et.Tnext.Tty)
                 /*typematch(tn,et.Tnext,0)*/)
        {
           // Initializing a reference to a function with a pointer to a function
           goto REF1;
        }
        // Allow implicit conversion of derived class to ref to base class
        // (Otherwise typechk() will try to call a constructor to do the
        // the conversion with base(base&). This will cause infinite
        // recursion.)
        else if (t1isbaseoft2(tn,et))
        {

            //if (result & BCFprivate && !cpp_funcisfriend(funcsym_p,et.Ttag))
            //  cpperr(EM_cvt_private_base,&et.Ttag.Sident[0],&tn.Ttag.Sident[0]); // cannot convert to private base
        }
        else
        {
            // See if ety has a defined reference conversion to t
            if (cpp_cast(&e,t,4|1))
            {
                return exp2_addr(e);
            }

            if (!(tnty & mTYconst))
            {
                if (1 || config.ansi_c)
                    typerr(EM_bad_ref,et,t);    // reference must refer to same type or be const
                else
                {   warerr(WM.WM_init2tmp);        // reference inited to temporary
                    e = exp2_copytotemp(e);
                }
            }
            e = exp2_paramchk(e,tn,param);
        }

        e1 = e = poptelem(e);   /* try to fold out any redundant * or & */
        et = e1.ET;
        while (1)
        {   switch (e1.Eoper)
            {   case OPstrpar:
                    e = selecte1(e,et);
                    break;
                case OPcomma:
                    e1 = e1.EV.E2;
                    continue;
                case OPind:
                    break;
                case OPcond:
                    /* Cast each leaf to the reference type     */
                    e.EV.E2.EV.E1 = exp2_paramchk(e.EV.E2.EV.E1,t,param);
                    e.EV.E2.EV.E2 = exp2_paramchk(e.EV.E2.EV.E2,t,param);
                    t = e.EV.E2.EV.E2.ET;
                    el_settype(e.EV.E2,t);
                    el_settype(e,t);
                    return e;
                case OPrelconst:
                    if (tybasic(tn.Tty) == TYarray &&
                        tybasic(e.EV.Vsym.Stype.Tty) == TYarray)
                    {
                        /* If initializing ref to an array with the     */
                        /* name of an array                             */
                        if (!typematch(e.EV.Vsym.Stype,tn,false))
                            goto mismatch;
                        el_settype(e,type_ptr(e,e.EV.Vsym.Stype));
                        goto REF1;
                    }
                    goto default;

                default:
                {
                    /* ARM says that (T&)X is equivalent to *(T*)&X   */
                    /* Rewrite e to be ((tmp=e),tmp)    */
                    Symbol *s;

                    e = poptelem(e);
                    if (init_staticctor /*|| e.Eoper == OPconst || e.Eoper == OPrelconst*/)
                    {
                        s = symbol_generate(SCstatic,et);
                        symbol_keep(s);
                        init_sym(s,e);
                        if (!funcsym_p)
                            outdata(s);
                        e = el_var(s);
                    }
                    else
                        e = exp2_copytotemp(e);

                    if (!(tnty & mTYconst))
                    {
                        if (1 || config.ansi_c)
                            typerr(EM_bad_ref,et,t);    // reference must refer to same type or be const
                        else
                            warerr(WM.WM_init2tmp);     // reference inited to temporary
                    }
                    break;
                }
                case OPvar:
                {   Symbol *s = e1.EV.Vsym;

                    symbol_debug(s);
                    switch (s.Sclass)
                    {
                        // In C++, the address of a variable declared as register
                        // is allowed.  Register is only a hint to the compiler
                        case SCregister:
                        case SCregpar:
                            if (CPP)
                                break;
                            goto case SCpseudo;

                        case SCpseudo:
                            synerr(EM_noaddress);       // can't take address of register
                            break;

                        default:
                            break;
                    }
                    if (pointertype == TYnptr && s.Stype.Tty & mTYfar)
                        cpperr(EM_near2far,&s.Sident[0]);  // s is far
                    break;
                }
                case OPstreq:
                    break;
            }
            break;
        }
        e = exp2_addr(e);
    REF1:
        e = exp2_paramchk(e,newpointer(tn),param);      // to default pointer type
        return e;
  }
  if (tymptr(ty))                       /* if converting to a pointer   */
  {
        /* 0 can be implicitly converted to any pointer type            */
        if ((tyintegral(ety) && ety != TYenum) || (type_relax && typtr(ety)))
        {   elem *e2;
            elem *er;

            e = poptelem(e);            /* catch things like ((long) 0) */
            er = e;
            while (1)
            {
                switch (er.Eoper)
                {   case OPcomma:
                        er = er.EV.E2;
                        continue;
                    case OPconst:
                        if (!boolres(er))
                            goto doit;          // null can be cast to a pointer
                        break;
                    case OPcond:
                        // Allow (e1 ? null : null)
                        e2 = er.EV.E2;
                        if (e2.EV.E1.Eoper == OPconst && !boolres(e2.EV.E1) &&
                            e2.EV.E2.Eoper == OPconst && !boolres(e2.EV.E2))
                            goto doit;
                        break;

                    default:
                        break;
                }
                break;
            }

            /* If the size fits, wear it        */
            if (type_relax && tyintegral(ety) && _tysize[ty] == _tysize[ety])
                goto doit;
        }

        // nullptr can be implicitly converted to any pointer type
        if (ety == TYnullptr)
            goto doit;

        if (tymptr(ety))
        {   Symbol *s;

            /* Be careful to disallow the conversion of a pointer to a  */
            /* const to a regular pointer. The other way is OK.         */
            if (et.Tnext.Tty & mTYconst && !(t.Tnext.Tty & mTYconst)
                && (!CPP || !tyfunc(t.Tnext.Tty))
                && !type_relax
               )
            {
                if (CPP && e.Eoper == OPstring)
                    /* C++ 4.2-2 Allow implicit conversions of string literals
                     * from const char* to char*
                     */
                { }
                else
                    goto mismatch;
            }

          if (CPP)
          { type *ft;
            ubyte parens = e.PEFflags & PEFparentheses;

            /* Check for overloaded function pointer, and fix it        */
            e = poptelem(e);
            et = e.ET;
            ety = tybasic(et.Tty);
            if (e.Eoper == OPrelconst &&
                tyfunc(t.Tnext.Tty) &&
                ((tyfunc((ft = et.Tnext).Tty) &&
                  tyfunc((s = e.EV.Vsym).Stype.Tty))
                    ||
                 (tymptr(et.Tnext.Tty) &&
                  tyfunc((ft = et.Tnext.Tnext).Tty) &&
                  (s = e.EV.Vsym.Simport) != null)
                ) &&
                 // If function is a member, then the offset is really
                 // the d offset.
                 (isclassmember(s) || e.EV.Voffset == 0)
               )
            {
                int notcast;

                /* Assume not overloaded if function pointer has been
                   cast to another type:
                        fp = (cast) func;
                 */
                notcast = typematch(ft,s.Stype,0);
                if (notcast)
                {
                    /* Search for function that matches t.
                     * BUG: the case where there is a static member function
                     * overloaded with a regular member function is not
                     * handled.
                     */
                    Funcsym *so;

                    so = cpp_findfunc(t.Tnext,null,s,2);
                    if (so == s)                // if same function selected
                        notcast = 0;            // don't change type of e
                    s = so;
                }
                if (s)
                {   type *et2;
                    targ_size_t d;

                    // BUG: what about Spath adjusting 'this' pointer?
                    while (s.Sclass == SCfuncalias)
                        s = s.Sfunc.Falias;

                    d = e.EV.Voffset;       // offset to start of class

                    /* If s is a virtual function       */
                    if (s.Sfunc.Fflags & Fvirtual)
                    {   int i;
                        targ_size_t d2;
                        Classsym *sclass = cast(Classsym *)s.Sscope;

                        /* Get offset, i, from start of vtbl[]  */
                        i = cpp_vtbloffset(sclass,s);

                        /* Get offset, d2, of vptr from this    */
                        d2 = sclass.Sstruct.Svptr.Smemoff;

                        /* Replace function with a thunk        */
                        s = nwc_genthunk(s,d,i,d2);
                        //symbol_keep(s);
                        d = 0;
                    }
                    else if (isclassmember(s))
                    {
                        if (parens && !(s.Sfunc.Fflags & Fstatic))
                        {
                            cpperr(EM_ptmsyntax, &s.Sident[0]);
                        }

                        if (d && !(s.Sfunc.Fflags & Fstatic))
                        {   s = nwc_genthunk(s,d,-1,0);
                            //symbol_keep(s);
                        }
                        d = 0;
                    }
                    e.EV.Vsym = s;
                    e.EV.Voffset = d;
                    if (notcast)
                    {
                        if ((!s.Sfunc.Fthunk || s.Sfunc.Fflags & Finstance) &&
                            (s.Sfunc.Fflags & Fstatic || !isclassmember(s)))
                        {
                            et2 = newpointer(s.Stype);
                        }
                        else
                        {
                            et2 = type_allocn(TYmemptr,s.Stype);
                            if (ety == TYmemptr)
                                et2.Ttag = et.Ttag;
                            else if (tybasic(et.Tnext.Tty) == TYmemptr)
                                et2.Ttag = et.Tnext.Ttag;
                            else
                                et2.Ttag = cast(Classsym *)s.Sscope;
                            ety = TYmemptr;
                        }
                        el_settype(e,et2);
                        et = et2;
                    }
                    if (s.Stype.Tty & mTYimport)
                    {
                        if (config.exe & EX_windos)
                            Obj._import(e);              // do deferred import
                    }
                }
            }

            // Check if we really can implicitly convert a far pointer
            // to a near or ss pointer.
            // BUG: this doesn't handle all the cases that exp2_ptrconv()
            // does, but it should.
            else if (ety == TYfptr && (ty == TYsptr || ty == TYnptr) &&
                e.Eoper == OPrelconst &&
                !(et.Tty & (mTYfar | mTYcs)) &&        // not in another segment
                typematch(et.Tnext,t.Tnext,3)
               )
            {
                switch (e.EV.Vsym.Sclass)
                {   case SCauto:
                    case SCparameter:
                    case SCregister:
                    case SCregpar:
                    case SCfastpar:
                    case SCbprel:
                        if (ty == TYsptr)
                            goto doit;
                        if (!(config.wflags & WFssneds)) // if SS == DS
                            goto doit;
                        break;
                    case SCglobal:
                    case SCstatic:
                    case SCextern:
                    case SCcomdef:
                        if (ty == TYnptr)
                            goto doit;
                        if (!(config.wflags & WFssneds)) // if SS == DS
                            goto doit;
                        break;

                    default:
                        break;
                }
            }
          }
        }
        if (typecompat(et,t))           /* if types are compatible      */
        {
            if (tymptr(ety))
            {
                // Do access check; cannot cast to private base class
                if (t1isbaseoft2(t.Tnext, et.Tnext) & BCFprivate)
                {
                    if (!cpp_classisfriend(et.Tnext.Ttag, t.Tnext.Ttag))
                        cpperr(EM_not_accessible, &t.Tnext.Ttag.Sident[0], &et.Tnext.Ttag.Sident[0]);
                }
            }
            goto doit;
        }
        if (exp2_ptrconv(et,t))
            goto doit;

        // Allow conversion of strings to CS-relative pointers
        if (ty == TYcptr && ety == TYnptr && e.Eoper == OPstring)
            goto doit;

        if (CPP && cpp_cast(&e,t,1))    // look for user-defined conversion
                return e;
  }
  else
  {
        // error if converting from an integer to an enum
        if (CPP && ty == TYenum && (ety != TYenum || et.Ttag != t.Ttag))
            goto mismatch;

        if (ety == TYnullptr && ty == TYnullptr)
            goto doit;

        /* error if converting from a pointer to a non-pointer  */
        if (!(tymptr(ety) || ety == TYnullptr)
            || ty == TYstruct
            || (tyref(ty) && type_struct(t.Tnext))
           )
                goto doit;

        /* Allow (void*) => integral    */
        if (type_relax && tymptr(ety) && tyintegral(ty) &&
            _tysize[ty] <= _tysize[ety])
        {
            e = poptelem(e);
            if (e.Eoper == OPconst && !boolres(e))
                goto doit;
        }

        // Allow pointers or enums => bool's
        if (CPP && ty == TYbool && (tymptr(ety) || ety == TYenum))
            goto doit;
  }
mismatch:
  //printf("et:\n");type_print(et);
  //printf("t:\n");type_print(t);
  typerr(param ? EM_explicitcast : EM_explicit_cast,et,t,param);        // cannot implicitly convert

doit:
  if (ety == TYstruct || ty == TYstruct)
  {
    if (CPP)
    {
        // Allow conversions to const or volatile
        if (et.Tty == ty && t.Ttag == et.Ttag)
        {
            return e;
        }

        // Allow conversions to base class
        if (e.Eoper == OPind && ety == ty &&
            (et.Tty & ~t.Tty & (mTYconst | mTYvolatile)) == 0)
        {
            if (t1isbaseoft2(t,et))
            {
                e = el_unat(OPaddr, newpointer(et), e);
                c1isbaseofc2(&e, t.Ttag, et.Ttag);
                e = el_unat(OPind, t, e);
                return e;
            }
        }

        if (!typematch(et,t,false))
        {
            if (ty == TYbool)
            {
                e = cpp_bool(e, 1);
                ety = tybasic(e.ET.Tty);
                if (ety != TYstruct)            // if found a boolean conversion
                    return typechk(e, t);
            }
            if (!cpp_cast(&e,t,8|1)) // look for user-defined conversion
            {
                if (!typecompat(et,t))
                {   typerr(param ? EM_explicitcast : EM_explicit_cast,et,t,param);      // can't implicitly convert
                    el_settype(e,t);
                }
            }
        }
    }
    else
    {
        if (!typecompat(et,t))
        {
            typerr(param ? EM_explicitcast : EM_explicit_cast,et,t,param);      // can't implicitly convert
            el_settype(e,t);            /* error recovery               */
        }
    }
    return e;
  }

    /* Carry along const and volatile modifiers */
    ety = e.ET.Tty & (mTYconst | mTYvolatile | mTYLINK);
    e = _cast(e,t);
    type_setty(&e.ET,e.ET.Tty | ety);
    return e;
}

/***************************
 * Return !=0 if types are compatible.
 * Regard pointer and array types as equivalent.
 * Regard far pointer and handle types as equivalent.
 * Watch out for arrays, one with unknown size and the other with a known size
 */

int typecompat(type *t1,type *t2)
{   tym_t t1ty,t2ty;

    //printf("typecompat() type_relax = %d\n", type_relax);
    //type_print(t1);
    //type_print(t2);

    int i =

    t1 == t2 ||

    t1 && t2
        &&
    ((t1ty = tybasic(t1.Tty)) == (t2ty = tybasic(t2.Tty))
                ||
        /* Arrays and pointer types are compatible      */
        (t1ty == TYarray || t2ty == TYarray) &&
        (t1ty == pointertype || t2ty == pointertype)
                ||
        (t1ty == TYfptr && t2ty == TYvptr)
                ||
        (t1ty == TYvptr && t2ty == TYfptr)
                ||
        /* see if we can relax the type checking (-Jm switch) */
        (type_relax && (t1ty & ~mTYbasic) == (t2ty & ~mTYbasic) &&
                 tyrelax(t1ty) == tyrelax(t2ty))
    )
        &&
    /* Array dimensions must match or be unknown        */
    (t1ty != TYarray || t2ty != TYarray || t1.Tdim == t2.Tdim ||
     ((t1.Tflags | t2.Tflags) & (TFsizeunknown | TFvla))
    )
        &&
    /* If structs, then the members must match  */
    (t1ty != TYstruct && (!CPP || t1ty != TYenum) || t1.Ttag == t2.Ttag
        || (CPP && t1isbaseoft2(t2,t1))
    )
        &&
    /* If subsequent types, they must either match or both are ptrs,    */
    /* one of which is a (void *), or we can convert from derived class */
    /* pointer to base class pointer                                    */
    ( (type_semirelax ? typecompat(t1.Tnext,t2.Tnext)
                  : typematch(t1.Tnext,t2.Tnext,type_relax ? 0x80+8+4+2+1 : 0x80+4+2+1)
      ) ||
      typtr(t1ty) && typtr(t2ty) &&
      (config.flags4 & CFG4implicitfromvoid && tybasic(t1.Tnext.Tty) == TYvoid
                ||
       tybasic(t2.Tnext.Tty) == TYvoid
                ||
        (CPP && t1isbaseoft2(t2.Tnext,t1.Tnext))
                ||
//      (CPP && t1isSameOrSubsett2(t1.Tnext, t2.Tnext))
//              ||
        type_relax
      )
    )
        &&
    /* If function, and both prototypes exist, then prototypes must match
       (for -Jm switch, we ignore prototype mismatches)
     */
    (!tyfunc(t1ty) ||
     !(t1.Tflags & TFprototype) ||
     !(t2.Tflags & TFprototype) ||
     ((t1.Tflags & (TFprototype | TFfixed)) ==
         (t2.Tflags & (TFprototype | TFfixed))) &&
        paramlstcompat(t1.Tparamtypes,t2.Tparamtypes)
         ||
     type_relax
    )
                &&
    // For template types, the template symbols must match
    (tybasic(t1ty) != TYtemplate ||
        (cast(typetemp_t *)t1).Tsym == (cast(typetemp_t *)t2).Tsym
    )
    ;
    //printf("typecompat returns %d\n", i);
    return i;
}


/****************************
 * Return true if type lists are compatible.
 */

/*private*/ int paramlstcompat(param_t *p1,param_t *p2)
{
        return p1 == p2 ||
                p1 && p2 && typecompat(p1.Ptype,p2.Ptype) &&
                paramlstcompat(p1.Pnext,p2.Pnext);
}

/******************************************
 * Return !=0 if exception specification of t1 is same or a subset of
 * exception specification of t2.
 */

int t1isSameOrSubsett2(type *tder, type *tbase)
{
    //printf("t1isSameOrSubsett2()\n");
    //type_print(tder);
    //type_print(tbase);
    if (tbase.Tflags & TFemptyexc)
    {
        if (!(tder.Tflags & TFemptyexc))
        {
            //printf("\treturn 0\n");
            return 0;
        }
    }
    else if (tbase.Texcspec)
    {
        if (tder.Tflags & TFemptyexc)
        {
        }
        else if (!tder.Texcspec)
            return 0;
        else
        {
            // All in sfder should be in sfbase
            for (list_t lder = tder.Texcspec; lder; lder = list_next(lder))
            {
                type *tderx = list_type(lder);

                for (list_t lbase = tbase.Texcspec; 1;
                     lbase = list_next(lbase))
                {
                    if (!lbase)
                        return 0;
                    type *tbasex = list_type(lbase);
                    if (typematch(tderx, tbasex, 0))
                        break;
                    if (!type_covariant(tderx, tbasex))
                        break;
                }
            }
        }
    }

    return 1;
}

/***********************************************
 * Determine if exception specifications match.
 * Return !=0 if it does.
 */

int type_exception_spec_match(type *t1, type *t2)
{   list_t list1 = t1.Texcspec;
    list_t list2 = t2.Texcspec;

    //printf("type_exception_spec_match()\n");
    //type_print(t1);
    //type_print(t2);

    if ((t1.Tflags & TFemptyexc) != (t2.Tflags & TFemptyexc))
        return 0;

    if (!list1 && !list2)
        return 1;

    // Each type in list1 must also be in list2
    for (list_t lx = list1; lx; lx = list_next(lx))
    {   type *tx = cast(type *)list_ptr(lx);

        for (list_t ly = list2; 1; ly = list_next(ly))
        {
            if (!ly)
                return 0;
            type *ty = cast(type *)list_ptr(ly);
            if (typematch(tx, ty, 0))
                break;
        }
    }

    // Each type in list2 must also be in list1
    for (list_t ly = list2; ly; ly = list_next(ly))
    {   type *ty = cast(type *)list_ptr(ly);

        for (list_t lx = list1; 1; lx = list_next(lx))
        {
            if (!lx)
                return 0;
            type *tx = cast(type *)list_ptr(lx);
            if (typematch(ty, tx, 0))
                break;
        }
    }

    //printf("\tmatch\n");
    return 1;
}

/**************************************
 * Modify ethis to point to virtual base sbase from class stag.
 */

elem *exp2_ptrvbaseclass(elem *ethis,Classsym *stag,Classsym *sbase)
{
    /* Construct:
            ethis = &(ethis.vbptr) + ethis.vbptr[offset] + baseoff
     */
    targ_size_t vbptroffset;
    targ_size_t vbtbloff;
    elem *ebaseoff;
    elem *es;
    elem *e;
    type *tp;
    baseclass_t *bs;
    type *tpbase;

    //dbg_printf("exp2_ptrvbaseclass(e=%p,'%s','%s')\n",e,stag.Sident,sbase.Sident);
    tpbase = type_allocn(ethis.ET.Tty,sbase.Stype);
    assert(typtr(tpbase.Tty));
    tp = type_allocn(tpbase.Tty,tpbase);       // base **

    symbol_debug(stag.Sstruct.Svbptr);
    vbptroffset = stag.Sstruct.Svbptr_off;
    ethis = el_bint(OPadd,tp,ethis,el_longt(tstypes[TYint],vbptroffset));
    if (el_sideeffect(ethis))
    {   es = exp2_copytotemp(ethis);
        ethis = el_copytree(es.EV.E2);
    }
    else
        es = null;

    e = el_unat(OPind,tp,el_copytree(ethis));

    ebaseoff = el_longt(tpbase,0);
    bs = baseclass_find(stag.Sstruct.Svirtbase,sbase);
    if (!bs)
    {   // sbase could be a base class of a virtual base of stag
        for (bs = stag.Sstruct.Svirtbase; bs; bs = bs.BCnext)
        {   int result;

            result = c1isbaseofc2(&ebaseoff,sbase,bs.BCbase);
            if (result)
            {   if (result & BCFvirtual)
                {   el_free(ebaseoff);
                    ebaseoff = el_longt(tpbase,0);
                }
                else
                    break;
            }
        }
    }

    // BUG: default parameters to functions are in class scope, but
    // Svirtbase is not constructed yet
    vbtbloff = bs ? bs.BCvbtbloff : _tysize[TYint];
    e = el_bint(OPadd,tp,e,el_longt(tstypes[TYint],vbtbloff));
    e = el_unat(OPind,tstypes[TYint],e);
    ethis = el_bint(OPadd,tpbase,ethis,e);
    ethis = el_bint(OPadd,tpbase,ethis,_cast(ebaseoff,tstypes[TYint]));

    ethis = el_combine(es,ethis);
    return ethis;
}

/**************************
 * Return !=0 if t2 is derived from t1.
 * The value is the number of derivation levels * 0x100 + some other bits.
 */

int t1isbaseoft2(type *t1,type *t2)
{   int result = 0;

    /*dbg_printf("t1isbaseoft2(x%lx,x%lx)\n",t1,t2);*/
    if (t1 && type_struct(t1) && t2 && type_struct(t2))
    {
        result = c1isbaseofc2(null,t1.Ttag,t2.Ttag);
    }
    return result;
}

/********************************
 * Determine if c2 is same as c1 or c1 is a base class of c2.
 * Input:
 *      *pethis         expression resulting in pointer to start of c2
 *                      (pethis can be null)
 * Output:
 *      *pethis         expression resulting in pointer to start of c1
 *                      (not affected if c1 is not a base of c2)
 * Returns:
 *      0               c1 is not a base of c2
 *      1               c1 is a base of c2
 *      BCFvirtual      (OR'd in) c1 is a virtual base class of c2
 *      BCFprivate      (OR'd in) c1 is a private base class of c2
 *      levels * 0x100  number of inheritance levels down c1 is from c2
 */

int c1isbaseofc2(elem **pethis,Symbol *c1,Symbol *c2)
{   Classsym *svirtual;

    //printf("c1isbaseofc2(c1 = '%s', c2 = '%s')\n", c1.Sident, c2.Sident);
    template_instantiate_forward(cast(Classsym *)c2);
    int result;
    elem **pe;

    if (c1 == c2)
        return 1;
    pe = null;
    result = c1isbaseofc2x(pe,c1,c2,&svirtual);
    if (result && pethis)
    {
        if (result & BCFvirtual)
            // We can do it in one step
            *pethis = exp2_ptrvbaseclass(*pethis,cast(Classsym *)c2,cast(Classsym *)c1);
        else
        {   debug assert(!pethis || *pethis);
            c1isbaseofc2x(pethis,c1,c2,&svirtual);
        }
    }
    return result;
}

/*private*/ int c1isbaseofc2x(elem **pethis,Symbol *c1,Symbol *c2,Classsym **psvirtual)
{
    baseclass_t *b;
    int value;
    int prevvalue;
    Classsym *svirtual;
    elem* ethis;
    elem** pe;

    symbol_debug(c1);
    symbol_debug(c2);
    *psvirtual = null;
    if (c1 == c2)
        return 1;
    prevvalue = 0;
    if (tybasic(c2.Stype.Tty) == TYident)
    {
        if (tybasic(c1.Stype.Tty) == TYident &&
            strcmp(c1.Stype.Tident, c2.Stype.Tident) == 0)
            return 1;
        return 0;
    }

debug
{
    if (!type_struct(c2.Stype))
        symbol_print(c2);
}
    assert(type_struct(c2.Stype));

    if (pethis)
    {   debug assert(*pethis);
        pe = &ethis;
    }
    else
    {   ethis = null;
        pe = null;
    }

    for (b = c2.Sstruct.Sbase; b; b = b.BCnext)
    {   Classsym *sbase = b.BCbase;

        //printf("\tbase = '%s', c1 = %p, sbase = %p\n", sbase.Sident, c1, sbase);
        if (c1 == sbase)
        {   svirtual = null;
            value = 1;
        }
        else
            value = c1isbaseofc2x(null,c1,sbase,&svirtual);

        if (value)
        {
            value += 0x100;                     /* bump derivation level */
            value |= b.BCflags & (BCFvirtual | BCFprivate);
            /* #6113 new error message */
            //value |= b.BCflags & BCFvirtual;

            if (b.BCflags & BCFvirtual && !svirtual)
                svirtual = sbase;

            if (prevvalue)
            {
                /* If pethis is null, assume that the test for  */
                /* ambiguity is done elsewhere.                 */
                if (pethis && !(svirtual && svirtual == *psvirtual))
                    cpperr(EM_ambig_ref_base,&c1.Sident[0]);       // ambiguous ref to c1

                /* Take shortest path for prevvalue     */
                if ((prevvalue & ~0xFF) > (value & ~0xFF))
                    prevvalue = (prevvalue & 0xFF) | (value & ~0xFF);
            }
            else
            {
                /* Compute ethis, which is the this pointer to sbase    */
                if (pethis)
                {   Classsym *s;

                    ethis = *pethis;
                    if (b.BCflags & BCFvirtual)
                    {
                        ethis = exp2_ptrvbaseclass(ethis,cast(Classsym *)c2,sbase);
                    }
                    else if (b.BCoffset)
                    {
                        type *tpbase;

                        tpbase = type_allocn(ethis.ET.Tty,sbase.Stype);
                        assert(typtr(tpbase.Tty));
                        ethis = el_bint(OPadd,tpbase,ethis,el_longt(tstypes[TYint],b.BCoffset));
                    }
                    *pethis = ethis;
                    if (c1 != sbase)
                        c1isbaseofc2x(pethis,c1,sbase,&s);
                }

                prevvalue = value;
                *psvirtual = svirtual;
            }
        }
    }
    return prevvalue;
}

/*********************************************************
 * Determine if c1 dominates c2, when both are inherited from stag.
 * This means that if we can find a path from stag to c2 that
 * does not pass through c1, then c1 does not dominate c2.
 */

int c1dominatesc2(Symbol *stag, Symbol *c1, Symbol *c2)
{
    baseclass_t *b;

    if (stag == c1)
        return 1;

    for (b = stag.Sstruct.Sbase; b; b = b.BCnext)
    {   Classsym *sbase = b.BCbase;

        if (sbase == c2)
        {   //if ((b.BCflags & BCFvirtual) == 0)
                return 0;
        }
        else if (sbase != c1)
        {
            if (!c1dominatesc2(sbase, c1, c2))
                return 0;
        }
    }
    return 1;
}

/***************************
 * If relax & 1:
 *      if t1 is a function with a prototype, and t2 doesn't have
 *      a prototype, it's a match.
 *      if t1 is a function without a prototype, and t2 has a prototype
 *      which are all default promotions, it's a match.
 * If relax & 2:
 *      Ignore const, volatile modifiers on first part of type
 * If relax & 4:
 *      Ignore _export
 * If relax & 8:
 *      Ignore function return values
 * if relax and 0x10    volatile ref with plain ref
 *      allow the first to be volatile and the second to be plain
 * If relax & 0x20
 *      ignore __far
 * If relax & 0x40
 *      ignore const, volatile on first part of parameter types
 * If relax & 0x80
 *      allow compatible exception specifications rather than exact ones
 * Returns:
 *      !=0 if types match.
 */

int typematch(type *t1,type *t2,int relax)
{ tym_t t1ty, t2ty;
  tym_t tym;

static if (0)
{
    printf("typematch(t1 = %p, t2 = %p, relax = x%x)\n", t1, t2, relax);
    if (t1 && t2)
    {   type_print(t1);
        type_print(t2);
    }
}

    if (relax & 2)
    {
        tym = mTYbasic;
        // C++98 4.4 Qualification conversions
        // BUG: should handle const and volatile separately with
        // separate relax bit, not combined
        if (t2 && !(t2.Tty & (mTYconst | mTYvolatile)))
            relax &= ~2;
    }
    else
    {
        tym = ~(mTYimport | mTYnaked);
    }

    if (relax & (4 | 0x20))
    {
        if (relax & 4)
            tym &= ~(mTYexport | mTYnaked);
        if (relax & 0x20)
            tym &= ~mTYfar;
    }
    int i = t1 == t2 ||
            t1 && t2 &&

            (
                /* ignore name mangling */
                (t1ty = (t1.Tty & tym)) == (t2ty = (t2.Tty & tym))

                /* see if we can relax the type checking (-Jm switch) */
                || (type_semirelax && (t1ty & ~mTYbasic) == (t2ty & ~mTYbasic) &&
                 tyrelax(t1ty) == tyrelax(t2ty))

                || (relax & 0x10 && ((t1ty & ~mTYvolatile) == t2ty))
            )
                 &&

            (tybasic(t1ty) != TYarray || t1.Tdim == t2.Tdim ||
             t1.Tflags & (TFsizeunknown | TFvla) || t2.Tflags & (TFsizeunknown | TFvla))
                 &&

            (tybasic(t1ty) != TYstruct
                && tybasic(t1ty) != TYenum
                && tybasic(t1ty) != TYmemptr
             || t1.Ttag == t2.Ttag)
                 &&

            (typematch(t1.Tnext,t2.Tnext,(relax & (4 | 2)) | 1)
                ||
             (!CPP && relax & 8 && tyfunc(t1ty))        // ignore function return value
            )
                 &&

            (!tyfunc(t1ty) ||
             ((relax & 1) && (type_relax || typerelax(t1,t2))) ||
             ((t1.Tflags & (TFfixed)) == (t2.Tflags & (TFfixed)) &&
               paramlstmatch(t1.Tparamtypes,t2.Tparamtypes) &&
               ((relax & 0x80) ? t1isSameOrSubsett2(t1, t2) : type_exception_spec_match(t1, t2))
             )
            )
                &&

            (tybasic(t1ty) != TYtemplate ||
                 template_paramlstmatch(t1, t2))
                &&

            (tybasic(t1ty) != TYident ||
                strcmp(t1.Tident, t2.Tident) == 0
            )
         ;
    //printf("typematch result = %d\n", i);
    return i;
}

/*private*/ int typerelax(type *t1,type *t2)
{
    if (t1.Tflags & TFprototype)
    {   type *t;

        t = t1;
        t1 = t2;
        t2 = t;
        goto L1;
    }
    if (t2.Tflags & TFprototype)
    {
    L1:
        param_t *p2;
        if (t1.Tflags & TFprototype)
            goto nomatch;               /* both have prototypes */

        if (config.ansi_c && !(t2.Tflags & TFfixed))
            /* Disallow: int f(int,...); int f();       */
            goto nomatch;

static if (0)
{
        /* Parameters for t2 must be all default conversions    */
        for (p2 = t2.Tparamtypes; p2; p2 = p2.Pnext)
        {
            switch (tybasic(p2.Ptype.Tty))
            {
                case TYint:
                case TYuint:
                case TYlong:
                case TYulong:
                case TYdouble:
                case TYdouble_alias:
                case TYldouble:
                case TYhptr:
                case TYstruct:
                case TYenum:
                case TYref:
                    continue;

                case TYnptr:
                    if (!LARGEDATA)
                        continue;
                    goto L2;

                case TYfptr:
                    if (LARGEDATA)
                        continue;
                L2: /* If pointer to function   */
                    if (tyfunc(p2.Ptype.Tnext.Tty))
                        continue;
                    goto nomatch;
                case TYschar:
                case TYuchar:
                case TYchar:
                case TYshort:
                case TYushort:
                    if (type_promorelax)
                        continue;
                    goto nomatch;
                default:
                    goto nomatch;
            }
        }
}
    }
    return true;

nomatch:
    return false;
}

/****************************
 * Return true if type lists match.
 */

int paramlstmatch(param_t *p1,param_t *p2)
{
    //printf("paramlstmatch()\n");
    int i = p1 == p2 ||
            p1 && p2 && typematch(p1.Ptype,p2.Ptype,2|1) &&
            paramlstmatch(p1.Pnext,p2.Pnext)
            //&& (!p1.Pelem || !p2.Pelem || el_match(p1.Pelem,p2.Pelem))
            ;
    //printf("paramlstmatch() returns %d\n", i);
    return i;
}

/****************************
 * Return true if template parameter lists match.
 */

int template_paramlstmatch(type *t1, type *t2)
{
    elem *e1;
    elem *e2;
    param_t *p1;
    param_t *p2;

    if ((cast(typetemp_t *)t1).Tsym != (cast(typetemp_t *)t2).Tsym)
        goto Lnomatch;

    p1 = t1.Tparamtypes;
    p2 = t2.Tparamtypes;

    //printf("template_paramlstmatch(p1 = %p, p2 = %p)\n", p1, p2);
    while (p1 != p2)
    {
        if (!p1 || !p2)
            goto Lnomatch;

        if (!typematch(p1.Ptype,p2.Ptype,1))
            goto Lnomatch;

        e1 = p1.Pelem;
        e2 = p2.Pelem;
        if (e1 && e2)
        {
            if (!el_match4(e1, e2))
                goto Lnomatch;
        }
        p1 = p1.Pnext;
        p2 = p2.Pnext;
    }

  Lmatch:
    //printf("-template_paramlstmatch() match\n");
    return true;

  Lnomatch:
    //printf("-template_paramlstmatch() nomatch\n");
    return false;
}


/*****************************
 * Convert E1-E2 to (E1-E2)/size
 * Input:
 *      E1-E2
 * Output:
 *      if E1 and E2 are pointers, (E1-E2)/size
 *      if E1 is a pointer, and E2 is integral, E1-E2*size
 */

elem *minscale(elem *e)
{ elem* e1,e2,eb;
  type* t1,t2;
  type *tdiff;
  targ_ptrdiff_t size;
  int i;

  assert(e);
  elem_debug(e);
  if ((e1 = e.EV.E1) == null ||
      (e2 = e.EV.E2) == null ||
      (t1 = e1.ET) == null ||
      (t2 = e2.ET) == null)
        return e;

    if (!typtr(t1.Tty))
    {   if (typtr(t2.Tty))
            typerr(EM_bad_ptr_arith,t1,t2);     // illegal pointer arithmetic
        return e;
    }


    if (!typtr(t2.Tty))
    {   scale(e);                       /* E1-E2*size                   */
        return e;
    }

    tdiff = tsptrdiff;

    if (tybasic(t1.Tty) == TYhptr)
        tdiff = tstypes[TYlong];

    /* Both leaves of e are pointers                            */
    /* Bring the result of the subtraction to be a ptrdiff_t    */
    el_settype(e,tdiff);

    if (!typecompat(t1.Tnext,t2.Tnext))
    {   typerr(EM_type_mismatch,t1,t2);         // pointer mismatch
        return e;
    }

    eb = el_typesize(t1.Tnext);

    /* Try to bring pointers to a common type   */
    if (tybasic(t1.Tty) == tybasic(t2.Tty))
    { }
    else if (tysize(t2.Tty) == _tysize[TYint] ||
             tybasic(t1.Tty) == TYhptr)
    {   e.EV.E2 = typechk(e2,t1);
        t2 = t1;
    }
    else
        e.EV.E1 = typechk(e1,t2);
    if (tysize(t2.Tty) > _tysize[TYint])
    {   e.EV.E1 = lptrtooffset(e.EV.E1);
        e.EV.E2 = lptrtooffset(e.EV.E2);
    }

    el_settype(eb,tdiff);               /* so we get a signed divide    */
    e = el_bint(OPdiv,tdiff,e,eb);
    if (e.Eoper != OPconst)
        return e;
    size = eb.EV.Vptrdiff;
    if (size == 0)
    {   synerr(EM_0size);               // object has 0 size
        size++;
    }

    // Determine if size is a power of 2. If so, convert to a signed
    // right shift. We can do this because we should never have remainders
    i = ispow2(cast(targ_uns) size);
    if (i != -1)
    {   size = i;
        e.Eoper = OPshr;
    }
    eb.EV.Vptrdiff = size;             /* divisor (or shift count)     */
    return e;
}


/*****************************
 * Convert (E1 op E2) to (E1 op s1*E2)
 * E1 must be a pointer and E2 must be an integral type.
 * If the reverse is true, swap E1 and E2.
 */

void scale(elem *eop)
{ elem* e1,e2;
  tym_t ty1,ty2;
  type *t;
  elem *esize;

  assert(eop);
  elem_debug(eop);
  e1 = eop.EV.E1;
  e2 = eop.EV.E2;
  assert(e1 && e2 && e1.ET && e2.ET);
  ty2 = tybasic(e2.ET.Tty);
  if (typtr(ty2))                       /* then swap e1 and e2  */
  {     e1 = eop.EV.E2;
        e2 = eop.EV.E1;
        ty1 = ty2;
        ty2 = tybasic(e2.ET.Tty);
  }
  else
  {     ty1 = tybasic(e1.ET.Tty);
        if (!typtr(ty1))                /* no scaling necessary         */
            return;
  }
  if (!tyintegral(ty2))                 /* if e2 is not an integral type */
  {     typerr(EM_bad_ptr_arith,eop.EV.E1.ET,eop.EV.E2.ET); // illegal pointer arithmetic
        return;
  }
    esize = el_typesize(e1.ET.Tnext);

    if (ty1 == TYhptr)
    {   esize = _cast(esize,tstypes[TYlong]);
        t = tstypes[TYlong];
        if (ty2 != TYlong && ty2 != TYulong)    // make sure it's a long
        {       e2 = tyuns(ty2)
                      ? _cast(e2,tstypes[TYulong])
                      : _cast(e2,t);
        }
    }
    else
    {   t = tstypes[TYint];
        if (ty2 != TYint && ty2 != TYuint)      /* make sure it's an int        */
        {       e2 = tyuns(ty2)
                      ? _cast(e2,tstypes[TYuint])
                      : _cast(e2,t);
        }
    }
    assert(e1.ET.Tnext);
    eop.EV.E2 = el_bint(OPmul,t,e2,esize);
    eop.EV.E1 = e1;                               /* in case we swapped them      */
}


/*****************************
 * Do implicit conversions (pg. 41 of K & R)
 * Input:
 *      e .    a binary arithmetic operator node
 * Output:
 *      any type conversions necessary
 */

void impcnv(elem *e)
{   tym_t t1,t2;
    elem* e1,e2;
    type *tresult = null;

    assert(e && !OTleaf(e.Eoper));
    elem_debug(e);
    e1 = e.EV.E1 = convertchk(cpp_bool(e.EV.E1, 0));
    e2 = e.EV.E2 = convertchk(cpp_bool(e.EV.E2, 0)); /* perform integral promotions */
    t1 = tybasic(e1.ET.Tty);
    t2 = tybasic(e2.ET.Tty);

    if (!tyscalar(t1) || !tyscalar(t2))
    {
      Lerr:
        typerr(EM_illegal_op_types,e1.ET,e2.ET); // illegal operand types
    }
    else if (t1 != t2)
    {   type* newt1,newt2;

        newt1 = e1.ET;
        newt2 = e2.ET;
        if (tynullptr(t1))
        {
            if (tyarithmetic(t2))
                goto Lerr;
        }
        else if (tynullptr(t2))
        {
            if (tyarithmetic(t1))
                goto Lerr;
        }
        else if (typtr(t1) || typtr(t2))
        {
            switch (t1)
            {   case TYfptr:
                case TYvptr:
                case TYhptr:
                    if (t2 == TYsptr || t2 == TYnptr ||
                        t2 == TYcptr || t2 == TYf16ptr)
                    {   newt2 = type_allocn(TYfptr | (t2 & ~mTYbasic),newt2.Tnext);
                    }
                    break;

                case TYsptr:
                case TYnptr:
                case TYcptr:
                case TYf16ptr:
                    if (t2 == TYfptr || t2 == TYvptr || t2 == TYhptr)
                    {
                        newt1 = type_allocn(TYfptr | (t1 & ~mTYbasic),newt1.Tnext);
                    }
                    break;

                default:
                    break;
            }
        }
        /* Do the usual arithmetic conversions  */
        else if (t1 > t2)
        {
            switch (t1)
            {   case TYldouble: newt2 = tstypes[TYldouble];      break;
                case TYdouble_alias:    newt2 = tstypes[TYdouble_alias];       break;
                case TYdouble:  newt2 = tstypes[TYdouble];       break;
                case TYfloat:   newt2 = tstypes[TYfloat];        break;
                case TYullong:  newt2 = tstypes[TYullong];       break;
                case TYllong:   newt2 = tstypes[TYllong];        break;
                case TYulong:   newt2 = tstypes[TYulong];        break;
                case TYlong:
                                if (_tysize[TYint] == LONGSIZE && t2 == TYuint)
                                {   newt1 = tstypes[TYulong];
                                    newt2 = tstypes[TYulong];
                                }
                                else
                                    newt2 = tstypes[TYlong];
                                break;
                case TYuint:    newt2 = tstypes[TYuint];          break;

                case TYifloat:  switch (t2)
                                {
                                    case TYdouble:
                                        newt1 = tstypes[TYidouble];
                                        break;
                                    case TYldouble:
                                        newt1 = tstypes[TYildouble];
                                        break;
                                    default:
                                        newt2 = tstypes[TYfloat];
                                        break;
                                }
                                break;
                case TYidouble: if (tyimaginary(t2))
                                    newt2 = tstypes[TYidouble];
                                else
                                    newt2 = tstypes[TYdouble];
                                break;
                case TYildouble: if (tyimaginary(t2))
                                    newt2 = tstypes[TYildouble];
                                else
                                    newt2 = tstypes[TYldouble];
                                break;
                case TYcfloat:
                                switch (t2)
                                {
                                    case TYfloat:
                                    case TYifloat:
                                    case TYcfloat:
                                        break;
                                    case TYdouble:
                                    case TYidouble:
                                        newt1 = tstypes[TYcdouble];
                                        break;
                                    case TYldouble:
                                    case TYildouble:
                                        newt1 = tstypes[TYcldouble];
                                        break;
                                    case TYdouble_alias:
                                        assert(0);
                                        break;
                                    default:
                                        newt2 = tstypes[TYfloat];
                                        break;
                                }
                                break;

                case TYcdouble:
                                switch (t2)
                                {
                                    case TYifloat:
                                        newt2 = tstypes[TYidouble];
                                        break;
                                    case TYcfloat:
                                        newt2 = tstypes[TYcdouble];
                                        break;
                                    case TYdouble:
                                    case TYidouble:
                                    case TYcdouble:
                                        break;
                                    case TYldouble:
                                    case TYildouble:
                                        newt1 = tstypes[TYcldouble];
                                        break;
                                    case TYdouble_alias:
                                        assert(0);
                                        break;
                                    default:
                                        newt2 = tstypes[TYdouble];
                                        break;
                                }
                                break;
                case TYcldouble:
                                switch (t2)
                                {
                                    case TYifloat:
                                    case TYidouble:
                                        newt2 = tstypes[TYildouble];
                                        break;
                                    case TYcfloat:
                                    case TYcdouble:
                                        newt2 = tstypes[TYcldouble];
                                        break;
                                    case TYldouble:
                                    case TYildouble:
                                    case TYcldouble:
                                        break;
                                    case TYdouble_alias:
                                        assert(0);
                                        break;
                                    default:
                                        newt2 = tstypes[TYldouble];
                                        break;
                                }
                                break;

                default:
                                break;
            }
        }
        else
        {
            switch (t2)
            {
                case TYldouble: newt1 = tstypes[TYldouble];      break;
                case TYdouble_alias:    newt1 = tstypes[TYdouble_alias];       break;
                case TYdouble:  newt1 = tstypes[TYdouble];       break;
                case TYfloat:   newt1 = tstypes[TYfloat];        break;
                case TYullong:  newt1 = tstypes[TYullong];       break;
                case TYllong:   newt1 = tstypes[TYllong];        break;
                case TYulong:   newt1 = tstypes[TYulong];        break;
                case TYlong:
                                if (_tysize[TYint] == LONGSIZE && t1 == TYuint)
                                {   newt1 = tstypes[TYulong];
                                    newt2 = tstypes[TYulong];
                                }
                                else
                                    newt1 = tstypes[TYlong];
                                break;
                case TYuint:    newt1 = tstypes[TYuint];          break;

                case TYifloat:
                                switch (t1)
                                {
                                    case TYdouble:
                                        newt2 = tstypes[TYidouble];
                                        break;
                                    case TYldouble:
                                        newt2 = tstypes[TYildouble];
                                        break;
                                    default:
                                        newt1 = tstypes[TYfloat];
                                        break;
                                }
                                break;
                case TYidouble: if (tyimaginary(t1))
                                    newt1 = tstypes[TYidouble];
                                else
                                    newt1 = tstypes[TYdouble];
                                tresult = newt2;
                                break;
                case TYildouble: if (tyimaginary(t1))
                                    newt1 = tstypes[TYildouble];
                                else
                                    newt1 = tstypes[TYldouble];
                                tresult = newt2;
                                break;
                case TYcfloat:
                                switch (t1)
                                {
                                    case TYfloat:
                                    case TYifloat:
                                    case TYcfloat:
                                        break;
                                    case TYdouble:
                                    case TYidouble:
                                        newt2 = tstypes[TYcdouble];
                                        break;
                                    case TYldouble:
                                    case TYildouble:
                                        newt2 = tstypes[TYcldouble];
                                        break;
                                    case TYdouble_alias:
                                        assert(0);
                                        break;
                                    default:
                                        newt1 = tstypes[TYcfloat];
                                        break;
                                }
                                tresult = newt2;
                                break;

                case TYcdouble:
                                switch (t1)
                                {
                                    case TYifloat:
                                        newt1 = tstypes[TYidouble];
                                        break;
                                    case TYcfloat:
                                        newt1 = tstypes[TYcdouble];
                                        break;
                                    case TYdouble:
                                    case TYidouble:
                                    case TYcdouble:
                                        break;
                                    case TYldouble:
                                    case TYildouble:
                                        newt2 = tstypes[TYcldouble];
                                        break;
                                    case TYdouble_alias:
                                        assert(0);
                                        break;
                                    default:
                                        newt1 = tstypes[TYdouble];
                                        break;
                                }
                                tresult = newt2;
                                break;
                case TYcldouble:
                                switch (t1)
                                {
                                    case TYifloat:
                                    case TYidouble:
                                        newt1 = tstypes[TYildouble];
                                        break;
                                    case TYcfloat:
                                    case TYcdouble:
                                        newt1 = tstypes[TYcldouble];
                                        break;
                                    case TYldouble:
                                    case TYildouble:
                                    case TYcldouble:
                                        break;
                                    case TYdouble_alias:
                                        assert(0);
                                        break;
                                    default:
                                        newt1 = tstypes[TYldouble];
                                        break;
                                }
                                tresult = newt2;
                                break;

                case TYmemptr:  if (tyfloating(t1))
                                    goto Lerr;
                                break;

                default:
                                break;
            }
        }

        e.EV.E1 = _cast(e1,newt1);
        e.EV.E2 = _cast(e2,newt2);
    }
    if (!e.ET)                         /* if not already assigned      */
    {
        if (!tresult)
            tresult = e.EV.E1.ET;        // type of lvalue
        el_settype(e,tresult);
    }
}


/*******************************
 * Bring pointers to a common type.
 * Suitable for == < : operators.
 */

void exp2_ptrtocomtype(elem *e)
{   tym_t ty1,ty2;
    elem* e1,e2;
    type* t1,t2;

    impcnv(e);
    e1 = e.EV.E1;
    t1 = e1.ET;
    ty1 = t1.Tty;
    e2 = e.EV.E2;
    t2 = e2.ET;
    ty2 = t2.Tty;
    // BUG: what happens with c-v when an operand is nullptr?
    if (typtr(ty1) && typtr(ty2))
    {
        type* t,u;

        if (exp2_ptrconv(t1,t2) && tybasic(t1.Tnext.Tty) != TYvoid)
            t = t2;
        else if (exp2_ptrconv(t2,t1))
            t = t1;
        else if (!I32 &&
                 tysize(ty1) == REGSIZE &&
                 tysize(ty2) == REGSIZE)
        {   t = newpointer(t1.Tnext);
            t.Tty = (t.Tty & ~mTYbasic) | TYfptr;
        }
        else
            goto L1;

        /* Combine const and volatile characteristics into common type  */

        t.Tcount++;
        if (tyfunc(t.Tnext.Tty))
            /* BUG: what if pointer to const member function?   */
            u = t;
        else
        {
            u = type_copy(t.Tnext);
            u.Tty |= (t1.Tnext.Tty | t2.Tnext.Tty) & (mTYconst | mTYvolatile | mTYLINK);
            u = type_allocn(t.Tty,u);
        }
        type_free(t);

        /* Bring both operands to common type   */
        e.EV.E1 = _cast(e1,u);
        e.EV.E2 = _cast(e2,u);

        return;
    }

L1:
    if (tymptr(ty1))
        e.EV.E2 = typechk(e2,t1);
    else if (tymptr(ty2))
        e.EV.E1 = typechk(e1,t2);
}

/***********************************
 * Determine if implicit conversions between pointer types is possible.
 * Returns:
 *      !=0     possible, larger values imply a 'worse' match
 *      1       pointer types are the same
 *      0       not without explicit cast
 */

int exp2_ptrconv(type *tfrom,type *tto)
{   tym_t tfm,ttm;
    type* tton,tfromn;
    int result = 0;

    type_debug(tfrom);
    type_debug(tto);
    tfm = tybasic(tfrom.Tty);
    ttm = tybasic(tto.Tty);

    if (tfm == TYmemptr && ttm == TYmemptr)
    {
        if (!typematch(tto.Tnext,tfrom.Tnext,0))
            goto ret;
        result = c1isbaseofc2(null,tfrom.Ttag,tto.Ttag);
        if (result)
            result = (result >> 8) + 1;
        goto ret;
    }

    if (!typtr(tfm) || !typtr(ttm))
        goto ret;                               /* no match             */

    /* First, see if pointer types themselves are compatible    */
    static uint hash(tym_t from, tym_t to) { return (from << 8) | to; }

    switch (hash(tfm,ttm))
    {
        case hash(TYnptr,TYsptr):
        case hash(TYsptr,TYnptr):
            if (config.wflags & WFssneds)
                goto ret;                       /* no match             */
            break;
        case hash(TYfptr,TYsptr):
        case hash(TYfptr,TYnptr):
            if (type_relax)                     // VC 1.5 allows this (!)
                break;
            goto ret;                           // no match;

        case hash(TYfptr,TYcptr):
        case hash(TYvptr,TYsptr):
        case hash(TYvptr,TYnptr):
        case hash(TYvptr,TYcptr):
        case hash(TYhptr,TYsptr):
        case hash(TYhptr,TYnptr):
        case hash(TYhptr,TYcptr):

        case hash(TYnptr,TYcptr):
        case hash(TYsptr,TYcptr):
        case hash(TYcptr,TYnptr):
        case hash(TYcptr,TYsptr):

        case hash(TYfptr,TYf16ptr):
        case hash(TYvptr,TYf16ptr):
        case hash(TYhptr,TYf16ptr):
//      case hash(TYhptr,TYfptr):
        case hash(TYhptr,TYvptr):
            goto ret;                           /* no match             */

        /* Allow other conversions      */
        default:
            break;
    }

    /* Allow conversions to/from void*  */
    tfromn = tfrom.Tnext;
    tton = tto.Tnext;
    if (tybasic(tton.Tty) == TYvoid ||
        (config.flags4 & CFG4implicitfromvoid && tybasic(tfromn.Tty) == TYvoid)
       )
    {
        if (CPP)
        {
            /* Pick a value that will not quite get us from MATCHstandard
               to MATCHuserdef (leave room for 'adjustment')
             */
            result = (TMATCHstandard - TMATCHuserdef) - 7;
            if (tybasic(tton.Tty) != tybasic(tfromn.Tty))
                result++;               // worse if converting to/from void*
        }
        else
            result = 2;
        goto ret;
    }

    result = CPP ? t1isbaseoft2(tton,tfromn) : 0;
    if (result)
        result = (result >> 8) + 1;
    else
        result = type_relax ? 1 : typematch(tfromn,tton,4|3);

ret:
    return result;
}

/* Construct a table that gives the action when casting from one type   */
/* to another.                                                          */

/* Rename these mainly to get the size down, so the table isn't so wide */
enum SHTLNG  = OPs16_32;
enum USHLNG  = OPu16_32;
enum DBLLNG  = OPd_s32;
enum LNGDBL  = OPs32_d;
enum DBLULNG = OPd_u32;
enum ULNGDBL = OPu32_d;
enum DBLSHT  = OPd_s16;
enum SHTDBL  = OPs16_d;
enum DBLUSH  = OPd_u16;
enum USHDBL  = OPu16_d;
enum DBLFLT  = OPd_f;
enum FLTDBL  = OPf_d;
enum LNGSHT  = OP32_16;
enum U8INT   = OPu8_16;
enum S8INT   = OPs8_16;
enum INT8    = OP16_8;
enum ULNGLL  = OPu32_64;
enum LNGLL   = OPs32_64;
enum LLLNG   = OP64_32;
enum PTRLPTR = OPnp_fp;
enum OFFSET  = OPoffset;
enum FPTR    = OPvp_fp;
enum TOF16   = OPnp_f16p;
enum FRF16   = OPf16p_np;

enum NONE    = OPMAX;           /* no conversion                        */
enum PAINT   = (OPMAX+1);       /* just 'paint' new type over the old one */
enum ERROR   = (OPMAX+2);       /* syntax error                         */
enum REF     = (OPMAX+3);       /* special handling with references     */
enum WARN    = (OPMAX+4);       /* generate warning before casting      */
enum LNGFPTR = (OPMAX+5);       /* convert long to far pointer          */
enum CINT    = (OPMAX+6);       /* convert char to int                  */
enum VOID    = (OPMAX+7);       // convert to void

/* Convert to this type and try again   */
enum INT     = (OPMAX+8);
enum UINT    = (OPMAX+9);
enum LONG    = (OPMAX+10);
enum ULONG   = (OPMAX+11);
enum DOUBLE  = (OPMAX+12);
enum IDOUBLE = (OPMAX+14);
enum CDOUBLE = (OPMAX+15);
enum CFLOAT  = (OPMAX+16);
enum CLDOUBLE = (OPMAX+17);
enum NPTR    = (OPMAX+18);
enum FTOC    = (OPMAX+19);      // real or imaginary to complex
enum LNGLNG  = (OPMAX+20);
enum ZERO    = (OPMAX+21);      // result is a constant 0
enum ENUM    = (OPMAX+22);
enum BOOL    = OPbool;          // boolean conversion

enum CASTTABMAX      = (TYvoid + 1);

mixin (import ("castab.d"));

/*************************
 * Cast e to type newt (no cast if newt is null).
 * Input:
 *      flags   1       no user defined conversions
 */

elem *exp2_cast(elem *e,type *newt)
{
    return exp2_castx(e,newt,null,1);
}

elem *_cast(elem *e,type *newt)
{
    return exp2_castx(e,newt,null,0);
}

/*private*/ elem * exp2_castx(elem *e,type *newt,elem **pethis,int flags)
{ type *oldt;
  tym_t newty,oldty;
  uint action;
  type *t = null;

  if (!newt)
        goto ret;                       /* no conversion to be done     */
  type_debug(newt);
static if (0)
{
  dbg_printf("cast from %p:\n", e.ET);  type_print(e.ET);
  dbg_printf("to        %p:\n", newt);  type_print(newt);
  elem_print(e);
  fflush(stdout);
  if (controlc_saw)
        exit(1);
}
again:
  assert(e);
  elem_debug(e);
  oldt = e.ET;
  assert(oldt);
  type_debug(oldt);
  if (newt == oldt)                     /* if cast to same type         */
        goto ret;

    oldty = tybasic(oldt.Tty);
    newty = tybasic(newt.Tty);

    // ARM says that (T&)X is equivalent to *(T*)&X
    if (tyref(newty))
    {
        switch (e.Eoper)
        {   case OPbit:
            case OPconst:
                synerr(EM_noaddress);   // can't take address of bit field or constant
                break;
            case OPstring:
                break;
            default:
                if (!(flags & 1) &&
                    !t1isbaseoft2(newt.Tnext, oldt) &&
                    cpp_cast(&e,newt,4|1))
                {
                    return e;
                }
                e = exp2_addr(reftostar(e));
                chklvalue(e);
                break;
        }
        e = _cast(e,newpointer(newt.Tnext));
        e = el_unat(OPind,newt.Tnext,e);
        return e;
    }

    /* Do this *after* any cast to reference    */
    if (oldty == TYarray || tyfunc(oldty))
    {   e = arraytoptr(e);
        goto again;
    }

    if (!CPP)
        goto L1;
    if (!(flags & 1) && cpp_cast(&e,newt,1))    // if found a user-defined conversion
    {
        //printf("found user defined conversion from:\n"); type_print(oldt);
        //printf("to:\n"); type_print(newt);
        goto ret;
    }

    /* Allow conversion of ints to member pointers                      */
    if (newty == TYmemptr)
    {   if (tyintegral(oldty))
        {
            if (tysize(tym_conv(newt)) > REGSIZE)
                e = _cast(e,tstypes[TYulong]);
            else
                e = _cast(e,tstypes[TYuint]);
            goto paint;
        }
        if (oldty == TYmemptr)
        {
            /* A pointer to member may be explicitly converted into a
                different pointer to member type when the two types are
                both pointers to members of the same class or when the
                two types are pointers to member functions of classes
                derived from each other.
             */
            Classsym *oldc = oldt.Ttag;
            Classsym *newc = newt.Ttag;
            Classsym *svirtual;
            elem *ethis;
            targ_size_t d;      /* offset to this       */
            int result;
            int sign;

            if (oldc == newc)
                goto paint;
            ethis = el_longt(newpointer(oldt),0);
            sign = 1;
            result = c1isbaseofc2x(&ethis,oldc,newc,&svirtual);
            if (!result)
            {   result = c1isbaseofc2x(&ethis,newc,oldc,&svirtual);
                sign = -1;
            }
            ethis = poptelem(ethis);
            d = sign * ethis.EV.Vint;
            el_free(ethis);

            /* Don't know how to handle things if base class is virtual */
            if (!result || svirtual)
                goto error;

            /* Now we have the this offset (d). For pointers to data,   */
            /* just add in d. For pointers to functions, we'll need to  */
            /* create a new thunk.                                      */
            if (d == 0)                 /* if no offset                 */
                goto paint;
            if (tyfunc(oldt.Tnext.Tty))       /* if pointer to function */
            {
                if (e.Eoper == OPrelconst)
                {
                    e.EV.Vsym = nwc_genthunk(e.EV.Vsym,d,-1,0);
                    //symbol_keep(e.EV.Vsym);
                    goto paint;
                }
                else if (e.Eoper == OPconst)   /* assume cast of null  */
                    goto paint;
                else if (pethis)
                {   // Add d to ethis
                    *pethis = el_bint(OPadd,(*pethis).ET,*pethis,el_longt(tstypes[TYint],d));
                }
                else if (I32)
                {   /* Dynamically generate a thunk by calling
                     *  __genthunk(d, thunkty, e);
                     */
                    tym_t thunkty = tybasic(oldt.Tnext.Tty);
                    if (thunkty == TYmfunc)
                        thunkty = 0x33;
                    else if (thunkty == TYjfunc)
                        thunkty = 0x34;
                    else
                        thunkty = 0;
                    cpp_getpredefined();
                    e = el_bint(OPcall, newt,
                            el_var(s_genthunk),
                            el_params(e,
                                el_longt(tstypes[TYuint], thunkty),
                                el_longt(tstypes[TYuint], d),
                                null));

                    /* BUG: pointer to members should compare equally if
                     * they point to the same instance. This possibly
                     * may not, but can be fixed by writing a runtime
                     * compare function that will analyze the code in
                     * the thunk.
                     */
                }
                else
                {
                    goto error;                 // not implemented
                }
            }
            else
            {   /* pointer to data member       */
                el_settype(e,tstypes[TYint]);
                e = el_bint(OPadd,newt,e,el_longt(tstypes[TYint],d));
                goto ret;
            }
        }
    }
    else if (oldty == TYmemptr)
    {   tym_t tcv = tym_conv(oldt);

        if (_tysize[newty] == tysize(tcv))
        {
            /* Allow conversion of <memptr><data> to <int>              */
            if (tyintegral(newty) && tyintegral(tcv))
                goto paint;

            /* Allow conversion of <memptr><func> to <ptr><func>        */
            if (typtr(tcv) && typtr(newty) && tyfunc(newt.Tnext.Tty))
                goto paint;
        }

        // C++98 4.12 Allow conversion of member pointer to bool
        if (newty == TYbool)
        {
            action = BOOL;
            goto doaction;
        }
    }

    /* Special code for converting from pointer to derived class to     */
    /* pointer to base class.                                           */
    if (typtr(newty) && typtr(oldty) &&
        type_struct(newt.Tnext) &&
        type_struct(oldt.Tnext))
    {   elem *ec;
        int isbase;
        int result;
        Classsym *basetag;
        Classsym *deritag;

        basetag = oldt.Tnext.Ttag;
        deritag = newt.Tnext.Ttag;
        if (basetag == deritag)
            goto L1;                    /* no conversion necessary      */
        /*ec = el_copytree(e);*/
        ec = el_same(&e);
        result = c1isbaseofc2(&ec,deritag,basetag);
        if (result)
        {   Classsym *tmp = basetag;
            basetag = deritag;
            deritag = tmp;              /* swap basetag and deritag     */
            isbase = 0;
        }
        else
        {   Classsym *svirtual;

            result = c1isbaseofc2x(&ec,basetag,deritag,&svirtual);
            isbase = result;
            if (svirtual)
            {   /* Can't convert from virt base */
                cpperr(EM_vbase,&basetag.Sident[0],&deritag.Sident[0]);
                goto L2;
            }
            else if (isbase == 0)       /* new is not a sub-object of old */
            {
             L2:
                el_free(ec);
                goto L1;
            }
        }

        /*  Cannot convert a pointer to a class X to a pointer
            to a private base class Y unless function is a member or
            a friend of X.
         */
        if (result & BCFprivate && !cpp_funcisfriend(funcsym_p,deritag))
        {
            //cpperr(EM_cvt_private_base,&deritag.Sident[0],&basetag.Sident[0]); // cannot convert to private base
        }

        if (el_match(e,ec))
            el_free(ec);
        else if (ec.Eoper == OPvar)
        {
            /* el_same(), above, created e <= ((tmp=e),tmp) and ec <= tmp. */
            /* Undo the effect of el_same()                             */
            elem *e2;

            e = selecte1(e,e.ET);      /* get rid of ,tmp      */
            e2 = e.EV.E2;
            e.EV.E2 = null;
            el_free(e);
            e = e2;                     /* get rid of tmp=      */
            if (init_staticctor)
                symbol_free(ec.EV.Vsym);
            el_free(ec);
        }
        else
        {
            if (isbase)
            {   /* We have to cleverly convert ec, which looks like     */
                /* e+c, into e-c.                                       */
                elem *e1;
                elem *es;

                es = e;
                while (es.Eoper == OPcomma)
                    es = es.EV.E2;

                e1 = ec;
                do
                {
                    assert(e1.Eoper == OPadd);
                    e1.Eoper = OPmin;
                    e1 = e1.EV.E1;
                    elem_debug(e1);
                } while (!el_match(es,e1));
            }

            /* Build (newt)(e ? ec : null)      */
            type *tc = type_allocn(oldt.Tty,newt.Tnext);     // retain old pointer type
            ec = el_bint(OPcolon,tc,ec,el_longt(tc,0));
            return _cast(el_bint(OPcond,tc,e,ec),newt); /* convert to new ptr type */
        }
    }
L1:
    if (oldty == newty && (oldty != TYstruct || oldt.Ttag == newt.Ttag))
        goto paint;
    if (oldty == TYarray)
    {   e = _cast(arraytoptr(e),newt);
        goto ret;
    }

    if (oldty >= CASTTABMAX || newty >= CASTTABMAX)
    {
        if (newty == TYvoid)            /* allow cast to void           */
            goto paint;
        if (type_relax)
        {
            /* Allow casting as a paint if types are the same size      */
            if (type_size(newt) == type_size(oldt))
                goto paint;
        }
        if ((oldty == TYident || newty == TYident) && pstate.STintemplate)
            goto paint;
        goto error;
    }

    action = (tysize(TYint) == LONGSIZE)
                ? casttab32[oldty][newty]
                : casttab[oldty][newty];
    switch (action)
    {
        case ERROR:
        error:
if (config.exe & EX_posix)
{
            if (oldty == TYvoid)        /* allow assignment from void */
                goto paint;
}
            typerr(EM_illegal_cast,oldt,newt);  // illegal cast
            goto paint;

        case NONE:              /* no conversion (but maybe 'const' or 'volatile') */
        case PAINT:             /* just 'paint' new type over the old one */
        paint:
            e = el_settype(e,newt);
            break;

        case REF:               /* special handling with references     */
            e = _cast(e,newt.Tnext);
            break;

static if (0)
{
        case WARN:              /* generate warning before casting      */
            e = poptelem(e);
            if (!cnst(e))
                synerr(EM_int2far);     // suspect conversion to far ptr
            t = tstypes[TYlong];
            goto retry;
}

        /* Convert to another type and try again        */
        case LONG:      t = tstypes[TYlong];     goto retry;
        case DOUBLE:    t = tstypes[TYdouble];   goto retry;
        case IDOUBLE:   t = tstypes[TYidouble];  goto retry;
        case CFLOAT:    t = tstypes[TYcfloat];   goto retry;
        case CDOUBLE:   t = tstypes[TYcdouble];  goto retry;
        case CLDOUBLE:  t = tstypes[TYcldouble]; goto retry;
        case INT:       t = tstypes[TYint];      goto retry;
        case UINT:      t = tstypes[TYuint];      goto retry;
        case ULONG:     t = tstypes[TYulong];    goto retry;

        case LNGLNG:
            if (config.exe & EX_posix)
            {
                t = tstypes[TYllong];
                goto retry;
            }
            goto default;

        retry:
            e = _cast(_cast(e,t),newt);
            break;

        case FTOC:
        {   elem *ezero;
            type *tzero;
            type *tcomplex;

            switch (oldty)
            {
                case TYfloat:   tzero = tstypes[TYifloat]; tcomplex = tstypes[TYcfloat]; break;
                case TYifloat:  tzero = tstypes[TYfloat];  tcomplex = tstypes[TYcfloat]; break;
                case TYdouble:  tzero = tstypes[TYidouble]; tcomplex = tstypes[TYcdouble]; break;
                case TYidouble: tzero = tstypes[TYdouble];  tcomplex = tstypes[TYcdouble]; break;
                case TYldouble: tzero = tstypes[TYildouble]; tcomplex = tstypes[TYcldouble]; break;
                case TYildouble: tzero = tstypes[TYldouble]; tcomplex = tstypes[TYcldouble]; break;
                default:
                    assert(0);
            }
            ezero = el_zero(tzero);
            e = el_bint(OPadd, tcomplex, e, ezero);
            goto again;
        }

        case LNGFPTR:
            if (e.Eoper == OPconst && el_tolong(e) == 0)
            {   e.EV.Vseg = 0;             /* set e to (far *)0    */
                goto paint;
            }
            else
                goto error;
        case PTRLPTR:
            if (e.Eoper == OPcond)
            {   elem *e2;

                e2 = e.EV.E2;
                assert(e2.Eoper == OPcolon);
                e2.EV.E1 = el_unat(action,newt,e2.EV.E1);
                e2.EV.E2 = el_unat(action,newt,e2.EV.E2);
                el_settype(e2,newt);
                el_settype(e,newt);
                break;
            }
            else
                goto doaction;

        case OP32_16:
        case OP16_8:
        case OP64_32:
        case OPoffset:
            if (config.ansi_c)
                goto doaction;

            // Support such abominations like &(short)x, since Microsoft C does.
            if (e.Eoper == OPvar || e.Eoper == OPind)
                el_settype(e,newt);
            else
                goto doaction;
            break;

        case FPTR:
            /* If handle pointer to const, use the constant conversion  */
            action = (newt.Tnext.Tty & mTYconst) ? OPcvp_fp : OPvp_fp;
            goto doaction;

        case CINT:              /* convert char to int                  */
            action = (config.flags & CFGuchar) ? OPu8_16 : OPs8_16;
            goto doaction;

        case VOID:
            if (config.exe & EX_flat &&
                OTcall(e.Eoper) &&
                tyfloating(e.ET.Tty)
               )
            {   // A float is returned in ST0
                action = OPvoid;
                goto doaction;
            }
            else
                goto paint;

        case ZERO:
            e = el_bint(OPcomma, newt, e, el_zero(newt));
            break;

        case ENUM:
            e = el_settype(e, oldt.Tnext);
            e = exp2_cast(e, newt);
            break;

        case OPnp_f16p:
        case OPf16p_np:
            if (config.exe & (EX_OSX | EX_OSX64))
                goto paint;
            goto default;

        default:                /* action is operator number            */
        doaction:
static if (0)
{
dbg_printf("action = %d ",action);
if (cast(uint) action < OPMAX) WROP(action);
printf("\n");
dbg_printf("cast from:\n");  type_print(e.ET);
dbg_printf("to:\n");  type_print(newt);
elem_print(e);
}
            assert(cast(uint) action < OPMAX && OTunary(action));
            e = el_unat(action,newt,e);
            break;
    }
ret:
    return e;
}


/**********************************
 * Create the second exp for an element
 * which is the size of the first one (do alignment too).
 * This is used for ++ and -- operators to give the increment.
 */

void getinc(elem *e)
{ elem *e2;

  elem_debug(e);
  e.EV.E2 = e2 = el_longt(tstypes[TYint],1);       /* inc always by an int         */
  if (typtr(e.EV.E1.ET.Tty))            /* if operating with a pointer  */
        scale(e);                       /* do any scaling necessary     */
  /*else if (tybasic(e.EV.E1.ET.Tty) == TYfloat)
        e.EV.E2 = typechk(e.EV.E2,tstypes[TYdouble]);*/
  else
  {
        if (CPP)
            chknosu(e.EV.E1);             // don't increment structs
        e.EV.E2 = typechk(e.EV.E2,e.EV.E1.ET);
  }
}


}

