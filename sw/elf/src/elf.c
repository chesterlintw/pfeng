/* =========================================================================
 *  Copyright 2018-2021 NXP
 *
 *  SPDX-License-Identifier: GPL-2.0
 *
 * ========================================================================= */

/**
 * @addtogroup dxgr_ELF
 * @{
 *
 * @file			elf.c
 * @version			0.0.0.0
 *
 * @brief			The ELF module. Module for loading executable ELF files.
 *
 */
/*==================================================================================================
==================================================================================================*/

/*==================================================================================================
                                         MISRA VIOLATIONS
==================================================================================================*/

/**
* @page misra_violations MISRA-C:2004 violations
*
* @section elf_c_REF_1
* Violates MISRA 2004 TODO Rule TODO,
*
*
*/

/*==================================================================================================
                                         INCLUDE FILES
 1) system and project includes
 2) needed interfaces from external units
 3) internal and external interfaces from this unit
==================================================================================================*/

#include "pfe_cfg.h"
#include "oal.h"

#include "elf_cfg.h"
#include "elf.h"

#include "hal.h"

/*==================================================================================================
                                      FILE VERSION CHECKS
==================================================================================================*/
#if (FALSE == ELF_CFG_ELF64_SUPPORTED) && (FALSE == ELF_CFG_ELF32_SUPPORTED)
    #error Either ELF32, ELF64, or both must be enabled.
#endif

/*==================================================================================================
                                        LOCAL MACROS
==================================================================================================*/
#define ELF64_HEADER_SIZE 64U
#define ELF32_HEADER_SIZE 52U
#define SHN_UNDEF       0U    /* Undefined/Not present */

/* Macros for change of endianness */
#define ENDIAN_SW_2B(VAL) ( (((VAL)&0xFF00U)>>8U) | (((VAL)&0x00FFU)<<8U) )
#define ENDIAN_SW_4B(VAL) ( (((VAL)&0xFF000000U)>>24U) | (((VAL)&0x000000FFU)<<24U) \
                          | (((VAL)&0x00FF0000U)>>8U) | (((VAL)&0x0000FF00U)<<8U) \
                          )
#define ENDIAN_SW_8B(VAL) ( (((VAL)&0xFF00000000000000U)>>56U) | (((VAL)&0x00000000000000FFU)<<56U) \
                          | (((VAL)&0x00FF000000000000U)>>40U) | (((VAL)&0x000000000000FF00U)<<40U) \
                          | (((VAL)&0x0000FF0000000000U)>>24U) | (((VAL)&0x0000000000FF0000U)<<24U) \
                          | (((VAL)&0x000000FF00000000U)>>8U ) | (((VAL)&0x00000000FF000000U)<<8U ) \
                          )

/*==================================================================================================
                          LOCAL TYPEDEFS (STRUCTURES, UNIONS, ENUMS)
==================================================================================================*/
typedef enum
{
    ELF_Endian_Little = 1,
    ELF_Endian_Big    = 2,
} ELF_Endian_t;

enum
{
    ELF_Type_Relocatable = 1U,
    ELF_Type_Executable = 2U,
    ELF_Type_Shared = 3U,
    ELF_Type_Core = 4U,
};

enum
{
    PT_NULL      = 0U,
    PT_LOAD      = 1U, /* Loadable segment */
    PT_DYNAMIC   = 2U,
    PT_INTERP    = 3U,
    PT_NOTE      = 4U,
    PT_SHLIB     = 5U,
    PT_PHDR      = 6U,
    PT_LOPROC    = 7U,
    PT_HIPROC    = 8U,
    PT_GNU_STACK = 9U,
};

/*==================================================================================================
                                       LOCAL CONSTANTS
==================================================================================================*/
#if TRUE == ELF_CFG_SECTION_PRINT_ENABLED
  #ifdef NXP_LOG_ENABLED /*  Debug message support */
    #if TRUE == ELF_CFG_SECTION_TABLE_USED
    static const int8_t aacSTypes[17][9] =
    {
        "NULL    ",
        "PROGBITS",
        "SYMTAB  ",
        "STRTAB  ",
        "RELA    ",
        "HASH    ",
        "DYNAMIC ",
        "NOTE    ",
        "NOBITS  ",
        "REL     ",
        "SHLIB   ",
        "DYNSYM  ",
        "LOPROC  ",
        "HIPROC  ",
        "LOUSER  ",
        "HIUSER  ",
        "UNDEFINE",
    };
    static const struct shf_flags_strings
    {
        uint32_t u32Flag;
        char_t   *szString;
    } ShT_Flags_Strings[] =
    {
        {0x1U,       "WRITE"},
        {0x2U,       "ALLOC"},
        {0x4U,       "EXECINSTR"},
        {0x10U,      "MERGE"},
        {0x20U,      "STRINGS"},
        {0x40U,      "INFO_LINK"},
        {0x80U,      "LINK_ORDER"},
        {0x100U,     "OS_NONCONFORMING"},
        {0x200U,     "GROUP"},
        {0x400U,     "TLS"},
        {0x0ff00000U,"MASKOS"},
        {0xf0000000U,"MASKPROC"},
        {0x4000000U, "ORDERED"},
        {0x8000000U, "EXCLUDE"},
    };
    static const uint32_t u32ShT_Flags_Strings_Count = sizeof(ShT_Flags_Strings) / sizeof(struct shf_flags_strings);
    #endif /* ELF_CFG_SECTION_TABLE_USED */
    #if TRUE == ELF_CFG_PROGRAM_TABLE_USED
    static const int8_t aacPTypes[11][10] =
    {
        "NULL     ",
        "LOAD     ",
        "DYNAMIC  ",
        "INTERP   ",
        "NOTE     ",
        "SHLIB    ",
        "PHDR     ",
        "LOPROC   ",
        "HIPROC   ",
        "GNU_STACK",
        "UNDEFINED",
    };
    #endif /* ELF_CFG_PROGRAM_TABLE_USED */
  #endif /* NXP_LOG_ENABLED */
#endif /* ELF_CFG_SECTION_PRINT_ENABLED */

/*==================================================================================================
                                       LOCAL VARIABLES
==================================================================================================*/

/*==================================================================================================
                                       GLOBAL CONSTANTS
==================================================================================================*/

/*==================================================================================================
                                       GLOBAL VARIABLES
==================================================================================================*/

/*==================================================================================================
                                   LOCAL FUNCTION PROTOTYPES
==================================================================================================*/
/* GENERAL */
static bool_t LoadFileData(const ELF_File_t *pElfFile, uint32_t u32Offset, uint32_t u32Size, void *pvDestMem);
static inline ELF_Endian_t GetLocalEndian(void);
/* ELF64 */
#if TRUE == ELF_CFG_ELF64_SUPPORTED
    static bool_t ELF64_LoadTables(ELF_File_t *pElfFile, bool_t bIsCrosEndian);
    static void ELF64_HeaderSwitchEndianness(Elf64_Ehdr *prElf64Header);
    static bool_t ELF64_Load(bool_t bIsCrosEndian,ELF_File_t *pElfFile,uint32_t *u32NamesSectionOffset,uint32_t *u32NamesSectionSize);
    static void ELF64_ProgTabSwitchEndianness(Elf64_Phdr *arProgHead64, uint32_t u32NumItems);
    static void ELF64_SectTabSwitchEndianness(Elf64_Shdr *arSectHead64, uint32_t u32NumItems);
    #if TRUE == ELF_CFG_PROGRAM_TABLE_USED
        static bool_t ELF64_ProgSectFindNext( ELF_File_t *pElfFile, uint32_t *pu32ProgIdx,
                                               uint64_t *pu64LoadVAddr, uint64_t *pu64LoadPAddr, uint64_t *pu64Length
                                             );
        static bool_t ELF64_ProgSectLoad( const ELF_File_t *pElfFile,
                                           uint32_t u32ProgIdx, addr_t AccessAddr, addr_t AllocSize
                                         );
    #endif
    #if TRUE == ELF_CFG_SECTION_TABLE_USED
        static bool_t ELF64_SectFindName( const ELF_File_t *pElfFile, const char_t *szSectionName,
                                           uint32_t *pu32SectIdx, uint64_t *pu64LoadAddr, uint64_t *pu64Length
                                         );
        static bool_t ELF64_SectLoad( const ELF_File_t *pElfFile,
                                       uint32_t u32SectIdx, addr_t AccessAddr, addr_t AllocSize
                                     );
    #endif
    #if TRUE == ELF_CFG_SECTION_PRINT_ENABLED
        static void ELF64_PrintSections(const ELF_File_t *pElfFile);
    #endif /* ELF_CFG_SECTION_PRINT_ENABLED */
#endif /* ELF_CFG_ELF64_SUPPORTED */
/* ELF32 */
#if TRUE == ELF_CFG_ELF32_SUPPORTED
    static bool_t ELF32_LoadTables(ELF_File_t *pElfFile, bool_t bIsCrosEndian);
    static void ELF32_HeaderSwitchEndianness(Elf32_Ehdr *prElf32Header);
    static bool_t ELF32_Load(bool_t bIsCrosEndian,ELF_File_t *pElfFile,uint32_t *u32NamesSectionOffset,uint32_t *u32NamesSectionSize);
    static void ELF32_ProgTabSwitchEndianness(Elf32_Phdr *arProgHead32, uint32_t u32NumItems);
    static void ELF32_SectTabSwitchEndianness(Elf32_Shdr *arSectHead32, uint32_t u32NumItems);
    #if TRUE == ELF_CFG_PROGRAM_TABLE_USED
        static bool_t ELF32_ProgSectFindNext( ELF_File_t *pElfFile, uint32_t *pu32ProgIdx,
                                               uint64_t *pu64LoadVAddr, uint64_t *pu64LoadPAddr, uint64_t *pu64Length
                                             );
        static bool_t ELF32_ProgSectLoad( const ELF_File_t *pElfFile,
                                           uint32_t u32ProgIdx, addr_t AccessAddr, addr_t AllocSize
                                         );
    #endif
    #if TRUE == ELF_CFG_SECTION_TABLE_USED
        static bool_t ELF32_SectFindName( const ELF_File_t *pElfFile, const char_t *szSectionName,
                                           uint32_t *pu32SectIdx, uint64_t *pu64LoadAddr, uint64_t *pu64Length
                                         );
        static bool_t ELF32_SectLoad( const ELF_File_t *pElfFile,
                                       uint32_t u32SectIdx, addr_t AccessAddr, addr_t AllocSize
                                     );
    #endif
    #if TRUE == ELF_CFG_SECTION_PRINT_ENABLED
        static void ELF32_PrintSections(const ELF_File_t *pElfFile);
    #endif /* ELF_CFG_SECTION_PRINT_ENABLED */
