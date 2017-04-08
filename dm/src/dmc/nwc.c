/**
 * Implementation of the
 * $(LINK2 http://www.digitalmars.com/download/freecompiler.html, Digital Mars C/C++ Compiler).
 *
 * Copyright:   Copyright (c) 1984-1998 by Symantec, All Rights Reserved
 *              Copyright (c) 2000-2017 by Digital Mars, All Rights Reserved
 * Authors:     $(LINK2 http://www.digitalmars.com, Walter Bright)
 * License:     Distributed under the Boost Software License, Version 1.0.
 *              http://www.boost.org/LICENSE_1_0.txt
 * Source:      https://github.com/DigitalMars/Compiler/blob/master/dm/src/dmc/nwc.c
 */

// Main program for compiler

#include        <stdio.h>
#include        <time.h>
#include        <string.h>
#include        <stdlib.h>
#ifdef __I86__
#include        <float.h>
#endif
#include        "cc.h"
#include        "token.h"
#include        "parser.h"
#include        "global.h"
#include        "el.h"
#include        "type.h"
#include        "code.h"
#include        "oper.h"
#include        "cpp.h"
#include        "exh.h"
#include        "cgcv.h"
#include        "scope.h"
#include        "outbuf.h"

static char __file__[] = __FILE__;      /* for tassert.h                */
#include        "tassert.h"

static char xyzzy[] = "written by Walter Bright";

static void nwc_outstatics();
STATIC void nwc_predefine();
STATIC type * getprototype(const char *,type *);
STATIC void getparamlst(type *,type *);
STATIC symbol * anonymous(Classsym *,enum SC);
STATIC void nwc_based(void);
void output_func(void);

Declar gdeclar;

static long linkage_kwd;
static int msbug;               // used to emulate MS C++ bug

int readini(char *argv0,char *ini);

#if TX86
/*******************************
 * Main program.
 */

int main(int argc,char *argv[])
{ list_t headerlist;

  argv0 = argv[0];                      // save program name
#if SPP
  mem_init();
  mem_setexception(MEM_CALLFP,err_nomem);
#if !_WINDLL && _WIN32
    readini(argv0,"sc.ini");            // read initialization file
#endif
  list_init();
  pragma_init();
  getcmd(argc,argv);                    /* process command line         */
  file_iofiles();
  token_init();                         // initialize tokenizer tables
  insblk((unsigned char *)finname,BLfile,NULL,FQtop,NULL);      // install top level block
  if (headers)
  {     insblk((unsigned char *)list_ptr(headers),BLfile,NULL,FQcwd | FQpath,NULL);
        list_pop(&headers);
  }

  while (stoken() != TKeof)
        ;

  pragma_term();
  file_term();
  fclose(fout);
#if TERMCODE                            /* dump this to speed up compile */
  blklst_term();
  token_term();
  mem_free(eline);
  getcmd_term();
  list_term();
#endif

#else

#if __I86__ && __DMC__
  {     extern int __cdecl _8087;
        _8087 = 0;                      /* no fuzzy floating point      */
                                        /* (use emulation only)         */
  }
#endif
#if _WIN32 && !_WINDOWS
    // Set unbuffered output in case output is redirected to a file
    // and we need to see how far it got before a crash.
    stdout->_flag |= _IONBF;
#endif
  mem_init();
  mem_setexception(MEM_CALLFP,err_nomem);
  list_init();
  vec_init();
  cod3_setdefault();
  getcmd(argc,argv);                    // process command line
  file_iofiles();
  token_init();                         // initialize tokenizer tables
  pstate.STinitseg = 1;                 // default is USER segment

  pstate.STsequence = 1;                // first Symbol will be #1
  pstate.STmaxsequence = ~0;            // accept all Symbol's

  cpp_init();
  pstate.STgclass = SCglobal;
  except_init();                        // exception handling code
  Outbuffer *objbuf = new Outbuffer();
#if HTOD
  htod_init(fdmodulename);
#else
#if _WIN32
  char *p = (config.exe & EX_dos) ? file_8dot3name(finname) : NULL;
  if (!p || !*p)
        p = finname;
  objmod = Obj::init(objbuf, p, configv.csegname);
  Obj::initfile(p, configv.csegname, NULL);
//  free(p);
#else
  objmod = Obj::init(objbuf, finname, configv.csegname);
  Obj::initfile(finname,configv.csegname);
#endif
#endif
  PARSER = 1;
  el_init();
  block_init();
  type_init();
  rtlsym_init();
  insblk((unsigned char *)finname,BLfile,NULL,FQtop,NULL);      // install top level block

  cstate.CSpsymtab = &globsym;
  createglobalsymtab();                 // create top level symbol table
  headerlist = headers;
  if (headers)
  {     pragma_include((char *)list_ptr(headers),FQcwd | FQpath);
        headers = list_next(headers);
  }
  nwc_predefine();                      // any initial declarations
  stoken();
  ext_def(0);                           // do external_definitions
#if !HTOD
  if (pstate.STflags & PFLhxgen)
      ph_autowrite();
  template_instantiate();
  cpp_build_STI_STD();                  // do static ctors/dtors
  output_func();                        /* write out any more functions */
  nwc_outstatics();
  if (ANSI && !CPP &&
      !(pstate.STflags & (PFLextdef | PFLcomdef)) &&
      !(config.flags2 & CFG2phgen))     // and not generating precompiled header
        synerr(EM_no_ext_def);          // need external definition
  if ((config.flags2 & (CFG2hdrdebug | CFG2noobj)) == CFG2hdrdebug)
        symbol_gendebuginfo();          // generate debug info for global symbols
  Obj::termfile();                       // fix up and terminate object file
  Obj::term(NULL);
  if (!errcnt)
  {
        objfile_close(objbuf->buf, objbuf->p - objbuf->buf);
  }
  if (fsymname)
  {
        assert(config.flags2 & CFG2phgen);
        symboltable_clean((symbol *)scope_find(SCTglobal)->root);
        symboltable_balance((symbol **)&scope_find(SCTglobal)->root);
        if (!CPP)
        {   symboltable_clean((symbol *)scope_find(SCTglobaltag)->root);
            symboltable_balance((symbol **)&scope_find(SCTglobaltag)->root);
        }
        ph_write(fsymname,2);           // write precompiled header
  }
#endif
  pragma_term();
  file_term();
#if HTOD
  htod_term();
  if (fdmodule)
        fclose(fdmodule);
#else
  if (flst)
        fclose(flst);
#endif

#if TERMCODE                            /* dump this to speed up compile */
  assert(pstate.STinparamlist == 0);
  if (!(config.flags2 & (CFG2phgen | CFG2phuse | CFG2phauto | CFG2phautoy)))
  {
        debug(printf("deletesymtab\n"));
        deletesymtab();                 /* for drill & error checking   */
        file_progress();
        symbol_free(cstate.CSlinkage);
        debug(printf("freesymtab\n"));
        freesymtab(globsym.tab,0,globsym.top); /* free symbol table     */
        symtab_free(globsym.tab);
        except_term();
        cpp_term();
        file_progress();
        iasm_term();                    // terminate inline assembler
        symbol_term();
        go_term();
        cgcs_term();
        code_term();
        file_progress();
        rtlsym_term();
        type_term();
        token_term();
        mem_free(eline);
        getcmd_term();
        vec_term();
        file_progress();
        el_term();
        file_progress();
        dt_term();
        blklst_term();
        list_free(&headerlist,FPNULL);
        list_term();
        scope_term();
        block_term();
  }
#endif /* TERMCODE */
#endif /* SPP */

  if (errcnt)                           /* if any errors occurred       */
        err_exit();

#if TERMCODE
#if SPP
  mem_free(foutname);
  mem_term();
#else
  objfile_term();
  if (!(config.flags2 & (CFG2phgen | CFG2phuse | CFG2phauto | CFG2phautoy)))
        mem_term();
#endif
#endif

#if !SPP
    ph_term();
#endif

#if !_WINDLL
    if (configv.verbose == 2)
    {
        clock_t stime = clock();

#if SPP
        printf("SPP complete. Time: %ld.%02ld seconds\n",
                stime/100,stime%100);
#else
        printf("%s complete. Time: %ld.%02ld seconds\n",
                COMPILER, stime/100, stime%100);
#if 0
        Offset(DATA) += UDoffset; // + TDoffset;
        printf(
            "%s complete. Code: 0x%04lx (%lu) Data: 0x%04lx (%lu) Time: %ld.%02ld seconds\n",
            COMPILER,
            (long)Offset(cseg),(long)Offset(cseg),
            (long)Offset(DATA),(long)Offset(DATA),
            stime / 100,stime % 100);
#endif
#endif
    }
#endif
    errmsgs_term();
#if _WIN32
    os_term();
#endif
    return EXIT_SUCCESS;
}

#else

void _cdecl main_parser(int argc,char **argv)
{
#if SPP
  list_init();
  pragma_init();
  getcmd(argc,argv);                    /* process command line         */
  file_iofiles();
  insblk((unsigned char *)finname,BLfile,NULL,FQtop,NULL);      // install top level block
  if (headers)
  {     insblk(list_ptr(headers),BLfile,NULL,FQcwd | FQpath,NULL);
        list_pop(&headers);
  }

  while (stoken() != TKeof)
        ;

  pragma_term();

#else

  ph_init();                            /* assume precompiled header memory */
  list_init();
  vec_init();
#if DEMO
  dbg_printf("Digital Mars C/C++ Demo Compiler\n");
#endif
  pragma_init();
  pstate.STgclass = SCglobal;
  getcmd(argc,argv);                    /* process command line         */
  file_iofiles();
  el_init();
  PARSER = 1;
  type_init();
  insblk((unsigned char *)finname,BLfile,NULL,FQtop,NULL);      // install top level block

  cstate.CSpsymtab = &globsym;
  createglobalsymtab();                 /* create top level symbol table */
#if TX86
  if (config.flags2 & (CFG2phauto | CFG2phautoy))       // if automatic precompiled headers
        ph_auto();
#endif
  if (headers)
  {     pragma_include((char *)list_ptr(headers),FQcwd | FQpath);
        list_pop(&headers);
  }
  nwc_predefine();                      /* any initial declarations     */
  stoken();
  ext_def(0);                           /* do external_definitions      */
  if (pstate.STflags & PFLhxgen)
      ph_autowrite();
  template_instantiate();
  cpp_build_STI_STD();                  // do static ctors/dtors
  output_func();                        /* write out any more functions */
#endif /* SPP */
  if (errcnt)                           /* if any errors occurred       */
        err_exit();
}
#endif


#if !SPP

#if TX86
/***********************************
 * 'Predefine' a number of symbols.
 */

STATIC void nwc_predefine()
{
    static char text[] =
"extern \"C++\"\
{ void  * __cdecl operator new(unsigned),\
        __cdecl operator delete(void *),"
        "* __cdecl __vec_new(void *,unsigned,int,void *(__pascal *)(void),int (__pascal *)(void)),"
        "* __cdecl __vec_ctor(void *,unsigned,int,void *(__pascal *)(void),int (__pascal *)(void)),"
        "* __cdecl __vec_cpct(void *,unsigned,int,void *(__pascal *)(void),int (__pascal *)(void),void *),"
        "__cdecl __vec_delete(void *,int,unsigned,int (__pascal *)(void)),\
        __cdecl __vec_dtor(void *,unsigned,int,int (__pascal*)(void)),\
        __cdecl __vec_invariant(void *,unsigned,int,int (__pascal*)(void)),\
        __cdecl __eh_throw(const char *,int(__pascal*)(),unsigned,...),\
        __cdecl __eh_rethrow(void);\
extern  void * (__cdecl * __eh_newp)(unsigned);\
typedef int (*__mptr)();\
__mptr __cdecl __genthunk(unsigned,unsigned,__mptr);\
"
"struct __eh_cv { volatile void *p; ~__eh_cv(); };"
"void * __cdecl __rtti_cast(void *,void *,const char *,const char *,int);"
"}"
"\n";
/*struct __mptr { short d; short i; int (*f)(); };\n"; */

#ifdef DEBUG
    if (!debugu)
#endif
    {
        if (CPP)
        {
            static unsigned char text2[] = "extern \"C\" { int __cdecl __far _fatexit(void(__cdecl __far *)());}\n";
            static unsigned char text3[] = "extern \"C\" { int __cdecl _fatexit(void(__cdecl *)());}\n";

            insblk2((intsize == 4) ? text3 : text2,BLrtext);
            insblk2((unsigned char *) text,BLrtext);
        }
#if NTEXCEPTIONS
        if (config.exe == EX_WIN32)
        {
#if NTEXCEPTIONS == 1
            static char text4[] =
                "struct __nt_context { int prev; int handler; int stable; int sindex; int ebp; int info; int esp; };\n";
#else
            static char text4[] =
                "struct __nt_context { int esp; int info; int prev; int handler; int stable; int sindex; int ebp; };\n";
#endif

            insblk2((unsigned char *)text4,BLrtext);
        }
#endif
        if (config.flags4 & CFG4anew)
        {   static char text5[] =
                "extern \"C++\" { void * __cdecl operator new[](unsigned),"
                "__cdecl operator delete[](void *); }";

            insblk2((unsigned char *)text5,BLrtext);
        }
    }
}

#endif

/***************************
 * Evaluate external_definitions.
 * This is called once for C, and recursively for C++ (to implement
 * block structured linkage specifications).
 * Input:
 *      nest    1       if recursive call
 *              2       within a namespace
 */

void ext_def(int nest)
{
    linkage_t link_current = linkage;

L1:
    while (tok.TKval != TKeof)
    {   int flag;

        pstate.STlastfunc = NULL;
        if (tok.TKval == TKrcur)
        {   if (nest)
                break;
            synerr(EM_id_or_decl);      // identifier expected
            stoken();
            continue;
        }
        flag = 0;
        level = 0;                      /* top level definitions        */
        linkage = link_current;         /* reset before each declaration */
        linkage_spec = nest & 1;        // !=0 if linkage specification

        if (tok.TKval == TK_debug)
        {
            if (config.flags5 & CFG5debug)
            {
                stoken();
#if 0 // disallow __debug { declarations }
                if (tok.TKval == TKlcur)        // if start of scope
                {   linkage_t save;

                    stoken();
                    save = link_current;
                    ext_def(nest | 1);
                    link_current = save;
                    chktok(TKrcur,EM_rcur);
                    continue;
                }
#endif
            }
            else
            {
                token_free(token_funcbody(TRUE));
                stoken();
                continue;
            }
        }

        if (CPP)
        {
            if (tok.TKval == TKtemplate)
            {
                template_declaration(NULL, SFLnone);    // not a member template
                continue;
            }

            // CPP98 7.3.1-4
            // Namespaces can only appear in global or namespace scope.
            if (tok.TKval == TKnamespace)
            {   namespace_definition();
                continue;
            }

            // Parse things like: extern "C" extern "C" func();
            while (tok.TKval == TKextern)
            {
                stoken();
                if (tok.TKval == TKstring)
                {
                    static char *linkagetab[] =
                    {
    #if TX86
                          "C","C++","Pascal","FORTRAN","syscall","stdcall",
                          "D"
    #else
                          TARGET_LINKAGETAB
    #endif
                    };
                    linkage_t i;
                    targ_size_t len;
                    char *p;

                    p = combinestrings(&len);
                    assert(arraysize(linkagetab) == LINK_MAXDIM);
                    for (i = LINK_C; 1; i = (linkage_t) (i + 1))
                    {   if (i == LINK_MAXDIM)
                        {   cpperr(EM_linkage_specs,p); // undefined linkage specification
                            i = LINK_CPP;
                            break;
                        }
                        if (strcmp(linkagetab[i],p) == 0)
                            break;
                    }
                    mem_free(p);
    #if TX86
                    // If -P switch used, then extern "C" is treated
                    // as if it was extern "Pascal"
                    if (i == LINK_C)
                    {   if (config.flags4 & CFG4pascal)
                            i = LINK_PASCAL;
                        if (config.flags4 & CFG4stdcall)
                            i = LINK_STDCALL;
                    }
    #endif
                    if (tok.TKval == TKlcur)    /* if start of scope            */
                    {   linkage_t save;

                        stoken();
                        save = link_current;
                        linkage = i;
                        ext_def(nest | 1);
                        link_current = save;
                        chktok(TKrcur,EM_rcur);
                        goto L1;
                    }
                    linkage_spec = 1;   // we're now using a linkage spec
                    linkage = i;                // just for this declaration
                    /* extern "C" is not a storage class specifier, it is only a
                       linkage specifier see ARM p.118 and try
                            extern "C" typedef int (*CF)(int);
                       Reread starting with p. 117, if inside { } then defined
                       otherwise declaration extern "C" char x[];
                       compiled as definition instead of declaration
                       Needs more work to fix typedef problem
                     */
                    flag = 2;           // force SCextern storage class
                }
                else
                {   token_unget();
                    tok.TKval = TKextern;
                    break;
                }
            }
        }

        if (config.flags2 & (CFG2phauto | CFG2phautoy))
            ph_testautowrite();

        declaration(flag);              // declare stuff of type
        output_func();                  /* write any queue'd functions  */
    }
}

/******************* THUNKS *************************/

typedef struct Thunk
{   symbol *sfunc;
    symbol *sthunk;
    targ_size_t d;
    targ_size_t d2;
    int i;
} Thunk;

/*********************************
 * Hydrate/dehydrate a thunk.
 */

#if HYDRATE
void thunk_hydrate(Thunk **pt)
{   Thunk *t;

    t = (Thunk *) ph_hydrate(pt);
    if (t)
    {   symbol_hydrate(&t->sfunc);
        symbol_hydrate(&t->sthunk);
    }
}
#endif

#if DEHYDRATE
void thunk_dehydrate(Thunk **pt)
{   Thunk *t;

    t = *pt;
    if (t && !isdehydrated(t))
    {
        ph_dehydrate(pt);
        symbol_dehydrate(&t->sfunc);
        symbol_dehydrate(&t->sthunk);
    }
}
#endif

/*********************************
 * Create thunk.
 * Input:
 *      s       original function symbol
 *      d       offset to this for that function
 *      i       offset into vtbl (-1 for non-virtual functions)
 *      d2      offset from this to vptr member (ignored if i == -1)
 * Returns:
 *      symbol for thunk
 */

symbol *nwc_genthunk(symbol *s,targ_size_t d,int i,targ_size_t d2)
{
    symbol *sthunk;
    Thunk *t;
    symlist_t sl;

    //dbg_printf("nwc_genthunk('%s', d=x%lx, i=x%x, d2=x%lx)\n",s->Sident,d,i,d2);

    // See if we can use an existing thunk
    for (sl = s->Sfunc->Fthunks; sl; sl = list_next(sl))
    {   sthunk = list_symbol(sl);
        t = sthunk->Sfunc->Fthunk;
        if (t->sfunc == s &&
            t->d == d &&
            t->i == i &&
            t->d2 == d2)
            return sthunk;
    }

    sthunk = symbol_generate(SCstatic,s->Stype);
    sthunk->Sflags |= SFLimplem;
    assert(sthunk->Sfunc);

    /*sthunk->Sfunc->Fflags |= Fpending;*/
    t = (Thunk *) mem_calloc(sizeof(Thunk));
    t->sfunc = s;
    t->sthunk = sthunk;
    t->d = d;
    t->i = i;
    t->d2 = d2;
    sthunk->Sfunc->Fthunk = t;
#if TX86
    list_append(&s->Sfunc->Fthunks,sthunk);
#endif
    return sthunk;
}


/******************************
 * Write out any functions queued for being output
 */

static list_t nwc_funcstowrite = NULL;  /* list of function symbols to write out */

void output_func()
{
    while (nwc_funcstowrite)
    {   symbol *s;
        func_t *f;
        Thunk  *t;

        s = list_symbol(nwc_funcstowrite);
        symbol_debug(s);
        list_subtract(&nwc_funcstowrite,s);
        assert(tyfunc(s->Stype->Tty));
        f = s->Sfunc;
        t = (f->Fflags & Finstance) ? NULL : f->Fthunk;
        if (t)                          /* if this is a thunk           */
        {
            unsigned p = 0;
            outthunk(t->sthunk,t->sfunc,p,pointertype,t->d,t->i,t->d2);
            //mem_free(t);
            //f->Fthunk = NULL;
        }
        else
        {
            writefunc(s);
        }
    }
}

/******************************************
 * Write out any remaining statics.
 */

static list_t nwc_staticstowrite = NULL;        // list of statics to write out

static void nwc_outstatics()
{
    //printf("nwc_outstatics()\n");
    for (list_t sl = nwc_staticstowrite; sl; sl = sl->next)
    {
        symbol *s;

        s = list_symbol(sl);
        symbol_debug(s);
        //printf("s = '%s', Sdt = %p\n", s->Sident, s->Sdt);
        if (s->Sxtrnnum == 0 &&
            s->Sdt ||
            (s->Sclass != SCstatic &&
            s->Sflags & SFLwasstatic &&
            !(s->Sflags & SFLlivexit)))
        {
            // Put it in BSS
            s->Sclass = SCstatic;
            s->Sfl = FLunde;
            if (!s->Sdt)
            {
                DtBuilder dtb;
                dtb.nzeros(type_size(s->Stype));
                s->Sdt = dtb.finish();
            }
            outdata(s);
            searchfixlist(s);
        }
    }

}

void nwc_addstatic(symbol *s)
{
    //dbg_printf("nwc_addstatic('%s')\n",s->Sident);
    list_prepend(&nwc_staticstowrite, s);
}

/***************************
 * We'll need to write out this function when it arrives.
 */

void nwc_mustwrite(symbol *sfunc)
{
    //dbg_printf("nwc_mustwrite('%s')\n",sfunc->Sident);
    //symbol_print(sfunc);
    assert(tyfunc(sfunc->Stype->Tty));
#if linux || __APPLE__ || __FreeBSD__ || __OpenBSD__
        if (sfunc->Sfunc->Fflags3 & Fnowrite)
            return;
#endif
    if (sfunc->Sflags & SFLimplem)              /* if body already read in */
    {
        queue_func(sfunc);                      /* send it out          */
    }
    else
    {
        sfunc->Sfunc->Fflags |= Fmustoutput;    /* output it when we see it */
    }
}

