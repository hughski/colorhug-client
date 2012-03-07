/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2012 Richard Hughes <richard@hughsie.com>
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
#include <string.h>
#include <lcms2.h>

#include "ch-common.h"
#include "ch-device.h"
#include "ch-device-queue.h"
#include "ch-math.h"

static void	ch_device_queue_finalize	(GObject     *object);

#define CH_DEVICE_QUEUE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), CH_TYPE_DEVICE_QUEUE, ChDeviceQueuePrivate))

/**
 * ChDeviceQueuePrivate:
 *
 * Private #ChDeviceQueue data
 **/
struct _ChDeviceQueuePrivate
{
	GPtrArray		*data_array;
	GHashTable		*devices_in_use;
};

enum {
	SIGNAL_DEVICE_FAILED,
	SIGNAL_PROGRESS_CHANGED,
	SIGNAL_LAST
};

G_DEFINE_TYPE (ChDeviceQueue, ch_device_queue, G_TYPE_OBJECT)

typedef gboolean (*ChDeviceQueueParseFunc)	(guint8		*output_buffer,
						 gpointer	 user_data,
						 GError		**error);

typedef struct {
	gboolean		 complete;
	GUsbDevice		*device;
	guint8			 cmd;
	guint8			*buffer_in;	/* we own this */
	gsize			 buffer_in_len;
	guint8			*buffer_out;	/* we sometimes own this */
	gsize			 buffer_out_len;
	ChDeviceQueueParseFunc	 parse_func;
	gpointer		 user_data;
} ChDeviceQueueData;

typedef struct {
	ChDeviceQueue		*device_queue;
	ChDeviceQueueProcessFlags process_flags;
	GCancellable		*cancellable;
	GSimpleAsyncResult	*res;
	GPtrArray		*failures;
} ChDeviceQueueHelper;

static guint signals[SIGNAL_LAST] = { 0 };

static gboolean ch_device_queue_process_data (ChDeviceQueueHelper *helper, ChDeviceQueueData *data);

/**
 * ch_device_queue_data_free:
 **/
static void
ch_device_queue_data_free (ChDeviceQueueData *data)
{
	g_free (data->buffer_in);
	g_object_unref (data->device);
	g_free (data);
}

/**
 * ch_device_queue_free_helper:
 **/
static void
ch_device_queue_free_helper (ChDeviceQueueHelper *helper)
{
	if (helper->cancellable != NULL)
		g_object_unref (helper->cancellable);
	g_object_unref (helper->device_queue);
	g_object_unref (helper->res);
	g_ptr_array_unref (helper->failures);
	g_free (helper);
}

/**
 * ch_device_queue_device_force_complete:
 **/
static void
ch_device_queue_device_force_complete (ChDeviceQueue *device_queue, GUsbDevice *device)
{
	ChDeviceQueueData *data;
	const gchar *device_id;
	const gchar *device_id_tmp;
	guint i;

	/* go through the list of commands and cancel them all */
	device_id = g_usb_device_get_platform_id (device);
	for (i = 0; i < device_queue->priv->data_array->len; i++) {
		data = g_ptr_array_index (device_queue->priv->data_array, i);
		device_id_tmp = g_usb_device_get_platform_id (data->device);
		if (g_strcmp0 (device_id_tmp, device_id) == 0)
			data->complete = TRUE;
	}
}

/**
 * ch_device_queue_update_progress:
 **/
static void
ch_device_queue_update_progress (ChDeviceQueue *device_queue)
{
	guint complete = 0;
	guint i;
	guint percentage;
	ChDeviceQueueData *data;

	/* no devices */
	if (device_queue->priv->data_array->len == 0)
		return;

	/* find out how many commands are complete */
	for (i = 0; i < device_queue->priv->data_array->len; i++) {
		data = g_ptr_array_index (device_queue->priv->data_array, i);
		if (data->complete)
			complete++;
	}

	/* emit a signal with our progress */
	percentage = (complete * 100) / device_queue->priv->data_array->len;
	g_signal_emit (device_queue,
		       signals[SIGNAL_PROGRESS_CHANGED], 0,
		       percentage);
}

/**
 * ch_device_queue_process_write_command_cb:
 **/
static void
ch_device_queue_process_write_command_cb (GObject *source,
					  GAsyncResult *res,
					  gpointer user_data)
{
	ChDeviceQueueData *data;
	ChDeviceQueueHelper *helper = (ChDeviceQueueHelper *) user_data;
	const gchar *device_id;
	gboolean ret;
	gchar *error_msg = NULL;
	GError *error = NULL;
	guint i;
	GUsbDevice *device = G_USB_DEVICE (source);

	/* mark it as not in use */
	device_id = g_usb_device_get_platform_id (device);
	data = g_hash_table_lookup (helper->device_queue->priv->devices_in_use,
				    device_id);
	g_hash_table_remove (helper->device_queue->priv->devices_in_use,
			     device_id);

	/* get data */
	ret = ch_device_write_command_finish (device, res, &error);
	if (ret && data->parse_func != NULL) {
		/* do any conversion function */
		ret = data->parse_func (data->buffer_out,
					data->user_data,
					&error);
	}
	if (!ret) {
		/* tell the client the device has failed */
		g_debug ("emit device-failed: %s", error->message);
		g_signal_emit (helper->device_queue,
			       signals[SIGNAL_DEVICE_FAILED], 0,
			       device,
			       error->message);

		/* save this so we can possibly use when we're done */
		g_ptr_array_add (helper->failures,
				 g_strdup_printf ("%s: %s",
						  g_usb_device_get_platform_id (device),
						  error->message));
		g_error_free (error);

		/* should we mark complete other commands as complete */
		if ((helper->process_flags & CH_DEVICE_QUEUE_PROCESS_FLAGS_CONTINUE_ERRORS) == 0) {
			ch_device_queue_device_force_complete (helper->device_queue, device);
			ch_device_queue_update_progress (helper->device_queue);
			goto out;
		}
	}

	/* update progress */
	ch_device_queue_update_progress (helper->device_queue);

	/* is there another pending command for this device */
	for (i = 0; i < helper->device_queue->priv->data_array->len; i++) {
		data = g_ptr_array_index (helper->device_queue->priv->data_array, i);
		ret = ch_device_queue_process_data (helper, data);
		if (ret)
			break;
	}
out:
	/* any more pending commands? */
	if (g_hash_table_size (helper->device_queue->priv->devices_in_use) == 0) {

		/* should we return the process with an error, or just
		 * rely on the signal? */
		if (helper->failures->len > 0 &&
		    (helper->process_flags & CH_DEVICE_QUEUE_PROCESS_FLAGS_NONFATAL_ERRORS) == 0) {
			g_ptr_array_add (helper->failures, NULL);
			error_msg = g_strjoinv (", ", (gchar**) helper->failures->pdata);
			g_simple_async_result_set_error (helper->res,
							 1, 0,
							 "There were %i failures: %s",
							 helper->failures->len - 1,
							 error_msg);
		} else {
			g_simple_async_result_set_op_res_gboolean (helper->res, TRUE);
		}
		/* remove all commands from the queue, as they are done */
		g_ptr_array_set_size (helper->device_queue->priv->data_array, 0);
		g_simple_async_result_complete_in_idle (helper->res);
		ch_device_queue_free_helper (helper);
	}
	g_free (error_msg);
}

/**
 * ch_device_queue_process_data:
 *
 * Returns TRUE if the command was submitted
 **/
