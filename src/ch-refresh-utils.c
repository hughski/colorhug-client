/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2006-2012 Richard Hughes <richard@hughsie.com>
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

#include <colord.h>
#include <glib/gi18n.h>

#include "ch-refresh-utils.h"
/**
 * ch_refresh_calc_average:
 **/
gdouble
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
gdouble
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
 * ch_refresh_sort:
 **/
static void
ch_refresh_sort (gdouble *data, guint data_len)
{
	gboolean sorted;
	gdouble tmp;
	guint i;

	/* simple bubble sort */
	do {
		sorted = TRUE;
		for (i = 0; i < data_len - 1; i++) {
			if (data[i] > data[i + 1]) {
				tmp = data[i];
				data[i] = data[i + 1];
				data[i + 1] = tmp;
				sorted = FALSE;
				break;
			}
		}
	} while (!sorted);
}

/**
 * ch_refresh_get_rise:
 *
 * Calculates the rise time from 10% to 90%
 **/
gboolean
ch_refresh_get_rise (CdSpectrum *sp, gdouble *value, gdouble *jitter, GError **error)
{
	gdouble pulse_data[NR_PULSES];
	gdouble tmp;
	guint i;
	guint idx_start;
	guint j;
	guint size;

	/* calcluate the time per sample */
	size = cd_spectrum_get_size (sp) / NR_PULSES;
	if (size == 0) {
		g_set_error_literal (error, 1, 0, "No data");
		return FALSE;
	}

	/* set to default value */
	for (j = 0; j < NR_PULSES; j++)
		pulse_data[j] = -1.f;

	/* work on each pulse in turn */
	for (j = 0; j < NR_PULSES; j++) {
		idx_start = 0;
		for (i = j * size; i < (j + 1) * size; i++) {
			tmp = cd_spectrum_get_value (sp, i);

			/* first time > 10% */
			if (tmp > 0.1 && idx_start == 0) {
				idx_start = i;
				continue;
			}

			/* first time > 90% */
			if (tmp > 0.9 && idx_start > 0) {
				pulse_data[j] = i - idx_start;
				break;
			}
		}
	}

	/* check values */
	for (j = 0; j < NR_PULSES; j++) {
		if (pulse_data[j] < 0.f) {
			g_set_error (error, 1, 0, "No edge on pulse %i", j + 1);
			return FALSE;
		}
	}

	/* multiply by the resolution */
	for (j = 0; j < NR_PULSES; j++)
		pulse_data[j] *= cd_spectrum_get_resolution (sp);

	/* print debugging */
	for (i = 0; i < NR_PULSES; i++)
		g_debug ("peak %i: %f", i + 1, pulse_data[i]);

	/* success */
	if (value != NULL)
		*value = ch_refresh_calc_average (pulse_data, NR_PULSES);
	if (jitter != NULL)
		*jitter = ch_refresh_calc_jitter (pulse_data, NR_PULSES);
	return TRUE;
}

/**
 * ch_refresh_get_fall:
 *
 * Calculates the rise time from 90% to 10%
 **/
gboolean
ch_refresh_get_fall (CdSpectrum *sp, gdouble *value, gdouble *jitter, GError **error)
{
	guint i;
	guint j;
	guint idx_start;
	guint size;
	gdouble pulse_data[NR_PULSES];
	gdouble tmp;

	/* calcluate the time per sample */
	size = cd_spectrum_get_size (sp) / NR_PULSES;
	if (size == 0) {
		g_set_error_literal (error, 1, 0, "No data");
		return FALSE;
	}

	/* set to default value */
	for (j = 0; j < NR_PULSES; j++)
		pulse_data[j] = -1.f;

	/* work on each pulse in turn */
	for (j = 0; j < NR_PULSES; j++) {
		idx_start = 0;
		for (i = j * size; i < (j + 1) * size; i++) {
			tmp = cd_spectrum_get_value (sp, i);

			/* last time > 90% */
			if (tmp > 0.9) {
				idx_start = i;
				continue;
			}

			/* last time > 10% */
			if (tmp < 0.1 && idx_start > 0) {
				pulse_data[j] = i - idx_start;
				idx_start = 0;
			}
		}
	}

	/* check values */
	for (j = 0; j < NR_PULSES; j++) {
		if (pulse_data[j] < 0.f) {
			g_set_error (error, 1, 0, "No edge on pulse %i", j + 1);
			return FALSE;
		}
	}

	/* multiply by the resolution */
	for (j = 0; j < NR_PULSES; j++)
		pulse_data[j] *= cd_spectrum_get_resolution (sp);

	/* print debugging */
	for (i = 0; i < NR_PULSES; i++)
		g_debug ("peak %i: %f", i + 1, pulse_data[i]);

	/* success */
	if (value != NULL)
		*value = ch_refresh_calc_average (pulse_data, NR_PULSES);
	if (jitter != NULL)
		*jitter = ch_refresh_calc_jitter (pulse_data, NR_PULSES);
	return TRUE;
}

