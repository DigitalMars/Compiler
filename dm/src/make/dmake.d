/*_ make.d   */
/* Copyright (C) 1985-2018 by Walter Bright     */
/* All rights reserved                          */
/* Written by Walter Bright                     */

/*
 * To build for Win32:
 *      dmd dmake dman -O -release -inline
 */

import core.stdc.ctype;
import core.stdc.stdio;
import core.stdc.stdlib;
import core.stdc.string;
import core.stdc.time;
import core.sys.windows.stat;
//import core.stdc.process;
//import core.stdc.direct;

version (Windows)
{
    import core.sys.windows.windows;
}

import man;

extern (C):

int stricmp(const(char)*, const(char)*) pure nothrow @nogc;
extern (Pascal) int response_expand(int*, char ***);
int chdir(const char *);
int putenv(const char *);
int spawnlp(int, const char*, const char *, ...);
int utime(const char *, time_t*);

struct Stat
{
  align (2):
    short       st_dev;
    ushort      st_ino;
    ushort st_mode;
    short       st_nlink;
    ushort      st_uid;
    ushort      st_gid;
    short       st_rdev;
    short       dummy;                  /* for alignment                */
    int st_size;
    int st_atime;
    int st_mtime;
    int st_ctime;
}
int stat(const(char)*, Stat*);

struct FINDA
{
  align (1):
    char[21] reserved;
    char attribute;
    ushort time,date;
    uint size;
    char[260] name;
}
FINDA* findfirst(const char*, int);
FINDA* findnext();

enum ESC =      '!';            // our escape character

type* NEWOBJ(type)() { return cast(type*) mem_calloc(type.sizeof); }


/* File name comparison is case-insensitive on some systems     */

version (Windows)
    int filenamecmp(const(char)* s1, const(char)* s2) { return stricmp(s1, s2); }
else
    int filenamecmp(const(char)* s1, const(char)* s2) { return strcmp(s1, s2); }


/* Length of command line
 */

__gshared int CMDLINELEN;
void set_CMDLINELEN();

enum EXTMAX = 5;


/*************************
 * List of macro names and replacement text.
 *      name    macro name
 *      perm    if true, then macro cannot be replaced
 *      text    replacement text
 *      next    next macro in list
 */

struct MACRO
{
    char* name,text;
    int perm;
    MACRO *next;
}

/*************************
 * List of files
 */

struct FILELIST
{
    FILENODE *fnode;
    FILELIST *next;
}

alias filelist = FILELIST;

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

struct FILENODE
{
        char            *name;
        char[EXTMAX+1]  genext;
        char            dblcln;
        char            expanding;
        time_t          time;
        filelist        *dep;
        RULE            *frule;
        FILENODE        *next;
}

alias filenode = FILENODE;

/*************************
 * Implicit rule.
 *      fromext         starting extension
 *      toext           generated extension
 *      grule           creation rules
 *      next            next in list
 */

struct IMPLICIT
{
        char[EXTMAX+1]  fromext;
        char[EXTMAX+1]  toext;
        RULE            *grule;
        IMPLICIT        *next;
}

alias implicit = IMPLICIT;

/*************************
 * Make rules.
 * Multiple people can point to one instance of this.
 *      count           # of parents of this
 *      gener           true if this is an implicit rule
 *      rulelist        list of rules
 */

struct RULE
{
        int count;
        int gener;
        LINELIST *rulelist;
}

alias rule = RULE;

/*************************
 * List of lines
 */

struct LINELIST
{
        char *line;
        LINELIST *next;
}

alias linelist = LINELIST;

/********************** Global Variables *******************/

__gshared
{
    bool ignore_errors = false;         /* if true then ignore errors from rules */
    bool execute = true;                /* if false then rules aren't executed  */
    bool gag = false;                   /* if true then don't echo commands     */
    bool touchem = false;               /* if true then just touch targets      */
    bool xdebug = false;                /* if true then output debugging info   */
    bool list_lines = false;            /* if true then show expanded lines     */
    bool usebuiltin = true;             /* if true then use builtin rules       */
    bool print = false;                 /* if true then print complete set of   */
                                        /* macro definitions and target desc.   */
    bool question = false;              /* exit(0) if file is up to date,       */
                                        /* else exit(1)                         */
    bool action = false;                /* 1 if rules were executed             */
    const(char)* makefile = "makefile"; /* default makefile                     */

    filenode *filenodestart = null;     /* list of all files            */
    filelist *targlist = null;          /* main target list             */
    implicit  *implicitstart = null;    /* list of implicits            */
    MACRO *macrostart = null;           /* list of macros               */

    filenode *dotdefault = null;        /* .DEFAULT rule                */

    char *buf = null;                   /* input line buffer                    */
    int bufmax = 0;                     /* max size of line buffer              */
    int curline = 0;                    /* makefile line counter                */

    int inreadmakefile = 0;             /* if reading makefile                  */
    int newcnt = 0;                     /* # of new'ed items                    */
}

