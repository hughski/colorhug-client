/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2011 Richard Hughes <richard@hughsie.com>
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
#include <gio/gio.h>
#include <locale.h>
#include <stdlib.h>
#include <lcms2.h>
#include <math.h>

#include "ch-client.h"
#include "ch-math.h"

typedef struct {
	ChClient		*client;
	GOptionContext		*context;
	GPtrArray		*cmd_array;
} ChUtilPrivate;

typedef gboolean (*ChUtilPrivateCb)	(ChUtilPrivate	*util,
					 gchar		**values,
					 GError		**error);

typedef struct {
	gchar		*name;
	gchar		*description;
	ChUtilPrivateCb	 callback;
} ChUtilItem;

/**
 * ch_util_item_free:
 **/
static void
ch_util_item_free (ChUtilItem *item)
{
	g_free (item->name);
	g_free (item->description);
	g_free (item);
}

/*
 * cd_sort_command_name_cb:
 */
static gint
cd_sort_command_name_cb (ChUtilItem **item1, ChUtilItem **item2)
{
	return g_strcmp0 ((*item1)->name, (*item2)->name);
}

/**
 * ch_util_add:
 **/
static void
ch_util_add (GPtrArray *array, const gchar *name, const gchar *description, ChUtilPrivateCb callback)
{
	gchar **names;
	guint i;
	ChUtilItem *item;

	/* add each one */
	names = g_strsplit (name, ",", -1);
	for (i=0; names[i] != NULL; i++) {
		item = g_new0 (ChUtilItem, 1);
		item->name = g_strdup (names[i]);
		if (i == 0) {
			item->description = g_strdup (description);
		} else {
			/* TRANSLATORS: this is a command alias */
			item->description = g_strdup_printf (_("Alias to %s"),
							     names[0]);
		}
		item->callback = callback;
		g_ptr_array_add (array, item);
	}
	g_strfreev (names);
}

/**
 * ch_util_get_descriptions:
 **/
static gchar *
ch_util_get_descriptions (GPtrArray *array)
{
	guint i;
	guint j;
	guint len;
	guint max_len = 0;
	ChUtilItem *item;
	GString *string;

	/* get maximum command length */
	for (i=0; i<array->len; i++) {
		item = g_ptr_array_index (array, i);
		len = strlen (item->name);
		if (len > max_len)
			max_len = len;
	}

	/* ensure we're spaced by at least this */
	if (max_len < 19)
		max_len = 19;

	/* print each command */
	string = g_string_new ("");
	for (i=0; i<array->len; i++) {
		item = g_ptr_array_index (array, i);
		g_string_append (string, "  ");
		g_string_append (string, item->name);
		len = strlen (item->name);
		for (j=len; j<max_len+3; j++)
			g_string_append_c (string, ' ');
		g_string_append (string, item->description);
		g_string_append_c (string, '\n');
	}

	/* remove trailing newline */
	if (string->len > 0)
		g_string_set_size (string, string->len - 1);

	return g_string_free (string, FALSE);
}

/**
 * ch_util_run:
 **/
static gboolean
ch_util_run (ChUtilPrivate *priv, const gchar *command, gchar **values, GError **error)
{
	gboolean ret = FALSE;
	guint i;
	ChUtilItem *item;
	GString *string;

	/* find command */
	for (i=0; i<priv->cmd_array->len; i++) {
		item = g_ptr_array_index (priv->cmd_array, i);
		if (g_strcmp0 (item->name, command) == 0) {
			ret = item->callback (priv, values, error);
			goto out;
		}
	}

	/* not found */
	string = g_string_new ("");
	/* TRANSLATORS: error message */
	g_string_append_printf (string, "%s\n",
				_("Command not found, valid commands are:"));
	for (i=0; i<priv->cmd_array->len; i++) {
		item = g_ptr_array_index (priv->cmd_array, i);
		g_string_append_printf (string, " * %s\n", item->name);
	}
	g_set_error_literal (error, 1, 0, string->str);
	g_string_free (string, TRUE);
out:
	return ret;
}

/**
 * ch_util_get_color_select:
 **/
static gboolean
ch_util_get_color_select (ChUtilPrivate *priv, gchar **values, GError **error)
{
	gboolean ret;
	ChColorSelect color_select;

	/* get from HW */
	ret = ch_client_get_color_select (priv->client, &color_select, error);
	if (!ret)
		goto out;

	switch (color_select) {
	case CH_COLOR_SELECT_BLUE:
		g_print ("Blue\n");
		break;
	case CH_COLOR_SELECT_RED:
		g_print ("Red\n");
		break;
	case CH_COLOR_SELECT_GREEN:
		g_print ("Green\n");
		break;
	case CH_COLOR_SELECT_WHITE:
		g_print ("White\n");
		break;
	default:
		ret = FALSE;
		g_set_error (error, 1, 0,
			     "invalid color value %i",
			     color_select);
	}
out:
	return ret;
}

/**
 * ch_util_get_hardware_version:
 **/
