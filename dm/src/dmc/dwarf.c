// Compiler implementation of the D programming language
// Copyright (c) 1999-2016 by Digital Mars
// All Rights Reserved
// written by Walter Bright
// http://www.digitalmars.com
// Distributed under the Boost Software License, Version 1.0.
// http://www.boost.org/LICENSE_1_0.txt
// https://github.com/dlang/dmd/blob/master/src/backend/dwarf.c

// Emit Dwarf symbolic debug info

#if !SPP
#include        <stdio.h>
#include        <string.h>
#include        <stdlib.h>
#include        <sys/types.h>
#include        <sys/stat.h>
#include        <fcntl.h>
#include        <ctype.h>

#if __DMC__ || __linux__
#include        <malloc.h>
#endif

#if __linux__ || __APPLE__ || __FreeBSD__ || __OpenBSD__ || __sun
#include        <signal.h>
#include        <unistd.h>
#include        <errno.h>
#endif

#include        "cc.h"
#include        "global.h"
#include        "code.h"
#include        "type.h"
#include        "outbuf.h"
#include        "filespec.h"
#include        "cv4.h"
#include        "cgcv.h"
#include        "dt.h"
#include        "rtlsym.h"

#include        "aa.h"
#include        "tinfo.h"

#if ELFOBJ
#include        "melf.h"
#endif
#if MACHOBJ
#include        "mach.h"
#endif

#if ELFOBJ || MACHOBJ

#if MARS
#include        "mars.h"
#endif

#include        "dwarf.h"
#include        "dwarf2.h"

extern int seg_count;

#if MACHOBJ
int except_table_seg = 0;       // __gcc_except_tab segment
int except_table_num = 0;       // sequence number for GCC_except_table%d symbols
int eh_frame_seg = 0;           // __eh_frame segment
Symbol *eh_frame_sym = NULL;            // past end of __eh_frame
#endif
unsigned CIE_offset_unwind;     // CIE offset for unwind data
unsigned CIE_offset_no_unwind;  // CIE offset for no unwind data

#if ELFOBJ
IDXSYM elf_addsym(IDXSTR nam, targ_size_t val, unsigned sz,
        unsigned typ, unsigned bind, IDXSEC sec,
        unsigned char visibility = STV_DEFAULT);
#endif

static Outbuffer  *reset_symbuf;        // Keep pointers to reset symbols

static char __file__[] = __FILE__;      // for tassert.h
#include        "tassert.h"

/***********************************
 * Determine if generating a eh_frame with full
 * unwinding information.
 * This decision is done on a per-function basis.
 * Returns:
 *      true if unwinding needs to be done
 */
bool doUnwindEhFrame()
{
    /* FreeBSD fails when having some frames as having unwinding info and some not.
     * (It hangs in unittests for std.datetime.)
     * g++ on FreeBSD does not generate mixed frames, while g++ on OSX and Linux does.
     */
    assert(!(usednteh & ~(EHtry | EHcleanup)));
    return (usednteh & (EHtry | EHcleanup)) || (config.exe & (EX_FREEBSD | EX_FREEBSD64));
}

#if ELFOBJ
#define MAP_SEG2SYMIDX(seg) (SegData[seg]->SDsymidx)
#else
#define MAP_SEG2SYMIDX(seg) (assert(0))
#endif

#define OFFSET_FAC REGSIZE

int dwarf_getsegment(const char *name, int align)
{
#if ELFOBJ
    return ElfObj::getsegment(name, NULL, SHT_PROGBITS, 0, align * 4);
#elif MACHOBJ
    return MachObj::getsegment(name, "__DWARF", align * 2, S_ATTR_DEBUG);
#else
    assert(0);
    return 0;
#endif
}

int dwarf_getsegment_alloc(const char *name, int align)
{
#if ELFOBJ
    return ElfObj::getsegment(name, NULL, SHT_PROGBITS, SHF_ALLOC, align * 4);
#elif MACHOBJ
    return MachObj::getsegment(name, "__DWARF", align * 2, S_ATTR_DEBUG);
#else
    assert(0);
    return 0;
#endif
}

int dwarf_except_table_alloc()
{
#if ELFOBJ
    return dwarf_getsegment_alloc(".gcc_except_table", 1);
#elif MACHOBJ
    int seg = MachObj::getsegment("__gcc_except_tab", "__TEXT", 2, S_REGULAR);
    except_table_seg = seg;
    return seg;
#else
    assert(0);
    return 0;
#endif
}

int dwarf_eh_frame_alloc()
{
#if ELFOBJ
    return dwarf_getsegment_alloc(".eh_frame", I64 ? 2 : 1);
#elif MACHOBJ
    int seg = MachObj::getsegment("__eh_frame", "__TEXT", I64 ? 3 : 2,
        S_COALESCED | S_ATTR_NO_TOC | S_ATTR_STRIP_STATIC_SYMS | S_ATTR_LIVE_SUPPORT);
    /* Generate symbol for it to use for fixups
     */
    if (!eh_frame_sym)
    {
        type *t = tspvoid;
        t->Tcount++;
        type_setmangle(&t, mTYman_sys);         // no leading '_' for mangled name
        eh_frame_sym = symbol_name("EH_frame0", SCstatic, t);
        Obj::pubdef(seg, eh_frame_sym, 0);
        symbol_keep(eh_frame_sym);
        eh_frame_seg = seg;
    }
    return seg;
#else
    assert(0);
    return 0;
#endif
}

// machobj.c
#define RELaddr 0       // straight address
#define RELrel  1       // relative to location to be fixed up

void dwarf_addrel(int seg, targ_size_t offset, int targseg, targ_size_t val)
{
#if ELFOBJ
    ElfObj::addrel(seg, offset, I64 ? R_X86_64_32 : R_386_32, MAP_SEG2SYMIDX(targseg), val);
#elif MACHOBJ
    MachObj::addrel(seg, offset, NULL, targseg, RELaddr, val);
#else
    assert(0);
#endif
}

void dwarf_addrel64(int seg, targ_size_t offset, int targseg, targ_size_t val)
{
#if ELFOBJ
    ElfObj::addrel(seg, offset, R_X86_64_64, MAP_SEG2SYMIDX(targseg), val);
#elif MACHOBJ
    MachObj::addrel(seg, offset, NULL, targseg, RELaddr, val);
#else
    assert(0);
#endif
}

void dwarf_appreladdr(int seg, Outbuffer *buf, int targseg, targ_size_t val)
{
    if (I64)
    {
        dwarf_addrel64(seg, buf->size(), targseg, val);
        buf->write64(0);
    }
    else
    {
        dwarf_addrel(seg, buf->size(), targseg, 0);
        buf->write32(val);
    }
}

void dwarf_apprel32(int seg, Outbuffer *buf, int targseg, targ_size_t val)
{
    dwarf_addrel(seg, buf->size(), targseg, I64 ? val : 0);
    buf->write32(I64 ? 0 : val);
}

void append_addr(Outbuffer *buf, targ_size_t addr)
{
    if (I64)
        buf->write64(addr);
    else
        buf->write32(addr);
}


/************************  DWARF DEBUG OUTPUT ********************************/

// Dwarf Symbolic Debugging Information

// CFA = value of the stack pointer at the call site in the previous frame

struct CFA_reg
{
    int offset;                 // offset from CFA
};

// Current CFA state for .debug_frame
struct CFA_state
{
    size_t location;
    int reg;                    // CFA register number
    int offset;                 // CFA register offset
    CFA_reg regstates[17];      // register states
};

#if TX86
/***********************
 * Convert CPU register number to Dwarf register number.
 * Params:
 *      reg = CPU register
 * Returns:
 *      dwarf register
 */
int dwarf_regno(int reg)
{
    assert(reg < NUMGENREGS);
    if (I32)
    {
#if MACHOBJ
        if (reg == BP || reg == SP)
            reg ^= BP ^ SP;     // swap EBP and ESP register values for OSX (!)
#endif
        return reg;
    }
    else
    {
        assert(I64);
        /* See https://software.intel.com/sites/default/files/article/402129/mpx-linux64-abi.pdf
         * Figure 3.3.8 pg. 62
         * R8..15    :  8..15
         * XMM0..15  : 17..32
         * ST0..7    : 33..40
         * MM0..7    : 41..48
         * XMM16..31 : 67..82
         */
        static const int to_amd64_reg_map[8] =
        // AX CX DX BX SP BP SI DI
        {   0, 2, 1, 3, 7, 6, 4, 5 };
        return reg < 8 ? to_amd64_reg_map[reg] : reg;
    }
}
#endif

static CFA_state CFA_state_init_32 =       // initial CFA state as defined by CIE
{   0,                // location
    -1,               // register
    4,                // offset
    {   { 0 },        // 0: EAX
        { 0 },        // 1: ECX
        { 0 },        // 2: EDX
        { 0 },        // 3: EBX
        { 0 },        // 4: ESP
        { 0 },        // 5: EBP
        { 0 },        // 6: ESI
        { 0 },        // 7: EDI
        { -4 },       // 8: EIP
    }
};

static CFA_state CFA_state_init_64 =       // initial CFA state as defined by CIE
{   0,                // location
    -1,               // register
    8,                // offset
    {   { 0 },        // 0: RAX
        { 0 },        // 1: RBX
        { 0 },        // 2: RCX
        { 0 },        // 3: RDX
        { 0 },        // 4: RSI
        { 0 },        // 5: RDI
        { 0 },        // 6: RBP
        { 0 },        // 7: RSP
        { 0 },        // 8: R8
        { 0 },        // 9: R9
        { 0 },        // 10: R10
        { 0 },        // 11: R11
        { 0 },        // 12: R12
        { 0 },        // 13: R13
        { 0 },        // 14: R14
        { 0 },        // 15: R15
        { -8 },       // 16: RIP
    }
};

static CFA_state CFA_state_current;     // current CFA state
static Outbuffer cfa_buf;               // CFA instructions

/***********************************
 * Set the location, i.e. the offset from the start
 * of the function. It must always be greater than
 * the current location.
 * Params:
 *      location = offset from the start of the function
 */
void dwarf_CFA_set_loc(size_t location)
{
    assert(location >= CFA_state_current.location);
    size_t inc = location - CFA_state_current.location;
    if (inc <= 63)
        cfa_buf.writeByte(DW_CFA_advance_loc + inc);
    else if (inc <= 255)
    {   cfa_buf.writeByte(DW_CFA_advance_loc1);
        cfa_buf.writeByte(inc);
    }
    else if (inc <= 0xFFFF)
    {   cfa_buf.writeByte(DW_CFA_advance_loc2);
        cfa_buf.writeWord(inc);
    }
    else
    {   cfa_buf.writeByte(DW_CFA_advance_loc4);
        cfa_buf.write32(inc);
    }
    CFA_state_current.location = location;
}

/*******************************************
 * Set the frame register, and its offset.
 * Params:
 *      reg = machine register
 *      offset = offset from frame register
 */
void dwarf_CFA_set_reg_offset(int reg, int offset)
{
    int dw_reg = dwarf_regno(reg);
    if (dw_reg != CFA_state_current.reg)
    {
        if (offset == CFA_state_current.offset)
        {
            cfa_buf.writeByte(DW_CFA_def_cfa_register);
            cfa_buf.writeuLEB128(dw_reg);
        }
        else if (offset < 0)
        {
            cfa_buf.writeByte(DW_CFA_def_cfa_sf);
            cfa_buf.writeuLEB128(dw_reg);
            cfa_buf.writesLEB128(offset / -OFFSET_FAC);
        }
        else
        {
            cfa_buf.writeByte(DW_CFA_def_cfa);
            cfa_buf.writeuLEB128(dw_reg);
            cfa_buf.writeuLEB128(offset);
        }
    }
    else if (offset < 0)
    {
        cfa_buf.writeByte(DW_CFA_def_cfa_offset_sf);
        cfa_buf.writesLEB128(offset / -OFFSET_FAC);
    }
    else
    {
        cfa_buf.writeByte(DW_CFA_def_cfa_offset);
        cfa_buf.writeuLEB128(offset);
    }
    CFA_state_current.reg = dw_reg;
    CFA_state_current.offset = offset;
}

/***********************************************
 * Set reg to be at offset from frame register.
 * Params:
 *      reg = machine register
 *      offset = offset from frame register
 */
void dwarf_CFA_offset(int reg, int offset)
{
    int dw_reg = dwarf_regno(reg);
    if (CFA_state_current.regstates[dw_reg].offset != offset)
    {
        if (offset <= 0)
        {
            cfa_buf.writeByte(DW_CFA_offset + dw_reg);
            cfa_buf.writeuLEB128(offset / -OFFSET_FAC);
        }
        else
        {
            cfa_buf.writeByte(DW_CFA_offset_extended_sf);
            cfa_buf.writeuLEB128(dw_reg);
            cfa_buf.writesLEB128(offset / -OFFSET_FAC);
        }
    }
    CFA_state_current.regstates[dw_reg].offset = offset;
}

