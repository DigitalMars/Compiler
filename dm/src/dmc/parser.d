/**
 * Implementation of the
 * $(LINK2 http://www.digitalmars.com/download/freecompiler.html, Digital Mars C/C++ Compiler).
 *
 * Copyright:   Copyright (c) 1989-1998 by Symantec, All Rights Reserved
 *              Copyright (c) 2000-2017 by Digital Mars, All Rights Reserved
 * Authors:     $(LINK2 http://www.digitalmars.com, Walter Bright)
 * License:     Distributed under the Boost Software License, Version 1.0.
 *              http://www.boost.org/LICENSE_1_0.txt
 * Source:      https://github.com/DigitalMars/Compiler/blob/master/dm/src/dmc/parser.d
 */

/* Definitions only used by the parser                  */

module parser;

import core.stdc.stdio;

import phstring;
import scopeh;

import dmd.backend.cdef;
import dmd.backend.cc;
import dmd.backend.el;
import dmd.backend.outbuf : Outbuffer;
import dmd.backend.ty;
import dmd.backend.type;

import dmd.backend.dlist;

extern (C++):

version (MARS)
{
}
else
{
    extern __gshared linkage_t linkage;       // current linkage that is in effect
    extern __gshared int linkage_spec;        // !=0 if active linkage specification

    static if (MEMMODELS == 1)
    {
        extern __gshared tym_t[LINK_MAXDIM] functypetab;
        tym_t FUNC_TYPE(int l, int m) { return functypetab[l]; }
    }
    else
    {
        extern __gshared tym_t[MEMMODELS][LINK_MAXDIM] functypetab;
        tym_t FUNC_TYPE(int l, int m) { return functypetab[l][m]; }
    }

    extern __gshared mangle_t[LINK_MAXDIM] funcmangletab;
    extern __gshared mangle_t[LINK_MAXDIM] varmangletab;
}


/* Type matches */
enum
{
    TMATCHnomatch    = 0,       // no match
    TMATCHellipsis   = 0x01,    // match using ellipsis
    TMATCHuserdef    = 0x09,    // match using user-defined conversions
    TMATCHboolean    = 0x0E,    // conversion of pointer or pointer-to-member to boolean
    TMATCHstandard   = 0xFA,    // match with standard conversions
    TMATCHpromotions = 0xFC,    // match using promotions
    TMATCHexact      = 0xFF,    // exact type match
}

alias match_t = ubyte;

struct Match
{
    match_t m;
    match_t m2;         // for user defined conversion, this is the second
                        // sequence match level (the second sequence is the
                        // one on the result of the user defined conversion)
    Symbol *s;          // user defined conversion function or constructor
    int _ref;           // !=0 if reference binding
    tym_t toplevelcv;

    static int cmp(ref Match m1, ref Match m2)
    {
        return Match__cmp(m1, m2);
    }
}

int Match__cmp(ref Match m1, ref Match m2);

/***************************
 * Type of destructor call
 */

enum
{
    DTORfree        = 1,       // it is destructor's responsibility to
                               //   free the pointer
    DTORvecdel      = 2,       // delete array syntax
    DTORmostderived = 4,       // destructor is invoked for most-derived
                               //  instance, not for a base class
    DTORvector      = 8,       // destructor has been invoked for an
                               //   array of instances of known size
    DTORvirtual     = 0x10,    // use virtual destructor, if any
    DTORnoeh        = 0x20,    // do not append eh stuff
    DTORnoaccess    = 0x40,    // do not perform access check
}



/***************************
 * Macros.
 */

alias mflags_t = ubyte;
enum
{
    Mdefined        = 1,       // if macro is defined
    Mfixeddef       = 2,       // if can't be re/un defined
    Minuse          = 4,       // if macro is currently being expanded
    Mellipsis       = 0x8,     // if arglist had trailing ...
    Mnoparen        = 0x10,    // if macro has no parentheses
    Mkeyword        = 0x20,    // this is a C/C++ keyword
    Mconcat         = 0x40,    // if macro uses concat operator
    Mnotexp         = 0x80,    // if macro should be expanded
}

alias macro_t = MACRO;
struct MACRO
{
    debug ushort id;
    enum IDmacro = 0x614D;

