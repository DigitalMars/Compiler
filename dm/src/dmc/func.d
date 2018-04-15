/**
 * Implementation of the
 * $(LINK2 http://www.digitalmars.com/download/freecompiler.html, Digital Mars C/C++ Compiler).
 *
 * Copyright:   Copyright (c) 1984-1998 by Symantec, All Rights Reserved
 *              Copyright (c) 2000-2017 by Digital Mars, All Rights Reserved
 * Authors:     $(LINK2 http://www.digitalmars.com, Walter Bright)
 * License:     Distributed under the Boost Software License, Version 1.0.
 *              http://www.boost.org/LICENSE_1_0.txt
 * Source:      https://github.com/DigitalMars/Compiler/blob/master/dm/src/dmc/func.d
 */

// Function and control flow parser

module func;

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
import dmd.backend.oper;
import dmd.backend.outbuf;
import dmd.backend.ty;
import dmd.backend.type;

import dmd.backend.dlist;
import tk.mem;

import cpp;
import dtoken;
import eh;
import msgs2;
import parser;
import scopeh;


extern (C++):

alias dbg_printf = printf;
alias MEM_PH_MALLOC = mem_malloc;
alias MEM_PH_FREE = mem_free;
alias MEM_PARF_CALLOC = mem_calloc;
alias MEM_PARF_REALLOC = mem_realloc;
alias MEM_PARF_FREE = mem_free;

/*****************************************
 * Data structure for case statement.
 */

struct CASES
{   targ_llong Cval;    // case value
    block *Clab;        // case label
}

struct casedat
{
    int Cmax;           /* max # of slots for cases in Ccases[] */
    int Cindex;         /* # of case slots already used         */
    block *Cdef;        // default label (NULL if not used)
    tym_t Cty;          /* type of case statement               */
    CASES *Ccases;
}

/*****************************
 * Named return value package
 */

struct Namret
{   char can;                   /* !=0 if we can still do it            */
    list_t explist;             /* list of expressions to replace       */
    Symbol *s;                  /* auto to be replaced                  */
    Symbol *sret;               /* with this value, which is the return val */
}

/+
void namret_init();
void namret_term();
void namret_process();
void namret_replace(elem *e);
+/

/****************************************
 * Current state of the function parser.
 */

alias FSFlags_t = uint;
enum
{
    FSFretexp = 1,     // if we've seen a return exp;
    FSFskip   = 4,     // if there are switch or goto statements
    FSFin     = 8,     // we're in an __in block
    FSFout    = 0x10,  // we're in an __out block
}

struct Funcstate
{
    block *brklabel;            // break label
    block *cntlabel;            // continue label

    // NTEXCEPTIONS
    block *leavelabel;          // leave label
    int scope_index;            // current scope index
    int next_index;             // value for next scope index

    casedat *caseptr;           // ptr to current case data
    FSFlags_t flags;
    list_t _scope;              // list of blocks that have open scopes
    Namret namret;              // named return value state
    Symbol *shidden;            /* symbol of hiddenparam                */
    block *outblock;            // if != NULL, the starting block of __out { }
    Symbol *sresult;            // __out (result) { }
}

__gshared Funcstate funcstate =
{       null,null,

        // NTEXCEPTIONS
        null,0,
};

/+
/*private*/ void fscope_end();
/*private*/ void func_state();
/*private*/ void compound_state();
/*private*/ void state_cpp();
/*private*/ Srcpos statement(int);
void dolabel(const(char)* labelident);
/*private*/ void if_state();
/*private*/ void debug_state();
/*private*/ void while_state();
/*private*/ void do_state();
/*private*/ void for_state();
/*private*/ void switch_state();
/*private*/ void case_label();
/*private*/ void default_label();
/*private*/ void break_state(block *,int);
/*private*/ void return_state();
/*private*/ void goto_state();
/*private*/ void nttry_state();
/*private*/ void func_adddestructors();
/*private*/ void func_doblock(block *bstart);
/*private*/ void func_lookat(block *bstart , block *b);
/*private*/ void func_adddtors(block *, SYMIDX , SYMIDX);
/*private*/ void appenddesr(elem **pe , elem *de);
/*private*/ void base_initializer(Symbol *s);
/*private*/ void with_state();
/*private*/ void except_try_state(int flags);
+/

/*********************************
 * Save state of globals so we can nest function definitions.
 */

void func_nest(Symbol *s)
{
    /* Save state of global variables   */
    block*      startblocksave  = startblock;
    block*      curblocksave    = curblock;
    Symbol*     funcsym_psave   = funcsym_p;
    int         levelsave       = level;
    Funcstate   funcstatesave   = funcstate;
    Pstate      pstatesave      = pstate;
    symtab_t    globsymsave     = globsym;
    symtab_t    *cstatesymsave  = cstate.CSpsymtab;
    bool        initsave        = init_staticctor;

    init_staticctor = 0;

    pstate.STinarglist = 0;
    pstate.STinsizeof  = 0;
    pstate.STbtry = null;
    pstate.STgotolist = null;
    pstate.STdeferDefaultArg = 0;

    if (globsym.top)
        memset(&globsym,0,globsym.sizeof);
    cstate.CSpsymtab = &globsym;

    func_body(s);

    startblock  = startblocksave;
    curblock    = curblocksave;
    funcsym_p   = funcsym_psave;
    level       = levelsave;
    pstate      = pstatesave;
    funcstate   = funcstatesave;
    init_staticctor = initsave;

    cstate.CSpsymtab = cstatesymsave;
    assert(globsym.top == 0);
    if (globsymsave.top)
    {
        symtab_free(globsym.tab);
        globsym = globsymsave;
    }
}


/*********************************
 * Do function body.
 * function_body ::= type_decl_list function_statement
 * Input:
 *      s ->            function symbol
 *      paramlst        list of function parameters
 */

