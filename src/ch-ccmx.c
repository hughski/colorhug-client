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
#include <colord-gtk.h>
#include <math.h>
#include <gusb.h>
#include <libsoup/soup.h>
#include <colorhug.h>

typedef enum {
	CH_CCMX_PAGE_DEVICES,
	CH_CCMX_PAGE_REFERENCE,
	CH_CCMX_PAGE_REFERENCE_ACTION,
	CH_CCMX_PAGE_COLORHUG,
	CH_CCMX_PAGE_COLORHUG_ACTION,
	CH_CCMX_PAGE_SUMMARY
} ChCcmxGenPage;

typedef struct {
	GtkApplication	*application;
	GtkBuilder	*builder;
	gboolean	 done_get_cal;
	GUsbContext	*usb_ctx;
	GUsbDevice	*device;
	GUsbDeviceList	*device_list;
	SoupSession	*session;
	guint16		 calibration_map[CH_CALIBRATION_MAX];
	guint		 ccmx_idx;
	guint8		 ccmx_types[CH_CALIBRATION_MAX];
	gchar		*ccmx_description[CH_CALIBRATION_MAX];
	GHashTable	*hash;
	guint32		 serial_number;
	gboolean	 needs_repair;
	gboolean	 force_repair;
	ChDeviceQueue	*device_queue;
	GSettings	*settings;
	/* for the ccmx generation feature */
	CdClient	*gen_client;
	CdDevice	*gen_device;
	CdIt8		*gen_ti1;
	CdIt8		*gen_ti3_colorhug;
	CdIt8		*gen_ti3_spectral;
	CdSensor	*gen_sensor_colorhug;
	CdSensor	*gen_sensor_spectral;
	ChCcmxGenPage	 gen_current_page;
	gboolean	 gen_waiting_for_interaction;
	GMainLoop	*gen_loop;
	GtkWidget	*gen_sample_widget;
	CdIt8		*gen_ccmx;
} ChCcmxPrivate;

enum {
	COLUMN_DESCRIPTION,
	COLUMN_INDEX,
	COLUMN_TYPE,
	COLUMN_LOCAL_FILENAME,
	COLUMN_LAST
};

#define	CH_CCMX_DISPLAY_REFRESH_TIME		200 /* ms */
#define	CH_CCMX_SAMPLE_SQUARE_SIZE		400 /* px */
#define	CH_CCMX_CCMX_UPLOAD_SERVER		"http://www.hughski.com/ccmx-store.php"

static void	 ch_ccmx_refresh_calibration_data	(ChCcmxPrivate *priv);
static gboolean	 ch_ccmx_set_calibration_data		(ChCcmxPrivate *priv,
							 guint16 cal_idx,
							 const guint8 *ccmx_data,
							 gsize ccmx_size,
							 GError **error);

/**
 * ch_ccmx_error_dialog:
 **/
static void
ch_ccmx_error_dialog (ChCcmxPrivate *priv,
		      const gchar *title,
		      const gchar *message)
{
	GtkWindow *window;
	GtkWidget *dialog;

	window = GTK_WINDOW(gtk_builder_get_object (priv->builder, "dialog_ccmx"));
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
 * ch_ccmx_activate_cb:
 **/
static void
ch_ccmx_activate_cb (GApplication *application, ChCcmxPrivate *priv)
{
	GtkWindow *window;
	window = GTK_WINDOW (gtk_builder_get_object (priv->builder, "dialog_ccmx"));
	gtk_window_present (window);
}

/**
 * ch_ccmx_close_button_cb:
 **/
static void
ch_ccmx_close_button_cb (GtkWidget *widget, ChCcmxPrivate *priv)
{
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "dialog_ccmx"));
	gtk_widget_destroy (widget);
}

/**
 * ch_ccmx_help_button_cb:
 **/
static void
ch_ccmx_help_button_cb (GtkWidget *widget, ChCcmxPrivate *priv)
{
	gboolean ret;
	GError *error = NULL;
	ret = gtk_show_uri (NULL,
			    "help:colorhug-client/load-ccmx",
			    GDK_CURRENT_TIME,
			    &error);
	if (!ret) {
		g_warning ("Failed to load help document: %s", error->message);
		g_error_free (error);
	}
}

/**
 * ch_ccmx_gen_close_button_cb:
 **/
static void
ch_ccmx_gen_close_button_cb (GtkWidget *widget, ChCcmxPrivate *priv)
{
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "dialog_gen"));
	gtk_widget_hide (widget);
}

/**
 * ch_ccmx_create_user_datadir:
 **/
static gboolean
ch_ccmx_create_user_datadir (ChCcmxPrivate *priv, const gchar *location)
{
	gboolean ret;
	GError *error = NULL;
	GFile *file = NULL;

	/* check if exists */
	file = g_file_new_for_path (location);
	ret = g_file_query_exists (file, NULL);
	if (ret)
		goto out;
	ret = g_file_make_directory_with_parents (file, NULL, &error);
	if (!ret) {
		ch_ccmx_error_dialog (priv,
				      _("Failed to create directory"),
				      error->message);
		g_error_free (error);
		goto out;
	}
out:
	g_object_unref (file);
	return ret;
}

/**
 * ch_ccmx_find_by_desc:
 **/
static gboolean
ch_ccmx_find_by_desc (GtkTreeModel *model,
		      GtkTreeIter *iter_found,
		      const gchar *desc)
{
	gboolean ret;
	gchar *desc_tmp = NULL;
	GtkTreeIter iter;

	ret = gtk_tree_model_get_iter_first (model, &iter);
	while (ret) {
		gtk_tree_model_get (model, &iter,
				    COLUMN_DESCRIPTION, &desc_tmp,
				    -1);
		ret = g_strcmp0 (desc_tmp, desc) == 0;
		g_free (desc_tmp);
		if (ret) {
			*iter_found = iter;
			break;
		}
		ret = gtk_tree_model_iter_next (model, &iter);
	}
	return ret;
}

/**
 * ch_ccmx_add_local_file:
 **/
static gboolean
ch_ccmx_add_local_file (ChCcmxPrivate *priv,
			const gchar *filename,
			GError **error)
{
	CdIt8 *it8 = NULL;
	const gchar *description;
	gboolean ret;
	gchar *ccmx_data = NULL;
	gsize ccmx_size;
	GtkListStore *list_store;
	GtkTreeIter iter;
	guint8 types;

	/* load file */
	g_debug ("opening %s", filename);
	ret = g_file_get_contents (filename,
				   &ccmx_data,
				   &ccmx_size,
				   error);
	if (!ret)
		goto out;

	/* parse */
	it8 = cd_it8_new ();
	ret = cd_it8_load_from_data (it8, ccmx_data, ccmx_size, error);
	if (!ret)
		goto out;

	/* get the description from the ccmx file */
	description = cd_it8_get_title (it8);
	if (description == NULL) {
		ret = FALSE;
		g_set_error_literal (error, 1, 0,
				     "CCMX file does not have title");
		goto out;
	}

	/* does already exist? */
	if (g_hash_table_lookup (priv->hash, description) != NULL) {
		ret = TRUE;
		g_debug ("CCMX '%s' already exists", description);
		goto out;
	}

	/* get the supported display types */
	if (cd_it8_has_option (it8, "TYPE_FACTORY")) {
		types = CH_CALIBRATION_TYPE_ALL;
	} else {
		types = 0;
		if (cd_it8_has_option (it8, "TYPE_LCD"))
			types += CH_CALIBRATION_TYPE_LCD;
		if (cd_it8_has_option (it8, "TYPE_LED"))
			types += CH_CALIBRATION_TYPE_LED;
		if (cd_it8_has_option (it8, "TYPE_CRT"))
			types += CH_CALIBRATION_TYPE_CRT;
		if (cd_it8_has_option (it8, "TYPE_PROJECTOR"))
			types += CH_CALIBRATION_TYPE_PROJECTOR;
	}

	/* is suitable for LCD */
	list_store = GTK_LIST_STORE (gtk_builder_get_object (priv->builder, "liststore_lcd"));
	if ((types & CH_CALIBRATION_TYPE_LCD) > 0) {
		ret = ch_ccmx_find_by_desc (GTK_TREE_MODEL (list_store),
					    &iter,
					    description);
		if (!ret)
			gtk_list_store_append (list_store, &iter);
		gtk_list_store_set (list_store, &iter,
				    COLUMN_DESCRIPTION, description,
				    COLUMN_INDEX, -1,
				    COLUMN_TYPE, "web-browser",
				    COLUMN_LOCAL_FILENAME, filename,
				    -1);
	}

	/* insert into hash */
	g_hash_table_insert (priv->hash,
			     g_strdup (description),
			     GINT_TO_POINTER (1));

	/* is suitable for LED */
	list_store = GTK_LIST_STORE (gtk_builder_get_object (priv->builder, "liststore_led"));
	if ((types & CH_CALIBRATION_TYPE_LED) > 0) {
		ret = ch_ccmx_find_by_desc (GTK_TREE_MODEL (list_store),
					    &iter,
					    description);
		if (!ret)
			gtk_list_store_append (list_store, &iter);
		gtk_list_store_set (list_store, &iter,
				    COLUMN_DESCRIPTION, description,
				    COLUMN_INDEX, -1,
				    COLUMN_TYPE, "web-browser",
				    COLUMN_LOCAL_FILENAME, filename,
				    -1);
	}

	/* is suitable for CRT */
	list_store = GTK_LIST_STORE (gtk_builder_get_object (priv->builder, "liststore_crt"));
	if ((types & CH_CALIBRATION_TYPE_CRT) > 0) {
		ret = ch_ccmx_find_by_desc (GTK_TREE_MODEL (list_store),
					    &iter,
					    description);
		if (!ret)
			gtk_list_store_append (list_store, &iter);
		gtk_list_store_set (list_store, &iter,
				    COLUMN_DESCRIPTION, description,
				    COLUMN_INDEX, -1,
				    COLUMN_TYPE, "web-browser",
				    COLUMN_LOCAL_FILENAME, filename,
				    -1);
	}

	/* is suitable for projector */
	list_store = GTK_LIST_STORE (gtk_builder_get_object (priv->builder, "liststore_projector"));
	if ((types & CH_CALIBRATION_TYPE_PROJECTOR) > 0) {
		ret = ch_ccmx_find_by_desc (GTK_TREE_MODEL (list_store),
					    &iter,
					    description);
		if (!ret)
			gtk_list_store_append (list_store, &iter);
		gtk_list_store_set (list_store, &iter,
				    COLUMN_DESCRIPTION, description,
				    COLUMN_INDEX, -1,
				    COLUMN_TYPE, "web-browser",
				    COLUMN_LOCAL_FILENAME, filename,
				    -1);
	}

	/* success */
	ret = TRUE;
out:
	g_free (ccmx_data);
	if (it8 != NULL)
		g_object_unref (it8);
	return ret;
}

/**
 * ch_ccmx_add_local_files:
 **/