/***************************
 * Queue function for output.
 */

void queue_func(symbol *sfunc)
{   func_t *f;

    //printf("queue_func('%s')\n", sfunc->Sident);
    assert(sfunc && tyfunc(sfunc->Stype->Tty));
    symbol_debug(sfunc);
    f = sfunc->Sfunc;
    f->Fflags &= ~Fmustoutput;

    // If not already output and not on nwc_funcstowrite list
    if ((f->Fflags & (Foutput | Fpending)) == 0)
    {   list_append(&nwc_funcstowrite,sfunc);
        f->Fflags |= Fpending;
    }
}

/**************************
 * Save local symbol table for function.
 */

void savesymtab(func_t *f)
{
  assert(f->Flocsym.symmax == 0);
  f->Flocsym.top = globsym.top;

#ifdef DEBUG
    if (debugy)
        dbg_printf("savesymtab(), globsym.top = %d\n",globsym.top);
#endif

  if (globsym.top)              /* if there are local symbols   */
  {     /* Save local symbol table      */
        f->Flocsym.symmax = globsym.top;
        f->Flocsym.tab = symtab_malloc(f->Flocsym.symmax);
        memcpy(f->Flocsym.tab,&globsym.tab[0],
            sizeof(symbol *) * f->Flocsym.symmax);
        memset(&globsym.tab[0],0,sizeof(symbol *) * globsym.top);
        globsym.top = 0;
  }
}

/***************************
 * Get the type_specifier.
 * Note:
 *      The Tcounts ARE incremented.
 *
 * Output:
 *      *ptyp_spec -> our data type
 * Returns:
 *      0       if we used the default
 *      1       if a type specifier
 *      2       if a storage class
 *      4       if a cdecl, pascal, or declspec
 */

int type_specifier(type **ptyp_spec)
{
    return declaration_specifier(ptyp_spec,NULL,NULL);
}

/*****************************
 * Get the declaration-specifier.
 * Output:
 *      *ptyp_spec      data type (the Tcounts are incremented)
 *      *pclass         storage class
 *      *pclassm        mask of storage class keywords seen
 * Returns:
 *      same as type_specifier()
 */

enum TKW
{
    TKWchar     = 1,
    TKWsigned   = 2,
    TKWunsigned = 4,
    TKWshort    = 8,
    TKWint      = 0x10,
    TKWlong     = 0x20,
    TKWllong    = 0x40,
    TKWfloat    = 0x80,
    TKWdouble   = 0x100,
    TKWldouble  = 0x200,
    TKWtag      = 0x400,        // TKunion, TKstruct, TKclass, TKenum
    TKWident    = 0x800,        // TKident
    TKWvoid     = 0x1000,
    TKWbool     = 0x4000,
    TKWwchar_t  = 0x8000,
    TKWimaginary = 0x10000,
    TKWcomplex   = 0x20000,
    TKWchar16    = 0x40000,
    TKWdchar     = 0x80000,
    TKWdecltype  = 0x100000,
};

#define SCWstatic       mskl(SCstatic)
#define SCWextern       mskl(SCextern)
#define SCWauto         mskl(SCauto)
#define SCWregister     mskl(SCregister)
#define SCWtypedef      mskl(SCtypedef)
#define SCWinline       mskl(SCinline)
#define SCWoverload     mskl(SCoverload)
#define SCWthread       mskl(SCthread)

#define SCWvirtual      mskl(SCvirtual)

int declaration_specifier(type **ptyp_spec, enum SC *pclass, unsigned long *pclassm)
{ symbol *s;
  Classsym *sclass0;
  tym_t modifiers = 0;
  tym_t modifiers2 = 0;
  tym_t modifiersx;
  int dependent = 0;
  type *t;
  int result;
  int tkw = 0;
  int tkwx;
  int sawtypename = 0;

  enum SC sc_specifier;
  unsigned long scw = pclassm ? *pclassm & SCWtypedef : 0;
  unsigned long scwx;

  /* Note, setting TkStrtSrcpos here does NOT get the true first line of the function */
  /* header. For the Apple PPC Debugger getting the true first line is very important.*/
  /* Thus, for HYBRID, at least we set TkStrtSrcpos in ext_def() (and elsewhere for   */
  /* inlines and templates).                                                      ILR */

  msbug = 0;
#if TARGET_LINUX || TARGET_OSX || TARGET_FREEBSD || TARGET_OPENBSD || TARGET_SOLARIS
  if (tok.TKval == TK_extension)
      stoken();                         // skip over __extension__ keyword
  else if (tok.TKval == TK_attribute)
  {
      int attrtype;
      int mod = getattributes(NULL,FALSE,&attrtype);
      if (attrtype & ATTR_LINKMOD)
          modifiersx = mod & ATTR_LINK_MODIFIERS;
                                        // not sure how precedence works
#if DEBUG
      attrtype |= ~ATTR_LINKMOD;
      assert(ATTR_CAN_IGNORE(attrtype));
#endif
  }
#endif

L2:
  switch (tok.TKval)
  {
        case TKstatic:
                                scwx = SCWstatic;
                                goto L9;
        case TKextern:
                                scwx = SCWextern;
                                if (CPP && level == -1)         // if class member
                                    pstate.STclasssym = NULL;   // not at class scope
                                goto L9;
        case TKauto:            scwx = SCWauto;         goto L9;
        case TKregister:        scwx = SCWregister;     goto L9;
        case TKtypedef:         scwx = SCWtypedef;      goto L9;
        case TKoverload:        scwx = SCWoverload;     goto L9;
        case TKvirtual:         scwx = SCWvirtual;      goto L9;
        case TKinline:
                                scwx = SCWinline;
                                goto L9;
        L9:
            stoken();

            if (scw & scwx)
                synerr(EM_storage_class,"");            // bad storage class
            scw |= scwx;
            goto L2;

        case TKchar:            tkwx = TKWchar;         goto L6;
        case TKchar16_t:        tkwx = TKWchar16;       goto L6;
        case TKchar32_t:        tkwx = TKWdchar;        goto L6;
        case TKsigned:          tkwx = TKWsigned;       goto L6;
        case TKunsigned:        tkwx = TKWunsigned;     goto L6;
        case TKshort:           tkwx = TKWshort;        goto L6;
        case TKint:             tkwx = TKWint;          goto L6;
        case TKlong:            tkwx = TKWlong;         goto L6;
        case TK_int64:          tkwx = TKWllong;        stoken(); goto L11;
        case TKfloat:           tkwx = TKWfloat;        goto L6;
        case TKdouble:          tkwx = TKWdouble;       goto L6;
        case TK_Imaginary:      tkwx = TKWimaginary;    goto L6;
        case TK_Complex:        tkwx = TKWcomplex;      goto L6;
        case TKvoid:            tkwx = TKWvoid;         goto L6;
        case TKbool:            tkwx = TKWbool;         goto L6;
        case TKwchar_t:         tkwx = TKWwchar_t;      goto L6;
        L6:
            stoken();
        L8:
            if (tkw & tkwx & TKWlong)   // recognize "long long"
            {   tkw &= ~TKWlong;
                tkwx = TKWllong;
        L11:    if (intsize == 2)
                    synerr(EM_no_longlong);     // long long not supported
            }
            if (tkw & tkwx)
                synerr(EM_illegal_type_combo);  // illegal combination of types
            else if (tkw & (TKWtag | TKWdecltype))
            {   synerr(EM_bad_type_comb);       // missing ';'
                tkw = tkwx;
            }
            else
                tkw |= tkwx;
            goto L2;

        case TK_unaligned:  modifiersx = mTYunaligned;  goto L3;
        case TKconst:       modifiersx = mTYconst;      goto L3;
        case TKvolatile:    modifiersx = mTYvolatile;   goto L3;
        case TKrestrict:    modifiersx = mTYrestrict;   goto L3;
        case TKthread_local:    modifiersx = mTYthread; goto L3;
#if TX86
        case TK_declspec:   modifiersx = nwc_declspec(); goto L3;
#endif
        L3:
            if (modifiers & modifiersx)
#if TX86
                synerr(EM_illegal_type_combo);          // illegal combination of types
            else
                modifiers |= modifiersx;
#else
                goto error;             /* illegal combination of types */
            modifiers |= modifiersx;
#endif
            stoken();
            goto L2;

        case TKsemi:
            if (ANSI && (tkw | modifiers) && !(tkw & (TKWtag | TKWdecltype)) &&
                !pstate.STnewtypeid)
                synerr(EM_empty_decl);          // empty declaration
            break;

        case TKclass:
        case TKstruct:
        case TKunion:
        {   enum_TK tk;

            tk = (enum_TK)tok.TKval;
            stoken();
            t = stunspec(tk,NULL,NULL,NULL);    // do struct or union specifier
            type_debug(t);
            if (CPP && type_struct(t))
            {
                if (tok.TKval == TKcolcol)
                {   token_unget();
                    token_setident(t->Ttag->Sident);
                    s = symbol_search(tok.TKid);
                    goto L7;
                }
                if (level > 0 && tok.TKval == TKlpar &&
                        t->Ttag->Sstruct->Sflags & STRanyctor)
                {   token_unget();
                    token_setident(t->Ttag->Sident);
                    break;
                }
            }
            tkwx = TKWtag;
            goto L8;
        }
        case TKenum:
            t = enumspec();             // enum specifier
            type_debug(t);
            tkwx = TKWtag;
            goto L8;

        case TKdecltype:
            t = parse_decltype();
            tkwx = TKWdecltype;
            goto L8;

        case TKcolcol:
            if (tkw)
                break;
            stoken();
            if (tok.TKval == TKnew || tok.TKval == TKdelete)
            {   token_unget();
                tok.TKval = TKcolcol;
                break;
            }
            if (tok.TKval != TKident)
                synerr(EM_ident_exp);           // identifier expected
            s = scope_search(tok.TKid,SCTglobal);
            goto L7;

        case TKtypename:
            // CPP98 14.6.3 elaborated-type-specifier
            //  'typename' ['::'] nested-name-specifier identifier ['<' template-argument-list '>']
            sawtypename = 1;
            stoken();
            if (tok.TKval == TKcolcol)
            {
                if (!Scope::inTemplate())
                    synerr(EM_no_typename);     // typename not allowed here
                goto L2;
            }
            if (tok.TKval == TKident)
            {
                if (tkw)
                    synerr(EM_no_typename);     // typename not allowed here
                s = symbol_search(tok.TKid);
                if (s && s->Sclass == SCtemplate)
                    ;
                else if (!Scope::inTemplate())
                    synerr(EM_no_typename);     // typename not allowed here
                goto L7;
            }
            synerr(EM_nested_name_specifier);   // nested-name-specifier expected
            break;

        case TKsymbol:
            assert(!tkw);
            s = tok.TKsym;
            goto L7;

        case TKnamespace:
            cpperr(EM_namespace_scope);
            namespace_definition();
            goto L2;

        case TKident:
            if (tkw)
                break;
            s = symbol_search(tok.TKid);
        L7:
            if (CPP)
            {
                if (funcsym_p && isclassmember(funcsym_p))
                    sclass0 = (Classsym *)funcsym_p->Sscope;
                else
                    sclass0 = NULL;
            }
            if (s)
            {
                type_debug(s->Stype);
                switch (s->Sclass)
                {
                    case SCtypedef:
                        /* Parse the Foo::type of:
                         *    template <class T,
                         *       class Foo = vector<T>,
                         *       class Bar = less<typename Foo::type> >
                         *      class priority_queue {};
                         */
                        dependent |= s->Stype->Tflags & TFdependent;
                        if (CPP &&
                            (pstate.STintemplate || sawtypename) &&
                            tybasic(s->Stype->Tty) == TYident)
                        {
                            stoken();
                            if (tok.TKval == TKcolcol)
                            {
                              L27:
                                stoken();
                                if (tok.TKval == TKtemplate)
                                    stoken();   // BUG: check following ident is a template
                                if (tok.TKval == TKident)
                                {
                                    t = type_alloc(TYident);
                                    t->Tident = (char *) MEM_PH_STRDUP(tok.TKid);
                                    t->Tnext = s->Stype;
                                    t->Tnext->Tcount++;

                                    // If another :: or <>, just skip them
                                    stoken();
                                    if (tok.TKval == TKlg)
                                        stoken();
                                    if (tok.TKval == TKlt)
                                    {   int bracket = 1;

                                        while (1)
                                        {
                                            stoken();
                                            switch (tok.TKval)
                                            {   case TKgt:
                                                    if (--bracket == 0)
                                                        break;
                                                    continue;
                                                case TKlt:
                                                    ++bracket;
                                                    continue;
                                                default:
                                                    continue;
                                            }
                                            break;
                                        }
                                        stoken();
                                    }
                                    if (tok.TKval == TKcolcol)
                                        goto L27;
                                    tkwx = TKWident;
                                    type_debug(t);
                                    goto L8;
                                }
                                else
                                    synerr(EM_ident_exp);       // identifier expected
                            }
                            else
                                token_unget();
                        }
#if TX86
                        // If we are past the header, and referencing typedefs,
                        // then output the typedef into the debug info.
                        if (config.fulltypes == CV4 &&
                            pstate.STflags & PFLextdef &&
                            (!CPP || tybasic(s->Stype->Tty) != TYident) &&
                            !s->Sxtrnnum
                           )
                           cv_outsym(s);
#endif
                        if (CPP && tybasic(s->Stype->Tty) == TYstruct)
                        {
                            modifiers2 |= s->Stype->Tty & (mTYconst | mTYvolatile);
                            s = s->Stype->Ttag;
                            goto L7;
                        }
                        msbug = 1;
                        t = s->Stype;     /* use pre-defined type        */
                        type_debug(t);
#if HTOD
                        t = type_copy(t);
                        t->Ttypedef = s;
#endif
                        tkwx = TKWident;
                        goto L6;

                    case SCtemplate:            /* class template       */
                        // BUG: CPP98 14.6.6
                        // check that template names inside template expansions
                        // should be preceded by 'typename'.
                        if (pstate.STintemplate)
                        {
                            t = template_expand_type(s);
                            type_debug(t);
                            stoken();
                            if (tok.TKval == TKcolcol)
                            {
                            L12:
                                stoken();
                                if (tok.TKval == TKtemplate)
                                    stoken();   // BUG: check following ident is a template
                                if (tok.TKval == TKident)
                                {
                                    type *tn = t;

                                    t = type_alloc(TYident);
                                    t->Tident = (char *) MEM_PH_STRDUP(tok.TKid);
                                    t->Tnext = tn;
                                    t->Tnext->Tcount++;
                                    // BUG: what if next token is a ::
                                    // or a <>?
                                    stoken();
                                    if (tok.TKval == TKcolcol)
                                    {
                                        goto L12;
                                    }

                                    tkwx = TKWident;
                                    type_debug(t);

                                    goto L8;
                                }
                                else
                                    synerr(EM_ident_exp);       // identifier expected
                            }
                            else
                            {
                                tkwx = TKWtag;
                                goto L8;
                            }
                        }
                        stoken();
                        s = template_expand(s,2|0);     // instantiate template
                        if (!s)
                            goto L7;
                        symbol_debug(s);
                        // FALL-THROUGH
                    case SCstruct:
                    case SCenum:
                        if (!CPP)
                            break;
                        if (stoken() == TKcolcol)
                        {
                            stoken();
                            if (tok.TKval == TKtemplate)
                                stoken();       // BUG: check following ident is a template
                            if (tok.TKval == TKident && s->Sclass != SCenum)
                            {   symbol *smem;
                                Classsym *sscope;

                                sscope = (Classsym *)s;
                                smem = cpp_findmember_nest((Classsym **)&s,tok.TKid,FALSE);
                                if (smem)
                                {   switch (smem->Sclass)
                                    {
                                        case SCtypedef:
                                        case SCstruct:
                                        case SCenum:
                                        case SCtemplate:
                                            if (smem == sscope)
                                            {   // Probably a nested constructor definition
                                                s = sscope;
                                                break;
                                            }
                                            if (!sclass0 || !c1isbaseofc2(NULL,sscope,sclass0))
                                                sclass0 = sscope;
                                            //if (level != 0)
                                            if ((level != 0 || pstate.STingargs) &&
                                                !(scw & SCWtypedef))
                                                cpp_memberaccess(smem,funcsym_p,sclass0);
                                            s = smem;
                                            goto L7;
                                    }
                                }
                            }
                            do
                            {   token_unget();
                                tok.TKval = TKcolcol;
                                token_unget();
                                tok.setSymbol(s);
                            } while ((s = s->Sscope) != NULL && s->Sclass != SCnamespace);
                        }
                        else
                        {   t = s->Stype;       // use class type
                            type_debug(t);
                            tkwx = TKWtag;
                            goto L8;
                        }
                        break;

                    case SCnamespace:
                        s = nspace_qualify((Nspacesym *)s);
                        goto L7;

                    case SCalias:
                        s = ((Aliassym *)s)->Smemalias;
                        goto L7;

                    default:
                        if (CPP && s->Scover)
                        {   enum_TK tk;

                            tk = stoken();
                            token_unget();
                            if (tk == TKcolcol)
                            {   s = s->Scover;
                                goto L7;
                            }
                        }
                        break;
                }
            }
            break;

        case TKusing:
            using_declaration();
            break;

        default:
            break;
    }
    result = 3;

    // Do storage class
    switch (scw)
    {
        case SCWextern:
            sc_specifier = SCextern;
            if (level == 1)
                goto bad_sc;
            break;

        case SCWstatic:
            sc_specifier = SCstatic;
            if (level == 1)
                goto bad_sc;
            break;

        case SCWthread:
        case SCWthread | SCWstatic:
            sc_specifier = SCthread;
            if (level == 1)
                goto bad_sc;
            break;

        case SCWauto:
            switch (level)              /* declaration level            */
            {   case 0:
                case 1:
                    goto bad_sc;        /* auto not allowed here        */
                default:
                    sc_specifier = SCauto;
                    break;
            }
            break;

        case SCWregister:
            switch (level)              /* declaration level            */
            {   case 0:
                    goto bad_sc;        /* no global register types     */
                case 1:
                    sc_specifier = SCregpar;
                    break;
                default:
                    sc_specifier = SCregister;
                    break;
            }
            break;

        case SCWtypedef:
            sc_specifier = SCtypedef;
            break;

#if linux || __APPLE__ || __FreeBSD__ || __OpenBSD__
        case SCWinline | SCWextern:
            sc_specifier = SCeinline;
            break;
#endif
        case SCWinline | SCWstatic:
            sc_specifier = SCsinline;
            break;

        case SCWinline:
            sc_specifier = SCinline;
            break;

        case SCWoverload:
            if (ANSI)
                goto bad_sc;            // overload keyword is an anachronism
            sc_specifier = SCoverload;
            break;

        case SCWvirtual:
            if (level != -1)
                goto bad_sc;
            sc_specifier = SCvirtual;
            break;

        case 0:
            result &= ~2;               // we're using the default
            switch (level)
            {
                case 0:     sc_specifier = CPP ? pstate.STgclass : SCglobal;
                            break;
                case 1:     sc_specifier = SCparameter; break;
                default:    sc_specifier = SCauto;      break;
            }
            break;

        default:
        bad_sc:
            synerr(EM_storage_class,"");                // bad storage class
            sc_specifier = SCstatic;    // error recovery
            break;
    }
    if (pclass)
        *pclass = sc_specifier;
    else if (pclassm)
        *pclassm = scw;
    else if (result & 2)
    {
        synerr(EM_storage_class,"");                    // bad storage class
        result &= ~2;
    }

    // Do type specifier
    switch (tkw)
    {
        case TKWchar:                   t = chartype;   break;
        case TKWsigned | TKWchar:       t = tsschar;    break;
        case TKWunsigned | TKWchar:     t = tsuchar;    break;
        case 0:                         result &= ~1;
        case TKWsigned:
        case TKWsigned | TKWint:
        case TKWint:                    t = tsint;      break;
        case TKWunsigned | TKWint:
        case TKWunsigned:               t = tsuns;      break;
        case TKWsigned | TKWshort | TKWint:
        case TKWsigned | TKWshort:
        case TKWshort | TKWint:
        case TKWshort:                  t = tsshort;    break;
        case TKWunsigned | TKWshort:
        case TKWunsigned | TKWshort | TKWint:   t = tsushort; break;
        case TKWsigned | TKWlong | TKWint:
        case TKWsigned | TKWlong:
        case TKWlong | TKWint:
        case TKWlong:                   t = tslong;     break;
        case TKWsigned | TKWllong | TKWint:
        case TKWsigned | TKWllong:
        case TKWllong | TKWint:
        case TKWllong:                  t = tsllong;    break;
        case TKWunsigned | TKWlong:
        case TKWunsigned | TKWlong | TKWint:    t = tsulong; break;
        case TKWunsigned | TKWllong:
        case TKWunsigned | TKWllong | TKWint:   t = tsullong; break;
        case TKWlong | TKWdouble:       t = LDOUBLE ? tsldouble : tsreal64;     break;
        case TKWdouble:                 t = tsdouble;   break;
        case TKWfloat:                  t = tsfloat;    break;
        case TKWvoid:                   t = tsvoid;     break;
        case TKWbool:                   t = tsbool;     break;
        case TKWwchar_t:                t = (config.flags4 & CFG4wchar_is_long) ? tsdchar : tswchar_t;
                                                        break;

        case TKWimaginary | TKWfloat:            t = tsifloat;   goto Lc99;
        case TKWimaginary | TKWdouble:           t = tsidouble;  goto Lc99;
        case TKWimaginary | TKWlong | TKWdouble: t = tsildouble; goto Lc99;
        case TKWcomplex | TKWfloat:              t = tscfloat;   goto Lc99;
        case TKWcomplex | TKWdouble:             t = tscdouble;  goto Lc99;
        case TKWcomplex | TKWlong | TKWdouble:   t = tscldouble; goto Lc99;
        Lc99:
            if (intsize == 2 ||
                !LDOUBLE ||
                !config.inline8087)
            {
                synerr(EM_no_complex);          // complex / imaginary not supported
                t = tsint;
            }

        case TKWtag:
        case TKWdecltype:
        case TKWident:          type_debug(t); break;   // t is already set

        case TKWchar16:         t = tschar16;   break;
        case TKWdchar:          t = tsdchar;    break;

        default:
        error:
            synerr(EM_illegal_type_combo);      /* illegal combination of types */
            t = tserr;
            if (pclass)
                *pclass = SCstatic;
            break;
    }
    type_debug(t);
    if (dependent && CPP)
        t = type_setdependent(t);
    t->Tcount++;                /* usage count                  */
    *ptyp_spec = t;
    modifiers |= modifiers2;
    if (modifiers)
    {
        if (modifiers & (mTYexport | mTYimport | mTYnaked | mTYthread | mTYcdecl | mTYpascal))
            result |= 4;
        if (modifiers & (mTYconst | mTYvolatile))
            result |= 1;
        type_setty(ptyp_spec,t->Tty | modifiers);
    }
    return result;
}


