
/**
 * Implementation of the
 * $(LINK2 http://www.digitalmars.com/download/freecompiler.html, Digital Mars C/C++ Compiler).
 *
 * Copyright:   Copyright (c) 1989-1998 by Symantec, All Rights Reserved
 *              Copyright (c) 2000-2017 by Digital Mars, All Rights Reserved
 * Authors:     $(LINK2 http://www.digitalmars.com, Walter Bright)
 * License:     Distributed under the Boost Software License, Version 1.0.
 *              http://www.boost.org/LICENSE_1_0.txt
 * Source:      https://github.com/DigitalMars/Compiler/blob/master/dm/src/dmc/rawstring.d
 */

/* C++ raw string type */

module rawstring;

extern (C++):

import dmd.backend.global;

import msgs2;

struct RawString
{
    enum { RAWdchar, RAWstring, RAWend, RAWdone, RAWerror }
    int rawstate;
    char[16 + 1] dcharbuf;
    int dchari;

    void init()
    {
        rawstate = RAWdchar;
        dchari = 0;
    }

    bool inString(ubyte c)
    {
        switch (rawstate)
        {
            case RAWdchar:
                if (c == '(')       // end of d-char-string
                {
                    dcharbuf[dchari] = 0;
                    rawstate = RAWstring;
                }
                else if (c == ' '  || c == '('  || c == ')'  ||
                         c == '\\' || c == '\t' || c == '\v' ||
                         c == '\f' || c == '\n')
                {
                    lexerr(EM_invalid_dchar, c);
                    rawstate = RAWerror;
                }
                else if (dchari >= dcharbuf.sizeof - 1)
                {
                    lexerr(EM_string2big, dcharbuf.sizeof - 1);
                    rawstate = RAWerror;
                }
                else
                {
                    dcharbuf[dchari] = c;
                    ++dchari;
                }
                break;

            case RAWstring:
                if (c == ')')
                {
                    dchari = 0;
                    rawstate = RAWend;
                }
                break;

            case RAWend:
                if (c == dcharbuf[dchari])
                {
                    ++dchari;
                }
                else if (dcharbuf[dchari] == 0)
                {
                    if (c == '"')
                        rawstate = RAWdone;
                    else
                        rawstate = RAWstring;
                }
                else if (c == ')')
                {
                    // Rewind ')' dcharbuf[0..dchari]
                    dchari = 0;
                }
                else
                {
                    // Rewind ')' dcharbuf[0..dchari]
                    rawstate = RAWstring;
                }
                break;

            default:
                assert(0);
        }
        return rawstate != RAWdone && rawstate != RAWerror;
    }
}

