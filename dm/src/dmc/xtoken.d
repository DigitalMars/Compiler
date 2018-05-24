/**
 * Implementation of the
 * $(LINK2 http://www.digitalmars.com/download/freecompiler.html, Digital Mars C/C++ Compiler).
 *
 * Copyright:   Copyright (c) 1983-1998 by Symantec, All Rights Reserved
 *              Copyright (c) 2000-2017 by Digital Mars, All Rights Reserved
 * Authors:     $(LINK2 http://www.digitalmars.com, Walter Bright)
 * License:     Distributed under the Boost Software License, Version 1.0.
 *              http://www.boost.org/LICENSE_1_0.txt
 * Source:      https://github.com/DigitalMars/Compiler/blob/master/dm/src/dmc/xtoken.d
 */

// Lexical analyzer

module xtoken;

import core.stdc.ctype;
import core.stdc.stdio;
import core.stdc.stdlib;
import core.stdc.string;
import core.stdc.errno;
version (Win32)
{
    extern (C) int* _errno();   // not the multi-threaded version
}
import core.stdc.locale;

import dmd.backend.cdef;
import dmd.backend.cc;
import dmd.backend.el;
import dmd.backend.global;
import dmd.backend.outbuf;
import dmd.backend.ty;
import dmd.backend.type;

import dmd.backend.dlist;
import tk.mem;

import dtoken;
import msgs2;
import parser;
import phstring;
import precomp;
import rawstring;
import utf;

extern (C++):

alias dbg_printf = printf;
alias dbg_fputc = fputc;
alias MEM_PH_MALLOC = mem_malloc;
alias MEM_PH_CALLOC = mem_calloc;
alias MEM_PH_FREE = mem_free;
alias MEM_PH_STRDUP = mem_strdup;
alias MEM_PH_REALLOC = mem_realloc;
alias MEM_PARF_MALLOC = mem_malloc;
alias MEM_PARF_CALLOC = mem_calloc;
alias MEM_PARF_REALLOC = mem_realloc;
alias MEM_PARF_FREE = mem_free;
alias MEM_PARF_STRDUP = mem_strdup;

enum MEM_PARF_FREEFP = &mem_freefp;
alias MEM_PERM_REALLOC = mem_realloc;

alias MEM_PARC_REALLOC = mem_realloc;

enum TX86 = 1;

version (Win32)
{
// from \sc\src\include\setlocal.h
extern (C) extern __gshared char* __locale_decpoint;
}

enum FASTKWD = 1;               // use fast instead of small kwd lookup
/* Globals:
 */

bool isoctal(char x) { return '0' <= x && x < '8'; }
bool isleadbyte(uint c) { return false; }

version (DigitalMars)
{
    alias ishex = isxdigit;
}
else
{
    bool ishex(char x) { return isxdigit(x) || isdigit(x); }
}

private enum_TK innum();
private enum_TK inchar(int flags);
private int escape();
private int instring(int tc, int flags);
private enum_TK inreal(const(char)*);
private void checkAllowedUniversal(uint uc);
void stringToUTF16(ubyte* string, uint len);
void stringToUTF32(ubyte* string, uint len);
void tok_string_reserve(uint nbytes);

// Flags for instring()
enum
{
    INSnoescape   = 1,      // escape sequences are not allowed
                            // (useful for pathnames in include
                            // files under MSDOS or OS2)
    INSwchar_t    = 2,      // L
    INSchar       = 4,      // u8
    INSwchar      = 8,      // u
    INSdchar      = 0x10,   // U
    INSraw        = 0x20,   // R
}

enum LOCALE         =       0;       // locale support for Unicode conversion
enum PASCAL_STRINGS =       0;

__gshared
{
char[2*IDMAX + 1] tok_ident = void;  /* identifier                   */
private char *tok_string;        /* for strings (not null terminated)    */
private int tok_strmax;          /* length of tok_string buffer          */

Outbuffer *utfbuf;

static if ( PASCAL_STRINGS)
{
private char tok_passtr;         /* pascal string in tok_string          */
}

token_t tok /*= { TKnone }*/;       /* last token scanned                   */
int ininclude;                  /* if in #include line                  */
int igncomment;                 // 1 if ignore comment in preprocessed output

char *tok_arg;                  /* argument buffer                      */
uint argmax;                /* length of argument buffer            */
}

__gshared
{
token_t *toklist;
private list_t toksrclist;
}

 __gshared
private token_t *token_freelist;


int isUniAlpha(uint u);
void cppcomment();
private bool inpragma();

/* This is so the isless(), etc., macros in math.h work even with -A99
 */
enum EXTENDED_FP_OPERATORS = 1;
//enum EXTENDED_FP_OPERATORS = !ANSI_STRICT;


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


__gshared ubyte[257] _chartype =
[       0,                      // in case we use EOF as an index
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
];


/*********************************
 * Make a copy of the current token.
 */

version (SPP)
{
}
else
{

token_t *token_copy()
{   token_t *t;
    size_t len;

    /* Try to get a token_t from the free list  */
    if (token_freelist)
    {   t = token_freelist;
        token_freelist = t.TKnext;
    }
    else
    {
static if (TX86)
{
        t = cast(token_t *) mem_fmalloc(token_t.sizeof);
}
else
{
        t = cast(token_t *) MEM_PH_MALLOC(token_t.sizeof);
}
        // tokens are kept in ph and on available free list
    }
    *t = tok;
    debug t.id = token_t.IDtoken;
                                        /* token_linnum will set TokenFile */
    t.TKsrcpos = token_linnum();
    t.TKnext = null;
    t.TKflags = 0;
    switch (tok.TKval)
    {
        case TKident:
            len = strlen(tok.TKid);
            if (len < (t.idtext).sizeof)
            {   memcpy(&t.idtext[0],tok.TKid,len + 1);
                t.TKid = &t.idtext[0];
            }
            else
            {
                t.TKid = cast(char *) MEM_PH_MALLOC(len + 1);
                memcpy(t.TKid,tok.TKid,len + 1);
            }
            break;
        case TKstring:
        case TKfilespec:
            t.TKstr = cast(char *) MEM_PH_MALLOC(tok.TKlenstr);
            memcpy(t.TKstr,tok.TKstr,tok.TKlenstr);
            break;

        default:
            break;
    }
    return t;
}

}

/********************************
 * Free a token list.
 */

version (SPP)
{
}
else
{

void token_free(token_t *tl)
{   token_t *tn;

    /*dbg_printf("token_free(%p)\n",tl);*/
    while (tl)
    {   token_debug(tl);
        tn = tl.TKnext;
        switch (tl.TKval)
        {   case TKident:
                if (tl.TKid != &tl.idtext[0])
                    MEM_PH_FREE(tl.TKid);
                break;
            case TKstring:
            case TKfilespec:
                MEM_PH_FREE(tl.TKstr);
                break;

            default:
                break;
        }
debug
{
        tl.id = 0;
        assert(tl != &tok);
}
        /* Prepend to list of available tokens  */
        tl.TKnext = token_freelist;
        token_freelist = tl;

        tl = tn;
    }
}

/*********************************
 * Hydrate a token list.
 */

static if (HYDRATE)
{
void token_hydrate(token_t **pt)
{
    token_t *tl;

    while (isdehydrated(*pt))
    {
        tl = cast(token_t *) ph_hydrate(cast(void**)pt);
        token_debug(tl);
        //type_hydrate(&tl.TKtype);
        switch (tl.TKval)
        {   case TKident:
                ph_hydrate(cast(void**)&tl.TKid);
                break;
            case TKstring:
            case TKfilespec:
                ph_hydrate(cast(void**)&tl.TKstr);
                break;
            case TKsymbol:
                symbol_hydrate(&tl.TKsym);
                break;
            default:
                break;
        }
static if (TX86)
{
        filename_translate(&tl.TKsrcpos);
        srcpos_hydrate(&tl.TKsrcpos);
}
        pt = &tl.TKnext;
    }
}
}

/*********************************
 * Dehydrate a token list.
 */

static if (DEHYDRATE)
{
void token_dehydrate(token_t **pt)
{
    token_t *tl;

    while ((tl = *pt) != null && !isdehydrated(tl))
    {
        token_debug(tl);
        ph_dehydrate(pt);
        //type_dehydrate(&tl.TKtype);
        switch (tl.TKval)
        {   case TKident:
                ph_dehydrate(&tl.TKid);
                break;
            case TKstring:
            case TKfilespec:
                ph_dehydrate(&tl.TKstr);
                break;
            case TKsymbol:
                symbol_dehydrate(&tl.TKsym);
                break;
            default:
                break;
        }
static if (TX86)
{
        srcpos_dehydrate(&tl.TKsrcpos);
}
        pt = &tl.TKnext;
    }
}
}

}


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

