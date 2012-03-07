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

typedef struct {
	GtkBuilder	*builder;
	GtkApplication	*application;
	guint8		 leds_old;
	CdColorRGB	 value_max;
	ChDeviceQueue	*device_queue;
	GUsbDevice	*device;
} ChUtilPrivate;

/**
 * ch_util_error_dialog:
 **/
static void
ch_util_error_dialog (ChUtilPrivate *priv, const gchar *title, const gchar *message)
{
	GtkWindow *window;
	GtkWidget *dialog;

	window = GTK_WINDOW(gtk_builder_get_object (priv->builder, "dialog_ch"));
	dialog = gtk_message_dialog_new (window, GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE, "%s", title);
//	gtk_window_set_icon_name (GTK_WINDOW (dialog), GCM_STOCK_ICON);
	gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog), "%s", message);
	gtk_dialog_run (GTK_DIALOG (dialog));
	gtk_widget_destroy (dialog);
}

/**
 * ch_util_activate_cb:
 **/
static void
ch_util_activate_cb (GApplication *application, ChUtilPrivate *priv)
{
	GtkWindow *window;
	window = GTK_WINDOW (gtk_builder_get_object (priv->builder, "dialog_ch"));
	gtk_window_present (window);
}

/**
 * ch_util_set_default_calibration:
 **/
static void
ch_util_set_default_calibration (ChUtilPrivate *priv)
{
	const gchar *title;
	gboolean ret;
	GError *error = NULL;
	CdMat3x3 calibration;
	gdouble pre_scale = 5.0f;
	gdouble post_scale = 3000.0f;
	guint16 calibration_map[6];

	/* set to HW */
	cd_mat33_set_identity (&calibration);
	ch_device_queue_set_calibration (priv->device_queue,
					 priv->device,
					 0,
					 &calibration,
					 CH_CALIBRATION_TYPE_ALL,
					 "Default unity value");
	ch_device_queue_set_post_scale (priv->device_queue,
					priv->device,
					post_scale);
	ch_device_queue_set_pre_scale (priv->device_queue,
				       priv->device,
				       pre_scale);
	/* set to HW */
	calibration_map[0] = 0x00;
	calibration_map[1] = 0x00;
	calibration_map[2] = 0x00;
	calibration_map[3] = 0x00;
	calibration_map[4] = 0x00;
	calibration_map[5] = 0x00;
	ch_device_queue_set_calibration_map (priv->device_queue,
					     priv->device,
					     calibration_map);
	ret = ch_device_queue_process (priv->device_queue,
				       CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
				       NULL,
				       &error);
	if (!ret) {
		/* TRANSLATORS: internal device error */
		title = _("Failed to set the calibration map");
		ch_util_error_dialog (priv, title, error->message);
		g_error_free (error);
		goto out;
	}
out:
	return;
}

/**
 * ch_util_refresh:
 **/
