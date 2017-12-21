/**
 * Implementation of the
 * $(LINK2 http://www.digitalmars.com/download/freecompiler.html, Digital Mars C/C++ Compiler).
 *
 * Copyright:   Copyright (c) 1993-1998 by Symantec, All Rights Reserved
 *              Copyright (c) 2000-2017 by Digital Mars, All Rights Reserved
 * Authors:     $(LINK2 http://www.digitalmars.com, Walter Bright)
 * License:     Distributed under the Boost Software License, Version 1.0.
 *              http://www.boost.org/LICENSE_1_0.txt
 * Source:      https://github.com/DigitalMars/Compiler/blob/master/dm/src/dmc/pseudo.d
 */

// Handle pseudo register variable stuff

version (SPP)
{
}
else
{

import core.stdc.stdio;
import core.stdc.string;

import dmd.backend.cc;
import dmd.backend.cdef;
import dmd.backend.code_x86;
import dmd.backend.global;
import dmd.backend.ty;
import dmd.backend.type;

import scopeh;

extern (C++):

/* 4 parallel tables using "X Macro" technique
 */

template Y(alias X)
{
    enum Y =
    [
        //  id   reg  mask   ty
        X!("AH",   4, mAX, TYuchar),
        X!("AL",   0, mAX, TYuchar),
        X!("AX",   8, mAX, TYushort),
        X!("BH",   7, mBX, TYuchar),
        X!("BL",   3, mBX, TYuchar),
        X!("BP",  13,   0, TYushort),
        X!("BX",  11, mBX, TYushort),
        X!("CH",   5, mCX, TYuchar),
        X!("CL",   1, mCX, TYuchar),
        X!("CX",   9, mCX, TYushort),
        X!("DH",   6, mDX, TYuchar),
        X!("DI",  15, mDI, TYushort),
        X!("DL",   2, mDX, TYuchar),
        X!("DX",  10, mDX, TYushort),
        X!("EAX", 16, mAX, TYulong),
        X!("EBP", 21,   0, TYulong),
        X!("EBX", 19, mBX, TYulong),
        X!("ECX", 17, mCX, TYulong),
        X!("EDI", 23, mDI, TYulong),
        X!("EDX", 18, mDX, TYulong),
        X!("ESI", 22, mSI, TYulong),
        X!("ESP", 20,   0, TYulong),
        X!("SI",  14, mSI, TYushort),
        X!("SP",  12,   0, TYushort),
    ];
}

// Table for identifiers

template Xtab(alias A, alias B, alias C, alias D) { enum Xtab = A; }

private __gshared const(char)*[24] pseudotab = Y!Xtab;


// Register number to use in addressing mode

template Xreg(alias A, alias B, alias C, alias D) { enum Xreg = B; }

__gshared ubyte[24] pseudoreg = Y!Xreg;


// Mask to use for registers affected

template Xmask(alias A, alias B, alias C, alias D) { enum Xmask = C; }

__gshared regm_t[24] pseudomask = Y!Xmask;


// Table for type of pseudo register variable

template Xty(alias A, alias B, alias C, alias D) { enum Xty = mTYvolatile | D; }

private __gshared const(tym_t)[24] pseudoty = Y!Xty;


//////////////////////////////////////
// Given an undefined symbol s, see if it is in fact a pseudo
// register variable. If it is, fill in the symbol.
// Returns:
//      null    not pseudo register variable
//      symbol created for pseudo register variable

Symbol *pseudo_declar(char *id)
{   Symbol *s = null;

    if (id[0] == '_')
    {
        int i = binary(id + 1,&pseudotab[0],cast(int)pseudotab.length);
        if (i >= 0)
        {
            tym_t ty = pseudoty[i];
            // Can't use extended registers for 16 bit compilations
            if (!I16 || !tylong(ty))
            {
                s = scope_define(id,SCTlocal,SCpseudo);
                s.Sreglsw = cast(ubyte)i;
                s.Stype = type_alloc(ty);
                s.Stype.Tcount++;
                symbol_add(s);
            }
        }
    }
    return s;
}

}
