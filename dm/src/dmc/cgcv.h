// Copyright (C) 1985-1998 by Symantec
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

/* Header for cgcv.c    */

#ifndef CGCV_H
#define CGCV_H
//#pragma once

extern char *ftdbname;

void cv_init ( void );
unsigned cv_typidx ( type *t );
void cv_outsym ( Symbol *s );
void cv_func ( Symbol *s );
void cv_term ( void );
unsigned long cv4_struct(Classsym *,int);


/* =================== Added for MARS compiler ========================= */

typedef unsigned long idx_t;    // type of type index

/* Data structure for a type record     */

#pragma pack(1)

typedef struct DEBTYP_T
{
    unsigned prev;              // previous debtyp_t with same hash
    unsigned short length;      // length of following array
    unsigned char data[2];      // variable size array
} debtyp_t;

#pragma pack()

struct Cgcv
{
    long signature;
    symlist_t list;             // deferred list of symbols to output
    idx_t deb_offset;           // offset added to type index
    unsigned sz_idx;            // size of stored type index
    int LCFDoffset;
    int LCFDpointer;
    int FD_code;                // frame for references to code
};

extern Cgcv cgcv;

debtyp_t * debtyp_alloc(unsigned length);
int cv_stringbytes(const char *name);
inline unsigned cv4_numericbytes(targ_size_t value);
void cv4_storenumeric(unsigned char *p,targ_size_t value);
idx_t cv_debtyp ( debtyp_t *d );
int cv_namestring ( unsigned char *p , const char *name );
unsigned cv4_typidx(type *t);
idx_t cv4_arglist(type *t,unsigned *pnparam);
unsigned char cv4_callconv(type *t);

#define TOIDX(a,b)      ((cgcv.sz_idx == 4) ? TOLONG(a,b) : TOWORD(a,b))

#define DEBSYM  5               /* segment of symbol info               */
#define DEBTYP  6               /* segment of type info                 */


#endif

