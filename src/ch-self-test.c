/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2014 Richard Hughes <richard@hughsie.com>
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

#include <colorhug.h>
#include <glib-object.h>
#include <math.h>
#include <stdlib.h>

#include "ch-refresh-utils.h"

static gchar *
cd_test_get_filename (const gchar *filename)
{
	gchar *tmp;
	char full_tmp[PATH_MAX];
	g_autofree gchar *path = NULL;
	path = g_build_filename (TESTDATADIR, filename, NULL);
	tmp = realpath (path, full_tmp);
	if (tmp == NULL)
		return NULL;
	return g_strdup (full_tmp);
}

static void
ch_test_refresh_smooth_func (void)
{
	CdSpectrum *sp;
	gboolean ret;
	gdouble jitter = 0.f;
	gdouble value = 0.f;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *filename = NULL;
	g_autoptr(CdIt8) samples = NULL;
	g_autoptr(GFile) file = NULL;

	/* load file */
	samples = cd_it8_new ();
	filename = cd_test_get_filename ("lenovo.ccss");
	g_assert (filename != NULL);
	file = g_file_new_for_path (filename);
	ret = cd_it8_load_from_file (samples, file, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* get the rise time */
	sp = cd_it8_get_spectrum_by_id (samples, "Y");
	cd_spectrum_normalize_max (sp, 1.f);
	ret = ch_refresh_get_rise (sp, &value, &jitter, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert_cmpfloat (fabs (value - 0.020f), <, 0.005f);
	g_assert_cmpfloat (fabs (jitter - 0.f), <, 0.005f);

	/* get the fall time */
	ret = ch_refresh_get_fall (sp, &value, &jitter, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert_cmpfloat (fabs (value - 0.018f), <, 0.05f);
	g_assert_cmpfloat (fabs (jitter - 0.03f), <, 0.05f);

	/* get the input latency */
	ret = ch_refresh_get_input_latency (sp, &value, &jitter, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert_cmpfloat (fabs (value - 0.034f), <, 0.05f);
	g_assert_cmpfloat (fabs (jitter - 0.015f), <, 0.05f);
}

static void
ch_test_refresh_pwm_func (void)
{
	const gchar *filenames[] = { "eco-off.ccss", "eco1.ccss", "eco2.ccss", NULL };
	const gchar *xyz[] = { "X", "Y", "Z", NULL };
	guint i;
	guint j;

	for (i = 0; filenames[i] != NULL; i++) {
		CdSpectrum *sp;
		gboolean ret;
		gdouble jitter = 0.f;
		gdouble value = 0.f;
		g_autoptr(GError) error = NULL;
		g_autofree gchar *filename = NULL;
		g_autoptr(CdIt8) samples = NULL;
		g_autoptr(GFile) file = NULL;

		/* load file */
		samples = cd_it8_new ();
		filename = cd_test_get_filename (filenames[i]);
		g_assert (filename != NULL);
		file = g_file_new_for_path (filename);
		ret = cd_it8_load_from_file (samples, file, &error);
		g_assert_no_error (error);
		g_assert (ret);

		/* get the rise time */
		g_debug ("%s RISE", filenames[i]);
		sp = cd_it8_get_spectrum_by_id (samples, "Y");
		cd_spectrum_normalize_max (sp, 1.f);
		ret = ch_refresh_get_rise (sp, &value, &jitter, &error);
		g_assert_no_error (error);
		g_assert (ret);
		g_assert_cmpfloat (fabs (value - 0.02f), <, 0.005f);
		g_assert_cmpfloat (fabs (jitter - 0.f), <, 0.005f);

		/* get the fall time */
		g_debug ("%s FALL", filenames[i]);
		ret = ch_refresh_get_fall (sp, &value, &jitter, &error);
		g_assert_no_error (error);
		g_assert (ret);
		g_assert_cmpfloat (fabs (value - 0.02f), <, 0.05f);
		g_assert_cmpfloat (fabs (jitter - 0.f), <, 0.005f);

		/* get the input latency */
		g_debug ("%s INPUT", filenames[i]);
		ret = ch_refresh_get_input_latency (sp, &value, &jitter, &error);
		g_assert_no_error (error);
		g_assert (ret);
		g_assert_cmpfloat (fabs (value - 0.05f), <, 0.05f);
		g_assert_cmpfloat (fabs (jitter - 0.0f), <, 0.05f);

		/* remove any PWM */
		for (j = 0; j < 3; j++) {
			sp = cd_it8_get_spectrum_by_id (samples, xyz[j]);
			ret = ch_refresh_remove_pwm (sp, &error);
			g_assert_no_error (error);
			g_assert (ret);
		}
	}
}

int
main (int argc, char **argv)
{
	g_test_init (&argc, &argv, NULL);

	/* only critical and error are fatal */
	g_log_set_fatal_mask (NULL, G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);

	/* tests go here */
	g_test_add_func ("/ChClient/refresh{smooth}", ch_test_refresh_smooth_func);
	g_test_add_func ("/ChClient/refresh{pwm}", ch_test_refresh_pwm_func);

	return g_test_run ();
}

