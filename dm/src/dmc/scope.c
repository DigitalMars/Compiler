/**
 * Implementation of the
 * $(LINK2 http://www.digitalmars.com/download/freecompiler.html, Digital Mars C/C++ Compiler).
 *
 * Copyright:   Copyright (c) 1993-1998 by Symantec, All Rights Reserved
 *              Copyright (c) 2000-2017 by Digital Mars, All Rights Reserved
 * Authors:     $(LINK2 http://www.digitalmars.com, Walter Bright)
 * License:     Distributed under the Boost Software License, Version 1.0.
 *              http://www.boost.org/LICENSE_1_0.txt
 * Source:      https://github.com/DigitalMars/Compiler/blob/master/dm/src/dmc/scope.c
 */

#if !SPP

#include        <stdio.h>
#include        <string.h>
#include        "cc.h"
#include        "parser.h"
#include        "global.h"
#include        "speller.h"
#if XCOFF_OBJ
#include        "cgobjxcoff.h"
#endif

#if M_UNIX
#include        <stdlib.h>
#endif

static char __file__[] = __FILE__;      /* for tassert.h                */
#include        "tassert.h"

#if TX86
Scope *scope_end;                       // pointer to innermost scope
#endif

/**********************************
 */

void scope_print()
{
    printf("scope_print()\n");
    for (Scope *sc = scope_end; sc; sc = sc->next)
    {
        printf("\tsctype = %x\n", sc->sctype);
    }
}

/////////////////////////////////
//

void Scope::setScopeEnd(Scope *s_end)
{
    scope_end = s_end;
}

/////////////////////////////////
//

int Scope::inTemplate()
{
    return scope_find(SCTtempsym | SCTtemparg) != 0;
}

/////////////////////////////////
// Checks to see if Symbol s is not findable if it was declared
// after the point of definition of a template.
// Returns:
//      NULL    not findable
//      s       findable
//

inline Symbol *Scope::checkSequence(Symbol *s)
{
#if 1
    symbol_debug(s);
    //printf("Scope::checkSequence('%s') s->Ssequence = x%x, pstate = x%x\n", s->Sident, s->Ssequence, pstate.STmaxsequence);
    if (s->Ssequence > pstate.STmaxsequence)
    {
        //printf("Scope::checkSequence('%s') s->Ssequence = x%x, pstate = x%x\n", s->Sident, s->Ssequence, pstate.STmaxsequence);
        //printf("s->Sscope = %p\n", s->Sscope);
        //symbol_print(s);

        if (config.flags4 & CFG4dependent &&
            // Only applies to global or namespace scoped Symbols
            sctype & (SCTglobal | SCTnspace) &&
            // Check functions during function overloading
            !tyfunc(s->Stype->Tty))
            s = NULL;
    }
#endif
    return s;
}

//////////////////////////////////
// Sort through covered symbols and aliases to find the real symbol.
//

inline symbol *Scope::findReal(symbol *s, unsigned sct)
{
    //printf("findReal('%s')\n", symbol_ident(s));
    if (s)
    {
        symbol_debug(s);
        if (sct & SCTcover && s->Scover /*&& checkSequence(s->Scover)*/)
        {
            s = s->Scover;
            symbol_debug(s);
            assert(s->Sclass != SCalias);
        }
        else
        {   s = checkSequence(s);
            if (s && s->Sclass == SCalias)
            {   s = s->Smemalias;
                symbol_debug(s);
            }
        }
    }
    return s;
}

///////////////////////////////
// Find scope matching sct.

Scope *scope_find(unsigned sct)
{   Scope *sc;

    for (sc = scope_end; sc; sc = sc->next)
    {
        scope_debug(sc);
        if (sc->sctype & sct)
            break;
    }
    return sc;
}

////////////////////////////////
// If a namespace is in scope, find it and return the namespace.

Nspacesym *scope_inNamespace()
{   Scope *sc;

    for (sc = scope_end; sc; sc = sc->next)
    {
        scope_debug(sc);
        if (sc->sctype & SCTnspace)
        {
            return (Nspacesym *)sc->root;
        }
    }
    return NULL;
}

