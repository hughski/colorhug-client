/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2006-2012 Richard Hughes <richard@hughsie.com>
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

#ifndef __CH_GRAPH_WIDGET_H__
#define __CH_GRAPH_WIDGET_H__

#include <gtk/gtk.h>

#include "ch-point-obj.h"

G_BEGIN_DECLS

#define CH_TYPE_GRAPH_WIDGET		(ch_graph_widget_get_type ())
#define CH_GRAPH_WIDGET(obj)		(G_TYPE_CHECK_INSTANCE_CAST ((obj), CH_TYPE_GRAPH_WIDGET, ChGraphWidget))
#define CH_GRAPH_WIDGET_CLASS(obj)	(G_TYPE_CHECK_CLASS_CAST ((obj), CH_GRAPH_WIDGET, ChGraphWidgetClass))
#define CH_IS_GRAPH_WIDGET(obj)	(G_TYPE_CHECK_INSTANCE_TYPE ((obj), CH_TYPE_GRAPH_WIDGET))
#define CH_IS_GRAPH_WIDGET_CLASS(obj)	(G_TYPE_CHECK_CLASS_TYPE ((obj), EFF_TYPE_GRAPH_WIDGET))
#define CH_GRAPH_WIDGET_GET_CLASS	(G_TYPE_INSTANCE_GET_CLASS ((obj), CH_TYPE_GRAPH_WIDGET, ChGraphWidgetClass))

#define CH_GRAPH_WIDGET_LEGEND_SPACING		17

typedef struct ChGraphWidget		ChGraphWidget;
typedef struct ChGraphWidgetClass	ChGraphWidgetClass;
typedef struct ChGraphWidgetPrivate	ChGraphWidgetPrivate;

typedef enum {
	CH_GRAPH_WIDGET_TYPE_INVALID,
	CH_GRAPH_WIDGET_TYPE_PERCENTAGE,
	CH_GRAPH_WIDGET_TYPE_FACTOR,
	CH_GRAPH_WIDGET_TYPE_TIME,
	CH_GRAPH_WIDGET_TYPE_POWER,
	CH_GRAPH_WIDGET_TYPE_VOLTAGE,
	CH_GRAPH_WIDGET_TYPE_WAVELENGTH,
	CH_GRAPH_WIDGET_TYPE_UNKNOWN
} ChGraphWidgetType;

typedef enum {
	CH_GRAPH_WIDGET_PLOT_LINE,
	CH_GRAPH_WIDGET_PLOT_POINTS,
	CH_GRAPH_WIDGET_PLOT_BOTH
} ChGraphWidgetPlot;

struct ChGraphWidget
{
	GtkDrawingArea		 parent;
	ChGraphWidgetPrivate	*priv;
};

struct ChGraphWidgetClass
{
	GtkDrawingAreaClass parent_class;
};

GType		 ch_graph_widget_get_type		(void);
GtkWidget	*ch_graph_widget_new			(void);

gboolean	 ch_graph_widget_clear			(ChGraphWidget		*graph);
gboolean	 ch_graph_widget_assign			(ChGraphWidget		*graph,
							 ChGraphWidgetPlot	 plot,
							 GPtrArray		*array);

G_END_DECLS

#endif