static gboolean
ch_util_get_hardware_version (ChUtilPrivate *priv, gchar **values, GError **error)
{
	gboolean ret;
	guint8 hw_version = 0;

	/* get from HW */
	ret = ch_client_get_hardware_version (priv->client,
					      &hw_version,
					      error);
	if (!ret)
		goto out;

	switch (hw_version) {
	case 0x00:
		g_print ("Prototype Hardware\n");
		break;
	default:
		g_print ("Hardware Version %i\n", hw_version);
	}
out:
	return ret;
}

/**
 * ch_util_set_color_select:
 **/
static gboolean
ch_util_set_color_select (ChUtilPrivate *priv, gchar **values, GError **error)
{
	gboolean ret;
	ChColorSelect color_select;

	/* parse */
	if (g_strv_length (values) != 1) {
		ret = FALSE;
		g_set_error_literal (error, 1, 0,
				     "invalid input, expect 'color'");
		goto out;
	}
	if (g_strcmp0 (values[0], "red") == 0)
		color_select = CH_COLOR_SELECT_RED;
	else if (g_strcmp0 (values[0], "blue") == 0)
		color_select = CH_COLOR_SELECT_BLUE;
	else if (g_strcmp0 (values[0], "green") == 0)
		color_select = CH_COLOR_SELECT_GREEN;
	else if (g_strcmp0 (values[0], "white") == 0)
		color_select = CH_COLOR_SELECT_WHITE;
	else {
		ret = FALSE;
		g_set_error (error, 1, 0,
			     "invalid input '%s', expect 'red|green|blue|white'",
			     values[0]);
		goto out;
	}

	/* set to HW */
	ret = ch_client_set_color_select (priv->client, color_select, error);
	if (!ret)
		goto out;
out:
	return ret;
}

/**
 * ch_util_get_multiplier:
 **/
static gboolean
ch_util_get_multiplier (ChUtilPrivate *priv, gchar **values, GError **error)
{
	gboolean ret;
	ChFreqScale multiplier;

	/* get from HW */
	ret = ch_client_get_multiplier (priv->client, &multiplier, error);
	if (!ret)
		goto out;

	switch (multiplier) {
	case CH_FREQ_SCALE_0:
		g_print ("0%% (disabled)\n");
		break;
	case CH_FREQ_SCALE_2:
		g_print ("2%%\n");
		break;
	case CH_FREQ_SCALE_20:
		g_print ("20%%\n");
		break;
	case CH_FREQ_SCALE_100:
		g_print ("100%%\n");
		break;
	default:
		ret = FALSE;
		g_set_error (error, 1, 0,
			     "invalid multiplier value %i",
			     multiplier);
	}
out:
	return ret;
}

/**
 * ch_util_set_multiplier:
 **/
static gboolean
ch_util_set_multiplier (ChUtilPrivate *priv, gchar **values, GError **error)
{
	gboolean ret;
	ChFreqScale multiplier;

	/* parse */
	if (g_strv_length (values) != 1) {
		ret = FALSE;
		g_set_error_literal (error, 1, 0,
				     "invalid input, expect 'color'");
		goto out;
	}
	if (g_strcmp0 (values[0], "0") == 0)
		multiplier = CH_FREQ_SCALE_0;
	else if (g_strcmp0 (values[0], "2") == 0)
		multiplier = CH_FREQ_SCALE_2;
	else if (g_strcmp0 (values[0], "20") == 0)
		multiplier = CH_FREQ_SCALE_20;
	else if (g_strcmp0 (values[0], "100") == 0)
		multiplier = CH_FREQ_SCALE_100;
	else {
		ret = FALSE;
		g_set_error (error, 1, 0,
			     "invalid input '%s', expect '0|2|20|100'",
			     values[0]);
		goto out;
	}

	/* set to HW */
	ret = ch_client_set_multiplier (priv->client, multiplier, error);
	if (!ret)
		goto out;
out:
	return ret;
}

/**
 * ch_util_get_integral_time:
 **/
static gboolean
ch_util_get_integral_time (ChUtilPrivate *priv, gchar **values, GError **error)
{
	gboolean ret;
	guint16 integral_time = 0;

	/* get from HW */
	ret = ch_client_get_integral_time (priv->client, &integral_time, error);
	if (!ret)
		goto out;
	g_print ("%i\n", integral_time);
out:
	return ret;
}

/**
 * ch_util_set_integral_time:
 **/
static gboolean
ch_util_set_integral_time (ChUtilPrivate *priv, gchar **values, GError **error)
{
	gboolean ret;
	guint16 integral_time = 0;

	/* parse */
	if (g_strv_length (values) != 1) {
		ret = FALSE;
		g_set_error_literal (error, 1, 0,
				     "invalid input, expect 'value'");
		goto out;
	}
	integral_time = atoi (values[0]);

	/* set to HW */
	ret = ch_client_set_integral_time (priv->client, integral_time, error);
	if (!ret)
		goto out;
out:
	return ret;
}

