/**
 * Implementation of the
 * $(LINK2 http://www.digitalmars.com/download/freecompiler.html, Digital Mars C/C++ Compiler).
 *
 * Copyright:   Copyright (c) 1995-1998 by Symantec, All Rights Reserved
 *              Copyright (c) 2000-2017 by Digital Mars, All Rights Reserved
 * Authors:     $(LINK2 http://www.digitalmars.com, Walter Bright)
 * License:     Distributed under the Boost Software License, Version 1.0.
 *              http://www.boost.org/LICENSE_1_0.txt
 * Source:      https://github.com/DigitalMars/Compiler/blob/master/dm/src/dmc/nspace.c
 */

// C++ namespace support


#if !SPP

#include        <stdio.h>
#include        <ctype.h>
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
#include        "cgcv.h"
#include        "scope.h"

static char __file__[] = __FILE__;      /* for tassert.h                */
#include        "tassert.h"

STATIC void using_directive();
STATIC void using_add(Nspacesym *sn);
STATIC void using_addx(Nspacesym *sn,Scope *sce);

/**********************************
 * Parse namespace-definition.
 */

void namespace_definition()
{   char *id;
    Nspacesym *sn;
    char *vident;
    unsigned sct;
    int unique;

    //printf("namespace_definition()\n");
    stoken();
    if (tok.TKval == TKident)
    {   vident = alloca_strdup(tok.TKid);
        stoken();
        unique = 0;
    }
    else if (tok.TKval == TKlcur)
    {   vident = file_unique();         // get unique identifier for module
        unique = 1;
    }
    else
    {   synerr(EM_ident_exp);           // identifier expected
        goto Lerrsemi;
    }

    sct = SCTglobal | SCTlocal | SCTclass | SCTnspace;

    if (tok.TKval == TKlcur)
    {   Nspacesym *namespacesave;

        sn = (Nspacesym *) scope_searchinner(vident,sct);
        if (sn)
        {
            if (sn->Sclass != SCnamespace)
            {   synerr(EM_multiple_def,vident); // symbol already defined
                goto Lerrrcur;
            }
        }
        else
        {   sn = (Nspacesym *) scope_define(vident,sct,SCnamespace);
            // Give type so routines that depend on non-NULL type won't fail
            sn->Stype = tsvoid;
            sn->Stype->Tcount++;
        }

        if (sn->Sclass == SCnamespace)
        {
            if (sn->Susing)
                using_addx(sn,scope_end);
            scope_push_nspace(sn);

            stoken();
            ext_def(2);                 // parse declarations

            scope_pop();
        }
        else
        {
            stoken();
            ext_def(2);                 // parse declarations
        }

        if (tok.TKval != TKrcur)
        {
            synerr(EM_rcur);            // right curly expected
        }
        else
            stoken();

        if (unique)
        {   // CPP98 7.3.1.1
            // Do the equivalent of a "using namespace unique;"
            using_add(sn);
        }
    }
    else if (tok.TKval == TKeq)
    {   // CPP98 7.3.2 namespace-alias-definition
        //      namespace identifier = qualified-namespace-specifier ;
        Aliassym *sa;

        //printf("namespace-alias-definition '%s'\n", vident);
        stoken();
        sn = (Nspacesym *)nspace_getqual(1);
        if (!sn)
            goto Lerrsemi;

        // CPP98 7.3.2-3 Look for redefinition in current declarative region
        symbol *s;
        s = scope_searchinner(vident, sct);
        if (!s || s != sn)
        {
            sa = (Aliassym *) scope_define(vident,sct,SCalias);
            sa->Smemalias = sn;
            sa->Stype = sn->Stype;
            sa->Stype->Tcount++;
        }
        chktok(TKsemi,EM_nspace_alias_semi);    // ';' expected
    }
    else
    {   synerr(EM_lcur_exp);            // left curly bracket expected
        goto Lerrsemi;
    }
    return;

Lerrsemi:
    panic(TKsemi);
    stoken();
    return;

Lerrrcur:
    panic(TKrcur);
    stoken();
}

void scope_push_nspace(Nspacesym *sn)
{
    //printf("scope_push_nspace(sn = '%s')\n", sn->Sident);
    assert(sn->Sclass == SCnamespace);
    scope_push(sn,(scope_fp)nspace_searchmember,SCTnspace);
}

