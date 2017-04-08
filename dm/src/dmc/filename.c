/**
 * Implementation of the
 * $(LINK2 http://www.digitalmars.com/download/freecompiler.html, Digital Mars C/C++ Compiler).
 *
 * Copyright:   Copyright (c) 1993-1998 by Symantec, All Rights Reserved
 *              Copyright (c) 2000-2017 by Digital Mars, All Rights Reserved
 * Authors:     $(LINK2 http://www.digitalmars.com, Walter Bright)
 * License:     Distributed under the Boost Software License, Version 1.0.
 *              http://www.boost.org/LICENSE_1_0.txt
 * Source:      https://github.com/DigitalMars/Compiler/blob/master/dm/src/dmc/filename.c
 */

// Package to handle source files.


#include        <stdio.h>
#include        <string.h>
#if _WIN32
#include        <sys\stat.h>
#endif

#include        "cc.h"
#include        "parser.h"
#include        "global.h"
#include        "filespec.h"

static char __file__[] = __FILE__;      /* for tassert.h                */
#include        "tassert.h"

Srcfiles srcfiles;

#define TRANS   0                       // translated file names

#if TRANS
static unsigned *filename_trans;                // translation array
static unsigned filename_transi;
#endif

/***********************************
 */

static __inline const char *filename_adj(const char *f)
{
    // Remove leading ./
    f = (*f == '.' && (f[1] == '\\' || f[1] == '/')) ? f + 2 : f;

    char *newf = (char *)f;
    char *q;
    for (const char *p = f; *p; ++p)
    {
        if ((*p == '/' || *p == '\\') && p[1] == '.')
        {
            if (p[2] == '/' || p[2] == '\\')
            {   // Replace /./ with /
                if (newf == f)
                {
                    newf = mem_strdup(f);       // memory leak
                    q = newf + (p - f);
                }
                p += 2;
            }
            else if (p[2] == '.' && (p[3] == '/' || p[3] == '\\'))
            {   // Replace /dir/../ with /
                if (newf == f)
                {
                    newf = mem_strdup(f);       // memory leak
                    q = newf + (p - f);
                }
                // Back up q to previous /
                while (q > newf && !(q[-1] == '/' || q[-1] == '\\'))
                    --q;
                --q;
                p += 3;
            }
        }

        if (newf != f)
        {
            *q = *p;
            ++q;
            *q = 0;
        }
    }

    return newf;
}

#ifdef DEBUG
void unittest_filename_adj()
{
    static bool run = false;
    if (run)
        return;
    run = true;

    const char *f;
    f = filename_adj("abc");
    assert(strcmp(f, "abc") == 0);
    f = filename_adj("./abc");
    assert(strcmp(f, "abc") == 0);
    f = filename_adj(".\\abc");
    assert(strcmp(f, "abc") == 0);
    f = filename_adj("abc./d");
    assert(strcmp(f, "abc./d") == 0);
    f = filename_adj("abc../d");
    assert(strcmp(f, "abc../d") == 0);
    f = filename_adj("abc/./d");
    assert(strcmp(f, "abc/d") == 0);
    f = filename_adj("abc\\.\\d/./e");
    assert(strcmp(f, "abc\\d/e") == 0);
    f = filename_adj("abc/d/../e");
    //printf("f = '%s'\n", f);
    assert(strcmp(f, "abc/e") == 0);
    f = filename_adj("abc\\.\\d\\..\\e");
    assert(strcmp(f, "abc\\e") == 0);
}
#endif

/**************************************
 * Compute hash of filename.
 */

STATIC unsigned filename_hash(const char *name)
{
    unsigned hashval;

    int len = strlen(name);
    for (hashval = len; len >= (int)(sizeof(unsigned) - 1); len -= sizeof(unsigned))
    {
        // The ~0x20... is to make it case insensitive
        hashval += *(unsigned *)name & ~0x20202020;
        name += sizeof(unsigned);
    }
    return hashval;
}

/**********************************
 * Convert from Sfile* to Sfile**.
 */

Sfile **filename_indirect(Sfile *sf)
{   unsigned u;

    for (u = 0; u < srcfiles.idx; u++)
    {
        if (srcfiles.pfiles[u] == sf)
            return &srcfiles.pfiles[u];
    }
    assert(0);
    return NULL;
}

/*****************************
 * Search for name in srcfiles.arr[].
 * If found, return pointer for it, else NULL.
 */

Sfile *filename_search(const char *name)
{
    //printf("filename_search('%s',%d)\n",name,srcfiles.idx);
    name = filename_adj(name);
    unsigned hashval = filename_hash(name);
    for (unsigned u = 0; u < srcfiles.idx; u++)
    {   Sfile *sf = &sfile(u);
        if (sf->SFhashval == hashval &&
            filespeccmp(sf->SFname,name) == 0)
            return sf;
    }
    return NULL;
}

/**************************
 * Add name to srcfiles.arr[].
 * Returns:
 *      filename pointer of added name
 */

Sfile *filename_add(const char *name)
{
#ifdef DEBUG
    unittest_filename_adj();
#endif

    name = filename_adj(name);
    Sfile *sf = filename_search(name);
    if (sf)
        return sf;

    // Extend the array
    unsigned u = srcfiles.idx;
    // Make sure pfiles[] is initialized
    if (!srcfiles.pfiles)
        srcfiles.pfiles = (Sfile **) mem_malloc(sizeof(Sfile *) * SRCFILES_MAX);

    sf = (Sfile *) mem_calloc(sizeof(Sfile));
    srcfiles.idx++;
#ifdef DEBUG
    sf->id = IDsfile;
#endif
    sf->SFname = mem_strdup(name);
    sf->SFhashval = filename_hash(sf->SFname);
    if (u >= SRCFILES_MAX)
        err_fatal(EM_2manyfiles,SRCFILES_MAX);
    srcfiles.pfiles[u] = sf;

    return sf;
}

