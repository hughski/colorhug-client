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
#include <colorhug.h>
#include <canberra-gtk.h>

#include "ch-markdown.h"
#include "ch-flash-md.h"

typedef struct {
	gchar		*filename;
	gchar		*checksum;
	GString		*update_details;
	GString		*warning_details;
	GtkApplication	*application;
	GtkBuilder	*builder;
	guint16		 firmware_version[3];
	guint8		 hardware_version;
	guint8		*firmware_data;
	guint32		 serial_number;
	gsize		 firmware_len;
	gboolean	 planned_replug;
	GUsbContext	*usb_ctx;
	GUsbDevice	*device;
	GUsbDeviceList	*device_list;
	SoupSession	*session;
	ChMarkdown	*markdown;
	ChDeviceQueue	*device_queue;
	GSettings	*settings;
} ChFlashPrivate;

/**
 * ch_flash_error_dialog:
 **/
static void
ch_flash_error_dialog (ChFlashPrivate *priv, const gchar *title, const gchar *message)
{
	GtkWindow *window;
	GtkWidget *widget;
	GtkWidget *dialog;

	/* play sound */
	ca_context_play (ca_gtk_context_get (), 0,
			 CA_PROP_EVENT_ID, "dialog-warning",
			 CA_PROP_APPLICATION_NAME, _("ColorHug Updater"),
			 CA_PROP_EVENT_DESCRIPTION, _("Calibration Failed"), NULL);

	window = GTK_WINDOW(gtk_builder_get_object (priv->builder, "dialog_flash"));
	dialog = gtk_message_dialog_new (window,
					 GTK_DIALOG_MODAL,
					 GTK_MESSAGE_ERROR,
					 GTK_BUTTONS_CLOSE,
					 "%s", title);
	if (message != NULL) {
		gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
							  "%s", message);
	}
	gtk_dialog_run (GTK_DIALOG (dialog));
	gtk_widget_destroy (dialog);

	/* close main window */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "dialog_flash"));
	gtk_widget_destroy (widget);
}

/**
 * ch_flash_error_do_not_panic:
 **/
static void
ch_flash_error_do_not_panic (ChFlashPrivate *priv)
{
	const gchar *title;
	GString *msg = NULL;
	GtkWidget *widget;

	/* create empty string for long message */
	msg = g_string_new ("");

	/* TRANSLATORS: we broke the device */
	title = _("Flashing the device failed but the ColorHug is not damaged.");
	g_string_append_printf (msg, "<b>%s</b>\n", title);

	/* TRANSLATORS: hopefully we can "fix" this automatically */
	title = _("Remove the ColorHug device from the computer, reinsert it and re-run this program.");
	g_string_append_printf (msg, "%s", title);

	/* update UI */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_flash"));
	gtk_widget_hide (widget);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_close"));
	gtk_widget_set_sensitive (widget, TRUE);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "box_detected"));
	gtk_widget_hide (widget);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "box_warning"));
	gtk_widget_hide (widget);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "image_usb"));
	gtk_widget_show (widget);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "box_msg"));
	gtk_widget_show (widget);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "spinner_progress"));
	gtk_widget_hide (widget);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_details"));
	gtk_widget_hide (widget);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "box_status"));
	gtk_widget_hide (widget);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_msg"));
	gtk_label_set_markup (GTK_LABEL (widget), msg->str);
	g_string_free (msg, TRUE);
}

/**
 * ch_flash_error_no_network:
 **/
static void
ch_flash_error_no_network (ChFlashPrivate *priv)
{
	const gchar *title;
	GtkWidget *widget;

	/* update UI */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_flash"));
	gtk_widget_hide (widget);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_close"));
	gtk_widget_set_sensitive (widget, TRUE);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "box_detected"));
	gtk_widget_show (widget);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "box_warning"));
	gtk_widget_hide (widget);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "box_msg"));
	gtk_widget_show (widget);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "spinner_progress"));
	gtk_widget_hide (widget);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_details"));
	gtk_widget_hide (widget);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "box_status"));
	gtk_widget_hide (widget);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_msg"));
	/* TRANSLATORS: the user has to be online to get firmware */
	title = _("Connect to the internet to check for updates");
	gtk_label_set_markup (GTK_LABEL (widget), title);
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
 * ch_flash_set_flash_success_1_cb:
 **/
