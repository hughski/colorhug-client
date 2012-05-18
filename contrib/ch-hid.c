/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2012 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

// compile with: gcc -o ch-hid ch-hid.c -lhid -lusb

#include <hid.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define	CH_USB_VID				0x273f
#define	CH_USB_PID_FIRMWARE			0x1001

int main (int argc, char *argv[])
{
	char buffer[64];
	HIDInterface* hid;
	HIDInterfaceMatcher matcher = { CH_USB_VID, CH_USB_PID_FIRMWARE, NULL, NULL, 0 };
	hid_return ret;
	int i;
	int iface_num = 0;
	int length;
	unsigned int timeout = 1000; /* ms */

	hid_set_debug (HID_DEBUG_ALL);
	hid_set_debug_stream (stderr);
	hid_set_usb_debug (0);

	ret = hid_init ();
	if (ret != HID_RET_SUCCESS) {
		fprintf (stderr, "hid_init failed with return code %d\n", ret);
		return 1;
	}

	hid = hid_new_HIDInterface ();
	if (hid == 0) {
		fprintf (stderr, "hid_new_HIDInterface () failed\n");
		return 1;
	}

	ret = hid_force_open (hid, iface_num, &matcher, 3);
	if (ret != HID_RET_SUCCESS) {
		fprintf (stderr, "hid_force_open failed with return code %d\n", ret);
		return 1;
	}

	ret = hid_write_identification (stdout, hid);
	if (ret != HID_RET_SUCCESS) {
		fprintf (stderr, "hid_write_identification failed with return code %d\n", ret);
		return 1;
	}

	ret = hid_dump_tree (stdout, hid);
	if (ret != HID_RET_SUCCESS) {
		fprintf (stderr, "hid_dump_tree failed with return code %d\n", ret);
		return 1;
	}

	/* turn all the LEDs on */
	memset (buffer, 0x00, sizeof (buffer));
	buffer[0] = 0x0e;
	buffer[1] = 0x03;
	ret = hid_interrupt_write (hid, 0x01,  buffer, sizeof (buffer), timeout);
	if (ret != HID_RET_SUCCESS) {
		fprintf (stderr, "failed calling hid_interrupt_write\n");
		return 1;
	}

	/* read the return message */
	length = 2;
	ret = hid_interrupt_read (hid, 0x01, buffer, length, timeout);
	if (ret != HID_RET_SUCCESS) {
		printf ("hid_interrupt_read failed\n");
		return 1;
	}
	printf ("Data received from USB device: ");
	for (i=0; i < length; i++) {
		printf ("0x%02x, ", buffer[i]);
	}
	printf ("\n");

	ret = hid_close (hid);
	if (ret != HID_RET_SUCCESS) {
		fprintf (stderr, "hid_close failed with return code %d\n", ret);
		return 1;
	}

	hid_delete_HIDInterface (&hid);

	ret = hid_cleanup ();
	if (ret != HID_RET_SUCCESS) {
		fprintf (stderr, "hid_cleanup failed with return code %d\n", ret);
		return 1;
	}

	return 0;
}
