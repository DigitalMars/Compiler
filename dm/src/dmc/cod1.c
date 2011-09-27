// Copyright (C) 1984-1998 by Symantec
// Copyright (C) 2000-2010 by Digital Mars
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
#include        "el.h"
#include        "oper.h"
#include        "code.h"
#include        "global.h"
#include        "type.h"

static char __file__[] = __FILE__;      /* for tassert.h                */
#include        "tassert.h"

targ_size_t paramsize(elem *e,unsigned stackalign);
STATIC code * funccall (elem *,unsigned,unsigned,regm_t *,regm_t);

/* array to convert from index register to r/m field    */
                                       /* AX CX DX BX SP BP SI DI       */
static const signed char regtorm32[8] = {  0, 1, 2, 3,-1, 5, 6, 7 };
             signed char regtorm  [8] = { -1,-1,-1, 7,-1, 6, 4, 5 };

/**************************
 * Determine if e is a 32 bit scaled index addressing mode.
 * Returns:
 *      0       not a scaled index addressing mode
 *      !=0     the value for ss in the SIB byte
 */

int isscaledindex(elem *e)
{   targ_uns ss;

    assert(I32);
    while (e->Eoper == OPcomma)
        e = e->E2;
    if (!(e->Eoper == OPshl && !e->Ecount &&
          e->E2->Eoper == OPconst &&
          (ss = e->E2->EV.Vuns) <= 3
         )
       )
        ss = 0;
    return ss;
}

/*********************************************
 * Generate code for which isscaledindex(e) returned a non-zero result.
 */

code *cdisscaledindex(elem *e,regm_t *pidxregs,regm_t keepmsk)
{   code *c;
    regm_t r;

    // Load index register with result of e->E1
    c = NULL;
    while (e->Eoper == OPcomma)
    {
        r = 0;
        c = cat(c,scodelem(e->E1,&r,keepmsk,TRUE));
        freenode(e);
        e = e->E2;
    }
    assert(e->Eoper == OPshl);
    c = cat(c,scodelem(e->E1,pidxregs,keepmsk,TRUE));
    freenode(e->E2);
    freenode(e);
    return c;
}

/***********************************
 * Determine index if we can do two LEA instructions as a multiply.
 * Returns:
 *      0       can't do it
 */

static struct Ssindex
{
    targ_uns product;
    char ss1;
    char ss2;
    char ssflags;
        #define SSFLnobp        1       // can't have EBP in relconst
        #define SSFLnobase1     2       // no base register for first LEA
        #define SSFLnobase      4       // no base register
        #define SSFLlea         8       // can do it in one LEA
} ssindex_array[] =
{       {0, 0,0},               // [0] is a place holder

        {3, 1,0,SSFLnobp | SSFLlea},
        {5, 2,0,SSFLnobp | SSFLlea},
        {9, 3,0,SSFLnobp | SSFLlea},

        {6, 1,1,SSFLnobase},
        {12,1,2,SSFLnobase},
        {24,1,3,SSFLnobase},
        {10,2,1,SSFLnobase},
        {20,2,2,SSFLnobase},
        {40,2,3,SSFLnobase},
        {18,3,1,SSFLnobase},
        {36,3,2,SSFLnobase},
        {72,3,3,SSFLnobase},

        {15,2,1,SSFLnobp},
        {25,2,2,SSFLnobp},
        {27,3,1,SSFLnobp},
        {45,3,2,SSFLnobp},
        {81,3,3,SSFLnobp},

        {16,3,1,SSFLnobase1 | SSFLnobase},
        {32,3,2,SSFLnobase1 | SSFLnobase},
        {64,3,3,SSFLnobase1 | SSFLnobase},
};

int ssindex(int op,targ_uns product)
{   int i;

    if (op == OPshl)
        product = 1 << product;
    for (i = 1; i < arraysize(ssindex_array); i++)
    {
        if (ssindex_array[i].product == product)
            return i;
    }
    return 0;
}

/***************************************
 * Build an EA of the form disp[base][index*scale].
 * Input:
 *      c       struct to fill in
 *      base    base register (-1 if none)
 *      index   index register (-1 if none)
 *      scale   scale factor - 1,2,4,8
 *      disp    displacement
 */

void buildEA(code *c,int base,int index,int scale,targ_size_t disp)
{   unsigned char rm;
    unsigned char sib;

    sib = 0;
    if (I32)
    {   unsigned ss;

        assert(index != SP);

        switch (scale)
        {   case 1:     ss = 0; break;
            case 2:     ss = 1; break;
            case 4:     ss = 2; break;
            case 8:     ss = 3; break;
            default:    assert(0);
        }

        if (base == -1)
        {
            if (index == -1)
                rm = modregrm(0,0,5);
            else
            {
                rm  = modregrm(0,0,4);
                sib = modregrm(ss,index,5);
            }
        }
        else if (index == -1)
        {
            if (base == SP)
            {
                rm  = modregrm(2,0,4);
                sib = modregrm(0,4,SP);
            }
            else
                rm = modregrm(2,0,base);
        }
        else
        {
            rm  = modregrm(2,0,4);
            sib = modregrm(ss,index,base);
        }
    }
    else
    {
        // -1 AX CX DX BX SP BP SI DI
        static unsigned char EA16rm[9][9] =
        {
            {   0x06,0x09,0x09,0x09,0x87,0x09,0x86,0x84,0x85,   },      // -1
            {   0x09,0x09,0x09,0x09,0x09,0x09,0x09,0x09,0x09,   },      // AX
            {   0x09,0x09,0x09,0x09,0x09,0x09,0x09,0x09,0x09,   },      // CX
            {   0x09,0x09,0x09,0x09,0x09,0x09,0x09,0x09,0x09,   },      // DX
            {   0x87,0x09,0x09,0x09,0x09,0x09,0x09,0x80,0x81,   },      // BX
            {   0x09,0x09,0x09,0x09,0x09,0x09,0x09,0x09,0x09,   },      // SP
            {   0x86,0x09,0x09,0x09,0x09,0x09,0x09,0x82,0x83,   },      // BP
            {   0x84,0x09,0x09,0x09,0x80,0x09,0x82,0x09,0x09,   },      // SI
            {   0x85,0x09,0x09,0x09,0x81,0x09,0x83,0x09,0x09,   }       // DI
        };

        assert(scale == 1);
        rm = EA16rm[base + 1][index + 1];
        assert(rm != 9);
    }
    c->Irm = rm;
    c->Isib = sib;
    c->IFL1 = FLconst;
    c->IEV1.Vuns = disp;
}

/**************************
 * For elems in regcon that don't match regconsave,
 * clear the corresponding bit in regcon.cse.mval.
 * Do same for regcon.immed.
 */

void andregcon(con_t *pregconsave)
{   int i;
    regm_t m;

    m = ~1;
    for (i = 0; i < REGMAX; i++)
    {   if (pregconsave->cse.value[i] != regcon.cse.value[i])
            regcon.cse.mval &= m;
        if (pregconsave->immed.value[i] != regcon.immed.value[i])
            regcon.immed.mval &= m;
        m <<= 1;
        m |= 1;
    }
    //printf("regcon.cse.mval = x%x, regconsave->mval = x%x ",regcon.cse.mval,pregconsave->cse.mval);
    regcon.used |= pregconsave->used;
    regcon.cse.mval &= pregconsave->cse.mval;
    regcon.immed.mval &= pregconsave->immed.mval;
    regcon.params &= pregconsave->params;
    //printf("regcon.cse.mval&regcon.cse.mops = x%x, regcon.cse.mops = x%x\n",regcon.cse.mval & regcon.cse.mops,regcon.cse.mops);
    regcon.cse.mops &= regcon.cse.mval;
}

/*********************************
 * Scan down comma-expressions.
 * Output:
 *      *pe = first elem down right side that is not an OPcomma
 * Returns:
 *      code generated for left branches of comma-expressions
 */

code *docommas(elem **pe)
{   elem *e;
    code *cc;
    unsigned stackpushsave;
    int stackcleansave;

    stackpushsave = stackpush;
    stackcleansave = cgstate.stackclean;
    cgstate.stackclean = 0;
    cc = CNIL;
    e = *pe;
    while (1)
    {   elem *eold;
        regm_t retregs;

        if (configv.addlinenumbers && e->Esrcpos.Slinnum)
        {       cc = genlinnum(cc,e->Esrcpos);
                //e->Esrcpos.Slinnum = 0;               // don't do it twice
        }
        if (e->Eoper != OPcomma)
                break;
        retregs = 0;
        cc = cat(cc,codelem(e->E1,&retregs,TRUE));
        eold = e;
        e = e->E2;
        freenode(eold);
    }
    *pe = e;
    assert(cgstate.stackclean == 0);
    cgstate.stackclean = stackcleansave;
    cc = genstackclean(cc,stackpush - stackpushsave,0);
    return cc;
}

/****************************************
 * Clean stack after call to codelem().
 */

code *gencodelem(code *c,elem *e,regm_t *pretregs,bool constflag)
{
    if (e)
    {
        unsigned stackpushsave;
        int stackcleansave;

        stackpushsave = stackpush;
        stackcleansave = cgstate.stackclean;
        cgstate.stackclean = 0;                         // defer cleaning of stack
        c = cat(c,codelem(e,pretregs,constflag));
        assert(cgstate.stackclean == 0);
        cgstate.stackclean = stackcleansave;
        c = genstackclean(c,stackpush - stackpushsave,*pretregs);       // do defered cleaning
    }
    return c;
}

/********************************************
 * Gen a save/restore sequence for mask of registers.
 */

void gensaverestore(regm_t regm,code **csave,code **crestore)
{   code *cs1;
    code *cs2;
    int i;

    cs1 = NULL;
    cs2 = NULL;
    regm &= mBP | mES | ALLREGS;
    for (i = 0; regm; i++)
    {
        if (regm & 1)
        {
            assert(i != ES);                    // fix later
            cs1 = gen1(cs1,0x50 + i);
            cs2 = cat(gen1(NULL,0x58 + i),cs2);
        }
        regm >>= 1;
    }
    *csave = cs1;
    *crestore = cs2;
}

/****************************************
 * Clean parameters off stack.
 * Input:
 *      numpara         amount to adjust stack pointer
 *      keepmsk         mask of registers to not destroy
 */

code *genstackclean(code *c,unsigned numpara,regm_t keepmsk)
{
    //dbg_printf("genstackclean(numpara = %d, stackclean = %d)\n",numpara,cgstate.stackclean);
    if (numpara && (cgstate.stackclean || STACKALIGN == 16))
    {
#if 0       // won't work if operand of scodelem
        if (numpara == stackpush &&             // if this is all those pushed
            needframe &&                        // and there will be a BP
            !config.windows &&
            !(regcon.mvar & fregsaved)          // and no registers will be pushed
        )
            c = genregs(c,0x89,BP,SP);  // MOV SP,BP
        else
#endif
        {   regm_t scratchm = 0;
            unsigned r;

            if (numpara == REGSIZE && config.flags4 & CFG4space)
            {
                scratchm = ALLREGS & ~keepmsk & regcon.used & ~regcon.mvar;
            }

            if (scratchm)
            {   c = cat(c,allocreg(&scratchm,&r,TYint));
                c = gen1(c,0x58 + r);           // POP r
            }
            else
                c = genc2(c,0x81,modregrm(3,0,SP),numpara); // ADD SP,numpara
        }
        stackpush -= numpara;
        c = genadjesp(c,-numpara);
    }
    return c;
}

/*********************************
 * Generate code for a logical expression.
 * Input:
 *      e       elem
 *      jcond
 *         bit 1 if TRUE then goto jump address if e
 *               if FALSE then goto jump address if !e
 *         2    don't call save87()
 *      fltarg   FLcode or FLblock, flavor of target if e evaluates to jcond
 *      targ    either code or block pointer to destination
 */

code *logexp(elem *e,int jcond,unsigned fltarg,code *targ)
{ code *cc,*c,*ce,*cnop;
  regm_t retregs;
  unsigned op;
  int no87;

  //printf("logexp(e = %p, jcond = %d)\n", e, jcond);
  no87 = (jcond & 2) == 0;
  _chkstack();
  cc = docommas(&e);            /* scan down commas                     */
  cgstate.stackclean++;

  if (EOP(e) && !e->Ecount)     /* if operator and not common sub */
  {     con_t regconsave;

        switch (e->Eoper)
        {   case OPoror:
                if (jcond & 1)
                {       c = logexp(e->E1,jcond,fltarg,targ);
                        regconsave = regcon;
                        ce = logexp(e->E2,jcond,fltarg,targ);
                }
                else
                {       cnop = gennop(CNIL);
                        c = logexp(e->E1,jcond | 1,FLcode,cnop);
                        regconsave = regcon;
                        ce = logexp(e->E2,jcond,fltarg,targ);
                        ce = cat(ce,cnop);
                }
                cnop = CNIL;
                goto L1;

            case OPandand:
                if (jcond & 1)
                {       cnop = gennop(CNIL);    /* a dummy target address */
                        c = logexp(e->E1,jcond & ~1,FLcode,cnop);
                        regconsave = regcon;
                        ce = logexp(e->E2,jcond,fltarg,targ);
                }
                else
                {       c = logexp(e->E1,jcond,fltarg,targ);
                        regconsave = regcon;
                        ce = logexp(e->E2,jcond,fltarg,targ);
                        cnop = CNIL;
                }
        L1:     andregcon(&regconsave);
                freenode(e);
                c = cat4(cc,c,ce,cnop);
                goto Lret;

            case OPnot:
                jcond ^= 1;
            case OPbool:
            case OPs8int:
            case OPu8int:
            case OPs16_32:
            case OPu16_32:
            case OPs32_64:
            case OPu32_64:
            case OPu32_d:
            case OPd_ld:
                c = logexp(e->E1,jcond,fltarg,targ);
                freenode(e);
                goto Lretc;

            case OPcond:
            {   code *cnop2;
                con_t regconold;

                cnop2 = gennop(CNIL);   /* addresses of start of leaves */
                cnop = gennop(CNIL);
                c = logexp(e->E1,FALSE,FLcode,cnop2);   /* eval condition */
                regconold = regcon;
                ce = logexp(e->E2->E1,jcond,fltarg,targ);
                ce = genjmp(ce,JMP,FLcode,(block *) cnop); /* skip second leaf */

                regconsave = regcon;
                regcon = regconold;

                code_next(cnop2) = logexp(e->E2->E2,jcond,fltarg,targ);
                andregcon(&regconold);
                andregcon(&regconsave);
                freenode(e->E2);
                freenode(e);
                c = cat6(cc,c,NULL,ce,cnop2,cnop);
                goto Lret;
            }
        }
  }

  /* Special code for signed long compare       */
  if (OTrel2(e->Eoper) &&               /* if < <= >= >                 */
      !e->Ecount &&
      ( (!I32 && tybasic(e->E1->Ety) == TYlong  && tybasic(e->E2->Ety) == TYlong) ||
        ( I32 && tybasic(e->E1->Ety) == TYllong && tybasic(e->E2->Ety) == TYllong))
     )
  {
        c = longcmp(e,jcond,fltarg,targ);
        goto Lretc;
  }

  retregs = mPSW;               /* return result in flags               */
  op = jmpopcode(e);            /* get jump opcode                      */
  if (!(jcond & 1))
        op ^= 0x101;            // toggle jump condition(s)
  c = codelem(e,&retregs,TRUE); /* evaluate elem                        */
  if (no87)
        c = cat(c,cse_flush(no87));     // flush CSE's to memory
  genjmp(c,op,fltarg,(block *) targ);   /* generate jmp instruction     */
Lretc:
  c = cat(cc,c);
Lret:
  cgstate.stackclean--;
  return c;
}

/******************************
 * Routine to aid in setting things up for gen().
 * Look for common subexpression.
 * Can handle indirection operators, but not if they're common subs.
 * Input:
 *      e ->    elem where we get some of the data from
 *      cs ->   partially filled code to add
 *      op =    opcode
 *      reg =   reg field of (mod reg r/m)
 *      offset = data to be added to Voffset field
 *      keepmsk = mask of registers we must not destroy
 *      desmsk  = mask of registers destroyed by executing the instruction
 * Returns:
 *      pointer to code generated
 */

