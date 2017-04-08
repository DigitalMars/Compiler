/**
 * Implementation of the
 * $(LINK2 http://www.digitalmars.com/download/freecompiler.html, Digital Mars C/C++ Compiler).
 *
 * Copyright:   Copyright (c) 1994-1998 by Symantec, All Rights Reserved
 *              Copyright (c) 2000-2017 by Digital Mars, All Rights Reserved
 * Authors:     John Micco, $(LINK2 http://www.digitalmars.com, Walter Bright)
 * License:     Distributed under the Boost Software License, Version 1.0.
 *              http://www.boost.org/LICENSE_1_0.txt
 * Source:      https://github.com/DigitalMars/Compiler/blob/master/dm/src/dmc/eh.c
 */

// Exception handling for the C++ compiler

#if !SPP

#include <stdio.h>
#include <stdlib.h>
#include "cc.h"

#include "global.h"
#include "exh.h"
#include "parser.h"
#include "type.h"
#include "token.h"
#include "cpp.h"
#include "oper.h"
#include "code.h"
#include "scope.h"

static char __file__[] = __FILE__;      /* for tassert.h                */
#include        "tassert.h"

typedef struct Ehstack
{
    int prev;                   // index of previous stack (-1 is beginning)
    elem *el;
    block *bl;
} Ehstack;

static Ehstack *ehstack;
static int ehstacki;            // current index into ehstack[]
static int ehstackmax;          // max index used in ehstack[]
static int ehstackdim;          // allocated dimension of ehstack[]
static list_t marklist;         // stack of marks

typedef struct Ehpair
{   targ_size_t offset;
    int index;                  // index into ehstack[]
    void *p;                    // pointer to use as identifying marker
                                // (points to Code or Block)
} Ehpair;

static Ehpair *ehpair;
static int ehpairi;             // current index into ehpair[]
static int ehpairdim;           // allocated dimension of ehpair[]

// Special predefined functions
static symbol *eh_throw;        // throw function
static symbol *eh_rethrow;      // rethrow function
static symbol *eh_newp;         // ptr to storage allocator for thrown object


/**************************************
 * Initialize exception handling code.
 */

void except_init()
{
}

/*************************************
 * Terminate exception handling code.
 */

void except_term()
{
    assert(marklist == NULL);
#if TX86
    mem_free(ehstack);
    mem_free(ehpair);
#else
    MEM_PARC_FREE(ehstack);
    MEM_PARC_FREE(ehpair);
#endif
}

/*************************************
 * Add 'object created' data to elem.
 * Input:
 *      e       symbol to augment
 *      s       symbol of created auto
 *      offset  offset within s to object created
 *      sdtor   symbol of destructor for object within s
 * Returns:
 *      augmented elem
 */

#if 0

elem *except_obj_ctor(elem *e,symbol *s,targ_size_t offset,symbol *sdtor)
{
    symbol_debug(s);
    e = el_unat(OPinfo,e->ET,e);
    e->EV.eop.Edtor = s;
    return e;
}

/***************************************
 * Add 'object destroyed' data to elem.
 */

elem *except_obj_dtor(elem *e,symbol *s,targ_size_t offset)
{
    return except_obj_ctor(e,s,offset,NULL);
}

#endif

/*************************************
 * Degrades function and array types to their appropriate
 * pointer types and checks to see if type void is directly specified.
 * This code is very analogous to the function parameter type code
 * in getprototype().
 */

STATIC type * except_degrade_type(type *t)
{   tym_t tym;

    tym = tybasic(t->Tty);

    // If the top type of the catch variable is an array, then warp the
    // top type to be a pointer ( as for function arguments )

    if (tym == TYvoid)
        synerr(EM_void_novalue);        // voids have no value

    // Convert function to pointer to function
    else if (tyfunc(tym))
    {
        t = newpointer(t);
        t->Tnext->Tcount--;
        t->Tcount++;
    }

    // Convert <array of> to <pointer to> in prototypes
    else if (tym == TYarray)
        t = topointer(t);

    return t;
}

/*********************************
 * Parse the throw-expression.
 * Input:
 *      tok     is on the "throw"
 * Output:
 *      tok     first token after throw-expression
 */