    char* Mtext;                // replacement text
    phstring_t Marglist;        // list of arguments (as char*'s)
    macro_t* ML,MR;
    macro_t* Mnext;             // next macro in threaded list (all macros
                                // are on one list or another)
    mflags_t Mflags;
    ubyte Mval;                 // if Mkeyword, this is the TKval
    char[1] Mid;                // macro identifier
}

debug
{
    void macro_debug(MACRO* m) { assert(m.id == MACRO.IDmacro); }
}
else
{
    void macro_debug(MACRO* m) { }
}

/**********************
 * Flags for #include files.
 */

enum
{
    FQcwd           = 1,       // search current working directory
    FQpath          = 2,       // search INCLUDE path
    FQsystem        = 4,       // this is a system include
    FQtop           = 8,       // top level file, already open
    FQqual          = 0x10,    // filename is already qualified
    FQnext          = 0x20,    // search starts after directory
                               // of last included file (for #include_next)
}

/***************************************************************************
 * Which block is active is maintained by the blklst, which is a backwardly
 * linked list of which blocks are active.
 */

alias blflags_t = ubyte;
enum
{
    BLspace    = 0x01,   // we've put out an extra space
    BLexpanded = 0x40,   // already macro expanded; don't do it again
    BLtokens   = 0x10,   // saw tokens in input
    BLsystem   = 0x04,   // it's a system #include (and it must be 4)
}

alias bltyp_t = ubyte;
enum
{
    BLstr    = 2,       // string
    BLfile   = 3,       // a #include file
    BLarg    = 4,       // macro argument
    BLrtext  = 5,       // random text

    // if IMPLIED_PRAGMA_ONCE
    BLnew    = 0x02,   // start of new file
    BLifndef = 0x20,   // found #ifndef/#define at start
    BLendif  = 0x08,   // found matching #endif
}

struct blklst
{
    // unsigned because of the chars with the 8th bit set
    char           *BLtextp;    // current position in text buffer
    char           *BLtext;     // start of text buffer
    blklst         *BLprev;     // enclosing blklst
    blflags_t       BLflags;    // input block list flags
    bltyp_t         BLtyp;      // type of block (BLxxxx)

static if (IMPLIED_PRAGMA_ONCE)
{
    uint ifnidx;                // index into ifn[] of IF_FIRSTIF
}

    list_t      BLaargs;        /* actual arguments                     */
    list_t      BLeargs;        /* actual arguments                     */
    int         BLnargs;        /* number of dummy args                 */
    int         BLtextmax;      /* size of text buffer                  */
    char       *BLbuf;          // BLfile: file buffer
    char       *BLbufp;         // BLfile: next position in file buffer
static if (IMPLIED_PRAGMA_ONCE)
{
    char        *BLinc_once_id; // macro identifier for #include guard
}
    Srcpos      BLsrcpos;       /* BLfile, position in that file        */
    int         BLsearchpath;   // BLfile: remaining search path for #include_next

    void print();
}

struct BlklstSave
{
    blklst *BSbl;
    ubyte *BSbtextp;
    ubyte BSxc;
}

static if (IMPLIED_PRAGMA_ONCE)
{
    extern __gshared int TokenCnt;
}

// Get filename for BLfile block
char* blklst_filename(blklst* b) { return srcpos_name(b.BLsrcpos); }

/* Different types of special values that can occur in the character stream */
enum
{
    PRE_ARG    = 0xFF,  // the next char following is a parameter number
                        // If next char is PRE_ARG, then PRE_ARG is the char
    PRE_BRK    = 0xFE,  // token separator
    PRE_STR    = 0xFA,  // If immediately following PRE_ARG, then the
                        // parameter is to be 'stringized'
    PRE_EXP    = 0xFC,  // following identifier may not be expanded as a macro
    PRE_CAT    = 0xFB,  // concatenate tokens
    PRE_EOB    = 0,     // end of block
    PRE_EOF    = 0,     // end of file
    PRE_SPACE  = 0xFD,  // token separator

    PRE_ARGMAX = 0xFB // maximum number of arguments to a macro
}

static if (1)
{
    int EGCHAR()
    {
        xc = *btextp;
        if (xc != PRE_EOB && xc != PRE_ARG)
        {
            btextp++;
            if (config.flags2 & CFG2expand)
                explist(xc);
        }
        else
            egchar2();
        return xc;
    }
}
else
{
    alias EGCHAR = egchar;
}

/**********************************
 * Function return value methods.
 */