///////////////////////////////
// Search all scopes for symbol starting from innermost scope.

struct ScopeSearch
{
    unsigned sct;
};

static void *scope_search_fp(void *arg, const char *id)
{
    ScopeSearch *ss = (ScopeSearch *)arg;

    Scope *sc;

    return scope_searchx(id, ss->sct, &sc);
}

symbol *scope_search_correct(const char *id, unsigned sct)
{
    ScopeSearch ss;
    ss.sct = sct;

    return (symbol *)speller(id, &scope_search_fp, &ss, idchars);
}

///////////////////////////////
// Search all scopes for symbol starting from innermost scope.

symbol *scope_search(const char *id,unsigned sct)
{   Scope *sc;

    return scope_searchx(id,sct,&sc);
}

///////////////////////////////
// Same, but don't print errors, just return NULL.

#if 0
static int scope_search_noerr;

symbol *scope_search2(const char *id,unsigned sct)
{   Scope *sc;
    symbol *s;

    scope_search_noerr++;
    s = scope_searchx(id,sct,&sc);
    scope_search_noerr--;
    return s;
}
#endif

///////////////////////////////
// Same as scope_search(), but also returns scope found.

symbol *scope_searchx(const char *id,unsigned sct,Scope **psc)
{   symbol *s;
    Scope *sc;

    //printf("scope_searchx('%s',x%x)\n",id,sct);
    s = NULL;
    for (sc = scope_end; sc; sc = sc->next)
    {
        //printf("\tsc = %p\n", sc);
        assert(sc != sc->next);
        scope_debug(sc);
        if (sc->sctype & sct)
        {
          if (CPP)
          {
            // We have to search through all using namespace directives
            Scope *scn;
            symbol *s2;         // subsequent symbols found
            symbol *s0;         // symbol in current scope

            //printf("in scope %p, root = %p, x%x\n",sc, sc->root, sc->sctype & sct);
            s = NULL;           // first symbol found
            s0 = NULL;
            for (scn = sc; scn; scn = scn->using_scope)
            {
                //printf("in using %p, root = %p, search=%p\n",scn,scn->root,scn->fpsearch);
                if (scn->root)
                {
                    s2 = (*scn->fpsearch)(id,scn->root);
                    if (s2 &&                           // if in that table
                        (s2 = sc->findReal(s2, sct)) != NULL)
                    {   symbol_debug(s2);
                        //printf("found symbol\n");
                        if (s)
                        {   // We have a multiply defined symbol. If it
                            // is not a function, it is an ambiguity error.
                            // Otherwise, we need to create an Foversym list
                            // symbol of all the functions across the namespaces
                            // so that overload resolution will work across
                            // using-directives.

                            if (s == s2)
                                goto Lcont;

                            if (!tyfunc(s->Stype->Tty) ||
                                !tyfunc(s2->Stype->Tty))
                            {
                                //if (!scope_search_noerr)
                                    err_ambiguous(s,s2);        // ambiguity error
                                break;
                            }

                            if (!s0)
                            {
                                // Create symbol in current scope
                                s0 = symbol_funcalias(s);
                                scope_addx(s0,sc);
                                nspace_addfuncalias(s0,s);
                            }
                            else if (s != s0)
                                nspace_addfuncalias(s0,s);
                            s = s0;

                            nspace_addfuncalias(s0,s2);
                        }
                        else
                        {
                            s = s2;
                            if (scn == sc)              // if not in a using-directive
                                s0 = s;
                        }
                    }
                }
            Lcont:
                if (!(sct & SCTnspace))
                    break;                      // do not search using-directives
            }
            if (s)
            {
                if (sct & SCTcolcol)
                {
                  Ls:
                    switch (s->Sclass)
                    {
                        case SCtypedef:
                            if (tybasic(s->Stype->Tty) == TYstruct)
                                break;
                            else
                            {   s = NULL;
                                continue;
                            }

                        case SCstruct:
                        case SCnamespace:
                        case SCtemplate:
                            break;

                        case SCalias:
                            s = ((Aliassym *)s)->Smemalias;
                            goto Ls;

                        default:
                            // Skip this symbol
                            s = NULL;
                            continue;
                    }
                }
                break;
            }
          }
          else
          {
            if (sc->root)
            {
                s = (*sc->fpsearch)(id,sc->root);
                if (s)                          // if in that table
                {   symbol_debug(s);
                    break;
                }
            }
          }
        }
    }
    *psc = sc;
    return s;
}

