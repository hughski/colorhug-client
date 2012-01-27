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

#include <string.h>
#include <glib-object.h>
#include <gio/gio.h>
#include <gusb.h>

#include "ch-math.h"
#include "ch-client.h"
#include "ch-common.h"

static void     ch_client_finalize	(GObject     *object);

#define CH_CLIENT_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), CH_TYPE_CLIENT, ChClientPrivate))

/**
 * ChClientPrivate:
 *
 * Private #ChClient data
 **/
struct _ChClientPrivate
{
	GUsbContext			*usb_ctx;
};

#define	CH_CLIENT_USB_TIMEOUT		5000 /* ms */

G_DEFINE_TYPE (ChClient, ch_client, G_TYPE_OBJECT)

/**
 * ch_client_get_default:
 **/
GUsbDevice *
ch_client_get_default (ChClient *client, GError **error)
{
	gboolean ret;
	GUsbDeviceList *list;
	GUsbDevice *device = NULL;

	g_return_val_if_fail (CH_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* try to find the ColorHug device */
	list = g_usb_device_list_new (client->priv->usb_ctx);
	g_usb_device_list_coldplug (list);
	device = g_usb_device_list_find_by_vid_pid (list,
						    CH_USB_VID,
						    CH_USB_PID,
						    error);
	if (device == NULL)
		goto out;
	g_debug ("Found ColorHug device %s",
		 g_usb_device_get_platform_id (device));

	/* load device */
	ret = g_usb_device_open (device, error);
	if (!ret)
		goto out;
	g_debug ("Opened device");
	ret = g_usb_device_set_configuration (device,
					      CH_USB_CONFIG,
					      error);
	if (!ret)
		goto out;
	g_debug ("Set configuration 0x%x", CH_USB_CONFIG);
	ret = g_usb_device_claim_interface (device,
					    CH_USB_INTERFACE,
					    G_USB_DEVICE_CLAIM_INTERFACE_BIND_KERNEL_DRIVER,
					    error);
	if (!ret)
		goto out;
	g_debug ("Claimed interface 0x%x for device", CH_USB_INTERFACE);
out:
	if (list != NULL)
		g_object_unref (list);
	return device;
}

/**
 * ch_client_flash_firmware:
 **/
gboolean
ch_client_flash_firmware (ChClient *client,
			  const gchar *filename,
			  GError **error)
{
	gboolean ret;
	gchar *data = NULL;
	guint8 buffer[60];
	guint idx;
	gsize len = 0;
	gsize chunk_len;
	guint8 flash_success;
	GUsbDevice *device = NULL;

	g_return_val_if_fail (CH_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (filename != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* load file */
	ret = g_file_get_contents (filename, &data, &len, error);
	if (!ret)
		goto out;

	/* boot to bootloader */
	device = ch_client_get_default (client, error);
	if (!ret)
		goto out;
	ret = ch_device_cmd_reset (device, error);
	if (!ret)
		goto out;

	/* wait for the device to reconnect */
	g_object_unref (device);
	g_usleep (1 * G_USEC_PER_SEC);
	device = ch_client_get_default (client, error);
	if (!ret)
		goto out;

	/* set flash success false */
	flash_success = 0x00;
	ret = ch_device_write_command (device,
				       CH_CMD_SET_FLASH_SUCCESS,
				       &flash_success, 1,
				       NULL, 0,
				       NULL,	/* cancellable */
				       error);
	if (!ret)
		goto out;

	/* erase flash */
	g_debug ("Erasing at %04x size %li",
		 CH_EEPROM_ADDR_RUNCODE, len);
	ret = ch_device_cmd_erase_flash (device,
					 CH_EEPROM_ADDR_RUNCODE,
					 len,
					 error);
	if (!ret)
		goto out;

	/* just write in 32 byte chunks, as we're sure that the firmware
	 * image has been prepared to end on a 64 byte chunk with
	 * colorhug-inhx32-to-bin >= 0.1.5 */
	idx = 0;
	chunk_len = CH_FLASH_TRANSFER_BLOCK_SIZE;
	do {
		if (idx + chunk_len > len)
			chunk_len = len - idx;
		g_debug ("Writing at %04x size %li",
			 CH_EEPROM_ADDR_RUNCODE + idx,
			 chunk_len);
		ret = ch_device_cmd_write_flash (device,
						 CH_EEPROM_ADDR_RUNCODE + idx,
						 (guint8 *) data + idx,
						 chunk_len,
						 error);
		if (!ret)
			goto out;
		idx += chunk_len;
	} while (idx < len);

	/* read in 60 byte chunks */
	idx = 0;
	chunk_len = 60;
	do {
		if (idx + chunk_len > len)
			chunk_len = len - idx;
		g_debug ("Reading at %04x size %li",
			 CH_EEPROM_ADDR_RUNCODE + idx,
			 chunk_len);
		ret = ch_device_cmd_read_flash (device,
						CH_EEPROM_ADDR_RUNCODE + idx,
						buffer,
						chunk_len,
						error);
		if (!ret)
			goto out;
		if (memcmp (data + idx, buffer, chunk_len) != 0) {
			ret = FALSE;
			g_set_error (error, 1, 0,
				     "Failed to verify @0x%04x",
				     CH_EEPROM_ADDR_RUNCODE + idx);
			goto out;
		}
		idx += chunk_len;
	} while (idx < len);

	/* boot into new code */
	ret = ch_device_write_command (device,
				       CH_CMD_BOOT_FLASH,
				       NULL, 0,
				       NULL, 0,
				       NULL,	/* cancellable */
				       error);
	if (!ret)
		goto out;

	/* wait for the device to reconnect */
	g_object_unref (device);
	g_usleep (CH_FLASH_RECONNECT_TIMEOUT * 1000);
	device = ch_client_get_default (client, error);
	if (!ret)
		goto out;

	/* set flash success true */
	flash_success = 0x01;
	ret = ch_device_write_command (device,
				       CH_CMD_SET_FLASH_SUCCESS,
				       &flash_success, 1,
				       NULL, 0,
				       NULL,	/* cancellable */
				       error);
	if (!ret)
		goto out;
out:
	if (device != NULL)
		g_object_unref (device);
	g_free (data);
	return ret;
}

/**
 * ch_client_class_init:
 **/
static void
ch_client_class_init (ChClientClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = ch_client_finalize;
	g_type_class_add_private (klass, sizeof (ChClientPrivate));
}

/**
 * ch_client_init:
 **/
static void
ch_client_init (ChClient *client)
{
	client->priv = CH_CLIENT_GET_PRIVATE (client);
	client->priv->usb_ctx = g_usb_context_new (NULL);
}

/**
 * ch_client_finalize:
 **/
static void
ch_client_finalize (GObject *object)
{
	ChClient *client = CH_CLIENT (object);
	ChClientPrivate *priv = client->priv;

	g_object_unref (priv->usb_ctx);

	G_OBJECT_CLASS (ch_client_parent_class)->finalize (object);
}

/**
 * ch_client_new:
 **/
ChClient *
ch_client_new (void)
{
	ChClient *client;
	client = g_object_new (CH_TYPE_CLIENT, NULL);
	return CH_CLIENT (client);
}

