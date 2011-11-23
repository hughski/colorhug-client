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
#include <gusb.h>
#include <libsoup/soup.h>
#include <stdlib.h>

#include "ch-common.h"

typedef struct {
	gchar		*filename;
	GString		*update_details;
	GtkApplication	*application;
	GtkBuilder	*builder;
	guint16		 firmware_version[3];
	guint8		*firmware_data;
	gsize		 firmware_len;
	GUsbContext	*usb_ctx;
	GUsbDevice	*device;
	GUsbDeviceList	*device_list;
	SoupSession	*session;
} ChFlashPrivate;

/**
 * ch_flash_error_dialog:
 **/
static void
ch_flash_error_dialog (ChFlashPrivate *priv, const gchar *title, const gchar *message)
{
	GtkWindow *window;
	GtkWidget *dialog;

	window = GTK_WINDOW(gtk_builder_get_object (priv->builder, "dialog_flash"));
	dialog = gtk_message_dialog_new (window, GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE, "%s", title);
	gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog), "%s", message);
	gtk_dialog_run (GTK_DIALOG (dialog));
	gtk_widget_destroy (dialog);
}

/**
 * ch_flash_activate_cb:
 **/
static void
ch_flash_activate_cb (GApplication *application, ChFlashPrivate *priv)
{
	GtkWindow *window;
	window = GTK_WINDOW (gtk_builder_get_object (priv->builder, "dialog_flash"));
	gtk_window_present (window);
}

/**
 * ch_flash_close_button_cb:
 **/
static void
ch_flash_close_button_cb (GtkWidget *widget, ChFlashPrivate *priv)
{
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "dialog_flash"));
	gtk_widget_destroy (widget);
}

/**
 * ch_flash_write_firmware:
 **/
static void
ch_flash_write_firmware (ChFlashPrivate *priv)
{
	ch_flash_error_dialog (priv,
			       _("Not supported"),
			       "This UI is work in progress");
}

/**
 * ch_flash_got_firmware_cb:
 **/
static void
ch_flash_got_firmware_cb (SoupSession *session,
			  SoupMessage *msg,
			  gpointer user_data)
{
	ChFlashPrivate *priv = (ChFlashPrivate *) user_data;

	/* we failed */
	if (!SOUP_STATUS_IS_SUCCESSFUL (msg->status_code)) {
		ch_flash_error_dialog (priv,
				       _("Failed to get firmware"),
				       soup_status_get_phrase (msg->status_code));
		goto out;
	}

	/* empty file */
	if (msg->response_body->length == 0) {
		ch_flash_error_dialog (priv,
				       _("Firmware has zero size"),
				       soup_status_get_phrase (msg->status_code));
		goto out;
	}

	/* success */
	priv->firmware_data = g_new0 (guint8, msg->response_body->length);
	priv->firmware_len = msg->response_body->length;
	memcpy (priv->firmware_data,
		msg->response_body->data,
		priv->firmware_len);
	ch_flash_write_firmware (priv);
out:
	return;
}

/**
 * ch_flash_firmware_got_chunk_cb:
 **/
static void
ch_flash_firmware_got_chunk_cb (SoupMessage *msg,
				SoupBuffer *chunk,
				ChFlashPrivate *priv)
{
	gfloat fraction;
	goffset body_length;
	goffset header_size;
	GtkWidget *widget;

	/* cancelled? */
#if 0
	if (g_cancellable_is_cancelled (priv->cancellable)) {
		g_debug ("cancelling download on %p", cancellable);
		soup_session_cancel_message (priv->session,
					     msg,
					     SOUP_STATUS_CANCELLED);
		goto out;
	}
#endif

	/* if it's returning "Found" or an error, ignore the percentage */
	if (msg->status_code != SOUP_STATUS_OK) {
		g_debug ("ignoring status code %i (%s)",
			 msg->status_code, msg->reason_phrase);
		goto out;
	}

	/* get data */
	body_length = msg->response_body->length;
	header_size = soup_message_headers_get_content_length (msg->response_headers);

	/* size is not known */
	if (header_size < body_length)
		goto out;

	/* update UI */
	fraction = (gfloat) body_length / (gfloat) header_size;
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "progressbar_status"));
	gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (widget), fraction);
