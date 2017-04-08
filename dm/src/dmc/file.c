/**
 * Implementation of the
 * $(LINK2 http://www.digitalmars.com/download/freecompiler.html, Digital Mars C/C++ Compiler).
 *
 * Copyright:   Copyright (c) 1985-1998 by Symantec, All Rights Reserved
 *              Copyright (c) 2000-2017 by Digital Mars, All Rights Reserved
 * Authors:     $(LINK2 http://www.digitalmars.com, Walter Bright)
 * License:     Distributed under the Boost Software License, Version 1.0.
 *              http://www.boost.org/LICENSE_1_0.txt
 * Source:      https://github.com/DigitalMars/Compiler/blob/master/dm/src/dmc/file.c
 */

#include        <stdio.h>
#include        <string.h>
#include        <ctype.h>
#include        <stdlib.h>
#include        <fcntl.h>
#include        <time.h>

#if _WIN32 || _WIN64
#include        <io.h>
#include        <share.h>
#include        <sys\stat.h>
#endif

#if __linux__ || __APPLE__ || __FreeBSD__ || __OpenBSD__ || __sun
#include        <sys/stat.h>
#include        <unistd.h>
#endif

#include        "cc.h"
#include        "parser.h"
#include        "global.h"
#include        "filespec.h"
#include        "token.h"
#include        "scdll.h"
#include        "html.h"
#include        "outbuf.h"

static char __file__[] = __FILE__;      /* for tassert.h                */
#include        "tassert.h"

STATIC void getcmd_filename (char **pname,const char *ext);
STATIC void file_openread(const char *f,blklst *b);

static int lastlinnum;
int includenest;

#if _WIN32 && _WINDLL
static list_t file_list;
#endif

// File name extensions
#if __linux__ || __APPLE__ || __FreeBSD__ || __OpenBSD__ || __sun
char ext_obj[] = ".o";
#endif

#if _WIN32 || _WIN64
char ext_obj[] = ".obj";
#endif

char ext_i[]   = ".i";
char ext_dep[] = ".dep";
char ext_lst[] = ".lst";
char ext_hpp[] = ".hpp";
char ext_c[]   = ".c";
char ext_cpp[] = ".cpp";
char ext_sym[] = ".sym";
char ext_tdb[] = ".tdb";
char ext_dmodule[]   = ".d";

/*********************************
 * Open file for writing.
 * Input:
 *      f ->            file name string
 *      mode ->         open mode string
 * Returns:
 *      file stream pointer, or NULL if error
 */

FILE *file_openwrite(const char *name,const char *mode)
{   FILE *stream;

    //printf("file_openwrite(name='%s', mode='%s')\n", name, mode);
    if (name)
    {
        const char *newname = file_nettranslate(name,mode);
        stream = fopen(newname,mode);
        if (!stream)
            cmderr(EM_open_output,newname);     // error opening output file
    }
    else
        stream = NULL;
    return stream;
}

/*********************************************
 * Given a #include filename, search for the file.
 * If it exists, return a path to the file, and the time of the file.
 * Input:
 *      *pfilespec      filespec string
 *      flag            FQxxxx
 *      pathlist        paths to look for file
 * Output:
 *      *pfilename      mem_malloc'd path of file, if found
 *      *next_path      remaining path for possible future #include_next
 * Returns:
 *      !=0 if file is found
 */


#define PATHSYSLIST 0

#if PATHSYSLIST
list_t pathsyslist;
#endif