/******************************************
 * C++98 A.4 Parse id-expression.
 *
 *      id-expression:
 *              unqualified-id
 *              qualified-id
 *      unqualified-id:
 *              identifier
 *              operator-function-id
 *              conversion-function-id
 *              ~class-name
 *              template-id
 *      qualified-id:
 *              [::] nested-name-specifier [template] unqualified-id
 *              :: identifier
 *              :: operator-function-id
 *              :: template-id
 *      nested-name-specifier:
 *              class-or-namespace-name :: [nested-name-specifier]
 *              class-or-namespace-name :: template nested-name-specifier
 *      class-or-namespace-name:
 *              class-name
 *              namespace-name
 * Returns:
 *      resulting symbol
 *      NULL if error
 */

symbol *id_expression()
{   symbol *s;
    unsigned sct;
    int gettemplate;

    // Parse id-expression

    sct = SCTglobal | SCTnspace | SCTtempsym |
        SCTtemparg | SCTmfunc | SCTlocal | SCTwith |
        SCTclass | SCTparameter;
    if (tok.TKval == TKcolcol)
    {
        stoken();
        sct = SCTglobal;
    }
    gettemplate = 0;
    if (tok.TKval == TKtemplate)
    {   stoken();
        gettemplate = 1;
    }
    if (tok.TKval != TKident)
    {   synerr(EM_ident_exp);           // identifier expected
        goto Lerr;
    }
    s = scope_search(tok.TKid, sct);
L2:
    if (!s)
    {   synerr(EM_undefined, tok.TKid);
        goto Lerr;
    }
    if (gettemplate && s->Sclass != SCtemplate && s->Sclass != SCfunctempl)
        synerr(EM_template_expected);
    gettemplate = 0;
    stoken();
L1:
    switch (s->Sclass)
    {
        case SCnamespace:
            token_unget();
            s = nspace_qualify((Nspacesym *)s);
            if (!s)
                goto Lerr;
            stoken();
            goto L1;

        case SCtemplate:
        case SCfunctempl:
            if (tok.TKval == TKlt || tok.TKval == TKlg)
            {   s = template_expand(s, 2);
                stoken();
                goto L1;
            }
            break;

        case SCtypedef:
            if (tybasic(s->Stype->Tty) == TYstruct)
            {   s = s->Stype->Ttag;
                goto L1;
            }
            break;

        case SCstruct:
        case SCenum:
            if (tok.TKval == TKcolcol)
            {
                stoken();
                if (tok.TKval == TKtemplate)
                {   gettemplate = 1;
                    stoken();
                }
                if (tok.TKval != TKident)
                {   synerr(EM_ident_exp);           // identifier expected
                    goto Lerr;
                }
                s = cpp_findmember_nest((Classsym **)&s, tok.TKid, FALSE);
                goto L2;
            }
            break;

        case SCalias:
            s = ((Aliassym *)s)->Smemalias;
            goto L1;

        default:
            if (s->Scover && tok.TKval == TKcolcol)
            {   s = s->Scover;
                goto L1;
            }
            break;
    }
    return s;

Lerr:
    return NULL;
}



/***************************
 * Do the actual declaration.
 * Input:
 *      flag & 1        no declaration if no decl-specifier-seq (C only)
 *      flag & 2        force SCextern storage class
 *      flag & 4        declaration in condition (C++ only)
 *      level           declaration level
 * Returns:
 *    (flag & 1):
 *      !=NULL  declaration
 *      NULL    not a declaration
 *    (flag & 4):
 *      e       e is expression to test
 *      pstate.STlastfunc = symbol declared
 */

elem *declaration(int flag)
{   symbol *s;
    type *dt;
    tym_t ty;
    char vident[2*IDMAX + 1];
    type *tspec;
    enum SC sc_specifier;
    int dss;

    _chkstack();

    //dbg_printf("declaration(flag = %d)\n",flag);

    if (tok.TKval == TKstatic_assert && !(flag & 4))
    {
        parse_static_assert();
        return (elem *)1;
    }
    dss = declaration_specifier(&tspec,&sc_specifier,NULL);

    type_debug(tspec);

    s = NULL;
    if (flag & 1)
    {   assert(!CPP);
        if (!dss)
        {   pstate.STlastfunc = NULL;
            return NULL;
        }
    }
    if (flag & 2)
        sc_specifier = SCextern;
    if (flag & 4)
        assert(CPP);

    while (1)
    {
        if (tok.TKval == TKsemi)
        {   symbol *stag;

            ty = tybasic(tspec->Tty);
            if (ty == TYenum)
            {
                stag = (symbol *)tspec->Ttag;
                if (stag->Senum->SEflags & SENnotagname && !stag->Senumlist)
                    synerr(EM_ident_exp);
            }
            else if (ty == TYstruct)
            {
                stag = (symbol *)tspec->Ttag;
                if (stag->Sstruct->Sflags & STRnotagname && !stag->Sstruct->Sfldlst)
                    synerr(EM_ident_exp);
            }
        }

        if (ANSI && (tok.TKval == TKcomma /*||
            (tok.TKval == TKsemi && ty != TYstruct && ty != TYenum*/))
            synerr(EM_id_or_decl);              // ident or '(' expected
        gdeclar.hasExcSpec = 0;
        dt = declar_fix(tspec,vident);
        //printf("vident = '%s'\n", vident);
        //type_print(dt);
        if (sc_specifier == SCtypedef && gdeclar.hasExcSpec)
            cpperr(EM_typedef_exception, vident);
        if (vident[0] == 0)             /* if there was no identifier   */
        {
            if (dt != tspec || flag & 4)
            {
                synerr(EM_id_or_decl);          // ident or '(' expected
                panic(TKsemi);
            }
            type_free(dt);

            // Look for anonymous union declarations
            if (CPP &&
                type_struct(tspec) &&
                tspec->Ttag->Sstruct->Sflags & STRunion &&
                tspec->Ttag->Sstruct->Sflags & STRnotagname &&
                sc_specifier != SCtypedef
               )
                s = anonymous(tspec->Ttag,sc_specifier);
            goto L1;
        }
        //dbg_printf("declaration('%s')\n",vident);
        if (gdeclar.constructor || gdeclar.destructor || gdeclar.invariant)
        {
            if (tybasic(tspec->Tty) != TYint ||
                !gdeclar.class_sym ||
                ((gdeclar.destructor || gdeclar.invariant) && dt->Tparamtypes)
               )
                cpperr(EM_bad_ctor_dtor);       // illegal ctor/dtor/invariant declaration
            else if (gdeclar.constructor)
            {   /* Constructors return <ptr to><class>  */
                type_free(dt->Tnext);
                dt->Tnext = newpointer(gdeclar.class_sym->Stype);
                dt->Tnext->Tcount++;
            }
        }
        s = symdecl(vident, dt, sc_specifier, NULL);
        if (!s)
            goto L1;

        /* If function returning        */
        if (tyfunc(s->Stype->Tty))
        {   symbol_func(s);
            if (sc_specifier == SCauto && CPP)
            {
                symbol *sa;

                dt->Tcount++;
                sa = symdecl(vident, dt, SCfuncalias, NULL);
                symbol_func(sa);
                sa->Sfunc->Falias = s;
            }
            if (flag & 4)
                synerr(EM_datadef,s->Sident);           // expected data def
            if (funcdecl(s,sc_specifier,1,&gdeclar))
            {   /* Function body was present    */
#if HTOD
                htod_decl(s);
#endif
                goto ret;
            }
        }
        else                            /* else must be data def        */
        {
            if (!dss
                && (CPP || level != 0)
               )
                synerr(EM_decl_spec_seq,vident);        // decl-specifier-seq required

            if (s->Sclass == SCtypedef)
            {
              if (CPP)
              {
                struct_t *st;
                enum_t   *se;

                if (flag & 4)
                    synerr(EM_storage_class,"typedef"); // illegal storage class

                /* Handle: typedef struct { ... } S;    */
                if (tybasic(s->Stype->Tty) == TYstruct &&
                    (st = s->Stype->Ttag->Sstruct)->Sflags & STRnotagname)
                {   st->Sflags &= ~STRnotagname;   /* we have a name now */
                    st->Salias = s;                /* steal name from typedef */
                }
                /* Handle: typedef enum { ... } E;      */
                if (tybasic(s->Stype->Tty) == TYenum &&
                    (se = s->Stype->Ttag->Senum)->SEflags & SENnotagname)
                {   se->SEflags &= ~SENnotagname;   /* we have a name now */
                    se->SEalias = s;               /* steal name from typedef */
                }
              }
            }
            else
            {   tym_t ty;
                type *t;

                t = s->Stype;
                ty = tybasic(t->Tty);
            L2:
                switch (s->Sclass)
                {
                    case SCauto:
                        if (!(t->Tflags & TFsizeunknown) &&
                            intsize == 2 &&
                            type_size(t) > 30000)
                            warerr(WM_large_auto);      // local variable is too big
                        break;
                    case SCextern:
                        if (tok.TKval == TKeq || (CPP && tok.TKval == TKlpar))
                        {   if (level != 0)
                                synerr(EM_ext_block_init);      // no initializer at block scope
                            s->Sclass = SCglobal;
                            goto L2;
                        }
                        goto Lthreshold;
                    case SCinline:
                    case SCsinline:
                        synerr(EM_storage_class,"inline");      // not allowed in this context
                        s->Sclass = SCstatic;   // error recovery
                        break;
                    case SCoverload:
                        if (ty != TYint)
                        {   synerr(EM_storage_class,"overload"); /* not allowed in this context */
                            s->Sclass = SCstatic;
                        }
                        else
                            goto L1;
                        break;

                    case SCcomdat:
                        // Keep as comdat if static member of template struct
                        if (CPP &&
                            !(isclassmember(s) && s->Sscope->Sstruct->Stempsym))
                            s->Sclass = SCglobal;
                    case SCglobal:
                        if (CPP)
                        {
                            // Default const variables to be static
                            // (but not for structs, arrays, or static class members)
                            if (t->Tty & mTYconst &&
                                !type_struct(t) &&
                                !(tybasic(t->Tty) == TYarray) &&
                                !isclassmember(s))
                            {
                                s->Sclass = SCstatic;
                            }
                        }
                        else
                        {
                    Lglobal:
                            // Don't output incomplete types
                            if (tok.TKval != TKeq &&
                                ty == TYarray &&
                                t->Tflags & TFsizeunknown)
                                s->Sclass = SCextern;
                        }
                        goto Lthreshold;

                    case SCpublic:
                        if (!CPP)
                            break;
                        s->Sclass = SCglobal;   // circumvent above
                        goto Lthreshold;

                    case SCstatic:
                        // Do not allow static variables in inline functions
                        if (level > 0 &&
                            funcsym_p->Sclass == SCinline)
                        {
                            s->Sclass = SCglobal;
                            if (CPP)
                                s->Sscope = (Classsym *)funcsym_p;
                            else
                                goto Lglobal;
                        }
                        else if (CPP)
                            goto Lthreshold;
                        else
                            goto Lglobal;
                        break;

                    Lthreshold:
#if TX86
                        if (t->Tty & mTYimport)
                        {
                            if (tok.TKval == TKeq)
                                tx86err(EM_bad_dllimport);      // initializer not allowed
                            else if (s->Sclass != SCextern)
                                s->Sclass = SCextern;
                        }

                        // Arrays of unknown size get automatically
                        // marked as __far
                        // BUG: what about arrays of near classes?
                        if (config.threshold != THRESHMAX &&
                            LARGEDATA &&
                            (ty == TYarray ||
                             (ty == TYstruct
                              && (!CPP || t->Ttag->Sstruct->ptrtype == TYfptr)
                             )) &&
                            !(t->Tty & mTYLINK) &&
                            (t->Tflags & TFsizeunknown ||
                             type_size(t) > config.threshold)
                           )
                        {
                            s->Stype = type_setty(&t,t->Tty | mTYfar);
                        }
#endif
                        break;
                }

                if (level == 1)         /* let func_body do this        */
                    goto L1;

                if (ty == TYvoid)
                {   synerr(EM_void_novalue);    // void has no value
                    type_free(t);
                    s->Stype = tsint;
                    tsint->Tcount++;
                }
                datadef(s);             /* do data def record           */
                if (level == 0 && s->Sclass != SCextern &&
                    s->Sclass != SCcomdef && s->Sclass != SCstatic)
                {
                    // Don't write out symbol if it is a const
                    if (s->Sclass != SCstatic ||
                        (CPP && !(s->Sflags & SFLvalue)))
                    {
                        outdata(s);     // and write out the symbol
                    }
                }
                if (level == 0 && CPP &&
                    (s->Sclass == SCglobal || s->Sclass == SCcomdat) &&
                    type_mangle(s->Stype) != mTYman_cpp)
                {
                    //printf("defining '%s'\n", s->Sident);
                    scope_define(s->Sident, SCTcglobal, SCglobal);
                }
                if (flag & 4)           // if declaration in conditional
                {
#if HTOD
                    if (level == 0)
                        htod_decl(s);
#endif
                    goto ret;           // only 1 allowed
                }
            }
        }
    L1:
#if HTOD
        if (level == 0)
            htod_decl(s);
#endif

        switch (tok.TKval)
        {
            case TKident:
                synerr(EM_missing_comma,vident,tok.TKid);       // missing ','
                goto L3;
            default:
L3:             synerr(EM_punctuation);         // = ; or , expected
                panic(TKsemi);
                /* FALL-THROUGH */
            case TKsemi:
                stoken();
                goto ret;               /* done with declaration        */
            case TKcomma:
                stoken();
                break;
        }
    } /* while */

ret:
    pstate.STlastfunc = s;
    type_free(tspec);
    if (flag & 4)
        return s ? el_var(s) : el_longt(tsint,0);
    else
        return (elem *)1;
}


/*********************************
 * Parse abstract-declarator.
 * Input:
 *      t       pointer to type specifier
 *      level
 * Returns:
 *      The Tcount is incremented.
 */

type *declar_abstract(type *t)
{
    return declar(t,NULL,3);
}

/******************************
 * Parse new-declarator, excluding trailing "[" expression "]"
 * ARM 5.3.3
 *      new-declarator:
 *              * [cv-qualifier-seq] [new-declarator]
 *              qualified-class-specifier :: * [cv-qualifer-seq] [new-declarator]
 *              direct-new-declarator
 *
 *      direct-new-declarator:
 *              [direct-new-declarator] "[" expression "]"
 * Returns:
 *      The Tcount is incremented.
 */

type *new_declarator(type *t)
{
    return declar(t,NULL,1);
}

/******************************
 * Parse ptr-operator.
 *      ptr-operator:
 *              "*" [cv-qualifier-seq]
 *              "&" [cv-qualifier-seq]
 *              qualified-class-specifier "::*" [cv-qualifier-seq]
 * Returns:
 *      The Tcount is incremented.
 */

type *ptr_operator(type *t)
{
    return declar(t,NULL,2);
}


/**********************************
 * Parse type_qualifier_list.
 */

tym_t type_qualifier_list()
{   tym_t tyq = 0;

    while (1)
    {   if (tok.TKval == TKconst)
            tyq |= mTYconst;
        else if (tok.TKval == TKvolatile)
            tyq |= mTYvolatile;
        else if (tok.TKval == TKrestrict)
            tyq |= mTYrestrict;
        else if (tok.TKval == TK_unaligned)
            tyq |= mTYunaligned;
        else
            break;
        stoken();
    }
    return tyq;
}


/********************************
 * Parse a declarator (including function definitions).
 * declarator ::=
 *      identifier
 *      ( declarator )
 *      * declarator
 *      declarator ()
 *      declarator [ const_exp opt ]
 * Input:
 *      t       pointer to type specifier
 *      sc_specifier
 *      level
 *      flag    0       parse declarator
 *              1       parse new-declarator (excluding trailing [ expression ] )
 *              2       parse ptr-operator for user-defined conversion
 *                      function (i.e. parentheses and square brackets
 *                      are not allowed)
 *              3       parse abstract-declarator
 * Output:
 *      vident[]
 *      gdeclar
 * Returns:
 *      The Tcount is incremented.
 */

