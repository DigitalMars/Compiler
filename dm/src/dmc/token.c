/**
 * Implementation of the
 * $(LINK2 http://www.digitalmars.com/download/freecompiler.html, Digital Mars C/C++ Compiler).
 *
 * Copyright:   Copyright (c) 1983-1998 by Symantec, All Rights Reserved
 *              Copyright (c) 2000-2017 by Digital Mars, All Rights Reserved
 * Authors:     $(LINK2 http://www.digitalmars.com, Walter Bright)
 * License:     Distributed under the Boost Software License, Version 1.0.
 *              http://www.boost.org/LICENSE_1_0.txt
 * Source:      https://github.com/DigitalMars/Compiler/blob/master/dm/src/dmc/token.c
 */

// Lexical analyzer

#include        <stdio.h>
#include        <ctype.h>
#include        <string.h>
#include        <stdlib.h>
#include        <errno.h>
#include        <locale.h>
#include        "cc.h"
#include        "token.h"
#include        "type.h"
#include        "parser.h"
#include        "global.h"
#include        "outbuf.h"
#include        "utf.h"

#if _WIN32 && __DMC__
// from \sc\src\include\setlocal.h
extern "C" char * __cdecl __locale_decpoint;
#endif

static char __file__[] = __FILE__;      /* for tassert.h                */
#include        "tassert.h"

#define FASTKWD 1               // use fast instead of small kwd lookup
/* Globals:
 */

#define isoctal(x)      ('0' <= (x) && (x) < '8')
#ifdef __DMC__
#define ishex(x)        isxdigit(x)
#else
#define ishex(x)        (isxdigit(x) || isdigit(x))
#endif

STATIC enum_TK innum(void);
STATIC enum_TK inchar(int flags);
STATIC int escape(void);
STATIC int instring(int tc , int flags);
STATIC enum_TK inreal(const char *);
STATIC void checkAllowedUniversal(unsigned uc);
void stringToUTF16(unsigned char *string, unsigned len);
void stringToUTF32(unsigned char *string, unsigned len);

// Flags for instring()
#define INSnoescape     1       // escape sequences are not allowed
                                // (useful for pathnames in include
                                // files under MSDOS or OS2)
#define INSwchar_t      2       // L
#define INSchar         4       // u8
#define INSwchar        8       // u
#define INSdchar        0x10    // U
#define INSraw          0x20    // R

char tok_ident[2*IDMAX + 1];    /* identifier                   */
static char *tok_string;        /* for strings (not null terminated)    */
static int tok_strmax;          /* length of tok_string buffer          */

Outbuffer *utfbuf;

#define LOCALE                  0       // locale support for Unicode conversion
#define PASCAL_STRINGS          0


#if PASCAL_STRINGS
STATIC char tok_passtr;         /* pascal string in tok_string          */
#endif

token_t tok = { TKnone };       /* last token scanned                   */
int ininclude = 0;              /* if in #include line                  */
int igncomment;                 // 1 if ignore comment in preprocessed output

char *tok_arg;                  /* argument buffer                      */
unsigned argmax;                /* length of argument buffer            */

int isUniAlpha(unsigned u);
void cppcomment(void);
STATIC bool inpragma();

/* This is so the isless(), etc., macros in math.h work even with -A99
 */
#define EXTENDED_FP_OPERATORS   1
//#define EXTENDED_FP_OPERATORS (!ANSI_STRICT)

/********************************
 * This table is very similar to _ctype[] in the library.
 * We make our own for fast character classifications.
 */

/*
    0   1   2   3   4   5   6   7   8   9   A   B   C   D   E   F
0  NUL SOH STX ETX EOT ENQ ACK BEL BS  HT  LF  VT  FF  CR  SO  SI
1  DLE DC1 DC2 DC3 DC4 NAK SYN ETB CAN EM  SUB ESC FS  GS  RS  US
2   SP  !   "   #   $   %   &   '   (   )   *   +   ,   -   .   /
3   0   1   2   3   4   5   6   7   8   9   :   ;   <   =   >   ?
4   @   A   B   C   D   E   F   G   H   I   J   K   L   M   N   O
5   P   Q   R   S   T   U   V   W   X   Y   Z   [   \   ]   ^   _
6   `   a   b   c   d   e   f   g   h   i   j   k   l   m   n   o
7   p   q   r   s   t   u   v   w   x   y   z   {   |   }   ~ DEL
*/


unsigned char _chartype[257] =
{       0,                      // in case we use EOF as an index
        _ZFF,0,0,0,0,0,0,0,0,0,_EOL,_EOL,_EOL,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,
        _TOK,_TOK,_MTK,_MTK,_TOK,_MTK,_MTK,_MTK, // ()*+,-./
        _ID,_ID,_ID,_ID,_ID,_ID,_ID,_ID,
        _ID,_ID,_MTK,_TOK,_MTK,_MTK,_MTK,_TOK,  // 89:;<=>?
        0,  _IDS,_IDS,_IDS,_IDS,_IDS,_IDS,_IDS,
        _IDS,_IDS,_IDS,_IDS,_IDS,_IDS,_IDS,_IDS,
        _IDS,_IDS,_IDS,_IDS,_IDS,_IDS,_IDS,_IDS,
        _IDS,_IDS,_IDS,_TOK,0,_TOK,_MTK,_IDS,   // XYZ[\]^_
        0,  _IDS,_IDS,_IDS,_IDS,_IDS,_IDS,_IDS,
        _IDS,_IDS,_IDS,_IDS,_IDS,_IDS,_IDS,_IDS,
        _IDS,_IDS,_IDS,_IDS,_IDS,_IDS,_IDS,_IDS,
        _IDS,_IDS,_IDS,_TOK,_MTK,_TOK,_TOK,0    // xyz{|}~DEL

        /* the remaining 128 bytes are 0        */
};

/*********************************
 * Make a copy of the current token.
 */

static token_t *token_freelist;

#if !SPP

token_t *token_copy()
{   token_t *t;
    size_t len;

    /* Try to get a token_t from the free list  */
    if (token_freelist)
    {   t = token_freelist;
        token_freelist = t->TKnext;
    }
    else
#if TX86
        t = (token_t *) mem_fmalloc(sizeof(token_t));
#else
        t = (token_t *) MEM_PH_MALLOC(sizeof(token_t));
#endif
        // tokens are kept in ph and on available free list
    *t = tok;
#ifdef DEBUG
    t->id = IDtoken;
#endif
                                        /* token_linnum will set TokenFile */
    t->TKsrcpos = token_linnum();
    t->TKnext = NULL;
    t->TKflags = 0;
    switch (tok.TKval)
    {
        case TKident:
            len = strlen(tok.TKid);
            if (len < sizeof(t->TKutok._idx.idtext))
            {   memcpy(t->TKutok._idx.idtext,tok.TKid,len + 1);
                t->TKid = t->TKutok._idx.idtext;
            }
            else
            {
                t->TKid = (char *) MEM_PH_MALLOC(len + 1);
                memcpy(t->TKid,tok.TKid,len + 1);
            }
            break;
        case TKstring:
        case TKfilespec:
            t->TKstr = (char *) MEM_PH_MALLOC(tok.TKlenstr);
            memcpy(t->TKstr,tok.TKstr,tok.TKlenstr);
            break;
    }
    return t;
}

#endif

/********************************
 * Free a token list.
 */

#if !SPP

void token_free(token_t *tl)
{   token_t *tn;

    /*dbg_printf("token_free(%p)\n",tl);*/
    while (tl)
    {   token_debug(tl);
        tn = tl->TKnext;
        switch (tl->TKval)
        {   case TKident:
                if (tl->TKid != tl->TKutok._idx.idtext)
                    MEM_PH_FREE(tl->TKid);
                break;
            case TKstring:
            case TKfilespec:
                MEM_PH_FREE(tl->TKstr);
                break;
        }
#ifdef DEBUG
        tl->id = 0;
        assert(tl != &tok);
#endif
        /* Prepend to list of available tokens  */
        tl->TKnext = token_freelist;
        token_freelist = tl;

        tl = tn;
    }
}

/*********************************
 * Hydrate a token list.
 */

#if HYDRATE
void token_hydrate(token_t **pt)
{
    token_t *tl;

    while (isdehydrated(*pt))
    {
        tl = (token_t *) ph_hydrate(pt);
        token_debug(tl);
        //type_hydrate(&tl->TKtype);
        switch (tl->TKval)
        {   case TKident:
                ph_hydrate(&tl->TKid);
                break;
            case TKstring:
            case TKfilespec:
                ph_hydrate(&tl->TKstr);
                break;
            case TKsymbol:
                symbol_hydrate(&tl->TKsym);
                break;
        }
#if TX86
        filename_translate(&tl->TKsrcpos);
        srcpos_hydrate(&tl->TKsrcpos);
#endif
        pt = &tl->TKnext;
    }
}
#endif

/*********************************
 * Dehydrate a token list.
 */

#if DEHYDRATE
void token_dehydrate(token_t **pt)
{
    token_t *tl;

    while ((tl = *pt) != NULL && !isdehydrated(tl))
    {
        token_debug(tl);
        ph_dehydrate(pt);
        //type_dehydrate(&tl->TKtype);
        switch (tl->TKval)
        {   case TKident:
                ph_dehydrate(&tl->TKid);
                break;
            case TKstring:
            case TKfilespec:
                ph_dehydrate(&tl->TKstr);
                break;
            case TKsymbol:
                symbol_dehydrate(&tl->TKsym);
                break;
        }
#if TX86
        srcpos_dehydrate(&tl->TKsrcpos);
#endif
        pt = &tl->TKnext;
    }
}
#endif

#endif

/*********************************
 * Read body of function into a list, and return that list.
 * Also used to read in class and function template definitions.
 * Also used to read in declaration for disambiguating between
 * declarations and expression-statements.
 * Input:
 *      flag    !=0 if trailing semi should be included in the list,
 *              0 FALSE otherwise
 * Output:
 *      tok = token past closing curly bracket
 */

#if !SPP

token_t *token_funcbody(int bFlag)
{   token_t *start = NULL;
    token_t **ptail = &start;
    int braces = 0;             /* nesting level of {} we're in */

    int tryblock = 0;

    //printf("token_funcbody(bFlag = %d), tryblock = %d\n", bFlag, tryblock);
    while (1)
    {   token_t *t;

        t = token_copy();
        //dbg_printf("Sfilnum = %d, Slinnum = %d\n",t->TKsrcpos.Sfilnum,t->TKsrcpos.Slinnum);
        *ptail = t;                     /* append to token list         */
        token_debug(t);
        ptail = &(t->TKnext);           /* point at new tail            */

        switch (tok.TKval)
        {
            case TKtry:
                if (braces == 0)
                    tryblock = 1;
                break;
            case TKlcur:
                braces++;
                break;
            case TKrcur:
                if (--braces <= 0)      // < is for error recovery
                {
                    if (tryblock)       // look for catch
                    {
                        stoken();
                        if (tok.TKval == TKcatch)
                            continue;
                        token_unget();
                    }
                    if (bFlag)
                    {   stoken();       // If the next token is a ;, attach it
                                        // to the token list as well because it
                                        // could be a static array member
                        if (tok.TKval == TKsemi)
                            continue;
                        token_unget();
                    }
                    return start;
                }
                break;
            case TKsemi:
            case TKeol:
                if (braces == 0)
                    return start;
                break;
            case TKeof:
                err_fatal(EM_eof);      // premature end of source file
        }
        stoken();
    }
}

/*************************************
 * Similar to token_funcbody(), but used to read in
 * template and function default arguments.
 * Input:
 *      token is on '='
 * Output:
 *      first token following argument
 */

#if 0

token_t *token_defarg()
{   token_t *start = NULL;
    token_t **ptail = &start;
    int parens = 0;             // nesting level of () we're in
    int brackets = 0;           // nesting level of <> we're in

    assert(tok.TKval == TKeq);
    while (1)
    {   token_t *t;

        switch (stoken())
        {
            case TKlpar:
                parens++;
                break;
            case TKrpar:
                parens--;
                if (parens < 0 && brackets <= 0)
                    goto Lret;
                break;
            case TKlt:
                brackets++;
                break;
            case TKgt:
                brackets--;
                if (parens <= 0 && brackets < 0)
                    goto Lret;
                break;
            case TKcomma:
                if (parens <= 0 && brackets <= 0)
                    goto Lret;
                break;
            case TKeof:
                err_fatal(EM_eof);      /* premature end of source file         */
        }

        // Append current token to list
        t = token_copy();
        //dbg_printf("Sfilnum = %d, Slinnum = %d\n",t->TKsrcpos.Sfilnum,t->TKsrcpos.Slinnum);
        *ptail = t;                     /* append to token list         */
        token_debug(t);
        ptail = &(t->TKnext);           /* point at new tail            */
    }

Lret:
    return start;
}

