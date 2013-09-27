// Copyright (C) 1984-1998 by Symantec
// Copyright (C) 2000-2009 by Digital Mars
// All Rights Reserved
// http://www.digitalmars.com
// Written by Walter Bright
/*
 * This source file is made available for personal use
 * only. The license is in /dmd/src/dmd/backendlicense.txt
 * or /dm/src/dmd/backendlicense.txt
 * For any other uses, please contact Digital Mars.
 */

/* Read in characters from a block.                     */

#include        <stdio.h>
#ifdef THINK_CPLUS
#include "transio.h"
#endif

#include        <string.h>
#include        <malloc.h>

#if __GNUC__ || __clang__
#include        <alloca.h>
#endif

#include        "ctype.h"
#include        "cc.h"
#include        "global.h"
#include        "parser.h"
#include        "token.h"
#if TARGET_MAC
#include        "TG.h"
#endif
#include        "filespec.h"
#include        "outbuf.h"

static char __file__[] = __FILE__;      /* for tassert.h                */
#include        "tassert.h"

#if TX86
extern char switch_E;
static blklst * last_blsave;
#else
INITIALIZED_STATIC_DEF blklst * last_blsave;
#endif
STATIC void freeblk(blklst *);

#if TARGET_MAC
INITIALIZED_STATIC_DEF blklst *bl_freelist = NULL;      /* pointer to next free blk     */
#elif TX86
static blklst *bl_freelist = NULL;      /* pointer to next free blk     */
#endif

Srcpos lastpos = {
0,      // line number
#if TX86
0,      // file number
#else
-1,     // file number
#endif
#if SOURCE_OFFSETS
0       // byte offset
#endif
};      // last filename/line seen
static bool uselastpos;

#if TX86
blklst * bl = NULL;     /* current block pointer                */
unsigned char * btextp = NULL;  // set to bl->BLtextp


/* Expanded version of source file: */

char *eline = NULL;
int elinmax = 0;                /* # of chars in buffer eline[]         */
int elini = 0;                  /* index into eline[]                   */
int elinnum = 1;                /* expanded line number                 */
int expflag = 0;                /* != 0 means not expanding list file   */
#endif

#if linux
#define FPUTC(c,fp) fputc_unlocked(c,fp)
#else
#define FPUTC(c,fp) fputc(c,fp)
#endif

STATIC unsigned char * stringize(unsigned char *text);

/************************************
 * Get and return current file block pointer.
 * Returns:
 *      NULL    if no more files
 */

blklst *blklst_getfileblock()
{   blklst *b;
    int i;

    i = 0;
    b = bl;
    while (1)
    {
        if (b)
        {   if (b->BLtyp == BLfile)
                break;
            b = b->BLprev;
        }
        else if (i)
            break;
        else
        {   b = last_blsave;
            i++;
        }
    }
    return b;
}

/************************************
 * Put a character back into the input of the current block.
 */

void putback(int c)
{
    if (c != PRE_EOF)
    {
        //printf("putback('%c')\n", c);
        //if (c == PRE_SPACE) *(char*)0=0;
#ifdef DEBUG
        assert(btextp > bl->BLtext);
#endif
        *--btextp = c;
    }
}

/**********************************
 * Take care of expanded listing.
 * Input:
 *      c       =       expanded character
 *      expflag =       if !=0, then don't insert chars in eline[]
 * Output:
 *      eline[] =       current line of expanded output
 *      elini =         index into eline[] to last char to be read
 *      elinnum =       line number of expanded output
 */

void explist(int c)
{
    if (expflag)
        return;
    if (c == PRE_SPACE)
        return;
//  if (c == ' ' && (bl && bl->BLflags & BLspace))
//      return;
    //printf("explist('%c', %x), elini = %d\n",c,c,elini);
    if (elini && (iseol(eline[elini - 1])       // if end of line character
#if SPP
        || c == PRE_EOF
#endif
       ))
    {
#if SPP
        if (*eline && *eline != '\n')   /* if line is not blank         */
        {
            static const char *format = "#line %d \"%s\"\n";
            blklst *b = cstate.CSfilblk;
            if (b)
            {
                int linnum = b->BLsrcpos.Slinnum - 1;
                if (!lastpos.Sfilptr || *lastpos.Sfilptr != *b->BLsrcpos.Sfilptr)
                {
                    if (!(config.flags3 & CFG3noline))
                    {
                        if (uselastpos)
                            fprintf(fout,format,lastpos.Slinnum - 1,srcpos_name(lastpos));
                        else
                            fprintf(fout,format,linnum,srcpos_name(b->BLsrcpos));
                    }
                    if (!uselastpos)
                        lastpos.Sfilptr = b->BLsrcpos.Sfilptr;
                }
                else if (linnum != elinnum)
                {
                    if (linnum == elinnum + 1)
                        FPUTC('\n',fout);
                    else if (!(config.flags3 & CFG3noline))
                        fprintf(fout,"#line %d\n",linnum);
                }
                elinnum = linnum;
            }
            else if (uselastpos && lastpos.Sfilptr && !(config.flags3 & CFG3noline))
            {
                fprintf(fout,format,lastpos.Slinnum - 1,srcpos_name(lastpos));
            }
        }
        uselastpos = false;
        wrtexp(fout);
#else
        if (flst) wrtexp(flst);         /* if we're making a list file  */
#endif
        elini = 0;
        eline[0] = 0;
        elinnum++;                      /* line number                  */
    }
    expinsert(c);
}