type *declar(type *t,char *vident,int flag)
{   type *tstart,*t2;
    long tym;
    tym_t ty;
    tym_t tyx;
    unsigned mangle;
#if TX86
    static int inprototype = 0;
#endif
    type *tconv = NULL;
    int op = OPunde;
    bool constructor = FALSE;
    bool destructor = FALSE;
    bool invariant = FALSE;
    bool explicitSpecialization = FALSE;
    symbol *s = NULL;
    Nspacesym *sn = NULL;
    param_t *ptal = NULL;
    Classsym *classsymsave = pstate.STclasssym;
    unsigned cpushcount = 0;
    int global = 0;
    tym_t initial;
    static long tym_start;

    //printf("declar(flag = %d), level = %d\n", flag, level);
    //type_print(t);

    type_debug(t);
    if (vident)
        vident[0] = 0;

#if TX86
    // Irrational MS syntax can have __declspec anywhere
    initial = t->Tty & (mTYthread | mTYnaked | mTYimport | mTYexport);
#endif

    // See if need to set up enclosing scopes
    if (CPP && tok.TKval == TKsymbol)
    {
        symbol *s = tok.TKsym;

        if (s->Sscope && scope_end->sctype & SCTglobal)
            cpushcount += scope_pushEnclosing(s);
    }

    tym = tym_start;
    tym_start = 0;                      // should do with extra parameter
    mangle = 0;
    while (1)
    {
        /* Some types we only see in this function, as they only apply to
           pointer types.
         */
        enum modifier { mTYhandle = 1, mTY_ss = 2, mTY_far16 = 8,
                        mTYhuge = 0x10 };

        //tok.print();
        switch (tok.TKval)
        {
            case TKcom:
                if (!CPP)
                    goto Ldefault;
            case TKident:
            case TKsymbol:
            case TKoperator:
            case TK_invariant:
#if TX86
            case TKrpar:
            Lcase_id:
                {   tym_t tyx;

                    tyx = tym & (mTYhandle | mTY_ss | mTYLINK | mTYhuge);
                    if (tyx)
                    {   if (tyx & (mTYhandle | mTY_ss) || tyx & (tyx - 1))
                        {   synerr(EM_illegal_type_combo);      // illegal combination of types
                            tyx = 0;
                        }
                        if (tyx & mTYhuge)
                            // Fake 'huge' allocation as 'far'.
                            tym ^= mTYhuge | mTYfar;
                    }
                    if (tok.TKval == TKrpar)
                        goto Ldefault;
                }
#endif
              if (CPP)
              {
                if (tok.TKval == TKident || tok.TKval == TKsymbol)
                {
                    char dident[2*IDMAX + 1];
                    symbol *stmp;
                    symbol *st;

                    if (tok.TKval == TKident)
                    {
                        strcpy(dident,tok.TKid);        // squirrel away identifier
                        stmp = NULL;
                        stoken();

                        // look for name<args>::member
                        if (tok.TKval == TKlt || tok.TKval == TKlg)
                        {
                            st = scope_search(dident,SCTglobal | SCTnspace | SCTtempsym);
                            if (!st)
                                cpperr(EM_not_class_templ,dident);      // %s is not a class template
                            else
                            {
                                if (st->Sclass == SCalias)
                                    st = ((Aliassym *)st)->Smemalias;
                                if (st->Sclass == SCtemplate)
                                {
                                    token_unget();
                                    stmp = template_expand(st,0);
                                    stoken();
                                }
                                else if (st->Sclass == SCfunctempl)
                                {
                                    goto Lfunctempl;
                                }
                                else
                                    cpperr(EM_not_class_templ,dident);  // %s is not a class template
                            }
                        }
                        else if (tok.TKval == TKcolcol &&
                                 pstate.STclasssym &&
                                 template_classname(dident, pstate.STclasssym))
                        {
                            stmp = pstate.STclasssym;
                        }
                    }
                    else // TKsymbol
                    {
                        stmp = tok.TKsym;
                        strcpy(dident, stmp->Sident);
                        stoken();
                    }

                    // Look for class members
                    while (tok.TKval == TKcolcol)       // if dident::
                    {   Classsym *stag;

                        if (stmp)
                        {   s = stmp;
                            stmp = NULL;
                        }
                        else if (s)
                            // Look for nested class
                            s = cpp_findmember_nest((Classsym **)&s,dident,TRUE);
                        else if (sn)
                        {   s = nspace_searchmember(dident,sn);
                            sn = NULL;
                        }
                        else
                        {
                            s = global ? scope_search(dident,SCTglobal)
                                       : symbol_search(dident);
                        }
                        global = 0;
                        stoken();
                    L23:
                        if (s && s->Sclass == SCnamespace)
                        {
                            sn = (Nspacesym *)s;
                            scope_push_symbol(s);
                            cpushcount++;
                            s = NULL;
                            if (tok.TKval == TKident)
                                goto Lcase_id;
                            if (tok.TKval == TKoperator)
                                goto Lcase_id;
                        }
                        if (!s || !type_struct(s->Stype))
                        {
                            if (s && tybasic(s->Stype->Tty) == TYident &&
                                tok.TKval == TKstar)    // pointer to member
                            {
                                // So template_deleteargtab() won't free it
                                s = template_createsym(s->Sident, NULL, NULL);
                                symbol_keep(s);
                                goto Lpm;
                            }
                            if (s && s->Scover)
                            {   s = s->Scover;
                                goto L23;
                            }
                            cpperr(EM_class_colcol,dident);     // must be a class name
                            s = NULL;
                        }
                        else
                        {
                            s = s->Stype->Ttag;         // in case s was a typedef'd class
                            pstate.STclasssym = (Classsym *)s;  // set new scope
                        Lpm:
                            sn = NULL;
                            stag = (Classsym *)s;
                            scope_pushclass(stag);
                            cpushcount++;
                            switch (tok.TKval)
                            {   const char *id;

                                case TKident:
                                    id = tok.TKid;
                                L16:
                                    strcpy(dident,id);  // squirrel away identifier
                                    stoken();
                                    stmp = NULL;

                                    // look for dident<args>::member
                                    if (tok.TKval == TKlt || tok.TKval == TKlg)
                                    {
                                        st = scope_search(dident,SCTglobal | SCTclass);
                                        if (!st)
                                            cpperr(EM_not_class_templ,dident);  // %s is not a class template
                                        else if (st->Sclass == SCtemplate)
                                        {
                                            token_unget();
                                            stmp = template_expand(st,0);
                                            stoken();
                                            continue;
                                        }
                                        else if (st->Sclass == SCfunctempl)
                                        {
                                          Lfunctempl:
                                            if (!pstate.STexplicitInstantiation &&
                                                !pstate.STexplicitSpecialization)
                                                cpperr(EM_not_class_templ,dident);      // %s is not a class template
                                            ptal = template_gargs(st);
                                            explicitSpecialization = TRUE;
                                            strcpy(vident, dident);
                                            stoken();
                                            tstart = t;                 // start of type list
                                            tstart->Tcount++;
                                            goto ret;
                                        }
                                        else
                                            cpperr(EM_not_class_templ,dident);  // %s is not a class template
                                    }

                                    if (template_classname(dident,stag))
                                    {   constructor = TRUE;     // X::X()
                                        // constructor declaration
                                        strcpy(dident, cpp_name_ct);
                                    }
                                    break;
                                case TKsymbol:
                                    // we really should use the symbol directly
                                    id = tok.TKsym->Sident;
                                    goto L16;
                                case TKcom:
                                    stoken();
                                    if (tok.TKval == TKident)
                                    {   if (!template_classname(tok.TKid,stag))
                                            cpperr(EM_tilde_class,stag->Sident);        // X::~X() expected
                                        destructor = TRUE;
                                        strcpy(dident,cpp_name_dt);
                                        stoken();
                                        break;
                                    }
                                    else
                                        cpperr(EM_tilde_class,stag->Sident);    // X::~X() expected
                                    break;

                                case TK_invariant:
                                    invariant = TRUE;
                                    strcpy(dident,cpp_name_invariant);
                                    stoken();
                                    break;

                                case TKstar:    // X::* pointer to member
                                    t = type_allocmemptr(stag,t);
                                    s = NULL;
                                    if (tym)
                                        type_setty(&t,t->Tty | tym);
                                    if (mangle)
                                        type_setmangle(&t,mangle);
                                    goto L14;
                                case TKoperator:
                                    goto Loperator;
                                default:
                                    goto Ldefault;
                            }
                        }
                    } // while ("::")

                    //if (global)
                        //cpperr(EM_colcol_exp);        // '::' expected
                    if (flag != 0)
                    {
                        synerr(EM_ident_abstract, dident);      // bad abstract declarator
                    }
                    else
                        strcpy(vident,dident);  // squirrel away identifier
#if TX86
                    // Determine if linkage was specified with #pragma linkage()
                    if (cstate.CSlinkage && vident)
                    {   symbol *s;

                        s = findsy(vident,cstate.CSlinkage);
                        if (s)
                        {   tym = s->Slinkage;
                            mangle = s->Smangle;
                        }
                    }
#endif
                }
                else if (tok.TKval == TKoperator)
                {   char *p;

                Loperator:
                    p = cpp_operator(&op,&tconv);
                    if (vident)
                    {
                        strcpy(vident,p);       // squirrel away identifier
                        if (pstate.STexplicitSpecialization &&
                            (tok.TKval == TKlt || tok.TKval == TKlg))
                        {
                            symbol *st = scope_search(vident, SCTglobal | SCTnspace);
                            if (st)
                            {
                                ptal = template_gargs2(st);
                                explicitSpecialization = TRUE;
                                stoken();
                            }
                        }
                    }
                    else
                        synerr(EM_ident_abstract, p);   // bad abstract declarator
                }
                else if (tok.TKval == TK_invariant)
                {
                    stoken();
                    invariant = TRUE;
                    strcpy(vident, cpp_name_invariant);
                }
                else // TKcom
                {
                    if (flag)
                        synerr(EM_ident_abstract, "~");         // bad abstract declarator
                    destructor = TRUE;
                    stoken();
                    if (tok.TKval == TKident)
                    {   if (vident)
                            strcpy(vident,tok.TKid);
                        stoken();
                    }
                }
              }
              else
              {
                if (vident)
                    strcpy(vident,tok.TKid);    // squirrel away identifier
                else
                    synerr(EM_ident_abstract, tok.TKid);        // bad abstract declarator
                stoken();
#if TX86
                // Determine if linkage was specified with #pragma linkage()
                if (cstate.CSlinkage && vident)
                {   symbol *s;

                    s = findsy(vident,cstate.CSlinkage);
                    if (s)
                    {   tym = s->Slinkage;
                        mangle = s->Smangle;
                    }
                }
#endif
              }
                tstart = t;                     // start of type list
                tstart->Tcount++;
                goto ret;

            case TKlpar:
#if TX86
                if (tym && CPP)
                    if (flag == 1 || flag == 2)
#else
                if (tym)
#endif
                {   synerr(EM_illegal_type_combo);      // illegal combination of types
                    tym = 0;
                }
                if (flag == 1)          // if new-declarator
                    goto Lnd;
                if (flag == 2)          // if ptr-operator
                    goto Ldefault;
#if TX86
                // This is a hack to support the Borland syntax:
                //      void __interrupt (__far * _dos_getvect(unsigned intr))()
                // which should have been written:
                //      void (__interrupt __far * _dos_getvect(unsigned intr))()

#if 0
                if (tym & mTYinterrupt)
                {   tym &= ~mTYinterrupt;
                    tok.TKval = TK_interrupt;
                }
                else
#endif
#endif
                    stoken();
                if (CPP && s)
                    synerr(EM_ident_exp);       // identifier expected
                switch (tok.TKval)
                {
                    case TKrpar:                // if ()
                    case TKchar:
                    case TKchar16_t:
                    case TKshort:
                    case TKint:
                    case TKlong:
                    case TK_int64:
                    case TKsigned:
                    case TKunsigned:
                    case TKfloat:
                    case TKdouble:
                    case TKvoid:
                    case TKbool:
                    case TKwchar_t:
                    case TKchar32_t:
                    case TKconst:
                    case TKvolatile:
#if linux || __APPLE__ || __FreeBSD__ || __OpenBSD__
                    case TK_attribute:
                    case TK_extension:
#endif
#if TX86
                        if (tym)
                            synerr(EM_illegal_type_combo);      // illegal combination of types
#endif
                        // It's type <function returning><t> without the identifer
                        tstart = t;
                        tstart->Tcount++;
                        if (CPP)
                        {   token_unget();
                            tok.TKval = TKlpar;
                        }
                        else
                            goto L13;
                        break;

                    case TKcolcol:
                    case TKsymbol:
                    case TKident:
                        /* Function returning:
                         *      (id,
                         *      (id&
                         *      (id)
                         * (declarator):
                         *      (id::*
                         *      (id<args>
                         */
                        if (CPP)
                        {   Token_lookahead tla;

                            tla.init();
                            if (tok.TKval == TKcolcol)
                                tla.lookahead();
                        Lahead:
                            tla.lookahead();
                            if (tok.TKval == TKlg)
                                tla.lookahead();
                            else if (tok.TKval == TKlt)
                            {   // Skip over template args
                                int bracket = 1;

                                while (1)
                                {   tla.lookahead();
                                    switch (tok.TKval)
                                    {   case TKgt:
                                            if (--bracket == 0)
                                                break;
                                            continue;
                                        case TKlt:
                                            ++bracket;
                                            continue;
                                        default:
                                            continue;
                                    }
                                    break;
                                }
                                tla.lookahead();
                            }
                            if (tok.TKval == TKcolcol)
                            {
                                tla.lookahead();
                                switch (tok.TKval)
                                {
                                    case TKident:
                                    case TKsymbol:
                                        goto Lahead;

                                    case TKstar:
                                        tla.term();
                                        goto Ldeclarator;

                                    default:
                                        break;
                                }
                            }
                            if (tok.TKval == TKrpar)
                            {
                                /* Look for:
                                 * double (x);
                                 * double (y) = 1.0;
                                 */
                                tla.lookahead();
                                if (tok.TKval == TKeq ||
                                    tok.TKval == TKsemi ||
                                    tok.TKval == TKlbra ||
                                    (tok.TKval == TKcomma && flag != 3) ||
                                    tok.TKval == TKlpar)
                                {   tla.term();
                                    goto Ldeclarator;
                                }
                            }
                            tla.term();
                            token_unget();
                            tok.TKval = TKlpar;
#if TX86
                            if (tym)
                                synerr(EM_illegal_type_combo);  // illegal combination of types
#endif
                            // It's type <function returning><t> without the identifer
                            tstart = t;
                            tstart->Tcount++;
                        }
                        else
                        {
                            goto Ldeclarator;
                        }
                        break;

                    default:
                    Ldeclarator:
                        // ( declarator )
                        tym_start = tym;
                        tym = 0;
                        tstart = declar(t,vident,flag);
                        if (CPP)
                        {   s = gdeclar.class_sym;
                            sn = gdeclar.namespace_sym;
                        }
//<<>>
                        chktok(TKrpar,EM_declarator_paren_expected);
                        assert(tstart);

                        /* Correctly account for things like
                           void (far f)(unsigned);
                           (t is no longer in tstart, causing declar() to hang)
                         */
                        for (t2 = tstart; t2 != t; t2 = t2->Tnext)
                        {   if (!t2)
                            {
                                tym |= tstart->Tty & ~mTYbasic;
                                type_setty(&tstart,tybasic(tstart->Tty));
                                t = tstart;
                                break;
                            }
                        }
                        break;
                }
                goto ret;

            case TKstar:
                msbug = 0;
                t = type_allocn(TYptr,t);
                switch ((tym_t)(tym & ~(mTYTFF | mTYMOD)))
                {
#if TX86
                    case mTYnear:       ty = (TYnptr | tym) & ~mTYnear;
                                        break;
                    case mTYfar:        ty = (TYfptr | tym) & ~mTYfar;
                                        break;
                    case mTYcs:         ty = (TYcptr | tym) & ~mTYcs;
                                        break;

                    case mTY_ss:        ty = TYsptr;    goto L5;
                    case mTYhuge:       ty = TYhptr;    goto L5;
                    case mTYhandle:     ty = TYvptr;    goto L5;
                    case mTY_far16:     ty = TYf16ptr;  goto L5;
                    L5:
                        if (!(tym & (mTYTFF | mTYMOD)))
                            break;
#endif
                    default:
                        synerr(EM_illegal_type_combo);  /* illegal combination of types */
                        tym = 0;
                        mangle = 0;
                        goto L2;
                    case 0:
                        goto L2;
                }
                t->Tty = ty;
              L2:
                t->Tty |= tym & mTYTFF;
                t->Tmangle = mangle;
              L14:
                tym = 0;
                mangle = 0;
                stoken();
                /* Parse const and volatile postfixes */
                while (1)
                {   tym_t tyx;

                    switch (tok.TKval)
                    {   case TKconst:       tyx = mTYconst;     goto L8;
                        case TKrestrict:    tyx = mTYrestrict;  goto L8;
                        case TKvolatile:    tyx = mTYvolatile;  goto L8;
                        case TK_unaligned:  tyx = mTYunaligned; goto L8;
                        L8: if (t->Tty & tyx)
                                synerr(EM_illegal_type_combo);  /* illegal combination of types */
                            t->Tty |= tyx;
                            break;
#if TX86
                        case TK_Seg16:
                            /* *_Seg16 is equivalent to _far16* */
                            if (tyref(ty))
                                synerr(EM_illegal_type_combo);  /* illegal combination of types */
                            ty = TYf16ptr;
                            goto L5;
#endif
                        default:
                            goto L6;
                    }
                    stoken();
                }
             L6:
                break;

            case TKand:
                if (!CPP)
                    goto Ldefault2;

                // "&" [cv-qualifier-seq] is not allowed in a new-declarator
                msbug = 0;
                t = newref(t);
#if TX86
                switch (tym)
                {   case mTYfar:
                        if (LARGEDATA)
                            break;
                        t->Tty = TYfref;
                        break;
                    case mTYnear:
                        if (!LARGEDATA)
                            break;
                        t->Tty = TYnref;
                        break;

                    default:
                    illegal:
                        synerr(EM_illegal_type_combo);  // illegal combination of types
                        tym = 0;
                        mangle = 0;
                        break;

                    case 0:
                        break;
                }
#endif
                goto L2;

            case TKcolcol:
                if (!CPP)
                    goto Ldefault2;
                if (stoken() != TKident)
                    synerr(EM_ident_exp);               // identifier expected
                global = 1;             // lookup in global table
                continue;

#if TX86
            /* Parse extended prefixes  */
            case TK_near:       tym |= mTYnear;
                                goto L3;
            case TK_huge:
                                // For non-16 bit compiles, ignore huge keyword
                                if (I16)
                                    tym |= mTYhuge;
                                goto L3;
            Lfar:
            case TK_far:        tym |= mTYfar;                  goto L3;
            case TK_ss:         tym |= mTY_ss;                  goto L3;
            case TK_cs:         tym |= mTYcs;                   goto L3;

            case TK_based:      nwc_based();
                                continue;

            case TK_declspec:   tym |= nwc_declspec();          goto L3;

            case TK_far16:
                                /* For 16 bit compiles, treat _far16
                                   keyword as equivalent to far.
                                 */
                                if (I16)
                                    goto Lfar;

                                if (config.exe & EX_flat)
                                    tym |= mTYfar16 | mTY_far16;
                                else
                                    synerr(EM_far16_model); // only in flat memory model
                                goto L3;

            case TK_fastcall:   goto L3;        // silently ignore

            case TK_interrupt:  tym |= mTYinterrupt;
                                goto L3;

            case TK_saveregs:   synerr(EM_bad_kwd);     // unsupported keyword
                                goto L3;        // ignore it

            case TK_loadds:     tym |= mTYloadds;
                                goto L3;

            case TK_export:     tym |= mTYexport;
                                if (I16)
                                    // export implies far for 16 bit models
                                    tym |= mTYfar;
                                goto L3;

            case TK_System:     /* For 16 bit compiles, treat _System
                                   keyword as equivalent to far pascal.
                                 */
                                if (I16)
                                {   tym |= mTYfar;
                                    mangle = mTYman_pas;
                                    goto L7;
                                }
                                /* For 32 bit compiles, treat as syscall
                                 */
            case TK_syscall:
                                if (linkage != LINK_CPP)
                                    mangle = mTYman_sys;
                                tym |= mTYsyscall;
                                goto L3;

            case TK_stdcall:
                                if (linkage != LINK_CPP)
                                    mangle = mTYman_std;
                                tym |= mTYstdcall;
                                goto L3;

            case TK_java:
                                if (linkage != LINK_CPP)
                                    mangle = mTYman_d;
                                tym |= mTYjava;
                                goto L3;
#endif
            case TK_cdecl:
                                if (linkage != LINK_CPP)
                                    mangle = mTYman_c;
                                tym |= mTYcdecl;                goto L3;
            case TK_fortran:                    /* same as pascal       */
            case TK_pascal:
            L7:
                                if (linkage != LINK_CPP)
                                    mangle = mTYman_pas;
                                tym |= mTYpascal;               goto L3;
            case TK_handle:     tym |= mTYhandle;               goto L3;
            L3:
                if (config.flags3 & CFG3nofar)
                {   // Flat model doesn't have far, etc.
                    tym &= ~(mTYfar | mTYinterrupt | mTYloadds | mTYhandle | mTYhuge);
                }
                stoken();
                continue;

            case TKconst:
                tyx = mTYconst;
                goto Lcvx;

            case TKvolatile:
                tyx = mTYvolatile;
                goto Lcvx;

            case TKrestrict:
                tyx = mTYrestrict;
                goto Lcvx;

            case TK_unaligned:
                tyx = mTYunaligned;
            Lcvx:
                t->Tcount++;
                type_setty(&t,t->Tty | tyx);
                t->Tcount--;
                stoken();
                continue;

#if linux || __APPLE__ || __FreeBSD__ || __OpenBSD__
            case TK_attribute:
                {
                int attrtype;
                int mod = getattributes(NULL,FALSE,&attrtype);
                if (attrtype & ATTR_LINKMOD)
                {
                    if (mod & mTYvolatile)
                    {
                        t->Tcount++;
                        type_setty(&t,t->Tty | mTYvolatile);
                        t->Tcount--;
                    }
                    if (mod & mTYconst)
                    {
                        t->Tcount++;
                        type_setty(&t,t->Tty | mTYconst);
                        t->Tcount--;
                    }
                    if (mod & (mTYstdcall|mTYcdecl))
                        tym |= (mod & (mTYstdcall|mTYcdecl));
                    mod &= ~(mTYvolatile|mTYconst|mTYstdcall|mTYcdecl);
                    attrtype &= ~ATTR_LINKMOD;
                }
#if DEBUG
                assert(ATTR_CAN_IGNORE(attrtype));
#endif
                continue;
                }
#endif
            default:
            Ldefault2:
                if (tym)
                {   synerr(EM_illegal_type_combo);      // illegal combination of types
                    tym = 0;
                    mangle = 0;
                }
            case TKcomma:
            Ldefault:
                if (CPP)
                {
                    if (flag == 1)
                        goto Lnd;
                    if (s)                      // if there was an X::
                    {   synerr(EM_ident_exp);   // identifier expected (after ::)
                        s = NULL;
                    }
                }
                tstart = t;
                tstart->Tcount++;
                goto ret;
        }
    }
ret:

  if (!CPP || flag != 2)                // if not in conversion function
    while (1)
    {   type **pt;

        if (tok.TKval == TKlpar)        // function returning
        {
            stoken();
        L13:
            inprototype++;
            t2 = getprototype(vident,t);
            inprototype--;
            if (CPP)
            {
                if (!t2)                        // if not a function
                {
                    token_unget();              // back up one token
                    tok.TKval = TKlpar;
                    break;
                }

                // Parse const and volatile postfixes
                if (!(vident && paramlst))
                    t2->Tty |= type_qualifier_list();

                if (tok.TKval == TKthrow)
                {   gdeclar.hasExcSpec = 1;
                    except_exception_spec(t2);  // parse exception-specification
                }
            }
        }
        else if (tok.TKval == TKlbra)   /* else [ const_exp opt ]       */
        {
            stoken();
            t2 = type_alloc(TYarray);   // array of
            if (!(CPP && ANSI))
            {   // C99 6.7.5.2 Array declarators
                if (tok.TKval == TKstatic)
                {   t2->Tflags |= TFstatic;
                    stoken();
                }
                t2->Tty |= type_qualifier_list();
                if (tok.TKval == TKstatic && !(t2->Tflags & TFstatic))
                {   t2->Tflags |= TFstatic;
                    stoken();
                }
                if (tok.TKval == TKstar && !(t2->Tflags & TFstatic))
                {
                    stoken();
                    if (tok.TKval == TKrbra)
                    {
                        t2->Tflags |= TFvla;
                        goto Lclosebra;
                    }
                    token_unget();
                    tok.TKval = TKstar;
                }
            Lclosebra:
                ;
            }
            if (tok.TKval == TKrbra)    // if no dimension
                t2->Tflags |= TFsizeunknown;
            else
            {   if (CPP && ANSI && !pstate.STintemplate)
                {
                    t2->Tdim = msc_getnum();    // array dimension
                    if ((int)t2->Tdim < 0 || (ANSI && t2->Tdim == 0))
                        synerr(EM_array_dim, (int)t2->Tdim);    // array dimension must be > 0
                }
                else
                {   elem *e;

                    e = assign_exp();
                    if (!tyintegral(e->ET->Tty) &&
                        !(CPP && pstate.STintemplate && tybasic(e->ET->Tty) == TYident))
                        synerr(EM_integral);    // need integral expression
                    e = poptelem(e);
                    if (e->Eoper == OPsizeof)
                    {   e->Eoper = OPconst;
                        e->EV.Vlong = type_size(e->EV.sp.Vsym->Stype);
                    }
                    if (e->Eoper == OPconst)    // if fixed dimension
                    {
                        t2->Tdim = el_tolong(e);        // array dimension
                        if ((int)t2->Tdim < 0 || (ANSI && t2->Tdim == 0))
                            synerr(EM_array_dim, (int)t2->Tdim);        // array dimension must be > 0
                        el_free(e);
                    }
                    else
                    {   // It's a VLA
                        t2->Tel = e;
                        t2->Tflags |= TFvla;
                    }
                }
            }
            chktok(TKrbra,EM_rbra);     // closing ']'
        }
        else if (tok.TKval == TKlcur && tyfunc(tstart->Tty))
        {   synerr(EM_explicit_param);          // can't inherit function type
            break;
        }
        else
            break;

        /* Insert t2 into the tstart list just before t                 */
        t2->Tnext = t;                  /* t is what func is returning  */
        for (pt = &tstart; *pt != t; pt = &((*pt)->Tnext))
            type_debug(*pt);
        *pt = t2;                       /* insert t2 into linked list   */
        t2->Tcount++;

        if (tok.TKval == TKlcur)
            break;
    }

    if (linkage != LINK_CPP)
    {
        // Don't let default linkage be C++
        if (type_mangle(tstart) == mTYman_cpp)
            type_setmangle(&tstart,mTYman_c);
    }

    /* If pascal or fortran linkage, set the bits for the name mangling.
        This is necessary for global variables.
     */
    // For C++ linkage, we *always* use C++ name mangling.
    if ((linkage == LINK_CPP || !mangle) &&
        !tyfunc(tstart->Tty) && !pstate.STinparamlist &&//!inprototype &&
        vident && vident[0])
        mangle = varmangletab[linkage];

    if (mangle)
        type_setmangle(&tstart,mangle);

#if TX86
    tym |= initial;
#endif

#if linux || __APPLE__ || __FreeBSD__ || __OpenBSD__
    if (tok.TKval == TK_attribute)
    {
        int attrtype;
        int mod = getattributes(NULL,FALSE,&attrtype);
        if (attrtype & ATTR_TYPEMOD)
        {
            tym &= ~mTYbasic;
            tym |= (mod & mTYbasic);
            mod &= ~mTYbasic;
            attrtype &= ~ATTR_TYPEMOD;
        }
        if (attrtype & ATTR_TRANSU)
        {
            tym |= mTYtransu;
            attrtype &= ~ATTR_TRANSU;
        }
#if DEBUG
        assert(mod == 0);
        assert(ATTR_CAN_IGNORE(attrtype));
#endif
    }
#endif

    if (tym)
    {
        tym |= tstart->Tty;
        type_setty(&tstart,tym);
    }

  if (CPP)
  {
    if (tconv != NULL ||                // if user-defined type conversion
        op != OPunde)                   // if operator function
    {
        if (!tyfunc(tstart->Tty))
        {   //cpperr(EM_opovl_function);        // must be a function
            if (tconv)
            {   op = OPMAX;
                type_free(tconv);
            }
        }
        else if (tconv)
        {   if (tybasic(tstart->Tnext->Tty) != TYint)
                cpperr(EM_conv_ret);    // no return type for conversion function
            type_free(tstart->Tnext);
            tstart->Tnext = tconv;
            op = OPMAX;
        }
        else if (op == OPnew || op == OPanew)
            tstart->Tflags |= TFfuncret;        // overload on function return value

        // Force C++ name mangling
        type_setmangle(&tstart, mTYman_cpp);
    }
    gdeclar.class_sym = (Classsym *)s;
    gdeclar.namespace_sym = sn;
    gdeclar.oper = op;
    gdeclar.constructor = constructor;
    gdeclar.destructor = destructor;
    gdeclar.invariant = invariant;
    gdeclar.ptal = ptal;
    gdeclar.explicitSpecialization = explicitSpecialization;
Lpop:
    if (constructor | destructor | invariant)
    {
        // Force C++ name mangling
        type_setmangle(&tstart, mTYman_cpp);
    }
    pstate.STclasssym = classsymsave;   // out of class scope
    while (cpushcount--)
        scope_pop();
  }
  return tstart;


Lnd:                            // if new-declarator
    tstart = t;                 // start of type list
    tstart->Tcount++;
    goto Lpop;
}