/**************************************
 * Parse using-declaration.
 */

void using_declaration()
{   symbol *su;
    symbol *s;
    unsigned sct;

    stoken();
    if (tok.TKval == TKnamespace)
    {   stoken();
        using_directive();
        return;
    }

    //printf("using_declaration()\n");
    su = nspace_getqual(4 | 2); // get qualified symbol
    if (!su)
        goto Lerrsemi;
    assert(su->Sclass != SCalias);

    sct = SCTglobal | SCTlocal | SCTclass | SCTnspace;
    s = scope_searchinner(su->Sident,sct);

    if (tyfunc(su->Stype->Tty))
    {   Funcsym *sf;
        Funcsym *s2;
        Funcsym *sf2;
        Funcsym **psf;

        //printf("\tusing function su = '%s' %p\n", symbol_ident(su));
        //printf("\texisting s = %p\n", s);

        sf = symbol_funcalias(su);
        psf = &sf->Sfunc->Foversym;

        // Add all overloaded versions of su to sf's Falias list. This is
        // so that when new overloaded functions are added to su, they will
        // *not* appear in s's overloading.
        for (s2 = su->Sfunc->Foversym; s2; s2 = s2->Sfunc->Foversym)
        {
            sf2 = symbol_funcalias(s2);
            *psf = sf2;
            psf = &sf2->Sfunc->Foversym;
        }

        if (s && tyfunc(s->Stype->Tty))
        {
            // Append sf to Foversym list
            for (psf = &s; *psf; psf = &(*psf)->Sfunc->Foversym)
                ;
            *psf = sf;
        }
        else
            scope_add(sf,sct);
    }
    else
    {   Aliassym *sa;

        if (level == 0 && s == su)      // if at global or namespace scope and
                                        // already there
            goto L1;

        if (s && isscover(su) && !isscover(s))
        {   symbol *s2;

            s2 = scope_searchinner(su->Sident, sct | SCTnoalias);
            assert(!s2->Scover);        // BUG: should issue error message
            assert(!s->Scover);         // BUG: should issue error message
            s2->Scover = su;
        }
        else
        {
            sa = (Aliassym *)scope_define(su->Sident,sct,SCalias);
            sa->Smemalias = su;
            if (!sa->Scover)
                sa->Scover = su->Scover;
            else if (su->Scover)
                assert(0);              // BUG: should issue error message
            sa->Stype = su->Stype;
            sa->Stype->Tcount++;
        }
    }

L1:
    if (tok.TKval != TKsemi)
    {
        cpperr(EM_using_semi,"declaration");            // ';' expected
        goto Lerrsemi;
    }
    if (level != -1 &&          // if not at class declaration level
        su->Sscope && su->Sscope->Sclass == SCstruct
       )
        cpperr(EM_using_member,cpp_prettyident(su));
    return;

Lerrsemi:
    panic(TKsemi);
}

/**************************************
 * C++98 7.3.4
 * Parse using-directive.
 */

STATIC void using_directive()
{   Nspacesym *sn;

    sn = (Nspacesym *)nspace_getqual(1);
    if (!sn)
        goto Lerrsemi;
    if (tok.TKval != TKsemi)
    {
        cpperr(EM_using_semi,"directive");              // ';' expected
        goto Lerrsemi;
    }
    using_add(sn);
    return;

Lerrsemi:
    panic(TKsemi);
}

/*************************************************
 * Add namespace to current scope.
 */

STATIC void using_add(Nspacesym *sn)
{
    Scope *sc;
    Nspacesym *sne;             // enclosing namespace scope

    //printf("using_add(sn = '%s')\n",sn->Sident);
    symbol_debug(sn);
    sne = NULL;
    // May appear in namespace or block scope
    sc = scope_find(SCTnspace | SCTlocal);
    if (sc && sc->sctype & SCTnspace)
    {   sne = (Nspacesym *)sc->root;
        symbol_debug(sne);
        if (sne == sn)
            return;
    }

    using_addx(sn,scope_end);

    if (sne)                            // if enclosing namespace
    {
        // Add to list of using-directives of enclosing namespace
        if (!list_inlist(sne->Susing,sn))
            list_prepend(&sne->Susing,sn);

        // If sne is in a using-directive of an enclosing scope, then add
        // sn to those as well.
        while ((sc = sc->next) != NULL)
        {   Scope *sc1;

            for (sc1 = sc->using_scope; sc1; sc1 = sc1->using_scope)
            {
                if (sne == (Nspacesym *)sc1->root && sne->Sclass == SCnamespace)
                {
                    using_addx(sn,sc);
                    break;
                }
            }
        }
    }
}

