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
#include "ch-markdown.h"
#include "ch-flash-md.h"

/* don't change this unless you want to provide firmware updates */
#define COLORHUG_FIRMWARE_LOCATION	"http://www.hughski.com/downloads/colorhug/firmware/"

typedef struct {
	gchar		*filename;
	GString		*update_details;
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
	guint		 flash_idx;
	gsize		 flash_chunk_len;
	guint8		 flash_buffer[64];
	ChMarkdown	*markdown;
} ChFlashPrivate;

static void	 ch_flash_read_firmware_chunk	(ChFlashPrivate *priv);
static void	 ch_flash_write_firmware_chunk	(ChFlashPrivate *priv);

/**
 * ch_flash_error_dialog:
 **/
static void
ch_flash_error_dialog (ChFlashPrivate *priv, const gchar *title, const gchar *message)
{
	GtkWindow *window;
	GtkWidget *dialog;

	window = GTK_WINDOW(gtk_builder_get_object (priv->builder, "dialog_flash"));
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

	/* TRANSLATORS: this explains the flashy light thing */
	title = _("If there were any problems flashing the device, it will enter a bootloader mode.");
	g_string_append_printf (msg, "%s\n", title);

	/* TRANSLATORS: hopefully we can "fix" this automatically */
	title = _("Just remove the ColorHug device from the computer, reinsert it and re-run this program.");
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
 * ch_flash_calculate_checksum:
 **/
static guint8
ch_flash_calculate_checksum (guint8 *data,
			     gsize len)
{
	guint8 checksum = 0xff;
	guint i;
	for (i = 0; i < len; i++)
		checksum ^= data[i];
	return checksum;
}

/**
 * ch_flash_write_flash_cb:
 **/
static void
ch_flash_write_flash_cb (GObject *source,
			 GAsyncResult *res,
			 gpointer user_data)
{
	const gchar *title;
	ChFlashPrivate *priv = (ChFlashPrivate *) user_data;
	gboolean ret;
	GError *error = NULL;
	GUsbDevice *device = G_USB_DEVICE (source);

	/* get data */
	ret = ch_device_write_command_finish (device, res, &error);
	if (!ret) {
		ch_flash_error_do_not_panic (priv);
		/* TRANSLATORS: the flash write failed */
		title = _("Failed to write the flash");
		ch_flash_error_dialog (priv, title, error->message);
		g_error_free (error);
		goto out;
	}

	/* write the next chunk */
	ch_flash_write_firmware_chunk (priv);
out:
	return;
}

/**
 * ch_flash_write_flash:
 **/
static void
ch_flash_write_flash (ChFlashPrivate *priv,
		      guint16 address,
		      guint8 *data,
		      gsize len)
{
	guint16 addr_le;
	guint8 buffer_tx[64];

	/* set address, length, checksum, data */
	addr_le = GUINT16_TO_LE (address);
	memcpy (buffer_tx + 0, &addr_le, 2);
	buffer_tx[2] = len;
	buffer_tx[3] = ch_flash_calculate_checksum (data, len);
	memcpy (buffer_tx + 4, data, len);

	/* hit hardware */
	ch_device_write_command_async (priv->device,
				       CH_CMD_WRITE_FLASH,
				       buffer_tx,
				       len + 4,
				       NULL, /* buffer_out */
				       0, /* buffer_out_len */
				       NULL, /* cancellable */
				       ch_flash_write_flash_cb,
				       priv);
}

/**
 * ch_flash_read_flash_cb:
 **/
static void
ch_flash_read_flash_cb (GObject *source,
			GAsyncResult *res,
			gpointer user_data)
{
	const gchar *title;
	ChFlashPrivate *priv = (ChFlashPrivate *) user_data;
	gboolean ret;
	gchar *str = NULL;
	GError *error = NULL;
	guint8 expected_checksum;
	GUsbDevice *device = G_USB_DEVICE (source);

	/* get data */
	ret = ch_device_write_command_finish (device, res, &error);
	if (!ret) {
		ch_flash_error_do_not_panic (priv);
		/* TRANSLATORS: the flash failed and the checksum is wrong */
		title = _("Failed to verify the flash");
		ch_flash_error_dialog (priv, title, error->message);
		g_error_free (error);
		goto out;
	}

	/* verify checksum */
	expected_checksum = ch_flash_calculate_checksum (priv->flash_buffer + 1,
							 priv->flash_chunk_len);
	if (priv->flash_buffer[0] != expected_checksum) {
		/* TRANSLATORS: the write failed */
		str = g_strdup_printf (_("Checksum failed at 0x%04x"),
				       CH_EEPROM_ADDR_RUNCODE + priv->flash_idx);
		ch_flash_error_do_not_panic (priv);
		ch_flash_error_dialog (priv,
				       _("Failed to verify the checksum"),
				       str);
		goto out;
	}

	/* failed to verify chunk */
	if (memcmp (priv->firmware_data + priv->flash_idx,
		    priv->flash_buffer + 1,
		    priv->flash_chunk_len) != 0) {
		/* TRANSLATORS: the write failed */
		str = g_strdup_printf (_("Verification failed at 0x%04x (len %i)"),
				       CH_EEPROM_ADDR_RUNCODE + priv->flash_idx,
				       (guint) priv->flash_chunk_len);
		ch_flash_error_do_not_panic (priv);
		/* TRANSLATORS: the write failed */
		title = _("Failed to verify the flash");
		ch_flash_error_dialog (priv, title, str);
		goto out;
	}

	/* read the next chunk */
	priv->flash_idx += priv->flash_chunk_len;
	ch_flash_read_firmware_chunk (priv);
out:
	g_free (str);
	return;
}

/**
 * ch_flash_read_flash:
 **/
static void
ch_flash_read_flash (ChFlashPrivate *priv,
		     guint16 address,
		     gsize len)
{
	guint8 buffer_tx[3];
	guint16 addr_le;

	/* set address, length, checksum, data */
	addr_le = GUINT16_TO_LE (address);
	memcpy (buffer_tx + 0, &addr_le, 2);
	buffer_tx[2] = len;

	/* hit hardware */
	ch_device_write_command_async (priv->device,
				       CH_CMD_READ_FLASH,
				       buffer_tx,
				       sizeof(buffer_tx),
				       priv->flash_buffer,
				       len + 1,
				       NULL, /* cancellable */
				       ch_flash_read_flash_cb,
				       priv);
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
	GUsbDevice *device = G_USB_DEVICE (source);

	/* get data */
	ret = ch_device_write_command_finish (device, res, &error);
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
	guint8 flash_success = 0x01;

	/* update UI */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_status"));
	/* TRANSLATORS: tell the hardware we succeeded */
	title = _("Setting flash success...");
	gtk_label_set_label (GTK_LABEL (widget), title);

	/* need to boot into bootloader */
	ch_device_write_command_async (priv->device,
				       CH_CMD_SET_FLASH_SUCCESS,
				       &flash_success,
				       1,
				       NULL, /* buffer_out */
				       0, /* buffer_out_len */
				       NULL, /* cancellable */
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
	GUsbDevice *device = G_USB_DEVICE (source);

	/* get data */
	ret = ch_device_write_command_finish (device, res, &error);
	if (!ret) {
		ch_flash_error_do_not_panic (priv);
		/* TRANSLATORS: the new firmware will not load */
		title = _("Failed to boot the ColorHug");
		ch_flash_error_dialog (priv, title, error->message);
		g_error_free (error);
		goto out;
	}

	/* wait one second */
	g_timeout_add (1000, ch_flash_boot_flash_delay_cb, priv);
out:
	return;
}

/**
 * ch_flash_read_firmware_chunk:
 **/
static void
ch_flash_read_firmware_chunk (ChFlashPrivate *priv)
{
	const gchar *title;
	GtkWidget *widget;
	gfloat complete;

	/* work out percentage complete */
	complete = (gfloat) priv->flash_idx / (gfloat) priv->firmware_len;

	/* update UI */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "progressbar_status"));
	gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (widget), complete);

	/* any more data to read? */
	if (priv->flash_idx < priv->firmware_len) {
		if (priv->flash_idx + priv->flash_chunk_len > priv->firmware_len)
			priv->flash_chunk_len = priv->firmware_len - priv->flash_idx;
		g_debug ("Reading at %04x size %li",
			 CH_EEPROM_ADDR_RUNCODE + priv->flash_idx,
			 priv->flash_chunk_len);
		ch_flash_read_flash (priv,
				     CH_EEPROM_ADDR_RUNCODE + priv->flash_idx,
				     priv->flash_chunk_len);
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
	title = _("Starting the new firmware...");
	gtk_label_set_label (GTK_LABEL (widget), title);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "box_detected"));
	gtk_widget_show (widget);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "box_msg"));
	gtk_widget_show (widget);

	/* this is planned */
	priv->planned_replug = TRUE;

	/* boot into new code */
	ch_device_write_command_async (priv->device,
				       CH_CMD_BOOT_FLASH,
				       NULL, /* buffer_in */
				       0, /* buffer_in_len */
				       NULL, /* buffer_out */
				       0, /* buffer_out_len */
				       NULL, /* cancellable */
				       ch_flash_boot_flash_cb,
				       priv);