/*****************************
 * Get prototype for function or parameter list.
 * Input:
 *      fident  Identifier of the function (NULL if not known)
 *      tret    What the function is returning
 * Returns:
 *      type of function
 *      NULL if not a function
 */

STATIC type * getprototype(const char *fident,type *tret)
{   char vident[2*IDMAX + 1];
    type *tfunc;
    type *pt;

    //printf("getprototype()\n");

    /* Function returning               */
    tfunc = type_alloc(FUNC_TYPE((int) linkage,config.memmodel));
    type_setmangle(&tfunc,funcmangletab[(int) linkage]);
    if (tok.TKval == TKrpar)            /* if () then no prototype      */
    {
        if (config.flags3 & CFG3strictproto)
            tfunc->Tflags |= TFfixed | TFprototype;     /* make like (void) */
        stoken();
        goto ret2;
    }
    tfunc->Tflags |= TFprototype;       /* there must be a prototype    */
    if (tok.TKval == TKellipsis)        // if (...)
    {   if (ANSI)
            synerr(EM_rpar);            // varargs must have at least one arg
        stoken();
        goto ret;
    }

    pstate.STignoretal = 0;             // can't ignore it anymore
    pstate.STinparamlist++;             // we're parsing the list
    if (tok.TKval == TKregister)        // storage class means it's a prototype
    {
        stoken();
        type_specifier(&pt);
        goto L4;
    }

    /* Determine if this is not a function declaration, but an initializer
        for a variable, as in
                int x(6);
        Be careful about old-style function definitions.
     */
    if (CPP)
    {   int i = isexpression();

        if (i == 1 || i == 2)
        {   tfunc->Tcount++;
            type_free(tfunc);
            pstate.STinparamlist--;
            return NULL;
        }
    }

    if (type_specifier(&pt))            /* we are doing a prototype     */
    {   param_t **pp;
        //param_t *px;

    L4:
        //px = NULL;
        pp = &tfunc->Tparamtypes;
        scope_push(pp, (scope_fp)param_search, SCTparameter);
        while (1)
        {   type *tparam;
            param_t *p;

            /* Watch for special case of (void) */
            if (tfunc->Tparamtypes == NULL &&   /* if first parameter and */
                tybasic(pt->Tty) == TYvoid &&   /* it's a void and      */
                tok.TKval == TKrpar)            /* closed by )          */
            {   tfunc->Tflags |= TFfixed;
                type_free(pt);
                break;
            }

            tparam = declar(pt,vident,0);
            type *tp2 = tparam;
            if (tyref(tp2->Tty))
                tp2 = tp2->Tnext;
            if (tybasic(tp2->Tty) == TYarray)
                tp2->Tflags |= TFfuncparam;
            fixdeclar(tparam);
            tp2->Tflags &= ~TFfuncparam;
            type_free(pt);

            /* Convert function to pointer to function  */
            if (tyfunc(tparam->Tty))
            {   tparam = newpointer(tparam);
                tparam->Tnext->Tcount--;
                tparam->Tcount++;
            }

            /* Convert <array of> to <pointer to> in prototypes         */
            else if (tybasic(tparam->Tty) == TYarray)
                tparam = topointer(tparam);

            if (CPP)
                chknoabstract(tparam);

            p = param_calloc();
            p->Ptype = tparam;
            *pp = p;                    /* append to parameter list     */
            pp = &p->Pnext;
            param_debug(p);

            if (vident[0])              /* if there was an identifier   */
                p->Pident = mem_strdup(vident);

            // Look for default parameter initializer
            if (CPP && tok.TKval == TKeq)
            {
#if 0
                // Declare existing parameters up to, but not including,
                // current parameter.
                if (!px)
                    px = tfunc->Tparamtypes;
                for (; px != p; px = px->Pnext)
                {   symbol *sp;
                    type *t;

                    t = px->Ptype;
                    sp = symbol_define(px->Pident, SCTlocal, SCparameter);
                    sp->Stype = t;
                    t->Tcount++;
                }
#endif
                stoken();               /* skip over =                  */
                pstate.STdefertemps++;
                pstate.STdeferaccesscheck++;
                pstate.STdefaultargumentexpression++;

                /* Skip over default value, since we cannot parse it in
                 * cases like: template<class T> int f(const T x = T(3));
                 */

                if (pstate.STintemplate)
                {   int paren = 0;

                    while (1)
                    {
                        switch (tok.TKval)
                        {
                            case TKlpar:
                                paren++;
                                break;

                            case TKrpar:
                                if (paren == 0)
                                    goto L6;
                                paren--;
                                break;

                            case TKcomma:
                                if (paren == 0)
                                    goto L6;
                                break;
                        }
                        stoken();
                    }
                L6: ;
                    /* Provide a dummy so overload resolution will work.
                     * Parse it for real on template instantiation.
                     */
                    p->Pelem = el_longt(tsint, 1);
                }
                else if (pstate.STdeferDefaultArg)
                {   int paren = 0;
                    token_t **ptail = &p->PelemToken;
                    token_t *t;

                    while (1)
                    {
                        t = token_copy();
                        *ptail = t;
                        ptail = &(t->TKnext);
                        switch (tok.TKval)
                        {
                            case TKlpar:
                                paren++;
                                break;

                            case TKrpar:
                                if (paren == 0)
                                    goto L7;
                                paren--;
                                break;

                            case TKcomma:
                                if (paren == 0)
                                    goto L7;
                                break;
                        }
                        stoken();
                    }
                L7: ;
                    /* Provide a dummy so overload resolution will work.
                     * Parse it for real on template instantiation.
                     */
                    p->Pelem = el_longt(tsint, 1);
                }
                else
                {
                    /* Defer type conversion until we use the parameter
                     * (in case the conversion generates any temporaries)
                     */
                    p->Pelem = arraytoptr(assign_exp());
                }

                pstate.STdefaultargumentexpression--;
                pstate.STdeferaccesscheck--;
                pstate.STdefertemps--;
            }

            switch (tok.TKval)
            {
                case TKrpar:                    // if reached end of prototype
                    tfunc->Tflags |= TFfixed;
                    break;

                case TKcomma:
                    stoken();                   // skip over ,
                    goto Lcase;

                case TKellipsis:
                    if (!CPP)
                        goto L1;
                Lcase:
                    switch (tok.TKval)
                    {
                        case TKrpar:
                            break;

                        case TKellipsis:                // if ,...
                        L3: stoken();
                            if (tok.TKval != TKrpar)    // should be ,...)
                            {
                        L1:     synerr(EM_rpar);        // ')' expected
                                panic(TKrpar);
                            }
                            break;

                        case TKregister:                // ignore register keyword
                            stoken();
                            type_specifier(&pt);
                            continue;

                        default:
                            if (!type_specifier(&pt))
                            {
                                type_free(pt);
                                goto L1;
                            }
                            continue;
                    }
                    break;
                default:
                    goto L1;
            }
            break;
        }
        scope_pop();
        //deletesymtab();               /* exit function prototype scope */
    }
    else                                /* else a simple parameter list */
    {   //deletesymtab();
        type_free(pt);
        tfunc->Tflags &= ~TFprototype;  /* which is not a prototype     */
        if (tok.TKval == TKident)
        {
            if (level != 0 || paramlst || !fident)
            {
                synerr(EM_param_context);       // param list out of context
                panic(TKrpar);
            }
            else
            {   symbol *s;
                type *prevty;

                s = scope_search(fident,SCTglobal);
                prevty = (s) ? s->Stype : NULL;
                getparamlst(tfunc,prevty);
            }
        }
    }
    pstate.STinparamlist--;             // we're done parsing the list
ret:

    /* If variable argument list, and pascal function type, change      */
    /* to cdecl function type.                                          */
    if (variadic(tfunc))
    {
        switch (tybasic(tfunc->Tty))
        {   case TYnpfunc:
            case TYnsfunc:
                tfunc->Tty = (tfunc->Tty & ~mTYbasic) | TYnfunc;
                break;

            case TYfsfunc:
            case TYfpfunc:
                tfunc->Tty = (tfunc->Tty & ~mTYbasic) | TYffunc;
                break;

#if TX86
            case TYf16func:             /* no variable arg list in pascal */
            case TYjfunc:
#endif
                synerr(EM_param_context);       // FIX - better error message
                tfunc->Tty = (tfunc->Tty & ~mTYbasic) | TYffunc;
                break;                  /* ??? needed for C->pascal interface ??? */
        }
    }

    chktok(TKrpar,EM_param_rpar);       // closing parenthesis
ret2:
#if linux || __APPLE__ || __FreeBSD__ || __OpenBSD__
    if (tok.TKval == TK_attribute)
    {

        int attrtype;
        int mod = getattributes(NULL,FALSE,&attrtype);
        if (attrtype & ATTR_FUNCINFO)
        {
            tfunc->Tty |= mTYnoret;
            attrtype &= ~ATTR_FUNCINFO;
            mod &= ~mTYnoret;
        }
        if (attrtype & ATTR_LINKMOD)
        {
            if(mod & mTYstdcall)
                tfunc->Tty = (tfunc->Tty & ~mTYbasic) | TYnsfunc;
            if(mod & mTYcdecl)
                tfunc->Tty = (tfunc->Tty & ~mTYbasic) | TYnfunc;
            if(mod & mTYconst)
                tfunc->Tty |= mTYconst;
            if(mod & mTYvolatile)
                tfunc->Tty |= mTYvolatile;
        }
#if DEBUG
        attrtype &= ~(ATTR_FUNCINFO|ATTR_LINKMOD);
        mod &= ~(mTYstdcall|mTYcdecl|mTYvolatile|mTYconst);
        assert(mod == 0);
        assert(ATTR_CAN_IGNORE(attrtype));
#endif
    }
    else if (tok.TKval == TK_asm)
    {
        // if a prototype is trailed by __asm__("name")
        // the new name is used for the function name
        lnx_redirect_funcname(fident);
    }
#endif
    return tfunc;
}


/*****************************
 * Read in parameter_list.
 * parameter_list ::= identifier {, identifier }
 * Output:
 *      paramlst        points to parameter list
 *      tfunc has duplicate of tprev prototype, if any
 */

STATIC void getparamlst(type *tfunc,type *tprev)
{
  param_t *p,**pp;
  int nparam;
  int nactual;

  if (paramlst && tok.TKval != TKrpar)
        param_free(&paramlst);
  nparam = 32767;                       /* random very large integer    */
  if (tprev)
  {     /*tfunc->Tflags = tprev->Tflags;*/      /* so typematch() won't fail    */
        /*tfunc->Tparamtypes = list_link(tprev->Tparamtypes);*/
        if (tfunc->Tflags & TFfixed)
        {       nparam = 0;
                for (p = tprev->Tparamtypes; p; p = p->Pnext)
                {       param_debug(p);
                        nparam++;               /* count parameters     */
                }
        }
  }
    nactual = 0;
    pp = &paramlst;
    while (tok.TKval == TKident)
    {
        nactual++;
        p = param_calloc();
        p->Pident = mem_strdup(tok.TKid);       /* copy in identifier   */
        *pp = p;
        pp = &(p->Pnext);               /* append to list               */
        if (stoken() != TKcomma)        /* commas mean more idents      */
            break;
        if (stoken() == TKident)
            continue;
        if (tok.TKval == TKellipsis)
        {   stoken();
            tfunc->Tflags |= TFprototype;
            break;
        }
        synerr(EM_ident_exp);                   /* identifier expected          */
        panic(TKrpar);                  /* slurp for ')'                */
    }
    if (nactual > nparam)
        synerr(EM_num_args,nparam,"function",nactual);
}

/**********************************
 * Do declaration parsing for identifier.
 * Returns:
 *      type of identifier
 */

type *declar_fix(type *typ_spec,char *vident)
{   type *t;

    t = declar(typ_spec,vident,0);
    assert(t);
    type_debug(t);
    linkage_kwd = t->Tty & (mTYLINK | mTYMOD | mTYTFF);
    fixdeclar(t);
    return t;
}

/*********************************
 * Fix declarations.
 * Fix array sizes, and check for illegal declarations.
 * Modify types based on if extended modifier keywords were present.
 */

