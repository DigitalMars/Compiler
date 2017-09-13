
/**
 * Implementation of the
 * $(LINK2 http://www.digitalmars.com/download/freecompiler.html, Digital Mars C/C++ Compiler).
 *
 * Copyright:   Copyright (c) 1989-1998 by Symantec, All Rights Reserved
 *              Copyright (c) 2000-2017 by Digital Mars, All Rights Reserved
 * Authors:     $(LINK2 http://www.digitalmars.com, Walter Bright)
 * License:     Distributed under the Boost Software License, Version 1.0.
 *              http://www.boost.org/LICENSE_1_0.txt
 * Source:      https://github.com/DigitalMars/Compiler/blob/master/dm/src/dmc/rawstring.di
 */

/* C++ raw string type */

module rawstring;

extern (C++):

struct RawString
{
    enum { RAWdchar, RAWstring, RAWend, RAWdone, RAWerror }
    int rawstate;
    char[16 + 1] dcharbuf;
    int dchari;

    void init();
    bool inString(ubyte c);
}


