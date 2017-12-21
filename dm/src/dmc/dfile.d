/**
 * Implementation of the
 * $(LINK2 http://www.digitalmars.com/download/freecompiler.html, Digital Mars C/C++ Compiler).
 *
 * Copyright:   Copyright (c) 1985-1998 by Symantec, All Rights Reserved
 *              Copyright (c) 2000-2017 by Digital Mars, All Rights Reserved
 * Authors:     $(LINK2 http://www.digitalmars.com, Walter Bright)
 * License:     Distributed under the Boost Software License, Version 1.0.
 *              http://www.boost.org/LICENSE_1_0.txt
 * Source:      https://github.com/DigitalMars/Compiler/blob/master/dm/src/dmc/dfile.d
 */

module dfile;

import core.stdc.ctype;
import core.stdc.limits;
import core.stdc.stdio;
import core.stdc.stdlib;
import core.stdc.string;
import core.stdc.time;

extern (C) int read(int,void*,uint);    // io.h
extern (C) int isatty(int);             // io.h

import dmd.backend.cc;
import dmd.backend.cdef;
import dmd.backend.global;
import dmd.backend.outbuf;

import tk.dlist;
import tk.filespec;
import tk.mem;

import html;
import msgs2;
import parser;
import phstring;
import dmcdll;
import dtoken;

extern (C) void crlf(FILE *);

extern (C++):

alias dbg_printf = printf;

__gshared
{
 /*private*/ int lastlinnum;
int includenest;

// File name extensions
extern (C):
version (Posix)
{
const char[3] ext_obj = ".o";
}

version (Windows)
{
const(char)[5] ext_obj = ".obj";
}

char[3] ext_i   = ".i";
char[5] ext_dep = ".dep";
char[5] ext_lst = ".lst";
char[5] ext_hpp = ".hpp";
char[3] ext_c   = ".c";
char[5] ext_cpp = ".cpp";
char[5] ext_sym = ".sym";
char[5] ext_tdb = ".tdb";
char[3] ext_dmodule = ".d";
}

version (none)
{
}
/*********************************
 * Open file for writing.
 * Input:
 *      f ->            file name string
 *      mode ->         open mode string
 * Returns:
 *      file stream pointer, or null if error
 */

