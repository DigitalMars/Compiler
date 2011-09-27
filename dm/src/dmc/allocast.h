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

#if !MACINTOSH
#define DEFINE_ALLOCA_HEAP
// Be careful that all functions actually get inlined, or else they
// will crash. This may not work on compilers other than SC.
// I don't know if it works on the Mac compiler.

__inline char *alloca_strdup(const char *p)
{	size_t len;
	char *s;

	len = strlen(p) + 1;
	s = (char *)alloca(len);
	return (char *) memcpy(s,p,len);
}

__inline char *alloca_strdup2(const char *p1,const char *p2)
{	size_t len1;
	size_t len2;
	char *s;

	len1 = strlen(p1);
	len2 = strlen(p2) + 1;
	s = (char *)alloca(len1 + len2);
	memcpy(s + len1,p2,len2);
	return (char *) memcpy(s,p1,len1);
}

__inline char *alloca_strdup3(const char *p1,const char *p2,const char *p3)
{	size_t len1;
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
{	char *s;
	size_t len;

	len = end - start;
	s = (char *)alloca(len + 1);
	s[len] = 0;
	return (char *) memcpy(s,p + start,len);
}
#else
void err_nomem(void);
struct __Alloca_Heap
{
    char buf[1024];	// buffer
    char *p;	// high water mark
    unsigned nleft;	// number of bytes left
};
//
// alloca is a problem because memory will not be freed on the Mac because we do not supply alloca
//
inline char * __mac_alloca(__Alloca_Heap *heap, size_t size)		
{ 
	char *p;
	if (size >= heap->nleft) err_nomem();
	if ((p = heap->p) == NULL)		// Initialize p if it is NULL
	    p = heap->p = heap->buf;
    	heap->p += size;
    	heap->nleft -= size;
    	return(p);
}

// Be careful that all functions actually get inlined, or else they
// will crash. This may not work on compilers other than SC.
// I don't know if it works on the Mac compiler.

__inline char *__mac_alloca_strdup(__Alloca_Heap *heap, const char *p)
{	size_t len;
	char *s;

	len = strlen(p) + 1;
	s = __mac_alloca(heap, len);
	return (char *) memcpy(s,p,len);
}

__inline char *__mac_alloca_strdup2(__Alloca_Heap *heap, const char *p1,const char *p2)
{	size_t len1;
	size_t len2;
	char *s;

	len1 = strlen(p1);
	len2 = strlen(p2) + 1;
	s = __mac_alloca(heap, len1 + len2);
	memcpy(s + len1,p2,len2);
	return (char *) memcpy(s,p1,len1);
}

__inline char *__mac_alloca_strdup3(__Alloca_Heap *heap, const char *p1,const char *p2,const char *p3)
{	size_t len1;
	size_t len2;
	size_t len3;
	char *s;

	len1 = strlen(p1);
	len2 = strlen(p2);
	len3 = strlen(p3) + 1;
	s = __mac_alloca(heap, len1 + len2 + len3);
	memcpy(s + len1,p2,len2);
	memcpy(s + len1 + len2,p3,len3);
	return (char *) memcpy(s,p1,len1);
}

__inline char *__mac_alloca_substring(__Alloca_Heap *heap, const char *p,int start,int end)
{	char *s;
	size_t len;

	len = end - start;
	s = (char *)__mac_alloca(heap, len + 1);
	s[len] = 0;
	return (char *) memcpy(s,p + start,len);
}
#define DEFINE_ALLOCA_HEAP  __Alloca_Heap __alloca_heap = { "", 0L, sizeof(__alloca_heap.buf) }
#define alloca_strdup(x) __mac_alloca_strdup(&__alloca_heap, (x) )
#define alloca_strdup2(x,y) __mac_alloca_strdup2(&__alloca_heap, (x), (y))
#define alloca_strdup3(x,y,z) __mac_alloca_strdup3(&__alloca_heap, (x), (y), (z) )
#define alloca(size) __mac_alloca( &__alloca_heap, (size) )
#define alloca_substring( p, n1, n2 ) __mac_alloca_substring( &__alloca_heap, (p), (n1), (n2))

#endif
