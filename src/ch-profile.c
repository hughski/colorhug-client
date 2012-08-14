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
	ChDeviceQueue *device_queue = NULL;
	gboolean ret;
	GError *error = NULL;
	GFile *file = NULL;
	gint rc = EXIT_SUCCESS;
	GMainLoop *loop = NULL;
	GString *string = NULL;
	GTimer *timer = NULL;
	GtkWindow *sample_window = NULL;
	guint i;
	GUsbDevice *device = NULL;

	gtk_init (&argc, &argv);

	/* use a sample window to get the measurements */
	sample_window = cd_sample_window_new ();
	gtk_window_set_keep_above (sample_window, TRUE);
	loop = g_main_loop_new (NULL, FALSE);

	device_queue = ch_device_queue_new ();
	device = ch_util_get_default_device (&error);
	if (device == NULL) {
		rc = EXIT_FAILURE;
		g_print ("No connection to device: %s\n", error->message);
		g_error_free (error);
		goto out;
	}

	/* set up display */
	ch_device_queue_set_multiplier (device_queue,
					device,
					CH_FREQ_SCALE_100);
	ch_device_queue_set_integral_time (device_queue,
					   device,
					   0xffff);
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

	/* record the sample time of each sample */
	timer = g_timer_new ();
	string = g_string_new ("Index,"
			       "Frequency R,"
			       "Frequency G,"
			       "Frequency B,"
			       "Frequency Time,"
			       "Duration R,"
			       "Duration G,"
			       "Duration B,"
			       "Duration Time,\n");
	for (i = 0; i <= 256; i += 1) {

		source.R = (1.0f / 256.0f) * i;
		source.G = source.R;
		source.B = source.R;
		cd_sample_window_set_color (CD_SAMPLE_WINDOW (sample_window), &source);
		cd_sample_window_set_fraction (CD_SAMPLE_WINDOW (sample_window), source.R);
		g_timeout_add (200, (GSourceFunc) cd_sample_window_loop_cb, loop);
		g_main_loop_run (loop);

		/* add 'Index' */
		g_string_append_printf (string, "%i,", i);

		/* do frequency sample */
		g_timer_reset (timer);

		ch_device_queue_set_measure_mode (device_queue,
						  device,
						  CH_MEASURE_MODE_FREQUENCY);
		ch_device_queue_take_readings (device_queue,
					       device,
					       &value_rgb);
		ret = ch_device_queue_process (device_queue,
					       CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
					       NULL,
					       &error);
		if (!ret) {
			rc = EXIT_FAILURE;
			g_print ("Failed to get freq RGB reading: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* add 'Frequency R,G,B' and 'Frequency Time' */
		g_string_append_printf (string, "%lf,%lf,%lf,",
					value_rgb.R,
					value_rgb.G,
					value_rgb.B);
		g_string_append_printf (string, "%lf,",
					g_timer_elapsed (timer, NULL) * 1000);
		g_print ("FRQ: %lf,%lf,%lf\n",
			 value_rgb.R, value_rgb.G, value_rgb.B);

		/* do duration sample */
		g_timer_reset (timer);
		ch_device_queue_set_measure_mode (device_queue,
						  device,
						  CH_MEASURE_MODE_DURATION);
		ch_device_queue_take_readings (device_queue,
					       device,
					       &value_rgb);
		ret = ch_device_queue_process (device_queue,
					       CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
					       NULL,
					       &error);
		if (!ret) {
			rc = EXIT_FAILURE;
			g_print ("Failed to get duration RGB reading: %s\n", error->message);
			g_error_free (error);
			goto out;
		}

		/* add 'Duration R,G,B' and 'Duration Time' */
		g_string_append_printf (string, "%lf,%lf,%lf,",
					value_rgb.R,
					value_rgb.G,
					value_rgb.B);
		g_string_append_printf (string, "%lf,",
					g_timer_elapsed (timer, NULL) * 1000);
		g_print ("DUR: %lf,%lf,%lf\n",
			 value_rgb.R, value_rgb.G, value_rgb.B);
		g_string_append (string, "\n");
	}

	/* save file */
	ret = g_file_set_contents ("./data.csv",
				   string->str, -1, &error);
	if (!ret) {
		rc = EXIT_FAILURE;
		g_print ("Failed to save data file: %s\n",
			 error->message);
		g_error_free (error);
		goto out;
	}
out:
	if (string != NULL)
		g_string_free (string, TRUE);
	if (timer != NULL)
		g_timer_destroy (timer);
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
	return rc;
}