/**
 * ch_util_get_calibration_map:
 **/
static gboolean
ch_util_get_calibration_map (ChUtilPrivate *priv,
			     gchar **values,
			     GError **error)
{
	gboolean ret;
	guint16 calibration_map[6];
	guint i;

	/* get from HW */
	ret = ch_client_get_calibration_map (priv->client,
					     calibration_map,
					     error);
	if (!ret)
		goto out;
	for (i = 0; i < 6; i++)
		g_print ("%i -> %i\n", i, calibration_map[i]);
out:
	return ret;
}

/**
 * ch_util_set_calibration_map:
 **/
static gboolean
ch_util_set_calibration_map (ChUtilPrivate *priv,
			     gchar **values,
			     GError **error)
{
	gboolean ret;
	guint16 calibration_map[6];
	guint i;

	/* parse */
	if (g_strv_length (values) != 6) {
		ret = FALSE;
		g_set_error_literal (error, 1, 0,
				     "invalid input, expect 'value'");
		goto out;
	}
	for (i = 0; i < 6; i++)
		calibration_map[i] = atoi (values[i]);

	/* set to HW */
	ret = ch_client_set_calibration_map (priv->client,
					     calibration_map,
					     error);
	if (!ret)
		goto out;
out:
	return ret;
}

/**
 * ch_util_get_firmware_ver:
 **/
static gboolean
ch_util_get_firmware_ver (ChUtilPrivate *priv, gchar **values, GError **error)
{
	gboolean ret;
	guint16 major, minor, micro;

	/* get from HW */
	ret = ch_client_get_firmware_ver (priv->client,
					  &major,
					  &minor,
					  &micro,
					  error);
	if (!ret)
		goto out;
	g_print ("%i.%i.%i\n", major, minor, micro);
out:
	return ret;
}

/**
 * ch_util_show_calibration:
 **/
static void
ch_util_show_calibration (const gdouble *calibration)
{
	guint i, j;
	for (j = 0; j < 3; j++) {
		g_print ("( ");
		for (i = 0; i < 3; i++) {
			g_print ("%.2f\t", calibration[j*3 + i]);
		}
		g_print (")\n");
	}
}

/**
 * ch_util_get_calibration:
 **/
static gboolean
ch_util_get_calibration (ChUtilPrivate *priv, gchar **values, GError **error)
{
	gboolean ret;
	gdouble calibration[9];
	guint16 calibration_index = 0;
	gchar description[24];
	guint8 types;

	/* parse */
	if (g_strv_length (values) != 1) {
		ret = FALSE;
		g_set_error_literal (error, 1, 0,
				     "invalid input, expect 'calibration_index'");
		goto out;
	}
	calibration_index = atoi (values[0]);

	/* get from HW */
	ret = ch_client_get_calibration (priv->client,
					 calibration_index,
					 calibration,
					 &types,
					 description,
					 error);
	if (!ret)
		goto out;
	g_print ("index: %i\n", calibration_index);
	g_print ("supports LCD: %i\n", (types & 0x01) > 0);
	g_print ("supports CRT: %i\n", (types & 0x02) > 0);
	g_print ("supports projector: %i\n", (types & 0x04) > 0);
	g_print ("description: %s\n", description);
	ch_util_show_calibration (calibration);
out:
	return ret;
}

/**
 * ch_util_set_calibration:
 **/
static gboolean
ch_util_set_calibration (ChUtilPrivate *priv, gchar **values, GError **error)
{
	gboolean ret;
	gdouble calibration[9];
	guint16 calibration_index = 0;
	guint types = 0;
	guint i;

	/* parse */
	if (g_strv_length (values) != 12) {
		ret = FALSE;
		g_set_error_literal (error, 1, 0,
				     "invalid input, expect 'index' 'types' 'values' 'description'");
		goto out;
	}
	calibration_index = atoi (values[0]);

	/* try to parse magic constants */
	if (g_strstr_len (values[1], -1, "lcd") != NULL)
		types += CH_CALIBRATION_TYPE_LCD;
	if (g_strstr_len (values[1], -1, "crt") != NULL)
		types += CH_CALIBRATION_TYPE_CRT;
	if (g_strstr_len (values[1], -1, "projector") != NULL)
		types += CH_CALIBRATION_TYPE_PROJECTOR;
	if (types == 0) {
		ret = FALSE;
		g_set_error_literal (error, 1, 0,
				     "invalid type, expected 'lcd', 'crt', 'projector'");
		goto out;
	}
	for (i = 0; i < 9; i++)
		calibration[i] = atof (values[i+2]);

	/* check is valid */
	for (i = 0; i < 9; i++) {
		if (calibration[i] > 0x7fff || calibration[i] < -0x7fff) {
			ret = FALSE;
			g_set_error_literal (error, 1, 0,
					     "invalid value, expect -1.0 to +1.0");
			goto out;
		}
	}

	/* check length */
	if (strlen (values[11]) > CH_CALIBRATION_DESCRIPTION_LEN) {
		ret = FALSE;
		g_set_error (error, 1, 0,
			     "decription is limited to %i chars",
			     CH_CALIBRATION_DESCRIPTION_LEN);
		goto out;
	}

	/* set to HW */
	ret = ch_client_set_calibration (priv->client,
					 calibration_index,
					 calibration,
					 types,
					 values[11],
					 error);
	if (!ret)
		goto out;
	ch_util_show_calibration (calibration);
out:
	return ret;
}

