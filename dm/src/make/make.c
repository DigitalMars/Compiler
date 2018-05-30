/*_ make.c   */
/* Copyright (C) 1985-2012 by Walter Bright     */
/* All rights reserved                          */
/* Written by Walter Bright                     */
/* $Header$ */

/*
 * To build for Win32:
 *      dmc make tinyheap man -o
 */

#include        <stdio.h>
#include        <ctype.h>
#include        <time.h>
#include        <sys/stat.h>
#include        <string.h>
#include        <stdlib.h>
#include        <process.h>
#include        <dos.h>
#include        <direct.h>
#include        <stdbool.h>

#if _WIN32
#include        <windows.h>
#endif

#ifdef DEBUG
#define debug1(a)       printf(a)
#define debug2(a,b)     printf(a,b)
#define debug3(a,b,c)   printf(a,b,c)
#else
#define debug1(a)
#define debug2(a,b)
#define debug3(a,b,c)
#endif

#define TRUE    1
#define FALSE   0

#define ESC     '!'             /* our escape character                 */

#define NEWOBJ(type)    ((type *) mem_calloc(sizeof(type)))
#define arraysize(array)        (sizeof(array) / sizeof(array[0]))

#if __SC__ && !__NT__
int _okbigbuf = 0;              /* disallow big file buffers, in order  */
                                /* to make the in-memory size of make   */
                                /* as small as possible                 */
unsigned __cdecl _stack = 20000;        // set default stack size
#endif

void browse(const char *url);

/* File name comparison is case-insensitive on some systems     */
#if MSDOS || __OS2__ || __NT__
#define filenamecmp(s1,s2)      stricmp((s1),(s2))
#else
#define filenamecmp(s1,s2)      strcmp((s1),(s2))
#endif

/* Length of command line
 */

int CMDLINELEN;
void set_CMDLINELEN();

#if MSDOS
#define EXTMAX  3
#else
#define EXTMAX  5
#endif


/*************************
 * List of macro names and replacement text.
 *      name    macro name
 *      perm    if TRUE, then macro cannot be replaced
 *      text    replacement text
 *      next    next macro in list
 */

typedef struct MACRO
        {       char *name,*text;
                int perm;
                struct MACRO *next;
        } macro;

/*************************
 * List of files
 */

typedef struct FILELIST
        {       struct FILENODE *fnode;
                struct FILELIST *next;
        } filelist;

/*************************
 * File node.
 * There is one of these for each file looked at.
 *      name            file name
 *      genext          extension to be added for generic rule
 *      dlbcln          if file is a double-colon target
 *      expanding       set to detect circular dependency graphs
 *      time            time last modified
 *      dep             list of dependencies
 *      rules           pointer to rules for making this
 *      next            next file node in list
 */

typedef struct FILENODE
        {       char            *name,genext[EXTMAX+1];
                char            dblcln;
                char            expanding;
                time_t          time;
                filelist        *dep;
                struct RULE     *frule;
                struct FILENODE *next;
        } filenode;

/*************************
 * Implicit rule.
 *      fromext         starting extension
 *      toext           generated extension
 *      grule           creation rules
 *      next            next in list
 */

typedef struct IMPLICIT
        {       char            fromext[EXTMAX+1],toext[EXTMAX+1];
                struct RULE     *grule;
                struct IMPLICIT *next;
        } implicit;

/*************************
 * Make rules.
 * Multiple people can point to one instance of this.
 *      count           # of parents of this
 *      gener           TRUE if this is an implicit rule
 *      rulelist        list of rules
 */

typedef struct RULE
        {       int count;
                int gener;
                struct LINELIST *rulelist;
        } rule;

/*************************
 * List of lines
 */

typedef struct LINELIST
        {       char *line;
                struct LINELIST *next;
        } linelist;

/********************** Global Variables *******************/

static ignore_errors = FALSE;   /* if TRUE then ignore errors from rules */
static execute = TRUE;          /* if FALSE then rules aren't executed  */
static gag = FALSE;             /* if TRUE then don't echo commands     */
static touchem = FALSE;         /* if TRUE then just touch targets      */
static debug = FALSE;           /* if TRUE then output debugging info   */
static list_lines = FALSE;      /* if TRUE then show expanded lines     */
static usebuiltin = TRUE;       /* if TRUE then use builtin rules       */
static print = FALSE;           /* if TRUE then print complete set of   */
                                /* macro definitions and target desc.   */
static question = FALSE;        /* exit(0) if file is up to date,       */
                                /* else exit(1)                         */
static action = FALSE;          /* 1 if rules were executed             */
char *makefile = "makefile";    /* default makefile                     */

static filenode *filenodestart = NULL;  /* list of all files            */
static filelist *targlist = NULL;       /* main target list             */
static implicit  *implicitstart = NULL; /* list of implicits            */
static macro *macrostart = NULL;        /* list of macros               */

static filenode *dotdefault = NULL;     /* .DEFAULT rule                */

static char *buf = NULL;        /* input line buffer                    */
static int bufmax = 0;          /* max size of line buffer              */
static int curline = 0;         /* makefile line counter                */

static int inreadmakefile = 0;  /* if reading makefile                  */
static int newcnt = 0;          /* # of new'ed items                    */

#if 1
#define mem_init()
void *mem_realloc ( void *oldbuf , unsigned newbufsize );
char *mem_strdup ( const char *s );
void *mem_calloc ( unsigned size );
void mem_free ( void *p );
#endif

int doswitch (char *p );
void cmderr(char *format,...);
void faterr (char *format ,...);
time_t getsystemtime ( void );
void setsystemtime ( time_t datetime );
time_t gettimex ( char *name );
time_t touch ( char *name );
linelist **readmakefile ( char *makefile , linelist **rl );
void addmacro ( char *name , char *text , int perm );
linelist **targetline ( char *p );
int isimplicit ( char *p );
linelist **addimplicit ( char *p );
int readline ( FILE *f );
void addtofilelist ( char *filename , filelist **pflist );
filenode *findfile ( char *filename , int install );
char *expandline ( char *buf );
char *searchformacro ( char *name );
char *skipspace ( char *p );
char *skipname ( char *p );
char *filespecdotext ( char *p );
int isfchar ( int c );
int ispchar ( int c );
char *filespecforceext ( char *name , char *ext );
char *filespecgetroot ( char *name );
int do_implicits ( void );
int make ( filenode *f );
int dorules ( filenode *f );
void executerule ( char *p );
void freemacro ( void );
void freefilelist ( filelist *fl );
int freeimplicits ( void );
void freefilenode ( filenode *f );
void freerule ( rule *r );
void WRmacro ( macro *m );
void WRmacrolist ( void );
void WRlinelist ( linelist *l );
void WRrule ( rule *r );
void WRimplicit ( void );
void WRfilenode ( filenode *f );
void WRfilelist ( filelist *fl );
void WRfilenodelist ( filenode *fn );

