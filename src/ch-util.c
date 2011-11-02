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
	guint8		 leds_old;
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
 * ch_util_refresh:
 **/
static void
ch_util_refresh (ChUtilPrivate *priv)
{
	ChColorSelect color_select = 0;
	ChFreqScale multiplier = 0;
	gboolean ret;
	gchar *label;
	gchar *tmp;
	GError *error = NULL;
	gfloat *calibration = NULL;
	GtkAdjustment *adj;
	GtkWidget *widget;
	guint16 integral_time = 0;
	guint16 major, minor, micro;
	guint16 red, green, blue;
	guint64 serial_number;
	guint8 leds;
	guint i, j;

	/* get leds from HW */
	ret = ch_client_get_leds (priv->client,
				  &leds,
				  &error);
	if (!ret) {
		ch_util_error_dialog (priv,
				      _("Failed to get LED status"),
				      error->message);
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
		ch_util_error_dialog (priv,
				      _("Failed to get color select"),
				      error->message);
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
		ch_util_error_dialog (priv,
				      _("Failed to get multiplier"),
				      error->message);
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
		ch_util_error_dialog (priv,
				      _("Failed to get firmware version"),
				      error->message);
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
		ch_util_error_dialog (priv,
				      _("Failed to get serial number"),
				      error->message);
		g_error_free (error);
		goto out;
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
		ch_util_error_dialog (priv,
				      _("Failed to get dark offsets"),
				      error->message);
		g_error_free (error);
		goto out;
	}
	tmp = g_strdup_printf ("%i", red);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_dark_red"));
	gtk_label_set_label (GTK_LABEL (widget), tmp);
	g_free (tmp);
	tmp = g_strdup_printf ("%i", green);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_dark_green"));
	gtk_label_set_label (GTK_LABEL (widget), tmp);
	g_free (tmp);
	tmp = g_strdup_printf ("%i", blue);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_dark_blue"));
	gtk_label_set_label (GTK_LABEL (widget), tmp);
	g_free (tmp);

	/* get calibration */
	ret = ch_client_get_calibration (priv->client, &calibration, &error);
	if (!ret) {
		ch_util_error_dialog (priv,
				      _("Failed to get calibration data"),
				      error->message);
		g_error_free (error);
		goto out;
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

	/* get integral time */
	ret = ch_client_get_integral_time (priv->client,
					   &integral_time,
					   &error);
	if (!ret) {
		ch_util_error_dialog (priv,
				      _("Failed to get integral time"),
				      error->message);
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
	g_free (calibration);
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
	gboolean ret;
	GError *error = NULL;

	ret = ch_client_write_eeprom (priv->client,
				      CH_WRITE_EEPROM_MAGIC,
				      &error);
	if (!ret) {
		ch_util_error_dialog (priv,
				      _("Failed to write EEPROM"),
				      error->message);
		g_error_free (error);
	}
}

/**
 * ch_util_measure_raw:
 **/
static void
ch_util_measure_raw (ChUtilPrivate *priv)
{
	gboolean ret;
	gchar *tmp;
	GError *error = NULL;
	gfloat red_f, green_f, blue_f;
	GtkWidget *widget;
	guint16 red, green, blue;
	GTimer *timer = NULL;

	/* turn on sensor */
	ret = ch_client_set_multiplier (priv->client,
					CH_FREQ_SCALE_100,
					&error);
	if (!ret) {
		ch_util_error_dialog (priv,
				      _("Failed to turn on sensor"),
				      error->message);
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
		ch_util_error_dialog (priv,
				      _("Failed to take readings"),
				      error->message);
		g_error_free (error);
		goto out;
	}

	/* convert to floating point */
	red_f = red / (gfloat) 0xffff;
	green_f = green / (gfloat) 0xffff;
	blue_f = blue / (gfloat) 0xffff;

	/* update profile label */
	tmp = g_strdup_printf ("%.2fms", g_timer_elapsed (timer, NULL) * 1000);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_profile"));
	gtk_label_set_label (GTK_LABEL (widget), tmp);
	g_free (tmp);

	/* update sample */
	tmp = g_strdup_printf ("%.4f", red_f);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_sample_x"));
	gtk_label_set_label (GTK_LABEL (widget), tmp);
	g_free (tmp);
	tmp = g_strdup_printf ("%.4f", green_f);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_sample_y"));
	gtk_label_set_label (GTK_LABEL (widget), tmp);
	g_free (tmp);
	tmp = g_strdup_printf ("%.4f", blue_f);
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
	gboolean ret;
	gchar *tmp;
	GError *error = NULL;
	gfloat red, green, blue;
	GtkWidget *widget;
	GTimer *timer = NULL;

	/* turn on sensor */
	ret = ch_client_set_multiplier (priv->client,
					CH_FREQ_SCALE_100,
					&error);
	if (!ret) {
		ch_util_error_dialog (priv,
				      _("Failed to turn on sensor"),
				      error->message);
		g_error_free (error);
		goto out;
	}

	/* get from HW */
	timer = g_timer_new ();
	ret = ch_client_take_readings_xyz (priv->client,
					   &red,
					   &green,
					   &blue,
					   &error);
	if (!ret) {
		ch_util_error_dialog (priv,
				      _("Failed to take readings"),
				      error->message);
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
	gboolean ret;
	guint16 red, green, blue;
	GError *error = NULL;

	/* reset to zero */
	ret = ch_client_set_dark_offsets (priv->client,
					  0, 0, 0,
					  &error);
	if (!ret) {
		ch_util_error_dialog (priv,
				      _("Failed to reset dark offsets"),
				      error->message);
		g_error_free (error);
		goto out;
	}

	/* turn on sensor */
	ret = ch_client_set_multiplier (priv->client,
					CH_FREQ_SCALE_100,
					&error);
	if (!ret) {
		ch_util_error_dialog (priv,
				      _("Failed to turn on sensor"),
				      error->message);
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
		ch_util_error_dialog (priv,
				      _("Failed to take readings"),
				      error->message);
		g_error_free (error);
		goto out;
	}

	/* set new values */
	ret = ch_client_set_dark_offsets (priv->client,
					  red, green, blue,
					  &error);
	if (!ret) {
		ch_util_error_dialog (priv,
				      _("Failed to set dark offsets"),
				      error->message);
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
	gboolean ret;
	GError *error = NULL;
	gfloat calibration[9];

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
					 calibration,
					 &error);
	if (!ret) {
		ch_util_error_dialog (priv,
				      _("Failed to set calibration"),
				      error->message);
		g_error_free (error);
		goto out;
	}

	/* refresh */
	ch_util_refresh (priv);
out:
	return;
}

/**
 * ch_util_color_select_changed_cb:
 **/
static void
ch_util_color_select_changed_cb (GtkWidget *widget, ChUtilPrivate *priv)
{
	ChColorSelect color_select;
	gboolean ret;
	GError *error = NULL;

	color_select = gtk_combo_box_get_active (GTK_COMBO_BOX (widget));

	/* set to HW */
	ret = ch_client_set_color_select (priv->client,
					  color_select,
					  &error);
	if (!ret) {
		ch_util_error_dialog (priv,
				      _("Failed to set color select"),
				      error->message);
		g_error_free (error);
	}
}

/**
 * ch_util_adjustment_value_changed_cb:
 **/
static void
ch_util_adjustment_value_changed_cb (GtkAdjustment *adjustment, ChUtilPrivate *priv)
{
	gboolean ret;
	gdouble value;
	GError *error = NULL;

	value = gtk_adjustment_get_value (adjustment);
	ret = ch_client_set_serial_number (priv->client, value, &error);
	if (!ret) {
		ch_util_error_dialog (priv,
				      _("Failed to set serial number"),
				      error->message);
		g_error_free (error);
	}
}

/**
 * ch_util_checkbutton0_toggled_cb:
 **/
static void
ch_util_checkbutton0_toggled_cb (GtkWidget *widget, ChUtilPrivate *priv)
{
	gboolean ret;
	GError *error = NULL;

	priv->leds_old ^= 0x01;

	/* set to HW */
	ret = ch_client_set_leds (priv->client,
				  priv->leds_old,
				  &error);
	if (!ret) {
		ch_util_error_dialog (priv,
				      _("Failed to set LEDs"),
				      error->message);
		g_error_free (error);
	}
}

/**
 * ch_util_checkbutton1_toggled_cb:
 **/
static void
ch_util_checkbutton1_toggled_cb (GtkWidget *widget, ChUtilPrivate *priv)
{
	gboolean ret;
	GError *error = NULL;

	priv->leds_old ^= 0x02;

	/* set to HW */
	ret = ch_client_set_leds (priv->client,
				  priv->leds_old,
				  &error);
	if (!ret) {
		ch_util_error_dialog (priv,
				      _("Failed to set LEDs"),
				      error->message);
		g_error_free (error);
	}
}

/**
 * ch_util_multiplier_changed_cb:
 **/
static void
ch_util_multiplier_changed_cb (GtkWidget *widget, ChUtilPrivate *priv)
{
	ChFreqScale multiplier;
	gboolean ret;
	GError *error = NULL;

	multiplier = gtk_combo_box_get_active (GTK_COMBO_BOX (widget));

	/* set to HW */
	ret = ch_client_set_multiplier (priv->client,
					multiplier,
					&error);
	if (!ret) {
		ch_util_error_dialog (priv,
				      _("Failed to set multiplier"),
				      error->message);
		g_error_free (error);
	}
}

/**
 * ch_util_integral_changed_cb:
 **/
static void
ch_util_integral_changed_cb (GtkWidget *widget, ChUtilPrivate *priv)
{
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
		ch_util_error_dialog (priv,
				      _("Failed to set integral time"),
				      error->message);
		g_error_free (error);
	}
}

/**
 * ch_util_startup_cb:
 **/
static void
ch_util_startup_cb (GApplication *application, ChUtilPrivate *priv)
{
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
		ch_util_error_dialog (priv,
				      _("Failed to connect"),
				      error->message);
		g_error_free (error);
		gtk_widget_destroy (main_window);
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

	context = g_option_context_new ("gnome-color-manager profile priv");
	g_option_context_add_group (context, gtk_get_option_group (TRUE));
	ret = g_option_context_parse (context, &argc, &argv, &error);
	if (!ret) {
		g_warning ("failed to parse options: %s", error->message);
		g_error_free (error);
	}
	g_option_context_free (context);

	priv = g_new0 (ChUtilPrivate, 1);

	/* ensure single instance */
	priv->application = gtk_application_new ("org.hughsie.ColorHug.Util", 0);
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
