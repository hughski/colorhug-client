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
	CdClient		*client;
	CdIt8			*it8_ti1;
	CdIt8			*samples;
	ChDeviceQueue		*device_queue;
	GSettings		*settings;
	GtkApplication		*application;
	GtkBuilder		*builder;
	GtkWidget		*graph;
	GtkWidget		*sample_widget;
	GtkWidget		*switch_channels;
	GtkWidget		*switch_pwm;
	GtkWidget		*switch_zoom;
	GUsbContext		*usb_ctx;
	GUsbDevice		*device;
	GHashTable		*results;
} ChRefreshPrivate;

typedef struct {
	CdColorXYZ		*values;
	CdDevice		*device;
	CdIt8			*it8_ti3;
	ChRefreshPrivate	*priv;
	GCancellable		*cancellable;
	GTimer			*measured;
	gchar			*title;
	gchar			*xrandr_id;
	gdouble			 sample_duration;	/* s */
	gdouble			 usb_latency;		/* s */
	guint8			*reading_array;		/* not used (SRAM) */
	guint			 sample_idx;
} ChRefreshMeasureHelper;

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
	ChRefreshMeasureHelper *helper = (ChRefreshMeasureHelper *) user_data;
	CdColorRGB source;

	/* set to black */
	cd_color_rgb_set (&source, 0.f, 0.f, 0.f);
	cd_sample_widget_set_color (CD_SAMPLE_WIDGET (helper->priv->sample_widget), &source);
	g_debug ("hiding patch at %fms",
		 g_timer_elapsed (helper->measured, NULL) * 1000.f);
	return FALSE;
}

/**
 * ch_refresh_sample_set_white_cb:
 **/
static gboolean
ch_refresh_sample_set_white_cb (gpointer user_data)
{
	ChRefreshMeasureHelper *helper = (ChRefreshMeasureHelper *) user_data;
	CdColorRGB source;

	/* set white */
	cd_color_rgb_set (&source, 1.f, 1.f, 1.f);
	cd_sample_widget_set_color (CD_SAMPLE_WIDGET (helper->priv->sample_widget), &source);
	g_debug ("showing patch at %fms",
		 g_timer_elapsed (helper->measured, NULL) * 1000.f);

	/* set a timeout set the patch black again */
	g_timeout_add (100, ch_refresh_sample_set_black_cb, helper);
	return FALSE;
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
ch_refresh_get_data_from_sram (ChRefreshMeasureHelper *helper)
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
	ch_device_queue_read_sram (helper->priv->device_queue,
				   helper->priv->device,
				   0x0000,
				   (guint8 *) buffer,
				   sizeof(buffer));
	ret = ch_device_queue_process (helper->priv->device_queue,
				       CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
				       NULL,
				       &error);
	if (!ret) {
		/* TRANSLATORS: permissions error perhaps? */
		title = _("Failed to get samples from device");
		ch_refresh_error_dialog (helper->priv, title, error->message);
		return;
	}

	/* extract data */
	for (j = 0; j < 3; j++) {
		sp = cd_spectrum_new ();
		cd_spectrum_set_id (sp, ids[j]);
		cd_spectrum_set_start (sp, 0.f);
		cd_spectrum_set_end (sp, helper->sample_duration);
		for (i = 0; i < NR_DATA_POINTS; i++)
			cd_spectrum_add_value (sp, buffer[i * 3 + j]);
		cd_spectrum_normalize_max (sp, 1.f);
		cd_it8_add_spectrum (helper->priv->samples, sp);
		cd_spectrum_free (sp);
	}

}

/**
 * ch_refresh_update_cancel_buttons:
 **/