/***********************
 */

_WILDCARDS;                     /* do wildcard expansion        */

int cdecl main(int argc,char *argv[])
{
    char *p;
    filelist *t;
    int i;

    mem_init();
    set_CMDLINELEN();

    /* Process switches from MAKEFLAGS environment variable     */
    p = getenv("MAKEFLAGS");
    if (p)
    {   char *p1,*p2,c;

        /* Create our own copy since we can't modify environment */
        p = mem_strdup(p);
        for (p1 = p; 1;)
        {   p1 = skipspace(p1);
            if (!*p1)
                break;
            p2 = p1;
            while (*p2 && !isspace(*p2))
                p2++;
            c = *p2;
            *p2 = 0;            /* terminate switch string */
            doswitch(p1);
            p1 = p2;
            if (c)
                p1++;
        }
    }

    if (response_expand(&argc,&argv))
        cmderr("can't expand response file\n");

    for (i = 1; i < argc; i++)          /* loop through arguments */
        doswitch(argv[i]);

    addmacro("**","$**",FALSE);
    addmacro("?","$?",FALSE);
    addmacro("*","$*",FALSE);
    addmacro("$","$$",FALSE);
    addmacro("@","$@",FALSE);
    addmacro("<","$<",FALSE);   /* so they expand safely        */

    readmakefile(makefile,NULL);
    do_implicits();

#ifdef DEBUG
    printf("***** FILES ******\n"); WRfilenodelist(filenodestart);
    printf("***** IMPLICITS *****\n"); WRimplicit();
    printf("***** TARGETS *****\n"); WRfilelist(targlist);
#endif

        /* Build each target    */
    for (t = targlist; t; t = t->next)
        if (t->fnode != dotdefault && !make(t->fnode))
        {   if (!question)
                printf("Target '%s' is up to date\n",t->fnode->name);
        }

#if TERMCODE
    /* Free everything up       */
    mem_free(p);
    mem_free(buf);
    freemacro();
    freefilelist(targlist);
    freeimplicits();
    freefilenode(filenodestart);
    mem_term();
    if (newcnt)
        printf("newcnt = %d\n",newcnt);
#endif
    if (question)
        exit(action);
    return EXIT_SUCCESS;
}

/***************************
 * Process switch p.
 */

doswitch(p)
char *p;
{
    if (*makefile == 0)
        /* Could have "-f filename"             */
        makefile = p;
    else if (*p == '-')                 /* if switch            */
    {   p++;
        switch (tolower(*p))
        {
            case 'd':
                debug = TRUE;
                break;
            case 'f':
                makefile = ++p;
                break;
            case 'i':
                ignore_errors = TRUE;
                break;
            case 'l':
                list_lines = TRUE;
                break;
            case 'm':
                if (p[1] == 'a' && p[2] == 'n' && p[3] == 0)
                {
                    browse("http://www.digitalmars.com/ctg/make.html");
                    exit(EXIT_SUCCESS);
                }
            case 'n':
                execute = FALSE;
                break;
            case 'p':
                print = TRUE;
                break;
            case 'q':
                question = TRUE;
                break;
            case 'r':
                usebuiltin = FALSE;
                break;
            case 's':
                gag = TRUE;
                break;
            case 't':
                touchem = TRUE;
                break;
            default:
                cmderr("undefined switch '%s'\n",--p);
        } /* switch */
    }
    else /* target or macro definition */
    {   char *text;

        text = strchr(p,'=');
        if (text)               /* it's a macro def */
        {   if (p == text)
                cmderr("bad macro definition '%s'\n",p);
            *text = 0;
            addmacro(p,text + 1,TRUE);
        }
        else
            addtofilelist(p,&targlist);
    }
}

/****************
 * Process command line error.
 */

void cmderr(format,arg)
char *format,*arg;
{
    printf("\
Digital Mars Make Version 5.06\n\
Copyright (C) Digital Mars 1985-2012.  All Rights Reserved.\n\
Written by Walter Bright  digitalmars.com\n\
Documentation: http://www.digitalmars.com/ctg/make.html\n\
\n\
        MAKE [-man] {target} {macro=text} {-dilnqst} [-fmakefile] {@file}\n\
\n\
@file   Get command args from environment or file\n\
target  What targets to make        macro=text  Define macro to be text\n\
-d      Output debugging info       -ffile      Use file instead of makefile\n\
-f-     Read makefile from stdin    -i  Ignore errors from executing make rules\n\
-l      List macro expansions       -n  Just echo rules that would be executed\n\
-q      If rules would be executed  -s  Do not echo make rules\n\
        then exit with errorlevel 1 -t  Just touch files\n\
-man    manual\n\
\n\
Predefined macros:\n\
        $$      Expand to $\n\
        $@      Full target name\n\
        $?      List of dependencies that are newer than target\n\
        $**     Full list of dependencies\n\
        $*      Name of current target without extension\n\
        $<      From name of current target, if made using an implicit rule\n\
Rule flags:\n\
        +       Force use of COMMAND.COM to execute rule\n\
        -       Ignore exit status\n\
        @       Do not echo rule\n\
        *       Can handle environment response files\n\
        ~       Force use of environment response file\
");
    printf("\nCommand error: ");
    printf(format,arg);
    exit(EXIT_FAILURE);
}

/*********************
 * Fatal error.
 */

void faterr(format,arg)
char *format,*arg;
{
    if (inreadmakefile)
        printf("Error on line %d: ",curline);
    else
        printf("Error: ");
    printf(format,arg);
    printf("\n");
    exit(EXIT_FAILURE);
}

/****************************
 * If system time is not known, get it.
 */

time_t getsystemtime()
{   time_t t;

#if __NT__
    time(&t);

    /* FAT systems get their file times rounded up to a 2 second
       boundary. So we round up system time to match.
     */
    return (t + 2) & ~1;
#else
    return time(&t);
#endif
}