void func_body(Symbol *s)
{ param_t* p,pproto;
  type *tfunc;
  Symbol *sp;
  symlist_t sl;
  symlist_t plist;
  elem *e;
  tym_t ety;
  func_t *f;
  Symbol *svirtbase;
  uint param;
  int nscopes;
  Scope *scsave;

  //printf("func_body('%s')\n", s->Sident);
  if (CPP)
        nspace_checkEnclosing(s);
  if (s.Sflags & SFLimplem)
  {     synerr(EM_multiple_def,prettyident(s)); // already defined
        return;
  }
  s.Sflags |= SFLimplem;               /* seen the implementation      */
  namret_init();
  f = s.Sfunc;
  assert(f);
  file_progress();
  if (configv.verbose == 2)
        dbg_printf("%s\n",prettyident(s));
  funcsym_p = s;
  if (CPP)
  {     Symbol *sscope;

        nscopes = 0;
        scsave = scope_end;

        sscope = s.Sscope;
        if (!sscope)
            sscope = f.Fparsescope;
        if (sscope)
        {
            if (scope_end.sctype & SCTglobal)
            {
                if (sscope.Sscope)
                    nscopes = scope_pushEnclosing(sscope);
                if (sscope.Sclass == SCnamespace)
                    scope_push_symbol(sscope);
            }
            if (sscope.Sclass == SCstruct)
            {   if (s.Sscope)
                    scope_push(sscope, cast(scope_fp)&struct_searchmember,SCTmfunc);
                else
                    scope_pushclass(cast(Classsym *)sscope);
                nscopes++;
            }
        }
  }
  scope_push(null,cast(scope_fp)&findsy,SCTlabel);   // create symbol table for labels
  createlocalsymtab();                  // create parameter symbol table

  if (configv.addlinenumbers)
        f.Fstartline = token_linnum();

  assert(CPP || globsym.top == 0 || errcnt);    // no local symbols yet
  level = 1;                            /* function param level         */
  funcstate.flags = 0;                  /* haven't seen return exp; yet */

  // NTEXCEPTIONS
  funcstate.scope_index = -1;
  funcstate.next_index = 0;

  while (1)
  {
        switch (tok.TKval)
        {
            case TKlcur:                // start of function statement
            case TKcolon:               // base initializer
            case TKtry:                 // function-try-block
            case TK_in:                 // __in statement
            case TK_out:                // __out statement
                break;

            default:
                // Must be old K&R style parameter declaration
                declaration(0);
                continue;
        }
        break;
  }

  tfunc = s.Stype;
  assert(tyfunc(tfunc.Tty));

    if (tybasic(tfunc.Tty) == TYf16func)
        synerr(EM_far16_extern);        // _far16 functions can only be extern
    if (tfunc.Tty & mTYimport && !SymInline(funcsym_p))
        tx86err(EM_bad_dllimport);      // definition for dllimport not allowed

  plist = null;
  svirtbase = null;
  if (CPP && f.Fclass && !(f.Fflags & Fstatic))
  {
        if (f.Fflags & (Fctor | Fdtor | Finvariant))
        {   baseclass_t *b;
            Symbol *sf;

            /* if X::X()        */
            if (f.Fflags & Fctor)
            {   /* Put out virtual base class parameters        */
                if (f.Fclass.Sstruct.Svirtbase)
                {   sf = scope_define(cpp_name_initvbases.ptr,SCTlocal,SCparameter);
                    sf.Stype = tstypes[TYint];
                    sf.Stype.Tcount++;
                    if (tybasic(tfunc.Tty) == TYmfunc)
                        svirtbase = sf;
                    else
                        list_append(&plist,sf);
                }
            }
        }
  }

  e = null;
  param = 0;
  if (!paramlst)                        /* if no parameter list         */
  {     /* Get parameters from the types themselves                     */
        for (p = tfunc.Tparamtypes; p; p = p.Pnext)
        {   type *t = p.Ptype;

            if (!p.Pident)
            // Generate identifier
            {   immutable string format = "_param__P%d";

                if (!CPP)
                    synerr(EM_no_ident_decl);   // no identifier for declarator
                p.Pident = cast(char *) MEM_PH_MALLOC(format.length + 3 + 1);
                sprintf(p.Pident,cast(const(char)*)format.ptr,++param);
            }
            {   int olderrcnt = errcnt;
                if (p.Psym)            // already have a symbol for this parameter
                {   sp = p.Psym;
                    p.Psym = null;
                    scope_add(sp, SCTlocal);
                }
                else
                {
                    sp = scope_define(p.Pident,SCTlocal,SCparameter);
                    sp.Sscope = funcsym_p;
                    sp.Stype = t;
                    t.Tcount++;
                }
                MEM_PH_FREE(p.Pident);
                p.Pident = null;
                list_append(&plist,sp);

                // Look for VLA expressions
                if (typtr(p.Ptype.Tty) && type_isvla(p.Ptype.Tnext))
                {
                    e = el_combine(e, type_vla_fix(&p.Ptype));
                }
            }
        }
  }

  /*
   * Check parameter types against prototype, if any.
   * If no prototype, generate a default one based on the type of the
   * parameter.
   */
  pproto = tfunc.Tparamtypes;
  for (p = paramlst; p; p = p.Pnext)
  {     type *tp;

        assert(p.Pident);
        /*dbg_printf("p.Pident = '%s'\n",p.Pident);*/
        if (!p.Ptype)                  /* if parameter is not declared */
        {   /* Declare parameter as integer                             */
            p.Ptype = tstypes[TYint];
            tstypes[TYint].Tcount++;
        }
        /* Conversion of arrays to pointers should have already occurred */
        assert(tybasic(p.Ptype.Tty) != TYarray);

        sp = scope_define(p.Pident,SCTlocal,SCparameter);
        MEM_PH_FREE(p.Pident);
        p.Pident = null;

        sp.Stype = p.Ptype;
        sp.Stype.Tcount++;
        if (tybasic(sp.Stype.Tty) == TYfloat)
        {   elem *ec;

            // Generate sp=(float)(double &)sp;
            ec = el_var(sp);
            el_settype(ec,tstypes[TYdouble]);
            ec = el_unat(OPd_f,tstypes[TYfloat],ec);
            e = el_combine(e,el_bint(OPeq,tstypes[TYfloat],el_var(sp),ec));
            sp.Sflags |= SFLdouble;
        }
        tp = p.Ptype;
        tp.Tcount++;                   // create copy before adjustment
        if (!typtr(p.Ptype.Tty))
            paramtypadj(&p.Ptype);     // default conversions

        if (tfunc.Tparamtypes)         /* if type was specified        */
        {
            if (pproto)
            {   if (!typematch(p.Ptype,pproto.Ptype,false))
                {   const(char)* ti;

                    // See if we should allow some cases of old style
                    // definitions
                    ti = "int";
                    switch (tybasic(tp.Tty))
                    {
                        case TYushort:
                            if (_tysize[TYint] <= SHORTSIZE)
                                ti = "unsigned";
                            goto case TYschar;
                        case TYschar:
                        case TYuchar:
                        case TYchar:
                        case TYshort:
                            if (typematch(tp,pproto.Ptype,false))
                            {   if (config.ansi_c)
                                    synerr(EM_prototype,&sp.Sident[0],ti);
                                goto typeok;
                            }
                            break;

                        default:
                            break;
                    }
                    typerr(EM_nomatch_proto,p.Ptype,pproto.Ptype,&sp.Sident[0]);
                typeok: ;
                }
                pproto = pproto.Pnext;
            }
            else if (config.ansi_c)
                synerr(EM_nomatch_proto,&sp.Sident[0]);    // definition doesn't match prototype
        }
        type_free(tp);
        list_append(&plist,sp);
    }
    // Generate prototype for functions that do not have one
    if (!(tfunc.Tflags & TFprototype) && config.flags3 & CFG3autoproto)
    {
        tfunc.Tflags |= TFprototype | TFfixed;         // there is one now
        assert(tfunc.Tparamtypes == null);
        tfunc.Tparamtypes = paramlst;
        paramlst = null;
    }
    else
        param_free(&paramlst);

    assert(!CPP || globsym.top == 0);           // no local symbols yet

    // If a member function, put out the symbol for 'this'
    if (CPP && f.Fclass && !(f.Fflags & Fstatic))
        symbol_add(cpp_declarthis(s,f.Fclass));

    if (exp2_retmethod(tfunc) == RET_STACK)
    {
        /* If function returns a struct, put a pointer to that  */
        /* as the first argument                                */
        {   type *thidden = exp2_hiddentype(tfunc);
            Symbol *shidden;
            char[9+1] hiddenparam = void;  // name of hidden parameter to
                                           // pascal functions
            __gshared int hiddenparami;    // how many we've generated so far

            sprintf(hiddenparam.ptr,"__TMP%d",++hiddenparami);
            shidden = symbol_name(hiddenparam.ptr,SCparameter,thidden);
            shidden.Sflags |= SFLfree | SFLtrue;
            funcstate.shidden = shidden;
            if (type_mangle(tfunc) == mTYman_cpp)
                list_prepend(&plist,shidden);
            else
                symbol_add(shidden);
        }
    }

    if (svirtbase)
        list_append(&plist,svirtbase);

    /* Put out parameter list (in reverse for pascal functions) */
    if (tyrevfunc(tfunc.Tty))
        for (sl = list_last(plist); sl; sl = list_prev(plist,sl))
            symbol_add(list_symbol(sl));
    else
        for (sl = plist; sl; sl = list_next(sl))
            symbol_add(list_symbol(sl));
    list_free(&plist,FPNULL);

    // Run through parameters, and output destructor info for
    // constructed parameters
    if (config.flags3 & CFG3eh)
    {   SYMIDX si;

        for (si = 0; si < globsym.top; si++)
        {
            sp = globsym.tab[si];
            if (type_struct(sp.Stype))
            {   Classsym *stag = sp.Stype.Ttag;

                template_instantiate_forward(stag);
                if (stag.Sstruct.Sdtor && pointertype == stag.Sstruct.ptrtype)
                {   Symbol *sd;
                    elem *ec;

                    sd = n2_createprimdtor(stag);
                    ec = el_ctor(el_ptr(sp),null,sd);
                    e = el_combine(e,ec);
                }
            }
        }
    }

    startblock = curblock = block_calloc();     // create initial block
    startblock.Bsymstart = 0;
    funcstate._scope = null;
    startblock.Belem = e;
    level = 2;                          // at function block scope
    func_state();                       // do function_statement
    symbol_debug(s);

    // Decide if we want to return; or return 0;
    if (f.Fflags3 & Fmain)
    {   funcstate.flags |= FSFretexp;
        block_endfunc(1);
    }
    else
        block_endfunc(0);               // do the final return

    list_free(&funcstate._scope,FPNULL); // dump any fluff

    if (configv.addlinenumbers)
        f.Fendline = token_linnum();

    level = 0;                          // back to global level
    deletesymtab();                     // delete parameter symbol table
    symbol_free(cast(Symbol *)scope_pop());         // delete label table
    if (CPP)
    {
        scope_unwind(nscopes);
        scope_setScopeEnd(scsave);

        tfunc = funcsym_p.Stype;       // in case type changed (possible if
                                        // redeclared within function body)

        // Look to see that if the function is declared to return a value,
        // that there is at least one return expression statement.
        if (tybasic(tfunc.Tnext.Tty) != TYvoid &&
          !(funcsym_p.Sfunc.Fflags & (Fctor | Fdtor | Finvariant)) &&
          !(funcstate.flags & FSFretexp))
            func_noreturnvalue();
    }

    block_ptr();
    if (CPP)
    {   namret_process();               // named return value optimization
        func_adddestructors();          // and add any destructor calls
    }
    f.Fstartblock = startblock;
    savesymtab(f);

    // "Fix" constructors and destructors
    if (f.Fflags & Fctor)
        cpp_fixconstructor(funcsym_p);
    else if (f.Fflags & Fdtor)
        cpp_fixdestructor(funcsym_p);
    else if (f.Fflags & Finvariant)
        cpp_fixinvariant(funcsym_p);

    if (SymInline(funcsym_p))
    {
        block_pred();
        block_compbcount();             // eliminate unreachable blocks
        brcombine();                    // attempt to simplify function
        if (inline_possible(funcsym_p))
            f.Fflags |= Finline;
        else
        {
            // temporary warning to help find all the cases
            //warerr(WM.WM_no_inline,funcsym_p.Sident);     // can't inline the function
        }

        // If NT, and inline function is exported, then we must write it out
        if (config.exe == EX_WIN32 && tfunc.Tty & mTYexport)
            nwc_mustwrite(funcsym_p);
        if (f.Fflags & Fmustoutput)            /* if must output anyway */
            queue_func(funcsym_p);
        else if (CPP && f.Fflags3 & Fvtblgen)  // if vtbl[] key function
        {   SC scvtbl;
            Classsym *stag = cast(Classsym *)funcsym_p.Sscope;

            assert(stag.Sclass == SCstruct);
            f.Fflags3 &= ~Fvtblgen;
            stag.Sstruct.Sflags &= ~STRvtblext;
            if (SYMDEB_CODEVIEW && config.fulltypes == CV4)
                cv4_struct(stag,3);             // generate debug info for class

            // If we already generated an SCextern reference to it
            if (stag.Sstruct.Svtbl
                || stag.Sstruct.Svbtbl
                )
            {
                scvtbl = cast(SC) ((config.flags2 & CFG2comdat) ? SCcomdat : SCstatic);
                n2_genvtbl(stag,scvtbl,1);
                n2_genvbtbl(stag,scvtbl,1);
            }
        }
    }
    else
        queue_func(funcsym_p);
    startblock = null;
}


/*****************************
 * Adjust types of function parameters for non-prototyped parameters.
 * Promote floats to doubles.
 * Promote bytes to words.
 * Convert <array of> to <pointer to>.
 */

void paramtypadj(type **pt)
{ type *t;

  assert(pt);
  t = *pt;
  assert(t);
  switch (tybasic(t.Tty))
  { case TYarray:
        assert(0);
        /* The following won't work if pointertype == TYnptr and SS != DS */
        t = newpointer(t.Tnext);
        break;

    case TYsptr:
        /* Near pointers point into DS, so if SS != DS we cannot        */
        /* implicitly convert stack pointers to near pointers.          */
        if (pointertype == TYnptr && (config.wflags & WFssneds))
            break;
        goto case TYnptr;
    case TYnptr:
        /* Convert to default pointer type      */
        t = newpointer(t.Tnext);
        break;
    case TYcptr:
        if (pointertype == TYfptr)
            t = newpointer(t.Tnext);
        break;

    case TYnullptr:
        t = tspvoid;
        break;
    case TYschar:
    case TYuchar:
    case TYchar:
    case TYshort:
    case TYbool:
        t = tstypes[TYint];
        break;
    case TYushort:
    case TYchar16:
    case TYwchar_t:
        if (_tysize[TYint] > SHORTSIZE)
            t = tstypes[TYint];                  /* value-preserving rules       */
        else
            t = tstypes[TYuint];
        break;

    case TYfloat:
        t = tstypes[TYdouble];                   /* convert to double            */
        break;

    default:
        return;
  }
  t.Tcount++;
  type_free(*pt);
  *pt = t;
}


/******************************
 * If we are doing line numbers, add line number
 * to current elem and return the resulting tree.
 */

elem *addlinnum(elem *e)
{
    elem_debug(e);
    if (configv.addlinenumbers)
    {
        e.Esrcpos = token_linnum();
        debug if (debugd)
        {
            dbg_printf("line number %d file %p ",e.Esrcpos.Slinnum,e.Esrcpos.Sfilptr);
            dbg_printf("added to elem %x\n",e);
        }
    }
    return e;
}

/*******************************
 * Begin and end a scope.
 */

void fscope_beg()
{
    if (CPP)
        list_prepend(&funcstate._scope,curblock);
}

/*private*/ void fscope_end()
{
    if (funcstate._scope)        /* syntax errors will screw up this list */
    {   block *begscope;

        assert(CPP);
        begscope = list_block(funcstate._scope);
        list_subtract(&funcstate._scope,begscope);
        /* All blocks inclusive between begscope and curblock have      */
        /* curblock as the end of the scope, unless they are the end    */
        /* of a nested scope.                                           */
        while (1)
        {   if (begscope.Bendscope == null)
                begscope.Bendscope = curblock;
            if (begscope == curblock)
                break;
            begscope = begscope.Bnext;
        }
    }
}


/**************************************
 */

