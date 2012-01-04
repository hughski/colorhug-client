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
#include <gusb.h>

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

GUsbDevice	*ch_client_get_default		(ChClient	*client,
						 GError		**error);
gboolean	 ch_client_flash_firmware	(ChClient	*client,
						 const gchar	*filename,
						 GError		**error);

G_END_DECLS

#endif /* __CH_CLIENT_H */
