// Copyright (C) ?-1998 by Symantec
// Copyright (C) 2000-2010 by Digital Mars
// All Rights Reserved
// http://www.digitalmars.com
/*
 * This source file is made available for personal use
 * only. The license is in /dmd/src/dmd/backendlicense.txt
 * For any other uses, please contact Digital Mars.
 */


// Output to ELF object files

#if SCPP || MARS
#include        <stdio.h>
#include        <string.h>
#include        <stdlib.h>

#if __sun&&__SVR4
#include        <alloca.h>
#endif

#include        "cc.h"
#include        "global.h"
#include        "code.h"
#include        "type.h"
#include        "melf.h"
#include        "outbuf.h"
#include        "filespec.h"
#include        "cv4.h"
#include        "cgcv.h"
#include        "dt.h"

#include        "aa.h"
#include        "tinfo.h"

#if ELFOBJ

#include        "dwarf.h"

#include        "aa.h"
#include        "tinfo.h"

//#define DEBSYM 0x7E

static Outbuffer *fobjbuf;

regm_t BYTEREGS = BYTEREGS_INIT;
regm_t ALLREGS = ALLREGS_INIT;

static char __file__[] = __FILE__;      // for tassert.h
#include        "tassert.h"

#define MATCH_SECTION 1

#define DEST_LEN (IDMAX + IDOHD + 1)
char *obj_mangle2(Symbol *s,char *dest);

#if MARS
// C++ name mangling is handled by front end
#define cpp_mangle(s) ((s)->Sident)
#endif

/***************************************************
 * Correspondence of relocation types
 *      386             32 bit in 64      64 in 64
 *      RI_TYPE_SYM32   R_X86_64_32       R_X86_64_64
 *      RI_TYPE_GOTOFF  R_X86_64_PC32     R_X86_64_
 *      RI_TYPE_GOTPC   R_X86_64_         R_X86_64_
 *      RI_TYPE_GOT32   R_X86_64_         R_X86_64_
 *      RI_TYPE_TLS_GD  R_X86_64_TLSGD    R_X86_64_
 *      RI_TYPE_TLS_IE  R_X86_64_GOTTPOFF R_X86_64_
 *      RI_TYPE_TLS_LE  R_X86_64_TPOFF32  R_X86_64_
 *      RI_TYPE_PLT32   R_X86_64_PLT32    R_X86_64_
 *      RI_TYPE_PC32    R_X86_64_PC32     R_X86_64_
 */

/******************************************
 */

symbol *GOTsym; // global offset table reference

symbol *Obj::getGOTsym()
{
    if (!GOTsym)
    {
        GOTsym = symbol_name("_GLOBAL_OFFSET_TABLE_",SCglobal,tspvoid);
    }
    return GOTsym;
}

void Obj::refGOTsym()
{
    if (!GOTsym)
    {
        symbol *s = Obj::getGOTsym();
        Obj::external(s);
    }
}

static void objfile_write(FILE *fd, void *buffer, unsigned len);

STATIC char * objmodtoseg (const char *modname);
STATIC void objfixupp (struct FIXUP *);
STATIC void ledata_new (int seg,targ_size_t offset);
void obj_tlssections();

static IDXSYM elf_addsym(IDXSTR sym, targ_size_t val, unsigned sz,
                        unsigned typ,unsigned bind,IDXSEC sec);
static long elf_align(targ_size_t size, long offset);

// The object file is built is several separate pieces

// Non-repeatable section types have single output buffers
//      Pre-allocated buffers are defined for:
//              Section Names string table
//              Section Headers table
//              Symbol table
//              String table
//              Notes section
//              Comment data

// Section Names  - String table for section names only
static Outbuffer *section_names;
#define SEC_NAMES_INIT  800
#define SEC_NAMES_INC   400

// Hash table for section_names
AArray *section_names_hashtable;

/* ====================== Cached Strings in section_names ================= */

struct TypeInfo_Idxstr : TypeInfo
{
    const char* toString();
    hash_t getHash(void *p);
    int equals(void *p1, void *p2);
    int compare(void *p1, void *p2);
    size_t tsize();
    void swap(void *p1, void *p2);
};

TypeInfo_Idxstr ti_idxstr;

const char* TypeInfo_Idxstr::toString()
{
    return "IDXSTR";
}

hash_t TypeInfo_Idxstr::getHash(void *p)
{
    IDXSTR a = *(IDXSTR *)p;
    hash_t hash = 0;
    for (const char *s = (char *)(section_names->buf + a);
         *s;
         s++)
    {
        hash = hash * 11 + *s;
    }
    return hash;
}

int TypeInfo_Idxstr::equals(void *p1, void *p2)
{
    IDXSTR a1 = *(IDXSTR*)p1;
    IDXSTR a2 = *(IDXSTR*)p2;
    const char *s1 = (char *)(section_names->buf + a1);
    const char *s2 = (char *)(section_names->buf + a2);

    return strcmp(s1, s2) == 0;
}

int TypeInfo_Idxstr::compare(void *p1, void *p2)
{
    IDXSTR a1 = *(IDXSTR*)p1;
    IDXSTR a2 = *(IDXSTR*)p2;
    const char *s1 = (char *)(section_names->buf + a1);
    const char *s2 = (char *)(section_names->buf + a2);

    return strcmp(s1, s2);
}

size_t TypeInfo_Idxstr::tsize()
{
    return sizeof(IDXSTR);
}

void TypeInfo_Idxstr::swap(void *p1, void *p2)
{
    assert(0);
}


/* ======================================================================== */

// String Table  - String table for all other names
static Outbuffer *symtab_strings;


// Section Headers
Outbuffer  *SECbuf;             // Buffer to build section table in
#define SecHdrTab ((Elf32_Shdr *)SECbuf->buf)
#define GET_SECTION(secidx) (SecHdrTab + secidx)
#define GET_SECTION_NAME(secidx) (section_names->buf + SecHdrTab[secidx].sh_name)

// The relocation for text and data seems to get lost.
// Try matching the order gcc output them
// This means defining the sections and then removing them if they are
// not used.
static int section_cnt; // Number of sections in table

#define SHI_TEXT        1
#define SHI_RELTEXT     2
#define SHI_DATA        3
#define SHI_RELDATA     4
#define SHI_BSS         5
#define SHI_RODAT       6
#define SHI_STRINGS     7
#define SHI_SYMTAB      8
#define SHI_SECNAMES    9
#define SHI_COM         10
#define SHI_NOTE        11

IDXSYM *mapsec2sym;
#define S2S_INC 20

#define SymbolTable   ((Elf32_Sym *)SYMbuf->buf)
#define SymbolTable64 ((Elf64_Sym *)SYMbuf->buf)
static int symbol_idx;          // Number of symbols in symbol table
static int local_cnt;           // Number of symbols with STB_LOCAL

#define STI_FILE 1              // Where file symbol table entry is
#define STI_TEXT 2
#define STI_DATA 3
#define STI_BSS  4
#define STI_GCC  5              // Where "gcc2_compiled" symbol is */
#define STI_RODAT 6             // Symbol for readonly data
#define STI_NOTE 7              // Where note symbol table entry is
#define STI_COM  8

// NOTE: There seems to be a requirement that the read-only data have the
// same symbol table index and section index. Use section NOTE as a place
// holder. When a read-only string section is required, swap to NOTE.

// Symbol Table
Outbuffer  *SYMbuf;             // Buffer to build symbol table in

// Notes data (note currently used)
static Outbuffer *note_data;
static IDXSEC secidx_note;      // Final table index for note data

// Comment data for compiler version
static Outbuffer *comment_data;
static const char compiler[] = "\0Digital Mars C/C++"
        VERSION
        ;       // compiled by ...

// Each compiler segment is an elf section
// Predefined compiler segments CODE,DATA,CDATA,UDATA map to indexes
//      into SegData[]
//      An additionl index is reserved for comment data
//      New compiler segments are added to end.
//
// There doesn't seem to be any way to get reserved data space in the
//      same section as initialized data or code, so section offsets should
//      be continuous when adding data. Fix-ups anywhere withing existing data.

#define COMD UDATA+1
#define OB_SEG_SIZ      10              // initial number of segments supported
#define OB_SEG_INC      10              // increment for additional segments

#define OB_CODE_STR     100000          // initial size for code
#define OB_CODE_INC     100000          // increment for additional code
#define OB_DATA_STR     100000          // initial size for data
#define OB_DATA_INC     100000          // increment for additional data
#define OB_CDATA_STR      1024          // initial size for data
#define OB_CDATA_INC      1024          // increment for additional data
#define OB_COMD_STR        256          // initial size for comments
                                        // increment as needed
#define OB_XTRA_STR        250          // initial size for extra segments
#define OB_XTRA_INC      10000          // increment size

#define MAP_SEG2SECIDX(seg) (SegData[seg]->SDshtidx)
#define MAP_SEG2SYMIDX(seg) (SegData[seg]->SDsymidx)
#define MAP_SEG2SEC(seg) (&SecHdrTab[MAP_SEG2SECIDX(seg)])
#define MAP_SEG2TYP(seg) (MAP_SEG2SEC(seg)->sh_flags & SHF_EXECINSTR ? CODE : DATA)

seg_data **SegData;
int seg_count;
int seg_max;
int seg_tlsseg = UNKNOWN;
int seg_tlsseg_bss = UNKNOWN;

int elf_getsegment2(IDXSEC shtidx, IDXSYM symidx, IDXSEC relidx);


/*******************************
 * Output a string into a string table
 * Input:
 *      strtab  =       string table for entry
 *      str     =       string to add
 *
 * Returns index into the specified string table.
 */

IDXSTR Obj::addstr(Outbuffer *strtab, const char *str)
{
    //dbg_printf("Obj::addstr(strtab = x%x str = '%s')\n",strtab,str);
    IDXSTR idx = strtab->size();        // remember starting offset
    strtab->writeString(str);
    //dbg_printf("\tidx %d, new size %d\n",idx,strtab->size());
    return idx;
}

/*******************************
 * Find a string in a string table
 * Input:
 *      strtab  =       string table for entry
 *      str     =       string to find
 *
 * Returns index into the specified string table or 0.
 */

static IDXSTR elf_findstr(Outbuffer *strtab, const char *str, const char *suffix)
{
    //printf("elf_findstr(strtab = %p, str = %s, suffix = %s\n", strtab, str ? str : "", suffix ? suffix : "");

    size_t len = strlen(str);

    // Combine str~suffix and have buf point to the combination
#ifdef DEBUG
    char tmpbuf[25];    // to exercise the alloca() code path
#else
    char tmpbuf[1024];  // the alloca() code path is slow
#endif
    const char *buf;
    if (suffix)
    {
        size_t suffixlen = strlen(suffix);
        if (len + suffixlen >= sizeof(tmpbuf))
        {
             buf = (char *)alloca(len + suffixlen + 1);
             assert(buf);
        }
        else
        {
             buf = tmpbuf;
        }
        memcpy((char *)buf, str, len);
        memcpy((char *)buf + len, suffix, suffixlen + 1);
        len += suffixlen;
    }
    else
        buf = str;

    // Linear search, slow
    const char *ent = (char *)strtab->buf+1;
    const char *pend = ent+strtab->size() - 1;
    while (ent + len < pend)
    {
        if (memcmp(buf, ent, len + 1) == 0)
            return ent - (const char *)strtab->buf;
        ent = (const char *)memchr(ent, 0, pend - ent);
        ent += 1;
    }
    return 0;                   // never found match
}

/*******************************
 * Output a mangled string into the symbol string table
 * Input:
 *      str     =       string to add
 *
 * Returns index into the table.
 */

static IDXSTR elf_addmangled(Symbol *s)
{
    //printf("elf_addmangled(%s)\n", s->Sident);
    char dest[DEST_LEN];
    char *destr;
    const char *name;
    int len;
    IDXSTR namidx;

    namidx = symtab_strings->size();
    destr = obj_mangle2(s, dest);
    name = destr;
    if (CPP && name[0] == '_' && name[1] == '_')
    {
        if (strncmp(name,"__ct__",6) == 0)
            name += 4;
#if 0
        switch(name[2])
        {
            case 'c':
                if (strncmp(name,"__ct__",6) == 0)
                    name += 4;
                break;
            case 'd':
                if (strcmp(name,"__dl__FvP") == 0)
                    name = "__builtin_delete";
                break;
            case 'v':
                //if (strcmp(name,"__vec_delete__FvPiUIPi") == 0)
                    //name = "__builtin_vec_del";
                //else
                //if (strcmp(name,"__vn__FPUI") == 0)
                    //name = "__builtin_vec_new";
                break;
            case 'n':
                if (strcmp(name,"__nw__FPUI") == 0)
                    name = "__builtin_new";
                break;
        }
#endif
    }
    else if (tyfunc(s->ty()) && s->Sfunc && s->Sfunc->Fredirect)
        name = s->Sfunc->Fredirect;
    len = strlen(name);
    symtab_strings->reserve(len+1);
    strcpy((char *)symtab_strings->p,name);
    symtab_strings->setsize(namidx+len+1);
    if (destr != dest)                  // if we resized result
        mem_free(destr);
    //dbg_printf("\telf_addmagled symtab_strings %s namidx %d len %d size %d\n",name, namidx,len,symtab_strings->size());
    return namidx;
}

