/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
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
#include <colorhug.h>

#include "ch-ambient.h"
#include "ch-cleanup.h"

static void	ch_ambient_finalize	(GObject     *object);

#define CH_AMBIENT_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), CH_TYPE_AMBIENT, ChAmbientPrivate))

/**
 * ChAmbientPrivate:
 *
 * Private #ChAmbient data
 **/
struct _ChAmbientPrivate
{
	ChAmbientKind		 kind;
	GSettings		*settings;
	GUsbContext		*usb_ctx;	/* watching USB devices */
	GUsbDevice		*device;	/* selected ColorHug */
	ChDeviceQueue		*device_queue;	/* ColorHug command queue */
	GFile			*acpi_internal;	/* internal device */
};

enum {
	SIGNAL_CHANGED,
	SIGNAL_LAST
};

G_DEFINE_TYPE (ChAmbient, ch_ambient, G_TYPE_OBJECT)

typedef struct {
	ChAmbient		*ambient;
	GCancellable		*cancellable;
	GSimpleAsyncResult	*res;
	guint32			 data[4];
} ChAmbientHelper;

static guint signals[SIGNAL_LAST] = { 0 };

/**
 * ch_ambient_free_helper:
 **/
static void
ch_ambient_free_helper (ChAmbientHelper *helper)
{
	if (helper->cancellable != NULL)
		g_object_unref (helper->cancellable);
	g_object_unref (helper->ambient);
	g_object_unref (helper->res);
	g_free (helper);
}

/**
 * ch_backlight_take_reading_cb:
 **/
static void
ch_backlight_take_reading_cb (GObject *source, GAsyncResult *res, gpointer user_data)
{
	GdkRGBA *rgba;
	ChAmbientHelper *helper = (ChAmbientHelper *) user_data;
	_cleanup_error_free_ GError *error = NULL;

	/* get result */
	if (!ch_device_queue_process_finish (CH_DEVICE_QUEUE (source), res, &error)) {
		g_simple_async_result_set_from_error (helper->res, error);
		g_simple_async_result_complete_in_idle (helper->res);
		ch_ambient_free_helper (helper);
		return;
	}

	/* copy out of the helper */
	rgba = g_new (GdkRGBA, 1);
	rgba->alpha = helper->data[0];
	rgba->red = helper->data[1];
	rgba->green = helper->data[2];
	rgba->blue = helper->data[3];
	g_simple_async_result_set_op_res_gpointer (helper->res, rgba, g_free);
	g_simple_async_result_complete_in_idle (helper->res);
	ch_ambient_free_helper (helper);
}

/**
 * ch_ambient_file_read_cb:
 **/
static void
ch_ambient_file_read_cb (GObject *source, GAsyncResult *res, gpointer user_data)
{
	ChAmbientHelper *helper = (ChAmbientHelper *) user_data;
	GdkRGBA *rgba;
	GFileInputStream *stream;
	gboolean ret;
	gchar buffer[256];
	gsize size = 0;
	_cleanup_error_free_ GError *error = NULL;

	/* get result */
	stream = g_file_read_finish (G_FILE (source), res, &error);
	if (stream == NULL) {
		g_simple_async_result_set_from_error (helper->res, error);
		g_simple_async_result_complete_in_idle (helper->res);
		ch_ambient_free_helper (helper);
		return;
	}

	/* read entire file */
	ret = g_input_stream_read_all (G_INPUT_STREAM (stream),
				       buffer, sizeof (buffer),
				       &size, helper->cancellable, &error);
	if (!ret) {
		g_simple_async_result_set_from_error (helper->res, error);
		g_simple_async_result_complete_in_idle (helper->res);
		ch_ambient_free_helper (helper);
		return;
	}

	/* copy out of the helper */
	rgba = g_new (GdkRGBA, 1);
	rgba->alpha = g_ascii_strtod (buffer, NULL);;
	rgba->red = 0.f;
	rgba->green = 0.f;
	rgba->blue = 0.f;
	g_simple_async_result_set_op_res_gpointer (helper->res, rgba, g_free);
	g_simple_async_result_complete_in_idle (helper->res);
	ch_ambient_free_helper (helper);
}

/**
 * ch_ambient_get_value_async:
 **/
