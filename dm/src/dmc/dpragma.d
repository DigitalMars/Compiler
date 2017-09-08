/**
 * Implementation of the
 * $(LINK2 http://www.digitalmars.com/download/freecompiler.html, Digital Mars C/C++ Compiler).
 *
 * Copyright:   Copyright (c) 1983-1998 by Symantec, All Rights Reserved
 *              Copyright (c) 2000-2017 by Digital Mars, All Rights Reserved
 * Authors:     $(LINK2 http://www.digitalmars.com, Walter Bright)
 * License:     Distributed under the Boost Software License, Version 1.0.
 *              http://www.boost.org/LICENSE_1_0.txt
 * Source:      https://github.com/DigitalMars/Compiler/blob/master/dm/src/dmc/dpragma.d
 */

// Pragma and macro processor

module dpragma;

import core.stdc.ctype;
import core.stdc.stdio;
import core.stdc.stdlib;
import core.stdc.string;

import ddmd.backend.cdef;
import ddmd.backend.cc;
import ddmd.backend.el;
import ddmd.backend.global;
import ddmd.backend.obj;
import ddmd.backend.outbuf;
import ddmd.backend.ty;
import ddmd.backend.type;

import tk.filespec;
import tk.dlist;
import tk.mem;

import dtoken;
import msgs2;
import parser;
import phstring;
import precomp;
import scopeh;

extern (C++):

alias dbg_printf = printf;
alias MEM_PH_MALLOC = mem_malloc;
alias MEM_PH_CALLOC = mem_calloc;
alias MEM_PH_FREE = mem_free;
alias MEM_PH_STRDUP = mem_strdup;
alias MEM_PARF_MALLOC = mem_malloc;
alias MEM_PARF_CALLOC = mem_calloc;
alias MEM_PARF_REALLOC = mem_realloc;
alias MEM_PARF_FREE = mem_free;
alias MEM_PARF_STRDUP = mem_strdup;

enum MEM_PARF_FREEFP = &mem_freefp;
alias MEM_PERM_REALLOC = mem_realloc;

alias MEM_PARC_REALLOC = mem_realloc;

enum TX86 = 1;


static if (TX86)
    void macro_textfree(macro_t* m) { mem_ffree(m.Mtext); }
else
{
    debug
        void macro_textfree(macro_t* m) { if (m.Mtext) MEM_PH_FREE(m.Mtext); }
    else
        void macro_textfree(macro_t* m) { }
}

// Other primes: 547,739,1009,2003,3001,4001,5003,6007,7001,8009
version (Windows)
    enum MACROHASHSIZE = 2003;            // size of macro hash table (prime)
else
    enum MACROHASHSIZE = 1009;            // size of macro hash table (prime)

// Convert hash to [0 .. MACROHASHSIZE-1]
uint hashtoidx(uint h) { return cast(uint)(h) % MACROHASHSIZE; }

/+
#undef MACROHASHSIZE
#undef hashtoidx
#define MACROHASHSIZE   2048
#define hashtoidx(h)    (((h) + ((h)>>16)) & 0x7FF)
+/

extern __gshared
{
/*private*/ macro_t **mactabroot;            // root of macro symbol table
/*private*/ macro_t **mac_array;
}

static if (TERMCODE)
{
/*private*/ macro_t *premacdefs;             // threaded list of predefined macros
}

void cppcomment();
/*private*/ macro_t ** macinsert(const(char)* p, uint hashval);
/*private*/ macro_t ** macfindparent(const(char)* p, uint hashval);
/*private*/ void deletemactab();
/*private*/ macro_t * macro_calloc(const(char)* p);
/*private*/ void macro_free(macro_t *m);
/*private*/ void prdefine();
/*private*/ char * macrotext(macro_t *m);
/*private*/ void prundef();
/*private*/ void princlude();
/*private*/ void princlude_next();
/*private*/ void princlude_flag(bool next);
/*private*/ void prmessage();
/*private*/ void prstring(int flag);
/*private*/ void prident();
/*private*/ void prcomment();
/*private*/ void pragma_setstructalign(int flag);
static if (TARGET_LINUX || TARGET_OSX || TARGET_FREEBSD || TARGET_OPENBSD || TARGET_SOLARIS)
{
/*private*/ void prassert();
/*private*/ void prassertid();
/*private*/ void prwarning();
}
/*private*/ void prpragma();
/*private*/ void prerror();
/*private*/ void prexit();
/*private*/ void prif();
/*private*/ void prelif();
/*private*/ void prelseif();
/*private*/ void prelse();
/*private*/ void pragma_elif(int seen);
/*private*/ void prendif();
/*private*/ void prifdef();
/*private*/ void prifndef();
/*private*/ void prline();
/*private*/ void prlinemarker();
/*private*/ void scantoelseend();
/*private*/ void scantodefine();
/*private*/ void eatrol();
/*private*/ void blankrol();
/*private*/ void incifn();
/*private*/ phstring_t gargs(ubyte *);
/*private*/ void macro_dehydrate(macro_t *m);
/*private*/ void macro_hydrate(macro_t *mb);
/*private*/ void macrotable_balance(macro_t **ps);
char *textbuf_reserve(char *pbuf, int n);
phstring_t inarglst(macro_t *m, BlklstSave *blsave);


/**********************************
 * Structure for nesting #ifs.
 */

enum
{
    IF_IF        = 0,      // #if, #ifdef or #ifndef
    IF_FIRSTIF   = 1,      // first #ifndef
    IF_ELIF      = 2,      // seen #elif
    IF_ELSE      = 3,      // seen #else
}

struct IFNEST
{       char IFseen;
}

extern __gshared
{
/*private*/ IFNEST *ifn;

/*private*/ uint ifnidx;     /* index into ifn[]                     */
/*private*/ uint ifnmax;     /* # of entries alloced for ifn[]       */

/*private*/ list_t pack_stack;       // stack of struct packing alignment
/*private*/ list_t dbcs_stack;       // stack of dbcs flag
}

/************************************************
 */

extern __gshared
{
/*private*/ char *abuf;              // allocated part of buf[]
/*private*/ char *buf;               // macro text buffer
/*private*/ int bufmax;              // allocated length of buf[]
}

void textbuf_init()
{
    if (bufmax == 0)
    {   bufmax = 80;

        // allocate text buffer
static if (TX86)
        abuf = cast(char *) parc_malloc(1 + bufmax);
else
        abuf = cast(char *) MEM_PARC_MALLOC(1 + bufmax);

        abuf[0] = 0;                    // so we can index buf[-1]
        buf = abuf + 1;
    }
}

void textbuf_term()
{
static if (TX86)
    parc_free(abuf);                    // macro text buffer
else
    MEM_PARC_FREE(abuf);                // macro text buffer
}

char *textbuf_reserve(char *pbuf, int n)
{
    uint buflen;
    uint newlen;

    buflen = pbuf - buf;
    newlen = buflen + n;
    if (newlen > bufmax)                // if potential buffer overflow
    {   uint newmax;

version (SCPP) // SPP does not use precompiled headers so does not have this limitation
{
        enum TEXTMAX = 0x3FF0;          // must fit in PH buffer
        if (newlen > TEXTMAX)
        {
            err_fatal(EM_max_macro_text,"macro text".ptr,TEXTMAX);  // too long
        }
}

        newmax = bufmax * 2;
        if (newmax < newlen)
            newmax = newlen;
        else
        {
version (SCPP)
{
        if (newmax > TEXTMAX)
            newmax = TEXTMAX;
}
        }

        bufmax = newmax;
static if (TX86)
        abuf = cast(char *) parc_realloc(abuf,1 + bufmax);
else
        abuf = cast(char *) MEM_PARC_REALLOC(abuf,1 + bufmax);

        buf = abuf + 1;
        pbuf = buf + buflen;
    }
    return pbuf;
}

/*************************************************
 * The pragma symbol table.
 * Must be in ascending alphabetical order.
 * First arg is pragma name, second is processing function.
 * Use ENUMPRMAC macro to generate parallel data structures.
 */
static if (TARGET_LINUX || TARGET_OSX || TARGET_FREEBSD || TARGET_OPENBSD || TARGET_SOLARIS)
{
enum {
        PR__linemarker,
        PRassert,
        PRcpu,
        PRdefine,
        PRelif,
        PRelse,
        PRelseif,
        PRendif,
        PRerror,
        PRident,
        PRif,
        PRifdef,
        PRifndef,
        PRinclude,
        PRinclude_next,
        PRline,
        PRlint,
        PRmachine,
        PRpragma,
        PRsystem,
        PRunassert,
        PRundef,
        PRwarning,
        PRMAX           // array dimension
};

// String table indexed by enum PR
/*private*/ const(char)*[PRMAX] pragtab =
[
        "__linemarker",
        "assert",
        "cpu",
        "define",
        "elif",
        "else",
        "elseif",
        "endif",
        "error",
        "ident",
        "if",
        "ifdef",
        "ifndef",
        "include",
        "include_next",
        "line",
        "lint",
        "machine",
        "pragma",
        "system",
        "unassert",
        "undef",
        "warning",
];

// Function table indexed by enum PR
/*private*/ void function()[PRMAX] pragfptab =
[
        &prlinemarker,
        &prassert,
        &prassertid,
        &prdefine,
        &prelif,
        &prelse,
        &prelseif,
        &prendif,
        &prerror,
        &prident,
        &prif,
        &prifdef,
        &prifndef,
        &princlude,
        &princlude_next,
        &prline,
        &prassertid,
        &prassertid,
        &prpragma,
        &prassertid,
        &prassert,
        &prundef,
        &prwarning,
];

}
else
{
enum {
        PR__linemarker,
        PRdefine,
        PRelif,
        PRelse,
        PRelseif,
        PRendif,
        PRerror,
        PRident,
        PRif,
        PRifdef,
        PRifndef,
        PRinclude,
        PRinclude_next,
        PRline,
        PRpragma,
        PRundef,
        PRMAX           // array dimension
};

// String table indexed by enum PR
/*private*/ __gshared const(char)*[PRMAX] pragtab =
[
        "__linemarker",
        "define",
        "elif",
        "else",
        "elseif",
        "endif",
        "error",
        "ident",
        "if",
        "ifdef",
        "ifndef",
        "include",
        "include_next",
        "line",
        "pragma",
        "undef",
];

// Function table indexed by enum PR
/*private*/ __gshared void function()[PRMAX] pragfptab =
[
        &prlinemarker,
        &prdefine,
        &prelif,
        &prelse,
        &prelseif,
        &prendif,
        &prerror,
        &prident,
        &prif,
        &prifdef,
        &prifndef,
        &princlude,
        &princlude_next,
        &prline,
        &prpragma,
        &prundef,
];

}

int pragma_search(const(char)* id)
{
static if (1)
{
    // Assume id[] is big enough to do this
    if ((cast(int *)id)[0] == (('i' << 24) | ('f' << 16) | ('e' << 8) | 'd'))
    {
        (cast(char*)id)[7] = 0;
        if ((cast(int *)id)[1] == (('e' << 8) | 'n'))
            return PRdefine;                // most common case
    }
    return binary(id,pragtab.ptr,PRMAX);        /* search for pragma    */
}
else
{
    if (id[0] == 'd' && memcmp(id + 1,"efine".ptr,6) == 0)
        return PRdefine;                // most common case
    else
        return binary(id,pragtab,PRMAX);        /* search for pragma    */
}
}

/********************************
 * When preprocessing, we suppress the output of any line beginning
 * with '#'. Thus, at the end of the line, we need to turn output
 * back on.
 */

version (SPP)
    void exp_ppon() { expflag--; assert(expflag >= 0); }
else
    void exp_ppon() { }

/********************************
 * Process TKpragma's.
 */

void pragma_process()
{
version (SPP)
{
}
else
{
        if (config.flags2 & (CFG2phauto | CFG2phautoy) &&
            (tok._pragma == PRdefine ||
             tok._pragma == PRundef  ||
             tok._pragma == PRpragma) &&
            bl.BLtyp == BLfile && !bl.BLprev)
        {
            if (pstate.STflags & PFLhxgen)
                ph_autowrite();
        }
}

        if (tok._pragma != -1)
        {
            pstate.STflags |= PFLpreprocessor;  // in preprocessor
            assert(tok._pragma < PRMAX);
            (*pragfptab[tok._pragma])();
            pstate.STflags &= ~PFLpreprocessor; // exiting preprocessor
            return;
        }
        lexerr(EM_preprocess);          // unrecognized pragma
}


/***************************
 * Insert macro in symbol table, if it's not already there.
 * Returns:
 *      pointer to macro table entry
 */

/*private*/ macro_t ** macinsert(const(char)* p,uint hashval)
{   macro_t *m;
    macro_t **mp;

    mp = macfindparent(p,hashval);

    if (!*mp)
    {
        // Insert macro into table
        m = macro_calloc(p);
        *mp = m;                                // link new entry into tree
    }
    return mp;
}

/***********************************
 * Return pointer to macro if id is define'd else NULL
 */

macro_t *macdefined(const(char)* id, uint hash)
{
    //printf("macdefined(%s)\n", id);
    if (!hash)
    {
        size_t len = strlen(id);
        hash = ((id[0] & 0xFF) << 16) | (len << 8) | (id[len - 1] & 0xFF);
    }

    uint hashsave = idhash;
    idhash = hash;
    char *idsave = tok.TKid;
    tok.TKid = cast(char *)id;
    macro_t *m = macfind();
    tok.TKid = idsave;
    idhash = hashsave;
    return (m && m.Mflags & Mdefined) ? m : null;
}

