/**
 * Implementation of the
 * $(LINK2 http://www.digitalmars.com/download/freecompiler.html, Digital Mars C/C++ Compiler).
 *
 * Copyright:   Copyright (c) 1993-1998 by Symantec, All Rights Reserved
 *              Copyright (c) 2000-2017 by Digital Mars, All Rights Reserved
 * Authors:     $(LINK2 http://www.digitalmars.com, Walter Bright)
 * License:     Distributed under the Boost Software License, Version 1.0.
 *              http://www.boost.org/LICENSE_1_0.txt
 * Source:      https://github.com/DigitalMars/Compiler/blob/master/dm/src/dmc/scope.h
 */

#if !SCOPE_H
#define SCOPE_H 1

#if !SPP

typedef symbol * (*scope_fp)(const char *id,void *root);

struct Scope
{
#ifdef DEBUG
    unsigned short      id;
#define IDscope 0xDEAF
#define scope_debug(s) assert((s)->id == IDscope)
#else
#define scope_debug(s)
#endif

    struct Scope *next;         // enclosing scope
    void *root;                 // root of symbol table
    unsigned sctype;            // scope type
#       define SCTglobal         1   // global table
#       define SCTlocal          2   // local table
#       define SCTlabel          4   // statement labels
#       define SCTclass          8   // class
#       define SCTwith        0x10   // with scope
#       define SCTtempsym     0x20   // template symbol table
#       define SCTtemparg     0x40   // template argument table
#       define SCTmfunc       0x80   // member function scope (like SCTclass,
                                     // but with implied "this" added)
#       define SCTtag        0x100   // tag symbol
#       define SCTglobaltag  0x200   // global tag symbol
#       define SCTnspace     0x400   // namespace
#       define SCTparameter  0x800   // function prototype
#       define SCTcglobal   0x1000   // extern "C" globals

// Modifiers:
#       define SCTcover         0x2000  // pick Scover if there is one
#       define SCTnoalias       0x4000  // don't do SCalias substitution
#       define SCTcolcol        0x8000  // ignore symbols that are not
                                        // class-names or namespace-names
#       define SCTjoin          0x10000 // don't allow redefinition of names
                                        // that appear in enclosing scope

    scope_fp fpsearch;          // search function
    Scope *using_scope;         // named during using-directive
    list_t using_list;          // cleanup list

    symlist_t friends;          // SCTlocal: hidden friend class symbols

    static void setScopeEnd(Scope *s_end);
    static int inTemplate();
    symbol *findReal(symbol *s, unsigned sct);
    inline Symbol *checkSequence(Symbol *s);
};

extern Scope *scope_end;        // pointer to innermost scope

void scope_print();
Scope *scope_find(unsigned sct);
Nspacesym *scope_inNamespace();
symbol *scope_search(const char *id,unsigned sct);
symbol *scope_search_correct(const char *id,unsigned sct);
symbol *scope_search2(const char *id,unsigned sct);
symbol *scope_searchx(const char *id,unsigned sct,Scope **psc);
symbol *scope_searchinner(const char *id,unsigned sct);
symbol *scope_searchouter(const char *id,unsigned sct,Scope **psc);
symbol *scope_define(const char *id,unsigned sct, enum SC sclass);
symbol *scope_add( symbol *s, unsigned sct );
symbol *scope_addx(symbol *s,Scope *sc);
void scope_pushclass(Classsym *stag);
void scope_push_symbol(symbol *s);
void scope_push(void *root,scope_fp fpsearch,int sctype);
#if TX86
void *scope_pop();
#else
symbol *scope_pop();
#endif
void scope_using(Nspacesym *sn,scope_fp fpsearch,int sctype,Scope *sce);
list_t scope_getList(symbol *s);
int scope_pushEnclosing(symbol *s);
void scope_unwind(int nscopes);
void scope_term();

#endif // !SPP
#endif // !SCOPE_H
