/**
 * Compiler implementation of the
 * $(LINK2 http://www.dlang.org, D programming language).
 *
 * Copyright:   Copyright (C) 1984-1998 by Symantec
 *              Copyright (c) 2000-2017 by Digital Mars, All Rights Reserved
 * Authors:     $(LINK2 http://www.digitalmars.com, Walter Bright)
 * License:     Distributed under the Boost Software License, Version 1.0.
 *              http://www.boost.org/LICENSE_1_0.txt
 * Source:      https://github.com/dlang/dmd/blob/master/src/dmd/backend/token.d
 */

/**********************************
 * Symbol tokens:
 *
 * TKstar       *       TKdot           .       TKeq            =
 * TKand        &       TKlbra          [       TKaddass        +=
 * TKmin        -       TKrbra          ]       TKminass        -=
 * TKnot        !       TKarrow         ->      TKmulass        *=
 * TKcom        ~       TKdiv           /       TKdivass        /=
 * TKplpl       ++      TKmod           %       TKmodass        %=
 * TKlpar       (       TKxor           ^       TKshrass        >>=
 * TKrpar       )       TKor            |       TKshlass        <<=
 * TKques       ?       TKoror          ||      TKandass        &=
 * TKcolon      :       TKandand        &&      TKxorass        ^=
 * TKcomma      ,       TKshl           <<      TKorass         |=
 * TKmimi       --      TKshr           >>      TKsemi          ;
 * TKlcur       {       TKrcur          }       TKlt            <
 * TKle         <=      TKgt            >       TKge            >=
 * TKeqeq       ==      TKne            !=      TKadd           +
 * TKellipsis   ...     TKcolcol        ::      TKdollar        $
 *
 * Other tokens:
 *
 * TKstring     string
 * TKfilespec   <filespec>
 */

module dtoken;

import dmd.backend.cdef;
import dmd.backend.cc;

extern (C++):

// Keyword tokens. Needn't be ascii sorted
alias enum_TK = ubyte;
enum {
        TKauto,
        TKbreak,
        TKcase,
        TKchar,
        TKconst,
        TKcontinue,
        TKdefault,
        TKdo,
        TKdouble,
        TKelse,
        TKenum,
        TKextern,
        TKfloat,
        TKfor,
        TKgoto,
        TKif,
        TKint,
        TKlong,
        TKregister,
        TKreturn,
        TKshort,
        TKsigned,
        TKsizeof,
        TKstatic,
        TKstruct,
        TKswitch,
        TKtypedef,
        TKunion,
        TKunsigned,
        TKvoid,
        TKvolatile,
        TKwhile,

        // ANSI C99
        TK_Complex,
        TK_Imaginary,
        TKrestrict,

        // CPP
        TKbool,
        TKcatch,
        TKclass,
        TKconst_cast,
        TKdelete,
        TKdynamic_cast,
        TKexplicit,
        TKfalse,
        TKfriend,
        TKinline,
        TKmutable,
        TKnamespace,
        TKnew,
        TKoperator,
        TKoverload,
        TKprivate,
        TKprotected,
        TKpublic,
        TKreinterpret_cast,
        TKstatic_cast,
        TKtemplate,
        TKthis,
        TKthrow,
        TKtrue,
        TKtry,
        TKtypeid,
        TKtypename,
        TKusing,
        TKvirtual,
        TKwchar_t,
        TK_typeinfo,
        TK_typemask,

        // CPP0X
        TKalignof,
        TKchar16_t,
        TKchar32_t,
        TKconstexpr,
        TKdecltype,
        TKnoexcept,
        TKnullptr,
        TKstatic_assert,
        TKthread_local,

        TKasm,
        TK_inf,
        TK_nan,
        TK_nans,
        TK_i,           // imaginary constant i
        TK_with,
        TK_istype,
        TK_cdecl,
        TK_fortran,
        TK_pascal,

        TK_debug,
        TK_in,
        TK_out,
        TK_body,
        TK_invariant,
//#if TX86
        TK_Seg16,
        TK_System,
        TK__emit__,
        TK_far,
        TK_huge,
        TK_near,

