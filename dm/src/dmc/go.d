/**
 * Compiler implementation of the
 * $(LINK2 http://www.dlang.org, D programming language).
 *
 * Copyright:   Copyright (C) 1986-1998 by Symantec
 *              Copyright (c) 2000-2017 by Digital Mars, All Rights Reserved
 * Authors:     $(LINK2 http://www.digitalmars.com, Walter Bright)
 * License:     Distributed under the Boost Software License, Version 1.0.
 *              http://www.boost.org/LICENSE_1_0.txt
 * Source:      https://github.com/dlang/dmd/blob/master/src/dmd/backend/go.c
 */

version (SPP)
{
}
else
{

import core.stdc.stdio;
import core.stdc.stdlib;
import core.stdc.string;
import core.stdc.time;

import dmd.backend.cc;
import dmd.backend.cdef;
import dmd.backend.oper;
import dmd.backend.global;
import dmd.backend.el;
import dmd.backend.ty;
import dmd.backend.type;

import dmd.backend.dlist;

import dvec;

extern (C++):

/***************************************
 * Bit masks for various optimizations.
 */

alias mftype = uint;        /* a type big enough for all the flags  */
enum
{
    MFdc    = 1,               // dead code
    MFda    = 2,               // dead assignments
    MFdv    = 4,               // dead variables
    MFreg   = 8,               // register variables
    MFcse   = 0x10,            // global common subexpressions
    MFvbe   = 0x20,            // very busy expressions
    MFtime  = 0x40,            // favor time (speed) over space
    MFli    = 0x80,            // loop invariants
    MFliv   = 0x100,           // loop induction variables
    MFcp    = 0x200,           // copy propagation
    MFcnp   = 0x400,           // constant propagation
    MFloop  = 0x800,           // loop till no more changes
    MFtree  = 0x1000,          // optelem (tree optimization)
    MFlocal = 0x2000,          // localize expressions
    MFall   = 0xFFFF,          // do everything
}

/**********************************
 * Definition elem vector, used for reaching definitions.
 */

struct DefNode
{
    elem    *DNelem;        // pointer to definition elem
    block   *DNblock;       // pointer to block that the elem is in
    vec_t    DNunambig;     // vector of unambiguous definitions
}

/* Global Variables */
//extern __gshared uint[] optab;

/* Global Optimizer variables
 */
struct GlobalOptimizer
{
    mftype mfoptim;
    uint changes;       // # of optimizations performed

    DefNode *defnod;    // array of definition elems
    uint deftop;        // # of entries in defnod[]
    uint defmax;        // capacity of defnod[]
    uint unambigtop;    // number of unambiguous defininitions ( <= deftop )

    vec_base_t *dnunambig;  // pool to allocate DNunambig vectors from
    uint    dnunambigmax;   // capacity of dnunambig[]

    elem **expnod;      // array of expression elems
    uint exptop;        // top of expnod[]
    block **expblk;     // parallel array of block pointers

    vec_t defkill;      // vector of AEs killed by an ambiguous definition
    vec_t starkill;     // vector of AEs killed by a definition of something that somebody could be
                        // pointing to
    vec_t vptrkill;     // vector of AEs killed by an access
}

extern __gshared GlobalOptimizer go;

/* gdag.c */
void builddags();
void boolopt();
void opt_arraybounds();

/* gflow.c */
void flowrd();
void flowlv();
void flowae();
void flowvbe();
void flowcp();
void flowae();
void genkillae();
void flowarraybounds();
int ae_field_affect(elem *lvalue,elem *e);

/* glocal.c */
void localize();

/* gloop.c */
int blockinit();
void compdom();
void loopopt();
void fillInDNunambig(vec_t v, elem *e);
void updaterd(elem *n,vec_t GEN,vec_t KILL);

/* gother.c */
void rd_arraybounds();
void rd_free();
void constprop();
void copyprop();
void rmdeadass();
void elimass(elem *);
void deadvar();
void verybusyexp();
list_t listrds(vec_t, elem *, vec_t);

/* gslice.c */
void sliceStructs();

/***************************************************************************/

void go_term()
{
    vec_free(go.defkill);
    vec_free(go.starkill);
    vec_free(go.vptrkill);
    util_free(go.expnod);
    util_free(go.expblk);
    util_free(go.defnod);
}

debug
{
                        // to print progress message and current trees set to
                        // DEBUG_TREES to 1 and uncomment next 2 lines
//debug = DEBUG_TREES;
debug (DEBUG_TREES)
    void dbg_optprint(const(char) *);
else
                        // to print progress message, undo comment
    void dbg_optprint(const(char) *c) { printf(c); }
}
else
{
    void dbg_optprint(const(char) *c) { }
}

/**************************************
 * Parse optimizer command line flag.
 * Input:
 *      cp      flag string
 * Returns:
 *      0       not recognized
 *      !=0     recognized
 */

int go_flag(char *cp)
{   uint i;
    uint flag;

    enum GL     // indices of various flags in flagtab[]
    {
        O,all,cnp,cp,cse,da,dc,dv,li,liv,local,loop,
        none,o,reg,space,speed,time,tree,vbe,MAX
    }
    __gshared const char*[GL.MAX] flagtab =
    [   "O","all","cnp","cp","cse","da","dc","dv","li","liv","local","loop",
        "none","o","reg","space","speed","time","tree","vbe"
    ];
    __gshared const mftype[GL.MAX] flagmftab =
    [   0,MFall,MFcnp,MFcp,MFcse,MFda,MFdc,MFdv,MFli,MFliv,MFlocal,MFloop,
        0,0,MFreg,0,MFtime,MFtime,MFtree,MFvbe
    ];

    i = GL.MAX;

    //printf("go_flag('%s')\n", cp);
    flag = binary(cp + 1,cast(const(char)**)flagtab.ptr,GL.MAX);
    if (go.mfoptim == 0 && flag != -1)
        go.mfoptim = MFall & ~MFvbe;

    if (*cp == '-')                     /* a regular -whatever flag     */
    {                                   /* cp -> flag string            */
        switch (flag)
        {
            case GL.all:
            case GL.cnp:
            case GL.cp:
            case GL.dc:
            case GL.da:
            case GL.dv:
            case GL.cse:
            case GL.li:
            case GL.liv:
            case GL.local:
            case GL.loop:
            case GL.reg:
            case GL.speed:
            case GL.time:
            case GL.tree:
            case GL.vbe:
                go.mfoptim &= ~flagmftab[flag];    /* clear bits   */
                break;
            case GL.o:
            case GL.O:
            case GL.none:
                go.mfoptim |= MFall & ~MFvbe;      // inverse of -all
                break;
            case GL.space:
                go.mfoptim |= MFtime;      /* inverse of -time     */
                break;
            case -1:                    /* not in flagtab[]     */
                goto badflag;
            default:
                assert(0);
        }
    }
    else if (*cp == '+')                /* a regular +whatever flag     */
    {                           /* cp -> flag string            */
        switch (flag)
        {
            case GL.all:
            case GL.cnp:
            case GL.cp:
            case GL.dc:
            case GL.da:
            case GL.dv:
            case GL.cse:
            case GL.li:
            case GL.liv:
            case GL.local:
            case GL.loop:
            case GL.reg:
            case GL.speed:
            case GL.time:
            case GL.tree:
            case GL.vbe:
                go.mfoptim |= flagmftab[flag];     /* set bits     */
                break;
            case GL.none:
                go.mfoptim &= ~MFall;      /* inverse of +all      */
                break;
            case GL.space:
                go.mfoptim &= ~MFtime;     /* inverse of +time     */
                break;
            case -1:                    /* not in flagtab[]     */
                goto badflag;
            default:
                assert(0);
        }
    }
    if (go.mfoptim)
    {
        go.mfoptim |= MFtree | MFdc;       // always do at least this much
        config.flags4 |= (go.mfoptim & MFtime) ? CFG4speed : CFG4space;
    }
    else
    {
        config.flags4 &= ~CFG4optimized;
    }
    return 1;                   // recognized

badflag:
    return 0;
}

debug (DEBUG_TREES)
{
void dbg_optprint(char *title)
{
    block *b;
    for (b = startblock; b; b = b.Bnext)
        if (b.Belem)
        {
            dll_printf("%s\n",title);
            elem_print(b.Belem);
        }
}
}

/****************************
 * Optimize function.
 */

void optfunc()
{
version (HTOD)
{
}
else
{
    block *b;
    int iter;           // iteration count
    clock_t starttime;

    if (debugc) printf("optfunc()\n");
    dbg_optprint("optfunc\n");

    debug if (debugb)
    {
        dll_printf("................Before optimization.........\n");
        WRfunc();
    }

    iter = 0;

    if (localgot)
    {   // Initialize with:
        //      localgot = OPgot;
        elem *e = el_long(TYnptr, 0);
        e.Eoper = OPgot;
        e = el_bin(OPeq, TYnptr, el_var(localgot), e);
        startblock.Belem = el_combine(e, startblock.Belem);
    }

    // Each pass through the loop can reduce only one level of comma expression.
    // The infinite loop check needs to take this into account.
    // Add 100 just to give optimizer more rope to try to converge.
    int iterationLimit = 0;
    for (b = startblock; b; b = b.Bnext)
    {
        if (!b.Belem)
            continue;
        int d = el_countCommas(b.Belem) + 100;
        if (d > iterationLimit)
            iterationLimit = d;
    }

    // Some functions can take enormous amounts of time to optimize.
    // We try to put a lid on it.
    starttime = clock();
    do
    {
        //printf("iter = %d\n", iter);
        if (++iter > 200)
        {   assert(iter < iterationLimit);      // infinite loop check
            break;
        }
version (MARS)
        util_progress();
else
        file_progress();

        //printf("optelem\n");
        /* canonicalize the trees        */
        for (b = startblock; b; b = b.Bnext)
            if (b.Belem)
            {
                debug if (debuge)
                {
                    dll_printf("before\n");
                    elem_print(b.Belem);
                    //el_check(b.Belem);
                }

                b.Belem = doptelem(b.Belem,bc_goal[b.BC] | GOALagain);

                debug if (0 && debugf)
                {
                    dll_printf("after\n");
                    elem_print(b.Belem);
                }
            }
        //printf("blockopt\n");
        if (go.mfoptim & MFdc)
            blockopt(0);                // do block optimization
        out_regcand(&globsym);          // recompute register candidates
        go.changes = 0;                 // no changes yet
        sliceStructs();
        if (go.mfoptim & MFcnp)
            constprop();                /* make relationals unsigned     */
        if (go.mfoptim & (MFli | MFliv))
            loopopt();                  /* remove loop invariants and    */
                                        /* induction vars                */
                                        /* do loop rotation              */
        else
            for (b = startblock; b; b = b.Bnext)
                b.Bweight = 1;
        dbg_optprint("boolopt\n");

        if (go.mfoptim & MFcnp)
            boolopt();                  // optimize boolean values
        if (go.changes && go.mfoptim & MFloop && (clock() - starttime) < 30 * CLOCKS_PER_SEC)
            continue;

        if (go.mfoptim & MFcnp)
            constprop();                /* constant propagation          */
        if (go.mfoptim & MFcp)
            copyprop();                 /* do copy propagation           */

        /* Floating point constants and string literals need to be
         * replaced with loads from variables in read-only data.
         * This can result in localgot getting needed.
         */
        Symbol *localgotsave = localgot;
        for (b = startblock; b; b = b.Bnext)
        {
            if (b.Belem)
            {
                b.Belem = doptelem(b.Belem,bc_goal[b.BC] | GOALstruct);
                if (b.Belem)
                    b.Belem = el_convert(b.Belem);
            }
        }
        if (localgot != localgotsave)
        {   /* Looks like we did need localgot, initialize with:
             *  localgot = OPgot;
             */
            elem *e = el_long(TYnptr, 0);
            e.Eoper = OPgot;
            e = el_bin(OPeq, TYnptr, el_var(localgot), e);
            startblock.Belem = el_combine(e, startblock.Belem);
        }

        /* localize() is after localgot, otherwise we wind up with
         * more than one OPgot in a function, which mucks up OSX
         * code generation which assumes at most one (localgotoffset).
         */
        if (go.mfoptim & MFlocal)
            localize();                 // improve expression locality
        if (go.mfoptim & MFda)
            rmdeadass();                /* remove dead assignments       */

        if (debugc) printf("changes = %d\n", go.changes);
        if (!(go.changes && go.mfoptim & MFloop && (clock() - starttime) < 30 * CLOCKS_PER_SEC))
            break;
    } while (1);
    if (debugc) printf("%d iterations\n",iter);
    if (go.mfoptim & MFdc)
        blockopt(1);                    // do block optimization

    for (b = startblock; b; b = b.Bnext)
    {
        if (b.Belem)
            postoptelem(b.Belem);
    }
    if (go.mfoptim & MFvbe)
        verybusyexp();              /* very busy expressions         */
    if (go.mfoptim & MFcse)
        builddags();                /* common subexpressions         */
    if (go.mfoptim & MFdv)
        deadvar();                  /* eliminate dead variables      */

    debug if (debugb)
    {
        dll_printf(".............After optimization...........\n");
        WRfunc();
    }

    // Prepare for code generator
    for (b = startblock; b; b = b.Bnext)
    {
        block_optimizer_free(b);
    }
}
}

}
