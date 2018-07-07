/**
 * Implementation of the
 * $(LINK2 http://www.digitalmars.com/download/freecompiler.html, Digital Mars C/C++ Compiler).
 *
 * Copyright:   Copyright (c) 1993-1998 by Symantec, All Rights Reserved
 *              Copyright (c) 2000-2017 by Digital Mars, All Rights Reserved
 * Authors:     $(LINK2 http://www.digitalmars.com, Walter Bright)
 * License:     Distributed under the Boost Software License, Version 1.0.
 *              http://www.boost.org/LICENSE_1_0.txt
 * Source:      https://github.com/DigitalMars/Compiler/blob/master/dm/src/dmc/dscope.d
 */

module dscope;

version (SPP)
{
}
else
{
import core.stdc.stdio;
import core.stdc.stdlib;
import core.stdc.string;

import dmd.backend.cdef;
import dmd.backend.cc;
import dmd.backend.global;
import dmd.backend.ty;

import dmd.backend.dlist;
import dmd.backend.memh;

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

enum TX86 = 1;

__gshared Scope *scope_end;                       // pointer to innermost scope
__gshared Scope *scope_freelist;

/**********************************
 */

void scope_print()
{
    printf("scope_print()\n");
    for (Scope *sc = scope_end; sc; sc = sc.next)
    {
        printf("\tsctype = %x\n", sc.sctype);
    }
}

/////////////////////////////////
//

void scope_setScopeEnd(Scope *s_end)
{
    scope_end = s_end;
}

/////////////////////////////////
//

int scope_inTemplate()
{
    return scope_find(SCTtempsym | SCTtemparg) !is null;
}

/////////////////////////////////
// Checks to see if Symbol s is not findable if it was declared
// after the point of definition of a template.
// Returns:
//      null    not findable
//      s       findable
//

Symbol *scope_checkSequence(Scope *sc, Symbol *s)
{
    symbol_debug(s);
    //printf("scope_checkSequence('%s') s.Ssequence = x%x, pstate = x%x\n", &s.Sident[0], s.Ssequence, pstate.STmaxsequence);
    if (s.Ssequence > pstate.STmaxsequence)
    {
        //printf("Scope::checkSequence('%s') s.Ssequence = x%x, pstate = x%x\n", &s.Sident[0], s.Ssequence, pstate.STmaxsequence);
        //printf("s.Sscope = %p\n", s.Sscope);
        //symbol_print(s);

        if (config.flags4 & CFG4dependent &&
            // Only applies to global or namespace scoped Symbols
            sc.sctype & (SCTglobal | SCTnspace) &&
            // Check functions during function overloading
            !tyfunc(s.Stype.Tty))
            s = null;
    }
    return s;
}

//////////////////////////////////
// Sort through covered symbols and aliases to find the real symbol.
//

Symbol *scope_findReal(Scope* sc, Symbol *s, uint sct)
{
    //printf("findReal('%s')\n", symbol_ident(s));
    if (s)
    {
        symbol_debug(s);
        if (sct & SCTcover && s.Scover /*&& scope_checkSequence(sc, s.Scover)*/)
        {
            s = s.Scover;
            symbol_debug(s);
            assert(s.Sclass != SCalias);
        }
        else
        {   s = scope_checkSequence(sc, s);
            if (s && s.Sclass == SCalias)
            {   s = s.Smemalias;
                symbol_debug(s);
            }
        }
    }
    return s;
}

///////////////////////////////
// Find scope matching sct.

Scope *scope_find(uint sct)
{   Scope *sc;

    for (sc = scope_end; sc; sc = sc.next)
    {
        scope_debug(sc);
        if (sc.sctype & sct)
            break;
    }
    return sc;
}

////////////////////////////////
// If a namespace is in scope, find it and return the namespace.

Nspacesym *scope_inNamespace()
{   Scope *sc;

    for (sc = scope_end; sc; sc = sc.next)
    {
        scope_debug(sc);
        if (sc.sctype & SCTnspace)
        {
            return cast(Nspacesym *)sc.root;
        }
    }
    return null;
}

///////////////////////////////
// Search all scopes for symbol starting from innermost scope.

Symbol *scope_search_correct(const(char)* id, uint sct)
{
    extern (D) void *scope_search_fp(const(char)* id, ref int cost)
    {
        Scope *sc;

        return scope_searchx(id, sct, &sc);
    }

    return cast(Symbol *)speller(id, &scope_search_fp, &idchars[0]);
}

///////////////////////////////
// Search all scopes for symbol starting from innermost scope.

Symbol *scope_search(const(char)* id,uint sct)
{   Scope *sc;

    return scope_searchx(id,sct,&sc);
}

///////////////////////////////
// Same, but don't print errors, just return null.

static if (0)
{
/*private*/ __gshared int scope_search_noerr;

Symbol *scope_search2(const(char)* id,uint sct)
{   Scope *sc;
    Symbol *s;

    scope_search_noerr++;
    s = scope_searchx(id,sct,&sc);
    scope_search_noerr--;
    return s;
}
}

///////////////////////////////
// Same as scope_search(), but also returns scope found.

Symbol *scope_searchx(const(char)* id,uint sct,Scope **psc)
{   Symbol *s;
    Scope *sc;

    //printf("scope_searchx('%s',x%x)\n",id,sct);
    s = null;
    for (sc = scope_end; sc; sc = sc.next)
    {
        //printf("\tsc = %p\n", sc);
        assert(sc != sc.next);
        scope_debug(sc);
        if (sc.sctype & sct)
        {
          if (CPP)
          {
            // We have to search through all using namespace directives
            Scope *scn;
            Symbol *s2;         // subsequent symbols found
            Symbol *s0;         // symbol in current scope

            //printf("in scope %p, root = %p, x%x\n",sc, sc.root, sc.sctype & sct);
            s = null;           // first symbol found
            s0 = null;
            for (scn = sc; scn; scn = scn.using_scope)
            {
                //printf("in using %p, root = %p, search=%p\n",scn,scn.root,scn.fpsearch);
                if (scn.root)
                {
                    s2 = (*scn.fpsearch)(id,scn.root);
                    if (s2 &&                           // if in that table
                        (s2 = scope_findReal(sc, s2, sct)) !is null)
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

                            if (!tyfunc(s.Stype.Tty) ||
                                !tyfunc(s2.Stype.Tty))
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
                    switch (s.Sclass)
                    {
                        case SCtypedef:
                            if (tybasic(s.Stype.Tty) == TYstruct)
                                break;
                            else
                            {   s = null;
                                continue;
                            }

                        case SCstruct:
                        case SCnamespace:
                        case SCtemplate:
                            break;

                        case SCalias:
                            s = (cast(Aliassym *)s).Smemalias;
                            goto Ls;

                        default:
                            // Skip this symbol
                            s = null;
                            continue;
                    }
                }
                break;
            }
          }
          else
          {
            if (sc.root)
            {
                s = (*sc.fpsearch)(id,sc.root);
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

Symbol *scope_searchinner(const(char)* id,uint sct)
{   Symbol *s;
    Scope *sc;

    s = null;
    for (sc = scope_end; sc; sc = sc.next)
    {
        scope_debug(sc);
        if (sc.sctype & sct)
        {
            if (sc.root)
                s = (*sc.fpsearch)(id,sc.root);
            if (s)
            {   symbol_debug(s);
                if (CPP && !(sct & SCTnoalias)) // if in that table
                {
                    s = scope_findReal(sc, s, sct);
                }
            }
            break;
        }
    }
    return s;
}

/////////////////////////////////
// Search outer scope for symbol beginning at scope enclosing *psc.

Symbol *scope_searchouter(const(char)* id,uint sct,Scope **psc)
{   Symbol *s;
    Scope *sc;

    s = null;
    for (sc = (*psc ? (*psc).next : scope_end); sc; sc = sc.next)
    {
        if (sc.sctype & sct && sc.root)
        {
            s = (*sc.fpsearch)(id,sc.root);
            if (s)                              // if in that table
            {   symbol_debug(s);
                if (CPP)
                {
                    s = scope_findReal(sc, s, sct);
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

Symbol *scope_define(const(char)* id,uint sct,enum_SC sclass)
{   Symbol *s;

    //printf("scope_define('%s',x%x)\n",id,sct);
    s = symbol_calloc(id);
    s.Sclass = sclass;
    return scope_add(s, sct);
}

////////////////////////////////////
// Add existing symbol in innermost scope.
// Error if it is already there.

Symbol *scope_add(Symbol *s, uint sct)
{   Scope *sc;

    symbol_debug(s);
    for (sc = scope_end; 1; sc = sc.next)
    {
        if (!sc)
        {
            /* From fail14.5-1xb.cpp:
             *    template<class T1, int I> void sort<T1, I>(T1 data[I]){}  // error: 'sort' is not a class template
             *    int main() {
             *      int arr[7];
             *      sort(arr);
             *    }
             * The error is not recovered from properly, resulting in coming here.
             */
            exit(1);
        }
        //printf("sc.sctype %x\n",sc.sctype);
        scope_debug(sc);
        if (sc.sctype & sct)
            return scope_addx(s,sc);
    }
}

/***************************************
 * Add symbol s to scope sc.
 */

Symbol *scope_addx(Symbol *s,Scope *sc)
{
    uint sctype;

    //printf("scope_addx('%s', sct = x%x\n",&s.Sident[0], sc.sctype);
    scope_debug(sc);
    sctype = sc.sctype;
//#if TX86
    if (CPP && sctype & SCTnspace)
        nspace_add(sc.root,s);
    else
/+#else
    if (CPP && sctype & (SCTclass | SCTmfunc))
        symbol_addtotree(&(cast(Symbol *)sc.root).Sstruct.Sroot, s);
    else
#endif+/
    {
        symbol_addtotree(cast(Symbol **)&sc.root,s);

        if (sctype & SCTjoin)
        {
            if ((*sc.next.fpsearch)(&s.Sident[0], sc.next.root))
            {
                synerr(EM_multiple_def, &s.Sident[0]);
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
        if (s.Sclass == SCextern && sctype & SCTlocal && CPP)
        {
            for (Scope *sc2 = sc; sc2; sc2 = sc2.next)
            {
                if (sc2.sctype & SCTnspace)
                {
                    s.Sscope = cast(Symbol *)sc2.root; // make s a member of the namespace
                    break;
                }
            }
        }
    }
static if (TX86)
{
    if (sctype & (SCTglobal | SCTglobaltag))
    {
        ph_add_global_symdef(s, sctype);
    }
}
else
{
//
// If a nested variable hides an outer scope struct tag, then
// s.Scover needs to get set to point to the outer scope struct tag.
//
    if (CPP && !s.Scover) {
        Symbol *sCover;
        switch (s.Sclass) {
        case SCstruct:
        case SCenum:
            break;
        default:
            for (sc = sc.next; sc; sc = sc.next) {
                if (sc.root)
                {
                    pstate.STno_ambig = 1;
                    sCover = (*sc.fpsearch)(cast(char *)&s.Sident[0],cast(Symbol *)sc.root);
                    pstate.STno_ambig = 0;

                    if (sCover) {
                        if (sCover.Scover)
                            s.Scover = sCover.Scover;
                        else {
                            switch (sCover.Sclass) {
                            case SCstruct:
                            case SCenum:
                                //symbol_keep(sCover);
                                s.Scover = sCover;
                                break;

                            }
                        }
                    }
                }
            }
           break;
        }
    }
}
    return s;
}

//////////////////////////////
// Push class scope onto stack of scopes.

void scope_pushclass(Classsym *stag)
{
    scope_push(stag,cast(scope_fp)&struct_searchmember,SCTclass);
}

///////////////////////////////////////
//

void scope_push_symbol(Symbol *s)
{
    if (s.Sclass == SCnamespace)
        scope_push_nspace(cast(Nspacesym *)s);
    else if (s.Sclass == SCstruct)
        scope_pushclass(cast(Classsym *)s);
    else
        assert(0);
}

///////////////////////////////
// Storage allocator


/*private*/ Scope * scope_calloc()
{   Scope *sc;

static if (TX86)
{
    if (scope_freelist)
    {
        sc = scope_freelist;
        scope_freelist = sc.next;
        memset(sc,0,Scope.sizeof);
    }
    else
    {
        sc = cast(Scope *) calloc(1,Scope.sizeof);
        //printf("scope_calloc(%d) = %p\n", Scope.sizeof, sc);
    }
}
else
{
    sc = cast(Scope *) MEM_PH_CALLOC(Scope.sizeof);
}
    debug sc.id = Scope.IDscope;
    return sc;
}

void scope_free(Scope *sc)
{
    if (CPP)
    {
        while (sc)
        {
            scope_debug(sc);
            debug sc.id = 0;
static if (TX86)
{
            sc.next = scope_freelist;
            scope_freelist = sc;
            sc = sc.using_scope;
}
else
{
            Scope *scn;

            scn = sc.using_scope;
            MEM_PH_FREE(sc);
            sc = scn;
}
        }
    }
    else
    {
static if (TX86)
{
        sc.next = scope_freelist;
        scope_freelist = sc;
}
else
{
        MEM_PH_FREE(sc);
}
    }
}

//////////////////////////////
// Push scope onto stack of scopes.

void scope_push(void *root,scope_fp fpsearch,int sctype)
{
    Scope *sc;

    sc = scope_calloc();
    //printf("scope_push(sctype = x%x), sc = %p\n",sctype,sc);
    sc.next = scope_end;
    sc.root = root;
    sc.fpsearch = fpsearch;
    sc.sctype = sctype;
    sc.using_scope = null;
    sc.using_list = null;
    scope_end = sc;
    scope_debug(sc);
}

//////////////////////////////
// Pop scope off of stack of scopes.

void *scope_pop()
//Symbol *scope_pop()
{
    Scope *sc;
    Symbol *root;
    list_t ul;

    //printf("scope_pop(): scope_end = %p\n",scope_end);
    assert(scope_end);
    scope_debug(scope_end);

    if (CPP)
    {
    // Remove using-directives from enclosing scopes
    for (ul = scope_end.using_list; ul; ul = list_next(ul))
    {   Scope *scx;

        scx = cast(Scope *) list_ptr(ul);
        ul = list_next(ul);
        sc = cast(Scope *) list_ptr(ul);
        scope_debug(sc);

        //printf("removing scope %p from scope %p,",sc,scx);
        while (1)
        {   scope_debug(scx);
            if (scx.using_scope == sc)
            {
                scx.using_scope = sc.using_scope;
                sc.using_scope = null;
                scope_free(sc);
                break;
            }
            scx = scx.using_scope;
            assert(scx);
        }
    }
    list_free(&scope_end.using_list,FPNULL);
    }

    sc = scope_end;
    root = cast(Symbol *)sc.root;
    scope_end = sc.next;
debug
{
    if (sc.next)
        scope_debug(sc.next);
}
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

    //printf("scope_using(scn=%s, fpsearch=%p, sctype=x%x, sce=%p)\n",sn.Sident,fpsearch,sctype,sce);
    assert(sce);

    // Find nearest scope scx that encloses both sn and end_scope.
    for (scx = sce; 1; scx = scxn)
    {   Nspacesym *sn1;

        scxn = scx.next;
        if (!scxn || scx.sctype & SCTglobal)   // if scx is outmost scope
            break;                      // then that's the one we use
        if (!(scx.sctype & SCTnspace))
            continue;
        for (sn1 = sn; sn1; sn1 = cast(Nspacesym *)sn1.Sscope)
        {
            if (sn1 == cast(Nspacesym *)scx.root)
                goto L1;
        }
    }
L1: ;

static if (0)
{
    // If sn is already in the using_scope list of scx, then don't add it again.
    for (sc = scx; sc; sc = sc.using_scope)
        if (sn == cast(Nspacesym *)sc.root)
            return;
}

    sc = scope_calloc();
    sc.root = sn;
    sc.fpsearch = fpsearch;
    sc.sctype = sctype;

    // Add using-directive scope sc to enclosing scope scx
    //printf("Installed scope %p into scope %p\n",sc,scx);
    sc.using_scope = scx.using_scope;
    scx.using_scope = sc;

    // Remember scx and sc so we can remove them when sce goes
    // out of scope.
    list_append(&sce.using_list,scx);
    list_append(&sce.using_list,sc);
}

/************************************
 * Get list of enclosing scopes for symbol, in reverse order.
 */

list_t scope_getList(Symbol *s)
{
    list_t scopelist = null;

    while ((s = s.Sscope) != null)
    {
        //printf("\tadding scope '%s'\n", &s.Sident[0]);
        list_prepend(&scopelist, s);
    }
    return scopelist;
}

/**************************************
 * Push all scopes enclosing symbol s.
 * Returns:
 *      number of scopes pushed
 */

int scope_pushEnclosing(Symbol *s)
{
    int nscopes = 0;
    list_t scopelist = scope_getList(s);

    // Push all the scopes
    for (list_t l = scopelist; l; l = list_next(l))
    {   Symbol *ss = cast(Symbol *) list_ptr(l);

        if (ss.Sclass == SCstruct)
        {
            if (ss.Sstruct.Stempsym)
            {
                template_createsymtab(
                    ss.Sstruct.Stempsym.Stemplate.TMptpl,
                    ss.Sstruct.Sarglist);
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
        if (scope_end.sctype == SCTtempsym)
            template_deletesymtab();
        else
            scope_pop();
    }
}

////////////////////////////////
//

void scope_term()
{
static if (TERMCODE)
{
    Scope *sc;

    while (scope_freelist)
    {   sc = scope_freelist.next;
static if (TX86)
{
        free(scope_freelist);
}
else
{
        MEM_PH_FREE(scope_freelist);
}
        scope_freelist = sc;
    }
}
}

}