///////////////////////////////
// Search innermost scope for symbol

symbol *scope_searchinner(const char *id,unsigned sct)
{   symbol *s;
    Scope *sc;

    s = NULL;
    for (sc = scope_end; sc; sc = sc->next)
    {
        scope_debug(sc);
        if (sc->sctype & sct)
        {
            if (sc->root)
                s = (*sc->fpsearch)(id,sc->root);
            if (s)
            {   symbol_debug(s);
                if (CPP && !(sct & SCTnoalias)) // if in that table
                {
                    s = sc->findReal(s, sct);
                }
            }
            break;
        }
    }
    return s;
}

/////////////////////////////////
// Search outer scope for symbol beginning at scope enclosing *psc.

symbol *scope_searchouter(const char *id,unsigned sct,Scope **psc)
{   symbol *s;
    Scope *sc;

    s = NULL;
    for (sc = (*psc ? (*psc)->next : scope_end); sc; sc = sc->next)
    {
        if (sc->sctype & sct && sc->root)
        {
            s = (*sc->fpsearch)(id,sc->root);
            if (s)                              // if in that table
            {   symbol_debug(s);
                if (CPP)
                {
                    s = sc->findReal(s, sct);
                    if (!s)
                        continue;
                }
                break;
            }
        }
    }
    *psc = sc;
    return s;
}

///////////////////////////////
// Define symbol in innermost scope.
// Error if it is already there.

symbol *scope_define(const char *id,unsigned sct,enum SC sclass)
{   symbol *s;

    //printf("scope_define('%s',x%x)\n",id,sct);
    s = symbol_calloc(id);
    s->Sclass = sclass;
    return scope_add(s, sct);
}

////////////////////////////////////
// Add existing symbol in innermost scope.
// Error if it is already there.

symbol *scope_add(symbol *s, unsigned sct)
{   Scope *sc;

    symbol_debug(s);
    for (sc = scope_end; 1; sc = sc->next)
    {
        assert(sc);
        //printf("sc->sctype %x\n",sc->sctype);
        scope_debug(sc);
        if (sc->sctype & sct)
            return scope_addx(s,sc);
    }
}

/***************************************
 * Add symbol s to scope sc.
 */

symbol *scope_addx(symbol *s,Scope *sc)
{
    unsigned sctype;

    //printf("scope_addx('%s', sct = x%x\n",s->Sident, sc->sctype);
    scope_debug(sc);
    sctype = sc->sctype;
#if TX86
    if (CPP && sctype & SCTnspace)
        nspace_add(sc->root,s);
    else
#else
    if (CPP && sctype & (SCTclass | SCTmfunc))
        symbol_addtotree(&((symbol *)sc->root)->Sstruct->Sroot, s);
    else
#endif
    {
        symbol_addtotree((symbol **)&sc->root,s);

        if (sctype & SCTjoin)
        {
            if ((*sc->next->fpsearch)(s->Sident, sc->next->root))
            {
                synerr(EM_multiple_def, s->Sident);
            }
        }

        /* Consider:
         *      namespace X
         *      {
         *        void p()
         *        {
         *            extern void q();  // q is a member of namespace X
         *        }
         *      }
         */
        if (s->Sclass == SCextern && sctype & SCTlocal && CPP)
        {
            for (Scope *sc2 = sc; sc2; sc2 = sc2->next)
            {
                if (sc2->sctype & SCTnspace)
                {
                    s->Sscope = (Symbol *)sc2->root; // make s a member of the namespace
                    break;
                }
            }
        }
    }
#if TX86
    if (sctype & (SCTglobal | SCTglobaltag))
    {
        ph_add_global_symdef(s, sctype);
    }
#else
//
// If a nested variable hides an outer scope struct tag, then
// s->Scover needs to get set to point to the outer scope struct tag.
//
    if (CPP && !s->Scover) {
        symbol *sCover;
        switch (s->Sclass) {
        case SCstruct:
        case SCenum:
            break;
        default:
            for (sc = sc->next; sc; sc = sc->next) {
                if (sc->root)
                {
                    pstate.STno_ambig = 1;
                    sCover = (*sc->fpsearch)((char *)s->Sident,(symbol *)sc->root);
                    pstate.STno_ambig = 0;

                    if (sCover) {
                        if (sCover->Scover)
                            s->Scover = sCover->Scover;
                        else {
                            switch (sCover->Sclass) {
                            case SCstruct:
                            case SCenum:
                                //symbol_keep(sCover);
                                s->Scover = sCover;
                                break;

                            }
                        }
                    }
                }
            }
           break;
        }
    }
#endif
    return s;
}