/**************************************
 * Set total size of arguments pushed on the stack.
 * Params:
 *      sz = total size
 */
void dwarf_CFA_args_size(size_t sz)
{
    cfa_buf.writeByte(DW_CFA_GNU_args_size);
    cfa_buf.writeuLEB128(sz);
}

// .debug_pubnames
static IDXSEC debug_pubnames_secidx;
static Outbuffer *debug_pubnames_buf;

// .debug_aranges
static IDXSEC debug_aranges_seg;
static IDXSEC debug_aranges_secidx;
static Outbuffer *debug_aranges_buf;

// .debug_ranges
static IDXSEC debug_ranges_seg;
static IDXSEC debug_ranges_secidx;
static Outbuffer *debug_ranges_buf;

// .debug_loc
static IDXSEC debug_loc_seg;
static IDXSEC debug_loc_secidx;
static Outbuffer *debug_loc_buf;

// .debug_abbrev
static IDXSEC abbrevseg;
static Outbuffer *abbrevbuf;

/* DWARF 7.5.3: "Each declaration begins with an unsigned LEB128 number
 * representing the abbreviation code itself."
 */
static unsigned abbrevcode = 1;
static AArray *abbrev_table;
static int hasModname;    // 1 if has DW_TAG_module

// .debug_info
static IDXSEC infoseg;
static Outbuffer *infobuf;
static AArray *infoFileName_table;

static AArray *type_table;
static AArray *functype_table;  // not sure why this cannot be combined with type_table
static Outbuffer *functypebuf;

// typeinfo declarations for hash of char*

struct Abuf
{
    const unsigned char *buf;
    size_t length;
};

struct TypeInfo_Abuf : TypeInfo
{
    const char* toString();
    hash_t getHash(void *p);
    int equals(void *p1, void *p2);
    int compare(void *p1, void *p2);
    size_t tsize();
    void swap(void *p1, void *p2);
};

TypeInfo_Abuf ti_abuf;

const char* TypeInfo_Abuf::toString()
{
    return "Abuf";
}

hash_t TypeInfo_Abuf::getHash(void *p)
{
    Abuf a = *(Abuf *)p;

    hash_t hash = 0;
    for (size_t i = 0; i < a.length; i++)
        hash = hash * 11 + a.buf[i];

    return hash;
}

int TypeInfo_Abuf::equals(void *p1, void *p2)
{
    Abuf a1 = *(Abuf*)p1;
    Abuf a2 = *(Abuf*)p2;

    return a1.length == a2.length &&
        memcmp(a1.buf, a2.buf, a1.length) == 0;
}

int TypeInfo_Abuf::compare(void *p1, void *p2)
{
    Abuf a1 = *(Abuf*)p1;
    Abuf a2 = *(Abuf*)p2;

    if (a1.length == a2.length)
        return memcmp(a1.buf, a2.buf, a1.length);
    else if (a1.length < a2.length)
        return -1;
    else
        return 1;
}

size_t TypeInfo_Abuf::tsize()
{
    return sizeof(Abuf);
}

void TypeInfo_Abuf::swap(void *p1, void *p2)
{
    assert(0);
}

#pragma pack(1)
struct DebugInfoHeader
{   unsigned total_length;
    unsigned short version;
    unsigned abbrev_offset;
    unsigned char address_size;
};
#pragma pack()

static DebugInfoHeader debuginfo_init =
{       0,      // total_length
        3,      // version
        0,      // abbrev_offset
        4       // address_size
};

static DebugInfoHeader debuginfo;

// .debug_line
static IDXSEC lineseg;
static Outbuffer *linebuf;
static size_t linebuf_filetab_end;

#pragma pack(1)
struct DebugLineHeader
{   unsigned total_length;
    unsigned short version;
    unsigned prologue_length;
    unsigned char minimum_instruction_length;
    unsigned char default_is_stmt;
    signed char line_base;
    unsigned char line_range;
    unsigned char opcode_base;
    unsigned char standard_opcode_lengths[9];
};
#pragma pack()

static DebugLineHeader debugline_init =
{       0,      // total_length
        2,      // version
        0,      // prologue_length
        1,      // minimum_instruction_length
        TRUE,   // default_is_stmt
        -5,     // line_base
        14,     // line_range
        10,     // opcode_base
        { 0,1,1,1,1,0,0,0,1 }
};

static DebugLineHeader debugline;

unsigned typidx_tab[TYMAX];

#if MACHOBJ
const char* debug_frame_name = "__debug_frame";
const char* debug_str = "__debug_str";
const char* debug_ranges = "__debug_ranges";
const char* debug_loc = "__debug_loc";
const char* debug_line = "__debug_line";
const char* debug_abbrev = "__debug_abbrev";
const char* debug_info = "__debug_info";
const char* debug_pubnames = "__debug_pubnames";
const char* debug_aranges = "__debug_aranges";
#elif ELFOBJ
const char* debug_frame_name = ".debug_frame";
const char* debug_str = ".debug_str";
const char* debug_ranges = ".debug_ranges";
const char* debug_loc = ".debug_loc";
const char* debug_line = ".debug_line";
const char* debug_abbrev = ".debug_abbrev";
const char* debug_info = ".debug_info";
const char* debug_pubnames = ".debug_pubnames";
const char* debug_aranges = ".debug_aranges";
#endif

/*****************************************
 * Append .debug_frame header to buf.
 * Params:
 *      buf = write raw data here
 */
void writeDebugFrameHeader(Outbuffer *buf)
{
    #pragma pack(1)
    struct DebugFrameHeader
    {
        unsigned length;
        unsigned CIE_id;
        unsigned char version;
        unsigned char augmentation;
        unsigned char code_alignment_factor;
        unsigned char data_alignment_factor;
        unsigned char return_address_register;
        unsigned char opcodes[11];
    };
    #pragma pack()
    static DebugFrameHeader debugFrameHeader =
    {   16,             // length
        0xFFFFFFFF,     // CIE_id
        1,              // version
        0,              // augmentation
        1,              // code alignment factor
        0x7C,           // data alignment factor (-4)
        8,              // return address register
      {
        DW_CFA_def_cfa, 4,4,    // r4,4 [r7,8]
        DW_CFA_offset   +8,1,   // r8,1 [r16,1]
        DW_CFA_nop, DW_CFA_nop,
        DW_CFA_nop, DW_CFA_nop, // 64 padding
        DW_CFA_nop, DW_CFA_nop, // 64 padding
      }
    };
    if (I64)
    {   debugFrameHeader.length = 20;
        debugFrameHeader.data_alignment_factor = 0x78;          // (-8)
        debugFrameHeader.return_address_register = 16;
        debugFrameHeader.opcodes[1] = 7;                        // RSP
        debugFrameHeader.opcodes[2] = 8;
        debugFrameHeader.opcodes[3] = DW_CFA_offset + 16;       // RIP
    }
    assert(debugFrameHeader.data_alignment_factor == 0x80 - OFFSET_FAC);

    buf->writen(&debugFrameHeader,debugFrameHeader.length + 4);
}

/*****************************************
 * Append .eh_frame header to buf.
 * Almost identical to .debug_frame
 * Params:
 *      dfseg = SegData[] index for .eh_frame
 *      buf = write raw data here
 *      personality = "__dmd_personality_v0"
 *      ehunwind = will have EH unwind table
 * Returns:
 *      offset of start of this header
 * See_Also:
 *      https://refspecs.linuxfoundation.org/LSB_3.0.0/LSB-PDA/LSB-PDA/ehframechpt.html
 */
static unsigned writeEhFrameHeader(IDXSEC dfseg, Outbuffer *buf, Symbol *personality, bool ehunwind)
{
    /* Augmentation string:
     *  z = first character, means Augmentation Data field is present
     *  eh = EH Data field is present
     *  P = Augmentation Data contains 2 args:
     *          1. encoding of 2nd arg
     *          2. address of personality routine
     *  L = Augmentation Data contains 1 arg:
     *          1. the encoding used for Augmentation Data in FDE
     *      Augmentation Data in FDE:
     *          1. address of LSDA (gcc_except_table)
     *  R = Augmentation Data contains 1 arg:
     *          1. encoding of addresses in FDE
     * Non-EH code: "zR"
     * EH code: "zPLR"
     */

    const unsigned startsize = buf->size();

    // Length of CIE, not including padding
    const unsigned cielen = 4 + 4 + 1 +
        (ehunwind ? 5 : 3) +
        1 + 1 + 1 +
        (ehunwind ? 8 : 2) +
        5;

    const unsigned pad = -cielen & (I64 ? 7 : 3);      // pad to addressing unit size boundary
    const unsigned length = cielen + pad - 4;

    buf->reserve(length + 4);
    buf->write32(length);       // length of CIE, not including length and extended length fields
    buf->write32(0);            // CIE ID
    buf->writeByten(1);         // version
    if (ehunwind)
        buf->write("zPLR", 5);  // Augmentation String
    else
        buf->writen("zR", 3);
    // not present: EH Data: 4 bytes for I32, 8 bytes for I64
    buf->writeByten(1);                 // code alignment factor
    buf->writeByten(0x80 - OFFSET_FAC); // data alignment factor (I64 ? -8 : -4)
    buf->writeByten(I64 ? 16 : 8);      // return address register
    if (ehunwind)
    {
#if ELFOBJ
        const unsigned char personality_pointer_encoding = config.flags3 & CFG3pic
                ? DW_EH_PE_indirect | DW_EH_PE_pcrel | DW_EH_PE_sdata4
                : DW_EH_PE_absptr | DW_EH_PE_udata4;
        const unsigned char LSDA_pointer_encoding = config.flags3 & CFG3pic
                ? DW_EH_PE_pcrel | DW_EH_PE_sdata4
                : DW_EH_PE_absptr | DW_EH_PE_udata4;
        const unsigned char address_pointer_encoding =
                DW_EH_PE_pcrel | DW_EH_PE_sdata4;
#elif MACHOBJ
        const unsigned char personality_pointer_encoding =
                DW_EH_PE_indirect | DW_EH_PE_pcrel | DW_EH_PE_sdata4;
        const unsigned char LSDA_pointer_encoding =
                DW_EH_PE_pcrel | DW_EH_PE_ptr;
        const unsigned char address_pointer_encoding =
                DW_EH_PE_pcrel | DW_EH_PE_ptr;
#endif
        buf->writeByten(7);                                  // Augmentation Length
        buf->writeByten(personality_pointer_encoding);       // P: personality routine address encoding
        /* MACHOBJ 64: pcrel 1 length 2 extern 1 RELOC_GOT
         *         32: [4] address x0013 pcrel 0 length 2 value xfc type 4 RELOC_LOCAL_SECTDIFF
         *             [5] address x0000 pcrel 0 length 2 value xc7 type 1 RELOC_PAIR
         */
        dwarf_reftoident(dfseg, buf->size(), personality, 0);
        buf->writeByten(LSDA_pointer_encoding);              // L: address encoding for LSDA in FDE
        buf->writeByten(address_pointer_encoding);           // R: encoding of addresses in FDE
    }
    else
    {
        buf->writeByten(1);                                  // Augmentation Length
#if ELFOBJ
        buf->writeByten(DW_EH_PE_pcrel | DW_EH_PE_sdata4);   // R: encoding of addresses in FDE
#elif MACHOBJ
        buf->writeByten(DW_EH_PE_pcrel | DW_EH_PE_ptr);      // R: encoding of addresses in FDE
#endif
    }

    // Set CFA beginning state at function entry point
    if (I64)
    {
        buf->writeByten(DW_CFA_def_cfa);        // DEF_CFA r7,8   RSP is at offset 8
        buf->writeByten(7);                     // r7 is RSP
        buf->writeByten(8);

        buf->writeByten(DW_CFA_offset + 16);    // OFFSET r16,1   RIP is at -8*1[RSP]
        buf->writeByten(1);
    }
    else
    {
        buf->writeByten(DW_CFA_def_cfa);        // DEF_CFA ESP,4
        buf->writeByten(dwarf_regno(SP));
        buf->writeByten(4);

        buf->writeByten(DW_CFA_offset + 8);     // OFFSET r8,1
        buf->writeByten(1);
    }

    for (unsigned i = 0; i < pad; ++i)
        buf->writeByten(DW_CFA_nop);

    assert(startsize + length + 4 == buf->size());
    return startsize;
}

/*********************************************
 * Generate function's Frame Description Entry into .debug_frame
 * Params:
 *      dfseg = SegData[] index for .debug_frame
 *      sfunc = the function
 */
