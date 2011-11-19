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
 * ch_client_strerror:
 **/
static const gchar *
ch_client_strerror (ChFatalError fatal_error)
{
	const char *str = NULL;
	switch (fatal_error) {
	case CH_FATAL_ERROR_NONE:
		str = "Success";
		break;
	case CH_FATAL_ERROR_UNKNOWN_CMD:
		str = "Unknown command";
		break;
	case CH_FATAL_ERROR_WRONG_UNLOCK_CODE:
		str = "Wrong unlock code";
		break;
	case CH_FATAL_ERROR_NOT_IMPLEMENTED:
		str = "Not implemented";
		break;
	case CH_FATAL_ERROR_UNDERFLOW_SENSOR:
		str = "Underflow of sensor";
		break;
	case CH_FATAL_ERROR_NO_SERIAL:
		str = "No serial";
		break;
	case CH_FATAL_ERROR_WATCHDOG:
		str = "Watchdog";
		break;
	case CH_FATAL_ERROR_INVALID_ADDRESS:
		str = "Invalid address";
		break;
	case CH_FATAL_ERROR_INVALID_LENGTH:
		str = "Invalid length";
		break;
	case CH_FATAL_ERROR_INVALID_CHECKSUM:
		str = "Invalid checksum";
		break;
	case CH_FATAL_ERROR_INVALID_VALUE:
		str = "Invalid value";
		break;
	case CH_FATAL_ERROR_UNKNOWN_CMD_FOR_BOOTLOADER:
		str = "Unknown command for bootloader";
		break;
	case CH_FATAL_ERROR_OVERFLOW_MULTIPLY:
		str = "Overflow of multiply";
		break;
	case CH_FATAL_ERROR_OVERFLOW_ADDITION:
		str = "Overflow of addition";
		break;
	case CH_FATAL_ERROR_OVERFLOW_SENSOR:
		str = "Overflow of sensor";
		break;
	default:
		str = "Unknown error, please report";
		break;
	}
	return str;
}

/**
 * ch_client_command_to_string:
 **/