/*private*/ void func_state()
{
    int seen = 0;
    const(char)* result = null;
    block *outcurblock;
    elem *eout;
    type *tresult = funcsym_p.Stype.Tnext;

    funcstate.outblock = null;
    funcstate.sresult = null;
    fscope_beg();
    if (tok.TKval == TK_in)
    {
        seen |= 1;
        stoken();
        if (tok.TKval != TKlcur)
            synerr(EM_lcur_exp);                // left curly expected
        if (config.flags5 & CFG5in)
        {
            funcstate.flags |= FSFin;

            // BUG: disallow assignments to parameters

            // Create a separate label symbol table, so __out and __body statements
            // cannot transfer into here
            scope_push(null,cast(scope_fp)&findsy,SCTlabel);

            createlocalsymtab();                        // separate table for other local symbols
            compound_state();
            stoken();
            deletesymtab();
            symbol_free(cast(Symbol *)scope_pop());         // delete label table

            funcstate.flags &= ~FSFin;
        }
        else
        {
            token_free(token_funcbody(false));
            stoken();
        }
    }

    // Add invariant checks
    if (config.flags5 & CFG5invariant)
    {   elem *e;

        e = Funcsym_invariant(funcsym_p, Fctor);
        if (e)
        {
            block_appendexp(curblock, e);
        }

        eout = Funcsym_invariant(funcsym_p, Fdtor);
    }
    else
        eout = null;

    if (tok.TKval == TK_out)
    {
        seen |= 2;
        stoken();
        if (tok.TKval == TKlpar)
        {
            stoken();
            if (tok.TKval != TKident)
                synerr(EM_ident_exp);

            {
                //result = alloca_strdup(tok.TKid);
                const(char)* p = tok.TKid;
                size_t len = strlen(p) + 1;
                char* s = cast(char *)alloca(len);
                result = cast(char *)memcpy(s,p,len);
            }
            stoken();
            chktok(TKrpar, EM_rpar);
        }
    }

    if (tybasic(tresult.Tty) == TYvoid)
    {
        if (result)
            synerr(EM_void_novalue);    // void has no value
        result = null;
    }
    else
    {
        if (!result)
            result = "__result";
    }

    if ((seen & 2 && config.flags5 & CFG5out) || eout)
    {   block *curblocksave;

        // BUG: disallow assignments to parameters

        funcstate.flags |= FSFout;
        curblocksave = curblock;
        funcstate.outblock = curblock = block_calloc();
        curblock.Bsymstart = globsym.top;

        if (eout)
            block_appendexp(curblock, eout);

        scope_push(null,cast(scope_fp)&findsy,SCTlabel);     // create symbol table for labels
        createlocalsymtab();

        // Declare __result symbol
        if (result)
        {
            int retmethod;

            retmethod = exp2_retmethod(funcsym_p.Stype);
            if (retmethod != RET_REGS)
            {
                tresult = newref(tresult);
                type_setty(&tresult, tresult.Tty | mTYconst);
                tresult.Tcount++;
            }
            else
            {
                tresult.Tcount++;
                type_setty(&tresult, tresult.Tty | mTYconst);
            }
            funcstate.sresult = scope_define(result, SCTlocal, SCauto);
            funcstate.sresult.Stype = tresult;
            symbol_add(funcstate.sresult);
        }

        if (seen & 2)
        {   if (config.flags5 & CFG5out)
            {
                if (tok.TKval != TKlcur)
                    synerr(EM_lcur_exp);                // left curly expected
                compound_state();
                stoken();
            }
            else
            {   // Skip & ignore __out clause
                token_free(token_funcbody(false));
                stoken();
            }
        }

        // Add in return __result;
        if (result)
        {
            elem *e;

            e = el_var(funcstate.sresult);
            if (tyref(e.ET.Tty))
            {   type *t;

                t = reftoptr(e.ET);
                el_settype(e, t);
            }
            block_appendexp(curblock, e);
            block_next(BCretexp, null);
            curblock.Bendscope = curblock;

        }

        deletesymtab();
        symbol_free(cast(Symbol *)scope_pop());             // delete label table

        outcurblock = curblock;

        curblock = curblocksave;
        curblock.Bsymstart = globsym.top;

        funcstate.flags &= ~FSFout;
    }
    else if (seen & 2)
    {   // Skip & ignore __out clause
        token_free(token_funcbody(false));
        stoken();
    }
    if (seen)
        chktok(TK_body, EM_body);

    if (funcsym_p.Sfunc.Fflags & Finvariant && !(config.flags5 & CFG5invariant))
    {   // Skip over invariant function body
        token_free(token_funcbody(false));

        // BUG: can't handle function-try-block
    }
    else
    {
        if (CPP)
        {
            if (tok.TKval == TKtry)
            {   // function-try-block
                except_try_state(1);
                token_unget();
            }
            else
            {
                base_initializer(funcsym_p);
                compound_state();
            }
        }
        else
            compound_state();
    }

    if (funcstate.outblock)
    {
        curblock.appendSucc(funcstate.outblock);
        curblock.BC = BCgoto;
        curblock.Bsymend = globsym.top;
        curblock.Bnext = funcstate.outblock;

        curblock = outcurblock;
        curblock.Bsymstart = globsym.top;
    }
    fscope_end();
}

/***************************
 * Do function_statement (or compound_statement).
 * function_statement ::= "{" [ declaration_list ] statement_list "}"
 */

/*private*/ void compound_state()
{
    stoken();                           /* skip '{'                     */
    fscope_beg();
    if (!CPP)
    {
        while (1)
        {
            // Take care of things like:
            //  __debug int x;
            if (tok.TKval == TK_debug)
            {
                stoken();
                if (tok.TKval == TKlpar || tok.TKval == TKlcur)
                {
                    debug_state();
                    break;
                }
                if (config.flags5 & CFG5debug)
                {
                    if (!declaration(1))
                    {
                        debug_state();
                        break;
                    }
                }
                else
                {   // Skip declaration
                    token_free(token_funcbody(false));
                    stoken();
                }
            }
            else if (!declaration(1))
                break;
        }

        while (declaration(1))
        { }
    }
    while (tok.TKval != TKrcur)         /* do statement list            */
        statement(1);

    if (tok.TKval != TKrcur)
    {   synerr(EM_rcur);                // '}' expected
        panic(TKrcur);
    }
    fscope_end();
    if (CPP)
        // Form a new block, so we leave a handy spot to attach any destructors
        block_goto();
}


/******************************
 * Parse and read in an expression.
 * Call destructors on any temporaries introduced by this expression.
 * If (keepresult), retain the result of the expression.
 */

elem *func_expr_dtor(int keepresult)
{
    if (CPP)
    {
        SYMIDX marksi;
        elem *e;

        marksi = globsym.top;
        e = func_expr();                // keep as separate statement from next
        // add in destructors for any temporaries generated
        func_expadddtors(&e,marksi,globsym.top,keepresult != 0,true);
        return e;
    }
    else
        return func_expr();
}

/**************************
 */

elem *func_expr()
{
    if (CPP)
    {
        // poptelem is necessary to collapse out any &func, so we know that
        // we need to generate an actual function if func is inline.
        debug
        {
        elem *e;

        e = expression();
        e = arraytoptr(e);
        e = poptelem(e);
        return e;
        }
        else
        {
        return poptelem(arraytoptr(expression()));
        }
    }
    else
        return arraytoptr(expression());
}

/***************************
 * Parse statement that may introduce a new scope.
 * Input:
 *      flag    1 means create a new symbol table scope
 *              2 means create a new symbol table scope, and link it to the
 *              enclosing one
 */

/*private*/ Srcpos statement_scope(int flag)
{
    if (!CPP)
        return statement(1);

    Srcpos srcpos;

    if (tok.TKval == TKlcur)
        return statement(flag);
    else
    {
        if (flag)               // If the symbol table has not already
                                // been created due to a declaration
                                // that appears on a conditional expression
        {
            createlocalsymtab();
            if (flag == 2)
                scope_end.sctype |= SCTjoin;
        }
        fscope_beg();
        statement(0);
        srcpos.Slinnum = 0;
        fscope_end();
        if (flag)
            deletesymtab();
        return srcpos;
    }
}

/*************************
 * Parse an expression that ends in a ';'
 * (that may contain a variable declaration)
 */

/*private*/ void state_cpp()
{   elem *e;

    if (/*CPP &&*/ isexpression() <= 1) // if it could be a declaration
    {                                   // assume it is a declaration
        declaration(0);                 // declare the variable(s)
    }
    else
    {   uint op;

        e = func_expr_dtor(false);
        e = addlinnum(e);
        op = e.Eoper;
        if (!OTsideff(op) &&
            op != OPcomma &&
            op != OPandand &&
            op != OPoror &&
            op != OPcond &&
            !(op == OPind && tyfloating(e.ET.Tty)) &&
            (!CPP || (op != OPconst && tybasic(e.ET.Tty) != TYstruct)) &&
            tybasic(e.ET.Tty) != TYvoid)
            warerr(WM.WM_valuenotused);            // operator has no effect
        block_appendexp(curblock, e);
        chktok(TKsemi,EM_semi_member);
    }
}

/*************************
 * Parse statement.
 * Input:
 *      flag    !=0 means create a new symbol table scope
 * Returns:
 *      line number of closing }, 0 if there wasn't one
 */

/*private*/ Srcpos statement(int flag)
{
  Srcpos srcpos;

  srcpos.Slinnum = 0;
  while (1)
  {
        if (eecontext.EEimminent)
            eecontext_parse();          // parse debugger expression

        switch (tok.TKval)
        {
            case TKeof:         synerr(EM_exp);
                                err_fatal(EM_eof);
                                assert(0);

            case TKif:          stoken();
                                if_state();
                                break;

            case TKwhile:       while_state();  break;
            case TKdo:          do_state();     break;
            case TKfor:         for_state();    break;
            case TKswitch:      switch_state(); break;
            case TKbreak:       break_state(funcstate.brklabel,EM_bad_break);   break;
            case TKcontinue:    break_state(funcstate.cntlabel,EM_bad_continue);        break;
            case TKreturn:      return_state(); break;
            case TKgoto:        goto_state();   break;
            case TKsemi:        stoken();       break;
            case TK_with:       with_state();   break;
            case TKtry:         except_try_state(0);    break;
            //case TKthrow:     except_throw_state();   break;
            case TK_asm:        funcstate.flags |= asm_state(PFLmasm);
                                break;
            case TKasm:         funcstate.flags |= asm_state(PFLbasm);
                                break;
            // NTEXCEPTIONS
            case TK_try:        nttry_state();          break;
            case TK_leave:      break_state(funcstate.leavelabel,EM_bad_leave); break;

            case TK_debug:      stoken();
                                debug_state();
                                break;

            case TKdefault:     default_label();        continue;
            case TKcase:        case_label();           continue;

            case TKlcur:
                if (!CPP || flag)       // If the symbol table has not already
                                // been created due to a declaration
                                // that appears on a conditional expression
                {   createlocalsymtab();                // create new symbol table
                    if (flag == 2)
                        scope_end.sctype |= SCTjoin;
                }
                if (CPP)
                    block_goto();       // start a new block
                compound_state();
                if (configv.addlinenumbers)
                    srcpos = token_linnum();
                stoken();
                if (!CPP || flag)
                    deletesymtab();
                break;

            case TKrcur:                /* end of statement_list        */
                synerr(EM_statement);   // statement expected
                return srcpos;

            case TKident:               /* could be a label             */
                if (CPP)
                {   char *p;
                    char[32] buffer;    // big enough to handle most cases
                    int len;

                    len = strlen(tok.TKid) + 1;
                    if (len <= buffer.sizeof)
                        p = cast(char *)memcpy(buffer.ptr, tok.TKid, len);
                    else
                    {
                        //p = alloca_strdup(tok.TKid);
                        p = tok.TKid;
                        size_t lenx = strlen(p) + 1;
                        char* s = cast(char *)alloca(lenx);
                        p = cast(char *)memcpy(s,p,lenx);
                    }
                    if (stoken() == TKcolon)
                    {   dolabel(p);
                        stoken();
                        continue;
                    }
                    token_unget();
                    token_setident(p);
                }
                else
                {
                    while (iswhite(xc) || xc == PRE_SPACE)
                        egchar2();
                    if (xc == ':')              // then it's a label
                    {
                        dolabel(tok.TKid);
                        stoken();               // skip over the :
                        stoken();
                        continue;
                    }
                }
                goto default;

            default:                    /* default is expression        */
                state_cpp();            // expression
                break;
        } /* switch */
        break;
  }
  return srcpos;
}


/**************************
 * Do a label.
 * Labels go into the label symbol table.
 */