/**
 * ch_util_clear_calibration:
 **/
static gboolean
ch_util_clear_calibration (ChUtilPrivate *priv, gchar **values, GError **error)
{
	gboolean ret;
	guint16 calibration_index = 0;

	/* parse */
	if (g_strv_length (values) != 1) {
		ret = FALSE;
		g_set_error_literal (error, 1, 0,
				     "invalid input, expect 'index'");
		goto out;
	}
	calibration_index = atoi (values[0]);

	/* set to HW */
	ret = ch_client_clear_calibration (priv->client,
					   calibration_index,
					   error);
	if (!ret)
		goto out;
out:
	return ret;
}

/**
 * ch_util_list_calibration:
 **/
static gboolean
ch_util_list_calibration (ChUtilPrivate *priv, gchar **values, GError **error)
{
	gboolean ret;
	gchar description[CH_CALIBRATION_DESCRIPTION_LEN];
	GString *string;
	guint16 i;

	string = g_string_new ("");
	for (i = 0; i < CH_CALIBRATION_MAX; i++) {
		ret = ch_client_get_calibration (priv->client,
						 i,
						 NULL,
						 NULL,
						 description,
						 NULL);
		if (ret) {
			g_string_append_printf (string, "%i\t%s\n",
						i, description);
		}
	}

	/* if no matrices */
	if (string->len == 0) {
		ret = FALSE;
		g_set_error_literal (error, 1, 0,
				     "no calibration matrices stored");
		goto out;
	}

	/* print */
	g_print ("Index\tDescription\n%s", string->str);
	ret = TRUE;
out:
	g_string_free (string, FALSE);
	return ret;
}

/**
 * ch_util_set_calibration_ccmx:
 **/
static gboolean
ch_util_set_calibration_ccmx (ChUtilPrivate *priv, gchar **values, GError **error)
{
	cmsHANDLE ccmx = NULL;
	const gchar *description;
	const gchar *sheet_type;
	gboolean ret;
	gchar *ccmx_data = NULL;
	gdouble calibration[9];
	gsize ccmx_size;
	guint16 calibration_index = 0;

	/* parse */
	if (g_strv_length (values) != 2) {
		ret = FALSE;
		g_set_error_literal (error, 1, 0,
				     "invalid input, expect 'index' 'filename'");
		goto out;
	}

	/* load file */
	calibration_index = atoi (values[0]);
	ret = g_file_get_contents (values[1],
				   &ccmx_data,
				   &ccmx_size,
				   error);
	if (!ret)
		goto out;
	ccmx = cmsIT8LoadFromMem (NULL, ccmx_data, ccmx_size);
	if (ccmx == NULL) {
		ret = FALSE;
		g_set_error (error, 1, 0, "Cannot open %s", values[0]);
		goto out;
	}

	/* select correct sheet */
	sheet_type = cmsIT8GetSheetType (ccmx);
	if (g_strcmp0 (sheet_type, "CCMX   ") != 0) {
		ret = FALSE;
		g_set_error (error, 1, 0, "%s is not a CCMX file [%s]",
			     values[0], sheet_type);
		goto out;
	}

	/* get the description from the ccmx file */
	description = CMSEXPORT cmsIT8GetProperty(ccmx, "DISPLAY");
	if (description == NULL) {
		ret = FALSE;
		g_set_error_literal (error, 1, 0,
				     "CCMX file does not have DISPLAY");
		goto out;
	}

	/* get the values */
	calibration[0] = cmsIT8GetDataRowColDbl(ccmx, 0, 0);
	calibration[1] = cmsIT8GetDataRowColDbl(ccmx, 0, 1);
	calibration[2] = cmsIT8GetDataRowColDbl(ccmx, 0, 2);
	calibration[3] = cmsIT8GetDataRowColDbl(ccmx, 1, 0);
	calibration[4] = cmsIT8GetDataRowColDbl(ccmx, 1, 1);
	calibration[5] = cmsIT8GetDataRowColDbl(ccmx, 1, 2);
	calibration[6] = cmsIT8GetDataRowColDbl(ccmx, 2, 0);
	calibration[7] = cmsIT8GetDataRowColDbl(ccmx, 2, 1);
	calibration[8] = cmsIT8GetDataRowColDbl(ccmx, 2, 2);

	/* set to HW */
	ret = ch_client_set_calibration (priv->client,
					 calibration_index,
					 calibration,
					 CH_CALIBRATION_TYPE_ALL,
					 description,
					 error);
	if (!ret)
		goto out;
	ch_util_show_calibration (calibration);
out:
	g_free (ccmx_data);
	if (ccmx != NULL)
		cmsIT8Free (ccmx);
	return ret;
}

