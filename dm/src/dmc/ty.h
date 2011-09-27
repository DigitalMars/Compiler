// Copyright (C) 1983-1998 by Symantec
// Copyright (C) 2000-2009 by Digital Mars
// All Rights Reserved
// http://www.digitalmars.com
// Written by Walter Bright
/*
 * This source file is made available for personal use
 * only. The license is in /dmd/src/dmd/backendlicense.txt
 * or /dm/src/dmd/backendlicense.txt
 * For any other uses, please contact Digital Mars.
 */


#if __SC__
#pragma once
#endif

#ifndef TY_H
#define TY_H 1

//#define TYjhandle     TYnptr          // use for Jupiter handle

/*****************************************
 * Data types.
 * (consists of basic type + modifier bits)
 */

// Basic types.
// casttab[][] in exp2.c depends on the order of this
// typromo[] in cpp.c depends on the order too

enum TYM
{
    TYbool              = 0,
    TYchar              = 1,
    TYschar             = 2,    // signed char
    TYuchar             = 3,    // unsigned char
    TYchar8             = 4,
    TYchar16            = 5,
    TYshort             = 6,
    TYwchar_t           = 7,
    TYushort            = 8,    // unsigned short
    TYenum              = 9,    // enumeration value
    TYint               = 0xA,
    TYuint              = 0xB,  // unsigned
    TYlong              = 0xC,
    TYulong             = 0xD,  // unsigned long
    TYdchar             = 0xE,  // 32 bit Unicode char
    TYllong             = 0xF,  // 64 bit long
    TYullong            = 0x10, // 64 bit unsigned long
    TYfloat             = 0x11, // 32 bit real
    TYdouble            = 0x12, // 64 bit real

    // long double is mapped to either of the following at runtime:
    TYdouble_alias      = 0x13, // 64 bit real (but distinct for overload purposes)
    TYldouble           = 0x14, // 80 bit real

    // Add imaginary and complex types for D and C99
    TYifloat            = 0x15,
    TYidouble           = 0x16,
    TYildouble          = 0x17,
    TYcfloat            = 0x18,
    TYcdouble           = 0x19,
    TYcldouble          = 0x1A,

#if TX86
    TYjhandle           = 0x1B, // Jupiter handle type, equals TYnptr except
                                // that the debug type is different so the
                                // debugger can distinguish them
    TYnullptr           = 0x1C,
    TYnptr              = 0x1D, // data segment relative pointer
    TYsptr              = 0x1E, // stack segment relative pointer
    TYcptr              = 0x1F, // code segment relative pointer
    TYf16ptr            = 0x20, // special OS/2 far16 pointer
    TYfptr              = 0x21, // far pointer (has segment and offset)
    TYhptr              = 0x22, // huge pointer (has segment and offset)
    TYvptr              = 0x23, // __handle pointer (has segment and offset)
    TYref               = 0x24, // reference to another type
    TYvoid              = 0x25,
    TYstruct            = 0x26, // watch tyaggregate()
    TYarray             = 0x27, // watch tyaggregate()
    TYnfunc             = 0x28, // near C func
    TYffunc             = 0x29, // far  C func
    TYnpfunc            = 0x2A, // near Cpp func
    TYfpfunc            = 0x2B, // far  Cpp func
    TYnsfunc            = 0x2C, // near stdcall func
    TYfsfunc            = 0x2D, // far stdcall func
    TYifunc             = 0x2E, // interrupt func
    TYmemptr            = 0x2F, // pointer to member
    TYident             = 0x30, // type-argument
    TYtemplate          = 0x31, // unexpanded class template
    TYvtshape           = 0x32, // virtual function table
    TYptr               = 0x33, // generic pointer type
    TYf16func           = 0x34, // _far16 _pascal function
    TYnsysfunc          = 0x35, // near __syscall func
    TYfsysfunc          = 0x36, // far __syscall func
    TYmfunc             = 0x37, // NT C++ member func
    TYjfunc             = 0x38, // LINKd D function
    TYhfunc             = 0x39, // C function with hidden parameter
    TYnref              = 0x3A, // near reference
    TYfref              = 0x3B, // far reference
    TYMAX               = 0x3C,

#if MARS
#define TYaarray        TYnptr
#define TYdelegate      TYllong
#define TYdarray        TYullong
#endif
};