/***********************
 */

//_WILDCARDS;                   /* do wildcard expansion        */

int main(int argc,char** argv)
{
    char *p;
    filelist *t;
    int i;

    //mem_init();
    set_CMDLINELEN();

    /* Process switches from MAKEFLAGS environment variable     */
    p = getenv("MAKEFLAGS");
    if (p)
    {   char* p1,p2;
        char c;

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

version (Win32)
{
    if (response_expand(&argc,&argv))
        cmderr("can't expand response file\n", null);
}

    for (i = 1; i < argc; i++)          /* loop through arguments */
        doswitch(argv[i]);

    addmacro("**","$**",false);
    addmacro("?","$?",false);
    addmacro("*","$*",false);
    addmacro("$","$$",false);
    addmacro("@","$@",false);
    addmacro("<","$<",false);   /* so they expand safely        */

    readmakefile(makefile,null);
    do_implicits();

    debug
    {
        printf("***** FILES ******\n"); WRfilenodelist(filenodestart);
        printf("***** IMPLICITS *****\n"); WRimplicit();
        printf("***** TARGETS *****\n"); WRfilelist(targlist);
    }

    /* Build each target        */
    for (t = targlist; t; t = t.next)
        if (t.fnode != dotdefault && !make(t.fnode))
        {   if (!question)
                printf("Target '%s' is up to date\n",t.fnode.name);
        }

    version (TERMCODE)
    {
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
    }
    if (question)
        exit(action);
    return EXIT_SUCCESS;
}

/***************************
 * Process switch p.
 */

void doswitch(char* p)
{
    if (*makefile == 0)
        /* Could have "-f filename"             */
        makefile = p;
    else if (*p == '-')                 /* if switch            */
    {   p++;
        switch (tolower(*p))
        {
            case 'd':
                xdebug = true;
                break;
            case 'f':
                makefile = ++p;
                break;
            case 'i':
                ignore_errors = true;
                break;
            case 'l':
                list_lines = true;
                break;
            case 'm':
                if (p[1] == 'a' && p[2] == 'n' && p[3] == 0)
                {
                    browse("http://www.digitalmars.com/ctg/make.html");
                    exit(EXIT_SUCCESS);
                }
                execute = false;
                break;
            case 'n':
                execute = false;
                break;
            case 'p':
                print = true;
                break;
            case 'q':
                question = true;
                break;
            case 'r':
                usebuiltin = false;
                break;
            case 's':
                gag = true;
                break;
            case 't':
                touchem = true;
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
            addmacro(p,text + 1,true);
        }
        else
            addtofilelist(p,&targlist);
    }
}

/****************
 * Process command line error.
 */

void cmderr(const char* format, const char* arg)
{
    printf(
"Digital Mars Make Version 6.00
Copyright (C) Digital Mars 1985-2018.  All Rights Reserved.
Written by Walter Bright  digitalmars.com
Documentation: http://www.digitalmars.com/ctg/make.html

        MAKE [-man] {target} {macro=text} {-dilnqst} [-fmakefile] {@file}

@file   Get command args from environment or file
target  What targets to make        macro=text  Define macro to be text
-d      Output debugging info       -ffile      Use file instead of makefile
-f-     Read makefile from stdin    -i  Ignore errors from executing make rules
-l      List macro expansions       -n  Just echo rules that would be executed
-q      If rules would be executed  -s  Do not echo make rules
        then exit with errorlevel 1 -t  Just touch files
-man    manual

Predefined macros:
        $$      Expand to $
        $@      Full target name
        $?      List of dependencies that are newer than target
        $**     Full list of dependencies
        $*      Name of current target without extension
        $<      From name of current target, if made using an implicit rule
Rule flags:
        +       Force use of COMMAND.COM to execute rule
        -       Ignore exit status
        @       Do not echo rule
        *       Can handle environment response files
        ~       Force use of environment response file
");
    printf("\nCommand error: ");
    printf(format,arg);
    exit(EXIT_FAILURE);
}

/*********************
 * Fatal error.
 */

void faterr(const char* format, const char* arg = null)
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

version (Windows)
{
    time(&t);

    /* FAT systems get their file times rounded up to a 2 second
       boundary. So we round up system time to match.
     */
    return (t + 2) & ~1;
}
else
{
    return time(&t);
}
}

/***************************
 * Set system time.
 */

void setsystemtime(time_t datetime)
{
/+
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
+/
}

/********************************
 * Get file's date and time.
 * Return 1L if file doesn't exist.
 */

time_t gettimex(char* name)
{   time_t datetime;
    time_t systemtime;

    Stat st;

    if (stat(name,&st) == -1)
        return 1L;
    datetime = st.st_mtime;

    debug printf("Returning x%lx\n",datetime);
    systemtime = getsystemtime();
    if (datetime > systemtime)
    {
        static if (1)
        {
            printf("File '%s' is newer than system time.\n",name);
            printf("File time = %ld, system time = %ld\n",datetime,systemtime);
            printf("File time = '%s'\n",ctime(&datetime));
            printf("Sys  time = '%s'\n",ctime(&systemtime));
        }
        else
        {   char c;

            printf("File '%s' is newer than system time. Fix system time (Y/N)? ",
                    name);
            fflush(stdout);
            c = bdos(1);
            if (c == 'y' || c == 'Y')
                setsystemtime(datetime);
            fputc('\n',stdout);
        }
    }
    return datetime;
}

/******************************
 * "Touch" a file, that is, give it the current date and time.
 * Returns:
 *      Time that was given to the file.
 */

time_t touch(char* name)
{
    time_t[2] timep = void;

    printf("touch('%s')\n",name);
    time(&timep[1]);
    utime(name,timep.ptr);
    return timep[1];
}

/***************************
 * Do our version of the DEL command.
 */

void builtin_del(char *args)
{   FINDA *f;
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
        {
            remove(f.name.ptr);
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
    i = chdir(args);
    if (i)
        return 1;
    return 0;
}

/*********************
 * Read makefile and build data structures.
 */

linelist **readmakefile(const char *makefile,linelist **rl)
{       FILE *f;
        char* line,p,q;
        int curlinesave = curline;

        if (!strcmp(makefile,"-"))
                f = stdin;              /* -f- means read from stdin    */
        else
                f = fopen(makefile,"r");
        if (!f)
                faterr("can't read makefile '%s'",makefile);
        inreadmakefile++;
        while (true)
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
                            faterr("target must appear before commands", null);
                        /* add line to current rule */
                        *rl = NEWOBJ!(linelist)();
                        (*rl).line = line;
                        rl = &((*rl).next);
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
                                addmacro(line,p,false);
                        }
                        else if (!*p)           /* if end of line       */
                        {       *pn = 0;        /* delete trailing whitespace */
                                if (!strcmp(line,".SILENT"))
                                    gag = true;
                                else if (!strcmp(line,".IGNORE"))
                                    ignore_errors = true;
                                else
                                    faterr("unrecognized target '%s'",line);
                        }
                        else if (memcmp(line,"include".ptr,7) == 0)
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

void addmacro(const char* name, const char* text, int perm)
{       MACRO **mp;

        debug printf("addmacro('%s','%s',%d)\n",name,text,perm);
        for (mp = &macrostart; *mp; mp = &((*mp).next))
        {       if (!strcmp(name,(*mp).name))   /* already in macro table */
                {       if ((*mp).perm) /* if permanent entry   */
                                return;         /* then don't change it */
                        mem_free((*mp).text);
                        goto L1;
                }
        }
        *mp = NEWOBJ!(MACRO)();
        (*mp).name = mem_strdup(name);
  L1:   (*mp).text = mem_strdup(skipspace(text));
        (*mp).perm = perm;
}

/*************************
 * Add target rule.
 * Return pointer to pointer to rule list.
 */

linelist **targetline(char* p)
{
        filelist* tlist,tl;
        filenode *t;
        rule *r;
        int nintlist;                   /* # of files in tlist          */
        char *pend;
        char c;
        char dblcln;

        debug printf("targetline('%s')\n",p);
        tlist = null;                   /* so addtofilelist() will work */

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
                    dotdefault = findfile(p,true);
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

        r = NEWOBJ!(rule)();
        for (tl = tlist; tl; tl = tl.next)
        {       t = tl.fnode;           /* for each target t in tlist   */
                t.dblcln = dblcln;
                if (t.frule)            /* if already got rules         */
                {   /*faterr("already have rules for %s\n",t.name);*/
                    freerule(t.frule);  /* dump them                    */
                }
                t.frule = r;            /* point at rule                */
                r.count++;              /* count how many point at this */
        }

        /* for each dependency broken out */
        p = skipspace(p);
        while (*p && *p != ';')
        {
                pend = skipname(p);
                if (p == pend)
                {
                    char[2] s = void;
                    s[0] = *p;
                    s[1] = 0;
                    faterr("'%s' is not a valid filename char", s.ptr);
                }
                c = *pend;
                *pend = 0;
                for (tl = tlist; tl; tl = tl.next)
                {       t = tl.fnode;   /* for each target t in tlist   */
                        /* add this dependency to its dependency list   */
                        addtofilelist(p,&(t.dep));
                        /*printf("Adding dep '%s' to file '%s'\n",p,t.name);*/
                }
                *pend = c;
                p = skipspace(pend);
        }
        if (!targlist &&                /* if we don't already have one */
            (tlist.next || tlist.fnode != dotdefault)
           )
                targlist = tlist;       /* use the first one we found   */
        else
        {       debug printf("freefilelist(%p)\n",tlist);
                freefilelist(tlist);    /* else dump it                 */
        }
        if (*p == ';')
        {
            p = skipspace(p + 1);
            if (*p)
            {   r.rulelist = NEWOBJ!(linelist)();
                r.rulelist.line = mem_strdup(p);
                return (&r.rulelist.next);
            }
        }
        return &(r.rulelist);
}

/***********************
 * Determine if line p is an implicit rule.
 */

int isimplicit(char* p)
{
    char *q;

    if (*p == '.' &&
        isfchar(p[1]) &&
        (q = strchr(p+2,'.')) != null &&
        strchr(p,':') > q)      /* implicit line        */

        return true;
    else
        return false;
}

/*************************
 * Add implicit rule.
 * Return pointer to pointer to rule list.
 */

linelist **addimplicit(char* p)
{
    implicit *g;
    implicit **pg;
    implicit *gr;
    rule *r;
    char *pend;
    char c;

    debug printf("addimplicit('%s')\n",p);
    pg = &implicitstart;
    r = NEWOBJ!(rule)();
    do
    {
        while (*pg)
            pg = &((*pg).next);

        g = *pg = NEWOBJ!(implicit)();
        g.grule = r;
        r.count++;

        /* Get fromext[]        */
        pend = ++p;                     /* skip over .                  */
        while (isfchar(*pend))
                pend++;
        if (p == pend) goto err;
        c = *pend;
        *pend = 0;
        if (strlen(p) > EXTMAX) goto err;
        strcpy(g.fromext.ptr,p);
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
        strcpy(g.toext.ptr,p);
        *pend = c;
        p = skipspace(pend);

        /* See if it's already in the list      */
        for (gr = implicitstart; gr != g; gr = gr.next)
                if (!filenamecmp(gr.fromext.ptr,g.fromext.ptr) &&
                    !filenamecmp(gr.toext.ptr,g.toext.ptr))
                        faterr("ambiguous implicit rule", null);

        debug printf("adding implicit rule from '%s' to '%s'\n",
                g.fromext,g.toext);
    } while (*p == '.');
    if (*p != ':')
        goto err;
    /* Rest of line must be blank       */
    p = skipspace(p + 1);
    if (*p == ';')              /* rest of line is a rule line          */
    {
        p = skipspace(p + 1);
        if (*p)
        {   r.rulelist = NEWOBJ!(linelist)();
            r.rulelist.line = mem_strdup(p);
            return (&r.rulelist.next);
        }
    }
    if (*p)
        goto err;
    return &(r.rulelist);

err:
    faterr("bad syntax for implicit rule, should be .frm.to:", null);
    assert(0);
}

/*************************
 * Read line from file f into buf.
 * Remove comments at this point.
 * Remove trailing whitespace from line.
 * Returns:
 *      true if end of file
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
                                buf = cast(char*)mem_realloc(buf,bufmax);
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

                        buf[i++] = cast(char)c;
            }
        } while (i == 0 && c != EOF);   /* if 0 length line             */
        buf[i] = 0;                     /* terminate string             */

        debug printf("[%d:%s]\n", curline, buf);

        return (c == EOF);
}

/*******************
 * Add filename to end of file list.
 */

void addtofilelist(char* filename, filelist** pflist)
{
        filelist **pfl;
        filelist* fl;

        for (pfl = pflist; *pfl; pfl = &((*pfl).next))
        {       if (!filenamecmp(filename,(*pfl).fnode.name))
                        return;         /* if already in list           */
        }
        fl = NEWOBJ!(filelist)();
        *pfl = fl;
        fl.fnode = findfile(filename,true);
}

/*****************
 * Find filename in file list.
 * If it isn't there and install is true, install it.
 */

filenode *findfile(char* filename, int install)
{       filenode **pfn;

        /*debug printf("findfile('%s')\n",filename);*/
        for (pfn = &filenodestart; *pfn; pfn = &((*pfn).next))
        {       if (!filenamecmp((*pfn).name,filename))
                        return *pfn;
        }
        if (install)
        {
            *pfn = NEWOBJ!(filenode)();
            (*pfn).name = mem_strdup(filename);
        }
        return *pfn;
}

/************************
 * Perform macro expansion on the line pointed to by buf.
 * Return pointer to created string.
 */

char *expandline(char *buf)
{
    uint i;                     /* where in buf we have expanded up to  */
    uint b;                     /* start of macro name                  */
    uint t;                     /* start of text following macro call   */
    uint p;                     /* 1 past end of macro name             */
    int paren;
    char c;
    const(char)* text;

    debug printf("expandline('%s')\n",buf);
    buf = mem_strdup(buf);
    i = 0;
    while (buf[i])
    {   if (buf[i] == '$')      /* if start of macro            */
        {   b = i + 1;
            if (buf[b] == '(')
            {   paren = true;
                b++;
                p = b;
                while (buf[p] != ')')
                        if (!buf[p++])
                                faterr("')' expected");
                t = p + 1;
            }
            else
            {   paren = false;
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
            const textlen = strlen(text);
            /* If replacement text exactly matches macro call, skip expansion */
            if (textlen == t - i && strncmp(text,buf + i,t - i) == 0)
                i = t;
            else
            {
                const buflen = strlen(buf);
                buf = cast(char*)mem_realloc(buf,buflen + textlen + 1);
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

const(char)* searchformacro(const char *name)
{       MACRO *m;
        char *envstring;

        for (m = macrostart; m; m = m.next)
                if (!strcmp(name,m.name))
                        return m.text;

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

                buf = null;
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

        return "".ptr;
}

/********************
 * Skip spaces.
 */

inout(char) *skipspace(inout(char)* p)
{
        while (isspace(*p))
                p++;
        return p;
}

/********************
 * Skip file names.
 */

char *skipname(char* p)
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

char *filespecdotext(char* p)
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
 * Return true if char is a file name character.
 */

int isfchar(char c)
{
    return isalnum(c) || c == '_' || c == '-';
}

/***********************
 * Return true if char is a file name character, including path separators
 * and .s
 */

int ispchar(char c)
{
    return isfchar(c) || c == ':' || c == '/' || c == '\\' || c == '.';
}

/***********************
 * Add extension to filename.
 * Delete old extension if there was one.
 */

char *filespecforceext(char* name, char* ext)
{       char* newname,p;

        newname = cast(char*)mem_calloc(strlen(name) + strlen(ext) + 1 + 1);
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

char *filespecgetroot(char* name)
{       char* root,p;
        char c;

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

void do_implicits()
{       filenode *f;
        char* ext,depname;
        implicit *g;
        time_t time;

        for (f = filenodestart; f; f = f.next)
        {       if (f.frule)
                {       if (f.frule.rulelist)   /* if already have rules */
                                continue;
                        freerule(f.frule);
                        f.frule = null;
                }
                ext = filespecdotext(f.name);
                if (*ext == '.')
                        ext++;
                for (g = implicitstart; g; g = g.next)
                {   if (!filenamecmp(ext,g.toext.ptr))
                    {   filenode *fd;

                        strcpy(f.genext.ptr,g.fromext.ptr);
                        depname = filespecforceext(f.name,f.genext.ptr);
                        time = gettimex(depname);
                        if (time == 1L) /* if file doesn't exist */
                        {   fd = findfile(depname,false);
                            if (!fd)
                            {
                                mem_free(depname);
                                continue;
                            }
                        }
                        else
                            fd = findfile(depname,true);

                        fd.time = time;
                        f.frule = g.grule;
                        f.frule.count++;
                        addtofilelist(depname,&(f.dep));
                        mem_free(depname);
                        break;
                    }
                }
static if (0)
{
                if (!g && dotdefault)   /* if failed to find implicit rule */
                {   /* Use default rule */
                    f.frule = dotdefault.frule;
                    f.frule.count++;
                }
}
        }
}

/***************************
 * Make a file. Return true if rules were executed.
 */

int make(filenode* f)
{       int made = false;
        int gooddate = false;
        filelist *dl;                   /* dependency list              */
        filelist *newer;
        char *newbuf;
        int totlength,starstarlen;

        if (f.expanding)
            faterr("circular dependency for '%s'",f.name);
        debug printf("make('%s')\n",f.name);
        dl = f.dep;

        addmacro("$","$",false);
        addmacro("?","",false);         /* the default                  */
        addmacro("**","",false);
        if (!f.frule || !f.frule.rulelist)      /* if no make rules     */
        {   if (f.time <= 1)
                f.time = gettimex(f.name);
            if (!dl)                            /* if no dependencies   */
            {   if (f.time == 1)                /* if file doesn't exist */
                {
                    if (dotdefault && dotdefault.frule &&
                        dotdefault.frule.rulelist)
                    {
                        f.frule = dotdefault.frule;
                        f.frule.count++;
                        return dorules(f);
                    }
                    else
                        faterr("don't know how to make '%s'",f.name);
                }
                return false;
            }
        }

        if (!dl)                        /* if no dependencies           */
                return dorules(f);      /* execute rules                */

        /* Make each dependency, also compute length of $** expansion   */
        starstarlen = 0;
        f.expanding++;
        for (; dl; dl = dl.next)
        {       made |= make(dl.fnode);
                starstarlen += strlen(dl.fnode.name) + 1;
        }
        f.expanding--;

        newbuf = cast(char*)mem_calloc(starstarlen + 1);
        *newbuf = 0;                    /* initial 0 length string      */

        /* If there are any newer dependencies, we must remake this one */
        newer = null;
        totlength = 0;
        for (dl = f.dep; dl; dl = dl.next)
        {       if (!dl.fnode.time)
                        dl.fnode.time = gettimex(dl.fnode.name);
                strcat(newbuf,dl.fnode.name);
                strcat(newbuf," ");
            L1:
                if (f.time < dl.fnode.time)
                {       if (!gooddate)  /* if date isn't guaranteed     */
                        {   f.time = gettimex(f.name);
                            gooddate = true;
                            goto L1;
                        }
                        if (xdebug)
                        {   if (f.time == 1L)
                                printf("File '%s' doesn't exist\n",f.name);
                            else
                                printf("file '%s' is older than '%s'\n",
                                        f.name,dl.fnode.name);
                        }
                        /* still out of date    */
                        addtofilelist(dl.fnode.name,&newer);
                        totlength += strlen(dl.fnode.name) + 1;
                }
        }
        addmacro("**",newbuf,false);    /* full list of dependencies    */
        mem_free(newbuf);

        if (newer)                      /* if any newer dependencies    */
        {       filelist *fl;

                newbuf = cast(char*)mem_calloc(totlength + 1);
                *newbuf = 0;            /* initial 0 length string      */
                for (fl = newer; fl; fl = fl.next)
                {       strcat(newbuf,fl.fnode.name);
                        strcat(newbuf," ");
                }
                addmacro("?",newbuf,false);     /* newer dependencies   */
                mem_free(newbuf);
                freefilelist(newer);
                return made | dorules(f);
        }
        if (!gooddate)
                f.time = gettimex(f.name);
        return made;
}

/********************************
 * Execute rules for a filenode.
 * Return true if we executed some rules.
 */

int dorules(filenode* f)
{       char* root, fromname;
        linelist *l;

        debug printf("dorules('%s')\n",f.name);
        if (!f.frule)
                return false;
        if (touchem)
        {       f.time = touch(f.name);
                return true;            /* assume rules were executed   */
        }
        root = filespecgetroot(f.name);
        fromname = filespecforceext(root,f.genext.ptr);
        addmacro("*",root,false);
        addmacro("<",fromname,false);
        addmacro("@",f.name,false);
        mem_free(root);
        mem_free(fromname);
        for (l = f.frule.rulelist; l; l = l.next)
                executerule(l.line);
        f.time = gettimex(f.name);
        return true;
}

/******************************
 * Determine if filename p is in the array of strings.
 */

int inarray(const char *p, const char **array, size_t dim)
{
    const(char*)* b;
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

void executerule(char* p)
{       char echo = true;
        char igerr = false;
        char useCOMMAND = false;
        char useenv = 0;
        char forceuseenv = 0;
        char* cmd,args;
        char c;

        debug printf("executerule('%s')\n",p);
        if (question)
        {       action = 1;     /* file is not up to date               */
                return;
        }
        p = skipspace(p);
        while (1)
        {   switch (*p)
            {   case '+':       useCOMMAND = true;      p++; continue;
                case '@':       echo = false;           p++; continue;
                case '-':       igerr = true;           p++; continue;
                case '*':       useenv = '@';           p++; continue;
                case '~':       forceuseenv = true;
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

        bool flag = true;
version (Windows)
{
        flag = false;
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
            __gshared const(char)*[] builtin =  /* MS-DOS built-in commands     */
            [   "break","cls","copy","ctty",
                "date","dir","echo","erase","exit",
                "for","goto","if","md","mkdir","pause",
                "rd","rem","rmdir","ren","rename",
                "shift","time","type","ver","verify","vol"
            ];

            __gshared const(char)*[] respenv =  /* uses our response files      */
            [   "ztc","make","touch","blink","blinkr","blinkx",
                "zorlib","zorlibx","lib","zrcc","sc","sj",
                "dmc","dmd"
            ];

            __gshared const(char)*[] pharrespenv = /* uses pharlap response files       */
            [   "386asm","386asmr","386link","386linkr","386lib","fastlink",
                "bind386",
            ];

            useCOMMAND  |= inarray(p,builtin.ptr,builtin.length);
            if (inarray(p,respenv.ptr,respenv.length))
                useenv = '@';
            if (inarray(p,pharrespenv.ptr,pharrespenv.length))
                useenv = '%';
        }

        /* Use COMMAND.COM for .BAT files       */
        if (!filenamecmp(filespecdotext(p),".bat"))
            useCOMMAND |= true;

        if (useCOMMAND)
        {
            Lcmd:
                *cmd = c;               /* restore stomped character    */
                if (strlen(p) > CMDLINELEN - 2) /* -2 for /c switch     */
                        faterr("command line too long");
                debug printf("Using COMMAND.COM\n");
                system(p);
        }
        else if (!filenamecmp(p,"del"))
        {
                if (strchr(args,'\\'))
                {   useCOMMAND = true;
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
                    *q = cast(char)toupper(*q);         /* convert env var to uc */

                if (putenv(args))               /* set environment      */
                    faterr("out of memory");
        }
        else
            flag = true;
}
        if (flag)
        {       int status;
                size_t len;

                debug printf("Using FORK\n");
version (Windows)
{
                if (forceuseenv || (len = strlen(args)) > CMDLINELEN)
                {   char *q;
                    char[10] envname = "@_CMDLINE";

                    if (!useenv)
                        goto L1;
                    envname[0] = useenv;
                    q = cast(char *) mem_calloc(envname.sizeof + len);
                    sprintf(q,"%s=%s",envname.ptr + 1,args);
                    status = putenv(q);
                    mem_free(q);
                    if (status == 0)
                        args = envname.ptr;
                    else
                    {
                     L1:
                        faterr("command line too long");
                    }
                }
}
static if (1)
{
version (POSIX)
                status = system(p);
else
                status = spawnlp(0,p,p,args,null);

                if (status == -1)
                        faterr("'%s' not found",p);
}
else
{
                status = forklp(p,p,"",args,null);
                if (status)                     /* if error             */
                        faterr("'%s' not found",p);
                else
                        status = wait();        /* exit code of p       */
}
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

version (Windows)
{
  version (all)
  {
    CMDLINELEN = 10000;
  }
  else
  {
    OSVERSIONINFO OsVerInfo;

    OsVerInfo.dwOSVersionInfoSize = OsVerInfo.sizeof;
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
  }
}
}

/****************** STORAGE MANAGEMENT *****************/

static if (1)
{

/*******************
 */

void *mem_calloc(size_t size)
{ void* p;

/*  debug printf("size = %d\n",size);*/
  newcnt++;
  p = calloc(size,1);
  if (!p)
        faterr("out of memory");
/*  debug printf("mem_calloc() = %p\n",p);*/
  return p;
}

/*******************
 */

void mem_free(void *p)
{
    /*debug printf("mem_free(%p)\n",p);*/
    if (p)
    {   free(p);
        newcnt--;
    }
}

/********************
 * Re-allocate a buffer.
 */

void *mem_realloc(void *oldbuf, size_t newbufsize)
{
        void* p;

        if (!oldbuf)
            newcnt++;
        p = realloc(oldbuf,newbufsize);
        if (p == null)
            faterr("out of memory");
        return p;
}

/******************
 * Save string in new'ed memory and return a pointer to it.
 */

char *mem_strdup(const char *s)
{
    return strcpy(cast(char*)mem_calloc(strlen(s) + 1),s);
}

}

version (TERMCODE)
{
void freemacro()
{       MACRO* m,mn;

        for (m = macrostart; m; m = mn)
        {       mn = m.next;
                mem_free(m.name);
                mem_free(m.text);
                mem_free(m);
        }
}
}

void freefilelist(filelist *fl)
{       filelist *fln;

        for (; fl; fl = fln)
        {       fln = fl.next;
                mem_free(fl);
        }
}

version (TERMCODE)
{

void freeimplicits()
{       implicit* g,gn;

        for (g = implicitstart; g; g = gn)
        {       gn = g.next;
                freerule(g.grule);
                mem_free(g);
        }
}

void freefilenode(filenode *f)
{       filenode *fn;

        for (; f; f = fn)
        {       fn = f.next;
                mem_free(f.name);
                freefilelist(f.dep);
                freerule(f.frule);
                mem_free(f);
        }
}

}

void freerule(rule* r)
{       linelist* l,ln;

        if (!r || --r.count)
                return;
        for (l = r.rulelist; l; l = ln)
        {       ln = l.next;
                mem_free(l.line);
                mem_free(l);
        }
        mem_free(r);
}

/****************** DEBUG CODE *************************/

debug
{

void WRmacro(MACRO *m)
{
    printf("macro %p: perm %d next %p name '%s' = '%s'\n",
                m,m.perm,m.next,m.name,m.text);
}

void WRmacrolist()
{       MACRO *m;

        printf("****** MACRO LIST ********\n");
        for (m = macrostart; m; m = m.next)
                WRmacro(m);
}

void WRlinelist(linelist *l)
{
        while (l)
        {       printf("line %p next %p: '%s'\n",l,l.next,l.line);
                l = l.next;
        }
}

void WRrule(rule *r)
{
        printf("rule %p: count = %d\n",r,r.count);
        WRlinelist(r.rulelist);
}

void WRimplicit()
{       implicit *g;

        for (g = implicitstart; g; g = g.next)
        {       printf("implicit %p next %p: from '%s' to '%s'\n",
                        g,g.next,g.fromext,g.toext);
                WRrule(g.grule);
                putchar('\n');
        }
}

void WRfilenode(filenode *f)
{       filelist *fl;

        printf("filenode %p: name '%s' genext '%s' time %ld\n",
                f,f.name,f.genext,f.time);
        printf("Dependency list:\n");
        for (fl = f.dep; fl; fl = fl.next)
                printf("\t%s\n",fl.fnode.name);
        if (f.frule)
                WRrule(f.frule);
        putchar('\n');
}

void WRfilelist(filelist *fl)
{
        for (; fl; fl = fl.next)
                WRfilenode(fl.fnode);
}

void WRfilenodelist(filenode *fn)
{
        for (; fn; fn = fn.next)
                WRfilenode(fn);
}

}