/**************************************
 * Recursively add namespace sn to scope.
 */

STATIC void using_addx(Nspacesym *sn,Scope *sce)
{   list_t ul;
    list_t ulsave;

    //printf("using_addx(sn = '%s',sce = %p, %x)\n",sn->Sident,sce,sce->sctype);

    scope_using(sn,(scope_fp)nspace_searchmember,SCTnspace,sce);

    // Add using-directives of sn as well
    ulsave = sn->Susing;
    sn->Susing = NULL;                  // prevent cycles
    for (ul = ulsave; ul; ul = list_next(ul))
    {   Nspacesym *snu;

        snu = (Nspacesym *)list_ptr(ul);
        symbol_debug(snu);
        assert(snu->Sclass == SCnamespace);
        using_add(snu);
    }
//printf("'%s'->Susing = '%s'\n",sn->Sident,sn->Susing ? list_symbol(sn->Susing)->Sident : "");
    assert(!sn->Susing);
    sn->Susing = ulsave;                // restore
}

/*************************************************
 * Parse qualified name.
 * Input:
 *      flag
 *              1       looking for namespace name only
 *              2       looking for any name
 *              4       don't drill down typedefs
 * Output:
 *      token past identifier
 * Returns:
 *      namespace symbol
 *      NULL    error, message already given
 */

symbol *nspace_getqual(int flag)
{   symbol *s;
    unsigned sct;

    sct = SCTglobal | SCTlocal | SCTclass | SCTnspace;

    if (tok.TKval == TKcolcol)
    {   sct = SCTglobal;                // search global table only
        stoken();
    }

    if (tok.TKval != TKident)
    {   synerr(EM_ident_exp);           // identifier expected
        s = NULL;
        goto Lret;
    }

    s = (sct == SCTglobal) ? scope_search(tok.TKid,sct) : symbol_search(tok.TKid);
L7:
    if (!s)
    {
        synerr(EM_undefined,tok.TKid);          // undefined identifier
        goto Lret;
    }
    if (flag & 2)
    {
        while (1)
        {
            switch (s->Sclass)
            {
                case SCtypedef:
#if TX86
                    // If we are past the header, and referencing typedefs,
                    // then output the typedef into the debug info.
                    if (config.fulltypes == CV4 &&
                        pstate.STflags & PFLextdef &&
                        tybasic(s->Stype->Tty) != TYident &&
                        !s->Sxtrnnum
                       )
                       cv_outsym(s);
#endif
                    if (!(flag & 4) && tybasic(s->Stype->Tty) == TYstruct)
                    {
                        s = s->Stype->Ttag;
                        goto L7;
                    }
                    goto Lret;

                case SCnamespace:
                    s = nspace_qualify((Nspacesym *)s);
                    if (!s)
                        goto Lret;
                    continue;

                case SCtemplate:                // class template
                    if (token_peek() == TKsemi)
                        goto Lret;
                    if (pstate.STintemplate)
                    {   assert(0);              // dunno what to do here
                        template_expand_type(s);
                        goto Lret;
                    }
                    s = template_expand(s,0);   // instantiate template
                    if (!s)
                        goto L7;
                    symbol_debug(s);
                    /* FALL-THROUGH */
                case SCstruct:
                    if (stoken() == TKcolcol)
                    {
                        if (stoken() != TKident)
                        {   synerr(EM_ident_exp);       // identifier expected
                            s = NULL;
                            goto Lret;
                        }
                        s = cpp_findmember_nest((Classsym **)&s,tok.TKid,FALSE);
                        if (!s)
                            goto L7;
                    }
                    else
                    {
                        token_unget();
                        goto Lret;
                    }
                    break;

                case SCenum:
                    if (stoken() == TKcolcol)
                    {
                        if (stoken() != TKident)
                        {   synerr(EM_ident_exp);       // identifier expected
                            s = NULL;
                            goto Lret;
                        }
                        s = symbol_searchlist(s->Senumlist,tok.TKid);
                        if (!s)
                            goto L7;
                    }
                    else
                    {
                        token_unget();
                        goto Lret;
                    }
                    break;

                default:
                    if (s->Scover)
                    {   enum_TK tk;

                        tk = stoken();
                        token_unget();
                        if (tk == TKcolcol)
                        {   s = s->Scover;
                            goto L7;
                        }
                    }
                    goto Lret;
            }
        }
    }
    else if (flag & 1)
    {
        while (1)
        {
            if (s->Sclass != SCnamespace)
            {   cpperr(EM_nspace_name,prettyident(s));  // must be namespace name
                s = NULL;
                goto Lret;
            }
            if (stoken() != TKcolcol)
                return s;
            token_unget();
            s = nspace_qualify((Nspacesym *)s);
            if (!s)
                goto Lret;
        }
    }
Lret:
    stoken();
    return s;
}