void fixdeclar(type *t)
{   type *tn;
    tym_t tym;
    tym_t newtym;
    tym_t nearfar;

    //printf("fixdeclar()\n");
    //type_print(t);
    while (t)
    {   type_debug(t);
        tn = t->Tnext;
        tym = t->Tty;
        switch (newtym = tybasic(tym))
        {
            case TYffunc:
            case TYfpfunc:
            case TYnfunc:
            case TYnpfunc:
            case TYnsfunc:
            case TYfsfunc:
            case TYnsysfunc:
            case TYfsysfunc:
                switch (tybasic(tn->Tty))
                {
                    case TYstruct:
                        // Can't return abstract classes
                        if (!CPP || !(tn->Ttag->Sstruct->Sflags & STRabstract))
                            break;
                        /* FALL-THROUGH */
                    case TYarray:
                    case TYnfunc:
                    case TYffunc:
                    case TYnpfunc:
                    case TYfpfunc:
                    case TYnsfunc:
                    case TYfsfunc:
                    case TYnsysfunc:
                    case TYfsysfunc:
                        synerr(EM_return_type); // can't return those types
                        break;
                }
                /* Adjust function type based on modifier keywords      */
                switch (tym & (mTYLINK|mTYTFF|mTYbasic))
                {
                    case            mTYnear | TYffunc:
                    case mTYcdecl | mTYnear | TYffunc:
                    case mTYcdecl           | TYnpfunc:
                    case mTYcdecl           | TYnsfunc:
                    case mTYcdecl | mTYnear | TYnpfunc:
                    case mTYcdecl | mTYnear | TYnsfunc:
                    case mTYcdecl | mTYnear | TYfpfunc:
                    case mTYcdecl | mTYnear | TYfsfunc:
                    case mTYcdecl           | TYnsysfunc:
                    case mTYcdecl | mTYnear | TYnsysfunc:
                    case mTYcdecl | mTYnear | TYfsysfunc:
                        newtym = TYnfunc;
                        break;
                    case            mTYfar | TYnfunc:
                    case mTYcdecl | mTYfar | TYnfunc:
                    case mTYcdecl          | TYfpfunc:
                    case mTYcdecl          | TYfsfunc:
                    case mTYcdecl | mTYfar | TYnpfunc:
                    case mTYcdecl | mTYfar | TYnsfunc:
                    case mTYcdecl | mTYfar | TYfpfunc:
                    case mTYcdecl | mTYfar | TYfsfunc:
                    case mTYcdecl          | TYfsysfunc:
                    case mTYcdecl | mTYfar | TYnsysfunc:
                    case mTYcdecl | mTYfar | TYfsysfunc:
                        newtym = TYffunc;
                        break;
                    case             mTYnear | TYfpfunc:
                    case mTYpascal | mTYnear | TYfpfunc:
                    case mTYpascal           | TYnfunc:
                    case mTYpascal           | TYnsfunc:
                    case mTYpascal | mTYnear | TYnfunc:
                    case mTYpascal | mTYnear | TYnsfunc:
                    case mTYpascal | mTYnear | TYffunc:
                    case mTYpascal | mTYnear | TYfsfunc:
                    case mTYpascal           | TYnsysfunc:
                    case mTYpascal | mTYnear | TYnsysfunc:
                    case mTYpascal | mTYnear | TYfsysfunc:
                        newtym = TYnpfunc;
                        break;
                    case             mTYfar | TYnpfunc:
                    case mTYpascal | mTYfar | TYnpfunc:
                    case mTYpascal          | TYffunc:
                    case mTYpascal          | TYfsfunc:
                    case mTYpascal | mTYfar | TYnfunc:
                    case mTYpascal | mTYfar | TYnsfunc:
                    case mTYpascal | mTYfar | TYffunc:
                    case mTYpascal | mTYfar | TYfsfunc:
                    case mTYpascal          | TYfsysfunc:
                    case mTYpascal | mTYfar | TYnsysfunc:
                    case mTYpascal | mTYfar | TYfsysfunc:
                        newtym = TYfpfunc;
                        break;
                    case              mTYnear | TYfsfunc:
                    case mTYstdcall | mTYnear | TYfsfunc:
                    case mTYstdcall           | TYnfunc:
                    case mTYstdcall           | TYnpfunc:
                    case mTYstdcall | mTYnear | TYnfunc:
                    case mTYstdcall | mTYnear | TYnpfunc:
                    case mTYstdcall | mTYnear | TYffunc:
                    case mTYstdcall | mTYnear | TYfpfunc:
                    case mTYstdcall           | TYnsysfunc:
                    case mTYstdcall | mTYnear | TYnsysfunc:
                    case mTYstdcall | mTYnear | TYfsysfunc:
                        newtym = TYnsfunc;
                        break;
                    case              mTYfar | TYnsfunc:
                    case mTYstdcall | mTYfar | TYnsfunc:
                    case mTYstdcall          | TYffunc:
                    case mTYstdcall          | TYfpfunc:
                    case mTYstdcall | mTYfar | TYnfunc:
                    case mTYstdcall | mTYfar | TYnpfunc:
                    case mTYstdcall | mTYfar | TYffunc:
                    case mTYstdcall | mTYfar | TYfpfunc:
                    case mTYstdcall          | TYfsysfunc:
                    case mTYstdcall | mTYfar | TYnsysfunc:
                    case mTYstdcall | mTYfar | TYfsysfunc:
                        newtym = TYfsfunc;
                        break;
                    case              mTYnear | TYfsysfunc:
                    case mTYsyscall | mTYnear | TYfsysfunc:
                    case mTYsyscall           | TYnfunc:
                    case mTYsyscall           | TYnpfunc:
                    case mTYsyscall | mTYnear | TYnfunc:
                    case mTYsyscall | mTYnear | TYnpfunc:
                    case mTYsyscall | mTYnear | TYffunc:
                    case mTYsyscall | mTYnear | TYfpfunc:
                    case mTYsyscall           | TYnsfunc:
                    case mTYsyscall | mTYnear | TYnsfunc:
                    case mTYsyscall | mTYnear | TYfsfunc:
                        newtym = TYnfunc; //TYnsysfunc;
                        break;
                    case              mTYfar | TYnsysfunc:
                    case mTYsyscall | mTYfar | TYnsysfunc:
                    case mTYsyscall          | TYffunc:
                    case mTYsyscall          | TYfpfunc:
                    case mTYsyscall | mTYfar | TYnfunc:
                    case mTYsyscall | mTYfar | TYnpfunc:
                    case mTYsyscall | mTYfar | TYffunc:
                    case mTYsyscall | mTYfar | TYfpfunc:
                    case mTYsyscall          | TYfsfunc:
                    case mTYsyscall | mTYfar | TYnsfunc:
                    case mTYsyscall | mTYfar | TYfsfunc:
                        newtym = TYffunc; //TYfsysfunc;
                        break;

                    case mTYjava | mTYnear | TYfsfunc:
                    case mTYjava           | TYnfunc:
                    case mTYjava           | TYnpfunc:
                    case mTYjava | mTYnear | TYnfunc:
                    case mTYjava | mTYnear | TYnpfunc:
                    case mTYjava | mTYnear | TYffunc:
                    case mTYjava | mTYnear | TYfpfunc:
                    case mTYjava           | TYnsysfunc:
                    case mTYjava | mTYnear | TYnsysfunc:
                    case mTYjava | mTYnear | TYfsysfunc:
                        newtym = TYjfunc;
                        break;

                    case TYnfunc:
                    case mTYnear | TYnfunc:
                    case mTYcdecl | TYnfunc:
                    case mTYcdecl | mTYnear | TYnfunc:

                    case TYffunc:
                    case mTYfar | TYffunc:
                    case mTYcdecl | TYffunc:
                    case mTYcdecl | mTYfar | TYffunc:

                    case TYnpfunc:
                    case mTYnear | TYnpfunc:
                    case mTYpascal | TYnpfunc:
                    case mTYpascal | mTYnear | TYnpfunc:

                    case TYfpfunc:
                    case mTYfar | TYfpfunc:
                    case mTYpascal | TYfpfunc:
                    case mTYpascal | mTYfar | TYfpfunc:

                    case TYnsfunc:
                    case mTYnear | TYnsfunc:
                    case mTYstdcall | TYnsfunc:
                    case mTYstdcall | mTYnear | TYnsfunc:

                    case TYfsfunc:
                    case mTYfar | TYfsfunc:
                    case mTYstdcall | TYfsfunc:
                    case mTYstdcall | mTYfar | TYfsfunc:

                    case TYnsysfunc:
                    case mTYnear | TYnsysfunc:
                    case mTYsyscall | TYnsysfunc:
                    case mTYsyscall | mTYnear | TYnsysfunc:

                    case TYfsysfunc:
                    case mTYfar | TYfsysfunc:
                    case mTYsyscall | TYfsysfunc:
                    case mTYsyscall | mTYfar | TYfsysfunc:
                        /* It's already the correct type        */
                        break;

                    case mTYfar16 | mTYpascal | TYnfunc:
                    case mTYfar16 | mTYpascal | mTYnear | TYnfunc:
                    case mTYfar16 | mTYpascal | TYnpfunc:
                    case mTYfar16 | mTYpascal | mTYnear | TYnpfunc:
                    case mTYfar16 | mTYpascal | TYnsfunc:
                    case mTYfar16 | mTYpascal | mTYnear | TYnsfunc:
                    case mTYfar16 | mTYpascal | TYnsysfunc:
                    case mTYfar16 | mTYpascal | mTYnear | TYnsysfunc:
                        newtym = TYf16func;
                        break;

                    case mTYinterrupt | TYnfunc:
                    case mTYinterrupt | mTYfar | TYnfunc:
                    case mTYinterrupt | TYnpfunc:
                    case mTYinterrupt | mTYfar | TYnpfunc:
                    case mTYinterrupt | TYnsfunc:
                    case mTYinterrupt | mTYfar | TYnsfunc:
                    case mTYinterrupt | TYnsysfunc:
                    case mTYinterrupt | mTYfar | TYnsysfunc:

                    case mTYinterrupt | TYffunc:
                    case mTYinterrupt | mTYfar | TYffunc:
                    case mTYinterrupt | TYfpfunc:
                    case mTYinterrupt | mTYfar | TYfpfunc:
                    case mTYinterrupt | TYfsfunc:
                    case mTYinterrupt | mTYfar | TYfsfunc:
                    case mTYinterrupt | TYfsysfunc:
                    case mTYinterrupt | mTYfar | TYfsysfunc:

                    case mTYinterrupt | mTYcdecl | TYnfunc:
                    case mTYinterrupt | mTYcdecl | mTYfar | TYnfunc:
                    case mTYinterrupt | mTYcdecl | TYnpfunc:
                    case mTYinterrupt | mTYcdecl | mTYfar | TYnpfunc:
                    case mTYinterrupt | mTYcdecl | TYnsfunc:
                    case mTYinterrupt | mTYcdecl | mTYfar | TYnsfunc:
                    case mTYinterrupt | mTYcdecl | TYnsysfunc:
                    case mTYinterrupt | mTYcdecl | mTYfar | TYnsysfunc:

                    case mTYinterrupt | mTYcdecl | TYffunc:
                    case mTYinterrupt | mTYcdecl | mTYfar | TYffunc:
                    case mTYinterrupt | mTYcdecl | TYfpfunc:
                    case mTYinterrupt | mTYcdecl | mTYfar | TYfpfunc:
                    case mTYinterrupt | mTYcdecl | TYfsfunc:
                    case mTYinterrupt | mTYcdecl | mTYfar | TYfsfunc:
                    case mTYinterrupt | mTYcdecl | TYfsysfunc:
                    case mTYinterrupt | mTYcdecl | mTYfar | TYfsysfunc:
                        newtym = TYifunc;
                        break;

                    default:
                    err:
                        synerr(EM_illegal_type_combo);  /* illegal combination          */
                        break;
                }
                /* no longer useful     */
                tym &= ~(mTYLINK | mTYTFF);

                // Set global modification bits
                if (config.wflags & WFexport && tyfarfunc(newtym))
                    tym |= mTYexport;
                if (config.wflags & WFloadds)
                    tym |= mTYloadds;

                if (tym & mTYexport && I16 && !tyfarfunc(newtym))
                    synerr(EM_illegal_type_combo);      // can't have near export functions

                if (variadic(t))
                {   switch (newtym)
                    {   case TYnpfunc:
                        case TYnsfunc:
                            newtym = TYnfunc;
                            break;
                        case TYfpfunc:
                        case TYfsfunc:
                            newtym = TYffunc;
                            break;
                    }
                }
                break;

            case TYarray:
                if (!(t->Tflags & TFfuncparam) && pstate.STinparamlist)
                {
                    if (t->Tty & (mTYconst | mTYvolatile | mTYrestrict | mTYunaligned) ||
                        t->Tflags & TFstatic)
                        synerr(EM_array_qual);
                }

                if (tyfunc(tn->Tty)
                    || tyref(tn->Tty)
                    || tybasic(tn->Tty) == TYvoid
                   )
                    synerr(EM_array_of_funcs);  // array of functions or refs isn't allowed
                else
                {   targ_size_t size;

                    fixdeclar(tn);

                    // Array of consts is itself const
                    tym |= tn->Tty & (mTYconst | mTYvolatile | mTYrestrict | mTYLINK);
                    if (intsize == 2 && !type_isvla(t)) // if we need to worry about large sizes
                    {   type *tx;
                        tym_t tymx;

                        for (tx = tn;
                             (tymx = tybasic(tx->Tty)) == TYarray;
                             tx = tx->Tnext)
                            ;

                        if (tymx == TYstruct && tx->Tflags & TFsizeunknown ||
                            tymx == TYident)
                            size = 0;   // defer any error message
                        else
                            size = type_size(tn);
                        if (type_chksize(t->Tdim * (unsigned long) size))
                            t->Tdim = 1;
                    }
                    t->Tty = newtym |
                        (tym & (mTYLINK | mTYconst | mTYvolatile | mTYMOD));
                    return;
                }
                break;
#if TX86
            case TYptr:
            {   type *t2;

                if (tyfunc(tn->Tty))
                {
                    tn->Tty |= (tym & (mTYTFF | mTYMOD));
                    tym &= ~((mTYTFF | mTYMOD) & ~(mTYexport | mTYimport));
                }
                else if (tyref(tn->Tty))
                {   synerr(EM_ptr_to_ref);
                    tn = tserr;
                }
                t2 = newpointer(tn);
                newtym = t2->Tty;
                t2->Tcount++;           // so it will free properly
                type_free(t2);          // and free it
                if (t->Tty & mTYinterrupt)
                    newtym = (newtym & ~mTYbasic) | TYfptr;
                break;
            }
            case TYnptr:
                nearfar = mTYnear;
                goto L1;
            case TYfptr:
                nearfar = mTYfar;
            L1:
                if (tyfunc(tn->Tty))
                {
                    tn->Tty |= nearfar | (tym & (mTYMOD
                                | mTYTFF));
                }
                else if (tyref(tn->Tty))
                {   synerr(EM_ptr_to_ref);
                    tn = tserr;
                }
                fixdeclar(tn);  /* make sure func types are correct */
                t->Tty &= mTYconst | mTYvolatile | mTYLINK | mTYbasic | mTYexport | mTYimport;
                return;

            case TYsptr:
                if (tyfunc(tn->Tty))
                    synerr(EM_illegal_type_combo);      // illegal combination
                else if (tyref(tn->Tty))
                    synerr(EM_ptr_to_ref);      // illegal pointer to reference
                break;
#endif

            case TYmemptr:
                if (tyfunc(tn->Tty))
                {
                    if (!((tn->Tty | t->Tty) & mTYTFF) &&
                        // The following is a kludge to get around
                        // a typedef'd stdcall type being rewritten.
                        tybasic(tn->Tty) != TYnsfunc &&
                        tybasic(tn->Tty) != TYfsfunc)
                    {   if (MFUNC)
                            tn->Tty = (tn->Tty & ~mTYbasic) | TYmfunc;
                        else
                            tn->Tty |= mTYpascal;
                    }
                    tn->Tty |= (tym & (mTYTFF | mTYMOD));
                    tym &= ~(mTYTFF | mTYMOD);
                }
                break;

            case TYnref:
            case TYfref:
            case TYref:
            {   tym_t tnty = tybasic(tn->Tty);

                if (tnty == TYvoid)
                    synerr(EM_void_novalue);    // void& is an illegal type
                else if (tyref(tnty))
                {   cpperr(EM_ref_to_ref);      // reference to reference
                    type_settype(&t->Tnext,tserr);
                    return;
                }
                break;
            }
#if linux || __APPLE__ || __FreeBSD__ || __OpenBSD__
            case TYstruct:
                if (tym & mTYtransu)
                {                               // linux transparent union
                    newtym |= mTYtransu;
                }
#endif
        }
        t->Tty = newtym |
            (tym & (mTYLINK | mTYconst | mTYvolatile | mTYMOD));
        t = tn;                         /* next type                    */
    } /* while */
}

/*****************************
 * Generate an instance of the anonymous union given by tspec.
 */

STATIC symbol * anonymous(Classsym *stag,enum SC sc_specifier)
{   symbol *s;
    list_t list;
    char *id,*unionid;
    unsigned sct;
    static char prefix[] = "_anon_";

    /* Use first member of union as the name of the union variable      */
    list = stag->Sstruct->Sfldlst;
    if (!list)                          /* if no members                */
        return NULL;
    id = list_symbol(list)->Sident;
    unionid = (char *) alloca(sizeof(prefix) + strlen(id));
    strcpy(unionid,prefix);
    strcat(unionid,id);

    /* Generate the union symbol        */
    if (ANSI && (sc_specifier == SCextern || sc_specifier == SCglobal))
        cpperr(EM_glbl_ambig_unions);   // anonymous unions must be static
    sct = (sc_specifier == SCextern) ? SCTglobal | SCTnspace : SCTglobal | SCTnspace | SCTlocal;
    s = scope_define(unionid,sct,sc_specifier);
    s->Stype = stag->Stype;
    s->Stype->Tcount++;
    type_setmangle(&s->Stype,mTYman_c);

    /* Generate a symbol for each member        */
    for (; list; list = list_next(list))
    {   symbol *sm,*sa;

        sm = list_symbol(list);

        /* Define the symbol    */
        sa = scope_define(sm->Sident,sct,SCanon);
        sa->Smemoff = sm->Smemoff;
        sa->Stype = sm->Stype;
        sa->Stype->Tcount++;
        sa->Sscope = (Classsym *)s;
    }

    datadef(s);                 /* handle initializer, if any           */
    if (level == 0 && s->Sclass != SCextern && s->Sclass != SCcomdef)
        outdata(s);
    return s;
}

/**********************************
 * Given a declaration for a symbol, either declare a new symbol
 * or find which symbol it already is.
 * Input:
 *      vident          identifier for symbol
 *      dt              type that was declared for it
 *      sc_specifier    current storage class
 *      ptpl            if function template, this is the template parameter list
 *      level           declaration level
 *                      -1 : class member
 *                      0  : global
 *                      1  : parameter
 *                      >1 : function block
 *      gdeclar.oper    if vident is an operator overload
 *      gdeclar.class_sym if gdeclar.class_sym::vident
 * Returns:
 *      pointer to symbol for this
 *      NULL don't declare this one
 */

symbol *symdecl(char *vident,type *dt,enum SC sc_specifier, param_t *ptpl)
{   type *prevty;
    symbol *s;
    enum SC sc2;
    int sct;

    //printf("symdecl('%s', dt = %p, gdeclar.class_sym = %p)\n",vident, dt, gdeclar.class_sym);
    //printf("sc_specifier = "); WRclass(sc_specifier); printf("\n");
    //printf("dt->Tty = x%x\n", dt->Tty);
    //type_print(dt);

    if (CPP)
    {
      sc2 = sc_specifier;
      if (sc2 == SCfriend)
        sc_specifier = SCextern;
      if (gdeclar.class_sym)            /* if id is a class member      */
      {   char *di;
        Classsym *stag;

        stag = gdeclar.class_sym;
        if (sc2 == SCfriend)
        {
            s = cpp_findmember_nest(&stag,vident,FALSE);
        }
        else
        {
            template_instantiate_forward(stag);
            s = n2_searchmember(stag,vident);
        }
        if (s)
        {
            goto L1;
        }
        di = mem_strdup(cpp_unmangleident(vident));
        err_notamember(di,gdeclar.class_sym);
        mem_free(di);
      }
      else if (gdeclar.namespace_sym)   // if vident is a member of a namespace
      {
        s = nspace_searchmember(vident,gdeclar.namespace_sym);
        if (s)
            goto L1;
        cpperr(EM_nspace_undef_id,vident,gdeclar.namespace_sym->Sident);        // id not in namespace
      }

      // Allow redundant things like typedef struct A A;
      if (sc_specifier == SCtypedef &&
        (tybasic(dt->Tty) == TYstruct || tybasic(dt->Tty) == TYenum) &&
        strcmp(dt->Ttag->Sident,vident) == 0 &&
        !isclassmember(dt->Ttag) &&
        dt->Ttag->Sscope == scope_inNamespace())
      {
        type_free(dt);
        return NULL;
      }

      // Make sure operator overloads are functions
      if (gdeclar.oper == OPMAX && !tyfunc(dt->Tty))
        cpperr(EM_opovl_function);      // must be a function

      if (scope_search(vident, SCTtempsym))
        cpperr(EM_template_parameter_redeclaration, vident);
    }

    /* Check for possible re-definition of existing identifier  */
    sct = SCTglobal;
    if (scope_inNamespace())
        sct = SCTnspace;
    if (sc_specifier == SCfuncalias || (CPP && sc_specifier == SCextern && tyfunc(dt->Tty)))
        sct |= SCTlocal;
    if ((level == 0 || sc_specifier == SCextern ||
         tyfunc(dt->Tty) && sc_specifier != SCtypedef) &&
      (s = scope_searchinner(vident,sct)) != 0) // and it's already defined
    {
      if (CPP)
      {
    L1:
        //printf("\talready defined\n");
        /* Allow structs to be redeclared as other variables or */
        /* functions, for struct tag name space stuff.          */
        switch (s->Sclass)
        {   case SCstruct:
            case SCenum:
                goto L6;
            case SCmember:
            case SCfield:
                err_noinstance(s->Sscope,s);
                goto L6;
        }

        if (tyfunc(dt->Tty))
        {
            if (gdeclar.class_sym)      /* if member function           */
            {
                /* Member functions are always prototyped               */
                /* Make it (void) if none                               */
                if (!(dt->Tflags & TFprototype))
                    dt->Tflags |= TFprototype | TFfixed;
            }

            if (s->Sclass == SCoverload)
            {   type_free(s->Stype);
                goto L2;
            }

            // If overloaded function, look for the right symbol
            if (tyfunc(s->Stype->Tty))
            {   symbol *sp;

                switch (sc_specifier)
                {
                    case SCfunctempl:
                        sp = cpp_findfunc(dt,ptpl,s,3);
                        break;
                    case SCtypedef:
                        sp = cpp_findfunc(dt,NULL,s,0);
                        break;
                    default:
                        sp = cpp_findfunc(dt,NULL,s,1);
                        break;
                }
                if (sp)
                {
                    s = sp;
                    // If this is an expansion of a template member
                    // function that we already have an implementation of
                    if (pstate.STflags & PFLmftemp && s->Sflags & SFLimplem)
                    {
                     L9:
                        if (tok.TKval != TKsemi && tok.TKval != TKcomma)
                        {   token_free(token_funcbody(FALSE));
                            tok.TKval = TKsemi;
                        }
                        type_free(dt);
                        //printf("already have implementation\n");
                        return NULL;
                    }
                    if (s->Sclass == SCfuncalias)
                    {   // Already defined
                        synerr(EM_multiple_def,cpp_prettyident(s->Sfunc->Falias));
                    }
                    goto L3;
                }

                /* Can only have one function with non-C++ linkage      */
                if (type_mangle(dt) != mTYman_cpp)
                {   symbol *so;

                    for (so = s; so; so = so->Sfunc->Foversym)
                    {
                        if (type_mangle(so->Stype) != mTYman_cpp)
                        {   //synerr(EM_multiple_def,prettyident(so));
                            nwc_typematch(so->Stype,dt,so);
                            break;
                        }
                    }
                }
                sp = s;
                //s = symbol_name(vident,SCunde,dt);
                s = symbol_name(vident,sc_specifier,dt);
                type_free(dt);
                s->Sfunc->Fflags |= Foverload /*| Ftypesafe*/ | Fnotparent;

                // Append to list of overloaded functions
                {   symbol **ps;

                    for (ps = &sp->Sfunc->Foversym; *ps; ps = &(*ps)->Sfunc->Foversym)
                        ;
                    *ps = s;
                }
                s->Sscope = scope_inNamespace();

                ph_add_global_symdef(s, SCTglobal);

                /* No matching function symbol. So create a new one     */
                if (gdeclar.class_sym)
                {   char *si;

                    s->Sscope = gdeclar.class_sym;
                    s->Sfunc->Fclass = gdeclar.class_sym;
                    si = mem_strdup(cpp_prettyident(s));
                    err_notamember(si,gdeclar.class_sym);
                    mem_free(si);
                }
                /* If overloading a member function, always gen name    */
                /* for root symbol                                      */
                if (level == -1 && sp->Sscope)
                    sp->Sfunc->Fflags |= Ftypesafe;

                goto L8;
            }
        }
        else
        {
            // If this is an expansion of a template member
            // data that we already have an implementation of
            if (pstate.STflags & PFLmftemp && s->Sflags & SFLimplem)
                goto L9;
        }
    L3: ;
      }
        prevty = s->Stype;
        if (tyfunc(prevty->Tty))
        {
            if (tyfunc(dt->Tty))
            {
                if (CPP
                    ? (!linkage_spec ||
                       // If non-static member func, don't change linkage_spec
                       (gdeclar.class_sym && !(s->Sfunc->Fflags & Fstatic))
                      )
                    : !linkage_kwd
                   )
                {
                    /* Default to function type from previous symbol
                     * (but pick up __export or __loadds from either source)
                     */
                    type_setty(&dt,prevty->Tty | (dt->Tty & mTYMOD));
                    type_setmangle(&dt,type_mangle(prevty));
                }
            }
        }
        else
        {
            type *t;
            tym_t ty;

            // Inherit stuff from previous declaration
            if (!(dt->Tty & (mTYLINK | mTYimport | mTYexport)) &&
                (ty = prevty->Tty & (mTYLINK | mTYimport | mTYexport)) != 0)
                type_setty(&dt,dt->Tty | ty);

            // Inherit mangling from previous declaration
            if (type_mangle(dt) == mTYman_cpp)  // if default
                type_setmangle(&dt,type_mangle(prevty));

            // If initializer
            if (tok.TKval == TKeq
                || (CPP && tok.TKval == TKlpar) // arguments to constructor
               )
            {
                // Need to allow:
                //  static int k;
                //  extern int k = 99;

                if (s->Sflags & SFLimplem
                    /* || s->Sclass == SCstatic */
                   )
                {
                    synerr(EM_multiple_def,prettyident(s));     // already defined
                }
                else if (sc_specifier == SCstatic)
                {
                    if (s->Sclass == SCextern && !ANSI)
                    {   // Because other compilers allow this, allow:
                        //      extern int x;
                        //      static int x = 3;
                        sc_specifier = SCstatic;
                    }
                    else if (s->Sclass == SCstatic)
                    {   // Allow:
                        //  static int j;
                        //  static int j = -99;
                    }
                    else
                    {
                        synerr(EM_multiple_def,prettyident(s)); // already defined
                    }
                }
            }
            else
            {   switch (s->Sclass)
                {
                    case SCcomdef:
                        if (!CPP || !s->Sscope)
                            goto L10;
                    case SCstatic:
                        if (sc_specifier == SCglobal)
                        {   synerr(EM_multiple_def,prettyident(s));     // already defined
                            goto L5;
                        }
                        goto L10;

                    case SCglobal:
                    case SCcomdat:
                    L10:
                        if (!typematch(prevty,dt,1))
                            goto L7;
                    L5:
                        type_free(dt);
                        return NULL;                    // ignore redeclaration
                    case SCstruct:
                    case SCenum:
                        break;          // allow redeclaration
                    default:
                        if (sc_specifier == SCextern)
                            goto L10;
                        break;
                }
            }
        }

        type_debug(dt);
        if (!typematch(prevty,dt,4|1) && sc_specifier != SCfunctempl)
        {
        L7:
            nwc_typematch(prevty,dt,s);
            if (tybasic(prevty->Tty) == TYstruct)
            {   /* Can't dump the struct, so create a new symbol        */
                static int num;

                sprintf(vident,"_u%d",num++);
                s = scope_define(vident,SCTglobal | SCTnspace,SCunde);
            }
            else if (prevty->Tty != dt->Tty)
                goto useprev;           /* use previous type            */
            else
                type_free(prevty);
            type_debug(dt);
        }

        /* If function, and no prototype specified, use previous        */
        /* definition because it might have a prototype. In any         */
        /* case, we're not worse off.                                   */
        else if (!CPP &&
                 tyfunc(dt->Tty) &&
                 !(dt->Tflags & TFprototype) &&
                 !(prevty->Tflags & TFgenerated))
        {
         useprev:
            if (dt->Tty & (mTYexport | mTYnaked))
                type_setty(&prevty,prevty->Tty | (dt->Tty & (mTYexport | mTYnaked)));
            type_free(dt);
            dt = prevty;                /* use original type            */
            type_debug(dt);
        }
        else
        {
            if (CPP && tyfunc(dt->Tty))
                /* Copy over default parameters */
                nwc_defaultparams(prevty->Tparamtypes,dt->Tparamtypes);
            // Use "_export" if it occurred on either the previous or current
            if (prevty->Tty & (mTYexport | mTYnaked))
                type_setty(&dt,dt->Tty | (prevty->Tty & (mTYexport | mTYnaked)));

            type_debug(dt);
            type_free(prevty);
            type_debug(dt);
        }
        if (CPP &&
            sc_specifier == SCglobal &&
            (s->Sclass == SCextern || s->Sclass == SCcomdef) &&
            dt->Tty & mTYconst)
            s->Sclass = tyfunc(dt->Tty) ? SCglobal : SCpublic; //type_struct(dt) ? SCpublic : SCextern;
#if linux || __APPLE__ || __FreeBSD__ || __OpenBSD__
        else if (!CPP && !ClassInline(sc_specifier) && !SymInline(s))
        {
            s->Sclass = sc_specifier;   /* force current storage class  */
        }
#else
        else if (!CPP || ClassInline(sc_specifier) || !SymInline(s))
        {
            s->Sclass = sc_specifier;   /* force current storage class  */
        }
#endif

        // If generating a precompiled header, create a copy of the symbol
        // for the header.
        if (sc_specifier == SCtypedef &&
            level == 0 &&
            config.flags2 & (CFG2phautoy | CFG2phauto | CFG2phgen) &&   // and doing precompiled headers
            !(pstate.STflags & (PFLhxwrote | PFLhxdone)) &&
            cstate.CSfilblk)                    // and there is a source file
        {   Sfile *sf;
            symbol *s1;

            s->Stype = dt;
            s1 = symbol_copy(s);

            ph_add_global_symdef(s1, SCTglobal);
        }
        type_debug(dt);
    }
    else
    {
    L6:
        //printf("\tnot already defined\n");

        if (CPP && tyfunc(dt->Tty))
            nwc_musthaveinit(dt->Tparamtypes);

        if (level == 1 && sc_specifier != SCtypedef)
        {   param_t *p;

            p = paramlst->search(vident);       // check against paramlst
            if (p)
            {   if (p->Ptype)                   // if already declared
                {   synerr(EM_multiple_def, vident);
                    type_free(dt);
                }
                else
                {   param_debug(p);
                    if (CPP)
                        chknoabstract(dt);
                    if (tybasic(dt->Tty) == TYvoid)
                    {   synerr(EM_void_novalue);        // void has no value
                        p->Ptype = tserr;
                        tserr->Tcount++;
                        type_free(dt);
                    }
                    else if (tyfunc(dt->Tty))
                    {   /* Convert function to pointer to function      */
                        p->Ptype = newpointer(dt);
                        dt->Tcount--;
                        p->Ptype->Tcount++;
                    }
                    else if (tybasic(dt->Tty) == TYarray)
                    {   // Convert <array of> to <pointer to> in prototypes
                        p->Ptype = topointer(dt);
                    }
                    else
                        p->Ptype = dt;
                }
            }
            else
            {   synerr(EM_not_param, vident);   // not in parameter list
                type_free(dt);
            }
            return NULL;
        }

        /* Be sure to declare externs or functions at top level */
        sct = SCTglobal | SCTnspace | SCTlocal;
        if ((!CPP && sc_specifier == SCextern) ||
            (tyfunc(dt->Tty) &&
             sc_specifier != SCtypedef &&
             (!CPP || sc_specifier != SCextern) &&
             sc_specifier != SCfuncalias))
        {
            sct = SCTglobal | SCTnspace;
        }
        s = scope_define(vident, sct, sc_specifier);
        if (sytab[s->Sclass] & SCSS)
            s->Sscope = funcsym_p;
    }
L2:
    s->Stype = dt;
L8:
    type_debug(dt);
    if (CPP && sc_specifier != SCtypedef)
        chknoabstract(dt);
    //printf("s->Sclass = "); WRclass((enum SC)s->Sclass); printf("\n");
    return s;
}