static void
ch_ccmx_add_local_files (ChCcmxPrivate *priv)
{
	const gchar *tmp;
	gboolean ret;
	gchar *location;
	gchar *location_tmp;
	GDir *dir;
	GError *error = NULL;

	/* open directory */
	location = g_build_filename (g_get_user_data_dir (),
				     "colorhug-ccmx",
				     NULL);
	dir = g_dir_open (location, 0, &error);
	if (dir == NULL) {
		g_warning ("Failed to get directory: %s", error->message);
		g_error_free (error);
		goto out;
	}
	while (TRUE) {
		tmp = g_dir_read_name (dir);
		if (tmp == NULL)
			break;
		location_tmp = g_build_filename (location, tmp, NULL);
		ret = ch_ccmx_add_local_file (priv, location_tmp, &error);
		if (!ret) {
			g_warning ("Failed to add file %s: %s",
				   location_tmp, error->message);
			g_error_free (error);
			goto out;
		}
		g_free (location_tmp);
	}
out:
	if (dir != NULL)
		g_dir_close (dir);
	g_free (location);
}

/**
 * ch_ccmx_set_combo_from_index:
 **/
static void
ch_ccmx_set_combo_from_index (GtkComboBox *combo, guint idx)
{
	gboolean ret;
	GtkTreeIter iter;
	GtkTreeModel *model;
	guint idx_tmp;

	model = gtk_combo_box_get_model (combo);
	ret = gtk_tree_model_get_iter_first (model, &iter);
	while (ret) {
		gtk_tree_model_get (model, &iter,
				    COLUMN_INDEX, &idx_tmp,
				    -1);
		if (idx == idx_tmp) {
			gtk_combo_box_set_active_iter (combo,
						       &iter);
			break;
		}
		ret = gtk_tree_model_iter_next (model, &iter);
	}
}

/**
 * ch_ccmx_got_factory_calibration_cb:
 **/
static void
ch_ccmx_got_factory_calibration_cb (SoupSession *session,
				    SoupMessage *msg,
				    gpointer user_data)
{
	ChCcmxPrivate *priv = (ChCcmxPrivate *) user_data;
	gboolean ret;
	gchar *location = NULL;
	GError *error = NULL;
	GtkListStore *list_store;
	SoupURI *uri;
	guint i;

	/* we failed */
	if (!SOUP_STATUS_IS_SUCCESSFUL (msg->status_code)) {
		uri = soup_message_get_uri (msg);
		location = g_strdup_printf ("%s: %s",
					    soup_status_get_phrase (msg->status_code),
					    uri->path);
		ch_ccmx_error_dialog (priv,
				      _("Failed to download file"),
				      location);
		goto out;
	}

	/* empty file */
	if (msg->response_body->length == 0) {
		ch_ccmx_error_dialog (priv,
				      _("File has zero size"),
				      soup_status_get_phrase (msg->status_code));
		goto out;
	}

	/* update UI */
	ret = ch_ccmx_set_calibration_data (priv, 0,
					    (const guint8 *) msg->response_body->data,
					    (gsize) msg->response_body->length,
					    &error);
	if (!ret) {
		ch_ccmx_error_dialog (priv,
				       _("Failed to load data"),
				       error->message);
		g_error_free (error);
		goto out;
	}

	/* reset the calibration map too */
	for (i = 0; i < CH_CALIBRATION_MAX; i++)
		priv->calibration_map[i] = 0;

	/* clear any existing profiles */
	list_store = GTK_LIST_STORE (gtk_builder_get_object (priv->builder, "liststore_lcd"));
	gtk_list_store_clear (list_store);
	list_store = GTK_LIST_STORE (gtk_builder_get_object (priv->builder, "liststore_led"));
	gtk_list_store_clear (list_store);
	list_store = GTK_LIST_STORE (gtk_builder_get_object (priv->builder, "liststore_crt"));
	gtk_list_store_clear (list_store);
	list_store = GTK_LIST_STORE (gtk_builder_get_object (priv->builder, "liststore_projector"));
	gtk_list_store_clear (list_store);
	g_hash_table_remove_all (priv->hash);
out:
	g_free (location);
}

/**
 * ch_ccmx_get_device_download_kind:
 **/
static const gchar *
ch_ccmx_get_device_download_kind (ChCcmxPrivate *priv)
{
	const char *str = NULL;
	switch (ch_device_get_mode (priv->device)) {
	case CH_DEVICE_MODE_LEGACY:
	case CH_DEVICE_MODE_BOOTLOADER:
	case CH_DEVICE_MODE_FIRMWARE:
		str = "colorhug";
		break;
	case CH_DEVICE_MODE_BOOTLOADER_SPECTRO:
	case CH_DEVICE_MODE_FIRMWARE_SPECTRO:
		str = "colorhug-spectro";
		break;
	default:
		str = "unknown";
		break;
	}
	return str;
}

/**
 * ch_ccmx_get_serial_number_cb:
 **/
static void
ch_ccmx_get_serial_number_cb (GObject *source,
			      GAsyncResult *res,
			      gpointer user_data)
{
	ChCcmxPrivate *priv = (ChCcmxPrivate *) user_data;
	const gchar *title;
	gboolean ret;
	GError *error = NULL;
	ChDeviceQueue *device_queue = CH_DEVICE_QUEUE (source);
	SoupMessage *msg = NULL;
	SoupURI *base_uri = NULL;
	gchar *server_uri = NULL;
	gchar *uri = NULL;

	/* get data */
	ret = ch_device_queue_process_finish (device_queue, res, &error);
	if (!ret) {
		/* TRANSLATORS: the request failed */
		title = _("Failed to contact ColorHug");
		ch_ccmx_error_dialog (priv, title, error->message);
		g_error_free (error);
		goto out;
	}

	/* download the correct factory calibration file */
	server_uri = g_settings_get_string (priv->settings, "server-uri");
	uri = g_strdup_printf ("%s/%s/%s/calibration-%06i.ccmx",
			       server_uri,
			       ch_ccmx_get_device_download_kind (priv),
			       "archive",
			       priv->serial_number);
	base_uri = soup_uri_new (uri);
	msg = soup_message_new_from_uri (SOUP_METHOD_GET, base_uri);
	if (msg == NULL) {
		/* TRANSLATORS: internal error when setting up HTTP request */
		title = _("Failed to setup message");
		ch_ccmx_error_dialog (priv, title, uri);
		goto out;
	}

	/* send sync */
	soup_session_queue_message (priv->session, msg,
				    ch_ccmx_got_factory_calibration_cb, priv);
out:
	g_free (server_uri);
	g_free (uri);
	if (base_uri != NULL)
		soup_uri_free (base_uri);
}

/**
 * ch_ccmx_device_needs_repair_cb:
 **/
static void
ch_ccmx_device_needs_repair_cb (GtkDialog *dialog,
			  GtkResponseType response_id,
			  ChCcmxPrivate *priv)
{
	if (response_id != GTK_RESPONSE_YES)
		goto out;

	/* get the serial number */
	ch_device_queue_get_serial_number (priv->device_queue,
					   priv->device,
					   &priv->serial_number);
	ch_device_queue_process_async (priv->device_queue,
				       CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
				       NULL,
				       ch_ccmx_get_serial_number_cb,
				       priv);
out:
	gtk_widget_destroy (GTK_WIDGET (dialog));
}

/**
 * ch_ccmx_device_needs_repair:
 **/
static void
ch_ccmx_device_needs_repair (ChCcmxPrivate *priv)
{
	const gchar *message;
	GtkWidget *dialog;
	GtkWindow *window;

	/* TRANSLATORS: device is broken and needs fixing */
	message = _("The ColorHug is missing the factory calibration values.");
	window = GTK_WINDOW(gtk_builder_get_object (priv->builder, "dialog_ccmx"));
	dialog = gtk_message_dialog_new (window,
					 GTK_DIALOG_MODAL,
					 GTK_MESSAGE_WARNING,
					 GTK_BUTTONS_NONE,
					 /* TRANSLATORS: the device is missing
					  * it's calibration matrix */
					 "%s", _("Device calibration error"));
	gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
						  "%s", message);

	/* TRANSLATORS: this is a button */
	gtk_dialog_add_button (GTK_DIALOG (dialog), _("Ignore"), GTK_RESPONSE_NO);

	/* TRANSLATORS: this is a button */
	gtk_dialog_add_button (GTK_DIALOG (dialog), _("Repair"), GTK_RESPONSE_YES);

	/* wait async */
	g_signal_connect (dialog, "response",
			  G_CALLBACK (ch_ccmx_device_needs_repair_cb),
			  priv);
	gtk_widget_show (dialog);
}

/**
 * ch_ccmx_device_force_repair:
 **/
static void
ch_ccmx_device_force_repair (ChCcmxPrivate *priv)
{
	const gchar *message;
	GtkWidget *dialog;
	GtkWindow *window;

	/* TRANSLATORS: device is broken and needs fixing */
	message = _("Update the factory calibration values?");
	window = GTK_WINDOW(gtk_builder_get_object (priv->builder, "dialog_ccmx"));
	dialog = gtk_message_dialog_new (window,
					 GTK_DIALOG_MODAL,
					 GTK_MESSAGE_QUESTION,
					 GTK_BUTTONS_NONE,
					 /* TRANSLATORS: the device has an
					  * updated calibration matrix */
					 "%s", _("Device calibration update"));
	gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
						  "%s", message);

	/* TRANSLATORS: this is a button */
	gtk_dialog_add_button (GTK_DIALOG (dialog), _("Cancel"), GTK_RESPONSE_NO);

	/* TRANSLATORS: this is a button */
	gtk_dialog_add_button (GTK_DIALOG (dialog), _("Repair"), GTK_RESPONSE_YES);

	/* wait async */
	g_signal_connect (dialog, "response",
			  G_CALLBACK (ch_ccmx_device_needs_repair_cb),
			  priv);
	gtk_widget_show (dialog);
}

/**
 * ch_ccmx_add_calibration:
 **/
static void
ch_ccmx_add_calibration (ChCcmxPrivate *priv,
			 guint16 idx,
			 const gchar *description,
			 guint8 types)
{
	gboolean ret;
	GtkListStore *list_store;
	GtkTreeIter iter;

	if (types == 0)
		return;

	/* is suitable for LCD */
	list_store = GTK_LIST_STORE (gtk_builder_get_object (priv->builder, "liststore_lcd"));
	if ((types & CH_CALIBRATION_TYPE_LCD) > 0) {
		ret = ch_ccmx_find_by_desc (GTK_TREE_MODEL (list_store),
					    &iter,
					    description);
		if (!ret)
			gtk_list_store_append (list_store, &iter);
		gtk_list_store_set (list_store, &iter,
				    COLUMN_DESCRIPTION, description,
				    COLUMN_INDEX, idx,
				    COLUMN_TYPE, NULL,
				    COLUMN_LOCAL_FILENAME, NULL,
				    -1);
	}

	/* is suitable for LED */
	list_store = GTK_LIST_STORE (gtk_builder_get_object (priv->builder, "liststore_led"));
	if ((types & CH_CALIBRATION_TYPE_LED) > 0) {
		ret = ch_ccmx_find_by_desc (GTK_TREE_MODEL (list_store),
					    &iter,
					    description);
		if (!ret)
			gtk_list_store_append (list_store, &iter);
		gtk_list_store_set (list_store, &iter,
				    COLUMN_DESCRIPTION, description,
				    COLUMN_INDEX, idx,
				    COLUMN_TYPE, NULL,
				    COLUMN_LOCAL_FILENAME, NULL,
				    -1);
	}

	/* is suitable for CRT */
	list_store = GTK_LIST_STORE (gtk_builder_get_object (priv->builder, "liststore_crt"));
	if ((types & CH_CALIBRATION_TYPE_CRT) > 0) {
		ret = ch_ccmx_find_by_desc (GTK_TREE_MODEL (list_store),
					    &iter,
					    description);
		if (!ret)
			gtk_list_store_append (list_store, &iter);
		gtk_list_store_set (list_store, &iter,
				    COLUMN_DESCRIPTION, description,
				    COLUMN_INDEX, idx,
				    COLUMN_TYPE, NULL,
				    COLUMN_LOCAL_FILENAME, NULL,
				    -1);
	}

	/* is suitable for projector */
	list_store = GTK_LIST_STORE (gtk_builder_get_object (priv->builder, "liststore_projector"));
	if ((types & CH_CALIBRATION_TYPE_PROJECTOR) > 0) {
		ret = ch_ccmx_find_by_desc (GTK_TREE_MODEL (list_store),
					    &iter,
					    description);
		if (!ret)
			gtk_list_store_append (list_store, &iter);
		gtk_list_store_set (list_store, &iter,
				    COLUMN_DESCRIPTION, description,
				    COLUMN_INDEX, idx,
				    COLUMN_TYPE, NULL,
				    COLUMN_LOCAL_FILENAME, NULL,
				    -1);
	}

	/* insert into hash */
	g_hash_table_insert (priv->hash,
			     g_strdup (description),
			     GINT_TO_POINTER (1));
}