/***************************
 * Search for the macro in the macro symbol table.
 * If found, return pointer to it, else NULL.
 */

macro_t * macfind()
{ macro_t *m;
  int cmp;
  char c;
  int len;

static if (0)
{
    m = mactabroot[hashtoidx(idhash)]; /* root of macro table   */
    if (!m)
        return cast(macro_t *) null;
    asm
    {
        mov     EDI,tok.TKid    ;
        mov     EDX,tok.TKid    ;
        mov     ECX,-1          ;
        mov     AL,0            ;
        repne   scasb           ;
        not     ECX             ;
        mov     AL,[EDX]        ;
        dec     ECX             ;
        inc     EDX             ;
        mov     len,ECX         ;
        mov     EBX,m           ;
        jmp     short L1        ;

L3:             mov     EBX,macro_t.ML[EBX]     ;
L1:             test    EBX,EBX                 ;
                je      L4                      ;
                cmp     AL,macro_t.Mid[EBX]     ;
                js      L3                      ;
                je      L2                      ;
L6:             mov     EBX,macro_t.MR[EBX]     ;
                test    EBX,EBX                 ;
                je      L4                      ;
                cmp     AL,macro_t.Mid[EBX]     ;
                js      L3                      ;
                jne     L6                      ;

L2:             mov     ESI,EDX                 ;
                lea     EDI,macro_t.Mid+1[EBX]  ;
                mov     ECX,len                 ;
                rep     cmpsb                   ;
                js      L3                      ;
                jne     L6                      ;

L4:     mov     EAX,EBX                         ;
    }
}
else
{
  c = tok.TKid[0];
  len = strlen(tok.TKid);
  m = mactabroot[hashtoidx(idhash)]; /* root of macro table     */
  while (m)                                     /* while more tree      */
  {     macro_debug(m);
        if ((cmp = c - m.Mid[0]) == 0)
        {   cmp = memcmp(tok.TKid + 1,m.Mid.ptr + 1,len);  /* compare identifiers  */
            if (cmp == 0)                       /* got it!              */
            {
                //dbg_printf("found macro %p %s flags = %X\n",m, m.Mid, m.Mflags);
                //dbg_printf("found macro %s\n",m.Mid);
                return m;
            }
        }
        m = (cmp < 0) ? m.ML : m.MR;  /* select correct child         */
  }
  return null;
}
}


/***************************
 * Search for the parent of the macro in the macro symbol table.
 */

/*private*/ macro_t ** macfindparent(const(char)* p,uint hashval)
{ macro_t* m;
  macro_t** mp;
  byte cmp;
  char c;
  int len;

  c = *p;
  len = strlen(p);
  mp = &mactabroot[hashtoidx(hashval)];         // root of macro table
  m = *mp;
  while (m)                                     /* while more tree      */
  {     macro_debug(m);
        if ((cmp = cast(byte)(c - m.Mid[0])) == 0)
        {   cmp = cast(byte)memcmp(p + 1,m.Mid.ptr + 1,len); /* compare identifiers  */
            if (cmp == 0)                       /* got it!              */
                return mp;
        }
        mp = (cmp < 0) ? &m.ML : &m.MR;       // select correct child
        m = *mp;
  }
  return mp;
}


/***************************
 * Put tok.TKid back in output file.
 */

void listident()
{
    //printf("listident('%s'), expflag = %d\n", tok.TKid, expflag);
    if (config.flags2 & CFG2expand)
    {   expflag--;                      /* expanding again              */
        assert(tok.TKval == TKident);
if (expflag < 0) assert(0);
        assert(expflag >= 0);
        expstring(tok.TKid);            /* send ident to exp listing    */
        explist(xc);
    }
}

/*************************************
 * 'stringize' a filename.
 */

char *filename_stringize(char *name)
{   char *s;
    char *s2;
version (Windows)
{
    int i;
    char *s3;

    /* The trick for MS-DOS is to double up any \ in the        */
    /* path name, so they don't disappear entirely.             */
    i = 0;
    for (s = name; *s; s++)
            i += (*s == '\\');  /* count up slashes     */
    s2 = cast(char *) parc_malloc(s - name + i + 2 + 1);
    s3 = s2;
    *s3++ = '"';
    for (s = name; *s; s++)
    {   *s3++ = *s;
            if (*s == '\\')
                *s3++ = '\\';
    }
    *s3++ = '"';
    *s3 = 0;
}
else
{
    s = name;
    s2 = cast(char *) MEM_PARC_CALLOC(strlen(s) + 2 + 1);
    strcpy(s2,"\"");
    strcat(s2,s);
    strcat(s2,"\"");
}
    return s2;
}

/**********************************************
 * Get parc_malloc'd replacement text for predefined macro m.
 */

ubyte *macro_predefined(macro_t *m)
{   ubyte *s;
    blklst *b;

    assert(!m.Mtext);

    b = cstate.CSfilblk;
    if (!b)
        return null;
    if (!strcmp(m.Mid.ptr,"__LINE__"))
    {
        s = cast(ubyte *)parc_malloc(uint.sizeof * 3 + 1);
        sprintf(cast(char *)s, "%u", b.BLsrcpos.Slinnum);
    }
    else if (!strcmp(m.Mid.ptr,"__FILE__"))
    {
        s = cast(ubyte *)filename_stringize(blklst_filename(b));
    }
    else if (!strcmp(m.Mid.ptr,"__FUNC__")
            || !strcmp(m.Mid.ptr,"__FUNCTION__")
            || !strcmp(m.Mid.ptr,"__PRETTY_FUNCTION__")
            )
    {
        if (funcsym_p)
        {   char *p;
            size_t len;

            // This is obsolete per C99 6.4.2.2. Should use
            // __func__ instead. Perhaps we should print
            // a warning here.

            if (m.Mid.ptr[3] == 'F')
                p = funcsym_p.Sident.ptr;
            else
                p = prettyident(funcsym_p);
            len = strlen(p);
            s = cast(ubyte *)parc_malloc(1 + len + 2);
            *s = '"';
            memcpy(s + 1,p,len);
            s[1 + len] = '"';
            s[1 + len + 1] = 0;
        }
        else
            s = null;
    }
    else if (!strcmp(m.Mid.ptr,"__COUNTER__"))
    {   __gshared uint counter;
        s = cast(ubyte *)parc_malloc(uint.sizeof * 3 + 1);
        sprintf(cast(char *)s, "%u", counter);
        ++counter;
    }
    else
            assert(0);
    return s;
}

/**************************************
 * Read in an argument and store in tok_arg[].
 * arg_char ::= all chars except the delimiters ) and ,
 * Delimiters protected by ' " or () are not delimiters
 * Input:
 *      xc =    first char of argument
 * Output:
 *      xc =    "," or ")"
 *      tok_arg[] =     the argument
 *      if (ellispsisit) all the remaining args are combined
 * Returns:
 *      pointer to the argument
 */

/*private*/ char * inarg(bool ellipsisit, BlklstSave *blsave)
{   int i;
    int parencnt = 0;                   // paren nesting count
    int tc;                             // terminating char of string
    int notinstr = 1;                   // 0 if we're in a string
    int lastxc = ' ';                   // last char read
    int pastend = 0;                    // if past end of input
    bool israwstring = false;

    RawString rs;

    //printf("+inarg()\n");

    i = 0;
    while (1)
    {
        if (i + 4 > argmax)
        {   argmax += 50;
            if (argmax >= 16000)        // guard against argmax overflow
            {   preerr(EM_macarg);
                err_nomem();            // out of memory
            }
            tok_arg = cast(char *) MEM_PARC_REALLOC(tok_arg,argmax);
        }
        //printf("\txc = '%c', 0x%02x\n", xc, xc);
        if (israwstring && xc != PRE_EOB)
        {
            if (!rs.inString(cast(ubyte)xc))
            {
                israwstring = false;
            }
        }
        else switch (xc)
        {
            case '\t':
            case LF:
            case 0x0B:
            case 0x0C:
            case CR:
            case ' ':                   /* all the isspace characters   */
                if (notinstr)
                {
                    do
                    { }
                    while (isspace(egchar()));
                    tok_arg[i++] = ' '; /* replace with a single space  */
                    continue;
                }
                break;
            case '*':                   /* check for start of comment   */
            case '/':
                if (notinstr && i && tok_arg[i - 1] == '/')
                {
version (SPP)
{
                    if (igncomment)
                    {   expbackup();
                        expbackup();
                    }
}
                    if (xc == '*')
                        comment();
                    else
                        cppcomment();
                    while (isspace(xc))
                        egchar();
                    if (i > 1 && tok_arg[i - 2] == ' ')
                        i -= 1;
                    else
                        // Replace comment with single space
                        tok_arg[i - 1] = ' ';
                    continue;
                }
                break;
            case ')':                   /* could be end of argument     */
                if (i>0 && tok_arg[i-1] == ' ')
                        i--;            // remove superfluous space
                if (parencnt != 0)
                {   parencnt -= notinstr; /* -1 if not in a string      */
                    break;
                }
                goto Lcomma;

            case ',':
                if (ellipsisit)         // , is part of argument for ...
                    break;
            Lcomma:
                if (notinstr && parencnt == 0)
                {
                Lret:
                    tok_arg[i] = 0;     // terminate string
                    debug assert(i + 1 <= argmax);
                    //printf("\tinarg returning [%s]\n",tok_arg);
                    //printf("-inarg: "); macrotext_print(tok_arg); printf("\n");

                    if (pastend)
                    {
                        blsave.BSbl = bl;
                        blsave.BSbtextp = cast(ubyte*)btextp;
                        blsave.BSxc = cast(ubyte)xc;
                        bl = null;
                        btextp = null;
                        xc = 0;
                    }

                    return tok_arg;
                }
                break;
            case '(':
                parencnt += notinstr;   /* +1 if not in a string        */
                break;
            case '\\':
                if (lastxc == '\\')
                {   lastxc = ' ';
                    goto L1;
                }
                break;

            case '"':                   /* if a string delimiter        */
                if (lastxc == 'R' && notinstr)
                {
                    rs.init();
                    israwstring = true;
                    break;
                }
                goto case '\'';
            case '\'':
                if (!notinstr)          /* if already in a string       */
                {   if (xc == tc && lastxc != '\\')
                    notinstr = 1;       /* drop out of string           */
                }
                else
                {   tc = xc;            /* terminating char of string   */
                    notinstr = 0;       /* we're in a string            */
                }
                break;

            case PRE_EOB:               /* if end of file               */
                if (!pastend && blsave)
                {
                    pastend = 1;
                    bl = blsave.BSbl;
                    btextp = blsave.BSbtextp;
                    xc = blsave.BSxc;
                    continue;
                }
                preerr(EM_macarg);
                goto Lret;

            default:
                if (dbcs && ismulti(cast(char)xc))        // if asian 2 byte char
                {   tok_arg[i++] = cast(char)xc;
                    lastxc = xc;
                    xc = egchar();      /* no processing for this char  */
                    goto L1;
                }
                break;
        } /* switch (xc) */
        lastxc = xc;

    L1:
        tok_arg[i++] = cast(char)xc;
        egchar();
        debug assert(i <= argmax);
    }
    assert(0);
}

/**************************************
 * Read in argument list.
 * arg_list ::= "(" [arg {,arg}] ")"
 * Input:
 *      xc =    on the opening '('
 * Output:
 *      xc =    char past closing ')'
 * Returns:
 *      Pointer to argument list. null if no argument list, or if the
 *      list was blank. The args in arglst are stored in order of their
 *      appearance.
 *      Dummy up an argument list if there is an error.
 */

phstring_t inarglst(macro_t *m, BlklstSave *blsave)
{
    phstring_t al;
    int n = 0;
    char err = false;

    assert(!(m.Mflags & Mnoparen));
    int nargs = m.Marglist.length();
    assert(xc == '(');
    egchar();                             // get char past '('
    while (true)
    {
        bool ellipsisit = ((m.Mflags & Mellipsis) && (n == (nargs - 1)));
        char *arg = inarg(ellipsisit, blsave);
        //printf("arg[%d] = '%s'\n", n, arg);
        if (*arg || nargs)
            n++;
        if (nargs)
            al.push(cast(char *) MEM_PH_STRDUP(arg));
        if (xc == ',')
        {   egchar();
            continue;
        }

        // If we're short the last argument, and the last argument is ...
        if ((m.Mflags & Mellipsis) && (n == (nargs-1)))
        {   // __VA_ARGS__ will be ""
            al.push(cast(char *) null);
            n++;
        }

        switch (xc)
        {
            case ')':                   /* end of arg list              */
                break;
            case 0:
                return al;
            default:
                assert(0);              /* invalid end of argument      */
        }
        break;
    }

    if (n != nargs)
    {   if (config.ansi_c)
            preerr(EM_num_args,nargs,m.Mid.ptr,n);         // wrong # of args
        else
            warerr(WM.WM_num_args,nargs,m.Mid.ptr,n);         // wrong # of args
    }
    egchar();
//printf("args[]\n");
//for (int i = 0; i < al.length(); ++i)
//    printf("\t[%d] = '%s'\n", i, al[i] ? al[i] : "null");
    return al;
}

/********************************
 * Process macro we discovered in the source.
 * Input:
 *      m .    the macro we found
 * Output:
 *      *pargs  actual arguments
 * Returns:
 *      true    macro installed
 *      false   don't do this macro
 */

