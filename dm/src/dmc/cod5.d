/**
 * Compiler implementation of the
 * $(LINK2 http://www.dlang.org, D programming language).
 *
 * Copyright:   Copyright (C) 1995-1998 by Symantec
 *              Copyright (C) 2000-2018 by The D Language Foundation, All Rights Reserved
 * Authors:     $(LINK2 http://www.digitalmars.com, Walter Bright)
 * License:     $(LINK2 http://www.boost.org/LICENSE_1_0.txt, Boost License 1.0)
 * Source:      $(LINK2 https://github.com/dlang/dmd/blob/master/src/dmd/backend/cod5.d, backend/cod5.d)
 */
module dmd.backend.cod5;

version (SCPP)
    version = COMPILE;
version (MARS)
    version = COMPILE;

version (COMPILE)
{

import core.stdc.stdio;
import core.stdc.string;
import core.stdc.time;
import dmd.backend.cc;
import dmd.backend.el;
import dmd.backend.oper;
import dmd.backend.code;
import dmd.backend.global;
import dmd.backend.type;

import dmd.backend.cdef;
import dmd.backend.dlist;
import dmd.backend.ty;

extern(C++):

private void pe_add(block *b);
private int need_prolog(block *b);

/********************************************************
 * Determine which blocks get the function prolog and epilog
 * attached to them.
 */

void cod5_prol_epi()
{
static if(1)
{
    cod5_noprol();
}
else
{
    tym_t tym;
    tym_t tyf;
    block *b;
    block *bp;
    int nepis;

    tyf = funcsym_p.ty();
    tym = tybasic(tyf);

    if (!(config.flags4 & CFG4optimized) ||
        anyiasm ||
        Alloca.size ||
        usednteh ||
        calledFinally ||
        tyf & (mTYnaked | mTYloadds) ||
        tym == TYifunc ||
        tym == TYmfunc ||       // can't yet handle ECX passed as parameter
        tym == TYjfunc ||       // can't yet handle EAX passed as parameter
        config.flags & (CFGalwaysframe | CFGtrace) ||
//      config.fulltypes ||
        (config.wflags & WFwindows && tyfarfunc(tym)) ||
        need_prolog(startblock)
       )
    {   // First block gets the prolog, all return blocks
        // get the epilog.
        //printf("not even a candidate\n");
        cod5_noprol();
        return;
    }

    // Turn on BFLoutsideprolog for all blocks outside the ones needing the prolog.

    for (b = startblock; b; b = b.Bnext)
        b.Bflags &= ~BFLoutsideprolog;                 // start with them all off

    pe_add(startblock);

    // Look for only one block (bp) that will hold the prolog
    bp = null;
    nepis = 0;
    for (b = startblock; b; b = b.Bnext)
    {   int mark;

        if (b.Bflags & BFLoutsideprolog)
            continue;

        // If all predecessors are marked
        mark = 0;
        assert(b.Bpred);
        foreach (bl; ListRange(b.Bpred))
        {
            if (list_block(bl).Bflags & BFLoutsideprolog)
            {
                if (mark == 2)
                    goto L1;
                mark = 1;
            }
            else
            {
                if (mark == 1)
                    goto L1;
                mark = 2;
            }
        }
        if (mark == 1)
        {
            if (bp)             // if already have one
                goto L1;
            bp = b;
        }

        // See if b is an epilog
        mark = 0;
        foreach (bl; ListRange(b.Bsucc))
        {
            if (list_block(bl).Bflags & BFLoutsideprolog)
            {
                if (mark == 2)
                    goto L1;
                mark = 1;
            }
            else
            {
                if (mark == 1)
                    goto L1;
                mark = 2;
            }
        }
        if (mark == 1 || b.BC == BCret || b.BC == BCretexp)
        {   b.Bflags |= BFLepilog;
            nepis++;
            if (nepis > 1 && config.flags4 & CFG4space)
                goto L1;
        }
    }
    if (bp)
    {   bp.Bflags |= BFLprolog;
        //printf("=============== prolog opt\n");
    }
}
}

/**********************************************
 * No prolog/epilog optimization.
 */

void cod5_noprol()
{
    block *b;

    //printf("no prolog optimization\n");
    startblock.Bflags |= BFLprolog;
    for (b = startblock; b; b = b.Bnext)
    {
        b.Bflags &= ~BFLoutsideprolog;
        switch (b.BC)
        {   case BCret:
            case BCretexp:
                b.Bflags |= BFLepilog;
                break;
            default:
                b.Bflags &= ~BFLepilog;
        }
    }
}

/*********************************************
 * Add block b, and its successors, to those blocks outside those requiring
 * the function prolog.
 */

private void pe_add(block *b)
{
    if (b.Bflags & BFLoutsideprolog ||
        need_prolog(b))
        return;

    b.Bflags |= BFLoutsideprolog;
    foreach (bl; ListRange(b.Bsucc))
        pe_add(list_block(bl));
}

/**********************************************
 * Determine if block needs the function prolog to be set up.
 */

private int need_prolog(block *b)
{
    if (b.Bregcon.used & fregsaved)
        goto Lneed;

    // If block referenced a param in 16 bit code
    if (!I32 && b.Bflags & BFLrefparam)
        goto Lneed;

    // If block referenced a stack local
    if (b.Bflags & BFLreflocal)
        goto Lneed;

    return 0;

Lneed:
    return 1;
}

}
