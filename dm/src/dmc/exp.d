/**
 * Implementation of the
 * $(LINK2 http://www.digitalmars.com/download/freecompiler.html, Digital Mars C/C++ Compiler).
 *
 * Copyright:   Copyright (c) 1983-1998 by Symantec, All Rights Reserved
 *              Copyright (c) 2000-2017 by Digital Mars, All Rights Reserved
 * Authors:     $(LINK2 http://www.digitalmars.com, Walter Bright)
 * License:     Distributed under the Boost Software License, Version 1.0.
 *              http://www.boost.org/LICENSE_1_0.txt
 * Source:      https://github.com/DigitalMars/Compiler/blob/master/dm/src/dmc/exp.d
 */

// Expression parser for C and C++

version (SPP)
{
}
else
{

import core.stdc.stdio;
import core.stdc.stdlib;
import core.stdc.string;

import dmd.backend.cc;
import dmd.backend.cdef;
import dmd.backend.code;
import dmd.backend.dt;
import dmd.backend.el;
import dmd.backend.exh;
import dmd.backend.global;
import dmd.backend.obj;
import dmd.backend.oper;
import dmd.backend.symtab;
import dmd.backend.ty;
import dmd.backend.type;

import dmd.backend.mem;
import dmd.backend.dlist;

import cpp;
import dtoken;
import msgs2;
import parser;
import scopeh;

extern (C++):

enum TX86 = 1;

auto tserr() { return tstypes[TYint]; }

/* Convert from token to assignment operator    */
int asgtoktoop(int tok) { return tok + OPeq - TKeq; }

OPER rel_toktoop(int tok) { return tok - TKle + RELOPMIN; }

extern (D)
{
    immutable uint[2] nanarray = [0,0x7FF80000 ];
    double NAN() { return *cast(double*)(&nanarray[0]); }

    immutable uint[2] nansarray = [0,0x7FF02000 ];
    double NANS() { return *cast(double*)(&nansarray[0]); }

    immutable uint[2] infinityarray = [0,0x7FF00000 ];
    double INFINITY() { return *cast(double*)(&infinityarray[0]); }
}


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

elem *expression()
{

    elem* e = assign_exp();
    while (tok.TKval == TKcomma)
    {
        if (!CPP && config.ansi_c && pstate.STinconstexp)
            synerr(EM_comma_const);     // no , in constant expression
        stoken();
        addlinnum(e);
        elem* e2 = assign_exp();
        e = el_bint(OPcomma,e2.ET,e,e2);  // type is type of rightmost
        if (CPP && (e2 = cpp_opfunc(e)) !is null)
            e = e2;
        /* Arrays are converted, even if in sizeof expression
         */
        if (tybasic(e.ET.Tty) == TYarray)
        {   e.EV.E2 = arraytoptr(e.EV.E2);
            el_settype(e,e.EV.E2.ET);
        }
    }
    return e;
}

/********************************
 * Parse constant-expression.
 */

elem *const_exp()
{
    char save = pstate.STinconstexp;

    pstate.STinconstexp = 1;
    elem* e = assign_exp();
    pstate.STinconstexp = save;
    return e;
}

/********************************
 * Parse assign_exp.
 * Groups right to left.
 *      assign_exp ::= cond_exp [ asgnop assign_exp ]
 *      e1 = e2 = e3 =>
 *                 =
 *                / \
 *              e1   =
 *                  / \
 *                 e2  e3
 * Convert assignments to bit fields to OPbitass to distinguish them
 * from references to bit fields.
 * Watch out for conversion ops on lvalues!
 */

elem *assign_exp()
{
    elem* e,e1,e2;
    type* t;
    type* t2;
    int oper;

    static int X(int fl1, int fl2) { return (fl1 << 16) | fl2; }

    pstate.STinexp++;
    if (tok.TKval == TKthrow)
    {
        e = except_throw_expression();
        goto done;
    }
    e = cond_exp();
    if (cast(int) tok.TKval >= cast(int) TKeq && cast(int) tok.TKval <= cast(int) TKorass)
    {   /* if assignment operator       */
        oper = asgtoktoop(tok.TKval);   /* convert to operator          */
        stoken();
        e = el_bint(oper,e.ET,e,assign_exp());
        if (CPP)
        {
            if (oper == OPeq && tybasic(e.EV.E1.ET.Tty) == TYstruct)
            {   e = cpp_structcopy(e);
                if (e.Eoper != OPeq)   /* if changed it                */
                    goto done;
            }
            else if ((e1 = cpp_opfunc(e)) !is null)
            {   e = e1;
                goto done;
            }
            if (oper != OPeq && (type_struct(e.EV.E1.ET) || type_struct(e.EV.E2.ET)))
            {   e.EV.E1 = cpp_bool(e.EV.E1, 0);
                e.EV.E2 = cpp_bool(e.EV.E2, 0);
                if (!tyintegral(e.ET.Tty))
                    el_settype(e,e.EV.E1.ET);
            }
        }
        chkassign(e);
        switch (oper)
        {   case OPaddass:
            case OPminass:
                impcnv(e);
                t = e.EV.E1.ET;
                if (typtr(t.Tty))
                    scale(e);   /* any scaling necessary */
                else
                {
                    chkarithmetic(e);    // operands must have arithmetic types
                    if (tyreal(t.Tty) || tyimaginary(t.Tty))
                    {
                        assert(errcnt || tyfloating(e.EV.E2.ET.Tty));
                        e.EV.E2 = _cast(e.EV.E2, t);
                        el_settype(e, t);
                    }
                }
                break;
            case OPmulass:
                impcnv(e);
                chkarithmetic(e);
                t2 = e.EV.E2.ET;
                // Floating point cases are:
                //      r *= r
                //      r *= i  => r *= i,0
                //      r *= c  => r *= (r)c
                //      i *= r
                //      i *= i  => i *= i,0
                //      i *= c  => i *= (r)c
                //      c *= r
                //      c *= i
                //      c *= c
                if (tyfloating(t2.Tty))
                {   t = e.EV.E1.ET;
                    switch (X(tytab[tybasic(t.Tty)],tytab[tybasic(t2.Tty)]))
                    {
                        case X(TYFLreal, TYFLimaginary):
                        case X(TYFLreal, TYFLcomplex):
                        case X(TYFLimaginary, TYFLimaginary):
                        case X(TYFLimaginary, TYFLcomplex):
                            switch (tybasic(t.Tty))
                            {   case TYfloat:
                                case TYifloat:  t2 = tstypes[TYfloat];   break;
                                case TYdouble:
                                case TYidouble: t2 = tstypes[TYdouble];  break;
                                case TYldouble:
                                case TYildouble: t2 = tstypes[TYldouble]; break;
                                default:        assert(0);
                            }
                            e.EV.E2 = _cast(e.EV.E2, t2);
                            break;
                        default:
                            break;
                    }
                    el_settype(e, t);
                }
                break;
            case OPdivass:
                impcnv(e);
                chkarithmetic(e);
                t2 = e.EV.E2.ET;
                // Floating point cases are:
                //      r /= r
                //      r /= i  => r /= i,0
                //      r /= c
                //      i /= r
                //      i /= i  => i /= i,0
                //      i /= c
                //      c /= r
                //      c /= i
                //      c /= c
                if (tyfloating(t2.Tty))
                {   t = e.EV.E1.ET;
                    switch (X(tytab[tybasic(t.Tty)],tytab[tybasic(t2.Tty)]))
                    {
                        case X(TYFLreal, TYFLimaginary):
                        case X(TYFLimaginary, TYFLimaginary):
                            switch (tybasic(t.Tty))
                            {   case TYfloat:
                                case TYifloat:  t2 = tstypes[TYfloat];   break;
                                case TYdouble:
                                case TYidouble: t2 = tstypes[TYdouble];  break;
                                case TYldouble:
                                case TYildouble: t2 = tstypes[TYldouble]; break;
                                default:        assert(0);
                            }
                            e.Eoper = OPeq;
                            e.EV.E2 = _cast(e.EV.E2, t2);
                            break;
                        default:
                            break;
                    }
                    el_settype(e, t);
                }
                break;

            case OPshlass:
            case OPshrass:
                e.EV.E1 = cpp_bool(e.EV.E1, 0);
                e.EV.E2 = cpp_bool(e.EV.E2, 0);
                chkintegral(e);
                e.EV.E1 = convertchk(e.EV.E1);      // integral promotions
                e.EV.E2 = _cast(e.EV.E2,tstypes[TYint]);
                break;
            case OPxorass:
            case OPandass:
            case OPorass:
            case OPmodass:
                impcnv(e);
                chkintegral(e);
                break;
            case OPeq:
                e2 = e.EV.E2 = arraytoptr(e.EV.E2);
                t = e.EV.E1.ET;
                if (tybasic(t.Tty) == TYstruct &&
                    tybasic(e2.ET.Tty) == TYstruct)
                    e.Eoper = OPstreq;
                e.EV.E2 = typechk(e2,t);
                break;
            default:
                assert(0);
        }
    }
done:
    pstate.STinexp--;
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
 * Watch out for the case ((e) ? null : (char *) e).
 */

private elem *cond_exp()
{
    elem* e1,e2;
    type* t1,t2;
    tym_t ty1,ty2;
    elem* e3;
    SYMIDX s2a,s2b;
    SYMIDX s3a,s3b;

    e1 = log_exp(OPoror);
    if (tok.TKval == TKques)
    {
        e1 = arraytoptr(e1);
        e1 = cpp_bool(e1, 1);
        chknosu(e1);                    // no structures
        chkunass(e1);                   // no unintended assignments
        stoken();

        s2a = globsym.length;
        e2 = expression();
        s2b = globsym.length;
        chktok(TKcolon,EM_colon);
        if (CPP)
        {
            s3a = globsym.length;
            e3 = assign_exp();
            s3b = globsym.length;
            e2 = el_bint(OPcolon,cast(type *) null,e2,e3);
        }
        else
            e2 = el_bint(OPcolon,cast(type *) null,e2,cond_exp());

        // If both operands are void, the result is void
        t1 = e2.EV.E1.ET;
        t2 = e2.EV.E2.ET;
        ty1 = tybasic(t1.Tty);
        ty2 = tybasic(t2.Tty);
        if (ty1 == TYvoid && ty2 != TYstruct)
            e2.EV.E2 = _cast(e2.EV.E2,t1);
        else if (ty2 == TYvoid && ty1 != TYstruct)
            e2.EV.E1 = _cast(e2.EV.E1,t2);

        // If both types are a struct
        else if (ty1 == TYstruct && ty2 == TYstruct)
        {
            if (t1.Ttag != t2.Ttag)
            {
                if (t1isbaseoft2(t1,t2))
                    e2.EV.E2 = typechk(e2.EV.E2,t1);
                else if (t1isbaseoft2(t2,t1))
                    e2.EV.E1 = typechk(e2.EV.E1,t2);
                else
                    goto L1;
            }
        }
        else if (ty1 == TYstruct || ty2 == TYstruct)
        {
          L1:
            int c1;
            int c2;

            c1 = cpp_cast(&e2.EV.E1, t2, 0);
            c2 = cpp_cast(&e2.EV.E2, t1, 0);
            if (c1 && !c2)
            {
                e2.EV.E1 = typechk(e2.EV.E1,t2);
            }
            else if (!c1 && c2)
            {
                e2.EV.E2 = typechk(e2.EV.E2,t1);
            }
            else
                typerr(EM_illegal_op_types, t1, t2);
        }
        // If types are arithmetic and the same
        else if (CPP && tyarithmetic(ty1) && typematch(t1,t2,0))
        { }
        else
        {
            exp2_ptrtocomtype(e2);
            if (tycomplex(e2.ET.Tty))
            {
                if (!tycomplex(e2.EV.E1.ET.Tty))
                    e2.EV.E1 = _cast(e2.EV.E1, e2.ET);
                if (!tycomplex(e2.EV.E2.ET.Tty))
                    e2.EV.E2 = _cast(e2.EV.E2, e2.ET);
            }
        }
        el_settype(e2,e2.EV.E2.ET);
        if (CPP)
        {
            /* Looks for case where both sides of : generate identical
               temporaries. If so, use only 1 temporary.
             */
            if (s2a < s2b && s3a < s3b)
            {   Symbol *s2 = globsym[s2a];
                Symbol *s3 = globsym[s3a];

                if (s2.Sclass == SCauto && s3.Sclass == SCauto &&
                    s2.Sflags == s3.Sflags &&
                    typematch(s2.Stype,s3.Stype,0)
                   )
                {   /* Replace s3 with s2   */
                    s3.Sflags |= SFLnodtor;
                    el_replacesym(e2.EV.E2,s3,s2);
                    s2a++;                  /* skip dtor for s2 for now     */
                    s3a++;
                }
            }
            func_conddtors(&e2.EV.E1,s2a,s2b);
            func_conddtors(&e2.EV.E2,s3a,s3b);
        }
        return el_bint(OPcond,e2.ET,e1,e2);
    }
    else
        return e1;
}


/******************************
 * Do double duty for || and &&.
 * Groups left to right.
 * log_or_exp ::= log_and_exp { "||" log_and_exp }
 *      e1 || e2 || e3 =>
 *           ||
 *          /  \
 *         ||   e3
 *        /  \
 *      e1   e2
 * log_and_exp ::= inc_or_exp { "&&" inc_or_exp }
 *      e1 && e2 && e3 =>
 *            &&
 *           /  \
 *         &&    e3
 *        / \
 *      e1   e2
 */

private elem *log_exp(int op)
{   elem *e;
    elem *e1;
    SYMIDX marksi;
    enum_TK tk;

    if (op == OPoror)
    {
        e = log_exp(OPandand);
        if (tok.TKval != TKoror)
            goto Lret;
        tk = TKoror;
    }
    else
    {   e = inc_or_exp();
        if (tok.TKval != TKandand)
            goto Lret;
        tk = TKandand;
    }
    do
    {
        addlinnum(e);
        if (CPP)
        {
            stoken();
            // Immediately destroy any temporaries created for the second
            // expression, as it is in its own scope.
            marksi = globsym.length;
            e1 = (op == OPoror) ? log_exp(OPandand) : inc_or_exp();
            func_expadddtors(&e1,marksi,globsym.length,true,true);
            e = el_bint(op,tslogical,e,e1);
        }
        else
        {
            chknosu(e);
            chkunass(e);
            stoken();
            e = el_bint(op,tslogical,e,
                (op == OPoror) ? log_exp(OPandand) : inc_or_exp());
        }
        e.EV.E1 = arraytoptr(e.EV.E1);
        e.EV.E2 = arraytoptr(e.EV.E2);
        if (CPP && (e1 = cpp_opfunc(e)) !is null)
                e = e1;
        else
        {
            if (CPP)
            {
                e.EV.E1 = cpp_bool(e.EV.E1, 1);
                e.EV.E2 = cpp_bool(e.EV.E2, 1);
                chknosu(e.EV.E1);
                chkunass(e.EV.E1);                // no unintended assignments
            }
            /*chknosu(e.EV.E2);*/         // skip check for void tree
            switch (tybasic(e.EV.E2.ET.Tty))
            {   case TYstruct:
                    synerr(EM_bad_struct_use);  // no structs or unions here
                    break;
                case TYvoid:
                    if (config.ansi_c)
                        synerr(EM_void_novalue); // voids have no value
                    el_settype(e,tstypes[TYvoid]);
                    break;
                default:
                    break;
            }
            chkunass(e.EV.E2);
        }
    } while (tok.TKval == tk);                  // if || or &&
Lret:
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

private elem *inc_or_exp()
{
    elem* e = xor_exp();
    while (tok.TKval == TKor)           // if "|"
    {
        stoken();
        e = el_bint(OPor,cast(type *) null,e,xor_exp());
        elem* e1;
        if (CPP && (e1 = cpp_opfunc(e)) !is null)
            e = e1;
        else
        {
            impcnv(e);                  // do implicit conversions
            chkintegral(e);
        }
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

private elem *xor_exp()
{
    elem* e = and_exp();
    while (tok.TKval == TKxor)          // if "^"
    {
        stoken();
        e = el_bint(OPxor,cast(type *) null,e,and_exp());
        elem* e1;
        if (CPP && (e1 = cpp_opfunc(e)) !is null)
            e = e1;
        else
        {
            impcnv(e);                  // do implicit conversions
            chkintegral(e);
        }
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

private elem *and_exp()
{
    elem* e = equal_exp();
    while (tok.TKval == TKand)            // if "&"
    {
        stoken();
        e = el_bint(OPand,cast(type *) null,e,equal_exp());
        elem* e1;
        if (CPP && (e1 = cpp_opfunc(e)) !is null)
            e = e1;
        else
        {
            impcnv(e);
            chkintegral(e);
        }
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

private elem *equal_exp()
{
    int op;
    elem *e1;

    elem* e = rel_exp();
    while (1)
    {
        switch (cast(int) tok.TKval)
        {    case TKeqeq:
                op = OPeqeq;
                goto L1;
             case TKne:
                op = OPne;
             L1:
                stoken();
                e = el_bint(op,tslogical,e,rel_exp());
                if (CPP && (e1 = cpp_opfunc(e)) !is null)
                    e = e1;
                else
                {   exp2_ptrtocomtype(e);

                    const tyfl = tytab[tybasic(e.EV.E1.ET.Tty)] | tytab[tybasic(e.EV.E2.ET.Tty)];
                    if ((tyfl & (TYFLreal | TYFLimaginary)) == (TYFLreal | TYFLimaginary))
                    {
                        // Cast both sides to be complex

                        e.EV.E1 = _cast(e.EV.E1, tstypes[TYcldouble]);
                        e.EV.E2 = _cast(e.EV.E2, tstypes[TYcldouble]);
                    }
                }
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

private elem *rel_exp()
{
    int op;
    elem *e1;

    elem* e = shift_exp();
    while (1)
    {   switch (tok.TKval)
        {
            case TKgt:
                if (pstate.STinarglist)         // if in template arg list
                    goto default;               // not an operator
                goto case TKle;
            case TKle:
            case TKge:
            case TKlt:
            case TKunord:
            case TKlg:
            case TKleg:
            case TKule:
            case TKul:
            case TKuge:
            case TKug:
            case TKue:
                op = rel_toktoop(tok.TKval);
                stoken();
                e = el_bint(op,tslogical,e,shift_exp());
                if (CPP && (e1 = cpp_opfunc(e)) !is null)
                    e = e1;
                else
                {   type *t1;
                    type *t2;

                    // Bring pointers to a common type.
                    exp2_ptrtocomtype(e);

                    // Only the offsets need to be compared
                    e.EV.E1 = lptrtooffset(e.EV.E1);
                    e.EV.E2 = lptrtooffset(e.EV.E2);

                    /*  For integral types, convert oddball relationals
                        into their integer equivalents.
                     */
                    t1 = e.EV.E1.ET;
                    if (!tyfloating(t1.Tty))
                    {
                        op = rel_integral(op);
                        if (op <= 1)
                        {
                            /* The result is determinate, create:
                                (e1 , e2) , op
                             */
                            e.Eoper = OPcomma;
                            e = el_bint(OPcomma,tstypes[TYint],e,el_longt(tstypes[TYint],op));
                        }
                        else
                            e.Eoper = cast(ubyte)op;
                    }

                    // Disallow complex operands
                    t2 = e.EV.E2.ET;
                    uint tyfl = tytab[tybasic(t1.Tty)] | tytab[tybasic(t2.Tty)];
                    if (tyfl & TYFLcomplex ||
                        (tyfl & (TYFLreal | TYFLimaginary)) == (TYFLreal | TYFLimaginary))
                    {
                        typerr(EM_complex_operands, t1, t2);
                    }
                }
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

private elem *shift_exp()
{
    int op;
    elem *e1;

    elem* e = add_exp();
    while (tok.TKval == TKshl || tok.TKval == TKshr)
    {
        if (CPP)
        {
            if (ANGLE_BRACKET_HACK && pstate.STinarglist && tok.TKval == TKshr)
            {   /* Split the '>>' token into '>' '>'    */
                tok.TKval = TKgt;
                token_unget();
                break;
            }
            op = (tok.TKval == TKshl) ? OPshl : OPshr;
            stoken();
            e = el_bint(op,cast(type *) null,e,add_exp());
            e.EV.E1 = arraytoptr(e.EV.E1);
            e.EV.E2 = arraytoptr(e.EV.E2);
            if ((e1 = cpp_opfunc(e)) !is null)
                e = e1;
            else
            {
                e.EV.E1 = cpp_bool(e.EV.E1, 0);
                e.EV.E2 = cpp_bool(e.EV.E2, 0);
                chkintegral(e);
                e.EV.E1 = convertchk(e.EV.E1);      // integral promotions
                e.EV.E2 = _cast(e.EV.E2,tstypes[TYint]);     // make right side an int
                el_settype(e,e.EV.E1.ET);
            }
        }
        else
        {
            op = (tok.TKval == TKshl) ? OPshl : OPshr;
            stoken();
            e = el_bint(op,cast(type *) null,e,add_exp());
            {
                chkintegral(e);
                e.EV.E1 = convertchk(e.EV.E1);      // integral promotions
                e.EV.E2 = _cast(e.EV.E2,tstypes[TYint]);     // make right side an int
                el_settype(e,e.EV.E1.ET);
            }
        }
    }
    return e;
}


/******************************
 * Groups left to right.
 * add_exp ::= mul_exp { "+" mul_exp | "-" mul_exp }
 * Bugs: doesn't do pointer addition and subtraction properly.
 */

private elem *add_exp()
{
    elem *e1;

    elem* e = mul_exp();
    while (tok.TKval == TKadd || tok.TKval == TKmin)
    {
        uint oper = (tok.TKval == TKadd) ? OPadd : OPmin;
        stoken();
        e = el_bint(oper,null,arraytoptr(e),arraytoptr(mul_exp()));
        if (CPP && (e1 = cpp_opfunc(e)) !is null)
            e = e1;
        else
        {
            if (CPP)
            {   e.EV.E1 = cpp_bool(e.EV.E1, 0);
                e.EV.E2 = cpp_bool(e.EV.E2, 0);
            }
            if (typtr(e.EV.E1.ET.Tty))
            {   e.ET = e.EV.E1.ET;
                e.ET.Tcount++;
                if (e.Eoper == OPadd)
                    scale(e);           // do any scaling necessary
                else
                    e = minscale(e);    // take care of - scaling
            }
            else if (typtr(e.EV.E2.ET.Tty))
            {   e.ET = e.EV.E2.ET;
                e.ET.Tcount++;
                if (e.Eoper == OPadd)
                    scale(e);
                else
                    synerr(EM_bad_ptr_arith);       // illegal pointer arithmetic
            }
            else
            {
                impcnv(e);

                // If one operand is real and the other is imaginary,
                // then the result is complex.
                if (tyfloating(e.ET.Tty) &&
                    (tytab[e.EV.E1.ET.Tty & 0xFF] ^
                     tytab[e.EV.E2.ET.Tty & 0xFF]) == (TYFLreal | TYFLimaginary))
                {   type *t;

                    switch (tybasic(e.ET.Tty))
                    {
                        case TYfloat:
                        case TYifloat:      t = tstypes[TYcfloat];    break;
                        case TYdouble:
                        case TYidouble:     t = tstypes[TYcdouble];   break;
                        case TYldouble:
                        case TYildouble:    t = tstypes[TYcldouble];  break;
                        default:
                            assert(0);
                    }
                    el_settype(e, t);
                }
            }
        }
    }
    return e;
}


/******************************
 * Groups left to right.
 * mul_exp ::= una_exp { "*" una_exp | "/" una_exp | "%" una_exp }
 */

private elem *mul_exp()
{
    elem *e1;

    elem* e = CPP ? memptr_exp() : una_exp();
    while (1)
    {
        int op;
        switch (tok.TKval)
        {
            case TKstar:        op = OPmul;     break;
            case TKdiv:         op = OPdiv;     break;
            case TKmod:         op = OPmod;     break;
            default:
                return e;
        }
        stoken();
        e = el_bint(op,null,e,CPP ? memptr_exp() : una_exp());
        if (CPP && (e1 = cpp_opfunc(e)) !is null)
            e = e1;
        else
        {   type *t1;
            type *t2;

            impcnv(e);
            final switch (e.Eoper)
            {
                case OPmul:
                case OPdiv:
                    chkarithmetic(e);
                    if (tyfloating(e.ET.Tty))
                    {
                        t1 = e.EV.E1.ET;
                        t2 = e.EV.E2.ET;
                        if (tyreal(t1.Tty))
                        {
                            el_settype(e, t2);
                            if (e.Eoper == OPdiv && tyimaginary(t2.Tty))
                            {   // x/iv = i(-x/v)
                                el_settype(e.EV.E2, t1);
                                e = el_unat(OPneg, t2, e);
                            }
                        }
                        else if (tyreal(t2.Tty))
                            el_settype(e, t1);
                        else if (tyimaginary(t1.Tty))
                        {
                            if (tyimaginary(t2.Tty))
                            {   type *t;

                                switch (tybasic(t1.Tty))
                                {
                                    case TYifloat:   t = tstypes[TYfloat];       break;
                                    case TYidouble:  t = tstypes[TYdouble];      break;
                                    case TYildouble: t = tstypes[TYldouble];     break;
                                    default:
                                        assert(0);
                                }
                                el_settype(e, t);
                                if (e.Eoper == OPmul)
                                {   // iy * iv = -yv
                                    e = el_unat(OPneg, t, e);
                                }
                            }
                            else
                                el_settype(e, t2);      // result is complex
                        }
                        else if (tyimaginary(t2.Tty))
                            el_settype(e, t1);          // t1 is complex
                    }
                    break;

                case OPmod:
                    chkintegral(e);
                    break;
            }
        }
    }
    return e;
}

/******************************
 * Groups left to right.
 * memptr_exp ::= una_exp { ".*" una_exp | ".*" una_exp }
 */

private elem *memptr_exp()
{
    elem* e1,e2;
    type *t;

    elem* e = una_exp();
    while (1)
    {
        switch (tok.TKval)
        {
            case TKarrowstar:
                stoken();
                e2 = una_exp();
                e = el_bint(OParrowstar,null,arraytoptr(e),e2);
                if ((e1 = cpp_opfunc(e)) !is null)
                {   e = e1;
                    continue;
                }
                e.EV.E2 = null;
                t = e.EV.E1.ET;
                e = selecte1(e,t);
                if (t)
                {
                    if (!typtr(t.Tty))
                        // pointer req'd before .*
                        typerr(EM_pointer, t, cast(type *) null);
                    else
                        t = t.Tnext;
                }
                e = el_unat(OPind, t, e);
                handleaccess(e);
                break;
            case TKdotstar:
                stoken();
                e2 = una_exp();
                break;
            default:
                return e;
        }

        /* Now we should have e, the instance of the class, and
           e2, the member pointer. Resolve the two.
         */
        e = dodotstar(e,e2);
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
 *              ++ primary_exp
 *              -- primary_exp
 *              (type_name) una_exp
 *              simple_type_name (expression_list)
 *              sizeof una_exp
 *              sizeof (type_name)      ?? primary ??
 *              primary_exp ++
 *              primary_exp --
 *              new type_name
 *              new type_name initializer
 *              new (type_name)
 *              delete una_exp
 *              delete [expression] una_exp
 */

/*private*/ elem *una_exp()
{
    elem* e, e1;
    int op;
    elem* eo;
    ubyte parens;

    switch (tok.TKval)
    {
        case TKand:
            op = OPaddr;
            if (!CPP)
                goto L2;
            parens = 0;
            stoken();
            if (tok.TKval == TKident || tok.TKval == TKcolcol)
            {   // Need to do some lookahead here
                Token_lookahead tla;

                tla.init();
                while (1)
                {   switch (tla.lookahead())
                    {   case TKident:
                        case TKcolcol:
                            continue;
                        case TKlbra:
                        case TKlpar:
                            break;
                        default:
                            pstate.STisaddr = 1;
                            break;
                    }
                    break;
                }

                tla.term();
            }
            // This is very primitive, but it works for 99% of the cases
            else if (tok.TKval == TKlpar)
            {   pstate.STisaddr = 1;    // tell dodot() we are doing an &
                parens = PEFparentheses;
            }
            e1 = una_exp();
            e1.PEFflags |= parens;
            pstate.STisaddr = 0;
            goto L5;

        case TKplpl:
            op = OPaddass;
            goto L2;

        case TKmimi:
            op = OPminass;
        L2:
            stoken();
            e1 = una_exp();             /* need some more checks here    */
        L5:
            if (op == OPminass)
                e1 = arraytoptr(e1);
            e = el_calloc();
            e.Eoper = cast(ubyte)op;
            e.EV.E1 = e1;
            if (CPP)
            {
                if (op == OPaddass)
                    e.Eoper = OPpreinc;
                else if (op == OPminass)
                    e.Eoper = OPpredec;
                if ((eo = cpp_opfunc(e)) !is null)
                {   e = eo;
                    goto done;
                }
                e.Eoper = cast(ubyte)op;          // no overload, go back to correct op
            }
            if (op == OPaddr)
            {
                if (e1.Eoper == OPstring ||
                    e1.Eoper == OPbit ||
                    e1.Eoper == OPconst)
                        synerr(EM_noaddress);   // can't take address of that
                version (none)
                {
                    // Ignore extra &s in front of address of function.
                    // Sloppy, but other compilers do it.
                    if (typtr(e1.ET.Tty) && tyfunc(e1.ET.Tnext.Tty))
                    {   e.EV.E1 = null;
                            el_free(e);
                            e = e1;
                            goto done;
                    }
                }
                e.EV.E1 = null;
                el_free(e);
                if (tyfunc(e1.ET.Tty))
                    e = arraytoptr(e1);
                else
                {
                    if (e1.Eoper == OPvar)
                    {
                        symbol_debug(e1.EV.Vsym);
                        switch (e1.EV.Vsym.Sclass)
                        {
                            case SCregister:
                            case SCregpar:
                                if (CPP)
                                    break;
                                goto case SCpseudo;

                            case SCpseudo:
                                synerr(EM_noaddress); // can't take address of register
                                break;

                            case SCstruct:      // kludge to do memptrs
                                if (!CPP)
                                    break;
                            {   /* Construct a pointer to member        */
                                type *tm;
                                targ_size_t offset;

                                tm = type_allocmemptr(e1.EV.Vsym,e1.ET);
                                offset = e1.EV.Voffset;
                                /* Offset by 1 to leave room for null memptr */
                                e = el_longt(tm,offset + 1);
                                el_free(e1);
                                goto done;
                            }

                            default:
                                break;
                        }
                    }
                    // Allow & in front of pointers to func members.
                    // Note that this allows &&&&CLASS::func, oh well.
                    else if (CPP &&
                             e1.Eoper == OPrelconst &&
                             isclassmember(e1.EV.Vsym) &&   // if member
                             tyfunc(e1.ET.Tnext.Tty))
                    {   e = e1;
                        goto done;
                    }

                    //e1 = poptelem(e1);
                    e = exp2_addr(e1);
                    chklvalue(e);       // can only take address of lvalue
                }
            }
            else
            {
                e.ET = e1.ET;
                e.ET.Tcount++;
                getinc(e);              // get size of increment
                chkassign(e);
                impcnv(e);
            }
            goto done;

        case TKadd:
            op = OPuadd;
            goto L1;
        case TKstar:
            op = OPind;
            goto L1;
        case TKmin:
            op = OPneg;
            goto L1;
        case TKnot:
            op = OPnot;
            goto L1;
        case TKcom:
            op = OPcom;
        L1:
            stoken();
            e1 = arraytoptr(una_exp());
            e = el_calloc();
            e.Eoper = cast(ubyte)op;
            e.EV.E1 = e1;
            if (CPP)
            {
                if ((eo = cpp_opfunc(e)) !is null)
                {   e = eo;
                    goto done;
                }
                if (op == OPuadd)
                {
                    eo = e.EV.E1;
                    e.EV.E1 = null;
                    el_free(e);
                    return eo;
                }
                else if (op == OPind)
                    e.EV.E1 = cpp_ind(e.EV.E1);
                else
                    e.EV.E1 = cpp_bool(e.EV.E1, 0);
            }
            e.EV.E1 = e1 = convertchk(e.EV.E1);
            chknosu(e.EV.E1);             // structs not allowed
            switch (op)
            {   case OPnot:
                    chkunass(e1);       // check unintended assignment
                    e.ET = tslogical;
                    break;
                case OPind:
                    if (tybasic(e1.ET.Tty) == TYvptr)
                    {
                        // Convert handle pointer to far pointer
                        type* t = newpointer(e1.ET.Tnext);
                        t.Tty = (t.Tty & ~mTYbasic) | TYfptr;
                        // Cast to far pointer
                        e1 = _cast(e1,t);
                        e.EV.E1 = e1;
                    }
                    else if (!typtr(e1.ET.Tty))
                    {   // pointer required before indirection
                        typerr(EM_pointer,e1.ET,cast(type *) null,cast(char *) null);
                        e.ET = e1.ET;
                        break;
                    }
                    e.ET = e1.ET.Tnext;
                    e.ET.Tcount++;
                    if (CPP)
                        e = reftostar(e);
                    handleaccess(e);
                    goto done;
                case OPcom:
                    chkintegral(e);
                    e.ET = e1.ET;
                    break;
                case OPneg:
                    chkarithmetic(e);
                    e.ET = e1.ET;
                    break;
                case OPuadd:
                    chkarithmetic(e);
                    e.EV.E1 = null;
                    el_free(e);
                    e = e1;
                    goto done;
                default:
                    break;
            }
            e.ET.Tcount++;
            goto done;

        static if (TARGET_LINUX || TARGET_OSX || TARGET_FREEBSD || TARGET_OPENBSD || TARGET_SOLARIS)
        {
        case TK_bltin_const:            // return 1 if compile time constant expression
            e = exp_isconst();          // otherwise return 0
            goto done;
        }

        case TKsizeof:
        case TK_typeinfo:
        case TK_typemask:
            if (config.ansi_c && preprocessor)
                synerr(EM_prep_exp);    // sizeof illegal in preprocessor exp
            e = exp_sizeof(tok.TKval);
            goto done;

        case TKcolcol:          // check for ::new or ::delete
            stoken();
            if (tok.TKval == TKnew)
            {
                e = exp_new(1);
                goto L4;
            }
            else if (tok.TKval == TKdelete)
            {   e = exp_delete(1);
                goto L4;
            }
            token_unget();
            tok.TKval = TKcolcol;
            goto def;                   // back to default behavior
        case TKnew:
            e = exp_new(0);
            goto L4;
        case TKdelete:
            e = exp_delete(0);
        L4:
            e = prim_post(e);
            goto done;
        case TKint:
        case TKunsigned:
        case TKshort:
        case TKchar:
        case TKfloat:
        case TKlong:
        case TKdouble:
        case TKvoid:
        case TKbool:
        case TKwchar_t:
        case TKchar16_t:
        case TKchar32_t:
        case TKdecltype:
            if (!CPP)
                goto def;
            {   type *t;

                type_specifier(&t);
                e = exp_simplecast(t);
                type_free(t);
            }
            goto done;
        case TKident:                           // JTM: Added this case to handle
            if (!CPP)
                goto def;
        {   type *typ_spec;                     // C++ casts of the form
                                                // ENUM::Aenum(i)
                                                // Where Anum is a nested enumeration
            if (isexpression() > 1 )
                goto def;
            if (type_specifier(&typ_spec ))
            {
                if (tok.TKval != TKlpar &&
                    pstate.STintemplate &&
                    tybasic(typ_spec.Tty) == TYident)
                {
                    e = el_longt(typ_spec, 0);
                }
                else
                    e = exp_simplecast( typ_spec );
                type_free(typ_spec);
            }
            else
            {   type_free(typ_spec);
                goto def;
            }
            break;
        }

        case TKstatic_cast:
        case TKconst_cast:
        case TKreinterpret_cast:
        case TKdynamic_cast:
        {
            type *typ_spec;

            int tk = tok.TKval;
            if (stoken() != TKlt)
                cpperr(EM_lt_cast);             // '<' expected

            // Parse type-name
            stoken();
            type_specifier(&typ_spec);
            type* t = declar_abstract(typ_spec);
            fixdeclar(t);

            if (tok.TKval != TKgt)
            {   cpperr(EM_gt);                  // '>' expected
                panic(TKgt);
            }

            // Parse "(" expression ")"
            stoken();
            chktok(TKlpar,EM_lpar2,"cast<>");
            { char save = pstate.STinarglist;
              pstate.STinarglist = 0;           // parens protect > and >>
              e = expression();
              pstate.STinarglist = save;
            }
            chktok(TKrpar,EM_rpar);

            e = rtti_cast(cast(enum_TK)tk,e,t);
            type_free(t);
            if (config.ansi_c && preprocessor)
                synerr(EM_prep_exp);            // casts not allowed in preprocessor
            e = prim_post(e);
            type_free(typ_spec);
            goto done;
        }
        case TKtypeid:
            e = exp_sizeof(tok.TKval);
            e = prim_post(e);
            goto done;
        case TKlpar:                    // (type_name) or (expression)
        {   type *typ_spec;

            stoken();
            if (CPP)
            {
                if (isexpression())
                {   typ_spec = null;
                    goto isexp;
                }
                pstate.STisaddr = 0;    // not a member pointer expression
            }
            if (type_specifier(&typ_spec)) // if type_name
            {
                type* t = declar_abstract(typ_spec); // read abstract_declarator
                fixdeclar(t);           // fix declarator
                chktok(TKrpar,EM_rpar); // ')' ends it
                e = una_exp();
                e = _cast(e,t);          // do the actual cast
                type_free(t);
                if (config.ansi_c && preprocessor)
                    synerr(EM_prep_exp);        // casts not allowed in preprocessor
                else if (config.flags4 & CFG4warnccast && !preprocessor)
                    warerr(WM.WM_ccast);           // no C style casts
            }
            else
            {
              isexp:
                if (CPP)
                {
                    char save = pstate.STinarglist;
                    pstate.STinarglist = 0;     // parens protect > and >>
                    e = expression();
                    pstate.STinarglist = save;
                }
                else
                {
                    e = expression();
                }
                if (tok.TKval != TKrpar)
                    synerr(EM_rpar);
                stoken();
                e = prim_post(e);
            }
            type_free(typ_spec);
            break;
        }
        default:
        def:
            e = primary_exp();
            break;
    } // switch (tok.TKval)

    version (none) // can't seem to make this work right
    {
        // Give error if a pointer to member stub
        e1 = EUNA(e) ? e.EV.E1 : e;
        if (e1.Eoper == OPvar && e1.EV.sp.Vsym.Sclass == SCstruct)
            // no instance of class
            cpperr(EM_no_instance,e1.EV.sp.Vsym.Sident);
    }

done:
    return e;
}


/******************************
 * Primary expression.
 * Groups left to right.
 *      primary_exp ::= identifier
 *                      :: identifier
 *                      constant
 *                      string
 *                      ( expression )
 *                      primary_lvalue . identifier
 *                      primary_exp [ expression ]
 *                      primary_exp ( exp_list opt )
 *                      primary_exp.identifier
 *                      asm ( string )
 */

/*private*/ elem *primary_exp()
{
    //printf("primary_exp()\n");
    elem *e;
    type *t;
    Symbol *s;
    enum_SC sc;
    Scope *scx;
    type *tclass;
    Symbol *smember;
    Symbol *sclass;
    int impthis;
    int bColcol = false;

    switch (tok.TKval)
    {
        case TKcolcol:
            // ::identifer means look for id at function scope level
            bColcol = true;
            stoken();
            if (tok.TKval == TKoperator)
            {
                type *t2;
                int oper;

                char* p = cpp_operator(&oper,&t2);
                type_free(t2);
                s = scope_search(p,SCTglobal);
                token_unget();
                goto L3;
            }
            else if (tok.TKval == TKident)
            {   s = scope_search(tok.TKid,SCTglobal);
                goto L3;
            }
            else if (tok.TKval == TKtemplate)
            {   stoken();
                if (tok.TKval != TKident)
                {   synerr(EM_ident_exp);               // identifier expected
                    goto err;
                }
                s = scope_search(tok.TKid,SCTglobal);
                if (!s || (s.Sclass != SCtemplate && s.Sclass != SCfunctempl))
                    synerr(EM_template_expected);
                goto L3;
            }
            else
            {   synerr(EM_ident_exp);           // identifier expected
                goto err;
            }
            assert(0);

        case TKoperator:
            {   char *p;
                type *t2;
                int oper;

                p = cpp_operator(&oper,&t2);
                type_free(t2);
                token_unget();
                token_setident(p);
            }
            goto L6;

        case TKident:
L6:         // Look for case of #if defined(identifier)
            if (preprocessor && strcmp(tok.TKid,"defined") == 0)
            {   e = el_longt(tstypes[TYint],pragma_defined());
                break;
            }
            if (!CPP)
            {   s = scope_search(tok.TKid,SCTglobal | SCTlocal | SCTparameter);
                goto L3;
            }
            s = scope_searchx(tok.TKid,SCTglobal | SCTnspace | SCTtempsym |
                        SCTtemparg | SCTmfunc | SCTlocal | SCTwith |
                        SCTclass | SCTparameter,
                        &scx);

            //printf("test5: '%s' = %p\n", tok.TKid, s);
            if (config.flags4 & CFG4adl)
            {
                // Argument Dependent Lookup
                //printf("ADL s = %p '%s'\n", s, s.Sident);
                if (!s ||
                        (tyfunc(s.Stype.Tty) &&
                         s.Sclass != SCtypedef &&
                         (scx.sctype & (SCTglobal | SCTnspace) ||
                          s.Sclass == SCfuncalias &&
                          (!s.Sfunc.Falias.Sscope ||
                           s.Sfunc.Falias.Sscope.Sclass == SCnamespace)
                         )
                        )
                   )
                {
                    char[32] buffer = void;
                    char *id;
                    param_t *ptal = null;
                    ubyte flags = 0;

                    int len = strlen(tok.TKid) + 1;
                    if (len <= buffer.sizeof)
                        id = cast(char *)memcpy(&buffer[0], tok.TKid, len);
                    else
                    {
                        //id = alloca_strdup(tok.TKid);
                        {
                            const(char)* p = tok.TKid;
                            char* s2 = cast(char *)alloca(len);
                            id = cast(char *) memcpy(s2,p,len);
                        }
                    }

                    stoken();
                    if (s)
                    {
                        if (tok.TKval == TKlt)
                        {
                            // Scan forward to see if <arglist> is followed by (
                            Token_lookahead tla;
                            tla.init();
                            int brack = 1;
                            int paren = 0;
                            enum_TK nextt;

                            while (1)
                            {   tla.lookahead();
                                switch (tok.TKval)
                                {   case TKlt:      if (!paren)
                                                        brack++;
                                                    continue;
                                    case TKgt:      if (paren)      continue;
                                                if (--brack)        continue;
                                                break;
                                    //case TKshr: if ((brack -= 2) > 0) continue;
                                    case TKlpar: paren++;   continue;
                                    case TKrpar: paren--;   continue;
                                    case TKeof:  break;
                                    default:     continue;
                                }
                                break;
                            }
                            nextt = tla.lookahead();
                            tla.term();

                            if (nextt == TKlpar)
                            {
                                pstate.STinarglist++;
                                ptal = template_gargs2(s);
                                pstate.STinarglist--;
                                flags = PEFtemplate_id;
                                stoken();
                            }
                        }
                        else if (tok.TKval == TKlg)
                        {
                            flags = PEFtemplate_id;
                            stoken();
                            if (tok.TKval != TKlpar)
                            {   // Back up two tokens
                                token_unget();
                                tok.TKval = TKlg;
                            }
                        }
                    }
                    if (tok.TKval == TKlpar)
                    {   list_t arglist;
                        Symbol *sadl;

                        char save = pstate.STinarglist;
                        pstate.STinarglist = 0;
                        stoken();
                        getarglist(&arglist);
                        if (tok.TKval != TKrpar)
                            synerr(EM_rpar);
                        stoken();
                        pstate.STinarglist = save;

                        /* Evaluating the arglist may have defined
                         * it in the global scope. (If s is a friend of
                         * a class template.)
                         * So try again to find it.
                         */
                        if (!s)
                            s = scope_searchx(id, SCTglobal | SCTnspace, &scx);

                        sadl = adl_lookup(id, s, arglist);
                        if (!sadl)
                        {   // Undefined symbol
                            synerr(EM_undefined, id);
                            e = el_longt(tserr,0L);
                            param_free(&ptal);
                            break;
                        }

                        s = cpp_overload(sadl,null,arglist,null,ptal,flags != 0);
                        e = el_var(s);
                        //e.EV.sp.spu.Vtal = ptal;
                        //e.PEFflags |= flags;
                        e = xfunccall(e, null, null, arglist);

                        if (sadl.Sclass == SCadl)
                        {   assert(s != sadl);
                            symbol_free(sadl);
                        }

                        // Check for builtin's
                        e = builtinFunc(e);

                        goto done;
                    }
                    else
                    {
                        token_unget();
                        token_setident(id);
                        param_free(&ptal);
                    }
                }
            }

            if (s && scx.sctype & (SCTclass | SCTwith | SCTmfunc))
            {
                if (scx.sctype & SCTclass)
                {
                    e = null;
                    impthis = 0;                // no implied "this"
                    goto Lmember;
                }
                else if (scx.sctype & SCTwith)  // if s is from with object
                {   e = cast(elem *) scx.root;  // retrieve with object
                    elem_debug(e);
                    impthis = 0;                // no implied "this"
                    goto Lmember;
                }
                else if (scx.sctype & SCTmfunc)
                {
                    /* If within a member function, check and see if
                     * identifier is a member of that class. If so, replace
                     * ident with this.ident.
                     */
                    impthis = 1;                // implied "this"
                    e = null;

                Lmember:
                    switch (s.Sclass)
                    {   case SCenum:
                        case SCtypedef:
                        case SCstruct:
                            goto L3;
                        default:
                            break;
                    }

                    stoken();
                    if (tok.TKval == TKcolcol)
                        goto Lretry;
                    token_unget();
                    token_setident(&s.Sident[0]);

                    sclass = pstate.STstag;     // from struct_searchmember()

                Lmember2:
                    smember = s;
                    symbol_debug(sclass);
                    pstate.STisaddr = 0;
                    if (!impthis || funcsym_p.Sfunc.Fflags & Fstatic)
                    {
                        e = dodot(el_copytree(e), sclass.Stype, false);
                    }
                    else
                    {
                        Symbol* sthis = scope_search("this",SCTlocal);
                        assert(sthis);
                        e = el_var(sthis);
                        type* t2 = e.ET;
                        e = el_unat(OPind,t2.Tnext,e);
                        e = dodot(e, e.ET, false);
                    }
                    goto done;
                }
                else
                    assert(0);
            }
            goto L3;

        case TKsymbol:
            s = tok.TKsym;
            goto L3;

        case TKthis:
            /* Ugly hack. Necessary if tokens were read from a token list */
            tok.TKid = strcpy(&tok_ident[0],&cpp_name_this[0]);

            s = scope_search(tok.TKid,CPP ? SCTlocal : (SCTglobal | SCTlocal));
            /* C++98 3.4.3 if the next token is a ::, symbols that are not
             * class-names or namespace-names are ignored in the lookup.
             */

        L3:
            if (!s)                     /* if identifier is not defined */
            {
                if (preprocessor)       /* if inside a #if              */
                {
                    stoken();
                    e = el_longt(tstypes[TYint],0); /* treat as number 0         */
                    break;
                }
                // Do not install "this" in the global symbol table!
                // (it will cause all kinds of trouble later)
                if (tok.TKval == TKthis)
                {
                    synerr(EM_undefined,&cpp_name_this[0]); /* undefined identifier */
                    stoken();
                    e = el_longt(tserr,0L);
                    break;
                }
                if (!config.ansi_c && (s = pseudo_declar(tok.TKid)) !is null)
                {   stoken();
                    e = el_var(s);
                }
                else if (tok.TKid[0] == '_' && strcmp(tok.TKid, "__func__") == 0 && funcsym_p)
                {
                    // Declare per C99 6.4.2.2:
                    //  static const char __func__[] = "function-name";
                    s = funcsym_p.Sfunc.F__func__;
                    if (!s)
                    {
                        s = symbol_calloc("__func__");
                        s.Sclass = SCstatic;
                        auto id = prettyident(funcsym_p);
                        size_t dim = strlen(id) + 1;
                        auto dtb = DtBuilder(0);
                        dtb.nbytes(dim, id);
                        s.Sdt = dtb.finish();
                        type *t2 = type_alloc(mTYconst | TYchar);
                        t2 = type_allocn(TYarray, t2);
                        t2.Tdim = dim;
                        s.Stype = t2;
                        t2.Tcount++;
                        outdata(s);
                        symbol_keep(s);
                        funcsym_p.Sfunc.F__func__ = s;
                    }
                    goto L3;
                }
                else
                {
                    if (pstate.STmaxsequence != ~0u)
                    {   // Could be a forward reference, don't try to
                        // create symbol.
                        synerr(EM_undefined, tok.TKid);
                        e = el_longt(tserr,0L);
                        break;
                    }

                    if (token_peek() == TKlpar) /* define as function returning int */
                    {
                        s = scope_define(tok.TKid,SCTglobal,SCextern); // top level def
                        stoken();
                        static if (MEMMODELS == 1)
                            t = type_alloc(functypetab[cast(int) linkage]);
                        else
                            t = type_alloc(functypetab[cast(int) linkage][config.memmodel]);
                        t.Tmangle = funcmangletab[cast(int) linkage];
                        t.Tnext = tstypes[TYint];
                        t.Tnext.Tcount++;
                        s.Stype = t;
                        t.Tcount++;
                        symbol_func(s);
                        if (CPP)
                            s.Sfunc.Fflags |= Foverload | Ftypesafe;
                        if (config.flags3 & CFG3strictproto)
                            synerr(EM_noprototype,prettyident(s));      // unprototyped function
                        e = el_var(s);
                    }
                    else                        /* else undefined identifier    */
                    {
                        Symbol *s2 = null;

                        if (tok.TKval == TKident)
                        {
                            if (CPP)
                                s2 = scope_search_correct(tok.TKid,SCTglobal | SCTnspace | SCTtempsym |
                                    SCTtemparg | SCTmfunc | SCTlocal | SCTwith |
                                    SCTclass | SCTparameter);
                            else
                                s2 = scope_search_correct(tok.TKid,SCTglobal | SCTlocal | SCTparameter);
                        }
                        s = scope_define(tok.TKid,SCTglobal,SCextern); // top level def
                        if (s2)
                            synerr(EM_undefined2, &s.Sident[0], &s2.Sident[0]);
                        else
                            synerr(EM_undefined,&s.Sident[0]);
                        s.Sclass = SCunde;
                        t = tserr;              /* guess that he meant an integer */
                        s.Stype = t;
                        t.Tcount++;
                        e = el_longt(tserr,0L);
                        stoken();
                    }
                }
                break;
            } // if ident is not defined
            else if (!CPP)
            {
                if (s.Sflags & SFLvalue && s.Sclass == SCconst)       // if constant value
                {   e = el_copytree(s.Svalue);
                    stoken();
                }
                else // symbol is a variable
                {
                    e = el_var(s);
                    t = e.ET;
                    if (!(sytab[cast(int) s.Sclass] & SCEXP))
                        synerr(EM_not_variable,tok.TKid,"".ptr);    // ident is not a variable
                    stoken();

                    // This can happen in C if in __out block and the
                    // __result is a reference
                    if (tyref(t.Tty))          // if <reference to>
                    {   // convert (e) to (*e)
                        e = reftostar(e);
                    }
                }
            }
            else if ((sc = s.Sclass) == SCtypedef &&
                     tybasic(s.Stype.Tty) == TYstruct)
            {
                tclass = s.Stype;
                s = tclass.Ttag;
                goto L5;
            }
            else if (sc == SCtemplate)
            {
                s = template_expand(s,0);
                goto L3;
            }
            else if (sc == SCalias)
            {
                s = (cast(Aliassym *)s).Smemalias;
                goto L3;
            }
            else if (sc == SCnamespace)
            {
                s = nspace_qualify(cast(Nspacesym *)s);
                goto L3;
            }
            else if (sc == SCstruct)
            {
                tclass = s.Stype;
            L5:
                Symbol *sthis;
                if (stoken() == TKlpar)         /* if cast              */
                {
                    e = exp_simplecast(tclass);
                    break;
                }
                else if (tok.TKval == TKcolcol)
                {
                }
                else
                {
                    cpperr(EM_colcol_lpar,prettyident(s));      // :: or ( expected
                    goto err2;
                }

                /* BUG: we are not handling the case:
                        struct ABC { typedef int DEF; };
                        x = ABC::DEF(expression);       // cast
                   but this works:
                        x = (ABC::DEF)(expression);     // cast
                 */

                /* Back up tokenizer to the identifier  */
                token_unget();
                tok.setSymbol(s);

                e = null;
if (!bColcol)
{
                if (funcsym_p && isclassmember(funcsym_p) &&
                    c1isbaseofc2(null,s,funcsym_p.Sscope))
                {
                    tclass = funcsym_p.Sscope.Stype;
                }
                if ((sthis = scope_search("this",SCTlocal)) !is null)
                {
                    /* Determine if there is an implied "this."        */
                    if (t1isbaseoft2(tclass,sthis.Stype.Tnext))
                    {
                        tclass = sthis.Stype.Tnext;
                        if (!pstate.STisaddr)
                        {
                            e = el_var(sthis);
                            e = el_unat(OPind,tclass,e);
                        }
                    }
                }
                if (pstate.STclasssym &&
                    c1isbaseofc2(null,tclass.Ttag,pstate.STclasssym))
                    tclass = pstate.STclasssym.Stype;
}
                e = dodot(e, tclass, cast(bool)bColcol);
                break;
            }
            else
            {
                pstate.STisaddr = 0;
                if (sc == SCtypedef || sc == SCenum)
                {
                    stoken();
                    if (tok.TKval == TKcolcol)
                    {
                        goto Lretry;
                    }

                    e = exp_simplecast(s.Stype);
                    break;
                }
                else // symbol is a variable
                {
                    stoken();
                    if (tok.TKval == TKcolcol)
                    {
                    Lretry:
                        token_unget();
                        token_setident(&s.Sident[0]);
                        s = scope_search(&s.Sident[0], SCTglobal | SCTnspace | SCTtempsym |
                            SCTtemparg | SCTmfunc | SCTlocal | SCTwith |
                            SCTclass | SCTparameter |
                            SCTcover | SCTcolcol);
                        goto L3;
                    }

                    param_t *ptal = null;
                    ubyte flags = 0;

                    if (s.Sflags & SFLvalue && s.Sclass == SCconst)       // if constant value
                    {   e = el_copytree(s.Svalue);
                        break;
                    }

                    if (s.Sclass == SCfuncalias &&
                        s.Sfunc.Falias.Sclass == SCfunctempl)
                        s = s.Sfunc.Falias;

                    // Look for function template arguments of the form:
                    //      func<type>(args)
                    // and process it into template-id
                    if (tok.TKval == TKlt && tyfunc(s.Stype.Tty))
                    {
                        Symbol *s2;

                        for (s2 = s; s2; s2 = s2.Sfunc.Foversym)
                        {
                            if (s2.Sclass == SCfunctempl)
                            {
                                s = s2;
                                pstate.STinarglist++;
                                ptal = template_gargs2(s);
                                pstate.STinarglist--;
                                stoken();
                                flags = PEFtemplate_id;
                                break;
                            }
                        }
                    }
                    // Look for function template arguments of the form:
                    //      func<>(args)
                    // and process it into template-id
                    else if (tok.TKval == TKlg && tyfunc(s.Stype.Tty))
                    {
                        Symbol *s2;

                        for (s2 = s; s2; s2 = s2.Sfunc.Foversym)
                        {
                            if (s2.Sclass == SCfunctempl)
                            {
                                s = s2;
                                ptal = null;
                                stoken();
                                flags = PEFtemplate_id;
                                break;
                            }
                        }
                    }

                    if (s.Stype.Tty & mTYimport &&
                        tyfunc(s.Stype.Tty))
                    {
                        // Defer Obj::_import() until after function overloading
                        e = el_calloc();
                        e.Eoper = OPvar;
                        e.EV.Vsym = s;
                        e.ET = s.Stype;
                        e.ET.Tcount++;
                    }
                    else
                    {
                        e = el_var(s);
                        //printf("s.Sscope = %p, funcsym_p = %p\n", s.Sscope, funcsym_p);
                        if (sytab[s.Sclass] & SCSS &&
                            s.Sscope &&    // BUG: need to find *all* the places where Sscope should be set
                            s.Sscope != funcsym_p)
                            cpperr(EM_nonlocal_auto, &s.Sident[0]);

                        if (pstate.STdefaultargumentexpression &&
                            (s.Sclass == SCparameter ||
                             (s.Sscope && s.Sscope == funcsym_p)))
                            cpperr(EM_nolocal_defargexp, &s.Sident[0]);
                    }

                    // Transform into template-id if it is one
                    e.EV.Vtal = ptal;
                    e.PEFflags |= flags;

                    t = e.ET;
                    while (s.Sclass == SCanon)
                        s = e.EV.Vsym = s.Sscope;
                    if (!(sytab[cast(int) s.Sclass] &
                            (init_staticctor ? SCSCT : SCEXP)) &&
                        !SymInline(s)&&
                        s.Sclass != SCfunctempl &&
                        s.Sclass != SCfuncalias)
                    {
                        // ident is not a variable
                        synerr(EM_not_variable,&s.Sident[0],init_staticctor ? "static ".ptr : "".ptr);
                        break;
                    }
                    if (tyref(t.Tty))              // if <reference to>
                    {   // convert (e) to (*e)
                        e = reftostar(e);
                    }
                }
            }
            break;
        case TKnum:
            e = el_longt(tstypes[tok.TKty],tok.Vllong);
            stoken();
            break;

        case TKtrue:
        case TKfalse:
            e = el_longt(tstypes[TYbool],(tok.TKval == TKtrue));
            stoken();
            break;

        static if (0)
        static if (TARGET_LINUX || TARGET_OSX || TARGET_FREEBSD || TARGET_OPENBSD || TARGET_SOLARIS)
        {
        case TKnull:
            e = el_longt(tspvoid,0);
            stoken();
            break;
        }

        case TKnullptr:
            e = el_longt(tstypes[TYnullptr], 0);
            e.Eoper = OPnullptr;
            stoken();
            break;

        case TK_inf:
            tok.Vdouble = INFINITY;
            goto L4;
        case TK_nan:
            tok.Vdouble = NAN;
            goto L4;
        case TK_nans:
            // Avoid converting NANS to NAN
            memcpy(&tok.Vdouble,(&nansarray[0]),DOUBLESIZE);
            goto L4;

        case TK_i:
            tok.Vfloat = 1.0f;
            t = tstypes[TYifloat];
            goto Lreal;

        case TKreal_f:  t = tstypes[TYfloat];    goto Lreal;
        L4:
        case TKreal_d:  t = tstypes[TYdouble];   goto Lreal;
        case TKreal_da: t = tstypes[TYdouble_alias];   goto Lreal;
        case TKreal_ld: t = tstypes[TYldouble];  goto Lreal;
        Lreal:
            e = el_calloc();
            e.Eoper = OPconst;
            e.ET = t;
            e.ET.Tcount++;
            // Avoid NANS problems
            memcpy(&e.EV.Vldouble,&tok.Vldouble,e.EV.Vldouble.sizeof);
            stoken();
            break;

        case TKstring:
        {   tym_t ty;
            e = el_calloc();
            e.Eoper = OPstring;
            e.EV.Vstring = combinestrings(&e.EV.Vstrlen, &ty);
            if (CPP)
            {   // C++98 says a string literal is a const pointer to char.
                t = type_alloc(mTYconst | ty);
                t = newpointer(t);
                t.Tmangle = mTYman_c;
                t.Tcount++;
            }
            else
            {
                // create pointer to char
                // string is in near static data
                t = type_pointer(tstypes[ty]);
            }
            e.ET = t;
            break;
        }

        case TKasm:
            stoken();
            if (tok.TKval == TKlpar)    // support anachronistic ASM style
                goto Lemit;             // which is replaced by __emit__
            else
            {   // Mike: inline assembly goes here
                goto Lemit;
            }
        case TK__emit__:
            stoken();
        Lemit:
            chktok(TKlpar,EM_lpar);
            {
            alias char emit_t;
            int imax = 0;
            int i = 0;
            emit_t *p = null;
            while (tok.TKval != TKrpar)
            {
                elem *ea;
                tym_t ty;
                int size;

                // TMAXSIZE is the largest size a constant can be
                if (i + TMAXSIZE > imax)
                {   imax += 16;
                    p = cast(emit_t *) mem_realloc(p,imax * emit_t.sizeof);
                }
                ea = poptelem(assign_exp());
                if (ea.Eoper != OPconst)
                    synerr(EM_num);                     // number expected
                else
                {
                    ty = tybasic(ea.ET.Tty);
                    size = _tysize[ty];
                    debug assert(size <= TMAXSIZE);
                    if (size == -1)
                        synerr(EM_integral);            // integral expression expected
                    else
                    {
                        // If it's an int that will fit in a byte
                        if (ty == TYint && el_tolong(ea) <= 255)
                            size = 1;
                        // BUG: depends on bytes being in order
                        memcpy(p + i,&ea.EV,size);
                        i += size;
                    }
                }
                el_free(ea);
                if (tok.TKval != TKcomma)
                    break;
                stoken();
            }
            e = el_calloc();
            e.Eoper = OPasm;
            el_settype(e,tstypes[TYlong]);
            // Make it volatile to skip various optimizations
            type_setty(&e.ET,TYlong | mTYvolatile);
            e.EV.Vstrlen = i * emit_t.sizeof;
            e.EV.Vstring = cast(char *) mem_realloc(p,i * emit_t.sizeof);
                                        // Will make permanent copy and free temp
            }
            stoken();
            break;
        default:
            synerr(EM_exp);             // primary_exp expected
        err:
            stoken();
        err2:
            e = el_longt(tserr,0);
            break;
    } /* switch */
done:
    //printf("-primary_exp()\n");
    return prim_post(e);
}


/************************************
 * Take care of stuff forming the end of the primary_exp.
 */

/*private*/ elem *prim_post(elem *e)
{
    elem *eo;
    int op;

    while (1)
    {
        switch (tok.TKval)
        {
            case TKlpar:
                if (CPP)
                {
                    const save = pstate.STinarglist;
                    pstate.STinarglist = 0;
                    e = dofunc(e);
                    pstate.STinarglist = save;
                }
                else
                    e = dofunc(e);
                break;
            case TKdot:
                stoken();
                if (CPP && tok.TKval == TK_istype)
                {
                    stoken();
                    chktok(TKlpar, EM_lpar);

                    type *typ_spec;
                    type_specifier(&typ_spec);
                    type* t = declar_abstract(typ_spec);
                    fixdeclar(t);

                    e = cpp_istype(e, t);

                    type_free(t);
                    type_free(typ_spec);
                    chktok(TKrpar,EM_rpar);
                }
                else
                {
                    e = dodot(e, e.ET, false);
                }
                continue;
            case TKlbra:
                if (CPP)
                {   const save = pstate.STinarglist;
                    pstate.STinarglist = 0;
                    e = doarray(e);
                    pstate.STinarglist = save;
                }
                else
                    e = doarray(e);
                continue;               // else tokens get out of sync
            case TKarrow:               // '.'
                stoken();
                e = doarrow(e);
                continue;
            case TKplpl:
            case TKmimi:
                if (CPP)
                {
                    // Try operator++(int) with an argument of 0
                    op = (tok.TKval == TKplpl) ? OPpostinc : OPpostdec;
                    e = el_bint(op, e.ET, e, el_longt(tstypes[TYint],0));
                    if ((eo = cpp_opfunc(e)) !is null)
                        e = eo;
                    else
                    {   el_free(e.EV.E2);         // dump second arg
                        e.EV.E2 = null;

                        // Try again with operator++() to support old C++ 2.0 code
                        e.Eoper = (op == OPpostinc) ? OPpreinc : OPpredec;
                        if (!config.ansi_c && (eo = cpp_opfunc(e)) !is null)
                        {   warerr(WM.WM_obsolete_inc);
                            e = eo;
                        }
                        else
                        {   e.Eoper = cast(ubyte)op;      // back to original operator
                            getinc(e);  // get size of increment
                            chkassign(e);
                        }
                    }
                }
                else
                {
                    e = el_bint(((tok.TKval == TKplpl) ? OPpostinc : OPpostdec),
                            e.ET, e, cast(elem *) null);
                    getinc(e);                  // get size of increment
                    chkassign(e);
                }
                stoken();
                continue;
            default:
                return e;
        }
        stoken();
    }
}


/****************************
 * Returns a bit mask:
 */

enum
{
    TRAITS_STRUCT           = 1,       // is a class/struct/union
    TRAITS_DTOR             = 2,       // has a destructor
    TRAITS_VIRTUAL_DTOR     = 4,       // has a virtual destructor
    TRAITS_TRIVIAL_DTOR     = 8,       // has a trivial destructor
    TRAITS_TRIVIAL_CTOR     = 0x10,    // has a trivial constructor
    TRAITS_TRIVIAL_CPCT     = 0x20,    // has a trivial copy constructor
    TRAITS_TRIVIAL_OPEQ     = 0x40,    // has a trivial assignment overload
    TRAITS_NOTHROW_CTOR     = 0x80,    // has a nothrow constructor
    TRAITS_NOTHROW_CPCT     = 0x100,   // has a nothrow copy constructor
    TRAITS_NOTHROW_OPEQ     = 0x200,   // has a nothrow assignment overload
    TRAITS_UNION            = 0x400,   // is a union
    TRAITS_POD              = 0x800,   // is POD (C++98 9-4)
    TRAITS_EMPTY            = 0x1000,  // is an empty union or struct
}


/*private*/ uint exp_typeinfo(type *t)
{
    uint typeinfo = 0;
    type_debug(t);
    if (type_struct(t))
    {   Classsym *s = t.Ttag;
        struct_t *st = s.Sstruct;
        Symbol *sdtor;

        typeinfo |= TRAITS_STRUCT;

        sdtor = st.Sdtor;
        if (sdtor)
        {   symbol_debug(sdtor);
            typeinfo |= TRAITS_DTOR;            // has destructor

            if (sdtor.Sfunc.Fflags & Fvirtual)
                typeinfo |= TRAITS_VIRTUAL_DTOR; // virtual destructor
        }
        else
            typeinfo |= TRAITS_TRIVIAL_DTOR;    // trivial destructor

        if (s.Sstruct.Sflags & STRunion)
            typeinfo |= TRAITS_UNION;           // is a union
        if (s.Sstruct.Sflags & STR0size)
            typeinfo |= TRAITS_EMPTY;           // empty union or struct

        if (!(st.Sflags & STRanyctor))
            // trivial and nothrow constructor
            typeinfo |= TRAITS_TRIVIAL_CTOR | TRAITS_NOTHROW_CTOR;

        template_instantiate_forward(s);
        n2_createcopyctor(s, 0);
        if (st.Scpct && st.Scpct.Sfunc.Fflags & Fbitcopy)
            // trivial and nothrow copy constructor
            typeinfo |= TRAITS_TRIVIAL_CPCT | TRAITS_NOTHROW_CPCT;

        n2_createopeq(s, 0);
        if (st.Sopeq && st.Sopeq.Sfunc.Fflags & Fbitcopy)
            // trivial and nothrow assignment overload
            typeinfo |= TRAITS_TRIVIAL_OPEQ | TRAITS_NOTHROW_OPEQ;

        /* Nothrow constructor
         */
        if (st.Sctor && st.Sctor.Stype.Tflags & TFemptyexc)
            typeinfo |= TRAITS_NOTHROW_CTOR;

        /* Nothrow copy constructor
         */
        if (st.Scpct && st.Scpct.Stype.Tflags & TFemptyexc)
            typeinfo |= TRAITS_NOTHROW_CPCT;

        /* Nothrow assignment overload
         */
        if (st.Sopeq && st.Sopeq.Stype.Tflags & TFemptyexc)
            typeinfo |= TRAITS_NOTHROW_OPEQ;

        /* C++98 9-4 defines POD:
         * A POD-struct is an aggregate class that has no non-static data members
         * of type pointer to member, non-POD-struct, non-POD-union (or array of
         * such types) or reference, and has no user-defined copy assignment
         * operator and no user-defined destructor. Similarly, a POD-union
         * is an aggregate union that has no non-static data members of type
         * pointer to member, non-POD-struct, non-POD-union (or array of such
         * types) or reference, and has no user-defined copy assignment operator
         * and no user-defined destructor. A POD class is a class that is either
         * a POD-struct or a POD-union.
         * C++98 8.5.1 defines aggregate classes:
         * An aggregate is an array or a class (clause 9) with no user-declared
         * constructors (12.1), no private or protected non-static
         * data members (clause 11), no base classes (clause 10), and no virtual
         * functions (10.3).
         */
        if (st.Sbase)                          // if base classes
            goto Lnotpod;
        if (st.Sflags & STRanyctor)            // if any user-declared constructors
            goto Lnotpod;
        if (st.Svptr)                          // if any virtual functions
            goto Lnotpod;
        if (st.Sdtor)                          // if any user defined destructor
            goto Lnotpod;
        if (st.Sopeq && !(st.Sopeq.Sfunc.Fflags & Fbitcopy))
            goto Lnotpod;                       // if user-defined copy assignment operator
        for (symlist_t sl = st.Sfldlst; sl; sl = list_next(sl))
        {   Symbol *sm = list_symbol(sl);

            switch (sm.Sclass)
            {
                case SCglobal:
                    continue;                   // skip static members

                case SCmember:
                case SCfield:                   // data members
                    if ((s.Sflags & SFLpmask) == SFLprivate ||
                        (s.Sflags & SFLpmask) == SFLprotected)
                        goto Lnotpod;           // no private/protected members
                    break;

                default:
                    break;
            }
            type *tm = type_arrayroot(sm.Stype);
            tym_t tym = tybasic(tm.Tty);

            if (tyref(tym))
                goto Lnotpod;                   // no reference types
            if (tym == TYstruct && tm != sm.Stype)
            {
                if (!(exp_typeinfo(tm) & TRAITS_POD))
                    goto Lnotpod;               // no non-POD members
            }
            else if (tym == TYmemptr)
                goto Lnotpod;                   // no pointers to members
        }
        typeinfo |= TRAITS_POD;                 // POD

    Lnotpod:
        ;

    }
    else if (tyref(t.Tty))
    {
        typeinfo |= TRAITS_TRIVIAL_DTOR |
                //  TRAITS_TRIVIAL_CTOR |
                //  TRAITS_TRIVIAL_CPCT |
                //  TRAITS_TRIVIAL_OPEQ |
                //  TRAITS_NOTHROW_CTOR |
                //  TRAITS_NOTHROW_CPCT |
                //  TRAITS_NOTHROW_OPEQ |
                //  TRAITS_POD          |
                    0;
    }
    else
    {
        typeinfo |= TRAITS_TRIVIAL_DTOR |
                    TRAITS_TRIVIAL_CTOR |
                    TRAITS_TRIVIAL_CPCT |
                    TRAITS_TRIVIAL_OPEQ |
                    TRAITS_NOTHROW_CTOR |
                    TRAITS_NOTHROW_CPCT |
                    TRAITS_NOTHROW_OPEQ |
                    TRAITS_POD;
    }
    if (t.Tty & (mTYconst | mTYvolatile))
        typeinfo &= ~(TRAITS_NOTHROW_OPEQ | TRAITS_TRIVIAL_OPEQ);
    if (t.Tty & mTYvolatile)
        typeinfo &= ~(TRAITS_NOTHROW_CPCT | TRAITS_TRIVIAL_CPCT);
    return typeinfo;
}


/****************************
 * Parse and return elem tree for: sizeof expressions.
 *      sizeof
 *      typeid
 *      __typeinfo
 *      __typemask
 */

elem *exp_sizeof(int tk)
{
    tym_t tym;
    elem *e;
    elem *e1;
    type *typ_spec;
    const insave = pstate.STinsizeof;        // nest things properly
    const inarglistsave = pstate.STinarglist;
    int typeinfo;
    uint typemask;
    SYMIDX marksi;

    pstate.STinarglist = 0;         // sizeof protects > and >>
    marksi = globsym.length;
    stoken();
    if (tok.TKval != TKlpar)
    {
        pstate.STinsizeof = true;
        e1 = una_exp();
        pstate.STinsizeof = insave;
        goto sizexp;
    }
    stoken();
    if (CPP && isexpression())
        goto isexp;
    if (type_specifier(&typ_spec))        // if type_name
    {   type* t,t1;

        t = declar_abstract(typ_spec);      // read abstract_declarator
        fixdeclar(t);               /* fix declarator                */
        chktok(TKrpar,EM_rpar);     // ')' ends it
        t1 = t;
        if (CPP && tk == TK_typeinfo)
            typeinfo = exp_typeinfo(t1);
        while (tyref(t1.Tty))
            t1 = t1.Tnext;         /* skip over reference types    */
        typemask = tybasic(t1.Tty);
        if (tk == TKtypeid)
            e = rtti_typeid(t,null);
        else
        {   e = el_typesize(t1);
            type_free(t);
        }
        type_free(typ_spec);
    }
    else                            /* sizeof ( expression )         */
    {
        type_free(typ_spec);
isexp:
        pstate.STinsizeof = true;
        e1 = expression();
        pstate.STinsizeof = insave;
        chktok(TKrpar,EM_rpar);     // closing ')'

sizexp:
        if (CPP && tk == TK_typeinfo)
            typeinfo = exp_typeinfo(e1.ET);
        typemask = tybasic(e1.ET.Tty);
        if (tk == TKtypeid)
            e = rtti_typeid(null,e1);
        else
        {
            switch (e1.Eoper)
            {   case OPstring:
                    // Use string length instead of pointer
                    e = el_longt(tstypes[TYint],e1.EV.Vstrlen);
                    break;
                case OPbit:
                    synerr(EM_sizeof_bitfield);     // can't take size of bit field
                    goto default;
                default:
                    e = el_typesize(e1.ET);        // size of expression
                    break;
            }
            if (CPP)
            {   SYMIDX si;

                for (si = marksi; si < globsym.length; si++)
                    globsym[si].Sflags |= SFLnodtor;
            }
            el_free(e1);
        }
    }
    if (tk == TK_typeinfo)
    {   el_free(e);
        e = el_longt(tstypes[TYint],typeinfo);
    }
    else if (tk == TK_typemask)
    {   el_free(e);
        e = el_longt(tstypes[TYulong],typemask);
    }
    pstate.STinarglist = inarglistsave;
    return e;
}


/*****************************
 * Parse and return new expression.
 *      ["::"] "new" [new-placement] new-type-id [new-initializer]
 *      ["::"] "new" [new-placement] "(" type-id ")" [new-initializer]
 *
 *      new-placement:
 *              "(" expression-list ")"
 * Input:
 *      global          1 if ::new, 0 otherwise
 *      tok.TKval       on TKnew
 */

/*private*/ elem *exp_new(int global)
{
    type *t;
    type *tret;
    list_t arglist;
    list_t placelist;
    elem *e;
    elem *et;
    elem *enelems;
    elem *esize;
    type *typ_spec;
    int array;
    tym_t ty;

    arglist = null;
    placelist = null;
    enelems = null;

    switch (stoken())
    {
        case TK_far:
            if (LARGEDATA)
                stoken();
            break;
        case TK_near:
            if (!LARGEDATA)
                stoken();
            break;
        default:
            break;
    }

    //printf("exp_new(global = %d)\n", global);
    if (tok.TKval == TKlpar)
    {   // Is it [new-placement] or "(" type-id ")" ?
        stoken();
        if (!isexpression())            /* if it's a declaration        */
        {
            //printf("\tdeclaration .. type-id\n");
            goto L1;                    // then it's a type-id
        }
        //printf("\texpression .. new-placement\n");
        getarglist(&placelist);         // get new-placement
        chktok(TKrpar,EM_rpar);
    }

    if (tok.TKval == TKlpar)
    {
        stoken();
    L1:
        // Parse type-id
        type_specifier(&typ_spec);      // get type-specifier-seq
        t = declar_abstract(typ_spec);  // get abstract-declarator
        chktok(TKrpar,EM_rpar);         // ')' ends it
    }
    else
    {   // Parse new-type-id
        const levelsave = level;
        level = 0;                      // trick type_specifier
        pstate.STnewtypeid++;
        type_specifier(&typ_spec);      // get type-specifier-seq
        pstate.STnewtypeid--;
        level = levelsave;

        t = new_declarator(typ_spec);   // get new-declarator
    }

    fixdeclar(t);
    type_free(typ_spec);
    tret = t;
    if (tok.TKval == TKlpar /*&& tybasic(t.Tty) == TYstruct*/)
    {   stoken();
        getarglist(&arglist);       /* get initializer               */
        chktok(TKrpar,EM_rpar);

        if (arglist == null && tybasic(t.Tty) != TYstruct)
        {
            e = el_longt(t, 0);     // initialize with 0
            list_append(&arglist, e);
        }
    }
    else if (tok.TKval == TKlbra)   /* get [dim1][dim2]...           */
    {
        type *ta;
        type **pt;
        int first = 1;

        pt = &tret;
        do
        {
            stoken();
            elem* ex = poptelem(expression());

            /* CPP98 5.3.4-6
             * "Every constant-expression in a direct-new-declarator
             * shall be an integral constant expression (5.19) and
             * evaluate to a strictly positive value.
             * The expression in a direct-new-declarator shall have
             * integral type (3.9.1) with a non-negative value."
             */
            if (!tyintegral(ex.ET.Tty))
                synerr(EM_integral);        // integer expression expected
            if (!first && ex.Eoper != OPconst)
                synerr(EM_num);             // constant integer expression expected
            first = 0;

            chktok(TKrbra,EM_rbra);
            // Link in ta just before t
            ta = type_alloc(TYarray);
            elem_debug(ex);
            ex = _cast(ex,tssize);
            if (ex.Eoper == OPconst)
                ta.Tdim = cast(uint)el_tolong(ex);
            else
                ta.Tflags |= TFsizeunknown;

            // Link in ta just before t
            ta.Tnext = t;
            *pt = ta;
            pt = &(ta.Tnext);

            enelems = enelems ? el_bint(OPmul,ex.ET,enelems,ex) : ex;
            enelems = poptelem(enelems);
            if (enelems.Eoper == OPconst && !(t.Tflags & TFsizeunknown))
                type_chksize(cast(uint)(el_tolong(enelems) * type_size(t)));
        } while (tok.TKval == TKlbra);

        for (ta = tret.Tnext; 1; ta = ta.Tnext)
        {
            ta.Tcount++;
            if (ta == t)
                break;
        }
    }

    // Compute type that is returned from new
    if (tybasic(tret.Tty) == TYarray)
    {   type *t1;

        if (tybasic(t.Tty) == TYarray)
        {   elem *en;

            if (t != tret)
                t.Tcount--;
            if (!enelems)
                enelems = el_longt(tssize, 1);
            do
            {
                if (t.Tflags & TFvla)
                {   assert(t.Tel);
                    en = el_copytree(t.Tel);
                }
                else
                    en = el_longt(tssize, t.Tdim);
                enelems = el_bint(OPmul, enelems.ET, enelems, en);
                t = t.Tnext;
            } while (tybasic(t.Tty) == TYarray);
            t.Tcount++;
        }
        else
            assert(enelems !is null);

        array = 2;                      /* arrays use global ::operator new() */
        /* <array of> actually returns <pointer to>     */
        t1 = tret;
        tret = newpointer(tret.Tnext);
        t1.Tcount++;
        type_free(t1);
    }
    else
    {   array = 0;
        assert(enelems == null);
        tret = newpointer(tret);
    }

    // At this point, we have:
    //  t:      Tcount incremented
    //  tret:   Tcount not incremented

    chknoabstract(t);
    et = el_typesize(t);
    elem_debug(et);
    esize = _cast(et,tssize);

    // esize is the size in bytes of whatever we're new'ing
    if (enelems)
        esize = el_bint(OPmul,tssize,enelems,esize);

    // If initializing a class and the class has a constructor
    e = null;
    ty = tybasic(t.Tty);
    if (ty == TYstruct)
        template_instantiate_forward(t.Ttag);
    if (ty == TYstruct &&
        (t.Ttag.Sstruct.Sflags & STRanyctor ||
         (array && t.Ttag.Sstruct.Sdtor)
        )
       )
    {
        /* An optimization could be done here: if there is no
           tclass::operator new(), then cpp_constructor() could be used
           (it will call ::new)
         */
        enelems = el_copytree(enelems);         /* in case cpp_new changes it */
        if (placelist || !array)
        {
            elem *eptr;
            Symbol *p;

            if (array)
                // esize <= esize + sizeof(unsigned)
                esize = el_bint(OPadd,tssize,esize,el_longt(tssize,tysize(TYuint)));

            eptr = cpp_new(global | array,funcsym_p,esize,placelist,tret);
            placelist = null;                   /* it's gone now        */
            p = symbol_genauto(tret);

            /* Build: (((p = eptr) && (p = ctor(p))), p)        */

            e = cpp_constructor(el_var(p),t,arglist,enelems,null,(array ? 1 : 0) | 2 | 8);
            if (e)
            {
                eptr = el_bint(OPeq,tret,el_var(p),eptr);
version (all) // For some reason this was disabled after 7.5
{
                if (config.flags3 & CFG3eh && !array &&
                    !eecontext.EEin &&
                    pointertype == t.Ttag.Sstruct.ptrtype)
                {   Symbol *sdelete;
                    elem *edelete;
                    Symbol *sctor;

                    if (t.Ttag.Sstruct.Sflags & STRgenctor0)
                        n2_creatector(t);
                    sctor = t.Ttag.Sstruct.Sctor;
                    edelete = cpp_delete(global | array,sctor,el_var(p),el_copytree(esize));

                    // Only do it if ambient memory model
                    if (tyfarfunc(edelete.EV.E1.ET.Tty) ? LARGECODE : !LARGECODE)
                    {
                        //sdelete = scope_search(cpp_name_delete,SCTglobal);
                        if (edelete.EV.E1.Eoper == OPvar)
                        {   sdelete = edelete.EV.E1.EV.Vsym;
                            sdelete = n2_delete(t.Ttag,sdelete,cast(uint)el_tolong(esize));
                            elem *ex = el_ctor(el_var(p),el_var(p),sdelete);
                            e = el_combine(ex, e);
                            e = el_combine(e,el_dtor(el_var(p),el_var(p)));
                        }
                    }
                    el_free(edelete);

                    e = el_bint(OPeq,tret,el_var(p),e);
                    e = el_bint(OPandand,tslogical,eptr,e);
                    e = el_bint(OPcomma,tret,e,el_var(p));
                }
                else
                {
                    e = el_bint(OPeq,tret,el_var(p),e);
                    e = el_bint(OPandand,tslogical,eptr,e);
                    e = el_bint(OPcomma,tret,e,el_var(p));
                }
}
            }
            else
            {   // Couldn't find ctor, syntax error already issued
                e = eptr;
            }
        }
        else
        {   // This causes the default operator new() to be called

            elem* eptr = el_longt(tret,0);            // build (tret *) 0
            //e = cpp_constructor(eptr,t,arglist,enelems,null,(array ? 1 : 0) | 2 | 8);
            e = cpp_constructor(eptr,t,arglist,enelems,null,(array ? 1 : 8) | 2);
            if (e)
            {
                el_free(esize);
            }
            else                                // no ctor found
            {   // Syntax error already issued
                e = el_longt(tserr,0);
            }

            // Do access check on operator new()
            {
                Symbol* snew = cpp_findmember(t.Ttag,cpp_name_new.ptr,false);
                if (snew)
                    cpp_memberaccess(snew,funcsym_p,t.Ttag);
            }
        }
    }
    else
    {   // Doing a simple allocation. Build the elem:
        //      (t *) operator new((targ_size_t) nelems * sizelem)

        e = cpp_new(global | array,funcsym_p,esize,placelist,tret);

        if (arglist)
        {
            if (list_nitems(arglist) != 1 ||
                (!tyscalar(ty) && ty != TYstruct))
            {
                cpperr(EM_vector_init); // no initializer for vector ctor
                list_free(&arglist,cast(list_free_fp)&el_free);
            }
            else
            {
                /* Generate:  ((p = e) && (*p = arglist)),p     */
                elem *arg = list_elem(arglist);
                type *targ;
                Symbol *p;
                int op;

                p = symbol_genauto(tret);

                assert(arg);
                elem_debug(arg);
                targ = tret.Tnext;
                type_debug(targ);
                arg = _cast(arg,targ);
                op = (ty == TYstruct) ? OPstreq : OPeq;
                arg = el_bint(op,targ,el_unat(OPind,targ,el_var(p)),arg);

                e = el_bint(OPeq,tret,el_var(p),e);
                e = el_bint(OPandand,tslogical,e,arg);
                e = el_bint(OPcomma,tret,e,el_var(p));

                list_free(&arglist,FPNULL);
            }
        }
    }

    type_free(t);
    return e;
}


/*****************************
 * Parse and return delete expression.
 *      delete cast-expression
 *      delete [] cast-expression
 *      delete [expression] cast-expression     (obsolete)
 * Input:
 *      global  1 if ::delete
 *      tok.TKval       on TKdelete
 */

/*private*/ elem *exp_delete(int global)
{
    elem *enelems;              // expression inside the []
    elem *eptr;                 // expression for pointer to be delete'd
    elem *ep;
    elem *e;
    Symbol *sdtor;
    Symbol *sadelete = null;
    type *tclass;
    tym_t ty;
    int flag = DTORmostderived | DTORnoeh;      // flag for call to destructor
    int sepnewdel = 1;

    //printf("exp_delete(global = %d)\n", global);

    enelems = null;
    if (stoken() == TKlbra)
    {
        flag |= DTORvecdel;     /* doing a vector destructor            */
        stoken();
        if (tok.TKval == TKrbra)        // if delete []
            stoken();
        else
        {
            while (1)
            {
                elem* e2 = expression();
                if (!tyintegral(e2.ET.Tty))
                    synerr(EM_integral);        // integer expression expected
                chktok(TKrbra,EM_rbra);
                e = _cast(e2,tstypes[TYuint]);
                enelems = enelems ? el_bint(OPmul,e2.ET,enelems,e2) : e2;
                elem_debug(enelems);
                if (tok.TKval != TKlbra)
                    break;
                stoken();
            }
            warerr(WM.WM_obsolete_del);    // delete [expr] is an obsolete feature
            el_free(enelems);
            enelems = null;
        }
    }
    eptr = arraytoptr(una_exp());
    /* Can only delete pointers */
    if (tybasic(eptr.ET.Tty) != pointertype
                                            )
    {
        cpperr(EM_del_ptrs);    // can only delete pointers
        el_free(enelems);
        return eptr;
    }

    // Deleting const pointers allowed by C++98 12.4-2
    version (none)
    {
        if (eptr.ET.Tnext.Tty & mTYconst)
            cpperr(EM_del_ptr_const);       // cannot delete pointer to const
    }

    // If no destructor, then dump enelems
    sdtor = null;
    tclass = eptr.ET.Tnext;
    ty = tybasic(tclass.Tty);
    if (ty == TYstruct)
    {   sdtor = tclass.Ttag.Sstruct.Sdtor;
        if (sdtor && sepnewdel && sdtor.Sfunc.Fflags & Fvirtual)
            sepnewdel = 0;

        // Look for local operator delete[]
        sadelete = cpp_findmember(tclass.Ttag,cpp_name_adelete.ptr,false);
        //printf("sadelete = %p\n", sadelete);
    }

    /* An optimization could be done here: if there is no
        tclass::operator delete(), then cpp_destructor() could be used
        (it will call ::delete)
     */
    /* arrays always use global ::operator delete() [NOT true anymore] */
    ep = eptr;
    if (ty == TYstruct &&
        (sdtor && !global && !sepnewdel && !(flag & DTORvecdel) ||
         (!sadelete && flag & DTORvecdel && (sdtor || tclass.Ttag.Sstruct.Sflags & STRanyctor))
        )
       )
    {
        flag |= DTORfree | DTORvirtual;

        // If destructor is virtual
        if (sdtor && sdtor.Sfunc.Fflags & Fvirtual)
        {   // Do not call virtual destructor if eptr is null
            // e1 <= (eptr && _dtor(eptr))

            eptr = el_same(&ep);
            e = el_bint(OPandand,tslogical,ep,
                    cpp_destructor(tclass,eptr,enelems,flag));
        }
        else
            e = cpp_destructor(tclass,eptr,enelems,flag);

        // Do access check if local operator delete()
        if (!(global | (flag & DTORvecdel)))
        {
            Symbol* sdelete = cpp_findmember(tclass.Ttag,cpp_name_delete.ptr,false);
            if (sdelete)
                cpp_memberaccess(sdelete,funcsym_p,tclass.Ttag);
        }
    }
    else
    {   /* Call operator delete(eptr)           */
        elem* esize;
        elem* e1,e2;
        int testnull;

        if (!(flag & DTORvecdel) && ty == TYarray)
            cpperr(EM_adel_array);                      // must use delete [] for arrays

        if (ty == TYvoid)
            esize = el_longt(tstypes[TYvoid],1);
        else
        {   type_size(tclass);          // flush out fwd references to class
            esize = el_typesize(tclass);
        }

        e1 = null;
        testnull = 0;
        if (sdtor)
        {   elem *en = enelems;

            testnull = sepnewdel;
            if (1)
            {   // Always copy to temp so that if someone took the address
                // of 'this', and the destructor modifies it, we still
                // call operator delete() on the original 'this'
                ep = exp2_copytotemp(ep);
                eptr = el_copytree(ep.EV.E2);
            }
            else
            {
                eptr = el_copytree(ep);
            }
            enelems = el_same(&en);
            /* If destructor is virtual */
            if (!testnull && (sdtor.Sfunc.Fflags & Fvirtual
                || flag & DTORvecdel
               ))
            {   /* Do not call virtual destructor if eptr is null       */
                testnull = 1;
            }

            e1 = cpp_destructor(tclass,(testnull ? el_copytree(eptr) : ep),
                                en,flag | DTORvirtual);

            if (flag & DTORvecdel)
            {   /* Pick up number of elements from int immediately
                   preceding eptr.
                        enelems = *(eptr - sizeof(TYint))
                 */

                eptr = el_bint(OPmin,eptr.ET,eptr,el_longt(tstypes[TYint],tysize(TYint)));
                enelems = el_unat(OPind,tstypes[TYint],el_copytree(eptr));
                esize = el_bint(OPmul,enelems.ET,esize,enelems);
            }
        }

        e2 = cpp_delete(global | ((flag & DTORvecdel) ? 2 : 0),funcsym_p,eptr,esize);
        e = el_combine(e1,e2);

        if (testnull)
            /* e <== (ep && e)  */
            e = el_bint(OPandand,tslogical,ep,e);
    }
    return e;
}


/**************************
 * Parse and return elem for:
 *      simple_type_name (expression_list)
 */

elem *exp_simplecast(type *t)
{   elem *e;

    //printf("exp_simplecast()\n");
    chktok(TKlpar,EM_lpar2,"simple type name");
    if (tybasic(t.Tty) == TYstruct) // && t.Ttag.Sstruct.Sctor)
    {
        list_t arglist;
        getarglist(&arglist);
        e = cpp_initctor(t,arglist);
    }
    else if (tok.TKval == TKrpar)
    {   // ARM 5.2.3: Unspecified value of specified type
        e = el_longt(t,0);              // make unspecified value 0
    }
    else
        e = _cast(expression(),t);
    chktok(TKrpar,EM_rpar);             // ')' ends it
    return e;
}

}