/***************************
 * Set system time.
 */

void setsystemtime(datetime)
time_t datetime;
{
#if 0
    union REGS inregs, outregs;
    unsigned date,time;

    datetime -= TIMEOFFSET;
    time = datetime;
    date = datetime >> 16;
    inregs.h.ah = 0x2B;                 /* set date                     */
    inregs.x.cx = (date >> 9) + 1980;
    inregs.h.dh = (date >> 5) & 0xF;
    inregs.h.dl = date & 0x1F;
    intdos(&inregs,&outregs);
    inregs.h.ah = 0x2D;                 /* set time                     */
    inregs.h.dl = 0;
    inregs.h.dh = (time & 0x1F) << 1;
    inregs.h.cl = (time >> 5) & 0x3F;
    inregs.h.ch = time >> 11;
    intdos(&inregs,&outregs);
#endif
}

/********************************
 * Get file's date and time.
 * Return 1L if file doesn't exist.
 */

#if MSDOS
/* Swiped from stat.c */
static time_t near pascal _filetime(unsigned date,unsigned time)
{       time_t t;
        unsigned dd,mm,yy;
        static signed char adjust[12] =
        /*  J  F  M  A  M  J  J  A  S  O  N  D */
        /* 31 28 31 30 31 30 31 31 30 31 30 31 */
        {   0, 1,-1, 0, 0, 1, 1, 2, 3, 3, 4, 4 };

        /* Convert time to seconds since midnight       */
        t = ((time & 0x1F) * 2 +                        /* 2-second increments */
                ((time >> 5) & 0x3F) * 60) +            /* minutes      */
            (time_t) ((time >> 11) & 0x1F) * 3600;      /* hours        */
        /* Convert date to days since Jan 1, 1980       */
        dd = date & 0x1F;                       /* 1..31                */
        mm = ((date >> 5) & 0x0F) - 1;          /* 0..11                */
        yy = (date >> 9) & 0x7F;                /* 0..119 (1980-2099)   */
        date = dd + yy * 365 + mm * 30 + adjust[mm] +
                ((yy + 3) >> 2); /* add day for each previous leap year */
        if (mm <= 1 || yy & 3)                  /* if not a leap year   */
                date--;

        /* Combine date and time to get seconds since Jan 1, 1970       */
        return t + (time_t) date * (time_t) (60*60*24L) + TIMEOFFSET;
}
#endif

time_t gettimex(name)
char *name;
{   time_t datetime;
    time_t systemtime;

#if MSDOS
    struct FIND *find;

    find = findfirst(name,FA_DIREC | FA_SYSTEM | FA_HIDDEN);
    if (!find)
        return 1L;
    datetime = _filetime(find->date,find->time);
#else
    struct stat st;

    if (stat(name,&st) == -1)
        return 1L;
    datetime = st.st_mtime;
#endif

    debug2("Returning x%lx\n",datetime);
    systemtime = getsystemtime();
    if (datetime > systemtime)
#if 1
    {
        printf("File '%s' is newer than system time.\n",name);
        printf("File time = %ld, system time = %ld\n",datetime,systemtime);
        printf("File time = '%s'\n",ctime(&datetime));
        printf("Sys  time = '%s'\n",ctime(&systemtime));
    }
#else
    {   char c;

        printf("File '%s' is newer than system time. Fix system time (Y/N)? ",
                name);
        fflush(stdout);
        c = bdos(1);
        if (c == 'y' || c == 'Y')
                setsystemtime(datetime);
        fputc('\n',stdout);
    }
#endif
    return datetime;
}

/******************************
 * "Touch" a file, that is, give it the current date and time.
 * Returns:
 *      Time that was given to the file.
 */

time_t touch(name)
char *name;
{   time_t timep[2];

    printf("touch('%s')\n",name);
    time(&timep[1]);
    utime(name,timep);
    return timep[1];
}

/***************************
 * Do our version of the DEL command.
 */

void builtin_del(char *args)
{   struct FIND *f;
    char *pe;
    char c;

    while (1)
    {
        /* Find start of argument       */
        args = skipspace(args);
        if (!*args)
            break;

        /* Find end of argument         */
        pe = args + 1;
        while (*pe && !isspace(*pe))
            pe++;
        c = *pe;
        *pe = 0;

        /* args now points at 0-terminated argument     */
        f = findfirst(args,0);
        while (f)
        {   remove(f->name);
            f = findnext();
        }

        /* Point past argument for next one     */
        *pe = c;
        args = pe;
    }
}

/***************************
 * Do our version of the CD command.
 */

int builtin_cd(char *args)
{
    char *pe;
    char c;
    int i;

    /* Find start of argument   */
    args = skipspace(args);
    if (!*args)
        return 0;

    /* Find end of argument             */
    pe = args + 1;
    while (*pe && !isspace(*pe))
        pe++;
    c = *pe;
    *pe = 0;

    /* args now points at 0-terminated argument */
    i = _chdir(args);
    if (i)
        return 1;
    return 0;
}

/*********************
 * Read makefile and build data structures.
 */

linelist **readmakefile(char *makefile,linelist **rl)
{       FILE *f;
        char *line,*p,*q;
        linelist **addimplicit(),**targetline();
        int curlinesave = curline;

        if (!strcmp(makefile,"-"))
                f = stdin;              /* -f- means read from stdin    */
        else
                f = fopen(makefile,"r");
        if (!f)
                faterr("can't read makefile '%s'",makefile);
        inreadmakefile++;
        while (TRUE)
        {       if (readline(f))        /* read input line              */
                        break;          /* end of file                  */
                line = expandline(buf); /* expand macros                */
                p = line;
                if (isspace(*p))                /* rule line            */
                {
                    if (*skipspace(p) == 0)     /* if line is blank     */
                        mem_free(line);         /* ignore it            */
                    else
                    {
                        if (!rl)                /* no current target    */
                            faterr("target must appear before commands");
                        /* add line to current rule */
                        *rl = NEWOBJ(linelist);
                        (*rl)->line = line;
                        rl = &((*rl)->next);
                    }
                }
                else if (isimplicit(p))
                {       rl = addimplicit(p);
                        mem_free(line);
                }
                else    /* macro line or target line                    */
                {       char *pn;

                        pn = skipname(p);
                        p = skipspace(pn);
                        if (*p == '=')          /* it's a macro line    */
                        {       *pn = 0;
                                p = skipspace(p + 1);
                                addmacro(line,p,FALSE);
                        }
                        else if (!*p)           /* if end of line       */
                        {       *pn = 0;        /* delete trailing whitespace */
                                if (!strcmp(line,".SILENT"))
                                    gag = TRUE;
                                else if (!strcmp(line,".IGNORE"))
                                    ignore_errors = TRUE;
                                else
                                    faterr("unrecognized target '%s'",line);
                        }
                        else if (memcmp(line,"include",7) == 0)
                                rl = readmakefile(p,rl);
                        else                    /* target line          */
                                rl = targetline(line);
                        mem_free(line);
                }
        }
        curline = curlinesave;
        fclose(f);
        inreadmakefile--;
        return rl;
}