out:
	return;
}

/**
 * ch_flash_flash_button_cb:
 **/
static void
ch_flash_flash_button_cb (GtkWidget *widget, ChFlashPrivate *priv)
{
	SoupURI *base_uri = NULL;
	SoupMessage *msg = NULL;
	gchar *uri = NULL;

	/* update UI */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_details"));
	gtk_widget_hide (widget);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "box_msg"));
	gtk_widget_hide (widget);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "box_detected"));
	gtk_widget_hide (widget);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "box_status"));
	gtk_widget_show (widget);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "box_warning"));
	gtk_widget_show (widget);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_status"));
	gtk_label_set_label (GTK_LABEL (widget), _("Downloading update..."));

	/* set progressbar to zero */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "progressbar_status"));
	gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (widget), 0.0f);

	/* download file */
	uri = g_strdup_printf ("http://people.freedesktop.org/~hughsient/temp/%s",
			       priv->filename);
	g_debug ("Downloading %s", uri);
	base_uri = soup_uri_new (uri);

	/* GET file */
	msg = soup_message_new_from_uri (SOUP_METHOD_GET, base_uri);
	if (msg == NULL) {
		ch_flash_error_dialog (priv,
				       _("Failed to setup message"),
				       NULL);
		goto out;
	}

	/* we want progress updates */
	g_signal_connect (msg, "got-chunk",
			  G_CALLBACK (ch_flash_firmware_got_chunk_cb),
			  priv);

	/* send sync */
	soup_session_queue_message (priv->session, msg,
				    ch_flash_got_firmware_cb, priv);
out:
	g_free (uri);
}

static gboolean
ch_flash_version_is_newer (ChFlashPrivate *priv, const gchar *version)
{
	gboolean ret = FALSE;
	gchar **split;
	guint16 tmp[3];
	guint i;

	/* split up the version string */
	split = g_strsplit (version, ".", -1);
	if (g_strv_length (split) != 3)
		goto out;
	for (i = 0; i < 3; i++)
		tmp[i] = atoi (split[i]);

	for (i = 0; i < 3; i++) {
		if (tmp[i] > priv->firmware_version[i]) {
			ret = TRUE;
			goto out;
		}
		if (tmp[i] == priv->firmware_version[i])
			goto out;
	}
out:
	g_debug ("%i.%i.%i compared to %i.%i.%i = %s",
		 tmp[0], tmp[1], tmp[2],
		 priv->firmware_version[0],
		 priv->firmware_version[1],
		 priv->firmware_version[2],
		 ret ? "newer" : "older");
	g_strfreev (split);
	return ret;
}

/**
 * ch_flash_no_updates:
 **/
static void
ch_flash_no_updates (ChFlashPrivate *priv)
{
	GtkWidget *widget;
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "spinner_progress"));
	gtk_widget_hide (widget);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_msg"));
	gtk_label_set_label (GTK_LABEL (widget), _("There are no updates available."));
}

/**
 * ch_flash_has_updates:
 **/
static void
ch_flash_has_updates (ChFlashPrivate *priv)
{
	GtkWidget *widget;
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "spinner_progress"));
	gtk_widget_hide (widget);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_details"));
	gtk_widget_show (widget);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_flash"));
	gtk_widget_show (widget);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_msg"));
	gtk_label_set_label (GTK_LABEL (widget), _("A firmware update is available."));
}

/**
 * ch_flash_got_manifest_cb:
 **/