static void
ch_flash_set_flash_success_1_cb (GObject *source,
				 GAsyncResult *res,
				 gpointer user_data)
{
	const gchar *title;
	ChFlashPrivate *priv = (ChFlashPrivate *) user_data;
	gboolean ret;
	GError *error = NULL;
	GtkWidget *widget;
	ChDeviceQueue *device_queue = CH_DEVICE_QUEUE (source);

	/* get data */
	ret = ch_device_queue_process_finish (device_queue, res, &error);
	if (!ret) {
		ch_flash_error_do_not_panic (priv);
		/* TRANSLATORS: we can only set the flash success
		 * flag when the *new* firmware is running */
		title = _("Failed to set the flash success true");
		ch_flash_error_dialog (priv, title, error->message);
		g_error_free (error);
		goto out;
	}

	/* setup UI */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_msg"));
	/* TRANSLATORS: we've uploaded new firmware */
	title = _("Device successfully updated");
	gtk_label_set_label (GTK_LABEL (widget), title);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_close"));
	gtk_widget_set_sensitive (widget, TRUE);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_flash"));
	gtk_widget_hide (widget);

	/* play sound */
	ca_context_play (ca_gtk_context_get (), 0,
			 CA_PROP_EVENT_ID, "complete",
			 CA_PROP_APPLICATION_NAME, _("ColorHug Updater"),
			 CA_PROP_EVENT_DESCRIPTION, _("Calibration Completed"), NULL);
out:
	return;
}

/**
 * ch_flash_set_flash_success_1:
 **/
static void
ch_flash_set_flash_success_1 (ChFlashPrivate *priv)
{
	const gchar *title;
	GtkWidget *widget;

	/* update UI */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_status"));
	/* TRANSLATORS: tell the hardware we succeeded */
	title = _("Setting flash success…");
	gtk_label_set_label (GTK_LABEL (widget), title);

	/* need to boot into bootloader */
	ch_device_queue_set_flash_success (priv->device_queue,
					   priv->device,
					   0x01);
	ch_device_queue_process_async (priv->device_queue,
				       CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
				       NULL,
				       ch_flash_set_flash_success_1_cb,
				       priv);
}

/**
 * ch_flash_boot_flash_delay_cb:
 **/
static gboolean
ch_flash_boot_flash_delay_cb (gpointer user_data)
{
	const gchar *title;
	ChFlashPrivate *priv = (ChFlashPrivate *) user_data;

	/* we failed to come back up */
	if (priv->device == NULL) {
		ch_flash_error_do_not_panic (priv);
		/* TRANSLATORS: the device failed to boot up with the
		 * new firmware */
		title = _("Failed to startup the ColorHug");
		ch_flash_error_dialog (priv, title, NULL);
		goto out;
	}

	/* now we can write to flash */
	ch_flash_set_flash_success_1 (priv);
out:
	return FALSE;
}

/**
 * ch_flash_boot_flash_cb:
 **/
static void
ch_flash_boot_flash_cb (GObject *source,
			GAsyncResult *res,
			gpointer user_data)
{
	const gchar *title;
	ChFlashPrivate *priv = (ChFlashPrivate *) user_data;
	gboolean ret;
	GError *error = NULL;
	ChDeviceQueue *device_queue = CH_DEVICE_QUEUE (source);

	/* get data */
	ret = ch_device_queue_process_finish (device_queue, res, &error);
	if (!ret) {
		ch_flash_error_do_not_panic (priv);
		/* TRANSLATORS: the new firmware will not load */
		title = _("Failed to boot the ColorHug");
		ch_flash_error_dialog (priv, title, error->message);
		g_error_free (error);
		goto out;
	}

	/* wait one second */
	g_timeout_add (CH_FLASH_RECONNECT_TIMEOUT,
		       ch_flash_boot_flash_delay_cb, priv);
out:
	return;
}

/**
 * ch_flash_verify_firmware_cb:
 **/
static void
ch_flash_verify_firmware_cb (GObject *source,
			     GAsyncResult *res,
			     gpointer user_data)
{
	ChDeviceQueue *device_queue = CH_DEVICE_QUEUE (source);
	ChFlashPrivate *priv = (ChFlashPrivate *) user_data;
	const gchar *title;
	gboolean ret;
	GError *error = NULL;
	GtkWidget *widget;

	/* get data */
	ret = ch_device_queue_process_finish (device_queue, res, &error);
	if (!ret) {
		ch_flash_error_do_not_panic (priv);
		/* TRANSLATORS: tell the device the firmware is no
		 * longer known working */
		title = _("Failed to verify the firmware");
		ch_flash_error_dialog (priv, title, error->message);
		g_error_free (error);
		goto out;
	}

	/* update the UI */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "box_warning"));
	gtk_widget_hide (widget);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "box_status"));
	gtk_widget_hide (widget);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "spinner_progress"));
	gtk_widget_hide (widget);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_details"));
	gtk_widget_hide (widget);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_msg"));
	/* TRANSLATORS: boot into the new firmware */
	title = _("Starting the new firmware…");
	gtk_label_set_label (GTK_LABEL (widget), title);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "box_detected"));
	gtk_widget_show (widget);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "box_msg"));
	gtk_widget_show (widget);

	/* this is planned */
	priv->planned_replug = TRUE;

	/* boot into new code */
	ch_device_queue_boot_flash (priv->device_queue,
				    priv->device);
	ch_device_queue_process_async (priv->device_queue,
				       CH_DEVICE_QUEUE_PROCESS_FLAGS_NONFATAL_ERRORS,
				       NULL,
				       ch_flash_boot_flash_cb,
				       priv);