#endif /* ELF_CFG_ELF32_SUPPORTED */

static uint32_t buf_read(void *src_buf, uint32_t u32FileSize, uint32_t u32Offset, void *dst_buf, uint32_t nbytes);
static void ELF_FreePtr(ELF_File_t *pElfFile);
static bool_t ELF_LoadTables(ELF_File_t *pElfFile, uint32_t *u32NamesSectionOffset, uint32_t *u32NamesSectionSize);

/*==================================================================================================
                                       LOCAL FUNCTIONS
==================================================================================================*/
/*================================================================================================*/
/**
* @brief        Purpose of this function is to implement the operations and checks only once.
* @param[in]    pElfFile Structure holding all informations about opened ELF file.
* @param[in]    u32Offset Offset within file.
* @param[in]    u32Size Number of bytes to load.
* @param[out]   pvDestMem Data from file are written here.
* @retval       TRUE Succeeded
* @retval       FALSE Failed
*/
/* Purpose of this function is to implement all the operations and checks only once */
static bool_t LoadFileData(const ELF_File_t *pElfFile, uint32_t u32Offset, uint32_t u32Size, void *pvDestMem)
{
    bool_t bSuccess = FALSE;

#if defined(PFE_CFG_NULL_ARG_CHECK)
    if (unlikely((NULL == pElfFile) || (NULL == pvDestMem)))
    {
        NXP_LOG_ERROR("NULL argument received\n");
        return FALSE;
    }
#endif /* PFE_CFG_NULL_ARG_CHECK */

    /* Does it fit to file? */
    if ((u32Offset + u32Size) > pElfFile->u32FileSize)
    {
        NXP_LOG_ERROR( "LoadFileData: Requested data block exceeds size of the file\n");
        NXP_LOG_INFO("\n");
    }
    /* Try to read. */
    else if (u32Size != buf_read(pElfFile->pvData, pElfFile->u32FileSize, u32Offset, pvDestMem, u32Size))
    {
        NXP_LOG_ERROR("LoadFileData: Reading program header failed\n");
    }
    /* DONE */
    else
    {
        bSuccess = TRUE;
    }
    return bSuccess;
}

/*================================================================================================*/
/**
* @brief        Determines endianess of the machine it is running on.
* @return       The endianness.
*/
static inline ELF_Endian_t GetLocalEndian(void)
{
    ELF_Endian_t RetEndian = ELF_Endian_Big;
    union
    {
        uint32_t u32Value;
        uint8_t  au8Values[4U];
    } TheUnion;
    TheUnion.u32Value = 1U;
    if(TheUnion.au8Values[0U] == 1U)
    {
        RetEndian = ELF_Endian_Little;
    }
    return RetEndian;
}


#if TRUE == ELF_CFG_ELF32_SUPPORTED
/*================================================================================================*/
static bool_t ELF32_LoadTables(ELF_File_t *pElfFile, bool_t bIsCrosEndian)
{
    bool_t bProgStatus = TRUE;
    bool_t bSectStatus = FALSE;

#if defined(PFE_CFG_NULL_ARG_CHECK)
    if (unlikely(NULL == pElfFile))
    {
        NXP_LOG_ERROR("NULL argument received\n");
        return FALSE;
    }
#endif /* PFE_CFG_NULL_ARG_CHECK */

  #if TRUE == ELF_CFG_PROGRAM_TABLE_USED
    bProgStatus = FALSE;
    /* === Load program header from file ============================================ */
    /* Check integrity */
    if(sizeof(Elf32_Phdr) != pElfFile->Header.r32.e_phentsize)
    {
        NXP_LOG_ERROR("ELF32_LoadTables: Unexpected progam header entry size\n");
    }
    /* Check the size */
    else if ((pElfFile->Header.r32.e_phoff + ((uint32_t)pElfFile->Header.r32.e_phentsize * (uint32_t)pElfFile->Header.r32.e_phnum)) > pElfFile->u32FileSize)
    {
        NXP_LOG_ERROR("ELF32_LoadTables: Requested data block exceeds size of the file\n");
        NXP_LOG_INFO("\n");
    }
    /* All checkes passed */
    else
    {
        /* Save the pointer */
        pElfFile->arProgHead32 = (Elf32_Phdr *)(((uint8_t*)pElfFile->pvData) + pElfFile->Header.r32.e_phoff);

        /* Now handle endianness */
        if (bIsCrosEndian)
        {
            ELF32_ProgTabSwitchEndianness(pElfFile->arProgHead32, pElfFile->Header.r32.e_phnum);
        }
        bProgStatus = TRUE;
    }
  #endif /* ELF_CFG_PROGRAM_TABLE_USED */
  #if TRUE == ELF_CFG_SECTION_TABLE_USED
#endif

#if TRUE == ELF_CFG_SECTION_TABLE_USED
    /* === Load section header from file ============================================ */
    if (FALSE == bProgStatus)
    {
        ; /* Loading the other table failed, this will abort. */
    }
    /* Check integrity */
    else if (sizeof(Elf32_Shdr) != pElfFile->Header.r32.e_shentsize)
    {
        NXP_LOG_ERROR("ELF32_LoadTables: Unexpected section header entry size\n");
    }
    /* Check the size */
    else if ((pElfFile->Header.r32.e_shoff + ((uint32_t)pElfFile->Header.r32.e_shentsize * (uint32_t)pElfFile->Header.r32.e_shnum)) > pElfFile->u32FileSize) 
    {
        NXP_LOG_ERROR("ELF32_LoadTables: Requested data block exceeds size of the file\n");
    }
    else /* All checkes passed */
    {
        /* Save the pointer */
        pElfFile->arSectHead32 = (Elf32_Shdr *)(((uint8_t*)pElfFile->pvData) + pElfFile->Header.r32.e_shoff);
        /* Now handle endianness */
        if (bIsCrosEndian)
        {
            ELF32_SectTabSwitchEndianness(pElfFile->arSectHead32, pElfFile->Header.r32.e_shnum);
        }
        bSectStatus = TRUE;
    }
  #else /* ELF_CFG_SECTION_TABLE_USED */
    bSectStatus = bProgStatus;
  #endif /* ELF_CFG_SECTION_TABLE_USED */
    return bSectStatus;
}

/*================================================================================================*/
static void ELF32_HeaderSwitchEndianness(Elf32_Ehdr *prElf32Header)
{
#if defined(PFE_CFG_NULL_ARG_CHECK)
    if (unlikely(NULL == prElf32Header))
    {
        NXP_LOG_ERROR("NULL argument received\n");
        return;
    }
#endif /* PFE_CFG_NULL_ARG_CHECK */

    prElf32Header->e_type       = ENDIAN_SW_2B(prElf32Header->e_type);
    prElf32Header->e_machine    = ENDIAN_SW_2B(prElf32Header->e_machine);
    prElf32Header->e_version    = ENDIAN_SW_4B(prElf32Header->e_version);
    prElf32Header->e_entry      = ENDIAN_SW_4B(prElf32Header->e_entry);
    prElf32Header->e_phoff      = ENDIAN_SW_4B(prElf32Header->e_phoff);
    prElf32Header->e_shoff      = ENDIAN_SW_4B(prElf32Header->e_shoff);
    prElf32Header->e_flags      = ENDIAN_SW_4B(prElf32Header->e_flags);
    prElf32Header->e_ehsize     = ENDIAN_SW_2B(prElf32Header->e_ehsize);
    prElf32Header->e_phentsize  = ENDIAN_SW_2B(prElf32Header->e_phentsize);
    prElf32Header->e_phnum      = ENDIAN_SW_2B(prElf32Header->e_phnum);
    prElf32Header->e_shentsize  = ENDIAN_SW_2B(prElf32Header->e_shentsize);
    prElf32Header->e_shnum      = ENDIAN_SW_2B(prElf32Header->e_shnum);
    prElf32Header->e_shstrndx   = ENDIAN_SW_2B(prElf32Header->e_shstrndx);
}

static bool_t ELF32_Load(bool_t bIsCrosEndian,ELF_File_t *pElfFile,uint32_t *u32NamesSectionOffset,uint32_t *u32NamesSectionSize)
{
    bool_t    bRetVal = FALSE;

    if (bIsCrosEndian)
    {
        ELF32_HeaderSwitchEndianness(&(pElfFile->Header.r32));
    }
    if ((uint16_t)ELF_Type_Executable != pElfFile->Header.r32.e_type)
    {
        NXP_LOG_ERROR("ELF_Open: Only executable ELFs are supported\n");
    }
    else if (FALSE == ELF32_LoadTables(pElfFile, bIsCrosEndian))
    {
        NXP_LOG_ERROR("ELF_Open: Failed to load tables\n");
    }
    /* Endianness is now solved in all tables */
#if TRUE == ELF_CFG_SECTION_TABLE_USED
    /* Look for section names section */
    else if ((pElfFile->Header.r32.e_shstrndx == SHN_UNDEF)
          || (pElfFile->Header.r32.e_shstrndx >= pElfFile->Header.r32.e_shnum)
          || (0U == pElfFile->arSectHead32[pElfFile->Header.r32.e_shstrndx].sh_size)
           )
    {
        NXP_LOG_ERROR("ELF_Open: Section names not found\n");
    }
    else
    {
        *u32NamesSectionOffset = pElfFile->arSectHead32[pElfFile->Header.r32.e_shstrndx].sh_offset;
        *u32NamesSectionSize = pElfFile->arSectHead32[pElfFile->Header.r32.e_shstrndx].sh_size;
        bRetVal = TRUE;
    }
#else  /* ELF_CFG_SECTION_TABLE_USED */
    else
    {
        bRetVal = TRUE;
    }
#endif /* ELF_CFG_SECTION_TABLE_USED */

    return bRetVal;
}