#endif

/*********************************
 * Set scanner to read from list of tokens rather than source.
 */

token_t *toklist;
static list_t toksrclist;

void token_setlist(token_t *t)
{
    if (t)
    {
        if (toklist)            /* if already reading from list         */
            list_prepend(&toksrclist,toklist);
        toklist = t;
        tok.TKsrcpos = t->TKsrcpos;
    }
}

/*******************************
 * Dump most recent token list.
 */

void token_poplist()
{
    if (toksrclist)
        toklist = (token_t *) list_pop(&toksrclist);
    else
        toklist = NULL;
}

/*********************************
 * "Unget" current token.
 */

void token_unget()
{   token_t *t;

    t = token_copy();
    t->TKflags |= TKFfree;
    token_setlist(t);
}

/***********************************
 * Mark token list as freeable.
 */

void token_markfree(token_t *t)
{
    for (; t; t = t->TKnext)
        t->TKflags |= TKFfree;
}

#endif

/***********************************
 * Set current token to be an identifier.
 */

void token_setident(char *id)
{
    tok.TKval = TKident;
    tok.TKid = tok_ident;
#ifdef DEBUG
    if (strlen(id) >= sizeof(tok_ident))
    {
        printf("id = '%s', strlen = %d\n", id, strlen(id));
    }
#endif
    assert(strlen(id) < sizeof(tok_ident));
    strcpy(tok_ident,id);
}

/***********************************
 * Set current token to be a symbol.
 */

void token_t::setSymbol(symbol *s)
{
    TKval = TKsymbol;
    TKsym = s;
}

/****************************
 * If an ';' is the next character, warn about possible extraneous ;
 */

void token_semi()
{
    if (
#if !SPP
        !toklist &&
#endif
        xc == ';')
        warerr(WM_extra_semi);                  // possible extraneous ;
}

/*******************************
 * Get current line number.
 */

#ifndef token_linnum

Srcpos token_linnum()
{
#if SPP
    return getlinnum();
#else
    return toklist
        ?
          tok.TKsrcpos
        : getlinnum();
#endif
}

#endif

#if !SPP

/***************************************
 * Peek at next token ahead without disturbing current token.
 */

enum_TK token_peek()
{
    enum_TK tk;
    token_t *t;

    t = token_copy();
    t->TKflags |= TKFfree;
    tk = stoken();
    token_unget();
    token_setlist(t);
    stoken();
    return tk;
}

#endif

#if TX86

#if !SPP

struct Keyword
{       char *id;
        unsigned char val;
};

struct Keyword kwtab1[] =
{
        "auto",         TKauto,
        "break",        TKbreak,
        "case",         TKcase,
        "char",         TKchar,
        "continue",     TKcontinue,
        "default",      TKdefault,
        "do",           TKdo,
        "double",       TKdouble,
        "else",         TKelse,
        "enum",         TKenum,
        "extern",       TKextern,
        "float",        TKfloat,
        "for",          TKfor,
        "goto",         TKgoto,
        "if",           TKif,
        "int",          TKint,
        "long",         TKlong,
        "register",     TKregister,
        "return",       TKreturn,
        "short",        TKshort,
        "sizeof",       TKsizeof,
        "static",       TKstatic,
        "struct",       TKstruct,
        "switch",       TKswitch,
        "typedef",      TKtypedef,
        "union",        TKunion,
        "unsigned",     TKunsigned,
        "void",         TKvoid,
        "while",        TKwhile,

        // For ANSI C99
        "_Complex",     TK_Complex,
        "_Imaginary",   TK_Imaginary,
        // BUG: do restrict and inline too

        "const",        TKconst,        // last three are non-traditional C
        "signed",       TKsigned,       // place at end for linux option to
        "volatile",     TKvolatile,     // exclude them

#if TARGET_LINUX || TARGET_OSX || TARGET_FREEBSD || TARGET_OPENBSD || TARGET_SOLARIS
#define OPTS_NON_TRADITIONAL 3          // ANSI + const,signed,volatile
#define OPTS_NON_ANSI        1          // asm
//#define OPTS_NON_ANSI      3          // asm,typeof,restrict
//      "typeof",       TK_typeof,
        "restrict",     TKrestrict,
        "asm",          TK_asm,
#else
        // Be compatible with IBM's OS/2 C compiler.
        "_Cdecl",       TK_cdecl,
        "_Far16",       TK_far16,
        "_Pascal",      TK_pascal,
        "_Seg16",       TK_Seg16,
        "_System",      TK_System,

        "__emit__",     TK__emit__,
        "__inf",        TK_inf,
        "__nan",        TK_nan,
        "__nans",       TK_nans,
        "__imaginary",  TK_i,
        "__istype",     TK_istype,
        "__with",       TK_with,
        "__restrict",   TKrestrict,

        // New features added with 8.0
        "__debug",      TK_debug,
        "__in",         TK_in,
        "__out",        TK_out,
        "__body",       TK_body,
        "__invariant",  TK_invariant,
#endif
};

struct Keyword kwtab_cpp[] =
{
        "catch",        TKcatch,
        "class",        TKclass,
        "const_cast",   TKconst_cast,
        "delete",       TKdelete,
        "dynamic_cast", TKdynamic_cast,
        "explicit",     TKexplicit,
        "friend",       TKfriend,
        "inline",       TKinline,
        "mutable",      TKmutable,
        "namespace",    TKnamespace,
        "new",          TKnew,
        "operator",     TKoperator,
        "overload",     TKoverload,
        "private",      TKprivate,
        "protected",    TKprotected,
        "public",       TKpublic,
        "reinterpret_cast",     TKreinterpret_cast,
        "static_cast",  TKstatic_cast,
        "template",     TKtemplate,
        "this",         TKthis,
        "throw",        TKthrow,
        "try",          TKtry,
        "typeid",       TKtypeid,
        "typename",     TKtypename,
        "using",        TKusing,
        "virtual",      TKvirtual,
        "__typeinfo",   TK_typeinfo,
        "__typemask",   TK_typemask,

        // CPP0X
        "alignof",      TKalignof,
        "char16_t",     TKchar16_t,
        "char32_t",     TKchar32_t,
        "constexpr",    TKconstexpr,
        "decltype",     TKdecltype,
        "noexcept",     TKnoexcept,
        "nullptr",      TKnullptr,
        "static_assert", TKstatic_assert,
        "thread_local", TKthread_local,
};

// Non-ANSI compatible keywords
#if _WIN32
struct Keyword kwtab2[] =
{
        // None, single, or double _ keywords
        "__cdecl",      TK_cdecl,
        "__far",        TK_far,
        "__huge",       TK_huge,
        "__near",       TK_near,
        "__pascal",     TK_pascal,

        // Single or double _ keywords
        "__asm",        TK_asm,
        "__based",      TK_based,
        "__cs",         TK_cs,
        "__declspec",   TK_declspec,
        "__ddecl",      TK_java,
        "__except",     TK_except,
        "__export",     TK_export,
        "__far16",      TK_far16,
        "__fastcall",   TK_fastcall,
        "__finally",    TK_finally,
        "__fortran",    TK_fortran,
        "__handle",     TK_handle,
        "__jupiter",    TK_java,
        "__inline",     TKinline,
        "__int64",      TK_int64,
        "__interrupt",  TK_interrupt,
        "__leave",      TK_leave,
        "__loadds",     TK_loadds,
        "__unaligned",  TK_unaligned,
        "__real80",     TK_real80,
        "__saveregs",   TK_saveregs,
        "__segname",    TK_segname,
        "__ss",         TK_ss,
        "__stdcall",    TK_stdcall,
        "__syscall",    TK_syscall,
        "__try",        TK_try,
};
#endif

#if TARGET_LINUX || TARGET_OSX || TARGET_FREEBSD || TARGET_OPENBSD || TARGET_SOLARIS
// linux is a little different about ANSI
// if -ansi or -traditional selected, the root keyword is removed,
// but the __keyword and __keyword__ are still accepted.
// The GNU header files take advantage of this to create header files
// that work with the -ansi and -traditional options
// Plus there are a number of additional __keywords and __keywords__.

struct Keyword kwtab2[] =
{
//    "__alignof",      TK_alignof,
//    "__alignof__",    TK_alignof,
    "__asm",            TK_asm,
    "__asm__",          TK_asm,
    "__attribute",      TK_attribute,
    "__attribute__",    TK_attribute,
    "__builtin_constant_p", TK_bltin_const,
    "__cdecl",          TK_cdecl,       // Not GNU keyword
//    "__complex",      TK_complex,
//    "__complex__",    TK_complex,
    "__const",          TKconst,
    "__const__",        TKconst,
    "__declspec",       TK_declspec,
    "__extension__",    TK_extension,
//    "__imag",         TK_imaginary,
//    "__imag__",       TK_imaginary,
    "__inline",         TKinline,       // remember to handle CPP keyword
    "__inline__",       TKinline,       // when inline for "C" implemented
    "inline",           TKinline,       // linux kernel uses inline for C
//    "__iterator",     TK_iterator,
//    "__iterator__",   TK_iterator,
//    "__label__",      TK_label,
//    "__real",         TK_real,
//    "__real__",       TK_real,
    "__restrict",       TKrestrict,
    "__restrict__",     TKrestrict,
    "__signed",         TKsigned,
    "__signed__",       TKsigned,
//    "__typeof",       TK_typeof,
//    "__typeof__",     TK_typeof,
    "__volatile",       TKvolatile,
    "__volatile__",     TKvolatile,
};
#endif

// Alternate tokens from C++98 2.11 Table 4
struct Keyword kwtab3[] =
{
        "and",          TKandand,
        "bitor",        TKor,
        "or",           TKoror,
        "xor",          TKxor,
        "compl",        TKcom,
        "bitand",       TKand,
        "and_eq",       TKandass,
        "or_eq",        TKorass,
        "xor_eq",       TKxorass,
        "not",          TKnot,
        "not_eq",       TKne,
};

struct Keyword kwtab4[] =
{       "bool",         TKbool,
        "true",         TKtrue,
        "false",        TKfalse,
};

STATIC void token_defkwds(struct Keyword *k,unsigned dim)
{   unsigned u;

    for (u = 0; u < dim; u++)
        defkwd(k[u].id,(enum_TK)k[u].val);
}

#endif

/***********************************************
 * Initialize tables for tokenizer.
 */

void token_init()
{
#if SPP
    igncomment = (config.flags3 & CFG3comment) == 0;
#else

    token_defkwds(kwtab1,arraysize(kwtab1)
#if TARGET_LINUX || TARGET_OSX || TARGET_FREEBSD || TARGET_OPENBSD || TARGET_SOLARIS
                - (ANSI ? OPTS_NON_ANSI : 0)
                - (OPT_IS_SET(OPTtraditional)? OPTS_NON_TRADITIONAL : 0)
#endif
                                          );
    token_defkwds(kwtab2,arraysize(kwtab2));

    if (config.flags4 & CFG4alternate)
        token_defkwds(kwtab3,arraysize(kwtab3));

    if (CPP)
    {
        token_defkwds(kwtab_cpp,arraysize(kwtab_cpp));
        if (config.flags4 & CFG4bool)
            token_defkwds(kwtab4,arraysize(kwtab4));
        if (config.flags4 & CFG4wchar_t)
            defkwd("wchar_t",TKwchar_t);
    }
    else
    {
        defkwd("_Bool",TKbool);
        //if (ANSI >= 99)
        {   defkwd("inline",TKinline);
            defkwd("restrict",TKrestrict);
        }
    }

#if _WIN32
    if (!ANSI)
    {   unsigned u;

        defkwd("asm",TKasm);

        // Single underscore version
        for (u = 0; u < arraysize(kwtab2); u++)
            defkwd(kwtab2[u].id + 1,(enum_TK)kwtab2[u].val);

        // No underscore version
        for (u = 0; u < 5; u++)
            defkwd(kwtab2[u].id + 2,(enum_TK)kwtab2[u].val);
    }
#endif
#endif

    if (CPP)
    {
        static char bcs[96 + 1] =
                // CPP98 2.2
                "abcdefghijklmnopqrstuvwxyz"
                "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                "0123456789"
                " _{}[]#()<>%:;.?*+-/^&|~!=,\\\"'"
                "\t\v\f\n";

        for (int i = 0; i < sizeof(bcs) - 1; i++)
        {
            assert(bcs[i]);
            _chartype[bcs[i] + 1] |= _BCS;
        }
    }

    if (!ANSI)
        _chartype['$' + 1] |= _IDS;             // '$' is a valid identifier char

    _chartype[0xFF + 1] |= _ZFF;
    token_setdbcs(config.asian_char);

    utfbuf = new Outbuffer();
}