elem *except_throw_expression()
{
    elem *pe;
    symbol *psym;
    targ_size_t tsize = 0;              // size of thrown object
    type *tae;                          // type of assignment-expression
    type *tdtor;                        // type of destructor
    list_t arglist = NULL;
    elem *efunc;
    elem *edtor;

    if (!(config.flags3 & CFG3eh))
    {
        cpperr(EM_compileEH);                   // EH not enabled
        panic(TKsemi);
        stoken();
        return el_longt(tsvoid,0);
    }

    stoken();
    if (tok.TKval == TKsemi || tok.TKval == TKcomma || tok.TKval == TKrpar)
    {   // Rethrow current exception

        // Generate a call to __rethrow
        if (!eh_rethrow)
        {   eh_rethrow = scope_search("__eh_rethrow",SCTglobal);
            eh_rethrow->Sflags |= SFLexit;    // 'rethrow' never returns
        }
        symbol_debug(eh_rethrow);
        efunc = el_var(eh_rethrow);
        goto L1;
    }
    // Parse assignment-expression to get object to throw
    pe = arraytoptr(assign_exp());
    tae = pe->ET;

    // If it is a pointer, adjust pointer types
    if (typtr(tae->Tty))
    {
        tae->Tcount++;
#if CFM68K || CFMV2
        paramtypadj(&tae,pe->ET);       // Check second argument
#else
        paramtypadj(&tae);
#endif
        pe = cast(pe,tae);
        type_free(tae);
    }

    pe = poptelem(pe);
    tae = pe->ET;

    // Create a new symbol for the string with the type embedded
    psym = init_typeinfo_data(tae);

    // Create the following tree:
    //
    // The throw function should be declared as:
    // void __eh_throw(char *pszType,pdtor,unsigned tsize,...);
    //  pszType - Is the type string constructed with the name mangling algorithm above
    //  pvValue - Is the value being thrown or a pointer to this value for
    //          values larger than 4 bytes.
    //  tsize - If non-zero indicates that the throw function should allocate memory
    //          for the thrown object and bit-copy it into that allocated memory.
    //
    //  Call
    //          var symthrow
    //          param
    //                  unsigned long size (0 for sizes <= 4)
    //                  param
    //                          pe      (&pe for sizes > 4 )
    //                          var psym (type string)

    if (!eh_throw)
    {   eh_throw = scope_search("__eh_throw",SCTglobal);
        eh_throw->Sflags |= SFLexit;    // 'throw' never returns
    }
    symbol_debug(eh_throw);
    efunc = el_var(eh_throw);
    tdtor = efunc->ET->Tparamtypes->Pnext->Ptype;

    tsize = type_size(tae);
    if (type_struct(tae))
    {   elem *en;
        list_t arglist;
        symbol *sdtor;
        symbol *stmp;

        if (!eh_newp)
            eh_newp = scope_search("__eh_newp",SCTglobal);
        symbol_debug(eh_newp);

        // Allocate memory by calling (*__eh_newp)(tsize)
        // (the following code emitted assumes eh_new()
        //  returns a non-NULL pointer)
        en = el_bint(OPcall,newpointer(tae),
                el_unat(OPind,eh_newp->Stype->Tnext,el_var(eh_newp)),
                el_longt(tsuns,tsize));

        // Allocate temporary for 'this' pointer
        stmp = symbol_genauto(en->ET);
        en = el_bint(OPeq,stmp->Stype,el_var(stmp),en);

        // Initialize allocated memory by using copy constructor
        arglist = list_build(pe,NULL);
        pe = cpp_constructor(el_var(stmp),tae,arglist,NULL,NULL,8);

        pe = el_bint(OPcomma,pe->ET,en,pe);

        // Indicate that storage was allocated by eh_new()
        tsize = 0;

        sdtor = tae->Ttag->Sstruct->Sdtor;

        // Error if this pointer type or
        // destructor is not of the ambient memory model.
        if (
#if TX86
            (sdtor && (tyfarfunc(sdtor->Stype->Tty) ? !LARGECODE : LARGECODE)) ||
#endif
             pointertype != tae->Ttag->Sstruct->ptrtype)
            cpperr(EM_not_of_ambient_model,prettyident(tae->Ttag));     // not ambient memory model

        // Create pointer to destructor
        if (sdtor)
        {   edtor = el_ptr(sdtor);
            el_settype(edtor,tdtor);
        }
        else
            edtor = el_longt(tdtor,0);          // NULL ptr to destructor

    }
    else
        edtor = el_longt(tdtor,0);              // NULL ptr to destructor

    // Assemble argument list to __eh_throw()
#if TX86
    arglist = list_build(el_ptr(psym),edtor,el_longt(tsuns,tsize),pe,NULL);
#else
    arglist = list_build(arraytoptr(el_var(psym)),edtor,el_longt(tsuns,tsize),pe,NULL);
#endif

L1:
    pe = xfunccall(efunc,NULL,NULL,arglist);    // call throw function
    return pe;
}