        TK_asm,
        TK_based,
        TK_cs,
        TK_declspec,
        TK_except,
        TK_export,
        TK_far16,
        TK_fastcall,
        TK_finally,
        TK_handle,
        TK_java,
        TK_int64,
        TK_interrupt,
        TK_leave,
        TK_loadds,
        TK_real80,
        TK_saveregs,
        TK_segname,
        TK_ss,
        TK_stdcall,
        TK_syscall,
        TK_try,
//#if TARGET_LINUX || TARGET_OSX || TARGET_FREEBSD || TARGET_OPENBSD || TARGET_SOLARIS
/+
        TK_attribute,
        TK_extension,
        TK_format,
        TK_restrict,
        TK_bltin_const,
+/
//#endif
//#else
/+
        TKcomp,
        TKextended,
        TK_handle,
        TK_machdl,
        TK_pasobj,
        TK__class,
        TKinherited,
+/
//#endif
        TK_unaligned,
        TKsymbol,                       // special internal token


        TKcolcol,               //      ::
        TKarrowstar,            //      ->*
        TKdotstar,              //      .*

        TKstar,TKand,TKmin,TKnot,TKcom,TKplpl,TKlpar,TKrpar,TKques,TKcolon,TKcomma,
        TKmimi,TKlcur,TKdot,TKlbra,TKrbra,TKarrow,TKdiv,TKmod,TKxor,TKor,TKoror,
        TKandand,TKshl,TKshr,TKrcur,TKeq,TKaddass,TKminass,TKmulass,TKdivass,
        TKmodass,TKshrass,TKshlass,TKandass,TKxorass,TKorass,TKsemi,
        TKadd,TKellipsis,
//#if !TX86 || TARGET_LINUX || TARGET_OSX || TARGET_FREEBSD || TARGET_OPENBSD || TARGET_SOLARIS
//        TKdollar,
//#endif

 /* The following relational tokens must be in the same order as the
    corresponding operators.
  */
 /*   ==     !=         */
        TKle,TKgt,TKlt,TKge,TKeqeq,TKne,

 /*   !<>=    <>   <>=   !>    !>=  !<    !<=  !<>      */
        TKunord,TKlg,TKleg,TKule,TKul,TKuge,TKug,TKue,

        TKstring,
        TKfilespec,     /* <filespec>           */
        TKpragma,
        TKnum,  /* integral number      */
        TKreal_f,
        TKreal_d,
        TKreal_da,
        TKreal_ld,
        TKident,        /* identifier           */
        TKeol,  /* end of line          */
        TKeof,  /* end of file          */
        TKnone, /* no token             */
        TKMAX   /* number of tokens     */
}

enum KWMAX = TK_unaligned + 1;      // number of keywords

enum
{
    TKFfree         = 1,       // free the token after it's scanned
    TKFinherited    = 2,       // keyword INHERITED prior to token
    TKFpasstr       = 4,       // pascal string
}

struct token_t
{
    enum_TK TKval;              // what the token is
    ubyte TKflags;              // TKFxxxx flags
    ubyte TKty;                 // TYxxxx for TKstring and TKnum
    union
    {
        // Scheme for short IDs avoids malloc/frees
        struct                  // TKident
        {   char* TKid;         // pointer to identifier
            char[4] idtext;     // if short identifier
        }

        struct                  // TKstring and TKfilespec
        {
            char* TKstr;        // for strings (not null terminated)
            int TKlenstr;       // length of string
        }
        Symbol *TKsym;          // TKsymbol
        int _pragma;            // TKpragma: PRxxxx, pragma number
                                // -1 if unrecognized pragma
        targ_long Vlong;        // integer when TKnum
        targ_llong Vllong;
        targ_float Vfloat;
        targ_double Vdouble;
        targ_ldouble Vldouble;
    }
    align(8)                    // necessary to line TKsrcpos where it is in C version
      Srcpos TKsrcpos;          // line number from where it was taken
    token_t *TKnext;            // to create a list of tokens

    debug ushort id;
    enum IDtoken = 0xA745;

    void setSymbol(Symbol *s)
    {
        TKval = TKsymbol;
        TKsym = s;
    }

    void print() { token_print(&this); }
}

void token_print(token_t *);

debug
{
    void token_debug(token_t* t) { assert(t.id == token_t.IDtoken); }
}
else
{
    void token_debug(token_t* t) { }
}

// Use this for fast scans
enum
{
    _IDS    = 1,       // start of identifier
    _ID     = 2,       // identifier
    _TOK    = 4,       // single character token
    _EOL    = 8,       // end of line
    _MUL    = 0x10,    // start of multibyte character sequence
    _BCS    = 0x20,    // in basic-source-character-set
    _MTK    = 0x40,    // could be multi-character token
    _ZFF    = 0x80,    // 0 or 0xFF (must be sign bit)
}

