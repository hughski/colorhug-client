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

// compile with: gcc -Wall -o ch-libcolorhug ch-libcolorhug.c `pkg-config --libs --cflags colorhug`

#define G_USB_API_IS_SUBJECT_TO_CHANGE
#include <colorhug.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static gboolean
connect_device (GUsbDeviceList *list, GUsbDevice **device_out, GError **error)
{
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
	ret = ch_device_open (device, error);
	if (!ret)
		goto out;

	g_warning ("Device ready!");
	*device_out = g_object_ref (device);
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
	GUsbContext *ctx;
	GUsbDevice *device = NULL;
	int retval = 0;
	GUsbDeviceList *list = NULL;
	ChDeviceQueue *device_queue;

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
	device_queue = ch_device_queue_new ();

	ret = connect_device (list, &device, &error);
	if (!ret) {
		g_warning ("Cannot connect to device : %s", error->message);
		g_error_free (error);
		retval = 1;
		goto out;
	}

	/* reset device so it boots back into bootloader mode */
	g_warning ("Switching to bootloader mode\n");
	ch_device_queue_reset (device_queue, device);
	ret = ch_device_queue_process (device_queue,
				       CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
				       NULL,
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
	ch_device_queue_boot_flash (device_queue, device);
	ret = ch_device_queue_process (device_queue,
				       CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
				       NULL,
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
	ch_device_queue_set_leds (device_queue, device, 3, 0, 0, 0);
	ret = ch_device_queue_process (device_queue,
				       CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
				       NULL,
				       &error);
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
	if (device_queue != NULL)
		g_object_unref (device_queue);
	if (device != NULL)
		g_object_unref (device);
	if (list != NULL)
		g_object_unref (list);
	return retval;
}