code *loadea(elem *e,code __ss *cs,unsigned op,unsigned reg,targ_size_t offset,
        regm_t keepmsk,regm_t desmsk)
{ unsigned i;
  regm_t rm;
  tym_t tym;
  code *c,*cg,*cd;

#ifdef DEBUG
  if (debugw)
    printf("loadea: e=x%p cs=x%x op=x%x reg=%d offset=%ld keepmsk=x%x desmsk=x%x\n",
            e,cs,op,reg,offset,keepmsk,desmsk);
#endif

  assert(e);
  cs->Iflags = 0;
  cs->Ijty = 0;
  cs->Iop = op;
  if (I32 && op >= 0x100)               /* if 2 byte opcode             */
  {     cs->Iop = op >> 8;
        cs->Iop2 = op;
  }
  tym = e->Ety;

  /* Determine if location we want to get is in a register. If so,      */
  /* substitute the register for the EA.                                */
  /* Note that operators don't go through this. CSE'd operators are     */
  /* picked up by comsub().                                             */
  if (e->Ecount &&                      /* if cse                       */
      e->Ecount != e->Ecomsub &&        /* and cse was generated        */
      op != 0x8D && op != 0xC4 &&       /* and not an LEA or LES        */
      (op != 0xFF || reg != 3) &&       /* and not CALLF MEM16          */
      (op & 0xFFF8) != 0xD8)            // and not 8087 opcode
  {     int sz;

        assert(!EOP(e));                /* can't handle this            */
        rm = regcon.cse.mval & ~regcon.cse.mops & ~regcon.mvar; /* possible regs                */
        sz = tysize(tym);
        if (sz > REGSIZE)
        {
                if (!I32 && sz == 8)
                {       static regm_t rmask[4] = { mDX,mCX,mBX,mAX };
                        rm &= rmask[offset >> 1];
                }

                else if (offset)
                        rm &= mMSW;             /* only high words      */
                else
                        rm &= mLSW;             /* only low words       */
        }
        for (i = 0; rm; i++)
        {       if (mask[i] & rm)
                {       if (regcon.cse.value[i] == e && // if register has elem
                            /* watch out for a CWD destroying DX        */
                            !(i == DX && op == 0xF7 && desmsk & mDX))
                        {
                                /* if ES, then it can only be a load    */
                                if (i == ES)
                                {       if (op != 0x8B)
                                            goto L1;    /* not a load   */
                                        cs->Iop = 0x8C; /* MOV reg,ES   */
                                        cs->Irm = modregrm(3,0,reg);
                                }
                                else    /* XXX reg,i                    */
                                        cs->Irm = modregrm(3,reg,i);
                                c = CNIL;
                                goto L2;
                        }
                        rm &= ~mask[i];
                }
        }
  }

L1:
  c = getlvalue(cs,e,keepmsk);
  if (offset == REGSIZE)
        getlvalue_msw(cs);
  else
        cs->IEVoffset1 += offset;
  cs->Irm |= modregrm(0,reg,0);         /* OR in reg field              */
  if (I32)
  {
      if (reg == 6 && op == 0xFF ||             /* don't PUSH a word    */
          op == 0x0FB7 || op == 0x0FBF ||       /* MOVZX/MOVSX          */
          (op & 0xFFF8) == 0xD8 ||              /* 8087 instructions    */
          op == 0x8D)                           /* LEA                  */
            cs->Iflags &= ~CFopsize;
  }
  else if ((op & 0xFFF8) == 0xD8 && ADDFWAIT())
        cs->Iflags |= CFwait;
L2:
  cg = getregs(desmsk);                 /* save any regs we destroy     */

  /* KLUDGE! fix up DX for divide instructions */
  cd = CNIL;
  if (op == 0xF7 && desmsk == (mAX|mDX))        /* if we need to fix DX */
  {     if (reg == 7)                           /* if IDIV              */
                cd = gen1(cd,0x99);             /* CWD                  */
        else if (reg == 6)                      /* if DIV               */
                cd = genregs(cd,0x33,DX,DX);    /* CLR DX               */
  }

  // Eliminate MOV reg,reg
  if ((cs->Iop & 0xFC) == 0x88 &&
      (cs->Irm & 0xC7) == modregrm(3,0,reg))
        cs->Iop = NOP;

  return cat4(c,cg,cd,gen(CNIL,cs));
}

/**************************
 * Get addressing mode.
 */

unsigned getaddrmode(regm_t idxregs)
{
    unsigned reg;
    unsigned mode;

    if (I32)
    {   reg = findreg(idxregs & (ALLREGS | mBP));
        mode = modregrm(2,0,reg);
    }
    else
    {
        mode =  (idxregs & mBX) ? modregrm(2,0,7) :     /* [BX] */
                (idxregs & mDI) ? modregrm(2,0,5):      /* [DI] */
                (idxregs & mSI) ? modregrm(2,0,4):      /* [SI] */
                                  (assert(0),1);
    }
    return mode;
}

/**********************************************
 */

void getlvalue_msw(code *c)
{
    if (c->IFL1 == FLreg)
    {   unsigned regmsw;

        regmsw = c->IEVsym1->Sregmsw;
        c->Irm = (c->Irm & ~7) | regmsw;
    }
    else
        c->IEVoffset1 += REGSIZE;
}

/**********************************************
 */

void getlvalue_lsw(code *c)
{
    if (c->IFL1 == FLreg)
    {   unsigned reglsw;

        reglsw = c->IEVsym1->Sreglsw;
        c->Irm = (c->Irm & ~7) | reglsw;
    }
    else
        c->IEVoffset1 -= REGSIZE;
}

/******************
 * Compute addressing mode.
 * Generate & return sequence of code (if any).
 * Return in cs the info on it.
 * Input:
 *      pcs ->  where to store data about addressing mode
 *      e ->    the lvalue elem
 *      keepmsk mask of registers we must not destroy or use
 *              if (keepmsk & RMstore), this will be only a store operation
 *              into the lvalue
 *              if (keepmsk & RMload), this will be a read operation only
 */

