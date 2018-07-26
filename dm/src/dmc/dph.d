/**
 * Implementation of the
 * $(LINK2 http://www.digitalmars.com/download/freecompiler.html, Digital Mars C/C++ Compiler).
 *
 * Copyright:   Copyright (c) 1991-1998 by Symantec, All Rights Reserved
 *              Copyright (c) 2000-2017 by Digital Mars, All Rights Reserved
 * Authors:     $(LINK2 http://www.digitalmars.com, Walter Bright)
 * License:     Distributed under the Boost Software License, Version 1.0.
 *              http://www.boost.org/LICENSE_1_0.txt
 * Source:      https://github.com/DigitalMars/Compiler/blob/master/dm/src/dmc/dph.d
 */

/* This file reads/writes precompiled headers. It doesn't know
   much of anything about the format of the file. What we do
   is create our own storage allocator for symbols that need
   to go out to the file. Then, to write the file, we dump
   our special heap to the file.

   The pointers are converted to file offsets, called 'dehydrating'.
   The inverse process of converting file offsets to pointers
   is called 'hydrating'. I don't know if this is standard
   terminology or not.
 */

version (SPP)
{
}
else
{

import core.stdc.stdio;
import core.stdc.stdlib;
import core.stdc.string;
import core.stdc.ctype;
import core.stdc.time;

extern (C) int read(int, void*, size_t);

import dmd.backend.cdef;
import dmd.backend.cc;
import dmd.backend.global;
import dmd.backend.oper;
import dmd.backend.ty;
import dmd.backend.type;

import dmd.backend.dlist;
import filespec;
import dmd.backend.memh;

import dcpp;
import dmcdll;
import msgs2;
import page;
import parser;
import precomp;
import scopeh;


extern (C++):

alias dbg_printf = printf;

enum TX86 = 1;
enum PH_METRICS = 0;
enum TAGGED_ALLOC = 0;

static if (TX86)
{
__gshared
{
char *ph_directory;             /* directory to read PH files from      */

char[9] ph_hxfilename = "scph.sym";
}
}

static if (MEMORYHX)
{

/* This object forms a block of memory kept around from compilation to
   compilation. It is currently used to keep track of where the in-memory
   precompiled header is.
 */

struct Hxblock
{
    int id;             // for debugging
    void *pdata;        // pointer to data
    size_t nbytes;      // size of data
    int mtime;          // timestamp
    void *reserve;      // pointer to reserved address range
    size_t reservesize; // size of that reserved address range
    char[FILENAME_MAX + 1] name;        // name of precompiled header
}

__gshared Hxblock* ph_hx;

enum IDhxblock = ('h' << 24) | ('x' << 16) | ('b' << 8) | 'l';
void hxblock_debug(Hxblock* s) { assert((s).id == IDhxblock); }

}

/***********************
 * Data stored as 'root' to give us hooks into the rest.
 */

struct Root
{
    Config config;              /* configuration                        */
    list_t STincalias;          // list of include aliases
    list_t STsysincalias;       // list of system include aliases
    uint STsequence;            // next Ssequence value for Symbol
    void *mactabroot;           // macro table root
    Symbol *symtab;
    Srcfiles srcfiles;          // list of headers already #include'd
    list_t cppoperfuncs;        // list of operator funcs defined
    uint[(OPMAX + 31) / 32] cppoperfuncs_nspace;
    //list_t template_ftlist;
    //Symbol *template_class_list;
    Symbol *tagsymtab;
static if (H_STYLE & (H_NONE | H_BIT0 | H_OFFSET))
{
    void *baseaddress;          // base address of address space for heap
    size_t size;                // size of pch file
}
    int bufk;                   // value of ph_bufk for PH file
}

__gshared Root *root;

static if (MMFIO)
{
// Use 64Kb for MMFIO because WIN32S has a page granularity of 64Kb, and to
// use -HX, we need a linear address space with heap allocated consecutively
// after where we mapped the HX file in.
enum PHBUFSIZE      = (4096 * 4 * 4);
enum PHADJUST       = 4;        // subtract 4 because the page functions
                                // cannot handle a buffer size of exactly 64Kb
}
else
{
enum PHBUFSIZE      = (4096 * 4);
enum PHADJUST       = 0;
}

enum VMEM_RESERVESIZE       = 30 * 0x100000;

__gshared
{
int ph_inited;           // !=0 if initialized

void **ph_buf;           /* array of allocated buffers           */
int ph_bufmax;           /* allocated dimension of ph_buf[]      */
int ph_bufi;             /* max in use                           */
int ph_bufk;             /* starting point of precompiled header */
uint ph_maxsize = PHBUFSIZE - PHADJUST; // max size of any allocation

static if (LINEARALLOC)
{
uint ph_reservesize = VMEM_RESERVESIZE;
void *ph_mmfiobase;          // fixed address where we map files in
void *ph_baseaddress;        // base address of address space
void *ph_resaddress;         // base address of reserved address space
size_t ph_resaddress_size;   // size reserved
void *ph_resaddress2;        // base address of 2nd reserved address space
size_t ph_resaddress2_size;  // size reserved
}

static if (H_STYLE & H_BIT0)
{
int ph_hdradjust;               // to hydrate a pointer, subtract this value
}

static if (H_STYLE & H_OFFSET)
{
int   ph_hdradjust;             // to hydrate a pointer, subtract this value
void *ph_hdrbaseaddress;        // precompiled header maps at this address
void *ph_hdrmaxaddress;         // max address in precompiled header
}

Root *hx_r;                     // root for HX file
}

void ph_check() { }

version (none)
{
}

/*****************************
 * Reserve memory.
 */

static if (MMFIO)
{

private void * ph_reserve(void *ptr, size_t size)
{
version (__GNUC__) { } else
{
static if (MEMORYHX)
{
    if (ptr && ptr == ph_hx.reserve)
        return ptr;
    else
        return vmem_reserve(ptr,size);
}
else
{
        return vmem_reserve(ptr,size);
}
}
}

/*****************************
 * Release memory.
 */

private void ph_release(void *ptr, size_t size)
{
version (__GNUC__) { } else
{
static if (MEMORYHX)
{
    if (ptr && ptr != ph_hx.reserve)
        vmem_release(ptr,size);
}
else
{
        vmem_release(ptr,size);
}
}
}

}

/**************************
 * Now that we know about command line options for precompiled headers,
 * we know how
 * to initialize the heap to be compatible with the multiple
 * schemes for memory mapped file I/O and precompiled headers.
 * This is the source of the requirement that -H -HF -HX -HY are
 * mutually exclusive.
 * Input:
 *      mmfiobase       if !=0, use this base address
 */

void ph_init(void *mmfiobase, uint reservesize)
{
version (__GNUC__) { } else
{
    ph_inited++;
    dmcdll_HookDetach(&ph_detach);
static if (MMFIO)
{
    if (reservesize && reservesize > 30 && reservesize < 300)
    {
debug
{
        ph_reservesize = reservesize * 0x10000;
}
else
{
        ph_reservesize = reservesize * 0x100000;
}
    }

    // Pick different base addresses for C and C++,
    // so we don't conflict (can't share PH anyway).
    version (_WINDLL)
        enum X = 2;
    else
        enum X = 3;

    ph_mmfiobase = mmfiobase
                ? mmfiobase
                : cast(void *)(cast(char *)vmem_baseaddr() + ph_reservesize * X);
    vmem_reservesize(&ph_reservesize);
    //dbg_printf("Reserving x%lx bytes of virtual address space\n",ph_reservesize);
    if (vmem_physmem() <= 20 * 0x100000)
        config.flags4 &= ~CFG4cacheph;

    if (dmcdll_build_server())
        config.flags4 |= CFG4cacheph;
debug (none)
{
    if (config.flags4 & CFG4cacheph)
        dbg_printf("caching ph in memory\n");
    else
        dbg_printf("not caching ph in memory\n");
}
}
static if (MEMORYHX)
{
    if (!ph_hx)
    {   // Allocate block of persistent memory
        ph_hx = cast(Hxblock *)dmcdll_PersistentAlloc(Hxblock.sizeof);
        assert(ph_hx);
        //dbg_printf("ph_init: ph_hx = %p, ph_hx.pdata = %p, ph_hx.name = '%s'\n",ph_hx,ph_hx.pdata,ph_hx.name);
        if (ph_hx.id)                          // if already existing
            hxblock_debug(ph_hx);               // check for our signature
        ph_hx.id = IDhxblock;
    }
    if (dmcdll_dump_compile_context())
    {   void *reservesave;
        size_t reservesizesave;

        // Delete existing precompiled header from memory

        if (configv.verbose == 2)
            dbg_printf(" dumping context\n");

        assert(ph_hx);
        hxblock_debug(ph_hx);
        globalrealloc(ph_hx.pdata,0);
        reservesave = ph_hx.reserve;
        reservesizesave = ph_hx.reservesize;
        memset(ph_hx,0,Hxblock.sizeof);
        ph_hx.reserve = reservesave;
        ph_hx.reservesize = reservesizesave;
        ph_hx.id = IDhxblock;
    }
    if (ph_hx.reserve)
        ph_reservesize = ph_hx.reservesize;
    else
    {   ph_hx.reserve = ph_reserve(ph_mmfiobase,ph_reservesize);
        ph_hx.reservesize = ph_reservesize;
    }
static if (H_STYLE & H_OFFSET)
{
    // If we can't pick the standard location, let operating system pick.
    if (!ph_hx.reserve)
    {   void *p;
        uint i;

        p = ph_mmfiobase;
        for (i = 0; i < 10; i++)
        {
            p = cast(void *)(cast(char *)p + VMEM_RESERVESIZE);
            if (i == 9)
                p = null;
            ph_hx.reserve = ph_reserve(p,ph_reservesize);
            if (ph_hx.reserve)
                break;
        }
        ph_hx.reservesize = ph_reservesize;
        //dbg_printf("relocating to %p\n",ph_hx.reserve);
    }

    if (ph_hx.reserve)                 // if reserved address range
    {   ph_mmfiobase = ph_hx.reserve;  // use that as our base from now on
        ph_reservesize = ph_hx.reservesize;
    }
    //dbg_printf("mmfiobase = %p\n",ph_mmfiobase);
}
}

    switch (config.flags2 & (CFG2phuse | CFG2phgen | CFG2phauto | CFG2phautoy))
    {
        case CFG2phuse:
        case 0:
static if (LINEARALLOC)
{
            // Reserve a big fat address space (20 Mb).
            // Hopefully, if CFG2phuse, this won't overlap the fixed place
            // where we would load a ph.
            ph_baseaddress = vmem_reserve(null,ph_reservesize);
            if (!ph_baseaddress)
                err_nomem();
            //dbg_printf("Reserving memory at %p .. %p\n",ph_baseaddress,(char *)ph_baseaddress + ph_reservesize);
            ph_resaddress = ph_baseaddress;
            ph_resaddress_size = ph_reservesize;

            /* Put configuration at start of heaps      */
            root = cast(Root *) ph_calloc(Root.sizeof);
            root.baseaddress = ph_baseaddress;
}
else
{
            /* Put configuration at start of heaps      */
            root = cast(Root *) ph_calloc(Root.sizeof);
            root.bufk = 0;
}
            break;

        case CFG2phgen:
static if (MMFIO)
{
            /*  Reserve heap at the address which will form the start of
                the generated precompiled header.
             */
            ph_baseaddress = ph_reserve(ph_mmfiobase,ph_reservesize);
            if (!ph_baseaddress)
                err_fatal(EM_cant_reserve_mem,ph_mmfiobase);    // can't reserve memory
            ph_resaddress = ph_baseaddress;
            ph_resaddress_size = ph_reservesize;

            /* Put configuration at start of heaps      */
            root = cast(Root *) ph_calloc(Root.sizeof);
            root.baseaddress = ph_baseaddress;
}
else
{
static if (LINEARALLOC)
{
            // Reserve a big fat address space (20 Mb)
            ph_baseaddress = vmem_reserve(null,ph_reservesize);
            if (!ph_baseaddress)
                err_nomem();
            ph_resaddress = ph_baseaddress;
            ph_resaddress_size = ph_reservesize;

            /* Put configuration at start of heaps      */
            root = cast(Root *) ph_calloc(Root.sizeof);
            root.baseaddress = ph_baseaddress;
}
else
{
            /* Put configuration at start of heaps      */
            root = cast(Root *) ph_calloc(Root.sizeof);
            root.bufk = 0;
}
}
            break;

        case CFG2phauto:
        case CFG2phautoy:
            ph_auto();
            break;

        default:
            assert(0);
    }

    ph_check();
}
}

/**************************
 */

void ph_term()
{
version (__GNUC__) { } else
{
    //dbg_printf("ph_term()\n");

    {   /* It's possible to recursively come through here if there is some
           failure that keeps calling err_exit() from the cleanup routines
           below.
           So, if we are nesting, then skip.
         */
        __gshared char again;

        if (again)
            return;
        again++;
    }

version (_WIN32)
    enum win32 = true;
else
    enum win32 = false;
static if (!LINEARALLOC && (TERMCODE || win32))
{
    {
    int i;

    for (i = 0; i < ph_bufi; i++)
        free(ph_buf[i]);
    ph_bufi = 0;
    }
}
static if (LINEARALLOC)
{
    vmem_decommit(ph_resaddress,ph_resaddress_size);
    ph_release(ph_resaddress,ph_resaddress_size);
    ph_resaddress = null;
}
static if (MEMORYHX)
{
    vmem_decommit(ph_resaddress2,ph_resaddress2_size);
    ph_release(ph_resaddress2,ph_resaddress2_size);
    ph_resaddress2 = null;
}
    free(ph_buf);
    ph_buf = null;
static if (MMFIO)
{
    vmem_unmapfile();
}
static if (MEMORYHX)
{
    if (ph_hx && !ph_hx.reserve)
    {   ph_hx.reserve = ph_reserve(ph_mmfiobase,ph_reservesize);
        ph_hx.reservesize = ph_reservesize;
    }
}
}
}

/**********************************
 * Called when DLL is detached.
 */


extern (C) private void ph_detach()
{
static if (MEMORYHX)
{
    if (ph_hx && ph_hx.reserve)
    {
        vmem_release(ph_hx.reserve,ph_hx.reservesize);
        ph_hx.reserve = null;
    }
}
}


/**************************
 * Initialize existing buffer.
 */

private void ph_initbuffer(void *buf)
{
version (__GNUC__) { } else
{
    debug memset(buf,'Z',PHBUFSIZE);
    ph_maxsize = page_initialize(buf,PHBUFSIZE - PHADJUST);
    assert(page_maxfree(buf) == PHBUFSIZE - PHADJUST - PAGEOVERHEAD - __PGOFF);
}
}

/**************************
 * Make sure we have at least i new entries in ph_buf[].
 * Returns:
 *      0       out of memory
 *      != 0    success
 */

private int ph_reservebuf(int i)
{
    void **p;

    if (ph_bufi + i > ph_bufmax)
    {
        assert(ph_bufi <= ph_bufmax);
debug
{
        ph_bufmax = ph_bufi + i;
}
else
{
        ph_bufmax += i + 10;
}
        p = cast(void **) realloc(ph_buf,ph_bufmax * (void *).sizeof);
        if (!p)
            return 0;
        ph_buf = p;
    }
    return 1;
}

/**************************
 * Allocate a new buffer.
 * Input:
 *      prealloc        buffer to use if != null
 */

private void * ph_newbuffer(void *prealloc)
{   void *buf;
    void **p;

version (__GNUC__) { } else
{
    assert(ph_inited);
    if (!ph_reservebuf(1))
        goto nomem;
    if (prealloc)
        buf = prealloc;
    else
    {
static if (LINEARALLOC)
{
        if (ph_bufi * PHBUFSIZE >= ph_resaddress_size)
            err_nomem();
        buf = cast(char *)ph_baseaddress + ph_bufi * PHBUFSIZE;
        if (!vmem_commit(buf,PHBUFSIZE))
            err_nomem();
}
else
{
        // Use calloc instead of malloc so that when we write out a .SYM
        // file, the unused areas are not full of irreproducible garbage.
        buf = calloc(PHBUFSIZE,1);
        if (!buf)
            goto nomem;
}
        ph_initbuffer(buf);
    }
    ph_buf[ph_bufi++] = buf;
    //dbg_printf("ph_newbuffer() = %p\n",buf);
    ph_check();
    return buf;

nomem:
}
    return null;
}

/**************************
 * Set adjustment for hydration.
 */

private void ph_setadjust(Root *r)
{
version (__GNUC__) { } else
{
static if (H_STYLE & H_BIT0)
{
    ph_hdradjust = cast(char *)r.baseaddress - (cast(char *)ph_baseaddress + r.bufk * PHBUFSIZE) + 1;
    //printf("adjust = x%x\n",ph_hdradjust);
}
static if (H_STYLE & H_OFFSET)
{
    ph_hdrbaseaddress = r.baseaddress;
    ph_hdrmaxaddress  = cast(char *)ph_hdrbaseaddress + r.size;
    ph_hdradjust = cast(char *)ph_hdrbaseaddress - cast(char *)ph_mmfiobase;
    //dbg_printf("base = %p, max = %p, adjust = x%x\n",
        //ph_hdrbaseaddress,ph_hdrmaxaddress,ph_hdradjust);

    // In order for this to work, the address ranges must not overlap
    if (dohydrate)
        assert(cast(char *)ph_mmfiobase + r.size < cast(char *)ph_hdrbaseaddress || cast(char *)ph_hdrmaxaddress <= cast(char *)ph_mmfiobase);
}
    ph_bufk = r.bufk;
}
}

/**************************
 * Convert from pointer to file offset.
 */

static if (H_STYLE & H_COMPLEX)
{
static if (1)
{
void *ph_dehydrate(void *pp)
{
    asm
    {
                naked                   ;
                mov     EAX,4[ESP]      ;
                mov     EAX,[EAX]       ;
                test    EAX,EAX         ;
                je      L13C            ;
                test    AL,1            ;
                jne     L13C            ;

                mov     EDX,ph_buf      ;
                mov     ECX,[EDX]       ;
                cmp     ECX,EAX         ;
                ja      L11D            ;
                add     ECX,PHBUFSIZE   ;
                cmp     ECX,EAX         ;
                ja      LFE             ;

L11D:           mov     ECX,4[EDX]      ;
                add     EDX,4           ;
                cmp     ECX,EAX         ;
                ja      L11D            ;
                add     ECX,PHBUFSIZE   ;
                cmp     ECX,EAX         ;
                jbe     L11D            ;

LFE:            sub     EDX,ph_buf      ;
                sub     EAX,ECX         ;
                mov     ECX,4[ESP]      ;
                shl     EDX,0xE-2       ;
                lea     EAX,PHBUFSIZE+1[EAX][EDX]               ;
                mov     [ECX],EAX       ;
L13C:           ret                     ;
    }
}
}
else
{
void *ph_dehydrate(void *pp)
{
    int i;
    char *p;

    assert((void *).sizeof == int.sizeof);
    p = *cast(char **)pp;
    /*dbg_printf("ph_dehydrate(%p)\n",p);*/
    /* Since pointers and file offsets are never odd, we
        can use bit 0 as a flag to indicate a 'dehydrated' pointer.
     */
    if (p && !isdehydrated(p))          /* null => 0L           */
    {
static if (TAGGED_ALLOC)
{
        assert((cast(Tag_t *)p)[-1] == TAG_PAGE);
}
version (DEBUG_XSYMGEN)
{
        if (xsym_gen && !ph_in_phbuf(p))
        {
            // We got a ptr that didn't come from the ph heap. Set it
            // to nil and return. This should only happen when dehydrating
            // blocklists that belong to blocks that were optimized away.
            // dehydrating a perm/temp pointer ?
            *cast(int *)pp = 0;
            return *cast(void **)pp;
        }
        // dehydrating a HEAD pointer ?
        assert(!(xsym_gen && ph_in_head(pp)));
}
        for (i = 0; 1; i++)
        {   char *pb = cast(char *) ph_buf[i];

            /*dbg_printf("ph_buf[%d] = %p\n",i,pb);*/
            debug assert(i < ph_bufi);
            {
                if (pb <= p && p < pb + PHBUFSIZE)
                {
                    *cast(int *)pp = (cast(int)i * PHBUFSIZE + (p - pb)) | 1;
static if (PH_METRICS)
{
                    hydrate_count++;
}
                    return *cast(void **)pp;
                }
            }
        }
    }
    return *cast(void **)pp;
}

}

/**************************
 * Convert from file offset to pointer.
 * Output:
 *      *pp     hydrated pointer
 * Returns:
 *      *pp
 */

static if (1)
{
void *ph_hydrate(void *pp)
{
    asm
    {
        naked                   ;
        mov     EAX,4[ESP]      ;
        mov     EAX,[EAX]       ;
        test    AL,1            ;
        je      L169            ;
        mov     ECX,EAX         ;
        and     EAX,0x3FFE      ;
        shr     ECX,0xE         ;
        mov     EDX,ph_buf      ;
        add     ECX,ph_bufk     ;
        add     EAX,[ECX*4][EDX]        ;
        mov     ECX,4[ESP]      ;
        mov     [ECX],EAX       ;
L169:   ret                     ;
    }
}
}
else
{
void *ph_hydrate(void *pp)
{
    uint off;
    uint i;

    assert(cast(void *).sizeof == uint.sizeof);
    off = *cast(int *)pp;
    /*assert(!off || (off & 1 && off > Config.sizeof));*/
    if (off & 1)                        /* if dehydrated        */
    {
        i = ph_bufk + cast(int)(off / PHBUFSIZE);
        debug assert(i < ph_bufi);
        *cast(void **)pp = cast(void *)(cast(char *)ph_buf[i] + (off & ((PHBUFSIZE - 1) & ~1)));
    }
    return *cast(void **)pp;
}
}

}

static if (PH_METRICS)
{
private void hydrate_message(char *s)
{
    if (hydrate_count) {
        dbg_printf("%s: %d pointers dehydrated", s, hydrate_count);
        ReportError(kWarning);
        hydrate_count = 0;
    }
}
}
else
{
private void hydrate_message(char *s) { }
}

/*************************************
 * A comdef has been generated. If we are generating a PH,
 * save the comdef away in a list, so that when the PH is read
 * back in, the comdef's can be generated.
 */

void ph_comdef(Symbol *s)
{
    if (config.flags2 & CFG2phgen &&
        cstate.CSfilblk)
    {   Sfile *sf;

        sf = *(cstate.CSfilblk.BLsrcpos).Sfilptr;
        sfile_debug(sf);
        list_prepend(&sf.SFcomdefs,s);
    }
}

/*************************************
 * Go through list of comdef's, and emit comdef records for them.
 */

private void ph_comdef_gen(symlist_t sl)
{
    for (; sl; sl = list_next(sl))
    {   Symbol *s = list_symbol(sl);

        if (s.Sclass == SCcomdef)
        {   s.Sclass = SCglobal;
            outcommon(s,type_size(s.Stype));
        }
    }
}

/*******************************
 * Read file i out of precompiled header r.
 * Input:
 *      r                       pointer to Root for header
 *      sfh                     pointer to Sfile in r's srcfiles.arr[]
 *      flag    FLAG_INPLACE    rehydrate in place (we just generated an auto ph file)
 *              FLAG_HX         rehydrate an HX file
 *              FLAG_SYM        rehydrate a .sym file
 */

private void ph_hydrate_h(Root *r,Sfile *sfh,int flag)
{   Sfile *sfc;

    //assert(sfh >= &r.srcfiles.arr[0] && sfh < &r.srcfiles.arr[r.srcfiles.idx]);
    sfile_debug(sfh);
    //dbg_printf("ph_hydrate_h('%s',sfh=%p,flag=%d)\n",sfh.SFname,sfh,flag);

    pstate.SThflag = cast(char)flag;
    switch (flag)
    {
        case FLAG_SYM:
        {   list_t li;

            if (sfh.SFflags & SFloaded)
                return;
            sfh.SFflags |= SFloaded;

            // First, recursively load all the dependent files
            for (li = sfh.SFfillist; li; li = list_next(li))
            {   Sfile *sfd = cast(Sfile *) list_ptr(li);

                ph_hydrate_h(r,sfd,flag);
            }

            if (dohydrate)
                list_hydrate(&sfh.SFcomdefs,cast(list_free_fp)&symbol_hydrate);
            ph_comdef_gen(sfh.SFcomdefs);
            list_free(&sfh.SFcomdefs,FPNULL);
            break;
        }

        case FLAG_INPLACE:
static if (H_STYLE & H_OFFSET)
{
            break;
}
        case FLAG_HX:
            pragma_hydrate_macdefs(&sfh.SFmacdefs,flag);
            symbol_symdefs_hydrate(&sfh.SFsymdefs,cast(Symbol **)&scope_find(SCTglobal).root,flag);
            if (!CPP)
                symbol_symdefs_hydrate(&sfh.SFtagsymdefs,cast(Symbol **)&scope_find(SCTglobaltag).root,flag);
            break;
        default:
            assert(0);
    }

    sfc = filename_search(sfh.SFname);
    sfile_debug(sfc);

  if (CPP)
  {
    if (dohydrate)
    {   list_hydrate(&r.cppoperfuncs,cast(list_free_fp)&symbol_hydrate);
        list_hydrate(&r.STincalias,FPNULL);
        list_hydrate(&r.STsysincalias,FPNULL);
        list_hydrate(&sfh.SFtemp_ft,FPNULL);
        list_hydrate(&sfh.SFtemp_class,FPNULL);
    }
    if (flag != FLAG_INPLACE)           // append to existing list
    {   symlist_t sl;

        list_cat(&template_ftlist,sfh.SFtemp_ft);
        list_cat(&pstate.STincalias,r.STincalias);
        list_cat(&pstate.STsysincalias,r.STsysincalias);

        memcpy(cpp_operfuncs_nspace.ptr, r.cppoperfuncs_nspace.ptr, (OPMAX + 7) / 8);

        if (!template_class_list_p)
            template_class_list_p = &template_class_list;
        for (sl = sfh.SFtemp_class; sl; sl = list_next(sl))
        {   Symbol *s = list_symbol(sl);

            symbol_debug(s);
            *template_class_list_p = s;
            template_class_list_p = &s.Stemplate.TMnext;
            *template_class_list_p = null;
        }
        for (sl = r.cppoperfuncs; sl; sl = list_next(sl))
        {   Symbol *s = list_symbol(sl);
            Symbol *so;
            int op;

            symbol_debug(s);
            op = s.Sfunc.Foper;
            for (so = cpp_operfuncs[op]; 1; so = so.Sfunc.Foversym)
            {   if (!so)
                {   cpp_operfuncs[op] = s;
                    break;
                }
                else if (so == s)       // if already in list
                    break;
            }
        }
    }

    sfc.SFtemp_ft = sfh.SFtemp_ft;
    sfc.SFtemp_class = sfh.SFtemp_class;
  }
  else
    sfc.SFtagsymdefs = sfh.SFtagsymdefs;

    sfc.SFfillist = sfh.SFfillist;
    sfc.SFmacdefs = sfh.SFmacdefs;
    sfc.SFsymdefs = sfh.SFsymdefs;
    sfc.SFcomdefs = sfh.SFcomdefs;
    filename_mergefl(sfc);
}

/*************************************
 * Test and see if we should do ph_autowrite().
 */

void ph_testautowrite()
{   blklst *bf;

    //dbg_printf("ph_testautowrite()\n");
    bf = cstate.CSfilblk;
    if (bf && !bf.BLprev && bl.BLtyp != BLrtext)
    {   pstate.STflags |= PFLhxdone;
        if (pstate.STflags & PFLhxgen)
            ph_autowrite();
    }
}

/*************************************
 * Write out current symbol table, but continue compiling.
 * Used for -HX automatic precompiled headers.
 */

void ph_autowrite()
{
    char *p;
    FILE *fp;

version (__GNUC__) { } else
{
    //dbg_printf("ph_autowrite()\n");
    pstate.STflags &= ~PFLhxgen;
    pstate.STflags |= PFLhxdone | PFLhxwrote;
    if (errcnt)                         // if syntax errors in headers
        return;

    //if (pstate.STflags & PFLextdef)
    //    synerr(EM_data_in_pch);       // data in precompiled header

    p = filespecaddpath(ph_directory,ph_hxfilename.ptr);
    if (configv.verbose == 2)
        dbg_printf(" write '%s'\n",p);

    if (pstate.STflags & PFLhxread)     // if we read in an HX file
    {   int i;

        // Hydrate all files not read in yet
        ph_setadjust(hx_r);
        filename_merge(&hx_r.srcfiles);        // build translation table
        for (i = 0; i < hx_r.srcfiles.idx; i++)
        {   Sfile *sf;
            Sfile *sfhx;

            sfhx = hx_r.srcfiles.pfiles[i];
            sf = filename_search(sfhx.SFname);
            if (sf.SFflags & SFhx)
            {   sf.SFflags &= ~SFhx;   // loaded now
                //dbg_printf("pre-hydrate: ");
                if (dohydrate)
                    ph_hydrate_h(hx_r,sfhx,FLAG_INPLACE);
            }
        }
    }

static if (0)
{
{ int i,j;
  list_t fl;
  for (i = 0; i < srcfiles.idx; i++)
  {
        printf("source %d '%s'\n",i,srcfiles_name(i));
        for (fl = sfile(i).fillist; fl; fl = list_next(fl))
            printf("\tdep %p\n",list_ptr(fl));
  }
}
}

static if (MEMORYHX)
{
}
else
{
static if (MMFIO)
{
        if (pstate.STflags & PFLhxread)
        {
            /*  We already have the HX file mapped into memory, and we
                can't write a new one while it is. Thus, we copy the heap
                into a temp, unmap the HX file, then copy the temp back
                onto the heap (at the same address so we don't disturb
                any pointers).
             */
            char *temp;
            int i;

            // Copy heap to a temp buffer
            temp = cast(char *) globalrealloc(null,ph_bufi * PHBUFSIZE);
            if (!temp)
                err_nomem();
            for (i = 0; i < ph_bufi; i++)
                memcpy(temp + i * PHBUFSIZE,ph_buf[i],PHBUFSIZE);

            // Unmap existing HX file
            vmem_unmapfile();
            vmem_decommit(ph_resaddress,ph_resaddress_size);
            ph_release(ph_resaddress,ph_resaddress_size);

            // Create a new heap at the old HX base address
            ph_baseaddress = ph_reserve(ph_mmfiobase,ph_reservesize);
            if (!ph_baseaddress)
                err_fatal(EM_cant_reserve_mem,ph_mmfiobase);    // can't reserve memory
            ph_resaddress = ph_baseaddress;
            ph_resaddress_size = ph_reservesize;
            if (!vmem_commit(ph_baseaddress,ph_bufi * PHBUFSIZE))
                err_nomem();

            // Copy from the temp buffer back into the heap
            for (i = 0; i < ph_bufi; i++)
                memcpy(ph_buf[i],temp + i * PHBUFSIZE,PHBUFSIZE);

            globalrealloc(temp,0);      // free temp buffer
        }
}
}
        ph_write(p,FLAG_HX);
        ph_setadjust(root);     // so the pointers will re-hydrate properly
        if (dohydrate)
            filename_hydrate(&root.srcfiles);
        filename_merge(&root.srcfiles);
        {   int i;

            for (i = 0; i < root.srcfiles.idx; i++)
                ph_hydrate_h(root,root.srcfiles.pfiles[i],FLAG_INPLACE);
        }
        mem_free(p);

    //dbg_printf("ph_autowrite() done\n");
}
}

/*************************************
 * Dehydrate, then write precompiled header.
 * Input:
 *      filename                name of precompiled header file
 *      flag    FLAG_HX:        write HX file
 *              FLAG_SYM:       write sym file
 */

void ph_write(const(char)* filename,int flag)
{
    int i;

    if (!errcnt)
    {
        //if (pstate.STflags & PFLextdef)
        //    synerr(EM_data_in_pch);   // data in precompiled header
        root.config = config;
        root.srcfiles = srcfiles;
        root.STincalias = pstate.STincalias;
        root.STsysincalias = pstate.STincalias;
        root.STsequence = pstate.STsequence;
static if (H_STYLE & (H_NONE | H_BIT0 | H_OFFSET))
{
        root.size = ph_bufi * PHBUFSIZE;
}
        if (CPP)
        {
        for (i = 0; i < root.srcfiles.idx; i++)
        {   Sfile *sf;
            Symbol *s;

            sf = root.srcfiles.pfiles[i];
            //dbg_printf("file '%s'\n",sf.SFname);
            for (s = sf.SFsymdefs; s; s = s.Snext)
                // If operator overload function, place in cpp_operfuncs
                if (tyfunc(s.Stype.Tty) && s.Sfunc.Fflags & Foperator)
                {
static if (0)
{
                    //if (s.Sscope) printf("%s\n", s.Sscope.Sident);
                    assert(!s.Sscope); // can't be a member of a class or namespace
}
else
{
                    /*  This will trip the assert:
                     *  namespace Foo
                     *  {   class X { };
                     *
                     *      template <class T>
                     *          inline int operator==(T t, int x) { return 0; }
                     *  }
                     *  void foo()
                     *  {   Foo::X x;
                     *      x == 3;
                     *  }
                     */
}
                    list_prepend(&root.cppoperfuncs,s);
                }
        }
static if (DEHYDRATE)
{
        list_dehydrate(&root.cppoperfuncs,cast(list_free_fp)&symbol_dehydrate);
}
        memcpy(root.cppoperfuncs_nspace.ptr, cpp_operfuncs_nspace.ptr, (OPMAX + 7) / 8);
        }
        if (flag == FLAG_SYM)
        {   root.mactabroot = pragma_dehydrate();
            root.symtab = cast(Symbol *)scope_find(SCTglobal).root;
            if (!CPP)
                root.tagsymtab = cast(Symbol *)scope_find(SCTglobaltag).root;
static if (DEHYDRATE)
{
            ph_dehydrate(&root.mactabroot);
            symbol_tree_dehydrate(&root.symtab);
            if (!CPP)
                symbol_tree_dehydrate(&root.tagsymtab);
}
        }
static if (DEHYDRATE)
{
        for (i = 0; i < root.srcfiles.idx; i++)
        {   Sfile *sf;

            sf = root.srcfiles.pfiles[i];
            //dbg_printf("dehydrating file '%s'\n",sf.SFname);
            if (!(config.flags2 & CFG2phgen))
            {   pragma_dehydrate_macdefs(&sf.SFmacdefs);
                symbol_symdefs_dehydrate(&sf.SFsymdefs);
                if (!CPP)
                    symbol_symdefs_dehydrate(&sf.SFtagsymdefs);
            }
            list_dehydrate(&sf.SFcomdefs,cast(list_free_fp)&symbol_dehydrate);
            if (CPP)
            {   list_dehydrate(&sf.SFtemp_ft,FPNULL);
                list_dehydrate(&sf.SFtemp_class,FPNULL);
            }
        }
        filename_dehydrate(&root.srcfiles);
        list_dehydrate(&root.STincalias,FPNULL);
        list_dehydrate(&root.STsysincalias,FPNULL);
}

static if (MEMORYHX)
{
        if (flag == FLAG_HX || config.flags4 & CFG4cacheph)
        {   void *p;

            hxblock_debug(ph_hx);
            p = globalrealloc(ph_hx.pdata,ph_bufi * PHBUFSIZE);
            if (!p)
                err_nomem();
            //dbg_printf("globalrealloc(%lx) => %p\n",ph_bufi * PHBUFSIZE,p);
            ph_hx.pdata = p;
            ph_hx.nbytes = ph_bufi * PHBUFSIZE;
            ph_hx.mtime = time(null);
            assert(strlen(filename) < ph_hx.name.sizeof);
            strcpy(cast(char*)ph_hx.name,cast(char*)filename);
            //dbg_printf("ph_hx = %p, ph_hx.name = '%s'\n",ph_hx,ph_hx.name);
static if (LINEARALLOC)
{
            memcpy(cast(char *)ph_hx.pdata,ph_buf[0],ph_bufi * PHBUFSIZE);
}
else
{
            for (i = 0; i < ph_bufi; i++)
                memcpy(cast(char *)ph_hx.pdata + i * PHBUFSIZE,ph_buf[i],PHBUFSIZE);
}
        }
}
        if (MEMORYHX && flag == FLAG_SYM)                   // if writing sym file
        {   FILE *fp;

            fp = file_openwrite(filename,"wb");
            if (!fp)
                return;

static if (LINEARALLOC)
{
            if (fwrite(ph_buf[0],ph_bufi * PHBUFSIZE,1,fp) < 1 ||
                ferror(fp) ||
                fclose(fp))
            {   fclose(fp);
                err_fatal(EM_write_error,filename);     // error writing output file
            }
}
else
{
            for (i = 0; i < ph_bufi; i++)
            {
                if (fwrite(ph_buf[i],PHBUFSIZE,1,fp) < 1 || ferror(fp))
                {   fclose(fp);
                    goto Lerr;          // error writing output file
                }
            }
            if (fclose(fp))
            {
             Lerr:
                err_fatal(EM_write_error,filename);     // error writing output file
            }
}
        }
    }
}


/*************************************
 * Read a precompiled header.
 * Input:
 *      filename                file to read in
 *      flag    FLAG_HX         read HX file
 *              FLAG_SYM        read .SYM file
 * Returns:
 *      null    error
 *      Root*   root in ph
 */

private Root * ph_readfile(char *filename,int flag)
{   char *buf;
    int fd;
    int nbufs;
    size_t nbytes;
    int fsize;
    Root *r;
    int page;
    int memoryhx = 0;
    char *p;
    char *newfilename = null;
    char *pdata = null;
    void *pview = null;

    //dbg_printf("ph_readfile('%s',%d)\n",filename,flag);
    //dbg_printf("ph_hx = %p, ph_hx.pdata = %p, ph_hx.name = '%s'\n",ph_hx,ph_hx.pdata,ph_hx.name);

version (__GNUC__) { } else
{
static if (MEMORYHX)
{
    assert(ph_hx);
    hxblock_debug(ph_hx);
    p = ph_hx.name.ptr;
    if (*p == '.' && (p[1] == '\\' || p[1] == '/'))
        p += 2;
    if (*filename == '.' && (filename[1] == '\\' || filename[1] == '/'))
        filename += 2;
    if (filename_cmp(filename,p) == 0)
    {
        fsize = ph_hx.nbytes;
        if (!fsize)
        {   if (flag == FLAG_HX)
                return null;
        }
        else
        {
            if (configv.verbose == 2)
                dbg_printf(" Reading from memory\n");
        Ldata:
            memoryhx = 1;
            pdata = cast(char *)ph_hx.pdata;
            nbytes = 0;
            if (configv.verbose == 2)
                dbg_printf(" '%s'\n",filename);
            if (flag == FLAG_SYM)               // if reading .SYM file
            {
                if (configv.verbose)
                    dmcdll_SpawnFile(filename,0);
                if (!pview)
                    pview = ph_reserve(ph_mmfiobase,fsize);
                if (!pview)
                    err_fatal(EM_cant_reserve_mem,ph_mmfiobase);        // can't reserve memory
                if (!vmem_commit(pview,fsize))
                    err_nomem();
                assert(!ph_resaddress2);
                ph_resaddress2 = pview;
                ph_resaddress2_size = fsize;
            }
        }
    }
    else if (flag == FLAG_HX)
        return null;
}

    if (!memoryhx)
    {
        //dbg_printf("Reading from file\n");
        fsize = file_size(filename);
        //dbg_printf("fsize = %ld, Root.sizeof = %d\n",fsize,Root.sizeof);
        if (fsize < PHBUFSIZE || fsize & (PHBUFSIZE - 1)) // file doesn't exist or is wrong
            goto ret_null;

        newfilename = dmcdll_TranslateFileName(filename,cast(char*)"rb");
        if (!newfilename)
            err_exit();

static if (MMFIO)
{
        if (flag == FLAG_HX)            // if reading HX file
        {
static if (MEMORYHX)
{
            if (ph_hx.reserve)
            {   vmem_release(ph_hx.reserve,ph_hx.reservesize);
                ph_hx.reserve = null;
            }
}
            pview = vmem_mapfile(newfilename,ph_mmfiobase,fsize,1);
            dmcdll_DisposeFile(newfilename);
            newfilename = null;
            if (!pview)
                err_fatal(EM_cant_map_file,filename,ph_mmfiobase);      // can't map file
            ph_baseaddress = pview;
            auto p2 = vmem_reserve(cast(char *)pview + fsize,ph_reservesize - fsize);
            if (!p2)
                err_fatal(EM_cant_reserve_mem,cast(char *)pview + fsize);   // can't reserve memory
            ph_resaddress = p2;
            ph_resaddress_size = ph_reservesize - fsize;
            ph_bufi = 0;
        }
        else if (flag == FLAG_SYM)
        {
static if (MEMORYHX)
{
            if (config.flags4 & CFG4cacheph)
            {
                // Read file into persistent memory
                int n;

                ph_hx.pdata = globalrealloc(ph_hx.pdata,0);
                pview = ph_reserve(ph_mmfiobase,fsize);
                void* p2 = globalrealloc(ph_hx.pdata,fsize);
                if (!p2)
                    err_nomem();
                ph_hx.pdata = p2;
                ph_hx.nbytes = fsize;
                ph_hx.mtime = 0;                       // don't need this

                fd = sopen(newfilename,_O_RDONLY | _O_BINARY,SH_DENYWR);
                dmcdll_DisposeFile(newfilename);
                newfilename = null;
                if (fd == -1)                   // couldn't open file
                    goto ret_null;

                if (configv.verbose == 2)
                    dbg_printf(" Reading from file\n");
                n = read(fd,ph_hx.pdata,fsize);
                assert(n > 0);
                close(fd);

                assert(strlen(filename) < ph_hx.name.sizeof);
                strcpy(ph_hx.name.ptr,filename);
                hxblock_debug(ph_hx);
                goto Ldata;
            }
            else
            {
                if (ph_hx.reserve && ph_hx.reserve == ph_mmfiobase)
                {   vmem_release(ph_hx.reserve,ph_hx.reservesize);
                    ph_hx.reserve = null;
                }
                pview = vmem_mapfile(newfilename,ph_mmfiobase,fsize,1);
                dmcdll_DisposeFile(newfilename);
                newfilename = null;
                if (!pview)
                    err_fatal(EM_cant_map_file,filename,ph_mmfiobase);  // can't map file
            }
}
else
{
            {
                pview = vmem_mapfile(newfilename,ph_mmfiobase,fsize,1);
                dmcdll_DisposeFile(newfilename);
                newfilename = null;
                if (!pview)
                    err_fatal(EM_cant_map_file,filename,ph_mmfiobase);  // can't map file
            }
}
        }
        else
        {
            fd = sopen(newfilename,_O_RDONLY | _O_BINARY,SH_DENYWR);
            dmcdll_DisposeFile(newfilename);
            newfilename = null;
            if (fd == -1)                       // couldn't open file
                goto ret_null;
        }
}
else
{
        {
            fd = sopen(newfilename,_O_RDONLY | _O_BINARY,SH_DENYWR);
            dmcdll_DisposeFile(newfilename);
            newfilename = null;
            if (fd == -1)                       // couldn't open file
                goto ret_null;
        }
}
        if (configv.verbose == 2)
        {
            dbg_printf(" '%s'\n",filename);
        }
        if (flag == FLAG_SYM && configv.verbose)                // if not HX file
            dmcdll_SpawnFile(filename,0);
    }

    // Read file into a sequence of PHBUFSIZE buffers

    ph_bufk = ph_bufi;
    assert(!(fsize & (PHBUFSIZE - 1)));
    nbufs = (fsize + (PHBUFSIZE - 1)) / PHBUFSIZE;
    if (!ph_reservebuf(nbufs))
        err_nomem();

    //dbg_printf("size of precompiled header %lu\n",(int)nbufs * PHBUFSIZE);

    for (page = 0; page < nbufs; page++)
    {
        if (pview)
            buf = cast(char *)ph_newbuffer(cast(char *)pview + page * PHBUFSIZE);
        else
            buf = cast(char *)ph_newbuffer(null);
        if (!buf)
            err_nomem();                /* out of memory        */
static if (MEMORYHX)
{
        if (pdata)
        {
            memcpy(buf,pdata + nbytes,PHBUFSIZE);
            nbytes += PHBUFSIZE;
        }
        else
        if (!pview)
        {
            //file_progress();
            nbytes = read(fd,buf,PHBUFSIZE);
            assert(nbytes == PHBUFSIZE);
            if (controlc_saw)
            {   close(fd);
                util_exit(EXIT_BREAK);
            }
        }
}
else
{
        if (!pview)
        {
            //file_progress();
            nbytes = read(fd,buf,PHBUFSIZE);
            assert(nbytes == PHBUFSIZE);
            if (controlc_saw)
            {   close(fd);
                util_exit(EXIT_BREAK);
            }
        }
}
        if (page == 0)
        {
            // The +__PGOFF is for the __PGOFF bytes of overhead for a page_malloc()
            r = cast(Root *)(cast(char *)ph_buf[ph_bufk] + PAGEOVERHEAD + __PGOFF);
            r.bufk = ph_bufk;

            // Pick up flags from precompiled header
            config.flags4 |= r.config.flags4 & CFGY4;

            // Ignore irrelevant flags
            r.config.flags  = (r.config.flags  & ~CFGX ) | (config.flags  & CFGX );
            r.config.flags2 = (r.config.flags2 & ~CFGX2) | (config.flags2 & CFGX2);
            r.config.flags3 = (r.config.flags3 & ~CFGX3) | (config.flags3 & CFGX3);
            r.config.flags4 = (r.config.flags4 & ~CFGX4) | (config.flags4 & CFGX4);

            // if configuration doesn't match
            if (memcmp(&r.config,&config,Config.sizeof))
                break;          // don't bother reading in rest of file
        }
    }

    if (!memoryhx)
    {
        if (!pview)
            close(fd);
        if (configv.verbose && filename_cmp(filename,ph_hxfilename.ptr))
            dmcdll_SpawnFile(filename,-1);
    }
Lret:
    //dbg_printf("ph_readfile() done\n");
    return r;

ret_null:
    if (newfilename)
        dmcdll_DisposeFile(newfilename);
    r = null;
    goto Lret;
}
}

/*************************************
 * HX automatic precompiled headers.
 * Determine if we can use existing HX precompiled header,
 * or if we must generate it.
 */

void ph_auto()
{
    int status;
    char *p;
    int i;
    Root *r;
    list_t fl;
    int mtime;

    //printf("ph_auto()\n");
version (__GNUC__) { } else
{
static if (MEMORYHX)
{
    // Reserve a big fat address space (20 Mb)
    ph_baseaddress = ph_reserve(ph_mmfiobase,ph_reservesize);
    if (!ph_baseaddress)
        err_fatal(EM_cant_reserve_mem,ph_mmfiobase);    // can't reserve memory
    ph_resaddress = ph_baseaddress;
    ph_resaddress_size = ph_reservesize;
}
else static if (LINEARALLOC)
{
    // Reserve a big fat address space (20 Mb)
    ph_baseaddress = vmem_reserve(null,ph_reservesize);
    if (!ph_baseaddress)
        err_nomem();
    ph_resaddress = ph_baseaddress;
    ph_resaddress_size = ph_reservesize;
}

    {   uint dirlen;

        // Combine directory and filename for HX filename.
        // Can't use filespecaddpath() because the heap isn't inited yet.
        p = ph_hxfilename.ptr;
        if (ph_directory && (dirlen = strlen(ph_directory)) != 0)
        {   p = cast(char *)parc_malloc(dirlen + 1 + ph_hxfilename.sizeof);
            strcpy(p,ph_directory);
            if (p[dirlen - 1] != '/' && p[dirlen - 1] != '\\')
                strcat(p,"\\");
            strcat(p,ph_hxfilename.ptr);
        }
        //p = filespecaddpath(ph_directory,ph_hxfilename);
    }

static if (MEMORYHX)
{
    mtime = ph_hx.mtime;
}
else
    {
        mtime = os_file_mtime(p);
        if (mtime == -1L)
        {   ph_bufk = ph_bufi;
            goto Lgenfile;
        }
    }

    r = ph_readfile(p,FLAG_HX); // read in HX file
    if (!r)
        goto Lgenfile;          // error reading file

    // If compile configuration is different
static if (H_STYLE & H_NONE)
{
    if (memcmp(&r.config,&config,Config.sizeof)
        || ph_mmfiobase != r.baseaddress
       )
        goto Lgenfile;
}
else
{
    if (memcmp(&r.config,&config,Config.sizeof))
        goto Lgenfile;
}
    ph_setadjust(r);

    // Test the date on each #include'd file in the HX precompiled header.
    // If any are newer than the HX precompiled header, we must generate
    // a new HX precompiled header.
    if (dohydrate)
        filename_hydrate(&r.srcfiles);
    for (i = 0; i < r.srcfiles.idx; i++)
    {
        Sfile *sfhx;

        sfhx = r.srcfiles.pfiles[i];
        sfile_debug(sfhx);
        if (sfhx.SFflags & SFtop)
        {
            // If source file is different...
            if (config.flags2 & CFG2phautoy &&
                filename_cmp(finname,sfhx.SFname))
                goto Lgenfile;

            continue;
        }

        int ftime = os_file_mtime(sfhx.SFname);
        if (ftime == -1 || mtime < ftime)
            goto Lgenfile;
    }

    // Use file
    //dbg_printf("Using HX file\n");

    filename_merge(&r.srcfiles);
    for (i = 0; i < r.srcfiles.idx; i++)
    {   Sfile *sf;

        sf = r.srcfiles.pfiles[i];
        assert(sf);
        sfile_debug(sf);
        sf.SFflags |= SFhx;    // not loaded yet
    }
    pstate.STflags |= PFLhxread;
    hx_r = r;
    hx_r.bufk = ph_bufk;
    root = r;

    goto Lret;

Lgenfile:
    // Generate HX file

static if (MMFIO && !MEMORYHX)
{
    vmem_decommit(ph_resaddress,ph_resaddress_size);
    ph_release(ph_resaddress,ph_resaddress_size);
    vmem_unmapfile();           // unmap existing HX file
    //vmem_setfilesize(0);      // set file to 0 length
    remove(p);                  // and delete it

    ph_baseaddress = ph_reserve(ph_mmfiobase,ph_reservesize);
    if (!ph_baseaddress)
        err_fatal(EM_cant_reserve_mem,ph_mmfiobase);    // can't reserve memory
    ph_resaddress = ph_baseaddress;
    ph_bufi = 0;
}
else
{
    // Clear out memory allocated by precompiled header
    for (i = ph_bufk; i < ph_bufi; i++)
        ph_initbuffer(ph_buf[i]);
}

    /* Put configuration at start of heaps      */
    root = cast(Root *) ph_calloc(Root.sizeof);
static if (MMFIO)
{
    root.baseaddress = ph_baseaddress;
}

    pstate.STflags |= PFLhxgen;

Lret:
    if (p != ph_hxfilename.ptr)
        parc_free(p);
}
}

/*************************************
 * Read precompiled header file (filename) from HX ph.
 */

private void ph_load(Sfile *sfhx)
{   list_t li;

    sfile_debug(sfhx);

    // First, recursively load all the dependent files
    for (li = sfhx.SFfillist; li; li = list_next(li))
    {   Sfile *sf;
        Sfile *sfd = cast(Sfile *) list_ptr(li);

        sfile_debug(sfd);
        sf = filename_search(sfd.SFname);
        //printf("filename_search('%s') = %p\n",sfd.SFname,sf);
        assert(sf);
        if (sf.SFflags & SFhx)
        {   sf.SFflags &= ~SFhx;
            ph_load(sfd);
        }
    }

    //dbg_printf("ph_load('%s')\n",sfhx.SFname);
    ph_hydrate_h(hx_r,sfhx,FLAG_HX);
}

/************************************
 * Look in HX file for header.
 * It must be in there.
 * Merge contents into symbol table.
 */

int ph_autoread(char *filename)
{   int i;
    int j;

    debug dbg_printf("ph_autoread('%s')\n",filename);
    assert(pstate.STflags & PFLhxread);
    assert(hx_r);
    ph_setadjust(hx_r);
    for (i = 1; i < hx_r.srcfiles.idx; i++)
    {   Sfile *sf;
        Sfile *sfhx;

        sfhx = hx_r.srcfiles.pfiles[i];
        sfile_debug(sfhx);
        if (filename_cmp(sfhx.SFname,filename) == 0)
        {
            sf = filename_search(sfhx.SFname);
            assert(sf);
            sfile_debug(sf);
            if (sf.SFflags & SFhx)             // if not already loaded
            {
                sf.SFflags &= ~SFhx;           // mark as loaded
                filename_merge(&hx_r.srcfiles);        // build translation table
                ph_load(sfhx);
            }
            break;
        }
    }
    assert(1 != hx_r.srcfiles.idx);
    return 1;
}


/*************************************
 * Look for precompiled header matching .h file.
 * Read it and merge it in if found.
 * Not used for reading HX files.
 * Input:
 *      filename        name of .h file
 * Returns:
 *      0       found ph file and successfully merged it in
 *      !=0     no ph file, or error
 */

int ph_read(char *filename)
{
    Root *r;

    //dbg_printf("ph_read('%s')\n",filename);

    if (pstate.STflags & PFLphread)
        return 1;                       // can only read in one PH file
    pstate.STflags |= PFLphread;

    // Find file
    if (fphreadname)
    {
        r = ph_readfile(fphreadname,FLAG_SYM);
        if (fdep)
            fprintf(fdep, "%s ", fphreadname);
    }
    else
    {
        char *pt;
        char *p;

        pt = filespecforceext(filespecname(filename),cast(char*)ext_sym);
        p = filespecaddpath(ph_directory,pt);
        mem_free(pt);
        r = ph_readfile(p,FLAG_SYM);
        if (!r)
        {
            mem_free(p);
            pt = filespecforceext(filespecname(filename),".pch");
            p = filespecaddpath(ph_directory,pt);
            mem_free(pt);
            r = ph_readfile(p,FLAG_SYM);
        }
        if (fdep)
            fprintf(fdep, "%s ", p);
        mem_free(p);
    }

    if (!r)
        return 1;

    if (pstate.STflags & (PFLmacdef | PFLsymdef | PFLinclude))
    {   //tx86err((pstate.STflags & PFLinclude) ? EM_pch_first : EM_before_pch);
        warerr((pstate.STflags & PFLinclude) ? WM.WM_pch_first : WM.WM_before_pch);
                                        // precompiled header must be first
                                        // symbols or macros already defined
        return 1;
    }

    if (memcmp(&r.config,&config,Config.sizeof))
    {
        if (r.config.language != config.language)
        {
            err_fatal(EM_wrong_lang);   // precompiled header compiled with C
        }
        warerr(WM.WM_pch_config);          // configuration doesn't match
        ph_initbuffer(ph_buf[ph_bufk]);
        return 1;
    }
    else
    {
static if (H_STYLE & H_NONE)
{
    if (ph_mmfiobase != r.baseaddress)
    {   warerr(WM_pch_config);          // configuration doesn't match
        return 1;
    }
}
    }
{   clock_t t0,t1;

    //t0 = clock();
    ph_setadjust(r);
    if (dohydrate)
        filename_hydrate(&r.srcfiles);
    filename_merge(&r.srcfiles);
    if (dohydrate)
    {   ph_hydrate(&r.mactabroot);
        symbol_tree_hydrate(&r.symtab);
    }
    pragma_hydrate(cast(macro_t **)r.mactabroot);
    scope_find(SCTglobal).root = r.symtab;
    pstate.STsequence = r.STsequence;
    if (!CPP)
    {
        if (dohydrate)
            symbol_tree_hydrate(&r.tagsymtab);
        scope_find(SCTglobaltag).root = r.tagsymtab;
    }
    ph_hydrate_h(r,r.srcfiles.pfiles[0],FLAG_SYM);
    //printf("hydrate time = %ld\n",clock() - t0);
}
    //dbg_printf("ph_read('%s') done\n",filename);
    return 0;
}


/**********************************************
 * Do our own storage allocator, a replacement
 * for malloc/free. This one uses our page package.
 */

void *ph_malloc(size_t nbytes)
{
    int i;

version (__GNUC__)
{
    void *p = malloc(nbytes);
    return p;
}
    ph_check();
    if (nbytes >= ph_maxsize) dbg_printf("nbytes = %u, ph_maxsize = %u\n",nbytes,ph_maxsize);
static if (TAGGED_ALLOC)
{
    nbytes += TAG_SIZE;
}
    assert(nbytes < ph_maxsize);
    do
    {
//      for (i = ph_bufi; --i >= 0;)
version (__GNUC__) { } else
{
        for (i = 0; i < ph_bufi; i++)
        {   void *buf;
            uint u,mf;


            buf = ph_buf[i];
static if (1)
{
            u = cast(uint)page_malloc(buf,nbytes);
            if (u)
            {
                debug assert(u > PAGEOVERHEAD && u + nbytes <= PHBUFSIZE);
                ph_check();
                /*dbg_printf("ph_malloc(x%x) = %p\n",nbytes,page_toptr(buf,u));*/
                return page_toptr(buf,u);
            }
            //mf = page_maxfree(buf);
            //printf("%02d nbytes = %d, mf = %d\n",i,nbytes,mf);
}
else
{
            mf = page_maxfree(buf);
            assert(mf <= PHBUFSIZE - PAGEOVERHEAD - __PGOFF);
            if (nbytes <= mf)
            {   u = page_malloc(buf,nbytes);
                assert(u && u > PAGEOVERHEAD && u + nbytes <= PHBUFSIZE);
                ph_check();
                /*dbg_printf("ph_malloc(x%x) = %p\n",nbytes,page_toptr(buf,u));*/
                return page_toptr(buf,u);
            }
}
        }
}
    } while (ph_newbuffer(null));
    return null;
}

void *ph_calloc(size_t nbytes)
{   void *p;
version (__GNUC__)
{
    p = calloc(nbytes,1);
    return p;
}
    p = ph_malloc(nbytes);
    if (p)
        memset(p,0,nbytes);
    ph_check();
    return p;
}

void ph_free(void *p)
{
    int i;
version (__GNUC__)
{
    if (!p)
        return;
    free(p);
    return ;
}

    //dbg_printf("ph_free(%p), size = %x\n",p,((short *)p)[-1]);
    ph_check();
    if (!p)
        return;
    // Assume that we'll have a tendency to free in reverse order
    // that we alloc'd.
    for (i = ph_bufi; --i >= 0;)
    {
        void *buf;

        buf = ph_buf[i];

        if (
            buf <= p && p < cast(void *)(cast(char *)buf + PHBUFSIZE))
        {
version (__GNUC__) { } else                   // finish later
{
            /*dbg_printf("ph_buf[%d] = %p, page_free(%p (offset = x%x))\n",
                i,buf,p,cast(char *)p - cast(char *)buf);*/
            i = page_free(buf,cast(char *)p - cast(char *)buf);
            assert(i == 0);
            ph_check();
            return;
}
        }
    }
    assert(0);
}

void * ph_realloc(void *p,size_t nbytes)
{   void *newp;
    uint i;

    /*dbg_printf("ph_realloc(%p,%d)\n",p,nbytes);*/
    if (!p)
        return ph_malloc(nbytes);
    if (!nbytes)
    {   ph_free(p);
        return null;
    }
version (__GNUC__)
{
    // NOTE: THIS IS TEMPORARY
    //       CAN'T DECREASE SIZE
    newp = ph_malloc(nbytes);
    if (newp)
        memcpy(newp,p,nbytes);
}
else
{
    for (i = 0; 1; i++)
    {
        void *buf;

        assert(i < ph_bufi);
        buf = ph_buf[i];
        if (
            buf <= p && p < cast(void *)(cast(char *)buf + PHBUFSIZE))
        {   uint offset = cast(char *)p - cast(char *)buf;
            uint oldsize;

            oldsize = page_size(buf,offset);
            if (nbytes <= oldsize || nbytes < page_maxfree(buf))
            {
                offset = page_realloc(buf,offset,nbytes);
                assert(offset);
                newp = page_toptr(buf,offset);
            }
            else
            {
                /*dbg_printf("reallocing across pages\n");*/
                newp = ph_malloc(nbytes);
                if (newp)
                {
                    memcpy(newp,p,oldsize);
                    page_free(buf,offset);
                }
            }
            break;
        }
    }
}
    return newp;
}

/************************************************
 * Add definition to list of global declarations for this file.
 */

void ph_add_global_symdef(Symbol *s, uint sctype)
{
    if (sctype & (SCTglobal | SCTglobaltag))
    {
        if (config.flags2 & (CFG2phautoy | CFG2phauto | CFG2phgen) &&   // and doing precompiled headers
            cstate.CSfilblk)                    // and there is a source file
        {   Sfile *sf;

            // Thread definition onto list of global declarations for this file
            sf = *(cstate.CSfilblk.BLsrcpos).Sfilptr;
            sfile_debug(sf);
            if (sctype & SCTglobaltag)
            {   s.Snext = sf.SFtagsymdefs;
                sf.SFtagsymdefs = s;
            }
            else
            {   s.Snext = sf.SFsymdefs;
                sf.SFsymdefs = s;
            }
        }
    }
}

}
