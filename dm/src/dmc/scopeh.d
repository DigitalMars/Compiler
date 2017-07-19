/**
 * Implementation of the
 * $(LINK2 http://www.digitalmars.com/download/freecompiler.html, Digital Mars C/C++ Compiler).
 *
 * Copyright:   Copyright (c) 1993-1998 by Symantec, All Rights Reserved
 *              Copyright (c) 2000-2017 by Digital Mars, All Rights Reserved
 * Authors:     $(LINK2 http://www.digitalmars.com, Walter Bright)
 * License:     Distributed under the Boost Software License, Version 1.0.
 *              http://www.boost.org/LICENSE_1_0.txt
 * Source:      https://github.com/DigitalMars/Compiler/blob/master/dm/src/dmc/scopeh.d
 */

version (SPP)
{
}
else
{

import ddmd.backend.cdef;
import ddmd.backend.cc;

import tk.dlist;

extern (C++):

alias scope_fp = Symbol *function(const(char)* id,void *root);

enum
{
    SCTglobal =       1,   // global table
    SCTlocal =        2,   // local table
    SCTlabel =        4,   // statement labels
    SCTclass =        8,   // class
    SCTwith =      0x10,   // with scope
    SCTtempsym =   0x20,   // template symbol table
    SCTtemparg =   0x40,   // template argument table
    SCTmfunc =     0x80,   // member function scope (like SCTclass,
                           // but with implied "this" added)
    SCTtag =      0x100,   // tag symbol
    SCTglobaltag =0x200,   // global tag symbol
    SCTnspace =   0x400,   // namespace
    SCTparameter =0x800,   // function prototype
    SCTcglobal = 0x1000,   // extern "C" globals

    // Modifiers:
    SCTcover =       0x2000,  // pick Scover if there is one
    SCTnoalias =     0x4000,  // don't do SCalias substitution
    SCTcolcol =      0x8000,  // ignore symbols that are not
                              // class-names or namespace-names
    SCTjoin =        0x10000, // don't allow redefinition of names
                              // that appear in enclosing scope
}

struct Scope
{
    debug ushort id;
    enum IDscope = 0xDEAF;
    //void scope_debug(Scope* s) { assert(s.id == IDscope); }

    Scope* next;               // enclosing scope
    void *root;                // root of Symbol table
    uint sctype;               // scope type (SCTxxxxx)

    scope_fp fpsearch;          // search function
    Scope *using_scope;         // named during using-directive
    list_t using_list;          // cleanup list

    symlist_t friends;          // SCTlocal: hidden friend class symbols

    static void setScopeEnd(Scope *s_end);
    static int inTemplate();
    Symbol *findReal(Symbol *s, uint sct);
    Symbol *checkSequence(Symbol *s);
}

extern __gshared Scope *scope_end;        // pointer to innermost scope

void scope_print();
Scope *scope_find(uint sct);
Nspacesym *scope_inNamespace();
Symbol *scope_search(const(char) *id,uint sct);
Symbol *scope_search_correct(const(char) *id,uint sct);
Symbol *scope_search2(const(char) *id,uint sct);
Symbol *scope_searchx(const(char) *id,uint sct,Scope **psc);
Symbol *scope_searchinner(const(char) *id,uint sct);
Symbol *scope_searchouter(const(char) *id,uint sct,Scope **psc);
Symbol *scope_define(const(char) *id,uint sct, enum_SC sclass);
Symbol *scope_add(Symbol *s, uint sct );
Symbol *scope_addx(Symbol *s,Scope *sc);
void scope_pushclass(Classsym *stag);
void scope_push_symbol(Symbol *s);
void scope_push(void *root,scope_fp fpsearch,int sctype);
//#if TX86
void *scope_pop();
//#else
//Symbol *scope_pop();
//#endif
void scope_using(Nspacesym *sn,scope_fp fpsearch,int sctype,Scope *sce);
list_t scope_getList(Symbol *s);
int scope_pushEnclosing(Symbol *s);
void scope_unwind(int nscopes);
void scope_term();

}