/************************************
 * Send a string to explist().
 */

void expstring(const char *p)
{
  while (*p) explist(*p++);
}


/************************************
 * Insert char into expanded output.
 */

void expinsert(int c)
{
  if (!(config.flags2 & CFG2expand) || !c || c == PRE_SPACE)
        return;
  if (elini + 1 >= elinmax)
  {     elinmax += 80;
        eline = (char *) MEM_PERM_REALLOC(eline,elinmax);
  }
  eline[elini++] = c;
  eline[elini] = 0;
}

/************************************
 * Backup expanded output 1 char. Tough
 * if nothing to back up.
 * Don't back up if we're not expanding.
 * Note that elini is the index of where to put the NEXT character,
 * so it currently points to where the End Of String is.
 */

void expbackup()
{
#if TX86
    if (config.flags2 & CFG2expand && expflag == 0 && elini != 0)
#else
    if (config.flags2 & CFG2expand && EXPANDING_LISTING() && elini != 0)
#endif
    {
        //printf("expbackup()\n");
        eline[--elini] = 0;
    }
}


/***********************************
 * Write expanded output to stream.
 * Make sure we get one and only one carriage return at the end.
 */

void wrtexp(FILE *fstream)
{
    if (!eline)
        return;

    for (char *p = eline; 1; p++)
    {   unsigned char c = *p;

        if ((signed char)c >= ' ')
        {
            FPUTC(c, fstream);
            continue;
        }
        switch (c)
        {
            case 0:
                crlf(fstream);
                return;

            case CR:
            case LF:
                break;

            case PRE_BRK:                               // token separator
                /* If token separator is needed to separate tokens, output a space.
                 * Multiple PRE_BRKs are treated as one.
                 */
                if (eline < p && p[1] != 0)
                {   unsigned char xclast, xcnext;

                    xclast = p[-1];
                    while (1)
                    {
                        xcnext = p[1];
                        if (xcnext != PRE_BRK)
                            break;
                        p++;
                    }

                    if (!isspace(xclast) && !isspace(xcnext) &&
                        insertSpace(xclast, xcnext))
                    {
                        FPUTC(' ', fstream);
                    }
                }
                break;

            default:
                FPUTC(c, fstream);
                break;
        }
    }
}

/***********************************************
 * Remove leading and trailing whitespace.
 * Leave PRE_SPACE intact.
 */

unsigned char *trimWhiteSpace(unsigned char *text)
{
    //printf("+trimWhiteSpace('%s')\n", text);
    size_t len = strlen((char *)text);
    unsigned char *p = text;

    // Remove leading
    while (1)
    {
        if (*p == ' ' || *p == PRE_BRK)
        {
            memmove(p, p + 1, len);
            len--;
        }
        else if (*p == PRE_SPACE)
        {
            len--;
            p++;
        }
        else
            break;
    }

    // Remove trailing
    int n = 0;
    p += len;
    while (len)
    {
        p--;
        if (*p == ' ' || *p == PRE_BRK)
            len--;
        else if (*p == PRE_SPACE)
        {   n++;
            len--;
        }
        else
        {   p++;
            break;
        }
    }
    if (n)
        memset(p, PRE_SPACE, n);
    p[n] = 0;

    //printf("-trimWhiteSpace('%s')\n", text);
    return text;
}

/******************************************
 * Remove all PRE_SPACE markers.
 * Remove all leading and trailing whitespace.
 */

unsigned char *trimPreWhiteSpace(unsigned char *text)
{
    //printf("+trimPreWhiteSpace('%s')\n", text);
    size_t len = strlen((char *)text);
    unsigned char *p = text;

    // Remove leading
    while (1)
    {
        if (*p == ' ' || *p == PRE_SPACE || *p == PRE_BRK)
        {
            memmove(p, p + 1, len);
            len--;
        }
        else
            break;
    }

    while (len)
    {
        if (*p == PRE_SPACE || *p == PRE_BRK)
        {
            memmove(p, p + 1, len);
        }
        else
            p++;
        len--;
    }

    // Remove trailing
    while (p > text && p[-1] == ' ')
        p--;
    *p = 0;

    //printf("-trimPreWhiteSpace('%s')\n", text);
    return text;
}