static void
ch_flash_got_manifest_cb (SoupSession *session,
			  SoupMessage *msg,
			  gpointer user_data)
{
	ChFlashPrivate *priv = (ChFlashPrivate *) user_data;
	gchar **lines = NULL;
	gchar **tmp;
	guint i;

	/* we failed */
	if (!SOUP_STATUS_IS_SUCCESSFUL (msg->status_code)) {
		ch_flash_error_dialog (priv,
				       _("Failed to get manifest"),
				       soup_status_get_phrase (msg->status_code));
		goto out;
	}

	/* empty file */
	if (msg->response_body->length == 0) {
		ch_flash_error_dialog (priv,
				       _("Manifest has zero size"),
				       soup_status_get_phrase (msg->status_code));
		goto out;
	}

	/* write file */
	lines = g_strsplit (msg->response_body->data, "\n", -1);
	for (i = 0; lines[i] != NULL; i++) {
		if (lines[i][0] == '\0')
			continue;
		if (lines[i][0] == '\t') {
			g_string_append_printf (priv->update_details,
						"• %s\n", lines[i] + 1);
			continue;
		}

		/* split up version, date, filename */
		tmp = g_strsplit (lines[i], "\t", -1);
		if (g_strv_length (tmp) != 3)
			continue;
		if (!ch_flash_version_is_newer (priv, tmp[0])) {
			g_strfreev (tmp);
			break;
		}
		if (i == 0)
			priv->filename = g_strdup (tmp[2]);
		g_strfreev (tmp);
	}

	/* no updates */
	if (priv->update_details->len == 0) {
		ch_flash_no_updates (priv);
		goto out;
	}

	/* remove trailing space */
	g_string_set_size (priv->update_details,
			   priv->update_details->len - 1);

	/* setup UI */
	ch_flash_has_updates (priv);
out:
	g_strfreev (lines);
	return;
}

/**
 * ch_flash_get_firmware_version_cb:
 **/
static void
ch_flash_get_firmware_version_cb (GObject *source,
				  GAsyncResult *res,
				  gpointer user_data)
{
	ChFlashPrivate *priv = (ChFlashPrivate *) user_data;
	gboolean ret;
	gchar *str;
	GError *error = NULL;
	GtkWidget *widget;
	GUsbDevice *device = G_USB_DEVICE (source);
	SoupURI *base_uri = NULL;
	SoupMessage *msg = NULL;

	/* get data */
	ret = ch_device_write_command_finish (device, res, &error);
	if (!ret) {
		ch_flash_error_dialog (priv,
				       _("Failed to contact ColorHug"),
				       error->message);
		g_error_free (error);
		goto out;
	}

	/* update label */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_firmware"));
	str = g_strdup_printf (_("Firmware version %i.%i.%i"),
			       priv->firmware_version[0],
			       priv->firmware_version[1],
			       priv->firmware_version[2]);
	gtk_label_set_label (GTK_LABEL (widget), str);
	g_free (str);

	/* setup UI */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_msg"));
	gtk_label_set_label (GTK_LABEL (widget), _("Checking for updates..."));
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "spinner_progress"));
	gtk_widget_show (widget);

	/* get the latest manifest file */
	base_uri = soup_uri_new ("http://people.freedesktop.org/~hughsient/temp/MANIFEST");

	/* GET file */
	msg = soup_message_new_from_uri (SOUP_METHOD_GET, base_uri);
	if (msg == NULL) {
		ch_flash_error_dialog (priv,
				       _("Failed to setup message"),
				       NULL);
		goto out;
	}

	/* send sync */
	soup_session_queue_message (priv->session, msg,
				    ch_flash_got_manifest_cb, priv);
out:
	return;
}

/**
 * ch_flash_got_device:
 **/
