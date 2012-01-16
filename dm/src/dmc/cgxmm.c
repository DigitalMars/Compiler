// Copyright (C) 2011-2012 by Digital Mars
// All Rights Reserved
// http://www.digitalmars.com
// Written by Walter Bright
/*
 * This source file is made available for personal use
 * only. The license is in /dmd/src/dmd/backendlicense.txt
 * or /dm/src/dmd/backendlicense.txt
 * For any other uses, please contact Digital Mars.
 */

#if !SPP

#include        <stdio.h>
#include        <string.h>
#include        <time.h>
#include        "cc.h"
#include        "oper.h"
#include        "el.h"
#include        "code.h"
#include        "global.h"
#include        "type.h"
#if SCPP
#include        "exh.h"
#endif
#include        "xmm.h"

static char __file__[] = __FILE__;      /* for tassert.h                */
#include        "tassert.h"

unsigned xmmoperator(tym_t tym, unsigned oper);

/*******************************************
 * Move constant value into xmm register xreg.
 */

code *movxmmconst(unsigned xreg, unsigned sz, targ_size_t value, regm_t flags)
{
    /* Generate:
     *    MOV reg,value
     *    MOV xreg,reg
     * Not so efficient. We should at least do a PXOR for 0.
     */
    assert(mask[xreg] & XMMREGS);
    assert(sz == 4 || sz == 8);
    code *c;
    if (I32 && sz == 8)
    {
        unsigned r;
        regm_t rm = ALLREGS;
        c = allocreg(&rm,&r,TYint);         // allocate scratch register
        union { targ_size_t s; targ_long l[2]; } u;
        u.l[1] = 0;
        u.s = value;
        targ_long *p = &u.l[0];
        c = movregconst(c,r,p[0],0);
        c = genfltreg(c,0x89,r,0);            // MOV floatreg,r
        c = movregconst(c,r,p[1],0);
        c = genfltreg(c,0x89,r,4);            // MOV floatreg+4,r

        unsigned op = xmmload(TYdouble);
        c = genfltreg(c,op,xreg - XMM0,0);     // MOVSD XMMreg,floatreg
    }
    else
    {
        unsigned reg;
        c = regwithvalue(CNIL,ALLREGS,value,&reg,(sz == 8) ? 64 : 0);
        c = gen2(c,LODD,modregxrmx(3,xreg-XMM0,reg));     // MOVD xreg,reg
        if (sz == 8)
            code_orrex(c, REX_W);
    }
    return c;
}

/***********************************************
 * Do simple orthogonal operators for XMM registers.
 */

code *orthxmm(elem *e, regm_t *pretregs)
{   elem *e1 = e->E1;
    elem *e2 = e->E2;
    regm_t retregs = *pretregs & XMMREGS;
    if (!retregs)
        retregs = XMMREGS;
    code *c = codelem(e1,&retregs,FALSE); // eval left leaf
    unsigned reg = findreg(retregs);
    regm_t rretregs = XMMREGS & ~retregs;
    code *cr = scodelem(e2, &rretregs, retregs, TRUE);  // eval right leaf

    unsigned op = xmmoperator(e1->Ety, e->Eoper);
    unsigned rreg = findreg(rretregs);

    code *cg;
    if (OTrel(e->Eoper))
    {
        retregs = mPSW;
        cg = NULL;
        code *cc = gen2(CNIL,op,modregxrmx(3,rreg-XMM0,reg-XMM0));
        return cat4(c,cr,cg,cc);
    }
    else
        cg = getregs(retregs);

    code *co = gen2(CNIL,op,modregxrmx(3,reg-XMM0,rreg-XMM0));
    if (retregs != *pretregs)
        co = cat(co,fixresult(e,retregs,pretregs));

    return cat4(c,cr,cg,co);
}


/************************
 * Generate code for an assignment using XMM registers.
 */