/********************************************
 * Get Ith arg from args.
 */

unsigned char *getIthArg(phstring_t args, int argi)
{
    if (args.length() < argi)
        return NULL;
    return (unsigned char *)args[argi - 1];
}

/*******************************************
 * Build macro replacement text.
 * Returns:
 *      string that must be parc_free'd
 */

unsigned char *macro_replacement_text(macro_t *m, phstring_t args)
{
    //printf("macro_replacement_text(m = '%s')\n", m->Mid);
    //printf("\tMtext = '%s'\n", m->Mtext);
    //printf("\tMtext = "); macrotext_print(m->Mtext); printf("\n");

    unsigned char tmpbuf[128];
    Outbuffer buffer(tmpbuf, 128, 100);
    buffer.reserve(strlen(m->Mtext) + 1);

    /* PRE_ARG, PRE_STR and PRE_CAT only appear in Mtext
     */

    for (unsigned char *q = (unsigned char *)m->Mtext; *q; q++)
    {
        if (*q == PRE_ARG)
        {   unsigned char argi;
            unsigned char *a;
            unsigned char argj;
            unsigned char *b;
            int expand = 1;
            int trimleft = 0;
            int trimright = 0;

        Lagain2:
            argi = *++q;
            switch (argi)
            {
                case PRE_ARG:           // PRE_ARG was 'quoted'
                    buffer.writeByte(xc);
                    continue;

                case PRE_STR:           // stringize argument
                    argi = *++q;
                    a = getIthArg(args, argi);
                    a = stringize(a);
                    buffer.write(a);
                    parc_free(a);
                    continue;

                case PRE_CAT:
                    if (q[1] == PRE_ARG)
                    {   expand = 0;
                        trimleft = 1;
                        q++;
                        goto Lagain2;
                    }
                    continue;           // ignore

                default:
                    // If followed by CAT, don't expand
                    if (q[1] == PRE_ARG && q[2] == PRE_CAT)
                    {   expand = 0;
                        trimright = 1;

                        /* Special case of PRE_ARG i PRE_ARG PRE_CAT PRE_ARG j
                         * Paul Mensonides writes:
                         * In summary, blue paint (PRE_EXP) on either operand of
                         * ## should be discarded unless the concatenation doesn't
                         * produce a new identifier--which can only happen (in
                         * well-defined code) via the concatenation of a
                         * placemarker.  (Concatenation that doesn't produce a
                         * single preprocessing token produces undefined
                         * behavior.)
                         */
                        if (q[3] == PRE_ARG &&
                            (argj = q[4]) != PRE_ARG &&
                            argj != PRE_STR &&
                            argj != PRE_CAT)
                        {
                            //printf("\tspecial CAT case\n");
                            a = getIthArg(args, argi);
                            size_t len = strlen((char *) a);
                            while (len && (a[len - 1] == ' ' || a[len - 1] == PRE_SPACE))
                                len--;

                            b = getIthArg(args, argj);
                            unsigned char *bstart = b;
                            while (*b == ' ' || *b == PRE_SPACE || *b == PRE_EXP)
                                b++;
                            if (!isidchar(*b))
                                break;
                            if (!len && b > bstart && b[-1] == PRE_EXP)
                             {  // Keep the PRE_EXP
                                buffer.write(b - 1);
                                q += 4;
                                continue;
                             }

                            unsigned char *pe = (unsigned char *)strrchr((char *)a, PRE_EXP);
                            if (!pe)
                                break;
                            if (!isidstart(pe[1]))
                                break;

                            for (size_t k = pe + 1 - a; k < len; k++)
                            {
                                if (!isidchar(a[k]))
                                    goto L1;
                            }

                            buffer.write(a, pe - a);
                            buffer.write(pe + 1, len - (pe + 1 - a));
                            buffer.write(b);
                            q += 4;
                            continue;
                        }
                    }
                    break;
            }
        L1:
            a = getIthArg(args, argi);
            //printf("\targ[%d] = '%s'\n", argi, a);
            if (expand)
            {
                a = macro_expand(a);
                trimPreWhiteSpace(a);
                buffer.write(a);
                parc_free(a);
            }
            else
            {   unsigned char *p = a;
                size_t len = strlen((char *) p);
                if (trimleft)
                {
                    while (len && (*p == ' ' || *p == PRE_SPACE || *p == PRE_EXP))
                    {   p++;
                        len--;
                    }
                }
                if (trimright)
                {
                    while (len && (p[len - 1] == ' ' || p[len - 1] == PRE_SPACE))
                        len--;
                }
                buffer.write(p, len);
            }
        }
        else
            buffer.writeByte(*q);
    }

    args.free(MEM_PARF_FREEFP);

    unsigned len = buffer.size();
    unsigned char *string = (unsigned char *)parc_malloc(len + 1);
    memcpy(string, buffer.buf, len);
    string[len] = 0;
    //printf("\treplacement text = '%s'\n", string);
    return string;
}