/*************************************************
 * Parse qualified name for using-declaration of a class member.
 * Input:
 *      stag    class definition we are in
 *      tok     token after 'using'
 * Output:
 *      tok     past closing ';'
 * Returns:
 *      constructed memalias or memfuncalias symbol
 *      NULL    error, message already given
 */

symbol *using_member_declaration(Classsym *stag0)
{   symbol *s;
    symbol *sa;
    symlist_t classpath;
    int global;
    Classsym *stag;

    // Remember class path, so when we reference the base symbol, we can
    // correctly modify the 'this' pointer. This is especially important if
    // there are multiple instances of the base class in the inheritance tree,
    // we have to modify 'this' to point to the correct one.
    classpath = NULL;

    global = 0;
    if (tok.TKval == TKcolcol)
    {   global = 1;                     // search global table only
        stoken();
    }

    if (tok.TKval != TKident)
    {   synerr(EM_ident_exp);           // identifier expected
        goto Lerr;
    }

    stag = stag0;
    s = (global) ? scope_search(tok.TKid,SCTglobal) : symbol_search(tok.TKid);
L7:
    if (!s)
    {
        synerr(EM_undefined,tok.TKid);          // undefined identifier
        goto Lerr;
    }
    while (1)
    {
        if (stoken() == TKlt)
            cpperr(EM_using_declaration_template_id, symbol_ident(s));
        else
            token_unget();

        switch (s->Sclass)
        {
            case SCtypedef:
#if TX86
                // If we are past the header, and referencing typedefs,
                // then output the typedef into the debug info.
                if (config.fulltypes == CV4 &&
                    pstate.STflags & PFLextdef &&
                    tybasic(s->Stype->Tty) != TYident &&
                    !s->Sxtrnnum
                   )
                   cv_outsym(s);
#endif
                if (tybasic(s->Stype->Tty) == TYstruct)
                {
                    s = s->Stype->Ttag;
                    goto L7;
                }
                goto Lret;

            case SCnamespace:
                s = nspace_qualify((Nspacesym *)s);
                if (!s)
                    goto Lerr;
                continue;

            case SCtemplate:            // class template
                if (pstate.STintemplate)
                {   assert(0);          // dunno what to do here
                    template_expand_type(s);
                    goto Lret;
                }
                s = template_expand(s,0);       // instantiate template
                if (!s)
                    goto L7;
                symbol_debug(s);
                /* FALL-THROUGH */
            case SCstruct:
                if (!c1isbaseofc2(NULL,s,stag))
                {
                    cpperr(EM_public_base,s->Sident,stag->Sident);      // not a base class
                    goto Lerr;
                }
                cpp_memberaccess(s,funcsym_p,stag);
                if (stoken() == TKcolcol)
                {
                    stag = (Classsym *)s;
                    list_append(&classpath,s);
                    if (stoken() != TKident)
                    {   synerr(EM_ident_exp);   // identifier expected
                        goto Lerr;
                    }
                    s = cpp_findmember_nest((Classsym **)&s,tok.TKid,FALSE);
                    if (!s)
                        goto L7;
                }
                else
                {
                    token_unget();
                    goto Lret;
                }
                break;

            case SCenum:
                if (stoken() == TKcolcol)
                {
                    if (stoken() != TKident)
                    {   synerr(EM_ident_exp);   // identifier expected
                        goto Lerr;
                    }
                    s = symbol_searchlist(s->Senumlist,tok.TKid);
                    if (!s)
                        goto L7;
                }
                else
                {
                    token_unget();
                    goto Lret;
                }
                break;

            default:
                if (s->Scover)
                {   enum_TK tk;

                    tk = stoken();
                    token_unget();
                    if (tk == TKcolcol)
                    {   s = s->Scover;
                        goto L7;
                    }
                }
                goto Lret;
        }
    }
Lret:
    if (tyfunc(s->Stype->Tty))          // if using-declaration of member function
    {   Funcsym *sf;
        Funcsym **ps;

        // Create an SCfuncalias symbol for each overloaded member function
        sa = NULL;
        ps = &sa;
        for (; s; s = s->Sfunc->Foversym)
        {
            if (s->Sfunc->Fflags3 & Foverridden)
                continue;
            cpp_memberaccess(s,funcsym_p,stag0);
            sf = symbol_funcalias(s);
            sf->Spath1 = list_link(classpath);
            *ps = sf;
            ps = &sf->Sfunc->Foversym;
        }
        list_free(&classpath,FPNULL);
    }
    else
    {
        cpp_memberaccess(s,funcsym_p,stag0);
        sa = symbol_name(s->Sident,SCmemalias,s->Stype);
        sa->Smemalias = s;
        sa->Spath = classpath;
    }
    stoken();
    return sa;

Lerr:
    panic(TKsemi);
    stoken();
    return NULL;
}