//////////////////////////////
// Push class scope onto stack of scopes.

void scope_pushclass(Classsym *stag)
{
    scope_push(stag,(scope_fp)struct_searchmember,SCTclass);
}

///////////////////////////////////////
//

void scope_push_symbol(symbol *s)
{
    if (s->Sclass == SCnamespace)
        scope_push_nspace((Nspacesym *)s);
    else if (s->Sclass == SCstruct)
        scope_pushclass((Classsym *)s);
    else
        assert(0);
}

///////////////////////////////
// Storage allocator

static Scope *scope_freelist;

STATIC Scope * scope_calloc()
{   Scope *sc;

#if TX86
    if (scope_freelist)
    {
        sc = scope_freelist;
        scope_freelist = sc->next;
        memset(sc,0,sizeof(Scope));
    }
    else
    {
        sc = (Scope *) calloc(1,sizeof(Scope));
        //printf("scope_calloc(%d) = %p\n", sizeof(Scope), sc);
    }
#else
    sc = (Scope *) MEM_PH_CALLOC(sizeof(Scope));
#endif
#if DEBUG
    sc->id = IDscope;
#endif
    return sc;
}

__inline void scope_free(Scope *sc)
{
    if (CPP)
    {
        while (sc)
        {
            scope_debug(sc);
#if DEBUG
            sc->id = 0;
#endif
#if TX86
            sc->next = scope_freelist;
            scope_freelist = sc;
            sc = sc->using_scope;
#else
            Scope *scn;

            scn = sc->using_scope;
            MEM_PH_FREE(sc);
            sc = scn;
#endif
        }
    }
    else
    {
#if TX86
        sc->next = scope_freelist;
        scope_freelist = sc;
#else
        MEM_PH_FREE(sc);
#endif
    }
}

//////////////////////////////
// Push scope onto stack of scopes.

void scope_push(void *root,scope_fp fpsearch,int sctype)
{
    Scope *sc;

    sc = scope_calloc();
    //printf("scope_push(sctype = x%x), sc = %p\n",sctype,sc);
    sc->next = scope_end;
    sc->root = root;
    sc->fpsearch = fpsearch;
    sc->sctype = sctype;
    sc->using_scope = NULL;
    sc->using_list = NULL;
    scope_end = sc;
    scope_debug(sc);
}

//////////////////////////////
// Pop scope off of stack of scopes.

#if TX86
void *scope_pop()
#else
symbol *scope_pop()
#endif
{
    Scope *sc;
    symbol *root;
    list_t ul;

    //printf("scope_pop(): scope_end = %p\n",scope_end);
    assert(scope_end);
    scope_debug(scope_end);

    if (CPP)
    {
    // Remove using-directives from enclosing scopes
    for (ul = scope_end->using_list; ul; ul = list_next(ul))
    {   Scope *scx;

        scx = (Scope *) list_ptr(ul);
        ul = list_next(ul);
        sc = (Scope *) list_ptr(ul);
        scope_debug(sc);

        //printf("removing scope %p from scope %p,",sc,scx);
        while (1)
        {   scope_debug(scx);
            if (scx->using_scope == sc)
            {
                scx->using_scope = sc->using_scope;
                sc->using_scope = NULL;
                scope_free(sc);
                break;
            }
            scx = scx->using_scope;
            assert(scx);
        }
    }
    list_free(&scope_end->using_list,FPNULL);
    }

    sc = scope_end;
    root = (symbol *)sc->root;
    scope_end = sc->next;
#if DEBUG
    if (sc->next)
        scope_debug(sc->next);
#endif
    scope_free(sc);
    return root;
}