/********************************************
 * Rescan macro replacement text.
 * Returns:
 *      string that must be parc_free'd
 */

unsigned char *macro_rescan(macro_t *m, unsigned char *text)
{   unsigned char *result;

    m->Mflags |= Minuse;
    result = macro_expand(text);
    result = trimWhiteSpace(result);
    m->Mflags &= ~Minuse;

    if (!*result)
    {
        // result is empty, replace with a PRE_SPACE
        parc_free(result);
        result = (unsigned char *)parc_malloc(2);
        result[0] = PRE_SPACE;
        result[1] = 0;
    }
    return result;
}


/*****************************************
 * Return copied string which is a fully macro expanded text.
 * Returns:
 *      string that must be parc_free'd
 */

unsigned char *macro_expand(unsigned char *text)
{
#define LOG_MACRO_EXPAND        0

#if LOG_MACRO_EXPAND
    printf("+macro_expand(text = '%s')\n", text);
#endif

    int tc;                             // terminating char of string
    int notinstr = 1;                   // 0 if we're in a string
    int lastxc = ' ';                   // last char read
    unsigned char blflags = 0;

    // ==========
    // Save the state of the scanner
    BlklstSave blsave;
    blsave.BSbl = bl;
    blsave.BSbtextp = btextp;
    blsave.BSxc = xc;
    bl = NULL;
    btextp = NULL;
    unsigned idhashsave = idhash;
    int tok_ident_len = strlen(tok_ident) + 1;
    char *tok_ident_save = (char *) alloca(tok_ident_len);
    memcpy(tok_ident_save,tok_ident,tok_ident_len);
    xc = 0;
    // ==========

    int expflagsave = expflag;
    expflag++;

    // rescan the string
    insblk2(text, BLarg);
    egchar();

    unsigned char tmpbuf[128];
    Outbuffer buffer(tmpbuf, 128, 100);

    buffer.reserve(strlen((char *)text) + 1);

    while (1)
    {
        buffer.reserve(4);

        //printf("xc = '%c'\n", xc);
        switch (xc)
        {
            case '\\':
                if (lastxc == '\\')
                {   lastxc = ' ';
                    goto L1;
                }
                break;

            case '\'':
            case '"':                   // if a string delimiter
                if (!notinstr)          // if already in a string
                {   if (xc == tc && lastxc != '\\')
                    notinstr = 1;       // drop out of string
                }
                else
                {   tc = xc;            // terminating char of string
                    notinstr = 0;       // we're in a string
                }
                break;

            case PRE_EXP:
                if (notinstr)
                {
                    buffer.writeByte(PRE_EXP);
                    egchar();
                    if (isidstart(xc))
                    {   /* Read in identifier, but do not check to
                         * see if it is a macro.
                         * Just pass it through.
                         */
                        inident();
                        buffer.write(tok_ident);
                        lastxc = ' ';
                        continue;
                    }
                }
                break;

            case PRE_EOB:               // if end of text[]
                goto Ldone;

            default:
                if (dbcs && ismulti(xc))        // if asian 2 byte char
                {   buffer.writeByten(xc);
                    lastxc = xc;
                L2:
                    xc = egchar();      // no processing for this char
                    goto L1;
                }

                if (notinstr && isidstart(xc))
                {
                    macro_t *m;

                    blflags = bl->BLflags;
                    inident();          // read in identifier

#if LOG_MACRO_EXPAND
                    printf("\ttok_ident[] = '%s'\n", tok_ident);
#endif
                    /* Handle case of 1234ULL.
                     * BUG: still regards ABC as a macro in: 0x123.ABC
                     */
                    if (!isdigit(lastxc))
                    {
                        // Determine if tok_ident[] is a macro
                        char *idsave = tok.TKid;
                        tok.TKid = tok_ident;
                        if (blflags & BLexpanded && bl && bl->BLflags & BLexpanded)
                        {   /* Identifier was already scanned, and is
                             * not the last token in the scanned text.
                             */
                            tok.TKid = idsave;
                            buffer.write(tok_ident);
                            lastxc = ' ';
                            continue;
                        }
                        m = macfind();
                        tok.TKid = idsave;

                        if (m && m->Mflags & Mdefined)
                        {   phstring_t args;

                            if (m->Mflags & Minuse)
                            {
                                // Mark this identifier as being disabled
                                buffer.writeByten(PRE_EXP);
                            }
                            else if (!m->Mtext)
                            {   // Predefined macro
                                unsigned char *p = macro_predefined(m);
                                putback(xc);
                                if (p)
                                    insblk2(p, BLstr);
                                egchar();
                                lastxc = ' ';
                                continue;
                            }
                            else if (macprocess(m, &args, &blsave))
                            {   unsigned char *p;
                                unsigned char *q;
                                unsigned char xcnext = xc;
                                unsigned char xclast;
                                static unsigned char brk[2] = { PRE_BRK, 0 };

                                putback(xc);
                                p = macro_replacement_text(m, args);
                                q = macro_rescan(m, p);
                                parc_free(p);

                                /*
                                 * Insert break if necessary to prevent
                                 * token concatenation.
                                 */
                                if (!isspace(xcnext))
                                {
                                    insblk2(brk, BLrtext);
                                }

                                insblk2(q, BLstr);
                                bl->BLflags |= BLexpanded;
                                insblk2(brk, BLrtext);
                                egchar();
                                lastxc = ' ';
                                continue;
                            }
                        }
                    }
                    buffer.write(tok_ident);
                    lastxc = ' ';
                    continue;
                }
                break;
        }
        lastxc = xc;

    L1:
#if LOG_MACRO_EXPAND
        printf("\twriteByten('%c', x%02x)\n", xc, xc);
#endif
        buffer.writeByten(xc);
        egchar();
    }

  Ldone:
    unsigned len = buffer.size();
    unsigned char *buf = buffer.buf;
    unsigned char *string = (unsigned char *)parc_malloc(len + 1);
    memcpy(string, buf, len);
    string[len] = 0;

    expflag--;
    assert(expflagsave == expflag);

    // ==========
    // Restore the state of the scanner
    xc = blsave.BSxc;
    bl = blsave.BSbl;
    btextp = blsave.BSbtextp;
    memcpy(tok_ident, tok_ident_save, tok_ident_len);
    idhash = idhashsave;
    // ==========

#if LOG_MACRO_EXPAND
    printf("\tlen = %d\n", len);
//    for (int i = 0; i < len; i++)
//      printf("\tx%02x\n", string[i]);
    printf("-macro_expand() = '%s', expflag = %d\n", string, expflag);
#endif
    return string;
}