void writeDebugFrameFDE(IDXSEC dfseg, Symbol *sfunc)
{
    if (I64)
    {
        #pragma pack(1)
        struct DebugFrameFDE
        {
            unsigned length;
            unsigned CIE_pointer;
            unsigned long long initial_location;
            unsigned long long address_range;
        };
        #pragma pack()
        static DebugFrameFDE debugFrameFDE =
        {   20,             // length
            0,              // CIE_pointer
            0,              // initial_location
            0,              // address_range
        };

        // Pad to 8 byte boundary
        for (unsigned n = (-cfa_buf.size() & 7); n; n--)
            cfa_buf.writeByte(DW_CFA_nop);

        debugFrameFDE.length = 20 + cfa_buf.size();
        debugFrameFDE.address_range = sfunc->Ssize;
        // Do we need this?
        //debugFrameFDE.initial_location = sfunc->Soffset;

        Outbuffer *debug_frame_buf = SegData[dfseg]->SDbuf;
        unsigned debug_frame_buf_offset = debug_frame_buf->p - debug_frame_buf->buf;
        debug_frame_buf->reserve(1000);
        debug_frame_buf->writen(&debugFrameFDE,sizeof(debugFrameFDE));
        debug_frame_buf->write(&cfa_buf);

#if ELFOBJ
        // Absolute address for debug_frame, relative offset for eh_frame
        dwarf_addrel(dfseg,debug_frame_buf_offset + 4,dfseg);
#endif
        dwarf_addrel64(dfseg,debug_frame_buf_offset + 8,sfunc->Sseg,0);
    }
    else
    {
        #pragma pack(1)
        struct DebugFrameFDE
        {
            unsigned length;
            unsigned CIE_pointer;
            unsigned initial_location;
            unsigned address_range;
        };
        #pragma pack()
        static DebugFrameFDE debugFrameFDE =
        {   12,             // length
            0,              // CIE_pointer
            0,              // initial_location
            0,              // address_range
        };

        // Pad to 4 byte boundary
        for (unsigned n = (-cfa_buf.size() & 3); n; n--)
            cfa_buf.writeByte(DW_CFA_nop);

        debugFrameFDE.length = 12 + cfa_buf.size();
        debugFrameFDE.address_range = sfunc->Ssize;
        // Do we need this?
        //debugFrameFDE.initial_location = sfunc->Soffset;

        Outbuffer *debug_frame_buf = SegData[dfseg]->SDbuf;
        unsigned debug_frame_buf_offset = debug_frame_buf->p - debug_frame_buf->buf;
        debug_frame_buf->reserve(1000);
        debug_frame_buf->writen(&debugFrameFDE,sizeof(debugFrameFDE));
        debug_frame_buf->write(&cfa_buf);

#if ELFOBJ
        // Absolute address for debug_frame, relative offset for eh_frame
        dwarf_addrel(dfseg,debug_frame_buf_offset + 4,dfseg);
#endif
        dwarf_addrel(dfseg,debug_frame_buf_offset + 8,sfunc->Sseg);
    }
}

/*********************************************
 * Append function's FDE (Frame Description Entry) to .eh_frame
 * Params:
 *      dfseg = SegData[] index for .eh_frame
 *      sfunc = the function
 *      ehunwind = will have EH unwind table
 *      CIE_offset = offset of enclosing CIE
 */
void writeEhFrameFDE(IDXSEC dfseg, Symbol *sfunc, bool ehunwind, unsigned CIE_offset)
{
    Outbuffer *buf = SegData[dfseg]->SDbuf;
    const unsigned startsize = buf->size();

#if MACHOBJ
    /* Create symbol named "funcname.eh" for the start of the FDE
     */
    Symbol *fdesym;
    {
        const size_t len = strlen(sfunc->Sident);
        char *name = (char *)malloc(len + 3 + 1);
        if (!name)
            err_nomem();
        memcpy(name, sfunc->Sident, len);
        memcpy(name + len, ".eh", 3 + 1);
        fdesym = symbol_name(name, SCglobal, tspvoid);
        Obj::pubdef(dfseg, fdesym, startsize);
        symbol_keep(fdesym);
    }
#endif

    if (sfunc->ty() & mTYnaked)
    {
        /* Do not have info on naked functions. Assume they are set up as:
         *   push RBP
         *   mov  RSP,RSP
         */
        int off = 2 * REGSIZE;
        dwarf_CFA_set_loc(1);
        dwarf_CFA_set_reg_offset(SP, off);
        dwarf_CFA_offset(BP, -off);
        dwarf_CFA_set_loc(I64 ? 4 : 3);
        dwarf_CFA_set_reg_offset(BP, off);
    }

    // Length of FDE, not including padding
    const unsigned fdelen = 4 + 4
#if ELFOBJ
        + 4 + 4
        + (ehunwind ? 5 : 1) + cfa_buf.size();
#elif MACHOBJ
        + (I64 ? 8 + 8 : 4 + 4)                         // PC_Begin + PC_Range
        + (ehunwind ? (I64 ? 9 : 5) : 1) + cfa_buf.size();
#endif

    const unsigned pad = -fdelen & (I64 ? 7 : 3);      // pad to addressing unit size boundary
    const unsigned length = fdelen + pad - 4;

    buf->reserve(length + 4);
    buf->write32(length);                               // Length (no Extended Length)
    buf->write32((startsize + 4) - CIE_offset);         // CIE Pointer
#if ELFOBJ
    int fixup = I64 ? R_X86_64_PC32 : R_386_PC32;
    buf->write32(I64 ? 0 : sfunc->Soffset);             // address of function
    ElfObj::addrel(dfseg, startsize + 8, fixup, MAP_SEG2SYMIDX(sfunc->Sseg), sfunc->Soffset);
    //ElfObj::reftoident(dfseg, startsize + 8, sfunc, 0, CFpc32 | CFoff); // PC_begin
    buf->write32(sfunc->Ssize);                         // PC Range
#elif MACHOBJ
    dwarf_eh_frame_fixup(dfseg, buf->size(), sfunc, 0, fdesym);

    if (I64)
        buf->write64(sfunc->Ssize);                     // PC Range
    else
        buf->write32(sfunc->Ssize);                     // PC Range
#else
    assert(0);
#endif
    if (ehunwind)
    {
        int etseg = dwarf_except_table_alloc();
#if ELFOBJ
        buf->writeByten(4);                             // Augmentation Data Length
        buf->write32(I64 ? 0 : sfunc->Sfunc->LSDAoffset); // address of LSDA (".gcc_except_table")
        if (config.flags3 & CFG3pic)
        {
            ElfObj::addrel(dfseg, buf->size() - 4, fixup, MAP_SEG2SYMIDX(etseg), sfunc->Sfunc->LSDAoffset);
        }
        else
            dwarf_addrel(dfseg, buf->size() - 4, etseg, sfunc->Sfunc->LSDAoffset);      // and the fixup
#elif MACHOBJ
        buf->writeByten(I64 ? 8 : 4);                   // Augmentation Data Length
        dwarf_eh_frame_fixup(dfseg, buf->size(), sfunc->Sfunc->LSDAsym, 0, fdesym);
#endif
    }
    else
        buf->writeByten(0);                             // Augmentation Data Length

    buf->write(&cfa_buf);

    for (unsigned i = 0; i < pad; ++i)
        buf->writeByten(DW_CFA_nop);

    assert(startsize + length + 4 == buf->size());
}

void dwarf_initfile(const char *filename)
{
    if (config.ehmethod == EH_DWARF)
    {
#if MACHOBJ
        except_table_seg = 0;
        except_table_num = 0;
        eh_frame_seg = 0;
        eh_frame_sym = NULL;
#endif
        CIE_offset_unwind = ~0;
        CIE_offset_no_unwind = ~0;
        //dwarf_except_table_alloc();
        dwarf_eh_frame_alloc();
    }
    if (!config.fulltypes)
        return;
    if (config.ehmethod == EH_DM)
    {
        int seg = dwarf_getsegment(debug_frame_name, 1);
        Outbuffer *buf = SegData[seg]->SDbuf;
        buf->reserve(1000);
        writeDebugFrameHeader(buf);
    }

    /* ======================================== */

    if (reset_symbuf)
    {
        symbol **p = (symbol **)reset_symbuf->buf;
        const size_t n = reset_symbuf->size() / sizeof(symbol *);
        for (size_t i = 0; i < n; ++i)
            symbol_reset(p[i]);
        reset_symbuf->setsize(0);
    }
    else
    {
        reset_symbuf = new Outbuffer(10 * sizeof(symbol *));
    }

    /* ======================================== */

    int seg = dwarf_getsegment(debug_str, 0);
    Outbuffer *debug_str_buf = SegData[seg]->SDbuf;
    debug_str_buf->reserve(1000);

    /* ======================================== */

    debug_ranges_seg = dwarf_getsegment(debug_ranges, 0);
    debug_ranges_secidx = SegData[debug_ranges_seg]->SDshtidx;
    debug_ranges_buf = SegData[debug_ranges_seg]->SDbuf;
    debug_ranges_buf->reserve(1000);

    /* ======================================== */

    debug_loc_seg = dwarf_getsegment(debug_loc, 0);
    debug_loc_secidx = SegData[debug_loc_seg]->SDshtidx;
    debug_loc_buf = SegData[debug_loc_seg]->SDbuf;
    debug_loc_buf->reserve(1000);

    /* ======================================== */

    if (infoFileName_table)
    {   delete infoFileName_table;
        infoFileName_table = NULL;
    }

    lineseg = dwarf_getsegment(debug_line, 0);
    linebuf = SegData[lineseg]->SDbuf;

    debugline = debugline_init;

    linebuf->write(&debugline, sizeof(debugline));

    // include_directories
#if SCPP
    for (size_t i = 0; i < pathlist.length(); ++i)
    {
        linebuf->writeString(pathlist[i]);
        linebuf->writeByte(0);
    }
#endif
#if 0 && MARS
    for (int i = 0; i < global.params.imppath->dim; i++)
    {
        linebuf->writeString((*global.params.imppath)[i]);
        linebuf->writeByte(0);
    }
#endif
    linebuf->writeByte(0);              // terminated with 0 byte

    /* ======================================== */

    abbrevseg = dwarf_getsegment(debug_abbrev, 0);
    abbrevbuf = SegData[abbrevseg]->SDbuf;
    abbrevcode = 1;

    // Free only if starting another file. Waste of time otherwise.
    if (abbrev_table)
    {   delete abbrev_table;
        abbrev_table = NULL;
    }

    static unsigned char abbrevHeader[] =
    {
        1,                      // abbreviation code
        DW_TAG_compile_unit,
        1,
        DW_AT_producer,  DW_FORM_string,
        DW_AT_language,  DW_FORM_data1,
        DW_AT_name,      DW_FORM_string,
        DW_AT_comp_dir,  DW_FORM_string,
        DW_AT_low_pc,    DW_FORM_addr,
        DW_AT_entry_pc,  DW_FORM_addr,
        DW_AT_ranges,    DW_FORM_data4,
        DW_AT_stmt_list, DW_FORM_data4,
        0,               0,
    };

    abbrevbuf->write(abbrevHeader,sizeof(abbrevHeader));

    /* ======================================== */

    infoseg = dwarf_getsegment(debug_info, 0);
    infobuf = SegData[infoseg]->SDbuf;

    debuginfo = debuginfo_init;
    if (I64)
        debuginfo.address_size = 8;

    infobuf->write(&debuginfo, sizeof(debuginfo));
#if ELFOBJ
    dwarf_addrel(infoseg,6,abbrevseg);
#endif

    infobuf->writeuLEB128(1);                   // abbreviation code
#if MARS
    infobuf->write("Digital Mars D ");
    infobuf->writeString(global.version);       // DW_AT_producer
    // DW_AT_language
    infobuf->writeByte((config.fulltypes == CVDWARF_D) ? DW_LANG_D : DW_LANG_C89);
#elif SCPP
    infobuf->write("Digital Mars C ");
    infobuf->writeString(global.version);       // DW_AT_producer
    infobuf->writeByte(DW_LANG_C89);            // DW_AT_language
#else
    assert(0);
#endif
    infobuf->writeString(filename);             // DW_AT_name
#if 0
    // This relies on an extension to POSIX.1 not always implemented
    char *cwd = getcwd(NULL, 0);
#else
    char *cwd;
    size_t sz = 80;
    while (1)
    {
        errno = 0;
        cwd = (char *)malloc(sz + 1);
        if (!cwd)
            err_nomem();
        char *buf = getcwd(cwd, sz);
        if (buf)
        {   cwd[sz] = 0;        // man page doesn't say if always 0 terminated
            break;
        }
        if (errno == ERANGE)
        {
            sz += 80;
            free(cwd);
            continue;
        }
        cwd[0] = 0;
        break;
    }
#endif
    //infobuf->write32(Obj::addstr(debug_str_buf, cwd)); // DW_AT_comp_dir as DW_FORM_strp, doesn't work on some systems
    infobuf->writeString(cwd);                  // DW_AT_comp_dir as DW_FORM_string
    free(cwd);

    append_addr(infobuf, 0);               // DW_AT_low_pc
    append_addr(infobuf, 0);               // DW_AT_entry_pc

#if ELFOBJ
    dwarf_addrel(infoseg,infobuf->size(),debug_ranges_seg);
#endif
    infobuf->write32(0);                        // DW_AT_ranges

#if ELFOBJ
    dwarf_addrel(infoseg,infobuf->size(),lineseg);
#endif
    infobuf->write32(0);                        // DW_AT_stmt_list

    memset(typidx_tab, 0, sizeof(typidx_tab));

    /* ======================================== */

    seg = dwarf_getsegment(debug_pubnames, 0);
    debug_pubnames_secidx = SegData[seg]->SDshtidx;
    debug_pubnames_buf = SegData[seg]->SDbuf;
    debug_pubnames_buf->reserve(1000);

    debug_pubnames_buf->write32(0);             // unit_length
    debug_pubnames_buf->writeWord(2);           // version
#if ELFOBJ
    dwarf_addrel(seg,debug_pubnames_buf->size(),infoseg);
#endif
    debug_pubnames_buf->write32(0);             // debug_info_offset
    debug_pubnames_buf->write32(0);             // debug_info_length

    /* ======================================== */

    debug_aranges_seg = dwarf_getsegment(debug_aranges, 0);
    debug_aranges_secidx = SegData[debug_aranges_seg]->SDshtidx;
    debug_aranges_buf = SegData[debug_aranges_seg]->SDbuf;
    debug_aranges_buf->reserve(1000);

    debug_aranges_buf->write32(0);              // unit_length
    debug_aranges_buf->writeWord(2);            // version
#if ELFOBJ
    dwarf_addrel(debug_aranges_seg,debug_aranges_buf->size(),infoseg);
#endif
    debug_aranges_buf->write32(0);              // debug_info_offset
    debug_aranges_buf->writeByte(I64 ? 8 : 4);  // address_size
    debug_aranges_buf->writeByte(0);            // segment_size
    debug_aranges_buf->write32(0);              // pad to 16
}