/********************************************
 * An initializer is needed for each variable appearing in a catch clause.
 * If the symbol is more than 4 bytes or is a structure, then a pointer to
 * it is given.  In this case, an initializer must be written as if the
 * user had written:
 *      T Cv = *((T *) I);
 *
 * Otherwise, it must behave as if the user had written:
 *      T Cv = ((T) I )
 */

STATIC void except_initialize_catchvar(symbol *psymCatchvar,symbol *sinit)
{
    tym_t       ty;
    type        *t;
    elem        *pe1;
    elem        *einit;
    elem        *e;

    einit = el_var( sinit );

    t = psymCatchvar->Stype;
    ty = tybasic(t->Tty);

    // The type of sinit is (void **), so we must convert it to (t **)
    el_settype(einit,newpointer(newpointer(t)));

    einit = el_unat( OPind, einit->ET->Tnext, einit );
    einit = el_unat( OPind, t, einit );

    if (ty == TYstruct)
    {
        list_t arglist;

        arglist = list_build(einit,NULL);
        init_constructor(psymCatchvar,t,arglist,0,1,NULL);
    }
    else
    {
#if 0
        if (!tyref(ty) && _tysize[ty] <= _tysize[pointertype])
            // s = cv;
            el_settype(einit,t);            // copy it over directly
        else
        {   // s = *cv;
            el_settype(einit,newpointer(t));
            einit = el_unat( OPind, t, einit );
        }
#endif
        pe1 = el_var(psymCatchvar);
        if (tyref(ty))
        {   pe1 = el_settype(pe1,reftoptr(t));
            einit = el_unat(OPaddr,pe1->ET,einit);
        }
        e = addlinnum(el_bint(OPeq,pe1->ET,pe1,einit));
        block_appendexp(curblock, e);
    }
}

/***********************************
 * Parse the exception-declaration.
 * This parser is very similar to declaration() in nwc.c.
 * Note that no storage class is allowed, i.e. no "catch(static int x)"
 *      exception-declaration:
 *              type-specifier-seq declarator
 *              type-specifier-seq abstract-declarator
 *              type-specifier-seq
 *              ...
 * Input:
 *      cv      symbol of the void* that is filled in by the stack
 *              unwinding code, it is the initializer for
 *              the exception-declaration.
 * Returns:
 *      type of exception-declaration (Tcount incremented)
 *      NULL if ...
 */

type *except_declaration(symbol *cv)
{   type *tcatch;

    if (tok.TKval == TKellipsis)        // If it is a ... it matches anything
    {   stoken();
        tcatch = NULL;
    }
    else
    {
        // Otherwise it must be a single parameter
        // style declaration
        type *typ_spec;
        char vident[2*IDMAX + 1];
        symbol *s;
        list_t lt;

#if TX86 && VERSIONINT >= 0x710
        type_specifier( &typ_spec );
#else
        type_specifier( &typ_spec, NULL );
#endif
        tcatch = declar_fix(typ_spec,vident);
        type_free(typ_spec);

        tcatch = except_degrade_type( tcatch );
        type_debug(tcatch);

        if (vident[0] != 0)             // if not abstract-declarator
        {
            tcatch->Tcount++;
            s = symdecl(vident,tcatch,SCauto,NULL);
            if (s)
            {   tym_t ty;

                ty = tybasic(s->Stype->Tty);
                if (ty == TYvoid || tyfunc(ty))
                {
                    synerr(EM_void_novalue);    // void has no value
                    type_free(s->Stype);
                    s->Stype = tsint;
                    tsint->Tcount++;
                }
                if (!(s->Stype->Tflags & TFsizeunknown) &&
                    intsize == 2 &&
                    type_size(s->Stype) > 30000)
                    warerr(WM_large_auto);      // local variable is too big
                symbol_add(s);
                except_initialize_catchvar(s,cv);
            }
        }
    }
    return tcatch;
}

/*******************************************
 * Parse optional exception-specification (15.5).
 * Input:
 *      t       Function type to which we add exception specification info
 *      tok     Current token would be the 'throw'
 * Output:
 *      tok     token past exception-specification
 *      t       filled in
 */