out:
	return;
}

/**
 * ch_flash_write_firmware_chunk:
 **/
static void
ch_flash_write_firmware_chunk (ChFlashPrivate *priv)
{
	const gchar *title;
	GtkWidget *widget;
	gfloat complete;

	/* work out percentage complete */
	complete = (gfloat) priv->flash_idx / (gfloat) priv->firmware_len;

	/* update UI */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "progressbar_status"));
	gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (widget), complete);

	/* write data */
	if (priv->flash_idx < priv->firmware_len) {
		if (priv->flash_idx + priv->flash_chunk_len > priv->firmware_len)
			priv->flash_chunk_len = priv->firmware_len - priv->flash_idx;
		g_debug ("Writing at %04x size %li",
			 CH_EEPROM_ADDR_RUNCODE + priv->flash_idx,
			 priv->flash_chunk_len);
		ch_flash_write_flash (priv,
				      CH_EEPROM_ADDR_RUNCODE + priv->flash_idx,
				      (guint8 *) priv->firmware_data + priv->flash_idx,
				      priv->flash_chunk_len);
		priv->flash_idx += priv->flash_chunk_len;
		goto out;
	};

	/* flush to 64 byte chunk */
	if ((priv->flash_idx & CH_FLASH_TRANSFER_BLOCK_SIZE) == 0) {
		priv->flash_idx -= priv->flash_chunk_len;
		priv->flash_idx += CH_FLASH_TRANSFER_BLOCK_SIZE;
		g_debug ("Flushing at %04x",
			 CH_EEPROM_ADDR_RUNCODE + priv->flash_idx);
		ch_flash_write_flash (priv,
				      CH_EEPROM_ADDR_RUNCODE + priv->flash_idx,
				      (guint8 *) priv->firmware_data,
				      0);
		goto out;
	}

	/* update UI */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_status"));
	/* TRANSLATORS: now we've written the firmware, we have to
	 * verify it before we tell the device it was successfull */
	title = _("Verifying new firmware...");
	gtk_label_set_label (GTK_LABEL (widget), title);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "progressbar_status"));
	gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (widget), 0.0f);

	/* verify by reading in 60 byte chunks */
	priv->flash_idx = 0;
	priv->flash_chunk_len = 60;
	ch_flash_read_firmware_chunk (priv);