code *xmmeq(elem *e,regm_t *pretregs)
{
    tym_t tymll;
    unsigned reg;
    int i;
    code *cl,*cr,*c,cs;
    elem *e11;
    bool regvar;                  /* TRUE means evaluate into register variable */
    regm_t varregm;
    unsigned varreg;
    targ_int postinc;

    //printf("xmmeq(e = %p, *pretregs = %s)\n", e, regm_str(*pretregs));
    elem *e1 = e->E1;
    elem *e2 = e->E2;
    int e2oper = e2->Eoper;
    tym_t tyml = tybasic(e1->Ety);              /* type of lvalue               */
    regm_t retregs = *pretregs;

    if (!(retregs & XMMREGS))
        retregs = XMMREGS;              // pick any XMM reg

    cs.Iop = xmmstore(tyml);
    regvar = FALSE;
    varregm = 0;
    if (config.flags4 & CFG4optimized)
    {
        // Be careful of cases like (x = x+x+x). We cannot evaluate in
        // x if x is in a register.
        if (isregvar(e1,&varregm,&varreg) &&    // if lvalue is register variable
            doinreg(e1->EV.sp.Vsym,e2)          // and we can compute directly into it
           )
        {   regvar = TRUE;
            retregs = varregm;
            reg = varreg;       /* evaluate directly in target register */
        }
    }
    if (*pretregs & mPSW && !EOP(e1))     // if evaluating e1 couldn't change flags
    {   // Be careful that this lines up with jmpopcode()
        retregs |= mPSW;
        *pretregs &= ~mPSW;
    }
    cr = scodelem(e2,&retregs,0,TRUE);    // get rvalue

    // Look for special case of (*p++ = ...), where p is a register variable
    if (e1->Eoper == OPind &&
        ((e11 = e1->E1)->Eoper == OPpostinc || e11->Eoper == OPpostdec) &&
        e11->E1->Eoper == OPvar &&
        e11->E1->EV.sp.Vsym->Sfl == FLreg
       )
    {
        postinc = e11->E2->EV.Vint;
        if (e11->Eoper == OPpostdec)
            postinc = -postinc;
        cl = getlvalue(&cs,e11,RMstore | retregs);
        freenode(e11->E2);
    }
    else
    {   postinc = 0;
        cl = getlvalue(&cs,e1,RMstore | retregs);       // get lvalue (cl == CNIL if regvar)
    }

    c = getregs_imm(varregm);

    reg = findreg(retregs & XMMREGS);
    cs.Irm |= modregrm(0,(reg - XMM0) & 7,0);
    if ((reg - XMM0) & 8)
        cs.Irex |= REX_R;

    // Do not generate mov from register onto itself
    if (!(regvar && reg == XMM0 + ((cs.Irm & 7) | (cs.Irex & REX_B ? 8 : 0))))
        c = gen(c,&cs);         // MOV EA+offset,reg

    if (e1->Ecount ||                     // if lvalue is a CSE or
        regvar)                           // rvalue can't be a CSE
    {
        c = cat(c,getregs_imm(retregs));        // necessary if both lvalue and
                                        //  rvalue are CSEs (since a reg
                                        //  can hold only one e at a time)
        cssave(e1,retregs,EOP(e1));     // if lvalue is a CSE
    }

    c = cat4(cr,cl,c,fixresult(e,retregs,pretregs));
Lp:
    if (postinc)
    {
        int reg = findreg(idxregm(&cs));
        if (*pretregs & mPSW)
        {   // Use LEA to avoid touching the flags
            unsigned rm = cs.Irm & 7;
            if (cs.Irex & REX_B)
                rm |= 8;
            c = genc1(c,0x8D,buildModregrm(2,reg,rm),FLconst,postinc);
            if (tysize(e11->E1->Ety) == 8)
                code_orrex(c, REX_W);
        }
        else if (I64)
        {
            c = genc2(c,0x81,modregrmx(3,0,reg),postinc);
            if (tysize(e11->E1->Ety) == 8)
                code_orrex(c, REX_W);
        }
        else
        {
            if (postinc == 1)
                c = gen1(c,0x40 + reg);         // INC reg
            else if (postinc == -(targ_int)1)
                c = gen1(c,0x48 + reg);         // DEC reg
            else
            {
                c = genc2(c,0x81,modregrm(3,0,reg),postinc);
            }
        }
    }
    freenode(e1);
    return c;
}