/**
 * ch_util_get_serial_number:
 **/
static gboolean
ch_util_get_serial_number (ChUtilPrivate *priv, gchar **values, GError **error)
{
	gboolean ret;
	guint64 serial_number;

	/* get from HW */
	ret = ch_client_get_serial_number (priv->client, &serial_number, error);
	if (!ret)
		goto out;
	g_print ("%li\n", serial_number);
out:
	return ret;
}

/**
 * ch_util_set_serial_number:
 **/
static gboolean
ch_util_set_serial_number (ChUtilPrivate *priv, gchar **values, GError **error)
{
	gboolean ret;
	guint64 serial_number;

	/* parse */
	if (g_strv_length (values) != 1) {
		ret = FALSE;
		g_set_error_literal (error, 1, 0,
				     "invalid input, expect 'value'");
		goto out;
	}
	serial_number = atol (values[0]);

	/* set to HW */
	g_print ("setting serial number to %li\n", serial_number);
	ret = ch_client_set_serial_number (priv->client, serial_number, error);
	if (!ret)
		goto out;
out:
	return ret;
}

/**
 * ch_util_get_leds:
 **/
static gboolean
ch_util_get_leds (ChUtilPrivate *priv, gchar **values, GError **error)
{
	gboolean ret;
	guint8 leds = 0xff;

	/* get from HW */
	ret = ch_client_get_leds (priv->client, &leds, error);
	if (!ret)
		goto out;
	if (leds > 3) {
		ret = FALSE;
		g_set_error (error, 1, 0,
			     "invalid leds value %i",
			     leds);
		goto out;
	}
	g_print ("LEDs: %i\n", leds);
out:
	return ret;
}

/**
 * ch_util_set_leds:
 **/
static gboolean
ch_util_set_leds (ChUtilPrivate *priv, gchar **values, GError **error)
{
	gboolean ret;
	guint8 leds;
	guint8 repeat = 0;
	guint8 time_on = 0x00;
	guint8 time_off = 0x00;

	/* parse */
	if (g_strv_length (values) != 1 &&
	    g_strv_length (values) != 4) {
		ret = FALSE;
		g_set_error_literal (error, 1, 0,
				     "invalid input, expect "
				     "'<leds> <repeat> <time_on> <time_off>' or "
				     "'<leds>'");
		goto out;
	}

	/* get the LEDs value */
	leds = atoi (values[0]);
	if (leds > 3) {
		ret = FALSE;
		g_set_error (error, 1, 0,
			     "invalid leds value %i",
			     leds);
		goto out;
	}

	/* get the optional other parameters */
	if (g_strv_length (values) == 4) {
		repeat = atoi (values[1]);
		time_on = atoi (values[2]);
		time_off = atoi (values[3]);
	}

	/* set to HW */
	ret = ch_client_set_leds (priv->client,
				  leds,
				  repeat,
				  time_on,
				  time_off,
				  error);
	if (!ret)
		goto out;
out:
	return ret;
}

/**
 * ch_util_get_dark_offsets:
 **/
static gboolean
ch_util_get_dark_offsets (ChUtilPrivate *priv, gchar **values, GError **error)
{
	gboolean ret;
	gdouble red, green, blue;

	/* get from HW */
	ret = ch_client_get_dark_offsets (priv->client,
					  &red,
					  &green,
					  &blue,
					  error);
	if (!ret)
		goto out;
	g_print ("R:%.4f G:%.4f B:%.4f\n", red, green, blue);
out:
	return ret;
}

/**
 * ch_util_set_dark_offsets:
 **/
static gboolean
ch_util_set_dark_offsets (ChUtilPrivate *priv, gchar **values, GError **error)
{
	gboolean ret;
	gdouble red, green, blue;

	/* parse */
	if (g_strv_length (values) != 3) {
		ret = FALSE;
		g_set_error_literal (error, 1, 0,
				     "invalid input, expect 'value'");
		goto out;
	}
	red = atof (values[0]);
	green = atof (values[1]);
	blue = atof (values[2]);

	/* set to HW */
	ret = ch_client_set_dark_offsets (priv->client,
					  red,
					  green,
					  blue,
					  error);
	if (!ret)
		goto out;
out:
	return ret;
}

/**
 * ch_util_write_eeprom:
 **/
static gboolean
ch_util_write_eeprom (ChUtilPrivate *priv, gchar **values, GError **error)
{
	gboolean ret;

	/* parse */
	if (g_strv_length (values) != 1) {
		ret = FALSE;
		g_set_error_literal (error, 1, 0,
				     "invalid input, expect 'value'");
		goto out;
	}

	/* set to HW */
	ret = ch_client_write_eeprom (priv->client, values[0], error);
	if (!ret)
		goto out;
out:
	return ret;
}