out:
	return;
}

/**
 * ch_flash_write_firmware_cb:
 **/
static void
ch_flash_write_firmware_cb (GObject *source,
			    GAsyncResult *res,
			    gpointer user_data)
{
	ChDeviceQueue *device_queue = CH_DEVICE_QUEUE (source);
	ChFlashPrivate *priv = (ChFlashPrivate *) user_data;
	const gchar *title;
	gboolean ret;
	GError *error = NULL;
	GtkWidget *widget;

	/* get data */
	ret = ch_device_queue_process_finish (device_queue, res, &error);
	if (!ret) {
		ch_flash_error_do_not_panic (priv);
		/* TRANSLATORS: tell the device the firmware is no
		 * longer known working */
		title = _("Failed to write the firmware");
		ch_flash_error_dialog (priv, title, error->message);
		g_error_free (error);
		goto out;
	}

	/* update UI */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_status"));
	/* TRANSLATORS: now we've written the firmware, we have to
	 * verify it before we tell the device it was successfull */
	title = _("Verifying new firmware…");
	gtk_label_set_label (GTK_LABEL (widget), title);

	/* verify firmware */
	ch_device_queue_verify_firmware (priv->device_queue,
					 priv->device,
					 priv->firmware_data,
					 priv->firmware_len);
	ch_device_queue_process_async (priv->device_queue,
				       CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
				       NULL,
				       ch_flash_verify_firmware_cb,
				       priv);
out:
	return;
}

/**
 * ch_flash_set_flash_success_0:
 **/
static void
ch_flash_set_flash_success_0 (ChFlashPrivate *priv)
{
	const gchar *title;
	GtkWidget *widget;

	/* update UI */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_status"));
	/* TRANSLATORS: now write the new firmware chunks */
	title = _("Writing new firmware…");
	gtk_label_set_label (GTK_LABEL (widget), title);

	/* write firmware */
	ch_device_queue_set_flash_success (priv->device_queue,
					   priv->device,
					   0x00);
	ch_device_queue_write_firmware (priv->device_queue,
					priv->device,
					priv->firmware_data,
					priv->firmware_len);
	ch_device_queue_process_async (priv->device_queue,
				       CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
				       NULL,
				       ch_flash_write_firmware_cb,
				       priv);
}

/**
 * ch_flash_reset_delay_cb:
 **/
static gboolean
ch_flash_reset_delay_cb (gpointer user_data)
{
	const gchar *title;
	ChFlashPrivate *priv = (ChFlashPrivate *) user_data;

	/* we failed to come back up */
	if (priv->device == NULL) {
		ch_flash_error_do_not_panic (priv);
		/* TRANSLATORS: the device failed to come back alive */
		title = _("Failed to reboot the ColorHug");
		ch_flash_error_dialog (priv, title, NULL);
		goto out;
	}

	/* now we can write to flash */
	ch_flash_set_flash_success_0 (priv);
out:
	return FALSE;
}

/**
 * ch_flash_reset_cb:
 **/
static void
ch_flash_reset_cb (GObject *source,
		   GAsyncResult *res,
		   gpointer user_data)
{
	const gchar *title;
	ChFlashPrivate *priv = (ChFlashPrivate *) user_data;
	gboolean ret;
	GError *error = NULL;
	ChDeviceQueue *device_queue = CH_DEVICE_QUEUE (source);

	/* get data */
	ret = ch_device_queue_process_finish (device_queue, res, &error);
	if (!ret) {
		ch_flash_error_do_not_panic (priv);
		/* TRANSLATORS: we restart the device in bootloader mode */
		title = _("Failed to reset the ColorHug");
		ch_flash_error_dialog (priv, title, error->message);
		g_error_free (error);
		goto out;
	}

	/* wait one second */
	g_timeout_add (CH_FLASH_RECONNECT_TIMEOUT,
		       ch_flash_reset_delay_cb, priv);
out:
	return;
}

/**
 * ch_flash_got_firmware_data:
 **/
