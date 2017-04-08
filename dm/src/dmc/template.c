/**
 * Implementation of the
 * $(LINK2 http://www.digitalmars.com/download/freecompiler.html, Digital Mars C/C++ Compiler).
 *
 * Copyright:   Copyright (c) 1991-1998 by Symantec, All Rights Reserved
 *              Copyright (c) 2000-2017 by Digital Mars, All Rights Reserved
 * Authors:     $(LINK2 http://www.digitalmars.com, Walter Bright)
 * License:     Distributed under the Boost Software License, Version 1.0.
 *              http://www.boost.org/LICENSE_1_0.txt
 * Source:      https://github.com/DigitalMars/Compiler/blob/master/dm/src/dmc/template.c
 */

#if !SPP

#include        <stdio.h>
#include        <time.h>
#include        <string.h>
#include        <stdlib.h>
#include        "cc.h"

#include        "token.h"
#include        "parser.h"
#include        "global.h"
#include        "el.h"
#include        "type.h"
#include        "oper.h"
#include        "cpp.h"

static char __file__[] = __FILE__;      /* for tassert.h                */
#include        "tassert.h"

STATIC int template_isclasstemplate(int);
STATIC void template_class_decl(Classsym *stag, param_t *temp_arglist, param_t *temp_arglist2, int member_template, unsigned access_specifier);
STATIC void template_function_decl(Classsym *stag, param_t *temp_arglist, param_t *temp_arglist2, int member_template, unsigned access_specifier,
        token_t *tbody, token_t *to, symbol *s);
STATIC symbol * template_define(Classsym *stag, symbol *sprimary, enum_TK tk, int structflags, char *vident, param_t *temp_arglist, param_t *ptal);
STATIC void template_chkarg(type *, param_t *, char *);
STATIC void template_createargtab(param_t *);
STATIC void template_deleteargtab(void);
STATIC Classsym * template_parsebody( Classsym *stag, symbol *stempl, param_t *arglist );
STATIC int template_elemdependent(elem *e, param_t *ptpl);
STATIC int template_typedependent(type *t, param_t *ptpl);
STATIC Classsym *template_expand2(symbol *stempl, param_t *template_argument_list);
STATIC elem *template_resolve_idents(elem *e, param_t *ptal);
symbol *template_class_match(symbol *sprimary, param_t *ptal, param_t **pptali);
void template_explicit_instantiation();

void template_instantiate_classmember(Symbol *st, TMF *tmf);
void template_instantiate_classmember(Symbol *st, Symbol *si);

#if TX86
symlist_t template_ftlist;              // list of template function symbols
symbol *template_class_list;
symbol **template_class_list_p;
#endif

INITIALIZED_STATIC_DEF list_t template_xlist;

/**************************************
 * Parse command line switches for templates.
 *      -XI...          Instantiate template ...
 *      -XIabc<int>     Instantiate class template abc with type int.
 *      -XIfunc(int)    Instantiate function template func with type int.
 * Input:
 *      p               Remainder of string following "-X" on command line
 * Returns:
 *      0       success
 *      !=0     command line error
 */

int template_getcmd(char *p)
{
    switch (*p)
    {
        case 'I':
            list_append(&template_xlist,p + 1);
            break;
    }
    return 0;

err:
    return 1;
}


/***************************************
 * Parse template-parameter-list.
 * Input:
 *      token is past "template" of the grammar:
 *              "template" "<" template-parameter-list ">" declaration
 * Output:
 *      token is past terminating ">"
 * Returns:
 *      parameter list
 */

STATIC param_t *template_parameter_list()
{
    param_t **pp;
    param_t *temp_arglist;
    param_t *p;
    char vident[2*IDMAX + 1];
    char mustinit = FALSE;
    int sequence = 0;

    if (tok.TKval != TKlt)
    {
        if (tok.TKval == TKlg)          // if <> token
        {
            stoken();
            return NULL;
        }
        else
            cpperr(EM_lt_following,"template");         // '<' expected
    }
    if (stoken() == TKgt)
    {   stoken();
        return NULL;
    }


    // Initialize symbol table
    template_createargtab(NULL);

    /* Pick up the template-parameter-list, of which there must always
        be at least one template-parameter
     */
    pp = &temp_arglist;
    temp_arglist = NULL;
    while (1)
    {
        /* CPP98 14.1
           A template-parameter can be either:
                type-parameter
                parameter-declaration

           A type-parameter can be:
                "class" [identifier]
                "class" [identifier] = type-id
                "typename" [identifier]
                "typename" [identifier] = type-id
                "template" < template-parameter-list> "class" [identifier]
                "template" < template-parameter-list> "class" [identifier] = id-expression

           This also should work:
                "typename" name::id
           as in:
                template<class T, typename T::result_type> struct B {};
         */

        Symbol *s;
        ++sequence;
        p = param_calloc();

        if (tok.TKval == TKclass || tok.TKval == TKtypename)
        {   // type-parameter
            enum_TK tksave = tok.TKval;

            //printf("type_parameter\n");
            stoken();
            if (tok.TKval == TKident)   // identifier is optional
            {
                p->Pident = (char *) MEM_PH_STRDUP(tok.TKid);   // copy in identifier
                //dbg_printf("template declaration class parameter %s\n",tok.TKid);
                stoken();
            }
            else if (tok.TKval == TKcolcol)
            {
                token_unget();
                tok.TKval = tksave;
                goto L1;                // it's an argument-declaration
            }

            // Detect default type-argument
            if (tok.TKval == TKeq)
            {   type *t;
                type *typ_spec;

                if (p->Pident)
                {   s = template_createsym(p->Pident, NULL, &(symbol *)scope_find(SCTtemparg)->root);
                    s->Ssequence = sequence;
                }
                pstate.STintemplate += 2;
                stoken();
                type_specifier(&typ_spec);
                t = declar_abstract(typ_spec);
                fixdeclar(t);
                p->Pdeftype = t;
                type_free(typ_spec);
                pstate.STintemplate -= 2;
                mustinit = TRUE;
            }

            // Detect case of template<class ABC *p> ...
            else if (tok.TKval != TKcomma && tok.TKval != TKgt && p->Pident)
            {
                /* Back up scanner over the two tokens and try again    */
                token_unget();
                token_setident(p->Pident);
                token_unget();
                tok.TKval = tksave;
                MEM_PH_FREE(p->Pident);
                p->Pident = NULL;
                goto L1;                /* oops, it's an argument-declaration */
            }

#if 0
            else if (mustinit)
                cpperr(EM_musthaveinit);        // must have initializer
#endif
            else if (p->Pident)
            {   s = template_createsym(p->Pident, NULL, &(symbol *)scope_find(SCTtemparg)->root);
                s->Ssequence = sequence;
            }
        }
        else if (tok.TKval == TKtemplate)
        {
            /* C++ 14.3.3 Parse template-template-parameter:
             *  "template" < template-parameter-list> "class" [identifier]
             *  "template" < template-parameter-list> "class" [identifier] = id-expression
             */
            param_t *temp_param_list;
            symbol *s;

            stoken();
            temp_param_list = template_parameter_list();
            p->Pptpl = temp_param_list;
            if (tok.TKval != TKclass)
            {
                cpperr(EM_class_expected);              // 'class' expected
            }
            if (stoken() == TKident)    // identifier is optional
            {
                p->Pident = (char *) MEM_PH_STRDUP(tok.TKid);   // copy in identifier
                stoken();
            }

            s = NULL;
            if (tok.TKval == TKeq)
            {
                stoken();
                s = id_expression();
                if (s)
                {
                    if (s->Sclass != SCtemplate)
                    {
                        cpperr(EM_class_template_expected, s->Sident); // only class templates allowed
                        s = NULL;
                    }
                }
            }
            p->Psym = s;
        }
        else
        {   // parameter-declaration
            type *pt;

        L1:
            //printf("parameter_declaration\n");
            pstate.STintemplate++;
            type_specifier(&pt);
            p->Ptype = declar(pt,vident,0);
            fixdeclar(p->Ptype);
            type_free(pt);
            pstate.STintemplate--;
            if (vident[0])      // if there was an identifier
                p->Pident = (char *) MEM_PH_STRDUP(vident);
#if 0
            // No longer an error
            if (!vident[0])
                synerr(EM_no_ident_decl);       // no identifier for declarator
#endif
            // A non-type template-parameter shall not be declared
            // to have floating point, class, or void type
            tym_t tym = tybasic(p->Ptype->Tty);
            if (tyfloating(tym) || tym == TYstruct || tym == TYvoid)
                cpperr(EM_param_no_float,vident);       // no floating types

            // Convert "array of T" to "pointer to T"
            if (tym == TYarray)
            {   type *t;

                t = newpointer(p->Ptype->Tnext);
                t->Tcount++;
                type_free(p->Ptype);
                p->Ptype = t;
            }

            // Convert "function returning T" to "pointer to function returning T"
            if (tyfunc(tym))
            {   type *t;

                t = newpointer(p->Ptype);
                type_settype(&p->Ptype, t);
            }

            /* Look for default initializer     */
            if (tok.TKval == TKeq)
            {   char inarglistsave = pstate.STinarglist;

                mustinit = TRUE;        /* subsequent parameters must have init */
                stoken();               /* skip over =                  */
                pstate.STinarglist = 1; /* regard > as not an operator  */
                p->Pelem = assign_exp();
                pstate.STinarglist = inarglistsave;
            }
#if 0
            else if (mustinit)
                cpperr(EM_musthaveinit);        // must have initializer
#endif
            if (p->Pident)
            {   s = template_createsym(p->Pident, p->Ptype, &(symbol *)scope_find(SCTtemparg)->root);
                s->Ssequence = sequence;
            }
        }

        // Check p->Pident to see if it is unique
        if (p->Pident && temp_arglist->search(p->Pident))
            synerr(EM_multiple_def,p->Pident);  // already defined

        *pp = p;
        pp = &(p->Pnext);               // append to list

        if (tok.TKval == TKcomma)
        {   stoken();
            continue;                   /* more arguments to come       */
        }
        if (tok.TKval == TKgt)
            stoken();
        else
        {   cpperr(EM_gt);              // '>' expected
            panic(TKgt);
            stoken();
        }
        break;
    }

    // Remove temporary symbol table
    template_deleteargtab();

    return temp_arglist;
}

/**************************************
 * Parse template declaration.
 * Input:
 *      token is on "template" of the grammar:
 *              "template" "<" template-parameter-list ">" declaration
 *      access_specifier = one of SFLpublic, SFLprivate, SFLprotected, SFLnone
 * Output:
 *      token is past terminating ";"
 */

void template_declaration(Classsym *stag, unsigned access_specifier)
{
    param_t *temp_arglist;
    param_t *temp_arglist2 = NULL;
    int member_template = 0;

    //printf("template_declaration(stag = '%s')\n", stag ? stag->Sident : "NULL");
//    if (funcsym_p)
//      cpperr(EM_bad_templ_decl);      // no templates in local classes

    stoken();
    if (tok.TKval != TKlt && tok.TKval != TKlg)
    {
        template_explicit_instantiation();
        return;
    }

    temp_arglist = template_parameter_list();

    if (tok.TKval == TKtemplate)
    {   stoken();
        temp_arglist2 = template_parameter_list();
        member_template |= 1;
    }

    if (tok.TKval == TKtypedef)
    {   cpperr(EM_bad_templ_decl);      // typedef not allowed
        stoken();
    }
    else if (tok.TKval == TKfriend)
    {   member_template |= 2;
        stoken();
    }

    if ((tok.TKval == TKclass || tok.TKval == TKstruct || tok.TKval == TKunion) &&
        template_isclasstemplate(member_template))
    {   // Class template
        template_class_decl(stag, temp_arglist, temp_arglist2, member_template, access_specifier);
    }
    else
    {   /* Function template    */
        template_function_decl(stag, temp_arglist, temp_arglist2, member_template, access_specifier,
                NULL, NULL, NULL);
    }
}

/**********************************************
 * Look ahead to see if this is a class template or function template.
 * Input:
 *      tok is on class, struct or union
 * Returns:
 *      !=0     class template
 *      0       function template
 */

STATIC int template_isclasstemplate(int member_template)
{
    Token_lookahead tla;
    int arglist;
    int again = 0;

    //printf("template_isclasstemplate()\n");
    tla.init();

Lagain:
    tla.lookahead();
    if (tok.TKval != TKident)
        goto Lisclass;
    tla.lookahead();

    // Skip over class template partial specialization
    arglist = 0;
    if (tok.TKval == TKlg)
    {   tla.lookahead();
        arglist = 1;
    }
    else if (tok.TKval == TKlt)
    {
        int brack = 1;
        int paren = 0;

        arglist = 1;
        while (1)
        {   tla.lookahead();
            switch (tok.TKval)
            {   case TKlt:      brack++;        continue;
                case TKgt:      if (paren)      continue;
                            if (--brack)        continue;
                            break;
#if 0
                case TKshr: if ((brack -= 2) > 0) continue;
#endif
                case TKlpar: paren++;   continue;
                case TKrpar: paren--;   continue;
                case TKeof:  goto Lerror;
                default:     continue;
            }
            break;
        }
        tla.lookahead();
    }

    switch (tok.TKval)
    {
        default:
            goto Lisfunc;

        case TKlcur:
            if (!arglist && again && !(member_template & 1))
                goto Lisfunc;
        case TKsemi:
        case TKcolon:
            goto Lisclass;

        case TKcolcol:
            if (arglist)
                again = 1;
            goto Lagain;
    }

Lerror:
    //printf("\terror\n");
Lisclass:
    //printf("\tisclass\n");
    tla.term();
    return 1;

Lisfunc:
    //printf("\tisfunc\n");
    tla.term();
    return 0;
}

/******************************************
 * Parse a class template declaration.
 */

STATIC void template_class_decl(
        Classsym *stag,         // != NULL means member template class
        param_t *temp_arglist,  // template parameter list
        param_t *temp_arglist2, // member template parameter list
        int member_template,    // |1 if member template
                                // |2 if friend template
        unsigned access_specifier)
{
    symbol *s;
    symbol *sprimary = NULL;
    symbol *root;
    char vident[2*IDMAX + 1];
    param_t *ptal;

    //printf("template_class_decl(stag = '%s', member_template = x%x, access_specifier = x%x)\n", stag ? stag->Sident : "", member_template, access_specifier);
    //printf("temp_arglist = %p, temp_arglist2 = %p\n", temp_arglist, temp_arglist2);
    int structflags = 0;
    enum_TK tk = (enum_TK)tok.TKval;

    switch (tk)
    {   case TKstruct:  structflags |= 0;                break;
        case TKunion:   structflags |= STRunion;         break;
        case TKclass:   structflags |= STRclass;         break;
        default:        assert(0);
    }

    stoken();

    switch (tok.TKval)
    {
#if TX86
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
        L5:
                stoken();
                break;
    }

    if (tok.TKval != TKident)
    {   synerr(EM_ident_exp);           // identifier expected
        goto Lpanic;
    }

    if (temp_arglist && temp_arglist->search(tok.TKid) ||
        temp_arglist2 && temp_arglist2->search(tok.TKid))
        cpperr(EM_template_parameter_redeclaration, tok.TKid);

    // If it's a friend, it can only be a class declaration
    if (member_template & 2)
    {
Pstate pstatesave = pstate;
pstate.STmaxsequence = ~0u;
        s = scope_search(tok.TKid,SCTglobal | SCTnspace);       // try to find symbol
pstate.STmaxsequence = pstatesave.STmaxsequence;
        strcpy(vident,tok.TKid);                // make our local copy
        stoken();

        if (s && s->Sclass == SCtemplate && (tok.TKval == TKlt || tok.TKval == TKlg))
        {   // Handle things like:
            // template<class T> friend struct A<T>::B;

            token_t *tk;
            TMNF *tmnf;

            token_unget();
            token_setident(vident);
            tk = token_funcbody(FALSE);         // gather the A<T>::B

            /* template<class T> friend class A<T*>; is a partial
             * specialization and is illegal
             */
            token_t *to = tk->TKnext;
            if (to->TKval == TKlg)
                ;
            else
            {   int brack = 0;
                int paren = 0;

                assert(to->TKval == TKlt);
                for (; to; to = to->TKnext)
                {
                    switch (to->TKval)
                    {
                        case TKlt:      brack++;        continue;
                        case TKgt:      if (paren)      continue;
                                        if (--brack)    continue;
                                        break;
                        case TKlpar:    paren++;        continue;
                        case TKrpar:    paren--;        continue;
                        case TKeof:     break;
                        default:        continue;
                    }
                    break;
                }
            }
            if (to && to->TKnext && to->TKnext->TKval == TKsemi)
                cpperr(EM_friend_partial, s->Sident);

            tmnf = (TMNF *) MEM_PH_MALLOC(sizeof(TMNF));
            tmnf->tdecl = tk;
            tmnf->temp_arglist = temp_arglist;
            assert(!temp_arglist2);             // don't know what that means
            tmnf->stempl = s;
            tmnf->stag = stag;

            assert(stag);
            list_append(&s->Stemplate->TMnestedfriends, tmnf);

            return;
        }

        if (tok.TKval != TKsemi)
        {
            // CPP98 14.5.3-3 "... a friend class template may not be defined
            // in a class or class template."
            cpperr(EM_semi_template, vident);                   // ';' expected
            goto Lpanic;
        }
        if (s)
        {
            if (!paramlstmatch(temp_arglist,s->Stemplate->TMptpl))
                cpperr(EM_templ_param_lists,s->Sident); // parameter lists must match
        }
        else
        {
            // Define forward reference to class template
            s = template_define(NULL, NULL, tk, structflags, vident, temp_arglist, NULL);
            temp_arglist = NULL;
        }
//printf("Adding '%s' to '%s'->TMfriends\n", stag->Sident, s->Sident);
        list_append(&s->Stemplate->TMfriends, stag);

        /* If s is already instantiated, go back and redo friends for
         * the instance members.
         */
        for (symlist_t sl = s->Stemplate->TMinstances; sl; sl = list_next(sl))
        {   Classsym *s1 = list_Classsym(sl);

            //printf("class '%s' is now a friend of class '%s'\n", s1->Sident, stag->Sident);
            list_append(&stag->Sstruct->Sfriendclass, s1);
            list_append(&s1->Sstruct->Sclassfriends, stag);
            n2_classfriends(s1);
        }

        goto Lerr;
    }

    if (stag && stag->Sclass == SCstruct)
    {
        s = n2_searchmember((Classsym *)stag, tok.TKid);
    }
    else
    {
        // Look for existing definition
        s = scope_searchinner(tok.TKid, SCTglobal | SCTnspace | SCTcover);
        if (s && s->Sclass == SCnamespace)
        {
            s = nspace_qualify((Nspacesym *)s);
        }
    }
    strcpy(vident,tok.TKid);            // make our local copy
    //dbg_printf("declare/define class template %s\n",vident);
    stoken();
#if 0
    if (s && tybasic(s->Stype->Tty) != TYstruct)
        s = s->Scover;                  // try the hidden definition
#endif

    // If it's a partial specialization
    ptal = NULL;
    if (tok.TKval == TKlt || tok.TKval == TKlg)
    {   param_t *p;

        //printf("it's a class template partial specialization\n");
        if (!s)
        {
            // Primary template must be declared before partial specializations
            cpperr(EM_primary_template_first, vident);
            goto Lpanic;
        }

        // CPP98 14.5.4.10 "The template parameter list of a specialization
        // shall not contain default template argument values."
        for (p = temp_arglist; p; p = p->Pnext)
        {
            if (p->Pdeftype || p->Pelem)
                cpperr(EM_no_default_template_arguments);
        }

        // Collect the template-argument-list
        template_createargtab(temp_arglist);
        pstate.STintemplate++;
        ptal = template_gargs(s);
        // template_deleteargtab() will delete the symbols. So
        // we have to replace the symbols.
        param_t *pp = s->Stemplate->TMptpl;
        for (p = ptal; p; p = p->Pnext)
        {
            elem *e = p->Pelem;
            if (e)
            {
                if (e->Eoper == OPvar)
                {   char *id = e->EV.sp.Vsym->Sident;
                    param_t *px = temp_arglist->search(id);
                    if (px)
                    {
                        symbol *sv = symbol_calloc(id);
                        sv->Ssequence = e->EV.sp.Vsym->Ssequence;
                        sv->Sclass = e->EV.sp.Vsym->Sclass;
                        sv->Stype = e->EV.sp.Vsym->Stype;
                        sv->Stype->Tcount++;
                        e->EV.sp.Vsym = sv;
                    }
                }
                else
                    template_elemdependent(e, temp_arglist);
#if 1 // Gives errors anyway
                /* CPP98 14.5.4-9 "The type of a template parameter
                 * corresponding to a specialized non-type argument shall not be
                 * dependent on a parameter of the specialization."
                 */
//type_print(pp->Ptype);
//type_print(e->ET);
                template_typedependent(pp->Ptype, temp_arglist);
                //if (!typematch(e->ET, pp->Ptype, 0))
                    //cpperr(EM_dependent_specialization, s->Sident);
#endif
            }
            pp = pp->Pnext;
        }
        pstate.STintemplate--;
        template_deleteargtab();

#if 0
        printf("primary template parameter list ***********\n");
        s->Stemplate->TMptpl->print_list();

        printf("specialized template parameter list ***********\n");
        temp_arglist->print_list();

        printf("specialized template argument list ptal ***********\n");
        ptal->print_list();
#endif
        // CPP98 14.5.4-9 "The argument list of the specialization shall
        // not be identical to the implicit argument list of the primary
        // template."
        pp = s->Stemplate->TMptpl;
        param_t *ta = temp_arglist;
        param_t *pa = ptal;
        int match = 1;
        for (pa = ptal; pa && pp && ta; pa = pa->Pnext, pp = pp->Pnext, ta = ta->Pnext)
        {
            if (pp->Ptype)
            {
                if (!(pa->Pelem && typematch(pp->Ptype, pa->Pelem->ET, 0) &&
                    pa->Pelem->Eoper == OPvar))
                    match = 0;
            }
            else if (!(pa->Ptype && pa->Ptype->Tty == TYident && ta))
                match = 0;
        }
        if (pp || ta || pa)
            match = 0;
        if (match)
        {
            cpperr(EM_identical_args, s->Sident);
        }

        // Look for match with existing partial specialization
        sprimary = s;
        for (s = s->Stemplate->TMpartial; s; s = s->Stemplate->TMpartial)
        {
#if 0
            printf("specialized template argument list TMptal ***********\n");
            s->Stemplate->TMptal->print_list();
            printf("specialized template argument list ptal ***********\n");
            ptal->print_list();
#endif
            if (template_arglst_match(s->Stemplate->TMptal, ptal))
                break;
        }

        stoken();
        if (tok.TKval == TKcolcol && !temp_arglist)
        {
            // This is the case:
            //  template<> template<class T2> struct A<short>::B { };
            // We need to save B away in template A, since A<short> may not be
            // instantiated yet.

            stoken();
            if (tok.TKval != TKident)
            {   synerr(EM_ident_exp);           // identifier expected
                goto Lpanic;
            }
            char vident[2*IDMAX + 1];
            strcpy(vident,tok.TKid);            // make our local copy

            Token_lookahead tla;
            tla.init();
            tla.lookahead();

            if ((tok.TKval == TKlt || tok.TKval == TKlg) && !temp_arglist2)
            {   // template<> template<> class A<int>::B<double>;

                tla.term();
                TMNE *tmne = (TMNE *) MEM_PH_CALLOC(sizeof(TMNE));

                tmne->tk = tk;
                tmne->name = (char *) MEM_PH_STRDUP(vident);    // copy in identifier
                tmne->ptal = ptal;
                tmne->tdecl = token_funcbody(TRUE);
#if 0
                if (tok.TKval == TKsemi)
                {
                    // BUG: forward reference, how to handle it?
                }
                else if (stoken() != TKsemi)
                    cpperr(EM_semi_template, vident);   // ';' expected
#endif
                // Save in template A all the details of B
                // BUG: should check for duplicates and issue error
                list_append(&sprimary->Stemplate->TMnestedexplicit, tmne);

                return;
            }

            tla.term();

            TME *tme = (TME *) MEM_PH_CALLOC(sizeof(TME));
            symbol *se;

            //printf("explicit\n");

            tme->ptal = ptal;

            // Create the template symbol for B
            se = symbol_calloc(vident);
            se->Sclass = SCtemplate;

            se->Stemplate = (template_t *) MEM_PH_CALLOC(sizeof(template_t));
            se->Stemplate->TMptpl = temp_arglist2;
            se->Stemplate->TMtk = tk;
            se->Stemplate->TMflags = sprimary->Stemplate->TMflags;
            se->Stemplate->TMptal = NULL;

            if (1)
            {
                if (template_class_list_p)
                    *template_class_list_p = se;
                else
                    template_class_list = se;
                template_class_list_p = &se->Stemplate->TMnext;
            }

            type *t;
            t = type_alloc(TYvoid);             // need a placeholder type
            //t->Tflags |= TFforward;
            t->Tcount++;
            se->Stype = t;

            tme->stempl = se;

            stoken();
            if (tok.TKval != TKlcur && tok.TKval != TKsemi)
            {
                synerr(EM_lcur_exp);            // "{" expected /* } */
                goto Lpanic;
            }
            se->Stemplate->TMbody = token_funcbody(FALSE);
            if (tok.TKval == TKsemi)
            {
                // BUG: forward reference, how to handle it?
            }
            else if (stoken() != TKsemi)
                cpperr(EM_semi_template,prettyident(se));       // ';' expected

            // Save in template A all the details of B
            // BUG: should check for duplicates and issue error
            list_append(&sprimary->Stemplate->TMexplicit, tme);

            return;
        }
    }

    if (!s)
        nwc_musthaveinit(temp_arglist);

    if (tok.TKval == TKsemi)            // if no body for template
    {   // Template declaration
        if (s)
        {
            if (!paramlstmatch(temp_arglist,s->Stemplate->TMptpl))
                cpperr(EM_templ_param_lists,s->Sident); // parameter lists must match

            /* BUG: what about T1 not being same spelling as 'S'?
             *  template<class T1, class T2 = T1> class A;
             *  template<class S = int, class T> class A;
             */
            nwc_defaultparams(temp_arglist, s->Stemplate->TMptpl);

            param_free(&s->Stemplate->TMptpl);
            s->Stemplate->TMptpl = temp_arglist;
        }
        else
        {
            // Define forward reference to class template
            s = template_define(stag, sprimary, tk, structflags, vident, temp_arglist, ptal);
            s->Sflags |= access_specifier;
        }
    }
    else
    {   // Template definition
        if (s)
        {
            if (s->Sclass == SCtemplate &&
                s->Stype->Tflags & TFforward &&
                paramlstmatch(temp_arglist,s->Stemplate->TMptpl))
            {
                symlist_t sl;

                s->Stemplate->TMbody = token_funcbody(FALSE); /* read token list */
                //token_funcbody_print(s->Stemplate->TMbody);
                s->Stype->Tflags &= ~TFforward;

                // Transfer default parameters
                nwc_defaultparams(temp_arglist,s->Stemplate->TMptpl);

                param_free(&s->Stemplate->TMptpl);
                s->Stemplate->TMptpl = temp_arglist;

#if 0
                // There could be instantiations already that need to
                // be parsed. Go through the instantiation list and
                // re-parse the body of the class.
                // This is to fix:
                //
                //      template <class T> A;
                //      A<int> *a;
                //      template <class T> A { int i; };
                //
                // which would mean that A<int> would never be expanded
                //

                for (sl = s->Stemplate->TMinstances; sl; sl = list_next(sl))
                {   Classsym *stag;

                    stag = list_Classsym(sl);
                    template_instantiate_forward(stag);
                }
#endif
            }
            else
            {
                synerr(EM_multiple_def,s->Sident); // already defined
                token_free(token_funcbody(FALSE)); // skip over class def
                goto Lpanic;
            }
        }
        else
        {
            s = template_define(stag, sprimary, tk, structflags, vident, temp_arglist, ptal);
            s->Sflags |= access_specifier;
            s->Stemplate->TMbody = token_funcbody(FALSE); /* read token list */
            //token_funcbody_print(s->Stemplate->TMbody);
            s->Stype->Tflags &= ~TFforward;
        }
        s->Stemplate->TMsequence = pstate.STsequence++;
        if (stoken() != TKsemi)
            cpperr(EM_semi_template,prettyident(s));    // ';' expected
    }
    return;

Lpanic:
    panic(TKsemi);
Lerr:
    param_free(&temp_arglist);          // free our parameter list
    param_free(&temp_arglist2);         // free our parameter list
}

