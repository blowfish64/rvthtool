/***************************************************************************
 * RVT-H Tool (librvth)                                                    *
 * query_win32.c: Query storage devices. (Win32 version)                   *
 *                                                                         *
 * Copyright (c) 2018-2019 by David Korth.                                 *
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

#include "query.h"

// C includes.
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// Windows includes.
#include <windows.h>
#include <setupapi.h>
#include <winioctl.h>
#include <devguid.h>
#include <cfgmgr32.h>

// _tcsdup() wrapper that returns NULL if the input is NULL.
static inline TCHAR *_tcsdup_null(const TCHAR *s)
{
	return (s ? _tcsdup(s) : NULL);
}

/**
 * Convert ANSI to TCHAR and _tcsdup() the string.
 * @param str ANSI string
 * @param len Length of string, or -1 for NULL-terminated
 * @return _tcsdup()'d TCHAR string
 */
static inline TCHAR *A2T_dup(const char *str, int len)
{
	// TODO: Remove trailing spaces?

#ifdef _UNICODE
	wchar_t wcsbuf[256];
	int ret = MultiByteToWideChar(CP_ACP, 0, str, len, wcsbuf, _countof(wcsbuf));
	if (ret < _countof(wcsbuf)) {
		wcsbuf[ret] = _T('\0');
	} else {
		wcsbuf[_countof(wcsbuf)-1] = _T('\0');
	}
	return _tcsdup(wcsbuf);
#else /* !_UNICODE */
	// ANSI build. Just _tcsdup() it.
	return (len >= 0 ? _tcsndup(str, len) : _tcsdup(str));
#endif /* _UNICODE */
}

/**
 * Scan all USB devices for RVT-H Readers.
 * @param pErr	[out,opt] Pointer to store positive POSIX error code in on error. (0 on success)
 * @return List of matching devices, or NULL if none were found.
 */