/*******************************
 * Output a symbol into the symbol table
 * Input:
 *      stridx  =       string table index for name
 *      val     =       value associated with symbol
 *      sz      =       symbol size
 *      typ     =       symbol type
 *      bind    =       symbol binding
 *      segidx  =       segment index for segment where symbol is defined
 *
 * Returns the symbol table index for the symbol
 */

static IDXSYM elf_addsym(IDXSTR nam, targ_size_t val, unsigned sz,
        unsigned typ, unsigned bind, IDXSEC sec)
{
    //dbg_printf("elf_addsym(nam %d, val %d, sz %x, typ %x, bind %x, sec %d\n",
            //nam,val,sz,typ,bind,sec);

    /* We want globally defined data symbols to have a size because
     * zero sized symbols break copy relocations for shared libraries.
     */
    if(sz == 0 && (bind == STB_GLOBAL || bind == STB_WEAK) &&
       (typ == STT_OBJECT || typ == STT_TLS) &&
       sec != SHT_UNDEF)
       sz = 1; // so fake it if it doesn't

    if (I64)
    {
        if (!SYMbuf)
        {   SYMbuf = new Outbuffer(50 * sizeof(Elf64_Sym));
            SYMbuf->reserve(100 * sizeof(Elf64_Sym));
        }
        Elf64_Sym sym;
        sym.st_name = nam;
        sym.st_value = val;
        sym.st_size = sz;
        sym.st_info = ELF_ST_INFO(bind,typ);
        sym.st_other = 0;
        sym.st_shndx = sec;
        SYMbuf->write(&sym,sizeof(sym));
    }
    else
    {
        if (!SYMbuf)
        {   SYMbuf = new Outbuffer(50 * sizeof(Elf32_Sym));
            SYMbuf->reserve(100 * sizeof(Elf32_Sym));
        }
        Elf32_Sym sym;
        sym.st_name = nam;
        sym.st_value = val;
        sym.st_size = sz;
        sym.st_info = ELF_ST_INFO(bind,typ);
        sym.st_other = 0;
        sym.st_shndx = sec;
        SYMbuf->write(&sym,sizeof(sym));
    }
    if (bind == STB_LOCAL)
        local_cnt++;
    //dbg_printf("\treturning symbol table index %d\n",symbol_idx);
    return symbol_idx++;
}

/*******************************
 * Create a new section header table entry.
 *
 * Input:
 *      name    =       section name
 *      suffix  =       suffix for name or NULL
 *      type    =       type of data in section sh_type
 *      flags   =       attribute flags sh_flags
 * Output:
 *      section_cnt = assigned number for this section
 *              Note: Sections will be reordered on output
 */

static IDXSEC elf_newsection2(
        elf_u32_f32 name,
        elf_u32_f32 type,
        elf_u32_f32 flags,
        elf_add_f32 addr,
        elf_off_f32 offset,
        elf_u32_f32 size,
        elf_u32_f32 link,
        elf_u32_f32 info,
        elf_u32_f32 addralign,
        elf_u32_f32 entsize)
{
    Elf32_Shdr sec;

    sec.sh_name = name;
    sec.sh_type = type;
    sec.sh_flags = flags;
    sec.sh_addr = addr;
    sec.sh_offset = offset;
    sec.sh_size = size;
    sec.sh_link = link;
    sec.sh_info = info;
    sec.sh_addralign = addralign;
    sec.sh_entsize = entsize;

    if (!SECbuf)
    {   SECbuf = new Outbuffer(4 * sizeof(Elf32_Shdr));
        SECbuf->reserve(16 * sizeof(Elf32_Shdr));
    }
    SECbuf->write((void *)&sec, sizeof(sec));
    return section_cnt++;
}

static IDXSEC elf_newsection(const char *name, const char *suffix,
        elf_u32_f32 type, elf_u32_f32 flags)
{
    // dbg_printf("elf_newsection(%s,%s,type %d, flags x%x)\n",
    //        name?name:"",suffix?suffix:"",type,flags);

    IDXSTR namidx = section_names->size();
    section_names->writeString(name);
    if (suffix)
    {   // Append suffix string
        section_names->setsize(section_names->size() - 1);  // back up over terminating 0
        section_names->writeString(suffix);
    }
    IDXSTR *pidx = (IDXSTR *)section_names_hashtable->get(&namidx);
    assert(!*pidx);             // must not already exist
    *pidx = namidx;

    return elf_newsection2(namidx,type,flags,0,0,0,0,0,0,0);
}

/**************************
 * Ouput read only data and generate a symbol for it.
 *
 */

symbol *Obj::sym_cdata(tym_t ty,char *p,int len)
{
    symbol *s;

#if 0
    if (OPT_IS_SET(OPTfwritable_strings))
    {
        alignOffset(DATA, tysize(ty));
        s = symboldata(Doffset, ty);
        SegData[DATA]->SDbuf->write(p,len);
        s->Sseg = DATA;
        s->Soffset = Doffset;   // Remember its offset into DATA section
        Doffset += len;
    }
    else
#endif
    {
        //printf("Obj::sym_cdata(ty = %x, p = %x, len = %d, CDoffset = %x)\n", ty, p, len, CDoffset);
        alignOffset(CDATA, tysize(ty));
        s = symboldata(CDoffset, ty);
        Obj::bytes(CDATA, CDoffset, len, p);
        s->Sseg = CDATA;
    }

    s->Sfl = /*(config.flags3 & CFG3pic) ? FLgotoff :*/ FLextern;
    return s;
}

/**************************
 * Ouput read only data for data.
 * Output:
 *      *pseg   segment of that data
 * Returns:
 *      offset of that data
 */

int Obj::data_readonly(char *p, int len, int *pseg)
{
    int oldoff;
    /*if (OPT_IS_SET(OPTfwritable_strings))
    {
        oldoff = Doffset;
        SegData[DATA]->SDbuf->reserve(len);
        SegData[DATA]->SDbuf->writen(p,len);
        Doffset += len;
        *pseg = DATA;
    }
    else*/
    {
        oldoff = CDoffset;
        SegData[CDATA]->SDbuf->reserve(len);
        SegData[CDATA]->SDbuf->writen(p,len);
        CDoffset += len;
        *pseg = CDATA;
    }
    return oldoff;
}

int Obj::data_readonly(char *p, int len)
{
    int pseg;

    return Obj::data_readonly(p, len, &pseg);
}

/******************************
 * Perform initialization that applies to all .o output files.
 *      Called before any other obj_xxx routines
 */

Obj *Obj::init(Outbuffer *objbuf, const char *filename, const char *csegname)
{
    //printf("Obj::init()\n");
    ElfObj *obj = new ElfObj();

    cseg = CODE;
    fobjbuf = objbuf;

    mapsec2sym = NULL;
    note_data = NULL;
    secidx_note = 0;
    comment_data = NULL;
    seg_tlsseg = UNKNOWN;
    seg_tlsseg_bss = UNKNOWN;
    GOTsym = NULL;

    // Initialize buffers

    if (symtab_strings)
        symtab_strings->setsize(1);
    else
    {   symtab_strings = new Outbuffer(1024);
        symtab_strings->reserve(2048);
        symtab_strings->writeByte(0);
    }

    if (SECbuf)
        SECbuf->setsize(0);
    section_cnt = 0;

    if (I64)
    {
        static char section_names_init64[] =
          "\0.symtab\0.strtab\0.shstrtab\0.text\0.data\0.bss\0.note\0.comment\0.rodata\0.note.GNU-stack\0.rela.text\0.rela.data";
        #define NAMIDX_NONE      0
        #define NAMIDX_SYMTAB    1       // .symtab
        #define NAMIDX_STRTAB    9       // .strtab
        #define NAMIDX_SHSTRTAB 17      // .shstrtab
        #define NAMIDX_TEXT     27      // .text
        #define NAMIDX_DATA     33      // .data
        #define NAMIDX_BSS      39      // .bss
        #define NAMIDX_NOTE     44      // .note
        #define NAMIDX_COMMENT  50      // .comment
        #define NAMIDX_RODATA   59      // .rodata
        #define NAMIDX_GNUSTACK 67      // .note.GNU-stack
        #define NAMIDX_RELTEXT  83      // .rel.text and .rela.text
        #define NAMIDX_RELDATA  93      // .rel.data
        #define NAMIDX_RELDATA64 94      // .rela.data

        if (section_names)
            section_names->setsize(sizeof(section_names_init64));
        else
        {   section_names = new Outbuffer(512);
            section_names->reserve(1024);
            section_names->writen(section_names_init64, sizeof(section_names_init64));
        }

        if (section_names_hashtable)
            delete section_names_hashtable;
        section_names_hashtable = new AArray(&ti_idxstr, sizeof(IDXSTR));

        // name,type,flags,addr,offset,size,link,info,addralign,entsize
        elf_newsection2(0,               SHT_NULL,   0,                 0,0,0,0,0, 0,0);
        elf_newsection2(NAMIDX_TEXT,SHT_PROGDEF,SHF_ALLOC|SHF_EXECINSTR,0,0,0,0,0, 4,0);
        elf_newsection2(NAMIDX_RELTEXT,SHT_RELA, 0,0,0,0,SHI_SYMTAB,     SHI_TEXT, 8,0x18);
        elf_newsection2(NAMIDX_DATA,SHT_PROGDEF,SHF_ALLOC|SHF_WRITE,    0,0,0,0,0, 8,0);
        elf_newsection2(NAMIDX_RELDATA64,SHT_RELA, 0,0,0,0,SHI_SYMTAB,   SHI_DATA, 8,0x18);
        elf_newsection2(NAMIDX_BSS, SHT_NOBITS,SHF_ALLOC|SHF_WRITE,     0,0,0,0,0, 16,0);
        elf_newsection2(NAMIDX_RODATA,SHT_PROGDEF,SHF_ALLOC,            0,0,0,0,0, 16,0);
        elf_newsection2(NAMIDX_STRTAB,SHT_STRTAB, 0,                    0,0,0,0,0, 1,0);
        elf_newsection2(NAMIDX_SYMTAB,SHT_SYMTAB, 0,                    0,0,0,0,0, 8,0);
        elf_newsection2(NAMIDX_SHSTRTAB,SHT_STRTAB, 0,                  0,0,0,0,0, 1,0);
        elf_newsection2(NAMIDX_COMMENT, SHT_PROGDEF,0,                  0,0,0,0,0, 1,0);
        elf_newsection2(NAMIDX_NOTE,SHT_NOTE,   0,                      0,0,0,0,0, 1,0);
        elf_newsection2(NAMIDX_GNUSTACK,SHT_PROGDEF,0,                  0,0,0,0,0, 1,0);

        IDXSTR namidx;
        namidx = NAMIDX_TEXT;      *(IDXSTR *)section_names_hashtable->get(&namidx) = namidx;
        namidx = NAMIDX_RELTEXT;   *(IDXSTR *)section_names_hashtable->get(&namidx) = namidx;
        namidx = NAMIDX_DATA;      *(IDXSTR *)section_names_hashtable->get(&namidx) = namidx;
        namidx = NAMIDX_RELDATA64; *(IDXSTR *)section_names_hashtable->get(&namidx) = namidx;
        namidx = NAMIDX_BSS;       *(IDXSTR *)section_names_hashtable->get(&namidx) = namidx;
        namidx = NAMIDX_RODATA;    *(IDXSTR *)section_names_hashtable->get(&namidx) = namidx;
        namidx = NAMIDX_STRTAB;    *(IDXSTR *)section_names_hashtable->get(&namidx) = namidx;
        namidx = NAMIDX_SYMTAB;    *(IDXSTR *)section_names_hashtable->get(&namidx) = namidx;
        namidx = NAMIDX_SHSTRTAB;  *(IDXSTR *)section_names_hashtable->get(&namidx) = namidx;
        namidx = NAMIDX_COMMENT;   *(IDXSTR *)section_names_hashtable->get(&namidx) = namidx;
        namidx = NAMIDX_NOTE;      *(IDXSTR *)section_names_hashtable->get(&namidx) = namidx;
        namidx = NAMIDX_GNUSTACK;  *(IDXSTR *)section_names_hashtable->get(&namidx) = namidx;
    }
    else
    {
        static char section_names_init[] =
          "\0.symtab\0.strtab\0.shstrtab\0.text\0.data\0.bss\0.note\0.comment\0.rodata\0.note.GNU-stack\0.rel.text\0.rel.data";

        if (section_names)
            section_names->setsize(sizeof(section_names_init));
        else
        {   section_names = new Outbuffer(512);
            section_names->reserve(100*1024);
            section_names->writen(section_names_init, sizeof(section_names_init));
        }

        if (section_names_hashtable)
            delete section_names_hashtable;
        section_names_hashtable = new AArray(&ti_idxstr, sizeof(IDXSTR));

        // name,type,flags,addr,offset,size,link,info,addralign,entsize
        elf_newsection2(0,               SHT_NULL,   0,                 0,0,0,0,0, 0,0);
        elf_newsection2(NAMIDX_TEXT,SHT_PROGDEF,SHF_ALLOC|SHF_EXECINSTR,0,0,0,0,0, 16,0);
        elf_newsection2(NAMIDX_RELTEXT,SHT_REL, 0,0,0,0,SHI_SYMTAB,      SHI_TEXT, 4,8);
        elf_newsection2(NAMIDX_DATA,SHT_PROGDEF,SHF_ALLOC|SHF_WRITE,    0,0,0,0,0, 4,0);
        elf_newsection2(NAMIDX_RELDATA,SHT_REL, 0,0,0,0,SHI_SYMTAB,      SHI_DATA, 4,8);
        elf_newsection2(NAMIDX_BSS, SHT_NOBITS,SHF_ALLOC|SHF_WRITE,     0,0,0,0,0, 32,0);
        elf_newsection2(NAMIDX_RODATA,SHT_PROGDEF,SHF_ALLOC,            0,0,0,0,0, 1,0);
        elf_newsection2(NAMIDX_STRTAB,SHT_STRTAB, 0,                    0,0,0,0,0, 1,0);
        elf_newsection2(NAMIDX_SYMTAB,SHT_SYMTAB, 0,                    0,0,0,0,0, 4,0);
        elf_newsection2(NAMIDX_SHSTRTAB,SHT_STRTAB, 0,                  0,0,0,0,0, 1,0);
        elf_newsection2(NAMIDX_COMMENT, SHT_PROGDEF,0,                  0,0,0,0,0, 1,0);
        elf_newsection2(NAMIDX_NOTE,SHT_NOTE,   0,                      0,0,0,0,0, 1,0);
        elf_newsection2(NAMIDX_GNUSTACK,SHT_PROGDEF,0,                  0,0,0,0,0, 1,0);

        IDXSTR namidx;
        namidx = NAMIDX_TEXT;      *(IDXSTR *)section_names_hashtable->get(&namidx) = namidx;
        namidx = NAMIDX_RELTEXT;   *(IDXSTR *)section_names_hashtable->get(&namidx) = namidx;
        namidx = NAMIDX_DATA;      *(IDXSTR *)section_names_hashtable->get(&namidx) = namidx;
        namidx = NAMIDX_RELDATA;   *(IDXSTR *)section_names_hashtable->get(&namidx) = namidx;
        namidx = NAMIDX_BSS;       *(IDXSTR *)section_names_hashtable->get(&namidx) = namidx;
        namidx = NAMIDX_RODATA;    *(IDXSTR *)section_names_hashtable->get(&namidx) = namidx;
        namidx = NAMIDX_STRTAB;    *(IDXSTR *)section_names_hashtable->get(&namidx) = namidx;
        namidx = NAMIDX_SYMTAB;    *(IDXSTR *)section_names_hashtable->get(&namidx) = namidx;
        namidx = NAMIDX_SHSTRTAB;  *(IDXSTR *)section_names_hashtable->get(&namidx) = namidx;
        namidx = NAMIDX_COMMENT;   *(IDXSTR *)section_names_hashtable->get(&namidx) = namidx;
        namidx = NAMIDX_NOTE;      *(IDXSTR *)section_names_hashtable->get(&namidx) = namidx;
        namidx = NAMIDX_GNUSTACK;  *(IDXSTR *)section_names_hashtable->get(&namidx) = namidx;
    }

    if (SYMbuf)
        SYMbuf->setsize(0);
    symbol_idx = 0;
    local_cnt = 0;
    // The symbols that every object file has
    elf_addsym(0, 0, 0, STT_NOTYPE,  STB_LOCAL, 0);
    elf_addsym(0, 0, 0, STT_FILE,    STB_LOCAL, SHT_ABS);       // STI_FILE
    elf_addsym(0, 0, 0, STT_SECTION, STB_LOCAL, SHI_TEXT);      // STI_TEXT
    elf_addsym(0, 0, 0, STT_SECTION, STB_LOCAL, SHI_DATA);      // STI_DATA
    elf_addsym(0, 0, 0, STT_SECTION, STB_LOCAL, SHI_BSS);       // STI_BSS
    elf_addsym(0, 0, 0, STT_NOTYPE,  STB_LOCAL, SHI_TEXT);      // STI_GCC
    elf_addsym(0, 0, 0, STT_SECTION, STB_LOCAL, SHI_RODAT);     // STI_RODAT
    elf_addsym(0, 0, 0, STT_SECTION, STB_LOCAL, SHI_NOTE);      // STI_NOTE
    elf_addsym(0, 0, 0, STT_SECTION, STB_LOCAL, SHI_COM);       // STI_COM

    // Initialize output buffers for CODE, DATA and COMMENTS
    //      (NOTE not supported, BSS not required)

    seg_count = 0;

    elf_getsegment2(SHI_TEXT, STI_TEXT, SHI_RELTEXT);
    assert(SegData[CODE]->SDseg == CODE);

    elf_getsegment2(SHI_DATA, STI_DATA, SHI_RELDATA);
    assert(SegData[DATA]->SDseg == DATA);

    elf_getsegment2(SHI_RODAT, STI_RODAT, 0);
    assert(SegData[CDATA]->SDseg == CDATA);

    elf_getsegment2(SHI_BSS, STI_BSS, 0);
    assert(SegData[UDATA]->SDseg == UDATA);

    elf_getsegment2(SHI_COM, STI_COM, 0);
    assert(SegData[COMD]->SDseg == COMD);

    if (config.fulltypes)
        dwarf_initfile(filename);
    return obj;
}