enum
{
    RET_REGS        = 1,       // returned in registers
    RET_STACK       = 2,       // returned on stack
    RET_STATIC      = 4,       // returned in static memory location
    RET_NDPREG      = 8,       // returned in floating point register
    RET_PSTACK      = 2,       // returned on stack (DOS pascal style)
}

/* from blklst.c */
extern __gshared blklst *bl;
extern __gshared ubyte *btextp;
extern __gshared int blklst_deferfree;
extern __gshared char *eline;
extern __gshared int elinmax;            // # of chars in buffer eline[]
extern __gshared int elini;              // index into eline[]
extern __gshared int elinnum;            // expanded line number
extern __gshared int expflag;            // != 0 means not expanding list file
blklst *blklst_getfileblock();
void putback(int);

ubyte *macro_replacement_text(macro_t *m, phstring_t args);
ubyte *macro_rescan(macro_t *m, ubyte *text);
char *macro_expand(char *text);

void explist(int);
void expstring(const(char)* );
void expinsert(int);
void expbackup();

/***************************************
 * Erase the current line of expanded output.
 */

void experaseline()
{
    // Remove the #pragma once from the expanded listing
    if (config.flags2 & CFG2expand && expflag == 0)
    {   elini = 0;
        eline[0] = 0;
    }
}

extern (C) void wrtexp(FILE *);
uint egchar2();
uint egchar();

void insblk(ubyte *text,int typ,list_t aargs,int nargs,macro_t *m);
void insblk2(ubyte *text,int typ);

version (__GNUC__)
{
    uint getreallinnum();
    void getcharnum();
}

uint blklst_linnum();
void blklst_term();

/* adl.c */
Symbol *adl_lookup(char *id, Symbol *so, list_t arglist);

/* exp.c */
elem *exp_sizeof(int);

/* exp2.c */
elem *typechk(elem *,type *);
elem *exp2_cast(elem *,type *);
elem *_cast(elem *,type *);
elem *doarray(elem *);
elem *doarrow(elem *);
elem *xfunccall(elem *efunc,elem *ethis,list_t pvirtbase,list_t arglist);
elem *exp2_gethidden(elem *e);
elem *dodotstar(elem *, elem *);
elem *reftostar(elem *);
elem *reftostart(elem *,type *);
//elem *exp2_copytotemp(elem *);
elem *dofunc(elem *);
elem *builtinFunc(elem *);
elem *arraytoptr(elem *);
elem *convertchk(elem *);
elem *lptrtooffset(elem *);
elem *dodot(elem *,type *,bool bColcol);
elem *minscale(elem *);
elem *exp2_addr(elem *);
void getarglist(list_t *);
elem *exp2_ptrvbaseclass(elem *ethis,Classsym *stag,Classsym *sbase);
int t1isbaseoft2(type *,type *);
int c1isbaseofc2(elem **,Symbol *,Symbol *);
int c1dominatesc2(Symbol *stag, Symbol *c1, Symbol *c2);
int exp2_retmethod(type *);
type *exp2_hiddentype(type *);
int typecompat(type *,type *);
int t1isSameOrSubsett2(type *,type *);
void handleaccess(elem *);
void chkarithmetic(elem *);
void chkintegral(elem *);
void scale(elem *);
void impcnv(elem *);
void exp2_ptrtocomtype(elem *);
int  exp2_ptrconv(type *,type *);
void getinc(elem *);
int paramlstmatch(param_t *,param_t *);
int template_paramlstmatch(type *, type *);

/* from file.c */
extern (C)
{
extern __gshared char[5] ext_obj; // or [3] for ".o"
extern __gshared char[3] ext_i;
extern __gshared char[5] ext_dep;
extern __gshared char[5] ext_lst;
extern __gshared char[5] ext_hpp;
extern __gshared char[3] ext_c;
extern __gshared char[5] ext_cpp;
extern __gshared char[5] ext_sym;
extern __gshared char[5] ext_tdb;
}

// htod
extern (C) extern __gshared char[] ext_dmodule;
extern __gshared int includenest;

int file_qualify(char **pfilename,int flag,phstring_t pathlist, int *next_path);
void afopen(char *,blklst *,int);
extern (C) FILE *file_openwrite(const(char)* name,const(char)* mode);
void file_iofiles();
int readln();
extern (C) void wrtpos(FILE *);
extern (C) void wrtlst(FILE *);