/*************************
 */

void addmacro(name,text,perm)
char *name,*text;
{       macro **mp;

#ifdef DEBUG
        printf("addmacro('%s','%s',%d)\n",name,text,perm);
#endif
        for (mp = &macrostart; *mp; mp = &((*mp)->next))
        {       if (!strcmp(name,(*mp)->name))  /* already in macro table */
                {       if ((*mp)->perm)        /* if permanent entry   */
                                return;         /* then don't change it */
                        mem_free((*mp)->text);
                        goto L1;
                }
        }
        *mp = NEWOBJ(macro);
        (*mp)->name = mem_strdup(name);
  L1:   (*mp)->text = mem_strdup(skipspace(text));
        (*mp)->perm = perm;
}

/*************************
 * Add target rule.
 * Return pointer to pointer to rule list.
 */

linelist **targetline(p)
char *p;
{       filelist *tlist,*tl;
        filenode *t;
        rule *r;
        int nintlist;                   /* # of files in tlist          */
        char *pend,c;
        char dblcln;

        debug2("targetline('%s')\n",p);
        tlist = NULL;                   /* so addtofilelist() will work */

        /* Pull out list of targets appearing before the ':'. Put them  */
        /* all in tlist.                                                */
        do
        {
                pend = skipname(p);
                if (p == pend || !*p)
                        faterr("expecting target : dependencies");

                /* Attempt to disambiguate :    */
                if (*(pend - 1) == ':')
                        pend--;

                c = *pend;
                *pend = 0;
                addtofilelist(p,&tlist);
                if (strcmp(p,".DEFAULT") == 0)
                    dotdefault = findfile(p,TRUE);
                /*printf("adding '%s' to tlist\n",p);*/
                *pend = c;
                p = skipspace(pend);
        } while (*p != ':');
        p++;                            /* skip over ':'                */
        dblcln = 1;
        if (*p == ':')                  /* if :: dependency             */
        {   dblcln++;
            p++;
        }

        r = NEWOBJ(rule);
        for (tl = tlist; tl; tl = tl->next)
        {       t = tl->fnode;          /* for each target t in tlist   */
                t->dblcln = dblcln;
                if (t->frule)           /* if already got rules         */
                {   /*faterr("already have rules for %s\n",t->name);*/
                    freerule(t->frule); /* dump them                    */
                }
                t->frule = r;           /* point at rule                */
                r->count++;             /* count how many point at this */
        }

        /* for each dependency broken out */
        p = skipspace(p);
        while (*p && *p != ';')
        {
                pend = skipname(p);
                if (p == pend)
                        faterr("'%c' is not a valid filename char",*p);
                c = *pend;
                *pend = 0;
                for (tl = tlist; tl; tl = tl->next)
                {       t = tl->fnode;  /* for each target t in tlist   */
                        /* add this dependency to its dependency list   */
                        addtofilelist(p,&(t->dep));
                        /*printf("Adding dep '%s' to file '%s'\n",p,t->name);*/
                }
                *pend = c;
                p = skipspace(pend);
        }
        if (!targlist &&                /* if we don't already have one */
            (tlist->next || tlist->fnode != dotdefault)
           )
                targlist = tlist;       /* use the first one we found   */
        else
        {       debug2("freefilelist(%p)\n",tlist);
                freefilelist(tlist);    /* else dump it                 */
        }
        if (*p == ';')
        {
            p = skipspace(p + 1);
            if (*p)
            {   r->rulelist = NEWOBJ(linelist);
                r->rulelist->line = mem_strdup(p);
                return (&r->rulelist->next);
            }
        }
        return &(r->rulelist);
}

/***********************
 * Determine if line p is an implicit rule.
 */

int isimplicit(p)
char *p;
{
    char *q;

    if (*p == '.' &&
        isfchar(p[1]) &&
        (q = strchr(p+2,'.')) != NULL &&
        strchr(p,':') > q)      /* implicit line        */

        return TRUE;
    else
        return FALSE;
}

/*************************
 * Add implicit rule.
 * Return pointer to pointer to rule list.
 */

linelist **addimplicit(p)
char *p;
{
    implicit *g,**pg,*gr;
    rule *r;
    char *pend,c;

    debug2("addimplicit('%s')\n",p);
    pg = &implicitstart;
    r = NEWOBJ(rule);
    do
    {
        while (*pg)
            pg = &((*pg)->next);

        g = *pg = NEWOBJ(implicit);
        g->grule = r;
        r->count++;

        /* Get fromext[]        */
        pend = ++p;                     /* skip over .                  */
        while (isfchar(*pend))
                pend++;
        if (p == pend) goto err;
        c = *pend;
        *pend = 0;
        if (strlen(p) > EXTMAX) goto err;
        strcpy(g->fromext,p);
        *pend = c;
        p = skipspace(pend);

        /* Get toext[]          */
        if (*p++ != '.') goto err;
        pend = p;
        while (isfchar(*pend))
                pend++;
        if (p == pend) goto err;
        c = *pend;
        *pend = 0;
        if (strlen(p) > EXTMAX) goto err;
        strcpy(g->toext,p);
        *pend = c;
        p = skipspace(pend);

        /* See if it's already in the list      */
        for (gr = implicitstart; gr != g; gr = gr->next)
                if (!filenamecmp(gr->fromext,g->fromext) &&
                    !filenamecmp(gr->toext,g->toext))
                        faterr("ambiguous implicit rule");

#ifdef DEBUG
        printf("adding implicit rule from '%s' to '%s'\n",
                g->fromext,g->toext);
#endif

    } while (*p == '.');
    if (*p != ':')
        goto err;
    /* Rest of line must be blank       */
    p = skipspace(p + 1);
    if (*p == ';')              /* rest of line is a rule line          */
    {
        p = skipspace(p + 1);
        if (*p)
        {   r->rulelist = NEWOBJ(linelist);
            r->rulelist->line = mem_strdup(p);
            return (&r->rulelist->next);
        }
    }
    if (*p)
        goto err;
    return &(r->rulelist);

err:
    faterr("bad syntax for implicit rule, should be .frm.to:");
}

