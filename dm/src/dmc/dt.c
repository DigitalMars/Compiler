// Copyright (C) 1984-1998 by Symantec
// Copyright (C) 2000-2010 by Digital Mars
// All Rights Reserved
// http://www.digitalmars.com
// Written by Walter Bright
/*
 * This source file is made available for personal use
 * only. The license is in backendlicense.txt
 * For any other uses, please contact Digital Mars.
 */

#if !SPP

#include        <stdio.h>
#include        <string.h>
#include        <stdlib.h>
#include        <time.h>
#include        "cc.h"
#include        "oper.h"
#include        "global.h"
#include        "el.h"
#include        "type.h"
#include        "dt.h"

static char __file__[] = __FILE__;      /* for tassert.h                */
#include        "tassert.h"

static dt_t *dt_freelist;

/**********************************************
 * Allocate a data definition struct.
 */

dt_t *dt_calloc(char dtx)
{
    dt_t *dt;
    static dt_t dtzero;

    if (dt_freelist)
    {
        dt = dt_freelist;
        dt_freelist = dt->DTnext;
        *dt = dtzero;
    }
    else
        dt = (dt_t *) mem_fcalloc(sizeof(dt_t));
    dt->dt = dtx;
    return dt;
}

/**********************************************
 * Free a data definition struct.
 */

void dt_free(dt_t *dt)
{   dt_t *dtn;

    for (; dt; dt = dtn)
    {
        switch (dt->dt)
        {
            case DT_abytes:
            case DT_nbytes:
                mem_free(dt->DTpbytes);
                break;
        }
        dtn = dt->DTnext;
        dt->DTnext = dt_freelist;
        dt_freelist = dt;
    }
}

/*********************************
 * Free free list.
 */

void dt_term()
{
#if TERMCODE
    dt_t *dtn;

    while (dt_freelist)
    {   dtn = dt_freelist->DTnext;
        mem_ffree(dt_freelist);
        dt_freelist = dtn;
    }
#endif
}


/**********************
 * Construct a DT_azeros record, and return it.
 * Increment dsout.
 */

dt_t **dtnzeros(dt_t **pdtend,targ_size_t size)
{   dt_t *dt;

    //printf("dtnzeros(x%x)\n",size);
    assert((long) size >= 0);
    while (*pdtend)
        pdtend = &((*pdtend)->DTnext);
    if (size)
    {   dt = dt_calloc(DT_azeros);
        dt->DTazeros = size;
        *pdtend = dt;
        pdtend = &dt->DTnext;
#if SCPP
        dsout += size;
#endif
    }
    return pdtend;
}

/**********************
 * Construct a DTsymsize record.
 */

void dtsymsize(symbol *s)
{
    symbol_debug(s);
    s->Sdt = dt_calloc(DT_symsize);
}

/**********************
 * Construct a DTnbytes record, and return it.
 */

dt_t ** dtnbytes(dt_t **pdtend,targ_size_t size,const char *ptr)
{   dt_t *dt;

    while (*pdtend)
        pdtend = &((*pdtend)->DTnext);
    if (size)
    {   if (size == 1)
        {   dt = dt_calloc(DT_1byte);
            dt->DTonebyte = *ptr;
        }
        else if (size <= 7)
        {   dt = dt_calloc(DT_ibytes);
            dt->DTn = size;
            memcpy(dt->DTdata,ptr,size);
        }
        else
        {
            dt = dt_calloc(DT_nbytes);
            dt->DTnbytes = size;
            dt->DTpbytes = (char *) MEM_PH_MALLOC(size);
            memcpy(dt->DTpbytes,ptr,size);
        }
        *pdtend = dt;
        pdtend = &dt->DTnext;
    }
    return pdtend;
}

/**********************
 * Construct a DTabytes record, and return it.
 */

dt_t **dtabytes(dt_t **pdtend,tym_t ty, targ_size_t offset, targ_size_t size, const char *ptr)
{   dt_t *dt;

    while (*pdtend)
        pdtend = &((*pdtend)->DTnext);

    dt = dt_calloc(DT_abytes);
    dt->DTnbytes = size;
    dt->DTpbytes = (char *) MEM_PH_MALLOC(size);
    dt->Dty = ty;
    dt->DTabytes = offset;
    memcpy(dt->DTpbytes,ptr,size);

    *pdtend = dt;
    pdtend = &dt->DTnext;
    return pdtend;
}

/**********************
 * Construct a DTibytes record, and return it.
 */

dt_t ** dtdword(dt_t **pdtend, int value)
{   dt_t *dt;

    while (*pdtend)
        pdtend = &((*pdtend)->DTnext);
    dt = dt_calloc(DT_ibytes);
    dt->DTn = 4;

    union { char* cp; int* lp; } u;
    u.cp = dt->DTdata;
    *u.lp = value;

    *pdtend = dt;
    pdtend = &dt->DTnext;
    return pdtend;
}