static void
ch_util_refresh (ChUtilPrivate *priv)
{
	CdColorRGB value;
	CdMat3x3 calibration;
	ChColorSelect color_select = 0;
	ChFreqScale multiplier = 0;
	guint8 leds;
	const gchar *title;
	gboolean ret;
	gchar *label;
	gchar *tmp;
	gdouble *calibration_tmp;
	gdouble post_scale;
	gdouble pre_scale;
	GError *error = NULL;
	GtkAdjustment *adj;
	GtkWidget *widget;
	guint16 integral_time = 0;
	guint16 major, minor, micro;
	guint32 serial_number = 0;
	guint i, j;

	/* get values from HW */
	ch_device_queue_get_leds (priv->device_queue,
				  priv->device,
				  &leds);
	ch_device_queue_get_color_select (priv->device_queue,
					  priv->device,
					  &color_select);
	ch_device_queue_get_multiplier (priv->device_queue,
					priv->device,
					&multiplier);
	ch_device_queue_get_firmware_ver (priv->device_queue,
					  priv->device,
					  &major,
					  &minor,
					  &micro);
	ch_device_queue_get_serial_number (priv->device_queue,
					   priv->device,
					   &serial_number);
	ch_device_queue_get_dark_offsets (priv->device_queue,
					  priv->device,
					  &value);
	ch_device_queue_get_pre_scale (priv->device_queue,
				       priv->device,
				       &pre_scale);
	ch_device_queue_get_post_scale (priv->device_queue,
					priv->device,
					&post_scale);
	ch_device_queue_get_integral_time (priv->device_queue,
					   priv->device,
					   &integral_time);
	ret = ch_device_queue_process (priv->device_queue,
				       CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
				       NULL,
				       &error);
	if (!ret) {
		/* TRANSLATORS: internal device error */
		title = _("Failed to get device status");
		ch_util_error_dialog (priv, title, error->message);
		g_error_free (error);
		goto out;
	}
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "checkbutton_led0"));
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget),
				      leds & 0x01);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "checkbutton_led1"));
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget),
				      leds & 0x02);
	priv->leds_old = leds;

	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "comboboxtext_color_select"));
	gtk_combo_box_set_active (GTK_COMBO_BOX (widget), color_select);

	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "comboboxtext_multiplier"));
	gtk_combo_box_set_active (GTK_COMBO_BOX (widget), multiplier);

	tmp = g_strdup_printf ("%i", major);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_firmware_major"));
	gtk_label_set_label (GTK_LABEL (widget), tmp);
	g_free (tmp);
	tmp = g_strdup_printf ("%i", minor);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_firmware_minor"));
	gtk_label_set_label (GTK_LABEL (widget), tmp);
	g_free (tmp);
	tmp = g_strdup_printf ("%i", micro);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_firmware_micro"));
	gtk_label_set_label (GTK_LABEL (widget), tmp);
	g_free (tmp);

	if (serial_number == 0xffffffff) {
		g_warning ("no valid serial number");
		serial_number = 0;
	}
	adj = GTK_ADJUSTMENT (gtk_builder_get_object (priv->builder, "adjustment_serial"));
	gtk_adjustment_set_value (adj, serial_number);

	tmp = g_strdup_printf ("%.4f", value.R);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_dark_red"));
	gtk_label_set_label (GTK_LABEL (widget), tmp);
	g_free (tmp);
	tmp = g_strdup_printf ("%.4f", value.G);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_dark_green"));
	gtk_label_set_label (GTK_LABEL (widget), tmp);
	g_free (tmp);
	tmp = g_strdup_printf ("%.4f", value.B);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_dark_blue"));
	gtk_label_set_label (GTK_LABEL (widget), tmp);
	g_free (tmp);

	/* get calibration */
	ch_device_queue_get_calibration (priv->device_queue,
					 priv->device,
					 0,
					 &calibration,
					 NULL,
					 NULL);
	ret = ch_device_queue_process (priv->device_queue,
				       CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
				       NULL,
				       &error);
	if (!ret) {
		/* TRANSLATORS: internal device error */
		title = _("Failed to get calibration data, resetting");
		ch_util_error_dialog (priv, title, error->message);
		g_clear_error (&error);
		ch_util_set_default_calibration (priv);
	}
	calibration_tmp = cd_mat33_get_data (&calibration);
	for (j = 0; j < 3; j++) {
		for (i = 0; i < 3; i++) {
			label = g_strdup_printf ("label_cal_%i%i", j, i);
			tmp = g_strdup_printf ("%.4f", calibration_tmp[j*3 + i]);
			widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, label));
			gtk_label_set_label (GTK_LABEL (widget), tmp);
			g_free (tmp);
			g_free (label);
		}
	}

	/* get pre scale */
	tmp = g_strdup_printf ("%.4f", pre_scale);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_pre_scale"));
	gtk_label_set_label (GTK_LABEL (widget), tmp);
	g_free (tmp);

	/* get post scale */
	tmp = g_strdup_printf ("%.4f", post_scale);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_post_scale"));
	gtk_label_set_label (GTK_LABEL (widget), tmp);
	g_free (tmp);

	/* get integral time */
	switch (integral_time) {
	case CH_INTEGRAL_TIME_VALUE_5MS:
		i = 0;
		break;
	case CH_INTEGRAL_TIME_VALUE_50MS:
		i = 1;
		break;
	case CH_INTEGRAL_TIME_VALUE_100MS:
		i = 2;
		break;
	case CH_INTEGRAL_TIME_VALUE_200MS:
		i = 3;
		break;
	case CH_INTEGRAL_TIME_VALUE_MAX:
		i = 4;
		break;
	default:
		g_debug ("Not found time %i, setting to max", integral_time);
		i = 4;
		break;
	}
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "comboboxtext_integral"));
	gtk_combo_box_set_active (GTK_COMBO_BOX (widget), i);