/****************************************
 * Return copied string which is a 'stringized' version of text.
 * Bugs: Comments are not dealt with in text string.
 * Input:
 * Returns:
 *      string that must be parc_free'd
 */

STATIC unsigned char * stringize(unsigned char *text)
{
    unsigned char tmpbuf[128];
    Outbuffer buffer(tmpbuf, 128, 100);

    unsigned char *p;
    unsigned char *string;
    unsigned char c;
    int tc;
    int esc;
    size_t len;

    //printf("+stringize('%s')\n", text);

    // Trim leading whitespace
    while (*text == ' ' || *text == PRE_SPACE || *text == PRE_BRK)
        text++;

    len = strlen((char *)text);

    // Trim trailing whitespace
    while (len && ((c = text[len - 1]) == ' ' ||
           c == PRE_SPACE || c == PRE_BRK))
        len--;

    buffer.reserve(len + 2 + 1);
    buffer.writeByten('"');

    tc = 0;
    esc = 0;
    for (size_t i = 0; i < len; i++)
    {
        unsigned char c = text[i];
        switch (c)
        {
            case '"':
                buffer.writeByte('\\');
            case '\'':
                if (tc)
                {
                   if (tc == c && !esc)
                        tc = 0;
                }
                else
                    tc = c;
                esc = 0;
                break;

            case '?':
                buffer.writeByte('\\');
                break;

            case '\\':
                if (tc)
                {   buffer.writeByte('\\');
                    esc ^= 1;
                }
                break;

            case PRE_EXP:
            case PRE_BRK:
                continue;               // skip markers

            default:
                esc = 0;
                break;
        }
        buffer.writeByte(c);
    }

  Ldone:
    buffer.writeByte('"');

    len = buffer.size();
    string = (unsigned char *)parc_malloc(len + 1);
    memcpy(string, buffer.buf, len);
    string[len] = 0;

    //printf("-stringize('%s')\n", string);
    return string;
}


/************************************
 * Get character from current block.
 * Back up a block if the current one ran out.
 * Substitute arguments.
 * Input:
 *      bl ->   current block
 * Output:
 *      bl ->   current block (may be previous one)
 * Returns:
 *      char
 *      0 if end of input
 */

