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

#include <glib.h>
#include <gusb.h>
#include <string.h>
#include <lcms2.h>

#include "ch-common.h"
#include "ch-math.h"

/**
 * ch_strerror:
 **/
const gchar *
ch_strerror (ChError error_enum)
{
	const char *str = NULL;
	switch (error_enum) {
	case CH_ERROR_NONE:
		str = "Success";
		break;
	case CH_ERROR_UNKNOWN_CMD:
		str = "Unknown command";
		break;
	case CH_ERROR_WRONG_UNLOCK_CODE:
		str = "Wrong unlock code";
		break;
	case CH_ERROR_NOT_IMPLEMENTED:
		str = "Not implemented";
		break;
	case CH_ERROR_UNDERFLOW_SENSOR:
		str = "Underflow of sensor";
		break;
	case CH_ERROR_NO_SERIAL:
		str = "No serial";
		break;
	case CH_ERROR_WATCHDOG:
		str = "Watchdog";
		break;
	case CH_ERROR_INVALID_ADDRESS:
		str = "Invalid address";
		break;
	case CH_ERROR_INVALID_LENGTH:
		str = "Invalid length";
		break;
	case CH_ERROR_INVALID_CHECKSUM:
		str = "Invalid checksum";
		break;
	case CH_ERROR_INVALID_VALUE:
		str = "Invalid value";
		break;
	case CH_ERROR_UNKNOWN_CMD_FOR_BOOTLOADER:
		str = "Unknown command for bootloader";
		break;
	case CH_ERROR_OVERFLOW_MULTIPLY:
		str = "Overflow of multiply";
		break;
	case CH_ERROR_OVERFLOW_ADDITION:
		str = "Overflow of addition";
		break;
	case CH_ERROR_OVERFLOW_SENSOR:
		str = "Overflow of sensor";
		break;
	case CH_ERROR_OVERFLOW_STACK:
		str = "Overflow of stack";
		break;
	case CH_ERROR_NO_CALIBRATION:
		str = "No calibration";
		break;
	case CH_ERROR_DEVICE_DEACTIVATED:
		str = "Device deactivated";
		break;
	default:
		str = "Unknown error, please report";
		break;
	}
	return str;
}

/**
 * ch_command_to_string:
 **/
const gchar *
ch_command_to_string (guint8 cmd)
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
	case CH_CMD_GET_CALIBRATION_MAP:
		str = "get-calibration-map";
		break;
	case CH_CMD_SET_CALIBRATION_MAP:
		str = "set-calibration-map";
		break;
	case CH_CMD_GET_HARDWARE_VERSION:
		str = "get-hardware-version";
		break;
	default:
		str = "unknown-command";
		break;
	}
	return str;
}

/**********************************************************************/

/**
 * ch_print_data_buffer:
 **/
