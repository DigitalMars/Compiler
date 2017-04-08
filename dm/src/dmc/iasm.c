/**
 * Implementation of the
 * $(LINK2 http://www.digitalmars.com/download/freecompiler.html, Digital Mars C/C++ Compiler).
 *
 * Copyright:   Copyright (c) 1992-1998 by Symantec, All Rights Reserved
 *              Copyright (c) 2000-2017 by Digital Mars, All Rights Reserved
 * Authors:     Mike Cote, John Micco, $(LINK2 http://www.digitalmars.com, Walter Bright)
 * License:     Distributed under the Boost Software License, Version 1.0.
 *              http://www.boost.org/LICENSE_1_0.txt
 * Source:      https://github.com/DigitalMars/Compiler/blob/master/dm/src/dmc/iasm.c
 */

/* Inline assembler                             */

#include        <ctype.h>
#include        <limits.h>
#include        <stdlib.h>
#include        <stdio.h>
#include        <string.h>
#include        <setjmp.h>
#include        <time.h>

#include        "cc.h"
#include        "token.h"
#include        "parser.h"
#include        "global.h"
#include        "el.h"
#include        "type.h"
#include        "oper.h"
#include        "code.h"
#include        "iasm.h"
#include        "cpp.h"

#if __DMC__
#undef setjmp
#endif

#if !DEMO && !SPP

//#define EXTRA_DEBUG 1

static char __file__[] = __FILE__;      /* for tassert.h                */
#include        "tassert.h"

#if MARS
// Error numbers
enum ASMERRMSGS
{
    EM_bad_float_op,
    EM_bad_addr_mode,
    EM_align,
    EM_opcode_exp,
    EM_prefix,
    EM_eol,
    EM_bad_operand,
    EM_ident_exp,
    EM_not_struct,
    EM_nops_expected,
    EM_bad_op,
    EM_const_init,
    EM_unknown_tag,
    EM_ambig_ref,
    EM_undefined,
    EM_pointer,
    EM_colon,
    EM_rbra,
    EM_rpar,
    EM_ptr_exp,
    EM_num,
};
#endif

// Additional tokens for the inline assembler
typedef enum
{
    ASMTKalign = TKMAX+1,
    ASMTKbyte,
    ASMTKdword,
    ASMTKeven,
    ASMTKfar,
    ASMTKint,
    ASMTKlength,
    ASMTKnear,
    ASMTKoffset,
    ASMTKptr,
    ASMTKqword,
    ASMTKseg,
    ASMTKshort,
    ASMTKsize,
    ASMTKtbyte,
    ASMTKtype,
    ASMTKword,
    ASMTKlocalsize,
    ASMTKmax = ASMTKlocalsize-(TKMAX+1)+1
} ASMTK;

static const char *apszAsmtk[ASMTKmax] = {
        "ALIGN",
        "BYTE",
        "DWORD",
        "EVEN",
        "FAR",
        "INT",
        "LENGTH",
        "NEAR",
        "OFFSET",
        "PTR",
        "QWORD",
        "SEG",
        "SHORT",
        "SIZE",
        "TBYTE",
        "TYPE",
        "WORD",
        "__LOCAL_SIZE",
};

void dolabel (const char *labelident );

// Determine if identifier is "$"
#define isdollar(p)     (*(unsigned short *)(p) == '$')

typedef struct _ASM_STATE
{
        unsigned char bAsm_block ;
        unsigned char ucItype;  // Instruction type

#if MARS
        Loc loc;
#else
        Srcpos Asrcpos;
#endif
        unsigned char bInit;
        symbol * psDollar;
        symbol * psLocalsize;
        jmp_buf env;
        unsigned char bReturnax;
} ASM_STATE;

ASM_STATE asmstate = { 0 };
extern block *curblock;

// From ptrntab.c
const char *asm_opstr(OP *pop);
OP *asm_op_lookup(const char *s);
void init_optab();

static unsigned char asm_TKlbra_seen = FALSE;

typedef struct
{
        char regstr[6];
        unsigned char val;
        opflag_t ty;
} REG;

static REG regFp =      { "ST", 0, _st };

static REG aregFp[] = {
        { "ST(0)", 0, _sti },
        { "ST(1)", 1, _sti },
        { "ST(2)", 2, _sti },
        { "ST(3)", 3, _sti },
        { "ST(4)", 4, _sti },
        { "ST(5)", 5, _sti },
        { "ST(6)", 6, _sti },
        { "ST(7)", 7, _sti }
};
#define _AL             0
#define _AH             4
#define _AX             0
#define _EAX            0
#define _BL             3
#define _BH             7
#define _BX             3
#define _EBX            3
#define _CL             1
#define _CH             5
#define _CX             1
#define _ECX            1
#define _DL             2
#define _DH             6
#define _DX             2
#define _EDX            2
#define _BP             5
#define _EBP            5
#define _SP             4
#define _ESP            4
#define _DI             7
#define _EDI            7
#define _SI             6
#define _ESI            6
#define _ES             0
#define _CS             1
#define _SS             2
#define _DS             3
#define _GS             5
#define _FS             4

static REG regtab[] =
{
"AL",   _AL,    _r8 | _al,
"AH",   _AH,    _r8,
"AX",   _AX,    _r16 | _ax,
"EAX",  _EAX,   _r32 | _eax,
"BL",   _BL,    _r8,
"BH",   _BH,    _r8,
"BX",   _BX,    _r16,
"EBX",  _EBX,   _r32,
"CL",   _CL,    _r8 | _cl,
"CH",   _CH,    _r8,
"CX",   _CX,    _r16,
"ECX",  _ECX,   _r32,
"DL",   _DL,    _r8,
"DH",   _DH,    _r8,
"DX",   _DX,    _r16 | _dx,
"EDX",  _EDX,   _r32,
"BP",   _BP,    _r16,
"EBP",  _EBP,   _r32,
"SP",   _SP,    _r16,
"ESP",  _ESP,   _r32,
"DI",   _DI,    _r16,
"EDI",  _EDI,   _r32,
"SI",   _SI,    _r16,
"ESI",  _ESI,   _r32,
"ES",   _ES,    _seg | _es,
"CS",   _CS,    _seg | _cs,
"SS",   _SS,    _seg | _ss ,
"DS",   _DS,    _seg | _ds,
"GS",   _GS,    _seg | _gs,
"FS",   _FS,    _seg | _fs,
"CR0",  0,      _special | _crn,
"CR2",  2,      _special | _crn,
"CR3",  3,      _special | _crn,
"CR4",  4,      _special | _crn,
"DR0",  0,      _special | _drn,
"DR1",  1,      _special | _drn,
"DR2",  2,      _special | _drn,
"DR3",  3,      _special | _drn,
"DR4",  4,      _special | _drn,
"DR5",  5,      _special | _drn,
"DR6",  6,      _special | _drn,
"DR7",  7,      _special | _drn,
"TR3",  3,      _special | _trn,
"TR4",  4,      _special | _trn,
"TR5",  5,      _special | _trn,
"TR6",  6,      _special | _trn,
"TR7",  7,      _special | _trn,
"MM0",  0,      _mm,
"MM1",  1,      _mm,
"MM2",  2,      _mm,
"MM3",  3,      _mm,
"MM4",  4,      _mm,
"MM5",  5,      _mm,
"MM6",  6,      _mm,
"MM7",  7,      _mm,
"XMM0", 0,      _xmm,
"XMM1", 1,      _xmm,
"XMM2", 2,      _xmm,
"XMM3", 3,      _xmm,
"XMM4", 4,      _xmm,
"XMM5", 5,      _xmm,
"XMM6", 6,      _xmm,
"XMM7", 7,      _xmm,
};

typedef enum {
    ASM_JUMPTYPE_UNSPECIFIED,
    ASM_JUMPTYPE_SHORT,
    ASM_JUMPTYPE_NEAR,
    ASM_JUMPTYPE_FAR
} ASM_JUMPTYPE;             // ajt

typedef struct opnd
{
        REG *base;              // if plain register
        REG *pregDisp1;         // if [register1]
        REG *pregDisp2;
        REG *segreg;            // if segment override
        char indirect;          // if had a '*' or '->'
        char bOffset;           // if 'offset' keyword
        char bSeg;              // if 'segment' keyword
        char bPtr;              // if 'ptr' keyword
        unsigned char   uchMultiplier;
                                // High bit = 1 if specified with pregdisp1
                                // otherwise specified with pregDisp2
        opflag_t usFlags;
        symbol *s;
        long disp;
        double real;
        type    *ptype;
        ASM_JUMPTYPE    ajt;
} OPND;

//
// Exported functions called from the compiler
//
int asm_state(int iFlags);
void iasm_term();
regm_t iasm_regs( block * bp );

//
// Local functions defined and only used here
//
STATIC OPND *asm_add_exp();
STATIC OPND *opnd_calloc();
STATIC void opnd_free(OPND *popnd);
STATIC OPND *asm_and_exp();
STATIC OPND *asm_cond_exp();
STATIC opflag_t asm_determine_operand_flags(OPND *popnd);
STATIC void asm_error();
#pragma SC noreturn(asm_error)

STATIC void asmerr(int);
#pragma SC noreturn(asmerr)

STATIC OPND *asm_equal_exp();
STATIC OPND *asm_inc_or_exp();
STATIC OPND *asm_log_and_exp();
STATIC OPND *asm_log_or_exp();
STATIC char asm_length_type_size(OPND *popnd);
STATIC enum_TK asm_token();
STATIC unsigned char asm_match_flags(opflag_t usOp , opflag_t usTable );
STATIC unsigned char asm_match_float_flags(opflag_t usOp, opflag_t usTable);
STATIC void asm_make_modrm_byte(
#ifdef DEBUG
        unsigned char *puchOpcode, unsigned *pusIdx,
#endif
        code *pc,
        unsigned usFlags,
        OPND * popnd, OPND * popnd2 );
STATIC regm_t asm_modify_regs( PTRNTAB ptb, OPND * popnd1, OPND * popnd2 );
STATIC void asm_output_flags( opflag_t usFlags );
STATIC void asm_output_popnd( OPND * popnd );
STATIC unsigned asm_type_size( type * ptype );
STATIC opflag_t asm_float_type_size( type * ptype, opflag_t *pusFloat );
STATIC OPND *asm_mul_exp();
STATIC OPND *asm_br_exp();
STATIC OPND *asm_primary_exp();
STATIC OPND *asm_prim_post(OPND *);
STATIC REG *asm_reg_lookup(char *);
STATIC OPND *asm_rel_exp();
STATIC OPND *asm_shift_exp();
STATIC OPND *asm_una_exp();
STATIC OPND *asm_xor_exp();
STATIC void *link_alloc(size_t, void *);
STATIC void asm_chktok(enum_TK toknum,unsigned errnum);
STATIC void asm_db_parse( OP *pop );
STATIC void asm_da_parse( OP *pop );

unsigned compute_hashkey(char *);


/***************************************
 */

symbol *asm_define_label(const char *id)
{   symbol *s;

    s = scope_define(id, SCTlabel,SClabel);
    s->Slabelblk = block_calloc();
    return s;
}

/*******************************
 */

STATIC OPND * opnd_calloc()
{
    return (OPND *)mem_calloc(sizeof(OPND));
}

/*******************************
 */

STATIC void opnd_free(OPND *popnd)
{
    if (popnd)
    {
        type_free( popnd->ptype );
        mem_free( popnd );
    }
}

/*******************************
 */

STATIC void asm_chktok(enum_TK toknum,unsigned errnum)
{
    if (tok.TKval == toknum)
        asm_token();                    // scan past token
    else
        asmerr(errnum);
}

/*******************************
 * Create a new block as
 * the successor to the old one.
 */

STATIC void asm_nextblock()
{
    block_goto();
}

/*******************************
 */

