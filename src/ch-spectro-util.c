/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2009-2012 Richard Hughes <richard@hughsie.com>
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
#include <gtk/gtk.h>
#include <locale.h>
#include <colord.h>
#include <math.h>
#include <gusb.h>
#include <colorhug.h>

#include "ch-graph-widget.h"

typedef struct {
	GtkApplication	*application;
	GtkBuilder	*builder;
	GUsbContext	*usb_ctx;
	GUsbDevice	*device;
	GUsbDeviceList	*device_list;
	ChDeviceQueue	*device_queue;
	GSettings	*settings;
	GtkWidget	*graph_raw;
	GtkWidget	*graph_dark_cal;
	GtkWidget	*graph_cmf;
	GtkWidget	*graph_temp_comp;
	GtkWidget	*graph_result;
	guint		 timer_id;
} ChCcmxPrivate;

/**
 * ch_util_error_dialog:
 **/
static void
ch_util_error_dialog (ChCcmxPrivate *priv,
		      const gchar *title,
		      const gchar *message)
{
	GtkWindow *window;
	GtkWidget *dialog;

	window = GTK_WINDOW(gtk_builder_get_object (priv->builder, "dialog_util"));
	dialog = gtk_message_dialog_new (window,
					 GTK_DIALOG_MODAL,
					 GTK_MESSAGE_ERROR,
					 GTK_BUTTONS_CLOSE,
					 "%s", title);
	gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
						  "%s", message);
	gtk_dialog_run (GTK_DIALOG (dialog));
	gtk_widget_destroy (dialog);
}

/**
 * ch_util_activate_cb:
 **/
static void
ch_util_activate_cb (GApplication *application, ChCcmxPrivate *priv)
{
	GtkWindow *window;
	window = GTK_WINDOW (gtk_builder_get_object (priv->builder, "dialog_util"));
	gtk_window_present (window);
}

/**
 * ch_util_close_button_cb:
 **/
static void
ch_util_close_button_cb (GtkWidget *widget, ChCcmxPrivate *priv)
{
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "dialog_util"));
	gtk_widget_destroy (widget);
}

/**
 * ch_util_refresh_device:
 **/
static void
ch_util_refresh_device (ChCcmxPrivate *priv)
{
	ChPointObj *point;
	gboolean ret;
	gdouble sample;
	GError *error = NULL;
	GPtrArray *array;
	guint16 buffer[3694];
	guint i;
	const gchar *title;
	guint32 color;

	/* get 3694 samples from the sram */
	ch_device_queue_read_sram (priv->device_queue,
				   priv->device,
				   0x0000,
				   (guint8 *) buffer,
				   sizeof(buffer));
	ret = ch_device_queue_process (priv->device_queue,
				       CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
				       NULL,
				       &error);
	if (!ret) {
		/* TRANSLATORS: permissions error perhaps? */
		title = _("Failed to get samples from device");
		ch_util_error_dialog (priv, title, error->message);
		g_error_free (error);
		return;
	}

	/* convert to a voltage measurement */
	array = g_ptr_array_new_with_free_func ((GDestroyNotify) ch_point_obj_free);
	for (i = 0; i < 3694; i++) {
		if (buffer[i] > 1024) {
			color = 0x0000df;
			sample = 0.0f;
		} else {
			color = 0xff0000;
			sample = (3.3f * (gdouble) buffer[i]) / 1024.0f;
		}
		point = ch_point_obj_new ();
		point->x = i;
		point->y = sample;
		point->color = color;
		g_ptr_array_add (array, point);
	}
	ch_graph_widget_clear (CH_GRAPH_WIDGET (priv->graph_raw));
	ch_graph_widget_assign (CH_GRAPH_WIDGET (priv->graph_raw),
				 CH_GRAPH_WIDGET_PLOT_LINE,
				 array);
	g_ptr_array_unref (array);
}