/**************************
 * Initialize the start of object output for this particular .o file.
 *
 * Input:
 *      filename:       Name of source file
 *      csegname:       User specified default code segment name
 */

void Obj::initfile(const char *filename, const char *csegname, const char *modname)
{
    //dbg_printf("Obj::initfile(filename = %s, modname = %s)\n",filename,modname);

    IDXSTR name = Obj::addstr(symtab_strings, filename);
    if (I64)
        SymbolTable64[STI_FILE].st_name = name;
    else
        SymbolTable[STI_FILE].st_name = name;

#if 0
    // compiler flag for linker
    if (I64)
        SymbolTable64[STI_GCC].st_name = Obj::addstr(symtab_strings,"gcc2_compiled.");
    else
        SymbolTable[STI_GCC].st_name = Obj::addstr(symtab_strings,"gcc2_compiled.");
#endif

    if (csegname && *csegname && strcmp(csegname,".text"))
    {   // Define new section and make it the default for cseg segment
        // NOTE: cseg is initialized to CODE
        IDXSEC newsecidx;
        Elf32_Shdr *newtextsec;
        IDXSYM newsymidx;
        SegData[cseg]->SDshtidx = newsecidx =
            elf_newsection(csegname,0,SHT_PROGDEF,SHF_ALLOC|SHF_EXECINSTR);
        newtextsec = &SecHdrTab[newsecidx];
        newtextsec->sh_addralign = 4;
        SegData[cseg]->SDsymidx =
            elf_addsym(0, 0, 0, STT_SECTION, STB_LOCAL, newsecidx);
    }
    if (config.fulltypes)
        dwarf_initmodule(filename, modname);
}

/***************************
 * Renumber symbols so they are
 * ordered as locals, weak and then global
 * Returns:
 *      sorted symbol table, caller must free with util_free()
 */

void *elf_renumbersyms()
{   void *symtab;
    int nextlocal = 0;
    int nextglobal = local_cnt;

    SYMIDX *sym_map = (SYMIDX *)util_malloc(sizeof(SYMIDX),symbol_idx);

    if (I64)
    {
        Elf64_Sym *oldsymtab = (Elf64_Sym *)SYMbuf->buf;
        Elf64_Sym *symtabend = oldsymtab+symbol_idx;

        symtab = util_malloc(sizeof(Elf64_Sym),symbol_idx);

        Elf64_Sym *sl = (Elf64_Sym *)symtab;
        Elf64_Sym *sg = sl + local_cnt;

        int old_idx = 0;
        for(Elf64_Sym *s = oldsymtab; s != symtabend; s++)
        {   // reorder symbol and map new #s to old
            int bind = ELF_ST_BIND(s->st_info);
            if (bind == STB_LOCAL)
            {
                *sl++ = *s;
                sym_map[old_idx] = nextlocal++;
            }
            else
            {
                *sg++ = *s;
                sym_map[old_idx] = nextglobal++;
            }
            old_idx++;
        }
    }
    else
    {
        Elf32_Sym *oldsymtab = (Elf32_Sym *)SYMbuf->buf;
        Elf32_Sym *symtabend = oldsymtab+symbol_idx;

        symtab = util_malloc(sizeof(Elf32_Sym),symbol_idx);

        Elf32_Sym *sl = (Elf32_Sym *)symtab;
        Elf32_Sym *sg = sl + local_cnt;

        int old_idx = 0;
        for(Elf32_Sym *s = oldsymtab; s != symtabend; s++)
        {   // reorder symbol and map new #s to old
            int bind = ELF_ST_BIND(s->st_info);
            if (bind == STB_LOCAL)
            {
                *sl++ = *s;
                sym_map[old_idx] = nextlocal++;
            }
            else
            {
                *sg++ = *s;
                sym_map[old_idx] = nextglobal++;
            }
            old_idx++;
        }
    }

    // Renumber the relocations
    for (int i = 1; i <= seg_count; i++)
    {                           // Map indicies in the segment table
        seg_data *pseg = SegData[i];
        pseg->SDsymidx = sym_map[pseg->SDsymidx];
        if (pseg->SDrel)
        {
            if (I64)
            {
                Elf64_Rela *rel = (Elf64_Rela *) pseg->SDrel->buf;
                for (int r = 0; r < pseg->SDrelcnt; r++)
                {
                    unsigned t = ELF64_R_TYPE(rel->r_info);
                    unsigned si = ELF64_R_SYM(rel->r_info);
                    assert(si < symbol_idx);
                    rel->r_info = ELF64_R_INFO(sym_map[si],t);
                    rel++;
                }
            }
            else
            {
                Elf32_Rel *rel = (Elf32_Rel *) pseg->SDrel->buf;
                assert(pseg->SDrelcnt == pseg->SDrel->size() / sizeof(Elf32_Rel));
                for (int r = 0; r < pseg->SDrelcnt; r++)
                {
                    unsigned t = ELF32_R_TYPE(rel->r_info);
                    unsigned si = ELF32_R_IDX(rel->r_info);
                    assert(si < symbol_idx);
                    rel->r_info = ELF32_R_INFO(sym_map[si],t);
                    rel++;
                }
            }
        }
    };

    return symtab;
}


/***************************
 * Fixup and terminate object file.
 */

void Obj::termfile()
{
    //dbg_printf("Obj::termfile\n");
    if (configv.addlinenumbers)
    {
        dwarf_termmodule();
    }
}

/*********************************
 * Terminate package.
 */