/***********************************************
 * Top level token parser.
 * For identifiers, see if they're a keyword.
 * Else insert identifier into symbol table.
 * Input:
 *      tokens from ptoken()
 * Output:
 *      tok.TKval =             token number (TKxxxx)
 * Returns:
 *      tok.TKval
 */

#if !SPP

enum_TK stokenx()
{
    if (!toklist)
        return rtoken(1);

    {   token_t *t;

        token_debug(toklist);
        t = toklist->TKnext;
        tok = *toklist;
        switch (tok.TKval)
        {   case TKident:
                tok.TKid = strcpy(tok_ident,toklist->TKid);
                break;
            case TKstring:
            case TKfilespec:
                if (tok_strmax < tok.TKlenstr)
                {   // Can happen if we're getting tokens from a precompiled header
                    tok_string = (char *) MEM_PARC_REALLOC(tok_string, tok.TKlenstr + 1);
                }

                tok.TKstr = (char *)memcpy(tok_string,toklist->TKstr,tok.TKlenstr);
                break;
        }
        if (tok.TKflags & TKFfree)
        {   toklist->TKnext = NULL;
            token_free(toklist);
        }
        if (t)
        {   toklist = t;
            token_debug(toklist);
        }
        else
            token_poplist();
        return tok.TKval;
    }
}

#endif

/*********************************
 * Set dbcs support.
 */

void token_setdbcs(int db)
{   unsigned c;
#if LOCALE
    static char *dblocale[4] =
    {   "C",    // default
        ".932", // Japan
        ".950", // Chinese
        ".949"  // Korean
    };

    assert(db < arraysize(dblocale));
    if (!locale_broken && setlocale(LC_ALL,dblocale[db]))
    {
        dbcs = db;                              // set global state

        // Initialize _chartype[] for multibyte characters
        for (c = 0x81; c <= 0xFF; c++)
        {
            _chartype[c + 1] &= ~_MUL;
            if (isleadbyte(c))
                _chartype[c + 1] |= _MUL;
        }
        return;
    }

    // locale broken, use old way
    locale_broken = 1;
#endif
    dbcs = db;                                  // set global state

    // Initialize _chartype[] for multibyte characters
    for (c = 0x81; c <= 0xFF; c++)
    {
        _chartype[c + 1] &= ~_MUL;
        switch (db)
        {   case 0:
                break;                          // no multibyte characters
            case 1:                             // Japanese
                if (c >= 0x81 && c <= 0x9F ||
                    c >= 0xE0 && c <= 0xFC)
                    goto L1;
                break;
            case 2:                             // Chinese and Taiwanese
                if (c >= 0x81 && c <= 0xFE)
                    goto L1;
                break;
            case 3:                             // Korean
                if (c >= 0x81 && c <= 0xFE)
                {
                  L1:
                    _chartype[c + 1] |= _MUL;
                }
                break;
            default:
                assert(0);
        }
    }
}

#endif // TX86

/************************************************
 * Set locale.
 */

void token_setlocale(const char *string)
{   unsigned c;

    if (setlocale(LC_ALL,string))
    {
        // Initialize _chartype[] for multibyte characters
        for (c = 0x81; c <= 0xFF; c++)
        {
            _chartype[c + 1] &= ~_MUL;
            if (isleadbyte(c))
                _chartype[c + 1] |= _MUL;
        }
    }
    else
        synerr(EM_nolocale,string);     // locale not supported
}

/***********************************************
 * Gut level token parser.
 * Parses chars into tokens
 * TKxxxx
 * Input:
 *      chars from egchar()
 *      flag    1       check idents for macros, process pragmas
 *              2       don't expand macro
 * Output:
 *      tok.TKval =     token number (TKxxxx)
 *      tok.TKutok.Vlong =      number (if tok.TKval == TKnum)
 *      tok.TKstr ->    character string (if tok.TKval == TKstring)
 *      ident ->        identifier string (if tok.TKval == TKident)
 *      xc =            character following the token returned
 * Returns:
 *      tok.TKval
 */