void
ch_ambient_get_value_async (ChAmbient *ambient,
			    GCancellable *cancellable,
			    GAsyncReadyCallback callback,
			    gpointer user_data)
{
	ChAmbientHelper *helper;
	ChAmbientPrivate *priv = ambient->priv;

	g_return_if_fail (CH_IS_AMBIENT (ambient));
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	helper = g_new0 (ChAmbientHelper, 1);
	helper->ambient = g_object_ref (ambient);
	helper->res = g_simple_async_result_new (G_OBJECT (ambient),
						 callback,
						 user_data,
						 ch_ambient_get_value_async);
	if (cancellable != NULL)
		helper->cancellable = g_object_ref (cancellable);

	/* take readings */
	switch (priv->kind) {
	case CH_AMBIENT_KIND_INTERNAL:
		g_file_read_async (priv->acpi_internal,
				   G_PRIORITY_DEFAULT, cancellable,
				   ch_ambient_file_read_cb, ambient);
		break;
	case CH_AMBIENT_KIND_COLORHUG:
		ch_device_queue_set_color_select (priv->device_queue,
						  priv->device,
						  CH_COLOR_SELECT_WHITE);
		ch_device_queue_take_reading_raw (priv->device_queue,
						  priv->device,
						  &helper->data[0]);
		ch_device_queue_set_color_select (priv->device_queue,
						  priv->device,
						  CH_COLOR_SELECT_RED);
		ch_device_queue_take_reading_raw (priv->device_queue,
						  priv->device,
						  &helper->data[1]);
		ch_device_queue_set_color_select (priv->device_queue,
						  priv->device,
						  CH_COLOR_SELECT_GREEN);
		ch_device_queue_take_reading_raw (priv->device_queue,
						  priv->device,
						  &helper->data[2]);
		ch_device_queue_set_color_select (priv->device_queue,
						  priv->device,
						  CH_COLOR_SELECT_BLUE);
		ch_device_queue_take_reading_raw (priv->device_queue,
						  priv->device,
						  &helper->data[3]);
		ch_device_queue_process_async (priv->device_queue,
					       CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
					       cancellable,
					       ch_backlight_take_reading_cb,
					       helper);
		break;
	default:
		g_simple_async_result_set_error (helper->res,
						 G_IO_ERROR,
						 G_IO_ERROR_NOT_SUPPORTED,
						 "%s", "no supported hardware found");
		g_simple_async_result_complete_in_idle (helper->res);
		ch_ambient_free_helper (helper);
		break;
	}
}

/**
 * ch_ambient_get_value_finish:
 **/
