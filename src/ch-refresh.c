/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2009-2014 Richard Hughes <richard@hughsie.com>
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
#include <colord-gtk.h>
#include <math.h>
#include <gusb.h>
#include <colorhug.h>

#include "ch-cleanup.h"
#include "ch-graph-widget.h"
#include "ch-refresh-utils.h"

typedef struct {
	ChDeviceQueue		*device_queue;
	GSettings		*settings;
	GtkApplication		*application;
	GtkBuilder		*builder;
	GtkWidget		*graph;
	GtkWidget		*sample_widget;
	guint			 timer_id;
	GUsbContext		*usb_ctx;
	GUsbDevice		*device;
	GUsbDeviceList		*device_list;
	GTimer			*measured;
	gdouble			 usb_latency;		/* s */
	gdouble			 sample_duration;	/* s */
	CdIt8			*samples;
	GtkWidget		*switch_zoom;
	GtkWidget		*switch_channels;
	GtkWidget		*switch_pwm;
} ChRefreshPrivate;

/**
 * ch_refresh_error_dialog:
 **/
static void
ch_refresh_error_dialog (ChRefreshPrivate *priv,
			 const gchar *title,
			 const gchar *message)
{
	GtkWindow *window;
	GtkWidget *dialog;

	window = GTK_WINDOW(gtk_builder_get_object (priv->builder, "dialog_refresh"));
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
 * ch_refresh_activate_cb:
 **/
static void
ch_refresh_activate_cb (GApplication *application, ChRefreshPrivate *priv)
{
	GtkWindow *window;
	window = GTK_WINDOW (gtk_builder_get_object (priv->builder, "dialog_refresh"));
	gtk_window_present (window);
}

/**
 * ch_refresh_sample_set_black_cb:
 **/
static gboolean
ch_refresh_sample_set_black_cb (gpointer user_data)
{
	ChRefreshPrivate *priv = (ChRefreshPrivate *) user_data;
	CdColorRGB source;

	/* set to black */
	cd_color_rgb_set (&source, 0.f, 0.f, 0.f);
	cd_sample_widget_set_color (CD_SAMPLE_WIDGET (priv->sample_widget), &source);
	g_debug ("hiding patch at %fms",
		 g_timer_elapsed (priv->measured, NULL) * 1000.f);
	return FALSE;
}

/**
 * ch_refresh_sample_set_white_cb:
 **/
static gboolean
ch_refresh_sample_set_white_cb (gpointer user_data)
{
	ChRefreshPrivate *priv = (ChRefreshPrivate *) user_data;
	CdColorRGB source;

	/* set white */
	cd_color_rgb_set (&source, 1.f, 1.f, 1.f);
	cd_sample_widget_set_color (CD_SAMPLE_WIDGET (priv->sample_widget), &source);
	g_debug ("showing patch at %fms",
		 g_timer_elapsed (priv->measured, NULL) * 1000.f);

	/* set a timeout set the patch black again */
	g_timeout_add (100, ch_refresh_sample_set_black_cb, priv);
	return FALSE;
}

/**
 * ch_refresh_calc_average:
 **/
static gdouble
ch_refresh_calc_average (const gdouble *data, guint data_len)
{
	gdouble tmp = 0.f;
	guint i;
	for (i = 0; i < data_len; i++)
		tmp += data[i];
	return tmp / (gdouble) data_len;
}

/**
 * ch_refresh_calc_jitter:
 **/
static gdouble
ch_refresh_calc_jitter (const gdouble *data, guint data_len)
{
	gdouble ave;
	gdouble jitter = 0.f;
	gdouble tmp;
	guint i;
	ave = ch_refresh_calc_average (data, data_len);
	for (i = 0; i < data_len; i ++) {
		tmp = ABS (data[i] - ave);
		if (tmp > jitter)
			jitter = tmp;
	}
	return jitter;
}

/**
 * ch_refresh_get_usb_speed:
 **/
static gboolean
ch_refresh_get_usb_speed (ChRefreshPrivate *priv,
			  gdouble *latency,
			  gdouble *jitter,
			  GError **error)
{
	gboolean ret;
	gdouble elapsed[10];
	guint8 hw_version;
	guint i;
	_cleanup_timer_destroy_ GTimer *timer = NULL;

	timer = g_timer_new ();
	for (i = 0; i < 10; i ++) {
		g_timer_reset (timer);
		ch_device_queue_get_hardware_version (priv->device_queue,
						      priv->device,
						      &hw_version);
		ret = ch_device_queue_process (priv->device_queue,
					       CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
					       NULL,
					       error);
		if (!ret)
			return FALSE;
		elapsed[i] = g_timer_elapsed (timer, NULL);
	}

	/* calculate average and jitter */
	if (latency != NULL)
		*latency = ch_refresh_calc_average (elapsed, 10);
	if (jitter != NULL)
		*jitter = ch_refresh_calc_jitter (elapsed, 10);
	return TRUE;
}

/**
 * ch_refresh_get_data_from_sram:
 **/
static void
ch_refresh_get_data_from_sram (ChRefreshPrivate *priv)
{
	CdSpectrum *sp;
	const gchar *ids[] = { "X", "Y", "Z", NULL };
	const gchar *title;
	gboolean ret;
	guint16 buffer[4096];
	guint i;
	guint j;
	_cleanup_error_free_ GError *error = NULL;

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
		ch_refresh_error_dialog (priv, title, error->message);
		return;
	}

	/* extract data */
	for (j = 0; j < 3; j++) {
		sp = cd_spectrum_new ();
		cd_spectrum_set_id (sp, ids[j]);
		cd_spectrum_set_start (sp, 0.f);
		cd_spectrum_set_end (sp, priv->sample_duration);
		for (i = 0; i < NR_DATA_POINTS; i++)
			cd_spectrum_add_value (sp, buffer[i * 3 + j]);
		cd_spectrum_normalize_max (sp, 1.f);
		cd_it8_add_spectrum (priv->samples, sp);
		cd_spectrum_free (sp);
	}

}

