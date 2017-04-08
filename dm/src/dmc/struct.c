/**
 * Implementation of the
 * $(LINK2 http://www.digitalmars.com/download/freecompiler.html, Digital Mars C/C++ Compiler).
 *
 * Copyright:   Copyright (c) 1984-1998 by Symantec, All Rights Reserved
 *              Copyright (c) 2000-2017 by Digital Mars, All Rights Reserved
 * Authors:     $(LINK2 http://www.digitalmars.com, Walter Bright)
 * License:     Distributed under the Boost Software License, Version 1.0.
 *              http://www.boost.org/LICENSE_1_0.txt
 * Source:      https://github.com/DigitalMars/Compiler/blob/master/dm/src/dmc/struct.c
 */

// Handle structure and union declarations

#if !SPP

#include        <stdio.h>
#include        <string.h>
#include        "cc.h"
#include        "token.h"               /* must be before parser.h */
#include        "parser.h"              /* for enum_TK             */
#include        "global.h"
#include        "type.h"
#include        "scope.h"
#include        "filespec.h"
#if TX86
#include        "cgcv.h"
#endif
#include        "cpp.h"
#include        "el.h"
#include        "oper.h"
//#include      "str4.h"
#if XCOFF_OBJ
#include "cgobjxcoff.h"
#endif

#define DEBUG_FWD_TYPE_DEF(theSym, title)

static char __file__[] = __FILE__;      /* for tassert.h                */
#include        "tassert.h"


STATIC type * strdcllst (Classsym *stag,int flags);
STATIC baseclass_t * struct_getbaseclass(tym_t ptrtype,symbol *stempsym,unsigned *pstructflags,unsigned flags);
STATIC void n2_parsememberfuncs(Classsym *stag,int flag);
STATIC void n2_parsefriends(Classsym *stag,list_t friendlist,char *vident);
STATIC void n2_parsenestedfriends(Classsym *stag);
STATIC targ_size_t n2_analysebase(Classsym *stag);
STATIC targ_size_t n2_structaddsize(type *t,targ_size_t size , targ_size_t *poffset);
STATIC void n2_virtbase(Classsym *s);
STATIC targ_uns getwidth(unsigned maxn);
STATIC void chkmemtyp(symbol *s);
STATIC void n2_createvptr(Classsym *stag, targ_size_t *poffset);
STATIC void n2_createsurrogatecall(Classsym *stag);
STATIC void struct_sortvtbl(Classsym *stag);
STATIC int n2_invirtlist(Classsym *stag , Funcsym *sfunc , int insert);
STATIC int struct_override(symbol *sfbase,symbol *sfder);
STATIC void n2_ambigvirt(Classsym *);
STATIC int n2_memberfunc(Classsym *stag , Funcsym *sfunc , unsigned long class_m);
STATIC void n2_adjaccess(Classsym *sder , Classsym *sbase , symbol *s , unsigned access_specifier);
STATIC void n2_static(Classsym *stag , symbol *s);
STATIC int n2_friend(Classsym *stag , type *tfriend , char *vident , unsigned long class_m , int ts);
STATIC void n2_makefriends(Classsym *sc , symbol *sfriend);
STATIC void n2_overload(Classsym *stag);
       list_t n2_copymptrlist(list_t ml);
STATIC void n2_newvtbl(baseclass_t *);
STATIC void n2_chkabstract(Classsym *stag);
STATIC int mptrcmp(mptr_t *m1 , mptr_t *m2);
STATIC unsigned long n2_memberclass(void);
STATIC symbol * n2_createfunc(Classsym *stag,const char *name,type *tret,unsigned flags,unsigned func);
STATIC void n2_createdtor(type *tclass);
STATIC void n2_createinvariant(type *tclass);
STATIC int struct_internalmember(symbol *s);

static char cpp_name_vptr[]  = "__vptr";        // virtual function table pointer
static char cpp_name_vbptr[] = "__vbptr";       // virtual base offset table pointer


/****************************
 * Do struct or union specifier.
 * Forward referencing works.
 *      struct_or_union_specifier ::=   struct identifier
 *                                      struct { struct_decl_list }
 *                                      struct identifier { struct_decl_list }
 *                                      union  identifier
 *                                      union  { struct_decl_list }
 *                                      union  identifier { struct_decl_list }
 * Input:
 *      tk =            TKclass, TKstruct or TKunion
 *      tok =           token following TKclass, TKstruct or TKunion
 * For templates:
 *      stempsym =      if !NULL, then it is the class template this is being
 *                      generated from
 *      template_argument_list  And the argument list for the template
 * Returns:
 *      pointer to type
 *      tserr if error
 */

type *stunspec(enum_TK tk, Symbol *s, Symbol *stempsym, param_t *template_argument_list)
{ char *struct_tag;
  type *t;
  tym_t tym;
  unsigned structflags;
    tym_t ptrtype = 0;
    unsigned flags = 0;
    int qualified = 0;          // !=0 if qualified name
    unsigned sct = SCTglobal | SCTnspace | SCTcover;

    //printf("stunspec(tk = %d, stempsym = %s)\n", tk, stempsym ? stempsym->Sident : "NULL");
    tym = TYstruct;
    switch (tk)
    {   case TKstruct:  structflags = 0;                break;
        case TKunion:   structflags = STRunion;         break;
        case TKclass:   flags |= 2;
                        structflags = STRclass;         break;
        default:        assert(0);
    }

    if (bl && bl->BLtyp == BLrtext)
        structflags |= STRpredef;               // a predefined struct

#if TARGET_WINDOS
  if (CPP)
  {
    switch (tok.TKval)
    {
#if TX86
        /* Look for __near/__far classes        */
        case TK_near:
                        ptrtype = TYnptr;
                        goto L6;
        case TK_far:
                        ptrtype = TYfptr;
        L6:             if (config.exe & EX_flat)       // if flat memory model
                            ptrtype = 0;                // ignore near/far
                        goto L5;
        case TK_export: structflags |= STRexport;       goto L5;
        case TK_declspec:
                    {   tym_t ty;

                        ty = nwc_declspec();
                        if (ty & mTYexport)
                            structflags |= STRexport;
                        if (ty & mTYimport)
                            structflags |= STRimport;
                        goto L5;
                    }
#endif
        L5:     stoken();
                break;

        case TKcolcol:
                sct = SCTglobal;
                goto L5;
    }
  }
#endif

  if (tok.TKval == TKident)             /* if we found an identifier    */
  {     struct_tag = alloca_strdup(tok.TKid);   // tag name
        stoken();
        if (!CPP)
            s = scope_searchinner(struct_tag,SCTglobaltag | SCTtag);    // find the struct tag
        else if (s)
        {
            // Already know the symbol
        }
        else
        {
            if (tok.TKval == TKlt ||
                tok.TKval == TKlg)              // if instance of a template
            {   Symbol *stempsym;

                stempsym = scope_search(struct_tag,sct);
                if (!stempsym)
                {
                    synerr(EM_undefined,struct_tag);
                    return tserr;
                }
                if (stempsym->Sclass != SCtemplate)
                {
                    cpperr(EM_not_class_templ,struct_tag);      // %s is not a class template
                    return tserr;
                }
                token_unget();
                if (pstate.STintemplate)
                {   type *t;

                    t = template_expand_type(stempsym);
                    stoken();
                    return t;
                }
                s = template_expand(stempsym,0);
                stoken();
                if (!s)
                    return tserr;
                /* BUG: this code really isn't right.
                 * It doesn't handle typedefs.
                 * Should be implemented using nspace_getqual().
                 * Happens to support:
                 *      class A<int>::B { ... };
                 */
                while (tok.TKval == TKcolcol)
                {
                    qualified = 1;
                    if (stoken() != TKident)
                    {   synerr(EM_ident_exp);
                        return tserr;
                    }
                    s = cpp_findmember_nest((Classsym **)&s,tok.TKid,FALSE);
                    if (!s)
                    {   synerr(EM_undefined, tok.TKid);
                        return tserr;
                    }
                    stoken();
                }
                if (tok.TKval == TKlcur)
                    goto L18;
                return s->Stype;
            }

            if (tok.TKval == TKcolcol)
            {
                qualified = 1;
                token_unget();
                token_setident(struct_tag);
                s = nspace_getqual(2);
                if (!s)
                {
                    return tserr;
                }
        L18:
                if (s->Sclass == SCstruct && tok.TKval == TKsemi)
                    return s->Stype;
                if (tok.TKval == TKsemi)
                {   token_unget();
                    token_setident(s->Sident);
                    token_unget();
                    tok.TKval = TKcolcol;
                    assert(s->Sscope);
                    return s->Sscope->Stype;
                }
            }
            else
            {
                if (stempsym)                   // if instantiation of a template
                {
                    // template class instantiations are always at global level
                    s = scope_search(struct_tag,sct);
                    assert(!s);         // not any more
                }
                else if (pstate.STclasssym)     // Check for nested class
                {
                    if (tok.TKval == TKlcur || tok.TKval == TKcolon)
                        s = n2_searchmember(pstate.STclasssym,struct_tag);
                    else
                        s = cpp_findmember(pstate.STclasssym,struct_tag,FALSE);
                    if (s)
                    {   if (s->Scover)
                            s = s->Scover;
                        if (s->Sscope == pstate.STclasssym &&
                            (s->Sflags & SFLpmask) != (pstate.STclasssym->Sstruct->access & SFLpmask) &&
                            (tok.TKval == TKlcur || tok.TKval == TKcolon || tok.TKval == TKsemi)
                            )
                            cpperr(EM_change_access2, pstate.STclasssym->Sident, s->Sident);
                    }
                }
                else
                {
                    // Find the struct tag
                    s = scope_searchinner(struct_tag, SCTcover | SCTlocal | SCTnspace | SCTglobal);
                }
                if (s)
                {
                    if (s->Sclass == SCtypedef)
                        cpperr(EM_notypedef, s->Sident);
                    if (!n2_isstruct(&s))
                    {
                        s = NULL;
                    }
                }
            }
        }
  }
  else
  {
        /* All structs must have a name, because info for them is       */
        /* stored in the symbol table. Generate a name.                 */
        char *p;

        //warerr(WM_notagname);
        p = n2_genident();
        struct_tag = alloca_strdup(p);
#if TX86
        parc_free(p);
#else
        MEM_PARF_FREE(p);
#endif
        structflags |= STRnotagname;
        s = NULL;
  }
  {
        if (tok.TKval == TKlcur
                || (CPP && tok.TKval == TKcolon)
           )                            // then it's a struct def
        {
#if 0  //&& CPP
            // I can't find a case that uses this
            if (s && s->Sscope != pstate.STclasssym)
                s = NULL;               // must be a base class
#endif
            if (s)                      /* if it exists                 */
            {   t = s->Stype;
                if (t->Tflags & TFforward)   /* if it was a forward reference */
                {
#if DEBUG_XSYMGEN

                    // move the symbol out of HEAD memory if necessary
                    if (!(config.flags2 & CFG2phgen) && ph_in_head(s))
                    {
                        unsigned sct;

                        if (CPP)
                        {   sct = SCTglobal | SCTnspace | SCTtempsym | SCTtemparg;
                            if (!stempsym)
                                // template class instantiations are always at global level
                                sct |= SCTlocal;
                        }
                        else
                            sct = SCTglobal;

                        s = move_symbol(s, (symbol **) &scope_find( sct )->root );
                        // replace struct ptr with XSYM ptr
                        if (xsym_gen) {
                            struct_t *st = struct_calloc();
                            *st = *s->Sstruct;
                            s->Sstruct = st;
                        }
                    }
#endif
                    if (CPP)
                    {
                        if ((s->Sstruct->Sflags & STRunion) != (structflags & STRunion))
                        {   synerr(EM_multiple_def,s->Sident);
                            goto Ldef;
                        }
#if TX86
                        // Retain previous setting of STRclass
                        s->Sstruct->Sflags |= structflags & (STRexport | STRimport);
#endif
                    }
                    // Set alignment to what is currently in effect
                    s->Sstruct->Sstructalign = structalign;
                }
                else
                {
                    if (s->Sstruct->Sflags & STRpredef)
                    {   int brace = 0;

                        do
                        {
                            switch (tok.TKval)
                            {   case TKlcur:
                                    brace++;
                                    break;
                                case TKeof:
                                    err_fatal(EM_eof);  // premature end of source file
                                case TKrcur:
                                    brace--;
                                    break;
                            }
                            stoken();
                        } while (brace);
                    }
                    else
                        synerr(EM_multiple_def,struct_tag);     /* already defined      */
                    return t;
                }
            }
            else
            {   /* define the symbol    */
        Ldef:
                s = n2_definestruct(struct_tag,structflags
                        ,ptrtype,stempsym,template_argument_list,1);
                t = s->Stype;
            }
          if (CPP)
          {
            //dbg_printf("struct_getbaseclass('%s')\n",s->Sident);
            s->Sstruct->Sbase = struct_getbaseclass(ptrtype,stempsym,&structflags,flags);
            stoken();                   /* skip over left curly bracket */
            strdcllst((Classsym *)s,
                    flags);

            // See if we should generate debug info for the class definition
#if SYMDEB_CODEVIEW
            if (config.fulltypes == CV4 && !eecontext.EEcompile)
                cv4_struct((Classsym *)s,1);
#endif
#if HTOD
            htod_decl(s);
#endif

            /* Put out static member data defs (after class is done, so */
            /* debug records don't point to fwd ref'd class)            */
            if (s->Sstruct->Sflags & STRstaticmems)
            {
                if (funcsym_p && !stempsym)     /* if inside function scope     */
                    cpperr(EM_local_static,s->Sident);  // cannot have static members

                if (!(config.flags2 & CFG2phgen))       /* speed up if no statics */
                {   symlist_t sl;

                    symbol_debug(s);
                    for (sl = s->Sstruct->Sfldlst; sl; sl = list_next(sl))
                    {   symbol *sm = list_symbol(sl);

                        symbol_debug(sm);
                        switch (sm->Sclass)
                        {   case SCglobal:
                                outdata(sm);
                                break;
                        }
                    }
                }
            }
          }
          else // C
          {
            stoken();                   /* skip over left curly bracket */
            strdcllst((Classsym *)s,flags);

            // See if we should generate debug info for the class definition
#if SYMDEB_CODEVIEW
            if (config.fulltypes == CV4 && !eecontext.EEcompile)
                cv4_struct((Classsym *)s,1);
#endif
#if HTOD
            htod_decl(s);
#endif
          }
         }
        else                            /* else struct reference        */
        {
            /* Determine if we should look at previous scopes for
                a definition or not.
             */
            int nestdecl = 1;
            if ((tok.TKval != TKcomma && tok.TKval != TKsemi
                && (!(s && ((sct & SCTglobal) || qualified)))
                )
                || pstate.STinexp
                || pstate.STnoparse     // if 'friend' class
               )
            {
                if (pstate.STnoparse    // if 'friend' class
                    && funcsym_p        // and a local class (C++98 9.8)
                    && !gdeclar.class_sym       // but not a nested one
                    && !qualified)              // and not qualified
                {
                    /* C++98 11.4-9 Search only innermost enclosing non-class
                     * scope
                     */
                    s = scope_searchinner(struct_tag, SCTlocal);
                    if (s && !n2_isstruct(&s))
                        s = NULL;

                    if (!s)
                    {   // Search hidden scope
                        Scope *sc = scope_find(SCTlocal);

                        assert(sc);
                        s = symbol_searchlist(sc->friends, struct_tag);
                    }

                    nestdecl = 2;
                }
                else if (CPP)
                {
                    unsigned sct;
                    Scope *sc;

                    sct = SCTmfunc | SCTlocal | SCTwith | SCTglobal | SCTnspace |
                          SCTtemparg | SCTtempsym | SCTclass | SCTcover;
                    sc = NULL;
                    while (1)
                    {
                        s = scope_searchouter(struct_tag,sct,&sc);
                        if (!s || n2_isstruct(&s))
                            break;
                    }

                    nestdecl = 0;
                    //printf("nestdecl = 0, s = %p, inexp = %d, noparse = %d\n",s,pstate.STinexp,pstate.STnoparse);
                }
                else
                    s = scope_search(struct_tag,SCTglobaltag | SCTtag);  // find the struct tag
            }

            if (!s)                     /* if tag doesn't exist         */
            {   /* create it            */
                s = n2_definestruct(struct_tag,structflags
                        ,ptrtype,stempsym,template_argument_list,nestdecl);
            }
            if (s->Sclass == SCunde)
                return tserr;
            if (CPP)
            {
                if (!n2_isstruct(&s))
                    goto L7;

                // Retain previous setting of STRclass
                //s->Sstruct->Sflags = (s->Sstruct->Sflags & ~STRclass) | (structflags & STRclass);
            }
            if (s->Sclass != SCstruct ||
                (s->Sstruct->Sflags & STRunion) != (structflags & STRunion))
            {
        L7:     synerr(EM_badtag,struct_tag);   // not a struct or union tag
                return tserr;
            }
        }
        return s->Stype;
    }
}

/*************************************
 * Parse base class list.
 * Input:
 *      flags   2 if class instead of struct
 */

#if 1
STATIC baseclass_t * struct_getbaseclass(tym_t ptrtype,symbol *stempsym,
        unsigned *pstructflags,unsigned flags)
{
    //printf("struct_getbaseclass(stempsym = %p, flags = x%x)\n", stempsym, flags);
    baseclass_t *baseclass = NULL;
    char deferparsesave = pstate.STdeferparse;
    pstate.STdeferparse = 1;

    if (tok.TKval == TKcolon)   // if a derived class
    {   int baseflags;
        int flags;
        baseclass_t **pb = &baseclass;  // ptr to end of list
        symbol *s;
        Classsym *sbase;

        do
        {
            baseflags = 0;
            while (1)
            {   stoken();
                switch (tok.TKval)
                {   case TKprivate:
                        if (baseflags & BCFpmask)
                            goto L2;
                        baseflags |= BCFprivate;
                        continue;

                    case TKprotected:
                        if (baseflags & BCFpmask)
                            goto L2;
                        baseflags |= BCFprotected;
                        continue;

                    case TKpublic:
                        if (baseflags & BCFpmask)
                        {
                         L2:
                            synerr(EM_ident_exp);               // identifier expected
                            baseflags &= ~BCFpmask;
                        }
                        baseflags |= BCFpublic;
                        continue;

                    case TKvirtual:
                        if (baseflags & BCFvirtual)
                            goto L2;
                        baseflags |= BCFvirtual;
                        continue;

                    default:
                        break;
                }
                break;
            }

            s = exp2_qualified_lookup(NULL, stempsym != NULL, &flags);
            if (!s)
            {   synerr(EM_ident_exp);   // identifier expected
                break;
            }
            if (s->Scover)
                s = s->Scover;
            if (s->Sclass != SCstruct)
            {   cpperr(EM_not_class,tok.TKid); // not a struct or class
                break;
            }
            sbase = (Classsym *)s;

            baseclass_t *b;

            // Verify that sbase is not already there
            b = baseclass_find(baseclass,sbase);
            if (b)
            {   cpperr(EM_dup_direct_base,sbase->Sident); /* duplicate base class */
                break;  /* ignore this one */
            }

            // No forward referenced classes
            if (sbase->Stype->Tflags & TFforward)
            {   cpperr(EM_fwd_ref_base,sbase->Sident);
                break;  /* ignore this one */
            }

            b = (baseclass_t *)MEM_PH_CALLOC(sizeof(baseclass_t));
            b->BCbase = sbase;
            b->BCflags = baseflags;
            if (flags & 1 && stempsym)
            {
                b->BCflags |= BCFdependent;
            }
            // Append b to end of baseclass list
            *pb = b;
            pb = &(b->BCnext);
            if (ptrtype)
            {   if (ptrtype != sbase->Sstruct->ptrtype)
                    cpperr(EM_base_memmodel,prettyident(sbase));
            }
            else
                ptrtype = sbase->Sstruct->ptrtype;
        } while (tok.TKval == TKcomma);
        if (tok.TKval != TKlcur)
            synerr(EM_lcur_exp);                // must be a definition
    }
    pstate.STdeferparse = deferparsesave;
    //if (baseclass) printf("baseclass is %s\n", baseclass->BCbase->Sident);
    return baseclass;
}
#else
STATIC baseclass_t * struct_getbaseclass(tym_t ptrtype,symbol *stempsym,
        unsigned *pstructflags,unsigned flags)
{       baseclass_t *baseclass;

        baseclass = NULL;
        if (tok.TKval == TKcolon)       // if a derived class
        {   int baseflags;
            baseclass_t **pb = &baseclass;      // ptr to end of list
            int global;

            do
            {
                global = 0;
                baseflags = 0;
                while (1)
                {   stoken();
                    switch (tok.TKval)
                    {   case TKprivate:
                            if (baseflags & BCFpmask)
                                goto L2;
                            baseflags |= BCFprivate;
                            continue;

                        case TKprotected:
                            if (baseflags & BCFpmask)
                                goto L2;
                            baseflags |= BCFprotected;
                            continue;

                        case TKpublic:
                            if (baseflags & BCFpmask)
                            {
                             L2:
                                synerr(EM_ident_exp);           // identifier expected
                                baseflags &= ~BCFpmask;
                            }
                            baseflags |= BCFpublic;
                            continue;

                        case TKvirtual:
                            if (baseflags & BCFvirtual)
                                goto L2;
                            baseflags |= BCFvirtual;
                            continue;

                        case TKcolcol:
                            global = 1;
                            stoken();
                            break;

                        default:
                            break;
                    }
                    break;
                }
                if (tok.TKval == TKident)
                {   Classsym *sbase;            /* base class           */
                    unsigned sct;

                    if (!(baseflags & BCFpmask))
                        baseflags |= (flags & 2) ? BCFprivate : BCFpublic;

                    sct = SCTglobal | SCTnspace | SCTtempsym | SCTtemparg | SCTclass | SCTcover;
                    if (!stempsym)
                        // template class instantiations are always at global level
                        sct |= SCTlocal;
                    if (global)
                        sct = SCTglobal | SCTnspace | SCTtempsym | SCTcover;
                    sbase = (Classsym *)scope_search(tok.TKid,sct);
                L4:
                    if (sbase)
                    {   baseclass_t *b;

                        switch (sbase->Sclass)
                        {
                            case SCstruct:
                                template_instantiate_forward(sbase);
                                if (stoken() == TKcolcol)
                                {   symbol *smem;

                                    stoken();
                                    if (tok.TKval == TKtemplate)
                                        stoken();       // BUG: check following ident is a template
                                    if (tok.TKval == TKident)
                                    {
                                        smem = cpp_findmember_nest(&sbase,tok.TKid,FALSE);
                                        if (smem)
                                        {
                                            sbase = (Classsym *)smem;
                                            goto L4;
                                        }
                                        else
                                            cpperr(EM_class_colcol,tok.TKid); // must be a class name
                                    }
                                    else
                                        synerr(EM_id_or_decl);  // identifier expected
                                }
                                token_unget();

                                // Verify that sbase is not already there
                                b = baseclass_find(baseclass,sbase);
                                if (b)
                                {   cpperr(EM_dup_direct_base,sbase->Sident); /* duplicate base class */
                                    goto L3;    /* ignore this one */
                                }

                                /* No forward referenced classes        */
                                if (sbase->Stype->Tflags & TFforward)
                                {   cpperr(EM_fwd_ref_base,sbase->Sident);
                                    goto L3;    /* ignore this one */
                                }

                                b = (baseclass_t *)MEM_PH_CALLOC(sizeof(baseclass_t));
                                b->BCbase = sbase;
                                b->BCflags = baseflags;
                                // Append b to end of baseclass list
                                *pb = b;
                                pb = &(b->BCnext);
                                if (ptrtype)
                                {   if (ptrtype != sbase->Sstruct->ptrtype)
                                        cpperr(EM_base_memmodel,prettyident(sbase));
                                }
                                else
                                    ptrtype = sbase->Sstruct->ptrtype;
                            L3:
                                break;

                            case SCtemplate:
                                sbase = template_expand(sbase,1);
                                goto L4;

                            case SCnamespace:
                                sbase = (Classsym *)nspace_qualify((Nspacesym *)sbase);
                                goto L4;

                            case SCalias:
                                sbase = (Classsym *)((Aliassym *)sbase)->Smemalias;
                                goto L4;

                            case SCtypedef:
                                if (n2_isstruct((symbol **)&sbase))
                                    goto L4;
                                /* FALL-THROUGH */
                            default:
                                if (sbase->Scover)
                                {   sbase = (Classsym *)sbase->Scover;
                                    goto L4;            /* try again    */
                                }
                                cpperr(EM_not_class,tok.TKid); // not a struct or class
                                break;
                        }
                    }
                    else
                        synerr(EM_undefined,tok.TKid);  /* undefined identifier */
                    stoken();
                }
                else
                    synerr(EM_ident_exp);       // identifier expected
            } while (tok.TKval == TKcomma);
            if (tok.TKval != TKlcur)
                synerr(EM_lcur_exp);            // must be a definition
        }
    return baseclass;
}
#endif


