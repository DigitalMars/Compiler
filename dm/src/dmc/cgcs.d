/**
 * Compiler implementation of the
 * $(LINK2 http://www.dlang.org, D programming language).
 *
 * Copyright:   Copyright (C) 1985-1995 by Symantec
 *              Copyright (C) 2000-2018 by The D Language Foundation, All Rights Reserved
 * Authors:     $(LINK2 http://www.digitalmars.com, Walter Bright)
 * License:     $(LINK2 http://www.boost.org/LICENSE_1_0.txt, Boost License 1.0)
 * Source:      https://github.com/dlang/dmd/blob/master/src/dmd/backend/cgcs.d
 * Coverage:    https://codecov.io/gh/dlang/dmd/src/master/src/dmd/backend/cgcs.d
 */

module dmd.backend.cgcs;

version (SPP)
{
}
else
{

import core.stdc.stdio;
import core.stdc.stdlib;

import dmd.backend.cc;
import dmd.backend.cdef;
import dmd.backend.code;
import dmd.backend.el;
import dmd.backend.global;
import dmd.backend.oper;
import dmd.backend.ty;
import dmd.backend.type;

import dmd.backend.barray;
import dmd.backend.dlist;
import dmd.backend.dvec;

extern (C++):

/*********************************
 * Struct for each elem:
 *      Helem   pointer to elem
 *      Hhash   hash value for the elem
 */

struct HCS
{
    elem    *Helem;
    uint Hhash;
}

private __gshared
{
    Barray!HCS hcstab;           /* array of hcs's               */
}

struct HCSArray
{
    uint touchstari;
    uint[2] touchfunci;
}

private __gshared HCSArray hcsarray;

// Use a bit vector for quick check if expression is possibly in hcstab[].
// This results in much faster compiles when hcstab[] gets big.
private __gshared vec_t csvec;                     // vector of used entries
enum CSVECDIM = 16001; //8009 //3001     // dimension of csvec (should be prime)

/*******************************
 * Eliminate common subexpressions across extended basic blocks.
 * String together as many blocks as we can.
 */

void comsubs()
{
    block* bl,blc,bln;
    int n;                       /* # of blocks to treat as one  */

    //static int xx;
    //printf("comsubs() %d\n", ++xx);
    //debugx = (xx == 37);

    debug if (debugx) printf("comsubs(%p)\n",startblock);

    // No longer do we just compute Bcount. We now eliminate unreachable
    // blocks.
    block_compbcount();                   // eliminate unreachable blocks

    version (SCPP)
    {
        if (errcnt)
            return;
    }

    if (!csvec)
    {
        csvec = vec_calloc(CSVECDIM);
    }

    for (bl = startblock; bl; bl = bln)
    {
        bln = bl.Bnext;
        if (!bl.Belem)
            continue;                   /* if no expression or no parents       */

        // Count up n, the number of blocks in this extended basic block (EBB)
        n = 1;                          // always at least one block in EBB
        blc = bl;
        while (bln && list_nitems(bln.Bpred) == 1 &&
               ((blc.BC == BCiftrue &&
                 blc.nthSucc(1) == bln) ||
                (blc.BC == BCgoto && blc.nthSucc(0) == bln)
               ) &&
               bln.BC != BCasm         // no CSE's extending across ASM blocks
              )
        {
            n++;                    // add block to EBB
            blc = bln;
            bln = blc.Bnext;
        }
        vec_clear(csvec);
        hcstab.setLength(0);
        hcsarray.touchstari = 0;
        hcsarray.touchfunci[0] = 0;
        hcsarray.touchfunci[1] = 0;
        bln = bl;
        while (n--)                     // while more blocks in EBB
        {
            debug if (debugx)
                printf("cses for block %p\n",bln);

            if (bln.Belem)
                ecom(&bln.Belem);  // do the tree
            bln = bln.Bnext;
        }
    }

    debug if (debugx)
        printf("done with comsubs()\n");
}

/*******************************
 */

void cgcs_term()
{
    vec_free(csvec);
    csvec = null;
    debug debugw && printf("freeing hcstab\n");
    //hcstab.dtor();  // cache allocation for next iteration
}

/*************************
 * Eliminate common subexpressions for an element.
 */

private void ecom(elem **pe)
{
    int op;
    uint hash;
    elem *e;
    elem *ehash;
    tym_t tym;

    e = *pe;
    assert(e);
    elem_debug(e);
    debug assert(e.Ecount == 0);
    //assert(e.Ecomsub == 0);
    tym = tybasic(e.Ety);
    op = e.Eoper;
    switch (op)
    {
        case OPconst:
        case OPvar:
        case OPrelconst:
            break;

        case OPstreq:
        case OPpostinc:
        case OPpostdec:
        case OPeq:
        case OPaddass:
        case OPminass:
        case OPmulass:
        case OPdivass:
        case OPmodass:
        case OPshrass:
        case OPashrass:
        case OPshlass:
        case OPandass:
        case OPxorass:
        case OPorass:
        case OPvecsto:
            /* Reverse order of evaluation for double op=. This is so that  */
            /* the pushing of the address of the second operand is easier.  */
            /* However, with the 8087 we don't need the kludge.             */
            if (op != OPeq && tym == TYdouble && !config.inline8087)
            {
                if (!OTleaf(e.EV.E1.Eoper))
                    ecom(&e.EV.E1.EV.E1);
                ecom(&e.EV.E2);
            }
            else
            {
                /* Don't mark the increment of an i++ or i-- as a CSE, if it */
                /* can be done with an INC or DEC instruction.               */
                if (!(OTpost(op) && elemisone(e.EV.E2)))
                    ecom(&e.EV.E2);           /* evaluate 2nd operand first   */
        case OPnegass:
                if (!OTleaf(e.EV.E1.Eoper))             /* if lvalue is an operator     */
                {
                    if (e.EV.E1.Eoper != OPind)
                        elem_print(e);
                    assert(e.EV.E1.Eoper == OPind);
                    ecom(&(e.EV.E1.EV.E1));
                }
            }
            touchlvalue(e.EV.E1);
            if (!OTpost(op))                /* lvalue of i++ or i-- is not a cse*/
            {
                hash = cs_comphash(e.EV.E1);
                vec_setbit(hash % CSVECDIM,csvec);
                addhcstab(e.EV.E1,hash);              // add lvalue to hcstab[]
            }
            return;

        case OPbtc:
        case OPbts:
        case OPbtr:
        case OPcmpxchg:
            ecom(&e.EV.E1);
            ecom(&e.EV.E2);
            touchfunc(0);                   // indirect assignment
            return;

        case OPandand:
        case OPoror:
        {
            ecom(&e.EV.E1);
            const lengthSave = hcstab.length;
            auto hcsarraySave = hcsarray;
            ecom(&e.EV.E2);
            hcsarray = hcsarraySave;        // no common subs by E2
            hcstab.setLength(lengthSave);
            return;                         /* if comsub then logexp() will */
        }

        case OPcond:
        {
            ecom(&e.EV.E1);
            const lengthSave = hcstab.length;
            auto hcsarraySave = hcsarray;
            ecom(&e.EV.E2.EV.E1);               // left condition
            hcsarray = hcsarraySave;        // no common subs by E2
            hcstab.setLength(lengthSave);
            ecom(&e.EV.E2.EV.E2);               // right condition
            hcsarray = hcsarraySave;        // no common subs by E2
            hcstab.setLength(lengthSave);
            return;                         // can't be a common sub
        }

        case OPcall:
        case OPcallns:
            ecom(&e.EV.E2);                   /* eval right first             */
            goto case OPucall;

        case OPucall:
        case OPucallns:
            ecom(&e.EV.E1);
            touchfunc(1);
            return;

        case OPstrpar:                      /* so we don't break logexp()   */
        case OPinp:                 /* never CSE the I/O instruction itself */
        case OPprefetch:            // don't CSE E2 or the instruction
            ecom(&e.EV.E1);
            goto case OPasm;

        case OPasm:
        case OPstrthis:             // don't CSE these
        case OPframeptr:
        case OPgot:
        case OPctor:
        case OPdtor:
        case OPdctor:
        case OPmark:
            return;

        case OPddtor:
            touchall();
            ecom(&e.EV.E1);
            touchall();
            return;

        case OPparam:
        case OPoutp:
            ecom(&e.EV.E1);
            goto case OPinfo;

        case OPinfo:
            ecom(&e.EV.E2);
            return;

        case OPcomma:
            ecom(&e.EV.E1);
            ecom(&e.EV.E2);
            return;

        case OPremquo:
            ecom(&e.EV.E1);
            ecom(&e.EV.E2);
            break;

        case OPvp_fp:
        case OPcvp_fp:
            ecom(&e.EV.E1);
            touchaccess(e);
            break;

        case OPind:
            ecom(&e.EV.E1);
            /* Generally, CSEing a *(double *) results in worse code        */
            if (tyfloating(tym))
                return;
            break;

        case OPstrcpy:
        case OPstrcat:
        case OPmemcpy:
        case OPmemset:
            ecom(&e.EV.E2);
            goto case OPsetjmp;

        case OPsetjmp:
            ecom(&e.EV.E1);
            touchfunc(0);
            return;

        default:                            /* other operators */
            if (!OTbinary(e.Eoper))
               WROP(e.Eoper);
            assert(OTbinary(e.Eoper));
            goto case OPadd;

        case OPadd:
        case OPmin:
        case OPmul:
        case OPdiv:
        case OPor:
        case OPxor:
        case OPand:
        case OPeqeq:
        case OPne:
        case OPscale:
        case OPyl2x:
        case OPyl2xp1:
            ecom(&e.EV.E1);
            ecom(&e.EV.E2);
            break;

        case OPstring:
        case OPaddr:
        case OPbit:
            WROP(e.Eoper);
            elem_print(e);
            assert(0);              /* optelem() should have removed these  */
            /* NOTREACHED */

        // Explicitly list all the unary ops for speed
        case OPnot: case OPcom: case OPneg: case OPuadd:
        case OPabs: case OPrndtol: case OPrint:
        case OPpreinc: case OPpredec:
        case OPbool: case OPstrlen: case OPs16_32: case OPu16_32:
        case OPd_s32: case OPd_u32:
        case OPs32_d: case OPu32_d: case OPd_s16: case OPs16_d: case OP32_16:
        case OPd_f: case OPf_d:
        case OPd_ld: case OPld_d:
        case OPc_r: case OPc_i:
        case OPu8_16: case OPs8_16: case OP16_8:
        case OPu32_64: case OPs32_64: case OP64_32: case OPmsw:
        case OPu64_128: case OPs64_128: case OP128_64:
        case OPd_s64: case OPs64_d: case OPd_u64: case OPu64_d:
        case OPstrctor: case OPu16_d: case OPd_u16:
        case OParrow:
        case OPvoid:
        case OPbsf: case OPbsr: case OPbswap: case OPpopcnt: case OPvector:
        case OPld_u64:
        case OPsqrt: case OPsin: case OPcos:
        case OPoffset: case OPnp_fp: case OPnp_f16p: case OPf16p_np:
        case OPvecfill:
            ecom(&e.EV.E1);
            break;

        case OPhalt:
            return;
    }

    /* don't CSE structures or unions or volatile stuff   */
    if (tym == TYstruct ||
        tym == TYvoid ||
        e.Ety & mTYvolatile ||
        tyxmmreg(tym) ||
        // don't CSE doubles if inline 8087 code (code generator can't handle it)
        (tyfloating(tym) && config.inline8087)
       )
        return;

    hash = cs_comphash(e);                /* must be AFTER leaves are done */

    /* Search for a match in hcstab[].
     * Search backwards, as most likely matches will be towards the end
     * of the list.
     */

    debug if (debugx) printf("elem: %p hash: %6d\n",e,hash);
    int csveci = hash % CSVECDIM;
    if (vec_testbit(csveci,csvec))
    {
        foreach_reverse (i, ref hcs; hcstab[])
        {
            debug if (debugx)
                printf("i: %2d Hhash: %6d Helem: %p\n",
                       i,hcs.Hhash,hcs.Helem);

            if (hash == hcs.Hhash && (ehash = hcs.Helem) != null)
            {
                /* if elems are the same and we still have room for more    */
                if (el_match(e,ehash) && ehash.Ecount < 0xFF)
                {
                    /* Make sure leaves are also common subexpressions
                     * to avoid false matches.
                     */
                    if (!OTleaf(op))
                    {
                        if (!e.EV.E1.Ecount)
                            continue;
                        if (OTbinary(op) && !e.EV.E2.Ecount)
                            continue;
                    }
                    ehash.Ecount++;
                    *pe = ehash;

                    debug if (debugx)
                        printf("**MATCH** %p with %p\n",e,*pe);

                    el_free(e);
                    return;
                }
            }
        }
    }
    else
        vec_setbit(csveci,csvec);
    addhcstab(e,hash);                    // add this elem to hcstab[]
}

/**************************
 * Compute hash function for elem e.
 */

private uint cs_comphash(elem *e)
{
    int hash;
    uint op;

    elem_debug(e);
    op = e.Eoper;
    hash = (e.Ety & (mTYbasic | mTYconst | mTYvolatile)) + (op << 8);
    if (!OTleaf(op))
    {
        hash += cast(size_t) e.EV.E1;
        if (OTbinary(op))
            hash += cast(size_t) e.EV.E2;
    }
    else
    {
        hash += e.EV.Vint;
        if (op == OPvar || op == OPrelconst)
            hash += cast(size_t) e.EV.Vsym;
    }
    return hash;
}

/****************************
 * Add an elem to the common subexpression table.
 */

private void addhcstab(elem *e,int hash)
{
    hcstab.push(HCS(e, hash));
}

/***************************
 * "touch" the elem.
 * If it is a pointer, "touch" all the suspects
 * who could be pointed to.
 * Eliminate common subs that are indirect loads.
 */

private void touchlvalue(elem *e)
{
    if (e.Eoper == OPind)                /* if indirect store            */
    {
        /* NOTE: Some types of array assignments do not need
         * to touch all variables. (Like a[5], where a is an
         * array instead of a pointer.)
         */

        touchfunc(0);
        return;
    }

    foreach_reverse (ref hcs; hcstab[])
    {
        if (hcs.Helem &&
            hcs.Helem.EV.Vsym == e.EV.Vsym)
            hcs.Helem = null;
    }

    if (!(e.Eoper == OPvar || e.Eoper == OPrelconst))
        elem_print(e);
    assert(e.Eoper == OPvar || e.Eoper == OPrelconst);
    switch (e.EV.Vsym.Sclass)
    {
        case SCregpar:
        case SCregister:
        case SCpseudo:
            break;

        case SCauto:
        case SCparameter:
        case SCfastpar:
        case SCshadowreg:
        case SCbprel:
            if (e.EV.Vsym.Sflags & SFLunambig)
                break;
            goto case SCstatic;

        case SCstatic:
        case SCextern:
        case SCglobal:
        case SClocstat:
        case SCcomdat:
        case SCinline:
        case SCsinline:
        case SCeinline:
        case SCcomdef:
            touchstar();
            break;

        default:
            elem_print(e);
            symbol_print(e.EV.Vsym);
            assert(0);
    }
}

/**************************
 * "touch" variables that could be changed by a function call or
 * an indirect assignment.
 * Eliminate any subexpressions that are "starred" (they need to
 * be recomputed).
 * Input:
 *      flag    If !=0, then this is a function call.
 *              If 0, then this is an indirect assignment.
 */

private void touchfunc(int flag)
{

    //printf("touchfunc(%d)\n", flag);
    HCS *petop = hcstab.ptr + hcstab.length;
    //pe = &hcstab[0]; printf("pe = %p, petop = %p\n",pe,petop);
    assert(hcsarray.touchfunci[flag] <= hcstab.length);
    for (HCS *pe = hcstab.ptr + hcsarray.touchfunci[flag]; pe < petop; pe++)
    {
        elem *he = pe.Helem;
        if (!he)
            continue;
        switch (he.Eoper)
        {
            case OPvar:
                switch (he.EV.Vsym.Sclass)
                {
                    case SCregpar:
                    case SCregister:
                        break;

                    case SCauto:
                    case SCparameter:
                    case SCfastpar:
                    case SCshadowreg:
                    case SCbprel:
                        //printf("he = '%s'\n", he.EV.Vsym.Sident);
                        if (he.EV.Vsym.Sflags & SFLunambig)
                            break;
                        goto case SCstatic;

                    case SCstatic:
                    case SCextern:
                    case SCcomdef:
                    case SCglobal:
                    case SClocstat:
                    case SCcomdat:
                    case SCpseudo:
                    case SCinline:
                    case SCsinline:
                    case SCeinline:
                        if (!(he.EV.Vsym.ty() & mTYconst))
                            goto L1;
                        break;

                    default:
                        //debug WRclass(cast(enum_SC)he.EV.Vsym.Sclass);
                        assert(0);
                }
                break;

            case OPind:
            case OPstrlen:
            case OPstrcmp:
            case OPmemcmp:
            case OPbt:
                goto L1;

            case OPvp_fp:
            case OPcvp_fp:
                if (flag == 0)          /* function calls destroy vptrfptr's, */
                    break;              /* not indirect assignments     */
            L1:
                pe.Helem = null;
                break;

            default:
                break;
        }
    }
    hcsarray.touchfunci[flag] = cast(uint)hcstab.length;
}


/*******************************
 * Eliminate all common subexpressions that
 * do any indirection ("starred" elems).
 */

private void touchstar()
{
    foreach (ref hcs; hcstab[hcsarray.touchstari .. $])
    {
        auto e = hcs.Helem;
        if (e && (e.Eoper == OPind || e.Eoper == OPbt) )
            hcs.Helem = null;
    }
    hcsarray.touchstari = cast(uint)hcstab.length;
}

/*******************************
 * Eliminate all common subexpressions.
 */

private void touchall()
{
    foreach (ref hcs; hcstab[])
    {
        hcs.Helem = null;
    }
    hcsarray.touchstari    = cast(uint)hcstab.length;
    hcsarray.touchfunci[0] = cast(uint)hcstab.length;
    hcsarray.touchfunci[1] = cast(uint)hcstab.length;
}

/*****************************************
 * Eliminate any common subexpressions that could be modified
 * if a handle pointer access occurs.
 */

private void touchaccess(elem *ev)
{
    ev = ev.EV.E1;
    foreach (ref hcs; hcstab[])
    {
        auto e = hcs.Helem;
        /* Invalidate any previous handle pointer accesses that */
        /* are not accesses of ev.                              */
        if (e && (e.Eoper == OPvp_fp || e.Eoper == OPcvp_fp) && e.EV.E1 != ev)
            hcs.Helem = null;
    }
}

}