enum_TK rtoken(int flag)
{       int xc1;
        unsigned char blflags;
        int insflags;

        //printf("rtoken(%d)\n", flag);
#if IMPLIED_PRAGMA_ONCE
        if (cstate.CSfilblk)
        {
            //printf("BLtokens %s\n", blklst_filename(cstate.CSfilblk));
            cstate.CSfilblk->BLflags |= BLtokens;
        }
#endif
#if PASCAL_STRINGS
        tok.TKflags &= ~TKFpasstr;
#endif

loop1:
  switch (xc)
  {
        case PRE_SPACE:
        case PRE_BRK:
            EGCHAR();
            goto loop1;

        case ' ':
        case '\t':
        case '\f':
        case '\13':
        case CR:
#if 1
                if (config.flags2 & CFG2expand)
                    egchar();
                else
                {
                    while (1)
                    {
                        switch (*btextp)
                        {
                            case ' ':
                            case '\t':
                                btextp++;
                                continue;

                            case LF:
                                btextp++;
                                goto case_LF;

                            case PRE_EOB:
                            case PRE_ARG:
                                egchar2();
                                break;

                            default:
                                xc = *btextp;
                                btextp++;
                                break;
                        }
                        break;
                    }
                }
#else
                EGCHAR();               /* eat white space              */
#endif
                goto loop1;
        case LF:
        case_LF:
                egchar();
                if (pstate.STflags & (PFLpreprocessor | PFLmasm | PFLbasm))
                    return tok.TKval = TKeol;   // end of line is a token
                goto loop1;
        case '0': case '1': case '2': case '3': case '4':
        case '5': case '6': case '7': case '8': case '9':
                return tok.TKval = innum();
        case 'L':               // L LR
                insflags = INSwchar_t;          goto Lstringprefix;
        case 'u':               // u u8 uR u8R
                insflags = INSwchar;            goto Lstringprefix;
        case 'U':               // U UR
                insflags = INSdchar;            goto Lstringprefix;
        case 'R':               // R
                insflags = INSraw;              goto Lstringprefix;
        Lstringprefix:
                blflags = bl->BLflags;
                if (config.flags2 & CFG2expand)
                {       expbackup();    /* remove first char of id      */
                        expflag++;      /* suppress expanded listing    */
                }
                inident();              /* read in identifier           */
                tok.TKid = tok_ident;
                if (!(xc == '\'' || xc == '"'))
                    goto Lident;
                if (tok_ident[1] == 0)
                    ;
                else if (tok_ident[2] == 0)
                {
                    if (insflags == INSraw)
                        goto Lident;
                    if (tok_ident[1] == 'R')
                        insflags |= INSraw;
                    else if (insflags == INSwchar && tok_ident[1] == '8')
                        insflags = INSchar;
                    else
                        goto Lident;
                }
                else if (tok_ident[3] == 0 && insflags == INSwchar &&
                         tok_ident[1] == '8' && tok_ident[2] == 'R')
                {
                    insflags = INSchar | INSraw;
                }
                else
                    goto Lident;
                {   /* It's a wide character constant or wide string    */
                    tok.TKval = TKident;
                    listident();
                    if (xc == '"')
                    {
                        egchar();
                        tok.TKlenstr = instring('"',insflags); // get string
                        tok.TKval = TKstring;
                        tok.TKstr = tok_string;
#if !SPP
                        switch (insflags & (INSchar | INSwchar | INSdchar | INSwchar_t))
                        {
                            case INSwchar_t:
                                switch (config.flags4 & (CFG4wchar_t | CFG4wchar_is_long))
                                {
                                    case 0:
                                        tok.TKty = TYushort;
                                        break;
                                    case CFG4wchar_t:
                                        tok.TKty = TYwchar_t;
                                        break;
                                    case CFG4wchar_is_long:
                                        tok.TKty = TYulong;
                                        break;
                                    case CFG4wchar_t | CFG4wchar_is_long:
                                        tok.TKty = TYdchar;
                                        break;
                                }
                                break;

                            case 0:
                            case INSchar:
                                tok.TKty = TYchar;
                                break;

                            case INSwchar:
                                tok.TKty = TYchar16;
                                break;

                            case INSdchar:
                                tok.TKty = TYdchar;
                                break;

                            default:
                                assert(0);
                        }
#endif
                    }
                    else
                    {   egchar();
                        tok.TKval = inchar(insflags);
                    }
                    break;
                }
                goto Lident;

        case '$':
                if (ANSI)
                {   lexerr(EM_badtoken);                // unrecognized token
                    goto loop1;         // try again
                }
        case 'A': case 'B': case 'C': case 'D': case 'E': case 'F':
        case 'G': case 'H': case 'I': case 'J': case 'K':
        case 'M': case 'N': case 'O': case 'P': case 'Q':
        case 'S': case 'T': case 'V': case 'W': case 'X':
        case 'Y': case 'Z':
        case 'a': case 'b': case 'c': case 'd': case 'e': case 'f':
        case 'g': case 'h': case 'i': case 'j': case 'k': case 'l':
        case 'm': case 'n': case 'o': case 'p': case 'q': case 'r':
        case 's': case 't': case 'v': case 'w': case 'x':
        case 'y': case 'z':
        case '_':
                blflags = bl->BLflags;
                if (config.flags2 & CFG2expand)
                {       expbackup();    /* remove first char of id      */
                        expflag++;      /* suppress expanded listing    */
                }
                inident();              /* read in identifier           */
                tok.TKid = tok_ident;
        Lident:
                if (flag & 1)
                {
                    macro_t *m;

                    m = macfind();
                    if (m)
                    {
                        /* This next determines if the identifier came from
                         * an expanded block, but is not the last token in
                         * that block.
                         */
                        if (!(blflags & BLexpanded && bl && bl->BLflags & BLexpanded))
                        {   if (m->Mflags & Mdefined && !(flag & 2))
                            {   phstring_t args;
#if TARGET_LINUX || TARGET_OSX || TARGET_FREEBSD || TARGET_OPENBSD || TARGET_SOLARIS
                                // extension over used, declarations and statements
                                if (m->Mval == TK_extension)
                                    goto loop1;
#endif
                                assert(!(m->Mflags & Minuse));

                                if (!m->Mtext)
                                {   // Predefined macro
                                    unsigned char *p = macro_predefined(m);
                                    putback(xc);
                                    if (p)
                                        insblk2(p, BLstr);
                                    if (config.flags2 & CFG2expand)
                                        expflag--;
                                    egchar();
                                    goto loop1;
                                }
                                else if (macprocess(m, &args, NULL))
                                {   unsigned char *p;
                                    unsigned char *q;
                                    unsigned char xcnext = xc;
                                    unsigned char xclast;
                                    static unsigned char brk[2] = { PRE_BRK, 0 };

                                    putback(xc);
                                    p = macro_replacement_text(m, args);
                                    //printf("macro replacement text is '%s'\n", p);
                                    q = macro_rescan(m, p);
                                    //printf("final expansion is '%s'\n", q);
                                    parc_free(p);

                                    /* Compare next character of source with
                                     * last character of macro expansion.
                                     * Insert space if necessary to prevent
                                     * token concatenation.
                                     */
                                    if (!isspace(xcnext))
                                        insblk2(brk, BLrtext);

                                    insblk2(q, BLstr);
                                    bl->BLflags |= BLexpanded;
                                    if (config.flags2 & CFG2expand)
                                        expflag--;
                                    explist(PRE_BRK);
                                    egchar();
                                    //printf("expflag = %d, xc = '%c'\n", expflag, xc);
                                    goto loop1;
                                }
                            }
                        }
                        if (m->Mflags & Mkeyword &&                // if it is a keyword
                            !(pstate.STflags & PFLpreprocessor)) // pp doesn't recognize kwds
                        {
                            if (config.flags2 & CFG2expand)
                            {   tok.TKval = TKident;
                                listident();
                            }
                            return tok.TKval = (enum_TK) m->Mval;   // change token to kwd
                        }
                    }
                    if (config.flags2 & CFG2expand)
                    {   tok.TKval = TKident;
                        listident();
                    }
                }
                return tok.TKval = TKident;

        case '!':
                tok.TKval = TKnot;              /* assume                       */
                if (egchar() == '=') { tok.TKval = TKne;   egchar(); }
                else if (xc == '<' && EXTENDED_FP_OPERATORS)
                {
                    tok.TKval = TKuge;
                    if (egchar() == '=') { tok.TKval = TKug; egchar(); }
                    else if (xc == '>')
                    {   tok.TKval = TKue;
                        if (egchar() == '=') { tok.TKval = TKunord; egchar(); }
                    }
                }
                else if (xc == '>' && EXTENDED_FP_OPERATORS)
                {
                    tok.TKval = TKule;
                    if (egchar() == '=') { tok.TKval = TKul; egchar(); }
                }
                break;

        case '.':
                egchar();
                tok.TKval = TKdot;
                if (isdigit(xc))
                {       tok.TKval = inreal(".");
                }
                else
                if (xc == '.')
                {       egchar();
                        if ((char) xc == '.')
                        {       egchar();
                                tok.TKval = TKellipsis;
                        }
                        else
                        {
#if SPP
                            if (ANSI)
#endif
                                lexerr(EM_badtoken);    // unrecognized token
                            goto loop1;
                        }
                }
                else if (CPP && xc == '*')
                {   egchar();
                    tok.TKval = TKdotstar;
                }
                break;

        case '"':
                egchar();
                assert((ininclude & ~1) == 0);
                tok.TKlenstr = instring('"',ininclude); /* get string   */
#if !SPP
                tok.TKty = chartype->Tty;
#endif
                tok.TKval = TKstring;
                tok.TKstr = tok_string;
#if PASCAL_STRINGS
                if (tok_passtr)
                    tok.TKflags |= TKFpasstr;
#endif
                break;

        case '\'':
                egchar();
                return tok.TKval = inchar(0);   /* read in char constant */

        case '<':
                if (ininclude)                  /* if #include string   */
                {   egchar();
                    tok.TKlenstr = instring('>',INSnoescape);   // get string
                    tok.TKval = TKfilespec;
                    tok.TKstr = tok_string;
                    break;
                }
                tok.TKval = TKlt;               /* assume '<'                   */
                switch (egchar())
                {   case '=':
                        tok.TKval = TKle;
                        egchar();
                        break;
                    case '<':
                        tok.TKval = TKshl;      /* assume '<<'                  */
                        if (egchar() == '=')
                        {   tok.TKval = TKshlass;
                            egchar();
                        }
                        break;
                    case '>':
                        if (EXTENDED_FP_OPERATORS)
                        {   tok.TKval = TKlg;
                            if (egchar() == '=')
                            {   tok.TKval = TKleg;
                                egchar();
                            }
                        }
                        break;
                    case '%':
                        if (config.flags3 & CFG3digraphs)
                        {   tok.TKval = TKlcur;
                            egchar();
                        }
                        break;
                    case ':':
                        if (config.flags3 & CFG3digraphs)
                        {   tok.TKval = TKlbra;
                            egchar();
                        }
                        break;
                }
                break;

        case '>':
                tok.TKval = TKgt;               /* assume '>'                   */
                if (egchar() == '=') { tok.TKval = TKge; egchar(); }
                else if (xc == '>')
                {   tok.TKval = TKshr;  /* assume '>>'                  */
                    if (egchar() == '=') { tok.TKval = TKshrass; egchar(); }
                }
                break;

        case '=':
                tok.TKval = TKeq;
                if ((char) egchar() == '=') { tok.TKval = TKeqeq; egchar(); }
                break;

        case '*':
                tok.TKval = TKstar;
                if ((char) egchar() == '=') { tok.TKval = TKmulass; egchar(); }
                break;

        case '&':
                if ((char) egchar() == '=') { egchar(); return tok.TKval = TKandass; }
                if ((char) xc == '&') { egchar(); return tok.TKval = TKandand; }
                return tok.TKval = TKand;

        case '+':
                if ((char) egchar() == '=') { egchar(); return tok.TKval = TKaddass; }
                if (xc == '+') { egchar(); return tok.TKval = TKplpl; }
                return tok.TKval = TKadd;

        case '-':
                switch (egchar())
                {   case '>':   egchar(); tok.TKval = TKarrow;
                                if (CPP && xc == '*')
                                {   tok.TKval = TKarrowstar;
                                    egchar();
                                }
                                                              break;
                    case '=':   tok.TKval = TKminass; egchar(); break;
                    case '-':   tok.TKval = TKmimi;   egchar(); break;
                    default:    tok.TKval = TKmin; break;
                }
                break;

        case '/':
#if SPP
                if (egchar() == '/')
                {   if (igncomment)
                    {   expbackup();
                        expbackup();
                    }
                    cppcomment();
                    goto loop1;
                }
                if (xc == '*')
                {   if (igncomment)
                    {   expbackup();
                        expbackup();
                    }
                    comment();
                    goto loop1;
                }
#else
                if (egchar() == '/') { cppcomment(); goto loop1; }
                if (xc == '*') { comment(); goto loop1; }
#endif
                if (xc == '=') { egchar(); return tok.TKval = TKdivass; }
                return tok.TKval = TKdiv;

        case '%':
                if (egchar() == '=') { egchar(); return tok.TKval = TKmodass; }
                if (xc == '>' && config.flags3 & CFG3digraphs)
                {   egchar();
                    tok.TKval = TKrcur;
                }
                else if (xc == ':' && config.flags3 & CFG3digraphs)
                {
#if SPP
                    expbackup();
#endif
                    goto case_hash;
                }
                else
                    tok.TKval = TKmod;
                return tok.TKval;

        case '^':
                if (egchar() == '=') { egchar(); return tok.TKval = TKxorass; }
                return tok.TKval = TKxor;

        case '|':
                if (egchar() == '|') { egchar(); return tok.TKval = TKoror; }
                if (xc == '=') { egchar(); return tok.TKval = TKorass; }
                return tok.TKval = TKor;

        case '\\':
                expbackup();            // remove \ from expanded listing
                expflag++;
                if (egchar() == CR)     /* ignore CR's                  */
                {   egchar();
                    expflag--;
                    if (xc == LF)
                        goto loop1;
                    lexerr(EM_badtoken);
                    break;
                }
                expflag--;
                if (xc == LF)
                    goto loop1;

                // \uxxxx or \UXXXXXXXX identifier start
                blflags = bl->BLflags;
                if (config.flags2 & CFG2expand)
                {
                        expflag++;      // suppress expanded listing
                }
                btextp--;
                xc = '\\';              // back up scanner
                inidentX(tok_ident);    // read in identifier
                tok.TKid = tok_ident;
                goto Lident;

        case ':':
                egchar();
                if (CPP && xc == ':') { egchar(); return tok.TKval = TKcolcol; }
                if (xc == '>' && config.flags3 & CFG3digraphs)
                {   egchar();
                    tok.TKval = TKrbra;
                }
                else
                    tok.TKval = TKcolon;
                return tok.TKval;

        case '#':
        case_hash:
                // BUG: not verifying that # is first on the line
#if SPP
                experaseline();
                expflag++;
#endif
                egchar();
                if (!inpragma())
                {
                    goto loop1;
                }
                if (flag && tok.TKval == TKpragma)
                {
                    pragma_process();
                    goto loop1;
                }
                return tok.TKval;

        case ';':
                if (pstate.STflags & PFLsemi)
                {
#if SPP
                    if (igncomment)
                        expbackup();
#endif
                    cppcomment();
                    goto loop1;
                }
                egchar();
                return tok.TKval = TKsemi;

        case '[':       egchar(); return tok.TKval = TKlbra;
        case ']':       egchar(); return tok.TKval = TKrbra;
        case '(':       egchar(); return tok.TKval = TKlpar;
        case ')':       egchar(); return tok.TKval = TKrpar;
        case '{':       egchar(); return tok.TKval = TKlcur;
        case '}':       egchar(); return tok.TKval = TKrcur;
        case ',':       egchar(); return tok.TKval = TKcomma;
        case '?':       egchar(); return tok.TKval = TKques;
        case '~':       egchar(); return tok.TKval = TKcom;
        case 0:
                if (bl)
                {       bl->BLtextp--;
                        egchar();
                        goto loop1;
                }
                return tok.TKval = TKeof;
        case 0x1A:
                egchar();
                return tok.TKval = TKeof;

        default:
                if (xc == EOF)
                {
                    return tok.TKval = TKeof;
                }
                if (xc == PRE_EXP)
                {   expbackup();
                    egchar();
                    return rtoken(flag | 2);
                }
                xc1 = xc;
                egchar();       /* so ^ points to the right character   */
                if (xc1 < ' ')
                        lexerr(EM_bad_char,xc1);        // illegal character
                else
#if SPP
                    if (ANSI)
#endif
                        lexerr(EM_badtoken);    // unrecognized token
                goto loop1;                     // try again
  } /* switch */
  return tok.TKval;
} /* token */

/****************************************
 * Read and throw away C++ comment.
 * Input:
 *      xc                      '/' after the '//'
 * Output:
 *      xc      first char past close of comment
 */

void cppcomment()
{
#if SPP
    expflag += igncomment;
#endif
    //synerr(EM_cpp_comments);        // // comments are not ANSI C
#if 1
    if (config.flags2 & CFG2expand)
    {
        while (1)
        {
            switch (egchar())
            {
                case LF:    goto L1;
                case 0:     return;
                default:    break;
            }
        }
    }
    else if (bl->BLtyp == BLfile)
    {
        // Line buffer is guaranteed to end with a '\n', and not
        // have any PRE_ARG's in it,
        // so just dump the rest of the line buffer
        *btextp = 0;
        xc = LF;
    }
    else
    {
        while (1)
        {
            switch (*btextp)
            {
                case LF:
                    btextp++;
                    xc = LF;
                    goto L1;

                default:
                    btextp++;
                    break;

                case PRE_EOB:
                case PRE_ARG:
                    egchar2();
                    if (xc == LF)
                        goto L1;
                    if (xc == 0)        // end of file
                        return;
                    break;
            }
        }
    }
L1:
#else // old (slower) way
    do
    {   EGCHAR();
        if (xc == 0)
            return;
    }
    while ((unsigned char) xc != LF);
#endif
#if SPP
    expflag -= igncomment;
    explist(xc);
#else
    if (!(pstate.STflags & (PFLpreprocessor T80x86(| PFLmasm | PFLbasm))))
        egchar();       // preprocessor treats \n as separate token
#endif
}

/****************************************
 * Slurp up and throw away comment.
 * Comments may not be nested! (flag these as errors)
 * Input:
 *      xc                      '*' after the '/'
 *      elinnum                 for error diagnostics
 * Output:
 *      xc      first char past close of comment
 */

void comment()
{   int line;

    line = elinnum;                     /* remember where comment started */

#if SPP
    expflag += igncomment;
#endif

#if !SPP
    if (!(config.flags2 & CFG2expand) && bl && bl->BLtyp == BLfile)
    {
        // Fast comment eater for the common case
        unsigned char c;

        c = *btextp;
        while (1)
        {
            switch (c)
            {
                case '/':
                    c = *++btextp;
                    if (c == '*')
                        warerr(WM_nestcomment); // can't nest comments
                    break;
                case '*':
                    c = *++btextp;
                    if (c == '/')       // if close of comment
                    {
                        btextp++;
                        xc = *btextp++; // get char past close of comment
                        return;
                    }
                    break;
                case 0:                 // end of line buffer
                    egchar();
                    if (!xc)
                        goto case_eof;
                    if (bl && bl->BLtyp == BLfile &&
                        xc != '/' && xc != '*')
                        break;
                    goto L1;
                default:
                    c = *++btextp;
                    break;
            }
        }
    }
    else
#endif
    {
        egchar();
     L1:
        while (1)
        {
            switch (xc)
            {
              case '*':                 /* could be close of comment    */
                    if ((char) egchar() == '/')
                    {
#if SPP
                        expflag -= igncomment;
#endif
                        egchar();       /* get char past close of comment */
                        return;
                    }
                    break;
              case '/':
                    if ((char) egchar() == '*')
                        warerr(WM_nestcomment); // can't nest comments
                    break;
              case 0:                           // end of file found
              case_eof:
                    lexerr(EM_no_comment_term,line);    // EOF before end of comment
                    err_fatal(EM_eof);
                    /* NOTREACHED */

              default:
                    EGCHAR();
                    break;
            }
        }
    }
}