/*================================================================================================*/
static void ELF32_ProgTabSwitchEndianness(Elf32_Phdr *arProgHead32, uint32_t u32NumItems)
{
    uint32_t u32Idx;

#if defined(PFE_CFG_NULL_ARG_CHECK)
    if (unlikely(NULL == arProgHead32))
    {
        NXP_LOG_ERROR("NULL argument received\n");
        return;
    }
#endif /* PFE_CFG_NULL_ARG_CHECK */

    for(u32Idx=0U; u32Idx<u32NumItems; u32Idx++)
    {
        arProgHead32[u32Idx].p_type   = ENDIAN_SW_4B(arProgHead32[u32Idx].p_type);
        arProgHead32[u32Idx].p_offset = ENDIAN_SW_4B(arProgHead32[u32Idx].p_offset);
        arProgHead32[u32Idx].p_vaddr  = ENDIAN_SW_4B(arProgHead32[u32Idx].p_vaddr);
        arProgHead32[u32Idx].p_paddr  = ENDIAN_SW_4B(arProgHead32[u32Idx].p_paddr);
        arProgHead32[u32Idx].p_filesz = ENDIAN_SW_4B(arProgHead32[u32Idx].p_filesz);
        arProgHead32[u32Idx].p_memsz  = ENDIAN_SW_4B(arProgHead32[u32Idx].p_memsz);
        arProgHead32[u32Idx].p_flags  = ENDIAN_SW_4B(arProgHead32[u32Idx].p_flags);
        arProgHead32[u32Idx].p_align  = ENDIAN_SW_4B(arProgHead32[u32Idx].p_align);
    }
}

/*================================================================================================*/
static void ELF32_SectTabSwitchEndianness(Elf32_Shdr *arSectHead32, uint32_t u32NumItems)
{
    uint32_t u32Idx;

#if defined(PFE_CFG_NULL_ARG_CHECK)
    if (unlikely(NULL == arSectHead32))
    {
        NXP_LOG_ERROR("NULL argument received\n");
        return;
    }
#endif /* PFE_CFG_NULL_ARG_CHECK */

    for(u32Idx=0U; u32Idx<u32NumItems; u32Idx++)
    {
        arSectHead32[u32Idx].sh_name      = ENDIAN_SW_4B(arSectHead32[u32Idx].sh_name);
        arSectHead32[u32Idx].sh_type      = ENDIAN_SW_4B(arSectHead32[u32Idx].sh_type);
        arSectHead32[u32Idx].sh_flags     = ENDIAN_SW_4B(arSectHead32[u32Idx].sh_flags);
        arSectHead32[u32Idx].sh_addr      = ENDIAN_SW_4B(arSectHead32[u32Idx].sh_addr);
        arSectHead32[u32Idx].sh_offset    = ENDIAN_SW_4B(arSectHead32[u32Idx].sh_offset);
        arSectHead32[u32Idx].sh_size      = ENDIAN_SW_4B(arSectHead32[u32Idx].sh_size);
        arSectHead32[u32Idx].sh_link      = ENDIAN_SW_4B(arSectHead32[u32Idx].sh_link);
        arSectHead32[u32Idx].sh_info      = ENDIAN_SW_4B(arSectHead32[u32Idx].sh_info);
        arSectHead32[u32Idx].sh_addralign = ENDIAN_SW_4B(arSectHead32[u32Idx].sh_addralign);
        arSectHead32[u32Idx].sh_entsize   = ENDIAN_SW_4B(arSectHead32[u32Idx].sh_entsize);
    }
}

  #if TRUE == ELF_CFG_PROGRAM_TABLE_USED
/*================================================================================================*/
static bool_t ELF32_ProgSectFindNext( ELF_File_t *pElfFile, uint32_t *pu32ProgIdx,
                                       uint64_t *pu64LoadVAddr, uint64_t *pu64LoadPAddr, uint64_t *pu64Length
                                     )
{
    bool_t bRetVal = FALSE;

#if defined(PFE_CFG_NULL_ARG_CHECK)
    /* Check prerequisities */
    if (unlikely((NULL == pElfFile) || (NULL == pElfFile->arProgHead32)))
    {
        NXP_LOG_ERROR("ELF32_ProgSectFindNext: Failed - elf not opened!\n");
    }
    else
#endif /* PFE_CFG_NULL_ARG_CHECK */
    {
        /* Find a record having RAM area */
        while (pElfFile->u32ProgScanIdx < pElfFile->Header.r32.e_phnum)
        {
            if (((uint32_t)PT_LOAD == pElfFile->arProgHead32[pElfFile->u32ProgScanIdx].p_type) /* Has RAM area */
                && (0U != pElfFile->arProgHead32[pElfFile->u32ProgScanIdx].p_memsz)      /* Size != 0 */
                )
            {   /* Match found */
                /* Set returned values */
                if (NULL != pu32ProgIdx)
                {
                    *pu32ProgIdx = pElfFile->u32ProgScanIdx;
                }
                if (NULL != pu64LoadVAddr)
                {
                    *pu64LoadVAddr = pElfFile->arProgHead32[pElfFile->u32ProgScanIdx].p_vaddr;
                }
                if (NULL != pu64LoadPAddr)
                {
                    *pu64LoadPAddr = pElfFile->arProgHead32[pElfFile->u32ProgScanIdx].p_paddr;
                }
                if (NULL != pu64Length)
                {
                    *pu64Length = pElfFile->arProgHead32[pElfFile->u32ProgScanIdx].p_memsz;
                }
                bRetVal = TRUE;
                pElfFile->u32ProgScanIdx++;
                break;
            }
            else
            {
                pElfFile->u32ProgScanIdx++;
            }
        }
    }

    return bRetVal;
}

/*================================================================================================*/
static bool_t ELF32_ProgSectLoad(const ELF_File_t *pElfFile, uint32_t u32ProgIdx,
                                   addr_t AccessAddr, addr_t AllocSize
                                 )
{
    bool_t bSuccess = FALSE;

#if defined(PFE_CFG_NULL_ARG_CHECK)
    /* CHECK */
    if (unlikely((NULL == pElfFile) || (NULL == pElfFile->arProgHead32)))
    {
        NXP_LOG_ERROR("ELF32_ProgSectLoad: Failed - elf not loaded!\n");
    }
    else
#endif /* PFE_CFG_NULL_ARG_CHECK */
    if (u32ProgIdx >= pElfFile->Header.r32.e_phnum)
    {
        NXP_LOG_ERROR("ELF32_ProgSectLoad: Invalid program index: %u\n", (uint_t)u32ProgIdx);
    }
    else if ((uint32_t)PT_LOAD != pElfFile->arProgHead32[u32ProgIdx].p_type)
    {
        NXP_LOG_ERROR("ELF32_ProgSectLoad: This section has no associated RAM area\n");
    }
    else if (AllocSize < pElfFile->arProgHead32[u32ProgIdx].p_memsz)
    {
        NXP_LOG_ERROR("ELF32_ProgSectLoad: Section does not fit to allocated memory\n");
    }
    else if (pElfFile->arProgHead32[u32ProgIdx].p_filesz > pElfFile->arProgHead32[u32ProgIdx].p_memsz)
    {
        NXP_LOG_ERROR("ELF32_ProgSectLoad: Section size mismatch.\n");
    }
    /* LOAD */
    else
    {   /* All OK */
        /* p_filesz bytes of data at the beginning of the memory area shall be copied from file
           the rest up to p_memsz bytes shal be set to 0
        */
        if (0U != pElfFile->arProgHead32[u32ProgIdx].p_filesz)
        {   /* Read from file */
            if (FALSE == LoadFileData(pElfFile, /* pElfFile, */
                pElfFile->arProgHead32[u32ProgIdx].p_offset, /* u32Offset, */
                pElfFile->arProgHead32[u32ProgIdx].p_filesz, /* u32Size, */
                (void *)AccessAddr /* pvDestMem */
            )
                )
            {
                NXP_LOG_ERROR("ELF32_ProgSectLoad: Failed to load section from file\n");
            }
            else
            {   /* Reading done */
                bSuccess = TRUE;
            }
        }
        else
        {   /* Reading skipped */
            bSuccess = TRUE;
        }

        /* Pad rest with zeros */
        if ((TRUE == bSuccess)
         && (pElfFile->arProgHead32[u32ProgIdx].p_memsz > pElfFile->arProgHead32[u32ProgIdx].p_filesz)
            )
        {
            (void)memset((void *)(AccessAddr + pElfFile->arProgHead32[u32ProgIdx].p_filesz),
                    0,
                    pElfFile->arProgHead32[u32ProgIdx].p_memsz - pElfFile->arProgHead32[u32ProgIdx].p_filesz
                  );
        }
    }
    return bSuccess;
}
  #endif /* ELF_CFG_PROGRAM_TABLE_USED */

  #if TRUE == ELF_CFG_SECTION_TABLE_USED

/*================================================================================================*/
static bool_t ELF32_SectFindName( const ELF_File_t *pElfFile, const char_t *szSectionName,
                                   uint32_t *pu32SectIdx, uint64_t *pu64LoadAddr, uint64_t *pu64Length
                                 )
{
    bool_t bRetVal = FALSE;
    bool_t bFound = FALSE;
    uint32_t SectIdx;

#if defined(PFE_CFG_NULL_ARG_CHECK)
    /* Check prerequisites */
    if (unlikely((NULL == pElfFile) || (NULL == pElfFile->arSectHead32) || (NULL == pElfFile->acSectNames)))
    {
        NXP_LOG_ERROR("ELF32_SectFindName: Failed - elf not opened!\n");
    }
    else
#endif /* PFE_CFG_NULL_ARG_CHECK */
    {
        /* Search section table */
        for (SectIdx = 0U; SectIdx < pElfFile->Header.r32.e_shnum; SectIdx++)
        {
            if (0 == strcmp((char_t *)(pElfFile->acSectNames + pElfFile->arSectHead32[SectIdx].sh_name), szSectionName))
            {   /* Found */
                if (NULL != pu32SectIdx)
                {
                    *pu32SectIdx = SectIdx;
                }
                if (NULL != pu64Length)
                {
                    *pu64Length = pElfFile->arSectHead32[SectIdx].sh_size;
                }
                if (NULL != pu64LoadAddr)
                {
                    *pu64LoadAddr = pElfFile->arSectHead32[SectIdx].sh_addr;
                }
                bFound = TRUE;
                bRetVal = TRUE;
                break;
            }
        }
        if (FALSE == bFound)
        {
            NXP_LOG_INFO("ELF32_SectFindName: Section %s not found\n", szSectionName);
        }
    }

    return bRetVal;
}

