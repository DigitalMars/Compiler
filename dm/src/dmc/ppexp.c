/**
 * Implementation of the
 * $(LINK2 http://www.digitalmars.com/download/freecompiler.html, Digital Mars C/C++ Compiler).
 *
 * Copyright:   Copyright (c) 1991-1998 by Symantec, All Rights Reserved
 *              Copyright (c) 2000-2017 by Digital Mars, All Rights Reserved
 * Authors:     $(LINK2 http://www.digitalmars.com, Walter Bright)
 * License:     Distributed under the Boost Software License, Version 1.0.
 *              http://www.boost.org/LICENSE_1_0.txt
 * Source:      https://github.com/DigitalMars/Compiler/blob/master/dm/src/dmc/ppexp.c
 */

// Expression parser for SPP

#include        <stdio.h>
#include        <string.h>
#include        "cc.h"
#include        "token.h"
#include        "global.h"

static char __file__[] = __FILE__;      /* for tassert.h                */
#include        "tassert.h"

#if SPP

STATIC targ_long  cond_exp(void);
STATIC targ_long  log_or_exp(void);
STATIC targ_long  log_and_exp(void);
STATIC targ_long  inc_or_exp(void);
STATIC targ_long  xor_exp(void);
STATIC targ_long  and_exp(void);
STATIC targ_long  equal_exp(void);
STATIC targ_long  rel_exp(void);
STATIC targ_long  shift_exp(void);
STATIC targ_long  add_exp(void);
STATIC targ_long  mul_exp(void);
STATIC targ_long  una_exp(void);
STATIC targ_long  primary_exp(void);
STATIC targ_long  exp_sizeof(void);

int pragma_option(void);

/********************************
 * Parse expression.
 * Groups left to right.
 *      expression ::= assign_exp { "," assign_exp }
 *      e1,e2,e3 =>
 *           ,
 *          / \
 *         ,  e3
 *        / \
 *      e1   e2
 * Returns:
 *      Root element of expression tree.
 */

targ_llong msc_getnum()
{ targ_long e;

  e = cond_exp();
  while (tok.TKval == TKcomma)
  {     stoken();
        e = cond_exp();
  }
  return e;
}

/******************************
 * Groups right to left.
 * cond_exp ::= log_or_exp [ "?" cond_exp ":" cond_exp ]
 *      e1 ? e2 ? e3 : e4 : e5 =>
 *
 *         ?
 *        / \
 *      e1   :
 *          / \
 *         ?   e5
 *        / \
 *      e2   :
 *          / \
 *        e3   e4
 *
 * Watch out for the case ((e) ? NULL : (char *) e).
 */

STATIC targ_long cond_exp()
{ targ_long e1,e2,e3;

  e1 = log_or_exp();
  if (tok.TKval == TKques)
  {
        stoken();
        e2 = msc_getnum();
        chktok(TKcolon,EM_colon);
        e3 = cond_exp();
        return e1 ? e2 : e3;
  }
  else
        return e1;
}


/******************************
 * Groups left to right.
 * log_or_exp ::= log_and_exp { "||" log_and_exp }
 *      e1 || e2 || e3 =>
 *           ||
 *          /  \
 *         ||   e3
 *        /  \
 *      e1   e2
 */

STATIC targ_long log_or_exp()
{ targ_long e;

  e = log_and_exp();

  while (tok.TKval == TKoror) /* if || */
  {
        stoken();
        e = log_and_exp() || e; /* do not short-circuit call to log_and_exp() */
  }
  return e;
}

/*******************************
 * Groups left to right.
 * log_and_exp ::= inc_or_exp { "&&" inc_or_exp }
 *      e1 && e2 && e3 =>
 *            &&
 *           /  \
 *         &&    e3
 *        / \
 *      e1   e2
 */

STATIC targ_long log_and_exp()
{ targ_long e;

  e = inc_or_exp();

  while (tok.TKval == TKandand) /* if && */
  {
        stoken();
        e = inc_or_exp() && e;
  }
  return e;
}