/********************************
 * Generate code for conversion using SSE2 instructions.
 *
 *      OPs32_d
 *      OPs64_d (64-bit only)
 *      OPu32_d (64-bit only)
 *      OPd_f
 *      OPf_d
 *      OPd_s32
 *      OPd_s64 (64-bit only)
 *
 */

code *xmmcnvt(elem *e,regm_t *pretregs)
{
    code *c;
    unsigned op=0, regs;
    tym_t ty;
    unsigned char rex = 0;
    bool zx = false; // zero extend uint

    /* There are no ops for integer <-> float/real conversions
     * but there are instructions for them. In order to use these
     * try to fuse chained conversions. Be careful not to loose
     * precision for real to long.
     */
    elem *e1 = e->E1;
    switch (e->Eoper)
    {
    case OPd_f:
        switch (e1->Eoper)
        {
        case OPs32_d:              goto Litof;
        case OPs64_d: rex = REX_W; goto Litof;
        case OPu32_d: rex = REX_W; zx = true; goto Litof;
        Litof:
            // directly use si2ss
            regs = ALLREGS;
            e1 = e1->E1;
            op = CVTSI2SS;
            break;
        default:
            regs = XMMREGS;
            op = CVTSD2SS;
            break;
        }
        ty = TYfloat;
        break;

    case OPs32_d:              goto Litod;
    case OPs64_d: rex = REX_W; goto Litod;
    case OPu32_d: rex = REX_W; zx = true; goto Litod;
    Litod:
        regs = ALLREGS;
        op = CVTSI2SD;
        ty = TYdouble;
        break;

    case OPd_s32: ty = TYint;  goto Ldtoi;
    case OPd_s64: ty = TYlong; rex = REX_W; goto Ldtoi;
    Ldtoi:
        regs = XMMREGS;
        switch (e1->Eoper)
        {
        case OPf_d:
            e1 = e1->E1;
            op = CVTTSS2SI;
            break;
        case OPld_d:
            if (e->Eoper == OPd_s64)
                return cnvt87(e,pretregs); // precision
            /* FALL-THROUGH */
        default:
            op = CVTTSD2SI;
            break;
        }
        break;

    case OPf_d:
        regs = XMMREGS;
        op = CVTSS2SD;
        ty = TYdouble;
        break;
    }
    assert(op);

    c = codelem(e1, &regs, FALSE);
    unsigned reg = findreg(regs);
    if (reg >= XMM0)
        reg -= XMM0;
    else if (zx)
    {   assert(I64);
        c = cat(c,getregs(regs));
        c = genregs(c,0x89,reg,reg); // MOV reg,reg to zero upper 32-bit
        code_orflag(c,CFvolatile);
    }

    unsigned retregs = *pretregs;
    if (tyxmmreg(ty)) // target is XMM
    {   if (!(*pretregs & XMMREGS))
            retregs = XMMREGS;
    }
    else              // source is XMM
    {   assert(regs & XMMREGS);
        if (!(retregs & ALLREGS))
            retregs = ALLREGS;
    }

    unsigned rreg;
    c = cat(c,allocreg(&retregs,&rreg,ty));
    if (rreg >= XMM0)
        rreg -= XMM0;

    c = gen2(c, op, modregxrmx(3,rreg,reg));
    assert(I64 || !rex);
    if (rex)
        code_orrex(c, rex);

    if (*pretregs != retregs)
        c = cat(c,fixresult(e,retregs,pretregs));
    return c;
}