/*================================================================================================*/
static bool_t ELF32_SectLoad(const ELF_File_t *pElfFile, uint32_t u32SectIdx, addr_t AccessAddr, addr_t AllocSize)
{
    bool_t bSuccess = FALSE;

#if defined(PFE_CFG_NULL_ARG_CHECK)
    /* CHECK */
    if (unlikely((NULL == pElfFile) || (NULL == pElfFile->arSectHead32)))
    {
        NXP_LOG_ERROR("ELF32_SectLoad: Failed - elf not loaded!\n");
    }
    else
#endif /* PFE_CFG_NULL_ARG_CHECK */
    if (u32SectIdx >= pElfFile->Header.r32.e_shnum)
    {
        NXP_LOG_ERROR("ELF32_SectLoad: Invalid section index: %u\n", (uint_t)u32SectIdx);
    }
    else if (AllocSize < pElfFile->arSectHead32[u32SectIdx].sh_size)
    {
        NXP_LOG_ERROR("ELF32_SectLoad: Section does not fit to allocated memory\n");
    }
    /* LOAD */
    else
    {   /* All OK */
        if ((uint32_t)SHT_NOBITS == pElfFile->arSectHead32[u32SectIdx].sh_type)
        {   /* Fill with zeros */
            (void)memset((void *)AccessAddr, 0, pElfFile->arSectHead32[u32SectIdx].sh_size);
            bSuccess = TRUE;
        }
        else
        {   /* Copy from file */
            if (FALSE == LoadFileData(pElfFile, /* pElfFile, */
                                      pElfFile->arSectHead32[u32SectIdx].sh_offset, /* u32Offset, */
                                      pElfFile->arSectHead32[u32SectIdx].sh_size, /* u32Size, */
                                      (void *)AccessAddr /* pvDestMem */
                                      )
            )
            {
                NXP_LOG_ERROR("ELF32_SectLoad: Failed to load section from file\n");
            }
            else
            {   /* Reading done */
                bSuccess = TRUE;
            }
        }
    }
    return bSuccess;
}
  #endif /* ELF_CFG_SECTION_TABLE_USED */
  #if TRUE == ELF_CFG_SECTION_PRINT_ENABLED

/*================================================================================================*/
static void ELF32_PrintSections(const ELF_File_t *pElfFile)
{
#ifdef NXP_LOG_ENABLED /*  Debug message support */
    uint32_t SectIdx;
    uint32_t ProgIdx;

#if defined(PFE_CFG_NULL_ARG_CHECK)
    /* Check prerequisities */
    if (unlikely ((NULL == pElfFile)
  #if TRUE == ELF_CFG_SECTION_TABLE_USED
     || (NULL == pElfFile->arSectHead32)
     || (NULL == pElfFile->acSectNames)
  #endif /* ELF_CFG_SECTION_TABLE_USED */
  #if TRUE == ELF_CFG_PROGRAM_TABLE_USED
     || (NULL == pElfFile->arProgHead32)
  #endif /* ELF_CFG_PROGRAM_TABLE_USED */
      ))
    {
        NXP_LOG_ERROR("NXP_LOG_INFOSections: Failed - elf not opened!\n");
    }
    else
#endif /* PFE_CFG_NULL_ARG_CHECK */
    {
#if TRUE == ELF_CFG_SECTION_TABLE_USED
        /* Search section table */
        NXP_LOG_INFO("\n");
        NXP_LOG_INFO("File contains %hu sections:\n", pElfFile->Header.r32.e_shnum);
        NXP_LOG_INFO("     SectionName    Type        FileOffset    FileSize      LoadAddress   Flags\n");
        for (SectIdx = 0U; SectIdx < pElfFile->Header.r32.e_shnum; SectIdx++)
        {
            uint32_t u32Type = pElfFile->arSectHead32[SectIdx].sh_type;
            uint32_t u32FlagIdx;

            if (u32Type >= 16U)
            {
                u32Type = 16U; /* Undefined */
            }
            NXP_LOG_INFO("%16s", pElfFile->acSectNames + pElfFile->arSectHead32[SectIdx].sh_name);
            NXP_LOG_INFO("%12s    0x%08x    0x%08x    0x%08x    ",
                        aacSTypes[u32Type],
                        (uint_t)pElfFile->arSectHead32[SectIdx].sh_offset,
                        (uint_t)pElfFile->arSectHead32[SectIdx].sh_size,
                        (uint_t)pElfFile->arSectHead32[SectIdx].sh_addr
                      );
            /* Now print flags on separate line: */
            for (u32FlagIdx = 0U; u32FlagIdx<u32ShT_Flags_Strings_Count; u32FlagIdx++)
            {
                if (0U != (ShT_Flags_Strings[u32FlagIdx].u32Flag & pElfFile->arSectHead32[SectIdx].sh_flags))
                {
                    NXP_LOG_INFO("%s, ", ShT_Flags_Strings[u32FlagIdx].szString);
                }
            }
            NXP_LOG_INFO("\n");
        }
#endif /* ELF_CFG_SECTION_TABLE_USED */
#if TRUE == ELF_CFG_PROGRAM_TABLE_USED
        /* Search program table */
        NXP_LOG_INFO("\n");
        NXP_LOG_INFO("File contains %hu program sections:\n", pElfFile->Header.r32.e_phnum);
        NXP_LOG_INFO("Idx Type        FileOffset         FileSize           "
                   "LoadVirtAddress    LoadPhysAddress    MemorySize         \n"
                  );
        for (ProgIdx = 0U; ProgIdx < pElfFile->Header.r32.e_phnum; ProgIdx++)
        {
            /* Try to find the name of the section in section header */
            uint32_t u32Type = pElfFile->arProgHead32[ProgIdx].p_type;

            if (u32Type >= 10U)
            {
                u32Type = 10U; /* Undefined */
            }

            /* Print program header data */
            NXP_LOG_INFO("%3u %s   0x%08x         0x%08x         0x%08x         0x%08x         0x%08x",
                        (uint_t)ProgIdx,
                        aacPTypes[u32Type],
                        (uint_t)pElfFile->arProgHead32[ProgIdx].p_offset,
                        (uint_t)pElfFile->arProgHead32[ProgIdx].p_filesz,
                        (uint_t)pElfFile->arProgHead32[ProgIdx].p_vaddr,
                        (uint_t)pElfFile->arProgHead32[ProgIdx].p_paddr,
                        (uint_t)pElfFile->arProgHead32[ProgIdx].p_memsz
                      );
            NXP_LOG_INFO("\n");
        }
#endif /* ELF_CFG_PROGRAM_TABLE_USED */
        NXP_LOG_INFO("\n");
    }
#else
    /* Do nothing */
    (void)pElfFile;
#endif /* NXP_LOG_ENABLED */

}
  #endif /* ELF_CFG_SECTION_PRINT_ENABLED */
#endif /* ELF_CFG_ELF32_SUPPORTED */

#if TRUE == ELF_CFG_ELF64_SUPPORTED
/*================================================================================================*/
static bool_t ELF64_LoadTables(ELF_File_t *pElfFile, bool_t bIsCrosEndian)
{
    bool_t bProgStatus = TRUE;
    bool_t bSectStatus = FALSE;

#if defined(PFE_CFG_NULL_ARG_CHECK)
    if (unlikely(NULL == pElfFile))
    {
        NXP_LOG_ERROR("NULL argument received\n");
        return FALSE;
    }
#endif /* PFE_CFG_NULL_ARG_CHECK */

  #if TRUE == ELF_CFG_PROGRAM_TABLE_USED
    bProgStatus = FALSE;
    /* === Load program header from file ============================================ */
    /* Check integrity */
    if (sizeof(Elf64_Phdr) != pElfFile->Header.r64.e_phentsize)
    {
        NXP_LOG_ERROR("ELF64_LoadTables: Unexpected program header entry size\n");
    }
    else if ((pElfFile->Header.r64.e_phoff + ((uint64_t)pElfFile->Header.r64.e_phentsize * (uint64_t)pElfFile->Header.r64.e_phnum)) > (uint64_t)(pElfFile->u32FileSize))
    {
        NXP_LOG_ERROR("ELF64_LoadTables: Requested data block exceeds size of the file\n");
    }
    else /* All checks passed */
    {
        /* Save the pointer */
        pElfFile->arProgHead64 = (Elf64_Phdr *)(((uint8_t*)pElfFile->pvData) + pElfFile->Header.r64.e_phoff);
        /* Now handle endianness */
        if (bIsCrosEndian)
        {
            ELF64_ProgTabSwitchEndianness(pElfFile->arProgHead64, pElfFile->Header.r64.e_phnum);
        }
        bProgStatus = TRUE;
    }
  #endif /* ELF_CFG_PROGRAM_TABLE_USED */
  #if TRUE == ELF_CFG_SECTION_TABLE_USED
    /* === Load section header from file ============================================ */
    if (FALSE == bProgStatus)
    {
        ; /* Loading the other table failed, this will abort. */
    }
    /* Check integrity */
    else if (sizeof(Elf64_Shdr) != pElfFile->Header.r64.e_shentsize)
    {
        NXP_LOG_ERROR("ELF64_LoadTables: Unexpected section header entry size\n");
    }
    else if ((pElfFile->Header.r64.e_shoff + ((uint64_t)pElfFile->Header.r64.e_shentsize * (uint64_t)pElfFile->Header.r64.e_shnum)) > (uint64_t)(pElfFile->u32FileSize))
    {
        NXP_LOG_ERROR("ELF64_LoadTables: Requested data block exceeds size of the file\n");
    }
    else /* All checks passed */
    {
        /* Save the pointer */
        pElfFile->arSectHead64 = (Elf64_Shdr *)(((uint8_t*)pElfFile->pvData) + pElfFile->Header.r64.e_shoff);
        /* Now handle endianness */
        if (bIsCrosEndian)
        {
            ELF64_SectTabSwitchEndianness(pElfFile->arSectHead64, pElfFile->Header.r64.e_shnum);
        }
        bSectStatus = TRUE;
    }
  #else /* ELF_CFG_SECTION_TABLE_USED */
    bSectStatus = bProgStatus;
  #endif /* ELF_CFG_SECTION_TABLE_USED */
    return bSectStatus;
}

