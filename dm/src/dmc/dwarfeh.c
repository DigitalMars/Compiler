/**
 * Compiler implementation of the D programming language.
 * Implements LSDA (Language Specific Data Area) table generation
 * for Dwarf Exception Handling.
 *
 * Copyright: Copyright (C) 2015-2018 by The D Language Foundation, All Rights Reserved
 * Authors: Walter Bright, http://www.digitalmars.com
 * License:   $(LINK2 http://www.boost.org/LICENSE_1_0.txt, Boost License 1.0)
 * Source:    $(LINK2 https://github.com/dlang/dmd/blob/master/src/dmd/backend/dwarfeh.c, backend/dwarfeh.c)
 */

#include        <stdio.h>
#include        <string.h>
#include        <stdlib.h>
#include        <time.h>

#include        "cc.h"
#include        "el.h"
#include        "code.h"
#include        "oper.h"
#include        "global.h"
#include        "type.h"
#include        "dt.h"
#include        "exh.h"
#include        "outbuf.h"

#include        "dwarf.h"
#include        "dwarf2.h"

static char __file__[] = __FILE__;      /* for tassert.h                */
#include        "tassert.h"

#define null NULL
typedef unsigned char ubyte;
typedef unsigned short ushort;


int actionTableInsert(Outbuffer *atbuf, int ttindex, int nextoffset);

uint uLEB128(ubyte **p);
int sLEB128(ubyte **p);
uint uLEB128size(uint value);
uint sLEB128size(int value);

void unittest_dwarfeh();

struct DwEhTableEntry
{
    uint start;
    uint end;           // 1 past end
    uint lpad;          // landing pad
    uint action;        // index into Action Table
    block *bcatch;      // catch block data
    int prev;           // index to enclosing entry (-1 for none)
};

struct DwEhTable
{
    DwEhTableEntry *ptr;    // pointer to table
    uint dim;               // current amount used
    uint capacity;

    DwEhTableEntry *index(uint i)
    {
        if (i >= dim) printf("i = %d dim = %d\n", i, dim);
        assert(i < dim);
        return ptr + i;
    }

    uint push()
    {
        assert(dim <= capacity);
        if (dim == capacity)
        {
            capacity += capacity + 16;
            ptr = (DwEhTableEntry *)::realloc(ptr, capacity * sizeof(DwEhTableEntry));
            assert(ptr);
        }
        memset(ptr + dim, 0, sizeof(DwEhTableEntry));
        return dim++;
    }
};

static DwEhTable dwehtable;

/****************************
 * Generate .gcc_except_table, aka LS
 * Params:
 *      sfunc = function to generate table for
 *      seg = .gcc_except_table segment
 *      et = buffer to insert table into
 *      scancode = true if there are destructors in the code (i.e. usednteh & EHcleanup)
 *      startoffset = size of function prolog
 *      retoffset = offset from start of function to epilog
 */

