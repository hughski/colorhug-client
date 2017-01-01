/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
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

#include "egg-graph-widget.h"

typedef struct {
	GPtrArray		*data;
	GSettings		*settings;
	GtkApplication		*application;
	GtkBuilder		*builder;
	GtkWidget		*graph_output;
	GtkWidget		*graph_raw;
	GtkWidget		*graph_dark;
	GtkWidget		*graph_irradiance;
	GtkWidget		*sample_widget;
	GUsbContext		*usb_ctx;
	GUsbDevice		*device;
	gdouble			 spectral_cal[4];
	gdouble			 adc_cal_pos;
	gdouble			 adc_cal_neg;
	guint16			 integral_time;
	gboolean		 integral_time_valid;
	guint			 tick_id;
	gboolean		 tick_enabled;
	ChIlluminant		 illum;
	gboolean		 illum_valid;
	CdSpectrum		*sensitivity_cal;
	CdSpectrum		*dark_cal;
	gboolean		 dark_cal_valid;
	CdSpectrum		*irradiance_cal;
	CdSpectrum		*sp_raw_last;
	GCancellable		*cancellable;
} ChSpectroPrivate;

static void
ch_spectro_error_dialog (ChSpectroPrivate *priv,
			 const gchar *title,
			 const gchar *message)
{
	GtkWindow *window;
	GtkWidget *dialog;

	window = GTK_WINDOW(gtk_builder_get_object (priv->builder, "dialog_spectro"));
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

static void
ch_spectro_activate_cb (GApplication *application, ChSpectroPrivate *priv)
{
	GtkWindow *window;
	window = GTK_WINDOW (gtk_builder_get_object (priv->builder, "dialog_spectro"));
	gtk_window_present (window);
}

static void
ch_spectro_update_ui_refresh (ChSpectroPrivate *priv)
{
	GtkWidget *w;
	g_autofree gchar *str = NULL;

	/* update UI */
	str = g_strdup_printf ("%ims", priv->integral_time);
	w = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_duration"));
	gtk_label_set_label (GTK_LABEL (w), str);
}

static void
ch_spectro_update_ui_graph_raw (ChSpectroPrivate *priv)
{
	guint i;
	g_autoptr(GPtrArray) array = NULL;

	/* nothing set */
	if (priv->dark_cal == NULL)
		return;

	/* add lines */
	array = g_ptr_array_new_with_free_func ((GDestroyNotify) egg_graph_point_free);
	for (i = 0; i < 1024; i++) {
		EggGraphPoint *point = egg_graph_point_new ();
		point->x = cd_spectrum_get_wavelength (priv->sp_raw_last, i);
		point->y = cd_spectrum_get_value (priv->sp_raw_last, i);
		point->color = 0x000000;
		g_ptr_array_add (array, point);
	}
	egg_graph_widget_data_clear (EGG_GRAPH_WIDGET (priv->graph_raw));
	egg_graph_widget_data_add (EGG_GRAPH_WIDGET (priv->graph_raw),
				EGG_GRAPH_WIDGET_PLOT_LINE, array);
}

static void
ch_spectro_update_ui_graph_dark (ChSpectroPrivate *priv)
{
	guint i;
	g_autoptr(GPtrArray) array = NULL;

	/* nothing set */
	if (priv->dark_cal == NULL)
		return;

	/* add lines */
	array = g_ptr_array_new_with_free_func ((GDestroyNotify) egg_graph_point_free);
	for (i = 0; i < 1024; i++) {
		EggGraphPoint *point = egg_graph_point_new ();
		point->x = cd_spectrum_get_wavelength (priv->dark_cal, i);
		point->y = cd_spectrum_get_value (priv->dark_cal, i);
		point->color = 0x000000;
		g_ptr_array_add (array, point);
	}
	egg_graph_widget_data_clear (EGG_GRAPH_WIDGET (priv->graph_dark));
	egg_graph_widget_data_add (EGG_GRAPH_WIDGET (priv->graph_dark),
				EGG_GRAPH_WIDGET_PLOT_LINE, array);
}

static void
ch_spectro_update_ui_graph_irradiance (ChSpectroPrivate *priv)
{
	gdouble nm;
	g_autoptr(GPtrArray) array = NULL;

	/* nothing set */
	if (priv->irradiance_cal == NULL)
		return;

	/* add lines */
	array = g_ptr_array_new_with_free_func ((GDestroyNotify) egg_graph_point_free);
	for (nm = cd_spectrum_get_start (priv->irradiance_cal);
	     nm < cd_spectrum_get_end (priv->irradiance_cal);
	     nm += 1) {
		EggGraphPoint *point = egg_graph_point_new ();
		CdColorRGB tmp;
		CdColorRGB8 tmp8;
		point->x = nm;
		point->y = cd_spectrum_get_value_for_nm (priv->irradiance_cal, nm);
		cd_color_rgb_from_wavelength (&tmp, point->x);
		cd_color_rgb_to_rgb8 (&tmp, &tmp8);
		point->color = (((guint32) tmp8.R) << 16) +
			       (((guint32) tmp8.G) << 8) +
			       ((guint32) tmp8.B);
		g_ptr_array_add (array, point);
	}
	egg_graph_widget_data_clear (EGG_GRAPH_WIDGET (priv->graph_irradiance));
	egg_graph_widget_data_add (EGG_GRAPH_WIDGET (priv->graph_irradiance),
				EGG_GRAPH_WIDGET_PLOT_POINTS, array);
}

static gboolean
ch_spetro_refresh_dark_cal (ChSpectroPrivate *priv, GError **error)
{
	/* take 0ms reading */
	if (!ch_device_set_integral_time (priv->device, 0,
					  priv->cancellable, error))
		return FALSE;
	if (!ch_device_take_reading_spectral (priv->device,
					      CH_SPECTRUM_KIND_RAW,
					      priv->cancellable, error))
		return FALSE;
	if (priv->dark_cal != NULL)
		cd_spectrum_free (priv->dark_cal);
	priv->dark_cal = ch_device_get_spectrum (priv->device,
						 priv->cancellable,
						 error);
	if (priv->dark_cal == NULL)
		return FALSE;
	cd_spectrum_set_start (priv->dark_cal, priv->spectral_cal[0]);
	cd_spectrum_set_wavelength_cal (priv->dark_cal,
					priv->spectral_cal[1],
					priv->spectral_cal[2],
					priv->spectral_cal[3]);

	/* update the UI */
	ch_spectro_update_ui_graph_dark (priv);

	/* save to device */
	if (!ch_device_set_spectrum_full (priv->device,
					  CH_SPECTRUM_KIND_DARK_CAL,
					  priv->dark_cal,
					  priv->cancellable,
					  error)) {
		return FALSE;
	}

	/* back to normal */
	priv->integral_time_valid = FALSE;
	priv->dark_cal_valid = TRUE;
	return TRUE;
}

#include <lcms2.h>

static gboolean
ch_spectro_convert_xyz_to_rgb (CdColorXYZ *xyz, CdColorRGB *rgb, GError **error)
{
	cmsHPROFILE hprofile;
	g_autoptr(CdIcc) icc_rgb = NULL;
	g_autoptr(CdIcc) icc_xyz = NULL;
	g_autoptr(CdTransform) transform = NULL;

	/* load input */
	icc_xyz = cd_icc_new ();
	hprofile = cmsCreateXYZProfileTHR (cd_icc_get_context (icc_xyz));
	if (!cd_icc_load_handle (icc_xyz, hprofile, CD_ICC_LOAD_FLAGS_ALL, error))
		return FALSE;

	/* load output */
	icc_rgb = cd_icc_new ();
	if (!cd_icc_create_default (icc_rgb, error))
		return FALSE;

	/* create transform */
	transform = cd_transform_new ();
	cd_transform_set_input_icc (transform, icc_xyz);
	cd_transform_set_output_icc (transform, icc_rgb);
	cd_transform_set_rendering_intent (transform, CD_RENDERING_INTENT_PERCEPTUAL);
	cd_transform_set_input_pixel_format (transform, TYPE_XYZ_DBL);
	cd_transform_set_output_pixel_format (transform, TYPE_RGB_DBL);
	return cd_transform_process (transform, xyz, rgb, 1, 1, 1, NULL, error);
}

static void
ch_spectro_update_ui_results (ChSpectroPrivate *priv)
{
	CdColorRGB rgb;
	CdColorXYZ xyz;
	GtkWidget *w;
	gdouble cct;
	g_autofree gchar *cct_str = NULL;
	g_autofree gchar *xyz_str = NULL;
	g_autoptr(CdIt8) cmf = NULL;
	g_autoptr(CdSpectrum) illuminant = NULL;
	g_autoptr(CdSpectrum) sp = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GFile) file = NULL;

	/* nothing set */
	if (priv->sp_raw_last == NULL)
		return;
	if (priv->dark_cal == NULL)
		return;

	/* dark offset */
	sp = cd_spectrum_subtract (priv->sp_raw_last, priv->dark_cal, 1);
	cd_spectrum_set_start (sp, priv->spectral_cal[0]);
	cd_spectrum_set_wavelength_cal (sp,
					priv->spectral_cal[1],
					priv->spectral_cal[2],
					priv->spectral_cal[3]);

	cmf = cd_it8_new ();
	file = g_file_new_for_path ("/usr/share/colord/cmf/CIE1931-2deg-XYZ.cmf");
	if (!cd_it8_load_from_file (cmf, file, &error)) {
		g_warning ("failed to load cmf: %s", error->message);
		return;
	}
	illuminant = cd_spectrum_planckian_new (6500);
	if (!cd_it8_utils_calculate_xyz_from_cmf (cmf, illuminant, sp,
						  &xyz, 1, &error)) {
		g_warning ("failed to get XYZ: %s", error->message);
		return;
	}

	/* update XYZ */
	w = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_xyz"));
	xyz_str = g_strdup_printf ("%.1f, %.1f, %.1f",
				   xyz.X * 100.f,
				   xyz.Y * 100.f,
				   xyz.Z * 100.f);
	gtk_label_set_label (GTK_LABEL (w), xyz_str);

	/* update RGB */
	w = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_rgb"));
	if (!ch_spectro_convert_xyz_to_rgb (&xyz, &rgb, &error)) {
		gtk_label_set_label (GTK_LABEL (w), "?, ?, ?");
	} else {
		g_autofree gchar *rgb_str = NULL;
		rgb_str = g_strdup_printf ("%.1f, %.1f, %.1f",
					   rgb.R, rgb.G, rgb.B);
		gtk_label_set_label (GTK_LABEL (w), rgb_str);
		cd_sample_widget_set_color (CD_SAMPLE_WIDGET (priv->sample_widget), &rgb);
	}

	/* update CCT */
	cct = cd_color_xyz_to_cct (&xyz);
	w = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_cct"));
	cct_str = g_strdup_printf ("%.0fK", cct);
	gtk_label_set_label (GTK_LABEL (w), cct_str);
}

