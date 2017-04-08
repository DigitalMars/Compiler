/**
 * Implementation of the
 * $(LINK2 http://www.digitalmars.com/download/freecompiler.html, Digital Mars C/C++ Compiler).
 *
 * Copyright:   Copyright (c) 2003-2017 by Digital Mars, All Rights Reserved
 * Authors:     $(LINK2 http://www.digitalmars.com, Walter Bright)
 * License:     Distributed under the Boost Software License, Version 1.0.
 *              http://www.boost.org/LICENSE_1_0.txt
 * Source:      https://github.com/DigitalMars/Compiler/blob/master/dm/src/dmc/utf.h
 */

#ifndef DMD_UTF_H
#define DMD_UTF_H


typedef unsigned dchar_t;

int utf_isValidDchar(dchar_t c);

const char *utf_decodeChar(unsigned char *s, size_t len, size_t *pidx, dchar_t *presult);
const char *utf_decodeWchar(unsigned short *s, size_t len, size_t *pidx, dchar_t *presult);

const char *utf_validateString(unsigned char *s, size_t len);

extern int isUniAlpha(dchar_t);

#endif