/*******************************
 * Parse struct_decl_list.
 * struct_decl_list       ::= struct_declaration { struct_declaration } "}"
 * struct_declaration     ::= type_specifier struct_declarator_list
 * struct_declarator_list ::= struct_declarator { "," struct_declarator }
 * struct_declarator      ::= [ declarator ] [ ":" const_expr ]
 * Input:
 *      stag -> the partially created struct
 *      flags
 *              1       bLocal
 *              2       this was a class, not a struct
 * Returns:
 *      t (completed)
 */

STATIC type * strdcllst(Classsym *stag,int flags)
{
    char vident[2*IDMAX + 1];
    int levelsave;
    type *typ_spec;
    targ_size_t size = 0;
    targ_size_t memsize = 0;
    unsigned memalignsize = 0;
    unsigned bit = 0;
    unsigned fieldsize = 0;
    unsigned long sflags = 0;
    unsigned access_specifier = 0;
    bool destructor;
    struct_t *st;
    targ_size_t offset;
    tym_t modifier;
    type *tclass;
    int deferparse;
    Classsym *classsymsave;
    int ts;
    unsigned long class_m;              /* mask of storage classes      */
    list_t friendlist = NULL;

    //printf("strdcllst(stag = '%s')\n", stag->Sident);
    tclass = stag->Stype;
    st = stag->Sstruct;
    offset = n2_analysebase(stag);
    if (CPP)
    {
        access_specifier = (flags & 2) ? SFLprivate : SFLpublic;
        classsymsave = pstate.STclasssym;
        pstate.STclasssym = stag;
        scope_pushclass(stag);
#if !_WINDLL
        if (configv.verbose == 2)
            dbg_printf("class %s\n",cpp_prettyident(stag));
#endif
    }

    assert(tclass);
    levelsave = level;
    level = -1;                         /* indicate member symbol table */
    while (tok.TKval != TKrcur)         /* loop through struct_declarations */
    {
        if (tok.TKval == TK_debug)
        {
            if (config.flags5 & CFG5debug)
            {
                stoken();
                // BUG: we're allowing things like:
                //      __debug private:
            }
            else
            {   token_free(token_funcbody(FALSE));
                stoken();
                continue;
            }
        }

      if (CPP)
      {
        pstate.STclasssym = stag;
        switch (tok.TKval)
        {   case TKpublic:      access_specifier = SFLpublic;   goto L9;
            case TKprivate:     access_specifier = SFLprivate;  goto L9;
            case TKprotected:   access_specifier = SFLprotected; goto L9;
            L9:
                stoken();
                chktok(TKcolon,EM_colon);
                continue;

            case TKtemplate:
                // Per C++98 14.5.2, this should be disallowed in local classes
                template_declaration(stag, access_specifier);
                continue;

            case TKfriend:
            case_TKfriend:
            {   token_t *tk,*tki;

                // Gather up friends and parse at close of class definition
                stoken();
                tk = token_funcbody(FALSE);
                for (tki = tk; 1; tki = tki->TKnext)
                {
                    if (!tki)
                    {   list_t list = NULL;

                        list_append(&list,tk);
                        n2_parsefriends(stag,list,vident);
                        break;
                    }
                    if (tki->TKval == TKlt ||
                        tki->TKval == TKlg)     // found a template, parse later
                    {
                        list_append(&friendlist,tk);
                        break;
                    }
                }
                stoken();
                continue;
            }

            case TKstatic_assert:
                parse_static_assert();
                continue;

            default:
                class_m = n2_memberclass();     /* get mask of storage classes  */
                if (class_m & mskl(SCfriend))
                {   token_unget();
                    if (class_m & mskl(SCinline))
                    {   tok.TKval = TKinline;
                        token_unget();
                    }
                    if (class_m & mskl(SCstatic))
                    {   tok.TKval = TKstatic;
                        token_unget();
                    }
                    if (class_m & mskl(SCextern))
                    {   tok.TKval = TKextern;
                        token_unget();
                    }
                    if (class_m & mskl(SCtypedef))
                    {   tok.TKval = TKtypedef;
                        token_unget();
                    }
                    if (class_m & mskl(SCexplicit))
                    {   tok.TKval = TKexplicit;
                        token_unget();
                    }
                    if (class_m & mskl(SCmutable))
                    {   tok.TKval = TKmutable;
                        token_unget();
                    }
                    if (class_m & mskl(SCthread))
                    {   tok.TKval = TKthread_local;
                        token_unget();
                    }
                    if (class_m & mskl(SCvirtual))
                        cpperr(EM_friend_sclass); // bad friend storage class
                    goto case_TKfriend;
                }
                break;

            case TKusing:               // using-declaration for class members
            {   symbol *s;

                stoken();
                s = using_member_declaration(stag);
                if (s)
                {

                    if (tyfunc(s->Stype->Tty))
                    {   Funcsym *sf;
                        Funcsym *sfn;

                        for (sf = s; sf; sf = sfn)
                        {   sfn = sf->Sfunc->Foversym;
                            sf->Sfunc->Foversym = NULL;
                            sf->Sflags = (sf->Sflags & ~SFLpmask) | access_specifier;
                            n2_addfunctoclass(stag,sf,0);
                        }
                    }
                    else
                    {
                        s->Sflags = (s->Sflags & ~SFLpmask) | access_specifier;

                        // Check for already existing member name
                        n2_chkexist(stag,s->Sident);

                        // add member s to list of fields
                        n2_addmember(stag,s);
                    }
                }
                continue;
            }
        }

        st->access = access_specifier;

        switch (tok.TKval)              // check for pascal before constructor
        {
            case TK_declspec:
                modifier = nwc_declspec();
                stoken();
                break;

            default:
                modifier = 0;
                break;
        }

        if (tok.TKval == TKcom)
        {   destructor = TRUE;
            stoken();
        }
        else
            destructor = FALSE;

        if (tok.TKval == TKident && template_classname(tok.TKid,stag))
        {   enum_TK tk;

            tk = stoken();
            if ((tk == TKlt || tk == TKlg) && stag->Sstruct->Stempsym)
            {   Classsym *s1;

                s1 = template_expand(stag->Sstruct->Stempsym,2);
                if (s1 != stag)
                {
                    tok.setSymbol(s1);
                    //token_setident(s1->Sident);
                    goto L20;
                }
                tk = stoken();
            }
            token_unget();
            token_setident(stag->Sstruct->Stempsym
                ? stag->Sstruct->Stempsym->Sident : stag->Sident);
            if (tk == TKlpar || tk == TKsemi)   // if constructor declaration
            {
             L8:
                ts = 0;
                typ_spec = tsint;
                tsint->Tcount++;
                goto L10;
            }
        }
    L20:
        if (destructor)
            goto L8;
        if (class_m & mskl(SCextern))
            pstate.STclasssym = NULL;

        unsigned long m = class_m;
        ts = declaration_specifier(&typ_spec,NULL,&m); // get declaration specifier
        class_m |= m;

        if (modifier)
        {   type_setty(&typ_spec,typ_spec->Tty | modifier);
        }
        pstate.STclasssym = stag;
      }
      else // C
      {
            type_specifier(&typ_spec); // get type specifier
        }

        while (TRUE)
        {
            if (tok.TKval == TKcolon)   /* if specifying a "hole"       */
            {   unsigned width;
                unsigned newfieldsize;

                if (CPP && class_m)
                    synerr(EM_storage_class2,"","bit field hole");      // storage class not allowed
                if (st->Sflags & STRunion)
                    synerr(EM_unnamed_bitfield);        // no holes in unions
                newfieldsize = type_size(typ_spec);
                width = getwidth(newfieldsize * 8); // get width of hole
                bit += width;           // add to current bit position
#if TARGET_LINUX || TARGET_OSX || TARGET_FREEBSD || TARGET_OPENBSD || TARGET_SOLARIS
                if (width == 0 || bit >= sizeof(int) * 8)
                {   offset += (bit+7)/8;
                    offset = alignmember(typ_spec,newfieldsize,offset);
                    fieldsize = 0;
                    bit = 0;            /* align to next boundary       */
                }
#else
                if (fieldsize != newfieldsize ||
                    width == 0 ||
                    bit >= fieldsize * 8)
                {   offset += fieldsize;
                    fieldsize = 0;
                    bit = 0;            /* align to next word           */
                }
#endif
            }
            /* Look for anonymous unions        */
            else if ((tok.TKval == TKsemi || tok.TKval == TKcomma)
                                &&
                     tybasic(typ_spec->Tty) == TYstruct
                                &&
                     typ_spec->Ttag->Sstruct->Sflags & STRnotagname
                    )
            {   list_t ml;
                list_t fldlst;
                struct_t *su;

                if (CPP && class_m)
                    synerr(EM_storage_class2,"", "anonymous unions");   // storage class not allowed

                /* This is not a bit field, so align on next int        */
                if (bit)                /* if previous decl was a field */
                {
#if TARGET_LINUX || TARGET_OSX || TARGET_FREEBSD || TARGET_OPENBSD || TARGET_SOLARIS
                    offset += (bit+7)/8;        /* advance past used bits */
#else
                    offset += fieldsize;        /* advance past field   */
#endif
                    bit = 0;
                    fieldsize = 0;
                }

                // Determine size of union, and so its offset
                memalignsize = type_alignsize(typ_spec);
                if (st->Salignsize < memalignsize)
                    st->Salignsize = memalignsize;
                memsize = type_size(typ_spec);
                if (st->Sflags & STRunion)
                {   if (size < memsize)
                        size = memsize;
                }
                else /* struct or class */
                {   size = memsize;
                    offset = alignmember(typ_spec,memalignsize,offset);
                }

                su = typ_spec->Ttag->Sstruct;
                su->Sflags |= STRanonymous;

                // For each member of the anonymous class
                fldlst = su->Sfldlst;
                su->Sfldlst = NULL;
                su->Sroot = NULL;

                for (ml = fldlst; ml; ml = list_next(ml))
                {   symbol *ms = list_symbol(ml);

                    ms->Sl = NULL;
                    ms->Sr = NULL;

                    if (tyfunc(ms->Stype->Tty))
                    {
                        if (!(ms->Sfunc->Fflags & Fgen))
                            cpperr(EM_func_anon_union,prettyident(ms)); // no function members
                        n2_addmember(typ_spec->Ttag,ms);
                        continue;
                    }

                    /* Check for already existing member name           */
                    n2_chkexist(stag,ms->Sident);

                    /* add member ms to list of fields                  */
                    n2_addmember(stag,ms);

                    if (CPP)
                        ms->Sflags &= ~SFLpmask;
                    ms->Sflags |= sflags | access_specifier;
                    if (ms->Sclass == SCmember || ms->Sclass == SCfield)
                        ms->Smemoff += offset;
                } /* for each member */
                list_free(&fldlst,FPNULL);
                if ((st->Sflags & STRunion) == 0)
                    offset += size;
                else
                {
                    sflags |= SFLskipinit;              // skip initializer
                }
            }
            else
            {   type *memtype;
                symbol *s;
                unsigned width;
                bool body;
                bool constructor;
                bool invariant;

                if (CPP && tok.TKval == TKcom)
                {   destructor = TRUE;
                    stoken();
                }
                else
                    destructor = FALSE;
            L10:
                if (CPP && modifier)
                    type_setty(&typ_spec,typ_spec->Tty | modifier);

                pstate.STdeferDefaultArg++;
                memtype = declar_fix(typ_spec,vident); // get a declarator
                pstate.STdeferDefaultArg--;
                destructor |= gdeclar.destructor;
                invariant = gdeclar.invariant;
                if (vident[0] == 0)
                {
                    if (destructor)
                        cpperr(EM_tilde_class,stag->Sident);    // X::~X() expected
                    type_free(memtype);
                    break;
                }
              if (CPP)
              {
                if (gdeclar.oper == OPMAX && (ts & 3))
                    cpperr(EM_conv_ret);        // no return type for conv operator
                constructor = template_classname(vident,stag);

                /* Ignore class specification if it's our own class     */
                if (gdeclar.class_sym == stag)
                {   if (!constructor)
                    {   destructor = strcmp(vident,cpp_name_dt) == 0;
                        constructor = destructor | (strcmp(vident,cpp_name_ct) == 0);
                    }
                    gdeclar.class_sym = NULL;
                }

                if (gdeclar.class_sym)
                {
                    if (gdeclar.constructor || gdeclar.destructor || gdeclar.invariant)
                    {   if (ts & 3 ||
                            ((gdeclar.destructor | gdeclar.invariant) && memtype->Tparamtypes)
                           )
                            cpperr(EM_bad_ctor_dtor);   // illegal ctor/dtor declaration
                        else if (gdeclar.constructor)
                        {   /* Constructors return <ref to><class>      */
                            type_free(memtype->Tnext);
                            memtype->Tnext = newpointer(gdeclar.class_sym->Stype);
                            memtype->Tnext->Tcount++;
                        }
                    }
                    constructor = FALSE;
                }
                else if (constructor | destructor | invariant)
                {   if (destructor && !constructor)          // destructor doesn't have
                    {   cpperr(EM_tilde_class,stag->Sident); // same name as class name
                        destructor = FALSE;
                    }
                    else if (
                        ts & 3 ||                       // type specifer
                        /*tybasic(memtype->Tty) != tybasic(functype) ||*/
                        !tyfunc(memtype->Tty) ||
                        /* Destructor can't have parameters     */
                        ((destructor | invariant) && memtype->Tparamtypes) ||
                        !(destructor | invariant) &&
                            class_m & (mskl(SCvirtual) | mskl(SCextern)) ||
                        /* Destructors can be virtual   */
                        (destructor | invariant) && class_m & mskl(SCextern)
                       )
                    {   cpperr(EM_bad_ctor_dtor);       // illegal ctor/dtor declaration
                        constructor = FALSE;
                        destructor = FALSE;
                        invariant = FALSE;
                    }
                    else
                    {
                        if (invariant)
                        {
                            constructor = FALSE;        // need this?
                            strcpy(vident, cpp_name_invariant);
                        }
                        else if (destructor)
                        {   constructor = FALSE;

                            /* Destructors return <int>                 */
                            /*memtype->Tnext = tsint;*/

                            strcpy(vident, cpp_name_dt);
                        }
                        else
                        {   param_t *p;

                            /* Constructors return <pointer to><class>  */
                            type_free(memtype->Tnext);
                            memtype->Tnext = newpointer(tclass);
                            memtype->Tnext->Tcount++;

                            // If this is a copy constructor
                            p = memtype->Tparamtypes;
                            if (p && (!p->Pnext || p->Pnext->Pelem)
                               )
                            {
                                // Search for arguments of type tclass
                                for (; p; p = p->Pnext)
                                {   type *tparam = p->Ptype;

                                    if (tybasic(tparam->Tty) == TYstruct &&
                                        tparam->Ttag == stag)
                                    {   cpperr(EM_ctor_X,stag->Sident); // use ref argument
                                        // Rewrite as a reference
                                        tparam = newref(tparam);
                                        tparam->Tcount++;
                                        tparam->Tnext->Tcount--;
                                        p->Ptype = tparam;
                                    }
                                }
                            }

                            if (st->Sflags & STRnoctor)
                                cpperr(EM_ctor_disallowed,vident);      // no ctor allowed
                            strcpy(vident, cpp_name_ct);
                        }
                        type_setmangle(&memtype, mTYman_cpp);
                    }
                }
                s = symbol_name(vident,SCunde,memtype);
                type_free(memtype);
                s->Sflags |= sflags;

                s->Sflags |= access_specifier;
                s->Sscope = stag;
                if (gdeclar.class_sym)
                {   if (ts || memtype->Tnext || class_m)
                        // qualifier or type is illegal in access declaration
                        cpperr(EM_access_class,cpp_prettyident(gdeclar.class_sym),vident);
                    n2_adjaccess(stag,gdeclar.class_sym,s,access_specifier);
                    goto L4;
                }

                /* Make sure operator overloads are functions   */
                if (gdeclar.oper && !tyfunc(memtype->Tty))
                {   cpperr(EM_opovl_function);  // must be a function
                    gdeclar.oper = OPunde;      /* error recovery       */
                    class_m = 0;
                    s->Sident[0] = 'x';         /* so doesn't start with __ */
                }

                /* Only now we can tell if a member is really a bit field */
                if (tok.TKval == TKcolon && !constructor)       /* if bit field */
                {
                    s->Sclass = SCfield;
                    if (class_m & ~mskl(SCmutable))
                    {   synerr(EM_storage_class2,"","bit fields");      /* bad storage class            */
                        class_m = 0;
                    }
                }

                if (class_m & mskl(SCtypedef))
                    s->Sclass = SCtypedef;
                chkmemtyp(s);           /* check data type of member    */
                body = FALSE;           /* assume no function body      */

                if (tyfunc(s->Stype->Tty) && !(class_m & mskl(SCtypedef)))
                {
                    /* Force prototype for all member functions, if no  */
                    /* prototype is given, generate one that is (void)  */
                    if (!(s->Stype->Tflags & TFprototype))
                        s->Stype->Tflags |= TFprototype | TFfixed;

                    symbol_func(s);
                    if (constructor)
                    {
                        s->Sfunc->Fflags |= Fctor;
                        st->Sflags |= STRanyctor;
                        if (class_m & mskl(SCexplicit))
                        {
                            s->Sfunc->Fflags |= Fexplicit;
                        }
                    }
                    else if (class_m & mskl(SCexplicit))
                        synerr(EM_explicit);
                    else if (destructor)
                        s->Sfunc->Fflags |= Fdtor;
                    else if (invariant)
                        s->Sfunc->Fflags |= Finvariant;
                    else if (class_m & mskl(SCstatic))
                        s->Sfunc->Fflags |= Fstatic;
                    else if (ANSI && !ts && !gdeclar.oper)
                        cpperr(EM_noreturntype, s->Sident);

                    /* Look for function down virtual list              */
                    /* If it's there, then this function is virtual too */
                    if (n2_invirtlist(stag,s,0))
                        class_m |= mskl(SCvirtual);
                    class_m &= ~mskl(SCstatic);

                    if (class_m & mskl(SCmutable))
                        synerr(EM_mutable);
                }
                else if (class_m & (mskl(SCvirtual) | mskl(SCexplicit) | mskl(SCmutable)))
                {   // bad storage class
                    if (class_m & mskl(SCexplicit))
                        synerr(EM_explicit);
                    else if (class_m & mskl(SCvirtual))
                        synerr(EM_storage_class, "virtual");

                    if (class_m & mskl(SCmutable))
                    {
                        if (class_m & (mskl(SCstatic) | mskl(SCtypedef)) ||
                            tyref(s->Stype->Tty) ||
                            s->Stype->Tty & mTYconst)
                        {
                            synerr(EM_mutable);
                        }
                        else
                        {
                            s->Sflags |= SFLmutable;
                        }
                    }

                    class_m &= ~(mskl(SCvirtual) | mskl(SCexplicit) | mskl(SCmutable));
                }

                if (class_m & mskl(SCtypedef))
                {   s->Sclass = SCtypedef;
                    if (class_m & ~mskl(SCtypedef))
                        synerr(EM_storage_class,"typedef");     /* bad storage class    */
                    /* Check for already existing member name   */
                    n2_chkexist(stag,s->Sident);
                }
                else if (class_m & mskl(SCvirtual))
                {
                    // Determine symbol for _vptr.
                    // If it doesn't exist, create it.
                    n2_createvptr(stag,&offset);

                    assert(s->Sfunc);
                    s->Sfunc->Fflags |= Fvirtual;               // mark function as virtual
                    if (s->Sfunc->Fflags & Fstatic)
                        cpperr(EM_static_virtual,s->Sident);    // static functions can't be virtual
                    body = n2_memberfunc(stag,s,class_m);       // declare the function
                    if (s->Sfunc->Fflags & Fpure)
                        st->Sflags |= STRabstract;              // mark abstract class

                    // Update virtual function list.
                    n2_invirtlist(stag,s,1);
                    goto L7;
                }
#if 0
                else if (class_m & mskl(SCinline))
                {   body = n2_memberfunc(stag,s,class_m);
                    goto L7;
                }
#endif
                else if (class_m & mskl(SCstatic))
                    n2_static(stag,s);
                else if (s->Sclass == SCfield)
                {   tym_t newty;
                    unsigned newfieldsize;

                    st->Sflags |= STRbitfields;
                    newfieldsize = type_size(memtype);
                    width = getwidth(newfieldsize * 8);
                    if (width == 0)
                        synerr(EM_decl_0size_bitfield); // no declarator allowed
                    if (newfieldsize != fieldsize ||
                        bit + width > fieldsize * 8)
                    {
                        offset += fieldsize;
                        bit = 0;        /* align to next word           */
                        fieldsize = newfieldsize;
                    }
                    goto L2;
                }
                else if (tyfunc(s->Stype->Tty))
                {   body = n2_memberfunc(stag,s,class_m);
                    goto L7;
                }
                else
                {
                    if (class_m)
                        synerr(EM_storage_class,(class_m & mskl(SCinline)) ? "inline" : "extern");      /* bad storage class    */
                    s->Sclass = SCmember;
                    if (type_struct(s->Stype) && s->Stype->Ttag == stag)
                    {   synerr(EM_mem_same_type,s->Sident,prettyident(stag));
                        type_free(s->Stype);
                        s->Stype = tserr;
                        tserr->Tcount++;
                    }
                    width = 0;          /* so bit won't screw up */
                    if (bit)            /* if previous decl was a field */
                    {
                        offset += fieldsize;    /* advance past field   */
                        bit = 0;
                        fieldsize = 0;
                    }
                L2:
                    s->Swidth = width;  /* width of member      */

                    // Allow arrays of 0 or [] length
                    type *ts = s->Stype;

                    if (type_isvla(ts))
                        synerr(EM_no_vla);      // no VLAs for struct members

                    if (!ANSI && tybasic(ts->Tty) == TYarray &&
                        ts->Tdim == 0)
                    {   memsize = 0;
                        memalignsize = type_alignsize(ts->Tnext);
                    }
                    else
                    {   memsize = type_size(ts);
                        memalignsize = type_alignsize(ts);
                    }
                    if (st->Salignsize < memalignsize)
                        st->Salignsize = memalignsize;
                    if (st->Sflags & STRunion)
                    {   struct_t *sm;
                        type *tm;

                        s->Smemoff = 0;
                        s->Sbit = 0;
                        size = (memsize > size) ? memsize : size;

                        /* Members of a union cannot have ctors or dtors */
                        tm = type_arrayroot(s->Stype);
                        if (tybasic(tm->Tty) == TYstruct &&
                            ((sm = tm->Ttag->Sstruct)->Sdtor ||
                             sm->Sflags & STRanyctor))
                            cpperr(EM_union_tors);      // member can't have dtor or ctor
                        sflags |= SFLskipinit;          // skip subsequent initializers
                    }
                    else /* struct or class */
                    {
                        offset = alignmember(s->Stype,memalignsize,offset);
                        s->Smemoff = offset;
                        s->Sbit = bit;
                        bit += width;   /* position of next member */
                        size = memsize;
                        if (s->Sclass != SCfield)
                            offset += size;
                    }
                    /* Check for already existing member name   */
                    n2_chkexist(stag,s->Sident);
                }

                /* add member s to list of fields               */
                if (!list_inlist(st->Sfldlst,s))
                {   n2_addmember(stag,s);
                }

            L7:
                if (body)               /* if saw a function body       */
                    goto L5;            /* ends this declaration loop   */
            L3: ;
              }
              else // C
              {
                s = symbol_name(vident,SCunde,memtype);
                type_free(memtype);
                s->Sflags |= sflags;

                if (tok.TKval == TKcolon)       /* if bit field                 */
                {   tym_t newty;
                    unsigned newfieldsize;

                    s->Sclass = SCfield;
                    st->Sflags |= STRbitfields;
                    newfieldsize = type_size(memtype);
                    width = getwidth(newfieldsize * 8);
                    if (width == 0)
                        synerr(EM_decl_0size_bitfield); // no declarator allowed
#if TARGET_LINUX || TARGET_OSX || TARGET_FREEBSD || TARGET_OPENBSD || TARGET_SOLARIS
                    // Very weird alignment
                    // if bits available in remaining word,
                    //   no matter what type, assign bits
                    //   i.e. char x; int y:17 are in same word
                    // But if bit field will cross word boundary
                    //   align based on type
                    // Switching types does not start new alignment
                    //   i.e. int y:17; short x:3 - The 17th bit of y is
                    //        in same byte as x.
                    if ((offset%4 * 8) + bit + width > sizeof(int) * 8)
                    {
                        offset = alignmember(memtype,newfieldsize,offset);
                        bit = 0;        /* align to next word           */
                        fieldsize = newfieldsize;
                    }
#else
                    if (newfieldsize != fieldsize ||
                        bit + width > fieldsize * 8)
                    {
                        offset += fieldsize;
                        bit = 0;        /* align to next word           */
                        fieldsize = newfieldsize;
                    }
#endif
                }
                else                    /* no bit field                 */
                {
                    s->Sclass = SCmember;
                    if (type_struct(s->Stype) && s->Stype->Ttag == stag)
                    {   synerr(EM_mem_same_type,s->Sident,prettyident(stag));
                        type_free(s->Stype);
                        s->Stype = tserr;
                        tserr->Tcount++;
                    }
                    width = 0;          /* so bit won't screw up        */
                    if (bit)            /* if previous decl was a field */
                    {
#if TARGET_LINUX || TARGET_OSX || TARGET_FREEBSD || TARGET_OPENBSD || TARGET_SOLARIS
                        offset += (bit+7)/8;    /* advance past used bits */
#else
                        offset += fieldsize;    /* advance past field   */
#endif
                        bit = 0;
                        fieldsize = 0;
                    }
                }
                chkmemtyp(s);           /* check data type of member    */
                s->Swidth = width;      /* width of member              */

                if (type_isvla(s->Stype))
                    synerr(EM_no_vla);  // no VLAs for struct members

                // Allow arrays of 0 or [] length
                if (!ANSI && tybasic(s->Stype->Tty) == TYarray &&
                    s->Stype->Tdim == 0)
                {   memsize = 0;
                    memalignsize = type_alignsize(s->Stype->Tnext);
                }
                else
                {   memsize = type_size(s->Stype);
                    memalignsize = type_alignsize(s->Stype);
                }

                if (st->Salignsize < memalignsize)
                    st->Salignsize = memalignsize;
                if (st->Sflags & STRunion)
                {   s->Smemoff = 0;
                    s->Sbit = 0;
                    size = (memsize > size) ? memsize : size;
                    sflags |= SFLskipinit;      // skip subsequent initializers
                }
                else /* struct or class */
                {
#if TARGET_LINUX || TARGET_OSX || TARGET_FREEBSD || TARGET_OPENBSD || TARGET_SOLARIS
                    // gcc does not align by type until crossing word boundary
                    if (s->Sclass != SCfield)
#endif
                        offset = alignmember(s->Stype,memalignsize,offset);
                    s->Smemoff = offset;
                    s->Sbit = bit;
#if TARGET_LINUX || TARGET_OSX || TARGET_FREEBSD || TARGET_OPENBSD || TARGET_SOLARIS
                    // now modify offset and start bit according to type size
                    while(s->Sbit > memsize*8)
                    {
                        s->Sbit -= 8;
                        s->Smemoff += 1;
                    }
#endif
                    bit += width;       /* position of next member      */
                    size = memsize;
                    if (s->Sclass != SCfield)
                        offset += size;
                }

                /* Check for already existing member name               */
                n2_chkexist(stag,vident);

                /* add member s to list of fields                       */
                n2_addmember(stag,s);
              }
            }
            if (tok.TKval == TKcomma)
            {   stoken();
                continue;
            }
            break;
        } /* while */
    L4:
        if (tok.TKval != TKsemi)
        {   synerr(EM_semi_member);             // ';' expected
            panic(TKsemi);
        }
        stoken();
    L5:
        type_free(typ_spec);
  } /* while */

  st->Sstructsize = (st->Sflags & STRunion)
                ? size                  /* size of union                */
                : (bit)
#if TARGET_LINUX || TARGET_OSX || TARGET_FREEBSD || TARGET_OPENBSD || TARGET_SOLARIS
                // gcc uses bit count instead of type for size
                        ? offset + (bit+7)/8
#else
                        ? offset + size
#endif
                        : offset;

  if (type_chksize(st->Sstructsize))    // if size exceeds 64Kb
        st->Sstructsize &= 0xFFFF;

#if CFM68K || CFMV2
  if (config.CFMOption && st->Sstructsize & 0x01)
        st->Sstructsize++;              /* no odd size structs in CFM conventions */
#endif
  //dbg_printf("Sstructsize = x%lx, Salignsize = x%x\n",st->Sstructsize,st->Salignsize);
  if (CPP)
  {
    struct_sortvtbl(stag);              // sort entrees in vtbl[]
    {   int fixdtor = cpp_dtor(tclass);
        if (fixdtor)                    // gen destructor if necessary
            n2_createdtor(tclass);      // generate a destructor

        n2_chkabstract(stag);           // determine if class is abstract
        n2_virtbase(stag);              // analyze virtual base classes
        if (fixdtor)
            cpp_fixdestructor(st->Sdtor);
    }
    if (cpp_needInvariant(tclass))
        n2_createinvariant(tclass);     // generate an invariant
    n2_overload(stag);                  // collect overload information
    n2_ambigvirt(stag);         // check for ambiguous virtual functions
    tclass->Tflags &= ~(TFforward | TFsizeunknown); // we've found the def
    scope_pop();
    pstate.STclasssym = classsymsave;   // out of class scope

    if (!st->Sctor)                     // if no constructors
    {
        if (cpp_ctor(stag))             // if we need a constructor
        {
            // Don't create X::X() unless we actually need it
            //n2_creatector(tclass);    // generate X::X()

            /* Regard a constructor for a base or member as a 'user-
               defined' constructor.
             */
            st->Sflags |= STRanyctor | STRgenctor0;
        }
        else
        {   symlist_t sl;

            // Error if no constructor but const members exist
            for (sl = st->Sfldlst; sl; sl = list_next(sl))
            {   symbol *s = list_symbol(sl);

                if ((s->Sclass == SCmember || s->Sclass == SCfield) &&
                    s->Stype->Tty & mTYconst)
                    cpperr(EM_const_mem_ctor,s->Sident);
            }
        }
    }

    n2_lookforcopyctor(stag);

#if TX86
    if ((config.fulltypes == CV4 || st->Sflags & STRexport) &&
        !(st->Sflags & STRunion))
    {
        n2_createopeq(stag,0);
        n2_createopeq(stag,2);
        n2_createcopyctor(stag,0);
    }
#endif

    if (CPP)
        n2_createsurrogatecall(stag);

    pstate.STclasssym = stag;                   // back in class scope
    scope_pushclass(stag);

    // Parse deferred default arguments for member function parameters
    for (symlist_t sml = st->Sfldlst; sml; sml = list_next(sml))
    {   symbol *sm = list_symbol(sml);

        //printf("member %s\n", sm->Sident);
        if (tyfunc(sm->Stype->Tty))
        {
            for (; sm; sm = sm->Sfunc->Foversym)
            {
                if (sm->Sclass == SCfunctempl)  // SCfuncalias?
                    continue;
                for (param_t *p = sm->Stype->Tparamtypes; p; p = p->Pnext)
                {
                    if (p->PelemToken)
                    {
                        token_unget();
                        token_markfree(p->PelemToken);
                        token_setlist(p->PelemToken);
                        p->PelemToken = NULL;
                        stoken();
                        pstate.STdefertemps++;
                        pstate.STdeferaccesscheck++;
                        pstate.STdefaultargumentexpression++;
                        p->Pelem = arraytoptr(assign_exp());
                        pstate.STdefaultargumentexpression--;
                        pstate.STdeferaccesscheck--;
                        pstate.STdefertemps--;
                        if (tok.TKval != TKcomma && tok.TKval != TKrpar)
                            token_poplist();    // try to recover from parse errors
                        stoken();
                    }
                }
            }
        }
    }

    n2_parsefriends(stag,friendlist,vident);    // parse friends

    scope_pop();
    pstate.STclasssym = classsymsave;           // out of class scope

    // If nested classes of this template are friends of other classes
    n2_parsenestedfriends(stag);

    // if defer parsing
    deferparse = (pstate.STnoparse || levelsave == -1 || pstate.STdeferparse);

    if (!deferparse)
    {   // Do defered parsing

        //printf("do deferred parsing\n");
        while (pstate.STclasslist)
        {   Classsym *sdefered = (Classsym *)list_symbol(pstate.STclasslist);

            symbol_debug(sdefered);
            list_pop(&pstate.STclasslist);      // protect against nesting
#if TX86
            n2_parsememberfuncs(sdefered,1);
#else
            if (!(sdefered->Sstruct->Sflags & STRgen))
                n2_parsememberfuncs(sdefered);
#endif
        }
    }
    level = levelsave;
    n2_classfriends(stag);                      // calculate class friends
    if (deferparse)
    {
        //printf("deferring parse of '%s'\n", stag->Sident);
        list_append(&pstate.STclasslist,stag);
    }
    else
    {   symbol_debug(stag);
        n2_parsememberfuncs(stag,0);            // parse member functions
    }

    stoken();                           /* scan past closing curly bracket */

    if (!stag->Sstruct->Stempsym) // this doesn't work when expanding templates
    {
        // Check for certain common errors
        switch (tok.TKval)
        {   case TKstruct:
            case TKunion:
            case TKclass:
            case TKtypedef:
            case TKextern:
            case TKstatic:
            case TKinline:
            case TKthread_local:
#if TX86
            case TK_declspec:
#endif
                cpperr(EM_semi_rbra,prettyident(stag)); // ';' expected
                token_unget();
                tok.TKval = TKsemi;             // insert ; into token stream
        }
    }
  }
    else
    {
        st->Sstructsize = alignmember(tclass,st->Salignsize,st->Sstructsize);
        tclass->Tflags &= ~(TFforward | TFsizeunknown); // we've found the def
        if (ANSI && !st->Sfldlst)
            synerr(EM_empty_sdl);               // can't have empty {}
        stoken();                               // scan past closing curly bracket
        level = levelsave;
    }
    file_progress();                    // report progress
    return tclass;
}