/*************************
 * Read line from file f into buf.
 * Remove comments at this point.
 * Remove trailing whitespace from line.
 * Returns:
 *      TRUE if end of file
 */

int readline(FILE *f)
{       int i,c;

        i = 0;
        again:
        do
        {
                L0:
            while (TRUE)
            {
                        if (i >= bufmax)
                        {   bufmax += 100;
                                buf = mem_realloc(buf,bufmax);
                        }
                        do
                        {
                                c = fgetc(f);   /* read char from file          */
                        } while(c == '\r');     /* ignoring CRs                 */
                        L1:
                        if (c == '#')
                        {
                                /* If this immediately follows an escape character ... */
                                if (i && (buf[i - 1] == ESC || buf[i - 1] == '\\'))
                                {
                                        /* ... then pretend that the # has not happened; */
                                        i--;
                                }
                                else
                                {
                                        int     cLast   =       -1;

                                        /* ... otherwise ignore everything until the end of line */
                                        for(;;)
                                        {
                                                        c = fgetc(f);

                                                if(c == '\n' || c == EOF)
                                                {
                                                        break;
                                                }

                                                cLast = c;
                                        }

                                        if(c == '\n')
                                        {
                                                curline++;

                                                /* Only continue as part of next line if the last character
                                                 * in this (comment) line is a continuation. In effect, this
                                                 * allows the abc#xyz\ sequence to denote a slice out of a
                                                 * (multi-)line which translates to abc\, which is what we're
                                                 * after in all circumstances.
                                                 *
                                                 * Hence, to ignore a line within a continued sequence of lines
                                                 * the commented line(s) must be terminated with a trailing
                                                 * continuation.
                                                 */
                                                if(cLast == ESC || cLast == '\\')
                                                {
                                                        goto L0;        /* This could be "continue", but not proof against future maintenance */
                                                }
                                        }
                                }
                        }
                        if (c == EOF)
                                break;
                        if (c == '\n')  /* if end of input line         */
                        {
                                curline++;
                                /* If the last character is line continuation, or escape ... */
                                if (i && (buf[i - 1] == ESC || buf[i - 1] == '\\'))
                                {
                                        /* ... then change it to a space and ignore any subsequent
                                         * whitespace.
                                         */
                                        buf[i - 1] = ' ';
                                        do
                                        {
                                                c = fgetc(f);
                                        } while (c == ' ' || c == '\t');
                                        goto L1;        /* line continuation            */
                                }
                                /* A complete line is aquired, so break out */
                                break;
                        }

                        buf[i++] = c;
            }
        } while (i == 0 && c != EOF);   /* if 0 length line             */
        buf[i] = 0;                     /* terminate string             */

        debug3("[%d:%s]\n", curline, buf);

        return (c == EOF);
}

/*******************
 * Add filename to end of file list.
 */

void addtofilelist(filename,pflist)
char *filename;
filelist **pflist;
{       filelist **pfl,*fl;

        for (pfl = pflist; *pfl; pfl = &((*pfl)->next))
        {       if (!filenamecmp(filename,(*pfl)->fnode->name))
                        return;         /* if already in list           */
        }
        fl = NEWOBJ(filelist);
        *pfl = fl;
        fl->fnode = findfile(filename,TRUE);
}

/*****************
 * Find filename in file list.
 * If it isn't there and install is TRUE, install it.
 */

filenode *findfile(filename,install)
char *filename;
int install;
{       filenode **pfn;

        /*debug2("findfile('%s')\n",filename);*/
        for (pfn = &filenodestart; *pfn; pfn = &((*pfn)->next))
        {       if (!filenamecmp((*pfn)->name,filename))
                        return *pfn;
        }
        if (install)
        {
            *pfn = NEWOBJ(filenode);
            (*pfn)->name = mem_strdup(filename);
        }
        return *pfn;
}

/************************
 * Perform macro expansion on the line pointed to by buf.
 * Return pointer to created string.
 */

char *expandline(char *buf)
{
    unsigned i;                 /* where in buf we have expanded up to  */
    unsigned b;                 /* start of macro name                  */
    unsigned t;                 /* start of text following macro call   */
    unsigned p;                 /* 1 past end of macro name             */
    unsigned textlen;           /* length of replacement text (excl. 0) */
    unsigned buflen;            /* length of buffer (excluding 0)       */
    int paren;
    char c,*text;

    debug2("expandline('%s')\n",buf);
    buf = mem_strdup(buf);
    i = 0;
    while (buf[i])
    {   if (buf[i] == '$')      /* if start of macro            */
        {   b = i + 1;
            if (buf[b] == '(')
            {   paren = TRUE;
                b++;
                p = b;
                while (buf[p] != ')')
                        if (!buf[p++])
                                faterr("')' expected");
                t = p + 1;
            }
            else
            {   paren = FALSE;
                /* Special case to recognize $** */
                p = b + 1;
                if (buf[b] == '*' && buf[p] == '*')
                        p++;
                t = p;
            }
            c = buf[p];
            buf[p] = 0;
            text = searchformacro(buf + b);
            buf[p] = c;
            textlen = strlen(text);
            /* If replacement text exactly matches macro call, skip expansion */
            if (textlen == t - i && strncmp(text,buf + i,t - i) == 0)
                i = t;
            else
            {
                buflen = strlen(buf);
                buf = mem_realloc(buf,buflen + textlen + 1);
                memmove(buf + i + textlen,buf + t,buflen + 1 - t);
                memmove(buf + i,text,textlen);
                if (textlen == 1 && *text == '$')
                    i++;
            }
        }
        else
            i++;
    }
    if (list_lines && inreadmakefile)
        printf("%s\n",buf);
    return buf;
}

/*********************
 * Search for macro.
 */