/**
 * ch_ccmx_get_calibration_cb:
 **/
static void
ch_ccmx_get_calibration_cb (GObject *source,
			    GAsyncResult *res,
			    gpointer user_data)
{
	ChCcmxPrivate *priv = (ChCcmxPrivate *) user_data;
	ChDeviceQueue *device_queue = CH_DEVICE_QUEUE (source);
	const gchar *title;
	gboolean ret;
	GError *error = NULL;
	GtkWidget *widget;
	guint i;

	/* get data */
	ret = ch_device_queue_process_finish (device_queue, res, &error);
	if (!ret) {
		/* TRANSLATORS: the calibration map is an array that
		 * maps a specific matrix to a display type */
		title = _("Failed to get the calibration data");
		ch_ccmx_error_dialog (priv, title, error->message);
		g_error_free (error);
		goto out;
	}

	/* add each item */
	for (i = 0; i < CH_CALIBRATION_MAX; i++) {
		ch_ccmx_add_calibration (priv,
					 i,
					 priv->ccmx_description[i],
					 priv->ccmx_types[i]);
	}

	/* does this device need repairing */
	if (g_strcmp0 (priv->ccmx_description[0], "Factory Calibration") == 0)
		priv->needs_repair = FALSE;

	/* add local files */
	ch_ccmx_add_local_files (priv);

	/* setup UI */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "box_progress"));
	gtk_widget_hide (widget);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_import"));
	gtk_widget_show (widget);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_generate"));
	gtk_widget_set_visible (widget, priv->gen_sensor_spectral != NULL);

	/* select the right checkboxes */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "combobox_lcd"));
	ch_ccmx_set_combo_from_index (GTK_COMBO_BOX (widget), priv->calibration_map[0]);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "combobox_crt"));
	ch_ccmx_set_combo_from_index (GTK_COMBO_BOX (widget), priv->calibration_map[1]);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "combobox_projector"));
	ch_ccmx_set_combo_from_index (GTK_COMBO_BOX (widget), priv->calibration_map[2]);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "combobox_led"));
	ch_ccmx_set_combo_from_index (GTK_COMBO_BOX (widget), priv->calibration_map[3]);

	/* we've setup */
	priv->done_get_cal = TRUE;

	/* offer to repair the device */
	if (priv->needs_repair) {
		priv->force_repair = FALSE;
		ch_ccmx_device_needs_repair (priv);
	} else if (priv->force_repair) {
		ch_ccmx_device_force_repair (priv);
		/* Force repair only once */
		priv->force_repair = FALSE;
	}
out:
	return;
}

/**
 * ch_ccmx_get_profile_filename:
 **/
static gchar *
ch_ccmx_get_profile_filename (GtkWindow *window)
{
	gchar *filename = NULL;
	GtkWidget *dialog;
	GtkFileFilter *filter;

	/* TRANSLATORS: dialog for chosing the correction matrix */
	dialog = gtk_file_chooser_dialog_new (_("Select correction matrix"), window,
					      GTK_FILE_CHOOSER_ACTION_OPEN,
					      GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
					      GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT,
					      NULL);
	gtk_file_chooser_set_create_folders (GTK_FILE_CHOOSER(dialog), FALSE);

	/* setup the all files filter */
	filter = gtk_file_filter_new ();
	gtk_file_filter_add_pattern (filter, "*.ccmx");
	/* TRANSLATORS: filter name on the file->open dialog */
	gtk_file_filter_set_name (filter, _("Correction matrices"));
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
 * ch_ccmx_set_calibration_map_cb:
 **/
static void
ch_ccmx_set_calibration_map_cb (GObject *source,
				GAsyncResult *res,
				gpointer user_data)
{
	const gchar *title;
	ChCcmxPrivate *priv = (ChCcmxPrivate *) user_data;
	gboolean ret;
	GError *error = NULL;
	GtkWidget *widget;
	ChDeviceQueue *device_queue = CH_DEVICE_QUEUE (source);

	/* get data */
	ret = ch_device_queue_process_finish (device_queue, res, &error);
	if (!ret) {
		/* TRANSLATORS: the calibration map is an array that
		 * maps a specific matrix to a display type */
		title = _("Failed to set the calibration map");
		ch_ccmx_error_dialog (priv, title, error->message);
		g_error_free (error);
		goto out;
	}

	/* update the combos */
	ch_ccmx_refresh_calibration_data (priv);
out:
	/* update UI */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_close"));
	gtk_widget_set_sensitive (widget, TRUE);
}

/**
 * ch_ccmx_set_calibration_cb:
 **/
static void
ch_ccmx_set_calibration_cb (GObject *source,
			    GAsyncResult *res,
			    gpointer user_data)
{
	ChCcmxPrivate *priv = (ChCcmxPrivate *) user_data;
	gboolean ret;
	GError *error = NULL;
	GtkWidget *widget;
	ChDeviceQueue *device_queue = CH_DEVICE_QUEUE (source);

	/* get data */
	ret = ch_device_queue_process_finish (device_queue, res, &error);
	if (!ret) {
		ch_ccmx_error_dialog (priv,
				       _("Failed to set the calibration matrix"),
				       error->message);
		g_error_free (error);
		goto out;
	}

	/* assign it here */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_close"));
	gtk_widget_set_sensitive (widget, FALSE);

	/* hit hardware */
	ch_device_queue_set_calibration_map (priv->device_queue,
					     priv->device,
					     priv->calibration_map);
	ch_device_queue_process_async (priv->device_queue,
				       CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
				       NULL,
				       ch_ccmx_set_calibration_map_cb,
				       priv);
out:
	return;
}

/**
 * ch_ccmx_set_calibration_data:
 **/
static gboolean
ch_ccmx_set_calibration_data (ChCcmxPrivate *priv,
			      guint16 cal_idx,
			      const guint8 *ccmx_data,
			      gsize ccmx_size,
			      GError **error)
{
	CdIt8 *it8 = NULL;
	const CdMat3x3 *calibration;
	const gchar *description;
	gboolean ret = TRUE;
	guint8 types = 0;

	/* read ccmx */
	it8 = cd_it8_new ();
	ret = cd_it8_load_from_data (it8, (const gchar*) ccmx_data, ccmx_size, error);
	if (!ret)
		goto out;

	/* get the description from the ccmx file */
	description = cd_it8_get_title (it8);
	if (description == NULL) {
		ret = FALSE;
		g_set_error_literal (error, 1, 0,
				     "CCMX file does not have description");
		goto out;
	}

	/* get the supported display types */
	if (cd_it8_has_option (it8, "TYPE_FACTORY")) {
		types = CH_CALIBRATION_TYPE_ALL;
	} else {
		types = 0;
		if (cd_it8_has_option (it8, "TYPE_LCD"))
			types += CH_CALIBRATION_TYPE_LCD;
		if (cd_it8_has_option (it8, "TYPE_LED"))
			types += CH_CALIBRATION_TYPE_LED;
		if (cd_it8_has_option (it8, "TYPE_CRT"))
			types += CH_CALIBRATION_TYPE_CRT;
		if (cd_it8_has_option (it8, "TYPE_PROJECTOR"))
			types += CH_CALIBRATION_TYPE_PROJECTOR;
	}

	/* set to HW */
	calibration = cd_it8_get_matrix (it8);
	ch_device_queue_set_calibration (priv->device_queue,
					 priv->device,
					 cal_idx,
					 calibration,
					 types,
					 description);
	ch_device_queue_process_async (priv->device_queue,
				       CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
				       NULL,
				       ch_ccmx_set_calibration_cb,
				       priv);
out:
	return ret;
}

/**
 * ch_ccmx_set_calibration_file:
 **/
static gboolean
ch_ccmx_set_calibration_file (ChCcmxPrivate *priv,
			      guint16 cal_idx,
			      const gchar *filename,
			      GError **error)
{
	gboolean ret;
	gchar *ccmx_data = NULL;
	gsize ccmx_size;

	/* load local file */
	ret = g_file_get_contents (filename,
				   &ccmx_data,
				   &ccmx_size,
				   error);
	if (!ret)
		goto out;

	ret = ch_ccmx_set_calibration_data (priv,
					    cal_idx,
					    (guint8 *)ccmx_data,
					    ccmx_size,
					    error);
out:
	g_free (ccmx_data);
	return ret;
}

/**
 * ch_ccmx_import_button_cb:
 **/
static void
ch_ccmx_import_button_cb (GtkWidget *widget, ChCcmxPrivate *priv)
{
	gboolean ret;
	gchar *filename;
	GError *error = NULL;
	GtkWindow *window;
	guint i;

	window = GTK_WINDOW (gtk_builder_get_object (priv->builder, "dialog_ccmx"));
	filename = ch_ccmx_get_profile_filename (window);
	if (filename == NULL)
		goto out;

	/* import the file into a spare slot */
	for (i = 0; i < CH_CALIBRATION_MAX; i++) {
		if (priv->ccmx_types[i] == 0)
			break;
	}
	if (i == CH_CALIBRATION_MAX) {
		ch_ccmx_error_dialog (priv,
				      _("No space left on device"),
				      _("All 64 slots are used up."));
		goto out;
	}

	/* load this ccmx file as the new calibration */
	ret = ch_ccmx_set_calibration_file (priv, i, filename, &error);
	if (!ret) {
		ch_ccmx_error_dialog (priv,
				       _("Failed to load file"),
				       error->message);
		g_error_free (error);
		goto out;
	}

	/* update the combos */
	ch_ccmx_refresh_calibration_data (priv);
out:
	g_free (filename);
}

/**
 * ch_ccmx_refresh_calibration_data:
 **/
static void
ch_ccmx_refresh_calibration_data (ChCcmxPrivate *priv)
{
	guint i;

	/* get latest from device */
	priv->done_get_cal = FALSE;

	/* get the calibration info from all slots */
	for (i = 0; i < CH_CALIBRATION_MAX; i++) {
		ch_device_queue_get_calibration (priv->device_queue,
						 priv->device,
						 i,
						 NULL,
						 &priv->ccmx_types[i],
						 priv->ccmx_description[i]);
	}
	ch_device_queue_get_calibration_map (priv->device_queue,
					     priv->device,
					     priv->calibration_map);

	ch_device_queue_process_async (priv->device_queue,
				       CH_DEVICE_QUEUE_PROCESS_FLAGS_CONTINUE_ERRORS |
				       CH_DEVICE_QUEUE_PROCESS_FLAGS_NONFATAL_ERRORS,
				       NULL,
				       ch_ccmx_get_calibration_cb,
				       priv);
}

/**
 * ch_ccmx_got_device:
 **/
static void
ch_ccmx_got_device (ChCcmxPrivate *priv)
{
	const gchar *title;
	gboolean ret;
	GError *error = NULL;
	GtkWidget *widget;

	/* fake device */
	if (g_getenv ("COLORHUG_EMULATE") != NULL)
		goto fake_device;

	/* open device */
	ret = ch_device_open (priv->device, &error);
	if (!ret) {
		/* TRANSLATORS: permissions error perhaps? */
		title = _("Failed to open device");
		ch_ccmx_error_dialog (priv, title, error->message);
		g_error_free (error);
		return;
	}

fake_device:
	/* update the UI */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "box_connect"));
	gtk_widget_hide (widget);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "box_header"));
	gtk_widget_show (widget);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "grid_data"));
	gtk_widget_show (widget);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "box_progress"));
	gtk_widget_show (widget);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_msg"));
	/* TRANSLATORS: get the calibration matrices from the device */
	title = _("Getting calibration from device…");
	gtk_label_set_label (GTK_LABEL (widget), title);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_refresh"));
	gtk_widget_show (widget);

	/* start getting the calibration matrices */
	ch_ccmx_refresh_calibration_data (priv);
}