/************************
 * Determine if *ps is a struct symbol or not.
 * If it is a typedef's struct symbol, modify *ps to be the
 * real tag symbol.
 */

int n2_isstruct(symbol **ps)
{   symbol *s;
    int result = 0;

    assert(ps);
    s = *ps;
    if (s)
    {   switch (s->Sclass)
        {   case SCstruct:
                result = 1;
                break;
            case SCtypedef:
                if (type_struct(s->Stype))
                {   *ps = s->Stype->Ttag;
                    result = 1;
                }
                break;
        }
    }
    return result;
}

/*********************************
 * Define a struct symbol.
 */

Classsym * n2_definestruct(
        char *struct_tag,       // identifier
        unsigned flags          // value for Sflags
        ,tym_t ptrtype,         // type of this pointer
        symbol *stempsym,       // if instantiation of template, this is
                                // the template symbol
        param_t *template_argument_list,        // and it's arguments
        int nestdecl            // 1: if declaration might be nested
                                // 2: declare in hidden 'friends' scope
        )
{   symbol *s;
    type *t;

    //dbg_printf("n2_definestruct('%s',template '%s',nest=%d)\n",struct_tag,stempsym ? stempsym->Sident : "",nestdecl);
  if (CPP)
  {
    if (stempsym)                       // if instantiation of a template
    {
        // template class instantiations are always at global level
        s = scope_search(struct_tag,SCTglobal | SCTnspace);
        flags |= STRglobal;
        assert(!s);                     // not anymore
#if 1
        s = symbol_calloc(struct_tag);
        s->Sclass = SCstruct;
        s->Sscope = stempsym->Sscope;
#else
        s = scope_define(struct_tag, SCTglobal | SCTnspace, SCstruct);
#endif
    }
    else
    {
        int nest;                       // != 0 if create nested class
        Symbol *sf = NULL;
        Scope *sc = scope_find(SCTlocal);

        // If we should make this a nested class
        if (pstate.STclasssym && nestdecl & 1)
        {   s = NULL;
            nest = 1;
        }
        else
        {   s = scope_searchinner(struct_tag,SCTglobal | SCTnspace | SCTlocal);
            nest = 0;
            if (sc)
                sf = symbol_searchlist(sc->friends, struct_tag);
        }
        if (!funcsym_p)
            flags |= STRglobal;
        if (s)
        {   // Already defined, so create a second 'covered' definition
            s->Scover = sf ? sf : (Classsym *)symbol_calloc(struct_tag);
            s = s->Scover;
            s->Sclass = SCstruct;
#if 0
            /* If you define a variable with the same name
             * as a class, that class cannot have any
             * constructors
             * Why?
             */
            flags |= STRnoctor;         // no constructor allowed
#endif
            if (sf)
                goto Lret;
        }
        else
        {
            if (nest)
            {
                // Created nested class definition
                n2_chkexist(pstate.STclasssym,struct_tag);
                s = symbol_calloc(struct_tag);
                s->Sclass = SCstruct;
                s->Sflags |= pstate.STclasssym->Sstruct->access;
                n2_addmember(pstate.STclasssym,s);
                //printf("adding '%s' as member of '%s'\n",s->Sident,pstate.STclasssym->Sident);
            }
            else if (nestdecl & 2)
            {
                s = symbol_calloc(struct_tag);
                s->Sclass = SCstruct;
                assert(sc);
                list_prepend(&sc->friends, s);
            }
            else if (sf)
            {   // Copy reference from friends symbol table to regular one
                s = sf;
                scope_addx(s, sc);
                list_subtract(&sc->friends, sf);
                goto Lret;
            }
            else
                s = scope_define(struct_tag, stempsym ? SCTglobal | SCTnspace : SCTglobal | SCTnspace | SCTlocal,SCstruct);
        }
    }
  }
  else
  {
    if (!funcsym_p)
        flags |= STRglobal;                     // we are at global scope
    s = scope_define(struct_tag, SCTglobaltag | SCTtag,SCstruct);
  }

#if TX86 && TARGET_WINDOS
    if (config.wflags & WFexport && LARGEDATA)
        flags |= STRexport;
#endif

    s->Sstruct = struct_calloc();
    s->Sstruct->Sflags |= flags;
    s->Sstruct->Sstructalign = structalign;
    if (CPP)
    {
        s->Sstruct->ptrtype = ptrtype ? ptrtype : pointertype;
        if (stempsym)
        {   s->Sstruct->Sflags |= stempsym->Stemplate->TMflags;
            s->Sstruct->Sarglist = template_argument_list;
            s->Sstruct->Stempsym = stempsym;    // remember which template generated this
            list_append(&stempsym->Stemplate->TMinstances,s);
        }
    }
    t = type_alloc(TYstruct);
    t->Tflags |= TFsizeunknown | TFforward;
    t->Ttag = (Classsym *)s;            /* structure tag name           */
    t->Tcount++;
    s->Stype = t;
Lret:
    return (Classsym *)s;
}

/********************************
 * Up till now, the member functions have been collected only as
 * a stream of tokens. Now, parse each of them for real.
 * Input:
 *      flag    0       template symbol table already there
 *              1       create symbol table
 */

STATIC void n2_parsememberfuncs(Classsym *stag,int flag)
{   struct_t *st;
    Scope *scsave;
    int nscopes;

    // If stag is an instance of a template, we need to recreate
    // the symbol table of the actual arguments.
    //dbg_printf("n2_parsememberfuncs('%s',%d)\n",cpp_prettyident(stag),flag);
    symbol_debug(stag);
    st = stag->Sstruct;
    scsave = NULL;
    if (flag && st->Stempsym)
    {
        //dbg_printf("generating instantiation from template\n");

        // Set up scope for instantiation

        nscopes = 0;
        scsave = scope_end;
        Scope::setScopeEnd(scope_find(SCTglobal));

        nscopes = scope_pushEnclosing(st->Stempsym);

        // Turn arglist into symbol table
        template_createsymtab(st->Stempsym->Stemplate->TMptpl,st->Sarglist);
        nscopes++;

        // func_body() will push the scope stag
    }

    while (st->Sinlinefuncs)
    {   symbol *s = list_symbol(st->Sinlinefuncs);

        //printf("\tLooking at '%s'\n", s->Sident);
        symbol_debug(s);
        list_pop(&st->Sinlinefuncs);    // protect against nesting

        /* Inline functions in class template instantiations are not
         * parsed until actually referenced. This is because they
         * are allowed to contain syntax errors.
         * See test\template2.cpp test16().
         */

        if ((!st->Stempsym || !s->Sfunc->Fclass ||
            s->Sfunc->Fflags & Fvirtual) &&
            !(s->Sfunc->Fflags & Finstance) &&
            s->Sfunc->Fbody)
        {
            //printf("\t\tparsing\n");
            //assert(s->Sfunc->Fbody);
            token_markfree(s->Sfunc->Fbody);
            token_setlist(s->Sfunc->Fbody);
            s->Sfunc->Fbody = NULL;
            stoken();
            func_nest(s);
        }
    }

    if (scsave)
    {
        // Unwind scope back to global
        scope_unwind(nscopes);
        Scope::setScopeEnd(scsave);
    }
}

/****************************************
 * Instantiate a member function of a class template.
 */

void n2_instantiate_memfunc(symbol *s)
{   symbol *stempl;
    Scope *scsave;
    Classsym *stag;
    int nscopes;

    //printf("1 n2_instantiate_memfunc('%s')\n", prettyident(s));
    if (s->Sfunc->Fbody &&
        (stag = s->Sfunc->Fclass) != NULL &&
        (stempl = stag->Sstruct->Stempsym) != NULL)
    {
        //dbg_printf("2 n2_instantiate_memfunc('%s')\n", prettyident(s));
        Pstate pstatesave = pstate;
        pstate.STmaxsequence = stempl->Stemplate->TMsequence;

        token_unget();

        scsave = scope_end;
        Scope::setScopeEnd(scope_find(SCTglobal));

        nscopes = scope_pushEnclosing(stempl);

        // Turn arglist into symbol table
        template_createsymtab(stempl->Stemplate->TMptpl,stag->Sstruct->Sarglist);
        nscopes++;

        token_markfree(s->Sfunc->Fbody);
        token_setlist(s->Sfunc->Fbody);
        s->Sfunc->Fbody = NULL;
        stoken();
        func_nest(s);

        // Unwind scope back to global
        scope_unwind(nscopes);

        Scope::setScopeEnd(scsave);
        pstate.STmaxsequence = pstatesave.STmaxsequence;
        //dbg_printf("-n2_instantiate_memfunc('%s')\n", s->Sident);
        stoken();
    }
}

/********************************
 * Up till now, the friends of stag have been collected only as
 * a stream of tokens. Now, parse each of them for real.
 */

STATIC void n2_parsefriends(Classsym *stag,list_t friendlist,char *vident)
{   list_t fl;
    unsigned long class_m;              // mask of storage classes
    type *typ_spec;
    type *memtype;
    int ts;
    Pstate pstatesave = pstate;

    //printf("n2_parsefriends(stag = '%s')\n", stag->Sident);
//    pstate.STclasssym = NULL;         // not at class scope
    pstate.STnoparse = 1;               // turn off parsing of member funcs
    pstate.STinexp = 0;
    pstate.STmaxsequence = ~0u;

    for (fl = friendlist; fl; fl = list_next(fl))
    {   token_t *t = (token_t *) list_ptr(fl);

        assert(t);
        token_markfree(t);
        token_setlist(t);
        stoken();
        class_m = 0;
        ts = declaration_specifier(&typ_spec,NULL,&class_m);

    Lgetts:
        pstate.STexplicitSpecialization++;      // account for: friend void f<>(int);
        memtype = declar_fix(typ_spec,vident);
        param_free(&gdeclar.ptal);
        pstate.STexplicitSpecialization--;

        if (gdeclar.class_sym)
        {
            if (gdeclar.constructor || gdeclar.destructor)
            {   if (ts ||
                    (gdeclar.destructor && memtype->Tparamtypes)
                   )
                    cpperr(EM_bad_ctor_dtor);   // illegal ctor/dtor declaration
                else if (gdeclar.constructor)
                {   /* Constructors return <ref to><class>      */
                    type_free(memtype->Tnext);
                    memtype->Tnext = newpointer(gdeclar.class_sym->Stype);
                    memtype->Tnext->Tcount++;
                }
            }
        }

        if (n2_friend(stag,memtype,vident,class_m,ts))
            token_unget();
        else if (tok.TKval == TKcomma)
        {
            if (!vident[0])
                type_free(memtype);
            stoken();
            type_free(typ_spec);
            ts = type_specifier(&typ_spec);
            goto Lgetts;
        }
        if (!vident[0])
            type_free(memtype);
        type_free(typ_spec);
    }
    list_free(&friendlist,FPNULL);

    pstate.STmaxsequence = pstatesave.STmaxsequence;
    pstate.STclasssym = pstatesave.STclasssym;
    pstate.STinexp = pstatesave.STinexp;
    pstate.STnoparse = pstatesave.STnoparse;
}

/******************************************************
 */

STATIC void n2_parsenestedfriends(Classsym *stag)
{
    Symbol *stempsym = stag->Sstruct->Stempsym;
    Classsym *classsymsave;
    unsigned long class_m;              // mask of storage classes
    type *typ_spec;
    type *memtype;
    int ts;
    char noparsesave;
    char inexpsave;


    if (stempsym)
    {
        //printf("n2_parsenestedfriends(stag = '%s')\n", stag->Sident);
        classsymsave = pstate.STclasssym;
        pstate.STclasssym = NULL;               // not at class scope

        for (list_t nl = stempsym->Stemplate->TMnestedfriends; nl; nl = list_next(nl))
        {   TMNF *tmnf = (TMNF *)list_ptr(nl);

            //printf("\tnested friend class '%s'\n", tmnf->stag->Sident);
            token_setlist(tmnf->tdecl);
            stoken();
            class_m = 0;
            ts = declaration_specifier(&typ_spec,NULL,&class_m);
            memtype = declar_fix(typ_spec, NULL);

            if (type_struct(memtype))
            {
                n2_friend(tmnf->stag, memtype, NULL, class_m, ts);
            }
            else
            {
                cpperr(EM_friend_type); // only classes and functions can be friends
            }

            type_free(memtype);
            type_free(typ_spec);
        }

        pstate.STclasssym = classsymsave;
    }
}

/********************************
 * Analyse and build data structures based on base classes.
 * Returns:
 *      offset past base classes
 */