void dolabel(const(char)* labelident)
{ Symbol *s;

  s = scope_search(labelident,SCTlabel);
  if (s)                                /* if symbol exists             */
  {     assert(s.Sclass == SClabel);
        if (s.Slabel)
        {   synerr(EM_multiple_def,labelident); // symbol already defined
            goto done;
        }

        // Check each goto to this label
        block *blabel = s.Slabelblk;
        for (block *bgoto = blabel.Bgotothread; bgoto; bgoto = bgoto.Bgotothread)
        {
            for (block *b = bgoto.Bgotolist; b != pstate.STgotolist; b = b.Bgotolist)
            {
                if (!b)
                {   // Cannot goto into try or catch block
                    cpperr(EM_gotoforward, &s.Sident[0]);
                    break;
                }
            }
        }
  }
  else                                          /* if it doesn't exist  */
  {     s = scope_define(labelident,SCTlabel,SClabel);  // define it
        s.Slabelblk = block_calloc();
  }
  s.Slabelblk.Bgotolist = pstate.STgotolist;
  s.Slabel = true;
  block_goto(s.Slabelblk);

done:
  ;
}


/**************************
 * Do if statement.
 * if_statement ::= "if" "(" expression ")" statement [ "else" statement ]
 */

/*private*/ void if_state()
{ elem *e;
  block *iflbl;
  block *ellbl;
  block *b;
  char flag = 1;

  chktok(TKlpar,EM_lpar2,"if");
  if (CPP && isexpression() <= 1)       // if it could be a declaration
  {                                     // assume it is a declaration
        flag = 2;
        createlocalsymtab();
        block_goto();
        fscope_beg();

        e = declaration(4);             // declare the variable
  }
  else
        e = addlinnum(func_expr_dtor(true));
  e = cpp_bool(e, 1);
  chknosu(e);
  chkunass(e);
  token_semi();                         /* check for extraneous ;       */
  chktok(TKrpar,EM_rpar);
  iflbl = block_calloc();
  block_appendexp(curblock, e);
  b = curblock;
  block_next(BCiftrue,null);
  b.appendSucc(curblock);    // label for if clause
  b.appendSucc(iflbl);       // else clause
  statement_scope(flag);
  if (tok.TKval == TKelse)
  {
        ellbl = block_calloc();
        block_goto(ellbl,iflbl);
        stoken();
        statement_scope(flag);
        iflbl = ellbl;
  }
  block_goto(iflbl,iflbl);
  if (flag == 2)
  {
        fscope_end();
        block_goto();
        deletesymtab();
  }
}

/**************************
 * Do debug statement.
 * debug_statement ::= "debug" ["(" expression ")"] statement [ "else" statement ]
 */

/*private*/ void debug_state()
{
    if (config.flags5 & CFG5debug)
    {
        if (tok.TKval == TKlpar)
        {
            // this works just like an if_statement
            if_state();
        }
        else
        {
            statement_scope(tok.TKval == TKlcur);
            if (tok.TKval == TKelse)
            {
                stoken();
                token_free(token_funcbody(false));
                stoken();
            }
        }
    }
    else
    {   int flag;

        flag = (tok.TKval == TKlpar);

        // Skip over debug clause
        token_free(token_funcbody(false));
        stoken();

        // Don't skip else clause
        if (tok.TKval == TKelse)
        {
            stoken();
            statement_scope(flag || tok.TKval == TKlcur);
        }
    }
}

/**************************
 * Scope search function for with statement.
 */

Symbol *with_search(const(char)* name,elem *e)
{
    //dbg_printf("with_search('%s',%p)\n",name,e);
    elem_debug(e);
    pstate.STstag = e.ET.Ttag;
    symbol_debug(pstate.STstag);
    return cpp_findmember_nest(&pstate.STstag,name,false);
}

/**************************
 * Do with statement.
 * with_statement ::= "__with" "(" expression ")" statement
 */

/*private*/ void with_state()
{   elem *e;
    type *t;
    elem *ew;

    stoken();
    chktok(TKlpar,EM_lpar2,"__with");
    e = addlinnum(func_expr_dtor(false));
    t = e.ET;
    if (tybasic(t.Tty) == TYstruct)
    {
        chkunass(e);
        token_semi();                   // check for extraneous ;

        // BUG: what if e is volatile?
        e = poptelem(e);
        if (e.Eoper == OPvar)
            ew = e;
        else
        {
            // Take the address of e and assign it to a temporary so
            // we can be assured that e only gets evaluated once

            e = el_unat(OPaddr,type_ptr(e,t),e);
            ew = el_var(symbol_genauto(e.ET));
            e = el_bint(OPeq,ew.ET,ew,e);
            ew = el_unat(OPind,ew.ET.Tnext,el_copytree(ew));
        }

        // ew is the expression whose type defines the scope and
        // provides instance of the object.
        scope_push(ew,cast(scope_fp)&with_search,SCTwith);
    }
    else
    {   synerr(EM_not_struct);          // not a struct or union
        ew = null;
    }
    chktok(TKrpar,EM_rpar);
    block_appendexp(curblock, e);

    statement_scope(1);

    // Form a new block, so we leave a handy spot to attach any destructors
    block_goto();

    if (ew)
    {   if (ew != e)
            el_free(ew);
        assert(scope_end.sctype == SCTwith);
        scope_pop();
    }
}


/*********************************
 * Parses the try-block.
 *  try {
 *      ...
 *  }
 *  catch (X x) {
 *      ...
 *  }
 *  ...
 * Input:
 *      tok     on the "try"
 *      flags   0 regular
 *              1 function-try-block
 * Output:
 *      tok     first token past the try-block
 */

/*private*/ void except_try_state(int flags)
{
    block*              tryblock;
    block*              b;
    char                bEllipsis;
    block*              endlabel;
    block*              cleanuplabel;
    list_t              catchtypes;
    type *              tcv;
    __gshared Classsym * eh_cv;          // class __eh_cv

    if (!(config.flags3 & CFG3eh))
        cpperr(EM_compileEH);                   // EH not enabled

    if (funcsym_p.Sfunc.Fflags3 & Fnteh)      // if used NT exception handling
        cpperr(EM_mix_EH);                      // can't mix them
    funcsym_p.Sfunc.Fflags3 |= Fcppeh;

    stoken();                           // skip over "try"

    if (flags)                          // if function-try-block
        base_initializer(funcsym_p);

    // A compound-statement follows a "try"
    if (tok.TKval != TKlcur)
        synerr(EM_lcur_exp);            // left curly expected

    block_goto();
    tryblock = curblock;
    pstate.STbtry = tryblock;

    tryblock.Bgotolist = pstate.STgotolist;
    pstate.STgotolist = tryblock;

    block_next(BCtry,null);
    tryblock.appendSucc(curblock);

    if (flags)
        funcsym_p.Sfunc.Fbaseblock = curblock;

    statement(1);

    if (flags)
        funcsym_p.Sfunc.Fbaseendblock = curblock;

    pstate.STbtry = tryblock.Btry;             // back to previous value
    block_goto();

    endlabel = block_calloc();
    cleanuplabel = block_calloc();

    curblock.Bsrcpos = getlinnum();
    curblock.appendSucc(endlabel);     // jump past catch blocks

    block_next(BCgoto,null);

    // Create a 'scope' around the handler-seq, ending with the cleanuplabel.
    // The purpose of this scope is so that the destructor for the catchvar
    // gets called upon all exits from the catch blocks.
    createlocalsymtab();
    fscope_beg();
    curblock.appendSucc(cleanuplabel);

    // Create catch variable (as a "class __eh_cv TMP")
    if (!eh_cv)
        eh_cv = cast(Classsym *)scope_search("__eh_cv",SCTglobal);
    symbol_debug(eh_cv);
    tcv = eh_cv.Stype;

    block_next(BCgoto,null);
    tryblock.catchvar = symbol_genauto(tcv);
    // Tell optimizer not to print warning about used-before-initialized
    tryblock.catchvar.Sflags |= SFLnord;
    block *bcx = curblock;
    block_goto();

    bEllipsis = 0;
    catchtypes = null;
    if (tok.TKval != TKcatch)
        cpperr(EM_catch_follows);       // at least one catch block is req'd
    else do
    {   type *tcatch;
        elem *e;

        if (bEllipsis)
        {   cpperr(EM_catch_ellipsis);  // ... must be the last catch clause
            bEllipsis = 0;              // shut up about more catch() clauses
        }
        curblock.Bgotolist = tryblock.Bgotolist;
        pstate.STgotolist = curblock;
        curblock.BC = BCcatch;
        tryblock.appendSucc(curblock);
        bcx.appendSucc(curblock);

        createlocalsymtab();                    // create new symbol table
        fscope_beg();                   // create a new C++ scope

        // Create constructor for catchvar
        e = el_ctor(el_ptr(tryblock.catchvar),null,eh_cv.Sstruct.Sdtor);
        block_appendexp(curblock, e);

        stoken();                       // go past the catch keyword
        chktok(TKlpar,EM_lpar);         // left paren expected

        // Parse exception-declaration
        tcatch = except_declaration(tryblock.catchvar);
        if (!tcatch)                    // if ...
            bEllipsis = 1;
        else
        {
            // Error if:
            //  o       tcatch appears more than once
            //  o       base class appears before derived class
            //  o       ptr/ref to base class appears before ptr/ref derived
            list_t lt;

            for (lt = catchtypes; lt; lt = list_next(lt))
            {   type *t = list_type(lt);

                assert(t);
                type_debug(t);
                if (cpp_typecmp(t,tcatch,2))
                    cpperr(EM_catch_masked);    // masked by previous catch
            }
            list_append(&catchtypes,tcatch);
        }
        list_free(&catchtypes,FPNULL);
        curblock.Bcatchtype = tcatch;

        chktok(TKrpar,EM_rpar);         // Look for the right paren on the catch clause
        if (tok.TKval != TKlcur)        // Make sure that there is a left curly on
            synerr(EM_lcur_exp);        // the catch clause

        b = block_calloc();
        curblock.appendSucc(b);
        block_next(cast(BC)curblock.BC,b);

        statement(0);

        curblock.appendSucc(cleanuplabel);
        block_next(BCgoto,null);

        fscope_end();
        deletesymtab();
    } while (tok.TKval == TKcatch);

    block_goto(cleanuplabel);
    fscope_end();
    deletesymtab();
    block_goto(endlabel,endlabel);
    pstate.STgotolist = tryblock.Bgotolist;    // back to previous value
}

/************************
 * while_statement ::= "while" "(" expression ")" statement
 */

/*private*/ void while_state()
{ elem *e;
  block *brksave;
  block *cntsave;
  block *b;
  char flag = 1;

  brksave = funcstate.brklabel;
  cntsave = funcstate.cntlabel;

  funcstate.cntlabel = block_calloc();
  funcstate.brklabel = block_calloc();
  block_goto(funcstate.cntlabel);               // "continue" label

  stoken();
  chktok(TKlpar,EM_lpar);
  if (CPP && isexpression() <= 1)       // if it could be a declaration
  {
        flag = 0;
        createlocalsymtab();
        fscope_beg();

        e = declaration(4);             // declare the variable
  }
  else
        e = addlinnum(func_expr_dtor(true));

  e = cpp_bool(e, 1);
  chknosu(e);
  chkunass(e);
  token_semi();                         /* check for extraneous ;       */
  chktok(TKrpar,EM_rpar);
  block_appendexp(curblock, e);
  b = curblock;
  block_next(BCiftrue,null);
  b.appendSucc(curblock);
  b.appendSucc(funcstate.brklabel);
  statement_scope(flag);
  block_goto(funcstate.cntlabel,funcstate.brklabel);

  funcstate.brklabel = brksave;
  funcstate.cntlabel = cntsave;
  if (!flag)
  {
        fscope_end();
        block_goto();
        deletesymtab();
  }
}

/***********************
 * do_statement ::= "do" statement "while" "(" expression ")" ";"
 */