out:
	return;
}

/**
 * ch_util_refresh_button_cb:
 **/
static void
ch_util_refresh_button_cb (GtkWidget *widget, ChUtilPrivate *priv)
{
	ch_util_refresh (priv);
}

/**
 * ch_util_close_button_cb:
 **/
static void
ch_util_close_button_cb (GtkWidget *widget, ChUtilPrivate *priv)
{
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "dialog_ch"));
	gtk_widget_destroy (widget);
}

/**
 * ch_util_write_button_cb:
 **/
static void
ch_util_write_button_cb (GtkWidget *widget, ChUtilPrivate *priv)
{
	const gchar *title;
	gboolean ret;
	GError *error = NULL;

	ch_device_queue_write_eeprom (priv->device_queue,
				      priv->device,
				      CH_WRITE_EEPROM_MAGIC);
	ret = ch_device_queue_process (priv->device_queue,
				       CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
				       NULL,
				       &error);
	if (!ret) {
		/* TRANSLATORS: internal device error */
		title = _("Failed to write EEPROM");
		ch_util_error_dialog (priv, title, error->message);
		g_error_free (error);
	}
}

/**
 * ch_util_measure_raw:
 **/
static void
ch_util_measure_raw (ChUtilPrivate *priv)
{
	const gchar *title;
	gboolean ret;
	gchar *tmp;
	GError *error = NULL;
	CdColorRGB value;
	GtkWidget *widget;
	GTimer *timer = NULL;

	/* turn on sensor */
	timer = g_timer_new ();
	ch_device_queue_set_multiplier (priv->device_queue,
					priv->device,
					CH_FREQ_SCALE_100);
	ch_device_queue_take_readings (priv->device_queue,
				       priv->device,
				       &value);
	ret = ch_device_queue_process (priv->device_queue,
				       CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
				       NULL,
				       &error);
	if (!ret) {
		/* TRANSLATORS: internal device error */
		title = _("Failed to take readings");
		ch_util_error_dialog (priv, title, error->message);
		g_error_free (error);
		goto out;
	}

	/* update profile label */
	tmp = g_strdup_printf ("%.2fms", g_timer_elapsed (timer, NULL) * 1000);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_profile"));
	gtk_label_set_label (GTK_LABEL (widget), tmp);
	g_free (tmp);

	/* update maximum values */
	if (value.R > priv->value_max.R)
		priv->value_max.R = value.R;
	if (value.G > priv->value_max.G)
		priv->value_max.G = value.G;
	if (value.B > priv->value_max.B)
		priv->value_max.B = value.B;

	/* update sliders */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "progressbar_red"));
	gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (widget),
				       value.R / priv->value_max.R);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "progressbar_green"));
	gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (widget),
				       value.G / priv->value_max.G);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "progressbar_blue"));
	gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (widget),
				       value.B / priv->value_max.B);

	/* update sample */
	tmp = g_strdup_printf ("%.4f", value.R);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_sample_x"));
	gtk_label_set_label (GTK_LABEL (widget), tmp);
	g_free (tmp);
	tmp = g_strdup_printf ("%.4f", value.G);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_sample_y"));
	gtk_label_set_label (GTK_LABEL (widget), tmp);
	g_free (tmp);
	tmp = g_strdup_printf ("%.4f", value.B);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_sample_z"));
	gtk_label_set_label (GTK_LABEL (widget), tmp);
	g_free (tmp);
