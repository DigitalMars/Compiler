
/**
 * Implementation of the
 * $(LINK2 http://www.digitalmars.com/download/freecompiler.html, Digital Mars C/C++ Compiler).
 *
 * Copyright:   Copyright (c) 1989-1998 by Symantec, All Rights Reserved
 *              Copyright (c) 2000-2017 by Digital Mars, All Rights Reserved
 * Authors:     $(LINK2 http://www.digitalmars.com, Walter Bright)
 * License:     Distributed under the Boost Software License, Version 1.0.
 *              http://www.boost.org/LICENSE_1_0.txt
 * Source:      https://github.com/DigitalMars/Compiler/blob/master/dm/src/dmc/phstring.d
 */

/* Simple string type that is allocated in the precompiled header                  */

module phstring;

import core.stdc.stdio;
import core.stdc.stdlib;
import core.stdc.string;

import dmd.backend.dlist;

import parser;
import precomp;

extern (C++):

struct phstring_t
{
    size_t length() { return dim; }

    int cmp(phstring_t s2, int function(void *,void *) func)
    {   int result = 0;
        for (int i = 0; 1; ++i)
        {
            if (i == length())
            {
                if (i < s2.length())
                    result = -1;    // <
                break;
            }
            if (i == s2.length())
            {
                result = 1;         // >
                break;
            }
            result = (*func)((this)[i], s2[i]);
            if (result)
                break;
        }
        return result;
    }

    bool empty() { return dim == 0; }

    char* opIndex(size_t index)
    {
        return dim == 1 ? cast(char *)data : data[index];
    }

    void push(const(char)* s)
    {
        if (dim == 0)
        {
            *cast(const(char)* *)&data = s;
        }
        else if (dim == 1)
        {
            version (SPP)
            {
                char **p = cast(char **)malloc((dim + 1) * (char *).sizeof);
            }
            else
            {
                char **p = cast(char **)ph_malloc((dim + 1) * (char *).sizeof);
            }
            assert(p);
            p[0] = cast(char *)data;
            p[1] = cast(char *)s;
            data = p;
        }
        else
        {
            version (SPP)
            {
                data = cast(char **)realloc(data, (dim + 1) * (char *).sizeof);
            }
            else
            {
                data = cast(char **)ph_realloc(data, (dim + 1) * (char *).sizeof);
            }
            data[dim] = cast(char*)s;
        }
        ++dim;
    }

    version (SPP)
    {
    }
    else
    {
        void hydrate()
        {
            if (dim)
            {
                if (isdehydrated(data))
                    ph_hydrate(cast(void**)&data);
                if (dim > 1)
                {
                    for (size_t i = 0; i < dim; ++i)
                    {
                        if (isdehydrated(data[i]))
                            ph_hydrate(cast(void**)&data[i]);
                    }
                }
            }
        }

        void dehydrate()
        {
            if (dim && !isdehydrated(data))
            {
                if (dim > 1)
                {
                    for (size_t i = 0; i < dim; ++i)
                    {
                        if (!isdehydrated(data[i]))
                            ph_dehydrate(&data[i]);
                    }
                }
                ph_dehydrate(&data);
            }
        }
    }


    /**********************************************
     * Search for string.
     * Returns:
     *      index of string if found, -1 if not
     */

    int find(const(char)* s)
    {
        //printf("phstring_t::find(%s)\n", s);
        size_t len = strlen(s) + 1;
        if (dim == 1)
        {
            if (memcmp(s,data,len) == 0)
                return 0;
        }
        else
        {
            for (int i = 0; i < dim; ++i)
            {
                if (memcmp(s,data[i],len) == 0)
                    return i;
            }
        }
        return -1;
    }

    void free(list_free_fp freeptr)
    {
        if (dim)
        {
            if (freeptr)
            {
                if (dim == 1)
                    (*freeptr)(data);
                else
                    for (size_t i = 0; i < dim; ++i)
                        (*freeptr)(data[i]);
            }
            if (dim > 1)
            {
                version (SPP)
                    .free(data);
                else
                    ph_free(data);
            }
            dim = 0;
            data = null;
        }
    }

//  private:
    size_t dim;
    char** data;
}