int macprocess(macro_t *m, phstring_t *pargs, BlklstSave *blsave)
{
    enum LOG_MACPROCESS = false;

static if (LOG_MACPROCESS)
    printf("macprocess('%s'), expflag = %d\n", m.Mid.ptr, expflag);

static if (0)
{
    if (bl)
    {
        blklst *b;
        dbg_printf("m = '%s',\tinuse=%x,defined=%x,Mnoparen=%x,Mtext '%s'\n",
            m.Mid.ptr, m.Mflags & Minuse, m.Mflags & Mdefined,
            m.Mflags & Mnoparen, m.Mtext);
        bl.print();
        for (b = bl.BLprev; b && b.BLtyp != BLfile; b = b.BLprev)
        {
            b.print();
        }
    }
    else
        dbg_printf("bl is null\n");
}
debug
{
    //static int xxx; if (++xxx == 5) exit(1);
}

    if (m.Mflags & Mnoparen)
    {
        assert(pargs.empty());
    }
    else
    {   // If ( doesn't follow, then this isn't a macro
        ubyte bSpace = 0;
        for (;;)                        // skip whitespace
        {
            if (isspace(xc) || xc == PRE_SPACE || xc == PRE_BRK)
            {
                bSpace = cast(ubyte)xc;
                egchar();
            }
            else if (xc == '/')
            {   if (egchar() == '*')
                    comment();
                else if (xc == '/')
                    cppcomment();
                else
                {   putback(xc);
                    xc = '/';
                    break;
                }
            }
            else
                break;
        }
        if (xc != '(')
        {
            // Handle case when a () macro
            // is followed by whitespace, it would not appear
            if (bSpace && xc != PRE_EOF)
            {
                putback(xc);
                xc = bSpace;    // restore the whitespace
            }
static if (LOG_MACPROCESS)
            printf("macprocess('%s') returns false\n", m.Mid.ptr);

            return false;
        }
        *pargs = inarglst(m, blsave);
    }

static if (LOG_MACPROCESS)
    printf("macprocess('%s') returns true\n", m.Mid.ptr);

    return true;
}

/************************
 */

macro_t *defkwd(const(char)* name, enum_TK val)
{   macro_t *m;
    macro_t **pm;

    //printf("defkwd('%s',%d)\n",name,val);
    pm = macfindparent(name,comphash(name));
    m = *pm;
    if (!m)
    {   m = defmac(name,null);
        m.Mflags = 0;
    }
    macro_debug(m);
    m.Mval = val;
    m.Mflags |= Mkeyword;      // the macro is a C/C++ keyword

    return m;
}

/************************
 */

void pragma_init()
{
    mactabroot = cast(macro_t **)MEM_PH_CALLOC(MACROHASHSIZE * (macro_t *).sizeof);
    textbuf_init();
}

/********************************
 * Terminate the preprocessor.
 */

void pragma_term()
{
    if (ifnidx)
        preerr(EM_eof_endif);           // #if without #endif
static if (TERMCODE)
{
    list_free(&pack_stack,FPNULL);
    list_free(&dbcs_stack,FPNULL);
    if (!(config.flags2 & (CFG2phgen | CFG2phuse | CFG2phauto | CFG2phautoy)))
    {
        list_free(&pstate.STincalias,FPNULL);
        list_free(&pstate.STsysincalias,FPNULL);
        macro_freelist(premacdefs);
        filename_free();
        //deletemactab();               /* get rid of this too          */
        MEM_PH_FREE(mactabroot);
        textbuf_term();
        mactabroot = null;
        MEM_PARC_FREE(ifn);
    }
}
static if (TX86)
{
    free(mac_array);
}
}

/******************************
 * Allocate a macro.
 */

/*private*/ macro_t * macro_calloc(const(char)* p)
{   size_t len = strlen(p);
    macro_t *m;
    __gshared macro_t mzero;

static if (TX86)
    m = cast(macro_t *) mem_fmalloc(macro_t.sizeof + len);
else
    m = cast(macro_t *) MEM_PH_MALLOC(macro_t.sizeof + len);

    *m = mzero;
    debug m.id = macro_t.IDmacro;
    memcpy(m.Mid.ptr,p,len + 1);           /* copy in identifier           */
    return m;
}

static if (TERMCODE)
{

void macro_freelist(macro_t *m)
{   macro_t *mn;

    for (; m; m = mn)
    {
        //printf("macro_free(%p,'%s')\n",m,m.Mid.ptr);
        macro_debug(m);
        debug m.id = 0;
        mn = m.Mnext;
        assert(!(m.Mflags & Minuse));
        macro_textfree(m);
        m.Marglist.free(MEM_PH_FREEP);
static if (TX86)
        mem_ffree(m);
else
        MEM_PH_FREE(m);
    }
}

}


/**************************
 * Define a predefined macro (no arguments).
 */

macro_t *defmac(const(char)* name,const(char)* text)
{   macro_t *m;
    macro_t **pm;
    char *p;

    //dbg_printf("defmac(%s,%s)\n",name,text);
static if (TX86)
    p = mem_fstrdup(text);
else
{
    p = null;
    if (text)
    {   p = cast(char *) MEM_PH_CALLOC(3 + strlen(text));
        sprintf(p," %s ",text);
    }
}
    pm = macfindparent(name,comphash(name));
    m = *pm;
    if (m)
    {   macro_debug(m);
        assert(m.Marglist.empty());
    }
    else
    {   m = macro_calloc(name);
        *pm = m;
static if (TERMCODE)
{
        // Prepend to list of predefined macros
        m.Mnext = premacdefs;
        premacdefs = m;
}
    }
    macro_textfree(m);
    m.Mtext = p;
    m.Mflags |= Mdefined | Mnoparen;   // the macro is now defined

    return m;
}

/********************************
 * Create a special 'defined' macro that is not a macro,
 * but cannot be defined or undefined.
 */

void definedmac()
{   macro_t *m;

    m = defmac("defined", null);
    m.Mflags &= ~Mdefined;
    m.Mflags |= Mfixeddef;
}

/*********************************
 * Define a macro that can't be undefined.
 */

macro_t *fixeddefmac(const(char)* name,const(char)* text)
{
    //printf("fixeddefmac(%s, \"%s\")\n", name, text);
    macro_t *m = defmac(name,text);
    m.Mflags |= Mfixeddef;
    return m;
}

/***************************
 * Return 1 or 0, depending on if ident is defined
 * or not.
 */

int pragma_defined()
{   macro_t *m;
    elem *e;
    char paren;
    int i;

    i = 0;
    paren = 0;
    if (token() == TKlpar)
    {   paren = 1;
        token();
    }
    if (tok.TKval != TKident)
        synerr(EM_ident_exp);           // identifier expected
    else
    {   if ((m = macfind()) != null && m.Mflags & Mdefined)
        {
            //printf("defined(%s)\n", m.Mid.ptr);
            i = 1;                      /* macro is defined             */
        }
        listident();
        stoken();
    }
    if (paren)
        chktok(TKrpar,EM_rpar);         /* ')' expected                 */
    return i;
}


/*************************
 * Comparison function for list_cmp().
 */

/*private*/ int pragma_strcmp(void *s1,void *s2)
{
    return strcmp(cast(const(char)*) s1,cast(const(char)*) s2);
}

/*************************
 * Define a macro.
 * #define identifier text
 * #define identifier( identifier, ... , identifier) text
 */

/*private*/ void prdefine()
{ macro_t *m;
  macro_t *mold;
  macro_t **pm;
  int n;
  char *text;
  ubyte flags;
  ubyte mflags;
  Sfile *sf;

  assert(srcfiles.idx > 0);
  sf = cstate.CSfilblk ? *(cstate.CSfilblk.BLsrcpos).Sfilptr : srcfiles.pfiles[0];
  if (token() != TKident)
  {     preerr(EM_ident_exp);           // identifier expected
        eatrol();                       /* scan to end of line          */
        return;
  }
    if (config.flags2 & CFG2expand)
        listident();

    mflags = 0;
    flags = 0;
    pm = macfindparent(tok.TKid,idhash);
    mold = *pm;
    if (mold)
    {
        macro_debug(mold);
        mflags = mold.Mflags;
        flags |= mflags & Mkeyword;
        if (mflags & (Mdefined | Mfixeddef))    // if macro is already defined
        {
            if (mflags & (Minuse | Mfixeddef))  // if it's in use
            {
                preerr(EM_undef,mold.Mid.ptr);     // would cause bugs
                eatrol();
                return;
            }
            assert(mold.Mtext);
        }
    }

    m = macro_calloc(tok.TKid);
    *pm = m;

    // Replace mold in place in macro table with m
    if (mold)
    {   m.ML = mold.ML;
        m.MR = mold.MR;
        m.Mval = mold.Mval;
        //mold.ML = mold.MR = null;   // this shouldn't be necessary
    }

    // Get argument list
    phstring_t al;
    if (xc == '(')                      /* then macro has arguments     */
    {
        egchar();                       // next character
        al = gargs(&flags);             // get dummy arg list
        n = al.length();
        if (n > PRE_ARGMAX)
            preerr(EM_max_macro_params,n,PRE_ARGMAX);   // max number of args exceeded
    }
    else
    {   // no dummy arguments
        flags |= Mnoparen;              /* indicate no parentheses      */
    }
    m.Marglist = al;
    m.Mflags |= flags;

    // Get macro text
    text = macrotext(m);                // read in macro text
    assert(text);
    if (mflags & Mdefined &&
        !(mold.Mflags & Mkeyword) &&
        config.ansi_c &&
        (strcmp(text,mold.Mtext) ||
         (flags ^ (mflags & Mnoparen)) ||
         al.cmp(mold.Marglist,&pragma_strcmp)
        )
     )
    {   preerr(EM_multiple_def,mold.Mid.ptr);              // already defined
debug
{
        dbg_printf("was: '%s'\n",mold.Mtext);
        dbg_printf("is : '%s'\n",text);
}
    }

    m.Mtext = text;
    m.Mflags |= Mdefined;              // the macro is now defined

    htod_define(m);
    //printf("define %s '%s'\n", m.Mid.ptr, m.Mtext);

    // Thread definition onto list of #define's for this file
    sfile_debug(sf);
    if (!sf.SFpmacdefs)
        sf.SFpmacdefs = &sf.SFmacdefs;
    *sf.SFpmacdefs = m;
    sf.SFpmacdefs = &m.Mnext;

    pstate.STflags |= PFLmacdef;
}


/********************************
 * Get dummy arg list.
 * Input:
 *      xc =    first char of first arg
 * Output:
 *      xc =    char past closing ')'
 * Returns:
 *      pointer to arglist
 */

phstring_t gargsx(ubyte *pflags);

/*private*/ phstring_t gargs(ubyte *pflags)
{
    token();

    phstring_t al;                          // start out with null list
    if (tok.TKval != TKrpar)
    {
        while (1)
        {
            switch (tok.TKval)
            {
                case TKident:
                {
                    if (config.flags2 & CFG2expand)
                        listident();            // put it in eline[]

                    // See if identifier is already in list
                    if (al.find(tok.TKid) >= 0)
                        preerr(EM_multiple_def,tok.TKid);   // already defined

                    al.push(MEM_PH_STRDUP(tok.TKid));
                    token();                    // get next token
                    if (tok.TKval == TKident && config.flags2 & CFG2expand)
                        listident();            // put it in eline[]

                    if (tok.TKval == TKcomma)   // id, case
                    {
                        token();
                        continue;
                    }
                    if (tok.TKval == TKellipsis)        // id... case
                    {
                        // #define X(y ...)     nonstandard GCC way
                        if (!config.ansi_c)
                        {
                            preerr(EM_arg_ellipsis);
                            goto Lellipsis;     // treat same as X(y, ...)
                        }
                    }
                    break;
                }

                case TKellipsis:
                    // Be able to handle the following:
                    //  #define X(...)
                    //  #define X(y, ...)       C99 way

                    if (config.ansi_c != 89)
                    {
                     Lellipsis:
                        *pflags |= Mellipsis;
                        token();                // skip over ...
                        // Treat the ... as if it were an argument named __VA_ARGS__
                        al.push(MEM_PH_STRDUP("__VA_ARGS__"));
                    }
                    break;

                default:
                    preerr(EM_ident_exp);       // identifier expected
                    break;
            }
            break;
        }
        if (tok.TKval != TKrpar)
            preerr(EM_rpar);            // ')' expected
    }
    return al;                  // pointer to start of arg_list
}


/*****************************
 * Read in macro text.
 * Replace comments with a single space.
 * Put a space at beginning and end of text.
 * This serves to:
 *      o prevent token concatenation
 *      o so argmatch() works on dummy args at the end of the text
 *      o so things like
 *              #define msdos msdos
 *        don't cause an infinite loop
 * Input:
 *      xc =    first char of text, or end_of_line
 *      al =    list of parameters
 * Output:
 *      xc =    char past closing end_of_line
 * Returns:
 *      pointer to copied macro replacement text string
 */