out:
	if (timer != NULL)
		g_timer_destroy (timer);
}

/**
 * ch_util_measure_device:
 **/
static void
ch_util_measure_device (ChUtilPrivate *priv)
{
	const gchar *title;
	gboolean ret;
	gchar *tmp;
	GError *error = NULL;
	CdColorXYZ value;
	GtkWidget *widget;
	GTimer *timer = NULL;

	/* turn on sensor */
	timer = g_timer_new ();
	ch_device_queue_set_multiplier (priv->device_queue,
					priv->device,
					CH_FREQ_SCALE_100);
	ch_device_queue_take_readings_xyz (priv->device_queue,
					   priv->device,
					   0,
					   &value);
	ret = ch_device_queue_process (priv->device_queue,
				       CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
				       NULL,
				       &error);
	if (!ret) {
		/* TRANSLATORS: internal device error */
		title = _("Failed to take readings");
		ch_util_error_dialog (priv, title, error->message);
		g_error_free (error);
		goto out;
	}

	/* update profile label */
	tmp = g_strdup_printf ("%.2fms", g_timer_elapsed (timer, NULL) * 1000);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_profile"));
	gtk_label_set_label (GTK_LABEL (widget), tmp);
	g_free (tmp);

	/* update sample */
	tmp = g_strdup_printf ("%.4f", value.X);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_sample_x"));
	gtk_label_set_label (GTK_LABEL (widget), tmp);
	g_free (tmp);
	tmp = g_strdup_printf ("%.4f", value.Y);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_sample_y"));
	gtk_label_set_label (GTK_LABEL (widget), tmp);
	g_free (tmp);
	tmp = g_strdup_printf ("%.4f", value.Z);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_sample_z"));
	gtk_label_set_label (GTK_LABEL (widget), tmp);
	g_free (tmp);
out:
	if (timer != NULL)
		g_timer_destroy (timer);
}

/**
 * ch_util_measure_button_cb:
 **/
static void
ch_util_measure_button_cb (GtkWidget *widget, ChUtilPrivate *priv)
{
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "radiobutton_mode_raw"));
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget))) {
		ch_util_measure_raw (priv);
		return;
	}
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "radiobutton_mode_device"));
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget))) {
		ch_util_measure_device (priv);
		return;
	}
}

/**
 * ch_util_dark_offset_button_cb:
 **/
static void
ch_util_dark_offset_button_cb (GtkWidget *widget, ChUtilPrivate *priv)
{
	const gchar *title;
	gboolean ret;
	CdColorRGB value;
	GError *error = NULL;

	/* reset to zero */
	value.R = 0.0f;
	value.G = 0.0f;
	value.B = 0.0f;
	ch_device_queue_set_dark_offsets (priv->device_queue,
					  priv->device,
					  &value);
	ch_device_queue_set_multiplier (priv->device_queue,
					priv->device,
					CH_FREQ_SCALE_100);
	ch_device_queue_take_readings (priv->device_queue,
				       priv->device,
				       &value);
	ret = ch_device_queue_process (priv->device_queue,
				       CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
				       NULL,
				       &error);
	if (!ret) {
		/* TRANSLATORS: internal device error */
		title = _("Failed to take readings");
		ch_util_error_dialog (priv, title, error->message);
		g_error_free (error);
		goto out;
	}

	/* set new values */
	ch_device_queue_set_dark_offsets (priv->device_queue,
					  priv->device,
					  &value);
	ret = ch_device_queue_process (priv->device_queue,
				       CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
				       NULL,
				       &error);
	if (!ret) {
		/* TRANSLATORS: internal device error */
		title = _("Failed to set dark offsets");
		ch_util_error_dialog (priv, title, error->message);
		g_error_free (error);
		goto out;
	}
out:
	/* refresh GUI */
	ch_util_refresh (priv);
}