/*************************************
 * Add a file to the .debug_line header
 */
int dwarf_line_addfile(const char* filename)
{
    if (!infoFileName_table) {
        infoFileName_table = new AArray(&ti_abuf, sizeof(unsigned));
        linebuf_filetab_end = linebuf->size();
    }

    Abuf abuf;
    abuf.buf = (const unsigned char*)filename;
    abuf.length = strlen(filename);

    unsigned *pidx = (unsigned *)infoFileName_table->get(&abuf);
    if (!*pidx)                 // if no idx assigned yet
    {
        *pidx = infoFileName_table->length(); // assign newly computed idx

        size_t before = linebuf->size();
        linebuf->writeString(filename);
        linebuf->writeByte(0);      // directory table index
        linebuf->writeByte(0);      // mtime
        linebuf->writeByte(0);      // length
        linebuf_filetab_end += linebuf->size() - before;
    }

    return *pidx;
}

void dwarf_initmodule(const char *filename, const char *modname)
{
    if (modname)
    {
        static unsigned char abbrevModule[] =
        {
            DW_TAG_module,
            //1,                // one children
            0,                  // no children
            DW_AT_name,         DW_FORM_string, // module name
            0,                  0,
        };
        abbrevcode++;
        abbrevbuf->writeuLEB128(abbrevcode);
        abbrevbuf->write(abbrevModule,sizeof(abbrevModule));
        infobuf->writeuLEB128(abbrevcode);      // abbreviation code
        infobuf->writeString(modname);          // DW_AT_name
        //hasModname = 1;
    }
    else
        hasModname = 0;

    dwarf_line_addfile(filename);
}

void dwarf_termmodule()
{
    if (hasModname)
        infobuf->writeByte(0);  // end of DW_TAG_module's children
}

/*************************************
 * Finish writing Dwarf debug info to object file.
 */

void dwarf_termfile()
{
    //printf("dwarf_termfile()\n");

    /* ======================================== */

    // Put out line number info

    // file_names
    unsigned last_filenumber = 0;
    const char* last_filename = NULL;
    for (unsigned seg = 1; seg <= seg_count; seg++)
    {
        for (unsigned i = 0; i < SegData[seg]->SDlinnum_count; i++)
        {
            linnum_data *ld = &SegData[seg]->SDlinnum_data[i];
            const char *filename;
#if MARS
            filename = ld->filename;
#else
            Sfile *sf = ld->filptr;
            if (sf)
                filename = sf->SFname;
            else
                filename = ::filename;
#endif
            if (last_filename == filename)
            {
                ld->filenumber = last_filenumber;
            }
            else
            {
                ld->filenumber = dwarf_line_addfile(filename);

                last_filenumber = ld->filenumber;
                last_filename = filename;
            }
        }
    }
    // assert we haven't emitted anything but file table entries
    assert(linebuf->size() == linebuf_filetab_end);
    linebuf->writeByte(0);              // end of file_names

    debugline.prologue_length = linebuf->size() - 10;

    for (unsigned seg = 1; seg <= seg_count; seg++)
    {
        seg_data *sd = SegData[seg];
        unsigned addressmax = 0;
        unsigned linestart = ~0;

        if (!sd->SDlinnum_count)
            continue;
#if ELFOBJ
        if (!sd->SDsym) // gdb ignores line number data without a DW_AT_name
            continue;
#endif

        //printf("sd = %x, SDlinnum_count = %d\n", sd, sd->SDlinnum_count);
        for (int i = 0; i < sd->SDlinnum_count; i++)
        {   linnum_data *ld = &sd->SDlinnum_data[i];

            // Set address to start of segment with DW_LNE_set_address
            linebuf->writeByte(0);
            linebuf->writeByte(NPTRSIZE + 1);
            linebuf->writeByte(DW_LNE_set_address);

            dwarf_appreladdr(lineseg,linebuf,seg,0);

            // Dwarf2 6.2.2 State machine registers
            unsigned address = 0;       // instruction address
            unsigned file = ld->filenumber;
            unsigned line = 1;          // line numbers beginning with 1

            linebuf->writeByte(DW_LNS_set_file);
            linebuf->writeuLEB128(file);

            for (int j = 0; j < ld->linoff_count; j++)
            {   int lininc = ld->linoff[j][0] - line;
                int addinc = ld->linoff[j][1] - address;

                //printf("\tld[%d] line = %d offset = x%x lininc = %d addinc = %d\n", j, ld->linoff[j][0], ld->linoff[j][1], lininc, addinc);

                //assert(addinc >= 0);
                if (addinc < 0)
                    continue;
                if (j && lininc == 0 && !(addinc && j + 1 == ld->linoff_count))
                    continue;
                line += lininc;
                if (line < linestart)
                    linestart = line;
                address += addinc;
                if (address >= addressmax)
                    addressmax = address + 1;
                if (lininc >= debugline.line_base && lininc < debugline.line_base + debugline.line_range)
                {   unsigned opcode = lininc - debugline.line_base +
                                    debugline.line_range * addinc +
                                    debugline.opcode_base;

                    if (opcode <= 255)
                    {   linebuf->writeByte(opcode);
                        continue;
                    }
                }
                if (lininc)
                {
                    linebuf->writeByte(DW_LNS_advance_line);
                    linebuf->writesLEB128((long)lininc);
                }
                if (addinc)
                {
                    linebuf->writeByte(DW_LNS_advance_pc);
                    linebuf->writeuLEB128((unsigned long)addinc);
                }
                if (lininc || addinc)
                    linebuf->writeByte(DW_LNS_copy);
            }

            // Write DW_LNS_advance_pc to cover the function prologue
            linebuf->writeByte(DW_LNS_advance_pc);
            linebuf->writeuLEB128((unsigned long)(sd->SDbuf->size() - address));

            // Write DW_LNE_end_sequence
            linebuf->writeByte(0);
            linebuf->writeByte(1);
            linebuf->writeByte(1);

            // reset linnum_data
            ld->linoff_count = 0;
        }
    }

    debugline.total_length = linebuf->size() - 4;
    memcpy(linebuf->buf, &debugline, sizeof(debugline));

    // Bugzilla 3502, workaround OSX's ld64-77 bug.
    // Don't emit the the debug_line section if nothing has been written to the line table.
    if (debugline.prologue_length + 10 == debugline.total_length + 4)
        linebuf->reset();

    /* ================================================= */

    abbrevbuf->writeByte(0);

    /* ================================================= */

    infobuf->writeByte(0);      // ending abbreviation code

    debuginfo.total_length = infobuf->size() - 4;
    memcpy(infobuf->buf, &debuginfo, sizeof(debuginfo));

    /* ================================================= */

    // Terminate by offset field containing 0
    debug_pubnames_buf->write32(0);

    // Plug final sizes into header
    *(unsigned *)debug_pubnames_buf->buf = debug_pubnames_buf->size() - 4;
    *(unsigned *)(debug_pubnames_buf->buf + 10) = infobuf->size();

    /* ================================================= */

    // Terminate by address/length fields containing 0
    append_addr(debug_aranges_buf, 0);
    append_addr(debug_aranges_buf, 0);

    // Plug final sizes into header
    *(unsigned *)debug_aranges_buf->buf = debug_aranges_buf->size() - 4;

    /* ================================================= */

    // Terminate by beg address/end address fields containing 0
    append_addr(debug_ranges_buf, 0);
    append_addr(debug_ranges_buf, 0);

    /* ================================================= */

    // Free only if starting another file. Waste of time otherwise.
    if (type_table)
    {   delete type_table;
        type_table = NULL;
    }
    if (functype_table)
    {   delete functype_table;
        functype_table = NULL;
    }
    if (functypebuf)
        functypebuf->setsize(0);
}

/*****************************************
 * Start of code gen for function.
 */
void dwarf_func_start(Symbol *sfunc)
{
    //printf("dwarf_func_start(%s)\n", sfunc->Sident);
    if (I16 || I32)
        CFA_state_current = CFA_state_init_32;
    else if (I64)
        CFA_state_current = CFA_state_init_64;
    else
        assert(0);
    CFA_state_current.reg = dwarf_regno(SP);
    assert(CFA_state_current.offset == OFFSET_FAC);
    cfa_buf.reset();
}

/*****************************************
 * End of code gen for function.
 */
