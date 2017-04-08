/**
 * Implementation of the
 * $(LINK2 http://www.digitalmars.com/download/freecompiler.html, Digital Mars C/C++ Compiler).
 *
 * Copyright:   Copyright (c) 1989-1998 by Symantec, All Rights Reserved
 *              Copyright (c) 2000-2017 by Digital Mars, All Rights Reserved
 * Authors:     $(LINK2 http://www.digitalmars.com, Walter Bright)
 * License:     Distributed under the Boost Software License, Version 1.0.
 *              http://www.boost.org/LICENSE_1_0.txt
 * Source:      https://github.com/DigitalMars/Compiler/blob/master/dm/src/dmc/trace.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unmangle.h>

#define _DEBUG_TRACE 1
#include <trace.h>

#if _WIN32
#define _fmemcpy        memcpy
#define _fmemcmp        memcmp
#endif

#define TIMER           defined(_WIN32)

#if _WIN32

extern "C" {

#if TIMER
#include <windows.h>
#endif

typedef long long timer_t;

/////////////////////////////////////
//

typedef struct SYMPAIR
{
    struct SYMPAIR *next;
    struct SYMBOL *sym;         // function that is called
    unsigned count;             // number of times sym is called
} Sympair;

/////////////////////////////////////
// A Symbol for each function name.

typedef struct SYMBOL
{
        struct SYMBOL *Sl,*Sr;          // left, right children
        struct SYMPAIR *Sfanin;         // list of calling functions
        struct SYMPAIR *Sfanout;        // list of called functions
#if TIMER
        timer_t totaltime;              // aggregate time
        timer_t functime;               // time excluding subfunction calls
#endif
        unsigned char Sflags;
#       define SFvisited        1       // visited
        char Sident[1];                 // name of symbol
} Symbol;

static Symbol *root;                    // root of symbol table

//////////////////////////////////
// Build a linked list of these.

typedef struct STACK
{
    struct STACK *prev;
    Symbol *sym;
#if TIMER
    timer_t starttime;                  // time when function was entered
    timer_t ohd;                        // overhead of all the bookkeeping code
    timer_t subtime;                    // time used by all subfunctions
#endif
} Stack;

static Stack *stack_freelist;
static Stack *trace_tos;                // top of stack
static int trace_inited;                // !=0 if initialized
static timer_t trace_ohd;

static Symbol **psymbols;
static unsigned nsymbols;               // number of symbols

static char trace_logfilename[FILENAME_MAX + 1] = "trace.log";
static FILE *fplog;

static char trace_deffilename[FILENAME_MAX + 1] = "trace.def";
static FILE *fpdef;

static void trace_init();
void __cdecl trace_term(void);
static void *trace_malloc(size_t nbytes);
static void trace_free(void *p);
static void trace_place(Symbol *s,unsigned count);
static void trace_merge();

#define FARFP   __near

__declspec(naked) void FARFP _trace_pro_n();
__declspec(naked) void FARFP _trace_epi_n();

////////////////////////////////////////
// Set file name for output.
// A file name of "" means write results to stdout.
// Returns:
//      0       success
//      !=0     failure

int __cdecl trace_setlogfilename(const char *name)
{
    if (strlen(name) < FILENAME_MAX)
    {   strcpy(trace_logfilename,name);
        return 0;
    }
    return 1;
}

////////////////////////////////////////
// Set file name for output.
// A file name of "" means write results to stdout.
// Returns:
//      0       success
//      !=0     failure

int __cdecl trace_setdeffilename(const char *name)
{
    if (strlen(name) < FILENAME_MAX)
    {   strcpy(trace_deffilename,name);
        return 0;
    }
    return 1;
}

////////////////////////////////////////
// Output optimal function link order.

static void trace_order(Symbol *s)
{
    while (s)
    {
        trace_place(s,0);
        if (s->Sl)
            trace_order(s->Sl);
        s = s->Sr;
    }
}

//////////////////////////////////////////////
//

static Stack * __near stack_malloc()
{   Stack *s;

    if (stack_freelist)
    {   s = stack_freelist;
        stack_freelist = s->prev;
    }
    else
        s = (Stack *)trace_malloc(sizeof(Stack));
    return s;
}

//////////////////////////////////////////////
//

static void __near stack_free(Stack *s)
{
    s->prev = stack_freelist;
    stack_freelist = s;
}

//////////////////////////////////////
// Qsort() comparison routine for array of pointers to Sympair's.

static int __cdecl sympair_cmp(const void *e1,const void *e2)
{   Sympair **psp1;
    Sympair **psp2;

    psp1 = (Sympair **)e1;
    psp2 = (Sympair **)e2;

    return (*psp2)->count - (*psp1)->count;
}

//////////////////////////////////////
// Place symbol s, and then place any fan ins or fan outs with
// counts greater than count.

static void trace_place(Symbol *s,unsigned count)
{   Sympair *sp;
    Sympair **base;

    if (!(s->Sflags & SFvisited))
    {   size_t num;
        unsigned u;

        //printf("\t%s\t%u\n",s->Sident,count);
        fprintf(fpdef,"\t%s\n",s->Sident);
        s->Sflags |= SFvisited;

#if 1
        // Compute number of items in array
        num = 0;
        for (sp = s->Sfanin; sp; sp = sp->next)
            num++;
        for (sp = s->Sfanout; sp; sp = sp->next)
            num++;
        if (!num)
            return;

        // Allocate and fill array
        base = (Sympair **)trace_malloc(sizeof(Sympair) * num);
        u = 0;
        for (sp = s->Sfanin; sp; sp = sp->next)
            base[u++] = sp;
        for (sp = s->Sfanout; sp; sp = sp->next)
            base[u++] = sp;

        // Sort array
        qsort(base,num,sizeof(Sympair *),sympair_cmp);
#if 0
        for (u = 0; u < num; u++)
            printf("\t\t%s\t%u\n",base[u]->sym->Sident,base[u]->count);
#endif
        // Place symbols
        for (u = 0; u < num; u++)
        {
            if (base[u]->count >= count)
            {   unsigned u2;
                unsigned c2;

                u2 = (u + 1 < num) ? u + 1 : u;
                c2 = base[u2]->count;
                if (c2 < count)
                    c2 = count;
                trace_place(base[u]->sym,c2);
            }
            else
                break;
        }

        // Clean up
        trace_free(base);
#else
        for (sp = s->Sfanin; sp; sp = sp->next)
        {
            if (sp->count >= count)
                trace_place(sp->sym,sp->count);
        }
        for (sp = s->Sfanout; sp; sp = sp->next)
        {
            if (sp->count >= count)
                trace_place(sp->sym,sp->count);
        }
#endif
    }
}

/////////////////////////////////////
// Initialize and terminate.

void _STI_trace()
{
    trace_init();
}

void _STD_trace()
{
    trace_term();
}

///////////////////////////////////
// Report results.
// Also compute nsymbols.

static void trace_report(Symbol *s)
{   Sympair *sp;
    unsigned count;

    while (s)
    {   nsymbols++;
        if (s->Sl)
            trace_report(s->Sl);
        fprintf(fplog,"------------------\n");
        count = 0;
        for (sp = s->Sfanin; sp; sp = sp->next)
        {
            fprintf(fplog,"\t%5d\t%s\n",sp->count,sp->sym->Sident);
            count += sp->count;
        }
        fprintf(fplog,"%s\t%u\t%lld\t%lld\n",s->Sident,count,s->totaltime,s->functime);
        for (sp = s->Sfanout; sp; sp = sp->next)
        {
            fprintf(fplog,"\t%5d\t%s\n",sp->count,sp->sym->Sident);
        }
        s = s->Sr;
    }
}

////////////////////////////////////
// Allocate and fill array of symbols.

static void trace_array(Symbol *s)
{   static unsigned u;

    if (!psymbols)
    {   u = 0;
        psymbols = (Symbol **)trace_malloc(sizeof(Symbol *) * nsymbols);
    }
    while (s)
    {
        psymbols[u++] = s;
        trace_array(s->Sl);
        s = s->Sr;
    }
}

#if TIMER

//////////////////////////////////////
// Qsort() comparison routine for array of pointers to Symbol's.

static int __cdecl symbol_cmp(const void *e1,const void *e2)
{   Symbol **ps1;
    Symbol **ps2;
    timer_t diff;

    ps1 = (Symbol **)e1;
    ps2 = (Symbol **)e2;

    diff = (*ps2)->functime - (*ps1)->functime;
    return (diff == 0) ? 0 : ((diff > 0) ? 1 : -1);
}


///////////////////////////////////
// Report function timings

static void trace_times(Symbol *root)
{   unsigned u;
    timer_t freq;

    // Sort array
    qsort(psymbols,nsymbols,sizeof(Symbol *),symbol_cmp);

    // Print array
    QueryPerformanceFrequency((LARGE_INTEGER *)&freq);
    fprintf(fplog,"\n======== Timer Is %lld Ticks/Sec, Times are in Microsecs ========\n\n",freq);
    fprintf(fplog,"  Num          Tree        Func        Per\n");
    fprintf(fplog,"  Calls        Time        Time        Call\n\n");
    for (u = 0; u < nsymbols; u++)
    {   Symbol *s = psymbols[u];
        timer_t tl,tr;
        timer_t fl,fr;
        timer_t pl,pr;
        timer_t percall;
        Sympair *sp;
        unsigned calls;
        char *id;

        id = unmangle_ident(s->Sident);
        if (!id)
            id = s->Sident;
        calls = 0;
        for (sp = s->Sfanin; sp; sp = sp->next)
            calls += sp->count;
        if (calls == 0)
            calls = 1;

#if 1
        tl = (s->totaltime * 1000000) / freq;
        fl = (s->functime  * 1000000) / freq;
        percall = s->functime / calls;
        pl = (s->functime * 1000000) / calls / freq;

        fprintf(fplog,"%7d%12lld%12lld%12lld     %s\n",
            calls,tl,fl,pl,id);
#else
        tl = s->totaltime / freq;
        tr = ((s->totaltime - tl * freq) * 10000000) / freq;

        fl = s->functime  / freq;
        fr = ((s->functime  - fl * freq) * 10000000) / freq;

        percall = s->functime / calls;
        pl = percall  / freq;
        pr = ((percall  - pl * freq) * 10000000) / freq;

        fprintf(fplog,"%7d\t%3lld.%07lld\t%3lld.%07lld\t%3lld.%07lld\t%s\n",
            calls,tl,tr,fl,fr,pl,pr,id);
#endif
        if (id != s->Sident)
            free(id);
    }
}

#endif

///////////////////////////////////
// Initialize.

static void trace_init()
{
    if (!trace_inited)
    {
        trace_inited = 1;

#if TIMER
        {   // See if we can determine the overhead.
            unsigned u;
            timer_t starttime;
            timer_t endtime;
            Stack *st;

            st = trace_tos;
            trace_tos = NULL;
            QueryPerformanceCounter((LARGE_INTEGER *)&starttime);
            for (u = 0; u < 100; u++)
            {
                __asm
                {
                    call _trace_pro_n
                    db   0
                    call _trace_epi_n
                }
            }
            QueryPerformanceCounter((LARGE_INTEGER *)&endtime);
            trace_ohd = (endtime - starttime) / u;
            //printf("trace_ohd = %lld\n",trace_ohd);
            if (trace_ohd > 0)
                trace_ohd--;            // round down
            trace_tos = st;
        }
#endif
    }
}

/////////////////////////////////
// Terminate.

void __cdecl trace_term()
{
    //printf("trace_term()\n");
    if (trace_inited == 1)
    {   Stack *n;

        trace_inited = 2;

        // Free remainder of the stack
        while (trace_tos)
        {
            n = trace_tos->prev;
            stack_free(trace_tos);
            trace_tos = n;
        }

        while (stack_freelist)
        {
            n = stack_freelist->prev;
            stack_free(stack_freelist);
            stack_freelist = n;
        }

        // Merge in data from any existing file
        trace_merge();

        // Report results
        fplog = fopen(trace_logfilename,"w");
        if (fplog)
        {   nsymbols = 0;
            trace_report(root);
            trace_array(root);
#if TIMER
            trace_times(root);
#endif
            fclose(fplog);
        }

        // Output function link order
        fpdef = fopen(trace_deffilename,"w");
        if (fpdef)
        {   fprintf(fpdef,"\nFUNCTIONS\n");
            trace_order(root);
            fclose(fpdef);
        }

        trace_free(psymbols);
        psymbols = NULL;
    }
}

/////////////////////////////////
// Our storage allocator.

static void *trace_malloc(size_t nbytes)
{   void *p;

    p = malloc(nbytes);
    if (!p)
        exit(EXIT_FAILURE);
    return p;
}

static void trace_free(void *p)
{
    free(p);
}

//////////////////////////////////////////////
//

static Symbol *trace_addsym(const char __far *p,size_t len)
{
    Symbol **parent;
    Symbol *rover;
    Symbol *s;
    signed char cmp;
    char c;

    //printf("trace_addsym('%s',%d)\n",p,len);
    parent = &root;
    rover = *parent;
    while (rover != NULL)               // while we haven't run out of tree
    {
        cmp = _fmemcmp(p,rover->Sident,len);
        if (cmp == 0)
        {   if ((c = rover->Sident[len]) == 0)
                return rover;
            cmp = 0 - c;
        }
        parent = (cmp < 0) ?            /* if we go down left side      */
            &(rover->Sl) :              /* then get left child          */
            &(rover->Sr);               /* else get right child         */
        rover = *parent;                /* get child                    */
    }
    /* not in table, so insert into table       */
    s = (Symbol *)trace_malloc(sizeof(Symbol) + len);
    memset(s,0,sizeof(Symbol));
    _fmemcpy(s->Sident,p,len);
    s->Sident[len] = 0;
    *parent = s;                        // link new symbol into tree
    return s;
}