/**
 * ch_util_take_reading_raw:
 **/
static gboolean
ch_util_take_reading_raw (ChUtilPrivate *priv, gchar **values, GError **error)
{
	gboolean ret;
	guint16 take_reading;

	/* get from HW */
	ret = ch_client_take_reading_raw (priv->client, &take_reading, error);
	if (!ret)
		goto out;
	g_print ("%i\n", take_reading);
out:
	return ret;
}

/**
 * ch_util_take_readings:
 **/
static gboolean
ch_util_take_readings (ChUtilPrivate *priv, gchar **values, GError **error)
{
	gboolean ret;
	gdouble red, green, blue;

	/* get from HW */
	ret = ch_client_take_readings (priv->client,
				       &red,
				       &green,
				       &blue,
				       error);
	if (!ret)
		goto out;
	g_print ("R:%.4f G:%.4f B:%.4f\n", red, green, blue);
out:
	return ret;
}

/**
 * ch_util_take_readings_xyz:
 **/
static gboolean
ch_util_take_readings_xyz (ChUtilPrivate *priv, gchar **values, GError **error)
{
	gboolean ret;
	gdouble red, green, blue;
	guint16 calibration_index = 0;

	/* parse */
	if (g_strv_length (values) != 1) {
		ret = FALSE;
		g_set_error_literal (error, 1, 0,
				     "invalid input, expect 'calibration_index'");
		goto out;
	}
	calibration_index = atoi (values[0]);

	/* get from HW */
	ret = ch_client_take_readings_xyz (priv->client,
					   calibration_index,
					   &red,
					   &green,
					   &blue,
					   error);
	if (!ret)
		goto out;
	g_print ("R:%.4f G:%.4f B:%.4f\n", red, green, blue);
out:
	return ret;
}

/**
 * ch_util_reset:
 **/
static gboolean
ch_util_reset (ChUtilPrivate *priv, gchar **values, GError **error)
{
	gboolean ret;

	/* this may return with an error */
	ret = ch_client_reset (priv->client,
			       error);
	if (!ret)
		goto out;
out:
	return ret;
}

/**
 * ch_util_flash_firmware:
 **/
static gboolean
ch_util_flash_firmware (ChUtilPrivate *priv, gchar **values, GError **error)
{
	gboolean ret;

	/* parse */
	if (g_strv_length (values) != 1) {
		ret = FALSE;
		g_set_error_literal (error, 1, 0,
				     "invalid input, expect 'filename'");
		goto out;
	}

	/* set to HW */
	ret = ch_client_flash_firmware (priv->client,
					values[0],
					error);
	if (!ret)
		goto out;
out:
	return ret;
}

/**
 * ch_util_get_pre_scale:
 **/
static gboolean
ch_util_get_pre_scale (ChUtilPrivate *priv, gchar **values, GError **error)
{
	gboolean ret;
	gdouble pre_scale;

	/* get from HW */
	ret = ch_client_get_pre_scale (priv->client,
				       &pre_scale,
				       error);
	if (!ret)
		goto out;
	g_print ("Pre Scale: %f\n", pre_scale);
out:
	return ret;
}

/**
 * ch_util_set_pre_scale:
 **/
static gboolean
ch_util_set_pre_scale (ChUtilPrivate *priv, gchar **values, GError **error)
{
	gboolean ret;
	gdouble pre_scale;

	/* parse */
	if (g_strv_length (values) != 1) {
		ret = FALSE;
		g_set_error_literal (error, 1, 0,
				     "invalid input, expect 'value'");
		goto out;
	}
	pre_scale = atof (values[0]);
	if (pre_scale < -0x7fff || pre_scale > 0x7fff) {
		ret = FALSE;
		g_set_error (error, 1, 0,
			     "invalid post scale value %f",
			     pre_scale);
		goto out;
	}

	/* set to HW */
	ret = ch_client_set_pre_scale (priv->client,
				       pre_scale,
				       error);
	if (!ret)
		goto out;
out:
	return ret;
}

/**
 * ch_util_get_post_scale:
 **/
static gboolean
ch_util_get_post_scale (ChUtilPrivate *priv, gchar **values, GError **error)
{
	gboolean ret;
	gdouble post_scale;

	/* get from HW */
	ret = ch_client_get_post_scale (priv->client,
					&post_scale,
					error);
	if (!ret)
		goto out;
	g_print ("Post Scale: %f\n", post_scale);
out:
	return ret;
}

/**
 * ch_util_set_post_scale:
 **/
static gboolean
ch_util_set_post_scale (ChUtilPrivate *priv, gchar **values, GError **error)
{
	gboolean ret;
	gdouble post_scale;

	/* parse */
	if (g_strv_length (values) != 1) {
		ret = FALSE;
		g_set_error_literal (error, 1, 0,
				     "invalid input, expect 'value'");
		goto out;
	}
	post_scale = atof (values[0]);
	if (post_scale < -0x7fff || post_scale > 0x7fff) {
		ret = FALSE;
		g_set_error (error, 1, 0,
			     "invalid post scale value %f",
			     post_scale);
		goto out;
	}

	/* set to HW */
	ret = ch_client_set_post_scale (priv->client,
					post_scale,
					error);
	if (!ret)
		goto out;
out:
	return ret;
}

