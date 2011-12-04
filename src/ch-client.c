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
	g_debug ("Claimed interface 0x%x for device", CH_USB_INTERFACE);
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

	g_return_val_if_fail (CH_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (color_select != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
	g_return_val_if_fail (client->priv->device != NULL, FALSE);

	/* hit hardware */
	ret = ch_device_write_command (client->priv->device,
				       CH_CMD_GET_COLOR_SELECT,
				       NULL,	/* buffer in */
				       0,	/* size of input buffer */
				       (guint8 *) color_select,
				       1,	/* size of output buffer */
				       NULL,	/* cancellable */
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
	guint8 csel8 = color_select;

	g_return_val_if_fail (CH_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
	g_return_val_if_fail (client->priv->device != NULL, FALSE);

	/* hit hardware */
	ret = ch_device_write_command (client->priv->device,
				       CH_CMD_SET_COLOR_SELECT,
				       &csel8,	/* buffer in */
				       1,	/* size of input buffer */
				       NULL,	/* buffer out */
				       0,	/* size of output buffer */
				       NULL,	/* cancellable */
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
	ret = ch_device_write_command (client->priv->device,
				       CH_CMD_GET_MULTIPLIER,
				       NULL,	/* buffer in */
				       0,	/* size of input buffer */
				       (guint8 *) multiplier,
				       1,	/* size of output buffer */
				       NULL,	/* cancellable */
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
	guint8 mult8 = multiplier;

	g_return_val_if_fail (CH_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
	g_return_val_if_fail (client->priv->device != NULL, FALSE);

	/* hit hardware */
	ret = ch_device_write_command (client->priv->device,
				       CH_CMD_SET_MULTIPLIER,
				       &mult8,	/* buffer in */
				       1,	/* size of input buffer */
				       NULL,	/* buffer out */
				       0,	/* size of output buffer */
				       NULL,	/* cancellable */
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
	guint16 integral_le;

	g_return_val_if_fail (CH_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (integral_time != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
	g_return_val_if_fail (client->priv->device != NULL, FALSE);

	/* hit hardware */
	ret = ch_device_write_command (client->priv->device,
				       CH_CMD_GET_INTEGRAL_TIME,
				       NULL,	/* buffer in */
				       0,	/* size of input buffer */
				       (guint8 *) &integral_le,
				       2,	/* size of output buffer */
				       NULL,	/* cancellable */
				       error);
	if (!ret)
		goto out;
	*integral_time = GUINT16_FROM_LE (integral_le);
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
	guint16 integral_le;

	g_return_val_if_fail (CH_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (integral_time > 0, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
	g_return_val_if_fail (client->priv->device != NULL, FALSE);

	integral_le = GUINT16_TO_LE (integral_time);

	/* hit hardware */
	ret = ch_device_write_command (client->priv->device,
				       CH_CMD_SET_INTEGRAL_TIME,
				       (const guint8 *) &integral_le,	/* buffer in */
				       sizeof(guint16),	/* size of input buffer */
				       NULL,	/* buffer out */
				       0,	/* size of output buffer */
				       NULL,	/* cancellable */
				       error);
	if (!ret)
		goto out;
out:
	return ret;
}

/**
 * ch_client_get_calibration_map:
 **/
gboolean
ch_client_get_calibration_map (ChClient *client,
			       guint16 *calibration_map,
			       GError **error)
{
	gboolean ret;

	g_return_val_if_fail (CH_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (calibration_map != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
	g_return_val_if_fail (client->priv->device != NULL, FALSE);

	/* hit hardware */
	ret = ch_device_write_command (client->priv->device,
				       CH_CMD_GET_CALIBRATION_MAP,
				       NULL,	/* buffer in */
				       0,	/* size of input buffer */
				       (guint8 *) calibration_map,
				       6 * sizeof(guint16),	/* size of output buffer */
				       NULL,	/* cancellable */
				       error);
	if (!ret)
		goto out;
out:
	return ret;
}

/**
 * ch_client_set_calibration_map:
 **/
gboolean
ch_client_set_calibration_map (ChClient *client,
			       guint16 *calibration_map,
			       GError **error)
{
	gboolean ret;

	g_return_val_if_fail (CH_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (calibration_map != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
	g_return_val_if_fail (client->priv->device != NULL, FALSE);

	/* hit hardware */
	ret = ch_device_write_command (client->priv->device,
				       CH_CMD_SET_CALIBRATION_MAP,
				       (const guint8 *) calibration_map,	/* buffer in */
				       6 * sizeof(guint16),	/* size of input buffer */
				       NULL,	/* buffer out */
				       0,	/* size of output buffer */
				       NULL,	/* cancellable */
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
	ret = ch_device_write_command (client->priv->device,
				       CH_CMD_GET_FIRMWARE_VERSION,
				       NULL,	/* buffer in */
				       0,	/* size of input buffer */
				       (guint8 *) buffer,
				       sizeof(buffer),	/* size of output buffer */
				       NULL,	/* cancellable */
				       error);
	if (!ret)
		goto out;

	/* parse */
	*major = GUINT16_FROM_LE (buffer[0]);
	*minor = GUINT16_FROM_LE (buffer[1]);
	*micro = GUINT16_FROM_LE (buffer[2]);
out:
	return ret;
}

/**
 * ch_client_get_calibration:
 **/
gboolean
ch_client_get_calibration (ChClient *client,
			   guint16 calibration_index,
			   gdouble *calibration,
			   guint8 *types,
			   gchar *description,
			   GError **error)
{
	gboolean ret;
	guint8 buffer[9*4 + 1 + CH_CALIBRATION_DESCRIPTION_LEN];
	guint i;

	g_return_val_if_fail (CH_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (calibration_index < CH_CALIBRATION_MAX, FALSE);
	g_return_val_if_fail (calibration != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
	g_return_val_if_fail (client->priv->device != NULL, FALSE);

	/* hit hardware */
	ret = ch_device_write_command (client->priv->device,
				       CH_CMD_GET_CALIBRATION,
				       (guint8 *) &calibration_index,
				       sizeof(guint16),
				       (guint8 *) buffer,
				       sizeof(buffer),
				       NULL,	/* cancellable */
				       error);
	if (!ret)
		goto out;

	/* convert back into floating point */
	for (i = 0; i < 9; i++) {
		ch_packed_float_to_double ((ChPackedFloat *) &buffer[i*4],
					   &calibration[i]);
	}

	/* get the supported types */
	if (types != NULL)
		*types = buffer[9*4];

	/* get the description */
	if (description != NULL) {
		strncpy (description,
			 (const char *) buffer + 9*4 + 1,
			 CH_CALIBRATION_DESCRIPTION_LEN);
	}
out:
	return ret;
}

/**
 * ch_client_set_calibration:
 **/
gboolean
ch_client_set_calibration (ChClient *client,
			   guint16 calibration_index,
			   const gdouble *calibration,
			   guint8 types,
			   const gchar *description,
			   GError **error)
{
	gboolean ret;
	guint8 buffer[9*4 + 2 + 1 + CH_CALIBRATION_DESCRIPTION_LEN];
	guint i;

	g_return_val_if_fail (CH_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (calibration_index < CH_CALIBRATION_MAX, FALSE);
	g_return_val_if_fail (calibration != NULL, FALSE);
	g_return_val_if_fail (description != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
	g_return_val_if_fail (client->priv->device != NULL, FALSE);

	/* write index */
	memcpy (buffer, &calibration_index, sizeof(guint16));

	/* convert from float to signed value */
	for (i = 0; i < 9; i++) {
		ch_double_to_packed_float (calibration[i],
					   (ChPackedFloat *) &buffer[i*4 + 2]);
	}

	/* write types */
	buffer[9*4 + 2] = types;

	/* write description */
	strncpy ((gchar *) buffer + 9*4 + 2 + 1,
		 description,
		 CH_CALIBRATION_DESCRIPTION_LEN);

	/* hit hardware */
	ret = ch_device_write_command (client->priv->device,
				       CH_CMD_SET_CALIBRATION,
				       (guint8 *) buffer,
				       sizeof(buffer),
				       NULL,	/* buffer out */
				       0,	/* size of output buffer */
				       NULL,	/* cancellable */
				       error);
	if (!ret)
		goto out;
out:
	return ret;
}

/**
 * ch_client_clear_calibration:
 **/
gboolean
ch_client_clear_calibration (ChClient *client,
			     guint16 calibration_index,
			     GError **error)
{
	gboolean ret;
	guint8 buffer[9*4 + 2 + 1 + CH_CALIBRATION_DESCRIPTION_LEN];

	g_return_val_if_fail (CH_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (calibration_index < CH_CALIBRATION_MAX, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
	g_return_val_if_fail (client->priv->device != NULL, FALSE);

	/* write index */
	memcpy (buffer, &calibration_index, sizeof(guint16));

	/* clear data */
	memset (buffer + 2, 0xff, sizeof (buffer) - 2);

	/* hit hardware */
	ret = ch_device_write_command (client->priv->device,
				       CH_CMD_SET_CALIBRATION,
				       (guint8 *) buffer,
				       sizeof(buffer),
				       NULL,	/* buffer out */
				       0,	/* size of output buffer */
				       NULL,	/* cancellable */
				       error);
	if (!ret)
		goto out;
out:
	return ret;
}

/**
 * ch_client_get_pre_scale:
 **/
gboolean
ch_client_get_pre_scale (ChClient *client,
			 gdouble *pre_scale,
			 GError **error)
{
	gboolean ret;
	ChPackedFloat buffer;

	g_return_val_if_fail (CH_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (pre_scale != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
	g_return_val_if_fail (client->priv->device != NULL, FALSE);

	/* hit hardware */
	ret = ch_device_write_command (client->priv->device,
				       CH_CMD_GET_PRE_SCALE,
				       NULL,	/* buffer in */
				       0,	/* size of input buffer */
				       (guint8 *) &buffer,
				       sizeof(buffer),	/* size of output buffer */
				       NULL,	/* cancellable */
				       error);
	if (!ret)
		goto out;

	/* convert back into floating point */
	ch_packed_float_to_double (&buffer, pre_scale);
out:
	return ret;
}

/**
 * ch_client_set_pre_scale:
 **/
gboolean
ch_client_set_pre_scale (ChClient *client,
			 gdouble pre_scale,
			 GError **error)
{
	gboolean ret;
	ChPackedFloat buffer;

	g_return_val_if_fail (CH_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
	g_return_val_if_fail (client->priv->device != NULL, FALSE);

	/* convert from float to signed value */
	ch_double_to_packed_float (pre_scale, &buffer);

	/* hit hardware */
	ret = ch_device_write_command (client->priv->device,
				       CH_CMD_SET_PRE_SCALE,
				       (guint8 *) &buffer,
				       sizeof(buffer),
				       NULL,	/* buffer out */
				       0,	/* size of output buffer */
				       NULL,	/* cancellable */
				       error);
	if (!ret)
		goto out;
out:
	return ret;
}

/**
 * ch_client_get_post_scale:
 **/
gboolean
ch_client_get_post_scale (ChClient *client,
			  gdouble *post_scale,
			  GError **error)
{
	gboolean ret;
	ChPackedFloat buffer;

	g_return_val_if_fail (CH_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (post_scale != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
	g_return_val_if_fail (client->priv->device != NULL, FALSE);

	/* hit hardware */
	ret = ch_device_write_command (client->priv->device,
				       CH_CMD_GET_POST_SCALE,
				       NULL,	/* buffer in */
				       0,	/* size of input buffer */
				       (guint8 *) &buffer,
				       sizeof(buffer),	/* size of output buffer */
				       NULL,	/* cancellable */
				       error);
	if (!ret)
		goto out;

	/* convert back into floating point */
	ch_packed_float_to_double (&buffer, post_scale);
out:
	return ret;
}

/**
 * ch_client_set_post_scale:
 **/
gboolean
ch_client_set_post_scale (ChClient *client,
			  gdouble post_scale,
			  GError **error)
{
	gboolean ret;
	ChPackedFloat buffer;

	g_return_val_if_fail (CH_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
	g_return_val_if_fail (client->priv->device != NULL, FALSE);

	/* convert from float to signed value */
	ch_double_to_packed_float (post_scale, &buffer);

	/* hit hardware */
	ret = ch_device_write_command (client->priv->device,
				       CH_CMD_SET_POST_SCALE,
				       (guint8 *) &buffer,
				       sizeof(buffer),
				       NULL,	/* buffer out */
				       0,	/* size of output buffer */
				       NULL,	/* cancellable */
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
	guint32 serial_le;

	g_return_val_if_fail (CH_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (serial_number != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
	g_return_val_if_fail (client->priv->device != NULL, FALSE);

	/* hit hardware */
	ret = ch_device_write_command (client->priv->device,
				       CH_CMD_GET_SERIAL_NUMBER,
				       NULL,	/* buffer in */
				       0,	/* size of input buffer */
				       (guint8 *) &serial_le,
				       sizeof(serial_le),	/* size of output buffer */
				       NULL,	/* cancellable */
				       error);
	if (!ret)
		goto out;
	/* XXX: 32 vs. 64 bits? */
	*serial_number = GUINT32_FROM_LE (serial_le);
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
	guint32 serial_le;

	g_return_val_if_fail (CH_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (serial_number > 0, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
	g_return_val_if_fail (client->priv->device != NULL, FALSE);

	serial_le = GUINT32_TO_LE (serial_number);

	/* hit hardware */
	ret = ch_device_write_command (client->priv->device,
				       CH_CMD_SET_SERIAL_NUMBER,
				       (const guint8 *) &serial_le,	/* buffer in */
				       sizeof(serial_le),	/* size of input buffer */
				       NULL,
				       0,	/* size of output buffer */
				       NULL,	/* cancellable */
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
	ret = ch_device_write_command (client->priv->device,
				       CH_CMD_GET_LEDS,
				       NULL,	/* buffer in */
				       0,	/* size of input buffer */
				       (guint8 *) leds,
				       1,	/* size of output buffer */
				       NULL,	/* cancellable */
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
		    guint8 repeat,
		    guint8 on_time,
		    guint8 off_time,
		    GError **error)
{
	gboolean ret;
	guint8 buffer[4];

	g_return_val_if_fail (CH_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (leds < 0x04, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
	g_return_val_if_fail (client->priv->device != NULL, FALSE);

	/* hit hardware */
	buffer[0] = leds;
	buffer[1] = repeat;
	buffer[2] = on_time;
	buffer[3] = off_time;
	ret = ch_device_write_command (client->priv->device,
				       CH_CMD_SET_LEDS,
				       (const guint8 *) buffer,
				       sizeof (buffer),
				       NULL,	/* buffer out */
				       0,	/* size of output buffer */
				       NULL,	/* cancellable */
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
	ret = ch_device_write_command (client->priv->device,
				       CH_CMD_WRITE_EEPROM,
				       (const guint8 *) magic,	/* buffer in */
				       strlen(magic),	/* size of input buffer */
				       NULL,
				       0,	/* size of output buffer */
				       NULL,	/* cancellable */
				       error);
	if (!ret)
		goto out;
out:
	return ret;
}

/**
 * ch_client_get_dark_offsets:
 **/
gboolean
ch_client_get_dark_offsets (ChClient *client,
			    gdouble *red,
			    gdouble *green,
			    gdouble *blue,
			    GError **error)
{
	gboolean ret;
	guint16 buffer[3];

	g_return_val_if_fail (CH_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (red != NULL, FALSE);
	g_return_val_if_fail (green != NULL, FALSE);
	g_return_val_if_fail (blue != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
	g_return_val_if_fail (client->priv->device != NULL, FALSE);

	/* hit hardware */
	ret = ch_device_write_command (client->priv->device,
				       CH_CMD_GET_DARK_OFFSETS,
				       NULL,	/* buffer in */
				       0,	/* size of input buffer */
				       (guint8 *) buffer,
				       sizeof(buffer),	/* size of output buffer */
				       NULL,	/* cancellable */
				       error);
	if (!ret)
		goto out;

	/* convert back into floating point */
	*red = (gdouble) buffer[0] / (gdouble) 0xffff;
	*green = (gdouble) buffer[1] / (gdouble) 0xffff;
	*blue = (gdouble) buffer[2] / (gdouble) 0xffff;
out:
	return ret;
}

/**
 * ch_client_set_dark_offsets:
 **/
gboolean
ch_client_set_dark_offsets (ChClient *client,
			    gdouble red,
			    gdouble green,
			    gdouble blue,
			    GError **error)
{
	gboolean ret;
	guint16 buffer[3];

	g_return_val_if_fail (CH_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
	g_return_val_if_fail (client->priv->device != NULL, FALSE);

	/* hit hardware */
	buffer[0] = red * (gdouble) 0xffff;
	buffer[1] = green * (gdouble) 0xffff;
	buffer[2] = blue * (gdouble) 0xffff;
	ret = ch_device_write_command (client->priv->device,
				       CH_CMD_SET_DARK_OFFSETS,
				       (const guint8 *) buffer,	/* buffer in */
				       sizeof(buffer),	/* size of input buffer */
				       NULL,
				       0,	/* size of output buffer */
				       NULL,	/* cancellable */
				       error);
	if (!ret)
		goto out;
out:
	return ret;
}

/**
 * ch_client_take_reading_raw:
 **/
gboolean
ch_client_take_reading_raw (ChClient *client,
			    guint16 *take_reading,
			    GError **error)
{
	gboolean ret;
	guint16 reading_le;

	g_return_val_if_fail (CH_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (take_reading != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
	g_return_val_if_fail (client->priv->device != NULL, FALSE);

	/* hit hardware */
	ret = ch_device_write_command (client->priv->device,
				       CH_CMD_TAKE_READING_RAW,
				       NULL,	/* buffer in */
				       0,	/* size of input buffer */
				       (guint8 *) &reading_le,
				       sizeof(reading_le),	/* size of output buffer */
				       NULL,	/* cancellable */
				       error);
	if (!ret)
		goto out;

	*take_reading = GUINT16_FROM_LE (reading_le);
out:
	return ret;
}

/**
 * ch_client_take_readings:
 **/
gboolean
ch_client_take_readings (ChClient *client,
			 gdouble *red,
			 gdouble *green,
			 gdouble *blue,
			 GError **error)
{
	gboolean ret;
	ChPackedFloat buffer[3];

	g_return_val_if_fail (CH_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (red != NULL, FALSE);
	g_return_val_if_fail (green != NULL, FALSE);
	g_return_val_if_fail (blue != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
	g_return_val_if_fail (client->priv->device != NULL, FALSE);

	/* hit hardware */
	ret = ch_device_write_command (client->priv->device,
				       CH_CMD_TAKE_READINGS,
				       NULL,	/* buffer in */
				       0,	/* size of input buffer */
				       (guint8 *) buffer,
				       sizeof(buffer),	/* size of output buffer */
				       NULL,	/* cancellable */
				       error);
	if (!ret)
		goto out;

	/* convert back into floating point */
	ch_packed_float_to_double (&buffer[0], red);
	ch_packed_float_to_double (&buffer[1], green);
	ch_packed_float_to_double (&buffer[2], blue);
out:
	return ret;
}

/**
 * ch_client_take_readings_xyz:
 **/
gboolean
ch_client_take_readings_xyz (ChClient *client,
			     guint16 calibration_index,
			     gdouble *red,
			     gdouble *green,
			     gdouble *blue,
			     GError **error)
{
	gboolean ret;
	ChPackedFloat buffer[3];

	g_return_val_if_fail (CH_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (red != NULL, FALSE);
	g_return_val_if_fail (green != NULL, FALSE);
	g_return_val_if_fail (blue != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
	g_return_val_if_fail (client->priv->device != NULL, FALSE);

	/* hit hardware */
	ret = ch_device_write_command (client->priv->device,
				       CH_CMD_TAKE_READING_XYZ,
				       (guint8 *) &calibration_index,
				       sizeof(guint16),
				       (guint8 *) buffer,
				       sizeof(buffer),
				       NULL,	/* cancellable */
				       error);
	if (!ret)
		goto out;

	/* convert back into floating point */
	ch_packed_float_to_double (&buffer[0], red);
	ch_packed_float_to_double (&buffer[1], green);
	ch_packed_float_to_double (&buffer[2], blue);
out:
	return ret;
}

/**
 * ch_client_reset:
 **/
gboolean
ch_client_reset (ChClient *client,
		 GError **error)
{
	gboolean ret;

	g_return_val_if_fail (CH_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
	g_return_val_if_fail (client->priv->device != NULL, FALSE);

	/* hit hardware */
	ret = ch_device_write_command (client->priv->device,
				       CH_CMD_RESET,
				       NULL,	/* buffer in */
				       0,	/* size of input buffer */
				       NULL,	/* buffer out */
				       0,	/* size of output buffer */
				       NULL,	/* cancellable */
				       error);
	return ret;
}

/**
 * ch_client_calculate_checksum:
 **/
static guint8
ch_client_calculate_checksum (guint8 *data,
			      gsize len)
{
	guint8 checksum = 0xff;
	guint i;
	for (i = 0; i < len; i++)
		checksum ^= data[i];
	return checksum;
}

/**
 * ch_client_write_flash:
 **/
static gboolean
ch_client_write_flash (ChClient *client,
		       guint16 address,
		       guint8 *data,
		       gsize len,
		       GError **error)
{
	gboolean ret;
	guint16 addr_le;
	guint8 buffer_tx[64];

	/* set address, length, checksum, data */
	addr_le = GUINT16_TO_LE (address);
	memcpy (buffer_tx + 0, &addr_le, 2);
	buffer_tx[2] = len;
	buffer_tx[3] = ch_client_calculate_checksum (data, len);
	memcpy (buffer_tx + 4, data, len);

	/* hit hardware */
	ret = ch_device_write_command (client->priv->device,
				       CH_CMD_WRITE_FLASH,
				       buffer_tx,
				       len + 4,
				       NULL,	/* buffer out */
				       0,	/* size of output buffer */
				       NULL,	/* cancellable */
				       error);
	return ret;
}

/**
 * ch_client_read_flash:
 **/
static gboolean
ch_client_read_flash (ChClient *client,
		      guint16 address,
		      guint8 *data,
		      gsize len,
		      GError **error)
{
	gboolean ret;
	guint8 buffer_rx[64];
	guint8 buffer_tx[3];
	guint8 expected_checksum;
	guint16 addr_le;

	/* set address, length, checksum, data */
	addr_le = GUINT16_TO_LE (address);
	memcpy (buffer_tx + 0, &addr_le, 2);
	buffer_tx[2] = len;

	/* hit hardware */
	ret = ch_device_write_command (client->priv->device,
				       CH_CMD_READ_FLASH,
				       buffer_tx,
				       sizeof(buffer_tx),
				       buffer_rx,
				       len + 1,
				       NULL,	/* cancellable */
				       error);
	if (!ret)
		goto out;

	/* verify checksum */
	expected_checksum = ch_client_calculate_checksum (buffer_rx + 1, len);
	if (buffer_rx[0] != expected_checksum) {
		ret = FALSE;
		g_set_error (error, 1, 0,
			     "Checksum @0x%04x invalid",
			     address);
		goto out;
	}
	memcpy (data, buffer_rx + 1, len);
out:
	return ret;
}

/**
 * ch_client_erase_flash:
 **/
static gboolean
ch_client_erase_flash (ChClient *client,
		       guint16 address,
		       gsize len,
		       GError **error)
{
	gboolean ret;
	guint8 buffer_tx[4];
	guint16 addr_le;
	guint16 len_le;

	/* set address, length, checksum, data */
	addr_le = GUINT16_TO_LE (address);
	memcpy (buffer_tx + 0, &addr_le, 2);
	len_le = GUINT16_TO_LE (len);
	memcpy (buffer_tx + 2, &len_le, 2);

	/* hit hardware */
	ret = ch_device_write_command (client->priv->device,
				       CH_CMD_ERASE_FLASH,
				       buffer_tx,
				       sizeof(buffer_tx),
				       NULL,
				       0,
				       NULL,	/* cancellable */
				       error);
	if (!ret)
		goto out;
out:
	return ret;
}

/**
 * ch_client_set_flash_success:
 **/
gboolean
ch_client_set_flash_success (ChClient *client,
			     gboolean value,
			     GError **error)
{
	gboolean ret;

	g_return_val_if_fail (CH_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
	g_return_val_if_fail (client->priv->device != NULL, FALSE);

	/* set flash success true */
	ret = ch_device_write_command (client->priv->device,
				       CH_CMD_SET_FLASH_SUCCESS,
				       (guint8 *) &value, 1,
				       NULL, 0,
				       NULL,	/* cancellable */
				       error);
	if (!ret)
		goto out;
out:
	return ret;
}

/**
 * ch_client_boot_flash:
 **/
gboolean
ch_client_boot_flash (ChClient *client,
		      GError **error)
{
	gboolean ret;

	g_return_val_if_fail (CH_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
	g_return_val_if_fail (client->priv->device != NULL, FALSE);

	/* boot into new code */
	ret = ch_device_write_command (client->priv->device,
				       CH_CMD_BOOT_FLASH,
				       NULL, 0,
				       NULL, 0,
				       NULL,	/* cancellable */
				       error);
	if (!ret)
		goto out;
out:
	return ret;
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

	g_return_val_if_fail (CH_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (filename != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
	g_return_val_if_fail (client->priv->device != NULL, FALSE);

	/* load file */
	ret = g_file_get_contents (filename, &data, &len, error);
	if (!ret)
		goto out;

	/* boot to bootloader */
	ret = ch_client_reset (client, error);
	if (!ret)
		goto out;

	/* wait for the device to reconnect */
	g_object_unref (client->priv->device);
	g_usleep (1 * G_USEC_PER_SEC);
	ret = ch_client_load (client, error);
	if (!ret)
		goto out;

	/* set flash success false */
	flash_success = 0x00;
	ret = ch_device_write_command (client->priv->device,
				       CH_CMD_SET_FLASH_SUCCESS,
				       &flash_success, 1,
				       NULL, 0,
				       NULL,	/* cancellable */
				       error);
	if (!ret)
		goto out;

	/* erase flash */
	ret = ch_client_erase_flash (client,
				     CH_EEPROM_ADDR_RUNCODE,
				     len,
				     error);
	if (!ret)
		goto out;

	/* write in 32 byte chunks */
	idx = 0;
	chunk_len = CH_FLASH_TRANSFER_BLOCK_SIZE;
	do {
		if (idx + chunk_len > len)
			chunk_len = len - idx;
		g_debug ("Writing at %04x size %li",
			 CH_EEPROM_ADDR_RUNCODE + idx,
			 chunk_len);
		ret = ch_client_write_flash (client,
					     CH_EEPROM_ADDR_RUNCODE + idx,
					     (guint8 *) data + idx,
					     chunk_len,
					     error);
		if (!ret)
			goto out;
		idx += chunk_len;
	} while (idx < len);

	/* flush to 64 byte chunk */
	if ((idx & CH_FLASH_TRANSFER_BLOCK_SIZE) == 0) {
		idx -= chunk_len;
		idx += CH_FLASH_TRANSFER_BLOCK_SIZE;
		g_debug ("Flushing at %04x",
			 CH_EEPROM_ADDR_RUNCODE + idx);
		ret = ch_client_write_flash (client,
					     CH_EEPROM_ADDR_RUNCODE + idx,
					     (guint8 *) data,
					     0,
					     error);
		if (!ret)
			goto out;

	}

	/* read in 60 byte chunks */
	idx = 0;
	chunk_len = 60;
	do {
		if (idx + chunk_len > len)
			chunk_len = len - idx;
		g_debug ("Reading at %04x size %li",
			 CH_EEPROM_ADDR_RUNCODE + idx,
			 chunk_len);
		ret = ch_client_read_flash (client,
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
	ret = ch_device_write_command (client->priv->device,
				       CH_CMD_BOOT_FLASH,
				       NULL, 0,
				       NULL, 0,
				       NULL,	/* cancellable */
				       error);
	if (!ret)
		goto out;

	/* wait for the device to reconnect */
	g_object_unref (client->priv->device);
	g_usleep (1 * G_USEC_PER_SEC);
	ret = ch_client_load (client, error);
	if (!ret)
		goto out;

	/* set flash success true */
	flash_success = 0x01;
	ret = ch_device_write_command (client->priv->device,
				       CH_CMD_SET_FLASH_SUCCESS,
				       &flash_success, 1,
				       NULL, 0,
				       NULL,	/* cancellable */
				       error);
	if (!ret)
		goto out;
out:
	g_free (data);
	return ret;
}

/**
 * ch_client_get_hardware_version:
 **/
gboolean
ch_client_get_hardware_version (ChClient *client,
				guint8 *hw_version,
				GError **error)
{
	gboolean ret;

	g_return_val_if_fail (CH_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (hw_version != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
	g_return_val_if_fail (client->priv->device != NULL, FALSE);

	/* hit hardware */
	ret = ch_device_write_command (client->priv->device,
				       CH_CMD_GET_HARDWARE_VERSION,
				       NULL,	/* buffer in */
				       0,	/* size of input buffer */
				       hw_version,
				       1,	/* size of output buffer */
				       NULL,	/* cancellable */
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