code *getlvalue(code __ss *pcs,elem *e,regm_t keepmsk)
{ regm_t idxregs;
  unsigned fl,f,opsave;
  code *c;
  elem *e1;
  elem *e11;
  elem *e12;
  bool e1isadd,e1free;
  unsigned reg;
  tym_t ty;
  tym_t e1ty;
  symbol *s;
  unsigned sz;

  //printf("getlvalue(e = %p)\n",e);
  //elem_print(e);
  assert(e);
  elem_debug(e);
  if (e->Eoper == OPvar || e->Eoper == OPrelconst)
  {     s = e->EV.sp.Vsym;
        fl = s->Sfl;
        if (tyfloating(s->ty()))
            obj_fltused();
  }
  else
        fl = FLoper;
  pcs->IFL1 = fl;
  pcs->Iflags = CFoff;                  /* only want offsets            */
  pcs->Ijty = 0;
  pcs->IEVoffset1 = 0;
  ty = e->Ety;
  if (tyfloating(ty))
        obj_fltused();
  sz = tysize(ty);
  if (I32 && sz == SHORTSIZE)
        pcs->Iflags |= CFopsize;
  if (ty & mTYvolatile)
        pcs->Iflags |= CFvolatile;
  c = CNIL;
  switch (fl)
  {
#if 0 && TARGET_LINUX
    case FLgot:
    case FLgotoff:
        gotref = 1;
        pcs->IEVsym1 = s;
        pcs->IEVoffset1 = e->EV.sp.Voffset;
        if (e->Eoper == OPvar && fl == FLgot)
        {
            code *c1;
            int saveop = pcs->Iop;
            idxregs = allregs & ~keepmsk;       // get a scratch register
            c = allocreg(&idxregs,&reg,TYptr);
            pcs->Irm = modregrm(2,reg,BX);      // BX has GOT
            pcs->Isib = 0;
            //pcs->Iflags |= CFvolatile;
            pcs->Iop = 0x8B;
            c = gen(c,pcs);                     // MOV reg,disp[EBX]
            pcs->Irm = modregrm(0,0,reg);
            pcs->IEVoffset1 = 0;
            pcs->Iop = saveop;
        }
        else
        {
            pcs->Irm = modregrm(2,0,BX);        // disp[EBX] is addr
            pcs->Isib = 0;
        }
        break;
#endif
    case FLoper:
#ifdef DEBUG
        if (debugw) printf("getlvalue(e = x%p, km = x%x)\n",e,keepmsk);
#endif
        switch (e->Eoper)
        {
            case OPadd:                 // this way when we want to do LEA
                e1 = e;
                e1free = FALSE;
                e1isadd = TRUE;
                break;
            case OPind:
            case OPpostinc:             // when doing (*p++ = ...)
            case OPpostdec:             // when doing (*p-- = ...)
            case OPbt:
            case OPbtc:
            case OPbtr:
            case OPbts:
                e1 = e->E1;
                e1free = TRUE;
                e1isadd = e1->Eoper == OPadd;
                break;
            default:
#ifdef DEBUG
                elem_print(e);
#endif
                assert(0);
        }
        e1ty = tybasic(e1->Ety);
        if (e1isadd)
        {   e12 = e1->E2;
            e11 = e1->E1;
        }

        /* First see if we can replace *(e+&v) with
         *      MOV     idxreg,e
         *      EA =    [ES:] &v+idxreg
         */

        if (e1isadd &&
            e12->Eoper == OPrelconst &&
            (f = el_fl(e12)) != FLfardata &&
            e1->Ecount == e1->Ecomsub &&
            (!e1->Ecount || (~keepmsk & ALLREGS & mMSW) || (e1ty != TYfptr && e1ty != TYhptr)) &&
            tysize(e11->Ety) == REGSIZE
           )
        {   unsigned char t;            /* component of r/m field */
            int ss;
            int ssi;

            /*assert(datafl[f]);*/              /* what if addr of func? */
            if (I32)
            {   /* Any register can be an index register        */
                idxregs = allregs & ~keepmsk;
                assert(idxregs);

                /* See if e1->E1 can be a scaled index  */
                ss = isscaledindex(e11);
                if (ss)
                {
                    /* Load index register with result of e11->E1       */
                    c = cdisscaledindex(e11,&idxregs,keepmsk);
                    reg = findreg(idxregs);
#if 0 && TARGET_LINUX
                    if (f == FLgot || f == FLgotoff)    // config.flags3 & CFG3pic
                    {
                        gotref = 1;
                        pcs->Irm = modregrm(2,0,4);
                        pcs->Isib = modregrm(ss,reg,BX);
                    }
                    else
#endif
                    {
                        t = stackfl[f] ? 2 : 0;
                        pcs->Irm = modregrm(t,0,4);
                        pcs->Isib = modregrm(ss,reg,5);
                    }
                }
                else if ((e11->Eoper == OPmul || e11->Eoper == OPshl) &&
                         !e11->Ecount &&
                         e11->E2->Eoper == OPconst &&
                         (ssi = ssindex(e11->Eoper,e11->E2->EV.Vuns)) != 0
                        )
                {
                    regm_t scratchm;
                    unsigned r;
                    int ss1;
                    int ss2;
                    char ssflags;

#if 0 && TARGET_LINUX
                    assert(f != FLgot && f != FLgotoff);
#endif
                    ssflags = ssindex_array[ssi].ssflags;
                    if (ssflags & SSFLnobp && stackfl[f])
                        goto L6;

                    // Load index register with result of e11->E1
                    c = scodelem(e11->E1,&idxregs,keepmsk,TRUE);
                    reg = findreg(idxregs);

                    ss1 = ssindex_array[ssi].ss1;
                    if (ssflags & SSFLlea)
                    {
                        assert(!stackfl[f]);
                        pcs->Irm = modregrm(2,0,4);
                        pcs->Isib = modregrm(ss1,reg,reg);
                    }
                    else
                    {   int rbase;

                        scratchm = ALLREGS & ~keepmsk;
                        c = cat(c,allocreg(&scratchm,&r,TYint));

                        if (ssflags & SSFLnobase1)
                        {   t = 0;
                            rbase = 5;
                        }
                        else
                        {   t = 0;
                            rbase = reg;
                            if (rbase == BP)
                            {   static unsigned imm32[4] = {1+1,2+1,4+1,8+1};

                                // IMUL r,BP,imm32
                                c = genc2(c,0x69,modregrm(3,r,BP),imm32[ss1]);
                                goto L7;
                            }
                        }

                        c = gen2sib(c,0x8D,modregrm(t,r,4),modregrm(ss1,reg,rbase));
                        if (ssflags & SSFLnobase1)
                        {   code_last(c)->IFL1 = FLconst;
                            code_last(c)->IEV1.Vuns = 0;
                        }
                    L7:
                        if (ssflags & SSFLnobase)
                        {   t = stackfl[f] ? 2 : 0;
                            rbase = 5;
                        }
                        else
                        {   t = 2;
                            rbase = r;
                            assert(rbase != BP);
                        }
                        pcs->Irm = modregrm(t,0,4);
                        pcs->Isib = modregrm(ssindex_array[ssi].ss2,r,rbase);
                    }
                    freenode(e11->E2);
                    freenode(e11);
                }
                else
                {
                 L6:
                    /* Load index register with result of e11   */
                    c = scodelem(e11,&idxregs,keepmsk,TRUE);
                    pcs->Irm = getaddrmode(idxregs);
#if 0 && TARGET_LINUX
                    if (e12->EV.sp.Vsym->Sfl == FLgot || e12->EV.sp.Vsym->Sfl == FLgotoff)
                    {
                        gotref = 1;
#if 1
                        reg = findreg(idxregs & (ALLREGS | mBP));
                        pcs->Irm = modregrm(2,0,4);
                        pcs->Isib = modregrm(0,reg,BX);
#else
                        pcs->Isib = modregrm(0,pcs->Irm,BX);
                        pcs->Irm = modregrm(2,0,4);
#endif
                    }
                    else
#endif
                    if (stackfl[f])             /* if we need [EBP] too */
                    {
                        pcs->Isib = modregrm(0,pcs->Irm,BP);
                        pcs->Irm = modregrm(2,0,4);
                    }
                }
            }
            else
            {
                idxregs = IDXREGS & ~keepmsk;   /* only these can be index regs */
                assert(idxregs);
#if 0 && TARGET_LINUX
                assert(f != FLgot && f != FLgotoff);
#endif
                if (stackfl[f])                 /* if stack data type   */
                {   idxregs &= mSI | mDI;       /* BX can't index off stack */
                    if (!idxregs) goto L1;      /* index regs aren't avail */
                    t = 6;                      /* [BP+SI+disp]         */
                }
                else
                    t = 0;                      /* [SI + disp]          */
                c = scodelem(e11,&idxregs,keepmsk,TRUE); /* load idx reg */
                pcs->Irm = getaddrmode(idxregs) ^ t;
            }
            if (f == FLpara)
                refparam = TRUE;
            else if (f == FLauto || f == FLtmp || f == FLbprel || f == FLfltreg)
                reflocal = TRUE;
            else if (f == FLcsdata || tybasic(e12->Ety) == TYcptr)
                pcs->Iflags |= CFcs;
            else
                assert(f != FLreg);
            pcs->IFL1 = f;
            pcs->IEVsym1 = e12->EV.sp.Vsym;
            pcs->IEVoffset1 = e12->EV.sp.Voffset; /* += ??? */

            /* If e1 is a CSE, we must generate an addressing mode      */
            /* but also leave EA in registers so others can use it      */
            if (e1->Ecount)
            {   unsigned flagsave;

                idxregs = IDXREGS & ~keepmsk;
                c = cat(c,allocreg(&idxregs,&reg,TYoffset));

                /* If desired result is a far pointer, we'll have       */
                /* to load another register with the segment of v       */
                if (e1ty == TYfptr)
                {
                    unsigned msreg;

                    idxregs |= mMSW & ALLREGS & ~keepmsk;
                    c = cat(c,allocreg(&idxregs,&msreg,TYfptr));
                    msreg = findregmsw(idxregs);
                                                /* MOV msreg,segreg     */
                    c = genregs(c,0x8C,segfl[f],msreg);
                }
                opsave = pcs->Iop;
                flagsave = pcs->Iflags;
                pcs->Iop = 0x8D;
                pcs->Irm |= modregrm(0,reg,0);
                if (I32)
                    pcs->Iflags &= ~CFopsize;
                c = gen(c,pcs);                 /* LEA idxreg,EA        */
                cssave(e1,idxregs,TRUE);
                if (I32)
                    pcs->Iflags = flagsave;
                if (stackfl[f] && (config.wflags & WFssneds))   // if pointer into stack
                    pcs->Iflags |= CFss;        // add SS: override
                pcs->Iop = opsave;
                pcs->IFL1 = FLoffset;
                pcs->IEV1.Vuns = 0;
                pcs->Irm = getaddrmode(idxregs);
            }
            freenode(e12);
            if (e1free)
                freenode(e1);
            goto Lptr;
        }

        L1:

        /* The rest of the cases could be a far pointer */

        idxregs = (I32 ? allregs : IDXREGS) & ~keepmsk; /* only these can be index regs */
        assert(idxregs);
        if (I32 && sz == REGSIZE && keepmsk & RMstore)
            idxregs |= regcon.mvar;

#if !TARGET_FLAT
        switch (e1ty)
        {   case TYfptr:                        /* if far pointer       */
            case TYhptr:
                idxregs = (mES | IDXREGS) & ~keepmsk;   // need segment too
                assert(idxregs & mES);
                pcs->Iflags |= CFes;            /* ES segment override  */
                break;
            case TYsptr:                        /* if pointer to stack  */
                if (config.wflags & WFssneds)   // if SS != DS
                    pcs->Iflags |= CFss;        /* then need SS: override */
                break;
            case TYcptr:                        /* if pointer to code   */
                pcs->Iflags |= CFcs;            /* then need CS: override */
                break;
        }
#endif
        pcs->IFL1 = FLoffset;
        pcs->IEV1.Vuns = 0;

        /* see if we can replace *(e+c) with
         *      MOV     idxreg,e
         *      [MOV    ES,segment]
         *      EA =    [ES:]c[idxreg]
         */

        if (e1isadd && e12->Eoper == OPconst &&
            tysize(e12->Ety) == REGSIZE &&
            (!e1->Ecount || !e1free)
           )
        {   int ss;

            pcs->IEV1.Vuns = e12->EV.Vuns;
            freenode(e12);
            if (e1free) freenode(e1);
            if (I32 && e11->Eoper == OPadd && !e11->Ecount &&
                tysize(e11->Ety) == REGSIZE)
            {
                e12 = e11->E2;
                e11 = e11->E1;
                e1 = e1->E1;
                e1free = TRUE;
                goto L4;
            }
            if (I32 && (ss = isscaledindex(e11)) != 0)
            {   // (v * scale) + const
                c = cdisscaledindex(e11,&idxregs,keepmsk);
                reg = findreg(idxregs);
                pcs->Irm = modregrm(0,0,4);
                pcs->Isib = modregrm(ss,reg,5);
            }
            else
            {
                c = scodelem(e11,&idxregs,keepmsk,TRUE); // load index reg
                pcs->Irm = getaddrmode(idxregs);
            }
            goto Lptr;
        }

        /* Look for *(v1 + v2)
         *      EA = [v1][v2]
         */

        if (I32 && e1isadd && (!e1->Ecount || !e1free) &&
            tysize[e1ty] == REGSIZE)
        {   code *c2;
            regm_t idxregs2;
            unsigned base,index;
            int ss;

        L4:
            // Look for *(v1 + v2 << scale)
            ss = isscaledindex(e12);
            if (ss)
            {
                c = scodelem(e11,&idxregs,keepmsk,TRUE);
                idxregs2 = allregs & ~(idxregs | keepmsk);
                c2 = cdisscaledindex(e12,&idxregs2,keepmsk | idxregs);
            }

            // Look for *(v1 << scale + v2)
            else if ((ss = isscaledindex(e11)) != 0)
            {
                idxregs2 = idxregs;
                c = cdisscaledindex(e11,&idxregs2,keepmsk);
                idxregs = allregs & ~(idxregs2 | keepmsk);
                c2 = scodelem(e12,&idxregs,keepmsk | idxregs2,TRUE);
            }
            // Look for *(((v1 << scale) + c1) + v2)
            else if (e11->Eoper == OPadd && !e11->Ecount &&
                     e11->E2->Eoper == OPconst &&
                     (ss = isscaledindex(e11->E1)) != 0
                    )
            {
                pcs->IEV1.Vuns = e11->E2->EV.Vuns;
                idxregs2 = idxregs;
                c = cdisscaledindex(e11->E1,&idxregs2,keepmsk);
                idxregs = allregs & ~(idxregs2 | keepmsk);
                c2 = scodelem(e12,&idxregs,keepmsk | idxregs2,TRUE);
                freenode(e11->E2);
                freenode(e11);
            }
            else
            {
                c = scodelem(e11,&idxregs,keepmsk,TRUE);
                idxregs2 = allregs & ~(idxregs | keepmsk);
                c2 = scodelem(e12,&idxregs2,keepmsk | idxregs,TRUE);
            }
            c = cat(c,c2);
            base = findreg(idxregs);
            index = findreg(idxregs2);
            pcs->Irm  = modregrm(2,0,4);
            pcs->Isib = modregrm(ss,index,base);
            if (e1free) freenode(e1);
            goto Lptr;
        }

        /* give up and replace *e1 with
         *      MOV     idxreg,e
         *      EA =    0[idxreg]
         * pinholeopt() will usually correct the 0, we need it in case
         * we have a pointer to a long and need an offset to the second
         * word.
         */

        assert(e1free);
        c = scodelem(e1,&idxregs,keepmsk,TRUE); /* load index register  */
        pcs->Irm = getaddrmode(idxregs);
    Lptr:
        if (config.flags3 & CFG3ptrchk)
            cod3_ptrchk(&c,pcs,keepmsk);        // validate pointer code
        break;
    case FLdatseg:
        assert(0);
#if 0
        pcs->Irm = modregrm(0,0,BPRM);
        pcs->IEVpointer1 = e->EVpointer;
        break;
#endif
    case FLfltreg:
        reflocal = TRUE;
        pcs->Irm = modregrm(2,0,BPRM);
        pcs->IEV1.Vint = 0;
        break;
    case FLreg:
        goto L2;
    case FLpara:
        refparam = TRUE;
        pcs->Irm = modregrm(2,0,BPRM);
        goto L2;

    case FLauto:
        if (s->Sclass == SCfastpar && regcon.params & mask[s->Spreg])
        {
            if (keepmsk & RMload)
            {
                if (sz == REGSIZE)      // could this be (sz <= REGSIZE) ?
                {
                    pcs->Irm = modregrm(3,0,s->Spreg);
                    regcon.used |= mask[s->Spreg];
                    break;
                }
            }
            else
                regcon.params &= ~mask[s->Spreg];
        }
    case FLtmp:
    case FLbprel:
        reflocal = TRUE;
        pcs->Irm = modregrm(2,0,BPRM);
        goto L2;
    case FLextern:
        if (s->Sident[0] == '_' && memcmp(s->Sident + 1,"tls_array",10) == 0)
        {
#if TARGET_LINUX || TARGET_FREEBSD || TARGET_SOLARIS
            // Rewrite as GS:[0000]
            pcs->Irm = modregrm(0, 0, BPRM);
            pcs->IFL1 = FLconst;
            pcs->IEV1.Vuns = 0;
            pcs->Iflags = CFgs;
#else
            pcs->Iflags |= CFfs;                // add FS: override
#endif
        }
        if (s->ty() & mTYcs && LARGECODE)
            goto Lfardata;
        goto L3;
    case FLdata:
    case FLudata:
    case FLcsdata:
#if TARGET_LINUX || TARGET_OSX || TARGET_FREEBSD || TARGET_SOLARIS
    case FLgot:
    case FLgotoff:
    case FLtlsdata:
#endif
    L3:
        pcs->Irm = modregrm(0,0,BPRM);
    L2:
        if (fl == FLreg)
        {   assert(s->Sregm & regcon.mvar);
            if (
                s->Sclass == SCregpar ||
                s->Sclass == SCparameter)
            {   refparam = TRUE;
                reflocal = TRUE;        // kludge to set up prolog
            }
            pcs->Irm = modregrm(3,0,s->Sreglsw);
            if (e->EV.sp.Voffset == 1 && sz == 1)
            {   assert(s->Sregm & BYTEREGS);
                pcs->Irm |= 4;                  // use 2nd byte of register
            }
            else
                assert(!e->EV.sp.Voffset);
        }
        else if (s->ty() & mTYcs && !(fl == FLextern && LARGECODE))
        {
            pcs->Iflags |= CFcs | CFoff;
        }
#if TARGET_LINUX || TARGET_OSX || TARGET_FREEBSD || TARGET_SOLARIS
//      if (fl == FLtlsdata || s->ty() & mTYthread)
//          pcs->Iflags |= CFgs;
#endif
        pcs->IEVsym1 = s;
        pcs->IEVoffset1 = e->EV.sp.Voffset;
        if (sz == 1)
        {   /* Don't use SI or DI for this variable     */
            s->Sflags |= GTbyte;
            if (e->EV.sp.Voffset > 1)
                s->Sflags &= ~GTregcand;
        }
        else if (e->EV.sp.Voffset)
            s->Sflags &= ~GTregcand;
        if (!(keepmsk & RMstore))               // if not store only
        {   s->Sflags |= SFLread;               // assume we are doing a read
        }
        break;
    case FLpseudo:
#if MARS
        assert(0);
#else
    {   unsigned u;

        u = s->Sreglsw;
        c = getregs(pseudomask[u]);
        pcs->Irm = modregrm(3,0,pseudoreg[u] & 7);
        break;
    }
#endif
    case FLfardata:
        assert(!TARGET_FLAT);
    case FLfunc:                                /* reading from code seg */
        if (config.exe & EX_flat)
            goto L3;
    Lfardata:
    {   regm_t regm;
        code *c1;

        regm = ALLREGS & ~keepmsk;              /* need scratch register */
        c1 = allocreg(&regm,&reg,TYint);
        /* MOV mreg,seg of symbol       */
        c = gencs(CNIL,0xB8 + reg,0,FLextern,s);
        c->Iflags = CFseg;
        c = gen2(c,0x8E,modregrm(3,0,reg));     /* MOV ES,reg           */
        c = cat3(c1,getregs(mES),c);
        pcs->Iflags |= CFes | CFoff;            /* ES segment override  */
        goto L3;
    }

    case FLstack:
        assert(I32);
        pcs->Irm = modregrm(2,0,4);
        pcs->Isib = modregrm(0,4,SP);
        pcs->IEVsym1 = s;
        pcs->IEVoffset1 = e->EV.sp.Voffset;
        break;

    default:
#ifdef DEBUG
        WRFL((enum FL)fl);
        symbol_print(s);
#endif
        assert(0);
  }
  return c;
}

/*******************************
 * Same as codelem(), but do not destroy the registers in keepmsk.
 * Use scratch registers as much as possible, then use stack.
 * Input:
 *      constflag       TRUE if user of result will not modify the
 *                      registers returned in *pretregs.
 */

code *scodelem(elem *e,regm_t *pretregs,regm_t keepmsk,bool constflag)
{ code *c,*cs1,*cs2,*cs3;
  unsigned i,j;
  regm_t oldmfuncreg,oldregcon,oldregimmed,overlap,tosave,touse;
  int adjesp;
  unsigned stackpushsave;
  char calledafuncsave;

#ifdef DEBUG
  if (debugw)
        printf("+scodelem(e=%p *pretregs=x%x keepmsk=x%x constflag=%d\n",
                e,*pretregs,keepmsk,constflag);
#endif
  elem_debug(e);
  if (constflag)
  {     regm_t regm;
        unsigned reg;

        if (isregvar(e,&regm,&reg) &&           // if e is a register variable
            (regm & *pretregs) == regm &&       // in one of the right regs
            e->EV.sp.Voffset == 0
           )
        {       unsigned sz1,sz2;

                sz1 = tysize(e->Ety);
                sz2 = tysize(e->EV.sp.Vsym->Stype->Tty);
                if (sz1 <= REGSIZE && sz2 > REGSIZE)
                    regm &= mLSW;
                c = fixresult(e,regm,pretregs);
                cssave(e,regm,0);
                freenode(e);
#ifdef DEBUG
                if (debugw)
                    printf("-scodelem(e=%p *pretregs=x%x keepmsk=x%x constflag=%d\n",
                            e,*pretregs,keepmsk,constflag);
#endif
                return c;
        }
  }
  overlap = msavereg & keepmsk;
  msavereg |= keepmsk;          /* add to mask of regs to save          */
  oldregcon = regcon.cse.mval;
  oldregimmed = regcon.immed.mval;
  oldmfuncreg = mfuncreg;       /* remember old one                     */
  mfuncreg = (mBP | mES | ALLREGS) & ~regcon.mvar;
  stackpushsave = stackpush;
#if 0
  if (keepmsk)
        stackpush++;            /* assume we'll have to save stuff on stack */
#endif
  calledafuncsave = calledafunc;
  calledafunc = 0;
  c = codelem(e,pretregs,constflag);    /* generate code for the elem   */
#if 0
  if (keepmsk)
        stackpush--;
#endif

  tosave = keepmsk & ~msavereg; /* registers to save                    */
  if (tosave)
  {     cgstate.stackclean++;
        c = genstackclean(c,stackpush - stackpushsave,*pretregs | msavereg);
        cgstate.stackclean--;
  }

  /* Assert that no new CSEs are generated that are not reflected       */
  /* in mfuncreg.                                                       */
#ifdef DEBUG
  if ((mfuncreg & (regcon.cse.mval & ~oldregcon)) != 0)
        printf("mfuncreg x%x, regcon.cse.mval x%x, oldregcon x%x, regcon.mvar x%x\n",
                mfuncreg,regcon.cse.mval,oldregcon,regcon.mvar);
#endif
  assert((mfuncreg & (regcon.cse.mval & ~oldregcon)) == 0);

  /* bugzilla 3521
   * The problem is:
   *    reg op (reg = exp)
   * where reg must be preserved (in keepregs) while the expression to be evaluated
   * must change it.
   * The only solution is to make this variable not a register.
   */
  if (regcon.mvar & tosave)
  {
        //elem_print(e);
        //printf("test1: regcon.mvar x%x tosave x%x\n", regcon.mvar, tosave);
        cgreg_unregister(regcon.mvar & tosave);
  }

  /* which registers can we use to save other registers in? */
  if (config.flags4 & CFG4space ||              // if optimize for space
      config.target_cpu >= TARGET_80486)        // PUSH/POP ops are 1 cycle
        touse = 0;                              // PUSH/POP pairs are always shorter
  else
  {     touse = mfuncreg & allregs & ~(msavereg | oldregcon | regcon.cse.mval);
        /* Don't use registers we'll have to save/restore               */
        touse &= ~(fregsaved & oldmfuncreg);
        /* Don't use registers that have constant values in them, since
           the code generated might have used the value.
         */
        touse &= ~oldregimmed;
  }

  cs1 = cs2 = cs3 = NULL;
  adjesp = 0;

  for (i = 0; tosave; i++)
  {     regm_t mi = mask[i];

        assert(i < REGMAX);
        if (mi & tosave)        /* i = register to save                 */
        {
            if (touse)          /* if any scratch registers             */
            {   for (j = 0; j < 8; j++)
                {   regm_t mj = mask[j];

                    if (touse & mj)
                    {   cs1 = genmovreg(cs1,j,i);
                        cs2 = cat(genmovreg(CNIL,i,j),cs2);
                        touse &= ~mj;
                        mfuncreg &= ~mj;
                        regcon.used |= mj;
                        break;
                    }
                }
                assert(j < 8);
            }
            else                        /* else use stack               */
            {   int push,pop;

                stackchanged = 1;
                adjesp += REGSIZE;
                if (i == ES)
                {       push = 0x06;
                        pop = 0x07;
                }
                else
                {       push = 0x50 + i;
                        pop = push | 8;
                }
                cs1 = gen1(cs1,push);                   /* PUSH i       */
                cs2 = cat(gen1(CNIL,pop),cs2);          /* POP i        */
            }
            cs3 = cat(getregs(mi),cs3);
            tosave &= ~mi;
        }
  }
  if (adjesp)
  {
        // If this is done an odd number of times, it
        // will throw off the 8 byte stack alignment.
        // We should *only* worry about this if a function
        // was called in the code generation by codelem().
        int sz;
        if (STACKALIGN == 16)
            sz = -(adjesp & (STACKALIGN - 1)) & (STACKALIGN - 1);
        else
            sz = -(adjesp & 7) & 7;
        if (calledafunc && I32 && sz && (STACKALIGN == 16 || config.flags4 & CFG4stackalign))
        {   code *cx;

            regm_t mval_save = regcon.immed.mval;
            regcon.immed.mval = 0;      // prevent reghasvalue() optimizations
                                        // because c hasn't been executed yet
            cs1 = genc2(cs1,0x81,modregrm(3,5,SP),sz);  // SUB ESP,sz
            regcon.immed.mval = mval_save;
            cs1 = genadjesp(cs1, sz);

            cx = genc2(CNIL,0x81,modregrm(3,0,SP),sz);  // ADD ESP,sz
            cx = genadjesp(cx, -sz);
            cs2 = cat(cx, cs2);
        }

        cs1 = genadjesp(cs1,adjesp);
        cs2 = genadjesp(cs2,-adjesp);
  }

  calledafunc |= calledafuncsave;
  msavereg &= ~keepmsk | overlap; /* remove from mask of regs to save   */
  mfuncreg &= oldmfuncreg;      /* update original                      */
#ifdef DEBUG
  if (debugw)
        printf("-scodelem(e=%p *pretregs=x%x keepmsk=x%x constflag=%d\n",
                e,*pretregs,keepmsk,constflag);
#endif
  return cat4(cs1,c,cs3,cs2);
}