/***********************************
 * Add symbol s with count to Sympair list.
 */

static void trace_sympair_add(Sympair **psp,Symbol *s,unsigned count)
{   Sympair *sp;

    for (; 1; psp = &sp->next)
    {
        sp = *psp;
        if (!sp)
        {
            sp = (Sympair *)trace_malloc(sizeof(Sympair));
            sp->sym = s;
            sp->count = 0;
            sp->next = NULL;
            *psp = sp;
            break;
        }
        else if (sp->sym == s)
        {
            break;
        }
    }
    sp->count += count;
}

//////////////////////////////////////////////
//

static void trace_pro(const char __far *p,size_t len)
{
#if 0
    char *name;

    name = (char *)trace_malloc(len + 1);
    _fmemcpy(name,p,len);
    name[len] = 0;
    printf("trace_pro('%s',%d)\n",name,len);
    free(name);
#else
    Stack *n;
    Symbol *s;
#if TIMER
    timer_t starttime;
    timer_t t;

    QueryPerformanceCounter((LARGE_INTEGER *)&starttime);
#endif
    if (*p == 0 || len == 0)
        return;
    if (!trace_inited)
        trace_init();                   // initialize package
    n = stack_malloc();
    n->prev = trace_tos;
    trace_tos = n;
    s = trace_addsym(p,len);
    trace_tos->sym = s;
    if (trace_tos->prev)
    {
        Symbol *prev;
        int i;

        // Accumulate Sfanout and Sfanin
        prev = trace_tos->prev->sym;
        trace_sympair_add(&prev->Sfanout,s,1);
        trace_sympair_add(&s->Sfanin,prev,1);
    }
#if TIMER
    QueryPerformanceCounter((LARGE_INTEGER *)&t);
    trace_tos->starttime = starttime;
    trace_tos->ohd = trace_ohd + t - starttime;
    trace_tos->subtime = 0;
    //printf("trace_tos->ohd=%lld, trace_ohd=%lld + t=%lld - starttime=%lld\n",
    //  trace_tos->ohd,trace_ohd,t,starttime);
#endif
#endif
}

