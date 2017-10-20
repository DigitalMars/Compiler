/**
 * Implementation of the
 * $(LINK2 http://www.digitalmars.com/download/freecompiler.html, Digital Mars C/C++ Compiler).
 *
 * Copyright:   Copyright (c) 2006-2017 by Digital Mars, All Rights Reserved
 * Authors:     $(LINK2 http://www.digitalmars.com, Walter Bright)
 * License:     Distributed under the Boost Software License, Version 1.0.
 *              http://www.boost.org/LICENSE_1_0.txt
 * Source:      https://github.com/DigitalMars/Compiler/blob/master/dm/src/dmc/stubobj.d
 */

module stubobj;

version (HTOD)
{

import ddmd.backend.cdef;
import ddmd.backend.cc;
import ddmd.backend.code;
import ddmd.backend.el;

extern (C++):

class Obj
{
  static:
    void termfile() { }
    void term(const(char)* objfilename) { }
    void linnum(Srcpos srcpos,int seg,targ_size_t offset) { }
    void startaddress(Symbol *s) { }
    void dosseg() { }
    bool includelib(const(char)* name) { return false; }
    void exestr(const(char)* p) { }
    void user(const(char)* p) { }
    void wkext(Symbol *s1,Symbol *s2) { }
    void lzext(Symbol *s1,Symbol *s2) { }
    void _alias(const(char)* n1,const(char)* n2) { }
    void obj_modname(const(char)* modname) { }
    void compiler() { }
    void segment_group(targ_size_t codesize,targ_size_t datasize,
                    targ_size_t cdatasize,targ_size_t udatasize) { }
    void staticctor(Symbol *s,int dtor,int seg) { }
    void setModuleCtorDtor(Symbol *s, bool isCtor) { }
    void ehtables(Symbol *sfunc,targ_size_t size,Symbol *ehsym) { }
    int comdat(Symbol *s) { return 0; }
    int comdatsize(Symbol *s, targ_size_t symsize) { return 0; }
    void setcodeseg(int seg) { }
    int string_literal_segment(uint sz) { return 0; }
    int codeseg(char *name,int suffix) { return 0; }
    seg_data *tlsseg() { return null; }
    int fardata(char *name,targ_size_t size,targ_size_t *poffset) { return 0; }
    void _import(elem *e) { }
    size_t mangle(Symbol *s,char *dest) { return 0; }
    void export_symbol(Symbol *s,uint argsize) { }
    void pubdef(int seg,Symbol *s,targ_size_t offset) { }
    void pubdefsize(int seg,Symbol *s,targ_size_t offset, targ_size_t symsize) { }
    int external_def(const(char)* name) { return 0; }
    int external(Symbol *s) { return 0; }
    int objcomdef(Symbol *s,int flag,targ_size_t size,targ_size_t count) { return 0; }
    void lidata(int seg,targ_size_t offset,targ_size_t count) { }
    void _byte(int seg,targ_size_t offset,uint _byte) { }
    uint bytes(int seg,targ_size_t offset,uint nbytes, void *p) { return 0; }
    void ledata(int seg,targ_size_t offset,targ_size_t data,
            uint lcfd,uint idx1,uint idx2) { }
    void write_long(int seg,targ_size_t offset,uint data,
            uint lcfd,uint idx1,uint idx2) { }
    void reftodatseg(int seg,targ_size_t offset,targ_size_t val,
            uint targetdatum,int flags) { }
    void reftofarseg(int seg,targ_size_t offset,targ_size_t val,
            int farseg,int flags) { }
    void reftocodeseg(int seg,targ_size_t offset,targ_size_t val) { }
    int reftoident(int seg,targ_size_t offset,Symbol *s,targ_size_t val,
            int flags) { return 0; }
    void far16thunk(Symbol *s) { }
    void fltused() { }
    int common_block(Symbol *s,int flag,targ_size_t size,targ_size_t count) { return 0; }
    void write_bytes(seg_data *pseg, uint nbytes, void *p) { }
    void gotref(Symbol *s) { }
    int jmpTableSegment(Symbol* s) { return 0; }
}

}