int file_qualify(char **pfilename, int flag, phstring_t pathlist, int *next_path)
{
    char *fname;
    phstring_t __searchpath = pathlist;

    char *p = *pfilename;
    assert(p);

#if 0
    printf("file_qualify(file='%s',flag=x%x)\n",p,flag);
    for (int i = 0; i < pathlist.length(); ++i)
        printf("[%d] = '%s'\n", i, pathlist[i]);
#endif

    *next_path = -1;

    if (flag & FQtop)
    {
        *pfilename = mem_strdup(p);
        return 1;
    }

    if (config.flags3 & CFG3igninc && flag & FQcwd)
    {   flag &= ~FQcwd;
        flag |= FQpath;
    }

    // If file spec is an absolute, rather than relative, file spec
    if (*p == '/' || *p == '\\' || (*p && p[1] == ':'))
        flag = FQcwd;       // don't look at paths

    if (flag & FQqual)                  // if already qualified
        flag = (flag | FQcwd) & ~(FQpath|FQnext);
#if PATHSYSLIST
    int save_flag;
    if (flag & FQpath)
    {
        __searchpath = pathsyslist;
        save_flag = flag;
        flag = FQpath|FQsystem;
    }
#endif

    blklst *b = cstate.CSfilblk;

    if (flag & FQnext)
    {
        /* Look at the path remaining after the current file was found.
         */
        if (b && b->BLsearchpath >= 0)
        {
            for (int i = b->BLsearchpath + 1; i < pathlist.length(); ++i)
            {
                fname = filespecaddpath(pathlist[i],p);
                int result = file_exists(fname);
                if (result)         // if file exists
                {
                    *next_path = i;
                    *pfilename = fname;
                    return result;
                }
                mem_free(fname);
            }
        }
        return 0;
    }

#if PATHSYSLIST
retry:
    ;
#endif

    char *pext = NULL;
    while (1)
    {
        switch (flag & (FQcwd | FQpath))
        {
            case FQpath:
                if (__searchpath.length())
                    break;
                /* FALL-THROUGH */
            case FQcwd | FQpath:                /* check current directory first */
                if (cstate.CSfilblk)
                {
                    /* Look for #include file relative to directory that
                       enclosing file resides in.
                     */
                    blklst *b = cstate.CSfilblk;
                    char *p2 = filespecname(blklst_filename(b));
                    char c = *p2;
                    *p2 = 0;
                    fname = filespecaddpath(blklst_filename(b),p);
                    *p2 = c;
                }
                else
                {
            case FQcwd:     // Look relative to current directory
                    fname = mem_strdup(p);
                }
            {
                //printf("file_exists 1 stat('%s')\n",fname);
                int result = file_exists(fname);
                if (result)             // if file exists
                {
                    *pfilename = fname;
                    return result;
                }
                mem_free(fname);
                break;
            }
            default:
                assert(0);
        }
        if (flag & FQpath)      // if look at include path
        {
            for (int i = 0; i < __searchpath.length(); ++i)
            {
                fname = filespecaddpath(__searchpath[i], p);
                //printf("file_exists 2 stat('%s')\n",fname);
                int result = file_exists(fname);
                if (result)             // if file exists
                {
                    *next_path = i;    // remember for FQnext
                    *pfilename = fname;
                    return result;
                }
                mem_free(fname);
            }
        }
        if (filespeccmp(filespecdotext(p),ext_hpp) == 0)
        {   // Chop off the "pp" and try again
            pext = p + strlen(p) - 2;
            *pext = 0;
        }
        else
        {   if (pext)
                *pext = 'p';            // restore ".hpp"
            break;
        }
    }
#if PATHSYSLIST
    if (__searchpath == pathsyslist)
    {
        __searchpath = pathlist;
        flag = save_flag;
        goto retry;
    }
#endif
    return 0;                   // not found
}

/*********************************************
 * Open a new file for input.
 * Watch out for open failures!
 * Input:
 *      p ->            filespec string (NULL for fin)
 *      bl ->           blklst structure to fill in
 *      flag            FQxxxx
 * Output:
 *      bl ->           newly opened file data
 */

void afopen(char *p,blklst *bl,int flag)
{
    //printf("afopen(%p,'%s',flag=x%x)\n",p,p,flag);
    assert(bl->BLtyp == BLfile);
#if HTOD
    htod_include(p, flag);
#endif
    if (flag & FQqual)
        p = mem_strdup(p);
    else if (!file_qualify(&p, flag, pathlist, &bl->BLsearchpath))
        err_fatal(EM_open_input,p);             // open failure
    bl->BLsrcpos.Sfilptr = filename_indirect(filename_add(p));
    sfile_debug(&srcpos_sfile(bl->BLsrcpos));
    srcpos_sfile(bl->BLsrcpos).SFflags |= (flag & FQtop) ? SFtop : 0;
    file_openread(p,bl);
    if (cstate.CSfilblk)
    {   sfile_debug(&srcpos_sfile(cstate.CSfilblk->BLsrcpos));
        list_append(&srcpos_sfile(cstate.CSfilblk->BLsrcpos).SFfillist,*bl->BLsrcpos.Sfilptr);
    }

    if (configv.verbose)
        NetSpawnFile(p,(flag & FQsystem) ? -(includenest + 1) : includenest);
    includenest++;
    if (configv.verbose == 2)
    {   int i;
        char buffer[32];

        memset(buffer,' ',sizeof(buffer));
        i = (includenest < sizeof(buffer)) ? includenest : sizeof(buffer) - 1;
        buffer[i] = 0;
        dbg_printf("%s'%s'\n",buffer,p);
    }

    if (fdep && !(flag & FQsystem))
    {
        //fprintf(fdep, "%s ", p);
        fdeplist.push(p);
    }
    else
        mem_free(p);
}