STATIC targ_size_t n2_analysebase(Classsym *stag)
{
    baseclass_t *b;
    baseclass_t **pb;
    baseclass_t *vbptr_base;
    struct_t *st;
    targ_size_t baseoffset;
    targ_size_t lastbaseoffset;

    if (!CPP)
        return 0;
    baseoffset = 0;
    symbol_debug(stag);
    st = stag->Sstruct;

    //dbg_printf("n2_analysebase('%s')\n",stag->Sident);
    /* Determine if we need an Svbptr.
       We need one if there are any virtual base classes.
     */
    vbptr_base = NULL;
    for (b = st->Sbase; b; b = b->BCnext)
    {
        if (b->BCflags & BCFvirtual || b->BCbase->Sstruct->Svirtbase)
            goto need_vbptr;
    }
    goto skip;

need_vbptr:
    /* If any non-virtual base classes have an Svbptr, pick first one
       in canonical order and expropriate that.
     */
    for (b = st->Sbase; b; b = b->BCnext)
    {
        if (b->BCflags & BCFvirtual)
            continue;
        if (b->BCbase->Sstruct->Svbptr)
        {   // Found one we can use
            st->Svbptr = b->BCbase->Sstruct->Svbptr;
            st->Svbptr_parent = b->BCbase;
            vbptr_base = b;             // don't know offset of b yet
            goto skip;
        }
    }

    // Allocate our own Svbptr, and place it before all the direct base classes
    {   type *t;
        symbol *s_vbptr;

        t = newpointer_share(tsint);
        s_vbptr = symbol_name(cpp_name_vbptr,SCmember,t);
        s_vbptr->Sflags |= SFLpublic;
        n2_addmember(stag,s_vbptr);
        st->Svbptr = s_vbptr;
        baseoffset = type_size(t);
        if (baseoffset > st->Salignsize)
            st->Salignsize = baseoffset;
    }

skip:

    // Determine offsets of base classes
    // Order base classes so that base classes that define virtual
    // functions come before those that don't
    lastbaseoffset = ~0;
    for (b = st->Sbase; b; b = b->BCnext)
    {
        if (b->BCflags & BCFvirtual)
            continue;
        // Do base classes that define virtual functions
        if (b->BCbase->Sstruct->Svptr)
        {
            if (!st->Sprimary)
            {   assert(!b->BCoffset);
                st->Sprimary = b;       // Sprimary is first base with a vptr
            }
            if (baseoffset == lastbaseoffset)
                baseoffset++;
            lastbaseoffset = baseoffset;
            b->BCoffset = n2_structaddsize(b->BCbase->Stype,
                b->BCbase->Sstruct->Snonvirtsize,
                &baseoffset);
        }
    }
    for (b = st->Sbase; b; b = b->BCnext)
    {
        // Also take care of alignment
        if (b->BCbase->Sstruct->Salignsize > st->Salignsize)
            st->Salignsize = b->BCbase->Sstruct->Salignsize;

        if (b->BCflags & BCFvirtual)
            continue;
        // Do base classes that don't define virtual functions
        if (!b->BCbase->Sstruct->Svptr)
        {
            if (baseoffset == lastbaseoffset)
                baseoffset++;
            lastbaseoffset = baseoffset;
            b->BCoffset = n2_structaddsize(b->BCbase->Stype,
                b->BCbase->Sstruct->Snonvirtsize,
                &baseoffset);
        }
    }

    if (vbptr_base)
        st->Svbptr_off = vbptr_base->BCbase->Sstruct->Svbptr_off + vbptr_base->BCoffset;

    /* Construct Smptrbase, list of base classes with separate vtbl[]s  */
    //dbg_printf("Constructing Smptrbase for '%s'\n",stag->Sident);
    pb = &st->Smptrbase;
    *pb = NULL;
    for (b = st->Sbase; b; b = b->BCnext)
    {   Classsym *sbase = b->BCbase;
        baseclass_t *bm;
        baseclass_t *bs;

        //dbg_printf("\tLooking at '%s'\n",sbase->Sident);
        symbol_debug(sbase);
        if (b->BCflags & BCFvirtual)
        {
            /* Skip it if it's already in Smptrbase list        */
            if (baseclass_find(st->Smptrbase,sbase))
                continue;
        }
        else
        {   /* The first occurrence is the primary base class   */
            /* (we'll use its vtbl[] and vptr)                  */
            if (!b->BCoffset &&
                sbase->Sstruct->Svptr &&
                (!st->Sprimary || st->Sprimary == b))
            {   st->Sprimary = b;
                st->Svirtual = list_link(sbase->Sstruct->Svirtual);
                st->Spvirtder = st->Svirtual
                        ? &list_next(list_last(st->Svirtual))
                        : &st->Svirtual;
                st->Svptr = sbase->Sstruct->Svptr; /* remember ptr to vtbl[] */
                assert(!st->Svptr || st->Svptr->Smemoff == 0);

                /* Primary base class does not appear in Smptrbase list unless
                   the offset to it is non-zero.  This means that this class
                   and the base cannot share "this".
                 */
                goto L1;
            }
        }

        /* sbase starts the Smptrbase list      */
        //dbg_printf("\tAdding '%s' to '%s'->Smptrbase list\n",sbase->Sident,stag->Sident);
        bm = baseclass_malloc();
        *bm = *b;
        bm->BCmptrlist = list_link(sbase->Sstruct->Svirtual);
        bm->BCflags &= ~BCFnewvtbl;
        bm->BCparent = stag;
        bm->BCpbase = NULL;
        *pb = bm;
        pb = &bm->BCnext;
        *pb = NULL;

        // Append any primary base classes of a virtual base class to
        // the Smptrbase list

        if (sbase->Sstruct->Sprimary && (b->BCflags & BCFvirtual))
        {   baseclass_t *b2;

            for (b2 = sbase->Sstruct->Sprimary; b2; b2 = b2->BCbase->Sstruct->Sprimary)
            {
                //dbg_printf("Adding virtprim '%s'\n",b2->BCbase->Sident);
                bm = baseclass_malloc();
                *bm = *b2;
                bm->BCmptrlist = list_link(sbase->Sstruct->Svirtual);
                bm->BCflags &= ~BCFnewvtbl;
                bm->BCflags |= BCFvirtprim;
                bm->BCparent = sbase;
                if (!bm->BCpbase)
                    bm->BCpbase = b;
                *pb = bm;
                pb = &bm->BCnext;
                *pb = NULL;
            }
        }
    L1:
        /* Append sbase's Smptrbase list to Smptrbase list      */
        for (bs = sbase->Sstruct->Smptrbase; bs; bs = bs->BCnext)
        {   Classsym *sbbase = bs->BCbase;

            if (bs->BCflags & (BCFvirtual | BCFvirtprim))
            {
                // Skip sbbase if it's already in Smptrbase list
                // because only 1 instance of virtual base class

                bm = baseclass_find(st->Smptrbase,sbbase);
                if (bm)
                {
                        /* Need to loop through mptrlist and see if
                           we need to "dominate" any virtual functions.
                           B::f dominates A::f if A is a base of B
                         */
                        list_t mla,mlb;

                    L2:
                        mlb = bs->BCmptrlist;
                        for (mla = bm->BCmptrlist; mla; mla = list_next(mla))
                        {
                            mptr_t *ma = list_mptr(mla);
                            mptr_t *mb = list_mptr(mlb);

                            symbol_debug(ma->MPf);
                            symbol_debug(mb->MPf);

                            //dbg_printf("c1isbaseofc2(%s,%s)\n",ma->MPf->Sscope->Sident,mb->MPf->Sscope->Sident);
                            if (c1isbaseofc2(NULL,ma->MPf->Sscope,mb->MPf->Sscope) &&
                                ma->MPf->Sscope != mb->MPf->Sscope)
                            {
                                if (!(bm->BCflags & BCFnewvtbl))
                                {   n2_newvtbl(bm);     /* create our own unique copy */
                                    goto L2;            /* new list, start over */
                                }
                                *ma = *mb;
                                /* override parent      */
                                ma->MPparent = bs->BCparent;
                            }

                            mlb = list_next(mlb);
                        }
#if 1
                        {
                        baseclass_t *bt;

                        // Skip any non-virtual bases that sbbase
                        // is a parent of
                        for (bt = bs; bs->BCnext; bs = bs->BCnext)
                        {   baseclass_t *b3;

                            if (bs->BCnext->BCflags & BCFvirtual)
                                break;
                            for (b3 = bt; b3 != bs->BCnext; b3 = b3->BCnext)
                                if (b3->BCbase == bs->BCnext->BCparent)
                                    goto La;
                            break;

                         La: ;
                        }
                        }
#endif
                        continue;
                }
            }

            //dbg_printf("\t2Adding '%s' to '%s'->Smptrbase list\n",sbbase->Sident,stag->Sident);
            bm = baseclass_malloc();
            *bm = *bs;
            bm->BCmptrlist = list_link(bs->BCmptrlist);
            bm->BCoffset += b->BCoffset;
            bm->BCflags &= ~BCFnewvtbl;
            bm->BCparent = sbase;
            if (!bm->BCpbase)
                bm->BCpbase = b;
            *pb = bm;
            pb = &bm->BCnext;
            *pb = NULL;
        }
    }
    return baseoffset;
}

/********************************
 */

STATIC targ_size_t n2_structaddsize(type *t,targ_size_t size,targ_size_t *poffset)
{   targ_size_t offset;

    offset = *poffset;
    if (size)
        offset = alignmember(t,size,offset);
    *poffset = offset + size;
    if (type_chksize(*poffset)) // if size exceeds 64Kb
        *poffset &= 0xFFFF;
    return offset;
}

/****************************************
 * This function is used during thunk generation to determine the correct
 * displacement values to multiply inherited classes on the Smptrbase list.
 * It recursively traverses to see if any of the classes on that list are
 * modified base classes so that the thunks being generated will have the
 * correct offset.
 * Input:
 *      psymParent      class that owns the virtual function being called
 *      b               list of base classes to search for the symbol
 *      bVirtuals       list of virtual base classes of the original class,
 *                      these are excluded from the search
 *      pmoffset        pointer to the offset to return
 * Returns:
 *      !=0     ???
 */

STATIC int n2_compute_moffset( symbol *psymParent, baseclass_t *b,
        baseclass_t *bVirtuals, targ_size_t *pmoffset )
{
    baseclass_t *bLoop;
    targ_size_t moffset;

    for (bLoop = b; bLoop; bLoop = bLoop->BCnext)
    {
        // If it is a virtual base of the original class, skip it
        if (baseclass_find( bVirtuals, bLoop->BCbase ))
            continue;

        if (bLoop->BCbase == psymParent)
        {   *pmoffset += bLoop->BCoffset;
            return 1;
        }
        else
        {   moffset = *pmoffset + bLoop->BCoffset;
            if (n2_compute_moffset( psymParent, bLoop->BCbase->Sstruct->Sbase,
                    bVirtuals, &moffset ))
            {
                *pmoffset = moffset;
                return 1;
            }
        }
    }
    return 0;
}

/**********************************
 * Analyse virtual base classes:
 *      o Construct Svirtbase
 *      o Add members to class that are pointers to the virtual base classes
 *      o Compute offsets to virtual base class instances, and adjust
 *        overall size of struct to account for additional size of
 *        virtual base classes
 *      o Fix the mptr.d values of the virtual function table
 */

STATIC void n2_virtbase(Classsym *stag)
{
    baseclass_t *b;
    struct_t *st;
    mptr_t *m;
    baseclass_t *b2;
    targ_size_t lastboffset;
    unsigned long flags;
    int nbases;

    //dbg_printf("n2_virtbase(%s)\n",stag->Sident);
    symbol_debug(stag);
    st = stag->Sstruct;

    /* Construct Svirtbase, list of virtual base classes        */
    assert(!st->Svirtbase);
    flags = STR0size;
    nbases = 0;
    for (b = st->Sbase; b; b = b->BCnext)
    {   baseclass_t *sl;

        flags &= b->BCbase->Sstruct->Sflags;
        nbases++;
        for (sl = b->BCbase->Sstruct->Svirtbase; 1; sl = sl->BCnext)
        {   Classsym *sb;
            baseclass_t **pb;

            /* Do virtual base classes of b, then b itself, so we do a  */
            /* depth-first traversal of the DAG                         */
            if (sl)
                sb = sl->BCbase;
            else if (b->BCflags & BCFvirtual)
                sb = b->BCbase;
            else
                break;

            for (pb = &st->Svirtbase; 1; pb = &(*pb)->BCnext)
            {
                if (!*pb)               /* end of list, so sb isn't in it */
                {
                    *pb = baseclass_malloc();
                    memset(*pb,0,sizeof(baseclass_t));
                    (*pb)->BCbase = sb;
                    (*pb)->BCflags = b->BCflags & BCFpmask;
                    break;
                }
                else if ((*pb)->BCbase == sb)   /* already in virtual list */
                    break;
            }
            if (!sl)
                break;
        }
    }
    st->Sstructsize = alignmember(stag->Stype,st->Salignsize,st->Sstructsize);
    st->Snonvirtsize = st->Sstructsize;

    /* Compute locations of virtual classes     */
    for (b = st->Svirtbase; b; b = b->BCnext)
    {
        b->BCoffset = n2_structaddsize(b->BCbase->Stype,
            b->BCbase->Sstruct->Snonvirtsize,&st->Sstructsize);
    }

    if (st->Sstructsize == 0)           /* disallow 0 sized structs     */
    {
        st->Salignsize++;
        st->Sstructsize++;
        st->Snonvirtsize++;
        st->Sflags |= STR0size;

        if (!(config.flags4 & CFG4noemptybaseopt))
        {
            // Spec says: "A base class subobject of an empty class type may have zero size."
            // See: http://www.cantrip.org/emptyopt.html
            // For:
            //  struct A {};
            //  struct B : A { A a[3]; };
            // the optimization should not be done - cannot put two objects
            // of the same type at the same offset - but VC6 does it anyway,
            // so to be compatible we do too.

//printf("empty base class\n");
//exit(EXIT_FAILURE);
            st->Snonvirtsize = 0;
        }
    }
    else
    {   if (flags && st->Sbase && st->Sstructsize == nbases)
            st->Sflags |= STR0size;
        st->Sstructsize = alignmember(stag->Stype,st->Salignsize,st->Sstructsize);
    }

    // Fix offsets of virtual primary base classes
    for (b = st->Smptrbase; b; b = b->BCnext)
    {
        targ_size_t offset = b->BCoffset;

        if (b->BCflags & BCFvirtprim)
            b->BCoffset += lastboffset;
        else if (b->BCflags & BCFvirtual)
        {   b2 = baseclass_find(st->Svirtbase,b->BCbase);
            assert(b2);
            lastboffset = b2->BCoffset - offset;
            b->BCoffset = b2->BCoffset;
        }
        else
            continue;
        //dbg_printf("offset of base '%s' was x%lx is x%lx\n",
        //      b->BCbase->Sident,offset,b->BCoffset);
    }

    /* Now that we know the offsets of the virtual base classes, we     */
    /* can fix the mptr.d values of the virtual function table.         */

    /* For each base class with it's own vtbl[]                         */
    for (b = st->Smptrbase; b; b = b->BCnext)
    {   list_t list;
        targ_size_t boffset;

        if (!(b->BCflags & (BCFvirtual | BCFvirtprim)))
            continue;

        //dbg_printf("virtual base '%s' with its own vtbl[]\n",b->BCbase->Sident);

        // Find offset of virtual base b in derived class stag
        boffset = b->BCoffset;

        /* If the position of the virtual base class changed, we need
           to generate a new vtbl.
         */
        if (!(b->BCflags & BCFnewvtbl)) /* already is a new vtbl        */
        {
            /* If any virtual function with a virtual offset            */
            for (list = b->BCmptrlist; list; list = list_next(list))
            {   m = list_mptr(list);
                if (m->MPflags & MPTRvirtual) /* if virtual offset      */
                    goto L1;
            }
            continue;                   /* no need to adjust vtbl       */

        L1:
            /* Generate a new vtbl[]    */
            n2_newvtbl(b);
        }

        /* Patch any mptr.d's   */
        for (list = b->BCmptrlist; list; list = list_next(list))
        {
            Classsym *psymParent;

            m = list_mptr(list);
            if (m->MPflags & MPTRvirtual)               /* if virtual offset    */
            {
                targ_size_t moffset;

                if (m->MPf->Sscope != stag &&
                    (psymParent = (Classsym *)m->MPf->Sscope) != 0) // if parent override
                {
                    //dbg_printf("parent '%s' override\n",psymParent->Sident);
                    moffset = 0;
                    b2 = baseclass_find( st->Svirtbase, psymParent );
                    if (!b2)
                    {
                        // If it is not a virtual base, it could still be a
                        // modified pointer base class that is on the
                        // Smptrlist.  We need to recursively traverse this
                        // list and determine the offset (if any) for
                        // psymParent on that list

                        moffset = 0;
                        n2_compute_moffset( psymParent, st->Smptrbase, st->Svirtbase, &moffset );
                    }
                    else
                        moffset = b2->BCoffset;
                }
                else
                    moffset = 0;
                //dbg_printf("m->MPf = '%s', m->d = %d, boffset = %ld, moffset = %ld\n",
                //    m->MPf->Sident,m->d,boffset,moffset);
                m->MPd = -boffset + moffset;
            }
        }
    }

    // For each virtual base class, compute the offset for its entry
    // in the vbtbl[], vbtbloff
    {   int vbtbl_offset = intsize;

        // If we share Svbptr, mark off the offsets in the old one
        // before extending the vbtbl[]
        if (st->Svbptr_parent)
        {   baseclass_t *bp;

            // for each virtual base in the shared vbtbl[]
            for (bp = st->Svbptr_parent->Sstruct->Svirtbase; bp; bp = bp->BCnext)
            {
                // Find corresponding base in stag
                b = baseclass_find(st->Svirtbase,bp->BCbase);
                assert(b);
                assert(bp->BCvbtbloff);
                b->BCvbtbloff = bp->BCvbtbloff;
                if (vbtbl_offset < b->BCvbtbloff)
                    vbtbl_offset = b->BCvbtbloff;       // find maximum
            }
            vbtbl_offset += intsize;            // offset of next slot
        }

        // Extend the vbtbl[] with the rest of the virtual base classes
        for (b = st->Svirtbase; b; b = b->BCnext)
        {
            if (!b->BCvbtbloff)                 // if not already assigned a slot
            {   b->BCvbtbloff = vbtbl_offset;
                vbtbl_offset += intsize;
            }
        }
    }

    // Construct Svbptrbase, the list of all base classes that require
    // their own vbtbl[]
    {   baseclass_t **pb;

        pb = &st->Svbptrbase;
        *pb = NULL;
        for (b = st->Sbase; b; b = b->BCnext)
        {   Classsym *sbase = b->BCbase;
            baseclass_t *bm;
            baseclass_t *bs;

            symbol_debug(sbase);
            if (!sbase->Sstruct->Svbptr)        // if no virtual bases
                continue;
            if (b->BCflags & BCFvirtual)
            {
                // Skip it if it's already in Svbptrbase list
                if (baseclass_find(st->Svbptrbase,sbase))
                    continue;
            }
            else
            {
                // If Svbptr is shared with stag's, then skip it unless it is
                // on the Smptrbase list because if it is, its vbtbl needs
                // to be filled in correctly.
                //
                if (st->Svbptr == sbase->Sstruct->Svbptr
                        && st->Svbptr_off == b->BCoffset + sbase->Sstruct->Svbptr_off
                   )
                    goto L2;
            }
            // sbase starts the Svbptrbase list
            /*dbg_printf("Adding '%s' to '%s'->Svbptrbase list\n",sbase->Sident,stag->Sident);*/
            bm = baseclass_malloc();
            *bm = *b;
            bm->BCmptrlist = NULL;
            bm->BCvtbl = NULL;
            *pb = bm;
            pb = &bm->BCnext;
            *pb = NULL;

        L2:
            // Append sbase's Svbptrbase list to Svbptrbase list
            for (bs = sbase->Sstruct->Svbptrbase; bs; bs = bs->BCnext)
            {   Classsym *sbbase = bs->BCbase;

                if (bs->BCflags & BCFvirtual)
                {
                    /* Skip it if it's already in Svbptrbase list        */
                    /* because only 1 instance of virtual base class */

                    if (baseclass_find(st->Svbptrbase,sbbase))
                        continue;
                }

                /*dbg_printf("2Adding '%s' to '%s'->Svbptrbase list\n",sbbase->Sident,stag->Sident);*/
                bm = baseclass_malloc();
                *bm = *bs;
                bm->BCmptrlist = NULL;
                bm->BCvtbl = NULL;
                bm->BCoffset += b->BCoffset;
                *pb = bm;
                pb = &bm->BCnext;
                *pb = NULL;
            }
        }
    }

    // For each virtual base class in Svbptrbase, fix the b->BCoffset to
    // what was computed in Svirtbase
    for (b = st->Svbptrbase; b; b = b->BCnext)
    {
        if (b->BCflags & BCFvirtual)
        {
            b2 = baseclass_find(st->Svirtbase,b->BCbase);
            assert(b2);
            b->BCoffset = b2->BCoffset;
        }
    }
}


/*************************
 * Get width of bit field.
 *      maxn    Maximum number of bits
 */

STATIC targ_uns getwidth(unsigned maxn)
{ targ_uns n;

  stoken();
  n = msc_getnum();
  if (n > maxn)                         /* if too wide                  */
  {     synerr(EM_bitwidth,n,maxn);
        n = maxn;
  }
  return n;
}

/**************************
 * See if we have a correct data type for a member.
 * For fields, must be int or unsigned.
 */

STATIC void chkmemtyp(symbol *s)
{ type *t;
  int ty;

  if (!s || (t = s->Stype) == NULL)
        return;
  ty = tybasic(t->Tty);                 /* type of member               */
  if (s->Sclass == SCfield && !tyintegral(ty))
        synerr(EM_bitfield,s->Sident);          // must be integral
  else
  {
        switch (ty)
        {
            case TYffunc:
            case TYfpfunc:
        #if TX86
            case TYnfunc:
            case TYnpfunc:
            case TYnsfunc:
            case TYfsfunc:
            case TYnsysfunc:
            case TYfsysfunc:
            case TYf16func:
            case TYmfunc:
            case TYifunc:
        #endif
                if (CPP)
                {
                    if (s->Sclass == SCfield)
                        synerr(EM_storage_class,"");    // bad storage class
                }
                else
                {
            case TYvoid:
                    if (s->Sclass != SCtypedef)
                        synerr(EM_bad_member_type,s->Sident); // bad member type
                }
                break;

            case TYref:
            case TYnref:
            case TYfref:
                break;

            case TYstruct:
                if (CPP && s->Sclass != SCtypedef)
                    chknoabstract(t);
                // FALL-THROUGH
            case TYarray:
                if (!CPP && ANSI && t->Tflags & TFsizeunknown)
                    synerr(EM_unknown_size,s->Sident);
                break;

            default:
                assert(tyscalar(ty));
                break;
        }
  }
}

/**********************************
 * Declare vptr member of class stag.
 * Input:
 *      *poffset        Updated to point past end of added member
 */