/********************************************
 * Make sure tok_string has at least nbytes of space
 */

inline void tok_string_reserve(unsigned nbytes)
{
    if (nbytes > tok_strmax)            // if string buffer overflow
    {
        if (nbytes > STRMAX)
        {
            lexerr(EM_string2big, STRMAX);      // max exceeded
            err_nomem();
        }
        else
        {
            tok_strmax = nbytes + 256;
            if (tok_strmax > STRMAX)
                tok_strmax = STRMAX;
            //dbg_printf("tok_strmax = %u\n",tok_strmax);
            tok_string = (char *) MEM_PARC_REALLOC(tok_string, tok_strmax);
        }
    }
}


/****************************************
 * Read in string and store in tok_string[].
 * Bugs:
 *      EOF instead of end of string encountered...
 * Input:
 *      xc =    first char of string
 *      tc =    character that terminates the string
 * Input:
 *      flags = bit mask INSxxxx
 * Output:
 *      xc =     first char after string
 *      tok_string[] holds the string + 0
 * Returns:
 *      length (in bytes) of the string + 0
 */


STATIC int instring(int tc,int flags)
{
  int c;
  int i = 0;                            // can't be unsigned!!!
  char ispascal = 0;

    enum { RAWinit, RAWdchar, RAWstring, RAWend } rawstate = RAWinit;
    char dchar[16 + 1];
    int dchari;

    while (1)
    {
        tok_string_reserve(i + 4 + 4 + 16);
        if (flags & INSraw)
        {
          L2:
            switch (rawstate)
            {
                case RAWinit:
                    dchari = 0;
                    rawstate = RAWdchar;
                    goto L2;

                case RAWdchar:
                    if (xc == '(')
                    {   dchar[dchari] = 0;
                        dchari++;
                        rawstate = RAWstring;
                        egchar();
                        continue;
                    }
                    if (dchari >= sizeof(dchar) - 1)
                    {   lexerr(EM_string2big, sizeof(dchar) - 1);
                        dchari = 0;
                        egchar();
                        continue;
                    }
                    if (xc == ' ' || xc == '(' || xc == ')' ||
                        xc == '\\' || xc == '\t' || xc == '\v' ||
                        xc == '\f' || xc == '\n')
                        lexerr(EM_invalid_dchar, xc);
                    dchar[dchari] = xc;
                    dchari++;
                    egchar();
                    continue;

                case RAWstring:
                    if (xc == ')')
                    {
                        dchari = 0;
                        rawstate = RAWend;
                        egchar();
                        continue;
                    }
                    break;

                case RAWend:
                    if (xc == dchar[dchari])
                    {   dchari++;
                        egchar();
                        continue;
                    }
                    if (dchar[dchari] == 0 && xc == tc)
                        goto Ldone;
                    tok_string[i] = ')';
                    memcpy(tok_string + i + 1, dchar, dchari);
                    i += 1 + dchari;
                    rawstate = RAWstring;
                    continue;
            }
        }
        else if (xc == tc)              // if reached closing quote
            break;
        c = xc;
        if (c == 0)                     /* if end of source input       */
        {   lexerr(EM_noendofstring);   // unterminated string
            err_fatal(EM_eof);
        }
        egchar();
        if (c == LF && !(flags & INSraw))       // embedded newline
        {   lexerr(EM_noendofstring);   // unterminated string
            continue;
        }
        if (c == PRE_SPACE || (c == PRE_BRK && tc == '>'))
            continue;
        if (ismulti(c) && !(flags & (INSchar | INSwchar | INSdchar)))
        {
#if LOCALE
            char mb[2];

            mb[0] = c;
            mb[1] = xc;
            if (flags & INSwchar_t &&           // if convert to unicode
                !locale_broken &&
                mbtowc((wchar_t *)&tok_string[i],mb,MB_CUR_MAX) > 0)
            {
                //printf("wc = x%x, mb = x%x\n",*(wchar_t *)&tok_string[i],*(unsigned short *)mb);
            }
            else
#endif
            {   tok_string[i    ] = c;
                tok_string[i + 1] = xc;
            }
            i += 2;
            egchar();
            continue;
        }
        if (c == '\\' && !(flags & INSraw))
        {
          L1:
            switch (xc)
            {   case CR:
                    egchar();
                    goto L1;            // ignore

                case LF:                // ignore \ followed by newline
                    egchar();
                    continue;

                case 'p':
                case 'P':
                    // if pascal string
                    if (!ANSI && i == 0 && !(flags & INSnoescape))
                    {   c = 0;
                        ispascal = 1;
                        egchar();               // skip the 'p'
                        break;
                    }
                default:
                    if (!(flags & INSnoescape))
                        c = escape();           // escape sequence
                    break;
            }
        }
        if (flags & INSwchar_t)                 // convert to UNICODE
        {
#if LOCALE
            wchar_t wc = c;
            if (locale_broken ||
                mbtowc((wchar_t *)&tok_string[i],(char *)&wc,MB_CUR_MAX) <= 0)
#endif
                // Convert to unicode by 0 extension
                *(wchar_t *)(&tok_string[i]) = c;
            i += 2;
        }
        else
        {
            tok_string[i++] = c;        // store char in tok_string
        }
    }

  Ldone:
    switch (flags & (INSchar | INSwchar | INSdchar | INSwchar_t))
    {
        case 0:
        case INSchar:
            tok_string[i] = 0;
            if (ispascal)
            {
                if (i >= 0x100)
                    lexerr(EM_pascal_str_2long,i - 1);  // pascal string too long
                tok_string[0] = i - 1;
            }
#if PASCAL_STRINGS
            tok_passtr = ispascal;
#endif
            i++;
            break;

        case INSwchar_t:
            *(wchar_t *)(tok_string + i) = 0;
            if (ispascal)
                // Store length in first position
                *(wchar_t *)tok_string = (i - 2) >> 1;
            i += 2;
            break;

        case INSwchar:
            // Translate string in place from UTF8 to UTF16
            utfbuf->setsize(0);
            stringToUTF16((unsigned char *)tok_string, i);
            utfbuf->writeWord(0);                       // terminating 0
            i = utfbuf->size();
            if (ispascal)
                // Store length in first position
                *(wchar_t *)tok_string = (i - (2 + 2)) >> 1;
            tok_string_reserve(i);
            memcpy(tok_string, utfbuf->buf, i);
            break;

        case INSdchar:
            // Translate string in place from UTF8 to UTF32
            utfbuf->setsize(0);
            stringToUTF32((unsigned char *)tok_string, i);
            utfbuf->write32(0);                         // terminating 0
            i = utfbuf->size();
            if (ispascal)
                // Store length in first position
                *(long *)tok_string = (i - (4 + 4)) >> 1;
            tok_string_reserve(i);
            memcpy(tok_string, utfbuf->buf, i);
            break;

        default:
            assert(0);
    }
    egchar();
    return i;
}

/*************************************
 * Convert string[0..len] to UTF-16/32 and append it to utfbuf.
 */


void stringToUTF16(unsigned char *string, unsigned len)
{
    for (size_t j = 0; j < len; j++)
    {
        dchar_t dc = string[j];
        if (dc >= 0x80)
        {
            const char *msg = utf_decodeChar(string, len, &j, &dc);
            if (msg)
            {
                lexerr(EM_bad_utf, msg);
                continue;
            }
            j--;
            if (dc > 0xFFFF)
            {   // Encode surrogate pair
                utfbuf->writeWord((((dc - 0x10000) >> 10) & 0x3FF) + 0xD800);
                dc = ((dc - 0x10000) & 0x3FF) + 0xDC00;
            }
        }
        utfbuf->writeWord(dc);
    }
}

void stringToUTF32(unsigned char *string, unsigned len)
{
    for (size_t j = 0; j < len; j++)
    {
        dchar_t dc = string[j];
        if (dc >= 0x80)
        {
            const char *msg = utf_decodeChar(string, len, &j, &dc);
            if (msg)
            {
                lexerr(EM_bad_utf, msg);
                continue;
            }
            j--;
        }
        utfbuf->write32(dc);
    }
}

/*************************************
 * Determine if next token is a string. If so,
 * concatenate the strings.
 * Output:
 *      *plen   # of bytes in returned string
 *      tok.TKval       next token
 * Returns:
 *      copied string
 */

char *combinestrings(targ_size_t *plen)
{
    tym_t ty;
    char *p = combinestrings(plen, &ty);
    if (_tysize[ty] != 1)
        lexerr(EM_narrow_string);
    return p;
}

char *combinestrings(targ_size_t *plen, tym_t *ptym)
{
    assert(tok.TKval == TKstring);
    tym_t ty = tok.TKty;
    int lendec;
    if (!ty)
        lendec = 0;
    else
    {
        lendec = _tysize[ty];
    }
    char *mstring = (char *) MEM_PH_MALLOC(tok.TKlenstr);
    memcpy(mstring, tok.TKstr, tok.TKlenstr);
    targ_size_t len = tok.TKlenstr;

#define MSTRING_REALLOC(newsize) \
            if (newsize > STRMAX)               \
            {   lexerr(EM_string2big, STRMAX);  \
                err_nomem();                    \
            }                                   \
            mstring = (char *) MEM_PH_REALLOC(mstring, newsize);

    while (stoken() == TKstring)
    {
        assert(len >= lendec);
        len -= lendec;                          // disregard existing terminating 0
        if (ty == tok.TKty)
        {
            MSTRING_REALLOC(len + tok.TKlenstr);
            memcpy(mstring + len, tok.TKstr, tok.TKlenstr);
            len += tok.TKlenstr;
            continue;
        }

#define X(a,b) ((a << 8) | b)
        switch (X(ty, tok.TKty))
        {
            case X(TYchar, TYwchar_t):
                utfbuf->setsize(0);
                utfbuf->reserve(len * 2);
                for (size_t j = 0; j < len; j++)
                {
                    dchar_t dc = mstring[j];
                    utfbuf->writeWord(dc);
                }
            L1:
                len = utfbuf->size();
                MSTRING_REALLOC(len + tok.TKlenstr);
                memcpy(mstring, utfbuf->buf, len);
                memcpy(mstring + len, tok.TKstr, tok.TKlenstr);
                len += tok.TKlenstr;
                ty = tok.TKty;
                lendec = _tysize[ty];
                break;

            case X(TYchar, TYchar16):
                utfbuf->setsize(0);
                stringToUTF16((unsigned char *)mstring, len);
                goto L1;

            case X(TYchar, TYdchar):
                utfbuf->setsize(0);
                stringToUTF32((unsigned char *)mstring, len);
                goto L1;

            case X(TYwchar_t, TYchar):
                MSTRING_REALLOC(len + tok.TKlenstr * 2);
                for (size_t j = 0; j < tok.TKlenstr; j++)
                    *(unsigned short *)&mstring[len + j * 2] = tok.TKstr[j];
                len += tok.TKlenstr * 2;
                break;

            case X(TYchar16, TYchar):
                utfbuf->setsize(0);
                stringToUTF16((unsigned char *)tok.TKstr, tok.TKlenstr);
            L2:
            {
                size_t utfbuf_len = utfbuf->size();
                MSTRING_REALLOC(len + utfbuf_len);
                memcpy(mstring + len, utfbuf->buf, utfbuf_len);
                len += utfbuf_len;
                break;
            }

            case X(TYdchar, TYchar):
                utfbuf->setsize(0);
                stringToUTF32((unsigned char *)tok.TKstr, tok.TKlenstr);
                goto L2;

            default:
                lexerr(EM_mismatched_string);
                break;
        }
#undef X
    }
    *plen = len;
    *ptym = ty;
    return mstring;
}

