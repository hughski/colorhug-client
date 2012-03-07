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
#include <libsoup/soup.h>
#include <lcms2.h>

#include "ch-common.h"
#include "ch-math.h"
#include "ch-device-queue.h"

/* don't change this unless you want to provide ccmx files */
#define COLORHUG_CCMX_LOCATION		"http://www.hughski.com/downloads/colorhug/ccmx/"
#define COLORHUG_ARCHIVE_LOCATION	"http://www.hughski.com/downloads/colorhug/archive/"

typedef struct {
	GtkApplication	*application;
	GtkBuilder	*builder;
	gboolean	 done_get_cal;
	GUsbContext	*usb_ctx;
	GUsbDevice	*device;
	GUsbDeviceList	*device_list;
	SoupSession	*session;
	guint16		 calibration_map[6];
	guint		 ccmx_idx;
	guint8		 ccmx_types[CH_CALIBRATION_MAX];
	gchar		*ccmx_description[CH_CALIBRATION_MAX];
	GHashTable	*hash;
	guint32		 serial_number;
	gboolean	 needs_repair;
	gboolean	 force_repair;
	ChDeviceQueue	*device_queue;
} ChCcmxPrivate;

enum {
	COLUMN_DESCRIPTION,
	COLUMN_INDEX,
	COLUMN_TYPE,
	COLUMN_LOCAL_FILENAME,
	COLUMN_LAST
};

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
	cmsHANDLE ccmx = NULL;
	const gchar *description;
	const gchar *sheet_type;
	const gchar *type_tmp;
	gboolean ret;
	gchar *ccmx_data = NULL;
	gsize ccmx_size;
	GtkListStore *list_store;
	GtkTreeIter iter;
	guint8 types = 0;

	/* load file */
	g_debug ("opening %s", filename);
	ret = g_file_get_contents (filename,
				   &ccmx_data,
				   &ccmx_size,
				   error);
	if (!ret)
		goto out;
	ccmx = cmsIT8LoadFromMem (NULL, ccmx_data, ccmx_size);
	if (ccmx == NULL) {
		ret = FALSE;
		g_set_error (error, 1, 0, "Cannot open %s", filename);
		goto out;
	}

	/* select correct sheet */
	sheet_type = cmsIT8GetSheetType (ccmx);
	if (g_strcmp0 (sheet_type, "CCMX   ") != 0) {
		ret = FALSE;
		g_set_error (error, 1, 0, "%s is not a CCMX file [%s]",
			     filename, sheet_type);
		goto out;
	}

	/* get the description from the ccmx file */
	description = cmsIT8GetProperty (ccmx, "DISPLAY");
	if (description == NULL) {
		ret = FALSE;
		g_set_error_literal (error, 1, 0,
				     "CCMX file does not have DISPLAY");
		goto out;
	}

	/* does already exist? */
	if (g_hash_table_lookup (priv->hash, description) != NULL) {
		ret = TRUE;
		g_debug ("CCMX '%s' already exists", description);
		goto out;
	}

	/* get the types */
	type_tmp = cmsIT8GetProperty (ccmx, "TYPE_LCD");
	if (g_strcmp0 (type_tmp, "YES") == 0)
		types += CH_CALIBRATION_TYPE_LCD;
	type_tmp = cmsIT8GetProperty (ccmx, "TYPE_LED");
	if (g_strcmp0 (type_tmp, "YES") == 0)
		types += CH_CALIBRATION_TYPE_LED;
	type_tmp = cmsIT8GetProperty (ccmx, "TYPE_CRT");
	if (g_strcmp0 (type_tmp, "YES") == 0)
		types += CH_CALIBRATION_TYPE_CRT;
	type_tmp = cmsIT8GetProperty (ccmx, "TYPE_PROJECTOR");
	if (g_strcmp0 (type_tmp, "YES") == 0)
		types += CH_CALIBRATION_TYPE_PROJECTOR;

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
	if (ccmx != NULL)
		cmsIT8Free (ccmx);
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
out:
	g_free (location);
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
	uri = g_strdup_printf ("%scalibration-%06i.ccmx",
			       COLORHUG_ARCHIVE_LOCATION,
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


{

	/* setup UI */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "box_progress"));
	gtk_widget_hide (widget);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_import"));
	gtk_widget_show (widget);

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
	if (priv->needs_repair)
		ch_ccmx_device_needs_repair (priv);
	else if (priv->force_repair) {
		ch_ccmx_device_force_repair (priv);
		/* Force repair only once */
		priv->force_repair = FALSE;
	}
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
	CdMat3x3 calibration;
	cmsHANDLE ccmx = NULL;
	const gchar *description;
	const gchar *sheet_type;
	const gchar *type_tmp;
	gboolean ret = TRUE;
	gboolean type_factory = FALSE;
	guint8 types = 0;

	/* load from a blob, as lcms sucks at reading files */
	ccmx = cmsIT8LoadFromMem (NULL, (void *) ccmx_data, ccmx_size);
	if (ccmx == NULL) {
		ret = FALSE;
		g_set_error_literal (error, 1, 0, "Cannot open CCMX");
		goto out;
	}

	/* select correct sheet */
	sheet_type = cmsIT8GetSheetType (ccmx);
	if (g_strcmp0 (sheet_type, "CCMX   ") != 0) {
		ret = FALSE;
		g_set_error (error, 1, 0, "file is not a CCMX file [%s]",
			     sheet_type);
		goto out;
	}

	/* get the description from the ccmx file */
	description = cmsIT8GetProperty (ccmx, "DISPLAY");
	if (description == NULL) {
		ret = FALSE;
		g_set_error_literal (error, 1, 0,
				     "CCMX file does not have DISPLAY");
		goto out;
	}

	/* get the types */
	type_tmp = cmsIT8GetProperty (ccmx, "TYPE_FACTORY");
	if (g_strcmp0 (type_tmp, "YES") == 0)
		type_factory = TRUE;
	type_tmp = cmsIT8GetProperty (ccmx, "TYPE_LCD");
	if (type_factory || g_strcmp0 (type_tmp, "YES") == 0)
		types += CH_CALIBRATION_TYPE_LCD;
	type_tmp = cmsIT8GetProperty (ccmx, "TYPE_LED");
	if (type_factory || g_strcmp0 (type_tmp, "YES") == 0)
		types += CH_CALIBRATION_TYPE_LED;
	type_tmp = cmsIT8GetProperty (ccmx, "TYPE_CRT");
	if (type_factory || g_strcmp0 (type_tmp, "YES") == 0)
		types += CH_CALIBRATION_TYPE_CRT;
	type_tmp = cmsIT8GetProperty (ccmx, "TYPE_PROJECTOR");
	if (type_factory || g_strcmp0 (type_tmp, "YES") == 0)
		types += CH_CALIBRATION_TYPE_PROJECTOR;

	/* read the calibration values */
	calibration.m00 = cmsIT8GetDataRowColDbl(ccmx, 0, 0);
	calibration.m01 = cmsIT8GetDataRowColDbl(ccmx, 0, 1);
	calibration.m02 = cmsIT8GetDataRowColDbl(ccmx, 0, 2);
	calibration.m10 = cmsIT8GetDataRowColDbl(ccmx, 1, 0);
	calibration.m11 = cmsIT8GetDataRowColDbl(ccmx, 1, 1);
	calibration.m12 = cmsIT8GetDataRowColDbl(ccmx, 1, 2);
	calibration.m20 = cmsIT8GetDataRowColDbl(ccmx, 2, 0);
	calibration.m21 = cmsIT8GetDataRowColDbl(ccmx, 2, 1);
	calibration.m22 = cmsIT8GetDataRowColDbl(ccmx, 2, 2);

	/* set to HW */
	ch_device_queue_set_calibration (priv->device_queue,
					 priv->device,
					 cal_idx,
					 &calibration,
					 types,
					 description);
	ch_device_queue_process_async (priv->device_queue,
				       CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
				       NULL,
				       ch_ccmx_set_calibration_cb,
				       priv);
	if (!ret)
		goto out;