static void
ch_spectro_update_ui_graph_output (ChSpectroPrivate *priv)
{
	gdouble nm;
	guint i;
	g_autoptr(CdSpectrum) sp = NULL;
	g_autoptr(CdSpectrum) sp_dark = NULL;
	g_autoptr(CdSpectrum) sp_irradiance = NULL;
	g_autoptr(GPtrArray) array = NULL;
	const guint black_bodies[] = { 6500, 5000, 3200, 0 };
	const guint32 black_bodies_col[] = { 0xdd0000, 0x00dd00, 0x0000dd, 0x0 };

	/* nothing set */
	if (priv->sp_raw_last == NULL)
		return;
	if (priv->dark_cal == NULL)
		return;

	/* remove dark offset */
	if (g_settings_get_boolean (priv->settings, "dark-calibration")) {
		sp_dark = cd_spectrum_subtract (priv->sp_raw_last,
						priv->dark_cal, 1);
		if (cd_spectrum_get_value_min (sp_dark) < 0.f)
			g_warning ("value less than stored dark cal!");
	} else {
		sp_dark = cd_spectrum_dup (priv->sp_raw_last);
	}

	/* apply irradiance cal */
	if (g_settings_get_boolean (priv->settings, "irradiance-calibration")) {
		g_autoptr(CdSpectrum) sp_resampled = NULL;

		/* we have no irradiance calibration */
		if (priv->irradiance_cal == NULL ||
		    cd_spectrum_get_size (priv->irradiance_cal) == 0) {
			g_error ("no irradiance calibration provided");
		}

		/* resample to a linear data space */
		sp_resampled = cd_spectrum_resample (sp_dark,
						     cd_spectrum_get_start (sp_dark),
						     cd_spectrum_get_end (sp_dark),
						     5);

		/* print something for debugging */
		if (g_getenv ("SPECTRO_DEBUG") != NULL) {
			g_autofree gchar *txt = NULL;
			txt = cd_spectrum_to_string (sp_resampled, 180, 20);
			g_print ("RESAMPLED\n%s", txt);
		}

		/* multiply with the irradiance calibration */
		sp_irradiance = cd_spectrum_multiply (sp_resampled, priv->irradiance_cal, 1);

		/* print something for debugging */
		if (g_getenv ("SPECTRO_DEBUG") != NULL) {
			g_autofree gchar *txt = NULL;
			txt = cd_spectrum_to_string (sp_irradiance, 180, 20);
			g_print ("IRRADIANCECAL\n%s", txt);
		}
	} else {
		sp_irradiance = cd_spectrum_dup (sp_dark);
	}

	/* multiply the spectrum with the sensitivity factor */
//	sp = cd_spectrum_multiply (sp_irradiance, priv->sensitivity_cal, 1);
	sp = cd_spectrum_dup (sp_irradiance);

	/* normalize */
	if (g_settings_get_boolean (priv->settings, "normalize"))
		cd_spectrum_normalize_max (sp, 1.0);

	/* add lines */
	array = g_ptr_array_new_with_free_func ((GDestroyNotify) egg_graph_point_free);
	for (nm = cd_spectrum_get_start (priv->sp_raw_last);
	     nm < cd_spectrum_get_end (priv->sp_raw_last);
	     nm += 1) {
		EggGraphPoint *point = egg_graph_point_new ();
		CdColorRGB tmp;
		CdColorRGB8 tmp8;
		point->x = nm;
		point->y = cd_spectrum_get_value_for_nm (sp, nm);
		cd_color_rgb_from_wavelength (&tmp, point->x);
		cd_color_rgb_to_rgb8 (&tmp, &tmp8);
		point->color = (((guint32) tmp8.R) << 16) +
			       (((guint32) tmp8.G) << 8) +
			       ((guint32) tmp8.B);
		g_ptr_array_add (array, point);
	}

	egg_graph_widget_key_legend_clear (EGG_GRAPH_WIDGET (priv->graph_output));
	egg_graph_widget_data_clear (EGG_GRAPH_WIDGET (priv->graph_output));
	egg_graph_widget_data_add (EGG_GRAPH_WIDGET (priv->graph_output),
				EGG_GRAPH_WIDGET_PLOT_POINTS, array);
	egg_graph_widget_key_legend_add (EGG_GRAPH_WIDGET (priv->graph_output),
					 0xffffff,
					 _("Result"));

	/* add black bodies */
	for (i = 0; black_bodies[i] != 0; i++) {
		g_autoptr(GPtrArray) array_plankian = NULL;
		g_autoptr(CdSpectrum) sp_plankian = NULL;
		g_autofree gchar *str = NULL;
		sp_plankian = cd_spectrum_planckian_new_full (black_bodies[i],
							      350, 750, 1);
		cd_spectrum_normalize_max (sp_plankian, 1.0);
		array_plankian = g_ptr_array_new_with_free_func ((GDestroyNotify) egg_graph_point_free);
		for (nm = cd_spectrum_get_start (sp_plankian);
		     nm < cd_spectrum_get_end (sp_plankian);
		     nm += 1) {
			EggGraphPoint *point = egg_graph_point_new ();
			point->x = nm;
			point->y = cd_spectrum_get_value_for_nm (sp_plankian, nm);
			point->color = black_bodies_col[i];
			g_ptr_array_add (array_plankian, point);
		}
		egg_graph_widget_data_add (EGG_GRAPH_WIDGET (priv->graph_output),
					      EGG_GRAPH_WIDGET_PLOT_LINE, array_plankian);
		str = g_strdup_printf ("%uK", black_bodies[i]);
		egg_graph_widget_key_legend_add (EGG_GRAPH_WIDGET (priv->graph_output),
						 black_bodies_col[i],
						 str);
	}
}