/***********************************************
 * Look up name in namespace as well as using-directives.
 * No error message if not found.
 * Returns:
 *      symbol if found
 *      NULL if not found
 */

symbol *nspace_search(const char *id,Nspacesym *sn)
{   symbol *s;
    list_t lu,lustart;

    symbol_debug(sn);
    assert(sn->Sclass == SCnamespace);
    //printf("nspace_search('%s','%s')\n",id,sn->Sident);
    s = findsy(id,sn->Snameroot);
    if (s)
    {   symbol_debug(s);
        //printf("found\n");
        if (s->Sclass == SCalias)
        {   s = ((Aliassym *)s)->Smemalias;
            symbol_debug(s);
        }
    }

    // Look at using-directives
    lustart = sn->Susing;
    sn->Susing = NULL;          // temporarilly disconnect to avoid cycles
    for (lu = lustart; lu; lu = list_next(lu))
    {   Nspacesym *su = (Nspacesym *) list_ptr(lu);
        symbol *s2;

        symbol_debug(su);
        assert(su->Sclass == SCnamespace);
        s2 = nspace_search(id,su);
        if (s2)
        {
            if (s && s != s2 && !nspace_isSame(s, s2))
            {
                // Function ambiguities are handled later by overload resolution
                if (!tyfunc(s->Stype->Tty) || !tyfunc(s2->Stype->Tty))
                    err_ambiguous(s,s2);
            }
            else
                s = s2;
        }
    }
    sn->Susing = lustart;       // reconnect

    return s;
}

/***********************************************
 * Look up name in namespace.
 * No error message if not found.
 * Returns:
 *      symbol if found
 *      NULL if not found
 */

symbol *nspace_searchmember(const char *id,Nspacesym *sn)
{   symbol *s;

    symbol_debug(sn);
    //printf("nspace_searchmember(id = '%s', sn = '%s')\n",id,sn->Sident);
    assert(sn->Sclass == SCnamespace);
    s = findsy(id,sn->Snameroot);
    if (s)
    {   symbol_debug(s);
        //printf("found\n");
        if (s->Sclass == SCalias)
        {   s = ((Aliassym *)s)->Smemalias;
            symbol_debug(s);
        }
    }
    return s;
}

/************************************************
 * Given that the parser is on a namespace symbol,
 * scan forward dealing with the :: until a non-namespace
 * symbol is found, and return that symbol.
 * Input:
 *      lexer is on the TKident of sn
 * Output:
 *      lexer is on the TKident of returned symbol
 * Returns:
 *      symbol of qualified name
 *      NULL    error
 */