/*******************************
 * Parse a function template.
 *      template<class T> T& func(int i) { ... }
 * Also parses member function templates, which look like:
 *      template<class T> T& vector<T>::operator[](int i) const { ... }
 * And member variable templates:
 *      template<class T> T& vector<T>::xyzzy;
 */

STATIC void template_function_decl(
        Classsym *stag,         // != NULL means member template member function
        param_t *temp_arglist,  // template parameter list
        param_t *temp_arglist2, // member template parameter list
        int member_template,    // |1 if template template
                                // |2 if template friend
        unsigned access_specifier,
        token_t *tbody,
        token_t *to,            // location to continue on inside tbody
        symbol *s)
{
    token_t *tgargs;
    int paren;
    int brack;
    int foundident;
    int anyident;
    char vident[2*IDMAX + 1];
    enum_TK lasttok;
    token_t *textra;
    token_t *tstart;

    //printf("template_function_decl(stag = '%s')\n", stag ? stag->Sident : "NULL");

    Pstate pstatesave = pstate;
    if (member_template & 2)
        pstate.STmaxsequence = ~0u;

    if (!tbody)
    {
        // Read in declaration, include trailing ; to handle things like:
        //      template <class T> int TMPL_ARR_A<T>::str[] = {1 ,2, 3, 4};
        tbody = token_funcbody(TRUE);
        to = tbody;
    }

    /* Scan the tokens for the function-template, to determine:
        o       if it is a member function template
        o       if it is a function template
        o       if it is neither (an error)
     */
    template_createargtab(temp_arglist); // create argument symbol table
    pstate.STintemplate++;

    paren = 0;
    brack = 0;
    foundident = 0;
    anyident = 0;
    lasttok = TKnone;
    vident[0] = 0;
    if (s)
        strcpy(vident, s->Sident);
    tgargs = NULL;
    tstart = tbody;
    for (; to; to = to->TKnext)
    {   int memberclass;

        //printf("s = %p ", s); to->print();
        switch (to->TKval)
        {
            case TKident:
                //printf("TKident: '%s'\n", to->TKid);
                anyident = 1;
                if (!brack && !foundident)
                {
                    if (s && s->Sclass == SCstruct)
                    {
                        s = n2_searchmember((Classsym *)s, to->TKid);
                    }
                    else if (s && s->Sclass == SCnamespace)
                    {
                        s = nspace_searchmember(to->TKid, (Nspacesym *)s);
                    }
                    else
                        s = scope_search(to->TKid,SCTglobal | SCTnspace);
                Lsym:
                    if (s && (s->Sclass == SCtypedef ||
                        s->Sclass == SCstruct ||
                        s->Sclass == SCenum) &&
                        to->TKnext->TKval != TKcolcol &&
                        !(to->TKnext->TKval == TKlpar && to->TKnext->TKnext->TKval != TKstar))
                    {
                        s = NULL;
                        lasttok = TKint;        // Anything but an identifier
                        continue;               // Do not treat the last token
                                                // as an identifier because it
                                                // is a typedef
                    }
                    strcpy(vident,to->TKid);
                }
                break;

            case TKtypename:
            case TKstatic:
            case TKconst:
            case TKinline:
                if (to == tstart)
                    tstart = tstart->TKnext;
                continue;                       // skip over it

            case TKoperator:
                foundident = 1;
                anyident = 1;
                goto found;

            case TKcolcol:
                // Look for leading '::' or leading 'static ::'
                if ((to == tstart /*||
                     (tbody->TKval == TKstatic && to == tbody->TKnext) ||
                     (tbody->TKval == TKconst  && to == tbody->TKnext)*/
                    ) &&
                    to->TKnext->TKval == TKident)
                {
                    to = to->TKnext;
                    s = scope_search(to->TKid, SCTglobal);
                    goto Lsym;
                }

            {   int istmf = 1;          // assume this is a member function

                if (!vident[0])
                    goto syntax_error;

            Lcolcol2:
                memberclass = 0;
                switch (to->TKnext->TKval)
                {
                    case TKoperator:
                    case TKcom:
                        break;

                    case TKtemplate:
                        // Account for ::template
                        to = to->TKnext;
                        goto Lcolcol2;

                    case TKident:
                        switch (to->TKnext->TKnext->TKval)
                        {
                            case TKlcur:
                                memberclass = 1;
                            case TKlpar:
                            case TKeq:
                            case TKsemi:
                            case TKlbra:
                                if (s && s->Sclass == SCnamespace)
                                    continue;
                                goto L1;

                            case TKrpar:        // Assume that it is a pointer to function static
                                                // data member
                                if (!foundident)
                                    goto L1;
                                break;

                            case TKcolcol:
                            {   token_t *tox;

                                if (!s || s->Sclass != SCtemplate)
                                    continue;
                                tox = to;
                                while (1)
                                {
                                    tox = tox->TKnext->TKnext;
                                    if (tox->TKnext->TKval == TKident)
                                    {
                                        if (tox->TKnext->TKnext->TKval == TKcolcol)
                                            continue;
                                        strcpy(vident, tox->TKnext->TKid);
                                        goto L1;
                                    }
                                    else if (tox->TKnext->TKval == TKoperator)
                                    {
                                        strcpy(vident, "operator");
                                        goto L1;
                                    }
                                    break;
                                }
                                continue;
                            }
                            case TKlt:
                            case TKlg:
                                // ... ::ident<
                                if (s && s->Sclass == SCtemplate)
                                {
                                    //printf("TMTMF\n");
                                    istmf = 0;  // it's a TMTMF
                                    goto L1;
                                }
                                continue;
                        }
                        // Intentional fall-through to the default case
                    default:
                        // This is not the function
                        // declaration, so assume that it is a type
                        // could be a template type is the return type e.g.
                        //template <class T> struct X {
                        //      typedef T * pointer;
                        //      typedef const T & reference;
                        //      reference f(void);
                        //};
                        //template <class T, class T2> class A {
                        //      typedef X<T>::pointer link_type;
                        //      X<T>::reference foo(link_type);
                        //};
                        //
                        //template <class T, class T2>
                        //inline X<T>::reference A<T, T2>::foo(link_type)
                        //{
                        //}

                        s = NULL;
                        foundident = 0;
                        continue;       // Keep looking for this
                }
            L1:
                if (!s)
                {
                    cpperr(EM_not_class_templ,vident);          // '%s' is not a class template
                    goto syntax_error;
                }
                if (lasttok == TKident && type_struct(s->Stype))
                {
                    break;
                }
                if ((lasttok == TKlg || lasttok == TKgt) && s->Sclass == SCtemplate)
                {
                    //printf("\ttemplate member function\n");

                    // Parse the template-argument-list so we can figure out
                    // which template this belongs to.
                    param_t *ptal;
                    symbol *sprimary = s;

                    token_unget();
                    token_setlist(tgargs);
                    stoken();                   // jump start it
                    ptal = template_gargs(s);
                    token_poplist();
                    stoken();

                    if (temp_arglist)
                    {
                        // Look for match with existing partial specialization
                        for (s = s->Stemplate->TMpartial; s; s = s->Stemplate->TMpartial)
                        {
                            if (template_arglst_match(s->Stemplate->TMptal, ptal))
                                break;
                        }
#if 0
                        if (s)
                            printf("match with partial\n");
                        else
                            printf("match with primary\n");
#endif
                        if (!s)
                        {
                            s = sprimary;
                            // C++98 14.5.1-3 make sure args are in order
#if 0
                            printf("\ntemp_arglist:\n");
                            temp_arglist->print_list();
                            printf("\nptal:\n");
                            ptal->print_list();
#endif
                            param_t *p1 = temp_arglist;
                            param_t *p2 = ptal;
                            for (; p1; p1 = p1->Pnext)
                            {   char *id = p1->Pident;

                                if (!p2)
                                    break;
                                if (p1->Ptype)
                                {
                                    if (!p2->Pelem ||
                                        p2->Pelem->Eoper != OPvar ||
                                        strcmp(id, p2->Pelem->EV.sp.Vsym->Sident))
                                        goto Lerr1;
                                }
                                else if (p1->Pptpl)
                                {
                                    if (!p2->Psym ||
                                        strcmp(id, p2->Psym->Sident))
                                        goto Lerr1;
                                }
                                else if (!p2->Ptype ||
                                    p2->Ptype->Tty != TYident ||
                                    strcmp(id, p2->Ptype->Tident))
                                    goto Lerr1;
                                p2 = p2->Pnext;
                            }
                            if (p2)
                            {
                              Lerr1:
                                cpperr(EM_template_arg_order);
                            }
                        }
                        param_free(&ptal);
                    }
                    else
                    {   // It's an explicit specialization.
                        // Look for match with existing partial specialization
                        param_t *ptal2;
                        s = template_class_match(s, ptal, &ptal2);
                        if (ptal != ptal2)
                        {   param_free(&ptal);
                            ptal = ptal2;
                        }
#if 0
                        if (s == sprimary)
                        {
                            printf("explicit match with primary '%s'\n", s->Sident);
                            //param_free(&ptal);
                        }
                        else
                            printf("explicit match '%s'\n", s->Sident);
#endif
                    }

                    /* This is:
                     *  vident<...>::ident
                     * followed by ( = ; [ ) lcur
                     * Store this away and deal with it at end of compilation
                     */
                    TMF *tmf = (TMF *) MEM_PH_MALLOC(sizeof(TMF));
                    tmf->stag = stag;
                    tmf->tbody = tbody;
                    tmf->to = to;
                    tmf->member_class = memberclass;
                    tmf->temp_arglist = temp_arglist;
                    tmf->member_template = member_template;
                    tmf->temp_arglist2 = temp_arglist2;
                    tmf->name = NULL;
                    tmf->castoverload = 0;
                    tmf->ptal = ptal;
                    tmf->access_specifier = access_specifier;
                    if (member_template & 2)
                        tmf->sclassfriend = stag;
                    else
                        tmf->sclassfriend = NULL;
                    if (member_template & 1)
                    {
                        to = to->TKnext;
                        if (to->TKval == TKident)
                            tmf->name = to->TKid;
                        else if (to->TKval == TKoperator)
                        {
                            tmf->name = cpp_operator2(to->TKnext, &tmf->castoverload);
                        }
                        else
                            assert(0);
                        tmf->name = MEM_PH_STRDUP(tmf->name);
                    }
#if 0
                    printf("Adding TMF %p to template '%s'", tmf, s->Sident);
                    if (tmf->name)
                        printf(" name='%s'", tmf->name);
                    if (member_template & 2)
                        printf(" as friend of stag '%s'", stag->Sident);
                    printf("\n");
#endif
                    if (ptal)
                    {
                        //printf("TMF is an explicit specialization\n");
                        // Put all explicit specializations first, so
                        // they get instantiated first by template_instantiate().
                        // symdecl() will
                        // cause subsequent partial specializations to
                        // be skipped.
                        list_prepend(&s->Stemplate->TMmemberfuncs, tmf);
                    }
                    else
                        list_append(&s->Stemplate->TMmemberfuncs, tmf);

                    stoken();
                    pstate.STintemplate--;
                    template_deleteargtab();            // delete argument symbol table
                    if (memberclass)
                    {
                        template_instantiate_classmember(s, tmf);
                    }
                    goto Lret;
                }
                else
                {
                    cpperr(EM_not_class_templ,vident);          // '%s' is not a class template
                    goto syntax_error;
                }
            }
            case TKlpar:
                paren++;
                /* Is start of parameter-list if previous token is an
                   identifier or a )    */
                if (lasttok == TKident || lasttok == TKrpar)
                {   foundident = 1;

                    if (to->TKnext && to->TKnext->TKval != TKstar)
                    {
                    // Skip over parameter list
                    while (to->TKnext)
                    {   to = to->TKnext;
                        switch (to->TKval)
                        {
                            case TKlpar: paren++;       continue;
                            case TKrpar: if (--paren)   continue;
                                         break;
                            default:    continue;
                        }
                        break;
                    }
                    }
                }
                break;
            case TKrpar:
                paren--;
                break;
            case TKlbra:
                brack++;
                break;
            case TKrbra:
                brack--;
                break;
            case TKlg:
                anyident = 1;
                tgargs = to;
                break;
            case TKlt:
                /* Skip over template-arg-list  */
                anyident = 1;
                tgargs = to;
                if (lasttok == TKident)         /* if ident<            */
                {   int brack = 1;
                    int paren = 0;

                    while (to->TKnext)
                    {   to = to->TKnext;
                        switch (to->TKval)
                        {   case TKlt:  brack++;        continue;
                            case TKgt:  if (paren)      continue;
                                        if (--brack)    continue;
                                        break;
#if 0
                            case TKshr: if ((brack -= 2) > 0) continue;
#endif
                            case TKlpar: paren++;       continue;
                            case TKrpar: paren--;       continue;
                            default:    continue;
                        }
                        break;
                    }
                }
                break;
            case TKsemi:
            case TKlcur:
                goto found;

            default:
                break;
        }
        lasttok = (enum_TK)to->TKval;
    }

syntax_error:
    /* must be some sort of error       */
    cpperr(EM_bad_templ_decl);          // malformed template declaration
err_ret:
    param_free(&temp_arglist);
    token_free(tbody);
    pstate.STintemplate--;
    template_deleteargtab();            // delete argument symbol table
    goto Lret;

found:
    if (paren || brack)
        goto syntax_error;

    // Make sure that { } are balanced and that there is no trailing semi colon.  If there
    // is a trailing semi then remove it and place it back onto the stream here
    // Trailing ;'s happen when there's a { } initalizer for a static data member.
    textra = NULL;
    if (to->TKval == TKlcur)
    {   int level = 1;

        while ((to = to->TKnext) != NULL)
        {   switch (to->TKval)
            {
                case TKlcur:
                    level++;
                    break;
                case TKrcur:
                    if (--level <= 0)
                        goto found_rcur;
                    break;
            }
        }
        goto syntax_error;

found_rcur:
        if (to->TKnext) {
            textra = to->TKnext;
            to->TKnext = NULL;
        }

    }
  {
    symbol *s;
    type *t;
    type *typ_spec;
    func_t *f;
    int funcdef;
    param_t *p;
    int constructor = 0;
    unsigned long class_m = 0;

    //printf("\tfound '%s', temp_arglist = %p\n", vident, temp_arglist);
    /* Parse the declaration of the function in order to get its type   */
    token_setlist(tbody);
    while (1)
    {
        stoken();
        switch (tok.TKval)
        {
            case TKstatic:
                if (class_m & mskl(SCstatic))
                    synerr(EM_storage_class, "");
                class_m |= mskl(SCstatic);
                continue;

            case TKextern:
            case TKexplicit:
            case TKinline:
                continue;

            default:
                break;
        }
        break;
    }

    if (!temp_arglist)
        pstate.STexplicitSpecialization = 1;

    if (stag && !(member_template & 2))
    {   enum_TK tk;

        if (tok.TKval == TKident && template_classname(tok.TKid, stag))
        {
            tk = stoken();
            token_unget();
            token_setident(stag->Sstruct->Stempsym ? stag->Sstruct->Stempsym->Sident : stag->Sident);
            if (tk == TKlpar)
            {
                //printf("constructor\n");
                constructor = 1;
                typ_spec = tsint;
                tsint->Tcount++;
                t = declar_fix(typ_spec, vident);
                type_free(t->Tnext);
                t->Tnext = newpointer(stag->Stype);
                t->Tnext->Tcount++;
                strcpy(vident, cpp_name_ct);
            }
        }
    }

    if (!constructor)
    {
        if (temp_arglist)
            pstate.STignoretal = 1;
        unsigned long class_m2 = 0;
        declaration_specifier(&typ_spec, NULL, &class_m2);
        if (class_m2 & mskl(SCvirtual))
        {
            synerr(EM_storage_class, "virtual");
            class_m2 = 0;
        }
        if (class_m2 & mskl(SCstatic))
        {
            if (class_m & mskl(SCstatic))
                synerr(EM_storage_class, "static");
            class_m |= mskl(SCstatic);
        }
        pstate.STintemplate++;
        t = declar_fix(typ_spec,vident);
        pstate.STintemplate--;
        pstate.STignoretal = 0;
    }

    pstate.STexplicitSpecialization = 0;
    pstate.STintemplate--;
    type_free(typ_spec);
    type_debug(t);

    if (!tyfunc(t->Tty))
    {
        synerr(EM_function);            // function expected
        type_free(t);
        token_poplist();
        template_deleteargtab();        /* delete argument symbol table */
        goto err_ret;
    }

    template_deleteargtab();            // delete argument symbol table

    symbol *sftemp = NULL;

    // if explicit specialization of operator overload
    if (!temp_arglist && gdeclar.oper == OPMAX && gdeclar.class_sym)
    {
        Match m;
        param_t *ptal;

        //printf("explicit specialization of operator overload, stag = %p\n", stag);
        // Find the SCfunctempl that matches
        for (list_t cl = gdeclar.class_sym->Sstruct->Scastoverload; cl; cl = list_next(cl))
        {
            s = list_symbol(cl);
            symbol_debug(s);
            if (s->Sclass != SCfunctempl)
                continue;

            param_t *ptpl = s->Sfunc->Farglist;

            ptal = ptpl->createTal(NULL);

            // There are no function arguments to a cast operator,
            // so pick up the template parameters from the cast
            // operator return type.
            m = template_matchtype(s->Stype->Tnext, t->Tnext, NULL, ptpl, ptal, 1);
            if (m.m < TMATCHexact)
            {
                //param_free(&ptal);
                continue;
            }
            // BUG: What if more than one match?
            sftemp = s;
            break;
        }

        if (!sftemp)
        {
            synerr(EM_undefined, vident);
            type_free(t);
            goto Lret;
        }
        s = symbol_name(vident, SCftexpspec, t);
        //printf("1: creating explicit specialization %p %s\n", s, vident);
        type_free(t);
        symbol_func(s);
        s->Sfunc->Fexplicitspec = sftemp->Sfunc->Fexplicitspec;
        sftemp->Sfunc->Fexplicitspec = s;
        s->Sfunc->Fptal = ptal;
        s->Sfunc->Ftempl = sftemp;
        s->Sscope = sftemp->Sscope;
        goto Lcheckexplicit;
    }
    // if explicit specialization of function
    else if (!temp_arglist /*&& gdeclar.ptal*/)
    {
        //printf("explicit specialization, stag = %p, vident = '%s'\n", stag, vident);
        if (stag && !(member_template & 2))
        {
            assert(0);                  // BUG: implement
        }
        else
        {
            // Find the SCfunctempl that matches
            if (gdeclar.class_sym)
            {
                sftemp = n2_searchmember(gdeclar.class_sym, vident);
            }
            else if (gdeclar.namespace_sym)
                sftemp = nspace_searchmember(vident, gdeclar.namespace_sym);
            else
            {   int sct = SCTglobal;
                if (scope_inNamespace())
                    sct = SCTnspace;
                sftemp = scope_searchinner(vident, sct);
            }

            if (!sftemp)
            {
                synerr(EM_undefined, vident);
                param_free(&gdeclar.ptal);
                type_free(t);
                goto Lret;
            }
            //sftemp = cpp_lookformatch(sftemp,NULL,NULL,NULL,NULL,NULL,gdeclar.ptal,1|4|8,NULL,NULL);
            sftemp = template_matchfunctempl(sftemp, gdeclar.ptal, t, NULL, 1|4|8);
            assert(sftemp);

            /* Now we need to finish the template argument list with
             * parameters deduced from the function parameter types.
             */
            assert(sftemp->Sclass == SCfunctempl);
            param_t *ptal;
            template_deduce_ptal(NULL, sftemp, gdeclar.ptal, NULL, 8, t->Tparamtypes, &ptal);
            param_free(&gdeclar.ptal);

            s = symbol_name(vident, SCftexpspec, t);
            type_free(t);
            symbol_func(s);
            //printf("2: creating explicit specialization %p %s\n", s, vident);
            s->Sfunc->Fexplicitspec = sftemp->Sfunc->Fexplicitspec;
            sftemp->Sfunc->Fexplicitspec = s;
            s->Sfunc->Fptal = ptal;
            s->Sfunc->Ftempl = sftemp;
            s->Sscope = sftemp->Sscope;

        Lcheckexplicit:
#if 0
            printf("\ns->Sfunc->Farglist:\n");
            if (s->Sfunc->Farglist) s->Sfunc->Farglist->print_list();
            printf("\ns->Sfunc->Fptal:\n");
            if (s->Sfunc->Fptal) s->Sfunc->Fptal->print_list();
            printf("\ns->Stype->Tparamtypes:\n");
            if (s->Stype->Tparamtypes) s->Stype->Tparamtypes->print_list();
#endif

            /* C++98 14.7.3-6 says an explicit specialization cannot
             * appear after the primary was instantiated.
             */
            for (Symbol *sx = sftemp; sx; sx = sx->Sfunc->Foversym)
            {
                if (!(sx->Sfunc->Fflags & Finstance) ||
                    sx->Sfunc->Ftempl != sftemp)
                    continue;
                printf("\texisting %p %s %s\n", sx, sx->Sident, cpp_mangle(sx));

                //printf("\nsx->Sfunc->Fptal:\n");
                //if (sx->Sfunc->Fptal) sx->Sfunc->Fptal->print_list();
                //printf("\nsx->Stype->Tparamtypes:\n");
                //if (sx->Stype->Tparamtypes) sx->Stype->Tparamtypes->print_list();

                // See if sx is a match for s
                if (template_arglst_match(sx->Sfunc->Fptal, s->Sfunc->Fptal))
                    cpperr(EM_explicit_following, s->Sident);
            }
            goto Lfuncdecl;
        }
    }

    if (stag && !(member_template & 2))
    {
        //printf("it's a member template member function\n");
        s = symbol_name(vident, SCfunctempl, t);
        type_free(t);
        s->Sflags |= access_specifier;
        s->Sscope = stag;
        //n2_chkexist(stag, vident);
        //n2_addmember(stag, s);
        n2_addfunctoclass(stag, s, 0);
        if (constructor)
        {   stag->Sstruct->Sflags |= STRanyctor;
            s->Sfunc->Fflags |= Fctor;
            if (!stag->Sstruct->Sctor)
                stag->Sstruct->Sctor = s;
        }
        else if (class_m & mskl(SCstatic))
        {
            s->Sfunc->Fflags |= Fstatic;
        }
        symbol_func(s);
    }
    else
    {
        //printf("it's a template function, gdeclar.class_sym = %p\n", gdeclar.class_sym);
        s = symdecl(vident,t,SCfunctempl,temp_arglist);
        if (!s)
            goto err_ret;
        symbol_func(s);
        list_append(&template_ftlist,s);

        // Also, add s to list for this file
        if (config.flags2 & (CFG2phautoy | CFG2phauto | CFG2phgen) &&   // and doing precompiled headers
            cstate.CSfilblk)                    // and there is a source file
        {   Sfile *sf;

            // Thread definition onto list for this file
            sf = &srcpos_sfile(cstate.CSfilblk->BLsrcpos);
            sfile_debug(sf);
            list_append(&sf->SFtemp_ft,s);
        }
    }

Lfuncdecl:
    if (member_template & 2)            // if friend of stag
    {
        assert(stag);
        list_append(&s->Sfunc->Fclassfriends, stag);
        list_append(&stag->Sstruct->Sfriendfuncs, s);
    }
    funcdef = funcdecl(s,(enum SC)s->Sclass,0,&gdeclar);        // declare function
    f = s->Sfunc;


    /* Merge difference between function declaration (no body) and
        function definition (body was present).
     */
    if (funcdef)                        // if function body was present
    {                                   // then it's a definition
        token_poplist();                // don't parse function body
#if DEBUG_XSYMGEN
        if (xsym_gen && !ph_in_head(f->Fbody))
#endif
        token_free(f->Fbody);           /* eliminate possible previous declaration */
        f->Fbody = tbody;
        f->Fsequence = pstate.STsequence++;
    }
    else
    {   /* It's a declaration   */
        if (f->Fbody)                   /* if an existing body          */
            token_free(tbody);          /* don't use new one            */
        else
            f->Fbody = tbody;           /* use declaration              */
    }

    if (textra)
    {   token_markfree(textra);
        token_setlist(textra);
    }

    stoken();
#if DEBUG_XSYMGEN
    if (xsym_gen && !ph_in_head(f->Farglist))
#endif
    param_free(&f->Farglist);
    f->Farglist = temp_arglist;

    // Do forward references
    if (funcdef)
    {   symlist_t sl;

        for (sl = f->Ffwdrefinstances; sl; sl = list_next(sl))
        {
            symbol *sf = (symbol *)list_ptr(sl);

            // Implement the function sf
            assert(!(sf->Sflags & SFLimplem));
            template_matchfunc(s,sf->Stype->Tparamtypes,4|1,TMATCHexact,NULL);
        }

        list_free(&f->Ffwdrefinstances,FPNULL);
    }
  }
Lret:
  pstate.STmaxsequence = pstatesave.STmaxsequence;
}