/*private*/ char * macrotext(macro_t *m)
{   char lastxc;
    char *pbuf;
    int hashidx;
    int buflen;
    int instr;                  // if " or ', we are in a string
    int stringize;              // if next parameter is to be ##
    RawString rs;
    bool israwstring;
    phstring_t al = m.Marglist;

    // It turns out that this can only happen when reading from
    // file. We can use this and the knowledge that an LF will be
    // found before the end of the buffer...
static if (TARGET_WINDOS)       // for linux nwc_predefine add a define string
    assert(bl.BLtyp == BLfile);        // make sure our assumption is correct

    static int egchar3()
    {
        xc = *btextp++;
        if (config.flags2 & CFG2expand)
        {
           explist(xc);
           return 1;
        }
        return xc;
    }

  Lrestart:
    // Skip leading whitespace
    if (xc == ' ' || xc == '\t')
    {
        if (!(config.flags2 & CFG2expand))
        {
            while (1)
            {
                switch (*btextp)
                {   case ' ':
                    case '\t':
                        btextp++;
                        continue;

                    default:
                        break;
                }
                break;
            }
            xc = *btextp++;
        }
        else
            while (1)
            {
                switch (xc)
                {   case ' ':
                    case '\t':
                        egchar3();
                        continue;

                    default:
                        break;
                }
                break;
            }
    }

    israwstring = false;
    instr = 0;
    stringize = 0;
    hashidx = -1;
    lastxc = ' ';                       // anything but end_of_line or /
    pbuf = buf;
    while (1)
    {
        int istringize;

        /* 6 == PRE_ARG + PRE_EXP + PRE_EXP + xc + ' ' + 0      */
        pbuf = textbuf_reserve(pbuf, 6);
        istringize = stringize;
        if (stringize)
            stringize--;
        if (israwstring && xc != 0)
        {
            if (xc == PRE_ARG)
                *pbuf++ = cast(char)xc;                   // double up the character

            if (!rs.inString(cast(ubyte)xc))
            {
                israwstring = false;
            }
        }
        else switch (xc)
        {   // Sort most likely cases first
            case ' ':
            case '\t':
                if (!instr)
                {
                    xc = ' ';
                    if (pbuf[-1] == ' ')
                    {   egchar3();
                        continue;
                    }
                }
                break;
            case CR:                    // ignore carriage returns
                egchar3();
                continue;
            case LF:                    // if end of line char
                goto done;
            case '0':
                break;
            case '/':
                if (!instr && pbuf[-1] == '/')  // if C++ style comment
                {
                    cppcomment();
                    pbuf--;             /* back up to start of comment  */
                    goto done;
                }
                break;
            case '*':
                if (!instr && pbuf[-1] == '/')  // if start of comment
                {
                    comment();
                    if (buf + 1 == pbuf)
                    {   // leading /* */ comment, ignore
                        goto Lrestart;
                    }
                    lastxc = pbuf[-1] = ' '; // replace with ' '
                    if (pbuf[-2] == ' ')
                        pbuf--;
                    if (istringize)     // If the loop started with a
                        stringize = 2;  // stringize, re-start it with
                                        // a stringize
                    continue;
                }
                break;

            case '"':                   // string delimiters
                if (lastxc == 'R' && !instr)
                {
                    rs.init();
                    israwstring = true;
                    break;
                }
                goto case '\'';
            case '\'':
                if (instr)
                {   if (xc == instr && lastxc != '\\')
                        instr = 0;
                }
                else
                    instr = xc;
                break;
            case '\\':
                if (lastxc == '\\')
                {
                    *pbuf++ = cast(char)xc;
                    lastxc = ' ';
                    egchar();
                    continue;
                }
                break;

            case ':':
                // Digraphs:
                //    %:   is #
                //    %:%: is ##

                if (instr || !(config.flags3 & CFG3digraphs) || lastxc != '%')
                    break;
                pbuf--;
                egchar();
                if (xc == '%')
                {   if (egchar() != ':')
                    {
                        if (m.Mflags & Mnoparen)
                        {
                            pbuf++;
                            *pbuf++ = ':';
                            *pbuf++ = '%';
                            lastxc = '%';
                            continue;
                        }
                        preerr(EM_hashparam);   // # must be followed by param
                    }
                    goto Lhashhash;
                }
                else
                    goto Lhash;

            case '#':
                if (instr)              /* if inside a string           */
                    break;              /* ignore it                    */
                egchar();
                if (xc == '#')          /* if ##, token concatenation   */
                {
            Lhashhash:
                    // Back up over spaces and tabs
                    while (1)
                    {   if (pbuf > buf)
                        {
                            if (!isspace(pbuf[-1]))
                                break;
                            pbuf--;
                        }
                        else
                        {   preerr(EM_hashhash_end); // ## can't appear at beg/end
                            break;
                        }
                    }

                    // Skip following spaces and tabs
                    while (1)
                    {   lastxc = '#';
                        switch (egchar())
                        {   case ' ':
                            case '\t':
                            case CR:
                                continue;

                            default:
                                break;
                        }
                        break;
                    }
                    stringize = 2;
                    *pbuf++ = PRE_ARG;
                    *pbuf++ = PRE_CAT;
                    m.Mflags |= Mconcat;
                }
                else                    /* else #, stringize token      */
                {
            Lhash:
                    if (m.Mflags & Mnoparen)
                    {   // C99 6.10.3.2-1 '#' is ignored for an object-like macro
                        *pbuf++ = '#';
                        lastxc = '#';
                        continue;
                    }
                    if (hashidx != -1)
                        preerr(EM_hashparam);   // # must be followed by param
                    lastxc = '#';
                    hashidx = pbuf - buf;
                    *pbuf++ = '#';
                }
                continue;
            case PRE_ARG:
                *pbuf++ = cast(char)xc;                   // double up the character
                break;

            default:
                if (dbcs && ismulti(cast(char)xc))        // if 2-byte Asian char
                {   *pbuf++ = cast(char)xc;
                    lastxc = cast(char)xc;
                    egchar();
                    goto L2;
                }
                if (!al.empty() && isidstart(cast(char)xc) && !instr) // if possible parameter
                {
                    /* BUG: wrongly picks up suffixes at end of integer and
                     * float literals, string literal suffixes, and string literal prefixes
                     */
                    inident();                  // read in identifier

                    // look for ident in parameter list
                    int n = 1 + al.find(tok_ident.ptr);
                    if (n)
                    {
                        // This is parameter n
                        if (hashidx >= 0)   // if 'stringize' param
                        {
                            // #param gets replaced with:
                            // PRE_ARG, PRE_STR, n

                            assert(buf[hashidx] == '#');
                            pbuf = buf + hashidx;
                            *pbuf++ = PRE_ARG;
                            *pbuf++ = PRE_STR;
                            hashidx = -1;
                        }
                        else if (0 && stringize)
                        {
                            // n ## m gets replaced with:
                            // PRE_ARG, PRE_EXP, n, PRE_ARG, PRE_CAT, PRE_ARG, PRE_EXP, m
                            if (    pbuf >= buf + 4 &&
                                    pbuf[-4] == cast(char)PRE_ARG &&
                                    pbuf[-2] == cast(char)PRE_ARG &&
                                    pbuf[-1] == cast(char)PRE_CAT)
                            {   pbuf[-2] = pbuf[-3];
                                pbuf[-3] = PRE_EXP;
                                pbuf[-1] = PRE_ARG;
                                pbuf[ 0] = PRE_CAT;
                                pbuf++;
                            }

                            *pbuf++ = PRE_ARG; /* param num prefix */
                            *pbuf++ = PRE_EXP;
                        }
                        else
                        {
                            // param gets replace with:
                            // PRE_ARG, n
                            *pbuf++ = PRE_ARG; // param num prefix
                        }
                        *pbuf++ = cast(char)n;        // which parameter
                        lastxc = ' ';       // a safe value
                        goto L1;
                    }
                    if (hashidx != -1)
                    {   preerr(EM_hashparam);   // # must be followed by arg
                        hashidx = -1;
                    }

                    // tok_ident is not a parameter, so put it
                    // into the buffer as it is.
                    int len = strlen(tok_ident.ptr);
                    pbuf = textbuf_reserve(pbuf, len + 1);
                    memcpy(pbuf,tok_ident.ptr,len);
                    pbuf += len;
                    lastxc = pbuf[-1];
                    continue;
                }
                break;
        }
        lastxc = cast(char)xc;
    L2:
        *pbuf++ = cast(char)xc;         /* put char in buffer           */
        egchar3();                      /* get next char                */
    L1: ;
  }

done:
    if (stringize)
        preerr(EM_hashhash_end);        // ## can't appear at beg/end
    if (hashidx != -1)
        preerr(EM_hashparam);           // # must be followed by param
    while (pbuf > buf && pbuf[-1] == ' ')
        pbuf--;
    *pbuf = 0;
    //printf("len = %d\n",strlen(buf));
static if (TX86)
    pbuf = mem_fstrdup(buf);
else
    pbuf = cast(char *) MEM_PH_STRDUP(buf);

    exp_ppon();
    egchar();
    //dbg_printf("macrotext() = "); macrotext_print(pbuf); printf("\n");
    return pbuf;
}

/***********************************
 * Pretty-print macro text string.
 */

void macrotext_print(char *p)
{
debug
{
    printf("[");
    for (; *p; p++)
    {
        uint c = *p & 0xFF;

        switch (c)
        {
            case PRE_ARG:       printf("PRE_ARG"); break;
            case PRE_STR:       printf("PRE_STR"); break;
            case PRE_EXP:       printf("PRE_EXP"); break;
            case PRE_CAT:       printf("PRE_CAT"); break;
            case PRE_BRK:       printf("PRE_BRK"); break;
            case PRE_SPACE:     printf("PRE_SPACE"); break;

            default:
                if (isprint(c))
                    printf("'%c'", c);
                else if (c < 10)
                    printf("%d", c);
                else
                    printf("x%02x", c);
                break;
        }
        if (p[1])
            printf(",");
    }
    printf("]");
}
}

/*************************
 * Undefine a macro definition.
 * #undef identifier
 */

/*private*/ void prundef()
{   macro_t *m;
    macro_t *mold;
    macro_t **pm;
    Sfile *sf;

    /*  This works by creating a new definition of the macro, and then
        marking that new definition as 'undefined'. The reason for this
        is to keep things straight in precompiled headers.
     */

    sf = cstate.CSfilblk ? *(cstate.CSfilblk.BLsrcpos).Sfilptr : srcfiles.pfiles[0];
    if (token() != TKident)
    {   preerr(EM_ident_exp);           // identifier expected
        panic(TKeol);                   /* scan to end of line          */
        exp_ppon();
        return;
    }
    listident();

    /*  We always create a new macro, and replace the macro in the table
        with the new one. This solves the problem of #undef's in
        precompiled headers.
     */
    m = macro_calloc(tok.TKid);
    pm = macfindparent(tok.TKid,idhash);
    mold = *pm;
    if (mold)
    {   macro_debug(mold);
        if (mold.Mflags & Mfixeddef)
            preerr(EM_undef,mold.Mid.ptr); // can't #undef it
        m.Mflags |= mold.Mflags & Mkeyword;
        m.Mval = mold.Mval;
        m.ML = mold.ML;
        m.MR = mold.MR;
    }
    *pm = m;

    // Thread definition onto list of #define's for this file
    sfile_debug(sf);
    if (!sf.SFpmacdefs)
        sf.SFpmacdefs = &sf.SFmacdefs;
    *sf.SFpmacdefs = m;
    sf.SFpmacdefs = &m.Mnext;

    pstate.STflags |= PFLmacdef;

    blankrol();                         /* eat rest of line             */
    exp_ppon();
}

/*************************
 * #include_next "filename"
 * #include_next <filename>
 */

/*private*/ void princlude_next() { princlude_flag(true);  }
/*private*/ void princlude()      { princlude_flag(false); }

/*private*/ void princlude_flag(bool next)
{
    //printf("princlude_flag(%d)\n", next);
    file_progress();

    /* If the current file is system, then the #include'd file is also system,
     * whether it is in " " or < >
     */
    const bool incbysys = (cstate.CSfilblk && cstate.CSfilblk.BLflags & BLsystem);

    ininclude++;
    enum_TK strtok = stoken();
    ininclude--;
    if (strtok != TKstring && strtok != TKfilespec)
    {   preerr(EM_filespec);            // filespec expected
        eatrol();
        exp_ppon();
        return;
    }

    blankrol();                         /* rest of line should be blank */
    exp_ppon();
    putback(LF);                        /* put char (EOL) back in input */
                                        /* (protect against no trailing */
                                        /*  EOL in #include file)       */
    experaseline();

    int flag;
    if (next)
        flag = FQnext;
    else
    {
        flag = FQpath;
        if (strtok == TKstring)
            flag |= FQcwd;
        else
            flag |= FQsystem;
        if (incbysys)
            flag |= FQsystem;
    }
    pragma_include(tok.TKstr, flag);
    egchar();
}


/*************************************
 * Given a #include'd filename, either read it in as a precompiled
 * header, or install it as a file to be read.
 * Input:
 *      flag    value for insblk()
 */

