/* Microsoft COFF object file format */

#pragma ZTC align 1

/***********************************************/

struct filehdr
{
        unsigned short f_magic; // identifies type of target machine
#define IMAGE_FILE_MACHINE_UNKNOWN 0            // applies to any machine type
#define IMAGE_FILE_MACHINE_I386    0x14C        // x86
#define IMAGE_FILE_MACHINE_AMD64   0x8664       // x86_64
        unsigned short f_nscns; // number of sections (96 is max)
        long f_timdat;          // creation date, number of seconds since 1970
        long f_symptr;          // file offset of symbol table
        long f_nsyms;           // number of entried in the symbol table
        unsigned short f_opthdr; // optional header size (0)
        unsigned short f_flags;
#define IMAGE_FILE_RELOCS_STRIPPED              1
#define IMAGE_FILE_EXECUTABLE_IMAGE             2
#define IMAGE_FILE_LINE_NUMS_STRIPPED           4
#define IMAGE_FILE_LOCAL_SYMS_STRIPPED          8
#define IMAGE_FILE_AGGRESSIVE_WS_TRIM           0x10
#define IMAGE_FILE_LARGE_ADDRESS_AWARE          0x20
#define IMAGE_FILE_BYTES_REVERSED_LO            0x80
#define IMAGE_FILE_32BIT_MACHINE                0x100
#define IMAGE_FILE_DEBUG_STRIPPED               0x200
#define IMAGE_FILE_REMOVABLE_RUN_FROM_SWAP      0x400
#define IMAGE_FILE_NET_RUN_FROM_SWAP            0x800
#define IMAGE_FILE_SYSTEM                       0x1000
#define IMAGE_FILE_DLL                          0x2000
#define IMAGE_FILE_UP_SYSTEM_ONLY               0x4000
#define IMAGE_FILE_BYTES_REVERSED_HI            0x8000
};

/***********************************************/

// size should be 40 bytes
struct scnhdr
{
        char s_name[8];         // name or /nnnn, where nnnn is offset in string table
        long s_paddr;           // virtual size, 0 for obj files
        long s_vaddr;           // virtual address, 0 for obj files
        long s_size;            // size of raw data on disk
        long s_scnptr;          // file offset of raw data on disk, should be aligned by 4
        long s_relptr;          // file offset of relocation data
        long s_lnnoptr;         // file offset of line numbers, should be 0
        unsigned short s_nreloc;        // number of relocations
        unsigned short s_nlnno;         // number of line number entries, should be 0
        unsigned long s_flags;
#define IMAGE_SCN_TYPE_NO_PAD           8       // obsolete
#define IMAGE_SCN_CNT_CODE              0x20    // code section
#define IMAGE_SCN_CNT_INITIALIZED_DATA  0x40
#define IMAGE_SCN_CNT_UNINITIALIZED_DATA        0x80
#define IMAGE_SCN_LNK_OTHER             0x100
#define IMAGE_SCN_LNK_INFO              0x200   // comments; for .drectve section
#define IMAGE_SCN_LNK_REMOVE            0x800   // do not put in image file
#define IMAGE_SCN_LNK_COMDAT            0x1000  // COMDAT section
#define IMAGE_SCN_GPREL                 0x8000  // data referenced through global pointer GP
#define IMAGE_SCN_MEM_PURGEABLE         0x20000
#define IMAGE_SCN_MEM_16BIT             0x20000
#define IMAGE_SCN_MEM_LOCKED            0x40000
#define IMAGE_SCN_MEM_PRELOAD           0x80000
#define IMAGE_SCN_ALIGN_1BYTES          0x100000
#define IMAGE_SCN_ALIGN_2BYTES          0x200000
#define IMAGE_SCN_ALIGN_4BYTES          0x300000
#define IMAGE_SCN_ALIGN_8BYTES          0x400000
#define IMAGE_SCN_ALIGN_16BYTES         0x500000
#define IMAGE_SCN_ALIGN_32BYTES         0x600000
#define IMAGE_SCN_ALIGN_64BYTES         0x700000
#define IMAGE_SCN_ALIGN_128BYTES        0x800000
#define IMAGE_SCN_ALIGN_256BYTES        0x900000
#define IMAGE_SCN_ALIGN_512BYTES        0xA00000
#define IMAGE_SCN_ALIGN_1024BYTES       0xB00000
#define IMAGE_SCN_ALIGN_2048BYTES       0xC00000
#define IMAGE_SCN_ALIGN_4096BYTES       0xD00000
#define IMAGE_SCN_ALIGN_8192BYTES       0xE00000
#define IMAGE_SCN_LNK_NRELOC_OVFL       0x1000000       // more than 0xFFFF relocations
#define IMAGE_SCN_MEM_DISCARDABLE       0x2000000       // can be discarded
#define IMAGE_SCN_MEM_NOT_CACHED        0x4000000       // cannot be cached
#define IMAGE_SCN_MEM_NOT_PAGED         0x8000000       // cannot be paged
#define IMAGE_SCN_MEM_SHARED            0x10000000      // can be shared
#define IMAGE_SCN_MEM_EXECUTE           0x20000000      // executable code
#define IMAGE_SCN_MEM_READ              0x40000000      // readable
#define IMAGE_SCN_MEM_WRITE             0x80000000      // writeable
};