static gboolean
ch_spectro_load_sample (ChSpectroPrivate *priv, GError **error)
{
	CdSpectrum *sp;
	g_autoptr(GTimer) timer = g_timer_new ();

	/* no device yet */
	if (priv->device == NULL) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_FAILED,
				     "No device connected");
		return FALSE;
	}

	/* re-get dark cal */
	if (!priv->dark_cal_valid) {
		if (!ch_spetro_refresh_dark_cal (priv, error))
			return FALSE;
	}

	/* re-set integral time */
	if (!priv->integral_time_valid) {
		if (!ch_device_set_integral_time (priv->device,
						  priv->integral_time,
						  priv->cancellable,
						  error))
			return FALSE;
		priv->integral_time_valid = TRUE;
	}

	/* re-set illuminants */
	if (!priv->illum_valid) {
		if (!ch_device_set_illuminants (priv->device,
						priv->illum,
						priv->cancellable,
						error))
			return FALSE;
		priv->illum_valid = TRUE;
	}

	/* take sample */
	g_debug ("taking sample");
	if (!ch_device_take_reading_spectral (priv->device,
					      CH_SPECTRUM_KIND_RAW,
					      priv->cancellable,
					      error)) {
		return FALSE;
	}
	g_debug ("took %.2fms", g_timer_elapsed (timer, NULL) * 1000.f);
	g_timer_reset (timer);

	/* get spectrum */
	g_debug ("getting spectrum");
	sp = ch_device_get_spectrum (priv->device, NULL, error);
	if (sp == NULL)
		return FALSE;

	/* clear last sample and adopt */
	if (priv->sp_raw_last != NULL)
		cd_spectrum_free (priv->sp_raw_last);
	priv->sp_raw_last = sp;

	g_debug ("took %.2fms", g_timer_elapsed (timer, NULL) * 1000.f);
	g_timer_reset (timer);

	/* set calibration on spectrum */
	cd_spectrum_set_start (priv->sp_raw_last, priv->spectral_cal[0]);
	cd_spectrum_set_wavelength_cal (priv->sp_raw_last,
					priv->spectral_cal[1],
					priv->spectral_cal[2],
					priv->spectral_cal[3]);
	return TRUE;
}