/********************************
 * Generate code for op=
 */

code *xmmopass(elem *e,regm_t *pretregs)
{   elem *e1 = e->E1;
    elem *e2 = e->E2;
    tym_t ty1 = tybasic(e1->Ety);
    unsigned sz1 = tysize[ty1];
    regm_t rretregs = XMMREGS & ~*pretregs;
    if (!rretregs)
        rretregs = XMMREGS;

    code *cr = codelem(e2,&rretregs,FALSE); // eval right leaf
    unsigned rreg = findreg(rretregs);

    code cs;
    code *cl,*cg;

    regm_t retregs;
    unsigned reg;
    bool regvar = FALSE;
    if (config.flags4 & CFG4optimized)
    {
        // Be careful of cases like (x = x+x+x). We cannot evaluate in
        // x if x is in a register.
        unsigned varreg;
        regm_t varregm;
        if (isregvar(e1,&varregm,&varreg) &&    // if lvalue is register variable
            doinreg(e1->EV.sp.Vsym,e2)          // and we can compute directly into it
           )
        {   regvar = TRUE;
            retregs = varregm;
            reg = varreg;                       // evaluate directly in target register
            cl = NULL;
            cg = getregs(retregs);              // destroy these regs
        }
    }

    if (!regvar)
    {
        cl = getlvalue(&cs,e1,rretregs);        // get EA
        retregs = *pretregs & XMMREGS & ~rretregs;
        if (!retregs)
            retregs = XMMREGS & ~rretregs;
        cg = allocreg(&retregs,&reg,ty1);
        cs.Iop = xmmload(ty1);                  // MOVSD xmm,xmm_m64
        code_newreg(&cs,reg - XMM0);
        cg = gen(cg,&cs);
    }

    unsigned op = xmmoperator(e1->Ety, e->Eoper);
    code *co = gen2(CNIL,op,modregxrmx(3,reg-XMM0,rreg-XMM0));

    if (!regvar)
    {
        cs.Iop = xmmstore(ty1);           // reverse operand order of MOVS[SD]
        gen(co,&cs);
    }

    if (e1->Ecount ||                     // if lvalue is a CSE or
        regvar)                           // rvalue can't be a CSE
    {
        cl = cat(cl,getregs_imm(retregs));        // necessary if both lvalue and
                                        //  rvalue are CSEs (since a reg
                                        //  can hold only one e at a time)
        cssave(e1,retregs,EOP(e1));     // if lvalue is a CSE
    }

    co = cat(co,fixresult(e,retregs,pretregs));
    freenode(e1);
    return cat4(cr,cl,cg,co);
}

/******************
 * Negate operator
 */

code *xmmneg(elem *e,regm_t *pretregs)
{
    //printf("xmmneg()\n");
    //elem_print(e);
    assert(*pretregs);
    tym_t tyml = tybasic(e->E1->Ety);
    int sz = tysize[tyml];

    regm_t retregs = *pretregs & XMMREGS;
    if (!retregs)
        retregs = XMMREGS;

    /* Generate:
     *    MOV reg,e1
     *    MOV rreg,signbit
     *    XOR reg,rreg
     */
    code *cl = codelem(e->E1,&retregs,FALSE);
    cl = cat(cl,getregs(retregs));
    unsigned reg = findreg(retregs);
    regm_t rretregs = XMMREGS & ~retregs;
    unsigned rreg;
    cl = cat(cl,allocreg(&rretregs,&rreg,tyml));
    targ_size_t signbit = 0x80000000;
    if (sz == 8)
        signbit = 0x8000000000000000LL;
    code *c = movxmmconst(rreg, sz, signbit, 0);

    code *cg = getregs(retregs);
    unsigned op = (sz == 8) ? XORPD : XORPS;       // XORPD/S reg,rreg
    code *co = gen2(CNIL,op,modregxrmx(3,reg-XMM0,rreg-XMM0));
    co = cat(co,fixresult(e,retregs,pretregs));
    return cat4(cl,c,cg,co);
}

