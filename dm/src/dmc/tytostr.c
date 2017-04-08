/**
 * Implementation of the
 * $(LINK2 http://www.digitalmars.com/download/freecompiler.html, Digital Mars C/C++ Compiler).
 *
 * Copyright:   Copyright (c) 1989-1998 by Symantec, All Rights Reserved
 *              Copyright (c) 2000-2017 by Digital Mars, All Rights Reserved
 * Authors:     $(LINK2 http://www.digitalmars.com, Walter Bright)
 * License:     Distributed under the Boost Software License, Version 1.0.
 *              http://www.boost.org/LICENSE_1_0.txt
 * Source:      https://github.com/DigitalMars/Compiler/blob/master/dm/src/dmc/tytostr.c
 */

// Convert a type to a readable string

#if !SPP

#include        <stdio.h>
#include        <stdlib.h>
#include        <string.h>
#include        <ctype.h>

#include        "cc.h"
#include        "parser.h"
#include        "global.h"
#include        "type.h"
#include        "outbuf.h"

static char __file__[] = __FILE__;      /* for tassert.h                */
#include        "tassert.h"

static char STR_CONST[] = "const ";
static char STR_VOLATILE[] = "volatile ";
static char STR_RESTRICT[] = "restrict ";

/********************************************
 * Take a type and turn it into a human readable string.
 * Note this returns a malloc'd buffer which should be free'd when
 * finished with if conversion takes place.
 */

void type_cv_tostring(Outbuffer *buf, tym_t ty)
{
    if (ty & mTYconst)
        buf->write(STR_CONST);
    if (ty & mTYvolatile)
        buf->write(STR_VOLATILE);
    if (ty & mTYrestrict)
        buf->write(STR_RESTRICT);
}

char *type_tostring(Outbuffer *buf,type *t)
{
    tym_t ty;
    tym_t tycv;
    const char *s;
    type *tn;
    Outbuffer buf2;
#if TX86
    mangle_t mangle;
#endif

    //printf("type_tostring()\n");
    //type_print(t);
    buf->reset();
    for (; t; t = t->Tnext)
    {
        type_debug(t);
#if TX86
        mangle = type_mangle(t);
#endif
        ty = t->Tty;
        if (!tyfunc(ty) && !tymptr(ty) &&
            !tyref(ty) &&
            tybasic(ty) != TYarray)
        {
            type_cv_tostring(buf, ty);
        }
        if (ty & mTYloadds)
                buf->write("__loadds ");
        if (ty & mTYfar)
                buf->write("__far ");
        if (ty & mTYnear)
                buf->write("__near ");
        if (ty & mTYcs)
                buf->write("__cs ");

        if (ty & mTYexport)
                buf->write("__export ");
        if (ty & mTYimport)
                buf->write("__import ");
#if TX86
        if (mangle == mTYman_pas)
                buf->write("__pascal ");
        if (mangle == mTYman_sys)
                buf->write("__syscall ");
#else
        if ((ty & mTYMAN) == mTYman_pas)
                buf->write("__pascal ");
#endif

        ty = tybasic(ty);
        assert(ty < TYMAX);

        switch (ty)
        {   case TYarray:
                if (buf->size())
                    buf->bracket('(',')');
                tycv = 0;
                for (tn = t; tybasic(tn->Tty) == TYarray; tn = tn->Tnext)
                {
                    buf->write("[");
                    if (tn->Tflags & TFstatic)
                        buf->write("static ");
                    //type_cv_tostring(buf, tn->Tty);
                    tycv |= tn->Tty & (mTYconst | mTYvolatile | mTYrestrict);
                    if (tn->Tflags & TFvla)
                        buf->write("*");
                    else if (tn->Tflags & TFsizeunknown)
                        ;
                    else
                    {   char buffer[sizeof(unsigned long) * 3 + 1];

                        sprintf(buffer,"%lu",(unsigned long)tn->Tdim);
                        buf->write(buffer);
                    }
                    buf->write("]");
                }
                if (tycv)
                {
                    tn->Tcount++;
                    tn = type_setty(&tn, tn->Tty | tycv);
                    buf->prependBytes(type_tostring(&buf2,tn));
                    type_free(tn);
                }
                else
                    buf->prependBytes(type_tostring(&buf2,tn));
                return buf->toString();

            case TYident:
                buf->write(t->Tident);
                break;

            case TYtemplate:
                buf->write((char *)((typetemp_t *)t)->Tsym->Sident);
                buf->write("<");
#if 1
                ptpl_tostring(buf, t->Tparamtypes);
#else
                if (((typetemp_t *)t)->Tsym->Stemplate->TMptal)
                    ptpl_tostring(buf, ((typetemp_t *)t)->Tsym->Stemplate->TMptal);
                else
                    ptpl_tostring(buf, ((typetemp_t *)t)->Tsym->Stemplate->TMptpl);
#endif
                buf->write(">");
                break;

            case TYstruct:
                if (CPP)
                {   Symbol *stmp = t->Ttag->Sstruct->Stempsym;
#if 0
                    if (stmp)
                    {
                        buf->write(stmp->Sident);
                        buf->write("<");
                        ptpl_tostring(buf, t->Ttag->Sstruct->Sarglist);
                        buf->write(">");
                        break;
                    }
#endif
                    s = "";
                }
                else
#if TX86
                    s = t->Ttag->Sstruct->Sflags & STRunion ? "union " : "struct ";
#else
                    s = t->Ttag->Sclass == SCunion ? "union " : "struct ";
#endif
                goto L1;

            case TYenum:
                s = "enum ";
            L1:
                buf->write(s);
                buf->write(t->Ttag ? prettyident(t->Ttag) : "{}");
                break;

            case TYmemptr:
                buf->writeByte(' ');
                buf->write(t->Ttag ? prettyident(t->Ttag) : "struct {}");
                buf->write("::*");
                goto Laddcv;

#if TX86
            case TYnptr:
            case TYsptr:
            case TYcptr:
            case TYhptr:
#endif
            case TYfptr:
            case TYvptr:
            case TYref:
                if (ty == pointertype)
                    s = tystring[TYptr];
                else
                    s = tystring[ty];
                buf->prependBytes(s);
            Laddcv:
                type_cv_tostring(buf, t->Tty);
                if (tyfunc(t->Tnext->Tty) || tybasic(t->Tnext->Tty == TYarray))
                    break;
                else
                {
                    buf->prependBytes(type_tostring(&buf2,t->Tnext));
                    goto Lret;
                }

            default:
                if (tyfunc(ty))
                {
                    size_t len;

                    len = buf->size();
                    buf->write(tystring[ty]);
                    if (len)
                        buf->bracket('(',')');
                    buf->write(param_tostring(&buf2,t));
                    buf->prependBytes(type_tostring(&buf2,t->Tnext));
                    goto Lret;
                }
                else
                {   const char *q;

                    q = tystring[ty];
                    buf2.reset();
                    buf2.write(q);
                    if (isalpha(q[strlen(q) - 1]))
                        buf2.writeByte(' ');
                    buf->prependBytes(buf2.toString());
                }
                break;
        }
    }
Lret:
    return buf->toString();
}