static void
ch_flash_got_firmware_data (ChFlashPrivate *priv)
{
	const gchar *title;
	GtkWidget *widget;

	/* we can shortcut as we're already in bootloader mode */
	if (priv->firmware_version[0] == 0) {
		ch_flash_set_flash_success_0 (priv);
		return;
	}

	/* update UI */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_status"));
	/* TRANSLATORS: switch from firmware mode into bootloader mode */
	title = _("Resetting into bootloader…");
	gtk_label_set_label (GTK_LABEL (widget), title);

	/* this is planned */
	priv->planned_replug = TRUE;

	/* need to boot into bootloader */
	ch_device_queue_reset (priv->device_queue,
			       priv->device);
	ch_device_queue_process_async (priv->device_queue,
				       CH_DEVICE_QUEUE_PROCESS_FLAGS_NONFATAL_ERRORS,
				       NULL,
				       ch_flash_reset_cb,
				       priv);
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
	const gchar *title;
	gchar *checksum_tmp = NULL;
	gchar *message = NULL;

	/* we failed */
	if (!SOUP_STATUS_IS_SUCCESSFUL (msg->status_code)) {
		ch_flash_error_no_network (priv);
		/* TRANSLATORS: failed to get the firmware file */
		title = _("Failed to get firmware");
		ch_flash_error_dialog (priv, title, soup_status_get_phrase (msg->status_code));
		goto out;
	}

	/* empty file */
	if (msg->response_body->length == 0) {
		ch_flash_error_no_network (priv);
		/* TRANSLATORS: the server gave us an invalid file */
		title = _("Firmware has zero size");
		ch_flash_error_dialog (priv, title, soup_status_get_phrase (msg->status_code));
		goto out;
	}

	/* check checksum */
	checksum_tmp = g_compute_checksum_for_data (G_CHECKSUM_SHA1,
						    (const guchar *) msg->response_body->data,
						    msg->response_body->length);
	if (priv->checksum != NULL &&
	    g_strcmp0 (priv->checksum, checksum_tmp) != 0) {
		/* TRANSLATORS: the server gave us an invalid file */
		title = _("Firmware has incorrect checksum");
		message = g_strdup_printf ("Expected %s, got %s",
					   priv->checksum, checksum_tmp);
		ch_flash_error_dialog (priv, title, message);
		goto out;
	}

	/* success */
	priv->firmware_data = g_new0 (guint8, msg->response_body->length);
	priv->firmware_len = msg->response_body->length;
	memcpy (priv->firmware_data,
		msg->response_body->data,
		priv->firmware_len);
	ch_flash_got_firmware_data (priv);
out:
	g_free (message);
	g_free (checksum_tmp);
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
 * ch_flash_show_warning_dialog:
 **/
static gboolean
ch_flash_show_warning_dialog (ChFlashPrivate *priv)
{
	const gchar *title;
	GtkWindow *window;
	GtkWidget *dialog;
	gchar *format;
	GtkResponseType response;

	/* anything to show? */
	if (priv->warning_details->len == 0)
		return TRUE;

	/* the update text is markdown formatted */
	format = ch_markdown_parse (priv->markdown,
				    priv->warning_details->str);
	window = GTK_WINDOW(gtk_builder_get_object (priv->builder, "dialog_flash"));
	/* TRANSLATORS: the long details about all the updates that
	 * are newer than the version the user has installed */
	title = _("Warnings about this update");
	dialog = gtk_message_dialog_new (window,
					 GTK_DIALOG_MODAL,
					 GTK_MESSAGE_WARNING,
					 GTK_BUTTONS_NONE, "%s",
					 title);
	/* TRANSLATORS: this is the button text to continue the flash
	 * after showing the user a warning */
	gtk_dialog_add_button (GTK_DIALOG (dialog), _("Flash anyway"), GTK_RESPONSE_OK);
	/* TRANSLATORS: this is the button text to abort the flash process */
	gtk_dialog_add_button (GTK_DIALOG (dialog), _("Do not flash"), GTK_RESPONSE_CANCEL);
	gtk_message_dialog_format_secondary_markup (GTK_MESSAGE_DIALOG (dialog), "%s",
						    format);
	response = gtk_dialog_run (GTK_DIALOG (dialog));
	gtk_widget_destroy (dialog);
	g_free (format);
	return (response == GTK_RESPONSE_OK);
}

/**
 * ch_flash_get_device_download_kind:
 **/
static const gchar *
ch_flash_get_device_download_kind (ChFlashPrivate *priv)
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
 * ch_flash_flash_button_cb:
 **/
static void
ch_flash_flash_button_cb (GtkWidget *widget, ChFlashPrivate *priv)
{
	const gchar *title;
	SoupURI *base_uri = NULL;
	SoupMessage *msg = NULL;
	gboolean ret;
	gchar *server_uri = NULL;
	gchar *uri = NULL;

	/* show the user any warning dialog */
	ret = ch_flash_show_warning_dialog (priv);
	if (!ret)
		goto out;

	/* update UI */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_flash"));
	gtk_widget_hide (widget);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_close"));
	gtk_widget_set_sensitive (widget, FALSE);
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
	/* TRANSLATORS: downloading the firmware binary file from a
	 * remote server */
	title = _("Downloading update…");
	gtk_label_set_label (GTK_LABEL (widget), title);

	/* set progressbar to zero */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "progressbar_status"));
	gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (widget), 0.0f);

	/* download file */
	server_uri = g_settings_get_string (priv->settings, "server-uri");
	uri = g_build_filename (server_uri,
				ch_flash_get_device_download_kind (priv),
				"firmware",
				priv->filename,
				NULL);
	g_debug ("Downloading %s", uri);
	base_uri = soup_uri_new (uri);

	/* GET file */
	msg = soup_message_new_from_uri (SOUP_METHOD_GET, base_uri);
	if (msg == NULL) {
		/* TRANSLATORS: internal error when setting up HTTP request */
		title = _("Failed to setup message");
		ch_flash_error_dialog (priv, title, NULL);
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
	g_free (server_uri);
	g_free (uri);
	if (base_uri != NULL)
		soup_uri_free (base_uri);
}