/* from func.c */
void func_nest(Symbol *);
void func_body(Symbol *);
elem *addlinnum(elem *);
void func_conddtors(elem **pe, SYMIDX sistart, SYMIDX siend);
void func_expadddtors(elem **,SYMIDX,SYMIDX,bool,bool);
void paramtypadj(type **);
void func_noreturnvalue();
elem *func_expr_dtor(int keepresult);
elem *func_expr();

/* getcmd.c */
extern __gshared uint netspawn_flags;
void getcmd(int,char **);
void getcmd_term();

/* init.c */
void datadef(Symbol *);
dt_t **dtnbytes(dt_t **,targ_size_t,const(char)* );
dt_t **dtnzeros(dt_t **pdtend,targ_size_t size);
dt_t **dtxoff(dt_t **pdtend,Symbol *s,targ_size_t offset,tym_t ty);
dt_t **dtcoff(dt_t **pdtend,targ_size_t offset);
void init_common(Symbol *);
Symbol *init_typeinfo_data(type *ptype);
Symbol *init_typeinfo(type *ptype);
elem *init_constructor(Symbol *,type *,list_t,targ_size_t,int,Symbol *);
void init_vtbl(Symbol *,symlist_t,Classsym *,Classsym *);
void init_vbtbl(Symbol *,baseclass_t *,Classsym *,targ_size_t);
void init_sym(Symbol *, elem *);

/* inline.c */
void inline_do(Symbol *sfunc);
bool inline_possible(Symbol *sfunc);

/* msc.c */
void list_hydrate(list_t *plist, void function(void *) hydptr);
void list_dehydrate(list_t *plist, void function(void *) dehydptr);
void list_hydrate_d(list_t *plist);
void list_dehydrate_d(list_t *plist);

// nspace.c
void namespace_definition();
void using_declaration();
Symbol *nspace_search(const(char)* id,Nspacesym *sn);
Symbol *nspace_searchmember(const(char)* id,Nspacesym *sn);
Symbol *nspace_qualify(Nspacesym *sn);
Symbol *nspace_getqual(int);
void nspace_add(void *snv,Symbol *s);
void nspace_addfuncalias(Funcsym *s,Funcsym *s2);
Symbol *using_member_declaration(Classsym *stag);
void scope_push_nspace(Nspacesym *sn);
void nspace_checkEnclosing(Symbol *s);
int nspace_isSame(Symbol *s1, Symbol *s2);

/* nwc.c */
void thunk_hydrate(Thunk **);
void thunk_dehydrate(Thunk **);
void nwc_defaultparams(param_t *,param_t *);
void nwc_musthaveinit(param_t *);
void nwc_addstatic(Symbol *);
void output_func();
void queue_func(Symbol *);
void savesymtab(func_t *f);
void nwc_mustwrite(Symbol *);
void ext_def(int);
type *declar_abstract(type *);
type *new_declarator(type *);
type *ptr_operator(type *);
type *declar_fix(type *,char *);
void fixdeclar(type *);
type *declar(type *,char *,int);
int type_specifier(type **);
int declaration_specifier(type **ptyp_spec, int *pclass, uint *pclassm);
Symbol *id_expression();
elem *declaration(int flag);
int funcdecl(Symbol *,int,int,Declar *);
Symbol *symdecl(char *,type *,SC,param_t *);
void nwc_typematch(type *,type *,Symbol *);
int isexpression();
void nwc_setlinkage(char *,int,mangle_t);
tym_t nwc_declspec();
void parse_static_assert();
type *parse_decltype();

/* struct.c */
type *stunspec(enum_TK tk, Symbol *s, Symbol *stempsym, param_t *template_argument_list);
Classsym * n2_definestruct(char *struct_tag,uint flags,tym_t ptrtype,
        Symbol *stempsym,param_t *template_argument_list,int nestdecl);
