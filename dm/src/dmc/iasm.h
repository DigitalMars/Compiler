
/*
 * Copyright (c) 1992-1999 by Symantec
 * Copyright (c) 1999-2008 by Digital Mars
 * All Rights Reserved
 * http://www.digitalmars.com
 * Written by Mike Cote, John Micco and Walter Bright
 *
 * This source file is made available for personal use
 * only. The license is in /dmd/src/dmd/backendlicense.txt
 * or /dm/src/dmd/backendlicense.txt
 * For any other uses, please contact Digital Mars.
 */

#include <setjmp.h>

/////////////////////////////////////////////////
// Instruction flags (usFlags)
//
//

// This is for when the reg field of modregrm specifies which instruction it is
#define NUM_MASK        0x7
#define _0      (0x0 | _modrm)          // insure that some _modrm bit is set
#define _1      0x1                     // with _0
#define _2      0x2
#define _3      0x3
#define _4      0x4
#define _5      0x5
#define _6      0x6
#define _7      0x7

#define _modrm  0x10

#define _r      _modrm
#define _cb     _modrm
#define _cw     _modrm
#define _cd     _modrm
#define _cp     _modrm
#define _ib     0
#define _iw     0
#define _id     0
#define _rb     0
#define _rw     0
#define _rd     0
#define _16_bit 0x20
#define _32_bit 0x40
#define _I386   0x80            // opcode is only for 386 and later
#define _16_bit_addr    0x100
#define _32_bit_addr    0x200
#define _fwait 0x400    // Add an FWAIT prior to the instruction opcode
#define _nfwait 0x800   // Do not add an FWAIT prior to the instruction

#define MOD_MASK        0xF000  // Mod mask
#define _modsi          0x1000  // Instruction modifies SI
#define _moddx          0x2000  // Instruction modifies DX
#define _mod2           0x3000  // Instruction modifies second operand
#define _modax          0x4000  // Instruction modifies AX
#define _modnot1        0x5000  // Instruction does not modify first operand
#define _modaxdx        0x6000  // instruction modifies AX and DX
#define _moddi          0x7000  // Instruction modifies DI
#define _modsidi        0x8000  // Instruction modifies SI and DI
#define _modcx          0x9000  // Instruction modifies CX
#define _modes          0xa000  // Instruction modifies ES
#define _modall         0xb000  // Instruction modifies all register values
#define _modsiax        0xc000  // Instruction modifies AX and SI
#define _modsinot1      0xd000  // Instruction modifies SI and not first param

/////////////////////////////////////////////////
// Operand flags - usOp1, usOp2, usOp3
//

typedef unsigned short opflag_t;

// Operand flags for normal opcodes

#define _r8     CONSTRUCT_FLAGS( _8, _reg, _normal, 0 )
#define _r16    CONSTRUCT_FLAGS(_16, _reg, _normal, 0 )
#define _r32    CONSTRUCT_FLAGS(_32, _reg, _normal, 0 )
#define _m8     CONSTRUCT_FLAGS(_8, _m, _normal, 0 )
#define _m16    CONSTRUCT_FLAGS(_16, _m, _normal, 0 )
#define _m32    CONSTRUCT_FLAGS(_32, _m, _normal, 0 )
#define _m48    CONSTRUCT_FLAGS( _48, _m, _normal, 0 )
#define _m64    CONSTRUCT_FLAGS( _anysize, _m, _normal, 0 )
#define _m128   CONSTRUCT_FLAGS( _anysize, _m, _normal, 0 )
#define _rm8    CONSTRUCT_FLAGS(_8, _rm, _normal, 0 )
#define _rm16   CONSTRUCT_FLAGS(_16, _rm, _normal, 0 )
#define _rm32   CONSTRUCT_FLAGS(_32, _rm, _normal, 0)
#define _r32m16 CONSTRUCT_FLAGS(_32|_16, _rm, _normal, 0)
#define _imm8   CONSTRUCT_FLAGS(_8, _imm, _normal, 0 )
#define _imm16  CONSTRUCT_FLAGS(_16, _imm, _normal, 0)
#define _imm32  CONSTRUCT_FLAGS(_32, _imm, _normal, 0)
#define _rel8   CONSTRUCT_FLAGS(_8, _rel, _normal, 0)
#define _rel16  CONSTRUCT_FLAGS(_16, _rel, _normal, 0)
#define _rel32  CONSTRUCT_FLAGS(_32, _rel, _normal, 0)
#define _p1616  CONSTRUCT_FLAGS(_32, _p, _normal, 0)
#define _m1616  CONSTRUCT_FLAGS(_32, _mnoi, _normal, 0)
#define _p1632  CONSTRUCT_FLAGS(_48, _p, _normal, 0 )
#define _m1632  CONSTRUCT_FLAGS(_48, _mnoi, _normal, 0)
#define _special  CONSTRUCT_FLAGS( 0, 0, _rspecial, 0 )
#define _seg    CONSTRUCT_FLAGS( 0, 0, _rseg, 0 )
#define _a16    CONSTRUCT_FLAGS( 0, 0, _addr16, 0 )
#define _a32    CONSTRUCT_FLAGS( 0, 0, _addr32, 0 )
#define _f16    CONSTRUCT_FLAGS( 0, 0, _fn16, 0)
                                                // Near function pointer
