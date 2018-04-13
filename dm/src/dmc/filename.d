/**
 * Implementation of the
 * $(LINK2 http://www.digitalmars.com/download/freecompiler.html, Digital Mars C/C++ Compiler).
 *
 * Copyright:   Copyright (c) 1993-1998 by Symantec, All Rights Reserved
 *              Copyright (c) 2000-2017 by Digital Mars, All Rights Reserved
 * Authors:     $(LINK2 http://www.digitalmars.com, Walter Bright)
 * License:     Distributed under the Boost Software License, Version 1.0.
 *              http://www.boost.org/LICENSE_1_0.txt
 * Source:      https://github.com/DigitalMars/Compiler/blob/master/dm/src/dmc/filename.d
 */

// Package to handle source files.

module filename;

import core.stdc.stdio;
import core.stdc.string;
version (Windows)
    import core.sys.windows.stat;

import dmd.backend.cc;
import dmd.backend.cdef;
import dmd.backend.global;

import dlist;
import tk.filespec;
import tk.mem;

import msgs2;
import parser;
import precomp;

extern (C++):

__gshared Srcfiles srcfiles;

//version = TRANS;                       // translated file names

version (TRANS)
{
    private __gshared uint* filename_trans;                // translation array
    private __gshared uint  filename_transi;
}

/***********************************
 */