/*********************************************
 * Determine the source file name.
 * Input:
 *      what the user gave us for a source name
 * Returns:
 *      malloc'd name
 */

char *file_getsource(const char *iname)
{
#if M_UNIX
    static char ext[][4] = { "cpp","cxx","c", "C", "cc", "c++" };
#else
    static char ext[][5] = { "cpp","c","cxx","htm","html" };
#endif

    // Generate file names
    if (!iname || *iname == 0)
        cmderr(EM_nosource);            // no input file specified

    size_t len = strlen(iname);
    char *n = (char *) malloc(len + 6);       // leave space for .xxxx0
    assert(n);
    strcpy(n,iname);
    char *p = filespecdotext(n);
    if (!*p)    // if no extension
    {
        for (int i = 0; i < arraysize(ext); i++)
        {   *p = '.';
            strcpy(p + 1,ext[i]);
            if (file_exists(n) & 1)
                break;
            *p = 0;
        }
    }
    return n;
}

/***************************************
 * Twiddle with file names, open files for I/O.
 */

void file_iofiles()
{
    // Switch into mem space
    char *p = finname;
    finname = mem_strdup(p);
    free(p);

    assert(finname);
    filename_add(finname);

#if SPP
    if (!foutname)
        fout = stdout;
    else
    {
        if (filespeccmp(filespecdotext(foutname),ext_obj) == 0)
            // Ignore -o switch if it is a .obj filename
            foutname = (char*)"";
#if M_UNIX
        // Default to writing preprocessed result to stdout
        if (!*foutname)
            fout = stdout;
        else
#endif
        {
            getcmd_filename(&foutname,ext_i);
            fout = file_openwrite(foutname,"w");
        }
    }

    /* If writing to a file, increase buffer size
     */
    if (!isatty(fileno(fout)))
    {
        //printf("writing to a file %d\n", BUFSIZ);
        setvbuf(fout,NULL,_IOFBF,1024*1024);
        /* Don't check result, don't care if it fails
         */
    }
    getcmd_filename(&fdepname,ext_dep);
    fdep = file_openwrite(fdepname,"w");
    if (0 && fdep)
    {   // Build entire makefile line
        fprintf(fdep, "%s : ", foutname);
    }
#else
    // See if silly user specified output file name for -HF with -o
    if (fsymname && !*fsymname && filespeccmp(filespecdotext(foutname),ext_sym) == 0)
    {   fsymname = foutname;
        foutname = (char*)"";
        config.flags2 |= CFG2noobj;
    }

    getcmd_filename(&foutname,ext_obj);
    getcmd_filename(&fdepname,ext_dep);
    getcmd_filename(&flstname,ext_lst);
    getcmd_filename(&fsymname,ext_sym);
#if HTOD
    getcmd_filename(&fdmodulename,ext_dmodule);
#endif

    if (!ftdbname || !ftdbname[0])
        ftdbname = (char*)"symc.tdb";
    getcmd_filename(&ftdbname,ext_tdb);

#ifdef DEBUG
    printf("source <= '%s' obj => '%s' dep => '%s' lst => '%s' sym => '%s' tdb => '%s'\n",
        finname,foutname,fdepname,flstname,fsymname,ftdbname);
#endif

    // Now open the files
#if HTOD
    fdmodule = file_openwrite(fdmodulename,"w");
#else
    objfile_open(foutname);
    fdep = file_openwrite(fdepname,"w");
    flst = file_openwrite(flstname,"w");

    if (0 && fdep)
    {   // Build entire makefile line
        fprintf(fdep, "%s : ", foutname);
    }
#endif
#endif
}