/*================================================================================================*/
static void ELF64_HeaderSwitchEndianness(Elf64_Ehdr *prElf64Header)
{
#if defined(PFE_CFG_NULL_ARG_CHECK)
    if (unlikely(NULL == prElf64Header))
    {
        NXP_LOG_ERROR("NULL argument received\n");
        return;
    }
#endif /* PFE_CFG_NULL_ARG_CHECK */

    prElf64Header->e_type       = ENDIAN_SW_2B(prElf64Header->e_type);
    prElf64Header->e_machine    = ENDIAN_SW_2B(prElf64Header->e_machine);
    prElf64Header->e_version    = ENDIAN_SW_4B(prElf64Header->e_version);
    prElf64Header->e_entry      = ENDIAN_SW_8B(prElf64Header->e_entry);
    prElf64Header->e_phoff      = ENDIAN_SW_8B(prElf64Header->e_phoff);
    prElf64Header->e_shoff      = ENDIAN_SW_8B(prElf64Header->e_shoff);
    prElf64Header->e_flags      = ENDIAN_SW_4B(prElf64Header->e_flags);
    prElf64Header->e_ehsize     = ENDIAN_SW_2B(prElf64Header->e_ehsize);
    prElf64Header->e_phentsize  = ENDIAN_SW_2B(prElf64Header->e_phentsize);
    prElf64Header->e_phnum      = ENDIAN_SW_2B(prElf64Header->e_phnum);
    prElf64Header->e_shentsize  = ENDIAN_SW_2B(prElf64Header->e_shentsize);
    prElf64Header->e_shnum      = ENDIAN_SW_2B(prElf64Header->e_shnum);
    prElf64Header->e_shstrndx   = ENDIAN_SW_2B(prElf64Header->e_shstrndx);
}

static bool_t ELF64_Load(bool_t bIsCrosEndian,ELF_File_t *pElfFile,uint32_t *u32NamesSectionOffset,uint32_t *u32NamesSectionSize)
{
    bool_t    bRetVal = FALSE;

    if (bIsCrosEndian)
    {
        ELF64_HeaderSwitchEndianness(&(pElfFile->Header.r64));
    }
    if ((uint16_t)ELF_Type_Executable != pElfFile->Header.r64.e_type)
    {
        NXP_LOG_ERROR("ELF_Open: Only executable ELFs are supported\n");
    }
    else if (FALSE == ELF64_LoadTables(pElfFile, bIsCrosEndian))
    {
        NXP_LOG_ERROR("ELF_Open: Failed to load tables\n");
    }
    /* Endianness is now solved in all tables */
#if TRUE == ELF_CFG_SECTION_TABLE_USED
    /* Look for section names section */
    else if ((pElfFile->Header.r64.e_shstrndx == SHN_UNDEF)
          || (pElfFile->Header.r64.e_shstrndx >= pElfFile->Header.r64.e_shnum)
          || (0U == pElfFile->arSectHead64[pElfFile->Header.r64.e_shstrndx].sh_size)
           )
    {
        NXP_LOG_ERROR("ELF_Open: Section names not found\n");
    }
    else
    {
        *u32NamesSectionOffset = (uint32_t)pElfFile->arSectHead64[pElfFile->Header.r64.e_shstrndx].sh_offset;
        *u32NamesSectionSize = (uint32_t)pElfFile->arSectHead64[pElfFile->Header.r64.e_shstrndx].sh_size;
        bRetVal = TRUE;
    }
#else  /* ELF_CFG_SECTION_TABLE_USED */
    else
    {
        bRetVal = TRUE;
    }
#endif /* ELF_CFG_SECTION_TABLE_USED */

    return bRetVal;
}

/*================================================================================================*/
static void ELF64_ProgTabSwitchEndianness(Elf64_Phdr *arProgHead64, uint32_t u32NumItems)
{
    uint32_t u32Idx;

#if defined(PFE_CFG_NULL_ARG_CHECK)
    if (unlikely(NULL == arProgHead64))
    {
        NXP_LOG_ERROR("NULL argument received\n");
        return;
    }
#endif /* PFE_CFG_NULL_ARG_CHECK */

    for(u32Idx=0U; u32Idx<u32NumItems; u32Idx++)
    {
        arProgHead64[u32Idx].p_type   = ENDIAN_SW_4B(arProgHead64[u32Idx].p_type);
        arProgHead64[u32Idx].p_flags  = ENDIAN_SW_4B(arProgHead64[u32Idx].p_flags);
        arProgHead64[u32Idx].p_offset = ENDIAN_SW_8B(arProgHead64[u32Idx].p_offset);
        arProgHead64[u32Idx].p_vaddr  = ENDIAN_SW_8B(arProgHead64[u32Idx].p_vaddr);
        arProgHead64[u32Idx].p_paddr  = ENDIAN_SW_8B(arProgHead64[u32Idx].p_paddr);
        arProgHead64[u32Idx].p_filesz = ENDIAN_SW_8B(arProgHead64[u32Idx].p_filesz);
        arProgHead64[u32Idx].p_memsz  = ENDIAN_SW_8B(arProgHead64[u32Idx].p_memsz);
        arProgHead64[u32Idx].p_align  = ENDIAN_SW_8B(arProgHead64[u32Idx].p_align);
    }
}

/*================================================================================================*/
static void ELF64_SectTabSwitchEndianness(Elf64_Shdr *arSectHead64, uint32_t u32NumItems)
{
    uint32_t u32Idx;

#if defined(PFE_CFG_NULL_ARG_CHECK)
    if (unlikely(NULL == arSectHead64))
    {
        NXP_LOG_ERROR("NULL argument received\n");
        return;
    }
#endif /* PFE_CFG_NULL_ARG_CHECK */

    for(u32Idx=0U; u32Idx<u32NumItems; u32Idx++)
    {
        arSectHead64[u32Idx].sh_name      = ENDIAN_SW_4B(arSectHead64[u32Idx].sh_name);
        arSectHead64[u32Idx].sh_type      = ENDIAN_SW_4B(arSectHead64[u32Idx].sh_type);
        arSectHead64[u32Idx].sh_flags     = ENDIAN_SW_8B(arSectHead64[u32Idx].sh_flags);
        arSectHead64[u32Idx].sh_addr      = ENDIAN_SW_8B(arSectHead64[u32Idx].sh_addr);
        arSectHead64[u32Idx].sh_offset    = ENDIAN_SW_8B(arSectHead64[u32Idx].sh_offset);
        arSectHead64[u32Idx].sh_size      = ENDIAN_SW_8B(arSectHead64[u32Idx].sh_size);
        arSectHead64[u32Idx].sh_link      = ENDIAN_SW_4B(arSectHead64[u32Idx].sh_link);
        arSectHead64[u32Idx].sh_info      = ENDIAN_SW_4B(arSectHead64[u32Idx].sh_info);
        arSectHead64[u32Idx].sh_addralign = ENDIAN_SW_8B(arSectHead64[u32Idx].sh_addralign);
        arSectHead64[u32Idx].sh_entsize   = ENDIAN_SW_8B(arSectHead64[u32Idx].sh_entsize);
    }
}

#if TRUE == ELF_CFG_PROGRAM_TABLE_USED
/*================================================================================================*/
static bool_t ELF64_ProgSectFindNext( ELF_File_t *pElfFile, uint32_t *pu32ProgIdx,
                                       uint64_t *pu64LoadVAddr, uint64_t *pu64LoadPAddr, uint64_t *pu64Length
                                     )
{
    bool_t bRetVal = FALSE;

#if defined(PFE_CFG_NULL_ARG_CHECK)
    /* Check prerequisities */
    if (unlikely((NULL == pElfFile) || (NULL == pElfFile->arProgHead64)))
    {
        NXP_LOG_ERROR("ELF64_ProgSectFindNext: Failed - elf not opened!\n");
    }
    else
#endif /* PFE_CFG_NULL_ARG_CHECK */
    {
        /* Find a record having RAM area */
        while (pElfFile->u32ProgScanIdx < pElfFile->Header.r64.e_phnum)
        {
            if (((uint32_t)PT_LOAD == pElfFile->arProgHead64[pElfFile->u32ProgScanIdx].p_type) /* Has RAM area */
                 && (0U != pElfFile->arProgHead64[pElfFile->u32ProgScanIdx].p_memsz)      /* Size != 0 */
                )
            {   /* Match found */
                /* Set returned values */
                if (NULL != pu32ProgIdx)
                {
                    *pu32ProgIdx = pElfFile->u32ProgScanIdx;
                }
                if (NULL != pu64LoadVAddr)
                {
                    *pu64LoadVAddr = pElfFile->arProgHead64[pElfFile->u32ProgScanIdx].p_vaddr;
                }
                if (NULL != pu64LoadPAddr)
                {
                    *pu64LoadPAddr = pElfFile->arProgHead64[pElfFile->u32ProgScanIdx].p_paddr;
                }
                if (NULL != pu64Length)
                {
                    *pu64Length = pElfFile->arProgHead64[pElfFile->u32ProgScanIdx].p_memsz;
                }
                bRetVal = TRUE;
                pElfFile->u32ProgScanIdx++;
                break;
            }
            else
            {
                pElfFile->u32ProgScanIdx++;
            }
        }
    }

    return bRetVal;
}