/*******************************
 * Fill in symbol table entry for function, once we have determined
 * the correct symbol for it.
 * Input:
 *      s               symbol for function
 *      sc_specifer     storage class for function
 *      opnum           operator number if operator overload function
 *                      OPunde for C
 *      pflags          bit mask
 *              1       parse function body if present
 *              2       this is a template function expansion
 *              4       allow nested function bodies
 * Returns:
 *      TRUE    function body was present
 *      FALSE   no function body
 */

int funcdecl(symbol *s,enum SC sc_specifier,int pflags,Declar *decl)
{
    char body = FALSE;
    int li;
    func_t *f;
    int nparams;
    symbol *sclass;
    param_t *tp;
    param_t *p;
    type *tf;
    int opnum;
#if TARGET_LINUX || TARGET_OSX || TARGET_FREEBSD || TARGET_OPENBSD || TARGET_SOLARIS
    enum_SC save_class = s->Sclass;
#endif

    if (CPP)
    {
        symbol_debug(s);
        f = s->Sfunc;
        assert(f);
        opnum = decl->oper;
#if 0
        dbg_printf("funcdecl(s=%p, '%s', pflags=x%x, Sclass=",s,s->Sident,pflags);
        WRclass((enum SC)s->Sclass);
        dbg_printf(", sc_specifier=");
        WRclass(sc_specifier);
        dbg_printf("\n");
#endif

        if (s->Stype->Tflags & TFemptyexc || s->Stype->Texcspec)
            f->Fflags3 |= Fcppeh;

        sclass = s->Sscope;
        if (sclass)                             // if not in global scope
        {
            if (sclass->Sclass != SCstruct)
                sclass = NULL;
            goto case_def;
        }
    }

    switch (s->Sident[0])
    {
        case 'm':
            if (strcmp(s->Sident,"main"))
                goto case_def;

        case_main:
            // Main function is detailed in CPP98 3.6.1 and
            // C99 5.1.2.2.1 and 5.1.2.2.3.
            // For C++, it must be ither int main() or
            // int main(int argc, char* argv[])
            s->Sfunc->Fflags3 |= Fmain;

            li = (int) LINK_C;

            // Force function type
            type_setty(&s->Stype,FUNC_TYPE(li,config.memmodel));
        case_com:
            type_setmangle(&s->Stype,funcmangletab[li]);
        case_sc:
            if (sc_specifier == SCstatic || ClassInline(sc_specifier))
            {
                if (CPP)
                    cpperr(EM_main_type);       // invalid storage class for main()
                else
                    synerr(EM_storage_class,(sc_specifier == SCstatic) ? "static" : "__inline");                        // storage class illegal
            }
            break;

#if TARGET_WINDOS
        case 'w':
            if (config.exe == EX_WIN32)
            {
                if (strcmp(s->Sident,"wmain") == 0)
                    goto case_main;

                if (strcmp(s->Sident,"wWinMain") == 0)
                    goto case_winmain;
            }
            goto case_def;

        case 'D':
            if (config.exe != EX_WIN32 || strcmp(s->Sident,"DllMain") ||
                type_mangle(s->Stype) != mTYman_cpp)
                goto case_def;
            // DllMain never gets C++ name mangling
            switch (tybasic(s->Stype->Tty))
            {   case TYnfunc:    li = (int) LINK_C;         goto case_com;
                case TYnpfunc:   li = (int) LINK_PASCAL;    goto case_com;
                case TYnsfunc:   li = (int) LINK_STDCALL;   goto case_com;
                case TYnsysfunc: li = (int) LINK_SYSCALL;   goto case_com;
            }
            goto case_def;

        case 'W':
            if (strcmp(s->Sident,"WinMain"))
                goto case_def;
        case_winmain:
            if (config.exe == EX_WIN32)
            {   // Default to stdcall name mangling for NT version
                type_setmangle(&s->Stype,mTYman_std);
                goto case_sc;
            }
            li = (int) LINK_PASCAL;
            type_setty(&s->Stype, (s->Stype->Tty & mTYloadds) | FUNC_TYPE(li,config.memmodel));
            goto case_com;

        case 'L':
            if (config.exe == EX_WIN32 || strcmp(s->Sident,"LibMain"))
                goto case_def;
        case_pascal:
            li = (int) LINK_PASCAL;
            type_setty(&s->Stype, (s->Stype->Tty & mTYloadds) | FUNC_TYPE(li,I16 ? Lmodel : Smodel));
            goto case_com;
#endif
        default:
        case_def:
            if (CPP && !(f->Fflags & Flinkage))
            {   /* Add in typesafe linkage for C++ functions    */
                if (type_mangle(s->Stype) == mTYman_cpp)
                    f->Fflags |= Foverload | Ftypesafe;
            }
            break;
    }
    if (CPP)
    {
      f->Fflags |= Flinkage;

      if (sclass)                               // if member function
      {
#if TX86
        if (sclass->Sstruct->Sflags & STRexport)
            type_setty(&s->Stype,s->Stype->Tty | mTYexport); // add in _export
        if (sclass->Sstruct->Sflags & STRimport)
            type_setty(&s->Stype,s->Stype->Tty | mTYimport);
#endif
        f->Fflags |= Foverload;         /* it can always be overloaded  */

        // I don't know what this was from
        if (0 && ANSI && sc_specifier == SCstatic)
            cpperr(EM_static_mem_func);         // member functions can't be static

        if (linkage_kwd & (mTYpascal | mTYcdecl | mTYstdcall | mTYsyscall | mTYjava))
            f->Fflags |= Fkeeplink;     // don't change linkage later
      }

      if (s->Stype->Tty & (mTYconst | mTYvolatile) &&
        (f->Fflags & Fstatic || !sclass) &&
        s->Sclass != SCtypedef)
        cpperr(EM_cv_func);             // can't be const or volatile

    /* Mark overloadable functions. Default storage class       */
    /* for overloaded functions is global.                      */
      if (sc_specifier == SCoverload || s->Sclass == SCoverload)
      {
        f->Fflags |= Foverload;
        if (s->Sclass != SCoverload)
            sc_specifier = (enum SC) s->Sclass;
        else
        {   if (sc_specifier == SCoverload)
                sc_specifier = SCglobal;
            s->Sclass = sc_specifier;
        }
      }

      tf = s->Stype;
      type_debug(tf);

      // Count number of parameters
      tp = tf->Tparamtypes;
      nparams = 0;
      for (p = tp; p; p = p->Pnext)
      { param_debug(p);
        nparams++;
      }

      if (opnum == OPMAX)               // if user-defined type conversion
      { f->Fflags |= Fcast;
        if (!sclass)                    /* if not a member              */
            cpperr(EM_conv_member);     // type conversions must be members
        else if (nparams != 0)          /* 'this' is the only parameter */
            cpperr(EM_n_op_params,"0"); // incorrect number of parameters
        else
        {   /* Add to list of type conversions for this class           */
            //printf("funcdecl: add '%s' to Scastoverload\n", s->Sident);
            if (!list_inlist(sclass->Sstruct->Scastoverload,s))
                list_append(&sclass->Sstruct->Scastoverload,s);
        }
      }
      else if (opnum != OPunde)         // if operator overload
      { list_t *plist;
        tym_t tyret = tf->Tnext->Tty;
        param_t *tpp;

        if (nparams == 2)
            tpp = tp->Pnext;
        if (sclass)                     /* if member of a class         */
        {
            if (opnum == OPnew || opnum == OPanew ||
                opnum == OPdelete || opnum == OPadelete)
                f->Fflags |= Fstatic;
            else if (!(f->Fflags & Fstatic))
            {   nparams++;              /* the hidden 'this' parameter  */
                tpp = tp;
            }
        }

        /* See if the unary operator is what was meant          */
        if (nparams == 1)
        {   switch (opnum)
            {   case OPmin:     opnum = OPneg;          break;
                case OPmul:     opnum = OPind;          break;
                case OPand:     opnum = OPaddr;         break;
                case OPadd:     opnum = OPuadd;         break;
                case OPpostinc: opnum = OPpreinc;       break;
                case OPpostdec: opnum = OPpredec;       break;
            }
        }

        /* Special cases for each type of operator      */
        switch (opnum)
        {   case OParrow:
            {
                /* Can only be one of these types:
                        pointer to class
                        object of a class for which -> is defined
                        ref to object of a class for which -> is defined
                                (but can't be class of which operator->()
                                 is a member)
                 */

                type *tfn = tf->Tnext;

                tyret = tybasic(tyret);
                if (typtr(tyret) &&
                    tybasic(tfn->Tnext->Tty) == TYstruct)
                        ;               /* pointer to class             */
                else
                {
                    if (tyref(tyret))
                    {   tfn = tfn->Tnext;
                        tyret = tybasic(tfn->Tty);
                    }

                    if (tyret == TYstruct &&
                        tfn->Ttag != sclass &&
                        n2_searchmember(tfn->Ttag,"?C"))  // should be OParrow from newman.c
                    {
                            ;           /* object of a class with ->    */
                    }
                    else if (sclass && sclass->Sstruct->Stempsym)
                    {
                        // Don't do return type check for template expansions
                        f->Fflags3 |= F3badoparrow;
                    }
                    else
                    {
#if 0 // This restriction seems to be gone from C++98 13.5.6
                        Outbuffer buf;
                        char *p1;

                        p1 = type_tostring(&buf, tf->Tnext);
                        cpperr(EM_bad_oparrow, p1);     // invalid return type
#endif
                    }
                }
                if (!sclass || f->Fflags & Fstatic)
                    /* -> () and [] operator functions must be non-static members */
                    cpperr(EM_non_static);
                /* FALL-THROUGH */
            }
            default:
                if (OTbinary(opnum))
                {   if (nparams != 2)
                        goto L5;
                }
                else /* unary */
                {
            L8:
                    if (nparams != 1)
                    L5: cpperr(EM_n_op_params,"1");     // incorrect number of parameters
                }
                break;
            case OPpostinc:
            case OPpostdec:
                if (nparams != 2)
                    goto L5;
                if (tpp->Ptype->Tty != TYint)
                    cpperr(EM_postfix_arg);     // second parameter must be of type int
                break;
            case OPcall:
            case OPbrack:
                if (!sclass || f->Fflags & Fstatic)
                    /* -> () and [] operator functions must be non-static members */
                    cpperr(EM_non_static);
                if (opnum == OPbrack && nparams != 2)
                    goto L5;
                break;
            case OPnew:
            case OPanew:
                /* Verify that it is of type void*()(size_t{,..})       */
                if (nparams == 0 ||
                    !typtr(tyret) ||
                    (tf->Tnext->Tnext->Tty & 0xFF) != TYvoid ||
                    (tp->Ptype->Tty & 0xFF) != TYsize
                   )
                    cpperr(EM_opnew_type,(opnum == OPanew) ? "[]" : "");        // wrong type for operator new()
                param_debug(tp);
                goto L7;
            case OPdelete:
            case OPadelete:
            {
                // Can only be one of these types:
                //      void operator delete(void *)
                //      void operator delete(void *,size_t)
                //      void operator delete(void *,void *)
                type *t;

#if 1
                if (nparams == 0 ||
                    tyret != TYvoid ||
                    !typtr(tp->Ptype->Tty) ||
                    tp->Ptype->Tnext->Tty != TYvoid
                   )
                    cpperr(EM_opdel_type,(opnum == OPadelete) ? "[]" : ""); // wrong type for operator delete()
#else
                if (nparams == 0 ||
                    nparams > 2 ||
                    tyret != TYvoid ||
                    !typtr(tp->Ptype->Tty) ||
                    tp->Ptype->Tnext->Tty != TYvoid ||
                    (nparams == 2 &&
                        !((t = tp->Pnext->Ptype)->Tty == TYsize ||
                          (typtr(t->Tty) &&
                           t->Tnext->Tty == TYvoid)
                         )
                    )
                   )
                    cpperr(EM_opdel_type,(opnum == OPadelete) ? "[]" : ""); // wrong type for operator delete()
#endif
                param_debug(tp);
                if (nparams == 2)
                    param_debug(tp->Pnext);
            }
            L7:
                if (f->Fflags & Fvirtual)
                    cpperr(EM_static_virtual,s->Sident); // can't be static and virtual
                goto L6;                  /* skip parameter check       */
        }

        /* C++98 13.5-6:
         * "An operator function shall either be a non-static member function or be a
         * non-member function and have at least one parameter whose type is a class,
         * a reference to a class, an enumeration, or a reference to an enumeration."
         */
        if (!sclass || f->Fflags & Fstatic) // members automatically satisfy this
        {   param_t *p;

            // Template generated functions don't need to satisfy this
            if (f->Ftempl)
                goto L6;

            for (p = tp; p; p = p->Pnext)
            {   type *t = p->Ptype;
                tym_t ty;

                param_debug(p);
                ty = tybasic(t->Tty);
                if (ty == TYstruct ||
                    ty == TYident ||
                    ty == TYtemplate ||
                    (config.flags4 & CFG4enumoverload && ty == TYenum) ||
                    tyref(ty) &&
                        ((ty = tybasic(t->Tnext->Tty)) == TYstruct ||
                         ty == TYident ||
                         (config.flags4 & CFG4enumoverload && ty == TYenum) ||
                         ty == TYtemplate)
                   )
                    goto L6;
            }
            cpperr(EM_param_class);             // one parameter must be a class
#if 0
            for (p = tp; p; p = p->Pnext)
            {   type *t = p->Ptype;
                type_print(t);
            }
#endif
        L6: ;
        }

        f->Fflags |= Foverload | Foperator | Ftypesafe;
        f->Foper = opnum;
        if (sclass)             /* if function is a member of a class   */
        {   plist = &sclass->Sstruct->Sopoverload;
            if (!list_inlist(*plist,s))
                list_append(plist,s);
        }
        else if (s->Sscope && s->Sscope->Sclass == SCnamespace)
        {
            cpp_operfuncs_nspace[(unsigned)opnum / 32] |= 1 << (opnum & 31);
        }
        else
        {   // Thread all global operator functions into one list
            symbol **ps;

            for (ps = &cpp_operfuncs[opnum]; *ps; ps = &(*ps)->Sfunc->Foversym)
            {
                if (*ps == s)
                    goto L17;
            }
            *ps = s;
        L17:
            ;
        }
      } /* operator overload */

      // We could have forced an SCinline to an SCstatic
      if (s->Sclass == SCunde)
        s->Sclass = sc_specifier; // storage class
      if (s->Sclass == SCglobal && sc_specifier == SCcomdat)
        s->Sclass = SCcomdat;

    // Cleverly make member functions default to __pascal
      if (sclass &&
        !(f->Fflags & Fstatic) &&
        (!(f->Fflags & Fkeeplink) || (MFUNC && f->Fflags & (Fdtor | Finvariant) && tybasic(s->Stype->Tty) == TYnsfunc)) &&
        !variadic(s->Stype) &&
        !(f->Fflags & Fcast && !msbug && (typtr(s->Stype->Tnext->Tty) || tyref(s->Stype->Tnext->Tty)))
       )
      { tym_t tym = s->Stype->Tty;
        tym_t newtym;

        switch (tybasic(tym))
        {
            case TYnpfunc:
            case TYnsfunc:
                if (MFUNC)
                {   newtym = TYmfunc;
                    goto case_set;
                }
                break;

            case TYnfunc:
                newtym = (MFUNC) ? TYmfunc : TYnpfunc;
                goto case_set;
            case TYffunc:
                newtym = TYfpfunc;
                goto case_set;
            case_set:
                type_setty(&s->Stype,(tym & ~mTYbasic) | newtym);
                break;
        }
      }

    }
    else
    {
        s->Sclass = sc_specifier;       // storage class
    }

    if (config.flags & CFGglobal && s->Sclass == SCstatic)
        s->Sclass = SCglobal;           /* make static functions global */

    /*if (level == 1) synerr(EM_decl_spec_seq);*/       /* illegal parameter decl       */
    if (sc_specifier == SCtypedef)
        goto done;
    if (CPP && tok.TKval == TKeq)
    {   targ_uns n;
        {
        L68k:

        if (!(f->Fflags & Fvirtual))
            cpperr(EM_pure_func_virtual);       // pure function must be virtual
        stoken();
        f->Fflags |= Fpure;
        /*s->Sflags |= SFLimplem;*/     /* seen the implementation      */
        n = msc_getnum();
        if (n != 0)
            cpperr(EM_zero);
        }
    }
    else
    if (tok.TKval == TKsemi || tok.TKval == TKcomma)
    {
        if (paramlst)                   // if there was a parameter list
        {
            synerr(EM_param_context);   // parameter list out of context
            param_free(&paramlst);      // free the parameter list
        }
        // Make storage class local or extern
        if (s->Sclass != SCstatic && !SymInline(s) && s->Sclass != SCfunctempl)
            s->Sclass = SCextern;
    }
    else if (CPP && !(pflags & 1))
        body = TRUE;
    else                        /* function body                        */
    {
        if (s->Sclass == SCextern)
            s->Sclass = SCglobal;
        if (CPP)
        {
            if (level != 0 || pflags & 2)       // if not at global scope
            {
                if (level == -1)        // if at class scope
                {   // Read function as a list of tokens
                    s->Sclass = SCinline;
                    if (s->Sfunc->Fbody)
                        synerr(EM_multiple_def,prettyident(s)); // already defined
                    if (tok.TKval != TKlcur &&
                        tok.TKval != TKcolon &&
                        tok.TKval != TKtry)
                    {   cpperr(EM_mem_init_following,prettyident(s));   // function body expected
                        goto done;
                    }
                    else
                        s->Sfunc->Fbody = token_funcbody(FALSE);
                }
                else
                {   if (!(pflags & 4))
                    {   synerr(EM_datadef,prettyident(s)); // expected data def, not func def
                        s->Sclass = SCstatic;
                    }
                    func_nest(s);       // need to nest function definitions
                }
            }
            else                        // else save on stack space
            {
                func_body(s);   // do function body
                funcsym_p = NULL;
            }
        }
        else
        {
            if (level != 0)             // if not at global scope
                synerr(EM_datadef,s->Sident); // expected data def, not func def
            func_body(s);               // do function body
            funcsym_p = NULL;
        }
        stoken();
        body = TRUE;
    }
done:
#if TARGET_LINUX || TARGET_OSX || TARGET_FREEBSD || TARGET_OPENBSD || TARGET_SOLARIS
    lnx_funcdecl(s,sc_specifier,save_class,body);
#endif
    if (body && CPP && type_mangle(s->Stype) != mTYman_cpp)
    {
        //printf("defining '%s'\n", s->Sident);
        scope_define(s->Sident, SCTcglobal, SCglobal);
    }
    return body;
}



