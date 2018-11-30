/**
 * Compiler implementation of the
 * $(LINK2 http://www.dlang.org, D programming language).
 *
 * Copyright:   Copyright (C) 1985-1998 by Symantec
 *              Copyright (C) 2000-2018 by The D Language Foundation, All Rights Reserved
 * Authors:     $(LINK2 http://www.digitalmars.com, Walter Bright)
 * License:     $(LINK2 http://www.boost.org/LICENSE_1_0.txt, Boost License 1.0)
 * Source:      $(LINK2 https://github.com/dlang/dmd/blob/master/src/dmd/backend/gflow.d, backend/gflow.d)
 * Coverage:    https://codecov.io/gh/dlang/dmd/src/master/src/dmd/backend/gflow.d
 */

module dmd.backend.gflow;

version (SCPP)
    version = COMPILE;
version (MARS)
    version = COMPILE;

version (COMPILE)
{

import core.stdc.stdio;
import core.stdc.stdlib;
import core.stdc.string;

import dmd.backend.cc;
import dmd.backend.cdef;
import dmd.backend.code_x86;
import dmd.backend.oper;
import dmd.backend.global;
import dmd.backend.goh;
import dmd.backend.el;
import dmd.backend.outbuf;
import dmd.backend.ty;
import dmd.backend.type;

import dmd.backend.barray;
import dmd.backend.dlist;
import dmd.backend.dvec;

void vec_setclear(size_t b, vec_t vs, vec_t vc) { vec_setbit(b, vs); vec_clearbit(b, vc); }

bool Eunambig(elem* e) { return OTassign(e.Eoper) && e.EV.E1.Eoper == OPvar; }

char symbol_isintab(Symbol *s) { return sytab[s.Sclass] & SCSS; }

void util_free(void* p) { if (p) free(p); }
void *util_calloc(uint n, uint size) { void* p = calloc(n, size); assert(!(n * size) || p); return p; }
void *util_realloc(void* p, uint n, uint size) { void* q = realloc(p, n * size); assert(!(n * size) || q); return q; }

extern (C++):


/* Since many routines are nearly identical, we can combine them with   */
/* this flag:                                                           */

enum
{
    AE = 1,
    CP,
    VBE
}


private __gshared
{
    int flowxx;              // one of the above values

    vec_t ambigsym = null;
}


/***************** REACHING DEFINITIONS *********************/

/************************************
 * Compute reaching definitions (RDs).
 * That is, for each block B and each program variable X
 * find all elems that could be the last elem that defines
 * X along some path to B.
 * Binrd = the set of defs reaching the beginning of B.
 * Boutrd = the set of defs reaching the end of B.
 * Bkillrd = set of defs that are killed by some def in B.
 * Bgenrd = set of defs in B that reach the end of B.
 */

void flowrd()
{
    rdgenkill();            /* Compute Bgen and Bkill for RDs       */
    if (go.defnod.length == 0)     /* if no definition elems               */
        return;             /* no analysis to be done               */

    /* The transfer equation is:                                    */
    /*      Bin = union of Bouts of all predecessors of B.          */
    /*      Bout = (Bin - Bkill) | Bgen                             */
    /* Using Ullman's algorithm:                                    */

    foreach (b; dfo[])
        vec_copy(b.Boutrd, b.Bgen);

    bool anychng;
    vec_t tmp = vec_calloc(go.defnod.length);
    do
    {
        anychng = false;
        foreach (b; dfo[])    // for each block
        {
            /* Binrd = union of Boutrds of all predecessors of b */
            vec_clear(b.Binrd);
            if (b.BC != BCcatch /*&& b.BC != BCjcatch*/)
            {
                /* Set Binrd to 0 to account for:
                 * i = 0;
                 * try { i = 1; throw; } catch () { x = i; }
                 */
                foreach (bp; ListRange(b.Bpred))
                    vec_orass(b.Binrd,list_block(bp).Boutrd);
            }
            /* Bout = (Bin - Bkill) | Bgen */
            vec_sub(tmp,b.Binrd,b.Bkill);
            vec_orass(tmp,b.Bgen);
            if (!anychng)
                anychng = !vec_equal(tmp,b.Boutrd);
            vec_copy(b.Boutrd,tmp);
        }
    } while (anychng);              /* while any changes to Boutrd  */
    vec_free(tmp);

    static if (0)
    {
        dbg_printf("Reaching definitions\n");
        foreach (i, b; dfo[])    // for each block
        {
            assert(vec_numbits(b.Binrd) == go.defnod.length);
            dbg_printf("B%d Bin ", cast(int)i); vec_println(b.Binrd);
            dbg_printf("  Bgen "); vec_println(b.Bgen);
            dbg_printf(" Bkill "); vec_println(b.Bkill);
            dbg_printf("  Bout "); vec_println(b.Boutrd);
        }
    }
}

/***************************
 * Compute Bgen and Bkill for RDs.
 */

private void rdgenkill()
{
    /* Compute number of definition elems. */
    uint num_unambig_def = 0;
    uint deftop = 0;
    foreach (b; dfo[])    // for each block
        if (b.Belem)
        {
            deftop += numdefelems(b.Belem, &num_unambig_def);
        }

    /* Allocate array of pointers to all definition elems   */
    /*      The elems are in dfo order.                     */
    /*      go.defnod[]s consist of a elem pointer and a pointer */
    /*      to the enclosing block.                         */
    go.defnod.setLength(deftop);
    if (deftop == 0)
        return;

    /* Allocate buffer for the DNunambig vectors
     */
    const size_t dim = (deftop + (VECBITS - 1)) >> VECSHIFT;
    const sz = (dim + 2) * num_unambig_def;
    go.dnunambig.setLength(sz);
    go.dnunambig[] = 0;

    go.defnod.setLength(0);
    foreach (b; dfo[])    // for each block
        if (b.Belem)
            asgdefelems(b, b.Belem);    // fill in go.defnod[]
    assert(go.defnod.length == deftop);

    initDNunambigVectors();

    foreach (b; dfo[])    // for each block
    {
        /* dump any existing vectors */
        vec_free(b.Bgen);
        vec_free(b.Bkill);
        vec_free(b.Binrd);
        vec_free(b.Boutrd);

        /* calculate and create new vectors */
        rdelem(&(b.Bgen),&(b.Bkill),b.Belem);
        if (b.BC == BCasm)
        {
            vec_clear(b.Bkill);        // KILL nothing
            vec_set(b.Bgen);           // GEN everything
        }
        b.Binrd = vec_calloc(deftop);
        b.Boutrd = vec_calloc(deftop);
    }
}

/**********************
 * Compute and return # of definition elems in e.
 */

private uint numdefelems(elem *e, uint *pnum_unambig_def)
{
    uint n = 0;
    while (1)
    {
        assert(e);
        if (OTdef(e.Eoper))
        {
            ++n;
            if (OTassign(e.Eoper) && e.EV.E1.Eoper == OPvar)
                ++*pnum_unambig_def;
        }
        if (OTbinary(e.Eoper))
        {
            n += numdefelems(e.EV.E1, pnum_unambig_def);
            e = e.EV.E2;
        }
        else if (OTunary(e.Eoper))
        {
            e = e.EV.E1;
        }
        else
            break;
    }
    return n;
}

/**************************
 * Load go.defnod[] array.
 * Loaded in order of execution of the elems. Not sure if this is
 * necessary.
 */

private void asgdefelems(block *b,elem *n)
{
    assert(b && n);
    const uint op = n.Eoper;
    if (ERTOL(n))
    {
        asgdefelems(b,n.EV.E2);
        asgdefelems(b,n.EV.E1);
    }
    else if (OTbinary(op))
    {
        asgdefelems(b,n.EV.E1);
        asgdefelems(b,n.EV.E2);
    }
    else if (OTunary(op))
        asgdefelems(b,n.EV.E1);
    if (OTdef(op))
    {
        n.Edef = cast(uint)go.defnod.length;
        go.defnod.push(DefNode(n, b, null));
    }
    else
        n.Edef = ~0;       // just to ensure it is not in the array
}

/*************************************
 * Allocate and initialize DNumambig vectors in go.defnod[]
 */

private void initDNunambigVectors()
{
    //printf("initDNunambigVectors()\n");
    const size_t numbits = go.defnod.length;
    const size_t dim = (numbits + (VECBITS - 1)) >> VECSHIFT;

    uint j = 0;
    foreach (const i; 0 .. go.defnod.length)
    {
        elem *e = go.defnod[i].DNelem;
        if (OTassign(e.Eoper) && e.EV.E1.Eoper == OPvar)
        {
            vec_t v = &go.dnunambig[j] + 2;
            assert(vec_dim(v) == 0);
            vec_dim(v) = dim;
            vec_numbits(v) = numbits;
            j += dim + 2;
            fillInDNunambig(v, e);
            go.defnod[i].DNunambig = v;
        }
    }
    assert(j <= go.dnunambig.length);
}

/*************************************
 * Allocate and compute rd GEN and KILL.
 */

private void rdelem(vec_t *pgen,vec_t *pkill,   /* where to put result          */
        elem *n)                                /* tree to evaluate for GEN and KILL */
{
    *pgen = vec_calloc(go.defnod.length);
    *pkill = vec_calloc(go.defnod.length);
    if (n)
        accumrd(*pgen,*pkill,n);
}

/**************************************
 * Accumulate GEN and KILL vectors for this elem.
 */

private void accumrd(vec_t GEN,vec_t KILL,elem *n)
{
    assert(GEN && KILL && n);
    const uint op = n.Eoper;
    if (OTunary(op))
        accumrd(GEN,KILL,n.EV.E1);
    else if (OTbinary(op))
    {
        if (op == OPcolon || op == OPcolon2)
        {
            vec_t Gl,Kl,Gr,Kr;
            rdelem(&Gl,&Kl,n.EV.E1);
            rdelem(&Gr,&Kr,n.EV.E2);

            switch (el_returns(n.EV.E1) * 2 | int(el_returns(n.EV.E2)))
            {
                case 3: // E1 and E2 return
                    /* GEN = (GEN - Kl) | Gl |
                     *       (GEN - Kr) | Gr
                     * KILL |= Kl & Kr
                     * This simplifies to:
                     * GEN = GEN | (Gl | Gr) | (GEN - (Kl & Kr)
                     * KILL |= Kl & Kr
                     */
                    vec_andass(Kl,Kr);
                    vec_orass(KILL,Kl);

                    vec_orass(Gl,Gr);
                    vec_sub(Gr,GEN,Kl);  // (GEN - (Kl & Kr)
                    vec_or(GEN,Gl,Gr);
                    break;

                case 2: // E1 returns
                    /* GEN = (GEN - Kl) | Gl
                     * KILL |= Kl
                     */
                    vec_subass(GEN,Kl);
                    vec_orass(GEN,Gl);
                    vec_orass(KILL,Kl);
                    break;

                case 1: // E2 returns
                    /* GEN = (GEN - Kr) | Gr
                     * KILL |= Kr
                     */
                    vec_subass(GEN,Kr);
                    vec_orass(GEN,Gr);
                    vec_orass(KILL,Kr);
                    break;

                case 0: // neither returns
                    break;

                default:
                    assert(0);
            }

            vec_free(Gl);
            vec_free(Kl);
            vec_free(Gr);
            vec_free(Kr);
        }
        else if (op == OPandand || op == OPoror)
        {
            vec_t Gr,Kr;
            accumrd(GEN,KILL,n.EV.E1);
            rdelem(&Gr,&Kr,n.EV.E2);
            if (el_returns(n.EV.E2))
                vec_orass(GEN,Gr);      // GEN |= Gr

            vec_free(Gr);
            vec_free(Kr);
        }
        else if (OTrtol(op) && ERTOL(n))
        {
            accumrd(GEN,KILL,n.EV.E2);
            accumrd(GEN,KILL,n.EV.E1);
        }
        else
        {
            accumrd(GEN,KILL,n.EV.E1);
            accumrd(GEN,KILL,n.EV.E2);
        }
    }

    if (OTdef(op))                  /* if definition elem           */
        updaterd(n,GEN,KILL);
}

/******************** AVAILABLE EXPRESSIONS ***********************/

/************************************
 * Compute available expressions (AEs).
 * That is, expressions whose result is still current.
 * Bin = the set of AEs reaching the beginning of B.
 * Bout = the set of AEs reaching the end of B.
 */

void flowae()
{
    flowxx = AE;
    flowaecp();
}

/**************************** COPY PROPAGATION ************************/

/***************************************
 * Compute copy propagation info (CPs).
 * Very similar to AEs (the same code is used).
 * Using RDs for copy propagation is WRONG!
 * That is, set of copy statements still valid.
 * Bin = the set of CPs reaching the beginning of B.
 * Bout = the set of CPs reaching the end of B.
 */

void flowcp()
{
    flowxx = CP;
    flowaecp();
}

/*****************************************
 * Common flow analysis routines for Available Expressions and
 * Copy Propagation.
 * Input:
 *      flowxx
 */

private void flowaecp()
{
    aecpgenkill();          /* Compute Bgen and Bkill for AEs or CPs */
    if (go.exptop <= 1)        /* if no expressions                    */
        return;

    /* The transfer equation is:                    */
    /*      Bin = & Bout(all predecessors P of B)   */
    /*      Bout = (Bin - Bkill) | Bgen             */
    /* Using Ullman's algorithm:                    */

    vec_clear(startblock.Bin);
    vec_copy(startblock.Bout,startblock.Bgen); /* these never change */
    if (startblock.BC == BCiftrue)
        vec_copy(startblock.Bout2,startblock.Bgen2); // these never change

    /* For all blocks except startblock     */
    foreach (b; dfo[1 .. $])
    {
        vec_set(b.Bin);        /* Bin = all expressions        */

        /* Bout = (Bin - Bkill) | Bgen  */
        vec_sub(b.Bout,b.Bin,b.Bkill);
        vec_orass(b.Bout,b.Bgen);
        if (b.BC == BCiftrue)
        {
            vec_sub(b.Bout2,b.Bin,b.Bkill2);
            vec_orass(b.Bout2,b.Bgen2);
        }
    }

    vec_t tmp = vec_calloc(go.exptop);
    bool anychng;
    do
    {
        anychng = false;

        // For all blocks except startblock
        foreach (b; dfo[1 .. $])
        {
            // Bin = & of Bout of all predecessors
            // Bout = (Bin - Bkill) | Bgen

            bool first = true;
            foreach (bl; ListRange(b.Bpred))
            {
                block* bp = list_block(bl);
                if (bp.BC == BCiftrue && bp.nthSucc(0) != b)
                {
                    if (first)
                        vec_copy(b.Bin,bp.Bout2);
                    else
                        vec_andass(b.Bin,bp.Bout2);
                }
                else
                {
                    if (first)
                        vec_copy(b.Bin,bp.Bout);
                    else
                        vec_andass(b.Bin,bp.Bout);
                }
                first = false;
            }
            assert(!first);     // it must have had predecessors

            if (anychng)
            {
                vec_sub(b.Bout,b.Bin,b.Bkill);
                vec_orass(b.Bout,b.Bgen);
            }
            else
            {
                vec_sub(tmp,b.Bin,b.Bkill);
                vec_orass(tmp,b.Bgen);
                if (!vec_equal(tmp,b.Bout))
                {   // Swap Bout and tmp instead of
                    // copying tmp over Bout
                    vec_t v = tmp;
                    tmp = b.Bout;
                    b.Bout = v;
                    anychng = true;
                }
            }

            if (b.BC == BCiftrue)
            {   // Bout2 = (Bin - Bkill2) | Bgen2
                if (anychng)
                {
                    vec_sub(b.Bout2,b.Bin,b.Bkill2);
                    vec_orass(b.Bout2,b.Bgen2);
                }
                else
                {
                    vec_sub(tmp,b.Bin,b.Bkill2);
                    vec_orass(tmp,b.Bgen2);
                    if (!vec_equal(tmp,b.Bout2))
                    {   // Swap Bout and tmp instead of
                        // copying tmp over Bout2
                        vec_t v = tmp;
                        tmp = b.Bout2;
                        b.Bout2 = v;
                        anychng = true;
                    }
                }
            }
        }
    } while (anychng);
    vec_free(tmp);
}

/******************************
 * A variable to avoid parameter overhead to asgexpelems().
 */

private __gshared block *this_block;

/***********************************
 * Compute Bgen and Bkill for AEs, CPs, and VBEs.
 */

private void aecpgenkill()
{
    go.expnod.setLength(0);             // dump any existing one

    /* Compute number of expressions */
    go.exptop = 1;                     /* start at 1                   */
    foreach (b; dfo[])
        if (b.Belem)
        {
            if (flowxx == CP)
                go.exptop += numcpelems(b.Belem);
            else // AE || VBE
                numaeelems(b.Belem);
        }
    if (go.exptop <= 1)                /* if no expressions            */
        return;

    /* Allocate array of pointers to all expression elems.          */
    /* (The elems are in order. Also, these expressions must not    */
    /* have any side effects, and possibly should not be machine    */
    /* dependent primitive addressing modes.)                       */
    go.expnod.setLength(go.exptop);
    go.expnod[0] = null;

    go.expblk.setLength(flowxx == VBE ? go.exptop : 0);

    const uint exptopsave = go.exptop;
    go.exptop = 1;
    foreach (b; dfo[])
    {
        this_block = b;    /* so asgexpelems knows about this */
        if (b.Belem)
            asgexpelems(b.Belem);
    }
    assert(go.exptop == exptopsave);

    defstarkill();                  /* compute go.defkill and go.starkill */

    static if (0)
    {
        assert(vec_numbits(go.defkill) == go.exptop);
        assert(vec_numbits(go.starkill) == go.exptop);
        assert(vec_numbits(go.vptrkill) == go.exptop);
        dbg_printf("defkill  "); vec_println(go.defkill);
        if (go.starkill)
        {   dbg_printf("starkill "); vec_println(go.starkill);}
        if (go.vptrkill)
        {   dbg_printf("vptrkill "); vec_println(go.vptrkill); }
    }

    foreach (i, b; dfo[])
    {
        /* dump any existing vectors    */
        vec_free(b.Bin);
        vec_free(b.Bout);
        vec_free(b.Bgen);
        vec_free(b.Bkill);
        b.Bgen = vec_calloc(go.exptop);
        b.Bkill = vec_calloc(go.exptop);
        switch (b.BC)
        {
            case BCiftrue:
                vec_free(b.Bout2);
                vec_free(b.Bgen2);
                vec_free(b.Bkill2);
                elem* e;
                for (e = b.Belem; e.Eoper == OPcomma; e = e.EV.E2)
                    accumaecp(b.Bgen,b.Bkill,e);
                if (e.Eoper == OPandand || e.Eoper == OPoror)
                {
                    accumaecp(b.Bgen,b.Bkill,e.EV.E1);
                    vec_t Kr = vec_calloc(go.exptop);
                    vec_t Gr = vec_calloc(go.exptop);
                    accumaecp(Gr,Kr,e.EV.E2);

                    // We might or might not have executed E2
                    // KILL1 = KILL | Kr
                    // GEN1 = GEN & ((GEN - Kr) | Gr)

                    // We definitely executed E2
                    // KILL2 = (KILL - Gr) | Kr
                    // GEN2 = (GEN - Kr) | Gr

                    const uint dim = cast(uint)vec_dim(Kr);
                    vec_t KILL = b.Bkill;
                    vec_t GEN = b.Bgen;

                    foreach (j; 0 .. dim)
                    {
                        vec_base_t KILL1 = KILL[j] | Kr[j];
                        vec_base_t GEN1  = GEN[j] & ((GEN[j] & ~Kr[j]) | Gr[j]);

                        vec_base_t KILL2 = (KILL[j] & ~Gr[j]) | Kr[j];
                        vec_base_t GEN2  = (GEN[j] & ~Kr[j]) | Gr[j];

                        KILL[j] = KILL1;
                        GEN[j] = GEN1;
                        Kr[j] = KILL2;
                        Gr[j] = GEN2;
                    }

                    if (e.Eoper == OPandand)
                    {   b.Bkill  = Kr;
                        b.Bgen   = Gr;
                        b.Bkill2 = KILL;
                        b.Bgen2  = GEN;
                    }
                    else
                    {   b.Bkill  = KILL;
                        b.Bgen   = GEN;
                        b.Bkill2 = Kr;
                        b.Bgen2  = Gr;
                    }
                }
                else
                {
                    accumaecp(b.Bgen,b.Bkill,e);
                    b.Bgen2 = vec_clone(b.Bgen);
                    b.Bkill2 = vec_clone(b.Bkill);
                }
                b.Bout2 = vec_calloc(go.exptop);
                break;

            case BCasm:
                vec_set(b.Bkill);              // KILL everything
                vec_clear(b.Bgen);             // GEN nothing
                break;

            default:
                // calculate GEN & KILL vectors
                if (b.Belem)
                    accumaecp(b.Bgen,b.Bkill,b.Belem);
                break;
        }
        static if (0)
        {
            printf("block %d Bgen ",i); vec_println(b.Bgen);
            printf("       Bkill "); vec_println(b.Bkill);
        }
        b.Bin = vec_calloc(go.exptop);
        b.Bout = vec_calloc(go.exptop);
    }
}

/*****************************
 * Accumulate number of expressions in go.exptop.
 * Set NFLaecp as a flag indicating an AE elem.
 * Returns:
 *      true if this elem is a possible AE elem.
 */

private int numaeelems(elem *n)
{
    uint ae;

    assert(n);
    const uint op = n.Eoper;
    if (OTunary(op))
    {
        ae = numaeelems(n.EV.E1);
        // Disallow starred references to avoid problems with VBE's
        // being hoisted before tests of an invalid pointer.
        if (flowxx == VBE && op == OPind)
            goto L1;
    }
    else if (OTbinary(op))
        ae = numaeelems(n.EV.E1) & numaeelems(n.EV.E2);
    else
        ae = true;

    if (ae && OTae(op) && !(n.Ety & mTYvolatile) &&
        // Disallow struct AEs, because we can't handle CSEs that are structs
        tybasic(n.Ety) != TYstruct)
    {
        n.Nflags |= NFLaecp;           /* remember for asgexpelems()   */
        go.exptop++;
    }
    else
    L1:
        n.Nflags &= ~NFLaecp;
    return n.Nflags & NFLaecp;
}


/****************************
 * Compute number of cp (copy propagation) elems.
 * Mark cp elems by setting NFLaecp flag.
 * Returns:
 *      number of cp elems
 */

private int numcpelems(elem *n)
{
    while (1)
    {
        const op = n.Eoper;
        if (OTunary(op))
        {
            n.Nflags &= ~NFLaecp;
            n = n.EV.E1;
            continue;
        }
        else if (OTbinary(op))
        {
            /* look for elem of the form OPvar=OPvar, where they aren't the */
            /* same variable.                                               */
            if ((op == OPeq || op == OPstreq) &&
                n.EV.E1.Eoper == OPvar &&
                n.EV.E2.Eoper == OPvar &&
                !((n.EV.E1.Ety | n.EV.E2.Ety) & mTYvolatile) &&
                n.EV.E1.EV.Vsym != n.EV.E2.EV.Vsym)
            {
                n.Nflags |= NFLaecp;
                return numcpelems(n.EV.E1) +
                       numcpelems(n.EV.E2) +
                       1;

            }
            n.Nflags &= ~NFLaecp;
            int num = numcpelems(n.EV.E2);
            if (num)
                return num + numcpelems(n.EV.E1);
            n = n.EV.E1;
            continue;
        }
        else
        {
            n.Nflags &= ~NFLaecp;
            return 0;
        }
    }
}


/********************************
 * Assign ae (or cp) elems to go.expnod[] (in order of evaluation).
 */

private void asgexpelems(elem *n)
{
    assert(n);
    if (OTunary(n.Eoper))
        asgexpelems(n.EV.E1);
    else if (ERTOL(n))
    {
        asgexpelems(n.EV.E2);
        asgexpelems(n.EV.E1);
    }
    else if (OTbinary(n.Eoper))
    {
        asgexpelems(n.EV.E1);
        asgexpelems(n.EV.E2);
    }

    if (n.Nflags & NFLaecp)              /* if an ae, cp or vbe elem     */
    {
        n.Eexp = go.exptop;              /* remember index into go.expnod[] */
        go.expnod[go.exptop] = n;
        if (go.expblk.length)
            go.expblk[go.exptop] = this_block;
        go.exptop++;
    }
    else
        n.Eexp = 0;
}

/********************************
 * Compute defkill, starkill and vptrkill vectors.
 *      starkill:       set of expressions killed when a variable is
 *                      changed that somebody could be pointing to.
 *                      (not needed for cp)
 *                      starkill is a subset of defkill.
 *      defkill:        set of expressions killed by an ambiguous
 *                      definition.
 *      vptrkill:       set of expressions killed by an access to a vptr.
 */

private void defstarkill()
{
    vec_free(go.vptrkill);
    vec_free(go.defkill);
    vec_free(go.starkill);             /* dump any existing ones       */
    go.defkill = vec_calloc(go.exptop);
    if (flowxx != CP)
    {
        go.starkill = vec_calloc(go.exptop);      /* and create new ones  */
        go.vptrkill = vec_calloc(go.exptop);      /* and create new ones  */
    }
    else /* CP */
    {
        go.starkill = null;
        go.vptrkill = null;
    }

    if (!go.exptop)
        return;

    if (flowxx == CP)
    {
        foreach (uint i; 1 .. go.exptop)
        {
            elem *n = go.expnod[i];
            const uint op = n.Eoper;
            assert(op == OPeq || op == OPstreq);
            assert(n.EV.E1.Eoper==OPvar && n.EV.E2.Eoper==OPvar);

            // Set bit in defkill if either the left or the
            // right variable is killed by an ambiguous def.

            Symbol *s1 = n.EV.E1.EV.Vsym;
            if (!(s1.Sflags & SFLunambig) ||
                !(n.EV.E2.EV.Vsym.Sflags & SFLunambig))
            {
                vec_setbit(i,go.defkill);
            }
        }
    }
    else
    {
        foreach (uint i; 1 .. go.exptop)
        {
            elem *n = go.expnod[i];
            const uint op = n.Eoper;
            switch (op)
            {
                case OPvar:
                    if (!(n.EV.Vsym.Sflags & SFLunambig))
                        vec_setbit(i,go.defkill);
                    break;

                case OPind:         // if a 'starred' ref
                    static if (1)
                    {
                        /* The following program fails for this:
                        import core.stdc.stdio;

                        class Foo
                        {
                            string foo = "abc";
                            size_t i = 0;

                            void bar()
                            {
                                printf("%c\n", foo[i]);
                                i++;
                                printf("%c\n", foo[i]);
                            }
                        }

                        void main()
                        {
                            auto f = new Foo();
                            f.bar();
                        }
                        */

                        // For C/C++, casting to 'const' doesn't mean it
                        // actually is const,
                        // but immutable really doesn't change
                        if ((n.Ety & (mTYimmutable | mTYvolatile)) == mTYimmutable &&
                            n.EV.E1.Eoper == OPvar &&
                            n.EV.E1.EV.Vsym.Sflags & SFLunambig
                           )
                            break;
                    }
                    goto case OPstrlen;

                case OPstrlen:
                case OPstrcmp:
                case OPmemcmp:
                case OPbt:          // OPbt is like OPind
                    vec_setbit(i,go.defkill);
                    vec_setbit(i,go.starkill);
                    break;

                case OPvp_fp:
                case OPcvp_fp:
                    vec_setbit(i,go.vptrkill);
                    goto Lunary;

                default:
                    if (OTunary(op))
                    {
                    Lunary:
                        if (vec_testbit(n.EV.E1.Eexp,go.defkill))
                                vec_setbit(i,go.defkill);
                        if (vec_testbit(n.EV.E1.Eexp,go.starkill))
                                vec_setbit(i,go.starkill);
                    }
                    else if (OTbinary(op))
                    {
                        if (vec_testbit(n.EV.E1.Eexp,go.defkill) ||
                            vec_testbit(n.EV.E2.Eexp,go.defkill))
                                vec_setbit(i,go.defkill);
                        if (vec_testbit(n.EV.E1.Eexp,go.starkill) ||
                            vec_testbit(n.EV.E2.Eexp,go.starkill))
                                vec_setbit(i,go.starkill);
                    }
                    break;
            }
        }
    }
}

/********************************
 * Compute GEN and KILL vectors only for AEs.
 * defkill and starkill are assumed to be already set up correctly.
 * go.expnod[] is assumed to be set up correctly.
 */

void genkillae()
{
    flowxx = AE;
    assert(go.exptop > 1);
    foreach (b; dfo[])
    {
        assert(b);
        vec_clear(b.Bgen);
        vec_clear(b.Bkill);
        if (b.Belem)
            accumaecp(b.Bgen,b.Bkill,b.Belem);
        else if (b.BC == BCasm)
        {
            vec_set(b.Bkill);          // KILL everything
            vec_clear(b.Bgen);         // GEN nothing
        }
    }
}

/************************************
 * Allocate and compute KILL and GEN vectors for a elem.
 */

private void aecpelem(vec_t *pgen,vec_t *pkill, elem *n)
{
    *pgen = vec_calloc(go.exptop);
    *pkill = vec_calloc(go.exptop);
    if (n)
    {
        if (flowxx == VBE)
            accumvbe(*pgen,*pkill,n);
        else
            accumaecp(*pgen,*pkill,n);
    }
}

/*************************************
 * Accumulate GEN and KILL sets for AEs and CPs for this elem.
 */

private __gshared
{
    vec_t GEN;       // use static copies to save on parameter passing
    vec_t KILL;
}

private void accumaecp(vec_t g,vec_t k,elem *n)
{   vec_t GENsave,KILLsave;

    assert(g && k);
    GENsave = GEN;
    KILLsave = KILL;
    GEN = g;
    KILL = k;
    accumaecpx(n);
    GEN = GENsave;
    KILL = KILLsave;
}

private void accumaecpx(elem *n)
{
    elem *t;

    assert(n);
    elem_debug(n);
    const uint op = n.Eoper;

    switch (op)
    {
        case OPvar:
        case OPconst:
        case OPrelconst:
            if ((flowxx == AE) && n.Eexp)
            {   uint b;
                debug assert(go.expnod[n.Eexp] == n);
                b = n.Eexp;
                vec_setclear(b,GEN,KILL);
            }
            return;

        case OPcolon:
        case OPcolon2:
        {   vec_t Gl,Kl,Gr,Kr;

            aecpelem(&Gl,&Kl,n.EV.E1);
            aecpelem(&Gr,&Kr,n.EV.E2);

            /* KILL |= Kl | Kr           */
            /* GEN =((GEN - Kl) | Gl) &  */
            /*     ((GEN - Kr) | Gr)     */

            vec_orass(KILL,Kl);
            vec_orass(KILL,Kr);

            vec_sub(Kl,GEN,Kl);
            vec_sub(Kr,GEN,Kr);
            vec_orass(Kl,Gl);
            vec_orass(Kr,Gr);
            vec_and(GEN,Kl,Kr);

            vec_free(Gl);
            vec_free(Gr);
            vec_free(Kl);
            vec_free(Kr);
            break;
        }

        case OPandand:
        case OPoror:
        {   vec_t Gr,Kr;

            accumaecpx(n.EV.E1);
            aecpelem(&Gr,&Kr,n.EV.E2);

            if (el_returns(n.EV.E2))
            {
                // KILL |= Kr
                // GEN &= (GEN - Kr) | Gr

                vec_orass(KILL,Kr);
                vec_sub(Kr,GEN,Kr);
                vec_orass(Kr,Gr);
                vec_andass(GEN,Kr);
            }

            vec_free(Gr);
            vec_free(Kr);
            break;
        }

        case OPddtor:
        case OPasm:
            assert(!n.Eexp);                   // no ASM available expressions
            vec_set(KILL);                      // KILL everything
            vec_clear(GEN);                     // GEN nothing
            return;

        case OPeq:
        case OPstreq:
            accumaecpx(n.EV.E2);
            goto case OPnegass;

        case OPnegass:
            accumaecpx(n.EV.E1);
            t = n.EV.E1;
            break;

        case OPvp_fp:
        case OPcvp_fp:                          // if vptr access
            if ((flowxx == AE) && n.Eexp)
                vec_orass(KILL,go.vptrkill);       // kill all other vptr accesses
            break;

        case OPprefetch:
            accumaecpx(n.EV.E1);                  // don't check E2
            break;

        default:
            if (OTunary(op))
            {
        case OPind:                             // most common unary operator
                accumaecpx(n.EV.E1);
                debug assert(!OTassign(op));
            }
            else if (OTbinary(op))
            {
                if (OTrtol(op) && ERTOL(n))
                {
                    accumaecpx(n.EV.E2);
                    accumaecpx(n.EV.E1);
                }
                else
                {
                    accumaecpx(n.EV.E1);
                    accumaecpx(n.EV.E2);
                }
                if (OTassign(op))               // if assignment operator
                    t = n.EV.E1;
            }
            break;
    }


    /* Do copy propagation stuff first  */

    if (flowxx == CP)
    {
        if (!OTdef(op))                         /* if not def elem      */
            return;
        if (!Eunambig(n))                       /* if ambiguous def elem */
        {
            vec_orass(KILL,go.defkill);
            vec_subass(GEN,go.defkill);
        }
        else                                    /* unambiguous def elem */
        {
            assert(t.Eoper == OPvar);
            Symbol* s = t.EV.Vsym;                  // ptr to var being def'd
            foreach (uint i; 1 .. go.exptop)        // for each ae elem
            {
                elem *e = go.expnod[i];

                /* If it could be changed by the definition,     */
                /* set bit in KILL.                              */

                if (e.EV.E1.EV.Vsym == s || e.EV.E2.EV.Vsym == s)
                    vec_setclear(i,KILL,GEN);
            }
        }

        /* GEN CP elems */
        if (n.Eexp)
        {
            const uint b = n.Eexp;
            vec_setclear(b,GEN,KILL);
        }

        return;
    }

    /* Else Available Expression stuff  */

    if (n.Eexp)
    {
        const uint b = n.Eexp;             // add elem to GEN
        assert(go.expnod[b] == n);
        vec_setclear(b,GEN,KILL);
    }
    else if (OTdef(op))                         /* else if definition elem */
    {
        if (!Eunambig(n))                       /* if ambiguous def elem */
        {
            vec_orass(KILL,go.defkill);
            vec_subass(GEN,go.defkill);
            if (OTcalldef(op))
            {
                vec_orass(KILL,go.vptrkill);
                vec_subass(GEN,go.vptrkill);
            }
        }
        else                                    /* unambiguous def elem */
        {
            assert(t.Eoper == OPvar);
            Symbol* s = t.EV.Vsym;             // idx of var being def'd
            if (!(s.Sflags & SFLunambig))
            {
                vec_orass(KILL,go.starkill);       /* kill all 'starred' refs */
                vec_subass(GEN,go.starkill);
            }
            foreach (uint i; 1 .. go.exptop)        // for each ae elem
            {
                elem *e = go.expnod[i];
                const int eop = e.Eoper;

                /* If it could be changed by the definition,     */
                /* set bit in KILL.                              */
                if (eop == OPvar)
                {
                    if (e.EV.Vsym != s)
                        continue;
                }
                else if (OTunary(eop))
                {
                    if (!vec_testbit(e.EV.E1.Eexp,KILL))
                        continue;
                }
                else if (OTbinary(eop))
                {
                    if (!vec_testbit(e.EV.E1.Eexp,KILL) &&
                        !vec_testbit(e.EV.E2.Eexp,KILL))
                        continue;
                }
                else
                        continue;

                vec_setclear(i,KILL,GEN);
            }
        }

        /* GEN the lvalue of an assignment operator      */
        if (OTassign(op) && !OTpost(op) && t.Eexp)
        {
            uint b = t.Eexp;

            vec_setclear(b,GEN,KILL);
        }
    }
}

/************************* LIVE VARIABLES **********************/

/*********************************
 * Do live variable analysis (LVs).
 * A variable is 'live' at some point if there is a
 * subsequent use of it before a redefinition.
 * Binlv = the set of variables live at the beginning of B.
 * Boutlv = the set of variables live at the end of B.
 * Bgen = set of variables used before any definition in B.
 * Bkill = set of variables unambiguously defined before
 *       any use in B.
 * Note that Bgen & Bkill = 0.
 */

void flowlv()
{
    lvgenkill();            /* compute Bgen and Bkill for LVs.      */
    //assert(globsym.top);  /* should be at least some symbols      */

    /* Create a vector of all the variables that are live on exit   */
    /* from the function.                                           */

    vec_t livexit = vec_calloc(globsym.top);
    foreach (uint i; 0 .. globsym.top)
    {
        if (globsym.tab[i].Sflags & SFLlivexit)
            vec_setbit(i,livexit);
    }

    /* The transfer equation is:                            */
    /*      Bin = (Bout - Bkill) | Bgen                     */
    /*      Bout = union of Bin of all successors to B.     */
    /* Using Ullman's algorithm:                            */

    foreach (b; dfo[])
    {
        vec_copy(b.Binlv, b.Bgen);   // Binlv = Bgen
    }

    vec_t tmp = vec_calloc(globsym.top);
    uint cnt = 0;
    bool anychng;
    do
    {
        anychng = false;

        /* For each block B in reverse DFO order        */
        foreach_reverse (b; dfo[])
        {
            /* Bout = union of Bins of all successors to B. */
            bool first = true;
            foreach (bl; ListRange(b.Bsucc))
            {
                const inlv = list_block(bl).Binlv;
                if (first)
                    vec_copy(b.Boutlv, inlv);
                else
                    vec_orass(b.Boutlv, inlv);
                first = false;
            }

            if (first) /* no successors, Boutlv = livexit */
            {   //assert(b.BC==BCret||b.BC==BCretexp||b.BC==BCexit);
                vec_copy(b.Boutlv,livexit);
            }

            /* Bin = (Bout - Bkill) | Bgen                  */
            vec_sub(tmp,b.Boutlv,b.Bkill);
            vec_orass(tmp,b.Bgen);
            if (!anychng)
                anychng = !vec_equal(tmp,b.Binlv);
            vec_copy(b.Binlv,tmp);
        }
        cnt++;
        assert(cnt < 50);
    } while (anychng);

    vec_free(tmp);
    vec_free(livexit);

    static if (0)
    {
        printf("Live variables\n");
        foreach (i, b; dfo[])
        {
            printf("B%d IN\t", cast(int)i);
            vec_println(b.Binlv);
            printf("B%d GEN\t", cast(int)i);
            vec_println(b.Bgen);
            printf("  KILL\t");
            vec_println(b.Bkill);
            printf("  OUT\t");
            vec_println(b.Boutlv);
        }
    }
}

/***********************************
 * Compute Bgen and Bkill for LVs.
 * Allocate Binlv and Boutlv vectors.
 */

private void lvgenkill()
{
    /* Compute ambigsym, a vector of all variables that could be    */
    /* referenced by a *e or a call.                                */

    assert(ambigsym == null);
    ambigsym = vec_calloc(globsym.top);
    foreach (uint i; 0 .. globsym.top)
        if (!(globsym.tab[i].Sflags & SFLunambig))
            vec_setbit(i,ambigsym);

    foreach (b; dfo[])
    {
        vec_free(b.Bgen);
        vec_free(b.Bkill);
        lvelem(&(b.Bgen),&(b.Bkill),b.Belem);
        if (b.BC == BCasm)
        {
            vec_set(b.Bgen);
            vec_clear(b.Bkill);
        }

        vec_free(b.Binlv);
        vec_free(b.Boutlv);
        b.Binlv = vec_calloc(globsym.top);
        b.Boutlv = vec_calloc(globsym.top);
    }

    vec_free(ambigsym);             /* dump any existing one        */
    ambigsym = null;
}

/*****************************
 * Allocate and compute KILL and GEN for live variables.
 */

private void lvelem(vec_t *pgen,vec_t *pkill,elem *n)
{
    *pgen = vec_calloc(globsym.top);
    *pkill = vec_calloc(globsym.top);
    if (n && globsym.top)
        accumlv(*pgen,*pkill,n);
}

/**********************************************
 * Accumulate GEN and KILL sets for LVs for this elem.
 */

private void accumlv(vec_t GEN,vec_t KILL,elem *n)
{
    assert(GEN && KILL && n);

    while (1)
    {
        elem_debug(n);
        const uint op = n.Eoper;
        switch (op)
        {
            case OPvar:
                if (symbol_isintab(n.EV.Vsym))
                {
                    SYMIDX si = n.EV.Vsym.Ssymnum;

                    assert(cast(uint)si < globsym.top);
                    if (!vec_testbit(si,KILL))  // if not in KILL
                        vec_setbit(si,GEN);     // put in GEN
                }
                break;

            case OPcolon:
            case OPcolon2:
            {
                vec_t Gl,Kl,Gr,Kr;
                lvelem(&Gl,&Kl,n.EV.E1);
                lvelem(&Gr,&Kr,n.EV.E2);

                /* GEN |= (Gl | Gr) - KILL      */
                /* KILL |= (Kl & Kr) - GEN      */

                vec_orass(Gl,Gr);
                vec_subass(Gl,KILL);
                vec_orass(GEN,Gl);
                vec_andass(Kl,Kr);
                vec_subass(Kl,GEN);
                vec_orass(KILL,Kl);

                vec_free(Gl);
                vec_free(Gr);
                vec_free(Kl);
                vec_free(Kr);
                break;
            }

            case OPandand:
            case OPoror:
            {
                vec_t Gr,Kr;
                accumlv(GEN,KILL,n.EV.E1);
                lvelem(&Gr,&Kr,n.EV.E2);

                /* GEN |= Gr - KILL     */
                /* KILL |= 0            */

                vec_subass(Gr,KILL);
                vec_orass(GEN,Gr);

                vec_free(Gr);
                vec_free(Kr);
                break;
            }

            case OPasm:
                vec_set(GEN);           /* GEN everything not already KILLed */
                vec_subass(GEN,KILL);
                break;

            case OPcall:
            case OPcallns:
            case OPstrcpy:
            case OPmemcpy:
            case OPmemset:
                debug assert(OTrtol(op));
                accumlv(GEN,KILL,n.EV.E2);
                accumlv(GEN,KILL,n.EV.E1);
                goto L1;

            case OPstrcat:
                debug assert(!OTrtol(op));
                accumlv(GEN,KILL,n.EV.E1);
                accumlv(GEN,KILL,n.EV.E2);
            L1:
                vec_orass(GEN,ambigsym);
                vec_subass(GEN,KILL);
                break;

            case OPeq:
            case OPstreq:
            {
                /* Avoid GENing the lvalue of an =      */
                accumlv(GEN,KILL,n.EV.E2);
                elem *t = n.EV.E1;
                if (t.Eoper != OPvar)
                    accumlv(GEN,KILL,t.EV.E1);
                else /* unambiguous assignment */
                {
                    Symbol* s = t.EV.Vsym;
                    symbol_debug(s);

                    uint tsz = tysize(t.Ety);
                    if (op == OPstreq)
                        tsz = cast(uint)type_size(n.ET);

                    /* if not GENed already, KILL it */
                    if (symbol_isintab(s) &&
                        !vec_testbit(s.Ssymnum,GEN) &&
                        t.EV.Voffset == 0 &&
                        tsz == type_size(s.Stype)
                       )
                    {
                        // printf("%s\n", s.Sident);
                        assert(cast(uint)s.Ssymnum < globsym.top);
                        vec_setbit(s.Ssymnum,KILL);
                    }
                }
                break;
            }

            case OPbt:                          // much like OPind
                accumlv(GEN,KILL,n.EV.E1);
                accumlv(GEN,KILL,n.EV.E2);
                vec_orass(GEN,ambigsym);
                vec_subass(GEN,KILL);
                break;

            case OPind:
            case OPucall:
            case OPucallns:
            case OPstrlen:
                accumlv(GEN,KILL,n.EV.E1);

                /* If it was a *p elem, set bits in GEN for all symbols */
                /* it could have referenced, but only if bits in KILL   */
                /* are not already set.                                 */

                vec_orass(GEN,ambigsym);
                vec_subass(GEN,KILL);
                break;

            default:
                if (OTunary(op))
                {
                    n = n.EV.E1;
                    continue;
                }
                else if (OTrtol(op) && ERTOL(n))
                {
                    accumlv(GEN,KILL,n.EV.E2);

                    /* Note that lvalues of op=,i++,i-- elems */
                    /* are GENed.                               */
                    n = n.EV.E1;
                    continue;
                }
                else if (OTbinary(op))
                {
                    accumlv(GEN,KILL,n.EV.E1);
                    n = n.EV.E2;
                    continue;
                }
                break;
        }
        break;
    }
}

/********************* VERY BUSY EXPRESSIONS ********************/

/**********************************************
 * Compute very busy expressions(VBEs).
 * That is,expressions that are evaluated along
 * separate paths.
 * Bin = the set of VBEs at the beginning of B.
 * Bout = the set of VBEs at the end of B.
 * Bgen = set of expressions X+Y such that X+Y is
 *      evaluated before any def of X or Y.
 * Bkill = set of expressions X+Y such that X or Y could
 *      be defined before X+Y is computed.
 * Note that gen and kill are mutually exclusive.
 */

void flowvbe()
{
    flowxx = VBE;
    aecpgenkill();          /* compute Bgen and Bkill for VBEs      */
    if (go.exptop <= 1)     /* if no candidates for VBEs            */
        return;

    /*foreach (uint i; 0 .. go.exptop)
            printf("go.expnod[%d] = 0x%x\n",i,go.expnod[i]);*/

    /* The transfer equation is:                    */
    /*      Bout = & Bin(all successors S of B)     */
    /*      Bin =(Bout - Bkill) | Bgen              */
    /* Using Ullman's algorithm:                    */

    /*printf("defkill = "); vec_println(go.defkill);
    printf("starkill = "); vec_println(go.starkill);*/

    foreach (b; dfo[])
    {
        /*printf("block %p\n",b);
        printf("Bgen = "); vec_println(b.Bgen);
        printf("Bkill = "); vec_println(b.Bkill);*/

        if (b.BC == BCret || b.BC == BCretexp || b.BC == BCexit)
            vec_clear(b.Bout);
        else
            vec_set(b.Bout);

        /* Bin = (Bout - Bkill) | Bgen  */
        vec_sub(b.Bin,b.Bout,b.Bkill);
        vec_orass(b.Bin,b.Bgen);
    }

    vec_t tmp = vec_calloc(go.exptop);
    bool anychng;
    do
    {
        anychng = false;

        /* for all blocks except return blocks in reverse dfo order */
        foreach_reverse (b; dfo[])
        {
            if (b.BC == BCret || b.BC == BCretexp || b.BC == BCexit)
                continue;

            /* Bout = & of Bin of all successors */
            bool first = true;
            foreach (bl; ListRange(b.Bsucc))
            {
                const vin = list_block(bl).Bin;
                if (first)
                    vec_copy(b.Bout, vin);
                else
                    vec_andass(b.Bout, vin);

                first = false;
            }

            assert(!first);     // must have successors

            /* Bin = (Bout - Bkill) | Bgen  */
            vec_sub(tmp,b.Bout,b.Bkill);
            vec_orass(tmp,b.Bgen);
            if (!anychng)
                anychng = !vec_equal(tmp,b.Bin);
            vec_copy(b.Bin,tmp);
        }
    } while (anychng);      /* while any changes occurred to any Bin */
    vec_free(tmp);
}

/*************************************
 * Accumulate GEN and KILL sets for VBEs for this elem.
 */

private void accumvbe(vec_t GEN,vec_t KILL,elem *n)
{
    elem *t;

    assert(GEN && KILL && n);
    const uint op = n.Eoper;

    switch (op)
    {
        case OPcolon:
        case OPcolon2:
        {
            vec_t Gl,Gr,Kl,Kr;

            aecpelem(&Gl,&Kl,n.EV.E1);
            aecpelem(&Gr,&Kr,n.EV.E2);

            /* GEN |=((Gr - Kl) | (Gl - Kr)) - KILL */
            vec_subass(Gr,Kl);
            vec_subass(Gl,Kr);
            vec_orass(Gr,Gl);
            vec_subass(Gr,KILL);
            vec_orass(GEN,Gr);

            /* KILL |=(Kl | Kr) - GEN       */
            vec_orass(Kl,Kr);
            vec_subass(Kl,GEN);
            vec_orass(KILL,Kl);

            vec_free(Gl);
            vec_free(Kl);
            vec_free(Gr);
            vec_free(Kr);
            break;
        }

        case OPandand:
        case OPoror:
            accumvbe(GEN,KILL,n.EV.E1);
            /* WARNING: just so happens that it works this way.     */
            /* Be careful about (b+c)||(b+c) being VBEs, only the   */
            /* first should be GENed. Doing things this way instead */
            /* of (GEN |= Gr - KILL) and (KILL |= Kr - GEN) will    */
            /* ensure this.                                         */
            accumvbe(GEN,KILL,n.EV.E2);
            break;

        case OPnegass:
            t = n.EV.E1;
            if (t.Eoper != OPvar)
            {
                accumvbe(GEN,KILL,t.EV.E1);
                if (OTbinary(t.Eoper))
                    accumvbe(GEN,KILL,t.EV.E2);
            }
            break;

        case OPcall:
        case OPcallns:
            accumvbe(GEN,KILL,n.EV.E2);
            goto case OPucall;

        case OPucall:
        case OPucallns:
            t = n.EV.E1;
            // Do not VBE indirect function calls
            if (t.Eoper == OPind)
                t = t.EV.E1;
            accumvbe(GEN,KILL,t);
            break;

        case OPasm:                 // if the dreaded OPasm elem
            vec_set(KILL);          // KILL everything
            vec_subass(KILL,GEN);   // except for GENed stuff
            return;

        default:
            if (OTunary(op))
            {
                t = n.EV.E1;
                accumvbe(GEN,KILL,t);
            }
            else if (ERTOL(n))
            {
                accumvbe(GEN,KILL,n.EV.E2);
                t = n.EV.E1;
                // do not GEN the lvalue of an assignment op
                if (OTassign(op))
                {
                    t = n.EV.E1;
                    if (t.Eoper != OPvar)
                    {
                        accumvbe(GEN,KILL,t.EV.E1);
                        if (OTbinary(t.Eoper))
                            accumvbe(GEN,KILL,t.EV.E2);
                    }
                }
                else
                    accumvbe(GEN,KILL,t);
            }
            else if (OTbinary(op))
            {
                /* do not GEN the lvalue of an assignment op    */
                if (OTassign(op))
                {
                    t = n.EV.E1;
                    if (t.Eoper != OPvar)
                    {
                        accumvbe(GEN,KILL,t.EV.E1);
                        if (OTbinary(t.Eoper))
                            accumvbe(GEN,KILL,t.EV.E2);
                    }
                }
                else
                    accumvbe(GEN,KILL,n.EV.E1);
                accumvbe(GEN,KILL,n.EV.E2);
            }
            break;
    }

    if (n.Eexp)                    /* if a vbe elem                */
    {
        const int ne = n.Eexp;

        assert(go.expnod[ne] == n);
        if (!vec_testbit(ne,KILL))      /* if not already KILLed */
        {
            /* GEN this expression only if it hasn't        */
            /* already been GENed in this block.            */
            /* (Don't GEN common subexpressions.)           */
            if (vec_testbit(ne,GEN))
                vec_clearbit(ne,GEN);
            else
            {
                vec_setbit(ne,GEN); /* GEN this expression  */
                /* GEN all identical expressions            */
                /* (operators only, as there is no point    */
                /* to hoisting out variables and constants) */
                if (!OTleaf(op))
                {
                    foreach (uint i; 1 .. go.exptop)
                    {
                        if (op == go.expnod[i].Eoper &&
                            i != ne &&
                            el_match(n,go.expnod[i]))
                        {
                            vec_setbit(i,GEN);
                            assert(!vec_testbit(i,KILL));
                        }
                    }
                }
            }
        }
        if (op == OPvp_fp || op == OPcvp_fp)
        {
            vec_orass(KILL,go.vptrkill);   /* KILL all vptr accesses */
            vec_subass(KILL,GEN);          /* except for GENed stuff */
        }
    }
    else if (OTdef(op))             /* if definition elem           */
    {
        if (!Eunambig(n))           /* if ambiguous definition      */
        {
            vec_orass(KILL,go.defkill);
            if (OTcalldef(op))
                vec_orass(KILL,go.vptrkill);
        }
        else                    /* unambiguous definition       */
        {
            assert(t.Eoper == OPvar);
            Symbol* s = t.EV.Vsym;  // ptr to var being def'd
            if (!(s.Sflags & SFLunambig))
                vec_orass(KILL,go.starkill);/* kill all 'starred' refs */
            foreach (uint i; 1 .. go.exptop)        // for each vbe elem
            {
                elem *e = go.expnod[i];
                uint eop = e.Eoper;

                /* If it could be changed by the definition,     */
                /* set bit in KILL.                              */
                if (eop == OPvar)
                {
                    if (e.EV.Vsym != s)
                        continue;
                }
                else if (OTbinary(eop))
                {
                    if (!vec_testbit(e.EV.E1.Eexp,KILL) &&
                        !vec_testbit(e.EV.E2.Eexp,KILL))
                        continue;
                }
                else if (OTunary(eop))
                {
                    if (!vec_testbit(e.EV.E1.Eexp,KILL))
                        continue;
                }
                else /* OPconst or OPrelconst or OPstring */
                    continue;

                vec_setbit(i,KILL);     // KILL it
            } /* for */
        } /* if */
        vec_subass(KILL,GEN);
    } /* if */
}

}
