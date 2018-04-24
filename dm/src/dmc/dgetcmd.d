/**
 * Implementation of the
 * $(LINK2 http://www.digitalmars.com/download/freecompiler.html, Digital Mars C/C++ Compiler).
 *
 * Copyright:   Copyright (c) 1991-1998 by Symantec, All Rights Reserved
 *              Copyright (c) 2000-2017 by Digital Mars, All Rights Reserved
 * Authors:     $(LINK2 http://www.digitalmars.com, Walter Bright)
 * License:     Distributed under the Boost Software License, Version 1.0.
 *              http://www.boost.org/LICENSE_1_0.txt
 * Source:      https://github.com/DigitalMars/Compiler/blob/master/dm/src/dmc/dgetcmd.d
 */

// Routines to get and process the command string

module dgetcmd;

import core.stdc.ctype;
import core.stdc.stdio;
import core.stdc.string;
import core.stdc.stdlib;
import core.stdc.time;

extern (Pascal) int response_expand(int*, char***);     // from dmc dos.h
extern (C) char* strupr(char*);                         // from dmc string.h

import dmd.backend.cdef;
import dmd.backend.cc;
import dmd.backend.code;
import dmd.backend.global;
import dmd.backend.ty;
import dmd.backend.type;

import dmd.backend.dlist;
import filespec;
import tk.mem;

import dtoken;
import msgs2;
import parser;
import phstring;
import precomp;
import dmcdll;


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

enum INC_ENV = "INCLUDE";

__gshared
{

private __gshared char[2] one = "1";
__gshared Config config =                 // part of configuration saved in ph
{
                'D',            // compile for C++
                VERSION,
                SUFFIX,
                TARGET_8086,    // target_cpu
                TARGET_8086,    // target_cpu
                VERSIONINT,     // versionint
                1,              // config.defstructalign
};

Configv configv;                // non-ph part of configuration
EEcontext eecontext;

char switch_E = 0;              // for LINRECOR.ASM
extern (C)
{
int  _version = VERSIONINT;
}

const(char)* versionString = "(SCVersion)@" ~ COMPILER ~ " " ~ VERSION ~ SUFFIX;
const(char)* copyright = COPYRIGHT;
}

version (_WINDLL)
{
}
else
{
private void notice()
{
    printf("Digital Mars %s Version %s\n%s\nhttp://www.digitalmars.com\n",
        COMPILER.ptr,(VERSION ~ SUFFIX).ptr,copyright);
}

private void usage()
{
    printf("%s\n",dlcmsgs(EM_usage));
}
}


/******************************************************
 * Get & parse the command line. Open necessary files.
 * Input:
 *      argc =          # of args in command line + 1
 *      argv[] =        array of pointers to the arg strings
 */