void except_exception_spec(type *t)
{
    //printf("except_exception_spec()\n");
    assert(t->Tcount == 0);
    if (tok.TKval == TKthrow)
    {
        stoken();
        chktok(TKlpar,EM_lpar2,"throw");
        t->Tflags |= TFemptyexc;                // assume empty exception-specification
        while (1)
        {   if (tok.TKval == TKeof)
                err_fatal(EM_eof);              // premature end of file
            if (tok.TKval != TKrpar)
            {   type *typ_spec, *type_id;
                list_t list1;

                // Get type-id
#if TX86 && VERSIONINT >= 0x710
                type_specifier(&typ_spec);
#else
                type_specifier(&typ_spec, NULL);
#endif
                type_id = declar_abstract(typ_spec);
                fixdeclar(type_id);
                type_free(typ_spec);

                // The type specification on the throw must degrade
                // function and array types
                type_id = except_degrade_type(type_id);

#if 0   // Not clear this check is required
                // Check to insure that the same type is not specified more
                // than once on an exception specification
                for (list1 = list; list1; list1 = list_next(list1))
                {
                    if (typematch( list_type(list1),type_id,0 ))
                    {
                        cpperr(EM_eh_types);    // duplicated type
                        break;
                    }
                }
#endif
                list_append(&t->Texcspec, type_id);     // append the type to the list
                t->Tflags &= ~TFemptyexc;       // non-empty exception-specification
            }

            switch (tok.TKval)
            {
                case TKcomma:           // skip any commas
                    stoken();           // keep going
                    continue;
                case TKrpar:            // end of the exception-specfication
                    stoken();
                    break;
                default:
                    synerr(EM_rpar);            // right paren expected
                    break;
            }
            break;
        }
    }
}

/*******************************************
 * Parse optional exception-specification (15.5).
 * Input:
 *      sfunc   Function symbol of function declarator
 *      tok     Current token would be the 'throw'
 * Output:
 *      tok     token past exception-specification
 */

void except_exception_spec_old(symbol *sfunc)
{
    list_t list = NULL;
    func_t *f;
    unsigned char flags3 = 0;

    if (tok.TKval == TKthrow)
    {
        stoken();
        chktok(TKlpar,EM_lpar2,"throw");
        flags3 |= Fcppeh | Femptyexc;   // assume empty exception-specification
        while (1)
        {   if (tok.TKval == TKeof)
                err_fatal(EM_eof);              // premature end of file
            if (tok.TKval != TKrpar)
            {   type *typ_spec, *type_id;
                list_t list1;

                // Get type-id
#if TX86 && VERSIONINT >= 0x710
                type_specifier(&typ_spec);
#else
                type_specifier(&typ_spec, NULL);
#endif
                type_id = declar_abstract(typ_spec);
                fixdeclar(type_id);
                type_free(typ_spec);

                // The type specification on the throw must degrade
                // function and array types
                type_id = except_degrade_type(type_id);

#if 0   // Not clear this check is required
                // Check to insure that the same type is not specified more
                // than once on an exception specification
                for (list1 = list; list1; list1 = list_next(list1))
                {
                    if (typematch( list_type(list1),type_id,0 ))
                    {
                        cpperr(EM_eh_types);    // duplicated type
                        break;
                    }
                }
#endif
                list_append(&list,type_id);     // append the type to the list
                flags3 &= ~Femptyexc;           // non-empty exception-specification
            }

            switch (tok.TKval)
            {
                case TKcomma:           // skip any commas
                    stoken();           // keep going
                    continue;
                case TKrpar:            // end of the exception-specfication
                    stoken();
                    break;
                default:
                    synerr(EM_rpar);            // right paren expected
                    break;
            }
            break;
        }
    }

    f = sfunc->Sfunc;
    if (f->Fflags3 & Fdeclared)         // if already declared function
    {   // Then any exception-specification must match
        // (i.e. contain the same set of type-id's)
        list_t list1;
        list_t list2;

        if ((flags3 ^ f->Fflags3) & Femptyexc)
            cpperr(EM_exception_specs);                         // exc-specs must match

        for (list1 = list; list1; list1 = list_next(list1))
        {
            for (list2 = f->Fexcspec; list2; list2 = list_next(list2))
            {
                if (typematch(list_type(list1),list_type(list2),0))
                {
                    type_free(list_type(list2));
                    list_subtract(&f->Fexcspec,list_type(list2));
                    goto L1;
                }
            }
            cpperr(EM_exception_specs);                         // exc-specs must match
            break;

         L1: ;
        }
        if (f->Fexcspec)                        // if any left over
        {
            cpperr(EM_exception_specs);                 // exc-specs must match
            list_free(&f->Fexcspec,(list_free_fp)type_free); // free remainder
        }
    }
    f->Fexcspec = list;
    f->Fflags3 |= flags3 | Fdeclared;
}

/********************************
 * Get/set eh stack index.
 */

void except_index_set(int index)
{ ehstacki = index; }

int except_index_get()
{ return ehstacki; }

/*********************************
 * Set code offset relative to p.
 * Not used for SEH exceptions.
 */

