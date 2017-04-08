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

#if !SPP

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
bool init_staticctor;   /* TRUE if this is a static initializer */
#endif

STATIC elem * initelem(type *, DtBuilder&, symbol *,targ_size_t);
STATIC elem * initstruct(type *, DtBuilder&, symbol *,targ_size_t);
STATIC elem * initarray(type *, DtBuilder&, symbol *,targ_size_t);
STATIC elem * elemtodt(symbol *, DtBuilder&, elem *, targ_size_t);
STATIC int init_arraywithctor(symbol *);
STATIC symbol * init_localstatic(elem **peinit,symbol *s);
STATIC elem * init_sets(symbol *sauto,symbol *s);
STATIC symbol * init_staticflag(symbol *s);

STATIC int endofarray(void);
STATIC size_t getArrayIndex(size_t i, size_t dim, char unknown);
STATIC void initializer(symbol *);
STATIC elem * dyn_init(symbol *);
STATIC symbol *init_alloca();


// Decide to put typeinfo symbols in code segment
#define CSTABLES        (config.memmodel & 2)
#if 1
#define CSFL            FLcsdata
#define CSMTY           mTYcs
#else
#define CSFL            FLfardata
#define CSMTY           mTYfar
#endif

static targ_size_t dsout = 0;   /* # of bytes actually output to data   */
                                /* segment, used to pad for alignment   */


/*********************** DtArray ***********************/

struct DtArray
{
    dt_t **data;
    size_t dim;

    DtArray()
    {   data = NULL;
        dim = 0;
    }

    ~DtArray()
    {
        if (data)
        {   free(data);
            data = NULL;
        }
    }

    void ensureSize(size_t i);

    void join(DtBuilder& dtb, size_t elemsize, size_t dim, char unknown);
};

void DtArray::ensureSize(size_t i)
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
        data[i] = NULL;
    }
}

/********************************
 * Put all the initializers together into one.
 */

void DtArray::join(DtBuilder& dtb, size_t elemsize, size_t dim, char unknown)
{
    size_t i = 0;
    for (size_t j = 0; j < this->dim; j++)
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
    t = s->Stype;
    assert(t);
    type_debug(t);
    //debug(debugy && dbg_printf("datadef('%s')\n",s->Sident));
    //printf("datadef('%s')\n",s->Sident);
    //symbol_print(s);

    if (CPP)
        nspace_checkEnclosing(s);

        switch (s->Sclass)
        {
            case SCauto:
            case SCregister:
#if TX86
                if (s->Stype->Tty & mTYcs)
                {   s->Sclass = SCstatic;
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
                    tyref(t->Tty) && t->Tnext->Tty & mTYconst)
                    endsi--;

                func_expadddtors(&curblock->Belem, marksi, endsi, 0, TRUE);
                break;
            case SCunde:
                assert(errcnt);         // only happens on errors
            case SCglobal:
                if (s->Sscope &&
                    s->Sscope->Sclass != SCstruct &&
                    s->Sscope->Sclass != SCnamespace)
                    s->Sclass = SCcomdat;
            case SCstatic:
            case SCcomdat:
            Lstatic:
                if (type_isvla(s->Stype))
                    synerr(EM_no_vla);          // can't be a VLA
                else
                    initializer(s);             // followed by initializer
#if SYMDEB_CODEVIEW
                if (s->Sclass == SCstatic && funcsym_p) // local static?
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
                if (type_isvla(s->Stype))
                    synerr(EM_no_vla);          // can't be a VLA
#if TX86
                if (s->Stype->Tty & mTYfar)
                    s->Sfl = FLfardata;
                else
#endif
                    s->Sfl = FLextern;
                break;
            default:
                symbol_print(s);
                assert(0);
        }
}

/************************************
 * Provide initializer for symbol s.
 * If statically initialized, output data directly to output
 * file. If dynamic, store init tree in the symbol table.
 * Take care of external references.
 */