/********************************
 * Generate output file name with default extension.
 * If no file name, use finname as the basis for it.
 * Input:
 *      finname         input file name
 *      foutdir         output file default directory
 */

STATIC void getcmd_filename(char **pname,const char *ext)
{   char *p;

#ifdef DEBUG
    assert(*ext == '.');
#endif
    ext++;                              // skip over '.'
    p = *pname;
    if (p)
    {   char *n;

        n = filespecforceext(filespecname(finname),ext);
        if (*p)
        {
            if (file_isdir(p))
                p = filespecaddpath(p,n);
            else if (foutdir && *p != '\\' && *p != '/' && p[1] != ':')
            {   mem_free(n);
                n = filespecaddpath(foutdir,p);
                p = filespecdefaultext(n,ext);
            }
            else
                p = filespecdefaultext(p,ext);
            mem_free(n);
        }
        else if (foutdir)
        {
            p = filespecaddpath(foutdir,n);
            mem_free(n);
        }
        else
            p = n;


        if (!filename_cmp(finname,p))
        {   *pname = NULL;
            cmderr(EM_mult_files,finname);      // duplicate file names
        }
        *pname = p;
    }
}


/******************************
 * Read in source file.
 */

STATIC void file_openread(const char *name,blklst *b)
{   unsigned char *p;
    unsigned long size;
    char *newname;
    int fd;

    //printf("file_openread('%s')\n",name);

    newname = file_nettranslate(name,"rb");
#if __linux__ || __APPLE__ || __FreeBSD__ || __OpenBSD__ || __sun
    fd = open(newname,O_RDONLY,S_IREAD);
#else
    fd = _sopen(newname,O_RDONLY | O_BINARY,_SH_DENYWR);
#endif
    if (fd == -1)
        err_fatal(EM_open_input,newname);               // open failure

    /*  1:      so we can index BLtext[-1]
        2:      so BLtext is 2 behind BLbufp, allowing for addition of
                \n at end of file followed by 0, without stepping on ^Z
        2:      allow room for appending LF ^Z
     */
    size = os_file_size(fd);

    // If it is an HTML file, read and preprocess it
    char *dotext = filespecdotext(name);
    if (filespeccmp(dotext, ".htm") == 0 ||
        filespeccmp(dotext, ".html") == 0)
    {
        unsigned char *p;

        p = (unsigned char *) util_malloc(size + 1, 1);
        if (read(fd,p,size) != size)
            err_fatal(EM_eof);                  // premature end of source file
        close(fd);
        p[size] = 0;                            // make sure it's terminated

        Outbuffer buf;
        Html h(name, p, size);

        buf.reserve(3 + size + 2);
        buf.writeByte(0);
        buf.writeByte(0);
        buf.writeByte(0);
        h.extractCode(&buf);                    // preprocess
        size = buf.size() - 3;

        b->BLbuf = buf.buf;
        b->BLtext = b->BLbuf + 1;
        b->BLbufp = b->BLbuf + 3;
#if __DMC__
        buf = NULL;
#endif
    }
    else
    {
        b->BLbuf = (unsigned char *) util_malloc(1 + 2 + size + 2,1);
        memset(b->BLbuf,0,3);
        b->BLtext = b->BLbuf + 1;
        b->BLbufp = b->BLbuf + 3;

        if (read(fd,b->BLbufp,size) != size)
            err_fatal(EM_eof);                  // premature end of source file
        close(fd);
    }

    p = (unsigned char *)&b->BLbufp[size];

    // File must end in LF. If it doesn't, make it.
    if (p[-1] != LF)
    {
        if (ANSI && !CPP)
            lexerr(EM_no_nl);   // file must be terminated by '\n'
        p[0] = LF;
        ++p;
    }

    // Put a ^Z past the end of the buffer as a sentinel
    // (So buffer is guaranteed to end in ^Z)
    *p = 0x1A;
}


/***********************************
 * Read next line from current input file.
 * Input:
 *      bl              currently open file
 * Returns:
 *      0               if no more input
 *      !=0             line buffer filled in
 */

