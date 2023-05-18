// Format of an ELF executable file

#define ELF_MAGIC 0x464C457FU  // "\x7FELF" in little endian

// File header
struct elfhdr {
  uint magic;  // must equal ELF_MAGIC
  uchar elf[12];
  ushort type;
  ushort machine;
  uint version;
  uint64 entry;
  uint64 phoff;
  uint64 shoff;
  uint flags;
  ushort ehsize;
  ushort phentsize;
  ushort phnum;
  ushort shentsize;
  ushort shnum;
  ushort shstrndx;
};

// Program section header
struct proghdr {
  uint32 type;
  uint32 flags;
  uint64 off;
  uint64 vaddr;
  uint64 paddr;
  uint64 filesz;
  uint64 memsz;
  uint64 align;
};

enum shtype {
   SHT_NULL          = 0x00,
   SHT_PROGBITS      = 0x01,
   SHT_SYMTAB        = 0x02,
   SHT_STRTAB        = 0x03,
   SHT_RELA          = 0x04,
   SHT_HASH          = 0x05,
   SHT_DYNAMIC       = 0x06,
   SHT_NOTE          = 0x07,
   SHT_NOBITS        = 0x08,
   SHT_REL           = 0x09,
   SHT_SHLIB         = 0x0A,
   SHT_DYNSYM        = 0x0B,
   SHT_INIT_ARRAY    = 0x0E,
   SHT_FINI_ARRAY    = 0x0F,
   SHT_PREINIT_ARRAY = 0x10,
   SHT_GROUP         = 0x11,
   SHT_SYMTAB_SHNDX  = 0x12,
   SHT_NUM           = 0x13,
};

enum shflags {
   SHF_WRITE            = 0x00000001,
   SHF_ALLOC            = 0x00000002,
   SHF_EXECINSTR        = 0x00000004,
   SHF_MERGE            = 0x00000010,
   SHF_STRINGS          = 0x00000020,
   SHF_INFO_LINK        = 0x00000040,
   SHF_LINK_ORDER       = 0x00000080,
   SHF_OS_NONCONFORMING = 0x00000100,
   SHF_GROUP            = 0x00000200,
   SHF_TLS              = 0x00000400,
   SHF_MASKOS           = 0x0FF00000,
   SHF_MASKPROC         = 0xF0000000,
   SHF_ORDERED          = 0x04000000,
   SHF_EXCLUDE          = 0x08000000,
};

// Section header
struct secthdr {
   uint32 name;
   enum shtype type;
   enum shflags flags;
   uint64 addr;
   uint64 offset;
   uint64 size;
   uint32 link;
   uint32 info;
   uint64 addralign;
   uint64 entsize;
};

// Values for Proghdr type
#define ELF_PROG_LOAD           1

// Flag bits for Proghdr flags
#define ELF_PROG_FLAG_EXEC      1
#define ELF_PROG_FLAG_WRITE     2
#define ELF_PROG_FLAG_READ      4
