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

#include "config.h"

#include <glib/gi18n.h>
#include <stdio.h>
#include <stdlib.h>
#include <colord.h>
#include <colord-gtk.h>
#include <colorhug.h>

/**
 * cd_sample_window_loop_cb:
 **/
static gboolean
cd_sample_window_loop_cb (GMainLoop *loop)
{
	g_main_loop_quit (loop);
	return FALSE;
}

/**
 * ch_util_get_default_device:
 **/
static GUsbDevice *
ch_util_get_default_device (GError **error)
{
	gboolean ret;
	GPtrArray *devices;
	guint i;
	GUsbContext *usb_ctx;
	GUsbDevice *device = NULL;
	GUsbDevice *device_success = NULL;
	GUsbDevice *device_tmp;
	GUsbDeviceList *list;

	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* try to find the ColorHug device */
	usb_ctx = g_usb_context_new (NULL);
	list = g_usb_device_list_new (usb_ctx);
	g_usb_device_list_coldplug (list);

	/* ensure we only find one device */
	devices = g_usb_device_list_get_devices (list);
	for (i = 0; i < devices->len; i++) {
		device_tmp = g_ptr_array_index (devices, i);
		if (!ch_device_is_colorhug (device_tmp))
			continue;
		if (device != NULL) {
			g_set_error_literal (error, 1, 0,
					     "Multiple ColorHug devices are attached");
			goto out;
		}
		device = g_object_ref (device_tmp);
	}
	if (device == NULL) {
		g_set_error_literal (error, 1, 0,
				     "No ColorHug devices were found");
		goto out;
	}
	g_debug ("Found ColorHug device %s",
		 g_usb_device_get_platform_id (device));
	ret = ch_device_open (device, error);
	if (!ret)
		goto out;

	/* success */
	device_success = g_object_ref (device);
out:
	g_object_unref (usb_ctx);
	if (device != NULL)
		g_object_unref (device);
	if (list != NULL)
		g_object_unref (list);
	if (devices != NULL)
		g_ptr_array_unref (devices);
	return device_success;
}

/**
 * main:
 **/
int
main (int argc, char *argv[])
{
	CdColorRGB source;
	CdColorRGB value_rgb;
	CdIt8 *it8 = NULL;
	ChDeviceQueue *device_queue = NULL;
	ChMeasureMode measure_mode;
	gboolean ret;
	GError *error = NULL;
	GFile *file = NULL;
	gint rc = EXIT_SUCCESS;
	GMainLoop *loop = NULL;
	GtkWindow *sample_window = NULL;
	guint i;
	GUsbDevice *device = NULL;

	gtk_init (&argc, &argv);

	/* parse the command */
	if (argc != 2) {
		g_print ("Usage: colorhug-profile duration|frequency\n");
		goto out;
	}
	if (g_strcmp0 (argv[1], "duration") == 0) {
		measure_mode = CH_MEASURE_MODE_DURATION;
	} else if (g_strcmp0 (argv[1], "frequency") == 0) {
		measure_mode = CH_MEASURE_MODE_FREQUENCY;
	} else {
		g_print ("Usage: colorhug-profile duration|frequency\n");
		goto out;
	}

	/* use a sample window to get the measurements */
	sample_window = cd_sample_window_new ();
	gtk_window_set_keep_above (sample_window, TRUE);
	loop = g_main_loop_new (NULL, FALSE);
	it8 = cd_it8_new_with_kind (CD_IT8_KIND_TI3);
	cd_it8_set_originator (it8, "colorhug-profile");
	if (measure_mode == CH_MEASURE_MODE_FREQUENCY)
		cd_it8_set_title (it8, "colorhug freq");
	else
		cd_it8_set_title (it8, "colorhug duration");
	cd_it8_set_normalized (it8, TRUE);

	device_queue = ch_device_queue_new ();
	device = ch_util_get_default_device (&error);
	if (device == NULL) {
		rc = EXIT_FAILURE;
		g_print ("No connection to device: %s\n", error->message);
		g_error_free (error);
		goto out;
	}

	ch_device_queue_set_multiplier (device_queue,
					device,
					CH_FREQ_SCALE_100);
	ch_device_queue_set_integral_time (device_queue,
					   device,
					   0xffff);
	ch_device_queue_set_measure_mode (device_queue,
					  device,
					  measure_mode);
	ret = ch_device_queue_process (device_queue,
				       CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
				       NULL,
				       &error);
	if (!ret) {
		rc = EXIT_FAILURE;
		g_print ("Failed to setup sensor: %s\n", error->message);
		g_error_free (error);
		goto out;
	}

	/* move to the center of device lvds1 */
	gtk_window_present (sample_window);

	for (i = 0; i <= 256; i += 1) {

		source.R = (1.0f / 256.0f) * i;
		source.G = source.R;
		source.B = source.R;
		cd_sample_window_set_color (CD_SAMPLE_WINDOW (sample_window), &source);
		cd_sample_window_set_fraction (CD_SAMPLE_WINDOW (sample_window), source.R);
		g_timeout_add (200, (GSourceFunc) cd_sample_window_loop_cb, loop);
		g_main_loop_run (loop);

		ch_device_queue_take_readings (device_queue,
					       device,
					       &value_rgb);
		ret = ch_device_queue_process (device_queue,
					       CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
					       NULL,
					       &error);
		if (!ret) {
			rc = EXIT_FAILURE;
			g_print ("Failed to get XYZ reading: %s\n", error->message);
			g_error_free (error);
			goto out;
		}
		g_print ("%f,%f,%f\n", value_rgb.R, value_rgb.G, value_rgb.B);

		cd_it8_add_data (it8, &source, (CdColorXYZ *) &value_rgb);
	}

	/* save file */
	if (measure_mode == CH_MEASURE_MODE_FREQUENCY)
		file = g_file_new_for_path ("./freq.ti3");
	else
		file = g_file_new_for_path ("./duration.ti3");
	ret = cd_it8_save_to_file (it8, file, &error);
	if (!ret) {
		rc = EXIT_FAILURE;
		g_print ("Failed to save file: %s\n", error->message);
		g_error_free (error);
		goto out;
	}
out:
	if (device != NULL)
		g_object_unref (device);
	if (file != NULL)
		g_object_unref (file);
	if (loop != NULL)
		g_main_loop_unref (loop);
	if (sample_window != NULL)
		gtk_widget_destroy (GTK_WIDGET (sample_window));
	if (device_queue != NULL)
		g_object_unref (device_queue);
	if (it8 != NULL)
		g_object_unref (it8);
	return rc;
}