void n2_classfriends(Classsym *stag);
int n2_isstruct(Symbol **ps);
void n2_addfunctoclass(Classsym *,Funcsym *,int flags);
void n2_chkexist(Classsym *stag, char *name);
void n2_addmember(Classsym *stag,Symbol *smember);
Symbol *n2_searchmember(Classsym *,const(char)* );
Symbol *struct_searchmember(const(char)* ,Classsym *);
void n2_instantiate_memfunc(Symbol *s);
type *n2_adjfunctype(type *t);
int n2_anypure(list_t);
void n2_genvtbl(Classsym *stag, int sc , int);
void n2_genvbtbl(Classsym *stag, int sc , int);
void n2_creatector(type *tclass);
/*void n2_createdtor(type *tclass);*/
Symbol *n2_createprimdtor(Classsym *stag);
Symbol *n2_createpriminv(Classsym *stag);
Symbol *n2_createscaldeldtor(Classsym *stag);
Symbol *n2_vecctor(Classsym *stag);
Symbol *n2_veccpct(Classsym *stag);
Symbol *n2_vecdtor(Classsym *stag, elem *enelems);
Symbol *n2_delete(Classsym *stag,Symbol *sfunc,uint nelems);
void n2_createopeq(Classsym *stag, int flag);
void n2_lookforcopyctor(Classsym *stag);
int n2_iscopyctor(Symbol *scpct);
void n2_createcopyctor(Classsym *stag, int flag);
char *n2_genident ();
Symbol *nwc_genthunk(Symbol *s, targ_size_t d, int i, targ_size_t d2);
type *enumspec();
int type_covariant(type *t1, type *t2);

/* ph.c */
extern __gshared char *ph_directory;              /* directory to read PH files from      */
version (MARS)
{
    void ph_init();
}
else
{
    void ph_init(void *, uint reservesize);
}
void ph_term();
void ph_comdef(Symbol *);
void ph_testautowrite();
void ph_autowrite();
void ph_write(const(char)* ,int);
void ph_auto();
int ph_read(char *filename);
int ph_autoread(char *filename);
void *ph_malloc(size_t nbytes);
void *ph_calloc(size_t nbytes);
void ph_free(void *p);
void *ph_realloc(void *p , size_t nbytes);
void ph_add_global_symdef(Symbol *s, uint sctype);

/* pragma.c */
int pragma_search(const(char)* id);
macro_t *macfind();
macro_t *macdefined(const(char)* id, uint hash);
//void listident();
char *filename_stringize(char *name);
ubyte *macro_predefined(macro_t *m);
int macprocess(macro_t *m, phstring_t *pargs, BlklstSave *blsave);
void pragma_include(char *filename,int flag);
void pragma_init();
//void pragma_term();
macro_t *defmac(const(char)* name , const(char)* text);
void definedmac();
macro_t *fixeddefmac(const(char)* name , const(char)* text);
macro_t *defkwd(const(char)* name , enum_TK val);
void macro_freelist(macro_t *m);
int pragma_defined();
void macro_print(macro_t *m);
void macrotext_print(char *p);

void pragma_hydrate_macdefs(macro_t **pmb,int flag);
void pragma_dehydrate_macdefs(macro_t **pm);

void *pragma_dehydrate();
void pragma_hydrate(macro_t **pmactabroot);

// rtti.c
Classsym *rtti_typeinfo();
elem *rtti_cast(enum_TK,elem *,type *);
elem *rtti_typeid(type *,elem *);

/* symbol.c */
Symbol *symbol_search(const(char)* );
void symbol_tree_hydrate(Symbol **ps);
void symbol_tree_dehydrate(Symbol **ps);
Symbol *symbol_hydrate(Symbol **ps);
void symbol_dehydrate(Symbol **ps);

Classsym *Classsym_hydrate(Classsym **ps)
{
    return cast(Classsym *) symbol_hydrate(cast(Symbol **)ps);
}

void Classsym_dehydrate(Classsym **ps)
{
    symbol_dehydrate(cast(Symbol **)ps);
}

void symbol_symdefs_dehydrate(Symbol **ps);
void symbol_symdefs_hydrate(Symbol **ps,Symbol **parent,int flag);
void symboltable_hydrate(Symbol *s, Symbol **parent);
void symboltable_clean(Symbol *s);
void symboltable_balance(Symbol **ps);
Symbol *symbol_membersearch(const(char)* id);
void symbol_gendebuginfo();