#define _f32    CONSTRUCT_FLAGS( 0, 0, _fn32, 0)
                                                // Far function pointer
#define _lbl    CONSTRUCT_FLAGS( 0, 0, _flbl, 0 )
                                                // Label (in current function)

#define _mmm32  CONSTRUCT_FLAGS( 0, _m, 0, _32)
#define _mmm64  CONSTRUCT_FLAGS( 0, _m, 0, _64)
#define _mmm128 CONSTRUCT_FLAGS( 0, _m, 0, _128)

#define _xmm_m32 CONSTRUCT_FLAGS( _32, _m, _rspecial, 0)
#define _xmm_m64 CONSTRUCT_FLAGS( _anysize, _m, _rspecial, 0)
#define _xmm_m128 CONSTRUCT_FLAGS( _anysize, _m, _rspecial, 0)

#define _moffs8 (_rel8)
#define _moffs16 (_rel16 )
#define _moffs32 (_rel32 )


////////////////////////////////////////////////////////////////////
// Operand flags for floating point opcodes are all just aliases for
// normal opcode variants and only asm_determine_operator_flags should
// need to care.
//
#define _fm80   CONSTRUCT_FLAGS( 0, _m, 0, _80 )
#define _fm64   CONSTRUCT_FLAGS( 0, _m, 0, _64 )
#define _fm128  CONSTRUCT_FLAGS( 0, _m, 0, _128 )
#define _fanysize (_64 | _80 | _112 | _224)

#define _float_m CONSTRUCT_FLAGS( _anysize, _float, 0, _fanysize)

#define _st     CONSTRUCT_FLAGS( 0, _float, 0, _rst )   // stack register 0
#define _m112   CONSTRUCT_FLAGS( 0, _m, 0, _112 )
#define _m224   CONSTRUCT_FLAGS( 0, _m, 0, _224 )
#define _m512   _m224
#define _sti    CONSTRUCT_FLAGS( 0, _float, 0, _rsti )

////////////////// FLAGS /////////////////////////////////////

#define CONSTRUCT_FLAGS( uSizemask, aopty, amod, uRegmask ) \
    ( (uSizemask) | (aopty) << 4 | (amod) << 7 | (uRegmask) << 10)

#define ASM_GET_uSizemask(us)   ((us) & 0x0F)
#define ASM_GET_aopty(us)       ((ASM_OPERAND_TYPE)(((us) & 0x70) >> 4))
#define ASM_GET_amod(us)        ((ASM_MODIFIERS)(((us) & 0x380) >> 7))
#define ASM_GET_uRegmask(us)    (((us) & 0xFC00) >> 10)


// For uSizemask (4 bits)
#define _8  0x1
#define _16 0x2
#define _32 0x4
#define _48 0x8
#define _anysize (_8 | _16 | _32 | _48 )

// For aopty (3 bits)
enum ASM_OPERAND_TYPE {
    _reg,           // _r8, _r16, _r32
    _m,             // _m8, _m16, _m32, _m48
    _imm,           // _imm8, _imm16, _imm32
    _rel,           // _rel8, _rel16, _rel32
    _mnoi,          // _m1616, _m1632
    _p,             // _p1616, _p1632
    _rm,            // _rm8, _rm16, _rm32
    _float          // Floating point operand, look at cRegmask for the
                    // actual size
};

// For amod (3 bits)
enum ASM_MODIFIERS {
    _normal,        // Normal register value
    _rseg,          // Segment registers
    _rspecial,      // Special registers
    _addr16,        // 16 bit address
    _addr32,        // 32 bit address
    _fn16,          // 16 bit function call
    _fn32,          // 32 bit function call
    _flbl           // Label
};

// For uRegmask (6 bits)

// uRegmask flags when aopty == _float
#define _rst    0x1
#define _rsti   0x2
#define _64     0x4
#define _80     0x8
#define _128    0x40
#define _112    0x10
#define _224    0x20

