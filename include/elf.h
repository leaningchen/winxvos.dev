#ifndef __ELF_H__
#define __ELF_H__

/*===========================================================================
 * include/elf.h — ELF64 格式定义（仅包含 exec 所需的最小子集）
 *
 * 参考：System V ABI AMD64, ELF-64 Object File Format
 *===========================================================================*/

#include <types.h>

/* ELF 魔数 */
#define ELF_MAGIC   0x464C457FU     /* "\x7fELF" 小端 */

/* e_type */
#define ET_EXEC     2   /* 可执行文件 */

/* e_machine */
#define EM_X86_64   62

/* p_type */
#define PT_LOAD     1   /* 可装载段 */

/* p_flags */
#define PF_X        0x1   /* Execute */
#define PF_W        0x2   /* Write */
#define PF_R        0x4   /* Read */

/*---------------------------------------------------------------------------
 * ELF64 文件头
 *---------------------------------------------------------------------------*/
struct elfhdr {
    uint32_t magic;       /* e_ident[0..3]: 魔数，应为 ELF_MAGIC */
    uint8_t  elf[12];     /* e_ident[4..15]: 其余标识字节 */
    uint16_t type;        /* e_type */
    uint16_t machine;     /* e_machine */
    uint32_t version;     /* e_version */
    uint64_t entry;       /* e_entry: 程序入口虚拟地址 */
    uint64_t phoff;       /* e_phoff: 程序头表文件偏移 */
    uint64_t shoff;       /* e_shoff: 节头表文件偏移（exec 不用）*/
    uint32_t flags;       /* e_flags */
    uint16_t ehsize;      /* e_ehsize: ELF 头大小 */
    uint16_t phentsize;   /* e_phentsize: 程序头项大小 */
    uint16_t phnum;       /* e_phnum: 程序头项数量 */
    uint16_t shentsize;   /* e_shentsize */
    uint16_t shnum;       /* e_shnum */
    uint16_t shstrndx;    /* e_shstrndx */
};

/*---------------------------------------------------------------------------
 * ELF64 程序头（段描述符）
 *---------------------------------------------------------------------------*/
struct proghdr {
    uint32_t type;        /* p_type */
    uint32_t flags;       /* p_flags */
    uint64_t off;         /* p_offset: 段在文件中的偏移 */
    uint64_t vaddr;       /* p_vaddr: 段加载的虚拟地址 */
    uint64_t paddr;       /* p_paddr: 物理地址（忽略）*/
    uint64_t filesz;      /* p_filesz: 段在文件中的字节数 */
    uint64_t memsz;       /* p_memsz: 段在内存中的字节数（>= filesz）*/
    uint64_t align;       /* p_align: 对齐要求 */
};

#endif /* __ELF_H__ */