int readln()
{   unsigned char c;
    unsigned char *ps,*p;
    int tristart = 0;
    blklst *b = bl;

    assert(bl);
    b->BLsrcpos.Slinnum++;              // line counter

#if TX86 && !(__linux__ || __APPLE__ || __FreeBSD__ || __OpenBSD__ || __sun)
        __asm
        {
                mov     ESI,b
                xor     DL,DL
                mov     EDI,[ESI].BLbuf
                mov     ECX,0x0D0A      ;CH = CR, CL = LF
                inc     EDI
                mov     [ESI].BLtext,EDI
                mov     btextp,EDI
                mov     ESI,[ESI].BLbufp
        L1:
                mov     AL,[ESI]
                cmp     AL,0x1A
                jnz     L4
        }
                includenest--;
                if (configv.verbose)
                    NetSpawnFile(blklst_filename(b),kCloseLevel);
#if HTOD
                htod_include_pop();
#endif
                return FALSE;
        __asm
        {
        L3:     mov     3[EDI],DL
                mov     AL,4[ESI]
                add     ESI,4
                add     EDI,4

        L4:     cmp     AL,CL
                jz      L10
                mov     DL,1[ESI]
                mov     [EDI],AL

                cmp     DL,CL
                jz      L11
                mov     AL,2[ESI]
                mov     1[EDI],DL

                cmp     AL,CL
                jz      L12
                mov     DL,3[ESI]
                mov     2[EDI],AL

                cmp     DL,CL
                jnz     L3

                cmp     AL,CH
                jnz     L13
                dec     EDI
        L13:    add     ESI,4
                add     EDI,3
                jmp     Lx

        L12:    cmp     DL,CH
                jnz     L14
                dec     EDI
        L14:    add     ESI,3
                add     EDI,2
                jmp     Lx

        L11:    cmp     AL,CH
                jnz     L15
                dec     EDI
        L15:    add     ESI,2
                inc     EDI
                jmp     Lx

        L10:    cmp     DL,CH
                jnz     L16
                dec     EDI
        L16:    inc     ESI

        Lx:     mov     p,EDI
                mov     ps,ESI

        }
#else
        b->BLtext = b->BLbuf + 1;               // +1 so we can bl->BLtext[-1]
        btextp = b->BLtext;             // set to start of line
        p = btextp;
        ps = b->BLbufp;
    L1:
        c = *ps++;
        if (c == 0x1A)
        {
            includenest--;
            if (configv.verbose)
                NetSpawnFile(blklst_filename(b),kCloseLevel);
#if HTOD
            htod_include_pop();
#endif
            return FALSE;
        }
        while (c != LF)
        {   if (c != CR)
                *p++ = c;               // store char in input buffer
            c = *ps++;
        }
#endif
        {
                if (TRIGRAPHS)
                {   // Do trigraph translation
                    // BUG: raw string literals do not undergo trigraph translation
                    static char trigraph[] = "=(/)'<!>-";
                    static char mongraph[] = "#[\\]^{|}~"; // translation of trigraph
                    int len;
                    unsigned char *s,*sn;

                    len = p - btextp;
                    // tristart is so we don't scan twice for trigraphs
                    for (s = btextp + tristart;
                         (sn = (unsigned char *)memchr(s,'?',len)) != NULL; )
                    {   unsigned char *q;

                        len -= sn - s;          // len = remaining length
                        s = sn;
                        if (*++s == '?' &&
                            (q = (unsigned char *) strchr(trigraph,s[1])) != NULL)
                        {   s[-1] = mongraph[q - (unsigned char *) trigraph];
                            len -= 2;
                            p -= 2;
                            memmove(s,s + 2,len);
                        }
                    }
                    tristart = p - btextp;
                }

                // Translate trailing CR-LF to LF
                //if (p > btextp && p[-1] == '\r')
                //    p--;

                // Look for backslash line splicing
                if (p[-1] == '\\')
                {
                    // BUG: backslash line splicing does not happen in raw strings
                    if (ismulti(p[-2]))
                    {   // Backslash may be part of multibyte sequence
                        unsigned char *s;

                        for (s = btextp; s < p; s++)
                        {
                            if (ismulti(*s))
                            {   s++;
                                if (s == p - 1) // backslash is part of multibyte
                                    goto L5;    // not a line continuation
                            }
                        }
                    }
                    p--;
                    b->BLsrcpos.Slinnum++;
#if TX86 && __DMC__
                    _asm
                    {
                        mov     EDI,p
                        mov     ESI,ps
                        xor     DL,DL
                        mov     ECX,0x0D0A      ;CH = CR, CL = LF
                    }
#endif
                    goto L1;
                }
                else
                {
                L5:
                    p[0] = LF;
                    p[1] = 0;
                    b->BLbufp = ps;
                    return TRUE;
                }
        }
}