/*private*/ void do_state()
{ elem *e;
  block *brksave;
  block *cntsave;
  block *dolabel;
  Srcpos srcpos;
  block *b;

  brksave = funcstate.brklabel;
  cntsave = funcstate.cntlabel;

  dolabel = block_calloc();
  funcstate.cntlabel = block_calloc();
  funcstate.brklabel = block_calloc();
  block_goto(dolabel);
  stoken();
  statement_scope(1);
  chktok(TKwhile,EM_while);
  chktok(TKlpar,EM_lpar);
  e = addlinnum(func_expr_dtor(true));
  e = cpp_bool(e, 1);
  chknosu(e);
  chkunass(e);
  chktok(TKrpar,EM_rpar);
  srcpos = token_linnum();
  chktok(TKsemi,EM_semi_member);
  block_goto(funcstate.cntlabel);
  block_appendexp(curblock, e);
  b = curblock;
  /* Convert to:
        dolabel:
            statement;
        cntlabel:
            if (!e) goto brklabel;
            goto dolabel;               // this block provides spot for dtors
        brklabel:
            ...
   */
  block_next(BCiftrue,null);
  b.appendSucc(curblock);
  b.appendSucc(funcstate.brklabel);
  curblock.Bsrcpos = srcpos;
  curblock.appendSucc(dolabel);
  block_next(BCgoto,funcstate.brklabel);

  funcstate.brklabel = brksave;
  funcstate.cntlabel = cntsave;
}

/************************
 * for_statement ::= "for" "("  [ expression-1 ] ";"
 *                              [ expression-2 ] ";"
 *                              [ expression-3 ] ")" statement
 */

/*private*/ void for_state()
{   elem* e2,e3;
    block *brksave;
    block *cntsave;
    block *forlabel;
    Srcpos srcpos;
    char flag = 1;

    brksave = funcstate.brklabel;
    cntsave = funcstate.cntlabel;

    funcstate.brklabel = block_calloc();
    funcstate.cntlabel = block_calloc();
    forlabel = block_calloc();

    if (config.flags4 & CFG4forscope)
    {   flag = 0;
        createlocalsymtab();
        block_goto();
        fscope_beg();
    }

    stoken();
    chktok(TKlpar,EM_lpar);
    if (tok.TKval == TKsemi)
        stoken();
    else
        state_cpp();

  block_goto(forlabel);
  if (tok.TKval == TKsemi)
        stoken();
  else
  {
        if (CPP && isexpression() <= 1) // if it could be a declaration
        {
            if (flag)
            {   flag = 0;
                createlocalsymtab();
                fscope_beg();
            }
            e2 = declaration(4);        // declare the variable
        }
        else
            e2 = addlinnum(func_expr_dtor(true));
        e2 = cpp_bool(e2, 1);
        chknosu(e2);
        chkunass(e2);
        chktok(TKsemi,EM_semi_member);
        {   block *b;

            block_appendexp(curblock, e2);
            b = curblock;
            block_next(BCiftrue,null);
            b.appendSucc(curblock);
            b.appendSucc(funcstate.brklabel);
        }
  }

  /* Be careful to add in destructors immediately for the
     3rd expression.
   */
  e3 = (tok.TKval != TKrpar) ? func_expr_dtor(false) : null;

  token_semi();                         /* check for extraneous ;       */
  chktok(TKrpar,EM_rpar);
  srcpos = statement_scope(flag);

  block_goto(funcstate.cntlabel);
  if (e3)                               /* if there was a 3rd expression */
  {
        if (configv.addlinenumbers)
        {
            if (srcpos.Slinnum)
                e3.Esrcpos = srcpos;
            //else
                //addlinnum(e3);                /* add this line number to it   */
        }
        block_appendexp(curblock, e3);
  }
  block_goto(forlabel,funcstate.brklabel);

  funcstate.brklabel = brksave;
  funcstate.cntlabel = cntsave;

  if (!flag)
  {
        fscope_end();
        block_goto();
        deletesymtab();
  }
}

/**************************
 * switch_statement ::= "switch" "(" expression ")" statement
 */

/*private*/ void switch_state()
{ elem *e;
  block *brksave;
  casedat *casesave;
  int i;
  tym_t tym;
  block *sw;
  targ_llong *pu;
  char flag = 1;

  stoken();
  chktok(TKlpar,EM_lpar);
  if (CPP && isexpression() <= 1)       // if it could be a declaration
  {
        flag = 2;
        createlocalsymtab();
        block_goto();
        fscope_beg();

        e = declaration(4);             // declare the variable
  }
  else
        e = func_expr_dtor(true);

    if (CPP)
        funcstate.flags |= FSFskip;
    brksave = funcstate.brklabel;
    funcstate.brklabel = block_calloc();
    casesave = funcstate.caseptr;
    funcstate.caseptr = cast(casedat *) MEM_PARF_CALLOC(casedat.sizeof);
    funcstate.caseptr.Cdef = null;     // no default seen yet

    if (CPP && !cpp_cast(&e,tstypes[TYint],1))   // look for user-defined conv to int
        cpp_cast(&e,tstypes[TYlong],1);
    tym = tybasic(e.ET.Tty);
    e = convertchk(e);                  // integral promotions
    if (!tyintegral(tym))
        synerr(EM_integral);
    if (tym == TYenum)
        e = _cast(e,tstypes[TYint]);
    funcstate.caseptr.Cty = tybasic(e.ET.Tty);
    if (_tysize[TYint] == LONGSIZE)
    {   if (funcstate.caseptr.Cty == TYint)
            funcstate.caseptr.Cty = TYlong;
        if (funcstate.caseptr.Cty == TYuint)
            funcstate.caseptr.Cty = TYulong;
    }
    e = addlinnum(e);
    block_appendexp(curblock, e);
    sw = curblock;
    block_next(BCswitch,null);
    token_semi();                       // check for extraneous ;
    chktok(TKrpar,EM_rpar);

    statement(flag);

    if (funcstate.caseptr.Cdef == null)                // if there wasn't a default
        funcstate.caseptr.Cdef = funcstate.brklabel;   // then it is 'break'
    sw.appendSucc(funcstate.caseptr.Cdef);
    sw.Bswitch = cast(targ_llong *)
        MEM_PH_MALLOC(targ_llong.sizeof * (funcstate.caseptr.Cindex + 1));
    pu = sw.Bswitch;
    *pu++ = funcstate.caseptr.Cindex;
    for (i = 0; i < funcstate.caseptr.Cindex; i++)
    {   *pu++ = funcstate.caseptr.Ccases[i].Cval;
        sw.appendSucc(funcstate.caseptr.Ccases[i].Clab);
    }

    block_goto(funcstate.brklabel);

    MEM_PARF_FREE(funcstate.caseptr.Ccases);
    MEM_PARF_FREE(funcstate.caseptr);
    funcstate.caseptr = casesave;
    funcstate.brklabel = brksave;

    if (flag == 2)
    {
        fscope_end();
        block_goto();
        deletesymtab();
    }
}

/**************************
 * case_statement ::= "case" constant_expression ":" statement
 */

/*private*/ void case_label()
{ elem *e;
  int i;
  tym_t tym;
  tym_t ctym;
  targ_llong val;

  stoken();
  e = func_expr();
  if (!funcstate.caseptr)
  {     synerr(EM_not_switch);          // not in a switch statement
        goto done;
  }
  tym = tybasic(e.ET.Tty);
  ctym = funcstate.caseptr.Cty;
  if (config.ansi_c && tym <= TYullong && tym > ctym)
        synerr(EM_const_case);          // type too big
  e = typechk(e,tstypes[TYlong]);
  e = poptelem(e);
  if (funcstate.caseptr.Cindex >= funcstate.caseptr.Cmax)
  {
        funcstate.caseptr.Cmax += 50;
        funcstate.caseptr.Ccases = cast(CASES *)
            MEM_PARF_REALLOC(funcstate.caseptr.Ccases,funcstate.caseptr.Cmax * CASES.sizeof);
  }
  if (e.Eoper == OPconst && tyintegral(e.ET.Tty))
        val = e.EV.Vllong;             /* the case value               */
  else if (CPP && e.Eoper == OPvar && e.EV.Vsym.Sflags & SFLvalue)
  {     symbol_debug(e.EV.Vsym);
        val = el_tolong(e.EV.Vsym.Svalue); /* the case value       */
  }
  else
  {     synerr(EM_num);                 // number expected
        goto done;
  }

  switch (_tysize[ctym])
  {     case 1:
            if ((val & 0xFFFFFF00) != 0 && (val & 0xFFFFFF00) != 0xFFFFFF00)
                synerr(EM_const_case);          // type too big
            break;
        case 2:
            if ((val & 0xFFFF0000) != 0 && (val & 0xFFFF0000) != 0xFFFF0000)
                synerr(EM_const_case);          // type too big
            break;
        default:
            break;
  }

  for (i = funcstate.caseptr.Cindex; --i >= 0;)
        if (val == funcstate.caseptr.Ccases[i].Cval)
        {   synerr(EM_mult_case,val);           // case value already used
            goto done;
        }
    block_goto();
    funcstate.caseptr.Ccases[funcstate.caseptr.Cindex].Cval = val;
    funcstate.caseptr.Ccases[funcstate.caseptr.Cindex].Clab = curblock;
    funcstate.caseptr.Cindex++;

done:
  chktok(TKcolon,EM_colon);
  el_free(e);
}

/*************************
 * default_state ::= "default" ":" statement
 */

/*private*/ void default_label()
{
  stoken();
  chktok(TKcolon,EM_colon);
  if (!funcstate.caseptr)
        synerr(EM_not_switch);          // not in a switch statement
  else
  {
        if (funcstate.caseptr.Cdef)
            synerr(EM_mult_default);    // default is already used
        funcstate.caseptr.Cdef = block_calloc();
        block_goto(funcstate.caseptr.Cdef);
  }
}

/*************************
 * break_statement ::= "break" ";"
 * continue_statement ::= "continue" ";"
 * leave_statement ::= "__leave" ";"
 */

/*private*/ void break_state(block *label,int errnum)
{ Srcpos srcpos;

  srcpos = token_linnum();
  stoken();
  chktok(TKsemi,EM_semi_member);
  if (label)
  {
        curblock.Bsrcpos = srcpos;
        curblock.appendSucc(label);
        block_next(BCgoto,null);
  }
  else
        synerr(errnum);                 // no break/continue/leave address
}

/**************************
 * return_statement ::= "return" [ expression ] ";"
 */