/*********************************
 * Convert function parameter list to a string.
 * Caller must free returned string.
 */

char *param_tostring(Outbuffer *buf,type *t)
{
    param_t *pm;
    static char ellipsis[] = "...";

    type_debug(t);
    buf->reset();
    if (!tyfunc(t->Tty))
        goto L1;
    buf->writeByte('(');
    pm = t->Tparamtypes;
    if (!pm)
    {
        // C++ and C interpret empty parameter lists differently
        if (CPP)
        {
            if (t->Tflags & TFfixed)
                ;
            else
                buf->write(ellipsis);
        }
        else
        {
            if (t->Tflags & TFfixed)
                buf->write("void");
        }
    }
    else
    {   Outbuffer pbuf;

        while (1)
        {   buf->write(type_tostring(&pbuf,pm->Ptype));
            pm = pm->Pnext;
            if (pm)
                buf->writeByte(',');
            else if (!(t->Tflags & TFfixed))
            {   buf->write(",...");
                break;
            }
            else
                break;
        }
    }
    buf->writeByte(')');
    if (CPP)
    {
        if (t->Tty & mTYconst)
            buf->write(STR_CONST);
        if (t->Tty & mTYvolatile)
            buf->write(STR_VOLATILE);
    }
L1:
    return buf->toString();
}

/******************************
 * Convert argument list into a string.
 */

char *arglist_tostring(Outbuffer *buf,list_t el)
{
    buf->reset();
    buf->writeByte('(');
    if (el)
    {   Outbuffer ebuf;

        while (1)
        {
            elem_debug(list_elem(el));
            buf->write(type_tostring(&ebuf,list_elem(el)->ET));
            el = list_next(el);
            if (!el)
                break;
            buf->writeByte(',');
        }
    }
    buf->writeByte(')');
    return buf->toString();
}

/*********************************
 * Convert function parameter list to a string.
 * Caller must free returned string.
 */

char *ptpl_tostring(Outbuffer *buf, param_t *ptpl)
{
    //printf("ptpl_tostring():\n");
    for (; ptpl; ptpl = ptpl->Pnext)
    {   Outbuffer pbuf;

        if (ptpl->Ptype)
            buf->write(type_tostring(&pbuf, ptpl->Ptype));
        else if (ptpl->Pelem)
            buf->write(el_tostring(&pbuf, ptpl->Pelem));
        else if (ptpl->Pident)
            buf->write(ptpl->Pident);
        if (ptpl->Pnext)
            buf->writeByte(',');
    }
    //printf("-ptpl_tostring()\n");
    return buf->toString();
}

/**********************************
 * Convert elem to string.
 */

char *el_tostring(Outbuffer *buf, elem *e)
{
    //printf("el_tostring(): "); elem_print(e);
    switch (e->Eoper)
    {
        case OPvar:
            buf->write(e->EV.sp.Vsym->Sident);
            break;

        case OPconst:
            char buffer[1 + sizeof(targ_llong) * 3 + 1];
            sprintf(buffer, "%d", el_tolongt(e));
            buf->write(buffer);
            break;

        // BUG: should handle same cases as in newman.c
    }
    return buf->toString();
}

#endif