/**
 * ch_refresh_update_graph:
 **/
static void
ch_refresh_update_graph (ChRefreshPrivate *priv)
{
	CdSpectrum *sp_graph[3] = { NULL, NULL, NULL };
	CdSpectrum *sp_tmp;
	ChPointObj *point;
	const gchar *ids[] = { "X", "Y", "Z", NULL };
	const gchar *title;
	gboolean filter_pwm;
	gdouble tmp;
	guint i;
	guint j;
	_cleanup_error_free_ GError *error = NULL;

	/* optionally remove pwm */
	filter_pwm = gtk_switch_get_active (GTK_SWITCH (priv->switch_pwm));
	for (j = 0; j < 3; j++) {
		sp_tmp = cd_it8_get_spectrum_by_id (priv->samples, ids[j]);
		sp_graph[j] = cd_spectrum_dup (sp_tmp);
		if (filter_pwm && !ch_refresh_remove_pwm (sp_graph[j], &error)) {
			/* TRANSLATORS: PWM is pulse-width-modulation? */
			title = _("Failed to remove PWM");
			ch_refresh_error_dialog (priv, title, error->message);
			return;
		}
		cd_spectrum_normalize_max (sp_graph[j], 1.f);
	}

	ch_graph_widget_clear (CH_GRAPH_WIDGET (priv->graph));
	if (gtk_switch_get_active (GTK_SWITCH (priv->switch_channels))) {
		for (j = 0; j < 3; j++) {
			_cleanup_ptrarray_unref_ GPtrArray *array = NULL;
			array = g_ptr_array_new_with_free_func ((GDestroyNotify) ch_point_obj_free);
			for (i = 0; i < NR_DATA_POINTS; i++) {
				point = ch_point_obj_new ();
				point->x = ((gdouble) i) * cd_spectrum_get_resolution (sp_graph[j]);
				point->y = cd_spectrum_get_value (sp_graph[j], i) * 100.f;
				point->color = 0x0000df << (j * 8);
				g_ptr_array_add (array, point);
			}
			ch_graph_widget_assign (CH_GRAPH_WIDGET (priv->graph),
						 CH_GRAPH_WIDGET_PLOT_LINE,
						 array);
		}
	} else {
		_cleanup_ptrarray_unref_ GPtrArray *array = NULL;
		array = g_ptr_array_new_with_free_func ((GDestroyNotify) ch_point_obj_free);
		for (i = 0; i < NR_DATA_POINTS; i++) {
			/* get maximum value */
			gdouble max = 0.f;
			for (j = 0; j < 3; j++) {
				tmp = cd_spectrum_get_value (sp_graph[j], i);
				if (tmp > max)
					max = tmp;
			}
			point = ch_point_obj_new ();
			point->x = ((gdouble) i) * cd_spectrum_get_resolution (sp_graph[1]);
			point->y = max * 100.f;
			point->color = 0x000000;
			g_ptr_array_add (array, point);
		}
		ch_graph_widget_assign (CH_GRAPH_WIDGET (priv->graph),
					 CH_GRAPH_WIDGET_PLOT_LINE,
					 array);
	}

	/* add trigger lines */
	if (!gtk_switch_get_active (GTK_SWITCH (priv->switch_zoom))) {
		for (j = 1; j < NR_PULSES; j++) {
			_cleanup_ptrarray_unref_ GPtrArray *array = NULL;
			array = g_ptr_array_new_with_free_func ((GDestroyNotify) ch_point_obj_free);

			/* bottom */
			point = ch_point_obj_new ();
			point->x = ((gdouble) j) * (gdouble) NR_PULSE_GAP / 1000.f;
			point->y = 0.f;
			point->color = 0xfff000;
			g_ptr_array_add (array, point);

			/* top */
			point = ch_point_obj_new ();
			point->x = ((gdouble) j) * (gdouble) NR_PULSE_GAP / 1000.f;
			point->y = 100.f;
			point->color = 0xffb000;
			g_ptr_array_add (array, point);
			ch_graph_widget_assign (CH_GRAPH_WIDGET (priv->graph),
						 CH_GRAPH_WIDGET_PLOT_LINE,
						 array);
		}
	}

	/* free spectra */
	for (j = 0; j < 3; j++) {
		if (sp_graph[j] == NULL)
			continue;
		cd_spectrum_free (sp_graph[j]);
	}
}

