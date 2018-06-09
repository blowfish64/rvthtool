/***************************************************************************
 * RVT-H Tool (librvth)                                                    *
 * rvth_p.c: RVT-H image handler. (PRIVATE FUNCTIONS)                      *
 *                                                                         *
 * Copyright (c) 2018 by David Korth.                                      *
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

#include "rvth_p.h"
#include "rvth_time.h"

#include "byteswap.h"
#include "nhcd_structs.h"

#include <assert.h>
#include <errno.h>
#include <string.h>

/**
 * Make an RVT-H object writable.
 * @param rvth	[in] RVT-H disk image.
 * @return Error code. (If negative, POSIX error; otherwise, see RvtH_Errors.)
 */
int rvth_make_writable(RvtH *rvth)
{
	if (ref_is_writable(rvth->f_img)) {
		// RVT-H is already writable.
		return 0;
	}

	// TODO: Allow making a disc image file writable.
	// (Single bank)

	// Make sure this is a device file.
	if (!ref_is_device(rvth->f_img)) {
		// This is not a device file.
		// Cannot make it writable.
		return RVTH_ERROR_NOT_A_DEVICE;
	}

	// Make this writable.
	return ref_make_writable(rvth->f_img);
}

/**
 * Check if a block is empty.
 * @param block Block.
 * @param size Block size. (Must be a multiple of 64 bytes.)
 * @return True if the block is all zeroes; false if not.
 */
bool rvth_is_block_empty(const uint8_t *block, unsigned int size)
{
	// Process the block using 64-bit pointers.
	const uint64_t *block64 = (const uint64_t*)block;
	unsigned int i;
	assert(size % 64 == 0);
	for (i = size/8/8; i > 0; i--, block64 += 8) {
		uint64_t x = block64[0];
		x |= block64[1];
		x |= block64[2];
		x |= block64[3];
		x |= block64[4];
		x |= block64[5];
		x |= block64[6];
		x |= block64[7];
		if (x != 0) {
			// Non-zero block.
			return false;
		}
	}

	// Block is all zeroes.
	return true;
}

/**
 * Write a bank table entry to disk.
 * @param rvth	[in] RvtH device.
 * @param bank	[in] Bank number. (0-7)
 * @return Error code. (If negative, POSIX error; otherwise, see RvtH_Errors.)
 */
int rvth_write_BankEntry(RvtH *rvth, unsigned int bank)
{
	int ret;
	size_t size;
	NHCD_BankEntry nhcd_entry;
	RvtH_BankEntry *rvth_entry;

	if (!rvth) {
		errno = EINVAL;
		return -EINVAL;
	} else if (!rvth_is_hdd(rvth)) {
		// Standalone disc image. No bank table.
		errno = EINVAL;
		return RVTH_ERROR_NOT_HDD_IMAGE;
	} else if (bank >= rvth->bank_count) {
		// Bank number is out of range.
		errno = ERANGE;
		return -ERANGE;
	}

	// Make the RVT-H object writable.
	ret = rvth_make_writable(rvth);
	if (ret != 0) {
		// Could not make the RVT-H object writable.
		return ret;
	}

	// Create a new NHCD bank entry.
	memset(&nhcd_entry, 0, sizeof(nhcd_entry));

	// If the bank entry is deleted, then it should be
	// all zeroes, so skip all of this.
	rvth_entry = &rvth->entries[bank];
	if (rvth_entry->is_deleted)
		goto skip_creating_bank_entry;

	// Check the bank type.
	rvth_entry = &rvth->entries[bank];
	switch (rvth_entry->type) {
		// These bank entries can be written.
		case RVTH_BankType_Empty:
			nhcd_entry.type = cpu_to_be32(NHCD_BankType_Empty);
			break;
		case RVTH_BankType_GCN:
			nhcd_entry.type = cpu_to_be32(NHCD_BankType_GCN);
			break;
		case RVTH_BankType_Wii_SL:
			nhcd_entry.type = cpu_to_be32(NHCD_BankType_Wii_SL);
			break;
		case RVTH_BankType_Wii_DL:
			nhcd_entry.type = cpu_to_be32(NHCD_BankType_Wii_DL);
			break;

		case RVTH_BankType_Unknown:
		default:
			// Unknown bank status...
			return RVTH_ERROR_BANK_UNKNOWN;

		case RVTH_BankType_Wii_DL_Bank2:
			// Second bank of a dual-layer Wii disc image.
			// TODO: Automatically select the first bank?
			return RVTH_ERROR_BANK_DL_2;
	}

	if (rvth_entry->type == RVTH_BankType_Empty ||
	    rvth_entry->is_deleted)
	{
		// Bank is either empty or deleted.
		// Don't bother doing anything with it.
		goto skip_creating_bank_entry;
	}

	// ASCII zero bytes.
	memset(nhcd_entry.all_zero, '0', sizeof(nhcd_entry.all_zero));

	// Timestamp.
	rvth_timestamp_create(nhcd_entry.timestamp, sizeof(nhcd_entry.timestamp), time(NULL));

	// LBA start and length.
	nhcd_entry.lba_start = cpu_to_be32(rvth_entry->lba_start);
	nhcd_entry.lba_len = cpu_to_be32(rvth_entry->lba_len);

skip_creating_bank_entry:
	// Write the bank entry.
	ret = ref_seeko(rvth->f_img, LBA_TO_BYTES(NHCD_BANKTABLE_ADDRESS_LBA + bank+1), SEEK_SET);
	if (ret != 0) {
		// Seek error.
		if (errno == 0) {
			errno = EIO;
		}
		return -errno;
	}
	size = ref_write(&nhcd_entry, 1, sizeof(nhcd_entry), rvth->f_img);
	if (size != sizeof(nhcd_entry)) {
		// Write error.
		if (errno == 0) {
			errno = EIO;
		}
		return -errno;
	}

	// Bank entry written successfully.
	return 0;
}