void genDwarfEh(Funcsym *sfunc, int seg, Outbuffer *et, bool scancode, uint startoffset, uint retoffset)
{
#ifdef DEBUG
    unittest_dwarfeh();
#endif

    /* LPstart = encoding of LPbase
     * LPbase = landing pad base (normally omitted)
     * TType = encoding of TTbase
     * TTbase = offset from next byte to past end of Type Table
     * CallSiteFormat = encoding of fields in Call Site Table
     * CallSiteTableSize = size in bytes of Call Site Table
     * Call Site Table[]:
     *    CallSiteStart
     *    CallSiteRange
     *    LandingPad
     *    ActionRecordPtr
     * Action Table
     *    TypeFilter
     *    NextRecordPtr
     * Type Table
     */

    et->reserve(100);
    block *startblock = sfunc->Sfunc->Fstartblock;
    //printf("genDwarfEh: func = %s, offset = x%x, startblock->Boffset = x%x, scancode = %d startoffset=x%x, retoffset=x%x\n",
      //sfunc->Sident, (int)sfunc->Soffset, (int)startblock->Boffset, scancode, startoffset, retoffset);

#if 0
    printf("------- before ----------\n");
    for (block *b = startblock; b; b = b->Bnext) WRblock(b);
    printf("-------------------------\n");
#endif

    uint startsize = et->size();
    assert((startsize & 3) == 0);       // should be aligned

    DwEhTable *deh = &dwehtable;
    deh->dim = 0;
    Outbuffer atbuf;
    Outbuffer cstbuf;

    /* Build deh table, and Action Table
     */
    int index = -1;
    block *bprev = null;
    // The first entry encompasses the entire function
    {
        uint i = deh->push();
        DwEhTableEntry *d = deh->index(i);
        d->start = startblock->Boffset + startoffset;
        d->end = startblock->Boffset + retoffset;
        d->lpad = 0;                    // no cleanup, no catches
        index = i;
    }
    for (block *b = startblock; b; b = b->Bnext)
    {
        if (index > 0 && b->Btry == bprev)
        {
            DwEhTableEntry *d = deh->index(index);
            d->end = b->Boffset;
            index = d->prev;
            if (bprev)
                bprev = bprev->Btry;
        }
        if (b->BC == BC_try)
        {
            uint i = deh->push();
            DwEhTableEntry *d = deh->index(i);
            d->start = b->Boffset;

            block *bf = b->nthSucc(1);
            if (bf->BC == BCjcatch)
            {
                d->lpad = bf->Boffset;
                d->bcatch = bf;
                uint *pat = bf->BS.BIJCATCH.actionTable;
                uint length = pat[0];
                assert(length);
                uint offset = -1;
                for (uint u = length; u; --u)
                {
                    /* Buy doing depth-first insertion into the Action Table,
                     * we can combine common tails.
                     */
                    offset = actionTableInsert(&atbuf, pat[u], offset);
                }
                d->action = offset + 1;
            }
            else
                d->lpad = bf->nthSucc(0)->Boffset;
            d->prev = index;
            index = i;
            bprev = b->Btry;
        }
        if (scancode)
        {
            uint coffset = b->Boffset;
            int n = 0;
            for (code *c = b->Bcode; c; c = code_next(c))
            {
                if (c->Iop == (ESCAPE | ESCdctor))
                {
                    uint i = deh->push();
                    DwEhTableEntry *d = deh->index(i);
                    d->start = coffset;
                    d->prev = index;
                    index = i;
                    ++n;
                }

                if (c->Iop == (ESCAPE | ESCddtor))
                {
                    assert(n > 0);
                    --n;
                    DwEhTableEntry *d = deh->index(index);
                    d->end = coffset;
                    d->lpad = coffset;
                    index = d->prev;
                }
                coffset += calccodsize(c);
            }
            assert(n == 0);
        }
    }
    //printf("deh->dim = %d\n", (int)deh->dim);

#if 1
    /* Build Call Site Table
     * Be sure to not generate empty entries,
     * and generate nested ranges reflecting the layout in the code.
     */
    assert(deh->dim);
    uint end = deh->index(0)->start;
    for (uint i = 0; i < deh->dim; ++i)
    {
        DwEhTableEntry *d = deh->index(i);
        if (d->start < d->end)
        {
#if ELFOBJ
                #define WRITE writeuLEB128
#elif MACHOBJ
                #define WRITE write32
#else
                assert(0);
#endif
                uint CallSiteStart = d->start - startblock->Boffset;
                cstbuf.WRITE(CallSiteStart);
                uint CallSiteRange = d->end - d->start;
                cstbuf.WRITE(CallSiteRange);
                uint LandingPad = d->lpad ? d->lpad - startblock->Boffset : 0;
                cstbuf.WRITE(LandingPad);
                uint ActionTable = d->action;
                cstbuf.writeuLEB128(ActionTable);
                //printf("\t%x %x %x %x\n", CallSiteStart, CallSiteRange, LandingPad, ActionTable);
                #undef WRITE
        }
    }
#else
    /* Build Call Site Table
     * Be sure to not generate empty entries,
     * and generate multiple entries for one DwEhTableEntry if the latter
     * is split by nested DwEhTableEntry's. This is based on the (undocumented)
     * presumption that there may not
     * be overlapping entries in the Call Site Table.
     */
    assert(deh->dim);
    uint end = deh->index(0)->start;
    for (uint i = 0; i < deh->dim; ++i)
    {
        uint j = i;
        do
        {
            DwEhTableEntry *d = deh->index(j);
            //printf(" [%d] start=%x end=%x lpad=%x action=%x bcatch=%p prev=%d\n",
            //  j, d->start, d->end, d->lpad, d->action, d->bcatch, d->prev);
            if (d->start <= end && end < d->end)
            {
                uint start = end;
                uint dend = d->end;
                if (i + 1 < deh->dim)
                {
                    DwEhTableEntry *dnext = deh->index(i + 1);
                    if (dnext->start < dend)
                        dend = dnext->start;
                }
                if (start < dend)
                {
#if ELFOBJ
                    #define WRITE writeLEB128
#elif MACHOBJ
                    #define WRITE write32
#else
                    assert(0);
#endif
                    uint CallSiteStart = start - startblock->Boffset;
                    cstbuf.WRITE(CallSiteStart);
                    uint CallSiteRange = dend - start;
                    cstbuf.WRITE(CallSiteRange);
                    uint LandingPad = d->lpad - startblock->Boffset;
                    cstbuf.WRITE(LandingPad);
                    uint ActionTable = d->action;
                    cstbuf.WRITE(ActionTable);
                    //printf("\t%x %x %x %x\n", CallSiteStart, CallSiteRange, LandingPad, ActionTable);
                    #undef WRITE
                }

                end = dend;
            }
        } while (j--);
    }
#endif

    /* Write LSDT header */
    const ubyte LPstart = DW_EH_PE_omit;
    et->writeByte(LPstart);
    uint LPbase = 0;
    if (LPstart != DW_EH_PE_omit)
        et->writeuLEB128(LPbase);

    const ubyte TType = (config.flags3 & CFG3pic)
                                ? DW_EH_PE_indirect | DW_EH_PE_pcrel | DW_EH_PE_sdata4
                                : DW_EH_PE_absptr | DW_EH_PE_udata4;
    et->writeByte(TType);

    /* Compute TTbase, which is the sum of:
     *  1. CallSiteFormat
     *  2. encoding of CallSiteTableSize
     *  3. Call Site Table size
     *  4. Action Table size
     *  5. 4 byte alignment
     *  6. Types Table
     * Iterate until it converges.
     */
    uint TTbase = 1;
    uint CallSiteTableSize = cstbuf.size();
    uint oldTTbase;
    do
    {
        oldTTbase = TTbase;
        uint start = (et->size() - startsize) + uLEB128size(TTbase);
        TTbase = 1 +
                uLEB128size(CallSiteTableSize) +
                CallSiteTableSize +
                atbuf.size();
        uint sz = start + TTbase;
        TTbase += -sz & 3;      // align to 4
        TTbase += sfunc->Sfunc->typesTableDim * 4;
    } while (TTbase != oldTTbase);

    if (TType != DW_EH_PE_omit)
        et->writeuLEB128(TTbase);
    uint TToffset = TTbase + et->size() - startsize;

#if ELFOBJ
    const ubyte CallSiteFormat = DW_EH_PE_absptr | DW_EH_PE_uleb128;
#elif MACHOBJ
    const ubyte CallSiteFormat = DW_EH_PE_absptr | DW_EH_PE_udata4;
#else
    assert(0);
#endif
    et->writeByte(CallSiteFormat);
    et->writeuLEB128(CallSiteTableSize);


    /* Insert Call Site Table */
    et->write(&cstbuf);

    /* Insert Action Table */
    et->write(&atbuf);

    /* Align to 4 */
    for (uint n = (-et->size() & 3); n; --n)
        et->writeByte(0);

    /* Write out Types Table in reverse */
    Symbol **typesTable = sfunc->Sfunc->typesTable;
    for (int i = sfunc->Sfunc->typesTableDim; i--; )
    {
        Symbol *s = typesTable[i];
        /* MACHOBJ 64: pcrel 1 length 1 extern 1 RELOC_GOT
         *         32: [0] address x004c pcrel 0 length 2 value x224 type 4 RELOC_LOCAL_SECTDIFF
         *             [1] address x0000 pcrel 0 length 2 value x160 type 1 RELOC_PAIR
         */
        dwarf_reftoident(seg, et->size(), s, 0);
    }
    assert(TToffset == et->size() - startsize);
}