STATIC void n2_createvptr(Classsym *stag,targ_size_t *poffset)
{   symbol *s_vptr;
    struct_t *st = stag->Sstruct;

    if (!st->Svptr)
    {
        /* _vptr member doesn't exist. Create it with the type of:      */
        type *t;

#if 0
        /*      int (**_vptr)()                                 */
        t = type_allocn(LARGECODE ? TYffunc : TYnfunc,tsint);
        t = newpointer(t);
        t = newpointer(t);
#else
        cpp_getpredefined();            /* define s_mptr                */
#if TX86
        if (config.fulltypes == CV4)
        {   // vtshape *_vptr
            t = type_alloc(TYvtshape);
            t->Ttag = stag;
        }
        else
#endif
            t = s_mptr->Stype;          // __mptr *_vptr
        t = type_allocn(st->ptrtype,t);
#endif
        s_vptr = symbol_name(cpp_name_vptr,SCmember,t);
        s_vptr->Sflags |= SFLpublic;

        // vptr members always appear at 0 offset to this, so we
        // must adjust down the other base classes and members
        {   unsigned sz;
            symlist_t sl;
            baseclass_t *b;
            int i;
            targ_size_t offset;

            sz = type_size(t);
            if (sz > st->Salignsize)
                st->Salignsize = sz;
            offset = *poffset;
            n2_structaddsize(t,sz,poffset);
            sz = *poffset - offset;

            // Adjust up member offsets
            for (sl = st->Sfldlst; sl; sl = list_next(sl))
            {   symbol *sm = list_symbol(sl);

                symbol_debug(sm);
                switch (sm->Sclass)
                {   case SCmember:
                    case SCfield:
                        sm->Smemoff += sz;
                        break;
                }
            }
            st->Svbptr_off += sz;

            // Ajust up base class offsets
            for (i = 0; 1; i++)
            {   switch (i)
                {   case 0:     b = st->Sbase;          goto L1;
                    case 1:     b = st->Svirtbase;      goto L1;
                    case 2:     b = st->Smptrbase;      goto L1;
                    case 3:     b = st->Svbptrbase;     goto L1;
                    L1:
                        for (; b; b = b->BCnext)
                        {   b->BCoffset += sz;
                        }
                        continue;
                    default:
                        break;
                }
                break;
            }
        }
        n2_addmember(stag,s_vptr);
        st->Svptr = s_vptr;
    }
}

/************************************
 * Return !=0 if functions s1 and s2 have the same name and type.
 */

STATIC int n2_funccmp(symbol *s1,symbol *s2)
{
    return (strcmp(s1->Sident,s2->Sident) == 0 &&
            cpp_funccmp(s2, s1));
}

/*****************************************
 * Sort order in which virtual functions will appear in vtbl[].
 */

STATIC void struct_sortvtbl(Classsym *stag)
{
#if TX86
    /*  The sorting order is in declaration order, except that overloaded
        functions are all grouped together, appearing where the first of the
        overloaded functions was declared, and the ordering of the overloaded
        functions within that group is the reverse of declaration order.
     */

    mptr_t *m;
    struct_t *st = stag->Sstruct;
    symbol *svirt;
    list_t list;
    list_t listn;
    list_t *plhead;
    list_t *pl;

    //if (stag->Sclass != SCstruct) *(char *)0=0;
    assert(stag->Sclass == SCstruct);
    if (!st->Svirtual)
        return;
    //printf("Sorting Spvirtder for '%s'\n",stag->Sident);
    plhead = st->Spvirtder;
    assert(plhead);
    list = *plhead;
    *plhead = NULL;
    for (; list; list = listn)
    {
        listn = list_next(list);
        m = list_mptr(list);
        svirt = m->MPf;
        for (pl = plhead; 1; pl = &list_next(*pl))
        {
            if (*pl)
            {
                if (strcmp(svirt->Sident,list_mptr(*pl)->MPf->Sident) == 0)
                {   // Overloaded function, place at head of overloads
                    list_next(list) = *pl;
                    *pl = list;
                    break;
                }
            }
            else
            {   // Not an overloaded function, append to end of list
                *pl = list;
                list_next(list) = NULL;
                break;
            }
        }
    }
#endif
}

/**************************************
 * Input:
 *      insert  if !=0, then insert sfunc into virtual list, otherwise
 *              just report on it.
 * Returns:
 *      !=0     if function that matches sfunc is already in the virtual list
 *      0       no match
 */

STATIC int n2_invirtlist(Classsym *stag,Funcsym *sfunc,int insert)
{   list_t list;
    Funcsym *svirt;
    baseclass_t *b;
    mptr_t *m;
    int result = 0;
    struct_t *st = stag->Sstruct;

    /* Create a different vtbl[] from the primary base class's vtbl[]   */
    if (insert)
    {
        b = st->Sprimary;
        if (b && !(b->BCflags & BCFnewvtbl))
        {   list_free(&st->Svirtual,FPNULL);
            st->Svirtual = n2_copymptrlist(b->BCbase->Sstruct->Svirtual);
            st->Spvirtder = st->Svirtual
                    ? &list_next(list_last(st->Svirtual))
                    : &st->Svirtual;
            b->BCflags |= BCFnewvtbl;
        }
    }

    // Look down primary list
    svirt = NULL;
    for (list = st->Svirtual; list; list = list_next(list))
    {
        m = list_mptr(list);
        svirt = m->MPf;
        symbol_debug(svirt);
        if (n2_funccmp(sfunc,svirt))
        {
            result = 1;
            if (insert)
            {
                if (struct_override(svirt,sfunc))
                    m->MPflags |= MPTRcovariant;
                m->MPf = sfunc;
                goto L1;
            }
            else
                goto L2;
        }
    }

    if (insert)
    {
        /* Append to primary list of mptr's     */
        m = (mptr_t *) MEM_PH_CALLOC(sizeof(mptr_t));
        m->MPf = sfunc;
        if (!st->Spvirtder)
            st->Spvirtder = &st->Svirtual;
        list_append(st->Spvirtder,m);
    }

L1:
    /* Look down vtbl[]s of base classes        */
    for (b = st->Smptrbase; b; b = b->BCnext)
    {   int n;

        svirt = NULL;
        n = 0;
        for (list = b->BCmptrlist; list; list = list_next(list), n++)
        {   m = list_mptr(list);
            svirt = m->MPf;
            symbol_debug(svirt);
            if (n2_funccmp(sfunc,svirt))
            {
                result = 1;
                if (insert)
                {   if (!(b->BCflags & BCFnewvtbl))
                    {
                        n2_newvtbl(b);
                        m = list_mptr(list_nth(b->BCmptrlist,n));
                    }
                    if (struct_override(svirt,sfunc))
                        m->MPflags |= MPTRcovariant;
                    m->MPf = sfunc;

                    if (b->BCflags & BCFvirtual)
                        /* Virtual base class offsets are computed later
                           in n2_virtbase().
                         */
                        m->MPflags |= MPTRvirtual;
                    else
                        m->MPd = -b->BCoffset;
                }
                break;
            }
        }
    }
L2:
#if TX86
    if (!result && sfunc->Sfunc->Fflags & Fvirtual)
        sfunc->Sfunc->Fflags |= Fintro; // an 'introducing' function
#endif
    return result;
}

/*********************************************
 * Determine if overriding virtual function is ill-formed.
 * C++98 10.3
 * Input:
 *      sfbase          overridden function
 *      sfder           overriding function
 */

int type_covariant(type *t1, type *t2)
{
    tym_t ty1,ty2;
    type *tb;
    type *td;
    elem *e;
    int result = 0;
    int access;

    //printf("type_covariant()\n");
    //type_print(t1);
    //type_print(t2);
    ty1 = t1->Tty;
    ty2 = t2->Tty;
    if (ty1 != ty2 || (!tyref(ty1) && !typtr(ty1)))
        goto Lerr;

    td = t1->Tnext;
    tb = t2->Tnext;
    if (!type_struct(td) || !type_struct(tb))
        goto Lerr;

    // Can't add cv qualifications
    if ((td->Tty & (mTYconst | mTYvolatile)) & ~(tb->Tty & (mTYconst | mTYvolatile)))
        goto Lerr;

    // C++98 10.3-5
    // tb must be an unambiguous direct or indirect base class of td,
    // and accessible in the class of the overriding function (sder).
    e = el_longt(newpointer(td),0);
    access = c1isbaseofc2(&e,tb->Ttag,td->Ttag);
    if (!access)
        goto Lerr2;
    if (access & BCFprivate)
    {
        // It's allowable if enclosing class is a friend of td
        if (!cpp_classisfriend(pstate.STclasssym, td->Ttag))
        {
            goto Lerr2;
        }
    }

    // If e is a pointer conversion, we need to replace sder with a
    // thunk that calls sder.
    if (e->Eoper != OPconst)
        result = 1;

    el_free(e);
    //printf("type_covariant: %d\n", result);
    return result;

  Lerr2:
    el_free(e);
  Lerr:
    //printf("type_covariant: 2\n");
    return 2;
}

/************************************************
 */

STATIC int struct_override(symbol *sfbase,symbol *sfder)
{   type *t1 = sfder->Stype;
    type *t2 = sfbase->Stype;
    tym_t ty1,ty2;
    int result = 0;

    //printf("struct_override('%s')\n", sfbase->Sident);
    if (!typematch(t1,t2,4))            // match types, ignoring mTYexport
    {
        ty1 = t1->Tty & ~(mTYexport | mTYimport | mTYnaked);
        ty2 = t2->Tty & ~(mTYexport | mTYimport | mTYnaked);

        if (ty1 != ty2 || !tyfunc(ty1))
            goto Lerr;

        if ((t1->Tflags & TFfixed) != (t2->Tflags & TFfixed))
            goto Lerr;

        if (!paramlstmatch(t1->Tparamtypes,t2->Tparamtypes))
            goto Lerr;

        if (!typematch(t1->Tnext, t2->Tnext, 4))
        {
            result = type_covariant(t1->Tnext, t2->Tnext);
            if (result == 2)
            {   //printf("test2\n");
                goto Lerr;
            }
        }
    }

    // Check this only if EH is turned on, and not if generated function
    if (config.flags3 & CFG3eh && !(sfder->Sfunc->Fflags & Fgen))
    {
        /* C++98 15.4-3: "If a virtual function has an exception-specification,
         * all declarations, including the definition, of any function
         * that overrides that virtual function in any derived class shall only
         * allow exceptions that are allowed by the exception-specification
         * of the base class virtual function."
         */
        if (!t1isSameOrSubsett2(sfder->Stype, sfbase->Stype))
            goto Lerr;
    }

    return result;

  Lerr:
    err_override(sfbase,sfder);
    return 0;
}

/******************************************
 * Pretty-print virtual function tables.
 */

void n2_prettyprint(Classsym *sder)
{
#ifdef DEBUG
    struct_t *st;
    symbol *sfunc;
    symbol *svirt;
    baseclass_t *b,*b2;
    mptr_t *m;
    list_t list,list2,list3;

    symbol_debug(sder);
    printf("==================== %s ==========================\n", sder->Sident);
    st = sder->Sstruct;

    printf("Svirtual:\n");
    for (list = st->Svirtual; list; list = list_next(list))
    {   m = list_mptr(list);
        svirt = m->MPf;
        printf("\t'%s', flags = %x\n", cpp_prettyident(svirt), m->MPflags);
    }
    printf("\n");

    printf("Sbase:\n");
    for (b = st->Sbase; b; b = b->BCnext)
    {   printf("\t%s, flags=%x\n", b->BCbase->Sident, b->BCflags & BCFvirtual);
    }
    printf("\n");

    printf("Svirtbase:\n");
    for (b = st->Svirtbase; b; b = b->BCnext)
    {   printf("\t%s, flags=%x\n", b->BCbase->Sident, b->BCflags & BCFvirtual);
    }
    printf("\n");

    printf("Svbptrbase:\n");
    for (b = st->Svbptrbase; b; b = b->BCnext)
    {   printf("\t%s, flags=%x\n", b->BCbase->Sident, b->BCflags & BCFvirtual);
    }
    printf("\n");

    printf("Smptrbase:\n");
    for (b = st->Smptrbase; b; b = b->BCnext)
    {   printf("\t%s, flags=%x\n", b->BCbase->Sident, b->BCflags & BCFvirtual);
        printf("\t\tBCmptrlist:\n");
        for (list = b->BCmptrlist; list; list = list_next(list))
        {   m = list_mptr(list);
            svirt = m->MPf;
            printf("\t\t'%s', virtual = %x\n", cpp_prettyident(svirt), m->MPflags & MPTRvirtual);
        }
    }
    printf("\n");
#endif
}

/**************************************
 * Look at virtual functions, and see that we don't have any
 * unresolved ambiguities. An ambiguity would be a virtual function f()
 * defined in more than one base class, but not in the derived class.
 * Input:
 *      sder    derived class
 */

#if 0   // They are only ambiguous if actually referenced

STATIC void n2_ambigvirt(Classsym *sder)
{
    struct_t *st;
    symbol *sfunc;
    symbol *svirt;
    baseclass_t *b,*b2;
    mptr_t *m;
    list_t list,list2,list3;

    symbol_debug(sder);
    //dbg_printf("n2_ambigvirt('%s')\n",sder->Sident);
    st = sder->Sstruct;

    /* Look down vtbl[]s of base classes        */
    list = st->Svirtual;
    for (b = st->Smptrbase; b; b = b->BCnext)
    {
        //printf("st->Smptrbase: %s, %x\n", b->BCbase->Sident, b->BCflags & BCFvirtual);
        for (; list; list = list_next(list))
        {   m = list_mptr(list);
            svirt = m->MPf;
            symbol_debug(svirt);

            /* See if svirt is in rest of base classes  */
            //dbg_printf("svirt = '%s'\n", cpp_prettyident(svirt));
            //if (m->MPflags & MPTRvirtual) printf("\tvirtual\n");
            for (b2 = b; b2; b2 = b2->BCnext)
            {
                //printf(" base: %s, %x\n", b->BCbase->Sident, b->BCflags & BCFvirtual);
                for (list2 = b2->BCmptrlist; list2; list2 = list_next(list2))
                {   m = list_mptr(list2);
                    sfunc = m->MPf;
                    symbol_debug(sfunc);

                    //dbg_printf("sfunc = '%s'\n", cpp_prettyident(sfunc));
                    //if (m->MPflags & MPTRvirtual) printf("\tvirtual\n");
                    if (sfunc != svirt && n2_funccmp(sfunc,svirt))
                    {
                        /* We have an ambiguous function. Check to see
                           if the ambiguity was resolved by overriding
                           the function by one in the derived class.
                         */
                        //dbg_printf("ambiguous\n");
                        for (list3 = st->Svirtual; list3; list3 = list_next(list3))
                        {   symbol *s;

                            m = list_mptr(list3);
                            s = m->MPf;
                            symbol_debug(s);
                            if (s->Sscope == sder &&
                                n2_funccmp(sfunc,s))
                                goto L1;                /* resolved     */
                        }
                        err_ambiguous(svirt,sfunc);     /* ambiguous reference  */
                    }
                }
            }
        L1: ;
        }
        list = b->BCmptrlist;
        //printf("switching to b->BCmptrlist of base %s\n", b->BCbase->Sident);
    }
}

#else

STATIC void n2_ambigvirt(Classsym *sder)
{
    struct_t *st;
    symbol *s,*s3;
    baseclass_t *b,*b2,*b3;
    mptr_t *m,*m3;
    list_t list,list3;

    symbol_debug(sder);
    //dbg_printf("n2_ambigvirt('%s')\n",sder->Sident);
    st = sder->Sstruct;

    for (b = st->Smptrbase; b; b = b->BCnext)
    {
        //printf("st->Smptrbase: %s, %x\n", b->BCbase->Sident, b->BCflags & BCFvirtual);
        if (!(b->BCflags & BCFvirtual))
            continue;

        for (b2 = b->BCnext; b2; b2 = b2->BCnext)
        {
            for (b3 = b2->BCbase->Sstruct->Smptrbase; b3; b3 = b3->BCnext)
            {
                if (b3->BCbase == b->BCbase)
                {
                    /* One virtual base with different vtbl[]s.
                     * C++98 10.3-10 If entries aren't the same,
                     * then must have final overrider.
                     */
                    // Step through each vtbl[] entry for b and b3
                    for (list  = b->BCmptrlist,
                         list3 = b3->BCmptrlist;
                         list;
                         list  = list_next(list),
                         list3 = list_next(list3))
                    {   m = list_mptr(list);
                        s = m->MPf;
                        symbol_debug(s);

                        m3 = list_mptr(list3);
                        s3 = m3->MPf;
                        symbol_debug(s3);

                        if (s == s3)
                            continue;

                        int x = c1isbaseofc2(NULL, s->Sscope, b->BCbase);
                        int x3 = c1isbaseofc2(NULL, s3->Sscope, b->BCbase);
                        if ((x && !(x & BCFvirtual)) ||
                            (x3 && !(x3 & BCFvirtual)))
                            continue;

                        /* Error if no final overrider in sder class
                         */
                        for (list_t list4 = st->Svirtual; list4; list4 = list_next(list4))
                        {   mptr_t *m4 = list_mptr(list4);
                            symbol *s4 = m4->MPf;

                            symbol_debug(s4);
                            if (s4->Sscope == sder &&
                                n2_funccmp(s,s4))
                                goto L1;                /* resolved     */
                        }
                        err_ambiguous(s, s3);   /* ambiguous reference  */
                      L1:
                        ;
                    }
                    break;
                }
            }
        }
    }
}

#endif


/*****************************
 * Declare a member function.
 * Input:
 *      stag            class symbol
 *      sfunc           function symbol
 * Returns:
 *      !=0             there was a function body
 *      0               no function body
 */

STATIC int n2_memberfunc(Classsym *stag,Funcsym *sfunc,unsigned long class_m)
{   enum SC mclass;
    int body;
    struct_t *st;

    if (gdeclar.class_sym)              /* if class was specified       */
    {   cpperr(EM_decl_other_class,gdeclar.class_sym->Sident); // declaring mem of another class
        gdeclar.class_sym = NULL;       /* forget we saw it             */
    }

    mclass = (enum SC) ((class_m & mskl(SCinline)) ? SCinline : SCextern);
    sfunc->Sclass = mclass;             /* force current storage class  */

    //dbg_printf("n2_memberfunc: %s::%s, class = %d\n",stag->Sident,sfunc->Sident,mclass);

    // Cannot have a template parameter turn a member into a function
    if (sfunc->Stype->Tcount > 1 && sfunc->Stype->Tflags & TFdependent)
        cpperr(EM_acquire_function, sfunc->Sident);

    n2_addfunctoclass(stag,sfunc,0);
    st = stag->Sstruct;
    body = funcdecl(sfunc,mclass,TRUE,&gdeclar);
    if (body)
        list_append(&st->Sinlinefuncs,sfunc);
    else if (mclass != SCinline && !(st->Sflags & STRvtblext) &&
        !(sfunc->Sfunc->Fflags & Fpure)
                // non virtual functions are just as unique as virtual ones
          && (sfunc->Sfunc->Fflags & Fvirtual)
         )
    {
        // Generate vtbl[] when we see the definition for sfunc
        // (sfunc is the first non-inline non-pure virtual member function)
        //dbg_printf("'%s' is the vtblgen function\n",cpp_prettyident(sfunc));
        sfunc->Sfunc->Fflags3 |= Fvtblgen;
        st->Sflags |= STRvtblext;
    }
    return body;
}

/*****************************************
 * Add function to a class.
 * Input:
 *      flags   1 means template instance
 */

void n2_addfunctoclass(Classsym *stag,Funcsym *sfunc, int flags)
{   symbol *sp;
    struct_t *st;
    func_t *f;
    type *tfunc;

#if 0
    printf("n2_addfunctoclass(stag = '%s', sfunc = '%s', %p, flags = %d)\n", stag->Sident, sfunc->Sident, sfunc, flags);
    symbol_print(sfunc);
#endif

    symbol_debug(stag);
    assert(stag->Sclass == SCstruct);
    symbol_debug(sfunc);

    tfunc = sfunc->Stype;
    type_debug(tfunc);
    f = sfunc->Sfunc;
    assert(tyfunc(tfunc->Tty) && f);
    f->Fflags |= Foverload | Ftypesafe;

    st = stag->Sstruct;
    f->Fclass = stag;

    /* See if symbol is already defined         */
    sp = n2_searchmember(stag,sfunc->Sident);
    if (sp)
    {
        symbol_debug(sp);
        if (!tyfunc(sp->Stype->Tty))    // can only overload with funcs
        {   synerr(EM_multiple_def,sp->Sident); // already declared
            goto L1;
        }
        else
        {   Funcsym *s;
            Funcsym **ps;
            int hasstatic = 0;

            if (f->Fflags & Fstatic)
                hasstatic = 1;

            // Look for match with existing function
            for (ps = &sp; (s = *ps) != NULL; ps = &(*ps)->Sfunc->Foversym)
            {   symbol_debug(s);
                assert(s->Sfunc);
                //printf("comparing with '%s'\n", s->Sident);
                //symbol_print(s);
                if (s->Sclass != sfunc->Sclass &&
                    (s->Sclass == SCfunctempl || sfunc->Sclass == SCfunctempl))
                    continue;

                /* C++98 13.1-2
                 * Member function declarations with the same name and the same
                 * parameter types cannot be overloaded if any of them is a
                 * static member function declaration (9.4). Likewise, member
                 * function template declarations with the same name, the same
                 * parameter types, and the same template parameter lists cannot
                 * be overloaded if any of them is a static member function
                 * template declaration. The types of the implicit object
                 * parameters constructed for the member functions for the
                 * purpose of overload resolution (13.3.1) are not considered
                 * when comparing parameter types for enforcement of this rule.
                 */
                if (s->Sfunc->Fflags & Fstatic)
                    hasstatic = 1;
                if (hasstatic && paramlstmatch(sfunc->Stype->Tparamtypes, s->Stype->Tparamtypes))
                    goto Lerr;

                if (cpp_funccmp(sfunc, s))
                {
                    //if (s->Sclass == SCfunctempl && sfunc->Sclass != SCfunctempl)
                        //continue;
                    if (s->Sclass == SCfuncalias)
                    {   if (s->Sfunc->Fflags3 & Foverridden)
                            continue;           // overridden already, ignore
                        if (sfunc->Sclass == SCfuncalias)
                        {
                            if (s->Sfunc->Falias == sfunc->Sfunc->Falias)
                                goto Lerr;
                        }
                        else
                        {   // Override previous using-declaration
                            s->Sfunc->Fflags3 |= Foverridden;   // override it
                        }
                    }
                    else if (flags && !(s->Sfunc->Fflags & Finstance))
                        ;
                    else
                    {   if (sfunc->Sclass == SCfuncalias)
                        {
                            sfunc->Sfunc->Fflags3 |= Foverridden;
                        }
                        else
                        {
                        Lerr:
#if 0
                            printf("\n\ntfunc:\n");
                            symbol_print(sfunc);
                            printf("s %p:\n", s);
                            symbol_print(s);
                            printf("sfunc instance = %x\n", sfunc->Sfunc->Fflags & Finstance);
                            printf("s     instance = %x\n", s->Sfunc->Fflags & Finstance);
#endif
                            synerr(EM_multiple_def,prettyident(s));     // already declared
                            break;
                        }
                    }
                }
            }

            // Append to list of overloaded functions
            *ps = sfunc;
            sfunc->Sfunc->Fflags |= Fnotparent; /* sfunc is not first in list */
        }
        sfunc->Sscope = stag;
    }
    else
    {
        n2_chkexist(stag,sfunc->Sident);        /* make sure it doesn't exist   */
    L1:
        /* Append sfunc to member list for class        */
        n2_addmember(stag,sfunc);
    }

    assert(tfunc->Tflags & TFprototype);

    if (f->Fflags & Fdtor)
        st->Sdtor = sfunc;
    /* There may be overloaded constructors     */
    else if (f->Fflags & Fctor)
    {   if (!st->Sctor)
            st->Sctor = sfunc;
    }
    else if (f->Fflags & Finvariant)
        st->Sinvariant = sfunc;
}

/************************************
 * If we are friends of any other classes, go through all the member
 * functions making them friend functions of those classes.
 */