//////////////////////////////
// Add using scope to current scope.
// Implements using-directive.

void scope_using(Nspacesym *sn,scope_fp fpsearch,int sctype,Scope *sce)
{
    Scope *sc;
    Scope *scx;
    Scope *scxn;

    //printf("scope_using(scn=%s, fpsearch=%p, sctype=x%x, sce=%p)\n",sn->Sident,fpsearch,sctype,sce);
    assert(sce);

    // Find nearest scope scx that encloses both sn and end_scope.
    for (scx = sce; 1; scx = scxn)
    {   Nspacesym *sn1;

        scxn = scx->next;
        if (!scxn || scx->sctype & SCTglobal)   // if scx is outmost scope
            break;                      // then that's the one we use
        if (!(scx->sctype & SCTnspace))
            continue;
        for (sn1 = sn; sn1; sn1 = (Nspacesym *)sn1->Sscope)
        {
            if (sn1 == (Nspacesym *)scx->root)
                goto L1;
        }
    }
L1: ;

#if 0
    // If sn is already in the using_scope list of scx, then don't add it again.
    for (sc = scx; sc; sc = sc->using_scope)
        if (sn == (Nspacesym *)sc->root)
            return;
#endif

    sc = scope_calloc();
    sc->root = sn;
    sc->fpsearch = fpsearch;
    sc->sctype = sctype;

    // Add using-directive scope sc to enclosing scope scx
    //printf("Installed scope %p into scope %p\n",sc,scx);
    sc->using_scope = scx->using_scope;
    scx->using_scope = sc;

    // Remember scx and sc so we can remove them when sce goes
    // out of scope.
    list_append(&sce->using_list,scx);
    list_append(&sce->using_list,sc);
}

/************************************
 * Get list of enclosing scopes for symbol, in reverse order.
 */

list_t scope_getList(symbol *s)
{
    list_t scopelist = NULL;

    while ((s = s->Sscope) != NULL)
    {
        //printf("\tadding scope '%s'\n", s->Sident);
        list_prepend(&scopelist, s);
    }
    return scopelist;
}

/**************************************
 * Push all scopes enclosing symbol s.
 * Returns:
 *      number of scopes pushed
 */

int scope_pushEnclosing(symbol *s)
{
    int nscopes = 0;
    list_t scopelist = scope_getList(s);

    // Push all the scopes
    for (list_t l = scopelist; l; l = list_next(l))
    {   symbol *ss = (symbol *) list_ptr(l);

        if (ss->Sclass == SCstruct)
        {
            if (ss->Sstruct->Stempsym)
            {
                template_createsymtab(
                    ss->Sstruct->Stempsym->Stemplate->TMptpl,
                    ss->Sstruct->Sarglist);
                nscopes++;
            }
        }
        scope_push_symbol(ss);
        nscopes++;
    }

    list_free(&scopelist, FPNULL);
    return nscopes;
}

/***************************************
 * Unwind scopes.
 */

void scope_unwind(int nscopes)
{
    // Unwind scopes
    while (nscopes--)
    {
        if (scope_end->sctype == SCTtempsym)
            template_deletesymtab();
        else
            scope_pop();
    }
}

////////////////////////////////
//

void scope_term()
{
#if TERMCODE
    Scope *sc;

    while (scope_freelist)
    {   sc = scope_freelist->next;
#if TX86
        free(scope_freelist);
#else
        MEM_PH_FREE(scope_freelist);
#endif
        scope_freelist = sc;
    }
#endif
}

#endif // !SPP
