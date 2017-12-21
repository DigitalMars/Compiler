/**
 * Implementation of the
 * $(LINK2 http://www.digitalmars.com/download/freecompiler.html, Digital Mars C/C++ Compiler).
 *
 * Copyright:   Copyright (c) 1999-2017 by Digital Mars, All Rights Reserved
 * Authors:     $(LINK2 http://www.digitalmars.com, Walter Bright)
 * License:     Distributed under the Boost Software License, Version 1.0.
 *              http://www.boost.org/LICENSE_1_0.txt
 * Source:      https://github.com/DigitalMars/Compiler/blob/master/dm/src/dmc/html.di
 */

module html;

import dmd.backend.outbuf : Outbuffer;

extern (C++):

struct Html
{
    const(char)* sourcename;

    ubyte* base;       // pointer to start of buffer
    ubyte* end;        // past end of buffer
    ubyte* p;          // current character
    uint linnum;       // current line number
    Outbuffer* dbuf;   // code source buffer
    int inCode;        // !=0 if in code


    void initialize(const(char)* sourcename, ubyte *base, uint length);

    void error(const(char)* format, ...);
    void extractCode(Outbuffer* buf);
    void skipTag();
    void skipString();
    ubyte* skipWhite(ubyte* q);
    void scanComment();
    int isCommentStart();
    void scanCDATA();
    int isCDATAStart();
    int charEntity();
}