/////////////////////////////////////////
//

static void trace_epi()
{   Stack *n;
    timer_t endtime;
    timer_t t;
    timer_t ohd;

    //printf("trace_epi()\n");
    if (trace_tos)
    {
#if TIMER
        timer_t starttime;
        timer_t totaltime;

        QueryPerformanceCounter((LARGE_INTEGER *)&endtime);
        starttime = trace_tos->starttime;
        totaltime = endtime - starttime - trace_tos->ohd;
        if (totaltime < 0)
        {   //printf("endtime=%lld - starttime=%lld - trace_tos->ohd=%lld < 0\n",
            //  endtime,starttime,trace_tos->ohd);
            totaltime = 0;              // round off error, just make it 0
        }

        // totaltime is time spent in this function + all time spent in
        // subfunctions - bookkeeping overhead.
        trace_tos->sym->totaltime += totaltime;

        //if (totaltime < trace_tos->subtime)
        //printf("totaltime=%lld < trace_tos->subtime=%lld\n",totaltime,trace_tos->subtime);
        trace_tos->sym->functime  += totaltime - trace_tos->subtime;
        ohd = trace_tos->ohd;
        n = trace_tos->prev;
        stack_free(trace_tos);
        trace_tos = n;
        if (n)
        {   QueryPerformanceCounter((LARGE_INTEGER *)&t);
            n->ohd += ohd + t - endtime;
            n->subtime += totaltime;
            //printf("n->ohd = %lld\n",n->ohd);
        }

#else
        n = trace_tos->prev;
        trace_free(trace_tos);
        trace_tos = n;
#endif
    }
#if 0
    if (!trace_tos)
        trace_term();
#endif
}