private const(char)* filename_adj(const(char)* f)
{
    // Remove leading ./
    f = (*f == '.' && (f[1] == '\\' || f[1] == '/')) ? f + 2 : f;

    char *newf = cast(char *)f;
    char *q;
    for (const(char)* p = f; *p; ++p)
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

debug void unittest_filename_adj()
{
    __gshared bool run = false;
    if (run)
        return;
    run = true;

    const(char)* f;
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


/**************************************
 * Compute hash of filename.
 */

private uint filename_hash(const(char)* name)
{
    uint hashval;

    int len = strlen(name);
    for (hashval = len; len >= cast(int)(uint.sizeof - 1); len -= uint.sizeof)
    {
        // The ~0x20... is to make it case insensitive
        hashval += *cast(uint *)name & ~0x20202020;
        name += uint.sizeof;
    }
    return hashval;
}

/**********************************
 * Convert from Sfile* to Sfile**.
 */

Sfile **filename_indirect(Sfile *sf)
{
    for (uint u = 0; u < srcfiles.idx; u++)
    {
        if (srcfiles.pfiles[u] == sf)
            return &srcfiles.pfiles[u];
    }
    assert(0);
    return null;
}

/*****************************
 * Search for name in srcfiles.arr[].
 * If found, return pointer for it, else null.
 */

Sfile *filename_search(const(char)* name)
{
    //printf("filename_search('%s',%d)\n",name,srcfiles.idx);
    name = filename_adj(name);
    uint hashval = filename_hash(name);
    for (uint u = 0; u < srcfiles.idx; u++)
    {   auto sf = sfile(u);
        if (sf.SFhashval == hashval &&
            filespeccmp(sf.SFname,name) == 0)
            return sf;
    }
    return null;
}

/**************************
 * Add name to srcfiles.arr[].
 * Returns:
 *      filename pointer of added name
 */

Sfile *filename_add(const(char)* name)
{
    debug unittest_filename_adj();

    name = filename_adj(name);
    Sfile *sf = filename_search(name);
    if (sf)
        return sf;

    // Extend the array
    uint u = srcfiles.idx;
    // Make sure pfiles[] is initialized
    if (!srcfiles.pfiles)
        srcfiles.pfiles = cast(Sfile **) mem_malloc((Sfile *).sizeof * SRCFILES_MAX);

    sf = cast(Sfile *) mem_calloc(Sfile.sizeof);
    srcfiles.idx++;
    debug sf.id = Sfile.IDsfile;
    sf.SFname = mem_strdup(name);
    sf.SFhashval = filename_hash(sf.SFname);
    if (u >= SRCFILES_MAX)
        err_fatal(EM_2manyfiles,SRCFILES_MAX);
    srcfiles.pfiles[u] = sf;

    return sf;
}

version (SPP)
{
}
else
{

/*****************************
 * Hydrate srcfiles.
 */

static if (HYDRATE)
{
void filename_hydrate(Srcfiles *fn)
{
//    ph_hydrate(&fn.arr);
    ph_hydrate(cast(void**)&fn.pfiles);
    for (uint u = 0; u < fn.idx; u++)
    {
        Sfile* sf = cast(Sfile *)ph_hydrate(cast(void**)&fn.pfiles[u]);
        ph_hydrate(cast(void**)&sf.SFname);
        ph_hydrate(cast(void**)&sf.SFinc_once_id);
        list_hydrate(&sf.SFfillist,FPNULL);
    }
}
}

/*****************************
 * Dehydrate Srcfiles.
 */

static if (DEHYDRATE)
{
void filename_dehydrate(Srcfiles *fn)
{
    for (uint u = 0; u < fn.idx; u++)
    {
        Sfile* sf = fn.pfiles[u];
        ph_dehydrate(&fn.pfiles[u]);
        ph_dehydrate(&sf.SFname);
        ph_dehydrate(&sf.SFinc_once_id);
        list_dehydrate(&sf.SFfillist,FPNULL);
    }
    //ph_dehydrate(&fn.arr);
    ph_dehydrate(&fn.pfiles);
}
}

/******************************
 */

void srcpos_hydrate(Srcpos *s)
{
    static if (HYDRATE)
        ph_hydrate(cast(void**)&s.Sfilptr);
}

/******************************
 */

void srcpos_dehydrate(Srcpos *s)
{
    static if (DEHYDRATE)
        ph_dehydrate(&s.Sfilptr);
}

}

/*****************************
 * Merge fn with global srcfiles.
 * Construct translation table.
 */

void filename_merge(Srcfiles *fn)
{
    version (TRANS)
    {
        //dbg_printf("filename_merge()\n");
        filename_trans = cast(uint *) mem_realloc(filename_trans,fn.idx * uint.sizeof);
        filename_transi = fn.idx;
        for (uint u = 0; u < fn.idx; u++)
        {
            uint t = filename_add(fn.arr[u].SFname);
            filename_trans[u] = t;
            sfile(t).SFflags |= fn.arr[u].SFflags & (SFonce | SFtop);
            sfile(t).SFinc_once_id = fn.arr[u].SFinc_once_id;
        }
    }
    else
    {
        Sfile *sf;

        //dbg_printf("filename_merge()\n");
        for (uint u = 0; u < fn.idx; u++)
        {   //sfile_debug(fn.pfiles[u]);
            filename_add(fn.pfiles[u].SFname);
        }

        for (uint u = 0; u < fn.idx; u++)
        {
            Sfile* sfn = fn.pfiles[u];
            //sfile_debug(sfn);
            sf = filename_search(sfn.SFname);
            //sfile_debug(sf);
            fn.pfiles[u] = sf;
            sf.SFflags |= sfn.SFflags & (SFonce | SFtop);

            sf.SFtemp_ft = sfn.SFtemp_ft;
            sf.SFtemp_class = sfn.SFtemp_class;
            sf.SFtagsymdefs = sfn.SFtagsymdefs;
            sf.SFfillist = sfn.SFfillist;
            sf.SFmacdefs = sfn.SFmacdefs;
            sf.SFsymdefs = sfn.SFsymdefs;
            sf.SFcomdefs = sfn.SFcomdefs;
            sf.SFinc_once_id = sfn.SFinc_once_id;
        }
    }
}

/*********************************
 * Translate file number list.
 */

void filename_mergefl(Sfile *sf)
{
    list_t fl;

    //sfile_debug(sf);
    for (fl = sf.SFfillist; fl; fl = list_next(fl))
    {
        Sfile* sfl = cast(Sfile *) list_ptr(fl);
        sfl = filename_search(sfl.SFname);
        assert(sfl);
        //sfile_debug(sfl);
        fl.ptr = sfl;
    }
}

/*****************************
 * Translate file number.
 */

void filename_translate(Srcpos *sp)
{
    version (TRANS)
    {
        if (sp.Slinnum)
        {
            //if (sp.Sfilnum >= filename_transi)
            //dbg_printf("Sfilnum = %d, transi = %d\n",sp.Sfilnum,filename_transi);
            assert(sp.Sfilnum < filename_transi);
            sp.Sfilnum = filename_trans[sp.Sfilnum];
        }
    }
}

/*****************************
 * Compare file names.
 */

int filename_cmp(const(char)* f1,const(char)* f2)
{
    f1 = filename_adj(f1);
    f2 = filename_adj(f2);
    return filespeccmp(f1,f2);
}

/*****************************
 * Free srcfiles.arr[].
 */

static if (TERMCODE)
{

void filename_free()
{
    for (uint u = 0; u < srcfiles.idx; u++)
    {
        //sfile_debug(&sfile(u));
        debug sfile(u).id = 0;
        mem_free(srcfiles_name(u));
        macro_freelist(sfile(u).SFmacdefs);
        list_free(&sfile(u).SFfillist,FPNULL);
        list_free(&sfile(u).SFtemp_ft,FPNULL);
        list_free(&sfile(u).SFtemp_class,FPNULL);
        mem_free(srcfiles.pfiles[u]);
    }
    //mem_free(srcfiles.arr);
    mem_free(srcfiles.pfiles);
    version (TRANS)
        mem_free(filename_trans);
}

}