void dwarf_func_term(Symbol *sfunc)
{
   //printf("dwarf_func_term(sfunc = '%s')\n", sfunc->Sident);

    if (config.ehmethod == EH_DWARF)
    {
        bool ehunwind = doUnwindEhFrame();

        IDXSEC dfseg = dwarf_eh_frame_alloc();

        Outbuffer *buf = SegData[dfseg]->SDbuf;
        buf->reserve(1000);

        unsigned *poffset = ehunwind ? &CIE_offset_unwind : &CIE_offset_no_unwind;
        if (*poffset == ~0)
            *poffset = writeEhFrameHeader(dfseg, buf, getRtlsym(RTLSYM_PERSONALITY), ehunwind);

        writeEhFrameFDE(dfseg, sfunc, ehunwind, *poffset);
    }
    if (!config.fulltypes)
        return;
#if MARS
    if (sfunc->Sflags & SFLnodebug)
        return;
    const char* filename = sfunc->Sfunc->Fstartline.Sfilename;
    if (!filename)
        return;
#endif

    unsigned funcabbrevcode;

    if (config.ehmethod == EH_DM)
    {
        IDXSEC dfseg = dwarf_getsegment(debug_frame_name, 1);
        writeDebugFrameFDE(dfseg, sfunc);
    }

    IDXSEC seg = sfunc->Sseg;
    seg_data *sd = SegData[seg];

#if MARS
    int filenum = dwarf_line_addfile(filename);
#else
    int filenum = 1;
#endif

        unsigned ret_type = dwarf_typidx(sfunc->Stype->Tnext);
        if (tybasic(sfunc->Stype->Tnext->Tty) == TYvoid)
            ret_type = 0;

        // See if there are any parameters
        int haveparameters = 0;
        unsigned formalcode = 0;
        unsigned autocode = 0;
        SYMIDX si;
        for (si = 0; si < globsym.top; si++)
        {
            symbol *sa = globsym.tab[si];
#if MARS
            if (sa->Sflags & SFLnodebug) continue;
#endif

            static unsigned char formal[] =
            {
                DW_TAG_formal_parameter,
                0,
                DW_AT_name,       DW_FORM_string,
                DW_AT_type,       DW_FORM_ref4,
                DW_AT_artificial, DW_FORM_flag,
                DW_AT_location,   DW_FORM_block1,
                0,                0,
            };

            switch (sa->Sclass)
            {   case SCparameter:
                case SCregpar:
                case SCfastpar:
                    dwarf_typidx(sa->Stype);
                    formal[0] = DW_TAG_formal_parameter;
                    if (!formalcode)
                        formalcode = dwarf_abbrev_code(formal,sizeof(formal));
                    haveparameters = 1;
                    break;

                case SCauto:
                case SCbprel:
                case SCregister:
                case SCpseudo:
                    dwarf_typidx(sa->Stype);
                    formal[0] = DW_TAG_variable;
                    if (!autocode)
                        autocode = dwarf_abbrev_code(formal,sizeof(formal));
                    haveparameters = 1;
                    break;
            }
        }

        Outbuffer abuf;
        abuf.writeByte(DW_TAG_subprogram);
        abuf.writeByte(haveparameters);          // have children?
        if (haveparameters)
        {
            abuf.writeByte(DW_AT_sibling);  abuf.writeByte(DW_FORM_ref4);
        }
        abuf.writeByte(DW_AT_name);      abuf.writeByte(DW_FORM_string);
#if (DWARF_VERSION >= 4)
        abuf.writeuLEB128(DW_AT_linkage_name);      abuf.writeByte(DW_FORM_string);
#else
        abuf.writeuLEB128(DW_AT_MIPS_linkage_name); abuf.writeByte(DW_FORM_string);
#endif
        abuf.writeByte(DW_AT_decl_file); abuf.writeByte(DW_FORM_data1);
        abuf.writeByte(DW_AT_decl_line); abuf.writeByte(DW_FORM_data2);
        if (ret_type)
        {
            abuf.writeByte(DW_AT_type);  abuf.writeByte(DW_FORM_ref4);
        }
        if (sfunc->Sclass == SCglobal)
        {
            abuf.writeByte(DW_AT_external);       abuf.writeByte(DW_FORM_flag);
        }
        abuf.writeByte(DW_AT_low_pc);     abuf.writeByte(DW_FORM_addr);
        abuf.writeByte(DW_AT_high_pc);    abuf.writeByte(DW_FORM_addr);
        abuf.writeByte(DW_AT_frame_base); abuf.writeByte(DW_FORM_data4);
        abuf.writeByte(0);                abuf.writeByte(0);

        funcabbrevcode = dwarf_abbrev_code(abuf.buf, abuf.size());

        unsigned idxsibling = 0;
        unsigned siblingoffset;

        unsigned infobuf_offset = infobuf->size();
        infobuf->writeuLEB128(funcabbrevcode);  // abbreviation code
        if (haveparameters)
        {
            siblingoffset = infobuf->size();
            infobuf->write32(idxsibling);       // DW_AT_sibling
        }

        const char *name;
#if MARS
        name = sfunc->prettyIdent ? sfunc->prettyIdent : sfunc->Sident;
#else
        name = sfunc->Sident;
#endif
        infobuf->writeString(name);             // DW_AT_name
        infobuf->writeString(sfunc->Sident);    // DW_AT_MIPS_linkage_name
        infobuf->writeByte(filenum);            // DW_AT_decl_file
        infobuf->writeWord(sfunc->Sfunc->Fstartline.Slinnum);   // DW_AT_decl_line
        if (ret_type)
            infobuf->write32(ret_type);         // DW_AT_type

        if (sfunc->Sclass == SCglobal)
            infobuf->writeByte(1);              // DW_AT_external

        // DW_AT_low_pc and DW_AT_high_pc
        dwarf_appreladdr(infoseg, infobuf, seg, funcoffset);
        dwarf_appreladdr(infoseg, infobuf, seg, funcoffset + sfunc->Ssize);

        // DW_AT_frame_base
#if ELFOBJ
        dwarf_apprel32(infoseg, infobuf, debug_loc_seg, debug_loc_buf->size());
#else
        // 64-bit DWARF relocations don't work for OSX64 codegen
        infobuf->write32(debug_loc_buf->size());
#endif

        if (haveparameters)
        {
            for (si = 0; si < globsym.top; si++)
            {
                symbol *sa = globsym.tab[si];
#if MARS
                if (sa->Sflags & SFLnodebug) continue;
#endif

                unsigned vcode;

                switch (sa->Sclass)
                {
                    case SCparameter:
                    case SCregpar:
                    case SCfastpar:
                        vcode = formalcode;
                        goto L1;
                    case SCauto:
                    case SCregister:
                    case SCpseudo:
                    case SCbprel:
                        vcode = autocode;
                    L1:
                    {
                        unsigned soffset;
                        unsigned tidx = dwarf_typidx(sa->Stype);

                        infobuf->writeuLEB128(vcode);           // abbreviation code
                        infobuf->writeString(sa->Sident);       // DW_AT_name
                        infobuf->write32(tidx);                 // DW_AT_type
                        infobuf->writeByte(sa->Sflags & SFLartifical ? 1 : 0); // DW_FORM_tag
                        soffset = infobuf->size();
                        infobuf->writeByte(2);                  // DW_FORM_block1
                        if (sa->Sfl == FLreg || sa->Sclass == SCpseudo)
                        {   // BUG: register pairs not supported in Dwarf?
                            infobuf->writeByte(DW_OP_reg0 + sa->Sreglsw);
                        }
                        else if (sa->Sscope && vcode == autocode)
                        {
                            assert(sa->Sscope->Stype->Tnext && sa->Sscope->Stype->Tnext->Tty == TYstruct);

                            /* find member offset in closure */
                            targ_size_t memb_off = 0;
                            struct_t *st = sa->Sscope->Stype->Tnext->Ttag->Sstruct; // Sscope is __closptr
                            for (symlist_t sl = st->Sfldlst; sl; sl = list_next(sl))
                            {
                                symbol *sf = list_symbol(sl);
                                if (sf->Sclass == SCmember)
                                {
                                    if(strcmp(sa->Sident, sf->Sident) == 0)
                                    {
                                        memb_off = sf->Smemoff;
                                        goto L2;
                                    }
                                }
                            }
                            L2:
                            targ_size_t closptr_off = sa->Sscope->Soffset; // __closptr offset
                            //printf("dwarf closure: sym: %s, closptr: %s, ptr_off: %lli, memb_off: %lli\n",
                            //    sa->Sident, sa->Sscope->Sident, closptr_off, memb_off);

                            infobuf->writeByte(DW_OP_fbreg);
                            infobuf->writesLEB128(Auto.size + BPoff - Para.size + closptr_off); // closure pointer offset from frame base
                            infobuf->writeByte(DW_OP_deref);
                            infobuf->writeByte(DW_OP_plus_uconst);
                            infobuf->writeuLEB128(memb_off); // closure variable offset
                        }
                        else
                        {
                            infobuf->writeByte(DW_OP_fbreg);
                            if (sa->Sclass == SCregpar ||
                                sa->Sclass == SCparameter)
                                infobuf->writesLEB128(sa->Soffset);
                            else if (sa->Sclass == SCfastpar)
                                infobuf->writesLEB128(Fast.size + BPoff - Para.size + sa->Soffset);
                            else if (sa->Sclass == SCbprel)
                                infobuf->writesLEB128(-Para.size + sa->Soffset);
                            else
                                infobuf->writesLEB128(Auto.size + BPoff - Para.size + sa->Soffset);
                        }
                        infobuf->buf[soffset] = infobuf->size() - soffset - 1;
                        break;
                    }
                }
            }
            infobuf->writeByte(0);              // end of parameter children

            idxsibling = infobuf->size();
            *(unsigned *)(infobuf->buf + siblingoffset) = idxsibling;
        }

        /* ============= debug_pubnames =========================== */

        debug_pubnames_buf->write32(infobuf_offset);
        // Should be the fully qualified name, not the simple DW_AT_name
        debug_pubnames_buf->writeString(sfunc->Sident);

        /* ============= debug_aranges =========================== */

        if (sd->SDaranges_offset)
            // Extend existing entry size
            *(unsigned long long *)(debug_aranges_buf->buf + sd->SDaranges_offset + NPTRSIZE) = funcoffset + sfunc->Ssize;
        else
        {   // Add entry
            sd->SDaranges_offset = debug_aranges_buf->size();
            // address of start of .text segment
            dwarf_appreladdr(debug_aranges_seg, debug_aranges_buf, seg, 0);
            // size of .text segment
            append_addr(debug_aranges_buf, funcoffset + sfunc->Ssize);
        }

        /* ============= debug_ranges =========================== */

        /* Each function gets written into its own segment,
         * indicate this by adding to the debug_ranges
         */
        // start of function and end of function
        dwarf_appreladdr(debug_ranges_seg, debug_ranges_buf, seg, funcoffset);
        dwarf_appreladdr(debug_ranges_seg, debug_ranges_buf, seg, funcoffset + sfunc->Ssize);

        /* ============= debug_loc =========================== */

        assert(Para.size >= 2 * REGSIZE);
        assert(Para.size < 63); // avoid sLEB128 encoding
        unsigned short op_size = 0x0002;
        unsigned short loc_op;

        // set the entry for this function in .debug_loc segment
        // after call
        dwarf_appreladdr(debug_loc_seg, debug_loc_buf, seg, funcoffset + 0);
        dwarf_appreladdr(debug_loc_seg, debug_loc_buf, seg, funcoffset + 1);

        loc_op = ((Para.size - REGSIZE) << 8) | (DW_OP_breg0 + dwarf_regno(SP));
        debug_loc_buf->write32(loc_op << 16 | op_size);

        // after push EBP
        dwarf_appreladdr(debug_loc_seg, debug_loc_buf, seg, funcoffset + 1);
        dwarf_appreladdr(debug_loc_seg, debug_loc_buf, seg, funcoffset + 3);

        loc_op = ((Para.size) << 8) | (DW_OP_breg0 + dwarf_regno(SP));
        debug_loc_buf->write32(loc_op << 16 | op_size);

        // after mov EBP, ESP
        dwarf_appreladdr(debug_loc_seg, debug_loc_buf, seg, funcoffset + 3);
        dwarf_appreladdr(debug_loc_seg, debug_loc_buf, seg, funcoffset + sfunc->Ssize);

        loc_op = ((Para.size) << 8) | (DW_OP_breg0 + dwarf_regno(BP));
        debug_loc_buf->write32(loc_op << 16 | op_size);

        // 2 zero addresses to end loc_list
        append_addr(debug_loc_buf, 0);
        append_addr(debug_loc_buf, 0);
}


/******************************************
 * Write out symbol table for current function.
 */

void cv_outsym(symbol *s)
{
    //printf("cv_outsym('%s')\n",s->Sident);
    //symbol_print(s);

    symbol_debug(s);
#if MARS
    if (s->Sflags & SFLnodebug)
        return;
#endif
    type *t = s->Stype;
    type_debug(t);
    tym_t tym = tybasic(t->Tty);
    if (tyfunc(tym) && s->Sclass != SCtypedef)
        return;

    Outbuffer abuf;
    unsigned code;
    unsigned typidx;
    unsigned soffset;
    switch (s->Sclass)
    {
        case SCglobal:
            typidx = dwarf_typidx(t);

            abuf.writeByte(DW_TAG_variable);
            abuf.writeByte(0);                  // no children
            abuf.writeByte(DW_AT_name);         abuf.writeByte(DW_FORM_string);
            abuf.writeByte(DW_AT_type);         abuf.writeByte(DW_FORM_ref4);
            abuf.writeByte(DW_AT_external);     abuf.writeByte(DW_FORM_flag);
            abuf.writeByte(DW_AT_location);     abuf.writeByte(DW_FORM_block1);
            abuf.writeByte(0);                  abuf.writeByte(0);
            code = dwarf_abbrev_code(abuf.buf, abuf.size());

            infobuf->writeuLEB128(code);        // abbreviation code
            infobuf->writeString(s->Sident);    // DW_AT_name
            infobuf->write32(typidx);           // DW_AT_type
            infobuf->writeByte(1);              // DW_AT_external

            soffset = infobuf->size();
            infobuf->writeByte(2);                      // DW_FORM_block1

#if ELFOBJ
            // debug info for TLS variables
            assert(s->Sxtrnnum);
            if (s->Sfl == FLtlsdata)
            {
                if (I64)
                {
                    infobuf->writeByte(DW_OP_const8u);
                    ElfObj::addrel(infoseg, infobuf->size(), R_X86_64_DTPOFF32, s->Sxtrnnum, 0);
                    infobuf->write64(0);
                }
                else
                {
                    infobuf->writeByte(DW_OP_const4u);
                    ElfObj::addrel(infoseg, infobuf->size(), R_386_TLS_LDO_32, s->Sxtrnnum, 0);
                    infobuf->write32(0);
                }
                infobuf->writeByte(DW_OP_GNU_push_tls_address);
            } else
#endif
            {
                infobuf->writeByte(DW_OP_addr);
                dwarf_appreladdr(infoseg, infobuf, s->Sseg, s->Soffset); // address of global
            }

            infobuf->buf[soffset] = infobuf->size() - soffset - 1;
            break;
    }
}


