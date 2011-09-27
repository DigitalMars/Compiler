// Copyright (C) 1985-1994 by Symantec
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

#ifndef __TYPE_H
#define __TYPE_H

#include <limits.h>

#if TX86
typedef unsigned char mangle_t;

#define mTYman_c        1       // C mangling
#define mTYman_cpp      2       // C++ mangling
#define mTYman_pas      3       // Pascal mangling
#define mTYman_for      4       // FORTRAN mangling
#define mTYman_sys      5       // _syscall mangling
#define mTYman_std      6       // _stdcall mangling
#define mTYman_d        7       // D mangling

#else
#include "TGtype.h"
#endif

/*********************************
 * Data type.
 */

#define list_type(tl)   ((struct TYPE *) list_ptr(tl))

struct TYPE
{
#ifdef DEBUG
    unsigned short      id;
#define IDtype  0x1234
#define type_debug(t) assert((t)->id == IDtype)
#else
#define type_debug(t)
#endif

    tym_t       Tty;            /* mask (TYxxx)                         */
    unsigned short Tflags;      // TFxxxxx

#if TX86
#if TARGET_LINUX || TARGET_OSX || TARGET_FREEBSD || TARGET_SOLARIS
#define mTYnoret        0x010000        // function has no return
#define mTYtransu       0x010000        // transparent union
#else
#define mTYfar16        0x010000
#endif
#define mTYstdcall      0x020000
#define mTYfastcall     0x040000
#define mTYinterrupt    0x080000
#define mTYcdecl        0x100000
#define mTYpascal       0x200000
#define mTYsyscall      0x400000
#define mTYjava         0x800000

#if TARGET_LINUX || TARGET_OSX || TARGET_FREEBSD || TARGET_SOLARIS
#define mTYTFF          0xFE0000
#else
#define mTYTFF          0xFF0000
#endif

#define TARGET_strucTYPE
    mangle_t Tmangle;           // name mangling
// Return name mangling of type
#define type_mangle(t)  ((t)->Tmangle)
#endif

    unsigned Tcount;            // # pointing to this type
    struct TYPE *Tnext;         // next in list
                                // TYenum: gives base type
    union
    {
        targ_size_t Tdim;       // TYarray: # of elements in array
        struct elem *Tel;       // TFvla: gives dimension (NULL if '*')
        struct PARAM *Tparamtypes; // TYfunc, TYtemplate: types of function parameters
        struct Classsym *Ttag;  // TYstruct,TYmemptr: tag symbol
                                // TYenum,TYvtshape: tag symbol
        char *Tident;           // TYident: identifier
#if SCPP
        struct TYPE *Talternate;        // typtr: type of parameter before converting
#endif
#if MARS
        struct TYPE *Tkey;      // typtr: key type for associative arrays
#endif
    };
    list_t Texcspec;            // tyfunc(): list of types of exception specification
#if 0
    unsigned short Tstabidx;    // Index into stab types
#endif
    TARGET_strucTYPE
#if SOURCE_4TYPES
    Srcpos Tsrcpos;             /* position of type definition */
#endif
#if HTOD
    Symbol *Ttypedef;           // if this type came from a typedef, this is
                                // the typedef symbol
#endif
};

typedef struct TYPETEMP
{   struct TYPE Ttype;

    /* Tsym should really be part of a derived class, as we only
        allocate room for it if TYtemplate
     */
    Symbol *Tsym;               // primary class template symbol
} typetemp_t;

/* Values for Tflags:                                                   */
#define TFprototype     1       /* if this function is prototyped       */
#define TFfixed         2       /* if prototype has a fixed # of parameters */
#define TFforward       8       // TYstruct: if forward reference of tag name
#define TFsizeunknown   0x10    // TYstruct,TYarray: if size of type is unknown
                                // TYmptr: the Stag is TYident type
