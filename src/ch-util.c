/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2009-2011 Richard Hughes <richard@hughsie.com>
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

#include "ch-client.h"

typedef struct {
	GtkBuilder	*builder;
	GtkApplication	*application;
	ChClient	*client;
	ChStatusLed	 leds_old;
	gdouble		 red_max;
	gdouble		 green_max;
	gdouble		 blue_max;
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
	gdouble calibration[9];
	gdouble pre_scale = 5.0f;
	gdouble post_scale = 1.0f;
	guint16 calibration_map[6];

	calibration[0] = 1.0f;
	calibration[1] = 0.0f;
	calibration[2] = 0.0f;
	calibration[3] = 0.0f;
	calibration[4] = 1.0f;
	calibration[5] = 0.0f;
	calibration[6] = 0.0f;
	calibration[7] = 0.0f;
	calibration[8] = 1.0f;

	/* set to HW */
	ret = ch_client_set_calibration (priv->client,
					 0,
					 calibration,
					 CH_CALIBRATION_TYPE_ALL,
					 "Default unity value",
					 &error);
	if (!ret) {
		/* TRANSLATORS: internal device error */
		title = _("Failed to set calibration");
		ch_util_error_dialog (priv, title, error->message);
		g_error_free (error);
		goto out;
	}

	/* set to HW */
	ret = ch_client_set_post_scale (priv->client,
					post_scale,
					&error);
	if (!ret) {
		/* TRANSLATORS: post scale is applied after the XYZ conversion */
		title = _("Failed to set post scale");
		ch_util_error_dialog (priv, title, error->message);
		g_error_free (error);
		goto out;
	}

	/* set to HW */
	ret = ch_client_set_pre_scale (priv->client,
				       pre_scale,
				       &error);
	if (!ret) {
		/* TRANSLATORS: pre scale is applied after the dRGB
		 * sample but before the XYZ conversion */
		title = _("Failed to set pre scale");
		ch_util_error_dialog (priv, title, error->message);
		g_error_free (error);
		goto out;
	}

	/* set to HW */
	calibration_map[0] = 0x00;
	calibration_map[1] = 0x00;
	calibration_map[2] = 0x00;
	calibration_map[3] = 0x00;
	calibration_map[4] = 0x00;
	calibration_map[5] = 0x00;
	ret = ch_client_set_calibration_map (priv->client,
					     calibration_map,
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
	const gchar *title;
	ChColorSelect color_select = 0;
	ChFreqScale multiplier = 0;
	gboolean ret;
	gchar *label;
	gchar *tmp;
	GError *error = NULL;
	gdouble calibration[9];
	gdouble pre_scale;
	gdouble post_scale;
	GtkAdjustment *adj;
	GtkWidget *widget;
	guint16 integral_time = 0;
	guint16 major, minor, micro;
	gdouble red, green, blue;
	guint32 serial_number = 0;
	ChStatusLed leds;
	guint i, j;

	/* get leds from HW */
	ret = ch_client_get_leds (priv->client,
				  &leds,
				  &error);
	if (!ret) {
		/* TRANSLATORS: internal device error */
		title = _("Failed to get LED status");
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

	/* get color select from HW */
	ret = ch_client_get_color_select (priv->client,
					  &color_select,
					  &error);
	if (!ret) {
		/* TRANSLATORS: internal device error */
		title = _("Failed to get color select");
		ch_util_error_dialog (priv, title, error->message);
		g_error_free (error);
		goto out;
	}
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "comboboxtext_color_select"));
	gtk_combo_box_set_active (GTK_COMBO_BOX (widget), color_select);

	/* get multiplier from HW */
	ret = ch_client_get_multiplier (priv->client,
					&multiplier,
					&error);
	if (!ret) {
		/* TRANSLATORS: the multiplier is the scale factor used
		 * when using the sensor */
		title = _("Failed to get multiplier");
		ch_util_error_dialog (priv, title, error->message);
		g_error_free (error);
		goto out;
	}
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "comboboxtext_multiplier"));
	gtk_combo_box_set_active (GTK_COMBO_BOX (widget), multiplier);

	/* get firmware */
	ret = ch_client_get_firmware_ver (priv->client,
					  &major,
					  &minor,
					  &micro,
					  &error);
	if (!ret) {
		/* TRANSLATORS: internal device error */
		title = _("Failed to get firmware version");
		ch_util_error_dialog (priv, title, error->message);
		g_error_free (error);
		goto out;
	}
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

	/* get firmware version */
	ret = ch_client_get_serial_number (priv->client, &serial_number, &error);
	if (!ret) {
		/* TRANSLATORS: internal device error */
		title = _("Failed to get serial number");
		ch_util_error_dialog (priv, title, error->message);
		g_error_free (error);
		goto out;
	}
	if (serial_number == 0xffffffff) {
		g_warning ("no valid serial number");
		serial_number = 0;
	}
	adj = GTK_ADJUSTMENT (gtk_builder_get_object (priv->builder, "adjustment_serial"));
	gtk_adjustment_set_value (adj, serial_number);

	/* get dark offsets */
	ret = ch_client_get_dark_offsets (priv->client,
					  &red,
					  &green,
					  &blue,
					  &error);
	if (!ret) {
		/* TRANSLATORS: failed to get the absolute black offset */
		title = _("Failed to get dark offsets");
		ch_util_error_dialog (priv, title, error->message);
		g_error_free (error);
		goto out;
	}
	tmp = g_strdup_printf ("%.4f", red);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_dark_red"));
	gtk_label_set_label (GTK_LABEL (widget), tmp);
	g_free (tmp);
	tmp = g_strdup_printf ("%.4f", green);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_dark_green"));
	gtk_label_set_label (GTK_LABEL (widget), tmp);
	g_free (tmp);
	tmp = g_strdup_printf ("%.4f", blue);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_dark_blue"));
	gtk_label_set_label (GTK_LABEL (widget), tmp);
	g_free (tmp);

	/* get calibration */
	ret = ch_client_get_calibration (priv->client,
					 0,
					 calibration,
					 NULL,
					 NULL,
					 &error);
	if (!ret) {
		/* TRANSLATORS: internal device error */
		title = _("Failed to get calibration data, resetting");
		ch_util_error_dialog (priv, title, error->message);
		g_clear_error (&error);
		ch_util_set_default_calibration (priv);
	}
	for (j = 0; j < 3; j++) {
		for (i = 0; i < 3; i++) {
			label = g_strdup_printf ("label_cal_%i%i", j, i);
			tmp = g_strdup_printf ("%.4f", calibration[j*3 + i]);
			widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, label));
			gtk_label_set_label (GTK_LABEL (widget), tmp);
			g_free (tmp);
			g_free (label);
		}
	}

	/* get pre scale */
	ret = ch_client_get_pre_scale (priv->client,
				       &pre_scale,
				       &error);
	if (!ret) {
		/* TRANSLATORS: internal device error */
		title = _("Failed to get pre scale");
		ch_util_error_dialog (priv, title, error->message);
		g_error_free (error);
		goto out;
	}
	tmp = g_strdup_printf ("%.4f", pre_scale);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_pre_scale"));
	gtk_label_set_label (GTK_LABEL (widget), tmp);
	g_free (tmp);

	/* get post scale */
	ret = ch_client_get_post_scale (priv->client,
					&post_scale,
					&error);
	if (!ret) {
		/* TRANSLATORS: internal device error */
		title = _("Failed to get post scale");
		ch_util_error_dialog (priv, title, error->message);
		g_error_free (error);
		goto out;
	}
	tmp = g_strdup_printf ("%.4f", post_scale);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_post_scale"));
	gtk_label_set_label (GTK_LABEL (widget), tmp);
	g_free (tmp);

	/* get integral time */
	ret = ch_client_get_integral_time (priv->client,
					   &integral_time,
					   &error);
	if (!ret) {
		/* TRANSLATORS: internal device error */
		title = _("Failed to get integral time");
		ch_util_error_dialog (priv, title, error->message);
		g_error_free (error);
		goto out;
	}
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

	ret = ch_client_write_eeprom (priv->client,
				      CH_WRITE_EEPROM_MAGIC,
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
	gdouble red, green, blue;
	GtkWidget *widget;
	GTimer *timer = NULL;

	/* turn on sensor */
	ret = ch_client_set_multiplier (priv->client,
					CH_FREQ_SCALE_100,
					&error);
	if (!ret) {
		/* TRANSLATORS: we have to enable the sensor */
		title = _("Failed to turn on sensor");
		ch_util_error_dialog (priv, title, error->message);
		g_error_free (error);
		goto out;
	}

	/* get from HW */
	timer = g_timer_new ();
	ret = ch_client_take_readings (priv->client,
				       &red,
				       &green,
				       &blue,
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
	if (red > priv->red_max)
		priv->red_max = red;
	if (green > priv->green_max)
		priv->green_max = green;
	if (blue > priv->blue_max)
		priv->blue_max = blue;

	/* update sliders */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "progressbar_red"));
	gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (widget),
				       red / priv->red_max);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "progressbar_green"));
	gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (widget),
				       green / priv->green_max);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "progressbar_blue"));
	gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (widget),
				       blue / priv->blue_max);

	/* update sample */
	tmp = g_strdup_printf ("%.4f", red);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_sample_x"));
	gtk_label_set_label (GTK_LABEL (widget), tmp);
	g_free (tmp);
	tmp = g_strdup_printf ("%.4f", green);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_sample_y"));
	gtk_label_set_label (GTK_LABEL (widget), tmp);
	g_free (tmp);
	tmp = g_strdup_printf ("%.4f", blue);
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
	gdouble red, green, blue;
	GtkWidget *widget;
	GTimer *timer = NULL;

	/* turn on sensor */
	ret = ch_client_set_multiplier (priv->client,
					CH_FREQ_SCALE_100,
					&error);
	if (!ret) {
		/* TRANSLATORS: internal device error */
		title = _("Failed to turn on sensor");
		ch_util_error_dialog (priv, title, error->message);
		g_error_free (error);
		goto out;
	}

	/* get from HW */
	timer = g_timer_new ();
	ret = ch_client_take_readings_xyz (priv->client,
					   0,
					   &red,
					   &green,
					   &blue,
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
	tmp = g_strdup_printf ("%.4f", red);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_sample_x"));
	gtk_label_set_label (GTK_LABEL (widget), tmp);
	g_free (tmp);
	tmp = g_strdup_printf ("%.4f", green);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_sample_y"));
	gtk_label_set_label (GTK_LABEL (widget), tmp);
	g_free (tmp);
	tmp = g_strdup_printf ("%.4f", blue);
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
	gdouble red, green, blue;
	GError *error = NULL;

	/* reset to zero */
	ret = ch_client_set_dark_offsets (priv->client,
					  0, 0, 0,
					  &error);
	if (!ret) {
		/* TRANSLATORS: internal device error */
		title = _("Failed to reset dark offsets");
		ch_util_error_dialog (priv, title, error->message);
		g_error_free (error);
		goto out;
	}

	/* turn on sensor */
	ret = ch_client_set_multiplier (priv->client,
					CH_FREQ_SCALE_100,
					&error);
	if (!ret) {
		/* TRANSLATORS: internal device error */
		title = _("Failed to turn on sensor");
		ch_util_error_dialog (priv, title, error->message);
		g_error_free (error);
		goto out;
	}

	/* get from HW */
	ret = ch_client_take_readings (priv->client,
				       &red,
				       &green,
				       &blue,
				       &error);
	if (!ret) {
		/* TRANSLATORS: internal device error */
		title = _("Failed to take readings");
		ch_util_error_dialog (priv, title, error->message);
		g_error_free (error);
		goto out;
	}

	/* set new values */
	ret = ch_client_set_dark_offsets (priv->client,
					  red, green, blue,
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
 * ch_util_reset_button_cb:
 **/
static void
ch_util_reset_button_cb (GtkWidget *widget, ChUtilPrivate *priv)
{
	const gchar *title;
	gboolean ret;
	GError *error = NULL;

	/* set to HW */
	ret = ch_client_reset (priv->client,
			       &error);
	if (!ret) {
		/* TRANSLATORS: internal device error */
		title = _("Failed to reset processor");
		ch_util_error_dialog (priv, title, error->message);
		g_error_free (error);
		goto out;
	}

	/* refresh */
	ch_util_refresh (priv);
out:
	return;
}

/**
 * ch_util_get_firmware_filename:
 **/
static gchar *
ch_util_get_firmware_filename (GtkWindow *window)
{
	gchar *filename = NULL;
	GtkWidget *dialog;
	GtkFileFilter *filter;

	/* TRANSLATORS: dialog for chosing the firmware */
	dialog = gtk_file_chooser_dialog_new (_("Select firmware file"), window,
					      GTK_FILE_CHOOSER_ACTION_OPEN,
					      GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
					      GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT,
					      NULL);
	gtk_file_chooser_set_create_folders (GTK_FILE_CHOOSER(dialog), FALSE);

	/* setup the filter */
	filter = gtk_file_filter_new ();
	/* TRANSLATORS: filter name on the file->open dialog */
	gtk_file_filter_set_name (filter, _("Supported firmware files"));
	gtk_file_chooser_add_filter (GTK_FILE_CHOOSER(dialog), filter);

	/* setup the all files filter */
	filter = gtk_file_filter_new ();
	gtk_file_filter_add_pattern (filter, "*.bin");
	/* TRANSLATORS: filter name on the file->open dialog */
	gtk_file_filter_set_name (filter, _("Firmware images"));
	gtk_file_chooser_add_filter (GTK_FILE_CHOOSER(dialog), filter);

	/* did user choose file */
	if (gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_ACCEPT)
		filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER(dialog));

	/* we're done */
	gtk_widget_destroy (dialog);

	/* or NULL for missing */
	return filename;
}

/**
 * ch_util_flash_firmware_button_cb:
 **/
static void
ch_util_flash_firmware_button_cb (GtkWidget *widget, ChUtilPrivate *priv)
{
	const gchar *title;
	gboolean ret;
	GError *error = NULL;
	GtkWindow *window;
	gchar *filename;

	window = GTK_WINDOW (gtk_builder_get_object (priv->builder, "dialog_ch"));
	filename = ch_util_get_firmware_filename (window);
	if (filename == NULL)
		goto out;

	/* set to HW */
	ret = ch_client_flash_firmware (priv->client,
					filename,
					&error);
	if (!ret) {
		/* TRANSLATORS: internal device error */
		title = _("Failed to flash new firmware");
		ch_util_error_dialog (priv, title, error->message);
		g_error_free (error);
		goto out;
	}

	/* refresh */
	ch_util_refresh (priv);
out:
	g_free (filename);
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
	ret = ch_client_set_color_select (priv->client,
					  color_select,
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
	ret = ch_client_set_serial_number (priv->client, (guint64) value, &error);
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
	ret = ch_client_set_leds (priv->client,
				  priv->leds_old,
				  0,
				  0xff,
				  0xff,
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
	ret = ch_client_set_leds (priv->client,
				  priv->leds_old,
				  0,
				  0xff,
				  0xff,
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
	ret = ch_client_set_multiplier (priv->client,
					multiplier,
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
	ret = ch_client_set_integral_time (priv->client,
					   integral_time,
					   &error);
	if (!ret) {
		/* TRANSLATORS: internal device error */
		title = _("Failed to set integral time");
		ch_util_error_dialog (priv, title, error->message);
		g_error_free (error);
	}
}

/**
 * ch_util_startup_cb:
 **/
static void
ch_util_startup_cb (GApplication *application, ChUtilPrivate *priv)
{
	const gchar *title;
	gboolean ret;
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
	ret = ch_client_load (priv->client, &error);
	if (!ret) {
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
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_reset"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (ch_util_reset_button_cb), priv);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_flash_firmware"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (ch_util_flash_firmware_button_cb), priv);
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
	priv->red_max = 0.0f;
	priv->green_max = 0.0f;
	priv->blue_max = 0.0f;

	/* ensure single instance */
	priv->application = gtk_application_new ("com.hughski.ColorHug.Util", 0);
	g_signal_connect (priv->application, "startup",
			  G_CALLBACK (ch_util_startup_cb), priv);
	g_signal_connect (priv->application, "activate",
			  G_CALLBACK (ch_util_activate_cb), priv);

	/* use client */
	priv->client = ch_client_new ();

	/* wait */
	status = g_application_run (G_APPLICATION (priv->application), argc, argv);

	g_object_unref (priv->application);
	if (priv->builder != NULL)
		g_object_unref (priv->builder);
	if (priv->client != NULL)
		g_object_unref (priv->client);
	g_free (priv);
	return status;
}
