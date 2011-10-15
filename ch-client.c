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

#include <glib-object.h>
#include <gio/gio.h>
#include <gusb.h>

#include "ch-client.h"

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
	GUsbDevice			*device;
};

#define	CH_CLIENT_USB_TIMEOUT		2000

G_DEFINE_TYPE (ChClient, ch_client, G_TYPE_OBJECT)

/**
 * ch_client_load:
 **/
gboolean
ch_client_load (ChClient *client, GError **error)
{
	gboolean ret;
	GUsbDeviceList *list;

	g_return_val_if_fail (CH_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* try to find the ColorHug device */
	list = g_usb_device_list_new (client->priv->usb_ctx);
	g_usb_device_list_coldplug (list);
	client->priv->device = g_usb_device_list_find_by_vid_pid (list,
								  CH_USB_VID,
								  CH_USB_PID,
								  error);
	if (client->priv->device == NULL) {
		ret = FALSE;
		goto out;
	}
	g_debug ("Found ColorHug device %s",
		 g_usb_device_get_platform_id (client->priv->device));

	/* load device */
	ret = g_usb_device_open (client->priv->device, error);
	if (!ret)
		goto out;
	ret = g_usb_device_set_configuration (client->priv->device,
					      CH_USB_CONFIG,
					      error);
	if (!ret)
		goto out;
	ret = g_usb_device_claim_interface (client->priv->device,
					    CH_USB_INTERFACE,
					    G_USB_DEVICE_CLAIM_INTERFACE_BIND_KERNEL_DRIVER,
					    error);
	if (!ret)
		goto out;
out:
	if (list != NULL)
		g_object_unref (list);
	return ret;
}


/**
 * ch_client_get_color_select:
 **/
gboolean
ch_client_get_color_select (ChClient *client,
			    ChColorSelect *color_select,
			    GError **error)
{
	gboolean ret;
	guint8 buffer[1];
	gsize actual_length = -1;

	g_return_val_if_fail (CH_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (color_select != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
	g_return_val_if_fail (client->priv->device != NULL, FALSE);

	/* request */
	ret = g_usb_device_control_transfer (client->priv->device,
					     G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					     G_USB_DEVICE_REQUEST_TYPE_STANDARD,
					     G_USB_DEVICE_RECIPIENT_DEVICE,
					     0, /* request */
					     0, /* value */
					     0, /* idx */
					     buffer,
					     sizeof (buffer), /* length */
					     &actual_length, /* actual_length */
					     CH_CLIENT_USB_TIMEOUT,
					     NULL,
					     error);
	if (!ret)
		goto out;

	/* read */
out:
	return ret;
}

/**
 * ch_client_set_color_select:
 **/
gboolean
ch_client_set_color_select (ChClient *client,
			    ChColorSelect color_select,
			    GError **error)
{
	g_return_val_if_fail (CH_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
	g_return_val_if_fail (client->priv->device != NULL, FALSE);
	return TRUE;
}

/**
 * ch_client_get_multiplier:
 **/
gboolean
ch_client_get_multiplier (ChClient *client,
			  ChFreqScale *multiplier,
			  GError **error)
{
	g_return_val_if_fail (CH_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (multiplier != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
	g_return_val_if_fail (client->priv->device != NULL, FALSE);
	return TRUE;
}

/**
 * ch_client_set_multiplier:
 **/
gboolean
ch_client_set_multiplier (ChClient *client,
			  ChFreqScale multiplier,
			  GError **error)
{
	g_return_val_if_fail (CH_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
	g_return_val_if_fail (client->priv->device != NULL, FALSE);
	return TRUE;
}

/**
 * ch_client_get_integral_time:
 **/
gboolean
ch_client_get_integral_time (ChClient *client,
			      guint16 *integral_time,
			      GError **error)
{
	g_return_val_if_fail (CH_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (integral_time != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
	g_return_val_if_fail (client->priv->device != NULL, FALSE);
	return TRUE;
}

/**
 * ch_client_set_integral_time:
 **/
gboolean
ch_client_set_integral_time (ChClient *client,
			     guint16 integral_time,
			     GError **error)
{
	g_return_val_if_fail (CH_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (integral_time > 0, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
	g_return_val_if_fail (client->priv->device != NULL, FALSE);
	return TRUE;
}

/**
 * ch_client_get_firmware_ver:
 **/
gboolean
ch_client_get_firmware_ver (ChClient *client,
			    guint16 *major,
			    guint16 *minor,
			    guint16 *micro,
			    GError **error)
{
	g_return_val_if_fail (CH_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (major != NULL, FALSE);
	g_return_val_if_fail (minor != NULL, FALSE);
	g_return_val_if_fail (micro != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
	g_return_val_if_fail (client->priv->device != NULL, FALSE);
	return TRUE;
}

/**
 * ch_client_set_firmware_ver:
 **/
gboolean
ch_client_set_firmware_ver (ChClient *client,
			    guint16 major,
			    guint16 minor,
			    guint16 micro,
			    GError **error)
{
	g_return_val_if_fail (CH_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (major > 0, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
	g_return_val_if_fail (client->priv->device != NULL, FALSE);
	return TRUE;
}

/**
 * ch_client_get_calibration:
 **/
gboolean
ch_client_get_calibration (ChClient *client,
			   gfloat **calibration,
			   GError **error)
{
	g_return_val_if_fail (CH_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (calibration != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
	g_return_val_if_fail (client->priv->device != NULL, FALSE);
	return TRUE;
}

/**
 * ch_client_set_calibration:
 **/
gboolean
ch_client_set_calibration (ChClient *client,
			   gfloat *calibration,
			   GError **error)
{
	g_return_val_if_fail (CH_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (calibration != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
	g_return_val_if_fail (client->priv->device != NULL, FALSE);
	return TRUE;
}

/**
 * ch_client_get_serial_number:
 **/
gboolean
ch_client_get_serial_number (ChClient *client,
			     guint64 *serial_number,
			     GError **error)
{
	g_return_val_if_fail (CH_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (serial_number != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
	g_return_val_if_fail (client->priv->device != NULL, FALSE);
	return TRUE;
}

/**
 * ch_client_set_serial_number:
 **/
gboolean
ch_client_set_serial_number (ChClient *client,
			     guint64 serial_number,
			     GError **error)
{
	g_return_val_if_fail (CH_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (serial_number > 0, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
	g_return_val_if_fail (client->priv->device != NULL, FALSE);
	return TRUE;
}

/**
 * ch_client_get_write_protect:
 **/
gboolean
ch_client_get_write_protect (ChClient *client,
			     gboolean *write_protect,
			     GError **error)
{
	g_return_val_if_fail (CH_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (write_protect != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
	g_return_val_if_fail (client->priv->device != NULL, FALSE);
	return TRUE;
}

/**
 * ch_client_set_write_protect:
 **/
gboolean
ch_client_set_write_protect (ChClient *client,
			     const gchar *write_protect,
			     GError **error)
{
	g_return_val_if_fail (CH_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (write_protect != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
	g_return_val_if_fail (client->priv->device != NULL, FALSE);
	return TRUE;
}

/**
 * ch_client_take_reading:
 **/
gboolean
ch_client_take_reading (ChClient *client,
			guint16 *take_reading,
			GError **error)
{
	g_return_val_if_fail (CH_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (take_reading != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
	g_return_val_if_fail (client->priv->device != NULL, FALSE);
	return TRUE;
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

	if (client->priv->device != NULL)
		g_object_unref (client->priv->device);
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