version (SPP)
{
}
else
{

token_t *token_funcbody(int bFlag)
{   token_t *start = null;
    token_t **ptail = &start;
    int braces = 0;             /* nesting level of {} we're in */

    int tryblock = 0;

    //printf("token_funcbody(bFlag = %d), tryblock = %d\n", bFlag, tryblock);
    while (1)
    {   token_t *t;

        t = token_copy();
        //dbg_printf("Sfilnum = %d, Slinnum = %d\n",t.TKsrcpos.Sfilnum,t.TKsrcpos.Slinnum);
        *ptail = t;                     /* append to token list         */
        token_debug(t);
        ptail = &(t.TKnext);           /* point at new tail            */

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
                assert(0);

            default:
                break;
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

static if (0)
{

token_t *token_defarg()
{   token_t *start = null;
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
                assert(0);
            default:
                break;
        }

        // Append current token to list
        t = token_copy();
        //dbg_printf("Sfilnum = %d, Slinnum = %d\n",t.TKsrcpos.Sfilnum,t.TKsrcpos.Slinnum);
        *ptail = t;                     /* append to token list         */
        token_debug(t);
        ptail = &(t.TKnext);           /* point at new tail            */
    }

Lret:
    return start;
}

}

/*********************************
 * Set scanner to read from list of tokens rather than source.
 */

void token_setlist(token_t *t)
{
    if (t)
    {
        if (toklist)            /* if already reading from list         */
            list_prepend(&toksrclist,toklist);
        toklist = t;
        tok.TKsrcpos = t.TKsrcpos;
    }
}

/*******************************
 * Dump most recent token list.
 */

void token_poplist()
{
    if (toksrclist)
        toklist = cast(token_t *) list_pop(&toksrclist);
    else
        toklist = null;
}

/*********************************
 * "Unget" current token.
 */

void token_unget()
{   token_t *t;

    t = token_copy();
    t.TKflags |= TKFfree;
    token_setlist(t);
}

/***********************************
 * Mark token list as freeable.
 */

void token_markfree(token_t *t)
{
    for (; t; t = t.TKnext)
        t.TKflags |= TKFfree;
}

}

/***********************************
 * Set current token to be an identifier.
 */

void token_setident(char *id)
{
    tok.TKval = TKident;
    tok.TKid = &tok_ident[0];
debug
{
    if (strlen(id) >= tok_ident.length)
    {
        printf("id = '%s', strlen = %d\n", id, strlen(id));
    }
}
    assert(strlen(id) < tok_ident.length);
    strcpy(tok_ident.ptr,id);
}

/***********************************
 * Set current token to be a symbol.
 */

/+
void token_t::setSymbol(symbol *s)
{
    TKval = TKsymbol;
    TKsym = s;
}
+/

/****************************
 * If an ';' is the next character, warn about possible extraneous ;
 */

void token_semi()
{
    version (SPP)
    {
        {
            if (xc == ';')
                warerr(WM.WM_extra_semi);                  // possible extraneous ;
        }
    }
    else
    {
        if (!toklist)
        {
            if (xc == ';')
                warerr(WM.WM_extra_semi);                  // possible extraneous ;
        }
    }
}

/*******************************
 * Get current line number.
 */

version (SPP)
{
}
else
{

Srcpos token_linnum()
{
    return toklist
        ?
          tok.TKsrcpos
        : getlinnum();
}

}

version (SPP)
{
}
else
{

/***************************************
 * Peek at next token ahead without disturbing current token.
 */

enum_TK token_peek()
{
    enum_TK tk;
    token_t *t;

    t = token_copy();
    t.TKflags |= TKFfree;
    tk = stoken();
    token_unget();
    token_setlist(t);
    stoken();
    return tk;
}

}


