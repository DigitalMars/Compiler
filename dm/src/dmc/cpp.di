/**
 * Implementation of the
 * $(LINK2 http://www.digitalmars.com/download/freecompiler.html, Digital Mars C/C++ Compiler).
 *
 * Copyright:   Copyright (c) 1987-1998 by Symantec, All Rights Reserved
 *              Copyright (c) 2000-2017 by Digital Mars, All Rights Reserved
 * Authors:     $(LINK2 http://www.digitalmars.com, Walter Bright)
 * License:     Distributed under the Boost Software License, Version 1.0.
 *              http://www.boost.org/LICENSE_1_0.txt
 * Source:      https://github.com/DigitalMars/Compiler/blob/master/dm/src/dmc/cpp.di
 */

/* Globals for C++                              */

module cpp;

extern (C++):

import dtoken;
import parser;

import ddmd.backend.cdef;
import ddmd.backend.cc;
import ddmd.backend.el;
import ddmd.backend.oper;
import ddmd.backend.type;

import tk.dlist;

alias char_p = char*;
alias symbol_p = Symbol*;

/* Names for special variables  */
extern __gshared
{
//    char[2*IDMAX + 1] cpp_name;
    char[2] cpp_name_ct;
    char[2] cpp_name_dt;
    char[2] cpp_name_as;
    char[3] cpp_name_vc;
    char[5] cpp_name_this;
    char[7] cpp_name_free;
    char[12] cpp_name_initvbases;
    char[2] cpp_name_new;
    char[2] cpp_name_delete;
    char[3] cpp_name_anew;
    char[3] cpp_name_adelete;
    char[3] cpp_name_primdt;
    char[3] cpp_name_scaldeldt;
    char[3] cpp_name_priminv;
    char[10] cpp_name_none;
    char[12] cpp_name_invariant;

    list_t cpp_stidtors;     // auto destructors that go in _STIxxxx
}

/* From init.c */
extern __gshared bool init_staticctor;

/* From cpp.c */
/* List of elems which are the constructor and destructor calls to make */
extern __gshared
{
    list_t constructor_list;         // for _STIxxxx
    list_t destructor_list;          // for _STDxxxx
    symbol_p s_mptr;
    symbol_p s_genthunk;
    symbol_p s_vec_dtor;
    symbol_p[OPMAX] cpp_operfuncs;
    uint[(OPMAX + 31) / 32] cpp_operfuncs_nspace;
}

elem *cpp_istype(elem *e, type *t);
char *cpp_unmangleident(const(char)* p);
int cpp_opidx(int op);
char *cpp_opident(int op);
char *cpp_catname(char *n1 , char *n2);
char *cpp_genname(char *cl_name , char *mem_name);
void cpp_getpredefined();
char *cpp_operator(int *poper , type **pt);
char *cpp_operator2(token_t *to, int *pcastoverload);
elem *cpp_new(int global , Symbol *sfunc , elem *esize , list_t arglist , type *tret);
elem *cpp_delete(int global , Symbol *sfunc , elem *eptr , elem *esize);
version (SCPP)
{
    match_t cpp_matchtypes(elem *e1,type *t2, Match *m = null);
    Symbol *cpp_typecast(type *tclass , type *t2 , Match *pmatch);
}
version (HTOD)
{
    match_t cpp_matchtypes(elem *e1,type *t2, Match *m = null);
    Symbol *cpp_typecast(type *tclass , type *t2 , Match *pmatch);
}
int cpp_typecmp(type *t1, type *t2, int relax, param_t *p1 = null, param_t *p2 = null);
char *cpp_typetostring(type *t, char *prefix);
int cpp_cast(elem **pe1, type *t2, int doit);
elem *cpp_initctor(type *tclass, list_t arglist);
int cpp_casttoptr(elem **pe);
elem *cpp_bool(elem *e, int flags);
Symbol *cpp_findopeq(Classsym *stag);
Symbol *cpp_overload(Symbol *sf,type *tthis,list_t arglist,Classsym *sclass,param_t *ptal, uint flags);
Symbol *cpp_findfunc(type *t, param_t *ptpl, Symbol *s, int td);
int cpp_funccmp(Symbol *s1, Symbol *s2);
int cpp_funccmp(type *t1, param_t *ptpl1, Symbol *s2);
elem *cpp_opfunc(elem *e);
elem *cpp_ind(elem *e);
int cpp_funcisfriend(Symbol *sfunc, Classsym *sclass);
int cpp_classisfriend(Classsym *s, Classsym *sclass);
Symbol *cpp_findmember(Classsym *sclass, const(char)* sident, uint flag);
Symbol *cpp_findmember_nest(Classsym **psclass, const(char)* sident, uint flag);
int cpp_findaccess(Symbol *smember, Classsym *sclass);
void cpp_memberaccess(Symbol *smember, Symbol *sfunc, Classsym *sclass);
type *cpp_thistype(type *tfunc, Classsym *stag);
Symbol *cpp_declarthis(Symbol *sfunc, Classsym *stag);
elem *cpp_fixptrtype(elem *e,type *tclass);
int cpp_vtbloffset(Classsym *sclass, Symbol *sfunc);
elem *cpp_getfunc(type *tclass, Symbol *sfunc, elem **pethis);
elem *cpp_constructor(elem *ethis, type *tclass, list_t arglist, elem *enelems, list_t pvirtbase, int flags);
elem *cpp_destructor(type *tclass, elem *eptr, elem *enelems, int dtorflag);
void cpp_build_STI_STD();
Symbol *cpp_getlocalsym(Symbol *sfunc, char *name);
Symbol *cpp_getthis(Symbol *sfunc);
Symbol *cpp_findctor0(Classsym *stag);
void cpp_buildinitializer(Symbol *s_ctor, list_t baseinit, int flag);
void cpp_fixconstructor(Symbol *s_ctor);
int cpp_ctor(Classsym *stag);
int cpp_dtor(type *tclass);
void cpp_fixdestructor(Symbol *s_dtor);
elem *cpp_structcopy(elem *e);
void cpp_fixmain();
int cpp_needInvariant(type *tclass);
void cpp_fixinvariant(Symbol *s_dtor);
elem *cpp_invariant(type *tclass,elem *eptr,elem *enelems,int invariantflag);
elem *Funcsym_invariant(Funcsym *s, int Fflag);
void cpp_init();
void cpp_term();
Symbol *mangle_tbl(int,type *,Classsym *,baseclass_t *);
void cpp_alloctmps(elem *e);

version (SCPP)
    Symbol *cpp_lookformatch(Symbol *sfunc, type *tthis, list_t arglist,
                Match *pmatch, Symbol **pambig, match_t *pma, param_t *ptal,
                uint flags, Symbol *sfunc2, type *tthis2, Symbol *stagfriend = null);
version (HTOD)
    Symbol *cpp_lookformatch(Symbol *sfunc, type *tthis, list_t arglist,
                Match *pmatch, Symbol **pambig, match_t *pma, param_t *ptal,
                uint flags, Symbol *sfunc2, type *tthis2, Symbol *stagfriend = null);

elem* M68HDL(elem* e) { return e; }

struct OPTABLE
{
    ubyte tokn;                 // token(TKxxxx)
    ubyte oper;                 // corresponding operator(OPxxxx)
    char *string;               // identifier string
    char[5] pretty;             // for pretty-printing
                                // longest OP is OPunord
}