/**
 * ch_util_calibrate_button_cb:
 **/
static void
ch_util_calibrate_button_cb (GtkWidget *widget, ChUtilPrivate *priv)
{
	ch_util_set_default_calibration (priv);

	/* refresh */
	ch_util_refresh (priv);
}

/**
 * ch_util_color_select_changed_cb:
 **/
static void
ch_util_color_select_changed_cb (GtkWidget *widget, ChUtilPrivate *priv)
{
	const gchar *title;
	ChColorSelect color_select;
	gboolean ret;
	GError *error = NULL;

	color_select = gtk_combo_box_get_active (GTK_COMBO_BOX (widget));

	/* set to HW */
	ch_device_queue_set_color_select (priv->device_queue,
					  priv->device,
					  color_select);
	ret = ch_device_queue_process (priv->device_queue,
				       CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
				       NULL,
				       &error);
	if (!ret) {
		/* TRANSLATORS: internal device error */
		title = _("Failed to set color select");
		ch_util_error_dialog (priv, title, error->message);
		g_error_free (error);
	}
}

/**
 * ch_util_adjustment_value_changed_cb:
 **/
static void
ch_util_adjustment_value_changed_cb (GtkAdjustment *adjustment, ChUtilPrivate *priv)
{
	const gchar *title;
	gboolean ret;
	gdouble value;
	GError *error = NULL;

	value = gtk_adjustment_get_value (adjustment);
	ch_device_queue_set_serial_number (priv->device_queue,
					   priv->device,
					   (guint64) value);
	ret = ch_device_queue_process (priv->device_queue,
				       CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
				       NULL,
				       &error);
	if (!ret) {
		/* TRANSLATORS: internal device error */
		title = _("Failed to set serial number");
		ch_util_error_dialog (priv, title, error->message);
		g_error_free (error);
	}
}

/**
 * ch_util_checkbutton0_toggled_cb:
 **/
static void
ch_util_checkbutton0_toggled_cb (GtkWidget *widget, ChUtilPrivate *priv)
{
	const gchar *title;
	gboolean ret;
	GError *error = NULL;

	priv->leds_old ^= CH_STATUS_LED_GREEN;

	/* set to HW */
	ch_device_queue_set_leds (priv->device_queue,
				  priv->device,
				  priv->leds_old,
				  0,
				  0xff,
				  0xff);
	ret = ch_device_queue_process (priv->device_queue,
				       CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
				       NULL,
				       &error);
	if (!ret) {
		/* TRANSLATORS: internal device error */
		title = _("Failed to set LEDs");
		ch_util_error_dialog (priv, title, error->message);
		g_error_free (error);
	}
}

/**
 * ch_util_checkbutton1_toggled_cb:
 **/
static void
ch_util_checkbutton1_toggled_cb (GtkWidget *widget, ChUtilPrivate *priv)
{
	const gchar *title;
	gboolean ret;
	GError *error = NULL;

	priv->leds_old ^= CH_STATUS_LED_RED;

	/* set to HW */
	ch_device_queue_set_leds (priv->device_queue,
				  priv->device,
				  priv->leds_old,
				  0,
				  0xff,
				  0xff);
	ret = ch_device_queue_process (priv->device_queue,
				       CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
				       NULL,
				       &error);
	if (!ret) {
		/* TRANSLATORS: internal device error */
		title = _("Failed to set LEDs");
		ch_util_error_dialog (priv, title, error->message);
		g_error_free (error);
	}
}

/**
 * ch_util_multiplier_changed_cb:
 **/