// These change depending on memory model
extern int TYptrdiff, TYsize, TYsize_t;

/* Linkage type                 */
#define mTYnear         0x100
#define mTYfar          0x200
#define mTYcs           0x400           // in code segment
#define mTYthread       0x800
#define mTYLINK         0xF00           // all linkage bits

#define mTYloadds       0x1000
#define mTYexport       0x2000
#define mTYweak         0x0000
#define mTYimport       0x4000
#define mTYnaked        0x8000
#define mTYMOD          0xF000          // all modifier bits

#else
#define TYTARG          0x11
#include "TGty.h"               /* Target types */
#endif

#define mTYbasic        0x3F    /* bit mask for basic types     */
#define tybasic(ty)     ((ty) & mTYbasic)

/* Modifiers to basic types     */

#ifdef JHANDLE
#define mTYarrayhandle  0x80
#else
#define mTYarrayhandle  0x0
#endif
#define mTYconst        0x40
#define mTYvolatile     0x80
#define mTYrestrict     0               // BUG: add for C99
#define mTYmutable      0               // need to add support
#define mTYunaligned    0               // non-zero for PowerPC

#define mTYimmutable    0x1000000       // immutable data
#define mTYshared       0x2000000       // shared data
#define mTYnothrow      0x4000000       // nothrow function

/* Flags in tytab[] array       */
extern unsigned tytab[];
#define TYFLptr         1
#define TYFLreal        2
#define TYFLintegral    4
#define TYFLcomplex     8
#define TYFLimaginary   0x10
#define TYFLuns         0x20
#define TYFLmptr        0x40
#define TYFLfv          0x80    /* TYfptr || TYvptr     */

#if TX86
#define TYFLfarfunc     0x100
#define TYFLpascal      0x200       // callee cleans up stack
#define TYFLrevparam    0x400       // function parameters are reversed
#else
#define TYFLcallstkc    0x100       // callee cleans up stack
#define TYFLrevparam    0x200       // function parameters are reversed
#endif
#define TYFLnullptr     0x800
#define TYFLshort       0x1000
#define TYFLaggregate   0x2000
#define TYFLfunc        0x4000
#define TYFLref         0x8000

/* Groupings of types   */

#define tyintegral(ty)  (tytab[(ty) & 0xFF] & TYFLintegral)

#define tyarithmetic(ty) (tytab[(ty) & 0xFF] & (TYFLintegral | TYFLreal | TYFLimaginary | TYFLcomplex))

#define tyaggregate(ty) (tytab[(ty) & 0xFF] & TYFLaggregate)

#define tyscalar(ty)    (tytab[(ty) & 0xFF] & (TYFLintegral | TYFLreal | TYFLimaginary | TYFLcomplex | TYFLptr | TYFLmptr | TYFLnullptr))

#define tyfloating(ty)  (tytab[(ty) & 0xFF] & (TYFLreal | TYFLimaginary | TYFLcomplex))

#define tyimaginary(ty) (tytab[(ty) & 0xFF] & TYFLimaginary)

#define tycomplex(ty)   (tytab[(ty) & 0xFF] & TYFLcomplex)

#define tyreal(ty)      (tytab[(ty) & 0xFF] & TYFLreal)

#ifndef tyshort
/* Types that are chars or shorts       */
#define tyshort(ty)     (tytab[(ty) & 0xFF] & TYFLshort)
#endif

/* Detect TYlong or TYulong     */
#ifndef tylong
#define tylong(ty)      (tybasic(ty) == TYlong || tybasic(ty) == TYulong)
#endif