void pragma_include(char *filename,int flag)
{
    //dbg_printf("pragma_include(filename = '%s', flag = x%x)\n",filename,flag);

    // Remove leading and trailing whitespace
    while (isspace(*filename))
        filename++;
    for (char *p = filename + strlen(filename);
         p > filename && isspace(p[-1]);
         *--p = 0)
    { }

    // Look for alias
    for (list_t al = (flag & FQsystem) ? pstate.STsysincalias : pstate.STincalias;
         al;
         al = list_next(al)
        )
    {
        char *n = cast(char *)list_ptr(al);
        al = list_next(al);
        assert(n && al);
        if (filename_cmp(filename,n) == 0)      // if file name matches
        {
            filename = cast(char *)list_ptr(al);    // substitute alias
            break;
        }
    }

version (SPP)
{
    int pl;
    if (file_qualify(&filename,flag,pathlist,&pl) == 0)      // if file not found
    {
version (Posix)
{
        const(char)* name;
        if (cstate.CSfilblk)
            name = srcfiles_name(cstate.CSfilblk.BLsrcpos.Sfilnum);
        else
            name = "preprocessed";
        // Display the name and line number to allow emacs to use this information
        fprintf(stderr, "%s(%d) : ", name, token_linnum().Slinnum);
}
        err_fatal(EM_open_input,filename);      // open failure
    }

    // If already read in
    Sfile *sf = filename_search(filename);
    if (sf)
    {   sfile_debug(sf);

        //printf("\t File already read in, %s\n", sf.SFinc_once_id);

        if (config.flags2 & CFG2once ||   // if only #include files once
            // If file is to be only #include'd once, skip it
            sf.SFflags & SFonce ||
            // include guard
            (sf.SFinc_once_id && macdefined(sf.SFinc_once_id, 0)))
        {
            //printf("\tSFonce set\n");
            if (cstate.CSfilblk)
                list_append(&srcpos_sfile(cstate.CSfilblk.BLsrcpos).SFfillist,sf);
            goto ret;
        }
    }

    // Parse #include file as text
    if (pl >= pathsysi)
        flag |= FQsystem;
    insblk(cast(ubyte *) filename,BLfile,cast(list_t) null,flag | FQqual,null);
    cstate.CSfilblk.BLsearchpath = pl;
}
else
{
    if (HEADER_LIST)
    {
static if (TX86)
{
        int pl;
        if (file_qualify(&filename,flag,pathlist,&pl) == 0)      // if file not found
        {
version (Posix)
{
            char *name;
            int line;
            line = token_linnum().Slinnum;
            if (cstate.CSfilblk)
                    name = srcfiles_name(cstate.CSfilblk.BLsrcpos.Sfilnum);
            else
                    name = "preprocessed";
            // Display the name and line number to allow emacs to use this information
            fprintf(stderr, "%s(%d) : ", name, line);
}
            err_fatal(EM_open_input,filename);      // open failure
        }

        // If filename doesn't end in .H or .HPP, and we are doing automatic
        // precompiled headers, then we are done with PH.
        if (config.flags2 & (CFG2phauto | CFG2phautoy))
        {   char *ext;

            ext = filespecdotext(filename);
            if (filespeccmp(ext,".h")
            && filespeccmp(ext,".hxx")
            && filespeccmp(ext,".hpp"))
            {   ph_testautowrite();
                goto text;
            }
        }

        // Determine if file has already been read in
        {
        Sfile *sf;
        sf = filename_search(filename);

        // If already read in
        if (sf)
        {   sfile_debug(sf);
            //dbg_printf("\t File already read in\n");

            if (config.flags2 & CFG2once)   // if only #include files once
                goto Ldep;

            // If file is in an hx file
            if (sf.SFflags & SFhx)
            {   ph_autoread(filename);
                if (fdep)
                    fprintf(fdep, "%s ", filename);
                goto Ldep;
            }

            // If file is to be only #include'd once, skip it
            // (Do this after check above)
            if (sf.SFflags & SFonce ||
                // include guard
                (sf.SFinc_once_id && macdefined(sf.SFinc_once_id, 0))
               )
            {
                //dbg_printf("\tSFonce set\n");
                goto Ldep;
            }

            // If using an automatically precompiled header
            if (config.flags2 & (CFG2phauto | CFG2phautoy) &&
                !(pstate.STflags & (PFLhxwrote | PFLhxdone)))
            {
            Ldep:
                if (cstate.CSfilblk)
                    list_append(&srcpos_sfile(cstate.CSfilblk.BLsrcpos).SFfillist,sf);
                goto ret;
            }
        }
        else // File not already read in
        {
            // If reading precompiled headers and we haven't already read in
            // a precompiled header for this file, try reading file as a ph.
            // (Note: This is intended for nuts who want to #include a file
            // multiple times, but still use ph. The first read of a #include is
            // a ph, subsequent reads are text)
            if (config.flags2 & CFG2phuse)
                if (!ph_read(filename))     // if successfully read in ph
                {
                    if (fdep)
                        fprintf(fdep, "%s ", filename);
                    goto ret;
                }
            // If we read in an HX file, and we are reading in more .h files,
            // then write a new HX file containing the new .h file.
            if (config.flags2 & (CFG2phauto | CFG2phautoy) &&
                !(pstate.STflags & (PFLhxwrote | PFLhxdone)))
                pstate.STflags |= PFLhxgen;
        }
        }
    text:
        // Parse #include file as text
        pstate.STflags |= PFLinclude;               // BUG: -HI doesn't affect PFLinclude
        if (pl >= pathsysi)
            flag |= FQsystem;
        //dbg_printf("\tReading file %s\n",filename);
        insblk(cast(ubyte *) filename,BLfile,cast(list_t) null,flag | FQqual,null);
        cstate.CSfilblk.BLsearchpath = pl;
}
    }

}

ret:
    mem_free(filename);
}

static if (TARGET_LINUX || TARGET_OSX || TARGET_FREEBSD || TARGET_OPENBSD || TARGET_SOLARIS)
{
/*************************
 * #warning string
 * #warning (string)
 */

/*private*/ void prwarning()
{
    char *p;
    targ_size_t len;
    ptoken();
    if (tok.TKval != TKstring)
    {   preerr(EM_string);                      // string expected
        eatrol();
        return;
    }
    p = combinestrings(&len);
version (SPP)
{
}
else
{
    warerr(WM.WM_warning_message,p);
}
    MEM_PH_FREE(p);
    if (tok.TKval != TKeol)
        blankrol();
}
}

/*************************
 * #message string
 * #message (string)
 * #pragma message(string)
 */

/*private*/ void prmessage()
{
    prstring(1);
}

/*************************
 * #message string
 * #message (string)
 * #pragma message(string)
 * #pragma setlocale(string)
 */

/*private*/ void prstring(int flag)
{ char *p;
  targ_size_t len;
  char paren;

  paren = 0;
  if (tok.TKval == TKlpar)              // optionally enclose message in ()
  {     paren++;
        ptoken();
  }
  if (tok.TKval != TKstring)
  {     preerr(EM_string);              // string expected
        eatrol();
        return;
  }
  p = combinestrings(&len);
  switch (flag)
  {     case 1:                         // message
static if (TARGET_WINDOS)
{
            dbg_printf("%s\n",p);
}
            break;
        case 2:                         // setlocale
            token_setlocale(p);
            break;
        default:
            assert(0);
  }
static if (TX86)
{
  mem_free(p);
}
  if (paren)
  {
        if (tok.TKval != TKrpar)
            preerr(EM_rpar);                    // ')' expected
        ptoken();
  }
  if (tok.TKval != TKeol)
        blankrol();
}

/*************************
 * #pragma comment string
 * Allowable uses:
 * #pragma comment(compiler)
 *      - No string, if one is supplied then a warning is generated.
 *      - Applies to OMF only and will add Compiler version to COMENT rec.
 * #pragma comment(exestr,string)
 *      - OMF and COFF
 *      - string is added to .comment section or COMENT record and is
 *        carried over to the executable file.
 * #pragma comment(lib,"filespec")
 *      - OMF only same as #pragma ZTC includelib "filespec"
 * #pragma comment(user,string)
 *      - OMF only
 *      - string is added to a general COMENT record and is ignored
 *        by the linker
 * Coded by David Bustin.
 */

/*private*/ void prcomment()
{
version (SPP)
{
    eatrol();
}
else
{
    __gshared const(char)*[5]  commenttypes =
    [
        "compiler",
        "exestr",
        "lib",
        "linker",
        "user"
    ];

    enum PRC { PRC_compiler,PRC_exestr,PRC_lib,PRC_linker,PRC_user };

    char *p;
    targ_size_t len;
    PRC i;

    p = null;
    i = cast(PRC) binary(tok.TKid,commenttypes.ptr,commenttypes.length);
    ptoken();
    if (tok.TKval == TKcomma)
    {
        ptoken();
        if (tok.TKval != TKstring)
        {   preerr(EM_string);                  // string expected
            goto Lerr;
        }
        p = combinestrings(&len);
    }

    switch (i)
    {
        case PRC.PRC_compiler:
            Obj.compiler();             // embed compiler version
            goto Lret;
        case PRC.PRC_exestr:
            Obj.exestr(p);              // put comment in executable
            goto Lret;
        case PRC.PRC_lib:
            Obj.includelib(tok.TKid);   // include library
            goto Lret;
        case PRC.PRC_linker:
            eatrol();
            goto Lret;                  // not implemented
        case PRC.PRC_user:
            Obj.user(p);                // put comment in object module
            goto Lret;
        default:
            preerr(EM_unknown_pragma);          // unrecognized pragma
            goto Lerr;
    }
Lerr:
Lret:
    mem_free(p);
}
}

/*************************
 * #ident string
 */

/*private*/ void prident()
{ char *p;
  targ_size_t len;

  if (config.ansi_c)
        preerr(EM_unknown_pragma);              // unrecognized pragma
  ptoken();
  if (tok.TKval != TKstring)
  {     preerr(EM_string);                      // string expected
        eatrol();
        exp_ppon();
        return;
  }
  p = combinestrings(&len);
version (SPP)
{
}
else
{
  Obj.exestr(p);
}
  MEM_PH_FREE(p);
  if (tok.TKval != TKeol)
        blankrol();
  exp_ppon();
}

version (Posix)
{
/*private*/ void prassert()
{

  if (config.ansi_c)
        preerr(EM_unknown_pragma);              // unrecognized pragma
  eatrol();
                                // Simply do nothing
}

/*private*/ void prassertid()
{
  char *p;
  targ_int n;
  targ_size_t len;

  if (config.ansi_c)
        preerr(EM_unknown_pragma);              // unrecognized pragma
  //printf("Found assert('%s')\n",tok_ident);
}

}


/***************************
 * Parse pack/dbcs pragma.
 * Input:
 *      flag    0       pack
 *              1       dbcs
 */

/*private*/ void pragma_setstructalign(int flag)
{   list_t *pstack;

    pstack = flag ? &dbcs_stack : &pack_stack;
    switch (tok.TKval)
    {
        case TKident:
            if (memcmp(tok.TKid,"push".ptr,5) == 0)
            {
                list_prependdata(pstack,flag ? dbcs : structalign); // push onto stack
                ptoken();
                if (tok.TKval == TKrpar)        // pack(push)
                    break;
                if (tok.TKval != TKcomma)
                    synerr(EM_punctuation);             // ',' expected
                ptoken();
                goto L2;
            }
            else if (memcmp(tok.TKid,"pop".ptr,4) == 0)
            {
                if (!*pstack)
                    preerr(EM_pop_wo_push);     // more pops than pushes
                else
                {
                    if (flag)
                        dbcs = cast(char)list_data(dbcs_stack);
                    else
                        structalign = list_data(pack_stack);
                    list_pop(pstack);
                }

                // Inconsistent docs on this, so just eat the rest of the syntax
                do
                    ptoken();
                while (tok.TKval != TKrpar && tok.TKval != TKeol);
            }
            else
                goto L1;
            break;

        case TKnum:
        L2:
            if (flag)
            {
                dbcs = cast(char)msc_getnum();
            }
            else
            {
                structalign = cast(int)msc_getnum();
                if (ispow2(structalign) == -1)
                    preerr(EM_align);           // must be power of 2
                structalign--;
            }
            break;

        default:
        L1:
            if (flag)
            {   dbcs = config.asian_char;
                if (cast(ubyte) dbcs > 3)
                    synerr(EM_unknown_pragma);          // unrecognized pragma
            }
            else
                structalign = config.defstructalign;
            break;
    }
    if (flag)
        token_setdbcs(dbcs);
}

/***************************
 * Recognize the pragmas:
 *      #pragma [SC] align [1|2|4]              (Macintosh)
 *      #pragma [SC] parameter reg (reg,...)    (Macintosh)
 *      #pragma [SC] once
 *      #pragma [SC] segment segid              (Macintosh)
 *      #pragma [SC] template(vector<int>)      (Macintosh and Unix)
 *      #pragma [SC] template_access access     (Macintosh and Unix)
 *      #pragma ZTC align [1|2|4]
 *      #pragma ZTC cseg name
 *      #pragma linkage( ... )
 *      #pragma pack(n)                 n is blank, 1, 2 or 4
 *      #pragma init_seg( ... )
 * For powerc compatibility:
 *      #pragma options align=mac68k == #pragma align 2
 *      #pragma options align=reset == #pragma align
 *      #pragma options align=power == #pragma align 4
*/