char *searchformacro(char *name)
{       macro *m;
        char *envstring;

        for (m = macrostart; m; m = m->next)
                if (!strcmp(name,m->name))
                        return m->text;

        envstring = getenv(name);
        if (envstring)
            return envstring;

        // Maybe it's a file
        {
            FILE *f;

            f = fopen(name, "r");
            if (f)
            {   char *bufsave = buf;
                int bufmaxsave = bufmax;
                int curlinesave = curline;
                char *p;

                buf = NULL;
                bufmax = 0;
                curline = 0;

                readline(f);    // BUG: should check for multiple lines instead of ignoring them
                p = buf;

                buf = bufsave;
                bufmax = bufmaxsave;
                curline = curlinesave;

                fclose(f);
                return p;
            }
        }

        return "";
}

/********************
 * Skip spaces.
 */

char *skipspace(p)
char *p;
{
        while (isspace(*p))
                p++;
        return p;
}

/********************
 * Skip file names.
 */

char *skipname(p)
char *p;
{       char *pstart = p;

        while (ispchar(*p))
        {
                /* for strings like "h: " or "abc:", do not regard the  */
                /* : as part of the filename                            */
                if (*p == ':' && ((p - pstart) != 1 || isspace(p[1])))
                        break;
                p++;
        }
        return p;
}

/********************
 * Return pointer to extension in name (the .).
 * If no extension, return pointer to trailing 0.
 */

char *filespecdotext(p)
char *p;
{       char *s;

        s = p + strlen(p);
        while (*s != '\\' && *s != ':' && *s != '/')
        {       if (*s == '.')
                        return s;
                if (s == p)
                        break;
                s--;
        }
        return p + strlen(p);
}

/***********************
 * Return TRUE if char is a file name character.
 */

int isfchar(c)
char c;
{
    return isalnum(c) || c == '_' || c == '-';
}

/***********************
 * Return TRUE if char is a file name character, including path separators
 * and .s
 */

int ispchar(c)
char c;
{
    return isfchar(c) || c == ':' || c == '/' || c == '\\' || c == '.';
}

/***********************
 * Add extension to filename.
 * Delete old extension if there was one.
 */

char *filespecforceext(name,ext)
char *name,*ext;
{       char *newname,*p;

        newname = mem_calloc(strlen(name) + strlen(ext) + 1 + 1);
        strcpy(newname,name);
        p = filespecdotext(newname);
        *p++ = '.';
        strcpy(p,ext);
        p = mem_strdup(newname);
        mem_free(newname);
        return p;
}

/***********************
 * Get root name of file name.
 */

char *filespecgetroot(name)
char *name;
{       char *root,*p,c;

        p = filespecdotext(name);
        c = *p;
        *p = 0;
        root = mem_strdup(name);
        *p = c;
        return root;
}

/*******************************
 * Look through files. If any have no rules, see if we can
 * apply an implicit rule.
 */

do_implicits()
{       filenode *f;
        char *ext,*depname;
        implicit *g;
        time_t time;

        for (f = filenodestart; f; f = f->next)
        {       if (f->frule)
                {       if (f->frule->rulelist) /* if already have rules */
                                continue;
                        freerule(f->frule);
                        f->frule = NULL;
                }
                ext = filespecdotext(f->name);
                if (*ext == '.')
                        ext++;
                for (g = implicitstart; g; g = g->next)
                {   if (!filenamecmp(ext,g->toext))
                    {   filenode *fd;

                        strcpy(f->genext,g->fromext);
                        depname = filespecforceext(f->name,f->genext);
                        time = gettimex(depname);
                        if (time == 1L) /* if file doesn't exist */
                        {   fd = findfile(depname,FALSE);
                            if (!fd)
                            {
                                mem_free(depname);
                                continue;
                            }
                        }
                        else
                            fd = findfile(depname,TRUE);

                        fd->time = time;
                        f->frule = g->grule;
                        f->frule->count++;
                        addtofilelist(depname,&(f->dep));
                        mem_free(depname);
                        break;
                    }
                }
#if 0
                if (!g && dotdefault)   /* if failed to find implicit rule */
                {   /* Use default rule */
                    f->frule = dotdefault->frule;
                    f->frule->count++;
                }
#endif
        }
}

/***************************
 * Make a file. Return TRUE if rules were executed.
 */

int make(f)
filenode *f;
{       int made = FALSE;
        int gooddate = FALSE;
        filelist *dl;                   /* dependency list              */
        filelist *newer;
        char *newbuf;
        int totlength,starstarlen;

        if (f->expanding)
            faterr("circular dependency for '%s'",f->name);
        debug2("make('%s')\n",f->name);
        dl = f->dep;

        addmacro("$","$",FALSE);
        addmacro("?","",FALSE);         /* the default                  */
        addmacro("**","",FALSE);
        if (!f->frule || !f->frule->rulelist)   /* if no make rules     */
        {   if (f->time <= 1)
                f->time = gettimex(f->name);
            if (!dl)                            /* if no dependencies   */
            {   if (f->time == 1)               /* if file doesn't exist */
                {
                    if (dotdefault && dotdefault->frule &&
                        dotdefault->frule->rulelist)
                    {
                        f->frule = dotdefault->frule;
                        f->frule->count++;
                        return dorules(f);
                    }
                    else
                        faterr("don't know how to make '%s'",f->name);
                }
                return FALSE;
            }
        }

        if (!dl)                        /* if no dependencies           */
                return dorules(f);      /* execute rules                */

        /* Make each dependency, also compute length of $** expansion   */
        starstarlen = 0;
        f->expanding++;
        for (; dl; dl = dl->next)
        {       made |= make(dl->fnode);
                starstarlen += strlen(dl->fnode->name) + 1;
        }
        f->expanding--;

        newbuf = mem_calloc(starstarlen + 1);
        *newbuf = 0;                    /* initial 0 length string      */

        /* If there are any newer dependencies, we must remake this one */
        newer = NULL;
        totlength = 0;
        for (dl = f->dep; dl; dl = dl->next)
        {       if (!dl->fnode->time)
                        dl->fnode->time = gettimex(dl->fnode->name);
                strcat(newbuf,dl->fnode->name);
                strcat(newbuf," ");
            L1:
                if (f->time < dl->fnode->time)
                {       if (!gooddate)  /* if date isn't guaranteed     */
                        {   f->time = gettimex(f->name);
                            gooddate = TRUE;
                            goto L1;
                        }
                        if (debug)
                        {   if (f->time == 1L)
                                printf("File '%s' doesn't exist\n",f->name);
                            else
                                printf("file '%s' is older than '%s'\n",
                                        f->name,dl->fnode->name);
                        }
                        /* still out of date    */
                        addtofilelist(dl->fnode->name,&newer);
                        totlength += strlen(dl->fnode->name) + 1;
                }
        }
        addmacro("**",newbuf,FALSE);    /* full list of dependencies    */
        mem_free(newbuf);

        if (newer)                      /* if any newer dependencies    */
        {       filelist *fl;

                newbuf = mem_calloc(totlength + 1);
                *newbuf = 0;            /* initial 0 length string      */
                for (fl = newer; fl; fl = fl->next)
                {       strcat(newbuf,fl->fnode->name);
                        strcat(newbuf," ");
                }
                addmacro("?",newbuf,FALSE);     /* newer dependencies   */
                mem_free(newbuf);
                freefilelist(newer);
                return made | dorules(f);
        }
        if (!gooddate)
                f->time = gettimex(f->name);
        return made;
}