/**
 * ch_ccmx_set_combo_simple_text:
 **/
static void
ch_ccmx_set_combo_simple_text (GtkWidget *combo_box)
{
	GtkCellRenderer *renderer;
	renderer = gtk_cell_renderer_text_new ();
	g_object_set (renderer,
		      "ellipsize", PANGO_ELLIPSIZE_END,
		      "wrap-mode", PANGO_WRAP_WORD_CHAR,
		      "width-chars", 60,
		      NULL);
	gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (combo_box), renderer, TRUE);
	gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (combo_box), renderer,
					"text", COLUMN_DESCRIPTION,
					NULL);
	renderer = gtk_cell_renderer_pixbuf_new ();
	g_object_set (renderer,
		      "stock-size", 1,
		      NULL);
	gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (combo_box), renderer, TRUE);
	gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (combo_box), renderer,
					"icon-name", COLUMN_TYPE,
					NULL);
}

/**
 * ch_ccmx_combo_changed_cb:
 **/
static void
ch_ccmx_combo_changed_cb (GtkComboBox *combo, ChCcmxPrivate *priv)
{
	const gchar *title;
	gboolean ret;
	gchar *local_filename = NULL;
	GError *error = NULL;
	gint idx_tmp;
	GtkTreeIter iter;
	GtkTreeModel *model;
	GtkWidget *widget;
	guint cal_index;
	guint i;

	/* not yet setup UI */
	if (!priv->done_get_cal)
		return;

	/* change this on the device */
	ret = gtk_combo_box_get_active_iter (combo, &iter);
	if (!ret)
		return;
	model = gtk_combo_box_get_model (combo);
	gtk_tree_model_get (model, &iter,
			    COLUMN_INDEX, &idx_tmp,
			    COLUMN_LOCAL_FILENAME, &local_filename,
			    -1);

	/* import the file into a spare slot */
	if (idx_tmp == -1) {

		for (i = 0; i < CH_CALIBRATION_MAX; i++) {
			if (priv->ccmx_types[i] == 0)
				break;
		}
		if (i == CH_CALIBRATION_MAX) {
			gtk_combo_box_set_active (combo, -1);
			/* TRANSLATORS: You can only store 64 calibration
			 * matrices on the device at any one time */
			title = _("No space left on device");
			ch_ccmx_error_dialog (priv, title,
					      _("All 64 slots are used up."));
			goto out;
		}

		/* load this ccmx file as the new calibration */
		ret = ch_ccmx_set_calibration_file (priv, i, local_filename, &error);
		if (!ret) {
			gtk_combo_box_set_active (combo, -1);
			ch_ccmx_error_dialog (priv,
					       _("Failed to load file"),
					       error->message);
			g_error_free (error);
			goto out;
		}

		/* fix the index */
		gtk_list_store_set (GTK_LIST_STORE (model), &iter,
				    COLUMN_INDEX, i,
				    COLUMN_TYPE, NULL,
				    -1);

		/* update the map */
		cal_index = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (combo),
								"colorhug-ccmx-idx"));
		priv->calibration_map[cal_index] = i;
		goto out;
	}

	/* update the map */
	cal_index = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (combo),
							"colorhug-ccmx-idx"));
	priv->calibration_map[cal_index] = idx_tmp;

	/* update UI */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_close"));
	gtk_widget_set_sensitive (widget, FALSE);

	/* hit hardware */
	ch_device_queue_set_calibration_map (priv->device_queue,
					     priv->device,
					     priv->calibration_map);
	ch_device_queue_write_eeprom (priv->device_queue,
				      priv->device,
				      CH_WRITE_EEPROM_MAGIC);
	ch_device_queue_process_async (priv->device_queue,
				       CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
				       NULL,
				       ch_ccmx_set_calibration_map_cb,
				       priv);
out:
	g_free (local_filename);
}

/**
 * ch_ccmx_got_file_cb:
 **/
static void
ch_ccmx_got_file_cb (SoupSession *session,
		     SoupMessage *msg,
		     gpointer user_data)
{
	ChCcmxPrivate *priv = (ChCcmxPrivate *) user_data;
	gboolean ret;
	gchar *basename = NULL;
	gchar *location = NULL;
	GError *error = NULL;
	GtkWidget *widget;
	SoupURI *uri;

	/* we failed */
	if (!SOUP_STATUS_IS_SUCCESSFUL (msg->status_code)) {
		uri = soup_message_get_uri (msg);
		location = g_strdup_printf ("%s: %s",
					    soup_status_get_phrase (msg->status_code),
					    uri->path);
		ch_ccmx_error_dialog (priv,
				      _("Failed to download file"),
				      location);
		goto out;
	}

	/* empty file */
	if (msg->response_body->length == 0) {
		ch_ccmx_error_dialog (priv,
				      _("File has zero size"),
				      soup_status_get_phrase (msg->status_code));
		goto out;
	}

	/* write file */
	uri = soup_message_get_uri (msg);
	basename = g_path_get_basename (soup_uri_get_path (uri));
	location = g_build_filename (g_get_user_data_dir (),
				     "colorhug-ccmx",
				     basename,
				     NULL);
	ret = g_file_set_contents (location,
				   msg->response_body->data,
				   msg->response_body->length,
				   &error);
	if (!ret) {
		ch_ccmx_error_dialog (priv,
				      _("Failed to write file"),
				      error->message);
		g_error_free (error);
		goto out;
	}

	/* update UI */
	if (--priv->ccmx_idx == 0) {
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "box_progress"));
		gtk_widget_hide (widget);
		ch_ccmx_add_local_files (priv);
	}
out:
	g_free (basename);
	g_free (location);
	return;
}

/**
 * ch_ccmx_download_file:
 **/
static void
ch_ccmx_download_file (ChCcmxPrivate *priv, const gchar *uri)
{
	const gchar *title;
	SoupMessage *msg = NULL;
	SoupURI *base_uri = NULL;

	/* GET file */
	base_uri = soup_uri_new (uri);
	msg = soup_message_new_from_uri (SOUP_METHOD_GET, base_uri);
	if (msg == NULL) {
		/* TRANSLATORS: internal error when setting up HTTP request */
		title = _("Failed to setup message");
		ch_ccmx_error_dialog (priv, title, uri);
		goto out;
	}

	/* send sync */
	soup_session_queue_message (priv->session, msg,
				    ch_ccmx_got_file_cb, priv);
out:
	if (base_uri != NULL)
		soup_uri_free (base_uri);
}

/**
 * ch_ccmx_got_index_cb:
 **/
static void
ch_ccmx_got_index_cb (SoupSession *session,
		      SoupMessage *msg,
		      gpointer user_data)
{
	const gchar *title;
	ChCcmxPrivate *priv = (ChCcmxPrivate *) user_data;
	gboolean ret;
	gchar *filename_tmp;
	gchar **lines = NULL;
	gchar *location = NULL;
	gchar *server_uri = NULL;
	gchar *uri_tmp;
	GtkWidget *widget;
	guint i;

	/* we failed */
	if (!SOUP_STATUS_IS_SUCCESSFUL (msg->status_code)) {
		/* TRANSLATORS: could not download the directory listing */
		title = _("Failed to get the list of firmware files");
		ch_ccmx_error_dialog (priv, title, soup_status_get_phrase (msg->status_code));
		goto out;
	}

	/* empty file */
	if (msg->response_body->length == 0) {
		/* TRANSLATORS: the directory listing returned no results */
		title = _("Firmware list has zero size");
		ch_ccmx_error_dialog (priv, title, soup_status_get_phrase (msg->status_code));
		goto out;
	}

	/* check cache directory exists */
	location = g_build_filename (g_get_user_data_dir (),
				     "colorhug-ccmx",
				     NULL);
	ret = ch_ccmx_create_user_datadir (priv, location);
	if (!ret)
		goto out;

	/* reset the counter */
	priv->ccmx_idx = 0;

	/* read file */
	server_uri = g_settings_get_string (priv->settings, "server-uri");
	lines = g_strsplit (msg->response_body->data, "\n", -1);
	for (i = 0; lines[i] != NULL; i++) {
		if (lines[i][0] == '\0')
			continue;

		/* check if file already exists, otherwise download */
		filename_tmp = g_build_filename (location,
						 lines[i],
						 NULL);
		ret = g_file_test (filename_tmp, G_FILE_TEST_EXISTS);
		if (!ret) {
			uri_tmp = g_build_filename (server_uri,
						    ch_ccmx_get_device_download_kind (priv),
						    "ccmx",
						    lines[i],
						    NULL);
			priv->ccmx_idx++;
			g_debug ("download %s to %s",
				 uri_tmp, filename_tmp);
			ch_ccmx_download_file (priv, uri_tmp);
			g_free (uri_tmp);
		}
		g_free (filename_tmp);
	}

	/* nothing to do */
	if (priv->ccmx_idx == 0) {
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "box_progress"));
		gtk_widget_hide (widget);
	}
out:
	g_free (server_uri);
	g_free (location);
	g_strfreev (lines);
	return;
}

static void ch_ccmx_gen_setup_page (ChCcmxPrivate *priv);

static gboolean
ch_ccmx_loop_quit_cb (gpointer user_data)
{
	ChCcmxPrivate *priv = (ChCcmxPrivate *) user_data;
	g_main_loop_quit (priv->gen_loop);
	return FALSE;
}