void n2_classfriends(Classsym *stag)
{   struct_t *st;
    symlist_t sml;

    //printf("n2_classfriends(stag = '%s')\n", stag->Sident);
    symbol_debug(stag);
    st = stag->Sstruct;
    if (st->Sclassfriends)
    {
        // For each member
        for (sml = st->Sfldlst; sml; sml = list_next(sml))
        {   symbol *sm = list_symbol(sml);

            symbol_debug(sm);
            if (tyfunc(sm->Stype->Tty))
            {

                do
                {   func_t *f = sm->Sfunc;

                    symbol_debug(sm);
                    if (sm->Sclass != SCfunctempl && sm->Sclass != SCfuncalias)
                    {   symlist_t sl;

                        // For each class that tclass is a friend of,
                        // make the member function sfunc a friend of it.
                        for (sl = st->Sclassfriends; sl; sl = list_next(sl))
                        {   symbol_debug(list_symbol(sl));
                            list_append(&f->Fclassfriends,list_symbol(sl));
                        }
                    }
                    sm = f->Foversym;
                } while (sm);
            }
        }
        list_free(&st->Sclassfriends,FPNULL);
    }
}

/*****************************
 * Check for existing member name.
 * Issue diagnostic if it does.
 */

void n2_chkexist(Classsym *stag,char *name)
{   symbol *s;

    s = n2_searchmember(stag,name);
    if (s && s->Sclass != SCstruct && s->Sclass != SCenum)
        synerr(EM_multiple_def,name);
    else if (CPP)
    {   baseclass_t *b;

        for (b = stag->Sstruct->Sbase; b; b = b->BCnext)
        {
            if (symbol_searchlist(b->BCpublics,name))
            {   synerr(EM_multiple_def,name);
                break;
            }
        }
    }
}

/*****************************
 * Add member to struct.
 */

void n2_addmember(Classsym *stag,symbol *smember)
{
    //printf("n2_addmember('%s','%s')\n",stag->Sident,smember->Sident);
    if (CPP)
    {   smember->Sscope = stag;

        if (scope_search(smember->Sident, SCTtempsym))
            cpperr(EM_template_parameter_redeclaration, smember->Sident);
    }

    // We keep the symbol twice, once in a list so that we can sequentially
    // access all members, and once in a tree for fast lookup.
    list_append(&stag->Sstruct->Sfldlst,smember);
#if !TX86
    if (CPP && scope_end->sctype & SCTclass && stag == scope_end->root)
        scope_add( smember, SCTclass );
    else
#endif
    symbol_addtotree(&stag->Sstruct->Sroot,smember);
}

/*****************************
 * Search for name in struct.
 */

symbol *n2_searchmember (Classsym *stag, const char *name)
{
    //printf("n2_searchmember('%s','%s')\n",stag->Sident,name);
    return findsy(name,stag->Sstruct->Sroot);
}

/*****************************
 * Search for name in struct.
 */

symbol *struct_searchmember (const char *name, Classsym *stag)
{
    pstate.STstag = stag;
    return cpp_findmember_nest(&pstate.STstag,name,2);
}

/*****************************
 * Adjust access to member of a base class
 * Input:
 *      sder                    derived class
 *      sbase                   base class
 *      s                       symbol of member of base class
 *      access_specifier        SFLprivate, SFLprotected or SFLpublic
 */

STATIC void n2_adjaccess(Classsym *sder,Classsym *sbase,symbol *s,unsigned access_specifier)
{   struct_t *st;
    baseclass_t *b;
    symbol *sb;
    unsigned access_s;

    //printf("n2_adjaccess(sder = '%s', sbase = '%s', s = '%s')\n", sder->Sident, sbase->Sident, s->Sident);
    st = sder->Sstruct;
    b = baseclass_find_nest(st->Sbase,sbase);
    if (!b)
    {
        cpperr(EM_base_class, sbase->Sident);   // must be a base class
        goto L2;
    }

    /* Find existing access to member from base class   */
    sb = cpp_findmember(sbase,s->Sident,1);
    if (sb)
    {
        access_s = cpp_findaccess(sb,sbase);

        /* Check for already existing member name       */
        //if (n2_searchmember(sder,s->Sident))
        //    cpperr(EM_derived_class_name,s->Sident);  // derived member with same name

        /* If sb is an overloaded function, all the functions must
           have the same access level. ARM 11.3
         */
        if (tyfunc(sb->Stype->Tty))
        {   unsigned as;
            symbol *ss;

            as = sb->Sflags & SFLpmask;
            for (ss = sb->Sfunc->Foversym; ss; ss = ss->Sfunc->Foversym)
            {   if (ss->Sfunc->Fflags & Foverridden)
                    continue;
                if ((ss->Sflags & SFLpmask) != as)
                    cpperr(EM_access_diff,prettyident(sb));     // different access levels
            }
        }

        if (access_s != access_specifier)
            cpperr(EM_change_access,prettyident(sb));   // can't grant/reduce access
        else
        {   list_t *pl;

            switch (access_specifier)
            {   case SFLpublic:
                case SFLprotected:
                    /* Don't worry if it winds up in this list more than once */
                    list_prepend(&b->BCpublics,sb);
                    if (tyfunc(sb->Stype->Tty))
                    {   while ((sb = sb->Sfunc->Foversym) != NULL)
                            list_prepend(&b->BCpublics,sb);
                    }
                    /*dbg_printf("adding %s to %s\n",sb->Sident,t->Ttag->Sident);*/
                    break;
                case SFLprivate:
                    cpperr(EM_access_decl); // must be in public or protected section
                    break;
                default:
                    assert(0);
            }
        }
    }

L2:
    symbol_free(s);
}

/**********************************
 * Process static members with static storage class.
 * Input:
 *      stag    class
 *      s       member symbol
 * BUGS: Does not detect the following errors:
 *      o Since the symbol is not put into the global symbol table,
 *        multiple definitions are possible
 */

STATIC void n2_static(Classsym *stag,symbol *s)
{   type *ts;

    symbol_debug(stag);
#if TX86 && TARGET_WINDOS
    if (stag->Sstruct->Sflags & STRexport)
    {   tym_t ty;

        ty = mTYexport;
        if (I16)
            ty |= mTYfar;
        type_setty(&s->Stype,s->Stype->Tty | ty); // add in _export
    }
    if (stag->Sstruct->Sflags & STRimport)
        type_setty(&s->Stype,s->Stype->Tty | mTYimport);
#endif
    ts = s->Stype;
    if (gdeclar.class_sym || tyfunc(ts->Tty))
        synerr(EM_storage_class,"static");      /* bad storage class for member */

    s->Sclass = SCglobal;

    if (tok.TKval == TKeq)
    {
        if (!(ts->Tty & mTYconst) || !tyintegral(ts->Tty))
            cpperr(EM_static_init_inside);      // initializer not allowed here
        datadef(s);
        s->Sclass = SCextern;
        if (s->Sflags & SFLdyninit)
        {
            cpperr(EM_in_class_init_not_const, s->Sident);      // in-class initializer not constant
        }
    }
    else
    {
        /* Determine if static member can be initialized with a common
           block or if it needs an explicit initializer elsewhere.
           ANSI C++ doesn't allow the common block
           initialization ARM 9.4.
         */
// JTM: 11/16/93, the way that the code below was written, all static data declarations
// were extern and the code was not used.  I have forced (through the #if 1 below)
// all of them to be extern until we decide to re-enable the code below.
// WB forced the same, so share the code
        /* NOBODY, including our tech support, seems to be able to figure
           this out, as I get repeated bug reports on it. So, I give up
           and we'll just do it the ansi c++ way.
         */
        if (1 || (config.flags2 & CFG2phgen) || ANSI || ts->Tflags & TFsizeunknown)
            s->Sclass = SCextern;
        else
        {
            ts = type_arrayroot(ts);
            /* If symbol needs a constructor    */
            if (tybasic(ts->Tty) == TYstruct &&
                ts->Ttag->Sstruct->Sflags & STRanyctor)
                s->Sclass = SCextern;
            else
                init_common(s);         /* create common block for member */
        }
        if (s->Sclass == SCextern)
            datadef(s);
    }
    stag->Sstruct->Sflags |= STRstaticmems;
    n2_chkexist(stag,s->Sident);
}

/**********************************
 * Process members with friend storage class.
 * Input:
 *      stag            class symbol
 *      tfriend         type of friend
 *      vident          identifier for friend (can be NULL)
 *      class_m         mask of storage classes
 *      ts              !=0 if there was a type-specifier
 * Returns:
 *      TRUE    it was a function with a body
 *      FALSE   it wasn't
 */

STATIC int n2_friend(Classsym *stag,type *tfriend,char *vident,unsigned long class_m,int ts)
{
    //printf("n2_friend(stag='%s',tfriend=%p,vident='%s')\n",stag->Sident,tfriend,vident);
    //type_print(tfriend);
    symbol_debug(stag);
    assert(tfriend);
    if (tyfunc(tfriend->Tty))
    {   symbol *s;
        enum SC mclass;

        if (class_m & mskl(SCvirtual))
            cpperr(EM_friend_sclass);           // friends can't be virtual
        mclass = (enum SC) ((class_m & mskl(SCinline)) ? SCinline : SCfriend);
        if (gdeclar.explicitSpecialization)
        {
            // The symbol lookup code mirrors symdecl()'s
            if (gdeclar.class_sym)
            {
                Classsym *stag2 = gdeclar.class_sym;
                if (mclass == SCfriend)
                    s = cpp_findmember_nest(&stag2,vident,FALSE);
                else
                {   template_instantiate_forward(stag2);
                    s = n2_searchmember(stag2, vident);
                }
                if (!s)
                {   char *di;

                    di = mem_strdup(cpp_unmangleident(vident));
                    err_notamember(di,gdeclar.class_sym);
                    mem_free(di);
                    return FALSE;
                }
            }
            else if (gdeclar.namespace_sym)
            {
                s = nspace_searchmember(vident,gdeclar.namespace_sym);
                if (!s)
                {   cpperr(EM_nspace_undef_id,vident,gdeclar.namespace_sym->Sident);
                    return FALSE;
                }
            }
            else
            {   int sct;
                sct = SCTglobal;
                if (scope_inNamespace())
                    sct |= SCTnspace;
                /* C++03 7.3.1.2-3 When looking for a prior declaration of a
                 * class or a function declared as a friend, and when the name
                 * of the friend class or function is neither a qualified name
                 * nor a template-id, scopes outside the innermost enclosing
                 * namespace scope are not considered.
                 */
                s = scope_search(vident,sct);
                //s = scope_searchinner(vident,sct);
                if (!s)
                {
                    synerr(EM_undefined, vident);
                    return FALSE;
                }
            }
            s = template_matchfunctempl(s, gdeclar.ptal, tfriend, stag);
            if (!s)
            {
                synerr(EM_undefined, vident);
                return FALSE;
            }
            symbol_func(s);
            s->Ssequence = 0;
        }
        else
        {
            /* C++98 11.4-9 If a friend declaration appears in a local class
             * (9.8) and the name specified is an unqualified name, a prior
             * declaration is looked up without considering scopes that are
             * outside the innermost enclosing nonclass scope. For a friend
             * function declaration, if there is no prior declaration, the
             * program is illformed.
             */
            symbol *sprior = NULL;
//printf(" %s::'%s' %p %p\n", stag->Sident, vident, stag->Sscope, gdeclar.class_sym);
            if (funcsym_p && !gdeclar.class_sym)
            {
                sprior = scope_searchinner(vident, SCTlocal);
                if (!sprior)
                    synerr(EM_undefined, vident);
            }

            s = symdecl(vident,tfriend,mclass,NULL);
            s->Ssequence = 0;
            if (sprior && s != sprior)
                    synerr(EM_undefined, vident);
            if (mclass == SCfriend)
                mclass = SCextern;
            assert(s);
            symbol_func(s);
            list_append(&s->Sfunc->Fclassfriends,stag);
            list_append(&stag->Sstruct->Sfriendfuncs,s);
            if (funcdecl(s,mclass,1|2|4,&gdeclar))
            {   // Function body was present, but not parsed
                //printf("appending '%s' to struct '%s'\n", s->Sident, stag->Sident);
                s->Sfunc->Fparsescope = stag;
                list_append(&stag->Sstruct->Sinlinefuncs,s);
                return TRUE;            // function has a body
            }
            /* Function is not defined  */
            if (s->Sclass != SCinline)
                s->Sclass = mclass;
        }
    }
    else if (type_struct(tfriend))
    {   symbol *ftag;

L1:
        ftag = tfriend->Ttag;
#ifdef DEBUG
        //assert(!vident || errcnt);
        symbol_debug(tfriend->Ttag);
#endif
        assert(ftag);

        /* if any other storage classes */
        if (class_m & ~(mskl(SCfriend) | mskl(SCextern)))
            cpperr(EM_friend_sclass);           // invalid storage class for friend

        list_append(&stag->Sstruct->Sfriendclass,ftag);

        /* if class is forward referenced       */
        if (tfriend->Tflags & TFforward)
        {
            /* Since the class is not defined yet, we only list */
            /* the classes of which it will be a friend of      */
            /*dbg_printf("Adding class %s as friend of class %s\n",
            ftag->Sident,stag->Sident);*/
            list_append(&ftag->Sstruct->Sclassfriends,stag);
        }
        else
        {   /* We know all about the class. For each of its     */
            /* member functions, list tclass as a class of      */
            /* the member function is a friend                  */
            n2_makefriends(stag,ftag);
        }
    }
    else if (tybasic(tfriend->Tty) == TYint && !ts)
    {
        /* A forward referenced class name, vident      */
        assert(vident);
        /* Declare the class vident as a forward referenced class       */
        token_unget();
        token_setident(vident);
        type_free(tfriend);
        pstate.STclasssym = NULL;               // temporarilly switch to global scope
        tfriend = stunspec(TKclass,NULL,NULL,NULL);
        pstate.STclasssym = stag;
        vident = NULL;
        goto L1;
    }
    else
    {
        cpperr(EM_friend_type); // only classes and functions can be friends
    }
    return FALSE;
}

/***************************
 * Find all the member functions of sfriend. For each of those member
 * functions, list sc as a class to which the function is a friend.
 * Input:
 *      sc              class symbol
 *      sfriend         friend of sc
 */

STATIC void n2_makefriends(Classsym *sc,symbol *sfriend)
{   list_t sl;

    symbol_debug(sc);
    symbol_debug(sfriend);
    assert(tybasic(sc->Stype->Tty) == TYstruct);
    assert(tybasic(sfriend->Stype->Tty) == TYstruct);
    for (sl = sfriend->Sstruct->Sfldlst; sl; sl = list_next(sl))
    {   symbol *s = list_symbol(sl);

        symbol_debug(s);
        if (tyfunc(s->Stype->Tty))
        {   symbol *sp;

            for (sp = s; sp; sp = sp->Sfunc->Foversym)
            {
                list_append(&sp->Sfunc->Fclassfriends,sc);
            }
        }
    }
}

/*****************************
 * Look down list of virtual functions. If there are any there that
 * belong in Sopoverload list or Scastoverload list and aren't there
 * already, put them there.
 */

STATIC void n2_overload(Classsym *stag)
{
    symlist_t l;
    struct_t *st = stag->Sstruct;

    for (l = st->Svirtual; l; l = list_next(l))
    {
        symbol *s = list_mptr(l)->MPf;

        symbol_debug(s);
        if (s->Sfunc->Fflags & Foperator &&
            !list_inlist(st->Sopoverload,s)
           )
            list_append(&st->Sopoverload,s);

        if (s->Sfunc->Fflags & Fcast &&
            !list_inlist(st->Scastoverload,s)
           )
            list_append(&st->Scastoverload,s);
    }
}

/********************************
 * Make and return copy of mptr list.
 */

list_t n2_copymptrlist(list_t ml)
{   list_t l2;

    l2 = NULL;
    for (; ml; ml = list_next(ml))
    {   mptr_t *m;

        m = (mptr_t *) MEM_PH_MALLOC(sizeof(mptr_t));
        *m = *list_mptr(ml);
        list_append(&l2,m);
    }
    return l2;
}

/*********************************
 * For base class b, make our own modifiable version of the vtbl[].
 */

STATIC void n2_newvtbl(baseclass_t *b)
{   list_t list;

    list = n2_copymptrlist(b->BCmptrlist);
    list_free(&b->BCmptrlist,FPNULL);
    b->BCmptrlist = list;
    b->BCflags |= BCFnewvtbl;
    b->BCvtbl = NULL;
}

/********************************
 * Determine if any of the virtual functions are pure.
 * Returns:
 *      0       None are pure
 *      1       At least one is pure
 *      2       All are pure
 */

int n2_anypure(list_t lvf)
{   int result = 0;
    static int convert[4] = { 0,2,0,1 };

    for (; lvf; lvf = list_next(lvf))
    {   mptr_t *m;

        m = (mptr_t *) list_ptr(lvf);
        result |= (m->MPf->Sfunc->Fflags & Fpure) ? 1 : 2;
    }
    return convert[result];
}

/********************************
 * Determine if struct is abstract. It is if any virtual functions
 * for it are pure.
 */

STATIC void n2_chkabstract(Classsym *stag)
{
    struct_t *st = stag->Sstruct;
    baseclass_t *b;

    if (n2_anypure(st->Svirtual))
    {   st->Sflags |= STRabstract;      /* it's an abstract class */
        return;
    }

    for (b = st->Smptrbase; b; b = b->BCnext)
        if (n2_anypure(b->BCmptrlist))
        {   st->Sflags |= STRabstract;  /* it's an abstract class */
            break;
        }
}

/*****************************
 * Generate the definition (if it doesn't already exist):
 *      static struct __mptr class__vtbl[] = {
 *              0,0,0,
 *              virtual function pointers,
 *              0,0,0 };
 * Store in stag->Svtbl the symbol pointer of the definition.
 * Input:
 *      sc      Storage class of definition:
 *              SCstatic        generate static definition
 *              SCcomdat        generate initialized common block
 *              SCglobal        generate global definition
 *              SCextern        generate extern declaration (no data defined)
 *      flag                    output the data definition
 */

STATIC type * n2_vtbltype(Classsym *stag,int nitems)
{   type *t;

    /* Construct the type of (*vtbl[])(...)     */
    t = type_allocn(TYarray,s_mptr->Stype);
    t->Tmangle = mTYman_sys;
    type_setty(&t->Tnext,t->Tnext->Tty | mTYconst);
#if TX86 && TARGET_WINDOS
    if (stag->Sstruct->ptrtype == TYfptr)
    {
        // Put table in code segment for large data models
        if (config.flags & CFGfarvtbls || LARGECODE)
            t->Tty |= mTYfar;
        else if (config.memmodel != Vmodel)     // table can't be in overlay
            t->Tty |= mTYcs;
        if (stag->Sstruct->Sflags & STRexport)
            t->Tty |= mTYexport;
        if (stag->Sstruct->Sflags & STRimport)
            t->Tty |= mTYimport;
    }
#endif
    t->Tdim = 1 + nitems + 1;
    return t;
}

void n2_genvtbl(Classsym *stag,enum SC sc,int flag)
{
    struct_t *st = stag->Sstruct;
    symbol *s_vtbl;
    type *t;
    baseclass_t *b;

#if 0
    dbg_printf("n2_genvtbl('%s') ",stag->Sident);
    WRclass(sc);
    dbg_printf("\n");
#endif

    s_vtbl = st->Svtbl;
    if (s_vtbl && sc == SCextern)
        return;                 // vtbl[] is already defined

    cpp_getpredefined();                        /* define s_mptr        */

    if (n2_anypure(st->Svirtual) == 2)
    {
        /* Don't generate vtbl[]        */
    }

    // Sharing vtbls is bad if your linker can't resolve multiply defined
    // names, as in Babel.

    /* See if we can get by using our primary base class's vtbl[].      */
    /* In order to use it we must have a base class with an             */
    /* identical Svirtual list. Always generate our own vtbl[] if       */
    /* dynamic typing information is desired.                           */
    else if (st->Sprimary && !(st->Sprimary->BCflags & BCFnewvtbl) &&
             !(config.flags2 & CFG2dyntyping) &&
             !(config.flags3 & CFG3rtti))
    {   Classsym *sbase;

        sbase = st->Sprimary->BCbase;
        //dbg_printf("Recursive...\n");
        n2_genvtbl(sbase,sc,flag);
        st->Svtbl = sbase->Sstruct->Svtbl;
    }
    else if (st->Svirtual)
    {   /* Generate our own vtbl[] array        */

        if (!s_vtbl)
        {
            t = n2_vtbltype(stag,list_nitems(st->Svirtual));

            /* Define the symbol for the vtbl[] */
            s_vtbl = mangle_tbl(0,t,stag,NULL);
            st->Svtbl = s_vtbl;
            //dbg_printf("Generating vtbl '%s'\n",s_vtbl->Sident);
        }
        else if (s_vtbl->Sclass != SCextern)
            goto L1;                            // already defined

        s_vtbl->Sclass = sc;
#if VEC_VTBL_LIST
        s_vtbl->Sflags |= SFLvtbl;
#endif
        if (sc != SCextern)
        {   init_vtbl(s_vtbl,st->Svirtual,stag,stag); // define data for vtbl[]
            if (flag)
                outdata(s_vtbl);
        }
        else
            datadef(s_vtbl);                    // set Sfl
    }

    /* Do vtbl[]s for each of the non-primary base classes that         */
    /* changed their vtbl[]s.                                           */
L1:
    for (b = st->Smptrbase; b; b = b->BCnext)
    {   symbol *s;

        //printf("base '%s'\n",b->BCbase->Sident);
        if (!(b->BCflags & BCFnewvtbl)) /* if didn't modify vtbl[]      */
        {
            if (!(config.flags3 & CFG3rtti) || !b->BCbase->Sstruct->Svptr)
                continue;                       // skip it
            n2_newvtbl(b);
        }

        /* If any of the virtual functions are pure, then optimize
           by not creating a vtbl[].
         */
        if (n2_anypure(b->BCmptrlist) == 2)
            continue;

        s = b->BCvtbl;
        if (!s)
        {
            t = n2_vtbltype(stag,list_nitems(b->BCmptrlist));

            /* Define symbol for the vtbl[]     */
            s = mangle_tbl(0,t,stag,b);

            assert(!b->BCvtbl);
            b->BCvtbl = s;
        }
        else if (s->Sclass != SCextern)
            continue;                           // already defined
        s->Sclass = sc;
        //dbg_printf("Generating base vtbl '%s'\n",s->Sident);
        if (sc != SCextern)
        {   init_vtbl(s,b->BCmptrlist,b->BCbase,stag);  // define data for vtbl[]
            if (flag)
                outdata(s);
        }
        else
            datadef(s);                 // set Sfl
    }
}


/*****************************
 * Generate the definition (if it doesn't already exist):
 *      static int class__vbtbl[] = { ... };
 * Store in stag->Svbtbl the symbol pointer of the definition.
 * Input:
 *      sc      Storage class of definition:
 *              SCstatic        generate static definition
 *              SCcomdat        generate initialized common block
 *              SCglobal        generate global definition
 *              SCextern        generate extern declaration (no data defined)
 *      flag                    output the data definition
 */

STATIC type * n2_vbtbltype(baseclass_t *b,int flags)
{   type *t;

    // Construct the type of int vbtbl[]
    t = type_allocn(TYarray,tsint);
    t->Tmangle = mTYman_sys;
    type_setty(&t->Tnext,tsint->Tty | mTYconst);
#if TX86 && TARGET_WINDOS
    if (b->BCbase->Sstruct->ptrtype == TYfptr)
    {
        // Put table in code segment for large data models
        // Put table in code segment for large data models
        if (config.flags & CFGfarvtbls || LARGECODE)
            t->Tty |= mTYfar;
        else if (config.memmodel != Vmodel)     // table can't be in overlay
            t->Tty |= mTYcs;
        if (flags & STRexport)
            t->Tty |= mTYexport;
        if (flags & STRimport)
            t->Tty |= mTYimport;
    }
#endif
    t->Tdim = 1 + baseclass_nitems(b);
    return t;
}