GdkRGBA *
ch_ambient_get_value_finish (ChAmbient *ambient,
			     GAsyncResult *res,
			     GError **error)
{
	GSimpleAsyncResult *simple;

	g_return_val_if_fail (CH_AMBIENT (ambient), FALSE);
	g_return_val_if_fail (G_IS_SIMPLE_ASYNC_RESULT (res), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (res);
	if (g_simple_async_result_propagate_error (simple, error))
		return NULL;

	return g_simple_async_result_get_op_res_gpointer (simple);
}

/**
 * ch_ambient_get_kind:
 **/
ChAmbientKind
ch_ambient_get_kind (ChAmbient *ambient)
{
	ChAmbientPrivate *priv = ambient->priv;
	return priv->kind;
}

/**
 * ch_ambient_class_init:
 **/
static void
ch_ambient_class_init (ChAmbientClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = ch_ambient_finalize;

	signals[SIGNAL_CHANGED] =
		g_signal_new ("changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (ChAmbientClass, changed),
			      NULL, NULL, g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);

	g_type_class_add_private (klass, sizeof (ChAmbientPrivate));
}

/**
 * ch_ambient_device_added_cb:
 **/
static void
ch_ambient_device_added_cb (GUsbContext *ctx, GUsbDevice *device, ChAmbient *ambient)
{
	guint16 integral_time;
	ChAmbientPrivate *priv = ambient->priv;
	_cleanup_error_free_ GError *error = NULL;

	switch (ch_device_get_mode (device)) {
	case CH_DEVICE_MODE_FIRMWARE_ALS:

		/* we've got an internal ALS sensor */
		if (priv->kind != CH_AMBIENT_KIND_NONE) {
			g_warning ("ignoring device as already have sensor");
			return;
		}

		/* open device */
		if (!ch_device_open (device, &error)) {
			g_warning ("Failed to open device: %s", error->message);
			return;
		}
		priv->device = g_object_ref (device);
		priv->kind = CH_AMBIENT_KIND_COLORHUG;

		/* these stay constant */
		ch_device_queue_set_multiplier (priv->device_queue, priv->device,
						CH_FREQ_SCALE_100);
		integral_time = 0xffff * g_settings_get_double (priv->settings, "integration");
		ch_device_queue_set_integral_time (priv->device_queue, priv->device,
						   integral_time);
		g_signal_emit (ambient, signals[SIGNAL_CHANGED], 0);
		break;
	default:
		break;
	}
}

/**
 * ch_ambient_device_removed_cb:
 **/
static void
ch_ambient_device_removed_cb (GUsbContext *ctx, GUsbDevice *device, ChAmbient *ambient)
{
	ChAmbientPrivate *priv = ambient->priv;
	switch (ch_device_get_mode (device)) {
	case CH_DEVICE_MODE_FIRMWARE_ALS:
		/* only set back to NONE if we are in COLORHUG mode */
		if (priv->kind == CH_AMBIENT_KIND_COLORHUG) {
			if (priv->device != NULL)
				g_object_unref (priv->device);
			priv->device = NULL;
			priv->kind = CH_AMBIENT_KIND_NONE;
			g_signal_emit (ambient, signals[SIGNAL_CHANGED], 0);
		}
		break;
	default:
		break;
	}
}

/**
 * ch_ambient_enumerate:
 **/
void
ch_ambient_enumerate (ChAmbient *ambient)
{
	/* is the device already plugged in? */
	g_usb_context_enumerate (ambient->priv->usb_ctx);
}

/**
 * ch_ambient_find_acpi_internal:
 **/
static GFile *
ch_ambient_find_acpi_internal (void)
{
	const gchar *tmp;
	_cleanup_free_ gchar *dev = NULL;
	_cleanup_dir_close_ GDir *dir = NULL;
	dir = g_dir_open ("/sys/class/als", 0, NULL);
	if (dir == NULL)
		return NULL;
	tmp = g_dir_read_name (dir);
	if (tmp == NULL)
		return NULL;
	dev = g_build_filename (tmp, "illuminance", NULL);
	if (!g_file_test (dev, G_FILE_TEST_EXISTS))
		return NULL;
	return g_file_new_for_path (dev);
}

/**
 * ch_ambient_init:
 **/
static void
ch_ambient_init (ChAmbient *ambient)
{
	ambient->priv = CH_AMBIENT_GET_PRIVATE (ambient);
	ambient->priv->settings = g_settings_new ("com.hughski.ColorHug.Backlight");
	ambient->priv->usb_ctx = g_usb_context_new (NULL);
	ambient->priv->device_queue = ch_device_queue_new ();
	g_signal_connect (ambient->priv->usb_ctx, "device-added",
			  G_CALLBACK (ch_ambient_device_added_cb), ambient);
	g_signal_connect (ambient->priv->usb_ctx, "device-removed",
			  G_CALLBACK (ch_ambient_device_removed_cb), ambient);

	/* internal device support does not support hotplug */
	ambient->priv->acpi_internal = ch_ambient_find_acpi_internal ();
	if (ambient->priv->acpi_internal != NULL)
		ambient->priv->kind = CH_AMBIENT_KIND_INTERNAL;
}

/**
 * ch_ambient_finalize:
 **/
static void
ch_ambient_finalize (GObject *object)
{
	ChAmbient *ambient = CH_AMBIENT (object);
	ChAmbientPrivate *priv = ambient->priv;

	g_object_unref (priv->acpi_internal);
	g_object_unref (priv->settings);
	g_object_unref (priv->usb_ctx);
	g_object_unref (priv->device_queue);

	G_OBJECT_CLASS (ch_ambient_parent_class)->finalize (object);
}

/**
 * ch_ambient_new:
 **/
ChAmbient *
ch_ambient_new (void)
{
	ChAmbient *ambient;
	ambient = g_object_new (CH_TYPE_AMBIENT, NULL);
	return CH_AMBIENT (ambient);
}