/*private*/ void return_state()
{ elem *e;
  type *tf = funcsym_p.Stype;
  type *tr = tf.Tnext;
  tym_t tym = tybasic(tr.Tty);
  tym_t ety;

  if (tf.Tty & mTYnaked)
        synerr(EM_nakedret);            // return not allowed for naked function
  if (funcstate.flags & (FSFin | FSFout))
        synerr(EM_return);              // return not allowed in __in or __out
  stoken();
  if (tok.TKval != TKsemi)              /* if expression                */
  {
        int retmethod;                  /* function return method       */
        Symbol *s = null;

        if (CPP)
        {
            e = expression();
            if (!tyref(tr.Tty))
                e = arraytoptr(e);
            e = poptelem(e);
        }
        else
            e = func_expr_dtor(true);
        funcstate.flags |= FSFretexp;   // seen a return exp;
        if (
            // This is here to support things like:
            //  return () (x += 3);
            tybasic(e.ET.Tty) != TYvoid &&
            (tym == TYvoid
            || funcsym_p.Sfunc.Fflags & (Fctor | Fdtor | Finvariant)
           ))
                synerr(EM_void_novalue);        // void has no value
        if (funcsym_p.Sfunc.Fflags3 & F3badoparrow)
            e = el_bint(OPcomma,tr,e,el_longt(tr,0));
        e = typechk(e,tr);

        if (e.Eoper == OPrelconst && sytab[e.EV.Vsym.Sclass] & SCSS)
            warerr(WM.WM_ret_auto, &e.EV.Vsym.Sident[0]);  // returning address of automatic
        ety = tybasic(e.ET.Tty);

        /* If the return value is passed back in something other than
           registers
         */
        retmethod = exp2_retmethod(tf);
        if (retmethod != RET_REGS && !errcnt)
        {
            // Rewrite tree to be:
            //  tmp = str; return &tmp;

            if (CPP)
            {
                type *tclass;
                list_t arglist;
                elem *es;
                Symbol *stmp;

                tclass = e.ET;
                if (retmethod == RET_STACK)
                {   /* Write *tmp=str; return tmp;      */
                    SYMIDX si;

                    /* Find hidden parameter, stmp      */
                    stmp = funcstate.shidden;

                    /* See if we can do the 'named return value' optimization */
                    if (e.Eoper == OPvar && funcstate.namret.can)
                    {   Symbol *sstr = e.EV.Vsym;

                        symbol_debug(sstr);
                        if ((sstr.Sclass == SCauto ||
                             sstr.Sclass == SCregister) &&
                            (!funcstate.namret.s || funcstate.namret.s == sstr) &&
                            e.EV.Voffset == 0
                           )
                        {
                            funcstate.namret.s = sstr;
                            funcstate.namret.sret = stmp;
                        }
                        else
                            namret_term();
                    }
                    else
                        namret_term();          /* can't do the optimization */

                    if (!funcstate.namret.can && ety == TYstruct)
                    {   list_t arglist2;

                        /* Try to minimize temporaries generated        */
                        arglist2 = null;
                        list_append(&arglist2,e);
                        e = init_constructor(stmp,stmp.Stype.Tnext,arglist2,0,2,null);
                        elem_debug(e);
                        goto L3;
                    }
                    else
                        es = el_var(stmp);
                }
                else
                {   /* Generate a static struct to copy the result into */

                    /*dbg_printf("Creating symbol %d\n",globsym.top);*/
                    assert(retmethod == RET_STATIC);
                    Symbol *s2 = symbol_generate(SCstatic,e.ET);
                    s2.Sflags |= SFLnodtor;     /* don't add dtors in later */

                    scope dtb = new DtBuilder();
                    dtb.nzeros(type_size(s2.Stype));
                    s2.Sdt = dtb.finish();

                    symbol_keep(s2);
                    es = el_ptr(s2);
                }

                if (ety == TYstruct)
                {
                    arglist = null;
                    list_append(&arglist,e);
                    e = cpp_constructor(es,tclass,arglist,null,null,8);
            L3:
                    if (!e)
                        e = el_longt(newpointer(tstypes[TYint]),0);      /* error recovery */
                    //else if (retinreg)          /* ret class in registers */
                        //e = el_unat(OPind,tclass,e);
                }
                else if (retmethod == RET_STACK)
                                        /* C floats returned in regs */
                {                       /* pascal - ptr to floating result */
                    /* Generate (*stmp = e),stmp        */
                    e = el_bint(OPeq,e.ET,el_unat(OPind,e.ET,es),e);
                    e = el_bint(OPcomma,es.ET,e,el_var(stmp));
                }
            }
            else // !CPP
            {
                elem *es;

                if (retmethod == RET_STACK)
                {   /* Write *shidden=str; return shidden;      */
                    int op;

                    es = el_unat(OPind,e.ET,el_var(funcstate.shidden));
                    op = (ety == TYstruct) ? OPstreq : OPeq;
                    es = el_bint(op,e.ET,es,e);
                    e = el_var(funcstate.shidden);
                }
                else
                {   /* Generate a static s, and write
                                s = str; return &s;
                     */
                    assert(retmethod == RET_STATIC);
                    s = symbol_generate(SCstatic,e.ET);
                    symbol_keep(s);
                    datadef(s);
                    es = el_var(s);
                    es = el_bint(OPstreq,es.ET,es,e);
                    e = el_ptr(s);
                    e = _cast(e,newpointer(s.Stype));   /* to default ptr type */
                }
                block_appendexp(curblock, es);                  // tmp = str;
            }
            if (retmethod == RET_STACK)
            {
                type *tret;

                {
                    tret = newpointer(e.ET.Tnext);
                    e = _cast(e,tret);
                    if (CPP)
                    {
                        e = poptelem(e);
                        if (e.Eoper == OPinfo)
                            e = el_selecte2(e);
                        if (funcstate.namret.s)
                            list_append(&funcstate.namret.explist,e);
                    }
                }
            }
        }

        if (funcstate.sresult)
        {   // Copy function return value into __result variable
            // for use by __out(__result) { ... } block
            int op;
            elem *eresult;

            op = (tybasic(e.ET.Tty) == TYstruct) ? OPstreq : OPeq;
            eresult = el_var(funcstate.sresult);
            type_settype(&eresult.ET, e.ET);
            e = el_bint(op, e.ET, eresult, e);
        }

        /* this should really be up before the struct assign code       */
        e = addlinnum(e);               /* add line number info         */

        block_appendexp(curblock, e);
  }
  else
  {
        if (CPP &&
            tym != TYvoid &&
            !(funcsym_p.Sfunc.Fflags & (Fctor | Fdtor | Finvariant))
            && (block_last && block_last.BC != BCasm)
           )
        {
            // Interrupt functions return values in other ways than using
            // the 'return' statement.
            if (tybasic(tf.Tty) != TYifunc)
                synerr(EM_no_ret_value,cpp_prettyident(funcsym_p));     // return value expected
        }
        curblock.Bsrcpos = token_linnum();
        ety = TYvoid;
  }
    if (funcstate.outblock)
    {   // Commandeer the result to go to outblock
        curblock.appendSucc(funcstate.outblock);
        block_next(BCgoto, null);
    }
    else
    {
        block_next((ety == TYvoid) ? BCret : BCretexp,null);
    }
    chktok(TKsemi,EM_semi_member);
}

/*****************************
 * Print message about no return value.
 */

void func_noreturnvalue()
{   static Symbol *lastf;

    if (lastf != funcsym_p)

    // Interrupt functions return values in other ways than using
    // the 'return' statement.
    if (tybasic(funcsym_p.Stype.Tty) != TYifunc)
    {   char *p = cpp_prettyident(funcsym_p);

        if (config.ansi_c)
            synerr(EM_implied_ret,p);   // implied return
        else
            warerr(WM.WM_implied_ret,p);   // implied return
    }
    lastf = funcsym_p;          // no duplicate messages
}

/**************************
 * goto_statement ::= "goto" identifier ";"
 */

/*private*/ void goto_state()
{ Symbol *s;

  if (CPP)
        funcstate.flags |= FSFskip;
  stoken();
  if (tok.TKval != TKident)
        synerr(EM_ident_exp);                   // identifier expected
  else
  {     s = scope_search(tok.TKid,SCTlabel);
        if (s)
        {
version (all)
            assert(s.Sclass == SClabel);
else
{
            if (s.Sclass != SClabel)
            {   synerr(EM_multiple_def,tok.TKid);
                stoken();
                stoken();
                return;
            }
}
            // If label is already defined, ensure we are not doing
            // a goto into a try or catch block
            if (s.Slabel)
            {
                block *blabel = s.Slabelblk.Bgotolist;
                for (block *b = pstate.STgotolist; b != blabel; b = b.Bgotolist)
                {
                    if (!b)
                    {   // Cannot goto into try or catch block
                        cpperr(EM_gotoforward, &s.Sident[0]);
                        break;
                    }
                }
            }
        }
        else
        {   s = scope_define(tok.TKid,SCTlabel,SClabel);
            s.Slabelblk = block_calloc();      // dunno where it is yet
        }
        if (!s.Slabel)
        {   // Create threaded list of blocks we need to check when
            // the label is found.
            curblock.Bgotothread = s.Slabelblk.Bgotothread;
            s.Slabelblk.Bgotothread = curblock;

            curblock.Bgotolist = pstate.STgotolist;
        }
        curblock.Bsrcpos = token_linnum();
        curblock.appendSucc(s.Slabelblk);
        block_next(BCgoto,null);
        stoken();
  }
  chktok(TKsemi,EM_semi_member);
}


static if (NTEXCEPTIONS)
{

/**********************************
 * Parse try-finally and try-except statements.
 */

/*private*/ void nttry_state()
{
    block *leavesave;
    block *leavelabel;
    block *b;
    block *tryblock;
    elem *e;
    Symbol *s;
    int lastindex;

    stoken();
    if (config.exe != EX_WIN32)
    {   tx86err(EM_try_needs_win32);            // only for NT
        return;
    }

    if (funcsym_p.Sfunc.Fflags3 & Fcppeh)     // if used C++ exception handling
        cpperr(EM_mix_EH);                      // can't mix them

    nteh_declarvars(null);                      // declare frame variables

    leavesave = funcstate.leavelabel;
    funcstate.leavelabel = block_calloc();
    leavelabel = funcstate.leavelabel;

    block_goto();
    tryblock = curblock;
    tryblock.BC = BC_try;
    lastindex = funcstate.scope_index;
    tryblock.Blast_index = lastindex;
    funcstate.scope_index = tryblock.Bscope_index = funcstate.next_index++;
    pstate.STbtry = tryblock;

    // Set the current scope index
    s = nteh_contextsym();
    e = el_var(s);
    e.EV.Voffset = nteh_offset_sindex_seh();        // offset of sindex
    el_settype(e,tstypes[TYint]);
    e = el_bint(OPeq,tstypes[TYint],e,el_longt(tstypes[TYint],tryblock.Bscope_index));
    block_appendexp(curblock, e);

    block_next(cast(BC)curblock.BC,null);
    tryblock.appendSucc(curblock);

    statement_scope(1);                  // guarded statement
    e = el_var(s);
    e.EV.Voffset = nteh_offset_sindex_seh();        // offset of sindex
    el_settype(e,tstypes[TYint]);
    e = el_bint(OPeq,tstypes[TYint],e,el_longt(tstypes[TYint],lastindex));
    block_appendexp(curblock, e);
    funcstate.leavelabel = leavesave;
    curblock.appendSucc(leavelabel);
    pstate.STbtry = tryblock.Btry;

    if (tok.TKval == TK_except)
    {   elem *efilter;
        block *be;
        block *bfiltersave;

        block_next(BCgoto,null);

        // Back out the Btry setting, which is for __try/__finally
        for (be = tryblock; be != curblock; be = be.Bnext)
        {
            if (be.Btry == tryblock)
                be.Btry = pstate.STbtry;
        }

        stoken();
        chktok(TKlpar,EM_lpar);
        bfiltersave = pstate.STbfilter;
        pstate.STbfilter = curblock;
        pstate.STinfilter++;
        efilter = func_expr_dtor(true);         // exception filter
        pstate.STinfilter--;
        efilter = typechk(efilter,tstypes[TYint]);       // cast to int
        efilter = addlinnum(efilter);
        block_appendexp(curblock, efilter);
        token_semi();                           // check for extraneous ;
        chktok(TKrpar,EM_rpar);
        tryblock.appendSucc(curblock);

        block_next(BC_filter,null);
        be = curblock;
        tryblock.appendSucc(be);

        block_next(BC_except,null);
        be.appendSucc(curblock);

        e = el_var(s);
        e.EV.Voffset = nteh_offset_sindex_seh();    // offset of sindex
        el_settype(e,tstypes[TYint]);
        e = el_bint(OPeq,tstypes[TYint],e,el_longt(tstypes[TYint],lastindex));
        block_appendexp(curblock, e);

        pstate.STinexcept++;
        statement_scope(1);              // exception handler
        pstate.STinexcept--;

        pstate.STbfilter = bfiltersave;
        funcstate.scope_index = lastindex;

        block_goto(leavelabel);
    }
    else if (tok.TKval == TK_finally)
    {   block *bf;

        block_next(BCgoto,leavelabel);

version (none)
{
        // if there was a return in the BC_try, we need
        // to call _local_unwind2(&s,-1);
        // so that the __finally code gets executed.

        // For all guarded blocks, set BFLunwind on return blocks
        for (bf = tryblock; bf != curblock; bf = bf.Bnext)
        {
            if (bf.BC == BCret || bf.BC == BCretexp)
                bf.Bflags |= BFLunwind;        // call _local_unwind2()
        }
}

        funcstate.scope_index = lastindex;
        stoken();
        tryblock.appendSucc(leavelabel);
        bf = curblock;
        block_next(BC_finally,null);
        bf.appendSucc(curblock);
        statement_scope(1);              // termination handler

        block_goto();

        b = block_calloc();
        bf.appendSucc(b);
        curblock.appendSucc(b);
        block_next(BC_ret,b);
    }
    else
        tx86err(EM_finally_or_except);          // finally or except expected
}

}