STATIC PTRNTAB asm_classify(OP *pop, OPND * popnd1, OPND * popnd2, OPND * popnd3,
        unsigned *pusNumops )
{
        unsigned usNumops;
        unsigned usActual;
        PTRNTAB ptbRet = { NULL };
        opflag_t usFlags1 = 0 ;
        opflag_t usFlags2 = 0;
        opflag_t usFlags3 = 0;
        PTRNTAB1 *pptb1;
        PTRNTAB2 *pptb2;
        PTRNTAB3 *pptb3;
        char    bFake = FALSE;

        unsigned        bMatch1, bMatch2, bMatch3, bRetry = FALSE;

        // How many arguments are there?  the parser is strictly left to right
        // so this should work.

        if (!popnd1)
            usNumops = 0;
        else
        {
            popnd1->usFlags = usFlags1 = asm_determine_operand_flags(popnd1);
            if (!popnd2)
                usNumops = 1;
            else
            {
                popnd2->usFlags = usFlags2 = asm_determine_operand_flags(popnd2);
                if (!popnd3)
                    usNumops = 2;
                else
                {
                    popnd3->usFlags = usFlags3 = asm_determine_operand_flags(popnd3);
                    usNumops = 3;
                }
            }
        }

        // Now check to insure that the number of operands is correct
        if ((usActual = (pop->usNumops & ITSIZE)) != usNumops &&
                asmstate.ucItype != ITopt && asmstate.ucItype != ITfloat)
        {
PARAM_ERROR:
                synerr(EM_nops_expected, usActual, asm_opstr(pop), usNumops );
                asm_error( );
        }
        if (usActual < usNumops)
            *pusNumops = usActual;
        else
            *pusNumops = usNumops;
//
//      The number of arguments matches, now check to find the opcode
//      in the associated opcode table
//
RETRY:
        //printf("usActual = %d\n", usActual);
        switch (usActual)
        {
            case 0:
                ptbRet = pop->ptb ;
                goto RETURN_IT;

            case 1:
                //printf("usFlags1 = "); asm_output_flags(usFlags1); printf("\n");
                for (pptb1 = pop->ptb.pptb1; pptb1->opcode != ASM_END;
                        pptb1++)
                {
                        //printf("table    = "); asm_output_flags(pptb1->usOp1); printf("\n");
                        bMatch1 = asm_match_flags(usFlags1, pptb1->usOp1);
                        //printf("bMatch1 = x%x\n", bMatch1);
                        if (bMatch1)
                        {   if (pptb1->opcode == 0x68 &&
                                !I16 &&
                                pptb1->usOp1 == _imm16
                              )
                                // Don't match PUSH imm16 in 32 bit code
                                continue;
                            break;
                        }
                        if ((asmstate.ucItype == ITimmed) &&
                            asm_match_flags(usFlags1,
                                CONSTRUCT_FLAGS(_8 | _16 | _32, _imm, _normal,
                                                 0)) &&
                                popnd1->disp == pptb1->usFlags)
                            break;
                        if ((asmstate.ucItype == ITopt ||
                             asmstate.ucItype == ITfloat) &&
                            !usNumops &&
                            !pptb1->usOp1)
                        {
                            if (usNumops > 1)
                                goto PARAM_ERROR;
                            break;
                        }
                }
                if (pptb1->opcode == ASM_END)
                {
#ifdef DEBUG
                    if (debuga)
                    {   printf("\t%s\t", asm_opstr(pop));
                        if (popnd1)
                                asm_output_popnd(popnd1);
                        if (popnd2) {
                                printf(",");
                                asm_output_popnd(popnd2);
                        }
                        if (popnd3) {
                                printf(",");
                                asm_output_popnd(popnd3);
                        }
                        printf("\n");

                        printf("OPCODE mism = ");
                        if (popnd1)
                            asm_output_flags(popnd1->usFlags);
                        else
                            printf("NONE");
                        printf("\n");
                    }
#endif
TYPE_SIZE_ERROR:
                        if (popnd1 && ASM_GET_aopty(popnd1->usFlags) != _reg)
                        {
                            usFlags1 = popnd1->usFlags |= _anysize;
                            if (asmstate.ucItype == ITjump)
                            {
                                if (bRetry && popnd1->s && popnd1->s->Sclass != SClabel)
                                {   // Assume forward referenced label
                                    symbol *s;

                                    s = scope_define(tok.TKid,SCTlabel,SClabel);
                                    s->Slabelblk = block_calloc();
                                    popnd1->s = s;
                                    type_settype(&popnd1->ptype,NULL);
                                    popnd1->usFlags = usFlags1 =
                                        asm_determine_operand_flags( popnd1 );
                                    bFake = TRUE;
                                    goto RETRY;
                                }

                                popnd1->usFlags |= CONSTRUCT_FLAGS(0, 0, 0,
                                        _fanysize);
                            }
                        }
                        if (popnd2 && ASM_GET_aopty(popnd2->usFlags) != _reg) {
                            usFlags2 = popnd2->usFlags |= (_anysize);
                            if (asmstate.ucItype == ITjump)
                                popnd2->usFlags |= CONSTRUCT_FLAGS(0, 0, 0,
                                        _fanysize);
                        }
                        if (popnd3 && ASM_GET_aopty(popnd3->usFlags) != _reg) {
                            usFlags3 = popnd3->usFlags |= (_anysize);
                            if (asmstate.ucItype == ITjump)
                                popnd3->usFlags |= CONSTRUCT_FLAGS(0, 0, 0,
                                        _fanysize);
                        }
                        if (bRetry)
                        {
                            synerr(EM_bad_op,asm_opstr(pop));   // illegal type/size of operands
                            asm_error();
                        }
                        bRetry = TRUE;
                        goto RETRY;

                }
                ptbRet.pptb1 = pptb1;
                goto RETURN_IT;

            case 2:
                //printf("usFlags1 = "); asm_output_flags(usFlags1); printf(" ");
                //printf("usFlags2 = "); asm_output_flags(usFlags2); printf("\n");
                for (pptb2 = pop->ptb.pptb2;
                     pptb2->opcode != ASM_END;
                     pptb2++)
                {
                        //printf("table1   = "); asm_output_flags(pptb2->usOp1); printf(" ");
                        //printf("table2   = "); asm_output_flags(pptb2->usOp2); printf("\n");
                        bMatch1 = asm_match_flags(usFlags1, pptb2->usOp1);
                        bMatch2 = asm_match_flags(usFlags2, pptb2->usOp2);
                        //printf("match1 = %d, match2 = %d\n",bMatch1,bMatch2);
                        if (bMatch1 && bMatch2) {

                            //printf("match\n");

// OK, if they both match and the first op in the table is not AL
// or size of 8 and the second is immediate 8,
// then check to see if the constant
// is a signed 8 bit constant.  If so, then do not match, otherwise match
//
                            if (!bRetry &&
                                !((ASM_GET_uSizemask(pptb2->usOp1) & _8) ||
                                  (ASM_GET_uRegmask(pptb2->usOp1) & _al)) &&
                                (ASM_GET_aopty(pptb2->usOp2) == _imm) &&
                                (ASM_GET_uSizemask(pptb2->usOp2) & _8))
                            {

                                if (popnd2->disp <= SCHAR_MAX)
                                    break;
                                else
                                    bFake = TRUE;
                            }
                            else
                                break;
                        }
                        if (asmstate.ucItype == ITopt ||
                            asmstate.ucItype == ITfloat)
                        {
                                switch (usNumops)
                                {
                                    case 0:
                                        if (!pptb2->usOp1)
                                            goto Lfound2;
                                        break;
                                    case 1:
                                        if (bMatch1 && !pptb2->usOp2)
                                            goto Lfound2;
                                        break;
                                    case 2:
                                        break;
                                    default:
                                        goto PARAM_ERROR;
                                }
                        }
#if 0
                        if (asmstate.ucItype == ITshift &&
                            !pptb2->usOp2 &&
                            bMatch1 && popnd2->disp == 1 &&
                            asm_match_flags(usFlags2,
                                CONSTRUCT_FLAGS(_8|_16|_32, _imm,_normal,0))
                          )
                            break;
#endif
                }
            Lfound2:
                if (pptb2->opcode == ASM_END)
                {
#ifdef DEBUG
                    if (debuga)
                    {   printf("\t%s\t", asm_opstr(pop));
                        if (popnd1)
                                asm_output_popnd(popnd1);
                        if (popnd2) {
                                printf(",");
                                asm_output_popnd(popnd2);
                        }
                        if (popnd3) {
                                printf(",");
                                asm_output_popnd(popnd3);
                        }
                        printf("\n");

                        printf("OPCODE mismatch = ");
                        if (popnd1)
                            asm_output_flags(popnd1->usFlags);
                        else
                            printf("NONE");
                        printf( " Op2 = ");
                        if (popnd2)
                            asm_output_flags(popnd2->usFlags);
                        else
                            printf("NONE");
                        printf("\n");
                    }
#endif
                    goto TYPE_SIZE_ERROR;
                }
                ptbRet.pptb2 = pptb2;
                goto RETURN_IT;
        case 3:
                for (pptb3 = pop->ptb.pptb3;
                     pptb3->opcode != ASM_END;
                     pptb3++)
                {
                        bMatch1 = asm_match_flags(usFlags1, pptb3->usOp1);
                        bMatch2 = asm_match_flags(usFlags2, pptb3->usOp2);
                        bMatch3 = asm_match_flags(usFlags3, pptb3->usOp3);
                        if (bMatch1 && bMatch2 && bMatch3)
                            goto Lfound3;
                        if (asmstate.ucItype == ITopt)
                        {
                            switch (usNumops)
                            {
                                case 0:
                                        if (!pptb3->usOp1)
                                            goto Lfound3;
                                        break;
                                case 1:
                                        if (bMatch1 && !pptb3->usOp2)
                                            goto Lfound3;
                                        break;
                                case 2:
                                        if (bMatch1 && bMatch2 && !pptb3->usOp3)
                                            goto Lfound3;
                                        break;
                                case 3:
                                        break;
                                default:
                                        goto PARAM_ERROR;
                            }
                        }
                }
            Lfound3:
                if (pptb3->opcode == ASM_END)
                {
#ifdef DEBUG
                    if (debuga)
                    {   printf("\t%s\t", asm_opstr(pop));
                        if (popnd1)
                                asm_output_popnd(popnd1);
                        if (popnd2) {
                                printf(",");
                                asm_output_popnd(popnd2);
                        }
                        if (popnd3) {
                                printf(",");
                                asm_output_popnd(popnd3);
                        }
                        printf("\n");

                        printf("OPCODE mismatch = ");
                        if (popnd1)
                            asm_output_flags(popnd1->usFlags);
                        else
                            printf("NONE");
                        printf( " Op2 = ");
                        if (popnd2)
                            asm_output_flags(popnd2->usFlags);
                        else
                            printf("NONE");
                        if (popnd3)
                            asm_output_flags(popnd3->usFlags);
                        printf("\n");
                    }
#endif
                    goto TYPE_SIZE_ERROR;
                }
                ptbRet.pptb3 = pptb3;
                goto RETURN_IT;
        }
RETURN_IT:
        if (bRetry && !bFake)
        {
#if MARS
            synerr( EM_bad_op, asm_opstr(pop) );
#else
            warerr( WM_bad_op, asm_opstr(pop) );
#endif
        }
        return ptbRet;
}

/*******************************
 */

STATIC opflag_t asm_determine_float_flags(OPND *popnd)
{
        opflag_t us, usFloat;
        symbol  * ps;
        tym_t   ty;

    // Insure that if it is a register, that it is not a normal processor
    // register.

        if (popnd->base &&
                !popnd->s && !popnd->disp && !popnd->real
                && !(popnd->base->ty & (_r8 | _r16 | _r32))) {
                return( popnd->base->ty );
        }
        if (popnd->pregDisp1 && !popnd->base) {
                us = asm_float_type_size(popnd->ptype, &usFloat);
                if (popnd->pregDisp1->ty & _r32)
                    return( CONSTRUCT_FLAGS( us, _m, _addr32, usFloat ));
                else
                if (popnd->pregDisp1->ty & _r16)
                    return( CONSTRUCT_FLAGS( us, _m, _addr16, usFloat ));
        }
        else
        if ((ps = popnd->s) != 0)
        {
            us = asm_float_type_size( popnd->ptype, &usFloat );
            return( CONSTRUCT_FLAGS( us, _m, _normal, usFloat ));
        }
        if (popnd->segreg) {
            us = asm_float_type_size( popnd->ptype, &usFloat );
            if (I32)
                return( CONSTRUCT_FLAGS( us, _m, _addr32, usFloat ));
            else
                return( CONSTRUCT_FLAGS( us, _m, _addr16, usFloat ));
        }
#if 0
        if (popnd->real) {
                if (tybasic(popnd->ptype->Tty) == TYfloat) {
                        popnd->s = fconst(popnd->real);
                        return( CONSTRUCT_FLAGS( _32, _m, _normal, 0 ));
                }
                else {                          // Yes.. double precision
                        popnd->s = dconst(popnd->real);
                        return( CONSTRUCT_FLAGS( 0, _m, _normal, _f64 ) );
                }
        }
#endif
        asmerr(EM_bad_float_op);        // unknown operand for floating point instruction
        return 0;
}

/*******************************
 */

STATIC opflag_t asm_determine_operand_flags( OPND * popnd )
{
        symbol  *ps;
        tym_t   ty;
        opflag_t us;
        opflag_t sz;
        ASM_OPERAND_TYPE opty;
        ASM_MODIFIERS amod;

        // If specified 'offset' or 'segment' but no symbol
        if ((popnd->bOffset || popnd->bSeg) && !popnd->s)
            asmerr(EM_bad_addr_mode);           // illegal addressing mode

        if (asmstate.ucItype == ITfloat)
            return asm_determine_float_flags(popnd);

        // If just a register
        if (popnd->base && !popnd->s && !popnd->disp && !popnd->real)
                return popnd->base->ty;
#if DEBUG
        if (debuga)
            printf( "popnd->base = %s\n, popnd->pregDisp1 = %ld\n", popnd->base ? popnd->base->regstr : "NONE", popnd->pregDisp1 );
#endif
        ps = popnd->s;
        sz = asm_type_size(popnd->ptype);
        if (popnd->pregDisp1 && !popnd->base)
        {
            if (ps && ps->Sclass == SClabel && sz == _anysize)
                sz = I32 ? _32 : _16;
            return (popnd->pregDisp1->ty & _r32)
                ? CONSTRUCT_FLAGS( sz, _m, _addr32, 0 )
                : CONSTRUCT_FLAGS( sz, _m, _addr16, 0 );
        }
        else if (ps)
        {
                if (popnd->bOffset || popnd->bSeg || ps->Sfl == FLlocalsize)
                    return I32
                        ? CONSTRUCT_FLAGS( _32, _imm, _normal, 0 )
                        : CONSTRUCT_FLAGS( _16, _imm, _normal, 0 );

                if (ps->Sclass == SClabel)
                {
                    switch (popnd->ajt)
                    {
                        case ASM_JUMPTYPE_UNSPECIFIED:
                            if (isdollar(ps->Sident))
                            {
                                if (popnd->disp >= CHAR_MIN &&
                                    popnd->disp <= CHAR_MAX)
                                    us = CONSTRUCT_FLAGS(_8, _rel, _flbl,0);
                                else
                                if (popnd->disp >= SHRT_MIN &&
                                    popnd->disp <= SHRT_MAX)
                                    us = CONSTRUCT_FLAGS(_16, _rel, _flbl,0);
                                else
                                    us = CONSTRUCT_FLAGS(_32, _rel, _flbl,0);
                            }
                            else if (asmstate.ucItype != ITjump)
                            {   if (sz == _8)
                                {   us = CONSTRUCT_FLAGS(_8,_rel,_flbl,0);
                                    break;
                                }
                                goto case_near;
                            }
                            else
                                us = I32
                                    ? CONSTRUCT_FLAGS(_8|_32, _rel, _flbl,0)
                                    : CONSTRUCT_FLAGS(_8|_16, _rel, _flbl,0);
                            break;

                        case ASM_JUMPTYPE_NEAR:
                        case_near:
                            us = I32
                                ? CONSTRUCT_FLAGS( _32, _rel, _flbl, 0 )
                                : CONSTRUCT_FLAGS( _16, _rel, _flbl, 0 );
                            break;
                        case ASM_JUMPTYPE_SHORT:
                            us = CONSTRUCT_FLAGS( _8, _rel, _flbl, 0 );
                            break;
                        case ASM_JUMPTYPE_FAR:
                            us = I32
                                ? CONSTRUCT_FLAGS( _48, _rel, _flbl, 0 )
                                : CONSTRUCT_FLAGS( _32, _rel, _flbl, 0 );
                            break;
                        default:
                            assert(0);
                    }
                    return us;
                }
                ty = popnd->ptype->Tty;
                if (typtr(ty) && tyfunc(popnd->ptype->Tnext->Tty))
                {
                        ty = popnd->ptype->Tnext->Tty;
                        if (tyfarfunc( tybasic (ty ))) {
                            return I32
                                ? CONSTRUCT_FLAGS( _48, _mnoi, _fn32, 0 )
                                : CONSTRUCT_FLAGS( _32, _mnoi, _fn32, 0 );
                        }
                        else {
                            return I32
                                ? CONSTRUCT_FLAGS( _32, _m, _fn16, 0 )
                                : CONSTRUCT_FLAGS( _16, _m, _fn16, 0 );
                        }
                }
                else if (tyfunc( ty))
                {
                    if (tyfarfunc( tybasic( ty )))
                        return I32
                            ? CONSTRUCT_FLAGS( _48, _p, _fn32, 0 )
                            : CONSTRUCT_FLAGS( _32, _p, _fn32, 0 );
                    else
                        return I32
                            ? CONSTRUCT_FLAGS( _32, _rel, _fn16, 0 )
                            : CONSTRUCT_FLAGS( _16, _rel, _fn16, 0 );
                }
                else if (asmstate.ucItype == ITjump)
                {   amod = _normal;
                    goto L1;
                }
                else
                    return CONSTRUCT_FLAGS( sz, _m, _normal, 0 );
        }
        if (popnd->segreg /*|| popnd->bPtr*/)
        {
            amod = I32 ? _addr32 : _addr16;
            if (asmstate.ucItype == ITjump)
            {
            L1:
                opty = _m;
                if (I32)
                {   if (sz == _48)
                        opty = _mnoi;
                }
                else
                {
                    if (sz == _32)
                        opty = _mnoi;
                }
                us = CONSTRUCT_FLAGS(sz,opty,amod,0);
            }
            else
                us = CONSTRUCT_FLAGS( sz,
//                                   _rel, amod, 0 );
                                     _m, amod, 0 );
        }

        else if (popnd->ptype)
            us = CONSTRUCT_FLAGS( sz, _imm, _normal, 0 );

        else if (popnd->disp >= CHAR_MIN && popnd->disp <= UCHAR_MAX)
            us = CONSTRUCT_FLAGS( _8 | _16 | _32, _imm, _normal, 0 );
        else if (popnd->disp >= SHRT_MIN && popnd->disp <= USHRT_MAX)
            us = CONSTRUCT_FLAGS( _16 | _32, _imm, _normal, 0);
        else
            us = CONSTRUCT_FLAGS( _32, _imm, _normal, 0 );
        return us;
}

/******************************
 * Convert assembly instruction into a code, and append
 * it to the code generated for this block.
 */