dt_t ** dtsize_t(dt_t **pdtend, targ_size_t value)
{   dt_t *dt;

    while (*pdtend)
        pdtend = &((*pdtend)->DTnext);
    dt = dt_calloc(DT_ibytes);
    dt->DTn = NPTRSIZE;

    union { char* cp; int* lp; } u;
    u.cp = dt->DTdata;
    *u.lp = value;
    if (NPTRSIZE == 8)
        u.lp[1] = value >> 32;

    *pdtend = dt;
    pdtend = &dt->DTnext;
    return pdtend;
}

/**********************
 * Concatenate two dt_t's.
 */

dt_t ** dtcat(dt_t **pdtend,dt_t *dt)
{
    while (*pdtend)
        pdtend = &((*pdtend)->DTnext);
    *pdtend = dt;
    pdtend = &dt->DTnext;
    return pdtend;
}

/**********************
 * Construct a DTcoff record, and return it.
 */

dt_t ** dtcoff(dt_t **pdtend,targ_size_t offset)
{   dt_t *dt;

    while (*pdtend)
        pdtend = &((*pdtend)->DTnext);
    dt = dt_calloc(DT_coff);
#if TARGET_SEGMENTED
    dt->Dty = TYcptr;
#else
    dt->Dty = TYnptr;
#endif
    dt->DToffset = offset;
    *pdtend = dt;
    pdtend = &dt->DTnext;
    return pdtend;
}

/**********************
 * Construct a DTxoff record, and return it.
 */

dt_t ** dtxoff(dt_t **pdtend,symbol *s,targ_size_t offset,tym_t ty)
{   dt_t *dt;

    symbol_debug(s);
    while (*pdtend)
        pdtend = &((*pdtend)->DTnext);
    dt = dt_calloc(DT_xoff);
    dt->DTsym = s;
    dt->DToffset = offset;
    dt->Dty = ty;
    *pdtend = dt;
    pdtend = &dt->DTnext;
    return pdtend;
}

/**************************
 * 'Optimize' a list of dt_t's.
 * (Try to collapse it into one DT_azeros object.)
 */

void dt_optimize(dt_t *dt)
{   dt_t *dtn;

    if (dt)
    {   for (; 1; dt = dtn)
        {
            dtn = dt->DTnext;
            if (!dtn)
                break;
            switch (dt->dt)
            {
                case DT_azeros:
                    if (dtn->dt == DT_1byte && dtn->DTonebyte == 0)
                    {
                        dt->DTazeros += 1;
                        goto L1;
                    }
                    else if (dtn->dt == DT_azeros)
                    {
                        dt->DTazeros += dtn->DTazeros;
                        goto L1;
                    }
                    break;

                case DT_1byte:
                    if (dt->DTonebyte == 0)
                    {
                        if (dtn->dt == DT_1byte && dtn->DTonebyte == 0)
                        {
                            dt->DTazeros = 2;
                            goto L1;
                        }
                        else if (dtn->dt == DT_azeros)
                        {
                            dt->DTazeros = 1 + dtn->DTazeros;
                         L1:
                            dt->dt = DT_azeros;
                            dt->DTnext = dtn->DTnext;
                            dtn->DTnext = NULL;
                            dt_free(dtn);
                            dtn = dt;
                        }
                    }
                    break;
            }
        }
    }
}

/**************************
 * Make a common block for s.
 */

void init_common(symbol *s)
{
    //printf("init_common('%s')\n", s->Sident);
    dtnzeros(&s->Sdt,type_size(s->Stype));
    if (s->Sdt)
        s->Sdt->dt = DT_common;
}

/**********************************
 * Compute size of a dt
 */

unsigned dt_size(dt_t *dtstart)
{   dt_t *dt;
    unsigned datasize;

    datasize = 0;
    for (dt = dtstart; dt; dt = dt->DTnext)
    {
        switch (dt->dt)
        {   case DT_abytes:
                datasize += size(dt->Dty);
                break;
            case DT_ibytes:
                datasize += dt->DTn;
                break;
            case DT_nbytes:
                datasize += dt->DTnbytes;
                break;
            case DT_symsize:
            case DT_azeros:
                datasize += dt->DTazeros;
                break;
            case DT_common:
                break;
            case DT_xoff:
            case DT_coff:
                datasize += size(dt->Dty);
                break;
            case DT_1byte:
                datasize++;
                break;
            default:
#ifdef DEBUG
                dbg_printf("dt = %p, dt = %d\n",dt,dt->dt);
#endif
                assert(0);
        }
    }
    return datasize;
}

#endif /* !SPP */