/*********************************
 * Determine if there is any path from startblock to bend that does not
 * also go through binit.
 * Returns:
 *      !=0 if so
 */

/*private*/ int func_anypathx(block *bstart,block *binit,block *bend)
{   list_t bl;

L1:
    bstart.Bflags |= BFLmark;
    if (bstart == binit)
        return 0;
    if (bstart == bend)
        return 1;

    for (bl = bstart.Bsucc; bl; bl = list_next(bl))
    {   block *b = list_block(bl);

        if (b.Bflags & BFLmark)
            continue;
        if (!list_next(bl))     // if no more successors to look at
        {   bstart = b;
            goto L1;            // tail recursion to speed things up
        }
        if (func_anypathx(b,binit,bend))
            return 1;
    }
    return 0;
}

/*private*/ int func_anypath(block *bend,block *binit)
{   block *b;

    // Mark all blocks as unvisited
    for (b = startblock; b; b = b.Bnext)
        b.Bflags &= ~BFLmark;
    return func_anypathx(startblock,binit,bend);
}

/*******************************
 * Add destructors for objects that go out of scope.
 */

enum DBG = false;

/*private*/ void func_adddestructors()
{   uint si;

    debug if (0)
    {   block *b;
        for (b = startblock; b; b = b.Bnext)
            WRblock(b);
    }

    // Need to do initialization skip testing if goto's or switches
    if (!(funcstate.flags & FSFskip))
    {
        // If no symbols needing destructors, then skip this
        for (si = 0; 1; si++)
        {   Symbol *s;
            type *tclass;

            if (si == globsym.top)
                return;                 // no symbols needing destructors
            s = globsym.tab[si];
            if (s.Sflags & SFLnodtor ||        /* if already called dtor */
                s.Sclass == SCstatic)  /* statics are destroyed elsewhere */
                continue;
            tclass = type_arrayroot(s.Stype);
            if (tybasic(tclass.Tty) == TYstruct && tclass.Ttag.Sstruct.Sdtor)
                break;
        }
    }

    if (DBG) dbg_printf("func_adddestructors()\n");
    if (!errcnt)        /* if errors occurred, data structures may be bad */
        func_doblock(startblock);
}

/*private*/ void func_doblock(block *bstart)
{
    if (bstart.Bnext)
        func_doblock(bstart.Bnext);

    {   block *b;

        if (DBG) dbg_printf("bstart = %d, Bendscope = %d BC = ",
         bstart.Bblknum,bstart.Bendscope.Bblknum);
        if (DBG) WRBC(bstart.BC);
        if (DBG) dbg_printf("\n");

        /* Unmark all blocks    */
        for (b = startblock; b; b = b.Bnext)
            b.Bflags &= ~BFLvisited;

        func_lookat(bstart,bstart);
    }
}

/*****************************
 * Check if b has any symbols that need destructors.
 * Recursively check successors.
 * Input:
 *      bstart  Start of scope
 */

/*private*/ void func_lookat(block *bstart,block *b)
{   int needdtor;
    list_t bl;
    int startnum = bstart.Bblknum;
    int endnum = bstart.Bendscope.Bblknum;
    assert(startnum <= endnum);

    if (DBG) dbg_printf("\tfunc_lookat() startnum=%d num=%d endnum=%d\n",startnum,b.Bblknum,endnum);
    //dbg_printf("\tbstart.Bsymstart=%d, bstart.Bsymend=%d\n", bstart.Bsymstart, bstart.Bsymend);
    assert(bstart.Bsymstart <= bstart.Bsymend);

    needdtor = 1;
    b.Bflags |= BFLvisited;    /* mark block b as visited              */
    for (bl = b.Bsucc; bl; bl = list_next(bl))
    {   block *bs = list_block(bl);

//      if (bs.BC == BCasm)
//          continue;

        /* If successor bs is out of scope      */
        if (bs.Bblknum <= startnum || endnum < bs.Bblknum)
        {
            if (DBG) dbg_printf("\t\tsucc %d for block %d is out of scope\n",bs.Bblknum,b.Bblknum);
            assert(needdtor || b.BC == BCasm); // can't have successors both in and out
            needdtor = 2;
        }
        else                    /* successor is in scope                */
        {
            if (DBG) dbg_printf("\t\tsucc %d for block %d is in scope\n",bs.Bblknum,b.Bblknum);
            assert(needdtor != 2 || b.BC == BCasm);
            needdtor = 0;
            if (!(bs.Bflags & BFLvisited))     /* not looked at it?    */
                func_lookat(bstart,bs);
        }
    }
    if (b.BC == BCasm)
        return;
    if (needdtor)
    {
        func_adddtors(b,bstart.Bsymstart,bstart.Bsymend);

        /* Check to see if there are any cases where an initialization
           was skipped.
         */
        if (funcstate.flags & FSFskip &&        // any goto's or switch's?
            bstart.Binitvar &&                 // an initialized variable?
            func_anypath(b,bstart))             // a skip?
            cpperr(EM_skip_init,cpp_prettyident(bstart.Binitvar));     // skipped initialization
    }
    else
    {
        if (DBG) dbg_printf("\t\tNo destructors needed for %d\n",b.Bblknum);
    }
}

/********************************
 * Append destructors for (sistart <= si < siend) to block b.
 */

/*private*/ void func_adddtors(block *b,SYMIDX sistart,SYMIDX siend)
{   bool keepresult;

    assert(sistart <= siend);
    if (DBG) dbg_printf("\t\tout of scope, destructors needed for %d\n",b.Bblknum);

    /* Append e to elems forming this block     */
    switch (b.BC)
    {   case BCgoto:
        case BCret:
        case BCtry:
        case BCcatch:

        // NTEXCEPTIONS
        case BC_try:
        case BC_finally:
        case BC_ret:
        case BC_except:

            keepresult = false;
            break;
        case BCiftrue:          // Now that this can declare variables
                                // on the switch statement, this is OK
        case BCswitch:
        case BCretexp:

        // NTEXCEPTIONS
        case BC_filter:

            keepresult = true;
            break;
        default:
            debug dbg_printf("b.BC == %d\n",b.BC);
            debug WRBC(b.BC);
            assert(0);
    }
    func_expadddtors(&b.Belem,sistart,siend,keepresult,false);
}

/****************************************
 * Add deferred destructors for conditionally executed code,
 * such as the e2 in (e1 ? e2 : e3).
 */

void func_conddtors(elem **pe, SYMIDX sistart, SYMIDX siend)
{   elem *ed;
    SYMIDX si;
    elem *e = *pe;

    if (DBG) dbg_printf("func_conddtors(sistart=%d, siend=%d)\n", sistart, siend);

    // Construct ed, which initializes the pointers to the temps
    ed = null;
    assert(sistart <= siend);
    for (si = siend; si-- != sistart;)
    {
        Symbol *s = globsym.tab[si];
        type *t = s.Stype;
        type *tclass;

        if (DBG) dbg_printf("\t\t\tLooking at symbol %d, '%s'\n",si,&s.Sident[0]);
        if (s.Sflags & SFLnodtor ||    // if already called dtor
            s.Sclass == SCstatic)      // statics are destroyed elsewhere
            continue;
        tclass = type_arrayroot(t);
        if (tybasic(tclass.Tty) == TYstruct && tclass.Ttag.Sstruct.Sdtor)
        {   elem *edtor;
            elem *enelems;
            elem *eptr;
            Symbol *sa;

            if (DBG) dbg_printf("\t\t\tAdding destructor for '%s'\n",&s.Sident[0]);
            eptr = el_ptr(s);
            sa = symbol_genauto(eptr.ET);
            edtor = el_bint(OPeq,sa.Stype,el_var(sa),eptr);
            s.Sflags |= SFLdtorexp;
            s.Svalue = el_var(sa);
            ed = el_combine(ed,edtor);
        }
    }

    // Prepend ed to e
    e = el_combine(ed, e);
    *pe = e;
}

/********************************
 * Add destructors for (sistart <= si < siend) to expression *pe.
 * If (keepresult), retain the result of the expression.
 * If (onlydtor), prevent further dtors from being called for the symbols.
 */

void func_expadddtors(elem **pe,
        SYMIDX sistart,SYMIDX siend,
        bool keepresult,bool onlydtor)
{   elem *ed;
    SYMIDX si;
    elem *e = *pe;

    //printf("func_expadddtors(sistart=%d, siend=%d, keepresult=%d, onlydtor=%d)\n",sistart,siend,keepresult,onlydtor);

    /* Construct ed,                                            */
    /* which calls the destructors for those symbols.           */
    /* Remember to go through the symbols in reverse order!     */
    ed = null;
    assert(sistart <= siend);
    for (si = siend; si-- != sistart;)
    {
        Symbol *s = globsym.tab[si];
        type *t = s.Stype;
        type *tclass;

        //printf("\t\t\tLooking at symbol %d, '%s'\n",si,&s.Sident[0]);
        if (s.Sflags & SFLnodtor ||    /* if already called dtor */
            s.Sclass == SCstatic)      /* statics are destroyed elsewhere */
            continue;
        if (onlydtor)
        {
            s.Sflags |= SFLnodtor;     /* don't add dtors in later */
        }
        tclass = type_arrayroot(t);
        if (tybasic(tclass.Tty) == TYstruct && tclass.Ttag.Sstruct.Sdtor)
        {   elem *edtor;
            elem *enelems;
            elem *eptr;

            if (DBG) dbg_printf("\t\t\tAdding destructor for '%s'\n",&s.Sident[0]);
            enelems = el_nelems(t);
version (all)
{
            /* Add in conditional destructor expression */
            if (s.Sflags & SFLdtorexp)
            {   elem *ei;

                eptr = el_ptr(s);
                edtor = cpp_destructor(tclass,eptr,enelems,DTORmostderived);
                edtor = el_bint(OPandand,tstypes[TYint],el_copytree(s.Svalue),edtor);
                ei = el_bint(OPeq,eptr.ET,el_copytree(s.Svalue),el_longt(eptr.ET,0));
                e = el_combine(ei,e);
            }
            else
            {
                eptr = el_ptr(s);
                edtor = cpp_destructor(tclass,eptr,enelems,DTORmostderived);
            }
}
else
{
            eptr = el_ptr(s);
            edtor = cpp_destructor(tclass,eptr,enelems,DTORmostderived);

            /* Add in conditional destructor expression */
            if (s.Sflags & SFLdtorexp)
                edtor = el_bint(OPandand,tstypes[TYint],el_copytree(s.Svalue),edtor);
}
            ed = el_combine(ed,edtor);
        }
    }

    /* Append ed to e   */
    if (keepresult && e && ed && tybasic(e.ET.Tty) != TYvoid)
        appenddesr(&e,ed);
    else
        e = el_combine(e,ed);
    *pe = e;
}