/**
 * ch_refresh_refresh_rate:
 **/
static void
ch_refresh_refresh_rate (ChRefreshPrivate *priv)
{
	GdkFrameClock *frame_clock;
	GtkWidget *w;
	gdouble refresh_rate;
	gint64 refresh_interval = 0;

	/* query the refresh rate from the frame clock */
	frame_clock = gtk_widget_get_frame_clock (priv->sample_widget);
	gdk_frame_clock_get_refresh_info (frame_clock, 0, &refresh_interval, NULL);
	refresh_rate = (gdouble) G_USEC_PER_SEC / (gdouble) refresh_interval;

	/* update the UI */
	w = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_refresh"));
	if (refresh_rate > 1.f) {
		_cleanup_free_ gchar *str = NULL;
		str = g_strdup_printf ("%.0f Hz", refresh_rate);
		gtk_label_set_label (GTK_LABEL (w), str);
	} else {
		gtk_label_set_label (GTK_LABEL (w), _("Unknown"));
	}
}

/**
 * ch_refresh_update_ui:
 **/
static void
ch_refresh_update_ui (ChRefreshPrivate *priv)
{
	CdSpectrum *sp_tmp;
	gboolean ret;
	gboolean zoom;
	gdouble jitter;
	gdouble tmp;
	gdouble value;
	gdouble duration;
	GtkWidget *w;
	_cleanup_error_free_ GError *error = NULL;

	/* update display refresh rate */
	ch_refresh_refresh_rate (priv);

	/* use Y for all measurements */
	sp_tmp = cd_it8_get_spectrum_by_id (priv->samples, "Y");
	if (sp_tmp == NULL)
		return;

	/* set the graph x scale */
	zoom = gtk_switch_get_active (GTK_SWITCH (priv->switch_zoom));
	duration = cd_spectrum_get_resolution (sp_tmp) * (gdouble) NR_DATA_POINTS;
	tmp = zoom ? duration / 5 : duration;
	tmp = ceilf (tmp * 10.f) / 10.f;
	if (zoom) {
		g_object_set (priv->graph,
			      "start-x", tmp * 2,
			      "stop-x", tmp * 3,
			      NULL);
	} else {
		g_object_set (priv->graph,
			      "start-x", 0.f,
			      "stop-x", tmp,
			      NULL);
	}

	/* find rise time (10% -> 90% transition) */
	ret = ch_refresh_get_rise (sp_tmp, &value, &jitter, &error);
	w = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_rise"));
	if (ret) {
		_cleanup_free_ gchar *str = NULL;
		str = g_strdup_printf ("%.0fms ±%.1fms", value * 1000.f, jitter * 1000.f);
		gtk_label_set_label (GTK_LABEL (w), str);
	} else {
		gtk_label_set_label (GTK_LABEL (w), error->message);
		g_clear_error (&error);
	}

	/* find rise time (10% -> 90% transition) */
	ret = ch_refresh_get_fall (sp_tmp, &value, &jitter, &error);
	w = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_fall"));
	if (ret) {
		_cleanup_free_ gchar *str = NULL;
		str = g_strdup_printf ("%.0fms ±%.1fms", value * 1000.f, jitter * 1000.f);
		gtk_label_set_label (GTK_LABEL (w), str);
	} else {
		gtk_label_set_label (GTK_LABEL (w), error->message);
		g_clear_error (&error);
	}

	/* find display latency */
	ret = ch_refresh_get_input_latency (sp_tmp, &value, &jitter, &error);
	w = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_display_latency"));
	if (ret) {
		_cleanup_free_ gchar *str = NULL;
		str = g_strdup_printf ("%.0fms ±%.1fms", value * 1000.f, jitter * 1000.f);
		gtk_label_set_label (GTK_LABEL (w), str);
	} else {
		gtk_label_set_label (GTK_LABEL (w), error->message);
		g_clear_error (&error);
	}

	/* render BW/RGB graphs */
	ch_refresh_update_graph (priv);
}