void Obj::term()
{
    //printf("Obj::term()\n");
#if SCPP
    if (!errcnt)
#endif
    {
        outfixlist();           // backpatches
    }

    if (configv.addlinenumbers)
    {
        dwarf_termfile();
    }

#if SCPP
    if (errcnt)
        return;
#endif

    // Write out the bytes for the header
    static const char elf_string32[EI_NIDENT] =
    {
        ELFMAG0,ELFMAG1,ELFMAG2,ELFMAG3,
        ELFCLASS32,             // EI_CLASS
        ELFDATA2LSB,    // EI_DATA
        EV_CURRENT,             // EI_VERSION
        ELFOSABI_LINUX,0,       // EI_OSABI,EI_ABIVERSION
        0,0,0,0,0,0,0
    };
    static const char elf_string64[EI_NIDENT] =
    {
        ELFMAG0,ELFMAG1,ELFMAG2,ELFMAG3,
        ELFCLASS64,             // EI_CLASS
        ELFDATA2LSB,    // EI_DATA
        EV_CURRENT,             // EI_VERSION
        ELFOSABI_LINUX,0,       // EI_OSABI,EI_ABIVERSION
        0,0,0,0,0,0,0
    };
    fobjbuf->write(I64 ? elf_string64 : elf_string32, EI_NIDENT);

    long foffset;
    Elf32_Shdr *sechdr;
    seg_data *seg;
    void *symtab = elf_renumbersyms();
    FILE *fd = NULL;

    // Output the ELF Header
    // The section header is build in the static variable elf_header
    static Elf64_Ehdr elf_header =
    {
        ET_REL,                         // e_type
        EM_X86_64,                      // e_machine
        EV_CURRENT,                     // e_version
        0,                              // e_entry
        0,                              // e_phoff
        0,                              // e_shoff
        0,                              // e_flags
        sizeof(Elf64_Ehdr) + EI_NIDENT, // e_ehsize
        sizeof(Elf64_Phdr),             // e_phentsize
        0,                              // e_phnum
        sizeof(Elf64_Shdr),             // e_shentsize
        0,                              // e_shnum
        0                               // e_shstrndx
    };
    int hdrsize = I64 ? sizeof(Elf64_Ehdr) : sizeof(Elf32_Hdr);

    elf_header.e_shnum = section_cnt;
    elf_header.e_shstrndx = SHI_SECNAMES;
    fobjbuf->writezeros(hdrsize);

            // Walk through sections determining size and file offsets
            // Sections will be output in the following order
            //  Null segment
            //  For each Code/Data Segment
            //      code/data to load
            //      relocations without addens
            //  .bss
            //  notes
            //  comments
            //  section names table
            //  symbol table
            //  strings table

    foffset = EI_NIDENT + hdrsize;      // start after header
                                    // section header table at end

    //
    // First output individual section data associate with program
    //  code and data
    //
    //printf("Setup offsets and sizes foffset %d\n\tsection_cnt %d, seg_count %d\n",foffset,section_cnt,seg_count);
    for (int i=1; i<= seg_count; i++)
    {
        seg_data *pseg = SegData[i];
        Elf32_Shdr *sechdr = MAP_SEG2SEC(i);        // corresponding section
        if (sechdr->sh_addralign < pseg->SDalignment)
            sechdr->sh_addralign = pseg->SDalignment;
        foffset = elf_align(sechdr->sh_addralign,foffset);
        if (i == UDATA) // 0, BSS never allocated
        {   // but foffset as if it has
            sechdr->sh_offset = foffset;
            sechdr->sh_size = pseg->SDoffset;
                                // accumulated size
            continue;
        }
        else if (sechdr->sh_type == SHT_NOBITS) // .tbss never allocated
        {
            sechdr->sh_offset = foffset;
            sechdr->sh_size = pseg->SDoffset;
                                // accumulated size
            continue;
        }
        else if (!pseg->SDbuf)
            continue;           // For others leave sh_offset as 0

        sechdr->sh_offset = foffset;
        //printf("\tsection name %d,",sechdr->sh_name);
        if (pseg->SDbuf && pseg->SDbuf->size())
        {
            //printf(" - size %d\n",pseg->SDbuf->size());
            sechdr->sh_size = pseg->SDbuf->size();
            fobjbuf->write(pseg->SDbuf->buf, sechdr->sh_size);
            foffset += sechdr->sh_size;
        }
        //printf(" assigned offset %d, size %d\n",foffset,sechdr->sh_size);
    }

    //
    // Next output any notes or comments
    //
    if (note_data)
    {
        sechdr = &SecHdrTab[secidx_note];               // Notes
        sechdr->sh_size = note_data->size();
        sechdr->sh_offset = foffset;
        fobjbuf->write(note_data->buf, sechdr->sh_size);
        foffset += sechdr->sh_size;
    }

    if (comment_data)
    {
        sechdr = &SecHdrTab[SHI_COM];           // Comments
        sechdr->sh_size = comment_data->size();
        sechdr->sh_offset = foffset;
        fobjbuf->write(comment_data->buf, sechdr->sh_size);
        foffset += sechdr->sh_size;
    }

    //
    // Then output string table for section names
    //
    sechdr = &SecHdrTab[SHI_SECNAMES];  // Section Names
    sechdr->sh_size = section_names->size();
    sechdr->sh_offset = foffset;
    //dbg_printf("section names offset %d\n",foffset);
    fobjbuf->write(section_names->buf, sechdr->sh_size);
    foffset += sechdr->sh_size;

    //
    // Symbol table and string table for symbols next
    //
    //dbg_printf("output symbol table size %d\n",SYMbuf->size());
    sechdr = &SecHdrTab[SHI_SYMTAB];    // Symbol Table
    sechdr->sh_size = SYMbuf->size();
    sechdr->sh_entsize = I64 ? sizeof(Elf64_Sym) : sizeof(Elf32_Sym);
    sechdr->sh_link = SHI_STRINGS;
    sechdr->sh_info = local_cnt;
    foffset = elf_align(4,foffset);
    sechdr->sh_offset = foffset;
    fobjbuf->write(symtab, sechdr->sh_size);
    foffset += sechdr->sh_size;
    util_free(symtab);

    //dbg_printf("output section strings size 0x%x,offset 0x%x\n",symtab_strings->size(),foffset);
    sechdr = &SecHdrTab[SHI_STRINGS];   // Symbol Strings
    sechdr->sh_size = symtab_strings->size();
    sechdr->sh_offset = foffset;
    fobjbuf->write(symtab_strings->buf, sechdr->sh_size);
    foffset += sechdr->sh_size;

    //
    // Now the relocation data for program code and data sections
    //
    foffset = elf_align(4,foffset);
    //dbg_printf("output relocations size 0x%x, foffset 0x%x\n",section_names->size(),foffset);
    for (int i=1; i<= seg_count; i++)
    {
        seg = SegData[i];
        if (!seg->SDbuf)
        {
//            sechdr = &SecHdrTab[seg->SDrelidx];
//          if (I64 && sechdr->sh_type == SHT_RELA)
//              sechdr->sh_offset = foffset;
            continue;           // 0, BSS never allocated
        }
        if (seg->SDrel && seg->SDrel->size())
        {
            assert(seg->SDrelidx);
            sechdr = &SecHdrTab[seg->SDrelidx];
            sechdr->sh_size = seg->SDrel->size();
            sechdr->sh_offset = foffset;
            if (I64)
            {
                assert(seg->SDrelcnt == seg->SDrel->size() / sizeof(Elf64_Rela));
#ifdef DEBUG
                for (size_t i = 0; i < seg->SDrelcnt; ++i)
                {   Elf64_Rela *p = ((Elf64_Rela *)seg->SDrel->buf) + i;
                    if (ELF64_R_TYPE(p->r_info) == R_X86_64_64)
                        assert(*(Elf64_Xword *)(seg->SDbuf->buf + p->r_offset) == 0);
                }
#endif
            }
            else
                assert(seg->SDrelcnt == seg->SDrel->size() / sizeof(Elf32_Rel));
            fobjbuf->write(seg->SDrel->buf, sechdr->sh_size);
            foffset += sechdr->sh_size;
        }
    }

    //
    // Finish off with the section header table
    //
    elf_header.e_shoff = foffset;       // remember location in elf header
    //dbg_printf("output section header table\n");

    // Output the completed Section Header Table
    if (I64)
    {   // Translate section headers to 64 bits
        int sz = section_cnt * sizeof(Elf64_Shdr);
        fobjbuf->reserve(sz);
        for (int i = 0; i < section_cnt; i++)
        {
            Elf32_Shdr *p = SecHdrTab + i;
            Elf64_Shdr s;
            s.sh_name      = p->sh_name;
            s.sh_type      = p->sh_type;
            s.sh_flags     = p->sh_flags;
            s.sh_addr      = p->sh_addr;
            s.sh_offset    = p->sh_offset;
            s.sh_size      = p->sh_size;
            s.sh_link      = p->sh_link;
            s.sh_info      = p->sh_info;
            s.sh_addralign = p->sh_addralign;
            s.sh_entsize   = p->sh_entsize;
            fobjbuf->write(&s, sizeof(s));
        }
        foffset += sz;
    }
    else
    {
        fobjbuf->write(SecHdrTab, section_cnt * sizeof(Elf32_Shdr));
        foffset += section_cnt * sizeof(Elf32_Shdr);
    }

    //
    // Now that we have correct offset to section header table, e_shoff,
    //  go back and re-output the elf header
    //
    fobjbuf->position(EI_NIDENT, hdrsize);
    if (I64)
    {
        fobjbuf->write(&elf_header, hdrsize);
    }
    else
    {   Elf32_Hdr h;
        // Transfer to 32 bit header
        h.e_type      = elf_header.e_type;
        h.e_machine   = EM_386;
        h.e_version   = elf_header.e_version;
        h.e_entry     = elf_header.e_entry;
        h.e_phoff     = elf_header.e_phoff;
        h.e_shoff     = elf_header.e_shoff;
        h.e_flags     = elf_header.e_flags;
        h.e_ehsize    = sizeof(Elf32_Hdr) + EI_NIDENT;
        h.e_phentsize = sizeof(elf_pht);
        h.e_phnum     = elf_header.e_phnum;
        h.e_shentsize = sizeof(Elf32_Shdr);
        h.e_shnum     = elf_header.e_shnum;
        h.e_shstrndx  = elf_header.e_shstrndx;
        fobjbuf->write(&h, hdrsize);
    }
    fobjbuf->position(foffset, 0);
    fobjbuf->flush();
}

/*****************************
 * Line number support.
 */

/***************************
 * Record file and line number at segment and offset.
 * The actual .debug_line segment is put out by dwarf_termfile().
 * Input:
 *      cseg    current code segment
 */

void Obj::linnum(Srcpos srcpos, targ_size_t offset)
{
    if (srcpos.Slinnum == 0)
        return;

#if 0
#if MARS || SCPP
    printf("Obj::linnum(cseg=%d, offset=0x%lx) ", cseg, offset);
#endif
    srcpos.print("");
#endif

#if MARS
    if (!srcpos.Sfilename)
        return;
#endif
#if SCPP
    if (!srcpos.Sfilptr)
        return;
    sfile_debug(&srcpos_sfile(srcpos));
    Sfile *sf = *srcpos.Sfilptr;
#endif

    size_t i;
    seg_data *seg = SegData[cseg];

    // Find entry i in SDlinnum_data[] that corresponds to srcpos filename
    for (i = 0; 1; i++)
    {
        if (i == seg->SDlinnum_count)
        {   // Create new entry
            if (seg->SDlinnum_count == seg->SDlinnum_max)
            {   // Enlarge array
                unsigned newmax = seg->SDlinnum_max * 2 + 1;
                //printf("realloc %d\n", newmax * sizeof(linnum_data));
                seg->SDlinnum_data = (linnum_data *)mem_realloc(
                    seg->SDlinnum_data, newmax * sizeof(linnum_data));
                memset(seg->SDlinnum_data + seg->SDlinnum_max, 0,
                    (newmax - seg->SDlinnum_max) * sizeof(linnum_data));
                seg->SDlinnum_max = newmax;
            }
            seg->SDlinnum_count++;
#if MARS
            seg->SDlinnum_data[i].filename = srcpos.Sfilename;
#endif
#if SCPP
            seg->SDlinnum_data[i].filptr = sf;
#endif
            break;
        }
#if MARS
        if (seg->SDlinnum_data[i].filename == srcpos.Sfilename)
#endif
#if SCPP
        if (seg->SDlinnum_data[i].filptr == sf)
#endif
            break;
    }

    linnum_data *ld = &seg->SDlinnum_data[i];
//    printf("i = %d, ld = x%x\n", i, ld);
    if (ld->linoff_count == ld->linoff_max)
    {
        if (!ld->linoff_max)
            ld->linoff_max = 8;
        ld->linoff_max *= 2;
        ld->linoff = (unsigned (*)[2])mem_realloc(ld->linoff, ld->linoff_max * sizeof(unsigned) * 2);
    }
    ld->linoff[ld->linoff_count][0] = srcpos.Slinnum;
    ld->linoff[ld->linoff_count][1] = offset;
    ld->linoff_count++;
}


/*******************************
 * Set start address
 */

void Obj::startaddress(Symbol *s)
{
    //dbg_printf("Obj::startaddress(Symbol *%s)\n",s->Sident);
    //obj.startaddress = s;
}

/*******************************
 * Output library name.
 */

bool Obj::includelib(const char *name)
{
    //dbg_printf("Obj::includelib(name *%s)\n",name);
    return false;
}

/**********************************
 * Do we allow zero sized objects?
 */

bool Obj::allowZeroSize()
{
    return true;
}

/**************************
 * Embed string in executable.
 */

void Obj::exestr(const char *p)
{
    //dbg_printf("Obj::exestr(char *%s)\n",p);
}

/**************************
 * Embed string in obj.
 */

void Obj::user(const char *p)
{
    //dbg_printf("Obj::user(char *%s)\n",p);
}

/*******************************
 * Output a weak extern record.
 */

void Obj::wkext(Symbol *s1,Symbol *s2)
{
    //dbg_printf("Obj::wkext(Symbol *%s,Symbol *s2)\n",s1->Sident,s2->Sident);
}

/*******************************
 * Output file name record.
 *
 * Currently assumes that obj_filename will not be called
 *      twice for the same file.
 */

void obj_filename(const char *modname)
{
    //dbg_printf("obj_filename(char *%s)\n",modname);
    unsigned strtab_idx = Obj::addstr(symtab_strings,modname);
    elf_addsym(strtab_idx,0,0,STT_FILE,STB_LOCAL,SHT_ABS);
}

/*******************************
 * Embed compiler version in .obj file.
 */

void Obj::compiler()
{
    //dbg_printf("Obj::compiler\n");
    comment_data = new Outbuffer();
    comment_data->write(::compiler,sizeof(::compiler));
    //dbg_printf("Comment data size %d\n",comment_data->size());
}


//#if NEWSTATICDTOR

/**************************************
 * Symbol is the function that calls the static constructors.
 * Put a pointer to it into a special segment that the startup code
 * looks at.
 * Input:
 *      s       static constructor function
 *      dtor    !=0 if leave space for static destructor
 *      seg     1:      user
 *              2:      lib
 *              3:      compiler
 */

void Obj::staticctor(Symbol *s,int dtor,int none)
{
// Static constructors and destructors
    IDXSEC seg;
    Outbuffer *buf;

    //dbg_printf("Obj::staticctor(%s) offset %x\n",s->Sident,s->Soffset);
    //symbol_print(s);
    s->Sseg = seg =
        ElfObj::getsegment(".ctors", NULL, SHT_PROGDEF, SHF_ALLOC|SHF_WRITE, 4);
    buf = SegData[seg]->SDbuf;
    if (I64)
        buf->write64(s->Soffset);
    else
        buf->write32(s->Soffset);
    ElfObj::addrel(seg,SegData[seg]->SDoffset,I64 ? R_X86_64_64 : RI_TYPE_SYM32,STI_TEXT,0);
    SegData[seg]->SDoffset = buf->size();
}

/**************************************
 * Symbol is the function that calls the static destructors.
 * Put a pointer to it into a special segment that the exit code
 * looks at.
 * Input:
 *      s       static destructor function
 */