void n2_genvbtbl(Classsym *stag,enum SC sc,int flag)
{
    struct_t *st = stag->Sstruct;
    symbol *s_vbtbl;
    type *t;
    baseclass_t *b;

#if 0
    dbg_printf("n2_genvbtbl('%s') ",stag->Sident);
    WRclass(sc);
    dbg_printf("\n");
#endif

    s_vbtbl = st->Svbtbl;
    if (s_vbtbl && sc == SCextern)
        return;                 // vbtbl[] is already defined

    if (st->Svirtbase)
    {   /* Generate our own vbtbl[] array       */

        if (!s_vbtbl)
        {
            t = n2_vbtbltype(st->Svirtbase,st->Sflags);

            /* Define the symbol for the vbtbl[]        */
            s_vbtbl = mangle_tbl(1,t,stag,NULL);
            st->Svbtbl = s_vbtbl;
        }
        else if (s_vbtbl->Sclass != SCextern)
            goto L1;                            // already defined
        //dbg_printf("Generating vbtbl '%s'\n",s_vbtbl->Sident);

        s_vbtbl->Sclass = sc;
        if (sc != SCextern)
        {
            symbol_debug(st->Svbptr);
            init_vbtbl(s_vbtbl,st->Svirtbase,stag,st->Svbptr_off); // define data for vbtbl[]
            if (flag)
                outdata(s_vbtbl);
        }
        else
            datadef(s_vbtbl);                   // set Sfl
    }

    // Do vbtbl[]s for each of the base classes that have their own vbtbl[]s
L1:
    for (b = st->Svbptrbase; b; b = b->BCnext)
    {   symbol *s;
        Classsym *sbase = b->BCbase;

        s = b->BCvtbl;
        if (!s)
        {
            t = n2_vbtbltype(sbase->Sstruct->Svirtbase,st->Sflags);

            /* Define symbol for the vbtbl[]    */
            s = mangle_tbl(1,t,stag,b);

            assert(!b->BCvtbl);
            b->BCvtbl = s;
        }
        else if (s->Sclass != SCextern)
            continue;                           // already defined
        s->Sclass = sc;
        //dbg_printf("Generating base vbtbl '%s'\n",s->Sident);
        if (sc != SCextern)
        {   symbol_debug(sbase->Sstruct->Svbptr);
            init_vbtbl(s,sbase->Sstruct->Svirtbase,stag,b->BCoffset + sbase->Sstruct->Svbptr_off);      // define data for vbtbl[]
            if (flag)
                outdata(s);
        }
        else
            datadef(s);                 // set Sfl
    }
}


/**************************
 * Get storage classes for member.
 * Return it as a mask.
 */

STATIC unsigned long n2_memberclass()
{   unsigned long class_m;

    /* Determine what storage classes there are                 */
    class_m = 0;                        /* assume no storage class seen */
    while (1)
    {
        switch (tok.TKval)
        {   case TKinline:
                class_m |= mskl(SCinline);
                goto L1;
            case TKvirtual:
                class_m |= mskl(SCvirtual);
                goto L1;
            case TKstatic:
                class_m |= mskl(SCstatic);
                goto L1;
            case TKextern:
                class_m |= mskl(SCextern);
                goto L1;
            case TKtypedef:
                class_m |= mskl(SCtypedef);
                goto L1;
            case TKexplicit:
                class_m |= mskl(SCexplicit);
                goto L1;
            case TKmutable:
                class_m |= mskl(SCmutable);
                goto L1;
            case TKthread_local:
                class_m |= mskl(SCthread);
                goto L1;
#if 1
            case TKfriend:
                class_m |= mskl(SCfriend);
#endif
            L1: stoken();
                continue;
            case TKoverload:            /* ignore overload keyword      */
                                        /* (should produce a warning)   */
                goto L1;
        }
        break;
    }
    return class_m;
}

/****************************
 * Generate type of generated member function.
 */

STATIC type * n2_typector(Classsym *stag,type *tret)
{   type *t;
    tym_t tym;

    type_debug(tret);
#if TX86
    if (LARGECODE)
        tym = TYfpfunc;
    else if (MFUNC)
        tym = TYmfunc;
    else
        tym = TYnpfunc;
    if ((config.wflags & WFexport && tyfarfunc(tym)) ||
        stag->Sstruct->Sflags & STRexport)
        tym |= mTYexport;
    if (stag->Sstruct->Sflags & STRimport)
        tym |= mTYimport;
    if (config.wflags & WFloadds)
        tym |= mTYloadds;
#else
    tym = TYfpfunc;
#endif
    t = type_alloc(tym);
    t->Tmangle = mTYman_cpp;
    t->Tnext = tret;
    t->Tnext->Tcount++;
    t->Tflags |= TFprototype | TFfixed;
    return t;
}

/******************************
 * Place in symbol table all the virtual base class pointer parameters.
 * Finish defining the function.
 * Input:
 *      tret    Return type of function
 *      flags   Flags for Fflags
 *      func    CFxxxx
 * Returns:
 *      function symbol created
 */

/* Which function we're generating      */
#define CFctor          1       /* X::X()                       */
#define CFdtor          2       /* X::~X()                      */
#define CFvecctor       4       /* vector version of X::X()     */
#define CFopeq          8       /* X& X::operator =(X&)         */
#define CFcopyctor      0x10    /* X::X(X&)                     */
#define CFprimdtor      0x20    // primary dtor
#define CFscaldtor      0x40    // scalar deleting dtor
#define CFvecdtor       0x80    // vector dtor
#define CFdelete        0x100   // shell around operator delete(void*,unsigned)
#define CFveccpct       0x200   // vector copy constructor
#define CFinvariant     0x400   // __invariant() (don't do virtual base classes)
#define CFpriminv       0x800   // primary __invariant() (do virtual base classes)

STATIC symbol * n2_createfunc(Classsym *stag,const char *name,
        type *tret,unsigned flags,unsigned func)
{   SYMIDX si;
    baseclass_t *b;
    int nvirt = 0;
    func_t *f;
    symbol *s;
    symbol *sthis;
    symbol *sfree;
    symbol *sfunc;
    symbol *funcsym_save;
    type *t;
    enum SC sclass;

    symbol_debug(stag);
    type_debug(tret);

    //printf("n2_createfunc('%s')\n", name);
    if (func == CFinvariant)
        func = CFdtor;                  // no difference in generated code

    // Leave room for flag
    if (func & (CFctor | CFcopyctor) && stag->Sstruct->Svirtbase)
        nvirt++;

    t = n2_typector(stag,tret);
    if (func & CFscaldtor)
        param_append_type(&t->Tparamtypes,tsuns);
    sclass = (flags & Finline) ? SCinline :
             ((config.flags2 & CFG2comdat) ? SCcomdat : SCstatic);
    sfunc = symbol_name(name,sclass,t);

    f = sfunc->Sfunc;
    f->Fflags |= flags;
    f->Fflags |= Fgen;                  // compiler generated function
    if (func & (CFscaldtor | CFvecctor | CFvecdtor | CFdelete | CFveccpct))
        f->Fflags |= Fnodebug;          // do not generate debug info for this
    f->Flocsym.top = f->Flocsym.symmax = 1 + nvirt +
                ((func & (CFscaldtor | CFopeq | CFcopyctor | CFveccpct)) != 0);
    f->Flocsym.tab = symtab_calloc(f->Flocsym.top);

    /* Remember to load the symbols in reverse order, because we made   */
    /* constructors and destructors "Pascal"                            */

    si = f->Flocsym.top;

    /* Do "this"        */
    sthis = symbol_name(cpp_name_this,
        ((tybasic(t->Tty) == TYmfunc) ? SCfastpar : SCparameter),
        newpointer(stag->Stype));
    sthis->Ssymnum = 0;
    sthis->Sflags |= SFLfree;
    f->Flocsym.tab[0] = sthis;

    if (nvirt)
    {
        s = symbol_name(cpp_name_initvbases,SCparameter,tsint);
        s->Ssymnum = --si;
        s->Sflags |= SFLfree;
        f->Flocsym.tab[si] = s;
    }

    if (func & ((1 ? CFscaldtor : CFdtor) | CFopeq | CFcopyctor | CFveccpct))
    {   /* dtor() or operator=()        */
        char *p;
        type *t;

        if (func & (1 ? CFscaldtor : CFdtor))
        {   p = cpp_name_free;
            t = tsint;
        }
        else
        {   p = "_param__P1";
            t = newref(stag->Stype);
        }
        s = symbol_name(p,SCparameter,t);
        s->Ssymnum = --si;
        s->Sflags |= SFLfree;
        f->Flocsym.tab[si] = s;
        sfree = s;
    }
    assert(si == 1);            /* "this" is always symbol 0    */

    if (func & (CFopeq | CFcopyctor | CFveccpct))
    {
        /* Add type of parameter, which is X&. Later on we'll OR in the */
        /* mTYconst bit if applicable, so this type must not be shared. */
        param_append_type(&t->Tparamtypes,newref(stag->Stype));
    }

    sfunc->Sflags |= SFLimplem | SFLpublic;     /* seen implementation  */
    n2_addfunctoclass(stag,sfunc,0);

    // Adjust which function we are in
    funcsym_save = funcsym_p;
    funcsym_p = sfunc;

    {
        char inopeqsave;
        inopeqsave = pstate.STinopeq;
        pstate.STinopeq = 0;

        symtab_t *psymtabsave;
        psymtabsave = cstate.CSpsymtab;
        cstate.CSpsymtab = &f->Flocsym;
        assert(cstate.CSpsymtab->tab);  // the local symbol table must exist

        block *b;
        b = block_calloc();
        f->Fstartblock = b;
        b->BC = BCret;
        if (func & (CFvecctor | CFopeq | CFscaldtor | CFveccpct))
            b->BC = BCretexp;
        if (func & CFvecctor)
        {
            /* Construct call to X::X() */
            b->Belem =
                cpp_constructor(el_var(sthis),stag->Stype,NULL,NULL,NULL,0);
        }
        else if (func & CFveccpct)
        {   elem *e;

            // Construct call to X::X(X&)
            e = el_var(sfree);
            el_settype(e,newpointer(e->ET->Tnext));
            e = el_unat(OPind,stag->Stype,e);
            b->Belem =
                cpp_constructor(el_var(sthis),stag->Stype,
                        list_build(e,NULL),NULL,NULL,0);
        }
        else if (func & CFprimdtor)
        {
            // Call basic dtor, then dtors for virtual base classes
            elem *e = NULL;
            baseclass_t *bc;

            for (bc = stag->Sstruct->Svirtbase; bc; bc = bc->BCnext)
            {   Classsym *sbase;

                sbase = bc->BCbase;
                symbol_debug(sbase);
                if (sbase->Sstruct->Sdtor)
                {   elem *et;

                    et = el_var(sthis);
                    et = el_bint(OPadd,et->ET,
                                 et,el_longt(tsint,bc->BCoffset));
                    et = cpp_destructor(sbase->Stype,et,NULL,DTORnoeh);
                    e = el_combine(et,e);
                }
            }
            b->Belem = el_combine(cpp_destructor(stag->Stype,el_var(sthis),NULL,DTORnoeh),e);
        }
        else if (func & CFpriminv)
        {
            // Call basic __invariant(), then __invariant()s for virtual base classes
            elem *e = NULL;
            baseclass_t *bc;

            for (bc = stag->Sstruct->Svirtbase; bc; bc = bc->BCnext)
            {   Classsym *sbase;

                sbase = bc->BCbase;
                symbol_debug(sbase);
                if (sbase->Sstruct->Sinvariant)
                {   elem *et;

                    et = el_var(sthis);
                    et = el_bint(OPadd,et->ET,
                                 et,el_longt(tsint,bc->BCoffset));
                    et = cpp_invariant(sbase->Stype,et,NULL,DTORnoeh);
                    e = el_combine(et,e);
                }
            }
            b->Belem = el_combine(cpp_invariant(stag->Stype,el_var(sthis),NULL,DTORnoeh),e);
        }
        else if (func & CFscaldtor)
        {
            // Call primary dtor, then operator delete
            elem *e;
            baseclass_t *bc;

            e = cpp_destructor(stag->Stype,el_var(sthis),NULL,DTORnoeh | DTORmostderived);

            // Append (this && (e,_free & 1) && _delete(this)) to return block
            {   elem *e1,*e2;

                e1 = el_bint(OPand,sfree->Stype,
                        el_var(sfree),el_longt(sfree->Stype,1));
                e1 = el_combine(e,e1);
                e1 = el_bint(OPandand,tsint,el_var(sthis),e1);
                e2 = cpp_delete(0,sfunc,el_var(sthis),el_typesize(stag->Stype));

                e = el_bint(OPandand,tsint,e1,e2);
            }

            // Return this
            b->Belem = el_bint(OPcomma,sthis->Stype,e,el_var(sthis));
        }
        cstate.CSpsymtab = psymtabsave;
        pstate.STinopeq = inopeqsave;
    }

    /* See if creating a destructor that should be virtual      */
    if (func & CFdtor && n2_invirtlist(stag,sfunc,0))
    {
        sfunc->Sfunc->Fflags |= Fvirtual;
        n2_invirtlist(stag,sfunc,1);    /* insert into vtbl list */
        n2_createvptr(stag,&stag->Sstruct->Sstructsize);
    }

    // If NT, and inline function is exported, then we must write it out
    if (config.exe == EX_WIN32 && sclass == SCinline && stag->Sstruct->Sflags & STRexport)
        nwc_mustwrite(funcsym_p);

    funcsym_p = funcsym_save;
    return sfunc;
}

/*****************************
 * Create a function that is a constructor for tclass.
 */

void n2_creatector(type *tclass)
{   symbol *s;

    //dbg_printf("n2_creatector(%s)\n",tclass->Ttag->Sident);
    type_debug(tclass);
    assert(tybasic(tclass->Tty) == TYstruct && tclass->Ttag);
    tclass->Ttag->Sstruct->Sflags &= ~STRgenctor0;

    /* Constructors return <pointer to><class>  */
    s = n2_createfunc(tclass->Ttag,cpp_name_ct,newpointer(tclass),Finline | Fctor,CFctor);
    cpp_buildinitializer(s,NULL,0);
    cpp_fixconstructor(s);
}

/********************************
 * Generate destructor for tclass.
 */

STATIC void n2_createdtor(type *tclass)
{   symbol *s;

    /*dbg_printf("n2_createdtor(%s)\n",tclass->Ttag->Sident);*/
    type_debug(tclass);
    assert(tybasic(tclass->Tty) == TYstruct && tclass->Ttag);

    /* Destructors return <int> */
    s = n2_createfunc(tclass->Ttag,cpp_name_dt,tsint,Finline | Fdtor,CFdtor);
    // Must defer fixing destructor until after n2_virtbase() is called
    //cpp_fixdestructor(s);
}

/********************************
 * Generate invariant for tclass.
 */

STATIC void n2_createinvariant(type *tclass)
{   symbol *s;

    //dbg_printf("n2_createinvariant(%s)\n",tclass->Ttag->Sident);
    type_debug(tclass);
    assert(tybasic(tclass->Tty) == TYstruct && tclass->Ttag);

    // Invariants return <int>
    s = n2_createfunc(tclass->Ttag,cpp_name_invariant,tsint,Finline | Finvariant,CFinvariant);
    cpp_fixinvariant(s);
}

/********************************
 * Generate primary destructor for stag.
 */

symbol *n2_createprimdtor(Classsym *stag)
{   symbol *s;
    struct_t *st = stag->Sstruct;

    //dbg_printf("n2_createprimdtor(%s)\n",stag->Sident);

    if (!st->Sprimdtor)
    {
        s = st->Sdtor;
        if (s && st->Svirtbase)
        {
            s = n2_createfunc(stag,cpp_name_primdt,tsvoid,
                    st->Sdtor->Sfunc->Fflags & Fvirtual,CFprimdtor);
        }
        st->Sprimdtor = s;
    }
    return st->Sprimdtor;
}

/********************************
 * Generate primary invariant for stag.
 */

symbol *n2_createpriminv(Classsym *stag)
{   symbol *s;
    struct_t *st = stag->Sstruct;

    //dbg_printf("n2_createpriminv(%s)\n",stag->Sident);

    if (!st->Spriminv)
    {
        s = st->Sinvariant;
        if (s && st->Svirtbase)
        {
            s = n2_createfunc(stag,cpp_name_priminv,tsvoid,
                    st->Sdtor->Sfunc->Fflags & Fvirtual,CFpriminv);
        }
        st->Spriminv = s;
    }
    return st->Spriminv;
}

/********************************
 * Generate scalar deleting destructor for stag.
 */

symbol *n2_createscaldeldtor(Classsym *stag)
{   struct_t *st = stag->Sstruct;

    //dbg_printf("n2_createscaldeldtor(%s)\n",stag->Sident);

    if (!st->Sscaldeldtor)
    {
        st->Sscaldeldtor = n2_createfunc(stag,cpp_name_scaldeldt,tspvoid,
            Finline | (st->Sdtor->Sfunc->Fflags & Fvirtual),CFscaldtor);
    }
    return st->Sscaldeldtor;
}


/**********************************
 * Create vector constructor for class stag.
 */

symbol *n2_vecctor(Classsym *stag)
{   struct_t *st;
    symbol *sctor;

    symbol_debug(stag);
    st = stag->Sstruct;
    if (!st->Svecctor)
    {   /* Look for constructor of the form X::X()      */
        sctor = cpp_findctor0(stag);
        /* If virtual bases or no X::X(), we need to build one  */
        if (st->Svirtbase || !sctor)
            sctor = n2_createfunc(stag,cpp_name_vc,newpointer(stag->Stype),0,
                        CFvecctor);
        assert(sctor);
        symbol_debug(sctor);
        st->Svecctor = sctor;
    }
    return st->Svecctor;
}

/**********************************
 * Create vector copy constructor for class stag.
 */

symbol *n2_veccpct(Classsym *stag)
{   symbol *scpct;

    scpct = stag->Sstruct->Sveccpct;
    if (!scpct)
    {
        n2_createcopyctor(stag,1);
        scpct = stag->Sstruct->Scpct;
        if (scpct->Stype->Tparamtypes->Pnext || !(scpct->Stype->Tflags & TFfixed))
        {
            // Need to create a wrapper function
            //assert(0);                        // BUG: not supported
            scpct = n2_createfunc(stag,"__veccpct",newpointer(stag->Stype),
                0,CFveccpct);
            stag->Sstruct->Sveccpct = scpct;
        }
    }
    return scpct;
}

/**********************************
 * Create vector destructor for class stag.
 * The vector dtor has hardcoded in it the number of objects in the array.
 * So, a different one is created for each different array of fixed dimension.
 * This is necessary so that the EH stack unwinder has a destructor to call
 * that takes only a single argument (in this case the address of the start
 * of the array).
 */

symbol *n2_vecdtor(Classsym *stag, elem *enelems)
{   struct_t *st;
    symbol *s;
    char *name;
    unsigned nelems;

    if (enelems->Eoper != OPconst)
    {
        cpperr(EM_no_vla_dtor);
        nelems = 1;
    }
    else
        nelems = el_tolong(enelems);

    symbol_debug(stag);
    name = (char *) alloca(strlen(stag->Sident) + 2 + 5 + 1);
    sprintf(name,"%s__%u",stag->Sident,nelems);

    s = n2_searchmember(stag,name);
    if (!s)
    {   func_t *f;
        elem *eptr;
        elem *edtor;
        elem *e;
        elem *enelems;
        elem *efunc;
        list_t arglist;
        symbol *sd;

        s = n2_createfunc(stag,name,tsvoid,0,CFvecdtor);
        assert(s);
        symbol_debug(s);
        f = s->Sfunc;

        /* call __vec_dtor(ptr,sizelem,nelems,edtor)            */
        eptr = el_var(cpp_getthis(s));
        sd = n2_createscaldeldtor(stag);
        edtor = el_ptr(sd);
        edtor = cast(edtor,s_vec_dtor->Stype->Tparamtypes->Pnext->Pnext->Pnext->Ptype);
        efunc = el_var(s_vec_dtor);
        enelems = el_longt(tsuns,nelems);
        arglist = list_build(eptr,el_typesize(stag->Stype),enelems,edtor,NULL);
        e = xfunccall(efunc,NULL,NULL,arglist);

        f->Fstartblock->Belem = e;
    }
    return s;
}

/**********************************
 * Create shell around operator delete(void *,unsigned)
 * so that only the first argument is needed.
 * Returns:
 *      symbol of shell function
 */

symbol *n2_delete(Classsym *stag,symbol *sfunc,unsigned nelems)
{
    struct_t *st;
    symbol *s;
    char *name;
    type *tfunc;

    tfunc = sfunc->Stype;
    if (!tfunc->Tparamtypes->Pnext)     // if only 1 parameter
        return sfunc;                   // don't need to create a shell

    symbol_debug(stag);
    name = (char *) alloca(strlen(sfunc->Sident) + 2 + 5 + 1);
    sprintf(name,"%s__%u",sfunc->Sident,nelems);

    s = n2_searchmember(stag,name);
    if (!s)
    {   func_t *f;
        elem *e;
        elem *eptr;
        list_t arglist;

        s = n2_createfunc(stag,name,tsvoid,Fstatic,CFdelete);
        assert(s);
        symbol_debug(s);
        f = s->Sfunc;

        eptr = el_var(f->Flocsym.tab[0]);
        arglist = list_build(eptr,el_longt(tsuns,nelems),NULL);
        e = xfunccall(el_var(sfunc),NULL,NULL,arglist);
        f->Fstartblock->Belem = e;
    }
    return s;
}

/*********************************
 * Generate assignment operator if possible for class stag.
 * X& X::operator =(X&);
 * Input:
 *      flag            bit mask
 *              1       if generate error message
 *              2       generate Sopeq2
 */