RvtH_QueryEntry *rvth_query_devices(int *pErr)
{
	RvtH_QueryEntry *list_head = NULL;
	RvtH_QueryEntry *list_tail = NULL;

	HDEVINFO hDevInfoSet = NULL;
	SP_DEVINFO_DATA devInfoData;
	int devIndex = 0;

	/* Temporary string buffers */

	// Instance IDs.
	TCHAR s_usbInstanceID[MAX_DEVICE_ID_LEN];	// USB drive
	TCHAR s_diskInstanceID[MAX_DEVICE_ID_LEN];	// Storage device

	// Pathnames.
	TCHAR s_devicePath[MAX_PATH];	// Storage device path

	// Get all USB devices and find one with a matching vendor/device ID.
	// Once we find it, get GUID_DEVINTERFACE_DISK for that ID.
	// References:
	// - https://docs.microsoft.com/en-us/windows/desktop/api/setupapi/nf-setupapi-setupdigetclassdevsw
	// - https://social.msdn.microsoft.com/Forums/vstudio/en-US/76315dfa-6764-4feb-a3e2-1f173fc5bdfd/how-to-get-usb-vendor-and-product-id-programmatically-in-c?forum=vcgeneral
	hDevInfoSet = SetupDiGetClassDevs(&GUID_DEVINTERFACE_DISK, NULL,
		NULL, DIGCF_DEVICEINTERFACE | DIGCF_PRESENT);
	if (!hDevInfoSet || hDevInfoSet == INVALID_HANDLE_VALUE) {
		// SetupDiGetClassDevs() failed.
		if (pErr) {
			// TODO: Convert GetLastError()?
			*pErr = EIO;
		}
		return NULL;
	}

	memset(&devInfoData, 0, sizeof(devInfoData));
	devInfoData.cbSize = sizeof(devInfoData);
	while (SetupDiEnumDeviceInfo(hDevInfoSet, devIndex, &devInfoData)) {
		DEVINST dnDevInstParent;
		TCHAR *p;
		int ret;

		// USB ID
		uint16_t vid = 0, pid = 0;
		unsigned int hw_serial = 0;

		// DeviceIoControl()
		HANDLE hDevice;
		union {
			STORAGE_DEVICE_NUMBER sdn;
			STORAGE_PROPERTY_QUERY spq;
			GET_LENGTH_INFORMATION gli;
		} io;
		union {
			BYTE spq[4096];
			TCHAR tcs[4096/sizeof(TCHAR)];
		} buf;
		DWORD cbBytesReturned;
		BOOL bRet;

		// for CM_Get_DevNode_Registry_Property()
		ULONG ulLength;
		ULONG ulRegDataType;

		// Increment the device index here.
		devIndex++;

		// Get the parent instance ID.
		// References:
		// - https://stackoverflow.com/questions/3098696/get-information-about-disk-drives-result-on-windows7-32-bit-system
		// - https://stackoverflow.com/a/3100268
		ret = CM_Get_Parent(&dnDevInstParent, devInfoData.DevInst, 0);
		if (ret != CR_SUCCESS)
			continue;
		ret = CM_Get_Device_ID(dnDevInstParent, s_usbInstanceID, _countof(s_usbInstanceID), 0);
		if (ret != CR_SUCCESS)
			continue;

		// Format: "USB\\VID_%04X&PID_%04X\\%08u"
		// NOTE: Needs to be converted to uppercase, since "Vid" and "Pid" might show up.
		// Last %08u is the serial number.
		p = s_usbInstanceID;
		while (*p != _T('\0')) {
			*p = _totupper(*p);
			p++;
		}

		// Try sscanf().
		ret = _stscanf(s_usbInstanceID, _T("USB\\VID_%04hX&PID_%04hX\\%08u"), &vid, &pid, &hw_serial);
		if (ret != 3)
			continue;

		// Does the USB device ID match?
		if (vid != RVTH_READER_VID || pid != RVTH_READER_PID) {
			// Not an RVT-H Reader.
			continue;
		}

		// Is the serial number valid?
		// - Wired:    10xxxxxx
		// - Wireless: 20xxxxxx
		if (hw_serial < 10000000 || hw_serial > 29999999) {
			// Not a valid serial number.
			continue;
		}

		// Get this device's instance path.
		// It works as a stand-in for \\\\.\\\PhysicalDrive#
		ret = CM_Get_Device_ID(devInfoData.DevInst, s_diskInstanceID, _countof(s_diskInstanceID), 0);
		if (ret != CR_SUCCESS)
			continue;

		// Use the disk instance ID to create the actual path.
		// NOTE: Needs some adjustments: [FOR COMMIT, use actual RVT-H path]
		// - Disk instance ID:   USBSTOR\DISK&VEN_&PROD_PATRIOT_MEMORY&REV_PMAP\070B7AAA9AF3D977&0
		// - Actual path: \\\\?\\usbstor#disk&ven_&prod_patriot_memory&rev_pmap#070b7aaa9af3d977&0#{53f56307-b6bf-11d0-94f2-00a0c91efb8b}
		// Adjustments required:
		// - Replace backslashes in instance ID with #
		// - Append GUID_DEVINTERFACE_DISK [TODO: Convert from the GUID at runtime?]

		p = s_diskInstanceID;
		while (*p != _T('\0')) {
			if (*p == _T('\\')) {
				*p = _T('#');
			}
			p++;
		}
		_sntprintf(s_devicePath, _countof(s_devicePath),
			_T("\\\\?\\%s#{53f56307-b6bf-11d0-94f2-00a0c91efb8b}"), s_diskInstanceID);

		// Open the drive to retrieve information.
		// NOTE: Might require administrative privileges.
		// References:
		// - https://stackoverflow.com/questions/5583553/name-mapping-physicaldrive-to-scsi
		// - https://stackoverflow.com/a/5584978
		hDevice = CreateFile(s_devicePath,
			GENERIC_READ,		// dwDesiredAccess
			FILE_SHARE_READ,	// dwShareMode
			NULL,			// lpSecurityAttributes
			OPEN_EXISTING,		// dwCreationDisposition
			FILE_ATTRIBUTE_NORMAL,	// dwFlagsAndAttributes
			NULL);			// hTemplateFile
		if (!hDevice || hDevice == INVALID_HANDLE_VALUE) {
			// Can't open this device for some reason...
			DWORD dwErr = GetLastError();
			if (dwErr == ERROR_ACCESS_DENIED) {
				// Access denied.
				// User is probably not Administrator.
				// Stop enumerating devices and return an error.
				if (pErr) {
					*pErr = EACCES;
				}
				if (list_head) {
					rvth_query_free(list_head);
					list_head = NULL;
					list_tail = NULL;
				}
				break;
			}

			// Other error. Continue enumerating.
			// TODO: Return for certain errors?
			continue;
		}

		bRet = DeviceIoControl(hDevice,
			IOCTL_STORAGE_GET_DEVICE_NUMBER,// dwIoControlCode
			NULL, 0,			// no input buffer
			(LPVOID)&io.sdn, sizeof(io.sdn),// output buffer
			&cbBytesReturned,		// # bytes returned
			NULL);				// synchronous I/O
		if (!bRet || cbBytesReturned != sizeof(io.sdn) ||
		    io.sdn.DeviceType != FILE_DEVICE_DISK)
		{
			// Unable to get the device information,
			// or this isn't a disk device.
			CloseHandle(hDevice);
			continue;
		}

		// Create a list entry.
		if (!list_head) {
			// New list head.
			list_head = malloc(sizeof(*list_head));
			if (!list_head) {
				// malloc() failed.
				CloseHandle(hDevice);
				break;
			}
			list_tail = list_head;
			list_tail->next = NULL;
		} else {
			// Add an entry to the current list.
			list_tail->next = malloc(sizeof(*list_tail));
			if (!list_tail->next) {
				// malloc() failed.
				CloseHandle(hDevice);
				rvth_query_free(list_head);
				list_head = NULL;
				list_tail = NULL;
				break;
			}
			list_tail->next = NULL;
		}

		// Device name.
		_sntprintf(s_devicePath, _countof(s_devicePath),
			_T("\\\\.\\PhysicalDrive%u"), io.sdn.DeviceNumber);
		list_tail->device_name = _tcsdup(s_devicePath);

		// Get USB information.
		// Reference: https://docs.microsoft.com/en-us/windows/desktop/api/cfgmgr32/nf-cfgmgr32-cm_get_devnode_registry_propertyw
		ulLength = sizeof(buf.tcs);
		ret = CM_Get_DevNode_Registry_Property(dnDevInstParent,
			CM_DRP_MFG,		// ulProperty
			&ulRegDataType,		// pulRegDataType
			(PVOID)&buf.tcs,	// Buffer
			&ulLength,		// pulLength
			0);			// ulFlags
		if (bRet && ulRegDataType == REG_SZ) {
			// For some reason, this shows up as
			// "Compatible USB storage device" on Windows,
			// even though it should be "Nintendo Co., Ltd.".
			if (!_tcscmp(buf.tcs, _T("Compatible USB storage device"))) {
				list_tail->usb_vendor = _tcsdup(_T("Nintendo Co., Ltd."));
			} else {
				list_tail->usb_vendor = _tcsdup(buf.tcs);
			}
		} else {
			list_tail->usb_vendor = NULL;
		}

		// NOTE: On XP, the device name is stored as "Location Information".
		// On Win7, it doesn't seem to be accessible with cfgmgr32.
		// We'll assume it's always "RVT-H READER".
		list_tail->usb_product = _tcsdup(_T("RVT-H READER"));
#if 0
		ulLength = sizeof(buf.tcs);
		ret = CM_Get_DevNode_Registry_Property(dnDevInstParent,
			CM_DRP_LOCATION_INFORMATION,	// ulProperty
			NULL,			// pulRegDataType
			(PVOID)&buf.tcs,	// Buffer
			&ulLength,		// pulLength
			0);			// ulFlags
		if (bRet && ulRegDataType == REG_SZ) {
			list_tail->usb_product = _tcsdup(buf.tcs);
		} else {
			list_tail->usb_product = NULL;
		}
#endif

		// RVT-H Reader serial number.
		// TODO: Prepend "HMA" or "HUA" to the serial number?
		_sntprintf(buf.tcs, sizeof(buf.tcs), _T("%08u"), hw_serial);
		list_tail->usb_serial = _tcsdup(buf.tcs);

		// Get more drive information.
		// References:
		// - https://stackoverflow.com/questions/327718/how-to-list-physical-disks
		// - https://stackoverflow.com/a/18183115
		io.spq.PropertyId = StorageDeviceProperty;
		io.spq.QueryType = PropertyStandardQuery;
		io.spq.AdditionalParameters[0] = 0;
		bRet = DeviceIoControl(hDevice,
			IOCTL_STORAGE_QUERY_PROPERTY,	// dwIoControlCode
			&io.spq, sizeof(io.spq),	// input buffer
			&buf.spq, sizeof(buf.spq),	// output buffer
			&cbBytesReturned,		// # bytes returned
			NULL);				// synchronous I/O
		if (bRet) {
			const STORAGE_DEVICE_DESCRIPTOR *const psdp = (const STORAGE_DEVICE_DESCRIPTOR*)buf.spq;
			if (psdp->VendorIdOffset) {
				list_tail->hdd_vendor = A2T_dup(&buf.spq[psdp->VendorIdOffset], -1);
			} else {
				list_tail->hdd_vendor = NULL;
			}
			if (psdp->ProductIdOffset) {
				list_tail->hdd_model = A2T_dup(&buf.spq[psdp->ProductIdOffset], -1);
			} else {
				list_tail->hdd_model = NULL;
			}
			if (psdp->ProductRevisionOffset) {
				list_tail->hdd_fwver = A2T_dup(&buf.spq[psdp->ProductRevisionOffset], -1);
			} else {
				list_tail->hdd_fwver = NULL;
			}

#ifdef RVTH_QUERY_ENABLE_HDD_SERIAL
			if (psdp->SerialNumberOffset) {
				list_tail->hdd_serial = A2T_dup(&buf.spq[psdp->SerialNumberOffset], -1);
			} else {
				list_tail->hdd_serial = NULL;
			}
#endif /* RVTH_QUERY_ENABLE_HDD_SERIAL */
		}

		// Get the disk capacity.
		bRet = DeviceIoControl(hDevice,
			IOCTL_DISK_GET_LENGTH_INFO,	// dwIoControlCode
			NULL, 0,			// no input buffer
			(LPVOID)&io.gli, sizeof(io.gli),// output buffer
			&cbBytesReturned,		// number of bytes returned
			nullptr);			// OVERLAPPED structure
		if (bRet && cbBytesReturned == sizeof(io.gli)) {
			// Size obtained successfully.
			list_tail->size = io.gli.Length.QuadPart;
		} else {
			// Unable to obtain the size.
			list_tail->size = 0;
		}

		CloseHandle(hDevice);
	}

	SetupDiDestroyDeviceInfoList(hDevInfoSet);
	return list_head;
}
