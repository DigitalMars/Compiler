/**
 * Implementation of the
 * $(LINK2 http://www.digitalmars.com/download/freecompiler.html, Digital Mars C/C++ Compiler).
 *
 * Copyright:   Copyright (c) 2003-2017 by Digital Mars, All Rights Reserved
 * Authors:     $(LINK2 http://www.digitalmars.com, Walter Bright)
 * License:     Distributed under the Boost Software License, Version 1.0.
 *              http://www.boost.org/LICENSE_1_0.txt
 * Source:      https://github.com/DigitalMars/Compiler/blob/master/dm/src/dmc/utf.d
 */

module utf;

import core.stdc.stdio;

extern (C++):

alias dchar_t = uint;

int utf_isValidDchar(dchar_t c)
{
    return c < 0xD800 ||
        (c > 0xDFFF && c <= 0x10FFFF && c != 0xFFFE && c != 0xFFFF);
}

extern (D) private __gshared immutable ubyte[256] UTF8stride =
[
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
    2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
    3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,
    4,4,4,4,4,4,4,4,5,5,5,5,6,6,0xFF,0xFF,
];

/**
 * stride() returns the length of a UTF-8 sequence starting at index i
 * in string s.
 * Returns:
 *  The number of bytes in the UTF-8 sequence or
 *  0xFF meaning s[i] is not the start of of UTF-8 sequence.
 */

uint stride(ubyte* s, size_t i)
{
    uint result = UTF8stride[s[i]];
    return result;
}

/********************************************
 * Decode a single UTF-8 character sequence.
 * Returns:
 *      NULL    success
 *      !=NULL  error message string
 */

const(char)* utf_decodeChar(ubyte *s, size_t len, size_t *pidx, dchar_t *presult)
{
    dchar_t V;
    size_t i = *pidx;
    ubyte u = s[i];

    //printf("utf_decodeChar(s = %02x, %02x, %02x len = %d)\n", u, s[1], s[2], len);

    assert(i >= 0 && i < len);

    if (u & 0x80)
    {   uint n;
        ubyte u2;

        /* The following encodings are valid, except for the 5 and 6 byte
         * combinations:
         *      0xxxxxxx
         *      110xxxxx 10xxxxxx
         *      1110xxxx 10xxxxxx 10xxxxxx
         *      11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
         *      111110xx 10xxxxxx 10xxxxxx 10xxxxxx 10xxxxxx
         *      1111110x 10xxxxxx 10xxxxxx 10xxxxxx 10xxxxxx 10xxxxxx
         */
        for (n = 1; ; n++)
        {
            if (n > 4)
                goto Lerr;              // only do the first 4 of 6 encodings
            if (((u << n) & 0x80) == 0)
            {
                if (n == 1)
                    goto Lerr;
                break;
            }
        }

        // Pick off (7 - n) significant bits of B from first byte of octet
        V = cast(dchar_t)(u & ((1 << (7 - n)) - 1));

        if (i + (n - 1) >= len)
            goto Lerr;                  // off end of string

        /* The following combinations are overlong, and illegal:
         *      1100000x (10xxxxxx)
         *      11100000 100xxxxx (10xxxxxx)
         *      11110000 1000xxxx (10xxxxxx 10xxxxxx)
         *      11111000 10000xxx (10xxxxxx 10xxxxxx 10xxxxxx)
         *      11111100 100000xx (10xxxxxx 10xxxxxx 10xxxxxx 10xxxxxx)
         */
        u2 = s[i + 1];
        if ((u & 0xFE) == 0xC0 ||
            (u == 0xE0 && (u2 & 0xE0) == 0x80) ||
            (u == 0xF0 && (u2 & 0xF0) == 0x80) ||
            (u == 0xF8 && (u2 & 0xF8) == 0x80) ||
            (u == 0xFC && (u2 & 0xFC) == 0x80))
            goto Lerr;                  // overlong combination

        for (uint j = 1; j != n; j++)
        {
            u = s[i + j];
            if ((u & 0xC0) != 0x80)
                goto Lerr;                      // trailing bytes are 10xxxxxx
            V = (V << 6) | (u & 0x3F);
        }
        if (!utf_isValidDchar(V))
            goto Lerr;
        i += n;
    }
    else
    {
        V = cast(dchar_t) u;
        i++;
    }

    assert(utf_isValidDchar(V));
    *pidx = i;
    *presult = V;
    return null;

  Lerr:
    *presult = cast(dchar_t) s[i];
    *pidx = i + 1;
    return "invalid UTF-8 sequence";
}

/***************************************************
 * Validate a UTF-8 string.
 * Returns:
 *      NULL    success
 *      !=NULL  error message string
 */

const(char)* utf_validateString(ubyte *s, size_t len)
{
    size_t idx;
    const(char)* err = null;
    dchar_t dc;

    for (idx = 0; idx < len; )
    {
        err = utf_decodeChar(s, len, &idx, &dc);
        if (err)
            break;
    }
    return err;
}


/********************************************
 * Decode a single UTF-16 character sequence.
 * Returns:
 *      null    success
 *      !=null  error message string
 */


const(char)* utf_decodeWchar(ushort *s, size_t len, size_t *pidx, dchar_t *presult)
{
    const(char)* msg;
    size_t i = *pidx;
    uint u = s[i];

    assert(i >= 0 && i < len);
    if (u & ~0x7F)
    {   if (u >= 0xD800 && u <= 0xDBFF)
        {   uint u2;

            if (i + 1 == len)
            {   msg = "surrogate UTF-16 high value past end of string";
                goto Lerr;
            }
            u2 = s[i + 1];
            if (u2 < 0xDC00 || u2 > 0xDFFF)
            {   msg = "surrogate UTF-16 low value out of range";
                goto Lerr;
            }
            u = ((u - 0xD7C0) << 10) + (u2 - 0xDC00);
            i += 2;
        }
        else if (u >= 0xDC00 && u <= 0xDFFF)
        {   msg = "unpaired surrogate UTF-16 value";
            goto Lerr;
        }
        else if (u == 0xFFFE || u == 0xFFFF)
        {   msg = "illegal UTF-16 value";
            goto Lerr;
        }
        else
            i++;
    }
    else
    {
        i++;
    }

    assert(utf_isValidDchar(u));
    *pidx = i;
    *presult = cast(dchar_t)u;
    return null;

  Lerr:
    *presult = cast(dchar_t)s[i];
    *pidx = i + 1;
    return msg;
}