/**
 * ch_flash_get_packed_version:
 **/
static guint32
ch_flash_get_packed_version (guint16 *ver)
{
	return ver[0] * 0xffff + ver[1] * 0xff + ver[2];
}

/**
 * ch_flash_version_is_newer:
 **/
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
		tmp[i] = g_ascii_strtoull (split[i], NULL, 10);

	/* check versions */
	if (ch_flash_get_packed_version (tmp) >
	    ch_flash_get_packed_version (priv->firmware_version))
		ret = TRUE;
	g_debug ("%i.%i.%i compared to %i.%i.%i = %s",
		 tmp[0], tmp[1], tmp[2],
		 priv->firmware_version[0],
		 priv->firmware_version[1],
		 priv->firmware_version[2],
		 ret ? "newer" : "older");
out:
	g_strfreev (split);
	return ret;
}

/**
 * ch_flash_no_updates:
 **/
static void
ch_flash_no_updates (ChFlashPrivate *priv)
{
	const gchar *title;
	GtkWidget *widget;
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "spinner_progress"));
	gtk_widget_hide (widget);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_msg"));
	/* TRANSLATORS: the user is already running the latest firmware */
	title = _("There are no updates available.");
	gtk_label_set_label (GTK_LABEL (widget), title);
}

/**
 * ch_flash_has_updates:
 **/
static void
ch_flash_has_updates (ChFlashPrivate *priv)
{
	const gchar *title;
	GtkWidget *widget;
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "spinner_progress"));
	gtk_widget_hide (widget);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_details"));
	gtk_widget_show (widget);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_flash"));
	gtk_widget_show (widget);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_msg"));
	/* TRANSLATORS: the user is running an old firmware version */
	title = _("A firmware update is available.");
	gtk_label_set_label (GTK_LABEL (widget), title);
}

/**
 * ch_flash_got_metadata_cb:
 **/
static void
ch_flash_got_metadata_cb (SoupSession *session,
			  SoupMessage *msg,
			  gpointer user_data)
{
	ChFlashPrivate *priv = (ChFlashPrivate *) user_data;
	ChFlashUpdate *update;
	const gchar *title;
	GError *error = NULL;
	GPtrArray *updates = NULL;
	guint i;
	gboolean enable_test_firmware;

	/* we failed */
	if (!SOUP_STATUS_IS_SUCCESSFUL (msg->status_code)) {
		ch_flash_error_no_network (priv);
		/* TRANSLATORS: the HTTP request failed */
		title = _("Failed to get the listing of firmware files");
		ch_flash_error_dialog (priv, title, soup_status_get_phrase (msg->status_code));
		goto out;
	}

	/* empty file */
	if (msg->response_body->length == 0) {
		ch_flash_error_no_network (priv);
		/* TRANSLATORS: we got an invalid response from the server */
		title = _("The firmware listing has zero size");
		ch_flash_error_dialog (priv, title, soup_status_get_phrase (msg->status_code));
		goto out;
	}

	/* this is a session configurable */
	enable_test_firmware = g_settings_get_boolean (priv->settings,
						       "enable-test-firmware");

	/* parse file */
	updates = ch_flash_md_parse_data (msg->response_body->data,
					  &error);
	if (updates == NULL) {
		/* TRANSLATORS: the XML file was corrupt */
		title = _("Failed to parse the update metadata");
		ch_flash_error_dialog (priv, title, error->message);
		g_error_free (error);
		goto out;
	}
	for (i = 0; i < updates->len; i++) {
		update = g_ptr_array_index (updates, i);

		/* this version is older than what we have now */
		if (!ch_flash_version_is_newer (priv, update->version))
			break;

		/* is the state correct? */
		if (update->state != CH_FLASH_MD_STATE_STABLE && !enable_test_firmware)
			continue;

		/* add info text */
		if (update->info->len > 0) {
			g_string_append_printf (priv->update_details,
						"%s\n", update->info->str);
		}

		/* add warning text */
		if (update->state == CH_FLASH_MD_STATE_TESTING) {
			g_string_append_printf (priv->warning_details,
						"**%s**\n%s\n",
						/* TRANSLATORS: this is test firmware */
						_("This is a test firmware not intended for general release."),
						/* TRANSLATORS: all bets are off */
						_("This firmware has not been widely tested and may not work as expected."));
		}
		if (update->warning->len > 0) {
			g_string_append_printf (priv->warning_details,
						"%s\n", update->warning->str);
		}

		/* save newest available firmware */
		if (priv->filename == NULL) {
			priv->filename = g_strdup (update->filename);
			priv->checksum = g_strdup (update->checksum);
		}
	}

	/* no updates */
	if (priv->filename == NULL) {
		ch_flash_no_updates (priv);
		goto out;
	}

	/* remove trailing space */
	if (priv->update_details->len > 1) {
		g_string_set_size (priv->update_details,
				   priv->update_details->len - 1);
	}
	if (priv->warning_details->len > 1) {
		g_string_set_size (priv->warning_details,
				   priv->warning_details->len - 1);
	}

	/* setup UI */
	ch_flash_has_updates (priv);
out:
	if (updates != NULL)
		g_ptr_array_unref (updates);
}