/*****************************
 * Given an opcode and EA in cs, generate code
 * for each floating register in turn.
 * Input:
 *      tym     either TYdouble or TYfloat
 */

code *fltregs(code __ss *pcs,tym_t tym)
{   code *c;

    tym = tybasic(tym);
    if (I32)
    {
        c = getregs((tym == TYfloat) ? mAX : mAX | mDX);
        if (tym != TYfloat)
        {
            pcs->IEVoffset1 += REGSIZE;
            NEWREG(pcs->Irm,DX);
            c = gen(c,pcs);
            pcs->IEVoffset1 -= REGSIZE;
        }
        NEWREG(pcs->Irm,AX);
        c = gen(c,pcs);
    }
    else
    {
        c = getregs((tym == TYfloat) ? FLOATREGS_16 : DOUBLEREGS_16);
        pcs->IEVoffset1 += (tym == TYfloat) ? 2 : 6;
        if (tym == TYfloat)
            NEWREG(pcs->Irm,DX);
        else
            NEWREG(pcs->Irm,AX);
        c = gen(c,pcs);
        pcs->IEVoffset1 -= 2;
        if (tym == TYfloat)
            NEWREG(pcs->Irm,AX);
        else
            NEWREG(pcs->Irm,BX);
        gen(c,pcs);
        if (tym != TYfloat)
        {     pcs->IEVoffset1 -= 2;
              NEWREG(pcs->Irm,CX);
              gen(c,pcs);
              pcs->IEVoffset1 -= 2;     /* note that exit is with Voffset unaltered */
              NEWREG(pcs->Irm,DX);
              gen(c,pcs);
        }
    }
    return c;
}

/*****************************
 * Given a result in registers, test it for TRUE or FALSE.
 * Will fail if TYfptr and the reg is ES!
 * If saveflag is TRUE, preserve the contents of the
 * registers.
 */

code *tstresult(regm_t regm,tym_t tym,unsigned saveflag)
{ code *ce;
  unsigned reg;
  unsigned scrreg;                      /* scratch register             */
  unsigned sz;
  regm_t scrregm;

#ifdef DEBUG
  if (!(regm & (mBP | ALLREGS)))
        printf("tstresult(regm = x%x, tym = x%x, saveflag = %d)\n",
            regm,tym,saveflag);
#endif
  assert(regm & (mBP | ALLREGS));
  tym = tybasic(tym);
  ce = CNIL;
  reg = findreg(regm);
  sz = tysize[tym];
  if (sz == 1)
  {     assert(regm & BYTEREGS);
        return genregs(ce,0x84,reg,reg);        // TEST regL,regL
  }
  if (sz <= REGSIZE)
  {
    if (I32)
    {
        if (tym == TYfloat)
        {   if (saveflag)
            {
                scrregm = allregs & ~regm;              /* possible scratch regs */
                ce = allocreg(&scrregm,&scrreg,TYoffset); /* allocate scratch reg */
                ce = genmovreg(ce,scrreg,reg);  /* MOV scrreg,msreg     */
                reg = scrreg;
            }
            ce = cat(ce,getregs(mask[reg]));
            return gen2(ce,0xD1,modregrm(3,4,reg)); /* SHL reg,1        */
        }
        ce = gentstreg(ce,reg);                 // TEST reg,reg
        if (tysize[tym] == SHORTSIZE)
            ce->Iflags |= CFopsize;             /* 16 bit operands      */
    }
    else
        ce = gentstreg(ce,reg);                 // TEST reg,reg
    return ce;
  }
  if (saveflag || tyfv(tym))
  {
        scrregm = ALLREGS & ~regm;              /* possible scratch regs */
        ce = allocreg(&scrregm,&scrreg,TYoffset); /* allocate scratch reg */
        if (I32 || sz == REGSIZE * 2)
        {   code *c;

            assert(regm & mMSW && regm & mLSW);

            reg = findregmsw(regm);
            if (I32)
            {
                if (tyfv(tym))
                {   c = genregs(CNIL,0x0F,scrreg,reg);
                    c->Iop2 = 0xB7;                     /* MOVZX scrreg,msreg   */
                    ce = cat(ce,c);
                }
                else
                {   ce = genmovreg(ce,scrreg,reg);      /* MOV scrreg,msreg     */
                    if (tym == TYdouble || tym == TYdouble_alias)
                        gen2(ce,0xD1,modregrm(3,4,scrreg)); /* SHL scrreg,1     */
                }
            }
            else
            {
                ce = genmovreg(ce,scrreg,reg);  /* MOV scrreg,msreg     */
                if (tym == TYfloat)
                    gen2(ce,0xD1,modregrm(3,4,scrreg)); /* SHL scrreg,1 */
            }
            reg = findreglsw(regm);
            genorreg(ce,scrreg,reg);                    /* OR scrreg,lsreg */
        }
        else if (sz == 8)
        {       /* !I32 */
                ce = genmovreg(ce,scrreg,AX);           /* MOV scrreg,AX */
                if (tym == TYdouble || tym == TYdouble_alias)
                    gen2(ce,0xD1,modregrm(3,4,scrreg)); // SHL scrreg,1
                genorreg(ce,scrreg,BX);                 /* OR scrreg,BX */
                genorreg(ce,scrreg,CX);                 /* OR scrreg,CX */
                genorreg(ce,scrreg,DX);                 /* OR scrreg,DX */
        }
        else
            assert(0);
  }
  else
  {
        if (I32 || sz == REGSIZE * 2)
        {
            /* can't test ES:LSW for 0  */
            assert(regm & mMSW & ALLREGS && regm & (mLSW | mBP));

            reg = findregmsw(regm);
            ce = getregs(mask[reg]);            /* we're going to trash reg */
            if (tyfloating(tym) && sz == 2 * intsize)
                ce = gen2(ce,0xD1,modregrm(3,4,reg));   // SHL reg,1
            ce = genorreg(ce,reg,findreglsw(regm));     // OR reg,reg+1
        }
        else if (sz == 8)
        {   assert(regm == DOUBLEREGS_16);
            ce = getregs(mAX);                          // allocate AX
            if (tym == TYdouble || tym == TYdouble_alias)
                ce = gen2(ce,0xD1,modregrm(3,4,AX));    // SHL AX,1
            genorreg(ce,AX,BX);                         // OR AX,BX
            genorreg(ce,AX,CX);                         // OR AX,CX
            genorreg(ce,AX,DX);                         // OR AX,DX
        }
        else
            assert(0);
  }
  code_orflag(ce,CFpsw);
  return ce;
}

/******************************
 * Given the result of an expression is in retregs,
 * generate necessary code to return result in *pretregs.
 */

code *fixresult(elem *e,regm_t retregs,regm_t *pretregs)
{ code *c,*ce;
  unsigned reg,rreg;
  regm_t forccs,forregs;
  tym_t tym;
  int sz;

//  printf("fixresult(e = %p, retregs = %s, *pretregs = %s)\n",
//      e,regm_str(retregs),regm_str(*pretregs));
  if (*pretregs == 0) return CNIL;      /* if don't want result         */
  assert(e && retregs);                 /* need something to work with  */
  forccs = *pretregs & mPSW;
  forregs = *pretregs & (mST01 | mST0 | mBP | ALLREGS | mES | mSTACK);
  tym = tybasic(e->Ety);
#if 0
  if (tym == TYstruct)
        // Hack to support cdstreq()
        tym = TYfptr;
#else
  if (tym == TYstruct)
        // Hack to support cdstreq()
        tym = (forregs & mMSW) ? TYfptr : TYnptr;
#endif
  c = CNIL;
  sz = tysize[tym];
  if (sz == 1)
  {     unsigned reg;

        assert(retregs & BYTEREGS);
        reg = findreg(retregs);
        if (e->Eoper == OPvar &&
            e->EV.sp.Voffset == 1 &&
            e->EV.sp.Vsym->Sfl == FLreg)
        {
            if (forccs)
                c = gen2(c,0x84,modregrm(3,reg | 4,reg | 4));   // TEST regH,regH
            forccs = 0;
        }
  }
  if ((retregs & forregs) == retregs)   /* if already in right registers */
        *pretregs = retregs;
  else if (forregs)             /* if return the result in registers    */
  {     unsigned opsflag;

        if (forregs & (mST01 | mST0))
            return fixresult87(e,retregs,pretregs);
        ce = CNIL;
        opsflag = FALSE;
        if (!I32 && sz == 8)
        {   if (forregs & mSTACK)
            {   assert(retregs == DOUBLEREGS_16);
                /* Push floating regs   */
                c = CNIL;
                ce = gen1(ce,0x50 + AX);
                gen1(ce,0x50 + BX);
                gen1(ce,0x50 + CX);
                gen1(ce,0x50 + DX);
                stackpush += DOUBLESIZE;
            }
            else if (retregs & mSTACK)
            {   assert(forregs == DOUBLEREGS_16);
                /* Pop floating regs    */
                c = getregs(forregs);
                ce = gen1(ce,0x58 + DX);
                gen1(ce,0x58 + CX);
                gen1(ce,0x58 + BX);
                gen1(ce,0x58 + AX);
                stackpush -= DOUBLESIZE;
                retregs = DOUBLEREGS_16; /* for tstresult() below       */
            }
            else
#ifdef DEBUG
                printf("retregs = x%x, forregs = x%x\n",retregs,forregs),
#endif
                assert(0);
            if (EOP(e))
                opsflag = TRUE;
        }
        else
        {
            c = allocreg(pretregs,&rreg,tym); /* allocate return regs   */
            if (sz > REGSIZE)
            {   unsigned msreg,lsreg;
                unsigned msrreg,lsrreg;

                msreg = findregmsw(retregs);
                lsreg = findreglsw(retregs);
                msrreg = findregmsw(*pretregs);
                lsrreg = findreglsw(*pretregs);

                ce = genmovreg(ce,msrreg,msreg); /* MOV msrreg,msreg    */
                ce = genmovreg(ce,lsrreg,lsreg); /* MOV lsrreg,lsreg    */
            }
            else
            {   reg = findreg(retregs & (mBP | ALLREGS));
                ce = genmovreg(ce,rreg,reg);    /* MOV rreg,reg         */
            }
        }
        c = cat(c,ce);
        cssave(e,retregs | *pretregs,opsflag);
        forregs = 0;                    /* don't care about result in reg  */
                                        /* cuz we have real result in rreg */
        retregs = *pretregs & ~mPSW;
  }
  if (forccs)                           /* if return result in flags    */
        c = cat(c,tstresult(retregs,tym,forregs));
  return c;
}


/********************************
 * Generate code sequence to call C runtime library support routine.
 *      clib = CLIBxxxx
 *      keepmask = mask of registers not to destroy. Currently can
 *              handle only 1. Should use a temporary rather than
 *              push/pop for speed.
 */

int clib_inited = 0;            // != 0 if initialized

