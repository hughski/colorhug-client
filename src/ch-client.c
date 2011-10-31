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

#include <stdlib.h>
#include <string.h>
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
	g_debug ("Opened device");
	ret = g_usb_device_set_configuration (client->priv->device,
					      CH_USB_CONFIG,
					      error);
	if (!ret)
		goto out;
	g_debug ("Set configuration 0x%x", CH_USB_CONFIG);
	ret = g_usb_device_claim_interface (client->priv->device,
					    CH_USB_INTERFACE,
					    G_USB_DEVICE_CLAIM_INTERFACE_BIND_KERNEL_DRIVER,
					    error);
	if (!ret)
		goto out;
	g_debug ("Caimed interface 0x%x for device", CH_USB_INTERFACE);
out:
	if (list != NULL)
		g_object_unref (list);
	return ret;
}

/**
 * ch_client_print_data:
 **/
static void
ch_client_print_data (const gchar *title,
		      const guint8 *data,
		      gsize length)
{
	guint i;

	if (g_strcmp0 (title, "request") == 0)
		g_print ("%c[%dm", 0x1B, 31);
	if (g_strcmp0 (title, "reply") == 0)
		g_print ("%c[%dm", 0x1B, 34);
	g_print ("%s\t", title);

	for (i=0; i< length; i++)
		g_print ("%02x [%c]\t", data[i], g_ascii_isprint (data[i]) ? data[i] : '?');

	g_print ("%c[%dm\n", 0x1B, 0);
}

/**
 * ch_client_write_command:
 *
 * @client:		A #ChClient
 * @cmd:		The command to use, e.g. %CH_CMD_GET_COLOR_SELECT
 * @buffer_in:		The input buffer of data, or %NULL
 * @buffer_in_length:	The input buffer length
 * @buffer_out:		The output buffer of data, or %NULL
 * @buffer_out_length:	The output buffer length
 * @error:		A #GError, or %NULL
 *
 * Sends a message to the device and waits for a reply.
 *
 **/