static gboolean
ch_device_queue_process_data (ChDeviceQueueHelper *helper,
			      ChDeviceQueueData *data)
{
	ChDeviceQueueData *data_tmp;
	const gchar *device_id;
	gboolean ret = FALSE;

	/* is this command already complete? */
	if (data->complete)
		goto out;

	/* is this device already busy? */
	device_id = g_usb_device_get_platform_id (data->device);
	data_tmp = g_hash_table_lookup (helper->device_queue->priv->devices_in_use,
					device_id);
	if (data_tmp != NULL)
		goto out;

	/* write this command and wait for a response */
	ch_device_write_command_async (data->device,
				       data->cmd,
				       data->buffer_in,
				       data->buffer_in_len,
				       data->buffer_out,
				       data->buffer_out_len,
				       helper->cancellable,
				       ch_device_queue_process_write_command_cb,
				       helper);
	/* mark this as in use */
	g_hash_table_insert (helper->device_queue->priv->devices_in_use,
			     g_strdup (device_id),
			     data);

	/* success */
	ret = TRUE;

	/* remove this from the command queue -- TODO: retries? */
	data->complete = TRUE;
out:
	return ret;
}

/**
 * ch_device_queue_process_async:
 * @device_queue:		A #ChDeviceQueue
 * @cancellable:	A #GCancellable, or %NULL
 * @callback:		A #GAsyncReadyCallback that will be called when finished.
 * @user_data:		User data passed to @callback
 *
 * Processes all commands in the command queue.
 **/
void
ch_device_queue_process_async (ChDeviceQueue		*device_queue,
			       ChDeviceQueueProcessFlags process_flags,
			       GCancellable		*cancellable,
			       GAsyncReadyCallback	 callback,
			       gpointer			 user_data)
{
	ChDeviceQueueHelper *helper;
	ChDeviceQueueData *data;
	guint i;

	g_return_if_fail (CH_IS_DEVICE_QUEUE (device_queue));
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	helper = g_new0 (ChDeviceQueueHelper, 1);
	helper->process_flags = process_flags;
	helper->failures = g_ptr_array_new_with_free_func (g_free);
	helper->device_queue = g_object_ref (device_queue);
	helper->res = g_simple_async_result_new (G_OBJECT (device_queue),
						 callback,
						 user_data,
						 ch_device_queue_process_async);
	if (cancellable != NULL)
		helper->cancellable = g_object_ref (cancellable);

	/* go through the list of commands and try to submit them all */
	ch_device_queue_update_progress (helper->device_queue);
	for (i = 0; i < device_queue->priv->data_array->len; i++) {
		data = g_ptr_array_index (device_queue->priv->data_array, i);
		ch_device_queue_process_data (helper, data);
	}

	/* is anything pending? */
	if (g_hash_table_size (device_queue->priv->devices_in_use) == 0) {
		g_simple_async_result_set_op_res_gboolean (helper->res, TRUE);
		g_simple_async_result_complete_in_idle (helper->res);
		ch_device_queue_free_helper (helper);
	}
}

/**
 * ch_device_queue_process_finish:
 * @device_queue: a #ChDeviceQueue instance.
 * @res: the #GAsyncResult
 * @error: A #GError or %NULL
 *
 * Gets the result from the asynchronous function.
 *
 * Return value: %TRUE if the request was fulfilled.
 **/