out:
	if (ccmx != NULL)
		cmsIT8Free (ccmx);
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
	title = _("Getting calibration from device...");
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
			uri_tmp = g_build_filename (COLORHUG_CCMX_LOCATION,
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
	g_free (location);
	g_strfreev (lines);
	return;
}

/**
 * ch_ccmx_refresh_button_cb:
 **/
static void
ch_ccmx_refresh_button_cb (GtkWidget *widget, ChCcmxPrivate *priv)
{
	const gchar *title;
	gchar *uri = NULL;
	SoupMessage *msg = NULL;
	SoupURI *base_uri = NULL;

	/* setup UI */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_msg"));
	/* TRANSLATORS: get the list of firmwares from the internet */
	title = _("Getting latest data from the web...");
	gtk_label_set_label (GTK_LABEL (widget), title);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "box_progress"));
	gtk_widget_show_all (widget);

	/* get the latest INDEX file */
	uri = g_build_filename (COLORHUG_CCMX_LOCATION,
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
 * ch_ccmx_startup_cb:
 **/
static void
ch_ccmx_startup_cb (GApplication *application, ChCcmxPrivate *priv)
{
	const gchar *title;
	GError *error = NULL;
	gint retval;
	GtkWidget *main_window;
	GtkWidget *widget;
	GdkPixbuf *pixbuf;

	/* get UI */
	priv->builder = gtk_builder_new ();
	retval = gtk_builder_add_from_file (priv->builder,
					    CH_DATA "/ch-ccmx.ui",
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

	main_window = GTK_WIDGET (gtk_builder_get_object (priv->builder, "dialog_ccmx"));
	gtk_application_add_window (priv->application, GTK_WINDOW (main_window));
	gtk_widget_set_size_request (main_window, 400, 100);

	/* Hide window first so that the dialogue resizes itself without redrawing */
	gtk_widget_hide (main_window);

	/* buttons */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_close"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (ch_ccmx_close_button_cb), priv);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_import"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (ch_ccmx_import_button_cb), priv);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_refresh"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (ch_ccmx_refresh_button_cb), priv);

	/* setup logo image */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "image_logo"));
	gtk_image_set_from_icon_name (GTK_IMAGE (widget),
				      "colorhug-gray",
				      GTK_ICON_SIZE_DIALOG);

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
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_refresh"));
	gtk_widget_hide (widget);

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

	/* emulate a device */
	if (g_getenv ("COLORHUG_EMULATE") != NULL) {
		priv->device = ch_ccmx_get_fake_device (priv);
		ch_ccmx_got_device (priv);
	}

	/* show main UI */
	gtk_widget_show (main_window);
out:
	return;
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
	if (g_usb_device_get_vid (device) == CH_USB_VID &&
	    g_usb_device_get_pid (device) == CH_USB_PID) {
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
	if (g_usb_device_get_vid (device) == CH_USB_VID &&
	    g_usb_device_get_pid (device) == CH_USB_PID) {
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
	priv->needs_repair = TRUE;
	priv->force_repair = force_repair;
	priv->usb_ctx = g_usb_context_new (NULL);
	priv->hash = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	priv->device_list = g_usb_device_list_new (priv->usb_ctx);
	priv->device_queue = ch_device_queue_new ();
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
	g_free (priv);
	return status;
}