void Obj::staticdtor(Symbol *s)
{
    IDXSEC seg;
    Outbuffer *buf;

    //dbg_printf("Obj::staticdtor(%s) offset %x\n",s->Sident,s->Soffset);
    //symbol_print(s);
    seg = ElfObj::getsegment(".dtors", NULL, SHT_PROGDEF, SHF_ALLOC|SHF_WRITE, 4);
    buf = SegData[seg]->SDbuf;
    if (I64)
        buf->write64(s->Soffset);
    else
        buf->write32(s->Soffset);
    ElfObj::addrel(seg,SegData[seg]->SDoffset,I64 ? R_X86_64_64 : RI_TYPE_SYM32,s->Sxtrnnum,0);
    SegData[seg]->SDoffset = buf->size();
}

//#else

/***************************************
 * Stuff pointer to function in its own segment.
 * Used for static ctor and dtor lists.
 */

void Obj::funcptr(Symbol *s)
{
    //dbg_printf("Obj::funcptr(%s) \n",s->Sident);
}

//#endif

/***************************************
 * Stuff the following data in a separate segment:
 *      pointer to function
 *      pointer to ehsym
 *      length of function
 */

void Obj::ehtables(Symbol *sfunc,targ_size_t size,Symbol *ehsym)
{
    //dbg_printf("Obj::ehtables(%s) \n",sfunc->Sident);

    symbol *ehtab_entry = symbol_generate(SCstatic,type_alloc(TYint));
    symbol_keep(ehtab_entry);
    ElfObj::getsegment(".deh_beg", NULL, SHT_PROGDEF, SHF_ALLOC, NPTRSIZE);
    int seg = ElfObj::getsegment(".deh_eh", NULL, SHT_PROGDEF, SHF_ALLOC, NPTRSIZE);
    ehtab_entry->Sseg = seg;
    Outbuffer *buf = SegData[seg]->SDbuf;
    ElfObj::getsegment(".deh_end", NULL, SHT_PROGDEF, SHF_ALLOC, NPTRSIZE);
    ehtab_entry->Stype->Tmangle = mTYman_c;
    ehsym->Stype->Tmangle = mTYman_c;

    assert(sfunc->Sxtrnnum && sfunc->Sseg);
    assert(ehsym->Sxtrnnum && ehsym->Sseg);
    if (I64)
    {
        ElfObj::addrel(seg, buf->size(), R_X86_64_64, MAP_SEG2SYMIDX(sfunc->Sseg), sfunc->Soffset);
        buf->write64(0);

        ElfObj::addrel(seg, buf->size(), R_X86_64_64, MAP_SEG2SYMIDX(ehsym->Sseg), ehsym->Soffset);
        buf->write64(0);

        buf->write64(sfunc->Ssize);
    }
    else
    {
        ElfObj::addrel(seg, buf->size(), RI_TYPE_SYM32, MAP_SEG2SYMIDX(sfunc->Sseg), 0);
        buf->write32(sfunc->Soffset);

        ElfObj::addrel(seg, buf->size(), RI_TYPE_SYM32, MAP_SEG2SYMIDX(ehsym->Sseg), 0);
        buf->write32(ehsym->Soffset);

        buf->write32(sfunc->Ssize);
    }
}

/*********************************************
 * Put out symbols that define the beginning/end of the .deh_eh section.
 */

void Obj::ehsections()
{
    int sec = ElfObj::getsegment(".deh_beg", NULL, SHT_PROGDEF, SHF_ALLOC, NPTRSIZE);
    //Obj::bytes(sec, 0, 4, NULL);

    IDXSTR namidx = Obj::addstr(symtab_strings,"_deh_beg");
    elf_addsym(namidx, 0, 4, STT_OBJECT, STB_GLOBAL, MAP_SEG2SECIDX(sec));
    //elf_addsym(namidx, 0, 4, STT_OBJECT, STB_GLOBAL, MAP_SEG2SECIDX(sec));

    ElfObj::getsegment(".deh_eh", NULL, SHT_PROGDEF, SHF_ALLOC, NPTRSIZE);

    sec = ElfObj::getsegment(".deh_end", NULL, SHT_PROGDEF, SHF_ALLOC, NPTRSIZE);
    namidx = Obj::addstr(symtab_strings,"_deh_end");
    elf_addsym(namidx, 0, 4, STT_OBJECT, STB_GLOBAL, MAP_SEG2SECIDX(sec));

    obj_tlssections();
}

/*********************************************
 * Put out symbols that define the beginning/end of the thread local storage sections.
 */

void obj_tlssections()
{
    IDXSTR namidx;
    int align = I64 ? 16 : 4;

    int sec = ElfObj::getsegment(".tdata", NULL, SHT_PROGDEF, SHF_ALLOC|SHF_WRITE|SHF_TLS, align);
    Obj::bytes(sec, 0, align, NULL);

    namidx = Obj::addstr(symtab_strings,"_tlsstart");
    elf_addsym(namidx, 0, align, STT_TLS, STB_GLOBAL, MAP_SEG2SECIDX(sec));

    ElfObj::getsegment(".tdata.", NULL, SHT_PROGDEF, SHF_ALLOC|SHF_WRITE|SHF_TLS, align);

    sec = ElfObj::getsegment(".tcommon", NULL, SHT_NOBITS, SHF_ALLOC|SHF_WRITE|SHF_TLS, align);
    namidx = Obj::addstr(symtab_strings,"_tlsend");
    elf_addsym(namidx, 0, align, STT_TLS, STB_GLOBAL, MAP_SEG2SECIDX(sec));
}

/*********************************
 * Setup for Symbol s to go into a COMDAT segment.
 * Output (if s is a function):
 *      cseg            segment index of new current code segment
 *      Coffset         starting offset in cseg
 * Returns:
 *      "segment index" of COMDAT
 */

STATIC void setup_comdat(Symbol *s)
{
    const char *prefix;
    int type;
    int flags;
    int align = 4;

    //printf("Obj::comdat(Symbol *%s\n",s->Sident);
    //symbol_print(s);
    symbol_debug(s);
    if (tyfunc(s->ty()))
    {
        //s->Sfl = FLcode;      // was FLoncecode
        //prefix = ".gnu.linkonce.t";   // doesn't work, despite documentation
        prefix = ".text.";              // undocumented, but works
        type = SHT_PROGDEF;
        flags = SHF_ALLOC|SHF_EXECINSTR;
    }
    else if ((s->ty() & mTYLINK) == mTYthread)
    {
        /* Ensure that ".tdata" precedes any other .tdata. section, as the ld
         * linker script fails to work right.
         */
        if (I64)
            align = 16;
        ElfObj::getsegment(".tdata", NULL, SHT_PROGDEF, SHF_ALLOC|SHF_WRITE|SHF_TLS, align);

        s->Sfl = FLtlsdata;
        prefix = ".tdata.";
        type = SHT_PROGDEF;
        flags = SHF_ALLOC|SHF_WRITE|SHF_TLS;
    }
    else
    {
        if (I64)
            align = 16;
        s->Sfl = FLdata;
        //prefix = ".gnu.linkonce.d.";
        prefix = ".data.";
        type = SHT_PROGDEF;
        flags = SHF_ALLOC|SHF_WRITE;
    }

    s->Sseg = ElfObj::getsegment(prefix, cpp_mangle(s), type, flags, align);
                                // find or create new segment
    if (s->Salignment > align)
        SegData[s->Sseg]->SDalignment = s->Salignment;
    SegData[s->Sseg]->SDsym = s;
}

int Obj::comdat(Symbol *s)
{
    setup_comdat(s);
    if (s->Sfl == FLdata || s->Sfl == FLtlsdata)
    {
        Obj::pubdef(s->Sseg,s,0);
        searchfixlist(s);               // backpatch any refs to this symbol
    }
    return s->Sseg;
}

int Obj::comdatsize(Symbol *s, targ_size_t symsize)
{
    setup_comdat(s);
    if (s->Sfl == FLdata || s->Sfl == FLtlsdata)
    {
        Obj::pubdefsize(s->Sseg,s,0,symsize);
        searchfixlist(s);               // backpatch any refs to this symbol
    }
    s->Soffset = 0;
    return s->Sseg;
}

/********************************
 * Get a segment for a segment name.
 * Input:
 *      name            name of segment, if NULL then revert to default name
 *      suffix          append to name
 *      align           alignment
 * Returns:
 *      segment index of found or newly created segment
 */

int elf_getsegment2(IDXSEC shtidx, IDXSYM symidx, IDXSEC relidx)
{
    //printf("SegData = %p\n", SegData);
    int seg = ++seg_count;
    if (seg_count >= seg_max)
    {                           // need more room in segment table
        seg_max += OB_SEG_INC;
        SegData = (seg_data **)mem_realloc(SegData,seg_max * sizeof(seg_data *));
        memset(&SegData[seg_count], 0, (seg_max - seg_count) * sizeof(seg_data *));
    }
    assert(seg_count < seg_max);
    if (!SegData[seg])
    {   SegData[seg] = (seg_data *)mem_calloc(sizeof(seg_data));
        //printf("test2: SegData[%d] = %p\n", seg, SegData[seg]);
    }

    seg_data *pseg = SegData[seg];
    pseg->SDseg = seg;
    pseg->SDshtidx = shtidx;
    pseg->SDoffset = 0;
    if (pseg->SDbuf)
        pseg->SDbuf->setsize(0);
    else
    {   if (SecHdrTab[shtidx].sh_type != SHT_NOBITS)
        {   pseg->SDbuf = new Outbuffer(OB_XTRA_STR);
            pseg->SDbuf->reserve(1024);
        }
    }
    if (pseg->SDrel)
        pseg->SDrel->setsize(0);
    pseg->SDsymidx = symidx;
    pseg->SDrelidx = relidx;
    pseg->SDrelmaxoff = 0;
    pseg->SDrelindex = 0;
    pseg->SDrelcnt = 0;
    pseg->SDshtidxout = 0;
    pseg->SDsym = NULL;
    pseg->SDaranges_offset = 0;
    pseg->SDlinnum_count = 0;
    return seg;
}

int ElfObj::getsegment(const char *name, const char *suffix, int type, int flags,
        int align)
{
    //printf("ElfObj::getsegment(%s,%s,flags %x, align %d)\n",name,suffix,flags,align);

    // Add name~suffix to the section_names table
    IDXSTR namidx = section_names->size();
    section_names->writeString(name);
    if (suffix)
    {   // Append suffix string
        section_names->setsize(section_names->size() - 1);  // back up over terminating 0
        section_names->writeString(suffix);
    }
    IDXSTR *pidx = (IDXSTR *)section_names_hashtable->get(&namidx);
    if (*pidx)
    {   // this section name already exists
        section_names->setsize(namidx);                 // remove addition
        namidx = *pidx;
        for (int seg = CODE; seg <= seg_count; seg++)
        {                               // should be in segment table
            if (MAP_SEG2SEC(seg)->sh_name == namidx)
            {
                return seg;             // found section for segment
            }
        }
        assert(0);      // but it's not a segment
        // FIX - should be an error message conflict with section names
    }
    *pidx = namidx;

    //dbg_printf("\tNew segment - %d size %d\n", seg,SegData[seg]->SDbuf);
    IDXSEC shtidx = elf_newsection2(namidx,type,flags,0,0,0,0,0,0,0);
    SecHdrTab[shtidx].sh_addralign = align;
    IDXSYM symidx = elf_addsym(0, 0, 0, STT_SECTION, STB_LOCAL, shtidx);
    int seg = elf_getsegment2(shtidx, symidx, 0);
    //printf("-ElfObj::getsegment() = %d\n", seg);
    return seg;
}

/********************************
 * Define a new code segment.
 * Input:
 *      name            name of segment, if NULL then revert to default
 *      suffix  0       use name as is
 *              1       append "_TEXT" to name
 * Output:
 *      cseg            segment index of new current code segment
 *      Coffset         starting offset in cseg
 * Returns:
 *      segment index of newly created code segment
 */

int Obj::codeseg(char *name,int suffix)
{
    int seg;
    const char *sfx;

    //dbg_printf("Obj::codeseg(%s,%x)\n",name,suffix);

    sfx = (suffix) ? "_TEXT" : NULL;

    if (!name)                          // returning to default code segment
    {
        if (cseg != CODE)               // not the current default
        {
            SegData[cseg]->SDoffset = Coffset;
            Coffset = SegData[CODE]->SDoffset;
            cseg = CODE;
        }
        return cseg;
    }

    seg = ElfObj::getsegment(name, sfx, SHT_PROGDEF, SHF_ALLOC|SHF_EXECINSTR, 4);
                                    // find or create code segment

    cseg = seg;                         // new code segment index
    Coffset = 0;

    return seg;
}