static void
ch_refresh_update_cancel_buttons (ChRefreshPrivate *priv, gboolean in_progress)
{
	GtkWidget *w;
	w = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_cancel"));
	gtk_widget_set_visible (w, in_progress);
	w = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_refresh"));
	gtk_widget_set_visible (w, !in_progress && priv->device != NULL);
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
 * ch_refresh_update_refresh_rate:
 **/
static void
ch_refresh_update_refresh_rate (ChRefreshPrivate *priv)
{
	GdkFrameClock *frame_clock;
	gdouble refresh_rate;
	gint64 refresh_interval = 0;

	/* query the refresh rate from the frame clock */
	frame_clock = gtk_widget_get_frame_clock (priv->sample_widget);
	gdk_frame_clock_get_refresh_info (frame_clock, 0, &refresh_interval, NULL);
	refresh_rate = (gdouble) G_USEC_PER_SEC / (gdouble) refresh_interval;
	ch_refresh_result_set_refresh (priv->results, refresh_rate);
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
	ch_refresh_update_refresh_rate (priv);

	/* use Y for all measurements */
	sp_tmp = cd_it8_get_spectrum_by_id (priv->samples, "Y");
	if (sp_tmp == NULL)
		return;

	/* show results box */
	w = GTK_WIDGET (gtk_builder_get_object (priv->builder, "box_results"));
	gtk_widget_set_visible (w, TRUE);

	/* set the graph x scale */
	zoom = gtk_switch_get_active (GTK_SWITCH (priv->switch_zoom));
	duration = cd_spectrum_get_resolution (sp_tmp) * (gdouble) NR_DATA_POINTS;
	tmp = zoom ? duration / 5 : duration;
	tmp = ceilf (tmp * 10.f) / 10.f;
	if (zoom) {
		g_object_set (priv->graph,
			      "start-x", tmp,
			      "stop-x", tmp + 0.3f,
			      NULL);
	} else {
		g_object_set (priv->graph,
			      "start-x", 0.f,
			      "stop-x", tmp,
			      NULL);
	}

	/* find rise time (10% -> 90% transition) */
	ret = ch_refresh_get_rise (sp_tmp, &value, &jitter, &error);
	if (ret) {
		_cleanup_free_ gchar *str = NULL;
		str = g_strdup_printf ("<b>%.0fms</b> ±%.1fms",
				       value * 1000.f, jitter * 1000.f);
		ch_refresh_result_add (priv->results, "label_rise", str);
	} else {
		ch_refresh_result_add (priv->results, "label_rise", error->message);
		g_clear_error (&error);
	}

	/* find rise time (10% -> 90% transition) */
	ret = ch_refresh_get_fall (sp_tmp, &value, &jitter, &error);
	if (ret) {
		_cleanup_free_ gchar *str = NULL;
		str = g_strdup_printf ("<b>%.0fms</b> ±%.1fms",
				       value * 1000.f, jitter * 1000.f);
		ch_refresh_result_add (priv->results, "label_fall", str);
	} else {
		ch_refresh_result_add (priv->results, "label_fall", error->message);
		g_clear_error (&error);
	}

	/* find display latency */
	ret = ch_refresh_get_input_latency (sp_tmp, &value, &jitter, &error);
	if (ret) {
		_cleanup_free_ gchar *str = NULL;
		str = g_strdup_printf ("<b>%.0fms</b> ±%.1fms",
				       value * 1000.f, jitter * 1000.f);
		ch_refresh_result_add (priv->results, "label_display_latency", str);
	} else {
		ch_refresh_result_add (priv->results, "label_display_latency", error->message);
		g_clear_error (&error);
	}

	/* render BW/RGB graphs */
	ch_refresh_update_graph (priv);
}

/**
 * ch_refresh_update_page:
 **/
static void
ch_refresh_update_page (ChRefreshPrivate *priv, gboolean is_results)
{
	CdColorRGB source;
	GtkWidget *w;

	w = GTK_WIDGET (gtk_builder_get_object (priv->builder, "stack_refresh"));
	gtk_stack_set_visible_child_name (GTK_STACK (w), is_results ? "results" : "measure");
	w = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_back"));
	gtk_widget_set_visible (w, is_results);
	w = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_graph_settings"));
	gtk_widget_set_visible (w, is_results);
	w = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_back"));
	gtk_widget_set_visible (w, is_results);
	w = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_refresh"));
	gtk_widget_set_visible (w, !is_results && priv->device != NULL);
	gtk_widget_set_visible (priv->graph, is_results);

	/* make the window as small as possible */
	w = GTK_WIDGET (gtk_builder_get_object (priv->builder, "dialog_refresh"));
	gtk_window_resize (GTK_WINDOW (w), 100, 100);

	/* set to initial color */
	cd_color_rgb_set (&source, 0.5f, 0.5f, 0.5f);
	cd_sample_widget_set_color (CD_SAMPLE_WIDGET (priv->sample_widget), &source);
}

/**
 * ch_refresh_measure_helper_free:
 **/
static void
ch_refresh_measure_helper_free (ChRefreshMeasureHelper *helper)
{
	GtkWidget *w;

	/* set the title */
	w = GTK_WIDGET (gtk_builder_get_object (helper->priv->builder, "label_display_title"));
	gtk_widget_set_visible (w, helper->title != NULL);
	gtk_label_set_label (GTK_LABEL (w), helper->title);

	/* free the helper */
	if (helper->device != NULL)
		g_object_unref (helper->device);
	g_object_unref (helper->cancellable);
	g_object_unref (helper->it8_ti3);
	g_timer_destroy (helper->measured);
	g_free (helper->reading_array);
	g_free (helper->title);
	g_free (helper->values);
	g_free (helper->xrandr_id);
	g_free (helper);
}

static void ch_refresh_ti3_show_patch (ChRefreshMeasureHelper *helper);

/**
 * ch_refresh_find_colord_icc_profile:
 **/
static GFile *
ch_refresh_find_colord_icc_profile (const gchar *filename)
{
	const gchar * const *dirs;
	guint i;

	dirs = g_get_system_data_dirs ();
	for (i = 0; dirs[i] != NULL; i++) {
		_cleanup_free_ gchar *tmp = NULL;
		tmp = g_build_filename (dirs[i],
					"color",
					"icc",
					"colord",
					filename,
					NULL);
		if (g_file_test (tmp, G_FILE_TEST_EXISTS))
			return g_file_new_for_path (tmp);
	}
	return NULL;
}

/**
 * ch_refresh_update_coverage:
 **/
static void
ch_refresh_update_coverage (ChRefreshMeasureHelper *helper)
{
	CdColorXYZ *tmp;
	CdColorYxy blue;
	CdColorYxy green;
	CdColorYxy red;
	CdColorYxy white;
	gboolean ret;
	gdouble coverage_adobergb = -1.f;
	gdouble coverage_srgb = -1.f;
	gdouble gamma_y;
	_cleanup_error_free_ GError *error = NULL;
	_cleanup_object_unref_ CdIcc *icc_adobergb = NULL;
	_cleanup_object_unref_ CdIcc *icc = NULL;
	_cleanup_object_unref_ CdIcc *icc_srgb = NULL;
	_cleanup_object_unref_ GFile *file_adobergb = NULL;
	_cleanup_object_unref_ GFile *file_srgb = NULL;

	/* convert to Yxy */
	tmp = cd_it8_get_xyz_for_rgb (helper->it8_ti3, 1.f, 0.f, 0.f, 0.01f);
	cd_color_xyz_to_yxy (tmp, &red);
	tmp = cd_it8_get_xyz_for_rgb (helper->it8_ti3, 0.f, 1.f, 0.f, 0.01f);
	cd_color_xyz_to_yxy (tmp, &green);
	tmp = cd_it8_get_xyz_for_rgb (helper->it8_ti3, 0.f, 0.f, 1.f, 0.01f);
	cd_color_xyz_to_yxy (tmp, &blue);
	tmp = cd_it8_get_xyz_for_rgb (helper->it8_ti3, 1.f, 1.f, 1.f, 0.01f);
	cd_color_xyz_to_yxy (tmp, &white);

	/* estimate gamma */
	ret = cd_it8_utils_calculate_gamma (helper->it8_ti3, &gamma_y, &error);
	if (!ret) {
		g_warning ("failed to calculate gamma: %s", error->message);
		goto out;
	}
	ch_refresh_result_set_gamma (helper->priv->results, gamma_y);

	/* create virtual profile */
	icc = cd_icc_new ();
	ret = cd_icc_create_from_edid (icc, gamma_y, &red, &green, &blue, &white, &error);
	if (!ret) {
		g_warning ("failed to create virtual profile: %s", error->message);
		goto out;
	}

	/* load sRGB */
	icc_srgb = cd_icc_new ();
	file_srgb = ch_refresh_find_colord_icc_profile ("sRGB.icc");
	if (file_srgb == NULL) {
		g_warning ("failed to find sRGB");
		goto out;
	}
	ret = cd_icc_load_file (icc_srgb, file_srgb,
				CD_ICC_LOAD_FLAGS_NONE, NULL, &error);
	if (!ret) {
		g_warning ("failed to load sRGB: %s", error->message);
		goto out;
	}

	/* load AdobeRGB */
	icc_adobergb = cd_icc_new ();
	file_adobergb = ch_refresh_find_colord_icc_profile ("AdobeRGB1998.icc");
	if (file_adobergb == NULL) {
		g_warning ("failed to find AdobeRGB");
		goto out;
	}
	ret = cd_icc_load_file (icc_adobergb, file_adobergb,
				CD_ICC_LOAD_FLAGS_NONE, NULL, &error);
	if (!ret) {
		g_warning ("failed to load AdobeRGB: %s", error->message);
		goto out;
	}

	/* calculate coverage */
	ret = cd_icc_utils_get_coverage (icc_srgb, icc,
					 &coverage_srgb, &error);
	if (!ret) {
		g_warning ("failed to calc sRGB coverage: %s", error->message);
		goto out;
	}
	ret = cd_icc_utils_get_coverage (icc_adobergb, icc,
					 &coverage_adobergb, &error);
	if (!ret) {
		g_warning ("failed to calc AdobeRGB coverage: %s", error->message);
		goto out;
	}
out:
	ch_refresh_result_set_srgb (helper->priv->results, coverage_srgb);
	ch_refresh_result_set_adobergb (helper->priv->results, coverage_adobergb);
}

/**
 * ch_refresh_update_labels_from_results:
 **/
static void
ch_refresh_update_labels_from_results (GtkBuilder *builder, GHashTable *results)
{
	GList *l;
	GtkWidget *w;
	const gchar *key;
	const gchar *value;
	_cleanup_list_free_ GList *keys = NULL;

	keys = g_hash_table_get_keys (results);
	for (l = keys; l != NULL; l = l->next) {
		key = l->data;
		if (!g_str_has_prefix (key, "label_"))
			continue;
		w = GTK_WIDGET (gtk_builder_get_object (builder, key));
		value = g_hash_table_lookup (results, key);
		gtk_label_set_markup (GTK_LABEL (w), value != NULL ? value : _("Unknown"));
	}
}

/**
 * ch_refresh_ti3_take_readings_cb:
 **/
static void
ch_refresh_ti3_take_readings_cb (GObject *source, GAsyncResult *res, gpointer user_data)
{
	CdColorRGB rgb;
	CdColorXYZ *tmp;
	ChRefreshMeasureHelper *helper = (ChRefreshMeasureHelper *) user_data;
	ChRefreshPrivate *priv = helper->priv;
	_cleanup_error_free_ GError *error = NULL;

	/* get result */
	if (!ch_device_queue_process_finish (CH_DEVICE_QUEUE (source), res, &error)) {
		g_warning ("failed to get measurement: %s", error->message);
		ch_refresh_measure_helper_free (helper);
		return;
	}

	/* copy to ti3 file */
	cd_it8_get_data_item (helper->priv->it8_ti1, helper->sample_idx, &rgb, NULL);
	cd_it8_add_data (helper->it8_ti3, &rgb, &helper->values[helper->sample_idx]);

	/* last patch */
	if (++helper->sample_idx >= cd_it8_get_data_size (priv->it8_ti1)) {
		/* calculate the native cct using the white patch */
		tmp = cd_it8_get_xyz_for_rgb (helper->it8_ti3, 1.f, 1.f, 1.f, 0.01f);
		ch_refresh_result_set_cct (priv->results, cd_color_xyz_to_cct (tmp));
		ch_refresh_result_set_lux_white (priv->results, tmp->Y);
		tmp = cd_it8_get_xyz_for_rgb (helper->it8_ti3, 0.f, 0.f, 0.f, 0.01f);
		ch_refresh_result_set_lux_black (priv->results, tmp->Y);
		ch_refresh_update_coverage (helper);
		ch_refresh_get_data_from_sram (helper);
		ch_refresh_update_ui (priv);
		ch_refresh_update_labels_from_results (priv->builder, priv->results);
		ch_refresh_update_cancel_buttons (priv, FALSE);
		ch_refresh_update_page (priv, TRUE);
		ch_refresh_measure_helper_free (helper);
		return;
	}

	/* show another patch */
	ch_refresh_ti3_show_patch (helper);
}

/**
 * ch_refresh_ti3_wait_for_patch_cb:
 **/
static gboolean
ch_refresh_ti3_wait_for_patch_cb (gpointer user_data)
{
	ChRefreshMeasureHelper *helper = (ChRefreshMeasureHelper *) user_data;

	/* take a reading */
	ch_device_queue_take_readings_xyz (helper->priv->device_queue,
					   helper->priv->device,
					   0,
					   &helper->values[helper->sample_idx]);
	ch_device_queue_process_async (helper->priv->device_queue,
				       CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
				       NULL,
				       ch_refresh_ti3_take_readings_cb,
				       helper);
	return FALSE;
}

/**
 * ch_refresh_ti3_show_patch:
 **/
static void
ch_refresh_ti3_show_patch (ChRefreshMeasureHelper *helper)
{
	CdColorRGB rgb;
	CdSampleWidget *w;

	cd_it8_get_data_item (helper->priv->it8_ti1, helper->sample_idx, &rgb, NULL);
	w = CD_SAMPLE_WIDGET (helper->priv->sample_widget);
	cd_sample_widget_set_color (w, &rgb);
	g_timeout_add (200, ch_refresh_ti3_wait_for_patch_cb, helper);
}

/**
 * ch_refresh_take_reading_array_cb:
 **/
static void
ch_refresh_take_reading_array_cb (GObject *source, GAsyncResult *res, gpointer data)
{
	ChRefreshMeasureHelper *helper = (ChRefreshMeasureHelper *) data;
	const gchar *title;
	_cleanup_error_free_ GError *error = NULL;

	/* check success */
	if (!ch_device_queue_process_finish (helper->priv->device_queue, res, &error)) {
		/* TRANSLATORS: permissions error perhaps? */
		title = _("Failed to get samples from device");
		ch_refresh_error_dialog (helper->priv, title, error->message);
		return;
	}

	/* calculate how long each sample took */
	helper->sample_duration = g_timer_elapsed (helper->measured, NULL) - helper->usb_latency;
	g_debug ("taking sample took %.2fs", helper->sample_duration);
	g_debug ("each sample took %.2fms", (helper->sample_duration / (gdouble) NR_DATA_POINTS) * 1000);

	/* measure the color performance of the display */
	ch_refresh_ti3_show_patch (helper);
}

/**
 * ch_refresh_update_usb_latency:
 **/
static void
ch_refresh_update_usb_latency (ChRefreshMeasureHelper *helper)
{
	GtkWidget *w;
	const gchar *title;
	gdouble usb_jitter = 0.f;
	_cleanup_error_free_ GError *error = NULL;
	_cleanup_free_ gchar *usb_jitter_str = NULL;
	_cleanup_free_ gchar *usb_latency_str = NULL;

	/* measure new USB values */
	if (!ch_refresh_get_usb_speed (helper->priv, &helper->usb_latency, &usb_jitter, &error)) {
		/* TRANSLATORS: permissions error perhaps? */
		title = _("Failed to calculate USB latency");
		ch_refresh_error_dialog (helper->priv, title, error->message);
		return;
	}

	/* update USB labels */
	w = GTK_WIDGET (gtk_builder_get_object (helper->priv->builder, "label_usb_latency"));
	usb_latency_str = g_strdup_printf ("<b>%.1fms</b> ±%.1fms",
					   helper->usb_latency * 1000,
					   usb_jitter * 1000);
	gtk_label_set_markup (GTK_LABEL (w), usb_latency_str);

	/* update results */
	ch_refresh_result_add (helper->priv->results,
			       "label_usb_latency", usb_latency_str);
}

/**
 * ch_refresh_get_readings_cb:
 **/
static gboolean
ch_refresh_get_readings_cb (gpointer user_data)
{
	ChRefreshMeasureHelper *helper = (ChRefreshMeasureHelper *) user_data;
	guint i;
	_cleanup_error_free_ GError *error = NULL;

	/* do NR_PULSES white flashes, 350ms apart */
	g_idle_add (ch_refresh_sample_set_white_cb, helper);
	for (i = 1; i < NR_PULSES; i++)
		g_timeout_add (NR_PULSE_GAP * i, ch_refresh_sample_set_white_cb, helper);

	/* start taking a reading */
	g_timer_reset (helper->measured);
	ch_device_queue_take_reading_array (helper->priv->device_queue,
					    helper->priv->device,
					    helper->reading_array);
	ch_device_queue_process_async (helper->priv->device_queue,
				       CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
				       NULL,
				       ch_refresh_take_reading_array_cb,
				       helper);
	return FALSE;
}

/**
 * ch_refresh_get_readings:
 **/
static void
ch_refresh_get_readings (ChRefreshMeasureHelper *helper)
{
	CdColorRGB source;

	/* set to black then start readings */
	cd_color_rgb_set (&source, 0.f, 0.f, 0.f);
	cd_sample_widget_set_color (CD_SAMPLE_WIDGET (helper->priv->sample_widget), &source);
	g_timeout_add (200, ch_refresh_get_readings_cb, helper);
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
 * ch_refresh_button_back_cb:
 **/
static void
ch_refresh_button_back_cb (GtkWidget *widget, ChRefreshPrivate *priv)
{
	ch_refresh_update_page (priv, FALSE);
}

/**
 * ch_refresh_cancel_cb:
 **/
static void
ch_refresh_cancel_cb (GtkWidget *widget, ChRefreshPrivate *priv)
{
	g_warning ("cancelling");
	ch_refresh_update_cancel_buttons (priv, FALSE);
}

/**
 * ch_refresh_find_xrandr_id:
 **/
static gchar *
ch_refresh_find_xrandr_id (GdkWindow *window)
{
	GdkScreen *screen;
	gint monitor_num;

	screen = gdk_window_get_screen (window);
	monitor_num = gdk_screen_get_monitor_at_window (screen, window);
	return gdk_screen_get_monitor_plug_name (screen, monitor_num);
}

/**
 * ch_refresh_device_profiling_inhibit_cb:
 **/
static void
ch_refresh_device_profiling_inhibit_cb (GObject *source, GAsyncResult *res, gpointer user_data)
{
	ChRefreshMeasureHelper *helper = (ChRefreshMeasureHelper *) user_data;
	_cleanup_error_free_ GError *error = NULL;

	/* get result, but it's no huge problem if it fails */
	if (!cd_device_profiling_inhibit_finish (CD_DEVICE (source), res, &error))
		g_debug ("Failed to inhibit device: %s", error->message);

	/* hurrah! we can start measuring */
	ch_refresh_get_readings (helper);
}

/**
 * ch_refresh_device_connect_cb:
 **/
static void
ch_refresh_device_connect_cb (GObject *source, GAsyncResult *res, gpointer user_data)
{
	ChRefreshMeasureHelper *helper = (ChRefreshMeasureHelper *) user_data;
	_cleanup_error_free_ GError *error = NULL;

	/* get result */
	if (!cd_device_connect_finish (CD_DEVICE (source), res, &error)) {
		g_warning ("Failed to connect to device: %s", error->message);
		/* not fatal */
		ch_refresh_get_readings (helper);
		return;
	}
	helper->title = g_strdup_printf ("%s %s",
					 cd_device_get_vendor (helper->device),
					 cd_device_get_model (helper->device));
	cd_device_profiling_inhibit (helper->device,
				     helper->cancellable,
				     ch_refresh_device_profiling_inhibit_cb,
				     helper);

	/* save results */
	ch_refresh_result_add (helper->priv->results, "title", helper->title);
}

/**
 * ch_refresh_colord_find_device_cb:
 **/
static void
ch_refresh_colord_find_device_cb (GObject *source, GAsyncResult *res, gpointer user_data)
{
	ChRefreshMeasureHelper *helper = (ChRefreshMeasureHelper *) user_data;
	_cleanup_error_free_ GError *error = NULL;

	/* get result */
	helper->device = cd_client_find_device_by_property_finish (CD_CLIENT (source), res, &error);
	if (helper->device == NULL) {
		/* not fatal */
		g_warning ("Failed to find device %s: %s",
			   helper->xrandr_id, error->message);
		ch_refresh_get_readings (helper);
		return;
	}
	cd_device_connect (helper->device, helper->cancellable,
			   ch_refresh_device_connect_cb, helper);
}

/**
 * ch_refresh_refresh_button_cb:
 **/
static void
ch_refresh_refresh_button_cb (GtkWidget *widget, ChRefreshPrivate *priv)
{
	ChRefreshMeasureHelper *helper;

	/* get the display name for the current window */
	helper = g_new0 (ChRefreshMeasureHelper, 1);
	helper->cancellable = g_cancellable_new ();
	helper->measured = g_timer_new ();
	helper->priv = priv;
	helper->sample_idx = 0;
	helper->reading_array = g_new0 (guint8, 30);
	helper->it8_ti3 = cd_it8_new_with_kind (CD_IT8_KIND_TI3);
	helper->values = g_new0 (CdColorXYZ, cd_it8_get_data_size (helper->priv->it8_ti1));
	helper->xrandr_id = ch_refresh_find_xrandr_id (gtk_widget_get_window (widget));
	if (cd_client_get_connected (priv->client)) {
		cd_client_find_device_by_property (priv->client,
						   "XRANDR_name", helper->xrandr_id,
						   helper->cancellable,
						   ch_refresh_colord_find_device_cb, helper);
	} else {
		ch_refresh_get_readings (helper);
	}

	/* get the latest from the device */
	ch_refresh_update_cancel_buttons (priv, TRUE);
	ch_refresh_update_usb_latency (helper);
}

/**
 * ch_refresh_update_title:
 **/
static void
ch_refresh_update_title (ChRefreshPrivate *priv, const gchar *filename)
{
	GtkWidget *w;
	_cleanup_free_ gchar *title = NULL;

	if (filename == NULL) {
		title = g_strdup ("ColorHug Display Analysis");
	} else {
		_cleanup_free_ gchar *basename = NULL;
		basename = g_path_get_basename (filename);
		title = g_strdup_printf ("ColorHug Display Analysis — %s", basename);
	}
	w = GTK_WIDGET (gtk_builder_get_object (priv->builder, "dialog_refresh"));
	gtk_window_set_title (GTK_WINDOW (w), title);
	w = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_csd_title"));
	gtk_label_set_label (GTK_LABEL (w), title);
}

/**
 * ch_refresh_export_html_file:
 **/
static gboolean
ch_refresh_export_html_file (ChRefreshPrivate *priv, const gchar *filename, GError **error)
{
	GString *html;
	GtkAllocation size;
	guint i;
	const gchar *tmp;
	_cleanup_free_ gchar *svg_data = NULL;
	const gchar *keys[] = { "label_display_latency",	_("Display"),
				"label_rise",			_("Black-to-White"),
				"label_fall",			_("White-to-Black"),
				"label_usb_latency",		_("USB"),
				"label_refresh",		_("Refresh Rate"),
				"label_cct",			_("Color Temperature"),
				"label_lux_white",		_("White Luminance"),
				"label_lux_black",		_("Black Luminance"),
				"label_coverage_srgb",		_("sRGB Coverage"),
				"label_coverage_adobergb",	_("AdobeRGB Coverage"),
				"label_gamma",			_("Native Gamma"),
				NULL };

	/* write header */
	html = g_string_new ("");
	g_string_append (html, "<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.0 "
			       "Transitional//EN\" "
			       "\"http://www.w3.org/TR/xhtml1/DTD/xhtml1-transitional.dtd\">\n");
	g_string_append (html, "<html xmlns=\"http://www.w3.org/1999/xhtml\">\n");
	g_string_append (html, "<head>\n");
	g_string_append (html, "<meta http-equiv=\"Content-Type\" content=\"text/html; "
			       "charset=UTF-8\" />\n");
	tmp = g_hash_table_lookup (priv->results, "title");
	if (tmp == NULL)
		tmp = filename;
	g_string_append_printf (html, "<title>%s</title>\n", tmp);

	/* write CSS */
	g_string_append (html, "<style>\n");
	g_string_append (html, "#results {\n");
	g_string_append (html, "    width: 800px;\n");
	g_string_append (html, "    margin: 0 auto; \n");
	g_string_append (html, "}\n");
	g_string_append (html, "td.key {\n");
	g_string_append (html, "    text-align: right;\n");
	g_string_append (html, "    color: #777777;\n");
	g_string_append (html, "}\n");
	g_string_append (html, "td {\n");
	g_string_append (html, "    padding: 5px;\n");
	g_string_append (html, "}\n");
	g_string_append (html, "body {\n");
	g_string_append (html, "    margin-left: 5%;\n");
	g_string_append (html, "    margin-right: 5%;\n");
	g_string_append (html, "    font-family: 'Lucida Grande', Verdana, Arial, Sans-Serif;\n");
	g_string_append (html, "}\n");
	g_string_append (html, "</style>\n");
	g_string_append (html, "</head>\n");
	g_string_append (html, "<body>\n");

	/* write results */
	g_string_append (html, "<div id=\"results\">\n");
	g_string_append (html, "<h1>Your Score<h1>\n");
	tmp = g_hash_table_lookup (priv->results, "title");
	if (tmp != NULL)
		g_string_append_printf (html, "<h2>%s<h2>\n", tmp);
	g_string_append (html, "</div>\n");
	g_string_append (html, "<div id=\"graph\">\n");
	gtk_widget_get_allocation (priv->graph, &size);
	svg_data = ch_graph_widget_export_to_svg (CH_GRAPH_WIDGET (priv->graph),
						  size.width, size.height);
	g_string_append (html, svg_data);
	g_string_append (html, "</div\n");
	g_string_append (html, "<div id=\"results\">\n");
	g_string_append (html, "<table>\n");
	for (i = 0; keys[i] != NULL; i += 2) {
		tmp = g_hash_table_lookup (priv->results, keys[i]);
		if (tmp == NULL)
			continue;
		g_string_append_printf (html,
					"<tr>"
					"<td class=\"key\">%s</td>"
					"<td class=\"value\">%s</td>"
					"</tr>\n",
					keys[i + 1], tmp);
	}
	g_string_append (html, "</table>\n");
	g_string_append (html, "</div>\n");

	/* write footer */
	g_string_append (html, "</body>\n");
	g_string_append (html, "</html>\n");
	return g_file_set_contents (filename, html->str, -1, NULL);
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
	_cleanup_free_ gchar *filename = NULL;

	w = GTK_WIDGET (gtk_builder_get_object (priv->builder, "dialog_refresh"));
	d = gtk_file_chooser_dialog_new (_("Export results"),
					 GTK_WINDOW (w),
					 GTK_FILE_CHOOSER_ACTION_SAVE,
					 _("Cancel"), GTK_RESPONSE_CANCEL,
					 _("Export"), GTK_RESPONSE_ACCEPT,
					 NULL);
	gtk_file_chooser_set_do_overwrite_confirmation (GTK_FILE_CHOOSER (d), TRUE);
	gtk_file_chooser_set_filename (GTK_FILE_CHOOSER (d), "export.html");
	filter = gtk_file_filter_new ();
	gtk_file_filter_set_name (filter, "HTML files");
	gtk_file_filter_add_pattern (filter, "*.html");
	gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (d), filter);
	if (gtk_dialog_run (GTK_DIALOG (d)) == GTK_RESPONSE_ACCEPT) {
		filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (d));
		if (!ch_refresh_export_html_file (priv, filename, &error)) {
			/* TRANSLATORS: permissions error perhaps? */
			title = _("Failed to get save file");
			ch_refresh_error_dialog (priv, title, error->message);
		} else {
			ch_refresh_update_title (priv, filename);
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
 * ch_refresh_update_ui_for_device:
 **/
static void
ch_refresh_update_ui_for_device (ChRefreshPrivate *priv)
{
	ChDeviceMode mode = CH_DEVICE_MODE_UNKNOWN;
	GtkWidget *w;
	_cleanup_string_free_ GString *msg = g_string_new ("");

	/* get actual device mode */
	if (priv->device != NULL)
		mode = ch_device_get_mode (priv->device);

	/* update UI */
	switch (mode) {
	case CH_DEVICE_MODE_UNKNOWN:
		w = GTK_WIDGET (gtk_builder_get_object (priv->builder, "box_results"));
		gtk_widget_set_visible (w, FALSE);
		w = GTK_WIDGET (gtk_builder_get_object (priv->builder, "image_usb"));
		gtk_widget_set_visible (w, TRUE);
		w = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_refresh"));
		gtk_widget_set_visible (w, FALSE);
		gtk_widget_set_visible (priv->sample_widget, FALSE);
		g_string_append (msg, _("Please connect your ColorHug2"));
		break;
	case CH_DEVICE_MODE_FIRMWARE2:
		w = GTK_WIDGET (gtk_builder_get_object (priv->builder, "box_results"));
		gtk_widget_set_visible (w, TRUE);
		w = GTK_WIDGET (gtk_builder_get_object (priv->builder, "image_usb"));
		gtk_widget_set_visible (w, FALSE);
		w = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_refresh"));
		gtk_widget_set_visible (w, TRUE);
		gtk_widget_set_visible (priv->sample_widget, TRUE);
		g_string_append_printf (msg, "%s\n",
			_("Place your ColorHug in the spot on the left and click "
			  "the blue button to start analysing your display."));
		g_string_append_printf (msg, "%s",
			_("Don't disturb the device while working!"));
		break;
	case CH_DEVICE_MODE_BOOTLOADER2:
		w = GTK_WIDGET (gtk_builder_get_object (priv->builder, "box_results"));
		gtk_widget_set_visible (w, FALSE);
		g_string_append_printf (msg, "%s\n\n",
			_("Please update the firmware on your ColorHug before "
			  "using this application."));
		break;
	default:
		w = GTK_WIDGET (gtk_builder_get_object (priv->builder, "box_results"));
		gtk_widget_set_visible (w, FALSE);
		g_string_append_printf (msg, "%s\n\%s\n",
			_("Device unsupported."),
			_("Please connect your ColorHug2."));
		break;
	}
	w = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_intro"));
	gtk_label_set_label (GTK_LABEL (w), msg->str);

	/* make the window as small as possible */
	w = GTK_WIDGET (gtk_builder_get_object (priv->builder, "dialog_refresh"));
	gtk_window_resize (GTK_WINDOW (w), 100, 100);
}

/**
 * ch_refresh_device_open:
 **/
static void
ch_refresh_device_open (ChRefreshPrivate *priv)
{
	const gchar *title;
	_cleanup_error_free_ GError *error = NULL;

	/* open device */
	if (!ch_device_open (priv->device, &error)) {
		/* TRANSLATORS: permissions error perhaps? */
		title = _("Failed to open device");
		ch_refresh_error_dialog (priv, title, error->message);
		return;
	}

	/* measurement possible */
	ch_refresh_update_ui_for_device (priv);
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
	logo = gtk_icon_theme_load_icon (icon_theme, "colorhug-refresh", 256, 0, NULL);
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
	{ "export", ch_refresh_export_activated_cb, NULL, NULL, NULL },
	{ "quit", ch_refresh_quit_activated_cb, NULL, NULL, NULL },
};

/**
 * ch_refresh_colord_connect_cb:
 **/
static void
ch_refresh_colord_connect_cb (GObject *source, GAsyncResult *res, gpointer user_data)
{
	_cleanup_error_free_ GError *error = NULL;

	/* get result */
	if (!cd_client_connect_finish (CD_CLIENT (source), res, &error)) {
		g_warning ("Failed to connect to colord: %s", error->message);
		return;
	}
}

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
	gtk_widget_set_size_request (main_window, 760, 250);

	/* Hide window first so that the dialogue resizes itself without redrawing */
	gtk_widget_hide (main_window);

	/* buttons */
	w = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_graph_settings"));
	g_signal_connect (w, "clicked",
			  G_CALLBACK (ch_refresh_graph_settings_cb), priv);
	w = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_refresh"));
	g_signal_connect (w, "clicked",
			  G_CALLBACK (ch_refresh_refresh_button_cb), priv);
	w = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_back"));
	g_signal_connect (w, "clicked",
			  G_CALLBACK (ch_refresh_button_back_cb), priv);
	w = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_cancel"));
	g_signal_connect (w, "clicked",
			  G_CALLBACK (ch_refresh_cancel_cb), priv);
	g_signal_connect (priv->switch_zoom, "notify::active",
			  G_CALLBACK (ch_refresh_zoom_changed_cb), priv);
	g_signal_connect (priv->switch_channels, "notify::active",
			  G_CALLBACK (ch_refresh_zoom_changed_cb), priv);
	g_signal_connect (priv->switch_pwm, "notify::active",
			  G_CALLBACK (ch_refresh_zoom_changed_cb), priv);

	/* optionally connect to colord */
	cd_client_connect (priv->client, NULL, ch_refresh_colord_connect_cb, priv);

	/* setup USB image */
	pixbuf = gdk_pixbuf_new_from_resource_at_scale ("/com/hughski/ColorHug/DisplayAnalysis/usb.svg",
							200, -1, TRUE, &error);
	if (pixbuf == NULL) {
		g_warning ("failed to load usb.svg: %s", error->message);
		return;
	}
	w = GTK_WIDGET (gtk_builder_get_object (priv->builder, "image_usb"));
	gtk_image_set_from_pixbuf (GTK_IMAGE (w), pixbuf);

	/* add graph */
	box = GTK_BOX (gtk_builder_get_object (priv->builder, "box_results"));
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
	gtk_widget_set_size_request (priv->graph, 600, 250);
	gtk_widget_set_margin_start (priv->graph, 18);
	gtk_widget_set_margin_end (priv->graph, 18);
	gtk_widget_show (priv->graph);

	/* add sample widget */
	box = GTK_BOX (gtk_builder_get_object (priv->builder, "box_measure"));
	priv->sample_widget = cd_sample_widget_new ();
	gtk_box_pack_start (box, priv->sample_widget, FALSE, FALSE, 0);
	gtk_widget_show (priv->sample_widget);
	gtk_widget_set_size_request (priv->sample_widget, 200, 300);

	/* set grey */
	source.R = 0.7f;
	source.G = 0.7f;
	source.B = 0.7f;
	cd_sample_widget_set_color (CD_SAMPLE_WIDGET (priv->sample_widget), &source);

	/* is the colorhug already plugged in? */
	g_usb_context_enumerate (priv->usb_ctx);

	/* show main UI */
	gtk_widget_show (main_window);

	ch_refresh_update_cancel_buttons (priv, FALSE);
	ch_refresh_update_page (priv, FALSE);
	ch_refresh_update_ui_for_device (priv);
	ch_refresh_update_refresh_rate (priv);
	ch_refresh_update_title (priv, NULL);
}

/**
 * ch_refresh_device_added_cb:
 **/
static void
ch_refresh_device_added_cb (GUsbContext *context,
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
ch_refresh_device_removed_cb (GUsbContext *context,
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
		ch_refresh_update_ui_for_device (priv);
	}
}

/**
 * main:
 **/
int
main (int argc, char **argv)
{
	CdColorRGB rgb;
	ChRefreshPrivate *priv;
	gboolean verbose = FALSE;
	GOptionContext *context;
	guint i;
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
	priv->client = cd_client_new ();
	priv->results = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
	priv->device_queue = ch_device_queue_new ();
	g_signal_connect (priv->usb_ctx, "device-added",
			  G_CALLBACK (ch_refresh_device_added_cb), priv);
	g_signal_connect (priv->usb_ctx, "device-removed",
			  G_CALLBACK (ch_refresh_device_removed_cb), priv);

	/* keep the data loaded in memory */
	priv->samples = cd_it8_new_with_kind (CD_IT8_KIND_CCSS);
	cd_it8_set_originator (priv->samples, "cd-refresh");
	cd_it8_set_title (priv->samples, "Sample Data");
	cd_it8_set_instrument (priv->samples, "ColorHug2");

	/* red, green, blue, black->white */
	priv->it8_ti1 = cd_it8_new_with_kind (CD_IT8_KIND_TI1);
	cd_color_rgb_set (&rgb, 1.f, 0.f, 0.f);
	cd_it8_add_data (priv->it8_ti1, &rgb, NULL);
	cd_color_rgb_set (&rgb, 0.f, 1.f, 0.f);
	cd_it8_add_data (priv->it8_ti1, &rgb, NULL);
	cd_color_rgb_set (&rgb, 0.f, 0.f, 1.f);
	cd_it8_add_data (priv->it8_ti1, &rgb, NULL);
	for (i = 0; i <= 10; i++) {
		gdouble frac = 0.1f * (gdouble) i;
		cd_color_rgb_set (&rgb, frac, frac, frac);
		cd_it8_add_data (priv->it8_ti1, &rgb, NULL);
	}

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
	if (priv->device_queue != NULL)
		g_object_unref (priv->device_queue);
	if (priv->usb_ctx != NULL)
		g_object_unref (priv->usb_ctx);
	if (priv->client != NULL)
		g_object_unref (priv->client);
	if (priv->builder != NULL)
		g_object_unref (priv->builder);
	if (priv->settings != NULL)
		g_object_unref (priv->settings);
	g_object_unref (priv->it8_ti1);
	g_hash_table_unref (priv->results);
	g_free (priv);
	return status;
}