static if (TX86)
{

version (SPP)
{
}
else
{

struct Keyword
{       const(char)* id;
        ubyte val;
}

static if (TARGET_LINUX || TARGET_OSX || TARGET_FREEBSD || TARGET_OPENBSD || TARGET_SOLARIS)
{
    enum OPTS_NON_TRADITIONAL = 3;  // ANSI + const,signed,volatile
    enum OPTS_NON_ANSI = 1;         // asm
    //enum OPTS_NON_ANSI = 3;       // asm,typeof,restrict

__gshared Keyword[36] kwtab1 =
[
    { "auto",         TKauto },
    { "break",        TKbreak },
    { "case",         TKcase },
    { "char",         TKchar },
    { "continue",     TKcontinue },
    { "default",      TKdefault },
    { "do",           TKdo },
    { "double",       TKdouble },
    { "else",         TKelse },
    { "enum",         TKenum },
    { "extern",       TKextern },
    { "float",        TKfloat },
    { "for",          TKfor },
    { "goto",         TKgoto },
    { "if",           TKif },
    { "int",          TKint },
    { "long",         TKlong },
    { "register",     TKregister },
    { "return",       TKreturn },
    { "short",        TKshort },
    { "sizeof",       TKsizeof },
    { "static",       TKstatic },
    { "struct",       TKstruct },
    { "switch",       TKswitch },
    { "typedef",      TKtypedef },
    { "union",        TKunion },
    { "unsigned",     TKunsigned },
    { "void",         TKvoid },
    { "while",        TKwhile },

        // For ANSI C99
    { "_Complex",     TK_Complex },
    { "_Imaginary",   TK_Imaginary },
        // BUG: do restrict and inline too

    { "const",        TKconst },        // last three are non-traditional C
    { "signed",       TKsigned },       // place at end for linux option to
    { "volatile",     TKvolatile },     // exclude them

    //{ "typeof",       TK_typeof },
    { "restrict",     TKrestrict },
    { "asm",          TK_asm },
];

}
else
{
__gshared Keyword[52] kwtab1 =
[
    { "auto",         TKauto },
    { "break",        TKbreak },
    { "case",         TKcase },
    { "char",         TKchar },
    { "continue",     TKcontinue },
    { "default",      TKdefault },
    { "do",           TKdo },
    { "double",       TKdouble },
    { "else",         TKelse },
    { "enum",         TKenum },
    { "extern",       TKextern },
    { "float",        TKfloat },
    { "for",          TKfor },
    { "goto",         TKgoto },
    { "if",           TKif },
    { "int",          TKint },
    { "long",         TKlong },
    { "register",     TKregister },
    { "return",       TKreturn },
    { "short",        TKshort },
    { "sizeof",       TKsizeof },
    { "static",       TKstatic },
    { "struct",       TKstruct },
    { "switch",       TKswitch },
    { "typedef",      TKtypedef },
    { "union",        TKunion },
    { "unsigned",     TKunsigned },
    { "void",         TKvoid },
    { "while",        TKwhile },

        // For ANSI C99
    { "_Complex",     TK_Complex },
    { "_Imaginary",   TK_Imaginary },
        // BUG: do restrict and inline too

    { "const",        TKconst },        // last three are non-traditional C
    { "signed",       TKsigned },       // place at end for linux option to
    { "volatile",     TKvolatile },     // exclude them

        // Be compatible with IBM's OS/2 C compiler.
    { "_Cdecl",       TK_cdecl },
    { "_Far16",       TK_far16 },
    { "_Pascal",      TK_pascal },
    { "_Seg16",       TK_Seg16 },
    { "_System",      TK_System },

    { "__emit__",     TK__emit__ },
    { "__inf",        TK_inf },
    { "__nan",        TK_nan },
    { "__nans",       TK_nans },
    { "__imaginary",  TK_i },
    { "__istype",     TK_istype },
    { "__with",       TK_with },
    { "__restrict",   TKrestrict },

        // New features added with 8.0
    { "__debug",      TK_debug },
    { "__in",         TK_in },
    { "__out",        TK_out },
    { "__body",       TK_body },
    { "__invariant",  TK_invariant },
];
}

__gshared Keyword[37] kwtab_cpp =
[
    { "catch",        TKcatch },
    { "class",        TKclass },
    { "const_cast",   TKconst_cast },
    { "delete",       TKdelete },
    { "dynamic_cast", TKdynamic_cast },
    { "explicit",     TKexplicit },
    { "friend",       TKfriend },
    { "inline",       TKinline },
    { "mutable",      TKmutable },
    { "namespace",    TKnamespace },
    { "new",          TKnew },
    { "operator",     TKoperator },
    { "overload",     TKoverload },
    { "private",      TKprivate },
    { "protected",    TKprotected },
    { "public",       TKpublic },
    { "reinterpret_cast",     TKreinterpret_cast },
    { "static_cast",  TKstatic_cast },
    { "template",     TKtemplate },
    { "this",         TKthis },
    { "throw",        TKthrow },
    { "try",          TKtry },
    { "typeid",       TKtypeid },
    { "typename",     TKtypename },
    { "using",        TKusing },
    { "virtual",      TKvirtual },
    { "__typeinfo",   TK_typeinfo },
    { "__typemask",   TK_typemask },

        // CPP0X
    { "alignof",      TKalignof },
    { "char16_t",     TKchar16_t },
    { "char32_t",     TKchar32_t },
    { "constexpr",    TKconstexpr },
    { "decltype",     TKdecltype },
    { "noexcept",     TKnoexcept },
    { "nullptr",      TKnullptr },
    { "static_assert", TKstatic_assert },
    { "thread_local", TKthread_local },
];

// Non-ANSI compatible keywords
version (Win32)
{
__gshared Keyword[31] kwtab2 =
[
        // None, single, or double _ keywords
    { "__cdecl",      TK_cdecl },
    { "__far",        TK_far },
    { "__huge",       TK_huge },
    { "__near",       TK_near },
    { "__pascal",     TK_pascal },

        // Single or double _ keywords
    { "__asm",        TK_asm },
    { "__based",      TK_based },
    { "__cs",         TK_cs },
    { "__declspec",   TK_declspec },
    { "__ddecl",      TK_java },
    { "__except",     TK_except },
    { "__export",     TK_export },
    { "__far16",      TK_far16 },
    { "__fastcall",   TK_fastcall },
    { "__finally",    TK_finally },
    { "__fortran",    TK_fortran },
    { "__handle",     TK_handle },
    { "__jupiter",    TK_java },
    { "__inline",     TKinline },
    { "__int64",      TK_int64 },
    { "__interrupt",  TK_interrupt },
    { "__leave",      TK_leave },
    { "__loadds",     TK_loadds },
    { "__unaligned",  TK_unaligned },
    { "__real80",     TK_real80 },
    { "__saveregs",   TK_saveregs },
    { "__segname",    TK_segname },
    { "__ss",         TK_ss },
    { "__stdcall",    TK_stdcall },
    { "__syscall",    TK_syscall },
    { "__try",        TK_try },
];
}

static if (TARGET_LINUX || TARGET_OSX || TARGET_FREEBSD || TARGET_OPENBSD || TARGET_SOLARIS)
{
// linux is a little different about ANSI
// if -ansi or -traditional selected, the root keyword is removed,
// but the __keyword and __keyword__ are still accepted.
// The GNU header files take advantage of this to create header files
// that work with the -ansi and -traditional options
// Plus there are a number of additional __keywords and __keywords__.

__gshared Keyword[19] kwtab2 =
[
//  { "__alignof",      TK_alignof },
//  { "__alignof__",    TK_alignof },
    { "__asm",          TK_asm },
    { "__asm__",        TK_asm },
    { "__attribute",    TK_attribute },
    { "__attribute__",  TK_attribute },
    { "__builtin_constant_p", TK_bltin_const },
    { "__cdecl",        TK_cdecl },       // Not GNU keyword
//  { "__complex",      TK_complex },
//  { "__complex__",    TK_complex },
    { "__const",        TKconst },
    { "__const__",      TKconst },
    { "__declspec",     TK_declspec },
    { "__extension__",  TK_extension },
//  { "__imag",         TK_imaginary },
//  { "__imag__",       TK_imaginary },
    { "__inline",       TKinline },       // remember to handle CPP keyword
    { "__inline__",     TKinline },       // when inline for "C" implemented
    { "inline",         TKinline },       // linux kernel uses inline for C
//  { "__iterator",     TK_iterator },
//  { "__iterator__",   TK_iterator },
//  { "__label__",      TK_label },
//  { "__real",         TK_real },
//  { "__real__",       TK_real },
    { "__restrict",     TKrestrict },
    { "__restrict__",   TKrestrict },
    { "__signed",       TKsigned },
    { "__signed__",     TKsigned },
//  { "__typeof",       TK_typeof },
//  { "__typeof__",     TK_typeof },
    { "__volatile",     TKvolatile },
    { "__volatile__",   TKvolatile },
];
}

// Alternate tokens from C++98 2.11 Table 4
__gshared Keyword[11] kwtab3 =
[
    { "and",          TKandand },
    { "bitor",        TKor     },
    { "or",           TKoror   },
    { "xor",          TKxor    },
    { "compl",        TKcom    },
    { "bitand",       TKand    },
    { "and_eq",       TKandass },
    { "or_eq",        TKorass  },
    { "xor_eq",       TKxorass },
    { "not",          TKnot    },
    { "not_eq",       TKne     },
];

__gshared Keyword[3] kwtab4 =
[
    { "bool",         TKbool  },
    { "true",         TKtrue  },
    { "false",        TKfalse },
];

private void token_defkwds(Keyword *k,uint dim)
{   uint u;

    for (u = 0; u < dim; u++)
        defkwd(k[u].id,cast(enum_TK)k[u].val);
}

}
}