/**
 * ch_refresh_get_readings:
 **/
static void
ch_refresh_get_readings_cb (GObject *source, GAsyncResult *res, gpointer data)
{
	ChRefreshPrivate *priv = (ChRefreshPrivate *) data;
	const gchar *title;
	_cleanup_error_free_ GError *error = NULL;

	/* check success */
	if (!ch_device_queue_process_finish (priv->device_queue, res, &error)) {
		/* TRANSLATORS: permissions error perhaps? */
		title = _("Failed to get samples from device");
		ch_refresh_error_dialog (priv, title, error->message);
		return;
	}

	/* calculate how long each sample took */
	priv->sample_duration = g_timer_elapsed (priv->measured, NULL) - priv->usb_latency;
	g_debug ("taking sample took %.2fs", priv->sample_duration);
	g_debug ("each sample took %.2fms", (priv->sample_duration / (gdouble) NR_DATA_POINTS) * 1000);
	ch_refresh_get_data_from_sram (priv);
	ch_refresh_update_ui (priv);
}

/**
 * ch_refresh_get_readings:
 **/
static void
ch_refresh_get_readings (ChRefreshPrivate *priv)
{
	GtkWidget *w;
	const gchar *title;
	gdouble usb_jitter = 0.f;
	guint8 reading_array[30];
	guint i;
	_cleanup_error_free_ GError *error = NULL;
	_cleanup_free_ gchar *usb_jitter_str = NULL;
	_cleanup_free_ gchar *usb_latency_str = NULL;

	/* measure new USB values */
	if (!ch_refresh_get_usb_speed (priv, &priv->usb_latency, &usb_jitter, &error)) {
		/* TRANSLATORS: permissions error perhaps? */
		title = _("Failed to calculate USB latency");
		ch_refresh_error_dialog (priv, title, error->message);
		return;
	}

	/* update USB labels */
	w = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_usb_latency"));
	usb_latency_str = g_strdup_printf ("%.1fms ±%.1fms",
					   priv->usb_latency * 1000,
					   usb_jitter * 1000);
	gtk_label_set_label (GTK_LABEL (w), usb_latency_str);

	/* do NR_PULSES white flashes, 350ms apart */
	g_idle_add (ch_refresh_sample_set_white_cb, priv);
	for (i = 1; i < NR_PULSES; i++)
		g_timeout_add (NR_PULSE_GAP * i, ch_refresh_sample_set_white_cb, priv);

	/* start taking a reading */
	g_timer_reset (priv->measured);
	ch_device_queue_take_reading_array (priv->device_queue,
					    priv->device,
					    reading_array);
	ch_device_queue_process_async (priv->device_queue,
				       CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
				       NULL,
				       ch_refresh_get_readings_cb,
				       priv);
}

/**
 * ch_refresh_graph_settings_cb:
 **/