static gboolean
ch_client_write_command (ChClient *client,
			 guint8 cmd,
			 const guint8 *buffer_in,
			 gsize buffer_in_length,
			 guint8 *buffer_out,
			 gsize buffer_out_length,
			 GError **error)
{
	gboolean ret;
	gsize actual_length = -1;
	guint8 buffer[64];

	/* clear buffer for debugging */
	memset (buffer, 0xff, sizeof (buffer));

	/* set command */
	buffer[CH_BUFFER_INPUT_CMD] = cmd;
	if (buffer_in != NULL) {
		memcpy (buffer + CH_BUFFER_INPUT_DATA,
			buffer_in,
			buffer_in_length);
	}

	/* request */
	ch_client_print_data ("request", buffer, sizeof(buffer));
	ret = g_usb_device_interrupt_transfer (client->priv->device,
					       CH_USB_HID_EP_OUT,
					       buffer,
					       sizeof(buffer),
					       &actual_length,
					       CH_CLIENT_USB_TIMEOUT,
					       NULL,
					       error);
	if (!ret)
		goto out;

	/* read */
	ret = g_usb_device_interrupt_transfer (client->priv->device,
					       CH_USB_HID_EP_IN,
					       buffer,
					       sizeof(buffer),
					       &actual_length,
					       CH_CLIENT_USB_TIMEOUT,
					       NULL,
					       error);
	if (!ret)
		goto out;
	ch_client_print_data ("reply", buffer, sizeof(buffer));

	/* parse */
	if (buffer[CH_BUFFER_OUTPUT_RETVAL] != CH_FATAL_ERROR_NONE ||
	    buffer[CH_BUFFER_OUTPUT_CMD] != cmd ||
	    actual_length != buffer_out_length + CH_BUFFER_OUTPUT_DATA) {
		ret = FALSE;
		g_set_error (error, 1, 0,
			     "Invalid read: retval=%02x "
			     "cmd=%02x (expected 0x%x) "
			     "len=%li (expected %li)",
			     buffer[CH_BUFFER_OUTPUT_RETVAL],
			     buffer[CH_BUFFER_OUTPUT_CMD],
			     cmd,
			     actual_length,
			     buffer_out_length + CH_BUFFER_OUTPUT_DATA);
		goto out;
	}

	/* copy */
	if (buffer_out != NULL) {
		memcpy (buffer_out,
			buffer + CH_BUFFER_OUTPUT_DATA,
			buffer_out_length);
	}
out:
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

	g_return_val_if_fail (CH_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (color_select != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
	g_return_val_if_fail (client->priv->device != NULL, FALSE);

	/* hit hardware */
	ret = ch_client_write_command (client,
				       CH_CMD_GET_COLOR_SELECT,
				       NULL,	/* buffer in */
				       0,	/* size of input buffer */
				       (guint8 *) color_select,
				       1,	/* size of output buffer */
				       error);
	if (!ret)
		goto out;
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
	gboolean ret;

	g_return_val_if_fail (CH_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
	g_return_val_if_fail (client->priv->device != NULL, FALSE);

	/* hit hardware */
	ret = ch_client_write_command (client,
				       CH_CMD_SET_COLOR_SELECT,
				       (const guint8 *) &color_select,	/* buffer in */
				       1,	/* size of input buffer */
				       NULL,	/* buffer out */
				       0,	/* size of output buffer */
				       error);
	if (!ret)
		goto out;
out:
	return ret;
}

/**
 * ch_client_get_multiplier:
 **/
gboolean
ch_client_get_multiplier (ChClient *client,
			  ChFreqScale *multiplier,
			  GError **error)
{
	gboolean ret;

	g_return_val_if_fail (CH_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (multiplier != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
	g_return_val_if_fail (client->priv->device != NULL, FALSE);

	/* hit hardware */
	ret = ch_client_write_command (client,
				       CH_CMD_GET_MULTIPLIER,
				       NULL,	/* buffer in */
				       0,	/* size of input buffer */
				       (guint8 *) multiplier,
				       1,	/* size of output buffer */
				       error);
	if (!ret)
		goto out;
out:
	return ret;
}

/**
 * ch_client_set_multiplier:
 **/
gboolean
ch_client_set_multiplier (ChClient *client,
			  ChFreqScale multiplier,
			  GError **error)
{
	gboolean ret;

	g_return_val_if_fail (CH_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
	g_return_val_if_fail (client->priv->device != NULL, FALSE);

	/* hit hardware */
	ret = ch_client_write_command (client,
				       CH_CMD_SET_MULTIPLIER,
				       (const guint8 *) &multiplier,	/* buffer in */
				       1,	/* size of input buffer */
				       NULL,	/* buffer out */
				       0,	/* size of output buffer */
				       error);
	if (!ret)
		goto out;
out:
	return ret;
}

/**
 * ch_client_get_integral_time:
 **/
gboolean
ch_client_get_integral_time (ChClient *client,
			      guint16 *integral_time,
			      GError **error)
{
	gboolean ret;

	g_return_val_if_fail (CH_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (integral_time != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
	g_return_val_if_fail (client->priv->device != NULL, FALSE);

	/* hit hardware */
	ret = ch_client_write_command (client,
				       CH_CMD_GET_INTERGRAL_TIME,
				       NULL,	/* buffer in */
				       0,	/* size of input buffer */
				       (guint8 *) integral_time,
				       2,	/* size of output buffer */
				       error);
	if (!ret)
		goto out;
out:
	return ret;
}

/**
 * ch_client_set_integral_time:
 **/
gboolean
ch_client_set_integral_time (ChClient *client,
			     guint16 integral_time,
			     GError **error)
{
	gboolean ret;

	g_return_val_if_fail (CH_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (integral_time > 0, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
	g_return_val_if_fail (client->priv->device != NULL, FALSE);

	/* hit hardware */
	ret = ch_client_write_command (client,
				       CH_CMD_SET_INTERGRAL_TIME,
				       (const guint8 *) &integral_time,	/* buffer in */
				       2,	/* size of input buffer */
				       NULL,	/* buffer out */
				       0,	/* size of output buffer */
				       error);
	if (!ret)
		goto out;
out:
	return ret;
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
	gboolean ret;
	guint16 buffer[3];

	g_return_val_if_fail (CH_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (major != NULL, FALSE);
	g_return_val_if_fail (minor != NULL, FALSE);
	g_return_val_if_fail (micro != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
	g_return_val_if_fail (client->priv->device != NULL, FALSE);

	/* hit hardware */
	ret = ch_client_write_command (client,
				       CH_CMD_GET_FIRMWARE_VERSION,
				       NULL,	/* buffer in */
				       0,	/* size of input buffer */
				       (guint8 *) buffer,
				       sizeof(buffer),	/* size of output buffer */
				       error);
	if (!ret)
		goto out;

	/* parse */
	*major = buffer[0];
	*minor = buffer[1];
	*micro = buffer[2];
out:
	return ret;
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
	gboolean ret;
	guint8 buffer[6];

	g_return_val_if_fail (CH_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (major > 0, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
	g_return_val_if_fail (client->priv->device != NULL, FALSE);

	/* hit hardware */
	buffer[0] = major & 0x00ff;
	buffer[1] = (major & 0xff00) / 0xff;
	buffer[2] = minor & 0x00ff;
	buffer[3] = (minor & 0xff00) / 0xff;
	buffer[4] = micro & 0x00ff;
	buffer[5] = (micro & 0xff00) / 0xff;
	ret = ch_client_write_command (client,
				       CH_CMD_SET_FIRMWARE_VERSION,
				       buffer,	/* buffer in */
				       sizeof(buffer),	/* size of input buffer */
				       NULL,
				       0,	/* size of output buffer */
				       error);
	if (!ret)
		goto out;
out:
	return ret;
}

/**
 * ch_client_get_calibration:
 **/
gboolean
ch_client_get_calibration (ChClient *client,
			   gfloat **calibration,
			   GError **error)
{
	gboolean ret;
	guint8 buffer[36];
	guint i;

	g_return_val_if_fail (CH_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (calibration != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
	g_return_val_if_fail (client->priv->device != NULL, FALSE);

	/* hit hardware */
	ret = ch_client_write_command (client,
				       CH_CMD_GET_CALIBRATION,
				       NULL,	/* buffer in */
				       0,	/* size of input buffer */
				       buffer,
				       sizeof(buffer),	/* size of output buffer */
				       error);
	if (!ret)
		goto out;

	/* parse */
	*calibration = g_new0 (gfloat, 9);
	for (i = 0; i < 9; i++)
		*calibration[i] = ((gfloat *)buffer)[i];
out:
	return ret;
}

/**
 * ch_client_set_calibration:
 **/
gboolean
ch_client_set_calibration (ChClient *client,
			   gfloat *calibration,
			   GError **error)
{
	gboolean ret;
	guint32 *tmp;
	guint8 buffer[36];
	guint i;

	g_return_val_if_fail (CH_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (calibration != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
	g_return_val_if_fail (client->priv->device != NULL, FALSE);

	/* hit hardware */
	for (i = 0; i < 9; i++) {
		tmp = (guint32 *) &calibration[i];
		buffer[i*4+0] = (*tmp << 24) & 0xff;
		buffer[i*4+1] = (*tmp << 16) & 0xff;
		buffer[i*4+2] = (*tmp << 8) & 0xff;
		buffer[i*4+3] = (*tmp << 0) & 0xff;
	}
	ret = ch_client_write_command (client,
				       CH_CMD_SET_CALIBRATION,
				       buffer,	/* buffer in */
				       sizeof(buffer),	/* size of input buffer */
				       NULL,
				       0,	/* size of output buffer */
				       error);
	if (!ret)
		goto out;
out:
	return ret;
}

/**
 * ch_client_get_serial_number:
 **/
gboolean
ch_client_get_serial_number (ChClient *client,
			     guint64 *serial_number,
			     GError **error)
{
	gboolean ret;

	g_return_val_if_fail (CH_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (serial_number != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
	g_return_val_if_fail (client->priv->device != NULL, FALSE);

	/* hit hardware */
	ret = ch_client_write_command (client,
				       CH_CMD_GET_SERIAL_NUMBER,
				       NULL,	/* buffer in */
				       0,	/* size of input buffer */
				       (guint8 *) serial_number,
				       4,	/* size of output buffer */
				       error);
	if (!ret)
		goto out;
out:
	return ret;
}

/**
 * ch_client_set_serial_number:
 **/
gboolean
ch_client_set_serial_number (ChClient *client,
			     guint64 serial_number,
			     GError **error)
{
	gboolean ret;

	g_return_val_if_fail (CH_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (serial_number > 0, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
	g_return_val_if_fail (client->priv->device != NULL, FALSE);

	/* hit hardware */
	ret = ch_client_write_command (client,
				       CH_CMD_SET_SERIAL_NUMBER,
				       (const guint8 *) &serial_number,	/* buffer in */
				       4,	/* size of input buffer */
				       NULL,
				       0,	/* size of output buffer */
				       error);
	if (!ret)
		goto out;
out:
	return ret;
}

/**
 * ch_client_get_leds:
 **/
gboolean
ch_client_get_leds (ChClient *client,
		    guint8 *leds,
		    GError **error)
{
	gboolean ret;

	g_return_val_if_fail (CH_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (leds != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
	g_return_val_if_fail (client->priv->device != NULL, FALSE);

	/* hit hardware */
	ret = ch_client_write_command (client,
				       CH_CMD_GET_LEDS,
				       NULL,	/* buffer in */
				       0,	/* size of input buffer */
				       (guint8 *) leds,
				       1,	/* size of output buffer */
				       error);
	if (!ret)
		goto out;
out:
	return ret;
}

/**
 * ch_client_set_leds:
 **/
gboolean
ch_client_set_leds (ChClient *client,
		    guint8 leds,
		    GError **error)
{
	gboolean ret;

	g_return_val_if_fail (CH_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (leds < 0x04, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
	g_return_val_if_fail (client->priv->device != NULL, FALSE);

	/* hit hardware */
	ret = ch_client_write_command (client,
				       CH_CMD_SET_LEDS,
				       (const guint8 *) &leds,	/* buffer in */
				       1,	/* size of input buffer */
				       NULL,
				       0,	/* size of output buffer */
				       error);
	if (!ret)
		goto out;
out:
	return ret;
}

/**
 * ch_client_write_eeprom:
 **/
gboolean
ch_client_write_eeprom (ChClient *client,
			const gchar *magic,
			GError **error)
{
	gboolean ret;

	g_return_val_if_fail (CH_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (magic != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
	g_return_val_if_fail (client->priv->device != NULL, FALSE);

	/* hit hardware */
	ret = ch_client_write_command (client,
				       CH_CMD_WRITE_EEPROM,
				       (const guint8 *) magic,	/* buffer in */
				       strlen(magic),	/* size of input buffer */
				       NULL,
				       0,	/* size of output buffer */
				       error);
	if (!ret)
		goto out;
out:
	return ret;
}

/**
 * ch_client_take_reading:
 **/
gboolean
ch_client_take_reading (ChClient *client,
			guint16 *take_reading,
			GError **error)
{
	gboolean ret;

	g_return_val_if_fail (CH_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (take_reading != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
	g_return_val_if_fail (client->priv->device != NULL, FALSE);

	/* hit hardware */
	ret = ch_client_write_command (client,
				       CH_CMD_TAKE_READING,
				       NULL,	/* buffer in */
				       0,	/* size of input buffer */
				       (guint8 *) &take_reading,
				       2,	/* size of output buffer */
				       error);
	if (!ret)
		goto out;
out:
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