static gboolean
ch_spectro_autorange_integration_time (ChSpectroPrivate *priv, GError **error)
{
	gdouble max_value = 0.5;

	max_value = cd_spectrum_get_value_max (priv->sp_raw_last);
	if (max_value > priv->adc_cal_pos && priv->integral_time >= 2) {
		g_debug ("autoranging down");
		priv->integral_time /= 2;
		priv->integral_time_valid = FALSE;
	}
	if (max_value < priv->adc_cal_neg && priv->integral_time < 2500) {
		g_debug ("autoranging up");
		priv->integral_time *= 2;
		priv->integral_time_valid = FALSE;
	}
	return TRUE;
}

static gboolean
ch_spectro_refresh_irradiance_cal (ChSpectroPrivate *priv, GError **error)
{
	CdSpectrum *sp;
	guint i;
	g_autoptr(CdIt8) it8 = NULL;
	g_autoptr(CdSpectrum) sp_black_body = NULL;
	g_autoptr(CdSpectrum) sp_in = NULL;
	g_autoptr(GError) error_local = NULL;

	/* take away dark reading */
	sp_in = cd_spectrum_subtract (priv->sp_raw_last,
				      priv->dark_cal, 1);
//	sp_in = cd_spectrum_dup (priv->sp_raw_last);
	if (g_getenv ("SPECTRO_DEBUG") != NULL) {
		g_autofree gchar *txt = NULL;
		txt = cd_spectrum_to_string (sp_in, 180, 20);
		g_print ("SP-IN\n%s", txt);
	}

	/* create reference spectra for a halogen bulb */
	sp_black_body = cd_spectrum_planckian_new_full (3200,
							cd_spectrum_get_start (sp_in),
							cd_spectrum_get_end (sp_in),
							1);
	cd_spectrum_normalize_max (sp_black_body, 1.f);
	if (g_getenv ("SPECTRO_DEBUG") != NULL) {
		g_autofree gchar *txt = NULL;
		txt = cd_spectrum_to_string (sp_black_body, 180, 20);
		g_print ("BLACKBODY@3200K\n%s", txt);
	}

	/* normalize the sensor result too */
	cd_spectrum_normalize_max (sp_in, 1.f);
	if (g_getenv ("SPECTRO_DEBUG") != NULL) {
		g_autofree gchar *txt = NULL;
		txt = cd_spectrum_to_string (sp_in, 180, 20);
		g_print ("NORMALIZED-SENSOR-RESPONSE\n%s", txt);
	}

	/* resample, calculating the correction curve */
	sp = cd_spectrum_new ();
	cd_spectrum_set_start (sp, cd_spectrum_get_start (sp_in));
	cd_spectrum_set_end (sp, cd_spectrum_get_end (sp_in));
	for (i = cd_spectrum_get_start (sp); i < cd_spectrum_get_end (sp); i += 5) {
		gdouble ref;
		gdouble val;
		ref = cd_spectrum_get_value_for_nm (sp_black_body, i);
		val = cd_spectrum_get_value_for_nm (sp_in, i);
		cd_spectrum_add_value (sp, ref / val);
	}
	cd_spectrum_normalize_max (sp, 1.f);

	/* try to use this to recreate the black body model */
	if (g_getenv ("SPECTRO_DEBUG") != NULL) {
		g_autofree gchar *txt = NULL;
		g_autoptr(CdSpectrum) sp_test = NULL;
		sp_test = cd_spectrum_multiply (sp, sp_in, 5);
		cd_spectrum_normalize_max (sp_test, 1.f);
		txt = cd_spectrum_to_string (sp_test, 180, 20);
		g_print ("CALIBRATED-RESPONSE\n%s", txt);
	}

	/* save locally */
	if (priv->irradiance_cal != NULL)
		cd_spectrum_free (priv->irradiance_cal);
	priv->irradiance_cal = cd_spectrum_dup (sp);
	cd_spectrum_set_id (priv->irradiance_cal, "1");

	/* update UI */
	ch_spectro_update_ui_graph_irradiance (priv);

	/* save to device */
	if (!ch_device_set_spectrum_full (priv->device,
					  CH_SPECTRUM_KIND_IRRADIANCE_CAL,
					  sp, priv->cancellable, error)) {
		return FALSE;
	}

	return TRUE;
}