/**
 * ch_ccmx_measure_patches_spectro:
 **/
static void
ch_ccmx_measure_patches_spectro (ChCcmxPrivate *priv)
{
	CdColorRGB rgb;
	CdColorXYZ xyz;
	CdColorXYZ *xyz_tmp;
	const gchar *tmp;
	gboolean ret;
	GError *error = NULL;
	GtkWidget *widget;
	guint i;
	guint len;

	/* only lock once */
	if (!cd_sensor_get_locked (priv->gen_sensor_spectral)) {
		ret = cd_sensor_lock_sync (priv->gen_sensor_spectral, NULL, &error);
		if (!ret) {
			g_warning ("failed to lock sensor: %s", error->message);
			g_error_free (error);
			goto out;
		}
	}

	len = cd_it8_get_data_size (priv->gen_ti1);
	for (i = 0; i < len; i++) {
		cd_it8_get_data_item (priv->gen_ti1,
				      i,
				      &rgb,
				      &xyz);
		cd_sample_widget_set_color (CD_SAMPLE_WIDGET (priv->gen_sample_widget),
					    &rgb);
		g_timeout_add (CH_CCMX_DISPLAY_REFRESH_TIME,
			       ch_ccmx_loop_quit_cb,
			       priv);
		g_main_loop_run (priv->gen_loop);
		xyz_tmp = cd_sensor_get_sample_sync (priv->gen_sensor_spectral,
						     CD_SENSOR_CAP_LCD,
						     NULL,
						     &error);
		if (xyz_tmp == NULL) {
			if (g_error_matches (error,
					     CD_SENSOR_ERROR,
					     CD_SENSOR_ERROR_REQUIRED_POSITION_CALIBRATE)) {
				priv->gen_waiting_for_interaction = TRUE;
				widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_gen_next"));
				gtk_widget_set_sensitive (widget, TRUE);
				widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_gen_measure"));
				/* TRANSLATORS: the user needs to change something on the device */
				gtk_label_set_markup (GTK_LABEL (widget), _("Set the device to the calibrate position and click 'Next'."));
				widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "image_gen_measure"));
				tmp = cd_sensor_get_metadata_item (priv->gen_sensor_spectral,
								   CD_SENSOR_METADATA_IMAGE_CALIBRATE);
				gtk_image_set_from_file (GTK_IMAGE (widget), tmp);
				gtk_widget_set_visible (widget, TRUE);
				gtk_widget_set_visible (priv->gen_sample_widget, FALSE);
				g_error_free (error);
				goto out;
			}
			if (g_error_matches (error,
					     CD_SENSOR_ERROR,
					     CD_SENSOR_ERROR_REQUIRED_POSITION_SURFACE)) {
				priv->gen_waiting_for_interaction = TRUE;
				widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_gen_next"));
				gtk_widget_set_sensitive (widget, TRUE);
				widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_gen_measure"));
				/* TRANSLATORS: the user needs to change something on the device */
				gtk_label_set_markup (GTK_LABEL (widget), _("Set the device to the surface position and click 'Next'."));
				g_error_free (error);
				widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "image_gen_measure"));
				tmp = cd_sensor_get_metadata_item (priv->gen_sensor_spectral,
								   CD_SENSOR_METADATA_IMAGE_SCREEN);
				gtk_image_set_from_file (GTK_IMAGE (widget), tmp);
				gtk_widget_set_visible (widget, TRUE);
				gtk_widget_set_visible (priv->gen_sample_widget, FALSE);
				goto out;
			}
			g_warning ("failed to get sample: %s", error->message);
			g_error_free (error);
			goto out;
		}
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "progressbar_gen_measure"));
		gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (widget), (gfloat) i / (len - 1));
		gtk_widget_set_visible (widget, TRUE);
		cd_it8_add_data (priv->gen_ti3_spectral, &rgb, xyz_tmp);
		g_debug ("for %f,%f,%f got %f,%f,%f",
			 rgb.R, rgb.G, rgb.B,
			 xyz_tmp->X, xyz_tmp->Y, xyz_tmp->Z);
		cd_color_xyz_free (xyz_tmp);
	}

	/* unlock */
	ret = cd_sensor_unlock_sync (priv->gen_sensor_spectral, NULL, &error);
	if (!ret) {
		g_warning ("failed to unlock sensor: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* set next page */
	priv->gen_current_page++;
	ch_ccmx_gen_setup_page (priv);
out:
	return;
}

static void
ch_ccmx_get_sample_colorhug_cb (GObject *source_object,
				GAsyncResult *res,
				gpointer user_data)
{
	ChCcmxPrivate *priv = (ChCcmxPrivate *) user_data;
	gboolean ret;
	GError *error = NULL;

	ret = ch_device_queue_process_finish (CH_DEVICE_QUEUE (source_object),
					      res,
					      &error);
	if (!ret) {
		g_warning ("failed to get sample: %s", error->message);
		g_error_free (error);
	}
	g_main_loop_quit (priv->gen_loop);
}

static void
ch_ccmx_measure_patches_colorhug (ChCcmxPrivate *priv)
{
	CdColorRGB rgb;
	CdColorXYZ xyz;
	GtkWidget *widget;
	guint i;
	guint len;

	len = cd_it8_get_data_size (priv->gen_ti1);
	for (i = 0; i < len; i++) {
		cd_it8_get_data_item (priv->gen_ti1,
				      i,
				      &rgb,
				      &xyz);
		cd_sample_widget_set_color (CD_SAMPLE_WIDGET (priv->gen_sample_widget),
					    &rgb);
		g_timeout_add (CH_CCMX_DISPLAY_REFRESH_TIME,
			       ch_ccmx_loop_quit_cb,
			       priv);
		g_main_loop_run (priv->gen_loop);
		ch_device_queue_set_integral_time (priv->device_queue,
						   priv->device,
						   CH_INTEGRAL_TIME_VALUE_MAX);
		ch_device_queue_set_multiplier (priv->device_queue,
						priv->device,
						CH_FREQ_SCALE_100);
		ch_device_queue_take_readings_xyz (priv->device_queue,
						   priv->device,
						   CH_CALIBRATION_INDEX_FACTORY_ONLY,
						   &xyz);
		ch_device_queue_process_async (priv->device_queue,
					       CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
					       NULL,
					       ch_ccmx_get_sample_colorhug_cb,
					       priv);
		g_main_loop_run (priv->gen_loop);
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "progressbar_gen_measure"));
		gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (widget), (gfloat) i / (len - 1));
		gtk_widget_set_visible (widget, TRUE);
		cd_it8_add_data (priv->gen_ti3_colorhug, &rgb, &xyz);
		g_debug ("for %f,%f,%f got %f,%f,%f",
			 rgb.R, rgb.G, rgb.B,
			 xyz.X, xyz.Y, xyz.Z);
	}

	/* set next page */
	priv->gen_current_page++;
	ch_ccmx_gen_setup_page (priv);
}

/**
 * ch_ccmx_gen_window_move:
 **/
static void
ch_ccmx_gen_window_move (ChCcmxPrivate *priv)
{
	const gchar *xrandr_name;
	gchar *plug_name;
	GdkRectangle rect;
	GdkScreen *screen;
	gint i;
	gint monitor_num = -1;
	gint num_monitors;
	gint win_height;
	gint win_width;
	GtkWindow *window;

	/* find the monitor num of the device output */
	screen = gdk_screen_get_default ();
	num_monitors = gdk_screen_get_n_monitors (screen);
	xrandr_name = cd_device_get_metadata_item (priv->gen_device,
						   CD_DEVICE_METADATA_XRANDR_NAME);
	for (i = 0; i < num_monitors; i++) {
		plug_name = gdk_screen_get_monitor_plug_name (screen, i);
		if (g_strcmp0 (plug_name, xrandr_name) == 0)
			monitor_num = i;
		g_free (plug_name);
	}
	if (monitor_num == -1) {
		g_warning ("failed to find output %s", xrandr_name);
		goto out;
	}

	/* move the window, and set it to the right size */
	gdk_screen_get_monitor_geometry (screen, monitor_num, &rect);
	window = GTK_WINDOW (gtk_builder_get_object (priv->builder, "dialog_ccmx"));
	gtk_window_get_size (window, &win_width, &win_height);
	gtk_window_move (window,
			 rect.x + ((rect.width - win_width) / 2),
			 rect.y + ((rect.height - win_height) / 2));
out:
	return;
}

/**
 * ch_ccmx_gen_setup_page:
 **/
static void
ch_ccmx_gen_setup_page (ChCcmxPrivate *priv)
{
	GtkNotebook *notebook;
	GtkWidget *widget;
	gchar *markup = NULL;
	gboolean ret;
	GError *error = NULL;

	notebook = GTK_NOTEBOOK (gtk_builder_get_object (priv->builder, "notebook_gen"));
	switch (priv->gen_current_page) {
	case CH_CCMX_PAGE_DEVICES:
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_gen_next"));
		gtk_widget_set_sensitive (widget, FALSE);
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "image_gen_measure"));
		gtk_widget_set_visible (widget, FALSE);
		gtk_notebook_set_current_page (notebook, 0);
		break;
	case CH_CCMX_PAGE_REFERENCE:
		/* show ref measure */
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_gen_measure"));
		gtk_label_set_markup (GTK_LABEL (widget), _("Put the photospectrometer on the screen and click 'Next'."));
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_gen_next"));
		gtk_widget_set_sensitive (widget, TRUE);
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "image_gen_measure"));
		gtk_image_set_from_file (GTK_IMAGE (widget), cd_sensor_get_metadata_item (priv->gen_sensor_spectral, CD_SENSOR_METADATA_IMAGE_ATTACH));
		gtk_widget_set_visible (widget, TRUE);
		gtk_widget_set_visible (priv->gen_sample_widget, FALSE);
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "progressbar_gen_measure"));
		gtk_widget_set_visible (widget, FALSE);
		gtk_notebook_set_current_page (notebook, 1);

		/* move window to right screen */
		ch_ccmx_gen_window_move (priv);
		break;
	case CH_CCMX_PAGE_REFERENCE_ACTION:
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_gen_measure"));
		markup = g_strdup_printf ("<b>%s</b>", _("Do not remove the device whilst measurement is in progress"));
		gtk_label_set_markup (GTK_LABEL (widget), markup);
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_gen_next"));
		gtk_widget_set_sensitive (widget, FALSE);
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "image_gen_measure"));
		gtk_widget_set_visible (widget, FALSE);
		gtk_widget_set_visible (priv->gen_sample_widget, TRUE);
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "progressbar_gen_measure"));
		gtk_widget_set_visible (widget, FALSE);
		/* measure spectro ti3 */
		ch_ccmx_measure_patches_spectro (priv);
		break;
	case CH_CCMX_PAGE_COLORHUG:
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_gen_measure"));
		gtk_label_set_markup (GTK_LABEL (widget), _("Now put the ColorHug on the screen and click 'Next' again."));
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_gen_next"));
		gtk_widget_set_sensitive (widget, TRUE);
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "progressbar_gen_measure"));
		gtk_widget_set_visible (widget, FALSE);
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "image_gen_measure"));
		gtk_image_set_from_file (GTK_IMAGE (widget), cd_sensor_get_metadata_item (priv->gen_sensor_colorhug, CD_SENSOR_METADATA_IMAGE_ATTACH));
		gtk_widget_set_visible (widget, TRUE);
		gtk_widget_set_visible (priv->gen_sample_widget, FALSE);
		gtk_notebook_set_current_page (notebook, 1);
		break;
	case CH_CCMX_PAGE_COLORHUG_ACTION:
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_gen_measure"));
		markup = g_strdup_printf ("<b>%s</b>", _("Do not remove the device whilst measurement is in progress"));
		gtk_label_set_markup (GTK_LABEL (widget), markup);
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_gen_next"));
		gtk_widget_set_sensitive (widget, FALSE);
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "progressbar_gen_measure"));
		gtk_widget_set_visible (widget, FALSE);
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "image_gen_measure"));
		gtk_widget_set_visible (widget, FALSE);
		gtk_widget_set_visible (priv->gen_sample_widget, TRUE);
		/* measure colorhug ti3 */
		ch_ccmx_measure_patches_colorhug (priv);
		break;
	case CH_CCMX_PAGE_SUMMARY:

		/* uninhibit */
		ret = cd_device_profiling_uninhibit_sync (priv->gen_device,
							  NULL,
							  &error);
		if (!ret) {
			g_warning ("failed to uninhibit device: %s", error->message);
			g_error_free (error);
			goto out;
		}

		/* show summary */
		gtk_notebook_set_current_page (notebook, 2);
		gtk_widget_set_visible (priv->gen_sample_widget, FALSE);
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "image_gen_measure"));
		gtk_widget_set_visible (widget, FALSE);
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_gen_next"));
		gtk_widget_set_visible (widget, FALSE);

		/* generate ccmx */
		ret = cd_it8_utils_calculate_ccmx (priv->gen_ti3_spectral,
						   priv->gen_ti3_colorhug,
						   priv->gen_ccmx,
						   &error);
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_gen_done_share"));
		gtk_widget_set_visible (widget, ret);
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_gen_done_save"));
		gtk_widget_set_visible (widget, ret);
		if (!ret) {
			g_warning ("Failed to calculate CCMX: %s", error->message);
			widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_gen_done_msg"));
			markup = g_strdup_printf ("<b>%s</b>\n%s", _("Correction matrix failed to be generated!"), error->message);
			gtk_label_set_markup (GTK_LABEL (widget), markup);
			g_error_free (error);
			goto out;
		} else {
			widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_gen_done_msg"));
			markup = g_strdup_printf ("<b>%s</b>\n", _("Correction matrix successfully generated!"));
			gtk_label_set_markup (GTK_LABEL (widget), markup);
		}
		break;
	default:
		g_assert_not_reached ();
	}
