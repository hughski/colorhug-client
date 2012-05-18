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

// compile with: gcc -o ch-usb ch-usb.c -lusb

#include <usb.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define	CH_USB_VID				0x273f
#define	CH_USB_PID_FIRMWARE			0x1001

static struct usb_device *
find_colorhug_device (void)
{
	struct usb_bus *bus;
	struct usb_bus *busses;
	struct usb_device *dev;
	struct usb_device *found = NULL;

	busses = usb_get_busses();
	for (bus = busses; bus; bus = bus->next) {
		for (dev = bus->devices; dev; dev = dev->next) {
			if (dev->descriptor.idVendor == CH_USB_VID &&
			    dev->descriptor.idProduct == CH_USB_PID_FIRMWARE) {
				found = dev;
				goto out;
			}
		}
	}
out:
	return found;
}

static int
write_command (usb_dev_handle *handle,
	       char cmd,
	       const char *in,
	       int in_size,
	       char *out,
	       int out_size)
{
	char buffer[64];
	int rc;
	int ret = -1;

	/* setup write packet */
	memset (buffer, 0x00, sizeof (buffer));
	buffer[0] = cmd;
	if (in != NULL)
		memcpy (buffer + 1, in, in_size);

	/* write to device */
	rc = usb_interrupt_write (handle, 0x01, buffer, 64, 5000);
	if (rc < 0) {
		printf ("Failed to write, got %s\n", usb_strerror ());
		goto out;
	}
	if (rc != 64) {
		printf ("Failed to write stream, only wrote %i\n", rc);
		goto out;
	}

	/* read status */
	rc = usb_interrupt_read (handle, 0x81, buffer, 64, 5000);
	if (rc != 2 + out_size) {
		printf ("Failed to read, got %i bytes\n", rc);
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
	int16_t calibration_index = 0x0;
	int16_t integral = 0xffff;
	int32_t xyz[3];
	int8_t led = 0x03;
	int8_t multiplier = 0x03;
	int rc;
	int retval = 0;
	struct usb_device *dev = NULL;
	usb_dev_handle *handle = NULL;

	/* setup usb */
	usb_init();
	usb_find_busses();
	usb_find_devices();

	/* find the first colorhug */
	dev = find_colorhug_device ();
	if (dev == NULL) {
		printf ("Cannot find device!\n");
		retval = 1;
		goto out;
	}

	/* open device */
	handle = usb_open (dev);
	if (handle == NULL) {
		printf ("Cannot open device!\n");
		retval = 1;
		goto out;
	}
	rc = usb_set_configuration (handle, 1);
	if (rc < 0)
		printf ("Failed to set configuration, got %s\n", usb_strerror ());

	/* this is not fatal, as we might have already detached the
	 * hid driver */
	rc = usb_detach_kernel_driver_np(handle, 0);
	if (rc < 0)
		printf ("Failed to detach kernel driver, got %s\n", usb_strerror ());

	/* claim interface */
	usb_claim_interface (handle, 0);
	printf ("device ready!\n");

	/* turn on LEDs */
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

	/* set the multiplier to 100% */
	rc = write_command (handle,
			    0x04,	/* cmd */
			    &multiplier, /* in buffer */
			    1,		/* in buffer size */
			    NULL,	/* out buffer */
			    0);		/* out buffer size */
	if (rc < 0) {
		printf ("Failed to set multiplier\n");
		retval = 1;
		goto out;
	}

	/* set the integral time to maximum */
	rc = write_command (handle,
			    0x04,	/* cmd */
			    (char *) &integral, /* in buffer */
			    2,		/* in buffer size */
			    NULL,	/* out buffer */
			    0);		/* out buffer size */
	if (rc < 0) {
		printf ("Failed to set integral\n");
		retval = 1;
		goto out;
	}

	/* take reading with default matrix for LCD */
	memset (xyz, 0x00, sizeof (3*4));
	rc = write_command (handle,
			    0x23,	/* cmd */
			    (char *) &calibration_index, /* in buffer */
			    2,		/* in buffer size */
			    (char *) xyz, /* out buffer */
			    3*4);	/* out buffer size */
	if (rc < 0) {
		printf ("Failed to take reading\n");
		retval = 1;
		goto out;
	}
	printf ("X: %lf\n", (double) xyz[0] / (double) 0xffff);
	printf ("Y: %lf\n", (double) xyz[1] / (double) 0xffff);
	printf ("Z: %lf\n", (double) xyz[2] / (double) 0xffff);

out:
	if (handle != NULL) {
		usb_release_interface (handle, 1);
		usb_close (handle);
	}
	return retval;
}
