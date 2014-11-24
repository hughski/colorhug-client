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

// compile with: gcc -Wall -o ch-usb1 ch-usb1.c -lusb-1.0

#include <libusb-1.0/libusb.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define	CH_USB_VID				0x273f
#define	CH_USB_PID_FIRMWARE			0x1001
#define	CH_USB_PID_BOOTLOADER			0x1000
#define	CH_USB_VID_LEGACY			0x04d8
#define	CH_USB_PID_LEGACY			0xf8da

static struct libusb_device *
find_colorhug_device (libusb_device **dev_list)
{
	int i;
	int rc;
	libusb_device *found = NULL;
	struct libusb_device_descriptor desc;

	for (i = 0; dev_list && dev_list[i]; i++) {
		rc = libusb_get_device_descriptor (dev_list[i], &desc);
		if (rc != LIBUSB_SUCCESS)
			printf ("Failed to get USB descriptor for device: %i", rc);
		if ((desc.idVendor == CH_USB_VID &&
		     (desc.idProduct == CH_USB_PID_FIRMWARE ||
		      desc.idProduct == CH_USB_PID_BOOTLOADER)) ||
		    (desc.idVendor == CH_USB_VID_LEGACY &&
		     desc.idProduct == CH_USB_PID_LEGACY)) {
			found = dev_list[i];
			goto out;
		}
	}
out:
	return found;
}

static int
connect_device (libusb_context *ctx, libusb_device_handle **handle_out)
{
	const int configuration = 1;
	int config_tmp;
	int rc;
	libusb_device **dev_list;
	libusb_device *dev = NULL;
	libusb_device_handle *handle = NULL;

	/* wait, then look for device again */
	libusb_get_device_list (ctx, &dev_list);
	dev = find_colorhug_device (dev_list);
	if (dev == NULL) {
		printf ("Cannot find device!\n");
		rc = -1;
		goto out;
	}

	/* open device */
	rc = libusb_open (dev, &handle);
	if (rc < 0) {
		printf ("Cannot open device!\n");
		goto out;
	}

	/* set configuration if different */
	rc = libusb_get_configuration (handle, &config_tmp);
	if (rc < 0) {
		printf ("Failed to get configuration!\n");
		goto out;
	}
	if (config_tmp != configuration) {
		rc = libusb_set_configuration (handle, configuration);
		if (rc < 0)
			printf ("Failed to set configuration, got %s\n", libusb_error_name (rc));
	}

	/* this is not fatal, as we might have already detached the
	 * hid driver */
	rc = libusb_detach_kernel_driver (handle, 0);
	if (rc < 0)
		printf ("Failed to detach kernel driver, got %s\n", libusb_error_name (rc));

	/* claim interface */
	rc = libusb_claim_interface (handle, 0);
	if (rc < 0)
		printf ("Failed to claim interface, got %s\n", libusb_error_name (rc));
	printf ("Device ready!\n");
	*handle_out = handle;
	rc = 0;
out:
	libusb_free_device_list (dev_list, 1);
	return rc;
}

static int
write_command (libusb_device_handle *handle,
	       char cmd,
	       const char *in,
	       int in_size,
	       char *out,
	       int out_size)
{
	unsigned char buffer[64];
	int rc;
	int ret = -1;
	int actual_length;

	/* setup write packet */
	memset (buffer, 0x00, sizeof (buffer));
	buffer[0] = cmd;
	if (in != NULL)
		memcpy (buffer + 1, in, in_size);

	/* write to device */
	rc = libusb_interrupt_transfer (handle, 0x01, buffer, 64, &actual_length, 5000);
	if (rc < 0) {
		printf ("Failed to write, got %s\n", libusb_error_name (rc));
		goto out;
	}
	if (actual_length < 64) {
		printf ("Failed to write stream, only wrote %i\n", actual_length);
		goto out;
	}

	/* read status */
	rc = libusb_interrupt_transfer (handle, 0x81, buffer, 64, &actual_length, 5000);
	if (rc < 0) {
		printf ("Failed to read, got %s\n", libusb_error_name (rc));
		goto out;
	}
	if (actual_length != 2 + out_size && actual_length != 64) {
		printf ("Failed to read, got %i bytes\n", actual_length);
		goto out;
	}
	if (buffer[0] != 0x00) {
		printf ("Failed to get valid command status %i\n", buffer[0]);
		goto out;
	}
	if (buffer[1] != cmd) {
		printf ("Failed to get valid command value %i\n", buffer[1]);
		goto out;
	}

	/* copy data out of buffer */
	if (out != NULL)
		memcpy (out, buffer + 2, out_size);

	/* success */
	ret = 0;
out:
	return ret;
}

int
main (int argc, char *argv[])
{
	int8_t led = 0x03;
	int rc;
	int retval = 0;
	libusb_context *ctx;
	libusb_device_handle *handle = NULL;

	/* setup usb */
	rc = libusb_init (&ctx);
	if (rc < 0) {
		printf ("failed to init libusb: %i", rc);
		retval = 1;
		goto out;
	}

	/* find the first colorhug */
	rc = connect_device (ctx, &handle);
	if (rc < 0) {
		printf ("Cannot connect to device!\n");
		retval = 1;
		goto out;
	}

	/* reset device so it boots back into bootloader mode */
	printf ("Switching to bootloader mode\n");
	rc = write_command (handle,
			    0x24,	/* cmd */
			    NULL,	 /* in buffer */
			    0,		/* in buffer size */
			    NULL,	/* out buffer */
			    0);		/* out buffer size */
	if (rc < 0) {
		printf ("Failed to reboot\n");
		retval = 1;
		goto out;
	}

	/* wait for device to re-appear */
	usleep (5000000);
	rc = connect_device (ctx, &handle);

	/* boot into firmware mode */
	printf ("Switching to firmware mode\n");
	rc = write_command (handle,
			    0x27,	/* cmd */
			    NULL,	/* in buffer */
			    0,		/* in buffer size */
			    NULL,	/* out buffer */
			    0);		/* out buffer size */
	if (rc < 0) {
		printf ("Failed to boot into firmware mode\n");
		retval = 1;
		goto out;
	}

	/* wait for device to re-appear */
	usleep (5000000);
	rc = connect_device (ctx, &handle);

	/* turn on LEDs */
	printf ("Turning on LEDs\n");
	rc = write_command (handle,
			    0x0e,	/* cmd */
			    (char *) &led, /* in buffer */
			    1,		/* in buffer size */
			    NULL,	/* out buffer */
			    0);		/* out buffer size */
	if (rc < 0) {
		printf ("Failed to turn on LEDs\n");
		retval = 1;
		goto out;
	}

	/* success */
	printf ("ALL OKAY\n");
out:
	if (handle != NULL) {
		rc = libusb_release_interface (handle, 1);
		if (rc < 0)
			printf ("Failed to release interface, got %s\n", libusb_error_name (rc));
		libusb_close (handle);
	}
	return retval;
}