out:
	return;
}

/**
 * ch_flash_erase_flash_cb:
 **/
static void
ch_flash_erase_flash_cb (GObject *source,
			 GAsyncResult *res,
			 gpointer user_data)
{
	const gchar *title;
	ChFlashPrivate *priv = (ChFlashPrivate *) user_data;
	gboolean ret;
	GError *error = NULL;
	GtkWidget *widget;
	GUsbDevice *device = G_USB_DEVICE (source);

	/* get data */
	ret = ch_device_write_command_finish (device, res, &error);
	if (!ret) {
		ch_flash_error_do_not_panic (priv);
		/* TRANSLATORS: clear enough flash for the new firmware */
		title = _("Failed to erase the flash");
		ch_flash_error_dialog (priv, title, error->message);
		g_error_free (error);
		goto out;
	}

	/* update UI */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_status"));
	/* TRANSLATORS: now write the new firmware chunks */
	title = _("Writing new firmware...");
	gtk_label_set_label (GTK_LABEL (widget), title);

	/* write in 32 byte chunks */
	priv->flash_idx = 0;
	priv->flash_chunk_len = CH_FLASH_TRANSFER_BLOCK_SIZE;
	ch_flash_write_firmware_chunk (priv);
out:
	return;
}

/**
 * ch_flash_set_flash_success_0_cb:
 **/
