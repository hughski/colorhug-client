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

// compile with: gcc -Wall -o ch-gusb ch-gusb.c `pkg-config --libs --cflags gusb`

#define G_USB_API_IS_SUBJECT_TO_CHANGE
#include <gusb.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define	CH_USB_VID				0x273f
#define	CH_USB_PID_FIRMWARE			0x1001
#define	CH_USB_PID_BOOTLOADER			0x1000
#define	CH_USB_VID_LEGACY			0x04d8
#define	CH_USB_PID_LEGACY			0xf8da

static gboolean
connect_device (GUsbDeviceList *list, GUsbDevice **device_out, GError **error)
{
	const gint configuration = 1;
	gboolean ret;
	GUsbDevice *device = NULL;

	/* find the first colorhug */
	g_usb_device_list_coldplug (list);
	device = g_usb_device_list_find_by_vid_pid (list,
						    CH_USB_VID_LEGACY,
						    CH_USB_PID_LEGACY,
						    NULL);
	if (device == NULL) {
		device = g_usb_device_list_find_by_vid_pid (list,
							    CH_USB_VID,
							    CH_USB_PID_BOOTLOADER,
							    NULL);
	}
	if (device == NULL) {
		device = g_usb_device_list_find_by_vid_pid (list,
							    CH_USB_VID,
							    CH_USB_PID_FIRMWARE,
							    error);
	}
	if (device == NULL) {
		ret = FALSE;
		goto out;
	}

	/* open device */
	ret = g_usb_device_open (device, error);
	if (!ret)
		goto out;

	/* set configuration if different */
	ret = g_usb_device_set_configuration (device,
					      configuration,
					      error);
	if (!ret)
		goto out;

	/* claim interface */
	ret = g_usb_device_claim_interface (device,
					    0,
					    G_USB_DEVICE_CLAIM_INTERFACE_BIND_KERNEL_DRIVER,
					    error);
	if (!ret)
		goto out;

	g_warning ("Device ready!");
	*device_out = g_object_ref (device);
out:
	return ret;
}

static gboolean
write_command (GUsbDevice *device,
	       gchar cmd,
	       const gchar *in,
	       gint in_size,
	       gchar *out,
	       gint out_size,
	       GError **error)
{
	gboolean ret;
	gsize actual_length;
	guint8 buffer[64];

	/* setup write packet */
	memset (buffer, 0x00, sizeof (buffer));
	buffer[0] = cmd;
	if (in != NULL)
		memcpy (buffer + 1, in, in_size);

	/* write to device */
	ret = g_usb_device_interrupt_transfer (device, 0x01, buffer, 64, &actual_length, 5000, NULL, error);
	if (!ret)
		goto out;
	if (actual_length != 64) {
		ret = FALSE;
		g_set_error (error, 1, 0,
			     "Failed to write stream, only wrote %li\n",
			     actual_length);
		goto out;
	}

	/* read status */
	ret = g_usb_device_interrupt_transfer (device, 0x81, buffer, 64, &actual_length, 5000, NULL, error);
	if (!ret)
		goto out;
	if (actual_length != 2 + out_size && actual_length != 64) {
		ret = FALSE;
		g_set_error (error, 1, 0,
			     "Failed to read, got %li bytes\n",
			     actual_length);
		goto out;
	}
	if (buffer[0] != 0x00) {
		ret = FALSE;
		g_set_error (error, 1, 0,
			     "Failed to get valid command status %i\n",
			     buffer[0]);
		goto out;
	}
	if (buffer[1] != cmd) {
		ret = FALSE;
		g_set_error (error, 1, 0,
			     "Failed to get valid command value %i\n",
			     buffer[1]);
		goto out;
	}

	/* copy data out of buffer */
	if (out != NULL)
		memcpy (out, buffer + 2, out_size);

	/* success */
	ret = TRUE;
out:
	return ret;
}

/**
 * quit_loop_cb:
 **/
static gboolean
quit_loop_cb (gpointer user_data)
{
	GMainLoop *loop = (GMainLoop *) user_data;
	g_main_loop_quit (loop);
	return FALSE;
}

int
main (int argc, char *argv[])
{
	gboolean ret;
	GError *error = NULL;
	GMainLoop *loop = NULL;
	guint8 led = 0x03;
	GUsbContext *ctx;
	GUsbDevice *device = NULL;
	int retval = 0;
	GUsbDeviceList *list = NULL;

	/* setup usb */
	g_type_init ();
	ctx = g_usb_context_new (&error);
	if (ctx == NULL) {
		g_warning ("Cannot connect to USB : %s", error->message);
		g_error_free (error);
		retval = 1;
		goto out;
	}
	list = g_usb_device_list_new (ctx);

	ret = connect_device (list, &device, &error);
	if (!ret) {
		g_warning ("Cannot connect to device : %s", error->message);
		g_error_free (error);
		retval = 1;
		goto out;
	}

	/* reset device so it boots back into bootloader mode */
	g_warning ("Switching to bootloader mode\n");
	ret = write_command (device,
			    0x24,	/* cmd */
			    NULL,	 /* in buffer */
			    0,		/* in buffer size */
			    NULL,	/* out buffer */
			    0,		/* out buffer size */
			    &error);
	if (!ret) {
		g_warning ("Failed to reboot : %s", error->message);
		g_error_free (error);
		retval = 1;
		goto out;
	}

	/* wait for device to re-appear */
	loop = g_main_loop_new (NULL, FALSE);
	g_timeout_add (5000, quit_loop_cb, loop);
	g_main_loop_run (loop);
	g_object_unref (device);
	ret = connect_device (list, &device, &error);
	if (!ret) {
		g_warning ("Cannot connect to device : %s", error->message);
		g_error_free (error);
		retval = 1;
		goto out;
	}

	/* boot into firmware mode */
	g_warning ("Switching to firmware mode\n");
	ret = write_command (device,
			    0x27,	/* cmd */
			    NULL,	/* in buffer */
			    0,		/* in buffer size */
			    NULL,	/* out buffer */
			    0,		/* out buffer size */
			    &error);
	if (!ret) {
		g_warning ("Failed to boot into firmware mode : %s", error->message);
		g_error_free (error);
		retval = 1;
		goto out;
	}

	/* wait for device to re-appear */
	g_timeout_add (5000, quit_loop_cb, loop);
	g_main_loop_run (loop);
	g_object_unref (device);
	ret = connect_device (list, &device, &error);
	if (!ret) {
		g_warning ("Cannot connect to device : %s", error->message);
		g_error_free (error);
		retval = 1;
		goto out;
	}

	/* turn on LEDs */
	g_warning ("Turning on LEDs\n");
	ret = write_command (device,
			    0x0e,	/* cmd */
			    (char *) &led, /* in buffer */
			    1,		/* in buffer size */
			    NULL,	/* out buffer */
			    0,
			    &error);		/* out buffer size */
	if (!ret) {
		g_warning ("Failed to turn on LEDs : %s", error->message);
		g_error_free (error);
		retval = 1;
		goto out;
	}

	/* success */
	g_warning ("ALL OKAY\n");
out:
	if (loop != NULL)
		g_main_loop_unref (loop);
	if (ctx != NULL)
		g_object_unref (ctx);
	if (device != NULL)
		g_object_unref (device);
	if (list != NULL)
		g_object_unref (list);
	return retval;
}