unsigned egchar2()
{

Lagain:
    while (1)
    {
        debug_assert(bl);
        //dbg_printf("egchar2 xc '%c'\n",*btextp);
        if ((xc = *btextp++) == PRE_EOB)
        {   char btyp = bl->BLtyp;

            if (btyp == BLfile)
            {
#if EECONTEXT
                //printf("EEpending = %d, EElinnum = %d\n",eecontext.EEpending,eecontext.EElinnum);
                if (eecontext.EEpending &&
                    bl->BLsrcpos.Slinnum == eecontext.EElinnum &&
                    srcpos_sfile(bl->BLsrcpos).SFflags & SFtop
                   )
                {
                    btextp--;
                    insblk2((unsigned char *)eecontext.EEexpr,BLrtext);
                    eecontext.EEpending = 0;    // no longer pending
                    eecontext.EEimminent = 1;   // but imminent
                    goto Lagain;
                }
#endif
                do
                {
                    if (readln())       /* read in next line            */
                    {
#if HTOD
                        htod_writeline();
#else
                        if (flst && !(config.flags2 & CFG2expand))
                            wrtlst(flst);       /* send line to .LST file */
#endif
                    }
                    else
                        goto L1;
                } while ((xc = *btextp++) == PRE_EOB);
                bl->BLtextp = btextp;
                break;                  /* xc better not be an arg!     */
            }
            else
            {
              L1:
                freeblk(bl);    // back up one block
                if (!bl)
                {   xc = PRE_EOF;
                    break;
                }
            }
        }
        else
            break;
  }
L2:
    if (!(config.flags2 & CFG2expand))
        return xc;
    explist(xc);                /* do expanded listing          */
    return xc;
}

/*****************************************
 */

#if 1
#if 1 && TX86 && __SC__ && __INTSIZE == 4

__declspec(naked) unsigned egchar()
{
    _asm
    {
        xor     EAX,EAX
        mov     ECX,btextp
        mov     DL,switch_E
        mov     AL,[ECX]
        inc     ECX
        mov     byte ptr xc,AL
        test    AL,AL
        mov     btextp,ECX
        jle     L1
L2:     cmp     DL,AH
        jne     L3
        ret

L1:     jz      L4
        cmp     AL,0FFh
        jne     L2
L4:
        dec     btextp
        jmp     egchar2

L3:     push    EAX
        call    explist
        mov     EAX,xc
        ret
    }
}

#elif TX86 && __SC__ && __INTSIZE == 2

unsigned egchar()
{
    _asm
    {
        les     BX,btextp
        mov     AL,ES:[BX]
        xor     AH,AH
        mov     xc,AX
        test    AL,AL
        jle     L1
L2:     inc     BX
        mov     word ptr btextp,BX
        cmp     byte ptr switch_E,AH
        jne     L4
        retf

L1:     jz      L3
        cmp     AL,0FFh
        jne     L2
    }
L3:
    return egchar2();

L4:
    explist(_AX);
    return xc;
}

#else

unsigned egchar()
{
    //printf("egchar(xc='%c')\n",xc);
    debug_assert(bl);
    if ((xc = *btextp) != PRE_EOB && xc != PRE_ARG)
    {
        btextp++;
#if TARGET_MAC
        bl->BLcurcnt++;
#endif
        //if (!(config.flags2 & CFG2expand))
        if (!switch_E)
            return xc;
        explist(xc);            /* do expanded listing          */
        return xc;
    }
    return egchar2();
}

#endif
#endif



/***********************************************
 * Install a block, of the type specified
 * Input:
 *      text ->         text string of block (must be free-able)
 *                      For BLfile, text -> a file name
 *                      string, or NULL for stdin.
 *      typ =           BLxxxx
 *      aargs ->        list of actual arguments
 *      nargs ->        number of dummy arguments
 *                      (also used as flag if BLfile)
 *      m ->            macro (BLmacr)
 *      bl ->           currently open block
 * Output:
 *      bl ->           newly installed block
 */

void insblk(unsigned char *text, int typ, list_t aargs, int nargs, macro_t *m)
{   blklst *p;
    int flag = nargs;                   // so we won't destroy nargs
    int n;

#if !SPP
    debug_assert(PARSER);
#endif
    if (bl_freelist)
    {
        p = bl_freelist;
        bl_freelist = p->BLprev;
        memset(p, 0, sizeof(blklst));
    }
    else
    {
#if TX86 || PRAGMA_ONCE
        p = (blklst *) MEM_PH_CALLOC(sizeof(blklst));
                                        /* Needed in PH for pragma once */
#else
        p = (blklst *) MEM_PARC_CALLOC(sizeof(blklst));
#endif
    }
    p->BLtyp = typ;                     /* = BLxxxx                     */
    p->BLtext = text;                   /* text of block                */
    //printf("insblk() typ %d: text [%s]\n",typ,text);
    switch (typ)
    {
        case BLfile:
#if TX86
#else
                        p->BLtext = (unsigned char *) MEM_PARC_CALLOC(80);
#endif
                                                /* text not in PH */
                        p->BLtextmax = 80;
                        afopen((char *) text,p,flag);   /* open input file */
                        uselastpos = true;
                        cstate.CSfilblk = p;
                        sfile_debug(&srcpos_sfile(cstate.CSfilblk->BLsrcpos));
#if IMPLIED_PRAGMA_ONCE
                        p->BLflags |= BLnew;    /* at the start of a new file */
                        TokenCnt = 0;           /* count tokens till first #if */
#endif
                        break;
#if TARGET_MAC
        case BLpdef:    p->BLflags |= BFpdef;   /* flag pre_compilation data */
                        p->BLtyp = BLarg;
#endif
                        break;
        case BLstr:
        case BLarg:
        case BLrtext:
                        break;
        default:        assert(0);
    }
    if (bl)
        bl->BLtextp = btextp;
    btextp = p->BLtext;                 // point to start of text
    p->BLprev = bl;                     // point to enclosing block
    bl = p;                             // point to new block
    //dbg_printf("-insblk()\n");
}