static void
ch_util_multiplier_changed_cb (GtkWidget *widget, ChUtilPrivate *priv)
{
	const gchar *title;
	ChFreqScale multiplier;
	gboolean ret;
	GError *error = NULL;

	multiplier = gtk_combo_box_get_active (GTK_COMBO_BOX (widget));

	/* set to HW */
	ch_device_queue_set_multiplier (priv->device_queue,
					priv->device,
					multiplier);
	ret = ch_device_queue_process (priv->device_queue,
				       CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
				       NULL,
				       &error);
	if (!ret) {
		/* TRANSLATORS: internal device error */
		title = _("Failed to set multiplier");
		ch_util_error_dialog (priv, title, error->message);
		g_error_free (error);
	}
}

/**
 * ch_util_mode_changed_cb:
 **/
static void
ch_util_mode_changed_cb (GtkWidget *widget, ChUtilPrivate *priv)
{
	gboolean is_raw;

	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "radiobutton_mode_raw"));
	is_raw = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget));

	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "grid_raw_bars"));
	gtk_widget_set_visible (widget, is_raw);
}

/**
 * ch_util_integral_changed_cb:
 **/
static void
ch_util_integral_changed_cb (GtkWidget *widget, ChUtilPrivate *priv)
{
	const gchar *title;
	gint idx;
	guint16 integral_time = 0;
	gboolean ret;
	GError *error = NULL;

	idx = gtk_combo_box_get_active (GTK_COMBO_BOX (widget));
	switch (idx) {
	case 0:
		/* 5ms */
		integral_time = CH_INTEGRAL_TIME_VALUE_5MS;
		break;
	case 1:
		/* 50ms */
		integral_time = CH_INTEGRAL_TIME_VALUE_50MS;
		break;
	case 2:
		/* 100ms */
		integral_time = CH_INTEGRAL_TIME_VALUE_100MS;
		break;
	case 3:
		/* 200ms */
		integral_time = CH_INTEGRAL_TIME_VALUE_200MS;
		break;
	case 4:
		/* maximum */
		integral_time = CH_INTEGRAL_TIME_VALUE_MAX;
		break;
	default:
		g_assert_not_reached ();
	}

	/* set to HW */
	ch_device_queue_set_integral_time (priv->device_queue,
					   priv->device,
					   integral_time);
	ret = ch_device_queue_process (priv->device_queue,
				       CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
				       NULL,
				       &error);
	if (!ret) {
		/* TRANSLATORS: internal device error */
		title = _("Failed to set sample read time");
		ch_util_error_dialog (priv, title, error->message);
		g_error_free (error);
	}
}

/**
 * ch_util_get_default_device:
 **/
static GUsbDevice *
ch_util_get_default_device (GError **error)
{
	gboolean ret;
	GUsbContext *usb_ctx;
	GUsbDevice *device = NULL;
	GUsbDeviceList *list;

	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* try to find the ColorHug device */
	usb_ctx = g_usb_context_new (NULL);
	list = g_usb_device_list_new (usb_ctx);
	g_usb_device_list_coldplug (list);
	device = g_usb_device_list_find_by_vid_pid (list,
						    CH_USB_VID,
						    CH_USB_PID,
						    error);
	if (device == NULL)
		goto out;
	g_debug ("Found ColorHug device %s",
		 g_usb_device_get_platform_id (device));
	ret = ch_device_open (device, error);
	if (!ret)
		goto out;
out:
	g_object_unref (usb_ctx);
	if (list != NULL)
		g_object_unref (list);
	return device;
}

/**
 * ch_util_startup_cb:
 **/