#define TFfuncret       0x20    // C++,tyfunc(): overload based on function return value
#define TFfuncparam     0x20    // TYarray: top level function parameter
#define TFstatic        0x40    // TYarray: static dimension
#define TFvla           0x80    // TYarray: variable length array
#define TFemptyexc      0x100   // tyfunc(): empty exception specification

// C
#define TFgenerated     4       // if we generated the prototype ourselves

// CPP
#define TFdependent     4       // template dependent type

#if !TX86
#define TFhydrated      0x20    // type data already hydrated
#define TFbasicrev      0x80    // if basic reserved type
#endif

/* Return !=0 if function type has a variable number of arguments       */
#define variadic(t)     (((t)->Tflags & (TFprototype | TFfixed)) == TFprototype)

/* Data         */

typedef type *typep_t;

extern typep_t tstypes[TYMAX];
extern typep_t tsptr2types[TYMAX];

#define tsbool    tstypes[TYbool]
#define tschar    tstypes[TYchar]
#define tsschar   tstypes[TYschar]
#define tsuchar   tstypes[TYuchar]
#define tschar16  tstypes[TYchar16]
#define tsshort   tstypes[TYshort]
#define tsushort  tstypes[TYushort]
#define tswchar_t tstypes[TYwchar_t]
#define tsint     tstypes[TYint]
#define tsuns     tstypes[TYuint]
#define tslong    tstypes[TYlong]
#define tsulong   tstypes[TYulong]
#define tsdchar   tstypes[TYdchar]
#define tsllong   tstypes[TYllong]
#define tsullong  tstypes[TYullong]
#define tsfloat   tstypes[TYfloat]
#define tsdouble  tstypes[TYdouble]
#define tsreal64  tstypes[TYdouble_alias]
#define tsldouble tstypes[TYldouble]
#define tsvoid    tstypes[TYvoid]

#define tsifloat   tstypes[TYifloat]
#define tsidouble  tstypes[TYidouble]
#define tsildouble tstypes[TYildouble]
#define tscfloat   tstypes[TYcfloat]
#define tscdouble  tstypes[TYcdouble]
#define tscldouble tstypes[TYcldouble]

#define tsnullptr tstypes[TYnullptr]

extern typep_t tslogical;
extern typep_t chartype;
extern typep_t tsclib;
extern typep_t tsdlib;
extern typep_t tspvoid,tspcvoid;
extern typep_t tsptrdiff, tssize;
#if TX86
extern typep_t tstrace;
#endif

#define tserr           tsint   /* error type           */

// Return !=0 if type is a struct, class or union
#define type_struct(t)  (tybasic((t)->Tty) == TYstruct)

/* Functions    */
void type_print(type *t);
void type_free(type *);
void type_init(void);
void type_term(void);
type *type_copy(type *);
elem *type_vla_fix(type **pt);
type *type_setdim(type **,targ_size_t);
type *type_setdependent(type *t);
int type_isdependent(type *t);
type *type_copy(type *);
void type_hydrate(type **);
void type_dehydrate(type **);

targ_size_t type_size(type *);
unsigned type_alignsize(type *);
targ_size_t type_paramsize(type *t);
type *type_alloc(tym_t);
type *type_alloc_template(symbol *s);
type *type_allocn(tym_t,type *tn);
type *type_allocmemptr(Classsym *stag,type *tn);
type *type_fake(tym_t);
type *type_setty(type **,long);
type *type_settype(type **pt, type *t);
type *type_setmangle(type **pt,mangle_t mangle);
type *type_setcv(type **pt,tym_t cv);
int type_embed(type *t,type *u);
int type_isvla(type *t);
int type_jparam(type *t);

param_t *param_calloc(void);
param_t *param_append_type(param_t **,type *);
void param_free_l(param_t *);
void param_free(param_t **);
symbol *param_search(const char *name, param_t **pp);
void param_hydrate(param_t **);
void param_dehydrate(param_t **);

#endif