code *callclib(elem *e,unsigned clib,regm_t *pretregs,regm_t keepmask)
{ code *c,*cpop;
  regm_t retregs;
  symbol *s;
  int i;

#if TARGET_LINUX || TARGET_OSX || TARGET_FREEBSD || TARGET_SOLARIS
  static symbol lib[] =
  {
/* Convert destroyed regs into saved regs       */
#define Z(desregs)      (~(desregs) & (mBP| mES | ALLREGS))
#if TARGET_LINUX || TARGET_OSX || TARGET_FREEBSD || TARGET_SOLARIS
#define N(name) "_" name
#else
#define N(name) name
#endif

/* Shorthand to map onto SYMBOLY()              */
#define Y(desregs,name)  SYMBOLY(FLfunc,Z(desregs),N(name),0)

    Y(0,"_LCMP__"),                     // CLIBlcmp
    Y(mAX|mCX|mDX,"_LMUL__"),           // CLIBlmul
#if 1
    Y(mAX|mBX|mCX|mDX,"_LDIV__"),       // CLIBldiv
    Y(mAX|mBX|mCX|mDX,"_LDIV__"),       // CLIBlmod
    Y(mAX|mBX|mCX|mDX,"_ULDIV__"),      // CLIBuldiv
    Y(mAX|mBX|mCX|mDX,"_ULDIV__"),      // CLIBulmod
#else
    Y(ALLREGS,"_LDIV__"),               // CLIBldiv
    Y(ALLREGS,"_LDIV__"),               // CLIBlmod
    Y(ALLREGS,"_ULDIV__"),              // CLIBuldiv
    Y(ALLREGS,"_ULDIV__"),              // CLIBulmod
#endif
#if 0
    Y(DOUBLEREGS_16,"_DNEG"),
    Y(mAX|mBX|mCX|mDX,"_DMUL"),         // CLIBdmul
    Y(mAX|mBX|mCX|mDX,"_DDIV"),         // CLIBddiv
    Y(0,"_DTST0"),                      // CLIBdtst0
    Y(0,"_DTST0EXC"),                   // CLIBdtst0exc
    Y(0,"_DCMP"),                       // CLIBdcmp
    Y(0,"_DCMPEXC"),                    // CLIBdcmpexc

    Y(mAX|mBX|mCX|mDX,"_DADD"),         // CLIBdadd
    Y(mAX|mBX|mCX|mDX,"_DSUB"),         // CLIBdsub

    Y(mAX|mBX|mCX|mDX,"_FMUL"),         // CLIBfmul
    Y(mAX|mBX|mCX|mDX,"_FDIV"),         // CLIBfdiv
    Y(0,"_FTST0"),                      // CLIBftst0
    Y(0,"_FTST0EXC"),                   // CLIBftst0exc
    Y(0,"_FCMP"),                       // CLIBfcmp
    Y(0,"_FCMPEXC"),                    // CLIBfcmpexc
    Y(FLOATREGS_32,"_FNEG"),            // CLIBfneg
    Y(mAX|mBX|mCX|mDX,"_FADD"),         // CLIBfadd
    Y(mAX|mBX|mCX|mDX,"_FSUB"),         // CLIBfsub
#endif
    Y(DOUBLEREGS_32,"_DBLLNG"),         // CLIBdbllng
    Y(DOUBLEREGS_32,"_LNGDBL"),         // CLIBlngdbl
    Y(DOUBLEREGS_32,"_DBLINT"),         // CLIBdblint
    Y(DOUBLEREGS_32,"_INTDBL"),         // CLIBintdbl
    Y(DOUBLEREGS_32,"_DBLUNS"),         // CLIBdbluns
    Y(DOUBLEREGS_32,"_UNSDBL"),         // CLIBunsdbl
    Y(mAX|mST0,"_DBLULNG"),             // CLIBdblulng
#if 0
    {DOUBLEREGS_16,DOUBLEREGS_32,0,INFfloat,1,1},       // _ULNGDBL@    ulngdbl
#endif
    Y(DOUBLEREGS_32,"_DBLFLT"),         // CLIBdblflt
    Y(DOUBLEREGS_32,"_FLTDBL"),         // CLIBfltdbl

    Y(DOUBLEREGS_32,"_DBLLLNG"),        // CLIBdblllng
    Y(DOUBLEREGS_32,"_LLNGDBL"),        // CLIBllngdbl
    Y(DOUBLEREGS_32,"_DBLULLNG"),       // CLIBdblullng
    Y(DOUBLEREGS_32,"_ULLNGDBL"),       // CLIBullngdbl

    Y(0,"_DTST"),                       // CLIBdtst
    Y(mES|mBX,"_HTOFPTR"),              // CLIBvptrfptr
    Y(mES|mBX,"_HCTOFPTR"),             // CLIBcvptrfptr
    Y(0,"_87TOPSW"),                    // CLIB87topsw
    Y(mST0,"_FLTTO87"),                 // CLIBfltto87
    Y(mST0,"_DBLTO87"),                 // CLIBdblto87
    Y(mST0|mAX,"_DBLINT87"),            // CLIBdblint87
    Y(mST0|mAX|mDX,"_DBLLNG87"),        // CLIBdbllng87
    Y(0,"_FTST"),                       // CLIBftst
    Y(0,"_FCOMPP"),                     // CLIBfcompp
    Y(0,"_FTEST"),                      // CLIBftest
    Y(0,"_FTEST0"),                     // CLIBftest0
    Y(mST0|mAX|mBX|mCX|mDX,"_FDIVP"),   // CLIBfdiv87

    Y(mST0|mST01,"Cmul"),               // CLIBcmul
    Y(mAX|mCX|mDX|mST0|mST01,"Cdiv"),   // CLIBcdiv
    Y(mAX|mST0|mST01,"Ccmp"),           // CLIBccmp

    Y(mST0,"_U64_LDBL"),                // CLIBu64_ldbl
#if ELFOBJ || MACHOBJ
    Y(mST0|mAX|mDX,"_LDBLULLNG"),       // CLIBld_u64
#else
    Y(mST0|mAX|mDX,"__LDBLULLNG"),      // CLIBld_u64
#endif
  };
#else
  static symbol lib[CLIBMAX] =
  {
/* Convert destroyed regs into saved regs       */
#define Z(desregs)      (~(desregs) & (mBP| mES | ALLREGS))

/* Shorthand to map onto SYMBOLY()              */
#define Y(desregs,name)  SYMBOLY(FLfunc,Z(desregs),name,0)

    Y(0,"_LCMP@"),
    Y(mAX|mCX|mDX,"_LMUL@"),
    Y(ALLREGS,"_LDIV@"),
    Y(ALLREGS,"_LDIV@"),
    Y(ALLREGS,"_ULDIV@"),
    Y(ALLREGS,"_ULDIV@"),
    Y(mAX|mBX|mCX|mDX,"_DMUL@"),
    Y(mAX|mBX|mCX|mDX,"_DDIV@"),
    Y(0,"_DTST0@"),
    Y(0,"_DTST0EXC@"),
    Y(0,"_DCMP@"),
    Y(0,"_DCMPEXC@"),

    /* _DNEG@ only really destroys EDX, but then EAX would hold */
    /* 2 values, and we can't handle that.                      */

    /* _DNEG@ only really destroys AX, but then BX,CX,DX would hold     */
    /* 2 values, and we can't handle that.                              */

    Y(DOUBLEREGS_16,"_DNEG@"),
    Y(mAX|mBX|mCX|mDX,"_DADD@"),
    Y(mAX|mBX|mCX|mDX,"_DSUB@"),

    Y(mAX|mBX|mCX|mDX,"_FMUL@"),
    Y(mAX|mBX|mCX|mDX,"_FDIV@"),
    Y(0,"_FTST0@"),
    Y(0,"_FTST0EXC@"),
    Y(0,"_FCMP@"),
    Y(0,"_FCMPEXC@"),
    Y(FLOATREGS_16,"_FNEG@"),
    Y(mAX|mBX|mCX|mDX,"_FADD@"),
    Y(mAX|mBX|mCX|mDX,"_FSUB@"),
    Y(DOUBLEREGS_16,"_DBLLNG@"),
    Y(DOUBLEREGS_16,"_LNGDBL@"),
    Y(DOUBLEREGS_16,"_DBLINT@"),
    Y(DOUBLEREGS_16,"_INTDBL@"),
    Y(DOUBLEREGS_16,"_DBLUNS@"),
    Y(DOUBLEREGS_16,"_UNSDBL@"),
    Y(DOUBLEREGS_16,"_DBLULNG@"),
    Y(DOUBLEREGS_16,"_ULNGDBL@"),
    Y(DOUBLEREGS_16,"_DBLFLT@"),
    Y(ALLREGS,"_FLTDBL@"),

    Y(DOUBLEREGS_16,"_DBLLLNG@"),
    Y(DOUBLEREGS_16,"_LLNGDBL@"),
#if 0
    Y(DOUBLEREGS_16,"__DBLULLNG"),
#else
    Y(DOUBLEREGS_16,"_DBLULLNG@"),
#endif
    Y(DOUBLEREGS_16,"_ULLNGDBL@"),

    Y(0,"_DTST@"),
    Y(mES|mBX,"_HTOFPTR@"),             // CLIBvptrfptr
    Y(mES|mBX,"_HCTOFPTR@"),            // CLIBcvptrfptr
    Y(0,"_87TOPSW@"),                   // CLIB87topsw
    Y(mST0,"_FLTTO87@"),                // CLIBfltto87
    Y(mST0,"_DBLTO87@"),                // CLIBdblto87
    Y(mST0|mAX,"_DBLINT87@"),           // CLIBdblint87
    Y(mST0|mAX|mDX,"_DBLLNG87@"),       // CLIBdbllng87
    Y(0,"_FTST@"),
    Y(0,"_FCOMPP@"),                    // CLIBfcompp
    Y(0,"_FTEST@"),                     // CLIBftest
    Y(0,"_FTEST0@"),                    // CLIBftest0
    Y(mST0|mAX|mBX|mCX|mDX,"_FDIVP"),   // CLIBfdiv87

    // NOTE: desregs is wrong for 16 bit code, mBX should be included
    Y(mST0|mST01,"_Cmul"),              // CLIBcmul
    Y(mAX|mCX|mDX|mST0|mST01,"_Cdiv"),  // CLIBcdiv
    Y(mAX|mST0|mST01,"_Ccmp"),          // CLIBccmp

    Y(mST0,"_U64_LDBL"),                // CLIBu64_ldbl
    Y(mST0|mAX|mDX,"__LDBLULLNG"),      // CLIBld_u64
  };
#endif

  static struct
  {
    regm_t retregs16;   /* registers that 16 bit result is returned in  */
    regm_t retregs32;   /* registers that 32 bit result is returned in  */
    char pop;           /* # of bytes popped off of stack upon return   */
    char flags;
        #define INF32           1       // if 32 bit only
        #define INFfloat        2       // if this is floating point
        #define INFwkdone       4       // if weak extern is already done
    char push87;                        // # of pushes onto the 8087 stack
    char pop87;                         // # of pops off of the 8087 stack
  } info[CLIBMAX] =
  {
    {0,0,0,0},                          /* _LCMP@       lcmp    */
    {mDX|mAX,mDX|mAX,0,0},              // _LMUL@       lmul
    {mDX|mAX,mDX|mAX,0,0},              // _LDIV@       ldiv
    {mCX|mBX,mCX|mBX,0,0},              /* _LDIV@       lmod    */
    {mDX|mAX,mDX|mAX,0,0},              /* _ULDIV@      uldiv   */
    {mCX|mBX,mCX|mBX,0,0},              /* _ULDIV@      ulmod   */

#if TARGET_WINDOS
    {DOUBLEREGS_16,DOUBLEREGS_32,8,INFfloat,1,1},       // _DMUL@       dmul
    {DOUBLEREGS_16,DOUBLEREGS_32,8,INFfloat,1,1},       // _DDIV@       ddiv
    {0,0,0,2},                                          // _DTST0@
    {0,0,0,2},                                          // _DTST0EXC@
    {0,0,8,INFfloat,1,1},                               // _DCMP@       dcmp
    {0,0,8,INFfloat,1,1},                               // _DCMPEXC@    dcmp
    {DOUBLEREGS_16,DOUBLEREGS_32,0,2},                  // _DNEG@       dneg
    {DOUBLEREGS_16,DOUBLEREGS_32,8,INFfloat,1,1},       // _DADD@       dadd
    {DOUBLEREGS_16,DOUBLEREGS_32,8,INFfloat,1,1},       // _DSUB@       dsub

    {FLOATREGS_16,FLOATREGS_32,0,INFfloat,1,1},         // _FMUL@       fmul
    {FLOATREGS_16,FLOATREGS_32,0,INFfloat,1,1},         // _FDIV@       fdiv
    {0,0,0,2},                                          // _FTST0@
    {0,0,0,2},                                          // _FTST0EXC@
    {0,0,0,INFfloat,1,1},                               // _FCMP@       fcmp
    {0,0,0,INFfloat,1,1},                               // _FCMPEXC@    fcmp
    {FLOATREGS_16,FLOATREGS_32,0,2},                    // _FNEG@       fneg
    {FLOATREGS_16,FLOATREGS_32,0,INFfloat,1,1},         // _FADD@       fadd
    {FLOATREGS_16,FLOATREGS_32,0,INFfloat,1,1},         // _FSUB@       fsub
#endif

    {mDX|mAX,mAX,0,INFfloat,1,1},                       // _DBLLNG@     dbllng
    {DOUBLEREGS_16,DOUBLEREGS_32,0,INFfloat,1,1},       // _LNGDBL@     lngdbl
    {mAX,mAX,0,INFfloat,1,1},                           // _DBLINT@     dblint
    {DOUBLEREGS_16,DOUBLEREGS_32,0,INFfloat,1,1},       // _INTDBL@     intdbl
    {mAX,mAX,0,INFfloat,1,1},                           // _DBLUNS@     dbluns
    {DOUBLEREGS_16,DOUBLEREGS_32,0,INFfloat,1,1},       // _UNSDBL@     unsdbl
#if TARGET_LINUX || TARGET_OSX || TARGET_FREEBSD || TARGET_SOLARIS
    {mDX|mAX,mAX,0,INF32|INFfloat,0,1},                 // _DBLULNG@    dblulng
#else
    {mDX|mAX,mAX,0,INFfloat,1,1},                       // _DBLULNG@    dblulng
#endif
#if TARGET_WINDOS
    {DOUBLEREGS_16,DOUBLEREGS_32,0,INFfloat,1,1},       // _ULNGDBL@    ulngdbl
#endif
    {FLOATREGS_16,FLOATREGS_32,0,INFfloat,1,1},         // _DBLFLT@     dblflt
    {DOUBLEREGS_16,DOUBLEREGS_32,0,INFfloat,1,1},       // _FLTDBL@     fltdbl

    {DOUBLEREGS_16,mDX|mAX,0,INFfloat,1,1},             // _DBLLLNG@
    {DOUBLEREGS_16,DOUBLEREGS_32,0,INFfloat,1,1},       // _LLNGDBL@
#if TARGET_LINUX || TARGET_OSX || TARGET_FREEBSD || TARGET_SOLARIS
    {DOUBLEREGS_16,mDX|mAX,0,INFfloat,2,2},             // _DBLULLNG@
#else
    {DOUBLEREGS_16,mDX|mAX,0,INFfloat,1,1},             // _DBLULLNG@
#endif
    {DOUBLEREGS_16,DOUBLEREGS_32,0,INFfloat,1,1},       // _ULLNGDBL@

    {0,0,0,2},                          // _DTST@       dtst
    {mES|mBX,mES|mBX,0,0},              // _HTOFPTR@    vptrfptr
    {mES|mBX,mES|mBX,0,0},              // _HCTOFPTR@   cvptrfptr
    {0,0,0,2},                          // _87TOPSW@    87topsw
    {mST0,mST0,0,INFfloat,1,0},         // _FLTTO87@    fltto87
    {mST0,mST0,0,INFfloat,1,0},         // _DBLTO87@    dblto87
    {mAX,mAX,0,2},                      // _DBLINT87@   dblint87
    {mDX|mAX,mAX,0,2},                  // _DBLLNG87@   dbllng87
    {0,0,0,2},                          // _FTST@
    {mPSW,mPSW,0,INFfloat,0,2},         // _FCOMPP@
    {mPSW,mPSW,0,2},                    // _FTEST@
    {mPSW,mPSW,0,2},                    // _FTEST0@
    {mST0,mST0,0,INFfloat,1,1},         // _FDIV@

    {mST01,mST01,0,INF32|INFfloat,3,5}, // _Cmul
    {mST01,mST01,0,INF32|INFfloat,0,2}, // _Cdiv
    {mPSW, mPSW, 0,INF32|INFfloat,0,4}, // _Ccmp

    {mST0,mST0,0,INF32|INFfloat,2,1},   // _U64_LDBL
    {0,mDX|mAX,0,INF32|INFfloat,1,2},   // __LDBLULLNG
  };

  if (!clib_inited)                             /* if not initialized   */
  {
        assert(sizeof(lib) / sizeof(lib[0]) == CLIBMAX);
        assert(sizeof(info) / sizeof(info[0]) == CLIBMAX);
        for (i = 0; i < CLIBMAX; i++)
        {   lib[i].Stype = tsclib;
#if MARS
            lib[i].Sxtrnnum = 0;
            lib[i].Stypidx = 0;
#endif
        }

        if (I32)
        {   /* Adjust table for 386     */
            lib[CLIBdbllng].Sregsaved  = Z(DOUBLEREGS_32);
            lib[CLIBlngdbl].Sregsaved  = Z(DOUBLEREGS_32);
            lib[CLIBdblint].Sregsaved  = Z(DOUBLEREGS_32);
            lib[CLIBintdbl].Sregsaved  = Z(DOUBLEREGS_32);
#if TARGET_WINDOS
            lib[CLIBfneg].Sregsaved    = Z(FLOATREGS_32);
            lib[CLIBdneg].Sregsaved    = Z(DOUBLEREGS_32);
            lib[CLIBdbluns].Sregsaved  = Z(DOUBLEREGS_32);
            lib[CLIBunsdbl].Sregsaved  = Z(DOUBLEREGS_32);
            lib[CLIBdblulng].Sregsaved = Z(DOUBLEREGS_32);
            lib[CLIBulngdbl].Sregsaved = Z(DOUBLEREGS_32);
#endif
            lib[CLIBdblflt].Sregsaved  = Z(DOUBLEREGS_32);
            lib[CLIBfltdbl].Sregsaved  = Z(DOUBLEREGS_32);

            lib[CLIBdblllng].Sregsaved = Z(DOUBLEREGS_32);
            lib[CLIBllngdbl].Sregsaved = Z(DOUBLEREGS_32);
            lib[CLIBdblullng].Sregsaved = Z(DOUBLEREGS_32);
            lib[CLIBullngdbl].Sregsaved = Z(DOUBLEREGS_32);
        }
        clib_inited++;
  }
#undef Z

  assert(clib < CLIBMAX);
  s = &lib[clib];
  assert(I32 || !(info[clib].flags & INF32));
  cpop = CNIL;
  c = getregs((~s->Sregsaved & (mES | mBP | ALLREGS)) & ~keepmask); // mask of regs destroyed
  keepmask &= ~s->Sregsaved;
  int npushed = 0;
  while (keepmask)
  {     unsigned keepreg;

        if (keepmask & (mBP|ALLREGS))
        {       keepreg = findreg(keepmask & (mBP|ALLREGS));
                c = gen1(c,0x50 + keepreg);             /* PUSH keepreg */
                cpop = cat(gen1(CNIL,0x58 + keepreg),cpop);     // POP keepreg
                keepmask &= ~mask[keepreg];
                npushed++;
        }
        if (keepmask & mES)
        {       c = gen1(c,0x06);                       /* PUSH ES      */
                cpop = cat(gen1(CNIL,0x07),cpop);       /* POP ES       */
                keepmask &= ~mES;
                npushed++;
        }
  }

    c = cat(c, save87regs(info[clib].push87));
    for (i = 0; i < info[clib].push87; i++)
        c = cat(c, push87());

    for (i = 0; i < info[clib].pop87; i++)
        pop87();

  if (config.target_cpu >= TARGET_80386 && clib == CLIBlmul && !I32)
  {     static char lmul[] = {
            0x66,0xc1,0xe1,0x10,        // shl  ECX,16
            0x8b,0xcb,                  // mov  CX,BX           ;ECX = CX,BX
            0x66,0xc1,0xe0,0x10,        // shl  EAX,16
            0x66,0x0f,0xac,0xd0,0x10,   // shrd EAX,EDX,16      ;EAX = DX,AX
            0x66,0xf7,0xe1,             // mul  ECX
            0x66,0x0f,0xa4,0xc2,0x10,   // shld EDX,EAX,16      ;DX,AX = EAX
        };

        c = genasm(c,lmul,sizeof(lmul));
  }
  else
  {     makeitextern(s);
        int nalign = 0;
        if (STACKALIGN == 16)
        {   // Align the stack (assume no args on stack)
            int npush = npushed * REGSIZE + stackpush;
            if (npush & (STACKALIGN - 1))
            {   nalign = STACKALIGN - (npush & (STACKALIGN - 1));
                c = genc2(c,0x81,modregrm(3,5,SP),nalign); // SUB ESP,nalign
            }
        }
        c = gencs(c,(LARGECODE) ? 0x9A : 0xE8,0,FLfunc,s);      // CALL s
        if (nalign)
            c = genc2(c,0x81,modregrm(3,0,SP),nalign); // ADD ESP,nalign
        calledafunc = 1;

        if (!I32 &&                                     // bug in Optlink
            config.flags3 & CFG3wkfloat &&
            (info[clib].flags & (INFfloat | INFwkdone)) == INFfloat)
        {   info[clib].flags |= INFwkdone;
            makeitextern(rtlsym[RTLSYM_INTONLY]);
            obj_wkext(s,rtlsym[RTLSYM_INTONLY]);
        }
  }
  if (!I32)
        stackpush -= info[clib].pop;
  retregs = I32 ? info[clib].retregs32 : info[clib].retregs16;
  return cat(cat(c,cpop),fixresult(e,retregs,pretregs));
}