/**
 * ch_util_refresh_button_cb:
 **/
static void
ch_util_refresh_button_cb (GtkWidget *widget, ChCcmxPrivate *priv)
{
	/* get the latest from the device */
	ch_util_refresh_device (priv);
}

/**
 * ch_util_get_raw_reading_cb:
 **/
static gboolean
ch_util_get_raw_reading_cb (ChCcmxPrivate *priv)
{
	gboolean ret;
	GError *error = NULL;
	guint32 readings;
	const gchar *title;
	gint integral_time;
	GtkWidget *widget;

	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "spinbutton_integral_time"));
	integral_time = gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (widget));

	/* get 3694 samples from the sram */
	ch_device_queue_set_integral_time (priv->device_queue,
					   priv->device,
					   integral_time);
	ch_device_queue_take_reading_raw (priv->device_queue,
					  priv->device,
					  &readings);
	ret = ch_device_queue_process (priv->device_queue,
				       CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
				       NULL,
				       &error);
	if (!ret) {
		/* TRANSLATORS: permissions error perhaps? */
		title = _("Failed to get raw reading");
		ch_util_error_dialog (priv, title, error->message);
		g_error_free (error);
		return FALSE;
	}
	g_debug ("refreshing values");
	ch_util_refresh_device (priv);
	return TRUE;
}

/**
 * ch_util_auto_update_changed_cb:
 **/
static void
ch_util_auto_update_changed_cb (GObject *object, GParamSpec *pspec, ChCcmxPrivate *priv)
{
	gboolean ret;
	ret = gtk_switch_get_active (GTK_SWITCH (object));

	if (!ret) {
		if (priv->timer_id != 0)
			g_source_remove (priv->timer_id);
		priv->timer_id = 0;
		return;
	}
	if (priv->timer_id == 0)
		priv->timer_id = g_timeout_add (3000, (GSourceFunc) ch_util_get_raw_reading_cb, priv);
}

/**
 * ch_util_got_device:
 **/
static void
ch_util_got_device (ChCcmxPrivate *priv)
{
	const gchar *title;
	gboolean ret;
	GError *error = NULL;

	/* fake device */
	if (g_getenv ("COLORHUG_EMULATE") != NULL)
		goto fake_device;

	/* open device */
	ret = ch_device_open (priv->device, &error);
	if (!ret) {
		/* TRANSLATORS: permissions error perhaps? */
		title = _("Failed to open device");
		ch_util_error_dialog (priv, title, error->message);
		g_error_free (error);
		return;
	}
fake_device:
	/* update the UI */
	ch_util_refresh_device (priv);
}

/**
 * ch_util_get_fake_device:
 **/
static GUsbDevice *
ch_util_get_fake_device (ChCcmxPrivate *priv)
{
	GPtrArray *array;
	GUsbDevice *device = NULL;

	/* just return the first device */
	array = g_usb_device_list_get_devices (priv->device_list);
	if (array->len == 0)
		goto out;
	device = g_object_ref (g_ptr_array_index (array, 0));
out:
	g_ptr_array_unref (array);
	return device;
}

/**
 * ch_util_add_graphs:
 **/