out:
	g_free (markup);
}

/**
 * ch_ccmx_gen_default_ccmx_filename:
 **/
static gchar *
ch_ccmx_gen_default_ccmx_filename (ChCcmxPrivate *priv)
{
	gchar *tmp;
	gchar *filename;

	tmp = g_strdup_printf ("%s-%s-%s.ccmx",
				cd_sensor_kind_to_string (CD_SENSOR_KIND_COLORHUG),
				cd_device_get_vendor (priv->gen_device),
				cd_device_get_model (priv->gen_device));
	g_strdelimit (tmp, " ", '-');
	filename = g_ascii_strdown (tmp, -1);
	g_free (tmp);
	return filename;
}

/**
 * ch_ccmx_gen_done_share_button_cb:
 **/
static void
ch_ccmx_gen_done_share_button_cb (GtkWidget *widget, ChCcmxPrivate *priv)
{
	const gchar *uri;
	gboolean ret = TRUE;
	gchar *ccmx_filename = NULL;
	gchar *data = NULL;
	GError *error = NULL;
	gsize length;
	guint status_code;
	SoupBuffer *buffer = NULL;
	SoupMessage *msg = NULL;
	SoupMultipart *multipart = NULL;

	/* get file data */
	ret = cd_it8_save_to_data (priv->gen_ccmx, &data, &length, &error);
	if (!ret) {
		g_warning ("failed to save file: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* get default filename */
	ccmx_filename = ch_ccmx_gen_default_ccmx_filename (priv);

	/* create multipart form and upload file */
	multipart = soup_multipart_new (SOUP_FORM_MIME_TYPE_MULTIPART);
	buffer = soup_buffer_new (SOUP_MEMORY_STATIC, data, length);
	soup_multipart_append_form_file (multipart,
					 "upload",
					 ccmx_filename,
					 NULL,
					 buffer);
	msg = soup_form_request_new_from_multipart (CH_CCMX_CCMX_UPLOAD_SERVER, multipart);
	status_code = soup_session_send_message (priv->session, msg);
	if (!SOUP_STATUS_IS_SUCCESSFUL (status_code)) {
		ret = FALSE;
		g_warning ("Failed to upload file: %s",
			   msg->reason_phrase);
		goto out;
	}
	uri = soup_message_headers_get_one (msg->response_headers, "Location");
	g_debug ("Successfully uploaded to %s", uri);

	/* disable button */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_gen_done_share"));
	gtk_widget_set_sensitive (widget, FALSE);
out:
	if (buffer != NULL)
		soup_buffer_free (buffer);
	if (msg != NULL)
		g_object_unref (msg);
	if (multipart != NULL)
		soup_multipart_free (multipart);
	g_free (ccmx_filename);
	g_free (data);
}

/**
 * ch_ccmx_gen_done_save_button_cb:
 **/
static void
ch_ccmx_gen_done_save_button_cb (GtkWidget *widget, ChCcmxPrivate *priv)
{
	gboolean ret;
	gchar *current_folder;
	gchar *current_name;
	gchar *filename = NULL;
	GError *error = NULL;
	GFile *file = NULL;
	GtkWidget *dialog;
	GtkWindow *window;

	window = GTK_WINDOW (gtk_builder_get_object (priv->builder, "dialog_gen"));
	dialog = gtk_file_chooser_dialog_new ("Save File",
					      window,
					      GTK_FILE_CHOOSER_ACTION_SAVE,
					      GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
					      GTK_STOCK_SAVE, GTK_RESPONSE_ACCEPT,
					      NULL);
	gtk_file_chooser_set_do_overwrite_confirmation (GTK_FILE_CHOOSER (dialog), TRUE);

	current_folder = g_build_filename (g_get_home_dir (),
					   ".local",
					   "share",
					   "colorhug-ccmx",
					   NULL);
	gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER (dialog), current_folder);
	current_name = ch_ccmx_gen_default_ccmx_filename (priv);
	gtk_file_chooser_set_current_name (GTK_FILE_CHOOSER (dialog), current_name);

	if (gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_ACCEPT) {
		filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (dialog));
		g_debug ("saving CCMX %s", filename);
		file = g_file_new_for_path (filename);
		ret = cd_it8_save_to_file (priv->gen_ccmx, file, &error);
		if (!ret) {
			g_warning ("failed to save file: %s", error->message);
			g_error_free (error);
			goto out;
		}
	}
out:
	if (file != NULL)
		g_object_unref (file);
	gtk_widget_destroy (dialog);
	g_free (filename);
	g_free (current_name);
	g_free (current_folder);
}

/**
 * ch_ccmx_gen_next_button_cb:
 **/
static void
ch_ccmx_gen_next_button_cb (GtkWidget *widget, ChCcmxPrivate *priv)
{
	if (!priv->gen_waiting_for_interaction)
		priv->gen_current_page++;
	priv->gen_waiting_for_interaction = FALSE;
	ch_ccmx_gen_setup_page (priv);
}

/**
 * ch_ccmx_gen_add_device:
 **/
static void
ch_ccmx_gen_add_device (ChCcmxPrivate *priv, CdDevice *device)
{
	GtkListStore *list_store;
	GtkTreeIter iter;
	gchar *title;

	title = g_strdup_printf ("%s - %s",
				 cd_device_get_vendor (device),
				 cd_device_get_model (device));
	list_store = GTK_LIST_STORE (gtk_builder_get_object (priv->builder, "liststore_devices"));
	gtk_list_store_append (list_store, &iter);
	gtk_list_store_set (list_store, &iter,
			    0, device,
			    1, title,
			    -1);
	g_free (title);
}

/**
 * ch_ccmx_client_get_devices_cb:
 **/
static void
ch_ccmx_client_get_devices_cb (GObject *object,
			       GAsyncResult *res,
			       gpointer user_data)
{
	CdDevice *device_tmp;
	ChCcmxPrivate *priv = (ChCcmxPrivate *) user_data;
	gboolean ret;
	GError *error = NULL;
	GPtrArray *array;
	guint i;

	array = cd_client_get_devices_finish (CD_CLIENT (object), res, &error);
	if (array == NULL) {
		g_warning ("Failed to get display devices: %s", error->message);
		g_error_free (error);
		goto out;
	}
	for (i = 0; i < array->len; i++) {
		device_tmp = g_ptr_array_index (array, i);
		ret = cd_device_connect_sync (device_tmp, NULL, &error);
		if (!ret) {
			g_warning ("Failed to contact device %s: %s",
				   cd_device_get_object_path (device_tmp),
				   error->message);
			g_error_free (error);
			goto out;
		}
		ch_ccmx_gen_add_device (priv, device_tmp);
	}
out:
	if (array != NULL)
		g_ptr_array_unref (array);
}

/**
 * ch_ccmx_generate_button_cb:
 **/
static void
ch_ccmx_generate_button_cb (GtkWidget *widget, ChCcmxPrivate *priv)
{
	GtkWindow *window;
	GtkListStore *list_store;

	/* clear devices */
	list_store = GTK_LIST_STORE (gtk_builder_get_object (priv->builder, "liststore_devices"));
	gtk_list_store_clear (list_store);

	/* get display devices */
	cd_client_get_devices_by_kind (priv->gen_client,
				       CD_DEVICE_KIND_DISPLAY,
				       NULL,
				       ch_ccmx_client_get_devices_cb,
				       priv);

	/* start ccmx generation */
	priv->gen_current_page = CH_CCMX_PAGE_DEVICES;
	ch_ccmx_gen_setup_page (priv);
	window = GTK_WINDOW (gtk_builder_get_object (priv->builder, "dialog_gen"));
	gtk_window_present (window);
}

/**
 * ch_ccmx_refresh_button_cb:
 **/
static void
ch_ccmx_refresh_button_cb (GtkWidget *widget, ChCcmxPrivate *priv)
{
	const gchar *title;
	gchar *server_uri = NULL;
	gchar *uri = NULL;
	SoupMessage *msg = NULL;
	SoupURI *base_uri = NULL;

	/* setup UI */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_msg"));
	/* TRANSLATORS: get the list of firmwares from the internet */
	title = _("Getting latest data from the web…");
	gtk_label_set_label (GTK_LABEL (widget), title);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "box_progress"));
	gtk_widget_show_all (widget);

	/* get the latest INDEX file */
	server_uri = g_settings_get_string (priv->settings, "server-uri");
	uri = g_build_filename (server_uri,
				ch_ccmx_get_device_download_kind (priv),
				"ccmx",
				"INDEX",
				NULL);
	base_uri = soup_uri_new (uri);

	/* GET file */
	msg = soup_message_new_from_uri (SOUP_METHOD_GET, base_uri);
	if (msg == NULL) {
		/* TRANSLATORS: internal error when setting up HTTP request */
		title = _("Failed to setup message");
		ch_ccmx_error_dialog (priv, title, NULL);
		goto out;
	}

	/* send sync */
	soup_session_queue_message (priv->session, msg,
				    ch_ccmx_got_index_cb, priv);