static void
ch_flash_got_device (ChFlashPrivate *priv)
{
	gboolean ret;
	GError *error = NULL;
	GtkWidget *widget;

	/* open device */
	ret = g_usb_device_open (priv->device, &error);
	if (!ret) {
		ch_flash_error_dialog (priv,
				       _("Failed to open device"),
				       error->message);
		g_error_free (error);
		return;
	}
	ret = g_usb_device_set_configuration (priv->device,
					      CH_USB_CONFIG,
					      &error);
	if (!ret) {
		ch_flash_error_dialog (priv,
				       _("Failed to set configuration"),
				       error->message);
		g_error_free (error);
		return;
	}
	ret = g_usb_device_claim_interface (priv->device,
					    CH_USB_INTERFACE,
					    G_USB_DEVICE_CLAIM_INTERFACE_BIND_KERNEL_DRIVER,
					    &error);
	if (!ret) {
		ch_flash_error_dialog (priv,
				       _("Failed to claim interface"),
				       error->message);
		g_error_free (error);
		return;
	}

	/* update the UI */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "image_usb"));
	gtk_widget_hide (widget);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "box_detected"));
	gtk_widget_show (widget);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_msg"));
	gtk_label_set_label (GTK_LABEL (widget), _("Getting firmware version..."));

	/* get the firmware version */
	ch_device_write_command_async (priv->device,
				       CH_CMD_GET_FIRMWARE_VERSION,
				       NULL, /* buffer_in */
				       0, /* buffer_in_len */
				       (guint8 *) priv->firmware_version,
				       6,
				       NULL, /* cancellable */
				       ch_flash_get_firmware_version_cb,
				       priv);
}

/**
 * ch_flash_activate_link_cb:
 **/
static gboolean
ch_flash_activate_link_cb (GtkLabel *label,
			   const gchar *uri,
			   ChFlashPrivate *priv)
{
	GtkWindow *window;
	GtkWidget *dialog;

	window = GTK_WINDOW(gtk_builder_get_object (priv->builder, "dialog_flash"));
	dialog = gtk_message_dialog_new (window,
					 GTK_DIALOG_MODAL,
					 GTK_MESSAGE_INFO,
					 GTK_BUTTONS_CLOSE, "%s",
					 _("Update details"));
	gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog), "%s",
						  priv->update_details->str);
	gtk_dialog_run (GTK_DIALOG (dialog));
	gtk_widget_destroy (dialog);
	return TRUE;
}

/**
 * ch_flash_startup_cb:
 **/
static void
ch_flash_startup_cb (GApplication *application, ChFlashPrivate *priv)
{
	GError *error = NULL;
	gint retval;
	GtkWidget *main_window;
	GtkWidget *widget;
	GdkPixbuf *pixbuf;

	/* get UI */
	priv->builder = gtk_builder_new ();
	retval = gtk_builder_add_from_file (priv->builder,
					    CH_DATA "/ch-flash.ui",
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

	main_window = GTK_WIDGET (gtk_builder_get_object (priv->builder, "dialog_flash"));
	gtk_application_add_window (priv->application, GTK_WINDOW (main_window));
	gtk_widget_set_size_request (main_window, 400, 100);

	/* Hide window first so that the dialogue resizes itself without redrawing */
	gtk_widget_hide (main_window);

	/* buttons */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_close"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (ch_flash_close_button_cb), priv);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_flash"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (ch_flash_flash_button_cb), priv);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_details"));
	g_signal_connect (widget, "activate-link",
			  G_CALLBACK (ch_flash_activate_link_cb), priv);

	/* setup logo image */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "image_logo"));
	gtk_image_set_from_icon_name (GTK_IMAGE (widget),
				      "colorhug-gray",
				      GTK_ICON_SIZE_DIALOG);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "image_usb"));

	/* setup USB image */
	pixbuf = gdk_pixbuf_new_from_file_at_scale (CH_DATA
						    G_DIR_SEPARATOR_S "icons"
						    G_DIR_SEPARATOR_S "usb.svg",
						    -1, 48, TRUE, &error);
	g_assert (pixbuf != NULL);
	gtk_image_set_from_pixbuf (GTK_IMAGE (widget), pixbuf);
	g_object_unref (pixbuf);

	/* hide all unused widgets until we've connected with the device */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_flash"));
	gtk_widget_hide (widget);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "box_detected"));
	gtk_widget_hide (widget);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "box_warning"));
	gtk_widget_hide (widget);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_details"));
	gtk_widget_hide (widget);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "box_status"));
	gtk_widget_hide (widget);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "spinner_progress"));
	gtk_widget_hide (widget);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_msg"));
	gtk_label_set_label (GTK_LABEL (widget), _("Please connect your ColorHug"));

	/* is the colorhug already plugged in? */
	g_usb_device_list_coldplug (priv->device_list);

	/* setup the session */
	priv->session = soup_session_sync_new_with_options (SOUP_SESSION_USER_AGENT, "colorhug-flash",
							    SOUP_SESSION_TIMEOUT, 5000,
							    NULL);
	if (priv->session == NULL) {
		ch_flash_error_dialog (priv,
				      _("Failed to setup networking"),
				      NULL);
		goto out;
	}

	/* show main UI */
	gtk_widget_show (main_window);