/************************************
 * Alternate version for simpler BLtyp's.
 */

void insblk2(unsigned char *text, int typ)
{   blklst *p;

#if !SPP
    debug_assert(PARSER);
#endif
    if (bl_freelist)
    {
        p = bl_freelist;
        bl_freelist = p->BLprev;
        memset(p, 0, sizeof(blklst));
    }
    else
    {
#if TX86 || PRAGMA_ONCE
        p = (blklst *) MEM_PH_CALLOC(sizeof(blklst));
                                        // Needed in PH for pragma once
#else
        p = (blklst *) MEM_PARC_CALLOC(sizeof(blklst));
#endif
    }
    p->BLtyp = typ;                     // = BLxxxx
    p->BLtext = text;                   // text of block
    //dbg_printf("insblk2(typ %d: text [%s]\n",typ,text);
#ifdef DEBUG
    switch (typ)
    {
        case BLstr:
        case BLarg:
        case BLrtext:
                        break;
        default:        assert(0);
    }
#endif
    if (bl)
        bl->BLtextp = btextp;
    btextp = p->BLtext;                 // point to start of text
    p->BLprev = bl;                     // point to enclosing block
    bl = p;                             // point to new block
    //dbg_printf("-insblk2()\n");
}


/************************************
 * Free a block.
 * Output:
 *      bl      pointer to previous block, NULL if no more blocks.
 */

STATIC void freeblk(blklst *p)
{
    assert(p);
    bl = p->BLprev;
    if (bl)
        btextp = bl->BLtextp;
    switch (p->BLtyp)
    {
        case BLfile:
                cstate.CSfilblk = blklst_getfileblock();
                lastpos = p->BLsrcpos;          /* remember last line # */
                uselastpos = true;
                util_free(p->BLbuf);
#if TX86
#else
                MEM_PARC_FREE(p->BLtext);       /* free file data       */
#endif
#if SOURCE_OFFSETS
                lastpos.Sfiloff = p->Bfoffset+p->Blincnt;
#endif
#if PRAGMA_ONCE
                if(p->BLflags & BLponce ||
                   (!OPTnotonce && p->BLflags & BLckonce && TokenCnt == 1))
                    {                           /* do not read this file again */
                    p->BLprev = Once;           /* save name in read once list */
                    Once = p;
                    return;
                    }
#endif
#if IMPLIED_PRAGMA_ONCE
                // See if file was totally wrapped in #ifdef xxx #define xxx ... #endif
                if(((p->BLflags & BLckonce) == BLckonce) && (TokenCnt == 0))
                {                               // Mark file to only include once
                    srcpos_sfile(p->BLsrcpos).SFflags |= SFonce;
                    //dbg_printf("Setting the once flag\n");
                }
#endif

#if HEADER_LIST
                // See if we need to start reading in another
                // of the include files specified on the command line
                if (headers)
                {   assert(bl);
                    if (!bl->BLprev)
                    {   pragma_include((char *)list_ptr(headers),FQcwd | FQpath);
                        headers = list_next(headers);
                    }
                }
#endif
                break;

        case BLstr:
#if TX86
                parc_free(p->BLtext);
#else
                MEM_PARC_FREE(p->BLtext);
#endif
                break;
        case BLarg:                             /* don't free BLtext    */
        case BLrtext:
#if (TARGET_MAC)
                if (CPP && p->BLflags & BFpdef)
                    ANSI = ansi_opt;    /* now turn on ansi checking */
#endif
                break;
        default:
                assert(0);
    }

#ifdef DEBUG
    memset(p,0xFF,sizeof(blklst));
#endif
    p->BLprev = bl_freelist;
    bl_freelist = p;
}

/*****************************
 * Get and return current line number.
 * Do not do this if we're in an include file, because that mucks up
 * debuggers.
 * Returns:
 *      source file position
 */

