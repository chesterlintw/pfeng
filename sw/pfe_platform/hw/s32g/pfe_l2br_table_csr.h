/* =========================================================================
 *  Copyright 2018-2019 NXP
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER
 * OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * ========================================================================= */

/**
 * @addtogroup  dxgr_PFE_GPI
 * @{
 *
 * @file		pfe_l2br_table_csr.h
 * @brief		The L2 Bridge Table module registers definition file (s32g).
 * @details
 *
 */

#ifndef HW_S32G_PFE_L2BR_TABLE_CSR_H_
#define HW_S32G_PFE_L2BR_TABLE_CSR_H_

/*
	2-field MAC TABLE
	Depth: 512 entries, Width: 124 bits
*/

#define _MAC2F_TABLE_HASH_ENTRIES		256U /* Must be power-of-2 */
#define _MAC2F_TABLE_COLL_ENTRIES		256U
#define _MAC2F_TABLE_HASH_SPACE_START	0U
#define _MAC2F_TABLE_COLL_SPACE_START	_MAC2F_TABLE_HASH_ENTRIES

#define HOST_MAC2F_CMD_REG				(0x420U)
#define HOST_MAC2F_MAC1_ADDR_REG		(0x424U)
#define HOST_MAC2F_MAC2_ADDR_REG		(0x428U)
#define HOST_MAC2F_MAC3_ADDR_REG		(0x42cU)
#define HOST_MAC2F_MAC4_ADDR_REG		(0x430U)
#define HOST_MAC2F_MAC5_ADDR_REG		(0x434U)
#define HOST_MAC2F_ENTRY_REG			(0x438U)
#define HOST_MAC2F_STATUS_REG			(0x43cU)
#define HOST_MAC2F_DIRECT_REG			(0x440U)

#define HOST_MAC2F_FREE_LIST_ENTRIES	(0x468U)
#define HOST_MAC2F_FREE_LIST_HEAD_PTR	(0x46cU)
#define HOST_MAC2F_FREE_LIST_TAIL_PTR	(0x470U)

/*
	VLAN TABLE
	Depth: 128 entries, Width: 100 bits
*/
#define _VLAN_TABLE_HASH_ENTRIES		64U /* Must be power-of-2 */
#define _VLAN_TABLE_COLL_ENTRIES		64U
#define _VLAN_TABLE_HASH_SPACE_START	0U
#define _VLAN_TABLE_COLL_SPACE_START	_VLAN_TABLE_HASH_ENTRIES

#define HOST_VLAN_CMD_REG				(0x474U)
#define HOST_VLAN_MAC1_ADDR_REG			(0x478U)
#define HOST_VLAN_MAC2_ADDR_REG			(0x47cU)
#define HOST_VLAN_MAC3_ADDR_REG			(0x480U)
#define HOST_VLAN_MAC4_ADDR_REG			(0x484U)
#define HOST_VLAN_MAC5_ADDR_REG			(0x488U)
#define HOST_VLAN_ENTRY_REG				(0x48cU)
#define HOST_VLAN_STATUS_REG			(0x490U)
#define HOST_VLAN_DIRECT_REG			(0x494U)

#define HOST_VLAN_FREE_LIST_ENTRIES		(0x4bcU)
#define HOST_VLAN_FREE_LIST_HEAD_PTR	(0x4c0U)
#define HOST_VLAN_FREE_LIST_TAIL_PTR	(0x4c4U)

/*
	COMMON
 */

/*	STATUS_REG bit-fields */
#define STATUS_REG_CMD_DONE				(1U << 0)
#define STATUS_REG_SIG_ENTRY_NOT_FOUND	(1U << 1)
#define STATUS_REG_SIG_INIT_DONE		(1U << 2)
#define STATUS_REG_SIG_ENTRY_ADDED		(1U << 3)
#define STATUS_REG_MATCH				(1U << 4)

/**
 * @brief	Available commands (see CMD_REG)
 */
typedef enum
{
	L2BR_CMD_INIT = 0x1,     /**< L2BR_CMD_INIT */
	L2BR_CMD_ADD = 0x2,      /**< L2BR_CMD_ADD */
	L2BR_CMD_DELETE = 0x3,   /**< L2BR_CMD_DELETE */
	L2BR_CMD_UPDATE = 0x4,   /**< L2BR_CMD_UPDATE */
	L2BR_CMD_SEARCH = 0x5,   /**< L2BR_CMD_SEARCH */
	L2BR_CMD_MEM_READ = 0x6, /**< L2BR_CMD_MEM_READ */
	L2BR_CMD_MEM_WRITE = 0x7,/**< L2BR_CMD_MEM_WRITE */
	L2BR_CMD_FLUSH = 0x8     /**< L2BR_CMD_FLUSH */
} pfe_l2br_table_cmd_t;

#endif /* HW_S32G_PFE_L2BR_TABLE_CSR_H_ */

/** @}*/