static void
ch_util_add_graphs (ChCcmxPrivate *priv)
{
	GtkBox *box;

	/* raw */
	box = GTK_BOX (gtk_builder_get_object (priv->builder, "box_graph_raw"));
	priv->graph_raw = ch_graph_widget_new ();
	g_object_set (priv->graph_raw,
		      "type-x", CH_GRAPH_WIDGET_TYPE_UNKNOWN,
		      "type-y", CH_GRAPH_WIDGET_TYPE_VOLTAGE,
		      "start-x", 0.f,
		      "stop-x", 3694.f,
		      "start-y", 0.f,
		      "stop-y", 3.3f,
		      "use-grid", TRUE,
		      NULL);
	gtk_box_pack_start (box, priv->graph_raw, TRUE, TRUE, 0);
	gtk_widget_set_margin_top (priv->graph_raw, 6);
	gtk_widget_set_margin_left (priv->graph_raw, 12);
	gtk_widget_set_margin_right (priv->graph_raw, 12);
	gtk_widget_set_size_request (priv->graph_raw, 800, 450);
	gtk_widget_show (priv->graph_raw);

	/* dark calibration */
	box = GTK_BOX (gtk_builder_get_object (priv->builder, "box_graph_dark_cal"));
	priv->graph_dark_cal = ch_graph_widget_new ();
	g_object_set (priv->graph_dark_cal,
		      "type-x", CH_GRAPH_WIDGET_TYPE_UNKNOWN,
		      "type-y", CH_GRAPH_WIDGET_TYPE_FACTOR,
		      "start-x", 0.f,
		      "stop-x", 3694.f,
		      "start-y", 0.f,
		      "stop-y", 1.f,
		      "use-grid", TRUE,
		      NULL);
	gtk_box_pack_start (box, priv->graph_dark_cal, TRUE, TRUE, 0);
	gtk_widget_set_margin_top (priv->graph_raw, 6);
	gtk_widget_set_margin_left (priv->graph_dark_cal, 12);
	gtk_widget_set_margin_right (priv->graph_dark_cal, 12);
	gtk_widget_set_size_request (priv->graph_dark_cal, 800, 450);
	gtk_widget_show (priv->graph_dark_cal);

	/* color match function */
	box = GTK_BOX (gtk_builder_get_object (priv->builder, "box_graph_cmf"));
	priv->graph_cmf = ch_graph_widget_new ();
	g_object_set (priv->graph_cmf,
		      "type-x", CH_GRAPH_WIDGET_TYPE_WAVELENGTH,
		      "type-y", CH_GRAPH_WIDGET_TYPE_FACTOR,
		      "start-x", 300.f,
		      "stop-x", 800.f,
		      "start-y", 0.f,
		      "stop-y", 4.f,
		      "use-grid", TRUE,
		      NULL);
	gtk_box_pack_start (box, priv->graph_cmf, TRUE, TRUE, 0);
	gtk_widget_set_margin_top (priv->graph_raw, 6);
	gtk_widget_set_margin_left (priv->graph_cmf, 12);
	gtk_widget_set_margin_right (priv->graph_cmf, 12);
	gtk_widget_set_size_request (priv->graph_cmf, 800, 450);
	gtk_widget_show (priv->graph_cmf);

	/* temperature compenstion */
	box = GTK_BOX (gtk_builder_get_object (priv->builder, "box_graph_temp_comp"));
	priv->graph_temp_comp = ch_graph_widget_new ();
	g_object_set (priv->graph_temp_comp,
		      "type-x", CH_GRAPH_WIDGET_TYPE_WAVELENGTH,
		      "type-y", CH_GRAPH_WIDGET_TYPE_FACTOR,
		      "start-x", 300.f,
		      "stop-x", 800.f,
		      "start-y", 0.f,
		      "stop-y", 4.f,
		      "use-grid", TRUE,
		      NULL);
	gtk_box_pack_start (box, priv->graph_temp_comp, TRUE, TRUE, 0);
	gtk_widget_set_margin_top (priv->graph_raw, 6);
	gtk_widget_set_margin_left (priv->graph_temp_comp, 12);
	gtk_widget_set_margin_right (priv->graph_temp_comp, 12);
	gtk_widget_set_size_request (priv->graph_temp_comp, 800, 450);
	gtk_widget_show (priv->graph_temp_comp);

	/* finial result */
	box = GTK_BOX (gtk_builder_get_object (priv->builder, "box_graph_result"));
	priv->graph_result = ch_graph_widget_new ();
	g_object_set (priv->graph_result,
		      "type-x", CH_GRAPH_WIDGET_TYPE_WAVELENGTH,
		      "type-y", CH_GRAPH_WIDGET_TYPE_FACTOR,
		      "start-x", 300.f,
		      "stop-x", 800.f,
		      "start-y", 0.f,
		      "stop-y", 1.f,
		      "use-grid", TRUE,
		      NULL);
	gtk_box_pack_start (box, priv->graph_result, TRUE, TRUE, 0);
	gtk_widget_set_margin_top (priv->graph_raw, 6);
	gtk_widget_set_margin_left (priv->graph_result, 12);
	gtk_widget_set_margin_right (priv->graph_result, 12);
	gtk_widget_set_size_request (priv->graph_result, 800, 450);
	gtk_widget_show (priv->graph_result);
}