/********************************
 * Execute rules for a filenode.
 * Return TRUE if we executed some rules.
 */

int dorules(f)
filenode *f;
{       char *root,*fromname;
        linelist *l;

        debug2("dorules('%s')\n",f->name);
        if (!f->frule)
                return FALSE;
        if (touchem)
        {       f->time = touch(f->name);
                return TRUE;            /* assume rules were executed   */
        }
        root = filespecgetroot(f->name);
        fromname = filespecforceext(root,f->genext);
        addmacro("*",root,FALSE);
        addmacro("<",fromname,FALSE);
        addmacro("@",f->name,FALSE);
        mem_free(root);
        mem_free(fromname);
        for (l = f->frule->rulelist; l; l = l->next)
                executerule(l->line);
        f->time = gettimex(f->name);
        return TRUE;
}

/******************************
 * Determine if filename p is in the array of strings.
 */

int inarray(char *p, char **array, size_t dim)
{
    char **b;
    int result = 0;

    for (b = array; b < &array[dim]; b++)
        if (!filenamecmp(p,*b))
        {
                result = 1;
                break;
        }
    return result;
}

/******************************
 * Execute a rule.
 */

void executerule(p)
char *p;
{       char echo = TRUE;
        char igerr = FALSE;
        char useCOMMAND = FALSE;
        char useenv = 0;
        char forceuseenv = 0;
        char *cmd,*args,c;

        debug2("executerule('%s')\n",p);
        if (question)
        {       action = 1;     /* file is not up to date               */
                return;
        }
        p = skipspace(p);
        while (1)
        {   switch (*p)
            {   case '+':       useCOMMAND = TRUE;      p++; continue;
                case '@':       echo = FALSE;           p++; continue;
                case '-':       igerr = TRUE;           p++; continue;
                case '*':       useenv = '@';           p++; continue;
                case '~':       forceuseenv = TRUE;
                                useenv = '@';           p++; continue;
                default:
                    break;
            }
            break;
        }
        p = skipspace(p);
        p = expandline(p);              /* expand any macros            */

        if (echo && !gag)               /* if not suppressed            */
                printf("%s\n",p);       /* print the line               */
        fflush(stdout);                 /* update all output before fork */
        cmd = skipspace(p);
        if (!execute || !*cmd)          /* if execution turned off or   */
                                        /* a blank command              */
        {       mem_free(p);
                return;
        }

#if MSDOS || __OS2__ || _WIN32
        /* Separate between command and args                            */
        bool quoted = false;
        char *cmdstart = cmd;
        while (!isspace(*cmd) && *cmd)
        {   if (*cmd == '"')
            {
                quoted = true;
                break;
            }
            cmd++;
        }

        // Handle quoted command
        if (quoted)
        {   cmd = cmdstart;
            char *q = cmd;
            quoted = false;
            while (*cmd)
            {
                if (quoted)
                {
                    if (*cmd == '"')
                        quoted = false;
                    else
                        *q++ = *cmd;
                }
                else if (isspace(*cmd))
                    break;
                else if (*cmd == '"')
                    quoted = true;
                else
                    *q++ = *cmd;
                ++cmd;
            }
            *q = 0;
        }

        args = (*cmd) ? cmd + 1 : cmd;
        c = *cmd;
        *cmd = 0;

        /* Handle commands we know about special        */
        {
            static char *builtin[] =    /* MS-DOS built-in commands     */
            {   "break","cls","copy","ctty",
                "date","dir","echo","erase","exit",
                "for","goto","if","md","mkdir","pause",
                "rd","rem","rmdir","ren","rename",
                "shift","time","type","ver","verify","vol"
            };

            static char *respenv[] =    /* uses our response files      */
            {   "ztc","make","touch","blink","blinkr","blinkx",
                "zorlib","zorlibx","lib","zrcc","sc","sj",
                "dmc","dmd"
            };

            static char *pharrespenv[] = /* uses pharlap response files */
            {   "386asm","386asmr","386link","386linkr","386lib","fastlink",
                "bind386",
            };

            useCOMMAND  |= inarray(p,builtin,arraysize(builtin));
            if (inarray(p,respenv,arraysize(respenv)))
                useenv = '@';
            if (inarray(p,pharrespenv,arraysize(pharrespenv)))
                useenv = '%';
        }

        /* Use COMMAND.COM for .BAT files       */
        if (!filenamecmp(filespecdotext(p),".bat"))
            useCOMMAND |= TRUE;

        if (useCOMMAND)
        {
            Lcmd:
                *cmd = c;               /* restore stomped character    */
                if (strlen(p) > CMDLINELEN - 2) /* -2 for /c switch     */
                        faterr("command line too long");
                debug1("Using COMMAND.COM\n");
                system(p);
        }
        else if (!filenamecmp(p,"del"))
        {
                if (strchr(args,'\\'))
                {   useCOMMAND = TRUE;
                    goto Lcmd;
                }
                builtin_del(args);
        }
        else if (!filenamecmp(p,"cd"))
        {       int status;

                status = builtin_cd(args);
                if (status && !ignore_errors && !igerr)
                {       printf("--- errorlevel %d\n",status);
                        exit((status & 0xFF) ? status : 0xFF);
                }
        }
        else if (!filenamecmp(p,"set"))
        {       char *q;

                for (q = args; *q && *q != '='; q++)
                    *q = toupper(*q);           /* convert env var to uc */

                if (putenv(args))               /* set environment      */
                    faterr("out of memory");
        }
        else
#endif
        {       int status;
                size_t len;

                debug1("Using FORK\n");
#if MSDOS || __OS2__ || _WIN32
                if (forceuseenv || (len = strlen(args)) > CMDLINELEN)
                {   char *q;
                    static char envname[] = "@_CMDLINE";

                    if (!useenv)
                        goto L1;
                    envname[0] = useenv;
                    q = (char *) mem_calloc(sizeof(envname) + len);
                    sprintf(q,"%s=%s",envname + 1,args);
                    status = putenv(q);
                    mem_free(q);
                    if (status == 0)
                        args = envname;
                    else
                    {
                     L1:
                        faterr("command line too long");
                    }
                }
#endif
#if 1
#if M_UNIX || M_XENIX
                status = system(p);
#else
                status = spawnlp(0,p,p,args,NULL);
#endif
                if (status == -1)
                        faterr("'%s' not found",p);
#else
                status = forklp(p,p,"",args,NULL);
                if (status)                     /* if error             */
                        faterr("'%s' not found",p);
                else
                        status = wait();        /* exit code of p       */
#endif
                printf("\n");
                if (status && !ignore_errors && !igerr)
                {       printf("--- errorlevel %d\n",status);
                        exit((status & 0xFF) ? status : 0xFF);
                }
        }
        mem_free(p);
}