#if !SPP

/*****************************
 * Hydrate srcfiles.
 */

#if HYDRATE
void filename_hydrate(Srcfiles *fn)
{
    unsigned u;

//    ph_hydrate(&fn->arr);
    ph_hydrate(&fn->pfiles);
    for (u = 0; u < fn->idx; u++)
    {   Sfile *sf;

        sf = (Sfile *)ph_hydrate(&fn->pfiles[u]);
        ph_hydrate(&sf->SFname);
        ph_hydrate(&sf->SFinc_once_id);
        list_hydrate(&sf->SFfillist,FPNULL);
    }
}
#endif

/*****************************
 * Dehydrate Srcfiles.
 */

#if DEHYDRATE
void filename_dehydrate(Srcfiles *fn)
{
    unsigned u;

    for (u = 0; u < fn->idx; u++)
    {   Sfile *sf;

        sf = fn->pfiles[u];
        ph_dehydrate(&fn->pfiles[u]);
        ph_dehydrate(&sf->SFname);
        ph_dehydrate(&sf->SFinc_once_id);
        list_dehydrate(&sf->SFfillist,FPNULL);
    }
    //ph_dehydrate(&fn->arr);
    ph_dehydrate(&fn->pfiles);
}
#endif

/******************************
 */

void srcpos_hydrate(Srcpos *s)
{
#if HYDRATE
    ph_hydrate(&s->Sfilptr);
#endif
}

/******************************
 */

void srcpos_dehydrate(Srcpos *s)
{
#if DEHYDRATE
    ph_dehydrate(&s->Sfilptr);
#endif
}

#endif

/*****************************
 * Merge fn with global srcfiles.
 * Construct translation table.
 */

void filename_merge(Srcfiles *fn)
{   unsigned u;

#if !TRANS
    Sfile *sf;

    //dbg_printf("filename_merge()\n");
    for (u = 0; u < fn->idx; u++)
    {   sfile_debug(fn->pfiles[u]);
        filename_add(fn->pfiles[u]->SFname);
    }

    for (u = 0; u < fn->idx; u++)
    {   Sfile *sfn;

        sfn = fn->pfiles[u];
        sfile_debug(sfn);
        sf = filename_search(sfn->SFname);
        sfile_debug(sf);
        fn->pfiles[u] = sf;
        sf->SFflags |= sfn->SFflags & (SFonce | SFtop);

        sf->SFtemp_ft = sfn->SFtemp_ft;
        sf->SFtemp_class = sfn->SFtemp_class;
        sf->SFtagsymdefs = sfn->SFtagsymdefs;
        sf->SFfillist = sfn->SFfillist;
        sf->SFmacdefs = sfn->SFmacdefs;
        sf->SFsymdefs = sfn->SFsymdefs;
        sf->SFcomdefs = sfn->SFcomdefs;
        sf->SFinc_once_id = sfn->SFinc_once_id;
    }
#else
    unsigned t;

    //dbg_printf("filename_merge()\n");
    filename_trans = (unsigned *) mem_realloc(filename_trans,fn->idx * sizeof(unsigned));
    filename_transi = fn->idx;
    for (u = 0; u < fn->idx; u++)
    {   t = filename_add(fn->arr[u].SFname);
        filename_trans[u] = t;
        sfile(t).SFflags |= fn->arr[u].SFflags & (SFonce | SFtop);
        sfile(t).SFinc_once_id = fn->arr[u].SFinc_once_id;
    }
#endif
}

/*********************************
 * Translate file number list.
 */

void filename_mergefl(Sfile *sf)
{
    list_t fl;

    sfile_debug(sf);
    for (fl = sf->SFfillist; fl; fl = list_next(fl))
    {   Sfile *sfl;

        sfl = (Sfile *) list_ptr(fl);
        sfl = filename_search(sfl->SFname);
        assert(sfl);
        sfile_debug(sfl);
        list_ptr(fl) = sfl;
    }
}

/*****************************
 * Translate file number.
 */

void filename_translate(Srcpos *sp)
{
#if TRANS
    if (sp->Slinnum)
    {
        //if (sp->Sfilnum >= filename_transi)
        //dbg_printf("Sfilnum = %d, transi = %d\n",sp->Sfilnum,filename_transi);
        assert(sp->Sfilnum < filename_transi);
        sp->Sfilnum = filename_trans[sp->Sfilnum];
    }
#endif
}

/*****************************
 * Compare file names.
 */

int filename_cmp(const char *f1,const char *f2)
{
    f1 = filename_adj(f1);
    f2 = filename_adj(f2);
    return filespeccmp(f1,f2);
}

/*****************************
 * Free srcfiles.arr[].
 */

#if TERMCODE

void filename_free()
{   unsigned u;

    for (u = 0; u < srcfiles.idx; u++)
    {
        sfile_debug(&sfile(u));
#ifdef DEBUG
        sfile(u).id = 0;
#endif
        mem_free(srcfiles_name(u));
        macro_freelist(sfile(u).SFmacdefs);
        list_free(&sfile(u).SFfillist,FPNULL);
        list_free(&sfile(u).SFtemp_ft,FPNULL);
        list_free(&sfile(u).SFtemp_class,FPNULL);
        mem_free(srcfiles.pfiles[u]);
    }
    //mem_free(srcfiles.arr);
    mem_free(srcfiles.pfiles);
#if TRANS
    mem_free(filename_trans);
#endif
}

#endif