/*************************************
 * Read in a character constant.
 * Input:
 *      flags = INSxxx
 * Output:
 *      tok.TKutok.Vlong        long integer
 * Returns:
 *      TKnum   integer constant
 */

STATIC enum_TK inchar(int flags)
{   int len;

    len = instring('\'',flags) - 1;     /* get the character string     */
                                        /* don't include 0 in length    */
    if (flags & (INSwchar | INSwchar_t | INSdchar))
        len--;
    if (flags & INSdchar)
        len -= 2;

  if (len > LONGSIZE)
  {     lexerr(EM_string2big,LONGSIZE);
        tok.TKty = TYint;
        return TKnum;
  }

#if !SPP
  /* If signed chars, this needs to be redone                           */
  if (chartype == tsuchar)
  {
        tok.TKty = TYuint;              // default
        if (len > intsize)
            tok.TKty = TYulong;         // else unsigned long
  }
  else
  {
        tok.TKty = TYint;
        if (len > intsize)
            tok.TKty = TYlong;          // else long
  }
#endif

    if (len == 1)
    {
        if (config.flags & CFGuchar || config.flags3 & CFG3ju)
            tok.TKutok.Vlong = (unsigned char) tok_string[0];
        else
            tok.TKutok.Vlong = (signed char) tok_string[0];
#if !SPP
        tok.TKty = chartype->Tty;
#endif
    }
    else
    {
        if (flags & INSwchar_t)
        {
            // To be MSVC compatible, only look at first Unicode char
            tok.TKutok.Vlong = *(targ_ushort *)tok_string;
            tok.TKty = (config.flags4 & CFG4wchar_t) ? TYwchar_t : TYushort;
        }
        else if (flags & INSwchar)
        {
            // To be MSVC compatible, only look at first Unicode char
            tok.TKutok.Vlong = *(targ_ushort *)tok_string;
            tok.TKty = TYchar16;
        }
        else if (flags & INSdchar)
        {
            // To be MSVC compatible, only look at first Unicode char
            tok.TKutok.Vlong = *(targ_ulong *)tok_string;
            tok.TKty = TYdchar;
        }
        else
        {
            tok.TKutok.Vlong = 0;
            char *p = (char *) &tok.TKutok.Vlong; // regard tok.TKutok.Vlong as a byte array
            while (len--)
                *p++ = tok_string[len]; // reverse the order of bytes
        }
    }
    return TKnum;
}


/*************************************
 * Read in and return character value from escape sequence.
 * Sequence can be:
 *      special character
 *      1..3 octal digits
 *      else is ignored (ascii value of following char is returned)
 * Input:
 *      xc = first char past the \
 * Output:
 *      xc = char past end of escape sequence
 * Returns:
 *      character value
 */

STATIC int escape()
{   int n;
    unsigned i;

    if (isoctal(xc))
    {   n = i = 0;
        do
        {   i = (i << 3) + xc - '0';
            n++;                        /* keep track of how many digits */
            egchar();
        } while (n < 3 && isoctal(xc));
        if (i & ~0xFF)
            lexerr(EM_badnumber);       // number is not representable
    }
    else
    {
        i = xc;
        egchar();
        switch (i)
        {
            case 'a':   i = 007; break;
            case 'n':   i = 012; break;
            case 't':   i = 011; break;
            case 'b':   i = 010; break;
            case 'v':   i = 013; break;
            case 'r':   i = 015; break;
            case 'f':   i = 014; break;
            case 'x':
                        if (!ishex(xc))
                        {   if (ANSI)
                                lexerr(EM_hexdigit,xc); // hex digit expected
                            break;
                        }
                        n = i = 0;
                        do
                        {   i <<= 4;
                            if (isdigit(xc))
                                i += xc - '0';
                            else
                                i += toupper(xc) - ('A' - 10);
                            n++;        /* keep track of how many digits */
                            egchar();
                        } while (n < 3 && ishex(xc));
                        break;

            case 'u':   // \uXXXX
                        n = 4;
                        goto L1;
            case 'U':   // \UXXXXXXXX
                        n = 8;
            L1:
                        i = 0;
                        for (; n; n--)
                        {
                            if (!ishex(xc))
                            {   lexerr(EM_hexdigit, xc);        // hex digit expected
                                break;
                            }
                            i <<= 4;
                            if (isdigit(xc))
                                i += xc - '0';
                            else
                                i += toupper(xc) - ('A' - 10);
                            egchar();
                        }
                        checkAllowedUniversal(i);
                        break;
            case '\'':
            case '\\':
            case '"':
            case '?':
                        break;
            default:
                        if (!expflag)
                            lexerr(EM_bad_escape_seq);  // undefined escape sequence
                        break;
        }
    }
    return i;
}

/*************************************
 * Check for allowed universal characters.
 */

STATIC void checkAllowedUniversal(unsigned uc)
{
    if (0)
    {
        // Check for disallowed characters per C++0x 2.2
        if (
            (uc >= 0xD800 && uc <= 0xDFFF)
           )
        {
            lexerr(EM_disallowed_char_name, uc);
        }
    }
    else if (CPP)
    {
        // Check for disallowed characters per C++98 2.2
        if (uc < 0x20 ||
            (uc >= 0x7F && uc <= 0x9F) ||
            (uc <= 0x7F && isbcs(uc))
#if WIN32
            || (uc & 0xFFFF0000)
#endif
           )
        {
            lexerr(EM_disallowed_char_name, uc);
        }
    }
    else
    {
        /* Check for disallowed characters per C99 6.4.3
         * Also for C++0x 2.2-2
         */
        if (
            (uc >= 0xD800 && uc <= 0xDFFF) ||
            (uc < 0xA0 && uc != 0x24 && uc != 0x40 && uc != 0x60)
#if WIN32
            || (uc & 0xFFFF0000)
#endif
           )
        {
            lexerr(EM_disallowed_char_name, uc);
        }
    }
}

/*************************************
 * Read in identifier and store it in tok_ident[].
 * Input:
 *      xc =    first char of identifier
 * Output:
 *      xc =            first char past identifier
 *      tok_ident[]     holds the identifier
 */

#if !__DMC__                            /* it's in loadline.c   */
void inident()
{
    int err = FALSE;
    char *p = &tok_ident[0];

    idhash = xc << 16;
    // printf("inident xc '%c', bl %x\n",xc,bl);
    *p++ = xc;
    while (isidchar(egchar()))
    {   if (p < &tok_ident[IDMAX])      /* if room left in tok_ident    */
            *p++ = xc;
        else
        {   if (!err)
                lexerr(EM_ident2big);   // identifier is too long
            err = TRUE;
        }
    }
    if (xc == '\\')
    {
        inidentX(p);
        return;
    }
    *p = 0;                             /* terminate string             */
    //printf("After inident xc '%c', bl %x, id %s\n",xc,bl,tok_ident);
    idhash += ((p - tok_ident) << 8) + (*(p - 1) & 0xFF);
}
#endif

/********************************
 * Look for \uxxxx or \UXXXXXXXX
 */

void inidentX(char *p)
{
    int err = FALSE;

    while (1)
    {
        if (isidchar(xc))
        {
            if (p < &tok_ident[IDMAX])  /* if room left in tok_ident    */
                *p++ = xc;
            else
            {   if (!err)
                    lexerr(EM_ident2big);       // identifier is too long
                err = TRUE;
            }
        }
        else if (xc == '\\')
        {   unsigned long uc;
            unsigned n;

            egchar();
            if (xc == 'u')
                n = 4;
            else if (xc == 'U')
                n = 8;
            else
                goto Lerr;
            // No identfier chars consume more than 3 UTF-8 bytes
            if ((p + 3) - tok_ident > IDMAX)
            {
                lexerr(EM_ident2big);   // identifier is too long
                goto Ldone;
            }

            uc = 0;
            while (n--)
            {
                egchar();
                if (!ishex(xc))
                {   lexerr(EM_hexdigit,xc);     // hex digit expected
                    goto Ldone;
                }
                uc <<= 4;
                if (isdigit(xc))
                    uc += xc - '0';
                else
                    uc += toupper(xc) - ('A' - 10);
            }
            checkAllowedUniversal(uc);
            if (!isUniAlpha(uc))
                lexerr(EM_not_universal_idchar, uc);
            else if (uc <= 0x7F)
            {
                *p++ = (unsigned char)uc;
            }
            else if (uc <= 0x7FF)
            {
                p[0] = (unsigned char)((uc >> 6) | 0xC0);
                p[1] = (unsigned char)((uc & 0x3F) | 0x80);
                p += 2;
            }
            else if (uc <= 0xFFFF)
            {
                p[0] = (unsigned char)((uc >> 12) | 0xE0);
                p[1] = (unsigned char)(((uc >> 6) & 0x3F) | 0x80);
                p[2] = (unsigned char)((uc & 0x3F) | 0x80);
                p += 3;
            }
        }
        else
            goto Ldone;
        egchar();
    }

Lerr:
    lexerr(EM_badtoken);
Ldone:
    *p = 0;
    idhash = tok_ident[0] << 16;
    idhash += ((p - tok_ident) << 8) + (*(p - 1) & 0xFF);
}

/*********************************
 * Compute and return hash value of string.
 * Must use same algorithm as inident().
 */

unsigned comphash(const char *p)
{       int idlen;

        idlen = strlen(p);
        return (((*p << 8) + idlen) << 8) + (p[idlen - 1] & 0xFF);;
}

/**************************************
 * Read in a number.
 * If it's an integer, store it in tok.TKutok.Vlong.
 *      integers can be decimal, octal or hex
 *      Handle the suffixes U, UL, LU, L, etc. Allows more cases than
 *      the Ansii C spec allows.
 * If it's double, store it in tok.TKutok.Vdouble.
 * Returns:
 *      TKnum
 *      TKdouble,...
 */