/*******************************
 * Generate code sequence for function call.
 */

code *cdfunc(elem *e,regm_t *pretregs)
{ unsigned numpara = 0;
  unsigned stackpushsave;
  unsigned preg;
  regm_t keepmsk;
  unsigned numalign = 0;
  code *c;

  //printf("cdfunc()\n"); elem_print(e);
  assert(e);
  stackpushsave = stackpush;            /* so we can compute # of parameters */
  cgstate.stackclean++;
  c = CNIL;
  keepmsk = 0;
  if (OTbinary(e->Eoper))               // if parameters
  {     unsigned stackalign = REGSIZE;
        elem *ep;
        elem *en;
        regm_t retregs;
        tym_t tyf;

        if (I32)
        {
            tyf = tybasic(e->E1->Ety);

            // First compute numpara, the total pushed on the stack
            switch (tyf)
            {   case TYf16func:
                    stackalign = 2;
                    goto Ldefault;
                case TYmfunc:
                case TYjfunc:
                    // last parameter goes into register
                    for (ep = e->E2; ep->Eoper == OPparam; ep = ep->E2)
                    {
                        numpara += paramsize(ep->E1,stackalign);
                    }
                    if (tyf == TYjfunc &&
                        // This must match type_jparam()
                        !(tyjparam(ep->Ety) ||
                          ((tybasic(ep->Ety) == TYstruct || tybasic(ep->Ety) == TYarray) && ep->Enumbytes <= intsize && ep->Enumbytes != 3 && ep->Enumbytes)
                         )
                        )
                    {
                        numpara += paramsize(ep,stackalign);
                    }
                    break;
                default:
                Ldefault:
                    numpara += paramsize(e->E2,stackalign);
                    break;
            }
            assert((numpara & (REGSIZE - 1)) == 0);
            assert((stackpush & (REGSIZE - 1)) == 0);

            /* Adjust start of the stack so after all args are pushed,
             * the stack will be aligned.
             */
            if (STACKALIGN == 16 && (numpara + stackpush) & (STACKALIGN - 1))
            {
                numalign = STACKALIGN - ((numpara + stackpush) & (STACKALIGN - 1));
                c = genc2(NULL,0x81,modregrm(3,5,SP),numalign); // SUB ESP,numalign
                c = genadjesp(c, numalign);
                stackpush += numalign;
                stackpushsave += numalign;
            }

            switch (tyf)
            {   case TYf16func:
                    stackalign = 2;
                    break;
                case TYmfunc:   // last parameter goes into ECX
                    preg = CX;
                    goto L1;
                case TYjfunc:   // last parameter goes into EAX
                    preg = AX;
                    goto L1;

                L1:
                    for (ep = e->E2; ep->Eoper == OPparam; ep = en)
                    {
                        c = cat(c,params(ep->E1,stackalign));
                        en = ep->E2;
                        freenode(ep);
                        ep = en;
                    }
                    if (tyf == TYjfunc &&
                        // This must match type_jparam()
                        !(tyjparam(ep->Ety) ||
                          ((tybasic(ep->Ety) == TYstruct || tybasic(ep->Ety) == TYarray) && ep->Enumbytes <= intsize && ep->Enumbytes != 3 && ep->Enumbytes)
                         )
                        )
                    {
                        c = cat(c,params(ep,stackalign));
                        goto Lret;
                    }
                    keepmsk = mask[preg];
                    retregs = keepmsk;
                    if (ep->Eoper == OPstrthis)
                    {   code *c1;
                        code *c2;
                        unsigned np;

                        c1 = getregs(retregs);
                        // LEA preg,np[ESP]
                        np = stackpush - ep->EV.Vuns;   // stack delta to parameter
                        c2 = genc1(CNIL,0x8D,modregrm(2,preg,4),FLconst,np);
                        c2->Isib = modregrm(0,4,SP);
                        c = cat3(c,c1,c2);
                    }
                    else
                    {   code *cp = codelem(ep,&retregs,FALSE);
                        c = cat(c,cp);
                    }
                    goto Lret;
            }
        }
        c = cat(c, params(e->E2,stackalign));   // push parameters
    }
    else
    {
        /* Adjust start of the stack so
         * the stack will be aligned.
         */
        if (STACKALIGN == 16 && (stackpush) & (STACKALIGN - 1))
        {
            numalign = STACKALIGN - ((stackpush) & (STACKALIGN - 1));
            c = genc2(NULL,0x81,modregrm(3,5,SP),numalign); // SUB ESP,numalign
            c = genadjesp(c, numalign);
            stackpush += numalign;
            stackpushsave += numalign;
        }

    }
Lret:
    cgstate.stackclean--;
    if (I32)
    {
        if (numpara != stackpush - stackpushsave)
            printf("numpara = %d, stackpush = %d, stackpushsave = %d\n", numpara, stackpush, stackpushsave);
        assert(numpara == stackpush - stackpushsave);
    }
    else
        numpara = stackpush - stackpushsave;
    return cat(c,funccall(e,numpara,numalign,pretregs,keepmsk));
}

/***********************************
 */

code *cdstrthis(elem *e,regm_t *pretregs)
{
    code *c1;
    code *c2;
    unsigned np;
    unsigned reg;

    assert(tysize(e->Ety) == REGSIZE);
    reg = findreg(*pretregs & allregs);
    c1 = getregs(mask[reg]);
    // LEA reg,np[ESP]
    np = stackpush - e->EV.Vuns;        // stack delta to parameter
    c2 = genc1(CNIL,0x8D,modregrm(2,reg,4),FLconst,np);
    c2->Isib = modregrm(0,4,SP);
    return cat3(c1,c2,fixresult(e,mask[reg],pretregs));
}

/******************************
 * Call function. All parameters are pushed onto the stack, numpara gives
 * the size of them all.
 */

STATIC code * funccall(elem *e,unsigned numpara,unsigned numalign,regm_t *pretregs,regm_t keepmsk)
{
    elem *e1;
    code *c,*ce,cs;
    tym_t tym1;
    char farfunc;
    regm_t retregs;
    symbol *s;

    //printf("funccall(e = %p, *pretregs = x%x, numpara = %d, numalign = %d)\n",e,*pretregs,numpara,numalign);
    calledafunc = 1;
    /* Determine if we need frame for function prolog/epilog    */
#if TARGET_WINDOS
    if (config.memmodel == Vmodel)
    {
        if (tyfarfunc(funcsym_p->ty()))
            needframe = TRUE;
    }
#endif
    e1 = e->E1;
    tym1 = tybasic(e1->Ety);
    farfunc = tyfarfunc(tym1) || tym1 == TYifunc;
    if (e1->Eoper == OPvar)
    {   /* Call function directly       */
        code *c1;

#ifdef DEBUG
        if (!tyfunc(tym1)) WRTYxx(tym1);
#endif
        assert(tyfunc(tym1));
        s = e1->EV.sp.Vsym;
        if (s->Sflags & SFLexit)
            c = NULL;
        else
            c = save87();               // assume 8087 regs are all trashed
        if (s->Sflags & SFLexit)
            // Function doesn't return, so don't worry about registers
            // it may use
            c1 = NULL;
        else if (!tyfunc(s->ty()) || !(config.flags4 & CFG4optimized))
            // so we can replace func at runtime
            c1 = getregs(~fregsaved & (mBP | ALLREGS | mES));
        else
            c1 = getregs(~s->Sregsaved & (mBP | ALLREGS | mES));
        if (strcmp(s->Sident,"alloca") == 0)
        {
#if 1
            s = rtlsym[RTLSYM_ALLOCA];
            makeitextern(s);
            c1 = cat(c1,getregs(mCX));
            c1 = genc(c1,0x8D,modregrm(2,CX,BPRM),FLallocatmp,0,0,0);  // LEA CX,&localsize[BP]
            usedalloca = 2;             // new way
#else
            usedalloca = 1;             // old way
#endif
        }
        if (sytab[s->Sclass] & SCSS)    // if function is on stack (!)
        {
            retregs = allregs & ~keepmsk;
            s->Sflags &= ~GTregcand;
            s->Sflags |= SFLread;
            ce = cat(c1,cdrelconst(e1,&retregs));
            if (farfunc)
                goto LF1;
            else
                goto LF2;
        }
        else
        {   int fl;

            fl = FLfunc;
            if (!tyfunc(s->ty()))
                fl = el_fl(e1);
            if (tym1 == TYifunc)
                c1 = gen1(c1,0x9C);                             // PUSHF
#if 0 && TARGET_LINUX
            if (s->Sfl == FLgot || s->Sfl == FLgotoff)
                fl = s->Sfl;
#endif
            ce = gencs(CNIL,farfunc ? 0x9A : 0xE8,0,fl,s);      // CALL extern
            ce->Iflags |= farfunc ? (CFseg | CFoff) : (CFselfrel | CFoff);
#if TARGET_LINUX
            if (s == tls_get_addr_sym)
            {   /* Append a NOP so GNU linker has patch room
                 */
                ce = gen1(ce, 0x90);            // NOP
                code_orflag(ce, CFvolatile);    // don't schedule it
            }
#endif
        }
        ce = cat(c1,ce);
  }
  else
  {     /* Call function via pointer    */
        elem *e11;
        tym_t e11ty;

#ifdef DEBUG
        if (e1->Eoper != OPind
                ) { WRFL((enum FL)el_fl(e1)); WROP(e1->Eoper); }
#endif
        c = save87();                   // assume 8087 regs are all trashed
        assert(e1->Eoper == OPind);
        e11 = e1->E1;
        e11ty = tybasic(e11->Ety);
        assert(I32 || (e11ty == (farfunc ? TYfptr : TYnptr)));

        /* if we can't use loadea()     */
        if ((EOP(e11) || e11->Eoper == OPconst) &&
            (e11->Eoper != OPind || e11->Ecount))
        {
            unsigned reg;

            retregs = allregs & ~keepmsk;
            cgstate.stackclean++;
            ce = scodelem(e11,&retregs,keepmsk,TRUE);
            cgstate.stackclean--;
            /* Kill registers destroyed by an arbitrary function call */
            ce = cat(ce,getregs((mBP | ALLREGS | mES) & ~fregsaved));
            if (e11ty == TYfptr)
            {   unsigned lsreg;
             LF1:
                reg = findregmsw(retregs);
                lsreg = findreglsw(retregs);
                floatreg = TRUE;                /* use float register   */
                reflocal = TRUE;
                ce = genc1(ce,0x89,             /* MOV floatreg+2,reg   */
                        modregrm(2,reg,BPRM),FLfltreg,REGSIZE);
                genc1(ce,0x89,                  /* MOV floatreg,lsreg   */
                        modregrm(2,lsreg,BPRM),FLfltreg,0);
                if (tym1 == TYifunc)
                    gen1(ce,0x9C);              // PUSHF
                genc1(ce,0xFF,                  /* CALL [floatreg]      */
                        modregrm(2,3,BPRM),FLfltreg,0);
            }
            else
            {
             LF2:
                reg = findreg(retregs);
                ce = gen2(ce,0xFF,modregrm(3,2,reg));   /* CALL reg     */
            }
        }
        else
        {
            if (tym1 == TYifunc)
                c = gen1(c,0x9C);               // PUSHF
                                                // CALL [function]
            cs.Iflags = 0;
            cgstate.stackclean++;
            ce = loadea(e11,&cs,0xFF,farfunc ? 3 : 2,0,keepmsk,(ALLREGS|mES|mBP) & ~fregsaved);
            cgstate.stackclean--;
            freenode(e11);
        }
        s = NULL;
  }
  c = cat(c,ce);
  freenode(e1);

  /* See if we will need the frame pointer.
     Calculate it here so we can possibly use BP to fix the stack.
   */
#if 0
  if (!needframe)
  {     SYMIDX si;

        /* If there is a register available for this basic block        */
        if (config.flags4 & CFG4optimized && (ALLREGS & ~regcon.used))
            ;
        else
        {
            for (si = 0; si < globsym.top; si++)
            {   symbol *s = globsym.tab[si];

                if (s->Sflags & GTregcand && type_size(s->Stype) != 0)
                {
                    if (config.flags4 & CFG4optimized)
                    {   /* If symbol is live in this basic block and    */
                        /* isn't already in a register                  */
                        if (s->Srange && vec_testbit(dfoidx,s->Srange) &&
                            s->Sfl != FLreg)
                        {   /* Then symbol must be allocated on stack */
                            needframe = TRUE;
                            break;
                        }
                    }
                    else
                    {   if (mfuncreg == 0)      /* if no registers left */
                        {   needframe = TRUE;
                            break;
                        }
                    }
                }
            }
        }
  }
#endif

    retregs = regmask(e->Ety, tym1);

    // If stack needs cleanup
    if (OTbinary(e->Eoper) && !typfunc(tym1) &&
      !(s && s->Sflags & SFLexit))
    {
        if (tym1 == TYhfunc)
        {   // Hidden parameter is popped off by the callee
            c = genadjesp(c, -4);
            stackpush -= 4;
            if (numpara + numalign > 4)
                c = genstackclean(c, numpara + numalign - 4, retregs);
        }
        else
            c = genstackclean(c,numpara + numalign,retregs);
    }
    else
    {
        c = genadjesp(c,-numpara);
        stackpush -= numpara;
        if (numalign)
            c = genstackclean(c,numalign,retregs);
    }

    /* Special handling for functions which return a floating point
       value in the top of the 8087 stack.
     */

    if (retregs & mST0)
    {
        if (*pretregs)                  // if we want the result
        {   //assert(stackused == 0);
            push87();                   // one item on 8087 stack
            return cat(c,fixresult87(e,retregs,pretregs));
        }
        else
            /* Pop unused result off 8087 stack */
            c = gen2(c,0xDD,modregrm(3,3,0));           /* FPOP         */
    }
    else if (retregs & mST01)
    {
        if (*pretregs)                  // if we want the result
        {   assert(stackused == 0);
            push87();
            push87();                   // two items on 8087 stack
            return cat(c,fixresult_complex87(e,retregs,pretregs));
        }
        else
        {
            // Pop unused result off 8087 stack
            c = gen2(c,0xDD,modregrm(3,3,0));           // FPOP
            c = gen2(c,0xDD,modregrm(3,3,0));           // FPOP
        }
    }

    return cat(c,fixresult(e,retregs,pretregs));
}

/***************************
 * Determine size of everything that will be pushed.
 */