void except_pair_setoffset(void *p,targ_size_t offset)
{   int i;

#ifdef DEBUG
    if (debuge)
        dbg_printf("except_pair_setoffset(p = %p, offset = x%lx)\n",p,offset);
#endif
    for (i = ehpairi; i;)
    {
        i--;
        if (ehpair[i].p == p)
        {   ehpair[i].offset = offset;
            if (i && ehpair[i - 1].offset == offset)
            {
                ehpair[i - 1].index = ehpair[i].index;
                ehpair[i - 1].p = p;
            }
        }
    }
}

/********************************
 * Append pair to ehpair[].
 * Not used for NT exceptions.
 */

void except_pair_append(void *p, int index)
{
#ifdef DEBUG
    if (debuge)
        dbg_printf("except_pair_append(p = %p, index = %d)\n",p,index);
#endif
#if 0
    if (ehpairi && offset == ehpair[ehpairi - 1].offset)
        ehpair[ehpairi - 1].index = index;
    else
#endif
    {
        if (ehpairi == ehpairdim)
        {   ehpairdim += ehpairdim + 10;
#if TX86 // DJB
            ehpair = (Ehpair *) mem_realloc(ehpair,ehpairdim * sizeof(Ehpair));
#else
            ehpair = (Ehpair *) MEM_PARC_REALLOC(ehpair,ehpairdim * sizeof(Ehpair));
#endif
        }
        ehpair[ehpairi].p = p;
        ehpair[ehpairi].offset = (targ_size_t)-1L;
        ehpair[ehpairi].index  = index;
        ehpairi++;
    }
}

/********************************
 * Add constructed object onto list of objects to be destructed.
 * Input:
 *      p               identifying pointer, NULL if NT
 *      e               Elem describing a pointer to the object
 *      b               Block pointer for tryblock
 */

void except_push(void *p,elem *e,block *b)
{   int prev;

#ifdef DEBUG
    if (debuge)
    {   dbg_printf("except_push(p = %p, e = %p, b = %p)\n",p,e,b);
        if (e) elem_print(e);
    }
#endif
    assert(b || e->Eoper == OPctor);
    if (ehstackdim == ehstackmax)
    {   ehstackdim += ehstackdim + 10;
#if TX86 // DJB
        ehstack = (Ehstack *) mem_realloc(ehstack,ehstackdim * sizeof(Ehstack));
#else
        ehstack = (Ehstack *) MEM_PARC_REALLOC(ehstack,ehstackdim * sizeof(Ehstack));
#endif
    }
    prev = ehstacki - 1;
    if (ehstacki < ehstackmax)
        ehstacki = ehstackmax;  // add new stack onto end
    ehstack[ehstacki].prev = prev;
    ehstack[ehstacki].el = e;
    ehstack[ehstacki].bl = b;
#if TX86 && NTEXCEPTIONS
    if (config.exe != EX_WIN32)
#endif
        except_pair_append(p,ehstacki);
    //printf(" prev = %d, ehstacki = %d\n",prev,ehstacki);
    ehstackmax = ++ehstacki;
}

/********************************
 * Remove constructed object from list of objects to be destructed.
 * Input:
 *      p               identifying pointer, NULL if NT
 *      e               Elem describing a pointer to the object
 */

void except_pop(void *p,elem *e,block *b)
{   int i,ip;
    list_t elist,el;

#ifdef DEBUG
    if (debuge)
    {   dbg_printf("except_pop(p = %p, e = %p, b = %p)\n",p,e,b);
        if (e) elem_print(e);
    }
#endif
    assert(b || e->Eoper == OPdtor);
    elist = NULL;
    i = ehstacki - 1;
    while (1)
    {
        //printf("i = %d\n",i);
        assert(i >= 0);
        ip = ehstack[i].prev;
        if ((b && b == ehstack[i].bl) ||
            (e && ehstack[i].el && el_match(e->E1,ehstack[i].el->E1))
           )
        {   ehstacki = ip + 1;
#if TX86 && NTEXCEPTIONS
            if (config.exe != EX_WIN32)
#endif
                except_pair_append(p,ip);
            break;
        }
#ifdef DEBUG
        if (debuge)
            dbg_printf("out-of-sequence\n");
#endif
        //assert(0);            // no out-of-sequence pops
        list_prependdata(&elist,i);
        i = ip;
    }

    // Push back on all the out-of-sequence pops
    for (el = elist; el; el = list_next(el))
    {
        i = list_data(el);
        except_push(p,ehstack[i].el,ehstack[i].bl);
        if (el == elist)
        {
            ehstack[ehstacki - 1].prev = ip;
            //printf(" prev = %d\n",ip);
        }
    }
    list_free(&elist,FPNULL);
}