/*================================================================================================*/
static bool_t ELF64_ProgSectLoad(const ELF_File_t *pElfFile, uint32_t u32ProgIdx,
                                   addr_t AccessAddr, addr_t AllocSize
                                 )
{
    bool_t bSuccess = FALSE;

#if defined(PFE_CFG_NULL_ARG_CHECK)
    /* CHECK */
    if (unlikely((NULL == pElfFile) || (NULL == pElfFile->arProgHead64)))
    {
        NXP_LOG_ERROR("ELF64_ProgSectLoad: Failed - elf not loaded!\n");
    }
    else
#endif /* PFE_CFG_NULL_ARG_CHECK */
    if (u32ProgIdx >= pElfFile->Header.r64.e_phnum)
    {
        NXP_LOG_ERROR("ELF64_ProgSectLoad: Invalid program index: %u\n", (uint_t)u32ProgIdx);
    }
    else if ((uint32_t)PT_LOAD != pElfFile->arProgHead64[u32ProgIdx].p_type)
    {
        NXP_LOG_ERROR("ELF64_ProgSectLoad: This section has no associated RAM area\n");
    }
    else if (AllocSize < pElfFile->arProgHead64[u32ProgIdx].p_memsz)
    {
        NXP_LOG_ERROR("ELF64_ProgSectLoad: Section does not fit to allocated memory\n");
    }
    else if (pElfFile->arProgHead64[u32ProgIdx].p_filesz > pElfFile->arProgHead64[u32ProgIdx].p_memsz)
    {
        NXP_LOG_ERROR("ELF64_ProgSectLoad: Section size mishmash.\n");
    }
    /* LOAD */
    else
    {   /* All OK */
        /* p_filesz bytes of data at the beginning of the memory area shall be copied from file
        the rest up to p_memsz bytes shall be set to 0
        */
        if (0U != pElfFile->arProgHead64[u32ProgIdx].p_filesz)
        {   /* Read from file */
            if (FALSE == LoadFileData(pElfFile, /* pElfFile, */
                (uint32_t)pElfFile->arProgHead64[u32ProgIdx].p_offset, /* u32Offset, */
                (uint32_t)pElfFile->arProgHead64[u32ProgIdx].p_filesz, /* u32Size, */
                (void *)AccessAddr /* pvDestMem */
                                      )
                )
            {
                NXP_LOG_ERROR("ELF64_ProgSectLoad: Failed to load section from file\n");
            }
            else
            {   /* Reading done */
                bSuccess = TRUE;
            }
        }
        else
        {   /* Reading skipped */
            bSuccess = TRUE;
        }

        /* Pad rest with zeros */
        if ((TRUE == bSuccess)
            && (pElfFile->arProgHead64[u32ProgIdx].p_memsz > pElfFile->arProgHead64[u32ProgIdx].p_filesz)
            )
        {
            if (sizeof(addr_t) < sizeof(uint64_t))
            {
                    NXP_LOG_WARNING("ELF64_ProgSectLoad: addr_t size is not sufficient (%u < %u)", (uint_t)sizeof(addr_t), (uint_t)sizeof(uint64_t));
            }

            (void)memset((void *)(AccessAddr + (addr_t)pElfFile->arProgHead64[u32ProgIdx].p_filesz),
                0,
                (uint32_t)pElfFile->arProgHead64[u32ProgIdx].p_memsz - (uint32_t)pElfFile->arProgHead64[u32ProgIdx].p_filesz
            );
        }
    }
    return bSuccess;
}
#endif /* ELF_CFG_PROGRAM_TABLE_USED */

#if TRUE == ELF_CFG_SECTION_TABLE_USED
/*================================================================================================*/
static bool_t ELF64_SectFindName(const ELF_File_t *pElfFile, const char_t *szSectionName,
                                   uint32_t *pu32SectIdx, uint64_t *pu64LoadAddr, uint64_t *pu64Length
                                 )
{
    bool_t bRetVal = FALSE;
    bool_t bFound = FALSE;
    uint32_t SectIdx;

#if defined(PFE_CFG_NULL_ARG_CHECK)
    /* Check prerequisites */
    if (unlikely((NULL == pElfFile) || (NULL == pElfFile->arSectHead64) || (NULL == pElfFile->acSectNames)))
    {
        NXP_LOG_ERROR("ELF64_SectFindName: Failed - elf not opened!\n");
    }
    else
#endif /* PFE_CFG_NULL_ARG_CHECK */
    {
        /* Search section table */
        for (SectIdx = 0U; SectIdx < pElfFile->Header.r64.e_shnum; SectIdx++)
        {
            if (0 == strcmp((char_t *)(pElfFile->acSectNames + pElfFile->arSectHead64[SectIdx].sh_name), szSectionName))
            {   /* Found */
                if (NULL != pu32SectIdx)
                {
                    *pu32SectIdx = SectIdx;
                }
                if (NULL != pu64Length)
                {
                    *pu64Length = pElfFile->arSectHead64[SectIdx].sh_size;
                }
                if (NULL != pu64LoadAddr)
                {
                    *pu64LoadAddr = pElfFile->arSectHead64[SectIdx].sh_addr;
                }
                bFound = TRUE;
                bRetVal = TRUE;
                break;
            }
        }
        if (FALSE == bFound)
        {
            NXP_LOG_ERROR("ELF64_SectFindName: Section %s not found\n", szSectionName);
        }
    }

    return bRetVal;
}

/*================================================================================================*/
static bool_t ELF64_SectLoad(const ELF_File_t *pElfFile, uint32_t u32SectIdx, addr_t AccessAddr, addr_t AllocSize)
{
    bool_t bSuccess = FALSE;

#if defined(PFE_CFG_NULL_ARG_CHECK)
    /* CHECK */
    if (unlikely((NULL == pElfFile) || (NULL == pElfFile->arSectHead64)))
    {
        NXP_LOG_ERROR("ELF64_SectLoad: Failed - elf not loaded!\n");
    }
    else
#endif /* PFE_CFG_NULL_ARG_CHECK */
    if (u32SectIdx >= pElfFile->Header.r64.e_shnum)
    {
        NXP_LOG_ERROR("ELF64_SectLoad: Invalid section index: %u\n", (uint_t)u32SectIdx);
    }
    else if (AllocSize < pElfFile->arSectHead64[u32SectIdx].sh_size)
    {
        NXP_LOG_ERROR("ELF64_SectLoad: Section does not fit to allocated memory\n");
    }
    /* LOAD */
    else
    {   /* All OK */
        if ((uint32_t)SHT_NOBITS == pElfFile->arSectHead64[u32SectIdx].sh_type)
        {   /* Fill with zeros */
            (void)memset((void *)AccessAddr, 0, (uint32_t)pElfFile->arSectHead64[u32SectIdx].sh_size);
            bSuccess = TRUE;
        }
        else
        {   /* Copy from file */
            if (FALSE == LoadFileData(pElfFile, /* pElfFile, */
                                       (uint32_t)pElfFile->arSectHead64[u32SectIdx].sh_offset, /* u32Offset, */
                                       (uint32_t)pElfFile->arSectHead64[u32SectIdx].sh_size, /* u32Size, */
                                       (void *)AccessAddr /* pvDestMem */
                                      )
                )
            {
                NXP_LOG_ERROR("ELF64_SectLoad: Failed to load section from file\n");
            }
            else
            {   /* Reading done */
                bSuccess = TRUE;
            }
        }
    }
    return bSuccess;
}
#endif /* ELF_CFG_SECTION_TABLE_USED */

#if TRUE == ELF_CFG_SECTION_PRINT_ENABLED
/*================================================================================================*/
static void ELF64_PrintSections(const ELF_File_t *pElfFile)
{
#ifdef NXP_LOG_ENABLED /*  Debug message support */
    uint32_t SectIdx;
    uint32_t ProgIdx;

#if defined(PFE_CFG_NULL_ARG_CHECK)
    /* Check prerequisites */
    if (unlikely((NULL == pElfFile)
#if TRUE == ELF_CFG_SECTION_TABLE_USED
        || (NULL == pElfFile->arSectHead64)
        || (NULL == pElfFile->acSectNames)
#endif /* ELF_CFG_SECTION_TABLE_USED */
#if TRUE == ELF_CFG_PROGRAM_TABLE_USED
        || (NULL == pElfFile->arProgHead64)
#endif /* ELF_CFG_PROGRAM_TABLE_USED */
        ))
    {
        NXP_LOG_ERROR("NXP_LOG_INFOSections: Failed - elf not opened!\n");
    }
    else
#endif /* PFE_CFG_NULL_ARG_CHECK */
    {
#if TRUE == ELF_CFG_SECTION_TABLE_USED
        /* Search section table */
        NXP_LOG_INFO("\n");
        NXP_LOG_INFO("File contains %hu sections:\n", pElfFile->Header.r64.e_shnum);
        NXP_LOG_INFO("     SectionName Type     FileOffset         FileSize           LoadAddress        Flags\n");
        for (SectIdx = 0U; SectIdx < pElfFile->Header.r64.e_shnum; SectIdx++)
        {
            uint32_t u32Type = pElfFile->arSectHead64[SectIdx].sh_type;
            uint32_t u32FlagIdx;

            if (u32Type >= 16U)
            {
                u32Type = 16U; /* Undefined */
            }
            NXP_LOG_INFO("%16s ", pElfFile->acSectNames + pElfFile->arSectHead64[SectIdx].sh_name);
            NXP_LOG_INFO("%s 0x%016"PRINT64"x 0x%016"PRINT64"x 0x%016"PRINT64"x ",
                        aacSTypes[u32Type],
                        pElfFile->arSectHead64[SectIdx].sh_offset,
                        pElfFile->arSectHead64[SectIdx].sh_size,
                        pElfFile->arSectHead64[SectIdx].sh_addr
            );
            /* Now print flags on separate line: */
            for (u32FlagIdx = 0U; u32FlagIdx<u32ShT_Flags_Strings_Count; u32FlagIdx++)
            {
                if (0U != (ShT_Flags_Strings[u32FlagIdx].u32Flag & pElfFile->arSectHead64[SectIdx].sh_flags))
                {
                    NXP_LOG_INFO("%s, ", ShT_Flags_Strings[u32FlagIdx].szString);
                }
            }
            NXP_LOG_INFO("\n");
        }
#endif /* ELF_CFG_SECTION_TABLE_USED */
#if TRUE == ELF_CFG_PROGRAM_TABLE_USED
        /* Search program table */
        NXP_LOG_INFO("\n");
        NXP_LOG_INFO("File contains %hu program sections:\n", pElfFile->Header.r64.e_phnum);
        NXP_LOG_INFO("Idx Type      FileOffset         FileSize           "
                   "LoadVirtAddress    LoadPhysAddress    MemorySize         \n"
        );
        for (ProgIdx = 0U; ProgIdx < pElfFile->Header.r64.e_phnum; ProgIdx++)
        {
            /* Try to find the name of the section in section header */
            uint32_t u32Type = pElfFile->arProgHead64[ProgIdx].p_type;

            if (u32Type >= 10U)
            {
                u32Type = 10U; /* Undefined */
            }

            /* Print program header data */
            NXP_LOG_INFO("%u %s 0x%016"PRINT64"x 0x%016"PRINT64"x 0x%016"PRINT64"x 0x%016"PRINT64"x 0x%016"PRINT64"x",
                (uint_t)ProgIdx,
                        aacPTypes[u32Type],
                        pElfFile->arProgHead64[ProgIdx].p_offset,
                        pElfFile->arProgHead64[ProgIdx].p_filesz,
                        pElfFile->arProgHead64[ProgIdx].p_vaddr,
                        pElfFile->arProgHead64[ProgIdx].p_paddr,
                        pElfFile->arProgHead64[ProgIdx].p_memsz
                      );
            NXP_LOG_INFO("\n");
        }
#endif /* ELF_CFG_PROGRAM_TABLE_USED */
        NXP_LOG_INFO("\n");
    }
#else
    /* Do nothing */
    (void)pElfFile;
#endif /* NXP_LOG_ENABLED */

}
#endif /* ELF_CFG_SECTION_PRINT_ENABLED */
#endif /* ELF_CFG_ELF64_SUPPORTED */