/**
 * ch_util_startup_cb:
 **/
static void
ch_util_startup_cb (GApplication *application, ChCcmxPrivate *priv)
{
	GError *error = NULL;
	gint retval;
	GtkWidget *main_window;
	GtkWidget *widget;
	GPtrArray *array;

	/* get UI */
	priv->builder = gtk_builder_new ();
	retval = gtk_builder_add_from_file (priv->builder,
					    CH_DATA "/ch-spectro-util.ui",
					    &error);
	if (retval == 0) {
		g_warning ("failed to load ui: %s",
			   error->message);
		g_error_free (error);
		goto out;
	}

	/* add application specific icons to search path */
	gtk_icon_theme_append_search_path (gtk_icon_theme_get_default (),
					   CH_DATA G_DIR_SEPARATOR_S "icons");

	main_window = GTK_WIDGET (gtk_builder_get_object (priv->builder, "dialog_util"));
	gtk_application_add_window (priv->application, GTK_WINDOW (main_window));
	gtk_widget_set_size_request (main_window, 400, 100);

	/* Hide window first so that the dialogue resizes itself without redrawing */
	gtk_widget_hide (main_window);

	/* buttons */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_close"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (ch_util_close_button_cb), priv);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_refresh"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (ch_util_refresh_button_cb), priv);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "switch_auto_update"));
	g_signal_connect (widget, "notify::active",
			  G_CALLBACK (ch_util_auto_update_changed_cb), priv);

	/* add graphs */
	ch_util_add_graphs (priv);
{
	ChPointObj *point;
	array = g_ptr_array_new_with_free_func ((GDestroyNotify) ch_point_obj_free);
	point = ch_point_obj_new ();
	point->x = 300;
	point->y = 2.2;
	point->color = 0xff0000;
	g_ptr_array_add (array, point);
	point = ch_point_obj_new ();
	point->x = 600;
	point->y = 1.8;
	point->color = 0xff0000;
	g_ptr_array_add (array, point);
	point = ch_point_obj_new ();
	point->x = 800;
	point->y = 1.2;
	point->color = 0xff0000;
	g_ptr_array_add (array, point);
	ch_graph_widget_assign (CH_GRAPH_WIDGET (priv->graph_result),
				 CH_GRAPH_WIDGET_PLOT_BOTH,
				 array);
	g_ptr_array_unref (array);
}
	/* is the colorhug already plugged in? */
	g_usb_device_list_coldplug (priv->device_list);

	/* emulate a device */
	if (g_getenv ("COLORHUG_EMULATE") != NULL) {
		priv->device = ch_util_get_fake_device (priv);
		ch_util_got_device (priv);
	}

	/* show main UI */
	gtk_widget_show (main_window);
out:
	return;
}

/**
 * ch_util_device_added_cb:
 **/
static void
ch_util_device_added_cb (GUsbDeviceList *list,
			 GUsbDevice *device,
			 ChCcmxPrivate *priv)
{
	g_debug ("Added: %i:%i",
		 g_usb_device_get_vid (device),
		 g_usb_device_get_pid (device));
	if (ch_device_get_mode (device) == CH_DEVICE_MODE_FIRMWARE_SPECTRO) {
		priv->device = g_object_ref (device);
		ch_util_got_device (priv);
	}
}