static const gchar *
ch_client_command_to_string (guint8 cmd)
{
	const char *str = NULL;
	switch (cmd) {
	case CH_CMD_GET_COLOR_SELECT:
		str = "get-color-select";
		break;
	case CH_CMD_SET_COLOR_SELECT:
		str = "set-color-select";
		break;
	case CH_CMD_GET_MULTIPLIER:
		str = "get-multiplier";
		break;
	case CH_CMD_SET_MULTIPLIER:
		str = "set-multiplier";
		break;
	case CH_CMD_GET_INTEGRAL_TIME:
		str = "get-integral-time";
		break;
	case CH_CMD_SET_INTEGRAL_TIME:
		str = "set-integral-time";
		break;
	case CH_CMD_GET_FIRMWARE_VERSION:
		str = "get-firmare-version";
		break;
	case CH_CMD_GET_CALIBRATION:
		str = "get-calibration";
		break;
	case CH_CMD_SET_CALIBRATION:
		str = "set-calibration";
		break;
	case CH_CMD_GET_SERIAL_NUMBER:
		str = "get-serial-number";
		break;
	case CH_CMD_SET_SERIAL_NUMBER:
		str = "set-serial-number";
		break;
	case CH_CMD_GET_LEDS:
		str = "get-leds";
		break;
	case CH_CMD_SET_LEDS:
		str = "set-leds";
		break;
	case CH_CMD_GET_DARK_OFFSETS:
		str = "get-dark-offsets";
		break;
	case CH_CMD_SET_DARK_OFFSETS:
		str = "set-dark-offsets";
		break;
	case CH_CMD_WRITE_EEPROM:
		str = "write-eeprom";
		break;
	case CH_CMD_TAKE_READING_RAW:
		str = "take-reading-raw";
		break;
	case CH_CMD_TAKE_READINGS:
		str = "take-readings";
		break;
	case CH_CMD_TAKE_READING_XYZ:
		str = "take-reading-xyz";
		break;
	case CH_CMD_RESET:
		str = "reset";
		break;
	case CH_CMD_READ_FLASH:
		str = "read-flash";
		break;
	case CH_CMD_ERASE_FLASH:
		str = "erase-flash";
		break;
	case CH_CMD_WRITE_FLASH:
		str = "write-flash";
		break;
	case CH_CMD_BOOT_FLASH:
		str = "boot-flash";
		break;
	case CH_CMD_SET_FLASH_SUCCESS:
		str = "set-flash-success";
		break;
	default:
		str = "unknown-command";
		break;
	}
	return str;
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
	ChFatalError fatal_error;
	guint8 buffer[CH_USB_HID_EP_SIZE];

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

	/* clear buffer for debugging */
	memset (buffer, 0xff, sizeof (buffer));

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
		fatal_error = buffer[CH_BUFFER_OUTPUT_RETVAL];
		g_set_error (error, 1, 0,
			     "Invalid read: retval=0x%02x [%s] "
			     "cmd=0x%02x (expected 0x%x [%s]) "
			     "len=%li (expected %li)",
			     fatal_error,
			     ch_client_strerror (fatal_error),
			     buffer[CH_BUFFER_OUTPUT_CMD],
			     cmd,
			     ch_client_command_to_string (cmd),
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
	guint8 csel8 = color_select;

	g_return_val_if_fail (CH_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
	g_return_val_if_fail (client->priv->device != NULL, FALSE);

	/* hit hardware */
	ret = ch_client_write_command (client,
				       CH_CMD_SET_COLOR_SELECT,
				       &csel8,	/* buffer in */
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
	guint8 mult8 = multiplier;

	g_return_val_if_fail (CH_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
	g_return_val_if_fail (client->priv->device != NULL, FALSE);

	/* hit hardware */
	ret = ch_client_write_command (client,
				       CH_CMD_SET_MULTIPLIER,
				       &mult8,	/* buffer in */
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
	guint16 integral_le;

	g_return_val_if_fail (CH_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (integral_time != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
	g_return_val_if_fail (client->priv->device != NULL, FALSE);

	/* hit hardware */
	ret = ch_client_write_command (client,
				       CH_CMD_GET_INTEGRAL_TIME,
				       NULL,	/* buffer in */
				       0,	/* size of input buffer */
				       (guint8 *) &integral_le,
				       2,	/* size of output buffer */
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
	ret = ch_client_write_command (client,
				       CH_CMD_SET_INTEGRAL_TIME,
				       (const guint8 *) &integral_le,	/* buffer in */
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
			   gdouble *calibration,
			   GError **error)
{
	gboolean ret;
	ChPackedFloat buffer[9];
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
				       (guint8 *) buffer,
				       sizeof(buffer),	/* size of output buffer */
				       error);
	if (!ret)
		goto out;

	/* convert back into floating point */
	for (i = 0; i < 9; i++)
		ch_packed_float_to_double (&buffer[i], &calibration[i]);
out:
	return ret;
}

/**
 * ch_client_set_calibration:
 **/
gboolean
ch_client_set_calibration (ChClient *client,
			   const gdouble *calibration,
			   GError **error)
{
	gboolean ret;
	ChPackedFloat buffer[9];
	guint i;

	g_return_val_if_fail (CH_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (calibration != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
	g_return_val_if_fail (client->priv->device != NULL, FALSE);

	/* convert from float to signed value */
	for (i = 0; i < 9; i++)
		ch_double_to_packed_float (calibration[i], &buffer[i]);

	/* hit hardware */
	ret = ch_client_write_command (client,
				       CH_CMD_SET_CALIBRATION,
				       (guint8 *) buffer,
				       sizeof(buffer),
				       NULL,	/* buffer out */
				       0,	/* size of output buffer */
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
	ret = ch_client_write_command (client,
				       CH_CMD_GET_POST_SCALE,
				       NULL,	/* buffer in */
				       0,	/* size of input buffer */
				       (guint8 *) &buffer,
				       sizeof(buffer),	/* size of output buffer */
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
	g_return_val_if_fail (post_scale > -20.0f, FALSE);
	g_return_val_if_fail (post_scale < 20.0f, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
	g_return_val_if_fail (client->priv->device != NULL, FALSE);

	/* convert from float to signed value */
	ch_double_to_packed_float (post_scale, &buffer);

	/* hit hardware */
	ret = ch_client_write_command (client,
				       CH_CMD_SET_POST_SCALE,
				       (guint8 *) &buffer,
				       sizeof(buffer),
				       NULL,	/* buffer out */
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
	guint32 serial_le;

	g_return_val_if_fail (CH_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (serial_number != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
	g_return_val_if_fail (client->priv->device != NULL, FALSE);

	/* hit hardware */
	ret = ch_client_write_command (client,
				       CH_CMD_GET_SERIAL_NUMBER,
				       NULL,	/* buffer in */
				       0,	/* size of input buffer */
				       (guint8 *) &serial_le,
				       sizeof(serial_le),	/* size of output buffer */
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
	ret = ch_client_write_command (client,
				       CH_CMD_SET_SERIAL_NUMBER,
				       (const guint8 *) &serial_le,	/* buffer in */
				       sizeof(serial_le),	/* size of input buffer */
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
	ret = ch_client_write_command (client,
				       CH_CMD_SET_LEDS,
				       (const guint8 *) buffer,
				       sizeof (buffer),
				       NULL,	/* buffer out */
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
	ret = ch_client_write_command (client,
				       CH_CMD_GET_DARK_OFFSETS,
				       NULL,	/* buffer in */
				       0,	/* size of input buffer */
				       (guint8 *) buffer,
				       sizeof(buffer),	/* size of output buffer */
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
	ret = ch_client_write_command (client,
				       CH_CMD_SET_DARK_OFFSETS,
				       (const guint8 *) buffer,	/* buffer in */
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
	ret = ch_client_write_command (client,
				       CH_CMD_TAKE_READING_RAW,
				       NULL,	/* buffer in */
				       0,	/* size of input buffer */
				       (guint8 *) &reading_le,
				       sizeof(reading_le),	/* size of output buffer */
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
	ret = ch_client_write_command (client,
				       CH_CMD_TAKE_READINGS,
				       NULL,	/* buffer in */
				       0,	/* size of input buffer */
				       (guint8 *) buffer,
				       sizeof(buffer),	/* size of output buffer */
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
	ret = ch_client_write_command (client,
				       CH_CMD_TAKE_READING_XYZ,
				       NULL,	/* buffer in */
				       0,	/* size of input buffer */
				       (guint8 *) buffer,
				       sizeof(buffer),	/* size of output buffer */
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
	ret = ch_client_write_command (client,
				       CH_CMD_RESET,
				       NULL,	/* buffer in */
				       0,	/* size of input buffer */
				       NULL,	/* buffer out */
				       0,	/* size of output buffer */
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
	ret = ch_client_write_command (client,
				       CH_CMD_WRITE_FLASH,
				       buffer_tx,
				       len + 4,
				       NULL,	/* buffer out */
				       0,	/* size of output buffer */
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
	ret = ch_client_write_command (client,
				       CH_CMD_READ_FLASH,
				       buffer_tx,
				       sizeof(buffer_tx),
				       buffer_rx,
				       len + 1,
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
	guint8 buffer_tx[3];
	guint16 addr_le;

	/* set address, length, checksum, data */
	addr_le = GUINT16_TO_LE (address);
	memcpy (buffer_tx + 0, &addr_le, 2);
	buffer_tx[2] = len;

	/* hit hardware */
	ret = ch_client_write_command (client,
				       CH_CMD_ERASE_FLASH,
				       buffer_tx,
				       sizeof(buffer_tx),
				       NULL,
				       0,
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
	ret = ch_client_write_command (client,
				       CH_CMD_SET_FLASH_SUCCESS,
				       &flash_success, 1,
				       NULL, 0,
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
			ch_client_print_data ("expected",
					      (const guint8*) &data[idx],
					      chunk_len);
			ch_client_print_data ("got     ",
					      buffer,
					      chunk_len);
			goto out;
		}
		idx += chunk_len;
	} while (idx < len);

	/* boot into new code */
	ret = ch_client_write_command (client,
				       CH_CMD_BOOT_FLASH,
				       NULL, 0,
				       NULL, 0,
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
	ret = ch_client_write_command (client,
				       CH_CMD_SET_FLASH_SUCCESS,
				       &flash_success, 1,
				       NULL, 0,
				       error);
	if (!ret)
		goto out;
out:
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

