
// Copyright (c) 2011-2011 by Digital Mars
// All Rights Reserved
// written by Walter Bright
// http://www.digitalmars.com
// License for redistribution is by either the Artistic License
// in artistic.txt, or the GNU General Public License in gpl.txt.
// See the included readme.txt for details.


#include <stdlib.h>

#include "tinfo.h"


// void*

TypeInfo_Pvoid ti_pvoid;

const char* TypeInfo_Pvoid::toString()
{
    return "void*";
}

hash_t TypeInfo_Pvoid::getHash(void *p)
{   void* s = *(void **)p;
    return (hash_t)s;
}

int TypeInfo_Pvoid::equals(void *p1, void *p2)
{
    void* s1 = *(void**)p1;
    void* s2 = *(void**)p2;

    return s1 == s2;
}

int TypeInfo_Pvoid::compare(void *p1, void *p2)
{
    void* s1 = *(void**)p1;
    void* s2 = *(void**)p2;

    return (s1 < s2) ? -1 : ((s1 == s2) ? 0 : 1);
}

size_t TypeInfo_Pvoid::tsize()
{
    return sizeof(void*);
}

void TypeInfo_Pvoid::swap(void *p1, void *p2)
{
}