/**********************************
 * Look at type t to see if there are any TYident's.
 * If there are, replace them with the type from ptal.
 * This is to handle syntax like:
 *      template <class D, int (*fp)(D), class C = *D) ...
 * Input:
 *      ptal            template-argument-list
 *      ptpl            template-parameter-list
 *      flag            !=0 means issue error message upon error
 * Returns:
 *      replacement type (Tcount incremented)
 *      NULL            if error and !flag
 */

type * template_tyident(type *t,param_t *ptal,param_t *ptpl, int flag)
{   param_t *pa;
    param_t *pt;

    //printf("template_tyident()\n");
    if (tybasic(t->Tty) == TYident)
    {
        //printf("TYident = '%s'\n",t->Tident);
        if (t->Tnext)
        {
            /* This comes from when Tident is a member of another symbol:
             *  Tnext::Tident
             */

            type *tn = template_tyident(t->Tnext, ptal, ptpl, flag);
            if (!tn)
                return NULL;
            switch (tybasic(tn->Tty))
            {
                case TYtemplate:
                case TYident:
                    tn->Tcount--;
                    break;

                default:
                    assert(0);

                case TYstruct:
                {   Classsym *stag = tn->Ttag;
                    symbol *smem = cpp_findmember_nest(&stag, t->Tident, flag != 0);
                    tn->Tcount--;
                    if (smem)
                    {
                        switch (smem->Sclass)
                        {
                            case SCtypedef:
                            case SCstruct:
                            case SCenum:
                                break;

                            default:
                                if (!flag)
                                    return NULL;
                                cpperr(EM_typename_expected, t->Tident, stag->Sident);
                        }
                        t = smem->Stype;
                    }
                    else
                    {   if (!flag)
                            return NULL;
                        t = tsint;
                    }
                    break;
                }
            }
            goto L2;
        }

        pa = ptal;
        for (pt = ptpl; pt && pa; pt = pt->Pnext)
        {   //dbg_printf("template arg '%s'\n",pt->Pident);
            if (strcmp(t->Tident,pt->Pident) == 0 && !pt->Ptype && pa->Ptype)
            {   t = pa->Ptype;
                //dbg_printf("replacing\n");
                goto L2;
            }
            pa = pa->Pnext;
        }
    }
    else if (t->Tty == TYtemplate)
    {
        symbol *stempl = ((typetemp_t *)t)->Tsym;
        Classsym *stag;
        param_t *template_argument_list = NULL;
        param_t *p;

        // Recursively expand t->Tparamtypes

        for (p = t->Tparamtypes; p; p = p->Pnext)
        {   type *tp;

            if (p->Ptype)
            {
                tp = template_tyident(p->Ptype,ptal,ptpl,flag);
                if (!tp)
                    return NULL;
                param_append_type(&template_argument_list,tp);
                tp->Tcount--;
            }
            else if (p->Pelem)
            {
                param_t **pp;

                for (pp = &template_argument_list; *pp; pp = &((*pp)->Pnext))
                    ;
                *pp = param_calloc();
                (*pp)->Pelem = el_copytree(p->Pelem);
            }
            else
            {
                assert(0);
            }
            assert(!p->Pident);
        }

        if (pstate.STintemplate || pstate.STnoexpand)
        {
            t = type_alloc_template(stempl);
            t->Tparamtypes = template_argument_list;
        }
        else
        {
            stag = template_expand2(stempl, template_argument_list);
            t = stag->Stype;
        }
    }
    else if (t->Tnext)
    {   type *tc;

        tc = type_alloc(t->Tty);
        *tc = *t;

        switch (tybasic(tc->Tty))
        {   case TYtemplate:
                // BUG: I don't think this case can happen (t->Tnext == NULL)
                ((typetemp_t *)tc)->Tsym = ((typetemp_t *)t)->Tsym;
                goto L1;

            case TYmemptr:
                if (t->Ttag->Stype->Tty == TYident)
                {
                    type *ttag = template_tyident(t->Ttag->Stype, ptal, ptpl, flag);
                    if (!ttag)
                        return NULL;

                    if (type_struct(ttag))
                        tc->Ttag = ttag->Ttag;
                    else if (tybasic(ttag->Tty) == TYident)
                    {
                        tc->Ttag = t->Ttag;
                        //tc->Tident = (char *)MEM_PH_STRDUP(ttag->Tident);
                    }
                    else
                        assert(0);
                }
                break;

            default:
                if (tyfunc(tc->Tty))
                {   param_t *p;

                L1:
                    tc->Tparamtypes = NULL;
                    for (p = t->Tparamtypes; p; p = p->Pnext)
                    {   type *tp;

                        tp = template_tyident(p->Ptype,ptal,ptpl,flag);
                        if (!tp)
                            return NULL;
                        tp->Tcount--;
                        param_append_type(&tc->Tparamtypes,tp);
                        assert(!p->Pelem);
                    }
                }
                break;
        }

        tc->Tcount = 0;
        tc->Tnext = template_tyident(tc->Tnext,ptal,ptpl,flag);
        if (!tc->Tnext)
            return NULL;

        if (!flag)
        {
            // Regard pointers to references as an error
            if (typtr(tc->Tty) && tyref(tc->Tnext->Tty))
                return NULL;

            if (tybasic(tc->Tty) == TYarray)
            {   type *tn = tc->Tnext;

                // Regard array of functions, references or voids as an error
                if (tyfunc(tn->Tty) ||
                    tyref(tn->Tty) ||
                    tybasic(tn->Tty) == TYvoid)
                    return NULL;

                // DR337: regard array of abstract classes as an error
                if (tybasic(tn->Tty) == TYstruct &&
                    tn->Ttag->Sstruct->Sflags & STRabstract)
                    return NULL;
            }
        }

        t = tc;
    }
L2:
    t->Tcount++;
    //printf("-template_tyident()\n");
    return t;
}

/*******************************
 * Create a symbol out of an identifier, and add it to symbol table.
 */

symbol *template_createsym(const char *id, type *t, symbol **proot)
{
    symbol *s;
    int sc;

    //printf("template_createsym('%s'), sequence = %x\n", id, pstate.STsequence);
    if (!t)
    {   // type parameter
        t = type_alloc(TYident);        /* construct type of type-argument */
        t->Tident = (char *) MEM_PH_STRDUP(id);
        sc = SCtypedef;
    }
    else
    {   // value parameter of type t
        sc = SCconst;
    }

    s = proot ? defsy(id,proot) : symbol_calloc(id);
    s->Sclass = sc;
    s->Stype = t;
    s->Stype->Tcount++;
    return s;
}

/*******************************
 * Create symbol table out of the template-parameter-list so we can parse the
 * template declaration in a specialization.
 * The arguments become place holders in
 * the type structure.
 */

STATIC void template_createargtab(param_t *ptpl)
{   symbol *root;
    int sequence = 0;

    //printf("template_createargtab(ptpl = %p)\n", ptpl);
    assert(level == 0 || level == -1);  // must be at global or class level
    /* Initialize symbol table  */
    root = NULL;

    for (; ptpl; ptpl = ptpl->Pnext)
    {   symbol *s;

        if (!ptpl->Pident)
            continue;

        assert(ptpl->Pelem == NULL);    // no default values in partial specialization

        // If it's a template-template-parameter
        if (ptpl->Pptpl)
        {   type *t;

            //printf("\ttemplate-template-parameter '%s'\n", ptpl->Pident);
            s = defsy(ptpl->Pident, &root);
            //printf("\ts = %p\n", s);
            s->Sclass = SCtemplate;
            s->Stemplate = (template_t *) MEM_PH_CALLOC(sizeof(template_t));
            s->Stemplate->TMtk = TKclass;
            //s->Stemplate->TMptal = ptpl->Pptpl;
            s->Stemplate->TMptpl = ptpl->Pptpl;
            s->Stemplate->TMprimary = s;
            s->Stemplate->TMflags2 = 1;

            t = type_alloc(TYvoid);     // placeholder
            t->Tflags |= TFforward;
            t->Tcount++;
            s->Stype = t;
        }
        else
            s = template_createsym(ptpl->Pident, ptpl->Ptype, &root);
        s->Ssequence = ++sequence;
    }
    //assert(!scope_find(SCTtemparg));
    scope_push(root,(scope_fp)findsy,SCTtemparg);
}

/******************************************
 * Remove symbol table created by template_createargtab().
 */

STATIC void template_deleteargtab()
{   symbol *root;

    assert(scope_end->sctype == SCTtemparg);
    root = (symbol *)scope_pop();
#if 0
    // Don't free because some parameters retain references
    // to symbols, like Pelem OPvar's.
    // Unfortunately, this means a memory leak.
    symbol_free(root);                  // free symbol table
#endif
}

/****************************
 * Define a class template symbol.
 *      tk =    TKstruct, TKclass, TKunion
 */

STATIC symbol * template_define(Classsym *stag, symbol *sprimary, enum_TK tk, int structflags, char *vident, param_t *temp_arglist, param_t *ptal)
{   symbol *s;
    type *t;
    unsigned sct;

    //dbg_printf("template_define('%s')\n",vident);
    if (stag || sprimary)
    {   // Template will be a member of stag
        s = symbol_calloc(vident);
        s->Sclass = SCtemplate;
        if (stag)
        {   s->Sflags |= stag->Sstruct->access;
            s->Sscope = stag;
        }
        if (sprimary)
        {   symbol **ps;

            s->Sscope = sprimary->Sscope;

            // Append to threaded list of class template partial specializations
            for (ps = &sprimary;
                 *ps;
                 ps = &(*ps)->Stemplate->TMpartial)
            {
            }
            *ps = s;
        }
        else
        {   n2_chkexist(stag, vident);
            n2_addmember(stag, s);
        }
    }
    else
    {
        sct = SCTglobal | SCTnspace;
        s = scope_searchinner(vident, sct);
        if (s)
        {   /* Already defined, so create a second 'covered' definition */
            s->Scover = (Classsym *)symbol_calloc(vident);
            s = s->Scover;
        }
        else
            s = scope_define(vident,sct,SCtemplate);
    }

    if (!stag)
    {
        // Also, add s to list for this file
        if (config.flags2 & (CFG2phautoy | CFG2phauto | CFG2phgen) &&   // and doing precompiled headers
            cstate.CSfilblk)                    // and there is a source file
        {   Sfile *sf;

            // Thread definition onto list of templates for this file
            sf = &srcpos_sfile(cstate.CSfilblk->BLsrcpos);
            sfile_debug(sf);
            list_append(&sf->SFtemp_class,s);
        }
    }

    s->Stemplate = (template_t *) MEM_PH_CALLOC(sizeof(template_t));
    s->Stemplate->TMptpl = temp_arglist;
    s->Stemplate->TMtk = tk;
    s->Stemplate->TMflags = structflags;
    s->Stemplate->TMptal = ptal;
    s->Stemplate->TMprimary = sprimary ? sprimary : s;

    if (1 || !stag)
    {
        if (template_class_list_p)
            *template_class_list_p = s;
        else
            template_class_list = s;
        template_class_list_p = &s->Stemplate->TMnext;
    }

    t = type_alloc(TYvoid);             /* need a placeholder type      */
    t->Tflags |= TFforward;
//    t->Ttag = s;                      /* template tag name            */
    t->Tcount++;
    s->Stype = t;
    return s;
}


/*******************************
 * Instantiate anything else.
 */