STATIC enum_TK innum()
{
    /* We use a state machine to collect numbers        */
    enum STATE { STATE_initial, STATE_0, STATE_decimal, STATE_octal, STATE_octale,
        STATE_hex, STATE_binary, STATE_hex0, STATE_binary0,
        STATE_hexh, STATE_error };
    enum STATE state;

#define FLAGS_decimal   1               /* decimal                      */
#define FLAGS_unsigned  2               /* u or U suffix                */
#define FLAGS_long      4               /* l or L suffix                */
#define FLAGS_llong     8               // LL or ll suffix
    unsigned char flags = FLAGS_decimal;

    int i;
    int base;

    state = STATE_initial;
    i = 0;
    base = 0;
    while (1)
    {
        switch (state)
        {
            case STATE_initial:         /* opening state                */
                if (xc == '0')
                    state = STATE_0;
                else
                    state = STATE_decimal;
                break;
            case STATE_0:
                flags &= ~FLAGS_decimal;
                switch (xc)
                {
                    case 'H':                   // 0h
                    case 'h':
                        goto hexh;
                    case 'X':
                    case 'x':                   // 0x
                        state = STATE_hex0;
                        break;
                    case '.':                   // 0.
                        goto real;
                    case 'E':
                    case 'e':                   // 0e
                        if (ANSI)
                            goto real;
                        goto case_hex;
                    case 'B':
                    case 'b':                   // 0b
                        if (!ANSI)
                        {   state = STATE_binary0;
                            break;
                        }
                        goto case_hex;

                    case '0': case '1': case '2': case '3':
                    case '4': case '5': case '6': case '7':
                        state = STATE_octal;
                        break;

                    case '8': case '9':
                        if (ANSI)
                            goto real;
                        goto case_hex;

                    case 'A':
                    case 'C': case 'D': case 'F':
                    case 'a': case 'c': case 'd': case 'f':
                    case_hex:
                        if (ANSI)
                        {
                            lexerr(EM_octal_digit);     // octal digit expected
                            state = STATE_error;
                        }
                        else
                            state = STATE_hexh;
                        break;
                    default:
                        goto done;
                }
                break;

            case STATE_decimal:         /* reading decimal number       */
                if (!isdigit(xc))
                {
#if SPP
                if (i == 0)
#endif
                    if (ishex(xc) || xc == 'H' || xc == 'h')
                        goto hexh;
                    if (xc == '.')
                    {
            real:       /* It's a real number. Rescan as a real         */
                        tok_string[i] = 0;      /* already consumed chars */
                        return inreal(tok_string);
                    }
                    goto done;
                }
                break;

            case STATE_hex0:            /* reading hex number           */
            case STATE_hex:
                if (!ishex(xc))
                {
                    if (xc == '.' || xc == 'P' || xc == 'p')
                        goto real;
                    if (state == STATE_hex0)
                        lexerr(EM_hexdigit,xc); // hex digit expected
                    goto done;
                }
                state = STATE_hex;
                break;

            hexh:
                state = STATE_hexh;
            case STATE_hexh:            // parse numbers like 0FFh
                if (!ishex(xc))
                {
                    if (xc == 'H' || xc == 'h')
                    {
                        egchar();
                        base = 16;
                        goto done;
                    }
                    else if (xc == '.')         // parse 08.5, 09.
                        goto real;
                    else
                    {
                        // Check for something like 1E3 or 0E24 or 09e0
                        if (memchr(tok_string,'E',i) ||
                            memchr(tok_string,'e',i))
                            goto real;
#if SPP
                        if (ANSI)
#endif
                            lexerr(EM_hexdigit,xc);     // hex digit expected
                        goto done;
                    }
                }
                break;

            case STATE_octal:           /* reading octal number         */
            case STATE_octale:          /* reading octal number         */
                if (!isoctal(xc))
                {   if ((ishex(xc) || xc == 'H' || xc == 'h') && !ANSI)
                        goto hexh;
                    if (xc == '.')
                        goto real;
                    if (isdigit(xc))
                    {
#if SPP
                        lexerr(EM_octal_digit); // octal digit expected
                        state = STATE_error;
                        break;
#else
                        state = STATE_octale;
#endif
                    }
                    else
                        goto done;
                }
                break;

            case STATE_binary0:         /* starting binary number       */
            case STATE_binary:          /* reading binary number        */
                if (xc != '0' && xc != '1')
                {   if (ishex(xc) || xc == 'H' || xc == 'h')
                        goto hexh;
                    if (state == STATE_binary0)
                    {   lexerr(EM_0or1);        // binary digit expected
                        state = STATE_error;
                        break;
                    }
                    else
                        goto done;
                }
                state = STATE_binary;
                break;

            case STATE_error:           /* for error recovery           */
                if (!isdigit(xc))       /* scan until non-digit         */
                    goto done;
                break;

            default:
                assert(0);
        }
        if (i >= tok_strmax)
        {       tok_strmax += 50;
                tok_string = (char *) MEM_PARC_REALLOC(tok_string,tok_strmax + 1);
        }
        tok_string[i++] = xc;
        egchar();
    }
done:
    tok_string[i] = 0;                  /* end of tok_string            */
#if !SPP
    if (state == STATE_octale)
        lexerr(EM_octal_digit);         // octal digit expected
#endif

    errno = 0;
    if (i == 1 && (state == STATE_decimal || state == STATE_0))
        tok.TKutok.Vllong = tok_string[0] - '0';
    else
        tok.TKutok.Vllong = strtoull(tok_string,NULL,base); /* convert string to integer    */
    if (errno == ERANGE)                /* if overflow                  */
        lexerr(EM_badnumber);           // overflow

        /* Parse trailing 'u', 'U', 'l' or 'L' in any combination       */
        while (TRUE)
        {   unsigned char f;

            switch (xc)
            {   case 'U':
                case 'u':
                    f = FLAGS_unsigned;
                    goto L1;
                case 'L':
                case 'l':
                    f = FLAGS_long;
                    if (flags & f)
                    {   f = FLAGS_llong;
                        if (intsize == 2)
                            lexerr(EM_no_longlong);     // long long not supported
                    }
                L1:
                    egchar();
                    if (flags & f)
                        lexerr(EM_badtoken);    /* unrecognized token   */
                    flags |= f;
                    continue;
                default:
                    break;
            }
            break;
        }

#if !SPP
        /* C99 6.4.4.1 -5
         */
        /* C++98 2.13.1-2: The type of an integer literal depends on its form,
         * value, and suffix. If it is decimal and has no suffix, it has
         * the first of these types in which its value can be represented: int,
         * long int; if the value cannot be represented as a long int, the
         * behavior is undefined. If it is octal or hexadecimal and has no
         * suffix, it has the first of these types in which its value can be
         * represented: int, unsigned int, long int, unsigned long int. If it is
         * suffixed by u or U, its type is the first of these types in which its
         * value can be represented: unsigned int, unsigned long int. If it is
         * suffixed by l or L, its type is the first of these types in which its
         * value can be represented: long int, unsigned long int. If it is
         * suffixed by ul, lu, uL, Lu, Ul, lU, UL, or LU, its type is unsigned
         * long int.
         */

        debug(assert(sizeof(long) == 4));       // some dependencies
        debug(assert(intsize == 2 || intsize == 4));
        tok.TKty = TYint;                       // default
        switch (flags)
        {
            case 0:
                /* Octal or Hexadecimal constant.
                 * First that fits: int, unsigned, long, unsigned long,
                 * long long, unsigned long long
                 */
                if (intsize == 4)
                {
                    if (tok.TKutok.Vllong & 0x8000000000000000LL)
                        tok.TKty = TYullong;
                    else if (tok.TKutok.Vllong & 0xFFFFFFFF00000000LL)
                        tok.TKty = TYllong;
                    else if (tok.TKutok.Vlong & 0x80000000)
                        tok.TKty = TYuint;
                    else if (preprocessor)
                        tok.TKty = TYlong;
                    else
                        tok.TKty = TYint;
                }
                else
                {
                    if (tok.TKutok.Vllong & 0xFFFFFFFF00000000LL)
                        lexerr(EM_badnumber);           // overflow
                    else if (tok.TKutok.Vlong & 0x80000000)
                        tok.TKty = TYulong;
                    else if (tok.TKutok.Vlong & 0xFFFF0000 || preprocessor)
                        tok.TKty = TYlong;
                    else if (tok.TKutok.Vlong & 0x8000)
                        tok.TKty = TYuint;
                    else
                        tok.TKty = TYint;
                }
                break;
            case FLAGS_decimal:
                /* First that fits: int, long, long long
                 */
                if (intsize == 4)
                {
                    if (tok.TKutok.Vllong & 0x8000000000000000LL)
                        lexerr(EM_badnumber);           // overflow
                    else if (tok.TKutok.Vllong & 0xFFFFFFFF80000000LL)
                        tok.TKty = TYllong;
                    else if (preprocessor)
                        tok.TKty = TYlong;
                    else
                        tok.TKty = TYint;
                }
                else
                {   // intsize == 2
                    if (tok.TKutok.Vllong & 0xFFFFFFFF80000000LL)
                        lexerr(EM_badnumber);           // overflow
                    else if (tok.TKutok.Vlong & 0xFFFF8000 || preprocessor)
                        tok.TKty = TYlong;
                    else
                        tok.TKty = TYint;
                }
                break;
            case FLAGS_unsigned:
            case FLAGS_decimal | FLAGS_unsigned:
                /* First that fits: unsigned, unsigned long, unsigned long long
                 */
                if (intsize == 4)
                {
                    if (tok.TKutok.Vllong & 0xFFFFFFFF00000000LL)
                        tok.TKty = TYullong;
                    else if (preprocessor)
                        tok.TKty = TYulong;
                    else
                        tok.TKty = TYuint;
                }
                else
                {
                    if (tok.TKutok.Vllong & 0xFFFFFFFF00000000LL)
                        lexerr(EM_badnumber);           // overflow
                    else if (tok.TKutok.Vlong & 0xFFFF0000 || preprocessor)
                        tok.TKty = TYulong;
                    else
                        tok.TKty = TYuint;
                }
                break;
            case FLAGS_decimal | FLAGS_long:
                /* First that fits: long, long long
                 */
                if (intsize == 4)
                {
                    if (tok.TKutok.Vllong & 0x8000000000000000LL)
                        lexerr(EM_badnumber);           // overflow
                    else if (tok.TKutok.Vllong & 0xFFFFFFFF80000000LL)
                        tok.TKty = TYllong;
                    else
                        tok.TKty = TYlong;
                }
                else
                {
                    if (tok.TKutok.Vllong & 0xFFFFFFFF80000000LL)
                        lexerr(EM_badnumber);           // overflow
                    else
                        tok.TKty = TYlong;
                }
                break;
            case FLAGS_long:
                /* First that fits: long, unsigned long, long long,
                 * unsigned long long
                 */
                if (intsize == 4)
                {
                    if (tok.TKutok.Vllong & 0x8000000000000000LL)
                        tok.TKty = TYullong;
                    else if (tok.TKutok.Vllong & 0xFFFFFFFF00000000LL)
                        tok.TKty = TYllong;
                    else if (tok.TKutok.Vlong & 0x80000000)
                        tok.TKty = TYulong;
                    else
                        tok.TKty = TYlong;
                }
                else
                {
                    if (tok.TKutok.Vllong & 0xFFFFFFFF00000000LL)
                        lexerr(EM_badnumber);           // overflow
                    else if (tok.TKutok.Vlong & 0x80000000)
                        tok.TKty = TYulong;
                    else
                        tok.TKty = TYlong;
                }
                break;
            case FLAGS_unsigned | FLAGS_long:
            case FLAGS_decimal | FLAGS_unsigned | FLAGS_long:
                /* First that fits: unsigned long, unsigned long long
                 */
                if (intsize == 4)
                {
                    if (tok.TKutok.Vllong & 0xFFFFFFFF00000000LL)
                        tok.TKty = TYullong;
                    else
                        tok.TKty = TYulong;
                }
                else
                {
                    if (tok.TKutok.Vllong & 0xFFFFFFFF00000000LL)
                        lexerr(EM_badnumber);           // overflow
                    else
                        tok.TKty = TYulong;
                }
                break;
            case FLAGS_long | FLAGS_llong:
                /* First that fits: long long, unsigned long long
                 */
                if (intsize == 4)
                {
                    if (tok.TKutok.Vllong & 0x8000000000000000LL)
                        tok.TKty = TYullong;
                    else
                        tok.TKty = TYllong;
                }
                else
                {
                    lexerr(EM_badnumber);               // overflow
                }
                break;
            case FLAGS_long | FLAGS_decimal | FLAGS_llong:
                /* long long
                 */
                tok.TKty = TYllong;
                if (intsize == 2)
                    lexerr(EM_badnumber);               // overflow
                break;
            case FLAGS_long | FLAGS_unsigned | FLAGS_llong:
            case FLAGS_long | FLAGS_decimal | FLAGS_unsigned | FLAGS_llong:
                tok.TKty = TYullong;
                break;
            default:
#ifdef DEBUG
                printf("%x\n",flags);
#endif
                assert(0);
        }

#if 0 // redundant
        // Check for overflow
        if (tok.TKutok.Vllong & 0xFFFFFFFF00000000LL &&
            tok.TKty != TYllong && tok.TKty != TYullong)
        {
            warerr(WM_badnumber);
        }
#endif
#endif
        return TKnum;
}

#if TX86

/**************************************
 * Read in characters, converting them to real.
 * Input:
 *      p ->    characters already read into double
 *      xc      next char
 * Output:
 *      xc      char past end of double
 *      tok.TKutok.Vdouble      the double read in
 * Bugs:
 *      Exponent overflow not detected.
 *      Too much requested precision is not detected.
 */