void getcmd(int argc,char **argv)
{   int i = 1;
    char *p;
    char *q;
    char *finname2;
    bool switch_U = false;
    char target = 0;
    char scheduler = 0;
    char switch_a = false;              // saw a -a switch
    __gshared char model = 'N';
    int switchalign = 1;                // default alignment
    int n;
    uint flags;
    uint *pflags;
    int defalign;
    void *mmfiobase = null;
    uint reservesize = 0;
    int cvtype = CV4;
    phstring_t pathsyslist;

version (__OS2__)
    config.exe = EX_OS1;
else
    config.exe = EX_MZ;

    config.objfmt = OBJ_OMF;
    config.threshold = THRESHMAX;       // default to near data
    config.flags4 |= CFG4anew;
    config.flags4 |= CFG4forscope;
    config.flags4 |= CFG4adl;
    config.flags4 |= CFG4enumoverload;
    config.flags4 |= CFG4underscore;

version (HTOD)
    fdmodulename = cast(char*)"";
else
    foutname = cast(char*)"";

    if (argc <= 1)
    {
version (_WINDLL)
{
}
else
{
        notice();
        //usage();
}
        err_exit();
    }
version (SPP)
    getcmd_cflags(&argc,&argv);         // handle CFLAGS

version (Windows)
{
    if (response_expand(&argc,&argv))   /* expand response files        */
        cmderr(EM_response_file);       // can't open response file
}
    configv.verbose = 1;
    configv.errmax = 5;
    dmcdll_command_line(argc,argv,copyright);

    int on;
    for (i = 1; i < argc; i++)          // for each argument
    {

        p = argv[i];                    /* p -> argument string         */
        //dbg_printf("arg[%d] = '%s'\n",i,p);

        /* Some proposed switch changes:
                -R      -Nw
                -S      -gr
                -s      -go
                !-p     -Ja
                -r      -Jp
         */
        switch (*p)
        {
version (Windows)
            case '/':
version (SPP)
{
            case '-':
                break;
            case '+':
                continue;
}
else
{
            case '-':
                if (go_flag(p))
                    continue;
                break;
            case '+':
                if (go_flag(p))
                    continue;
                goto badflag;
}
            default:
            {   // Assume argument is a file name or directory name

version(all){
                char *dotext;

                if (file_isdir(p))
                {   foutdir = p;        // set directory for output files
                    continue;
                }
                dotext = filespecdotext(p);
version (SPP)
{
                if (filespeccmp(dotext,ext_i.ptr) == 0)
                    foutname = p;
                else if (filespeccmp(dotext,ext_dep.ptr) == 0)
                    fdepname = p;
                else
                    finname = p;
}
else version (HTOD)
{
                if (filespeccmp(dotext,ext_dmodule.ptr) == 0)
                    fdmodulename = p;
                else
                    finname = p;
}
else
{
                if (filespeccmp(dotext,ext_obj.ptr) == 0)
                    foutname = p;
                else if (filespeccmp(dotext,ext_dep.ptr) == 0)
                    fdepname = p;
                else if (filespeccmp(dotext,ext_lst.ptr) == 0)
                    flstname = p;
                else if (filespeccmp(dotext,ext_sym.ptr) == 0)
                    goto case_HF;       // equivalent to -HF being thrown
                else if (filespeccmp(dotext,ext_tdb.ptr) == 0)
                {   p--;
                    goto case_g6;       // equivalent to -g6
                }
                else if (*dotext &&
                        (filespeccmp(dotext,ext_cpp.ptr) == 0 ||
                         filespeccmp(dotext,".cxx") == 0))
                {   config.flags3 |= CFG3cpp;   // ".cpp" extension, assume C++
                    finname = p;
                }
                else
                    finname = p;
}
}
                continue;
            }
        }

        p++;
        on = strchr(p,'-') == null;
        switch (*p++)
        {
            case 'a':
                switch_a = true;
                switchalign = 0;
                if (isdigit(*p))
                {
                    switchalign = atoi(p);
                    if (ispow2(switchalign) == -1)
                        cmderr(EM_align);       // must be power of 2
                    switchalign--;
                }
                break;
            case 'A':
                if (*p == 0)
                {   config.ansi_c = 99;
                    break;
                }
                if (p[0] == '8' && p[1] == '9' && p[2] == 0)
                {   config.ansi_c = 89;
                    break;
                }
                if (p[0] == '9')
                {
                    if (p[1] == '0' && p[2] == 0)
                    {   config.ansi_c = 89;
                        break;
                    }
                    if (p[1] == '4' && p[2] == 0)
                    {   config.ansi_c = 95;
                        break;
                    }
                    if (p[1] == '5' && p[2] == 0)
                    {   config.ansi_c = 95;
                        break;
                    }
                    if (p[1] == '9' && p[2] == 0)
                    {   config.ansi_c = 99;
                        break;
                    }
                }
                while (1)
                {
                    switch (*p++)
                    {
                        case 0:   break;
                        case 'a': config.flags4 |= CFG4anew;    continue;
                        case 'b': config.flags4 |= CFG4bool;    continue;
                        case 'd': config.flags4 |= CFG4dependent; continue;
                        case 'e': config.flags3 |= CFG3eh;      continue;
                        case 'i': config.flags4 |= CFG4implicitfromvoid; continue;
                        case 'k': config.flags4 |= CFG4adl;     continue;
                        case 'r': config.flags3 |= CFG3rtti;    continue;
                        case 's': config.flags4 |= CFG4forscope; continue;
                        case 'v': config.flags4 |= CFG4enumoverload; continue;
                        case 'w': config.flags4 |= CFG4wchar_t; continue;
                        case '-': config.flags3 &= ~(CFG3eh | CFG3rtti);
                                  config.flags4 &= ~(CFG4anew | CFG4bool | CFG4wchar_t | CFG4forscope | CFG4adl | CFG4enumoverload | CFG4implicitfromvoid | CFG4dependent);
                                  continue;
                        default:  goto badflag;
                    }
                    break;
                }
                break;

            case 'B':
                switch (*p)
                {   case 0:
                    case 'e':   configv.language = LANGenglish; break;
                    case 'f':   configv.language = LANGfrench;  break;
                    case 'g':   configv.language = LANGgerman;  break;
                    case 'j':   configv.language = LANGjapanese;        break;
                    default:
                        break; // goto badflag ?
                }
                break;

            case 'c':
                        if (p[0] == 'p' && p[1] == 'p')        // if -cpp
                        {   config.flags3 |= CFG3cpp;
                            break;
                        }
                        goto Lonec;             // ignore SC driver flag

            case 'C':   flags = CFGnoinlines;
                        goto Lflags1;

            case 'd':
                        fdepname = (on || strcmp(p,"-")) ? p : null;
                        break;

            case 'D':   // do sw_d(p) later
                        break;

            case 'e':   flags = CFG2expand;
                        goto Lflags2;

            case 'E':
                switch (*p++)
                {
                    case 'C':   flags = CFG3comment;    goto Lflags3;
                    case 'H':   flags = CFG3eh;         goto Lflags3;   // obsolete
                    case 'L':   flags = CFG3noline;     goto Lflags3;
                    case 'R':   flags = CFG3rtti;       goto Lflags3;   // obsolete
                    default:
                        goto badflag;
                }
                break;

            case 'f':
                switch (*p)
                {   case 0:
                        config.inline8087 = cast(ubyte)on;
                        break;
                    case 'a':
                        config.flags4 |= CFG4stackalign;
                        break;
                    case 'd':
                        config.inline8087 = 1;
                        config.flags4 |= CFG4fdivcall;
                        break;
                    case 'f':                   // fast floating point
                        config.inline8087 = 1;
                        config.flags4 |= CFG4fastfloat;
                        break;
                    case 'w':                   // weak floating point
                        config.flags3 |= CFG3wkfloat;
                        break;
                    default:
                        break;
                }
                break;

            case 'g':
version (SCPP)
{
                for (q = p; 1; p++)
                {
                    switch (*p)
                    {   case 'f':   config.flags2 |= CFG2fulltypes;
                                    configv.addlinenumbers = 1;
                                    goto case_s;
                        case 'l':   configv.addlinenumbers = 1; break;
                        case 'd':   config.flags2 |= CFG2dyntyping;     break;
                        case 'b':   config.flags2 |= CFG2browse;        break;
                        case 'g':   config.flags |= CFGglobal;  break;
                        case 't':   config.flags |= CFGtrace;   break;
                        case 'p':   config.flags3 |= CFG3ptrchk; break;
                        case 'x':   config.flags2 |= CFG2stomp;  break;

                        case '-':   configv.addlinenumbers = 0;
                                    config.fulltypes = CVNONE;
                                    config.flags &= ~(CFGglobal |
                                                CFGtrace | CFGalwaysframe);
                                    config.flags2 &= ~(CFG2browse |
                                                CFG2dyntyping | CFG2hdrdebug |
                                                CFG2fulltypes);
                                    break;

                        case 0:     if (p == q)         // -g by itself
                                    {   configv.addlinenumbers = 1;
                                        config.flags2 |= CFG2dyntyping;
                                        config.fulltypes = cast(byte)cvtype;
                                        config.flags |= CFGalwaysframe;
                                    }
                                    goto case_g_done;

                        case 'h':   config.flags2 |= CFG2hdrdebug;
                                    configv.addlinenumbers = 1;
                                    if (config.fulltypes != CVNONE)
                                        break;
                                    goto case_s;

                        case 's':
                        case_s:
                                    config.fulltypes = cast(byte)cvtype;
                                    config.flags |= CFGalwaysframe;
                                    break;

                        case '3':
static if (CV3)
{
                                    cvtype = CVOLD;
                                    goto case_s;
}
else
{
                                    tx86err(EM_no_CV3); // CV3 is obsolete
                                    break;
}
                        case '4':   cvtype = CV4;
                                    goto case_s;
                        case '5':   cvtype = CVSYM;
                                    goto case_s;
                        case '6':
                        case_g6:
version (Win32)
                                    cvtype = CVTDB;
else
                                    cvtype = CVSYM;     // type database not supported

                                    config.fulltypes = cast(byte)cvtype;
                                    config.flags |= CFGalwaysframe;
                                    ftdbname = p + 1;
                                    if (p[1])
                                    {   ftdbname = p + 1;
                                        goto case_g_done;
                                    }
                                    goto case_s;

                        default:    goto badflag;
                    }
                }
            case_g_done:
}
                break;

            case 'G':
version (SCPP)
{
                    switch (*p)
                    {
                        case 'T':               // set data threshold
                            if (isdigit(p[1]))
                                config.threshold = atoi(p + 1);
                            else
                                config.threshold = THRESHMAX;
                            break;

                        default:
                            goto badflag;
                    }
}
                    break;

version (HTOD)
{
            case 'h':
                switch (*p)
                {
                    case 'c':           // skip C declarations as comments
                        config.htodFlags |= HTODFcdecl;
                        break;
                    case 'i':           // drill down into #include files
                        config.htodFlags |= HTODFinclude;
                        break;
                    case 's':           // drill down into system #include files
                        config.htodFlags |= HTODFsysinclude;
                        break;
                    case 't':           // drill down into typedefs
                        config.htodFlags |= HTODFtypedef;
                        break;
                    default:
                        goto badflag;
                }
                break;
}
            case 'H':                   /* precompiled headers          */
                    switch (*p)
                    {
                        case 'I':       /* read this header file in     */
                            if (!p[1])
                                cmderr(EM_filespec);
                            //list_append(&headers,p + 1);
                            break;
                        case 'S':
                            p++;
                            flags = CFG3igninc;
                            goto Lflags3;
                version (SCPP)
                {
                        case 'C':       // don't cache precompiled headers in memory
                            p++;
                            flags = CFG4cacheph;
                            goto Lflags4;

                        case 'D':       /* set directory for PH         */
                            ph_directory = p + 1;
                            break;

                        case 'N':
                            if (!p[1])
                                cmderr(EM_filespec);
                            break;

                        case 'H':
                            p++;
                            if (!p[0])
                                cmderr(EM_filespec);
                            fphreadname = p;
                            goto case_H0;

                        case 0:         /* use PH                       */
                        case_H0:
                            config.flags2 |= CFG2phuse;
                            config.flags2 &= ~(CFG2phgen | CFG2phauto | CFG2phautoy);
                            break;

                        case 'F':
                            p++;
                        case_HF:
                            fsymname = p;
                            config.flags2 |= CFG2phgen;
                            config.flags2 &= ~(CFG2phuse | CFG2phauto | CFG2phautoy);
                            break;

                        case 'M':
                static if (MMFIO)
                {
                            if (isxdigit(p[1]))
                                mmfiobase = cast(void *) strtoul(p + 1,null,16);
                            else
                                mmfiobase = null;          // use default
                }
                            break;

                        case 'P':
                static if (MMFIO)
                {
                            if (isxdigit(p[1]))
                                reservesize = strtoul(p + 1,null,10);
                            else
                                reservesize = 0;                // use default
                }
                            break;

                        case 'O':
                            flags = CFG2once;
                            p++;
                            goto Lflags2;

                        case 'X':
                            flags = CFG2phauto;
                            goto Lhx;
                        case 'Y':
                            flags = CFG2phautoy;
                        Lhx:
                            config.flags2 &= ~(CFG2phuse | CFG2phgen);
                            p++;
                            config.hxversion = 0;
                            if (isdigit(*p))
                            {   config.hxversion = cast(short)atoi(p);
                                config.flags2 |= flags;
                                break;
                            }
                            goto Lflags2;

                        case '-':       /* do not use PH                */
                        default:        /* ignore invalid flags         */
                            config.flags2 &= ~(CFG2phgen | CFG2phuse | CFG2phauto | CFG2phautoy | CFG2once);
                            break;
                }
                else
                {
                        default:
                            break;
                }
                    }
                break;

            case 'I':
                //addpath(p);
                break;

            case 'i':
                break;

/*
     1. -j switch is only work for source code scanning with MBCS text.
        For example, in Japan, trail byte of Kanji include 0x5c "\".

     2. Unicode conversion routine for L string needs correct code page
        information in order to use NLS conversion which OS provides.
        There are 2 types of Chinese language as you know, and each of
        those has own code conversion table. If sc++ supports code page
        flag, It is helpful for not only Asia but also Arabia and countries
        which have other code page.

     3. We might not need both of -j switch and code page switch

   Ryohei Ihara Jufuku
 */
            case 'j':
                if (!*p)
                    config.asian_char = 1;
                else
                    config.asian_char = cast(ubyte)(*p - '0' + 1);
                if (config.asian_char > 3)
                    goto badflag;               /* unrecognized parameter */
                break;

            case 'J':
                switch (*p)
                {   case 'u':                   /* chars are unsigned chars */
                        flags = CFG3ju;
                        p++;
                        warerr(WM.WM_obsolete, "-Ju".ptr, "unsigned char type".ptr);
                        goto Lflags3;
                    case 'm':                   /* relaxed type checking */
                        flags = CFG3autoproto | CFG3relax;      // turn off auto prototyping
                        p++;
                        goto Lflags3;
                    case 0:                     /* chars are unsigned   */
                        flags = CFGuchar;
                        goto Lflags1;
                    case 'b':                   // no empty base class optimization
                        flags = CFG4noemptybaseopt;
                        goto Lflags4x;
                    default:
                        goto badflag;
                }
                break;

            case 'l':
version (SCPP)
                flstname = on ? p : null;
version (HTOD)
                flstname = on ? p : null;
                break;

            case 'm':
                model = *p++;
                if (model == '3' && *p == '2')
                {
                    model = 'N';
                    ++p;
                }
                else if (model == '6' && *p == '4')
                {
                    model = 'A';
                    ++p;
                }
                model = cast(char)toupper(model);
                if (!strchr("TSMCRZLVFNPXA",model))
                    cmderr(EM_memmodels);               // bad memory model
                for (;; p++)
                {   switch (*p)
                    {
                        case 'd':
                            config.exe = EX_MZ;
                            continue;
                        case 'i':               // integer only
                            continue;           // ignore
                        case 'o':
                            config.exe = EX_OS1;
                            continue;
                        case 'u':
                            config.wflags |= WFloadds;
                            continue;
                        case 'w':
                            config.wflags |= WFssneds;
                            continue;
                        case 0:
                            break;
                        default:
                            goto badflag;
                    }
                    break;
                }
                break;

            case 'N':
                switch (*p++)
                {
                    case '_':
                        config.flags4 &= ~CFG4underscore;
                        break;

                    case 'c':
                        config.flags4 |= CFG4allcomdat;
                        break;

                    case 'C':
                        flags = CFG2comdat;
                        goto Lflags2;

                    case 'D':
                        flags = CFG4dllrtl;
                        goto Lflags4;

                    case 'F':
                        flags = CFG3nofar;
                        goto Lflags3;

                    case 'L':
                        if (*p)
                        {   configv.deflibname = p;
                            config.flags2 &= ~CFG2nodeflib;
                        }
                        else
                        {   flags = CFG2nodeflib;
                            goto Lflags2;
                        }
                        break;

                    case 'V':
                        config.flags |= CFGfarvtbls;
                        break;

                    case 't':
                        flags = CFG4oldtmangle;
                        goto Lflags4;

                    case 'T':           /* code segment name            */
                        configv.csegname = strupr(p);
                        break;

                    case 's':
                        config.flags3 |= CFG3strcod;
                        break;

                    case 'S':           /* new segs for each far glbl func */
                        config.flags |= CFGsegs;
                        break;
                    default:
                        goto badflag;
                }
                break;

            case 'o':
version (SPP)
{
                if (!*p)
                {   foutname = null;            // send output to stdout
                    break;
                }
                if (*p == '+' || *p == '-')     // if optimizer switch
                    break;
}
version (HTOD)
                fdmodulename = p;
else
                foutname = p;

                break;

static if (0)
{
            case 'O':
                flags = CFGeasyomf;
                goto Lflags1;
}

            case 'p':   flags = CFG3autoproto;  goto Lflags3;

            case 'P':
                switch (*p)
                {
                    case 0:
                        config.flags4 &= ~(CFG4stdcall);
                        flags = CFG4pascal;
                        goto Lflags4;

                    case 's':
                        flags = CFG4oldstdmangle;
                        p++;
                        goto Lflags4;

                    case 'z':
                        config.flags4 &= ~(CFG4pascal);
                        flags = CFG4stdcall;
                        p++;
                        goto Lflags4;

                    default:
                        break;
                }
                goto badflag;

            case 'r':
                flags = CFG3strictproto;        // strict prototyping
                goto Lflags3;

            case 'R':                   /* switch tables in code segment */
                flags = CFGromable;
                goto Lflags1;

            case 's':                   /* stack overflow checking      */
                flags = CFGstack;
                goto Lflags1;

            case 'S':                   /* always generate stack frame  */
                flags = CFGalwaysframe;
                goto Lflags1;

            case 'u':                   // https://digitalmars.com/ctg/sc.html#dashu
                switch_U = cast(bool)on;
                goto Lonec;

            case 'v':                   /* suppress non-essential msgs  */
                switch (*p)
                {   case 0:
                    case '2':
                        configv.verbose = 2;
                        break;
                    case '0':
                        configv.verbose = 0;
                        break;
                    case '1':
                    case '-':
                        configv.verbose = 1;
                        break;
                    default:
                        break;
                }
                break;

            case 'w':                   /* disable warnings             */
                if (isdigit(*p))
                    n = atoi(p);
                else if (*p == 'c')
                {   flags = CFG4warnccast;
                    goto Lflags4x;
                }
                else if (*p == 'x')
                {   flags = CFG2warniserr;
                    goto Lflags2x;
                }
                else
                {   n = -1;
                    config.flags |= ~(on - 1) & CFGnowarning;
                }
                n = isdigit(*p) ? atoi(p) : -1;
                err_warning_enable(n,on);
                break;

            case 'W':                   /* generate Windows prolog/epilog */
            {
                enum WFCOMMON = (WFwindows|WFthunk|WFincbp|WFexpdef|WFmacros);
                __gshared int[18] wftable =
                [   /* 1 */     WFCOMMON | WFds,
                    /* 2 */     WFCOMMON | WFds | WFreduced,
                    /* 3 */     WFCOMMON | WFss,
                    /* A */     WFwindows | WFthunk | WFmacros | WFreduced |
                                WFss,
                    /* D */     WFwindows | WFthunk | WFmacros | WFreduced |
                                WFdgroup | WFdll | WFexpdef | WFssneds,
                    /* abdef */ WFds, WFdsnedgroup, WFdgroup, WFexpdef, WFexport,
                    /* mrstu */ WFincbp, WFreduced, WFss, WFthunk, WFloadds,
                    /* vwx  */  WFsaveds, WFssneds, WFwindows,
                ];
                __gshared char[18+1] wfopts = "123ADabdefmrstuvwx";
                char onx;
                char c;

                static assert(wftable.length == wfopts.length - 1);
                switch (*p)
                {
                    case 0:             // -W is same as -W1
                        config.wflags = wftable[0];
                        break;
                    case '-':
                        if (!p[1])      // -W-
                        {   config.wflags = 0;
                            break;
                        }
                    goto default;
                    default:
                        onx = 1;
                        for (; (c = *p) != 0; p++)
                        {
                            if (c == '+')
                            {   onx = 1;
                                continue;
                            }
                            else if (c == '-')
                            {   onx = 0;
                                continue;
                            }
                            char* qx = strchr(wfopts.ptr,c);
                            if (!qx)
                                goto badflag;
                            int j = qx - wfopts.ptr;
                            if (j < 5)                  // if 123AD
                                config.wflags = 0;      // no previous baggage
                            if (onx)
                            {   config.wflags |= wftable[j];
                                switch (c)
                                {   case 'a':
                                        config.wflags &= ~(WFdgroup | WFss);
                                        break;
                                    case 'd':
                                        config.wflags &= ~(WFds | WFss);
                                        break;
                                    case 's':
                                        config.wflags &= ~(WFdgroup | WFds);
                                        break;
                                    default:
                                        break;
                                }
                            }
                            else
                                config.wflags &= ~wftable[j];
                        }
                        break;
                }
                break;
            }

            case 'x':
static if (0)
{
                flags = CFG2noerrmax;
                goto Lflags2;
}
else
{
                if (!isdigit(*p))
                    configv.errmax = 10000;
                else
                    configv.errmax = atoi(p);
                break;
}

            case 'X':
                switch (*p++)
                {   case 'D':   flags = CFG4tempinst;
                                goto Lflags4;
                    case 'E':   eecontext.EEexpr = p;
                                if (!*p)
                                    goto badflag;
                                break;
                    case 'I':   break;
                    case 'L':   eecontext.EElinnum = atoi(p);
                                if (!eecontext.EElinnum)
                                    goto badflag;
                                eecontext.EEpending = 1;
                                eecontext.EEcompile = 1;
                                break;
                    case 'N':   flags = CFG4notempexp;
                                goto Lflags4;
                    case 'T':   eecontext.EEtypedef = p;
                                if (!*p)
                                    goto badflag;
                                break;
                    default:
                        goto badflag;
                }
                break;

            case '0':
            case '2':
            case '3':
            case '4':
            case '5':
            case '6':   target = p[-1];
                        scheduler = *p;
                        break;

debug
{
            case '-':
                switch (*p)
                {
                    case 'a':   debuga = 1; break; // look at assembler
                    case 'b':   debugb = 1; break; // look at block optimization
                    case 'c':   debugc = 1; break; // look at code generated
                    case 'e':   debuge = 1; break; // dump EH info
                    case 'f':   debugf = 1; break; // fully print expressions
                    case 'r':   debugr = 1; break; // register allocation
                    case 's':   debugs = 1; break; // look at scheduling
                    case 't':   debugt = 1; break; // look at test points
                    case 'u':   debugu = 1; break; // suppress predefines
                    case 'w':   debugw = 1; break; // watch progress of cg
                    case 'x':   debugx = 1; break; // echo input to stderr
                    case 'y':   debugy = 1; break; // watch output, common subs
                    default:
                        break;
                }
                p++;
                break;
}

            default:
version (SPP)
                break;                  // ignore unrecognized flags

            badflag:
                cmderr(EM_bad_parameter,argv[i]);       // unrecognized parameter
                break;                  // ignore other switches

            Lflags1x:
                p++;
            Lflags1:
                pflags = cast(uint *)&config.flags;
                goto Lflags;

            Lflags2x:
                p++;
            Lflags2:
                pflags = cast(uint *)&config.flags2;
                goto Lflags;

            Lflags3x:
                p++;
            Lflags3:
                pflags = cast(uint *)&config.flags3;
                goto Lflags;

            Lflags4x:
                p++;
            Lflags4:
                pflags = cast(uint *)&config.flags4;
                goto Lflags;

            Lflags:
                if (on)
                    *pflags |= flags;
                else
                    *pflags &= ~flags;
                goto Lonec;

            Lonec:                      // one character switch
                if (!(p[0] == 0 || (p[0] == '-' && p[1] == 0)))
                    goto badflag;
                break;
        }
    }

    finname2 = finname;
    finname = file_getsource(finname);
    char *dotext = filespecdotext(finname);
    if (filespeccmp(dotext,ext_cpp.ptr) == 0 ||
        filespeccmp(dotext,".cxx") == 0)
    {
        config.flags3 |= CFG3cpp;       // ".cpp" extension, assume C++
    }
    debug printf("Compiling for %s\n",CPP ? "C++".ptr : "C".ptr);

version (SCPP)
{
    if (eecontext.EEcompile)
    {   configv.addlinenumbers = 0;
        config.flags2 |= CFG2nodeflib;
        if (model != 'N' ||             // only supported in WIN32
            !(config.flags & CFGalwaysframe) ||
            !eecontext.EEexpr ||
            !eecontext.EElinnum ||
            config.flags4 & CFG4optimized
           )
        {
            cmderr(EM_nodebexp);        // can't compile debugger expression
        }
    }
}
    config.flags2 ^= CFG2comdat;        // toggle state of flag
    config.flags3 ^= CFG3autoproto;     // toggle state of flag
    config.flags4 ^= CFG4cacheph | CFG4dependent;

    if (config.flags2 & CFG2phgen && config.fulltypes)
        config.flags2 |= CFG2hdrdebug;
    if (CPP)
    {
        config.linkage = LINK_CPP;
        config.flags3 &= ~CFG3relax;
        config.flags3 |= CFG3cppcomment | CFG3strictproto | CFG3digraphs;
        config.flags4 |= CFG4bool | CFG4wchar_t;        // default it to on
        config.flags4 |= CFG4alternate;                 // alternate keywords
        //config.flags4 |= CFG4dependent;               // dependent name lookup
        if (config.ansi_c)
        {   config.flags3 |= CFG3eh | CFG3rtti; // these are part of ANSI C++
            config.flags4 |= CFG4anew | CFG4alternate | CFG4forscope | CFG4adl |
                             CFG4dependent;
        }
    }
    else
    {
        config.linkage = LINK_C;
        config.flags &= ~CFGfarvtbls;
        config.flags3 &= ~(CFG3eh | CFG3rtti);
        config.flags4 &= ~(CFG4anew | CFG4bool | CFG4wchar_t |
                CFG4forscope | CFG4warnccast | CFG4adl | CFG4enumoverload);
        config.flags3 |= CFG3cppcomment;        // new for C99
        config.flags4 |= CFG4forscope;          // new for C99
        config.flags4 |= CFG4implicitfromvoid;
        if (!config.ansi_c)
            config.flags3 |= CFG3digraphs;
    }
    if (config.flags4 & CFG4pascal)     // if default to __pascal linkage
    {
        if (CPP)
        {
static if (MEMMODELS == 1)
            functypetab[LINK_CPP] = TYnpfunc;
else
{
            functypetab[LINK_CPP][Smodel] = TYnpfunc;
            functypetab[LINK_CPP][Mmodel] = TYfpfunc;
            functypetab[LINK_CPP][Cmodel] = TYnpfunc;
            functypetab[LINK_CPP][Lmodel] = TYfpfunc;
            functypetab[LINK_CPP][Vmodel] = TYfpfunc;
}
        }
        else
            config.linkage = LINK_PASCAL;
    }
    if (config.flags4 & CFG4stdcall)    // if default to __stdcall linkage
    {
        if (CPP)
        {
static if (MEMMODELS == 1)
            functypetab[LINK_CPP] = TYnsfunc;
else
{
            functypetab[LINK_CPP][Smodel] = TYnsfunc;
            functypetab[LINK_CPP][Mmodel] = TYfsfunc;
            functypetab[LINK_CPP][Cmodel] = TYnsfunc;
            functypetab[LINK_CPP][Lmodel] = TYfsfunc;
            functypetab[LINK_CPP][Vmodel] = TYfsfunc;
}
        }
        else
            config.linkage = LINK_STDCALL;
    }

version (_WINDLL)
{
}
else
{
    if (configv.verbose == 2)
        notice();
}

    switch (model)
    {
            case 'N':
static if (TARGET_LINUX)
                      config.exe = EX_LINUX;
else
                      config.exe = EX_WIN32;

                      config.defstructalign = 8 - 1; // NT uses 8 byte alignment
            Lx2:
                      config.memmodel = Smodel;
                      if (!target)              // if target not specified
                            target = '6';       // default to PentiumPro
                      else if (target < '3')
                            cmderr(EM_bad_iset,target,model);   // invalid instruction set
                      util_set32();
version (SCPP)
                      cod3_set32();

                      break;

            case 'A':
                      static if (TARGET_LINUX)
                      {
                          config.exe = EX_LINUX64;
                      }
                      else
                      {
                          config.exe = EX_WIN64;
                          config.objfmt = OBJ_MSCOFF;
                      }
                      config.fpxmmregs = true;
                      config.defstructalign = 8 - 1; // NT uses 8 byte alignment
                      config.flags |= CFGnoebp;
                      config.flags |= CFGalwaysframe;
                      config.flags |= CFGromable;       // put switch tables in code segment
                      config.target_cpu = TARGET_PentiumPro;
                      config.target_scheduler = config.target_cpu;
                      config.inline8087 = 1;
                      config.avx = 0;
                      config.memmodel = Smodel;
                      config.target_cpu = '6';
                      if (config.fulltypes == CV4)
                        config.fulltypes = CV8;
                      util_set64();
                      version (SCPP)
                          cod3_set64();

                      break;

static if (MEMMODELS > 1)
{
            case 'F': config.exe = EX_OS2;
                      goto Lx;

            case 'P': if (config.fulltypes != CV4)
                            // CV4 and Pharlap OMF are incompatible
                            config.flags |= CFGeasyomf;
                      config.exe = EX_PHARLAP;
                      goto Lx;

            case 'X': config.exe = EX_DOSX;

            Lx:       config.defstructalign = 4 - 1;    // align struct members on dwords
                      goto Lx2;

            case 'T': config.exe = EX_COM;
                      config.memmodel = Smodel;
                      goto L16;

            case 'R': config.exe = EX_RATIONAL;
                      goto Lr;

            case 'Z': config.exe = EX_ZPM;
            Lr:       if (!target)              // if target not specified
                        target = '2';           // default to 286
                      config.memmodel = Lmodel;
                      goto L16;

            case 0:
            case 'S': config.memmodel = Smodel; goto Ldos;
            case 'M': config.memmodel = Mmodel; goto Ldos;
            case 'C': config.memmodel = Cmodel; goto Ldos;
            case 'L': config.memmodel = Lmodel; goto Ldos;
            case 'V': config.memmodel = Vmodel; goto Ldos;
            Ldos:     if (config.exe == EX_OS1)
                      {
                        if (!target)
                            target = '2';       // default to 286
                      }
                      else
                        config.exe = EX_MZ;
            L16:
                      util_set16();
                      util_set16();
                      break;
}
            default:  assert(0);
    }

    if (!target)
        target = '0';
    config.target_cpu = cast(byte)((target == '6') ? TARGET_PentiumPro : target - '0');
    if (scheduler)
        config.target_scheduler = cast(byte)((scheduler == '6') ? TARGET_PentiumPro : scheduler - '0');
    else
        config.target_scheduler = config.target_cpu;

version (SPP)
{
    config.flags2 |= CFG2expand;        // doing "expanded" listing
    switch_E = 1;
}
else
    switch_E = (config.flags2 & CFG2expand) != 0;


    defalign = config.defstructalign;
    if (!switch_a)              // if no alignment specified
        switchalign = config.defstructalign;
    structalign = config.defstructalign = switchalign;

    /* Select either large or small pointers, based on memory model     */
    pointertype = LARGEDATA ? TYfptr : TYnptr;

    if (LARGEDATA)
    {
        if (!(config.wflags & (WFss | WFwindows)))
            config.wflags |= WFssneds;
static if (MEMMODELS > 1)
{
        if (config.memmodel == Vmodel)
            config.wflags |= WFincbp | WFthunk;
}
    }
    if (!(config.wflags & WFwindows))
        config.wflags |= WFexpdef;

    if (config.exe & EX_flat)
    {   if (!config.inline8087)
            config.inline8087 = 1;
        config.wflags &= ~WFssneds;     // SS == DS for flat memory models
        config.flags &= ~CFGromable;    // no switch tables in code segment
        config.flags3 |= CFG3nofar;
        if (config.exe & (EX_WIN32 | EX_WIN64 | EX_LINUX | EX_LINUX64))
            config.flags3 |= CFG3eseqds;        // ES == DS for flat memory models
    }
    if (!config.inline8087)
        config.flags4 &= ~CFG4fastfloat;
    if (!CPP && config.exe == EX_OS2 && config.linkage != LINK_PASCAL)
        config.linkage = LINK_STDCALL;
    if (config.flags3 & CFG3eh)
    {
        if (config.exe == EX_WIN32)
        {
            config.ehmethod = EHmethod.EH_WIN32;
        }
        else
        {
            config.ehmethod = EHmethod.EH_DM;
            config.flags |= CFGalwaysframe;
        }
    }
    else if (config.exe == EX_WIN32)
        config.ehmethod = EHmethod.EH_SEH;
    else
        config.ehmethod = EHmethod.EH_NONE;

    if (config.exe & (EX_LINUX | EX_LINUX64 | EX_OSX | EX_OSX64 | EX_FREEBSD | EX_FREEBSD64))
        config.flags4 |= CFG4wchar_is_long;
    if (I32)
        config.flags &= ~CFGstack;
    if (I16)
        config.flags2 &= ~CFG2stomp;

    // Autoprototype unprototyped functions so that stdcall name
    // mangling will work.
    if (!CPP && (config.exe == EX_WIN32 || config.exe == EX_WIN64) &&
        (config.flags4 & (CFG4stdcall | CFG4oldstdmangle)) == CFG4stdcall
       )
        config.flags3 |= CFG3autoproto; // turn on autoprototyping

    if (!dmcdll_first_compile())
        config.flags2 &= ~CFG2phautoy;

version (SPP)
{
}
else
{
    ph_init(mmfiobase, reservesize);    // reinitialize heaps taking ph options
                                        // into account
    pragma_init();
}

    if (config.flags3 & CFG3rtti && !(config.flags2 & CFG2phuse))
        list_append(&headers,cast(void*)"typeinfo.h".ptr);

    // Go back through argument list and handle arguments that allocate
    // memory in the PH.
    for (i = 1; i < argc; i++)          // for each argument
    {
        p = argv[i];                    // p -> argument string
        if (*p != '-' && *p != '/')
            continue;
        p++;
        switch (*p++)
        {
            case 'D':
                sw_d(p);
                break;
            case 'H':
                if (*p == 'I')
                    list_append(&headers,p + 1);
                if (*p == 'N')
                    list_prepend(&headers,p + 1);
                break;
            case 'I':
                addpath(&pathlist, p);
                break;
            case 'i':
                if (memcmp(p, "system=".ptr, 7))
                    goto badflag;
                addpath(&pathsyslist, p + 7);
                break;
            case 'X':
version (SCPP)
{
                if (CPP && template_getcmd(p))
                    goto badflag;
}
                break;
            default:
                break;
        }
    }

    if (!(config.flags2 & CFG2phgen))   // if not generating PH
        fsymname = null;                // then no sym output file
    if (!(config.flags3 & CFG3igninc))  // if not ignored
    {
        /* To see what paths gcc uses on linux,
         *    `gcc -print-prog-name=cc1` -v
         */
        addpath(&pathlist, getenv(INC_ENV));       // get path from environment
    }
    mergepaths(pathlist, pathsyslist, pathlist);
    linkage = config.linkage;

    if (config.flags & CFGuchar)        /* if chars are unsigned        */
    {   tytab[TYchar] |= TYFLuns;
        tyequiv[TYchar] = TYuchar;
    }

    setPredefinedMacros(switch_U, model, target, defalign, finname2);
}