// _seg register values (amod == _rseg)
//
#define _ds     CONSTRUCT_FLAGS( 0, 0, _rseg, 0x01 )
#define _es     CONSTRUCT_FLAGS( 0, 0, _rseg, 0x02 )
#define _ss     CONSTRUCT_FLAGS( 0, 0, _rseg, 0x04 )
#define _fs     CONSTRUCT_FLAGS( 0, 0, _rseg, 0x08 )
#define _gs     CONSTRUCT_FLAGS( 0, 0, _rseg, 0x10 )
#define _cs     CONSTRUCT_FLAGS( 0, 0, _rseg, 0x20 )

//
// _special register values
//
#define _crn    CONSTRUCT_FLAGS( 0, 0, _rspecial, 0x01 ) // CRn register (0,2,3)
#define _drn    CONSTRUCT_FLAGS( 0, 0, _rspecial, 0x02 ) // DRn register (0-3,6-7)
#define _trn    CONSTRUCT_FLAGS( 0, 0, _rspecial, 0x04 ) // TRn register (3-7)
#define _mm     CONSTRUCT_FLAGS( 0, 0, _rspecial, 0x08 ) // MMn register (0-7)
#define _xmm    CONSTRUCT_FLAGS( 0, 0, _rspecial, 0x10 ) // XMMn register (0-7)

//
// Default register values
//

#define _al     CONSTRUCT_FLAGS( 0, 0, _normal, 0x01 )  // AL register
#define _ax     CONSTRUCT_FLAGS( 0, 0, _normal, 0x02 )  // AX register
#define _eax    CONSTRUCT_FLAGS( 0, 0, _normal, 0x04 )  // EAX register
#define _dx     CONSTRUCT_FLAGS( 0, 0, _normal, 0x08 )  // DX register
#define _cl     CONSTRUCT_FLAGS( 0, 0, _normal, 0x10 )  // CL register

#define _rplus_r        0x20
#define _plus_r CONSTRUCT_FLAGS( 0, 0, 0, _rplus_r )
                // Add the register to the opcode (no mod r/m)



//////////////////////////////////////////////////////////////////

#define ITprefix        0x10    // special prefix
#define ITjump          0x20    // jump instructions CALL, Jxx and LOOPxx
#define ITimmed         0x30    // value of an immediate operand controls
                                // code generation
#define ITopt           0x40    // not all operands are required
#define ITshift         0x50    // rotate and shift instructions
#define ITfloat         0x60    // floating point coprocessor instructions
#define ITdata          0x70    // DB, DW, DD, DQ, DT pseudo-ops
#define ITaddr          0x80    // DA (define addresss) pseudo-op
#define ITMASK          0xF0
#define ITSIZE          0x0F    // mask for size

enum OP_DB
{
#if SCPP
    // These are the number of bytes
    OPdb = 1,
    OPdw = 2,
    OPdd = 4,
    OPdq = 8,
    OPdt = 10,
    OPdf = 4,
    OPde = 10,
    OPds = 2,
    OPdi = 4,
    OPdl = 8,
#endif
#if MARS
    // Integral types
    OPdb,
    OPds,
    OPdi,
    OPdl,

    // Float types
    OPdf,
    OPdd,
    OPde,

    // Deprecated
    OPdw = OPds,
    OPdq = OPdl,
    OPdt = OPde,
#endif
};


/* from iasm.c */
int asm_state(int iFlags);

void asm_process_fixup( block **ppblockLabels );

typedef struct _PTRNTAB3 {
        unsigned usOpcode;
        unsigned short usFlags;
        opflag_t usOp1;
        opflag_t usOp2;
        opflag_t usOp3;
} PTRNTAB3, * PPTRNTAB3, ** PPPTRNTAB3;

typedef struct _PTRNTAB2 {
        unsigned usOpcode;
        unsigned short usFlags;
        opflag_t usOp1;
        opflag_t usOp2;
} PTRNTAB2, * PPTRNTAB2, ** PPPTRNTAB2;

typedef struct _PTRNTAB1 {
        unsigned usOpcode;
        unsigned short usFlags;
        opflag_t usOp1;
} PTRNTAB1, * PPTRNTAB1, ** PPPTRNTAB1;

typedef struct _PTRNTAB0 {
        unsigned usOpcode;
        #define ASM_END 0xffff          // special opcode meaning end of table
        unsigned short usFlags;
} PTRNTAB0, * PPTRNTAB0, ** PPPTRNTAB0;

typedef union _PTRNTAB {
        PTRNTAB0        *pptb0;
        PTRNTAB1        *pptb1;
        PTRNTAB2        *pptb2;
        PTRNTAB3        *pptb3;
} PTRNTAB, * PPTRNTAB, ** PPPTRNTAB;

typedef struct
{
        unsigned char usNumops;
        PTRNTAB ptb;
} OP;