static void
ch_refresh_graph_settings_cb (GtkWidget *widget, ChRefreshPrivate *priv)
{
	GtkWidget *pop;
	GtkWidget *box;

	/* reclaim */
	if (gtk_widget_get_parent (priv->switch_zoom) != NULL)
		g_object_ref (priv->switch_zoom);
	gtk_widget_unparent (priv->switch_zoom);
	if (gtk_widget_get_parent (priv->switch_channels) != NULL)
		g_object_ref (priv->switch_channels);
	gtk_widget_unparent (priv->switch_channels);
	if (gtk_widget_get_parent (priv->switch_pwm) != NULL)
		g_object_ref (priv->switch_pwm);
	gtk_widget_unparent (priv->switch_pwm);

	/* show settings */
	pop = gtk_popover_new (widget);
	gtk_popover_set_position (GTK_POPOVER (pop), GTK_POS_BOTTOM);
	gtk_container_set_border_width (GTK_CONTAINER (pop), 18);
	box = gtk_grid_new ();
	gtk_grid_set_row_spacing (GTK_GRID (box), 6);
	gtk_grid_set_column_spacing (GTK_GRID (box), 12);
	gtk_grid_attach (GTK_GRID (box),
			 gtk_label_new (_("Show single pulse")),
			 0, 0, 1, 1);
	gtk_grid_attach (GTK_GRID (box), priv->switch_zoom, 1, 0, 1, 1);
	gtk_grid_attach (GTK_GRID (box),
			 gtk_label_new (_("Show channels")),
			 0, 1, 1, 1);
	gtk_grid_attach (GTK_GRID (box), priv->switch_channels, 1, 1, 1, 1);
	gtk_grid_attach (GTK_GRID (box),
			 gtk_label_new (_("Filter backlight")),
			 0, 2, 1, 1);
	gtk_grid_attach (GTK_GRID (box), priv->switch_pwm, 1, 2, 1, 1);
	gtk_container_add (GTK_CONTAINER (pop), box);
	gtk_widget_show_all (pop);
}

/**
 * ch_refresh_refresh_button_cb:
 **/
static void
ch_refresh_refresh_button_cb (GtkWidget *widget, ChRefreshPrivate *priv)
{
	/* get the latest from the device */
	ch_refresh_get_readings (priv);
}

/**
 * ch_refresh_update_title:
 **/
static void
ch_refresh_update_title (ChRefreshPrivate *priv, GFile *file)
{
	GtkWidget *w;
	_cleanup_free_ gchar *title = NULL;

	if (file == NULL) {
		title = g_strdup ("ColorHug Display Analysis");
	} else {
		_cleanup_free_ gchar *basename = NULL;
		basename = g_file_get_basename (file);
		title = g_strdup_printf ("ColorHug Display Analysis — %s", basename);
	}
	w = GTK_WIDGET (gtk_builder_get_object (priv->builder, "dialog_refresh"));
	gtk_window_set_title (GTK_WINDOW (w), title);
}

/**
 * ch_refresh_export_activated_cb:
 **/
static void
ch_refresh_export_activated_cb (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	ChRefreshPrivate *priv = (ChRefreshPrivate *) user_data;
	GtkFileFilter *filter = NULL;
	GtkWidget *d;
	GtkWidget *w;
	const gchar *title;
	_cleanup_error_free_ GError *error = NULL;
	_cleanup_object_unref_ GFile *file = NULL;

	w = GTK_WIDGET (gtk_builder_get_object (priv->builder, "dialog_refresh"));
	d = gtk_file_chooser_dialog_new (_("Export data file"),
					 GTK_WINDOW (w),
					 GTK_FILE_CHOOSER_ACTION_SAVE,
					 _("Cancel"), GTK_RESPONSE_CANCEL,
					 _("Export"), GTK_RESPONSE_ACCEPT,
					 NULL);
	gtk_file_chooser_set_do_overwrite_confirmation (GTK_FILE_CHOOSER (d), TRUE);
	gtk_file_chooser_set_filename (GTK_FILE_CHOOSER (d), "export.ccss");
	filter = gtk_file_filter_new ();
	gtk_file_filter_set_name (filter, "CCSS files");
	gtk_file_filter_add_pattern (filter, "*.ccss");
	gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (d), filter);
	if (gtk_dialog_run (GTK_DIALOG (d)) == GTK_RESPONSE_ACCEPT) {
		file = gtk_file_chooser_get_file (GTK_FILE_CHOOSER (d));
		if (!cd_it8_save_to_file (priv->samples, file, &error)) {
			/* TRANSLATORS: permissions error perhaps? */
			title = _("Failed to get save file");
			ch_refresh_error_dialog (priv, title, error->message);
		} else {
			ch_refresh_update_title (priv, file);
		}
	}
	gtk_widget_destroy (d);
}

/**
 * ch_refresh_normalize_channels:
 **/
static void
ch_refresh_normalize_channels (ChRefreshPrivate *priv)
{
	CdSpectrum *sp;
	const gchar *ids[] = { "X", "Y", "Z", NULL };
	guint j;

	/* normalize all channels */
	for (j = 0; j < 3; j++) {
		sp = cd_it8_get_spectrum_by_id (priv->samples, ids[j]);
		cd_spectrum_normalize_max (sp, 1.f);
	}
}


/**
 * ch_refresh_import_activated_cb:
 **/