/************************************************
 * Set all predefined macros.
 * https://digitalmars.com/ctg/predefined.html
 * Params:
 *      switch_U = true if -u switch is thrown
 *      model = 'T' if tiny memory model
 *      target = `0` == 8088, `2` == 286, `3` == 386, `4` == 486, `5` == P5, `6` == P6
 *      defalign = default struct alignment - 1
 *      finname2 = source file as specified on the command line
 */
private void setPredefinedMacros(bool switch_U, char model, char target, int defalign, char* finname2)
{
    /*****************************
     * Predefine a macro to 1
     */

    static void predefine(const(char)* name)
    {
        __gshared char[2] one = "1";
        if (*name == '_' || !config.ansi_c)
            defmac(name,one.ptr);
    }

    if (!switch_U)                      /* if didn't turn them off      */
    {
        {   const(char)* i8086  = "_M_I86?M";
            const(char)* modelc = "SMCLV";
            const(char)* m86    = "_M_I86";

            /* For compatibility with MSC       */
            predefine(m86);
            predefine(m86 + 1);

            char[9] i86 = void;
            strcpy(i86.ptr, i8086);
            i86[6] = (model == 'T') ? 'T' : modelc[config.memmodel];
            predefine(i86.ptr);
            predefine(i86.ptr + 1);

            if (target == '0')
            {   const(char)* m8086 = "_M_I8086";

                predefine(m8086);
                predefine(m8086 + 1);
            }
            else
            {   const(char)* cpu = "_M_I286";

                predefine(cpu);
                predefine(cpu + 1);
            }

            if (config.exe == EX_WIN64)
                predefine("_M_AMD64");

            if (config.exe == EX_LINUX64)
                predefine("__LP64__");
        }

        if (I32)
        {
            char[4] cpu = void;
            cpu[] = "300";
            if (target >= '4')
                cpu[0] = target;
            defmac("_M_IX86",cpu.ptr);
        }

        defmac("__SC__",VERSIONHEX);
        defmac("__ZTC__",VERSIONHEX);
        defmac("__DMC__",VERSIONHEX);

        defmac("__DMC_VERSION_STRING__", "\"Digital Mars C/C++ " ~ VERSION ~ "\"");
    }

    // Do operating system #defines
    switch (config.exe)
    {
        __gshared const(char)* msdos = "_MSDOS";
        __gshared const(char)* win32 = "_WIN32";

        case EX_DOSX:
        case EX_PHARLAP:        predefine("DOS386");    goto case_msdos;

        case EX_ZPM:
        case EX_RATIONAL:       predefine("DOS16RM");   goto case_msdos;

        case EX_OS2:
        case EX_OS1:            predefine("__OS2__");   break;

        case EX_WIN32:             predefine(win32);
                                predefine(win32 + 1);
                                predefine("__NT__");    break;

        case EX_WIN64:          predefine(win32);
                                predefine(win32 + 1);
                                predefine("_WIN64");
                                predefine("__NT__");    break;

        case EX_COM:
        case EX_MZ:
        case_msdos:             predefine(msdos);
                                predefine(msdos + 1);
                                break;

        case EX_LINUX:
        case EX_LINUX64:        predefine("linux");
                                predefine("__linux");
                                predefine("__linux__");
                                break;

        default:
            assert(0);
    }

    if (config.flags4 & CFG4dllrtl)
    {   predefine("_MT");
        predefine("_DLL");
    }

    if (config.flags & CFGuchar)        // if chars are unsigned
        predefine("_CHAR_UNSIGNED");

    if (config.flags3 & CFG3ju)         // if -Ju
        predefine("_CHAR_EQ_UCHAR");

    if (config.flags4 & CFG4anew)
        predefine("_ENABLE_ARRAYNEW");

    if (config.flags4 & CFG4bool)
        predefine("_BOOL_DEFINED");

    if (config.flags4 & CFG4wchar_t)
        predefine("_WCHAR_T_DEFINED");

    if (config.flags3 & CFG3rtti)
        predefine("_CPPRTTI");

    if (config.flags3 & CFG3eh)
        predefine("_CPPUNWIND");

    if (config.flags & CFGtrace)
        predefine("_DEBUG_TRACE");

    predefine("_STDCALL_SUPPORTED");    // supports __stdcall
    predefine("_PUSHPOP_SUPPORTED");    // supports #pragma pack(push)

    {
        char[2] isize = void;
        isize[0] = cast(char)(_tysize[TYint] + '0');
        isize[1] = 0;
        defmac("__INTSIZE".ptr,isize.ptr);
        isize[0] = cast(char)((defalign + 1) + '0');
        defmac("__DEFALIGN",isize.ptr);
        defmac("_INTEGRAL_MAX_BITS",(_tysize[TYint] == 4) ? "64" : "32");
    }

    if (config.wflags & WFmacros)
    {   if (config.wflags & WFwindows)
            predefine("_WINDOWS");
        if (config.wflags & WFdll)
            predefine("_WINDLL");
    }

    /* ANSI C macros to give memory model and cpu type  */
    {

        static if (MEMMODELS == 1)
        {
           __gshared const(char)*[MEMMODELS] modelmac =
                ["__SMALL__"
                ];
        }
        else
        {
           __gshared const(char)*[MEMMODELS] modelmac =
                ["__SMALL__"
                ,"__MEDIUM__","__COMPACT__","__LARGE__","__VCM__"
                ];
        }
        const(char)* i86 = "__I86__";
        char[2] i86value = void;

        fixeddefmac(modelmac[config.memmodel],one.ptr);
        i86value[0] = target;
        i86value[1] = 0;
        fixeddefmac(i86,i86value.ptr);

        if (config.inline8087)
            predefine("__INLINE_8087");
    }

    if (CPP || config.flags3 & CFG3cpp)
    {
        defmac("__cplusplus", "199711L");
    }
    else
    {
        // C99 6.10.8 Predefined macros
        fixeddefmac("__STDC_HOSTED__", "0");    // not a hosted implementation

        /* C89 __STDC_VERSION__ not defined     ANSI X3.159-1989
         * C90 __STDC_VERSION__ not defined     ISO/IEC 9899:1990
         * C95 __STDC_VERSION__ 199409L         ISO/IEC 9899-1:1994
         * C99 __STDC_VERSION__ 199901L         ISO/IEC 9899:1999
         */

        if (config.ansi_c == 95)
            fixeddefmac("__STDC_VERSION__", "199409L");
        if (!config.ansi_c || config.ansi_c == 99)
            fixeddefmac("__STDC_VERSION__", "199901L");

        if (!(config.flags4 & CFG4fastfloat))   // if not fast floating point
        {
version (linux)
{
            defmac("__STDC_IEC_559__", one.ptr);
            defmac("__STDC_IEC_559_COMPLEX__", one.ptr);
}
else
{
            fixeddefmac("__STDC_IEC_559__", one.ptr);
            fixeddefmac("__STDC_IEC_559_COMPLEX__", one.ptr);
}
        }
    }

    definedmac();
    fixeddefmac("__LINE__",cast(char *)null);
    fixeddefmac("__FILE__",cast(char *)null);
    fixeddefmac("__FUNC__",cast(char *)null);
    fixeddefmac("__COUNTER__",cast(char *)null);
    if (CPP)
    {   fixeddefmac("__FUNCTION__", "__func__");
        fixeddefmac("__PRETTY_FUNCTION__", "__func__");
    }
    else
    {   fixeddefmac("__FUNCTION__", cast(char *)null);
        fixeddefmac("__PRETTY_FUNCTION__",cast(char *)null);
    }
    predefine("__FPCE__");              // NCEG conformance
    fixeddefmac("__BASE_FILE__", filename_stringize(finname2)); // source file as specified on the command line

    if (!(config.flags4 & CFG4fastfloat))       // if not fast floating point
        predefine("__FPCE_IEEE__");     // IEEE conformance

    {   char[1+26+1] date = void;
        time_t t;
        char *px;

        time(&t);
        if (cast(int) t < 0)               // if bug in library
            t = 852152259;              // Jan 1, 1995
        px = ctime(&t);
        assert(px);
        sprintf(date.ptr,"\"%.6s %.4s\"",px+4,px+20);
        fixeddefmac("__DATE__",date.ptr);
        sprintf(date.ptr,"\"%.8s\"",px+11);
        fixeddefmac("__TIME__",date.ptr);
        sprintf(date.ptr,"\"%.24s\"",px);
        fixeddefmac("__TIMESTAMP__",date.ptr);
    }


    if (config.ansi_c)                           // if strict ANSI C/C++
        fixeddefmac("__STDC__",one.ptr);

    if (htod_running())
        fixeddefmac("__HTOD__", one.ptr);
}