targ_size_t paramsize(elem *e,unsigned stackalign)
{
    targ_size_t psize = 0;
    targ_size_t szb;

    while (e->Eoper == OPparam)         /* if more params               */
    {
        elem *e2 = e->E2;
        psize += paramsize(e->E1,stackalign);   // push them backwards
        e = e2;
    }
    tym_t tym = tybasic(e->Ety);
    if (tyscalar(tym))
        szb = size(tym);
    else if (tym == TYstruct)
        szb = e->Enumbytes;
    else
    {
#ifdef DEBUG
        WRTYxx(tym);
#endif
        assert(0);
    }
    psize += align(stackalign,szb);     /* align on word stack boundary */
    return psize;
}

/***************************
 * Generate code to push parameter list.
 * stackpush is incremented by stackalign for each PUSH.
 */

code *params(elem *e,unsigned stackalign)
{ code *c,*ce,cs;
  code *cp;
  unsigned reg;
  targ_size_t szb;                      // size before alignment
  targ_size_t sz;                       // size after alignment
  tym_t tym;
  regm_t retregs;
  elem *e1;
  elem *e2;
  symbol *s;
  int fl;

  //printf("params(e = %p, stackalign = %d)\n", e, stackalign);
  cp = NULL;
  stackchanged = 1;
  assert(e);
  while (e->Eoper == OPparam)           /* if more params               */
  {
        e2 = e->E2;
        cp = cat(cp,params(e->E1,stackalign));  // push them backwards
        freenode(e);
        e = e2;
  }
  //printf("params()\n"); elem_print(e);

  tym = tybasic(e->Ety);
  if (tyfloating(tym))
        obj_fltused();

  /* sz = number of bytes pushed        */
  if (tyscalar(tym))
        szb = size(tym);
  else if (tym == TYstruct)
        szb = e->Enumbytes;
  else
  {
#ifdef DEBUG
        WRTYxx(tym);
#endif
        assert(0);
  }
  sz = align(stackalign,szb);           /* align on word stack boundary */
  assert((sz & (stackalign - 1)) == 0); /* ensure that alignment worked */
  assert((sz & (REGSIZE - 1)) == 0);

  c = CNIL;
  cs.Iflags = 0;
  cs.Ijty = 0;
  switch (e->Eoper)
  {
#if SCPP
    case OPstrctor:
    {
        e1 = e->E1;
        c = docommas(&e1);              /* skip over any comma expressions */

        c = genc2(c,0x81,modregrm(3,5,SP),sz); /* SUB SP,sizeof(struct) */
        stackpush += sz;
        genadjesp(c,sz);

        // Find OPstrthis and set it to stackpush
        exp2_setstrthis(e1,NULL,stackpush,NULL);

        retregs = 0;
        ce = codelem(e1,&retregs,TRUE);
        goto L2;
    }
    case OPstrthis:
        // This is the parameter for the 'this' pointer corresponding to
        // OPstrctor. We push a pointer to an object that was already
        // allocated on the stack by OPstrctor.
    {   unsigned np;

        retregs = allregs;
        c = allocreg(&retregs,&reg,TYoffset);
        c = genregs(c,0x89,SP,reg);             // MOV reg,SP
        np = stackpush - e->EV.Vuns;            // stack delta to parameter
        c = genc2(c,0x81,modregrm(3,0,reg),np); // ADD reg,np
        if (sz > REGSIZE)
        {   c = gen1(c,0x16);                   // PUSH SS
            stackpush += REGSIZE;
        }
        c = gen1(c,0x50 + reg);                 // PUSH reg
        stackpush += REGSIZE;
        genadjesp(c,sz);
        ce = CNIL;
        goto L2;
    }
#endif
    case OPstrpar:
        {       code *cc,*c1,*c2,*c3;
                unsigned rm;
                unsigned seg;           // segment override prefix flags
                bool doneoff;
                unsigned pushsize = REGSIZE;
                unsigned op16 = 0;
                unsigned npushes;

                e1 = e->E1;
                if (sz == 0)
                {
                    ce = docommas(&e1); /* skip over any commas         */
                    goto L2;
                }
                if ((sz & 3) == 0 && (sz / REGSIZE) <= 4 && e1->Eoper == OPvar)
                {   freenode(e);
                    e = e1;
                    goto L1;
                }
                cc = docommas(&e1);     /* skip over any commas         */
                seg = 0;                /* assume no seg override       */
                retregs = sz ? IDXREGS : 0;
                doneoff = FALSE;
                if (I32 && sz & 2)      /* if odd number of words to push */
                {   pushsize = 2;
                    op16 = 1;
                }
                else if (!I32 && config.target_cpu >= TARGET_80386 && (sz & 3) == 0)
                {   pushsize = 4;       // push DWORDs at a time
                    op16 = 1;
                }
                npushes = sz / pushsize;
                switch (e1->Eoper)
                {   case OPind:
                        if (sz)
                        {   switch (tybasic(e1->E1->Ety))
                            {
                                case TYfptr:
                                case TYhptr:
                                    seg = CFes;
                                    retregs |= mES;
                                    break;
                                case TYsptr:
                                    if (config.wflags & WFssneds)
                                        seg = CFss;
                                    break;
                                case TYcptr:
                                    seg = CFcs;
                                    break;
                            }
                        }
                        c1 = codelem(e1->E1,&retregs,FALSE);
                        freenode(e1);
                        break;
                    case OPvar:
                        /* Symbol is no longer a candidate for a register */
                        e1->EV.sp.Vsym->Sflags &= ~GTregcand;

                        if (!e1->Ecount && npushes > 4)
                        {       /* Kludge to point at last word in struct. */
                                /* Don't screw up CSEs.                 */
                                e1->EV.sp.Voffset += sz - pushsize;
                                doneoff = TRUE;
                        }
                        //if (LARGEDATA) /* if default isn't DS */
                        {   static unsigned segtocf[4] = { CFes,CFcs,CFss,0 };
                            unsigned s;
                            int fl;

                            fl = el_fl(e1);
                            if (fl == FLfardata)
                            {   seg = CFes;
                                assert(!TARGET_FLAT);
                                retregs |= mES;
                            }
                            else
                            {
                                s = segfl[fl];
                                assert(s < 4);
                                seg = segtocf[s];
                                if (seg == CFss && !(config.wflags & WFssneds))
                                    seg = 0;
                            }
                        }
#if !TARGET_FLAT
                        if (e1->Ety & mTYfar)
                        {   seg = CFes;
                            retregs |= mES;
                        }
#endif
                        c1 = cdrelconst(e1,&retregs);
                        /* Reverse the effect of the previous add       */
                        if (doneoff)
                                e1->EV.sp.Voffset -= sz - pushsize;
                        freenode(e1);
                        break;
                    case OPstreq:
                    //case OPcond:
                        if (!(config.exe & EX_flat))
                        {   seg = CFes;
                            retregs |= mES;
                        }
                        c1 = codelem(e1,&retregs,FALSE);
                        break;
                    default:
#ifdef DEBUG
                        elem_print(e1);
#endif
                        assert(0);
                }
                reg = findreglsw(retregs);
                rm = I32 ? regtorm32[reg] : regtorm[reg];
                if (op16)
                    seg |= CFopsize;            // operand size
                if (npushes <= 4)
                {
                    assert(!doneoff);
                    for (c2 = CNIL; npushes > 1; npushes--)
                    {   c2 = genc1(c2,0xFF,modregrm(2,6,rm),FLconst,pushsize * (npushes - 1));  // PUSH [reg]
                        code_orflag(c2,seg);
                        genadjesp(c2,pushsize);
                    }
                    c3 = gen2(CNIL,0xFF,modregrm(0,6,rm));      // PUSH [reg]
                    c3->Iflags |= seg;
                    genadjesp(c3,pushsize);
                    ce = cat4(cc,c1,c2,c3);
                }
                else if (sz)
                {   int size;

                    c2 = getregs_imm(mCX | retregs);
                                                        /* MOV CX,sz/2  */
                    c2 = movregconst(c2,CX,npushes,0);
                    if (!doneoff)
                    {   /* This disgusting thing should be done when    */
                        /* reg is loaded. Too lazy to fix it now.       */
                                                        /* ADD reg,sz-2 */
                        c2 = genc2(c2,0x81,modregrm(3,0,reg),sz-pushsize);
                    }
                    c3 = gen2(CNIL,0xFF,modregrm(0,6,rm));      // PUSH [reg]
                    c3->Iflags |= seg | CFtarg2;
                    genc2(c3,0x81,modregrm(3,5,reg),pushsize);  // SUB reg,2
                    size = ((seg & CFSEG) ? -8 : -7) - op16;
                    if (code_next(c3)->Iop != 0x81)
                        size++;
                    //genc2(c3,0xE2,0,size);    // LOOP .-7 or .-8
                    genjmp(c3,0xE2,FLcode,(block *)c3);         // LOOP c3
                    regimmed_set(CX,0);
                    genadjesp(c3,sz);
                    ce = cat4(cc,c1,c2,c3);
                }
                else
                    ce = cat(cc,c1);
                stackpush += sz;
                goto L2;
        }
    case OPind:
        if (!e->Ecount)                         /* if *e1       */
        {       if (sz <= REGSIZE)
                {   // Watch out for single byte quantities being up
                    // against the end of a segment or in memory-mapped I/O
                    if (!(config.exe & EX_flat) && szb == 1)
                        break;
                    goto L1;            // can handle it with loadea()
                }

                // Avoid PUSH MEM on the Pentium when optimizing for speed
                if (config.flags4 & CFG4speed &&
                    (config.target_cpu >= TARGET_80486 &&
                     config.target_cpu <= TARGET_PentiumMMX) &&
                    sz <= 2 * REGSIZE &&
                    !tyfloating(tym))
                    break;

                if (tym == TYldouble || tym == TYildouble || tycomplex(tym))
                    break;
                if (I32)
                {
                    assert(sz == REGSIZE * 2);
                    ce = loadea(e,&cs,0xFF,6,REGSIZE,0,0); /* PUSH EA+4 */
                    ce = genadjesp(ce,REGSIZE);
                }
                else
                {
                    if (sz == DOUBLESIZE)
                    {   ce = loadea(e,&cs,0xFF,6,DOUBLESIZE - REGSIZE,0,0); /* PUSH EA+6        */
                        cs.IEVoffset1 -= REGSIZE;
                        gen(ce,&cs);                    /* PUSH EA+4    */
                        ce = genadjesp(ce,REGSIZE);
                        getlvalue_lsw(&cs);
                        gen(ce,&cs);                    /* PUSH EA+2    */
                    }
                    else /* TYlong */
                        ce = loadea(e,&cs,0xFF,6,REGSIZE,0,0); /* PUSH EA+2 */
                    ce = genadjesp(ce,REGSIZE);
                }
                stackpush += sz;
                getlvalue_lsw(&cs);
                gen(ce,&cs);                            /* PUSH EA      */
                ce = genadjesp(ce,REGSIZE);
                goto L2;
        }
        break;
    case OPptrlptr:
        if (!e->Ecount)                         /* if (far *)e1 */
        {
            int segreg;
            tym_t tym1;

            e1 = e->E1;
            tym1 = tybasic(e1->Ety);
            /* BUG: what about pointers to functions?   */
            segreg = (tym1 == TYnptr) ? 3<<3 :
                     (tym1 == TYcptr) ? 1<<3 : 2<<3;
            if (I32 && stackalign == 2)
                c = gen1(c,0x66);               /* push a word          */
            c = gen1(c,0x06 + segreg);          /* PUSH SEGREG          */
            if (I32 && stackalign == 2)
                code_orflag(c,CFopsize);        // push a word
            c = genadjesp(c,stackalign);
            stackpush += stackalign;
            ce = params(e1,stackalign);
            goto L2;
        }
        break;
    case OPrelconst:
        /* Determine if we can just push the segment register           */
        /* Test size of type rather than TYfptr because of (long)(&v)   */
        s = e->EV.sp.Vsym;
        //if (sytab[s->Sclass] & SCSS && !I32)  // if variable is on stack
        //    needframe = TRUE;                 // then we need stack frame
#if !TARGET_FLAT
        if (tysize[tym] == tysize[TYfptr] &&
            (fl = s->Sfl) != FLfardata &&
            /* not a function that CS might not be the segment of       */
            (!((fl == FLfunc || s->ty() & mTYcs) &&
              (s->Sclass == SCcomdat || s->Sclass == SCextern || s->Sclass == SCinline || config.wflags & WFthunk)) ||
             (fl == FLfunc && config.exe == EX_DOSX)
            )
           )
        {
            stackpush += sz;
            c = gen1(c,0x06 +           /* PUSH SEGREG                  */
                    (((fl == FLfunc || s->ty() & mTYcs) ? 1 : segfl[fl]) << 3));
            c = genadjesp(c,REGSIZE);

            if (config.target_cpu >= TARGET_80286 && !e->Ecount)
            {   ce = getoffset(e,STACK);
                goto L2;
            }
            else
            {   c = cat(c,offsetinreg(e,&retregs));
                c = gen1(c,0x50+findreg(retregs)); /* PUSH reg          */
                genadjesp(c,REGSIZE);
            }
            goto ret;
        }
        if (config.target_cpu >= TARGET_80286 && !e->Ecount)
        {
            stackpush += sz;
            if (tysize[tym] == tysize[TYfptr])
            {   code *c1;

                /* PUSH SEG e   */
                c1 = gencs(CNIL,0x68,0,FLextern,s);
                c1->Iflags = CFseg;
                genadjesp(c1,REGSIZE);
                c = cat(c,c1);
            }
            ce = getoffset(e,STACK);
            goto L2;
        }
#endif
        break;                          /* else must evaluate expression */
    case OPvar:
    L1:
        if (0 && I32 && sz == 2)
        {   /* 32 bit code, but pushing 16 bit values anyway    */
            ce = loadea(e,&cs,0xFF,6,0,0,0);            /* PUSH EA      */
            // BUG: 0x66 fails with scheduler
            ce = cat(gen1(CNIL,0x66),ce);               /* 16 bit override */
            stackpush += sz;
            genadjesp(ce,sz);
        }
        else if (config.flags4 & CFG4speed &&
                 (config.target_cpu >= TARGET_80486 &&
                  config.target_cpu <= TARGET_PentiumMMX) &&
                 sz <= 2 * REGSIZE &&
                 !tyfloating(tym))
        {   // Avoid PUSH MEM on the Pentium when optimizing for speed
            break;
        }
        else
        {   int regsize = REGSIZE;
            unsigned flag = 0;

            if (!I32 && config.target_cpu >= TARGET_80386 && sz > 2 &&
                !e->Ecount)
            {   regsize = 4;
                flag |= CFopsize;
            }
            ce = loadea(e,&cs,0xFF,6,sz - regsize,RMload,0);    // PUSH EA+sz-2
            code_orflag(ce,flag);
            ce = genadjesp(ce,REGSIZE);
            stackpush += sz;
            while ((targ_int)(sz -= regsize) > 0)
            {   ce = cat(ce,loadea(e,&cs,0xFF,6,sz - regsize,RMload,0));
                code_orflag(ce,flag);
                ce = genadjesp(ce,REGSIZE);
            }
        }
    L2:
        freenode(e);
        c = cat(c,ce);
        goto ret;
    case OPconst:
    {   targ_int *pi;
        targ_short *ps;
        char pushi = 0;
        unsigned flag = 0;
        int i;
        int regsize = REGSIZE;
        targ_int value;

        if (tycomplex(tym))
            break;

        if (I32 && szb == 10)           // special case for long double constants
        {
            assert(sz == 12);
            value = ((unsigned short *)&e->EV.Vldouble)[4];
            stackpush += sz;
            ce = genadjesp(NULL,sz);
            for (i = 2; i >= 0; i--)
            {
                if (reghasvalue(allregs, value, &reg))
                    ce = gen1(ce,0x50 + reg);           // PUSH reg
                else
                    ce = genc2(ce,0x68,0,value);        // PUSH value
                value = ((unsigned *)&e->EV.Vldouble)[i - 1];
            }
            goto L2;
        }

        assert(sz <= LNGDBLSIZE);
        i = sz;
        if (I32 && i == 2)
            flag = CFopsize;

        if (config.target_cpu >= TARGET_80286)
//       && (e->Ecount == 0 || e->Ecount != e->Ecomsub))
        {   pushi = 1;
            if (!I32 && config.target_cpu >= TARGET_80386 && i >= 4)
            {   regsize = 4;
                flag = CFopsize;
            }
        }
        else if (i == REGSIZE)
            break;

        stackpush += sz;
        ce = genadjesp(NULL,sz);
        pi = (targ_long *) &e->EV.Vdouble;
        ps = (targ_short *) pi;
        i /= regsize;
        do
        {   code *cp;

            if (i)                      /* be careful not to go negative */
                i--;
            value = (regsize == 4) ? pi[i] : ps[i];
            if (pushi)
            {
                if (regsize == REGSIZE && reghasvalue(allregs,value,&reg))
                    goto Preg;
                ce = genc2(ce,(szb == 1) ? 0x6A : 0x68,0,value); // PUSH value
            }
            else
            {
                ce = regwithvalue(ce,allregs,value,&reg,0);
            Preg:
                ce = gen1(ce,0x50 + reg);               /* PUSH reg     */
            }
            code_orflag(ce,flag);                       /* operand size */
        } while (i);
        goto L2;
    }
    default:
        break;
  }
  retregs = tybyte(tym) ? BYTEREGS : allregs;
  if (tyfloating(tym))
  {     if (config.inline8087)
        {   code *c1,*c2;
            unsigned op;
            unsigned r;

            retregs = tycomplex(tym) ? mST01 : mST0;
            c = cat(c,codelem(e,&retregs,FALSE));
            stackpush += sz;
            c = genadjesp(c,sz);
            c = genc2(c,0x81,modregrm(3,5,SP),sz);      /* SUB SP,sz    */
            switch (tym)
            {
                case TYfloat:
                case TYifloat:
                case TYcfloat:
                    op = 0xD9;
                    r = 3;
                    break;

                case TYdouble:
                case TYidouble:
                case TYdouble_alias:
                case TYcdouble:
                    op = 0xDD;
                    r = 3;
                    break;

                case TYldouble:
                case TYildouble:
                case TYcldouble:
                    op = 0xDB;
                    r = 7;
                    break;

                default:
                    assert(0);
            }
            if (I32)
            {
                c1 = NULL;
                c2 = NULL;
                if (tycomplex(tym))
                {
                    // FSTP sz/2[ESP]
                    c2 = genc1(CNIL,op,modregrm(2,r,4),FLconst,sz/2);
                    c2->Isib = modregrm(0,4,SP);
                    pop87();
                }
                pop87();
                c2 = gen2sib(c2,op,modregrm(0,r,4),modregrm(0,4,SP));   // FSTP [ESP]
            }
            else
            {
                retregs = IDXREGS;                      /* get an index reg */
                c1 = allocreg(&retregs,&reg,TYoffset);
                c1 = genregs(c1,0x89,SP,reg);           /* MOV reg,SP    */
                pop87();
                c2 = gen2(CNIL,op,modregrm(0,r,regtorm[reg]));          // FSTP [reg]
            }
            if (LARGEDATA)
                c2->Iflags |= CFss;     /* want to store into stack     */
            genfwait(c2);               // FWAIT
            c = cat3(c,c1,c2);
            goto ret;
        }
        else if (!I32 && (tym == TYdouble || tym == TYdouble_alias))
            retregs = mSTACK;
  }
#if LONGLONG
  else if (!I32 && sz == 8)             // if long long
        retregs = mSTACK;
#endif
  c = cat(c,scodelem(e,&retregs,0,TRUE));
  if (retregs != mSTACK)                /* if stackpush not already inc'd */
      stackpush += sz;
  if (sz <= REGSIZE)
  {
        c = gen1(c,0x50+findreg(retregs));      /* PUSH reg             */
        genadjesp(c,REGSIZE);
  }
  else if (sz == REGSIZE * 2)
  {     c = gen1(c,0x50+findregmsw(retregs)); /* PUSH msreg             */
        gen1(c,0x50+findreglsw(retregs));         /* PUSH lsreg         */
        genadjesp(c,sz);
  }
ret:
  return cat(cp,c);
}