////////////////////////// FILE INTERFACE /////////////////////////

/////////////////////////////////////
// Read line from file fp.
// Returns:
//      trace_malloc'd line buffer
//      NULL if end of file

static char *trace_readline(FILE *fp)
{   int c;
    int dim;
    int i;
    char *buf;

    i = 0;
    dim = 0;
    buf = NULL;
    while (1)
    {
        if (i == dim)
        {   char *p;

            dim += 80;
            p = (char *)trace_malloc(dim);
            memcpy(p,buf,i);
            trace_free(buf);
            buf = p;
        }
        c = fgetc(fp);
        switch (c)
        {
            case EOF:
                if (i == 0)
                {   trace_free(buf);
                    return NULL;
                }
            case '\n':
                goto L1;
        }
        buf[i] = c;
        i++;
    }
L1:
    buf[i] = 0;
    //printf("line '%s'\n",buf);
    return buf;
}

//////////////////////////////////////
// Skip space

static char *skipspace(char *p)
{
    while (isspace(*p))
        p++;
    return p;
}

////////////////////////////////////////////////////////
// Merge in profiling data from existing file.

static void trace_merge()
{   FILE *fp;
    char *buf;
    char *p;
    unsigned count;
    Symbol *s;
    Sympair *sfanin;
    Sympair **psp;

    if (*trace_logfilename && (fp = fopen(trace_logfilename,"r")) != NULL)
    {
        buf = NULL;
        sfanin = NULL;
        psp = &sfanin;
        while (1)
        {
            trace_free(buf);
            buf = trace_readline(fp);
            if (!buf)
                break;
            switch (*buf)
            {
                case '=':               // ignore rest of file
                    trace_free(buf);
                    goto L1;
                case ' ':
                case '\t':              // fan in or fan out line
                    count = strtoul(buf,&p,10);
                    if (p == buf)       // if invalid conversion
                        continue;
                    p = skipspace(p);
                    if (!*p)
                        continue;
                    s = trace_addsym(p,strlen(p));
                    trace_sympair_add(psp,s,count);
                    break;
                default:
                    if (!isalpha(*buf))
                    {
                        if (!sfanin)
                            psp = &sfanin;
                        continue;       // regard unrecognized line as separator
                    }
                case '?':
                case '_':
                case '$':
                case '@':
                    p = buf;
                    while (isgraph(*p))
                        p++;
                    *p = 0;
                    //printf("trace_addsym('%s')\n",buf);
                    s = trace_addsym(buf,strlen(buf));
                    if (s->Sfanin)
                    {   Sympair *sp;

                        for (; sfanin; sfanin = sp)
                        {
                            trace_sympair_add(&s->Sfanin,sfanin->sym,sfanin->count);
                            sp = sfanin->next;
                            trace_free(sfanin);
                        }
                    }
                    else
                    {   s->Sfanin = sfanin;
                    }
                    sfanin = NULL;
                    psp = &s->Sfanout;

#if TIMER
                    {   timer_t t;

                        p++;
                        count = strtoul(p,&p,10);
                        t = strtoull(p,&p,10);
                        s->totaltime += t;
                        t = strtoull(p,&p,10);
                        s->functime += t;
                    }
#endif
                    break;
            }
        }
    L1:
        fclose(fp);
    }
}