Srcpos getlinnum()
{       blklst *b;

#if TARGET_MAC
        if (FromTokenList)              /* rescanning old tokens */
            return tok.TKsrcpos;
#endif
#if TX86
        b = cstate.CSfilblk;
#else
        b = blklst_getfileblock();
#endif
#if HOST_MPW
#if SOURCE_OFFSETS
        if (b)                          /* get file offset also */
            b->BLsrcpos.Sfiloff = b->BLfoffset+b->BLlincnt;
#endif
#endif
        // If past end of file, use last known position
        return b ? b->BLsrcpos : lastpos;
}

#if SOURCE_4SYMS || SOURCE_4TYPES || SOURCE_4PARAMS
/*****************************
 * Get current character position into TkIdStrtSrcpos.
 *      source file position
 */

void getcharnum()
{       blklst *b; long i;

        b = cstate.CSfilblk;
        if (b)
        {
            TkIdStrtSrcpos = b->BLsrcpos;
#if SOURCE_OFFSETS
            TkIdStrtSrcpos.Sfiloff = b->Bfoffset+b->Blincnt + (b->BLtextp-b->BLtext) - 1;
#endif
        }
        else
            TkIdStrtSrcpos = lastpos;
}
#endif

#if PRAGMA_ONCE && !HOST_THINK
void *once_dehydrate()
    {
    blklst *bl,*bl_next;
    for(bl=Once; bl; bl=bl_next)
        {
        bl_next = bl->BLprev;
        ph_dehydrate(&bl->BLprev);
        };
    return (void *)Once;
    }

/*
 * In order to rehydrate the once list and keep its data common, we need
 * to append the rehydrated list at the end, instead of insterting it at
 * the start. This is OK since the once list is only appended to at the
 * beginning, not the end.
 */
void once_hydrate(blklst *bl)
{
    blklst *last_bl = Once;

    if (last_bl)
    {

        /* find the last block */

        while (last_bl->BLprev)
            last_bl = last_bl->BLprev;

        /* point it at the start of the dehyrated Once list */

        last_bl->BLprev = bl;
    }

    /* if Once list is empty, use dehydrated bl to start list */

    else
        Once = bl;

    /* rehydrate the Once list in place */

    while (bl)
    {
        bl = (blklst *)ph_hydrate(&bl->BLprev); /* get link to previous block */
    }
}
#endif

/**************************
 * Terminate.
 */

#if TX86

void blklst_term()
{
#if TERMCODE
    blklst *b;

    for (b = bl_freelist; b; )
    {
        b = bl_freelist->BLprev;
        mem_free(bl_freelist);
        bl_freelist = b;
    }
#endif
}

#endif

#if HOST_RAINBOW
/*
 * In order to rehydrate the once list and keep its data common, we need
 * to append the rehydrated list at the end, instead of insterting it at
 * the start. This is OK since the once list is only appended to at the
 * beginning, not the end.
 */
void once_hydrate_loaded(blklst *bl)
{
    blklst *last_bl = Once;

    if (last_bl)
    {

        /* find the last block */

        while (last_bl->BLprev)
            last_bl = last_bl->BLprev;

        /* point it at the start of the dehyrated Once list */

        last_bl->BLprev = bl;
    }

    /* if Once list is empty, use dehydrated bl to start list */

    else
        Once = bl;
}
#endif


#if !TX86
void blklst_reinit(void)
{
        last_blsave = NULL;
        bl_freelist = NULL;
        btextp = NULL;          // block list text pointer
        bl = NULL;
        eline = NULL;
        elinmax = 0;
        elini = 0;
        elinnum = 1;
        expflag = 0;
        xc = ' ';

}
#endif

#if !TX86 && BROWSER
/*****************************
 * Determine current file and line number.
 * If file is different, a new filename is output.
 * Returns:
 *      line number
 */

unsigned blklst_linnum()
{   blklst *b;

    if (config.flags2 & CFG2browse)
    {
        b = blklst_getfileblock();
        if (b)
        {
#if !SPP
            outfilename(blklst_filename(b),b->BLcondlin);
#endif
            return b->BLsrcpos.Slinnum;
        }
    }
    return lastpos.Slinnum;
}

/*******************************
 * Set conditional line number for current block.
 */

void blklst_setcondlin()
{   blklst *b;

    if (config.flags2 & CFG2browse)
    {
        b = blklst_getfileblock();
        if (b)
            b->BLcondlin = b->BLsrcpos.Slinnum;
    }
}

#endif

/********************************
 * Pretty-print
 */

void BLKLST::print()
{
#ifdef DEBUG
    char *bltyp[] = {"    ","macr ","str  ","file ","arg  ","rtext" };

    printf("  %p BL%s", this, bltyp[BLtyp]);
    if (BLtyp == BLarg || BLtyp == BLrtext)
        printf(" Btext='%s', Btextp='%s'", BLtext, BLtextp);
    printf("\n");
#endif
}