/*********************************
 * Define segments for Thread Local Storage.
 * Here's what the elf tls spec says:
 *      Field           .tbss                   .tdata
 *      sh_name         .tbss                   .tdata
 *      sh_type         SHT_NOBITS              SHT_PROGBITS
 *      sh_flags        SHF_ALLOC|SHF_WRITE|    SHF_ALLOC|SHF_WRITE|
 *                      SHF_TLS                 SHF_TLS
 *      sh_addr         virtual addr of section virtual addr of section
 *      sh_offset       0                       file offset of initialization image
 *      sh_size         size of section         size of section
 *      sh_link         SHN_UNDEF               SHN_UNDEF
 *      sh_info         0                       0
 *      sh_addralign    alignment of section    alignment of section
 *      sh_entsize      0                       0
 * We want _tlsstart and _tlsend to bracket all the D tls data.
 * The default linker script (ld -verbose) says:
 *  .tdata      : { *(.tdata .tdata.* .gnu.linkonce.td.*) }
 *  .tbss       : { *(.tbss .tbss.* .gnu.linkonce.tb.*) *(.tcommon) }
 * so if we assign names:
 *      _tlsstart .tdata
 *      symbols   .tdata.
 *      symbols   .tbss
 *      _tlsend   .tbss.
 * this should work.
 * Don't care about sections emitted by other languages, as we presume they
 * won't be storing D gc roots in their tls.
 * Output:
 *      seg_tlsseg      set to segment number for TLS segment.
 * Returns:
 *      segment for TLS segment
 */

seg_data *Obj::tlsseg()
{
    /* Ensure that ".tdata" precedes any other .tdata. section, as the ld
     * linker script fails to work right.
     */
    ElfObj::getsegment(".tdata", NULL, SHT_PROGDEF, SHF_ALLOC|SHF_WRITE|SHF_TLS, 4);

    static const char tlssegname[] = ".tdata.";
    //dbg_printf("Obj::tlsseg(\n");

    if (seg_tlsseg == UNKNOWN)
    {
        seg_tlsseg = ElfObj::getsegment(tlssegname, NULL, SHT_PROGDEF,
            SHF_ALLOC|SHF_WRITE|SHF_TLS, I64 ? 16 : 4);
    }
    return SegData[seg_tlsseg];
}


/*********************************
 * Define segments for Thread Local Storage.
 * Output:
 *      seg_tlsseg_bss  set to segment number for TLS segment.
 * Returns:
 *      segment for TLS segment
 */

seg_data *Obj::tlsseg_bss()
{
    static const char tlssegname[] = ".tbss";
    //dbg_printf("Obj::tlsseg_bss(\n");

    if (seg_tlsseg_bss == UNKNOWN)
    {
        seg_tlsseg_bss = ElfObj::getsegment(tlssegname, NULL, SHT_NOBITS,
            SHF_ALLOC|SHF_WRITE|SHF_TLS, I64 ? 16 : 4);
    }
    return SegData[seg_tlsseg_bss];
}


/*******************************
 * Output an alias definition record.
 */

void Obj::alias(const char *n1,const char *n2)
{
    dbg_printf("Obj::alias(%s,%s)\n",n1,n2);
    assert(0);
#if NOT_DONE
    char *buffer = (char *) alloca(strlen(n1) + strlen(n2) + 2 * ONS_OHD);
    unsigned len = obj_namestring(buffer,n1);
    len += obj_namestring(buffer + len,n2);
    objrecord(ALIAS,buffer,len);
#endif
}

char *unsstr(unsigned value)
{
    static char buffer[64];

    sprintf(buffer, "%d", value);
    return buffer;
}

/*******************************
 * Mangle a name.
 * Returns:
 *      mangled name
 */

char *obj_mangle2(Symbol *s,char *dest)
{
    char *name;

    //dbg_printf("Obj::mangle('%s'), mangle = x%x\n",s->Sident,type_mangle(s->Stype));
    symbol_debug(s);
    assert(dest);
#if SCPP
    name = CPP ? cpp_mangle(s) : s->Sident;
#elif MARS
    name = cpp_mangle(s);
#else
    name = s->Sident;
#endif
    size_t len = strlen(name);                 // # of bytes in name
    //dbg_printf("len %d\n",len);
    switch (type_mangle(s->Stype))
    {
        case mTYman_pas:                // if upper case
        case mTYman_for:
            if (len >= DEST_LEN)
                dest = (char *)mem_malloc(len + 1);
            memcpy(dest,name,len + 1);  // copy in name and ending 0
            for (int i = 0; 1; i++)
            {   char c = dest[i];
                if (!c)
                    break;
                if (c >= 'a' && c <= 'z')
                    dest[i] = c + 'A' - 'a';
            }
            break;
        case mTYman_std:
#if TARGET_LINUX || TARGET_FREEBSD || TARGET_OPENBSD || TARGET_SOLARIS
            if (tyfunc(s->ty()) && !variadic(s->Stype))
#else
            if (!(config.flags4 & CFG4oldstdmangle) &&
                config.exe == EX_NT && tyfunc(s->ty()) &&
                !variadic(s->Stype))
#endif
            {
                char *pstr = unsstr(type_paramsize(s->Stype));
                size_t pstrlen = strlen(pstr);
                size_t destlen = len + 1 + pstrlen + 1;

                if (destlen > DEST_LEN)
                    dest = (char *)mem_malloc(destlen);
                memcpy(dest,name,len);
                dest[len] = '@';
                memcpy(dest + 1 + len, pstr, pstrlen + 1);
                break;
            }
        case mTYman_cpp:
        case mTYman_c:
        case mTYman_d:
        case mTYman_sys:
        case 0:
            if (len >= DEST_LEN)
                dest = (char *)mem_malloc(len + 1);
            memcpy(dest,name,len+1);// copy in name and trailing 0
            break;

        default:
#ifdef DEBUG
            printf("mangling %x\n",type_mangle(s->Stype));
            symbol_print(s);
#endif
            printf("%d\n", type_mangle(s->Stype));
            assert(0);
    }
    //dbg_printf("\t %s\n",dest);
    return dest;
}

/*******************************
 * Export a function name.
 */

void Obj::export_symbol(Symbol *s,unsigned argsize)
{
    //dbg_printf("Obj::export_symbol(%s,%d)\n",s->Sident,argsize);
}

/*******************************
 * Update data information about symbol
 *      align for output and assign segment
 *      if not already specified.
 *
 * Input:
 *      sdata           data symbol
 *      datasize        output size
 *      seg             default seg if not known
 * Returns:
 *      actual seg
 */

int Obj::data_start(Symbol *sdata, targ_size_t datasize, int seg)
{
    targ_size_t alignbytes;
    //printf("Obj::data_start(%s,size %llx,seg %d)\n",sdata->Sident,datasize,seg);
    //symbol_print(sdata);

    if (sdata->Sseg == UNKNOWN) // if we don't know then there
        sdata->Sseg = seg;      // wasn't any segment override
    else
        seg = sdata->Sseg;
    targ_size_t offset = Offset(seg);
    if (sdata->Salignment > 0)
    {   if (SegData[seg]->SDalignment < sdata->Salignment)
            SegData[seg]->SDalignment = sdata->Salignment;
        alignbytes = ((offset + sdata->Salignment - 1) & ~(sdata->Salignment - 1)) - offset;
    }
    else
        alignbytes = align(datasize, offset) - offset;
    if (alignbytes)
        Obj::lidata(seg, offset, alignbytes);
    sdata->Soffset = offset + alignbytes;
    return seg;
}

/*******************************
 * Update function info before codgen
 *
 * If code for this function is in a different segment
 * than the current default in cseg, switch cseg to new segment.
 */

void Obj::func_start(Symbol *sfunc)
{
    //dbg_printf("Obj::func_start(%s)\n",sfunc->Sident);
    symbol_debug(sfunc);

    if ((tybasic(sfunc->ty()) == TYmfunc) && (sfunc->Sclass == SCextern))
    {                                   // create a new code segment
        sfunc->Sseg =
            ElfObj::getsegment(".gnu.linkonce.t.", cpp_mangle(sfunc), SHT_PROGDEF, SHF_ALLOC|SHF_EXECINSTR,4);

    }
    else if (sfunc->Sseg == UNKNOWN)
        sfunc->Sseg = CODE;
    //dbg_printf("sfunc->Sseg %d CODE %d cseg %d Coffset %d\n",sfunc->Sseg,CODE,cseg,Coffset);
    cseg = sfunc->Sseg;
    assert(cseg == CODE || cseg > COMD);
    Obj::pubdef(cseg, sfunc, Coffset);
    sfunc->Soffset = Coffset;

    if (config.fulltypes)
        dwarf_func_start(sfunc);
}

/*******************************
 * Update function info after codgen
 */

void Obj::func_term(Symbol *sfunc)
{
    //dbg_printf("Obj::func_term(%s) offset %x, Coffset %x symidx %d\n",
//          sfunc->Sident, sfunc->Soffset,Coffset,sfunc->Sxtrnnum);

    // fill in the function size
    if (I64)
        SymbolTable64[sfunc->Sxtrnnum].st_size = Coffset - sfunc->Soffset;
    else
        SymbolTable[sfunc->Sxtrnnum].st_size = Coffset - sfunc->Soffset;
    if (config.fulltypes)
        dwarf_func_term(sfunc);
}

/********************************
 * Output a public definition.
 * Input:
 *      seg =           segment index that symbol is defined in
 *      s ->            symbol
 *      offset =        offset of name within segment
 */

void Obj::pubdef(int seg, Symbol *s, targ_size_t offset)
{
    const targ_size_t symsize=
        tyfunc(s->ty()) ? Offset(s->Sseg) - offset : type_size(s->Stype);
    Obj::pubdefsize(seg, s, offset, symsize);
}

/********************************
 * Output a public definition.
 * Input:
 *      seg =           segment index that symbol is defined in
 *      s ->            symbol
 *      offset =        offset of name within segment
 *      symsize         size of symbol
 */

void Obj::pubdefsize(int seg, Symbol *s, targ_size_t offset, targ_size_t symsize)
{
    int bind;
    switch (s->Sclass)
    {
        case SCglobal:
        case SCinline:
            bind = STB_GLOBAL;
            break;
        case SCcomdat:
        case SCcomdef:
            bind = STB_WEAK;
            break;
        default:
            bind = STB_LOCAL;
            break;
    }

#if 0
    //printf("\nObj::pubdef(%d,%s,%d)\n",seg,s->Sident,offset);
    //symbol_print(s);
#endif

    symbol_debug(s);
    IDXSTR namidx = elf_addmangled(s);
    //printf("\tnamidx %d,section %d\n",namidx,MAP_SEG2SECIDX(seg));
    if (tyfunc(s->ty()))
    {
        s->Sxtrnnum = elf_addsym(namidx, offset, symsize,
            STT_FUNC, bind, MAP_SEG2SECIDX(seg));
    }
    else
    {
        const unsigned typ = (s->ty() & mTYthread) ? STT_TLS : STT_OBJECT;
        s->Sxtrnnum = elf_addsym(namidx, offset, symsize,
            typ, bind, MAP_SEG2SECIDX(seg));
    }
    fflush(NULL);
}

/*******************************
 * Output an external symbol for name.
 * Input:
 *      name    Name to do EXTDEF on
 *              (Not to be mangled)
 * Returns:
 *      Symbol table index of the definition
 *      NOTE: Numbers will not be linear.
 */

int Obj::external_def(const char *name)
{
    //dbg_printf("Obj::external_def('%s')\n",name);
    assert(name);
    IDXSTR namidx = Obj::addstr(symtab_strings,name);
    IDXSYM symidx = elf_addsym(namidx, 0, 0, STT_NOTYPE, STB_GLOBAL, SHT_UNDEF);
    return symidx;
}


/*******************************
 * Output an external for existing symbol.
 * Input:
 *      s       Symbol to do EXTDEF on
 *              (Name is to be mangled)
 * Returns:
 *      Symbol table index of the definition
 *      NOTE: Numbers will not be linear.
 */

int Obj::external(Symbol *s)
{
    int symtype,sectype;
    unsigned size;

    //dbg_printf("Obj::external('%s') %x\n",s->Sident,s->Svalue);
    symbol_debug(s);
    IDXSTR namidx = elf_addmangled(s);

#if SCPP
    if (s->Sscope && !tyfunc(s->ty()))
    {
        symtype = STT_OBJECT;
        sectype = SHT_COMMON;
        size = type_size(s->Stype);
    }
    else
#endif
    {
        symtype = STT_NOTYPE;
        sectype = SHT_UNDEF;
        size = 0;
    }
    if (s->ty() & mTYthread)
    {
        //printf("Obj::external('%s') %x TLS\n",s->Sident,s->Svalue);
        symtype = STT_TLS;
    }

    s->Sxtrnnum = elf_addsym(namidx, size, size, symtype,
        /*(s->ty() & mTYweak) ? STB_WEAK : */STB_GLOBAL, sectype);
    return s->Sxtrnnum;

}

/*******************************
 * Output a common block definition.
 * Input:
 *      p ->    external identifier
 *      size    size in bytes of each elem
 *      count   number of elems
 * Returns:
 *      Symbol table index for symbol
 */