////////////////////////// COMPILER INTERFACE /////////////////////

/////////////////////////////////////////////
// Function called by trace code in function prolog.

__declspec(naked) void FARFP _trace_pro_n()
{
    /* Length of string is either:
     *  db      length
     *  ascii   string
     * or:
     *  db      0x0FF
     *  db      0
     *  dw      length
     *  ascii   string
     */

#if _WIN32
    __asm
    {
        pushad
        mov     ECX,8*4[ESP]
        xor     EAX,EAX
        mov     AL,[ECX]
        cmp     AL,0FFh
        jne     L1
        cmp     byte ptr 1[ECX],0
        jne     L1
        mov     AX,2[ECX]
        add     8*4[ESP],3
        add     ECX,3
    L1: inc     EAX
        inc     ECX
        add     8*4[ESP],EAX
    }
    trace_pro((char __far *)_ECX,_EAX - 1);
    __asm
    {
        popad
        ret
    }
#elif 1    // should be DOS386
    char __far *p;
    __asm
    {
        pushad
        push    ES
        mov     EBP,ESP
        sub     ESP,__LOCAL_SIZE
        mov     ECX,9*4[EBP]
        mov     ES,10*4[EBP]
        xor     EAX,EAX
        mov     AL,ES:[ECX]
        cmp     AL,0FFh
        jne     L1
        cmp     byte ptr ES:1[ECX],0
        jne     L1
        mov     AX,ES:2[ECX]
        add     9*4[EBP],3
        add     ECX,3
    L1: inc     EAX
        inc     ECX
        add     9*4[EBP],EAX
        mov     dword ptr p[EBP],ECX
        mov     CX,ES
        mov     dword ptr p+4[EBP],ECX
    }
    trace_pro(p,_EAX - 1);
    __asm
    {
        mov     ESP,EBP
        pop     ES
        popad
        ret
    }
#else
    char __far *p;
    __asm
    {
        pusha
        push    ES
        mov     BP,SP
        sub     SP,__LOCAL_SIZE
        mov     BX,9*2[BP]
        mov     ES,10*2[BP]
        xor     AX,AX
        mov     AL,ES:[BX]
        cmp     AL,0FFh
        jne     L1
        cmp     byte ptr ES:1[BX],0
        jne     L1
        mov     AX,ES:2[BX]
        add     9*2[BP],3
        add     BX,3
    L1: inc     AX
        inc     BX
        add     9*2[BP],AX
        mov     word ptr p[BP],BX
        mov     BX,ES
        mov     word ptr p+2[BP],BX
    }
    trace_pro(p,_AX - 1);
    __asm
    {
        mov     SP,BP
        pop     ES
        popa
        retf
    }
#endif
}

/////////////////////////////////////////////
// Function called by trace code in function epilog.

__declspec(naked) void FARFP _trace_epi_n()
{
    __asm
    {
        pushad
    }
    trace_epi();
    __asm
    {
        popad
        ret
    }
}

}

#endif