/**
 * ch_refresh_get_input_latency:
 *
 * Calculates the input latency to 10%.
 **/
gboolean
ch_refresh_get_input_latency (CdSpectrum *sp, gdouble *value, gdouble *jitter, GError **error)
{
	guint i;
	guint j;
	guint size;
	gdouble pulse_data[NR_PULSES];
	gdouble tmp;

	/* calcluate the time per sample */
	size = cd_spectrum_get_size (sp) / NR_PULSES;
	if (size == 0) {
		g_set_error_literal (error, 1, 0, "No data");
		return FALSE;
	}

	/* set to default value */
	for (j = 0; j < NR_PULSES; j++)
		pulse_data[j] = -1.f;

	/* work on each pulse in turn */
	for (j = 0; j < NR_PULSES; j++) {
		for (i = j * size; i < (j + 1) * size; i++) {
			tmp = cd_spectrum_get_value (sp, i);

			/* first time > 10% */
			if (tmp > 0.1f) {
				pulse_data[j] = i - (j * size);
				break;
			}
		}
	}

	/* check values */
	for (j = 0; j < NR_PULSES; j++) {
		if (pulse_data[j] < 0.f) {
			g_set_error (error, 1, 0, "No edge on pulse %i", j + 1);
			return FALSE;
		}
	}

	/* multiply by the resolution */
	for (j = 0; j < NR_PULSES; j++)
		pulse_data[j] *= cd_spectrum_get_resolution (sp);

	/* print debugging */
	for (i = 0; i < NR_PULSES; i++)
		g_debug ("peak %i: %f", i + 1, pulse_data[i]);

	/* success */
	ch_refresh_sort (pulse_data, NR_PULSES);
	if (value != NULL)
		*value = ch_refresh_calc_average (pulse_data + 1, NR_PULSES - 2);
	if (jitter != NULL)
		*jitter = ch_refresh_calc_jitter (pulse_data + 1, NR_PULSES - 2);
	return TRUE;
}

/**
 * ch_refresh_remove_pwm:
 *
 * Removes any pulses in the spectrum caused by PWM
 **/
gboolean
ch_refresh_remove_pwm (CdSpectrum *sp, GError **error)
{
	guint i;
	guint j;
	guint size;
	gdouble tmp;

	/* calcluate the time per sample */
	size = cd_spectrum_get_size (sp) / NR_PULSES;
	if (size == 0) {
		g_set_error_literal (error, 1, 0, "No data");
		return FALSE;
	}

	/* work on each pulse in turn */
	for (j = 0; j < NR_PULSES; j++) {
		guint pulse_start = 0;
		guint pulse_end = 0;
		gboolean fix_idx = 0;
		gdouble old_value = -1.f;

		for (i = j * size; i < (j + 1) * size; i++) {
			tmp = cd_spectrum_get_value (sp, i);

			/* first time > 10% */
			if (tmp > 0.1f && pulse_start == 0) {
				pulse_start = i;
				continue;
			}

			/* last time > 50% */
			if (tmp > 0.5f) {
				pulse_end = i;
				continue;
			}
		}
		if (pulse_start == 0 || pulse_end == 0) {
			g_set_error (error, 1, 0, "No edge on pulse %i", j + 1);
			return FALSE;
		}

		/* for each point inside the pulse, if the point is less than
		 * previous point, then copy 95% of the previous point value
		 * to this one */
		g_debug ("removing PWM from %i to %i", pulse_start, pulse_end);
		for (i = pulse_start; i < pulse_end; i++) {
			/* if we got 90% the way through without fixing up a
			 * data point then don't bother now */
			tmp = pulse_start + ((gdouble) (pulse_end - pulse_start) * 0.9f);
			if (fix_idx == 0 && i > tmp) {
				g_debug ("no PWM fixup after %i, ignoring", i);
				break;
			}
			tmp = cd_spectrum_get_value (sp, i);
			if (tmp < old_value * 0.95f) {
				cd_spectrum_set_value (sp, i, old_value);
				fix_idx = i;
				continue;
			}
			old_value = tmp * 0.99f;
		}
	}

	return TRUE;
}