/*****************************
 * Get correct load operator based on type.
 * It is important to use the right one even if the number of bits moved is the same,
 * as there are performance consequences for using the wrong one.
 */

unsigned xmmload(tym_t tym)
{   unsigned op;
    switch (tybasic(tym))
    {
        case TYfloat:
        case TYifloat:  op = LODSS; break;       // MOVSS
        case TYdouble:
        case TYidouble: op = LODSD; break;       // MOVSD

        case TYfloat4:  op = LODAPS; break;      // MOVAPS
        case TYdouble2: op = LODAPD; break;      // MOVAPD
        case TYschar16:
        case TYuchar16:
        case TYshort8:
        case TYushort8:
        case TYlong4:
        case TYulong4:
        case TYllong2:
        case TYullong2: op = LODDQA; break;      // MOVDQA

        default:
            printf("tym = x%x\n", tym);
            assert(0);
    }
    return op;
}

/*****************************
 * Get correct store operator based on type.
 */

unsigned xmmstore(tym_t tym)
{   unsigned op;
    switch (tybasic(tym))
    {
        case TYfloat:
        case TYifloat:  op = STOSS; break;       // MOVSS
        case TYdouble:
        case TYidouble:
        case TYllong:
        case TYullong:
        case TYuint:
        case TYlong:
        case TYcfloat:  op = STOSD; break;       // MOVSD

        case TYfloat4:  op = STOAPS; break;      // MOVAPS
        case TYdouble2: op = STOAPD; break;      // MOVAPD
        case TYschar16:
        case TYuchar16:
        case TYshort8:
        case TYushort8:
        case TYlong4:
        case TYulong4:
        case TYllong2:
        case TYullong2: op = STODQA; break;      // MOVDQA

        default:
            printf("tym = x%x\n", tym);
            assert(0);
    }
    return op;
}

/************************************
 * Get correct XMM operator based on type and operator.
 */