static void
ch_flash_set_flash_success_0_cb (GObject *source,
				 GAsyncResult *res,
				 gpointer user_data)
{
	const gchar *title;
	ChFlashPrivate *priv = (ChFlashPrivate *) user_data;
	gboolean ret;
	GError *error = NULL;
	guint16 addr_le;
	guint8 buffer_tx[4];
	guint16 len_le;
	GUsbDevice *device = G_USB_DEVICE (source);

	/* get data */
	ret = ch_device_write_command_finish (device, res, &error);
	if (!ret) {
		ch_flash_error_do_not_panic (priv);
		/* TRANSLATORS: tell the device the firmware is no
		 * longer known working */
		title = _("Failed to set the flash success false");
		ch_flash_error_dialog (priv, title, error->message);
		g_error_free (error);
		goto out;
	}

	/* set address, length */
	addr_le = GUINT16_TO_LE (CH_EEPROM_ADDR_RUNCODE);
	memcpy (buffer_tx + 0, &addr_le, 2);
	len_le = GUINT16_TO_LE (priv->firmware_len);
	memcpy (buffer_tx + 2, &len_le, 2);

	/* erase enough flash for the program code */
	ch_device_write_command_async (priv->device,
				       CH_CMD_ERASE_FLASH,
				       buffer_tx,
				       sizeof(buffer_tx),
				       NULL, /* buffer_out */
				       0, /* buffer_out_len */
				       NULL, /* cancellable */
				       ch_flash_erase_flash_cb,
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
	guint8 flash_success = 0x00;

	/* update UI */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_status"));
	/* TRANSLATORS: tell the device the firmware is no longer known working */
	title = _("Clearing flash success...");
	gtk_label_set_label (GTK_LABEL (widget), title);

	/* need to boot into bootloader */
	ch_device_write_command_async (priv->device,
				       CH_CMD_SET_FLASH_SUCCESS,
				       &flash_success,
				       1,
				       NULL, /* buffer_out */
				       0, /* buffer_out_len */
				       NULL, /* cancellable */
				       ch_flash_set_flash_success_0_cb,
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
	GUsbDevice *device = G_USB_DEVICE (source);

	/* get data */
	ret = ch_device_write_command_finish (device, res, &error);
	if (!ret) {
		ch_flash_error_do_not_panic (priv);
		/* TRANSLATORS: we restart the device in bootloader mode */
		title = _("Failed to reset the ColorHug");
		ch_flash_error_dialog (priv, title, error->message);
		g_error_free (error);
		goto out;
	}

	/* wait one second */
	g_timeout_add (1000, ch_flash_reset_delay_cb, priv);
out:
	return;
}

/**
 * ch_flash_reset_into_bootloader:
 **/
static void
ch_flash_reset_into_bootloader (ChFlashPrivate *priv)
{
	const gchar *title;
	GtkWidget *widget;

	/* update UI */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_status"));
	/* TRANSLATORS: switch from firmware mode into bootloader mode */
	title = _("Resetting into bootloader...");
	gtk_label_set_label (GTK_LABEL (widget), title);

	/* this is planned */
	priv->planned_replug = TRUE;

	/* need to boot into bootloader */
	ch_device_write_command_async (priv->device,
				       CH_CMD_RESET,
				       NULL, /* buffer_in */
				       0, /* buffer_in_len */
				       NULL, /* buffer_out */
				       0, /* buffer_out_len */
				       NULL, /* cancellable */
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
	const gchar *title;
	ChFlashPrivate *priv = (ChFlashPrivate *) user_data;

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

	/* success */
	priv->firmware_data = g_new0 (guint8, msg->response_body->length);
	priv->firmware_len = msg->response_body->length;
	memcpy (priv->firmware_data,
		msg->response_body->data,
		priv->firmware_len);

	/* we can shortcut as we're already in bootloader mode */
	if (priv->firmware_version[0] == 0) {
		ch_flash_set_flash_success_0 (priv);
		goto out;
	}

	/* reset into the bootloader where we can load the firmware */
	ch_flash_reset_into_bootloader (priv);
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
	const gchar *title;
	SoupURI *base_uri = NULL;
	SoupMessage *msg = NULL;
	gchar *uri = NULL;

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
	title = _("Downloading update...");
	gtk_label_set_label (GTK_LABEL (widget), title);

	/* set progressbar to zero */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "progressbar_status"));
	gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (widget), 0.0f);

	/* download file */
	uri = g_build_filename (COLORHUG_FIRMWARE_LOCATION,
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
		tmp[i] = atoi (split[i]);

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

		/* add changelog text */
		g_string_append_printf (priv->update_details,
					"%s\n", update->changelog->str);

		/* save newest available firmware */
		if (priv->filename == NULL)
			priv->filename = g_strdup (update->filename);
	}

	/* no updates */
	if (priv->filename == NULL) {
		ch_flash_no_updates (priv);
		goto out;
	}

	/* remove trailing space */
	g_string_set_size (priv->update_details,
			   priv->update_details->len - 1);

	/* setup UI */
	ch_flash_has_updates (priv);
out:
	if (updates != NULL)
		g_ptr_array_unref (updates);
}