symbol *nspace_qualify(Nspacesym *sn)
{   symbol *s;
    char *id;
    int oper;
    type *t;

    s = NULL;
    if (stoken() != TKcolcol)
    {
        //token_unget();
        //token_setident(sn->Sident);
        //s = sn;
        cpperr(EM_colcol_exp);          // :: expected
    }
    else
    {
        switch (stoken())
        {
            case TKident:
                id = tok.TKid;
                break;

            case TKoperator:
                id = cpp_operator(&oper, &t);
                token_unget();
                token_setident(id);
                break;

            default:
                synerr(EM_ident_exp);           // identifier expected
                return NULL;
        }
        //s = nspace_searchmember(id, sn);
        s = nspace_search(id, sn);
        if (!s)
        {
            cpperr(EM_nspace_undef_id, id, sn->Sident); // id not in namespace
        }
    }
    return s;
}

/************************************
 * Add symbol to namespace symbol table.
 */

void nspace_add(void *snv,symbol *s)
{   Nspacesym *sn = (Nspacesym *)snv;

    symbol_debug(sn);
    symbol_debug(s);
    assert(sn->Sclass == SCnamespace);

    //printf("adding '%s' to namespace '%s'\n",s->Sident,sn->Sident);
    symbol_addtotree(&sn->Snameroot,s);
    s->Sscope = sn;
}

/**************************************
 * Add function alias s2 to existing symbol s.
 */

void nspace_addfuncalias(Funcsym *s,Funcsym *s2)
{   Funcsym *s2p;
    Funcsym **ps;

    symbol_debug(s);
    symbol_debug(s2);
    for (; s2; s2 = s2->Sfunc->Foversym)
    {
        s2p = s2;
        if (s2p->Sclass == SCfuncalias)
            s2p = s2p->Sfunc->Falias;

        // Append s2p to s, if it is not already there
        for (ps = &s; *ps; ps = &(*ps)->Sfunc->Foversym)
        {   Funcsym *sp;

            sp = *ps;
            if (sp->Sclass == SCfuncalias)
                sp = sp->Sfunc->Falias;
            if (sp == s2p)                      // if symbol is already there
                goto L1;                        // don't add in redundant copy
        }
        *ps = symbol_funcalias(s2p);
    L1:
        ;
    }
}

/****************************************
 * Check for CPP98 7.3.1.2-2, where outside definitions
 * must be in an enclosing namespace.
 */

void nspace_checkEnclosing(symbol *s)
{
    //printf("nspace_checkEnclosing('%s')\n", symbol_ident(s));
    Nspacesym *snc = scope_inNamespace();

    if (snc)
    {
        symbol *start = s;
        symbol *n = NULL;

        for (; s; s = s->Sscope)
        {
            if (s == snc)
                return;
            if (!n && s->Sclass == SCnamespace)
                n = s;
        }
        if (n)
        {
            cpperr(EM_namespace_enclose,
                symbol_ident(snc),
                symbol_ident(start),
                symbol_ident(n));
        }
    }
}

/***********************************************
 * CPP98 7.3.4-4
 * Determine if two symbols in different namespaces are really
 * the same symbol, which they would be if they had the same
 * name mangling.
 * Returns:
 *      !=0     if same symbol
 */

int nspace_isSame(symbol *s1, symbol *s2)
{
    symbol_debug(s1);
    symbol_debug(s2);

    //printf("nspace_isSame('%s', '%s')\n", symbol_ident(s1), symbol_ident(s2));
    //symbol_print(s1);
    //symbol_print(s2);
    if (s1->Sclass == SCfuncalias)
        s1 = s1->Sfunc->Falias;
    if (s2->Sclass == SCfuncalias)
        s2 = s2->Sfunc->Falias;

    if (s1->Sclass == SCalias)
        s1 = s1->Smemalias;
    if (s2->Sclass == SCalias)
        s2 = s2->Smemalias;

    if (s1 == s2)
        return 1;

    if (s1->Sscope != s2->Sscope)
    {
        unsigned mangle = type_mangle(s1->Stype);

        //printf("mangle1 = %d, mangle2 = %d\n", mangle, type_mangle(s2->Stype));
        if (mangle == type_mangle(s2->Stype) &&
            (mangle == mTYman_c ||
             mangle == mTYman_pas ||
             mangle == mTYman_for) &&
            typematch(s1->Stype, s2->Stype, 0))
        {
            //printf("same\n");
            return 1;
        }
    }
    //printf("not same\n");
    return 0;   // not same
}

#endif