/*private*/ void prpragma()
{
version (SPP)
{
    exp_ppon();
    expstring("#pragma ".ptr);
    ptoken();           // BUG: shouldn't macro expand this if it is "STDC"
    if (tok.TKval == TKident)
    {
        if (strcmp(tok.TKid,"STDC".ptr) == 0)
        {
            // C99 6.10.6-2 No macro expansion
            eatrol();
            return;
        }
        else if (strcmp(tok.TKid,"once".ptr) == 0)
        {
            if (cstate.CSfilblk)
            {
                // Mark source file as only being #include'd once
                (**(cstate.CSfilblk.BLsrcpos).Sfilptr).SFflags |= SFonce;
            }
            // Remove the #pragma once from the expanded listing
            experaseline();
            return;
        }
        else if (strcmp(tok.TKid,"GCC".ptr) == 0)
        {
            ptoken();
            if (strcmp(tok.TKid, "system_header".ptr) == 0)
            {
                // Remove the #pragma GCC system_header from the expanded listing
                experaseline();
                return;
            }
        }
    }
    while (tok.TKval != TKeol)
        ptoken();
}
else
{
    ptoken();           // BUG: shouldn't macro expand this if it is "STDC"
    if (tok.TKval == TKident || tok.TKval < KWMAX)
    {   char sawztc = 0;

// Parse flags for prxf[]
enum
{
    PRXFparen     = 1,      // take argument(s) enclosed by ()
    PRXFnoptoken  = 2,      // don't call ptoken()
    PRXFident     = 4,      // first argument must be TKident
}

        enum PRX
        {
            PRXstdc,
            PRXalias,
            PRXalign,
            PRXauto_inline,
            PRXCFMexport,
            PRXcode_seg,
            PRXcomment,
            PRXcseg,
            PRXdbcs,
            PRXdosseg,
            PRXfunction,
            PRXhdrstop,
            PRXinclude_alias,
            PRXincludelib,
            PRXinit_seg,
            PRXinline_depth,
            PRXinline_recursion,
            PRXintrinsic,
            PRXlinkage,
            PRXmessage,
            PRXmfc,
            PRXnoreturn,
            PRXonce,
            PRXoptimize,
            PRXoptions,
            PRXpack,
            PRXparameter,
            PRXpasobj,
            PRXsegment,
            PRXsetlocale,
            PRXstartaddress,
            PRXtemplate,
            PRXtemp_access,
            PRXtrace,
            PRXwarning,
            PRXMAX
        };

        __gshared const(char)*[PRX.PRXMAX] table =
        [
            "STDC",
            "alias",
            "align",
            "auto_inline",
            "cfm_export",
            "code_seg",
            "comment",
            "cseg",
            "dbcs",
            "dosseg",
            "function",
            "hdrstop",
            "include_alias",
            "includelib",
            "init_seg",
            "inline_depth",
            "inline_recursion",
            "intrinsic",
            "linkage",
            "message",
            "mfc",
            "noreturn",
            "once",
            "optimize",
            "options",
            "pack",
            "parameter",
            "pascal_object",
            "segment",
            "setlocale",
            "startaddress",
            "template",
            "template_access",
            "trace",
            "warning",
        ];
        __gshared const ubyte[PRX.PRXMAX + 1] prxf =
        [
            PRXFnoptoken,       // extra value at prxf[0]

            PRXFnoptoken,
            PRXFparen,
            0,
            0,
            PRXFident,
            PRXFparen,
            PRXFparen | PRXFident,
            PRXFident,
            PRXFparen,
            0,
            0,
            PRXFnoptoken,
            PRXFparen | PRXFnoptoken,
            PRXFnoptoken,
            PRXFparen | PRXFident,
            0,
            0,
            0,
            PRXFparen | PRXFident,
            0,
            0,
            PRXFparen | PRXFident,
            0,
            0,
            PRXFident,
            PRXFparen,
            PRXFident,
            0,
            0,
            0,
            PRXFparen | PRXFident,
            PRXFnoptoken,
            PRXFident,
            PRXFident,
            0,
        ];

        PRX i;

        if (strcmp(tok.TKid,"ZTC".ptr) == 0 ||
            strcmp(tok.TKid,"SC".ptr) == 0  ||
            strcmp(tok.TKid,"DMC".ptr) == 0)
        {   ptoken();
            sawztc++;
            if (tok.TKval != TKident)
                goto err;
        }
        i = cast(PRX) binary(tok.TKid,table.ptr,table.length);

        if (prxf[cast(int) i + 1] & PRXFparen)
        {
            ptoken();
            if (tok.TKval != TKlpar)
            {   preerr(EM_lpar);                // '(' expected
                goto err;
            }
        }
        if (!(prxf[cast(int) i + 1] & PRXFnoptoken))
            ptoken();
        if (prxf[cast(int) i + 1] & PRXFident)
        {   if (tok.TKval != TKident)
            {   preerr(EM_ident_exp);
                goto err;               // identifier expected
            }
        }

        switch (i)
        {
            case PRX.PRXstdc:
                eatrol();               // ignore
                return;

            case PRX.PRXalias:
                // #pragma alias("string1","string2")
                // #pragma alias(symbol,"string2")
                {   char* str1,str2;
                    targ_size_t len1,len2;
                    char[IDMAX+IDOHD+1] name = void;
                    size_t len;

                    str1 = null;
                    str2 = null;
                    if (tok.TKval == TKident)
                    {   Symbol *s;

                        s = scope_search(tok.TKid,SCTglobal | SCTnspace);
                        if (s)
                        {
                            if (tyfunc(s.Stype.Tty))
                                nwc_mustwrite(s);
                        }
                        else
                        {   synerr(EM_undefined,tok.TKid);
                            goto err;
                        }
                        len = Obj.mangle(s,name.ptr);
                        assert(len < name.length);
                        name[len] = 0;
                        str1 = mem_strdup(name.ptr);
                        stoken();
                    }
                    else if (tok.TKval == TKstring)
                        str1 = combinestrings(&len1);
                    else
                    {   preerr(EM_string);              // string expected
                        goto err;
                    }

                    if (tok.TKval != TKcomma)
                        preerr(EM_punctuation);         // ',' expected

                    stoken();
                    if (tok.TKval != TKstring)
                        preerr(EM_string);              // string expected
                    else
                    {   str2 = combinestrings(&len2);
                        Obj._alias(str1,str2);
                        mem_free(str2);
                    }
                    mem_free(str1);
                }
                break;

            case PRX.PRXalign:              /* align [1|2|4]                */
                pragma_setstructalign(0);
                break;

            case PRX.PRXcomment:
                // Comment pragmas are enclosed in brackets so the
                // following is the correct sequence:
                //      #pragma comment(comment-type[,comment-string])

                prcomment();
                break;

            case PRX.PRXcseg:                               /* segment segid */
                if (level != 0)
                    preerr(EM_cseg_global);     // only at global scope
                output_func();          /* flush pending functions */
version (SPP)
{
}
else
{
                outcsegname(tok.TKid);
}
                ptoken();
                break;

            case PRX.PRXdbcs:
                pragma_setstructalign(1);
                break;

            case PRX.PRXmessage:
                prmessage();
                return;

            case PRX.PRXmfc:
                config.flags4 |= CFG4nowchar_t;
                if (pstate.STflags & PFLmfc)
                    synerr(EM_mfc);
                break;

            case PRX.PRXnoreturn:
                {   Symbol *s;

                    s = scope_search(tok.TKid,SCTglobal | SCTnspace);
                    if (s)
                        s.Sflags |= SFLexit;
                    else
                        synerr(EM_undefined,tok.TKid);
                }
                ptoken();
                break;

            case PRX.PRXonce:                       /* once */
                if (cstate.CSfilblk)
                {
                    // Mark source file as only being #include'd once
                    srcpos_sfile(cstate.CSfilblk.BLsrcpos).SFflags |= SFonce;
                }
                break;

            case PRX.PRXsetlocale:
                prstring(2);
                return;

            case PRX.PRXcode_seg:
                {   char *segname;
                    targ_size_t len;

                    segname = null;
                    if (tok.TKval == TKstring)
                        segname = combinestrings(&len);
                    if (level != 0)
                        preerr(EM_cseg_global); // only at global scope
                    output_func();      /* flush pending functions */
version (SPP)
{
}
else
{
                    outcsegname(segname);
}
                    mem_free(segname);
                }
                break;

version (Windows)
{
            case PRX.PRXdosseg:
                Obj.dosseg();
                break;
}

            case PRX.PRXinclude_alias:
                // #pragma include_alias("long_filename","short_filename")
                // #pragma include_alias(<long_filename>,<short_filename>)
                {   enum_TK tk;
                    char *longname;
                    char *shortname;

                    ininclude++;
                    stoken();
                    ininclude--;
                    if (tok.TKval != TKstring && tok.TKval != TKfilespec)
                    {   preerr(EM_filespec);            // filespec expected
                        goto err;
                    }
                    tk = cast(enum_TK)tok.TKval;
                    longname = mem_strdup(tok.TKstr);

                    ptoken();
                    if (tok.TKval != TKcomma)
                        preerr(EM_punctuation);         // ',' expected

                    ininclude++;
                    stoken();
                    ininclude--;
                    if (tok.TKval != tk)
                    {   preerr(EM_filespec);            // filespec expected
                        goto err;
                    }
                    shortname = mem_strdup(tok.TKstr);
                    if (tk == TKstring)
                    {   list_prepend(&pstate.STincalias,shortname);
                        list_prepend(&pstate.STincalias,longname);
                    }
                    else
                    {   list_prepend(&pstate.STsysincalias,shortname);
                        list_prepend(&pstate.STsysincalias,longname);
                    }
                    ptoken();
                }
                break;

            case PRX.PRXincludelib:
                ininclude++;
                stoken();
                ininclude--;
                if (tok.TKval != TKstring && tok.TKval != TKfilespec)
                {   preerr(EM_filespec);                // filespec expected
                    goto err;
                }
                Obj.includelib(tok.TKstr);
                ptoken();
                break;

            case PRX.PRXinit_seg:
                {   __gshared const(char)*[3] segtable =
                    [   "compiler",
                        "lib",
                        "user",
                    ];
                    int si;

                    si = binary(tok.TKid,segtable.ptr,segtable.length);
                    if (si == -1)
                        goto err;
                    pstate.STinitseg = 3 - si;
                    ptoken();
                    break;
                }

            case PRX.PRXlinkage:
                {   __gshared const(char)*[6]  linktable =
                    [   /* optlink and fastcall not supported   */
                        "_cdecl",
                        "_pascal",
                        "cdecl",
                        "far16",
                        "pascal",
                        "system",
                    ];
                    enum PRL { PRL_cdecl,PRL_pascal,PRLcdecl,PRLfar16,
                                PRLpascal,PRLsystem };
                    PRL prl;
                    uint tym;
                    mangle_t mangle;
                    char *funcident = null;

                    funcident = mem_strdup(tok.TKid);
                    ptoken();
                    if (tok.TKval != TKcomma)
                        preerr(EM_punctuation);         /* ',' expected         */
                    ptoken();
                    if (tok.TKval != TKident)
                        preerr(EM_ident_exp);           /* identifier expected  */
                    prl = cast(PRL) binary(tok.TKid,linktable.ptr,linktable.length);
                    ptoken();
                    switch (prl)
                    {   case PRL.PRLsystem:
                            if (I16)
                            {   tym = mTYfar | mTYpascal;
                                mangle = mTYman_pas;
                            }
                            else
                            {   tym = mTYcdecl;
                                mangle = mTYman_sys;
                            }
                            break;
                        case PRL.PRLfar16:
                            mangle = mTYman_c;
static if (TARGET_WINDOS)
{
                            if (I16)
                                tym = mTYfar;
                            else if (config.exe & EX_flat)
                                tym = 8 /*mTY_far16*/ | mTYfar16;
                            else
                                preerr(EM_far16_model); // only in flat memory model
}
                            if (tok.TKval != TKident)
                                break;
                            prl = cast(PRL) binary(tok.TKid,linktable.ptr,linktable.length);
                            ptoken();
                            switch (prl)
                            {   case PRL.PRLcdecl:
                                case PRL.PRL_cdecl:
                                    tym |= mTYcdecl;
                                    mangle = mTYman_c;
                                    break;
                                case PRL.PRLpascal:
                                case PRL.PRL_pascal:
                                    tym |= mTYpascal;
                                    mangle = mTYman_pas;
                                    break;
                                default:
                                    mem_free(funcident);
                                    goto err;
                            }
                            break;
                        default:
                            mem_free(funcident);
                            goto err;
                    }
                    nwc_setlinkage(funcident,tym,mangle);
                    mem_free(funcident);
                    break;
                }

            case PRX.PRXpack:
                pragma_setstructalign(0);
                break;

            case PRX.PRXstartaddress:
                {   Symbol *s;

                    s = scope_search(tok.TKid,SCTglobal | SCTnspace);
                    if (s)
                    {
                        Obj.startaddress(s);
                        nwc_mustwrite(s);
                    }
                    else
                        synerr(EM_undefined,tok.TKid);
                }
                ptoken();
                break;

            case PRX.PRXhdrstop:
version (SPP)
{
}
else
{
                if (config.flags2 & (CFG2phauto | CFG2phautoy))
                    ph_testautowrite();
}
                eatrol();
                return;

            case PRX.PRXoptimize:
static if (TARGET_LINUX || TARGET_OSX || TARGET_FREEBSD || TARGET_OPENBSD || TARGET_SOLARIS)
{
                if (tok.TKval != TKident)
                    goto err;
                if (!strcmp(tok.TKid, "none".ptr))
                    go_flag("+none");
                else if (!strcmp(tok.TKid, "all".ptr ))
                {
                    printf("Calling go_flag with all\n");
                    go_flag("+all");
                }
                else if (!strcmp(tok.TKid, "space".ptr ))
                    go_flag("+space");
                else
                    goto err;
                break;
}
            case PRX.PRXauto_inline:
            case PRX.PRXfunction:
            case PRX.PRXinline_depth:
            case PRX.PRXinline_recursion:
            case PRX.PRXintrinsic:
            case PRX.PRXwarning:
                eatrol();               // ignore
                return;

            default:
            err:
                if (sawztc)
                    synerr(EM_unknown_pragma);
                else if (!config.ansi_c)
                    warerr(WM.WM_unknown_pragma);  // unrecognized pragma
                goto Ldone;
        }
        if (prxf[cast(int) i + 1] & PRXFparen)
        {
            if (tok.TKval != TKrpar)
                preerr(EM_rpar);                // ')' expected
            ptoken();
        }
        if (tok.TKval == TKeol)
            return;
        goto err;
    }

Ldone:
    /* Ignore rest of line      */
    eatrol();
}
}