STATIC void initializer(symbol *s)
{ type *t;
  tym_t ty;
  enum SC sclass;
  symbol *sauto;
  elem *einit;                          /* for dynamic initializers     */
  symbol *sinit;                        /* symbol to pass to initelem   */
  symbol *si;                           /* symbol for local statics     */
  Classsym *classsymsave;
  SYMIDX marksisave;
  char localstatic;
  int paren = FALSE;

  assert(s);

  //printf("initializer('%s')\n", s->Sident);
  //symbol_print(s);

  /* Allow void through */
  /*assert(tybasic(s->Stype->Tty));*/   /* variables only               */

  t = s->Stype;
  assert(t);
  ty = tybasic(t->Tty);                 /* type of data                 */
  sclass = (enum SC) s->Sclass;         // storage class
  if (CPP)
  {
    localstatic = ((sclass == SCstatic || sclass == SCglobal || sclass == SCcomdat)
        && level > 0);
    scope_pushclass((Classsym *)s->Sscope);
    classsymsave = pstate.STclasssym;
    if (s->Sscope && s->Sscope->Sclass == SCstruct)
        // Go into scope of class that s is a member of
        pstate.STclasssym = (Classsym *)s->Sscope;

    // Remember top of symbol table so we can see if any temporaries are
    // generated during initialization, so we can add in destructors for
    // them.
    marksisave = pstate.STmarksi;
    pstate.STmarksi = globsym.top;
  }

    if (CPP && tok.TKval == TKlpar && !tyaggregate(ty))
        paren = TRUE;
    else
    if (tok.TKval != TKeq)              /* if no initializer            */
    {
        switch (sclass)
        {   case SCglobal:
            case SCcomdat:
                sclass = SCglobal;
                s->Sclass = sclass;
                if (s->Sdt)
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
                    {   s->Sclass = SCextern;
                        s->Sfl = FLextern;
                        s->Sflags |= SFLwasstatic;
                        nwc_addstatic(s);
                        return;
                    }
                    else
                    {   type *tr = type_arrayroot(t);
                        if (tybasic(tr->Tty) == TYstruct)
                            template_instantiate_forward(tr->Ttag);
                        if (tybasic(tr->Tty) != TYstruct ||
                            ((tr->Ttag->Sstruct->Sflags & STRanyctor) == 0 &&
                             tr->Ttag->Sstruct->Sdtor == NULL))
                        {
                            s->Sclass = SCextern;
                            s->Sfl = FLextern;
                            s->Sflags |= SFLwasstatic;
                            nwc_addstatic(s);
                            goto cret;
                        }
                    }
                }
                {
                DtBuilder dtb;
                dtb.nzeros(type_size(t));
                dsout += type_size(t);
                assert(!s->Sdt);
                s->Sdt = dtb.finish();
                }
            Ls: ;
                if (CPP && !localstatic)
                {
                    init_staticctor = TRUE;
                }
        }
        if (t->Tflags & TFsizeunknown)
        {
            if (tybasic(t->Tty) == TYstruct)
            {   Classsym *stag = t->Ttag;
                template_instantiate_forward(stag);
                if (stag->Stype->Tflags & TFsizeunknown)
                    synerr(EM_unknown_size,stag->Sident); // size of %s is not known
            }
            else if (tybasic(t->Tty) == TYarray)
                synerr(EM_unknown_size,"array");        // size of %s is not known
            t->Tflags &= ~TFsizeunknown;
        }
        if (tybasic(t->Tty) == TYarray && type_isvla(t))
        {
            elem *e;
            elem *en;

            e = type_vla_fix(&s->Stype);
            t = s->Stype;
            block_appendexp(curblock, e);

            // Call alloca() to allocate the variable length array
            type *troot = type_arrayroot(t);
            symbol *salloca = init_alloca();

            e = el_var(s);
            e = arraytoptr(e);
            en = el_typesize(t);
            en = el_bint(OPcall, salloca->Stype->Tnext, el_var(salloca), en);
            en = cast(en, e->ET);
            e = el_bint(OPeq, e->ET, e, en);
            block_appendexp(curblock, e);
        }
        if (!CPP)
            return;
        if (tyref(ty) ||
            (t->Tty & mTYconst &&
             tybasic(type_arrayroot(t)->Tty) != TYstruct &&
             // MSC allows static const x;
             (ANSI || sclass != SCstatic)))
        {
            cpperr(EM_const_needs_init,s->Sident);      // uninitialized reference
        }
        if (tybasic(type_arrayroot(t)->Tty) == TYstruct)
        {   list_t arglist;

            /* Get list of arguments to constructor     */
            arglist = NULL;
            if (tok.TKval == TKlpar)
            {   stoken();
                getarglist(&arglist);
                chktok(TKrpar,EM_rpar);
            }

            init_constructor(s,t,arglist,0,1,NULL);
        }
        init_staticctor = FALSE;
        goto cret;
  }

  stoken();                             /* skip over '='                */
  s->Sflags |= SFLimplem;               /* seen implementation          */

    if (CPP)
    {
        if (ty == TYstruct)
        {
            Classsym *stag = t->Ttag;

            template_instantiate_forward(stag);
            if (stag->Sstruct->Sflags & STRanyctor)
            {
                // If { } and we can get by without the generated constructor
                if (tok.TKval == TKlcur &&
                    cpp_ctor(stag) != 1 &&
                    (!stag->Sstruct->Sctor ||
                     stag->Sstruct->Sctor->Sfunc->Fflags & Fgen))
                {
                    ;
                }
                else
                {
                    if (sclass == SCstatic || sclass == SCglobal)
                    {
                        DtBuilder dtb;
                        dtb.nzeros(type_size(t));
                        dsout += type_size(t);
                        s->Sdt = dtb.finish();
                        if (!localstatic)
                            init_staticctor = TRUE;
                    }

                    list_t arglist = NULL;
                    list_append(&arglist,assign_exp());
                    init_constructor(s,t,arglist,0,0x21,NULL);
                    init_staticctor = FALSE;
                    goto cret;
                }
            }
        }

        if (ty == TYarray && init_arraywithctor(s))
            goto cret;
    }

    einit = NULL;
    sauto = NULL;
    sinit = s;
    if ((sclass == SCauto || sclass == SCregister) && tyaggregate(ty) &&
        (tok.TKval == TKlcur || ty == TYarray))
    {   /* Auto aggregate initializer. Create a static copy of it, and  */
        /* copy it into the auto at runtime.                            */
        sauto = s;
        s = symbol_generate(SCstatic,t);
        if (CPP)
        {
            s->Sflags |= SFLnodtor;     // don't add dtors in later
        }
        s->Sflags |= SFLimplem;         // seen implementation
    }

    // If array of unknown size
    if (ty == TYarray && t->Tflags & TFsizeunknown)
    {   char bracket = 0;

        /* Take care of char a[]="  ";          */
#define isstring(t) (tok.TKval == TKstring && tyintegral((t)->Tnext->Tty) && tysize((t)->Tnext->Tty) == _tysize[tok.TKty])

        if (tok.TKval == TKstring || tok.TKval == TKlpar)
        {   elem *e;

        Lstring:
            e = arraytoptr(CPP ? assign_exp() : const_exp());
            e = poptelem(e);
            if (e->Eoper == OPstring && tyintegral(t->Tnext->Tty) && tysize((t)->Tnext->Tty) == tysize(e->ET->Tnext->Tty))
            {
                type_setdim(&s->Stype,e->EV.ss.Vstrlen / type_size(s->Stype->Tnext));   /* we have determined its size  */
                assert(s->Sdt == NULL);
                DtBuilder dtb;
                dtb.nbytes(e->EV.ss.Vstrlen,e->EV.ss.Vstring);
                s->Sdt = dtb.finish();
                el_free(e);
                t = s->Stype;
            }
            else
            {
                e = typechk(e,t->Tnext);
                e = poptelem(e);
                t = type_setdim(&s->Stype,1);
                DtBuilder dtb;
                einit = elemtodt(s,dtb,e,0);
                assert(!s->Sdt);
                s->Sdt = dtb.finish();
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

            elemsize = type_size(t->Tnext);
            if (CPP &&
                sinit == s &&
                !localstatic &&
                (sinit->Sclass == SCglobal || sinit->Sclass == SCstatic))
                init_staticctor = TRUE;
            dim = 0;                    // # of elements that we find
            i = 0;
            do
            {
                i = getArrayIndex(i, 0, 1);
                dta.ensureSize(i);
                DtBuilder dtb;
                einit = el_combine(einit,initelem(t->Tnext,dtb,sinit,i * elemsize));
                dta.data[i] = dtb.finish();
                i++;
                if (i > dim)
                    dim = i;
            } while (!endofarray());
            DtBuilder dtb;
            dta.join(dtb, elemsize, 0, 1);
            s->Sdt = dtb.finish();
            t = type_setdim(&s->Stype,dim);     // we have determined its size
            chktok(TKrcur,EM_rcur);             // end with a right curly

            if (CPP)
            {
                si = NULL;
                if (localstatic)
                    si = init_localstatic(&einit,s);

                init_staticctor = TRUE;
                init_constructor(s,t,NULL,0,0x21,si);   /* call destructor, if any */
                init_staticctor = FALSE;
            }
            goto ret;
        }

        /* Only 1 element in array */
        t = type_setdim(&s->Stype,1);
    }

    // Figure out if we need a bracketed initializer list

    if (tok.TKval != TKlcur &&
        (
         (!CPP && ty == TYstruct && s->Sclass != SCauto && s->Sclass != SCregister) ||
         (ty == TYarray && t->Tdim > 1 && !isstring(t))
        )
     )
        synerr(EM_lcur_exp);            // left curly expected

    switch (s->Sclass)
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
                DtBuilder dtb;
                initelem(t,dtb,s,0);                // static initializer
                assert(!s->Sdt);
                s->Sdt = dtb.finish();
                break;
            }
        case SCcomdat:
            // No symbols that get this far have constructors
            assert(CPP);
            if (sinit == s && !localstatic)
                init_staticctor = TRUE;
            DtBuilder dtb;
            if (ty == TYstruct && tok.TKval != TKlcur)
            {   elem *e;

                e = assign_exp();
                e = typechk(e,t);
                e = poptelem3(e);
                einit = elemtodt(s,dtb,e,0);
            }
            else
                einit = initelem(t,dtb,sinit,0);
            assert(!s->Sdt);
            s->Sdt = dtb.finish();

            /* Handle initialization of local statics   */
            si = NULL;
            if (localstatic)
                si = init_localstatic(&einit,s);
            else if (init_staticctor && einit)
            {
                list_append(&constructor_list,einit);
                einit = NULL;
            }

            init_constructor(s,t,NULL,0,0x41,si);       /* call destructor, if any */
            init_staticctor = FALSE;
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

        type_settype(&sauto->Stype,t);
        e = init_sets(sauto,s);
        if (config.flags3 & CFG3eh)
        {   // The following code is equivalent to that found in
            // cpp_constructor(). There is no constructor to initialize
            // sauto, so we hang the EH info off of the OPstreq.

            if (ty == TYstruct)
            {   Classsym *stag = t->Ttag;

                if (stag->Sstruct->Sdtor && pointertype == stag->Sstruct->ptrtype)
                    e = el_ctor(cpp_fixptrtype(el_ptr(sauto),t),e,n2_createprimdtor(stag));
            }
            else if (ty == TYarray)
            {   type *tclass;

                tclass = type_arrayroot(t);
                if (type_struct(tclass))
                {   Classsym *stag = tclass->Ttag;

                    if (stag->Sstruct->Sdtor && pointertype == stag->Sstruct->ptrtype)
                    {   elem *enelems;

                        enelems = el_nelems(t);
                        assert(enelems->Eoper == OPconst);
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
    st = stag->Sstruct;
    for (b = st->Svirtbase; b; b = b->BCnext)
        if (!(b->BCflags & BCFprivate))
            nbases++;
    for (b = st->Sbase; b; b = b->BCnext)
        if (!(b->BCflags & (BCFprivate | BCFvirtual)))
            nbases++;
    dtb.nbytes(2,(char *)&nbases);

    // Put out the base class info for each class
    flags = BCFprivate;
    b = st->Svirtbase;
    for (i = 0; i < 2; i++)
    {
        for (; b; b = b->BCnext)
        {   if (!(b->BCflags & flags))
            {   symbol *s;

                // Put out offset to base class
                dtb.nbytes(intsize,(char *)&b->BCoffset);

                // Put out pointer to base class type info
                s = init_typeinfo_data(b->BCbase->Stype);
                dtb.xoff(s,0,pointertype);
            }
        }
        flags |= BCFvirtual;
        b = st->Sbase;
    }
}

/**********************************
 * Create a symbol representing a type.
 * The symbol will be unique for each type, suitable for both RTTI and
 * exception handling type matching.
 */

symbol *init_typeinfo_data(type *ptype)
{
    symbol *s;
    char *id;
    enum FL fl;
    type *t = NULL;

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
        if (tyref(ptype->Tty))
            ptype = ptype->Tnext;
        t = type_copy(ptype);
        t->Tty &= ~(mTYconst | mTYvolatile);    // remove volatile too
        t->Tcount++;

        id = cpp_typetostring( t, "__ti" );
    }

    // Now we have the symbol name.
    // Type names appear in global symbol table
    s = scope_search(id, SCTglobal);

    // If it is not already there, create a new one
    if (!s)
    {   tym_t ty;
        DtBuilder dtb;

        s = scope_define(id, SCTglobal,SCcomdat);       // create the symbol
        s->Ssequence = 0;
        s->Stype = tschar;
        s->Stype->Tcount++;
        if (CSTABLES)
            type_setty(&s->Stype,s->Stype->Tty | CSMTY);

        if (!ptype)
        {   // Generate reference to extern
            goto Lextern;
        }
        else if ((ty = tybasic(ptype->Tty)) == TYstruct)
        {   // Generate:
            //  2, type-info, name
            init_typeinfo_struct(dtb,ptype->Ttag);
            s->Sfl = fl;
            goto Lname;
        }
        else if (typtr(ty))
        {   // Generate:
            //  1, flags, ptr-to-next, name
            char data[2];
            symbol *sn;
            type *tn;

            tn = ptype->Tnext;
            data[0] = 1;                        // indicate a pointer type
            data[1] = ty | (tn->Tty & (mTYvolatile | mTYconst));
            dtb.nbytes(2,data);
            sn = init_typeinfo_data(tn);
            dtb.xoff(sn,0,pointertype);
            s->Sfl = fl;
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
            s->Sdt = dtb.finish();
        }
        else if (ty == TYvoid)
        {   // Generate reference to extern
          Lextern:
            s->Sclass = SCextern;
#if TX86
            if (s->Stype->Tty & mTYcs)
                s->Sfl = FLcsdata;
            else
#endif
                s->Sfl = FLextern;
        }
        else
        {   // Generate:
#if 1
            // 0, name
            char data = 0;
            dtb.nbytes(1,&data);
            s->Sfl = fl;
            goto Lname;
#else
            //  0 (a common block)
            s->Sclass = SCglobal;
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
 *      symbol for created object
 *      NULL for error
 */

symbol *init_typeinfo(type *ptype)
{
    symbol *s;
    char *id;
    Classsym *srtti;
    type *t;

    srtti = rtti_typeinfo();
    if (!srtti)
        return NULL;

    type_debug(ptype);

    // C++ 5.2.8-4, -5 Ignore top level reference and cv qualifiers
    t = ptype;
    if (tyref(t->Tty))
        t = t->Tnext;
    t->Tcount++;
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
        s->Ssequence = 0;
        s->Stype = srtti->Stype;
        s->Stype->Tcount++;
        type_setmangle(&s->Stype,mTYman_c);
        if (CSTABLES)
            type_setty(&s->Stype,s->Stype->Tty | CSMTY);

        st = srtti->Sstruct;
        pty = st->ptrtype;

        // Create an initializer for the symbol.

        // The first entry is a pointer to the vtbl[]
        assert(st->Svptr->Smemoff == 0);        // should be first member
        enum SC scvtbl = (enum SC) (config.flags2 & CFG2comdat) ? SCcomdat :
             (st->Sflags & STRvtblext) ? SCextern : SCstatic;
        n2_genvtbl(srtti,scvtbl,0);
        DtBuilder dtb;
        dtb.xoff(st->Svtbl,0,pty);

        // The second is the pdata
        symbol *sti = init_typeinfo_data(ptype);
        dtb.xoff(sti,0,pty);
        s->Sdt = dtb.finish();
        s->Sfl = CSTABLES ? CSFL : FLdatseg;
    }
    symbol_debug(s);
    return s;
}

/**************************
 * Initialize symbol with expression e.
 */

void init_sym(symbol *s,elem *e)
{
    e = poptelem(e);
    DtBuilder dtb;
    e = elemtodt(s,dtb,e,0);
    assert(e == NULL);
    assert(!s->Sdt);
    s->Sdt = dtb.finish();
}

/*********************************
 * Read in a dynamic initializer for symbol s.
 * Output the assignment expression.
 */

STATIC elem * dyn_init(symbol *s)
{   elem *e,*e1,*e2;
    type *t;
    type *tv;
    tym_t ty;

    //printf("dyn_init('%s')\n", s->Sident);
    symbol_debug(s);
    t = s->Stype;
    assert(t);
    if (tok.TKval == TKlcur)    /* could be { initializer }     */
    {   stoken();
        e = dyn_init(s);
        chktok(TKrcur,EM_rcur);
    }
    else
    {
        type_debug(t);
        e2 = poptelem(arraytoptr(assign_exp()));
        ty = t->Tty;
        if (ty & mTYconst && e2->Eoper == OPconst)
        {   s->Sflags |= SFLvalue;
            tv = t;
            if (tyref(ty))
                tv = t->Tnext;
            s->Svalue = poptelem2(typechk(el_copytree(e2),tv));
            assert(s->Svalue->Eoper == OPconst);
        }
        e1 = el_var(s);
        if (tyref(ty))
        {
            t = newpointer_share(t->Tnext);
            e1 = el_settype(e1,t);
            if (tybasic(t->Tnext->Tty) == TYarray &&
                e2->ET->Tty == TYnptr &&
                typematch(t->Tnext->Tnext, e2->ET->Tnext, 0))
            {
                goto L1;
            }
        }
        e2 = typechk(e2,s->Stype);
      L1:
        e = el_bint(OPeq,t,e1,e2);
        e = addlinnum(e);
        if (tyaggregate(ty))
        {   e->Eoper = OPstreq;
            if (config.flags3 & CFG3eh && tybasic(ty) == TYstruct)
            {   Classsym *stag = t->Ttag;

                if (stag->Sstruct->Sdtor && pointertype == stag->Sstruct->ptrtype)
                    e = el_ctor(cpp_fixptrtype(el_ptr(s),t),e,n2_createprimdtor(stag));
            }
        }
        elem_debug(e);
    }
    return e;
}

/*******************************
 * Parse closing bracket of initializer list.
 */

STATIC void init_closebrack(int brack)
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

STATIC int endofarray()
{
    if (tok.TKval != TKcomma)
        return 1;
    stoken();                           /* skip over comma      */
    return (tok.TKval == TKrcur);               /* {A,B,C,} case        */
}

/*********************************
 * Return index of initializer.
 */

STATIC size_t getArrayIndex(size_t i, size_t dim, char unknown)
{
    // C99 6.7.8
    if (tok.TKval == TKlbra)    // [ constant-expression ]
    {   // If designator
        targ_size_t index;

        stoken();
        index = msc_getnum();
        if ((int)index < 0 || (ANSI && index == 0) ||
            (!unknown && index >= dim))
        {
            synerr(EM_array_dim, (int)index);
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
 *      s       symbol being initialized
 *      offset  from start of symbol where initializer goes
 * Returns:
 *      elem to be used for dynamic part of initialization
 *      NULL = no dynamic part of initialization
 */

STATIC elem * initelem(type *t, DtBuilder& dtb, symbol *s, targ_size_t offset)
{   elem *e;

    //dbg_printf("+initelem()\n");
    assert(t);
    type_debug(t);
    switch (tybasic(t->Tty))
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
#if TX86
        case TYnullptr:
        case TYnptr:
        case TYsptr:
        case TYcptr:
        case TYhptr:
#endif
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
                if (e->Eoper != OPconst)
                    e = poptelem3(e);
                if (s && s->Stype->Tty & mTYconst &&
                    e->Eoper == OPconst && tyscalar(s->Stype->Tty))
                {   s->Sflags |= SFLvalue;
                    s->Svalue = el_copytree(e);
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
            e = NULL;
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
    symbol *smember;
    elem *exp;          // SCfield
    dt_t *dt;           // SCmember
};

STATIC elem * initstruct(type *t, DtBuilder& dtb, symbol *ss,targ_size_t offset)
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

    ei = NULL;
    dsstart = dsout;
    assert(t);
    type_debug(t);
    if (tok.TKval == TKlcur)
    {   brack = TRUE;
        stoken();
    }
    else
        brack = FALSE;                  /* elements are not bracketed   */

    stag = t->Ttag;
    //printf("initstruct('%s')\n",stag->Sident);
    if (!brack && stag->Sstruct->Sflags & STRanyctor)
    {
        list_t arglist = NULL;
        list_append(&arglist, assign_exp());
        ei = init_constructor(ss, t, arglist, offset, 0x24, NULL);
        goto Ldone;
    }

    // Count up how many designators we'll need
    nmembers = 0;
    for (sl = stag->Sstruct->Sfldlst; sl; sl = list_next(sl))
    {   symbol *s = list_symbol(sl);

        if (!s)
            continue;
        switch (s->Sclass)
        {
            case SCfield:
            case SCmember:
                nmembers++;
                break;
        }
    }

    // Allocate and clear array of designators
    sd = (StructDesignator *)alloca(sizeof(StructDesignator) * nmembers);
    memset(sd, 0, sizeof(StructDesignator) * nmembers);

    // Populate array of designators
    i = 0;
    for (sl = stag->Sstruct->Sfldlst; sl; sl = list_next(sl))
    {   symbol *s = list_symbol(sl);

        switch (s->Sclass)
        {
            case SCfield:
            case SCmember:
                sd[i].smember = s;
                i++;
                break;
        }
    }

    e = NULL;
    soffset = (targ_size_t)-1;
    designated = 0;
    for (i = 0; 1; i++)
    {   symbol *s;
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

            //printf("\tsd[%d] = '%s'\n", i, sd[i].smember->Sident);
            s = sd[i].smember;
            symbol_debug(s);
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
                    return NULL;
                }
                if (strcmp(tok.TKid, sd[i].smember->Sident) == 0)
                {   s = sd[i].smember;
                    break;
                }
            }

            stoken();

            if (tok.TKval == TKeq)
                stoken();

            designated = 1;
            //printf("\tdesignator sd[%d] = '%s'\n", i, sd[i].smember->Sident);
        }
        else if (s->Sflags & SFLskipinit)
        {
            // The rest are the not-first union members
            continue;
        }
        //printf("  member '%s', offset x%lx\n",s->Sident,s->Smemoff);
        switch (s->Sclass)
        {
            case SCfield:
                if (!designated)
                {
                    if (!e && s->Smemoff == soffset)
                        continue;               // if not 1st member of union
                    if (soffset != -1)          // if not first
                    {   stoken();               // skip over separating comma
                        if (tok.TKval == TKrcur)
                            break;
                    }
                    if (tok.TKval == TKdot)
                        goto Ldesignator;
                }
                soffset = s->Smemoff;
                e = (elem *)1;
                e1 = CPP ? assign_exp() : const_exp();  // get an integer
                e1 = typechk(e1,s->Stype);
                if (!e1)
                    return NULL;                // error somewhere
                el_free(sd[i].exp);
                sd[i].exp = poptelem(e1);       // fold out constants
                break;

            case SCmember:
                if (!designated)
                {
                    if (s->Smemoff == soffset)
                        continue;               // must be a union
                    if (soffset != -1)          // if not first
                    {   stoken();               // skip over separating comma
                        if (tok.TKval == TKrcur)
                            break;
                    }
                    if (tok.TKval == TKdot)
                        goto Ldesignator;
                }
                soffset = s->Smemoff;
                dt_free(sd[i].dt);
                sd[i].dt = NULL;
                DtBuilder dtb;
                sd[i].exp = initelem(s->Stype,dtb,ss,offset + soffset);
                sd[i].dt = dtb.finish();
                break;

            default:
                assert(0);
        }

        if (tok.TKval != TKcomma)
            break;

        if (designated and !brack)
            break;
    }

    e = NULL;
    soffset = (targ_size_t)-1;
    dsout = dsstart;
    for (i = 0; i < nmembers; i++)
    {
        symbol *s = sd[i].smember;
        unsigned long ul;
        unsigned long fieldmask;
        elem *e1;

        switch (s->Sclass)
        {
            case SCfield:
                if (e && s->Smemoff != soffset)
                {
                    unsigned n = soffset - (dsout - dsstart);
                    dtb.nzeros(n);
                    dsout += n;
                    e = poptelem(e);
                    ec = elemtodt(ss,dtb,e,offset + soffset);
                    ei = el_combine(ei,ec);
                    e = NULL;
                }
                soffset = s->Smemoff;
                e1 = sd[i].exp;
                if (!e1)
                    continue;
                if (!e)
                    e = el_longt(s->Stype,0);
                fieldmask = ~(~0L << s->Swidth);
                if (CPP)
                {
                    e1 = el_bint(OPand,e1->ET,e1,el_longt(e1->ET,fieldmask));
                    e1 = el_bint(OPshl,e1->ET,e1,el_longt(tsint,s->Sbit));
                    if (e)
                    {
                        e = el_bint(OPand,e->ET,e,el_longt(e1->ET,~(fieldmask << s->Sbit)));
                        e = el_bint(OPor,e->ET,e,e1);
                    }
                    else
                        e = e1;
                }
                else
                {
                    if (EOP(e1))
                    {
                        e1 = poptelem2(e1);
                        if (EOP(e1))
                            synerr(EM_const_init);      // constant initializer expected
                    }
                    ul = e1->EV.Vulong;
                    el_free(e1);                // chuck the element
                    ul &= fieldmask;
                    ul <<= s->Sbit;             // shift into proper orientation

                    // Override existing initializer for this field
                    e->EV.Vulong &= ~(fieldmask << s->Sbit);

                    e->EV.Vulong |= ul;         // OR in new field
                }
                break;

            case SCmember:
                if (e)                  // if bit field
                {
                    unsigned n = soffset - (dsout - dsstart);
                    dtb.nzeros(n);
                    dsout += n;
                    e = poptelem(e);
                    ec = elemtodt(ss,dtb,e,offset + soffset);
                    ei = el_combine(ei,ec);
                    e = NULL;
                }

                if (sd[i].dt)
                {
                    soffset = s->Smemoff;
                    unsigned n = soffset - (dsout - dsstart);
                    dtb.nzeros(n);
                    dsout += n;
                    dtb.cat(sd[i].dt);
                    dsout += dt_size(sd[i].dt);
                }
                if (sd[i].exp)
                    ei = el_combine(ei,sd[i].exp);
                break;
        }
    }

    // if there is a bit field we still need to write out
    if (e)
    {
        e = poptelem(e);
        ec = elemtodt(ss,dtb,e,offset + soffset);
        ei = el_combine(ei,ec);
        e = NULL;
    }

Ldone:
    tsize = type_size(t);
    if (tsize > (dsout - dsstart))
    {
        unsigned n = tsize - (dsout - dsstart);
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

STATIC elem * initarray(type *t, DtBuilder& dtb,symbol *s,targ_size_t offset)
{
  char brack;
  targ_size_t dsstart,elemsize;
  targ_size_t tsize;
  char unknown;

  elem *e = NULL;

  if (tok.TKval == TKlcur)
  {     brack = TRUE;
        stoken();
  }
  else
        brack = FALSE;                  /* elements are not bracketed   */

    //printf("initarray(brack = %d, s = '%s')\n", brack, s->Sident);

    assert(tybasic(t->Tty) == TYarray);
    targ_size_t dim = t->Tdim;
    if (t->Tflags & TFsizeunknown)
    {   unknown = 1;
    }
    else
    {   unknown = 0;
        tsize = type_size(t);
    }

    // Take care of string initialization
    if (tok.TKval == TKstring && tyintegral(t->Tnext->Tty))
    {
        targ_size_t len;
        tym_t ty;

        char *mstring = combinestrings(&len, &ty);      // concatenate adjacent strings
        int ts = tysize(t->Tnext->Tty);
        if (ts == _tysize[ty])
        {
            if (unknown)
                tsize = len;

            // Lop off trailing 0 so 'char a[3]="abc";' works
            if (len - ts == tsize && (!CPP || !ANSI))
                len -= ts;

            if (len > tsize)
            {   synerr(EM_2manyinits);  // string is too long
                len = tsize;
            }
            dtb.nbytes(len,mstring);
            dsout += len;
            dtb.nzeros(tsize - len);
            dsout += tsize - len;
            MEM_PH_FREE(mstring);
            goto Ldone;
        }
    }

    dsstart = dsout;
    /* Determine size of each array element     */
    //dbg_printf("Tty = x%x, Tdim = %d, size = %d, Tnext->Tty = x%x\n",
            //t->Tty,t->Tdim,tsize,t->Tnext->Tty);
    if (dim || unknown)
    {   DtArray dta;

        targ_uns i = 0;
        if (unknown)
        {
            elemsize = type_size(t->Tnext);
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

            DtBuilder dtb;
            elem *ec = initelem(t->Tnext,dtb,s,i * elemsize);
            dta.data[i] = dtb.finish();
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
 *      NULL = no dynamic part of initialization, e is free'd
 */

STATIC elem * elemtodt(symbol *s, DtBuilder& dtb, elem *e, targ_size_t offset)
{
  char *p;
  tym_t ty;
  targ_size_t size;
  symbol *sa;

Lagain:
  if (errcnt)                   /* if errors have occurred in source file */
        goto ret;               /* then forget about output file        */
  assert(e);
  ty = e->ET->Tty;
  assert(CPP || !tyaggregate(ty));
  switch (e->Eoper)
  {
    case OPrelconst:
        sa = e->EV.sp.Vsym;
        if (!sa) return NULL;
    again:
        switch (sa->Sclass)
        {
            case SCanon:
                sa = sa->Sscope;
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
                return NULL;
            default:
#ifdef DEBUG
                WRclass((enum SC)sa->Sclass);
#endif
                assert(0);
        }
        ty = tym_conv(e->ET);
        dtb.xoff(sa,e->EV.sp.Voffset,ty);
        dsout += tysize(ty);
        break;

    case OPstring:
        size = e->EV.ss.Vstrlen;
        dtb.abytes(e->ET->Tty, e->EV.ss.Voffset, size, e->EV.ss.Vstring, 0);
        dsout += tysize(e->ET->Tty);
        break;

    case OPconst:
    {
        size = type_size(e->ET);
        targ_float f;
        targ_double d;
        Complex_f fc;
        Complex_d dc;
        switch (e->ET->Tty)
        {
            case TYfloat:
            case TYifloat:
                f = e->EV.Vfloat;
                p = (char *) &f;
                break;

            case TYdouble:
            case TYdouble_alias:
            case TYidouble:
                d = e->EV.Vdouble;
                p = (char *) &d;
                break;

            case TYcfloat:
                fc = e->EV.Vcfloat;
                p = (char *) &fc;
                break;

            case TYcdouble:
                dc = e->EV.Vcdouble;
                p = (char *) &dc;
                break;

            default:
                p = (char *) &e->EV;
                break;
        }
        dsout += size;
        dtb.nbytes(size,p);
        break;
    }

    default:
        e = poptelem4(e);               // try again to fold constants
        if (!EOP(e))
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

            s->Sflags |= SFLdyninit;
            ev = el_var(s);
            ev->EV.sp.Voffset = offset;
            t = e->ET;
            assert(!tyref(t->Tty));
            el_settype(ev,t);
            e = el_bint(OPeq,t,ev,e);
            if (tyaggregate(t->Tty))
                e->Eoper = OPstreq;
            if (init_staticctor)
            {   // Evaluate it in the module constructor
                if (pstate.STinsizeof)
                    el_free(e);         // ignore - it's in a sizeof()
                else
                    list_append(&constructor_list,e);
                e = NULL;
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
    e = NULL;

ret2:
    return e;

err:
#ifdef DEBUG
    elem_print(e);
#endif
    synerr(EM_const_init);      // constant initializer expected
    goto ret;
}



/*********************************
 * Construct static initializer for vtbl[].
 * Input:
 *      s_vtbl          symbol for vtbl[]
 *      virtlist        list of mptrs which will initialize vtbl[]
 *      stag            class of symbol we're generating a vtbl[] for
 *      srtti           class of complete symbol
 */

void init_vtbl(symbol *s_vtbl,list_t virtlist,Classsym *stag,Classsym *srtti)
{
    list_t lvf;
    targ_size_t size;
    tym_t fty;                          /* pointer to function type     */
    short offset;

    symbol_debug(s_vtbl);
    symbol_debug(stag);
    fty = LARGECODE ? TYfptr : TYnptr;
    cpp_getpredefined();                        /* get s_mptr           */
#ifdef DEBUG
    assert(fty == s_mptr->Stype->Tty);
#endif
    fty = tybasic(fty);
    size = type_size(s_mptr->Stype);

    DtBuilder dtb;

    // Put in RTTI information
    if (config.flags3 & CFG3rtti)
    {   symbol *s;

        symbol_debug(srtti);
        s = init_typeinfo(srtti->Stype);
        if (s)
            dtb.xoff(s,0,srtti->Sstruct->ptrtype);
    }

    for (lvf = virtlist; lvf; lvf = list_next(lvf))
    {   symbol *s;

        mptr_t *m = (mptr_t *) list_ptr(lvf);
        s = m->MPf;
        // Replace destructor call with scalar deleting destructor
        if (s->Sfunc->Fflags & Fdtor)
        {   Classsym *stag;

            stag = (Classsym *)s->Sscope;
            if (!stag->Sstruct->Sscaldeldtor)
                n2_createscaldeldtor(stag);
            s = stag->Sstruct->Sscaldeldtor;
        }
        if (m->MPd && !(s->Sfunc->Fflags & Fpure)) // if displacement from this
        {                                         // then a thunk is required
            symbol *sthunk;
            targ_size_t d;

            d = m->MPd;
            sthunk = nwc_genthunk(s,d,-1,0);
            //symbol_keep(sthunk);
            /*dbg_printf("Adding %s to class %s\n",sthunk->Sident,stag->Sident);*/
            //n2_addfunctoclass(stag->Stype,sthunk);
#if 0
            m->MPf = sthunk;            /* for possible other users     */
            m->MPd = 0;
#endif
            s = sthunk;
        }

        // BUG: if covariant return type needs adjustment, build wrapper
        // for s here.
        symbol_debug(s);
        /*dbg_printf("vtbl[] = %s\n",s->Sident);*/
        assert(s->Sfunc && tyfunc(s->Stype->Tty));

        if (m->MPflags & MPTRcovariant)
            synerr(EM_covariant, prettyident(m->MPf));

        // if 'pure' function, put a NULL in it's place in the vtbl[]
        if (s->Sfunc->Fflags & Fpure)
        {
            dtb.nzeros(_tysize[fty]);
            dsout += _tysize[fty];
        }
        else
        {
            /* Compute __mptr.f, the function pointer itself    */
            dtb.xoff(s,0,fty);
            dsout += _tysize[fty];
#ifdef DEBUG
            /*dbg_printf(" tysize = %d, size = %d\n",
                _tysize[fty],size - 2 * SHORTSIZE);*/
            assert(_tysize[fty] == size);
#endif
        }
    }
    s_vtbl->Sdt = dtb.finish();

    /*dbg_printf("Tdim = %d\n",s_vtbl->Stype->Tdim);*/
}


/*********************************
 * Construct static initializer for vtbl[].
 */

void init_vbtbl(
        symbol      *s_vbtbl,   // symbol for vbtbl[]
        baseclass_t *virtbase,  // list of virtual base classes
        Classsym    *stag,      // class of symbol we're generating vbtbl[] for
        targ_size_t  vbptr_off) // offset of Svbptr from address point of class
{
    char *pdata;
    baseclass_t *b;
    unsigned size;
    unsigned dim;

    //dbg_printf("init_vbtbl(s_vbtbl='%s',stag='%s',vbptr_off=x%lx)\n",
    //  s_vbtbl->Sident,stag->Sident,(long)vbptr_off);

    // Guess number of slots
    dim = (1 + baseclass_nitems(virtbase)) * intsize;   // extra slot at beginning

    // Allocate table
    pdata = (char *) MEM_PARF_CALLOC(dim);

    // Fill the table
    size = 0;
    for (b = virtbase; b; b = b->BCnext)
    {   baseclass_t *b2;

        b2 = baseclass_find(stag->Sstruct->Svirtbase,b->BCbase);
        assert(b2);
        //dbg_printf("b2='%s' vbtbloff = x%lx, size=x%x\n",b2->BCbase->Sident,(long)b2->vbtbloff,size);
        if (b2->BCvbtbloff + intsize > size)
        {   size = b2->BCvbtbloff + intsize;
            if (size > dim)             // need to reallocate array
            {   pdata = (char *) MEM_PARF_REALLOC(pdata,size);
                memset(pdata + dim,0,size - dim);
                dim = size;
            }
        }
#if TX86
        TOOFFSET(pdata + b->BCvbtbloff,b2->BCoffset - vbptr_off);
#else
        *(unsigned long *)(pdata + b->BCvbtbloff) = b2->BCoffset - vbptr_off;
#endif
    }

    DtBuilder dtb;
    dtb.nbytes(size, pdata);
    s_vbtbl->Sdt = dtb.finish();
    MEM_PARF_FREE(pdata);
}


/******************************
 * Handle constructor for s, if any.
 *      s               either a class or an array of classes.
 *      offset          offset from start of symbol s
 *      dtorflag        0: do not do destructor
 *                      1: add in destructor too
 *                      2: do not do destructor, return elem created
 *                         also means s is a pointer to a struct
 *                         do not generate eh information
 *                      3: do not do destructor, return elem created
 *                         also means s is NULL, we are constructing
 *                         on the stack
 *                      4: do not do destructor, return elem created
 *                      0x20: do not allow explicit constructors
 *                      0x40: do destructors only
 *      sinit           For local statics, sinit is the symbol that
 *                      creates a 'wrapper' around the construction/
 *                      destruction of the static.
 */

elem *init_constructor(symbol *s,type *t,list_t arglist,
        targ_size_t offset,int dtorflag,symbol *sinit)
{
    elem *e;
    elem *eptr;
    elem *enelems;
    type *tclass;
    Classsym *stag;

#define DTRdtor 1       /* add in destructor            */
#define DTRrete 2       /* return elem created          */
#define DTRptre 4       /* s is a pointer to a struct   */
#define DTRsnull        8       /* s is NULL, (constructing on stack)   */
#define DTRnoeh 0x10    // do not generate eh information
    static char translate[5] =
    { 0, DTRdtor, DTRrete|DTRptre|DTRnoeh, DTRrete|DTRsnull, DTRrete };
    char dflag = translate[dtorflag & 0x1F];

    //printf("init_constructor(s = '%s', level = %d)\n", s->Sident, level);
    if (!(dflag & DTRsnull))
        symbol_debug(s);
    tclass = type_arrayroot(t);
    if (tybasic(tclass->Tty) != TYstruct)
        return NULL;
    stag = tclass->Ttag;
    symbol_debug(stag);

    if (dtorflag & 0x40)
        goto Ldtor;

    enelems = el_nelems(t);

    /* Look for special cases where we can dump temporary variables     */
    if (list_nitems(arglist) == 1 && !errcnt)
    {   symbol *sa;
        symbol *sctor;
        elem *e1,*e2;

#if 0
        printf("init_constructor(s = '%s', dtorflag = x%x)\n", s->Sident, dtorflag);
        elem_print(list_elem(arglist));
#endif
        e = poptelem(list_elem(arglist));
        list_ptr(arglist) = e;

        /* Look for (tmp = xxx),tmp                             */
        if (e->Eoper == OPcomma &&
            (e2 = e->E2)->Eoper == OPvar &&
            /* BUG: what if mTYconst? */
            type_struct(e2->ET) && e2->ET->Ttag == stag &&
            (sa = e2->EV.sp.Vsym)->Sclass == SCauto &&
            (e1 = e->E1)->Eoper == OPstreq &&
            el_match(e1->E1,e2) &&
            !(dflag & DTRsnull))
        {
assert(0); // can't find any cases of this, must be an anachronism
            if (dflag & DTRptre)
            {
                el_free(e->E2);
                el_free(e1->E1);
                e->E2 = el_unat(OPind,tclass,el_var(s));
                e1->E1 = el_copytree(e->E2);
            }
            else
            {
                e2->EV.sp.Vsym = s;
                e2->EV.sp.Voffset = offset;
                e1->E1->EV.sp.Vsym = s;
                e1->E1->EV.sp.Voffset = offset;
            }
        L3: // Discard the temporary sa
            sa->Sflags |= SFLnodtor;
            if (sa->Sflags & SFLfree)
            {   sa->Sflags &= ~SFLfree;
                symbol_keep(sa);
            }
            goto L2;
        }

        if (e->Eoper == OPcond && (dflag & (DTRdtor | DTRrete)) == DTRrete)
        {   type *t2;

            list_free(&arglist,FPNULL);
            list_append(&arglist,e->E2->E1);
            e->E2->E1 = init_constructor(s,t,arglist,offset,dtorflag & 0x1F,sinit);
            arglist = NULL;
            list_append(&arglist,e->E2->E2);
            e->E2->E2 = init_constructor(s,t,arglist,offset,dtorflag & 0x1F,sinit);
            assert(e->E2->E2);
            t2 = e->E2->E2->ET;
            el_settype(e->E2,t2);
            el_settype(e,t2);
            e->E2->E1 = cast(e->E2->E1,t2);
            return e;
        }

        if (e->Eoper == OPind)
        {   elem *e1;
            elem *ec;

            e1 = e->E1;
#if TX86
            if (e1->Eoper == OPoffset)
                e1 = e1->E1;
#endif
            ec = NULL;
            if (e1->Eoper == OPinfo)
            {   if (e1->E1->Eoper == OPctor &&
                    e1->E1->E1->Eoper == OPrelconst)
                    ec = e1->E1->E1;
                e1 = e1->E2;
            }

            if (e1->Eoper == OPcall)
            {
                /* If argument is a call to a function that returns the
                 * struct as a hidden argument, replace the hidden argument
                 * with s, and let the function construct s.
                 */
                if (type_struct(e->ET) && e->ET->Ttag == stag)
                {   type *tf = e1->E1->ET;      /* function type        */
                    type *tret = tf->Tnext;     /* function return type */

                    type_debug(tret);
                    if (exp2_retmethod(tf) == RET_STACK &&
                        tret->Ttag == stag)
                    {   elem *eh;

                        // Find hidden parameter, and set e1 to it
                        eh = exp2_gethidden(e1);
                        if (eh->Eoper == OPrelconst &&
                            /* BUG: what if mTYconst? */
                            type_struct(eh->ET->Tnext) &&
                            eh->ET->Tnext->Ttag == stag &&
                            (sa = eh->EV.sp.Vsym)->Sclass == SCauto)
                        {
                            elem *es;
                            int result;

                            if (dflag & DTRsnull)
                            {   eh->Eoper = OPstrthis;
                                if (ec)
                                    ec->Eoper = OPstrthis;
                                e = list_elem(arglist);
#if TX86
                                if (e->E1->Eoper == OPoffset)
                                    list_setelem(arglist,selecte1(e,e->E1->ET));
#endif
                                goto L3;
                            }

                            es = (dflag & DTRptre) ? el_var(s) : el_ptr(s);
                            result = exp2_ptrconv(es->ET,eh->ET);
                            el_free(es);

                            if (result)         /* if compatible ptr types */
                            {
                                if (dflag & DTRptre)
                                    eh->Eoper = OPvar;
                                eh->EV.sp.Vsym = s;
                                eh->EV.sp.Voffset = offset;
                                if (ec)
                                {   ec->Eoper = eh->Eoper;
                                    ec->EV.sp.Vsym = s;
                                    ec->EV.sp.Voffset = offset;
                                }
                                goto L3;
                            }
                        }
                    }
                }
            }
            /* If argument is a call to a constructor for tclass        */
            if (e1->Eoper == OPcall &&
                (
                (e1->E1->Eoper == OPvar &&
                 (sctor = e1->E1->EV.sp.Vsym)->Sfunc->Fclass == stag &&
                 sctor->Sfunc->Fflags & Fctor
                )
                        ||
                (e1->E1->Eoper == OPind &&
                 e1->E1->E1->Eoper == OPvar &&
                 e1->E1->E1->EV.sp.Vsym->Sclass == SCextern &&
                 (sctor = e1->E1->E1->EV.sp.Vsym->Simport) != NULL &&
                 tyfunc(sctor->Stype->Tty) &&
                 sctor->Sfunc->Fclass == stag &&
                 sctor->Sfunc->Fflags & Fctor
                )
                )
               )
            {
                // Find ethis, which is the last parameter pushed
                do
                    e1 = e1->E2;
                while (e1->Eoper == OPparam);
                if (e1->Eoper == OPrelconst)
                {
                    elem_debug(e1);
                    assert(e1->EV.sp.Vsym->Sclass == SCauto);
#ifdef DEBUG
                    assert(enelems == NULL);
#endif
                    /* This ctor is sufficient. Discard the temporary,  */
                    /* putting s in instead                             */
                    sa = e1->EV.sp.Vsym;
                    sa->Sflags |= SFLnodtor;
                    if (sa->Sflags & SFLfree)
                    {   sa->Sflags &= ~SFLfree;
                        symbol_keep(sa);
                    }
                    if (dflag & DTRsnull)
                        e1->Eoper = OPstrthis;
                    else
                    {
                        if (dflag & DTRptre)
                            e1->Eoper = OPvar;
                        e1->EV.sp.Vsym = s;
                        e1->EV.sp.Voffset = offset;
                    }
                    if (ec)
                    {   ec->Eoper = e1->Eoper;
                        ec->EV.sp.Vsym = e1->EV.sp.Vsym;
                        ec->EV.sp.Voffset = e1->EV.sp.Voffset;
                    }
                L2: e = list_elem(arglist);
                    list_free(&arglist,FPNULL);

                    /* Convert to pointer to struct     */
                    if (dflag & DTRptre)
                    {   type *tret;

                        tret = newpointer(e->ET);
#if TX86
                        if (!I32)
                        {   tret->Tty = TYfptr;
                            if (e->Eoper == OPind &&
                                e->E1->Eoper == OPoffset)
                            {
                                e = selecte1(selecte1(e,tret),tret);
                                goto L1;
                            }
                        }
#endif
                        e = el_unat(OPaddr,tret,e);
                    }
                    else if (dflag & DTRsnull)
                    {
                        e = selecte1(e,e->E1->ET);
                    }

                    goto L1;
                }
            }
        }
    }

    switch (dflag & (DTRptre | DTRsnull))
    {   case DTRptre:
            eptr = el_var(s);
            break;
        case DTRsnull:
            eptr = el_longt(newpointer(tclass),0L);
            eptr->Eoper = OPstrthis;
            break;
        case 0:
            eptr = el_ptr_offset(s,offset);
            break;
        case DTRptre | DTRsnull:
            assert(0);
    }
    e = cpp_constructor(eptr,tclass,arglist,enelems,NULL,(dtorflag & 0x20) | ((dflag & DTRnoeh) ? 8 : 0));

    if (e)
    {   char localstatic;
    L1:
        assert(!sinit);
        if (dflag & DTRrete)
            return e;

        s->Sflags |= SFLimplem;         /* got an initializer for variable */
        localstatic = 0;
        switch (s->Sclass)
        {
            case SCstatic:
            /*case SClocstat:*/
                if (level > 0)
                    localstatic = 1;
                /* FALL-THROUGH */
            case SCglobal:
                if (localstatic || (s->Sscope && s->Sscope->Sclass == SCinline))
                {
                    if (s->Sdt)
                        s->Sdt->dt = DT_azeros; // don't use common block if ctor
                    if (!(dflag & DTRdtor))
                        break;
                    sinit = init_localstatic(&e,s);
                }
                else if (s->Sscope &&
                         ((s->Sscope->Sclass == SCstruct && s->Sscope->Sstruct->Stempsym) ||
                          s->Sscope->Sclass == SCinline))
                {
                    /* If s is a member of a template or inside an inline
                     * function, it needs to be protected from being
                     * constructed multiple times by separate modules.
                     */
                    elem *einit;

                    sinit = init_staticflag(s);

                    // Generate (sinit += 1)
                    einit = el_bint(OPaddass,tschar,el_var(sinit),el_longt(tschar,1));

                    // Generate (sinit || ((sinit += 1),e))
                    e = el_bint(OPoror,tsint,el_var(sinit),
                                             el_combine(einit,e));

                    list_append(&constructor_list,e);
                    e = NULL;
                    break;
                }
                else
                {
                    if (s->Sdt)
                        s->Sdt->dt = DT_azeros; // don't use common block if ctor
                    list_append(&constructor_list,e);
                    e = NULL;
                    break;
                }
                /* FALLTHROUGH */
            case SCauto:
            case SCregister:
                block_appendexp(curblock, addlinnum(e));
                block_initvar(s);
                e = NULL;
                break;

            case SCcomdat:
                synerr(EM_should_be_static, s->Sident);
                e = NULL;
                break;

            default:
                symbol_print(s);
                assert(0);
        }
    }

Ldtor:
    if (dflag & DTRdtor && stag->Sstruct->Sdtor &&
        (s->Sclass == SCstatic || s->Sclass == SCglobal))
    {
            elem *enelems;
            elem *e;
            int temp = 0;

            for (Symbol *sc = s->Sscope; sc; sc = sc->Sscope)
            {
                if (sc->Sclass == SCstruct && sc->Sstruct->Stempsym)
                {
                    temp = 1;
                    if (!sinit)
                    {   sinit = init_staticflag(s);
                        temp = 2;               // destructor only
                    }
                    break;
                }
            }

            if (temp == 0 && s->Sclass == SCglobal && s->Sdt && s->Sdt->dt == DT_common)
                s->Sdt->dt = DT_azeros;         // don't use common block if dtor
            enelems = el_nelems(t);
            e = el_ptr_offset(s,offset);
            e = cpp_destructor(tclass,e,enelems,DTORmostderived | DTORnoeh);
            if (e && sinit)
            {   /* Rewrite e as (sinit && e)    */
                elem *ex;
                if (temp)
                {
                    // Rewrite e as (sinit -= 1, e)
                    ex = el_bint(OPminass,tschar,el_var(sinit),el_longt(tschar,1));
                    e = el_combine(ex, e);
                }
                e = el_bint(OPandand,tsint,el_var(sinit),e);
                if (temp == 2)
                {   // (sinit || (sinit += 1, e))
                    ex->Eoper = OPaddass;
                    e->Eoper = OPoror;
                }
            }
            list_prepend(&destructor_list,e);
    }
    return e;
}

/*******************************************
 * Generate symbol to be used as a global flag to indicate if
 * symbol s is constructed or not.
 */

STATIC symbol * init_staticflag(symbol *s)
{   symbol *sinit;
    char *sid;
    char *name;

    // Generate name as _flag_%s
    sid = cpp_mangle(s);
    name = (char *)alloca(6 + strlen(sid) + 1);
    memcpy(name, "_flag_", 6);
    strcpy(name + 6, sid);

    sinit = symbol_name(name, SCglobal, tschar);
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

STATIC elem * initarrayelem(symbol *s,type *t,targ_size_t offset)
{   list_t arglist;
    targ_uns dim;
    bool brack;
    targ_size_t elemsize;
    elem *e;

    if (tok.TKval == TKlcur)
    {   brack = TRUE;
        stoken();
    }
    else
        brack = FALSE;                  /* elements are not bracketed   */

    e = NULL;
    switch (tybasic(t->Tty))
    {   case TYstruct:
            arglist = NULL;
            list_append(&arglist,assign_exp());
            e = init_constructor(s,t,arglist,offset,0x20,NULL);
            break;

        case TYarray:
            if (t->Tdim)
            {   elem *e2;

                elemsize = type_size(t) / t->Tdim;
                dim = 0;
                do
                {
                    e2 = initarrayelem(s,t->Tnext,offset + dim * elemsize);
                    e = el_combine(e,e2);
                    dim++;
                    if (dim == t->Tdim)         /* array is full, exit    */
                        break;
                } while (!endofarray());

                if (dim < t->Tdim)      /* if not enough initializers   */
                {
                    type *tc;

                    tc = type_copy(t);
                    tc->Tcount++;
                    tc->Tdim = t->Tdim - dim;
                    e2 = init_constructor(s,tc,NULL,offset + dim * elemsize,0x20,NULL);
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

STATIC int init_arraywithctor(symbol *s)
{   type *t;
    type *tclass;
    symbol *sinit;
    Classsym *stag;
    char localstatic;

    t = s->Stype;
    tclass = type_arrayroot(t);
    if (tybasic(tclass->Tty) != TYstruct)
        return 0;
    stag = tclass->Ttag;
    template_instantiate_forward(stag);
    if (stag->Sstruct->Sflags & STRanyctor)
    {   targ_size_t dim;                /* # of initializers seen so far */
        targ_size_t elemsize;
        enum SC sclass;
        elem *e,*e2;

        sclass = (enum SC)s->Sclass;    // storage class
        localstatic = (level > 0) && (sclass == SCstatic);
        if (/*sclass == SClocstat ||*/ (sclass == SCstatic && !localstatic) ||
            sclass == SCglobal)
            init_staticctor = TRUE;

        elemsize = type_size(t->Tnext);
        if (tok.TKval == TKlcur)
        {
            stoken();
            e = NULL;
            dim = 0;
            do
            {
                e2 = initarrayelem(s,t->Tnext,dim * elemsize);
                e = el_combine(e,e2);
                dim++;
            } while (!endofarray());
            chktok(TKrcur,EM_rcur);                     /* {end with a '}'      */
        }
        else
        {
            e = initarrayelem(s,t->Tnext,0);
            dim = 1;
        }

        if (t->Tflags & TFsizeunknown)
            t = type_setdim(&s->Stype,dim);     /* we know the size     */
        else
        {
            if (t->Tdim > dim)                  /* if not enough initializers */
            {                                   /* initialize remainder */
                type *tc;

                tc = type_copy(t);
                tc->Tcount++;
                tc->Tdim = t->Tdim - dim;
                e2 = init_constructor(s,tc,NULL,dim * elemsize,0x20,NULL);
                e = el_combine(e,e2);
                type_free(tc);
            }
            else if (t->Tdim < dim)
                synerr(EM_rcur);                // too many initializers
        }
        init_staticctor = FALSE;

        sinit = NULL;
        if (localstatic)
        {   sinit = init_localstatic(&e,s);
            block_appendexp(curblock, addlinnum(e));
            block_initvar(s);
        }
        else
            assert(!e);

        if (/*sclass == SClocstat ||*/ sclass == SCstatic || sclass == SCglobal)
        {
            DtBuilder dtb;
            dtb.nzeros(elemsize * t->Tdim);
            dsout += elemsize * t->Tdim;
            assert(!s->Sdt);
            s->Sdt = dtb.finish();

            /* Call destructors */
            {   elem *enelems;
                elem *e;

                enelems = el_nelems(t);
                e = el_ptr(s);
                e = cpp_destructor(tclass,e,enelems,DTORmostderived | DTORnoeh);
                if (e && sinit)
                {   /* Rewrite e as (sinit && e)        */
                    e = el_bint(OPandand,tsint,el_var(sinit),e);
                }
                list_prepend(&destructor_list,e);
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
 *      symbol generated that is the conditional, NULL if none needed
 */

STATIC symbol * init_localstatic(elem **peinit,symbol *s)
{   type *tr;
    symbol *sinit = NULL;
    elem *einit;

    //printf("init_localstatic(s = '%s')\n", s->Sident);
    symbol_debug(s);
    assert(s->Sclass == SCstatic || s->Sclass == SCcomdat || s->Sclass == SCglobal);
    tr = type_arrayroot(s->Stype);
    func_expadddtors(peinit,pstate.STmarksi,globsym.top,TRUE,TRUE);
    einit = *peinit;
    if (einit ||
        tybasic(tr->Tty) == TYstruct && tr->Ttag->Sstruct->Sdtor)
    {   /* Initialize a local static    */
        elem *e;

        if (funcsym_p->Sclass == SCinline)
        {
            if (dtallzeros(s->Sdt))
            {   s->Sclass = SCglobal;
                dt2common(&s->Sdt);
            }
            else
                s->Sclass = SCcomdat;
            s->Sscope = funcsym_p;

            type *t = tschar;
            t->Tcount++;
            type_setmangle(&t, mTYman_cpp);
            t->Tcount--;
            sinit = symbol_name("__g", SCglobal, t);
            sinit->Sscope = s;
            init_common(sinit);
        }
        else
        {
            sinit = symbol_generate(SCstatic,tschar);
            DtBuilder dtb;
            dtb.nzeros(tysize(TYchar));
            dsout += tysize(TYchar);
            sinit->Sdt = dtb.finish();
        }
        outdata(sinit);
        symbol_keep(sinit);

        // Generate (sinit = 1)
        e = el_bint(OPeq,tschar,el_var(sinit),el_longt(tschar,1));
        if (einit)
        {   /* Generate (sinit || ((sinit += 1),einit)) */
            e->Eoper = OPaddass;
            einit = el_bint(OPoror,tsint,el_var(sinit),
                                         el_combine(e,einit));
        }
        else
            einit = e;
        *peinit = einit;
    }
    return sinit;
}

/********************************
 * Initialize auto symbol sauto with static symbol s.
 * Returns:
 *      initialization expression
 */

STATIC elem * init_sets(symbol *sauto,symbol *s)
{
    elem *e;
    if (s->Sdt && dtallzeros(s->Sdt))
    {   // Generate memset(&sauto,0,n);

        elem *ea = el_ptr(sauto);
        e = el_bint(OPmemset,ea->ET,
                ea,
                el_bint(OPparam,tsint,
                    el_longt(tsint,s->Sdt->DTazeros),
                    el_longt(tschar,0)));
    }
    else
        e = el_bint(OPstreq,sauto->Stype,el_var(sauto),el_var(s));      // tmp = str;
    e = addlinnum(e);
    return e;
}

/*******************************************
 */

STATIC symbol * init_alloca()
{
    symbol *s;

    s = scope_search("alloca", SCTglobal);
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

        tfunc = type_alloc(FUNC_TYPE((int) linkage, config.memmodel));
        type_setmangle(&tfunc, funcmangletab[(int) linkage]);
        tfunc->Tflags |= TFprototype;

        p = param_calloc();
        p->Ptype = tsuns;
        tsuns->Tcount++;
        tfunc->Tparamtypes = p;

        tret = tspvoid;
        if (config.exe & (EX_DOSX | EX_PHARLAP | EX_ZPM | EX_RATIONAL | EX_COM | EX_MZ))
        {
            tret = newpointer(tsvoid);
            tret->Tty = TYsptr;
        }
        tfunc->Tnext = tret;
        tret->Tcount++;

        s = symbol_name("alloca", SCextern, tfunc);
    }
    return s;
}

#endif /* !SPP */