int Obj::common_block(Symbol *s,targ_size_t size,targ_size_t count)
{
    //printf("Obj::common_block('%s',%d,%d)\n",s->Sident,size,count);
    symbol_debug(s);

    int align = I64 ? 16 : 4;
    if (s->ty() & mTYthread)
    {
        s->Sseg = ElfObj::getsegment(".tbss.", cpp_mangle(s),
                SHT_NOBITS, SHF_ALLOC|SHF_WRITE|SHF_TLS, align);
        s->Sfl = FLtlsdata;
        SegData[s->Sseg]->SDsym = s;
        SegData[s->Sseg]->SDoffset += size * count;
        Obj::pubdef(s->Sseg, s, 0);
        searchfixlist(s);
        return s->Sseg;
    }
    else
    {
        s->Sseg = ElfObj::getsegment(".bss.", cpp_mangle(s),
                SHT_NOBITS, SHF_ALLOC|SHF_WRITE, align);
        s->Sfl = FLudata;
        SegData[s->Sseg]->SDsym = s;
        SegData[s->Sseg]->SDoffset += size * count;
        Obj::pubdef(s->Sseg, s, 0);
        searchfixlist(s);
        return s->Sseg;
    }
#if 0
    IDXSTR namidx = elf_addmangled(s);
    alignOffset(UDATA,size);
    IDXSYM symidx = elf_addsym(namidx, SegData[UDATA]->SDoffset, size*count,
                    (s->ty() & mTYthread) ? STT_TLS : STT_OBJECT,
                    STB_WEAK, SHI_BSS);
    //dbg_printf("\tObj::common_block returning symidx %d\n",symidx);
    s->Sseg = UDATA;
    s->Sfl = FLudata;
    SegData[UDATA]->SDoffset += size * count;
    return symidx;
#endif
}

int Obj::common_block(Symbol *s, int flag, targ_size_t size, targ_size_t count)
{
    return Obj::common_block(s, size, count);
}

/***************************************
 * Append an iterated data block of 0s.
 * (uninitialized data only)
 */

void Obj::write_zeros(seg_data *pseg, targ_size_t count)
{
    Obj::lidata(pseg->SDseg, pseg->SDoffset, count);
}

/***************************************
 * Output an iterated data block of 0s.
 *
 *      For boundary alignment and initialization
 */

void Obj::lidata(int seg,targ_size_t offset,targ_size_t count)
{
    //printf("Obj::lidata(%d,%x,%d)\n",seg,offset,count);
    if (seg == UDATA || seg == UNKNOWN)
    {   // Use SDoffset to record size of .BSS section
        SegData[UDATA]->SDoffset += count;
    }
    else if (MAP_SEG2SEC(seg)->sh_type == SHT_NOBITS)
    {   // Use SDoffset to record size of .TBSS section
        SegData[seg]->SDoffset += count;
    }
    else
    {
        Obj::bytes(seg, offset, count, NULL);
    }
}

/***********************************
 * Append byte to segment.
 */

void Obj::write_byte(seg_data *pseg, unsigned byte)
{
    Obj::byte(pseg->SDseg, pseg->SDoffset, byte);
}

/************************************
 * Output byte to object file.
 */

void Obj::byte(int seg,targ_size_t offset,unsigned byte)
{
    Outbuffer *buf = SegData[seg]->SDbuf;
    int save = buf->size();
    //dbg_printf("Obj::byte(seg=%d, offset=x%lx, byte=x%x)\n",seg,offset,byte);
    buf->setsize(offset);
    buf->writeByte(byte);
    if (save > offset+1)
        buf->setsize(save);
    else
        SegData[seg]->SDoffset = offset+1;
    //dbg_printf("\tsize now %d\n",buf->size());
}

/***********************************
 * Append bytes to segment.
 */

void Obj::write_bytes(seg_data *pseg, unsigned nbytes, void *p)
{
    Obj::bytes(pseg->SDseg, pseg->SDoffset, nbytes, p);
}

/************************************
 * Output bytes to object file.
 * Returns:
 *      nbytes
 */

unsigned Obj::bytes(int seg, targ_size_t offset, unsigned nbytes, void *p)
{
#if 0
    if (!(seg >= 0 && seg <= seg_count))
    {   printf("Obj::bytes: seg = %d, seg_count = %d\n", seg, seg_count);
        *(char*)0=0;
    }
#endif
    assert(seg >= 0 && seg <= seg_count);
    Outbuffer *buf = SegData[seg]->SDbuf;
    if (buf == NULL)
    {
        //dbg_printf("Obj::bytes(seg=%d, offset=x%lx, nbytes=%d, p=x%x)\n", seg, offset, nbytes, p);
        //raise(SIGSEGV);
        assert(buf != NULL);
    }
    int save = buf->size();
    //dbg_printf("Obj::bytes(seg=%d, offset=x%lx, nbytes=%d, p=x%x)\n",
            //seg,offset,nbytes,p);
    buf->setsize(offset);
    buf->reserve(nbytes);
    if (p)
    {
        buf->writen(p,nbytes);
    }
    else
    {   // Zero out the bytes
        buf->clearn(nbytes);
    }
    if (save > offset+nbytes)
        buf->setsize(save);
    else
        SegData[seg]->SDoffset = offset+nbytes;
    return nbytes;
}

/*******************************
 * Output a relocation entry for a segment
 * Input:
 *      seg =           where the address is going
 *      offset =        offset within seg
 *      type =          ELF relocation type RI_TYPE_XXXX
 *      index =         Related symbol table index
 *      val =           addend or displacement from address
 */

int relcnt=0;

void ElfObj::addrel(int seg, targ_size_t offset, unsigned type,
                                        IDXSYM symidx, targ_size_t val)
{
    seg_data *segdata;
    Outbuffer *buf;
    IDXSEC secidx;

    //assert(val == 0);
    relcnt++;
    //dbg_printf("%d-ElfObj::addrel(seg %d,offset x%x,type x%x,symidx %d,val %d)\n",
            //relcnt,seg, offset, type, symidx,val);

    assert(seg >= 0 && seg <= seg_count);
    segdata = SegData[seg];
    secidx = MAP_SEG2SECIDX(seg);
    assert(secidx != 0);

    if (segdata->SDrel == NULL)
        segdata->SDrel = new Outbuffer();
    if (segdata->SDrel->size() == 0)
    {   IDXSEC relidx;

        if (secidx == SHI_TEXT)
            relidx = SHI_RELTEXT;
        else if (secidx == SHI_DATA)
            relidx = SHI_RELDATA;
        else
        {
            // Get the section name, and make a copy because
            // elf_newsection() may reallocate the string buffer.
            char *section_name = (char *)GET_SECTION_NAME(secidx);
            int len = strlen(section_name) + 1;
            char *p = (char *)alloca(len);
            memcpy(p, section_name, len);

            relidx = elf_newsection(I64 ? ".rela" : ".rel", p, I64 ? SHT_RELA : SHT_REL, 0);
            segdata->SDrelidx = relidx;
        }

        if (I64)
        {
            /* Note that we're using Elf32_Shdr here instead of Elf64_Shdr. This is to make
             * the code a bit simpler. In Obj::term(), we translate the Elf32_Shdr into the proper
             * Elf64_Shdr.
             */
            Elf32_Shdr *relsec = &SecHdrTab[relidx];
            relsec->sh_link = SHI_SYMTAB;
            relsec->sh_info = secidx;
            relsec->sh_entsize = sizeof(Elf64_Rela);
            relsec->sh_addralign = 8;
        }
        else
        {
            Elf32_Shdr *relsec = &SecHdrTab[relidx];
            relsec->sh_link = SHI_SYMTAB;
            relsec->sh_info = secidx;
            relsec->sh_entsize = sizeof(Elf32_Rel);
            relsec->sh_addralign = 4;
        }
    }

    if (I64)
    {
        Elf64_Rela rel;
        rel.r_offset = offset;          // build relocation information
        rel.r_info = ELF64_R_INFO(symidx,type);
        rel.r_addend = val;
        buf = segdata->SDrel;
        buf->write(&rel,sizeof(rel));
        segdata->SDrelcnt++;

        if (offset >= segdata->SDrelmaxoff)
            segdata->SDrelmaxoff = offset;
        else
        {   // insert numerically
            Elf64_Rela *relbuf = (Elf64_Rela *)buf->buf;
            int i = relbuf[segdata->SDrelindex].r_offset > offset ? 0 : segdata->SDrelindex;
            while (i < segdata->SDrelcnt)
            {
                if (relbuf[i].r_offset > offset)
                    break;
                i++;
            }
            assert(i != segdata->SDrelcnt);     // slide greater offsets down
            memmove(relbuf+i+1,relbuf+i,sizeof(Elf64_Rela) * (segdata->SDrelcnt - i - 1));
            *(relbuf+i) = rel;          // copy to correct location
            segdata->SDrelindex = i;    // next entry usually greater
        }
    }
    else
    {
        Elf32_Rel rel;
        rel.r_offset = offset;          // build relocation information
        rel.r_info = ELF32_R_INFO(symidx,type);
        buf = segdata->SDrel;
        buf->write(&rel,sizeof(rel));
        segdata->SDrelcnt++;

        if (offset >= segdata->SDrelmaxoff)
            segdata->SDrelmaxoff = offset;
        else
        {   // insert numerically
            Elf32_Rel *relbuf = (Elf32_Rel *)buf->buf;
            int i = relbuf[segdata->SDrelindex].r_offset > offset ? 0 : segdata->SDrelindex;
            while (i < segdata->SDrelcnt)
            {
                if (relbuf[i].r_offset > offset)
                    break;
                i++;
            }
            assert(i != segdata->SDrelcnt);     // slide greater offsets down
            memmove(relbuf+i+1,relbuf+i,sizeof(Elf32_Rel) * (segdata->SDrelcnt - i - 1));
            *(relbuf+i) = rel;          // copy to correct location
            segdata->SDrelindex = i;    // next entry usually greater
        }
    }
}

/*******************************
 * Refer to address that is in the data segment.
 * Input:
 *      seg =           where the address is going
 *      offset =        offset within seg
 *      val =           displacement from address
 *      targetdatum =   DATA, CDATA or UDATA, depending where the address is
 *      flags =         CFoff, CFseg, CFoffset64
 * Example:
 *      int *abc = &def[3];
 *      to allocate storage:
 *              Obj::reftodatseg(DATA,offset,3 * sizeof(int *),UDATA);
 */

void Obj::reftodatseg(int seg,targ_size_t offset,targ_size_t val,
        unsigned targetdatum,int flags)
{
    Outbuffer *buf;
    int save;

    buf = SegData[seg]->SDbuf;
    save = buf->size();
    buf->setsize(offset);
#if 0
    printf("Obj::reftodatseg(seg=%d, offset=x%llx, val=x%llx,data %x, flags %x)\n",
        seg,(unsigned long long)offset,(unsigned long long)val,targetdatum,flags);
#endif
    /*if (OPT_IS_SET(OPTfwritable_strings))
    {
        ElfObj::addrel(seg,offset,RI_TYPE_SYM32,STI_DATA,0);
    }
    else*/
    {
        int relinfo;
        targ_size_t v = 0;

        if (I64)
        {
            if (flags & CFoffset64)
            {
                relinfo = R_X86_64_64;
                ElfObj::addrel(seg, offset, relinfo, STI_RODAT, val);
                buf->write64(0);
                if (save > offset + 8)
                    buf->setsize(save);
                return;
            }
            else if (MAP_SEG2TYP(seg) == CODE && config.flags3 & CFG3pic)
            {   relinfo = R_X86_64_PC32;
                //v = -4L;
            }
            else if (MAP_SEG2SEC(targetdatum)->sh_flags & SHF_TLS)
                relinfo = config.flags3 & CFG3pic ? R_X86_64_TLSGD : R_X86_64_TPOFF32;
            else
                relinfo = R_X86_64_32;
        }
        else
        {
            if (MAP_SEG2TYP(seg) == CODE && config.flags3 & CFG3pic)
                relinfo = RI_TYPE_GOTOFF;
            else if (MAP_SEG2SEC(targetdatum)->sh_flags & SHF_TLS)
                relinfo = config.flags3 & CFG3pic ? RI_TYPE_TLS_GD : RI_TYPE_TLS_LE;
            else
                relinfo = RI_TYPE_SYM32;
        }

        ElfObj::addrel(seg, offset, relinfo, STI_RODAT, v);
    }
    buf->write32(val);
    if (save > offset + 4)
        buf->setsize(save);
}

/*******************************
 * Refer to address that is in the code segment.
 * Only offsets are output, regardless of the memory model.
 * Used to put values in switch address tables.
 * Input:
 *      seg =           where the address is going (CODE or DATA)
 *      offset =        offset within seg
 *      val =           displacement from start of this module
 */

void Obj::reftocodeseg(int seg,targ_size_t offset,targ_size_t val)
{
    Outbuffer *buf;
    int save;
    int segtyp = MAP_SEG2TYP(seg);

    //dbg_printf("Obj::reftocodeseg(seg=%d, offset=x%lx, val=x%lx )\n",seg,offset,val);
    assert(seg > 0);            // COMDATs not done yet
    buf = SegData[seg]->SDbuf;
    save = buf->size();
    buf->setsize(offset);
#if 0
    if (segtyp == CODE)
    {
        val = val - funcsym_p->Soffset;
        ElfObj::addrel(seg,offset,RI_TYPE_PC32,funcsym_p->Sxtrnnum,0);
    }
    else
#endif
    {
        val = val - funcsym_p->Soffset;
        int relinfo;
        targ_size_t v = 0;
        if (I64)
            relinfo = (config.flags3 & CFG3pic) ? R_X86_64_PC32 : R_X86_64_32;
        else
            relinfo = (config.flags3 & CFG3pic) ? RI_TYPE_GOTOFF : RI_TYPE_SYM32;
        ElfObj::addrel(seg,offset, relinfo, funcsym_p->Sxtrnnum, v);
    }
    buf->write32(val);
    if (save > offset + 4)
        buf->setsize(save);
}