/**************************
 * Print diagnostic message and exit.
 */

/*private*/ void prerror()
{
    const(char)* name;
    int line;
    blklst *b;
    char[200] buffer = void;
    char *p = buffer.ptr;

    line = token_linnum().Slinnum;
    if (cstate.CSfilblk)
        name = srcpos_name(cstate.CSfilblk.BLsrcpos);
    else
        name = "preprocessed";

    while (xc && xc != LF)
    {
        if (p < &buffer[buffer.length - 1])
            *p++ = cast(char)xc;
        egchar();
    }
    *p = 0;
    err_message("Error %s %d: %s",name,line,buffer.ptr);
    err_exit();
}

/**************************
 * #exit constant_expression
 */

static if (0)
{
/*private*/ void prexit()
{
  stoken();
  exit(cast(int) msc_getnum());
}
}

/*************************
 * #if constant_expression
 * #elif constant_expression
 */

/*private*/ void prif()
{ targ_int n;

  stoken();
  n = cast(targ_int)msc_getnum();
  exp_ppon();
  if (tok.TKval != TKeol)               // if not end of line
  {     preerr(EM_eol);
        eatrol();
        return;
  }
  else
  {
version (SPP)
{
    if (!isidstart(cast(char)xc))
        explist(xc);
}
  }
  incifn();                             /* increase nesting level       */
  if (n)                                /* if result was true           */
  {
  }
  else                                  /* false conditional            */
  {     expbackup();                    /* dump first char of this line */
        expflag++;                      /* stop listing                 */
        scantoelseend();                /* scan till #else or #end      */
  }
}


/*************************
 * #elif
 * #else
 */

/*private*/ void prelif()   { pragma_elif(IF_ELIF); }
/*private*/ void prelse()   { pragma_elif(IF_ELSE); }

/*private*/ void prelseif()
{
    if (config.ansi_c)
        lexerr(EM_preprocess,tok_ident.ptr);        // unrecognized pragma
    pragma_elif(IF_ELIF);               // synonym for #elif
}

/*private*/ void pragma_elif(int seen)
{
    eatrol();                           /* scanto to next line          */
    exp_ppon();
    if (ifnidx == 0 ||                  /* if #else without a #if       */
        ifn[ifnidx].IFseen == IF_ELSE)  /* if already seen #else        */
        preerr(EM_else);                // #else without a #if
    else
    {
        ifn[ifnidx].IFseen = cast(char)seen;      /* seen it now                  */
        expflag++;                      /* shut off listing             */
        scantoelseend();                /* scan till we find #end       */
    }
}


/*************************
 * #endif
 */

/*private*/ void prendif()
{
  blankrol();                           /* scanto to next line          */
  exp_ppon();
static if (IMPLIED_PRAGMA_ONCE)
{
    if (ifn[ifnidx].IFseen == IF_FIRSTIF)
    {                                   // Found closing #endif for 1st #if
        blklst *bl = cstate.CSfilblk;
        if (bl &&          // candidate for single inclusion
            (bl.BLflags & BLifndef) &&
            bl.ifnidx == ifnidx)
        {
            if (bl.BLflags & BLendif)
                bl.BLflags &= ~(BLifndef | BLendif);
            else
            {
                bl.BLflags |= BLendif;
                //dbg_printf("\tprendif setting BLendif\n");

                // BLtokens gets set if there are any more tokens between #endif and end of file
                bl.BLflags &= ~BLtokens;
            }
        }
    }
}
version (SPP)
{
  explist(LF);
}
  if (ifnidx == 0)
        preerr(EM_endif);                       // #endif without #if
  else
        ifnidx--;
}

/*************************
 * If identifier is defined as a macro.
 * #ifdef identifier
 */

/*private*/ void prifdef()
{ macro_t *m;

  token();
  if (tok.TKval != TKident)
  {     preerr(EM_ident_exp);                   /* identifier expected          */
        exp_ppon();
        return;
  }
  listident();
  blankrol();                           /* finish off line              */
  exp_ppon();
  incifn();
  m = macfind();
  if (m != null &&                      /* if macro is in table and     */
      m.Mflags & Mdefined)             /* it's defined                 */
  {
        //printf("ifdef %s\n", m.Mid.ptr);
  }
  else                                  /* false conditional            */
  {     expflag++;                      /* shut off listing             */
        scantoelseend();                /* scan till #else or #end      */
  }
}


/*************************
 * If identifier is not defined as a macro.
 * #ifndef identifier
 */

/*private*/ void prifndef()
{
    //printf("prifndef()\n");
    ubyte bfl = 0;
    blklst *bl = cstate.CSfilblk;
    if (bl)
        bfl = bl.BLflags;

  if (token() != TKident)
  {     preerr(EM_ident_exp);                   /* identifier expected          */
        exp_ppon();
        return;
  }
  listident();
  blankrol();                           /* finish off line              */
  exp_ppon();
  incifn();
  macro_t *m = macfind();
  if (m == null ||                      /* if macro isn't in table or   */
      !(m.Mflags & Mdefined))          /* it isn't defined             */
  {
static if (IMPLIED_PRAGMA_ONCE)
{
        /* Look for:
         *   #ifndef ident
         * as first thing in the source file
         */
        if (bl && !(bfl & BLtokens) && (bl.BLflags & BLnew))
        {                                       // Found #ifndef at start of file
            ifn[ifnidx].IFseen = IF_FIRSTIF;
            bl.BLflags |= BLifndef;
            bl.ifnidx = ifnidx;
            assert(tok.TKval == TKident);
            bl.BLinc_once_id = mem_strdup(tok.TKid);        // Save the identifier
            //dbg_printf("\tSetting BLifndef for token %s \n", bl.inc_once_id);
        }
}
  }
  else                                  /* false conditional            */
  {     expflag++;
        scantoelseend();                /* scan till #else or #end      */
  }
}


/*************************
 * Set new line number and (optional) file name.
 * #line constant identifier
 * # constant identifier flags...
 */

/*private*/ void prlinex(bool linemarker)
{
    int lLine, savenum = 0;
version (SPP)
{
    // Pass these on through to preprocessed output
    exp_ppon();
    expstring(linemarker ? "# ".ptr : "#line ".ptr);
    if (linemarker)
        explist(xc);
}

  stoken();
  if (tok.TKval != TKnum)
  {
        preerr(EM_linnum);                      // line number expected
        eatrol();
        return;
  }

  if (xc == '_')                                // Maybe __LINE__ (fixes Defect #8792)
        {
        char[10] buf = void;
        savenum = tok.Vlong;
        ptoken();
        if (tok.TKval == TKnum)
                {
                int i;
                sprintf(buf.ptr,"%d", tok.Vlong);
                i = strlen(buf.ptr);
                while (i-.0)
                        savenum *= 10;
                }
        }
  if (!cstate.CSfilblk)
        return;
  if (!iswhite(xc) && xc != CR && xc != LF && xc != PRE_SPACE && xc != PRE_BRK)
  {
        preerr(EM_badtoken);                    // Unrecognised token
        return;
  }
  cstate.CSfilblk.BLsrcpos.Slinnum = savenum + tok.Vlong - 1;
                                                  // line number for next line
  ininclude++;
  stoken();
  ininclude--;
  if (tok.TKval == TKstring)
  {     targ_size_t len;
        char *name;

        if (tok.TKty == TYushort)
            preerr(EM_string);                  // wide char string not allowed
        name = combinestrings(&len);
        if (cstate.CSfilblk)
            cstate.CSfilblk.BLsrcpos.Sfilptr = filename_indirect(filename_add(name));
        MEM_PH_FREE(name);
  }
  else
  {
static if (TARGET_LINUX || TARGET_OSX || TARGET_FREEBSD || TARGET_OPENBSD || TARGET_SOLARIS)
{
// PATN: lLine used before set
        cstate.CSfilblk.BLsrcpos.Slinnum = lLine;
}
  }

    if (linemarker)                     // if # constant identifier flags...
    {   while (tok.TKval == TKnum)
            stoken();                   // skip over flags
    }

  if (tok.TKval != TKeol)
  {     lexerr(EM_eol);                 // end of line expected
        blankrol();
  }
}


/*private*/ void prline()       { return prlinex(false); }
/*private*/ void prlinemarker() { return prlinex(true);  }

/*************************
 * Skip over tokens, looking for #elif, #else or
 * a #end.
 * Note that conditionals can be nested.
 */

/*private*/ void scantoelseend()
{   int ifnidxstart = ifnidx;

    //printf("scantoelseend(): expflag = %d\n", expflag);
    while (1)
    {
        switch (xc)
        {
            default:
                eatrol();               /* ignore the line              */
                goto case ' ';
            case ' ':
            case LF:
            case '\t':
            case '\f':
            case '\13':
            case CR:
                egchar();
                continue;
            case '/':
                egchar();
                if (xc == '/')
                {   cppcomment();
                    continue;
                }
                else if (xc == '*')
                {   comment();
                    continue;           /* treat comment as whitespace  */
                }
                eatrol();
                egchar();
                continue;

            case '#':                   /* could be start of pragma     */
                break;
            case '%':
                // %: is digraph for #
                if (config.flags3 & CFG3digraphs && egchar() == ':')
                {
version (SPP)
{
                    expbackup();
}
                    xc = '#';
                }
                break;
            case PRE_EOF:
                preerr(EM_eof_endif);           // eof before #endif
                err_fatal(EM_eof);              // premature end of source file
                break;
        }
        token();
    L1:
        if (tok.TKval == TKpragma)      /* if we read in a pragma       */
        {
            switch (tok._pragma)  /* pragma number                */
            {
                case PRelif:
                case PRelseif:
                    if (ifn[ifnidx].IFseen == IF_ELSE)  /* already seen #else */
                        synerr(EM_else);                // #elif without #if
                    if ((ifn[ifnidx].IFseen == IF_IF || ifn[ifnidx].IFseen == IF_FIRSTIF) &&
                        ifnidxstart == ifnidx)
                    {
                        if (ifnidx > 0)
                            ifnidx--;
                        expflag--;      /* start listing again          */
version (SPP)
{
}
else
{
                        expstring("#elif ");
}
                        prif();         /* #else followed by #if        */
                        return;
                    }
                    break;
                case PRif:
                case PRifdef:
                case PRifndef:
                    incifn();           /* bump nesting level           */
                    break;
                case PRelse:
                    blankrol();
                    if (ifn[ifnidx].IFseen == IF_ELSE)  /* already seen #else */
                        synerr(EM_else);                // #else without #if
                    if (ifnidxstart == ifnidx &&
                        (ifn[ifnidx].IFseen == IF_IF || ifn[ifnidx].IFseen == IF_FIRSTIF))
                    {   expflag--;      /* start listing again          */
version (SPP)
{
}
else
{
                        expstring("#else" ~ LF_STR);
}
                        exp_ppon();
                        return;
                    }
                    ifn[ifnidx].IFseen = IF_ELSE;       /* seen the #else */
                    break;
                case PRendif:
                    //printf("PRendif: expflag = %d\n", expflag);
                    assert(ifnidx > 0);
                    if (ifnidxstart == ifnidx--)
                    {   expflag--;
version (SPP)
{
}
else
{
                        expstring("#endif ");
                        if (xc == LF)
                            expstring(LF_STR);
}
                        blankrol();
                        exp_ppon();
                        return;
                    }
                    blankrol();
                    break;
                case PRdefine:
                    scantodefine();
                    break;
                case PRline:
                    if (token() != TKnum)
                        goto L1;
                    goto case PRinclude;
                case PRinclude:
                case PRinclude_next:
                    //printf("PRinclude: expflag = %d\n", expflag);
                    ininclude++;
                    if (token() == TKident && config.flags2 & CFG2expand)
                        listident();
                    ininclude--;
                    if (tok.TKval == TKpragma)
                        goto L1;
                    break;
                case PRundef:
static if (0)
{
                case PRexit:
                case PRmessage:
}
                case PRpragma:
                case PRerror:
                case PRident:
static if (TARGET_LINUX || TARGET_OSX || TARGET_FREEBSD || TARGET_OPENBSD || TARGET_SOLARIS)
{
                case PRassert:
                case PRunassert:
                case PRsystem:
                case PRcpu:
                case PRlint:
                case PRmachine:
                case PRwarning:
}
                    break;              /* scanto these pragmas         */
                case -1:                // ignore unrecognized pragmas in
                                        // false conditionals
                    if (config.ansi_c)
                        lexerr(EM_preprocess);
                    break;
                default:
                    assert(0);
            } /* switch */
            eatrol();                   /* scanto rest of line          */
            exp_ppon();
        } /* if */
  } /* while */
}


/*******************************
 * Skip over a #define. This requires care as it can be
 * a multi-line definition.
 * Output:
 *      xc is on EOL
 */

