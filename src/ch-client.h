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

#ifndef __CH_CLIENT_H
#define __CH_CLIENT_H

#include "ch-common.h"

#include <glib-object.h>

G_BEGIN_DECLS

#define CH_TYPE_CLIENT		(ch_client_get_type ())
#define CH_CLIENT(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), CH_TYPE_CLIENT, ChClient))
#define CH_CLIENT_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), CH_TYPE_CLIENT, ChClientClass))
#define CH_IS_CLIENT(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), CH_TYPE_CLIENT))

typedef struct _ChClientPrivate		ChClientPrivate;
typedef struct _ChClient		ChClient;
typedef struct _ChClientClass		ChClientClass;

struct _ChClient
{
	 GObject		 parent;
	 ChClientPrivate	*priv;
};

struct _ChClientClass
{
	GObjectClass		 parent_class;
};

GType		 ch_client_get_type		(void);
ChClient	*ch_client_new			(void);

gboolean	 ch_client_load			(ChClient	*client,
						 GError		**error);

gboolean	 ch_client_get_color_select	(ChClient	*client,
						 ChColorSelect	*color_select,
						 GError		**error);
gboolean	 ch_client_set_color_select	(ChClient	*client,
						 ChColorSelect	 color_select,
						 GError		**error);

gboolean	 ch_client_get_multiplier	(ChClient	*client,
						 ChFreqScale	*multiplier,
						 GError		**error);
gboolean	 ch_client_set_multiplier	(ChClient	*client,
						 ChFreqScale	 multiplier,
						 GError		**error);

gboolean	 ch_client_get_integral_time	(ChClient	*client,
						 guint16	*integral_time,
						 GError		**error);
gboolean	 ch_client_set_integral_time	(ChClient	*client,
						 guint16	 integral_time,
						 GError		**error);

gboolean	 ch_client_get_firmware_ver	(ChClient	*client,
						 guint16	*major,
						 guint16	*minor,
						 guint16	*micro,
						 GError		**error);

gboolean	 ch_client_get_calibration	(ChClient	*client,
						 guint16	 calibration_index,
						 gdouble	*calibration,
						 gchar		*description,
						 GError		**error);
gboolean	 ch_client_set_calibration	(ChClient	*client,
						 guint16	 calibration_index,
						 const gdouble	*calibration,
						 const gchar	*description,
						 GError		**error);

gboolean	 ch_client_get_pre_scale	(ChClient	*client,
						 gdouble	*pre_scale,
						 GError		**error);
gboolean	 ch_client_set_pre_scale	(ChClient	*client,
						 gdouble	 pre_scale,
						 GError		**error);

gboolean	 ch_client_get_post_scale	(ChClient	*client,
						 gdouble	*post_scale,
						 GError		**error);
gboolean	 ch_client_set_post_scale	(ChClient	*client,
						 gdouble	 post_scale,
						 GError		**error);

gboolean	 ch_client_get_serial_number	(ChClient	*client,
						 guint64	*serial_number,
						 GError		**error);
gboolean	 ch_client_set_serial_number	(ChClient	*client,
						 guint64	 serial_number,
						 GError		**error);

gboolean	 ch_client_get_leds		(ChClient	*client,
						 guint8		*leds,
						 GError		**error);
gboolean	 ch_client_set_leds		(ChClient	*client,
						 guint8		 leds,
						 guint8		 repeat,
						 guint8		 on_time,
						 guint8		 off_time,
						 GError		**error);

gboolean	 ch_client_get_dark_offsets	(ChClient	*client,
						 gdouble	*red,
						 gdouble	*green,
						 gdouble	*blue,
						 GError		**error);
gboolean	 ch_client_set_dark_offsets	(ChClient	*client,
						 gdouble	 red,
						 gdouble	 green,
						 gdouble	 blue,
						 GError		**error);

gboolean	 ch_client_write_eeprom		(ChClient	*client,
						 const gchar	*magic,
						 GError		**error);

gboolean	 ch_client_take_reading_raw	(ChClient	*client,
						 guint16	*take_reading,
						 GError		**error);

gboolean	 ch_client_take_readings	(ChClient	*client,
						 gdouble	*red,
						 gdouble	*green,
						 gdouble	*blue,
						 GError		**error);
gboolean	 ch_client_take_readings_xyz	(ChClient	*client,
						 guint16	 calibration_index,
						 gdouble	*red,
						 gdouble	*green,
						 gdouble	*blue,
						 GError		**error);

gboolean	 ch_client_reset		(ChClient	*client,
						 GError		**error);

gboolean	 ch_client_flash_firmware	(ChClient	*client,
						 const gchar	*filename,
						 GError		**error);

G_END_DECLS

#endif /* __CH_CLIENT_H */