static void
ch_refresh_import_activated_cb (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	ChRefreshPrivate *priv = (ChRefreshPrivate *) user_data;
	GtkFileFilter *filter = NULL;
	GtkWidget *d;
	GtkWidget *w;
	const gchar *title;
	_cleanup_error_free_ GError *error = NULL;
	_cleanup_object_unref_ GFile *file = NULL;

	w = GTK_WIDGET (gtk_builder_get_object (priv->builder, "dialog_refresh"));
	d = gtk_file_chooser_dialog_new (_("Import data file"),
					 GTK_WINDOW (w),
					 GTK_FILE_CHOOSER_ACTION_SAVE,
					 _("Cancel"), GTK_RESPONSE_CANCEL,
					 _("Import"), GTK_RESPONSE_ACCEPT,
					 NULL);
	filter = gtk_file_filter_new ();
	filter = gtk_file_filter_new ();
	gtk_file_filter_set_name (filter, "CCSS files");
	gtk_file_filter_add_pattern (filter, "*.ccss");
	gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER (d), TESTDATADIR);
	gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (d), filter);
	if (gtk_dialog_run (GTK_DIALOG (d)) == GTK_RESPONSE_ACCEPT) {
		file = gtk_file_chooser_get_file (GTK_FILE_CHOOSER (d));
		if (!cd_it8_load_from_file (priv->samples, file, &error)) {
			/* TRANSLATORS: permissions error perhaps? */
			title = _("Failed to get import file");
			ch_refresh_error_dialog (priv, title, error->message);
		} else {
			ch_refresh_update_title (priv, file);
			ch_refresh_normalize_channels (priv);
			ch_refresh_update_ui (priv);
		}
	}
	gtk_widget_destroy (d);
}

/**
 * ch_refresh_zoom_changed_cb:
 **/
static void
ch_refresh_zoom_changed_cb (GObject *object, GParamSpec *pspec, ChRefreshPrivate *priv)
{
	ch_refresh_update_ui (priv);
}

/**
 * ch_refresh_device_close:
 **/
static void
ch_refresh_device_close (ChRefreshPrivate *priv)
{
	GtkWidget *w;
	w = GTK_WIDGET (gtk_builder_get_object (priv->builder, "notebook_refresh"));
	gtk_notebook_set_current_page (GTK_NOTEBOOK (w), 1);
}

/**
 * ch_refresh_device_open:
 **/
static void
ch_refresh_device_open (ChRefreshPrivate *priv)
{
	GtkWidget *w;
	const gchar *title;
	_cleanup_error_free_ GError *error = NULL;

	/* open device */
	if (!ch_device_open (priv->device, &error)) {
		/* TRANSLATORS: permissions error perhaps? */
		title = _("Failed to open device");
		ch_refresh_error_dialog (priv, title, error->message);
		return;
	}
	w = GTK_WIDGET (gtk_builder_get_object (priv->builder, "notebook_refresh"));
	gtk_notebook_set_current_page (GTK_NOTEBOOK (w), 0);
}

/**
 * ch_refresh_about_activated_cb:
 **/
static void
ch_refresh_about_activated_cb (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	ChRefreshPrivate *priv = (ChRefreshPrivate *) user_data;
	GList *windows;
	GtkIconTheme *icon_theme;
	GtkWindow *parent = NULL;
	const gchar *authors[] = { "Richard Hughes", NULL };
	const gchar *copyright = "Copyright \xc2\xa9 2014 Richard Hughes";
	_cleanup_object_unref_ GdkPixbuf *logo = NULL;

	windows = gtk_application_get_windows (GTK_APPLICATION (priv->application));
	if (windows)
		parent = windows->data;

	icon_theme = gtk_icon_theme_get_default ();
	logo = gtk_icon_theme_load_icon (icon_theme, "input-gaming", 256, 0, NULL);
	gtk_show_about_dialog (parent,
			       /* TRANSLATORS: this is the title of the about window */
			       "title", _("About ColorHug Display Analysis"),
			       /* TRANSLATORS: this is the application name */
			       "program-name", _("ColorHug Display Analysis"),
			       "authors", authors,
			       "comments", _("Sample the display over time to observe PWM, input latency and refresh artifacts."),
			       "copyright", copyright,
			       "license-type", GTK_LICENSE_GPL_2_0,
			       "logo", logo,
			       "translator-credits", _("translator-credits"),
			       "version", VERSION,
			       NULL);
}

/**
 * ch_refresh_quit_activated_cb:
 **/
static void
ch_refresh_quit_activated_cb (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	ChRefreshPrivate *priv = (ChRefreshPrivate *) user_data;
	g_application_quit (G_APPLICATION (priv->application));
}