/**
 * ch_flash_set_lost_or_stolen_cb:
 **/
static void
ch_flash_set_lost_or_stolen_cb (GObject *source,
				GAsyncResult *res,
				gpointer user_data)
{
	const gchar *title;
	ChFlashPrivate *priv = (ChFlashPrivate *) user_data;
	gboolean ret;
	GError *error = NULL;
	GString *msg = NULL;
	GtkWidget *widget;
	GUsbDevice *device = G_USB_DEVICE (source);

	/* get data */
	ret = ch_device_write_command_finish (device, res, &error);
	if (!ret) {
		ch_flash_error_do_not_panic (priv);
		/* TRANSLATORS: the device refused our request to mark
		 * it lost or stolen */
		title = _("Failed to set the device as lost or stolen");
		ch_flash_error_dialog (priv, title, error->message);
		g_error_free (error);
		goto out;
	}

	/* setup UI */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "box_detected"));
	gtk_widget_hide (widget);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "box_warning"));
	gtk_widget_show (widget);

	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_warning"));
	msg = g_string_new ("");
	/* TRANSLATORS: we have a blacklist that we match the device against */
	title = _("This ColorHug has been registered as lost or stolen.");
	g_string_append_printf (msg, "<span weight='bold' size='x-large'>%s</span>",
				title);
	gtk_label_set_markup (GTK_LABEL (widget), msg->str);
	g_string_free (msg, TRUE);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_msg"));
	msg = g_string_new ("");
	/* TRANSLATORS: We disable the device so it can't go into
	 * firmware mode without a magic incantation */
	title = _("The device has now been deactivated.");
	g_string_append_printf (msg, "<b>%s</b>",
				title);
	/* TRANSLATORS: We can tell the user the magic incantation if we've
	 * made a mistake */
	title = _("Please contact <tt>info@hughski.com</tt> for more details.");
	g_string_append_printf (msg, "\n\n%s",
				title);
	gtk_label_set_markup (GTK_LABEL (widget), msg->str);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_close"));
	gtk_widget_set_sensitive (widget, TRUE);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_flash"));
	gtk_widget_hide (widget);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "spinner_progress"));
	gtk_widget_hide (widget);
out:
	if (msg != NULL)
		g_string_free (msg, TRUE);
}

/**
 * ch_flash_set_lost_or_stolen:
 **/
static void
ch_flash_set_lost_or_stolen (ChFlashPrivate *priv)
{
	guint8 flash_success = 0xff;

	/* TODO: need to erase device as well */
	ch_device_write_command_async (priv->device,
				       CH_CMD_SET_FLASH_SUCCESS,
				       &flash_success,
				       1,
				       NULL, /* buffer_out */
				       0, /* buffer_out_len */
				       NULL, /* cancellable */
				       ch_flash_set_lost_or_stolen_cb,
				       priv);
}

/**
 * ch_flash_got_blacklist_cb:
 **/
static void
ch_flash_got_blacklist_cb (SoupSession *session,
			   SoupMessage *msg,
			   gpointer user_data)
{
	const gchar *title;
	ChFlashPrivate *priv = (ChFlashPrivate *) user_data;
	gchar **lines = NULL;
	gchar *uri = NULL;
	guint i;
	guint tmp;
	SoupURI *base_uri = NULL;

	/* we failed */
	if (!SOUP_STATUS_IS_SUCCESSFUL (msg->status_code)) {
		ch_flash_error_no_network (priv);
		/* TRANSLATORS: we failed to get the blacklist from the server */
		title = _("Failed to get device blacklist");
		ch_flash_error_dialog (priv, title, soup_status_get_phrase (msg->status_code));
		goto out;
	}

	/* empty file */
	if (msg->response_body->length == 0)
		goto out;

	/* read file */
	lines = g_strsplit (msg->response_body->data, "\n", -1);
	for (i = 0; lines[i] != NULL; i++) {
		if (lines[i][0] == '\0')
			continue;
		/* check if matches serial number */
		tmp = atoi (lines[i]);
		if (tmp == priv->serial_number) {
			ch_flash_set_lost_or_stolen (priv);
			goto out;
		}
	}

	/* get the latest manifest file */
	uri = g_build_filename (COLORHUG_FIRMWARE_LOCATION,
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
	if (base_uri != NULL)
		soup_uri_free (base_uri);
	g_free (uri);
	g_strfreev (lines);
}

/**
 * ch_flash_got_device_data:
 **/
static void
ch_flash_got_device_data (ChFlashPrivate *priv)
{
	const gchar *title;
	gchar *str = NULL;
	gchar *uri = NULL;
	gchar *user_agent = NULL;
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
	title = _("Checking for updates...");
	gtk_label_set_label (GTK_LABEL (widget), title);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "spinner_progress"));
	gtk_widget_show (widget);

	/* get the latest blacklist file */
	uri = g_build_filename (COLORHUG_FIRMWARE_LOCATION,
				"BLACKLIST",
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
				    ch_flash_got_blacklist_cb, priv);