/******************************************
 * Write out any deferred symbols.
 */

void cv_outlist()
{
}


/******************************************
 * Write out symbol table for current function.
 */

void cv_func(Funcsym *s)
{
}

/* =================== Cached Types in debug_info ================= */

struct Atype
{
    Outbuffer *buf;
    size_t start;
    size_t end;
};

struct TypeInfo_Atype : TypeInfo
{
    const char* toString();
    hash_t getHash(void *p);
    int equals(void *p1, void *p2);
    int compare(void *p1, void *p2);
    size_t tsize();
    void swap(void *p1, void *p2);
};

TypeInfo_Atype ti_atype;

const char* TypeInfo_Atype::toString()
{
    return "Atype";
}

hash_t TypeInfo_Atype::getHash(void *p)
{   Atype a;
    hash_t hash = 0;
    size_t i;

    a = *(Atype *)p;
    for (i = a.start; i < a.end; i++)
    {
        hash = hash * 11 + a.buf->buf[i];
    }
    return hash;
}

int TypeInfo_Atype::equals(void *p1, void *p2)
{
    Atype a1 = *(Atype*)p1;
    Atype a2 = *(Atype*)p2;
    size_t len = a1.end - a1.start;

    return len == a2.end - a2.start &&
        memcmp(a1.buf->buf + a1.start, a2.buf->buf + a2.start, len) == 0;
}

int TypeInfo_Atype::compare(void *p1, void *p2)
{
    Atype a1 = *(Atype*)p1;
    Atype a2 = *(Atype*)p2;
    size_t len = a1.end - a1.start;
    if (len == a2.end - a2.start)
        return memcmp(a1.buf->buf + a1.start, a2.buf->buf + a2.start, len);
    else if (len < a2.end - a2.start)
        return -1;
    else
        return 1;
}

size_t TypeInfo_Atype::tsize()
{
    return sizeof(Atype);
}

void TypeInfo_Atype::swap(void *p1, void *p2)
{
    assert(0);
}

unsigned char dwarf_classify_struct(unsigned long sflags)
{
    if (sflags & STRclass)
        return DW_TAG_class_type;

    if (sflags & STRunion)
        return DW_TAG_union_type;

    return DW_TAG_structure_type;
}

/* ======================= Type Index ============================== */