/***********************************************/

struct syment
{
    union
    {
#define SYMNMLEN        8
        char _n_name[SYMNMLEN];
        struct
        {   long _n_zeroes;
            long _n_offset;
        } _n_n;
        char *_n_nptr[2];
    } _n;
#define n_name          _n._n_name
#define n_zeroes        _n._n_n._n_zeroes
#define n_offset        _n._n_n._n_offset
#define n_nptr          _n._n_nptr[1]

    unsigned n_value;
    short n_scnum;
#define IMAGE_SYM_DEBUG                 -2
#define IMAGE_SYM_ABSOLUTE              -1
#define IMAGE_SYM_UNDEFINED             0

    unsigned short n_type;      // 0x20 function; 0x00 not a function

    unsigned char n_sclass;
/* Values for n_sclass  */
#define IMAGE_SYM_CLASS_EXTERNAL        2
#define IMAGE_SYM_CLASS_STATIC          3
#define IMAGE_SYM_CLASS_LABEL           6
#define IMAGE_SYM_CLASS_FUNCTION        101
#define IMAGE_SYM_CLASS_FILE            103

    unsigned char n_numaux;
};


/***********************************************/

struct reloc
{
#pragma pack(1)
    unsigned r_vaddr;           // file offset of relocation
    unsigned r_symndx;          // symbol table index
    unsigned short r_type;      // IMAGE_REL_XXX kind of relocation to be performed

#define IMAGE_REL_AMD64_ABSOLUTE        0
#define IMAGE_REL_AMD64_ADDR64          1
#define IMAGE_REL_AMD64_ADDR32          2
#define IMAGE_REL_AMD64_ADDR32NB        3
#define IMAGE_REL_AMD64_REL32           4
#define IMAGE_REL_AMD64_REL32_1         5
#define IMAGE_REL_AMD64_REL32_2         6
#define IMAGE_REL_AMD64_REL32_3         7
#define IMAGE_REL_AMD64_REL32_4         8
#define IMAGE_REL_AMD64_REL32_5         9
#define IMAGE_REL_AMD64_SECTION         0xA
#define IMAGE_REL_AMD64_SECREL          0xB
#define IMAGE_REL_AMD64_SECREL7         0xC
#define IMAGE_REL_AMD64_TOKEN           0xD
#define IMAGE_REL_AMD64_SREL32          0xE
#define IMAGE_REL_AMD64_PAIR            0xF
#define IMAGE_REL_AMD64_SSPAN32         0x10

#define IMAGE_REL_I386_ABSOLUTE         0
#define IMAGE_REL_I386_DIR16            1
#define IMAGE_REL_I386_REL16            2
#define IMAGE_REL_I386_DIR32            6
#define IMAGE_REL_I386_DIR32NB          7
#define IMAGE_REL_I386_SEG12            9
#define IMAGE_REL_I386_SECTION          0xA
#define IMAGE_REL_I386_SECREL           0xB
#define IMAGE_REL_I386_TOKEN            0xC
#define IMAGE_REL_I386_SECREL7          0xD
#define IMAGE_REL_I386_REL32            0x14

#pragma pack()
};


/***********************************************/

struct lineno
{
    union
    {
        unsigned l_symndx;
        unsigned l_paddr;
    } l_addr;
    unsigned short l_lnno;
};


/***********************************************/

#pragma pack(1)
union auxent
{
#pragma pack(1)
    // Function definitions
    struct
    {   unsigned TagIndex;
        unsigned TotalSize;
        unsigned PointerToLinenumber;
        unsigned PointerToNextFunction;
    } x_fd;

    // .bf symbols
    struct
    {
#pragma pack(1)
        unsigned Unused;
        unsigned short Linenumber;
        char filler[6];
        unsigned PointerToNextFunction;
#pragma pack()
    } x_bf;

    // .ef symbols
    struct
    {   unsigned Unused;
        unsigned short Linenumber;
    } x_ef;

    // Weak externals
    struct
    {   unsigned TagIndex;
        unsigned Characteristics;
#define IMAGE_WEAK_EXTERN_SEARCH_NOLIBRARY
#define IMAGE_WEAK_EXTERN_SEARCH_LIBRARY
#define IMAGE_WEAK_EXTERN_SEARCH_ALIAS
    } x_weak;

    // Files
    struct
    {   char FileName[18];
    } x_filename;

    // Section definitions
    struct
    {   unsigned length;
        unsigned short NumberOfRelocations;
        unsigned short NumberOfLinenumbers;
        unsigned CheckSum;
        unsigned short Number;
        unsigned char Selection;
#define IMAGE_COMDAT_SELECT_NODUPLICATES        1
#define IMAGE_COMDAT_SELECT_ANY                 2
#define IMAGE_COMDAT_SELECT_SAME_SIZE           3
#define IMAGE_COMDAT_SELECT_EXACT_MATCH         4
#define IMAGE_COMDAT_SELECT_ASSOCIATIVE         5
#define IMAGE_COMDAT_SELECT_LARGEST             6
    } x_section;

    char filler[18];
#pragma pack()
};
#pragma pack()


/***********************************************/

#pragma ZTC align