static GActionEntry actions[] = {
	{ "about", ch_refresh_about_activated_cb, NULL, NULL, NULL },
	{ "import", ch_refresh_import_activated_cb, NULL, NULL, NULL },
	{ "export", ch_refresh_export_activated_cb, NULL, NULL, NULL },
	{ "quit", ch_refresh_quit_activated_cb, NULL, NULL, NULL },
};

/**
 * ch_refresh_startup_cb:
 **/
static void
ch_refresh_startup_cb (GApplication *application, ChRefreshPrivate *priv)
{
	CdColorRGB source;
	GtkBox *box;
	GtkWidget *main_window;
	GtkWidget *w;
	gint retval;
	_cleanup_error_free_ GError *error = NULL;
	_cleanup_object_unref_ GdkPixbuf *pixbuf = NULL;

	/* add application menu items */
	g_action_map_add_action_entries (G_ACTION_MAP (application),
					 actions, G_N_ELEMENTS (actions),
					 priv);

	/* get UI */
	priv->builder = gtk_builder_new ();
	retval = gtk_builder_add_from_resource (priv->builder,
						"/com/hughski/ColorHug/DisplayAnalysis/ch-refresh.ui",
						&error);
	if (retval == 0) {
		g_warning ("failed to load ui: %s", error->message);
		return;
	}

	main_window = GTK_WIDGET (gtk_builder_get_object (priv->builder, "dialog_refresh"));
	gtk_application_add_window (priv->application, GTK_WINDOW (main_window));
	gtk_widget_set_size_request (main_window, 400, 100);

	/* Hide window first so that the dialogue resizes itself without redrawing */
	gtk_widget_hide (main_window);

	/* buttons */
	w = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_graph_settings"));
	g_signal_connect (w, "clicked",
			  G_CALLBACK (ch_refresh_graph_settings_cb), priv);
	w = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_refresh"));
	g_signal_connect (w, "clicked",
			  G_CALLBACK (ch_refresh_refresh_button_cb), priv);
	g_signal_connect (priv->switch_zoom, "notify::active",
			  G_CALLBACK (ch_refresh_zoom_changed_cb), priv);
	g_signal_connect (priv->switch_channels, "notify::active",
			  G_CALLBACK (ch_refresh_zoom_changed_cb), priv);
	g_signal_connect (priv->switch_pwm, "notify::active",
			  G_CALLBACK (ch_refresh_zoom_changed_cb), priv);

	/* set connect device page */
	w = GTK_WIDGET (gtk_builder_get_object (priv->builder, "notebook_refresh"));
	gtk_notebook_set_current_page (GTK_NOTEBOOK (w), 1);

	/* setup USB image */
	pixbuf = gdk_pixbuf_new_from_resource_at_scale ("/com/hughski/ColorHug/DisplayAnalysis/usb.svg",
							-1, 48, TRUE, &error);
	if (pixbuf == NULL) {
		g_warning ("failed to load usb.svg: %s", error->message);
		return;
	}
	w = GTK_WIDGET (gtk_builder_get_object (priv->builder, "image_usb"));
	gtk_image_set_from_pixbuf (GTK_IMAGE (w), pixbuf);

	/* add graph */
	box = GTK_BOX (gtk_builder_get_object (priv->builder, "box_graph"));
	priv->graph = ch_graph_widget_new ();
	g_object_set (priv->graph,
		      "type-x", CH_GRAPH_WIDGET_TYPE_TIME,
		      "type-y", CH_GRAPH_WIDGET_TYPE_PERCENTAGE,
		      "start-x", 0.f,
		      "stop-x", 2.f,
		      "start-y", 0.f,
		      "stop-y", 100.f,
		      "use-grid", TRUE,
		      NULL);
	gtk_box_pack_start (box, priv->graph, TRUE, TRUE, 0);
	gtk_widget_set_margin_top (priv->graph, 6);
	gtk_widget_set_margin_start (priv->graph, 12);
	gtk_widget_set_margin_end (priv->graph, 12);
	gtk_widget_set_size_request (priv->graph, 800, 450);
	gtk_widget_show (priv->graph);

	/* add sample widget */
	box = GTK_BOX (gtk_builder_get_object (priv->builder, "box_sidebar"));
	priv->sample_widget = cd_sample_widget_new ();
	gtk_box_pack_start (box, priv->sample_widget, TRUE, TRUE, 0);
	gtk_widget_show (priv->sample_widget);

	/* set black */
	source.R = 0.0f;
	source.G = 0.0f;
	source.B = 0.0f;
	cd_sample_widget_set_color (CD_SAMPLE_WIDGET (priv->sample_widget), &source);

	/* is the colorhug already plugged in? */
	g_usb_device_list_coldplug (priv->device_list);

	/* show main UI */
	gtk_widget_show (main_window);

	ch_refresh_refresh_rate (priv);
	ch_refresh_update_title (priv, NULL);
}