STATIC void asm_emit( Srcpos srcpos,
        unsigned usNumops, PTRNTAB ptb,
        OP *popPrefix, OP *pop,
        OPND * popnd1, OPND * popnd2, OPND * popnd3 )
{
#ifdef DEBUG
        unsigned char auchOpcode[16];
        unsigned usIdx = 0;
        #define emit(op)        (auchOpcode[usIdx++] = op)
#else
        #define emit(op)        (void)(op)
#endif
        char *id;
//      unsigned us;
        unsigned char *puc;
        unsigned usDefaultseg;
        code *pc = NULL;
        elem *e;
        OPND *  popndTmp;
        ASM_OPERAND_TYPE    aoptyTmp;
        unsigned        uSizemaskTmp;
        REG     *pregSegment;
        code    *pcPrefix = NULL;
        unsigned            uSizemask1 =0, uSizemask2 =0, uSizemask3 =0;
        //ASM_OPERAND_TYPE    aopty1 = _reg , aopty2 = 0, aopty3 = 0;
        ASM_MODIFIERS       amod1 = _normal, amod2 = _normal, amod3 = _normal;
        unsigned            uRegmask1 = 0, uRegmask2 =0, uRegmask3 =0;
        unsigned            uSizemaskTable1 =0, uSizemaskTable2 =0,
                            uSizemaskTable3 =0;
        ASM_OPERAND_TYPE    aoptyTable1 = _reg, aoptyTable2 = _reg, aoptyTable3 = _reg;
        ASM_MODIFIERS       amodTable1 = _normal,
                            amodTable2 = _normal,
                            amodTable3 = _normal;
        unsigned            uRegmaskTable1 = 0, uRegmaskTable2 =0,
                            uRegmaskTable3 =0;

        pc = code_calloc();
        pc->Iflags |= CFpsw;            // assume we want to keep the flags
        if (popnd1)
        {
            uSizemask1 = ASM_GET_uSizemask( popnd1->usFlags );
            //aopty1 = ASM_GET_aopty(popnd1->usFlags );
            amod1 = ASM_GET_amod( popnd1->usFlags );
            uRegmask1 = ASM_GET_uRegmask( popnd1->usFlags );

            uSizemaskTable1 = ASM_GET_uSizemask( ptb.pptb1->usOp1 );
            aoptyTable1 = ASM_GET_aopty(ptb.pptb1->usOp1 );
            amodTable1 = ASM_GET_amod( ptb.pptb1->usOp1 );
            uRegmaskTable1 = ASM_GET_uRegmask( ptb.pptb1->usOp1 );

        }
        if (popnd2)
        {
#if 0
            printf("asm_emit:\nop: ");
            asm_output_flags(popnd2->usFlags);
            printf("\ntb: ");
            asm_output_flags(ptb.pptb2->usOp2);
            printf("\n");
#endif
            uSizemask2 = ASM_GET_uSizemask( popnd2->usFlags );
            //aopty2 = ASM_GET_aopty(popnd2->usFlags );
            amod2 = ASM_GET_amod( popnd2->usFlags );
            uRegmask2 = ASM_GET_uRegmask( popnd2->usFlags );

            uSizemaskTable2 = ASM_GET_uSizemask( ptb.pptb2->usOp2 );
            aoptyTable2 = ASM_GET_aopty(ptb.pptb2->usOp2 );
            amodTable2 = ASM_GET_amod( ptb.pptb2->usOp2 );
            uRegmaskTable2 = ASM_GET_uRegmask( ptb.pptb2->usOp2 );
        }
        if (popnd3)
        {
            uSizemask3 = ASM_GET_uSizemask( popnd3->usFlags );
            //aopty3 = ASM_GET_aopty(popnd3->usFlags );
            amod3 = ASM_GET_amod( popnd3->usFlags );
            uRegmask3 = ASM_GET_uRegmask( popnd3->usFlags );

            uSizemaskTable3 = ASM_GET_uSizemask( ptb.pptb3->usOp3 );
            aoptyTable3 = ASM_GET_aopty(ptb.pptb3->usOp3 );
            amodTable3 = ASM_GET_amod( ptb.pptb3->usOp3 );
            uRegmaskTable3 = ASM_GET_uRegmask( ptb.pptb3->usOp3 );
        }

        if (popPrefix)
        {   curblock->usIasmregs |= asm_modify_regs(popPrefix->ptb,NULL,NULL);
            emit(popPrefix->ptb.pptb0->opcode);
            switch (popPrefix->ptb.pptb0->opcode)
            {
                case 0xf3:                              // REP/REPZ
                case 0xf2:                              // REPNE
                        curblock->usIasmregs |= mCX;
                case 0xf0:                              // LOCK
                        pcPrefix = gen1(NULL, popPrefix->ptb.pptb0->opcode);
                        break;
                default:
                        assert(0);
            }
        }
        curblock->usIasmregs |= asm_modify_regs( ptb, popnd1, popnd2 );

        if (!I32 && ptb.pptb0->usFlags & _I386)
        {
            switch (usNumops) {
                case 0:
                    break;
                case 1:
                    if (popnd1 && popnd1->s)
                    {
L386_WARNING:
                        id = popnd1->s->Sident;
L386_WARNING2:
                        if (config.target_cpu < TARGET_80386)
                        {   // Reference to %s caused a 386 instruction to be generated
                            warerr(WM_386_op, id);
                        }
                    }
                    break;
                case 2:
                case 3:     // The third operand is always an _imm
                    if (popnd1 && popnd1->s)
                        goto L386_WARNING;
                    if (popnd2 && popnd2->s)
                    {
                        id = popnd2->s->Sident;
                        goto L386_WARNING2;
                    }
                    break;
            }
        }

        switch (usNumops)
        {
            case 0:
                if ((I32 && (ptb.pptb0->usFlags & _16_bit)) ||
                        (!I32 && (ptb.pptb0->usFlags & _32_bit)))
                {
                        emit(0x66);
                        pc->Iflags |= CFopsize;
                }
                break;

            // 3 and 2 are the same because the third operand is always
            // an immediate and does not affect operation size
            case 3:
            case 2:
                if ( (I32 &&
                      (amod2 == _addr16 ||
                       (uSizemaskTable2 & _16 && aoptyTable2 == _rel) ||
                       (uSizemaskTable2 & _32 && aoptyTable2 == _mnoi) ||
                       (ptb.pptb2->usFlags & _16_bit_addr)
                      )
                     ) ||
                     (!I32 &&
                       (amod2 == _addr32 ||
                        (uSizemaskTable2 & _32 && aoptyTable2 == _rel) ||
                        (uSizemaskTable2 & _48 && aoptyTable2 == _mnoi) ||
                        (ptb.pptb2->usFlags & _32_bit_addr)))
                   )
                {
                        emit(0x67);
                        pc->Iflags |= CFaddrsize;
                        if (I32)
                            amod2 = _addr16;
                        else
                            amod2 = _addr32;
                        popnd2->usFlags &= ~CONSTRUCT_FLAGS(0,0,7,0);
                        popnd2->usFlags |= CONSTRUCT_FLAGS(0,0,amod2,0);
                }


            /* Fall through, operand 1 controls the opsize, but the
                address size can be in either operand 1 or operand 2,
                hence the extra checking the flags tested for SHOULD
                be mutex on operand 1 and operand 2 because there is
                only one MOD R/M byte
             */

            case 1:
                if ( (I32 &&
                      (amod1 == _addr16 ||
                       (uSizemaskTable1 & _16 && aoptyTable1 == _rel) ||
                        (uSizemaskTable1 & _32 && aoptyTable1 == _mnoi) ||
                        (ptb.pptb1->usFlags & _16_bit_addr))) ||
                     (!I32 &&
                      (amod1 == _addr32 ||
                        (uSizemaskTable1 & _32 && aoptyTable1 == _rel) ||
                        (uSizemaskTable1 & _48 && aoptyTable1 == _mnoi) ||
                         (ptb.pptb1->usFlags & _32_bit_addr))))
                {
                        emit(0x67);     // address size prefix
                        pc->Iflags |= CFaddrsize;
                        if (I32)
                            amod1 = _addr16;
                        else
                            amod1 = _addr32;
                        popnd1->usFlags &= ~CONSTRUCT_FLAGS(0,0,7,0);
                        popnd1->usFlags |= CONSTRUCT_FLAGS(0,0,amod1,0);
                }

                // If the size of the operand is unknown, assume that it is
                // the default size
                if (( I32 && (ptb.pptb0->usFlags & _16_bit)) ||
                    (!I32 && (ptb.pptb0->usFlags & _32_bit)))
                {
                    //if (asmstate.ucItype != ITjump)
                    {   emit(0x66);
                        pc->Iflags |= CFopsize;
                    }
                }
                if (((pregSegment = (popndTmp = popnd1)->segreg) != NULL) ||
                        ((popndTmp = popnd2) != NULL &&
                        (pregSegment = popndTmp->segreg) != NULL)
                   )
                {
                    if ((popndTmp->pregDisp1 &&
                            popndTmp->pregDisp1->val == _BP) ||
                            popndTmp->pregDisp2 &&
                            popndTmp->pregDisp2->val == _BP)
                            usDefaultseg = _SS;
                    else
                            usDefaultseg = _DS;
                    if (pregSegment->val != usDefaultseg)
                        switch (pregSegment->val) {
                        case _CS:
                                emit(0x2e);
                                pc->Iflags |= CFcs;
                                break;
                        case _SS:
                                emit(0x36);
                                pc->Iflags |= CFss;
                                break;
                        case _DS:
                                emit(0x3e);
                                pc->Iflags |= CFds;
                                break;
                        case _ES:
                                emit(0x26);
                                pc->Iflags |= CFes;
                                break;
                        case _FS:
                                emit(0x64);
                                pc->Iflags |= CFfs;
                                break;
                        case _GS:
                                emit(0x65);
                                pc->Iflags |= CFgs;
                                break;
                        default:
                                assert(0);
                        }
                }
                break;
        }

        unsigned opcode = ptb.pptb0->opcode;

        pc->Iop = opcode;
        if ((opcode & 0xFFFFFF00) == 0x660F3A00 ||    // SSE4
            (opcode & 0xFFFFFF00) == 0x660F3800)      // SSE4
        {
            pc->Iop = 0x66000F00 | ((opcode >> 8) & 0xFF) | ((opcode & 0xFF) << 16);
            goto L3;
        }
        switch (opcode & 0xFF0000)
        {
            case 0:
                break;

            case 0x660000:
                opcode &= 0xFFFF;
                goto L3;

            case 0xF20000:                      // REPNE
            case 0xF30000:                      // REP/REPE
                // BUG: What if there's an address size prefix or segment
                // override prefix? Must the REP be adjacent to the rest
                // of the opcode?
                opcode &= 0xFFFF;
                goto L3;

            case 0x0F0000:                      // an AMD instruction
                puc = ((unsigned char *) &opcode);
                if (puc[1] != 0x0F)             // if not AMD instruction 0x0F0F
                    goto L4;
                emit(puc[2]);
                emit(puc[1]);
                emit(puc[0]);
                pc->Iop >>= 8;
                pc->IEVint2 = puc[0];
                pc->IFL2 = FLconst;
                goto L3;

            default:
                puc = ((unsigned char *) &opcode);
            L4:
                emit(puc[2]);
                emit(puc[1]);
                emit(puc[0]);
                pc->Iop >>= 8;
                pc->Irm = puc[0];
                goto L3;
        }
        if (opcode & 0xff00)
        {
            puc = ((unsigned char *) &(opcode));
            emit(puc[1]);
            emit(puc[0]);
            pc->Iop = puc[1];
            if (pc->Iop == 0x0f)
                pc->Iop = 0x0F00 | puc[0];
            else
            {
                if (opcode == 0xDFE0) // FSTSW AX
                {   pc->Irm = puc[0];
                    goto L2;
                }
                if (asmstate.ucItype == ITfloat)
                    pc->Irm = puc[0];
                else
                {   pc->IEVint2 = puc[0];
                    pc->IFL2 = FLconst;
                }
            }
        }
        else
        {
            emit(opcode);
        }
    L3: ;

        // If CALL, Jxx or LOOPx to a symbolic location
        if (/*asmstate.ucItype == ITjump &&*/
            popnd1 && popnd1->s && popnd1->s->Sclass == SClabel)
        {   symbol *s;

            s = popnd1->s;
            if (isdollar(s->Sident))
            {
                pc->IFL2 = FLconst;
                if (uSizemaskTable1 & (_8 | _16))
                    pc->IEVint2 = popnd1->disp;
                else if (uSizemaskTable1 & _32)
                    pc->IEVpointer2 = (targ_size_t) popnd1->disp;
            }
            else
            {
                if (s->Sclass == SClabel)
                {   if ((pc->Iop & ~0x0F) == 0x70)
                        pc->Iflags |= CFjmp16;
                    if (usNumops == 1)
                    {   pc->IFL2 = FLblock;
                        pc->IEV2.Vblock = popnd1->s->Slabelblk;
                    }
                    else
                    {   pc->IFL1 = FLblock;
                        pc->IEV1.Vblock = popnd1->s->Slabelblk;
                    }
                }
            }
        }

        switch ( usNumops )
        {
            case 0:
                break;
            case 1:
                if (((aoptyTable1 == _reg || aoptyTable1 == _float) &&
                     amodTable1 == _normal && (uRegmaskTable1 & _rplus_r)))
                {
                        if (asmstate.ucItype == ITfloat)
                                pc->Irm += popnd1->base->val;
                        else
                                pc->Iop += popnd1->base->val;
#ifdef DEBUG
                        auchOpcode[usIdx-1] += popnd1->base->val;
#endif
                }
                else
                {       asm_make_modrm_byte(
#ifdef DEBUG
                                auchOpcode, &usIdx,
#endif
                                pc,
                                ptb.pptb1->usFlags,
                                popnd1, NULL );
                }
                popndTmp = popnd1;
                aoptyTmp = aoptyTable1;
                uSizemaskTmp = uSizemaskTable1;
L1:
                if (aoptyTmp == _imm)
                {
                    if (popndTmp->bSeg)
                        switch (popndTmp->s->Sclass) {
                        case SCstatic:
                        case SCextern:
                        case SCglobal:
                        case SCcomdef:
                            break;
                        default:
                            asmerr(EM_bad_addr_mode);   // illegal addressing mode
                        }
                    switch (uSizemaskTmp) {
                        case _8:
                            if (popndTmp->s) {
                                if ((pc->IFL2 = popndTmp->s->Sfl) == 0)
                                    pc->IFL2 = FLauto;
                                pc->Iflags &= ~(CFseg | CFoff);
                                if (popndTmp->bSeg)
                                    pc->Iflags |= CFseg;
                                else
                                    pc->Iflags |= CFoff;
                                pc->IEVoffset2 = popndTmp->disp;
                                pc->IEVsym2 = popndTmp->s;
                            }
                            else {
                                pc->IEVint2 = popndTmp->disp;
                                pc->IFL2 = FLconst;
                            }
                            break;
                        case _16:
                            if (popndTmp->s) {
                                if ((pc->IFL2 = popndTmp->s->Sfl) == 0)
                                    pc->IFL2 = FLauto;
                                pc->Iflags &= ~(CFseg | CFoff);
                                if (popndTmp->bSeg)
                                    pc->Iflags |= CFseg;
                                else
                                    pc->Iflags |= CFoff;
                                pc->IEVoffset2 = popndTmp->disp;
                                pc->IEVsym2 = popndTmp->s;
                            }
                            else {
                                pc->IEVint2 = popndTmp->disp;
                                pc->IFL2 = FLconst;
                            }
                            break;
                        case _32:
                            if (popndTmp->s) {
                                if ((pc->IFL2 = popndTmp->s->Sfl) == 0)
                                    pc->IFL2 = FLauto;
                                pc->Iflags &= ~(CFseg | CFoff);
                                if (popndTmp->bSeg)
                                    pc->Iflags |= CFseg;
                                else
                                    pc->Iflags |= CFoff;
                                pc->IEVoffset2 = popndTmp->disp;
                                pc->IEVsym2 = popndTmp->s;
                            }
                            else {
                                pc->IEVpointer2 = (targ_size_t)
                                                    popndTmp->disp;
                                pc->IFL2 = FLconst;
                            }
                            break;
                    }
                }

                break;
        case 2:
//
// If there are two immediate operands then
//
                if (aoptyTable1 == _imm &&
                    aoptyTable2 == _imm) {
                        pc->IEVint1 = popnd1->disp;
                        pc->IFL1 = FLconst;
                        pc->IEVint2 = popnd2->disp;
                        pc->IFL2 = FLconst;
                        break;
                }
                if (aoptyTable2 == _m ||
                    aoptyTable2 == _rel ||
                    // If not MMX register (_mm) or XMM register (_xmm)
                    (amodTable1 == _rspecial && !(uRegmaskTable1 & (0x08 | 0x10)) && !uSizemaskTable1) ||
                    aoptyTable2 == _rm ||
                    (popnd1->usFlags == _r32 && popnd2->usFlags == _xmm) ||
                    (popnd1->usFlags == _r32 && popnd2->usFlags == _mm))
                {
                        if (ptb.pptb0->opcode == 0x0F7E ||
                            ptb.pptb0->opcode == 0x660F7E)
                        {
                            asm_make_modrm_byte(
#ifdef DEBUG
                                auchOpcode, &usIdx,
#endif
                                pc,
                                ptb.pptb1->usFlags,
                                popnd1, popnd2);
                        }
                        else
                        {
                            asm_make_modrm_byte(
#ifdef DEBUG
                                auchOpcode, &usIdx,
#endif
                                pc,
                                ptb.pptb1->usFlags,
                                popnd2, popnd1);
                        }
                        popndTmp = popnd1;
                        aoptyTmp = aoptyTable1;
                        uSizemaskTmp = uSizemaskTable1;
                }
                else {
                        if (((aoptyTable1 == _reg || aoptyTable1 == _float) &&
                             amodTable1 == _normal &&
                             (uRegmaskTable1 & _rplus_r)))
                        {
                                if (asmstate.ucItype == ITfloat)
                                        pc->Irm += popnd1->base->val;
                                else
                                        pc->Iop += popnd1->base->val;
#ifdef DEBUG
                                auchOpcode[usIdx-1] += popnd1->base->val;
#endif
                        }
                        else
                        if (((aoptyTable2 == _reg || aoptyTable2 == _float) &&
                             amodTable2 == _normal &&
                             (uRegmaskTable2 & _rplus_r)))
                        {
                                if (asmstate.ucItype == ITfloat)
                                        pc->Irm += popnd2->base->val;
                                else
                                        pc->Iop += popnd2->base->val;
#ifdef DEBUG
                                auchOpcode[usIdx-1] += popnd2->base->val;
#endif
                        }
                        else if (ptb.pptb0->opcode == 0xF30FD6 ||
                                 ptb.pptb0->opcode == 0x0F12 ||
                                 ptb.pptb0->opcode == 0x0F16 ||
                                 ptb.pptb0->opcode == 0x660F50 ||
                                 ptb.pptb0->opcode == 0x0F50 ||
                                 ptb.pptb0->opcode == 0x660FD7 ||
                                 ptb.pptb0->opcode == 0x0FD7)
                        {
                            //printf("test1 %x\n", ptb.pptb0->opcode);
                            asm_make_modrm_byte(
#ifdef DEBUG
                                    auchOpcode, &usIdx,
#endif
                                    pc,
                                    ptb.pptb1->usFlags,
                                    popnd2, popnd1);
                        }
                        else
                                asm_make_modrm_byte(
#ifdef DEBUG
                                        auchOpcode, &usIdx,
#endif
                                        pc,
                                        ptb.pptb1->usFlags,
                                        popnd1, popnd2 );

                        if (aoptyTable1 == _imm) {
                                popndTmp = popnd1;
                                aoptyTmp = aoptyTable1;
                                uSizemaskTmp = uSizemaskTable1;
                        }
                        else {
                                popndTmp = popnd2;
                                aoptyTmp = aoptyTable2;
                                uSizemaskTmp = uSizemaskTable2;
                        }
                }
                goto L1;

        case 3:
                if (aoptyTable2 == _m || aoptyTable2 == _rm ||
                    opcode == 0x0FC5) // PEXTRW
                {

                    asm_make_modrm_byte(
#ifdef DEBUG
                                auchOpcode, &usIdx,
#endif
                                pc,
                                ptb.pptb1->usFlags,
                                popnd2, popnd1 );
                        popndTmp = popnd3;
                        aoptyTmp = aoptyTable3;
                        uSizemaskTmp = uSizemaskTable3;
                }
                else
                {
                    if (((aoptyTable1 == _reg || aoptyTable1 == _float) &&
                         amodTable1 == _normal &&
                         (uRegmaskTable1 &_rplus_r)))
                    {
                            if (asmstate.ucItype == ITfloat)
                                    pc->Irm += popnd1->base->val;
                            else
                                    pc->Iop += popnd1->base->val;
#ifdef DEBUG
                            auchOpcode[usIdx-1] += popnd1->base->val;
#endif
                    }
                    else
                    if (((aoptyTable2 == _reg || aoptyTable2 == _float) &&
                         amodTable2 == _normal &&
                         (uRegmaskTable2 &_rplus_r)))
                    {
                            if (asmstate.ucItype == ITfloat)
                                    pc->Irm += popnd1->base->val;
                            else
                                    pc->Iop += popnd2->base->val;
#ifdef DEBUG
                            auchOpcode[usIdx-1] += popnd2->base->val;
#endif
                    }
                    else
                    {
                        asm_make_modrm_byte(
#ifdef DEBUG
                                auchOpcode, &usIdx,
#endif
                                pc,
                                ptb.pptb1->usFlags,
                                popnd1, popnd2 );
                    }
                    popndTmp = popnd3;
                    aoptyTmp = aoptyTable3;
                    uSizemaskTmp = uSizemaskTable3;

                }
                goto L1;
        }
L2:

        if ((pc->Iop & ~7) == 0xD8 &&
            ADDFWAIT() &&
            !(ptb.pptb0->usFlags & _nfwait))
                pc->Iflags |= CFwait;
        else if ((ptb.pptb0->usFlags & _fwait) &&
            config.target_cpu >= TARGET_80386)
                pc->Iflags |= CFwait;

#ifdef DEBUG
        if (debuga)
        {   unsigned u;

            for (u = 0; u < usIdx; u++)
                printf( "  %02X", auchOpcode[u] );

            if (popPrefix)
                printf( "\t%s\t", asm_opstr(popPrefix) );

            printf( "\t%s\t", asm_opstr(pop) );
            if (popnd1)
                asm_output_popnd( popnd1 );
            if (popnd2) {
                printf( "," );
                asm_output_popnd( popnd2 );
            }
            if (popnd3) {
                printf( "," );
                asm_output_popnd( popnd3 );
            }
            printf("\n");
        }
#endif
        pc = cat( pcPrefix, pc );
        if (configv.addlinenumbers)
        {       code *pcLin;

                pcLin = genlinnum( NULL, srcpos );
                pc = cat( pcLin, pc );
        }
        curblock->Bcode = cat( curblock->Bcode, pc );
}

