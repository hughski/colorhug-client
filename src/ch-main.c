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
#include <stdio.h>
#include <math.h>
#include <colord.h>
#include <colorhug.h>
#include <libsoup/soup.h>

typedef struct {
	ChDeviceQueue		*device_queue;
	GOptionContext		*context;
	GPtrArray		*cmd_array;
	GUsbDevice		*device;
	SoupSession		*session;
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
	for (i = 0; i < array->len; i++) {
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
	for (i = 0; i < array->len; i++) {
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
	for (i = 0; i < priv->cmd_array->len; i++) {
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
	for (i = 0; i < priv->cmd_array->len; i++) {
		item = g_ptr_array_index (priv->cmd_array, i);
		g_string_append_printf (string, " * %s\n", item->name);
	}
	g_set_error_literal (error, 1, 0, string->str);
	g_string_free (string, TRUE);
out:
	return ret;
}

/**
 * ch_util_get_prompt:
 **/
static gboolean
ch_util_get_prompt (const gchar *question, gboolean defaultyes)
{
	gboolean ret = FALSE;
	gchar value;

	g_print ("%s %s ",
		 question,
		 defaultyes ? "[Y/n]" : "[N/y]");
	while (TRUE) {
		value = getchar ();
		if (value == 'y' || value == 'Y') {
			ret = TRUE;
			goto out;
		}
		if (value == 'n' || value == 'N') {
			ret = FALSE;
			goto out;
		}
		if (value == '\n') {
			ret = defaultyes;
			goto out;
		}
	}
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
	ChColorSelect color_select = 0;

	/* get from HW */
	ch_device_queue_get_color_select (priv->device_queue,
					  priv->device,
					  &color_select);
	ret = ch_device_queue_process (priv->device_queue,
				       CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
				       NULL,
				       error);
	if (!ret)
		goto out;

	switch (color_select) {
	case CH_COLOR_SELECT_BLUE:
	case CH_COLOR_SELECT_RED:
	case CH_COLOR_SELECT_GREEN:
	case CH_COLOR_SELECT_WHITE:
		g_print ("%s\n", ch_color_select_to_string (color_select));
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
	ch_device_queue_get_hardware_version (priv->device_queue,
					      priv->device,
					      &hw_version);
	ret = ch_device_queue_process (priv->device_queue,
				       CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
				       NULL,
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
 * ch_util_take_reading_array:
 **/
static gboolean
ch_util_take_reading_array (ChUtilPrivate *priv, gchar **values, GError **error)
{
	gboolean ret;
	gdouble ave = 0.0f;
	gint i, j;
	guint8 max = 0;
	guint8 reading_array[30];
	gdouble std_dev = 0.0f;

	/* setup HW */
	ch_device_queue_set_integral_time (priv->device_queue,
					   priv->device,
					   CH_INTEGRAL_TIME_VALUE_MAX);
	ch_device_queue_set_multiplier (priv->device_queue,
					priv->device,
					CH_FREQ_SCALE_100);
	ch_device_queue_set_color_select (priv->device_queue,
					  priv->device,
					  CH_COLOR_SELECT_WHITE);
	ch_device_queue_take_reading_array (priv->device_queue,
					    priv->device,
					    reading_array);
	ret = ch_device_queue_process (priv->device_queue,
				       CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
				       NULL,
				       error);
	if (!ret)
		goto out;

	/* show as a bar graph */
	for (i = 0; i < 30; i++) {
		if (reading_array[i] > max)
			max = reading_array[i];
		ave += reading_array[i];
	}
	ave /= 30;
	for (i = 0; i < 30; i++) {
		g_print ("%i.\t%u\t[",
			 i + 1,
			 reading_array[i]);
		for (j = 0; j < reading_array[i]; j++) {
			if (j == floor (ave)) {
				g_print ("#");
				continue;
			}
			g_print ("*");
		}
		for (j = reading_array[i]; j < max; j++) {
			if (j == floor (ave)) {
				g_print (".");
				continue;
			}
			g_print (" ");
		}
		g_print ("]\n");
	}

	/* print standard deviation */
	for (i = 0; i < 30; i++)
		std_dev += pow (reading_array[i] - ave, 2);
	g_print ("Standard deviation: %.03lf\n", sqrt (std_dev / 60));
out:
	return ret;
}

/**
 * ch_util_remote_profile_download:
 **/
static gboolean
ch_util_remote_profile_download (ChUtilPrivate *priv, gchar **values, GError **error)
{
	ChSha1 remote_hash;
	gboolean ret;
	gchar *filename = NULL;
	gchar *sha1 = NULL;
	gchar *uri = NULL;
	guint status_code;
	SoupMessage *msg = NULL;
	SoupURI *base_uri = NULL;

	/* get the remote hash from the device */
	ch_device_queue_get_remote_hash (priv->device_queue,
					 priv->device,
					 &remote_hash);
	ret = ch_device_queue_process (priv->device_queue,
				       CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
				       NULL,
				       error);
	if (!ret)
		goto out;

	/* print hash */
	sha1 = ch_sha1_to_string (&remote_hash);
	uri = g_strdup_printf ("http://www.hughski.com/uploads/%s.icc", sha1);

	/* GET file */
	base_uri = soup_uri_new (uri);
	msg = soup_message_new_from_uri (SOUP_METHOD_GET, base_uri);
	if (msg == NULL) {
		/* TRANSLATORS: internal error when setting up HTTP request */
		ret = FALSE;
		g_set_error (error, 1, 0,
			     "Failed to setup message");
		goto out;
	}
	status_code = soup_session_send_message (priv->session, msg);
	if (status_code != 200) {
		ret = FALSE;
		g_set_error (error, 1, 0,
			     "Failed to download file %s: %s",
			     uri, msg->reason_phrase);
		goto out;
	}

	/* copy this file into the users default icc folder */
	filename = g_strdup_printf ("%s/%s/%s.icc",
				    g_get_user_data_dir (),
				    "icc",
				    sha1);
	ret = g_file_set_contents (filename,
				   msg->response_body->data,
				   msg->response_body->length,
				   error);
	if (!ret)
		goto out;

	/* print something */
	if (g_strcmp0 (g_getenv ("COLORHUG_OUTPUT"), "plain") == 0)
		g_print ("%s\n", filename);
	else
		g_print ("Copied remote profile into %s\n", filename);
out:
	if (base_uri != NULL)
		soup_uri_free (base_uri);
	if (msg != NULL)
		g_object_unref (msg);
	g_free (sha1);
	g_free (filename);
	g_free (uri);
	return ret;
}

/**
 * ch_util_self_test:
 **/
static gboolean
ch_util_self_test (ChUtilPrivate *priv, gchar **values, GError **error)
{
	gboolean ret;
	ch_device_queue_self_test (priv->device_queue,
				   priv->device);
	ret = ch_device_queue_process (priv->device_queue,
				       CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
				       NULL,
				       error);
	if (!ret)
		goto out;
out:
	return ret;
}

/**
 * ch_util_remote_profile_upload:
 **/
static gboolean
ch_util_remote_profile_upload (ChUtilPrivate *priv, gchar **values, GError **error)
{
	ChSha1 remote_hash;
	const gchar *uri;
	gboolean ret = TRUE;
	gchar *data = NULL;
	gchar *sha1 = NULL;
	gsize length;
	guint status_code;
	SoupBuffer *buffer = NULL;
	SoupMessage *msg = NULL;
	SoupMultipart *multipart = NULL;

	/* parse */
	if (g_strv_length (values) != 1) {
		ret = FALSE;
		g_set_error_literal (error, 1, 0,
				     "invalid input, expect 'filename.icc'");
		goto out;
	}

	/* read file */
	ret = g_file_get_contents (values[0], &data, &length, error);
	if (!ret)
		goto out;

	/* create multipart form and upload file */
	multipart = soup_multipart_new (SOUP_FORM_MIME_TYPE_MULTIPART);
	buffer = soup_buffer_new (SOUP_MEMORY_STATIC, data, length);
	soup_multipart_append_form_file (multipart,
					 "upload",
					 values[0],
					 NULL,
					 buffer);
	msg = soup_form_request_new_from_multipart ("http://www.hughski.com/profile-store.php", multipart);
	status_code = soup_session_send_message (priv->session, msg);
	if (status_code != 201) {
		ret = FALSE;
		g_set_error (error, 1, 0,
			     "Failed to upload file: %s",
			     msg->reason_phrase);
		goto out;
	}
	uri = soup_message_headers_get_one (msg->response_headers, "Location");
	g_debug ("Successfully uploaded to %s", uri);

	/* print something machine readable */
	if (g_strcmp0 (g_getenv ("COLORHUG_OUTPUT"), "plain") == 0)
		g_print ("%s\n", uri);
	else
		g_print ("Uploaded profile to %s\n", uri);

	/* set SHA1 hash to device */
	sha1 = g_compute_checksum_for_data (G_CHECKSUM_SHA1,
					    (const guchar *) data,
					    length);
	g_debug ("Setting %s to device", sha1);
	ret = ch_sha1_parse (sha1, &remote_hash, error);
	if (!ret)
		goto out;
	ch_device_queue_set_remote_hash (priv->device_queue,
					 priv->device,
					 &remote_hash);
	ch_device_queue_write_eeprom (priv->device_queue,
				      priv->device,
				      CH_WRITE_EEPROM_MAGIC);
	ret = ch_device_queue_process (priv->device_queue,
				       CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
				       NULL,
				       error);
	if (!ret)
		goto out;
out:
	if (buffer != NULL)
		soup_buffer_free (buffer);
	if (msg != NULL)
		g_object_unref (msg);
	if (multipart != NULL)
		soup_multipart_free (multipart);
	g_free (data);
	g_free (sha1);
	return ret;
}

/**
 * ch_util_ccmx_upload:
 **/
static gboolean
ch_util_ccmx_upload (ChUtilPrivate *priv, gchar **values, GError **error)
{
	const gchar *uri;
	gboolean ret = TRUE;
	gchar *data = NULL;
	gsize length;
	guint status_code;
	SoupBuffer *buffer = NULL;
	SoupMessage *msg = NULL;
	SoupMultipart *multipart = NULL;

	/* parse */
	if (g_strv_length (values) != 1) {
		ret = FALSE;
		g_set_error_literal (error, 1, 0,
				     "invalid input, expect 'filename.ccmx'");
		goto out;
	}

	/* read file */
	ret = g_file_get_contents (values[0], &data, &length, error);
	if (!ret)
		goto out;

	/* create multipart form and upload file */
	multipart = soup_multipart_new (SOUP_FORM_MIME_TYPE_MULTIPART);
	buffer = soup_buffer_new (SOUP_MEMORY_STATIC, data, length);
	soup_multipart_append_form_file (multipart,
					 "upload",
					 values[0],
					 NULL,
					 buffer);
	msg = soup_form_request_new_from_multipart ("http://www.hughski.com/ccmx-store.php", multipart);
	status_code = soup_session_send_message (priv->session, msg);
	if (!SOUP_STATUS_IS_SUCCESSFUL (status_code)) {
		ret = FALSE;
		g_set_error (error, 1, 0,
			     "Failed to upload file: %s",
			     msg->reason_phrase);
		goto out;
	}
	uri = soup_message_headers_get_one (msg->response_headers, "Location");
	g_debug ("Successfully uploaded to %s", uri);

	/* print something machine readable */
	if (g_strcmp0 (g_getenv ("COLORHUG_OUTPUT"), "plain") == 0)
		g_print ("%s\n", uri);
	else
		g_print ("Uploaded CCMX to %s\n", uri);
out:
	if (buffer != NULL)
		soup_buffer_free (buffer);
	if (msg != NULL)
		g_object_unref (msg);
	if (multipart != NULL)
		soup_multipart_free (multipart);
	g_free (data);
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
	ch_device_queue_set_color_select (priv->device_queue,
					  priv->device,
					  color_select);
	ret = ch_device_queue_process (priv->device_queue,
				       CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
				       NULL,
				       error);
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
	ChFreqScale multiplier = CH_FREQ_SCALE_0;

	/* get from HW */
	ch_device_queue_get_multiplier (priv->device_queue,
					priv->device,
					&multiplier);
	ret = ch_device_queue_process (priv->device_queue,
				       CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
				       NULL,
				       error);
	if (!ret)
		goto out;

	switch (multiplier) {
	case CH_FREQ_SCALE_0:
		g_print ("0%% (disabled)\n");
		break;
	case CH_FREQ_SCALE_2:
	case CH_FREQ_SCALE_20:
	case CH_FREQ_SCALE_100:
		g_print ("%s\n", ch_multiplier_to_string (multiplier));
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
	ch_device_queue_set_multiplier (priv->device_queue,
					priv->device,
					multiplier);
	ret = ch_device_queue_process (priv->device_queue,
				       CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
				       NULL,
				       error);
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
	ch_device_queue_get_integral_time (priv->device_queue,
					   priv->device,
					   &integral_time);
	ret = ch_device_queue_process (priv->device_queue,
				       CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
				       NULL,
				       error);
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
	integral_time = g_ascii_strtoull (values[0], NULL, 10);

	/* set to HW */
	ch_device_queue_set_integral_time (priv->device_queue,
					   priv->device,
					   integral_time);
	ret = ch_device_queue_process (priv->device_queue,
				       CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
				       NULL,
				       error);
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
	ch_device_queue_get_calibration_map (priv->device_queue,
					     priv->device,
					     calibration_map);
	ret = ch_device_queue_process (priv->device_queue,
				       CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
				       NULL,
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
				     "invalid input, expect '#lcd' '#crt' '#projector' '#led' '0' '0'");
		goto out;
	}
	for (i = 0; i < 6; i++)
		calibration_map[i] = g_ascii_strtoull (values[i], NULL, 10);

	/* set to HW */
	ch_device_queue_set_calibration_map (priv->device_queue,
					     priv->device,
					     calibration_map);
	ret = ch_device_queue_process (priv->device_queue,
				       CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
				       NULL,
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
	guint16 major = 0;
	guint16 minor = 0;
	guint16 micro = 0;

	/* get from HW */
	ch_device_queue_get_firmware_ver (priv->device_queue,
					  priv->device,
					  &major,
					  &minor,
					  &micro);
	ret = ch_device_queue_process (priv->device_queue,
				       CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
				       NULL,
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
ch_util_show_calibration (const CdMat3x3 *calibration)
{
	gdouble *calibration_tmp;
	guint i, j;
	calibration_tmp = cd_mat33_get_data (calibration);
	for (j = 0; j < 3; j++) {
		g_print ("( ");
		for (i = 0; i < 3; i++) {
			g_print ("%.2f\t", calibration_tmp[j*3 + i]);
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
	CdMat3x3 calibration;
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
	calibration_index = g_ascii_strtoull (values[0], NULL, 10);

	/* get from HW */
	ch_device_queue_get_calibration (priv->device_queue,
					 priv->device,
					 calibration_index,
					 &calibration,
					 &types,
					 description);
	ret = ch_device_queue_process (priv->device_queue,
				       CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
				       NULL,
				       error);
	if (!ret)
		goto out;

	g_print ("index: %i\n", calibration_index);
	g_print ("supports LCD: %i\n", (types & CH_CALIBRATION_TYPE_LCD) > 0);
	g_print ("supports LED: %i\n", (types & CH_CALIBRATION_TYPE_LED) > 0);
	g_print ("supports CRT: %i\n", (types & CH_CALIBRATION_TYPE_CRT) > 0);
	g_print ("supports projector: %i\n", (types & CH_CALIBRATION_TYPE_PROJECTOR) > 0);
	g_print ("description: %s\n", description);
	ch_util_show_calibration (&calibration);
out:
	return ret;
}

/**
 * ch_util_set_calibration:
 **/
static gboolean
ch_util_set_calibration (ChUtilPrivate *priv, gchar **values, GError **error)
{
	CdMat3x3 calibration;
	gboolean ret;
	gdouble *calibration_tmp;
	guint16 calibration_index = 0;
	guint i;
	guint types = 0;

	/* parse */
	if (g_strv_length (values) != 12) {
		ret = FALSE;
		g_set_error_literal (error, 1, 0,
				     "invalid input, expect 'index' 'types' 'values' 'description'");
		goto out;
	}
	calibration_index = g_ascii_strtoull (values[0], NULL, 10);

	/* try to parse magic constants */
	if (g_strstr_len (values[1], -1, "lcd") != NULL)
		types += CH_CALIBRATION_TYPE_LCD;
	if (g_strstr_len (values[1], -1, "led") != NULL)
		types += CH_CALIBRATION_TYPE_LED;
	if (g_strstr_len (values[1], -1, "crt") != NULL)
		types += CH_CALIBRATION_TYPE_CRT;
	if (g_strstr_len (values[1], -1, "projector") != NULL)
		types += CH_CALIBRATION_TYPE_PROJECTOR;
	if (types == 0) {
		ret = FALSE;
		g_set_error_literal (error, 1, 0,
				     "invalid type, expected 'lcd', 'led', 'crt', 'projector'");
		goto out;
	}
	calibration_tmp = cd_mat33_get_data (&calibration);
	for (i = 0; i < 9; i++)
		calibration_tmp[i] = g_ascii_strtod (values[i+2], NULL);

	/* check is valid */
	for (i = 0; i < 9; i++) {
		if (calibration_tmp[i] > 0x7fff || calibration_tmp[i] < -0x7fff) {
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
	ch_device_queue_set_calibration (priv->device_queue,
					 priv->device,
					 calibration_index,
					 &calibration,
					 types,
					 values[11]);
	ret = ch_device_queue_process (priv->device_queue,
				       CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
				       NULL,
				       error);
	if (!ret)
		goto out;

	ch_util_show_calibration (&calibration);
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
	calibration_index = g_ascii_strtoull (values[0], NULL, 10);

	/* set to HW */
	ch_device_queue_clear_calibration (priv->device_queue,
					   priv->device,
					   calibration_index);
	ret = ch_device_queue_process (priv->device_queue,
				       CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
				       NULL,
				       error);
	if (!ret)
		goto out;

out:
	return ret;
}

/**
 * ch_util_types_to_short_string:
 **/
static gchar *
ch_util_types_to_short_string (guint8 types)
{
	GString *str = g_string_new ("");
	if ((types & CH_CALIBRATION_TYPE_LCD) > 0)
		g_string_append (str, "L");
	if ((types & CH_CALIBRATION_TYPE_CRT) > 0)
		g_string_append (str, "C");
	if ((types & CH_CALIBRATION_TYPE_PROJECTOR) > 0)
		g_string_append (str, "P");
	if ((types & CH_CALIBRATION_TYPE_LED) > 0)
		g_string_append (str, "E");
	return g_string_free (str, FALSE);
}

/**
 * ch_util_list_calibration:
 **/
static gboolean
ch_util_list_calibration (ChUtilPrivate *priv, gchar **values, GError **error)
{
	gboolean ret;
	gchar description[CH_CALIBRATION_DESCRIPTION_LEN];
	gchar *tmp;
	GError *error_local = NULL;
	GString *string;
	guint16 i;
	guint8 types;

	string = g_string_new ("");
	for (i = 0; i < CH_CALIBRATION_MAX; i++) {
		description[0] = '\0';
		ch_device_queue_get_calibration (priv->device_queue,
						 priv->device,
						 i,
						 NULL,
						 &types,
						 description);
		ret = ch_device_queue_process (priv->device_queue,
					       CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
					       NULL,
					       &error_local);
		if (ret) {
			if (description[0] != '\0') {
				tmp = ch_util_types_to_short_string (types);
				g_string_append_printf (string, "%i\t%s [%s]\n",
							i, description, tmp);
				g_free (tmp);
			}
		} else {
			g_debug ("ignoring error: %s", error_local->message);
			g_clear_error (&error_local);
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
	gboolean ret;
	guint16 calibration_index;
	CdIt8 *ccmx = NULL;
	GFile *file = NULL;

	/* parse */
	if (g_strv_length (values) != 2) {
		ret = FALSE;
		g_set_error_literal (error, 1, 0,
				     "invalid input, expect 'index' 'filename'");
		goto out;
	}

	/* load file */
	calibration_index = g_ascii_strtoull (values[0], NULL, 10);

	/* set to HW */
	ccmx = cd_it8_new ();
	file = g_file_new_for_path (values[1]);
	ret = cd_it8_load_from_file (ccmx, file, error);
	if (!ret)
		goto out;
	ret = ch_device_queue_set_calibration_ccmx (priv->device_queue,
						    priv->device,
						    calibration_index,
						    ccmx,
						    error);
	if (!ret)
		goto out;
	ret = ch_device_queue_process (priv->device_queue,
				       CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
				       NULL,
				       error);
	if (!ret)
		goto out;
out:
	if (ccmx != NULL)
		g_object_unref (ccmx);
	if (file != NULL)
		g_object_unref (file);
	return ret;
}

/**
 * ch_util_get_serial_number:
 **/
static gboolean
ch_util_get_serial_number (ChUtilPrivate *priv, gchar **values, GError **error)
{
	gboolean ret;
	guint32 serial_number = G_MAXUINT32;

	/* get from HW */
	ch_device_queue_get_serial_number (priv->device_queue,
					   priv->device,
					   &serial_number);
	ret = ch_device_queue_process (priv->device_queue,
				       CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
				       NULL,
				       error);
	if (!ret)
		goto out;

	g_print ("%06i\n", serial_number);
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
	guint32 serial_number;

	/* parse */
	if (g_strv_length (values) != 1) {
		ret = FALSE;
		g_set_error_literal (error, 1, 0,
				     "invalid input, expect 'value' or 'auto'");
		goto out;
	}
	serial_number = g_ascii_strtoull (values[0], NULL, 10);
	if (serial_number == 0) {
		ret = FALSE;
		g_set_error (error, 1, 0,
			     "serial number is invalid: %i",
			     serial_number);
		goto out;
	}

	/* set to HW */
	g_print ("setting serial number to %i\n", serial_number);
	ch_device_queue_set_serial_number (priv->device_queue,
					   priv->device,
					   serial_number);
	ret = ch_device_queue_process (priv->device_queue,
				       CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
				       NULL,
				       error);
	if (!ret)
		goto out;

out:
	return ret;
}

/**
 * ch_util_get_owner_name:
 **/
static gboolean
ch_util_get_owner_name (ChUtilPrivate *priv, gchar **values, GError **error)
{
	gboolean ret;
	gchar name[CH_OWNER_LENGTH_MAX];

	/* get from HW */
	ch_device_queue_get_owner_name (priv->device_queue,
					priv->device,
					name);
	ret = ch_device_queue_process (priv->device_queue,
				       CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
				       NULL,
				       error);
	if (!ret)
		goto out;

	g_print ("%s\n", name);
out:
	return ret;
}

/**
 * ch_util_set_owner_name:
 **/
static gboolean
ch_util_set_owner_name (ChUtilPrivate *priv, gchar **values, GError **error)
{
	gboolean ret;
	gchar name[CH_OWNER_LENGTH_MAX];

	/* parse */
	if (g_strv_length (values) != 1) {
		ret = FALSE;
		g_set_error_literal (error, 1, 0,
				     "invalid input, expect 'name'");
		goto out;
	}
	if(strlen(values[0]) >= CH_OWNER_LENGTH_MAX) {
		g_print ("truncating name to %d characters\n", CH_OWNER_LENGTH_MAX-1);
	}
	memset(name, 0, CH_OWNER_LENGTH_MAX);
	g_strlcpy(name, values[0], CH_OWNER_LENGTH_MAX);

	/* set to HW */
	g_print ("setting name to %s\n", name);
	ch_device_queue_set_owner_name (priv->device_queue,
					priv->device,
					name);
	ret = ch_device_queue_process (priv->device_queue,
				       CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
				       NULL,
				       error);
	if (!ret)
		goto out;

out:
	return ret;
}

/**
 * ch_util_get_owner_email:
 **/
static gboolean
ch_util_get_owner_email (ChUtilPrivate *priv, gchar **values, GError **error)
{
	gboolean ret;
	gchar email[CH_OWNER_LENGTH_MAX];

	/* get from HW */
	ch_device_queue_get_owner_email (priv->device_queue,
					priv->device,
					email);
	ret = ch_device_queue_process (priv->device_queue,
				       CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
				       NULL,
				       error);
	if (!ret)
		goto out;

	g_print ("%s\n", email);
out:
	return ret;
}

/**
 * ch_util_set_owner_email:
 **/
static gboolean
ch_util_set_owner_email (ChUtilPrivate *priv, gchar **values, GError **error)
{
	gboolean ret;
	gchar email[CH_OWNER_LENGTH_MAX];

	/* parse */
	if (g_strv_length (values) != 1) {
		ret = FALSE;
		g_set_error_literal (error, 1, 0,
				     "invalid input, expect 'email'");
		goto out;
	}
	if(strlen(values[0]) >= CH_OWNER_LENGTH_MAX) {
		g_print ("truncating email to %d characters\n", CH_OWNER_LENGTH_MAX-1);
	}
	memset (email, 0, CH_OWNER_LENGTH_MAX);
	g_strlcpy (email, values[0], CH_OWNER_LENGTH_MAX);

	/* set to HW */
	g_print ("setting email to %s\n", email);
	ch_device_queue_set_owner_email (priv->device_queue,
					priv->device,
					email);
	ret = ch_device_queue_process (priv->device_queue,
				       CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
				       NULL,
				       error);
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
	guint8 leds = 0;

	/* get from HW */
	ch_device_queue_get_leds (priv->device_queue,
				  priv->device,
				  &leds);
	ret = ch_device_queue_process (priv->device_queue,
				       CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
				       NULL,
				       error);
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
	gboolean ret = FALSE;
	guint8 leds = 0;
	guint8 repeat = 0;
	guint8 time_on = 0x00;
	guint8 time_off = 0x00;

	/* parse */
	if (g_strv_length (values) != 1 &&
	    g_strv_length (values) != 4) {
		g_set_error_literal (error, 1, 0,
				     "invalid input, expect "
				     "'<red|green|blue|white|off> <repeat> <time_on> <time_off>' or "
				     "'<leds>'");
		goto out;
	}

	/* get the LEDs value */
	if (g_strstr_len (values[0], -1, "red") != NULL) {
		leds |= CH_STATUS_LED_RED;
		ret = TRUE;
	}
	if (g_strstr_len (values[0], -1, "green") != NULL) {
		leds |= CH_STATUS_LED_GREEN;
		ret = TRUE;
	}
	if (g_strstr_len (values[0], -1, "blue") != NULL) {
		leds |= CH_STATUS_LED_BLUE;
		ret = TRUE;
	}
	if (g_strstr_len (values[0], -1, "white") != NULL) {
		leds |= CH_STATUS_LED_RED |
			CH_STATUS_LED_GREEN |
			CH_STATUS_LED_BLUE;
		ret = TRUE;
	}

	/* nothing recognised */
	if (!ret) {
		g_set_error_literal (error, 1, 0,
				     "invalid input, expect "
				     "'<red|green|blue|white|off>");
		goto out;
	}

	/* get the optional other parameters */
	if (g_strv_length (values) == 4) {
		repeat = g_ascii_strtoull (values[1], NULL, 10);
		time_on = g_ascii_strtoull (values[2], NULL, 10);
		time_off = g_ascii_strtoull (values[3], NULL, 10);
	}

	/* set to HW */
	ch_device_queue_set_leds (priv->device_queue,
				  priv->device,
				  leds,
				  repeat,
				  time_on,
				  time_off);
	ret = ch_device_queue_process (priv->device_queue,
				       CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
				       NULL,
				       error);
	if (!ret)
		goto out;

out:
	return ret;
}

/**
 * ch_util_get_pcb_errata:
 **/
static gboolean
ch_util_get_pcb_errata (ChUtilPrivate *priv, gchar **values, GError **error)
{
	gboolean ret;
	guint16 pcb_errata = CH_PCB_ERRATA_NONE;

	/* get from HW */
	ch_device_queue_get_pcb_errata (priv->device_queue,
					priv->device,
					&pcb_errata);
	ret = ch_device_queue_process (priv->device_queue,
				       CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
				       NULL,
				       error);
	if (!ret)
		goto out;

	if (pcb_errata == 0) {
		g_print ("Errata: none\n");
		goto out;
	}
	if ((pcb_errata & CH_PCB_ERRATA_SWAPPED_LEDS) > 0)
		g_print ("Errata: swapped-leds\n");
	if ((pcb_errata & CH_PCB_ERRATA_NO_WELCOME) > 0)
		g_print ("Errata: no-welcome\n");
out:
	return ret;
}

/**
 * ch_util_set_pcb_errata:
 **/
static gboolean
ch_util_set_pcb_errata (ChUtilPrivate *priv, gchar **values, GError **error)
{
	gboolean ret;
	guint16 pcb_errata = CH_PCB_ERRATA_NONE;

	/* parse */
	if (g_strv_length (values) != 1) {
		ret = FALSE;
		g_set_error_literal (error, 1, 0,
				     "invalid input, expect 'none|swapped-leds'");
		goto out;
	}

	/* swap the green and red LEDs */
	if (g_strstr_len (values[0], -1, "swapped-leds") != NULL) {
		g_print ("Errata: swapped-leds\n");
		pcb_errata += CH_PCB_ERRATA_SWAPPED_LEDS;
	}
	if (g_strstr_len (values[0], -1, "no-welcome") != NULL) {
		g_print ("Errata: no-welcome\n");
		pcb_errata += CH_PCB_ERRATA_NO_WELCOME;
	}

	/* nothing known by this client version */
	if (pcb_errata == CH_PCB_ERRATA_NONE)
		pcb_errata = g_ascii_strtoull (values[0], NULL, 10);

	if (pcb_errata == 0)
		g_print ("Errata: none\n");

	/* set to HW */
	ch_device_queue_set_pcb_errata (priv->device_queue,
					priv->device,
					pcb_errata);
	ch_device_queue_write_eeprom (priv->device_queue,
				      priv->device,
				      CH_WRITE_EEPROM_MAGIC);
	ret = ch_device_queue_process (priv->device_queue,
				       CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
				       NULL,
				       error);
	if (!ret)
		goto out;

out:
	return ret;
}

/**
 * ch_util_get_remote_hash:
 **/
static gboolean
ch_util_get_remote_hash (ChUtilPrivate *priv, gchar **values, GError **error)
{
	ChSha1 remote_hash;
	gboolean ret;
	gchar *tmp = NULL;

	/* get from HW */
	ch_device_queue_get_remote_hash (priv->device_queue,
					 priv->device,
					 &remote_hash);
	ret = ch_device_queue_process (priv->device_queue,
				       CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
				       NULL,
				       error);
	if (!ret)
		goto out;

	/* print hash */
	tmp = ch_sha1_to_string (&remote_hash);
	g_print ("%s\n", tmp);
out:
	g_free (tmp);
	return ret;
}

/**
 * ch_util_set_remote_hash:
 **/
static gboolean
ch_util_set_remote_hash (ChUtilPrivate *priv, gchar **values, GError **error)
{
	gboolean ret;
	ChSha1 remote_hash;

	/* parse */
	if (g_strv_length (values) != 1) {
		ret = FALSE;
		g_set_error_literal (error, 1, 0,
				     "invalid input, expect sha1");
		goto out;
	}

	/* try to parse string */
	ret = ch_sha1_parse (values[0], &remote_hash, error);
	if (!ret)
		goto out;

	/* set to HW */
	ch_device_queue_set_remote_hash (priv->device_queue,
					 priv->device,
					 &remote_hash);
	ch_device_queue_write_eeprom (priv->device_queue,
				      priv->device,
				      CH_WRITE_EEPROM_MAGIC);
	ret = ch_device_queue_process (priv->device_queue,
				       CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
				       NULL,
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
	CdColorRGB value;

	/* get from HW */
	ch_device_queue_get_dark_offsets (priv->device_queue,
					  priv->device,
					  &value);
	ret = ch_device_queue_process (priv->device_queue,
				       CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
				       NULL,
				       error);
	if (!ret)
		goto out;

	g_print ("R:%.5f G:%.5f B:%.5f\n", value.R, value.G, value.B);
out:
	return ret;
}

/**
 * ch_util_set_dark_offsets_auto:
 **/
static gboolean
ch_util_set_dark_offsets_auto (ChUtilPrivate *priv, GError **error)
{
	gboolean ret;
	gdouble post_scale_old = 0.0f;
	CdColorRGB value_old;
	CdColorRGB value_zero;
	CdColorRGB value;

	/* TRANSLATORS: wait for user to press the device into a desk… */
	ret = ch_util_get_prompt (_("Ensure the ColorHug aperture is blocked"), TRUE);
	if (!ret) {
		g_set_error_literal (error, 1, 0,
				     "user declined");
		goto out;
	}

	/* set dark offsets */
	cd_color_rgb_set (&value_zero, 0.0f, 0.0f, 0.0f);

	/* get from HW */
	ch_device_queue_get_dark_offsets (priv->device_queue,
					  priv->device,
					  &value_old);
	ch_device_queue_get_post_scale (priv->device_queue,
					priv->device,
					&post_scale_old);
	ch_device_queue_set_dark_offsets (priv->device_queue,
					  priv->device,
					  &value_zero);
	ch_device_queue_set_integral_time (priv->device_queue,
					   priv->device,
					   CH_INTEGRAL_TIME_VALUE_MAX);
	ch_device_queue_set_post_scale (priv->device_queue,
					priv->device,
					1);
	ch_device_queue_set_multiplier (priv->device_queue,
					priv->device,
					CH_FREQ_SCALE_100);
	ch_device_queue_take_readings (priv->device_queue,
				       priv->device,
				       &value);
	ret = ch_device_queue_process (priv->device_queue,
				       CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
				       NULL,
				       error);
	if (!ret)
		goto out;

	g_print ("Values: R:%.5f G:%.5f B:%.5f\n", value.R, value.G, value.B);

	/* TRANSLATORS: ask before we set these */
	ret = ch_util_get_prompt (_("Set these values as the dark offsets"), FALSE);
	if (!ret) {
		/* restore HW */
		ch_device_queue_set_dark_offsets (priv->device_queue,
						  priv->device,
						  &value_old);
		ch_device_queue_set_post_scale (priv->device_queue,
						priv->device,
						post_scale_old);
		ret = ch_device_queue_process (priv->device_queue,
					       CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
					       NULL,
					       error);
		if (!ret)
			goto out;
		g_set_error_literal (error, 1, 0,
				     "user declined");
		goto out;
	}

	/* set to HW */
	ch_device_queue_set_dark_offsets (priv->device_queue,
					  priv->device,
					  &value);
	ch_device_queue_set_post_scale (priv->device_queue,
					priv->device,
					post_scale_old);
	ch_device_queue_write_eeprom (priv->device_queue,
				      priv->device,
				      CH_WRITE_EEPROM_MAGIC);
	ret = ch_device_queue_process (priv->device_queue,
				       CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
				       NULL,
				       error);
	if (!ret)
		goto out;

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
	CdColorRGB value;

	/* be interactive */
	if (g_strv_length (values) == 0) {
		ret = ch_util_set_dark_offsets_auto (priv, error);
		goto out;
	}

	/* parse */
	if (g_strv_length (values) != 3) {
		ret = FALSE;
		g_set_error_literal (error, 1, 0,
				     "invalid input, expect 'value'");
		goto out;
	}
	value.R = g_ascii_strtod (values[0], NULL);
	value.G = g_ascii_strtod (values[1], NULL);
	value.B = g_ascii_strtod (values[2], NULL);

	/* set to HW */
	ch_device_queue_set_dark_offsets (priv->device_queue,
					  priv->device,
					  &value);
	ret = ch_device_queue_process (priv->device_queue,
				       CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
				       NULL,
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
	ch_device_queue_write_eeprom (priv->device_queue,
				      priv->device,
				      values[0]);
	ret = ch_device_queue_process (priv->device_queue,
				       CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
				       NULL,
				       error);
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
	ChColorSelect color_select = 0;
	ChFreqScale multiplier = 0;
	ChMeasureMode measure_mode = 0;
	gboolean ret;
	guint16 integral_time = 0;
	guint32 take_reading;

	/* get from HW */
	ch_device_queue_get_color_select (priv->device_queue,
					  priv->device,
					  &color_select);
	ch_device_queue_get_multiplier (priv->device_queue,
					priv->device,
					&multiplier);
	ch_device_queue_get_measure_mode (priv->device_queue,
					  priv->device,
					  &measure_mode);
	ch_device_queue_get_integral_time (priv->device_queue,
					   priv->device,
					   &integral_time);
	ch_device_queue_take_reading_raw (priv->device_queue,
					  priv->device,
					  &take_reading);
	ret = ch_device_queue_process (priv->device_queue,
				       CH_DEVICE_QUEUE_PROCESS_FLAGS_CONTINUE_ERRORS,
				       NULL,
				       error);
	if (!ret)
		goto out;

	/* TRANSLATORS: this is the enabled sensor color */
	g_print ("%s:\t\t%s\n", _("Color"),
		 ch_color_select_to_string (color_select));

	/* TRANSLATORS: this is the sensor scale factor */
	g_print ("%s:\t%s\n", _("Multiplier"),
		 ch_multiplier_to_string (multiplier));

	/* TRANSLATORS: this is the measurement mode */
	g_print ("%s:\t%s\n", _("Measure mode"),
		 ch_measure_mode_to_string (measure_mode));

	/* TRANSLATORS: this is the sensor sample time */
	g_print ("%s:\t0x%04x\n", _("Integral"), integral_time);

	/* TRANSLATORS: this is the number of pulses detected */
	g_print ("%s:\t\t%" G_GUINT32_FORMAT "\n", _("Pulses"), take_reading);
out:
	return ret;
}

/**
 * ch_util_take_readings:
 **/
static gboolean
ch_util_take_readings (ChUtilPrivate *priv, gchar **values, GError **error)
{
	CdColorRGB value;
	gboolean ret;
	guint16 integral_time = 0;

	/* get from HW */
	ch_device_queue_set_multiplier (priv->device_queue,
					priv->device,
					CH_FREQ_SCALE_100);
	ch_device_queue_get_integral_time (priv->device_queue,
					   priv->device,
					   &integral_time);
	ch_device_queue_take_readings (priv->device_queue,
				       priv->device,
				       &value);
	ret = ch_device_queue_process (priv->device_queue,
				       CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
				       NULL,
				       error);
	if (!ret)
		goto out;

	/* TRANSLATORS: this is the sensor sample time */
	g_print ("%s:\t0x%04x\n", _("Integral"), integral_time);

	g_print ("R:%.5f G:%.5f B:%.5f\n", value.R, value.G, value.B);
out:
	return ret;
}

/**
 * ch_util_take_readings_xyz:
 **/
static gboolean
ch_util_take_readings_xyz (ChUtilPrivate *priv, gchar **values, GError **error)
{
	CdColorXYZ value;
	ChFreqScale multiplier = 0;
	gboolean ret;
	guint16 calibration_index = 0;
	guint16 integral_time = 0;

	/* parse */
	if (g_strv_length (values) != 1) {
		ret = FALSE;
		g_set_error_literal (error, 1, 0,
				     "invalid input, expect 'calibration_index'");
		goto out;
	}
	calibration_index = g_ascii_strtoull (values[0], NULL, 10);

	/* get from HW */
	ch_device_queue_get_multiplier (priv->device_queue,
					priv->device,
					&multiplier);
	ch_device_queue_get_integral_time (priv->device_queue,
					   priv->device,
					   &integral_time);
	ch_device_queue_take_readings_xyz (priv->device_queue,
					   priv->device,
					   calibration_index,
					   &value);
	ret = ch_device_queue_process (priv->device_queue,
				       CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
				       NULL,
				       error);
	if (!ret)
		goto out;

	/* TRANSLATORS: this is the sensor scale factor */
	g_print ("%s:\t%s\n", _("Multiplier"),
		 ch_multiplier_to_string (multiplier));

	/* TRANSLATORS: this is the sensor sample time */
	g_print ("%s:\t0x%04x\n", _("Integral"), integral_time);

	g_print ("X:%.5f Y:%.5f Z:%.5f\n", value.X, value.Y, value.Z);
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
	ch_device_queue_reset (priv->device_queue,
			       priv->device);
	ret = ch_device_queue_process (priv->device_queue,
				       CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
				       NULL,
				       error);
	if (!ret)
		goto out;

out:
	return ret;
}

/**
 * ch_util_get_default_device:
 **/
static GUsbDevice *
ch_util_get_default_device (GError **error)
{
	gboolean ret;
	GPtrArray *devices;
	guint i;
	GUsbContext *usb_ctx;
	GUsbDevice *device = NULL;
	GUsbDevice *device_success = NULL;
	GUsbDevice *device_tmp;
	GUsbDeviceList *list;

	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* try to find the ColorHug device */
	usb_ctx = g_usb_context_new (NULL);
	list = g_usb_device_list_new (usb_ctx);
	g_usb_device_list_coldplug (list);

	/* ensure we only find one device */
	devices = g_usb_device_list_get_devices (list);
	for (i = 0; i < devices->len; i++) {
		device_tmp = g_ptr_array_index (devices, i);
		if (!ch_device_is_colorhug (device_tmp))
			continue;
		if (device != NULL) {
			g_set_error_literal (error, 1, 0,
					     _("Multiple ColorHug devices are attached"));
			goto out;
		}
		device = g_object_ref (device_tmp);
	}
	if (device == NULL) {
		g_set_error_literal (error, 1, 0,
				     _("No ColorHug devices were found"));
		goto out;
	}
	g_debug ("Found ColorHug device %s",
		 g_usb_device_get_platform_id (device));
	ret = ch_device_open (device, error);
	if (!ret)
		goto out;

	/* success */
	device_success = g_object_ref (device);
out:
	g_object_unref (usb_ctx);
	if (device != NULL)
		g_object_unref (device);
	if (list != NULL)
		g_object_unref (list);
	if (devices != NULL)
		g_ptr_array_unref (devices);
	return device_success;
}

/**
 * ch_util_helper_quit_loop_cb:
 **/
static gboolean
ch_util_helper_quit_loop_cb (gpointer user_data)
{
	GMainLoop *loop = (GMainLoop *) user_data;
	g_main_loop_quit (loop);
	return FALSE;
}

/**
 * ch_util_flash_firmware_internal:
 **/
static gboolean
ch_util_flash_firmware_internal (ChUtilPrivate *priv,
				 const gchar *filename,
				 GError **error)
{
	gboolean ret;
	gchar *data_raw = NULL;
	guint8 *data = NULL;
	gsize len = 0;
	GUsbDevice *device = NULL;
	GMainLoop *loop = NULL;

	g_return_val_if_fail (filename != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* load file */
	ret = g_file_get_contents (filename, &data_raw, &len, error);
	if (!ret)
		goto out;

	/* unpack the hex file to a binary blob if required */
	if (g_str_has_suffix (filename, ".bin")) {
		data = g_memdup (data_raw, len);
	} else if (g_str_has_suffix (filename, ".hex")) {
		ret = ch_inhx32_to_bin (data_raw,
					&data,
					&len,
					error);
		if (!ret)
			goto out;
	} else {
		ret = FALSE;
		g_set_error_literal (error, 1, 0,
				     "invalid file type, expect .bin or .hex");
		goto out;
	}

	/* boot to bootloader */
	ch_device_queue_reset (priv->device_queue, priv->device);
	ret = ch_device_queue_process (priv->device_queue,
				       CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
				       NULL,
				       error);
	if (!ret)
		goto out;

	/* wait for the device to reconnect */
	loop = g_main_loop_new (NULL, FALSE);
	g_timeout_add (CH_FLASH_RECONNECT_TIMEOUT,
		       ch_util_helper_quit_loop_cb,
		       loop);
	g_main_loop_run (loop);
	device = ch_util_get_default_device (error);
	if (device == NULL)
		goto out;

	/* write firmware */
	ch_device_queue_set_flash_success (priv->device_queue,
					   device,
					   0x00);
	ch_device_queue_write_firmware (priv->device_queue,
					device,
					data,
					len);
	ret = ch_device_queue_process (priv->device_queue,
				       CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
				       NULL,
				       error);
	if (!ret)
		goto out;

	/* read firmware */
	ch_device_queue_verify_firmware (priv->device_queue,
					 device,
					 data,
					 len);
	ch_device_queue_boot_flash (priv->device_queue,
				    device);
	ret = ch_device_queue_process (priv->device_queue,
				       CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
				       NULL,
				       error);
	if (!ret)
		goto out;

	/* wait again for the device to reconnect */
	g_object_unref (device);
	g_timeout_add (CH_FLASH_RECONNECT_TIMEOUT,
		       ch_util_helper_quit_loop_cb,
		       loop);
	g_main_loop_run (loop);
	device = ch_util_get_default_device (error);
	if (device == NULL) {
		ret = FALSE;
		goto out;
	}

	/* set flash success true */
	ch_device_queue_set_flash_success (priv->device_queue,
					   device,
					   0x01);
	ret = ch_device_queue_process (priv->device_queue,
				       CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
				       NULL,
				       error);
	if (!ret)
		goto out;
out:
	if (loop != NULL)
		g_main_loop_unref (loop);
	if (device != NULL)
		g_object_unref (device);
	g_free (data);
	g_free (data_raw);
	return ret;
}

/**
 * ch_util_flash_firmware_force:
 **/
static gboolean
ch_util_flash_firmware_force (ChUtilPrivate *priv, gchar **values, GError **error)
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
	ret = ch_util_flash_firmware_internal (priv,
					       values[0],
					       error);
	if (!ret)
		goto out;

	/* print success */
	g_print ("INFO: Flashing was successful.\n");
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

	/* print warning */
	g_print ("WARNING: Do not shutdown the computer or unplug the device.\n");

	/* TRANSLATORS: confirmation */
	ret = ch_util_get_prompt (_("Flash the device?"), FALSE);
	if (!ret) {
		g_set_error_literal (error, 1, 0,
				     "user declined");
		goto out;
	}

	/* set to HW */
	ret = ch_util_flash_firmware_internal (priv,
					       values[0],
					       error);
	if (!ret)
		goto out;

	/* print success */
	g_print ("INFO: Flashing was successful.\n");
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
	gdouble pre_scale = 0.0f;

	/* get from HW */
	ch_device_queue_get_pre_scale (priv->device_queue,
				       priv->device,
				       &pre_scale);
	ret = ch_device_queue_process (priv->device_queue,
				       CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
				       NULL,
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
	pre_scale = g_ascii_strtod (values[0], NULL);
	if (pre_scale < -0x7fff || pre_scale > 0x7fff) {
		ret = FALSE;
		g_set_error (error, 1, 0,
			     "invalid post scale value %f",
			     pre_scale);
		goto out;
	}

	/* set to HW */
	ch_device_queue_set_pre_scale (priv->device_queue,
				       priv->device,
				       pre_scale);
	ret = ch_device_queue_process (priv->device_queue,
				       CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
				       NULL,
				       error);
	if (!ret)
		goto out;

out:
	return ret;
}

/**
 * ch_util_get_adc_vrefs:
 **/
static gboolean
ch_util_get_adc_vrefs (ChUtilPrivate *priv, gchar **values, GError **error)
{
	gboolean ret;
	gdouble vref_neg = 0.0f;
	gdouble vref_pos = 0.0f;

	/* get from HW */
	ch_device_queue_get_adc_vref_neg (priv->device_queue,
					  priv->device,
					  &vref_neg);
	ch_device_queue_get_adc_vref_pos (priv->device_queue,
					  priv->device,
					  &vref_pos);
	ret = ch_device_queue_process (priv->device_queue,
				       CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
				       NULL,
				       error);
	if (!ret)
		goto out;

	g_print ("ADC Vref+: %f Volts\n", vref_pos);
	g_print ("ADC Vref-: %f Volts\n", vref_neg);
out:
	return ret;
}

/**
 * ch_util_get_temperature:
 **/
static gboolean
ch_util_get_temperature (ChUtilPrivate *priv, gchar **values, GError **error)
{
	gboolean ret;
	gdouble temperature = 0.0f;

	/* get from HW */
	ch_device_queue_get_temperature (priv->device_queue,
				         priv->device,
				         &temperature);
	ret = ch_device_queue_process (priv->device_queue,
				       CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
				       NULL,
				       error);
	if (!ret)
		goto out;

	g_print ("Temperature: %f\n", temperature);
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
	gdouble post_scale = 0.0f;

	/* get from HW */
	ch_device_queue_get_post_scale (priv->device_queue,
					priv->device,
					&post_scale);
	ret = ch_device_queue_process (priv->device_queue,
				       CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
				       NULL,
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
	post_scale = g_ascii_strtod (values[0], NULL);
	if (post_scale < -0x7fff || post_scale > 0x7fff) {
		ret = FALSE;
		g_set_error (error, 1, 0,
			     "invalid post scale value %f",
			     post_scale);
		goto out;
	}

	/* set to HW */
	ch_device_queue_set_post_scale (priv->device_queue,
					priv->device,
					post_scale);
	ret = ch_device_queue_process (priv->device_queue,
				       CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
				       NULL,
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
	ch_device_queue_boot_flash (priv->device_queue,
				    priv->device);
	ret = ch_device_queue_process (priv->device_queue,
				       CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
				       NULL,
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
	flash_success = g_ascii_strtoull (values[0], NULL, 10);
	if (flash_success > 1) {
		ret = FALSE;
		g_set_error (error, 1, 0,
			     "invalid flash success value %i",
			     flash_success);
		goto out;
	}

	/* set to HW */
	ch_device_queue_set_flash_success (priv->device_queue,
					   priv->device,
					   flash_success);
	ret = ch_device_queue_process (priv->device_queue,
				       CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
				       NULL,
				       error);
	if (!ret)
		goto out;

out:
	return ret;
}

/**
 * ch_util_eeprom_write:
 **/
static gboolean
ch_util_eeprom_write (ChUtilPrivate *priv, gchar **values, GError **error)
{
	gboolean ret;
	gsize len;
	guint16 address;
	guint8 *data = NULL;
	guint i;

	/* parse */
	if (g_strv_length (values) != 2) {
		ret = FALSE;
		g_set_error_literal (error, 1, 0,
				     "invalid input, expect 'address (base-16)' 'length (base-10)'");
		goto out;
	}

	/* read flash */
	address = g_ascii_strtoull (values[0], NULL, 16);
	if (address < CH_EEPROM_ADDR_RUNCODE) {
		ret = FALSE;
		g_set_error (error, 1, 0,
			     "invalid address 0x%04x",
			     address);
		goto out;
	}
	len = g_ascii_strtoull (values[1], NULL, 10);
	if (len < 1 || len > 60) {
		ret = FALSE;
		g_set_error (error, 1, 0,
			     "invalid length %" G_GSIZE_FORMAT " (1-60)",
			     len);
		goto out;
	}

	/* just write zeros */
	data = g_new0 (guint8, len);
	ch_device_queue_write_flash (priv->device_queue,
				     priv->device,
				     address,
				     data,
				     len);
	ret = ch_device_queue_process (priv->device_queue,
				       CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
				       NULL,
				       error);
	if (!ret)
		goto out;

	/* flush */
	ch_device_queue_write_flash (priv->device_queue,
				     priv->device,
				     address | CH_FLASH_TRANSFER_BLOCK_SIZE,
				     data,
				     len);
	ret = ch_device_queue_process (priv->device_queue,
				       CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
				       NULL,
				       error);
	if (!ret)
		goto out;

	g_print ("Wrote:\n");
	for (i = 0; i < len; i++)
		g_print ("0x%04x = %02x\n", address + i, data[i]);
out:
	g_free (data);
	return ret;
}

/**
 * ch_util_eeprom_erase:
 **/
static gboolean
ch_util_eeprom_erase (ChUtilPrivate *priv, gchar **values, GError **error)
{
	gboolean ret;
	gsize len;
	guint16 address;

	/* parse */
	if (g_strv_length (values) != 2) {
		ret = FALSE;
		g_set_error_literal (error, 1, 0,
				     "invalid input, expect 'address (base-16)' 'length (base-10)'");
		goto out;
	}

	/* read flash */
	address = g_ascii_strtoull (values[0], NULL, 16);
	if (address < CH_EEPROM_ADDR_RUNCODE) {
		ret = FALSE;
		g_set_error (error, 1, 0,
			     "invalid address 0x%04x",
			     address);
		goto out;
	}
	len = g_ascii_strtoull (values[1], NULL, 10);
	if (len < 1 || len > 0xffff) {
		ret = FALSE;
		g_set_error (error, 1, 0,
			     "invalid length %" G_GSIZE_FORMAT " (1-60)",
			     len);
		goto out;
	}
	ch_device_queue_erase_flash (priv->device_queue,
				     priv->device,
				     address,
				     len);
	ret = ch_device_queue_process (priv->device_queue,
				       CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
				       NULL,
				       error);
	if (!ret)
		goto out;

out:
	return ret;
}

/**
 * ch_util_eeprom_read:
 **/
static gboolean
ch_util_eeprom_read (ChUtilPrivate *priv, gchar **values, GError **error)
{
	gboolean ret;
	gsize len;
	guint16 address;
	guint8 *data = NULL;
	guint i;

	/* parse */
	if (g_strv_length (values) != 2) {
		ret = FALSE;
		g_set_error_literal (error, 1, 0,
				     "invalid input, expect 'address (base-16)' 'length (base-10)'");
		goto out;
	}

	/* read flash */
	address = g_ascii_strtoull (values[0], NULL, 16);
	if (address < CH_EEPROM_ADDR_RUNCODE) {
		ret = FALSE;
		g_set_error (error, 1, 0,
			     "invalid address 0x%04x",
			     address);
		goto out;
	}
	len = g_ascii_strtoull (values[1], NULL, 10);
	if (len < 1 || len > 60) {
		ret = FALSE;
		g_set_error (error, 1, 0,
			     "invalid length %" G_GSIZE_FORMAT " (1-60)",
			     len);
		goto out;
	}
	data = g_new0 (guint8, len);
	ch_device_queue_read_flash (priv->device_queue,
				    priv->device,
				    address,
				    data,
				    len);
	ret = ch_device_queue_process (priv->device_queue,
				       CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
				       NULL,
				       error);
	if (!ret)
		goto out;

	g_print ("Read:\n");
	for (i = 0; i < len; i++)
		g_print ("0x%04x = %02x\n", address + i, data[i]);
out:
	g_free (data);
	return ret;
}

/**
 * ch_util_get_measure_mode:
 **/
static gboolean
ch_util_get_measure_mode (ChUtilPrivate *priv, gchar **values, GError **error)
{
	gboolean ret;
	ChMeasureMode measure_mode = CH_MEASURE_MODE_FREQUENCY;

	/* get from HW */
	ch_device_queue_get_measure_mode (priv->device_queue,
					  priv->device,
					  &measure_mode);
	ret = ch_device_queue_process (priv->device_queue,
				       CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
				       NULL,
				       error);
	if (!ret)
		goto out;

	switch (measure_mode) {
	case CH_MEASURE_MODE_FREQUENCY:
	case CH_MEASURE_MODE_DURATION:
		g_print ("%s\n", ch_measure_mode_to_string (measure_mode));
		break;
	default:
		ret = FALSE;
		g_set_error (error, 1, 0,
			     "invalid measure_mode value %i",
			     measure_mode);
	}
out:
	return ret;
}

/**
 * ch_util_set_measure_mode:
 **/
static gboolean
ch_util_set_measure_mode (ChUtilPrivate *priv, gchar **values, GError **error)
{
	gboolean ret;
	ChMeasureMode measure_mode;

	/* parse */
	if (g_strv_length (values) != 1) {
		ret = FALSE;
		g_set_error_literal (error, 1, 0,
				     "invalid input, expect 'frequency|duration'");
		goto out;
	}
	if (g_strcmp0 (values[0], "frequency") == 0)
		measure_mode = CH_MEASURE_MODE_FREQUENCY;
	else if (g_strcmp0 (values[0], "duration") == 0)
		measure_mode = CH_MEASURE_MODE_DURATION;
	else {
		ret = FALSE;
		g_set_error (error, 1, 0,
			     "invalid input '%s', expect 'frequency|duration'",
			     values[0]);
		goto out;
	}

	/* set to HW */
	ch_device_queue_set_measure_mode (priv->device_queue,
					  priv->device,
					  measure_mode);
	ret = ch_device_queue_process (priv->device_queue,
				       CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
				       NULL,
				       error);
	if (!ret)
		goto out;

out:
	return ret;
}

/**
 * ch_util_sram_write:
 **/
static gboolean
ch_util_sram_write (ChUtilPrivate *priv, gchar **values, GError **error)
{
	gboolean ret;
	gsize len;
	guint32 address;
	guint8 *data = NULL;
	guint i;

	/* parse */
	if (g_strv_length (values) != 2) {
		ret = FALSE;
		g_set_error_literal (error, 1, 0,
				     "invalid input, expect 'address (base-16)' 'length (base-10)'");
		goto out;
	}

	/* read sram */
	address = g_ascii_strtoull (values[0], NULL, 16);
	if (address > 0xffff) {
		ret = FALSE;
		g_set_error (error, 1, 0,
			     "invalid address 0x%04x",
			     address);
		goto out;
	}
	len = g_ascii_strtoull (values[1], NULL, 10);
	if (len < 1) {
		ret = FALSE;
		g_set_error_literal (error, 1, 0,
				     "invalid length");
		goto out;
	}

	/* just write zeros */
	data = g_new0 (guint8, len);
	for (i = 0; i < len; i++)
		data[i] = g_random_int_range (0x00, 0xff);
	ch_device_queue_write_sram (priv->device_queue,
				    priv->device,
				    (guint16) address,
				    data,
				    len);
	ret = ch_device_queue_process (priv->device_queue,
				       CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
				       NULL,
				       error);
	if (!ret)
		goto out;

	g_print ("Wrote:\n");
	for (i = 0; i < len; i++)
		g_print ("0x%04x = %02x\n", address + i, data[i]);
out:
	g_free (data);
	return ret;
}

/**
 * ch_util_sram_read:
 **/
static gboolean
ch_util_sram_read (ChUtilPrivate *priv, gchar **values, GError **error)
{
	gboolean ret;
	gsize len;
	guint32 address;
	guint8 *data = NULL;
	guint i;

	/* parse */
	if (g_strv_length (values) != 2) {
		ret = FALSE;
		g_set_error_literal (error, 1, 0,
				     "invalid input, expect 'address (base-16)' 'length (base-10)'");
		goto out;
	}

	/* read sram */
	address = g_ascii_strtoull (values[0], NULL, 16);
	if (address > 0xffff) {
		ret = FALSE;
		g_set_error (error, 1, 0,
			     "invalid address 0x%04x",
			     address);
		goto out;
	}
	len = g_ascii_strtoull (values[1], NULL, 10);
	if (len < 1) {
		ret = FALSE;
		g_set_error (error, 1, 0,
			     "invalid length %" G_GSIZE_FORMAT,
			     len);
		goto out;
	}
	data = g_new0 (guint8, len);
	ch_device_queue_read_sram (priv->device_queue,
				   priv->device,
				   (guint16) address,
				   data,
				   len);
	ret = ch_device_queue_process (priv->device_queue,
				       CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
				       NULL,
				       error);
	if (!ret)
		goto out;

	g_print ("Read:\n");
	for (i = 0; i < len; i++)
		g_print ("0x%04x = %02x\n", address + i, data[i]);
out:
	g_free (data);
	return ret;
}

/**
 * ch_util_ignore_cb:
 **/
static void
ch_util_ignore_cb (const gchar *log_domain, GLogLevelFlags log_level,
		   const gchar *message, gpointer user_data)
{
}

/**
 * main:
 **/
int
main (int argc, char *argv[])
{
	ChUtilPrivate *priv;
	gboolean ret;
	gboolean verbose = FALSE;
	gchar *cmd_descriptions = NULL;
	GError *error = NULL;
	guint retval = 1;
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
		     _("Gets the sensor sample read time"),
		     ch_util_get_integral_time);
	ch_util_add (priv->cmd_array,
		     "set-integral-time",
		     /* TRANSLATORS: command description */
		     _("Sets the sensor sample read time"),
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
		     "get-owner-name",
		     /* TRANSLATORS: command description */
		     _("Gets the owner's name"),
		     ch_util_get_owner_name);
	ch_util_add (priv->cmd_array,
		     "set-owner-name",
		     /* TRANSLATORS: command description */
		     _("Sets the owner's name"),
		     ch_util_set_owner_name);
	ch_util_add (priv->cmd_array,
		     "get-owner-email",
		     /* TRANSLATORS: command description */
		     _("Gets the owner's email address"),
		     ch_util_get_owner_email);
	ch_util_add (priv->cmd_array,
		     "set-owner-email",
		     /* TRANSLATORS: command description */
		     _("Sets the owner's email address"),
		     ch_util_set_owner_email);
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
		     "get-pcb-errata",
		     /* TRANSLATORS: command description */
		     _("Gets the PCB errata"),
		     ch_util_get_pcb_errata);
	ch_util_add (priv->cmd_array,
		     "set-pcb-errata",
		     /* TRANSLATORS: command description */
		     _("Sets the PCB errata"),
		     ch_util_set_pcb_errata);
	ch_util_add (priv->cmd_array,
		     "get-remote-hash",
		     /* TRANSLATORS: command description */
		     _("Gets the remote profile SHA1 hash"),
		     ch_util_get_remote_hash);
	ch_util_add (priv->cmd_array,
		     "set-remote-hash",
		     /* TRANSLATORS: command description */
		     _("Sets the remote profile SHA1 hash"),
		     ch_util_set_remote_hash);
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
		     _("Takes all color readings (to device RGB)"),
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
		     "flash-firmware-force",
		     /* TRANSLATORS: command description */
		     _("Flash firmware into the processor"),
		     ch_util_flash_firmware_force);
	ch_util_add (priv->cmd_array,
		     "eeprom-read",
		     /* TRANSLATORS: command description */
		     _("Read EEPROM at a specified address"),
		     ch_util_eeprom_read);
	ch_util_add (priv->cmd_array,
		     "eeprom-erase",
		     /* TRANSLATORS: command description */
		     _("Erase EEPROM at a specified address"),
		     ch_util_eeprom_erase);
	ch_util_add (priv->cmd_array,
		     "eeprom-write",
		     /* TRANSLATORS: command description */
		     _("Write EEPROM at a specified address"),
		     ch_util_eeprom_write);
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
	ch_util_add (priv->cmd_array,
		     "take-reading-array",
		     /* TRANSLATORS: command description */
		     _("Gets an array of raw samples"),
		     ch_util_take_reading_array);
	ch_util_add (priv->cmd_array,
		     "remote-profile-download",
		     /* TRANSLATORS: command description */
		     _("Downloads a remote profile"),
		     ch_util_remote_profile_download);
	ch_util_add (priv->cmd_array,
		     "remote-profile-upload",
		     /* TRANSLATORS: command description */
		     _("Uploads a remote profile"),
		     ch_util_remote_profile_upload);
	ch_util_add (priv->cmd_array,
		     "ccmx-upload",
		     /* TRANSLATORS: command description */
		     _("Uploads a correction matrix"),
		     ch_util_ccmx_upload);
	ch_util_add (priv->cmd_array,
		     "self-test",
		     /* TRANSLATORS: command description */
		     _("Does a quick self test on the device"),
		     ch_util_self_test);
	ch_util_add (priv->cmd_array,
		     "get-measure-mode",
		     /* TRANSLATORS: command description */
		     _("Gets the sensor measurement mode"),
		     ch_util_get_measure_mode);
	ch_util_add (priv->cmd_array,
		     "set-measure-mode",
		     /* TRANSLATORS: command description */
		     _("Sets the sensor measurement mode"),
		     ch_util_set_measure_mode);
	ch_util_add (priv->cmd_array,
		     "sram-read",
		     /* TRANSLATORS: command description */
		     _("Read SRAM at a specified address"),
		     ch_util_sram_read);
	ch_util_add (priv->cmd_array,
		     "sram-write",
		     /* TRANSLATORS: command description */
		     _("Write SRAM at a specified address"),
		     ch_util_sram_write);
	ch_util_add (priv->cmd_array,
		     "get-temperature",
		     /* TRANSLATORS: command description */
		     _("Gets the sensor temperature"),
		     ch_util_get_temperature);
	ch_util_add (priv->cmd_array,
		     "get-adc-vrefs",
		     /* TRANSLATORS: command description */
		     _("Gets the ADC Vref values"),
		     ch_util_get_adc_vrefs);

	/* sort by command name */
	g_ptr_array_sort (priv->cmd_array,
			  (GCompareFunc) cd_sort_command_name_cb);

	/* get a list of the commands */
	priv->context = g_option_context_new (NULL);
	cmd_descriptions = ch_util_get_descriptions (priv->cmd_array);
	g_option_context_set_summary (priv->context, cmd_descriptions);

	/* TRANSLATORS: program name */
	g_set_application_name (_("Color Management"));
	g_option_context_add_main_entries (priv->context, options, NULL);
	g_option_context_parse (priv->context, &argc, &argv, NULL);

	/* set verbose? */
	if (verbose) {
		g_setenv ("COLORHUG_VERBOSE", "1", FALSE);
	} else {
		g_log_set_handler (G_LOG_DOMAIN, G_LOG_LEVEL_DEBUG,
				   ch_util_ignore_cb, NULL);
	}

	/* get connection to colord */
	priv->device_queue = ch_device_queue_new ();
	priv->device = ch_util_get_default_device (&error);
	if (priv->device == NULL) {
		/* TRANSLATORS: no colord available */
		g_print ("%s %s\n", _("No connection to device:"),
			 error->message);
		g_error_free (error);
		goto out;
	}

	/* setup the session */
	priv->session = soup_session_sync_new_with_options (SOUP_SESSION_USER_AGENT, "colorhug",
							    SOUP_SESSION_TIMEOUT, 5000,
							    NULL);
	if (priv->session == NULL) {
		/* TRANSLATORS: internal error when setting up HTTP */
		g_print ("%s\n", _("Failed to setup networking"));
		goto out;
	}

	/* automatically use the correct proxies */
	soup_session_add_feature_by_type (priv->session,
					  SOUP_TYPE_PROXY_RESOLVER_DEFAULT);

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
		if (priv->session != NULL)
			g_object_unref (priv->session);
		if (priv->cmd_array != NULL)
			g_ptr_array_unref (priv->cmd_array);
		if (priv->device != NULL)
			g_object_unref (priv->device);
		if (priv->device_queue != NULL)
			g_object_unref (priv->device_queue);
		g_option_context_free (priv->context);
		g_free (priv);
	}
	g_free (cmd_descriptions);
	return retval;
}