/**
 * ch_refresh_device_added_cb:
 **/
static void
ch_refresh_device_added_cb (GUsbDeviceList *list,
			 GUsbDevice *device,
			 ChRefreshPrivate *priv)
{
	g_debug ("Added: %i:%i",
		 g_usb_device_get_vid (device),
		 g_usb_device_get_pid (device));
	if (ch_device_get_mode (device) == CH_DEVICE_MODE_FIRMWARE2) {
		priv->device = g_object_ref (device);
		ch_refresh_device_open (priv);
	}
}

/**
 * ch_refresh_device_removed_cb:
 **/
static void
ch_refresh_device_removed_cb (GUsbDeviceList *list,
			    GUsbDevice *device,
			    ChRefreshPrivate *priv)
{
	g_debug ("Removed: %i:%i",
		 g_usb_device_get_vid (device),
		 g_usb_device_get_pid (device));
	if (ch_device_get_mode (device) == CH_DEVICE_MODE_FIRMWARE2) {
		if (priv->device != NULL)
			g_object_unref (priv->device);
		priv->device = NULL;
		ch_refresh_device_close (priv);
	}
}

/**
 * main:
 **/
int
main (int argc, char **argv)
{
	ChRefreshPrivate *priv;
	gboolean verbose = FALSE;
	GOptionContext *context;
	int status = 0;
	_cleanup_error_free_ GError *error = NULL;
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
	context = g_option_context_new (_("ColorHug Display Analysis"));
	g_option_context_add_group (context, gtk_get_option_group (TRUE));
	g_option_context_add_main_entries (context, options, NULL);
	if (!g_option_context_parse (context, &argc, &argv, &error)) {
		g_warning ("%s: %s", _("Failed to parse command line options"),
			   error->message);
	}
	g_option_context_free (context);

	priv = g_new0 (ChRefreshPrivate, 1);
	priv->settings = g_settings_new ("com.hughski.ColorHug.DisplayAnalysis");
	priv->usb_ctx = g_usb_context_new (NULL);
	priv->measured = g_timer_new ();
	priv->device_list = g_usb_device_list_new (priv->usb_ctx);
	priv->device_queue = ch_device_queue_new ();
	g_signal_connect (priv->device_list, "device-added",
			  G_CALLBACK (ch_refresh_device_added_cb), priv);
	g_signal_connect (priv->device_list, "device-removed",
			  G_CALLBACK (ch_refresh_device_removed_cb), priv);

	/* keep the data loaded in memory */
	priv->samples = cd_it8_new_with_kind (CD_IT8_KIND_CCSS);
	cd_it8_set_originator (priv->samples, "cd-refresh");
	cd_it8_set_title (priv->samples, "Sample Data");
	cd_it8_set_instrument (priv->samples, "ColorHug2");

	/* keep these local as they get reparented to the popover */
	priv->switch_zoom = gtk_switch_new ();
	priv->switch_channels = gtk_switch_new ();
	priv->switch_pwm = gtk_switch_new ();
	g_settings_bind (priv->settings, "graph-zoom-enable",
			 priv->switch_zoom, "active",
			 G_SETTINGS_BIND_DEFAULT);
	g_settings_bind (priv->settings, "graph-show-channels",
			 priv->switch_channels, "active",
			 G_SETTINGS_BIND_DEFAULT);
	g_settings_bind (priv->settings, "graph-pwm-fixup",
			 priv->switch_pwm, "active",
			 G_SETTINGS_BIND_DEFAULT);

	/* ensure single instance */
	priv->application = gtk_application_new ("com.hughski.ColorHug.DisplayAnalysis", 0);
	g_signal_connect (priv->application, "startup",
			  G_CALLBACK (ch_refresh_startup_cb), priv);
	g_signal_connect (priv->application, "activate",
			  G_CALLBACK (ch_refresh_activate_cb), priv);
	/* set verbose? */
	if (verbose)
		g_setenv ("G_MESSAGES_DEBUG", "ChClient", FALSE);

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
	if (priv->measured != NULL)
		g_timer_destroy (priv->measured);
	g_free (priv);
	return status;
}