/*******************************
 */

STATIC void asm_error(void)
{
    while (1)
    {   switch (tok.TKval)
        {   case TKeol:
            case TKsemi:
            case TKeof:
            case TK_asm:
            case TKasm:
            case TKrcur:
                longjmp(asmstate.env,1);
        }
        asm_token();
    }
}

/*******************************
 */

STATIC void asmerr(int errnum)
{
    synerr(errnum);
    asm_error();
}

/*******************************
 */

STATIC opflag_t asm_float_type_size( type * ptype, opflag_t *pusFloat )
{
    *pusFloat = 0;

    if (!ptype) {
        *pusFloat = _fanysize;
        return _anysize;
    }

    if (tyscalar(ptype->Tty)) {
        switch (type_size(ptype)) {
            case 2:
                return( _16 );
            case 4:
                return( _32 );
            case 8:
                *pusFloat = _f64;
                return 0;
            case 10:
                *pusFloat = _f80;
                return 0;
            default:
                *pusFloat = _fanysize;
                return( _anysize );
        }
    }
    else {
        *pusFloat = _fanysize;
        return _anysize;
    }
}

/*******************************
 */

STATIC int asm_isint( OPND *o)
{
        if (!o || o->base || o->s)
                return 0;
        return o->disp != 0;
}

/*******************************
 */

STATIC int asm_is_fpreg( char *szReg )
{
        return( szReg[2] == '\0' && (szReg[0] == 's' || szReg[0] == 'S') &&
                (szReg[1] == 't' || szReg[1] == 'T' ));
}

/*******************************
 * Merge operands o1 and o2 into a single operand.
 */

STATIC OPND * asm_merge_opnds( OPND * o1, OPND * o2 )
{
#ifdef DEBUG
    char *psz;
#endif
#ifdef DEBUG
    if (debuga)
    {   printf("In merge operands /");
        if (o1) asm_output_popnd( o1 );
        printf(",");
        if (o2) asm_output_popnd( o2 );
        printf("/\n");
    }
#endif
        if (!o1)
                return o2;
        if (!o2)
                return o1;
#ifdef EXTRA_DEBUG
        printf( "Combining Operands: mult1 = %d, mult2 = %d",
                o1->uchMultiplier, o2->uchMultiplier );
#endif
        /*      combine the OPND's disp field */
        if (o2->segreg) {
            if (o1->segreg) {
#ifdef DEBUG
                psz = "o1->segement && o2->segreg";
#endif
                goto ILLEGAL_ADDRESS_ERROR;
            }
            else
                o1->segreg = o2->segreg;
        }

        if (o1->disp && o2->disp)
                o1->disp += o2->disp;
        else if (o2->disp)
                o1->disp = o2->disp;

        /* combine the OPND's symbol field */
        if (o1->s && o2->s)
        {
#ifdef DEBUG
            psz = "o1->s && os->s";
#endif
ILLEGAL_ADDRESS_ERROR:
#ifdef DEBUG
            printf("Invalid addr because /%s/\n", psz);
#endif

            asmerr(EM_bad_addr_mode);           // illegal addressing mode
        }
        else if (o2->s)
                o1->s = o2->s;

        /* combine the OPND's base field */
        if (o1->base != NULL && o2->base != NULL) {
#ifdef DEBUG
                psz = "o1->base != NULL && o2->base != NULL";
#endif
                goto ILLEGAL_ADDRESS_ERROR;
        }
        else if (o2->base)
                o1->base = o2->base;

        /* Combine the displacement register fields */
        if (o2->pregDisp1) {
                if (o1->pregDisp2) {
#ifdef DEBUG
                psz = "o2->pregDisp1 && o1->pregDisp2";
#endif
                        goto ILLEGAL_ADDRESS_ERROR;
                }
                else
                if (o1->pregDisp1) {
                        if (o1->uchMultiplier ||
                                (o2->pregDisp1->val == _ESP &&
                                (o2->pregDisp1->ty & _r32) &&
                                !o2->uchMultiplier )) {
                                o1->pregDisp2 = o1->pregDisp1;
                                o1->pregDisp1 = o2->pregDisp1;
                        }
                        else
                                o1->pregDisp2 = o2->pregDisp1;
                }
                else
                        o1->pregDisp1 = o2->pregDisp1;
        }
        if (o2->pregDisp2) {
                if (o1->pregDisp2) {
#ifdef DEBUG
                psz = "o1->pregDisp2 && o2->pregDisp2";
#endif
                        goto ILLEGAL_ADDRESS_ERROR;
                }
                else
                        o1->pregDisp2 = o2->pregDisp2;
        }
        if (o2->uchMultiplier) {
                if (o1->uchMultiplier) {
#ifdef DEBUG
                psz = "o1->uchMultiplier && o2->uchMultiplier";
#endif
                        goto ILLEGAL_ADDRESS_ERROR;
                }
                else
                        o1->uchMultiplier = o2->uchMultiplier;
        }
        if (o2->ptype && !o1->ptype)
            type_settype(&o1->ptype,o2->ptype);
        if (o2->bOffset)
            o1->bOffset = o2->bOffset;
        if (o2->bSeg)
            o1->bSeg = o2->bSeg;

        if (o2->ajt && !o1->ajt)
            o1->ajt = o2->ajt;

        opnd_free (o2);
#ifdef EXTRA_DEBUG
        printf( "Result = %d\n",
                o1->uchMultiplier );
#endif
#ifdef DEBUG
        if (debuga)
        {   printf( "Merge result = /");
            asm_output_popnd( o1 );
            printf( "/\n");
        }
#endif
        return o1;
}

/***************************************
 */

STATIC void asm_merge_symbol(OPND *o1,symbol *s)
{   type *ptype;

    switch (s->Sclass)
    {   case SCconst:
            o1->disp = el_tolong(s->Svalue);
            goto L3;

        case SCregpar:
        case SCparameter:
            curblock->bIasmrefparam = TRUE;
            break;

        case SCextern:
        case SCstatic:
        case SClocstat:
        case SCglobal:
        case SCcomdef:
        case SCinline:
        case SCcomdat:
            if (tyfunc(s->Stype->Tty))
                nwc_mustwrite(s);       // must write out function
            else if (s->Sdt)            // if initializer for symbol
                outdata(s);     // write out data for symbol
            s->Sflags |= SFLlivexit;
            break;
        case SCmember:
        case SCfield:
            o1->disp += s->Smemoff;
            goto L2;
        case SCstruct:
            goto L2;
    }
    o1->s = s;  // a C identifier
L2:
    for (ptype = s->Stype;
        ptype && tybasic(ptype->Tty) == TYarray;
        ptype = ptype->Tnext) ;
    if (!ptype)
        ptype = s->Stype;
    type_settype(&o1->ptype,ptype);
L3:
    ;
}

/****************************
 * Fill in the modregrm and sib bytes of code.
 */