unsigned dwarf_typidx(type *t)
{   unsigned idx = 0;
    unsigned nextidx;
    unsigned keyidx;
    unsigned pvoididx;
    unsigned code;
    type *tnext;
    type *tbase;
    const char *p;

    static unsigned char abbrevTypeBasic[] =
    {
        DW_TAG_base_type,
        0,                      // no children
        DW_AT_name,             DW_FORM_string,
        DW_AT_byte_size,        DW_FORM_data1,
        DW_AT_encoding,         DW_FORM_data1,
        0,                      0,
    };
    static unsigned char abbrevWchar[] =
    {
        DW_TAG_typedef,
        0,                      // no children
        DW_AT_name,             DW_FORM_string,
        DW_AT_type,             DW_FORM_ref4,
        DW_AT_decl_file,        DW_FORM_data1,
        DW_AT_decl_line,        DW_FORM_data2,
        0,                      0,
    };
    static unsigned char abbrevTypePointer[] =
    {
        DW_TAG_pointer_type,
        0,                      // no children
        DW_AT_type,             DW_FORM_ref4,
        0,                      0,
    };
    static unsigned char abbrevTypePointerVoid[] =
    {
        DW_TAG_pointer_type,
        0,                      // no children
        0,                      0,
    };
    static unsigned char abbrevTypeRef[] =
    {
        DW_TAG_reference_type,
        0,                      // no children
        DW_AT_type,             DW_FORM_ref4,
        0,                      0,
    };
    static unsigned char abbrevTypeConst[] =
    {
        DW_TAG_const_type,
        0,                      // no children
        DW_AT_type,             DW_FORM_ref4,
        0,                      0,
    };
    static unsigned char abbrevTypeConstVoid[] =
    {
        DW_TAG_const_type,
        0,                      // no children
        0,                      0,
    };
    static unsigned char abbrevTypeVolatile[] =
    {
        DW_TAG_volatile_type,
        0,                      // no children
        DW_AT_type,             DW_FORM_ref4,
        0,                      0,
    };
    static unsigned char abbrevTypeVolatileVoid[] =
    {
        DW_TAG_volatile_type,
        0,                      // no children
        0,                      0,
    };

    if (!t)
        return 0;

    if (t->Tty & mTYconst)
    {   // We make a copy of the type to strip off the const qualifier and
        // recurse, and then add the const abbrev code. To avoid ending in a
        // loop if the type references the const version of itself somehow,
        // we need to set TFforward here, because setting TFforward during
        // member generation of dwarf_typidx(tnext) has no effect on t itself.
        unsigned short old_flags = t->Tflags;
        t->Tflags |= TFforward;

        tnext = type_copy(t);
        tnext->Tcount++;
        tnext->Tty &= ~mTYconst;
        nextidx = dwarf_typidx(tnext);

        t->Tflags = old_flags;

        code = nextidx
            ? dwarf_abbrev_code(abbrevTypeConst, sizeof(abbrevTypeConst))
            : dwarf_abbrev_code(abbrevTypeConstVoid, sizeof(abbrevTypeConstVoid));
        goto Lcv;
    }

    if (t->Tty & mTYvolatile)
    {   tnext = type_copy(t);
        tnext->Tcount++;
        tnext->Tty &= ~mTYvolatile;
        nextidx = dwarf_typidx(tnext);
        code = nextidx
            ? dwarf_abbrev_code(abbrevTypeVolatile, sizeof(abbrevTypeVolatile))
            : dwarf_abbrev_code(abbrevTypeVolatileVoid, sizeof(abbrevTypeVolatileVoid));
    Lcv:
        idx = infobuf->size();
        infobuf->writeuLEB128(code);    // abbreviation code
        if (nextidx)
            infobuf->write32(nextidx);  // DW_AT_type
        goto Lret;
    }

    tym_t ty;
    ty = tybasic(t->Tty);
    if (!(t->Tnext && (ty == TYdarray || ty == TYdelegate)))
    {   // use cached basic type if it's not TYdarray or TYdelegate
        idx = typidx_tab[ty];
        if (idx)
            return idx;
    }

    unsigned char ate;
    ate = tyuns(t->Tty) ? DW_ATE_unsigned : DW_ATE_signed;
    switch (tybasic(t->Tty))
    {
        Lnptr:
            nextidx = dwarf_typidx(t->Tnext);
            code = nextidx
                ? dwarf_abbrev_code(abbrevTypePointer, sizeof(abbrevTypePointer))
                : dwarf_abbrev_code(abbrevTypePointerVoid, sizeof(abbrevTypePointerVoid));
            idx = infobuf->size();
            infobuf->writeuLEB128(code);        // abbreviation code
            if (nextidx)
                infobuf->write32(nextidx);      // DW_AT_type
            break;

        case TYullong:
        case TYucent:
            if (!t->Tnext)
            {   p = (tybasic(t->Tty) == TYullong) ? "unsigned long long" : "ucent";
                goto Lsigned;
            }

            static unsigned char abbrevTypeStruct[] =
            {
                DW_TAG_structure_type,
                1,                      // children
                DW_AT_name,             DW_FORM_string,
                DW_AT_byte_size,        DW_FORM_data1,
                0,                      0,
            };

            static unsigned char abbrevTypeMember[] =
            {
                DW_TAG_member,
                0,                      // no children
                DW_AT_name,             DW_FORM_string,
                DW_AT_type,             DW_FORM_ref4,
                DW_AT_data_member_location, DW_FORM_block1,
                0,                      0,
            };

            /* It's really TYdarray, and Tnext is the
             * element type
             */
            {
            unsigned lenidx = I64 ? dwarf_typidx(tsulong) : dwarf_typidx(tsuns);

            {
                type *tdata = type_alloc(TYnptr);
                tdata->Tnext = t->Tnext;
                t->Tnext->Tcount++;
                tdata->Tcount++;
                nextidx = dwarf_typidx(tdata);
                type_free(tdata);
            }

            code = dwarf_abbrev_code(abbrevTypeStruct, sizeof(abbrevTypeStruct));
            idx = infobuf->size();
            infobuf->writeuLEB128(code);        // abbreviation code
            infobuf->write("_Array_", 7);       // DW_AT_name
            if (tybasic(t->Tnext->Tty))
                infobuf->writeString(tystring[tybasic(t->Tnext->Tty)]);
            else
                infobuf->writeByte(0);
            infobuf->writeByte(tysize(t->Tty)); // DW_AT_byte_size

            // length
            code = dwarf_abbrev_code(abbrevTypeMember, sizeof(abbrevTypeMember));
            infobuf->writeuLEB128(code);        // abbreviation code
            infobuf->writeString("length");     // DW_AT_name
            infobuf->write32(lenidx);           // DW_AT_type

            infobuf->writeByte(2);              // DW_AT_data_member_location
            infobuf->writeByte(DW_OP_plus_uconst);
            infobuf->writeByte(0);

            // ptr
            infobuf->writeuLEB128(code);        // abbreviation code
            infobuf->writeString("ptr");        // DW_AT_name
            infobuf->write32(nextidx);          // DW_AT_type

            infobuf->writeByte(2);              // DW_AT_data_member_location
            infobuf->writeByte(DW_OP_plus_uconst);
            infobuf->writeByte(I64 ? 8 : 4);

            infobuf->writeByte(0);              // no more children
            }
            break;

        case TYllong:
        case TYcent:
            if (!t->Tnext)
            {   p = (tybasic(t->Tty) == TYllong) ? "long long" : "cent";
                goto Lsigned;
            }
            /* It's really TYdelegate, and Tnext is the
             * function type
             */
            {
                type *tp = type_fake(TYnptr);
                tp->Tcount++;
                pvoididx = dwarf_typidx(tp);    // void*

                tp->Tnext = t->Tnext;           // fptr*
                tp->Tnext->Tcount++;
                nextidx = dwarf_typidx(tp);
                type_free(tp);
            }

            code = dwarf_abbrev_code(abbrevTypeStruct, sizeof(abbrevTypeStruct));
            idx = infobuf->size();
            infobuf->writeuLEB128(code);        // abbreviation code
            infobuf->writeString("_Delegate");  // DW_AT_name
            infobuf->writeByte(tysize(t->Tty)); // DW_AT_byte_size

            // ctxptr
            code = dwarf_abbrev_code(abbrevTypeMember, sizeof(abbrevTypeMember));
            infobuf->writeuLEB128(code);        // abbreviation code
            infobuf->writeString("ctxptr");     // DW_AT_name
            infobuf->write32(pvoididx);         // DW_AT_type

            infobuf->writeByte(2);              // DW_AT_data_member_location
            infobuf->writeByte(DW_OP_plus_uconst);
            infobuf->writeByte(0);

            // funcptr
            infobuf->writeuLEB128(code);        // abbreviation code
            infobuf->writeString("funcptr");    // DW_AT_name
            infobuf->write32(nextidx);          // DW_AT_type

            infobuf->writeByte(2);              // DW_AT_data_member_location
            infobuf->writeByte(DW_OP_plus_uconst);
            infobuf->writeByte(I64 ? 8 : 4);

            infobuf->writeByte(0);              // no more children
            break;

        case TYnref:
        case TYref:
            nextidx = dwarf_typidx(t->Tnext);
            assert(nextidx);
            code = dwarf_abbrev_code(abbrevTypeRef, sizeof(abbrevTypeRef));
            idx = infobuf->size();
            infobuf->writeuLEB128(code);        // abbreviation code
            infobuf->write32(nextidx);          // DW_AT_type
            break;

        case TYnptr:
            if (!t->Tkey)
                goto Lnptr;

            /* It's really TYaarray, and Tnext is the
             * element type, Tkey is the key type
             */
            {
                type *tp = type_fake(TYnptr);
                tp->Tcount++;
                pvoididx = dwarf_typidx(tp);    // void*
            }

            code = dwarf_abbrev_code(abbrevTypeStruct, sizeof(abbrevTypeStruct));
            idx = infobuf->size();
            infobuf->writeuLEB128(code);        // abbreviation code
            infobuf->write("_AArray_", 8);      // DW_AT_name
            if (tybasic(t->Tkey->Tty))
                p = tystring[tybasic(t->Tkey->Tty)];
            else
                p = "key";
            infobuf->write(p, strlen(p));

            infobuf->writeByte('_');
            if (tybasic(t->Tnext->Tty))
                p = tystring[tybasic(t->Tnext->Tty)];
            else
                p = "value";
            infobuf->writeString(p);

            infobuf->writeByte(tysize(t->Tty)); // DW_AT_byte_size

            // ptr
            code = dwarf_abbrev_code(abbrevTypeMember, sizeof(abbrevTypeMember));
            infobuf->writeuLEB128(code);        // abbreviation code
            infobuf->writeString("ptr");        // DW_AT_name
            infobuf->write32(pvoididx);         // DW_AT_type

            infobuf->writeByte(2);              // DW_AT_data_member_location
            infobuf->writeByte(DW_OP_plus_uconst);
            infobuf->writeByte(0);

            infobuf->writeByte(0);              // no more children
            break;

        case TYvoid:        return 0;
        case TYbool:        p = "_Bool";         ate = DW_ATE_boolean;       goto Lsigned;
        case TYchar:        p = "char";          ate = (config.flags & CFGuchar) ? DW_ATE_unsigned_char : DW_ATE_signed_char;   goto Lsigned;
        case TYschar:       p = "signed char";   ate = DW_ATE_signed_char;   goto Lsigned;
        case TYuchar:       p = "unsigned char"; ate = DW_ATE_unsigned_char; goto Lsigned;
        case TYshort:       p = "short";                goto Lsigned;
        case TYushort:      p = "unsigned short";       goto Lsigned;
        case TYint:         p = "int";                  goto Lsigned;
        case TYuint:        p = "unsigned";             goto Lsigned;
        case TYlong:        p = "long";                 goto Lsigned;
        case TYulong:       p = "unsigned long";        goto Lsigned;
        case TYdchar:       p = "dchar";                goto Lsigned;
        case TYfloat:       p = "float";        ate = DW_ATE_float;     goto Lsigned;
        case TYdouble_alias:
        case TYdouble:      p = "double";       ate = DW_ATE_float;     goto Lsigned;
        case TYldouble:     p = "long double";  ate = DW_ATE_float;     goto Lsigned;
        case TYifloat:      p = "imaginary float";       ate = DW_ATE_imaginary_float;  goto Lsigned;
        case TYidouble:     p = "imaginary double";      ate = DW_ATE_imaginary_float;  goto Lsigned;
        case TYildouble:    p = "imaginary long double"; ate = DW_ATE_imaginary_float;  goto Lsigned;
        case TYcfloat:      p = "complex float";         ate = DW_ATE_complex_float;    goto Lsigned;
        case TYcdouble:     p = "complex double";        ate = DW_ATE_complex_float;    goto Lsigned;
        case TYcldouble:    p = "complex long double";   ate = DW_ATE_complex_float;    goto Lsigned;
        Lsigned:
            code = dwarf_abbrev_code(abbrevTypeBasic, sizeof(abbrevTypeBasic));
            idx = infobuf->size();
            infobuf->writeuLEB128(code);        // abbreviation code
            infobuf->writeString(p);            // DW_AT_name
            infobuf->writeByte(tysize(t->Tty)); // DW_AT_byte_size
            infobuf->writeByte(ate);            // DW_AT_encoding
            typidx_tab[ty] = idx;
            return idx;

        case TYnsfunc:
        case TYnpfunc:
        case TYjfunc:

        case TYnfunc:
        {
            /* The dwarf typidx for the function type is completely determined by
             * the return type typidx and the parameter typidx's. Thus, by
             * caching these, we can cache the function typidx.
             * Cache them in functypebuf[]
             */
            Outbuffer tmpbuf;
            nextidx = dwarf_typidx(t->Tnext);                   // function return type
            tmpbuf.write32(nextidx);
            unsigned params = 0;
            for (param_t *p = t->Tparamtypes; p; p = p->Pnext)
            {   params = 1;
                unsigned paramidx = dwarf_typidx(p->Ptype);
                //printf("1: paramidx = %d\n", paramidx);
#ifdef DEBUG
                if (!paramidx) type_print(p->Ptype);
#endif
                assert(paramidx);
                tmpbuf.write32(paramidx);
            }

            if (!functypebuf)
                functypebuf = new Outbuffer();
            unsigned functypebufidx = functypebuf->size();
            functypebuf->write(tmpbuf.buf, tmpbuf.size());
            /* If it's in the cache already, return the existing typidx
             */
            if (!functype_table)
                functype_table = new AArray(&ti_atype, sizeof(unsigned));
            Atype functype;
            functype.buf = functypebuf;
            functype.start = functypebufidx;
            functype.end = functypebuf->size();
            unsigned *pidx = (unsigned *)functype_table->get(&functype);
            if (*pidx)
            {   // Reuse existing typidx
                functypebuf->setsize(functypebufidx);
                return *pidx;
            }

            /* Not in the cache, create a new typidx
             */
            Outbuffer abuf;             // for abbrev
            abuf.writeByte(DW_TAG_subroutine_type);
            if (params)
                abuf.writeByte(1);      // children
            else
                abuf.writeByte(0);      // no children
            abuf.writeByte(DW_AT_prototyped);   abuf.writeByte(DW_FORM_flag);
            if (nextidx != 0)           // Don't write DW_AT_type for void
            {   abuf.writeByte(DW_AT_type);     abuf.writeByte(DW_FORM_ref4);
            }

            abuf.writeByte(0);                  abuf.writeByte(0);
            code = dwarf_abbrev_code(abuf.buf, abuf.size());

            unsigned paramcode;
            if (params)
            {   abuf.reset();
                abuf.writeByte(DW_TAG_formal_parameter);
                abuf.writeByte(0);
                abuf.writeByte(DW_AT_type);     abuf.writeByte(DW_FORM_ref4);
                abuf.writeByte(0);              abuf.writeByte(0);
                paramcode = dwarf_abbrev_code(abuf.buf, abuf.size());
            }

            idx = infobuf->size();
            infobuf->writeuLEB128(code);
            infobuf->writeByte(1);              // DW_AT_prototyped
            if (nextidx)                        // if return type is not void
                infobuf->write32(nextidx);      // DW_AT_type

            if (params)
            {   unsigned *pparamidx = (unsigned *)(functypebuf->buf + functypebufidx);
                //printf("2: functypebufidx = %x, pparamidx = %p, size = %x\n", functypebufidx, pparamidx, functypebuf->size());
                for (param_t *p = t->Tparamtypes; p; p = p->Pnext)
                {   infobuf->writeuLEB128(paramcode);
                    //unsigned x = dwarf_typidx(p->Ptype);
                    unsigned paramidx = *++pparamidx;
                    //printf("paramidx = %d\n", paramidx);
                    assert(paramidx);
                    infobuf->write32(paramidx);        // DW_AT_type
                }
                infobuf->writeByte(0);          // end parameter list
            }

            *pidx = idx;                        // remember it in the functype_table[] cache
            break;
        }

        case TYarray:
        {   static unsigned char abbrevTypeArray[] =
            {
                DW_TAG_array_type,
                1,                      // child (the subrange type)
                DW_AT_type,             DW_FORM_ref4,
                0,                      0,
            };
            static unsigned char abbrevTypeArrayVoid[] =
            {
                DW_TAG_array_type,
                1,                      // child (the subrange type)
                0,                      0,
            };
            static unsigned char abbrevTypeSubrange[] =
            {
                DW_TAG_subrange_type,
                0,                      // no children
                DW_AT_type,             DW_FORM_ref4,
                DW_AT_upper_bound,      DW_FORM_data4,
                0,                      0,
            };
            static unsigned char abbrevTypeSubrange2[] =
            {
                DW_TAG_subrange_type,
                0,                      // no children
                DW_AT_type,             DW_FORM_ref4,
                0,                      0,
            };
            unsigned code2 = (t->Tflags & TFsizeunknown)
                ? dwarf_abbrev_code(abbrevTypeSubrange2, sizeof(abbrevTypeSubrange2))
                : dwarf_abbrev_code(abbrevTypeSubrange, sizeof(abbrevTypeSubrange));
            unsigned idxbase = dwarf_typidx(tssize);
            nextidx = dwarf_typidx(t->Tnext);
            unsigned code1 = nextidx ? dwarf_abbrev_code(abbrevTypeArray, sizeof(abbrevTypeArray))
                                     : dwarf_abbrev_code(abbrevTypeArrayVoid, sizeof(abbrevTypeArrayVoid));
            idx = infobuf->size();

            infobuf->writeuLEB128(code1);       // DW_TAG_array_type
            if (nextidx)
                infobuf->write32(nextidx);      // DW_AT_type

            infobuf->writeuLEB128(code2);       // DW_TAG_subrange_type
            infobuf->write32(idxbase);          // DW_AT_type
            if (!(t->Tflags & TFsizeunknown))
                infobuf->write32(t->Tdim ? t->Tdim - 1 : 0);    // DW_AT_upper_bound

            infobuf->writeByte(0);              // no more children
            break;
        }

        // SIMD vector types
        case TYfloat4:   tbase = tsfloat;  goto Lvector;
        case TYdouble2:  tbase = tsdouble; goto Lvector;
        case TYschar16:  tbase = tsschar;  goto Lvector;
        case TYuchar16:  tbase = tsuchar;  goto Lvector;
        case TYshort8:   tbase = tsshort;  goto Lvector;
        case TYushort8:  tbase = tsushort; goto Lvector;
        case TYlong4:    tbase = tslong;   goto Lvector;
        case TYulong4:   tbase = tsulong;  goto Lvector;
        case TYllong2:   tbase = tsllong;  goto Lvector;
        case TYullong2:  tbase = tsullong; goto Lvector;
        Lvector:
        {
            static unsigned char abbrevTypeArray[] =
            {
                DW_TAG_array_type,
                1,                      // child (the subrange type)
                (DW_AT_GNU_vector & 0x7F) | 0x80, DW_AT_GNU_vector >> 7,        DW_FORM_flag,
                DW_AT_type,             DW_FORM_ref4,
                0,                      0,
            };
            static unsigned char abbrevSubRange[] =
            {
                DW_TAG_subrange_type,
                0,                                // no children
                DW_AT_upper_bound, DW_FORM_data1, // length of vector
                0,                 0,
            };

            unsigned code = dwarf_abbrev_code(abbrevTypeArray, sizeof(abbrevTypeArray));
            unsigned idxbase = dwarf_typidx(tbase);

            idx = infobuf->size();

            infobuf->writeuLEB128(code);        // DW_TAG_array_type
            infobuf->writeByte(1);              // DW_AT_GNU_vector
            infobuf->write32(idxbase);          // DW_AT_type

            // vector length stored as subrange type
            code = dwarf_abbrev_code(abbrevSubRange, sizeof(abbrevSubRange));
            infobuf->writeuLEB128(code);        // DW_TAG_subrange_type
            unsigned char dim = tysize(t->Tty) / tysize(tbase->Tty);
            infobuf->writeByte(dim - 1);        // DW_AT_upper_bound

            infobuf->writeByte(0);              // no more children
            break;
        }

        case TYwchar_t:
        {
            unsigned code = dwarf_abbrev_code(abbrevWchar, sizeof(abbrevWchar));
            unsigned typebase = dwarf_typidx(tsint);
            idx = infobuf->size();
            infobuf->writeuLEB128(code);        // abbreviation code
            infobuf->writeString("wchar_t");    // DW_AT_name
            infobuf->write32(typebase);         // DW_AT_type
            infobuf->writeByte(1);              // DW_AT_decl_file
            infobuf->writeWord(1);              // DW_AT_decl_line
            typidx_tab[ty] = idx;
            break;
        }


        case TYstruct:
        {
            Classsym *s = t->Ttag;
            struct_t *st = s->Sstruct;

            if (s->Stypidx)
                return s->Stypidx;

            static unsigned char abbrevTypeStruct0[] =
            {
                DW_TAG_structure_type,
                0,                      // no children
                DW_AT_name,             DW_FORM_string,
                DW_AT_byte_size,        DW_FORM_data1,
                0,                      0,
            };
            static unsigned char abbrevTypeStruct1[] =
            {
                DW_TAG_structure_type,
                0,                      // no children
                DW_AT_name,             DW_FORM_string,
                DW_AT_declaration,      DW_FORM_flag,
                0,                      0,
            };

            if (t->Tflags & (TFsizeunknown | TFforward))
            {
                abbrevTypeStruct1[0] = dwarf_classify_struct(st->Sflags);
                code = dwarf_abbrev_code(abbrevTypeStruct1, sizeof(abbrevTypeStruct1));
                idx = infobuf->size();
                infobuf->writeuLEB128(code);
                infobuf->writeString(s->Sident);        // DW_AT_name
                infobuf->writeByte(1);                  // DW_AT_declaration
                break;                  // don't set Stypidx
            }

            Outbuffer fieldidx;

            // Count number of fields
            unsigned nfields = 0;
            symlist_t sl;
            t->Tflags |= TFforward;
            for (sl = st->Sfldlst; sl; sl = list_next(sl))
            {   symbol *sf = list_symbol(sl);

                switch (sf->Sclass)
                {
                    case SCmember:
                        fieldidx.write32(dwarf_typidx(sf->Stype));
                        nfields++;
                        break;
                }
            }
            t->Tflags &= ~TFforward;
            if (nfields == 0)
            {
                abbrevTypeStruct0[0] = dwarf_classify_struct(st->Sflags);
                abbrevTypeStruct0[1] = 0;               // no children
                abbrevTypeStruct0[5] = DW_FORM_data1;   // DW_AT_byte_size
                code = dwarf_abbrev_code(abbrevTypeStruct0, sizeof(abbrevTypeStruct0));
                idx = infobuf->size();
                infobuf->writeuLEB128(code);
                infobuf->writeString(s->Sident);        // DW_AT_name
                infobuf->writeByte(0);                  // DW_AT_byte_size
            }
            else
            {
                Outbuffer abuf;         // for abbrev
                abuf.writeByte(dwarf_classify_struct(st->Sflags));
                abuf.writeByte(1);              // children
                abuf.writeByte(DW_AT_name);     abuf.writeByte(DW_FORM_string);
                abuf.writeByte(DW_AT_byte_size);

                size_t sz = st->Sstructsize;
                if (sz <= 0xFF)
                    abuf.writeByte(DW_FORM_data1);      // DW_AT_byte_size
                else if (sz <= 0xFFFF)
                    abuf.writeByte(DW_FORM_data2);      // DW_AT_byte_size
                else
                    abuf.writeByte(DW_FORM_data4);      // DW_AT_byte_size
                abuf.writeByte(0);              abuf.writeByte(0);

                code = dwarf_abbrev_code(abuf.buf, abuf.size());

                unsigned membercode;
                abuf.reset();
                abuf.writeByte(DW_TAG_member);
                abuf.writeByte(0);              // no children
                abuf.writeByte(DW_AT_name);
                abuf.writeByte(DW_FORM_string);
                abuf.writeByte(DW_AT_type);
                abuf.writeByte(DW_FORM_ref4);
                abuf.writeByte(DW_AT_data_member_location);
                abuf.writeByte(DW_FORM_block1);
                abuf.writeByte(0);
                abuf.writeByte(0);
                membercode = dwarf_abbrev_code(abuf.buf, abuf.size());

                idx = infobuf->size();
                infobuf->writeuLEB128(code);
                infobuf->writeString(s->Sident);        // DW_AT_name
                if (sz <= 0xFF)
                    infobuf->writeByte(sz);     // DW_AT_byte_size
                else if (sz <= 0xFFFF)
                    infobuf->writeWord(sz);     // DW_AT_byte_size
                else
                    infobuf->write32(sz);       // DW_AT_byte_size

                s->Stypidx = idx;
                unsigned n = 0;
                for (sl = st->Sfldlst; sl; sl = list_next(sl))
                {   symbol *sf = list_symbol(sl);
                    size_t soffset;

                    switch (sf->Sclass)
                    {
                        case SCmember:
                            infobuf->writeuLEB128(membercode);
                            infobuf->writeString(sf->Sident);
                            //infobuf->write32(dwarf_typidx(sf->Stype));
                            unsigned fi = ((unsigned *)fieldidx.buf)[n];
                            infobuf->write32(fi);
                            n++;
                            soffset = infobuf->size();
                            infobuf->writeByte(2);
                            infobuf->writeByte(DW_OP_plus_uconst);
                            infobuf->writeuLEB128(sf->Smemoff);
                            infobuf->buf[soffset] = infobuf->size() - soffset - 1;
                            break;
                    }
                }

                infobuf->writeByte(0);          // no more children
            }
            s->Stypidx = idx;
            reset_symbuf->write(&s, sizeof(s));
            return idx;                 // no need to cache it
        }

        case TYenum:
        {   static unsigned char abbrevTypeEnum[] =
            {
                DW_TAG_enumeration_type,
                1,                      // child (the subrange type)
                DW_AT_name,             DW_FORM_string,
                DW_AT_byte_size,        DW_FORM_data1,
                0,                      0,
            };
            static unsigned char abbrevTypeEnumMember[] =
            {
                DW_TAG_enumerator,
                0,                      // no children
                DW_AT_name,             DW_FORM_string,
                DW_AT_const_value,      DW_FORM_data1,
                0,                      0,
            };

            symbol *s = t->Ttag;
            enum_t *se = s->Senum;
            type *tbase = s->Stype->Tnext;
            unsigned sz = type_size(tbase);
            symlist_t sl;

            if (s->Stypidx)
                return s->Stypidx;

            if (se->SEflags & SENforward)
            {
                static unsigned char abbrevTypeEnumForward[] =
                {
                    DW_TAG_enumeration_type,
                    0,                  // no children
                    DW_AT_name,         DW_FORM_string,
                    DW_AT_declaration,  DW_FORM_flag,
                    0,                  0,
                };
                code = dwarf_abbrev_code(abbrevTypeEnumForward, sizeof(abbrevTypeEnumForward));
                idx = infobuf->size();
                infobuf->writeuLEB128(code);
                infobuf->writeString(s->Sident);        // DW_AT_name
                infobuf->writeByte(1);                  // DW_AT_declaration
                break;                  // don't set Stypidx
            }

            Outbuffer abuf;             // for abbrev
            abuf.write(abbrevTypeEnum, sizeof(abbrevTypeEnum));
            code = dwarf_abbrev_code(abuf.buf, abuf.size());

            unsigned membercode;
            abuf.reset();
            abuf.writeByte(DW_TAG_enumerator);
            abuf.writeByte(0);
            abuf.writeByte(DW_AT_name);
            abuf.writeByte(DW_FORM_string);
            abuf.writeByte(DW_AT_const_value);
            if (tyuns(tbase->Tty))
                abuf.writeByte(DW_FORM_udata);
            else
                abuf.writeByte(DW_FORM_sdata);
            abuf.writeByte(0);
            abuf.writeByte(0);
            membercode = dwarf_abbrev_code(abuf.buf, abuf.size());

            idx = infobuf->size();
            infobuf->writeuLEB128(code);
            infobuf->writeString(s->Sident);    // DW_AT_name
            infobuf->writeByte(sz);             // DW_AT_byte_size

            for (sl = s->Senumlist; sl; sl = list_next(sl))
            {   symbol *sf = (symbol *)list_ptr(sl);
                unsigned long value = el_tolongt(sf->Svalue);

                infobuf->writeuLEB128(membercode);
                infobuf->writeString(sf->Sident);
                if (tyuns(tbase->Tty))
                    infobuf->writeuLEB128(value);
                else
                    infobuf->writesLEB128(value);
            }

            infobuf->writeByte(0);              // no more children

            s->Stypidx = idx;
            reset_symbuf->write(&s, sizeof(s));
            return idx;                 // no need to cache it
        }

        default:
            return 0;
    }
Lret:
    /* If infobuf->buf[idx .. size()] is already in infobuf,
     * discard this one and use the previous one.
     */
    Atype atype;
    atype.buf = infobuf;
    atype.start = idx;
    atype.end = infobuf->size();

    if (!type_table)
        /* unsigned[Adata] type_table;
         * where the table values are the type indices
         */
        type_table = new AArray(&ti_atype, sizeof(unsigned));

    unsigned *pidx;
    pidx = (unsigned *)type_table->get(&atype);
    if (!*pidx)                 // if no idx assigned yet
    {
        *pidx = idx;            // assign newly computed idx
    }
    else
    {   // Reuse existing code
        infobuf->setsize(idx);  // discard current
        idx = *pidx;
    }
    return idx;
}

