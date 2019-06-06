/**
 * Compiler implementation of the
 * $(LINK2 http://www.digitalmars.com, C/C++ programming language).
 *
 * Copyright:   Copyright (C) 1985-1998 by Symantec
 *              Copyright (c) 2000-2017 by Digital Mars, All Rights Reserved
 * Authors:     $(LINK2 http://www.digitalmars.com, Walter Bright)
 * License:     Distributed under the Boost Software License, Version 1.0.
 *              http://www.boost.org/LICENSE_1_0.txt
 * Source:      https://github.com/dlang/dmd/blob/master/src/dmd/backend/_precomp.d
 */

// Precompiled header configuration

module precomp;

extern (C++):
nothrow:

version (_WINDLL)
    enum MEMORYHX = 1;     // HX and SYM files are cached in memory
else
    enum MEMORYHX = 0;

version (Windows)
    enum MMFIO = 1;        // if memory mapped files
else version (Posix)
    enum MMFIO = 1;
else
    static assert(0);

version (Windows)
    enum LINEARALLOC = 1;  // can reserve address ranges
else
    enum LINEARALLOC = 0;  // can not reserve address ranges

// H_STYLE takes on one of these precompiled header methods
enum
{
    H_NONE    = 1,       // no hydration/dehydration necessary
    H_BIT0    = 2,       // bit 0 of the pointer determines if pointer
                         // is dehydrated, an offset is added to
                         // hydrate it
    H_OFFSET  = 4,       // the address range of the pointer determines
                         // if pointer is dehydrated, and an offset is
                         // added to hydrate it. No dehydration necessary.
    H_COMPLEX = 8,       // dehydrated pointers have bit 0 set, hydrated
                         // pointers are in non-contiguous buffers
}

// Determine hydration style
//      NT console:     H_NONE
//      NT DLL:         H_OFFSET
//      DOSX:           H_COMPLEX
version (MARS)
{
    enum H_STYLE = H_NONE;         // DMD doesn't use precompiled headers
}
else
{
    static if (MMFIO)
    {
        version (_WINDLL)
            enum H_STYLE = H_OFFSET;
        else
            enum H_STYLE = H_OFFSET; //H_NONE
    }
    else static if (LINEARALLOC)
        enum H_STYLE = H_BIT0;
    else
        enum H_STYLE = H_COMPLEX;
}

// Do we need hydration code
enum HYDRATE = H_STYLE & (H_BIT0 | H_OFFSET | H_COMPLEX);

// Do we need dehydration code
enum DEHYDRATE = H_STYLE & (H_BIT0 | H_COMPLEX);


static if (H_STYLE & H_OFFSET)
{
    bool dohydrate() { return ph_hdradjust != 0; }
    bool isdehydrated(void* p) { return ph_hdrbaseaddress <= p && p < ph_hdrmaxaddress; }
    void *ph_hydrate(void** pp)
    {
        if (isdehydrated(*cast(void **)pp))
            *cast(void **)pp -= ph_hdradjust;
        return *cast(void **)pp;
    }
    void *ph_dehydrate(void *pp) { return *cast(void **)pp; }
    extern __gshared
    {
        void *ph_hdrbaseaddress;
        void *ph_hdrmaxaddress;
        int   ph_hdradjust;
    }
}
else static if (H_STYLE & H_BIT0)
{
    enum dohydrate = 1;
    bool isdehydrated(void *p) { return cast(int)p & 1; }
    extern __gshared int ph_hdradjust;
    void *ph_hydrate(void *pp)
    {
        if (isdehydrated(*cast(void **)pp))
            *cast(void **)pp -= ph_hdradjust;
        return *cast(void **)pp;
    }
    void *ph_dehydrate(void *pp)
    {
        if (*cast(int *)pp)
            *cast(int *)pp |= 1;
        return *cast(void **)pp;
    }
}
else static if (H_STYLE & H_COMPLEX)
{
    enum dohydrate = 1;
    bool isdehydrated(void *p) { return cast(int)p & 1; }
    void *ph_hydrate(void *pp);
    void *ph_dehydrate(void *pp);
}
else static if (H_STYLE & H_NONE)
{
    enum dohydrate = 0;
    bool isdehydrated(void *p)   { return false; }
    void *ph_hydrate(void *pp)   { return *cast(void **)pp; }
    void *ph_dehydrate(void *pp) { return *cast(void **)pp; }
}
else
{
    pragma(msg, "H_STYLE is ", H_STYLE, " which is wrong");
    static assert(0);
}