/**
 * ch_util_device_removed_cb:
 **/
static void
ch_util_device_removed_cb (GUsbDeviceList *list,
			    GUsbDevice *device,
			    ChCcmxPrivate *priv)
{
	g_debug ("Removed: %i:%i",
		 g_usb_device_get_vid (device),
		 g_usb_device_get_pid (device));
	if (ch_device_get_mode (device) == CH_DEVICE_MODE_FIRMWARE ||
	    ch_device_get_mode (device) == CH_DEVICE_MODE_LEGACY) {
		if (priv->device != NULL)
			g_object_unref (priv->device);
		priv->device = NULL;
	}
}

/**
 * ch_util_ignore_cb:
 **/
static void
ch_util_ignore_cb (const gchar *log_domain, GLogLevelFlags log_level,
		   const gchar *message, gpointer user_data)
{
}

/**
 * main:
 **/
int
main (int argc, char **argv)
{
	ChCcmxPrivate *priv;
	gboolean ret;
	gboolean verbose = FALSE;
	GError *error = NULL;
	GOptionContext *context;
	int status = 0;
	const GOptionEntry options[] = {
		{ "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose,
			/* TRANSLATORS: command line option */
			_("Show extra debugging information"), NULL },
		{ NULL}
	};

	setlocale (LC_ALL, "");

	bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	gtk_init (&argc, &argv);

	/* TRANSLATORS: A program to load on CCMX correction matrices
	 * onto the hardware */
	context = g_option_context_new (_("ColorHug CCMX loader"));
	g_option_context_add_group (context, gtk_get_option_group (TRUE));
	g_option_context_add_main_entries (context, options, NULL);
	ret = g_option_context_parse (context, &argc, &argv, &error);
	if (!ret) {
		g_warning ("%s: %s",
			   _("Failed to parse command line options"),
			   error->message);
		g_error_free (error);
	}
	g_option_context_free (context);

	priv = g_new0 (ChCcmxPrivate, 1);
	priv->settings = g_settings_new ("com.hughski.colorhug-client");
	priv->usb_ctx = g_usb_context_new (NULL);
	priv->device_list = g_usb_device_list_new (priv->usb_ctx);
	priv->device_queue = ch_device_queue_new ();
	g_signal_connect (priv->device_list, "device-added",
			  G_CALLBACK (ch_util_device_added_cb), priv);
	g_signal_connect (priv->device_list, "device-removed",
			  G_CALLBACK (ch_util_device_removed_cb), priv);

	/* ensure single instance */
	priv->application = gtk_application_new ("com.hughski.ColorHug.Util", 0);
	g_signal_connect (priv->application, "startup",
			  G_CALLBACK (ch_util_startup_cb), priv);
	g_signal_connect (priv->application, "activate",
			  G_CALLBACK (ch_util_activate_cb), priv);
	/* set verbose? */
	if (verbose) {
		g_setenv ("COLORHUG_VERBOSE", "1", FALSE);
	} else {
		g_log_set_handler (G_LOG_DOMAIN, G_LOG_LEVEL_DEBUG,
				   ch_util_ignore_cb, NULL);
	}

	/* wait */
	status = g_application_run (G_APPLICATION (priv->application), argc, argv);

	g_object_unref (priv->application);
	if (priv->device_list != NULL)
		g_object_unref (priv->device_list);
	if (priv->device_queue != NULL)
		g_object_unref (priv->device_queue);
	if (priv->usb_ctx != NULL)
		g_object_unref (priv->usb_ctx);
	if (priv->builder != NULL)
		g_object_unref (priv->builder);
	if (priv->settings != NULL)
		g_object_unref (priv->settings);
	if (priv->timer_id != 0)
		g_source_remove (priv->timer_id);
	g_free (priv);
	return status;
}