/***********************************
 * Write out current line, and draw a ^
 * under the current position of the line pointer.
 * Input:
 *      fstream =       output stream pointer
 *      bl->            input file data
 */

#define line_out TRUE
void wrtpos(FILE *fstream)
{   char *p,*ptop,*fname;
    int fline;
    int aline;
    blklst *b;
    Srcpos sp;

    sp = token_linnum();
    fline = sp.Slinnum;
    b = cstate.CSfilblk;
    if (!b)                             /* no data to read              */
    {
        if (fline)
            fname = srcpos_name(sp);
        else
            fname = finname;
        aline = 0;
    }
    else
    {
        fname = srcpos_name(sp);
        p = (char *) b->BLtext;
        ptop = (char *) ((b == bl) ? btextp : b->BLtextp);
        aline = b->BLsrcpos.Slinnum;    /* actual line number           */
    }
    if (line_out && aline == fline)     /* if on right line             */
    {
        if (config.flags2 & CFG2expand)
        {
            wrtexp(fstream);            /* write expanded output        */
            p = eline;
            ptop = p + elini;
        }
#if __linux__ || __APPLE__ || __FreeBSD__ || __OpenBSD__ || __sun
        else if (fstream == stderr)     /* line already written to .LST */
#else
        else if (fstream == stdout)     /* line already written to .LST */
#endif
            wrtlst(fstream);            // write listing line
    }

    if (fline)
    {
     if (line_out && aline == fline && *p)
     {                                  /* only if on right line        */
         if (ptop - p >= 2)
            ptop -= 2;
         for (; *p != '\n' && p < ptop; p++)
            fputc(((*p == '\t') ? '\t' : ' '),fstream);
         fprintf(fstream,"^\n");
     }
     fprintf(fstream,dlcmsgs(EM_line_format),fname,fline);
    }
}


/**********************************
 * Send current line to stream.
 * Input:
 *      fstream =       output stream pointer
 *      bl->            input file data
 */

void wrtlst(FILE *fstream)
{ blklst *b;

  b = cstate.CSfilblk;
  if (b)                                /* if data to read              */
  {     char c,*p;

        for (p = (char *) b->BLtext; (c = *p) != 0; p++)
        {   if (isillegal(c))
                c = ' ';
            if (c != '\n' && c != '\r')
                fputc(c,fstream);
        }
        crlf(fstream);
        fflush(fstream);
  }
}

/***********************************
 * Send progress report.
 */

void file_progress()
{
    if (controlc_saw)
        util_exit(EXIT_BREAK);
#if USEDLLSHELL
    if (configv.verbose)
    {   blklst *b;

        b = cstate.CSfilblk;
        if (NetSpawnProgress(b ? b->BLsrcpos.Slinnum : kNoLineNumber) != NetSpawnOK)
            err_exit();
    }
#endif
}

/***********************************
 * Net translate filename.
 */

#if _WIN32 && _WINDLL

char *file_nettranslate(const char *filename,const char *mode)
{   char *newname;
    static int nest;

    nest++;
    newname = NetSpawnTranslateFileName((char *)filename,(char *)mode);
    if (!newname)
    {   if (nest == 1)
            err_exit();                 // abort without message
    }
    else
        list_append(&file_list,newname);
    nest--;
    return newname;
}

#endif

/************************************
 * Delete file.
 */

void file_remove(char *fname)
{   char *newname;

    if (fname)
    {   newname = NetSpawnTranslateFileName(fname,"w");
        if (newname)
        {   remove(newname);    // delete file
            NetSpawnDisposeFile(newname);
        }
    }
}

/***********************************
 * Do a stat on a file.
 */

int file_stat(const char *fname,struct stat *pbuf)
{
    //printf("file_stat(%s)\n", fname);
#if _WIN32 && _WINDLL
    int result;
    char *newname;

    newname = NetSpawnTranslateFileName((char *)fname,"rb");
    if (newname)
    {   result = stat(newname,pbuf);
        NetSpawnDisposeFile(newname);
    }
    else
        result = -1;
    return result;
#else
    return stat(fname,pbuf);
#endif
}