unsigned xmmoperator(tym_t tym, unsigned oper)
{
    tym = tybasic(tym);
    unsigned op;
    switch (oper)
    {
        case OPadd:
        case OPaddass:
            switch (tym)
            {
                case TYfloat:
                case TYifloat:  op = ADDSS;  break;
                case TYdouble:
                case TYidouble: op = ADDSD;  break;

                // SIMD vector types
                case TYfloat4:  op = ADDPS;  break;
                case TYdouble2: op = ADDPD;  break;
                case TYschar16:
                case TYuchar16: op = PADDB;  break;
                case TYshort8:
                case TYushort8: op = PADDW;  break;
                case TYlong4:
                case TYulong4:  op = PADDD;  break;
                case TYllong2:
                case TYullong2: op = PADDQ;  break;

                default:        assert(0);
            }
            break;

        case OPmin:
        case OPminass:
            switch (tym)
            {
                case TYfloat:
                case TYifloat:  op = SUBSS;  break;
                case TYdouble:
                case TYidouble: op = SUBSD;  break;

                // SIMD vector types
                case TYfloat4:  op = SUBPS;  break;
                case TYdouble2: op = SUBPD;  break;
                case TYschar16:
                case TYuchar16: op = PSUBB;  break;
                case TYshort8:
                case TYushort8: op = PSUBW;  break;
                case TYlong4:
                case TYulong4:  op = PSUBD;  break;
                case TYllong2:
                case TYullong2: op = PSUBQ;  break;

                default:        assert(0);
            }
            break;

        case OPmul:
        case OPmulass:
            switch (tym)
            {
                case TYfloat:
                case TYifloat:  op = MULSS;  break;
                case TYdouble:
                case TYidouble: op = MULSD;  break;

                // SIMD vector types
                case TYfloat4:  op = MULPS;  break;
                case TYdouble2: op = MULPD;  break;
                case TYshort8:
                case TYushort8: op = PMULLW; break;

                default:        assert(0);
            }
            break;

        case OPdiv:
        case OPdivass:
            switch (tym)
            {
                case TYfloat:
                case TYifloat:  op = DIVSS;  break;
                case TYdouble:
                case TYidouble: op = DIVSD;  break;

                // SIMD vector types
                case TYfloat4:  op = DIVPS;  break;
                case TYdouble2: op = DIVPD;  break;

                default:        assert(0);
            }
            break;

        case OPor:
        case OPorass:
            switch (tym)
            {
                // SIMD vector types
                case TYschar16:
                case TYuchar16:
                case TYshort8:
                case TYushort8:
                case TYlong4:
                case TYulong4:
                case TYllong2:
                case TYullong2: op = POR; break;

                default:        assert(0);
            }
            break;

        case OPand:
        case OPandass:
            switch (tym)
            {
                // SIMD vector types
                case TYschar16:
                case TYuchar16:
                case TYshort8:
                case TYushort8:
                case TYlong4:
                case TYulong4:
                case TYllong2:
                case TYullong2: op = PAND; break;

                default:        assert(0);
            }
            break;

        case OPxor:
        case OPxorass:
            switch (tym)
            {
                // SIMD vector types
                case TYschar16:
                case TYuchar16:
                case TYshort8:
                case TYushort8:
                case TYlong4:
                case TYulong4:
                case TYllong2:
                case TYullong2: op = PXOR; break;

                default:        assert(0);
            }
            break;

        case OPlt:
        case OPle:
        case OPgt:
        case OPge:
        case OPne:
        case OPeqeq:
        case OPunord:        /* !<>=         */
        case OPlg:           /* <>           */
        case OPleg:          /* <>=          */
        case OPule:          /* !>           */
        case OPul:           /* !>=          */
        case OPuge:          /* !<           */
        case OPug:           /* !<=          */
        case OPue:           /* !<>          */
        case OPngt:
        case OPnge:
        case OPnlt:
        case OPnle:
        case OPord:
        case OPnlg:
        case OPnleg:
        case OPnule:
        case OPnul:
        case OPnuge:
        case OPnug:
        case OPnue:
            switch (tym)
            {
                case TYfloat:
                case TYifloat:  op = UCOMISS;  break;
                case TYdouble:
                case TYidouble: op = UCOMISD;  break;

                default:        assert(0);
            }
            break;

        default:
            assert(0);
    }
    return op;
}

code *cdvector(elem *e, regm_t *pretregs)
{
    /* e should look like:
     *    vector
     *      |
     *    param
     *    /   \
     *  param op2
     *  /   \
     * op   op1
     */

    elem *e1 = e->E1;
    assert(e1->Eoper == OPparam);
    elem *op2 = e1->E2;
    e1 = e1->E1;
    assert(e1->Eoper == OPparam);
    elem *eop = e1->E1;
    assert(eop->Eoper == OPconst);
    elem *op1 = e1->E2;

    tym_t ty1 = tybasic(op1->Ety);
    unsigned sz1 = tysize[ty1];
    assert(sz1 == 16);       // float or double
    regm_t retregs = *pretregs & XMMREGS;
    if (!retregs)
        retregs = XMMREGS;
    code *c = codelem(op1,&retregs,FALSE); // eval left leaf
    unsigned reg = findreg(retregs);
    regm_t rretregs = XMMREGS & ~retregs;
    code *cr = scodelem(op2, &rretregs, retregs, TRUE);  // eval right leaf
    unsigned rreg = findreg(rretregs);
    code *cg = getregs(retregs);
    unsigned op = el_tolong(eop);
    code *co = gen2(CNIL,op,modregxrmx(3,reg-XMM0,rreg-XMM0));
    co = cat(co,fixresult(e,retregs,pretregs));
    return cat4(c,cr,cg,co);
}

#endif // !SPP