/*================================================================================================*/
static uint32_t buf_read(void *src_buf, uint32_t u32FileSize, uint32_t u32Offset, void *dst_buf, uint32_t nbytes)
{
    uint32_t u32i = 0;
    const uint8_t *pu8src = (uint8_t *)((addr_t)src_buf + u32Offset);
    uint8_t *pu8dst = (uint8_t *)dst_buf;

#if defined(PFE_CFG_NULL_ARG_CHECK)
    if (unlikely((NULL == src_buf) || (NULL == dst_buf)))
    {
        NXP_LOG_ERROR("NULL argument received\n");
        return 0;
    }
#endif /* PFE_CFG_NULL_ARG_CHECK */

    for (u32i = 0U; u32i < nbytes; u32i++)
    {
        /* Check for file end here */
        if (u32i >= (u32FileSize - u32Offset))
        {
            /* File end reached */
            break;
        }
        *pu8dst = *pu8src;
        pu8dst++;
        pu8src++;
    }
    return u32i;
}

static bool_t ELF_LoadTables(ELF_File_t *pElfFile, uint32_t *u32NamesSectionOffset, uint32_t *u32NamesSectionSize)
{
    bool_t bRetVal = FALSE;
    bool_t    bIsCrosEndian;
    ELF_Endian_t NativeEndian = GetLocalEndian();
    ELF_Endian_t BinaryEndian;

    /* Check Endianness */
    BinaryEndian = ELF_IsLittleEndian(pElfFile) ? ELF_Endian_Little : ELF_Endian_Big;
    bIsCrosEndian = (BinaryEndian == NativeEndian) ? FALSE : TRUE;
    NXP_LOG_INFO("ELF_Open: File format: %s\n", pElfFile->bIs64Bit ? "Elf64" : "Elf32");
    NXP_LOG_INFO("ELF_Open: File endian: %s (%s)\n",
              bIsCrosEndian ? "Alien" : "Native",
              (BinaryEndian==ELF_Endian_Little) ? "Little" : "Big"
            );

    if (TRUE == pElfFile->bIs64Bit)
    {   /* Loading 64-bit ELF */
#if TRUE == ELF_CFG_ELF64_SUPPORTED
        bRetVal = ELF64_Load(bIsCrosEndian, pElfFile, u32NamesSectionOffset, u32NamesSectionSize);
#else /* ELF_CFG_ELF64_SUPPORTED */
        NXP_LOG_ERROR("Support for Elf64 was not compiled\n");
#endif /* ELF_CFG_ELF64_SUPPORTED */
    }
    else
    {   /* Loading 32-bit ELF */
#if TRUE == ELF_CFG_ELF32_SUPPORTED
        bRetVal = ELF32_Load(bIsCrosEndian, pElfFile, u32NamesSectionOffset, u32NamesSectionSize);
#else /* ELF_CFG_ELF32_SUPPORTED */
        NXP_LOG_ERROR("Support for Elf32 was not compiled\n");
#endif /* ELF_CFG_ELF32_SUPPORTED */
    }

    return bRetVal;
}
/*================================================================================================*/
static void ELF_FreePtr(ELF_File_t *pElfFile)
{
    if (NULL != pElfFile->arProgHead64)
    {
        pElfFile->arProgHead64 = NULL;
    }
    if (NULL != pElfFile->arSectHead64)
    {
        pElfFile->arSectHead64 = NULL;
    }
    if (NULL != pElfFile->arProgHead32)
    {
        pElfFile->arProgHead32 = NULL;
    }
    if (NULL != pElfFile->arSectHead32)
    {
        pElfFile->arSectHead32 = NULL;
    }
    if (NULL != pElfFile->acSectNames)
    {
        pElfFile->acSectNames = NULL;
    }
    if (NULL != pElfFile->pvData)
    {
        pElfFile->pvData = NULL;
    }
}
/*==================================================================================================
                                       GLOBAL FUNCTIONS
==================================================================================================*/
/*================================================================================================*/
/**
* @brief        Checks whether file is ELF, and initializes the pElfFile structure.
* @details      It also handles file format and loads all tables handling their endianness.
* @param[out]   pElfFile Structure holding all informations about opened ELF file.
* @param[in]    pvFile Pointer to the file content.
* @param[in]    u32FileSize Size of the file data passed in pvFile.
* @retval       TRUE Succeeded
* @retval       FALSE Failed
*/
bool_t ELF_Open(ELF_File_t *pElfFile, void *pvFile, uint32_t u32FileSize)
{
    bool_t    bRetVal = FALSE;
    uint32_t     u32NamesSectionOffset = 0U;
    uint32_t     u32NamesSectionSize = 0U;

#if defined(PFE_CFG_NULL_ARG_CHECK)
    if (unlikely((NULL == pElfFile) || (NULL == pvFile)))
    {
        NXP_LOG_ERROR("NULL argument received\n");
        return FALSE;
    }
#endif /* PFE_CFG_NULL_ARG_CHECK */

    /* Init File info */
    pElfFile->arProgHead64 = NULL;
    pElfFile->arSectHead64 = NULL;
    pElfFile->arProgHead32 = NULL;
    pElfFile->arSectHead32 = NULL;
    pElfFile->acSectNames = NULL;
    pElfFile->pvData = NULL;

    if (ELF64_HEADER_SIZE != buf_read(pvFile, u32FileSize, 0,(void *)&(pElfFile->Header.r64), ELF64_HEADER_SIZE))
    {
        NXP_LOG_ERROR("ELF_Open: Failed to read ELF header\n");
    }
    /* Check file type */
    else if ((0x7FU != pElfFile->Header.e_ident[EI_MAG0]) ||
             ((uint8_t)'E' != pElfFile->Header.e_ident[EI_MAG1]) ||
             ((uint8_t)'L' != pElfFile->Header.e_ident[EI_MAG2]) ||
             ((uint8_t)'F' != pElfFile->Header.e_ident[EI_MAG3]) ||
             (1U           != pElfFile->Header.e_ident[EI_VERSION])
           )
    {
        NXP_LOG_ERROR("ELF_Open: This is not ELF version 1\n");
    }
    else /* So far SUCCESS */
    {
        pElfFile->pvData = pvFile;
        pElfFile->u32FileSize = u32FileSize;
        pElfFile->bIs64Bit = ELF_Is64bit(pElfFile);
        pElfFile->u32ProgScanIdx = 0U;
        /* Load tables */
        bRetVal = ELF_LoadTables(pElfFile, &u32NamesSectionOffset, &u32NamesSectionSize);
    }

    #if TRUE == ELF_CFG_SECTION_TABLE_USED
    /* === Load section names from file ============================================= */
    if (TRUE == bRetVal)
    {
        bRetVal = FALSE;

        /* Check file size */
        if ((u32NamesSectionOffset + u32NamesSectionSize) >= u32FileSize)
        {
            NXP_LOG_ERROR("ELF_Open: Section names section offset out of file\n");
        }
        /* Save the section name pointer */
        else
        {
            pElfFile->acSectNames = (((int8_t *)pElfFile->pvData) + u32NamesSectionOffset);
            bRetVal = TRUE;
        }
    }
    #endif /* ELF_CFG_SECTION_TABLE_USED */

    /* === Check overall status and possibly clean-up ================================= */
    if(FALSE == bRetVal)
    {   /* In case of failure free the memory now */
        ELF_FreePtr(pElfFile);
    }

    return bRetVal;
}

/*================================================================================================*/
/**
* @brief        Closes previously opened ELF file and frees previously allocated memory for headers.
* @param[in,out] pElfFile Structure holding all informations about opened ELF file.
*/
void ELF_Close(ELF_File_t *pElfFile)
{
    bool_t    bIsCrosEndian;
    ELF_Endian_t NativeEndian = GetLocalEndian();
    ELF_Endian_t BinaryEndian;

#if defined(PFE_CFG_NULL_ARG_CHECK)
    if (unlikely(NULL == pElfFile))
    {
        NXP_LOG_ERROR("NULL argument received\n");
        return;
    }
#endif /* PFE_CFG_NULL_ARG_CHECK */

    /* Check Endianness */
    BinaryEndian = ELF_IsLittleEndian(pElfFile) ? ELF_Endian_Little : ELF_Endian_Big;
    bIsCrosEndian = (BinaryEndian == NativeEndian) ? FALSE : TRUE;
        
    /* If cross endian, swap the header bytes again to revert the elf file to original,
       so it can be used again in the next call of Eth_43_PFE_Init()  */
    if (bIsCrosEndian)
    {
        if (TRUE == pElfFile->bIs64Bit)
        {   
            /* Loading 64-bit ELF */
            #if TRUE == ELF_CFG_ELF64_SUPPORTED
                /* Handle endianness */
                ELF64_ProgTabSwitchEndianness(pElfFile->arProgHead64, pElfFile->Header.r64.e_phnum);
                ELF64_SectTabSwitchEndianness(pElfFile->arSectHead64, pElfFile->Header.r64.e_shnum);
            #endif  /* ELF_CFG_ELF64_SUPPORTED */
        }
        else
        {
            #if TRUE == ELF_CFG_ELF32_SUPPORTED
                /* Handle endianness */
                ELF32_ProgTabSwitchEndianness(pElfFile->arProgHead32, pElfFile->Header.r32.e_phnum);
                ELF32_SectTabSwitchEndianness(pElfFile->arSectHead32, pElfFile->Header.r32.e_shnum);
            #endif  /* ELF_CFG_ELF32_SUPPORTED */
        }
    }
    
    ELF_FreePtr(pElfFile);
}