STATIC void asm_make_modrm_byte(
#ifdef DEBUG
        unsigned char *puchOpcode, unsigned *pusIdx,
#endif
        code *pc,
        unsigned usFlags,
        OPND * popnd, OPND * popnd2 )
{
    #undef modregrm

    typedef union {
        unsigned char   uchOpcode;
        struct {
            unsigned rm  : 3;
            unsigned reg : 3;
            unsigned mod : 2;
        } modregrm;
    } MODRM_BYTE;                       // mrmb

    typedef union {
        unsigned char   uchOpcode;
        struct {
            unsigned base  : 3;
            unsigned index : 3;
            unsigned ss    : 2;
        } sib;
    } SIB_BYTE;


        MODRM_BYTE      mrmb = { 0 };
        SIB_BYTE        sib = { 0 };
        char            bSib = FALSE;
        char            bDisp = FALSE;
        char            b32bit = FALSE;
        unsigned char *puc;
        char            bModset = FALSE;
        symbol *        s;

        unsigned            uSizemask =0;
        ASM_OPERAND_TYPE    aopty;
        ASM_MODIFIERS       amod;
        unsigned            uRegmask;
        unsigned char       bOffsetsym = FALSE;

        uSizemask = ASM_GET_uSizemask( popnd->usFlags );
        aopty = ASM_GET_aopty(popnd->usFlags );
        amod = ASM_GET_amod( popnd->usFlags );
        uRegmask = ASM_GET_uRegmask( popnd->usFlags );
        s = popnd->s;
        if (s)
        {
                if ( amod == _fn16 || amod == _fn32)
                {
                        pc->Iflags |= CFoff;
#ifdef DEBUG
                        puchOpcode[(*pusIdx)++] = 0;
                        puchOpcode[(*pusIdx)++] = 0;
#endif
                        if (aopty == _m || aopty == _mnoi) {
                                pc->IFL1 = FLdata;
                                pc->IEVsym1 = s;
                                pc->IEVoffset1 = 0;
                        }
                        else {
                                if (aopty == _p)
                                    pc->Iflags |= CFseg;
#ifdef DEBUG
                                if (aopty == _p || aopty == _rel)
                                {   puchOpcode[(*pusIdx)++] = 0;
                                    puchOpcode[(*pusIdx)++] = 0;
                                }
#endif
                                pc->IFL2 = FLfunc;
                                pc->IEVsym2 = s;
                                pc->IEVoffset2 = 0;
                                return;
                        }
                }
                else
                {
                    if (s->Sclass == SClabel)
                    {
                        if (isdollar(s->Sident))
                        {
                            pc->IFL1 = FLconst;
                            if (uSizemask & (_8 | _16))
                                pc->IEVint1 = popnd->disp;
                            else if (uSizemask & _32)
                                pc->IEVpointer1 = (targ_size_t) popnd->disp;
                        }
                        else
                        {   pc->IFL1 = FLblockoff;
                            pc->IEV1.Vblock = s->Slabelblk;
                        }
                    }
                    else
                    {
#ifdef DEBUG
                        if (debuga)
                            dbg_printf("Setting up symbol %s\n", popnd->s->Sident);
#endif
                        pc->IFL1 = popnd->s->Sfl;
                        pc->IEVsym1 = popnd->s;
                        pc->Iflags |= CFoff;
                        pc->IEVoffset1 = popnd->disp;
                    }
                }
        }
        mrmb.modregrm.reg = usFlags & NUM_MASK;

        if (s && (aopty == _m || aopty == _mnoi))
        {
            switch (s->Sfl)
            {
                case FLextern:
                case FLdata:
                case FLudata:
                    if ((I32 && amod == _addr16) ||
                        (!I32 && amod == _addr32))
                        asmerr(EM_bad_addr_mode);               // illegal addressing mode
DATA_REF:
                    mrmb.modregrm.rm = BPRM;
                    if (amod == _addr16 || amod == _addr32)
                        mrmb.modregrm.mod = 0x2;
                    else
                        mrmb.modregrm.mod = 0x0;
                    break;
                case FLcsdata:
                    pc->Iflags |= CFcs;
                    goto DATA_REF;
                case FLfardata:
                    pc->Iflags |= CFes;
                    goto DATA_REF;

                case FLlocalsize:
                    goto DATA_REF;

                default:
                    mrmb.modregrm.rm = BPRM;
                    mrmb.modregrm.mod = 0x2;
                    switch (s->Sclass)
                    {
                        case SCauto:
                        case SCregister:
                                pc->IFL1 = FLauto;
                                break;
                        case SCfastpar:
                                pc->IFL1 = FLfast;
                                break;
                        case SCshadowreg:
                        case SCregpar:
                        case SCparameter:
                                pc->IFL1 = FLpara;
                                break;
                        case SCtypedef:
                                pc->IFL1 = FLconst;
                                mrmb.modregrm.mod = 0x0;
                                break;

                        case SCextern:
                        case SCglobal:
                        case SCstatic:
                        case SCcomdat:
                        case SCinline:
                        case SCcomdef:
                                pc->IFL1 = s->Sfl;
                                goto DATA_REF;

                        case SClabel:
                                break;          // already taken care of

                        default:
                                WRFL((enum FL)s->Sfl);
                                asmerr( EM_bad_addr_mode );     // illegal addressing mode
                    }
                    break;
            }
        }

        if (aopty == _reg || amod == _rspecial) {
                mrmb.modregrm.mod = 0x3;
                mrmb.modregrm.rm |= popnd->base->val;
        }
        else if (amod == _addr16 || (amod == _flbl && !I32))
        {   unsigned rm;

#ifdef DEBUG
            if (debuga)
                printf("This is an ADDR16\n");
#endif
            if (!popnd->pregDisp1)
            {   rm = 0x6;
                if (!s)
                    bDisp = TRUE;
            }
            else
            {   unsigned r1r2;
                #define X(r1,r2)        (((r1) * 16) + (r2))
                #define Y(r1)           X(r1,9)


                if (popnd->pregDisp2)
                    r1r2 = X(popnd->pregDisp1->val,popnd->pregDisp2->val);
                else
                    r1r2 = Y(popnd->pregDisp1->val);
                switch (r1r2)
                {
                    case X(_BX,_SI):    rm = 0; break;
                    case X(_BX,_DI):    rm = 1; break;
                    case Y(_BX):        rm = 7; break;

                    case X(_BP,_SI):    rm = 2; break;
                    case X(_BP,_DI):    rm = 3; break;
                    case Y(_BP):        rm = 6; bDisp = TRUE;   break;

                    case X(_SI,_BX):    rm = 0; break;
                    case X(_SI,_BP):    rm = 2; break;
                    case Y(_SI):        rm = 4; break;

                    case X(_DI,_BX):    rm = 1; break;
                    case X(_DI,_BP):    rm = 3; break;
                    case Y(_DI):        rm = 5; break;

                    default:
                        asmerr(EM_bad_addr_mode);       // illegal addressing mode
                }
                #undef X
                #undef Y
            }
            mrmb.modregrm.rm = rm;

#ifdef DEBUG
            if (debuga)
                printf("This is an mod = %d, popnd->s =%ld, popnd->disp = %ld\n",
                   mrmb.modregrm.mod, s, popnd->disp);
#endif
                if (!s || (!mrmb.modregrm.mod && popnd->disp))
                {
                    if ((!popnd->disp && !bDisp) ||
                        !popnd->pregDisp1)
                        mrmb.modregrm.mod = 0x0;
                    else
                    if (popnd->disp >= CHAR_MIN &&
                        popnd->disp <= SCHAR_MAX)
                        mrmb.modregrm.mod = 0x1;
                    else
                        mrmb.modregrm.mod = 0X2;
                }
                else
                    bOffsetsym = TRUE;

        }
        else if (amod == _addr32 || (amod == _flbl && I32))
        {
#ifdef DEBUG
            if (debuga)
                printf("This is an ADDR32\n");
#endif
            if (!popnd->pregDisp1)
                mrmb.modregrm.rm = 0x5;
            else if (popnd->pregDisp2 ||
                     popnd->uchMultiplier ||
                     popnd->pregDisp1->val == _ESP)
            {
                if (popnd->pregDisp2)
                {   if (popnd->pregDisp2->val == _ESP)
                        asmerr(EM_bad_addr_mode);       // illegal addressing mode
                }
                else
                {   if (popnd->uchMultiplier &&
                        popnd->pregDisp1->val ==_ESP)
                        asmerr( EM_bad_addr_mode );     // illegal addressing mode
                    bDisp = TRUE;
                }

                mrmb.modregrm.rm = 0x4;
                bSib = TRUE;
                if (bDisp)
                {
                    if (!popnd->uchMultiplier &&
                        popnd->pregDisp1->val==_ESP)
                    {
                        sib.sib.base = popnd->pregDisp1->val;
                        sib.sib.index = 0x4;
                    }
                    else
                    {
#ifdef DEBUG
                        if (debuga)
                            printf("Resetting the mod to 0\n");
#endif
                        if (popnd->pregDisp2)
                        {
                            if (popnd->pregDisp2->val != _EBP)
                                asmerr( EM_bad_addr_mode );     // illegal addressing mode
                        }
                        else
                        {   mrmb.modregrm.mod = 0x0;
                            bModset = TRUE;
                        }

                        sib.sib.base = 0x5;
                        sib.sib.index = popnd->pregDisp1->val;
                    }
                }
                else
                {
                    sib.sib.base = popnd->pregDisp1->val;
                    //
                    // This is to handle the special case
                    // of using the EBP register and no
                    // displacement.  You must put in an
                    // 8 byte displacement in order to
                    // get the correct opcodes.
                    //
                    if (popnd->pregDisp1->val == _EBP &&
                        (!popnd->disp && !s))
                    {
#ifdef DEBUG
                        if (debuga)
                            printf("Setting the mod to 1 in the _EBP case\n");
#endif
                        mrmb.modregrm.mod = 0x1;
                        bDisp = TRUE;   // Need a
                                        // displacement
                        bModset = TRUE;
                    }

                    sib.sib.index = popnd->pregDisp2->val;
                }
                switch (popnd->uchMultiplier)
                {
                    case 0:     sib.sib.ss = 0; break;
                    case 2:     sib.sib.ss = 1; break;
                    case 4:     sib.sib.ss = 2; break;
                    case 8:     sib.sib.ss = 3; break;

                    default:
                        asmerr( EM_bad_addr_mode );             // illegal addressing mode
                        break;
                }
                if (bDisp && sib.sib.base == 0x5)
                    b32bit = TRUE;
            }
            else
            {   unsigned rm;

                if (popnd->uchMultiplier)
                    asmerr( EM_bad_addr_mode );         // illegal addressing mode
                switch (popnd->pregDisp1->val)
                {
                    case _EAX:  rm = 0; break;
                    case _ECX:  rm = 1; break;
                    case _EDX:  rm = 2; break;
                    case _EBX:  rm = 3; break;
                    case _ESI:  rm = 6; break;
                    case _EDI:  rm = 7; break;

                    case _EBP:
                        if (!popnd->disp && !s)
                        {
                            mrmb.modregrm.mod = 0x1;
                            bDisp = TRUE;   // Need a displacement
                            bModset = TRUE;
                        }
                        rm = 5;
                        break;

                    default:
                        asmerr(EM_bad_addr_mode);       // illegal addressing mode
                        break;
                }
                mrmb.modregrm.rm = rm;
            }
            if (!bModset && (!s ||
                    (!mrmb.modregrm.mod && popnd->disp)))
            {
                if ((!popnd->disp && !mrmb.modregrm.mod) ||
                    (!popnd->pregDisp1 && !popnd->pregDisp2))
                {   mrmb.modregrm.mod = 0x0;
                    bDisp = TRUE;
                }
                else if (popnd->disp >= CHAR_MIN &&
                         popnd->disp <= SCHAR_MAX)
                    mrmb.modregrm.mod = 0x1;
                else
                    mrmb.modregrm.mod = 0x2;
            }
            else
                bOffsetsym = TRUE;
        }
        if (popnd2 && !mrmb.modregrm.reg &&
            asmstate.ucItype != ITshift &&
            (ASM_GET_aopty( popnd2->usFlags ) == _reg  ||
             ASM_GET_amod( popnd2->usFlags ) == _rseg ||
             ASM_GET_amod( popnd2->usFlags ) == _rspecial))
        {
                mrmb.modregrm.reg =  popnd2->base->val;
        }
#ifdef DEBUG
        puchOpcode[ (*pusIdx)++ ] = mrmb.uchOpcode;
#endif
        pc->Irm = mrmb.uchOpcode;
        if (bSib)
        {
#ifdef DEBUG
                puchOpcode[ (*pusIdx)++ ] = sib.uchOpcode;
#endif
                pc->Isib= sib.uchOpcode;
        }
        if ((!s || (popnd->pregDisp1 && !bOffsetsym)) &&
            aopty != _imm &&
            (popnd->disp || bDisp))
        {
                if (popnd->usFlags & _a16)
                {
#ifdef DEBUG
                        puc = ((unsigned char *) &(popnd->disp));
                        puchOpcode[(*pusIdx)++] = puc[1];
                        puchOpcode[(*pusIdx)++] = puc[0];
#endif
                        if (usFlags & (_modrm | NUM_MASK)) {
#ifdef DEBUG
                            if (debuga)
                                printf("Setting up value %ld\n", popnd->disp);
#endif
                            pc->IEVint1 = popnd->disp;
                            pc->IFL1 = FLconst;
                        }
                        else {
                            pc->IEVint2 = popnd->disp;
                            pc->IFL2 = FLconst;
                        }

                }
                else
                {
#ifdef DEBUG
                        puc = ((unsigned char *) &(popnd->disp));
                        puchOpcode[(*pusIdx)++] = puc[3];
                        puchOpcode[(*pusIdx)++] = puc[2];
                        puchOpcode[(*pusIdx)++] = puc[1];
                        puchOpcode[(*pusIdx)++] = puc[0];
#endif
                        if (usFlags & (_modrm | NUM_MASK)) {
#ifdef DEBUG
                            if (debuga)
                                printf("Setting up value %ld\n", popnd->disp);
#endif
                                pc->IEVpointer1 = (targ_size_t) popnd->disp;
                                pc->IFL1 = FLconst;
                        }
                        else {
                                pc->IEVpointer2 = (targ_size_t) popnd->disp;
                                pc->IFL2 = FLconst;
                        }

                }
        }
}

/*******************************
 */

STATIC regm_t asm_modify_regs( PTRNTAB ptb, OPND * popnd1, OPND * popnd2 )
{
    regm_t usRet = 0;

    switch (ptb.pptb0->usFlags & MOD_MASK) {
    case _modsi:
        usRet |= mSI;
        break;
    case _moddx:
        usRet |= mDX;
        break;
    case _mod2:
        if (popnd2)
            usRet |= asm_modify_regs( ptb, popnd2, NULL );
        break;
    case _modax:
        usRet |= mAX;
        break;
    case _modnot1:
        popnd1 = NULL;
        break;
    case _modaxdx:
        usRet |= (mAX | mDX);
        break;
    case _moddi:
        usRet |= mDI;
        break;
    case _modsidi:
        usRet |= (mSI | mDI);
        break;
    case _modcx:
        usRet |= mCX;
        break;
    case _modes:
        usRet |= mES;
        break;
    case _modall:
        asmstate.bReturnax = TRUE;
        return mES | ALLREGS;
    case _modsiax:
        usRet |= (mSI | mAX);
        break;
    case _modsinot1:
        usRet |= mSI;
        popnd1 = NULL;
        break;
    }
    if (popnd1 && ASM_GET_aopty(popnd1->usFlags) == _reg) {
        switch (ASM_GET_amod( popnd1->usFlags)) {
        default:
            if (ASM_GET_uSizemask(popnd1->usFlags) == _8) {
                switch( popnd1->base->val ) {
                    case _AL:
                    case _AH:
                        usRet |= mAX;
                        break;
                    case _BL:
                    case _BH:
                        usRet |= mBX;
                        break;
                    case _CL:
                    case _CH:
                        usRet |= mCX;
                        break;
                    case _DL:
                    case _DH:
                        usRet |= mDX;
                        break;
                    default:
                        assert(0);
                }
            }
            else {
                switch (popnd1->base->val) {
                    case _AX:
                        usRet |= mAX;
                        break;
                    case _BX:
                        usRet |= mBX;
                        break;
                    case _CX:
                        usRet |= mCX;
                        break;
                    case _DX:
                        usRet |= mDX;
                        break;
                    case _SI:
                        usRet |= mSI;
                        break;
                    case _DI:
                        usRet |= mDI;
                        break;
                }
            }
            break;
        case _rseg:
            if (popnd1->base->val == _ES)
                usRet |= mES;
            break;

        case _rspecial:
            break;
        }
    }
    if (usRet & mAX)
        asmstate.bReturnax = TRUE;

    return usRet;
}

/*******************************
 * Match flags in operand against flags in opcode table.
 * Returns:
 *      !=0 if match
 */