/* Use to detect a pointer type */
#ifndef typtr
#define typtr(ty)       (tytab[(ty) & 0xFF] & TYFLptr)
#endif

/* Use to detect a reference type */
#ifndef tyref
#define tyref(ty)       (tytab[(ty) & 0xFF] & TYFLref)
#endif

/* Use to detect a pointer type or a member pointer     */
#ifndef tymptr
#define tymptr(ty)      (tytab[(ty) & 0xFF] & (TYFLptr | TYFLmptr))
#endif

// Use to detect a nullptr type or a member pointer
#ifndef tynullptr
#define tynullptr(ty)      (tytab[(ty) & 0xFF] & TYFLnullptr)
#endif

/* Detect TYfptr or TYvptr      */
#ifndef tyfv
#define tyfv(ty)        (tytab[(ty) & 0xFF] & TYFLfv)
#endif

/* Array to give the size in bytes of a type, -1 means error    */
extern signed char tysize[];

// Give size of type
#define tysize(ty)      tysize[(ty) & 0xFF]

/* All data types that fit in exactly 8 bits    */
#ifndef tybyte
#define tybyte(ty)      (tysize(ty) == 1)
#endif

/* Types that fit into a single machine register        */
#ifndef tyreg
#define tyreg(ty)       (tysize(ty) <= REGSIZE)
#endif

/* Detect function type */
#ifndef tyfunc
#define tyfunc(ty)      (tytab[(ty) & 0xFF] & TYFLfunc)
#endif

/* Detect function type where parameters are pushed in reverse order    */
#ifndef tyrevfunc
#define tyrevfunc(ty)   (tytab[(ty) & 0xFF] & TYFLrevparam)
#endif

/* Detect unsigned types */
#ifndef tyuns
#define tyuns(ty)       (tytab[(ty) & 0xFF] & (TYFLuns | TYFLptr))
#endif

/* Target dependent info        */
#if TX86
#define TYoffset TYuint         /* offset to an address         */

/* Detect cpp function type (callee cleans up stack)    */
#define typfunc(ty)     (tytab[(ty) & 0xFF] & TYFLpascal)

#else
/* Detect cpp function type (callee cleans up stack)    */
#ifndef typfunc
#define typfunc(ty)     (tytab[(ty) & 0xFF] & TYFLcallstkc)
#endif
#endif

/* Array to convert a type to its unsigned equivalent   */
extern const tym_t tytouns[];
#ifndef touns
#define touns(ty)       (tytouns[(ty) & 0xFF])
#endif

/* Determine if TYffunc or TYfpfunc (a far function) */
#ifndef tyfarfunc
#define tyfarfunc(ty)   (tytab[(ty) & 0xFF] & TYFLfarfunc)
#endif

// Determine if parameter can go in register for TYjfunc
#ifndef tyjparam
#define tyjparam(ty)    (tysize(ty) <= intsize && !tyfloating(ty) && tybasic(ty) != TYstruct && tybasic(ty) != TYarray)
#endif

/* Determine relaxed type       */
#ifndef tyrelax
#define tyrelax(ty)     (_tyrelax[tybasic(ty)])
#endif

/* Array to give the 'relaxed' type for relaxed type checking   */
extern unsigned char _tyrelax[];
#define type_relax      (config.flags3 & CFG3relax)     // !=0 if relaxed type checking
#if TARGET_LINUX || TARGET_OSX || TARGET_FREEBSD || TARGET_SOLARIS
#define type_semirelax  (config.flags3 & CFG3semirelax) // !=0 if semi-relaxed type checking
#else
#define type_semirelax  type_relax
#endif

/* Determine functionally equivalent type       */
extern unsigned char tyequiv[];

/* Give an ascii string for a type      */
extern const char *tystring[];

#if TX86
/* Debugger value for type      */
extern unsigned char dttab[];
extern unsigned short dttab4[];
#endif

#endif /* TY_H */