/******************************
 * Groups left to right.
 * inc_or_exp ::= xor_exp { "|" xor_exp }
 *      e1 | e2 | e3 =>
 *           |
 *          / \
 *         |  e3
 *        / \
 *      e1   e2
 */

STATIC targ_long inc_or_exp()
{ targ_long e;

  e = xor_exp();
  while (tok.TKval == TKor)                     /* if "|"                       */
  {
        stoken();
        e |= xor_exp();
  }
  return e;
}


/******************************
 * Groups left to right.
 * xor_exp ::= and_exp { "^" and_exp }
 *      e1 ^ e2 ^ e3 =>
 *
 *           ^
 *          / \
 *         ^  e3
 *        / \
 *      e1   e2
 */

STATIC targ_long xor_exp()
{ targ_long e;

  e = and_exp();
  while (tok.TKval == TKxor)                    /* if "^"                       */
  {
        stoken();
        e ^= and_exp();
  }
  return e;
}


/******************************
 * Groups left to right.
 * and_exp ::= equal_exp { "&" equal_exp }
 *      e1 & e2 & e3 =>
 *           &
 *          / \
 *         &  e3
 *        / \
 *      e1   e2
 */

STATIC targ_long and_exp()
{ targ_long e;

  e = equal_exp();
  while (tok.TKval == TKand)            /* if "&"                       */
  {
        stoken();
        e &= equal_exp();
  }
  return e;
}


/******************************
 * Groups left to right.
 * equal_exp ::= rel_exp { "==" rel_exp | "!=" rel_exp }
 *      e1 == e2 == e3 =>
 *            ==
 *           /  \
 *         ==    e3
 *        /  \
 *      e1    e2
 */

STATIC targ_long equal_exp()
{ targ_long e;

  e = rel_exp();
  while (1)
  {     switch ((int) tok.TKval)
        {    case TKeqeq:
                stoken();
                e = e == rel_exp();
                continue;
             case TKne:
                stoken();
                e = e != rel_exp();
                continue;
             default:
                break;
        }
        break;
  }
  return e;
}

/******************************
 * Groups left to right.
 * rel_exp ::= shift_exp { relop shift_exp }
 *      e1 < e2 < e3 =>
 *
 *           <
 *          / \
 *         <  e3
 *        / \
 *      e1   e2
 */

STATIC targ_long rel_exp()
{   targ_long e;

    e = shift_exp();
    while (1)
    {   switch (tok.TKval)
        {
            case TKgt:
                stoken();
                e = e > shift_exp();
                break;
            case TKle:
                stoken();
                e = e <= shift_exp();
                break;
            case TKge:
                stoken();
                e = e >= shift_exp();
                break;
            case TKlt:
                stoken();
                e = e < shift_exp();
                break;
            default:
                return e;
        }
    }
    assert(0);
}


/******************************
 * Groups left to right.
 * shift_exp ::= add_exp { "<<" add_exp | ">>" add_exp }
 */

STATIC targ_long shift_exp()
{ targ_long e;

  e = add_exp();
  while (tok.TKval == TKshl || tok.TKval == TKshr)
  {
        if (tok.TKval == TKshl)
        {
            stoken();
            e <<= add_exp();
        }
        else
        {
            stoken();
            e >>= add_exp();    /* BUG: unsigned longs? */
        }
  }
  return e;
}


/******************************
 * Groups left to right.
 * add_exp ::= mul_exp { "+" mul_exp | "-" mul_exp }
 */

STATIC targ_long add_exp()
{ targ_long e;

  e = mul_exp();
  while (tok.TKval == TKadd || tok.TKval == TKmin)
  {
        enum_TK x;

        x = tok.TKval;
        stoken();
        if (x == TKadd)
            e += mul_exp();
        else
            e -= mul_exp();
  }
  return e;
}


/******************************
 * Groups left to right.
 * mul_exp ::= una_exp { "*" una_exp | "/" una_exp | "%" una_exp }
 */