static gboolean ch_spectro_tick_cb (gpointer user_data);

static void
ch_spectro_sample_tick_start (ChSpectroPrivate *priv)
{
	priv->tick_enabled = TRUE;
	if (priv->tick_id != 0)
		return;
	priv->tick_id = g_timeout_add (50, ch_spectro_tick_cb, priv);
}

static void
ch_spectro_sample_tick_stop (ChSpectroPrivate *priv)
{
	priv->tick_enabled = FALSE;
	if (priv->tick_id == 0)
		return;
	g_source_remove (priv->tick_id);
	priv->tick_id = 0;
}

static gboolean
ch_spectro_tick_cb (gpointer user_data)
{
	ChSpectroPrivate *priv = (ChSpectroPrivate *) user_data;
	g_autoptr(GError) error = NULL;

	/* get from hardware */
	if (!ch_spectro_load_sample (priv, &error)) {
		ch_spectro_error_dialog (priv,
					 "Failed to get sample",
					 error->message);
		priv->tick_id = 0;
		return G_SOURCE_REMOVE;
	}

	/* update the graph */
	ch_spectro_update_ui_graph_raw (priv);
	ch_spectro_update_ui_graph_output (priv);

	/* auto range the integral time */
	if (!ch_spectro_autorange_integration_time (priv, &error)) {
		ch_spectro_error_dialog (priv,
					 "Failed to auto-set integration",
					 error->message);
		priv->tick_id = 0;
		return G_SOURCE_REMOVE;
	}

	ch_spectro_update_ui_refresh (priv);
	ch_spectro_update_ui_results (priv);

	/* schedule again */
	priv->tick_id = 0;
	if (priv->tick_enabled)
		ch_spectro_sample_tick_start (priv);
	return G_SOURCE_REMOVE;
}

static void
ch_spectro_update_ui (ChSpectroPrivate *priv)
{
	GtkWidget *w;

	/* update UI */
	w = GTK_WIDGET (gtk_builder_get_object (priv->builder, "stack_spectro"));
	if (priv->device == NULL) {
		gtk_stack_set_visible_child_name (GTK_STACK (w), "connect");
		if (priv->tick_id != 0)
			g_source_remove (priv->tick_id);
		priv->tick_id = 0;
		return;
	}
	gtk_stack_set_visible_child_name (GTK_STACK (w), "results");

	/* get the periodic poll */
	if (priv->tick_id == 0)
		priv->tick_id = g_timeout_add (200, ch_spectro_tick_cb, priv);

	/* update specific widgets */
	ch_spectro_update_ui_refresh (priv);

	/* set subtitle */
	w = GTK_WIDGET (gtk_builder_get_object (priv->builder, "header"));
	gtk_header_bar_set_subtitle (GTK_HEADER_BAR (w),
				     /* TRANSLATORS: our device */
				     _("Using ColorHug+ v1.1"));
}

static void
ch_spectro_about_activated_cb (GSimpleAction *action,
			       GVariant *parameter,
			       gpointer user_data)
{
	ChSpectroPrivate *priv = (ChSpectroPrivate *) user_data;
	GList *windows;
	GtkIconTheme *icon_theme;
	GtkWindow *parent = NULL;
	const gchar *authors[] = { "Richard Hughes", NULL };
	const gchar *copyright = "Copyright \xc2\xa9 2016 Richard Hughes";
	g_autoptr(GdkPixbuf) logo = NULL;

	windows = gtk_application_get_windows (GTK_APPLICATION (priv->application));
	if (windows)
		parent = windows->data;

	icon_theme = gtk_icon_theme_get_default ();
	logo = gtk_icon_theme_load_icon (icon_theme, "colorhug-spectro", 256, 0, NULL);
	gtk_show_about_dialog (parent,
			       /* TRANSLATORS: this is the title of the about window */
			       "title", _("About ColorHug Spectro Utility"),
			       /* TRANSLATORS: this is the application name */
			       "program-name", _("ColorHug Spectro Utility"),
			       "authors", authors,
			       /* TRANSLATORS: application description */
			       "comments", _("Sample the spectrum continiously."),
			       "copyright", copyright,
			       "license-type", GTK_LICENSE_GPL_2_0,
			       "logo", logo,
			       "translator-credits", _("translator-credits"),
			       "version", VERSION,
			       NULL);
}

static void
ch_spectro_quit_activated_cb (GSimpleAction *action,
			      GVariant *parameter,
			      gpointer user_data)
{
	ChSpectroPrivate *priv = (ChSpectroPrivate *) user_data;
	g_application_quit (G_APPLICATION (priv->application));
}

static GActionEntry actions[] = {
	{ "about", ch_spectro_about_activated_cb, NULL, NULL, NULL },
	{ "quit", ch_spectro_quit_activated_cb, NULL, NULL, NULL }
};

static void
ch_spectro_settings_changed_cb (GSettings *settings,
				const gchar *key,
				ChSpectroPrivate *priv)
{
	ch_spectro_update_ui_graph_output (priv);
}

static void
ch_spectro_dark_clicked_cb (GtkButton *button, ChSpectroPrivate *priv)
{
	priv->dark_cal_valid = FALSE;
}

static void
ch_spectro_irradiance_clicked_cb (GtkButton *button, ChSpectroPrivate *priv)
{
	g_autoptr(GError) error = NULL;
	if (!ch_spectro_refresh_irradiance_cal (priv, &error)) {
		ch_spectro_error_dialog (priv,
					 "Failed to do irradiance calibration",
					 error->message);
	}
}