/**
 * ch_flash_got_device_data:
 **/
static void
ch_flash_got_device_data (ChFlashPrivate *priv)
{
	const gchar *title;
	gboolean ret;
	gchar *server_uri = NULL;
	gchar *str = NULL;
	gchar *uri = NULL;
	gchar *user_agent = NULL;
	GError *error = NULL;
	GtkWidget *widget;
	SoupMessage *msg = NULL;
	SoupURI *base_uri = NULL;

	/* set user agent */
	user_agent = g_strdup_printf ("colorhug-flash-hw%i-fw%i.%i.%i-sn%i",
				      priv->hardware_version,
				      priv->firmware_version[0],
				      priv->firmware_version[1],
				      priv->firmware_version[2],
				      priv->serial_number);
	g_object_set (priv->session, "user-agent", user_agent, NULL);

	/* update product label */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_detected"));
	switch (priv->hardware_version) {
	case 0x00:
		/* TRANSLATORS: pre-production hardware */
		str = g_strdup (_("Prototype ColorHug Detected"));
		break;
	case 0x01:
		/* TRANSLATORS: first release hardware */
		str = g_strdup (_("ColorHug Detected"));
		break;
	case 0x02:
		/* TRANSLATORS: first release hardware */
		str = g_strdup (_("ColorHug Spectro Detected"));
		break;
	case 0xff:
		/* TRANSLATORS: fake hardware */
		str = g_strdup (_("Emulated ColorHug Detected"));
		break;
	default:
		/* TRANSLATORS: new-issue hardware */
		str = g_strdup_printf (_("ColorHug v%i Detected"),
				       priv->hardware_version);
		break;
	}
	gtk_label_set_label (GTK_LABEL (widget), str);
	g_free (str);

	/* update firmware label */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_firmware"));
	if (priv->firmware_version[0] == 0) {
		/* TRANSLATORS: the device is in bootloader mode */
		title = _("Bootloader version");
		str = g_strdup_printf ("%s %i.%i.%i",
				       title,
				       priv->firmware_version[0],
				       priv->firmware_version[1],
				       priv->firmware_version[2]);
	} else {
		/* TRANSLATORS: the device is in firmware mode */
		title = _("Firmware version");
		str = g_strdup_printf ("%s %i.%i.%i",
				       title,
				       priv->firmware_version[0],
				       priv->firmware_version[1],
				       priv->firmware_version[2]);
	}
	gtk_label_set_label (GTK_LABEL (widget), str);

	/* already done flash, we're just booting into the new firmware */
	if (priv->planned_replug) {
		g_debug ("after booting into new firmware");
		return;
	}

	/* setup UI */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_msg"));
	/* TRANSLATORS: Check for newer version of the firmware compared
	 * to what is installed on the device */
	title = _("Checking for updates…");
	gtk_label_set_label (GTK_LABEL (widget), title);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "spinner_progress"));
	gtk_widget_show (widget);

	/* we've manually specified a local firmware file */
	if (priv->filename != NULL) {
		/* TRANSLATORS: we've specified a local file */
		title = _("Flashing firmware file…");
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_msg"));
		gtk_label_set_label (GTK_LABEL (widget), title);
		ret = g_file_get_contents (priv->filename,
					   (gchar **) &priv->firmware_data,
					   &priv->firmware_len,
					   &error);
		if (!ret) {
			/* TRANSLATORS: file read error when reading
			 * local firmware file */
			title = _("Failed to load file");
			ch_flash_error_dialog (priv, title, error->message);
			g_error_free (error);
			goto out;
		}
		ch_flash_got_firmware_data (priv);
		goto out;
	}

	/* get the latest manifest file */
	server_uri = g_settings_get_string (priv->settings, "server-uri");
	uri = g_build_filename (server_uri,
				ch_flash_get_device_download_kind (priv),
				"firmware",
				"metadata.xml",
				NULL);
	base_uri = soup_uri_new (uri);

	/* GET file */
	msg = soup_message_new_from_uri (SOUP_METHOD_GET, base_uri);
	if (msg == NULL) {
		/* TRANSLATORS: internal error when setting up HTTP request */
		title = _("Failed to setup message");
		ch_flash_error_dialog (priv, title, NULL);
		goto out;
	}

	/* send sync */
	soup_session_queue_message (priv->session, msg,
				    ch_flash_got_metadata_cb, priv);
