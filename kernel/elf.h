// Format of an ELF executable file

#define ELF_MAGIC 0x464C457FU  // "\x7FELF" in little endian

// File header
struct elfhdr {
  uint magic;        // must equal ELF_MAGIC = 0x464C457F ("\x7FELF")
  uchar elf[12];     // identification bytes (class = 32/64 bit, endianness, etc.)
  ushort type;       // kind of ELF file (relocatable, executable, shared object, core dump)
  ushort machine;    // target machine architecture (RISC-V, x86, etc.)
  uint version;      // ELF version
  uint64 entry;      // virtual address of program entry point (where execution starts)
  uint64 phoff;      // file offset of program header table
  uint64 shoff;      // file offset of section header table
  uint flags;        // processor-specific flags
  ushort ehsize;     // size of this ELF header
  ushort phentsize;  // size of one program header entry
  ushort phnum;      // number of program headers
  ushort shentsize;  // size of one section header entry
  ushort shnum;      // number of section headers
  ushort shstrndx;   // section header string table index
};


// Program section header
struct proghdr {
  uint32 type;     // what kind of segment this is
  uint32 flags;    // permissions (read, write, exec)
  uint64 off;      // offset in the ELF file
  uint64 vaddr;    // virtual address where it should be loaded
  uint64 paddr;    // physical address (ignored in xv6/Linux user-space)
  uint64 filesz;   // how many bytes to read from file
  uint64 memsz;    // how many bytes in memory (may be bigger than filesz for .bss)
  uint64 align;    // required alignment in memory (usually page size = 4096)
};


// Values for Proghdr type
#define ELF_PROG_LOAD           1

// Flag bits for Proghdr flags
#define ELF_PROG_FLAG_EXEC      1
#define ELF_PROG_FLAG_WRITE     2
#define ELF_PROG_FLAG_READ      4