STATIC unsigned char asm_match_flags( opflag_t usOp,
                opflag_t usTable )
{
    ASM_OPERAND_TYPE    aoptyTable;
    ASM_OPERAND_TYPE    aoptyOp;
    ASM_MODIFIERS       amodTable;
    ASM_MODIFIERS       amodOp;
    unsigned            uRegmaskTable;
    unsigned            uRegmaskOp;
    unsigned char       bRegmatch;
    unsigned char       bRetval = FALSE;
    unsigned            uSizemaskOp;
    unsigned            uSizemaskTable;
    unsigned            bSizematch;

    //printf("asm_match_flags(usOp = x%x, usTable = x%x)\n", usOp, usTable);
    if (asmstate.ucItype == ITfloat)
    {
        bRetval = asm_match_float_flags(usOp, usTable);
        goto EXIT;
    }

    uSizemaskOp = ASM_GET_uSizemask(usOp);
    uSizemaskTable = ASM_GET_uSizemask(usTable);

    // Check #1, if the sizes do not match, NO match
    bSizematch =  (uSizemaskOp & uSizemaskTable);

    amodOp = ASM_GET_amod(usOp);

    aoptyTable = ASM_GET_aopty(usTable);
    aoptyOp = ASM_GET_aopty(usOp);

    // _mmm64 matches with a 64 bit mem or an MMX register
    if (usTable == _mmm64)
    {
        if (usOp == _mm)
            goto Lmatch;
        if (aoptyOp == _m && (bSizematch || uSizemaskOp == _anysize))
            goto Lmatch;
        goto EXIT;
    }

    // _xmm_m32, _xmm_m64, _xmm_m128 match with XMM register or memory
    if (usTable == _xmm_m32 ||
        usTable == _xmm_m64 ||
        usTable == _xmm_m128)
    {
        if (usOp == _xmm)
            goto Lmatch;
        if (aoptyOp == _m && (bSizematch || uSizemaskOp == _anysize))
            goto Lmatch;
    }

    if (!bSizematch && uSizemaskTable)
    {
        //printf("no size match\n");
        goto EXIT;
    }


//
// The operand types must match, otherwise return FALSE.
// There is one exception for the _rm which is a table entry which matches
// _reg or _m
//
    if (aoptyTable != aoptyOp)
    {
        if (aoptyTable == _rm && (aoptyOp == _reg ||
                                  aoptyOp == _m ||
                                  aoptyOp == _rel))
            goto Lok;
        if (aoptyTable == _mnoi && aoptyOp == _m &&
            (uSizemaskOp == _32 && amodOp == _addr16 ||
             uSizemaskOp == _48 && amodOp == _addr32 ||
             uSizemaskOp == _48 && amodOp == _normal)
          )
            goto Lok;
        //printf("no operand type match\n");
        goto EXIT;
    }
Lok:

//
// Looks like a match so far, check to see if anything special is going on
//
    amodTable = ASM_GET_amod(usTable);
    uRegmaskOp = ASM_GET_uRegmask(usOp);
    uRegmaskTable = ASM_GET_uRegmask(usTable);
    bRegmatch = ((!uRegmaskTable && !uRegmaskOp) ||
                 (uRegmaskTable & uRegmaskOp));

    switch (amodTable)
    {
    case _normal:               // Normal's match with normals
        switch(amodOp) {
            case _normal:
            case _addr16:
            case _addr32:
            case _fn16:
            case _fn32:
            case _flbl:
                bRetval = (bSizematch || bRegmatch);
                goto EXIT;
            default:
                goto EXIT;
        }
    case _rseg:
    case _rspecial:
        bRetval = (amodOp == amodTable && bRegmatch);
        goto EXIT;

    case _fn32:
    case _fn16:
        bRetval = 0;
        goto EXIT;

    default:
#ifdef DEBUG
        printf("amodTable = x%x\n", amodTable);
#endif
        assert(0);
    }
EXIT:
#if 0
    printf("OP : ");
    asm_output_flags(usOp);
    printf("\nTBL: ");
    asm_output_flags(usTable);
    printf(": %s\n", bRetval ? "MATCH" : "NOMATCH");
#endif
    return bRetval;

Lmatch:
    //printf("match\n");
    return 1;
}

/*******************************
 */

STATIC unsigned char asm_match_float_flags(opflag_t usOp, opflag_t usTable )
{
    ASM_OPERAND_TYPE    aoptyTable;
    ASM_OPERAND_TYPE    aoptyOp;
    ASM_MODIFIERS       amodTable;
    ASM_MODIFIERS       amodOp;
    unsigned            uRegmaskTable;
    unsigned            uRegmaskOp;
    unsigned            bRegmatch;


//
// Check #1, if the sizes do not match, NO match
//
    uRegmaskOp = ASM_GET_uRegmask( usOp );
    uRegmaskTable = ASM_GET_uRegmask( usTable );
    bRegmatch = (uRegmaskTable & uRegmaskOp);

    if (!(ASM_GET_uSizemask( usTable ) & ASM_GET_uSizemask( usOp ) ||
          (bRegmatch)))
        return(FALSE);

    aoptyTable = ASM_GET_aopty( usTable );
    aoptyOp = ASM_GET_aopty( usOp );
//
// The operand types must match, otherwise return FALSE.
// There is one exception for the _rm which is a table entry which matches
// _reg or _m
//
    if (aoptyTable != aoptyOp) {
        if (aoptyOp != _float)
            return(FALSE);
    }

//
// Looks like a match so far, check to see if anything special is going on
//
    amodOp = ASM_GET_amod( usOp );
    amodTable = ASM_GET_amod( usTable );
    switch (amodTable) {
//
// Normal's match with normals
//
    case _normal:
        switch( amodOp ) {
            case _normal:
            case _addr16:
            case _addr32:
            case _fn16:
            case _fn32:
            case _flbl:
                return( TRUE );
            default:
                return(FALSE);
        }
    case _rseg:
    case _rspecial:
        return( FALSE );
    default:
        assert(0);
        return 0;
    }
}

#ifdef DEBUG

/*******************************
 */

STATIC void asm_output_flags( opflag_t usFlags )
{
        ASM_OPERAND_TYPE    aopty = ASM_GET_aopty( usFlags );
        ASM_MODIFIERS       amod = ASM_GET_amod( usFlags );
        unsigned            uRegmask = ASM_GET_uRegmask( usFlags );
        unsigned            uSizemask = ASM_GET_uSizemask( usFlags );

        if (uSizemask == _anysize)
            printf("_anysize ");
        else
        {
            if (uSizemask & _8)
                printf( "_8  " );
            if (uSizemask & _16)
                printf( "_16 " );
            if (uSizemask & _32)
                printf( "_32 " );
            if (uSizemask & _48)
                printf( "_48 " );
        }

        printf("_");
        switch (aopty) {
            case _reg:
                printf( "reg   " );
                break;
            case _m:
                printf( "m     " );
                break;
            case _imm:
                printf( "imm   " );
                break;
            case _rel:
                printf( "rel   " );
                break;
            case _mnoi:
                printf( "mnoi  " );
                break;
            case _p:
                printf( "p     " );
                break;
            case _rm:
                printf( "rm    " );
                break;
            case _float:
                printf( "float " );
                break;
            default:
                printf(" UNKNOWN " );
        }

        printf("_");
        switch (amod) {
            case _normal:
                printf( "normal   " );
                if (uRegmask & 1) printf("_al ");
                if (uRegmask & 2) printf("_ax ");
                if (uRegmask & 4) printf("_eax ");
                if (uRegmask & 8) printf("_dx ");
                if (uRegmask & 0x10) printf("_cl ");
                return;
            case _rseg:
                printf( "rseg     " );
                break;
            case _rspecial:
                printf( "rspecial " );
                break;
            case _addr16:
                printf( "addr16   " );
                break;
            case _addr32:
                printf( "addr32   " );
                break;
            case _fn16:
                printf( "fn16     " );
                break;
            case _fn32:
                printf( "fn32     " );
                break;
            case _flbl:
                printf( "flbl     " );
                break;
            default:
                printf( "UNKNOWN  " );
                break;
        }
        printf( "uRegmask=x%02x", uRegmask );

}

/*******************************
 */

STATIC void asm_output_popnd( OPND * popnd )
{
        if (popnd->segreg)
                printf( "%s:", popnd->segreg->regstr );

        if (popnd->s)
                printf( "%s", popnd->s->Sident );

        if (popnd->base)
                printf( "%s", popnd->base->regstr );
        if (popnd->pregDisp1) {
                if (popnd->pregDisp2) {
                        if (popnd->usFlags & _a32)
                                if (popnd->uchMultiplier)
                                        printf( "[%s][%s*%d]",
                                                popnd->pregDisp1->regstr,
                                                popnd->pregDisp2->regstr,
                                                popnd->uchMultiplier );
                                else
                                        printf( "[%s][%s]",
                                                popnd->pregDisp1->regstr,
                                                popnd->pregDisp2->regstr );
                        else
                                printf( "[%s+%s]",
                                        popnd->pregDisp1->regstr,
                                        popnd->pregDisp2->regstr );
                }
                else {
                        if (popnd->uchMultiplier)
                                printf( "[%s*%d]",
                                        popnd->pregDisp1->regstr,
                                        popnd->uchMultiplier );
                        else
                                printf( "[%s]",
                                        popnd->pregDisp1->regstr );
                }
        }
        if (ASM_GET_aopty(popnd->usFlags) == _imm)
                printf( "%lxh", popnd->disp);
        else
        if (popnd->disp)
                printf( "+%lxh", popnd->disp );
}

#endif

/*******************************
 */

STATIC REG * asm_reg_lookup( char *s)
{
    char szBuf[12];
    int i;

    //dbg_printf("asm_reg_lookup('%s')\n",s);
    if (strlen(s) >= sizeof(szBuf))
        return NULL;
    strcpy(szBuf,s);
    strupr(szBuf);

    for (i = 0; i < sizeof(regtab) / sizeof(regtab[0]); i++)
    {
        if (strcmp(szBuf,regtab[i].regstr) == 0)
            return &regtab[i];
    }
    return NULL;
}


/*******************************
 * Input:
 *      iFlags  PFLmasm         Microsoft style (_asm or __asm)
 *              PFLbasm         Borland style (asm)
 */

int asm_state(int iFlags)
{
        OP *o;
        OP *popPrefix;
        OPND *o1 = NULL,*o2 = NULL, *o3 = NULL;
        PTRNTAB ptb;
        unsigned usNumops;
        unsigned char uchPrefix = 0;
        unsigned char   bAsmseen;
        char    *pszLabel = NULL;
        Srcpos srcpos;

        srcpos.Slinnum = 0;
        pstate.STflags |= iFlags;

        // Scalar return values will always be in AX.  So if it is a scalar
        // then asm block sets return value if it modifies AX, if it is non-scalar
        // then always assume that the ASM block sets up an appropriate return
        // value.

        asmstate.bReturnax = !tyscalar( funcsym_p->Stype->Tnext->Tty );

        if (!asmstate.bInit)
        {
                asmstate.bInit = TRUE;
                init_optab();
                asmstate.psDollar = symbol_calloc( "$" );
                asmstate.psDollar->Sclass = SClabel;
                asmstate.psDollar->Slabel = SHRT_MAX;
                asmstate.psDollar->Sfl = FLconst;

                asmstate.psLocalsize = symbol_calloc( "__LOCAL_SIZE" );
                asmstate.psLocalsize->Sclass = SCconst;
                asmstate.psLocalsize->Sfl = FLlocalsize;
        }
        asm_nextblock();
        asm_token();
        while (tok.TKval == TKeol)
            asm_token();

        if (tok.TKval == TKlcur)
        {       asmstate.bAsm_block = TRUE;
                if (iFlags & PFLmasm)
                {   pstate.STflags |= PFLsemi;  // ';' is now start of comment
                    iFlags |= PFLsemi;  // to reset pstate.STflags on return
                }
                asm_token();
        }
        while (TRUE)
        {
                if (setjmp(asmstate.env))
                {       opnd_free( o1 );
                        opnd_free( o2 );
                        opnd_free( o3 );
                        o1 = o2 = o3 = NULL;
                        if (!asmstate.bAsm_block || tok.TKval == TKeof)
                        {
                                pstate.STflags &= ~iFlags;
                                asm_nextblock();
                                return asmstate.bReturnax;
                        }
                }
                asmstate.Asrcpos = srcpos = token_linnum();
                if (asmstate.bAsm_block)
                {
                        if (tok.TKval == TKeol ||
                            tok.TKval == TK_asm ||
                            tok.TKval == TKasm ||
                            tok.TKval == TKsemi)
                        {
                                bAsmseen = (tok.TKval == TK_asm ||
                                        tok.TKval == TKasm);
                                do
                                {   bAsmseen = (bAsmseen ||
                                            tok.TKval == TKasm ||
                                            tok.TKval == TK_asm);
                                    asm_token();
                                } while (tok.TKval == TKeol ||
                                        tok.TKval == TKsemi ||
                                        (!bAsmseen && (tok.TKval == TK_asm ||
                                        tok.TKval == TKasm) ));
                                asmstate.Asrcpos = srcpos = token_linnum();
                        }
                        if (tok.TKval == TKrcur)
                        {
                                pstate.STflags &= ~iFlags;
                                asmstate.bAsm_block = FALSE;
                                stoken();
                                asm_nextblock();
                                return asmstate.bReturnax;
                        }
                }

                if (tok.TKval == ASMTKeven)
                {
                    asm_token();
                    if (curblock->Bcode)
                        asm_nextblock();
                    curblock->Balign = 2;
                    goto AFTER_EMIT;
                }

                if (tok.TKval == ASMTKalign)
                {   unsigned align;

                    asm_token();
                    if (curblock->Bcode)
                        asm_nextblock();
                    align = msc_getnum();
                    if (ispow2(align) == -1)
                        asmerr(EM_align);       // power of 2 expected
                    else
                        curblock->Balign = align;
                    goto AFTER_EMIT;
                }

                if (tok.TKval == TKint || tok.TKval == ASMTKint)
                    token_setident("int");

                // C++ alternate tokens
                else if (tok.TKval == TKandand)
                    token_setident("and");
                else if (tok.TKval == TKoror)
                    token_setident("or");
                else if (tok.TKval == TKxor)
                    token_setident("xor");
                else if (tok.TKval == TKnot)
                    token_setident("not");

                else if (tok.TKval != TKident)
                {
OPCODE_EXPECTED:
                        asmerr(EM_opcode_exp);  // assembler opcode expected
                }

                popPrefix = NULL;
                o = asm_op_lookup(tok.TKid);
                if (!o)
                {
                        pszLabel = mem_strdup( tok.TKid );
                        if (asm_token() == TKcolon) {
                                dolabel( pszLabel );
                                mem_free( pszLabel );
                                asm_token();
                                continue;
                        }
                        else {
                                mem_free( pszLabel );
                                goto OPCODE_EXPECTED;
                        }
                }
                asmstate.Asrcpos = srcpos = token_linnum();
                asmstate.ucItype = o->usNumops & ITMASK;
                asm_token();
                if (o->usNumops > 3)
                {
                    switch (asmstate.ucItype)
                    {
                        case ITprefix:
                                // special case for LOCK prefix, parse and
                                // validate instruction
                                if (tok.TKval != TKident &&
                                    tok.TKval != TKint &&
                                    tok.TKval != ASMTKint)
                                {
                                    asmerr(EM_prefix);  // prefix must be followed by assembler opcode
                                    break;
                                }

                                popPrefix = o;
                                o = asm_op_lookup(tok.TKid);
                                if (!o)
                                    asmerr(EM_prefix);
                                asmstate.ucItype = o->usNumops & ITMASK;
                                asm_token();
                                break;

                        case ITdata:
                                asm_db_parse(o);
                                goto AFTER_EMIT;

                        case ITaddr:
                                asm_da_parse(o);
                                goto AFTER_EMIT;
                    }
                }
                /* get the first part of an expr */
                o1 = asm_cond_exp();
                if (tok.TKval == TKcomma)
                {
                        asm_token();
                        o2 = asm_cond_exp();
                }
                if (tok.TKval == TKcomma)
                {
                        asm_token();
                        o3 = asm_cond_exp();
                }
                //match opcode and operands in ptrntab to verify legal inst and
                // generate

                ptb = asm_classify( o, o1, o2, o3, &usNumops );
                assert( ptb.pptb0 );
//
// The Multiply instruction takes 3 operands, but if only 2 are seen
// then the third should be the second and the second should
// be a duplicate of the first.
//

                if (asmstate.ucItype == ITopt &&
                        (usNumops == 2) &&
                        (ASM_GET_aopty( o2->usFlags ) == _imm) &&
                        ((o->usNumops & ITSIZE) == 3))
                {
                        o3 = o2;
                        o2 = opnd_calloc();
                        *o2 = *o1;
//
// Re-classify the opcode because the first classification
// assumed 2 operands.
//
                        ptb = asm_classify( o, o1, o2, o3, &usNumops );
                }
#if 0
                else
                if (asmstate.ucItype == ITshift && (ptb.pptb2->usOp2 == 0 ||
                        (ptb.pptb2->usOp2 & _cl))) {
                        opnd_free( o2 );
                        o2 = NULL;
                        usNumops = 1;
                }
#endif
                asm_emit( srcpos, usNumops, ptb, popPrefix, o, o1, o2, o3 );
                if (asmstate.ucItype == ITjump &&
                    o1 && o1->s && o1->s->Sclass == SClabel &&
                    o1->s != asmstate.psDollar)
                {       block *b;

                        b = curblock;
                        asm_nextblock();
                        list_append( &b->Bsucc, o1->s->Slabelblk );
                }
AFTER_EMIT:
                opnd_free( o1 );
                opnd_free( o2 );
                opnd_free( o3 );
                o1 = o2 = o3 = NULL;

                if (!asmstate.bAsm_block)
                {
                        pstate.STflags &= ~iFlags;
                        if (tok.TKval == TKeol)
                                asm_token();
                        opttok(TKsemi);
                        asm_nextblock();
                        return asmstate.bReturnax;
                }
                else
                {
                    switch (tok.TKval)
                    {
                        case TKeol:
                        case TK_asm:
                        case TKasm:
                        case TKrcur:
                        case TKsemi:
                                break;
                        default:
                                asmerr(EM_eol); // end of line expected
                                break;
                    }
                }
        }
        pstate.STflags &= ~iFlags;
        return asmstate.bReturnax;
}

