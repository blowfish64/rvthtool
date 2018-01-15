/***************************************************************************
 * RVT-H Tool                                                              *
 * rvth.h: RVT-H data structures.                                          *
 *                                                                         *
 * Copyright (c) 2008-2018 by David Korth.                                 *
 *                                                                         *
 * This program is free software; you can redistribute it and/or modify it *
 * under the terms of the GNU General Public License as published by the   *
 * Free Software Foundation; either version 2 of the License, or (at your  *
 * option) any later version.                                              *
 *                                                                         *
 * This program is distributed in the hope that it will be useful, but     *
 * WITHOUT ANY WARRANTY; without even the implied warranty of              *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           *
 * GNU General Public License for more details.                            *
 *                                                                         *
 * You should have received a copy of the GNU General Public License       *
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.   *
 ***************************************************************************/

// FIXME: ASSERT_STRUCT() doesn't work in C99.

#ifndef __RVTHTOOL_RVTHTOOL_RVTH_H__
#define __RVTHTOOL_RVTHTOOL_RVTH_H__

#include <stdint.h>
#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

#pragma pack(1)

/**
 * RVT-H bank table header.
 * All fields are in big-endian.
 */
#define RVTH_BANKTABLE_MAGIC 0x4E484344	/* "NHCD" */
typedef struct PACKED _RVTH_BankTable_Header {
	uint32_t magic;		// [0x000] "NHCD"
	uint32_t x004;		// [0x004] 0x00000001
	uint32_t x008;		// [0x008] 0x00000008
	uint32_t x00C;		// [0x00C] 0x00000000
	uint32_t x010;		// [0x010] 0x002FF000
	uint8_t unk[492];	// [0x014] Unknown
} RVTH_BankTable_Header;
//ASSERT_STRUCT(RVTH_BankTable_Header, 512);

/**
 * RVT-H bank entry.
 * All fields are in big-endian.
 */
typedef struct PACKED _RVTH_BankEntry {
	uint32_t type;		// [0x000] Type. See RVTH_BankType_e.
	char all_zero[14];	// [0x004] All ASCII zeroes. ('0')
	char mdate[8];		// [0x012] Date stamp, in ASCII. ('20180112')
	char mtime[6];		// [0x01A] Time stamp, in ASCII. ('222720')
	uint32_t lba_start;	// [0x020] Starting LBA. (512-byte sectors)
	uint32_t lba_len;	// [0x024] Length, in 512-byte sectors.
	uint8_t unk[472];	// [0x028] Unknown
} RVTH_BankEntry;
//ASSERT_STRUCT(RVTH_BankEntry, 512);

/**
 * RVT-H bank types.
 */
typedef enum {
	RVTH_BankType_GCN = 0x4743314C,		// "GC1L"
	RVTH_BankType_Wii_SL = 0x4E4E314C,	// "NN1L"
	RVTH_BankType_Wii_DL = 0x4E4E324C,	// "NN2L"
} RVTH_BankType_e;

/**
 * RVT-H bank table.
 */
typedef struct _RVTH_BankTable {
	RVTH_BankTable_Header header;
	RVTH_BankEntry entries[8];
} RVTH_BankTable;
//ASSERT_STRUCT(RVTH_BankTable, 512*9);

// Bank table address.
#define RVTH_BANKTABLE_ADDRESS	0x60000000ULL
// Bank 1 starting address.
#define RVTH_BANK_1_START	0x60001200ULL
// Maximum bank size.
#define RVTH_BANK_SIZE		0x118940000ULL

// Block size.
#define RVTH_BLOCK_SIZE		512

#pragma pack()

#ifdef __cplusplus
}
#endif

#endif /* __RVTHTOOL_RVTHTOOL_RVTH_H__ */