void n2_createopeq(Classsym *stag,int flag)
{   struct_t *st;
    symbol *sopeq;

    //dbg_printf("n2_createopeq('%s',flag = x%x)\n",stag->Sident,flag);
    symbol_debug(stag);
    st = stag->Sstruct;
    if (flag & 2 && !st->Sopeq2)
        goto gen;

    if (!st->Sopeq)
    {
        /* If a class X has any X::operator=() that takes an argument
           of class X, the default assignment will not be generated.
           ARM 12.8
         */
        sopeq = cpp_findopeq(stag);
        if (sopeq)
        {   st->Sopeq = st->Sopeq2 = sopeq;
            return;
        }
    gen:
        {   /* Generate one     */
            type *tclass = stag->Stype;
            symlist_t sl;
            baseclass_t *b;
            tym_t tyqual = mTYconst;    /* type qualifier for arg to op=() */
            Classsym *stag2;
            int bitcopy;                /* !=0 if we can bit copy the struct */
            int flags2;
            tym_t tym;
            type *t;
            symbol *s;

            flags2 = Fnodebug;
            bitcopy = Fbitcopy;
            /* If virtual functions or virtual base classes     */
            if (st->Svirtual || st->Svirtbase)
                bitcopy = 0;

            /* Determine feasability    */
            for (b = st->Sbase; b; b = b->BCnext)
            {
                /* If operator= is private, can't generate derived one  */
                stag2 = b->BCbase;
                goto L1;                /* share some common code       */
            L2: ;
            }
            for (sl = st->Sfldlst; sl; sl = list_next(sl))
            {   s = list_symbol(sl);
                t = s->Stype;
                tym = t->Tty;

                /* If member is const, has a ref, or a private operator= */
                if (s->Sclass == SCfield && tym & mTYconst)
                {
                    goto Lcant2;
                }
                else if (s->Sclass == SCmember)
                {
                    if (tym & mTYconst || tyref(tym))
                    {
                    Lcant2:
                        if (flag & 1)
                            // can't generate operator=()
                            cpperr(EM_cant_generate_const,
                                stag->Sident,s->Sident,
                                (tym & mTYconst) ? "const" : "reference");
                        return;
                    }
                    if (tybasic(t->Tty) == TYstruct)
                    {   type *t1;

                        stag2 = t->Ttag;
                    L1:
                        n2_createopeq(stag2,flag);
                        sopeq = stag2->Sstruct->Sopeq;
                    L4:
                        if (!sopeq)
                        {   //dbg_printf("no operator=() for %s\n",stag2->Sident);
                            goto cant;
                        }
                        /* If first parameter is not const      */
                        t1 = sopeq->Stype->Tparamtypes->Ptype;
                        if (!tyref(t1->Tty))
                        {   /*dbg_printf("first param to operator=() for %s is not a ref\n",stag2->Sident);*/
                            sopeq = sopeq->Sfunc->Foversym;
                            goto L4;
                        }
                        /* If op= is private    */
                        if ((sopeq->Sflags & SFLpmask) == SFLprivate)
                        {   //dbg_printf("operator=() for %s is private\n",stag2->Sident);
                            goto cant;
                        }
#if 1
                        if ((sopeq->Sflags & SFLpmask) == SFLprotected &&
                            !cpp_classisfriend(stag2,stag) &&
                            (!c1isbaseofc2(NULL,stag2,stag) ||
                             cpp_findaccess(sopeq,stag) == SFLpublic)
                           )
                        {   //dbg_printf("operator=() for %s is protected\n",stag2->Sident);
                            goto cant;
                        }
#endif
                        if (!(t1->Tnext->Tty & mTYconst))
                            tyqual = 0;
                        /* If we didn't generate it ourselves, can't use */
                        /* bitcopy because we don't know if we can      */
                        bitcopy &= sopeq->Sfunc->Fflags & Fbitcopy;
                        flags2 &= sopeq->Sfunc->Fflags;
                        if (b)
                            goto L2;
                    }
                }
                continue;

        cant:   if (flag & 1)
                    // can't generate operator=()
                    cpperr(EM_cant_generate,"operator =()",stag->Sident);
                return;
            }

            /* Whether the function is inline or not is determined by   */
            /* its size and whether we can bitcopy the struct           */
            sopeq = n2_createfunc(stag,
                (flag & 2) ? "?_R" : cpp_name_as,
                newref(tclass),
                (bitcopy || st->Sstructsize <= 8) ? Finline : 0,CFopeq);
            type_setty(&sopeq->Stype->Tparamtypes->Ptype->Tnext,tyqual | TYstruct);

            {
            elem *e;
            elem *e1,*e2;
            symbol *sthis;
            symbol *sx;
            targ_size_t lastoffset;
            char inopeqsave;
            symtab_t *psymtabsave;
            func_t *f;

            f = sopeq->Sfunc;

            /* Switch to local symtab, so if temporary variables are generated, */
            /* they are added to the local symbol table rather than the global  */
            psymtabsave = cstate.CSpsymtab;
            cstate.CSpsymtab = &f->Flocsym;
            assert(cstate.CSpsymtab->tab);      // the local symbol table must exist

            inopeqsave = pstate.STinopeq;
            pstate.STinopeq = 1;
            sthis = cpp_getthis(sopeq);
            sx = cpp_getlocalsym(sopeq,"_param__P1");

            /* Note that unions always wind up as bit copy(!)   */
            if (bitcopy)
            {   f->Fflags |= Fbitcopy;
                if (!(st->Sflags & STR0size))
                {   e1 = el_unat(OPind,tclass,el_var(sthis));
                    e2 = reftostar(el_var(sx));
                    e = el_bint(OPstreq,tclass,e1,e2);
                }
                else
                    e = NULL;
            }
            else
            {
                enum SC scvtbl;         // storage class for vbtbl[]
                int i;

                e = NULL;
                scvtbl = (enum SC) (config.flags2 & CFG2comdat) ? SCcomdat :
                         (st->Sflags & STRvtblext) ? SCextern : SCstatic;

                // Do loop twice, first is virtual bases, followed
                // by non-virtual bases

                for (i = (scvtbl == SCstatic || flag & 2); i < 2; i++)
                {

                    // Copy base classes
                    for (b = (i == 0) ? st->Svirtbase : st->Sbase; b; b = b->BCnext)
                    {   symbol *sf;
                        elem *e11;

                        if (i == 1 && b->BCflags & BCFvirtual &&
                            scvtbl != SCstatic)
                            continue;

                        e1 = el_var(sthis);
                        c1isbaseofc2(&e1,b->BCbase,stag);
                        e1 = el_unat(OPind,b->BCbase->Stype,e1);
                        e2 = el_var(sx);
                        el_settype(e2,newpointer(e2->ET->Tnext));       /* ref to ptr */
                        c1isbaseofc2(&e2,b->BCbase,stag);
                        e2 = el_unat(OPind,b->BCbase->Stype,e2);

                        e1 = el_bint(OPstreq,e1->ET,e1,e2);
                        e1 = cpp_structcopy(e1);
                        e1 = poptelem(e1);

                        // Call version of opeq that doesn't copy virtual base classes
                        if (e1->Eoper == OPind && e1->E1->Eoper == OPcall &&
                            (e11 = e1->E1->E1)->Eoper == OPvar &&
                            (sf = e11->EV.sp.Vsym)->Sfunc->Fflags & Fgen &&
                            sf->Sscope->Sstruct->Sopeq == sf)
                        {
                            n2_createopeq((Classsym *)sf->Sscope,2);
                            e11->EV.sp.Vsym = sf->Sscope->Sstruct->Sopeq2;
                        }

                        e = el_combine(e,e1);
                    }

                    // Only copy virtual bases for most-derived object.
                    // We detect this by examining the pointer to the vbtable.
                    // Danger, Will Robinson! We are doing it the old way
                    // if static vbtbl[]s are used!
                    if (i == 0 && e)
                    {   elem *ev;
                        symbol *svptr;
                        type *t;

                        svptr = st->Svbptr;
                        // match pointer type of ethis
                        t = type_allocn(sthis->Stype->Tty,svptr->Stype);
                        e1 = el_bint(OPadd,t,el_var(sthis),el_longt(tsint,st->Svbptr_off));     // ethis + off
                        t = svptr->Stype;
                        e1 = el_unat(OPind,t,e1);
                        n2_genvbtbl(stag,scvtbl,0);             // make sure vbtbl[]s exist
                        ev = el_var(st->Svbtbl);
                        ev = el_unat(OPaddr,t,ev);              // &_vbtbl
                        e1 = el_bint(OPeqeq,t,e1,ev);

                        e = el_bint(OPandand,tsint,e1,e);       // e1 && e
                    }
                }

                /* Copy the members             */
                lastoffset = (targ_size_t)-1;           /* an invalid value     */
                for (sl = st->Sfldlst; sl; sl = list_next(sl))
                {   symbol *s = list_symbol(sl);
                    type *t = s->Stype;
                    targ_size_t memoff = s->Smemoff;

                    if (s->Sclass == SCfield)
                    {   /* Copy all the fields at once  */
                        if (memoff == lastoffset)
                            continue;           /* already copied       */
                        goto L3;
                    }
                    else if (s->Sclass == SCmember)
                    {
                        /* don't copy __vptr or __Pxx members */
                        if (struct_internalmember(s))
                        {   lastoffset = (targ_size_t)-1;
                            continue;
                        }
                        /* BUG: Array of classes with operator=() done
                           with bit copy
                         */
                    L3:
                        if (lastoffset != -1 && tybasic(t->Tty) != TYstruct)
                        {   // Merge with previous member copy
                            // (Skip aggregates because Enumbytes would
                            // be stomped on later by outelem())
                            type *ta;

                            // ta is an array of chars, dim is # of bytes
                            elem_debug(e1);
                            e1->Eoper = OPstreq;
                            ta = type_allocn(TYarray,tschar);
                            ta->Tdim = memoff + type_size(t) - lastoffset;
                            el_settype(e1,ta);
                            el_settype(e1->E1,ta);
                            el_settype(e1->E2,ta);
                            continue;
                        }
                        lastoffset = memoff;
                        if (tyref(t->Tty))
                            t = reftoptr(t);
                        e1 = el_bint(OPadd,newpointer(t),
                            el_var(sthis),el_longt(tsint,lastoffset));
                        e1 = el_unat(OPind,t,e1);
                        e2 = el_copytree(e1);
                        e2->E1->E1->EV.sp.Vsym = sx;
                        e1 = el_bint(OPeq,t,e1,e2);
                        if (tyaggregate(t->Tty))
                        {   e1->Eoper = OPstreq;
                            e1 = cpp_structcopy(e1);
                            lastoffset = (targ_size_t)-1;
                        }
                        e = el_combine(e,e1);
                    }
                }
            }

            /* Set the return value (this)      */
            e = el_combine(e,el_var(sthis));
            f->Fstartblock->Belem = e;

            f->Fflags |= flags2;

            cstate.CSpsymtab = psymtabsave;
            pstate.STinopeq = inopeqsave;
            }

            if (flag & 2)
                st->Sopeq2 = sopeq;
            else
            {   st->Sopeq = sopeq;
                if (!(sopeq->Sfunc->Fflags & Fgen) || SymInline(sopeq))
                    st->Sopeq2 = sopeq;
            }
        }
    }
}

/*********************************
 * Determine if constructor scpct is a copy constructor.
 * Returns:
 *      0       not a copy constructor
 *      1       X(X&)
 *      2       X(const X&)
 */

int n2_iscopyctor(symbol *scpct)
{
    /* Copy constructor is defined as one whose first argument
       is an X&.
       Subsequent arguments must be non-existent, optional, or
       have default arguments.
     */
    param_t *p;
    Classsym *stag;
    int result;

    //printf("n2_iscopyctor(scpct = %p, '%s')\n", scpct, cpp_prettyident(scpct));
    symbol_debug(scpct);
    stag = scpct->Stype->Tnext->Tnext->Ttag;
    symbol_debug(stag);
    assert(stag == scpct->Sfunc->Fclass);
    result = 0;

    if (!(scpct->Sfunc->Fflags & Fctor))
    {   assert(errcnt);                 // only happens upon error
        goto Lret;
    }

    p = scpct->Stype->Tparamtypes;
    if (p)
    {   type *t = p->Ptype;

        type_debug(t);
        if (tyref(t->Tty) &&
            tybasic(t->Tnext->Tty) == TYstruct &&
            t->Tnext->Ttag == stag &&
            (!p->Pnext || p->Pnext->Pelem) /* no more args or defaults */
           )
        {
            result = 1;
            //if (t->Tty & mTYconst)            // if X(const X&)
            if (t->Tnext->Tty & mTYconst)       // if X(const X&)
                result++;
        }
    }
Lret:
    //printf("n2_iscopyctor(scpct = %p) result = %d\n", scpct, result);
    return result;
}


/***************************************
 * Return !=0 if symbol is an internally generated one that should
 * not be copied or assigned.
 */

STATIC int struct_internalmember(symbol *s)
{   const char *p;

    symbol_debug(s);
    p = s->Sident;
    return p[0] == '_' && p[1] == '_' &&
        (
                strcmp(p,cpp_name_vptr ) == 0 ||
                strcmp(p,cpp_name_vbptr) == 0
        );
}

/******************************************
 * Look for copy constructor for class stag.
 */

void n2_lookforcopyctor(Classsym *stag)
{   struct_t *st;

    //dbg_printf("n2_lookforcopyctor('%s')\n",stag->Sident);
    symbol_debug(stag);
    st = stag->Sstruct;
    if (!st->Scpct)                     /* if copy ctor doesn't exist   */
    {   symbol *scpct;
        type *tclass = stag->Stype;

        /* If any copy ctor is defined for this class, don't generate one */
        for (scpct = st->Sctor; scpct; scpct = scpct->Sfunc->Foversym)
        {
            /* Give priority to one with a const X&     */
            int result;

            result = n2_iscopyctor(scpct);
            if (result)
            {
                st->Scpct = scpct;
                if (result == 2)                /* if X(const X&)       */
                {
                    break;                      /* found it             */
                }
                /* Keep looking, maybe we'll find a const one   */
            }
        }
    }
}

/*********************************
 * Generate copy constructor if possible for class stag.
 * X::X(const X&);
 * Input:
 *      flag    !=0 if generate error message
 */

void n2_createcopyctor(Classsym *stag,int flag)
{   struct_t *st;

    //dbg_printf("n2_createcopyctor('%s',%d)\n",stag->Sident,flag);
    symbol_debug(stag);
    st = stag->Sstruct;
    if (!st->Scpct)                     /* if copy ctor doesn't exist   */
    {   symbol *scpct;
        type *tclass = stag->Stype;

        if (!st->Scpct)
        {   /* Generate one     */
            symlist_t sl;
            baseclass_t *b;
            tym_t tyqual = mTYconst;    /* type qualifier for arg to ctor */
            Classsym *stag2;
            int bitcopy;                /* !=0 if we can bit copy the struct */
            int flags2;
            type *t;
            symbol *s;

            //dbg_printf("Generating copy ctor for '%s'\n",stag->Sident);
            flags2 = Fnodebug;
            bitcopy = STRbitcopy;
            /* If virtual functions or virtual base classes     */
            if (st->Svirtual || st->Svirtbase)
                bitcopy = 0;

            /* Determine feasability    */
            for (b = st->Sbase; b; b = b->BCnext)
            {
                stag2 = b->BCbase;
                goto L1;                /* share some common code       */
            L2: ;
            }
            for (sl = st->Sfldlst; sl; sl = list_next(sl))
            {   s = list_symbol(sl);

                /* If member has a private copy ctor */
                if (s->Sclass == SCmember)
                {   t = type_arrayroot(s->Stype);

                    if (tybasic(t->Tty) == TYstruct)
                    {
                        stag2 = t->Ttag;
                    L1:
                        n2_createcopyctor(stag2,flag);
                        bitcopy &= stag2->Sstruct->Sflags;
                        scpct = stag2->Sstruct->Scpct;
                        if (!scpct)
                            goto cant;
                        /* If copy constructor is private       */
                        if ((scpct->Sflags & SFLpmask) == SFLprivate)
                            goto cant;
#if 1
                        if ((scpct->Sflags & SFLpmask) == SFLprotected &&
                            !cpp_classisfriend(stag2,stag) &&
                            (!c1isbaseofc2(NULL,stag2,stag) ||
                             cpp_findaccess(scpct,stag) == SFLpublic)
                           )
                            goto cant;
#endif
                        /* If first parameter is not const      */
                        if (!(scpct->Stype->Tparamtypes->Ptype->Tnext->Tty & mTYconst))
                            tyqual = 0;
                        /* If we didn't generate it ourselves, can't use */
                        /* bitcopy because we don't know if we can       */
                        if (!(scpct->Sfunc->Fflags & Fbitcopy))
                            bitcopy &= ~STRbitcopy;
                        flags2 &= scpct->Sfunc->Fflags;
                        if (b)
                            goto L2;
                    }
                }
                continue;

        cant:   if (flag)
                    // can't generate copy ctor
                    cpperr(EM_nogen_cpct,stag->Sident,stag2->Sident);
                return;
            }

            /* Define function symbol returning X&      */
            st->Sflags |= bitcopy;

            /* Whether the function is inline or not is determined by   */
            /* its size and whether we can bitcopy the struct           */
            scpct = n2_createfunc(stag,cpp_name_ct,newpointer(tclass),
                (bitcopy || st->Sstructsize <= 4) ? Finline | Fctor : Fctor,
                CFcopyctor);
            type_setty(&scpct->Stype->Tparamtypes->Ptype->Tnext,tyqual | TYstruct);

            {
            elem *e = NULL;
            elem *e1,*e2;
            symbol *sthis;
            symbol *sx;
            unsigned lastoffset;

            sthis = cpp_getthis(scpct);
            sx = cpp_getlocalsym(scpct,"_param__P1");

            /* Note that unions always wind up as bit copy(!)   */
            if (bitcopy)
            {   scpct->Sfunc->Fflags |= Fbitcopy;
                if (!(st->Sflags & STR0size))
                {   e1 = el_unat(OPind,tclass,el_var(sthis));
                    e2 = reftostar(el_var(sx));
                    e = el_bint(OPstreq,tclass,e1,e2);
                }
            }
            else
            {
                /* Do the initialization by creating a base/member      */
                /* initialization list and letting cpp_fixconstructor() */
                /* build the actual code.                               */
                /* Copy bit fields en masse to avoid gross inefficiency */
                meminit_t *m;
                list_t bi = NULL;
                int i;

                /* Generate initializers for base classes.
                   Go through loop once for non-virtual bases, then
                   for virtual bases.
                   Remember that since virtual base initialization is
                   done only by the most derived class, we have to generate
                   an initializer for all the virtual bases that appear
                   in the inheritance tree.
                 */
                for (i = 0; i < 2; i++)
                {
                    b = i ? st->Sbase : st->Svirtbase;
                    for (; b; b = b->BCnext)
                    {
                        if (i && b->BCflags & BCFvirtual)
                            continue;           /* already done         */
                        /*dbg_printf("Generating initializer for base '%s'\n",b->BCbase->Sident);*/
                        e2 = el_var(sx);
                        el_settype(e2,newpointer(e2->ET->Tnext));       /* ref to ptr */
                        c1isbaseofc2(&e2,b->BCbase,stag);
                        e2 = el_unat(OPind,b->BCbase->Stype,e2);

                        m = (meminit_t *) MEM_PARF_CALLOC(sizeof(meminit_t));
                        m->MIsym = b->BCbase;
                        list_append(&m->MIelemlist,e2);
                        list_append(&bi,m);
                    }
                }

                /* Initialize the members                               */
                lastoffset = (unsigned int)-1;          /* an invalid value     */
                e = NULL;
                for (sl = st->Sfldlst; sl; sl = list_next(sl))
                {   symbol *s = list_symbol(sl);
                    type *t;
                    int op = OPeq;

                    symbol_debug(s);
                    t = s->Stype;
                    type_debug(t);
                    if (s->Sclass == SCfield)
                    {   /* Copy all the fields at once  */
                        if (s->Smemoff == lastoffset)
                            continue;           /* already copied       */
                        goto L3;
                    }
                    else if (s->Sclass == SCmember)
                    {
                        /* don't initialize __vptr or __Pxx members */
                        if (struct_internalmember(s))
                        {   lastoffset = (unsigned int)-1;
                            continue;
                        }

                        /* Array initializations of classes with ctors  */
                        /* are handled by cpp_fixconstructor().         */
                        if (tybasic(t->Tty) == TYarray)
                        {   type *tr;

                            tr = type_arrayroot(t);
                            if (tybasic(tr->Tty) == TYstruct &&
                                tr->Ttag->Sstruct->Sctor)
                            {   lastoffset = (unsigned int)-1;
                                //continue;     /* defer to fixconstructor */
                            }
                            else
                                op = OPstreq;   /* use bit copy on array   */
                        }

                    L3:
                        if (lastoffset != -1 && tybasic(t->Tty) != TYstruct)
                        {   // Merge with previous member copy
                            // (Skip aggregates because Enumbytes would
                            // be stomped on later by outelem())
                            type *ta;

                            elem_debug(e1);
                            e1->Eoper = OPstreq;
                            ta = type_allocn(TYarray,tschar);
                            ta->Tdim = s->Smemoff + type_size(t) - lastoffset;
                            el_settype(e1,ta);
                            el_settype(e1->E1,ta);
                            el_settype(e1->E2,ta);
                            continue;
                        }
                        lastoffset = s->Smemoff;

                        e2 = el_var(sx);
                        /* Convert reference to pointer */
                        el_settype(e2,newpointer(e2->ET->Tnext));
                        if (tyref(t->Tty))
                            t = reftoptr(t);
                        e2 = el_bint(OPadd,newpointer(t),
                            e2,el_longt(tsint,lastoffset));
                        e2 = el_unat(OPind,t,e2);

                        // Decide if we want to assign now or use initializer
                        if (s->Sclass == SCfield || op == OPstreq ||
                            tyscalar(t->Tty))
                        {
                            e1 = el_copytree(e2);
                            e1->E1->E1->EV.sp.Vsym = sthis;
                            e1 = el_bint(op,t,e1,e2);
                            e = el_combine(e,e1);
                        }
                        else
                        {
                            m = (meminit_t *) MEM_PARF_CALLOC(sizeof(meminit_t));
                            m->MIsym = s;
                            list_append(&m->MIelemlist,e2);
                            list_append(&bi,m);
                            lastoffset = (unsigned int)-1;
                        }
                    }
                }
                /*scpct->Sfunc->Fbaseinit = bi;*/
                cpp_buildinitializer(scpct,bi,1);
            }

            scpct->Sfunc->Fflags |= flags2;
            scpct->Sfunc->Fstartblock->Belem = e;

            /* The coup-de-grace, turn it from a data structure into code */
            cpp_fixconstructor(scpct);
            }

            st->Scpct = scpct;
            if (!st->Sctor)
                st->Sctor = scpct;
            //printf("generating done\n");
        }
    }
}


/***************************************
 * C++98 13.3.1.1.2
 * Generate any surrogate call functions.
 */

void n2_createsurrogatecall(Classsym *stag)
{
    /* Generate operator() overloads of the form:
     *   R call-function (conversion-type-id F, P1 a1, ..., Pn an)
     *   {
     *      return F(a1, ..., an);
     *   }
     * for each conversion function of the form:
     *   operator conversion-type-id() cv-qualifier;
     */

    struct_t *st = stag->Sstruct;

    //printf("n2_createsurrogatecall('%s')\n", stag->Sident);
    for (list_t cl = st->Scastoverload; cl; cl = list_next(cl))
    {
        symbol *s = list_symbol(cl);
        symbol_debug(s);
        type *t = s->Stype->Tnext;
        symbol *sfunc;
        symbol *funcsym_save;
        func_t *f;
        type *tf;
        int nparams;
        param_t *p;

        if (tyref(t->Tty))
            t = t->Tnext;
        if (typtr(t->Tty))
            t = t->Tnext;
        if (!tyfunc(t->Tty))
            continue;

        // Create the surrogate function
        type *tret = t->Tnext;
        type *tfunc = n2_typector(stag, tret);
        tfunc->Tflags |= t->Tflags & TFfixed;

        // First parameter is conversion-type-id
        //param_append_type(&tfunc->Tparamtypes, s->Stype->Tnext);

        // Subsequent parameters P1 ... Pn
        nparams = 1;
        for (p = t->Tparamtypes; p; p = p->Pnext)
        {
            param_append_type(&tfunc->Tparamtypes, p->Ptype);
            nparams++;
        }

        sfunc = symbol_name(cpp_opident(OPcall), SCextern, tfunc);

        f = sfunc->Sfunc;
        f->Fflags |= Fsurrogate;
        f->Fsurrogatesym = s;
        sfunc->Sflags |= SFLpublic;
        n2_addfunctoclass(stag, sfunc, 0);

        /* We cannot generate the body of the class, because if
         * the function type is varargs.
         */
    }
}


/******************************
 * We need a unique identifier. Generate one.
 * Return a pointer to a parc_malloc'd string.
 */

char *n2_genident()
{
    static int num = 0;
    char *p;
    char *fn;

    /*  Base generated identifier on current source file, in addition
        to sequence number. This is so that when symbols are read
        from a precompiled header, they will not conflict with
        existing generated identifiers.
     */

    fn = file_unique();
#if TX86
    p = (char *) parc_malloc(7 + strlen(fn));
#else
    p = (char *) MEM_PARF_MALLOC(7 + strlen(fn));
#endif
#if HTOD
    sprintf(p,"_N%d",++num);            // p is free'd right after call to n2_genident
#else
    sprintf(p,"_N%d%s",++num,fn);       // p is free'd right after call to n2_genident
#endif
    if (strlen(p) > IDMAX)
        p[IDMAX] = 0;
    return p;
}

/****************************************
 * Determine if symbol needs a 'this' pointer.
 */

int Symbol::needThis()
{
    //printf("needThis() '%s'\n", Sident);
#ifdef DEBUG
    assert(isclassmember(this));
#endif
    if (Sclass == SCmember || Sclass == SCfield)
        return 1;
    if (tyfunc(Stype->Tty) && !(Sfunc->Fflags & Fstatic))
        return 1;
    return 0;
}

#endif /* !SPP */