/****************************
 * Insert action (ttindex, offset) in Action Table
 * if it is not already there.
 * Params:
 *      atbuf = Action Table
 *      ttindex = Types Table index (1..)
 *      offset = offset of next action, -1 for none
 * Returns:
 *      offset of inserted action
 */
int actionTableInsert(Outbuffer *atbuf, int ttindex, int nextoffset)
{
    //printf("actionTableInsert(%d, %d)\n", ttindex, nextoffset);
    ubyte *p;
    for (p = atbuf->buf; p < atbuf->p; )
    {
        int offset = p - atbuf->buf;
        int TypeFilter = sLEB128(&p);
        int nrpoffset = p - atbuf->buf;
        int NextRecordPtr = sLEB128(&p);

        if (ttindex == TypeFilter &&
            nextoffset == nrpoffset + NextRecordPtr)
            return offset;
    }
    assert(p == atbuf->p);
    int offset = atbuf->size();
    atbuf->writesLEB128(ttindex);
    if (nextoffset == -1)
        nextoffset = 0;
    else
        nextoffset -= atbuf->size();
    atbuf->writesLEB128(nextoffset);
    return offset;
}

#ifdef DEBUG
void unittest_actionTableInsert()
{
    Outbuffer atbuf;
    static int tt1[] = { 1,2,3 };
    static int tt2[] = { 2 };

    int offset = -1;
    for (size_t i = sizeof(tt1)/sizeof(tt1[0]); i--; )
    {
        offset = actionTableInsert(&atbuf, tt1[i], offset);
    }
    offset = -1;
    for (size_t i = sizeof(tt2)/sizeof(tt2[0]); i--; )
    {
        offset = actionTableInsert(&atbuf, tt2[i], offset);
    }

    static ubyte result[] = { 3,0,2,0x7D,1,0x7D,2,0 };
    //for (int i = 0; i < atbuf.size(); ++i) printf(" %02x\n", atbuf.buf[i]);
    assert(sizeof(result) == atbuf.size());
    int r = memcmp(result, atbuf.buf, atbuf.size());
    assert(r == 0);
}
#endif

