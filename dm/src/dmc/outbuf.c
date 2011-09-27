// Copyright (C) 1994-1998 by Symantec
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

// Output buffer

#include <string.h>
#include <stdlib.h>
#include <time.h>

#include "cc.h"

#include "outbuf.h"
#include "mem.h"

#if DEBUG
static char __file__[] = __FILE__;      // for tassert.h
#include        "tassert.h"
#endif

Outbuffer::Outbuffer()
{
    buf = NULL;
    pend = NULL;
    p = NULL;
    len = 0;
    inc = 0;
}

Outbuffer::Outbuffer(unsigned bufinc)
{
    buf = NULL;
    pend = NULL;
    p = NULL;
    len = 0;
    inc = bufinc;
}

Outbuffer::~Outbuffer()
{
#if MEM_DEBUG
    mem_free(buf);
#else
    if (buf)
        free(buf);
#endif
}

void Outbuffer::reset()
{
    p = buf;
}

// Reserve nbytes in buffer
void Outbuffer::reserve(unsigned nbytes)
{
    if (pend - p < nbytes)
    {   unsigned oldlen = len;
        unsigned used = p - buf;

        if (inc > nbytes)
        {
            len = used + inc;
        }
        else
        {
            len = used + nbytes;
            if (len < 2 * oldlen)
            {   len = 2 * oldlen;
                if (len < 8)
                    len = 8;
            }
        }
#if MEM_DEBUG
        buf = (unsigned char *)mem_realloc(buf, len);
#else
        if (buf)
            buf = (unsigned char *) realloc(buf,len);
        else
            buf = (unsigned char *) malloc(len);
#endif
        pend = buf + len;
        p = buf + used;
    }
}

// Position buffer for output at a specified location and size.
//      If data will extend buffer size, reserve space
//      If data will rewrite existing data
//              position for write and return previous buffer size
//
//      If data will append to buffer
//              position for write and return new size
int Outbuffer::position(unsigned pos, unsigned nbytes)
{
    int current_sz = size();
    unsigned char *fend = buf+pos+nbytes;       // future end of buffer
    if (fend >= pend)
    {
        reserve (fend - pend);
    }
    setsize(pos);
    return pos+nbytes > current_sz ? pos+nbytes : current_sz;
}

// Write an array to the buffer.
void Outbuffer::write(const void *b, int len)
{
    if (pend - p < len)
        reserve(len);
    memcpy(p,b,len);
    p += len;
}

// Write n zeros to the buffer.
void *Outbuffer::writezeros(unsigned len)
{
    if (pend - p < len)
        reserve(len);
    void *pstart = memset(p,0,len);
    p += len;
    return pstart;
}

/**
 * Writes an 8 bit byte.
 */
void Outbuffer::writeByte(int v)
{
    if (pend == p)
        reserve(1);
    *p++ = v;
}

/**
 * Writes a 32 bit int.
 */
void Outbuffer::write32(int v)
{
    if (pend - p < 4)
        reserve(4);
    *(int *)p = v;
    p += 4;
}

/**
 * Writes a 64 bit long.
 */

#if __INTSIZE == 4
void Outbuffer::write64(long long v)
{
    if (pend - p < 8)
        reserve(8);
    *(long long *)p = v;
    p += 8;
}
#endif

/**
 * Writes a 32 bit float.
 */
void Outbuffer::writeFloat(float v)
{
    if (pend - p < sizeof(float))
        reserve(sizeof(float));
    *(float *)p = v;
    p += sizeof(float);
}

/**
 * Writes a 64 bit double.
 */
void Outbuffer::writeDouble(double v)
{
    if (pend - p < sizeof(double))
        reserve(sizeof(double));
    *(double *)p = v;
    p += sizeof(double);
}

/**
 * Writes a String as a sequence of bytes.
 */
void Outbuffer::write(const char *s)
{
    write(s,strlen(s));
}


/**
 * Writes a String as a sequence of bytes.
 */
void Outbuffer::write(const unsigned char *s)
{
    write(s,strlen((const char *)s));
}


/**
 * Writes a NULL terminated String
 */
void Outbuffer::writeString(const char *s)
{
    write(s,strlen(s)+1);
}

/**
 * Inserts string at beginning of buffer.
 */

void Outbuffer::prependBytes(const char *s)
{
    size_t len = strlen(s);

    reserve(len);
    memmove(buf + len,buf,p - buf);
    memcpy(buf,s,len);
    p += len;
}

/**
 */

void Outbuffer::bracket(char c1,char c2)
{
    reserve(2);
    memmove(buf + 1,buf,p - buf);
    buf[0] = c1;
    p[1] = c2;
    p += 2;
}

/**
 * Convert to a string.
 */

char *Outbuffer::toString()
{
    if (pend == p)
        reserve(1);
    *p = 0;                     // terminate string
    return (char *)buf;
}

/**
 * Set current size of buffer.
 */

void Outbuffer::setsize(unsigned size)
{
    p = buf + size;
}

void Outbuffer::writesLEB128(int value)
{
    while (1)
    {
        unsigned char b = value & 0x7F;

        value >>= 7;            // arithmetic right shift
        if (value == 0 && !(b & 0x40) ||
            value == -1 && (b & 0x40))
        {
             writeByte(b);
             break;
        }
        writeByte(b | 0x80);
    }
}

void Outbuffer::writeuLEB128(unsigned value)
{
    do
    {   unsigned char b = value & 0x7F;

        value >>= 7;
        if (value)
            b |= 0x80;
        writeByte(b);
    } while (value);
}