extern (C) FILE *file_openwrite(const(char)* name,const(char)* mode)
{   FILE *stream;

    //printf("file_openwrite(name='%s', mode='%s')\n", name, mode);
    if (name)
    {
        const(char)* newname = dmcdll_nettranslate(name,mode);
        stream = fopen(newname,mode);
        if (!stream)
            cmderr(EM_open_output,newname);     // error opening output file
    }
    else
        stream = null;
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


enum PATHSYSLIST = 0;

static if (PATHSYSLIST)
{
extern __gshared list_t pathsyslist;
}

int file_qualify(char **pfilename, int flag, phstring_t pathlist, int *next_path)
{
    char *fname;
    phstring_t __searchpath = pathlist;

    char *p = *pfilename;
    assert(p);

static if (0)
{
    printf("file_qualify(file='%s',flag=x%x)\n",p,flag);
    for (int i = 0; i < pathlist.length(); ++i)
        printf("[%d] = '%s'\n", i, pathlist[i]);
}

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
static if (PATHSYSLIST)
{
    int save_flag;
    if (flag & FQpath)
    {
        __searchpath = pathsyslist;
        save_flag = flag;
        flag = FQpath|FQsystem;
    }
}

    blklst *b = cstate.CSfilblk;

    if (flag & FQnext)
    {
        /* Look at the path remaining after the current file was found.
         */
        if (b && b.BLsearchpath >= 0)
        {
            for (int i = b.BLsearchpath + 1; i < pathlist.length(); ++i)
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

static if (PATHSYSLIST)
{
retry:
    ;
}

    char *pext = null;
    while (1)
    {
        switch (flag & (FQcwd | FQpath))
        {
            case FQpath:
                if (__searchpath.length())
                    break;
                goto case FQcwd | FQpath;
            case FQcwd | FQpath:                /* check current directory first */
                if (cstate.CSfilblk)
                {
                    /* Look for #include file relative to directory that
                       enclosing file resides in.
                     */
                    blklst *bx = cstate.CSfilblk;
                    char *p2 = filespecname(blklst_filename(bx));
                    char c = *p2;
                    *p2 = 0;
                    fname = filespecaddpath(blklst_filename(bx),p);
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
        if (filespeccmp(filespecdotext(p),ext_hpp.ptr) == 0)
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
static if (PATHSYSLIST)
{
    if (__searchpath == pathsyslist)
    {
        __searchpath = pathlist;
        flag = save_flag;
        goto retry;
    }
}
    return 0;                   // not found
}


/*********************************************
 * Open a new file for input.
 * Watch out for open failures!
 * Input:
 *      p ->            filespec string (null for fin)
 *      bl ->           blklst structure to fill in
 *      flag            FQxxxx
 * Output:
 *      bl ->           newly opened file data
 */

void afopen(char *p,blklst *bl,int flag)
{
    //printf("afopen(%p,'%s',flag=x%x)\n",p,p,flag);
    assert(bl.BLtyp == BLfile);

version (HTOD)
    htod_include(p, flag);

    if (flag & FQqual)
        p = mem_strdup(p);
    else if (!file_qualify(&p, flag, pathlist, &bl.BLsearchpath))
        err_fatal(EM_open_input,p);             // open failure
    bl.BLsrcpos.Sfilptr = filename_indirect(filename_add(p));
    sfile_debug(&(**(bl.BLsrcpos).Sfilptr));
    srcpos_sfile(bl.BLsrcpos).SFflags |= (flag & FQtop) ? SFtop : 0;
    file_openread(p,bl);
    if (cstate.CSfilblk)
    {   sfile_debug(&(**(cstate.CSfilblk.BLsrcpos).Sfilptr));
        list_append(&srcpos_sfile(cstate.CSfilblk.BLsrcpos).SFfillist,*bl.BLsrcpos.Sfilptr);
    }

    if (configv.verbose)
        dmcdll_SpawnFile(p,(flag & FQsystem) ? -(includenest + 1) : includenest);
    includenest++;
    if (configv.verbose == 2)
    {   int i;
        char[32] buffer = void;

        memset(buffer.ptr,' ',buffer.sizeof);
        i = (includenest < buffer.sizeof) ? includenest : buffer.sizeof - 1;
        buffer[i] = 0;
        dbg_printf("%s'%s'\n",buffer.ptr,p);
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

char *file_getsource(const(char)* iname)
{
version (Posix)
    __gshared char[4][6] ext = [ "cpp","cxx","c", "C", "cc", "c++" ];
else
    __gshared char[5][5] ext = [ "cpp","c","cxx","htm","html" ];

    // Generate file names
    if (!iname || *iname == 0)
        cmderr(EM_nosource);            // no input file specified

    size_t len = strlen(iname);
    char *n = cast(char *) malloc(len + 6);       // leave space for .xxxx0
    assert(n);
    strcpy(n,iname);
    char *p = filespecdotext(n);
    if (!*p)    // if no extension
    {
        for (int i = 0; i < ext.length; i++)
        {   *p = '.';
            strcpy(p + 1,ext[i].ptr);
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

version (SPP)
{
    if (!foutname)
        fout = stdout;
    else
    {
        if (filespeccmp(filespecdotext(foutname),cast(char*)ext_obj) == 0)
            // Ignore -o switch if it is a .obj filename
            foutname = cast(char*)"";
version (Posix)
{
        // Default to writing preprocessed result to stdout
        if (!*foutname)
            fout = stdout;
        else
        {
            getcmd_filename(&foutname,ext_i);
            fout = file_openwrite(foutname,"w");
        }
}
else
{
        {
            getcmd_filename(&foutname,cast(char*)ext_i);
            fout = file_openwrite(foutname,"w");
        }
}
    }

    /* If writing to a file, increase buffer size
     */
    if (!isatty(fileno(fout)))
    {
        //printf("writing to a file %d\n", BUFSIZ);
        setvbuf(fout,null,_IOFBF,1024*1024);
        /* Don't check result, don't care if it fails
         */
    }
    getcmd_filename(&fdepname,cast(char*)ext_dep);
    fdep = file_openwrite(fdepname,"w");
    if (0 && fdep)
    {   // Build entire makefile line
        fprintf(fdep, "%s : ", foutname);
    }
}
else
{
    // See if silly user specified output file name for -HF with -o
    if (fsymname && !*fsymname && filespeccmp(filespecdotext(foutname),ext_sym.ptr) == 0)
    {   fsymname = foutname;
        foutname = cast(char*)"";
        config.flags2 |= CFG2noobj;
    }

    getcmd_filename(&foutname,ext_obj.ptr);
    getcmd_filename(&fdepname,ext_dep.ptr);
    getcmd_filename(&flstname,ext_lst.ptr);
    getcmd_filename(&fsymname,ext_sym.ptr);
version (HTOD)
    getcmd_filename(&fdmodulename,ext_dmodule.ptr);

    if (!ftdbname || !ftdbname[0])
        ftdbname = cast(char*)"symc.tdb";
    getcmd_filename(&ftdbname,ext_tdb.ptr);

    debug printf("source <= '%s' obj => '%s' dep => '%s' lst => '%s' sym => '%s' tdb => '%s'\n",
        finname,foutname,fdepname,flstname,fsymname,ftdbname);

    // Now open the files
version (HTOD)
    fdmodule = file_openwrite(fdmodulename,"w");
else
{
    objfile_open(foutname);
    fdep = file_openwrite(fdepname,"w");
    flst = file_openwrite(flstname,"w");

    if (0 && fdep)
    {   // Build entire makefile line
        fprintf(fdep, "%s : ", foutname);
    }
}
}
}

/********************************
 * Generate output file name with default extension.
 * If no file name, use finname as the basis for it.
 * Input:
 *      finname         input file name
 *      foutdir         output file default directory
 */

/*private*/ void getcmd_filename(char **pname,const(char)* ext)
{   char *p;

    debug assert(*ext == '.');
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
        {   *pname = null;
            cmderr(EM_mult_files,finname);      // duplicate file names
        }
        *pname = p;
    }
}


/******************************
 * Read in source file.
 */

/*private*/ void file_openread(const(char)* name,blklst *b)
{   ubyte *p;
    uint size;
    char *newname;
    int fd;

    //printf("file_openread('%s')\n",name);

    newname = dmcdll_nettranslate(name,"rb");

version (Posix)
    fd = open(newname,O_RDONLY,S_IREAD);
else
    fd = _sopen(newname,O_RDONLY | O_BINARY,_SH_DENYWR);

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
        ubyte *px;

        px = cast(ubyte *) util_malloc(size + 1, 1);
        if (read(fd,px,size) != size)
            err_fatal(EM_eof);                  // premature end of source file
        close(fd);
        px[size] = 0;                            // make sure it's terminated

        Outbuffer buf;
        Html h;
        h.initialize(name, px, size);

        buf.reserve(3 + size + 2);
        buf.writeByte(0);
        buf.writeByte(0);
        buf.writeByte(0);
        h.extractCode(&buf);                    // preprocess
        size = buf.size() - 3;

        b.BLbuf = cast(char*)buf.buf;
        b.BLtext = b.BLbuf + 1;
        b.BLbufp = b.BLbuf + 3;

version (DigitalMars)
        buf.buf = null;
    }
    else
    {
        b.BLbuf = cast(char *) util_malloc(1 + 2 + size + 2,1);
        memset(b.BLbuf,0,3);
        b.BLtext = b.BLbuf + 1;
        b.BLbufp = b.BLbuf + 3;

        if (read(fd,b.BLbufp,size) != size)
            err_fatal(EM_eof);                  // premature end of source file
        close(fd);
    }

    p = cast(ubyte *)&b.BLbufp[size];

    // File must end in LF. If it doesn't, make it.
    if (p[-1] != LF)
    {
        if (config.ansi_c && !CPP)
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
{   ubyte c;
    ubyte* ps,p;
    int tristart = 0;
    blklst *b = bl;

    assert(bl);
    b.BLsrcpos.Slinnum++;              // line counter

version (Win32)
{
        asm
        {
                mov     ESI,b                   ;
                xor     DL,DL                   ;
                mov     EDI,[ESI]blklst.BLbuf.offsetof   ;
                mov     ECX,0x0D0A              ; //CH = CR, CL = LF
                inc     EDI                     ;
                mov     [ESI]blklst.BLtext.offsetof,EDI  ;
                mov     btextp,EDI              ;
                mov     ESI,[ESI]blklst.BLbufp.offsetof  ;
        L1:
                mov     AL,[ESI]                ;
                cmp     AL,0x1A                 ;
                jnz     L4                      ;
        }
                includenest--;
                if (configv.verbose)
                    dmcdll_SpawnFile(blklst_filename(b));
version (HTOD)
                htod_include_pop();

                return false;
        asm
        {
        L3:     mov     3[EDI],DL       ;
                mov     AL,4[ESI]       ;
                add     ESI,4           ;
                add     EDI,4           ;

        L4:     cmp     AL,CL           ;
                jz      L10             ;
                mov     DL,1[ESI]       ;
                mov     [EDI],AL        ;

                cmp     DL,CL           ;
                jz      L11             ;
                mov     AL,2[ESI]       ;
                mov     1[EDI],DL       ;

                cmp     AL,CL           ;
                jz      L12             ;
                mov     DL,3[ESI]       ;
                mov     2[EDI],AL       ;

                cmp     DL,CL           ;
                jnz     L3              ;

                cmp     AL,CH           ;
                jnz     L13             ;
                dec     EDI             ;
        L13:    add     ESI,4           ;
                add     EDI,3           ;
                jmp     Lx              ;

        L12:    cmp     DL,CH           ;
                jnz     L14             ;
                dec     EDI             ;
        L14:    add     ESI,3           ;
                add     EDI,2           ;
                jmp     Lx              ;

        L11:    cmp     AL,CH           ;
                jnz     L15             ;
                dec     EDI             ;
        L15:    add     ESI,2           ;
                inc     EDI             ;
                jmp     Lx              ;

        L10:    cmp     DL,CH           ;
                jnz     L16             ;
                dec     EDI             ;
        L16:    inc     ESI             ;

        Lx:     mov     p,EDI           ;
                mov     ps,ESI          ;

        }
}
else
{
        b.BLtext = b.BLbuf + 1;               // +1 so we can bl.BLtext[-1]
        btextp = cast(ubyte*)b.BLtext;             // set to start of line
        p = btextp;
        ps = cast(ubyte*)b.BLbufp;
    L1:
        c = *ps++;
        if (c == 0x1A)
        {
            includenest--;
            if (configv.verbose)
                dmcdll_SpawnFile(blklst_filename(b));
version (HTOD)
            htod_include_pop();

            return false;
        }
        while (c != LF)
        {   if (c != CR)
                *p++ = c;               // store char in input buffer
            c = *ps++;
        }
}
        {
                if (config.ansi_c)
                {   // Do trigraph translation
                    // BUG: raw string literals do not undergo trigraph translation
                    __gshared const(char)* trigraph = "=(/)'<!>-";
                    __gshared const(char)* mongraph = "#[\\]^{|}~"; // translation of trigraph
                    int len;
                    ubyte* s,sn;

                    len = p - btextp;
                    // tristart is so we don't scan twice for trigraphs
                    for (s = btextp + tristart;
                         (sn = cast(ubyte *)memchr(s,'?',len)) != null; )
                    {   ubyte *q;

                        len -= sn - s;          // len = remaining length
                        s = sn;
                        if (*++s == '?' &&
                            (q = cast(ubyte *) strchr(trigraph,s[1])) != null)
                        {   s[-1] = mongraph[q - cast(ubyte *) trigraph];
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
                        ubyte *s;

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
                    b.BLsrcpos.Slinnum++;
version (Win32)
{
                    asm
                    {
                        mov     EDI,p           ;
                        mov     ESI,ps          ;
                        xor     DL,DL           ;
                        mov     ECX,0x0D0A      ; // CH = CR, CL = LF
                    }
}
                    goto L1;
                }
                else
                {
                L5:
                    p[0] = LF;
                    p[1] = 0;
                    b.BLbufp = cast(char*)ps;
                    return true;
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

enum line_out = true;

extern (C) void wrtpos(FILE *fstream)
{   char* p,ptop,fname;
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
        p = cast(char *) b.BLtext;
        ptop = cast(char *) ((b == bl) ? cast(char*)btextp : b.BLtextp);
        aline = b.BLsrcpos.Slinnum;    /* actual line number           */
    }
    if (line_out && aline == fline)     /* if on right line             */
    {
        if (config.flags2 & CFG2expand)
        {
            wrtexp(fstream);            /* write expanded output        */
            p = eline;
            ptop = p + elini;
        }
        else
        {
            version (POSIX)
                enum stream = stderr;
            else
                enum stream = stdout;
            if (fstream == stream)     // line already written to .LST
                wrtlst(fstream);            // write listing line
        }
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
 *      bl.            input file data
 */

extern (C) void wrtlst(FILE *fstream)
{ blklst *b;

  b = cstate.CSfilblk;
  if (b)                                /* if data to read              */
  {     char c;
        char* p;

        for (p = cast(char *) b.BLtext; (c = *p) != 0; p++)
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

    if (configv.verbose)
    {   blklst *b;

        b = cstate.CSfilblk;
        if (dmcdll_Progress(b ? b.BLsrcpos.Slinnum : -1))
            err_exit();
    }
}


/************************************
 * Delete file.
 */

void file_remove(char *fname)
{   char *newname;

    if (fname)
    {   newname = dmcdll_TranslateFileName(fname,cast(char*)"w".ptr);
        if (newname)
        {   remove(newname);    // delete file
            dmcdll_DisposeFile(newname);
        }
    }
}

/*************************************
 * Determine if fname is a directory.
 * Returns:
 *      0       not a directory
 *      !=0     a directory
 */

int file_isdir(const(char)* fname)
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

int file_exists(const(char)* fname)
{
    //printf("file_exists(%s)\n", fname);
    int result;
    char *newname;

    newname = dmcdll_TranslateFileName(cast(char *)fname,cast(char*)"rb".ptr);
    if (newname)
    {   result = os_file_exists(newname);
        dmcdll_DisposeFile(newname);
    }
    else
        result = 0;
    return result;
}

/***********************************
 * Determine size of file.
 * Returns:
 *      -1L     file not found
 */

int file_size(const(char)* fname)
{
    //printf("file_size(%s)\n", fname);
    int result;
    char *newname;

    newname = dmcdll_TranslateFileName(cast(char *)fname,cast(char*)"rb".ptr);
    if (newname)
    {   result = os_file_size(newname);
        dmcdll_DisposeFile(newname);
    }
    else
        result = -1L;
    return result;
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
version (SPP)
{
        if (i == 0)
        {
            char *q = filespecforceext(p, cast(char*)ext_obj);
            fprintf(fdep, "%s: ", q);
            col += strlen(q) + 2;
            mem_free(q);
        }
}
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
version (SPP)
{
    for (size_t i = 1; i < dim; i++)
    {
        char *p = fdeplist[i];
        fprintf(fdep, "\n%s:\n", p);
    }
}
    fclose(fdep);
    fdeplist.free(&mem_freefp);
}

/***********************************
 * Terminate use of all network translated filenames.
 */

void file_term()
{
    if (fdep)
        file_dependency_write();
    dmcdll_file_term();

    //printf("free(%p)\n",cstate.modname);
    free(cstate.modname);
    cstate.modname = null;
}

/*************************************
 * Translate input file name into an identifier unique to this module.
 * Returns:
 *      pointer to identifier we can use. Caller does not need to free it.
 */

version (SPP)
{
}
else
{

char *file_unique()
{
    if (!cstate.modname)
    {   char *p;
        size_t len;

        len = 2 + strlen(finname) + int.sizeof * 3 + 1;
        p = cast(char *)malloc(len);
        //printf("malloc(%d) = %p\n",len,p);
        cstate.modname = p;

version (Posix)
        snprintf(p,len,"__%s%lu",finname,getpid());
else
        sprintf(p,"?%%%s%lu",finname,os_unique());

        assert(strlen(p) < len);
        p += 2;
        do
        {   if (!isalnum(*p))
                *p = '_';               // force valid identifier char
        } while (*++p);
    }
    return cstate.modname;
}

}