/*******************************
 * Get offset portion of e, and store it in an index
 * register. Return mask of index register in *pretregs.
 */

code *offsetinreg( elem *e, regm_t *pretregs)
{   regm_t retregs;
    code *c;
    unsigned reg;

    retregs = mLSW;                     /* want only offset     */
    if (e->Ecount && e->Ecount != e->Ecomsub)
    {   unsigned i;
        regm_t rm;

        rm = retregs & regcon.cse.mval & ~regcon.cse.mops & ~regcon.mvar; /* possible regs */
        for (i = 0; rm; i++)
        {       if (mask[i] & rm && regcon.cse.value[i] == e)
                {   reg = i;
                    *pretregs = mask[i];
                    c = getregs(*pretregs);
                    goto L3;
                }
                rm &= ~mask[i];
        }
    }

    *pretregs = retregs;
    c = allocreg(pretregs,&reg,TYoffset);
    c = cat(c,getoffset(e,reg));
L3:
    cssave(e,*pretregs,FALSE);
    freenode(e);
    return c;
}

/******************************
 * Generate code to load data into registers.
 */

code *loaddata(elem *e,regm_t *pretregs)
{ unsigned reg,nreg,op,sreg;
  tym_t tym;
  int sz;
  code *c,*ce,cs;
  regm_t flags,forregs,regm;

#ifdef DEBUG
  if (debugw)
        printf("loaddata(e = x%p,*pretregs = x%x)\n",e,*pretregs);
  //elem_print(e);
#endif
  assert(e);
  elem_debug(e);
  if (*pretregs == 0)
        return CNIL;
  tym = tybasic(e->Ety);
  if (tym == TYstruct)
        return cdrelconst(e,pretregs);
  if (tyfloating(tym))
  {     obj_fltused();
        if (config.inline8087)
        {   if (*pretregs & mST0)
                return load87(e,0,pretregs,NULL,-1);
            else if (tycomplex(tym))
                return cload87(e, pretregs);
        }
  }
  sz = tysize[tym];
  cs.Iflags = 0;
  cs.Ijty = 0;
  if (*pretregs == mPSW)
  {
        regm = allregs;
        if (e->Eoper == OPconst)
        {       /* TRUE:        OR SP,SP        (SP is never 0)         */
                /* FALSE:       CMP SP,SP       (always equal)          */
                c = genregs(CNIL,(boolres(e)) ? 0x09 : 0x39,SP,SP);
        }
        else if (sz <= REGSIZE)
        {
            if (I32 && (tym == TYfloat || tym == TYifloat))
            {   c = allocreg(&regm,&reg,TYoffset);      /* get a register */
                ce = loadea(e,&cs,0x8B,reg,0,0,0);      // MOV reg,data
                c = cat(c,ce);
                ce = gen2(CNIL,0xD1,modregrm(3,4,reg)); /* SHL reg,1      */
                c = cat(c,ce);
            }
            else
            {   cs.IFL2 = FLconst;
                cs.IEV2.Vint = 0;
                op = (sz == 1) ? 0x80 : 0x81;
                c = loadea(e,&cs,op,7,0,0,0);           /* CMP EA,0     */

                // Convert to TEST instruction if EA is a register
                // (to avoid register contention on Pentium)
                if ((c->Iop & 0xFE) == 0x38 &&
                    (c->Irm & modregrm(3,0,0)) == modregrm(3,0,0)
                   )
                {   c->Iop = (c->Iop & 1) | 0x84;
                    c->Irm = (c->Irm & modregrm(3,0,7)) | modregrm(0,c->Irm & 7,0);
                }
            }
        }
        else if (sz < 8)
        {
            c = allocreg(&regm,&reg,TYoffset);          /* get a register */
            if (I32)                                    // it's a 48 bit pointer
                ce = loadea(e,&cs,0x0FB7,reg,REGSIZE,0,0); /* MOVZX reg,data+4 */
            else
            {   ce = loadea(e,&cs,0x8B,reg,REGSIZE,0,0); /* MOV reg,data+2 */
                if (tym == TYfloat || tym == TYifloat)  // dump sign bit
                    gen2(ce,0xD1,modregrm(3,4,reg));    /* SHL reg,1      */
            }
            c = cat(c,ce);
            ce = loadea(e,&cs,0x0B,reg,0,regm,0);       /* OR reg,data */
            c = cat(c,ce);
        }
        else if (sz == 8)
        {   code *c1;
            int i;

            c = allocreg(&regm,&reg,TYoffset);  /* get a register */
            i = sz - REGSIZE;
            ce = loadea(e,&cs,0x8B,reg,i,0,0);  /* MOV reg,data+6 */
            if (tyfloating(tym))                // TYdouble or TYdouble_alias
                gen2(ce,0xD1,modregrm(3,4,reg));        // SHL reg,1
            c = cat(c,ce);

            while ((i -= REGSIZE) >= 0)
            {
                c1 = loadea(e,&cs,0x0B,reg,i,regm,0);   // OR reg,data+i
                if (i == 0)
                    c1->Iflags |= CFpsw;                // need the flags on last OR
                c = cat(c,c1);
            }
        }
        else if (sz == LNGDBLSIZE)                      // TYldouble
            return load87(e,0,pretregs,NULL,-1);
        else
            assert(0);
        return c;
  }
  /* not for flags only */
  flags = *pretregs & mPSW;             /* save original                */
  forregs = *pretregs & (mBP | ALLREGS | mES);
  if (*pretregs & mSTACK)
        forregs |= DOUBLEREGS;
  if (e->Eoper == OPconst)
  {     regm_t save;

        if (sz == REGSIZE && reghasvalue(forregs,e->EV.Vint,&reg))
            forregs = mask[reg];

        save = regcon.immed.mval;
        c = allocreg(&forregs,&reg,tym);        /* allocate registers   */
        regcon.immed.mval = save;               // KLUDGE!
        if (sz <= REGSIZE)
        {
            if (sz == 1)
                flags |= 1;
            else if (I32 && sz == SHORTSIZE &&
                     !(mask[reg] & regcon.mvar) &&
                     !(config.flags4 & CFG4speed)
                    )
                flags |= 2;
            ce = movregconst(CNIL,reg,e->EV.Vint,flags);
            flags = 0;                          // flags are already set
        }
        else if (sz < 8)        // far pointers, longs for 16 bit targets
        {
            targ_int msw,lsw;
            regm_t mswflags;

            msw = I32   ? e->EV.Vfp.Vseg
                        : (e->EV.Vulong >> 16);
            lsw = e->EV.Vfp.Voff;
            mswflags = 0;
            if (forregs & mES)
            {
                ce = movregconst(CNIL,reg,msw,0);       // MOV reg,segment
                genregs(ce,0x8E,0,reg);                 // MOV ES,reg
                msw = lsw;                              // MOV reg,offset
            }
            else
            {
                sreg = findreglsw(forregs);
                ce = movregconst(CNIL,sreg,lsw,0);
                reg = findregmsw(forregs);
                /* Decide if we need to set flags when we load msw      */
                if (flags && (msw && msw|lsw || !msw && !(msw|lsw)))
                {   mswflags = mPSW;
                    flags = 0;
                }
            }
            ce = movregconst(ce,reg,msw,mswflags);
        }
        else if (sz == 8)
        {
            if (I32)
            {   targ_long *p = (targ_long *) &e->EV.Vdouble;
                ce = movregconst(CNIL,findreglsw(forregs),p[0],0);
                ce = movregconst(ce,findregmsw(forregs),p[1],0);
            }
            else
            {   targ_short *p = (targ_short *) &e->EV.Vdouble;

                assert(reg == AX);
                ce = movregconst(CNIL,AX,p[3],0);       /* MOV AX,p[3]  */
                ce = movregconst(ce,DX,p[0],0);
                ce = movregconst(ce,CX,p[1],0);
                ce = movregconst(ce,BX,p[2],0);
            }
        }
        else
                assert(0);
        c = cat(c,ce);
  }
  else
  {
    // See if we can use register that parameter was passed in
    if (regcon.params && e->EV.sp.Vsym->Sclass == SCfastpar &&
        regcon.params & mask[e->EV.sp.Vsym->Spreg] &&
        !(e->Eoper == OPvar && e->EV.sp.Voffset > 0) && // Must be at the base of that variable
        sz <= REGSIZE)                  // make sure no 'paint' to a larger size happened
    {
        reg = e->EV.sp.Vsym->Spreg;
        forregs = mask[reg];
        mfuncreg &= ~forregs;
        regcon.used |= forregs;
        return fixresult(e,forregs,pretregs);
    }

    c = allocreg(&forregs,&reg,tym);            /* allocate registers   */

    if (sz == 1)
    {   regm_t nregm;

#ifdef DEBUG
        if (!(forregs & BYTEREGS))
        {       elem_print(e);
                printf("forregs = x%x\n",forregs);
        }
#endif
        assert(forregs & BYTEREGS);
        if (I32)
            c = cat(c,loadea(e,&cs,0x8A,reg,0,0,0));    // MOV regL,data
        else
        {   nregm = tyuns(tym) ? BYTEREGS : mAX;
            if (*pretregs & nregm)
                nreg = reg;                     /* already allocated    */
            else
                c = cat(c,allocreg(&nregm,&nreg,tym));
            ce = loadea(e,&cs,0x8A,nreg,0,0,0); /* MOV nregL,data       */
            c = cat(c,ce);
            if (reg != nreg)
            {   genmovreg(c,reg,nreg);          /* MOV reg,nreg         */
                cssave(e,mask[nreg],FALSE);
            }
        }
    }
    else if (sz <= REGSIZE)
    {
        ce = loadea(e,&cs,0x8B,reg,0,RMload,0); // MOV reg,data
        c = cat(c,ce);
    }
    else if (sz <= 2 * REGSIZE && forregs & mES)
    {
        ce = loadea(e,&cs,0xC4,reg,0,0,mES);    /* LES data             */
        c = cat(c,ce);
    }
    else if (sz <= 2 * REGSIZE)
    {
        if (I32 && sz == 8 &&
            (*pretregs & (mSTACK | mPSW)) == mSTACK)
        {   int i;

            assert(0);
            /* Note that we allocreg(DOUBLEREGS) needlessly     */
            stackchanged = 1;
            i = DOUBLESIZE - REGSIZE;
            do
            {   c = cat(c,loadea(e,&cs,0xFF,6,i,0,0)); /* PUSH EA+i     */
                c = genadjesp(c,REGSIZE);
                stackpush += REGSIZE;
                i -= REGSIZE;
            }
            while (i >= 0);
            return c;
        }

        reg = findregmsw(forregs);
        ce = loadea(e,&cs,0x8B,reg,REGSIZE,forregs,0); /* MOV reg,data+2 */
        if (I32 && sz == REGSIZE + 2)
            ce->Iflags |= CFopsize;                     /* seg is 16 bits */
        c = cat(c,ce);
        reg = findreglsw(forregs);
        ce = loadea(e,&cs,0x8B,reg,0,forregs,0);
        c = cat(c,ce);
    }
    else if (sz >= 8)
    {
        code *c1,*c2,*c3;

        assert(!I32);
        if ((*pretregs & (mSTACK | mPSW)) == mSTACK)
        {   int i;

            /* Note that we allocreg(DOUBLEREGS) needlessly     */
            stackchanged = 1;
            i = sz - REGSIZE;
            do
            {   c = cat(c,loadea(e,&cs,0xFF,6,i,0,0)); /* PUSH EA+i     */
                c = genadjesp(c,REGSIZE);
                stackpush += REGSIZE;
                i -= REGSIZE;
            }
            while (i >= 0);
            return c;
        }
        else
        {
            assert(reg == AX);
            ce = loadea(e,&cs,0x8B,AX,6,0,0);           /* MOV AX,data+6 */
            c1 = loadea(e,&cs,0x8B,BX,4,mAX,0);         /* MOV BX,data+4 */
            c2 = loadea(e,&cs,0x8B,CX,2,mAX|mBX,0);     /* MOV CX,data+2 */
            c3 = loadea(e,&cs,0x8B,DX,0,mAX|mCX|mCX,0); /* MOV DX,data  */
            c = cat6(c,ce,c1,c2,c3,CNIL);
        }
    }
    else
        assert(0);
  }
  /* Flags may already be set   */
  *pretregs &= flags | ~mPSW;
  c = cat(c,fixresult(e,forregs,pretregs));
  return c;
}

#endif // SPP