STATIC targ_long mul_exp()
{   targ_long e;
    targ_long divisor;

    e = una_exp();
    while (1)
    {   int op;

        switch (tok.TKval)
        {
            case TKstar:
                stoken();
                e *= una_exp();
                break;
            case TKdiv:
                stoken();
                divisor = una_exp();
                if (divisor)
                    e /= divisor;
                else
                    preerr(EM_divby0);          // divide by 0
                break;
            case TKmod:
                stoken();
                divisor = una_exp();
                if (divisor)
                    e %= divisor;
                else
                    preerr(EM_divby0);          // divide by 0
                break;
            default:
                return e;
        }
    }
    return e;
}

/******************************
 * Groups right to left.
 * una_exp ::=  * una_exp
 *              - una_exp
 *              + una_exp
 *              ! una_exp
 *              ~ una_exp
 *              & primary_exp
 *              (type_name) una_exp
 *              simple_type_name (expression_list)
 *              sizeof una_exp
 *              sizeof (type_name)      ?? primary ??
 */

STATIC targ_long una_exp()
{
    targ_long e;

    switch (tok.TKval)
    {
        case TKadd:
            stoken();
            return una_exp();
        case TKmin:
            stoken();
            return -una_exp();
        case TKnot:
            stoken();
            return !una_exp();
        case TKcom:
            stoken();
            return ~una_exp();
        case TKlpar:
            stoken();
            e = msc_getnum();
            if (tok.TKval != TKrpar)
                synerr(EM_rpar);
            stoken();
            break;
#if !TX86
        case TKsizeof:
            if (ANSI && preprocessor)
                synerr(EM_prep_exp);    // sizeof illegal in preprocessor exp
            e = exp_sizeof();
            return 1;           // BUG: this looks like a bug to me! -Walter
#endif
        default:
            e = primary_exp();
            break;
    }
    return e;
}

/******************************
 * Primary expression.
 * Groups left to right.
 *      primary_exp ::= identifier
 *                      constant
 *                      ( expression )
 */

STATIC targ_long primary_exp()
{ targ_long e;
  symbol *s;

  _chkstack();
  switch (tok.TKval)
  {
        case TKident:
            /* Look for case of #if defined(identifier) */
            if (strcmp(tok.TKid,"defined") == 0)
            {   e = pragma_defined();
                break;
            }
#if !TX86
            if (strcmp( tok.TKid, "__option") == 0)
            {
                e = pragma_option();
                break;
            }
#endif
            /* If identifier appears here, that is because it
               is an undefined macro.
               Treat as number 0.
             */
            stoken();
            e = 0;
            break;
        case TKnum:
            /* BUG: what about unsigned longs? */
            e = tok.TKutok.Vlong;
            stoken();
            break;
        default:
            synerr(EM_exp);             // primary_exp expected
            stoken();
            e = 0;
            break;
  } /* switch */
  return e;
}

/****************************
 * Parse and return elem tree for sizeof expressions.
 * Handle __typeinfo expressions also.
 */

#if !TX86

STATIC targ_long exp_sizeof()
{       unsigned short had_paren = FALSE;
        int siz=-1;

        stoken();
        if (tok.TKval == TKlpar)
        {
            had_paren = TRUE;
            stoken();
        }
        switch (tok.TKval)
        {
            case TKvoid:
                break;
// BUG: need to add in TYbool, wchar_t, etc. -Walter
            case TYchar:
            case TYschar:
            case TYuchar:
            case TYshort:
            case TYushort:
            case TYenum:
            case TYint:
            case TYuint:
            case TYlong:
            case TYulong:
            case TYllong:
            case TYullong:
            case TYfloat:
            case TYdouble:
            case TYldouble:
                siz = _tysize[tok.TKval];
                break;
            default:
                synerr(EM_function);
        }
        stoken();
        if (tok.TKval == TKstar)
            {
            siz = tysize(TYfptr);
            stoken();
            }
        if (had_paren && tok.TKval == TKrpar)
            stoken();
        if (siz == -1)
            synerr(EM_function);
        return siz;
}

#endif

#endif /* SPP */