/*******************************
 * Refer to an identifier.
 * Input:
 *      segtyp =        where the address is going (CODE or DATA)
 *      offset =        offset within seg
 *      s ->            Symbol table entry for identifier
 *      val =           displacement from identifier
 *      flags =         CFselfrel: self-relative
 *                      CFseg: get segment
 *                      CFoff: get offset
 *                      CFoffset64: 64 bit fixup
 *                      CFpc32: I64: PC relative 32 bit fixup
 * Returns:
 *      number of bytes in reference (4 or 8)
 */

int Obj::reftoident(int seg, targ_size_t offset, Symbol *s, targ_size_t val,
        int flags)
{
    bool external = TRUE;
    Outbuffer *buf;
    elf_u32_f32 relinfo,refseg;
    int segtyp = MAP_SEG2TYP(seg);
    //assert(val == 0);
    int retsize = (flags & CFoffset64) ? 8 : 4;
    targ_size_t v = 0;

#if 0
    printf("\nObj::reftoident('%s' seg %d, offset x%llx, val x%llx, flags x%x)\n",
        s->Sident,seg,offset,val,flags);
    dbg_printf("Sseg = %d, Sxtrnnum = %d, retsize = %d\n",s->Sseg,s->Sxtrnnum,retsize);
    symbol_print(s);
#endif

    tym_t ty = s->ty();
    if (s->Sxtrnnum)
    {                           // identifier is defined somewhere else
        if (I64)
        {
            if (SymbolTable64[s->Sxtrnnum].st_shndx != SHT_UNDEF)
                external = FALSE;
        }
        else
        {
            if (SymbolTable[s->Sxtrnnum].st_shndx != SHT_UNDEF)
                external = FALSE;
        }
    }

    switch (s->Sclass)
    {
        case SClocstat:
            buf = SegData[seg]->SDbuf;
            if (I64)
            {
                if (s->Sfl == FLtlsdata)
                    relinfo = config.flags3 & CFG3pic ? R_X86_64_TLSGD : R_X86_64_TPOFF32;
                else
                {   relinfo = config.flags3 & CFG3pic ? R_X86_64_PC32 : R_X86_64_32;
                    if (flags & CFpc32)
                        relinfo = R_X86_64_PC32;
                    if (relinfo == R_X86_64_PC32)
                    {
                        assert(retsize == 4);
                        if (val > 0xFFFFFFFF)
                        {   /* The value to be added is bigger than 32 bits, so we
                             * transfer it to the 64 bit addend of the fixup record
                             */
                            v = val;
                            val = 0;
                        }
                    }
                }
            }
            else
            {
                if (s->Sfl == FLtlsdata)
                    relinfo = config.flags3 & CFG3pic ? RI_TYPE_TLS_GD : RI_TYPE_TLS_LE;
                else
                    relinfo = config.flags3 & CFG3pic ? RI_TYPE_GOTOFF : RI_TYPE_SYM32;
            }
            if (flags & CFoffset64 && relinfo == R_X86_64_32)
            {
                relinfo = R_X86_64_64;
                ElfObj::addrel(seg,offset,relinfo,STI_RODAT,val + s->Soffset);
                buf->write64(0);
            }
            else
            {
                ElfObj::addrel(seg,offset,relinfo,STI_RODAT,v);
                if (retsize == 8)
                    buf->write64(val + s->Soffset);
                else
                    buf->write32(val + s->Soffset);
            }
            break;

        case SCcomdat:
        case_SCcomdat:
        case SCstatic:
#if 0
            if ((s->Sflags & SFLthunk) && s->Soffset)
            {                   // A thunk symbol that has be defined
                assert(s->Sseg == seg);
                val = (s->Soffset+val) - (offset+4);
                goto outaddrval;
            }
            // FALL_THROUGH
#endif

        case SCextern:
        case SCcomdef:
        case_extern:
        case SCglobal:
            if (!s->Sxtrnnum)
            {   // not in symbol table yet - class might change
                //dbg_printf("\tadding %s to fixlist\n",s->Sident);
                addtofixlist(s,offset,seg,val,flags);
                return retsize;
            }
            else
            {
                int save;
                buf = SegData[seg]->SDbuf;
                save = buf->size();
                buf->setsize(offset);
                if (flags & CFselfrel)
                {               // only for function references within code segments
                    if (!external &&            // local definition found
                         s->Sseg == seg &&      // within same code segment
                          (!(config.flags3 & CFG3pic) ||        // not position indp code
                           s->Sclass == SCstatic)) // or is pic, but declared static
                    {                   // Can use PC relative
                        //dbg_printf("\tdoing PC relative\n");
                        val = (s->Soffset+val) - (offset+4);
                    }
                    else
                    {
                        //dbg_printf("\tadding relocation\n");
                        if (I64)
                        {   relinfo = config.flags3 & CFG3pic ?  R_X86_64_PLT32 : R_X86_64_PC32;
                            ElfObj::addrel(seg,offset, relinfo, s->Sxtrnnum, -4);
                            val = 0;
                        }
                        else
                        {   relinfo = config.flags3 & CFG3pic ?  RI_TYPE_PLT32 : RI_TYPE_PC32;
                            ElfObj::addrel(seg,offset, relinfo, s->Sxtrnnum, 0);
                            val = (targ_size_t)-4;
                        }
                    }
                }
                else
                {       // code to code code to data, data to code, data to data refs
                    refseg = s->Sxtrnnum;       // default to name symbol table entry
                    if (s->Sclass == SCstatic)
                    {                           // offset into .data or .bss seg
                        refseg = MAP_SEG2SYMIDX(s->Sseg);
                                                // use segment symbol table entry
                        val += s->Soffset;
                        if (!(config.flags3 & CFG3pic) ||       // all static refs from normal code
                             segtyp == DATA)    // or refs from data from posi indp
                        {
                            if (I64)
                                relinfo = (flags & CFpc32) ? R_X86_64_PC32 : R_X86_64_32;
                            else
                                relinfo = RI_TYPE_SYM32;
                        }
                        else
                        {
                            relinfo = I64 ? R_X86_64_PC32 : RI_TYPE_GOTOFF;
                        }
                    }
                    else if (config.flags3 & CFG3pic && s == GOTsym)
                    {                   // relocation for Gbl Offset Tab
                        relinfo =  I64 ? R_X86_64_NONE : RI_TYPE_GOTPC;
                    }
                    else if (segtyp == DATA)
                    {                   // relocation from within DATA seg
                        relinfo = I64 ? R_X86_64_32 : RI_TYPE_SYM32;
                    }
                    else
                    {                   // relocation from within CODE seg
                        if (I64)
                        {   if (config.flags3 & CFG3pic)
                                relinfo = R_X86_64_GOTPCREL;
                            else
                                relinfo = (flags & CFpc32) ? R_X86_64_PC32 : R_X86_64_32;
                        }
                        else
                            relinfo = config.flags3 & CFG3pic ? RI_TYPE_GOT32 : RI_TYPE_SYM32;
                    }
                    if ((s->ty() & mTYLINK) & mTYthread)
                    {
                        if (I64)
                        {
                            if (config.flags3 & CFG3pic)
                            {
                                if (s->Sclass == SCstatic || s->Sclass == SClocstat)
                                    relinfo = R_X86_64_TLSGD;  // TLS_GD?
                                else
                                    relinfo = R_X86_64_TLSGD;
                            }
                            else
                            {
                                if (s->Sclass == SCstatic || s->Sclass == SClocstat)
                                    relinfo = R_X86_64_TPOFF32;
                                else
                                    relinfo = R_X86_64_GOTTPOFF;
                            }
                        }
                        else
                        {
                            if (config.flags3 & CFG3pic)
                            {
                                if (s->Sclass == SCstatic)
                                    relinfo = RI_TYPE_TLS_LE;  // TLS_GD?
                                else
                                    relinfo = RI_TYPE_TLS_IE;
                            }
                            else
                            {
                                if (s->Sclass == SCstatic)
                                    relinfo = RI_TYPE_TLS_LE;
                                else
                                    relinfo = RI_TYPE_TLS_IE;
                            }
                        }
                    }
                    targ_size_t v = 0;
                    if (flags & CFoffset64 && relinfo == R_X86_64_32)
                    {
                        // The value to be added must only reside in the 64 bit addend.
                        relinfo = R_X86_64_64;
                        v = val;
                        val = 0;
                    }
                    //printf("\t\t************* adding relocation\n");
                    if (I64 && retsize == 4)
                    {
                        assert(retsize == 4);
                        if (val > 0xFFFFFFFF)
                        {   /* The value to be added is bigger than 32 bits, so we
                             * transfer it to the 64 bit addend of the fixup record
                             */
                            v = val;
                            val = 0;
                        }
                    }
#if 0
                    targ_size_t v = (relinfo == R_X86_64_PC32) ? -4 : 0;
                    if (relinfo == R_X86_64_PC32 && flags & CFaddend8)
                        v = -8;
#endif
                    assert(!(I64 && relinfo == R_X86_64_64) || val == 0);
                    ElfObj::addrel(seg,offset,relinfo,refseg,v);
                }
outaddrval:
                if (retsize == 8)
                    buf->write64(val);
                else
                    buf->write32(val);
                if (save > offset + retsize)
                    buf->setsize(save);
            }
            break;

        case SCsinline:
        case SCeinline:
            printf ("Undefined inline value <<fixme>>\n");
            //warerr(WM_undefined_inline,s->Sident);
        case SCinline:
            if (tyfunc(ty))
            {
                s->Sclass = SCextern;
                goto case_extern;
            }
            else if (config.flags2 & CFG2comdat)
                goto case_SCcomdat;     // treat as initialized common block

        default:
#ifdef DEBUG
            //symbol_print(s);
#endif
            assert(0);
    }
    return retsize;
}

/*****************************************
 * Generate far16 thunk.
 * Input:
 *      s       Symbol to generate a thunk for
 */

void Obj::far16thunk(Symbol *s)
{
    //dbg_printf("Obj::far16thunk('%s')\n", s->Sident);
    assert(0);
}

/**************************************
 * Mark object file as using floating point.
 */

void Obj::fltused()
{
    //dbg_printf("Obj::fltused()\n");
}

/************************************
 * Close and delete .OBJ file.
 */

void objfile_delete()
{
    //remove(fobjname); // delete corrupt output file
}

/**********************************
 * Terminate.
 */

void objfile_term()
{
#if TERMCODE
    mem_free(fobjname);
    fobjname = NULL;
#endif
}

/**********************************
  * Write to the object file
  */
void objfile_write(FILE *fd, void *buffer, unsigned len)
{
    fobjbuf->write(buffer, len);
}

long elf_align(targ_size_t size,long foffset)
{
    if (size <= 1)
        return foffset;
    long offset = (foffset + size - 1) & ~(size - 1);
    if (offset > foffset)
        fobjbuf->writezeros(offset - foffset);
    return offset;
}

/***************************************
 * Stuff pointer to ModuleInfo in its own segment.
 */

#if MARS

void Obj::moduleinfo(Symbol *scc)
{
//    if (I64) return;          // for now, until Phobos64 works

    int codeOffset, refOffset;

    /* Put in the ModuleReference. */
    {
        /* struct ModuleReference
         * {
         *      void*   next;
         *      ModuleReference* module;
         * }
         */
        const int seg = DATA;
        alignOffset(seg, NPTRSIZE);
        SegData[seg]->SDoffset = SegData[seg]->SDbuf->size();
        refOffset = SegData[seg]->SDoffset;
        SegData[seg]->SDbuf->writezeros(NPTRSIZE);
        SegData[seg]->SDoffset += NPTRSIZE;
        SegData[seg]->SDoffset += Obj::reftoident(seg, SegData[seg]->SDoffset, scc, 0, CFoffset64 | CFoff);
    }

    {
        const int seg = CODE;
        Outbuffer *buf = SegData[seg]->SDbuf;
        SegData[seg]->SDoffset = buf->size();
        codeOffset = SegData[seg]->SDoffset;

        cod3_buildmodulector(buf, codeOffset, refOffset);

        SegData[seg]->SDoffset = buf->size();
    }

    /* Add reference to constructor into ".ctors" segment
     */
    const int seg = ElfObj::getsegment(".ctors", NULL, SHT_PROGDEF, SHF_ALLOC|SHF_WRITE, NPTRSIZE);

    Outbuffer *buf = SegData[seg]->SDbuf;
    if (I64)
    {
        ElfObj::addrel(seg, SegData[seg]->SDoffset, R_X86_64_64, STI_TEXT, codeOffset);
        buf->write64(0);
    }
    else
    {
        ElfObj::addrel(seg, SegData[seg]->SDoffset, RI_TYPE_SYM32, STI_TEXT, 0);
        buf->write32(codeOffset);
    }
    SegData[seg]->SDoffset += NPTRSIZE;
}

#endif

/*************************************
 */

void Obj::gotref(symbol *s)
{
    //printf("Obj::gotref(%x '%s', %d)\n",s,s->Sident, s->Sclass);
    switch(s->Sclass)
    {
        case SCstatic:
        case SClocstat:
            s->Sfl = FLgotoff;
            break;

        case SCextern:
        case SCglobal:
        case SCcomdat:
        case SCcomdef:
            s->Sfl = FLgot;
            break;

        default:
            break;
    }
}

#endif
#endif