out:
	if (base_uri != NULL)
		soup_uri_free (base_uri);
	g_free (server_uri);
	g_free (uri);
}

/**
 * ch_ccmx_get_fake_device:
 **/
static GUsbDevice *
ch_ccmx_get_fake_device (ChCcmxPrivate *priv)
{
	GPtrArray *array;
	GUsbDevice *device = NULL;

	/* just return the first device */
	array = g_usb_device_list_get_devices (priv->device_list);
	if (array->len == 0)
		goto out;
	device = g_object_ref (g_ptr_array_index (array, 0));
out:
	g_ptr_array_unref (array);
	return device;
}

/**
 * ch_ccmx_gen_update_ui:
 **/
static void
ch_ccmx_gen_update_ui (ChCcmxPrivate *priv)
{
	GtkWidget *widget;

	/* next button sensitivity */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_generate"));
	gtk_widget_set_visible (widget,
				priv->gen_sensor_colorhug != NULL &&
				priv->gen_sensor_spectral != NULL);
}

/**
 * ch_ccmx_check_sensor:
 **/
static void
ch_ccmx_check_sensor (ChCcmxPrivate *priv, CdSensor *sensor)
{
	/* is ColorHug sensor */
	if (cd_sensor_get_kind (sensor) == CD_SENSOR_KIND_COLORHUG) {
		if (priv->gen_sensor_colorhug != NULL)
			g_object_unref (priv->gen_sensor_colorhug);
		priv->gen_sensor_colorhug = g_object_ref (sensor);
		cd_it8_set_instrument (priv->gen_ti3_colorhug,
				       cd_sensor_get_model (sensor));
		ch_ccmx_gen_update_ui (priv);
		goto out;
	}

	/* is spectral sensor */
	if (cd_sensor_get_kind (sensor) == CD_SENSOR_KIND_COLOR_MUNKI_PHOTO ||
	    cd_sensor_get_kind (sensor) == CD_SENSOR_KIND_I1_PRO) {
		if (priv->gen_sensor_spectral != NULL)
			g_object_unref (priv->gen_sensor_spectral);
		priv->gen_sensor_spectral = g_object_ref (sensor);
		cd_it8_set_instrument (priv->gen_ti3_spectral,
				       cd_sensor_get_model (sensor));
		ch_ccmx_gen_update_ui (priv);
		goto out;
	}
out:
	return;
}

/**
 * ch_ccmx_client_get_sensors_cb:
 **/
static void
ch_ccmx_client_get_sensors_cb (GObject *object,
			       GAsyncResult *res,
			       gpointer user_data)
{
	CdSensor *sensor_tmp;
	ChCcmxPrivate *priv = (ChCcmxPrivate *) user_data;
	gboolean ret;
	GError *error = NULL;
	GPtrArray *array;
	guint i;

	/* get all the sensors */
	array = cd_client_get_sensors_finish (CD_CLIENT (object), res, &error);
	if (array == NULL) {
		g_warning ("Failed to get display devices: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* conect to all the sensors */
	for (i = 0; i < array->len; i++) {
		sensor_tmp = g_ptr_array_index (array, i);
		ret = cd_sensor_connect_sync (sensor_tmp, NULL, &error);
		if (!ret) {
			g_warning ("Failed to contact sensor %s: %s",
				   cd_sensor_get_object_path (sensor_tmp),
				   error->message);
			g_error_free (error);
			goto out;
		}
		ch_ccmx_check_sensor (priv, sensor_tmp);
	}
out:
	if (array != NULL)
		g_ptr_array_unref (array);
}

/**
 * ch_ccmx_sensor_added_cb:
 **/
static void
ch_ccmx_sensor_added_cb (CdClient *gen_client,
			 CdSensor *sensor,
			 ChCcmxPrivate *priv)
{
	gboolean ret;
	GError *error = NULL;

	ret = cd_sensor_connect_sync (sensor, NULL, &error);
	if (!ret) {
		g_warning ("Failed to contact sensor %s: %s",
			   cd_sensor_get_object_path (sensor),
			   error->message);
		g_error_free (error);
		return;
	}
	ch_ccmx_check_sensor (priv, sensor);
}

/**
 * ch_ccmx_sensor_removed_cb:
 **/
static void
ch_ccmx_sensor_removed_cb (CdClient *gen_client,
			   CdSensor *sensor,
			   ChCcmxPrivate *priv)
{
	g_warning ("Sensor %s removed, calibration cay fail",
		   cd_sensor_get_object_path (sensor));
}

/**
 * ch_ccmx_client_connect_cb:
 **/
static void
ch_ccmx_client_connect_cb (GObject *object,
			   GAsyncResult *res,
			   gpointer user_data)
{
	ChCcmxPrivate *priv = (ChCcmxPrivate *) user_data;
	gboolean ret;
	GError *error = NULL;

	ret = cd_client_connect_finish (CD_CLIENT (object), res, &error);
	if (!ret) {
		g_warning ("Failed to contact colord: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* get sensors */
	cd_client_get_sensors (priv->gen_client,
			       NULL,
			       ch_ccmx_client_get_sensors_cb,
			       priv);

	/* watch for changes */
	g_signal_connect (priv->gen_client, "sensor-added",
			  G_CALLBACK (ch_ccmx_sensor_added_cb), priv);
	g_signal_connect (priv->gen_client, "sensor-removed",
			  G_CALLBACK (ch_ccmx_sensor_removed_cb), priv);

out:
	return;
}

/**
 * gpk_ccmx_treeview_clicked_cb:
 **/
static void
gpk_ccmx_treeview_clicked_cb (GtkTreeSelection *selection,
			      ChCcmxPrivate *priv)
{
	gboolean ret;
	GError *error = NULL;
	GtkTreeIter iter;
	GtkTreeModel *model;
	GtkWidget *widget;

	if (!gtk_tree_selection_get_selected (selection, &model, &iter)) {
		g_debug ("no row selected");
		return;
	}

	/* get new selection */
	if (priv->gen_device != NULL) {
		ret = cd_device_profiling_uninhibit_sync (priv->gen_device,
							  NULL,
							  &error);
		if (!ret) {
			g_warning ("failed to uninhibit device: %s", error->message);
			g_error_free (error);
			goto out;
		}
		g_object_unref (priv->gen_device);
	}
	gtk_tree_model_get (model, &iter,
			    0, &priv->gen_device,
			    -1);

	/* inhibit device */
	ret = cd_device_profiling_inhibit_sync (priv->gen_device,
						NULL,
						&error);
	if (!ret) {
		g_warning ("failed to inhibit device: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* set next button */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_gen_next"));
	gtk_widget_set_sensitive (widget, TRUE);
out:
	return;
}

/**
 * ch_ccmx_startup_cb:
 **/
static void
ch_ccmx_startup_cb (GApplication *application, ChCcmxPrivate *priv)
{
	CdColorRGB rgb;
	CdColorXYZ xyz;
	const gchar *title;
	gchar *title_ccmx = NULL;
	GdkPixbuf *pixbuf;
	GError *error = NULL;
	gint retval;
	GtkCellRenderer *renderer;
	GtkListStore *list_store;
	GtkTreeSelection *selection;
	GtkTreeViewColumn *column;
	GtkWidget *main_window;
	GtkWidget *widget;
	guint i;

	/* get UI */
	priv->builder = gtk_builder_new ();
	retval = gtk_builder_add_from_resource (priv->builder,
						"/com/hughski/colorhug/ch-ccmx.ui",
						&error);
	if (retval == 0) {
		g_warning ("failed to load ui: %s",
			   error->message);
		g_error_free (error);
		goto out;
	}

	/* generate source priv->gen_ti1 with 5*5 patches */
	priv->gen_ti1 = cd_it8_new_with_kind (CD_IT8_KIND_TI1);
	cd_it8_set_title (priv->gen_ti1, "Source data");
	cd_color_xyz_clear (&xyz);
	for (i = 0; i < 3; i++) {
		cd_color_rgb_set (&rgb, 0.0, 0.0, 0.0);
		cd_it8_add_data (priv->gen_ti1, &rgb, &xyz);
		cd_color_rgb_set (&rgb, 1.0, 1.0, 1.0);
		cd_it8_add_data (priv->gen_ti1, &rgb, &xyz);
		cd_color_rgb_set (&rgb, 1.0, 0.0, 0.0);
		cd_it8_add_data (priv->gen_ti1, &rgb, &xyz);
		cd_color_rgb_set (&rgb, 0.0, 1.0, 0.0);
		cd_it8_add_data (priv->gen_ti1, &rgb, &xyz);
		cd_color_rgb_set (&rgb, 0.0, 0.0, 1.0);
		cd_it8_add_data (priv->gen_ti1, &rgb, &xyz);
	}
	priv->gen_ti3_colorhug = cd_it8_new_with_kind (CD_IT8_KIND_TI3);
	priv->gen_ti3_spectral = cd_it8_new_with_kind (CD_IT8_KIND_TI3);
	priv->gen_ccmx = cd_it8_new_with_kind (CD_IT8_KIND_CCMX);
	cd_it8_add_option (priv->gen_ccmx, "TYPE_LCD");
	cd_it8_set_originator (priv->gen_ccmx, "colorhug-ccmx");
	title_ccmx = g_strdup_printf ("%s %s",
				      cd_device_get_vendor (priv->gen_device),
				      cd_device_get_model (priv->gen_device));
	cd_it8_set_title (priv->gen_ccmx, title_ccmx);

	/* setup devices treeview */
	renderer = gtk_cell_renderer_text_new ();
	column = gtk_tree_view_column_new_with_attributes ("Device", renderer,
							   "markup", 1, NULL);
	gtk_tree_view_column_set_sort_column_id (column, 1);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "treeview_gen_detect"));
	gtk_tree_view_append_column (GTK_TREE_VIEW (widget), column);
	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (widget));
	g_signal_connect (selection, "changed",
			  G_CALLBACK (gpk_ccmx_treeview_clicked_cb), priv);

	/* add application specific icons to search path */
	gtk_icon_theme_append_search_path (gtk_icon_theme_get_default (),
					   CH_DATA G_DIR_SEPARATOR_S "icons");

	main_window = GTK_WIDGET (gtk_builder_get_object (priv->builder, "dialog_ccmx"));
	gtk_application_add_window (priv->application, GTK_WINDOW (main_window));
	gtk_widget_set_size_request (main_window, 400, 100);

	/* Hide window first so that the dialogue resizes itself without redrawing */
	gtk_widget_hide (main_window);

	/* buttons */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_close"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (ch_ccmx_close_button_cb), priv);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_help"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (ch_ccmx_help_button_cb), priv);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_gen_close"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (ch_ccmx_gen_close_button_cb), priv);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_import"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (ch_ccmx_import_button_cb), priv);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_refresh"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (ch_ccmx_refresh_button_cb), priv);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_generate"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (ch_ccmx_generate_button_cb), priv);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_gen_next"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (ch_ccmx_gen_next_button_cb), priv);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_gen_done_save"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (ch_ccmx_gen_done_save_button_cb), priv);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_gen_done_share"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (ch_ccmx_gen_done_share_button_cb), priv);

	/* setup logo image */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "image_logo"));
	gtk_image_set_from_icon_name (GTK_IMAGE (widget),
				      "colorhug-gray",
				      GTK_ICON_SIZE_DIALOG);

	/* setup list stores */
	list_store = GTK_LIST_STORE (gtk_builder_get_object (priv->builder, "liststore_lcd"));
	gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (list_store),
					      COLUMN_DESCRIPTION,
					      GTK_SORT_ASCENDING);
	list_store = GTK_LIST_STORE (gtk_builder_get_object (priv->builder, "liststore_led"));
	gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (list_store),
					      COLUMN_DESCRIPTION,
					      GTK_SORT_ASCENDING);
	list_store = GTK_LIST_STORE (gtk_builder_get_object (priv->builder, "liststore_crt"));
	gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (list_store),
					      COLUMN_DESCRIPTION,
					      GTK_SORT_ASCENDING);
	list_store = GTK_LIST_STORE (gtk_builder_get_object (priv->builder, "liststore_projector"));
	gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (list_store),
					      COLUMN_DESCRIPTION,
					      GTK_SORT_ASCENDING);

	/* setup comboboxes */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "combobox_lcd"));
	g_object_set_data (G_OBJECT (widget),
			   "colorhug-ccmx-idx",
			   GINT_TO_POINTER (0));
	g_signal_connect (widget, "changed",
			  G_CALLBACK (ch_ccmx_combo_changed_cb), priv);
	ch_ccmx_set_combo_simple_text (widget);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "combobox_crt"));
	g_object_set_data (G_OBJECT (widget),
			   "colorhug-ccmx-idx",
			   GINT_TO_POINTER (1));
	g_signal_connect (widget, "changed",
			  G_CALLBACK (ch_ccmx_combo_changed_cb), priv);
	ch_ccmx_set_combo_simple_text (widget);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "combobox_projector"));
	g_object_set_data (G_OBJECT (widget),
			   "colorhug-ccmx-idx",
			   GINT_TO_POINTER (2));
	g_signal_connect (widget, "changed",
			  G_CALLBACK (ch_ccmx_combo_changed_cb), priv);
	ch_ccmx_set_combo_simple_text (widget);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "combobox_led"));
	g_object_set_data (G_OBJECT (widget),
			   "colorhug-ccmx-idx",
			   GINT_TO_POINTER (3));
	g_signal_connect (widget, "changed",
			  G_CALLBACK (ch_ccmx_combo_changed_cb), priv);
	ch_ccmx_set_combo_simple_text (widget);

	/* setup USB image */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "image_usb"));
	pixbuf = gdk_pixbuf_new_from_file_at_scale (CH_DATA
						    G_DIR_SEPARATOR_S "icons"
						    G_DIR_SEPARATOR_S "usb.svg",
						    -1, 48, TRUE, &error);
	g_assert (pixbuf != NULL);
	gtk_image_set_from_pixbuf (GTK_IMAGE (widget), pixbuf);
	g_object_unref (pixbuf);

	/* hide all unused widgets until we've connected with the device */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "grid_data"));
	gtk_widget_hide (widget);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "box_progress"));
	gtk_widget_hide (widget);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "box_header"));
	gtk_widget_hide (widget);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_import"));
	gtk_widget_hide (widget);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_generate"));
	gtk_widget_hide (widget);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_refresh"));
	gtk_widget_hide (widget);

	/* add sample widget */
	priv->gen_sample_widget = cd_sample_widget_new ();
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "box_gen_measure"));
	gtk_box_pack_start (GTK_BOX (widget), priv->gen_sample_widget, TRUE, TRUE, 0);
	gtk_widget_set_size_request (priv->gen_sample_widget,
				     CH_CCMX_SAMPLE_SQUARE_SIZE,
				     CH_CCMX_SAMPLE_SQUARE_SIZE);

	/* is the colorhug already plugged in? */
	g_usb_device_list_coldplug (priv->device_list);

	/* setup the session */
	priv->session = soup_session_sync_new_with_options (SOUP_SESSION_USER_AGENT, "colorhug-ccmx",
							    SOUP_SESSION_TIMEOUT, 5000,
							    NULL);
	if (priv->session == NULL) {
		/* TRANSLATORS: internal error when setting up HTTP */
		title = _("Failed to setup networking");
		ch_ccmx_error_dialog (priv, title, NULL);
		goto out;
	}

	/* automatically use the correct proxies */
	soup_session_add_feature_by_type (priv->session,
					  SOUP_TYPE_PROXY_RESOLVER_DEFAULT);

	/* connect to colord */
	priv->gen_client = cd_client_new ();
	cd_client_connect (priv->gen_client,
			   NULL,
			   ch_ccmx_client_connect_cb,
			   priv);

	/* emulate a device */
	if (g_getenv ("COLORHUG_EMULATE") != NULL) {
		priv->device = ch_ccmx_get_fake_device (priv);
		ch_ccmx_got_device (priv);
	}

	/* show main UI */
	gtk_widget_show (main_window);
