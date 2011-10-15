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
	GKeyFile			*keyfile;
};

G_DEFINE_TYPE (ChClient, ch_client, G_TYPE_OBJECT)

/**
 * ch_client_load:
 **/
gboolean
ch_client_load (ChClient *client, GError **error)
{
	g_return_val_if_fail (CH_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
	return TRUE;
}


/**
 * ch_client_get_color_select:
 **/
gboolean
ch_client_get_color_select (ChClient *client,
			    ChColorSelect *color_select,
			    GError **error)
{
	g_return_val_if_fail (CH_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (color_select != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
	return TRUE;
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
	client->priv->keyfile = g_key_file_new ();
}

/**
 * ch_client_finalize:
 **/
static void
ch_client_finalize (GObject *object)
{
	ChClient *client = CH_CLIENT (object);
	ChClientPrivate *priv = client->priv;

	g_key_file_free (priv->keyfile);

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