out:
	/* reset the flag */
	priv->planned_replug = FALSE;
	if (base_uri != NULL)
		soup_uri_free (base_uri);
	g_free (user_agent);
	g_free (str);
	g_free (server_uri);
	g_free (uri);
}

/**
 * ch_flash_get_serial_number_cb:
 **/
static void
ch_flash_get_serial_number_cb (GObject *source,
			       GAsyncResult *res,
			       gpointer user_data)
{
	const gchar *title;
	ChFlashPrivate *priv = (ChFlashPrivate *) user_data;
	gboolean ret;
	GError *error = NULL;
	ChDeviceQueue *device_queue = CH_DEVICE_QUEUE (source);

	/* get data */
	ret = ch_device_queue_process_finish (device_queue, res, &error);
	if (!ret) {
		/* TRANSLATORS: the request failed */
		title = _("Failed to contact ColorHug");
		ch_flash_error_dialog (priv, title, error->message);
		g_error_free (error);
		goto out;
	}

	/* show device data */
	ch_flash_got_device_data (priv);
out:
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
	const gchar *title;
	ChFlashPrivate *priv = (ChFlashPrivate *) user_data;
	gboolean ret;
	GError *error = NULL;
	ChDeviceQueue *device_queue = CH_DEVICE_QUEUE (source);

	/* get data */
	ret = ch_device_queue_process_finish (device_queue, res, &error);
	if (!ret) {
		/* TRANSLATORS: the request failed */
		title = _("Failed to contact ColorHug");
		ch_flash_error_dialog (priv, title, error->message);
		g_error_free (error);
		goto out;
	}

	/* bootloader mode has no idea what the serial number is */
	if (priv->firmware_version[0] == 0) {
		ch_flash_got_device_data (priv);
		goto out;
	}

	/* get the serial number */
	ch_device_queue_get_serial_number (priv->device_queue,
					   priv->device,
					   &priv->serial_number);
	ch_device_queue_process_async (priv->device_queue,
				       CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
				       NULL,
				       ch_flash_get_serial_number_cb,
				       priv);
out:
	return;
}

/**
 * ch_flash_get_fake_device:
 **/
static GUsbDevice *
ch_flash_get_fake_device (ChFlashPrivate *priv)
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
 * ch_flash_got_device:
 **/
static void
ch_flash_got_device (ChFlashPrivate *priv)
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
		ch_flash_error_dialog (priv, title, error->message);
		g_error_free (error);
		return;
	}

fake_device:
	/* initial detection */
	if (!priv->planned_replug) {
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "image_usb"));
		gtk_widget_hide (widget);
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "box_detected"));
		gtk_widget_show (widget);
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_msg"));
		/* TRANSLATORS: request the firmware version from the device */
		title = _("Getting firmware version…");
		gtk_label_set_label (GTK_LABEL (widget), title);
	}

	/* get the hardware and firmware version */
	ch_device_queue_get_hardware_version (priv->device_queue,
					      priv->device,
					      &priv->hardware_version);
	ch_device_queue_get_firmware_ver (priv->device_queue,
					  priv->device,
					  &priv->firmware_version[0],
					  &priv->firmware_version[1],
					  &priv->firmware_version[2]);
	ch_device_queue_process_async (priv->device_queue,
				       CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
				       NULL,
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
	const gchar *title;
	GtkWindow *window;
	GtkWidget *dialog;
	gchar *format;

	/* the update text is markdown formatted */
	format = ch_markdown_parse (priv->markdown,
				    priv->update_details->str);
	window = GTK_WINDOW(gtk_builder_get_object (priv->builder, "dialog_flash"));
	/* TRANSLATORS: the long details about all the updates that
	 * are newer than the version the user has installed */
	title = _("Update details");
	dialog = gtk_message_dialog_new (window,
					 GTK_DIALOG_MODAL,
					 GTK_MESSAGE_INFO,
					 GTK_BUTTONS_CLOSE, "%s",
					 title);
	gtk_message_dialog_format_secondary_markup (GTK_MESSAGE_DIALOG (dialog), "%s",
						    format);
	gtk_dialog_run (GTK_DIALOG (dialog));
	gtk_widget_destroy (dialog);
	g_free (format);
	return TRUE;
}

/**
 * ch_flash_please_attach_device:
 **/
static void
ch_flash_please_attach_device (ChFlashPrivate *priv)
{
	const gchar *title;
	GtkWidget *widget;

	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "image_usb"));
	gtk_widget_show (widget);
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
	/* TRANSLATORS: device is not connected to the computer */
	title = _("Please connect your ColorHug");
	gtk_label_set_label (GTK_LABEL (widget), title);
}

/**
 * ch_flash_startup_cb:
 **/
