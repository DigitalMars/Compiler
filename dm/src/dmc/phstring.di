
/**
 * Implementation of the
 * $(LINK2 http://www.digitalmars.com/download/freecompiler.html, Digital Mars C/C++ Compiler).
 *
 * Copyright:   Copyright (c) 1989-1998 by Symantec, All Rights Reserved
 *              Copyright (c) 2000-2017 by Digital Mars, All Rights Reserved
 * Authors:     $(LINK2 http://www.digitalmars.com, Walter Bright)
 * License:     Distributed under the Boost Software License, Version 1.0.
 *              http://www.boost.org/LICENSE_1_0.txt
 * Source:      https://github.com/DigitalMars/Compiler/blob/master/dm/src/dmc/phstring.di
 */

/* Simple string type that is allocated in the precompiled header                  */

module phstring;

import dlist;

extern (C++):

struct phstring_t
{
    size_t length() { return dim; }

    int cmp(phstring_t s2, int function(void *,void *) func);

    bool empty() { return dim == 0; }

    char* opIndex(size_t index)
    {
        return dim == 1 ? cast(char *)data : data[index];
    }

    void push(const(char)* s);

    void hydrate();

    void dehydrate();

    int find(const(char)* s);

    void free(list_free_fp freeptr);

//  private:
    size_t dim;
    char** data;
}