/* template.c */
int template_getcmd(char *);
void template_declaration(Classsym *stag, uint access_specifier);
void template_instantiate();
type *template_expand_type(Symbol *s);
Classsym *template_expand(Symbol *s, int flag);
void template_instantiate_forward(Classsym *stag);
param_t *template_gargs(Symbol *s);
param_t *template_gargs2(Symbol *s);
void template_createsymtab(param_t *pt , param_t *p);
void template_deletesymtab();
Symbol *template_createsym(const(char)* id, type *t, Symbol **proot);
char *template_mangle(Symbol *s , param_t *arglist);
Symbol *template_matchfunc(Symbol *stemp, param_t *pl, int, match_t, param_t *ptal, Symbol *stagfriend = null);
Symbol *template_matchfunctempl(Symbol *sfunc, param_t *ptali, type *tf, Symbol *stagfriend = null, int flags = 1);
int template_match_expanded_type(type *ptyTemplate, param_t *ptpl, param_t *ptal, type *ptyActual,
        type *ptyFormal );
bool template_classname(char *vident, Classsym *stag);
void template_free_ptal(param_t *ptal);
int template_function_leastAsSpecialized(Symbol *f1, Symbol *f2, param_t *ptal);
version (SCPP)
{
    Match template_matchtype(type *tp,type *te,elem *ee,param_t *ptpl, param_t *ptal, int flags);
    Match template_deduce_ptal(type *tthis, Symbol *sfunc, param_t *ptali,
        Match *ma, int flags, param_t *pl, param_t **pptal);
}
version (HTOD)
{
    Match template_matchtype(type *tp,type *te,elem *ee,param_t *ptpl, param_t *ptal, int flags);
    Match template_deduce_ptal(type *tthis, Symbol *sfunc, param_t *ptali,
        Match *ma, int flags, param_t *pl, param_t **pptal);
}
void template_function_verify(Symbol *sfunc, list_t arglist, param_t *ptali, int matchStage);
int template_arglst_match(param_t *p1, param_t *p2);
type * template_tyident(type *t,param_t *ptal,param_t *ptpl, int flag);

void tmf_free(TMF *tmf);
void tmf_hydrate(TMF **ptmf);
void tmf_dehydrate(TMF **ptmf);

void tme_free(TME *tme);
void tme_hydrate(TME **ptme);
void tme_dehydrate(TME **ptme);

void tmne_free(TMNE *tmne);
void tmne_hydrate(TMNE **ptmne);
void tmne_dehydrate(TMNE **ptmne);

void tmnf_free(TMNF *tmnf);
void tmnf_hydrate(TMNF **ptmnf);
void tmnf_dehydrate(TMNF **ptmnf);

extern __gshared symlist_t template_ftlist;       // list of template function symbols
extern __gshared Symbol *template_class_list;
extern __gshared Symbol **template_class_list_p;
version (PUBLIC_EXT)
{
    extern __gshared short template_expansion;
}

// from htod.c
void htod_init(const(char)* name);
void htod_term();
bool htod_running();
void htod_include(const(char)* p, int flag);
void htod_include_pop();
void htod_writeline();
void htod_define(macro_t *m);
void htod_decl(Symbol *s);

// from token.c
extern __gshared char *Arg;

/* tytostr.c    */
char *type_tostring(Outbuffer *,type *);
char *param_tostring(Outbuffer *,type *);
char *arglist_tostring(Outbuffer *,list_t el);
char *ptpl_tostring(Outbuffer *, param_t *ptpl);
char *el_tostring(Outbuffer *, elem *e);

extern __gshared phstring_t pathlist;   // include paths
extern __gshared int pathsysi;          // pathlist[pathsysi] is start of -isystem=
extern __gshared list_t headers;        // pre-include files

extern __gshared int structalign;       // alignment for members of structures
extern __gshared char dbcs;
extern __gshared int colnumber;         // current column number
extern __gshared int xc;                // character last read
extern __gshared phstring_t fdeplist;
extern __gshared char *fdepname;
extern (C) extern __gshared FILE *fdep;
extern __gshared char* flstname,fsymname,fphreadname,ftdbname;
extern (C) extern __gshared FILE *flst;

// htod
extern __gshared char *fdmodulename;
extern (C) extern __gshared FILE *fdmodule;

version (SPP)
{
    extern (C) extern __gshared FILE *fout;
}

extern __gshared uint idhash;        // hash value of identifier

extern __gshared int level;     // declaration level
                                // -2: base class list
                                // -1: class body
                                // 0: top level
                                // 1: function parameter declarations
                                // 2: function local declarations
                                // 3+: compound statement decls

extern __gshared Symbol *symlinkage;              // symbol linkage table
extern __gshared param_t *paramlst;               // function parameter list