/******************************
 * Decode Unsigned LEB128.
 * Params:
 *      p = pointer to data pointer, *p is updated
 *      to point past decoded value
 * Returns:
 *      decoded value
 * See_Also:
 *      https://en.wikipedia.org/wiki/LEB128
 */
uint uLEB128(ubyte **p)
{
    ubyte *q = *p;
    uint result = 0;
    uint shift = 0;
    while (1)
    {
        ubyte byte = *q++;
        result |= (byte & 0x7F) << shift;
        if ((byte & 0x80) == 0)
            break;
        shift += 7;
    }
    *p = q;
    return result;
}

/******************************
 * Decode Signed LEB128.
 * Params:
 *      p = pointer to data pointer, *p is updated
 *      to point past decoded value
 * Returns:
 *      decoded value
 * See_Also:
 *      https://en.wikipedia.org/wiki/LEB128
 */
int sLEB128(ubyte **p)
{
    ubyte *q = *p;
    ubyte byte;

    int result = 0;
    uint shift = 0;
    while (1)
    {
        byte = *q++;
        result |= (byte & 0x7F) << shift;
        shift += 7;
        if ((byte & 0x80) == 0)
            break;
    }
    if (shift < sizeof(result) * 8 && (byte & 0x40))
        result |= -(1 << shift);
    *p = q;
    return result;
}

/******************************
 * Determine size of Signed LEB128 encoded value.
 * Params:
 *      value = value to be encoded
 * Returns:
 *      length of decoded value
 * See_Also:
 *      https://en.wikipedia.org/wiki/LEB128
 */
uint sLEB128size(int value)
{
    uint size = 0;
    while (1)
    {
        ++size;
        ubyte b = value & 0x40;

        value >>= 7;            // arithmetic right shift
        if (value == 0 && !b ||
            value == -1 && b)
        {
             break;
        }
    }
    return size;
}

/******************************
 * Determine size of Unsigned LEB128 encoded value.
 * Params:
 *      value = value to be encoded
 * Returns:
 *      length of decoded value
 * See_Also:
 *      https://en.wikipedia.org/wiki/LEB128
 */
uint uLEB128size(uint value)
{
    uint size = 1;
    while ((value >>= 7) != 0)
        ++size;
    return size;
}

#ifdef DEBUG
void unittest_LEB128()
{
    Outbuffer buf;

    static int values[] =
    {
        0,1,2,3,300,4000,50000,600000,
        -0,-1,-2,-3,-300,-4000,-50000,-600000,
    };

    for (size_t i = 0; i < sizeof(values)/sizeof(values[0]); ++i)
    {
        const int value = values[i];

        buf.reset();
        buf.writeuLEB128(value);
        assert(buf.size() == uLEB128size(value));
        ubyte *p = buf.buf;
        int result = uLEB128(&p);
        assert(p == buf.p);
        assert(result == value);

        buf.reset();
        buf.writesLEB128(value);
        assert(buf.size() == sLEB128size(value));
        p = buf.buf;
        result = sLEB128(&p);
        assert(p == buf.p);
        assert(result == value);
    }
}
#endif

#ifdef DEBUG

void unittest_dwarfeh()
{
    static bool run = false;
    if (run)
        return;
    run = true;

    unittest_LEB128();
    unittest_actionTableInsert();
}

#endif