/*************************************
 * Determine if fname is a directory.
 * Returns:
 *      0       not a directory
 *      !=0     a directory
 */

int file_isdir(const char *fname)
{
    char c;
    int result;

    c = fname[strlen(fname) - 1];
    if (c == ':' || c == '/' || c == '\\')
        result = 2;
    else
        result = file_exists(fname) & 2;
    return result;
}

/**************************************
 * Determine if file exists.
 * Returns:
 *      0:      file doesn't exist
 *      1:      normal file
 *      2:      directory
 */

int file_exists(const char *fname)
{
    //printf("file_exists(%s)\n", fname);
#if _WIN32
    int result;
    char *newname;

    newname = NetSpawnTranslateFileName((char *)fname,"rb");
    if (newname)
    {   result = os_file_exists(newname);
        NetSpawnDisposeFile(newname);
    }
    else
        result = 0;
    return result;
#elif __linux__ || __APPLE__ || __FreeBSD__ || __OpenBSD__ || __sun
    struct stat buf;

    return stat(fname,&buf) == 0;       /* file exists if stat succeeded */

#else
    return os_file_exists(fname);
#endif
}

/***********************************
 * Determine size of file.
 * Returns:
 *      -1L     file not found
 */

long file_size(const char *fname)
{
    //printf("file_size(%s)\n", fname);
#if __DMC__
    long result;
    char *newname;

    newname = NetSpawnTranslateFileName((char *)fname,"rb");
    if (newname)
    {   result = filesize(newname);
        NetSpawnDisposeFile(newname);
    }
    else
        result = -1L;
    return result;
#else
    long result;
    struct stat buf;

    if (file_stat(fname,&buf) != -1)
        result = buf.st_size;
    else
        result = -1L;
    return result;
#endif
}

/***********************************
 * Write out dependency file.
 */

void file_dependency_write()
{
    size_t dim = fdeplist.length();
    int col = 1;
    for (size_t i = 0; i < dim; i++)
    {
        char *p = fdeplist[i];
#if SPP
        if (i == 0)
        {
            char *q = filespecforceext(p, ext_obj);
            fprintf(fdep, "%s: ", q);
            col += strlen(q) + 2;
            mem_free(q);
        }
#endif
        if (col >= 70)
        {
            fputs(" \\\n ", fdep);
            col = 2;
        }
        else if (i)
        {
            fputc(' ', fdep);
            ++col;
        }
        fputs(p, fdep);
        col += strlen(p);
    }
    if (col > 1)
        fputc('\n', fdep);
#if SPP
    for (size_t i = 1; i < dim; i++)
    {
        char *p = fdeplist[i];
        fprintf(fdep, "\n%s:\n", p);
    }
#endif
    fclose(fdep);
    fdeplist.free(mem_freefp);
}

/***********************************
 * Terminate use of all network translated filenames.
 */

void file_term()
{
    if (fdep)
        file_dependency_write();
#if _WIN32 && _WINDLL
    list_t fl;

    for (fl = file_list; fl; fl = list_next(fl))
        NetSpawnDisposeFile((char *)list_ptr(fl));
    list_free(&file_list,FPNULL);
#endif
    //printf("free(%p)\n",cstate.modname);
    free(cstate.modname);
    cstate.modname = NULL;
}

/*************************************
 * Translate input file name into an identifier unique to this module.
 * Returns:
 *      pointer to identifier we can use. Caller does not need to free it.
 */

#if !SPP

char *file_unique()
{
    if (!cstate.modname)
    {   char *p;
        size_t len;

        len = 2 + strlen(finname) + sizeof(long) * 3 + 1;
        p = (char *)malloc(len);
        //printf("malloc(%d) = %p\n",len,p);
        cstate.modname = p;
#if __linux__ || __APPLE__ || __FreeBSD__ || __OpenBSD__ || __sun
        snprintf(p,len,"__%s%lu",finname,getpid());
#else
        sprintf(p,"?%%%s%lu",finname,os_unique());
#endif
        assert(strlen(p) < len);
        p += 2;
        do
        {   if (!isalnum(*p))
                *p = '_';               // force valid identifier char
        } while (*++p);
    }
    return cstate.modname;
}

#endif