static void
ch_spectro_sample_clicked_cb (GtkButton *button, ChSpectroPrivate *priv)
{
	g_autoptr(GError) error = NULL;

	/* get from hardware */
	if (!ch_spectro_load_sample (priv, &error)) {
		ch_spectro_error_dialog (priv,
					 "Failed to get sample",
					 error->message);
		return;
	}

	/* update the UI */
	ch_spectro_update_ui_graph_raw (priv);
	ch_spectro_update_ui_graph_output (priv);
	ch_spectro_update_ui_results (priv);
}

static void
ch_spectro_device_added_cb (GUsbContext *context,
			    GUsbDevice *device,
			    ChSpectroPrivate *priv)
{
	g_autoptr(GError) error = NULL;
	if (ch_device_get_mode (device) != CH_DEVICE_MODE_FIRMWARE_PLUS)
		return;

	g_debug ("Found ColorHug device %s", g_usb_device_get_platform_id (device));
	g_set_object (&priv->device, device);

	/* open device */
	if (!ch_device_open_full (priv->device, priv->cancellable, &error)) {
		ch_spectro_error_dialog (priv,
					 "Failed to open device",
					 error->message);
		return;
	}

	/* get the calibration values */
	if (!ch_device_get_ccd_calibration (priv->device,
					    &priv->spectral_cal[0],
					    &priv->spectral_cal[1],
					    &priv->spectral_cal[2],
					    &priv->spectral_cal[3],
					    priv->cancellable,
					    &error)) {
		ch_spectro_error_dialog (priv,
					 "Failed to get spectral calibration",
					 error->message);
		return;
	}
	if (!ch_device_get_adc_calibration_pos (priv->device,
						&priv->adc_cal_pos,
						priv->cancellable,
						&error)) {
		ch_spectro_error_dialog (priv,
					 "Failed to get ADC calibration",
					 error->message);
		return;
	}
	if (!ch_device_get_adc_calibration_neg (priv->device,
						&priv->adc_cal_neg,
						priv->cancellable,
						&error)) {
		ch_spectro_error_dialog (priv,
					 "Failed to get ADC calibration",
					 error->message);
		return;
	}
	priv->adc_cal_pos = 0.75;
	priv->adc_cal_neg = 0.3;

	/* load stored spectra from the device */
	priv->dark_cal =
		ch_device_get_spectrum_full (priv->device,
					     CH_SPECTRUM_KIND_DARK_CAL,
					     priv->cancellable,
					     &error);
	if (priv->dark_cal == NULL) {
		ch_spectro_error_dialog (priv,
					 "Failed to get dark calibration",
					 error->message);
		return;
	}
	cd_spectrum_set_start (priv->dark_cal, priv->spectral_cal[0]);
	cd_spectrum_set_wavelength_cal (priv->dark_cal,
					priv->spectral_cal[1],
					priv->spectral_cal[2],
					priv->spectral_cal[3]);

	priv->irradiance_cal =
		ch_device_get_spectrum_full (priv->device,
					     CH_SPECTRUM_KIND_IRRADIANCE_CAL,
					     priv->cancellable,
					     &error);
	if (priv->irradiance_cal == NULL) {
		ch_spectro_error_dialog (priv,
					 "Failed to get irradiance calibration",
					 error->message);
		return;
	}
	cd_spectrum_set_start (priv->irradiance_cal, priv->spectral_cal[0]);
	cd_spectrum_set_wavelength_cal (priv->irradiance_cal,
					priv->spectral_cal[1],
					priv->spectral_cal[2],
					priv->spectral_cal[3]);

	/* reset to something sane */
	priv->dark_cal_valid = TRUE;
	priv->integral_time = 40; /* ms */
	if (!ch_device_set_integral_time (priv->device,
					  priv->integral_time,
					  priv->cancellable,
					  &error)) {
		ch_spectro_error_dialog (priv,
					 "Failed to set integration",
					 error->message);
		return;
	}
	ch_spectro_update_ui (priv);
	ch_spectro_update_ui_graph_dark (priv);
	ch_spectro_update_ui_graph_irradiance (priv);
}

static void
ch_spectro_device_removed_cb (GUsbContext *context,
			      GUsbDevice *device,
			      ChSpectroPrivate *priv)
{
	if (ch_device_get_mode (device) != CH_DEVICE_MODE_FIRMWARE_PLUS)
		return;
	g_debug ("Removed ColorHug device %s",
		 g_usb_device_get_platform_id (device));
	g_clear_object (&priv->device);
	ch_spectro_update_ui (priv);
}

static void
ch_spectro_illum_switch_cb (GtkSwitch *sw,
			     GParamSpec *pspec,
			     ChSpectroPrivate *priv)
{
	GtkWidget *w;
	priv->illum = CH_ILLUMINANT_NONE;
	w = GTK_WIDGET (gtk_builder_get_object (priv->builder, "switch_illum_wb"));
	if (gtk_switch_get_active (GTK_SWITCH (w)))
		priv->illum |= CH_ILLUMINANT_A;
	w = GTK_WIDGET (gtk_builder_get_object (priv->builder, "switch_illum_uv"));
	if (gtk_switch_get_active (GTK_SWITCH (w)))
		priv->illum |= CH_ILLUMINANT_UV;
	priv->illum_valid = FALSE;
}

static void
ch_spectro_sample_switch_cb (GtkSwitch *sw,
			     GParamSpec *pspec,
			     ChSpectroPrivate *priv)
{
	GtkWidget *w;
	w = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_sample"));
	if (gtk_switch_get_active (sw)) {
		gtk_widget_set_visible (w, FALSE);
		ch_spectro_sample_tick_start (priv);
	} else {
		gtk_widget_set_visible (w, TRUE);
		ch_spectro_sample_tick_stop (priv);
	}
}