/*****************************
 * Define macro.
 * Recognize:
 *      -D
 *      -Dmacro
 *      -Dmacro=string
 */

private void sw_d(char *p)
{ char *pstart;

    __gshared char[2] one = "1";

  if (!*p)
  {     defmac("DEBUG",one.ptr);
        config.flags5 |= CFG5debug | CFG5in | CFG5out | CFG5invariant;
        return;
  }
  pstart = p;
  while (isidchar(*p)) p++;
  if (p == pstart || p - pstart > IDMAX)
        cmderr(EM_dashD,"invalid identifier".ptr);

    switch (*p)
    {   case '=':
        case '#':
            *p = 0;
            p++;
            defmac(pstart,p);
            break;
        case 0:
            defmac(pstart,one.ptr);
            break;
        default:
            cmderr(EM_dashD,"need '=' after macro name".ptr);
    }
}

/*****************************
 * Break up a ';' delimited path into a list of paths
 * and append to *ppathlist
 */

version (Windows)
    enum PATH_SEP = ';';
else
    enum PATH_SEP = ':';

private void addpath(phstring_t *ppathlist, const(char)* q)
{   char *t;
    char  c;
    char *buf;
    char *s;
    char *p;

    //printf("addpath('%s')\n", q);
    if (q)
    {
        p = buf = mem_strdup(q);        // original is read-only
        do
        {   char instring = 0;

            while (isspace(*p))         // skip leading whitespace
                p++;
            s = p;
            for (t = p; 1; p++)
            {
                c = *p;
                switch (c)
                {
                    case '"':
                        instring ^= 1;  // toggle inside/outside of string
                        continue;

                    case PATH_SEP:
                        p++;
                        break;          // note that ; cannot appear as part
                                        // of a path, quotes won't protect it

                    case 0x1A:          // if some lame-brain editor left
                                        // a ^Z end-of-file in there
                    case 0:
                        break;

                    case '\r':
                        continue;       // ignore carriage returns

static if (0)
{
                    case ' ':
                    case '\t':          // tabs in filenames?
                        if (!instring)  // if not in string
                            break;      // treat as end of path
}
                    default:
                        *s++ = c;
                        continue;
                }
                break;
            }
            *s = 0;
            if (*t)                     // if path is not blank
                ppathlist.push(mem_strdup(t));
        } while (c);
        mem_free(buf);
    }
}