/*******************************
 */

STATIC enum_TK asm_token (void )
{
    char    szBuf[20];
    ASMTK   asmtk;

    // This kludge is to prevent the scanner from dumping the current
    // line buffer until after error checking is done, so that the correct
    // line is dumped out.
    if (tok.TKval != TKeol && xc == LF
        && !toklist
       )
        tok.TKval = TKeol;
    else
    {
        stoken();
        if (tok.TKval == TKident)
        {   size_t len;

            len = strlen(tok.TKid);
            if (len >= sizeof(szBuf) )
                goto Lret;
            memcpy( szBuf, tok.TKid, len + 1);
            strupr(szBuf);
            asmtk = (ASMTK) binary( szBuf, apszAsmtk, ASMTKmax );
            if (asmtk != -1)
                tok.TKval = (enum_TK) (asmtk + TKMAX + 1);
        }
    }
Lret:
    return tok.TKval;
}

/*******************************
 */

STATIC unsigned asm_type_size( type * ptype )
{   unsigned u;

    u = _anysize;
    if (ptype &&
        (tyscalar(ptype->Tty) || tybasic(ptype->Tty) == TYstruct))
    {
        switch (type_size(ptype))
        {   case 1:     u = _8;         break;
            case 2:     u = _16;        break;
            case 4:     u = _32;        break;
            case 6:     u = _48;        break;
        }
    }
    return u;
}

/*******************************
 *      start of inline assemblers expression parser
 *      NOTE: functions in call order instead of alphabetical
 */

/*******************************************
 * Parse DA expression
 *
 * Very limited define address to place a code
 * address in the assembly
 * Problems:
 *      o       Should use dw offset and dd offset instead,
 *              for near/far support.
 *      o       Should be able to add an offset to the label address.
 *      o       Blocks addressed by DA should get their Bpred set correctly
 *              for optimizer.
 */

STATIC void asm_da_parse( OP *pop )
{
    CodeBuilder cb;
    while (1)
    {
        if (tok.TKval == TKident)
        {
            symbol *s = scope_search(tok.TKid,SCTlabel);
            if (!s)
            {   //Assume it is a forward referenced label
                s = asm_define_label(tok.TKid);
            }
            if (configv.addlinenumbers)
                cb.genlinnum(asmstate.Asrcpos);
            cb.genasm(s->Slabelblk);
        }
        else
            asmerr(EM_bad_addr_mode);   // illegal addressing mode
        asm_token();
        if (tok.TKval != TKcomma)
            break;
        asm_token();
    }


    curblock->Bcode = cat( curblock->Bcode, cb.finish() );
    curblock->usIasmregs |= mES|ALLREGS;
    asmstate.bReturnax = TRUE;
}

/*******************************************
 * Parse DB, DW, DD, DQ and DT expressions.
 */

STATIC void asm_db_parse( OP *pop )
{
    union DT
    {   targ_ullong ul;
        targ_float f;
        targ_double d;
        targ_ldouble ld;
        char value[10];
    } dt;

    unsigned usSize = pop->usNumops & ITSIZE;

    unsigned usBytes = 0;
    unsigned usMaxbytes = 0;
    char *bytes = NULL;

    while (1)
    {
        if (usBytes+usSize > usMaxbytes)
        {   usMaxbytes = usBytes + usSize + 10;
            bytes = (char *)mem_realloc( bytes,usMaxbytes );
        }
        elem *e = CPP ? assign_exp() : const_exp();
        e = poptelem(e);
        if (e->Eoper == OPconst)                // if result is a constant
        {   targ_ullong ul;
            targ_ldouble ld;

            if (sizeof(targ_ldouble) < 10)
                memset(&dt,0,sizeof(dt));

            ul = el_tolong(e);
            ld = el_toldouble(e);
            if (tyintegral(e->ET->Tty))
            {   dt.ul = ul;
                if (usSize == 10)               // if long double
                    dt.ld = ld;
            }
            else
            {
                switch (usSize)
                {
                    case 1:
                    case 2:
                        dt.ul = ul;
                        break;
                    case 4:
                        dt.f = ld;
                        break;
                    case 8:
                        dt.d = ld;
                        break;
                    case 10:
                        dt.ld = ld;
                        break;
                    default:
                        assert(0);
                }
            }
            memcpy(bytes + usBytes, &dt, usSize);
            usBytes += usSize;
        }
        else if (e->Eoper == OPstring)
        {   size_t len = e->EV.ss.Vstrlen;

            if (len)
            {   len--;                          // leave off terminating 0
                bytes = (char *)mem_realloc( bytes, usMaxbytes + len );
                memcpy(bytes + usBytes, e->EV.ss.Vstring,len);
                usBytes += len;
            }
        }
        else
        {
            synerr(EM_const_init);              // constant initializer
        }
        el_free(e);

        if (tok.TKval != TKcomma)
            break;
        asm_token();
    }

    CodeBuilder cb;
    if (configv.addlinenumbers)
        cb.genlinnum(asmstate.Asrcpos);
    cb.genasm(bytes, usBytes);
    mem_free(bytes);

    curblock->Bcode = cat( curblock->Bcode, cb.finish() );
    curblock->usIasmregs |= mES|ALLREGS;
    asmstate.bReturnax = TRUE;
}

/*******************************
 */

STATIC OPND * asm_cond_exp(void)
{
        OPND *o1,*o2,*o3;

        o1 = asm_log_or_exp();
        if (tok.TKval == TKques)
        {
                asm_token();
                o2 = asm_cond_exp();
                asm_token();
                asm_chktok(TKcolon,EM_colon);
                o3 = asm_cond_exp();
                o1 = (o1->disp) ? o2 : o3;
        }
        return o1;
}

/*******************************
 */

STATIC OPND * asm_log_or_exp(void)
{
        register OPND *o1,*o2;

        o1 = asm_log_and_exp();
        while (tok.TKval == TKoror)
        {
                asm_token();
                o2 = asm_log_and_exp();
                if (asm_isint(o1) && asm_isint(o2))
                    o1->disp = o1->disp || o2->disp;
                else
                    asmerr(EM_bad_operand);             // illegal operand
                o2->disp = 0;
                o1 = asm_merge_opnds( o1, o2 );
        }
        return o1;
}

/*******************************
 */

STATIC OPND * asm_log_and_exp(void)
{
        register OPND *o1,*o2;

        o1 = asm_inc_or_exp();
        while (tok.TKval == TKandand)
        {
                asm_token();
                o2 = asm_inc_or_exp();
                if (asm_isint(o1) && asm_isint(o2))
                        o1->disp = o1->disp && o2->disp;
                else {
                        asmerr(EM_bad_operand);         // illegal operand
                }
                o2->disp = 0;
                o1 = asm_merge_opnds( o1, o2 );
        }
        return o1;
}

/*******************************
 */

STATIC OPND * asm_inc_or_exp(void)
{
        register OPND *o1,*o2;

        o1 = asm_xor_exp();
        while (tok.TKval == TKor)
        {
                asm_token();
                o2 = asm_xor_exp();
                if (asm_isint(o1) && asm_isint(o2))
                        o1->disp |= o2->disp;
                else {
                        asmerr(EM_bad_operand);         // illegal operand
                }
                o2->disp = 0;
                o1 = asm_merge_opnds( o1, o2 );
        }
        return o1;
}

/*******************************
 */

STATIC OPND * asm_xor_exp(void)
{
        register OPND *o1,*o2;

        o1 = asm_and_exp();
        while (tok.TKval == TKxor)
        {
                asm_token();
                o2 = asm_and_exp();
                if (asm_isint(o1) && asm_isint(o2))
                        o1->disp ^= o2->disp;
                else {
                        asmerr(EM_bad_operand);         // illegal operand
                }
                o2->disp = 0;
                o1 = asm_merge_opnds( o1, o2 );
        }
        return o1;
}

/*******************************
 */

STATIC OPND * asm_and_exp(void)
{
        register OPND *o1,*o2;

        o1 = asm_equal_exp();
        while (tok.TKval == TKand)
        {
                asm_token();
                o2 = asm_equal_exp();
                if (asm_isint(o1) && asm_isint(o2))
                        o1->disp &= o2->disp;
                else {
                        asmerr(EM_bad_operand);         // illegal operand
                }
                o2->disp = 0;
                o1 = asm_merge_opnds( o1, o2 );
        }
        return o1;
}

/*******************************
 */

STATIC OPND * asm_equal_exp(void)
{
        register OPND *o1,*o2;

        o1 = asm_rel_exp();
        while (1)
                switch (tok.TKval)
                {
                case TKeqeq:
                                asm_token();
                                o2 = asm_rel_exp();
                                if (asm_isint(o1) && asm_isint(o2))
                                        o1->disp = o1->disp == o2->disp;
                                else {
                                    asmerr(EM_bad_operand);     // illegal operand
                                }
                                o2->disp = 0;
                                o1 = asm_merge_opnds( o1, o2 );
                                break;
                case TKne:
                                asm_token();
                                o2 = asm_rel_exp();
                                if (asm_isint(o1) && asm_isint(o2))
                                        o1->disp = o1->disp != o2->disp;
                                else {
                                    asmerr(EM_bad_operand);
                                }
                                o2->disp = 0;
                                o1 = asm_merge_opnds( o1, o2 );
                                break;
                default:
                                return o1;
                }
}

/*******************************
 */

STATIC OPND * asm_rel_exp(void)
{
        register OPND *o1,*o2;

        o1 = asm_shift_exp();
        while (1)
                switch (tok.TKval)
                {
                case TKgt:
                                asm_token();
                                o2 = asm_shift_exp();
                                if (asm_isint(o1) && asm_isint(o2))
                                        o1->disp = o1->disp > o2->disp;
                                else {
                                        asmerr( EM_bad_operand );
                                }
                                o2->disp = 0;
                                o1 = asm_merge_opnds( o1, o2 );
                                break;
                case TKge:
                                asm_token();
                                o2 = asm_shift_exp();
                                if (asm_isint(o1) && asm_isint(o2))
                                        o1->disp = o1->disp >= o2->disp;
                                else {
                                        asmerr( EM_bad_operand );
                                }
                                o2->disp = 0;
                                o1 = asm_merge_opnds( o1, o2 );
                                break;
                case TKlt:
                                asm_token();
                                o2 = asm_shift_exp();
                                if (asm_isint(o1) && asm_isint(o2))
                                        o1->disp = o1->disp < o2->disp;
                                else {
                                        asmerr( EM_bad_operand );
                                }
                                o2->disp = 0;
                                o1 = asm_merge_opnds( o1, o2 );
                                break;
                case TKle:
                                asm_token();
                                o2 = asm_shift_exp();
                                if (asm_isint(o1) && asm_isint(o2))
                                        o1->disp = o1->disp <= o2->disp;
                                else {
                                        asmerr( EM_bad_operand );
                                }
                                o2->disp = 0;
                                o1 = asm_merge_opnds( o1, o2 );
                                break;
                default:
                                return o1;
                }
}

/*******************************
 */

STATIC OPND * asm_shift_exp(void)
{
        register OPND *o1,*o2;
        register int op;
        enum_TK tk;

        o1 = asm_add_exp();
        while (tok.TKval == TKshl || tok.TKval == TKshr)
        {   tk = tok.TKval;
            asm_token();
            o2 = asm_add_exp();
            if (asm_isint(o1) && asm_isint(o2))
            {   if (tk == TKshl)
                    o1->disp <<= o2->disp;
                else
                    o1->disp >>= o2->disp;
            }
            else {
                    asmerr( EM_bad_operand );
            }
            o2->disp = 0;
            o1 = asm_merge_opnds( o1, o2 );
        }
        return o1;
}

/*******************************
 */

STATIC OPND * asm_add_exp(void)
{
        register OPND *o1,*o2;

        o1 = asm_mul_exp();
        while (1)
                switch (tok.TKval)
                {
                        case TKadd:
                                asm_token();
                                o2 = asm_mul_exp();
                                o1 = asm_merge_opnds( o1, o2 );
                                break;
                        case TKmin:
                                asm_token();
                                o2 = asm_mul_exp();
                                if (asm_isint(o1) && asm_isint(o2)) {
                                        o1->disp -= o2->disp;
                                        o2->disp = 0;
                                }
                                else
                                    o2->disp = - o2->disp;
                                o1 = asm_merge_opnds( o1, o2 );
                                break;
                default:
                                return o1;
                }
        return o1;
}

/*******************************
 */