void template_instantiate()
{
    //printf("template_instantiate()\n");

    if (!CPP)
        return;
#if PUBLIC_EXT
    list_t expand_list = NULL;          // list of explicit items to expand
#endif
    if (errcnt)
    {
#if TERMCODE
        list_free(&template_xlist,FPNULL);
        list_free(&template_ftlist,FPNULL);
#endif
        return;
    }

  {
    /* Instantiate anything given on the command line.  */
    list_t pl;
    char *p;
    int eofcnt;
    char bOperator;
#if PUBLIC_EXT
    symbol *se;
#endif

    //dbg_printf("template_instantiate xlist %x\n",template_xlist);
    for (pl = template_xlist; pl; pl = list_next(pl))
    {
        p = (char *) list_ptr(pl);
        assert(p);
        insblk2((unsigned char *) p,BLarg);
        egchar2();
        eofcnt = 0;
    L3:
        stoken();
        bOperator = FALSE;
        switch (tok.TKval)
        {   int op;
            type *tconv;
            symbol *s;
            char *id;
            param_t *pl;

            case TKeof:
                if (eofcnt)                     // don't hang at EOF
                    goto err;
                eofcnt++;
                goto L3;                        // try again
            case TKoperator:
                id = cpp_operator(&op,&tconv);
                bOperator = TRUE;
                goto L2;
            case TKident:
                id = tok.TKid;
            L2:
                s = scope_search(id,SCTglobal); /* search global symbol table */
            L1:
                if (!s)
                    goto err;
                switch (s->Sclass)
                {
                    case SCtemplate:            /* class template       */
#if PUBLIC_EXT
                        se =
#endif
                        template_expand(s,0);
#if PUBLIC_EXT
                        if (template_access == SCglobal)
                            list_append(&expand_list, se);
#endif
                        continue;
                    case SCstatic:
                    case SCglobal:
                    case SCextern:
                        if (!tyfunc(s->Stype->Tty))
                            break;
                    case SCfuncalias:
                        s = s->Sfunc->Foversym;
                        goto L1;
                    case SCfunctempl:           /* function template    */
                        if (!bOperator)
                            stoken();
                        chktok(TKlpar,EM_lpar2, "function template");

                        pl = NULL;
                        while (1)
                        {   type *typ_spec;
                            type *t;

                            if (!type_specifier(&typ_spec) )
                            {   type_free(typ_spec);
                                param_free(&pl);
                                goto err;
                            }
                            t = declar_abstract(typ_spec);
                            fixdeclar(t);
                            param_append_type(&pl,t);
                            type_free(t);
                            type_free(typ_spec);
                            if (tok.TKval != TKcomma)
                            {   chktok(TKrpar,EM_rpar);
                                break;
                            }
                            stoken();           /* skip over ','        */
                        }
#if TX86
                        s = template_matchfunc(s,pl,TRUE,TMATCHexact,NULL);
#else
#if PUBLIC_EXT
                        template_expansion = TRUE;
#endif
//
// Need to call template_implemented_already so that if the template function
// already has this expansion implemented, it is not implemented again.
//
                        if ((se = template_implemented_already( stemp, pl )) != NULL)
                            s = se;
                        else
                            s = template_matchfunc(stemp,pl,TRUE, TMATCHexact,NULL);
#endif
                        param_free(&pl);
                        if (!s)
                            goto err;
                        continue;
                    default:
                        goto err;
                }
            default:
#ifdef DEBUG
                tok.print();
#endif
                goto err;
        }
    err:
        cpperr(EM_cant_gen_templ_inst,p); // cannot generate template instance
    }

    list_free(&template_xlist,FPNULL);
  }

  {
    /* Instantiate outlined member functions for each template class
       for each instantiation of that class.
     */
    symbol *s;
    symlist_t sl;
    symlist_t ilist;            // instantiated class list
    int anyinst;                // did we instantiate any class?

    ilist = NULL;
    do
    {
    anyinst = 0;
    for (s = template_class_list; s; s = s->Stemplate->TMnext)
    {   symbol_debug(s);
        assert(s->Sclass == SCtemplate);

        // For each instance of template class s
        //printf("instantiating outlined member functions for template class '%s' %p\n", s->Sident, s);
        for (sl = s->Stemplate->TMinstances; sl; sl = list_next(sl))
        {   Classsym *si = (Classsym *)list_symbol(sl);
            list_t tl;
            enum SC gclasssave;

            symbol_debug(si);

            /*  If class was not generated from the template tokens, then
                don't generate the member functions either
             */
            if (!(si->Sstruct->Sflags & STRgen))
                continue;

            if (list_inlist(ilist,si))      // if class already instantiated
                continue;

#if PUBLIC_EXT
           // only expand the templates specified on the command line
           if (template_access == SCglobal && !list_inlist(expand_list, si))
                continue;
#endif

            //dbg_printf("\tinstance '%s'\n",si->Sident);
            anyinst = 1;

            gclasssave = pstate.STgclass;
            pstate.STgclass = (config.flags2 & CFG2comdat) ? SCcomdat : SCstatic;
            // If explicit instantiation, comdat's become global
            if (pstate.STgclass == SCcomdat && si->Sstruct->Sflags & STRexplicit)
                pstate.STgclass = SCglobal;
            pstate.STflags |= PFLmftemp;

            /* Instantiate each member  */
            for (tl = s->Stemplate->TMmemberfuncs; tl; tl = list_next(tl))
            {
                int nscopes = 0;
                TMF *tmf = (TMF *)list_ptr(tl);
                //printf("\tinstantiating TMF %p\n", tmf);

                if (tmf->member_class)
                    continue;
#if 1
                nscopes = scope_pushEnclosing(s);
#else
                if (isclassmember(s))
                {   Classsym *stag = (Classsym *)s->Sscope;

                    if (stag->Sstruct->Stempsym)
                    {
                        //printf("\tstag '%s' is an instance of template '%s'\n", stag->Sident, stag->Sstruct->Stempsym->Sident);
                        template_createsymtab(
                            stag->Sstruct->Stempsym->Stemplate->TMptpl,
                            stag->Sstruct->Sarglist);
                        nscopes++;

                        // BUG: should add in outer scopes, too
                    }
                }
#endif

                if (tmf->temp_arglist)
                {   template_createsymtab(tmf->temp_arglist, si->Sstruct->Sarglist);
                    nscopes++;
                }
                else
                {   // It's an explicit specialization
                    // Skip if it doesn't match this instantiation
                    if (!template_arglst_match(tmf->ptal, si->Sstruct->Sarglist))
                    {
#if 0
                        printf("\tno explicit match\n");
                        param_t *p;
                        printf("tmf->ptal:\n");
                        for (p = tmf->ptal; p; p = p->Pnext)
                        {
                            p->print();
                        }
                        printf("si->Sstruct->Sarglist:\n");
                        for (p = si->Sstruct->Sarglist; p; p = p->Pnext)
                        {
                            p->print();
                        }
#endif
                        scope_unwind(nscopes);
                        continue;
                    }
                }

                if (tmf->member_template & 1)
                {   symbol *s;
                    symbol *sf;

                    //printf("instantiating member template function '%s'\n", tmf->name ? tmf->name : "null");
                    if (tmf->castoverload)
                    {
                        //s = si->Sstruct->Scastoverload;
                        assert(0); // BUG: not implemented
                    }
                    else if (template_classname(tmf->name, si))
                    {
                        s = si->Sstruct->Sctor;
                    }
                    else
                        s = n2_searchmember(si, tmf->name);
                    assert(s);
                    if (s->Sclass == SCtemplate)
                    {
                        //printf("nested template member function\n");

                        /* Need to build a new TMF to add to TMmemberfuncs
                         * of s. Do it by restarting template_function_decl()
                         * where we left off.
                         */
                        template_function_decl(tmf->stag,
                                tmf->temp_arglist2,
                                NULL,
                                tmf->member_template & ~1,
                                tmf->access_specifier,
                                tmf->tbody,
                                tmf->to,
                                si);
                    }
                    else
                    {
                        for (sf = s; sf; sf = sf->Sfunc->Foversym)
                        {
                            symbol_debug(sf);
                            if (sf->Sclass != SCfunctempl &&
                                sf->Sclass != SCfuncalias &&
                                !(sf->Sflags & SFLimplem) &&
                                sf->Sfunc->Fflags & Finstance)
                            {   symbol *st;

                                // Try each function template
                                for (st = s; st; st = st->Sfunc->Foversym)
                                {
                                    if (st->Sclass == SCfunctempl)
                                    {   symbol *sm;

                                        token_t *save = st->Sfunc->Fbody;
                                        st->Sfunc->Fbody = tmf->tbody;
                                        if (tmf->ptal)
                                            pstate.STexplicitSpecialization++;
                                        //printf("calling template_matchfunc()\n");
                                        sm = template_matchfunc(st,sf->Stype->Tparamtypes,4|1,TMATCHexact,NULL);
                                        if (tmf->ptal)
                                            pstate.STexplicitSpecialization--;
                                        st->Sfunc->Fbody = save;
                                        if (sm)
                                        {
                                            //printf("it's instantiated\n");
                                            break;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                else
                {
                    //printf("instantiating non-member template function '%s'\n", s->Sident);
                    token_setlist(tmf->tbody);
#if 0
                    printf("+++++\n");
                    token_funcbody_print(tmf->tbody);
                    printf("-----\n");
#endif
                    stoken();
                    ext_def(0);                 // parse instantiations

                    if (tmf->sclassfriend)
                    {   Classsym *stag = (Classsym *)tmf->sclassfriend;

                        symbol *s = pstate.STlastfunc;
                        if (s && tyfunc(s->Stype->Tty))
                        {
                            //printf("\t'%s' which is a friend of class '%s'\n", s->Sident, stag->Sident);
                            list_append(&s->Sfunc->Fclassfriends, stag);
                            list_append(&stag->Sstruct->Sfriendfuncs, s);
                        }
                    }
                }
                scope_unwind(nscopes);
            }

            pstate.STflags &= ~PFLmftemp;
            pstate.STgclass = gclasssave;

            list_prepend(&ilist,si);
        }
    }
    } while (anyinst);
    list_free(&ilist,FPNULL);
  }

#if PUBLIC_EXT
  if (template_access != SCglobal)
#endif
#if TX86
  // VC 2.0 does not instantiate functions for which a declaration exists
  // but no body does. So, we don't either.
  if (config.flags4 & CFG4tempinst)
#endif
  {
    /* Instantiate any undefined functions that we can using function
        templates.
     */
    symlist_t sl;

    for (sl = template_ftlist; sl; sl = list_next(sl))
    {   symbol *s = list_symbol(sl);
        symbol *sf;

        //dbg_printf("looking at '%s' in template_ftlist\n",cpp_prettyident(s));
        s = scope_search(s->Sident,SCTglobal | SCTnspace);
        assert(s);
        for (sf = s; sf; sf = sf->Sfunc->Foversym)
        {
            symbol_debug(sf);
            if (sf->Sclass != SCfunctempl &&
                sf->Sclass != SCfuncalias &&
                !(sf->Sflags & SFLimplem))
            {   symbol *st;

                /* Try each function template   */
                for (st = s; st; st = st->Sfunc->Foversym)
                {
                    if (st->Sclass == SCfunctempl)
                    {
                        if (template_matchfunc(st,sf->Stype->Tparamtypes,TRUE,TMATCHexact,NULL))
                        {
                            //dbg_printf("instantiated\n");
                            break;
                        }
                    }
                }
            }
        }
    }

  }
    list_free(&template_ftlist,FPNULL);
#if PUBLIC_EXT && TX86 // DJB
    list_free(&expand_list,FPNULL);
#endif
}


/*****************************************************
 * Instantiate class member tmf for instance si of class template st.
 */

void template_instantiate_classmember(Symbol *stempl, symbol *si, TMF *tmf)
{
    //printf("template_instantiate_classmember(stempl = '%s', si = '%s', tmf = %p)\n", stempl->Sident, si->Sident, tmf);
    symbol_debug(stempl);
    symbol_debug(si);

    /*  If class was not generated from the template tokens, then
        don't generate the members either
     */
    if (!(si->Sstruct->Sflags & STRgen))
        return;

    // If not a member class, skip
    if (!tmf->member_class)
        return;

    if (!tmf->temp_arglist)             // if explicit specialization
    {
        // Skip if it doesn't match this instantiation
        if (!template_arglst_match(tmf->ptal, si->Sstruct->Sarglist))
        {
#if 0
            printf("\tno explicit match\n");
            param_t *p;
            printf("tmf->ptal:\n");
            for (p = tmf->ptal; p; p = p->Pnext)
            {
                p->print();
            }
            printf("si->Sstruct->Sarglist:\n");
            for (p = si->Sstruct->Sarglist; p; p = p->Pnext)
            {
                p->print();
            }
#endif
            return;
        }
    }

    //dbg_printf("\tinstance '%s'\n",si->Sident);

    Pstate pstatesave = pstate;
    pstate.STgclass = (config.flags2 & CFG2comdat) ? SCcomdat : SCstatic;
    // If explicit instantiation, comdat's become global
    if (pstate.STgclass == SCcomdat && si->Sstruct->Sflags & STRexplicit)
        pstate.STgclass = SCglobal;

    pstate.STmaxsequence = stempl->Stemplate->TMsequence;
    pstate.STflags |= PFLmftemp;

    int nscopes = 0;

    nscopes = scope_pushEnclosing(stempl);

    if (tmf->temp_arglist)
    {   template_createsymtab(tmf->temp_arglist, si->Sstruct->Sarglist);
        nscopes++;
    }

    assert(!(tmf->member_template & 1));

    //printf("instantiating non-member template function '%s'\n", s->Sident);
    token_unget();
    token_setlist(tmf->tbody);

#if 0
    printf("+++++\n");
    token_funcbody_print(tmf->tbody);
    printf("-----\n");
#endif

    unsigned char tk = stoken();
    stoken();
    stunspec(tk, NULL, NULL, NULL);
    stoken();

    scope_unwind(nscopes);

    pstate.STflags &= ~PFLmftemp;
    pstate.STmaxsequence = pstatesave.STmaxsequence;
    pstate.STgclass = pstatesave.STgclass;
}

/*********************************************
 * Given a class member tmf, generate classes for each instantiation of
 * class template st.
 */

void template_instantiate_classmember(Symbol *st, TMF *tmf)
{
    assert(st->Sclass == SCtemplate);

    // For each instance of template class s
    //printf("instantiating outlined member functions for template class '%s' %p\n", s->Sident, s);
    for (symlist_t sl = st->Stemplate->TMinstances; sl; sl = list_next(sl))
    {   Classsym *si = (Classsym *)list_symbol(sl);

        template_instantiate_classmember(st, si, tmf);
    }
}

/*********************************************
 * Given a class template instance si of class template st,
 * generate all the class members.
 */

void template_instantiate_classmember(Symbol *st, Symbol *si)
{
    assert(st->Sclass == SCtemplate);

    for (list_t tl = st->Stemplate->TMmemberfuncs; tl; tl = list_next(tl))
    {
        TMF *tmf = (TMF *)list_ptr(tl);
        //printf("\tTMF %p\n", tmf);

        template_instantiate_classmember(st, si, tmf);
    }
}

/***********************************
 * Determine if identifier is a match for the class name.
 * The idea here is that when we are expanding a class template, if
 * we see a class name, the name will be that of the template, not the
 * instance name of the class.
 * Input:
 *      vident  identifier
 *      stag    tag symbol of class
 * Returns:
 *      0       not a class name
 *      1       is a class name
 */

int template_classname(char *vident,Classsym *stag)
{   symbol *s;

    symbol_debug(stag);
    type_debug(stag->Stype);
    debug(assert(tybasic(stag->Stype->Tty) == TYstruct));
    assert(stag->Sclass != SCtypedef);
    if (stag->Sstruct->Stempsym)        /* if stag was generated from a template */
        s = stag->Sstruct->Stempsym;    /* use the template name        */
    else
        s = stag;
    symbol_debug(s);
    return strcmp(vident,s->Sident) == 0;
}


/*******************************
 * Parse the template-argument-list (CPP98 14.2) for an instance of a template.
 * Input:
 *      s       class template symbol.
 *      tok is on the opening '<'
 * Returns:
 *      template-argument-list
 *      tok has an unspecified value in it, next stoken() will read
 *      token following name<args>
 */

param_t * template_gargs(symbol *s)
{   param_t *arglist;
    param_t *pt;
    param_t **pp;
    param_t *ptpl;
    int nargs;
    enum_TK tkval;

    //printf("template_gargs('%s', s = %p)\n", s->Sident, s);
    assert(s);
    symbol_debug(s);

    // Get template-parameter-list
    switch (s->Sclass)
    {
        case SCtemplate:
            if (s->Stemplate->TMprimary)
                s = s->Stemplate->TMprimary;
            ptpl = s->Stemplate->TMptpl;
            break;
        case SCfunctempl:
            ptpl = s->Sfunc->Farglist;
            break;
        default:
            assert(0);
    }
//assert(ptpl);

    /* Read in parameter list   */

    switch (tok.TKval)
    {
        case TKlt:
            tkval = stoken();
            break;

        case TKlg:                      // <> token
            if (pstate.STignoretal)
                return NULL;
            tkval = TKgt;
            break;

        default:
            cpperr(EM_lt_following,prettyident(s));     // '<' expected
            break;
    }

    // See if we should just skip it
    if (pstate.STignoretal)
    {   int anglebracket = 1;
        int bracket = 0;
        int paren = 0;

        while (1)
        {
            switch (tok.TKval)
            {
                case TKlt:
                    if (!paren && !bracket)
                        anglebracket++;
                    break;

#if ANGLE_BRACKET_HACK
                case TKshr:
                    if (!paren && !bracket && --anglebracket == 0)
                        goto case_err;
#endif
                case TKgt:
                    if (!paren && !bracket && --anglebracket == 0)
                        return NULL;
                    break;

                case TKeof:
                case_err:
                    cpperr(EM_gt);              // '>' expected
                    return NULL;

                case TKlpar:
                    paren++;
                    break;

                case TKrpar:
                    paren--;
                    break;

                case TKlbra:
                    bracket++;
                    break;

                case TKrbra:
                    bracket--;
                    break;
            }
            stoken();
        }
    }

    pstate.STingargs++;

    /* Loop through template argument list      */
    arglist = NULL;
    pp = &arglist;
    pt = ptpl;
    nargs = 0;
    if (pt)
    while (1)
    {   param_t *p;

        //tok.print();
        if (tkval == TKgt && s->Sclass == SCfunctempl)
            break;

        nargs++;
        p = param_calloc();
        *pp = p;
        pp = &(p->Pnext);               /* append to list               */
        p->Pflags |= PFexplicit;

        /* Looking for either a type or an expression   */
        if (pt->Ptype)
        {   // Looking for an expression of type pt->Ptype
            elem *e;
            type *t;
            char inarglistsave = pstate.STinarglist;

            t = template_tyident(pt->Ptype,arglist,ptpl,TRUE);

            /* See if we should use default initializer */
            if (tkval == TKgt && pt->Pelem)
                e = el_copytree(pt->Pelem);
            else
            {
                if (!pt->Pnext ||
                     pt->Pnext->Pelem ||
                     pt->Pnext->Pdeftype ||
                     pt->Pnext->Psym)           // if last argument
                    pstate.STinarglist++;       // regard > as not an operator
                if (pstate.STintemplate)
                {   pstate.STintemplate--;
                    e = assign_exp();
                    pstate.STintemplate++;
                }
                else
                    e = assign_exp();
                pstate.STinarglist = inarglistsave;
                elem_debug(e);
                tkval = tok.TKval;
            }
            e = poptelem3(typechk(arraytoptr(e),t));
            type_free(t);
            p->Pelem = e;

            /* C++98 14.3.2-3 "Addresses of array elements and names or
             * addresses of nonstatic class members are not acceptable
             * template-arguments."
             */
            if (e->Eoper == OPrelconst && typtr(e->ET->Tty))
            {   symbol *s = e->EV.sp.Vsym;

                if (e->PEFflags & PEFaddrmem || e->EV.ss.Voffset)
                    cpperr(EM_bad_template_arg, s->Sident);

                if (type_struct(s->Stype))
                {
                    if (tybasic(s->Stype->Tty) != tybasic(e->ET->Tnext->Tty) ||
                        s->Stype->Ttag != e->ET->Tnext->Ttag)
                        cpperr(EM_bad_template_arg, s->Sident);
                }
            }

        }
        else if (pt->Pptpl)
        {   // Looking for a template-template-parameter
            symbol *s;

            if (tkval == TKgt)
                s = pt->Psym;
            else
            {   s = id_expression();
                tkval = tok.TKval;
            }
            if (s)
            {   if (s->Sclass != SCtemplate)
                {
                    cpperr(EM_class_template_expected, s->Sident); // only class templates allowed
                    s = NULL;
                }
            }
            p->Psym = s;
        }
        else
        {   // Looking for a type-parameter
            type *t;

            if (tkval == TKgt)
            {
                if (pt->Pdeftype)               // if default type argument
                {   t = pt->Pdeftype;
                    p->Ptype = template_tyident(t,arglist,ptpl,TRUE);
                }
                else
                {   synerr(EM_num_args,nargs,s->Sident,nargs + 1);      // %d args expected
                    p->Ptype = tserr;
                    tserr->Tcount++;
                }
            }
            else
            {
                if (!type_specifier(&t))
                    cpperr(EM_type_argument,pt->Pident,s->Sident);      // must be type-argument
                p->Ptype = declar_abstract(t);
                fixdeclar(p->Ptype);
                type_free(t);
                tkval = tok.TKval;
            }
        }
        pt = pt->Pnext;
        if (pt)                         /* if more args coming          */
        {
            if (tkval == TKcomma)
            {   tkval = stoken();
            }
            else if (!(tkval == TKgt &&
                      (pt->Pelem || pt->Pdeftype || pt->Psym || s->Sclass == SCfunctempl)))
            {   int nactual = nargs;

                nargs += pt->length();
                synerr(EM_num_args,nargs,s->Sident,nactual);    // %d args expected
                break;
            }
        }
        else
            break;
    }

    switch (tkval)
    {   case TKgt:
            break;

#if ANGLE_BRACKET_HACK
        case TKshr:
            tok.TKval = TKgt;
            token_unget();
            break;
#endif

        default:
#ifdef DEBUG
            tok.print();
#endif
            cpperr(EM_gt);              // '>' expected
            panic(TKgt);
            break;
    }
    //printf("-template_gargs('%s')\n", s->Sident);
    pstate.STingargs--;
    return arglist;
}

param_t * template_gargs2(symbol *s)
{   param_t *arglist;
    param_t *pt;
    param_t **pp;
    int nargs;
    enum_TK tkval;

    //printf("template_gargs2('%s')\n", s->Sident);
    assert(s);
    symbol_debug(s);

    /* Read in parameter list   */

    switch (tok.TKval)
    {
        case TKlt:
            tkval = stoken();
            break;

        case TKlg:                      // <> token
            return NULL;

        default:
            cpperr(EM_lt_following,prettyident(s));     // '<' expected
            break;
    }

    // See if we should just skip it
    if (pstate.STignoretal)
    {   int anglebracket = 1;
        int bracket = 0;
        int paren = 0;

        while (1)
        {
            switch (tok.TKval)
            {
                case TKlt:
                    if (!paren && !bracket)
                        anglebracket++;
                    break;

#if ANGLE_BRACKET_HACK
                case TKshr:
                    if (!paren && !bracket && --anglebracket == 0)
                        goto case_err;
#endif

                case TKgt:
                    if (!paren && !bracket && --anglebracket == 0)
                        return NULL;
                    break;

                case TKeof:
                case_err:
                    cpperr(EM_gt);              // '>' expected
                    return NULL;

                case TKlpar:
                    paren++;
                    break;

                case TKrpar:
                    paren--;
                    break;

                case TKlbra:
                    bracket++;
                    break;

                case TKrbra:
                    bracket--;
                    break;
            }
            stoken();
        }
    }

    // Loop through template argument list
    arglist = NULL;
    pp = &arglist;
    while (1)
    {   param_t *p;
        int isexp;

        //tok.print();
        p = param_calloc();
        *pp = p;
        pp = &(p->Pnext);               /* append to list               */
        p->Pflags |= PFexplicit;

        // Looking for either a type or an expression or a template
        isexp = isexpression();
        if (isexp == 4)                 // if template
        {   symbol *s;

            s = id_expression();
            if (s)
            {   if (s->Sclass != SCtemplate)
                {
                    cpperr(EM_class_template_expected, s->Sident); // only class templates allowed
                    s = NULL;
                }
            }
            p->Psym = s;
        }
        else if (isexp)
        {
        Lisexp:
            elem *e;

            //printf("isexpression\n");
            if (pstate.STintemplate)
            {   pstate.STintemplate--;
                e = assign_exp();
                pstate.STintemplate++;
            }
            else
                e = assign_exp();
            elem_debug(e);
            e = poptelem3(arraytoptr(e));
            p->Pelem = e;
        }
        else
        {   // Get type-parameter
            type *t = NULL;

            //printf("istype\n");
            if (!type_specifier(&t))
            {   type_free(t);
                goto Lisexp;
            }
            p->Ptype = declar_abstract(t);
            fixdeclar(p->Ptype);
            type_free(t);
        }

        switch (tok.TKval)
        {
            case TKcomma:
                tkval = stoken();
                continue;

            case TKgt:
                break;

#if ANGLE_BRACKET_HACK
            case TKshr:
                tok.TKval = TKgt;
                token_unget();
                break;
#endif

            default:
#ifdef DEBUG
                tok.print();
#endif
                cpperr(EM_gt);          // '>' expected
                panic(TKgt);
                break;
        }
        break;
    }

    //printf("-template_gargs2('%s')\n", s->Sident);
    return arglist;
}

/******************************************
 * Determine if two template argument lists match.
 * Similar to paramlstmatch().
 * Return !=0 if they do.
 */

int template_arglst_match(param_t *p1, param_t *p2)
{
    while (p1 != p2)
    {
        if (!p1 || !p2)
            goto Lnomatch;
        if (!typematch(p1->Ptype, p2->Ptype, 0))
            goto Lnomatch;
        if (!el_match4(p1->Pelem, p2->Pelem))
            goto Lnomatch;
        if (p1->Psym != p2->Psym)
            goto Lnomatch;
        if (!template_arglst_match(p1->Pptpl, p2->Pptpl))
            goto Lnomatch;

        p1 = p1->Pnext;
        p2 = p2->Pnext;
    }
    return 1;

Lnomatch:
    return 0;
}

/***********************************
 * See if template symbol is a synonym for an enclosing class instance
 * of that template.
 * Input:
 *      s       template symbol
 * Returns:
 *      NULL    no enclosing instance
 *      !=NULL  the enclosing instance
 */

Classsym *template_inscope(symbol *s)
{
    //printf("template_inscope('%s')\n", s->Sident);
    //printf("pstate.STclasssym = %p\n", pstate.STclasssym);

    for (; s; s = s->Stemplate->TMpartial)
    {
        for (symbol *sscope = pstate.STclasssym; sscope; sscope = sscope->Sscope)
        {
            if (list_inlist(s->Stemplate->TMinstances,sscope))
            {   // Allow template name as synonym for this instance
                return (Classsym *)sscope;
            }
        }

        if (funcsym_p && isclassmember(funcsym_p) &&
            list_inlist(s->Stemplate->TMinstances,funcsym_p->Sscope))
        {   // Allow template name as synonym for this instance
            return (Classsym *)funcsym_p->Sscope;
        }
    }
    //printf("not in scope\n");
    return NULL;
}

/*******************************
 * Expand a class template into a type (that is, a dummy type).
 * Input:
 *      s       primary class template symbol.
 *      tok is on identifier of symbol s.
 * Returns:
 *      dummy type of instantiated class.
 *      tok has an unspecified value in it, next stoken() will read
 *      token following name<args>
 */

type *template_expand_type(symbol *s)
{
    type *t;
    param_t *ptal;

    //dbg_printf("template_expand_type('%s')\n",s->Sident);
    stoken();
    if (tok.TKval != TKlt && tok.TKval != TKlg)
    {   symbol *si;

        si = template_inscope(s);
        if (si)
        {
            token_unget();
            return si->Stype;
        }
    }
    ptal = template_gargs(s);
    t = type_alloc_template(s);
    t->Tparamtypes = ptal;
    return t;
}

/******************************************
 * If stag is forward referenced, and is an instantiation of a class template
 * that is not forward referenced, instantiate it.
 */

void template_instantiate_forward(Classsym *stag)
{   symbol *stempl;

//printf("template_instantiate_forward(%p, '%s')\n", stag, stag->Sident);
//if (stag->Stype->Tflags & TFforward) printf("\tstag is forward\n");
//if (stag->Sstruct->Sflags & STRinstantiating) printf("\tstag is instantiating\n");

    assert(PARSER);
    stempl = stag->Sstruct->Stempsym;
//printf("\tstempl = %p\n", stempl);
//if (stempl && stempl->Stype->Tflags & TFforward) printf("\tstempl is forward\n");
    if (stempl &&                               // if instantiation of a template
        stag->Stype->Tflags & TFforward &&      // forward referenced class
        !(stempl->Stype->Tflags & TFforward) && // template has a body
        !(stag->Sstruct->Sflags & STRinstantiating)
       )
    {
        Declar save = gdeclar;
        Classsym *classsymsave = pstate.STclasssym;
        symlist_t classlistsave = pstate.STclasslist;
        char noparsesave = pstate.STnoparse;
        int levelsave = level;
        linkage_t linkagesave = linkage;
        symbol *funcsym_save = funcsym_p;
        Scope *scsave;
        symbol *sprimary;

//printf("+template_instantiate_forward('%s')\n", stag->Sident);
//printf("STinexp = %d, STnoparse = %d\n",pstate.STinexp,pstate.STnoparse);

        // Make stag a friend of every class it should be a friend of
        sprimary = stempl->Stemplate->TMprimary;
        assert(sprimary);
        for (list_t fl = sprimary->Stemplate->TMfriends; fl; fl = list_next(fl))
        {   Classsym *sf = (Classsym *)list_ptr(fl);

            assert(sf->Sclass == SCstruct);
            //printf("class '%s' is now a friend of class '%s'\n", stag->Sident, sf->Sident);
            list_append(&sf->Sstruct->Sfriendclass, stag);
            list_append(&stag->Sstruct->Sclassfriends, sf);
        }

        pstate.STclasssym = NULL;
        pstate.STclasslist = NULL;
        pstate.STnoparse = 0;
        level = 0;                              // at global level
        linkage = LINK_CPP;
        funcsym_p = NULL;

        // Note equivalence of scope setting code to that in
        // template_matchfunc().

        int nscopes = 0;
        scsave = scope_end;
        Scope::setScopeEnd(scope_find(SCTglobal));

        nscopes = scope_pushEnclosing(stempl);

        stag->Sstruct->Sflags |= STRinstantiating;
//printf("+instantiating %p, %s\n", stag, stag->Sident);
        token_unget();
        token_setident((char *)stag->Sident);
        template_parsebody(stag, stempl, stag->Sstruct->Sarglist);
        stag->Sstruct->Sflags &= ~STRinstantiating;
//printf("-instantiating %p, %s\n", stag, stag->Sident);

        // Unwind scope back to global
        scope_unwind(nscopes);

        Scope::setScopeEnd(scsave);
        funcsym_p = funcsym_save;
        linkage = linkagesave;
        level = levelsave;
        pstate.STnoparse = noparsesave;

        while (pstate.STclasslist)
        {
            list_append(&classlistsave, list_pop(&pstate.STclasslist));
        }
        pstate.STclasslist = classlistsave;

        pstate.STclasssym = classsymsave;
        gdeclar = save;
    }
}

/*******************************
 * Expand a class template into a symbol.
 * Input:
 *      stag    instance of stempl (NULL if not known)
 *      stempl  class template symbol.
 *      arglist template-argument-list
 *      tok     on identifier of symbol s.
 * Returns:
 *      symbol of instantiated class (NULL if error).
 *      tok has an unspecified value in it, next stoken() will read
 *      token following name<args>
 */


STATIC Classsym * template_parsebody(Classsym *stag, symbol *stempl, param_t *arglist)
{
    Scope *scsave;
    type *t;
    Pstate pstatesave = pstate;

    //printf("template_parsebody(stempl = %p)\n", stempl);
    //dbg_printf("generating instantiation from template\n");
//    scsave = scope_end;
//    Scope::setScopeEnd(scope_find(SCTglobal | SCTnspace));

    /* Turn arglist into symbol table   */
    template_createsymtab(stempl->Stemplate->TMptpl,arglist);

    pstate.STinparamlist = 0;
    pstate.STinexp = 0;
    pstate.STnewtypeid = 0;
    pstate.STdefaultargumentexpression = 0;
    pstate.STmaxsequence = stempl->Stemplate->TMsequence;

    token_setlist(stempl->Stemplate->TMbody);
    t = stunspec(stempl->Stemplate->TMtk,stag,stempl,arglist);  // parse class declaration
    template_deletesymtab();    /* remove temporary symbol table */
//    Scope::setScopeEnd(scsave);

    pstate.STmaxsequence = pstatesave.STmaxsequence;
    pstate.STinparamlist = pstatesave.STinparamlist;
    pstate.STinexp = pstatesave.STinexp;
    pstate.STnewtypeid = pstatesave.STnewtypeid;
    pstate.STdefaultargumentexpression = pstatesave.STdefaultargumentexpression;

    if (!type_struct(t))
    {
        return(NULL);
    }
    stag = t->Ttag;
    stag->Sstruct->Sflags |= STRgen;    /* this instance was generated  */
    template_instantiate_classmember(stempl, stag);
    return stag;
}

#if 0
void template_parse_nobody(void)
{
    symlist_t sl;
    Classsym *si;
    symlist_t sl_tmp;
    symbol *s;

    sl_tmp = tmpl_noparse_list;
    tmpl_noparse_list = NULL;
    for ( sl = sl_tmp; sl; sl = list_next(sl) ) {
        si = (Classsym *)list_symbol(sl);
        template_instantiate_forward(si);
        s = si->Sstruct->Stempsym;
        instantiate_list( s, si, s->Stemplate->TMinlinememberfuncs);
    }
    list_free( &sl_tmp, FPNULL );
}
#endif

/*******************************
 * Expand a class template into a symbol.
 * Input:
 *      s       class template symbol.
 *      flag    1       0: Might be an explicit definition
 *                      1: Definitely not an explicit definition
 *              2       token is on "<"
 *      tok     on identifier of symbol s.
 * Returns:
 *      symbol of instantiated class (NULL if error).
 *      tok has an unspecified value in it, next stoken() will read
 *      token following name<args>
 */

Classsym *template_expand(symbol *s,int flag)
{   param_t *template_argument_list;
    Classsym *si;
    enum_TK tk;
    type *t;

    //printf("template_expand('%s', flag = x%x)\n",s->Sident,flag);
    if (!(flag & 2))
        stoken();
    if (tok.TKval != TKlt && tok.TKval != TKlg)
    {
        si = template_inscope(s);
        if (si)
        {
            token_unget();
            return si;
        }
    }
    template_argument_list = template_gargs(s);
    si = template_expand2(s, template_argument_list);
    s = si->Sstruct->Stempsym;

    if (!(flag & 1))
    {
        tk = stoken();
        if (tk == TKcolon || tk == TKlcur)
        {   /* Generate explicit class definition from source   */

            //dbg_printf("generating explicit instantiation '%s'\n",si->Sident);
            token_unget();
            token_setident(si->Sident);         // set token to be template id
//printf("+instantiating %p '%s')\n", si, si->Sident);
            si->Sstruct->Sflags |= STRinstantiating;
            t = stunspec(s->Stemplate->TMtk,si,s,si->Sstruct->Sarglist);        // parse class declaration
            si->Sstruct->Sflags &= ~STRinstantiating;
//printf("-instantiating %p '%s')\n", si, si->Sident);
            if (!type_struct(t))
            {
                return NULL;
            }
        }
        token_unget();                  // back up scanner
    }
    symbol_debug(si);
    //printf("-template_expand('%s',x%x)\n",si->Sident,flag);
    return si;
}

/******************************************
 * Expand a template stempl with arguments into a Classsym.
 * Input for:
 *      template<class T> struct A<T*> { ... }
 *      A<int>
 *
 *  template_argument_list: <int*>
 */

STATIC Classsym *template_expand2(symbol *stempl, param_t *template_argument_list)
{
    symlist_t sl;
    Classsym *si;
    char *tident;
    symbol *sp;

#define LOG_EXPAND2     0

#if LOG_EXPAND2
    printf("template_expand2() stempl = '%s', %p\n", stempl->Sident, stempl);
#endif

    stempl = stempl->Stemplate->TMprimary;
    assert(stempl);

    // Create template identifier
    tident = template_mangle(stempl, template_argument_list);
    //printf("\ttident = '%s'\n", tident);

    // Look for a match with an existing instantiation of stempl
    for (sp = stempl; sp; sp = sp->Stemplate->TMpartial)
    {
#if LOG_EXPAND2
        printf("\tsp = %p\n", sp);
#endif
        for (sl = sp->Stemplate->TMinstances; sl; sl = list_next(sl))
        {   // for each instantiation of stempl

            si = list_Classsym(sl);
#if LOG_EXPAND2
            printf("\t\tsi = '%s'\n", si->Sident);
#endif
            // Compare template_argument_list with instantiation's
            // template_argument_list. Since it is all mangled into the name,
            // we can do this by simply comparing names.
            if (strcmp(tident,si->Sident) == 0)
            {
                param_free(&template_argument_list);
#if LOG_EXPAND2
                printf("\t\tfound existing instantiation '%s', %p\n",si->Sident, si);
#endif
                return si;                      // instantiation is found
            }
        }
    }

    // Instantiation is not found, so create new instantiation
#if LOG_EXPAND2
    printf("\tcreate new instantiation\n");
#endif

    tident = alloca_strdup(tident);     // original may get overwritten

    // Deduce which partial specialization it is
    param_t *ptal2;
    sp = template_class_match(stempl, template_argument_list, &ptal2);

    unsigned structflags = stempl->Stemplate->TMflags;
    si = n2_definestruct(tident,structflags,0,sp,ptal2,0);
    if (ptal2 != template_argument_list)
        si->Sstruct->Spr_arglist = template_argument_list;
#if LOG_EXPAND2
    printf("\tnew instantiation on %p is '%s', %p\n", sp, si->Sident, si);
#endif

    return si;
}

/*****************************************
 * Create symbol table for template expansion.
 * Input:
 *      ptpl    template-parameter-list
 *      ptal    template-argument-list
 */

void template_createsymtab(param_t *ptpl,param_t *ptal)
{   symbol *root;

#define LOG_CREATESYMTAB        0

#if LOG_CREATESYMTAB
    printf("template_createsymtab(%p,%p)\n",ptpl,ptal);
#endif

#ifdef DEBUG
    assert(ptpl->length() == ptal->length());
#endif
    root = NULL;
    for (; ptal; ptal = ptal->Pnext, ptpl = ptpl->Pnext)
    {   symbol *st;

        assert(ptpl);
        if (!ptpl->Pident)
            continue;
#if LOG_CREATESYMTAB
        printf("\tArgument '%s':\n",ptpl->Pident);
#endif
        st = defsy(ptpl->Pident,&root);
        if (ptal->Ptype)                // create a typedef
        {
            st->Stype = type_setdependent(ptal->Ptype);
            st->Sclass = SCtypedef;
#if LOG_CREATESYMTAB
            printf("\t\tType %p %p\n", ptal->Ptype, st->Stype);
            type_print(st->Stype);
#endif
        }
        else if (ptal->Psym)
        {   // Create an alias for the template-template-argument
            Symbol *s;

#if LOG_CREATESYMTAB
            printf("\t\tTemplate '%s'\n", ptal->Psym->Sident);
#endif
            for (s = ptal->Psym; s->Sclass == SCalias; s = ((Aliassym *)s)->Smemalias)
                ;                       // no nested alias's
            ((Aliassym *)st)->Smemalias = s;
            st->Sclass = SCalias;
            st->Stype = type_setdependent(s->Stype);
            st->Stype->Tcount++;
        }
        else                            // create a constant declaration
        {   elem *e = ptal->Pelem;
            tym_t ty;

            if (!e)
            {
                cpperr(EM_templ_arg_unused,ptpl->Pident);       // argument not used
                st->Stype = tsint;
                st->Sclass = SCtypedef;
            }
            else
            {
                elem_debug(e);
#if LOG_CREATESYMTAB
                printf("\t\tElem:\n");
                elem_print(e);
#endif
                st->Sclass = SCconst;
                st->Sflags |= SFLvalue;
                st->Stype = e->ET;
                st->Svalue = el_copytree(e);
                st->Svalue->PEFflags |= PEFdependent;   // arg is value-dependent
            }
        }
#if LOG_CREATESYMTAB
        printf("\n");
#endif
        st->Stype->Tcount++;
    }
    scope_push(root,(scope_fp)findsy,SCTtempsym);
}


/******************************************
 * Remove symbol table created by template_createsymtab().
 */

void template_deletesymtab()
{   symbol *root;

    //dbg_printf("template_deletesymtab()\n");
    assert(scope_end->sctype == SCTtempsym);
    root = (symbol *)scope_pop();
    symbol_free(root);                          // free symbol table
}

/*******************************************
 * If arglist is a match for template function stemp, generate a
 * function based on the template, and return that symbol.
 * Input:
 *      stemp           template function
 *      pl              list of argument types for function
 *      parsebody       if 1, then parse function body (if body is present)
 *                      if 2, then just return matching function template
 *                      if |4, then use symdecl
 *                      if |8, we don't have pl
 *      matchStage
 *      ptali           initial template-argument-list
 *      stagfriend      if instantiate function is to be a friend of this class
 * Returns:
 *      sf              function which is instantiation of stemp
 *      NULL            template is not a match for arglist
 */

symbol *template_matchfunc(symbol *stemp,param_t *pl,int parsebody,
        match_t matchStage, param_t *ptali, symbol *stagfriend)
{
    param_t *ptal = NULL;               // template-argument-list
    symbol *sf = NULL;                  // generated function
    symbol *ses;
    Scope *scsave;
    Classsym *stag = NULL;

#define LOG_MATCHFUNC   0

#if LOG_MATCHFUNC
    dbg_printf("template_matchfunc(stemp = '%s', pl = %p, parsebody = %d, matchState = x%x, ptali = %p)\n",cpp_prettyident(stemp),pl,parsebody,matchStage, ptali);
    if (pl)
    {   printf("pl:\n");
        pl->print_list();
    }
    if (ptali)
    {   printf("ptali:\n");
        ptali->print_list();
    }
#endif

    symbol_debug(stemp);
    assert(stemp->Sclass == SCfunctempl);
    assert(tyfunc(stemp->Stype->Tty));
    assert(stemp->Sfunc);
    type_debug(tsint);

    if (!template_deduce_ptal(NULL, stemp, ptali, NULL, parsebody & 8, pl, &ptal).m)
        goto nomatch;

    if ((parsebody & 3) == 2)
        return stemp;

    /* Determine if template is already instantiated.
     */
    for (sf = stemp; sf; sf = sf->Sfunc->Foversym)
    {
        if (sf->Sclass == SCfunctempl)
            continue;
#if LOG_MATCHFUNC
        printf("\tprevious function '%s' %p\n", sf->Sident, sf);
        printf("\tinstance = %x\n", sf->Sfunc->Fflags & Finstance);
        printf("\timplem   = %x\n", sf->Sflags & SFLimplem);
        printf("\tFtempl   = %p\n", sf->Sfunc->Ftempl);
#endif
        if (!(sf->Sfunc->Fflags & Finstance) ||
            sf->Sfunc->Ftempl != stemp
           )
            continue;

#if 0
        printf("sf->Sfunc->Fptal ***********\n");
        sf->Sfunc->Fptal->print_list();

        printf("ptal ***********\n");
        ptal->print_list();
#endif
        // It's a match if ptal matches sf->Sfunc->Fptal
        if (template_arglst_match(ptal, sf->Sfunc->Fptal))
        {
#if LOG_MATCHFUNC
            printf("\tmatch with previous function\n");
#endif
            break;
        }
    }

    // If operator overloading
    if (stemp->Sfunc->Fflags & Fcast)
    {   type *tret;

        // Look for previous instantiation
        assert(sf == NULL);

        assert(isclassmember(stemp));
        stag = (Classsym *)stemp->Sscope;

        assert(ptali);
        tret = ptali->Ptype;
        assert(tret);

        list_t castlist;
        for (castlist = stag->Sstruct->Scastoverload;
             castlist;
             castlist = list_next(castlist))
        {
            sf = (symbol *)list_ptr(castlist);

            if (sf->Sclass == SCfunctempl)
                continue;
    #if LOG_MATCHFUNC
            printf("\tprevious cast function '%s' %p\n", sf->Sident, sf);
            printf("\tinstance = %x\n", sf->Sfunc->Fflags & Finstance);
            printf("\timplem   = %x\n", sf->Sflags & SFLimplem);
            printf("\tFtempl   = %p\n", sf->Sfunc->Ftempl);
    #endif
            if (!(sf->Sfunc->Fflags & Finstance) ||
                sf->Sfunc->Ftempl != stemp
               )
                continue;
#if 1
            // It's a match if ptali matches sf->Sfunc->Fptal
            if (template_arglst_match(ptali, sf->Sfunc->Fptal))
            {
#if LOG_MATCHFUNC
                printf("\tmatch with previous cast function\n");
#endif
                break;
            }
#else
            if (typematch(tret, sf->Stype->Tnext, 0))
                break;
printf("types don't match\n");
printf("tret: "); type_print(tret);
printf("sf  : "); type_print(sf->Stype->Tnext);
#endif
        }
        if (!castlist)
            sf = NULL;
    }

    if (sf && !(parsebody & 4))
    {   param_free(&ptal);
#if LOG_MATCHFUNC
        printf("\treturning previous match\n");
#endif
        return sf;
    }

    /*  Instantiate the template with the types given in the ptal
     *  argument list.
     */
#if LOG_MATCHFUNC
    dbg_printf("template_matchfunc() instantiate\n");
    ptal->print_list();
#endif

    // Note equivalence of scope setting code to that in
    // template_instantiate_forward().

    int nscopes = 0;

    scsave = scope_end;
    if (!(parsebody & 4))
        Scope::setScopeEnd(scope_find(SCTglobal));

    // Generate list of all the scopes
    list_t scopelist = scope_getList(stemp);
    symbol *ss;

    // Push all the scopes
    for (list_t l = scopelist; l; l = list_next(l))
    {   ss = (symbol *) list_ptr(l);

        if (ss->Sclass == SCstruct)
        {
            if (ss->Sstruct->Stempsym && !(parsebody & 4))
            {
#if LOG_MATCHFUNC
                printf("\tstag '%s' is an instance of template '%s'\n", ss->Sident, ss->Sstruct->Stempsym->Sident);
#endif
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

    /* Look for explicit specialization for stemp
     */
    for (ses = stemp->Sfunc->Fexplicitspec; 1; ses = ses->Sfunc->Fexplicitspec)
    {
        if (!ses)
        {   ses = stemp;
            break;
        }
        if (template_arglst_match(ses->Sfunc->Fptal, ptal))
            break;
    }

    if (ses == stemp)
        template_createsymtab(stemp->Sfunc->Farglist,ptal);     // turn arglist into symbol table
    else
        // Create empty symbol table for explicit specialization
        scope_push(NULL, (scope_fp)findsy, SCTtempsym);

    type_debug(tsint);
    nscopes++;
    token_unget();                      // Put back the current token
    token_setlist(ses->Sfunc->Fbody);   // instantiate function
    stoken();
  { type *dt;
    type *typ_spec;
    char vident[2*IDMAX + 1];
    linkage_t save = linkage;
    int constructor = 0;
    enum SC sc;
    Pstate pstatesave = pstate;
    int levelSave = level;
    unsigned Fflags = 0;

pstate.STclasssym = NULL;
    if (ses->Sfunc->Fsequence)
        pstate.STmaxsequence = ses->Sfunc->Fsequence;
    linkage = LINK_CPP;                 /* template functions are always C++ */

    if (tok.TKval == TKexplicit)
    {   Fflags |= Fexplicit;
        stoken();
    }

    if (isclassmember(stemp))
        stag = (Classsym *)stemp->Sscope;

    if (ses->Sclass == SCftexpspec)
        pstate.STexplicitSpecialization++;
    if (stag && !(parsebody & 4))
    {
        enum_TK tk;
        stag = (Classsym *)stemp->Sscope;

        if (tok.TKval == TKident && template_classname(tok.TKid, stag))
        {
            tk = stoken();
            token_unget();
            token_setident(stag->Sstruct->Stempsym ? stag->Sstruct->Stempsym->Sident : stag->Sident);
            if (tk == TKlpar)
            {
                //printf("constructor\n");
                constructor = 1;
                typ_spec = tsint;
                tsint->Tcount++;
                pstate.STclasssym = stag;
                dt = declar_fix(typ_spec, vident);
                pstate.STclasssym = pstatesave.STclasssym;
                type_free(dt->Tnext);
                dt->Tnext = newpointer(stag->Stype);
                dt->Tnext->Tcount++;
                strcpy(vident, cpp_name_ct);
                sc = SCinline;
            }
        }
    }
    if (!constructor)
    {
        Classsym *save = pstate.STclasssym;
        pstate.STclasssym = stag;

        if (Fflags & Fexplicit)
            synerr(EM_explicit);        // explicit is only for constructors

        if (!(declaration_specifier(&typ_spec,&sc,NULL) & 2))
            sc = SCglobal;
        if (config.flags2 & CFG2comdat && (sc == SCglobal /*|| sc == SCinline*/))
            sc = SCcomdat;
        pstate.STdeferDefaultArg++;
        dt = declar_fix(typ_spec,vident);
        pstate.STdeferDefaultArg--;
        if (gdeclar.constructor)
        {
            type_free(dt->Tnext);
            dt->Tnext = newpointer(gdeclar.class_sym->Stype);
            dt->Tnext->Tcount++;
        }
        else
            dt->Tflags |= TFfuncret;
        pstate.STclasssym = save;
    }
    if (ses->Sclass == SCftexpspec)
    {   pstate.STexplicitSpecialization--;
        param_free(&gdeclar.ptal);
    }

    if (stag && !(parsebody & 4))
    {
        //printf("it's a class member '%s'\n", vident);
        sf = symbol_name(vident, sc, dt);
        type_free(dt);
        sf->Sflags |= stemp->Sflags & SFLpmask;
        sf->Sscope = stag;
        // BUG: should issue error if dt is not a function type
        sf->Sfunc->Fptal = ptal;
        ptal = NULL;
        n2_addfunctoclass(stag, sf, 1);

        if (constructor)
        {
            sf->Sfunc->Fflags |= Fctor;
        }
        if (stemp->Sfunc->Fflags & Fcast)
        {
            sf->Sfunc->Fflags |= Fcast;
            list_append(&stag->Sstruct->Scastoverload, sf);
        }
        pstate.STclasssym = stag;
    }
#if 1
    else if (sf)
    {
        //printf("dt:\n");
        //type_print(dt);
        //printf("sf->Stype:\n");
        //type_print(sf->Stype);

        // Copy Pident's from dt to sf->Stype
        if (tyfunc(dt->Tty) && tyfunc(sf->Stype->Tty))
        {
            param_t *p1 = dt->Tparamtypes;
            param_t *p2 = sf->Stype->Tparamtypes;

            while (p1 && p2)
            {
                if (p1->Pident && !p2->Pident)
                {
                    p2->Pident = p1->Pident;
                    p1->Pident = NULL;
                }
                p1 = p1->Pnext;
                p2 = p2->Pnext;
            }
        }

        type_setty(&dt,sf->Stype->Tty | (dt->Tty & mTYMOD));
        type_setmangle(&dt,type_mangle(sf->Stype));
        if (!typematch(sf->Stype, dt, 4|1))
        {
            type_free(dt);
            sf = NULL;
        }
        else
        {
            type_free(dt);
            if (sf->Sscope && sf->Sscope->Sclass == SCstruct)
                pstate.STclasssym = (Classsym *)sf->Sscope;
        }
    }
#endif
    else if (tyfunc(dt->Tty))
    {
        //printf("it's a symdecl\n");
#if 1
        sf = symbol_name(vident, sc, dt);
        type_free(dt);
        sf->Sfunc->Fflags |= Foverload | Fnotparent | Ftypesafe;

        // Append to list of overloaded functions
        {   symbol **ps;

            for (ps = &stemp; *ps; ps = &(*ps)->Sfunc->Foversym)
                ;
            *ps = sf;
        }
        sf->Sscope = stemp->Sscope;
        ph_add_global_symdef(sf, SCTglobal);
#else
        sf = symdecl(vident,dt,sc);
#endif
        if (sf && sf->Sscope && sf->Sscope->Sclass == SCstruct)
            pstate.STclasssym = (Classsym *)sf->Sscope;
    }
    if (sf)
    {
        symbol_func(sf);

        // If already generated by template, don't generate it again
        if (sf->Sflags & SFLimplem && sf->Sfunc->Fflags & Finstance ||
            config.flags4 & CFG4notempexp)
            parsebody = 0;

        sf->Sfunc->Fflags |= Fflags | Finstance; // flag generated by template
        sf->Sfunc->Fflags |= stemp->Sfunc->Fflags & Fstatic;
        sf->Sfunc->Ftempl = stemp;              // remember which template created this
        if (ptal)
        {
            param_free(&sf->Sfunc->Fptal);
            sf->Sfunc->Fptal = ptal;
            ptal = NULL;
        }
        parsebody &= 3;
        if (!pstate.STclasssym)
            level = 0;                          // kludge, should get rid of level
        if (stagfriend)
        {
            list_append(&sf->Sfunc->Fclassfriends,stagfriend);
            list_append(&stagfriend->Sstruct->Sfriendfuncs,sf);
        }
        if (funcdecl(sf,sc,parsebody | 2 | 4,&gdeclar)) // if function body was present
        {   if (!parsebody)                     /* but we didn't parse it       */
            {   token_poplist();                /* dump tokens for body         */
                stoken();                       /* undo previous token_unget()  */
            }

            if (stag)
                list_append(&stag->Sstruct->Sinlinefuncs, sf);
        }
        else if (tok.TKval == TKsemi)
        {
            stoken();                           // skip terminating ';'
            // If we find the template function body in the future,
            // remember to instantiate this function
            list_append(&stemp->Sfunc->Ffwdrefinstances, sf);
        }
    }
    else
    {
        if (tok.TKval != TKsemi)
            token_poplist();                    // dump tokens for body
        stoken();                               // undo previous token_unget()
    }
    linkage = save;
    type_free(typ_spec);
    level = levelSave;
    pstate.STclasssym = pstatesave.STclasssym;
    pstate.STmaxsequence = pstatesave.STmaxsequence;
  }

    // Unwind scope back to global
    scope_unwind(nscopes);

    // Reset scope chain back to what it was before template instantiation
    Scope::setScopeEnd(scsave);

    param_free(&ptal);
#if LOG_MATCHFUNC
    dbg_printf("template_matchfunc() match = '%s' %p\n", sf ? sf->Sident : "null", sf);
#endif
    return sf;

nomatch:
#if LOG_MATCHFUNC
    dbg_printf("template_matchfunc() nomatch\n");
#endif
    param_free(&ptal);
    return NULL;
}

/********************************************
 * Given an explicit argument list and a type, find the matching
 * SCfunctempl and instantiate it.
 * Input:
 *      T foo<arg1,arg2>(U a1, V a2)
 * Returns:
 *      instantiated function symbol
 *      NULL if no match
 */

symbol *template_matchfunctempl(
        symbol *sfunc,          // foo function symbol
        param_t *ptali,         // <arg1,arg2>
        type *tf,               // T ()(U,V)
        symbol *stagfriend,     // if !NULL, the generated function is to be a friend
        int flags)              // for cpp_lookformatch()
{
    /* Convert tf parameter list into arglist suitable for cpp_lookformatch()
     */

    //printf("template_matchfunctempl(sfunc = '%s')\n", sfunc->Sident);
    //type_print(tf);
    list_t arglist = NULL;
    elem *e;
    param_t *pt;

    if (!tyfunc(tf->Tty))
        return NULL;

    for (pt = tf->Tparamtypes; pt; pt = pt->Pnext)
    {
        e = el_longt(pt->Ptype, 0);
        e->Eoper = OPvar;               // BUG: should be some OPplaceholder
        list_append(&arglist, e);
    }

    symbol *s;
    s = cpp_lookformatch(sfunc, NULL, arglist, NULL, NULL, NULL, ptali,
        flags, NULL, NULL, stagfriend);

    list_free(&arglist, (list_free_fp)el_free);


    if (s && !(flags & 4))
    {
        if (!typematch(s->Stype, tf, 0))
            s = NULL;
    }
    return s;
}

/************************************
 * Input:
 *      ptyTemplate     pointer to the type of a template function argument
 *                      (which might need expansion)
 *      ptpl            template-parameter-list
 *      ptal            template-argument-list
 *      ptyActual       type of actual argument
 *      ptyFormal       parameter type for existing instance of template
 * Returns:
 *      TRUE if the argument type would be the same type after expansion
 */

int template_match_expanded_type(
        type *ptyTemplate,
        param_t *ptpl,
        param_t *ptal,
        type *ptyActual,
        type *ptyFormal )
{
    tym_t       tymTemplate;
    tym_t       tymFormal;
    param_t *p;
    int iResult;

    // Outbuffer buf;
    //dbg_printf("template_match_expanded_type()\n");
    //dbg_printf("ptyTemplate = '%s'\n",type_tostring(&buf,ptyTemplate));
    //dbg_printf("ptyFormal   = '%s'\n",type_tostring(&buf,ptyFormal));
    //dbg_printf("ptyActual   = '%s'\n",type_tostring(&buf,ptyActual));

    Match m;
    m = template_matchtype(ptyTemplate, ptyActual, NULL, ptpl, ptal, 0);
    if (m.m < TMATCHexact)
        return 0;

    while (ptyTemplate && ptyFormal) {
        tymTemplate = ptyTemplate->Tty;
        tymFormal = ptyFormal->Tty;

        if (tybasic(tymTemplate) == TYident)
        {
            p = ptal->search(ptyTemplate->Tident);
            assert(p);                  // it must be there

            if (p->Ptype) {

                // Add the const and volatile modifiers back onto the type
                // and then call typematch on it to insure that the types
                // really do match exactly.

                ptyTemplate = p->Ptype;
                ptyTemplate->Tcount++;
                type_setcv(&ptyTemplate,
                    tymTemplate & (mTYconst | mTYvolatile));
                iResult = typematch( ptyTemplate, ptyFormal, 0 );
                type_free( ptyTemplate );
                return(iResult);
            }
        }
        if (tybasic(tymTemplate) == TYtemplate &&
            tybasic(tymFormal) == TYstruct)
        {
            Symbol *stemplate = ((typetemp_t *)ptyTemplate)->Tsym;
            Classsym *stag = ptyFormal->Ttag;

            for (; stemplate; stemplate = stemplate->Stemplate->TMpartial)
            {
                if (stag->Sstruct->Stempsym == stemplate)
                    return 1;
            }
            return 0;
        }
        if (tymTemplate != tymFormal)
                return(0);
        if (!ptyTemplate->Tnext || !ptyFormal->Tnext)
                return(typematch(ptyTemplate, ptyFormal, 0));
        ptyTemplate = ptyTemplate->Tnext;
        ptyFormal = ptyFormal->Tnext;
    }
    assert(0);
    return 0;
}

/*******************************
 * Helper function to see if any template arguments in type t
 * match any of the arguments in temp_arglist.
 * Mark any that are found.
 */

STATIC void template_chkarg(type *t,param_t *temp_arglist, char *temp_used)
{   param_t *p;
    tym_t ty;
    char *id;
    int i;

    //dbg_printf("template_chkarg(%p)\n", t);
    while (t)
    {
        type_debug(t);
        ty = tybasic(t->Tty);
        if (ty == TYident)              /* if template argument */
        {
            id = t->Tident;

        L1:
            //dbg_printf("\tfunction type '%s'\n",id);
            i = 0;
            for (p = temp_arglist; p; p = p->Pnext)
            {   //dbg_printf("\t\ttemplate arg '%s'\n",p->Pident);
                if (strcmp(id,p->Pident) == 0)
                {   temp_used[i] = 1;   // mark as used
                    return;
                }
                i++;
            }
        }
        else if (tyfunc(ty) || ty == TYtemplate)
        {
            for (p = t->Tparamtypes; p; p = p->Pnext)
            {
                if (p->Ptype)
                {
                    if (p->Ptype != t)  // prevent infinite recursion (can this happen?)
                        template_chkarg(p->Ptype, temp_arglist, temp_used);
                }
                else if (p->Pelem)
                {
                    elem *e = p->Pelem;

                    if (e->Eoper == OPvar)
                    {
                        id = e->EV.sp.Vsym->Sident;
                        goto L1;
                    }
                    // BUG: should handle expressions
                }
            }
        }
        else if (ty == TYmemptr &&
                 tybasic(t->Ttag->Stype->Tty) == TYident)
        {
            // Continue on below to check the rest of the pointer to
            // member type
            template_chkarg( t->Ttag->Stype, temp_arglist, temp_used );
        }
        t = t->Tnext;
    }
}

/*********************************
 * Return !=0 if type depends on a TYident or a template.
 */

STATIC int template_typedependent( type *t )
{
    tym_t ty;
    param_t *p;

    while (t)
    {
        type_debug(t);
        ty = tybasic(t->Tty);
        if (ty == TYident)              /* if template argument */
        {
            return(TRUE);
        }
        else if (ty == TYtemplate)
            return TRUE;
        else if (tyfunc(ty) /*|| ty == TYtemplate*/)
        {
            for (p = t->Tparamtypes; p; p = p->Pnext)
            {
                if (!p->Ptype && p->Pelem && p->Pelem->Eoper == OPvar)
                    return TRUE;
                if (template_typedependent(p->Ptype))
                    return TRUE;
            }
        }
        else if (ty == TYmemptr && tybasic(t->Ttag->Stype->Tty) == TYident)
            return TRUE;
        else if (ty == TYarray)
        {
            if (t->Tflags & TFvla && t->Tel)
                return TRUE;
        }
        t = t->Tnext;
    }
    return(FALSE);
}

/******************************************
 * CPP98 14.5.4-9: "A partially specialized non-type argument expression shall not
 * involve a template parameter of the partial specialization except when
 * the argument expression is a simple identifier."
 * Returns:
 *      0       success
 *      !=0     error
 */

STATIC int template_elemdependent(elem *e, param_t *ptpl)
{   int error = 0;

    while (EOP(e))
    {
        if (EBIN(e))
            error |= template_elemdependent(e->E2, ptpl);
        e = e->E1;
    }
    if (e->Eoper == OPvar || e->Eoper == OPrelconst)
    {
        symbol *s = e->EV.sp.Vsym;
        if (ptpl->search(s->Sident))
        {
            cpperr(EM_not_simple_id, s->Sident);
            error = 1;
            e->Eoper = OPconst;
        }
    }
    return error;
}

STATIC int template_typedependent(type *t, param_t *ptpl)
{
    // BUG: this code should be more like template_tyident(), which
    // handles a lot more cases.

    type_debug(t);
    for (; t; t = t->Tnext)
    {
        switch (tybasic(t->Tty))
        {
            case TYident:
                if (ptpl->search(t->Tident))
                    cpperr(EM_dependent_specialization, t->Tident);
                return 1;

            case TYarray:
                if (t->Tflags & TFvla)
                {
                    if (template_elemdependent(t->Tel, ptpl))
                        return 1;
                }
                break;

            default:
                if (tyfunc(t->Tty))
                {   param_t *p;

                    for (p = t->Tparamtypes; p; p = p->Pnext)
                    {
                        if (template_typedependent(p->Ptype, ptpl))
                            return 1;
                    }
                }
                break;
        }
    }
    return 0;
}

/************************************
 * Perform template argument deduction per ARM 14.10.2.
 * Input:
 *      tp      type of template function parameter (P)
 *      te      type of actual argument (A)
 *      ee      the actual argument, if non-NULL
 *      ptpl    template-parameter-list of template function (T)
 *      ptal    matching template-argument-list to fill in
 *      flags   |1: determining partial ordering
 *              |2: deducing template arguments from a type
 *              |4: do not shave off TYref's from parameter
 *              |8: deducing class template arguments from a type
 * Output:
 *      ptpl->Ptype's are filled in with the types to use as arguments
 *      to the template.
 * Returns:
 *      !=0     match level (templated arguments successfully deduced)
 *      0       nomatch
 */

Match template_matchtype(type *tp,type *te,elem *ee,param_t *ptpl, param_t *ptal,
        int flags)
{   tym_t tpty, tety;
    param_t *p;
    char first;
    char pisref;
    match_t match;
    int adjustment = 0;
    type *testart = te;
    Match m;

    #define LOG_MATCHTYPE       0

    elem e = {0};
    e.Eoper = OPvar;            /* kludge for parameter         */

#if DEBUG
    e.id = IDelem;
#endif

#if LOG_MATCHTYPE
    dbg_printf("\n\n\ntemplate_matchtype(tp=%p, te=%p, flags=x%x)================================\n", tp, te, flags);
    printf("*** tp type of template function parameter\n");
    if (tp) type_print(tp);
    printf("*** te type of actual function argument\n");
    if (te) type_print(te);
    printf("*** ee actual argument\n");
    if (ee) elem_print(ee);
    printf("***\n");

    printf("**** ptpl template parameter list ***********\n");
    ptpl->print_list();
    printf("**** ptal template argument list ***********\n");
    ptal->print_list();
#endif

    if (!template_typedependent(tp) && tp && te)
    {
#if LOG_MATCHTYPE
        printf("\tnot template_typedependent\n");
#endif
        e.ET = te;
        if (!ee)
            ee = &e;
        if (flags & 4)
        {
            // Don't match references
            if (tyref(tp->Tty) != tyref(te->Tty))
                goto nomatch;
        }
        match = cpp_matchtypes(ee, tp);
        goto match;
    }

    pisref = 0;
    first = 1;
    while (1)
    {
        if (tp == te)
        {   match = TMATCHexact;
            goto match;
        }
        if (!tp || !te)
            goto nomatch;
        type_debug(tp);
        type_debug(te);

        /* ignore name mangling */
#if TX86
        tpty = tp->Tty & ~(mTYexport | mTYimport);
        tety = te->Tty & ~(mTYexport | mTYimport);
#else
        tpty = tp->Tty & ~mTYMAN;
        tety = te->Tty & ~mTYMAN;
#endif
        if (tyref(tety))
            // cv-qualified references are ignored
            tety &= ~(mTYconst | mTYvolatile);

        if (tyref(tpty) && !(flags & 4))
        {   pisref = 1;
            tp = tp->Tnext;

            if (first)
                m.ref = 1;

            if (first &&
                (tybasic(tp->Tty) == TYarray || tybasic(tp->Tty) == TYident) &&
                typtr(tety) && te->Talternate)
            {
#if 0
                printf("te:\n");
                type_print(te);
                printf("te->Talternate:\n");
                type_print(te->Talternate);
#endif
                te = te->Talternate;
                type_debug(te);
            }
#if TX86
            if (tyref(tety))
                te = te->Tnext;
#endif
            // Adding cv to a reference type is a no match:
            //  f(t&) cannot take a (const t) as an argument
            if (tybasic(tp->Tty) != TYident &&
                te->Tty & ~tp->Tty & (mTYconst | mTYvolatile))
                goto nomatch;

            continue;
        }

        // This was causing problems in producing the ident type
        // for example:
        //      template <class T> T f(T t) { return(t); }
        //      void g(short &sr)
        //      {
        //          f(sr);      // Would cause f(short &) to expand as f(short)
        //      }

        if (tybasic(tpty) != TYident && tyref(tety) && !(flags & 4))
        {   te = te->Tnext;
            continue;
        }

        if (first)
        {
            m.toplevelcv = tpty & (mTYconst | mTYvolatile);

            // Adding cv is a tie-breaker
            if (tpty & ~tety & (mTYconst | mTYvolatile))
                adjustment = 1;

            //tpty &= ~(mTYconst | mTYvolatile);
            if (!pisref && !(flags & (2|8)))
                tety &= ~(mTYconst | mTYvolatile);
        }

        /* If type is a template-argument       */
        if (tybasic(tpty) == TYident)
        {
            tym_t cv;           // cv qualification to use for deduced argument

            // If we're determining partial ordering of function templates,
            // then an implicit conversion to const or volatile is
            // a no-match
            if (1 || flags & 1)
            {
                if ((tpty & ~tety) & (mTYconst | mTYvolatile))
                {
                    adjustment = 1;
                    //goto nomatch;
                }
            }

            if (tp->Tnext)
            {
                // Try to infer type from cls<V>::foo
                // doesn't always work
                pstate.STnoexpand++;
                tp = template_tyident(tp, ptal, ptpl, FALSE);
                pstate.STnoexpand--;
                if (!tp)
                    goto nomatch;
                if (tybasic(tp->Tty) == TYident)
                {
                    /* This can happen for things like the U on:
                     *  template<class T> void f1(typename T::U v, T t);
                     * We can't deduce what it is at the moment, so just
                     * assume it matches.
                     */
                    match = TMATCHexact;
                    goto match;
                }
                continue;
            }

            p = ptal->search(tp->Tident);
#ifdef DEBUG
            if (!p)
            {   printf("tp->Tident = '%s'\n", tp->Tident);
                type_print(tp);
            }
#endif
            assert(p);                          // it must be there
            //cv = (te->Tty & ~tpty);
            cv = (tety & ~tpty);
            //cv = first ? 0 : (te->Tty & ~tpty);
            if (p->Ptype)
            {   type *t;
                int m;

                // Mimic the code on the else branch.  The type
                // should be constructed the same way and then compared.
                te->Tcount++;
                t = te;
                type_setcv(&t, cv);
                if (first || tybasic(t->Tty) == TYstruct)
                {
                    e.ET = t;
                    if (!ee || !first || tybasic(p->Ptype->Tty) == TYenum)
                        ee = &e;
#if 0
                    else if (first && ee)
                    {
                        type_print(t);
                        type_print(ee->ET);
                        type_print(p->Ptype);
                        elem_print(ee);
                    }
#endif
                    if (flags & 4 && first && !typematch(t, p->Ptype, 0))
                        goto nomatch;

                    match = cpp_matchtypes(ee, p->Ptype);

                    if (!(p->Pflags & PFexplicit) &&
                        match <= TMATCHpromotions)
                        match = TMATCHnomatch;

//printf("match = x%x\n", match);
//type_print(te);
//type_print(t);
//type_print(p->Ptype);
                    if (match != TMATCHnomatch && match <= TMATCHuserdef)
                        adjustment *= 2;        // match behavior in cpp_matchtypes()
                }
#if 1
                else if (t->Tty == TYident && !(flags & 1))
                {
                    match = TMATCHexact;
                }
#endif
                else
                {
                    match = typematch(t,p->Ptype,0);
                    match = match ? TMATCHexact : TMATCHnomatch;
                }
#if 0
                printf(" previous match = x%02x\n", match);
                printf("p->Ptype:\n");
                type_print(p->Ptype);
                printf("t:\n");
                type_print(t);
#endif
                type_free(t);
                goto match;
            }
            else
            {
                //printf(" no previous match\n");
                p->Ptype = te;
                if (te->Talternate && typtr(te->Tty))
                {
                    p->Ptype = type_copy(te);
                    p->Ptype->Talternate = NULL;
                }
                p->Ptype->Tcount++;
                type_setcv(&p->Ptype,cv);

                //printf("setcv %x: ", cv); type_print(p->Ptype);
                match = TMATCHexact;
                goto match;
            }
        }
        else if (tybasic(tpty) == TYtemplate)
        {
            symbol *st;
            param_t *pp;
            param_t *pe;
            symbol *sprimary;

            sprimary = ((typetemp_t *)tp)->Tsym;
            symbol_debug(sprimary);

            pp = tp->Tparamtypes;               // template-parameter-list

            if (tybasic(tety) == TYtemplate)
            {
                /* The idea is to detect partial ordering dummy template names by
                 * checking to see if they are forward declarations.
                 * Should probably use a separate bit for that.
                 */
                {   symbol *se = ((typetemp_t *)te)->Tsym;
                    if (flags & 1 &&
                        strcmp(sprimary->Sident, se->Sident) &&
                        !(sprimary->Stype->Tflags & TFforward) &&
                        (se->Stype->Tflags & TFforward))
                        goto nomatch;
//else printf("%s %s %x\n", sprimary->Sident, se->Sident, flags);
else if (flags & 8 && strcmp(sprimary->Sident, se->Sident))
    goto nomatch;
                }

                pe = te->Tparamtypes;

              Lplmatch:
                for (;pp; pp = pp->Pnext)
                {   if (!pe)
                        goto nomatch;
#if LOG_MATCHTYPE
                    printf("\n\n1Comparing template-parameter with template-argument, flags=x%x\n", flags);
                    printf("template-parameter:\n\t");
                    pp->print();
                    printf("template-argument:\n\t");
                    pe->print();
                    printf("\n\n");
#endif
                    Match m;
                    m = template_matchtype(pp->Ptype,pe->Ptype,NULL,ptpl,ptal,flags | 2);
                    if (m.m < TMATCHexact)
                        goto nomatch;
                    if (!el_match(pp->Pelem,pe->Pelem))
                    {
                        elem *epar = pp->Pelem;
                        elem *earg = pe->Pelem;

                        if (epar->Eoper == OPvar && !EOP(earg))
                        {
                            char *id = epar->EV.sp.Vsym->Sident;
                            p = ptal->search(id);
                            assert(p);
                            if (p->Pelem)
                            {
                                if (!el_match(p->Pelem, earg))
                                    goto nomatch;
                            }
                            else
                            {
                                p->Pelem = el_copytree(earg);
                            }
                        }
                        else
                            goto nomatch;
                    }

                    pe = pe->Pnext;
                }
                if (!pe)
                {   match = TMATCHexact;
                    goto match;
                }
                goto nomatch;
            }
            else if (tybasic(tety) == TYstruct && sprimary->Stemplate->TMflags2 & 1)
            {
                // The actual argument is a struct and the formal is a
                // template-template-parameter.
#if LOG_MATCHTYPE
                printf("matching struct against template-template-parameter\n");
#endif
                st = te->Ttag;
                if (!st->Sstruct->Stempsym)
                    goto nomatch;

                p = ptal->search(sprimary->Sident);
                assert(p);

                match = TMATCHexact;
                if (p->Psym == st->Sstruct->Stempsym)
                    goto match;
                if (p->Psym)
                    goto nomatch;
                p->Psym = st->Sstruct->Stempsym;

                // Run down argument list of st
                pe = (st->Sstruct->Stempsym == st->Sstruct->Stempsym->Stemplate->TMprimary)
                        ? st->Sstruct->Sarglist
                        : st->Sstruct->Spr_arglist;     // si's template-argument-list

                goto Lplmatch;
            }
            else if (tybasic(tety) == TYstruct)
            {
                // The actual argument is a struct and the formal is a
                // template struct.
                // A match if one of the template instances is the same as the
                // struct or is a base of the struct.
#if LOG_MATCHTYPE
                printf("matching struct against template\n");
#endif
                symlist_t sl;
                Classsym *si;
                int adj;
                symbol *sp;

                st = te->Ttag;

                match = TMATCHexact;
                adj = 0;
                sprimary = ((typetemp_t *)tp)->Tsym;
                for (sp = sprimary; sp; sp = sp->Stemplate->TMpartial)
                {
                    for (sl = sp->Stemplate->TMinstances; sl; sl = list_next(sl))
                    {   int conv;

                        si = (Classsym *)list_symbol(sl);
                        if (si == st)
                            goto L27;
                        conv = c1isbaseofc2(NULL, si, st);
                        if (conv)
                        {
                            match = TMATCHstandard;
#if TX86
                            adj += (conv >> 8);
#else
                            adj += 0x100;
#endif
                            goto L27;
                        }
                    }
                }
            L27:
                match = match - adj;

                if (!sl || match == TMATCHnomatch)
                    goto nomatch;

                pe = (sp == sprimary)
                        ? si->Sstruct->Sarglist
                        : si->Sstruct->Spr_arglist;     // si's template-argument-list

                for (;pp; pp = pp->Pnext)
                {   if (!pe)
                        goto nomatch;
#if LOG_MATCHTYPE
                    printf("\n\n2Comparing template-parameter with template-argument, flags=x%x\n", flags);
                    printf("template-parameter:\n\t");
                    pp->print();
                    printf("template-argument:\n\t");
                    pe->print();
                    printf("\n\n");
#endif
                    Match m;
                    m = template_matchtype(pp->Ptype,pe->Ptype,NULL,ptpl,ptal,flags | 2);
                    if (m.m < TMATCHexact)
                        goto nomatch;

                    if (!el_match(pp->Pelem,pe->Pelem))
                    {
                        elem *epar = pp->Pelem;
                        elem *earg = pe->Pelem;

                        if (!EOP(earg) && earg->Eoper != OPvar)
                        {
                            if (epar->Eoper == OPvar)
                            {
                                char *id = epar->EV.sp.Vsym->Sident;
                                p = ptal->search(id);
                                assert(p);
                                if (p->Pelem)
                                {
                                    if (!el_match(p->Pelem, earg))
                                        goto nomatch;
                                }
                                else
                                {
                                    elem *e = el_copytree(earg);
                                    e = poptelem(typechk(e, epar->ET));
                                    p->Pelem = e;
                                }
                            }
                            else if (tybasic(epar->ET->Tty) == TYident ||
                                     EOP(epar))
                            {
                                elem *e = el_copytree(epar);

                                if (tybasic(e->ET->Tty) == TYident)
                                {   type *t = e->ET;
                                    e->ET = template_tyident(t, ptal, ptpl, 0);
                                    type_free(t);
                                }

                                e = template_resolve_idents(e, ptal);
                                e = poptelem(e);
                                if (!el_match(e, earg))
                                {   el_free(e);
                                    goto nomatch;
                                }
                                el_free(e);
                            }
                            else
                                goto nomatch;
                        }
                        else
                            goto nomatch;
                    }
                    else if (!pp->Pelem && pp->Psym && pe->Psym)
                    {
                        p = ptal->search(pp->Psym->Sident);
                        if (p)
                        {
                            if (p->Psym && p->Psym != pe->Psym)
                                goto nomatch;
                            p->Psym = pe->Psym;
                        }
                    }
                    pe = pe->Pnext;
                }
                if (!pe)
                    goto match;
            }
            goto nomatch;
        }
        else if (tety == tpty)
        {
            if (tybasic(tpty) == TYarray)
            {
                if (!(tp->Tflags & TFvla) && !(te->Tflags & TFvla))
                {
                    if (tp->Tdim != te->Tdim)
                        goto nomatch;
                }
                else if (tp->Tflags & TFvla && !(te->Tflags & TFvla))
                {
                    elem *epar = tp->Tel;

                    if (epar && epar->Eoper == OPvar)
                    {
                        elem *earg = el_longt(epar->ET, te->Tdim);
                        char *id = epar->EV.sp.Vsym->Sident;
                        p = ptal->search(id);
                        assert(p);
                        if (p->Pelem)
                        {
                            if (!el_match(p->Pelem, earg))
                            {   el_free(earg);
                                goto nomatch;
                            }
                            el_free(earg);
                            //printf("\tp '%s' already filled in with %p\n", p->Pident, p->Pelem);
                        }
                        else
                        {
                            p->Pelem = earg;
                            //printf("\tp '%s' filled in with %p\n", p->Pident, p->Pelem);
                        }
                    }
                    else
                        // BUG: more vla cases to consider
                        goto nomatch;
                }
                else
                    goto nomatch;
            }
samety:
            if (tety == TYmemptr &&
                tybasic(tp->Ttag->Stype->Tty) == TYident)
            {
                Match m;
                m = template_matchtype(tp->Ttag->Stype,te->Ttag->Stype,NULL,ptpl,ptal,flags);
                if (m.m < TMATCHexact)
                    goto nomatch;
            }

            if (tyfunc(tety))
            {
                /* 14.8.2.4-16xa
                 * A template-argument can be deduced from a pointer to function
                 * or pointer to member function argument if the set of
                 * overloaded functions does not contain function templates and
                 * at most one of a set of overloaded functions provides a
                 * unique match.
                 */
                if (ee && ee->Eoper == OPrelconst && ee->ET == testart &&
                    typtr(testart->Tty) && tyfunc(testart->Tnext->Tty) &&
                    testart->Tnext == te &&
                    !(ee->EV.sp.Vsym->Sfunc->Fflags & Finstance)
                   )
                {   Symbol *s = ee->EV.sp.Vsym;
                    Symbol *smatch = NULL;
                    int ntemplates = 0;

//printf("s = %p, '%s'\n", s, s->Sident);
                    if (s->Sclass == SCfuncalias)
                        s = s->Sfunc->Falias;
                    for (Symbol *so = s; s; s = s->Sfunc->Foversym)
                    {   Symbol *sf = s;

                        if (sf->Sclass == SCfuncalias)
                            sf = sf->Sfunc->Falias;
                        if (sf->Sfunc->Fflags & Finstance)
                            continue;
#if 0
                        if (sf->Sclass == SCfunctempl && ++ntemplates > 1)
                        {   // More than one template; disallow
#if LOG_MATCHTYPE
                            printf("\tno match from argument that is a pointer to a template function\n");
#endif
                            goto nomatch;
                        }
#endif
                        param_t *pp = tp->Tparamtypes;
                        param_t *pe = sf->Stype->Tparamtypes;
                        param_t *ptal2 = ptpl->createTal(ptal);

                        for (;pp; pp = pp->Pnext)
                        {   if (!pe)
                                goto Lnext;

                            Match m;
                            m = template_matchtype(pp->Ptype,pe->Ptype,NULL,ptpl,ptal2,flags);
                            if (m.m < TMATCHexact)
                                goto Lnext;

                            pe = pe->Pnext;
                        }
                        if (pe)
                            goto Lnext;
                        if (smatch)             // if more than one match
                        {   param_free(&ptal2);
#if LOG_MATCHTYPE
                            printf("\tmore than one function match for '%s'\n", smatch->Sident);
#endif
                            goto nomatch;
                        }
                        smatch = sf;
                     Lnext:
                        param_free(&ptal2);
                    }
                    if (!smatch)
                        goto nomatch;
                    te = smatch->Stype;
                }

                if (!ee && (tp->Tflags & TFfixed) != (te->Tflags & TFfixed))
                    goto nomatch;

                param_t *pp = tp->Tparamtypes;
                param_t *pe = te->Tparamtypes;

                for (;pp; pp = pp->Pnext)
                {   if (!pe)
                        goto nomatch;

                    Match m;
                    m = template_matchtype(pp->Ptype,pe->Ptype,NULL,ptpl,ptal,flags);
                    if (m.m < TMATCHexact)
                        goto nomatch;

                    pe = pe->Pnext;
                }
                if (pe)
                    goto nomatch;
            }
            else if (!te->Tnext && !tp->Tnext)
            {
                e.ET = te;
                match = cpp_matchtypes(&e,tp);
                goto match;
            }
        }
        else if (typtr(tety) && typtr(tpty))
        {
            // Allow addition of const/volatile in template type arguments
            tym_t ten = te->Tnext->Tty & (mTYconst | mTYvolatile);
            tym_t tpn = tp->Tnext->Tty & (mTYconst | mTYvolatile);

            if (ten & ~tpn)
                goto nomatch;

#if TX86
            // Regard certain implicit pointer conversions as matches
            tety = tybasic(tety);
            tpty = tybasic(tpty);
            if (first && tety == tpty)
                goto Lcont;
            if (tpty == TYnptr && tety == TYsptr && !(config.wflags & WFssneds))
                goto Lcont;
            if (tpty == TYfptr && (tety == TYnptr || tety == TYsptr || tety == TYcptr))
                goto Lcont;
            goto nomatch;
#endif
        }
        else if (tybasic(tety) == TYmemptr && tybasic(tpty) == TYmemptr)
        {
            // Allow addition of const/volatile in template type arguments
            tym_t t1n = te->Tnext->Tty & (mTYconst | mTYvolatile);
            tym_t t2n = tp->Tnext->Tty & (mTYconst | mTYvolatile);

            if (t1n & ~t2n)
                goto nomatch;
            goto samety;
        }
        else if (!te->Tnext)
        {
            if (tybasic(tpty) == TYmemptr &&
                tybasic(tp->Ttag->Stype->Tty) == TYident)
            {
                p = ptal->search(tp->Ttag->Sident);
                assert(p);
                if (p->Ptype && !type_struct(p->Ptype))
                    goto nomatch;
            }

            // No match for T& with T
            if (flags & 4 && tyref(tpty) && !tyref(tety))
                goto nomatch;

            pstate.STnoexpand++;
            tp = template_tyident(tp, ptal, ptpl, FALSE);
            pstate.STnoexpand--;
            if (!tp)
                goto nomatch;

            e.ET = te;
            if (!ee || !first)
                ee = &e;
            match = cpp_matchtypes(ee, tp);
            goto match;
        }
        else
        {
            goto nomatch;
        }

    Lcont:
        first = 0;
        te = te->Tnext;
        tp = tp->Tnext;
    }

match:
    if (match == TMATCHnomatch)
        goto nomatch;
#if LOG_MATCHTYPE
    printf("\ttemplate_matchtype() match x%x, adjustment = %d\n", match - adjustment, adjustment);
#endif
    m.m = match - adjustment;
    return m;

nomatch:
#if LOG_MATCHTYPE
    printf("\ttemplate_matchtype() no match\n");
#endif
    m.m = TMATCHnomatch;
    return m;
}


/************************************
 * See if we can resolve variables in e from values in ptal.
 */

STATIC elem *template_resolve_idents(elem *e, param_t *ptal)
{
    if (EOP(e))
    {
        e->E1 = template_resolve_idents(e->E1, ptal);
        if (EBIN(e))
            e->E2 = template_resolve_idents(e->E2, ptal);
    }
    else if (e->Eoper == OPvar)
    {
        char *id = e->EV.sp.Vsym->Sident;
        param_t *p = ptal->search(id);
        assert(p);
        if (p->Pelem)
        {
            el_free(e);
            e = el_copytree(p->Pelem);
        }
    }
    return e;
}

/*********************************************************
 * Deduce template-argument-list for function.
 *      template<ptpl> T sfunc(tproto);
 *      sfunc<ptali>(pl);
 * This corresponds to cpp_matchfuncs() for non-template functions.
 * Input:
 *      sfunc   function template
 *      ptpl    template-parameter-list
 *      ptali   explicit template-argument-list
 *      ma[2+nargs]     match levels to be filled in like cpp_matchfuncs()
 *      flags   1: determining partial ordering
 *              |8: we don't have pl
 *      tproto  function prototype
 *      pl      function argument list
 * Output:
 *      *pptal  template-argument-list
 * Returns:
 *      !=0     match level
 *      0       no match
 */

Match template_deduce_ptal(type *tthis, symbol *sfunc, param_t *ptali, Match *ma,
        int flags, param_t *pl, param_t **pptal)
{
    param_t *ptpl = sfunc->Sfunc->Farglist;     // template-parameter-list
    type    *tproto = sfunc->Stype;             // function prototype
    param_t *ptal;
    param_t *p;
    Match wmatch;
    Match m;
    int i;
    int any;

    #define LOG_DEDUCE  0

    // Create ptal
    ptal = ptpl->createTal(ptali);

#if LOG_DEDUCE
    printf("\n\ntemplate_deduce_ptal(flags = x%x) ======================================\n", flags);
    printf("**** ptpl template parameter list ***********\n");
    ptpl->print_list();
    printf("**** ptali template initial argument list ***********\n");
    ptali->print_list();
    printf("**** ptal template argument list ***********\n");
    ptal->print_list();
    printf("**** tproto function prototype list ***********\n");
    tproto->Tparamtypes->print_list();
    printf("**** pl function argument list ***********\n");
    pl->print_list();
#endif

    wmatch.m = TMATCHexact;
    wmatch.s = NULL;
    any = 0;
    if (ma && tthis && isclassmember(sfunc) &&
        !(sfunc->Sfunc->Fflags & (Fstatic | Fctor | Fdtor | Finvariant)))
    {
        if (sfunc->Stype->Tty & ~tthis->Tty & (mTYconst | mTYvolatile))
        {
            wmatch.m--;
        }
        if (tthis->Tty & ~sfunc->Stype->Tty & (mTYconst | mTYvolatile))
        {
            goto nomatch;
        }
        any = 1;
    }
    if (ma)
        ma[1] = wmatch;
    i = 2;

#if 0
    // If explicitly specified, it's an exact match
    if (ptpl->length() == ptali->length())
        goto match;
#endif

    // If the ptali is not compatible with ptpl, then no match
    {
        param_t *pt = ptpl;

        for (p = ptali; p; p = p->Pnext)
        {
            if (!pt)
                goto nomatch;
            if (pt->Ptype)
            {   // Looking for an expression of type pt->Ptype
                if (p->Ptype)
                    goto nomatch;
            }
            else if (pt->Pptpl)
            {   // Looking for template-template-argument
                if (!p->Psym)
                    goto nomatch;
            }
            else
            {   // Looking for a type-parameter
                if (!p->Ptype)
                    goto nomatch;
            }
            pt = pt->Pnext;
        }
    }

    if (flags & 8 && ptpl->length() == ptali->length())
        goto match;

    // Loop through the function prototype
    p = tproto->Tparamtypes;            // function prototype
    while (pl || p)
    {
        if (pl)
        {
            if (p)
            {
                if (p->Psym && p->Psym->Sclass == SCtemplate &&
                    p->Psym->Stemplate->TMflags2 & 1)
                {   char *id = p->Psym->Sident;
                    param_t *p2 = ptal->search(id);

                    //printf("template-template-parameter\n");
                    if (!pl->Psym)
                        goto nomatch;

                    if (p2)
                    {
                        p2->Psym = pl->Psym;
                        m.m = TMATCHexact;
                        m.s = NULL;
                        goto Lnext;
                    }
                }
                type_debug(p->Ptype);
                type_debug(pl->Ptype);
                m = template_matchtype(p->Ptype,pl->Ptype,pl->Pelem,ptpl,ptal, flags);
#if LOG_DEDUCE
                printf("\tm = x%02x\n", m.m);
#endif
              Lnext:
                if (m.m == TMATCHnomatch)
                     goto nomatch;
                if (!any || Match::cmp(m, wmatch) < 0)
                {
#if LOG_DEDUCE
                    printf("\tworst\n");
#endif
                    wmatch = m;                 // keep worst match level
                }
                if (ma)
                    ma[i++] = m;
                p = p->Pnext;
            }
            else
            {   // More arguments than prototypes
                if (tproto->Tflags & TFfixed)
                {
                    if (flags & 1 && pl->Pelem) // if it's an unused default argument
                        goto match;
                    goto nomatch;
                }
                else
                {   wmatch.m = TMATCHellipsis;
                    wmatch.s = NULL;
                    goto match;
                }
            }
            pl = pl->Pnext;
        }
        else
        {
            // More prototypes than arguments
            if (p->Pelem)               // if use defaults for remaining parameters
            {
#if 0 // C++ 14.8.2.4-17 defaults do not participate in template type parameter deduction
                for (; p; p = p->Pnext)
                {   elem_debug(p->Pelem);
                    type_debug(p->Ptype);
                    template_matchtype(p->Ptype,p->Pelem->ET,NULL,ptpl,ptal, flags);
                }
#endif
                goto match;
            }
            else
                goto nomatch;
        }
        any = 1;
    }

match:
#if LOG_DEDUCE
    dbg_printf("deduce match = x%x\n", wmatch);
#endif
    if (ma)
        ma[0] = wmatch;
    *pptal = ptal;
    return wmatch;

nomatch:
    param_free(&ptal);
    wmatch.m = TMATCHnomatch;
    wmatch.s = NULL;
    goto match;
}

/*********************************************************
 * Determine if f1 is at least as specialized than f2.
 * Per CPP98 14.5.5.2
 * Input:
 *      ptali   explicit template arguments (should we ignore this?)
 * Returns:
 *      !=0     f1 is at least as specialized than f2
 *      0       less specialized
 */

int template_function_leastAsSpecialized(symbol *f1, symbol *f2, param_t *ptali)
{
    param_t *ptal = NULL;

    //printf("template_function_leastAsSpecialized(%p, %p)\n", f1, f2);
#if 0
    char *p1, *p2;
    Outbuffer buf1;
    Outbuffer buf2;
    p1 = param_tostring(&buf1, f1->Stype);
    p2 = param_tostring(&buf2, f2->Stype);
    printf("template_function_leastAsSpecialized(%s%s, %s%s)\n", f1->Sident,p1, f2->Sident,p2);
#endif

    assert(f1->Sclass == SCfunctempl);
    assert(f2->Sclass == SCfunctempl);

    if (template_deduce_ptal(NULL, f2, NULL, NULL, 1,
        f1->Stype->Tparamtypes, &ptal).m == TMATCHexact)
    {
        // Adding c-v makes it less specialized
        if (f1->Stype->Tty & ~f2->Stype->Tty & (mTYconst | mTYvolatile))
            goto Lless;

        // Make sure ptal was filled in
        for (param_t *p = ptal; p; p = p->Pnext)
        {
            if (!p->Ptype && !p->Pelem && !p->Psym)
                goto Lless;
        }

        //ptal->print_list();
        param_free(&ptal);
        //printf("\tat least as specialized\n");
        return 1;
    }
Lless:
    param_free(&ptal);
    //printf("\tless specialized\n");
    return 0;
}

/*********************************************************
 * Deduce template-argument-list for class templates.
 * Input:
 *      ptpl    template-parameter-list
 *      matchStage
 *      flags   1: determining partial ordering
 *              8:
 *      pproto  prototype
 *      pl      actual argument list
 *
 * Template:
 *      template<class T, int I> class A<T, T*, I> { };
 *      A<int, int*, 2> a2;
 * corresponds to:
 *      template<ptpl> class A<pproto> { };
 *      A<pl> a2;
 *
 * Output:
 *      *pptal  template-argument-list
 * Returns:
 *      !=0     succeeded
 *      0       no match
 */

int template_deduce_ptal2(param_t *ptpl, match_t matchStage,
        int flags, param_t *pproto, param_t *pl, param_t **pptal)
{
    param_t *ptal;
    param_t *p;
    Match m;

    #define LOG_DEDUCE2 0

    // Create ptal
    ptal = ptpl->createTal(NULL);

#if LOG_DEDUCE2
    printf("\n\ntemplate_deduce_ptal2(flags = x%x, matchStage = x%x) ================================\n", flags, matchStage);
    printf("template parameter list ***********\n");
    ptpl->print_list();
    printf("prototype list ***********\n");
    pproto->print_list();
    printf("actual argument list ***********\n");
    pl->print_list();
#endif

    // Loop through the prototype
    p = pproto;
    while (pl || p)
    {
        if (pl)
        {
            if (p)
            {
                if (p->Psym && p->Psym->Sclass == SCtemplate &&
                    p->Psym->Stemplate->TMflags2 & 1)
                {   char *id = p->Psym->Sident;
                    param_t *p2 = ptal->search(id);

                    //printf("template-template-parameter '%s'\n", id);
                    if (!pl->Psym)
                        goto nomatch;

                    if (p2)
                    {
                        p2->Psym = pl->Psym;
                        goto Lnext;
                    }
                }
                m = template_matchtype(p->Ptype,pl->Ptype,NULL,ptpl,ptal, flags | 4);
                if (m.m < matchStage)
                    goto nomatch;
                if (!el_match3(p->Pelem,pl->Pelem))
                {
                    elem *epar = p->Pelem;
                    elem *earg = pl->Pelem;

#if 0
                    printf("epar:\n");
                    elem_print(epar);
                    printf("earg:\n");
                    elem_print(earg);
#endif
                    if (epar->Eoper == OPvar)
                    {
                        char *id = epar->EV.sp.Vsym->Sident;
                        param_t *p = ptal->search(id);
                        if (p)
                        {
                            if (EOP(earg))
                                goto nomatch;
                            if (flags & 1)      // if partial ordering test
                            {
                                if (p->Pelem)
                                {
                                    if (earg->Eoper == OPvar &&
                                        p->Pelem->Eoper == OPvar &&
                                        strcmp(p->Pelem->EV.sp.Vsym->Sident, earg->EV.sp.Vsym->Sident) == 0)
                                        goto Lnext;
                                    if (el_match3(earg, p->Pelem))
                                        goto Lnext;
                                    goto nomatch;
                                }
                                else
                                {
                                    p->Pelem = el_copytree(earg);
                                }
                            }
                            else
                            {
                                if (earg->Eoper == OPvar)
                                    goto nomatch;
                                elem *e = el_copytree(earg);
                                e = poptelem(typechk(e, epar->ET));
                                if (p->Pelem && !el_match(p->Pelem, e))
                                    goto nomatch;
                                p->Pelem = e;
                            }
                            goto Lnext;
                        }
                    }
                    goto nomatch;
                }
              Lnext:
                p = p->Pnext;
            }
            else
            {   // More arguments than prototypes
                goto nomatch;
            }
            pl = pl->Pnext;
        }
        else
        {
            // More prototypes than arguments
            if (p->Pelem)               // if use defaults for remaining parameters
            {
                for (; p; p = p->Pnext)
                {   elem_debug(p->Pelem);
                    type_debug(p->Ptype);
                    template_matchtype(p->Ptype,p->Pelem->ET,NULL,ptpl,ptal, flags);
                }
                goto match;
            }
            else
                goto nomatch;
        }
    }

match:
#if LOG_DEDUCE2
    dbg_printf("deduce match with ptal of:\n");
    ptal->print_list();
#endif
    *pptal = ptal;
    return 1;

nomatch:
#if LOG_DEDUCE2
    dbg_printf("deduce nomatch\n");
#endif
    param_free(&ptal);
    *pptal = NULL;
    return 0;
}

/********************************************************
 * Determine if class template st1 is at least as specialized as st2.
 * Per CPP98 14.5.4.2
 * Returns:
 *      !=0     st1 is at least as specialized than st2
 *      0       less specialized
 */

int template_class_leastAsSpecialized(symbol *st1, symbol *st2)
{
    param_t *ptal = NULL;

    //printf("template_class_leastAsSpecialized(%p, %p)\n", st1, st2);
#if 0
    char *p1, *p2;
    Outbuffer buf1;
    Outbuffer buf2;
    p1 = param_tostring(&buf1, st1->Stemplate->TMptal);
    p2 = param_tostring(&buf2, st2->Stemplate->TMptal);
    printf("template_class_leastAsSpecialized(%s%s, %s%s)\n", st1->Sident,p1, st2->Sident,p2);
#endif

    assert(st1->Sclass == SCtemplate);
    assert(st2->Sclass == SCtemplate);

    if (template_deduce_ptal2(st2->Stemplate->TMptpl, TMATCHexact, 1,
        st2->Stemplate->TMptal, st1->Stemplate->TMptal, &ptal))
    {
        param_free(&ptal);
        //printf("\tat least as specialized\n");
        return 1;
    }
Lless:
    param_free(&ptal);
    //printf("\tless specialized\n");
    return 0;
}

/********************************************************
 * Determine which class template is a match, either the primary
 * class or one of the partial specializations.
 * Use CPP98 14.5.4.1 rules.
 * Rewrite *pptali if we get a match with a partial.
 * Returns:
 *      matching class to use
 */

symbol *template_class_match(symbol *sprimary, param_t *ptali, param_t **pptali)
{
    symbol *s;
    symbol *spartial = NULL;
    param_t *ppartial = NULL;
    symbol *ambig = NULL;
    int i = 1;
    int n;

#define LOG_CLASS_MATCH 0

#if LOG_CLASS_MATCH
    printf("template_class_match(sprimary = %p, '%s')\n", sprimary, sprimary->Sident);
#endif

    /* Per CPP98 14.5.4.3-2: "If the primary member template is explicitly
     * specialized for a given (implicit) specialization of the enclosing
     * class template, the partial specializations of the member template
     * are ignored for this specialization of the enclosing class template."
     */

    if (sprimary->Sscope &&
        sprimary->Sscope->Sclass == SCstruct &&         // if sprimary is a member template
        sprimary->Sscope->Sstruct->Stempsym)            // of an enclosing class template
    {   Classsym *sencl = (Classsym *)sprimary->Sscope;
        symbol *sencltemp = sencl->Sstruct->Stempsym;
        list_t tl;

#if LOG_CLASS_MATCH
        printf("\tprimary member template is member of enclosing class template '%s'\n", sencltemp->Sident);
#endif

        for (tl = sencltemp->Stemplate->TMnestedexplicit; tl; tl = list_next(tl))
        {
            TMNE *tmne = (TMNE *)list_ptr(tl);

            //printf("\t\tTMNE '%s'\n", tmne->name);
            if (strcmp(tmne->name, sprimary->Sident) != 0)
                continue;
            //printf("\t\tnames match\n");
            if (!template_arglst_match(tmne->ptal, sencl->Sstruct->Sarglist))
                continue;
            //printf("\t\ttemplate-argument-list matches for enclosing template\n");
#if 0
            printf("+++++\n");
            token_funcbody_print(tmne->tdecl);
            printf("-----\n");
#endif

            int levelsave = level;
            level = 0;
            // BUG: What other context save/restore do we need to do?

            token_unget();
            token_setlist(tmne->tdecl);
            tok.TKval = tmne->tk;
            template_class_decl(sencl, NULL, NULL, 1, 0);
            tmne_free(tmne);
            level = levelsave;
            stoken();

            list_subtract(&sencltemp->Stemplate->TMnestedexplicit, tmne);
            break;
        }

        for (tl = sencltemp->Stemplate->TMexplicit; tl; tl = list_next(tl))
        {
            /* We now have:
             *  template<> template<class T2> struct sencltemp<tme->ptal>::s {}
             */

            TME *tme = (TME *)list_ptr(tl);
            symbol *s = tme->stempl;

            if (strcmp(s->Sident, sprimary->Sident) != 0)
                continue;
            //printf("\t\tnames match\n");
            if (!template_arglst_match(tme->ptal, sencl->Sstruct->Sarglist))
                continue;
            //printf("\t\ttemplate-argument-list matches for enclosing template\n");

            if (!s->Sscope)
            {
                s->Sflags |= sencl->Sstruct->access;
                s->Sscope = sencl;
                s->Stemplate->TMprimary = sprimary;
            }
#if LOG_CLASS_MATCH
            printf("\t\tmatch is %p\n", s);
#endif
            *pptali = ptali;
            return s;
        }
    }

    // Look at partial specializations for a match

    for (s = sprimary->Stemplate->TMpartial; s; s = s->Stemplate->TMpartial)
    {   int deduce;
        param_t *ptal = NULL;

        i++;
#if LOG_CLASS_MATCH
        printf("\tlooking at partial %p (#%d)\n", s, i);
#endif

        deduce = template_deduce_ptal2(s->Stemplate->TMptpl, TMATCHexact, 8,
//      deduce = template_deduce_ptal2(s->Stemplate->TMptpl, TMATCHexact, 0,
            s->Stemplate->TMptal, ptali, &ptal);

        if (deduce)
        {
#if LOG_CLASS_MATCH
            printf("\t\tmatch with %p (#%d), deduce = x%x\n", s, i, deduce);
#endif
            if (!spartial)   // if first match
            {
                ambig = NULL;
                spartial = s;
                ppartial = ptal;
                n = i;
                ptal = NULL;
            }
            else                        /* subsequent match     */
            {
                // Class templates s and spartial both match.
                // Try to disambiguate by determining which is
                // more specialized.
                // spartial gets the more specialized class template.
                int c1 = template_class_leastAsSpecialized(s, spartial);
                int c2 = template_class_leastAsSpecialized(spartial, s);

#if LOG_CLASS_MATCH
                printf("c1 = %d, c2 = %d\n", c1, c2);
#endif
                if (c1 && !c2)
                {   // s is more specialized
                    ambig = NULL;
                    spartial = s;
                    ppartial = ptal;
                    n = i;
                    ptal = NULL;
                }
                else if (!c1 && c2)
                {   // spartial is more specialized
                    ambig = NULL;
                }
                else
                {   // Equally specialized
                    ambig = s;        // which is ambiguous
                }
            }
        }
        param_free(&ptal);
    }

    if (ambig)
        cpperr(EM_ambiguous_partial, ambig->Sident);

    if (spartial)
    {
#if LOG_CLASS_MATCH
        printf("\tmatch with partial %p '%s' #%d\n", spartial, spartial->Sident, n);
#endif
        *pptali = ppartial;
        return spartial;
    }
    else
    {
#if LOG_CLASS_MATCH
        printf("\tmatch with primary\n");
#endif
        *pptali = ptali;
        return sprimary;
    }
}

/********************************************************
 * If an already instantiated template function matches,
 * verify that it is well-formed, i.e. the template parameters are
 * accounted for.
 * See example CPP98 14.8.1.2
 */

void template_function_verify(symbol *sfunc,
        list_t arglist, param_t *ptali, int matchStage)
{
    if (sfunc->Sfunc && sfunc->Sfunc->Fflags & Finstance)
    {   symbol *stemp;
        param_t *ptal = NULL;
        param_t *pl = NULL;

        //printf("template_function_verify('%s', match = %x)\n", sfunc->Sident, matchStage);
        stemp = sfunc->Sfunc->Ftempl;           // the function template

        // Turn function argument list into pl
        for (; arglist; arglist = list_next(arglist))
        {   elem *e = list_elem(arglist);
            param_t *p = param_append_type(&pl,e->ET);
            //if (e->Eoper == OPconst)
                p->Pelem = el_copytree(e);
        }

        if (!template_deduce_ptal(NULL, stemp, ptali, NULL, 0, pl, &ptal).m)
        {
            assert(0);                          // no match
        }

        // Check and see they are all filled in
        param_t *ptpl = stemp->Sfunc->Farglist;
        for (; ptal; ptal = ptal->Pnext, ptpl = ptpl->Pnext)
        {
            if (!ptal->Ptype && !ptal->Pelem && !ptal->Psym)
            {
                cpperr(EM_templ_arg_unused, ptpl->Pident);
            }
        }

        param_free(&pl);
        param_free(&ptal);
    }
}

/*********************************************
 * CPP98 14.7.2 Explicit instantiation.
 */

void template_explicit_instantiation()
{
    /* explicit-instantiation:
     *     template declaration
     */
    type *tspec;
    char vident[2*IDMAX + 1];
    type *dt;
    int dss;

    //printf("template_explicit_instantiation()\n");
    pstate.STexplicitInstantiation++;
    dss = type_specifier(&tspec);
    dt = declar_fix(tspec,vident);
    pstate.STexplicitInstantiation--;

    if (vident[0] == 0)         // if there was no identifier
    {
        if (type_struct(tspec))
        {   tspec->Ttag->Sstruct->Sflags |= STRexplicit;
            template_instantiate_forward(tspec->Ttag);
        }
        else
            goto Lerr;
    }
    else
    {
        symbol *sft;
        symbol *sfunc;
        list_t arglist;
        param_t *p;

        if (gdeclar.class_sym)
        {   Classsym *stag = gdeclar.class_sym;

            stag->Sstruct->Sflags |= STRexplicit;
            template_instantiate_forward(stag);
            //printf("\tstag = '%s', vident = '%s'\n", stag->Sident, vident);
            if (gdeclar.oper == OPMAX)
            {   Match m;

                //printf("\tcast operator overload\n");
                sft = cpp_typecast(stag->Stype, dt->Tnext, &m);
                if (!sft || m.m != TMATCHexact)
                     goto Lerr;
                goto Lret;
            }
            sft = n2_searchmember(stag, vident);
            assert(sft);
            if (sft->Sclass != SCfunctempl)
            {
#if 0 // needs work, as sft->Stype could be a TYmfunc but dt is still a TYnfunc
                if (!typematch(dt, sft->Stype, 0))
                    goto Lerr;
#endif
                goto Lret;
            }
        }
        else if (gdeclar.namespace_sym)
            sft = nspace_searchmember(vident, gdeclar.namespace_sym);
        else
            sft = scope_search(vident,SCTglobal | SCTnspace);
        assert(sft && sft->Sclass == SCfunctempl);

        if (!tyfunc(dt->Tty))
            goto Lerr;

        arglist = NULL;
        for (p = dt->Tparamtypes; p; p = p->Pnext)
        {
            elem *e = el_longt(p->Ptype, 0);

            list_append(&arglist, e);
        }

        sfunc = cpp_overload(sft,NULL,arglist,NULL,gdeclar.ptal,0);

        list_free(&arglist, (list_free_fp)el_free);
    }

Lret:
    type_free(dt);
    type_free(tspec);
    return;

Lerr:
    cpperr(EM_malformed_explicit_instantiation);
    goto Lret;
}

/************************* TMF ******************************/

void tmf_free(TMF *tmf)
{
    token_free(tmf->tbody);
    param_free(&tmf->temp_arglist);
    param_free(&tmf->temp_arglist2);
    param_free(&tmf->ptal);
    mem_free(tmf);
}

#if HYDRATE
void tmf_hydrate(TMF **ptmf)
{
    if (isdehydrated(*ptmf))
    {   TMF *tmf = (TMF *)ph_hydrate(ptmf);

        token_hydrate(&tmf->tbody);
        ph_hydrate(&tmf->to);
        param_hydrate(&tmf->temp_arglist);
        param_hydrate(&tmf->temp_arglist2);
        param_hydrate(&tmf->ptal);
        ph_hydrate(&tmf->name);
        symbol_hydrate(&tmf->sclassfriend);
        Classsym_hydrate(&tmf->stag);
    }
}
#endif

#if DEHYDRATE
void tmf_dehydrate(TMF **ptmf)
{
    TMF *tmf = *ptmf;

    if (tmf && !isdehydrated(tmf))
    {
        token_dehydrate(&tmf->tbody);
        ph_dehydrate(&tmf->to);
        param_dehydrate(&tmf->temp_arglist);
        param_dehydrate(&tmf->temp_arglist2);
        param_dehydrate(&tmf->ptal);
        ph_dehydrate(&tmf->name);
        symbol_dehydrate(&tmf->sclassfriend);
        Classsym_dehydrate(&tmf->stag);
    }
}
#endif

/************************* TME ******************************/

void tme_free(TME *tme)
{
    symbol_free(tme->stempl);
    param_free(&tme->ptal);
    mem_free(tme);
}

#if HYDRATE
void tme_hydrate(TME **ptme)
{
    if (isdehydrated(*ptme))
    {   TME *tme = (TME *)ph_hydrate(ptme);

        symbol_hydrate(&tme->stempl);
        param_hydrate(&tme->ptal);
    }
}
#endif

#if DEHYDRATE
void tme_dehydrate(TME **ptme)
{
    TME *tme = *ptme;

    if (tme && !isdehydrated(tme))
    {
        symbol_dehydrate(&tme->stempl);
        param_dehydrate(&tme->ptal);
    }
}
#endif

/************************* TMNE ******************************/

void tmne_free(TMNE *tmne)
{
    MEM_PH_FREE(tmne->name);
    param_free(&tmne->ptal);
    token_free(tmne->tdecl);
    mem_free(tmne);
}

#if HYDRATE
void tmne_hydrate(TMNE **ptmne)
{
    if (isdehydrated(*ptmne))
    {   TMNE *tmne = (TMNE *)ph_hydrate(ptmne);

        ph_hydrate(&tmne->name);
        param_hydrate(&tmne->ptal);
        token_hydrate(&tmne->tdecl);
    }
}
#endif

#if DEHYDRATE
void tmne_dehydrate(TMNE **ptmne)
{
    TMNE *tmne = *ptmne;

    if (tmne && !isdehydrated(tmne))
    {
        ph_dehydrate(&tmne->name);
        param_dehydrate(&tmne->ptal);
        token_dehydrate(&tmne->tdecl);
    }
}
#endif

/************************* TMNF ******************************/

void tmnf_free(TMNF *tmnf)
{
    token_free(tmnf->tdecl);
    param_free(&tmnf->temp_arglist);
    mem_free(tmnf);
}

#if HYDRATE
void tmnf_hydrate(TMNF **ptmnf)
{
    if (isdehydrated(*ptmnf))
    {   TMNF *tmnf = (TMNF *)ph_hydrate(ptmnf);

        token_hydrate(&tmnf->tdecl);
        param_hydrate(&tmnf->temp_arglist);
        symbol_hydrate(&tmnf->stempl);
        symbol_hydrate((symbol **)&tmnf->stag);
    }
}
#endif

#if DEHYDRATE
void tmnf_dehydrate(TMNF **ptmnf)
{
    TMNF *tmnf = *ptmnf;

    if (tmnf && !isdehydrated(tmnf))
    {
        token_dehydrate(&tmnf->tdecl);
        param_dehydrate(&tmnf->temp_arglist);
        symbol_dehydrate(&tmnf->stempl);
        symbol_dehydrate((symbol **)&tmnf->stag);
    }
}
#endif

#endif