static void
ch_print_data_buffer (const gchar *title,
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

/* the default timeout */
#define CH_DEVICE_USB_TIMEOUT	2000

typedef struct {
	GUsbDevice		*device;
	GCancellable		*cancellable;
	GSimpleAsyncResult	*res;
	guint8			*buffer;
	guint8			*buffer_out;
	gsize			 buffer_out_len;
	guint8			 cmd;
} ChDeviceHelper;

/**
 * ch_device_write_command_finish:
 * @device: a #GUsbDevice instance.
 * @res: the #GAsyncResult
 * @error: A #GError or %NULL
 *
 * Gets the result from the asynchronous function.
 *
 * Return value: %TRUE if the request was fulfilled.
 **/
gboolean
ch_device_write_command_finish (GUsbDevice *device,
				GAsyncResult *res,
				GError **error)
{
	GSimpleAsyncResult *simple;

	g_return_val_if_fail (G_USB_IS_DEVICE (device), FALSE);
	g_return_val_if_fail (G_IS_SIMPLE_ASYNC_RESULT (res), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (res);
	if (g_simple_async_result_propagate_error (simple, error))
		return FALSE;

	return g_simple_async_result_get_op_res_gboolean (simple);
}

/**
 * ch_device_free_helper:
 **/
static void
ch_device_free_helper (ChDeviceHelper *helper)
{
	/* clear busy flag */
	g_object_steal_data (G_OBJECT (helper->device),
			     "ChCommonDeviceBusy");
	if (helper->cancellable != NULL)
		g_object_unref (helper->cancellable);
	g_object_unref (helper->device);
	g_object_unref (helper->res);
	g_free (helper->buffer);
	g_free (helper);
}

/**
 * ch_device_reply_cb:
 **/
static void
ch_device_reply_cb (GObject *source_object,
		    GAsyncResult *res,
		    gpointer user_data)
{
	ChError error_enum;
	GError *error = NULL;
	gsize actual_len;
	gchar *msg = NULL;
	GUsbDevice *device = G_USB_DEVICE (source_object);
	ChDeviceHelper *helper = (ChDeviceHelper *) user_data;

	/* get the result */
	actual_len = g_usb_device_interrupt_transfer_finish (device,
							     res,
							     &error);
	if ((gssize) actual_len < 0) {
		g_simple_async_result_take_error (helper->res, error);
		g_simple_async_result_complete_in_idle (helper->res);
		ch_device_free_helper (helper);
		return;
	}

	/* parse the reply */
	if (g_getenv ("COLORHUG_VERBOSE") != NULL) {
		ch_print_data_buffer ("reply",
				      helper->buffer,
				      actual_len);
	}

	/* parse */
	if (helper->buffer[CH_BUFFER_OUTPUT_RETVAL] != CH_ERROR_NONE ||
	    helper->buffer[CH_BUFFER_OUTPUT_CMD] != helper->cmd ||
	    actual_len != helper->buffer_out_len + CH_BUFFER_OUTPUT_DATA) {
		error_enum = helper->buffer[CH_BUFFER_OUTPUT_RETVAL];
		msg = g_strdup_printf ("Invalid read: retval=0x%02x [%s] "
				       "cmd=0x%02x (expected 0x%x [%s]) "
				       "len=%li (expected %li)",
				       error_enum,
				       ch_strerror (error_enum),
				       helper->buffer[CH_BUFFER_OUTPUT_CMD],
				       helper->cmd,
				       ch_command_to_string (helper->cmd),
				       actual_len,
				       helper->buffer_out_len + CH_BUFFER_OUTPUT_DATA);
		g_simple_async_result_set_error (helper->res, 1, 0, "%s", msg);
		g_simple_async_result_complete_in_idle (helper->res);
		ch_device_free_helper (helper);
		return;
	}

	/* copy */
	if (helper->buffer_out != NULL) {
		memcpy (helper->buffer_out,
			helper->buffer + CH_BUFFER_OUTPUT_DATA,
			helper->buffer_out_len);
	}

	/* success */
	g_simple_async_result_set_op_res_gboolean (helper->res, TRUE);
	g_simple_async_result_complete_in_idle (helper->res);
	ch_device_free_helper (helper);
}

/**
 * ch_device_request_cb:
 **/
static void
ch_device_request_cb (GObject *source_object,
		      GAsyncResult *res,
		      gpointer user_data)
{
	GError *error = NULL;
	gssize actual_len;
	GUsbDevice *device = G_USB_DEVICE (source_object);
	ChDeviceHelper *helper = (ChDeviceHelper *) user_data;

	/* get the result */
	actual_len = g_usb_device_interrupt_transfer_finish (device,
							     res,
							     &error);
	if (actual_len < CH_USB_HID_EP_SIZE) {
		g_simple_async_result_take_error (helper->res, error);
		g_simple_async_result_complete_in_idle (helper->res);
		ch_device_free_helper (helper);
		return;
	}

	/* request the reply */
	g_usb_device_interrupt_transfer_async (helper->device,
					       CH_USB_HID_EP_IN,
					       helper->buffer,
					       CH_USB_HID_EP_SIZE,
					       CH_DEVICE_USB_TIMEOUT,
					       helper->cancellable,
					       ch_device_reply_cb,
					       helper);
}

/**
 * ch_device_emulate_cb:
 **/
static gboolean
ch_device_emulate_cb (gpointer user_data)
{
	ChDeviceHelper *helper = (ChDeviceHelper *) user_data;

	switch (helper->cmd) {
	case CH_CMD_GET_SERIAL_NUMBER:
		helper->buffer_out[6] = 42;
		break;
	case CH_CMD_GET_FIRMWARE_VERSION:
		helper->buffer_out[0] = 0x01;
		helper->buffer_out[4] = 0x01;
		break;
	case CH_CMD_GET_HARDWARE_VERSION:
		helper->buffer_out[0] = 0xff;
		break;
	default:
		g_debug ("Ignoring command %s",
			 ch_command_to_string (helper->cmd));
		break;
	}

	/* success */
	g_simple_async_result_set_op_res_gboolean (helper->res, TRUE);
	g_simple_async_result_complete_in_idle (helper->res);
	ch_device_free_helper (helper);

	return FALSE;
}

/**
 * ch_device_write_command_async:
 * @device:		A #GUsbDevice
 * @cmd:		The command to use, e.g. %CH_CMD_GET_COLOR_SELECT
 * @buffer_in:		The input buffer of data, or %NULL
 * @buffer_in_len:	The input buffer length
 * @buffer_out:		The output buffer of data, or %NULL
 * @buffer_out_len:	The output buffer length
 * @cancellable:	A #GCancellable, or %NULL
 * @callback:		A #GAsyncReadyCallback that will be called when finished.
 * @user_data:		User data passed to @callback
 *
 * Sends a message to the device and waits for a reply.
 **/
void
ch_device_write_command_async (GUsbDevice *device,
			       guint8 cmd,
			       const guint8 *buffer_in,
			       gsize buffer_in_len,
			       guint8 *buffer_out,
			       gsize buffer_out_len,
			       GCancellable *cancellable,
			       GAsyncReadyCallback callback,
			       gpointer user_data)
{
	ChDeviceHelper *helper;
	gpointer device_busy;

	g_return_if_fail (G_USB_IS_DEVICE (device));
	g_return_if_fail (cmd != 0);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	helper = g_new0 (ChDeviceHelper, 1);
	helper->device = g_object_ref (device);
	helper->buffer_out = buffer_out;
	helper->buffer_out_len = buffer_out_len;
	helper->buffer = g_new0 (guint8, CH_USB_HID_EP_SIZE);
	helper->res = g_simple_async_result_new (G_OBJECT (device),
						 callback,
						 user_data,
						 ch_device_write_command_async);
	if (cancellable != NULL)
		helper->cancellable = g_object_ref (cancellable);

	/* device busy processing another command */
	device_busy = g_object_get_data (G_OBJECT (device),
					 "ChCommonDeviceBusy");
	if (device_busy != NULL) {
		g_simple_async_result_set_error (helper->res, 1, 0, "Device busy!");
		g_simple_async_result_complete_in_idle (helper->res);
		ch_device_free_helper (helper);
		return;
	}

	/* set command */
	helper->cmd = cmd;
	helper->buffer[CH_BUFFER_INPUT_CMD] = helper->cmd;
	if (buffer_in != NULL) {
		memcpy (helper->buffer + CH_BUFFER_INPUT_DATA,
			buffer_in,
			buffer_in_len);
	}

	/* request */
	if (g_getenv ("COLORHUG_VERBOSE") != NULL) {
		ch_print_data_buffer ("request",
				      helper->buffer,
				      buffer_in_len + 1);
	}

	/* dummy hardware */
	if (g_getenv ("COLORHUG_EMULATE") != NULL) {
		g_timeout_add (20, ch_device_emulate_cb, helper);
		return;
	}

	/* set a private flag so we don't do reentrancy */
	g_object_set_data (G_OBJECT (device),
			   "ChCommonDeviceBusy",
			   GUINT_TO_POINTER (TRUE));

	/* do interrupt transfer */
	g_usb_device_interrupt_transfer_async (helper->device,
					       CH_USB_HID_EP_OUT,
					       helper->buffer,
					       CH_USB_HID_EP_SIZE,
					       CH_DEVICE_USB_TIMEOUT,
					       helper->cancellable,
					       ch_device_request_cb,
					       helper);
}

/* tiny helper to help us do the async operation */
typedef struct {
	GError		**error;
	GMainLoop	*loop;
	gboolean	 ret;
} ChDeviceSyncHelper;

/**
 * ch_device_write_command_finish_cb:
 **/
static void
ch_device_write_command_finish_cb (GObject *source,
				   GAsyncResult *res,
				   gpointer user_data)
{
	GUsbDevice *device = G_USB_DEVICE (source);
	ChDeviceSyncHelper *helper = (ChDeviceSyncHelper *) user_data;
	helper->ret = ch_device_write_command_finish (device, res, helper->error);
	g_main_loop_quit (helper->loop);
}

/**
 * ch_device_write_command:
 *
 * @client:		A #ChClient
 * @cmd:		The command to use, e.g. %CH_CMD_GET_COLOR_SELECT
 * @buffer_in:		The input buffer of data, or %NULL
 * @buffer_in_len:	The input buffer length
 * @buffer_out:		The output buffer of data, or %NULL
 * @buffer_out_len:	The output buffer length
 * @cancellable:	A #GCancellable or %NULL
 * @error:		A #GError, or %NULL
 *
 * Sends a message to the device and waits for a reply.
 *
 **/
gboolean
ch_device_write_command (GUsbDevice *device,
			 guint8 cmd,
			 const guint8 *buffer_in,
			 gsize buffer_in_len,
			 guint8 *buffer_out,
			 gsize buffer_out_len,
			 GCancellable *cancellable,
			 GError **error)
{
	ChDeviceSyncHelper helper;

	/* create temp object */
	helper.loop = g_main_loop_new (NULL, FALSE);
	helper.error = error;

	/* run async method */
	ch_device_write_command_async (device,
				       cmd,
				       buffer_in,
				       buffer_in_len,
				       buffer_out,
				       buffer_out_len,
				       cancellable,
				       ch_device_write_command_finish_cb,
				       &helper);
	g_main_loop_run (helper.loop);

	/* free temp object */
	g_main_loop_unref (helper.loop);

	return helper.ret;
}

/**********************************************************************/

/**
 * ch_device_cmd_get_color_select:
 **/
gboolean
ch_device_cmd_get_color_select (GUsbDevice *device,
				ChColorSelect *color_select,
				GError **error)
{
	gboolean ret;

	g_return_val_if_fail (G_USB_IS_DEVICE (device), FALSE);
	g_return_val_if_fail (color_select != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* hit hardware */
	ret = ch_device_write_command (device,
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
 * ch_device_cmd_set_color_select:
 **/
gboolean
ch_device_cmd_set_color_select (GUsbDevice *device,
				ChColorSelect color_select,
				GError **error)
{
	gboolean ret;
	guint8 csel8 = color_select;

	g_return_val_if_fail (G_USB_IS_DEVICE (device), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* hit hardware */
	ret = ch_device_write_command (device,
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
 * ch_device_cmd_get_multiplier:
 **/
gboolean
ch_device_cmd_get_multiplier (GUsbDevice *device,
			      ChFreqScale *multiplier,
			      GError **error)
{
	gboolean ret;

	g_return_val_if_fail (G_USB_IS_DEVICE (device), FALSE);
	g_return_val_if_fail (multiplier != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* hit hardware */
	ret = ch_device_write_command (device,
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
 * ch_device_cmd_set_multiplier:
 **/
gboolean
ch_device_cmd_set_multiplier (GUsbDevice *device,
			      ChFreqScale multiplier,
			      GError **error)
{
	gboolean ret;
	guint8 mult8 = multiplier;

	g_return_val_if_fail (G_USB_IS_DEVICE (device), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* hit hardware */
	ret = ch_device_write_command (device,
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
 * ch_device_cmd_get_integral_time:
 **/
gboolean
ch_device_cmd_get_integral_time (GUsbDevice *device,
				 guint16 *integral_time,
				 GError **error)
{
	gboolean ret;
	guint16 integral_le;

	g_return_val_if_fail (G_USB_IS_DEVICE (device), FALSE);
	g_return_val_if_fail (integral_time != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* hit hardware */
	ret = ch_device_write_command (device,
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
 * ch_device_cmd_set_integral_time:
 **/
gboolean
ch_device_cmd_set_integral_time (GUsbDevice *device,
				 guint16 integral_time,
				 GError **error)
{
	gboolean ret;
	guint16 integral_le;

	g_return_val_if_fail (G_USB_IS_DEVICE (device), FALSE);
	g_return_val_if_fail (integral_time > 0, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	integral_le = GUINT16_TO_LE (integral_time);

	/* hit hardware */
	ret = ch_device_write_command (device,
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
 * ch_device_cmd_get_calibration_map:
 **/
gboolean
ch_device_cmd_get_calibration_map (GUsbDevice *device,
				   guint16 *calibration_map,
				   GError **error)
{
	gboolean ret;

	g_return_val_if_fail (G_USB_IS_DEVICE (device), FALSE);
	g_return_val_if_fail (calibration_map != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* hit hardware */
	ret = ch_device_write_command (device,
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
 * ch_device_cmd_set_calibration_map:
 **/
gboolean
ch_device_cmd_set_calibration_map (GUsbDevice *device,
				   guint16 *calibration_map,
				   GError **error)
{
	gboolean ret;

	g_return_val_if_fail (G_USB_IS_DEVICE (device), FALSE);
	g_return_val_if_fail (calibration_map != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* hit hardware */
	ret = ch_device_write_command (device,
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
 * ch_device_cmd_get_firmware_ver:
 **/
gboolean
ch_device_cmd_get_firmware_ver (GUsbDevice *device,
				guint16 *major,
				guint16 *minor,
				guint16 *micro,
				GError **error)
{
	gboolean ret;
	guint16 buffer[3];

	g_return_val_if_fail (G_USB_IS_DEVICE (device), FALSE);
	g_return_val_if_fail (major != NULL, FALSE);
	g_return_val_if_fail (minor != NULL, FALSE);
	g_return_val_if_fail (micro != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* hit hardware */
	ret = ch_device_write_command (device,
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
 * ch_device_cmd_get_calibration:
 **/
gboolean
ch_device_cmd_get_calibration (GUsbDevice *device,
			       guint16 calibration_index,
			       gdouble *calibration,
			       guint8 *types,
			       gchar *description,
			       GError **error)
{
	gboolean ret;
	guint8 buffer[9*4 + 1 + CH_CALIBRATION_DESCRIPTION_LEN];
	guint i;

	g_return_val_if_fail (G_USB_IS_DEVICE (device), FALSE);
	g_return_val_if_fail (calibration_index < CH_CALIBRATION_MAX, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* hit hardware */
	ret = ch_device_write_command (device,
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
	if (calibration != NULL) {
		for (i = 0; i < 9; i++) {
			ch_packed_float_to_double ((ChPackedFloat *) &buffer[i*4],
						   &calibration[i]);
		}
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
 * ch_device_cmd_set_calibration:
 **/
gboolean
ch_device_cmd_set_calibration (GUsbDevice *device,
			       guint16 calibration_index,
			       const gdouble *calibration,
			       guint8 types,
			       const gchar *description,
			       GError **error)
{
	gboolean ret;
	guint8 buffer[9*4 + 2 + 1 + CH_CALIBRATION_DESCRIPTION_LEN];
	guint i;

	g_return_val_if_fail (G_USB_IS_DEVICE (device), FALSE);
	g_return_val_if_fail (calibration_index < CH_CALIBRATION_MAX, FALSE);
	g_return_val_if_fail (calibration != NULL, FALSE);
	g_return_val_if_fail (description != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

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
	ret = ch_device_write_command (device,
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
 * ch_device_cmd_set_calibration_ccmx:
 **/
gboolean
ch_device_cmd_set_calibration_ccmx (GUsbDevice *device,
				    guint16 calibration_index,
				    const gchar *filename,
				    GError **error)
{
	cmsHANDLE ccmx = NULL;
	const gchar *description;
	const gchar *sheet_type;
	gboolean ret;
	gchar *ccmx_data = NULL;
	gdouble calibration[9];
	gsize ccmx_size;

	g_return_val_if_fail (calibration != NULL, FALSE);
	g_return_val_if_fail (G_USB_IS_DEVICE (device), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* load file */
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
	description = CMSEXPORT cmsIT8GetProperty(ccmx, "DISPLAY");
	if (description == NULL) {
		ret = FALSE;
		g_set_error_literal (error, 1, 0,
				     "CCMX file does not have DISPLAY");
		goto out;
	}

	/* get the values */
	calibration[0] = cmsIT8GetDataRowColDbl(ccmx, 0, 0);
	calibration[1] = cmsIT8GetDataRowColDbl(ccmx, 0, 1);
	calibration[2] = cmsIT8GetDataRowColDbl(ccmx, 0, 2);
	calibration[3] = cmsIT8GetDataRowColDbl(ccmx, 1, 0);
	calibration[4] = cmsIT8GetDataRowColDbl(ccmx, 1, 1);
	calibration[5] = cmsIT8GetDataRowColDbl(ccmx, 1, 2);
	calibration[6] = cmsIT8GetDataRowColDbl(ccmx, 2, 0);
	calibration[7] = cmsIT8GetDataRowColDbl(ccmx, 2, 1);
	calibration[8] = cmsIT8GetDataRowColDbl(ccmx, 2, 2);

	/* set to HW */
	ret = ch_device_cmd_set_calibration (device,
					     calibration_index,
					     calibration,
					     CH_CALIBRATION_TYPE_ALL,
					     description,
					     error);
	if (!ret)
		goto out;
out:
	g_free (ccmx_data);
	if (ccmx != NULL)
		cmsIT8Free (ccmx);
	return ret;
}

/**
 * ch_device_cmd_clear_calibration:
 **/
gboolean
ch_device_cmd_clear_calibration (GUsbDevice *device,
				 guint16 calibration_index,
				 GError **error)
{
	gboolean ret;
	guint8 buffer[9*4 + 2 + 1 + CH_CALIBRATION_DESCRIPTION_LEN];

	g_return_val_if_fail (G_USB_IS_DEVICE (device), FALSE);
	g_return_val_if_fail (calibration_index < CH_CALIBRATION_MAX, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* factory calibration needs to be hard to erase */
	if (calibration_index == 0) {
		ret = FALSE;
		g_set_error_literal (error, 1, 0,
				     "You cannot clear the factory calibration");
		goto out;
	}

	/* write index */
	memcpy (buffer, &calibration_index, sizeof(guint16));

	/* clear data */
	memset (buffer + 2, 0xff, sizeof (buffer) - 2);

	/* hit hardware */
	ret = ch_device_write_command (device,
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
 * ch_device_cmd_get_pre_scale:
 **/
gboolean
ch_device_cmd_get_pre_scale (GUsbDevice *device,
			     gdouble *pre_scale,
			     GError **error)
{
	gboolean ret;
	ChPackedFloat buffer;

	g_return_val_if_fail (G_USB_IS_DEVICE (device), FALSE);
	g_return_val_if_fail (pre_scale != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* hit hardware */
	ret = ch_device_write_command (device,
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
 * ch_device_cmd_set_pre_scale:
 **/
gboolean
ch_device_cmd_set_pre_scale (GUsbDevice *device,
			     gdouble pre_scale,
			     GError **error)
{
	gboolean ret;
	ChPackedFloat buffer;

	g_return_val_if_fail (G_USB_IS_DEVICE (device), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* convert from float to signed value */
	ch_double_to_packed_float (pre_scale, &buffer);

	/* hit hardware */
	ret = ch_device_write_command (device,
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
 * ch_device_cmd_get_post_scale:
 **/
gboolean
ch_device_cmd_get_post_scale (GUsbDevice *device,
			      gdouble *post_scale,
			      GError **error)
{
	gboolean ret;
	ChPackedFloat buffer;

	g_return_val_if_fail (G_USB_IS_DEVICE (device), FALSE);
	g_return_val_if_fail (post_scale != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* hit hardware */
	ret = ch_device_write_command (device,
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
 * ch_device_cmd_set_post_scale:
 **/
gboolean
ch_device_cmd_set_post_scale (GUsbDevice *device,
			      gdouble post_scale,
			      GError **error)
{
	gboolean ret;
	ChPackedFloat buffer;

	g_return_val_if_fail (G_USB_IS_DEVICE (device), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* convert from float to signed value */
	ch_double_to_packed_float (post_scale, &buffer);

	/* hit hardware */
	ret = ch_device_write_command (device,
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
 * ch_device_cmd_get_serial_number:
 **/
gboolean
ch_device_cmd_get_serial_number (GUsbDevice *device,
				 guint32 *serial_number,
				 GError **error)
{
	gboolean ret;
	guint32 serial_le;

	g_return_val_if_fail (G_USB_IS_DEVICE (device), FALSE);
	g_return_val_if_fail (serial_number != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* hit hardware */
	ret = ch_device_write_command (device,
				       CH_CMD_GET_SERIAL_NUMBER,
				       NULL,	/* buffer in */
				       0,	/* size of input buffer */
				       (guint8 *) &serial_le,
				       sizeof(serial_le),	/* size of output buffer */
				       NULL,	/* cancellable */
				       error);
	if (!ret)
		goto out;
	*serial_number = GUINT32_FROM_LE (serial_le);
out:
	return ret;
}

/**
 * ch_device_cmd_set_serial_number:
 **/
gboolean
ch_device_cmd_set_serial_number (GUsbDevice *device,
				 guint32 serial_number,
				 GError **error)
{
	gboolean ret;
	guint32 serial_le;

	g_return_val_if_fail (G_USB_IS_DEVICE (device), FALSE);
	g_return_val_if_fail (serial_number > 0, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	serial_le = GUINT32_TO_LE (serial_number);

	/* hit hardware */
	ret = ch_device_write_command (device,
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
 * ch_device_cmd_get_leds:
 **/
gboolean
ch_device_cmd_get_leds (GUsbDevice *device,
			ChStatusLed *leds,
			GError **error)
{
	gboolean ret;

	g_return_val_if_fail (G_USB_IS_DEVICE (device), FALSE);
	g_return_val_if_fail (leds != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* hit hardware */
	ret = ch_device_write_command (device,
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
 * ch_device_cmd_set_leds:
 **/
gboolean
ch_device_cmd_set_leds (GUsbDevice *device,
			ChStatusLed leds,
			guint8 repeat,
			guint8 on_time,
			guint8 off_time,
			GError **error)
{
	gboolean ret;
	guint8 buffer[4];

	g_return_val_if_fail (G_USB_IS_DEVICE (device), FALSE);
	g_return_val_if_fail (leds < 0x04, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* hit hardware */
	buffer[0] = leds;
	buffer[1] = repeat;
	buffer[2] = on_time;
	buffer[3] = off_time;
	ret = ch_device_write_command (device,
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
 * ch_device_cmd_write_eeprom:
 **/
gboolean
ch_device_cmd_write_eeprom (GUsbDevice *device,
			    const gchar *magic,
			    GError **error)
{
	gboolean ret;

	g_return_val_if_fail (G_USB_IS_DEVICE (device), FALSE);
	g_return_val_if_fail (magic != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* hit hardware */
	ret = ch_device_write_command (device,
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
 * ch_device_cmd_get_dark_offsets:
 **/
gboolean
ch_device_cmd_get_dark_offsets (GUsbDevice *device,
				CdColorRGB *value,
				GError **error)
{
	gboolean ret;
	guint16 buffer[3];

	g_return_val_if_fail (G_USB_IS_DEVICE (device), FALSE);
	g_return_val_if_fail (value != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* hit hardware */
	ret = ch_device_write_command (device,
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
	value->R = (gdouble) buffer[0] / (gdouble) 0xffff;
	value->G = (gdouble) buffer[1] / (gdouble) 0xffff;
	value->B = (gdouble) buffer[2] / (gdouble) 0xffff;
out:
	return ret;
}

/**
 * ch_device_cmd_set_dark_offsets:
 **/
gboolean
ch_device_cmd_set_dark_offsets (GUsbDevice *device,
				CdColorRGB *value,
				GError **error)
{
	gboolean ret;
	guint16 buffer[3];

	g_return_val_if_fail (G_USB_IS_DEVICE (device), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* hit hardware */
	buffer[0] = value->R * (gdouble) 0xffff;
	buffer[1] = value->G * (gdouble) 0xffff;
	buffer[2] = value->B * (gdouble) 0xffff;
	ret = ch_device_write_command (device,
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
 * ch_device_cmd_take_reading_raw:
 **/
gboolean
ch_device_cmd_take_reading_raw (GUsbDevice *device,
				guint16 *take_reading,
				GError **error)
{
	gboolean ret;
	guint16 reading_le;

	g_return_val_if_fail (G_USB_IS_DEVICE (device), FALSE);
	g_return_val_if_fail (take_reading != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* hit hardware */
	ret = ch_device_write_command (device,
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
 * ch_device_cmd_take_readings:
 **/
gboolean
ch_device_cmd_take_readings (GUsbDevice *device,
			     CdColorRGB *value,
			     GError **error)
{
	gboolean ret;
	ChPackedFloat buffer[3];

	g_return_val_if_fail (G_USB_IS_DEVICE (device), FALSE);
	g_return_val_if_fail (value != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* hit hardware */
	ret = ch_device_write_command (device,
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
	ch_packed_float_to_double (&buffer[0], &value->R);
	ch_packed_float_to_double (&buffer[1], &value->G);
	ch_packed_float_to_double (&buffer[2], &value->B);
out:
	return ret;
}

/**
 * ch_device_cmd_take_readings_xyz:
 **/
gboolean
ch_device_cmd_take_readings_xyz (GUsbDevice *device,
				 guint16 calibration_index,
				 CdColorXYZ *value,
				 GError **error)
{
	gboolean ret;
	ChPackedFloat buffer[3];

	g_return_val_if_fail (G_USB_IS_DEVICE (device), FALSE);
	g_return_val_if_fail (value != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* hit hardware */
	ret = ch_device_write_command (device,
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
	ch_packed_float_to_double (&buffer[0], &value->X);
	ch_packed_float_to_double (&buffer[1], &value->Y);
	ch_packed_float_to_double (&buffer[2], &value->Z);
out:
	return ret;
}

/**
 * ch_device_cmd_reset:
 **/
gboolean
ch_device_cmd_reset (GUsbDevice *device,
		     GError **error)
{
	gboolean ret;

	g_return_val_if_fail (G_USB_IS_DEVICE (device), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* hit hardware */
	ret = ch_device_write_command (device,
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
 * ch_device_cmd_calculate_checksum:
 **/
static guint8
ch_device_cmd_calculate_checksum (guint8 *data,
			      gsize len)
{
	guint8 checksum = 0xff;
	guint i;
	for (i = 0; i < len; i++)
		checksum ^= data[i];
	return checksum;
}

/**
 * ch_device_cmd_write_flash:
 **/
gboolean
ch_device_cmd_write_flash (GUsbDevice *device,
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
	buffer_tx[3] = ch_device_cmd_calculate_checksum (data, len);
	memcpy (buffer_tx + 4, data, len);

	/* hit hardware */
	ret = ch_device_write_command (device,
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
 * ch_device_cmd_read_flash:
 **/
gboolean
ch_device_cmd_read_flash (GUsbDevice *device,
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
	ret = ch_device_write_command (device,
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
	expected_checksum = ch_device_cmd_calculate_checksum (buffer_rx + 1, len);
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
 * ch_device_cmd_erase_flash:
 **/
gboolean
ch_device_cmd_erase_flash (GUsbDevice *device,
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
	ret = ch_device_write_command (device,
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
 * ch_device_cmd_set_flash_success:
 **/
gboolean
ch_device_cmd_set_flash_success (GUsbDevice *device,
				 guint8 value,
				 GError **error)
{
	gboolean ret;

	g_return_val_if_fail (G_USB_IS_DEVICE (device), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* set flash success true */
	ret = ch_device_write_command (device,
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
 * ch_device_cmd_boot_flash:
 **/
gboolean
ch_device_cmd_boot_flash (GUsbDevice *device,
			  GError **error)
{
	gboolean ret;

	g_return_val_if_fail (G_USB_IS_DEVICE (device), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* boot into new code */
	ret = ch_device_write_command (device,
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
 * ch_device_cmd_get_hardware_version:
 **/
gboolean
ch_device_cmd_get_hardware_version (GUsbDevice *device,
				    guint8 *hw_version,
				    GError **error)
{
	gboolean ret;

	g_return_val_if_fail (G_USB_IS_DEVICE (device), FALSE);
	g_return_val_if_fail (hw_version != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* hit hardware */
	ret = ch_device_write_command (device,
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