static void
ch_flash_startup_cb (GApplication *application, ChFlashPrivate *priv)
{
	const gchar *title;
	GError *error = NULL;
	gint retval;
	GtkWidget *main_window;
	GtkWidget *widget;
	GdkPixbuf *pixbuf;
	GString *string;

	/* get UI */
	string = g_string_new ("");
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
	/* TRANSLATORS: show the user some markdown formatted text */
	title = _("See details about the update");
	g_string_append_printf (string, "<a href=\"#\">%s</a>", title);
	gtk_label_set_markup (GTK_LABEL (widget), string->str);
	g_signal_connect (widget, "activate-link",
			  G_CALLBACK (ch_flash_activate_link_cb), priv);

	/* setup logo image */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "image_logo"));
	gtk_image_set_from_icon_name (GTK_IMAGE (widget),
				      "colorhug-gray",
				      GTK_ICON_SIZE_DIALOG);

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
	ch_flash_please_attach_device (priv);

	/* is the colorhug already plugged in? */
	g_usb_device_list_coldplug (priv->device_list);

	/* setup the session */
	priv->session = soup_session_sync_new_with_options (SOUP_SESSION_USER_AGENT, "colorhug-flash",
							    SOUP_SESSION_TIMEOUT, 5000,
							    NULL);
	if (priv->session == NULL) {
		/* TRANSLATORS: internal error when setting up HTTP */
		title = _("Failed to setup networking");
		ch_flash_error_dialog (priv, title, NULL);
		goto out;
	}

	/* automatically use the correct proxies */
	soup_session_add_feature_by_type (priv->session,
					  SOUP_TYPE_PROXY_RESOLVER_DEFAULT);

	/* emulate a device */
	if (g_getenv ("COLORHUG_EMULATE") != NULL) {
		priv->device = ch_flash_get_fake_device (priv);
		ch_flash_got_device (priv);
	}

	/* show main UI */
	gtk_widget_show (main_window);
out:
	g_string_free (string, TRUE);
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
	if (ch_device_is_colorhug (device)) {
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
	if (ch_device_is_colorhug (device)) {
		if (priv->device != NULL)
			g_object_unref (priv->device);
		priv->device = NULL;
		if (!priv->planned_replug)
			ch_flash_please_attach_device (priv);
	}
}

/**
 * ch_flash_ignore_cb:
 **/
static void
ch_flash_ignore_cb (const gchar *log_domain, GLogLevelFlags log_level,
		    const gchar *message, gpointer user_data)
{
}

static void
ch_flash_device_queue_progress_changed_cb (ChDeviceQueue *device_queue,
					   guint percentage,
					   ChFlashPrivate *priv)
{
	GtkWidget *widget;
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "progressbar_status"));
	g_debug ("queue complete %i%%", percentage);
	gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (widget), (gdouble) percentage / 100.0f);
}

/**
 * main:
 **/
int
main (int argc, char **argv)
{
	ChFlashPrivate *priv;
	gboolean ret;
	gboolean verbose = FALSE;
	gchar *filename = NULL;
	GError *error = NULL;
	GOptionContext *context;
	int status = 0;
	const GOptionEntry options[] = {
		{ "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose,
			/* TRANSLATORS: command line option */
			_("Show extra debugging information"), NULL },
		{ "filename", 'f', 0, G_OPTION_ARG_STRING, &filename,
			/* TRANSLATORS: command line option */
			_("Flash a specific firmware file"), NULL },
		{ NULL}
	};

	setlocale (LC_ALL, "");

	bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	gtk_init (&argc, &argv);

	/* TRANSLATORS: a program to update the firmware flash on the device */
	context = g_option_context_new (_("ColorHug Flash Program"));
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

	priv = g_new0 (ChFlashPrivate, 1);
	priv->settings = g_settings_new ("com.hughski.colorhug-client");
	priv->filename = filename;
	priv->update_details = g_string_new ("");
	priv->warning_details = g_string_new ("");
	priv->markdown = ch_markdown_new ();
	priv->device_queue = ch_device_queue_new ();
	g_signal_connect (priv->device_queue,
			  "progress-changed",
			  G_CALLBACK (ch_flash_device_queue_progress_changed_cb),
			  priv);
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

	/* set verbose? */
	if (verbose) {
		g_setenv ("COLORHUG_VERBOSE", "1", FALSE);
	} else {
		g_log_set_handler (G_LOG_DOMAIN, G_LOG_LEVEL_DEBUG,
				   ch_flash_ignore_cb, NULL);
	}

	/* wait */
	status = g_application_run (G_APPLICATION (priv->application), argc, argv);

	g_object_unref (priv->application);
	if (priv->update_details != NULL)
		g_string_free (priv->update_details, TRUE);
	if (priv->warning_details != NULL)
		g_string_free (priv->warning_details, TRUE);
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
	if (priv->markdown != NULL)
		g_object_unref (priv->markdown);
	if (priv->settings != NULL)
		g_object_unref (priv->settings);
	g_free (priv->filename);
	g_free (priv->checksum);
	g_free (priv->firmware_data);
	g_free (priv);
	return status;
}