out:
	/* reset the flag */
	priv->planned_replug = FALSE;
	if (base_uri != NULL)
		soup_uri_free (base_uri);
	g_free (user_agent);
	g_free (str);
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
	GUsbDevice *device = G_USB_DEVICE (source);

	/* get data */
	ret = ch_device_write_command_finish (device, res, &error);
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
	GUsbDevice *device = G_USB_DEVICE (source);

	/* get data */
	ret = ch_device_write_command_finish (device, res, &error);
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
	ch_device_write_command_async (priv->device,
				       CH_CMD_GET_SERIAL_NUMBER,
				       NULL, /* buffer_in */
				       0, /* buffer_in_len */
				       (guint8 *) &priv->serial_number,
				       4,
				       NULL, /* cancellable */
				       ch_flash_get_serial_number_cb,
				       priv);
out:
	return;
}

/**
 * ch_flash_get_hardware_version_cb:
 **/
static void
ch_flash_get_hardware_version_cb (GObject *source,
				  GAsyncResult *res,
				  gpointer user_data)
{
	const gchar *title;
	ChFlashPrivate *priv = (ChFlashPrivate *) user_data;
	gboolean ret;
	GError *error = NULL;
	GUsbDevice *device = G_USB_DEVICE (source);

	/* get data */
	ret = ch_device_write_command_finish (device, res, &error);
	if (!ret) {
		/* TRANSLATORS: the request failed */
		title = _("Failed to contact ColorHug");
		ch_flash_error_dialog (priv, title, error->message);
		g_error_free (error);
		goto out;
	}

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
	ret = g_usb_device_open (priv->device, &error);
	if (!ret) {
		/* TRANSLATORS: permissions error perhaps? */
		title = _("Failed to open device");
		ch_flash_error_dialog (priv, title, error->message);
		g_error_free (error);
		return;
	}
	ret = g_usb_device_set_configuration (priv->device,
					      CH_USB_CONFIG,
					      &error);
	if (!ret) {
		/* TRANSLATORS: we can't set the device config */
		title = _("Failed to set configuration");
		ch_flash_error_dialog (priv, title, error->message);
		g_error_free (error);
		return;
	}
	ret = g_usb_device_claim_interface (priv->device,
					    CH_USB_INTERFACE,
					    G_USB_DEVICE_CLAIM_INTERFACE_BIND_KERNEL_DRIVER,
					    &error);
	if (!ret) {
		/* TRANSLATORS: we can't claim the interface for our sole usage */
		title = _("Failed to claim interface");
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
		title = _("Getting firmware version...");
		gtk_label_set_label (GTK_LABEL (widget), title);
	}

	/* get the firmware version */
	ch_device_write_command_async (priv->device,
				       CH_CMD_GET_HARDWARE_VERSION,
				       NULL, /* buffer_in */
				       0, /* buffer_in_len */
				       &priv->hardware_version,
				       1,
				       NULL, /* cancellable */
				       ch_flash_get_hardware_version_cb,
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
	gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog), "%s",
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

/**
 * main:
 **/
int
main (int argc, char **argv)
{
	ChFlashPrivate *priv;
	gboolean ret;
	gboolean verbose = FALSE;
	GError *error = NULL;
	GOptionContext *context;
	int status = 0;
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
	priv->update_details = g_string_new ("");
	priv->markdown = ch_markdown_new ();
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
	if (priv->device_list != NULL)
		g_object_unref (priv->device_list);
	if (priv->usb_ctx != NULL)
		g_object_unref (priv->usb_ctx);
	if (priv->builder != NULL)
		g_object_unref (priv->builder);
	if (priv->session != NULL)
		g_object_unref (priv->session);
	if (priv->markdown != NULL)
		g_object_unref (priv->markdown);
	g_free (priv->filename);
	g_free (priv->firmware_data);
	g_free (priv);
	return status;
}