static if (TX86)
{

/***********************************************
 * Initialize tables for tokenizer.
 */
void token_init()
{
version (SPP)
{
    igncomment = (config.flags3 & CFG3comment) == 0;
}
else
{
    auto kwtab1_length = kwtab1.length;
static if (TARGET_LINUX || TARGET_OSX || TARGET_FREEBSD || TARGET_OPENBSD || TARGET_SOLARIS)
{
    kwtab1_length -=
                (config.ansi_c ? OPTS_NON_ANSI : 0) +
                (OPT_IS_SET(OPTtraditional)? OPTS_NON_TRADITIONAL : 0);
}

    token_defkwds(kwtab1.ptr,kwtab1_length);
    token_defkwds(kwtab2.ptr,kwtab2.length);

    if (config.flags4 & CFG4alternate)
        token_defkwds(kwtab3.ptr,kwtab3.length);

    if (CPP)
    {
        token_defkwds(kwtab_cpp.ptr,kwtab_cpp.length);
        if (config.flags4 & CFG4bool)
            token_defkwds(kwtab4.ptr,kwtab4.length);
        if (config.flags4 & CFG4wchar_t)
            defkwd("wchar_t",TKwchar_t);
    }
    else
    {
        defkwd("_Bool",TKbool);
        //if (config.ansi_c >= 99)
        {   defkwd("inline",TKinline);
            defkwd("restrict",TKrestrict);
        }
    }

version (Win32)
{
    if (!config.ansi_c)
    {   uint u;

        defkwd("asm",TKasm);

        // Single underscore version
        for (u = 0; u < kwtab2.length; u++)
            defkwd(kwtab2[u].id + 1,cast(enum_TK)kwtab2[u].val);

        // No underscore version
        for (u = 0; u < 5; u++)
            defkwd(kwtab2[u].id + 2,cast(enum_TK)kwtab2[u].val);
    }
}
}

    if (CPP)
    {
        immutable string bcs =
                // CPP98 2.2
                "abcdefghijklmnopqrstuvwxyz" ~
                "ABCDEFGHIJKLMNOPQRSTUVWXYZ" ~
                "0123456789" ~
                " _{}[]#()<>%:;.?*+-/^&|~!=,\\\"'" ~
                "\t\v\f\n";

        for (int i = 0; i < bcs.length; i++)
        {
            assert(bcs[i]);
            _chartype[bcs[i] + 1] |= _BCS;
        }
    }

    if (!config.ansi_c)
        _chartype['$' + 1] |= _IDS;             // '$' is a valid identifier char

    _chartype[0xFF + 1] |= _ZFF;
    token_setdbcs(config.asian_char);

    __gshared Outbuffer ob;
    utfbuf = &ob;
    memset(utfbuf, 0, ob.sizeof);
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

version (SPP)
{
}
else
{

enum_TK stokenx()
{
    if (!toklist)
        return rtoken(1);

    {   token_t *t;

        token_debug(toklist);
        t = toklist.TKnext;
        tok = *toklist;
        switch (tok.TKval)
        {   case TKident:
                tok.TKid = strcpy(tok_ident.ptr,toklist.TKid);
                break;
            case TKstring:
            case TKfilespec:
                if (tok_strmax < tok.TKlenstr)
                {   // Can happen if we're getting tokens from a precompiled header
                    tok_string = cast(char *) MEM_PARC_REALLOC(tok_string, tok.TKlenstr + 1);
                }

                tok.TKstr = cast(char *)memcpy(tok_string,toklist.TKstr,tok.TKlenstr);
                break;

            default:
                break;
        }
        if (tok.TKflags & TKFfree)
        {   toklist.TKnext = null;
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

}

/*********************************
 * Set dbcs support.
 */

void token_setdbcs(int db)
{   uint c;
static if (LOCALE)
{
    static char*[4] dblocale =
    [   "C",    // default
        ".932", // Japan
        ".950", // Chinese
        ".949"  // Korean
    ];

    assert(db < dblocale.length);
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
}
    dbcs = cast(char)db;                                  // set global state

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

} // TX86

/************************************************
 * Set locale.
 */

void token_setlocale(const(char)* string)
{   uint c;

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
 *      tok.Vlong =      number (if tok.TKval == TKnum)
 *      tok.TKstr .    character string (if tok.TKval == TKstring)
 *      ident .        identifier string (if tok.TKval == TKident)
 *      xc =            character following the token returned
 * Returns:
 *      tok.TKval
 */

enum_TK rtoken(int flag)
{       int xc1;
        ubyte blflags;
        int insflags;

        //printf("rtoken(%d)\n", flag);
static if (IMPLIED_PRAGMA_ONCE)
{
        if (cstate.CSfilblk)
        {
            //printf("BLtokens %s\n", blklst_filename(cstate.CSfilblk));
            cstate.CSfilblk.BLflags |= BLtokens;
        }
}
static if (PASCAL_STRINGS)
{
        tok.TKflags &= ~TKFpasstr;
}

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
static if (1)
{
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
}
else
{
                EGCHAR();               /* eat white space              */
}
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
                blflags = bl.BLflags;
                if (config.flags2 & CFG2expand)
                {       expbackup();    /* remove first char of id      */
                        expflag++;      /* suppress expanded listing    */
                }
                inident();              /* read in identifier           */
                tok.TKid = tok_ident.ptr;
                if (!(xc == '\'' || xc == '"'))
                    goto Lident;
                if (tok_ident[1] == 0)
                { }
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
version (SPP)
{
}
else
{
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
                                    default:
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
}
                    }
                    else
                    {   egchar();
                        tok.TKval = inchar(insflags);
                    }
                    break;
                }
                goto Lident;

        case '$':
                if (config.ansi_c)
                {   lexerr(EM_badtoken);                // unrecognized token
                    goto loop1;         // try again
                }
                goto case 'A';
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
                blflags = bl.BLflags;
                if (config.flags2 & CFG2expand)
                {       expbackup();    /* remove first char of id      */
                        expflag++;      /* suppress expanded listing    */
                }
                inident();              /* read in identifier           */
                tok.TKid = tok_ident.ptr;
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
                        if (!(blflags & BLexpanded && bl && bl.BLflags & BLexpanded))
                        {   if (m.Mflags & Mdefined && !(flag & 2))
                            {   phstring_t args;
                                args.dim = 0;
static if (TARGET_LINUX || TARGET_OSX || TARGET_FREEBSD || TARGET_OPENBSD || TARGET_SOLARIS)
{
                                // extension over used, declarations and statements
                                if (m.Mval == TK_extension)
                                    goto loop1;
}
                                assert(!(m.Mflags & Minuse));

                                if (!m.Mtext)
                                {   // Predefined macro
                                    ubyte* p = macro_predefined(m);
                                    putback(xc);
                                    if (p)
                                        insblk2(p, BLstr);
                                    if (config.flags2 & CFG2expand)
                                        expflag--;
                                    egchar();
                                    goto loop1;
                                }
                                else if (macprocess(m, &args, null))
                                {   ubyte* p;
                                    ubyte* q;
                                    ubyte xcnext = cast(ubyte)xc;
                                    ubyte xclast;
                                    __gshared ubyte[2] brk = [ PRE_BRK, 0 ];

                                    putback(xc);
                                    p = macro_replacement_text(m, args);
                                    //printf("macro replacement text is '%s'\n", p);
                                    q = cast(ubyte*)macro_rescan(m, p);
                                    //printf("final expansion is '%s'\n", q);
                                    parc_free(p);

                                    /* Compare next character of source with
                                     * last character of macro expansion.
                                     * Insert space if necessary to prevent
                                     * token concatenation.
                                     */
                                    if (!isspace(xcnext))
                                        insblk2(brk.ptr, BLrtext);

                                    insblk2(q, BLstr);
                                    bl.BLflags |= BLexpanded;
                                    if (config.flags2 & CFG2expand)
                                        expflag--;
                                    explist(PRE_BRK);
                                    egchar();
                                    //printf("expflag = %d, xc = '%c'\n", expflag, xc);
                                    goto loop1;
                                }
                            }
                        }
                        if (m.Mflags & Mkeyword &&                // if it is a keyword
                            !(pstate.STflags & PFLpreprocessor)) // pp doesn't recognize kwds
                        {
                            if (config.flags2 & CFG2expand)
                            {   tok.TKval = TKident;
                                listident();
                            }
                            return tok.TKval = cast(enum_TK) m.Mval;   // change token to kwd
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
                        if (cast(char) xc == '.')
                        {       egchar();
                                tok.TKval = TKellipsis;
                        }
                        else
                        {
version (SPP)
{
                            if (config.ansi_c)
                                lexerr(EM_badtoken);    // unrecognized token
}
else
{
                                lexerr(EM_badtoken);    // unrecognized token
}
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
version (SPP)
{
}
else
{
                tok.TKty = cast(ubyte)chartype.Tty;
}
                tok.TKval = TKstring;
                tok.TKstr = tok_string;
static if (PASCAL_STRINGS)
{
                if (tok_passtr)
                    tok.TKflags |= TKFpasstr;
}
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

                    default:
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
                if (cast(char) egchar() == '=') { tok.TKval = TKeqeq; egchar(); }
                break;

        case '*':
                tok.TKval = TKstar;
                if (cast(char) egchar() == '=') { tok.TKval = TKmulass; egchar(); }
                break;

        case '&':
                if (cast(char) egchar() == '=') { egchar(); return tok.TKval = TKandass; }
                if (cast(char) xc == '&') { egchar(); return tok.TKval = TKandand; }
                return tok.TKval = TKand;

        case '+':
                if (cast(char) egchar() == '=') { egchar(); return tok.TKval = TKaddass; }
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
version (SPP)
{
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
}
else
{
                if (egchar() == '/') { cppcomment(); goto loop1; }
                if (xc == '*') { comment(); goto loop1; }
}
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
version (SPP)
{
                    expbackup();
}
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
                blflags = bl.BLflags;
                if (config.flags2 & CFG2expand)
                {
                        expflag++;      // suppress expanded listing
                }
                btextp--;
                xc = '\\';              // back up scanner
                inidentX(tok_ident.ptr);    // read in identifier
                tok.TKid = tok_ident.ptr;
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
version (SPP)
{
                experaseline();
                expflag++;
}
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
version (SPP)
{
                    if (igncomment)
                        expbackup();
}
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
                {       bl.BLtextp--;
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
version (SPP)
{
                    if (config.ansi_c)
                        lexerr(EM_badtoken);    // unrecognized token
}
else
{
                        lexerr(EM_badtoken);    // unrecognized token
}
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
version (SPP)
{
    expflag += igncomment;
}
    //synerr(EM_cpp_comments);        // // comments are not ANSI C
static if (1)
{
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
    else if (bl.BLtyp == BLfile)
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
L1: ;
}
else // old (slower) way
{
    do
    {   EGCHAR();
        if (xc == 0)
            return;
    }
    while (cast(ubyte) xc != LF);
}
version (SPP)
{
    expflag -= igncomment;
    explist(xc);
}
else
{
    if (!(pstate.STflags & (PFLpreprocessor | PFLmasm | PFLbasm)))
        egchar();       // preprocessor treats \n as separate token
}
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

version (SPP)
{
    expflag += igncomment;
    enum common_case = false;
}
else
{
    enum common_case = true;
}
    if (common_case && !(config.flags2 & CFG2expand) && bl && bl.BLtyp == BLfile)
    {
        // Fast comment eater for the common case
        ubyte c;

        c = *btextp;
        while (1)
        {
            switch (c)
            {
                case '/':
                    c = *++btextp;
                    if (c == '*')
                        warerr(WM.WM_nestcomment); // can't nest comments
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
                    if (bl && bl.BLtyp == BLfile &&
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
    {
        egchar();
     L1:
        while (1)
        {
            switch (xc)
            {
              case '*':                 /* could be close of comment    */
                    if (cast(char) egchar() == '/')
                    {
version (SPP)
{
                        expflag -= igncomment;
}
                        egchar();       /* get char past close of comment */
                        return;
                    }
                    break;
              case '/':
                    if (cast(char) egchar() == '*')
                        warerr(WM.WM_nestcomment); // can't nest comments
                    break;
              case 0:                           // end of file found
              case_eof:
                    lexerr(EM_no_comment_term,line);    // EOF before end of comment
                    err_fatal(EM_eof);
                    assert(0);

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

void tok_string_reserve(uint nbytes)
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
            tok_string = cast(char *) MEM_PARC_REALLOC(tok_string, tok_strmax);
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


private int instring(int tc,int flags)
{
  int c;
  int i = 0;                            // can't be unsigned!!!
  char ispascal = 0;

    enum { RAWinit, RAWdchar, RAWstring, RAWend }
    int rawstate = RAWinit;
    char[16 + 1] dchr = void;
    int dchri;

    while (1)
    {
        tok_string_reserve(i + 4 + 4 + 16);
        if (flags & INSraw)
        {
          L2:
            switch (rawstate)
            {
                case RAWinit:
                    dchri = 0;
                    rawstate = RAWdchar;
                    goto L2;

                case RAWdchar:
                    if (xc == '(')
                    {   dchr[dchri] = 0;
                        dchri++;
                        rawstate = RAWstring;
                        egchar();
                        continue;
                    }
                    if (dchri >= dchr.sizeof - 1)
                    {   lexerr(EM_string2big, dchr.sizeof - 1);
                        dchri = 0;
                        egchar();
                        continue;
                    }
                    if (xc == ' ' || xc == '(' || xc == ')' ||
                        xc == '\\' || xc == '\t' || xc == '\v' ||
                        xc == '\f' || xc == '\n')
                        lexerr(EM_invalid_dchar, xc);
                    dchr[dchri] = cast(char)xc;
                    dchri++;
                    egchar();
                    continue;

                case RAWstring:
                    if (xc == ')')
                    {
                        dchri = 0;
                        rawstate = RAWend;
                        egchar();
                        continue;
                    }
                    break;

                case RAWend:
                    if (xc == dchr[dchri])
                    {   dchri++;
                        egchar();
                        continue;
                    }
                    if (dchr[dchri] == 0 && xc == tc)
                        goto Ldone;
                    tok_string[i] = ')';
                    memcpy(tok_string + i + 1, dchr.ptr, dchri);
                    i += 1 + dchri;
                    rawstate = RAWstring;
                    continue;

                default:
                    break;  // assert(0) ?
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
        if (ismulti(cast(char)c) && !(flags & (INSchar | INSwchar | INSdchar)))
        {
static if (LOCALE)
{
            char[2] mb = void;

            mb[0] = c;
            mb[1] = xc;
            if (flags & INSwchar_t &&           // if convert to unicode
                !locale_broken &&
                mbtowc(cast(wchar_t *)&tok_string[i],mb,MB_CUR_MAX) > 0)
            {
                //printf("wc = x%x, mb = x%x\n",*(wchar_t *)&tok_string[i],*(unsigned short *)mb);
            }
            else
            {   tok_string[i    ] = cast(char)c;
                tok_string[i + 1] = cast(char)xc;
            }
}
else
{
            {   tok_string[i    ] = cast(char)c;
                tok_string[i + 1] = cast(char)xc;
            }
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
                    if (!config.ansi_c && i == 0 && !(flags & INSnoescape))
                    {   c = 0;
                        ispascal = 1;
                        egchar();               // skip the 'p'
                        break;
                    }
                    goto default;

                default:
                    if (!(flags & INSnoescape))
                        c = escape();           // escape sequence
                    break;
            }
        }
        if (flags & INSwchar_t)                 // convert to UNICODE
        {
static if (LOCALE)
{
            wchar_t wc = c;
            if (locale_broken ||
                mbtowc(cast(wchar_t *)&tok_string[i],cast(char *)&wc,MB_CUR_MAX) <= 0)
                // Convert to unicode by 0 extension
                *cast(wchar_t *)(&tok_string[i]) = c;
}
else
{
                // Convert to unicode by 0 extension
                *cast(wchar_t *)(&tok_string[i]) = cast(wchar)c;
}
            i += 2;
        }
        else
        {
            tok_string[i++] = cast(char)c;        // store char in tok_string
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
                tok_string[0] = cast(char)(i - 1);
            }
static if (PASCAL_STRINGS)
{
            tok_passtr = ispascal;
}
            i++;
            break;

        case INSwchar_t:
            *cast(wchar_t *)(tok_string + i) = 0;
            if (ispascal)
                // Store length in first position
                *cast(wchar_t *)tok_string = cast(wchar)((i - 2) >> 1);
            i += 2;
            break;

        case INSwchar:
            // Translate string in place from UTF8 to UTF16
            utfbuf.setsize(0);
            stringToUTF16(cast(ubyte* )tok_string, i);
            utfbuf.writeWord(0);                       // terminating 0
            i = utfbuf.size();
            if (ispascal)
                // Store length in first position
                *cast(wchar_t *)tok_string = cast(wchar)((i - (2 + 2)) >> 1);
            tok_string_reserve(i);
            memcpy(tok_string, utfbuf.buf, i);
            break;

        case INSdchar:
            // Translate string in place from UTF8 to UTF32
            utfbuf.setsize(0);
            stringToUTF32(cast(ubyte*)tok_string, i);
            utfbuf.write32(0);                         // terminating 0
            i = utfbuf.size();
            if (ispascal)
                // Store length in first position
                *cast(int *)tok_string = (i - (4 + 4)) >> 1;
            tok_string_reserve(i);
            memcpy(tok_string, utfbuf.buf, i);
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

void stringToUTF16(ubyte* string, uint len)
{
    for (size_t j = 0; j < len; j++)
    {
        dchar_t dc = string[j];
        if (dc >= 0x80)
        {
            const(char)* msg = utf_decodeChar(string, len, &j, &dc);
            if (msg)
            {
                lexerr(EM_bad_utf, msg);
                continue;
            }
            j--;
            if (dc > 0xFFFF)
            {   // Encode surrogate pair
                utfbuf.writeWord((((dc - 0x10000) >> 10) & 0x3FF) + 0xD800);
                dc = ((dc - 0x10000) & 0x3FF) + 0xDC00;
            }
        }
        utfbuf.writeWord(dc);
    }
}

void stringToUTF32(ubyte* string, uint len)
{
    for (size_t j = 0; j < len; j++)
    {
        dchar_t dc = string[j];
        if (dc >= 0x80)
        {
            const(char)* msg = utf_decodeChar(string, len, &j, &dc);
            if (msg)
            {
                lexerr(EM_bad_utf, msg);
                continue;
            }
            j--;
        }
        utfbuf.write32(dc);
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
    char *mstring = cast(char *) MEM_PH_MALLOC(tok.TKlenstr);
    memcpy(mstring, tok.TKstr, tok.TKlenstr);
    targ_size_t len = tok.TKlenstr;

    void MSTRING_REALLOC(int newsize)
    {
        if (newsize > STRMAX)
        {   lexerr(EM_string2big, STRMAX);
            err_nomem();
        }
        mstring = cast(char *) MEM_PH_REALLOC(mstring, newsize);
    }

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

        static uint X(tym_t a, tym_t b) { return (a << 8) | b; }
        switch (X(ty, tok.TKty))
        {
            case X(TYchar, TYwchar_t):
                utfbuf.setsize(0);
                utfbuf.reserve(len * 2);
                for (size_t j = 0; j < len; j++)
                {
                    dchar_t dc = mstring[j];
                    utfbuf.writeWord(dc);
                }
            L1:
                len = utfbuf.size();
                MSTRING_REALLOC(len + tok.TKlenstr);
                memcpy(mstring, utfbuf.buf, len);
                memcpy(mstring + len, tok.TKstr, tok.TKlenstr);
                len += tok.TKlenstr;
                ty = tok.TKty;
                lendec = _tysize[ty];
                break;

            case X(TYchar, TYchar16):
                utfbuf.setsize(0);
                stringToUTF16(cast(ubyte* )mstring, len);
                goto L1;

            case X(TYchar, TYdchar):
                utfbuf.setsize(0);
                stringToUTF32(cast(ubyte* )mstring, len);
                goto L1;

            case X(TYwchar_t, TYchar):
                MSTRING_REALLOC(len + tok.TKlenstr * 2);
                for (size_t j = 0; j < tok.TKlenstr; j++)
                    *cast(ushort *)&mstring[len + j * 2] = tok.TKstr[j];
                len += tok.TKlenstr * 2;
                break;

            case X(TYchar16, TYchar):
                utfbuf.setsize(0);
                stringToUTF16(cast(ubyte* )tok.TKstr, tok.TKlenstr);
            L2:
            {
                size_t utfbuf_len = utfbuf.size();
                MSTRING_REALLOC(len + utfbuf_len);
                memcpy(mstring + len, utfbuf.buf, utfbuf_len);
                len += utfbuf_len;
                break;
            }

            case X(TYdchar, TYchar):
                utfbuf.setsize(0);
                stringToUTF32(cast(ubyte* )tok.TKstr, tok.TKlenstr);
                goto L2;

            default:
                lexerr(EM_mismatched_string);
                break;
        }
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
 *      tok.Vlong        long integer
 * Returns:
 *      TKnum   integer constant
 */

private enum_TK inchar(int flags)
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

version (SPP)
{
}
else
{
  /* If signed chars, this needs to be redone                           */
  if (chartype == tstypes[TYuchar])
  {
        tok.TKty = TYuint;              // default
        if (len > _tysize[TYint])
            tok.TKty = TYulong;         // else unsigned long
  }
  else
  {
        tok.TKty = TYint;
        if (len > _tysize[TYint])
            tok.TKty = TYlong;          // else long
  }
}

    if (len == 1)
    {
        if (config.flags & CFGuchar || config.flags3 & CFG3ju)
            tok.Vlong = cast(ubyte) tok_string[0];
        else
            tok.Vlong = cast(byte) tok_string[0];
version (SPP)
{
}
else
{
        tok.TKty = cast(ubyte)chartype.Tty;
}
    }
    else
    {
        if (flags & INSwchar_t)
        {
            // To be MSVC compatible, only look at first Unicode char
            tok.Vlong = *cast(targ_ushort *)tok_string;
            tok.TKty = (config.flags4 & CFG4wchar_t) ? TYwchar_t : TYushort;
        }
        else if (flags & INSwchar)
        {
            // To be MSVC compatible, only look at first Unicode char
            tok.Vlong = *cast(targ_ushort *)tok_string;
            tok.TKty = TYchar16;
        }
        else if (flags & INSdchar)
        {
            // To be MSVC compatible, only look at first Unicode char
            tok.Vlong = *cast(targ_ulong *)tok_string;
            tok.TKty = TYdchar;
        }
        else
        {
            tok.Vlong = 0;
            char *p = cast(char *) &tok.Vlong; // regard tok.Vlong as a byte array
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

private int escape()
{   int n;
    uint i;

    if (isoctal(cast(char)xc))
    {   n = i = 0;
        do
        {   i = (i << 3) + xc - '0';
            n++;                        /* keep track of how many digits */
            egchar();
        } while (n < 3 && isoctal(cast(char)xc));
        if (i & ~0xFF)
            lexerr(EM_badnumber);       // number is not representable
    }
    else
    {
        i = xc;
        egchar();
        switch (i)
        {
            case 'a':   i = 0x07; break;
            case 'n':   i = 0x0A; break;
            case 't':   i = 0x09; break;
            case 'b':   i = 0x08; break;
            case 'v':   i = 0x0B; break;
            case 'r':   i = 0x0D; break;
            case 'f':   i = 0x0C; break;
            case 'x':
                        if (!ishex(xc))
                        {   if (config.ansi_c)
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

private void checkAllowedUniversal(uint uc)
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
            (uc <= 0x7F && isbcs(cast(char)uc))
            || (uc & 0xFFFF0000) // Win32 only check?
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
            || (uc & 0xFFFF0000)        // Win32 only check?
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

version (DigitalMars)
{
}
else                            /* it's in loadline.c   */
{
void inident()
{
    int err = false;
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
}

/********************************
 * Look for \uxxxx or \UXXXXXXXX
 */

void inidentX(char *p)
{
    int err = false;

    while (1)
    {
        if (isidchar(cast(char)xc))
        {
            if (p < &tok_ident[IDMAX])  /* if room left in tok_ident    */
                *p++ = cast(char)xc;
            else
            {   if (!err)
                    lexerr(EM_ident2big);       // identifier is too long
                err = true;
            }
        }
        else if (xc == '\\')
        {   uint uc;
            uint n;

            egchar();
            if (xc == 'u')
                n = 4;
            else if (xc == 'U')
                n = 8;
            else
                goto Lerr;
            // No identfier chars consume more than 3 UTF-8 bytes
            if ((p + 3) - &tok_ident[0] > IDMAX)
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
                *p++ = cast(ubyte)uc;
            }
            else if (uc <= 0x7FF)
            {
                p[0] = cast(ubyte)((uc >> 6) | 0xC0);
                p[1] = cast(ubyte)((uc & 0x3F) | 0x80);
                p += 2;
            }
            else if (uc <= 0xFFFF)
            {
                p[0] = cast(ubyte)((uc >> 12) | 0xE0);
                p[1] = cast(ubyte)(((uc >> 6) & 0x3F) | 0x80);
                p[2] = cast(ubyte)((uc & 0x3F) | 0x80);
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
    idhash += ((p - &tok_ident[0]) << 8) + (*(p - 1) & 0xFF);
}

/*********************************
 * Compute and return hash value of string.
 * Must use same algorithm as inident().
 */

uint comphash(const(char)* p)
{       int idlen;

        idlen = strlen(p);
        return (((*p << 8) + idlen) << 8) + (p[idlen - 1] & 0xFF);
}

/**************************************
 * Read in a number.
 * If it's an integer, store it in tok.Vlong.
 *      integers can be decimal, octal or hex
 *      Handle the suffixes U, UL, LU, L, etc. Allows more cases than
 *      the Ansii C spec allows.
 * If it's double, store it in tok.Vdouble.
 * Returns:
 *      TKnum
 *      TKdouble,...
 */

private enum_TK innum()
{
    /* We use a state machine to collect numbers        */
    enum { STATE_initial, STATE_0, STATE_decimal, STATE_octal, STATE_octale,
        STATE_hex, STATE_binary, STATE_hex0, STATE_binary0,
        STATE_hexh, STATE_error }
    int state;

    enum
    {
        FLAGS_decimal  = 1,               /* decimal                      */
        FLAGS_unsigned = 2,               /* u or U suffix                */
        FLAGS_long     = 4,               /* l or L suffix                */
        FLAGS_llong    = 8,               // LL or ll suffix
    }
    ubyte flags = FLAGS_decimal;

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
                        goto Lreal;
                    case 'E':
                    case 'e':                   // 0e
                        if (config.ansi_c)
                            goto Lreal;
                        goto case_hex;
                    case 'B':
                    case 'b':                   // 0b
                        if (!config.ansi_c)
                        {   state = STATE_binary0;
                            break;
                        }
                        goto case_hex;

                    case '0': case '1': case '2': case '3':
                    case '4': case '5': case '6': case '7':
                        state = STATE_octal;
                        break;

                    case '8': case '9':
                        if (config.ansi_c)
                            goto Lreal;
                        goto case_hex;

                    case 'A':
                    case 'C': case 'D': case 'F':
                    case 'a': case 'c': case 'd': case 'f':
                    case_hex:
                        if (config.ansi_c)
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
version (SPP)
{
                if (i == 0)
                    if (ishex(xc) || xc == 'H' || xc == 'h')
                        goto hexh;
}
else
{
                    if (ishex(xc) || xc == 'H' || xc == 'h')
                        goto hexh;
}
                    if (xc == '.')
                    {
            Lreal:      /* It's a real number. Rescan as a real         */
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
                        goto Lreal;
                    if (state == STATE_hex0)
                        lexerr(EM_hexdigit,xc); // hex digit expected
                    goto done;
                }
                state = STATE_hex;
                break;

            hexh:
                state = STATE_hexh;
                goto case STATE_hexh;
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
                        goto Lreal;
                    else
                    {
                        // Check for something like 1E3 or 0E24 or 09e0
                        if (memchr(tok_string,'E',i) ||
                            memchr(tok_string,'e',i))
                            goto Lreal;
version (SPP)
{
                        if (config.ansi_c)
                            lexerr(EM_hexdigit,xc);     // hex digit expected
}
else
{
                            lexerr(EM_hexdigit,xc);     // hex digit expected
}
                        goto done;
                    }
                }
                break;

            case STATE_octal:           /* reading octal number         */
            case STATE_octale:          /* reading octal number         */
                if (!isoctal(cast(char)xc))
                {   if ((ishex(xc) || xc == 'H' || xc == 'h') && !config.ansi_c)
                        goto hexh;
                    if (xc == '.')
                        goto Lreal;
                    if (isdigit(xc))
                    {
version (SPP)
{
                        lexerr(EM_octal_digit); // octal digit expected
                        state = STATE_error;
                        break;
}
else
{
                        state = STATE_octale;
}
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
                tok_string = cast(char *) MEM_PARC_REALLOC(tok_string,tok_strmax + 1);
        }
        tok_string[i++] = cast(char)xc;
        egchar();
    }
done:
    tok_string[i] = 0;                  /* end of tok_string            */
version (SPP)
{
}
else
{
    if (state == STATE_octale)
        lexerr(EM_octal_digit);         // octal digit expected
}

    *_errno() = 0;
    if (i == 1 && (state == STATE_decimal || state == STATE_0))
        tok.Vllong = tok_string[0] - '0';
    else
        tok.Vllong = strtoull(tok_string,null,base); /* convert string to integer    */
    if (*_errno() == ERANGE)                /* if overflow                  */
        lexerr(EM_badnumber);           // overflow

        /* Parse trailing 'u', 'U', 'l' or 'L' in any combination       */
        while (true)
        {   ubyte f;

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
                        if (_tysize[TYint] == 2)
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

version (SPP)
{
}
else
{
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

        debug assert(_tysize[TYint] == 2 || _tysize[TYint] == 4);
        tok.TKty = TYint;                       // default
        switch (flags)
        {
            case 0:
                /* Octal or Hexadecimal constant.
                 * First that fits: int, unsigned, long, unsigned long,
                 * long long, unsigned long long
                 */
                if (_tysize[TYint] == 4)
                {
                    if (tok.Vllong & 0x8000000000000000L)
                        tok.TKty = TYullong;
                    else if (tok.Vllong & 0xFFFFFFFF00000000L)
                        tok.TKty = TYllong;
                    else if (tok.Vlong & 0x80000000)
                        tok.TKty = TYuint;
                    else if (preprocessor)
                        tok.TKty = TYlong;
                    else
                        tok.TKty = TYint;
                }
                else
                {
                    if (tok.Vllong & 0xFFFFFFFF00000000L)
                        lexerr(EM_badnumber);           // overflow
                    else if (tok.Vlong & 0x80000000)
                        tok.TKty = TYulong;
                    else if (tok.Vlong & 0xFFFF0000 || preprocessor)
                        tok.TKty = TYlong;
                    else if (tok.Vlong & 0x8000)
                        tok.TKty = TYuint;
                    else
                        tok.TKty = TYint;
                }
                break;
            case FLAGS_decimal:
                /* First that fits: int, long, long long
                 */
                if (_tysize[TYint] == 4)
                {
                    if (tok.Vllong & 0x8000000000000000L)
                        lexerr(EM_badnumber);           // overflow
                    else if (tok.Vllong & 0xFFFFFFFF80000000L)
                        tok.TKty = TYllong;
                    else if (preprocessor)
                        tok.TKty = TYlong;
                    else
                        tok.TKty = TYint;
                }
                else
                {   // _tysize[TYint] == 2
                    if (tok.Vllong & 0xFFFFFFFF80000000L)
                        lexerr(EM_badnumber);           // overflow
                    else if (tok.Vlong & 0xFFFF8000 || preprocessor)
                        tok.TKty = TYlong;
                    else
                        tok.TKty = TYint;
                }
                break;
            case FLAGS_unsigned:
            case FLAGS_decimal | FLAGS_unsigned:
                /* First that fits: unsigned, unsigned long, unsigned long long
                 */
                if (_tysize[TYint] == 4)
                {
                    if (tok.Vllong & 0xFFFFFFFF00000000L)
                        tok.TKty = TYullong;
                    else if (preprocessor)
                        tok.TKty = TYulong;
                    else
                        tok.TKty = TYuint;
                }
                else
                {
                    if (tok.Vllong & 0xFFFFFFFF00000000L)
                        lexerr(EM_badnumber);           // overflow
                    else if (tok.Vlong & 0xFFFF0000 || preprocessor)
                        tok.TKty = TYulong;
                    else
                        tok.TKty = TYuint;
                }
                break;
            case FLAGS_decimal | FLAGS_long:
                /* First that fits: long, long long
                 */
                if (_tysize[TYint] == 4)
                {
                    if (tok.Vllong & 0x8000000000000000L)
                        lexerr(EM_badnumber);           // overflow
                    else if (tok.Vllong & 0xFFFFFFFF80000000L)
                        tok.TKty = TYllong;
                    else
                        tok.TKty = TYlong;
                }
                else
                {
                    if (tok.Vllong & 0xFFFFFFFF80000000L)
                        lexerr(EM_badnumber);           // overflow
                    else
                        tok.TKty = TYlong;
                }
                break;
            case FLAGS_long:
                /* First that fits: long, unsigned long, long long,
                 * unsigned long long
                 */
                if (_tysize[TYint] == 4)
                {
                    if (tok.Vllong & 0x8000000000000000L)
                        tok.TKty = TYullong;
                    else if (tok.Vllong & 0xFFFFFFFF00000000L)
                        tok.TKty = TYllong;
                    else if (tok.Vlong & 0x80000000)
                        tok.TKty = TYulong;
                    else
                        tok.TKty = TYlong;
                }
                else
                {
                    if (tok.Vllong & 0xFFFFFFFF00000000L)
                        lexerr(EM_badnumber);           // overflow
                    else if (tok.Vlong & 0x80000000)
                        tok.TKty = TYulong;
                    else
                        tok.TKty = TYlong;
                }
                break;
            case FLAGS_unsigned | FLAGS_long:
            case FLAGS_decimal | FLAGS_unsigned | FLAGS_long:
                /* First that fits: unsigned long, unsigned long long
                 */
                if (_tysize[TYint] == 4)
                {
                    if (tok.Vllong & 0xFFFFFFFF00000000L)
                        tok.TKty = TYullong;
                    else
                        tok.TKty = TYulong;
                }
                else
                {
                    if (tok.Vllong & 0xFFFFFFFF00000000L)
                        lexerr(EM_badnumber);           // overflow
                    else
                        tok.TKty = TYulong;
                }
                break;
            case FLAGS_long | FLAGS_llong:
                /* First that fits: long long, unsigned long long
                 */
                if (_tysize[TYint] == 4)
                {
                    if (tok.Vllong & 0x8000000000000000L)
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
                if (_tysize[TYint] == 2)
                    lexerr(EM_badnumber);               // overflow
                break;
            case FLAGS_long | FLAGS_unsigned | FLAGS_llong:
            case FLAGS_long | FLAGS_decimal | FLAGS_unsigned | FLAGS_llong:
                tok.TKty = TYullong;
                break;
            default:
                debug printf("%x\n",flags);
                assert(0);
        }

static if (0) // redundant
{
        // Check for overflow
        if (tok.Vllong & 0xFFFFFFFF00000000L &&
            tok.TKty != TYllong && tok.TKty != TYullong)
        {
            warerr(WM.WM_badnumber);
        }
}
}
        return TKnum;
}


static if (TX86)
{

/**************************************
 * Read in characters, converting them to real.
 * Input:
 *      p .    characters already read into double
 *      xc      next char
 * Output:
 *      xc      char past end of double
 *      tok.Vdouble      the double read in
 * Bugs:
 *      Exponent overflow not detected.
 *      Too much requested precision is not detected.
 */

private enum_TK inreal(const(char)* p)
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
            c = cast(char)egchar();
        else
        {
            c = *p++;
            if (!c)
            {   c = cast(char)xc;
                chrstate++;
            }
        }

        while (1)
        {   final switch (dblstate)
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
                    goto case 1;

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
                    goto case 4;

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
                    goto case 6;

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
                tok_string = cast(char *) mem_realloc(tok_string,tok_strmax + 1);
        }
        tok_string[i++] = c;
  }
done:
    tok_string[i] = 0;
    *_errno() = 0;
version (Win32)
{
    char *save = __locale_decpoint;
    __locale_decpoint = cast(char*)".".ptr;
}

    switch (xc)
    {
        case 'F':
        case 'f':
version (__GNUC__)
{
            tok.Vfloat = cast(float)strtod(tok_string, null);
}
else
{
            tok.Vfloat = strtof(tok_string, null);
}
            result = TKreal_f;
            egchar();
            break;
        case 'L':
        case 'l':
            if (LDOUBLE)
            {
                tok.Vldouble = strtold(tok_string, null);
                result = TKreal_ld;
            }
            else
            {   tok.Vdouble = strtod(tok_string, null);
                result = TKreal_da;
            }
            egchar();
            break;
        default:
            tok.Vdouble = strtod(tok_string, null);
            result = TKreal_d;
            break;
    }
version (Win32)
{
    __locale_decpoint = save;
}
    // ANSI C99 says let it slide
    if (*_errno() == ERANGE && !config.ansi_c)
        warerr(WM.WM_badnumber);           // number is not representable
    return result;
}

}


/****************************
 * Read in pragma.
 * Pragmas must start in first column.
 * pragma ::= "#" [identifier]
 * Returns:
 *      true    it's a pragma; tok is set to which one
 *      false   it's just # followed by whitespace
 */

private bool inpragma()
{

    while (1)
    {
        if (isidstart(cast(char)xc))
        {
            inident();                  // read in identifier
            tok._pragma = pragma_search(tok_ident.ptr);
            tok.TKval = TKpragma;
            return true;
        }
        else if (isdigit(xc))
        {
            tok._pragma = pragma_search("__linemarker");
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
version (SPP)
{
                expflag--;
}
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
                goto default;
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

int insertSpace(ubyte xclast, ubyte xcnext)
{
    ubyte ctlast = _chartype[xclast + 1];
    if (ctlast & _TOK)
        return 0;

    ubyte ctnext = _chartype[xcnext + 1];
    if (ctnext & _TOK)
        return 0;

static if (0)
{
    return ctlast & (_ID | _IDS | _MTK) && ctnext & (_ID | _IDS | _MTK);
}
else
{
    return ctlast & (_ID | _IDS) && ctnext & (_ID | _IDS) ||
           ctlast & _MTK && ctnext & _MTK;
}
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

void chktok(enum_TK toknum,uint errnum)
{
    if (tok.TKval != toknum)
        synerr(errnum);
    stoken();                   // scan past token
}


void chktok(enum_TK toknum,uint errnum, const(char)* str)
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
{
    return((c == ' ') || (c == '\t') || (!preprocessor && c == LF));
}


/**********************************
 * Binary string search.
 * Input:
 *      p .    string of characters
 *      tab     array of pointers to strings
 *      n =     number of pointers in the array
 * Returns:
 *      index (0..n-1) into tab[] if we found a string match
 *      else -1
 */

static if (1)
{

int binary(const(char)* p, const(char)*  *table,int high)
{ int low,mid;
  byte cond;
  char cp;

  low = 0;
  high--;
  cp = *p;
  p++;
  while (low <= high)
  {     mid = (low + high) >> 1;
        if ((cond = cast(byte)(table[mid][0] - cp)) == 0)
            cond = cast(byte)strcmp(table[mid] + 1,p);
        if (cond > 0)
            high = mid - 1;
        else if (cond < 0)
            low = mid + 1;
        else
            return mid;                 /* match index                  */
  }
  return -1;
}

}
else
{

int binary(const(char)* p, const(char)*  *table,int high)
{
    alias len = high;        // reuse parameter storage
    asm
    {

// First find the length of the identifier.
        xor     EAX,EAX         ; // Scan for a 0.
        mov     EDI,p           ;
        mov     ECX,EAX         ;
        dec     ECX             ; // Longest possible string.
        repne   scasb           ;
        mov     EDX,high        ; // EDX = high
        not     ECX             ; // length of the id including '/0', stays in ECX
        dec     EDX             ; // high--
        js      short Lnotfound ;
        dec     EAX             ; // EAX = -1, so that eventually EBX = low (0)
        mov     len,ECX

        even                    ;
L4D:    lea     EBX,1[EAX]      ; // low = mid + 1
        cmp     EBX,EDX         ;
        jg      Lnotfound       ;

        even                    ;
L15:    lea     EAX,[EBX + EDX] ; // EAX = low + high

// Do the string compare.

        mov     EDI,table       ;
        sar     EAX,1           ; // mid = (low + high) >> 1
        mov     ESI,p           ;
        mov     EDI,[4*EAX+EDI] ; // Load table[mid]
        mov     ECX,len         ; // length of id
        repe    cmpsb           ;

        je      short L63       ; // return mid if equal
        jns     short L4D       ; // if (cond < 0)
        lea     EDX,-1[EAX]     ; // high = mid - 1
        cmp     EBX,EDX         ;
        jle     L15             ;

Lnotfound:
        mov     EAX,-1          ; // Return -1.

        even                    ;
L63:                            ;
    }
}

}


/**********************************
 * Terminate use of scanner
 */

static if (TERMCODE)
{
void token_term()
{
    token_t *tn;

    for (; token_freelist; token_freelist = tn)
    {   tn = token_freelist.TKnext;
static if (TX86)
{
        mem_ffree(token_freelist);
}
else
{
        MEM_PH_FREE(token_freelist);
}
    }
    MEM_PARC_FREE(tok_string);
    MEM_PARC_FREE(tok_arg);
}
}


debug
{
/*******************************
 * Type token information.
 */

void token_print(token_t* t)
{   int i;

    dbg_printf("t.TKval = %3d ",t.TKval);
    switch (t.TKval)
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
            dbg_printf("Vlong = %ld",t.Vlong);
            break;
        case TKstring:
        case TKfilespec:
            dbg_printf("string = '");
            for (i = 0; i < t.TKlenstr; i++)
                dbg_printf("%.*s", cast(int)t.TKlenstr, t.TKstr);
            break;
        case TKdouble:
        case TKreal_da:
            dbg_printf("double = %g",t.Vdouble); break;
        case TKpragma:
            dbg_printf("pragma = %d",t._pragma); break;
        case TKident:
            dbg_printf("ident = '%s'",t.TKid); break;
        case TKsymbol:
            dbg_printf("Symbol = '%s'",t.TKsym.Sident.ptr); break;
        default:
            dbg_printf(", TKMAX = %d",TKMAX);
    }
    dbg_printf("\n");
}

void token_funcbody_print(token_t *t)
{
    while(t)
    {
        t.print();
        t = t.TKnext;
    }
}

}

