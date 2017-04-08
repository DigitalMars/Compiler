/**
 * Implementation of the
 * $(LINK2 http://www.digitalmars.com/download/freecompiler.html, Digital Mars C/C++ Compiler).
 *
 * Copyright:   Copyright (c) 1999-2017 by Digital Mars, All Rights Reserved
 * Authors:     $(LINK2 http://www.digitalmars.com, Walter Bright)
 * License:     Distributed under the Boost Software License, Version 1.0.
 *              http://www.boost.org/LICENSE_1_0.txt
 * Source:      https://github.com/DigitalMars/Compiler/blob/master/dm/src/dmc/html.h
 */


#if MARS
struct OutBuffer;
#else
struct Outbuffer;
#endif

struct Html
{
    const char *sourcename;

    unsigned char *base;        // pointer to start of buffer
    unsigned char *end;         // past end of buffer
    unsigned char *p;           // current character
    unsigned linnum;            // current line number
#if MARS
    OutBuffer *dbuf;            // code source buffer
#else
    Outbuffer *dbuf;            // code source buffer
#endif
    int inCode;                 // !=0 if in code


    Html(const char *sourcename, unsigned char *base, unsigned length);

    void error(const char *format, ...);
#if MARS
    void extractCode(OutBuffer *buf);
#else
    void extractCode(Outbuffer *buf);
#endif
    void skipTag();
    void skipString();
    unsigned char *skipWhite(unsigned char *q);
    void scanComment();
    int isCommentStart();
    void scanCDATA();
    int isCDATAStart();
    int charEntity();
    static int namedEntity(unsigned char *p, int length);
};