/*private*/ void scantodefine()
{   int lastxc;
    int instr;

    instr = 0;
    lastxc = ' ';
    while (1)
    {
        switch (xc)
        {
            case LF:
            case '\13':
            case '\f':
                if (lastxc == '\\')
                    break;
                goto case 0;
            case 0:
                return;

            case CR:
                egchar();
                continue;

            case '/':
                if (!instr && lastxc == '/')    /* if C++ style comment */
                {   cppcomment();
                    return;
                }
                break;

            case '*':
                if (!instr && lastxc == '/')
                {   comment();
                    lastxc = ' ';
                    continue;
                }
                break;

            case '"':                   /* string delimiters            */
                if (lastxc == 'R' && !instr)
                {
                    RawString rs;
                    rs.init();
                    while (1)
                    {
                        egchar();
                        if (xc == 0 || !rs.inString(cast(ubyte)xc))
                            break;
                    }
                    break;
                }
                goto case '\'';
            case '\'':
                if (instr)
                {   if (xc == instr && lastxc != '\\')
                        instr = 0;
                }
                else
                    instr = xc;
                break;

            case '\\':
                if (lastxc == '\\')
                    xc = ' ';
                break;

            default:
                break;
        }
        lastxc = xc;
        egchar();
    }
}


/******************************
 * Eat chars till we reach end of line.
 * Watch out for end of files (xc == 0)!
 */

/*private*/ void eatrol()
{
    if (config.ansi_c)                   /* elide comments and strings   */
    {
        while (1)
        {
            switch (xc)
            {
                default:
                    egchar();
                    continue;
                case LF:
                case PRE_EOF:
                    return;
                case '/':
                    egchar();
                    if (xc == '/')
                    {   cppcomment();
                        return;
                    }
                    if (xc == '*')
                        comment();
                    continue;
                case '"':
                case '\'':
                    token();            /* read in string               */
                    continue;           /* and ignore it                */
            }
            break;
        }
    }
    if (xc != LF && xc != 0)
    {
version (SPP)
{
            while (xc != LF && xc != PRE_EOF)
                egchar();
}
else
{
        if (config.flags2 & CFG2expand || bl.BLtyp != BLfile)
            while (xc != LF && xc != PRE_EOF)
                egchar();
        else
        {
            // Line buffer is guaranteed to end with a '\n', and not
            // have any PRE_ARG's in it,
            // so just dump the rest of the line buffer
            *btextp = 0;
            xc = LF;
        }
    }
}
}

/******************************
 * Eat rest of line, but gripe if there is anything on it except whitespace.
 */

/*private*/ void blankrol()
{
    while (1)
    {
        switch (xc)
        {
            case LF:
            case 0:
                return;
            case ' ':
            case '\t':
            case '\f':
            case '\13':
            case PRE_SPACE:
            case PRE_BRK:
                break;
            case '/':
                egchar();
                if (xc == '/')
                {
                    cppcomment();
                    return;
                }
                if (xc == '*')
                {
                    comment();
                    continue;
                }
                goto default;

            default:
                if (config.ansi_c)
                    preerr(EM_eol);             // end of line expected
                goto rol;
        }
        egchar();
    }

rol:
    eatrol();
}

/******************************
 * Bump nesting level of #ifs.
 */

enum IFNEST_INC = 30;

/*private*/ void incifn()
{
        if (++ifnidx >= ifnmax)
        {   ifnmax += IFNEST_INC;
            ifn = cast(IFNEST *)
                MEM_PARC_REALLOC(ifn,ifnmax * IFNEST.sizeof);
        }
        ifn[ifnidx].IFseen = IF_IF;
}

/*************************
 * Pretty-print macro
 */

debug
{

void macro_print(macro_t *m)
{
    dbg_printf("MACRO: %p\n",m);
    dbg_printf("m.Mid = '%s'\n",m.Mid.ptr);
    dbg_printf("m.Mtext  = '%s'\n",m.Mtext);
    dbg_printf("m.Mflags = x%x\n",m.Mflags);
    dbg_printf("m.ML     = %p\n",m.ML);
    dbg_printf("m.MR     = %p\n",m.MR);
    macro_debug(m);
}

}


version (SPP)
{
}
else
{

/**********************************
 * Take the threaded list of #defines, hydrate the macros on that list,
 * and merge those macros into the macro symbol table.
 * Input:
 *      flag    !=0     hydrate and add into existing macro table
 *              0       hydrate in place
 */

void pragma_hydrate_macdefs(macro_t **pmb,int flag)
{   macro_t *mb;
    debug int count = 0;

    for (; *pmb; pmb = &mb.Mnext)
    {
        if (dohydrate)
        {
            mb = cast(macro_t *)ph_hydrate(cast(void**)pmb);
            macro_debug(mb);
            //dbg_printf("macro_hydrate(%p, '%s')\n",mb,mb.Mid.ptr);
            debug assert(!mb.Mtext || isdehydrated(mb.Mtext));
            mb.Marglist.hydrate();
            ph_hydrate(cast(void**)&mb.Mtext);
        }
        else
        {
            mb = *pmb;
            macro_debug(mb);
            //dbg_printf("macro_hydrate(%p, '%s')\n",mb,mb.Mid.ptr);
        }

        // Skip predefined macros
        if (flag && !(mb.Mflags & Mfixeddef))
        {   char *p;
            uint hash;
            byte cmp;
            char c;
            int len;
            macro_t *m;
            macro_t **mp;

            p = mb.Mid.ptr;
            c = *p;
            len = strlen(p);

            //dbg_printf("macro '%s' = '%s'\n",mb.Mid.ptr,mb.Mtext);
debug
{
            count++;
            assert(isidstart(mb.Mid[0]));
}

static if (0)       // inlined for speed
            hash = comphash(p);
else
            hash = (((cast(int)c << 8) + len) << 8) + (p[len - 1] & 0xFF);;


            mp = &mactabroot[hashtoidx(hash)];  /* parent of root       */
            m = *mp;                            /* root of macro table  */
            while (1)                           /* while more tree      */
            {
                if (!m)
                {
                    if (mb.ML || mb.MR)
                        mb.ML = mb.MR = null;
                    *mp = mb;
                    break;
                }
                macro_debug(m);
                if ((cmp = cast(byte)(c - m.Mid[0])) == 0)
                {   cmp = cast(byte)memcmp(p + 1,m.Mid.ptr + 1,len); // compare identifiers
                    if (cmp == 0)                       // already there
                    {
                        if (m.Mflags & mb.Mflags & Mdefined)
                        {
                            if (config.ansi_c &&
                                (strcmp(m.Mtext,mb.Mtext) ||
                                 m.Mflags & (Minuse | Mfixeddef))
                               )
                                //dbg_printf("text1 '%s' text2 '%s' flags x%x\n",m.Mtext,mb.Mtext,m.Mflags),
                                preerr(EM_multiple_def,p);      // already defined
                        }
                        mb.ML = m.ML;
                        mb.MR = m.MR;
                        *mp = mb;
                        break;
                    }
                }
                mp = (cmp < 0) ? &m.ML : &m.MR;       /* select correct child */
                m = *mp;
            }
        }
    }
debug
{
    //printf("%d macdefs hydrated\n",count);
}
}

static if (DEHYDRATE)
{

void pragma_dehydrate_macdefs(macro_t **pm)
{   macro_t *m;

    for (; *pm; pm = &m.Mnext)
    {
        m = *pm;
        macro_debug(m);
        //dbg_printf("macro_dehydrate(%p, '%s')\n",m,m.Mid.ptr);
        ph_dehydrate(pm);
        ph_dehydrate(&m.Mtext);
        m.Marglist.dehydrate();
    }
}

}

/**********************************
 */

void *pragma_dehydrate()
{
    int i;

    for (i = 0; i < MACROHASHSIZE; i++)
    {   macro_t *m;

        m = mactabroot[i];
        if (m)
        {   macrotable_balance(&mactabroot[i]);
static if (DEHYDRATE)
{
            m = mactabroot[i];
            ph_dehydrate(&mactabroot[i]);
            macro_dehydrate(m);
}
        }
    }

    return mactabroot;
}

/**********************************
 */

/*private*/ void pragma_insert(macro_t *m)
{
    while (m)
    {
        //printf("pragma_insert('%s', flags = x%x)\n",m.Mid.ptr,m.Mflags);
        if ((m.Mflags & (Mdefined | Mfixeddef | Mnoparen)) == (Mdefined | Mnoparen) &&
            m.Marglist.empty())
        {
            //printf("defmac('%s')\n",m.Mid.ptr);
            defmac(m.Mid.ptr,m.Mtext);
        }
        pragma_insert(m.ML);
        m = m.MR;
    }
}


/**********************************
 * Rehydrate all the preprocessor symbols, starting from
 * pmactabroot.
 */

void pragma_hydrate(macro_t **pmactabroot)
{   int i;
    macro_t **oldmactabroot;

    //printf("pragma_hydrate()\n");
    oldmactabroot = mactabroot;
    mactabroot = pmactabroot;

    if (dohydrate)
    {
    for (i = 0; i < MACROHASHSIZE; i++)
    {
debug
{
        macro_t *m = pmactabroot[i];

        assert(!m || isdehydrated(m));
}
        if (pmactabroot[i])
        {   //printf("i = %d, m = %p\n",i,pmactabroot[i]);
            ph_hydrate(cast(void**)&pmactabroot[i]);
            macro_hydrate(pmactabroot[i]);
        }
    }
    }

    // Run through the old macro table, and transfer any #defines in there
    // to the new macro table. This is so that command line defines are not
    // lost when a PH is read.
    for (i = 0; i < MACROHASHSIZE; i++)
    {
        if (oldmactabroot[i])
            pragma_insert(oldmactabroot[i]);
    }
}

/**********************************
 * Dehydrate a macro and its children.
 */

static if (DEHYDRATE)
{
/*private*/ void macro_dehydrate(macro_t *m)
{
    while (m)
    {   macro_t *ml;
        macro_t *mr;

        //dbg_printf("macro_dehydrate(%p, '%s')\n",m,m.Mid.ptr);
        macro_debug(m);
        ml = m.ML;
        mr = m.MR;
        ph_dehydrate(&m.ML);
        ph_dehydrate(&m.MR);
        ph_dehydrate(&m.Mtext);
        m.Marglist.dehydrate();
        macro_dehydrate(ml);
        m = mr;
    }
}
}

/**********************************
 * Hydrate a macro, including all it's children.
 * Sort the macros into the global macro table.
 * Discard macros that we don't need.
 */

static if (HYDRATE)
{
/*private*/ void macro_hydrate(macro_t *mb)
{
    while (mb)
    {   macro_t* ml,mr;

debug
{
        assert(!mb.ML || isdehydrated(mb.ML));
        assert(!mb.MR || isdehydrated(mb.MR));
        assert(!mb.Mtext || isdehydrated(mb.Mtext));
        macro_debug(mb);
}
        //dbg_printf("macro_hydrate(%p, '%s')\n",mb,mb.Mid.ptr);

        mb.Marglist.hydrate();
        ph_hydrate(cast(void**)&mb.Mtext);
        ml = cast(macro_t *)ph_hydrate(cast(void**)&mb.ML);
        mr = cast(macro_t *)ph_hydrate(cast(void**)&mb.MR);
        macro_hydrate(ml);
        mb = mr;
    }
}
}

/*
 * Balance our macro tree in place. This is nice for precompiled headers, since they
 * will typically be written out once, but read in many times. We balance the tree in
 * place by traversing the tree inorder and writing the pointers out to an ordered
 * list. Once we have a list of macro pointers, we can create a tree by recursively
 * dividing the list, using the midpoint of each division as the new root for that
 * subtree.
 */

__gshared
{
/*private*/ uint nmacs;
/*private*/ uint mac_dim;
/*private*/ uint mac_index;
}

/*private*/ void count_macros(macro_t *m)
{
    while (m)
    {   nmacs++;
        count_macros(m.ML);
        m = m.MR;
    }
}

/*private*/ void place_in_array(macro_t *m)
{
    while (m)
    {   place_in_array(m.ML);
        mac_array[mac_index++] = m;
        m = m.MR;
    }
}

/*
 * Create a tree in place by subdividing between lo and hi inclusive, using i
 * as the root for the tree. When the lo-hi interval is one, we've either
 * reached a leaf or an empty node. We subdivide below i by halving the interval
 * between i and lo, and using i-1 as our new hi point. A similar subdivision
 * is created above i.
 */
/*private*/ macro_t * create_tree(int i, int lo, int hi)
{
    macro_t *m;

    if (i < lo || i > hi)               /* empty node ? */
        return null;

    assert(cast(uint) i < nmacs);
    m = mac_array[i];
debug
{
    macro_debug(m);
    assert(m);
    mac_array[i] = null;
}
    if (i == lo && i == hi)             /* leaf node ? */
    {   m.ML = null;
        m.MR = null;
    }
    else
    {
        m.ML = create_tree((i + lo) / 2, lo, i - 1);
        m.MR = create_tree((i + hi + 1) / 2, i + 1, hi);
    }

    return m;
}


/*private*/ void count_macros(macro_t *m);
/*private*/ void place_in_array(macro_t *m);
/*private*/ macro_t * create_tree(int i, int lo, int hi);

/*private*/ void macrotable_balance(macro_t **ps)
{
    nmacs = 0;
    count_macros(*ps);
    //dbg_printf("Number of macros = %d\n",nmacs);
    if (nmacs <= 2)
        return;

    if (nmacs > mac_dim)
    {
        // Use malloc instead of mem because of pagesize limits
        mac_array = cast(macro_t **) realloc(mac_array,nmacs * (macro_t *).sizeof);
        mac_dim = nmacs;
        if (!mac_array)
            err_nomem();
    }

    mac_index = 0;
    place_in_array(*ps);
    assert(mac_index == nmacs);

    *ps = create_tree(nmacs / 2, 0, nmacs - 1);
}


}