/****************************************
 * Get max command line length
 */

void set_CMDLINELEN()
{
   /* See also:
    * http://msdn.microsoft.com/library/default.asp?url=/library/en-us/win9x/msdos_694j.asp
    */

#if _WIN32
    OSVERSIONINFO OsVerInfo;

    OsVerInfo.dwOSVersionInfoSize = sizeof(OsVerInfo);
    GetVersionEx(&OsVerInfo);
    CMDLINELEN = 10000;
    switch (OsVerInfo.dwMajorVersion)
    {
        case 3: // NT 3.51
            CMDLINELEN = 996;
            break;

        case 4: // Windows 95, 98, Me, NT 4.0
        case 5: // 2000, XP, Server 2003 family
        default:
            if (OsVerInfo.dwPlatformId == VER_PLATFORM_WIN32_WINDOWS)
            {   // Windows 95/98/Me
                CMDLINELEN = 1024;
            }
            break;
    }
#endif
#if __OS2__
    CMDLINELEN = 255;
#endif
#if MSDOS
    CMDLINELEN = 127;
#endif
}

/****************** STORAGE MANAGEMENT *****************/

#if 1

/*******************
 */

void *mem_calloc(unsigned size)
{ char *p;

/*  debug2("size = %d\n",size);*/
  newcnt++;
  p = calloc(size,1);
  if (!p)
        faterr("out of memory");
/*  debug2("mem_calloc() = %p\n",p);*/
  return p;
}

/*******************
 */

void mem_free(void *p)
{
    /*debug2("mem_free(%p)\n",p);*/
    if (p)
    {   free(p);
        newcnt--;
    }
}

/********************
 * Re-allocate a buffer.
 */

void *mem_realloc(void *oldbuf,unsigned newbufsize)
{
        char *p;

        if (!oldbuf)
            newcnt++;
        p = realloc(oldbuf,newbufsize);
        if (p == NULL)
            faterr("out of memory");
        return p;
}

/******************
 * Save string in new'ed memory and return a pointer to it.
 */

char *mem_strdup(const char *s)
{
    return strcpy(mem_calloc(strlen(s) + 1),s);
}

#endif

#if TERMCODE
void freemacro()
{       macro *m,*mn;

        for (m = macrostart; m; m = mn)
        {       mn = m->next;
                mem_free(m->name);
                mem_free(m->text);
                mem_free(m);
        }
}
#endif

void freefilelist(fl)
filelist *fl;
{       filelist *fln;

        for (; fl; fl = fln)
        {       fln = fl->next;
                mem_free(fl);
        }
}

#if TERMCODE

freeimplicits()
{       implicit *g,*gn;

        for (g = implicitstart; g; g = gn)
        {       gn = g->next;
                freerule(g->grule);
                mem_free(g);
        }
}

void freefilenode(f)
filenode *f;
{       filenode *fn;

        for (; f; f = fn)
        {       fn = f->next;
                mem_free(f->name);
                freefilelist(f->dep);
                freerule(f->frule);
                mem_free(f);
        }
}

#endif

void freerule(r)
rule *r;
{       linelist *l,*ln;

        if (!r || --r->count)
                return;
        for (l = r->rulelist; l; l = ln)
        {       ln = l->next;
                mem_free(l->line);
                mem_free(l);
        }
        mem_free(r);
}

/****************** DEBUG CODE *************************/

#ifdef DEBUG

void WRmacro(m)
macro *m;
{
    printf("macro %p: perm %d next %p name '%s' = '%s'\n",
                m,m->perm,m->next,m->name,m->text);
}

void WRmacrolist()
{       macro *m;

        printf("****** MACRO LIST ********\n");
        for (m = macrostart; m; m = m->next)
                WRmacro(m);
}

void WRlinelist(l)
linelist *l;
{
        while (l)
        {       printf("line %p next %p: '%s'\n",l,l->next,l->line);
                l = l->next;
        }
}

void WRrule(r)
rule *r;
{
        printf("rule %p: count = %d\n",r,r->count);
        WRlinelist(r->rulelist);
}

void WRimplicit()
{       implicit *g;

        for (g = implicitstart; g; g = g->next)
        {       printf("implicit %p next %p: from '%s' to '%s'\n",
                        g,g->next,g->fromext,g->toext);
                WRrule(g->grule);
                putchar('\n');
        }
}

void WRfilenode(f)
filenode *f;
{       filelist *fl;

        printf("filenode %p: name '%s' genext '%s' time %ld\n",
                f,f->name,f->genext,f->time);
        printf("Dependency list:\n");
        for (fl = f->dep; fl; fl = fl->next)
                printf("\t%s\n",fl->fnode->name);
        if (f->frule)
                WRrule(f->frule);
        putchar('\n');
}

void WRfilelist(fl)
filelist *fl;
{
        for (; fl; fl = fl->next)
                WRfilenode(fl->fnode);
}

void WRfilenodelist(fn)
filenode *fn;
{
        for (; fn; fn = fn->next)
                WRfilenode(fn);
}

#endif