/*****************************************
 * Append pathsyslist to pathlist.
 * Remove any paths in pathlist that are also in pathsyslist.
 * Output:
 *      pathsysi
 */

private void mergepaths(phstring_t pathlist, phstring_t pathsyslist, ref phstring_t res)
{
    if (pathsyslist.length() == 0)
    {
        pathsysi = pathlist.length();
        res = pathlist;
        return;
    }

    phstring_t result = void;
    result.dim = 0;

    for (size_t i = 0; i < pathlist.length(); ++i)
    {
        for (size_t j = 0; j < pathsyslist.length(); ++j)
        {
            if (filespeccmp(pathlist[i], pathsyslist[j]) == 0)
                goto L1;
        }
        result.push(mem_strdup(pathlist[i]));
     L1: ;
    }
    pathsysi = result.length();
    for (size_t j = 0; j < pathsyslist.length(); ++j)
        result.push(mem_strdup(pathsyslist[j]));

    pathlist.free(&mem_freefp);
    pathsyslist.free(&mem_freefp);

    res = result;
}

/*********************************
 * Handle expansion of CFLAGS like SC driver does.
 */

version (SPP)
{

__gshared char *cflags;
__gshared char **newargv;

private void getcmd_cflags(int *pargc,char ***pargv)
{
    cflags = getenv("CFLAGS");
    if (!cflags)
    {   // Try again, but with a trailing space
        cflags = getenv("CFLAGS ");
    }
    if (cflags)
    {
        char **argv;
        list_t arglist = null;
        int nitems = 0;
        int i;

        // Create our own copy since we can't modify environment
        // BUG: doesn't handle quoted arguments
        cflags = mem_strdup(cflags);
static if (0)
{
        for (p1 = cflags; 1; p1 = p2 + 1)
        {   while (isspace(*p1))
                p1++;
            if (!*p1)
                break;
            p2 = p1;
            while (*p2 && !isspace(*p2))
                p2++;
            c = *p2;
            *p2 = 0;            // terminate substring
            list_append(&arglist,p1);
            nitems++;
            if (!c)
                break;
        }
}
else
{
        /* The logic of this should match that in setargv()
         * and it's taken from response_expand()
         */

        //printf("getcmd_cflags(cflags = '%s')\n", cflags);
        for (char *p = cflags; 1; p++)
        {
            char *d;
            char c,lastc;
            ubyte instring;
            int num_slashes,non_slashes;

            switch (*p)
            {
                case 26:      /* ^Z marks end of file      */
                case 0:
                    goto L2;

                case 0xD:
                case ' ':
                case '\t':
                case '\n':
                    continue;   // scan to start of argument

                default:      /* start of new argument   */
                    list_append(&arglist, p);
                    nitems++;
                    instring = 0;
                    c = 0;
                    num_slashes = 0;
                    for (d = p; 1; p++)
                    {
                        lastc = c;
                        c = *p;
                        switch (c)
                        {
                            case '"':
                                /*
                                    Yes this looks strange,but this is so that we are
                                    MS Compatible, tests have shown that:
                                    \\\\"foo bar"  gets passed as \\foo bar
                                    \\\\foo  gets passed as \\\\foo
                                    \\\"foo gets passed as \"foo
                                    and \"foo gets passed as "foo in VC!
                                 */
                                non_slashes = num_slashes % 2;
                                num_slashes = num_slashes / 2;
                                for (; num_slashes > 0; num_slashes--)
                                {
                                    d--;
                                    *d = '\0';
                                }

                                if (non_slashes)
                                {
                                    *(d-1) = c;
                                }
                                else
                                {
                                    instring ^= 1;
                                }
                                break;
                            case 26:
                        Lend:
                                *d = 0;      // terminate argument
                                goto L2;

                            case 0xD:      // CR
                                c = lastc;
                                continue;      // ignore

                            case ' ':
                            case '\t':
                                if (!instring)
                                {
                            case '\n':
                            case 0:
                                    *d = 0;      // terminate argument
                                    goto Lnextarg;
                                }
                                goto default;
                            default:
                            Ladd:
                                if (c == '\\')
                                    num_slashes++;
                                else
                                    num_slashes = 0;
                                *d++ = c;
                                break;
                        }
static if (0) // _MBCS
{
                        if (_istlead (c)) {
                            *d++ = *++p;
                            if (*(d - 1) == '\0') {
                                d--;
                                goto Lnextarg;
                            }
                        }
}
                    }
                break;
            }
        Lnextarg:
            ;
        }
    L2:
        ;

}
        argv = cast(char **) mem_malloc((char *).sizeof * (1 + nitems + *pargc));
        argv[0] = (*pargv)[0];
        for (i = 1; i <= nitems; i++)
            argv[i] = cast(char *)list_pop(&arglist);
        assert(arglist == null);
        for (; i < nitems + *pargc; i++)
            argv[i] = (*pargv)[i - nitems];
        *pargc += nitems;
        *pargv = argv;
static if (TERMCODE)
        newargv = argv;
    }
}

}

/***************************************
 * Terminate getcmd package.
 */

void getcmd_term()
{
static if (TERMCODE)
{
version (SPP)
{
    mem_free(cflags);
    mem_free(newargv);
    mem_free(fdepname);
}
else
{
    mem_free(fdepname);
    mem_free(flstname);
    mem_free(fsymname); fsymname = null;
    mem_free(ftdbname); ftdbname = null;
}
    mem_free(fdmodulename);
    mem_free(finname);
    list_free(&pathlist,&mem_freefp);
    list_free(&pathsyslist,&mem_freefp);
    list_free(&headers,FPNULL);
}
}