ubyte istok(char x)        { return _chartype[x + 1] & _TOK; }
ubyte iseol(char x)        { return _chartype[x + 1] & _EOL; }
ubyte isidstart(char x)    { return _chartype[x + 1] & _IDS; }
ubyte isidchar(char x)     { return _chartype[x + 1] & (_IDS | _ID); }
ubyte ismulti(char x)      { return _chartype[x + 1] & _MUL; }
ubyte isbcs(char x)        { return _chartype[x + 1] & _BCS; }

/* from token.c */
extern __gshared
{
    int igncomment;
    char *tok_arg;
    uint argmax;
    token_t tok;
    int ininclude;
    char[2*IDMAX + 1] tok_ident;       // identifier
    ubyte[257] _chartype;
    token_t *toklist;
}

void token_setdbcs(int);
void token_setlocale(const(char)*);
token_t *token_copy();
void token_free(token_t *tl);
void token_hydrate(token_t **ptl);
void token_dehydrate(token_t **ptl);
token_t *token_funcbody(int bFlag);
token_t *token_defarg();
void token_funcbody_print(token_t *t);
void token_setlist(token_t *t);
void token_poplist();
void token_unget();
void token_markfree(token_t *t);
void token_setident(char *);
void token_semi();
enum_TK token_peek();

enum_TK rtoken(int);
version (SPP)
    enum_TK stoken() { return rtoken(1); }
else
{
    enum_TK stokenx();
    enum_TK stoken() { return toklist ? stokenx() : rtoken(1); }
}

void token_init();
void removext();
void comment();
void cppcomment();
char *combinestrings(targ_size_t *plen);
char *combinestrings(targ_size_t *plen, tym_t *pty);
void inident();
void inidentX(char *p);
uint comphash(const(char)* p);
int insertSpace(ubyte xclast, ubyte xcnext);
void panic(enum_TK ptok);
void chktok(enum_TK toknum , uint errnum);
void chktok(enum_TK toknum , uint errnum, const(char)* str);
void opttok(enum_TK toknum);
bool iswhite(int c);
void token_term();

enum_TK ptoken() { return rtoken(1); }
enum_TK token()  { return rtoken(0); }

// !MARS
/* from pragma.c */
//enum_TK ptoken();
void pragma_process();
//int pragma_search(const(char)* id);
//macro_t * macfind();
//macro_t *macdefined(const(char)* id, uint hash);
void listident();
void pragma_term();
//macro_t *defmac(const(char)* name , const(char)* text);
//int pragma_defined();

Srcpos getlinnum();
version (SPP)
    alias token_linnum = getlinnum;
else
    Srcpos token_linnum();


//      listing control
//      Listings can be produce via -l and SCpre
//              -l      expand all characters not if'd out including
//                      comments
//              SCpre   list only characters to be compiled
//                      i.e. exclude comments and # preprocess lines

/+
#if SPP
#define SCPRE_LISTING_ON()      expflag--; assert(expflag >= 0)
#define SCPRE_LISTING_OFF()     assert(expflag >= 0); expflag++
#define EXPANDED_LISTING_ON()   expflag--; assert(expflag >= 0)
#define EXPANDED_LISTING_OFF()  assert(expflag >= 0); expflag++
#else
#define SCPRE_LISTING_OFF()
#define SCPRE_LISTING_ON()
#define EXPANDED_LISTING_ON()   expflag--; assert(expflag >= 0)
#define EXPANDED_LISTING_OFF()  assert(expflag >= 0); expflag++
#endif

#define EXPANDING_LISTING()     (expflag == 0)
#define NOT_EXPANDING_LISTING() (expflag)
+/

/***********************************************
 * This is the token lookahead API, which enables us to
 * look an arbitrary number of tokens ahead and then
 * be able to 'unget' all of them.
 */

struct Token_lookahead
{
    int inited;                 // 1 if initialized
    token_t *toks;              // list of tokens
    token_t **pend;             // pointer to end of that list

    void init()
    {
        toks = null;
        pend = &toks;
        inited = 1;
    }

    enum_TK lookahead()
    {
        //assert(inited == 1);
        *pend = token_copy();
        (*pend).TKflags |= TKFfree;
        pend = &(*pend).TKnext;
        return stoken();
    }

    void term()
    {
        //assert(inited == 1);
        inited--;
        if (toks)
        {
            token_unget();
            token_setlist(toks);
            stoken();
        }
    }

    void discard()
    {
        inited--;
        token_free(toks);
    }
}