/****************************************
 * Mark/release eh stack.
 */

void except_mark()
{
#ifdef DEBUG
    if (debuge)
        dbg_printf("except_mark() %d\n",ehstacki);
#endif
    list_prependdata(&marklist,ehstacki);
}

void except_release()
{
    ehstacki = list_data(marklist);
#ifdef DEBUG
    if (debuge)
        dbg_printf("except_release() %d\n",ehstacki);
#endif
    list_pop(&marklist);
}

/****************************************
 * Generate symbol for scope table for current function.
 * Returns:
 *      NULL    None generated
 */

static symbol *except_sym;

symbol *except_gensym()
{
#if !TX86
    static type *t = NULL;
#endif

    // Determine if we need to do anything at all
    if (!(ehstackmax || ehpairi || funcsym_p->Sfunc->Fflags3 & Fcppeh))
        except_sym = NULL;
    else if (!except_sym)
    {
#if !TX86
        if (!t)
        {
            t = newpointer(tschar);
            t->Tty = TYarray;
            t->Tdim = 0;
        }
        except_sym = symbol_generate(SCextern,t);
#else
        except_sym = symbol_generate(SCextern,type_alloc(TYint));
#endif
        symbol_keep(except_sym);
    }
    return except_sym;
}

/****************************************
 * Generate C++ exception handler tables for current function.
 * Input:
 *      funcsym_p       Current function
 * The tables are:
 * In segment XF, there is one of these for each function with eh data,
 * generated by Obj::ehtables():
 *      void*           pointer to start of function
 *      void*           pointer to eh data for function
 *      unsigned        size of function
 * The eh data consists of:
 *      short           1 = near function, 2 = far function
 *      void*           pointer to start of function (NT only)
 *      unsigned        offset of SP from BP
 *      unsigned        offset from start of function to return code
 *      exception-specification-table
 *      address-table
 *      handler-table
 *      cleanup-table
 * exception-specification-table
 *      This is a NULL-terminated array of pointers to typeinfo objects.
 *      A (void*)(-1) as the first entry means no exceptions are thrown.
 * address-table
 *      short number-of-entries
 *      entries { address, index into cleanup-table }
 *
 * For the PowerPC and 68K, the structure is:
 *                              bytes   description
 *          type info           4       Type info list or -1
 *      zero                    4       Null terminiation for type info
 *      number of pairs         2       Number of pairs
 *          offset              4       offset into code
 *          index into handler  2       Index into the handler table
 *      number of try blocks    2       Number of try blocks
 *          offset of catch var 4       Offset from SP to catch variable
 *          number of handlers  2       Number of handlers for this try
 *              offset          4       Offset from start of function to catch
 *              type            4       Type of caught variable
 *      cleanup table           -       -
 *          previous index      2       Index of the previous cleanup entry
 *          type                2       type of this entry (1=Try, 2=dtor(this),
 *                                      3=dtor(this+offset), 4=dtor(ptr)
 *          offset              4       Offset to this
 *          thisoff             4       offset from this to object
 *          dtor()              4       Destructor pointer
 */