#if TRUE == ELF_CFG_PROGRAM_TABLE_USED
/*================================================================================================*/
/**
* @brief        Finds next section in program table which shall be loaded into RAM.
* @details      Function provides section index and informations needed for memory allocation.
*               Once the memory is allocated, the index shall be passed to function ELF_ProgSectLoad.
* @param[in]    pElfFile Structure holding all informations about opened ELF file.
* @param[out]   pu32ProgIdx Index which shall be passed to function ELF_ProgSectLoad.
* @param[out]   pu64LoadVAddr Returns the (virtual) address the data shall be loaded at.
* @param[out]   pu64LoadPAddr Returns the physical address the data shall be loaded at. This is
*               used when the physical address is important, usually just virtual address is used.
* @param[out]   pu64Length Length of the section in memory.
* @retval       TRUE Succeeded
* @retval       FALSE Failed
*/
/*  */
bool_t ELF_ProgSectFindNext(ELF_File_t *pElfFile, uint32_t *pu32ProgIdx,
                              uint64_t *pu64LoadVAddr, uint64_t *pu64LoadPAddr, uint64_t *pu64Length
                            )
{
    bool_t bRetVal = FALSE;

#if defined(PFE_CFG_NULL_ARG_CHECK)
    if (unlikely((NULL == pElfFile) || (NULL == pu32ProgIdx) || (NULL == pu64LoadVAddr) || (NULL == pu64LoadPAddr) || (NULL == pu64Length)))
    {
        NXP_LOG_ERROR("NULL argument received\n");
        return FALSE;
    }
#endif /* PFE_CFG_NULL_ARG_CHECK */

    if (TRUE == pElfFile->bIs64Bit)
    {
        #if TRUE == ELF_CFG_ELF64_SUPPORTED
        bRetVal = ELF64_ProgSectFindNext(pElfFile, pu32ProgIdx, pu64LoadVAddr, pu64LoadPAddr, pu64Length);
        #endif /* ELF_CFG_ELF64_SUPPORTED */
    }
    else
    {
        #if TRUE == ELF_CFG_ELF32_SUPPORTED
        bRetVal = ELF32_ProgSectFindNext(pElfFile, pu32ProgIdx, pu64LoadVAddr, pu64LoadPAddr, pu64Length);
        #endif /* ELF_CFG_ELF32_SUPPORTED */
    }
    return bRetVal;
}

/*================================================================================================*/
/**
* @brief        Loads a program section from file to given memory buffer.
* @param[in]    pElfFile Structure holding all informations about opened ELF file.
* @param[in]    u32ProgIdx Section index obtained from function ELF_ProgSectFindNext.
* @param[in]    AccessAddr Address of allocated memory the data will be written to.
* @param[in]    AllocSize Size of the allocated memory.
* @retval       TRUE Succeeded
* @retval       FALSE Failed
*/
bool_t ELF_ProgSectLoad(const ELF_File_t *pElfFile, uint32_t u32ProgIdx, addr_t AccessAddr, addr_t AllocSize)
{
    bool_t bRetVal = FALSE;

#if defined(PFE_CFG_NULL_ARG_CHECK)
    if (unlikely(NULL == pElfFile))
    {
        NXP_LOG_ERROR("NULL argument received\n");
        return FALSE;
    }
#endif /* PFE_CFG_NULL_ARG_CHECK */

    if (0U != (ELF_NAMED_SECT_IDX_FLAG & u32ProgIdx))
    {
        NXP_LOG_ERROR("ELF_ProgSectLoad: Expecting index from function ELF_ProgSectFindNext\n");
        bRetVal = FALSE;
    }
    else if (TRUE == pElfFile->bIs64Bit)
    {
        #if TRUE == ELF_CFG_ELF64_SUPPORTED
        bRetVal = ELF64_ProgSectLoad(pElfFile, u32ProgIdx, AccessAddr, AllocSize);
        #endif /* ELF_CFG_ELF64_SUPPORTED */
    }
    else
    {
        #if TRUE == ELF_CFG_ELF32_SUPPORTED
        bRetVal = ELF32_ProgSectLoad(pElfFile, u32ProgIdx, AccessAddr, AllocSize);
        #endif /* ELF_CFG_ELF32_SUPPORTED */
    }
    return bRetVal;
}
#endif /* ELF_CFG_PROGRAM_TABLE_USED */

#if TRUE == ELF_CFG_SECTION_TABLE_USED
/*================================================================================================*/
/**
* @brief        Finds section with matching name in section table.
* @warning      Use of functions ELF_SectFindName and ELF_SectLoad provides alternative way of
*               loading binary. Usually it is better to use functions ELF_ProgSectFindNext
*               and ELF_ProgSectLoad instead.
* @param[in]    pElfFile Structure holding all informations about opened ELF file.
* @param[in]    szSectionName Zero terminated string with exact section name. For example ".bss".
* @param[out]   pu32SectIdx Index which shall be passed to function ELF_SectLoad.
* @param[out]   pu64LoadAddr The address the section data shall be loaded at.
* @param[out]   pu64Length Length of the section in memory.
* @retval       TRUE Succeeded
* @retval       FALSE Failed
*/
bool_t ELF_SectFindName(const ELF_File_t *pElfFile, const char_t *szSectionName,
                          uint32_t *pu32SectIdx, uint64_t *pu64LoadAddr, uint64_t *pu64Length
                        )
{
    bool_t bRetVal = FALSE;

#if defined(PFE_CFG_NULL_ARG_CHECK)
    if (unlikely((NULL == pElfFile) || (NULL == szSectionName) || (NULL == pu32SectIdx)))
    {
        NXP_LOG_ERROR("NULL argument received\n");
        return FALSE;
    }
#endif /* PFE_CFG_NULL_ARG_CHECK */

    if (TRUE == pElfFile->bIs64Bit)
    {
        #if TRUE == ELF_CFG_ELF64_SUPPORTED
        bRetVal = ELF64_SectFindName(pElfFile, szSectionName, pu32SectIdx, pu64LoadAddr, pu64Length);
        #endif /* ELF_CFG_ELF64_SUPPORTED */
    }
    else
    {
        #if TRUE == ELF_CFG_ELF32_SUPPORTED
        bRetVal = ELF32_SectFindName(pElfFile, szSectionName, pu32SectIdx, pu64LoadAddr, pu64Length);
        #endif /* ELF_CFG_ELF32_SUPPORTED */
    }

    /* Set the highest bit in the index to make sure that this index is not used in wrong load function. */
    *pu32SectIdx |= ELF_NAMED_SECT_IDX_FLAG; /* Safe since the ELF index is 16-bit only. */

    return bRetVal;
}

/*================================================================================================*/
/**
* @brief        Loads a named section from file to given memory buffer.
* @warning      Only sections with ALLOC flag shall be loaded for execution.
* @param[in]    pElfFile Structure holding all informations about opened ELF file.
* @param[in]    u32SectIdx Section index obtained from function ELF_SectFindName.
* @param[in]    AccessAddr Address of allocated memory the data will be written to.
* @param[in]    AllocSize Size of the allocated memory.
* @retval       TRUE Succeeded
* @retval       FALSE Failed
*/
bool_t ELF_SectLoad(const ELF_File_t *pElfFile, uint32_t u32SectIdx, addr_t AccessAddr, addr_t AllocSize)
{
    bool_t bRetVal = FALSE;

#if defined(PFE_CFG_NULL_ARG_CHECK)
    if (unlikely(NULL == pElfFile))
    {
        NXP_LOG_ERROR("NULL argument received\n");
        return FALSE;
    }
#endif /* PFE_CFG_NULL_ARG_CHECK */

    if (0U == (ELF_NAMED_SECT_IDX_FLAG & u32SectIdx))
    {
        NXP_LOG_ERROR("ELF_SectLoad: Expecting index from function ELF_SectFindName\n");
        bRetVal = FALSE;
    }
    else if (TRUE == pElfFile->bIs64Bit)
    {
        #if TRUE == ELF_CFG_ELF64_SUPPORTED
        bRetVal = ELF64_SectLoad(pElfFile, (~(uint32_t)ELF_NAMED_SECT_IDX_FLAG) & u32SectIdx, AccessAddr, AllocSize);
        #endif /* ELF_CFG_ELF64_SUPPORTED */
    }
    else
    {
        #if TRUE == ELF_CFG_ELF32_SUPPORTED
        bRetVal = ELF32_SectLoad(pElfFile, (~(uint32_t)ELF_NAMED_SECT_IDX_FLAG) & u32SectIdx, AccessAddr, AllocSize);
        #endif /* ELF_CFG_ELF32_SUPPORTED */
    }
    return bRetVal;
}
#endif /* ELF_CFG_SECTION_TABLE_USED */

#if TRUE == ELF_CFG_SECTION_PRINT_ENABLED
/*================================================================================================*/
/**
* @brief        Writes sections and program sections to console.
* @details      This function is intended mainly for debugging purposes. It is not needed for
*               loading. Disable this function in configuration if it is not needed.
* @param[in]    pElfFile Structure holding all informations about opened ELF file.
*/
void ELF_PrintSections(const ELF_File_t *pElfFile)
{
#if defined(PFE_CFG_NULL_ARG_CHECK)
    if (unlikely(NULL == pElfFile))
    {
        NXP_LOG_ERROR("NULL argument received\n");
        return;
    }
#endif /* PFE_CFG_NULL_ARG_CHECK */

    if (TRUE == pElfFile->bIs64Bit)
    {
        #if TRUE == ELF_CFG_ELF64_SUPPORTED
        ELF64_PrintSections(pElfFile);
        #endif /* ELF_CFG_ELF64_SUPPORTED */
    }
    else
    {
        #if TRUE == ELF_CFG_ELF32_SUPPORTED
        ELF32_PrintSections(pElfFile);
        #endif /* ELF_CFG_ELF32_SUPPORTED */
    }
}
#endif /* ELF_CFG_SECTION_PRINT_ENABLED */

/** @}*/
