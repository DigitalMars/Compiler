// Copyright (C) 1994-1998 by Symantec
// Copyright (C) 2000-2009 by Digital Mars
// All Rights Reserved
// http://www.digitalmars.com
/*
 * This source file is made available for personal use
 * only. The license is in /dmd/src/dmd/backendlicense.txt
 * or /dm/src/dmd/backendlicense.txt
 * For any other uses, please contact Digital Mars.
 */

// String package that uses alloca() so that storage is free'd
// upon exit from the function.
// Useful for temporary strings.

#include <string.h>
#include <stdlib.h>

#define DEFINE_ALLOCA_HEAP
// Be careful that all functions actually get inlined, or else they
// will crash. This may not work on compilers other than SC.
// I don't know if it works on the Mac compiler.

__inline char *alloca_strdup(const char *p)
{       size_t len;
        char *s;

        len = strlen(p) + 1;
        s = (char *)alloca(len);
        return (char *) memcpy(s,p,len);
}

__inline char *alloca_strdup2(const char *p1,const char *p2)
{       size_t len1;
        size_t len2;
        char *s;

        len1 = strlen(p1);
        len2 = strlen(p2) + 1;
        s = (char *)alloca(len1 + len2);
        memcpy(s + len1,p2,len2);
        return (char *) memcpy(s,p1,len1);
}

__inline char *alloca_strdup3(const char *p1,const char *p2,const char *p3)
{       size_t len1;
        size_t len2;
        size_t len3;
        char *s;

        len1 = strlen(p1);
        len2 = strlen(p2);
        len3 = strlen(p3) + 1;
        s = (char *)alloca(len1 + len2 + len3);
        memcpy(s + len1,p2,len2);
        memcpy(s + len1 + len2,p3,len3);
        return (char *) memcpy(s,p1,len1);
}

__inline char *alloca_substring(const char *p,int start,int end)
{       char *s;
        size_t len;

        len = end - start;
        s = (char *)alloca(len + 1);
        s[len] = 0;
        return (char *) memcpy(s,p + start,len);
}