symbol *except_gentables()
{
    symbol *ehsym;              // symbol for eh data
    type *tsym;                 // type for eh data
    DtBuilder dtb;
    unsigned psize;             // target size of (void *)
    unsigned fsize;             // target size of function pointer
    int i;
    block *b;
    unsigned short us;          // For placing short values into the stream
#if TX86
    int ntrys;
    typedef char tyexcept_type;
#else
    unsigned short ntrys;       // Number of trys (short)
    typedef short tyexcept_type;
#endif
    int sz;                     // size so far
    int farfunc;
    long zero = 0;
    targ_size_t spoff;

    ehsym = except_gensym();
    if (!ehsym)
        return NULL;
    ehsym->Sclass = SCstatic;
    tsym = ehsym->Stype;
    sz = 0;

#if !TX86
    psize = _tysize[pointertype];
    fsize = psize;

#else

    psize = _tysize[pointertype];
    fsize = LARGECODE ? (2 + intsize) : intsize;

    if (LARGEDATA)
    {   // Put table in code segment for large data models
        if (config.flags & CFGfarvtbls)
            tsym->Tty |= mTYfar;
#if MEMMODELS != 1
        else if (config.memmodel != Vmodel)     // table can't be in overlay
        {   tsym->Tty |= mTYcs;
            if (SegData[cseg]->segidx < 0)
                ehsym->Sxtrnnum = funcsym_p->Sxtrnnum;
        }
#endif
    }

#if TARGET_WINDOS
    if (config.exe != EX_WIN64)
    {
        farfunc = tyfarfunc(funcsym_p->Stype->Tty) ? 2 : 1;
        dtb.nbytes(2,(char *)&farfunc);
        sz += 2;
    }

    if (config.exe == EX_WIN32)
    {   // Address of start of function
        symbol_debug(funcsym_p);
        dtb.xoff(funcsym_p,0,TYnptr);
        sz += fsize;
    }
#endif

    // Get offset of SP from BP
    // BUG: what if alloca() was used?
    spoff = cod3_spoff();
    dtb.nbytes(intsize,(char *)&spoff);
    sz += intsize;

    // Offset from start of function to return code
    dtb.nbytes(intsize,(char *)&retoffset);
    sz += intsize;

#endif

    // Generate exception-specification-table
    if (funcsym_p->Stype->Tflags & TFemptyexc)  // no exceptions thrown
    {   long x = -1L;

        dtb.nbytes(psize,(char *)&x);
        sz += psize;
    }
    else
    {   list_t tl;

        for (tl = funcsym_p->Stype->Texcspec; tl; tl = list_next(tl))
        {   type *t = list_type(tl);
            symbol *s;

            s = init_typeinfo_data(t);
            dtb.xoff(s,0,pointertype);
            sz += psize;
        }
    }
    // Append the NULL
    dtb.nzeros(psize);
    sz += psize;

#if TX86
    if (config.ehmethod == EH_DM)
#endif
    {
        // Generate the address-table
        //printf("dim of address-table = %d\n",ehpairi);
        dtb.nbytes(intsize,(char *)&ehpairi);
        sz += intsize;
        for (i = 0; i < ehpairi; i++)
        {   dtb.nbytes(intsize,(char *)&ehpair[i].offset);
            us = ehpair[i].index;
            dtb.nbytes(2,(char *)&us);
            sz += intsize + 2;
#if DEBUG
            if (debuge)
                dbg_printf("ehpair[%d] = (offset=%X, index=%d, p = %p)\n", i, ehpair[i].offset, ehpair[i].index, ehpair[i].p);
#endif
            assert(ehpair[i].offset != -1);
        }
    }

    // Generate the handler-table
    //  short number-of-try-blocks
    //  for (each try block)
    //          BP offset of catch variable
    //          short number-of-catch-blocks
    //          for (each catch block)
    //                  offset of handler from start of function
    //                  pointer to typeinfo
    ntrys = 0;
    for (b = startblock; b; b = b->Bnext)
        ntrys += (b->BC == BCtry);
#ifdef DEBUG
        if (debuge)
    dbg_printf("ntrys = %d\n",ntrys);
#endif
    //printf("ntrys = %d\n",ntrys);
    dtb.nbytes(2,(char *)&ntrys);
    sz += 2;
    for (b = startblock; b; b = b->Bnext)
    {   list_t list;
        targ_size_t cvoffset;
#if DEBUG
        int handler = 0;
#endif

        if (b->BC != BCtry)
            continue;
        b->Btryoff = sz;

        // Put out BP offset of catch variable
        symbol_debug(b->catchvar);
        cvoffset = cod3_bpoffset(b->catchvar);
        dtb.nbytes(intsize,(char *)&cvoffset);
        sz += intsize;

#if TX86
        i = b->numSucc() - 1;          // number of handlers
        assert(i > 0);
        dtb.nbytes(2,(char *)&i);
#ifdef DEBUG
        if (debuge)
            dbg_printf("cvoffset=%X ncatches=%d\n", cvoffset, i);
#endif
#else
        us = b->numSucc() - 1;         // number of handlers
        assert(us > 0);
        dtb.nbytes(2,(char *)&us);
#endif
        sz += 2;
        for (list = b->Bsucc; (list = list_next(list)) != NULL;)
        {   block *bc = list_block(list);
            symbol *s;
            targ_size_t hoffset;

            assert(bc->BC == BCcatch);
#if TX86
            hoffset = bc->Boffset - funcoffset;
#else
            hoffset = bc->Boffset;
#endif
            dtb.nbytes(intsize,(char *)&hoffset);
            sz += intsize;

            s = init_typeinfo_data(bc->Bcatchtype);
            dtb.xoff(s,0,pointertype);
            sz += psize;
#ifdef DEBUG
            if (debuge)
                dbg_printf("\tCatch #%d: offset=%X typeinfo='%s'\n", ++handler, hoffset, prettyident(s));
#endif
        }
    }

    // Generate the cleanup table
    //  prev, 1, offset from ehsym to tryblock data, 0, 0
    //  prev, 2, BP offset, 0, dtor
    //  prev, 3, BP offset of this, offset from this, dtor
    //  prev, 4, far pointer, dtor
#ifdef DEBUG
    if (debuge)
        dbg_printf("Cleanup table:\n");
#endif
    for (i = 0; i < ehstackmax; i++)
    {   Ehstack *eh;
        elem *e;
        block *tb;
        tyexcept_type type;
        symbol *s;
        targ_size_t offset;
        targ_size_t thisoff;
        long prev;
        elem *es;

        eh = &ehstack[i];
        prev = eh->prev;
        dtb.nbytes(intsize,(char *)&prev);
        sz += intsize;
        tb = eh->bl;
        e = eh->el;
        if (tb)
        {
            assert(tb->BC == BCtry);
            type = 1;
            dtb.nbytes(sizeof(type),(char *) &type);
            dtb.nbytes(intsize,(char *)&tb->Btryoff);
            dtb.nbytes(intsize,(char *)&zero);
            dtb.nbytes(fsize,(char *)&zero);
            sz += sizeof(type) + intsize + intsize + fsize;
#if DEBUG
            if (debuge)
                dbg_printf("cleanup[%d]: prev=%2d type=1 [try] offset=%p\n",
                        i, prev, tb->Btryoff);
#endif
        }
        else if (e->E1->Eoper == OPadd)
        {
            type = 3;
            es = e->E1->E1;
            assert(es->Eoper == OPvar);
            assert(e->E1->E2->Eoper == OPconst);
            thisoff = el_tolong(e->E1->E2);
            goto L1;
        }
        else if (e->E1->Eoper == OPvar)
        {
            type = 3;
            es = e->E1;
            thisoff = 0;
            goto L1;
        }
        else if (e->E1->Eoper == OPrelconst)
        {
            type = 2;
            thisoff = 0;
            es = e->E1;

        L1:
            s = es->EV.sp.Vsym;
            offset = es->EV.sp.Voffset;
            if (sytab[s->Sclass] & SCSS)        // if stack variable
            {
                dtb.nbytes(sizeof(type),(char *) &type);
                offset += cod3_bpoffset(s);
                dtb.nbytes(intsize,(char *)&offset);
                dtb.nbytes(intsize,(char *)&thisoff);
                sz += sizeof(type) + intsize + intsize;
            }
            else
            {   type = 4;
                dtb.nbytes(sizeof(type), (char *) &type);
                dtb.xoff(s,offset,pointertype);
                sz += sizeof(type) + psize;
#if TX86
                if (pointertype == TYnptr)
#endif
                {
                    dtb.nbytes(intsize,(char *)&thisoff);
                    sz += intsize;
                }
            }

#if 0
            // This function might be an inline template function that was
            // never parsed. If so, parse it now.
            if (e->EV.eop.Edtor->Sfunc->Fbody)
            {
                n2_instantiate_memfunc(e->EV.eop.Edtor);
            }
            nwc_mustwrite(e->EV.eop.Edtor);
#endif
#if TX86
            dtb.xoff(e->EV.eop.Edtor,0,LARGECODE ? TYfptr : TYnptr);
#else
            dtb.xoff(e->EV.eop.Edtor,0,TYfptr);
#endif
            sz += fsize;
#if DEBUG
            if (debuge)
                {
                char types[3][18] = { "dtor(this)", "dtor(this+offset)", "dtor(ptr)" };
                dbg_printf("cleanup[%d]: prev=%2d type=%d [%s] offset=%X thisoffset=%X dtor=%p '%s'",
                        i, prev, type, types[type-2], offset, thisoff,
                        e->EV.eop.Edtor, prettyident(e->EV.eop.Edtor));
                dbg_printf(" var='%s'\n", prettyident(s));
                }
#endif
        }
        else
        {
#ifdef DEBUG
            elem_print(e);
#endif
            assert(0);
        }
    }
#if !TX86
    ehsym->Stype->Tdim = sz;                    // Set the size into the type
#endif
    ehsym->Sdt = dtb.finish();
    outdata(ehsym);                             // output the eh data

#if TX86
    if (config.ehmethod == EH_DM)
        Obj::ehtables(funcsym_p,funcsym_p->Ssize,ehsym);
#endif

    return ehsym;
}

/********************************
 * Reset variables for next time
 */

void except_reset()
{
    // Reset for next time
    ehstacki = 0;
    ehstackmax = 0;
    ehpairi = 0;
    except_sym = NULL;
}

#endif