static void
ch_spectro_startup_cb (GApplication *application, ChSpectroPrivate *priv)
{
	GtkBox *box;
	GtkWidget *main_window;
	GtkWidget *w;
	gint retval;
	g_autoptr(GError) error = NULL;
	g_autoptr(GdkPixbuf) pixbuf = NULL;

	/* add application menu items */
	g_action_map_add_action_entries (G_ACTION_MAP (application),
					 actions, G_N_ELEMENTS (actions),
					 priv);

	/* get UI */
	priv->builder = gtk_builder_new ();
	retval = gtk_builder_add_from_resource (priv->builder,
						"/com/hughski/ColorHug/Spectro/ch-spectro.ui",
						&error);
	if (retval == 0) {
		g_warning ("failed to load ui: %s", error->message);
		return;
	}

	main_window = GTK_WIDGET (gtk_builder_get_object (priv->builder, "dialog_spectro"));
	gtk_application_add_window (priv->application, GTK_WINDOW (main_window));
	gtk_widget_set_size_request (main_window, 760, 250);

	/* Hide window first so that the dialogue resizes itself without redrawing */
	gtk_widget_hide (main_window);

	/* setup USB image */
	pixbuf = gdk_pixbuf_new_from_resource_at_scale ("/com/hughski/ColorHug/Spectro/usb.svg",
							200, -1, TRUE, &error);
	if (pixbuf == NULL) {
		g_warning ("failed to load usb.svg: %s", error->message);
		return;
	}
	w = GTK_WIDGET (gtk_builder_get_object (priv->builder, "image_usb"));
	gtk_image_set_from_pixbuf (GTK_IMAGE (w), pixbuf);

	/* add results graph */
	box = GTK_BOX (gtk_builder_get_object (priv->builder, "box_view_results"));
	priv->graph_output = egg_graph_widget_new ();
	g_object_set (priv->graph_output,
		      "type-x", EGG_GRAPH_WIDGET_KIND_WAVELENGTH,
		      "type-y", EGG_GRAPH_WIDGET_KIND_FACTOR,
		      "start-x", 350.f,
		      "stop-x", 750.f,
		      "start-y", 0.f,
		      "stop-y", 100.f,
		      "autorange-y", TRUE,
		      "use-grid", TRUE,
		      "use-legend", TRUE,
		      NULL);
	gtk_box_pack_start (box, priv->graph_output, TRUE, TRUE, 0);
	gtk_widget_set_size_request (priv->graph_output, 1200, 600);
	gtk_widget_set_margin_top (priv->graph_output, 18);
	gtk_widget_set_margin_start (priv->graph_output, 18);
	gtk_widget_set_margin_end (priv->graph_output, 18);
	gtk_widget_show (priv->graph_output);

	/* add results graph */
	box = GTK_BOX (gtk_builder_get_object (priv->builder, "box_view_raw"));
	priv->graph_raw = egg_graph_widget_new ();
	g_object_set (priv->graph_raw,
		      "type-x", EGG_GRAPH_WIDGET_KIND_WAVELENGTH,
		      "type-y", EGG_GRAPH_WIDGET_KIND_FACTOR,
		      "start-x", 350.f,
		      "stop-x", 750.f,
		      "use-grid", TRUE,
		      "autorange-y", TRUE,
		      NULL);
	gtk_box_pack_start (box, priv->graph_raw, TRUE, TRUE, 0);
	gtk_widget_set_size_request (priv->graph_raw, 1200, 600);
	gtk_widget_set_margin_top (priv->graph_raw, 18);
	gtk_widget_set_margin_start (priv->graph_raw, 18);
	gtk_widget_set_margin_end (priv->graph_raw, 18);
	gtk_widget_show (priv->graph_raw);

	/* add dark calibration graph */
	box = GTK_BOX (gtk_builder_get_object (priv->builder, "box_view_dark"));
	priv->graph_dark = egg_graph_widget_new ();
	g_object_set (priv->graph_dark,
		      "type-x", EGG_GRAPH_WIDGET_KIND_WAVELENGTH,
		      "type-y", EGG_GRAPH_WIDGET_KIND_FACTOR,
		      "start-x", 350.f,
		      "stop-x", 750.f,
		      "use-grid", TRUE,
		      "autorange-y", TRUE,
		      NULL);
	gtk_box_pack_start (box, priv->graph_dark, TRUE, TRUE, 0);
	gtk_widget_set_size_request (priv->graph_dark, 1200, 600);
	gtk_widget_set_margin_top (priv->graph_dark, 18);
	gtk_widget_set_margin_start (priv->graph_dark, 18);
	gtk_widget_set_margin_end (priv->graph_dark, 18);
	gtk_widget_show (priv->graph_dark);

	/* add irradiance graph */
	box = GTK_BOX (gtk_builder_get_object (priv->builder, "box_view_irradiance"));
	priv->graph_irradiance = egg_graph_widget_new ();
	g_object_set (priv->graph_irradiance,
		      "type-x", EGG_GRAPH_WIDGET_KIND_WAVELENGTH,
		      "type-y", EGG_GRAPH_WIDGET_KIND_FACTOR,
		      "start-x", 350.f,
		      "stop-x", 750.f,
		      "use-grid", TRUE,
		      "autorange-y", TRUE,
		      NULL);
	gtk_box_pack_start (box, priv->graph_irradiance, TRUE, TRUE, 0);
	gtk_widget_set_size_request (priv->graph_irradiance, 1200, 600);
	gtk_widget_set_margin_top (priv->graph_irradiance, 18);
	gtk_widget_set_margin_start (priv->graph_irradiance, 18);
	gtk_widget_set_margin_end (priv->graph_irradiance, 18);
	gtk_widget_show (priv->graph_irradiance);

	/* bind */
	w = GTK_WIDGET (gtk_builder_get_object (priv->builder, "checkbutton_dark"));
	g_settings_bind (priv->settings, "dark-calibration",
			 w, "active", G_SETTINGS_BIND_DEFAULT);
	w = GTK_WIDGET (gtk_builder_get_object (priv->builder, "checkbutton_irradiance"));
	g_settings_bind (priv->settings, "irradiance-calibration",
			 w, "active", G_SETTINGS_BIND_DEFAULT);
	w = GTK_WIDGET (gtk_builder_get_object (priv->builder, "checkbutton_normalize"));
	g_settings_bind (priv->settings, "normalize",
			 w, "active", G_SETTINGS_BIND_DEFAULT);

	/* widgets */
	w = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_dark"));
	g_signal_connect (w, "clicked",
			  G_CALLBACK (ch_spectro_dark_clicked_cb), priv);
	w = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_irradiance"));
	g_signal_connect (w, "clicked",
			  G_CALLBACK (ch_spectro_irradiance_clicked_cb), priv);
	w = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_sample"));
	g_signal_connect (w, "clicked",
			  G_CALLBACK (ch_spectro_sample_clicked_cb), priv);
	gtk_widget_set_visible (w, FALSE);
	w = GTK_WIDGET (gtk_builder_get_object (priv->builder, "switch_sample"));
	g_signal_connect (w, "notify::active",
			  G_CALLBACK (ch_spectro_sample_switch_cb), priv);
	w = GTK_WIDGET (gtk_builder_get_object (priv->builder, "switch_illum_wb"));
	g_signal_connect (w, "notify::active",
			  G_CALLBACK (ch_spectro_illum_switch_cb), priv);
	w = GTK_WIDGET (gtk_builder_get_object (priv->builder, "switch_illum_uv"));
	g_signal_connect (w, "notify::active",
			  G_CALLBACK (ch_spectro_illum_switch_cb), priv);

	box = GTK_BOX (gtk_builder_get_object (priv->builder, "box_results"));
	priv->sample_widget = cd_sample_widget_new ();
	gtk_box_pack_start (box, priv->sample_widget, FALSE, FALSE, 0);
	gtk_widget_show (priv->sample_widget);
	gtk_widget_set_size_request (priv->sample_widget, 200, 100);

	/* set default values */
//	ch_spectro_settings_changed_cb (priv->settings, "gamma", priv);

	/* coldplug devices */
	g_usb_context_enumerate (priv->usb_ctx);

	/* show main UI */
	gtk_widget_show (main_window);
	ch_spectro_update_ui (priv);
}