STATIC OPND * asm_mul_exp(void)
{
        register OPND *o1,*o2;
        OPND *  popndTmp;

        o1 = asm_br_exp();
        while (1)
                switch (tok.TKval)
                {
                        case TKstar:
                                asm_token();
                                o2 = asm_br_exp();
#ifdef EXTRA_DEBUG
printf( "Star  o1.isint=%d, o2.isint=%d, lbra_seen=%d\n",
asm_isint(o1), asm_isint(o2), asm_TKlbra_seen  );
#endif
                                if (asm_isint(o1) && asm_isint(o2))
                                        o1->disp *= o2->disp;
                                else
                                if (asm_TKlbra_seen &&
                                        o1->pregDisp1 && asm_isint(o2)) {
                                    o1->uchMultiplier = o2->disp;
#ifdef EXTRA_DEBUG
                                    printf( "Multiplier: %d\n",
                                            o1->uchMultiplier );
#endif
                                }
                                else
                                if (asm_TKlbra_seen &&
                                        o2->pregDisp1 && asm_isint(o1)) {
                                        popndTmp = o2;
                                        o2 = o1;
                                        o1 = popndTmp;
                                        o1->uchMultiplier = o2->disp;
#ifdef EXTRA_DEBUG
                                    printf( "Multiplier: %d\n",
                                            o1->uchMultiplier );
#endif
                                }
                                else {
                                        asmerr( EM_bad_operand );
                                }
                                o2->disp = 0;
                                o1 = asm_merge_opnds( o1, o2 );
                                break;
                case TKdiv:
                                asm_token();
                                o2 = asm_br_exp();
                                if (asm_isint(o1) && asm_isint(o2))
                                        o1->disp /= o2->disp;
                                else {
                                        asmerr( EM_bad_operand );
                                }
                                o2->disp = 0;
                                o1 = asm_merge_opnds( o1, o2 );
                                break;
                case TKmod:
                                asm_token();
                                o2 = asm_br_exp();
                                if (asm_isint(o1) && asm_isint(o2))
                                        o1->disp %= o2->disp;
                                else {
                                        asmerr( EM_bad_operand );
                                }
                                o2->disp = 0;
                                o1 = asm_merge_opnds( o1, o2 );
                                break;
                default:
                                return o1;
                }
        return o1;
}

/*******************************
 */

STATIC OPND * asm_br_exp(void)
{
        register OPND *o1,*o2;
        symbol *s;

        o1 = asm_una_exp();
        while (1)
                switch (tok.TKval)
                {
                        case TKlbra:
                        {
#ifdef EXTRA_DEBUG
        printf("Saw a left bracket\n");
#endif
                                asm_token();
                                asm_TKlbra_seen++;
                                o2 = asm_cond_exp();
                                asm_TKlbra_seen--;
                                asm_chktok(TKrbra,EM_rbra);
#ifdef EXTRA_DEBUG
        printf("Saw a right bracket\n");
#endif
                                o1 = asm_merge_opnds( o1, o2 );
                                if (tok.TKval == TKdot)
                                {
                                    asm_token();
                                    if (tok.TKval != TKident)
                                        asmerr(EM_ident_exp);
                                    s = symbol_membersearch( tok.TKid );
                                    if (!s)
                                    {
                                        synerr(EM_unknown_tag, tok.TKid );
                                        asm_error();
                                    }
                                    asm_token();
                                    o2 = opnd_calloc();
                                    asm_merge_symbol(o2,s);
                                    o1 = asm_merge_opnds( o1, o2 );
                                }
                                else if (tok.TKval == TKident)
                                {   o2 = asm_una_exp();
                                    o1 = asm_merge_opnds( o1, o2 );
                                }
                                break;
                        }
                        default:
                                return o1;
                }
}

/*******************************
 */

STATIC OPND * asm_una_exp(void)
{
        register OPND *o1;
        register int op;
        type    *ptype;
        type    *ptypeSpec;
        ASM_JUMPTYPE    ajt = ASM_JUMPTYPE_UNSPECIFIED;
        char bPtr = 0;

        switch (tok.TKval)
        {
                case TKand:
                    asm_token();
                    o1 = asm_una_exp();
                    break;

                case TKstar:
                    asm_token();
                    o1 = asm_una_exp();
                    ++o1->indirect;
                    break;

                case TKadd:
                    asm_token();
                    o1 = asm_una_exp();
                    if (asm_isint(o1))
                        o1->disp = +o1->disp;
                    break;

                case TKmin:
                    asm_token();
                    o1 = asm_una_exp();
                    if (asm_isint(o1))
                        o1->disp = -o1->disp;
                    break;

                case TKnot:
                    asm_token();
                    o1 = asm_una_exp();
                    if (asm_isint(o1))
                        o1->disp = !o1->disp;
                    break;

                case TKcom:
                    asm_token();
                    o1 = asm_una_exp();
                    if (asm_isint(o1))
                        o1->disp = ~o1->disp;
                    break;

                case TKlpar:
                    // stoken() is called directly here because we really
                    // want the INT token to be an INT.
                    stoken();
                    if (type_specifier(&ptypeSpec)) /* if type_name     */
                    {

                        ptype = declar_abstract(ptypeSpec);
                                    /* read abstract_declarator  */
                        fixdeclar(ptype);/* fix declarator               */
                        type_free( ptypeSpec );/* the declar() function
                                            allocates the typespec again */
                        chktok(TKrpar,EM_rpar);
                        ptype->Tcount--;
                        goto CAST_REF;
                    }
                    else
                    {
                        type_free(ptypeSpec);
                        o1 = asm_cond_exp();
                        chktok(TKrpar, EM_rpar );
                    }

                    break;

                case ASMTKoffset:
                    asm_token();
                    o1 = asm_cond_exp();
                    if (!o1)
                        o1 = opnd_calloc();
                    o1->bOffset= TRUE;
                    break;
                case ASMTKseg:
                    asm_token();
                    o1 = asm_cond_exp();
                    if (!o1)
                        o1 = opnd_calloc();
                    o1->bSeg= TRUE;
                    break;

                case TKshort:
                case ASMTKshort:
                    if (asmstate.ucItype != ITjump)
                    {
                        ptype = tsshort;
                        goto TYPE_REF;
                    }
                    ajt = ASM_JUMPTYPE_SHORT;
                    asm_token();
                    goto JUMP_REF2;
                case TK_near:
                case ASMTKnear:
                    ajt = ASM_JUMPTYPE_NEAR;
                    goto JUMP_REF;
                case TK_far:
                case ASMTKfar:
                    ajt = ASM_JUMPTYPE_FAR;
JUMP_REF:
                    asm_token();
                    asm_chktok( (enum_TK) ASMTKptr, EM_ptr_exp );
JUMP_REF2:
                    o1 = asm_cond_exp();
                    if (!o1)
                        o1 = opnd_calloc();
                    o1->ajt= ajt;
                    break;

                case ASMTKbyte:
                case TKchar:
                    ptype = tschar;
                    goto TYPE_REF;
                case TKunsigned:
                    ptype = tsuns;
                    goto TYPE_REF;
                case ASMTKint:
                case TKint:
                case TKsigned:
                    ptype = tsint;
                    goto TYPE_REF;
                case ASMTKdword:
                case TKlong:
                    ptype = tslong;
                    goto TYPE_REF;
                case TKfloat:
                    ptype = tsfloat;
                    goto TYPE_REF;
                case ASMTKqword:
                case TKdouble:
                    ptype = tsdouble;
                    goto TYPE_REF;
                case ASMTKtbyte:
                    ptype = tsldouble;
                    goto TYPE_REF;
                case ASMTKword:
                    ptype = tsshort;
TYPE_REF:
                    bPtr = 1;
                    asm_token();
                    asm_chktok( (enum_TK) ASMTKptr, EM_ptr_exp );
CAST_REF:
                    o1 = asm_cond_exp();
                    if (!o1)
                        o1 = opnd_calloc();
                    type_settype(&o1->ptype,ptype);
                    o1->bPtr = bPtr;
                    break;

                default:
                    o1 = asm_primary_exp();
                    break;
        }
        return o1;
}

/*******************************
 */

STATIC OPND * asm_primary_exp(void)
{
        OPND *o1 = NULL;
        OPND *o2 = NULL;
        type    *ptype;
        symbol *s;
        enum_TK tkOld;
        int global;

        global = 0;
        switch (tok.TKval)
        {
                case TKcolcol:
                    global = 1;
                    asm_token();
                    if (tok.TKval != TKident)
                        asmerr(EM_ident_exp);
                    goto case_ident;

                case TKthis:
                    strcpy(tok.TKid,cpp_name_this);
                case TKident:
                case_ident:
                {
                    REG *regp;

                    o1 = opnd_calloc();
                    if (isdollar(tok.TKid))
                    {   o1->disp = 0;
                        s = asmstate.psDollar;
                        goto L1;
                    }
                    if ((regp = asm_reg_lookup(tok.TKid)) != NULL)
                    {
                        asm_token();
                        // see if it is segment override (like SS:)
                        if (!asm_TKlbra_seen &&
                                (regp->ty & _seg) &&
                                tok.TKval == TKcolon)
                        {
                            o1->segreg = regp;
                            asm_token();
                            o2 = asm_cond_exp();
                            o1 = asm_merge_opnds( o1, o2 );
                        }
                        else if (asm_TKlbra_seen)
                        {   // should be a register
                            if (o1->pregDisp1)
                                asmerr( EM_bad_operand );
                            else
                                o1->pregDisp1 = regp;
                        }
                        else
                        {   if (o1->base == NULL)
                                o1->base = regp;
                            else
                                asmerr( EM_bad_operand );
                        }
                        goto Lret;
                    }
                    // If floating point instruction and id is a floating register
                    else if (asmstate.ucItype == ITfloat &&
                             asm_is_fpreg( tok.TKid))
                    {
                        asm_token();
                        if (tok.TKval == TKlpar)
                        {
                            asm_token();
                            asm_chktok( TKnum, EM_num );
                            if (tok.TKutok.Vlong < 0 ||
                                tok.TKutok.Vlong > 7)
                                    asmerr( EM_bad_operand );
                            o1->base = &(aregFp[tok.TKutok.Vlong]);
                            asm_chktok( TKrpar, EM_rpar );
                        }
                        else
                            o1->base = &regFp;
                    }
                    else
                    {   symbol *s2;

                        s = scope_search(tok.TKid,
                                CPP ? (SCTlocal | SCTlabel | SCTwith)
                                    : (SCTlocal | SCTlabel | SCTtag));
                        if (!s)
                        {
                            s = scope_search(tok.TKid,
                                CPP ? SCTglobal | SCTnspace : SCTglobal | SCTglobaltag);
                            s2 = (!global && asmstate.ucItype != ITjump)
                                ? symbol_membersearch(tok.TKid)
                                : NULL;
                            if (s2)
                            {
                                if (s)
                                {
                                    synerr(EM_ambig_ref,s->Sident);     // ambiguous reference
                                }
                                else
                                    s = s2;
                            }
                        }
                        if (!s)
                        {   // Assume it is a forward referenced label
                            s = asm_define_label(tok.TKid);
                        }
                    L1:
                        asm_merge_symbol(o1,s);

                        // for . -> []
                        asm_token();
                        if (tok.TKval == TKdot ||
                            tok.TKval == TKcolcol ||
                            tok.TKval == TKarrow ||
                            tok.TKval == TKlbra)
                                o1 = asm_prim_post(o1);
                        goto Lret;
                    }
                }
                break;

            case ASMTKlength:
            case ASMTKsize:
            case ASMTKtype:
                tkOld = tok.TKval;
                asm_token();
                if (tok.TKval != TKident)
                    asmerr(EM_ident_exp);
                if ((s = symbol_search(tok.TKid)) == NULL)
                {
                    synerr(EM_undefined, tok.TKid );
                    asm_error();
                }
                o1 = opnd_calloc();
                switch (tkOld)
                {
                    case ASMTKlength:
                        if (tybasic(s->Stype->Tty) == TYarray)
                            o1->disp = s->Stype->Tdim;
                        else
                            o1->disp = 1;
                        break;
                    case ASMTKsize:
                        o1->disp = type_size( s->Stype );
                        break;
                    case ASMTKtype:
                        if (tybasic(s->Stype->Tty) == TYarray)
                            o1->disp = type_size( s->Stype->Tnext );
                        else
                            o1->disp = type_size( s->Stype );
                        break;
                }
                asm_token();
                break;

            case TKsizeof:
                o1 = opnd_calloc();
                o1->disp = msc_getnum();
                break;

            case TKnum:
                o1 = opnd_calloc();
                o1->disp = tok.TKutok.Vlong;
                asm_token();
                break;

            case TKreal_f:
                o1 = opnd_calloc();
                o1->real = tok.TKutok.Vfloat;
                type_settype(&o1->ptype,tsfloat);
                asm_token();
                break;

            case TKreal_d:
            case TKreal_da:
                o1 = opnd_calloc();
                o1->real = tok.TKutok.Vdouble;
                type_settype(&o1->ptype,tsdouble);
                asm_token();
                break;

            case TKreal_ld:
                o1 = opnd_calloc();
                o1->real = tok.TKutok.Vldouble;
                type_settype(&o1->ptype,tsldouble);
                asm_token();
                break;

            case ASMTKlocalsize:
                o1 = opnd_calloc();
                o1->s = asmstate.psLocalsize;
                type_settype(&o1->ptype,tsint);
                asm_token();
                break;

            case TKdot:
                asm_token();
                if (tok.TKval != TKident)
                    asmerr(EM_ident_exp);
                s = symbol_membersearch( tok.TKid );
                if (!s)
                {
                    synerr(EM_unknown_tag, tok.TKid );
                    asm_error();
                }
                o1 = opnd_calloc();
                asm_merge_symbol(o1,s);
                asm_token();
                break;
        }
Lret:
        return o1;
}

/*******************************
 */

STATIC OPND * asm_prim_post( OPND *o1)
{
    OPND *o2;
    symbol *s = o1->s;
    type *t;

    t = s ? s->Stype : o1->ptype;
    while (1)
    {
        switch (tok.TKval)
        {
            case TKarrow:
                if (++o1->indirect > 1)
                {
BAD_OPERAND:                asmerr( EM_bad_operand );
                }
                if (s->Sclass != SCregister)
                    goto BAD_OPERAND;
                if (!typtr(t->Tty))
                {
                    synerr(EM_pointer,t,(type *) NULL);
                    asm_error();
                }
                else
                    t = t->Tnext;
            case TKcolcol:
                if (tybasic(t->Tty) != TYstruct)
                    asmerr(EM_not_struct);      // not a struct or union type
                goto L1;

            case TKdot:
                for (; t && tybasic(t->Tty) != TYstruct;
                     t = t->Tnext)
                        ;
                if (!t)
                    asmerr(EM_not_struct);
            L1:
                /* try to find the symbol */
                asm_token();
                if (tok.TKval != TKident)
                    asmerr(EM_ident_exp);
                s = n2_searchmember(t->Ttag,tok.TKid);
                if (!s)
                {
                    err_notamember(tok.TKid,t->Ttag);
                    asm_error();
                }
                else
                {
                    asm_merge_symbol(o1,s);
                    t = s->Stype;
                    asm_token();
                }
                break;

            case TKlbra:
                asm_token();
                asm_TKlbra_seen++;
                o2 = asm_cond_exp();
                asm_chktok(TKrbra,EM_rbra);
                asm_TKlbra_seen--;
                return asm_merge_opnds( o1, o2 );

            default:
                return o1;
        }
    }
}

/*******************************
 */

unsigned compute_hashkey(char *s)
{
        register unsigned i, j;

//      printf("Compute Hashkey/%s/",s);
        for (i = j = 1; *s != '\0'; ++s, ++j)
                i = (i << 1) ^ *s;
//      printf( "i=%d, j=%d, i*j=%d (unsigned) (i*j)=%d\n",
//              i, j, i*j, (unsigned) (i*j) );
        return (i * j);
}


/*******************************
 */

void iasm_term( void )
{
    if (asmstate.bInit)
    {   symbol_free(asmstate.psDollar);
        symbol_free(asmstate.psLocalsize);
        asmstate.psDollar = NULL;
        asmstate.psLocalsize = NULL;
        asmstate.bInit = 0;
    }
}

/**********************************
 * Return mask of registers used by block bp.
 */

regm_t iasm_regs( block * bp )
{
#ifdef DEBUG
    if (debuga)
        printf( "Block iasm regs = 0x%X\n", bp->usIasmregs );
#endif

    refparam |= bp->bIasmrefparam;
    return( bp->usIasmregs );
}

#endif // !SPP

