/**
 * Implementation of the
 * $(LINK2 http://www.digitalmars.com/download/freecompiler.html, Digital Mars C/C++ Compiler).
 *
 * Copyright:   Copyright (c) 1984-1998 by Symantec, All Rights Reserved
 *              Copyright (c) 2000-2017 by Digital Mars, All Rights Reserved
 * Authors:     $(LINK2 http://www.digitalmars.com, Walter Bright)
 * License:     Distributed under the Boost Software License, Version 1.0.
 *              http://www.boost.org/LICENSE_1_0.txt
 * Source:      https://github.com/DigitalMars/Compiler/blob/master/dm/src/dmc/init.c
 */

// Parse initializers.

module dinit;

version (HTOD) { version = COMPILEIT; }
version (SCPP) { version = COMPILEIT; }

version (COMPILEIT)
{
import core.stdc.stdio;
import core.stdc.string;
import core.stdc.stdlib;


import dtoken;
import msgs2;
import parser;
import scopeh;

import ddmd.backend.bcomplex;
import ddmd.backend.cdef;
import ddmd.backend.cc;
import ddmd.backend.cpph;
import ddmd.backend.dt;
import ddmd.backend.el;
import ddmd.backend.global;
import ddmd.backend.oper;
import ddmd.backend.ty;
import ddmd.backend.type;

import tk.dlist;
import tk.mem;

extern (C++):

int endofarray();
elem* initarrayelem(Symbol *s,type *t,targ_size_t offset);
void init_closebrack(int brack);
//elem* initelem(type *, DtBuilder, Symbol *,targ_size_t);
size_t getArrayIndex(size_t i, size_t dim, char unknown);

enum
{
    DT_abytes = 0,
    DT_azeros = 1,
    DT_xoff   = 2,
    DT_nbytes = 3,
    DT_common = 4,
    DT_coff   = 5,
    DT_ibytes = 6,
}

bool I16() { return _tysize[TYnptr] == 2; }
bool I32() { return _tysize[TYnptr] == 4; }
bool I64() { return _tysize[TYnptr] == 8; }

void TOOFFSET(void* p, targ_size_t value)
{
    final switch (_tysize[TYnptr])
    {
        case 2: *cast(ushort*)p = cast(ushort)value; break;
        case 4: *cast(uint*)  p = cast(uint)  value; break;
        case 8: *cast(ulong*) p = cast(ulong) value; break;
    }
}

/+
#include        <stdio.h>
#include        <string.h>
#include        <stdlib.h>
#include        "cc.h"
#include        "parser.h"
#include        "token.h"
#include        "oper.h"
#include        "global.h"
#include        "el.h"
#include        "type.h"
#include        "dt.h"
#include        "cpp.h"
#include        "outbuf.h"
#include        "scope.h"

static char __file__[] = __FILE__;      /* for tassert.h                */
#include        "tassert.h"

#define outtype(tym)    outword(tym)    /* fix this later               */

#if TX86
bool init_staticctor;   /* true if this is a static initializer */
#endif

STATIC elem * initelem(type *, DtBuilder&, Symbol *,targ_size_t);
STATIC elem * initstruct(type *, DtBuilder&, Symbol *,targ_size_t);
STATIC elem * initarray(type *, DtBuilder&, Symbol *,targ_size_t);
STATIC elem * elemtodt(symbol *, DtBuilder&, elem *, targ_size_t);
STATIC int init_arraywithctor(symbol *);
STATIC Symbol * init_localstatic(elem **peinit,symbol *s);
STATIC elem * init_sets(symbol *sauto,symbol *s);
STATIC Symbol * init_staticflag(symbol *s);

STATIC int endofarray(void);
STATIC size_t getArrayIndex(size_t i, size_t dim, char unknown);
STATIC void initializer(symbol *);
STATIC elem * dyn_init(symbol *);
STATIC Symbol *init_alloca();


// Decide to put typeinfo symbols in code segment
#define CSTABLES        (config.memmodel & 2)
#if 1
#define CSFL            FLcsdata
#define CSMTY           mTYcs
#else
#define CSFL            FLfardata
#define CSMTY           mTYfar
#endif
+/

extern __gshared targ_size_t dsout;   // # of bytes actually output to data
                                // segment, used to pad for alignment

/*********************** DtArray ***********************/

struct DtArray
{
    dt_t **data;
    size_t dim;

    void dtor()
    {
        if (data)
        {   free(data);
            data = null;
        }
    }

    void ensureSize(size_t i);

    void join(DtBuilder dtb, size_t elemsize, size_t dim, char unknown);
}

/+
void DtArray.ensureSize(size_t i)
{
    if (i >= dim)
    {   size_t newdim;
        newdim = (i + 1) * 2;
        data = (dt_t **)realloc(data, newdim * sizeof(dt_t *));
        if (!data)
            err_nomem();
        memset(data + dim, 0, (newdim - dim) * sizeof(dt_t *));
        dim = newdim;
    }
    else if (data[i])
    {
        dt_free(data[i]);
        data[i] = null;
    }
}

/********************************
 * Put all the initializers together into one.
 */

void DtArray.join(DtBuilder& dtb, size_t elemsize, size_t dim, char unknown)
{
    size_t i = 0;
    for (size_t j = 0; j < this.dim; j++)
    {
        if (data[j])
        {
            if (j != i)
            {
                dtb.nzeros(elemsize * (j - i));
                dsout += elemsize * (j - 1);
            }
            dtb.cat(data[j]);
            i = j + 1;
        }
    }

    if (i < dim && !unknown)            // need to pad remainder with 0
    {
        dtb.nzeros(elemsize * (dim - i));
        dsout += elemsize * (dim - 1);
    }
}


/********************************************************************/


/**********************************************
 * Output data definition record. Parse initializer if there is one.
 * Calculate symbol number.
 * Watch out for forward referenced structs and unions.
 */

void datadef(symbol *s)
{   type *t;
    SYMIDX marksi;

    assert(s);
    symbol_debug(s);
    t = s.Stype;
    assert(t);
    type_debug(t);
    //debug(debugy && dbg_printf("datadef('%s')\n",s.Sident));
    //printf("datadef('%s')\n",s.Sident);
    //symbol_print(s);

    if (CPP)
        nspace_checkEnclosing(s);

        switch (s.Sclass)
        {
            case SCauto:
            case SCregister:
#if TX86
                if (s.Stype.Tty & mTYcs)
                {   s.Sclass = SCstatic;
                    goto Lstatic;
                }
#endif
                symbol_add(s);

                marksi = globsym.top;
                initializer(s);         /* followed by initializer      */

                /* Initializing a const& with a temporary means we
                 * delay calling the destructor on the temporary until
                 * the end of the scope.
                 */
                SYMIDX endsi = globsym.top;
                if (marksi < endsi &&
                    tyref(t.Tty) && t.Tnext.Tty & mTYconst)
                    endsi--;

                func_expadddtors(&curblock.Belem, marksi, endsi, 0, true);
                break;
            case SCunde:
                assert(errcnt);         // only happens on errors
            case SCglobal:
                if (s.Sscope &&
                    s.Sscope.Sclass != SCstruct &&
                    s.Sscope.Sclass != SCnamespace)
                    s.Sclass = SCcomdat;
            case SCstatic:
            case SCcomdat:
            Lstatic:
                if (type_isvla(s.Stype))
                    synerr(EM_no_vla);          // can't be a VLA
                else
                    initializer(s);             // followed by initializer
#if SYMDEB_CODEVIEW
                if (s.Sclass == SCstatic && funcsym_p) // local static?
                    // So debug data appears twice (sigh) to be
                    // MS bug compatible
                    symbol_add(s);
#endif
                break;
            case SCregpar:
            case SCparameter:
            case SCfastpar:
                symbol_add(s);
                break;
            case SCextern:
                if (type_isvla(s.Stype))
                    synerr(EM_no_vla);          // can't be a VLA
#if TX86
                if (s.Stype.Tty & mTYfar)
                    s.Sfl = FLfardata;
                else
#endif
                    s.Sfl = FLextern;
                break;
            default:
                symbol_print(s);
                assert(0);
        }
}

/************************************
 * Provide initializer for Symbol s.
 * If statically initialized, output data directly to output
 * file. If dynamic, store init tree in the symbol table.
 * Take care of external references.
 */

STATIC void initializer(symbol *s)
{ type *t;
  tym_t ty;
  enum SC sclass;
  Symbol *sauto;
  elem *einit;                          /* for dynamic initializers     */
  Symbol *sinit;                        /* Symbol to pass to initelem   */
  Symbol *si;                           /* Symbol for local statics     */
  Classsym *classsymsave;
  SYMIDX marksisave;
  char localstatic;
  int paren = false;

  assert(s);

  //printf("initializer('%s')\n", s.Sident);
  //symbol_print(s);

  /* Allow void through */
  /*assert(tybasic(s.Stype.Tty));*/   /* variables only               */

  t = s.Stype;
  assert(t);
  ty = tybasic(t.Tty);                 /* type of data                 */
  sclass = (enum SC) s.Sclass;         // storage class
  if (CPP)
  {
    localstatic = ((sclass == SCstatic || sclass == SCglobal || sclass == SCcomdat)
        && level > 0);
    scope_pushclass((Classsym *)s.Sscope);
    classsymsave = pstate.STclasssym;
    if (s.Sscope && s.Sscope.Sclass == SCstruct)
        // Go into scope of class that s is a member of
        pstate.STclasssym = (Classsym *)s.Sscope;

    // Remember top of symbol table so we can see if any temporaries are
    // generated during initialization, so we can add in destructors for
    // them.
    marksisave = pstate.STmarksi;
    pstate.STmarksi = globsym.top;
  }

    if (CPP && tok.TKval == TKlpar && !tyaggregate(ty))
        paren = true;
    else
    if (tok.TKval != TKeq)              /* if no initializer            */
    {
        switch (sclass)
        {   case SCglobal:
            case SCcomdat:
                sclass = SCglobal;
                s.Sclass = sclass;
                if (s.Sdt)
                    return;
                init_common(s);
                goto Ls;

            case SCstatic:
                /* Need to handle:
                 *      static int i;
                 *      int *p = &i;
                 *      static int i = 3;
                 */
                if (!funcsym_p)         // no local statics
                {
                    if (!CPP)
                    {   s.Sclass = SCextern;
                        s.Sfl = FLextern;
                        s.Sflags |= SFLwasstatic;
                        nwc_addstatic(s);
                        return;
                    }
                    else
                    {   type *tr = type_arrayroot(t);
                        if (tybasic(tr.Tty) == TYstruct)
                            template_instantiate_forward(tr.Ttag);
                        if (tybasic(tr.Tty) != TYstruct ||
                            ((tr.Ttag.Sstruct.Sflags & STRanyctor) == 0 &&
                             tr.Ttag.Sstruct.Sdtor == null))
                        {
                            s.Sclass = SCextern;
                            s.Sfl = FLextern;
                            s.Sflags |= SFLwasstatic;
                            nwc_addstatic(s);
                            goto cret;
                        }
                    }
                }
                {
                scope dtb = new DtBuilder();
                dtb.nzeros(type_size(t));
                dsout += type_size(t);
                assert(!s.Sdt);
                s.Sdt = dtb.finish();
                }
            Ls: ;
                if (CPP && !localstatic)
                {
                    init_staticctor = true;
                }
        }
        if (t.Tflags & TFsizeunknown)
        {
            if (tybasic(t.Tty) == TYstruct)
            {   Classsym *stag = t.Ttag;
                template_instantiate_forward(stag);
                if (stag.Stype.Tflags & TFsizeunknown)
                    synerr(EM_unknown_size,stag.Sident); // size of %s is not known
            }
            else if (tybasic(t.Tty) == TYarray)
                synerr(EM_unknown_size,"array");        // size of %s is not known
            t.Tflags &= ~TFsizeunknown;
        }
        if (tybasic(t.Tty) == TYarray && type_isvla(t))
        {
            elem *e;
            elem *en;

            e = type_vla_fix(&s.Stype);
            t = s.Stype;
            block_appendexp(curblock, e);

            // Call alloca() to allocate the variable length array
            type *troot = type_arrayroot(t);
            Symbol *salloca = init_alloca();

            e = el_var(s);
            e = arraytoptr(e);
            en = el_typesize(t);
            en = el_bint(OPcall, salloca.Stype.Tnext, el_var(salloca), en);
            en = cast(en, e.ET);
            e = el_bint(OPeq, e.ET, e, en);
            block_appendexp(curblock, e);
        }
        if (!CPP)
            return;
        if (tyref(ty) ||
            (t.Tty & mTYconst &&
             tybasic(type_arrayroot(t).Tty) != TYstruct &&
             // MSC allows static const x;
             (ANSI || sclass != SCstatic)))
        {
            cpperr(EM_const_needs_init,s.Sident);      // uninitialized reference
        }
        if (tybasic(type_arrayroot(t).Tty) == TYstruct)
        {   list_t arglist;

            /* Get list of arguments to constructor     */
            arglist = null;
            if (tok.TKval == TKlpar)
            {   stoken();
                getarglist(&arglist);
                chktok(TKrpar,EM_rpar);
            }

            init_constructor(s,t,arglist,0,1,null);
        }
        init_staticctor = false;
        goto cret;
  }

  stoken();                             /* skip over '='                */
  s.Sflags |= SFLimplem;               /* seen implementation          */

    if (CPP)
    {
        if (ty == TYstruct)
        {
            Classsym *stag = t.Ttag;

            template_instantiate_forward(stag);
            if (stag.Sstruct.Sflags & STRanyctor)
            {
                // If { } and we can get by without the generated constructor
                if (tok.TKval == TKlcur &&
                    cpp_ctor(stag) != 1 &&
                    (!stag.Sstruct.Sctor ||
                     stag.Sstruct.Sctor.Sfunc.Fflags & Fgen))
                {
                    ;
                }
                else
                {
                    if (sclass == SCstatic || sclass == SCglobal)
                    {
                        scope dtb = new DtBuilder();
                        dtb.nzeros(type_size(t));
                        dsout += type_size(t);
                        s.Sdt = dtb.finish();
                        if (!localstatic)
                            init_staticctor = true;
                    }

                    list_t arglist = null;
                    list_append(&arglist,assign_exp());
                    init_constructor(s,t,arglist,0,0x21,null);
                    init_staticctor = false;
                    goto cret;
                }
            }
        }

        if (ty == TYarray && init_arraywithctor(s))
            goto cret;
    }

    einit = null;
    sauto = null;
    sinit = s;
    if ((sclass == SCauto || sclass == SCregister) && tyaggregate(ty) &&
        (tok.TKval == TKlcur || ty == TYarray))
    {   /* Auto aggregate initializer. Create a static copy of it, and  */
        /* copy it into the auto at runtime.                            */
        sauto = s;
        s = symbol_generate(SCstatic,t);
        if (CPP)
        {
            s.Sflags |= SFLnodtor;     // don't add dtors in later
        }
        s.Sflags |= SFLimplem;         // seen implementation
    }

    // If array of unknown size
    if (ty == TYarray && t.Tflags & TFsizeunknown)
    {   char bracket = 0;

        /* Take care of char a[]="  ";          */
#define isstring(t) (tok.TKval == TKstring && tyintegral((t).Tnext.Tty) && tysize((t).Tnext.Tty) == _tysize[tok.TKty])

        if (tok.TKval == TKstring || tok.TKval == TKlpar)
        {   elem *e;

        Lstring:
            e = arraytoptr(CPP ? assign_exp() : const_exp());
            e = poptelem(e);
            if (e.Eoper == OPstring && tyintegral(t.Tnext.Tty) && tysize((t).Tnext.Tty) == tysize(e.ET.Tnext.Tty))
            {
                type_setdim(&s.Stype,e.EV.ss.Vstrlen / type_size(s.Stype.Tnext));   /* we have determined its size  */
                assert(s.Sdt == null);
                scope dtb = new DtBuilder();
                dtb.nbytes(e.EV.ss.Vstrlen,e.EV.ss.Vstring);
                s.Sdt = dtb.finish();
                el_free(e);
                t = s.Stype;
            }
            else
            {
                e = typechk(e,t.Tnext);
                e = poptelem(e);
                t = type_setdim(&s.Stype,1);
                scope dtb = new DtBuilder();
                einit = elemtodt(s,dtb,e,0);
                assert(!s.Sdt);
                s.Sdt = dtb.finish();
            }
            if (bracket)
                chktok(TKrcur,EM_rcur);
            goto ret;
        }

        if (tok.TKval == TKlcur)                /* if left curly                */
        {   targ_size_t elemsize,dim;
            size_t i;

            stoken();

            /* Take care of char a[]={"  "};    */
            if (isstring(t))
            {   bracket = 1;
                goto Lstring;
            }

            DtArray dta;

            elemsize = type_size(t.Tnext);
            if (CPP &&
                sinit == s &&
                !localstatic &&
                (sinit.Sclass == SCglobal || sinit.Sclass == SCstatic))
                init_staticctor = true;
            dim = 0;                    // # of elements that we find
            i = 0;
            do
            {
                i = getArrayIndex(i, 0, 1);
                dta.ensureSize(i);
                scope dtb = new DtBuilder();
                einit = el_combine(einit,initelem(t.Tnext,dtb,sinit,i * elemsize));
                dta.data[i] = dtb.finish();
                i++;
                if (i > dim)
                    dim = i;
            } while (!endofarray());
            scope dtb = new DtBuilder();
            dta.join(dtb, elemsize, 0, 1);
            s.Sdt = dtb.finish();
            t = type_setdim(&s.Stype,dim);     // we have determined its size
            chktok(TKrcur,EM_rcur);             // end with a right curly

            if (CPP)
            {
                si = null;
                if (localstatic)
                    si = init_localstatic(&einit,s);

                init_staticctor = true;
                init_constructor(s,t,null,0,0x21,si);   /* call destructor, if any */
                init_staticctor = false;
            }
            dta.dtor();
            goto ret;
        }

        /* Only 1 element in array */
        t = type_setdim(&s.Stype,1);
    }

    // Figure out if we need a bracketed initializer list

    if (tok.TKval != TKlcur &&
        (
         (!CPP && ty == TYstruct && s.Sclass != SCauto && s.Sclass != SCregister) ||
         (ty == TYarray && t.Tdim > 1 && !isstring(t))
        )
     )
        synerr(EM_lcur_exp);            // left curly expected

    switch (s.Sclass)
    {
        case SCauto:
        case SCregister:
            einit = dyn_init(s);        /* dynamic initializer          */
            elem_debug(einit);
            break;

        case SCstatic:
        case SCglobal:
            if (!CPP)
            {
                scope dtb = new DtBuilder();
                initelem(t,dtb,s,0);                // static initializer
                assert(!s.Sdt);
                s.Sdt = dtb.finish();
                break;
            }
        case SCcomdat:
            // No symbols that get this far have constructors
            assert(CPP);
            if (sinit == s && !localstatic)
                init_staticctor = true;
            scope dtb = new DtBuilder();
            if (ty == TYstruct && tok.TKval != TKlcur)
            {   elem *e;

                e = assign_exp();
                e = typechk(e,t);
                e = poptelem3(e);
                einit = elemtodt(s,dtb,e,0);
            }
            else
                einit = initelem(t,dtb,sinit,0);
            assert(!s.Sdt);
            s.Sdt = dtb.finish();

            /* Handle initialization of local statics   */
            si = null;
            if (localstatic)
                si = init_localstatic(&einit,s);
            else if (init_staticctor && einit)
            {
                list_append(&constructor_list,einit);
                einit = null;
            }

            init_constructor(s,t,null,0,0x41,si);       /* call destructor, if any */
            init_staticctor = false;
            break;
        case SCextern:
        case SCunde:
        case SCparameter:
        case SCregpar:
        case SCfastpar:
        case SCtypedef:
        default:
            symbol_print(s);
            assert(0);
    }
    if (CPP && paren)
        chktok(TKrpar,EM_rpar);

ret:
    if (sauto)                          /* if auto aggregate initializer */
    {   elem *e;

        type_settype(&sauto.Stype,t);
        e = init_sets(sauto,s);
        if (config.flags3 & CFG3eh)
        {   // The following code is equivalent to that found in
            // cpp_constructor(). There is no constructor to initialize
            // sauto, so we hang the EH info off of the OPstreq.

            if (ty == TYstruct)
            {   Classsym *stag = t.Ttag;

                if (stag.Sstruct.Sdtor && pointertype == stag.Sstruct.ptrtype)
                    e = el_ctor(cpp_fixptrtype(el_ptr(sauto),t),e,n2_createprimdtor(stag));
            }
            else if (ty == TYarray)
            {   type *tclass;

                tclass = type_arrayroot(t);
                if (type_struct(tclass))
                {   Classsym *stag = tclass.Ttag;

                    if (stag.Sstruct.Sdtor && pointertype == stag.Sstruct.ptrtype)
                    {   elem *enelems;

                        enelems = el_nelems(t);
                        assert(enelems.Eoper == OPconst);
                        e = el_ctor(cpp_fixptrtype(el_ptr(sauto),tclass),e,
                                n2_vecdtor(stag,enelems));
                        el_free(enelems);
                    }
                }
            }
        }
        block_appendexp(curblock, e);
        symbol_keep(s);
        s = sauto;
    }
    block_appendexp(curblock, einit);           // any dynamic stuff
    if (CPP)
    {
        if (einit)
            block_initvar(s);           // initializer for s in this block
cret:
        pstate.STmarksi = marksisave;
        pstate.STclasssym = classsymsave;
        scope_pop();
    }
}

/*************************************
 * Create typeinfo data for a struct.
 */

STATIC void init_typeinfo_struct(DtBuilder& dtb, Classsym *stag)
{
    int nbases;
    baseclass_t *b;
    struct_t *st;
    int i;
    int flags;

    char c = 2;
    dtb.nbytes(1,&c);

    // Compute number of bases
    nbases = 0;
    class_debug(stag);
    st = stag.Sstruct;
    for (b = st.Svirtbase; b; b = b.BCnext)
        if (!(b.BCflags & BCFprivate))
            nbases++;
    for (b = st.Sbase; b; b = b.BCnext)
        if (!(b.BCflags & (BCFprivate | BCFvirtual)))
            nbases++;
    dtb.nbytes(2,(char *)&nbases);

    // Put out the base class info for each class
    flags = BCFprivate;
    b = st.Svirtbase;
    for (i = 0; i < 2; i++)
    {
        for (; b; b = b.BCnext)
        {   if (!(b.BCflags & flags))
            {   Symbol *s;

                // Put out offset to base class
                dtb.nbytes(_tysize[TYint],(char *)&b.BCoffset);

                // Put out pointer to base class type info
                s = init_typeinfo_data(b.BCbase.Stype);
                dtb.xoff(s,0,pointertype);
            }
        }
        flags |= BCFvirtual;
        b = st.Sbase;
    }
}

/**********************************
 * Create a symbol representing a type.
 * The symbol will be unique for each type, suitable for both RTTI and
 * exception handling type matching.
 */

symbol *init_typeinfo_data(type *ptype)
{
    Symbol *s;
    char *id;
    enum FL fl;
    type *t = null;

    fl = CSTABLES ? CSFL : FLdatseg;
    // Create the basic mangled name
    if (!ptype)                 // if ... (any type)
    {
        id = "__tiX";           // this symbol defined by the runtime
    }
    else
    {
        type_debug(ptype);

        // Since T, const T, T& and const T& are all considered the same,
        // remove any & and const.
        if (tyref(ptype.Tty))
            ptype = ptype.Tnext;
        t = type_copy(ptype);
        t.Tty &= ~(mTYconst | mTYvolatile);    // remove volatile too
        t.Tcount++;

        id = cpp_typetostring( t, "__ti" );
    }

    // Now we have the symbol name.
    // Type names appear in global symbol table
    s = scope_search(id, SCTglobal);

    // If it is not already there, create a new one
    if (!s)
    {   tym_t ty;
        scope dtb = new DtBuilder();

        s = scope_define(id, SCTglobal,SCcomdat);       // create the symbol
        s.Ssequence = 0;
        s.Stype = tstypes[TYchar];
        s.Stype.Tcount++;
        if (CSTABLES)
            type_setty(&s.Stype,s.Stype.Tty | CSMTY);

        if (!ptype)
        {   // Generate reference to extern
            goto Lextern;
        }
        else if ((ty = tybasic(ptype.Tty)) == TYstruct)
        {   // Generate:
            //  2, type-info, name
            init_typeinfo_struct(dtb,ptype.Ttag);
            s.Sfl = fl;
            goto Lname;
        }
        else if (typtr(ty))
        {   // Generate:
            //  1, flags, ptr-to-next, name
            char data[2];
            Symbol *sn;
            type *tn;

            tn = ptype.Tnext;
            data[0] = 1;                        // indicate a pointer type
            data[1] = ty | (tn.Tty & (mTYvolatile | mTYconst));
            dtb.nbytes(2,data);
            sn = init_typeinfo_data(tn);
            dtb.xoff(sn,0,pointertype);
            s.Sfl = fl;
          Lname:
            // Append RTTI human readable type name
            Outbuffer buf;
            char *name = type_tostring(&buf,t);
            size_t len = buf.size();
            if (name[len - 1] == ' ')           // cut off trailing ' '
            {   name[len - 1] = 0;
                len--;
            }
            dtb.nbytes(len + 1,name);
            s.Sdt = dtb.finish();
        }
        else if (ty == TYvoid)
        {   // Generate reference to extern
          Lextern:
            s.Sclass = SCextern;
#if TX86
            if (s.Stype.Tty & mTYcs)
                s.Sfl = FLcsdata;
            else
#endif
                s.Sfl = FLextern;
        }
        else
        {   // Generate:
#if 1
            // 0, name
            char data = 0;
            dtb.nbytes(1,&data);
            s.Sfl = fl;
            goto Lname;
#else
            //  0 (a common block)
            s.Sclass = SCglobal;
            init_common(s);
#endif
        }
    }
    if (t)
        type_free(t);
    symbol_debug(s);
    return s;
}

/**************************************
 * Create and initialize an instance of Type_info.
 * Input:
 *      ptype   type for which we need to create an object
 * Returns:
 *      Symbol for created object
 *      null for error
 */

Symbol *init_typeinfo(type *ptype)
{
    Symbol *s;
    char *id;
    Classsym *srtti;
    type *t;

    srtti = rtti_typeinfo();
    if (!srtti)
        return null;

    type_debug(ptype);

    // C++ 5.2.8-4, -5 Ignore top level reference and cv qualifiers
    t = ptype;
    if (tyref(t.Tty))
        t = t.Tnext;
    t.Tcount++;
    type_setcv(&t, 0);

    id = cpp_typetostring(t, "__rtti");
    type_free(t);

    // Now we have the symbol name.
    // Type names appear in global symbol table
    s = scope_search(id, SCTglobal);

    // If it is not already there, create a new one
    if (!s)
    {
        tym_t pty;
        struct_t *st;

        s = scope_define(id, SCTglobal,SCcomdat);       // create the symbol
        s.Ssequence = 0;
        s.Stype = srtti.Stype;
        s.Stype.Tcount++;
        type_setmangle(&s.Stype,mTYman_c);
        if (CSTABLES)
            type_setty(&s.Stype,s.Stype.Tty | CSMTY);

        st = srtti.Sstruct;
        pty = st.ptrtype;

        // Create an initializer for the symbol.

        // The first entry is a pointer to the vtbl[]
        assert(st.Svptr.Smemoff == 0);        // should be first member
        enum_SC scvtbl = cast(enum_SC) (config.flags2 & CFG2comdat) ? SCcomdat :
             (st.Sflags & STRvtblext) ? SCextern : SCstatic;
        n2_genvtbl(srtti,scvtbl,0);
        scope dtb = new DtBuilder();
        dtb.xoff(st.Svtbl,0,pty);

        // The second is the pdata
        Symbol *sti = init_typeinfo_data(ptype);
        dtb.xoff(sti,0,pty);
        s.Sdt = dtb.finish();
        s.Sfl = CSTABLES ? CSFL : FLdatseg;
    }
    symbol_debug(s);
    return s;
}

/**************************
 * Initialize Symbol with expression e.
 */

void init_sym(Symbol *s,elem *e)
{
    e = poptelem(e);
    scope dtb = new DtBuilder();
    e = elemtodt(s,dtb,e,0);
    assert(e == null);
    assert(!s.Sdt);
    s.Sdt = dtb.finish();
}
+/

/*********************************
 * Read in a dynamic initializer for Symbol s.
 * Output the assignment expression.
 */

//private
 elem * dyn_init(Symbol *s)
{   elem* e,e1,e2;
    type *t;
    type *tv;
    tym_t ty;

    //printf("dyn_init('%s')\n", s.Sident);
    //symbol_debug(s);
    t = s.Stype;
    assert(t);
    if (tok.TKval == TKlcur)    /* could be { initializer }     */
    {   stoken();
        e = dyn_init(s);
        chktok(TKrcur,EM_rcur);
    }
    else
    {
        //type_debug(t);
        e2 = poptelem(arraytoptr(assign_exp()));
        ty = t.Tty;
        if (ty & mTYconst && e2.Eoper == OPconst)
        {   s.Sflags |= SFLvalue;
            tv = t;
            if (tyref(ty))
                tv = t.Tnext;
            s.Svalue = poptelem2(typechk(el_copytree(e2),tv));
            assert(s.Svalue.Eoper == OPconst);
        }
        e1 = el_var(s);
        if (tyref(ty))
        {
            t = newpointer_share(t.Tnext);
            e1 = el_settype(e1,t);
            if (tybasic(t.Tnext.Tty) == TYarray &&
                e2.ET.Tty == TYnptr &&
                typematch(t.Tnext.Tnext, e2.ET.Tnext, 0))
            {
                goto L1;
            }
        }
        e2 = typechk(e2,s.Stype);
      L1:
        e = el_bint(OPeq,t,e1,e2);
        e = addlinnum(e);
        if (tyaggregate(ty))
        {   e.Eoper = OPstreq;
            if (config.flags3 & CFG3eh && tybasic(ty) == TYstruct)
            {   Classsym *stag = t.Ttag;

                if (stag.Sstruct.Sdtor && pointertype == stag.Sstruct.ptrtype)
                    e = el_ctor(cpp_fixptrtype(el_ptr(s),t),e,n2_createprimdtor(stag));
            }
        }
        //elem_debug(e);
    }
    return e;
}

/*******************************
 * Parse closing bracket of initializer list.
 */

//private
 void init_closebrack(int brack)
{
    if (brack)                          /* if expecting closing bracket */
    {   if (tok.TKval == TKcomma)
            stoken();
        if (tok.TKval != TKrcur)
        {   synerr(EM_rcur);            // right bracket expected
            panic(TKrcur);
        }
        stoken();
    }
}

/*********************************
 * Parse end of array.
 */

//private
 int endofarray()
{
    if (tok.TKval != TKcomma)
        return 1;
    stoken();                           /* skip over comma      */
    return (tok.TKval == TKrcur);               /* {A,B,C,} case        */
}

/*********************************
 * Return index of initializer.
 */

//private
 size_t getArrayIndex(size_t i, size_t dim, char unknown)
{
    // C99 6.7.8
    if (tok.TKval == TKlbra)    // [ constant-expression ]
    {   // If designator
        targ_size_t index;

        stoken();
        index = cast(uint)msc_getnum();  // BUG: fix truncation
        if (cast(int)index < 0 || (config.ansi_c && index == 0) ||
            (!unknown && index >= dim))
        {
            synerr(EM_array_dim, cast(int)index);
            index = dim - 1;
        }
        chktok(TKrbra, EM_rbra);        // closing ']'
        i = index;
        if (tok.TKval == TKeq)
        {   stoken();
            if (tok.TKval == TKdot || tok.TKval == TKlbra)
            {
                synerr(EM_exp);
            }
        }
    }
    return i;
}

/*******************************
 * Read and write an initializer of type t.
 * Input:
 *      t       type of initializer
 *      pdt     where to store static part of initialization
 *      s       Symbol being initialized
 *      offset  from start of Symbol where initializer goes
 * Returns:
 *      elem to be used for dynamic part of initialization
 *      null = no dynamic part of initialization
 */

//private
 elem* initelem(type *t, DtBuilder dtb, Symbol *s, targ_size_t offset)
{   elem *e;

    //dbg_printf("+initelem()\n");
    assert(t);
    //type_debug(t);
    switch (tybasic(t.Tty))
    {
        case TYbool:
        case TYwchar_t:
        case TYchar:
        case TYschar:
        case TYuchar:
        case TYchar16:
        case TYshort:
        case TYushort:
        case TYint:
        case TYuint:
        case TYlong:
        case TYulong:
        case TYdchar:
        case TYllong:
        case TYullong:
        case TYfloat:
        case TYdouble:
        case TYdouble_alias:
        case TYldouble:
        case TYifloat:
        case TYidouble:
        case TYildouble:
        case TYcfloat:
        case TYcdouble:
        case TYcldouble:

        // TX86
        case TYnullptr:
        case TYnptr:
        case TYsptr:
        case TYcptr:
        case TYhptr:

        case TYfptr:
        case TYvptr:
        case TYenum:
        case TYref:
        case TYnref:
        case TYfref:
        case TYmemptr:
            if (tok.TKval == TKlcur)    /* could be { initializer }     */
            {   stoken();
                e = initelem(t,dtb,s,offset);
                chktok(TKrcur,EM_rcur);
            }
            else
            {
                e = arraytoptr(CPP ? assign_exp() : const_exp());
                e = typechk(e,t);
                if (e.Eoper != OPconst)
                    e = poptelem3(e);
                if (s && s.Stype.Tty & mTYconst &&
                    e.Eoper == OPconst && tyscalar(s.Stype.Tty))
                {   s.Sflags |= SFLvalue;
                    s.Svalue = el_copytree(e);
                }
                e = elemtodt(s,dtb,e,offset);
            }
            break;
        case TYstruct:
            e = initstruct(t,dtb,s,offset);
            break;
        case TYarray:
            e = initarray(t,dtb,s,offset);
            break;
        default:
            /* We could get these as a result of syntax errors  */
            e = null;
            stoken();
            break;                      /* just ingnore them            */
    }
    //dbg_printf("-initelem(): e = %p\n", e);
    return e;
}


/***************************
 * Read in initializer for a structure.
 * Watch out for bit fields.
 * Support union initializers by allowing initialization of the first
 * member of the union.
 * Input:
 *      pdt     where to store initializer list
 */

struct StructDesignator
{
    Symbol *smember;
    elem *exp;          // SCfield
    dt_t *dt;           // SCmember
}

//private
 elem* initstruct(type *t, DtBuilder dtb, Symbol *ss,targ_size_t offset)
{   elem *e;
    list_t sl;
    targ_size_t dsstart;
    targ_size_t soffset;                // offset into struct s
    targ_size_t tsize;
    int brack;
    //symbol *classsymsave;     // other compilers don't seem to do this
    elem *ei;
    elem *ec;
    Classsym *stag;
    int nmembers;
    int i;
    StructDesignator *sd;
    int designated;

    ei = null;
    dsstart = dsout;
    assert(t);
    //type_debug(t);
    if (tok.TKval == TKlcur)
    {   brack = true;
        stoken();
    }
    else
        brack = false;                  /* elements are not bracketed   */

    stag = t.Ttag;
    //printf("initstruct('%s')\n",stag.Sident);
    if (!brack && stag.Sstruct.Sflags & STRanyctor)
    {
        list_t arglist = null;
        list_append(&arglist, assign_exp());
        ei = init_constructor(ss, t, arglist, offset, 0x24, null);
        goto Ldone;
    }

    // Count up how many designators we'll need
    nmembers = 0;
    for (sl = stag.Sstruct.Sfldlst; sl; sl = list_next(sl))
    {   Symbol *s = list_symbol(sl);

        if (!s)
            continue;
        switch (s.Sclass)
        {
            case SCfield:
            case SCmember:
                nmembers++;
                break;

            default:
                break;
        }
    }

    // Allocate and clear array of designators
    sd = cast(StructDesignator *)alloca(StructDesignator.sizeof * nmembers);
    memset(sd, 0, StructDesignator.sizeof * nmembers);

    // Populate array of designators
    i = 0;
    for (sl = stag.Sstruct.Sfldlst; sl; sl = list_next(sl))
    {   Symbol *s = list_symbol(sl);

        switch (s.Sclass)
        {
            case SCfield:
            case SCmember:
                sd[i].smember = s;
                i++;
                break;

            default:
                break;
        }
    }

    e = null;
    soffset = cast(targ_size_t)-1;
    designated = 0;
    for (i = 0; 1; i++)
    {   Symbol *s;
        elem *e1;

        if (i == nmembers)
        {
            /* Handle case of:
             *  struct Foo { int quot; int rem; };
             *  struct Foo bar = { .rem  = 3, .quot =  7 };
             */
            if (!brack || !designated || soffset == -1 || tok.TKval != TKcomma)
                break;
            stoken();           // skip over separating comma
            if (tok.TKval != TKdot)
                break;
        }
        else
        {

            //printf("\tsd[%d] = '%s'\n", i, sd[i].smember.Sident);
            s = sd[i].smember;
            //symbol_debug(s);
        }

        designated = 0;
        if (tok.TKval == TKdot)
        {
          Ldesignator:
            // A designator follows
            stoken();
            if (tok.TKval != TKident)
            {
                synerr(EM_ident_exp);           // identifier expected
                continue;
            }

            for (i = 0; 1; i++)
            {
                if (i == nmembers)
                {   err_notamember(tok.TKid, stag);
                    return null;
                }
                if (strcmp(tok.TKid, &sd[i].smember.Sident[0]) == 0)
                {   s = sd[i].smember;
                    break;
                }
            }

            stoken();

            if (tok.TKval == TKeq)
                stoken();

            designated = 1;
            //printf("\tdesignator sd[%d] = '%s'\n", i, sd[i].smember.Sident);
        }
        else if (s.Sflags & SFLskipinit)
        {
            // The rest are the not-first union members
            continue;
        }
        //printf("  member '%s', offset x%lx\n",s.Sident,s.Smemoff);
        switch (s.Sclass)
        {
            case SCfield:
                if (!designated)
                {
                    if (!e && s.Smemoff == soffset)
                        continue;               // if not 1st member of union
                    if (soffset != -1)          // if not first
                    {   stoken();               // skip over separating comma
                        if (tok.TKval == TKrcur)
                            break;
                    }
                    if (tok.TKval == TKdot)
                        goto Ldesignator;
                }
                soffset = s.Smemoff;
                e = cast(elem *)1;
                e1 = CPP ? assign_exp() : const_exp();  // get an integer
                e1 = typechk(e1,s.Stype);
                if (!e1)
                    return null;                // error somewhere
                el_free(sd[i].exp);
                sd[i].exp = poptelem(e1);       // fold out constants
                break;

            case SCmember:
                if (!designated)
                {
                    if (s.Smemoff == soffset)
                        continue;               // must be a union
                    if (soffset != -1)          // if not first
                    {   stoken();               // skip over separating comma
                        if (tok.TKval == TKrcur)
                            break;
                    }
                    if (tok.TKval == TKdot)
                        goto Ldesignator;
                }
                soffset = s.Smemoff;
                dt_free(sd[i].dt);
                sd[i].dt = null;
                scope dtb2 = new DtBuilder();
                sd[i].exp = initelem(s.Stype,dtb2,ss,offset + soffset);
                sd[i].dt = dtb2.finish();
                break;

            default:
                assert(0);
        }

        if (tok.TKval != TKcomma)
            break;

        if (designated && !brack)
            break;
    }

    e = null;
    soffset = cast(targ_size_t)-1;
    dsout = dsstart;
    for (i = 0; i < nmembers; i++)
    {
        Symbol *s = sd[i].smember;
        uint ul;
        uint fieldmask;
        elem *e1;

        switch (s.Sclass)
        {
            case SCfield:
                if (e && s.Smemoff != soffset)
                {
                    uint n = soffset - (dsout - dsstart);
                    dtb.nzeros(n);
                    dsout += n;
                    e = poptelem(e);
                    ec = elemtodt(ss,dtb,e,offset + soffset);
                    ei = el_combine(ei,ec);
                    e = null;
                }
                soffset = s.Smemoff;
                e1 = sd[i].exp;
                if (!e1)
                    continue;
                if (!e)
                    e = el_longt(s.Stype,0);
                fieldmask = ~(~0 << s.Swidth);
                if (CPP)
                {
                    e1 = el_bint(OPand,e1.ET,e1,el_longt(e1.ET,fieldmask));
                    e1 = el_bint(OPshl,e1.ET,e1,el_longt(tstypes[TYint],s.Sbit));
                    if (e)
                    {
                        e = el_bint(OPand,e.ET,e,el_longt(e1.ET,~(fieldmask << s.Sbit)));
                        e = el_bint(OPor,e.ET,e,e1);
                    }
                    else
                        e = e1;
                }
                else
                {
                    if (!OTleaf(e1.Eoper))
                    {
                        e1 = poptelem2(e1);
                        if (!OTleaf(e1.Eoper))
                            synerr(EM_const_init);      // constant initializer expected
                    }
                    ul = e1.EV.Vulong;
                    el_free(e1);                // chuck the element
                    ul &= fieldmask;
                    ul <<= s.Sbit;             // shift into proper orientation

                    // Override existing initializer for this field
                    e.EV.Vulong &= ~(fieldmask << s.Sbit);

                    e.EV.Vulong |= ul;         // OR in new field
                }
                break;

            case SCmember:
                if (e)                  // if bit field
                {
                    uint n = soffset - (dsout - dsstart);
                    dtb.nzeros(n);
                    dsout += n;
                    e = poptelem(e);
                    ec = elemtodt(ss,dtb,e,offset + soffset);
                    ei = el_combine(ei,ec);
                    e = null;
                }

                if (sd[i].dt)
                {
                    soffset = s.Smemoff;
                    uint n = soffset - (dsout - dsstart);
                    dtb.nzeros(n);
                    dsout += n;
                    dtb.cat(sd[i].dt);
                    dsout += dt_size(sd[i].dt);
                }
                if (sd[i].exp)
                    ei = el_combine(ei,sd[i].exp);
                break;

            default:
                break;
        }
    }

    // if there is a bit field we still need to write out
    if (e)
    {
        e = poptelem(e);
        ec = elemtodt(ss,dtb,e,offset + soffset);
        ei = el_combine(ei,ec);
        e = null;
    }

Ldone:
    tsize = type_size(t);
    if (tsize > (dsout - dsstart))
    {
        uint n = tsize - (dsout - dsstart);
        dtb.nzeros(n);
        dsout += n;
    }
    init_closebrack(brack);
    //printf("-initstruct(): ei = %p\n", ei);
    return ei;
}


/*************************
 * Read and write an initializer for an array of type t.
 */

//private
 elem* initarray(type *t, DtBuilder dtb,Symbol *s,targ_size_t offset)
{
    char brack;
    targ_size_t dsstart,elemsize;
    targ_size_t tsize;
    char unknown;

    elem *e = null;

    if (tok.TKval == TKlcur)
    {
        brack = true;
        stoken();
    }
    else
        brack = false;                  /* elements are not bracketed   */

    //printf("initarray(brack = %d, s = '%s')\n", brack, s.Sident);

    assert(tybasic(t.Tty) == TYarray);
    targ_size_t dim = t.Tdim;
    if (t.Tflags & TFsizeunknown)
    {   unknown = 1;
    }
    else
    {   unknown = 0;
        tsize = type_size(t);
    }

    // Take care of string initialization
    if (tok.TKval == TKstring && tyintegral(t.Tnext.Tty))
    {
        targ_size_t len;
        tym_t ty;

        char *mstring = combinestrings(&len, &ty);      // concatenate adjacent strings
        int ts = tysize(t.Tnext.Tty);
        if (ts == _tysize[ty])
        {
            if (unknown)
                tsize = len;

            // Lop off trailing 0 so 'char a[3]="abc";' works
            if (len - ts == tsize && (!CPP || !config.ansi_c))
                len -= ts;

            if (len > tsize)
            {   synerr(EM_2manyinits);  // string is too long
                len = tsize;
            }
            dtb.nbytes(len,mstring);
            dsout += len;
            dtb.nzeros(tsize - len);
            dsout += tsize - len;
            mem_free(mstring); // MEM_PH_FREE()
            goto Ldone;
        }
    }

    dsstart = dsout;
    /* Determine size of each array element     */
    //dbg_printf("Tty = x%x, Tdim = %d, size = %d, Tnext.Tty = x%x\n",
            //t.Tty,t.Tdim,tsize,t.Tnext.Tty);
    if (dim || unknown)
    {   DtArray dta;

        targ_uns i = 0;
        if (unknown)
        {
            elemsize = type_size(t.Tnext);
        }
        else
        {   elemsize = tsize / dim;
            assert(elemsize * dim == tsize);    /* no holes     */
        }
        do
        {
            i = getArrayIndex(i, dim, unknown);

            // Ensure dtarray[] is big enough
            dta.ensureSize(i);

            scope dtb2 = new DtBuilder();
            elem *ec = initelem(t.Tnext,dtb2,s,i * elemsize);
            dta.data[i] = dtb2.finish();
            e = el_combine(e,ec);
            i++;                        /* # of elements in array       */
            //dbg_printf("elemsize = %ld, i = %ld, dsout = %ld, dsstart = %ld, unknown = %d\n",
                //(long)elemsize,(long)i,(long)dsout,(long)dsstart,unknown);

            /* This assert can be tripped due to syntax errors in initelem */
//              assert(elemsize * i == dsout - dsstart || unknown || errcnt);
            if (i >= dim && !unknown)   // array is full, exit
                break;
        } while (!endofarray());

        dta.join(dtb, elemsize, dim, unknown);
        dta.dtor();
    }

Ldone:
    init_closebrack(brack);
    return e;
}


/***********************************
 * Convert from elem to dt.
 * Input:
 *      e       poptelem() already done
 * Returns:
 *      elem to be used for dynamic part of initialization
 *      null = no dynamic part of initialization, e is free'd
 */

//private
 elem* elemtodt(Symbol *s, DtBuilder dtb, elem *e, targ_size_t offset)
{
  char *p;
  tym_t ty;
  targ_size_t size;
  Symbol *sa;

Lagain:
  if (errcnt)                   /* if errors have occurred in source file */
        goto ret;               /* then forget about output file        */
  assert(e);
  ty = e.ET.Tty;
  assert(CPP || !tyaggregate(ty));
  switch (e.Eoper)
  {
    case OPrelconst:
        sa = e.EV.Vsym;
        if (!sa) return null;
    again:
        switch (sa.Sclass)
        {
            case SCanon:
                sa = sa.Sscope;
                goto again;
            case SCinline:
            case SCsinline:
            case SCeinline:
            case SCstatic:
            /*case SClocstat:*/
            case SCglobal:
            case SCextern:
            case SCcomdef:
            case SCcomdat:
                break;
            case SCauto:
            case SCparameter:
            case SCregister:
            case SCregpar:
            case SCfastpar:
                if (CPP)
                    goto Lexp;
                else
                    goto err;

            case SCunde:
                return null;
            default:
                debug WRclass(cast(enum_SC)sa.Sclass);
                assert(0);
        }
        ty = tym_conv(e.ET);
        dtb.xoff(sa,e.EV.Voffset,ty);
        dsout += tysize(ty);
        break;

    case OPstring:
        size = e.EV.Vstrlen;
        dtb.abytes(e.ET.Tty, e.EV.Voffset, size, e.EV.Vstring, 0);
        dsout += tysize(e.ET.Tty);
        break;

    case OPconst:
    {
        size = type_size(e.ET);
        switch (e.ET.Tty)
        {
            case TYfloat:
            case TYifloat:
                targ_float f = e.EV.Vfloat;
                p = cast(char *) &f;
                break;

            case TYdouble:
            case TYdouble_alias:
            case TYidouble:
                targ_double d = e.EV.Vdouble;
                p = cast(char *) &d;
                break;

            case TYcfloat:
                Complex_f fc = e.EV.Vcfloat;
                p = cast(char *) &fc;
                break;

            case TYcdouble:
                Complex_d dc = e.EV.Vcdouble;
                p = cast(char *) &dc;
                break;

            default:
                p = cast(char *) &e.EV;
                break;
        }
        dsout += size;
        dtb.nbytes(size,p);
        break;
    }

    default:
        e = poptelem4(e);               // try again to fold constants
        if (OTleaf(e.Eoper))
            goto Lagain;
    case OPvar:
    Lexp:
        if (!CPP)
            goto err;
        if (s)
        {
            // The expression is not constant.
            // Build initializer expression of the form:
            //  *(&s + offset) = e;
            elem *ev;
            type *t;

            s.Sflags |= SFLdyninit;
            ev = el_var(s);
            ev.EV.Voffset = offset;
            t = e.ET;
            assert(!tyref(t.Tty));
            el_settype(ev,t);
            e = el_bint(OPeq,t,ev,e);
            if (tyaggregate(t.Tty))
                e.Eoper = OPstreq;
            if (init_staticctor)
            {   // Evaluate it in the module constructor
                if (pstate.STinsizeof)
                    el_free(e);         // ignore - it's in a sizeof()
                else
                    list_append(&constructor_list,e);
                e = null;
            }
            dtb.nzeros(type_size(t)); // leave a hole for it
            dsout += type_size(t);
            goto ret2;
        }
        else
            goto err;                   // constant initializer expected
  }

ret:
    el_free(e);
    e = null;

ret2:
    return e;

err:
    debug elem_print(e);
    synerr(EM_const_init);      // constant initializer expected
    goto ret;
}


/*********************************
 * Construct static initializer for vtbl[].
 * Input:
 *      s_vtbl          Symbol for vtbl[]
 *      virtlist        list of mptrs which will initialize vtbl[]
 *      stag            class of Symbol we're generating a vtbl[] for
 *      srtti           class of complete Symbol
 */

void init_vtbl(Symbol *s_vtbl,list_t virtlist,Classsym *stag,Classsym *srtti)
{
    list_t lvf;
    targ_size_t size;
    tym_t fty;                          /* pointer to function type     */
    short offset;

    //symbol_debug(s_vtbl);
    //symbol_debug(stag);
    fty = LARGECODE ? TYfptr : TYnptr;
    cpp_getpredefined();                        /* get s_mptr           */
    debug assert(fty == s_mptr.Stype.Tty);
    fty = tybasic(fty);
    size = type_size(s_mptr.Stype);

    scope dtb = new DtBuilder();

    // Put in RTTI information
    if (config.flags3 & CFG3rtti)
    {   Symbol *s;

        //symbol_debug(srtti);
        s = init_typeinfo(srtti.Stype);
        if (s)
            dtb.xoff(s,0,srtti.Sstruct.ptrtype);
    }

    for (lvf = virtlist; lvf; lvf = list_next(lvf))
    {   Symbol *s;

        mptr_t *m = cast(mptr_t *) list_ptr(lvf);
        s = m.MPf;
        // Replace destructor call with scalar deleting destructor
        if (s.Sfunc.Fflags & Fdtor)
        {
            Classsym* stag2 = cast(Classsym *)s.Sscope;
            if (!stag.Sstruct.Sscaldeldtor)
                n2_createscaldeldtor(stag2);
            s = stag2.Sstruct.Sscaldeldtor;
        }
        if (m.MPd && !(s.Sfunc.Fflags & Fpure)) // if displacement from this
        {                                         // then a thunk is required
            Symbol *sthunk;
            targ_size_t d;

            d = m.MPd;
            sthunk = nwc_genthunk(s,d,-1,0);
            //symbol_keep(sthunk);
            /*dbg_printf("Adding %s to class %s\n",sthunk.Sident,stag.Sident);*/
            //n2_addfunctoclass(stag.Stype,sthunk);

            //m.MPf = sthunk;            // for possible other users
            //m.MPd = 0;

            s = sthunk;
        }

        // BUG: if covariant return type needs adjustment, build wrapper
        // for s here.
        //symbol_debug(s);
        /*dbg_printf("vtbl[] = %s\n",s.Sident);*/
        assert(s.Sfunc && tyfunc(s.Stype.Tty));

        if (m.MPflags & MPTRcovariant)
            synerr(EM_covariant, prettyident(m.MPf));

        // if 'pure' function, put a null in it's place in the vtbl[]
        if (s.Sfunc.Fflags & Fpure)
        {
            dtb.nzeros(_tysize[fty]);
            dsout += _tysize[fty];
        }
        else
        {
            /* Compute __mptr.f, the function pointer itself    */
            dtb.xoff(s,0,fty);
            dsout += _tysize[fty];

            /*dbg_printf(" tysize = %d, size = %d\n",
                _tysize[fty],size - 2 * SHORTSIZE);*/
            assert(_tysize[fty] == size);
        }
    }
    s_vtbl.Sdt = dtb.finish();

    /*dbg_printf("Tdim = %d\n",s_vtbl.Stype.Tdim);*/
}


/*********************************
 * Construct static initializer for vtbl[].
 */

void init_vbtbl(
        Symbol      *s_vbtbl,   // symbol for vbtbl[]
        baseclass_t *virtbase,  // list of virtual base classes
        Classsym    *stag,      // class of Symbol we're generating vbtbl[] for
        targ_size_t  vbptr_off) // offset of Svbptr from address point of class
{
    //dbg_printf("init_vbtbl(s_vbtbl='%s',stag='%s',vbptr_off=x%lx)\n",
    //  s_vbtbl.Sident,stag.Sident,(long)vbptr_off);

    // Guess number of slots
    uint dim = (1 + baseclass_nitems(virtbase)) * _tysize[TYint];   // extra slot at beginning

    // Allocate table
    char* pdata = cast(char *)mem_calloc(dim);

    // Fill the table
    uint size = 0;
    for (baseclass_t* b = virtbase; b; b = b.BCnext)
    {
        baseclass_t* b2 = baseclass_find(stag.Sstruct.Svirtbase,b.BCbase);
        assert(b2);
        //dbg_printf("b2='%s' vbtbloff = x%lx, size=x%x\n",b2.BCbase.Sident,(long)b2.vbtbloff,size);
        if (b2.BCvbtbloff + _tysize[TYint] > size)
        {   size = b2.BCvbtbloff + _tysize[TYint];
            if (size > dim)             // need to reallocate array
            {   pdata = cast(char *)mem_realloc(pdata,size);
                memset(pdata + dim,0,size - dim);
                dim = size;
            }
        }
        TOOFFSET(pdata + b.BCvbtbloff,b2.BCoffset - vbptr_off);
    }

    scope dtb = new DtBuilder();
    dtb.nbytes(size, pdata);
    s_vbtbl.Sdt = dtb.finish();
    mem_free(pdata);
}


/******************************
 * Handle constructor for s, if any.
 *      s               either a class or an array of classes.
 *      offset          offset from start of Symbol s
 *      dtorflag        0: do not do destructor
 *                      1: add in destructor too
 *                      2: do not do destructor, return elem created
 *                         also means s is a pointer to a struct
 *                         do not generate eh information
 *                      3: do not do destructor, return elem created
 *                         also means s is null, we are constructing
 *                         on the stack
 *                      4: do not do destructor, return elem created
 *                      0x20: do not allow explicit constructors
 *                      0x40: do destructors only
 *      sinit           For local statics, sinit is the Symbol that
 *                      creates a 'wrapper' around the construction/
 *                      destruction of the static.
 */

elem *init_constructor(Symbol *s,type *t,list_t arglist,
        targ_size_t offset,int dtorflag,Symbol *sinit)
{
    elem *e;
    elem *eptr;
    elem *enelems;
    type *tclass;
    Classsym *stag;

    enum : ubyte
    {
        DTRdtor  = 1,       // add in destructor
        DTRrete  = 2,       // return elem created
        DTRptre  = 4,       // s is a pointer to a struct
        DTRsnull = 8,       // s is null, (constructing on stack)
        DTRnoeh  = 0x10,    // do not generate eh information
    }
    __gshared immutable ubyte[5] translate =
    [ 0, DTRdtor, DTRrete|DTRptre|DTRnoeh, DTRrete|DTRsnull, DTRrete ];
    ubyte dflag = translate[dtorflag & 0x1F];

    //printf("init_constructor(s = '%s', level = %d)\n", s.Sident, level);
//    if (!(dflag & DTRsnull))
//        symbol_debug(s);
    tclass = type_arrayroot(t);
    if (tybasic(tclass.Tty) != TYstruct)
        return null;
    stag = tclass.Ttag;
//    symbol_debug(stag);

    if (dtorflag & 0x40)
        goto Ldtor;

    enelems = el_nelems(t);

    /* Look for special cases where we can dump temporary variables     */
    if (list_nitems(arglist) == 1 && !errcnt)
    {   Symbol *sa;
        Symbol *sctor;
        elem* e1,e2;
        elem* ec;
        elem *e1x;

        //printf("init_constructor(s = '%s', dtorflag = x%x)\n", s.Sident, dtorflag);
        //elem_print(list_elem(arglist));

        e = poptelem(list_elem(arglist));
        list_setelem(arglist, e);

        /* Look for (tmp = xxx),tmp                             */
        if (e.Eoper == OPcomma &&
            (e2 = e.EV.E2).Eoper == OPvar &&
            /* BUG: what if mTYconst? */
            type_struct(e2.ET) && e2.ET.Ttag == stag &&
            (sa = e2.EV.Vsym).Sclass == SCauto &&
            (e1 = e.EV.E1).Eoper == OPstreq &&
            el_match(e1.EV.E1,e2) &&
            !(dflag & DTRsnull))
        {
assert(0); // can't find any cases of this, must be an anachronism
            if (dflag & DTRptre)
            {
                el_free(e.EV.E2);
                el_free(e1.EV.E1);
                e.EV.E2 = el_unat(OPind,tclass,el_var(s));
                e1.EV.E1 = el_copytree(e.EV.E2);
            }
            else
            {
                e2.EV.Vsym = s;
                e2.EV.Voffset = offset;
                e1.EV.E1.EV.Vsym = s;
                e1.EV.E1.EV.Voffset = offset;
            }
        L3: // Discard the temporary sa
            sa.Sflags |= SFLnodtor;
            if (sa.Sflags & SFLfree)
            {   sa.Sflags &= ~SFLfree;
                symbol_keep(sa);
            }
            goto L2;
        }

        if (e.Eoper == OPcond && (dflag & (DTRdtor | DTRrete)) == DTRrete)
        {   type *t2;

            list_free(&arglist,FPNULL);
            list_append(&arglist,e.EV.E2.EV.E1);
            e.EV.E2.EV.E1 = init_constructor(s,t,arglist,offset,dtorflag & 0x1F,sinit);
            arglist = null;
            list_append(&arglist,e.EV.E2.EV.E2);
            e.EV.E2.EV.E2 = init_constructor(s,t,arglist,offset,dtorflag & 0x1F,sinit);
            assert(e.EV.E2.EV.E2);
            t2 = e.EV.E2.EV.E2.ET;
            el_settype(e.EV.E2,t2);
            el_settype(e,t2);
            e.EV.E2.EV.E1 = _cast(e.EV.E2.EV.E1,t2);
            return e;
        }

        if (e.Eoper == OPind)
        {
            e1x = e.EV.E1;
            if (e1x.Eoper == OPoffset)
                e1x = e1x.EV.E1;
            ec = null;
            if (e1x.Eoper == OPinfo)
            {   if (e1x.EV.E1.Eoper == OPctor &&
                    e1x.EV.E1.EV.E1.Eoper == OPrelconst)
                    ec = e1x.EV.E1.EV.E1;
                e1x = e1x.EV.E2;
            }

            if (e1x.Eoper == OPcall)
            {
                /* If argument is a call to a function that returns the
                 * struct as a hidden argument, replace the hidden argument
                 * with s, and let the function construct s.
                 */
                if (type_struct(e.ET) && e.ET.Ttag == stag)
                {   type *tf = e1x.EV.E1.ET;      /* function type        */
                    type *tret = tf.Tnext;     /* function return type */

                    //type_debug(tret);
                    if (exp2_retmethod(tf) == RET_STACK &&
                        tret.Ttag == stag)
                    {   elem *eh;

                        // Find hidden parameter, and set e1x to it
                        eh = exp2_gethidden(e1x);
                        if (eh.Eoper == OPrelconst &&
                            /* BUG: what if mTYconst? */
                            type_struct(eh.ET.Tnext) &&
                            eh.ET.Tnext.Ttag == stag &&
                            (sa = eh.EV.Vsym).Sclass == SCauto)
                        {
                            elem *es;
                            int result;

                            if (dflag & DTRsnull)
                            {   eh.Eoper = OPstrthis;
                                if (ec)
                                    ec.Eoper = OPstrthis;
                                e = list_elem(arglist);
                                if (e.EV.E1.Eoper == OPoffset)
                                    list_setelem(arglist,selecte1(e,e.EV.E1.ET));
                                goto L3;
                            }

                            es = (dflag & DTRptre) ? el_var(s) : el_ptr(s);
                            result = exp2_ptrconv(es.ET,eh.ET);
                            el_free(es);

                            if (result)         /* if compatible ptr types */
                            {
                                if (dflag & DTRptre)
                                    eh.Eoper = OPvar;
                                eh.EV.Vsym = s;
                                eh.EV.Voffset = offset;
                                if (ec)
                                {   ec.Eoper = eh.Eoper;
                                    ec.EV.Vsym = s;
                                    ec.EV.Voffset = offset;
                                }
                                goto L3;
                            }
                        }
                    }
                }
            }
            /* If argument is a call to a constructor for tclass        */
            if (e1x.Eoper == OPcall &&
                (
                (e1x.EV.E1.Eoper == OPvar &&
                 (sctor = e1x.EV.E1.EV.Vsym).Sfunc.Fclass == stag &&
                 sctor.Sfunc.Fflags & Fctor
                )
                        ||
                (e1x.EV.E1.Eoper == OPind &&
                 e1x.EV.E1.EV.E1.Eoper == OPvar &&
                 e1x.EV.E1.EV.E1.EV.Vsym.Sclass == SCextern &&
                 (sctor = e1x.EV.E1.EV.E1.EV.Vsym.Simport) != null &&
                 tyfunc(sctor.Stype.Tty) &&
                 sctor.Sfunc.Fclass == stag &&
                 sctor.Sfunc.Fflags & Fctor
                )
                )
               )
            {
                // Find ethis, which is the last parameter pushed
                do
                    e1x = e1x.EV.E2;
                while (e1x.Eoper == OPparam);
                if (e1x.Eoper == OPrelconst)
                {
                    //elem_debug(e1x);
                    assert(e1x.EV.Vsym.Sclass == SCauto);
                    debug assert(enelems == null);
                    /* This ctor is sufficient. Discard the temporary,  */
                    /* putting s in instead                             */
                    sa = e1x.EV.Vsym;
                    sa.Sflags |= SFLnodtor;
                    if (sa.Sflags & SFLfree)
                    {   sa.Sflags &= ~SFLfree;
                        symbol_keep(sa);
                    }
                    if (dflag & DTRsnull)
                        e1x.Eoper = OPstrthis;
                    else
                    {
                        if (dflag & DTRptre)
                            e1x.Eoper = OPvar;
                        e1x.EV.Vsym = s;
                        e1x.EV.Voffset = offset;
                    }
                    if (ec)
                    {   ec.Eoper = e1x.Eoper;
                        ec.EV.Vsym = e1x.EV.Vsym;
                        ec.EV.Voffset = e1x.EV.Voffset;
                    }
                L2: e = list_elem(arglist);
                    list_free(&arglist,FPNULL);

                    /* Convert to pointer to struct     */
                    if (dflag & DTRptre)
                    {   type *tret;

                        tret = newpointer(e.ET);
                        if (!I32)
                        {   tret.Tty = TYfptr;
                            if (e.Eoper == OPind &&
                                e.EV.E1.Eoper == OPoffset)
                            {
                                e = selecte1(selecte1(e,tret),tret);
                                goto L1;
                            }
                        }
                        e = el_unat(OPaddr,tret,e);
                    }
                    else if (dflag & DTRsnull)
                    {
                        e = selecte1(e,e.EV.E1.ET);
                    }

                    goto L1;
                }
            }
        }
    }

    final switch (dflag & (DTRptre | DTRsnull))
    {   case DTRptre:
            eptr = el_var(s);
            break;
        case DTRsnull:
            eptr = el_longt(newpointer(tclass),0L);
            eptr.Eoper = OPstrthis;
            break;
        case 0:
            eptr = el_ptr_offset(s,offset);
            break;
        case DTRptre | DTRsnull:
            assert(0);
    }
    e = cpp_constructor(eptr,tclass,arglist,enelems,null,(dtorflag & 0x20) | ((dflag & DTRnoeh) ? 8 : 0));

    if (e)
    {
    L1:
        assert(!sinit);
        if (dflag & DTRrete)
            return e;

        s.Sflags |= SFLimplem;         /* got an initializer for variable */
        char localstatic = 0;
        switch (s.Sclass)
        {
            case SCstatic:
            /*case SClocstat:*/
                if (level > 0)
                    localstatic = 1;
                /* FALL-THROUGH */
            case SCglobal:
                if (localstatic || (s.Sscope && s.Sscope.Sclass == SCinline))
                {
                    if (s.Sdt)
                        s.Sdt.dt = DT_azeros; // don't use common block if ctor
                    if (!(dflag & DTRdtor))
                        break;
                    sinit = init_localstatic(&e,s);
                }
                else if (s.Sscope &&
                         ((s.Sscope.Sclass == SCstruct && s.Sscope.Sstruct.Stempsym) ||
                          s.Sscope.Sclass == SCinline))
                {
                    /* If s is a member of a template or inside an inline
                     * function, it needs to be protected from being
                     * constructed multiple times by separate modules.
                     */
                    elem *einit;

                    sinit = init_staticflag(s);

                    // Generate (sinit += 1)
                    einit = el_bint(OPaddass,tstypes[TYchar],el_var(sinit),el_longt(tstypes[TYchar],1));

                    // Generate (sinit || ((sinit += 1),e))
                    e = el_bint(OPoror,tstypes[TYint],el_var(sinit),
                                             el_combine(einit,e));

                    list_append(&constructor_list,e);
                    e = null;
                    break;
                }
                else
                {
                    if (s.Sdt)
                        s.Sdt.dt = DT_azeros; // don't use common block if ctor
                    list_append(&constructor_list,e);
                    e = null;
                    break;
                }
                /* FALLTHROUGH */
            case SCauto:
            case SCregister:
                block_appendexp(curblock, addlinnum(e));
                block_initvar(s);
                e = null;
                break;

            case SCcomdat:
                synerr(EM_should_be_static, &s.Sident[0]);
                e = null;
                break;

            default:
                symbol_print(s);
                assert(0);
        }
    }

Ldtor:
    if (dflag & DTRdtor && stag.Sstruct.Sdtor &&
        (s.Sclass == SCstatic || s.Sclass == SCglobal))
    {
            elem *enelems2;
            elem *ey;
            int temp = 0;

            for (Symbol *sc = s.Sscope; sc; sc = sc.Sscope)
            {
                if (sc.Sclass == SCstruct && sc.Sstruct.Stempsym)
                {
                    temp = 1;
                    if (!sinit)
                    {   sinit = init_staticflag(s);
                        temp = 2;               // destructor only
                    }
                    break;
                }
            }

            if (temp == 0 && s.Sclass == SCglobal && s.Sdt && s.Sdt.dt == DT_common)
                s.Sdt.dt = DT_azeros;         // don't use common block if dtor
            enelems2 = el_nelems(t);
            ey = el_ptr_offset(s,offset);
            ey = cpp_destructor(tclass,ey,enelems2,DTORmostderived | DTORnoeh);
            if (ey && sinit)
            {   /* Rewrite ey as (sinit && ey)    */
                elem *ex;
                if (temp)
                {
                    // Rewrite ey as (sinit -= 1, ey)
                    ex = el_bint(OPminass,tstypes[TYchar],el_var(sinit),el_longt(tstypes[TYchar],1));
                    ey = el_combine(ex, ey);
                }
                ey = el_bint(OPandand,tstypes[TYint],el_var(sinit),ey);
                if (temp == 2)
                {   // (sinit || (sinit += 1, e))
                    ex.Eoper = OPaddass;
                    ey.Eoper = OPoror;
                }
            }
            list_prepend(&destructor_list,ey);
    }
    return e;
}

/*******************************************
 * Generate Symbol to be used as a global flag to indicate if
 * Symbol s is constructed or not.
 */

//private
 Symbol* init_staticflag(Symbol *s)
{
    // Generate name as _flag_%s
    char* sid = cpp_mangle(s);
    char* name = cast(char *)alloca(6 + strlen(sid) + 1);
    memcpy(name, cast(const(char)*)"_flag_", 6);
    strcpy(name + 6, sid);

    Symbol* sinit = symbol_name(name, SCglobal, tstypes[TYchar]);
    init_common(sinit);
    outdata(sinit);
    symbol_keep(sinit);

    return sinit;
}

/********************************
 * Initialize an element of an array.
 * Input:
 *      offset  offset into array
 * Returns:
 *      initialization expression if a local static initialization
 */

//private
 elem* initarrayelem(Symbol *s,type *t,targ_size_t offset)
{   list_t arglist;
    targ_uns dim;
    bool brack;
    targ_size_t elemsize;
    elem *e;

    if (tok.TKval == TKlcur)
    {   brack = true;
        stoken();
    }
    else
        brack = false;                  /* elements are not bracketed   */

    e = null;
    switch (tybasic(t.Tty))
    {   case TYstruct:
            arglist = null;
            list_append(&arglist,assign_exp());
            e = init_constructor(s,t,arglist,offset,0x20,null);
            break;

        case TYarray:
            if (t.Tdim)
            {   elem *e2;

                elemsize = type_size(t) / t.Tdim;
                dim = 0;
                do
                {
                    e2 = initarrayelem(s,t.Tnext,offset + dim * elemsize);
                    e = el_combine(e,e2);
                    dim++;
                    if (dim == t.Tdim)         /* array is full, exit    */
                        break;
                } while (!endofarray());

                if (dim < t.Tdim)      /* if not enough initializers   */
                {
                    type *tc;

                    tc = type_copy(t);
                    tc.Tcount++;
                    tc.Tdim = t.Tdim - dim;
                    e2 = init_constructor(s,tc,null,offset + dim * elemsize,0x20,null);
                    e = el_combine(e,e2);
                    type_free(tc);
                }
            }
            break;

        default:
            assert(0);
    }
    init_closebrack(brack);
    return e;
}

/**********************************
 * Initialize array of structs with constructors.
 * Returns:
 *      1       did it
 *      0       s is not an array of structs with constructors
 */

//private
 int init_arraywithctor(Symbol *s)
{
    Symbol *sinit;
    Classsym *stag;
    char localstatic;

    type* t = s.Stype;
    type* tclass = type_arrayroot(t);
    if (tybasic(tclass.Tty) != TYstruct)
        return 0;
    stag = tclass.Ttag;
    template_instantiate_forward(stag);
    if (stag.Sstruct.Sflags & STRanyctor)
    {   targ_size_t dim;                /* # of initializers seen so far */
        targ_size_t elemsize;
        enum_SC sclass;
        elem* e,e2;

        sclass = cast(enum_SC)s.Sclass;    // storage class
        localstatic = (level > 0) && (sclass == SCstatic);
        if (/*sclass == SClocstat ||*/ (sclass == SCstatic && !localstatic) ||
            sclass == SCglobal)
            init_staticctor = true;

        elemsize = type_size(t.Tnext);
        if (tok.TKval == TKlcur)
        {
            stoken();
            e = null;
            dim = 0;
            do
            {
                e2 = initarrayelem(s,t.Tnext,dim * elemsize);
                e = el_combine(e,e2);
                dim++;
            } while (!endofarray());
            chktok(TKrcur,EM_rcur);                     /* {end with a '}'      */
        }
        else
        {
            e = initarrayelem(s,t.Tnext,0);
            dim = 1;
        }

        if (t.Tflags & TFsizeunknown)
            t = type_setdim(&s.Stype,dim);     /* we know the size     */
        else
        {
            if (t.Tdim > dim)                  /* if not enough initializers */
            {                                   /* initialize remainder */
                type* tc = type_copy(t);
                tc.Tcount++;
                tc.Tdim = t.Tdim - dim;
                e2 = init_constructor(s,tc,null,dim * elemsize,0x20,null);
                e = el_combine(e,e2);
                type_free(tc);
            }
            else if (t.Tdim < dim)
                synerr(EM_rcur);                // too many initializers
        }
        init_staticctor = false;

        sinit = null;
        if (localstatic)
        {   sinit = init_localstatic(&e,s);
            block_appendexp(curblock, addlinnum(e));
            block_initvar(s);
        }
        else
            assert(!e);

        if (/*sclass == SClocstat ||*/ sclass == SCstatic || sclass == SCglobal)
        {
            scope dtb = new DtBuilder();
            dtb.nzeros(elemsize * t.Tdim);
            dsout += elemsize * t.Tdim;
            assert(!s.Sdt);
            s.Sdt = dtb.finish();

            /* Call destructors */
            {
                elem* enelems = el_nelems(t);
                elem* ex = el_ptr(s);
                ex = cpp_destructor(tclass,ex,enelems,DTORmostderived | DTORnoeh);
                if (ex && sinit)
                {   // Rewrite ex as (sinit && ex)
                    ex = el_bint(OPandand,tstypes[TYint],el_var(sinit),ex);
                }
                list_prepend(&destructor_list,ex);
            }
        }
        return 1;
    }
    else
        return 0;
}


/****************************************
 * Handle conditional initialization of local statics.
 * Input:
 *      peinit  initialization expression
 *      s       the local static to be initialized
 * Output:
 *      *peinit rewritten expression that has the conditional in it
 * Returns:
 *      Symbol generated that is the conditional, null if none needed
 */

//private
 Symbol* init_localstatic(elem **peinit, Symbol *s)
{   type *tr;
    Symbol *sinit = null;
    elem *einit;

    //printf("init_localstatic(s = '%s')\n", s.Sident);
    //symbol_debug(s);
    assert(s.Sclass == SCstatic || s.Sclass == SCcomdat || s.Sclass == SCglobal);
    tr = type_arrayroot(s.Stype);
    func_expadddtors(peinit,pstate.STmarksi,globsym.top,true,true);
    einit = *peinit;
    if (einit ||
        tybasic(tr.Tty) == TYstruct && tr.Ttag.Sstruct.Sdtor)
    {   /* Initialize a local static    */
        elem *e;

        if (funcsym_p.Sclass == SCinline)
        {
            if (dtallzeros(s.Sdt))
            {   s.Sclass = SCglobal;
                dt2common(&s.Sdt);
            }
            else
                s.Sclass = SCcomdat;
            s.Sscope = funcsym_p;

            type *t = tstypes[TYchar];
            t.Tcount++;
            type_setmangle(&t, mTYman_cpp);
            t.Tcount--;
            sinit = symbol_name("__g", SCglobal, t);
            sinit.Sscope = s;
            init_common(sinit);
        }
        else
        {
            sinit = symbol_generate(SCstatic,tstypes[TYchar]);
            scope dtb = new DtBuilder();
            dtb.nzeros(tysize(TYchar));
            dsout += tysize(TYchar);
            sinit.Sdt = dtb.finish();
        }
        outdata(sinit);
        symbol_keep(sinit);

        // Generate (sinit = 1)
        e = el_bint(OPeq,tstypes[TYchar],el_var(sinit),el_longt(tstypes[TYchar],1));
        if (einit)
        {   /* Generate (sinit || ((sinit += 1),einit)) */
            e.Eoper = OPaddass;
            einit = el_bint(OPoror,tstypes[TYint],el_var(sinit),
                                         el_combine(e,einit));
        }
        else
            einit = e;
        *peinit = einit;
    }
    return sinit;
}


/********************************
 * Initialize auto Symbol sauto with static Symbol s.
 * Returns:
 *      initialization expression
 */

//private
 elem* init_sets(Symbol *sauto, Symbol *s)
{
    elem *e;
    if (s.Sdt && dtallzeros(s.Sdt))
    {   // Generate memset(&sauto,0,n);

        elem *ea = el_ptr(sauto);
        e = el_bint(OPmemset,ea.ET,
                ea,
                el_bint(OPparam,tstypes[TYint],
                    el_longt(tstypes[TYint],s.Sdt.DTazeros),
                    el_longt(tstypes[TYchar],0)));
    }
    else
        e = el_bint(OPstreq,sauto.Stype,el_var(sauto),el_var(s));      // tmp = str;
    e = addlinnum(e);
    return e;
}


/*******************************************
 */

//private
 Symbol * init_alloca()
{
    Symbol* s = scope_search("alloca", SCTglobal);
    if (!s)
    {
        /* Define alloca() according to the stdlib.h definition:
         * #if __OS2__ && __INTSIZE == 4
         * #define __CLIB __stdcall
         * #else
         * #define __CLIB __cdecl
         * #endif
         * #if _MSDOS
         * void __ss *    __CLIB alloca(unsigned);
         * #else
         * void *    __CLIB alloca(unsigned);
         * #endif
         */

        type *tfunc;
        type *tret;
        param_t *p;
        linkage_t linkage;

        linkage = LINK_C;
        if (config.exe & EX_OS2 && tysize(TYint) == 4)
            linkage = LINK_STDCALL;

        tfunc = type_alloc(FUNC_TYPE(cast(int) linkage, config.memmodel));
        type_setmangle(&tfunc, funcmangletab[cast(int) linkage]);
        tfunc.Tflags |= TFprototype;

        p = param_calloc();
        p.Ptype = tstypes[TYuint];
        tstypes[TYuint].Tcount++;
        tfunc.Tparamtypes = p;

        tret = tspvoid;
        if (config.exe & (EX_DOSX | EX_PHARLAP | EX_ZPM | EX_RATIONAL | EX_COM | EX_MZ))
        {
            tret = newpointer(tstypes[TYvoid]);
            tret.Tty = TYsptr;
        }
        tfunc.Tnext = tret;
        tret.Tcount++;

        s = symbol_name("alloca", SCextern, tfunc);
    }
    return s;
}

}