int
main (int argc, char **argv)
{
	ChSpectroPrivate *priv;
	gboolean verbose = FALSE;
	GOptionContext *context;
	int status = 0;
	g_autoptr(GError) error = NULL;
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
	context = g_option_context_new (_("ColorHug Spectro Utility"));
	g_option_context_add_group (context, gtk_get_option_group (TRUE));
	g_option_context_add_main_entries (context, options, NULL);
	if (!g_option_context_parse (context, &argc, &argv, &error)) {
		/* TRANSLATORS: user has sausages for fingers */
		g_warning ("%s: %s", _("Failed to parse command line options"),
			   error->message);
	}
	g_option_context_free (context);

	priv = g_new0 (ChSpectroPrivate, 1);
	priv->dark_cal_valid = TRUE;
	priv->illum = CH_ILLUMINANT_NONE;
	priv->cancellable = g_cancellable_new ();
	priv->adc_cal_pos = 0.95;
	priv->adc_cal_neg = 0.80;
	priv->tick_enabled = TRUE;
	priv->usb_ctx = g_usb_context_new (NULL);
	g_signal_connect (priv->usb_ctx, "device-added",
			  G_CALLBACK (ch_spectro_device_added_cb), priv);
	g_signal_connect (priv->usb_ctx, "device-removed",
			  G_CALLBACK (ch_spectro_device_removed_cb), priv);
	priv->settings = g_settings_new ("com.hughski.ColorHug.Spectro");
	g_signal_connect (priv->settings, "changed",
			  G_CALLBACK (ch_spectro_settings_changed_cb), priv);
	priv->data = g_ptr_array_new_with_free_func (g_free);

	/* ensure single instance */
	priv->application = gtk_application_new ("com.hughski.ColorHug.Spectro", 0);
	g_signal_connect (priv->application, "startup",
			  G_CALLBACK (ch_spectro_startup_cb), priv);
	g_signal_connect (priv->application, "activate",
			  G_CALLBACK (ch_spectro_activate_cb), priv);
	/* set verbose? */
	if (verbose)
		g_setenv ("G_MESSAGES_DEBUG", "ChClient", FALSE);

	/* load the sensor sensitivity from a file */
	priv->sensitivity_cal = cd_spectrum_new ();
	cd_spectrum_set_start (priv->sensitivity_cal, 0);
	cd_spectrum_set_end (priv->sensitivity_cal, 1000);
	cd_spectrum_add_value (priv->sensitivity_cal, 34); // <- FIXME: this needs to come from the device itself

	/* wait */
	status = g_application_run (G_APPLICATION (priv->application), argc, argv);

	g_cancellable_cancel (priv->cancellable);
	g_object_unref (priv->application);
	if (priv->builder != NULL)
		g_object_unref (priv->builder);
	if (priv->cancellable != NULL)
		g_object_unref (priv->cancellable);
	if (priv->settings != NULL)
		g_object_unref (priv->settings);
	if (priv->usb_ctx != NULL)
		g_object_unref (priv->usb_ctx);
	if (priv->tick_id != 0)
		g_source_remove (priv->tick_id);
	if (priv->dark_cal != NULL)
		cd_spectrum_free (priv->dark_cal);
	if (priv->sp_raw_last != NULL)
		cd_spectrum_free (priv->sp_raw_last);
	if (priv->irradiance_cal != NULL)
		cd_spectrum_free (priv->irradiance_cal);
	if (priv->sensitivity_cal != NULL)
		cd_spectrum_free (priv->sensitivity_cal);
	g_ptr_array_unref (priv->data);
	g_free (priv);
	return status;
}