out:
	g_free (title_ccmx);
}

/**
 * ch_ccmx_device_added_cb:
 **/
static void
ch_ccmx_device_added_cb (GUsbDeviceList *list,
			 GUsbDevice *device,
			 ChCcmxPrivate *priv)
{
	g_debug ("Added: %i:%i",
		 g_usb_device_get_vid (device),
		 g_usb_device_get_pid (device));
	if (ch_device_get_mode (device) == CH_DEVICE_MODE_FIRMWARE ||
	    ch_device_get_mode (device) == CH_DEVICE_MODE_LEGACY) {
		priv->device = g_object_ref (device);
		ch_ccmx_got_device (priv);
	}
}

/**
 * ch_ccmx_device_removed_cb:
 **/
static void
ch_ccmx_device_removed_cb (GUsbDeviceList *list,
			    GUsbDevice *device,
			    ChCcmxPrivate *priv)
{
	g_debug ("Removed: %i:%i",
		 g_usb_device_get_vid (device),
		 g_usb_device_get_pid (device));
	if (ch_device_get_mode (device) == CH_DEVICE_MODE_FIRMWARE ||
	    ch_device_get_mode (device) == CH_DEVICE_MODE_LEGACY) {
		if (priv->device != NULL)
			g_object_unref (priv->device);
		priv->device = NULL;
	}
}

/**
 * ch_ccmx_ignore_cb:
 **/
static void
ch_ccmx_ignore_cb (const gchar *log_domain, GLogLevelFlags log_level,
		   const gchar *message, gpointer user_data)
{
}

/**
 * main:
 **/
int
main (int argc, char **argv)
{
	ChCcmxPrivate *priv;
	gboolean ret;
	gboolean verbose = FALSE;
	gboolean force_repair = FALSE;
	guint i;
	GError *error = NULL;
	GOptionContext *context;
	int status = 0;
	const GOptionEntry options[] = {
		{ "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose,
			/* TRANSLATORS: command line option */
			_("Show extra debugging information"), NULL },
		{ "repair", 'r', 0, G_OPTION_ARG_NONE, &force_repair,
			/* TRANSLATORS: command line option */
			_("Repair the factory calibration matrix"), NULL },
		{ NULL}
	};

	setlocale (LC_ALL, "");

	bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	gtk_init (&argc, &argv);

	/* TRANSLATORS: A program to load on CCMX correction matrices
	 * onto the hardware */
	context = g_option_context_new (_("ColorHug CCMX loader"));
	g_option_context_add_group (context, gtk_get_option_group (TRUE));
	g_option_context_add_main_entries (context, options, NULL);
	ret = g_option_context_parse (context, &argc, &argv, &error);
	if (!ret) {
		g_warning ("%s: %s",
			   _("Failed to parse command line options"),
			   error->message);
		g_error_free (error);
	}
	g_option_context_free (context);

	priv = g_new0 (ChCcmxPrivate, 1);
	priv->settings = g_settings_new ("com.hughski.colorhug-client");
	priv->needs_repair = TRUE;
	priv->force_repair = force_repair;
	priv->usb_ctx = g_usb_context_new (NULL);
	priv->hash = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	priv->device_list = g_usb_device_list_new (priv->usb_ctx);
	priv->device_queue = ch_device_queue_new ();
	priv->gen_current_page = CH_CCMX_PAGE_DEVICES;
	priv->gen_loop = g_main_loop_new (NULL, FALSE);
	g_signal_connect (priv->device_list, "device-added",
			  G_CALLBACK (ch_ccmx_device_added_cb), priv);
	g_signal_connect (priv->device_list, "device-removed",
			  G_CALLBACK (ch_ccmx_device_removed_cb), priv);

	/* clear initial calibration table */
	for (i = 0; i < CH_CALIBRATION_MAX; i++) {
		priv->ccmx_types[i] = 0;
		priv->ccmx_description[i] = g_new0 (gchar, 24);
	}

	/* ensure single instance */
	priv->application = gtk_application_new ("com.hughski.ColorHug.Ccmx", 0);
	g_signal_connect (priv->application, "startup",
			  G_CALLBACK (ch_ccmx_startup_cb), priv);
	g_signal_connect (priv->application, "activate",
			  G_CALLBACK (ch_ccmx_activate_cb), priv);
	/* set verbose? */
	if (verbose) {
		g_setenv ("COLORHUG_VERBOSE", "1", FALSE);
	} else {
		g_log_set_handler (G_LOG_DOMAIN, G_LOG_LEVEL_DEBUG,
				   ch_ccmx_ignore_cb, NULL);
	}

	/* wait */
	status = g_application_run (G_APPLICATION (priv->application), argc, argv);

	g_object_unref (priv->application);
	if (priv->hash != NULL)
		g_hash_table_destroy (priv->hash);
	if (priv->device_list != NULL)
		g_object_unref (priv->device_list);
	if (priv->device_queue != NULL)
		g_object_unref (priv->device_queue);
	if (priv->usb_ctx != NULL)
		g_object_unref (priv->usb_ctx);
	if (priv->builder != NULL)
		g_object_unref (priv->builder);
	if (priv->session != NULL)
		g_object_unref (priv->session);
	if (priv->settings != NULL)
		g_object_unref (priv->settings);
	if (priv->gen_client != NULL)
		g_object_unref (priv->gen_client);
	if (priv->gen_sensor_colorhug != NULL)
		g_object_unref (priv->gen_sensor_colorhug);
	if (priv->gen_sensor_spectral != NULL)
		g_object_unref (priv->gen_sensor_spectral);
	if (priv->gen_ti1 != NULL)
		g_object_unref (priv->gen_ti1);
	if (priv->gen_ccmx != NULL)
		g_object_unref (priv->gen_ccmx);
	if (priv->gen_ti3_spectral != NULL)
		g_object_unref (priv->gen_ti3_spectral);
	if (priv->gen_ti3_colorhug != NULL)
		g_object_unref (priv->gen_ti3_colorhug);
	if (priv->gen_loop != NULL)
		g_main_loop_unref (priv->gen_loop);
	g_free (priv);
	return status;
}
