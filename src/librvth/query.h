/***************************************************************************
 * RVT-H Tool (librvth)                                                    *
 * query.h: Query storage devices.                                         *
 *                                                                         *
 * Copyright (c) 2018-2020 by David Korth.                                 *
 * SPDX-License-Identifier: GPL-2.0-or-later                               *
 ***************************************************************************/

#ifndef __RVTHTOOL_LIBRVTH_QUERY_H__
#define __RVTHTOOL_LIBRVTH_QUERY_H__

#include <stdint.h>
#include "tcharx.h"

#ifdef __cplusplus
extern "C" {
#endif

// RVT-H Reader USB VID/PID.
#define RVTH_READER_VID 0x057e
#define RVTH_READER_PID 0x0304

// NOTE: STORAGE_DEVICE_DESCRIPTOR has a serial number value
// for the HDD itself, but the RVT-H Reader USB bridge
// doesn't support this query. The code is still present,
// but disabled because it isn't useful.
//#define RVTH_QUERY_ENABLE_HDD_SERIAL 1

/**
 * Scanned device entry.
 * This is a singly-linked list.
 */
typedef struct _RvtH_QueryEntry {
	struct _RvtH_QueryEntry *next;

	const TCHAR *device_name;	// Device name, e.g. "/dev/sdc" or "\\.\PhysicalDrive3".

	const TCHAR *usb_vendor;	// USB vendor name.
	const TCHAR *usb_product;	// USB product name.
	const TCHAR *usb_serial;	// USB serial number, in ASCII.

	const TCHAR *hdd_vendor;	// HDD vendor.
	const TCHAR *hdd_model;		// HDD model number.
	const TCHAR *hdd_fwver;		// HDD firmware version.

#ifdef RVTH_QUERY_ENABLE_HDD_SERIAL
	const TCHAR *hdd_serial;	// HDD serial number, in ASCII.
#endif /* RVTH_QUERY_ENABLE_HDD_SERIAL */

	uint64_t size;			// HDD size, in bytes.
} RvtH_QueryEntry;

/**
 * Create a full serial number string.
 * This includes the check digit.
 *
 * TODO: Figure out if there's a way to determine HMA (wireless)
 * vs. HUA (wired). Both wireless and wired systems have been
 * seen with serial numbers 20xxxxxx.
 *
 * @param serial Serial number as provided by the RVT-H Reader.
 * @return Allocated serial number string.
 */
TCHAR *rvth_create_full_serial_number(unsigned int serial);

/**
 * Scan all USB devices for RVT-H Readers.
 * @param pErr	[out,opt] Pointer to store positive POSIX error code in on error. (0 on success)
 * @return List of matching devices, or NULL if none were found.
 */
RvtH_QueryEntry *rvth_query_devices(int *pErr);

/**
 * Get the serial number for the specified RVT-H Reader device.
 * @param filename	[in] RVT-H Reader device filename.
 * @param pErr		[out,opt] Pointer to store positive POSIX error code in on error. (0 on success)
 * @return Allocated serial number string, or nullptr on error.
 */
TCHAR *rvth_get_device_serial_number(const TCHAR *filename, int *pErr);

/**
 * Free a list of queried devices.
 */
void rvth_query_free(RvtH_QueryEntry *devs);

#ifdef __cplusplus
}
#endif

#endif /* __RVTHTOOL_LIBRVTH_QUERY_H__ */