gboolean
ch_device_queue_process_finish (ChDeviceQueue	*device_queue,
				GAsyncResult	*res,
				GError		**error)
{
	GSimpleAsyncResult *simple;

	g_return_val_if_fail (CH_DEVICE_QUEUE (device_queue), FALSE);
	g_return_val_if_fail (G_IS_SIMPLE_ASYNC_RESULT (res), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (res);
	if (g_simple_async_result_propagate_error (simple, error))
		return FALSE;

	return g_simple_async_result_get_op_res_gboolean (simple);
}

/**********************************************************************/

/* tiny helper to help us do the async operation */
typedef struct {
	GError		**error;
	GMainLoop	*loop;
	gboolean	 ret;
} ChDeviceQueueSyncHelper;

/**
 * ch_device_queue_process_finish_cb:
 **/
static void
ch_device_queue_process_finish_cb (GObject *source,
				   GAsyncResult *res,
				   gpointer user_data)
{
	ChDeviceQueue *device_queue = CH_DEVICE_QUEUE (source);
	ChDeviceQueueSyncHelper *helper = (ChDeviceQueueSyncHelper *) user_data;
	helper->ret = ch_device_queue_process_finish (device_queue, res, helper->error);
	g_main_loop_quit (helper->loop);
}

/**
 * ch_device_queue_process:
 **/
gboolean
ch_device_queue_process (ChDeviceQueue	*device_queue,
			 ChDeviceQueueProcessFlags process_flags,
			 GCancellable	*cancellable,
			 GError		**error)
{
	ChDeviceQueueSyncHelper helper;

	g_return_val_if_fail (CH_IS_DEVICE_QUEUE (device_queue), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* create temp object */
	helper.loop = g_main_loop_new (NULL, FALSE);
	helper.error = error;

	/* run async method */
	ch_device_queue_process_async (device_queue,
				       process_flags,
				       cancellable,
				       ch_device_queue_process_finish_cb,
				       &helper);
	g_main_loop_run (helper.loop);

	/* free temp object */
	g_main_loop_unref (helper.loop);

	return helper.ret;
}

/**********************************************************************/

/**
 * ch_device_queue_add_internal:
 **/
static void
ch_device_queue_add_internal (ChDeviceQueue		*device_queue,
			      GUsbDevice		*device,
			      guint8			 cmd,
			      const guint8		*buffer_in,
			      gsize			 buffer_in_len,
			      guint8			*buffer_out,
			      gsize			 buffer_out_len,
			      ChDeviceQueueParseFunc	 parse_func,
			      gpointer			 user_data)
{
	ChDeviceQueueData *data;

	g_return_if_fail (CH_IS_DEVICE_QUEUE (device_queue));
	g_return_if_fail (G_USB_IS_DEVICE (device));

	data = g_new0 (ChDeviceQueueData, 1);
	data->parse_func = parse_func;
	data->user_data = user_data;
	data->cmd = cmd;
	data->device = g_object_ref (device);
	if (buffer_in != NULL)
		data->buffer_in = g_memdup (buffer_in, buffer_in_len);
	data->buffer_in_len = buffer_in_len;
	data->buffer_out = buffer_out;
	data->buffer_out_len = buffer_out_len;
	g_ptr_array_add (device_queue->priv->data_array, data);
}

/**
 * ch_device_queue_add:
 **/
void
ch_device_queue_add (ChDeviceQueue	*device_queue,
		     GUsbDevice		*device,
		     guint8		 cmd,
		     const guint8	*buffer_in,
		     gsize		 buffer_in_len,
		     guint8		*buffer_out,
		     gsize		 buffer_out_len)
{
	ch_device_queue_add_internal (device_queue,
				      device,
				      cmd,
				      buffer_in,
				      buffer_in_len,
				      buffer_out,
				      buffer_out_len,
				      NULL,
				      NULL);
}

/**********************************************************************/



/**
 * ch_device_queue_get_color_select:
 **/
void
ch_device_queue_get_color_select (ChDeviceQueue *device_queue,
				  GUsbDevice *device,
				  ChColorSelect *color_select)
{
	g_return_if_fail (CH_IS_DEVICE_QUEUE (device_queue));
	g_return_if_fail (G_USB_IS_DEVICE (device));
	g_return_if_fail (color_select != NULL);

	ch_device_queue_add (device_queue,
			     device,
			     CH_CMD_GET_COLOR_SELECT,
			     NULL,
			     0,
			     (guint8 *) color_select,
			     1);
}

/**
 * ch_device_queue_set_color_select:
 **/
void
ch_device_queue_set_color_select (ChDeviceQueue *device_queue,
				  GUsbDevice *device,
				  ChColorSelect color_select)
{
	guint8 csel8 = color_select;

	g_return_if_fail (CH_IS_DEVICE_QUEUE (device_queue));
	g_return_if_fail (G_USB_IS_DEVICE (device));

	ch_device_queue_add (device_queue,
			     device,
			     CH_CMD_SET_COLOR_SELECT,
			     &csel8,
			     1,
			     NULL,
			     0);
}

/**
 * ch_device_queue_get_multiplier:
 **/
void
ch_device_queue_get_multiplier (ChDeviceQueue *device_queue,
				GUsbDevice *device,
				ChFreqScale *multiplier)
{
	g_return_if_fail (CH_IS_DEVICE_QUEUE (device_queue));
	g_return_if_fail (G_USB_IS_DEVICE (device));
	g_return_if_fail (multiplier != NULL);

	ch_device_queue_add (device_queue,
			     device,
			     CH_CMD_GET_MULTIPLIER,
			     NULL,
			     0,
			     (guint8 *) multiplier,
			     1);
}

/**
 * ch_device_queue_set_multiplier:
 **/
void
ch_device_queue_set_multiplier (ChDeviceQueue *device_queue,
				GUsbDevice *device,
				ChFreqScale multiplier)
{
	guint8 mult8 = multiplier;

	g_return_if_fail (CH_IS_DEVICE_QUEUE (device_queue));
	g_return_if_fail (G_USB_IS_DEVICE (device));

	ch_device_queue_add (device_queue,
			     device,
			     CH_CMD_SET_MULTIPLIER,
			     &mult8,
			     1,
			     NULL,
			     0);
}

/**
 * ch_device_queue_buffer_uint16_from_le_cb:
 **/
static gboolean
ch_device_queue_buffer_uint16_from_le_cb (guint8 *output_buffer,
					  gpointer user_data,
					  GError **error)
{
	guint16 *tmp = (guint16 *) output_buffer;
	*tmp = GUINT16_FROM_LE (*tmp);
	return TRUE;
}

/**
 * ch_device_queue_buffer_uint16_from_le_cb:
 **/
static gboolean
ch_device_queue_buffer_uint32_from_le_cb (guint8 *output_buffer,
					  gpointer user_data,
					  GError **error)
{
	guint32 *tmp = (guint32 *) output_buffer;
	*tmp = GUINT32_FROM_LE (*tmp);
	return TRUE;
}

/**
 * ch_device_queue_get_integral_time:
 **/
void
ch_device_queue_get_integral_time (ChDeviceQueue *device_queue,
				   GUsbDevice *device,
				   guint16 *integral_time)
{
	g_return_if_fail (CH_IS_DEVICE_QUEUE (device_queue));
	g_return_if_fail (G_USB_IS_DEVICE (device));
	g_return_if_fail (integral_time != NULL);

	ch_device_queue_add_internal (device_queue,
				      device,
				      CH_CMD_GET_INTEGRAL_TIME,
				      NULL,
				      0,
				      (guint8 *) integral_time,
				      2,
				      ch_device_queue_buffer_uint16_from_le_cb,
				      NULL);
}

/**
 * ch_device_queue_set_integral_time:
 **/
void
ch_device_queue_set_integral_time (ChDeviceQueue *device_queue,
				   GUsbDevice *device,
				   guint16 integral_time)
{
	guint16 integral_le;

	g_return_if_fail (CH_IS_DEVICE_QUEUE (device_queue));
	g_return_if_fail (G_USB_IS_DEVICE (device));
	g_return_if_fail (integral_time > 0);

	integral_le = GUINT16_TO_LE (integral_time);

	ch_device_queue_add (device_queue,
			     device,
			     CH_CMD_SET_INTEGRAL_TIME,
			     (const guint8 *) &integral_le,
			     sizeof(guint16),
			     NULL,
			     0);
}

/**
 * ch_device_queue_get_calibration_map:
 **/
void
ch_device_queue_get_calibration_map (ChDeviceQueue *device_queue,
				     GUsbDevice *device,
				     guint16 *calibration_map)
{
	g_return_if_fail (CH_IS_DEVICE_QUEUE (device_queue));
	g_return_if_fail (G_USB_IS_DEVICE (device));
	g_return_if_fail (calibration_map != NULL);

	ch_device_queue_add (device_queue,
			     device,
			     CH_CMD_GET_CALIBRATION_MAP,
			     NULL,
			     0,
			     (guint8 *) calibration_map,
			     6 * sizeof(guint16));
}

/**
 * ch_device_queue_set_calibration_map:
 **/
void
ch_device_queue_set_calibration_map (ChDeviceQueue *device_queue,
				     GUsbDevice *device,
				     guint16 *calibration_map)
{
	g_return_if_fail (CH_IS_DEVICE_QUEUE (device_queue));
	g_return_if_fail (G_USB_IS_DEVICE (device));
	g_return_if_fail (calibration_map != NULL);

	ch_device_queue_add (device_queue,
			     device,
			     CH_CMD_SET_CALIBRATION_MAP,
			     (const guint8 *) calibration_map,
			     6 * sizeof(guint16),
			     NULL,
			     0);
}

/* tiny helper */
typedef struct {
	guint16		*major;
	guint16		*minor;
	guint16		*micro;
} ChDeviceQueueGetFirmwareVerHelper;

/**
 * ch_device_queue_buffer_to_firmware_ver_cb:
 **/
static gboolean
ch_device_queue_buffer_to_firmware_ver_cb (guint8 *output_buffer,
					   gpointer user_data,
					   GError **error)
{
	ChDeviceQueueGetFirmwareVerHelper *helper = (void *) user_data;
	guint16 *tmp = (guint16 *) output_buffer;

	*helper->major = GUINT16_FROM_LE (tmp[0]);
	*helper->minor = GUINT16_FROM_LE (tmp[1]);
	*helper->micro = GUINT16_FROM_LE (tmp[2]);

	/* yes, we own this */
	g_free (output_buffer);
	g_free (helper);
	return TRUE;
}

/**
 * ch_device_queue_get_firmware_ver:
 **/
void
ch_device_queue_get_firmware_ver (ChDeviceQueue *device_queue,
				  GUsbDevice *device,
				  guint16 *major,
				  guint16 *minor,
				  guint16 *micro)
{
	guint8 *buffer;
	ChDeviceQueueGetFirmwareVerHelper *helper;

	g_return_if_fail (CH_IS_DEVICE_QUEUE (device_queue));
	g_return_if_fail (G_USB_IS_DEVICE (device));
	g_return_if_fail (major != NULL);
	g_return_if_fail (minor != NULL);
	g_return_if_fail (micro != NULL);

	/* create a helper structure */
	helper = g_new0 (ChDeviceQueueGetFirmwareVerHelper, 1);
	helper->major = major;
	helper->minor = minor;
	helper->micro = micro;

	buffer = g_new0 (guint8, sizeof (guint16) * 3);
	ch_device_queue_add_internal (device_queue,
				      device,
				      CH_CMD_GET_FIRMWARE_VERSION,
				      NULL,
				      0,
				      buffer,
				      sizeof (guint16) * 3,
				      ch_device_queue_buffer_to_firmware_ver_cb,
				      helper);
}

/* tiny helper */
typedef struct {
	CdMat3x3	*calibration;
	guint8		*types;
	gchar		*description;
} ChDeviceQueueGetCalibrationHelper;

/**
 * ch_device_queue_buffer_to_get_calibration_cb:
 **/
static gboolean
ch_device_queue_buffer_to_get_calibration_cb (guint8 *output_buffer,
					      gpointer user_data,
					      GError **error)
{
	ChDeviceQueueGetCalibrationHelper *helper = (void *) user_data;
	gdouble *calibration_tmp;
	guint i;

	/* convert back into floating point */
	if (helper->calibration != NULL) {
		calibration_tmp = cd_mat33_get_data (helper->calibration);
		for (i = 0; i < 9; i++) {
			ch_packed_float_to_double ((ChPackedFloat *) &output_buffer[i*4],
						   &calibration_tmp[i]);
		}
	}

	/* get the supported types */
	if (helper->types != NULL)
		*helper->types = output_buffer[9*4];

	/* get the description */
	if (helper->description != NULL) {
		strncpy (helper->description,
			 (const char *) output_buffer + 9*4 + 1,
			 CH_CALIBRATION_DESCRIPTION_LEN);
	}

	/* yes, we own this */
	g_free (output_buffer);
	g_free (helper);
	return TRUE;
}

/**
 * ch_device_queue_get_calibration:
 **/
void
ch_device_queue_get_calibration (ChDeviceQueue *device_queue,
				 GUsbDevice *device,
				 guint16 calibration_index,
				 CdMat3x3 *calibration,
				 guint8 *types,
				 gchar *description)
{
	guint8 *buffer;
	ChDeviceQueueGetCalibrationHelper *helper;

	g_return_if_fail (CH_IS_DEVICE_QUEUE (device_queue));
	g_return_if_fail (G_USB_IS_DEVICE (device));
	g_return_if_fail (calibration_index < CH_CALIBRATION_MAX);

	/* create a helper structure */
	helper = g_new0 (ChDeviceQueueGetCalibrationHelper, 1);
	helper->calibration = calibration;
	helper->types = types;
	helper->description = description;

	buffer = g_new0 (guint8, 9*4 + 1 + CH_CALIBRATION_DESCRIPTION_LEN);
	ch_device_queue_add_internal (device_queue,
				      device,
				      CH_CMD_GET_CALIBRATION,
				      (guint8 *) &calibration_index,
				      sizeof(guint16),
				      (guint8 *) buffer,
				      9*4 + 1 + CH_CALIBRATION_DESCRIPTION_LEN,
				      ch_device_queue_buffer_to_get_calibration_cb,
				      helper);
}

/**
 * ch_device_queue_set_calibration:
 **/
void
ch_device_queue_set_calibration (ChDeviceQueue *device_queue,
				 GUsbDevice *device,
				 guint16 calibration_index,
				 const CdMat3x3 *calibration,
				 guint8 types,
				 const gchar *description)
{
	gdouble *calibration_tmp;
	guint8 buffer[9*4 + 2 + 1 + CH_CALIBRATION_DESCRIPTION_LEN];
	guint i;

	g_return_if_fail (CH_IS_DEVICE_QUEUE (device_queue));
	g_return_if_fail (G_USB_IS_DEVICE (device));
	g_return_if_fail (calibration_index < CH_CALIBRATION_MAX);
	g_return_if_fail (calibration != NULL);
	g_return_if_fail (description != NULL);

	/* write index */
	memcpy (buffer, &calibration_index, sizeof(guint16));

	/* convert from float to signed value */
	for (i = 0; i < 9; i++) {
		calibration_tmp = cd_mat33_get_data (calibration);
		ch_double_to_packed_float (calibration_tmp[i],
					   (ChPackedFloat *) &buffer[i*4 + 2]);
	}

	/* write types */
	buffer[9*4 + 2] = types;

	/* write description */
	strncpy ((gchar *) buffer + 9*4 + 2 + 1,
		 description,
		 CH_CALIBRATION_DESCRIPTION_LEN);

	ch_device_queue_add (device_queue,
			     device,
			     CH_CMD_SET_CALIBRATION,
			     (guint8 *) buffer,
			     sizeof(buffer),
			     NULL,
			     0);
}

#define CH_DEVICE_DETERMINANT_AVE	21.53738
#define CH_DEVICE_DETERMINANT_ERROR	10.00000

/**
 * ch_device_queue_set_calibration_ccmx:
 **/
gboolean
ch_device_queue_set_calibration_ccmx (ChDeviceQueue *device_queue,
				      GUsbDevice *device,
				      guint16 calibration_index,
				      const gchar *filename,
				      GError **error)
{
	CdMat3x3 calibration;
	cmsHANDLE ccmx = NULL;
	const gchar *description;
	const gchar *sheet_type;
	gboolean ret;
	gdouble *calibration_tmp;
#if CD_CHECK_VERSION(0,1,17)
	gdouble det;
#endif
	gchar *ccmx_data = NULL;
	gsize ccmx_size;
	guint i;

	g_return_val_if_fail (filename != NULL, FALSE);
	g_return_val_if_fail (CH_IS_DEVICE_QUEUE (device_queue), FALSE);
	g_return_val_if_fail (G_USB_IS_DEVICE (device), FALSE);

	/* load file */
	ret = g_file_get_contents (filename,
				   &ccmx_data,
				   &ccmx_size,
				   error);
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
	calibration.m00 = cmsIT8GetDataRowColDbl(ccmx, 0, 0);
	calibration.m01 = cmsIT8GetDataRowColDbl(ccmx, 0, 1);
	calibration.m02 = cmsIT8GetDataRowColDbl(ccmx, 0, 2);
	calibration.m10 = cmsIT8GetDataRowColDbl(ccmx, 1, 0);
	calibration.m11 = cmsIT8GetDataRowColDbl(ccmx, 1, 1);
	calibration.m12 = cmsIT8GetDataRowColDbl(ccmx, 1, 2);
	calibration.m20 = cmsIT8GetDataRowColDbl(ccmx, 2, 0);
	calibration.m21 = cmsIT8GetDataRowColDbl(ccmx, 2, 1);
	calibration.m22 = cmsIT8GetDataRowColDbl(ccmx, 2, 2);

	/* check for sanity */
	calibration_tmp = cd_mat33_get_data (&calibration);
	for (i = 0; i < 9; i++) {
		if (calibration_tmp[i] < -10.0f ||
		    calibration_tmp[i] > 10.0f) {
			ret = FALSE;
			g_set_error (error, 1, 0,
				     "Matrix value %i out of range %f",
				     i, calibration_tmp[i]);
			goto out;
		}
	}

#if CD_CHECK_VERSION(0,1,17)
	/* check the scale is correct */
	det = cd_mat33_determinant (&calibration);
	if (ABS (det - CH_DEVICE_DETERMINANT_AVE) > CH_DEVICE_DETERMINANT_ERROR) {
		ret = FALSE;
		g_set_error (error, 1, 0,
			     "Matrix determinant out of range: %f", det);
		goto out;
	}
#endif

	/* set to HW */
	ch_device_queue_set_calibration (device_queue,
					 device,
					 calibration_index,
					 &calibration,
					 CH_CALIBRATION_TYPE_ALL,
					 description);
out:
	g_free (ccmx_data);
	if (ccmx != NULL)
		cmsIT8Free (ccmx);
	return ret;
}

/**
 * ch_device_queue_write_firmware:
 **/
void
ch_device_queue_write_firmware (ChDeviceQueue	*device_queue,
				GUsbDevice	*device,
				const guint8	*data,
				gsize		 len)
{
	gsize chunk_len;
	guint idx;

	g_return_if_fail (CH_IS_DEVICE_QUEUE (device_queue));
	g_return_if_fail (G_USB_IS_DEVICE (device));
	g_return_if_fail (data != NULL);

	/* erase flash */
	g_debug ("Erasing at %04x size %" G_GSIZE_FORMAT,
		 CH_EEPROM_ADDR_RUNCODE, len);
	ch_device_queue_erase_flash (device_queue,
				     device,
				     CH_EEPROM_ADDR_RUNCODE,
				     len);

	/* just write in 32 byte chunks, as we're sure that the firmware
	 * image has been prepared to end on a 64 byte chunk with
	 * colorhug-inhx32-to-bin >= 0.1.5 */
	idx = 0;
	chunk_len = CH_FLASH_TRANSFER_BLOCK_SIZE;
	do {
		if (idx + chunk_len > len)
			chunk_len = len - idx;
		g_debug ("Writing at %04x size %" G_GSIZE_FORMAT,
			 CH_EEPROM_ADDR_RUNCODE + idx,
			 chunk_len);
		ch_device_queue_write_flash (device_queue,
					     device,
					     CH_EEPROM_ADDR_RUNCODE + idx,
					     (guint8 *) data + idx,
					     chunk_len);
		idx += chunk_len;
	} while (idx < len);
}

/**
 * ch_device_queue_verify_firmware:
 **/
void
ch_device_queue_verify_firmware (ChDeviceQueue	*device_queue,
				 GUsbDevice	*device,
				 const guint8	*data,
				 gsize		 len)
{
	gsize chunk_len;
	guint idx;

	g_return_if_fail (CH_IS_DEVICE_QUEUE (device_queue));
	g_return_if_fail (G_USB_IS_DEVICE (device));
	g_return_if_fail (data != NULL);

	/* read in 60 byte chunks */
	idx = 0;
	chunk_len = 60;
	do {
		if (idx + chunk_len > len)
			chunk_len = len - idx;
		g_debug ("Verifying at %04x size %" G_GSIZE_FORMAT,
			 CH_EEPROM_ADDR_RUNCODE + idx,
			 chunk_len);
		ch_device_queue_verify_flash (device_queue,
					      device,
					      CH_EEPROM_ADDR_RUNCODE + idx,
					      data + idx,
					      chunk_len);
		idx += chunk_len;
	} while (idx < len);
}

/**
 * ch_device_queue_clear_calibration:
 **/
void
ch_device_queue_clear_calibration (ChDeviceQueue *device_queue,
				   GUsbDevice *device,
				   guint16 calibration_index)
{
	guint8 buffer[9*4 + 2 + 1 + CH_CALIBRATION_DESCRIPTION_LEN];

	g_return_if_fail (CH_IS_DEVICE_QUEUE (device_queue));
	g_return_if_fail (G_USB_IS_DEVICE (device));
	g_return_if_fail (calibration_index < CH_CALIBRATION_MAX);

	/* write index */
	memcpy (buffer, &calibration_index, sizeof(guint16));

	/* clear data */
	memset (buffer + 2, 0xff, sizeof (buffer) - 2);

	ch_device_queue_add (device_queue,
			     device,
			     CH_CMD_SET_CALIBRATION,
			     (guint8 *) buffer,
			     sizeof(buffer),
			     NULL,
			     0);
}

/**
 * ch_device_queue_buffer_to_double_cb:
 **/
static gboolean
ch_device_queue_buffer_to_double_cb (guint8 *output_buffer,
				     gpointer user_data,
				     GError **error)
{
	ChPackedFloat *buffer = (ChPackedFloat *) output_buffer;
	gdouble *value = (gdouble *) user_data;

	/* convert back into floating point */
	ch_packed_float_to_double (buffer, value);
	g_free (output_buffer);
	return TRUE;
}

/**
 * ch_device_queue_get_pre_scale:
 **/
void
ch_device_queue_get_pre_scale (ChDeviceQueue *device_queue,
			       GUsbDevice *device,
			       gdouble *pre_scale)
{
	guint8 *buffer;

	g_return_if_fail (CH_IS_DEVICE_QUEUE (device_queue));
	g_return_if_fail (G_USB_IS_DEVICE (device));
	g_return_if_fail (pre_scale != NULL);

	*pre_scale = 0.0f;
	buffer = g_new0 (guint8, sizeof (ChPackedFloat));
	ch_device_queue_add_internal (device_queue,
				     device,
				     CH_CMD_GET_PRE_SCALE,
				     NULL,
				     0,
				     buffer,
				     sizeof(ChPackedFloat),
				     ch_device_queue_buffer_to_double_cb,
				     pre_scale);
}

/**
 * ch_device_queue_set_pre_scale:
 **/
void
ch_device_queue_set_pre_scale (ChDeviceQueue *device_queue,
			       GUsbDevice *device,
			       gdouble pre_scale)
{
	ChPackedFloat buffer;

	g_return_if_fail (CH_IS_DEVICE_QUEUE (device_queue));
	g_return_if_fail (G_USB_IS_DEVICE (device));

	/* convert from float to signed value */
	ch_double_to_packed_float (pre_scale, &buffer);

	ch_device_queue_add (device_queue,
			     device,
			     CH_CMD_SET_PRE_SCALE,
			     (guint8 *) &buffer,
			     sizeof(buffer),
			     NULL,
			     0);
}

/**
 * ch_device_queue_get_post_scale:
 **/
void
ch_device_queue_get_post_scale (ChDeviceQueue *device_queue,
				GUsbDevice *device,
				gdouble *post_scale)
{
	guint8 *buffer;

	g_return_if_fail (CH_IS_DEVICE_QUEUE (device_queue));
	g_return_if_fail (G_USB_IS_DEVICE (device));
	g_return_if_fail (post_scale != NULL);

	*post_scale = 0.0f;
	buffer = g_new0 (guint8, sizeof (ChPackedFloat));
	ch_device_queue_add_internal (device_queue,
				      device,
				      CH_CMD_GET_POST_SCALE,
				      NULL,
				      0,
				     buffer,
				     sizeof(ChPackedFloat),
				     ch_device_queue_buffer_to_double_cb,
				     post_scale);
}

/**
 * ch_device_queue_set_post_scale:
 **/
void
ch_device_queue_set_post_scale (ChDeviceQueue *device_queue,
				GUsbDevice *device,
				gdouble post_scale)
{
	ChPackedFloat buffer;

	g_return_if_fail (CH_IS_DEVICE_QUEUE (device_queue));
	g_return_if_fail (G_USB_IS_DEVICE (device));

	/* convert from float to signed value */
	ch_double_to_packed_float (post_scale, &buffer);

	ch_device_queue_add (device_queue,
			     device,
			     CH_CMD_SET_POST_SCALE,
			     (guint8 *) &buffer,
			     sizeof(buffer),
			     NULL,
			     0);
}

/**
 * ch_device_queue_get_serial_number:
 **/
void
ch_device_queue_get_serial_number (ChDeviceQueue *device_queue,
				   GUsbDevice *device,
				   guint32 *serial_number)
{
	g_return_if_fail (CH_IS_DEVICE_QUEUE (device_queue));
	g_return_if_fail (G_USB_IS_DEVICE (device));
	g_return_if_fail (serial_number != NULL);

	*serial_number = 0;
	ch_device_queue_add_internal (device_queue,
				      device,
				      CH_CMD_GET_SERIAL_NUMBER,
				      NULL,
				      0,
				      (guint8 *) serial_number,
				      sizeof(guint32),
				      ch_device_queue_buffer_uint32_from_le_cb,
				      NULL);
}

/**
 * ch_device_queue_set_serial_number:
 **/
void
ch_device_queue_set_serial_number (ChDeviceQueue *device_queue,
				   GUsbDevice *device,
				   guint32 serial_number)
{
	guint32 serial_le;

	g_return_if_fail (CH_IS_DEVICE_QUEUE (device_queue));
	g_return_if_fail (G_USB_IS_DEVICE (device));
	g_return_if_fail (serial_number > 0);

	serial_le = GUINT32_TO_LE (serial_number);

	ch_device_queue_add (device_queue,
			     device,
			     CH_CMD_SET_SERIAL_NUMBER,
			     (const guint8 *) &serial_le,
			     sizeof(serial_le),
			     NULL,
			     0);
}

/**
 * ch_device_queue_get_leds:
 **/
void
ch_device_queue_get_leds (ChDeviceQueue *device_queue,
			  GUsbDevice *device,
			  guint8 *leds)
{
	g_return_if_fail (CH_IS_DEVICE_QUEUE (device_queue));
	g_return_if_fail (G_USB_IS_DEVICE (device));
	g_return_if_fail (leds != NULL);

	*leds = 0;
	ch_device_queue_add (device_queue,
			     device,
			     CH_CMD_GET_LEDS,
			     NULL,
			     0,
			     leds,
			     1);
}

/**
 * ch_device_queue_set_leds:
 **/
void
ch_device_queue_set_leds (ChDeviceQueue *device_queue,
			  GUsbDevice *device,
			  guint8 leds,
			  guint8 repeat,
			  guint8 on_time,
			  guint8 off_time)
{
	guint8 buffer[4];

	g_return_if_fail (CH_IS_DEVICE_QUEUE (device_queue));
	g_return_if_fail (G_USB_IS_DEVICE (device));
	g_return_if_fail (leds < 0x04);

	buffer[0] = leds;
	buffer[1] = repeat;
	buffer[2] = on_time;
	buffer[3] = off_time;
	ch_device_queue_add (device_queue,
			     device,
			     CH_CMD_SET_LEDS,
			     (const guint8 *) buffer,
			     sizeof (buffer),
			     NULL,
			     0);
}

/**
 * ch_device_queue_get_pcb_errata:
 **/
void
ch_device_queue_get_pcb_errata (ChDeviceQueue *device_queue,
				GUsbDevice *device,
				guint16 *pcb_errata)
{
	g_return_if_fail (CH_IS_DEVICE_QUEUE (device_queue));
	g_return_if_fail (G_USB_IS_DEVICE (device));
	g_return_if_fail (pcb_errata != NULL);

	*pcb_errata = CH_PCB_ERRATA_NONE;
	ch_device_queue_add (device_queue,
			     device,
			     CH_CMD_GET_PCB_ERRATA,
			     NULL,
			     0,
			     (guint8 *) pcb_errata,
			     sizeof (guint16));
}

/**
 * ch_device_queue_set_pcb_errata:
 **/
void
ch_device_queue_set_pcb_errata (ChDeviceQueue *device_queue,
				GUsbDevice *device,
				guint16 pcb_errata)
{
	g_return_if_fail (CH_IS_DEVICE_QUEUE (device_queue));
	g_return_if_fail (G_USB_IS_DEVICE (device));

	ch_device_queue_add (device_queue,
			     device,
			     CH_CMD_SET_PCB_ERRATA,
			     (const guint8 *) &pcb_errata,
			     sizeof (guint16),
			     NULL,
			     0);
}

/**
 * ch_device_queue_write_eeprom:
 **/
void
ch_device_queue_write_eeprom (ChDeviceQueue *device_queue,
			      GUsbDevice *device,
			      const gchar *magic)
{
	g_return_if_fail (CH_IS_DEVICE_QUEUE (device_queue));
	g_return_if_fail (G_USB_IS_DEVICE (device));
	g_return_if_fail (magic != NULL);

	ch_device_queue_add (device_queue,
			     device,
			     CH_CMD_WRITE_EEPROM,
			     (const guint8 *) magic,
			     strlen(magic),
			     NULL,
			     0);
}

/**
 * ch_device_queue_buffer_dark_offsets_cb:
 **/
static gboolean
ch_device_queue_buffer_dark_offsets_cb (guint8 *output_buffer,
					gpointer user_data,
					GError **error)
{
	CdColorRGB *value = (CdColorRGB *) user_data;
	guint16 *buffer = (guint16 *) output_buffer;

	/* convert back into floating point */
	value->R = (gdouble) buffer[0] / (gdouble) 0xffff;
	value->G = (gdouble) buffer[1] / (gdouble) 0xffff;
	value->B = (gdouble) buffer[2] / (gdouble) 0xffff;

	/* yes, we own this */
	g_free (buffer);
	return TRUE;
}

/**
 * ch_device_queue_get_dark_offsets:
 **/
void
ch_device_queue_get_dark_offsets (ChDeviceQueue *device_queue,
				  GUsbDevice *device,
				  CdColorRGB *value)
{
	guint8 *buffer;

	g_return_if_fail (CH_IS_DEVICE_QUEUE (device_queue));
	g_return_if_fail (G_USB_IS_DEVICE (device));
	g_return_if_fail (value != NULL);

	buffer = g_new0 (guint8, sizeof(guint16) * 3);
	ch_device_queue_add_internal (device_queue,
				      device,
				      CH_CMD_GET_DARK_OFFSETS,
				      NULL,
				      0,
				      buffer,
				      sizeof(guint16) * 3,
				      ch_device_queue_buffer_dark_offsets_cb,
				      value);
}

/**
 * ch_device_queue_set_dark_offsets:
 **/
void
ch_device_queue_set_dark_offsets (ChDeviceQueue *device_queue,
				  GUsbDevice *device,
				  CdColorRGB *value)
{
	guint16 buffer[3];

	g_return_if_fail (CH_IS_DEVICE_QUEUE (device_queue));
	g_return_if_fail (G_USB_IS_DEVICE (device));

	buffer[0] = value->R * (gdouble) 0xffff;
	buffer[1] = value->G * (gdouble) 0xffff;
	buffer[2] = value->B * (gdouble) 0xffff;
	ch_device_queue_add (device_queue,
			     device,
			     CH_CMD_SET_DARK_OFFSETS,
			     (const guint8 *) buffer,
			     sizeof(buffer),
			     NULL,
			     0);
}

/**
 * ch_device_queue_take_reading_raw:
 **/
void
ch_device_queue_take_reading_raw (ChDeviceQueue *device_queue,
				  GUsbDevice *device,
				  guint16 *take_reading)
{
	g_return_if_fail (CH_IS_DEVICE_QUEUE (device_queue));
	g_return_if_fail (G_USB_IS_DEVICE (device));
	g_return_if_fail (take_reading != NULL);

	ch_device_queue_add_internal (device_queue,
				      device,
				      CH_CMD_TAKE_READING_RAW,
				      NULL,
				      0,
				      (guint8 *) take_reading,
				      sizeof(guint16),
				      ch_device_queue_buffer_uint16_from_le_cb,
				      NULL);
}

/**
 * ch_device_queue_buffer_triple_rgb_cb:
 **/
static gboolean
ch_device_queue_buffer_triple_rgb_cb (guint8 *output_buffer,
				      gpointer user_data,
				      GError **error)
{
	CdColorRGB *value = (CdColorRGB *) user_data;
	ChPackedFloat *buffer = (ChPackedFloat *) output_buffer;

	/* convert back into floating point */
	ch_packed_float_to_double (&buffer[0], &value->R);
	ch_packed_float_to_double (&buffer[1], &value->G);
	ch_packed_float_to_double (&buffer[2], &value->B);

	/* yes, we own this */
	g_free (buffer);
	return TRUE;
}

/**
 * ch_device_queue_take_readings:
 **/
void
ch_device_queue_take_readings (ChDeviceQueue *device_queue,
			       GUsbDevice *device,
			       CdColorRGB *value)
{
	guint8 *buffer;

	g_return_if_fail (CH_IS_DEVICE_QUEUE (device_queue));
	g_return_if_fail (G_USB_IS_DEVICE (device));
	g_return_if_fail (value != NULL);

	buffer = g_new0 (guint8, sizeof(ChPackedFloat) * 3);
	ch_device_queue_add_internal (device_queue,
				      device,
				      CH_CMD_TAKE_READINGS,
				      NULL,
				      0,
				      buffer,
				      sizeof(ChPackedFloat) * 3,
				      ch_device_queue_buffer_triple_rgb_cb,
				      value);
}

/**
 * ch_device_queue_buffer_triple_xyz_cb:
 **/
static gboolean
ch_device_queue_buffer_triple_xyz_cb (guint8 *output_buffer,
				      gpointer user_data,
				      GError **error)
{
	CdColorXYZ *value = (CdColorXYZ *) user_data;
	ChPackedFloat *buffer = (ChPackedFloat *) output_buffer;

	/* convert back into floating point */
	ch_packed_float_to_double (&buffer[0], &value->X);
	ch_packed_float_to_double (&buffer[1], &value->Y);
	ch_packed_float_to_double (&buffer[2], &value->Z);

	/* yes, we own this */
	g_free (buffer);
	return TRUE;
}

/**
 * ch_device_queue_take_readings_xyz:
 **/
void
ch_device_queue_take_readings_xyz (ChDeviceQueue *device_queue,
				   GUsbDevice *device,
				   guint16 calibration_index,
				   CdColorXYZ *value)
{
	guint8 *buffer;

	g_return_if_fail (CH_IS_DEVICE_QUEUE (device_queue));
	g_return_if_fail (G_USB_IS_DEVICE (device));
	g_return_if_fail (value != NULL);

	buffer = g_new0 (guint8, sizeof(ChPackedFloat) * 3);
	ch_device_queue_add_internal (device_queue,
				     device,
				     CH_CMD_TAKE_READING_XYZ,
				     (guint8 *) &calibration_index,
				     sizeof(guint16),
				     buffer,
				     sizeof(ChPackedFloat) * 3,
				     ch_device_queue_buffer_triple_xyz_cb,
				     value);
}

/**
 * ch_device_queue_reset:
 **/
void
ch_device_queue_reset (ChDeviceQueue *device_queue,
		       GUsbDevice *device)
{
	g_return_if_fail (CH_IS_DEVICE_QUEUE (device_queue));
	g_return_if_fail (G_USB_IS_DEVICE (device));

	ch_device_queue_add (device_queue,
			     device,
			     CH_CMD_RESET,
			     NULL,
			     0,
			     NULL,
			     0);
}

/**
 * ch_device_queue_calculate_checksum:
 **/
static guint8
ch_device_queue_calculate_checksum (guint8 *data,
				    gsize len)
{
	guint8 checksum = 0xff;
	guint i;
	for (i = 0; i < len; i++)
		checksum ^= data[i];
	return checksum;
}

/**
 * ch_device_queue_write_flash:
 **/
void
ch_device_queue_write_flash (ChDeviceQueue *device_queue,
			     GUsbDevice *device,
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
	buffer_tx[3] = ch_device_queue_calculate_checksum (data, len);
	memcpy (buffer_tx + 4, data, len);

	ch_device_queue_add (device_queue,
			     device,
			     CH_CMD_WRITE_FLASH,
			     buffer_tx,
			     len + 4,
			     NULL,
			     0);
}

/* tiny helper */
typedef struct {
	guint16		 address;
	guint8		*data;
	gsize		 len;
} ChDeviceQueueReadFlashHelper;

/**
 * ch_device_queue_buffer_read_flash_cb:
 **/
static gboolean
ch_device_queue_buffer_read_flash_cb (guint8 *output_buffer,
				      gpointer user_data,
				      GError **error)
{
	ChDeviceQueueReadFlashHelper *helper = (ChDeviceQueueReadFlashHelper *) user_data;
	gboolean ret = TRUE;
	guint8 expected_checksum;

	/* verify checksum */
	expected_checksum = ch_device_queue_calculate_checksum (output_buffer + 1,
								helper->len);
	if (output_buffer[0] != expected_checksum) {
		ret = FALSE;
		g_set_error (error, 1, 0,
			     "Checksum @0x%04x invalid",
			     helper->address);
		goto out;
	}

	/* copy data to final location */
	memcpy (helper->data, output_buffer + 1, helper->len);
out:
	g_free (output_buffer);
	g_free (helper);
	return ret;
}

/**
 * ch_device_queue_read_flash:
 **/
void
ch_device_queue_read_flash (ChDeviceQueue *device_queue,
			    GUsbDevice *device,
			    guint16 address,
			    guint8 *data,
			    gsize len)
{
	ChDeviceQueueReadFlashHelper *helper;
	guint16 addr_le;
	guint8 *buffer;
	guint8 buffer_tx[3];

	/* set address, length, checksum, data */
	addr_le = GUINT16_TO_LE (address);
	memcpy (buffer_tx + 0, &addr_le, 2);
	buffer_tx[2] = len;

	/* create a helper structure as the checksum needs an extra
	 * byte for the checksum */
	helper = g_new0 (ChDeviceQueueReadFlashHelper, 1);
	helper->data = data;
	helper->len = len;
	helper->address = address;

	buffer = g_new0 (guint8, len + 1);
	ch_device_queue_add_internal (device_queue,
				      device,
				      CH_CMD_READ_FLASH,
				      buffer_tx,
				      sizeof(buffer_tx),
				      buffer,
				      len + 1,
				      ch_device_queue_buffer_read_flash_cb,
				      helper);
}

/**
 * ch_device_queue_buffer_verify_flash_cb:
 **/
static gboolean
ch_device_queue_buffer_verify_flash_cb (guint8 *output_buffer,
					gpointer user_data,
					GError **error)
{
	ChDeviceQueueReadFlashHelper *helper = (ChDeviceQueueReadFlashHelper *) user_data;
	gboolean ret = TRUE;
	guint8 expected_checksum;

	/* verify checksum */
	expected_checksum = ch_device_queue_calculate_checksum (output_buffer + 1,
								helper->len);
	if (output_buffer[0] != expected_checksum) {
		ret = FALSE;
		g_set_error (error, 1, 0,
			     "Checksum @0x%04x invalid",
			     helper->address);
		goto out;
	}

	/* verify data */
	if (memcmp (helper->data,
		    output_buffer + 1,
		    helper->len) != 0) {
		ret = FALSE;
		g_set_error (error, 1, 0,
			     "Failed to verify at @0x%04x",
			     helper->address);
		goto out;
	}
out:
	g_free (output_buffer);
	g_free (helper);
	return ret;
}

/**
 * ch_device_queue_verify_flash:
 **/
void
ch_device_queue_verify_flash (ChDeviceQueue *device_queue,
			      GUsbDevice *device,
			      guint16 address,
			      const guint8 *data,
			      gsize len)
{
	ChDeviceQueueReadFlashHelper *helper;
	guint16 addr_le;
	guint8 *buffer;
	guint8 buffer_tx[3];

	/* set address, length, checksum, data */
	addr_le = GUINT16_TO_LE (address);
	memcpy (buffer_tx + 0, &addr_le, 2);
	buffer_tx[2] = len;

	/* create a helper structure as the checksum needs an extra
	 * byte for the checksum */
	helper = g_new0 (ChDeviceQueueReadFlashHelper, 1);
	helper->data = (guint8 *) data;
	helper->len = len;
	helper->address = address;

	buffer = g_new0 (guint8, len + 1);
	ch_device_queue_add_internal (device_queue,
				      device,
				      CH_CMD_READ_FLASH,
				      buffer_tx,
				      sizeof(buffer_tx),
				      buffer,
				      len + 1,
				      ch_device_queue_buffer_verify_flash_cb,
				      helper);
}

/**
 * ch_device_queue_erase_flash:
 **/
void
ch_device_queue_erase_flash (ChDeviceQueue *device_queue,
			     GUsbDevice *device,
			     guint16 address,
			     gsize len)
{
	guint8 buffer_tx[4];
	guint16 addr_le;
	guint16 len_le;

	/* set address, length, checksum, data */
	addr_le = GUINT16_TO_LE (address);
	memcpy (buffer_tx + 0, &addr_le, 2);
	len_le = GUINT16_TO_LE (len);
	memcpy (buffer_tx + 2, &len_le, 2);

	ch_device_queue_add (device_queue,
			     device,
			     CH_CMD_ERASE_FLASH,
			     buffer_tx,
			     sizeof(buffer_tx),
			     NULL,
			     0);
}

/**
 * ch_device_queue_set_flash_success:
 **/
void
ch_device_queue_set_flash_success (ChDeviceQueue *device_queue,
				   GUsbDevice *device,
				   guint8 value)
{
	g_return_if_fail (CH_IS_DEVICE_QUEUE (device_queue));
	g_return_if_fail (G_USB_IS_DEVICE (device));

	/* set flash success true */
	ch_device_queue_add (device_queue,
			     device,
			     CH_CMD_SET_FLASH_SUCCESS,
			     (guint8 *) &value, 1,
			     NULL, 0);
}

/**
 * ch_device_queue_boot_flash:
 **/
void
ch_device_queue_boot_flash (ChDeviceQueue *device_queue,
			    GUsbDevice *device)
{
	g_return_if_fail (CH_IS_DEVICE_QUEUE (device_queue));
	g_return_if_fail (G_USB_IS_DEVICE (device));

	/* boot into new code */
	ch_device_queue_add (device_queue,
			     device,
			     CH_CMD_BOOT_FLASH,
			     NULL, 0,
			     NULL, 0);
}

/**
 * ch_device_queue_get_hardware_version:
 **/
void
ch_device_queue_get_hardware_version (ChDeviceQueue *device_queue,
				      GUsbDevice *device,
				      guint8 *hw_version)
{
	g_return_if_fail (CH_IS_DEVICE_QUEUE (device_queue));
	g_return_if_fail (G_USB_IS_DEVICE (device));
	g_return_if_fail (hw_version != NULL);

	ch_device_queue_add (device_queue,
			     device,
			     CH_CMD_GET_HARDWARE_VERSION,
			     NULL,
			     0,
			     hw_version,
			     1);
}

/**
 * ch_device_queue_get_owner_name:
 **/
void
ch_device_queue_get_owner_name (ChDeviceQueue *device_queue,
				GUsbDevice *device,
				gchar *name)
{
	g_return_if_fail (CH_IS_DEVICE_QUEUE (device_queue));
	g_return_if_fail (G_USB_IS_DEVICE (device));
	g_return_if_fail (name != NULL);

	ch_device_queue_add (device_queue,
			     device,
			     CH_CMD_GET_OWNER_NAME,
			     NULL,
			     0,
			     (guint8 *) name,
			     sizeof(gchar) * CH_OWNER_LENGTH_MAX);
	name[CH_OWNER_LENGTH_MAX-1] = 0;
}

/**
 * ch_device_queue_set_owner_name:
 **/
void
ch_device_queue_set_owner_name (ChDeviceQueue *device_queue,
				GUsbDevice *device,
				const gchar *name)
{
	gchar buf[CH_OWNER_LENGTH_MAX];

	g_return_if_fail (CH_IS_DEVICE_QUEUE (device_queue));
	g_return_if_fail (G_USB_IS_DEVICE (device));
	g_return_if_fail (name != NULL);

	memset(buf, 0, CH_OWNER_LENGTH_MAX);
	g_strlcpy(buf, name, CH_OWNER_LENGTH_MAX);

	ch_device_queue_add (device_queue,
			     device,
			     CH_CMD_SET_OWNER_NAME,
			     (const guint8 *) buf,
			     sizeof(gchar) * CH_OWNER_LENGTH_MAX,
			     NULL,
			     0);
}

/**
 * ch_device_queue_get_owner_email:
 **/
void
ch_device_queue_get_owner_email (ChDeviceQueue *device_queue,
				 GUsbDevice *device,
				 gchar *email)
{
	g_return_if_fail (CH_IS_DEVICE_QUEUE (device_queue));
	g_return_if_fail (G_USB_IS_DEVICE (device));
	g_return_if_fail (email != NULL);

	ch_device_queue_add (device_queue,
			     device,
			     CH_CMD_GET_OWNER_EMAIL,
			     NULL,
			     0,
			     (guint8 *) email,
			     sizeof(gchar) * CH_OWNER_LENGTH_MAX);
	email[CH_OWNER_LENGTH_MAX-1] = 0;
}

/**
 * ch_device_queue_set_owner_email:
 **/
void
ch_device_queue_set_owner_email (ChDeviceQueue *device_queue,
				 GUsbDevice *device,
				 const gchar *email)
{
	gchar buf[CH_OWNER_LENGTH_MAX];

	g_return_if_fail (CH_IS_DEVICE_QUEUE (device_queue));
	g_return_if_fail (G_USB_IS_DEVICE (device));
	g_return_if_fail (email != NULL);

	memset (buf, 0, CH_OWNER_LENGTH_MAX);
	g_strlcpy (buf, email, CH_OWNER_LENGTH_MAX);

	ch_device_queue_add (device_queue,
			     device,
			     CH_CMD_SET_OWNER_EMAIL,
			     (const guint8 *) buf,
			     sizeof(gchar) * CH_OWNER_LENGTH_MAX,
			     NULL,
			     0);
}

/**
 * ch_device_queue_take_reading_array:
 **/
void
ch_device_queue_take_reading_array (ChDeviceQueue *device_queue,
				    GUsbDevice *device,
				    guint8 *reading_array)
{
	g_return_if_fail (CH_IS_DEVICE_QUEUE (device_queue));
	g_return_if_fail (G_USB_IS_DEVICE (device));
	g_return_if_fail (reading_array != NULL);

	ch_device_queue_add (device_queue,
			     device,
			     CH_CMD_TAKE_READING_ARRAY,
			     NULL,
			     0,
			     reading_array,
			     30);
}

/**********************************************************************/

/**
 * ch_device_queue_class_init:
 **/
static void
ch_device_queue_class_init (ChDeviceQueueClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = ch_device_queue_finalize;

	/**
	 * ChDeviceQueueClass::device-failed:
	 * @device_queue: the #ChDeviceQueue instance that emitted the signal
	 * @device: the device that failed
	 * @error_message: the error that caused the failure
	 *
	 * The ::device-failed signal is emitted when a device has failed.
	 **/
	signals[SIGNAL_DEVICE_FAILED] =
		g_signal_new ("device-failed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (ChDeviceQueueClass, device_failed),
			      NULL, NULL, g_cclosure_marshal_generic,
			      G_TYPE_NONE, 2, G_TYPE_OBJECT, G_TYPE_STRING);

	/**
	 * ChDeviceQueueClass::progress-changed:
	 * @device_queue: the #ChDeviceQueue instance that emitted the signal
	 * @percentage: the percentage complete the action is
	 * @error_message: the error that caused the failure
	 *
	 * The ::progress-changed signal is emitted when a the commands
	 * are being submitted.
	 **/
	signals[SIGNAL_PROGRESS_CHANGED] =
		g_signal_new ("progress-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (ChDeviceQueueClass, progress_changed),
			      NULL, NULL, g_cclosure_marshal_generic,
			      G_TYPE_NONE, 1, G_TYPE_UINT);

	g_type_class_add_private (klass, sizeof (ChDeviceQueuePrivate));
}

/**
 * ch_device_queue_init:
 **/
static void
ch_device_queue_init (ChDeviceQueue *device_queue)
{
	device_queue->priv = CH_DEVICE_QUEUE_GET_PRIVATE (device_queue);
	device_queue->priv->data_array = g_ptr_array_new_with_free_func ((GDestroyNotify) ch_device_queue_data_free);
	device_queue->priv->devices_in_use = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
}

/**
 * ch_device_queue_finalize:
 **/
static void
ch_device_queue_finalize (GObject *object)
{
	ChDeviceQueue *device_queue = CH_DEVICE_QUEUE (object);
	ChDeviceQueuePrivate *priv = device_queue->priv;

	g_ptr_array_unref (priv->data_array);
	g_hash_table_unref (priv->devices_in_use);

	G_OBJECT_CLASS (ch_device_queue_parent_class)->finalize (object);
}

/**
 * ch_device_queue_new:
 **/
ChDeviceQueue *
ch_device_queue_new (void)
{
	ChDeviceQueue *device_queue;
	device_queue = g_object_new (CH_TYPE_DEVICE_QUEUE, NULL);
	return CH_DEVICE_QUEUE (device_queue);
}
