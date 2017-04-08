/**
 * Implementation of the
 * $(LINK2 http://www.digitalmars.com/download/freecompiler.html, Digital Mars C/C++ Compiler).
 *
 * Copyright:   Copyright (c) 1993-1998 by Symantec, All Rights Reserved
 *              Copyright (c) 2000-2017 by Digital Mars, All Rights Reserved
 * Authors:     $(LINK2 http://www.digitalmars.com, Walter Bright)
 * License:     Distributed under the Boost Software License, Version 1.0.
 *              http://www.boost.org/LICENSE_1_0.txt
 * Source:      https://github.com/DigitalMars/Compiler/blob/master/dm/src/dmc/pseudo.c
 */

// Handle pseudo register variable stuff

#if !SPP

#include        <stdio.h>
#include        <time.h>
#include        <string.h>
#include        "cc.h"
#include        "type.h"
#include        "global.h"
#include        "code.h"
#include        "scope.h"

// Macro trick to generate several parallel tables

#define Y \
        X("AH",4,mAX,TYuchar)   \
        X("AL",0,mAX,TYuchar)   \
        X("AX",8,mAX,TYushort)  \
        X("BH",7,mBX,TYuchar)   \
        X("BL",3,mBX,TYuchar)   \
        X("BP",13,0,TYushort)   \
        X("BX",11,mBX,TYushort) \
        X("CH",5,mCX,TYuchar)   \
        X("CL",1,mCX,TYuchar)   \
        X("CX",9,mCX,TYushort)  \
        X("DH",6,mDX,TYuchar)   \
        X("DI",15,mDI,TYushort) \
        X("DL",2,mDX,TYuchar)   \
        X("DX",10,mDX,TYushort) \
        X("EAX",16,mAX,TYulong) \
        X("EBP",21,0,TYulong)   \
        X("EBX",19,mBX,TYulong) \
        X("ECX",17,mCX,TYulong) \
        X("EDI",23,mDI,TYulong) \
        X("EDX",18,mDX,TYulong) \
        X("ESI",22,mSI,TYulong) \
        X("ESP",20,0,TYulong)   \
        X("SI",14,mSI,TYushort) \
        X("SP",12,0,TYushort)

// Table for identifiers
static const char *pseudotab[] =
{
#define X(id,reg,m,ty)  id,
        Y
#undef X
};

// Register number to use in addressing mode
unsigned char pseudoreg[] =
{
#define X(id,reg,m,ty)  reg,
        Y
#undef X
};

// Mask to use for registers affected
regm_t pseudomask[] =
{
#define X(id,reg,m,ty)  m,
        Y
#undef X
};

// Table for type of pseudo register variable
static unsigned char pseudoty[] =
{
#define X(id,reg,m,ty)  mTYvolatile | ty,
        Y
#undef X
};

//////////////////////////////////////
// Given an undefined symbol s, see if it is in fact a pseudo
// register variable. If it is, fill in the symbol.
// Returns:
//      NULL    not pseudo register variable
//      symbol created for pseudo register variable

symbol *pseudo_declar(char *id)
{   symbol *s = NULL;

    if (id[0] == '_')
    {   int i;

        i = binary(id + 1,pseudotab,arraysize(pseudotab));
        if (i >= 0)
        {   tym_t ty;

            ty = pseudoty[i];
            // Can't use extended registers for 16 bit compilations
            if (!I16 || !tylong(ty))
            {
                s = scope_define(id,SCTlocal,SCpseudo);
                s->Sreglsw = i;
                s->Stype = type_alloc(ty);
                s->Stype->Tcount++;
                symbol_add(s);
            }
        }
    }
    return s;
}

#endif // !SPP
