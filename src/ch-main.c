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

#include "ch-client.h"

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
 * ch_util_set_firmware_ver:
 **/
static gboolean
ch_util_set_firmware_ver (ChUtilPrivate *priv, gchar **values, GError **error)
{
	gboolean ret;
	guint16 major, minor, micro;

	/* parse */
	if (g_strv_length (values) != 3) {
		ret = FALSE;
		g_set_error_literal (error, 1, 0,
				     "invalid input, expect 'value'");
		goto out;
	}
	major = atoi (values[0]);
	minor = atoi (values[1]);
	micro = atoi (values[2]);

	/* set to HW */
	ret = ch_client_set_firmware_ver (priv->client,
					  major,
					  minor,
					  micro,
					  error);
	if (!ret)
		goto out;
out:
	return ret;
}

/**
 * ch_util_get_calibration:
 **/
static gboolean
ch_util_get_calibration (ChUtilPrivate *priv, gchar **values, GError **error)
{
	gboolean ret;
	gfloat *calibration = NULL;
	guint i, j;

	/* get from HW */
	ret = ch_client_get_calibration (priv->client, &calibration, error);
	if (!ret)
		goto out;
	for (j = 0; j < 3; j++) {
		g_print ("( ");
		for (i = 0; i < 3; i++) {
			g_print ("%.3f\t", calibration[j*3 + i]);
		}
		g_print (")\n");
	}
out:
	g_free (calibration);
	return ret;
}

/**
 * ch_util_set_calibration:
 **/
static gboolean
ch_util_set_calibration (ChUtilPrivate *priv, gchar **values, GError **error)
{
	gboolean ret;
	gfloat calibration[9];
	guint i;

	/* parse */
	if (g_strv_length (values) != 9) {
		ret = FALSE;
		g_set_error_literal (error, 1, 0,
				     "invalid input, expect 'value'");
		goto out;
	}
	for (i = 0; i < 9; i++)
		calibration[i] = atof (values[i]);

	/* set to HW */
	ret = ch_client_set_calibration (priv->client, calibration, error);
	if (!ret)
		goto out;
out:
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
	serial_number = atoi (values[0]);

	/* set to HW */
	ret = ch_client_set_serial_number (priv->client, serial_number, error);
	if (!ret)
		goto out;
out:
	return ret;
}

/**
 * ch_util_get_write_protect:
 **/
static gboolean
ch_util_get_write_protect (ChUtilPrivate *priv, gchar **values, GError **error)
{
	gboolean ret;
	gboolean write_protect;

	/* get from HW */
	ret = ch_client_get_write_protect (priv->client, &write_protect, error);
	if (!ret)
		goto out;
	g_print ("%s\n", write_protect ? "1" : "0");
out:
	return ret;
}

/**
 * ch_util_set_write_protect:
 **/
static gboolean
ch_util_set_write_protect (ChUtilPrivate *priv, gchar **values, GError **error)
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
	ret = ch_client_set_write_protect (priv->client, values[0], error);
	if (!ret)
		goto out;
out:
	return ret;
}

/**
 * ch_util_take_reading:
 **/
static gboolean
ch_util_take_reading (ChUtilPrivate *priv, gchar **values, GError **error)
{
	gboolean ret;
	guint16 take_reading;

	/* get from HW */
	ret = ch_client_take_reading (priv->client, &take_reading, error);
	if (!ret)
		goto out;
	g_print ("%i\n", take_reading);
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
		     "set-firmware-version",
		     /* TRANSLATORS: command description */
		     _("Sets the sensor firmware version"),
		     ch_util_set_firmware_ver);
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
		     "get-write-protect",
		     /* TRANSLATORS: command description */
		     _("Gets the sensor wrote protect status"),
		     ch_util_get_write_protect);
	ch_util_add (priv->cmd_array,
		     "set-write-protect",
		     /* TRANSLATORS: command description */
		     _("Sets the sensor write protect status"),
		     ch_util_set_write_protect);
	ch_util_add (priv->cmd_array,
		     "take-reading",
		     /* TRANSLATORS: command description */
		     _("Takes a reading"),
		     ch_util_take_reading);

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