STATIC enum_TK inreal(const char *p)
{ int dblstate;
  int chrstate;
  int i;
  char c;
  char hex;                     /* is this a hexadecimal-floating-constant? */
  enum_TK result;

  i = 0;
  dblstate = 0;
  chrstate = 0;
  hex = 0;
  while (1)
  {
        // Get next char from p, xc, or egchar()
        if (chrstate)
            c = egchar();
        else
        {
            c = *p++;
            if (!c)
            {   c = xc;
                chrstate++;
            }
        }

        while (1)
        {   switch (dblstate)
            {
                case 0:                 /* opening state                */
                    if (c == '0')
                        dblstate = 9;
                    else
                        dblstate = 1;
                    break;

                case 9:
                    dblstate = 1;
                    if (c == 'X' || c == 'x')
                    {   hex++;
                        break;
                    }
                    /* FALL-THROUGH */

                case 1:                 /* digits to left of .          */
                case 3:                 /* digits to right of .         */
                case 7:                 /* continuing exponent digits   */
                    if (!isdigit(c) && !(hex && isxdigit(c)))
                    {   dblstate++;
                        continue;
                    }
                    break;

                case 2:                 /* no more digits to left of .  */
                    if (c == '.')
                    {   dblstate++;
                        break;
                    }
                    /* FALL-THROUGH */

                case 4:                 /* no more digits to right of . */
                    if ((c == 'E' || c == 'e') ||
                        hex && (c == 'P' || c == 'p'))
                    {   dblstate = 5;
                        hex = 0;        /* exponent is always decimal   */
                        break;
                    }
                    if (hex)
                        lexerr(EM_binary_exp);  // binary-exponent-part required
                    goto done;

                case 5:                 /* looking immediately to right of E */
                    dblstate++;
                    if (c == '-' || c == '+')
                        break;
                    /* FALL-THROUGH */

                case 6:                 /* 1st exponent digit expected  */
                    if (!isdigit(c))
                        lexerr(EM_exponent);    // exponent expected
                    dblstate++;
                    break;

                case 8:                 /* past end of exponent digits  */
                    goto done;
            }
            break;
        }
        if (i >= tok_strmax)
        {       tok_strmax += 50;
                tok_string = (char *) mem_realloc(tok_string,tok_strmax + 1);
        }
        tok_string[i++] = c;
  }
done:
    tok_string[i] = 0;
    errno = 0;
#if _WIN32 && __DMC__
    char *save = __locale_decpoint;
    __locale_decpoint = ".";
#endif

    switch (xc)
    {
        case 'F':
        case 'f':
#if __GNUC__
            tok.TKutok.Vfloat = (float)strtod(tok_string, NULL);
#else
            tok.TKutok.Vfloat = strtof(tok_string, NULL);
#endif
            result = TKreal_f;
            egchar();
            break;
        case 'L':
        case 'l':
            if (LDOUBLE)
            {
                tok.TKutok.Vldouble = strtold(tok_string, NULL);
                result = TKreal_ld;
            }
            else
            {   tok.TKutok.Vdouble = strtod(tok_string, NULL);
                result = TKreal_da;
            }
            egchar();
            break;
        default:
            tok.TKutok.Vdouble = strtod(tok_string, NULL);
            result = TKreal_d;
            break;
    }
#if _WIN32 && __DMC__
    __locale_decpoint = save;
#endif
    // ANSI C99 says let it slide
    if (errno == ERANGE && !ANSI)
        warerr(WM_badnumber);           // number is not representable
    return result;
}

#endif


/****************************
 * Read in pragma.
 * Pragmas must start in first column.
 * pragma ::= "#" [identifier]
 * Returns:
 *      true    it's a pragma; tok is set to which one
 *      false   it's just # followed by whitespace
 */

STATIC bool inpragma()
{

    while (1)
    {
        if (isidstart(xc))
        {
            inident();                  // read in identifier
            tok.TKutok.pragma = pragma_search(tok_ident);
            tok.TKval = TKpragma;
            return true;
        }
        else if (isdigit(xc))
        {
            tok.TKutok.pragma = pragma_search("__linemarker");
            tok.TKval = TKpragma;
            return true;
        }

        switch (xc)
        {   case ' ':
            case '\t':                  /* whitespace between # and ident */
            case CR:
                egchar();
                continue;
            case LF:
#if SPP
                expflag--;
#endif
                break;
            case '/':
                if (egchar() == '/')
                {   cppcomment();
                    continue;
                }
                if (xc == '*')
                {   comment();
                    continue;
                }
                /* FALL-THROUGH */
            default:
                lexerr(EM_ident_exp);   // identifier expected
                break;
        }
        break;
    }
    return false;
}

/*******************************************
 * Determine if a space needs to be inserted to prevent
 * token concatenation.
 */

int insertSpace(unsigned char xclast, unsigned char xcnext)
{
    unsigned char ctlast = _chartype[xclast + 1];
    if (ctlast & _TOK)
        return 0;

    unsigned char ctnext = _chartype[xcnext + 1];
    if (ctnext & _TOK)
        return 0;

#if 0
    return ctlast & (_ID | _IDS | _MTK) && ctnext & (_ID | _IDS | _MTK);
#else
    return ctlast & (_ID | _IDS) && ctnext & (_ID | _IDS) ||
           ctlast & _MTK && ctnext & _MTK;
#endif
}

/*********************
 * Panic mode. Discard tokens until a specified
 * one is encountered. Watch out for end of file!
 */

void panic(enum_TK ptok)
{
    while (ptok != tok.TKval)
    {   if (tok.TKval == TKeof)
            err_fatal(EM_eof);                  /* premature end of source file */
        stoken();
    }
}


/***********************************
 * Check token. If it matches tok, get next token,
 * else print error message.
 */

void chktok(enum_TK toknum,unsigned errnum)
{
    if (tok.TKval != toknum)
        synerr(errnum);
    stoken();                   // scan past token
}


void chktok(enum_TK toknum,unsigned errnum, const char *str)
{
  if (tok.TKval == toknum)
        stoken();                       /* scan past token              */
  else
        synerr(errnum, str);
}


/*********************************
 * Look at next token. If we have a match, then
 * throw it away and read in the next one. This is
 * used mostly when optional semicolons are in the syntax.
 */

void opttok(enum_TK toknum)
{
  if (tok.TKval == toknum) stoken();
}

/*************************************************
 * white_space_char ::= space | tab | end_of_line
 * End_of_lines are not white_space if preprocessor != 0.
 */

bool iswhite(int c)                     /* is c white space?            */
{ return((c == ' ') || (c == '\t') || (!preprocessor && c == LF)); }


/**********************************
 * Binary string search.
 * Input:
 *      p ->    string of characters
 *      tab     array of pointers to strings
 *      n =     number of pointers in the array
 * Returns:
 *      index (0..n-1) into tab[] if we found a string match
 *      else -1
 */

#if !(TX86 && __DMC__ && !_DEBUG_TRACE)

int binary(const char *p, const char * *table,int high)
{ int low,mid;
  signed char cond;
  char cp;

  low = 0;
  high--;
  cp = *p;
  p++;
  while (low <= high)
  {     mid = (low + high) >> 1;
        if ((cond = table[mid][0] - cp) == 0)
            cond = strcmp(table[mid] + 1,p);
        if (cond > 0)
            high = mid - 1;
        else if (cond < 0)
            low = mid + 1;
        else
            return mid;                 /* match index                  */
  }
  return -1;
}

#else

int binary(const char *p, const char * *table,int high)
{
#define len high        // reuse parameter storage
    _asm
    {

;First find the length of the identifier.
        xor     EAX,EAX         ;Scan for a 0.
        mov     EDI,p
        mov     ECX,EAX
        dec     ECX             ;Longest possible string.
        repne   scasb
        mov     EDX,high        ;EDX = high
        not     ECX             ;length of the id including '/0', stays in ECX
        dec     EDX             ;high--
        js      short Lnotfound
        dec     EAX             ;EAX = -1, so that eventually EBX = low (0)
        mov     len,ECX

        even
L4D:    lea     EBX,1[EAX]      ;low = mid + 1
        cmp     EBX,EDX
        jg      Lnotfound

        even
L15:    lea     EAX,[EBX + EDX] ;EAX = low + high

;Do the string compare.

        mov     EDI,table
        sar     EAX,1           ;mid = (low + high) >> 1;
        mov     ESI,p
        mov     EDI,[4*EAX+EDI] ;Load table[mid]
        mov     ECX,len         ;length of id
        repe    cmpsb

        je      short L63       ;return mid if equal
        jns     short L4D       ;if (cond < 0)
        lea     EDX,-1[EAX]     ;high = mid - 1
        cmp     EBX,EDX
        jle     L15

Lnotfound:
        mov     EAX,-1          ;Return -1.

        even
L63:
    }
#undef len
}

#endif

/******************************************
 */
void RawString::init()
{
    rawstate = RAWdchar;
    dchari = 0;
}

bool RawString::inString(unsigned char c)
{
    switch (rawstate)
    {
        case RAWdchar:
            if (c == '(')       // end of d-char-string
            {
                dcharbuf[dchari] = 0;
                rawstate = RAWstring;
            }
            else if (c == ' '  || c == '('  || c == ')'  ||
                     c == '\\' || c == '\t' || c == '\v' ||
                     c == '\f' || c == '\n')
            {
                lexerr(EM_invalid_dchar, c);
                rawstate = RAWerror;
            }
            else if (dchari >= sizeof(dcharbuf) - 1)
            {
                lexerr(EM_string2big, sizeof(dcharbuf) - 1);
                rawstate = RAWerror;
            }
            else
            {
                dcharbuf[dchari] = c;
                ++dchari;
            }
            break;

        case RAWstring:
            if (c == ')')
            {
                dchari = 0;
                rawstate = RAWend;
            }
            break;

        case RAWend:
            if (c == dcharbuf[dchari])
            {
                ++dchari;
            }
            else if (dcharbuf[dchari] == 0)
            {
                if (c == '"')
                    rawstate = RAWdone;
                else
                    rawstate = RAWstring;
            }
            else if (c == ')')
            {
                // Rewind ')' dcharbuf[0..dchari]
                dchari = 0;
            }
            else
            {
                // Rewind ')' dcharbuf[0..dchari]
                rawstate = RAWstring;
            }
            break;

        default:
            assert(0);
    }
    return rawstate != RAWdone && rawstate != RAWerror;
}

/**********************************
 * Terminate use of scanner
 */

#if TERMCODE
void token_term()
{
    token_t *tn;

    for (; token_freelist; token_freelist = tn)
    {   tn = token_freelist->TKnext;
#if TX86
        mem_ffree(token_freelist);
#else
        MEM_PH_FREE(token_freelist);
#endif
    }
    MEM_PARC_FREE(tok_string);
    MEM_PARC_FREE(tok_arg);
}
#endif

#ifdef DEBUG

/*******************************
 * Type token information.
 */

void token_t::print()
{   int i;

    dbg_printf("this->TKval = %3d ",this->TKval);
    switch (this->TKval)
    {   case TKsemi:        dbg_printf("';'");      break;
        case TKcolon:       dbg_printf("':'");      break;
        case TKcolcol:      dbg_printf("'::'");     break;
        case TKcomma:       dbg_printf("','");      break;
        case TKstar:        dbg_printf("'*'");      break;
        case TKlcur:        dbg_printf("'{'");      break;
        case TKrcur:        dbg_printf("'}'");      break;
        case TKlpar:        dbg_printf("'('");      break;
        case TKrpar:        dbg_printf("')'");      break;
        case TKeq:          dbg_printf("'='");      break;
        case TKlt:          dbg_printf("'<'");      break;
        case TKgt:          dbg_printf("'>'");      break;
        case TKlg:          dbg_printf("'<>'");     break;
        case TKne:          dbg_printf("'!='");     break;
        case TKeol:         dbg_printf("'EOL'");    break;
        case TKeof:         dbg_printf("'EOF'");    break;
        case TKnone:        dbg_printf("'none'");   break;
        case TKvoid:        dbg_printf("void");   break;
        case TKchar:        dbg_printf("char");   break;
        case TKconst:       dbg_printf("const");   break;
        case TKclass:       dbg_printf("class");      break;
        case TKtypename:    dbg_printf("typename");   break;

        case TKnum:
            dbg_printf("Vlong = %ld",this->TKutok.Vlong);
            break;
        case TKstring:
        case TKfilespec:
            dbg_printf("string = '");
            for (i = 0; i < this->TKlenstr; i++)
                dbg_fputc(this->TKstr[i],stdout);
            break;
        case TKdouble:
        case TKreal_da:
            dbg_printf("double = %g",this->TKutok.Vdouble); break;
        case TKpragma:
            dbg_printf("pragma = %d",this->TKutok.pragma); break;
        case TKident:
            dbg_printf("ident = '%s'",this->TKid); break;
        case TKsymbol:
            dbg_printf("Symbol = '%s'",this->TKsym->Sident); break;
        default:
            dbg_printf(", TKMAX = %d",TKMAX);
    }
    dbg_printf("\n");
}

void token_funcbody_print(token_t *t)
{
    while(t)
    {
        t->print();
        t = t->TKnext;
    }
}

#endif