out:
	return;
}

/**
 * ch_flash_device_added_cb:
 **/
static void
ch_flash_device_added_cb (GUsbDeviceList *list,
			  GUsbDevice *device,
			  ChFlashPrivate *priv)
{
	g_debug ("Added: %i:%i",
		 g_usb_device_get_vid (device),
		 g_usb_device_get_pid (device));
	if (g_usb_device_get_vid (device) == CH_USB_VID &&
	    g_usb_device_get_pid (device) == CH_USB_PID) {
		priv->device = g_object_ref (device);
		ch_flash_got_device (priv);
	}
}

/**
 * ch_flash_device_removed_cb:
 **/
static void
ch_flash_device_removed_cb (GUsbDeviceList *list,
			    GUsbDevice *device,
			    ChFlashPrivate *priv)
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
 * main:
 **/
int
main (int argc, char **argv)
{
	ChFlashPrivate *priv;
	GOptionContext *context;
	int status = 0;
	gboolean ret;
	GError *error = NULL;

	setlocale (LC_ALL, "");

	bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	gtk_init (&argc, &argv);

	context = g_option_context_new ("ColorHug Flash program");
	g_option_context_add_group (context, gtk_get_option_group (TRUE));
	ret = g_option_context_parse (context, &argc, &argv, &error);
	if (!ret) {
		g_warning ("failed to parse options: %s", error->message);
		g_error_free (error);
	}
	g_option_context_free (context);

	priv = g_new0 (ChFlashPrivate, 1);
	priv->update_details = g_string_new ("");
	priv->usb_ctx = g_usb_context_new (NULL);
	priv->device_list = g_usb_device_list_new (priv->usb_ctx);
	g_signal_connect (priv->device_list, "device-added",
			  G_CALLBACK (ch_flash_device_added_cb), priv);
	g_signal_connect (priv->device_list, "device-removed",
			  G_CALLBACK (ch_flash_device_removed_cb), priv);

	/* ensure single instance */
	priv->application = gtk_application_new ("com.hughski.ColorHug.Flash", 0);
	g_signal_connect (priv->application, "startup",
			  G_CALLBACK (ch_flash_startup_cb), priv);
	g_signal_connect (priv->application, "activate",
			  G_CALLBACK (ch_flash_activate_cb), priv);

	/* wait */
	status = g_application_run (G_APPLICATION (priv->application), argc, argv);

	g_object_unref (priv->application);
	if (priv->update_details != NULL)
		g_string_free (priv->update_details, TRUE);
	if (priv->device_list != NULL)
		g_object_unref (priv->device_list);
	if (priv->usb_ctx != NULL)
		g_object_unref (priv->usb_ctx);
	if (priv->builder != NULL)
		g_object_unref (priv->builder);
	if (priv->session != NULL)
		g_object_unref (priv->session);
	g_free (priv->filename);
	g_free (priv->firmware_data);
	g_free (priv);
	return status;
}