/**
 * ch_refresh_result_add:
 **/
void
ch_refresh_result_add (GHashTable *results, const gchar *key, const gchar *value)
{
	g_hash_table_insert (results, g_strdup (key), g_strdup (value));
}

/**
 * ch_refresh_result_set_cct:
 **/
void
ch_refresh_result_set_cct (GHashTable *results, gdouble value)
{
	g_autofree gchar *str = NULL;
	if (value <= 0.f) {
		g_hash_table_remove (results, "label_cct");
		return;
	}
	str = g_strdup_printf ("<b>%uK</b>", (((guint) value) / 100) * 100);
	ch_refresh_result_add (results, "label_cct", str);
}

/**
 * ch_refresh_result_set_lux_white:
 **/
void
ch_refresh_result_set_lux_white (GHashTable *results, gdouble value)
{
	g_autofree gchar *str = NULL;
	if (value <= 0.f) {
		g_hash_table_remove (results, "label_lux_white");
		return;
	}
	str = g_strdup_printf ("<b>%.1f cd/m²</b>", value);
	ch_refresh_result_add (results, "label_lux_white", str);
}

/**
 * ch_refresh_result_set_lux_black:
 **/
void
ch_refresh_result_set_lux_black (GHashTable *results, gdouble value)
{
	g_autofree gchar *str = NULL;
	if (value <= 0.f) {
		g_hash_table_remove (results, "label_lux_black");
		return;
	}
	str = g_strdup_printf ("<b>%.2f cd/m²</b>", value);
	ch_refresh_result_add (results, "label_lux_black", str);
}

/**
 * ch_refresh_result_set_srgb:
 **/
void
ch_refresh_result_set_srgb (GHashTable *results, gdouble value)
{
	g_autofree gchar *str = NULL;
	if (value <= 0.f) {
		g_hash_table_remove (results, "label_coverage_srgb");
		return;
	}
	str = g_strdup_printf ("<b>%.0f%%</b>", value * 100.f);
	ch_refresh_result_add (results, "label_coverage_srgb", str);
}

/**
 * ch_refresh_result_set_adobergb:
 **/
void
ch_refresh_result_set_adobergb (GHashTable *results, gdouble value)
{
	g_autofree gchar *str = NULL;
	if (value <= 0.f) {
		g_hash_table_remove (results, "label_coverage_adobergb");
		return;
	}
	str = g_strdup_printf ("<b>%.0f%%</b>", value * 100.f);
	ch_refresh_result_add (results, "label_coverage_adobergb", str);
}

/**
 * ch_refresh_result_set_refresh:
 **/
void
ch_refresh_result_set_refresh (GHashTable *results, gdouble value)
{
	g_autofree gchar *str = NULL;
	if (value <= 0.f) {
		g_hash_table_remove (results, "label_refresh");
		return;
	}
	str = g_strdup_printf ("<b>%.0f Hz</b>", value);
	ch_refresh_result_add (results, "label_refresh", str);
}

/**
 * ch_refresh_result_set_gamma:
 **/
void
ch_refresh_result_set_gamma (GHashTable *results, gdouble value)
{
	g_autofree gchar *str = NULL;
	if (value <= 0.f) {
		g_hash_table_remove (results, "label_gamma");
		return;
	}
	str = g_strdup_printf ("<b>%.2f</b>", value);
	ch_refresh_result_add (results, "label_gamma", str);
}
