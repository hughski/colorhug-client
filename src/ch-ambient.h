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

#ifndef __CH_AMBIENT_H
#define __CH_AMBIENT_H

#include <glib-object.h>
#include <gdk/gdk.h>

G_BEGIN_DECLS

#define CH_TYPE_AMBIENT		(ch_ambient_get_type ())
#define CH_AMBIENT(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), CH_TYPE_AMBIENT, ChAmbient))
#define CH_AMBIENT_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), CH_TYPE_AMBIENT, ChAmbientClass))
#define CH_IS_AMBIENT(o)	(G_TYPE_CHECK_INSTANCE_TYPE ((o), CH_TYPE_AMBIENT))

typedef struct _ChAmbientPrivate	ChAmbientPrivate;
typedef struct _ChAmbient		ChAmbient;
typedef struct _ChAmbientClass		ChAmbientClass;

struct _ChAmbient
{
	 GObject		 parent;
	 ChAmbientPrivate	*priv;
};

struct _ChAmbientClass
{
	GObjectClass		 parent_class;
	/* signals */
	void			(* changed)	(ChAmbient	*ambient);
};

typedef enum {
	CH_AMBIENT_KIND_NONE,
	CH_AMBIENT_KIND_INTERNAL,
	CH_AMBIENT_KIND_COLORHUG,
	CH_AMBIENT_KIND_LAST
} ChAmbientKind;

GType		 ch_ambient_get_type		(void);
ChAmbient	*ch_ambient_new			(void);

ChAmbientKind	 ch_ambient_get_kind		(ChAmbient	*ambient);
void		 ch_ambient_enumerate		(ChAmbient	*ambient);
void		 ch_ambient_get_value_async	(ChAmbient	*ambient,
						 GCancellable	*cancellable,
						 GAsyncReadyCallback callback,
						 gpointer	 user_data);
GdkRGBA		*ch_ambient_get_value_finish	(ChAmbient	*ambient,
						 GAsyncResult	*res,
						 GError		**error);

G_END_DECLS

#endif /* __CH_AMBIENT_H */