/**********************************
 * Determine if types match for symbol s.
 * Print error message if not.
 */

void nwc_typematch(type *t1,type *t2,symbol *s)
{
    if (!typematch(t1,t2,4|1))
        err_redeclar(s,t1,t2);
}

/************************************
 * Set the linkage for function to tym.
 * The function may or may not have already been declared.
 */

void nwc_setlinkage(char *name,long tym,mangle_t mangle)
{   symbol *s;

    s = scope_search(name,SCTglobal);
    if (s)
    {   // Symbol is already defined
        type_setty(&s->Stype,tym | s->Stype->Tty);
        type_setmangle(&s->Stype,mangle);
        fixdeclar(s->Stype);
    }
    else
    {   // Symbol is not defined, so we have to save name away for
        // future reference.
        s = defsy(name,&cstate.CSlinkage);
        s->Sclass = SClinkage;
        s->Slinkage = tym;
        s->Smangle = mangle;
    }
}

/************************************
 * Transfer default parameters from previous type to current type.
 */

void nwc_defaultparams(param_t *pprev,param_t *dnext)
{
    //printf("nwc_defaultparams()\n");

    /* Transfer any default parameters to the new type  */
    if (pprev)
    {   int mustinit = 0;

        do
        {   param_debug(pprev);
            if (dnext)
            {
                char *id = dnext->Pident ? dnext->Pident : NULL;
                int haveinit = 0;

                param_debug(dnext);
                if (pprev->Pelem)
                {
                    if (dnext->Pelem)
                        cpperr(EM_default_redef, id);   // default param redefinition
                    else
                        dnext->Pelem = el_copytree(pprev->Pelem);
                    mustinit = 1;
                    haveinit = 1;
                }
                else if (dnext->Pelem)
                {
                    pprev->Pelem = el_copytree(dnext->Pelem);
                    mustinit = 1;
                    haveinit = 1;
                }

                if (pprev->Pdeftype)
                {
                    if (dnext->Pdeftype)
                        cpperr(EM_default_redef, id);   // default param redefinition
                    else
                    {   dnext->Pdeftype = pprev->Pdeftype;
                        dnext->Pdeftype->Tcount++;
                    }
                    mustinit = 1;
                }
                else if (dnext->Pdeftype)
                {   pprev->Pdeftype = dnext->Pdeftype;
                    pprev->Pdeftype->Tcount++;
                    mustinit = 1;
                }
                else if (mustinit && !haveinit)
                {
                    cpperr(EM_musthaveinit);    // must have initializer
                }

                dnext = dnext->Pnext;
            }
        } while ((pprev = pprev->Pnext) != NULL);
    }
}

/***************************************
 * Verify trailing initializers are non-blank.
 */

void nwc_musthaveinit(param_t *paramtypes)
{   int mustinit = 0;

    for (param_t *p = paramtypes; p; p = p->Pnext)
    {
        if (p->Pelem || p->Pdeftype || p->Psym)
            mustinit = 1;
        else if (mustinit)
            cpperr(EM_musthaveinit);    // must have initializer
    }
}


/********************************************
 * Determine if this statement can be parsed as an expression.
 *      simple-type-name ( expression opt )
 * Returns:
 *      4       it's a template-template-argument
 *      3       it's an expression, but the identifier is undeclared
 *      2       it's an expression
 *      1       expression or a declaration
 *      0       it's a declaration
 */

int isexpression()
{   int parens;
    int rpar;
    int sawident;
    int bra;
    enum_TK lasttok;
    int result;
    symbol *s;
    Token_lookahead tla;

    /*  This function works by looking ahead at tokens until it
        can disambiguate the cases. It only looks as far as
        necessary, to minimize getting the ^ too far out of whack.
        The lookahead tokens are then put back into the input.
        Thus, the position in the token stream is not affected.
     */

    //printf("+isexpression()\n");
Lagain:
    //printf("\tLagain:\n");
    tla.init();
    //tok.print();
    switch (tok.TKval)
    {
        // Storage classes are obviously declarations
        case TKstatic:
        case TKextern:
        case TKauto:
        case TKregister:
        case TKtypedef:
        case TKinline:
        case TKoverload:
        case TKtypename:
        case TKstatic_assert:
#ifdef DEBUG
            tla.term();         // skip for speed reasons
#endif
            return 0;

        case TKnum:
        case TKplpl:
        case TKmimi:
        case TKnot:
        case TKcom:
        case TKmin:
        case TKadd:
        case TKreal_f:
        case TKreal_d:
        case TKreal_da:
        case TKreal_ld:
        case TKstring:
        case TKstar:
        case TKand:
        case TKlpar:
        case TKnew:
        case TKdelete:
        case TKthis:
        case TKoperator:
        case TKsizeof:
        case TK_inf:
        case TK_nan:
        case TK_nans:
        case TKtypeid:
        case TKstatic_cast:
        case TKconst_cast:
        case TKreinterpret_cast:
        case TKdynamic_cast:
        case TKthrow:
        case TKtrue:
        case TKfalse:
        case TKnullptr:
#if linux || __APPLE__ || __FreeBSD__ || __OpenBSD__
        case TK_bltin_const:
#endif
        case TKasm:
#if TX86
        case TK_asm:
        case TK__emit__:
#endif
#ifdef DEBUG
            tla.term();                 // skip for speed reasons
#endif
            return 2;                   /* obviously it's an expression */

    /*  If it's not a simple-type-name (ARM 17.3), then it cannot
        be an expression-statement.
     */
        case TKchar:
        case TKchar16_t:
        case TKshort:
        case TKint:
        case TKlong:
        case TK_int64:
        case TKsigned:
        case TKunsigned:
        case TKfloat:
        case TKdouble:
        case TKvoid:
        case TKbool:
        case TKwchar_t:
        case TKchar32_t:
#if linux || __APPLE__ || __FreeBSD__ || __OpenBSD__
        case TK_attribute:
        case TK_extension:
#endif
            s = NULL;
            break;

        case TKcolcol:
            tla.lookahead();
            if (tok.TKval == TKnew ||
                tok.TKval == TKdelete || tok.TKval == TKoperator)
                goto isexpr;
            if (tok.TKval == TKtemplate)
                // ::template bar<...>
                tla.lookahead();
            if (tok.TKval != TKident)
                goto isdecl;            /* actually, a syntax error     */
            s = scope_search(tok.TKid,SCTglobal);
            if (!s)
                goto isexpr;
            goto L4;

        case TKsymbol:
            s = tok.TKsym;
            goto L6;

        case TKident:
            // It's a simple-type-name if it's a complete-class-name
            // or qualified-type-name
            s = symbol_search(tok.TKid);
            if (!s)
            {   result = 3;
                goto ret;
            }
        L4:
#if 0   // Doesn't work because of ADL, etc.
            // Replace token list with the symbol s so we don't need to
            // look it up again
            token_unget();
            tla.discard();
            tok.setSymbol(s);
#endif
        L6:
            //symbol_print(s);
            switch (s->Sclass)
            {
                case SCnamespace:
                    tla.lookahead();
                    if (tok.TKval == TKcolcol)
                    {
                        tla.lookahead();
                        if (tok.TKval != TKident)
                            goto isexpr;        /* actually, a syntax error */
                        s = nspace_search(tok.TKid,(Nspacesym *)s);
                        if (!s)
                            goto isexpr;        /* syntax error         */
                        goto L4;
                    }
                    goto L3;

                case SCtypedef:
                    if (tybasic(s->Stype->Tty) == TYstruct)
                    {
                        // If no 'typename' in front of dependent type, assume expression
                        if (s->Stype->Tflags & TFdependent)
                        {
                            tla.lookahead();
                            if (tok.TKval == TKcolcol)
                                goto isexpr;
                            s = s->Stype->Ttag;
                            goto L3;
                        }
                        s = s->Stype->Ttag;
                    }
                    else
                        break;
                case SCstruct:
                    tla.lookahead();
                    if (tok.TKval == TKcolcol)
                    {
                L5:     tla.lookahead();
                        if (tok.TKval == TKtemplate)
                            tla.lookahead();
                        if (tok.TKval != TKident)
                            goto isexpr;        /* actually, a syntax error */
#if 1
                        pstate.STstag = (Classsym *)s;
                        s = cpp_findmember_nest(&pstate.STstag,tok.TKid,0);
#else
                        s = struct_searchmember(tok.TKid,(Classsym *)s);
#endif
                        if (!s)
                            goto isexpr;        /* syntax error         */
                        goto L4;
                    }
                    goto L3;

                case SCtemplate:
                    tla.lookahead();
                    if (tok.TKval == TKlt || tok.TKval == TKlg)
                    {   /* It's a template, and we have to do a real parse
                           on it. So, parse it, and then stuff the resulting
                           symbol back into the token stream
                           and use that for subsequent parsing.
                         */
                        if (pstate.STintemplate ||
                            (s->Sscope && s->Sscope->Sclass != SCnamespace))
                            goto isdecl;

                        token_unget();
                        tla.discard();
                        s = template_expand(s,0);
                        tok.setSymbol(s);
                        goto Lagain;
                    }
                    if (tok.TKval == TKident)
                        goto isdecl;
                    if (tok.TKval == TKcomma || tok.TKval == TKgt)
                        goto istemptemparg;
                    break;

                case SCenum:
                    break;

                case SCalias:
                    s = ((Aliassym *)s)->Smemalias;
                    goto L6;

                case SCfunctempl:
                    goto isexpr;

                default:
                    if (s->Scover)
                    {
                        tla.lookahead();
                        if (tok.TKval == TKcolcol)
                        {   s = s->Scover;
                            goto L5;
                        }
                    }
                    goto isexpr;
            }
            s = NULL;
            break;

        case TKdecltype:
            // Skip over the ( expression )
            tla.lookahead();
            if (tok.TKval != TKlpar)
                goto isdecl;
            parens = 1;
            while (1)
            {
                tla.lookahead();
                switch (tok.TKval)
                {
                    case TKlpar:
                        parens++;
                        continue;
                    case TKrpar:
                        parens--;
                        if (parens == 0)
                            break;
                        continue;
                    case TKsemi:
                    case TKeof:
                        goto isdecl;
                    default:
                        continue;
                }
            }
            break;

        default:
            goto isdecl;                /* not an expression-statement  */
    }
    tla.lookahead();
L3:
    if (tok.TKval != TKlpar)
        goto isdecl;

    /* We now have type(
     */

    parens = 1;
    rpar = 0;
    sawident = 0;
    bra = 0;

    lasttok = TKlpar;
    tla.lookahead();

    if (tok.TKval == TKrpar)
    {
        if (s)
            goto isexpr;        // X() is an expression
        else
            goto isdecl;        // int() is a declaration
    }

#if 0
    // Fails with:
    //  char(X::*p)[3];
    if (tok.TKval == TKident || tok.TKval == TKsymbol)
    {
        result = isexpression();
        if (result != 1)
            goto ret;
    }
#endif

    while (1)
    {
        switch (tok.TKval)
        {
            case TKlpar:
                parens++;
                if (rpar)               /* this is probably a func prototype */
                    goto isdecl;
                break;
            case TKrpar:
                rpar = 1;
                if (--parens == 0)
                    goto L2;
                break;

            case TKsymbol:
                s = tok.TKsym;
                goto L7;

            case TKident:
                if (!sawident)
                {   // Could be a cast expression
                    s = symbol_search(tok.TKid);
                L7:
                    if (s)
                    {   switch (s->Sclass)
                        {   case SCtypedef:
                            case SCstruct:
                            case SCenum:
                                lasttok = TKident;
                                tla.lookahead();
                                switch (tok.TKval)
                                {
                                    case TKcolcol:
                                        goto Lbreak;
                                    case TKcomma:
                                        goto isdecl;
                                    case TKstar:
                                    case TKand:
                                    case TKrpar:
                                        if (parens == 1)
                                            goto isdecl;
                                        break;
                                }
                                goto isexpr;
                        }
                    }
                }
            case TKoperator:
                if (sawident && !bra)
                    goto isexpr;
                sawident = 1;
            Lbreak:
                break;
            case TKlbra:
                if (lasttok == TKstar)  // type(*[
                    goto isdecl;
                bra = 1;
                break;
            case TKcolcol:
                if (lasttok == TKident)
                    sawident = 0;
                break;
            case TKstar:
            case TKand:
#if TX86
            /* Parse extended prefixes  */
            case TK_near:
            case TK_far:
            case TK_far16:
            case TK_fastcall:
            case TK_huge:
            case TK_interrupt:
            case TK_ss:
            case TK_cs:
            case TK_based:
            case TK_loadds:
            case TK_export:
            case TK_declspec:
            case TK_saveregs:
            case TK_stdcall:
            case TK_syscall:
            case TK_System:
            case TK_Seg16:
#endif
            case TK_cdecl:
            case TK_fortran:
            case TK_pascal:
            case TK_handle:
            case TKconst:
            case TKvolatile:
            case TK_unaligned:
            case TKrestrict:
                break;
            case TKthis:
                goto isexpr;

            case TKchar:
            case TKchar16_t:
            case TKshort:
            case TKint:
            case TKlong:
            case TK_int64:
            case TKsigned:
            case TKunsigned:
            case TKfloat:
            case TKdouble:
            case TKvoid:
            case TKbool:
            case TKwchar_t:
            case TKchar32_t:
#if linux || __APPLE__ || __FreeBSD__ || __OpenBSD__
            case TK_attribute:
            case TK_extension:
#endif
                if (parens == 1)
                    goto isdecl;
            default:
                if (!bra)
                    goto isexpr;
                break;
        }

        lasttok = (enum_TK)tok.TKval;
        tla.lookahead();
    }

  L2:
    if (!sawident)
        goto isdecl;

    /* Look at the tail, it can tell us which it is     */
    tla.lookahead();
    switch (tok.TKval)
    {
        case TKrestrict:
        case TKconst:
        case TKvolatile:
        case TK_unaligned:
        case TKeq:
            goto isdecl;
        case TKcomma:
        case TKsemi:
        case TKrpar:
        case TKeol:
            break;
        case TKlpar:                    /* could be T(*d)(double(3))    */
        case TKlbra:                    /* could be int(x)["abc"]       */
            goto isdecl;                /* BUG: assume it isn't         */

        case TKplpl:
        case TKmimi:
            goto isexpr;
        default:                        /* assume infix operator        */
            goto isexpr;
    }
    result = 1;                         /* it's ambiguous               */
    goto ret;

  istemptemparg:                        // it's a template-template-argument
    result = 4;
    goto ret;

  isdecl:
    result = 0;                         /* it's a declaration           */
    goto ret;

  isexpr:
    result = 2;                         /* it's an expression           */
  ret:
    tla.term();
    //printf("-isexpression(): %d\n", result);
    return result;
}

/*************************************
 * Parse static_assert(constant-expression, string-literal);
 * Current token is the static_assert.
 */

void parse_static_assert()
{
    assert(tok.TKval == TKstatic_assert);
    stoken();
    chktok(TKlpar, EM_lpar2, "static_assert");
    targ_llong n = msc_getnum();
    char *p = NULL;
    if (tok.TKval == TKcomma)
    {
        stoken();
        if (tok.TKval != TKstring)
        {   synerr(EM_string);                  // string expected
            panic(TKsemi);
            return;
        }
        else
        {   targ_size_t len;
            p = combinestrings(&len);
        }
    }
    chktok(TKrpar, EM_rpar);
    if (n == 0)
    {
        synerr(EM_static_assert, p ? p : "");
    }
    mem_free(p);
    chktok(TKsemi, EM_static_assert_semi);
}

/*******************************************
 * Parse:
 *      decltype ( expression )
 * C++0x 7.1.5.2:
 *      The type denoted by decltype(e) is defined as follows:
 *      1. If e is an id-expression or a class member access (5.2.5 [expr.ref]), decltype(e) is
 *      defined as the type of the entity named by e. If there is no such entity, or e names a set
 *      of overloaded functions, the program is ill-formed.
 *      2. If e is a function call (5.2.2 [expr.call]) or an invocation of an overloaded operator
 *      (parentheses around e are ignored), decltype(e) is defined as the return type of that
 *      function.
 *      3. Otherwise, where T is the type of e, if e is an lvalue, decltype(e) is defined as T&,
 *      otherwise decltype(e) is defined as T.
 *
 *      The operand of the decltype specifier is an unevaluated operand (clause 5 [expr]).
 *      Example:
 *              const int&& foo();
 *              int i;
 *              struct A { double x; }
 *              const A* a = new A();
 *              decltype(foo()); // type is const int&&
 *              decltype(i); // type is int
 *              decltype(a->x); // type is double
 *              decltype((a->x)); // type is const double&
 */

int islvalue(elem *e)
{
  L1:
    if (e->PEFflags & PEFnotlvalue)
        return 0;
    switch (e->Eoper)
    {
        case OPvar:
            if (ANSI)
            {
                /* ANSI 3.3.3.1 lvalue cannot be cast   */
                if (!typematch(e->EV.sp.Vsym->Stype, e->ET, 0) &&
                    // Allow anonymous unions
                    memcmp(e->EV.sp.Vsym->Sident, "_anon_", 6))
                      return 0;
            }
        case OPbit:
        case OPind:
            return 1;

        case OPcond:
            // convert (a ? b : c) to *(a ? &b : &c)
            if (CPP && islvalue(e->E2->E1) && islvalue(e->E2->E2))
                return 1;
            break;

        case OPcomma:
            e = e->E2;
            goto L1;
    }
    return 0;
}

type *parse_decltype()
{
    assert(tok.TKval == TKdecltype);

    char insave = pstate.STinsizeof;    // nest things properly
    char inarglistsave = pstate.STinarglist;

    pstate.STinarglist = 0;             // sizeof protects > and >>
    SYMIDX marksi = globsym.top;
    stoken();
    chktok(TKlpar, EM_lpar2, "decltype");

    pstate.STinsizeof = TRUE;
    int parens = (tok.TKval == TKlpar);
    elem *e = expression();
    pstate.STinsizeof = insave;
    chktok(TKrpar,EM_rpar);     // closing ')'

    type *t = e->ET;

    if (!parens && (e->Eoper == OPvar || e->PEFflags & PEFmember))
    {
        if (e->PEFflags & PEFmember)
            t = e->Emember->Stype;
    }
    else if (islvalue(e))
    {
        // Convert to reference type
        t = newref(t);
    }

    t->Tcount++;
    el_free(e);
    t->Tcount--;

    /* BUG: should fail on overloaded functions:
     *  void foo(int);
     *  void foo(double);
     *  decltype(&foo) fp = &foo;
     */

    for (SYMIDX si = marksi; si < globsym.top; si++)
        globsym.tab[si]->Sflags |= SFLnodtor;

    pstate.STinarglist = inarglistsave;
    return t;
}

#if TX86
/************************************
 * We recognize some special cases of based pointers.
 *      __based(__segname("_DATA"))  => __near
 *      __based(__segname("_STACK")) => __ss
 *      __based(__segname("_CODE"))  => __cs
 * Input:
 *      lexer is on __based
 * Returns:
 *      replacement token if recognized
 *      next token if not recognized
 */

STATIC void nwc_based()
{
    int i;
    static const char *basetab[] =
    {   "CODE",
        "DATA",
        "STACK",
    };
    static enum_TK subst[arraysize(basetab)] = { TK_cs,TK_near,TK_ss };
    char *p;

#ifdef DEBUG
    assert(tok.TKval == TK_based);
#endif
    stoken();
    chktok(TKlpar,EM_lpar);
    if (tok.TKval != TK_segname)
        goto err;
    stoken();
    chktok(TKlpar,EM_lpar);
    if (tok.TKval != TKstring)
        goto err;
    p = tok.TKstr;
    // Microsoft fails to document it, but the _ on the strings
    // is not necessary.
    p += (*p == '_');
    i = binary(p,basetab,arraysize(basetab));
    if (i < 0)
        goto err;
    stoken();
    chktok(TKrpar,EM_rpar);
    if (tok.TKval != TKrpar)
        synerr(EM_rpar);
    tok.TKval = subst[i];
    return;

err:
    tx86err(EM_bad_based_type);                 // unsupported based type
}

/************************************
 * Parse __declspec.
 * Input:
 *      lexer is on __declspec
 * Returns:
 *      mTYxxx
 *      scanner is on ')' token
 */

tym_t nwc_declspec()
{
    int i;
    long ty;
    static const char *basetab[] =
    {
        "dllexport",
        "dllimport",
        "naked",
        "thread",
    };
    static long subst[arraysize(basetab)] = { mTYexport,mTYimport,mTYnaked,mTYthread };

#ifdef DEBUG
    assert(tok.TKval == TK_declspec);
#endif
    stoken();
    chktok(TKlpar,EM_lpar2,"declspec");
    if (tok.TKval != TKident)
        goto err;
    i = binary(tok.TKid,basetab,arraysize(basetab));
    if (i < 0)
        goto err;
    if (stoken() != TKrpar)
        synerr(EM_rpar);                        // ')' expected
    ty = subst[i];
    if (!(config.exe & (EX_WIN32 | EX_DOSX)))
        ty &= ~(mTYimport | mTYthread);         // ignore for non-NT versions
    return ty;

err:
    tx86err(EM_bad_declspec);                   // unsupported __declspec type
    return 0;
}

#endif

#endif