/*********************
 * Append destructor elem, de, to pe without disturbing the final
 * result of the elem pe.
 */

/*private*/ void appenddesr(elem **pe,elem *de)
{   elem *es;

    while ((*pe).Eoper == OPcomma)
        pe = &(*pe).EV.E2;
    es = *pe;

    /* If the destructor cannot affect the result of *pe, just  */
    /* jam e in front of it.                                    */
    if (es.Eoper == OPconst || es.Eoper == OPrelconst)
        *pe = el_combine(de,es);
    else
    {
        /* Assign the result of the expression to a new symbol  */
        Symbol *s;
        elem *e;
        type *t = es.ET;

        s = symbol_generate(SCauto,t);
        s.Sflags |= SFLfree;
        symbol_add(s);
        /* Gen the following:
         *           ,
         *          / \
         *         /   ,
         *        =   / \
         *       / \ de  s
         *      s   es
         */
        if (tybasic(t.Tty) == TYstruct)
        {
            if (t.Ttag.Sstruct.Sflags & STRanyctor)
            {   list_t arglist = null;

                list_append(&arglist,es);
                e = cpp_constructor(el_ptr(s),t,arglist,null,null,0);
                /*      Cannot use init_constructor() because it eliminates
                    temporaries that we are already calling destructors
                    for in expression de.
                    We'd like to use it, because better code is generated.
                 */
                /*e = init_constructor(s,t,arglist,0,4,null);*/
            }
            else
            {
                e = el_bint(OPstreq,t,el_var(s),es);
                if (config.flags3 & CFG3eh && !eecontext.EEin)
                {   Classsym *stag = t.Ttag;

                    if (stag.Sstruct.Sdtor && pointertype == stag.Sstruct.ptrtype)
                        e = el_ctor(cpp_fixptrtype(el_ptr(s),t),e,n2_createprimdtor(stag));
                }
            }
        }
        else
            e = el_bint((tyaggregate(t.Tty) ? OPstreq : OPeq),t,el_var(s),es);
        *pe = el_combine(e,el_combine(de,el_var(s)));
    }
}

/********************************
 * Parse base initializers and build up the baseinit list.
 */

/*private*/ void base_initializer(Symbol *s_ctor)
{   func_t *f;
    int seenbase = 0;                   /* !=0 if we've seen base initializer */
    meminit_t *m;
    Symbol *sm;
    Classsym *stag;
    struct_t *st;
    type *tclass;
    list_t baseinit;                    /* list of initializers         */
    list_t sl;

    /*dbg_printf("base_initializer(%s)\n",s_ctor.Sident);*/
    symbol_debug(s_ctor);
    assert(tyfunc(s_ctor.Stype.Tty));
    f = s_ctor.Sfunc;
    assert(f);
    if (!(f.Fflags & Fctor))
    {   if (tok.TKval == TKcolon)
        {   cpperr(EM_not_ctor, &s_ctor.Sident[0]); // not a constructor
            panic(TKlcur);
        }
        return;
    }
    baseinit = null;
    tclass = s_ctor.Stype.Tnext.Tnext;
    assert(tybasic(tclass.Tty) == TYstruct);
    stag = tclass.Ttag;
    st = stag.Sstruct;
    if (tok.TKval == TKcolon)           /* if base initializer          */
    {
    stoken();                           /* skip over :                  */
    while (1)
    {   baseclass_t *b;
        Symbol *s;
        int i;
        char *p;

        if (tok.TKval == TKcolcol)
        {
            stoken();
            if (tok.TKval == TKident)
            {
                s = scope_search(tok.TKid, SCTglobal);
                p = tok.TKid;
                goto L4;
            }
            else
            {   synerr(EM_ident_exp);   // identifier expected
                break;
            }
        }
        else if (tok.TKval == TKident)
        {   /* See if identifier is a member of stag    */
            sm = n2_searchmember(stag,tok.TKid);
            if (sm)
            {
                switch (sm.Sclass)
                {
                    case SCmember:
                    case SCfield:
                        break;

                    case SCtypedef:
                        if (tybasic(sm.Stype.Tty) == TYstruct)
                        {   s = sm.Stype.Ttag;
                            goto L4;
                        }
                        goto default;

                    default:
                        cpperr(EM_no_mem_init,prettyident(sm)); // cannot have member initializer
                        break;
                }
            }
            else
            {
                // See if tok.TKid is the name of a base class
                for (b = st.Sbase; b; b = b.BCnext)
                {
                    s = b.BCbase;
                    if (strcmp(&s.Sident[0], tok.TKid) == 0)
                        goto L10;
                }

                s = symbol_search(tok.TKid);
            L10:
                p = tok.TKid;
            L4:
                if (s)
                {
                    p = &s.Sident[0];
                    switch (s.Sclass)
                    {   case SCtypedef:
                            // Maybe it's a typedef'd struct
                            if (tybasic(s.Stype.Tty) == TYstruct)
                            {   s = s.Stype.Ttag;
                                goto L4;
                            }
                            break;

                        case SCnamespace:
                            if (stoken() == TKcolcol)
                            {
                                if (stoken() != TKident)
                                    synerr(EM_id_or_decl);      // identifier expected
                                else
                                {   Nspacesym *sn = cast(Nspacesym *)s;
                                    Symbol *smem;

                                    smem = nspace_searchmember(tok.TKid, sn);
                                    if (smem)
                                    {
                                        s = smem;
                                        goto L4;
                                    }
                                    else
                                        synerr(EM_undefined, tok.TKid);
                                }
                            }
                            else
                                token_unget();
                            break;

                        case SCtemplate:
                            s = template_expand(s,1);
                            goto L4;

                        case SCstruct:
                            if (stoken() == TKcolcol)
                            {   Symbol *smem;

                                if (stoken() != TKident)
                                    synerr(EM_id_or_decl);      // identifier expected
                                else
                                {   Classsym *sbase = cast(Classsym *)s;

                                    smem = cpp_findmember_nest(&sbase,tok.TKid,false);
                                    if (smem)
                                    {
                                        s = smem;
                                        goto L4;
                                    }
                                    else
                                        cpperr(EM_class_colcol,tok.TKid); // must be a class name
                                }
                            }
                            else
                                token_unget();

                            // It's not a member, see if base class or virtual base class
                            b = st.Sbase;      // try base classes first
                            for (i = 0; i < 2; i++)
                            {
                                for (; b; b = b.BCnext)
                                {
                                    if (b.BCbase == s)
                                    {
                                        sm = s;
                                        goto L2;
                                    }
                                }
                                b = st.Svirtbase; /* try virtual base class */
                            }
                            break;

                        default:
                            break;
                    }
                }

            }
        L2:
            if (!sm)
                err_notamember(tok.TKid,stag);  // not a member
            stoken();
        L1:
            chktok(TKlpar,EM_lpar2,"base/member class");
            if (sm)
            {   list_t bl;

                /* See if we already have an initializer for sm         */
                for (bl = baseinit; bl; bl = list_next(bl))
                {   m = cast(meminit_t *) list_ptr(bl);
                    if (m.MIsym == sm)
                    {   cpperr(EM_seen_init, &sm.Sident[0]); // already seen initializer
                        sm = null;
                    }
                }
            }
        }
        else if (tok.TKval == TKlpar)
        {
            /* Treat unnamed initializer as initializer for first base class */
            /* (this is an anachronism to support pre-MI code)          */
            b = st.Sbase;
            if (!b)
                cpperr(EM_bad_mem_init, &s_ctor.Sident[0]); // bad member initializer
            else
            {   sm = b.BCbase;
                goto L1;
            }
        }
        else
        {   cpperr(EM_bad_mem_init, &s_ctor.Sident[0]);     // bad member initializer
            break;                      /* no more member initializers  */
        }

        if (sm)
        {
            m = cast(meminit_t *) MEM_PARF_CALLOC((*m).sizeof);
            m.MIsym = sm;

            getarglist(&m.MIelemlist); /* Read list of parameters into arglist */
            chktok(TKrpar,EM_rpar);
            list_append(&baseinit,m);
        }
        else
        {   panic(TKrpar);              /* error recovery               */
            stoken();
        }
        if (tok.TKval != TKcomma)
            break;
        stoken();
    }
    }

    // Make sure we have an initializer for each const and ref member
    for (sl = st.Sfldlst; sl; sl = list_next(sl))
    {   Symbol *sm2 = list_symbol(sl);
        tym_t ty;

        if ((sm2.Sclass == SCmember || sm2.Sclass == SCfield) &&
            (tyref(ty = sm2.Stype.Tty) ||
                (tybasic(ty) != TYstruct && ty & mTYconst)))
        {   list_t bl;

            for (bl = baseinit; 1; bl = list_next(bl))
            {   if (!bl)
                {   cpperr(EM_const_needs_init,prettyident(sm2));        // uninitialized reference
                    break;
                }
                m = cast(meminit_t *) list_ptr(bl);
                if (m.MIsym == sm2)
                    break;
            }
        }
    }

    cpp_buildinitializer(s_ctor,baseinit,0);
}


/**********************************
 * Initialize
 */

/*private*/ void namret_init()
{
    memset(&funcstate.namret,0,Namret.sizeof);
    funcstate.namret.can = true;
}

/**********************************
 * We can't do the named return value.
 */

/*private*/ void namret_term()
{
    list_free(&funcstate.namret.explist,null);
    memset(&funcstate.namret,0,funcstate.namret.sizeof);
}

/**********************************
 * Process gathered info on named return values.
 */

/*private*/ void namret_process()
{   list_t el;
    block *b;

    if ((el = funcstate.namret.explist) != null)
    {
        /* Turn each return copy constructor into just the address of sret */
        for (; el; el = list_next(el))
        {   elem *e = list_elem(el);
            elem *et;
            elem tmp = void;

            debug tmp.id = elem.IDelem;

            et = _cast(el_var(funcstate.namret.sret),e.ET);

            /* swap e and et    */
            el_copy(&tmp,e);
            el_copy(e,et);
            el_copy(et,&tmp);

            el_free(et);
        }

        /* Go through function, replacing all occurrences of s with sret */
        for (b = startblock; b; b = b.Bnext)
        {   namret_replace(b.Belem);
        }

        funcstate.namret.s.Sflags |= SFLnodtor;        /* s doesn't need any destructor */

        // If need a destructor, then the OPctor is unbalanced
        if (type_struct(funcstate.namret.s.Stype) &&
            funcstate.namret.s.Stype.Ttag.Sstruct.Sdtor)
            funcsym_p.Sfunc.Fflags3 |= Fmark;
    }
    namret_term();
}

/***********************************
 * Walk elem tree, replacing funcstate.namret.s with funcstate.namret.sret.
 */

/*private*/ void namret_replace(elem *e)
{
    while (e)
    {   elem_debug(e);
        if (!OTleaf(e.Eoper))
        {   namret_replace(e.EV.E1);
            e = e.EV.E2;
        }
        else
        {
            if ((e.Eoper == OPvar || e.Eoper == OPrelconst) &&
                e.EV.Vsym == funcstate.namret.s)
            {   elem *eo = el_longt(tstypes[TYint],e.EV.Voffset);
                elem *ev = el_var(funcstate.namret.sret);

                symbol_debug(e.EV.Vsym);
                if (e.Eoper == OPvar)
                {
                    /* s => *(sret + offset)    */
                    e.Eoper = OPind;
                    e.EV.E1 = el_bint(OPadd,ev.ET,ev,eo);
                    e.EV.E2 = null;
                }
                else /* OPrelconst */
                {
                    /* &s => sret + offset      */
                    e.Eoper = OPadd;
                    e.EV.E1 = _cast(ev,e.ET);
                    e.EV.E2 = eo;
                }
            }
            break;
        }
    }
}

}