/**
 * ch_util_boot_flash:
 **/
static gboolean
ch_util_boot_flash (ChUtilPrivate *priv, gchar **values, GError **error)
{
	gboolean ret;

	/* set to HW */
	ret = ch_client_boot_flash (priv->client,
				    error);
	if (!ret)
		goto out;
out:
	return ret;
}

/**
 * ch_util_set_flash_success:
 **/
static gboolean
ch_util_set_flash_success (ChUtilPrivate *priv, gchar **values, GError **error)
{
	gboolean ret;
	gboolean flash_success;

	/* parse */
	if (g_strv_length (values) != 1) {
		ret = FALSE;
		g_set_error_literal (error, 1, 0,
				     "invalid input, expect 'value'");
		goto out;
	}
	flash_success = atoi (values[0]);
	if (flash_success > 1) {
		ret = FALSE;
		g_set_error (error, 1, 0,
			     "invalid flash success value %i",
			     flash_success);
		goto out;
	}

	/* set to HW */
	ret = ch_client_set_flash_success (priv->client,
					   flash_success,
					   error);
	if (!ret)
		goto out;
out:
	return ret;
}

/**
 * main:
 **/
int
main (int argc, char *argv[])
{
	gboolean ret;
	GError *error = NULL;
	guint retval = 1;
	ChUtilPrivate *priv;
	gchar *cmd_descriptions = NULL;

	setlocale (LC_ALL, "");

	bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	g_type_init ();

	/* create helper object */
	priv = g_new0 (ChUtilPrivate, 1);

	/* add commands */
	priv->cmd_array = g_ptr_array_new_with_free_func ((GDestroyNotify) ch_util_item_free);
	ch_util_add (priv->cmd_array,
		     "get-color-select",
		     /* TRANSLATORS: command description */
		     _("Gets the sensor color filter"),
		     ch_util_get_color_select);
	ch_util_add (priv->cmd_array,
		     "set-color-select",
		     /* TRANSLATORS: command description */
		     _("Sets the sensor color filter"),
		     ch_util_set_color_select);
	ch_util_add (priv->cmd_array,
		     "get-multiplier",
		     /* TRANSLATORS: command description */
		     _("Gets the sensor multiplier"),
		     ch_util_get_multiplier);
	ch_util_add (priv->cmd_array,
		     "set-multiplier",
		     /* TRANSLATORS: command description */
		     _("Sets the sensor multiplier"),
		     ch_util_set_multiplier);
	ch_util_add (priv->cmd_array,
		     "get-integral-time",
		     /* TRANSLATORS: command description */
		     _("Gets the sensor integral time"),
		     ch_util_get_integral_time);
	ch_util_add (priv->cmd_array,
		     "set-integral-time",
		     /* TRANSLATORS: command description */
		     _("Sets the sensor integral time"),
		     ch_util_set_integral_time);
	ch_util_add (priv->cmd_array,
		     "get-firmware-version",
		     /* TRANSLATORS: command description */
		     _("Gets the sensor firmware version"),
		     ch_util_get_firmware_ver);
	ch_util_add (priv->cmd_array,
		     "get-calibration",
		     /* TRANSLATORS: command description */
		     _("Gets the sensor calibration matrix"),
		     ch_util_get_calibration);
	ch_util_add (priv->cmd_array,
		     "set-calibration",
		     /* TRANSLATORS: command description */
		     _("Sets the sensor calibration matrix"),
		     ch_util_set_calibration);
	ch_util_add (priv->cmd_array,
		     "clear-calibration",
		     /* TRANSLATORS: command description */
		     _("Clear the sensor calibration matrix"),
		     ch_util_clear_calibration);
	ch_util_add (priv->cmd_array,
		     "list-calibration",
		     /* TRANSLATORS: command description */
		     _("List the sensor calibration matrices"),
		     ch_util_list_calibration);
	ch_util_add (priv->cmd_array,
		     "set-calibration-ccmx",
		     /* TRANSLATORS: command description */
		     _("Sets the sensor calibration matrix from a CCMX file"),
		     ch_util_set_calibration_ccmx);
	ch_util_add (priv->cmd_array,
		     "get-serial-number",
		     /* TRANSLATORS: command description */
		     _("Gets the sensor serial number"),
		     ch_util_get_serial_number);
	ch_util_add (priv->cmd_array,
		     "set-serial-number",
		     /* TRANSLATORS: command description */
		     _("Sets the sensor serial number"),
		     ch_util_set_serial_number);
	ch_util_add (priv->cmd_array,
		     "get-leds",
		     /* TRANSLATORS: command description */
		     _("Gets the LED values"),
		     ch_util_get_leds);
	ch_util_add (priv->cmd_array,
		     "set-leds",
		     /* TRANSLATORS: command description */
		     _("Sets the LEDs"),
		     ch_util_set_leds);
	ch_util_add (priv->cmd_array,
		     "get-dark-offsets",
		     /* TRANSLATORS: command description */
		     _("Gets the dark offset values"),
		     ch_util_get_dark_offsets);
	ch_util_add (priv->cmd_array,
		     "set-dark-offsets",
		     /* TRANSLATORS: command description */
		     _("Sets the dark offset values"),
		     ch_util_set_dark_offsets);
	ch_util_add (priv->cmd_array,
		     "write-eeprom",
		     /* TRANSLATORS: command description */
		     _("Writes the EEPROM with updated values"),
		     ch_util_write_eeprom);
	ch_util_add (priv->cmd_array,
		     "take-reading-raw",
		     /* TRANSLATORS: command description */
		     _("Takes a reading"),
		     ch_util_take_reading_raw);
	ch_util_add (priv->cmd_array,
		     "take-readings",
		     /* TRANSLATORS: command description */
		     _("Takes all color readings (to dRGB)"),
		     ch_util_take_readings);
	ch_util_add (priv->cmd_array,
		     "take-readings-xyz",
		     /* TRANSLATORS: command description */
		     _("Takes all color readings (to XYZ)"),
		     ch_util_take_readings_xyz);
	ch_util_add (priv->cmd_array,
		     "reset",
		     /* TRANSLATORS: command description */
		     _("Reset the processor back to the bootloader"),
		     ch_util_reset);
	ch_util_add (priv->cmd_array,
		     "flash-firmware",
		     /* TRANSLATORS: command description */
		     _("Flash firmware into the processor"),
		     ch_util_flash_firmware);
	ch_util_add (priv->cmd_array,
		     "get-pre-scale",
		     /* TRANSLATORS: command description */
		     _("Gets the pre scale constant"),
		     ch_util_get_pre_scale);
	ch_util_add (priv->cmd_array,
		     "set-pre-scale",
		     /* TRANSLATORS: command description */
		     _("Sets the pre scale constant"),
		     ch_util_set_pre_scale);
	ch_util_add (priv->cmd_array,
		     "get-post-scale",
		     /* TRANSLATORS: command description */
		     _("Gets the post scale constant"),
		     ch_util_get_post_scale);
	ch_util_add (priv->cmd_array,
		     "set-post-scale",
		     /* TRANSLATORS: command description */
		     _("Sets the post scale constant"),
		     ch_util_set_post_scale);
	ch_util_add (priv->cmd_array,
		     "set-flash-success",
		     /* TRANSLATORS: command description */
		     _("Sets the flash success"),
		     ch_util_set_flash_success);
	ch_util_add (priv->cmd_array,
		     "boot-flash",
		     /* TRANSLATORS: command description */
		     _("Boots from the bootloader into the firmware"),
		     ch_util_boot_flash);
	ch_util_add (priv->cmd_array,
		     "get-calibration-map",
		     /* TRANSLATORS: command description */
		     _("Gets the sensor calibration map"),
		     ch_util_get_calibration_map);
	ch_util_add (priv->cmd_array,
		     "set-calibration-map",
		     /* TRANSLATORS: command description */
		     _("Sets the sensor calibration map"),
		     ch_util_set_calibration_map);
	ch_util_add (priv->cmd_array,
		     "get-hardware-version",
		     /* TRANSLATORS: command description */
		     _("Gets the hardware version"),
		     ch_util_get_hardware_version);

	/* sort by command name */
	g_ptr_array_sort (priv->cmd_array,
			  (GCompareFunc) cd_sort_command_name_cb);

	/* get a list of the commands */
	priv->context = g_option_context_new (NULL);
	cmd_descriptions = ch_util_get_descriptions (priv->cmd_array);
	g_option_context_set_summary (priv->context, cmd_descriptions);

	/* TRANSLATORS: program name */
	g_set_application_name (_("Color Management"));
	g_option_context_parse (priv->context, &argc, &argv, NULL);

	/* get connection to colord */
	priv->client = ch_client_new ();
	ret = ch_client_load (priv->client, &error);
	if (!ret) {
		/* TRANSLATORS: no colord available */
		g_print ("%s %s\n", _("No connection to device:"),
			 error->message);
		g_error_free (error);
		goto out;
	}

	/* run the specified command */
	ret = ch_util_run (priv, argv[1], (gchar**) &argv[2], &error);
	if (!ret) {
		g_print ("%s\n", error->message);
		g_error_free (error);
		goto out;
	}

	/* success */
	retval = 0;
out:
	if (priv != NULL) {
		g_object_unref (priv->client);
		if (priv->cmd_array != NULL)
			g_ptr_array_unref (priv->cmd_array);
		g_option_context_free (priv->context);
		g_free (priv);
	}
	g_free (cmd_descriptions);
	return retval;
}