/* ======================= Abbreviation Codes ====================== */

struct Adata
{
    size_t start;
    size_t end;
};

struct TypeInfo_Adata : TypeInfo
{
    const char* toString();
    hash_t getHash(void *p);
    int equals(void *p1, void *p2);
    int compare(void *p1, void *p2);
    size_t tsize();
    void swap(void *p1, void *p2);
};

TypeInfo_Adata ti_adata;

const char* TypeInfo_Adata::toString()
{
    return "Adata";
}

hash_t TypeInfo_Adata::getHash(void *p)
{   Adata a;
    hash_t hash = 0;
    size_t i;

    a = *(Adata *)p;
    for (i = a.start; i < a.end; i++)
    {
        //printf("%02x ", abbrevbuf->buf[i]);
        hash = hash * 11 + abbrevbuf->buf[i];
    }
    //printf("\nhash = %x, length = %d\n", hash, a.end - a.start);
    return hash;
}

int TypeInfo_Adata::equals(void *p1, void *p2)
{
    Adata a1 = *(Adata*)p1;
    Adata a2 = *(Adata*)p2;
    size_t len = a1.end - a1.start;

    return len == a2.end - a2.start &&
        memcmp(abbrevbuf->buf + a1.start, abbrevbuf->buf + a2.start, len) == 0;
}

int TypeInfo_Adata::compare(void *p1, void *p2)
{
    Adata a1 = *(Adata*)p1;
    Adata a2 = *(Adata*)p2;
    size_t len = a1.end - a1.start;
    if (len == a2.end - a2.start)
        return memcmp(abbrevbuf->buf + a1.start, abbrevbuf->buf + a2.start, len);
    else if (len < a2.end - a2.start)
        return -1;
    else
        return 1;
}

size_t TypeInfo_Adata::tsize()
{
    return sizeof(Adata);
}

void TypeInfo_Adata::swap(void *p1, void *p2)
{
    assert(0);
}


unsigned dwarf_abbrev_code(unsigned char *data, size_t nbytes)
{
    if (!abbrev_table)
        /* unsigned[Adata] abbrev_table;
         * where the table values are the abbreviation codes.
         */
        abbrev_table = new AArray(&ti_adata, sizeof(unsigned));

    /* Write new entry into abbrevbuf
     */
    Adata adata;

    unsigned idx = abbrevbuf->size();
    abbrevcode++;
    abbrevbuf->writeuLEB128(abbrevcode);
    adata.start = abbrevbuf->size();
    abbrevbuf->write(data, nbytes);
    adata.end = abbrevbuf->size();

    /* If abbrevbuf->buf[idx .. size()] is already in abbrevbuf,
     * discard this one and use the previous one.
     */

    unsigned *pcode;
    pcode = (unsigned *)abbrev_table->get(&adata);
    if (!*pcode)                // if no code assigned yet
    {
        *pcode = abbrevcode;    // assign newly computed code
    }
    else
    {   // Reuse existing code
        abbrevbuf->setsize(idx);        // discard current
        abbrevcode--;
    }
    return *pcode;
}

/*****************************************************
 * Write Dwarf-style exception tables.
 * Params:
 *      sfunc = function to generate tables for
 *      startoffset = size of function prolog
 *      retoffset = offset from start of function to epilog
 */
void dwarf_except_gentables(Funcsym *sfunc, unsigned startoffset, unsigned retoffset)
{
    if (!doUnwindEhFrame())
        return;

    int seg = dwarf_except_table_alloc();
    Outbuffer *buf = SegData[seg]->SDbuf;
    buf->reserve(100);

#if ELFOBJ
    sfunc->Sfunc->LSDAoffset = buf->size();
#endif
#if MACHOBJ
    char name[16 + sizeof(except_table_num) * 3 + 1];
    sprintf(name, "GCC_except_table%d", ++except_table_num);
    type *t = tspvoid;
    t->Tcount++;
    type_setmangle(&t, mTYman_sys);         // no leading '_' for mangled name
    Symbol *s = symbol_name(name, SCstatic, t);
    Obj::pubdef(seg, s, buf->size());
    symbol_keep(s);

    sfunc->Sfunc->LSDAsym = s;
#endif
    genDwarfEh(sfunc, seg, buf, usednteh & EHcleanup, startoffset, retoffset);
}

#else
void dwarf_CFA_set_loc(size_t location) { }
void dwarf_CFA_set_reg_offset(int reg, int offset) { }
void dwarf_CFA_offset(int reg, int offset) { }
void dwarf_except_gentables(Funcsym *sfunc, unsigned startoffset, unsigned retoffset) { }
#endif
#endif