static void
ch_util_startup_cb (GApplication *application, ChUtilPrivate *priv)
{
	const gchar *title;
	GError *error = NULL;
	gint retval;
	GtkWidget *main_window;
	GtkWidget *widget;
	GtkAdjustment *adj;

	/* get UI */
	priv->builder = gtk_builder_new ();
	retval = gtk_builder_add_from_file (priv->builder,
					    CH_DATA "/ch-util.ui",
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

	main_window = GTK_WIDGET (gtk_builder_get_object (priv->builder, "dialog_ch"));
	gtk_application_add_window (priv->application, GTK_WINDOW (main_window));

	/* Hide window first so that the dialogue resizes itself without redrawing */
	gtk_widget_hide (main_window);

	/* connect to device */
	priv->device_queue = ch_device_queue_new ();
	priv->device = ch_util_get_default_device (&error);
	if (priv->device == NULL) {
		/* TRANSLATORS: internal device error */
		title = _("Failed to connect to device");
		ch_util_error_dialog (priv, title, error->message);
		g_error_free (error);
		gtk_widget_destroy (main_window);
		goto out;
	}

	/* buttons */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_refresh"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (ch_util_refresh_button_cb), priv);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_close"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (ch_util_close_button_cb), priv);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_write"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (ch_util_write_button_cb), priv);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_measure"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (ch_util_measure_button_cb), priv);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_dark_offset"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (ch_util_dark_offset_button_cb), priv);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_calibrate"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (ch_util_calibrate_button_cb), priv);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "comboboxtext_color_select"));
	g_signal_connect (widget, "changed",
			  G_CALLBACK (ch_util_color_select_changed_cb), priv);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "comboboxtext_multiplier"));
	g_signal_connect (widget, "changed",
			  G_CALLBACK (ch_util_multiplier_changed_cb), priv);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "comboboxtext_integral"));
	g_signal_connect (widget, "changed",
			  G_CALLBACK (ch_util_integral_changed_cb), priv);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "checkbutton_led0"));
	g_signal_connect (widget, "toggled",
			  G_CALLBACK (ch_util_checkbutton0_toggled_cb), priv);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "checkbutton_led1"));
	g_signal_connect (widget, "toggled",
			  G_CALLBACK (ch_util_checkbutton1_toggled_cb), priv);

	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "radiobutton_mode_raw"));
	g_signal_connect (widget, "toggled",
			  G_CALLBACK (ch_util_mode_changed_cb), priv);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "radiobutton_mode_device"));
	g_signal_connect (widget, "toggled",
			  G_CALLBACK (ch_util_mode_changed_cb), priv);

	/* show main UI */
	gtk_widget_show (main_window);

	/* update UI */
	ch_util_refresh (priv);

	/* post refresh connects */
	adj = GTK_ADJUSTMENT (gtk_builder_get_object (priv->builder, "adjustment_serial"));
	g_signal_connect (adj, "value-changed",
			  G_CALLBACK (ch_util_adjustment_value_changed_cb), priv);
out:
	return;
}

/**
 * main:
 **/
int
main (int argc, char **argv)
{
	ChUtilPrivate *priv;
	GOptionContext *context;
	int status = 0;
	gboolean ret;
	GError *error = NULL;

	setlocale (LC_ALL, "");

	bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	gtk_init (&argc, &argv);

	/* TRANSLATORS: A program to do low level commands on the hardware */
	context = g_option_context_new (_("ColorHug command line tool"));
	g_option_context_add_group (context, gtk_get_option_group (TRUE));
	ret = g_option_context_parse (context, &argc, &argv, &error);
	if (!ret) {
		g_warning ("%s: %s",
			   _("Failed to parse command line options"),
			   error->message);
		g_error_free (error);
	}
	g_option_context_free (context);

	priv = g_new0 (ChUtilPrivate, 1);
	priv->value_max.R = 0.0f;
	priv->value_max.G = 0.0f;
	priv->value_max.B = 0.0f;

	/* ensure single instance */
	priv->application = gtk_application_new ("com.hughski.ColorHug.Util", 0);
	g_signal_connect (priv->application, "startup",
			  G_CALLBACK (ch_util_startup_cb), priv);
	g_signal_connect (priv->application, "activate",
			  G_CALLBACK (ch_util_activate_cb), priv);

	/* wait */
	status = g_application_run (G_APPLICATION (priv->application), argc, argv);

	g_object_unref (priv->application);
	if (priv->builder != NULL)
		g_object_unref (priv->builder);
	if (priv->device != NULL)
		g_object_unref (priv->device);
	if (priv->device_queue != NULL)
		g_object_unref (priv->device_queue);
	g_free (priv);
	return status;
}
